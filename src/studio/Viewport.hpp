#pragma once
#include "Project.hpp"
#include "renderer.hpp"
#include "TransformGizmo.hpp"
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
    void previewObject(const platz::Object& object);
    void setTransformSettings(platz::studio::TransformSettings settings);
    void cancelDrag();
    bool dragging() const { return drag_.has_value(); }
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
private:
    std::unique_ptr<platz::engine::Renderer> renderer_;
    platz::engine::Camera camera_, authoringCamera_;
    std::vector<platz::engine::DrawObject> draws_;
    std::vector<std::string> ids_, displayedIds_;
    std::uint32_t selection_ = 0;
    QSize surface_;
    QPointF mouse_;
    QString backendText_;
    bool failed_ = false, playing_ = false;
    platz::studio::TransformSettings transformSettings_;
    std::optional<platz::studio::TransformDrag> drag_;
    int hovered_=-1;
    void releaseDrag();
    void finishDrag(bool commit);
    platz::studio::TransformValue selectedTransform() const;
    void fail(const std::exception& error);
};
