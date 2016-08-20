#include "norichan.h"

norichan::norichan(QWidget *parent) : QMainWindow(parent)
      ,current_field(false)
      ,currently_streaming(false)
      ,display_width(0)
      ,display_height(0)
      ,framerate(0.0)
{
	update_pixmap_bool = currently_encoding = audio_in_playing = audio_out_created = easycap_audio_not_found = audio_muted = shutting_down = false;
	ext = encode_thread.ext = ".avi";
	dropped_frame_count = frame_number = 0;
	last_imagelabel_timestamp = 0.0;

        initial_setup = true;

	frame_buffer = NULL;
	audio_in_stream = NULL;
	audio_out_stream = NULL;
	audio_out_buffer = NULL;
	audio_device = NULL;


	my_easycap_thread.colorspace = encode_thread.colorspace = colorspace = PIX_FMT_YUV422P; //PIX_FMT_YUV420P is not currently working with huffyuv (chroma ghosting results) even with the interlaced flags set (20111009)
	my_easycap_thread.colorspace_multiplier = colorspace_multiplier = colorspace == PIX_FMT_YUV420P ? 1.5 : 2.0;

	initial_width = my_easycap_thread.initial_width = encode_thread.width = 720; //always the same - due to the easycap (20111010)
	pal_d4_height = 288;
        pal_d1_width = 512*2; //was 384*2 (20130310)
	pal_d1_height = 576;

	//these buffers are fixed size - the biggest possible size regardless of whether we are currently streaming ntsc or pal (20111010)
        frame_buffer_for_display = (uchar *)malloc(pal_d1_width * pal_d1_height * 4); //4 for argb
        frame_buffer_for_display_1 = (uchar *)malloc(pal_d1_width * pal_d1_height * 4); //4 for argb
        frame_buffer_for_display_2 = (uchar *)malloc(pal_d1_width * pal_d1_height * 4); //4 for argb
        frame_buffer_for_encoding = (uchar *)malloc(initial_width * pal_d4_height * colorspace_multiplier);

	my_easycap_thread.frame_buffer_for_encoding = frame_buffer_for_encoding;
        my_easycap_thread.frame_buffer_for_display = frame_buffer_for_display;
        my_easycap_thread.frame_buffer_for_display_1 = frame_buffer_for_display_1;
        my_easycap_thread.frame_buffer_for_display_2 = frame_buffer_for_display_2;

	set_sizes();

	my_easycap_thread.brightness = 0x7F; //from 0x00 (all black) to 0xFF (all white)
	my_easycap_thread.contrast = 0x3F; //default is 0x3F
	my_easycap_thread.saturation = 0x3F; //default is 0x3F
	my_easycap_thread.hue = 0x00; //default is 0x00


        prefs_filename = QStandardPaths::writableLocation(QStandardPaths::DataLocation)+"/norichan_preferences";
        QFile infile(prefs_filename);
        if (infile.open(QIODevice::ReadOnly)) {
                QDataStream in(&infile);
                in.setVersion(QDataStream::Qt_4_7);
                in >> global_prefs;
        }

        create_menus();
	setWindowTitle("Norichan");
        rita = new Rita;
        connect(this, SIGNAL(frame_ready(QImage)), rita, SLOT(display_image(QImage)));
        setCentralWidget(rita);
        rita->show();



        deinterlacer.moveToThread(&deinterlacer_thread);
        connect(this, SIGNAL(field_ready(QImage, bool)), &deinterlacer, SLOT(add_field(QImage, bool)));
        connect(&deinterlacer, SIGNAL(image_ready(QImage)), this, SLOT(decimate(QImage)));
        deinterlacer_thread.start();

        broadcaster.moveToThread(&broadcaster_thread);

        connect(this, SIGNAL(broadcaster_start(QString,int,int,double,QString,int,int,int,int)),
                &broadcaster, SLOT(start(QString,int,int,double,QString,int,int,int,int)));

        connect(this, SIGNAL(broadcaster_stop()),
                &broadcaster, SLOT(stop()));

        connect(this, SIGNAL(frame_ready(QImage)),
                &broadcaster, SLOT(add_video_frame(QImage)));

        connect(this, SIGNAL(broadcast_audio_data(QByteArray)),
                &broadcaster, SLOT(add_audio_data(QByteArray)));

        connect(&broadcaster, SIGNAL(error_with_string(QString)),
                this, SLOT(error_dialog_slot(QString)));

        broadcaster_thread.start();


	connect(&my_easycap_thread, SIGNAL(capture_device_not_found()), this, SLOT(capture_device_not_found()));
	connect(&my_easycap_thread, SIGNAL(capture_device_not_usb_2_0()), this, SLOT(capture_device_not_usb_2_0()));
	connect(&my_easycap_thread, SIGNAL(easycap_audio_not_found()), this, SLOT(set_easycap_audio_not_found()));
	qRegisterMetaType<QImage>("QImage");
	connect(&my_easycap_thread, SIGNAL(frame_ready_signal(bool)), this, SLOT(frame_ready(bool)));
	connect(&my_easycap_thread, SIGNAL(easycap_streaming()), this, SLOT(audio_start()));
        //connect(this, SIGNAL(set_input()), &my_easycap_thread, SLOT(set_input()));
        connect(this, SIGNAL(change_channel(int)), &my_easycap_thread, SLOT(set_input(int)));
        connect(this, SIGNAL(set_standard()), &my_easycap_thread, SLOT(set_standard()));
        connect(&encode_thread, SIGNAL(dropped_frames(uint)), this, SLOT(dropped_frames(uint)));

        //QThread *my_easycap_render_thread = &(my_easycap_thread.my_easycap_render_thread);
        //connect(my_easycap_render_thread, SIGNAL(broadcast_frame(uchar*, int, int)), &udp_send, SLOT(send_image(uchar*, int, int)));



        audio_format.setSampleRate(44100); //can be one of 22050, 44100, 11025, 48000, 8000 (20111004) - must be 44100 for streaming (flv container) (20130312)
	audio_format.setChannelCount(2);
	audio_format.setSampleSize(16);
	audio_format.setCodec("audio/pcm");
	audio_format.setByteOrder(QAudioFormat::LittleEndian);
	audio_format.setSampleType(QAudioFormat::SignedInt);

	if (0) {
		debug_console = new Debug_Console;
		debug_console->start_logging();
		connect(&my_easycap_thread, SIGNAL(debug_console_set_text(QString)), debug_console, SLOT(set_text(QString)));
		connect(this, SIGNAL(debug_console_set_text(QString)), debug_console, SLOT(set_text(QString)));
		//debug_console->show2();
		raise();
		my_easycap_thread.debug_console = true;
	}

	last_imagelabel_timer.start();
	my_easycap_thread.render();
}

void norichan::dropped_frames(uint count) {
	dropped_frame_count += count;
}

void norichan::set_easycap_audio_not_found() {
	easycap_audio_not_found = true;
}

void norichan::resize_window(int w, int h) {
#ifdef Q_OS_MAC
        resize(w, h);
#endif
#ifdef Q_OS_WIN32
        resize(w, h+20); //for the menu bar
#endif
}

void norichan::set_sizes() {
        set_always_on_top();

        my_easycap_thread.ntsc = encode_thread.ntsc = global_prefs.ntsc;
        my_easycap_thread.pal60 = global_prefs.pal60;
        my_easycap_thread.svideo = global_prefs.svideo;

        my_easycap_thread.my_easycap_render_thread.f2_display = global_prefs.f2_display;
        my_easycap_thread.my_easycap_render_thread.interlaced_display = global_prefs.interlaced_display;
        my_easycap_thread.my_easycap_render_thread.field_blended_display = global_prefs.field_blended_display;
        my_easycap_thread.my_easycap_render_thread.deinterlaced_display = global_prefs.deinterlaced_display;
        my_easycap_thread.my_easycap_render_thread.deinterlaced_f2_display = global_prefs.deinterlaced_f2_display;
        my_easycap_thread.my_easycap_render_thread.changed_dimensions = true;

        initial_height = my_easycap_thread.initial_height = global_prefs.ntsc ? 480 : 576;
        targetwidth = my_easycap_thread.targetwidth = global_prefs.ntsc ? (global_prefs.widescreen ? 428 : 320) : (global_prefs.widescreen ? 512 : 384);
        targetheight = my_easycap_thread.targetheight = global_prefs.ntsc ? 240 : 288;
        targetheight_for_encoding = encode_thread.height = global_prefs.ntsc ? 240 : 288;

        framerate = 50.0;
        if (global_prefs.ntsc || global_prefs.pal60) {
                framerate = 59.94;
        }
        if (global_prefs.f2_display
                        || global_prefs.interlaced_display
                        || global_prefs.field_blended_display
                        || global_prefs.deinterlaced_f2_display) {
                framerate /= 2.0;
        }

	vis_width = targetwidth*2; vis_height = targetheight*2;
        resize_window(vis_width, vis_height);
}

void norichan::switch_aspect_ratio() {
        global_prefs.widescreen = !global_prefs.widescreen;
        set_sizes();
        emit set_standard(); //this is just a dummy so that easycap_thread sets easycap_render_thread's targetheight variable (20111013)
        save_prefs();
}

void norichan::switch_f2_display(QAction *action) {
        global_prefs.f2_display = false;
        global_prefs.interlaced_display = false;
        global_prefs.field_blended_display = false;
        global_prefs.deinterlaced_display = false;
        global_prefs.deinterlaced_f2_display = false;
        if (f1_act == action) {
                //all off
        } else if (f2_act == action) {
                global_prefs.f2_display = true;
        } else if (interlaced_act == action) {
                global_prefs.interlaced_display = true;
        } else if (field_blended_act == action) {
                global_prefs.field_blended_display = true;
        } else if (deinterlaced_act == action) {
                global_prefs.deinterlaced_display = true;
        } else if (deinterlaced_f2_act == action) {
                global_prefs.deinterlaced_f2_display = true;
        }
        set_sizes();
        save_prefs();
}

void norichan::switch_standard(QAction *action) {
        if (ntsc_act == action) {
                global_prefs.ntsc = true;
                global_prefs.pal60 = false;
        } else if (pal_act == action) {
                global_prefs.ntsc = global_prefs.pal60 = false;
        } else if (pal60_act == action) {
                global_prefs.ntsc = global_prefs.pal60 = true;
        }
        set_sizes();
        emit(set_standard());
        save_prefs();
}

void norichan::switch_input() {
        global_prefs.svideo = !global_prefs.svideo;
        set_sizes();
        //emit(set_input());
        emit(change_channel(global_prefs.svideo ? 0 : 2));
        save_prefs();
}

void norichan::switch_audio() {
        global_prefs.easycap_audio = !global_prefs.easycap_audio;
        if (audio_in_playing) { audio_in_playing = false;
                audio_in_stream->stop();
	}
	audio_start();
        save_prefs();
}

void norichan::able_acts(bool boolean) {
	ntsc_act->setEnabled(boolean);
	pal_act->setEnabled(boolean);
	pal60_act->setEnabled(boolean);

	composite_act->setEnabled(boolean);
	svideo_act->setEnabled(boolean);

	four_by_three_act->setEnabled(boolean);
	sixteen_by_nine_act->setEnabled(boolean);

	if (!easycap_audio_not_found) easycap_audio_act->setEnabled(boolean);
	line_in_audio_act->setEnabled(boolean);
}

void norichan::action_selected(QAction *action) {
        if ((action == ntsc_act && (!global_prefs.ntsc || global_prefs.pal60))
                        || (action == pal_act && global_prefs.ntsc)
                        || (action == pal60_act && !(global_prefs.ntsc && global_prefs.pal60)))
                switch_standard(action);
        if ((action == svideo_act && !global_prefs.svideo) || (action == composite_act && global_prefs.svideo)) switch_input();
        if ((action == four_by_three_act && global_prefs.widescreen) || (action == sixteen_by_nine_act && !global_prefs.widescreen)) switch_aspect_ratio();
        if ((action == easycap_audio_act && !global_prefs.easycap_audio) || (action == line_in_audio_act && global_prefs.easycap_audio)) switch_audio();

        //every other menu item ...
        if ((action == f1_act && (global_prefs.f2_display
                                  || global_prefs.interlaced_display
                                  || global_prefs.field_blended_display
                                  || global_prefs.deinterlaced_display
                                  || global_prefs.deinterlaced_f2_display
                                  ))
                        //then them again in a list
                        || (action == interlaced_act && !global_prefs.interlaced_display)
                        || (action == f2_act && !global_prefs.f2_display)
                        || (action == field_blended_act && !global_prefs.field_blended_display)
                        || (action == deinterlaced_act && !global_prefs.deinterlaced_display)
                        || (action == deinterlaced_f2_act && !global_prefs.deinterlaced_f2_display)
                        )
                switch_f2_display(action);
}

void norichan::window_action_selected(QAction *action) {
        double multiplier = 1.0;

        if (window_50_act == action) {
                multiplier = 0.5;
        } else if (window_100_act == action) {
                multiplier = 1.0;
        } else if (window_150_act == action) {
                multiplier = 1.5;
        } else if (window_200_act == action) {
                multiplier = 2.0;
        } else if (window_250_act == action) {
                multiplier = 2.5;
        } else if (window_300_act == action) {
                multiplier = 3.0;
        }

        resize_window(vis_width*multiplier, vis_height*multiplier);
}

void norichan::about() {
	QMessageBox::about(this, tr("About Norichan"),

			   tr("<p><i>\"This is such a wonderful experience here with the Buddhist.\"</i>"

			      "<p>&nbsp;<p>"

			      "<b><u>Legal information</u></b>"
                              "<p>Copyright Taiga Software LLC"
			      "<p>This software uses libraries from the <a href=\"http://ffmpeg.org/\">FFmpeg project</a> under the <a href=\"http://www.gnu.org/licenses/lgpl.html\">LGPLv3.0</a>."
			      ));
}

void norichan::channel_action_group_slot(QAction *action) {
        emit change_channel(action->text().toInt());
}

void norichan::create_menus() {
	menu_bar = new QMenuBar(this);


        file_menu = new QMenu(tr("&File"), this);

        stream_menu = new QMenu(tr("&Stream"), this);

        std_menu = new QMenu(tr("&NTSC/PAL"), this);
	std_action_group = new QActionGroup(this);
	connect(std_action_group, SIGNAL(triggered(QAction*)), this, SLOT(action_selected(QAction*)));

	input_menu = new QMenu(tr("&Input"), this);
	input_action_group = new QActionGroup(this);
	connect(input_action_group, SIGNAL(triggered(QAction*)), this, SLOT(action_selected(QAction*)));


        QMenu *channel_menu;
        QActionGroup *channel_action_group;

        channel_menu = new QMenu(tr("&Channel"), this);
        channel_action_group = new QActionGroup(this);
        connect(channel_action_group, SIGNAL(triggered(QAction*)), this, SLOT(channel_action_group_slot(QAction*)));
        for (uint channel_number = 0; channel_number <= 3; channel_number++) {
                QAction *channel_action = new QAction(tr("%1").arg(channel_number), this);
                channel_action->setShortcut(tr("Ctrl+%1").arg(channel_number));
                channel_action->setCheckable(true);
                channel_menu->addAction(channel_action);
                channel_action_group->addAction(channel_action);
        }


	aspect_ratio_menu = new QMenu(tr("Aspect &ratio"), this);
	aspect_ratio_action_group = new QActionGroup(this);
	connect(aspect_ratio_action_group, SIGNAL(triggered(QAction*)), this, SLOT(action_selected(QAction*)));

	audio_menu = new QMenu(tr("&Audio"), this);
	audio_action_group = new QActionGroup(this);
	connect(audio_action_group, SIGNAL(triggered(QAction*)), this, SLOT(action_selected(QAction*)));

	display_menu = new QMenu(tr("&Display"), this);
	display_action_group = new QActionGroup(this);
	connect(display_action_group, SIGNAL(triggered(QAction*)), this, SLOT(action_selected(QAction*)));

        window_menu = new QMenu(tr("&Window"), this);
        window_action_group = new QActionGroup(this);
        connect(window_action_group, SIGNAL(triggered(QAction*)), this, SLOT(window_action_selected(QAction*)));

	help_menu = new QMenu(tr("&Help"), this);


	set_capture_file_act = new QAction(tr("Set capture &file ..."), this);
	connect(set_capture_file_act, SIGNAL(triggered()), this, SLOT(set_capture_file()));

        start_capture_act = new QAction(tr("&Capture"), this);
	start_capture_act->setDisabled(true);
	connect(start_capture_act, SIGNAL(triggered()), this, SLOT(start_encoding()));
	stop_capture_act = new QAction(tr("&Stop capture"), this);
	stop_capture_act->setDisabled(true);
        connect(stop_capture_act, SIGNAL(triggered()), this, SLOT(stop_encoding()));

        set_streaming_url_act = new QAction(tr("Set streaming &URL ..."), this);
        connect(set_streaming_url_act, SIGNAL(triggered()), this, SLOT(set_streaming_url()));

        start_streaming_act = new QAction(tr("S&tream"), this);
        connect(start_streaming_act, SIGNAL(triggered()), this, SLOT(start_streaming()));
        stop_streaming_act = new QAction(tr("St&op streaming"), this);
        stop_streaming_act->setDisabled(true);
        connect(stop_streaming_act, SIGNAL(triggered()), this, SLOT(stop_streaming()));

        file_menu->addAction(set_capture_file_act);
	file_menu->addSeparator();
	file_menu->addAction(start_capture_act);
	file_menu->addAction(stop_capture_act);

        stream_menu->addAction(set_streaming_url_act);
        stream_menu->addSeparator();
        stream_menu->addAction(start_streaming_act);
        stream_menu->addAction(stop_streaming_act);

	ntsc_act = std_action_group->addAction(tr("&NTSC"));
	ntsc_act->setCheckable(true);
        ntsc_act->setChecked(global_prefs.ntsc && !global_prefs.pal60);
	pal_act = std_action_group->addAction(tr("&PAL"));
	pal_act->setCheckable(true);
        pal_act->setChecked(!global_prefs.ntsc);
	pal60_act = std_action_group->addAction(tr("PAL-&60"));
	pal60_act->setCheckable(true);
        pal60_act->setChecked(global_prefs.ntsc && global_prefs.pal60);
	std_menu->addAction(ntsc_act);
	std_menu->addAction(pal_act);
	std_menu->addAction(pal60_act);

	svideo_act = input_action_group->addAction(tr("&S-Video"));
	svideo_act->setCheckable(true);
        svideo_act->setChecked(global_prefs.svideo);
	composite_act = input_action_group->addAction(tr("&Composite"));
	composite_act->setCheckable(true);
        composite_act->setChecked(!global_prefs.svideo);
	input_menu->addAction(svideo_act);
	input_menu->addAction(composite_act);

	four_by_three_act = aspect_ratio_action_group->addAction(tr("&4:3"));
	four_by_three_act->setCheckable(true);
        four_by_three_act->setChecked(!global_prefs.widescreen);
	sixteen_by_nine_act = aspect_ratio_action_group->addAction(tr("&16:9"));
	sixteen_by_nine_act->setCheckable(true);
        sixteen_by_nine_act->setChecked(global_prefs.widescreen);
	aspect_ratio_menu->addAction(four_by_three_act);
	aspect_ratio_menu->addAction(sixteen_by_nine_act);

	easycap_audio_act = audio_action_group->addAction(tr("&Easycap"));
	easycap_audio_act->setCheckable(true);
        easycap_audio_act->setChecked(global_prefs.easycap_audio);
	line_in_audio_act = audio_action_group->addAction(tr("&Line-in"));
	line_in_audio_act->setCheckable(true);
        line_in_audio_act->setChecked(!global_prefs.easycap_audio);
	mute_audio_act = new QAction(tr("&Mute audio preview"), this);
	mute_audio_act->setCheckable(true);
	mute_audio_act->setChecked(audio_muted);
	connect(mute_audio_act, SIGNAL(triggered()), this, SLOT(audio_mute()));
	audio_menu->addAction(easycap_audio_act);
	audio_menu->addAction(line_in_audio_act);
	audio_menu->addSeparator();
	audio_menu->addAction(mute_audio_act);

        f1_act = display_action_group->addAction(tr("F&1"));
        f1_act->setCheckable(true);
        f1_act->setChecked(!global_prefs.f2_display);
        f2_act = display_action_group->addAction(tr("F&2"));
        f2_act->setCheckable(true);
        f2_act->setChecked(global_prefs.f2_display);
        interlaced_act = display_action_group->addAction(tr("&Interlaced"));
        interlaced_act->setCheckable(true);
        interlaced_act->setChecked(global_prefs.interlaced_display);
        field_blended_act = display_action_group->addAction(tr("&Field-blended"));
        field_blended_act->setCheckable(true);
        field_blended_act->setChecked(global_prefs.field_blended_display);
        deinterlaced_act = display_action_group->addAction(tr("&Deinterlaced"));
        deinterlaced_act->setCheckable(true);
        deinterlaced_act->setChecked(global_prefs.deinterlaced_display);
        deinterlaced_f2_act = display_action_group->addAction(tr("&Deinterlaced (F2)"));
        deinterlaced_f2_act->setCheckable(true);
        deinterlaced_f2_act->setChecked(global_prefs.deinterlaced_f2_display);
        display_menu->addAction(f1_act);
        display_menu->addAction(f2_act);
        display_menu->addAction(interlaced_act);
        display_menu->addAction(field_blended_act);
        display_menu->addAction(deinterlaced_act);
        display_menu->addAction(deinterlaced_f2_act);

        always_on_top_act = new QAction(tr("&Always on top"), this);
        always_on_top_act->setCheckable(true);
        always_on_top_act->setChecked(global_prefs.always_on_top);
        connect(always_on_top_act, SIGNAL(triggered()), this, SLOT(switch_always_on_top()));
        window_50_act = window_action_group->addAction(tr("50%")); window_50_act->setCheckable(true);
        window_100_act = window_action_group->addAction(tr("100%")); window_100_act->setCheckable(true);
        window_150_act = window_action_group->addAction(tr("150%")); window_150_act->setCheckable(true);
        window_200_act = window_action_group->addAction(tr("200%")); window_200_act->setCheckable(true);
        window_250_act = window_action_group->addAction(tr("250%")); window_250_act->setCheckable(true);
        window_300_act = window_action_group->addAction(tr("300%")); window_300_act->setCheckable(true);
        window_100_act->setChecked(true);
        window_menu->addAction(always_on_top_act);
        window_menu->addSeparator();
        window_menu->addAction(window_50_act);
        window_menu->addAction(window_100_act);
        window_menu->addAction(window_150_act);
        window_menu->addAction(window_200_act);
        window_menu->addAction(window_250_act);
        window_menu->addAction(window_300_act);

	about_act = new QAction(tr("&About Norichan"), this);
	connect(about_act, SIGNAL(triggered()), this, SLOT(about()));
	about_qt_act = new QAction(tr("About &Qt"), this);
	connect(about_qt_act, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
	help_menu->addAction(about_act);
	help_menu->addAction(about_qt_act);


        menu_bar->addMenu(file_menu);
        menu_bar->addMenu(stream_menu);
        menu_bar->addMenu(std_menu);
	menu_bar->addMenu(input_menu);
        //
        menu_bar->addMenu(channel_menu);
        //
	menu_bar->addMenu(aspect_ratio_menu);
	menu_bar->addMenu(audio_menu);
        menu_bar->addMenu(display_menu);
        menu_bar->addMenu(window_menu);
	menu_bar->addMenu(help_menu);

	setMenuBar(menu_bar);
}

void norichan::set_capture_file() {
	QFileDialog newdialog(this
			      ,tr("Set Capture File")
                              ,QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
			      ,QString("*%1").arg(ext)
			      );
	newdialog.setFileMode(QFileDialog::AnyFile);
	newdialog.setAcceptMode(QFileDialog::AcceptSave);
	newdialog.setOptions(newdialog.options() | QFileDialog::DontConfirmOverwrite);
	if (!newdialog.exec()) { //they hit cancel
		return;
	}
	QStringList newfilenames = newdialog.selectedFiles(); //even though we only want one ...
	QString newfilename = newfilenames[0];
	encode_thread.filename_base = newfilename.replace(QRegExp(QString("\\%1$").arg(ext), Qt::CaseInsensitive), "");

	start_capture_act->setDisabled(false);
}

void norichan::set_streaming_url() {
        bool ok;
        QString text = QInputDialog::getText(this, tr("Set Streaming URL"),
                                             tr("URL:"), QLineEdit::Normal,
                                             global_prefs.streaming_url, &ok);
        if (ok && !text.isEmpty()) {
                global_prefs.streaming_url = text;
                save_prefs();
        }
}

void norichan::start_encoding() {
	if (currently_encoding) return;
#ifndef FIELDSPLIT
	frame_buffer = (uchar *)malloc(initial_width * pal_d4_height * colorspace_multiplier*2); //a large enough buffer whether we are capturing ntsc or pal (20111010)
#endif
	encode_thread.render();
	set_capture_file_act->setDisabled(true);
	start_capture_act->setDisabled(true);
	stop_capture_act->setEnabled(true);
	able_acts(false);
	currently_encoding = my_easycap_thread.my_easycap_render_thread.currently_encoding = true;
}

void norichan::stop_encoding() {
	if (!currently_encoding) return;
	encode_thread.abort = true;
	encode_thread.wait();
	set_capture_file_act->setEnabled(true);
	start_capture_act->setEnabled(true);
        stop_capture_act->setDisabled(true);
	able_acts(true);
	if (frame_buffer) {
		free(frame_buffer);
		frame_buffer = NULL;
	}
	currently_encoding = my_easycap_thread.my_easycap_render_thread.currently_encoding = false;
}

void norichan::start_streaming() {
        if (currently_streaming) return;

        start_streaming_act->setEnabled(false);
        stop_streaming_act->setEnabled(true);

        emit broadcaster_start(global_prefs.streaming_url,
                               display_width, display_height, framerate,
                               "medium", 400, 128, //x264 preset, video bitrate kbit, audio bitrate kbit
                               44100, 2);

        currently_streaming = true;
}

void norichan::stop_streaming() {
        if (!currently_streaming) return;

        start_streaming_act->setEnabled(true);
        stop_streaming_act->setEnabled(false);

        emit broadcaster_stop();

        currently_streaming = false;
}

void norichan::capture_device_not_found() {
        if (1) {
                QMessageBox::critical(this, tr("Norichan"), tr("Sorry, Norichan could not locate the EasyCap hardware attached to this computer. Please connect the hardware, then try again."));
                QApplication::exit(1);
        }
}

void norichan::capture_device_not_usb_2_0() {
        QMessageBox::critical(this, tr("Norichan"), tr("Sorry, the EasyCap must be plugged in to a USB 2.0 port. Please ensure that it is plugged in to a USB 2.0 (High Speed) port, then try again."));
        QApplication::exit(1);
}

void norichan::frame_ready(bool high_field) {
	frame_number++;
	if (shutting_down) return;

	//for encoding
	if (currently_encoding) {
#ifdef FIELDSPLIT
		uchar *frame_buffer = (uchar *)malloc(frame_buffer_for_encoding_size);
		memcpy(frame_buffer, frame_buffer_for_encoding, frame_buffer_for_encoding_size);
		encode_thread.add_video_frame(frame_buffer);
#else
		int linewidth = initial_width;
		int start = high_field ? linewidth : 0;
		uint8_t *src = (uint8_t *)frame_buffer_for_encoding;
		uint8_t *dst = (uint8_t *)frame_buffer;

		//luma
		dst += high_field ? linewidth : 0;
		uint top = targetheight_for_encoding;
                for (uint line = 0; line < top; ++line) {
			memcpy(dst, src, linewidth);
			src += linewidth;
			dst += linewidth*2;
		}
		//chroma
		int halflinewidth = linewidth / colorspace_multiplier;
		dst += high_field ? halflinewidth : linewidth;
		top *= 2;
                for (uint line = 0; line < top; ++line) {
			memcpy(dst, src, halflinewidth);
			src += halflinewidth;
			dst += linewidth;
		}

		if (!start) {
			size_t new_frame_buffer_length = initial_width * pal_d4_height * colorspace_multiplier*2; //a large enough buffer whether we are capturing ntsc or pal (20111010)
			uchar *new_frame_buffer = (uchar *)malloc(new_frame_buffer_length);
			memcpy(new_frame_buffer, frame_buffer, new_frame_buffer_length);
			encode_thread.add_video_frame(frame_buffer);
			frame_buffer = new_frame_buffer;
		}
#endif
	}


	//for display
	if (windowState() & Qt::WindowMinimized) { //they can't see it anyway - drop the frame
		return;
	}
        if (last_imagelabel_timestamp && last_imagelabel_timer.elapsed() - last_imagelabel_timestamp < 5.0) { //don't set pixmaps too quickly - it locks the gui (20111201) was 10.0 (20121124)
		return;
	}
        if ((global_prefs.f2_display || global_prefs.interlaced_display || global_prefs.field_blended_display) && frame_number % 2 == 0) { //we get called every field (in order to encode) even if we only draw every frame (20120227)
                return;
        }

        int frame_buffer_for_display_height = targetheight*2;
        if (global_prefs.field_blended_display || global_prefs.deinterlaced_display || global_prefs.deinterlaced_f2_display) {
                frame_buffer_for_display_height = targetheight;
        }
        display_width = targetwidth*2;
        display_height = frame_buffer_for_display_height;
        QImage image = QImage(frame_buffer_for_display, display_width, display_height, display_width*4, QImage::Format_RGB32).copy();

        if (global_prefs.deinterlaced_display || global_prefs.deinterlaced_f2_display) {
                display_height *= 2;
                emit field_ready(image, high_field);
                return;
        }

        if (debug_console) timer.start();
        emit frame_ready(image);
        if (debug_console) emit(debug_console_set_text(QString("imageLabel->setPixmap: %1").arg(timer.elapsed())));


	last_imagelabel_timestamp = last_imagelabel_timer.elapsed();
}

void norichan::closeEvent(QCloseEvent *event) {
	if (currently_encoding) {
		QMessageBox infobox(QMessageBox::Information,
				    "Norichan",
				    tr("Really close Norichan?"),
				    QMessageBox::Ok | QMessageBox::Cancel
				    );
		if (infobox.exec() == QMessageBox::Cancel) {
			event->ignore();
			return;
		}
	}

	shutting_down = true;
	event->accept();

	if (currently_encoding) stop_encoding();

	my_easycap_thread.abort = true;
	my_easycap_thread.wait();

        deinterlacer_thread.quit();
        deinterlacer_thread.wait();

        if (currently_streaming) emit broadcaster_stop();
        broadcaster_thread.quit();
        broadcaster_thread.wait();

        QApplication::quit();
}

void norichan::audio_start() {
        if (initial_setup) { initial_setup = false; //at this point the easycap is already streaming so it's safe to set the resolution to a lower value if we're using ntsc (20120217)
                set_sizes();
                emit(set_standard()); //the standard is irrelevant - this is just to kick the easycap thread to make it restart (20120217)
        }

	if (audio_in_playing) return;

	bool found_easycap_audio_device = false;
	if (!easycap_audio_not_found) {
		foreach (const QAudioDeviceInfo &deviceInfo, QAudioDeviceInfo::availableDevices(QAudio::AudioInput)) {
			//QMessageBox::critical(this, tr("Norichan"), deviceInfo.deviceName());
			if (deviceInfo.deviceName() == "USB Audio Interface"
					|| deviceInfo.deviceName().contains("Digital Audio Interface")) {
				found_easycap_audio_device = true;
				easycap_audio = deviceInfo;
				break;
			}
		}
		if (found_easycap_audio_device) {
			if (!easycap_audio.isFormatSupported(audio_format)) {
				QMessageBox::critical(this, tr("Norichan"), tr("audio format not supported"));
				found_easycap_audio_device = false;
			}
		} else {
			//QMessageBox::critical(this, tr("Norichan"), tr("USB Audio Interface not found"));
		}
	} else {
		//QMessageBox::critical(this, tr("Norichan"), tr("easycap_audio_not_found"));
	}
	if (!found_easycap_audio_device) {
		easycap_audio_not_found = true;
                global_prefs.easycap_audio = false;
		easycap_audio_act->setChecked(false);
		easycap_audio_act->setDisabled(true);
		line_in_audio_act->setChecked(true);
	}

        if (global_prefs.easycap_audio) {
		audio_in_stream = new QAudioInput(easycap_audio, audio_format);
	} else {
		audio_in_stream = new QAudioInput(audio_format); //better hope their default input supports this format! (20111014)
	}
#ifdef Q_OS_MAC
	audio_in_stream->setBufferSize(1024 * 1024); //a large enough buffer is important to prevent fatal overruns (20111005)
#endif

	audio_device = audio_in_stream->start();
	connect(audio_device, SIGNAL(readyRead()), this, SLOT(audio_read()));

	if (!audio_out_created) {
		audio_out_stream = new QAudioOutput(audio_format, this); //better hope their default output supports this format! (20111008)
		audio_out_buffer = audio_out_stream->start();
		audio_out_created = true;
	} else if (audio_muted) {
		mute_audio_act->trigger();
	}

	audio_in_playing = true;
}

void norichan::audio_mute() {
	if (audio_muted) {
		audio_out_stream->resume();
	} else {
		audio_out_stream->suspend();
	}
	audio_muted = !audio_muted;
}

void norichan::audio_read() {
	if (shutting_down) return;

	if (!audio_device) return;

	QByteArray *new_data = new QByteArray(audio_device->readAll());
	if (!new_data->size()) { //why does this happen? (20111006)
		delete new_data;
		return;
	}

	audio_out_buffer->write(*new_data);
        if (currently_streaming) {
                QByteArray to_broadcaster(*new_data);
                emit broadcast_audio_data(to_broadcaster);
        }
	if (currently_encoding) {
		encode_thread.add_audio_frame(new_data);
	} else {
		delete new_data;
	}
}

void norichan::save_prefs() {
        QFile outfile(prefs_filename);
        if (!outfile.open(QIODevice::WriteOnly)) {
                QMessageBox::critical(this,
                                      tr("Save Preferences Error"),
                                      tr("There was an error saving the preferences file: %1").arg(outfile.errorString()),
                                      QMessageBox::Ok);
                return;
        }
        QDataStream out(&outfile);
        out.setVersion(QDataStream::Qt_4_7);
        out << global_prefs;
}

void norichan::set_always_on_top() {
        if (global_prefs.always_on_top) {
                setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
        } else {
                setWindowFlags(windowFlags() ^ Qt::WindowStaysOnTopHint);
        }
        show();
}

void norichan::switch_always_on_top() {
        global_prefs.always_on_top = !global_prefs.always_on_top;
        set_always_on_top();
        save_prefs();
}

void norichan::decimate(QImage frame) {
        current_field = !current_field;
        if (global_prefs.deinterlaced_display || current_field) emit frame_ready(frame);
}

void norichan::error_dialog_slot(QString text) {
        QMessageBox::critical(this, tr("Norichan"), text);
}
