#include "MainWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("VisionStudio");
    app.setApplicationVersion("5.0.0");

    MainWindow win;
    win.show();
    return app.exec();
}
