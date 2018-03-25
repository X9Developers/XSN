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

    using Pub3KeyTuple = std::tuple<QString, QString, QString>; // 3 public keys
    using SignedMessagesTuple = std::tuple<QString, QString>; // two signatures
    using MultisigTuple = std::tuple<Pub3KeyTuple, QString>; // 3 pubkeys + multisig addr
    using TPoSTuple = std::tuple<MultisigTuple, SignedMessagesTuple, QString>; // multisig + signatures + txid
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
    void stakeTPoS(MultisigTuple info);

private:
    Ui::TPoSPage *ui;
    WalletModel *_walletModel = nullptr;
    GUIUtil::TableViewLastColumnResizingFixer* _columnResizingFixer = nullptr;
    QPointer<TPoSAddressesTableModel> _addressesTableModel;
};

#endif // TPOSPAGE_H
