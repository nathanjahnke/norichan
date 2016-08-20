#ifndef DEINTERLACER_H
#define DEINTERLACER_H

#include <QtGui>

#ifdef Q_OS_WIN32
#include <stdint.h>
#endif

class Deinterlacer : public QObject {
        Q_OBJECT
public:
        explicit Deinterlacer(QObject *parent = 0);
        ~Deinterlacer();
        
signals:
        void image_ready(QImage);
        
public slots:
        void add_field(QImage, bool);

private:
        void clean_up();

        int field_number;
        int width, height, linewidth;
        int *mfd_prevFrame;
        unsigned char *mfd_moving, *mfd_fmoving;
        int mfd_threshold;
        bool bDenoise, bMotionOnly;

};

#endif // DEINTERLACER_H
