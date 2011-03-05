/***************************************************************************
                          wwidget.cpp  -  description
                             -------------------
    begin                : Wed Jun 18 2003
    copyright            : (C) 2003 by Tue & Ken Haste Andersen
    email                : haste@diku.dk
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/


#include <QtGui>
#include <QtDebug>

#include "wwidget.h"
#include "controlobject.h"
#include "controlobjectthreadwidget.h"


// Static member variable definition
QString WWidget::m_qPath;

WWidget::WWidget(QWidget * parent, Qt::WFlags flags) : QWidget(parent, flags)
{

    m_fValue = 0.;
    m_bOff = false;
    connect(this, SIGNAL(valueChangedLeftDown(double)), this, SLOT(slotReEmitValueDown(double)));
    connect(this, SIGNAL(valueChangedRightDown(double)), this, SLOT(slotReEmitValueDown(double)));
    connect(this, SIGNAL(valueChangedLeftUp(double)), this, SLOT(slotReEmitValueUp(double)));
    connect(this, SIGNAL(valueChangedRightUp(double)), this, SLOT(slotReEmitValueUp(double)));

    setAttribute(Qt::WA_StaticContents);
    setFocusPolicy(Qt::ClickFocus);
    //setBackgroundMode(Qt::NoBackground); //this is deprecated, and commenting it out doesn't seem to change anything -kousu 2009/03
}

WWidget::~WWidget()
{
}


bool WWidget::event(QEvent *event)
{
  if(event->type() == QEvent::ToolTip)
    {
      QHelpEvent *tooltip = static_cast<QHelpEvent *>(event);
      QToolTip::showText(tooltip->globalPos(), toolTip(), this);
      //http://doc.trolltech.com/4.3/widgets-tooltips-sortingbox-cpp.html suggests you should use QToolTip::hideText in certain cases; it works well enough on OS X with just this code though...
    }
  return QWidget::event(event);
}

void WWidget::setValue(double fValue)
{
    m_fValue = fValue;
    update();
}

void WWidget::setOnOff(double d)
{
    if (d==0.)
        m_bOff = false;
    else
        m_bOff = true;

    repaint();
}

void WWidget::slotReEmitValueDown(double fValue)
{
    emit(valueChangedDown(fValue));
}

void WWidget::slotReEmitValueUp(double fValue)
{
    emit(valueChangedUp(fValue));
}

int WWidget::selectNodeInt(const QDomNode &nodeHeader, const QString sNode)
{
    QString text = selectNode(nodeHeader, sNode).toElement().text();
    bool ok;
    int conv = text.toInt(&ok, 0);
    if (ok) {
        return conv;
    } else {
        return 0;
    }
}

float WWidget::selectNodeFloat(const QDomNode &nodeHeader, const QString sNode)
{
    return selectNode(nodeHeader, sNode).toElement().text().toFloat();
}

QString WWidget::selectNodeQString(const QDomNode &nodeHeader, const QString sNode)
{
    QString ret;
    QDomNode node = selectNode(nodeHeader, sNode);
    if (!node.isNull())
        ret = node.toElement().text();
    else
        ret = "";
    return ret;
}

QDomNode WWidget::selectNode(const QDomNode &nodeHeader, const QString sNode)
{
    QDomNode node = nodeHeader.firstChild();
    while (!node.isNull())
    {
        if (node.nodeName() == sNode)
            return node;
        node = node.nextSibling();
    }
    return node;
}

const QString WWidget::getPath(QString location)
{
    QString l(location);
    return l.prepend(m_qPath);
}

void WWidget::setPixmapPath(QString qPath)
{
    m_qPath = qPath;
}

QDomElement WWidget::openXMLFile(QString path, QString name)
{
    QDomDocument doc(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "Could not open xml file:" << file.fileName();
        return QDomElement();
    }
    if (!doc.setContent(&file))
    {
        qWarning() << "Error parsing xml file:" << file.fileName();
        file.close();
        return QDomElement();
    }

    file.close();
    return doc.documentElement();
}

double WWidget::getValue() {
   return m_fValue;
}

void WWidget::updateValue(double fValue)
{
    setValue(fValue);
    m_fValue = fValue;
    emit(valueChangedUp(fValue));
    emit(valueChangedDown(fValue));
}
