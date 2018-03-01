#ifndef TRANSACTIONSDIALOG_H
#define TRANSACTIONSDIALOG_H

#include <QWidget>

class TransactionView;
class PlatformStyle;

namespace Ui {
class TransactionsDialog;
}

class TransactionsDialog : public QWidget
{
    Q_OBJECT

public:
    explicit TransactionsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~TransactionsDialog();

    TransactionView *transactionView() const;

private Q_SLOTS:
    void onThemeChanged();

private:
    Ui::TransactionsDialog *ui;
    TransactionView *view;
};

#endif // TRANSACTIONSDIALOG_H
