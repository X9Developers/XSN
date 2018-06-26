// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CLIENTMODEL_H
#define BITCOIN_QT_CLIENTMODEL_H

#include <QObject>
#include <QDateTime>

#include <atomic>
#include <memory>

class AddressTableModel;
class BanTableModel;
class OptionsModel;
class PeerTableModel;
class TransactionTableModel;

class CWallet;
class CBlockIndex;

namespace interfaces {
class Handler;
class Node;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

enum BlockSource {
    BLOCK_SOURCE_NONE,
    BLOCK_SOURCE_REINDEX,
    BLOCK_SOURCE_DISK,
    BLOCK_SOURCE_NETWORK
};

enum NumConnections {
    CONNECTIONS_NONE = 0,
    CONNECTIONS_IN   = (1U << 0),
    CONNECTIONS_OUT  = (1U << 1),
    CONNECTIONS_ALL  = (CONNECTIONS_IN | CONNECTIONS_OUT),
};

/** Model for XSN network client. */
class ClientModel : public QObject
{
    Q_OBJECT

public:
    explicit ClientModel(interfaces::Node& node, OptionsModel *optionsModel, QObject *parent = 0);
    ~ClientModel();

    OptionsModel *getOptionsModel();
    PeerTableModel *getPeerTableModel();
    BanTableModel *getBanTableModel();

    //! Return number of connections, default is in- and outbound (total)
    int getNumConnections(unsigned int flags = CONNECTIONS_ALL) const;
    QString getMasternodeCountString() const;
    int getNumBlocks() const;
    int getHeaderTipHeight() const;
    int64_t getHeaderTipTime() const;
    //! Return number of transactions in the mempool
    long getMempoolSize() const;
    //! Return the dynamic memory usage of the mempool
    size_t getMempoolDynamicUsage() const;
    
    quint64 getTotalBytesRecv() const;
    quint64 getTotalBytesSent() const;

    //double getVerificationProgress(const CBlockIndex *tip) const;
    QDateTime getLastBlockDate() const;

    //! Return true if core is doing initial block download
    bool inInitialBlockDownload() const;
    //! Returns enum BlockSource of the current importing/syncing state
    enum BlockSource getBlockSource() const;
    //! Return true if network activity in core is enabled
    bool getNetworkActive() const;
    //! Toggle network activity state in core
    void setNetworkActive(bool active);
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;
    QString formatSubVersion() const;
    bool isReleaseVersion() const;
    QString clientName() const;
    QString formatClientStartupTime() const;
    QString dataDir() const;

    // caches for the best header
    mutable std::atomic<int> cachedBestHeaderHeight;
    mutable std::atomic<int64_t> cachedBestHeaderTime;

private:
    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_show_progress;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_connections_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_network_active_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_alert_changed;
    std::unique_ptr<interfaces::Handler> m_handler_banned_list_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_header_tip;
    OptionsModel *optionsModel;
    PeerTableModel *peerTableModel;
    QString cachedMasternodeCountString;
    BanTableModel *banTableModel;

    QTimer *pollTimer;
    QTimer *pollMnTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

Q_SIGNALS:
    void numConnectionsChanged(int count);
    void strMasternodesChanged(const QString &strMasternodes);
    void numBlocksChanged(int count, const QDateTime& blockDate, double nVerificationProgress, bool header);
    void additionalDataSyncProgressChanged(double nSyncProgress);
    void mempoolSizeChanged(long count, size_t mempoolSizeInBytes);
    void networkActiveChanged(bool networkActive);
    void alertsChanged(const QString &warnings);
    void bytesChanged(quint64 totalBytesIn, quint64 totalBytesOut);

    //! Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Show progress dialog e.g. for verifychain
    void showProgress(const QString &title, int nProgress);

public Q_SLOTS:
    void updateTimer();
    void updateMnTimer();
    void updateNumConnections(int numConnections);
    void updateNetworkActive(bool networkActive);
    void updateAlert();
//    void updateAlert(const QString &hash, int status);
    void updateBanlist();
};

#endif // BITCOIN_QT_CLIENTMODEL_H
