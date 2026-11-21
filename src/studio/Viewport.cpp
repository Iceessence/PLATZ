#include "Viewport.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>

namespace {
platz::engine::Vec3 toEngine(platz::Vec3 v) { return {v.x,v.y,v.z}; }
platz::engine::Mesh toEngine(platz::Mesh mesh) {
    switch (mesh) {
    case platz::Mesh::Imported: return platz::engine::Mesh::Imported;
    case platz::Mesh::Sphere: return platz::engine::Mesh::Sphere;
    case platz::Mesh::Cylinder: return platz::engine::Mesh::Cylinder;
    default: return platz::engine::Mesh::Cube;
    }
}
}

Viewport::Viewport(QWidget* parent) : QWidget(parent) {
    setObjectName("viewport");
    setAccessibleName(tr("Perspective viewport"));
    setAccessibleDescription(tr("Arrow keys orbit. W A S D move. F frames the selected object. Home resets the view."));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMinimumSize(200,170);
}

void Viewport::setScene(const std::vector<platz::Object>& objects, const std::string& selection) {
    ids_.clear();
    draws_.clear();
    selection_ = 0;
    for (const auto& object : objects) {
        ids_.push_back(object.id);
        const auto token = static_cast<std::uint32_t>(ids_.size());
        draws_.push_back({token, toEngine(object.mesh), toEngine(object.position), toEngine(object.rotation),
                          toEngine(object.scale), toEngine(object.color),object.asset.data,object.slotColors,object.texturesOff});
        if (selection == object.id) selection_ = token;
    }
    update();
}

void Viewport::resetView() { camera_ = {}; update(); }
void Viewport::frameSelection() {
    if (selection_ && selection_ <= draws_.size()) {
        const auto& object = draws_[selection_-1];
        camera_.frameObject(object,float(width())/std::max(1,height()));
        update();
    }
}
void Viewport::beginPlay() { authoringCamera_ = camera_; }
void Viewport::endPlay() { camera_ = authoringCamera_; update(); }
platz::engine::Capture Viewport::captureFrame() const {
    if (!renderer_ || failed_) throw std::runtime_error("The GPU viewport is unavailable.");
    return renderer_->capture();
}

void Viewport::fail(const std::exception& error) {
    failed_ = true;
    displayedIds_.clear();
    if (onError) onError(QString::fromUtf8(error.what()));
}
QString Viewport::diagnostics() const {
    return renderer_ ? QString::fromStdString(renderer_->diagnostics()) : tr("Open a project to initialize the GPU.");
}
void Viewport::retryRenderer() {
    renderer_.reset();
    displayedIds_.clear();
    surface_ = {};
    backendText_.clear();
    failed_ = false;
    update();
}
void Viewport::paintEvent(QPaintEvent*) {
    if (failed_ || !isVisible() || !width() || !height()) return;
    try {
        if (!renderer_) {
            renderer_ = std::make_unique<platz::engine::Renderer>(reinterpret_cast<void*>(winId()));
        }
        const QSize pixels = size() * devicePixelRatioF();
        if (pixels != surface_) {
            renderer_->resize(pixels.width(), pixels.height());
            surface_ = pixels;
        }
        renderer_->draw(draws_, camera_, selection_);
        auto backend=QString::fromStdString(renderer_->adapter());
        if (const auto failures=renderer_->textureWarnings())
            backend+=tr(" · %n texture(s) unavailable",nullptr,static_cast<int>(failures));
        if (backend!=backendText_) {
            backendText_=backend;
            if (onBackend) onBackend(backendText_);
        }
        displayedIds_ = ids_;
    } catch (const std::exception& error) { fail(error); }
}
void Viewport::resizeEvent(QResizeEvent*) { update(); }
void Viewport::mousePressEvent(QMouseEvent* event) {
    mouse_ = event->position();
    setFocus();
    if (event->button() == Qt::LeftButton && renderer_ && !failed_) {
        try {
            const auto pixel = event->position() * devicePixelRatioF();
            auto token = renderer_->pick(int(pixel.x()), int(pixel.y()));
            if (onPick) onPick(token > 0 && token <= displayedIds_.size() ? displayedIds_[token-1] : std::string{});
        } catch (const std::exception& error) { fail(error); }
    }
}
void Viewport::mouseMoveEvent(QMouseEvent* event) {
    const QPointF delta = event->position() - mouse_;
    mouse_ = event->position();
    if (event->buttons() & Qt::RightButton) camera_.orbit(float(delta.x()), float(delta.y()));
    else if (event->buttons() & Qt::MiddleButton) camera_.pan(float(delta.x()), float(delta.y()), float(height()));
    else return;
    update();
}
void Viewport::wheelEvent(QWheelEvent* event) {
    camera_.zoom(float(event->angleDelta().y()) / 120.f);
    event->accept();
    update();
}
void Viewport::keyPressEvent(QKeyEvent* event) {
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
