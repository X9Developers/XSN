// Copyright (c) 2011-2017 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientmodel.h>

#include <bantablemodel.h>
#include <guiconstants.h>
#include <peertablemodel.h>

#include <chainparams.h>
#include <checkpoints.h>
#include <clientversion.h>
#include <validation.h>
#include <net.h>
#include <txmempool.h>
#include <ui_interface.h>
#include <util.h>

#include <masternodeman.h>
#include <masternode-sync.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <privatesend/privatesend.h>

#include <stdint.h>

#include <QDebug>
#include <QTimer>

class CBlockIndex;

static const int64_t nClientStartupTime = GetTime();
static int64_t nLastHeaderTipUpdateNotification = 0;
static int64_t nLastBlockTipUpdateNotification = 0;

ClientModel::ClientModel(interfaces::Node& node, OptionsModel *_optionsModel, QObject *parent) :
    QObject(parent),
    m_node(node),
    optionsModel(_optionsModel),
    peerTableModel(0),
    cachedMasternodeCountString(""),
    banTableModel(0),
    pollTimer(0)
{
    cachedBestHeaderHeight = -1;
    cachedBestHeaderTime = -1;
    peerTableModel = new PeerTableModel(m_node, this);
    banTableModel = new BanTableModel(m_node, this);
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    pollMnTimer = new QTimer(this);
    connect(pollMnTimer, SIGNAL(timeout()), this, SLOT(updateMnTimer()));
    // no need to update as frequent as data for balances/txes/blocks
    pollMnTimer->start(MODEL_UPDATE_DELAY * 4);

    subscribeToCoreSignals();
}

ClientModel::~ClientModel()
{
    unsubscribeFromCoreSignals();
}

int ClientModel::getNumConnections(unsigned int flags) const
{
    CConnman::NumConnections connections = CConnman::CONNECTIONS_NONE;

    if(flags == CONNECTIONS_IN)
        connections = CConnman::CONNECTIONS_IN;
    else if (flags == CONNECTIONS_OUT)
        connections = CConnman::CONNECTIONS_OUT;
    else if (flags == CONNECTIONS_ALL)
        connections = CConnman::CONNECTIONS_ALL;

	return m_node.getNodeCount(connections);
}

QString ClientModel::getMasternodeCountString() const
{
    // return tr("Total: %1 (PS compatible: %2 / Enabled: %3) (IPv4: %4, IPv6: %5, TOR: %6)").arg(QString::number((int)mnodeman.size()))
    return tr("Total: %1 (PS compatible: %2 / Enabled: %3)")
            .arg(QString::number((int)mnodeman.size()))
            .arg(QString::number((int)mnodeman.CountEnabled(MIN_PRIVATESEND_PEER_PROTO_VERSION)))
            .arg(QString::number((int)mnodeman.CountEnabled()));
            // .arg(QString::number((int)mnodeman.CountByIP(NET_IPV4)))
            // .arg(QString::number((int)mnodeman.CountByIP(NET_IPV6)))
            // .arg(QString::number((int)mnodeman.CountByIP(NET_TOR)));
}

int ClientModel::getNumBlocks() const
{
    LOCK(cs_main);
    return chainActive.Height();
}

int ClientModel::getHeaderTipHeight() const
{
    if (cachedBestHeaderHeight == -1) {
        // make sure we initially populate the cache via a cs_main lock
        // otherwise we need to wait for a tip update
        int height;
        int64_t blockTime;
        if (m_node.getHeaderTip(height, blockTime)) {
            cachedBestHeaderHeight = height;
            cachedBestHeaderTime = blockTime;
        }
    }
    return cachedBestHeaderHeight;
}

int64_t ClientModel::getHeaderTipTime() const
{
    if (cachedBestHeaderTime == -1) {
        int height;
        int64_t blockTime;
        if (m_node.getHeaderTip(height, blockTime)) {
            cachedBestHeaderHeight = height;
            cachedBestHeaderTime = blockTime;
        }
    }
    return cachedBestHeaderTime;
}

quint64 ClientModel::getTotalBytesRecv() const
{
    if(!g_connman)
        return 0;
    return g_connman->GetTotalBytesRecv();
}

quint64 ClientModel::getTotalBytesSent() const
{
    if(!g_connman)
        return 0;
    return g_connman->GetTotalBytesSent();
}

QDateTime ClientModel::getLastBlockDate() const
{
    LOCK(cs_main);

    if (chainActive.Tip())
        return QDateTime::fromTime_t(chainActive.Tip()->GetBlockTime());

    return QDateTime::fromTime_t(Params().GenesisBlock().GetBlockTime()); // Genesis block's time of current network
}

long ClientModel::getMempoolSize() const
{
    return mempool.size();
}

size_t ClientModel::getMempoolDynamicUsage() const
{
    return mempool.DynamicMemoryUsage();
}

//double ClientModel::getVerificationProgress(const CBlockIndex *tipIn) const
//{
//    CBlockIndex *tip = const_cast<CBlockIndex *>(tipIn);
//    if (!tip)
//    {
//        LOCK(cs_main);
//        tip = chainActive.Tip();
//    }
//    return Checkpoints::GuessVerificationProgress(Params().Checkpoints(), tip);
//}

void ClientModel::updateTimer()
{
    // no locking required at this point
    // the following calls will acquire the required lock
    Q_EMIT mempoolSizeChanged(m_node.getMempoolSize(), m_node.getMempoolDynamicUsage());
    Q_EMIT bytesChanged(m_node.getTotalBytesRecv(), m_node.getTotalBytesSent());
}

void ClientModel::updateMnTimer()
{
    QString newMasternodeCountString = getMasternodeCountString();

    if (cachedMasternodeCountString != newMasternodeCountString)
    {
        cachedMasternodeCountString = newMasternodeCountString;

        Q_EMIT strMasternodesChanged(cachedMasternodeCountString);
    }
}

void ClientModel::updateNumConnections(int numConnections)
{
    Q_EMIT numConnectionsChanged(numConnections);
}

void ClientModel::updateNetworkActive(bool networkActive)
{
    Q_EMIT networkActiveChanged(networkActive);
}

void ClientModel::updateAlert()
{
    // Show error message notification for new alert
//    if(status == CT_NEW)
//    {
//        uint256 hash_256;
//        hash_256.SetHex(hash.toStdString());
//        CAlert alert = CAlert::getAlertByHash(hash_256);
//        if(!alert.IsNull())
//        {
//            Q_EMIT message(tr("Network Alert"), QString::fromStdString(alert.strStatusBar), CClientUIInterface::ICON_ERROR);
//        }
//    }

    Q_EMIT alertsChanged(getStatusBarWarnings());
}

bool ClientModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload();
}

enum BlockSource ClientModel::getBlockSource() const
{
    if (m_node.getReindex())
        return BlockSource::BLOCK_SOURCE_REINDEX;
    else if (m_node.getImporting())
        return BlockSource::BLOCK_SOURCE_DISK;
    else if (getNumConnections() > 0)
        return BLOCK_SOURCE_NETWORK;

    return BLOCK_SOURCE_NONE;
}

void ClientModel::setNetworkActive(bool active)
{
    if (g_connman) {
         g_connman->SetNetworkActive(active);
    }
}

bool ClientModel::getNetworkActive() const
{
    if (g_connman) {
        return g_connman->GetNetworkActive();
    }
    return false;
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(m_node.getWarnings("gui"));
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

PeerTableModel *ClientModel::getPeerTableModel()
{
    return peerTableModel;
}

BanTableModel *ClientModel::getBanTableModel()
{
    return banTableModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatSubVersion() const
{
    return QString::fromStdString(strSubVersion);
}

bool ClientModel::isReleaseVersion() const
{
    return CLIENT_VERSION_IS_RELEASE;
}

QString ClientModel::clientName() const
{
    return QString::fromStdString(CLIENT_NAME);
}

QString ClientModel::formatClientStartupTime() const
{
    return QDateTime::fromTime_t(nClientStartupTime).toString();
}

QString ClientModel::dataDir() const
{
    return QString::fromStdString(GetDataDir().string());
}

void ClientModel::updateBanlist()
{
    banTableModel->refresh();
}

// Handlers for core signals
static void ShowProgress(ClientModel *clientmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(clientmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
}

static void NotifyNumConnectionsChanged(ClientModel *clientmodel, int newNumConnections)
{
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged: " + QString::number(newNumConnections);
    QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumConnections));
}

static void NotifyNetworkActiveChanged(ClientModel *clientmodel, bool networkActive)
{
    QMetaObject::invokeMethod(clientmodel, "updateNetworkActive", Qt::QueuedConnection,
                              Q_ARG(bool, networkActive));
}

static void NotifyAlertChanged(ClientModel *clientmodel)
{
    qDebug() << "NotifyAlertChanged";
    QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection);
}

static void BannedListChanged(ClientModel *clientmodel)
{
    qDebug() << QString("%1: Requesting update for peer banlist").arg(__func__);
    QMetaObject::invokeMethod(clientmodel, "updateBanlist", Qt::QueuedConnection);
}

static void BlockTipChanged(ClientModel *clientmodel, bool initialSync, int height, int64_t blockTime, double verificationProgress, bool fHeader)
{
    // lock free async UI updates in case we have a new block tip
    // during initial sync, only update the UI if the last update
    // was > 250ms (MODEL_UPDATE_DELAY) ago
    int64_t now = 0;
    if (initialSync)
        now = GetTimeMillis();

    int64_t& nLastUpdateNotification = fHeader ? nLastHeaderTipUpdateNotification : nLastBlockTipUpdateNotification;

    if (fHeader) {
        // cache best headers time and height to reduce future cs_main locks
        clientmodel->cachedBestHeaderHeight = height;
        clientmodel->cachedBestHeaderTime = blockTime;
    }
    // if we are in-sync, update the UI regardless of last update time
    if (!initialSync || now - nLastUpdateNotification > MODEL_UPDATE_DELAY) {
        //pass an async signal to the UI thread
        QMetaObject::invokeMethod(clientmodel, "numBlocksChanged", Qt::QueuedConnection,
                                  Q_ARG(int, height),
                                  Q_ARG(QDateTime, QDateTime::fromTime_t(blockTime)),
                                  Q_ARG(double, verificationProgress),
                                  Q_ARG(bool, fHeader));
        nLastUpdateNotification = now;
    }
}

static void NotifyAdditionalDataSyncProgressChanged(ClientModel *clientmodel, double nSyncProgress)
{
    QMetaObject::invokeMethod(clientmodel, "additionalDataSyncProgressChanged", Qt::QueuedConnection,
                              Q_ARG(double, nSyncProgress));
}

void ClientModel::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_show_progress = m_node.handleShowProgress(boost::bind(ShowProgress, this, _1, _2));
    m_handler_notify_num_connections_changed = m_node.handleNotifyNumConnectionsChanged(boost::bind(NotifyNumConnectionsChanged, this, _1));
    m_handler_notify_network_active_changed = m_node.handleNotifyNetworkActiveChanged(boost::bind(NotifyNetworkActiveChanged, this, _1));
    m_handler_notify_alert_changed = m_node.handleNotifyAlertChanged(boost::bind(NotifyAlertChanged, this));
    m_handler_banned_list_changed = m_node.handleBannedListChanged(boost::bind(BannedListChanged, this));
    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(boost::bind(BlockTipChanged, this, _1, _2, _3, _4, false));
    m_handler_notify_header_tip = m_node.handleNotifyHeaderTip(boost::bind(BlockTipChanged, this, _1, _2, _3, _4, true));
}

void ClientModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_show_progress->disconnect();
    m_handler_notify_num_connections_changed->disconnect();
    m_handler_notify_network_active_changed->disconnect();
    m_handler_notify_alert_changed->disconnect();
    m_handler_banned_list_changed->disconnect();
    m_handler_notify_block_tip->disconnect();
    m_handler_notify_header_tip->disconnect();
}
