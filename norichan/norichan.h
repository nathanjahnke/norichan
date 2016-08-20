#ifndef NORICHAN_H
#define NORICHAN_H

#include <QActionGroup>
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#include <QMenu>
#include <QMenuBar>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include "easycap_thread.h"
#include "encode.h"
#include "debug_console.h"
#include "norichan_prefs.h"
#include "rita.h"
#include "deinterlacer.h"
#include "rtmp_broadcaster.h"

class norichan : public QMainWindow {
	Q_OBJECT

public:
	norichan(QWidget *parent = 0);
	easycap_thread my_easycap_thread;
	encode encode_thread;
        uchar *frame_buffer_for_encoding, *frame_buffer_for_display, *frame_buffer_for_display_1, *frame_buffer_for_display_2;
        int initial_width, initial_height, targetwidth, targetheight, targetheight_for_encoding, vis_width, vis_height, pal_d4_height, pal_d1_height, pal_d1_width;
        bool debug;
	Debug_Console *debug_console;


signals:
	void set_input();
	void set_standard();
	void debug_console_set_text(QString);
        void change_channel(int);
        void frame_ready(QImage);
        void field_ready(QImage, bool);
        void broadcaster_start(QString,int,int,double,QString,int,int,int,int);
        void broadcaster_stop();
        void broadcast_audio_data(QByteArray);

public slots:
	void capture_device_not_found();
	void capture_device_not_usb_2_0();
	void frame_ready(bool);
	void audio_start();
	void audio_read();
        void error_dialog_slot(QString text);

private slots:
	void set_capture_file();
	void start_encoding();
	void stop_encoding();
        void action_selected(QAction *);
        void window_action_selected(QAction *);
	void about();
	void dropped_frames(uint);
	void audio_mute();
	void set_easycap_audio_not_found();
        void switch_always_on_top();
        void switch_f2_display(QAction*);
        void channel_action_group_slot(QAction *);
        void decimate(QImage);
        void start_streaming();
        void stop_streaming();
        void set_streaming_url();

private:
        void closeEvent(QCloseEvent *);
	void create_menus();
	void set_sizes();
	void able_acts(bool);
	void switch_aspect_ratio();
	void switch_input();
	void switch_standard(QAction *);
	void switch_audio();
        void set_always_on_top();

	QMenuBar *menu_bar;
        QMenu *file_menu, *stream_menu, *std_menu, *input_menu, *aspect_ratio_menu, *audio_menu, *display_menu, *window_menu, *help_menu;
	QAction *set_capture_file_act, *start_capture_act, *stop_capture_act,
        *set_streaming_url_act, *start_streaming_act, *stop_streaming_act,
	*ntsc_act, *pal_act, *pal60_act,
	*svideo_act, *composite_act,
	*four_by_three_act, *sixteen_by_nine_act,
	*easycap_audio_act, *line_in_audio_act, *mute_audio_act,
        *f1_act, *f2_act, *interlaced_act, *field_blended_act, *deinterlaced_act, *deinterlaced_f2_act,
        *always_on_top_act,
        *window_50_act, *window_100_act, *window_150_act, *window_200_act, *window_250_act, *window_300_act,
	*about_act, *about_qt_act;
        QActionGroup *std_action_group, *input_action_group, *aspect_ratio_action_group, *audio_action_group, *display_action_group, *window_action_group;

//	QGraphicsScene *scene;
//	QGraphicsPixmapItem *imageLabel;
//	QGLWidget *opengl_widget;
//	VideoItem *videoItem;
//	QGraphicsView *scrollArea;
        Rita *rita;

	QMutex update_pixmap_mutex;
	bool update_pixmap_bool;

	bool currently_encoding, shutting_down;

	QAudioInput* audio_in_stream;
	QIODevice *audio_device;
	QAudioDeviceInfo easycap_audio;
	QIODevice *audio_out_buffer;
	QAudioOutput *audio_out_stream;
	QAudioFormat audio_format;

        GTimer timer;

#ifndef FIELDSPLIT
	uchar *frame_buffer;
#endif

	double colorspace_multiplier;
	int colorspace;
	QString ext;
	bool audio_in_playing;
        bool audio_out_created, easycap_audio_not_found, audio_muted;
	uint dropped_frame_count;

	uint frame_number;

	double last_imagelabel_timestamp;
        GTimer last_imagelabel_timer;

        Norichan_Prefs global_prefs;
        QString prefs_filename;
        void save_prefs();

        bool initial_setup;

        void resize_window(int, int);

        Deinterlacer deinterlacer;
        QThread deinterlacer_thread;
        bool current_field;

        RTMP_Broadcaster broadcaster;
        QThread broadcaster_thread;

        bool currently_streaming;

        int display_width, display_height;
        double framerate;

};

#endif // NORICHAN_H
