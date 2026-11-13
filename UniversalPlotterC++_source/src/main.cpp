#include <QApplication>
#include "MainWindow.h"
#include "NoWheelComboFilter.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("UniversalDataPlotter");
    QApplication::setApplicationName("Universal Data Plotter");
    NoWheelComboFilter noWheelFilter;
    app.installEventFilter(&noWheelFilter);
    MainWindow w;
    w.resize(1150, 700);
    w.show();
    return app.exec();
}
