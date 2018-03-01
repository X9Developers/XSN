// Copyright (c) 2017-2018 The XSN developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "clickablelabel.h"

#include <QMouseEvent>

//==============================================================================

ClickableLabel::ClickableLabel(QWidget *parent, Qt::WindowFlags f) :
    QLabel(parent, f)
{

}

//==============================================================================

ClickableLabel::ClickableLabel(const QString &text, QWidget *parent, Qt::WindowFlags f) :
    QLabel(text, parent, f)
{

}

//==============================================================================

void ClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        clicked();
    }
}

//==============================================================================
