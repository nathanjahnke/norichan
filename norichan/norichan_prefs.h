#ifndef NORICHAN_PREFS_H
#define NORICHAN_PREFS_H

#include <QtGui>

class Norichan_Prefs {
public:
        Norichan_Prefs();

        bool svideo;
        bool ntsc;
        bool pal60;
        bool widescreen;
        bool easycap_audio;
        bool always_on_top;
        bool f2_display;
        bool interlaced_display;
        bool field_blended_display;
        bool deinterlaced_display;
        bool deinterlaced_f2_display;
        QString streaming_url;
};

QDataStream &operator<<(QDataStream &out, const Norichan_Prefs &theprefs);
QDataStream &operator>>(QDataStream &in, Norichan_Prefs &theprefs);

#endif // NORICHAN_PREFS_H
