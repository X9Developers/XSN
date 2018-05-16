// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemerchantnode.h"
#include "addrman.h"
#include "merchantnode-sync.h"
#include "merchantnodeman.h"
#include "merchantnode.h"
#include "netfulfilledman.h"
#include "net_processing.h"
#include "script/standard.h"
#include "messagesigner.h"
#include "util.h"
#include "init.h"

/** Merchantnode manager */
CMerchantnodeMan merchantnodeman;

const std::string CMerchantnodeMan::SERIALIZATION_VERSION_STRING = "CMerchantnodeMan-Version-7";

struct CompareByAddr

{
    bool operator()(const CMerchantnode* t1,
                    const CMerchantnode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

CMerchantnodeMan::CMerchantnodeMan()
    : cs(),
      mapMerchantnodes(),
      mAskedUsForMerchantnodeList(),
      mWeAskedForMerchantnodeList(),
      mWeAskedForMerchantnodeListEntry(),
      mWeAskedForVerification(),
      mMnbRecoveryRequests(),
      mMnbRecoveryGoodReplies(),
      listScheduledMnbRequestConnections(),
      nLastWatchdogVoteTime(0),
      mapSeenMerchantnodeBroadcast(),
      mapSeenMerchantnodePing(),
      nDsqCount(0)
{}

bool CMerchantnodeMan::Add(CMerchantnode &mn)
{
    LOCK(cs);

    if (Has(mn.pubKeyMerchantnode)) return false;

    LogPrint("merchantnode", "CMerchantnodeMan::Add -- Adding new Merchantnode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
    mapMerchantnodes[mn.pubKeyMerchantnode] = mn;

    return true;
}

void CMerchantnodeMan::AskForMN(CNode* pnode, const CPubKey &pubKeyMerchantnode, CConnman& connman)
{
    if(!pnode) return;

    LOCK(cs);

    auto it1 = mWeAskedForMerchantnodeListEntry.find(pubKeyMerchantnode);
    if (it1 != mWeAskedForMerchantnodeListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CMerchantnodeMan::AskForMN -- Asking same peer %s for missing merchantnode entry again: %s\n", pnode->addr.ToString(), HexStr(pubKeyMerchantnode.Raw()));
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CMerchantnodeMan::AskForMN -- Asking new peer %s for missing merchantnode entry: %s\n", pnode->addr.ToString(), HexStr(pubKeyMerchantnode.Raw()));
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CMerchantnodeMan::AskForMN -- Asking peer %s for missing merchantnode entry for the first time: %s\n", pnode->addr.ToString(), HexStr(pubKeyMerchantnode.Raw()));
    }
    mWeAskedForMerchantnodeListEntry[pubKeyMerchantnode][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    connman.PushMessage(pnode, NetMsgType::MERCHANTNODESEG, pubKeyMerchantnode);
}

bool CMerchantnodeMan::PoSeBan(const CPubKey &pubKeyMerchantnode)
{
    LOCK(cs);
    CMerchantnode* pmn = Find(pubKeyMerchantnode);
    if (!pmn) {
        return false;
    }
    pmn->PoSeBan();

    return true;
}

void CMerchantnodeMan::Check()
{
    LOCK(cs);

    LogPrint("merchantnode", "CMerchantnodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    for (auto& mnpair : mapMerchantnodes) {
        mnpair.second.Check();
    }
}

void CMerchantnodeMan::CheckAndRemove(CConnman& connman)
{
    if(!merchantnodeSync.IsMerchantnodeListSynced()) return;

    LogPrintf("CMerchantnodeMan::CheckAndRemove\n");
    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateMerchantnodeList()
        LOCK2(cs_main, cs);

        Check();



        // Remove spent merchantnodes, prepare structures and make requests to reasure the state of inactive ones
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES merchantnode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        auto it = mapMerchantnodes.begin();
        while (it != mapMerchantnodes.end()) {
            CMerchantnodeBroadcast mnb = CMerchantnodeBroadcast(it->second);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if (it->second.IsNewStartRequired()) {
                LogPrint("merchantnode", "CMerchantnodeMan::CheckAndRemove -- Removing Merchantnode: %s  addr=%s  %i now\n", it->second.GetStateString(), it->second.addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenMerchantnodeBroadcast.erase(hash);
                mWeAskedForMerchantnodeListEntry.erase(it->first);

                // and finally remove it from the list
                mapMerchantnodes.erase(it++);
            } else {
                bool fAsk = (nAskForMnbRecovery > 0) &&
                        merchantnodeSync.IsSynced() &&
                        !IsMnbRecoveryRequested(hash);
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for MERCHANTNODE_NEW_START_REQUIRED merchantnodes
        LogPrint("merchantnode", "CMerchantnodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CMerchantnodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("merchantnode", "CMerchantnodeMan::CheckAndRemove -- reprocessing mnb, merchantnode=%s\n", HexStr(itMnbReplies->second[0].pubKeyMerchantnode.Raw()));
                    // mapSeenMerchantnodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateMerchantnodeList(NULL, itMnbReplies->second[0], nDos, connman);
                }
                LogPrint("merchantnode", "CMerchantnodeMan::CheckAndRemove -- removing mnb recovery reply, merchantnode=%s, size=%d\n", HexStr(itMnbReplies->second[0].pubKeyMerchantnode.Raw()), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in MERCHANTNODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the Merchantnode list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMerchantnodeList.begin();
        while(it1 != mAskedUsForMerchantnodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForMerchantnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Merchantnode list
        it1 = mWeAskedForMerchantnodeList.begin();
        while(it1 != mWeAskedForMerchantnodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForMerchantnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Merchantnodes we've asked for
        auto it2 = mWeAskedForMerchantnodeListEntry.begin();
        while(it2 != mWeAskedForMerchantnodeListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForMerchantnodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CMerchantnodeVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenMerchantnodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenMerchantnodePing
        std::map<uint256, CMerchantnodePing>::iterator it4 = mapSeenMerchantnodePing.begin();
        while(it4 != mapSeenMerchantnodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("merchantnode", "CMerchantnodeMan::CheckAndRemove -- Removing expired Merchantnode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenMerchantnodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenMerchantnodeVerification
        std::map<uint256, CMerchantnodeVerification>::iterator itv2 = mapSeenMerchantnodeVerification.begin();
        while(itv2 != mapSeenMerchantnodeVerification.end()){
            if((*itv2).second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS){
                LogPrint("merchantnode", "CMerchantnodeMan::CheckAndRemove -- Removing expired Merchantnode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenMerchantnodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CMerchantnodeMan::CheckAndRemove -- %s\n", ToString());
    }
}

void CMerchantnodeMan::Clear()
{
    LOCK(cs);
    mapMerchantnodes.clear();
    mAskedUsForMerchantnodeList.clear();
    mWeAskedForMerchantnodeList.clear();
    mWeAskedForMerchantnodeListEntry.clear();
    mapSeenMerchantnodeBroadcast.clear();
    mapSeenMerchantnodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
}

int CMerchantnodeMan::CountMerchantnodes(int nProtocolVersion) const
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = PROTOCOL_VERSION;

    for (auto& mnpair : mapMerchantnodes) {
        if(mnpair.second.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CMerchantnodeMan::CountEnabled(int nProtocolVersion) const
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = PROTOCOL_VERSION;

    for (auto& mnpair : mapMerchantnodes) {
        if(mnpair.second.nProtocolVersion < nProtocolVersion || !mnpair.second.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 merchantnodes are allowed in 12.1, saving this for later
int CMerchantnodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    for (auto& mnpair : mapMerchantnodes)
        if ((nNetworkType == NET_IPV4 && mnpair.second.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mnpair.second.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mnpair.second.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CMerchantnodeMan::DsegUpdate(CNode* pnode, CConnman& connman)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMerchantnodeList.find(pnode->addr);
            if(it != mWeAskedForMerchantnodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CMerchantnodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }

    connman.PushMessage(pnode, NetMsgType::MERCHANTNODESEG, CPubKey());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForMerchantnodeList[pnode->addr] = askAgain;

    LogPrint("merchantnode", "CMerchantnodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CMerchantnode* CMerchantnodeMan::Find(const CPubKey &pubKeyMerchantnode)
{
    LOCK(cs);
    auto it = mapMerchantnodes.find(pubKeyMerchantnode);
    return it == mapMerchantnodes.end() ? NULL : &(it->second);
}

bool CMerchantnodeMan::Get(const CKeyID &pubKeyID, CMerchantnode& merchantnodeRet)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    for (auto& mnpair : mapMerchantnodes) {
        CKeyID keyID = mnpair.second.pubKeyMerchantnode.GetID();
        if (keyID == pubKeyID) {
            merchantnodeRet = mnpair.second;
            return true;
        }
    }
    return false;
}

bool CMerchantnodeMan::Get(const CPubKey &pubKeyMerchantnode, CMerchantnode &merchantnodeRet)
{
    LOCK(cs);
    auto it = mapMerchantnodes.find(pubKeyMerchantnode);
    if (it == mapMerchantnodes.end()) {
        return false;
    }

    merchantnodeRet = it->second;
    return true;
}

bool CMerchantnodeMan::GetMerchantnodeInfo(const CPubKey& pubKeyMerchantnode, merchantnode_info_t& mnInfoRet)
{
    LOCK(cs);
    auto it = mapMerchantnodes.find(pubKeyMerchantnode);
    if (it == mapMerchantnodes.end()) {
        return false;
    }
    mnInfoRet = it->second.GetInfo();
    return true;
}

bool CMerchantnodeMan::GetMerchantnodeInfo(const CKeyID &pubKeyMerchantnode, merchantnode_info_t &mnInfoRet)
{
    LOCK(cs);
    for (auto& mnpair : mapMerchantnodes) {
        CKeyID keyID = mnpair.second.pubKeyMerchantnode.GetID();
        if (keyID == pubKeyMerchantnode) {
            mnInfoRet = mnpair.second.GetInfo();
            return true;
        }
    }
    return false;
}

bool CMerchantnodeMan::GetMerchantnodeInfo(const CScript& payee, merchantnode_info_t& mnInfoRet)
{
    LOCK(cs);
    for (auto& mnpair : mapMerchantnodes) {
        CScript scriptCollateralAddress = GetScriptForDestination(mnpair.second.pubKeyMerchantnode.GetID());
        if (scriptCollateralAddress == payee) {
            mnInfoRet = mnpair.second.GetInfo();
            return true;
        }
    }
    return false;
}

bool CMerchantnodeMan::Has(const CPubKey &pubKeyMerchantnode)
{
    LOCK(cs);
    return mapMerchantnodes.find(pubKeyMerchantnode) != mapMerchantnodes.end();
}

void CMerchantnodeMan::ProcessMerchantnodeConnections(CConnman& connman)
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    connman.ForEachNode(CConnman::AllNodes, [](CNode* pnode) {
        if(pnode->fMerchantnode) {
            LogPrintf("Closing Merchantnode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    });
}

std::pair<CService, std::set<uint256> > CMerchantnodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CMerchantnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all XSN specific functionality

    if (strCommand == NetMsgType::MERCHANTNODEANNOUNCE) { //Merchantnode Broadcast

        CMerchantnodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        if(!merchantnodeSync.IsBlockchainSynced()) return;

        LogPrint("merchantnode", "MERCHANTNODEANNOUNCE -- Merchantnode announce, merchantnode=%s\n", HexStr(mnb.pubKeyMerchantnode.Raw()));

        int nDos = 0;

        if (CheckMnbAndUpdateMerchantnodeList(pfrom, mnb, nDos, connman)) {
            // use announced Merchantnode as a peer
            connman.AddNewAddress(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

    } else if (strCommand == NetMsgType::MERCHANTNODEPING) { //Merchantnode Ping

        CMerchantnodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        if(!merchantnodeSync.IsBlockchainSynced()) return;

        LogPrint("merchantnode", "MERCHANTNODEPING -- Merchantnode ping, merchantnode=%s\n", HexStr(mnp.merchantPubKey.Raw()));

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenMerchantnodePing.count(nHash)) return; //seen
        mapSeenMerchantnodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("merchantnode", "MERCHANTNODEPING -- Merchantnode ping, merchantnode=%s new\n", HexStr(mnp.merchantPubKey.Raw()));

        // see if we have this Merchantnode
        CMerchantnode* pmn = Find(mnp.merchantPubKey);

        // if merchantnode uses sentinel ping instead of watchdog
        // we shoud update nTimeLastWatchdogVote here if sentinel
        // ping flag is actual
        if(pmn && mnp.fSentinelIsCurrent)
            UpdateWatchdogVoteTime(mnp.merchantPubKey, mnp.sigTime);

        // too late, new MERCHANTNODEANNOUNCE is required
        if(pmn && pmn->IsExpired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos, connman)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a merchantnode entry once
        AskForMN(pfrom, mnp.merchantPubKey, connman);

    } else if (strCommand == NetMsgType::MERCHANTNODESEG) { //Get Merchantnode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after merchantnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!merchantnodeSync.IsSynced()) return;

        CPubKey pubKeyMerchantnode;
        vRecv >> pubKeyMerchantnode;

        LogPrint("merchantnode", "MERCHANTNODESEG -- Merchantnode list, merchantnode=%s\n", HexStr(pubKeyMerchantnode.Raw()));

        LOCK(cs);

        if(!pubKeyMerchantnode.IsValid()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator it = mAskedUsForMerchantnodeList.find(pfrom->addr);
                if (it != mAskedUsForMerchantnodeList.end() && it->second > GetTime()) {
                    Misbehaving(pfrom->GetId(), 34);
                    LogPrintf("MERCHANTNODESEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                    return;
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForMerchantnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        for (auto& mnpair : mapMerchantnodes) {
            if (pubKeyMerchantnode.IsValid() && pubKeyMerchantnode != mnpair.second.pubKeyMerchantnode) continue; // asked for specific vin but we are not there yet
            if (mnpair.second.addr.IsRFC1918() || mnpair.second.addr.IsLocal()) continue; // do not send local network merchantnode
            if (mnpair.second.IsUpdateRequired()) continue; // do not send outdated merchantnodes

            CMerchantnodeBroadcast mnb = CMerchantnodeBroadcast(mnpair.second);
            LogPrint("merchantnode", "MERCHANTNODESEG -- Sending Merchantnode entry: merchantnode=%s  addr=%s\n", HexStr(mnb.pubKeyMerchantnode.Raw()), mnb.addr.ToString());
            CMerchantnodePing mnp = mnpair.second.lastPing;
            uint256 hashMNB = mnb.GetHash();
            uint256 hashMNP = mnp.GetHash();
            pfrom->PushInventory(CInv(MSG_MERCHANTNODE_ANNOUNCE, hashMNB));
            pfrom->PushInventory(CInv(MSG_MERCHANTNODE_PING, hashMNP));
            nInvCount++;

            mapSeenMerchantnodeBroadcast.insert(std::make_pair(hashMNB, std::make_pair(GetTime(), mnb)));
            mapSeenMerchantnodePing.insert(std::make_pair(hashMNP, mnp));

            if (pubKeyMerchantnode == mnpair.first) {
                LogPrintf("MERCHANTNODESEG -- Sent 1 Merchantnode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(!pubKeyMerchantnode.IsValid()) {
            connman.PushMessage(pfrom, NetMsgType::MERCHANTSYNCSTATUSCOUNT, MERCHANTNODE_SYNC_LIST, nInvCount);
            LogPrintf("MERCHANTNODESEG -- Sent %d Merchantnode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("merchantnode", "MERCHANTNODESEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MERCHANTNODEVERIFY) { // Merchantnode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CMerchantnodeVerification mnv;
        vRecv >> mnv;

        pfrom->setAskFor.erase(mnv.GetHash());

        if(!merchantnodeSync.IsMerchantnodeListSynced()) return;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv, connman);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some merchantnode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some merchantnode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of merchantnodes via unique direct requests.

void CMerchantnodeMan::DoFullVerificationStep(CConnman& connman)
{
    if(!activeMerchantnode.pubKeyMerchantnode.IsValid()) return;
    if(!merchantnodeSync.IsSynced()) return;

#if 0
    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CMerchantnode> >::iterator it = vecMerchantnodeRanks.begin();
    while(it != vecMerchantnodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("merchantnode", "CMerchantnodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                     (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin.prevout == activeMerchantnode.outpoint) {
            nMyRank = it->first;
            LogPrint("merchantnode", "CMerchantnodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d merchantnodes\n",
                     nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this merchantnode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS merchantnodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecMerchantnodeRanks.size()) return;

    std::vector<CMerchantnode*> vSortedByAddr;
    for (auto& mnpair : mapMerchantnodes) {
        vSortedByAddr.push_back(&mnpair.second);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecMerchantnodeRanks.begin() + nOffset;
    while(it != vecMerchantnodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("merchantnode", "CMerchantnodeMan::DoFullVerificationStep -- Already %s%s%s merchantnode %s address %s, skipping...\n",
                     it->second.IsPoSeVerified() ? "verified" : "",
                     it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                     it->second.IsPoSeBanned() ? "banned" : "",
                     it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecMerchantnodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("merchantnode", "CMerchantnodeMan::DoFullVerificationStep -- Verifying merchantnode %s rank %d/%d address %s\n",
                 it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr, connman)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecMerchantnodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }


    LogPrint("merchantnode", "CMerchantnodeMan::DoFullVerificationStep -- Sent verification requests to %d merchantnodes\n", nCount);
#endif
}

// This function tries to find merchantnodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CMerchantnodeMan::CheckSameAddr()
{
    if(!merchantnodeSync.IsSynced() || mapMerchantnodes.empty()) return;

    std::vector<CMerchantnode*> vBan;
    std::vector<CMerchantnode*> vSortedByAddr;

    {
        LOCK(cs);

        CMerchantnode* pprevMerchantnode = NULL;
        CMerchantnode* pverifiedMerchantnode = NULL;

        for (auto& mnpair : mapMerchantnodes) {
            vSortedByAddr.push_back(&mnpair.second);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CMerchantnode* pmn, vSortedByAddr) {
            // check only (pre)enabled merchantnodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevMerchantnode) {
                pprevMerchantnode = pmn;
                pverifiedMerchantnode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevMerchantnode->addr) {
                if(pverifiedMerchantnode) {
                    // another merchantnode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this merchantnode with the same ip is verified, ban previous one
                    vBan.push_back(pprevMerchantnode);
                    // and keep a reference to be able to ban following merchantnodes with the same ip
                    pverifiedMerchantnode = pmn;
                }
            } else {
                pverifiedMerchantnode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevMerchantnode = pmn;
        }
    }

    // ban duplicates
    for(CMerchantnode* pmn : vBan) {
        LogPrintf("CMerchantnodeMan::CheckSameAddr -- increasing PoSe ban score for merchantnode %s\n", HexStr(pmn->pubKeyMerchantnode.Raw()));
        pmn->IncreasePoSeBanScore();
    }
}

bool CMerchantnodeMan::SendVerifyRequest(const CAddress& addr, const std::vector<CMerchantnode*>& vSortedByAddr, CConnman& connman)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MERCHANTNODEVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("merchantnode", "CMerchantnodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = connman.ConnectNode(addr, NULL, true);
    if(pnode == NULL) {
        LogPrintf("CMerchantnodeMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    netfulfilledman.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MERCHANTNODEVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CMerchantnodeVerification mnv(addr, GetRandInt(999999), nCachedBlockHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogPrintf("CMerchantnodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    connman.PushMessage(pnode, NetMsgType::MERCHANTNODEVERIFY, mnv);

    return true;
}

void CMerchantnodeMan::SendVerifyReply(CNode* pnode, CMerchantnodeVerification& mnv, CConnman& connman)
{
    // only merchantnodes can sign this, why would someone ask regular node?
    if(!fMerchantNode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MERCHANTNODEVERIFY)+"-reply")) {
        // peer should not ask us that often
        LogPrintf("MerchantnodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("MerchantnodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeMerchantnode.service.ToString(false), mnv.nonce, blockHash.ToString());

    if(!CMessageSigner::SignMessage(strMessage, mnv.vchSig1, activeMerchantnode.keyMerchantnode)) {
        LogPrintf("MerchantnodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!CMessageSigner::VerifyMessage(activeMerchantnode.pubKeyMerchantnode, mnv.vchSig1, strMessage, strError)) {
        LogPrintf("MerchantnodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    connman.PushMessage(pnode, NetMsgType::MERCHANTNODEVERIFY, mnv);
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MERCHANTNODEVERIFY)+"-reply");
}

void CMerchantnodeMan::ProcessVerifyReply(CNode* pnode, CMerchantnodeVerification& mnv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MERCHANTNODEVERIFY)+"-request")) {
        LogPrintf("CMerchantnodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CMerchantnodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                  mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CMerchantnodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                  mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("MerchantnodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MERCHANTNODEVERIFY)+"-done")) {
        LogPrintf("CMerchantnodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CMerchantnode* prealMerchantnode = NULL;
        std::vector<CMerchantnode*> vpMerchantnodesToBan;
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(false), mnv.nonce, blockHash.ToString());
        for (auto& mnpair : mapMerchantnodes) {
            if(CAddress(mnpair.second.addr, NODE_NETWORK) == pnode->addr) {
                if(CMessageSigner::VerifyMessage(mnpair.second.pubKeyMerchantnode, mnv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealMerchantnode = &mnpair.second;
                    if(!mnpair.second.IsPoSeVerified()) {
                        mnpair.second.DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MERCHANTNODEVERIFY)+"-done");

                    // we can only broadcast it if we are an activated merchantnode
                    if(!activeMerchantnode.pubKeyMerchantnode.IsValid()) continue;
                    // update ...
                    mnv.addr = mnpair.second.addr;
                    mnv.pubKeyMerchantnode1 = mnpair.second.pubKeyMerchantnode;
                    mnv.pubKeyMerchantnode2 = activeMerchantnode.pubKeyMerchantnode;
                    std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                                        HexStr(mnv.pubKeyMerchantnode1.Raw()), HexStr(mnv.pubKeyMerchantnode2.Raw()));
                    // ... and sign it
                    if(!CMessageSigner::SignMessage(strMessage2, mnv.vchSig2, activeMerchantnode.keyMerchantnode)) {
                        LogPrintf("MerchantnodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!CMessageSigner::VerifyMessage(activeMerchantnode.pubKeyMerchantnode, mnv.vchSig2, strMessage2, strError)) {
                        LogPrintf("MerchantnodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mapSeenMerchantnodeVerification.insert(std::make_pair(mnv.GetHash(), mnv));
                    mnv.Relay();

                } else {
                    vpMerchantnodesToBan.push_back(&mnpair.second);
                }
            }
        }
        // no real merchantnode found?...
        if(!prealMerchantnode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CMerchantnodeMan::ProcessVerifyReply -- ERROR: no real merchantnode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CMerchantnodeMan::ProcessVerifyReply -- verified real merchantnode %s for addr %s\n",
                  HexStr(prealMerchantnode->pubKeyMerchantnode.Raw()), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CMerchantnode* pmn, vpMerchantnodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("merchantnode", "CMerchantnodeMan::ProcessVerifyReply -- increased PoSe ban score for %s addr %s, new score %d\n",
                     HexStr(prealMerchantnode->pubKeyMerchantnode.Raw()), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        if(!vpMerchantnodesToBan.empty())
            LogPrintf("CMerchantnodeMan::ProcessVerifyReply -- PoSe score increased for %d fake merchantnodes, addr %s\n",
                      (int)vpMerchantnodesToBan.size(), pnode->addr.ToString());
    }
}

void CMerchantnodeMan::ProcessVerifyBroadcast(CNode* pnode, const CMerchantnodeVerification& mnv)
{
    std::string strError;

    if(mapSeenMerchantnodeVerification.find(mnv.GetHash()) != mapSeenMerchantnodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenMerchantnodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS) {
        LogPrint("merchantnode", "CMerchantnodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                 nCachedBlockHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.pubKeyMerchantnode1 == mnv.pubKeyMerchantnode2) {
        LogPrint("merchantnode", "CMerchantnodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                 HexStr(mnv.pubKeyMerchantnode1.Raw()), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank;

#if 0
    if (!GetMerchantnodeRank(mnv.vin2.prevout, nRank, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION)) {
        LogPrint("merchantnode", "CMerchantnodeMan::ProcessVerifyBroadcast -- Can't calculate rank for merchantnode %s\n",
                 mnv.vin2.prevout.ToStringShort());
        return;
    }
#endif

    if(nRank > MAX_POSE_RANK) {
        LogPrint("merchantnode", "CMerchantnodeMan::ProcessVerifyBroadcast -- Merchantnode %s is not in top %d, current rank %d, peer=%d\n",
                 HexStr(mnv.pubKeyMerchantnode2.Raw()), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(false), mnv.nonce, blockHash.ToString(),
                                            HexStr(mnv.pubKeyMerchantnode1.Raw()), HexStr(mnv.pubKeyMerchantnode2.Raw()));

        CMerchantnode* pmn1 = Find(mnv.pubKeyMerchantnode1);
        if(!pmn1) {
            LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- can't find merchantnode1 %s\n", HexStr(mnv.pubKeyMerchantnode1.Raw()));
            return;
        }

        CMerchantnode* pmn2 = Find(mnv.pubKeyMerchantnode2);
        if(!pmn2) {
            LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- can't find merchantnode2 %s\n", HexStr(mnv.pubKeyMerchantnode2.Raw()));
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- addr %s does not match %s\n", mnv.addr.ToString(), pmn1->addr.ToString());
            return;
        }

        if(!CMessageSigner::VerifyMessage(pmn1->pubKeyMerchantnode, mnv.vchSig1, strMessage1, strError)) {
            LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for merchantnode1 failed, error: %s\n", strError);
            return;
        }

        if(!CMessageSigner::VerifyMessage(pmn2->pubKeyMerchantnode, mnv.vchSig2, strMessage2, strError)) {
            LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for merchantnode2 failed, error: %s\n", strError);
            return;
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- verified merchantnode %s for addr %s\n",
                  HexStr(pmn1->pubKeyMerchantnode.Raw()), pmn1->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        for (auto& mnpair : mapMerchantnodes) {
            if(mnpair.second.addr != mnv.addr || mnpair.first == mnv.pubKeyMerchantnode1) continue;
            mnpair.second.IncreasePoSeBanScore();
            nCount++;
            LogPrint("merchantnode", "CMerchantnodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                     HexStr(mnpair.first.Raw()), mnpair.second.addr.ToString(), mnpair.second.nPoSeBanScore);
        }
        if(nCount)
            LogPrintf("CMerchantnodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake merchantnodes, addr %s\n",
                      nCount, pmn1->addr.ToString());
    }
}

std::string CMerchantnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Merchantnodes: " << (int)mapMerchantnodes.size() <<
            ", peers who asked us for Merchantnode list: " << (int)mAskedUsForMerchantnodeList.size() <<
            ", peers we asked for Merchantnode list: " << (int)mWeAskedForMerchantnodeList.size() <<
            ", entries in Merchantnode list we asked for: " << (int)mWeAskedForMerchantnodeListEntry.size() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CMerchantnodeMan::UpdateMerchantnodeList(CMerchantnodeBroadcast mnb, CConnman& connman)
{
    LOCK2(cs_main, cs);
    mapSeenMerchantnodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenMerchantnodeBroadcast.insert(std::make_pair(mnb.GetHash(), std::make_pair(GetTime(), mnb)));

    LogPrintf("CMerchantnodeMan::UpdateMerchantnodeList -- merchantnode=%s  addr=%s\n", HexStr(mnb.pubKeyMerchantnode.Raw()), mnb.addr.ToString());

    CMerchantnode* pmn = Find(mnb.pubKeyMerchantnode);
    if(pmn == NULL) {

        if(Add(mnb)) {
            merchantnodeSync.BumpAssetLastTime("CMerchantnodeMan::UpdateMerchantnodeList - new");
        }
    } else {
        CMerchantnodeBroadcast mnbOld = mapSeenMerchantnodeBroadcast[CMerchantnodeBroadcast(*pmn).GetHash()].second;
        if(pmn->UpdateFromNewBroadcast(mnb, connman)) {
            merchantnodeSync.BumpAssetLastTime("CMerchantnodeMan::UpdateMerchantnodeList - seen");
            mapSeenMerchantnodeBroadcast.erase(mnbOld.GetHash());
        }
    }
}

bool CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList(CNode* pfrom, CMerchantnodeBroadcast mnb, int& nDos, CConnman& connman)
{
    // Need to lock cs_main here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- merchantnode=%s\n", HexStr(mnb.pubKeyMerchantnode.Raw()));

        uint256 hash = mnb.GetHash();
        if(mapSeenMerchantnodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
            LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- merchantnode=%s seen\n", HexStr(mnb.pubKeyMerchantnode.Raw()));
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if(GetTime() - mapSeenMerchantnodeBroadcast[hash].first > MERCHANTNODE_NEW_START_REQUIRED_SECONDS - MERCHANTNODE_MIN_MNP_SECONDS * 2) {
                LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- merchantnode=%s seen update\n", HexStr(mnb.pubKeyMerchantnode.Raw()));
                mapSeenMerchantnodeBroadcast[hash].first = GetTime();
                merchantnodeSync.BumpAssetLastTime("CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList - seen");
            }
            // did we ask this node for it?
            if(pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
                LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- mnb=%s seen request\n", hash.ToString());
                if(mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if(mnb.lastPing.sigTime > mapSeenMerchantnodeBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CMerchantnode mnTemp = CMerchantnode(mnb);
                        mnTemp.Check();
                        LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetAdjustedTime() - mnb.lastPing.sigTime)/60, mnTemp.GetStateString());
                        if(mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- merchantnode=%s seen good\n", HexStr(mnb.pubKeyMerchantnode.Raw()));
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenMerchantnodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

        LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- merchantnode=%s new\n", HexStr(mnb.pubKeyMerchantnode.Raw()));

        if(!mnb.SimpleCheck(nDos)) {
            LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- SimpleCheck() failed, merchantnode=%s\n", HexStr(mnb.pubKeyMerchantnode.Raw()));
            return false;
        }

        // search Merchantnode list
        CMerchantnode* pmn = Find(mnb.pubKeyMerchantnode);
        if(pmn) {
            CMerchantnodeBroadcast mnbOld = mapSeenMerchantnodeBroadcast[CMerchantnodeBroadcast(*pmn).GetHash()].second;
            if(!mnb.Update(pmn, nDos, connman)) {
                LogPrint("merchantnode", "CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- Update() failed, merchantnode=%s\n", HexStr(mnb.pubKeyMerchantnode.Raw()));
                return false;
            }
            if(hash != mnbOld.GetHash()) {
                mapSeenMerchantnodeBroadcast.erase(mnbOld.GetHash());
            }
            return true;
        }
    }

    if(mnb.CheckMerchantnode(nDos)) {

        Add(mnb);
        merchantnodeSync.BumpAssetLastTime("CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList - new");
        // if it matches our Merchantnode privkey...
        if(fMerchantNode && mnb.pubKeyMerchantnode == activeMerchantnode.pubKeyMerchantnode) {
            mnb.nPoSeBanScore = -MERCHANTNODE_POSE_BAN_MAX_SCORE;
            if(mnb.nProtocolVersion == PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- Got NEW Merchantnode entry: merchantnode=%s  sigTime=%lld  addr=%s\n",
                          HexStr(mnb.pubKeyMerchantnode.Raw()), mnb.sigTime, mnb.addr.ToString());
                activeMerchantnode.ManageState(connman);
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.Relay(connman);
    } else {
        LogPrintf("CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList -- Rejected Merchantnode entry: %s  addr=%s\n", HexStr(mnb.pubKeyMerchantnode.Raw()), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CMerchantnodeMan::UpdateWatchdogVoteTime(const CPubKey &pubKeyMerchantnode, uint64_t nVoteTime)
{
    LOCK(cs);
    CMerchantnode* pmn = Find(pubKeyMerchantnode);
    if(!pmn) {
        return;
    }
    pmn->UpdateWatchdogVoteTime(nVoteTime);
    nLastWatchdogVoteTime = GetTime();
}

bool CMerchantnodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any merchantnodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= MERCHANTNODE_WATCHDOG_MAX_SECONDS;
}

void CMerchantnodeMan::CheckMerchantnode(const CPubKey& pubKeyMerchantnode, bool fForce)
{
    LOCK(cs);
    for (auto& mnpair : mapMerchantnodes) {
        if (mnpair.second.pubKeyMerchantnode == pubKeyMerchantnode) {
            mnpair.second.Check(fForce);
            return;
        }
    }
}

bool CMerchantnodeMan::IsMerchantnodePingedWithin(const CPubKey &pubKeyMerchantnode, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CMerchantnode* pmn = Find(pubKeyMerchantnode);
    return pmn ? pmn->IsPingedWithin(nSeconds, nTimeToCheckAt) : false;
}

void CMerchantnodeMan::SetMerchantnodeLastPing(const CPubKey &pubKeyMerchantnode, const CMerchantnodePing& mnp)
{
    LOCK(cs);
    CMerchantnode* pmn = Find(pubKeyMerchantnode);
    if(!pmn) {
        return;
    }
    pmn->lastPing = mnp;
    // if merchantnode uses sentinel ping instead of watchdog
    // we shoud update nTimeLastWatchdogVote here if sentinel
    // ping flag is actual
    if(mnp.fSentinelIsCurrent) {
        UpdateWatchdogVoteTime(mnp.merchantPubKey, mnp.sigTime);
    }
    mapSeenMerchantnodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CMerchantnodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if(mapSeenMerchantnodeBroadcast.count(hash)) {
        mapSeenMerchantnodeBroadcast[hash].second.lastPing = mnp;
    }
}

void CMerchantnodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    nCachedBlockHeight = pindex->nHeight;
    LogPrint("merchantnode", "CMerchantnodeMan::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    CheckSameAddr();
}

void ThreadMerchantnodeCheck(CConnman &connman)
{
    if(fLiteMode) return; // disable all XSN specific functionality

    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    RenameThread("xsn-tpos");

    unsigned int nTick = 0;

    while (true)
    {
        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        merchantnodeSync.ProcessTick(connman);

        if(merchantnodeSync.IsBlockchainSynced() && !ShutdownRequested()) {

            nTick++;

            // make sure to check all merchantnodes first
            merchantnodeman.Check();

            // check if we should activate or ping every few minutes,
            // slightly postpone first run to give net thread a chance to connect to some peers
            if(nTick % MERCHANTNODE_MIN_MNP_SECONDS == 15)
                activeMerchantnode.ManageState(connman);

            if(nTick % 60 == 0) {
                merchantnodeman.ProcessMerchantnodeConnections(connman);
                merchantnodeman.CheckAndRemove(connman);
            }
            if(fMerchantNode && (nTick % (60 * 5) == 0)) {
                merchantnodeman.DoFullVerificationStep(connman);
            }
        }
    }

}
