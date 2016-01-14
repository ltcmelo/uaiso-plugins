DEFINES += UAISOEDITOR_LIBRARY

LIBS += -L$$(UAISO_PATH) -lUaiSoEngine
INCLUDEPATH += $$(UAISO_PATH)
INCLUDEPATH += $$(UAISO_PATH)/External

include(../../qtcreatorplugin.pri)

HEADERS += \
    uaisoeditor.h \
    uaisocompletion.h \
    uaisosettings.h

SOURCES += \
    uaisoeditor.cpp \
    uaisocompletion.cpp \
    uaisosettings.cpp

RESOURCES += \
    uaisoeditor.qrc

FORMS += \
    uaisosettings.ui
