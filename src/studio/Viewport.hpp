#pragma once
#include "Project.hpp"
#include "renderer.hpp"
#include "TransformGizmo.hpp"
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <functional>

class Viewport final : public QWidget {
public:
    explicit Viewport(QWidget* parent = nullptr);
    std::function<void(const std::string&)> onPick;
    std::function<void(QString)> onBackend;
    std::function<void(QString)> onError;
    std::function<bool(const std::string&)> onDragStart;
    std::function<void(const platz::studio::TransformValue&)> onDragUpdate;
    std::function<void(bool)> onDragEnd;
    std::function<void(platz::studio::TransformTool)> onTool;
    void setScene(const std::vector<platz::Object>& objects, const std::string& selection);
    void previewObject(const platz::Object& object, bool redraw = true);
    void setTransformSettings(platz::studio::TransformSettings settings);
    void cancelDrag();
    bool dragging() const { return drag_.has_value(); }
    float dragAngle() const { return drag_ ? drag_->degrees() : 0.f; }
    bool selectionPending() const { return pickTimer_.isActive(); }
    std::uint64_t submittedFrames() const { return submittedFrames_; }
    bool presentScene();
    platz::studio::GizmoPaths manipulatorPaths() const;
    void frameSelection();
    void resetView();
    void beginPlay();
    void endPlay();
    platz::engine::Capture captureFrame() const;
    QString diagnostics() const;
    void retryRenderer();
    QPaintEngine* paintEngine() const override { return nullptr; }
protected:
    bool event(QEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
private:
    bool renderScene(bool waitForFrame);
    bool startPick(QPoint pixel);
    void completePick();
    std::unique_ptr<platz::engine::Renderer> renderer_;
    platz::engine::Camera camera_, authoringCamera_;
    std::vector<platz::engine::DrawObject> draws_;
    bool idsChanged_{true};
    std::vector<std::string> ids_, displayedIds_;
    std::vector<std::string> pickIds_;
    std::optional<QPoint> queuedPick_;
    QTimer frameRetry_, pickTimer_;
    QElapsedTimer lastMotionFrame_;
    int motionIntervalMs_{1};
    std::uint64_t submittedFrames_{};
    platz::studio::GizmoPaths displayedGizmo_;
    std::uint32_t selection_ = 0;
    QSize surface_;
    QPointF mouse_;
    QString backendText_;
    bool failed_ = false, playing_ = false;
    platz::studio::TransformSettings transformSettings_;
    std::optional<platz::studio::TransformDrag> drag_;
    std::optional<platz::studio::TransformValue> pendingDrag_;
    int hovered_=-1;
    void releaseDrag();
    void applyDragSample();
    void queueDragSample(QPointF cursor,Qt::KeyboardModifiers modifiers);
    void finishDrag(bool commit);
    platz::studio::TransformValue selectedTransform() const;
    platz::Vec3 localPivot() const;
    void fail(const std::exception& error);
};
