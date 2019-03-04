/**
 * This file is part of QFCgi.
 *
 * QFCgi is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * QFCgi is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with QFCgi. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QBuffer>
#include <QtGlobal>

#include "connection.h"
#include "record.h"
#include "request.h"
#include "stream.h"

#define q2Debug(format, args...) qDebug("[%d] " format, this->id, ##args)

QFCgiRequest::QFCgiRequest(int id, bool keepConn, QFCgiConnection *parent) : QObject(parent)
{
	this->id = id;
	this->keepConn = keepConn;
	this->in = new QFCgiStream(this);
	this->out = new QFCgiStream(this);
	this->err = new QFCgiStream(this);

	this->in->open(QIODevice::ReadOnly);
	this->out->open(QIODevice::WriteOnly);
	this->err->open(QIODevice::WriteOnly);

	_headers.insert("Content-type", "text/html"); // A default value
	data_sent = false;

	connect(this->out, SIGNAL(bytesWritten(qint64)), this, SLOT(onOutBytesWritten(qint64)));
	connect(this->err, SIGNAL(bytesWritten(qint64)), this, SLOT(onErrBytesWritten(qint64)));

}

int QFCgiRequest::getId() const
{
	return this->id;
}

bool QFCgiRequest::keepConnection() const
{
	return this->keepConn;
}

void QFCgiRequest::endRequest(quint32 appStatus)
{
	if(!data_sent)
	{
		sendHeaders();
	}

	QFCgiConnection *connection = qobject_cast<QFCgiConnection*>(parent());

	connection->send(QFCgiRecord::createOutStream(this->id, QByteArray()));
	connection->send(QFCgiRecord::createErrStream(this->id, QByteArray()));
	connection->send(QFCgiRecord::createEndRequest(this->id, appStatus, QFCgiRecord::FCGI_REQUEST_COMPLETE));

	if (!keepConnection()) {
		q2Debug("endRequest - about to close connection");
		connection->closeConnection();
	}
}

QHash<QString, QByteArray> QFCgiRequest::params() const
{
	return _params;
}

QByteArray QFCgiRequest::param(const QString &name) const
{
	return _params.value(name);
}

QByteArray QFCgiRequest::getOption(const QString &name)
{
	return getOptions().value(name);
}

QByteArray QFCgiRequest::postOption(const QString &name)
{
	return postOptions().value(name);
}

QByteArray QFCgiRequest::cookie(const QString &name)
{
	return cookies().value(name);
}

QIODevice* QFCgiRequest::getIn() const
{
	return this->in;
}

QIODevice* QFCgiRequest::getOut() const
{
	return this->out;
}

QIODevice* QFCgiRequest::getErr() const
{
	return this->err;
}

QHash<QString, QByteArray>& QFCgiRequest::getOptions()
{
	if(!get_processed)
	{
		QByteArray raw = _params.value("QUERY_STRING");
		processEncodedFormData(raw, _get);
		get_processed = 1;
	}
	return _get;
}

QHash<QString, QByteArray>& QFCgiRequest::postOptions()
{
	if(!post_processed)
	{
		processPost();
	}
	return _post;
}

QHash<QString, QByteArray>& QFCgiRequest::cookies()
{
	if(!cookies_processed)
	{
		// Process HTTP_COOKIE
		processCookies();
	}
	return _cookies;
}

bool QFCgiRequest::setHeader(QString name, QString value, bool override)
{
	if(!data_sent)
	{
		name = name.toLower();
		name[0] = name[0].toUpper();
		if(name == "Location" || name == "Content-type")
		{
			override = 1; // Force an override if this type of header does not support multiple values
		}
		if(override)
		{
			_headers.replace(name, value);
		}
		else
		{
			_headers.insert(name, value);
		}
		return 1;
	}
	return 0;
}

bool QFCgiRequest::sendHeaders()
{
	if(!data_sent)
	{
		data_sent = 1;
		// Send headers
		// Note, however, that the out stream buffer already has data in it, so we need to be careful
		QByteArray toSend;
		for(auto it = _headers.begin(); it != _headers.end(); ++it)
		{
			toSend.append(it.key().toUtf8());
			if(it.value().size())
			{
				toSend.append(": ");
				toSend.append(it.value().toUtf8());
			}
			toSend.append("\n");
		}
		toSend.append("\n");
		propagate(toSend);
		return true;
	}
	return false;
}

bool QFCgiRequest::dataSent()
{
	return this->data_sent;
}

void QFCgiRequest::onOutBytesWritten(qint64 bytes)
{
	Q_UNUSED(bytes);
	// This is a buffer device, the data is actually only sent here, so we get a chance to send headers before it happens
	if(!data_sent)
	{
		sendHeaders();
	}
	propagate(this->out->getBuffer());
}

void QFCgiRequest::propagate(QByteArray& ba)
{
	int nbytes = qMin(65535, ba.size());
	QFCgiConnection *connection = qobject_cast<QFCgiConnection*>(parent());

	while(nbytes)
	{
		QFCgiRecord record = QFCgiRecord::createOutStream(this->id, ba.left(nbytes));

		ba.remove(0, nbytes);
		connection->send(record);
		nbytes = qMin(65535, ba.size());
	}
}

void QFCgiRequest::onErrBytesWritten(qint64 bytes)
{
	Q_UNUSED(bytes);
	QByteArray &ba = this->err->getBuffer();
	int nbytes = qMin(65535, ba.size());

	QFCgiConnection *connection = qobject_cast<QFCgiConnection*>(parent());
	QFCgiRecord record = QFCgiRecord::createErrStream(this->id, ba.left(nbytes));

	ba.remove(0, nbytes);
	connection->send(record);
}

void QFCgiRequest::consumeParamsBuffer(const QByteArray &data)
{
	qint32 nread;
	QString name;
	QByteArray value;

	this->paramsBuffer.append(data);

	while ((nread = readNameValuePair(name, value)) > 0)
	{
		q2Debug("param(%s): %s", qPrintable(name), qPrintable(value));
		this->paramsBuffer.remove(0, nread);
		this->_params.insert(name, value);
	}
}

qint32 QFCgiRequest::readNameValuePair(QString &name, QByteArray &value)
{
	quint32 nameLength, valueLength;
	qint32 nnl, nvl, nn, nv;
	QByteArray name_temp;

	if ((nnl = readLengthField(0, &nameLength)) <= 0)
	{
		return nnl;
	}

	if ((nvl = readLengthField(nnl, &valueLength)) < 0)
	{
		return nvl;
	}

	if ((nn = readValueField(nnl + nvl, nameLength, name_temp)) <= 0)
	{
		return nn;
	}
	name = name_temp;

	if ((nv = readValueField(nnl + nvl + nn, valueLength, value)) < 0)
	{
		return nv;
	}

	return nnl + nvl + nn + nv;
}

qint32 QFCgiRequest::readLengthField(int pos, quint32 *length)
{
	QByteArray ba = this->paramsBuffer.mid(pos);

	if (ba.isEmpty())
	{
		return 0;
	}

	if ((ba[0] & 0x80) == 0)
	{
		*length = quint32(ba[0]);
		return 1;
	}

	if (ba.size() < 4)
	{
		return 0;
	}

	*length = quint32(((ba[0] & 0x7F) << 24) |
			  ((ba[1] & 0xFF) << 16) |
			  ((ba[2] & 0xFF) << 8) |
			  (ba[3] & 0xFF));
	return 4;
}

qint32 QFCgiRequest::readValueField(int pos, quint32 length, QByteArray &value)
{
	if(paramsBuffer.size() >= pos + qint32(length))
	{
		value = this->paramsBuffer.mid(pos, length);
		return length;
	}
	return -1;
}

qint32 QFCgiRequest::processEncodedFormData(QByteArray data, QHash<QString, QByteArray>& values)
{
	int pos;
	QByteArray name, value;
	bool val = 0;
	for(pos = 0; pos < data.size(); ++pos)
	{
		char byte = data[pos];
		if(byte == '=')
		{
			val = 1;
			continue;
		}
		if(byte == '&')
		{
			val = 0;
			values[name] = value;
			value.clear();
			continue;
		}
		if(byte == '%')
		{
			if(data.size() > pos + 2)
			{
				char b1 = data[pos + 1], b2 = data[pos + 2];
				if(b1 >= '0' && b1 <= '9') b1 -= '0';
				else if(b1 >= 'A' && b1 <= 'F') b1 -= 'A' - 10;
				else if(b1 >= 'a' && b1 <= 'f') b1 -= 'a' - 10;
				else return -1;
				if(b2 >= '0' && b2 <= '9') b2 -= '0';
				else if(b2 >= 'A' && b2 <= 'F') b2 -= 'A' - 10;
				else if(b2 >= 'a' && b2 <= 'f') b2 -= 'a' - 10;
				else return -1;
				byte = char((b1 << 8) | b2);
			}
		}
		if(val) value.push_back(byte);
		else name.push_back(byte);
	}
	values[name] = value;
	return values.size();
}

qint32 QFCgiRequest::processPost()
{
	// Acquire all data
	qint32 maxlen = _params.value("CONTENT_LENGTH").toInt();
	// TODO: check the max length
	QByteArray type = _params.value("CONTENT_TYPE");
	QByteArray raw = in->read(maxlen);
	if(type == "application/x-www-form-urlencoded")
	{
		// Text data in encoded form
		post_processed = 1;
		return processEncodedFormData(raw, _post);
	}
	else if(type.startsWith("multipart/form-data"))
	{
		// Multipart data
		// Read the fields without the Content-Type header as POST options
		// Read the fields with one as files to be uploaded
		// Note that raw may contain megabytes of data and thus should NOT be copied
		QByteArray boundary = "--" + type.right(type.indexOf("boundary=") + 9);
		// Go through all fields
		int pos = raw.indexOf(boundary, 0) + boundary.size();
		while(pos != raw.size())
		{
			// The content is supposed to start with the boundary
			if(raw[pos] == '\n')
			{
				// A field
				// This field must start with:
				// Content-Disposition: form-data
				// With optional fields "name", "filename"

				int npos = raw.indexOf("name=\"", pos) + 6;
				int nepos = raw.indexOf('"', npos);
				QByteArray name = raw.mid(npos, nepos - npos); // Get the field name; it is supposed to be right after "form-data"
				pos = nepos + 1;
				int next = raw.indexOf(boundary, pos); // Where the field ends

				if(raw[pos] == '\n')
				{
					// It's a simple pair and the header ends here
					++pos;
					_post[name] = raw.mid(pos, next - 1 - pos);
				}
				else if(raw[pos] == ';')
				{
					// It's either a file or other type of value that has extra headers; should be considered a file instead
					// TODO; The current realization doesn't really need file uploads
					;
				}
				else
				{
					// An error
					return -1;
				}

				pos = next + boundary.size();
			}
			else if(raw[pos] == '-')
			{
				// Finished
				++pos;
				break;
			}
			else
			{
				// Error
				return -1;
			}
		}
		;
		post_processed = 1;
		return _post.size();
	}
	return -1;
}

qint32 QFCgiRequest::processCookies()
{
	QByteArray raw = _params.value("HTTP_COOKIE");
	int pos;
	QByteArray name, value;
	bool val = 0;
	for(pos = 0; pos < raw.size(); ++pos)
	{
		if(raw[pos] == '=' && !val)
		{
			val = 1;
			continue;
		}
		if(raw[pos] == ';')
		{
			val = 0;
			_cookies[name] = value;
			value.clear();
			continue;
		}
		if(!isspace(raw[pos])) // omit all whitespace characters
		{
			if(val) value.push_back(raw[pos]);
			else name.push_back(raw[pos]);
		}
	}
	_cookies[name] = value;
	_cookies_orig = _cookies;
	cookies_processed = 1;
	return _cookies.size();
}
