#include "encode.h"

static AVFrame *alloc_picture(enum PixelFormat pix_fmt, int width, int height) {
	AVFrame *picture;
	uint8_t *picture_buf;
	int size;

	picture = avcodec_alloc_frame();
	if (!picture)
		return NULL;
	size = avpicture_get_size(pix_fmt, width, height);
	picture_buf = (uint8_t*)av_malloc(size);
	if (!picture_buf) {
		av_free(picture);
		return NULL;
	}
	avpicture_fill((AVPicture *)picture, picture_buf,
		       pix_fmt, width, height);
	return picture;
}

encode::encode(QObject *parent) : QThread(parent) {
	ntsc = false;
	framerate = 25.0;
	abort = true; //ignore data coming in until we actually start encoding (20111008)
	audio_in_buffer = new QByteArray;
	samples_length = 0;
}

void encode::render() {
	QMutexLocker locker(&video_encoding_mutex);
	if (!isRunning()) {
		abort = false;
		framerate = ntsc ? 29.97 : 25.0;
		start();
	}
}

void encode::add_video_frame(uint8_t *buffer) {
	if (abort) return;
	QMutexLocker locker(&video_encoding_mutex);
	video_queue.enqueue(buffer);
	//qDebug() << "video_queue.size()" << video_queue.size();
}

void encode::add_audio_frame(QByteArray *buffer) {
	if (abort) return;

	audio_in_buffer->append(*buffer);
	delete buffer;
	//qDebug() << "audio_in_buffer.size()" << audio_in_buffer->size();

	if (!samples_length) return; //the thread has not yet started so we don't know what the expected audio buffer length is (20111008)
	//qDebug() << samples_length; //1000, so 6.406406406406406 audio frames per 1 video frame in ntsc (20111009)

	while (audio_in_buffer->size() >= samples_length) {
		QByteArray *new_audio_in_buffer = new QByteArray(audio_in_buffer->right(audio_in_buffer->size() - samples_length));
		audio_in_buffer->resize(samples_length);

		video_encoding_mutex.lock();
		audio_queue.enqueue(audio_in_buffer);
		video_encoding_mutex.unlock();

		audio_in_buffer = new_audio_in_buffer;
	}
}

void encode::run() {
	av_register_all();
	video_start();

	video_encoding_mutex.lock();
	trim_audio_queue_to(0);
	trim_video_queue_to(0);
	video_encoding_mutex.unlock();

	encoding_timer.start();
	while (!abort) {
		video_encoding_mutex.lock();
		while (video_queue.length() && audio_queue.length()) {
			video_encoding_mutex.unlock();


			//audio
			video_encoding_mutex.lock();
			QByteArray *new_samples = audio_queue.dequeue();
			video_encoding_mutex.unlock();

			memcpy(samples, new_samples->constData(), samples_length);
			delete new_samples;
			//qDebug() << "write_audio_frame()";
			write_audio_frame(oc, audio_st);

			video_pts = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
			audio_pts = (double)audio_st->pts.val * audio_st->time_base.num / audio_st->time_base.den;
			if (audio_pts < video_pts) {
				video_encoding_mutex.lock();
				continue; //encode more audio if possible, otherwise wait until possible
			}


			//video
			video_encoding_mutex.lock();
			video_frame_buffer = video_queue.dequeue();
			video_encoding_mutex.unlock();
			//qDebug() << "video_encode_frame()";
			video_encode_frame();


			video_encoding_mutex.lock();
		}
		video_encoding_mutex.unlock();

#ifdef FIELDSPLIT
		double supposed_frames = encoding_timer.elapsed() / (ntsc ? ((1.0/59.94)*1000.0) : 20.0);
#else
		double supposed_frames = encoding_timer.elapsed() / (ntsc ? ((1.0/29.97)*1000.0) : 40.0);
#endif
		video_encoding_mutex.lock();
		qDebug() << QString("%1").arg(encoded_frames - supposed_frames, 10, 'f', 10)
			 << ": supposed to have encoded" << supposed_frames
			 << "actually encoded" << encoded_frames
			 << "; video_queue.size()" << video_queue.size()
			 << "audio_queue.size()" << audio_queue.size()
			    ;


		//do not allow the audio to run more than 3 video frames behind - we are more slack here than with the video because we prefer to drop audio frames (20111009)
		if (audio_queue.size() > 18) { //~3 video frames
			size_t dropped_frame_count = trim_audio_queue_to(12); //~2 video frames
			if (dropped_frame_count > 0) qDebug() << "audio: dropped" << dropped_frame_count << "frames!";
		}

		//do not allow the video to run more than 2 video frames behind the audio (20111013)
		if (video_queue.size() > 1 && audio_queue.size() == 0 && samples_length) {
                        int audio_frames_per_video_frame = (44100 * 2 * 2) / framerate / samples_length;
			for (uint i = 0; i < video_queue.size() - 1; i++) { //this may come up to one frame short (20111013)
				qDebug() << "adding" << audio_frames_per_video_frame << "audio frames to audio_queue";
				for (uint j = 0; j < audio_frames_per_video_frame; j++) {
					QByteArray *blank_audio_buffer = new QByteArray(samples_length, 0);
					audio_queue.enqueue(blank_audio_buffer);
				}
			}
		}

//		size_t dropped_frame_count = trim_video_queue_to(1); //do not allow the video to run more than 1 video frame behind the audio (20111009)
//		if (dropped_frame_count > 0) {
//			qDebug() << "video: dropped" << dropped_frame_count << "frames!";
//			emit(dropped_frames(dropped_frame_count));
//		}

		video_encoding_mutex.unlock();

		//segment the output - we can't let it grow too long or the audio will be unparseable due to the 32-bit wav header (20111010)
		if (encoded_frames > (ntsc ? 539460 : 450000)) { //~5 hours - signed 32-bit wav header limit is ~5.7 hours
		//if (encoded_frames > (ntsc ? 300 : 300)) { //~10 seconds
			video_stop();
			video_start();
			encoding_timer.restart();
		}


#ifdef FIELDSPLIT
		msleep(20); //much simpler than screwing with a QWaitCondition (20111008)
#else
		msleep(40);
#endif
	}

	video_stop();

//	if (video_frame_buffer) {
//		free(video_frame_buffer);
//		video_frame_buffer = NULL;
//	}

	video_encoding_mutex.lock();
	trim_audio_queue_to(0);
	trim_video_queue_to(0);
	video_encoding_mutex.unlock();
}

size_t encode::trim_audio_queue_to(size_t new_length) {
	size_t drop_count = 0;
	while ((size_t)audio_queue.size() > new_length) {
		QByteArray *new_samples = audio_queue.dequeue();
		delete new_samples;
		drop_count++;
	}
	return drop_count;
}

size_t encode::trim_video_queue_to(size_t new_length) {
	size_t drop_count = 0;
	while ((size_t)video_queue.size() > new_length) {
		video_frame_buffer = video_queue.dequeue();
		free(video_frame_buffer);
		drop_count++;
	}
	return drop_count;
}

AVStream *encode::add_audio_stream(AVFormatContext *oc, enum AVCodecID codec_id) {
    AVCodecContext *c;
    AVStream *st;

    st = av_new_stream(oc, 1);
    if (!st) {
	fprintf(stderr, "Could not alloc stream\n");
	exit(1);
    }

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = AVMEDIA_TYPE_AUDIO;

    /* put sample parameters */
    c->sample_fmt = AV_SAMPLE_FMT_S16;
    //c->bit_rate = 64000;
    c->sample_rate = 44100;
    c->channels = 2;

    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
	c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

void encode::open_audio(AVFormatContext *, AVStream *st) {
    AVCodecContext *c;
    AVCodec *codec;

    c = st->codec;

    /* find the audio encoder */
    codec = avcodec_find_encoder(c->codec_id);
    if (!codec) {
	fprintf(stderr, "codec not found\n");
	exit(1);
    }

    /* open it */
    if (avcodec_open2(c, codec, nullptr) < 0) {
	fprintf(stderr, "could not open codec\n");
	exit(1);
    }

    audio_outbuf_size = 1000;
    audio_outbuf = (uint8_t*)av_malloc(audio_outbuf_size);

    /* ugly hack for PCM codecs (will be removed ASAP with new PCM
       support to compute the input frame size in samples */
    if (c->frame_size <= 1) {
	audio_input_frame_size = audio_outbuf_size / c->channels;
	switch(st->codec->codec_id) {
	case CODEC_ID_PCM_S16LE:
	case CODEC_ID_PCM_S16BE:
	case CODEC_ID_PCM_U16LE:
	case CODEC_ID_PCM_U16BE:
	    audio_input_frame_size >>= 1;
	    break;
	default:
	    break;
	}
    } else {
	audio_input_frame_size = c->frame_size;
    }
    samples_length = audio_input_frame_size * 2 * c->channels;
    samples = (int16_t*)av_malloc(samples_length);
}

void encode::close_audio(AVFormatContext *, AVStream *st) {
    avcodec_close(st->codec);

    av_free(samples);
    av_free(audio_outbuf);
}

void encode::write_audio_frame(AVFormatContext *oc, AVStream *st) {
	AVCodecContext *c;
	AVPacket pkt;
	av_init_packet(&pkt);

	c = st->codec;

	pkt.size = avcodec_encode_audio(c, audio_outbuf, audio_outbuf_size, samples);

	if (c->coded_frame && c->coded_frame->pts != AV_NOPTS_VALUE)
		pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
	pkt.flags |= AV_PKT_FLAG_KEY;
	pkt.stream_index = st->index;
	pkt.data = audio_outbuf;

	/* write the compressed frame in the media file */
	if (av_interleaved_write_frame(oc, &pkt) != 0) {
		fprintf(stderr, "Error while writing audio frame\n");
		exit(1);
	}
}


void encode::next_filename() {
	filename = filename_base + ext;

	QFileInfo info(filename);
	uint inc = 1;
	while (info.exists()) {
		inc++;
		filename = filename_base + QString("%1").arg(inc) + ext;
		info.setFile(filename);
	}
}

void encode::video_start() {
	next_filename();
	QByteArray temp_ba = filename.toLocal8Bit();
	const char *cfilename = temp_ba.data();
	fmt = av_guess_format(NULL, cfilename, NULL);
	oc = avformat_alloc_context();
	oc->oformat = fmt;
	snprintf(oc->filename, sizeof(oc->filename), "%s", cfilename);
	video_st = NULL;
	audio_st = NULL;
	if (fmt->video_codec != CODEC_ID_NONE) {
		//video_st = add_video_stream(oc, fmt->video_codec);
		AVCodecContext *c;
		video_st = av_new_stream(oc, 0);
		c = video_st->codec;
		c->codec_id = CODEC_ID_HUFFYUV; //CODEC_ID_FFVHUFF
		c->codec_type = AVMEDIA_TYPE_VIDEO;
		c->bit_rate = 5000000;
		c->width = width;
#ifdef FIELDSPLIT
		c->height = height;
#else
		c->height = height*2;
#endif
		if (ntsc) {
			c->time_base.num = 100;
#ifdef FIELDSPLIT
			c->time_base.den = 5994;
#else
			c->time_base.den = 2997;
#endif
		} else {
			c->time_base.num = 1;
#ifdef FIELDSPLIT
			c->time_base.den = 50;
#else
			c->time_base.den = 25;
#endif
		}
		c->gop_size = 12; /* emit one intra frame every twelve frames at most */
		c->pix_fmt = (PixelFormat)colorspace;
		// some formats want stream headers to be separate
		if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
			c->flags |= CODEC_FLAG_GLOBAL_HEADER;
		}
#ifndef FIELDSPLIT
		c->flags |= CODEC_FLAG_INTERLACED_ME;
		c->flags |= CODEC_FLAG_INTERLACED_DCT;
#endif
	}
	if (fmt->audio_codec != CODEC_ID_NONE) {
		audio_st = add_audio_stream(oc, CODEC_ID_PCM_S16LE);
	}
        //av_set_parameters(oc, NULL);
        //dump_format(oc, 0, cfilename, 1);
	if (video_st) {
		AVCodecContext *c;
		c = video_st->codec;
		AVCodec *codec = avcodec_find_encoder(c->codec_id);
                avcodec_open2(c, codec, nullptr);
		video_outbuf = NULL;
		if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
			video_outbuf_size = 2000000;
			video_outbuf = (uint8_t*)av_malloc(video_outbuf_size);
		}

		picture = alloc_picture(c->pix_fmt, c->width, c->height);
	}
	if (audio_st) {
		open_audio(oc, audio_st);
	}

	if (!(fmt->flags & AVFMT_NOFILE)) {
                //if (url_fopen(&oc->pb, cfilename, URL_WRONLY) < 0) {
                if (avio_open(&oc->pb, cfilename, AVIO_FLAG_WRITE) < 0) {
                        fprintf(stderr, "Could not open '%s'\n", cfilename);
			//FIXME add error handling
		}
	}
        avformat_write_header(oc, NULL);
	encoded_frames = 0;
}

void encode::video_stop() {
	av_write_trailer(oc);
	if (video_st) {
		avcodec_close(video_st->codec);
		//av_free(picture->data[0]);
		av_free(picture);
		av_free(video_outbuf);
	}
	if (audio_st) {
		close_audio(oc, audio_st);
	}
	for (uint i = 0; i < oc->nb_streams; i++) {
		av_freep(&oc->streams[i]->codec);
		av_freep(&oc->streams[i]);
	}
	if (!(fmt->flags & AVFMT_NOFILE)) {
                //url_fclose(oc->pb);
                avio_close(oc->pb);
        }
	av_free(oc);
}

void encode::video_encode_frame() {
	int out_size, ret;
	AVCodecContext *c;
	c = video_st->codec;

#ifdef FIELDSPLIT
	avpicture_fill((AVPicture *)picture, (uint8_t *)video_frame_buffer, (PixelFormat)colorspace, width, height);
#else
	avpicture_fill((AVPicture *)picture, (uint8_t *)video_frame_buffer, (PixelFormat)colorspace, width, height*2);
#endif
	if (oc->oformat->flags & AVFMT_RAWPICTURE) {
		AVPacket pkt;
		av_init_packet(&pkt);

		pkt.flags |= AV_PKT_FLAG_KEY;
		pkt.stream_index= video_st->index;
		pkt.data= (uint8_t *)picture;
		pkt.size= sizeof(AVPicture);

		ret = av_interleaved_write_frame(oc, &pkt);
	} else {
		out_size = avcodec_encode_video(c, video_outbuf, video_outbuf_size, picture);
		if (out_size > 0) {
			AVPacket pkt;
			av_init_packet(&pkt);

			if (c->coded_frame->pts != AV_NOPTS_VALUE)
				pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, video_st->time_base);
			if(c->coded_frame->key_frame)
				pkt.flags |= AV_PKT_FLAG_KEY;
			pkt.stream_index= video_st->index;
			pkt.data= video_outbuf;
			pkt.size= out_size;

			ret = av_interleaved_write_frame(oc, &pkt);
		} else {
			ret = 0;
		}
	}
	if (ret != 0) {
		fprintf(stderr, "Error while writing video frame\n");
	}
	free(video_frame_buffer);
	encoded_frames++;
}

