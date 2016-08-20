#include "easycap_render_thread.h"

#ifdef NORICHAN
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
#endif

easycap_render_thread::easycap_render_thread(QObject *parent) : QThread(parent) {
	abort = running = false;
	save_data = false;
	debug_console = false;
	f2_display = false;
	frame_number = 0;
}

void easycap_render_thread::render(bool high_field) {
	//qDebug() << "easycap_render_thread::render()";
	QMutexLocker locker(&mutex); //locks mutex in this scope
	frame_number++;
	if (!isRunning()) { //initial startup
		field_boolean = high_field;
		running = true;
		luma_size = (initial_width * 576) / 2; //make a large enough buffer regardless of whether we are capturing ntsc or pal (20111010)
		luma = (unsigned char *)malloc(sizeof(unsigned char) * luma_size);
		if (save_data) {
			output.setFileName("/input2");
			output.open(QIODevice::WriteOnly | QIODevice::Truncate);
			out.setDevice(&output);
			out.setVersion(QDataStream::Qt_4_0);
		}
		currently_encoding = false;
		start(); //inherits my_easycap_thread run priority
	} else if (!running) { //don't interfere with the thread if it's already working - this will result in a duplicate frame, but if the machine can't keep up then it can't keep up (20110709)
		field_boolean = high_field;
		running = true;
		condition.wakeOne();
	} else {
		if (debug_console) emit(debug_console_set_text(QString("easycap_render_thread dropping frame")));
		//qDebug() << "easycap_render_thread dropping frame!";
	}
}

void easycap_render_thread::run() {
    //qDebug() << "easycap_render_thread::run()";

#ifdef NORICHAN
	source_picture = NULL;
	display_dest_picture = NULL;
	encode_dest_picture = NULL;

	int convert_height = 0;
        int last_targetheight = 0;
        int last_targetwidth = 0;
        changed_dimensions = true;

	struct SwsContext *display_img_convert_ctx, *encode_img_convert_ctx;
	display_img_convert_ctx = encode_img_convert_ctx = NULL;
#endif
	mutex.lock();
	while (!abort) {
		mutex.unlock();
#ifdef NORICHAN
		//qDebug() << "if (last_targetheight != targetheight) { last_targetheight = targetheight;";
                if (last_targetwidth != targetwidth || last_targetheight != targetheight || changed_dimensions) {
                        last_targetwidth = targetwidth;
                        last_targetheight = targetheight;
                        changed_dimensions = false;
			convert_height = initial_height/2;


			if (source_picture) {
				//av_free(source_picture->data[0]);
				av_free(source_picture);
			}
			source_picture = alloc_picture(SOURCE_PIX_FMT, initial_width, initial_height);

			if (display_dest_picture) {
				//av_free(display_dest_picture->data[0]);
				av_free(display_dest_picture);
			}
			display_dest_picture = alloc_picture(DISPLAY_DEST_PIX_FMT, targetwidth*2, targetheight*2);

			if (encode_dest_picture) {
				//av_free(encode_dest_picture->data[0]);
				av_free(encode_dest_picture);
			}
			encode_dest_picture = alloc_picture((PixelFormat)colorspace, initial_width, convert_height);


			if (display_img_convert_ctx) sws_freeContext(display_img_convert_ctx);
                        int display_img_convert_ctx_height = targetheight*2;
                        if (interlaced_display || field_blended_display || deinterlaced_display || deinterlaced_f2_display) {
                                display_img_convert_ctx_height = targetheight;
                        }
                        display_img_convert_ctx = sws_getContext(initial_width, convert_height,
                                                                 SOURCE_PIX_FMT,
                                                                 targetwidth*2, display_img_convert_ctx_height,
                                                                 DISPLAY_DEST_PIX_FMT,
                                                                 SWS_FAST_BILINEAR, NULL, NULL, NULL);

			if (encode_img_convert_ctx) sws_freeContext(encode_img_convert_ctx);
                        encode_img_convert_ctx = sws_getContext(initial_width, convert_height,
                                                                SOURCE_PIX_FMT,
                                                                initial_width, convert_height,
                                                                (PixelFormat)colorspace,
                                                                SWS_BICUBIC, NULL, NULL, NULL);
		}
#endif

		if (debug_console) timer.start();
#ifdef NORICHAN
		avpicture_fill((AVPicture *)source_picture, (uint8_t *)lol_dest_2, SOURCE_PIX_FMT, initial_width, convert_height);


                if (interlaced_display || field_blended_display || !f2_display || deinterlaced_display || deinterlaced_f2_display || field_boolean) {
                        sws_scale(display_img_convert_ctx,
                                  source_picture->data, source_picture->linesize,
                                  0, convert_height,
                                  display_dest_picture->data, display_dest_picture->linesize);

                        if (interlaced_display || field_blended_display) {
                                uchar *dest_buffer = !field_boolean ? frame_buffer_for_display_1 : frame_buffer_for_display_2;
                                memcpy(dest_buffer, display_dest_picture->data[0], targetwidth*2 * targetheight * 4);
                        } else if (deinterlaced_display || deinterlaced_f2_display) {
                                memcpy(frame_buffer_for_display, display_dest_picture->data[0], targetwidth*2 * targetheight * 4);
                        } else {
                                memcpy(frame_buffer_for_display, display_dest_picture->data[0], targetwidth*2 * targetheight*2 * 4);
                        }
                }
                if ( field_boolean && (interlaced_display || field_blended_display) ) {
                        uint8_t *src1 = (uint8_t *)frame_buffer_for_display_1;
                        uint8_t *src2 = (uint8_t *)frame_buffer_for_display_2;
                        uint8_t *dst = (uint8_t *)frame_buffer_for_display;

                        uint linewidth = targetwidth*2 * 4; //rgb
                        uint linewidth_times_2 = linewidth*2;
                        uint top = targetheight*linewidth;

                        if (interlaced_display) {
                                for (uint i = 0; i < top; i += linewidth) {
                                        memcpy(dst, src1, linewidth);
                                        memcpy(dst + linewidth, src2, linewidth);
                                        dst += linewidth_times_2;
                                        src1 += linewidth;
                                        src2 += linewidth;
                                }
                        } else if (field_blended_display) {
                                for (uint i = 0; i < top; ++i) {
                                        dst[i] = (src1[i] + src2[i]) / 2;
                                }
                        }
                }

		if (currently_encoding) {
			sws_scale(encode_img_convert_ctx,
				  source_picture->data, source_picture->linesize,
				  0, convert_height,
				  encode_dest_picture->data, encode_dest_picture->linesize);
                        memcpy(frame_buffer_for_encoding, encode_dest_picture->data[0], initial_width * convert_height * colorspace_multiplier); //this relies on data[1], data[2] etc being adjacent to data[0] (20111013)
                        memset(frame_buffer_for_encoding, 0, initial_width * colorspace_multiplier); //get rid of the green line at the top of the image (20120412)
                }

		//qDebug() << "emit(frame_ready(field_boolean));";
                emit frame_ready(field_boolean);
                //emit broadcast_frame(frame_buffer_for_display, targetwidth, targetheight);
#else
		//we are assuming that initial_height and targetheight are the same - that is, that we are working with half-height input (20110913)

		//first get rid of chrominance - since targetdifferencefactor may not be a factor of 2, it will complicate resizing
		for (unsigned int luma_pos = 0; luma_pos < luma_size; luma_pos++) {
			luma[luma_pos] = lol_dest_2[luma_pos*2+1];
		}

		double targetdifferencefactor = double(initial_width) / double(targetwidth);
		double top = (double)luma_size;
		unsigned int i = 0;
                if (0) {
                        for (double counter = 0.0; counter < top; counter += targetdifferencefactor) {
                                incoming_image_buffer[i++] = 255 - luma[(int)counter];
                        }
                } else {
                        for (double counter = 0.0; counter < top; counter += targetdifferencefactor) {
                                incoming_image_buffer[i++] = luma[(int)counter];
                        }
                }

		if (save_data) {
			int written = out.writeRawData((char*)incoming_image_buffer, targetwidth * targetheight);
			if (written != targetwidth * targetheight) {
				qDebug() << "save data: write error:" << written;
			}
		}
#endif

		if (debug_console) emit(debug_console_set_text(QString("easycap_render_thread: %1").arg(timer.restart())));
		mutex.lock();
		running = false;
		if (abort) break; //needed to prevent deadlock (20111013)
		//qDebug() << "condition.wait(&mutex);";
		condition.wait(&mutex);
	}
	mutex.unlock();
#ifdef NORICHAN
	//qDebug() << "av_free(encode_dest_picture);";
	//av_free(encode_dest_picture->data[0]);
	av_free(encode_dest_picture);
	//qDebug() << "av_free(display_dest_picture);";
	//av_free(display_dest_picture->data[0]);
	av_free(display_dest_picture);
	//qDebug() << "av_free(source_picture);";
	//av_free(source_picture->data[0]);
	av_free(source_picture);
#endif
    //qDebug() << "easycap render thread exited successfully";
}
