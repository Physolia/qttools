// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "errorsview.h"

#include "globals.h"
#include "messagemodel.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <QtGui/QStandardItem>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QListView>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

using namespace Qt::Literals::StringLiterals;

ErrorsView::ErrorsView(MultiDataModel *dataModel, QWidget *parent) :
    QListView(parent),
    m_dataModel(dataModel)
{
    m_list = new QStandardItemModel(this);
    setModel(m_list);
}

void ErrorsView::clear()
{
    m_list->clear();
}

void ErrorsView::addError(int model, const ErrorType type, const QString &arg)
{
    switch (type) {
      case SuperfluousAccelerator:
        addError(model, tr("Accelerator possibly superfluous in translation."));
        break;
      case MissingAccelerator:
        addError(model, tr("Accelerator possibly missing in translation."));
        break;
      case SurroundingWhitespaceDiffers:
        addError(model, tr("Translation does not have same leading and trailing whitespace as the source text."));
        break;
      case PunctuationDiffers:
        addError(model, tr("Translation does not end with the same punctuation as the source text."));
        break;
      case IgnoredPhrasebook:
        addError(model, tr("A phrase book suggestion for '%1' was ignored.").arg(arg));
        break;
      case PlaceMarkersDiffer:
        addError(model, tr("Translation does not refer to the same place markers as in the source text."));
        break;
      case NumerusMarkerMissing:
        addError(model, tr("Translation does not contain the necessary %n/%Ln place marker."));
        break;
      default:
        addError(model, tr("Unknown error"));
        break;
    }
}

QString ErrorsView::firstError()
{
    return (m_list->rowCount() == 0) ? QString() : m_list->item(0)->text();
}

void ErrorsView::addError(int model, const QString &error)
{
    static QIcon pxDanger = MarkIcon::create(MarkIcon::dangerMark);
    QString lang;
    if (m_dataModel->modelCount() > 1)
        lang = m_dataModel->model(model)->localizedLanguage() + ": "_L1;
    QStandardItem *item = new QStandardItem(pxDanger, lang + error);
    item->setEditable(false);
    m_list->appendRow(QList<QStandardItem*>() << item);
}

QT_END_NAMESPACE
