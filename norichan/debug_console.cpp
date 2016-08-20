#include "debug_console.h"

Debug_Console::Debug_Console(QWidget *parent) : QMainWindow(parent) {
	text = "";
	max_length = 1000;
	shown = flush = false;

	file = NULL;
	outfile = NULL;

	debug_label = new QLabel;
	setCentralWidget(debug_label);
	setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
	screen_width = QApplication::desktop()->screenGeometry().width();
	screen_height = QApplication::desktop()->screenGeometry().height();
	width = screen_width;
	height = 240;
	resize(width, height);
	debug_label->resize(size());
	setMaximumSize(width, height);
	debug_label->setMaximumSize(width, height);
	move(0, screen_height - height);
}

Debug_Console::~Debug_Console() {
	if (file) {
		if (outfile) {
			outfile->flush();
		}
		file->close();
		delete file;
	}
}


void Debug_Console::show2() {
	this->show();
	shown = true;
}

void Debug_Console::start_logging() {
        file = new QFile(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)+"/debug_console_out.txt");
	if (file->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
		outfile = new QTextStream(file);
	}
	start_timer();
}

void Debug_Console::start_timer() {
	timer.start();
}

void Debug_Console::set_text(QString new_text) {
	if (!new_text.length()) {
		return;
	}

	if (outfile) {
		//qDebug() << QString("%1 " + new_text).arg(timer.elapsed(), 10, 'f', 3);
		*outfile << QString("%1 " + new_text + "\n").arg(timer.elapsed(), 10, 'f', 3);
		if (flush) outfile->flush();
	} else {
		qDebug() << QString("%1 " + new_text).arg(timer.elapsed(), 10, 'f', 3);
	}

	if (shown) {
		text = QString(new_text + "\n" + text);

		if (text.length() > max_length) {
			text.truncate(max_length);
		}

		debug_label->setText(text);
	}
}
