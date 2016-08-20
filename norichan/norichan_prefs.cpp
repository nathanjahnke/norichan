#include "norichan_prefs.h"

Norichan_Prefs::Norichan_Prefs() {
        this->svideo = false;
        this->ntsc = false;
        this->pal60 = false;
        this->widescreen = false;
        this->easycap_audio = true;
        this->always_on_top = false;
        this->f2_display = false;
        this->interlaced_display = false;
        this->field_blended_display = false;
        this->deinterlaced_display = false;
        this->deinterlaced_f2_display = false;
}

QDataStream &operator<<(QDataStream &out, const Norichan_Prefs &theprefs) {
        out
                        << theprefs.svideo
                        << theprefs.ntsc
                        << theprefs.pal60
                        << theprefs.widescreen
                        << theprefs.easycap_audio
                        << theprefs.always_on_top
                        << theprefs.f2_display
                        << theprefs.interlaced_display
                        << theprefs.field_blended_display
                        << theprefs.deinterlaced_display
                        << theprefs.deinterlaced_f2_display
                        << theprefs.streaming_url
                           ;

        return out;
}

QDataStream &operator>>(QDataStream &in, Norichan_Prefs &theprefs) {
        in
                        >> theprefs.svideo
                        >> theprefs.ntsc
                        >> theprefs.pal60
                        >> theprefs.widescreen
                        >> theprefs.easycap_audio
                        >> theprefs.always_on_top
                        >> theprefs.f2_display
                        >> theprefs.interlaced_display
                        >> theprefs.field_blended_display
                        >> theprefs.deinterlaced_display
                        >> theprefs.deinterlaced_f2_display
                        >> theprefs.streaming_url
                           ;

        return in;
}

