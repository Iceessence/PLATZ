#pragma once
#include "../assets/RasterImage.hpp"
#include <QImage>
#include <QWidget>
#include <functional>
#include <optional>

class RasterCanvas final : public QWidget {
public:
    enum class Tool { Pencil, Eraser, Fill, Eyedropper };
    explicit RasterCanvas(QImage image,QWidget* parent=nullptr);
    const QImage& image() const { return image_; }
    void setTool(Tool tool);
    void setColor(QColor color) { color_=color; }
    void setZoom(int zoom);
    void setGrid(bool grid);
    void resizeImage(QSize size);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo();
    void redo();
    std::function<void()> changed;
    std::function<void(QColor)> sampled;
    std::function<void(QPoint)> revealPixel;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool event(QEvent*) override;
private:
    std::optional<QPoint> pixel(QPointF point) const;
    bool line(QPoint from,QPoint to,QColor color);
    bool fill(QPoint at,QColor color);
    void beginStroke(QPoint at);
    QPoint keyboardPixel_;
    void finishStroke();
    void record(QImage before);
    void redraw();
    QImage image_, beforeStroke_;
    std::vector<QImage> undo_, redo_;
    std::optional<QPoint> last_;
    QColor color_{88,204,172,255};
    Tool tool_=Tool::Pencil;
    int zoom_=8;
    bool grid_=true;
};

QImage canvasImage(const platz::Raster& raster);
platz::Raster rasterSource(const QImage& image);
