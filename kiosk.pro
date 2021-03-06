TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

CONFIG -= c++11
CONFIG += c++17
QMAKE_CXXFLAGS += -std=c++17
QMAKE_LFLAGS += -std=c++17
LIBS += -lstdc++fs
CONFIG += static

LIBS += -pthread -ldl

INCLUDEPATH += $$quote($$PWD/json/single_include/)
DEPENDPATH  += $$quote($$PWD/json/single_include/)

INCLUDEPATH += $$quote($$PWD/stb)
DEPENDPATH  += $$quote($$PWD/stb)

QMAKE_CXXFLAGS += $$system(pkg-config --cflags sdl2 SDL2_image SDL2_ttf libcurl)

LIBS += $$system(pkg-config --libs sdl2 SDL2_image SDL2_ttf libcurl)

SOURCES += \
    fontrenderer.cpp \
    main.cpp \
    modules/eventsview.cpp \
    modules/infoview.cpp \
    modules/mateview.cpp \
    modules/screensaver.cpp \
    module.cpp \
    modules/mainmenu.cpp \
    gui_module.cpp \
    widget.cpp \
    widgets/button.cpp \
    modules/lightroom.cpp \
    modules/tramview.cpp \
    http_client.cpp \
    modules/powerview.cpp

HEADERS += \
    fontrenderer.hpp \
    modules/eventsview.hpp \
    modules/infoview.hpp \
    modules/mateview.hpp \
    modules/screensaver.hpp \
    module.hpp \
    kiosk.hpp \
    modules/mainmenu.hpp \
    gui_module.hpp \
    protected_value.hpp \
    rect_tools.hpp \
    rendering.hpp \
    widget.hpp \
    widgets/button.hpp \
    modules/lightroom.hpp \
    modules/tramview.hpp \
    http_client.hpp \
    modules/powerview.hpp
