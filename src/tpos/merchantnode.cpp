// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemerchantnode.h"
#include "base58.h"
#include "init.h"
#include "netbase.h"
#include "merchantnode.h"
#include "merchantnodeman.h"
#include "merchantnode-sync.h"
#include "messagesigner.h"
#include "script/standard.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include <boost/lexical_cast.hpp>


CMerchantnode::CMerchantnode() :
    merchantnode_info_t{ MERCHANTNODE_ENABLED, PROTOCOL_VERSION, GetAdjustedTime()}
{}

CMerchantnode::CMerchantnode(CService addr, CPubKey pubKeyMerchantnode, uint256 hashTPoSContractTxNew, int nProtocolVersionIn) :
    merchantnode_info_t{ MERCHANTNODE_ENABLED, nProtocolVersionIn, GetAdjustedTime(), addr, pubKeyMerchantnode, hashTPoSContractTxNew }
{}

CMerchantnode::CMerchantnode(const CMerchantnode& other) :
    merchantnode_info_t{other},
    lastPing(other.lastPing),
    vchSig(other.vchSig),
    nPoSeBanScore(other.nPoSeBanScore),
    nPoSeBanHeight(other.nPoSeBanHeight),
    fUnitTest(other.fUnitTest)
{}

CMerchantnode::CMerchantnode(const CMerchantnodeBroadcast& mnb) :
    merchantnode_info_t{ mnb.nActiveState, mnb.nProtocolVersion,
                         mnb.sigTime, mnb.addr,
                         mnb.pubKeyMerchantnode,
                         mnb.hashTPoSContractTx,
                         mnb.sigTime /*nTimeLastWatchdogVote*/},
    lastPing(mnb.lastPing),
    vchSig(mnb.vchSig)
{}

//
// When a new merchantnode broadcast is sent, update our information
//
bool CMerchantnode::UpdateFromNewBroadcast(CMerchantnodeBroadcast& mnb, CConnman& connman)
{
    if(mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyMerchantnode = mnb.pubKeyMerchantnode;
    hashTPoSContractTx = mnb.hashTPoSContractTx;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if(mnb.lastPing == CMerchantnodePing() || (mnb.lastPing != CMerchantnodePing() && mnb.lastPing.CheckAndUpdate(this, true, nDos, connman))) {
        lastPing = mnb.lastPing;
        merchantnodeman.mapSeenMerchantnodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Merchantnode privkey...
    if(fMerchantNode && pubKeyMerchantnode == activeMerchantnode.pubKeyMerchantnode) {
        nPoSeBanScore = -MERCHANTNODE_POSE_BAN_MAX_SCORE;
        if(nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeMerchantnode.ManageState(connman);
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CMerchantnode::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

void CMerchantnode::Check(bool fForce)
{
    LOCK(cs);

    if(ShutdownRequested()) return;

    if(!fForce && (GetTime() - nTimeLastChecked < MERCHANTNODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());

    int nHeight = 0;
    if(!fUnitTest) {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain) return;

        nHeight = chainActive.Height();
    }

    if(IsPoSeBanned()) {
        if(nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Merchantnode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CMerchantnode::Check -- Merchantnode %s is unbanned and back in list now\n", HexStr(pubKeyMerchantnode.Raw()));
        DecreasePoSeBanScore();
    } else if(nPoSeBanScore >= MERCHANTNODE_POSE_BAN_MAX_SCORE) {
        nActiveState = MERCHANTNODE_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = 60;
        LogPrintf("CMerchantnode::Check -- Merchantnode %s is banned till block %d now\n", HexStr(pubKeyMerchantnode.Raw()), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurMerchantnode = fMerchantNode && activeMerchantnode.pubKeyMerchantnode == pubKeyMerchantnode;

    // merchantnode doesn't meet payment protocol requirements ...
    bool fRequireUpdate =
            // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
            (fOurMerchantnode && nProtocolVersion < PROTOCOL_VERSION);

    if(fRequireUpdate) {
        nActiveState = MERCHANTNODE_UPDATE_REQUIRED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state now\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
        }
        return;
    }

    // keep old merchantnodes on start, give them a chance to receive updates...
    bool fWaitForPing = !merchantnodeSync.IsMerchantnodeListSynced() && !IsPingedWithin(MERCHANTNODE_MIN_MNP_SECONDS);

    if(fWaitForPing && !fOurMerchantnode) {
        // ...but if it was already expired before the initial check - return right away
        if(IsExpired() || IsWatchdogExpired() || IsNewStartRequired()) {
            LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state, waiting for ping\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own merchantnode
    if(!fWaitForPing || fOurMerchantnode) {

        if(!IsPingedWithin(MERCHANTNODE_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = MERCHANTNODE_NEW_START_REQUIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state now\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
            }
            return;
        }

        bool fWatchdogActive = merchantnodeSync.IsSynced() && merchantnodeman.IsWatchdogActive();
        bool fWatchdogExpired = (fWatchdogActive && ((GetAdjustedTime() - nTimeLastWatchdogVote) > MERCHANTNODE_WATCHDOG_MAX_SECONDS));

        LogPrint("merchantnode", "CMerchantnode::Check -- outpoint=%s, nTimeLastWatchdogVote=%d, GetAdjustedTime()=%d, fWatchdogExpired=%d\n",
                 HexStr(pubKeyMerchantnode.Raw()), nTimeLastWatchdogVote, GetAdjustedTime(), fWatchdogExpired);

        if(fWatchdogExpired) {
            nActiveState = MERCHANTNODE_WATCHDOG_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state now\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
            }
            return;
        }

        if(!IsPingedWithin(MERCHANTNODE_MAX_EXPIRATION_SECONDS)) {
            nActiveState = MERCHANTNODE_SIGNIFICANTLY_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state now\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
            }
        }

        if(!IsPingedWithin(MERCHANTNODE_EXPIRATION_SECONDS)) {
            nActiveState = MERCHANTNODE_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state now\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
            }
            return;
        }
    }

    if(lastPing.sigTime - sigTime < MERCHANTNODE_MIN_MNP_SECONDS) {
        nActiveState = MERCHANTNODE_PRE_ENABLED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state now\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
        }
        return;
    }

    nActiveState = MERCHANTNODE_ENABLED; // OK
    if(nActiveStatePrev != nActiveState) {
        LogPrint("merchantnode", "CMerchantnode::Check -- Merchantnode %s is in %s state now\n", HexStr(pubKeyMerchantnode.Raw()), GetStateString());
    }
}

bool CMerchantnode::IsValidNetAddr() const
{
    return IsValidNetAddr(addr);
}

bool CMerchantnode::IsValidNetAddr(CService addrIn)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
            (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

merchantnode_info_t CMerchantnode::GetInfo() const
{
    merchantnode_info_t info{*this};
    info.nTimeLastPing = lastPing.sigTime;
    info.fInfoValid = true;
    return info;
}

std::string CMerchantnode::StateToString(int nStateIn)
{
    switch(nStateIn) {
    case MERCHANTNODE_PRE_ENABLED:            return "PRE_ENABLED";
    case MERCHANTNODE_ENABLED:                return "ENABLED";
    case MERCHANTNODE_EXPIRED:                return "EXPIRED";
    case MERCHANTNODE_SIGNIFICANTLY_EXPIRED:  return "SIGNIFICATLY_EXPIRED";
    case MERCHANTNODE_UPDATE_REQUIRED:        return "UPDATE_REQUIRED";
    case MERCHANTNODE_WATCHDOG_EXPIRED:       return "WATCHDOG_EXPIRED";
    case MERCHANTNODE_NEW_START_REQUIRED:     return "NEW_START_REQUIRED";
    case MERCHANTNODE_POSE_BAN:               return "POSE_BAN";
    default:                                return "UNKNOWN";
    }
}

std::string CMerchantnode::GetStateString() const
{
    return StateToString(nActiveState);
}

std::string CMerchantnode::GetStatus() const
{
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

#ifdef ENABLE_WALLET
bool CMerchantnodeBroadcast::Create(std::string strService, std::string strMerchantPrivKey,
                                    std::string strHashTPoSContractTx, std::string& strErrorRet,
                                    CMerchantnodeBroadcast &mnbRet, bool fOffline)
{
    CPubKey pubKeyMerchantnodeNew;
    CKey keyMerchantnodeNew;

    auto Log = [&strErrorRet](std::string sErr)->bool
    {
        strErrorRet = sErr;
        LogPrintf("CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    };

    //need correct blocks to send ping
    if (!fOffline && !merchantnodeSync.IsBlockchainSynced())
        return Log("Sync in progress. Must wait until sync is complete to start Merchantnode");

    if (!CMessageSigner::GetKeysFromSecret(strMerchantPrivKey, keyMerchantnodeNew, pubKeyMerchantnodeNew))
        return Log(strprintf("Invalid merchantnode key %s", strMerchantPrivKey));

    CService service;
    if (!Lookup(strService.c_str(), service, 0, false))
        return Log(strprintf("Invalid address %s for merchantnode.", strService));
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort)
            return Log(strprintf("Invalid port %u for merchantnode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort));
    } else if (service.GetPort() == mainnetDefaultPort)
        return Log(strprintf("Invalid port %u for merchantnode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort));

    return Create(service, keyMerchantnodeNew, pubKeyMerchantnodeNew, uint256S(strHashTPoSContractTx), strErrorRet, mnbRet);
}

bool CMerchantnodeBroadcast::Create(const CService& service, const CKey& keyMerchantnodeNew,
                                    const CPubKey& pubKeyMerchantnodeNew, const uint256 &hashTPoSContractTx,
                                    std::string &strErrorRet, CMerchantnodeBroadcast &mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("merchantnode", "CMerchantnodeBroadcast::Create -- pubKeyMerchantnodeNew.GetID() = %s\n",
             pubKeyMerchantnodeNew.GetID().ToString());

    auto Log = [&strErrorRet,&mnbRet](std::string sErr)->bool
    {
        strErrorRet = sErr;
        LogPrintf("CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CMerchantnodeBroadcast();
        return false;
    };

    CMerchantnodePing mnp(pubKeyMerchantnodeNew);
    if (!mnp.Sign(keyMerchantnodeNew, pubKeyMerchantnodeNew))
        return Log(strprintf("Failed to sign ping, merchantnode=%s", HexStr(pubKeyMerchantnodeNew.Raw())));

    mnbRet = CMerchantnodeBroadcast(service, pubKeyMerchantnodeNew, hashTPoSContractTx, PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr())
        return Log(strprintf("Invalid IP address, merchantnode=%s", HexStr(pubKeyMerchantnodeNew.Raw())));

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyMerchantnodeNew))
        return Log(strprintf("Failed to sign broadcast, merchantnode=%s", HexStr(pubKeyMerchantnodeNew.Raw())));

    return true;
}
#endif // ENABLE_WALLET

bool CMerchantnodeBroadcast::SimpleCheck(int& nDos)
{
    nDos = 0;

    // make sure addr is valid
    if(!IsValidNetAddr()) {
        LogPrintf("CMerchantnodeBroadcast::SimpleCheck -- Invalid addr, rejected: merchantnode=%s  addr=%s\n",
                  HexStr(pubKeyMerchantnode.Raw()), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CMerchantnodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: merchantnode=%s\n", HexStr(pubKeyMerchantnode.Raw()));
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if(lastPing == CMerchantnodePing() || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = lastPing.IsSignificantlyExpired() ? MERCHANTNODE_SIGNIFICANTLY_EXPIRED : MERCHANTNODE_EXPIRED;
    }

    if(nProtocolVersion < PROTOCOL_VERSION) {
        LogPrintf("CMerchantnodeBroadcast::SimpleCheck -- ignoring outdated Merchantnode: merchantnode=%s  nProtocolVersion=%d\n", HexStr(pubKeyMerchantnode.Raw()), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyMerchantnode.GetID());

    if(pubkeyScript.size() != 25) {
        LogPrintf("CMerchantnodeBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != mainnetDefaultPort) return false;
    } else if(addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CMerchantnodeBroadcast::Update(CMerchantnode* pmn, int& nDos, CConnman& connman)
{
    nDos = 0;

    if(pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenMerchantnodeBroadcast in CMerchantnodeMan::CheckMnbAndUpdateMerchantnodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if(pmn->sigTime > sigTime) {
        LogPrintf("CMerchantnodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Merchantnode %s %s\n",
                  sigTime, pmn->sigTime, HexStr(pubKeyMerchantnode.Raw()), addr.ToString());
        return false;
    }

    pmn->Check();

    // merchantnode is banned by PoSe
    if(pmn->IsPoSeBanned()) {
        LogPrintf("CMerchantnodeBroadcast::Update -- Banned by PoSe, merchantnode=%s\n", HexStr(pubKeyMerchantnode.Raw()));
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if(pmn->pubKeyMerchantnode != pubKeyMerchantnode) {
        LogPrintf("CMerchantnodeBroadcast::Update -- Got mismatched pubKeyMerchantnode");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CMerchantnodeBroadcast::Update -- CheckSignature() failed, merchantnode=%s\n", HexStr(pubKeyMerchantnode.Raw()));
        return false;
    }

    // if ther was no merchantnode broadcast recently or if it matches our Merchantnode privkey...
    if(!pmn->IsBroadcastedWithin(MERCHANTNODE_MIN_MNB_SECONDS) || (fMerchantNode && pubKeyMerchantnode == activeMerchantnode.pubKeyMerchantnode)) {
        // take the newest entry
        LogPrintf("CMerchantnodeBroadcast::Update -- Got UPDATED Merchantnode entry: addr=%s\n", addr.ToString());
        if(pmn->UpdateFromNewBroadcast(*this, connman)) {
            pmn->Check();
            Relay(connman);
        }
        merchantnodeSync.BumpAssetLastTime("CMerchantnodeBroadcast::Update");
    }

    return true;
}

bool CMerchantnodeBroadcast::CheckMerchantnode(int &nDos)
{
    nDos = 0;
    return CheckSignature(nDos);
}

bool CMerchantnodeBroadcast::Sign(const CKey& keyCollateralAddress)
{
    std::string strError;
    std::string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
            pubKeyMerchantnode.GetID().ToString() +
            boost::lexical_cast<std::string>(nProtocolVersion);

    if(!CMessageSigner::SignMessage(strMessage, vchSig, keyCollateralAddress)) {
        LogPrintf("CMerchantnodeBroadcast::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(pubKeyMerchantnode, vchSig, strMessage, strError)) {
        LogPrintf("CMerchantnodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CMerchantnodeBroadcast::CheckSignature(int& nDos)
{
    std::string strMessage;
    std::string strError = "";
    nDos = 0;

    strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
            pubKeyMerchantnode.GetID().ToString() +
            boost::lexical_cast<std::string>(nProtocolVersion);

    LogPrint("merchantnode", "CMerchantnodeBroadcast::CheckSignature -- strMessage: %s  pubKeyMerchantnode address: %s  sig: %s\n", strMessage, CBitcoinAddress(pubKeyMerchantnode.GetID()).ToString(), EncodeBase64(&vchSig[0], vchSig.size()));

    if(!CMessageSigner::VerifyMessage(pubKeyMerchantnode, vchSig, strMessage, strError)){
        LogPrintf("CMerchantnodeBroadcast::CheckSignature -- Got bad Merchantnode announce signature, error: %s\n", strError);
        nDos = 100;
        return false;
    }

    return true;
}

void CMerchantnodeBroadcast::Relay(CConnman& connman)
{
    // Do not relay until fully synced
    if(!merchantnodeSync.IsSynced()) {
        LogPrint("merchantnode", "CMerchantnodeBroadcast::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_MERCHANTNODE_ANNOUNCE, GetHash());
    connman.RelayInv(inv);
}

CMerchantnodePing::CMerchantnodePing(const CPubKey &merchantPubKey)
{
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    this->merchantPubKey = merchantPubKey;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
}

bool CMerchantnodePing::Sign(const CKey& keyMerchantnode, const CPubKey& pubKeyMerchantnode)
{
    std::string strError;
    std::string strMasterNodeSignMessage;

    // TODO: add sentinel data
    sigTime = GetAdjustedTime();
    std::string strMessage = merchantPubKey.GetID().ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if(!CMessageSigner::SignMessage(strMessage, vchSig, keyMerchantnode)) {
        LogPrintf("CMerchantnodePing::Sign -- SignMessage() failed\n");
        return false;
    }

    if(!CMessageSigner::VerifyMessage(pubKeyMerchantnode, vchSig, strMessage, strError)) {
        LogPrintf("CMerchantnodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CMerchantnodePing::CheckSignature(CPubKey& pubKeyMerchantnode, int &nDos)
{
    // TODO: add sentinel data
    std::string strMessage = merchantPubKey.GetID().ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";
    nDos = 0;

    if(!CMessageSigner::VerifyMessage(pubKeyMerchantnode, vchSig, strMessage, strError)) {
        LogPrintf("CMerchantnodePing::CheckSignature -- Got bad Merchantnode ping signature, merchantnode=%s, error: %s\n", HexStr(merchantPubKey.Raw()), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CMerchantnodePing::SimpleCheck(int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CMerchantnodePing::SimpleCheck -- Signature rejected, too far into the future, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
        nDos = 1;
        return false;
    }

    {
        AssertLockHeld(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("merchantnode", "CMerchantnodePing::SimpleCheck -- Merchantnode ping is invalid, unknown block hash: merchantnode=%s blockHash=%s\n", HexStr(merchantPubKey.Raw()), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("merchantnode", "CMerchantnodePing::SimpleCheck -- Merchantnode ping verified: merchantnode=%s  blockHash=%s  sigTime=%d\n", HexStr(merchantPubKey.Raw()), blockHash.ToString(), sigTime);
    return true;
}

bool CMerchantnodePing::CheckAndUpdate(CMerchantnode* pmn, bool fFromNewBroadcast, int& nDos, CConnman& connman)
{
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- Couldn't find Merchantnode entry, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
        return false;
    }

    if(!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- merchantnode protocol is outdated, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- merchantnode is completely expired, new start is required, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
            return false;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            LogPrintf("CMerchantnodePing::CheckAndUpdate -- Merchantnode ping is invalid, block hash is too old: merchantnode=%s  blockHash=%s\n", HexStr(merchantPubKey.Raw()), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- New ping: merchantnode=%s  blockHash=%s  sigTime=%d\n", HexStr(merchantPubKey.Raw()), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", HexStr(pubKeyMerchantnode.Raw()));
    // update only if there is no known ping for this merchantnode or
    // last ping was more then MERCHANTNODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(MERCHANTNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- Merchantnode ping arrived too early, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyMerchantnode, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that MERCHANTNODE_EXPIRATION_SECONDS/2 should be enough to finish mn list sync)
    if(!merchantnodeSync.IsMerchantnodeListSynced() && !pmn->IsPingedWithin(MERCHANTNODE_EXPIRATION_SECONDS/2)) {
        // let's bump sync timeout
        LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- bumping sync timeout, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
        merchantnodeSync.BumpAssetLastTime("CMerchantnodePing::CheckAndUpdate");
    }

    // let's store this ping as the last one
    LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- Merchantnode ping accepted, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
    pmn->lastPing = *this;

    // and update merchantnodeman.mapSeenMerchantnodeBroadcast.lastPing which is probably outdated
    CMerchantnodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (merchantnodeman.mapSeenMerchantnodeBroadcast.count(hash)) {
        merchantnodeman.mapSeenMerchantnodeBroadcast[hash].second.lastPing = *this;
    }

    // force update, ignoring cache
    pmn->Check(true);
    // relay ping for nodes in ENABLED/EXPIRED/WATCHDOG_EXPIRED state only, skip everyone else
    if (!pmn->IsEnabled() && !pmn->IsExpired() && !pmn->IsWatchdogExpired()) return false;

    LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate -- Merchantnode ping acceepted and relayed, merchantnode=%s\n", HexStr(merchantPubKey.Raw()));
    Relay(connman);

    return true;
}

void CMerchantnodePing::Relay(CConnman& connman)
{
    // Do not relay until fully synced
    if(!merchantnodeSync.IsSynced()) {
        LogPrint("merchantnode", "CMerchantnodePing::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_MERCHANTNODE_PING, GetHash());
    connman.RelayInv(inv);
}

void CMerchantnode::UpdateWatchdogVoteTime(uint64_t nVoteTime)
{
    LOCK(cs);
    nTimeLastWatchdogVote = (nVoteTime == 0) ? GetAdjustedTime() : nVoteTime;
}
