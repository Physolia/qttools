// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "messagemodel.h"
#include "statistics.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>

#include <QtWidgets/QMessageBox>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QTextDocument>

#include <private/qtranslator_p.h>

#include <limits.h>

static QString resolveNcr(QStringView str)
{
    constexpr QStringView notation = u"&#";
    constexpr QChar cx = QLatin1Char('x');
    constexpr QChar ce = QLatin1Char(';');

    QString result;
    result.reserve(str.size());
    qsizetype offset = str.indexOf(notation);
    while (offset >= 0) {

        qsizetype metaLen = 2;
        if (str.size() <= offset + metaLen)
            break;

        int base = 10;
        if (const QChar ch = str[offset + metaLen]; ch == cx) {
            metaLen++;
            base = 16;
        }
        offset += metaLen;

        const qsizetype end = str.sliced(offset).indexOf(ce);
        if (end > 0) {
            bool valid;
            if (const uint c = str.sliced(offset, end).toUInt(&valid, base);
                valid && c <= QChar::LastValidCodePoint) {
                if (QChar::requiresSurrogates(c))
                    result += str.sliced(0, offset - metaLen) + QChar(QChar::highSurrogate(c))
                            + QChar(QChar::lowSurrogate(c));
                else
                    result += str.sliced(0, offset - metaLen) + QChar(c);
                str.slice(offset + end + 1);
                offset = str.indexOf(notation);
                continue;
            }
        }
        result += str.sliced(0, offset);
        str.slice(offset);
        offset = str.indexOf(notation);
    }
    result += str;
    return result;
}

static QString showNcr(const QString &str)
{
    QString result;
    result.reserve(str.size());
    for (const QChar ch : str) {
        if (uint c = ch.unicode(); Q_UNLIKELY(!ch.isPrint() && c > 0x20))
            result += QString(QLatin1String("&#x%1;")).arg(c, 0, 16);
        else
            result += ch;
    }
    return result;
}

static QString adjustNcrVisibility(const QString &str, bool ncrMode)
{
    return ncrMode ? showNcr(str) : resolveNcr(str);
}

QT_BEGIN_NAMESPACE

/******************************************************************************
 *
 * MessageItem
 *
 *****************************************************************************/

MessageItem::MessageItem(const TranslatorMessage &message)
    : m_message(message), m_danger(false), m_ncrMode(false)
{
    if (m_message.translation().isEmpty())
        m_message.setTranslation(QString());
}


bool MessageItem::compare(const QString &findText, bool matchSubstring,
    Qt::CaseSensitivity cs) const
{
    return matchSubstring
        ? text().indexOf(findText, 0, cs) >= 0
        : text().compare(findText, cs) == 0;
}

void MessageItem::setTranslation(const QString &translation)
{
    m_message.setTranslation(resolveNcr(translation));
}

QString MessageItem::text() const
{
    return adjustNcrVisibility(m_message.sourceText(), m_ncrMode);
}

QString MessageItem::pluralText() const
{
    return adjustNcrVisibility(m_message.extra(QLatin1String("po-msgid_plural")), m_ncrMode);
}

QString MessageItem::translation() const
{
    return adjustNcrVisibility(m_message.translation(), m_ncrMode);
}

QStringList MessageItem::translations() const
{
    QStringList translations;
    translations.reserve(m_message.translations().size());
    for (QString &trans : m_message.translations())
        translations.append(adjustNcrVisibility(trans, m_ncrMode));
    return translations;
}

void MessageItem::setTranslations(const QStringList &translations)
{
    QStringList trans;
    trans.reserve(translations.size());
    for (const QString &t : translations)
        trans.append(resolveNcr(t));

    m_message.setTranslations(trans);
}

/******************************************************************************
 *
 * ContextItem
 *
 *****************************************************************************/

ContextItem::ContextItem(const QString &context)
  : m_context(context),
    m_finishedCount(0),
    m_finishedDangerCount(0),
    m_unfinishedDangerCount(0),
    m_nonobsoleteCount(0)
{}

void ContextItem::appendToComment(const QString &str)
{
    if (!m_comment.isEmpty())
        m_comment += QLatin1String("\n\n");
    m_comment += str;
}

MessageItem *ContextItem::messageItem(int i) const
{
    if (i >= 0 && i < msgItemList.size())
        return const_cast<MessageItem *>(&msgItemList[i]);
    Q_ASSERT(i >= 0 && i < msgItemList.size());
    return 0;
}

MessageItem *ContextItem::findMessage(const QString &sourcetext, const QString &comment) const
{
    for (int i = 0; i < messageCount(); ++i) {
        MessageItem *mi = messageItem(i);
        if (mi->text() == sourcetext && mi->comment() == comment)
            return mi;
    }
    return 0;
}

/******************************************************************************
 *
 * DataModel
 *
 *****************************************************************************/

DataModel::DataModel(QObject *parent)
  : QObject(parent),
    m_modified(false),
    m_numMessages(0),
    m_srcWords(0),
    m_srcChars(0),
    m_srcCharsSpc(0),
    m_language(QLocale::Language(-1)),
    m_sourceLanguage(QLocale::Language(-1)),
    m_territory(QLocale::Territory(-1)),
    m_sourceTerritory(QLocale::Territory(-1))
{}

QStringList DataModel::normalizedTranslations(const MessageItem &m) const
{
    QStringList translations =
            Translator::normalizedTranslations(m.message(), m_numerusForms.size());
    QStringList ncrTranslations;
    ncrTranslations.reserve(translations.size());
    for (const QString &translate : translations)
        ncrTranslations.append(adjustNcrVisibility(translate, m.ncrMode()));
    return ncrTranslations;
}

ContextItem *DataModel::contextItem(int context) const
{
    if (context >= 0 && context < m_contextList.size())
        return const_cast<ContextItem *>(&m_contextList[context]);
    Q_ASSERT(context >= 0 && context < m_contextList.size());
    return 0;
}

MessageItem *DataModel::messageItem(const DataIndex &index) const
{
    if (ContextItem *c = contextItem(index.context()))
        return c->messageItem(index.message());
    return 0;
}

ContextItem *DataModel::findContext(const QString &context) const
{
    for (int c = 0; c < m_contextList.size(); ++c) {
        ContextItem *ctx = contextItem(c);
        if (ctx->context() == context)
            return ctx;
    }
    return 0;
}

MessageItem *DataModel::findMessage(const QString &context,
    const QString &sourcetext, const QString &comment) const
{
    if (ContextItem *ctx = findContext(context))
        return ctx->findMessage(sourcetext, comment);
    return 0;
}

static int calcMergeScore(const DataModel *one, const DataModel *two)
{
    int inBoth = 0;
    for (int i = 0; i < two->contextCount(); ++i) {
        ContextItem *oc = two->contextItem(i);
        if (ContextItem *c = one->findContext(oc->context())) {
            for (int j = 0; j < oc->messageCount(); ++j) {
                MessageItem *m = oc->messageItem(j);
                if (c->findMessage(m->text(), m->comment()))
                    ++inBoth;
            }
        }
    }
    return inBoth * 100 / two->messageCount();
}

bool DataModel::isWellMergeable(const DataModel *other) const
{
    if (!other->messageCount() || !messageCount())
        return true;

    return calcMergeScore(this, other) + calcMergeScore(other, this) > 90;
}

bool DataModel::load(const QString &fileName, bool *langGuessed, QWidget *parent)
{
    Translator tor;
    ConversionData cd;
    bool ok = tor.load(fileName, cd, QLatin1String("auto"));
    if (!ok) {
        QMessageBox::warning(parent, QObject::tr("Qt Linguist"), cd.error());
        return false;
    }

    if (!tor.messageCount()) {
        QMessageBox::warning(parent, QObject::tr("Qt Linguist"),
                             tr("The translation file '%1' will not be loaded because it is empty.")
                             .arg(fileName.toHtmlEscaped()));
        return false;
    }

    const Translator::Duplicates dupes = tor.resolveDuplicates();
    if (!dupes.byId.isEmpty() || !dupes.byContents.isEmpty()) {
        QString err = tr("<qt>Duplicate messages found in '%1':").arg(fileName.toHtmlEscaped());
        int numdups = 0;
        for (auto it = dupes.byId.begin(); it != dupes.byId.end(); ++it) {
            if (++numdups >= 5) {
                err += tr("<p>[more duplicates omitted]");
                goto doWarn;
            }
            err += tr("<p>* ID: %1").arg(tor.message(it.key()).id().toHtmlEscaped());
        }
        for (auto it = dupes.byContents.begin(); it != dupes.byContents.end(); ++it) {
            const TranslatorMessage &msg = tor.message(it.key());
            if (++numdups >= 5) {
                err += tr("<p>[more duplicates omitted]");
                break;
            }
            err += tr("<p>* Context: %1<br>* Source: %2")
                    .arg(msg.context().toHtmlEscaped(), msg.sourceText().toHtmlEscaped());
            if (!msg.comment().isEmpty())
                err += tr("<br>* Comment: %3").arg(msg.comment().toHtmlEscaped());
        }
      doWarn:
        QMessageBox::warning(parent, QObject::tr("Qt Linguist"), err);
    }

    m_srcFileName = fileName;
    m_relativeLocations = (tor.locationsType() == Translator::RelativeLocations);
    m_extra = tor.extras();
    m_contextList.clear();
    m_numMessages = 0;

    QHash<QString, int> contexts;

    m_srcWords = 0;
    m_srcChars = 0;
    m_srcCharsSpc = 0;

    for (const TranslatorMessage &msg : tor.messages()) {
        if (!contexts.contains(msg.context())) {
            contexts.insert(msg.context(), m_contextList.size());
            m_contextList.append(ContextItem(msg.context()));
        }

        ContextItem *c = contextItem(contexts.value(msg.context()));
        if (msg.sourceText() == QLatin1String(ContextComment)) {
            c->appendToComment(msg.comment());
        } else {
            MessageItem tmp(msg);
            if (msg.type() == TranslatorMessage::Finished)
                c->incrementFinishedCount();
            if (msg.type() == TranslatorMessage::Finished || msg.type() == TranslatorMessage::Unfinished) {
                doCharCounting(tmp.text(), m_srcWords, m_srcChars, m_srcCharsSpc);
                doCharCounting(tmp.pluralText(), m_srcWords, m_srcChars, m_srcCharsSpc);
                c->incrementNonobsoleteCount();
            }
            c->appendMessage(tmp);
            ++m_numMessages;
        }
    }

    // Try to detect the correct language in the following order
    // 1. Look for the language attribute in the ts
    //   if that fails
    // 2. Guestimate the language from the filename
    //   (expecting the qt_{en,de}.ts convention)
    //   if that fails
    // 3. Retrieve the locale from the system.
    *langGuessed = false;
    QString lang = tor.languageCode();
    if (lang.isEmpty()) {
        lang = QFileInfo(fileName).baseName();
        int pos = lang.indexOf(QLatin1Char('_'));
        if (pos != -1)
            lang.remove(0, pos + 1);
        else
            lang.clear();
        *langGuessed = true;
    }
    QLocale::Language l;
    QLocale::Territory c;
    Translator::languageAndTerritory(lang, &l, &c);
    if (l == QLocale::C) {
        QLocale sys;
        l = sys.language();
        c = sys.territory();
        *langGuessed = true;
    }
    if (!setLanguageAndTerritory(l, c))
        QMessageBox::warning(parent, QObject::tr("Qt Linguist"),
                             tr("Linguist does not know the plural rules for '%1'.\n"
                                "Will assume a single universal form.")
                             .arg(m_localizedLanguage));
    // Try to detect the correct source language in the following order
    // 1. Look for the language attribute in the ts
    //   if that fails
    // 2. Assume English
    lang = tor.sourceLanguageCode();
    if (lang.isEmpty()) {
        l = QLocale::C;
        c = QLocale::AnyTerritory;
    } else {
        Translator::languageAndTerritory(lang, &l, &c);
    }
    setSourceLanguageAndTerritory(l, c);

    setModified(false);

    return true;
}

bool DataModel::save(const QString &fileName, QWidget *parent)
{
    Translator tor;
    for (DataModelIterator it(this); it.isValid(); ++it)
        tor.append(it.current()->message());

    tor.setLanguageCode(Translator::makeLanguageCode(m_language, m_territory));
    tor.setSourceLanguageCode(Translator::makeLanguageCode(m_sourceLanguage, m_sourceTerritory));
    tor.setLocationsType(m_relativeLocations ? Translator::RelativeLocations
                                             : Translator::AbsoluteLocations);
    tor.setExtras(m_extra);
    ConversionData cd;
    tor.normalizeTranslations(cd);
    bool ok = tor.save(fileName, cd, QLatin1String("auto"));
    if (ok)
        setModified(false);
    if (!cd.error().isEmpty())
        QMessageBox::warning(parent, QObject::tr("Qt Linguist"), cd.error());
    return ok;
}

bool DataModel::saveAs(const QString &newFileName, QWidget *parent)
{
    if (!save(newFileName, parent))
        return false;
    m_srcFileName = newFileName;
    return true;
}

bool DataModel::release(const QString &fileName, bool verbose, bool ignoreUnfinished,
    TranslatorSaveMode mode, QWidget *parent)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(parent, QObject::tr("Qt Linguist"),
            tr("Cannot create '%2': %1").arg(file.errorString()).arg(fileName));
        return false;
    }
    Translator tor;
    QLocale locale(m_language, m_territory);
    tor.setLanguageCode(locale.name());
    for (DataModelIterator it(this); it.isValid(); ++it)
        tor.append(it.current()->message());
    ConversionData cd;
    cd.m_verbose = verbose;
    cd.m_ignoreUnfinished = ignoreUnfinished;
    cd.m_saveMode = mode;
    cd.m_idBased =
            std::all_of(tor.messages().begin(), tor.messages().end(),
                        [](const TranslatorMessage &message) { return !message.id().isEmpty(); });
    bool ok = saveQM(tor, file, cd);
    if (!ok)
        QMessageBox::warning(parent, QObject::tr("Qt Linguist"), cd.error());
    return ok;
}

void DataModel::doCharCounting(const QString &text, int &trW, int &trC, int &trCS)
{
    trCS += text.size();
    bool inWord = false;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i].isLetterOrNumber() || text[i] == QLatin1Char('_')) {
            if (!inWord) {
                ++trW;
                inWord = true;
            }
        } else {
            inWord = false;
        }
        if (!text[i].isSpace())
            trC++;
    }
}

bool DataModel::setLanguageAndTerritory(QLocale::Language lang, QLocale::Territory territory)
{
    if (m_language == lang && m_territory == territory)
        return true;
    m_language = lang;
    m_territory = territory;

    if (lang == QLocale::C || uint(lang) > uint(QLocale::LastLanguage)) // XXX does this make any sense?
        lang = QLocale::English;
    QByteArray rules;
    bool ok = getNumerusInfo(lang, territory, &rules, &m_numerusForms, 0);
    QLocale loc(lang, territory);
    // Add territory name if we couldn't match the (lang, territory) combination,
    // or if the language is used in more than one territory.
    const bool mentionTerritory = (loc.territory() != territory) || [lang, territory]() {
        const auto locales = QLocale::matchingLocales(lang, QLocale::AnyScript,
                                                      QLocale::AnyTerritory);
        return std::any_of(locales.cbegin(), locales.cend(), [territory](const QLocale &locale) {
            return locale.territory() != territory;
        });
    }();
    m_localizedLanguage = mentionTerritory
            //: <language> (<territory>)
            ? tr("%1 (%2)").arg(loc.nativeLanguageName(), loc.nativeTerritoryName())
            : loc.nativeLanguageName();
    m_countRefNeeds.clear();
    for (int i = 0; i < rules.size(); ++i) {
        m_countRefNeeds.append(!(rules.at(i) == Q_EQ && (i == (rules.size() - 2) || rules.at(i + 2) == (char)Q_NEWRULE)));
        while (++i < rules.size() && rules.at(i) != (char)Q_NEWRULE) {}
    }
    m_countRefNeeds.append(true);
    if (!ok) {
        m_numerusForms.clear();
        m_numerusForms << tr("Universal Form");
    }
    emit languageChanged();
    setModified(true);
    return ok;
}

void DataModel::setSourceLanguageAndTerritory(QLocale::Language lang, QLocale::Territory territory)
{
    if (m_sourceLanguage == lang && m_sourceTerritory == territory)
        return;
    m_sourceLanguage = lang;
    m_sourceTerritory = territory;
    setModified(true);
}

void DataModel::updateStatistics()
{
    StatisticalData stats {};
    for (DataModelIterator it(this); it.isValid(); ++it) {
        const MessageItem *mi = it.current();
        if (mi->isObsolete()) {
            stats.obsoleteMsg++;
        } else if (mi->isFinished()) {
            bool hasDanger = false;
            for (const QString &trnsl : mi->translations()) {
                doCharCounting(trnsl, stats.wordsFinished, stats.charsFinished, stats.charsSpacesFinished);
                hasDanger |= mi->danger();
            }
            if (hasDanger)
                stats.translatedMsgDanger++;
            else
                stats.translatedMsgNoDanger++;
        } else if (mi->isUnfinished()) {
            bool hasDanger = false;
            for (const QString &trnsl : mi->translations()) {
                doCharCounting(trnsl, stats.wordsUnfinished, stats.charsUnfinished, stats.charsSpacesUnfinished);
                hasDanger |= mi->danger();
            }
            if (hasDanger)
                stats.unfinishedMsgDanger++;
            else
                stats.unfinishedMsgNoDanger++;
        }
    }
    stats.wordsSource = m_srcWords;
    stats.charsSource = m_srcChars;
    stats.charsSpacesSource = m_srcCharsSpc;
    emit statsChanged(stats);
}

void DataModel::setModified(bool isModified)
{
    if (m_modified == isModified)
        return;
    m_modified = isModified;
    emit modifiedChanged();
}

QString DataModel::prettifyPlainFileName(const QString &fn)
{
    static QString workdir = QDir::currentPath() + QLatin1Char('/');

    return QDir::toNativeSeparators(fn.startsWith(workdir) ? fn.mid(workdir.size()) : fn);
}

QString DataModel::prettifyFileName(const QString &fn)
{
    if (fn.startsWith(QLatin1Char('=')))
        return QLatin1Char('=') + prettifyPlainFileName(fn.mid(1));
    else
        return prettifyPlainFileName(fn);
}

/******************************************************************************
 *
 * DataModelIterator
 *
 *****************************************************************************/

DataModelIterator::DataModelIterator(DataModel *model, int context, int message)
  : DataIndex(context, message), m_model(model)
{
}

bool DataModelIterator::isValid() const
{
    return m_context < m_model->m_contextList.size();
}

void DataModelIterator::operator++()
{
    ++m_message;
    if (m_message >= m_model->m_contextList.at(m_context).messageCount()) {
        ++m_context;
        m_message = 0;
    }
}

MessageItem *DataModelIterator::current() const
{
    return m_model->messageItem(*this);
}


/******************************************************************************
 *
 * MultiMessageItem
 *
 *****************************************************************************/

MultiMessageItem::MultiMessageItem(const MessageItem *m)
    : m_id(m->id()),
      m_text(m->text()),
      m_pluralText(m->pluralText()),
      m_comment(m->comment()),
      m_nonnullCount(0),
      m_nonobsoleteCount(0),
      m_editableCount(0),
      m_unfinishedCount(0)
{
}

/******************************************************************************
 *
 * MultiContextItem
 *
 *****************************************************************************/

MultiContextItem::MultiContextItem(int oldCount, ContextItem *ctx, bool writable)
    : m_context(ctx->context()),
      m_comment(ctx->comment()),
      m_finishedCount(0),
      m_editableCount(0),
      m_nonobsoleteCount(0)
{
    QList<MessageItem *> mList;
    QList<MessageItem *> eList;
    for (int j = 0; j < ctx->messageCount(); ++j) {
        MessageItem *m = ctx->messageItem(j);
        mList.append(m);
        eList.append(0);
        m_multiMessageList.append(MultiMessageItem(m));
    }
    for (int i = 0; i < oldCount; ++i) {
        m_messageLists.append(eList);
        m_writableMessageLists.append(0);
        m_contextList.append(0);
    }
    m_messageLists.append(mList);
    m_writableMessageLists.append(writable ? &m_messageLists.last() : 0);
    m_contextList.append(ctx);
}

void MultiContextItem::appendEmptyModel()
{
    QList<MessageItem *> eList;
    for (int j = 0; j < messageCount(); ++j)
        eList.append(0);
    m_messageLists.append(eList);
    m_writableMessageLists.append(0);
    m_contextList.append(0);
}

void MultiContextItem::assignLastModel(ContextItem *ctx, bool writable)
{
    if (writable)
        m_writableMessageLists.last() = &m_messageLists.last();
    m_contextList.last() = ctx;
}

// XXX this is not needed, yet
void MultiContextItem::moveModel(int oldPos, int newPos)
{
    m_contextList.insert(newPos, m_contextList[oldPos]);
    m_messageLists.insert(newPos, m_messageLists[oldPos]);
    m_writableMessageLists.insert(newPos, m_writableMessageLists[oldPos]);
    removeModel(oldPos < newPos ? oldPos : oldPos + 1);
}

void MultiContextItem::removeModel(int pos)
{
    m_contextList.removeAt(pos);
    m_messageLists.removeAt(pos);
    m_writableMessageLists.removeAt(pos);
}

void MultiContextItem::putMessageItem(int pos, MessageItem *m)
{
    m_messageLists.last()[pos] = m;
}

void MultiContextItem::appendMessageItems(const QList<MessageItem *> &m)
{
    QList<MessageItem *> nullItems = m; // Basically, just a reservation
    for (int i = 0; i < nullItems.size(); ++i)
        nullItems[i] = 0;
    for (int i = 0; i < m_messageLists.size() - 1; ++i)
        m_messageLists[i] += nullItems;
    m_messageLists.last() += m;
    for (MessageItem *mi : m)
        m_multiMessageList.append(MultiMessageItem(mi));
}

void MultiContextItem::removeMultiMessageItem(int pos)
{
    for (int i = 0; i < m_messageLists.size(); ++i)
        m_messageLists[i].removeAt(pos);
    m_multiMessageList.removeAt(pos);
}

int MultiContextItem::firstNonobsoleteMessageIndex(int msgIdx) const
{
    for (int i = 0; i < m_messageLists.size(); ++i)
        if (m_messageLists[i][msgIdx] && !m_messageLists[i][msgIdx]->isObsolete())
            return i;
    return -1;
}

int MultiContextItem::findMessage(const QString &sourcetext, const QString &comment) const
{
    for (int i = 0, cnt = messageCount(); i < cnt; ++i) {
        MultiMessageItem *m = multiMessageItem(i);
        if (m->text() == sourcetext && m->comment() == comment)
            return i;
    }
    return -1;
}

int MultiContextItem::findMessageById(const QString &id) const
{
    for (int i = 0, cnt = messageCount(); i < cnt; ++i) {
        MultiMessageItem *m = multiMessageItem(i);
        if (m->id() == id)
            return i;
    }
    return -1;
}

/******************************************************************************
 *
 * MultiDataModel
 *
 *****************************************************************************/

static const uchar paletteRGBs[7][3] = {
    { 236, 244, 255 }, // blue
    { 236, 255, 255 }, // cyan
    { 236, 255, 232 }, // green
    { 255, 255, 230 }, // yellow
    { 255, 242, 222 }, // orange
    { 255, 236, 236 }, // red
    { 252, 236, 255 }  // purple
};

MultiDataModel::MultiDataModel(QObject *parent) :
    QObject(parent),
    m_numFinished(0),
    m_numEditable(0),
    m_numMessages(0),
    m_modified(false)
{
    for (int i = 0; i < 7; ++i)
        m_colors[i] = QColor(paletteRGBs[i][0], paletteRGBs[i][1], paletteRGBs[i][2]);

    m_bitmap = QBitmap(8, 8);
    m_bitmap.clear();
    QPainter p(&m_bitmap);
    for (int j = 0; j < 8; ++j)
        for (int k = 0; k < 8; ++k)
            if ((j + k) & 4)
                p.drawPoint(j, k);
}

MultiDataModel::~MultiDataModel()
{
    qDeleteAll(m_dataModels);
}

QBrush MultiDataModel::brushForModel(int model) const
{
    QBrush brush(m_colors[model % 7]);
    if (!isModelWritable(model))
        brush.setTexture(m_bitmap);
    return brush;
}

bool MultiDataModel::isWellMergeable(const DataModel *dm) const
{
    if (!dm->messageCount() || !messageCount())
        return true;

    int inBothNew = 0;
    for (int i = 0; i < dm->contextCount(); ++i) {
        ContextItem *c = dm->contextItem(i);
        if (MultiContextItem *mc = findContext(c->context())) {
            for (int j = 0; j < c->messageCount(); ++j) {
                MessageItem *m = c->messageItem(j);
                if (mc->findMessage(m->text(), m->comment()) >= 0)
                    ++inBothNew;
            }
        }
    }
    int newRatio = inBothNew * 100 / dm->messageCount();

    int inBothOld = 0;
    for (int k = 0; k < contextCount(); ++k) {
        MultiContextItem *mc = multiContextItem(k);
        if (ContextItem *c = dm->findContext(mc->context())) {
            for (int j = 0; j < mc->messageCount(); ++j) {
                MultiMessageItem *m = mc->multiMessageItem(j);
                if (c->findMessage(m->text(), m->comment()))
                    ++inBothOld;
            }
        }
    }
    int oldRatio = inBothOld * 100 / messageCount();

    return newRatio + oldRatio > 90;
}

void MultiDataModel::append(DataModel *dm, bool readWrite)
{
    int insCol = modelCount() + 1;
    m_msgModel->beginInsertColumns(QModelIndex(), insCol, insCol);
    m_dataModels.append(dm);
    for (int j = 0; j < contextCount(); ++j) {
        m_msgModel->beginInsertColumns(m_msgModel->createIndex(j, 0), insCol, insCol);
        m_multiContextList[j].appendEmptyModel();
        m_msgModel->endInsertColumns();
    }
    m_msgModel->endInsertColumns();
    int appendedContexts = 0;
    for (int i = 0; i < dm->contextCount(); ++i) {
        ContextItem *c = dm->contextItem(i);
        int mcx = findContextIndex(c->context());
        if (mcx >= 0) {
            MultiContextItem *mc = multiContextItem(mcx);
            mc->assignLastModel(c, readWrite);
            QList<MessageItem *> appendItems;
            for (int j = 0; j < c->messageCount(); ++j) {
                MessageItem *m = c->messageItem(j);

                int msgIdx = -1;
                if (!m->id().isEmpty()) // id based translation
                    msgIdx = mc->findMessageById(m->id());

                if (msgIdx == -1)
                    msgIdx = mc->findMessage(m->text(), m->comment());

                if (msgIdx >= 0)
                    mc->putMessageItem(msgIdx, m);
                else
                    appendItems << m;
            }
            if (!appendItems.isEmpty()) {
                int msgCnt = mc->messageCount();
                m_msgModel->beginInsertRows(m_msgModel->createIndex(mcx, 0),
                                            msgCnt, msgCnt + appendItems.size() - 1);
                mc->appendMessageItems(appendItems);
                m_msgModel->endInsertRows();
                m_numMessages += appendItems.size();
            }
        } else {
            m_multiContextList << MultiContextItem(modelCount() - 1, c, readWrite);
            m_numMessages += c->messageCount();
            ++appendedContexts;
        }
    }
    if (appendedContexts) {
        // Do that en block to avoid itemview inefficiency. It doesn't hurt that we
        // announce the availability of the data "long" after it was actually added.
        m_msgModel->beginInsertRows(QModelIndex(),
                                    contextCount() - appendedContexts, contextCount() - 1);
        m_msgModel->endInsertRows();
    }
    dm->setWritable(readWrite);
    updateCountsOnAdd(modelCount() - 1, readWrite);
    connect(dm, &DataModel::modifiedChanged,
            this, &MultiDataModel::onModifiedChanged);
    connect(dm, &DataModel::languageChanged,
            this, &MultiDataModel::onLanguageChanged);
    connect(dm, &DataModel::statsChanged,
            this, &MultiDataModel::statsChanged);
    emit modelAppended();
}

void MultiDataModel::close(int model)
{
    if (m_dataModels.size() == 1) {
        closeAll();
    } else {
        updateCountsOnRemove(model, isModelWritable(model));
        int delCol = model + 1;
        m_msgModel->beginRemoveColumns(QModelIndex(), delCol, delCol);
        for (int i = m_multiContextList.size(); --i >= 0;) {
            m_msgModel->beginRemoveColumns(m_msgModel->createIndex(i, 0), delCol, delCol);
            m_multiContextList[i].removeModel(model);
            m_msgModel->endRemoveColumns();
        }
        delete m_dataModels.takeAt(model);
        m_msgModel->endRemoveColumns();
        emit modelDeleted(model);
        for (int i = m_multiContextList.size(); --i >= 0;) {
            MultiContextItem &mc = m_multiContextList[i];
            QModelIndex contextIdx = m_msgModel->createIndex(i, 0);
            for (int j = mc.messageCount(); --j >= 0;)
                if (mc.multiMessageItem(j)->isEmpty()) {
                    m_msgModel->beginRemoveRows(contextIdx, j, j);
                    mc.removeMultiMessageItem(j);
                    m_msgModel->endRemoveRows();
                    --m_numMessages;
                }
            if (!mc.messageCount()) {
                m_msgModel->beginRemoveRows(QModelIndex(), i, i);
                m_multiContextList.removeAt(i);
                m_msgModel->endRemoveRows();
            }
        }
        onModifiedChanged();
    }
}

void MultiDataModel::closeAll()
{
    m_msgModel->beginResetModel();
    m_numFinished = 0;
    m_numEditable = 0;
    m_numMessages = 0;
    qDeleteAll(m_dataModels);
    m_dataModels.clear();
    m_multiContextList.clear();
    m_msgModel->endResetModel();
    emit allModelsDeleted();
    onModifiedChanged();
}

// XXX this is not needed, yet
void MultiDataModel::moveModel(int oldPos, int newPos)
{
    int delPos = oldPos < newPos ? oldPos : oldPos + 1;
    m_dataModels.insert(newPos, m_dataModels[oldPos]);
    m_dataModels.removeAt(delPos);
    for (int i = 0; i < m_multiContextList.size(); ++i)
        m_multiContextList[i].moveModel(oldPos, newPos);
}

QStringList MultiDataModel::prettifyFileNames(const QStringList &names)
{
    QStringList out;

    for (const QString &name : names)
        out << DataModel::prettifyFileName(name);
    return out;
}

QString MultiDataModel::condenseFileNames(const QStringList &names)
{
    if (names.isEmpty())
        return QString();

    if (names.size() < 2)
        return names.first();

    QString prefix = names.first();
    if (prefix.startsWith(QLatin1Char('=')))
        prefix.remove(0, 1);
    QString suffix = prefix;
    for (int i = 1; i < names.size(); ++i) {
        QString fn = names[i];
        if (fn.startsWith(QLatin1Char('=')))
            fn.remove(0, 1);
        for (int j = 0; j < prefix.size(); ++j)
            if (fn[j] != prefix[j]) {
                if (j < prefix.size()) {
                    while (j > 0 && prefix[j - 1].isLetterOrNumber())
                        --j;
                    prefix.truncate(j);
                }
                break;
            }
        int fnl = fn.size() - 1;
        int sxl = suffix.size() - 1;
        for (int k = 0; k <= sxl; ++k)
            if (fn[fnl - k] != suffix[sxl - k]) {
                if (k < sxl) {
                    while (k > 0 && suffix[sxl - k + 1].isLetterOrNumber())
                        --k;
                    if (prefix.size() + k > fnl)
                        --k;
                    suffix.remove(0, sxl - k + 1);
                }
                break;
            }
    }
    QString ret = prefix + QLatin1Char('{');
    int pxl = prefix.size();
    int sxl = suffix.size();
    for (int j = 0; j < names.size(); ++j) {
        if (j)
            ret += QLatin1Char(',');
        int off = pxl;
        QString fn = names[j];
        if (fn.startsWith(QLatin1Char('='))) {
            ret += QLatin1Char('=');
            ++off;
        }
        ret += fn.mid(off, fn.size() - sxl - off);
    }
    ret += QLatin1Char('}') + suffix;
    return ret;
}

QStringList MultiDataModel::srcFileNames(bool pretty) const
{
    QStringList names;
    for (DataModel *dm : m_dataModels)
        names << (dm->isWritable() ? QString() : QString::fromLatin1("=")) + dm->srcFileName(pretty);
    return names;
}

QString MultiDataModel::condensedSrcFileNames(bool pretty) const
{
    return condenseFileNames(srcFileNames(pretty));
}

bool MultiDataModel::isModified() const
{
    for (const DataModel *mdl : m_dataModels)
        if (mdl->isModified())
            return true;
    return false;
}

void MultiDataModel::onModifiedChanged()
{
    bool modified = isModified();
    if (modified != m_modified) {
        emit modifiedChanged(modified);
        m_modified = modified;
    }
}

void MultiDataModel::onLanguageChanged()
{
    int i = 0;
    while (sender() != m_dataModels[i])
        ++i;
    emit languageChanged(i);
}

int MultiDataModel::isFileLoaded(const QString &name) const
{
    for (int i = 0; i < m_dataModels.size(); ++i)
        if (m_dataModels[i]->srcFileName() == name)
            return i;
    return -1;
}

int MultiDataModel::findContextIndex(const QString &context) const
{
    for (int i = 0; i < m_multiContextList.size(); ++i) {
        const MultiContextItem &mc = m_multiContextList[i];
        if (mc.context() == context)
            return i;
    }
    return -1;
}

MultiContextItem *MultiDataModel::findContext(const QString &context) const
{
    for (int i = 0; i < m_multiContextList.size(); ++i) {
        const MultiContextItem &mc = m_multiContextList[i];
        if (mc.context() == context)
            return const_cast<MultiContextItem *>(&mc);
    }
    return 0;
}

MessageItem *MultiDataModel::messageItem(const MultiDataIndex &index, int model) const
{
    if (index.context() < contextCount() && index.context() >= 0 && model >= 0
        && model < modelCount()) {
        MultiContextItem *mc = multiContextItem(index.context());
        if (index.message() < mc->messageCount())
            return mc->messageItem(model, index.message());
    }
    Q_ASSERT(model >= 0 && model < modelCount());
    Q_ASSERT(index.context() < contextCount());
    return 0;
}

void MultiDataModel::setTranslation(const MultiDataIndex &index, const QString &translation)
{
    MessageItem *m = messageItem(index);
    if (translation == m->translation())
        return;
    m->setTranslation(translation);
    setModified(index.model(), true);
    emit translationChanged(index);
}

void MultiDataModel::setFinished(const MultiDataIndex &index, bool finished)
{
    MultiContextItem *mc = multiContextItem(index.context());
    MultiMessageItem *mm = mc->multiMessageItem(index.message());
    ContextItem *c = contextItem(index);
    MessageItem *m = messageItem(index);
    TranslatorMessage::Type type = m->type();
    if (type == TranslatorMessage::Unfinished && finished) {
        m->setType(TranslatorMessage::Finished);
        mm->decrementUnfinishedCount();
        if (!mm->countUnfinished()) {
            incrementFinishedCount();
            mc->incrementFinishedCount();
            emit multiContextDataChanged(index);
        }
        c->incrementFinishedCount();
        if (m->danger()) {
            c->incrementFinishedDangerCount();
            c->decrementUnfinishedDangerCount();
            if (!c->unfinishedDangerCount()
                || c->finishedCount() == c->nonobsoleteCount())
                emit contextDataChanged(index);
        } else if (c->finishedCount() == c->nonobsoleteCount()) {
            emit contextDataChanged(index);
        }
        emit messageDataChanged(index);
        setModified(index.model(), true);
    } else if (type == TranslatorMessage::Finished && !finished) {
        m->setType(TranslatorMessage::Unfinished);
        mm->incrementUnfinishedCount();
        if (mm->countUnfinished() == 1) {
            decrementFinishedCount();
            mc->decrementFinishedCount();
            emit multiContextDataChanged(index);
        }
        c->decrementFinishedCount();
        if (m->danger()) {
            c->decrementFinishedDangerCount();
            c->incrementUnfinishedDangerCount();
            if (c->unfinishedDangerCount() == 1
                || c->finishedCount() + 1 == c->nonobsoleteCount())
                emit contextDataChanged(index);
        } else if (c->finishedCount() + 1 == c->nonobsoleteCount()) {
            emit contextDataChanged(index);
        }
        emit messageDataChanged(index);
        setModified(index.model(), true);
    }
}

void MultiDataModel::setDanger(const MultiDataIndex &index, bool danger)
{
    ContextItem *c = contextItem(index);
    MessageItem *m = messageItem(index);
    if (!m->danger() && danger) {
        if (m->isFinished()) {
            c->incrementFinishedDangerCount();
            if (c->finishedDangerCount() == 1)
                emit contextDataChanged(index);
        } else {
            c->incrementUnfinishedDangerCount();
            if (c->unfinishedDangerCount() == 1)
                emit contextDataChanged(index);
        }
        emit messageDataChanged(index);
        m->setDanger(danger);
    } else if (m->danger() && !danger) {
        if (m->isFinished()) {
            c->decrementFinishedDangerCount();
            if (!c->finishedDangerCount())
                emit contextDataChanged(index);
        } else {
            c->decrementUnfinishedDangerCount();
            if (!c->unfinishedDangerCount())
                emit contextDataChanged(index);
        }
        emit messageDataChanged(index);
        m->setDanger(danger);
    }
}

void MultiDataModel::updateCountsOnAdd(int model, bool writable)
{
    for (int i = 0; i < m_multiContextList.size(); ++i) {
        MultiContextItem &mc = m_multiContextList[i];
        for (int j = 0; j < mc.messageCount(); ++j)
            if (MessageItem *m = mc.messageItem(model, j)) {
                MultiMessageItem *mm = mc.multiMessageItem(j);
                mm->incrementNonnullCount();
                if (!m->isObsolete()) {
                    if (writable) {
                        if (!mm->countEditable()) {
                            mc.incrementEditableCount();
                            incrementEditableCount();
                            if (m->isFinished()) {
                                mc.incrementFinishedCount();
                                incrementFinishedCount();
                            } else {
                                mm->incrementUnfinishedCount();
                            }
                        } else if (!m->isFinished()) {
                            if (!mm->isUnfinished()) {
                                mc.decrementFinishedCount();
                                decrementFinishedCount();
                            }
                            mm->incrementUnfinishedCount();
                        }
                        mm->incrementEditableCount();
                    }
                    mc.incrementNonobsoleteCount();
                    mm->incrementNonobsoleteCount();
                }
            }
    }
}

void MultiDataModel::updateCountsOnRemove(int model, bool writable)
{
    for (int i = 0; i < m_multiContextList.size(); ++i) {
        MultiContextItem &mc = m_multiContextList[i];
        for (int j = 0; j < mc.messageCount(); ++j)
            if (MessageItem *m = mc.messageItem(model, j)) {
                MultiMessageItem *mm = mc.multiMessageItem(j);
                mm->decrementNonnullCount();
                if (!m->isObsolete()) {
                    mm->decrementNonobsoleteCount();
                    mc.decrementNonobsoleteCount();
                    if (writable) {
                        mm->decrementEditableCount();
                        if (!mm->countEditable()) {
                            mc.decrementEditableCount();
                            decrementEditableCount();
                            if (m->isFinished()) {
                                mc.decrementFinishedCount();
                                decrementFinishedCount();
                            } else {
                                mm->decrementUnfinishedCount();
                            }
                        } else if (!m->isFinished()) {
                            mm->decrementUnfinishedCount();
                            if (!mm->isUnfinished()) {
                                mc.incrementFinishedCount();
                                incrementFinishedCount();
                            }
                        }
                    }
                }
            }
    }
}

/******************************************************************************
 *
 * MultiDataModelIterator
 *
 *****************************************************************************/

MultiDataModelIterator::MultiDataModelIterator(MultiDataModel *dataModel, int model, int context, int message)
  : MultiDataIndex(model, context, message), m_dataModel(dataModel)
{
}

void MultiDataModelIterator::operator++()
{
    Q_ASSERT(isValid());
    ++m_message;
    if (m_message >= m_dataModel->m_multiContextList.at(m_context).messageCount()) {
        ++m_context;
        m_message = 0;
    }
}

bool MultiDataModelIterator::isValid() const
{
    return m_context < m_dataModel->m_multiContextList.size();
}

MessageItem *MultiDataModelIterator::current() const
{
    return m_dataModel->messageItem(*this);
}


/******************************************************************************
 *
 * MessageModel
 *
 *****************************************************************************/

MessageModel::MessageModel(QObject *parent, MultiDataModel *data)
  : QAbstractItemModel(parent), m_data(data)
{
    data->m_msgModel = this;
    connect(m_data, &MultiDataModel::multiContextDataChanged,
            this, &MessageModel::multiContextItemChanged);
    connect(m_data, &MultiDataModel::contextDataChanged,
            this, &MessageModel::contextItemChanged);
    connect(m_data, &MultiDataModel::messageDataChanged,
            this, &MessageModel::messageItemChanged);
}

QModelIndex MessageModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid())
        return createIndex(row, column);
    if (!parent.internalId())
        return createIndex(row, column, parent.row() + 1);
    return QModelIndex();
}

QModelIndex MessageModel::parent(const QModelIndex& index) const
{
    if (index.internalId())
        return createIndex(index.internalId() - 1, 0);
    return QModelIndex();
}

void MessageModel::multiContextItemChanged(const MultiDataIndex &index)
{
    QModelIndex idx = createIndex(index.context(), m_data->modelCount() + 2);
    emit dataChanged(idx, idx);
}

void MessageModel::contextItemChanged(const MultiDataIndex &index)
{
    QModelIndex idx = createIndex(index.context(), index.model() + 1);
    emit dataChanged(idx, idx);
}

void MessageModel::messageItemChanged(const MultiDataIndex &index)
{
    QModelIndex idx = createIndex(index.message(), index.model() + 1, index.context() + 1);
    emit dataChanged(idx, idx);
}

QModelIndex MessageModel::modelIndex(const MultiDataIndex &index)
{
    if (index.message() < 0) // Should be unused case
        return createIndex(index.context(), index.model() + 1);
    return createIndex(index.message(), index.model() + 1, index.context() + 1);
}

int MessageModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_data->contextCount(); // contexts
    if (!parent.internalId()) // messages
        return m_data->multiContextItem(parent.row())->messageCount();
    return 0;
}

int MessageModel::columnCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_data->modelCount() + 3;
    return m_data->modelCount() + 2;
}

QVariant MessageModel::data(const QModelIndex &index, int role) const
{
    static QVariant pxOn  =
        QVariant::fromValue(QPixmap(QLatin1String(":/images/s_check_on.png")));
    static QVariant pxOff =
        QVariant::fromValue(QPixmap(QLatin1String(":/images/s_check_off.png")));
    static QVariant pxObsolete =
        QVariant::fromValue(QPixmap(QLatin1String(":/images/s_check_obsolete.png")));
    static QVariant pxDanger =
        QVariant::fromValue(QPixmap(QLatin1String(":/images/s_check_danger.png")));
    static QVariant pxWarning =
        QVariant::fromValue(QPixmap(QLatin1String(":/images/s_check_warning.png")));
    static QVariant pxEmpty =
        QVariant::fromValue(QPixmap(QLatin1String(":/images/s_check_empty.png")));

    int row = index.row();
    int column = index.column() - 1;
    if (column < 0)
        return QVariant();

    int numLangs = m_data->modelCount();

    if (role == Qt::ToolTipRole && column < numLangs) {
        return tr("Completion status for %1").arg(m_data->model(column)->localizedLanguage());
    } else if (index.internalId()) {
        // this is a message
        int crow = index.internalId() - 1;
        MultiContextItem *mci = m_data->multiContextItem(crow);
        if (row >= mci->messageCount() || !index.isValid())
            return QVariant();

        if (role == Qt::DisplayRole || (role == Qt::ToolTipRole && column == numLangs)) {
            switch (column - numLangs) {
            case 0: // Source text
                {
                    MultiMessageItem *msgItem = mci->multiMessageItem(row);

                    auto text = msgItem->text();
                    if (text.isEmpty())
                        text = msgItem->id();

                    if (text.isEmpty()) {
                        if (mci->context().isEmpty())
                            return tr("<file header>");
                        else
                            return tr("<context comment>");
                    }
                    return text.simplified();
                }
            default: // Status or dummy column => no text
                return QVariant();
            }
        }
        else if (role == Qt::DecorationRole && column < numLangs) {
            if (MessageItem *msgItem = mci->messageItem(column, row)) {
                switch (msgItem->message().type()) {
                case TranslatorMessage::Unfinished:
                    if (msgItem->translation().isEmpty())
                        return pxEmpty;
                    if (msgItem->danger())
                        return pxDanger;
                    return pxOff;
                case TranslatorMessage::Finished:
                    if (msgItem->danger())
                        return pxWarning;
                    return pxOn;
                default:
                    return pxObsolete;
                }
            }
            return QVariant();
        }
        else if (role == SortRole) {
            switch (column - numLangs) {
            case 0: // Source text
                return mci->multiMessageItem(row)->text().simplified().remove(QLatin1Char('&'));
            case 1: // Dummy column
                return QVariant();
            default:
                if (MessageItem *msgItem = mci->messageItem(column, row)) {
                    int rslt = !msgItem->translation().isEmpty();
                    if (!msgItem->danger())
                        rslt |= 2;
                    if (msgItem->isObsolete())
                        rslt |= 8;
                    else if (msgItem->isFinished())
                        rslt |= 4;
                    return rslt;
                }
                return INT_MAX;
            }
        }
        else if (role == Qt::ForegroundRole && column > 0
                 && mci->multiMessageItem(row)->isObsolete()) {
            return QBrush(Qt::darkGray);
        }
        else if (role == Qt::ForegroundRole && column == numLangs
                 && mci->multiMessageItem(row)->text().isEmpty()) {
            return QBrush(QColor(0, 0xa0, 0xa0));
        }
        else if (role == Qt::BackgroundRole) {
            if (column < numLangs && numLangs != 1)
                return m_data->brushForModel(column);
        }
    } else {
        // this is a context
        if (row >= m_data->contextCount() || !index.isValid())
            return QVariant();

        MultiContextItem *mci = m_data->multiContextItem(row);

        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            switch (column - numLangs) {
            case 0: // Context
                {
                    if (mci->context().isEmpty())
                        return tr("<unnamed context>");
                    return mci->context().simplified();
                }
            case 1:
                {
                    if (role == Qt::ToolTipRole) {
                        return tr("%n unfinished message(s) left.", 0,
                                  mci->getNumEditable() - mci->getNumFinished());
                    }
                    return QString::asprintf("%d/%d", mci->getNumFinished(), mci->getNumEditable());
                }
            default:
                return QVariant(); // Status => no text
            }
        }
        else if (role == Qt::DecorationRole && column < numLangs) {
            if (ContextItem *contextItem = mci->contextItem(column)) {
                if (contextItem->isObsolete())
                    return pxObsolete;
                if (contextItem->isFinished())
                    return contextItem->finishedDangerCount() > 0 ? pxWarning : pxOn;
                return contextItem->unfinishedDangerCount() > 0 ? pxDanger : pxOff;
            }
            return QVariant();
        }
        else if (role == SortRole) {
            switch (column - numLangs) {
            case 0: // Context (same as display role)
                return mci->context().simplified();
            case 1: // Items
                return mci->getNumEditable();
            default: // Percent
                if (ContextItem *contextItem = mci->contextItem(column)) {
                    int totalItems = contextItem->nonobsoleteCount();
                    int percent = totalItems ? (100 * contextItem->finishedCount()) / totalItems : 100;
                    int rslt = percent * (((1 << 28) - 1) / 100) + totalItems;
                    if (contextItem->isObsolete()) {
                        rslt |= (1 << 30);
                    } else if (contextItem->isFinished()) {
                        rslt |= (1 << 29);
                        if (!contextItem->finishedDangerCount())
                            rslt |= (1 << 28);
                    } else {
                        if (!contextItem->unfinishedDangerCount())
                            rslt |= (1 << 28);
                    }
                    return rslt;
                }
                return INT_MAX;
            }
        }
        else if (role == Qt::ForegroundRole && column >= numLangs
                 && m_data->multiContextItem(row)->isObsolete()) {
            return QBrush(Qt::darkGray);
        }
        else if (role == Qt::ForegroundRole && column == numLangs
                 && m_data->multiContextItem(row)->context().isEmpty()) {
            return QBrush(QColor(0, 0xa0, 0xa0));
        }
        else if (role == Qt::BackgroundRole) {
            if (column < numLangs && numLangs != 1) {
                QBrush brush = m_data->brushForModel(column);
                if (row & 1) {
                    brush.setColor(brush.color().darker(108));
                }
                return brush;
            }
        }
    }
    return QVariant();
}

MultiDataIndex MessageModel::dataIndex(const QModelIndex &index, int model) const
{
    Q_ASSERT(index.isValid());
    Q_ASSERT(index.internalId());
    return MultiDataIndex(model, index.internalId() - 1, index.row());
}

QT_END_NAMESPACE
