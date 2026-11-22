#include "Viewport.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>

namespace {
using namespace platz::studio;
platz::engine::Vec3 toEngine(platz::Vec3 v) { return {v.x,v.y,v.z}; }
platz::Vec3 fromEngine(platz::engine::Vec3 v) { return {v[0],v[1],v[2]}; }
platz::engine::Mesh toEngine(platz::Mesh mesh) {
    switch (mesh) {
    case platz::Mesh::Imported: return platz::engine::Mesh::Imported;
    case platz::Mesh::Sphere: return platz::engine::Mesh::Sphere;
    case platz::Mesh::Cylinder: return platz::engine::Mesh::Cylinder;
    default: return platz::engine::Mesh::Cube;
    }
}
platz::engine::DrawObject drawObject(const platz::Object& object,std::uint32_t token) {
    return {token,toEngine(object.mesh),toEngine(object.position),toEngine(object.rotation),toEngine(object.scale),
            toEngine(object.color),object.asset.data,object.slotColors,object.texturesOff,object.images};
}
}

Viewport::Viewport(QWidget* parent) : QWidget(parent) {
    setObjectName("viewport");
    setAccessibleName(tr("Perspective viewport"));
    setAccessibleDescription(tr("1 Select, 2 Move, 3 Rotate, 4 Scale. F frames the selected object. Escape cancels a drag. Arrow keys orbit and W A S D move the camera."));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMinimumSize(200,170);
}

void Viewport::setScene(const std::vector<platz::Object>& objects, const std::string& selection) {
    releaseDrag();
    ids_.clear();
    draws_.clear();
    selection_ = 0;
    for (const auto& object : objects) {
        ids_.push_back(object.id);
        const auto token = static_cast<std::uint32_t>(ids_.size());
        draws_.push_back(drawObject(object,token));
        if (selection == object.id) selection_ = token;
    }
    update();
}

void Viewport::previewObject(const platz::Object& object) {
    if (selection_ && ids_[selection_-1]==object.id) {
        draws_[selection_-1]=drawObject(object,selection_);
        update();
    }
}
TransformValue Viewport::selectedTransform() const {
    const auto& object=draws_.at(selection_-1);
    return {fromEngine(object.position),fromEngine(object.rotation),fromEngine(object.scale)};
}
GizmoPaths Viewport::manipulatorPaths() const {
    return selection_ && !playing_ && !failed_ ?
        gizmoPaths(camera_,size(),selectedTransform(),transformSettings_) : GizmoPaths{};
}
void Viewport::setTransformSettings(TransformSettings settings) {
    cancelDrag();
    transformSettings_=settings;
    hovered_=-1;
    update();
}
void Viewport::releaseDrag() {
    if (!drag_) return;
    drag_.reset();
    unsetCursor();
    if (mouseGrabber()==this) releaseMouse();
}
void Viewport::finishDrag(bool commit) {
    if (!drag_) return;
    releaseDrag();
    if (onDragEnd) onDragEnd(commit);
    update();
}
void Viewport::cancelDrag() { finishDrag(false); }
bool Viewport::event(QEvent* event) {
    switch (event->type()) {
    case QEvent::FocusOut:
    case QEvent::UngrabMouse:
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
    case QEvent::DevicePixelRatioChange:
        cancelDrag();
        break;
    default: break;
    }
    return QWidget::event(event);
}

void Viewport::resetView() { cancelDrag(); camera_ = {}; update(); }
void Viewport::frameSelection() {
    cancelDrag();
    if (selection_ && selection_ <= draws_.size()) {
        const auto& object = draws_[selection_-1];
        camera_.frameObject(object,float(width())/std::max(1,height()));
        update();
    }
}
void Viewport::beginPlay() { cancelDrag(); playing_=true; authoringCamera_=camera_; }
void Viewport::endPlay() { playing_=false; camera_=authoringCamera_; update(); }
bool Viewport::presentScene() {
    return renderScene();
}
platz::engine::Capture Viewport::captureFrame() const {
    if (!renderer_ || failed_) throw std::runtime_error("The GPU viewport is unavailable.");
    return renderer_->capture();
}

void Viewport::fail(const std::exception& error) {
    failed_ = true;
    cancelDrag();
    displayedIds_.clear();
    if (onError) onError(QString::fromUtf8(error.what()));
}
QString Viewport::diagnostics() const {
    return renderer_ ? QString::fromStdString(renderer_->diagnostics()) : tr("Open a project to initialize the GPU.");
}
void Viewport::retryRenderer() {
    cancelDrag();
    renderer_.reset();
    displayedIds_.clear();
    surface_ = {};
    backendText_.clear();
    failed_ = false;
    update();
}
void Viewport::paintEvent(QPaintEvent*) {
    renderScene();
}
bool Viewport::renderScene() {
    if (failed_ || !isVisible() || !width() || !height()) return false;
    try {
        if (!renderer_) {
            renderer_ = std::make_unique<platz::engine::Renderer>(reinterpret_cast<void*>(winId()));
        }
        const QSize pixels = size() * devicePixelRatioF();
        if (pixels != surface_) {
            renderer_->resize(pixels.width(), pixels.height());
            surface_ = pixels;
        }
        const auto overlay=gizmoOverlay(manipulatorPaths(),size(),transformSettings_.tool,
                                        hovered_,drag_ ? drag_->axis() : -1);
        renderer_->draw(draws_,camera_,selection_,overlay);
        auto backend=QString::fromStdString(renderer_->adapter());
        if (const auto failures=renderer_->textureWarnings())
            backend+=tr(" · %n texture(s) unavailable",nullptr,static_cast<int>(failures));
        if (backend!=backendText_) {
            backendText_=backend;
            if (onBackend) onBackend(backendText_);
        }
        displayedIds_ = ids_;
        return true;
    } catch (const std::exception& error) { fail(error); }
    return false;
}
void Viewport::resizeEvent(QResizeEvent*) { cancelDrag(); update(); }
void Viewport::mousePressEvent(QMouseEvent* event) {
    mouse_ = event->position();
    setFocus();
    if (event->button() == Qt::LeftButton && renderer_ && !failed_) {
        try {
            const auto handle=hitGizmo(manipulatorPaths(),mouse_);
            if (handle>=0 && onDragStart) {
                const auto initial=TransformDrag::start(camera_,size(),selectedTransform(),transformSettings_,handle,mouse_);
                if (initial && onDragStart(ids_[selection_-1])) {
                    drag_=initial;
                    grabMouse();
                    setCursor(Qt::ClosedHandCursor);
                    update();
                    return;
                }
            }
            const auto pixel = event->position() * devicePixelRatioF();
            auto token = renderer_->pick(int(pixel.x()), int(pixel.y()));
            if (onPick) onPick(token > 0 && token <= displayedIds_.size() ? displayedIds_[token-1] : std::string{});
        } catch (const std::exception& error) { fail(error); }
    }
}
void Viewport::mouseMoveEvent(QMouseEvent* event) {
    const QPointF delta = event->position() - mouse_;
    mouse_ = event->position();
    if (drag_) {
        if (!(event->buttons()&Qt::LeftButton)) cancelDrag();
        else if (onDragUpdate) onDragUpdate(drag_->move(mouse_));
        return;
    }
    if (event->buttons() & Qt::RightButton) camera_.orbit(float(delta.x()), float(delta.y()));
    else if (event->buttons() & Qt::MiddleButton) camera_.pan(float(delta.x()), float(delta.y()), float(height()));
    else {
        const auto next=hitGizmo(manipulatorPaths(),mouse_);
        if (next!=hovered_) {
            hovered_=next;
            if (hovered_>=0) setCursor(Qt::OpenHandCursor); else unsetCursor();
            update();
        }
        return;
    }
    update();
}
void Viewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button()==Qt::LeftButton && drag_) {
        if (onDragUpdate) onDragUpdate(drag_->move(event->position()));
        finishDrag(true);
    }
}
void Viewport::wheelEvent(QWheelEvent* event) {
    if (!drag_) camera_.zoom(float(event->angleDelta().y()) / 120.f);
    event->accept();
    update();
}
void Viewport::keyPressEvent(QKeyEvent* event) {
    if (drag_) {
        if (event->key()==Qt::Key_Escape) cancelDrag();
        event->accept();
        return;
    }
    if (!playing_ && onTool && event->modifiers()==Qt::NoModifier &&
        event->key()>=Qt::Key_1 && event->key()<=Qt::Key_4) {
        onTool(static_cast<TransformTool>(event->key()-Qt::Key_1));
        return;
    }
    const float step = event->modifiers().testFlag(Qt::ShiftModifier) ? 1.f : 0.28f;
    switch (event->key()) {
    case Qt::Key_Left: camera_.orbit(-8,0); break;
    case Qt::Key_Right: camera_.orbit(8,0); break;
    case Qt::Key_Up: camera_.orbit(0,-8); break;
    case Qt::Key_Down: camera_.orbit(0,8); break;
    case Qt::Key_W: camera_.move(0,0,step); break;
    case Qt::Key_S: camera_.move(0,0,-step); break;
    case Qt::Key_A: camera_.move(-step,0,0); break;
    case Qt::Key_D: camera_.move(step,0,0); break;
    case Qt::Key_F: frameSelection(); break;
    case Qt::Key_Home: resetView(); break;
    case Qt::Key_Plus: case Qt::Key_Equal: camera_.zoom(1); break;
    case Qt::Key_Minus: camera_.zoom(-1); break;
    case Qt::Key_Space:
        if (selection_ && onPick) onPick(ids_.at(selection_-1));
        break;
    default: QWidget::keyPressEvent(event); return;
    }
    update();
}
