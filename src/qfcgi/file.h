#ifndef FILE_H
#define FILE_H

#include "QByteArray"
#include "QString"

// TODO: make the fields private

class FCgiFile
{
	public:
		QByteArray data;
		QByteArray mime; // Content-Type for this file
		QString name; // Name of the HTML field that the file was assigned to; Multiple files may be assigned to the same field
		QString filename; // The file's original filename

	public:
		FCgiFile(QByteArray data, QString name, QString filename, QByteArray mime);
		~FCgiFile();
};

#endif // FILE_H
