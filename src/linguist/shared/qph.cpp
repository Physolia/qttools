// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "translator.h"

#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QTextStream>

#include <QtCore/QXmlStreamReader>

QT_BEGIN_NAMESPACE

using namespace Qt::Literals::StringLiterals;

class QPHReader : public QXmlStreamReader
{
public:
    QPHReader(QIODevice &dev)
      : QXmlStreamReader(&dev)
    {}

    // the "real thing"
    bool read(Translator &translator);

private:
    bool isWhiteSpace() const
    {
        return isCharacters() && text().toString().trimmed().isEmpty();
    }

    enum DataField { NoField, SourceField, TargetField, DefinitionField };
    DataField m_currentField;
    QString m_currentSource;
    QString m_currentTarget;
    QString m_currentDefinition;
};

bool QPHReader::read(Translator &translator)
{
    m_currentField = NoField;
    while (!atEnd()) {
        readNext();
        if (isStartElement()) {
            if (name() == "source"_L1) {
                m_currentField = SourceField;
            } else if (name() == "target"_L1) {
                m_currentField = TargetField;
            } else if (name() == "definition"_L1) {
                m_currentField = DefinitionField;
            } else {
                m_currentField = NoField;
                if (name() == "QPH"_L1) {
                    QXmlStreamAttributes atts = attributes();
                    translator.setLanguageCode(atts.value("language"_L1).toString());
                    translator.setSourceLanguageCode(atts.value("sourcelanguage"_L1).toString());
                }
            }
        } else if (isWhiteSpace()) {
            // ignore these
        } else if (isCharacters()) {
            if (m_currentField == SourceField)
                m_currentSource += text();
            else if (m_currentField == TargetField)
                m_currentTarget += text();
            else if (m_currentField == DefinitionField)
                m_currentDefinition += text();
        } else if (isEndElement() && name() == "phrase"_L1) {
            m_currentTarget.replace(Translator::TextVariantSeparator, Translator::BinaryVariantSeparator);
            TranslatorMessage msg;
            msg.setSourceText(m_currentSource);
            msg.setTranslation(m_currentTarget);
            msg.setComment(m_currentDefinition);
            translator.append(msg);
            m_currentSource.clear();
            m_currentTarget.clear();
            m_currentDefinition.clear();
        }
    }
    return true;
}

static bool loadQPH(Translator &translator, QIODevice &dev, ConversionData &)
{
    translator.setLocationsType(Translator::NoLocations);
    QPHReader reader(dev);
    return reader.read(translator);
}

static QString qphProtect(const QString &str)
{
    QString result;
    result.reserve(str.size() * 12 / 10);
    for (int i = 0; i != str.size(); ++i) {
        uint c = str.at(i).unicode();
        switch (c) {
        case '\"':
            result += "&quot;"_L1;
            break;
        case '&':
            result += "&amp;"_L1;
            break;
        case '>':
            result += "&gt;"_L1;
            break;
        case '<':
            result += "&lt;"_L1;
            break;
        case '\'':
            result += "&apos;"_L1;
            break;
        default:
            if (c < 0x20 && c != '\r' && c != '\n' && c != '\t')
                result += QString("&#%1;"_L1).arg(c);
            else // this also covers surrogates
                result += QChar(c);
        }
    }
    return result;
}

static bool saveQPH(const Translator &translator, QIODevice &dev, ConversionData &)
{
    QTextStream t(&dev);
    t << "<!DOCTYPE QPH>\n<QPH";
    QString languageCode = translator.languageCode();
    if (!languageCode.isEmpty() && languageCode != "C"_L1)
        t << " language=\"" << languageCode << "\"";
    languageCode = translator.sourceLanguageCode();
    if (!languageCode.isEmpty() && languageCode != "C"_L1)
        t << " sourcelanguage=\"" << languageCode << "\"";
    t << ">\n";
    for (const TranslatorMessage &msg : translator.messages()) {
        t << "<phrase>\n";
        t << "    <source>" << qphProtect(msg.sourceText()) << "</source>\n";
        QString str = msg.translations().join(u'@');
        str.replace(QChar(Translator::BinaryVariantSeparator),
                    QChar(Translator::TextVariantSeparator));
        t << "    <target>" << qphProtect(str)
            << "</target>\n";
        if (!msg.comment().isEmpty())
            t << "    <definition>" << qphProtect(msg.comment()) << "</definition>\n";
        t << "</phrase>\n";
    }
    t << "</QPH>\n";
    return true;
}

int initQPH()
{
    Translator::FileFormat format;

    format.extension = "qph"_L1;
    format.untranslatedDescription = QT_TRANSLATE_NOOP("FMT", "Qt Linguist 'Phrase Book'");
    format.fileType = Translator::FileFormat::TranslationSource;
    format.priority = 0;
    format.loader = &loadQPH;
    format.saver = &saveQPH;
    Translator::registerFileFormat(format);

    return 1;
}

Q_CONSTRUCTOR_FUNCTION(initQPH)

QT_END_NAMESPACE
