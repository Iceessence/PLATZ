#include "Viewport.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QWheelEvent>
#include <algorithm>
#include <numeric>

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
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMinimumSize(200,170);
    frameRetry_.setSingleShot(true);
    frameRetry_.setTimerType(Qt::PreciseTimer);
    frameRetry_.setInterval(1);
    connect(&frameRetry_,&QTimer::timeout,this,[this]{update();});
    pickTimer_.setTimerType(Qt::PreciseTimer);
    pickTimer_.setInterval(1);
    connect(&pickTimer_,&QTimer::timeout,this,[this]{completePick();});
}

void Viewport::setScene(const std::vector<platz::Object>& objects, const std::string& selection) {
    releaseDrag();
    pickIds_.clear();
    queuedPick_.reset();
    displayedGizmo_={};
    idsChanged_=true;
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

void Viewport::previewObject(const platz::Object& object,bool redraw) {
    if (selection_ && ids_[selection_-1]==object.id) {
        draws_[selection_-1]=drawObject(object,selection_);
        if (redraw) update();
    }
}
TransformValue Viewport::selectedTransform() const {
    const auto& object=draws_.at(selection_-1);
    return {fromEngine(object.position),fromEngine(object.rotation),fromEngine(object.scale)};
}
platz::Vec3 Viewport::localPivot() const {
    if (!transformSettings_.boundsPivot) return {};
    const auto& asset=draws_.at(selection_-1).asset;
    if (!asset) return {};
    const auto& min=asset->minCorner();
    const auto& max=asset->maxCorner();
    return {std::midpoint(min[0],max[0]),std::midpoint(min[1],max[1]),std::midpoint(min[2],max[2])};
}
GizmoPaths Viewport::manipulatorPaths() const {
    return manipulatorVisible_ && selection_ && !playing_ && !failed_ ?
        gizmoPaths(camera_,size(),selectedTransform(),transformSettings_,localPivot()) : GizmoPaths{};
}
void Viewport::setManipulatorVisible(bool visible) {
    if (manipulatorVisible_==visible) return;
    if (!visible) cancelDrag();
    manipulatorVisible_=visible;
    displayedGizmo_={};
    hovered_=-1;
    update();
}
void Viewport::setTransformSettings(TransformSettings settings) {
    cancelDrag();
    transformSettings_=settings;
    displayedGizmo_={};
    hovered_=-1;
    update();
}
void Viewport::releaseDrag() {
    pendingDrag_.reset();
    if (!drag_) return;
    drag_.reset();
    unsetCursor();
    if (mouseGrabber()==this) releaseMouse();
}
void Viewport::applyDragSample() {
    if (!pendingDrag_) return;
    const auto value=*pendingDrag_;
    pendingDrag_.reset();
    if (onDragUpdate) onDragUpdate(value);
}
void Viewport::queueDragSample(QPointF cursor,Qt::KeyboardModifiers modifiers) {
    pendingDrag_=drag_->move(cursor,modifiers.testFlag(Qt::ControlModifier),modifiers.testFlag(Qt::ShiftModifier));
    if (frameRetry_.isActive()) return;
    const auto remaining=lastMotionFrame_.isValid() ? motionIntervalMs_-lastMotionFrame_.elapsed() : 0;
    if (remaining>0) frameRetry_.start(static_cast<int>(remaining));
    else update();
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
        if (event->type()==QEvent::Hide || event->type()==QEvent::DevicePixelRatioChange) {
            pickIds_.clear();
            queuedPick_.reset();
        }
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
    return renderScene(true);
}
platz::engine::Capture Viewport::captureFrame() const {
    if (!renderer_ || failed_) throw std::runtime_error("The GPU viewport is unavailable.");
    return renderer_->capture();
}

void Viewport::fail(const std::exception& error) {
    failed_ = true;
    cancelDrag();
    displayedIds_.clear();
    idsChanged_=true;
    pickTimer_.stop();
    frameRetry_.stop();
    pickIds_.clear();
    queuedPick_.reset();
    if (onError) onError(QString::fromUtf8(error.what()));
}
QString Viewport::diagnostics() const {
    return renderer_ ? QString::fromStdString(renderer_->diagnostics()) : tr("Open a project to initialize the GPU.");
}
void Viewport::retryRenderer() {
    cancelDrag();
    pickTimer_.stop();
    frameRetry_.stop();
    pickIds_.clear();
    queuedPick_.reset();
    renderer_.reset();
    displayedIds_.clear();
    idsChanged_=true;
    surface_ = {};
    backendText_.clear();
    failed_ = false;
    update();
}
void Viewport::paintEvent(QPaintEvent*) {
    renderScene(false);
}
bool Viewport::renderScene(bool waitForFrame) {
    if (failed_ || !isVisible() || window()->isMinimized() || !width() || !height()) return false;
    try {
        if (!renderer_) {
            using namespace platz::engine;
            renderer_=std::make_unique<Renderer>(reinterpret_cast<void*>(winId()),Renderer::defaultViewportBytes,
                                                  Presentation::Interactive);
        }
        const QSize pixels = size() * devicePixelRatioF();
        if (pixels != surface_) {
            renderer_->resize(pixels.width(), pixels.height());
            surface_ = pixels;
        }
        applyDragSample();
        auto paths=manipulatorPaths();
        if (drag_) paths.rotationGuide=drag_->rotationGuide();
        const auto overlay=gizmoOverlay(paths,size(),transformSettings_.tool,
                                        hovered_,drag_ ? drag_->axis() : -1,devicePixelRatioF());
        if (!renderer_->draw(draws_,camera_,selection_,overlay,waitForFrame)) {
            if (!frameRetry_.isActive()) frameRetry_.start(1);
            return false;
        }
        frameRetry_.stop();
        ++submittedFrames_;
        if (drag_) lastMotionFrame_.start();
        displayedGizmo_=std::move(paths);
        auto backend=QString::fromStdString(renderer_->adapter());
        if (const auto failures=renderer_->textureWarnings())
            backend+=tr(" · %n texture(s) unavailable",nullptr,static_cast<int>(failures));
        if (backend!=backendText_) {
            backendText_=backend;
            if (onBackend) onBackend(backendText_);
        }
        if (idsChanged_) {
            displayedIds_ = ids_;
            idsChanged_=false;
        }
        return true;
    } catch (const std::exception& error) { fail(error); }
    return false;
}
bool Viewport::startPick(QPoint pixel) {
    if (!renderer_->requestPick(pixel.x(),pixel.y())) return false;
    pickIds_=displayedIds_;
    pickTimer_.start();
    QTimer::singleShot(0,this,[this]{if (pickTimer_.isActive()) completePick();});
    return true;
}
void Viewport::completePick() {
    try {
        const auto token=renderer_->pollPick();
        if (!token) return;
        pickTimer_.stop();
        if (queuedPick_) {
            const auto pixel=*queuedPick_;
            queuedPick_.reset();
            if (startPick(pixel)) return;
        }
        auto ids=std::move(pickIds_);
        pickIds_.clear();
        if (!ids.empty() && onPick)
            onPick(*token>0 && *token<=ids.size() ? ids[*token-1] : std::string{});
    } catch (const std::exception& error) { fail(error); }
}
void Viewport::resizeEvent(QResizeEvent*) {
    cancelDrag();
    pickIds_.clear();
    queuedPick_.reset();
    update();
}
void Viewport::mousePressEvent(QMouseEvent* event) {
    mouse_ = event->position();
    if (drag_ && event->button()==Qt::RightButton) {
        cancelDrag();
        event->accept();
        return;
    }
    setFocus();
    if (event->button() == Qt::LeftButton && renderer_ && !failed_) {
        try {
            const auto handle=hitGizmo(manipulatorPaths(),mouse_);
            if (handle>=0 && onDragStart) {
                const auto initial=TransformDrag::start(camera_,size(),selectedTransform(),transformSettings_,handle,mouse_,localPivot());
                if (initial && onDragStart(ids_[selection_-1])) {
                    pickIds_.clear();
                    queuedPick_.reset();
                    drag_=initial;
                    // Leave time for native event delivery and GPU submission within a refresh period.
                    motionIntervalMs_=std::max(1,int(1000./std::max(30.,screen()->refreshRate()))-2);
                    lastMotionFrame_.invalidate();
                    grabMouse();
                    setCursor(Qt::ClosedHandCursor);
                    update();
                    return;
                }
            }
            const auto pixel = event->position() * devicePixelRatioF();
            const QPoint client(int(pixel.x()),int(pixel.y()));
            if (pickTimer_.isActive()) queuedPick_=client;
            else startPick(client);
        } catch (const std::exception& error) { fail(error); }
    }
}
void Viewport::mouseMoveEvent(QMouseEvent* event) {
    const QPointF delta = event->position() - mouse_;
    mouse_ = event->position();
    if (drag_) {
        if (!(event->buttons()&Qt::LeftButton)) cancelDrag();
        else queueDragSample(mouse_,event->modifiers());
        return;
    }
    if (event->buttons() & Qt::RightButton) camera_.orbit(float(delta.x()), float(delta.y()));
    else if (event->buttons() & Qt::MiddleButton) camera_.pan(float(delta.x()), float(delta.y()), float(height()));
    else {
        const auto next=hitGizmo(displayedGizmo_,mouse_);
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
        queueDragSample(event->position(),event->modifiers());
        applyDragSample();
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
        else if (event->key()==Qt::Key_Control) queueDragSample(mouse_,event->modifiers()|Qt::ControlModifier);
        event->accept();
        return;
    }
    if (!playing_ && manipulatorVisible_ && onTool && event->modifiers()==Qt::NoModifier &&
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
void Viewport::keyReleaseEvent(QKeyEvent* event) {
    if (drag_ && event->key()==Qt::Key_Control) {
        queueDragSample(mouse_,event->modifiers()&~Qt::ControlModifier);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
