#include "Icons.h"
#include "MainWindow.h"

#include <QtCore/QCoreApplication>
#include <QtWebEngine/qtwebengineglobal.h>
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication app(argc, argv);
    QtWebEngine::initialize();
    app.setOrganizationName(QStringLiteral("lazier"));
    app.setApplicationName(QStringLiteral("lazier"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(lazierAppIcon());

    MainWindow window;
    window.show();
    return app.exec();
}
