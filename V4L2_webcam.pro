QT -= core
QT -= gui

CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

TARGET = V4L2_webcam

DEFINES += _DEBUG

INCLUDEPATH += . 

HEADERS += \
    Conversions.h

SOURCES += \
    Main.c
