// Copyright (c) 2017-2018 The XSN developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef CLICKABLELABEL_HPP
#define CLICKABLELABEL_HPP

#include <QLabel>

class ClickableLabel : public QLabel
{
    Q_OBJECT
public:
    ClickableLabel(QWidget *parent = Q_NULLPTR,
                   Qt::WindowFlags f = Qt::WindowFlags());
    ClickableLabel(const QString &text,
                   QWidget *parent = Q_NULLPTR,
                   Qt::WindowFlags f = Qt::WindowFlags());

Q_SIGNALS:
    void clicked();

protected:
    virtual void mouseReleaseEvent(QMouseEvent *event);

};

#endif // CLICKABLELABEL_HPP
