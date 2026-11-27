#include "MainWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("VisionMaster_CPP");
    app.setApplicationVersion("4.7.0");

    MainWindow win;
    win.show();
    return app.exec();
}
