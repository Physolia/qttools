// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "helpviewer.h"
#include "helpviewerimpl.h"

#include "helpenginewrapper.h"
#include "tracer.h"

#include <QtCore/QFileInfo>
#include <QtCore/QStringBuilder>
#include <QtCore/QTemporaryFile>

#include <QtGui/QDesktopServices>
#if QT_CONFIG(clipboard)
#include <QtGui/QClipboard>
#endif
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <QtGui/QWheelEvent>

#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

#include <QtHelp/QHelpEngineCore>

#include <qlitehtmlwidget.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

const int kMaxHistoryItems = 20;

const struct ExtensionMap {
    const char *extension;
    const char *mimeType;
} extensionMap[] = {
    { ".bmp", "image/bmp" },
    { ".css", "text/css" },
    { ".gif", "image/gif" },
    { ".html", "text/html" },
    { ".htm", "text/html" },
    { ".ico", "image/x-icon" },
    { ".jpeg", "image/jpeg" },
    { ".jpg", "image/jpeg" },
    { ".js", "application/x-javascript" },
    { ".mng", "video/x-mng" },
    { ".pbm", "image/x-portable-bitmap" },
    { ".pgm", "image/x-portable-graymap" },
    { ".pdf", nullptr },
    { ".png", "image/png" },
    { ".ppm", "image/x-portable-pixmap" },
    { ".rss", "application/rss+xml" },
    { ".svg", "image/svg+xml" },
    { ".svgz", "image/svg+xml" },
    { ".text", "text/plain" },
    { ".tif", "image/tiff" },
    { ".tiff", "image/tiff" },
    { ".txt", "text/plain" },
    { ".xbm", "image/x-xbitmap" },
    { ".xml", "text/xml" },
    { ".xpm", "image/x-xpm" },
    { ".xsl", "text/xsl" },
    { ".xhtml", "application/xhtml+xml" },
    { ".wml", "text/vnd.wap.wml" },
    { ".wmlc", "application/vnd.wap.wmlc" },
    { "about:blank", nullptr },
    { nullptr, nullptr }
};

static void setLight(QWidget *widget)
{
    // Make docs' contents visible in dark theme
    QPalette p = widget->palette();
    p.setColor(QPalette::Inactive, QPalette::Highlight,
               p.color(QPalette::Active, QPalette::Highlight));
    p.setColor(QPalette::Inactive, QPalette::HighlightedText,
               p.color(QPalette::Active, QPalette::HighlightedText));
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::Text, Qt::black);
    widget->setPalette(p);
}

static bool isDarkTheme()
{
    // Either Qt realizes that it is dark, or the palette exposes it, by having
    // the window background darker than the text
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
            || QGuiApplication::palette().color(QPalette::Base).lightnessF()
            < QGuiApplication::palette().color(QPalette::Text).lightnessF();
}

static void setPaletteFromApp(QWidget *widget)
{
    QPalette appPalette = QGuiApplication::palette();
    // Detach the palette from the application palette, so it doesn't change directly when the
    // application palette changes.
    // That ensures that if the system is dark, and dark documentation is show, and the user
    // switches the system to a light theme, that the documentation background stays dark for the
    // visible page until either the we got informed by the change in application palette, or the
    // user switched pages
    appPalette.setColor(QPalette::Base, appPalette.color(QPalette::Base));
    widget->setPalette(appPalette);
}

static QByteArray getData(const QUrl &url, QWidget *widget)
{
    // This is a hack for Qt documentation,
    // which decides to use a simpler CSS if the viewer does not have JavaScript
    // which was a hack to decide if we are viewing in QTextBrowser or QtWebEngine et al.
    // Force it to use the "normal" offline CSS even without JavaScript, since litehtml can
    // handle that, and inject a dark themed CSS into Qt documentation for dark Qt Creator themes
    QUrl actualUrl = url;
    QString path = url.path(QUrl::FullyEncoded);
    static const char simpleCss[] = "/offline-simple.css";
    if (path.endsWith(simpleCss)) {
        if (isDarkTheme()) {
            // check if dark CSS is shipped with documentation
            QString darkPath = path;
            darkPath.replace(simpleCss, "/offline-dark.css");
            actualUrl.setPath(darkPath);
            QByteArray data = HelpEngineWrapper::instance().fileData(actualUrl);
            if (!data.isEmpty()) {
                // we found the dark style
                // set background dark (by using app palette)
                setPaletteFromApp(widget);
                return data;
            }
        }
        path.replace(simpleCss, "/offline.css");
        actualUrl.setPath(path);
    }

    if (actualUrl.isValid())
        return HelpEngineWrapper::instance().fileData(actualUrl);

    const bool isAbout = (actualUrl.toString() == "about:blank"_L1);
    return isAbout ? HelpViewerImpl::AboutBlank.toUtf8()
                   : HelpViewerImpl::PageNotFoundMessage.arg(url.toString()).toUtf8();
}

class HelpViewerPrivate
{
public:
    struct HistoryItem
    {
        QUrl url;
        QString title;
        int vscroll;
    };
    HistoryItem currentHistoryItem() const;
    void setSourceInternal(const QUrl &url, int *vscroll = nullptr, bool reload = false);
    void incrementZoom(int steps);
    void applyZoom(int percentage);

    HelpViewer *q = nullptr;
    QLiteHtmlWidget *m_viewer = nullptr;
    std::vector<HistoryItem> m_backItems;
    std::vector<HistoryItem> m_forwardItems;
    int m_fontZoom = 100; // zoom percentage
};

HelpViewerPrivate::HistoryItem HelpViewerPrivate::currentHistoryItem() const
{
    return { m_viewer->url(), m_viewer->title(), m_viewer->verticalScrollBar()->value() };
}

void HelpViewerPrivate::setSourceInternal(const QUrl &url, int *vscroll, bool reload)
{
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    const bool isHelp = (url.toString() == "help"_L1);
    const QUrl resolvedUrl = (isHelp ? HelpViewerImpl::LocalHelpFile
                                     : HelpEngineWrapper::instance().findFile(url));

    QUrl currentUrlWithoutFragment = m_viewer->url();
    currentUrlWithoutFragment.setFragment({});
    QUrl newUrlWithoutFragment = resolvedUrl;
    newUrlWithoutFragment.setFragment({});

    m_viewer->setUrl(resolvedUrl);
    if (currentUrlWithoutFragment != newUrlWithoutFragment || reload) {
        // Users can register arbitrary documentation, so we do not expect the documentation to
        // support dark themes, and start with light palette.
        // We override this if we find Qt's dark style
        setLight(q);
        m_viewer->setHtml(QString::fromUtf8(getData(resolvedUrl, q)));
    }
    if (vscroll)
        m_viewer->verticalScrollBar()->setValue(*vscroll);
    else
        m_viewer->scrollToAnchor(resolvedUrl.fragment(QUrl::FullyEncoded));

    QGuiApplication::restoreOverrideCursor();

    emit q->sourceChanged(q->source());
    emit q->loadFinished();
    emit q->titleChanged();
}

void HelpViewerPrivate::incrementZoom(int steps)
{
    const int incrementPercentage = 10 * steps; // 10 percent increase by single step
    const int previousZoom = m_fontZoom;
    applyZoom(previousZoom + incrementPercentage);
}

void HelpViewerPrivate::applyZoom(int percentage)
{
    const int newZoom = qBound(10, percentage, 300);
    if (newZoom == m_fontZoom)
        return;
    m_fontZoom = newZoom;
    m_viewer->setZoomFactor(newZoom / 100.0);
}

HelpViewer::HelpViewer(qreal zoom, QWidget *parent)
    : QWidget(parent)
    , d(new HelpViewerPrivate)
{
    auto layout = new QVBoxLayout;
    d->q = this;
    d->m_viewer = new QLiteHtmlWidget(this);
    d->m_viewer->setResourceHandler([this](const QUrl &url) { return getData(url, this); });
    d->m_viewer->viewport()->installEventFilter(this);
    const int zoomPercentage = zoom == 0 ? 100 : zoom * 100;
    d->applyZoom(zoomPercentage);
    connect(d->m_viewer, &QLiteHtmlWidget::linkClicked, this, &HelpViewer::setSource);
    connect(d->m_viewer, &QLiteHtmlWidget::linkHighlighted, this, &HelpViewer::highlighted);
#if QT_CONFIG(clipboard)
    connect(d->m_viewer, &QLiteHtmlWidget::copyAvailable, this, &HelpViewer::copyAvailable);
#endif
    setLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(d->m_viewer, 10);

    // If the platform supports it, changes of color scheme light/dark take effect during runtime:
    connect(
            QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] {
                int vscroll = d->m_viewer->verticalScrollBar()->value();
                d->setSourceInternal(source(), &vscroll, /*reload*/ true);
            },
            // Queue to make sure that the palette is actually applied on the application
            Qt::QueuedConnection);
}

HelpViewer::~HelpViewer()
{
    delete d;
}

QFont HelpViewer::viewerFont() const
{
    return d->m_viewer->defaultFont();
}

void HelpViewer::setViewerFont(const QFont &font)
{
    d->m_viewer->setDefaultFont(font);
}

void HelpViewer::scaleUp()
{
    d->incrementZoom(1);
}

void HelpViewer::scaleDown()
{
    d->incrementZoom(-1);
}

void HelpViewer::resetScale()
{
    d->applyZoom(100);
}

qreal HelpViewer::scale() const
{
    return d->m_viewer->zoomFactor();
}

QString HelpViewer::title() const
{
    return d->m_viewer->title();
}

QUrl HelpViewer::source() const
{
    return d->m_viewer->url();
}

void HelpViewer::reload()
{
    doSetSource(source(), true);
}

void HelpViewer::setSource(const QUrl &url)
{
    doSetSource(url, false);
}

void HelpViewer::doSetSource(const QUrl &url, bool reload)
{
    if (launchWithExternalApp(url))
        return;

    d->m_forwardItems.clear();
    emit forwardAvailable(false);
    if (d->m_viewer->url().isValid()) {
        d->m_backItems.push_back(d->currentHistoryItem());
        while (d->m_backItems.size() > kMaxHistoryItems) // this should trigger only once anyhow
            d->m_backItems.erase(d->m_backItems.begin());
        emit backwardAvailable(true);
    }

    d->setSourceInternal(url, nullptr, reload);
}

#if QT_CONFIG(printer)
void HelpViewer::print(QPrinter *printer)
{
    d->m_viewer->print(printer);
}
#endif

QString HelpViewer::selectedText() const
{
    return d->m_viewer->selectedText();
}

bool HelpViewer::isForwardAvailable() const
{
    return !d->m_forwardItems.empty();
}

bool HelpViewer::isBackwardAvailable() const
{
    return !d->m_backItems.empty();
}

static QTextDocument::FindFlags textDocumentFlagsForFindFlags(HelpViewer::FindFlags flags)
{
    QTextDocument::FindFlags textDocFlags;
    if (flags & HelpViewer::FindBackward)
        textDocFlags |= QTextDocument::FindBackward;
    if (flags & HelpViewer::FindCaseSensitively)
        textDocFlags |= QTextDocument::FindCaseSensitively;
    return textDocFlags;
}

bool HelpViewer::findText(const QString &text, FindFlags flags, bool incremental, bool fromSearch)
{
    Q_UNUSED(fromSearch);
    return d->m_viewer->findText(text, textDocumentFlagsForFindFlags(flags), incremental);
}

#if QT_CONFIG(clipboard)
void HelpViewer::copy()
{
    QGuiApplication::clipboard()->setText(selectedText());
}
#endif

void HelpViewer::home()
{
    setSource(HelpEngineWrapper::instance().homePage());
}

void HelpViewer::forward()
{
    HelpViewerPrivate::HistoryItem nextItem = d->currentHistoryItem();
    if (d->m_forwardItems.empty())
        return;
    d->m_backItems.push_back(nextItem);
    nextItem = d->m_forwardItems.front();
    d->m_forwardItems.erase(d->m_forwardItems.begin());

    emit backwardAvailable(isBackwardAvailable());
    emit forwardAvailable(isForwardAvailable());
    d->setSourceInternal(nextItem.url, &nextItem.vscroll);
}

void HelpViewer::backward()
{
    HelpViewerPrivate::HistoryItem previousItem = d->currentHistoryItem();
    if (d->m_backItems.empty())
        return;
    d->m_forwardItems.insert(d->m_forwardItems.begin(), previousItem);
    previousItem = d->m_backItems.back();
    d->m_backItems.pop_back();

    emit backwardAvailable(isBackwardAvailable());
    emit forwardAvailable(isForwardAvailable());
    d->setSourceInternal(previousItem.url, &previousItem.vscroll);
}

bool HelpViewer::eventFilter(QObject *src, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        auto we = static_cast<QWheelEvent *>(event);
        if (we->modifiers() == Qt::ControlModifier) {
            we->accept();
            const int deltaY = we->angleDelta().y();
            if (deltaY != 0)
                d->incrementZoom(deltaY / 120);
            return true;
        }
    }
    return QWidget::eventFilter(src, event);
}

bool HelpViewer::isLocalUrl(const QUrl &url)
{
    TRACE_OBJ
    const QString &scheme = url.scheme();
    return scheme.isEmpty()
        || scheme == "file"_L1
        || scheme == "qrc"_L1
        || scheme == "data"_L1
        || scheme == "qthelp"_L1
        || scheme == "about"_L1;
}

bool HelpViewer::canOpenPage(const QString &path)
{
    TRACE_OBJ
    return !mimeFromUrl(QUrl::fromLocalFile(path)).isEmpty();
}

QString HelpViewer::mimeFromUrl(const QUrl &url)
{
    TRACE_OBJ
    const QString &path = url.path();
    const int index = path.lastIndexOf(u'.');
    const QByteArray &ext = path.mid(index).toUtf8().toLower();

    const ExtensionMap *e = extensionMap;
    while (e->extension) {
        if (ext == e->extension)
            return QLatin1StringView(e->mimeType);
        ++e;
    }
    return "application/octet-stream"_L1;
}

bool HelpViewer::launchWithExternalApp(const QUrl &url)
{
    TRACE_OBJ
    if (isLocalUrl(url)) {
        const HelpEngineWrapper &helpEngine = HelpEngineWrapper::instance();
        const QUrl &resolvedUrl = helpEngine.findFile(url);
        if (!resolvedUrl.isValid())
            return false;

        const QString& path = resolvedUrl.toLocalFile();
        if (!canOpenPage(path)) {
            QTemporaryFile tmpTmpFile;
            if (!tmpTmpFile.open())
                return false;

            const QString &extension = QFileInfo(path).completeSuffix();
            QFile actualTmpFile(tmpTmpFile.fileName() % "."_L1 % extension);
            if (!actualTmpFile.open(QIODevice::ReadWrite | QIODevice::Truncate))
                return false;

            actualTmpFile.write(helpEngine.fileData(resolvedUrl));
            actualTmpFile.close();
            return QDesktopServices::openUrl(QUrl::fromLocalFile(actualTmpFile.fileName()));
        }
        return false;
    }
    return QDesktopServices::openUrl(url);
}

QT_END_NAMESPACE
