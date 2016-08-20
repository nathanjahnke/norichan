#ifndef ENCODE_H
#define ENCODE_H

#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>
#include <QFileInfo>

#include "gtimer.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
}
#define SOURCE_PIX_FMT PIX_FMT_UYVY422


class encode : public QThread {
	Q_OBJECT
public:
	explicit encode(QObject *parent = 0);
	void render();
	void add_video_frame(uint8_t *);
	void add_audio_frame(QByteArray *);
	int width, height;
	bool abort, ntsc;
	double framerate;
	int colorspace;
	QString filename, filename_base, ext;

signals:
	void dropped_frames(uint);

public slots:

protected:
	void run();

private:
	void video_start();
	void video_stop();
	void video_encode_frame();
	void open_audio(AVFormatContext *, AVStream *);
	void close_audio(AVFormatContext *, AVStream *);
	void write_audio_frame(AVFormatContext *, AVStream *);
        AVStream *add_audio_stream(AVFormatContext *, AVCodecID);
	size_t trim_audio_queue_to(size_t);
	size_t trim_video_queue_to(size_t);
	void next_filename();

	AVOutputFormat *fmt;
	AVFormatContext *oc;
	AVStream *video_st, *audio_st;
	double video_pts, audio_pts;
	AVFrame *picture;
	uint8_t *video_outbuf, *video_frame_buffer;
	int video_outbuf_size;
	QMutex video_encoding_mutex;
	QQueue<uchar*> video_queue;
	QQueue<QByteArray*> audio_queue;

	signed int encoded_frames;
        GTimer encoding_timer, timer;

	int16_t *samples;
	uint8_t *audio_outbuf;
	int audio_outbuf_size;
	int audio_input_frame_size;

	size_t samples_length;
	QByteArray *audio_in_buffer;

};

#endif // ENCODE_H
