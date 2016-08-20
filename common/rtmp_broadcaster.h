#ifndef RTMP_BROADCASTER_H
#define RTMP_BROADCASTER_H

#include <QtGui>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

extern "C" {
#include <libavutil/mathematics.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include "gtimer.h"

class RTMP_Broadcaster : public QObject {
        Q_OBJECT
public:
        explicit RTMP_Broadcaster(QObject *parent = 0);
        ~RTMP_Broadcaster();
        QMutex *mutex;

signals:
        void error_with_string(QString);
        
public slots:
        void add_video_frame(QImage);
        void add_audio_data(QByteArray);
        void start(QString, int, int, double, QString, int, int, int, int);
        void stop();

private slots:
        void broadcast();
        void restart();

private:
        const char *filename;
        AVOutputFormat *fmt;
        AVFormatContext *oc;
        AVStream *audio_st, *video_st;
        AVCodec *audio_codec, *video_codec;
        void close_video(AVFormatContext *oc, AVStream *st);
        int16_t *samples;
        int audio_samples_per_channel;
        AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id);
        void open_audio(AVFormatContext *oc, AVCodec *codec, AVStream *st);
        void write_audio_frame(AVFormatContext *oc, AVStream *st);
        void close_audio(AVFormatContext *oc, AVStream *st);
        void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st);
        void write_video_frame(AVFormatContext *oc, AVStream *st);

        int sws_flags;

        QByteArray audio_data;
        QList<QImage> video_frames;

        //AVFrame *frame;
        //AVPicture src_picture, dst_picture;
//        int frame_count;
        int video_width, video_height;
        double video_framerate;
        int audio_sample_rate_bits;
        int audio_number_of_channels;

        struct SwsContext *sws_ctx;
        bool currently_streaming;
        QTimer broadcast_timer;
        int audio_frame_size;
        QByteArray filename_ba;

        AVPicture *input_avpicture;
        AVFrame *output_avframe;
        AVPicture dst_picture;
        void clean_up_ffmpeg_crap();

//        GTimer session_timer;
//        double audio_timestamp, video_timestamp;

        AVFrame *audio_frame;

//        GTimer session_timer;
//        qint64 video_frames_added, audio_frames_added;
        double video_frame_time_ms;
//        double last_video_jitter;
//        double last_audio_jitter;

        GTimer session_timer;
        int audio_frames_encoded, video_frames_encoded;

        QString video_preset;
        int video_bitrate_kbit, audio_bitrate_kbit;

        QString current_url;
        void emergency_restart();

        AVPixelFormat input_pixel_format, output_pixel_format;
};

#endif // RTMP_BROADCASTER_H
