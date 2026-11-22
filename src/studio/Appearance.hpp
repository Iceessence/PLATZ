#pragma once
#include "Project.hpp"
#include <QIcon>
#include <QKeySequence>
#include <array>
class QApplication;
enum class StudioTheme { Slate, Light, Contrast };
enum class StudioAccent { Mint, Sky, Amber, Rose };
struct StudioAppearance {
    StudioTheme theme=StudioTheme::Slate;
    StudioAccent accent=StudioAccent::Mint;
    int scale=100;
};
struct StudioPreferences {
    StudioAppearance appearance;
    std::array<QKeySequence,4> shortcuts{QKeySequence(QKeySequence::Save),QKeySequence(Qt::Key_F5),QKeySequence(Qt::Key_F),{}};
};
void configureAppearance(QApplication& app);
void applyStudioAppearance(QApplication& app,const StudioAppearance& appearance);
QIcon meshIcon(platz::Mesh mesh, platz::Vec3 color);
QIcon platzIcon();
