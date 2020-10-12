#ifndef TPOSPAGE_H
#define TPOSPAGE_H

#include <QWidget>
#include <tuple>
#include <functional>
#include <QPointer>
#include "primitives/transaction.h"
#include "interfaces/wallet.h"


class WalletModel;
class CBitcoinAddress;
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
    void onCancelClicked();
    void onThemeChanged();
    void onShowRequestClicked();

private:
    void init();
    void connectSignals();
    void onStakeError();
    void SendToAddress(const CTxDestination &address, CAmount nValue, int splitCount);
    void sendToTPoSAddress(const CBitcoinAddress &tposAddress);
    CTxDestination GetNewAddress();

    std::unique_ptr<interfaces::PendingWalletTx> CreateContractTransaction(QWidget *widget,
                                          const CTxDestination &tposAddress,
                                          const CBitcoinAddress &merchantAddress,
                                          int merchantCommission);

    std::unique_ptr<interfaces::PendingWalletTx> CreateCancelContractTransaction(QWidget *widget,
                                                const TPoSContract &contract);



private:
    Ui::TPoSPage *ui;
    WalletModel *_walletModel = nullptr;
    GUIUtil::TableViewLastColumnResizingFixer* _columnResizingFixer = nullptr;
    QPointer<TPoSAddressesTableModel> _addressesTableModel;
};

#endif // TPOSPAGE_H
