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

const QString NEWADDRCOMMAND("getnewaddress");
const QString ADDMSADDRCOMMAND(R"(addmultisigaddress 2 '["%2", "%3", "%4"]')");
const QString VALIDATEADDRESS(R"(validateaddress "%2")");
const QString SIGNMESSAGECOMMAND(R"(signmessage "%1" "%2")");

namespace ColumnWidths {
enum Values {
    MINIMUM_COLUMN_WIDTH = 120,
    ADDRESS_COLUMN_WIDTH = 240,
    AMOUNT_MINIMUM_COLUMN_WIDTH = 200,
};
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
    stakeTPoS();
}

void TPoSPage::onClearClicked()
{

}

void TPoSPage::onRedeemClicked()
{
    auto selectedIndexes = ui->stakingAddressesView->selectionModel()->selectedRows();

    if (_walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        WalletModel::UnlockContext ctx(_walletModel->requestUnlock(false));
        if (!ctx.isValid())
        {
            //unlock was cancelled
            QMessageBox::warning(this, tr("TPoS"),
                                 tr("Wallet is locked and user declined to unlock. Can't redeem from TPoS address."),
                                 QMessageBox::Ok, QMessageBox::Ok);
            if (fDebug)
                LogPrintf("Wallet is locked and user declined to unlock. Can't redeem from TPoS address.\n");

            return;
        }
    }

    if(!selectedIndexes.empty())
    {

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
    connect(ui->removeRequestButton, &QPushButton::clicked, this, &TPoSPage::onRedeemClicked);
}

void TPoSPage::onStakeError()
{
    //    ui->stakeButton->setEnabled(false);
}

static QString PrepareQuestionString(const CBitcoinAddress &tposAddress,
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

static bool SendRawTransaction(CWalletTx &walletTx, CReserveKey &reserveKey)
{
    return pwalletMain->CommitTransaction(walletTx, reserveKey, g_connman.get(), NetMsgType::TX);
}

void TPoSPage::stakeTPoS()
{
    CReserveKey reserveKey(pwalletMain);
    CBitcoinAddress tposAddress(ui->tposAddress->text().toStdString());
    CBitcoinAddress merchantAddress(ui->merchantAddress->text().toStdString());

    if(!tposAddress.IsValid())
    {
        QMessageBox::warning(this, "TPoS", "Critical error, TPoS address is empty");
        return;
    }
    if(!merchantAddress.IsValid())
    {
        QMessageBox::warning(this, "TPoS", "Critical error, merchant address is empty");
        return;
    }

    std::string strError;
    auto merchantCommission = ui->merchantCut->value();
    if(auto walletTx = TPoSUtils::CreateTPoSTransaction(pwalletMain, reserveKey, tposAddress, merchantAddress, merchantCommission, strError))
    {
        // Display message box
        auto questionString = PrepareQuestionString(tposAddress, merchantAddress, merchantCommission);
        QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm creating tpos contract"),
                                                                   questionString,
                                                                   QMessageBox::Yes | QMessageBox::Cancel,
                                                                   QMessageBox::Cancel);

        if(retval != QMessageBox::Yes)
        {
            return;
        }

        if(!SendRawTransaction(*walletTx, reserveKey))
        {
            QMessageBox::warning(this, "TPoS", "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
        }

    }
    else
    {
        QMessageBox::warning(this, "TPoS", QString("Failed to create TPoS transaction: %1").arg(strError.c_str()));
        return;
    }
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void TPoSPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if(_columnResizingFixer)
        _columnResizingFixer->stretchColumnWidth(TPoSAddressesTableModel::CommissionPaid);
}
