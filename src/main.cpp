#include <QApplication>

#include "ui/MainWindow.hpp"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    QApplication::setApplicationName(
        QStringLiteral("RBY1 Desktop Qt"));

    MainWindow window;
    window.show();

    return application.exec();
}
