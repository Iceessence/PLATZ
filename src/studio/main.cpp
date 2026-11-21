#include "Appearance.hpp"
#include "StudioWindow.hpp"
#include <QApplication>
#include <QSettings>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("PLATZ");
    app.setApplicationName("PLATZ Studio");
    const auto profile=qEnvironmentVariable("PLATZ_SETTINGS_DIR");
    if (!profile.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile);
    }
    configureAppearance(app);
    StudioWindow window;
    const auto arguments=app.arguments();
    if (arguments.size()==2) window.openProject(arguments.at(1));
    window.show();
    return app.exec();
}
