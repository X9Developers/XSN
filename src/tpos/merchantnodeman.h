// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERCHANTNODEMAN_H
#define MERCHANTNODEMAN_H

#include "merchantnode.h"
#include "sync.h"

using namespace std;

class CMerchantnodeMan;
class CConnman;

extern CMerchantnodeMan merchantnodeman;

class CMerchantnodeMan
{
private:
    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block height
    int nCachedBlockHeight;

    // map to hold all MNs
    std::map<CPubKey, CMerchantnode> mapMerchantnodes;
    // who's asked for the Merchantnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMerchantnodeList;
    // who we asked for the Merchantnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMerchantnodeList;
    // which Merchantnodes we've asked for
    std::map<CPubKey, std::map<CNetAddr, int64_t> > mWeAskedForMerchantnodeListEntry;
    // who we asked for the masternode verification
    std::map<CNetAddr, CMerchantnodeVerification> mWeAskedForVerification;

    // these maps are used for masternode recovery from MASTERNODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CMerchantnodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastWatchdogVoteTime;

    friend class CMerchantnodeSync;
    /// Find an entry
    CMerchantnode* Find(const CPubKey &pubKeyMerchantnode);
public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CMerchantnodeBroadcast> > mapSeenMerchantnodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CMerchantnodePing> mapSeenMerchantnodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CMerchantnodeVerification> mapSeenMerchantnodeVerification;
    // keep track of dsq count to prevent masternodes from gaming darksend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(mapMerchantnodes);
        READWRITE(mAskedUsForMerchantnodeList);
        READWRITE(mWeAskedForMerchantnodeList);
        READWRITE(mWeAskedForMerchantnodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenMerchantnodeBroadcast);
        READWRITE(mapSeenMerchantnodePing);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CMerchantnodeMan();

    /// Add an entry
    bool Add(CMerchantnode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CPubKey &pubKeyMerchantnode, CConnman& connman);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    bool PoSeBan(const CPubKey &pubKeyMerchantnode);

    /// Check all Merchantnodes
    void Check();

    /// Check all Merchantnodes and remove inactive
    void CheckAndRemove(CConnman& connman);
    /// This is dummy overload to be used for dumping/loading mncache.dat
    void CheckAndRemove() {}

    /// Clear Merchantnode vector
    void Clear();

    /// Count Merchantnodes filtered by nProtocolVersion.
    /// Merchantnode nProtocolVersion should match or be above the one specified in param here.
    int CountMerchantnodes(int nProtocolVersion = -1) const;
    /// Count enabled Merchantnodes filtered by nProtocolVersion.
    /// Merchantnode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1) const;

    /// Count Merchantnodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode, CConnman& connman);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey &pubKeyMerchantnode, CMerchantnode& masternodeRet);
    bool Has(const CPubKey &pubKeyMerchantnode);

    bool GetMerchantnodeInfo(const CPubKey& pubKeyMerchantnode, merchantnode_info_t& mnInfoRet);
    bool GetMerchantnodeInfo(const CKeyID& pubKeyMerchantnode, merchantnode_info_t& mnInfoRet);
    bool GetMerchantnodeInfo(const CScript& payee, merchantnode_info_t& mnInfoRet);

    std::map<CPubKey, CMerchantnode> GetFullMerchantnodeMap() { return mapMerchantnodes; }

    void ProcessMerchantnodeConnections(CConnman& connman);
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    void DoFullVerificationStep(CConnman& connman);
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CMerchantnode*>& vSortedByAddr, CConnman& connman);
    void SendVerifyReply(CNode* pnode, CMerchantnodeVerification& mnv, CConnman& connman);
    void ProcessVerifyReply(CNode* pnode, CMerchantnodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CMerchantnodeVerification& mnv);

    /// Return the number of (unique) Merchantnodes
    int size() { return mapMerchantnodes.size(); }

    std::string ToString() const;

    /// Update masternode list and maps using provided CMerchantnodeBroadcast
    void UpdateMerchantnodeList(CMerchantnodeBroadcast mnb, CConnman& connman);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateMerchantnodeList(CNode* pfrom, CMerchantnodeBroadcast mnb, int& nDos, CConnman& connman);
    bool CheckMnbIPAddressAndRemoveDuplicatedEntry(CMerchantnodeBroadcast mnb, int &nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CPubKey &pubKeyMerchantnode, uint64_t nVoteTime = 0);

    void CheckMerchantnode(const CPubKey& pubKeyMerchantnode, bool fForce);

    bool IsMerchantnodePingedWithin(const CPubKey &pubKeyMerchantnode, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetMerchantnodeLastPing(const CPubKey &pubKeyMerchantnode, const CMerchantnodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

void ThreadMerchantnodeCheck(CConnman& connman);

#endif
