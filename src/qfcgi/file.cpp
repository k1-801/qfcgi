#include "file.h"

FCgiFile::FCgiFile(QByteArray data, QString name, QString filename, QByteArray mime)
{
	this->data = data;
	this->name = name;
	this->filename = filename;
	this->mime = mime;
}

FCgiFile::~FCgiFile() {}
