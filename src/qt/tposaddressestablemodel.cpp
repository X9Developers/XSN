#include "tposaddressestablemodel.h"
#include "wallet/wallet.h"
#include "amount.h"
#include "init.h"
#include "optionsmodel.h"
#include "bitcoinunits.h"
#include "tpos/tposutils.h"
#include "validation.h"
#include <numeric>
#include <QFile>

static QString GetCommisionAmountColumnTitle(int unit)
{
    QString amountTitle = QObject::tr("Commission");
    if (BitcoinUnits::valid(unit)) {
        amountTitle += " (" + BitcoinUnits::name(unit) + ")";
    }
    return amountTitle;
}

static QString GetStakeAmountColumnTitle(int unit)
{
    QString amountTitle = QObject::tr("Reward");
    if (BitcoinUnits::valid(unit)) {
        amountTitle += " (" + BitcoinUnits::name(unit) + ")";
    }
    return amountTitle;
}

static QString FormatAmount(int displayUnit, CAmount amount, BitcoinUnits::SeparatorStyle separators)
{
    return BitcoinUnits::format(displayUnit,
                                amount,
                                false,
                                separators);
}

TPoSAddressesTableModel::TPoSAddressesTableModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent)
    : QAbstractTableModel(parent),
      wallet(wallet),
      optionsModel(optionsModel),
      tposContracts(wallet->tposOwnerContracts)

{
    auto displayUnit = optionsModel->getDisplayUnit();
    columns << tr("TPoS Address")
            << BitcoinUnits::getAmountColumnTitle(displayUnit)
            << GetStakeAmountColumnTitle(displayUnit)
            << GetCommisionAmountColumnTitle(displayUnit);

    connect(optionsModel, &OptionsModel::displayUnitChanged,
            this, &TPoSAddressesTableModel::updateDisplayUnit);
    refreshModel();

    wallet->NotifyTransactionChanged.connect(boost::bind(&TPoSAddressesTableModel::NotifyTransactionChanged, this, _1, _2, _3));
}

TPoSAddressesTableModel::~TPoSAddressesTableModel()
{
    wallet->NotifyTransactionChanged.disconnect(boost::bind(&TPoSAddressesTableModel::NotifyTransactionChanged, this, _1, _2, _3));
}

QVariant TPoSAddressesTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole && section < columns.size()) {
            return columns[section];
        }
    }
    return QVariant();
}

int TPoSAddressesTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return tposContracts.size();
}

int TPoSAddressesTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return columns.size();
}

QVariant TPoSAddressesTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole ||
            index.row() >= tposContracts.size())
        return QVariant();

    auto entryIt = std::next(tposContracts.begin(), index.row());
    CBitcoinAddress address = entryIt->second.tposAddress;
    auto it = amountsMap.find(address);

    switch(index.column())
    {
    case Address: return QString::fromStdString(address.ToString());
    case Amount: return it != std::end(amountsMap) ? formatAmount(it->second.totalAmount) : QString();
    case AmountStaked: return it != std::end(amountsMap) ? formatAmount(it->second.stakeAmount) : QString();
    case CommissionPaid: return it != std::end(amountsMap) ? formatCommissionAmount(it->second.commissionAmount, entryIt->second.stakePercentage) : QString();
    default:
        break;
    }

    return QVariant();
}

const TPoSContract &TPoSAddressesTableModel::contractByIndex(int index) const
{
    auto entryIt = std::next(tposContracts.begin(), index);
    return entryIt->second;
}

void TPoSAddressesTableModel::updateModel()
{
    beginResetModel();
    refreshModel();
    endResetModel();
}

void TPoSAddressesTableModel::updateAmount(int row)
{
    auto entryIt = std::next(tposContracts.begin(), row);
    CBitcoinAddress address = entryIt->second.tposAddress;
    amountsMap[address] = GetAmountForAddress(address);
    Q_EMIT dataChanged(index(row, Amount), index(row, CommissionPaid));
}

void TPoSAddressesTableModel::updateDisplayUnit()
{
    // Q_EMIT dataChanged to update Amount column with the current unit
    updateAmountColumnTitle();
    Q_EMIT dataChanged(index(0, Amount), index(rowCount() - 1, CommissionPaid));
}

void TPoSAddressesTableModel::refreshModel()
{
    amountsMap.clear();
    for(auto &&contract : tposContracts)
    {
        amountsMap[contract.second.tposAddress] = GetAmountForAddress(contract.second.tposAddress);
    }
}

/** Updates the column title to "Amount (DisplayUnit)" and Q_EMITs headerDataChanged() signal for table headers to react. */
void TPoSAddressesTableModel::updateAmountColumnTitle()
{
    columns[Amount] = BitcoinUnits::getAmountColumnTitle(optionsModel->getDisplayUnit());
    Q_EMIT headerDataChanged(Qt::Horizontal, Amount, Amount);
}

void TPoSAddressesTableModel::NotifyTransactionChanged(CWallet* wallet, const uint256& hash, ChangeType status)
{
    (void)status;

    return;

    auto maybeUpdate = [this](uint256 txid) {
        auto it = tposContracts.find(txid);
        if(it != std::end(tposContracts))
        {
            updateAmount(std::distance(tposContracts.begin(), it));
        }
    };

    // Find transaction in wallet
    std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash);
    // Determine whether to show transaction or not (determine this here so that no relocking is needed in GUI thread)
    if(mi != wallet->mapWallet.end())
    {
        const CWalletTx &wtx = mi->second;

        CAmount stakeAmount = 0;
        CAmount commissionAmount = 0;
        CBitcoinAddress tposAddress;
        if(TPoSUtils::GetTPoSPayments(wallet, wtx, stakeAmount, commissionAmount, tposAddress))
        {
            maybeUpdate(wtx.GetHash());
        }
    }
}

QString TPoSAddressesTableModel::formatCommissionAmount(CAmount commissionAmount, int percentage) const
{
    return QString("%1 (%2 %)").arg(formatAmount(commissionAmount)).arg(100 - percentage);
}

TPoSAddressesTableModel::Entry TPoSAddressesTableModel::GetAmountForAddress(CBitcoinAddress address)
{
    Entry result = { 0, 0, 0 };

    // xsn address
    if (!address.IsValid())
        return result;

    CScript scriptPubKey = GetScriptForDestination(address.Get());

    // Minimum confirmations
    int nMinDepth = 1;

    for (auto &&it : pwalletMain->mapWallet)
    {
        const CWalletTx& wtx = it.second;
        if (wtx.IsCoinBase() /*|| !IsFinalTx(wtx, )*/)
            continue;

        for(size_t i = 0; i < wtx.vout.size(); ++i)
        {
            const CTxOut& txout = wtx.vout[i];
            int tposIndex = -1;
            if (txout.scriptPubKey == scriptPubKey)
            {
                if (wtx.GetDepthInMainChain() >= nMinDepth && !wallet->IsSpent(it.first, i))
                {
                    result.totalAmount += txout.nValue;
                    tposIndex = i;
                }
            }

            if(tposIndex > 0 && wtx.IsCoinStake())
            {
                auto stakeAmount = wtx.vout[tposIndex].nValue;

                CAmount nCredit = 0;
                CAmount nDebit = 0;
                for(size_t i = 0; i < tposIndex; ++i)
                {
                    nCredit += wtx.vout[i].nValue;
                }
                for(auto &&txin : wtx.vin)
                {
                    CTransaction prevTx;
                    uint256 prevBlockHash;
                    if(GetTransaction(txin.prevout.hash, prevTx, Params().GetConsensus(), prevBlockHash))
                    {
                        if(txin.prevout.n < prevTx.vout.size())
                        {
                            nDebit += prevTx.vout[txin.prevout.n].nValue;
                        }
                    }
                }

                CAmount nNet = nCredit - nDebit;

                // at this moment nNet contains net stake reward
                // commission was sent to merchant address, so it was base of tx
                result.commissionAmount += nNet;
                // stake amount is just what was sent to tpos address
                result.stakeAmount += stakeAmount;
            }
        }
    }


    return result;
}

QString TPoSAddressesTableModel::formatAmount(CAmount amount) const
{
    return FormatAmount(optionsModel->getDisplayUnit(),
                        amount,
                        BitcoinUnits::separatorAlways);
}
