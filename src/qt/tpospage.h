#ifndef TPOSPAGE_H
#define TPOSPAGE_H

#include <QWidget>
#include <tuple>
#include <functional>
#include <QPointer>
#include "primitives/transaction.h"

class WalletModel;
class TPoSAddressesTableModel;

namespace Ui {
class TPoSPage;
}

namespace GUIUtil {
class TableViewLastColumnResizingFixer;
}

class TPoSPage : public QWidget
{
    Q_OBJECT

public:
    explicit TPoSPage(QWidget *parent = 0);
    ~TPoSPage();

    void setWalletModel(WalletModel* model);
    void refresh();

protected:
    virtual void resizeEvent(QResizeEvent *event) override;

private Q_SLOTS:
    void onStakeClicked();
    void onClearClicked();
    void onRedeemClicked();
    void onThemeChanged();
    void onShowRequestClicked();

private:
    void init();
    void connectSignals();
    void onStakeError();
    void stakeTPoS();

private:
    Ui::TPoSPage *ui;
    WalletModel *_walletModel = nullptr;
    GUIUtil::TableViewLastColumnResizingFixer* _columnResizingFixer = nullptr;
    QPointer<TPoSAddressesTableModel> _addressesTableModel;
};

#endif // TPOSPAGE_H
