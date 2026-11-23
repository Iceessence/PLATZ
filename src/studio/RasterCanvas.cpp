#include "RasterCanvas.hpp"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
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
    setAccessibleDescription(tr("Use arrow keys to move the pixel cursor and Space to use the selected tool. Mouse input can draw strokes."));
    setFocusPolicy(Qt::StrongFocus);
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
    keyboardPixel_.setX(std::min(keyboardPixel_.x(),image_.width()-1));
    keyboardPixel_.setY(std::min(keyboardPixel_.y(),image_.height()-1));
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
    if (hasFocus()) {
        const QRect cursor(keyboardPixel_*zoom_,QSize(zoom_,zoom_));
        painter.fillRect(cursor.adjusted(-1,-1,1,1),QColor(245,252,255,125));
        painter.drawImage(cursor,image_,QRect(keyboardPixel_,QSize(1,1)));
        painter.setPen(Qt::black);
        painter.drawRect(cursor.adjusted(-1,-1,0,0));
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
    auto* bytes=image_.bits();
    const auto stride=image_.bytesPerLine();
    auto address=[&](int x,int y){return bytes+y*stride+4*x;};
    auto pixelAt=[&](int x,int y){
        std::uint32_t value;
        std::memcpy(&value,address(x,y),sizeof value);
        return value;
    };
    const auto original=pixelAt(at.x(),at.y());
    const auto replacement=std::bit_cast<std::uint32_t>(std::array{
        uchar(color.red()),uchar(color.green()),uchar(color.blue()),uchar(color.alpha())});
    if (original==replacement) return false;

    // Visit one seed per connected run. Keep the canvas in native RGBA byte order.
    const int width=image_.width(),height=image_.height();
    std::vector<QPoint> seeds{at};
    while (!seeds.empty()) {
        const auto seed=seeds.back();
        seeds.pop_back();
        const int y=seed.y();
        if (pixelAt(seed.x(),y)!=original) continue;
        int left=seed.x(),right=seed.x();
        while (left>0 && pixelAt(left-1,y)==original) --left;
        while (right+1<width && pixelAt(right+1,y)==original) ++right;
        for (int x=left;x<=right;++x) std::memcpy(address(x,y),&replacement,sizeof replacement);
        for (const int neighbor:{y-1,y+1}) {
            if (neighbor<0 || neighbor>=height) continue;
            bool inRun=false;
            for (int x=left;x<=right;++x) {
                const bool matches=pixelAt(x,neighbor)==original;
                if (matches && !inRun) seeds.emplace_back(x,neighbor);
                inRun=matches;
            }
        }
    }
    return true;
}
void RasterCanvas::mousePressEvent(QMouseEvent* event) {
    const auto at=pixel(event->position());
    if (!at || event->button()!=Qt::LeftButton) return;
    keyboardPixel_=*at;
    beginStroke(*at);
}
void RasterCanvas::beginStroke(QPoint at) {
    if (tool_==Tool::Eyedropper) {
        if (sampled) sampled(image_.pixelColor(at));
        return;
    }
    finishStroke();
    beforeStroke_=image_;
    if (tool_==Tool::Fill) {
        if (fill(at,color_)) redraw();
        finishStroke();
        return;
    }
    last_=at;
    if (line(at,at,tool_==Tool::Eraser ? QColor(0,0,0,0) : color_)) redraw();
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
void RasterCanvas::keyPressEvent(QKeyEvent* event) {
    QPoint shift;
    switch (event->key()) {
    case Qt::Key_Left: shift={-1,0}; break;
    case Qt::Key_Right: shift={1,0}; break;
    case Qt::Key_Up: shift={0,-1}; break;
    case Qt::Key_Down: shift={0,1}; break;
    case Qt::Key_Space:
        beginStroke(keyboardPixel_);
        finishStroke();
        event->accept();
        return;
    default: QWidget::keyPressEvent(event); return;
    }
    const auto moved=keyboardPixel_+shift;
    keyboardPixel_={std::clamp(moved.x(),0,image_.width()-1),std::clamp(moved.y(),0,image_.height()-1)};
    if (revealPixel) revealPixel(keyboardPixel_*zoom_+QPoint(zoom_/2,zoom_/2));
    update();
    event->accept();
}
bool RasterCanvas::event(QEvent* event) {
    if (event->type()==QEvent::FocusIn || event->type()==QEvent::FocusOut) update();
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
