QT += core gui widgets

CONFIG += c++11

TARGET = smart_home_panel
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

unix:!android {
    target.path = /opt/smart_home_panel/bin
    INSTALLS += target
}
