#include "rtmp_broadcaster.h"

RTMP_Broadcaster::RTMP_Broadcaster(QObject *parent) :
        QObject(parent)
      ,mutex(NULL)
      ,sws_ctx(NULL)
      ,currently_streaming(false)
      ,broadcast_timer(this)
      ,input_avpicture(NULL)
      ,output_avframe(NULL)
      ,audio_frame(NULL)
      ,input_pixel_format(AV_PIX_FMT_BGRA)
      ,output_pixel_format(AV_PIX_FMT_YUV420P)
{
        av_register_all();
        avformat_network_init();

        connect(&broadcast_timer, SIGNAL(timeout()), this, SLOT(broadcast()));
}

RTMP_Broadcaster::~RTMP_Broadcaster() {
        clean_up_ffmpeg_crap();
}

/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat API example.
 *
 * Output a media file in any supported libavformat format.
 * The default codecs are used.
 * @example doc/examples/muxing.c
 */

/* Add an output stream. */
AVStream *RTMP_Broadcaster::add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id) {
    AVCodecContext *c;
    AVStream *st;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        emit error_with_string(tr("Could not find encoder for '%1'").arg(avcodec_get_name(codec_id)));
            return NULL;
    }

    st = avformat_new_stream(oc, *codec);
    if (!st) {
            emit error_with_string(tr("Could not allocate stream"));
            return NULL;
    }
    st->id = oc->nb_streams-1;
    c = st->codec;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        st->id = 1;
        c->sample_fmt  = AV_SAMPLE_FMT_S16P;
        c->bit_rate    = audio_bitrate_kbit * 1000;
        c->sample_rate = audio_sample_rate_bits;
        c->channels    = audio_number_of_channels;
        c->channel_layout = c->channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
        break;

    case AVMEDIA_TYPE_VIDEO:
        avcodec_get_context_defaults3(c, *codec);
        c->codec_id = codec_id;

        c->bit_rate = video_bitrate_kbit * 1000;
//        c->rc_max_rate = 1024000;
//        c->rc_buffer_size = 10 * 1000 * 1000;
        c->width    = video_width;
        c->height   = video_height;
        c->time_base.num = 1000;
        c->time_base.den = (int)(video_framerate * 1000.0);
        video_frame_time_ms = ((double)c->time_base.num / (double)c->time_base.den) * 1000.0;
        c->gop_size      = 60; /* emit one intra frame every 60 frames at most */
        c->pix_fmt       = output_pixel_format;
        c->thread_count = 0;
    break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

/**************************************************************/
/* audio output */

void RTMP_Broadcaster::open_audio(AVFormatContext *oc, AVCodec *codec, AVStream *st) {
        Q_UNUSED(oc);
    AVCodecContext *c;
    int ret;

    c = st->codec;

    /* open it */
    if (mutex) mutex->lock();
    ret = avcodec_open2(c, codec, NULL);
    if (mutex) mutex->unlock();
    if (ret < 0) {
            char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
            QString error_string = tr("Could not open audio codec: %1").arg(ffmpeg_error_p);
            emit error_with_string(error_string);
            return;
    }

    if (c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)
        audio_samples_per_channel = 10000;
    else
        audio_samples_per_channel = c->frame_size;

    audio_frame_size = audio_samples_per_channel * av_get_bytes_per_sample(c->sample_fmt) * c->channels;
//    if (!av_sample_fmt_is_planar(c->sample_fmt)) { //FIXME is this necessary? (20130311)
//            qDebug() << "audio_frame_size *= c->channels";
//            audio_frame_size *=;
//    }

    samples = (int16_t *)av_malloc(audio_frame_size);
    if (!samples) {
        emit error_with_string(tr("Could not allocate audio samples buffer"));
            return;
    }

    audio_frame = avcodec_alloc_frame();
    audio_frame->nb_samples = audio_samples_per_channel;
    avcodec_fill_audio_frame(audio_frame, c->channels, c->sample_fmt,
                             (uint8_t *)samples,
                             audio_frame_size, 1);
//    audio_frame->pts = 0;
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
//void RTMP_Broadcaster::get_audio_frame(int16_t *samples, int frame_size, int nb_channels)
//{
//    int j, i, v;
//    int16_t *q;

//    q = samples;
//    for (j = 0; j < frame_size; j++) {
//        v = (int)(sin(t) * 10000);
//        for (i = 0; i < nb_channels; i++)
//            *q++ = v;
//        t     += tincr;
//        tincr += tincr2;
//    }
//}

void RTMP_Broadcaster::write_audio_frame(AVFormatContext *oc, AVStream *st) {
        AVCodecContext *c = st->codec;

        if (audio_data.size() < audio_frame_size) {
                qDebug() << this << "audio underflow!";
                return;
        }

        uint16_t *in = (uint16_t *)audio_data.data();

//        QByteArray planar_temp;
//        planar_temp.resize(audio_frame_size);
//        uint16_t *out = (uint16_t *)planar_temp.data();


//        memcpy(samples, planar_temp.data(), audio_frame_size);

        //
//        QFile lol("/down/lol");
//        lol.open(QIODevice::WriteOnly | QIODevice::Append);
//        lol.write((char *)samples, audio_frame_size);
//        lol.close();
        //

        //would you look at this here hack
        uint16_t *left_channel = (uint16_t *)audio_frame->extended_data[0];
        uint16_t *right_channel = (uint16_t *)audio_frame->extended_data[1];
        int top = audio_frame_size/2/2;
        for (int i = 0; i < top; ++i, in += 2) {
                left_channel[i] = in[0];
                right_channel[i] = in[1];
        }

        audio_data = audio_data.mid(audio_frame_size);

        int got_packet, ret;

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.size = 0;
    pkt.data = 0;


//    get_audio_frame(samples, audio_input_frame_size, c->channels);

//    audio_frame->pts = audio_timestamp;

    ret = avcodec_encode_audio2(c, &pkt, audio_frame, &got_packet);
    if (ret < 0) {
            char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
            QString error_string = tr("Error encoding audio frame: %1").arg(ffmpeg_error_p);
            emit error_with_string(error_string);
            return;
    }

    if (!got_packet) {
//            qDebug() << "audio: !got_packet";
        return;
    }

    pkt.stream_index = st->index;

    /* Write the compressed frame to the media file. */
    ret = av_interleaved_write_frame(oc, &pkt);
//    qDebug() << "wrote an audio frame";
    if (ret != 0) {
            char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
            QString error_string = tr("Error while writing audio frame: %1").arg(ffmpeg_error_p);
            emit error_with_string(error_string);
            return emergency_restart();
    }
    ++audio_frames_encoded;
}

void RTMP_Broadcaster::close_audio(AVFormatContext *oc, AVStream *st) {
        Q_UNUSED(oc);
        if (mutex) mutex->lock();
        avcodec_close(st->codec);
        if (mutex) mutex->unlock();

        av_free(samples);
}

/**************************************************************/
/* video output */

void RTMP_Broadcaster::open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st) {
        Q_UNUSED(oc);
    int ret;
    AVCodecContext *c = st->codec;

    AVDictionary *opts = NULL;
    QByteArray video_preset_ba = video_preset.toLocal8Bit();
    const char *video_preset_pointer = video_preset_ba.constData();
    av_dict_set(&opts, "preset", video_preset_pointer, 0); //this automatically sets the profile (20130313)
    av_dict_set(&opts, "tune", "zerolatency", 0);

    /* open the codec */
    if (mutex) mutex->lock();
    ret = avcodec_open2(c, codec, &opts);
    if (mutex) mutex->unlock();
    if (ret < 0) {
            char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
            QString error_string = tr("Could not open video codec: %1").arg(ffmpeg_error_p);
            emit error_with_string(error_string);
            return;
    }

//    /* allocate and init a re-usable frame */
//    frame = avcodec_alloc_frame();
//    if (!frame) {
//        emit error_with_string(tr("Could not allocate video frame"));
//        return;
//    }

//    /* Allocate the encoded raw picture. */
//    ret = avpicture_alloc(&dst_picture, c->pix_fmt, c->width, c->height);
//    if (ret < 0) {
//        emit error_with_string(tr("Could not allocate picture: %1").arg(av_err2str(ret)));
//        return;
//    }

//    /* If the output format is not YUV420P, then a temporary YUV420P
//     * picture is needed too. It is then converted to the required
//     * output format. */
//    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
//            qDebug() << "avpicture_alloc" << c->width << c->height;
//        ret = avpicture_alloc(&src_picture, AV_PIX_FMT_RGB24, c->width, c->height);
//        if (ret < 0) {
//            emit error_with_string(tr("Could not allocate temporary picture: %1").arg(av_err2str(ret)));
//            return;
//        }
//    }

//    /* copy data and linesize picture pointers to frame */
//    *((AVPicture *)frame) = dst_picture;
}

/* Prepare a dummy image. */
//static void fill_yuv_image(AVPicture *pict, int frame_index,
//                           int width, int height)
//{
//    int x, y, i;

//    i = frame_index;

//    /* Y */
//    for (y = 0; y < height; y++)
//        for (x = 0; x < width; x++)
//            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

//    /* Cb and Cr */
//    for (y = 0; y < height / 2; y++) {
//        for (x = 0; x < width / 2; x++) {
//            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
//            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
//        }
//    }
//}

void RTMP_Broadcaster::write_video_frame(AVFormatContext *oc, AVStream *st) {
        if (!video_frames.size()) {
                qDebug() << this << "video underflow!";
                return;
        }

    int ret;
    AVCodecContext *c = st->codec;

//    if (frame_count >= STREAM_NB_FRAMES) {
//        /* No more frames to compress. The codec has a latency of a few
//         * frames if using B-frames, so we get the last frames by
//         * passing the same picture again. */
//    } else {
//        if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
            /* as we only generate a YUV420P picture, we must convert it
             * to the codec pixel format if needed */
    QImage input_frame = video_frames.takeFirst();
    //input_frame = input_frame.convertToFormat(QImage::Format_RGB888);

//    qDebug() << "memcpy:" << src_picture.linesize[0] << input_frame.byteCount();
    memcpy(input_avpicture->data[0], input_frame.bits(), input_frame.byteCount());
    sws_scale(sws_ctx,
              input_avpicture->data, input_avpicture->linesize, 0, input_frame.height(),
              output_avframe->data, output_avframe->linesize);

//    sws_scale(sws_ctx,
//              (const uint8_t * const *)src_picture.data, src_picture.linesize,
//              0, c->height, dst_picture.data, dst_picture.linesize);
//        } else {
//            fill_yuv_image(&dst_picture, frame_count, c->width, c->height);
//        }
//    }

//    if (oc->oformat->flags & AVFMT_RAWPICTURE) {
//        /* Raw video case - directly store the picture in the packet */
//        AVPacket pkt;
//        av_init_packet(&pkt);

//        pkt.flags        |= AV_PKT_FLAG_KEY;
//        pkt.stream_index  = st->index;
//        pkt.data          = dst_picture.data[0];
//        pkt.size          = sizeof(AVPicture);

//        ret = av_interleaved_write_frame(oc, &pkt);
//    } else {
        /* encode the image */
        AVPacket pkt;
        int got_output;

        av_init_packet(&pkt);
        pkt.data = NULL;    // packet data will be allocated by the encoder
        pkt.size = 0;

//        output_avframe->pts = video_timestamp;
//        video_timestamp += av_rescale_q(1, video_st->codec->time_base, video_st->time_base);

        ret = avcodec_encode_video2(c, &pkt, output_avframe, &got_output);
        if (ret < 0) {
                char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
                QString error_string = tr("Error encoding video frame: %1").arg(ffmpeg_error_p);
                emit error_with_string(error_string);
                return;
        }

        /* If size is zero, it means the image was buffered. */
        if (got_output) {
            if (c->coded_frame->key_frame)
                pkt.flags |= AV_PKT_FLAG_KEY;

            pkt.stream_index = st->index;

            /* Write the compressed frame to the media file. */
            ret = av_interleaved_write_frame(oc, &pkt);
//            qDebug() << "wrote a video frame";
        } else {
            ret = 0;
        }
//    }
        if (ret != 0) {
                char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
                QString error_string = tr("Error while writing video frame: %1").arg(ffmpeg_error_p);
                emit error_with_string(error_string);
                return emergency_restart();
        }
        ++video_frames_encoded;
}

void RTMP_Broadcaster::close_video(AVFormatContext *oc, AVStream *st) {
        Q_UNUSED(oc);
        if (mutex) mutex->lock();
        avcodec_close(st->codec);
        if (mutex) mutex->unlock();
//        av_free(src_picture.data[0]);
//        av_free(dst_picture.data[0]);
//        av_free(frame);
}

/**************************************************************/
/* media file output */

void RTMP_Broadcaster::start(QString url, int width, int height, double framerate, QString preset, int new_video_bitrate_kbit, int new_audio_bitrate_kbit, int new_audio_sample_rate_bits, int new_audio_number_of_channels) {
        if (currently_streaming) return;

        current_url = url;
        video_width = width;
        video_height = height;
        video_framerate = framerate;
        video_preset = preset;
        video_bitrate_kbit = new_video_bitrate_kbit;
        audio_bitrate_kbit = new_audio_bitrate_kbit;
        audio_sample_rate_bits = new_audio_sample_rate_bits;
        audio_number_of_channels = new_audio_number_of_channels;

        video_frames.clear();
        audio_data.clear();

        sws_flags = SWS_LANCZOS;

    filename_ba = url.toLocal8Bit();
    filename = filename_ba.constData();

    /* allocate the output media context */
//    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
//    if (!oc) {
        //printf("Could not deduce output format from file extension: using FLV.\n");
        avformat_alloc_output_context2(&oc, NULL, "flv", filename);
//    }
//    if (!oc) {
            Q_ASSERT(oc);
//    }
    fmt = oc->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    video_st = NULL;
    audio_st = NULL;

    if (fmt->video_codec != AV_CODEC_ID_NONE) {
            fmt->video_codec = AV_CODEC_ID_H264;
            video_st = add_stream(oc, &video_codec, fmt->video_codec);
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
            fmt->audio_codec = AV_CODEC_ID_MP3;
            audio_st = add_stream(oc, &audio_codec, fmt->audio_codec);
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (video_st)
        open_video(oc, video_codec, video_st);
    if (audio_st)
        open_audio(oc, audio_codec, audio_st);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        int ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
                char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
                QString error_string = tr("Could not open: %1").arg(ffmpeg_error_p);
                emit error_with_string(error_string);
                return;
        }
    }

    /* Write the stream header, if any. */
    int ret = avformat_write_header(oc, NULL);
    if (ret < 0) {
            char ffmpeg_error_p[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(ffmpeg_error_p, AV_ERROR_MAX_STRING_SIZE, ret);
            QString error_string = tr("Error occurred when opening output file: %1").arg(ffmpeg_error_p);
            emit error_with_string(error_string);
            return;
    }

    sws_ctx = sws_getContext(video_width, video_height, input_pixel_format,
                             video_width, video_height, output_pixel_format,
                             sws_flags, NULL, NULL, NULL);
    if (!sws_ctx) {
            emit error_with_string(tr("Could not initialize the conversion context"));
            return;
    }
    //fill_yuv_image(&src_picture, frame_count, c->width, c->height);

    input_avpicture = (AVPicture *)av_malloc(sizeof(AVPicture));
    avpicture_alloc(input_avpicture, input_pixel_format, video_width, video_height);
    //input_length = avpicture_get_size(PIX_FMT_RGB24, video_width, video_height);
    output_avframe = avcodec_alloc_frame();
    avpicture_alloc(&dst_picture, output_pixel_format, video_width, video_height);
    /* copy data and linesize picture pointers to frame */
    *((AVPicture *)output_avframe) = dst_picture;
//    output_avframe->pts = 0;

//    video_frames_added = audio_frames_added = 0;
    audio_frames_encoded = video_frames_encoded = 0;
    session_timer.start();

    broadcast_timer.setInterval((1.0 / video_framerate) * 1000.0);
    broadcast_timer.start();

    currently_streaming = true;
}

void RTMP_Broadcaster::add_video_frame(QImage frame) {
//        qDebug() << "add_video_frame():" << frame.size();
        if (!currently_streaming) return;
        Q_ASSERT(frame.width() == video_width);
        Q_ASSERT(frame.height() == video_height);

//        qDebug() << "add_video_frame()";

//        double elapsed = session_timer.elapsed();
//        if (video_frames_added * video_frame_time_ms < elapsed + video_frame_time_ms*5) {
//                qDebug() << "video_frames << frame";
                video_frames << frame;
//                ++video_frames_added;
//        }
//        while (video_frames_added * video_frame_time_ms < elapsed) {
//                qDebug() << "catching up video";
//                video_frames << frame;
//                ++video_frames_added;
//        }

//        qDebug() << "V" << (video_frames_added * video_frame_time_ms) - elapsed;
//        last_video_jitter = (video_frames_added * video_frame_time_ms) - elapsed;
}

void RTMP_Broadcaster::add_audio_data(QByteArray data) {
        if (!currently_streaming) return;

        //qDebug() << "add_audio_data()";

//        double audio_frame_time_ms = ((double)data.size()/2/2/44100)*1000.0;
        //qDebug() << "audio_frame_time_ms" << audio_frame_time_ms;

//        double elapsed = session_timer.elapsed();
//        if (audio_frames_added * audio_frame_time_ms < elapsed + audio_frame_time_ms*5) {
                //qDebug() << "audio_data.append(data)";
                audio_data.append(data);
//                ++audio_frames_added;
//        }// else qDebug() << "rejecting audio frame";
//        while (audio_frames_added * audio_frame_time_ms < elapsed - audio_frame_time_ms*5) {
//                qDebug() << "catching up audio";
//                //qDebug() << "audio_data.append(data)";
//                audio_data.append(data);
//                ++audio_frames_added;
//        }// else qDebug() << "rejecting audio frame";
//        qDebug() << "A" << (audio_frames_added * audio_frame_time_ms) - elapsed;
//        last_audio_jitter = (audio_frames_added * audio_frame_time_ms) - elapsed;
}

void RTMP_Broadcaster::broadcast() {
        if (!currently_streaming) return;

//        static QList<double> jitter_list;
//        jitter_list << last_video_jitter - last_audio_jitter;
//        while (jitter_list.size() > 30) jitter_list.pop_front();
//        double jitter_sum = 0;
//        foreach (double jitter, jitter_list) {
//                jitter_sum += jitter;
//        }

//        qDebug() << "avg" << jitter_sum / jitter_list.size() << "diff" << last_video_jitter - last_audio_jitter << "A" << last_audio_jitter << "V" << last_video_jitter;

//        qDebug() << "broadcast()";

//        while (video_frames.size() && audio_data.size() >= audio_frame_size) {
//                qDebug() << video_frames.size() << audio_data.size() << audio_frame_size << "output_avframe->pts" << output_avframe->pts << "video_pts" << video_pts << "audio_pts" << audio_pts;
//                /* Compute current audio and video time. */
//                if (audio_st)
//                        audio_pts = (double)audio_st->pts.val * audio_st->time_base.num / audio_st->time_base.den;
//                else
//                        audio_pts = 0.0;

//                if (video_st)
//                        video_pts = (double)video_st->pts.val * video_st->time_base.num /
//                                        video_st->time_base.den;
//                else
//                        video_pts = 0.0;

//                //        if ((!audio_st || audio_pts >= STREAM_DURATION) &&
//                //                        (!video_st || video_pts >= STREAM_DURATION))
//                //                break;

//                /* write interleaved audio and video frames */
////                if (!video_st || (video_st && audio_st && audio_pts < video_pts)) {
//        audio_timestamp = video_timestamp = session_timer.elapsed();
        while (audio_data.size() >= audio_frame_size && video_frames.size()) {
//                audio_frame->pts += av_rescale_q(1 * audio_st->codec->frame_size, audio_st->codec->time_base, audio_st->time_base); //codec timebase is 1/sample rate (20130314)

                double video_pts = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
                //qDebug() << "video_st->pts.val is" << video_st->pts.val;
                double audio_pts = (double)audio_st->pts.val * audio_st->time_base.num / audio_st->time_base.den;
                //qDebug() << audio_pts << video_pts << "                          " << audio_data.size() << video_frames.size();
                if (audio_pts < video_pts) {
                        //qint64 old_pts = audio_st->pts.val;
                        write_audio_frame(oc, audio_st);
                        //qDebug() << "audio pts increased by" << audio_st->pts.val - old_pts;
                }
                        //continue; //encode more audio if possible, otherwise wait until possible

//                while (audio_data.size() >= audio_frame_size) {
//                        write_audio_frame(oc, audio_st);
//                        audio_frame->pts += av_rescale_q(1 * audio_st->codec->frame_size, audio_st->codec->time_base, audio_st->time_base); //codec timebase is 1/sample rate (20130314)
////                        qDebug() << audio_st->codec->time_base.num << audio_st->codec->time_base.den << audio_st->time_base.num << audio_st->time_base.den;
//                }
//                while (video_frames.size()) {
                else
                        write_video_frame(oc, video_st);
//                        qDebug() << "adding" << av_rescale_q(1, video_st->codec->time_base, video_st->time_base) << "to output_avframe->pts ... ";
//                        output_avframe->pts += av_rescale_q(1, video_st->codec->time_base, video_st->time_base);
//                        qDebug() << video_st->codec->time_base.num << video_st->codec->time_base.den << video_st->time_base.num << video_st->time_base.den;
//                        qDebug() << "now output_avframe->pts is" << output_avframe->pts;
//                }

//                int audio_data_max_bulge = audio_frame_size*2;
//                if (audio_data.size() > audio_data_max_bulge) {
//                        audio_data = audio_data.mid(audio_data_max_bulge);
//                }
//                int video_frames_max_bulge = 2;
//                while (video_frames.size() > video_frames_max_bulge) {
//                        video_frames.pop_front();
//                }
//                qDebug() << video_frames.size() << audio_data.size() << audio_data_max_bulge << (audio_data.size() > audio_data_max_bulge ? "audio_data.size() > audio_data_max_bulge" : "");
//        }
        }


        //
        //qDebug() << audio_data.size() << video_frames.size();
        //return;
        //

                //do not allow the audio to run more than 3 frames behind - we are more slack here than with the video because we prefer to drop audio frames (20111009)
                if (audio_data.size() > audio_frame_size*3) {
                        audio_data = audio_data.right(audio_frame_size*2);
                        qDebug() << this << "dropped audio frames";
                }

                //do not allow the video to run more than 1 frame behind the audio (20111013)
                if (video_frames.size() > 1 && audio_data.size() < audio_frame_size) {
                        int audio_frames_per_video_frame = (audio_sample_rate_bits * 2 * 2) / video_frame_time_ms / audio_frame_size;
                        for (int i = 0; i < video_frames.size() - 1; i++) { //this may come up to one frame short (20111013)
                                qDebug() << this << "adding" << audio_frames_per_video_frame << "audio frames to audio_data";
                                for (int j = 0; j < audio_frames_per_video_frame; j++) {
                                        QByteArray silence(audio_frame_size, 0);
                                        audio_data.append(silence);
                                }
                        }
                }
//        } else if (0) {
//                double elapsed = session_timer.elapsed();

//                qint64 video_pts_gap = (elapsed / video_frame_time_ms) - video_frames_encoded;
//                //qDebug() << "video_pts_gap is" << video_pts_gap;
//                if (video_pts_gap > 0) {
//                        qint64 scaled_skew = av_rescale_q(video_pts_gap, (AVRational){1,video_framerate}, video_st->time_base);
//                        qDebug() << this << "adding" << scaled_skew << "to video_st->pts.val" << video_st->pts.val;
//                        video_st->pts.val += scaled_skew;
//                        video_frames_encoded += video_pts_gap;
//                }

//                if (0) {
//                        double audio_frame_time_ms = ((double)audio_st->time_base.num / (double)audio_st->time_base.den) * 1000.0;
//                        qint64 audio_pts_gap = (elapsed / audio_frame_time_ms) - audio_frames_encoded;
//                        if (audio_pts_gap > 0) {
//                                //qint64 scaled_skew = av_rescale_q(audio_pts_gap, (AVRational){audio_st->time_base.den,audio_st->time_base.num}, audio_st->time_base);
//                                qint64 scaled_skew = audio_pts_gap;
//                                qDebug() << this << "adding" << scaled_skew << "to audio_st->pts.val" << audio_st->pts.val;
//                                audio_st->pts.val += scaled_skew;
//                                audio_frames_encoded += audio_pts_gap;
//                        }
//                }
//        }
}

void RTMP_Broadcaster::clean_up_ffmpeg_crap() {
        if (sws_ctx) {
                sws_freeContext(sws_ctx);
                sws_ctx = NULL;
        }
        if (input_avpicture) {
                avpicture_free(input_avpicture);
                input_avpicture = NULL;
        }
        if (output_avframe) {
                avpicture_free(&dst_picture);
                avcodec_free_frame(&output_avframe);
                output_avframe = NULL;
        }
        if (audio_frame) {
                avcodec_free_frame(&audio_frame);
                audio_frame = NULL;
        }
}

void RTMP_Broadcaster::stop() {
        if (!currently_streaming) return;

        broadcast_timer.stop();
        clean_up_ffmpeg_crap();

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

    /* Close each codec. */
    if (video_st)
        close_video(oc, video_st);
    if (audio_st)
        close_audio(oc, audio_st);

    /* Free the streams. */
    for (uint i = 0; i < oc->nb_streams; i++) {
        av_freep(&oc->streams[i]->codec);
        av_freep(&oc->streams[i]);
    }

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_close(oc->pb);

    /* free the stream */
    av_free(oc);

    currently_streaming = false;
}

void RTMP_Broadcaster::emergency_restart() {
        stop();
        QTimer::singleShot(5000, this, SLOT(restart()));
}

void RTMP_Broadcaster::restart() {
        return start(current_url, video_width, video_height, video_framerate, video_preset, video_bitrate_kbit, audio_bitrate_kbit, audio_sample_rate_bits, audio_number_of_channels);
}
