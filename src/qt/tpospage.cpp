#include "tpospage.h"
#include "forms/ui_tpospage.h"
#include "util.h"
#include "base58.h"
#include "utilstrencodings.h"
#include "tpos/tposutils.h"
#include "init.h"
#include "wallet/wallet.h"
#include "walletmodel.h"
#include "tposaddressestablemodel.h"
#include "guiutil.h"
#include "script/sign.h"
#include "guiutil.h"
#include "net.h"
#include "utilmoneystr.h"
#include <interfaces/wallet.h>
#include <wallet/coincontrol.h>

#include <boost/optional.hpp>
#include <QPushButton>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QFile>
#include <fstream>

namespace ColumnWidths {
enum Values {
    MINIMUM_COLUMN_WIDTH = 120,
    ADDRESS_COLUMN_WIDTH = 240,
    AMOUNT_MINIMUM_COLUMN_WIDTH = 200,
};
}

static QString PrepareCreateContractQuestionString(const CBitcoinAddress &tposAddress,
                                                   const CBitcoinAddress &merchantAddress,
                                                   int commission)
{
    QString questionString = QObject::tr("Are you sure you want to setup tpos contract?");
    questionString.append("<br /><br />");

    // Show total amount + all alternative units
    questionString.append(QObject::tr("TPoS Address = <b>%1</b><br />Merchant address = <b>%2</b> <br />Merchant commission = <b>%3</b>")
                          .arg(QString::fromStdString(tposAddress.ToString()))
                          .arg(QString::fromStdString(merchantAddress.ToString()))
                          .arg(commission));


    return questionString;
}

std::unique_ptr<interfaces::PendingWalletTx> TPoSPage::CreateContractTransaction(QWidget *widget,
                                                                                 const CBitcoinAddress &tposAddress,
                                                                                 const CBitcoinAddress &merchantAddress,
                                                                                 int merchantCommission)
{
    std::string strError;
    auto questionString = PrepareCreateContractQuestionString(tposAddress, merchantAddress, merchantCommission);
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(widget, QObject::tr("Confirm creating tpos contract"),
                                                               questionString,
                                                               QMessageBox::Yes | QMessageBox::Cancel,
                                                               QMessageBox::Cancel);
    if(retval != QMessageBox::Yes)
    {
        return {};
    }
    if(auto walletTx =  _walletModel->wallet().createTPoSContractTransaction(tposAddress.Get(), merchantAddress.Get(), merchantCommission, strError))  {
        return walletTx;
    }

    throw std::runtime_error(QString("Failed to create tpos transaction: %1").arg(QString::fromStdString(strError)).toStdString());
}

std::unique_ptr<interfaces::PendingWalletTx> TPoSPage::CreateCancelContractTransaction(QWidget *widget,
                                                                                       const TPoSContract &contract)
{
    std::string strError;
    CTxDestination address;
    ExtractDestination(contract.scriptTPoSAddress, address);
    auto questionString = QString("Are you sure you want to cancel contract with address: <b>%1</b>").arg(EncodeDestination(address).c_str());
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(widget, QObject::tr("Confirm canceling tpos contract"),
                                                               questionString,
                                                               QMessageBox::Yes | QMessageBox::Cancel,
                                                               QMessageBox::Cancel);
    if(retval != QMessageBox::Yes)
    {
        return {};
    }

    if(auto walletTx = _walletModel->wallet().createCancelContractTransaction(contract, strError))
    {
        return walletTx;
    }

    throw std::runtime_error(QString("Failed to create tpos transaction: %1").arg(QString::fromStdString(strError)).toStdString());
}

static void SendPendingTransaction(interfaces::PendingWalletTx *pendingTx)
{
    std::string rejectReason;
    if (!pendingTx->commit({}, {}, {}, rejectReason))
        throw std::runtime_error(rejectReason);
}

void TPoSPage::SendToAddress(const CTxDestination &address, CAmount nValue, int splitCount)
{
    CAmount curBalance = _walletModel->wallet().getBalance();

    // Check amount
    if (nValue <= 0)
        throw std::runtime_error("Invalid amount");

    if (nValue > curBalance)
        throw std::runtime_error("Insufficient funds");

    if (!g_connman)
        std::runtime_error("Error: Peer-to-peer functionality missing or disabled");

    // Parse XSN address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    //    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;

    for (int i = 0; i < splitCount; ++i)
    {
        if (i == splitCount - 1)
        {
            uint64_t nRemainder = nValue % splitCount;
            vecSend.push_back({scriptPubKey, static_cast<CAmount>(nValue / splitCount + nRemainder), false});
        }
        else
        {
            vecSend.push_back({scriptPubKey, static_cast<CAmount>(nValue / splitCount), false});
        }
    }

    auto penWalletTx = _walletModel->wallet().createTransaction(vecSend, {}, true, nChangePosRet, nFeeRequired, strError);
    if(!penWalletTx)
    {
        if (nValue + nFeeRequired > _walletModel->wallet().getBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw std::runtime_error(strError);
    }
    SendPendingTransaction(penWalletTx.get());
}

CBitcoinAddress TPoSPage::GetNewAddress()
{
    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!_walletModel->wallet().getKeyFromPool(false, newKey))
        throw std::runtime_error("Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();


    _walletModel->wallet().setAddressBook(keyID, std::string(), "tpos address");
    //pwalletMain->SetAddressBook(keyID, std::string(), "tpos address");

    return CBitcoinAddress(keyID);
}

TPoSPage::TPoSPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TPoSPage)
{
    ui->setupUi(this);
    init();
}

TPoSPage::~TPoSPage()
{
    delete ui;
}

void TPoSPage::setWalletModel(WalletModel *model)
{
    _addressesTableModel = model->getTPoSAddressModel();
    _walletModel = model;
    ui->stakingAddressesView->setModel(_addressesTableModel);

    using namespace ColumnWidths;
    _columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(ui->stakingAddressesView, AMOUNT_MINIMUM_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH, this);

    ui->stakingAddressesView->setColumnWidth(TPoSAddressesTableModel::Address, ADDRESS_COLUMN_WIDTH);
    ui->stakingAddressesView->setColumnWidth(TPoSAddressesTableModel::AmountStaked, MINIMUM_COLUMN_WIDTH);
    ui->stakingAddressesView->setColumnWidth(TPoSAddressesTableModel::CommissionPaid, MINIMUM_COLUMN_WIDTH);
    ui->stakingAddressesView->setColumnWidth(TPoSAddressesTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);
}

void TPoSPage::refresh()
{
    _addressesTableModel->updateModel();
}

void TPoSPage::onStakeClicked()
{
    try
    {
        auto worker = [this]() {
            //            CReserveKey reserveKey(pwalletMain);
            CBitcoinAddress tposAddress = GetNewAddress();
            if(!tposAddress.IsValid())
            {
                throw std::runtime_error("Critical error, TPoS address is empty");
            }
            CBitcoinAddress merchantAddress(ui->merchantAddress->text().toStdString());
            if(!merchantAddress.IsValid())
            {
                throw std::runtime_error("Critical error, merchant address is empty");
            }
            auto merchantCommission = ui->merchantCut->value();
            if(auto penWalletTx = CreateContractTransaction(this, tposAddress, merchantAddress, merchantCommission))
            {
                SendPendingTransaction(penWalletTx.get());
                sendToTPoSAddress(tposAddress);
            }
        };
        WalletModel::EncryptionStatus encStatus = _walletModel->getEncryptionStatus();
        if (encStatus == WalletModel::Locked || encStatus == _walletModel->UnlockedForStakingOnly)
        {
            WalletModel::UnlockContext ctx(_walletModel->requestUnlock());
            if (!ctx.isValid())
            {
                //unlock was cancelled
                throw std::runtime_error("Wallet is locked and user declined to unlock. Can't redeem from TPoS address.");
            }

            worker();
        }
        else
        {
            worker();
        }
    }
    catch(std::exception &ex)
    {
        QMessageBox::warning(this, "TPoS", ex.what());
    }
}

void TPoSPage::onClearClicked()
{

}

void TPoSPage::onCancelClicked()
{
    auto selectedIndexes = ui->stakingAddressesView->selectionModel()->selectedRows();

    if(selectedIndexes.empty())
        return;

    auto worker = [this, &selectedIndexes] {
        //CReserveKey reserveKey(pwalletMain);
        auto contract = _addressesTableModel->contractByIndex(selectedIndexes.first().row());
        //CWalletTx wtxNew;
        if(auto penWalletTx = CreateCancelContractTransaction(this, contract))
        {
            SendPendingTransaction(penWalletTx.get());
        }
    };

    try
    {
        WalletModel::EncryptionStatus encStatus = _walletModel->getEncryptionStatus();
        if (encStatus == WalletModel::Locked || encStatus == _walletModel->UnlockedForStakingOnly)
        {
            WalletModel::UnlockContext ctx(_walletModel->requestUnlock());
            if (!ctx.isValid())
            {
                //unlock was cancelled
                QMessageBox::warning(this, tr("TPoS"),
                                     tr("Wallet is locked and user declined to unlock. Can't redeem from TPoS address."),
                                     QMessageBox::Ok, QMessageBox::Ok);

                return;
            }
            worker();
        }
        else
        {
            worker();
        }
    }
    catch(std::exception &ex)
    {
        QMessageBox::warning(this, "TPoS", ex.what());
    }
}

void TPoSPage::onThemeChanged()
{
    auto themeName = GUIUtil::getThemeName();
    ui->label->setPixmap(QPixmap(
                             QString(
                                 ":/images/res/images/pages/tpos/%1/tpos-header.png").arg(themeName)));
}

void TPoSPage::onShowRequestClicked()
{
    return;
    QItemSelectionModel *selectionModel = ui->stakingAddressesView->selectionModel();
    auto rows = selectionModel->selectedRows();
    if(!rows.empty())
    {
        QModelIndex index = rows.first();
        QString address = index.data(TPoSAddressesTableModel::Address).toString();
    }

}

void TPoSPage::init()
{
    connectSignals();
    onThemeChanged();
}

void TPoSPage::connectSignals()
{
    connect(ui->stakeButton, &QPushButton::clicked, this, &TPoSPage::onStakeClicked);
    connect(ui->clearButton, &QPushButton::clicked, this, &TPoSPage::onClearClicked);
    //    connect(ui->showRequestButton, &QPushButton::clicked, this, &TPoSPage::onShowRequestClicked);
    connect(ui->cancelButton, &QPushButton::clicked, this, &TPoSPage::onCancelClicked);
}

void TPoSPage::onStakeError()
{
    //    ui->stakeButton->setEnabled(false);
}


void TPoSPage::sendToTPoSAddress(const CBitcoinAddress &tposAddress)
{
    CAmount amount = ui->stakingAmount->value();
    int numberOfSplits = 1;
    if(amount > _walletModel->wallet().getStakeSplitThreshold() * COIN)
        numberOfSplits = std::min<unsigned int>(500, amount / (_walletModel->wallet().getStakeSplitThreshold() * COIN));
    SendToAddress(tposAddress.Get(), amount, numberOfSplits);
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void TPoSPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if(_columnResizingFixer)
        _columnResizingFixer->stretchColumnWidth(TPoSAddressesTableModel::CommissionPaid);
}
