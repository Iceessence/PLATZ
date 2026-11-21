#include "Appearance.hpp"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>

QIcon platzIcon() {
    QPixmap canvas(64,64);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#76e3bb"));
    p.drawRoundedRect(1,1,62,62,14,14);
    p.setBrush(QColor("#12392f"));
    p.drawPolygon(QPolygonF{{17,14},{47,14},{43,23},{29,23},{21,49},{12,49}});
    p.drawPolygon(QPolygonF{{31,28},{47,28},{44,36},{29,36}});
    return QIcon(canvas);
}
QIcon meshIcon(platz::Mesh mesh, platz::Vec3 color) {
    QPixmap pix(48,48);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor::fromRgbF(color.x,color.y,color.z));
    p.setPen(QPen(QColor("#afc6be"),1.7));
    if (mesh == platz::Mesh::Sphere) p.drawEllipse(9,9,30,30);
    else if (mesh == platz::Mesh::Cylinder) {
        QPainterPath path;
        path.moveTo(9,13); path.cubicTo(9,4,39,4,39,13); path.lineTo(39,34);
        path.cubicTo(39,43,9,43,9,34); path.closeSubpath();
        p.drawPath(path); p.drawArc(9,6,30,14,180*16,180*16);
    } else {
        p.drawPolygon(QPolygonF{{24,6},{40,15},{40,33},{24,42},{8,33},{8,15}});
        p.drawPolyline(QPolygonF{{8,15},{24,24},{40,15}});
        p.drawLine(24,24,24,41);
    }
    return QIcon(pix);
}
void configureAppearance(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setFont(QFont("Segoe UI",10));
    QPalette palette;
    palette.setColor(QPalette::Window,QColor("#181e23"));
    palette.setColor(QPalette::WindowText,QColor("#e2e9e8"));
    palette.setColor(QPalette::Base,QColor("#141a1f"));
    palette.setColor(QPalette::AlternateBase,QColor("#20292e"));
    palette.setColor(QPalette::Text,QColor("#e2e9e8"));
    palette.setColor(QPalette::Button,QColor("#273238"));
    palette.setColor(QPalette::ButtonText,QColor("#e2e9e8"));
    palette.setColor(QPalette::Highlight,QColor("#31594e"));
    palette.setColor(QPalette::HighlightedText,QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipBase,QColor("#273238"));
    palette.setColor(QPalette::ToolTipText,QColor("#ffffff"));
    app.setPalette(palette);
    app.setWindowIcon(platzIcon());
    app.setStyleSheet(R"(
QMainWindow, QDialog, QWidget#projectHub { background: #141a1f; }
QLabel { background: transparent; }
QMenuBar, QStatusBar { background: #171d22; color: #b6c2c6; }
QMenuBar { padding: 2px 8px; }
QMenuBar::item { padding: 4px 9px; }
QMenuBar::item:selected, QMenu::item:selected { background: #2a4940; }
QToolBar { background: #1c242a; border: 0; border-bottom: 1px solid #364147; spacing: 7px; padding: 6px; }
QDockWidget { font-weight: 600; }
QDockWidget::title { background: #20292f; padding: 9px 10px; border-bottom: 1px solid #354047; }
QMainWindow::separator { background: #323d43; width: 2px; height: 2px; }
QPushButton, QToolButton { background: #2a353b; color: #dfebe7; border: 1px solid #425056; border-radius: 5px; padding: 6px 11px; }
QPushButton:hover, QToolButton:hover { background: #38464d; }
QPushButton:focus, QToolButton:focus, QLineEdit:focus { border: 1px solid #7fe3bc; }
QPushButton:disabled, QToolButton:disabled { color: #879094; background: #222b30; border-color: #303a40; }
QPushButton[accent="true"] { color: #102c23; background: #86e8bd; border-color: #97f1cc; font-weight: 650; }
QPushButton[accent="true"]:hover { background: #acf6d4; }
QLineEdit { background: #11181c; border: 1px solid #3a484e; padding: 5px; border-radius: 4px; }
QDoubleSpinBox QLineEdit { background: transparent; color: #e2e9e8; border: 0; padding: 0; }
QListWidget { background: #181f24; border: none; outline: 0; }
QListWidget::item { padding: 7px 5px; border-radius: 4px; }
QListWidget::item:selected { background: #27483f; color: #ecfff7; }
QListWidget::item:focus { border: 1px solid #72b79b; }
QFrame#hubCard, QFrame#recentCard { border: 1px solid #38474c; border-radius: 8px; background: #1c252a; }
QLabel[role="minor"] { color: #a2b3b7; }
QLabel[role="section"] { font-size: 11px; font-weight: 700; color: #a6bbb6; }
QLabel#brand { color: #b6f5d7; font-size: 17px; font-weight: 750; }
QLabel#sceneHeading { font-weight: 650; }
QLabel#warning { color: #f6c276; }
QStatusBar { border-top: 1px solid #354047; }
QToolTip { color: #eef7f3; background: #263238; border: 1px solid #486057; padding: 4px; }
)");
}
