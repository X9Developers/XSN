// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tpos/activemerchantnode.h>
#include <checkpoints.h>
#if 0
#include <governance.h>
#endif
#include <validation.h>
#include <tpos/merchantnode-sync.h>
#include <tpos/merchantnode.h>
#include <tpos/merchantnodeman.h>
#include <netfulfilledman.h>
#include <spork.h>
#include <ui_interface.h>
#include <util.h>

class CMerchantnodeSync;
CMerchantnodeSync merchantnodeSync;

void CMerchantnodeSync::Fail()
{
    nTimeLastFailure = GetTime();
    nRequestedMerchantnodeAssets = MERCHANTNODE_SYNC_FAILED;
}

void CMerchantnodeSync::Reset()
{
    nRequestedMerchantnodeAssets = MERCHANTNODE_SYNC_INITIAL;
    nRequestedMerchantnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastBumped = GetTime();
    nTimeLastFailure = 0;
}

void CMerchantnodeSync::BumpAssetLastTime(std::string strFuncName)
{
    if(IsSynced() || IsFailed()) return;
    nTimeLastBumped = GetTime();
    LogPrint(BCLog::MNSYNC, "CMerchantnodeSync::BumpAssetLastTime -- %s\n", strFuncName);
}

std::string CMerchantnodeSync::GetAssetName()
{
    switch(nRequestedMerchantnodeAssets)
    {
        case(MERCHANTNODE_SYNC_INITIAL):      return "MERCHANTNODE_SYNC_INITIAL";
        case(MERCHANTNODE_SYNC_WAITING):      return "MERCHANTNODE_SYNC_WAITING";
        case(MERCHANTNODE_SYNC_LIST):         return "MERCHANTNODE_SYNC_LIST";
        case(MERCHANTNODE_SYNC_FAILED):       return "MERCHANTNODE_SYNC_FAILED";
        case MERCHANTNODE_SYNC_FINISHED:      return "MERCHANTNODE_SYNC_FINISHED";
        default:                            return "UNKNOWN";
    }
}

void CMerchantnodeSync::SwitchToNextAsset(CConnman& connman)
{
    switch(nRequestedMerchantnodeAssets)
    {
        case(MERCHANTNODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(MERCHANTNODE_SYNC_INITIAL):
            ClearFulfilledRequests(connman);
            nRequestedMerchantnodeAssets = MERCHANTNODE_SYNC_WAITING;
            LogPrintf("CMerchantnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(MERCHANTNODE_SYNC_WAITING):
            ClearFulfilledRequests(connman);
            LogPrintf("CMerchantnodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nRequestedMerchantnodeAssets = MERCHANTNODE_SYNC_LIST;
            LogPrintf("CMerchantnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(MERCHANTNODE_SYNC_LIST):
            LogPrintf("CMerchantnodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nRequestedMerchantnodeAssets = MERCHANTNODE_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
            //try to activate our masternode if possible
            activeMerchantnode.ManageState(connman);

            // TODO: Find out whether we can just use LOCK instead of:
            // TRY_LOCK(cs_vNodes, lockRecv);
            // if(lockRecv) { ... }

            connman.ForEachNode([](CNode* pnode) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-mrnsync");
            });
            LogPrintf("CMerchantnodeSync::SwitchToNextAsset -- Sync has finished\n");

            break;
    }
    nRequestedMerchantnodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    BumpAssetLastTime("CMerchantnodeSync::SwitchToNextAsset");
}

std::string CMerchantnodeSync::GetSyncStatus()
{
    switch (merchantnodeSync.nRequestedMerchantnodeAssets) {
        case MERCHANTNODE_SYNC_INITIAL:       return _("Synchroning blockchain...");
        case MERCHANTNODE_SYNC_WAITING:       return _("Synchronization pending...");
        case MERCHANTNODE_SYNC_LIST:          return _("Synchronizing masternodes...");
        case MERCHANTNODE_SYNC_FAILED:        return _("Synchronization failed");
        case MERCHANTNODE_SYNC_FINISHED:      return _("Synchronization finished");
        default:                            return "";
    }
}

void CMerchantnodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::MERCHANTSYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if(IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("MERCHANTSYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->GetId());
    }
}

void CMerchantnodeSync::ClearFulfilledRequests(CConnman& connman)
{
    // TODO: Find out whether we can just use LOCK instead of:
    // TRY_LOCK(cs_vNodes, lockRecv);
    // if(!lockRecv) return;

    connman.ForEachNode([](CNode* pnode) {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "merchantnode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-mrnsync");
    });
}

void CMerchantnodeSync::ProcessTick(CConnman& connman)
{
    static int nTick = 0;
    if(nTick++ % MERCHANTNODE_SYNC_TICK_SECONDS != 0) return;

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    static int64_t nTimeLastProcess = GetTime();
    if(GetTime() - nTimeLastProcess > 60*60) {
        LogPrintf("CMerchantnodeSync::HasSyncFailures -- WARNING: no actions for too long, restarting sync...\n");
        Reset();
        SwitchToNextAsset(connman);
        nTimeLastProcess = GetTime();
        return;
    }
    nTimeLastProcess = GetTime();

    // reset sync status in case of any other sync failure
    if(IsFailed()) {
        if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
            LogPrintf("CMerchantnodeSync::HasSyncFailures -- WARNING: failed to sync, trying again...\n");
            Reset();
            SwitchToNextAsset(connman);
        }
        return;
    }

    // gradually request the rest of the votes after sync finished
    if(IsSynced()) {
        std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();
        governance.RequestGovernanceObjectVotes(vNodesCopy, connman);
        connman.ReleaseNodeVector(vNodesCopy);
        return;
    }

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(nRequestedMerchantnodeAttempt + (nRequestedMerchantnodeAssets - 1) * 8) / (8*4);
    LogPrintf("CMerchantnodeSync::ProcessTick -- nTick %d nRequestedMerchantnodeAssets %d nRequestedMerchantnodeAttempt %d nSyncProgress %f\n", nTick, nRequestedMerchantnodeAssets, nRequestedMerchantnodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector();

    for(CNode* pnode : vNodesCopy)
    {
        // Don't try to sync any data from outbound "merchantnode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "merchantnode" connection
        // initiated from another node, so skip it too.
        if(pnode->fMerchantnode || (fMerchantNode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
#if 0
        if(Params().NetworkIDString() == CBaseChainParams::REGTEST)
        {
            if(nRequestedMerchantnodeAttempt <= 2) {
                connman.PushMessageWithVersion(pnode, INIT_PROTO_VERSION, NetMsgType::GETSPORKS); //get current network sporks
            } else if(nRequestedMerchantnodeAttempt < 4) {
                merchantnodeman.DsegUpdate(pnode, connman);
            } else if(nRequestedMerchantnodeAttempt < 6) {
                int nMnCount = merchantnodeman.CountMerchantnodes();
                connman.PushMessage(pnode, NetMsgType::MERCHANTNODEPAYMENTSYNC, nMnCount); //sync payment votes
                SendGovernanceSyncRequest(pnode, connman);
            } else {
                nRequestedMerchantnodeAssets = MERCHANTNODE_SYNC_FINISHED;
            }
            nRequestedMerchantnodeAttempt++;
            connman.ReleaseNodeVector(vNodesCopy);
            return;
        }
#endif

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-mrnsync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CMerchantnodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->GetId());
                continue;
            }

            // INITIAL TIMEOUT

            if(nRequestedMerchantnodeAssets == MERCHANTNODE_SYNC_WAITING) {
                if(GetTime() - nTimeLastBumped > MERCHANTNODE_SYNC_TIMEOUT_SECONDS) {
                    // At this point we know that:
                    // a) there are peers (because we are looping on at least one of them);
                    // b) we waited for at least MERCHANTNODE_SYNC_TIMEOUT_SECONDS since we reached
                    //    the headers tip the last time (i.e. since we switched from
                    //     MERCHANTNODE_SYNC_INITIAL to MERCHANTNODE_SYNC_WAITING and bumped time);
                    // c) there were no blocks (UpdatedBlockTip, NotifyHeaderTip) or headers (AcceptedBlockHeader)
                    //    for at least MERCHANTNODE_SYNC_TIMEOUT_SECONDS.
                    // We must be at the tip already, let's move to the next asset.
                    SwitchToNextAsset(connman);
                }
            }

            // MNLIST : SYNC MERCHANTNODE LIST FROM OTHER CONNECTED CLIENTS

            if(nRequestedMerchantnodeAssets == MERCHANTNODE_SYNC_LIST) {
                LogPrint(BCLog::MERCHANTNODE, "CMerchantnodeSync::ProcessTick -- nTick %d nRequestedMerchantnodeAssets %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, nRequestedMerchantnodeAssets, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if(GetTime() - nTimeLastBumped > MERCHANTNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CMerchantnodeSync::ProcessTick -- nTick %d nRequestedMerchantnodeAssets %d -- timeout\n", nTick, nRequestedMerchantnodeAssets);
                    if (nRequestedMerchantnodeAttempt == 0) {
                        LogPrintf("CMerchantnodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without masternode list, fail here and try later
                        Fail();
                        connman.ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "merchantnode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "merchantnode-list-sync");

                if (pnode->nVersion < PROTOCOL_VERSION) continue;
                nRequestedMerchantnodeAttempt++;

                merchantnodeman.DsegUpdate(pnode, connman);

                connman.ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }
    // looped through all nodes, release them
    connman.ReleaseNodeVector(vNodesCopy);
}


void CMerchantnodeSync::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    LogPrint(BCLog::MNSYNC, "CMerchantnodeSync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we are still syncing blockchain
        BumpAssetLastTime("CMerchantnodeSync::AcceptedBlockHeader");
    }
}

void CMerchantnodeSync::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint(BCLog::MNSYNC, "CMerchantnodeSync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CMerchantnodeSync::NotifyHeaderTip");
    }
}

void CMerchantnodeSync::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint(BCLog::MNSYNC, "CMerchantnodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CMerchantnodeSync::UpdatedBlockTip");
    }

    if (fInitialDownload) {
        // switched too early
        if (IsBlockchainSynced()) {
            Reset();
        }

        // no need to check any further while still in IBD mode
        return;
    }

    // Note: since we sync headers first, it should be ok to use this
    static bool fReachedBestHeader = false;
    bool fReachedBestHeaderNew = pindexNew->GetBlockHash() == pindexBestHeader->GetBlockHash();

    if (fReachedBestHeader && !fReachedBestHeaderNew) {
        // Switching from true to false means that we previousely stuck syncing headers for some reason,
        // probably initial timeout was not enough,
        // because there is no way we can update tip not having best header
        Reset();
        fReachedBestHeader = false;
        return;
    }

    fReachedBestHeader = fReachedBestHeaderNew;

    LogPrint(BCLog::MNSYNC, "CMerchantnodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d pindexBestHeader->nHeight: %d fInitialDownload=%d fReachedBestHeader=%d\n",
                pindexNew->nHeight, pindexBestHeader->nHeight, fInitialDownload, fReachedBestHeader);

    if (!IsBlockchainSynced() && fReachedBestHeader) {
        // Reached best header while being in initial mode.
        // We must be at the tip already, let's move to the next asset.
        SwitchToNextAsset(connman);
    }
}
