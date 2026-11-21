#pragma once
#include "Project.hpp"
#include "renderer.hpp"
#include <QWidget>
#include <functional>

class Viewport final : public QWidget {
public:
    explicit Viewport(QWidget* parent = nullptr);
    std::function<void(const std::string&)> onPick;
    std::function<void(QString)> onBackend;
    std::function<void(QString)> onError;
    void setScene(const std::vector<platz::Object>& objects, const std::string& selection);
    void frameSelection();
    void resetView();
    void beginPlay();
    void endPlay();
    platz::engine::Capture captureFrame() const;
    QPaintEngine* paintEngine() const override { return nullptr; }
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
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
    bool failed_ = false;
    void fail(const std::exception& error);
};
