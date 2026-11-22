#include "Appearance.hpp"
#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <algorithm>

QIcon platzIcon() {
    static const auto icon=[]{
        Q_INIT_RESOURCE(platz_brand);
        QIcon result;
        for (int size:{16,24,32,48,64,128,256})
            result.addFile(QString(":/brand/platz-%1.png").arg(size),QSize(size,size));
        return result;
    }();
    return icon;
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
    } else if (mesh == platz::Mesh::Imported) {
        p.drawPolygon(QPolygonF{{24,6},{41,35},{24,42},{7,35}});
        p.drawLine(24,6,24,42);
    } else {
        p.drawPolygon(QPolygonF{{24,6},{40,15},{40,33},{24,42},{8,33},{8,15}});
        p.drawPolyline(QPolygonF{{8,15},{24,24},{40,15}});
        p.drawLine(24,24,24,41);
    }
    return QIcon(pix);
}
namespace {
struct Colors {
    const char *background, *bar, *panel, *surface, *input, *edge, *text, *secondary, *disabled;
};
constexpr std::array themes{
    Colors{"#141a1f","#171d22","#20292f","#2a353b","#11181c","#425056","#e2e9e8","#a9bbbe","#87969b"},
    Colors{"#eef3f3","#e5eded","#f8fbfb","#f5f8f8","#ffffff","#85999c","#1a2c31","#43595d","#5e7479"},
    Colors{"#0a1116","#10181d","#17212a","#22313a","#070b10","#647988","#f4fbfc","#c7d8dd","#a2b1ba"}
};
QColor blend(QColor background,QColor foreground,double amount) {
    return QColor::fromRgbF((1-amount)*background.redF()+amount*foreground.redF(),
                           (1-amount)*background.greenF()+amount*foreground.greenF(),
                           (1-amount)*background.blueF()+amount*foreground.blueF());
}
}

void applyStudioAppearance(QApplication& app,const StudioAppearance& appearance) {
    const auto& colors=themes.at(static_cast<size_t>(appearance.theme));
    const bool light=appearance.theme==StudioTheme::Light;
    const std::array accents{QColor("#86e8bd"),QColor("#8bcdf8"),QColor("#f3c57b"),QColor("#f0aaba")};
    const auto accent=accents.at(static_cast<size_t>(appearance.accent));
    const QColor text(colors.text), background(colors.background);
    const auto selected=blend(QColor(colors.panel),accent,light ? .35 : .18);
    const auto focus=light ? blend(accent,QColor("#102824"),.64) : accent;
    const auto hover=blend(QColor(colors.surface),text,.09);
    const QColor warning(light ? "#7d4900" : "#f6c276");
    const auto scale=[&](int base){return QString::number(qRound(base*appearance.scale/100.0));};
    auto font=QFont("Segoe UI");
    font.setPointSizeF(10.0*appearance.scale/100.0);
    app.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window,background);
    palette.setColor(QPalette::WindowText,text);
    palette.setColor(QPalette::Base,QColor(colors.input));
    palette.setColor(QPalette::AlternateBase,QColor(colors.panel));
    palette.setColor(QPalette::Text,text);
    palette.setColor(QPalette::Button,QColor(colors.surface));
    palette.setColor(QPalette::ButtonText,text);
    palette.setColor(QPalette::Highlight,selected);
    palette.setColor(QPalette::HighlightedText,text);
    palette.setColor(QPalette::ToolTipBase,QColor(colors.surface));
    palette.setColor(QPalette::ToolTipText,text);
    for (auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})
        palette.setColor(QPalette::Disabled,role,QColor(colors.disabled));
    app.setPalette(palette);
    QString style=R"(
QMainWindow, QDialog, QWidget#projectHub { background: $background; }
QLabel { background: transparent; }
QMenuBar, QStatusBar { background: $bar; color: $secondary; }
QMenuBar { padding: 2px $widepx; }
QMenuBar::item { padding: $smallpx $widepx; }
QMenuBar::item:selected, QMenu::item:selected { background: $selected; color: $text; }
QToolBar { background: $panel; border: 0; border-bottom: 1px solid $edge; spacing: $smallpx; padding: $tightpx; }
QDockWidget { font-weight: 600; }
QDockWidget::title { background: $panel; padding: $widepx; border-bottom: 1px solid $edge; }
QMainWindow::separator { background: $edge; width: 3px; height: 3px; }
QPushButton, QToolButton { background: $surface; color: $text; border: 1px solid $edge; border-radius: 5px; padding: $tightpx $buttonpx; }
QPushButton:hover, QToolButton:hover { background: $hover; }
QToolButton:checked { background: $selected; color: $text; border-color: $focus; }
QPushButton:focus, QToolButton:focus, QLineEdit:focus { border: 1px solid $focus; }
QPushButton:disabled, QToolButton:disabled { color: $disabled; background: $panel; border-color: $edge; }
QPushButton[accent="true"] { color: #112322; background: $accent; border-color: $focus; font-weight: 650; }
QPushButton[accent="true"]:focus { border: 2px solid #18312d; padding: $pressedpx $insetpx; }
QPushButton[accent="true"]:hover { background: $accentHover; }
QLineEdit { background: $input; border: 1px solid $edge; padding: $smallpx; border-radius: 4px; }
QDoubleSpinBox QLineEdit, QSpinBox QLineEdit { background: transparent; color: $text; border: 0; padding: 0; }
QListWidget { background: $background; border: none; outline: 0; }
QListWidget::item { padding: $rowpx $smallpx; border-radius: 4px; }
QListWidget::item:selected { background: $selected; color: $text; }
QListWidget::item:focus { border: 1px solid $focus; }
QFrame#hubCard, QFrame#recentCard { border: 1px solid $edge; border-radius: 8px; background: $panel; }
QLabel[role="minor"] { color: $secondary; }
QLabel[role="section"] { font-size: $sectionpx; font-weight: 700; color: $secondary; }
QLabel[role="hubTitle"] { font-size: $titlepx; font-weight: 650; }
QLabel#brand { color: $focus; font-size: $brandpx; font-weight: 750; }
QLabel#sceneHeading { font-weight: 650; }
QLabel#warning, QLabel[role="warning"] { color: $warning; }
QStatusBar { border-top: 1px solid $edge; }
QToolTip { color: $text; background: $surface; border: 1px solid $edge; padding: $smallpx; }
)";
    const std::pair<const char*,QString> tokens[]{
        {"$background",colors.background},{"$bar",colors.bar},{"$panel",colors.panel},{"$surface",colors.surface},
        {"$input",colors.input},{"$edge",colors.edge},{"$text",colors.text},{"$secondary",colors.secondary},
        {"$disabled",colors.disabled},{"$accentHover",blend(accent,Qt::white,.28).name()},{"$accent",accent.name()},
        {"$focus",focus.name()},{"$selected",selected.name()},{"$hover",hover.name()},{"$warning",warning.name()},
        {"$small",scale(5)},{"$tight",scale(6)},{"$wide",scale(9)},{"$button",scale(11)},{"$row",scale(7)},
        {"$pressed",scale(6).toInt()>1 ? QString::number(scale(6).toInt()-1) : QString("1")},
        {"$inset",QString::number(scale(11).toInt()-1)},{"$brand",scale(17)},{"$section",scale(11)},{"$title",scale(34)}
    };
    for (const auto& [token,value]:tokens) style.replace(token,value);
    app.setStyleSheet(style);
}
void configureAppearance(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setWindowIcon(platzIcon());
    applyStudioAppearance(app,{});
}
