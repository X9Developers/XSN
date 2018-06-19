#include "transactionsdialog.h"
#include "forms/ui_transactionsdialog.h"
#include "guiutil.h"
#include "transactionview.h"

TransactionsDialog::TransactionsDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TransactionsDialog)
{
    ui->setupUi(this);

    view = new TransactionView(platformStyle, this);
    ui->TransactionsPageContent->layout()->addWidget(view);
//    connect(&ThemeProvider::Instance(), SIGNAL(currentThemeChanged()), this, SLOT(onThemeChanged()));
    onThemeChanged();
}

TransactionsDialog::~TransactionsDialog()
{
    delete ui;
}

TransactionView *TransactionsDialog::transactionView() const
{
    return view;
}

void TransactionsDialog::onThemeChanged()
{
    auto themeName = GUIUtil::getThemeName();
    ui->label->setPixmap(QPixmap(
                             QString(
                                 ":/images/res/images/pages/transactions/%1/transactions-header.png").arg(themeName)));
}
