/* poppler-document.cc: qt interface to poppler
 * Copyright (C) 2005, Albert Astals Cid <aacid@kde.org>
 * Copyright (C) 2005, Brad Hards <bradh@frogmouth.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <QtCore/QString>
#include <QtCore/QDateTime>

#include "Object.h"
#include "Stream.h"
#include "Catalog.h"

#include "poppler-qt4.h"
#include "poppler-private.h"

namespace Poppler
{

class EmbeddedFileData
{
public:
	QString m_label;
	QString m_description;
	int m_size;
	QDateTime m_modDate;
	QDateTime m_createDate;
	QByteArray m_checksum;
	Object m_streamObject;
};

EmbeddedFile::EmbeddedFile(EmbFile *embfile)
{
	m_embeddedFile = new EmbeddedFileData();
	m_embeddedFile->m_label = QString(embfile->name()->getCString());
	m_embeddedFile->m_description = UnicodeParsedString(embfile->description());
	m_embeddedFile->m_size = embfile->size();
	m_embeddedFile->m_modDate = convertDate(embfile->modDate()->getCString());
	m_embeddedFile->m_createDate = convertDate(embfile->createDate()->getCString());
	m_embeddedFile->m_checksum = QByteArray::fromRawData(embfile->checksum()->getCString(), embfile->checksum()->getLength());
	embfile->streamObject().copy(&m_embeddedFile->m_streamObject);
}

EmbeddedFile::~EmbeddedFile()
{
	delete m_embeddedFile;
}

QString EmbeddedFile::name() const
{
	return m_embeddedFile->m_label;
}

QString EmbeddedFile::description() const
{
	return m_embeddedFile->m_description;
}

int EmbeddedFile::size() const
{
	return m_embeddedFile->m_size;
}

QDateTime EmbeddedFile::modDate() const
{
	return m_embeddedFile->m_modDate;
}

QDateTime EmbeddedFile::createDate() const
{
	return m_embeddedFile->m_createDate;
}

QByteArray EmbeddedFile::checksum() const
{
	return m_embeddedFile->m_checksum;
}

QByteArray EmbeddedFile::data()
{
	Object obj;
	Stream *stream = m_embeddedFile->m_streamObject.getStream();
	stream->reset();
	int dataLen = 0;
	QByteArray fileArray;
	int i;
	while ( (i = stream->getChar()) != EOF) {
		fileArray[dataLen] = (char)i;
		++dataLen;
	}
	fileArray.resize(dataLen);
	return fileArray;
};

}
