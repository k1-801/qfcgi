QT += core network
CONFIG += c++17 silent
CONFIG -= app_bundle
TEMPLATE = lib
TARGET = qfcgi
DEFINES += QT_NO_DEBUG_OUTPUT

unix
{
    target.path = /usr/lib/
    header.path = /usr/include/
    header.files = src/qfcgi.h
    headers.path = /usr/include/qfcgi
    headers.files = src/qfcgi/fcgi.h src/qfcgi/request.h
    INSTALLS += target header headers
}

HEADERS += \
    src/qfcgi/builder.h \
    src/qfcgi/connection.h \
    src/qfcgi/fcgi.h \
    src/qfcgi/fdbuilder.h \
    src/qfcgi/localbuilder.h \
    src/qfcgi/record.h \
    src/qfcgi/request.h \
    src/qfcgi/stream.h \
    src/qfcgi/tcpbuilder.h \
    src/qfcgi.h \
    src/qfcgi/file.h

SOURCES += \
    src/qfcgi/connection.cpp \
    src/qfcgi/fcgi.cpp \
    src/qfcgi/fdbuilder.cpp \
    src/qfcgi/localbuilder.cpp \
    src/qfcgi/record.cpp \
    src/qfcgi/request.cpp \
    src/qfcgi/stream.cpp \
    src/qfcgi/tcpbuilder.cpp \
    src/qfcgi/file.cpp

