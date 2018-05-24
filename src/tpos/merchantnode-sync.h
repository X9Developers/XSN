// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MERCHANTNODE_SYNC_H
#define MERCHANTNODE_SYNC_H

#include <chain.h>
#include <net.h>

class CMerchantnodeSync;

static const int MERCHANTNODE_SYNC_FAILED          = -1;
static const int MERCHANTNODE_SYNC_INITIAL         = 0; // sync just started, was reset recently or still in IDB
static const int MERCHANTNODE_SYNC_WAITING         = 1; // waiting after initial to see if we can get more headers/blocks
static const int MERCHANTNODE_SYNC_LIST            = 2;
static const int MERCHANTNODE_SYNC_FINISHED        = 999;

static const int MERCHANTNODE_SYNC_TICK_SECONDS    = 6;
static const int MERCHANTNODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int MERCHANTNODE_SYNC_ENOUGH_PEERS    = 6;

extern CMerchantnodeSync merchantnodeSync;

//
// CMerchantnodeSync : Sync masternode assets in stages
//

class CMerchantnodeSync
{
private:
    // Keep track of current asset
    int nRequestedMerchantnodeAssets;
    // Count peers we've requested the asset from
    int nRequestedMerchantnodeAttempt;

    // Time when current masternode asset sync started
    int64_t nTimeAssetSyncStarted;
    // ... last bumped
    int64_t nTimeLastBumped;
    // ... or failed
    int64_t nTimeLastFailure;

    void Fail();
    void ClearFulfilledRequests(CConnman& connman);

public:
    CMerchantnodeSync() { Reset(); }


    void SendGovernanceSyncRequest(CNode* pnode, CConnman& connman);

    bool IsFailed() { return nRequestedMerchantnodeAssets == MERCHANTNODE_SYNC_FAILED; }
    bool IsBlockchainSynced() { return nRequestedMerchantnodeAssets > MERCHANTNODE_SYNC_WAITING; }
    bool IsMerchantnodeListSynced() { return nRequestedMerchantnodeAssets > MERCHANTNODE_SYNC_LIST; }
    bool IsSynced() { return nRequestedMerchantnodeAssets == MERCHANTNODE_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedMerchantnodeAssets; }
    int GetAttempt() { return nRequestedMerchantnodeAttempt; }
    void BumpAssetLastTime(std::string strFuncName);
    int64_t GetAssetStartTime() { return nTimeAssetSyncStarted; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset(CConnman& connman);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick(CConnman& connman);

    void AcceptedBlockHeader(const CBlockIndex *pindexNew);
    void NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman);
    void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman);
};

#endif
