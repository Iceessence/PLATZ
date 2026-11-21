#pragma once
#include "Project.hpp"
#include <QIcon>
class QApplication;
void configureAppearance(QApplication& app);
QIcon meshIcon(platz::Mesh mesh, platz::Vec3 color);
QIcon platzIcon();
