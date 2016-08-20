#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include <QString>
#include <QLabel>
#include <QFile>
#include <QTextStream>
#include <QMainWindow>
#include <QApplication>
#include <QStandardPaths>
#include <QDebug>
#include <QDesktopWidget>

#include "gtimer.h"

class Debug_Console : public QMainWindow
{
	Q_OBJECT
public:
	explicit Debug_Console(QWidget *parent = 0);
	~Debug_Console();
	QLabel *debug_label;
	void start_logging();
	void show2();
	void start_timer();
	bool flush;

signals:

public slots:
	void set_text(QString);

private:
	int width, height, screen_width, screen_height;
	QString text;
	int max_length;
	QFile *file;
	QTextStream *outfile;
	bool shown;
        GTimer timer;

};

#endif // DEBUG_CONSOLE_H
