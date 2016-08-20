#-------------------------------------------------
#
# Project created by QtCreator 2011-09-18T16:13:23
#
#-------------------------------------------------

QT       += gui multimedia opengl

TARGET = norichan
TEMPLATE = app

CONFIG += c++11

SOURCES += main.cpp\
        norichan.cpp \
    easycap_thread.cpp \
    easycap_render_thread.cpp \
    encode.cpp \
    debug_console.cpp \
    norichan_prefs.cpp \
    deinterlacer.cpp \
    ../common/rita.cpp \
    ../common/gtimer.cpp \
    ../common/rtmp_broadcaster.cpp

HEADERS  += norichan.h \
    easycap_thread.h \
    easycap_render_thread.h \
    encode.h \
    debug_console.h \
    norichan_prefs.h \
    deinterlacer.h \
    ../common/rita.h \
    ../common/gtimer.h \
    ../common/rtmp_broadcaster.h


VERSION = b16


DEFINES += NORICHAN
#DEFINES += OLDVIDEO


QMAKE_CXXFLAGS += -D__STDC_CONSTANT_MACROS #for libavcodec/avcodec.h


win32:PLATFORMNAME = windows
linux-g++-64:PLATFORMNAME = linux
mac {
PLATFORMNAME = ios
macx:PLATFORMNAME = mac
}
LIBS += -L../lib/$${PLATFORMNAME}

INCLUDEPATH += ../common
INCLUDEPATH += ../include/$${PLATFORMNAME}


LIBS += \
libavresample.a \
libavdevice.a \
libavfilter.a \
libavformat.a \
libavcodec.a \
libavutil.a \
libpostproc.a \
libswresample.a \
libswscale.a
LIBS += libmp3lame.a
LIBS += libx264.a

win32 {
INCLUDEPATH += wininclude
LIBS += -L../norichan -lusb
LIBS += -lwsock32 #for ffmpeg's networking (20130312)
RC_FILE = norichan.rc
}

mac {
LIBS += -lz -lbz2

#os x
INCLUDEPATH += /System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/CarbonCore.framework/Versions/A/Headers
INCLUDEPATH += /System/Library/Frameworks/ApplicationServices.framework/Versions/A/Headers
LIBS += -framework CoreFoundation -framework CoreAudio -framework CoreServices -framework IOKit -framework AudioToolbox -framework ApplicationServices -framework Accelerate


#QMAKE_INFO_PLIST = Info.plist #this is no longer needed thanks to the targets below
ICON = Norichan.icns
#App_SIGNATURE = NORI

#Info_plist.target = Info.plist
#Info_plist.depends = Info.plist.template $${TARGET}.app/Contents/Info.plist
#Info_plist.commands = @$(DEL_FILE) $${TARGET}.app/Contents/Info.plist$$escape_expand(\n\t) @$(SED) -e "s,@EXECUTABLE@,$$TARGET,g" -e "s,@VERSION@,$$VERSION,g" -e "s,@TYPEINFO@,$$App_SIGNATURE,g" -e "s,@ICON@,$$replace(ICON, .icns, ),g" Info.plist.template > $${TARGET}.app/Contents/Info.plist
#QMAKE_EXTRA_TARGETS += Info_plist
#PRE_TARGETDEPS += $$Info_plist.target

#PkgInfo.target = PkgInfo
#PkgInfo.depends = $${TARGET}.app/Contents/PkgInfo
#PkgInfo.commands = @$(DEL_FILE) $$PkgInfo.depends$$escape_expand(\n\t) @echo "APPL$$App_SIGNATURE" > $$PkgInfo.depends
#QMAKE_EXTRA_TARGETS += PkgInfo
#PRE_TARGETDEPS += $$PkgInfo.target

}
