#ifndef EASYCAP_RENDER_THREAD_H
#define EASYCAP_RENDER_THREAD_H

#include <QThread>
#include <QtGui>

#include "gtimer.h"

#ifdef NORICHAN
extern "C" {
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
}
#define SOURCE_PIX_FMT PIX_FMT_UYVY422
#define DISPLAY_DEST_PIX_FMT PIX_FMT_BGRA//PIX_FMT_RGB24
#endif

class easycap_render_thread : public QThread {
	Q_OBJECT

public:
	easycap_render_thread(QObject *parent = 0);
	void render(bool high_field);
	bool abort, running, save_data;
	int initial_width, initial_height, targetwidth, targetheight;
        unsigned char *incoming_image_buffer, *lol_dest_2, *frame_buffer_for_encoding, *frame_buffer_for_display, *frame_buffer_for_display_1, *frame_buffer_for_display_2;
	bool debug_console;
        GTimer timer;
	int colorspace;
	double colorspace_multiplier;
        bool currently_encoding, f2_display, interlaced_display, field_blended_display, deinterlaced_display, deinterlaced_f2_display;
        bool changed_dimensions;

private:
	bool field_boolean;
	unsigned int incoming_image_buffer_pos;
	QMutex mutex;
	QWaitCondition condition;
	QFile output;
	QDataStream out;
	unsigned int luma_size;
	unsigned char *luma;
#ifdef NORICHAN
	AVFrame *source_picture, *display_dest_picture, *encode_dest_picture;
#endif
	uint frame_number;

protected:
	void run();

signals:
	void frame_ready(bool);
	void debug_console_set_text(QString);
        //void broadcast_frame(uchar*, int, int);
};


#endif // EASYCAP_RENDER_THREAD_H
