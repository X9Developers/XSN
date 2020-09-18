#ifndef TPOSADDRESSESTABLEMODEL_HPP
#define TPOSADDRESSESTABLEMODEL_HPP

#include <QAbstractTableModel>
#include <vector>
#include <QString>
#include <string>
#include <amount.h>
#include <ui_interface.h>
#include <base58.h>
#include <key_io.h>

class OptionsModel;
class TPoSContract;
class WalletModel;

namespace interfaces {
class Handler;
class Wallet;
}

class TPoSAddressesTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TPoSAddressesTableModel(WalletModel *parent,
                                     OptionsModel *optionsModel);

    ~TPoSAddressesTableModel();

    enum ColumnIndex {
        Address = 0, /**< TPoS address */
        Amount, /** < Total amount */
        AmountStaked,
        CommissionPaid
    };

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    const TPoSContract &contractByIndex(int index) const;

    void updateModel();
    void updateAmount(int row);

private Q_SLOTS:
    void updateDisplayUnit();

private:
    struct Entry {
        CAmount totalAmount;
        CAmount stakeAmount;
        CAmount commissionAmount;
    };

private:
    void refreshModel();
    void updateAmountColumnTitle();
    void NotifyTransactionChanged(const uint256& hash, ChangeType status);
    QString formatCommissionAmount(CAmount commissionAmount, int nOperatorReward) const;
    QString formatAmount(CAmount amountAsStr) const;
    Entry GetAmountForAddress(CTxDestination address);

private:
    std::unique_ptr<interfaces::Handler> transactionChangedHandler;
    WalletModel *walletModel;
    OptionsModel *optionsModel;
    const std::map<uint256, TPoSContract> &tposContracts;
    std::map<CTxDestination, Entry> amountsMap;
    QStringList columns;
};

#endif // TPOSADDRESSESTABLEMODEL_HPP
