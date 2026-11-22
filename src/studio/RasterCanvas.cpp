#include "RasterCanvas.hpp"
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {
constexpr size_t maxCanvasUndo=32;
QImage checker() {
    QImage image(16,16,QImage::Format_RGB32);
    image.fill(QColor(122,130,136));
    QPainter painter(&image);
    painter.fillRect(0,0,8,8,QColor(159,167,172));
    painter.fillRect(8,8,8,8,QColor(159,167,172));
    return image;
}
}

QImage canvasImage(const platz::Raster& raster) {
    return QImage(raster->pixels().data(),raster->width(),raster->height(),
                  QImage::Format_RGBA8888).copy();
}
platz::Raster rasterSource(const QImage& image) {
    if (image.isNull() || image.format()!=QImage::Format_RGBA8888)
        throw std::runtime_error("The canvas has no RGBA pixels.");
    const auto* bytes=image.constBits();
    return platz::RasterImage::create(image.width(),image.height(),
        std::vector<std::uint8_t>(bytes,bytes+image.sizeInBytes()));
}
RasterCanvas::RasterCanvas(QImage image,QWidget* parent)
    : QWidget(parent),image_(std::move(image)) {
    setObjectName("imageCanvas");
    setAccessibleName(tr("RGBA pixel canvas"));
    setAccessibleDescription(tr("Use the mouse to draw or sample pixels. Width, height, color and history have keyboard controls."));
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setZoom(zoom_);
}
void RasterCanvas::setTool(Tool tool) {
    finishStroke();
    tool_=tool;
}
void RasterCanvas::setZoom(int zoom) {
    zoom_=zoom;
    setFixedSize(image_.size()*zoom_);
    update();
}
void RasterCanvas::setGrid(bool grid) { grid_=grid; update(); }
void RasterCanvas::paintEvent(QPaintEvent* event) {
    static const auto pattern=checker();
    QPainter painter(this);
    painter.setClipRegion(event->region());
    painter.fillRect(rect(),QBrush(pattern));
    painter.drawImage(rect(),image_);
    if (grid_ && zoom_>=8) {
        painter.setPen(QColor(20,33,39,85));
        for (int x=zoom_;x<width();x+=zoom_) painter.drawLine(x,0,x,height());
        for (int y=zoom_;y<height();y+=zoom_) painter.drawLine(0,y,width(),y);
    }
}
std::optional<QPoint> RasterCanvas::pixel(QPointF point) const {
    const QPoint at(int(std::floor(point.x()/zoom_)),int(std::floor(point.y()/zoom_)));
    if (!image_.rect().contains(at)) return {};
    return at;
}
bool RasterCanvas::line(QPoint from,QPoint to,QColor color) {
    int dx=std::abs(to.x()-from.x()), dy=-std::abs(to.y()-from.y());
    const int sx=from.x()<to.x() ? 1 : -1, sy=from.y()<to.y() ? 1 : -1;
    int error=dx+dy;
    bool painted=false;
    for (;;) {
        if (image_.pixelColor(from)!=color) {
            image_.setPixelColor(from,color);
            painted=true;
        }
        if (from==to) return painted;
        const int step=2*error;
        if (step>=dy) {error+=dy; from.rx()+=sx;}
        if (step<=dx) {error+=dx; from.ry()+=sy;}
    }
}
bool RasterCanvas::fill(QPoint at,QColor color) {
    const auto original=image_.pixelColor(at);
    if (original==color) return false;
    std::vector<QPoint> queue{at};
    image_.setPixelColor(at,color);
    const std::array sides{QPoint(1,0),QPoint(-1,0),QPoint(0,1),QPoint(0,-1)};
    for (size_t i=0;i<queue.size();++i) {
        const auto current=queue[i];
        for (const auto direction:sides) {
            const auto neighbor=current+direction;
            if (image_.rect().contains(neighbor) && image_.pixelColor(neighbor)==original) {
                image_.setPixelColor(neighbor,color);
                queue.push_back(neighbor);
            }
        }
    }
    return true;
}
void RasterCanvas::mousePressEvent(QMouseEvent* event) {
    const auto at=pixel(event->position());
    if (!at || event->button()!=Qt::LeftButton) return;
    if (tool_==Tool::Eyedropper) {
        if (sampled) sampled(image_.pixelColor(*at));
        return;
    }
    finishStroke();
    beforeStroke_=image_;
    if (tool_==Tool::Fill) {
        if (fill(*at,color_)) redraw();
        finishStroke();
        return;
    }
    last_=at;
    if (line(*at,*at,tool_==Tool::Eraser ? QColor(0,0,0,0) : color_)) redraw();
}
void RasterCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (beforeStroke_.isNull()) return;
    if (!(event->buttons()&Qt::LeftButton)) { finishStroke(); return; }
    const auto at=pixel(event->position());
    if (at && line(last_.value_or(*at),*at,tool_==Tool::Eraser ? QColor(0,0,0,0) : color_)) redraw();
    last_=at;
}
void RasterCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button()==Qt::LeftButton) finishStroke();
}
bool RasterCanvas::event(QEvent* event) {
    if (event->type()==QEvent::Hide || event->type()==QEvent::UngrabMouse || event->type()==QEvent::WindowDeactivate)
        finishStroke();
    return QWidget::event(event);
}
void RasterCanvas::finishStroke() {
    last_.reset();
    if (beforeStroke_.isNull()) return;
    auto before=std::exchange(beforeStroke_,QImage{});
    if (before!=image_) record(std::move(before));
}
void RasterCanvas::record(QImage before) {
    undo_.push_back(std::move(before));
    if (undo_.size()>maxCanvasUndo) undo_.erase(undo_.begin());
    redo_.clear();
    redraw();
}
void RasterCanvas::resizeImage(QSize size) {
    finishStroke();
    if (size==image_.size()) return;
    auto previous=image_;
    QImage resized(size,QImage::Format_RGBA8888);
    if (resized.isNull()) { redraw(); return; }
    resized.fill(Qt::transparent);
    const auto copied=size.boundedTo(image_.size());
    for (int y=0;y<copied.height();++y)
        std::copy_n(image_.constScanLine(y),copied.width()*4,resized.scanLine(y));
    image_=std::move(resized);
    setZoom(zoom_);
    record(std::move(previous));
}
void RasterCanvas::undo() {
    finishStroke();
    if (undo_.empty()) return;
    redo_.push_back(std::exchange(image_,std::move(undo_.back())));
    undo_.pop_back();
    setZoom(zoom_);
    redraw();
}
void RasterCanvas::redo() {
    if (redo_.empty() || !beforeStroke_.isNull()) return;
    undo_.push_back(std::exchange(image_,std::move(redo_.back())));
    redo_.pop_back();
    setZoom(zoom_);
    redraw();
}
void RasterCanvas::redraw() {
    update();
    if (changed) changed();
}
