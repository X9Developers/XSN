#ifndef TPOSADDRESSESTABLEMODEL_HPP
#define TPOSADDRESSESTABLEMODEL_HPP

#include <QAbstractTableModel>
#include <vector>
#include <QString>
#include <string>
#include "amount.h"
#include "ui_interface.h"
#include "base58.h"

class CWallet;
class OptionsModel;
class TPoSContract;

class TPoSAddressesTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TPoSAddressesTableModel(CWallet* wallet,
                                     OptionsModel *optionsModel,
                                     QObject *parent = nullptr);

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
    void NotifyTransactionChanged(CWallet* wallet, const uint256& hash, ChangeType status);
    QString formatCommissionAmount(CAmount commissionAmount, int percentage) const;
    QString formatAmount(CAmount amountAsStr) const;
    Entry GetAmountForAddress(CBitcoinAddress address);

private:

    CWallet *wallet;
    OptionsModel *optionsModel;
    const std::map<uint256, TPoSContract> &tposContracts;
    std::map<CBitcoinAddress, Entry> amountsMap;
    QStringList columns;
};

#endif // TPOSADDRESSESTABLEMODEL_HPP
