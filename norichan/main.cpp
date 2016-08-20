#include "norichan.h"

int main(int argc, char *argv[]) {
	QApplication a(argc, argv);

	norichan w;
	w.show();

	return a.exec();
}
