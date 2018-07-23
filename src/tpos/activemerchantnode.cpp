// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tpos/activemerchantnode.h>
#include <tpos/merchantnode.h>
#include <tpos/merchantnode-sync.h>
#include <tpos/merchantnodeman.h>
#include <protocol.h>
#include <utilstrencodings.h>

// Keep track of the active Merchantnode
CActiveMerchantnode activeMerchantnode;

void CActiveMerchantnode::ManageState(CConnman& connman)
{
    LogPrint(BCLog::MERCHANTNODE, "CActiveMerchantnode::ManageState -- Start\n");
    if(!fMerchantNode) {
        LogPrint(BCLog::MERCHANTNODE, "CActiveMerchantnode::ManageState -- Not a masternode, returning\n");
        return;
    }

    if(Params().NetworkIDString() != CBaseChainParams::REGTEST && !merchantnodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMerchantnode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if(nState == ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_MERCHANTNODE_INITIAL;
    }

    LogPrint(BCLog::MERCHANTNODE, "CActiveMerchantnode::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if(eType == MERCHANTNODE_UNKNOWN) {
        ManageStateInitial(connman);
    }

    if(eType == MERCHANTNODE_REMOTE) {
        ManageStateRemote();
    }

    SendMerchantnodePing(connman);
}

std::string CActiveMerchantnode::GetStateString() const
{
    switch (nState) {
        case ACTIVE_MERCHANTNODE_INITIAL:         return "INITIAL";
        case ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS: return "SYNC_IN_PROCESS";
        case ACTIVE_MERCHANTNODE_INPUT_TOO_NEW:   return "INPUT_TOO_NEW";
        case ACTIVE_MERCHANTNODE_NOT_CAPABLE:     return "NOT_CAPABLE";
        case ACTIVE_MERCHANTNODE_STARTED:         return "STARTED";
        default:                                return "UNKNOWN";
    }
}

std::string CActiveMerchantnode::GetStatus() const
{
    switch (nState) {
        case ACTIVE_MERCHANTNODE_INITIAL:         return "Node just started, not yet activated";
        case ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start Merchantnode";
        case ACTIVE_MERCHANTNODE_INPUT_TOO_NEW:   return strprintf("Merchantnode input must have at least %d confirmations", Params().GetConsensus().nMerchantnodeMinimumConfirmations);
        case ACTIVE_MERCHANTNODE_NOT_CAPABLE:     return "Not capable merchantnode: " + strNotCapableReason;
        case ACTIVE_MERCHANTNODE_STARTED:         return "Merchantnode successfully started";
        default:                                return "Unknown";
    }
}

std::string CActiveMerchantnode::GetTypeString() const
{
    std::string strType;
    switch(eType) {
    case MERCHANTNODE_REMOTE:
        strType = "REMOTE";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveMerchantnode::SendMerchantnodePing(CConnman& connman)
{
    if(!fPingerEnabled) {
        LogPrint(BCLog::MERCHANTNODE, "CActiveMerchantnode::SendMerchantnodePing -- %s: masternode ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if(!merchantnodeman.Has(pubKeyMerchantnode)) {
        strNotCapableReason = "Merchantnode not in merchantnode list";
        nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        LogPrintf("CActiveMerchantnode::SendMerchantnodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CMerchantnodePing mnp(pubKeyMerchantnode);
    mnp.nSentinelVersion = nSentinelVersion;
    mnp.fSentinelIsCurrent =
            (abs(GetAdjustedTime() - nSentinelPingTime) < MERCHANTNODE_WATCHDOG_MAX_SECONDS);
    if(!mnp.Sign(keyMerchantnode, pubKeyMerchantnode)) {
        LogPrintf("CActiveMerchantnode::SendMerchantnodePing -- ERROR: Couldn't sign Merchantnode Ping\n");
        return false;
    }

    // Update lastPing for our masternode in Merchantnode list
    if(merchantnodeman.IsMerchantnodePingedWithin(pubKeyMerchantnode, MERCHANTNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveMerchantnode::SendMerchantnodePing -- Too early to send Merchantnode Ping\n");
        return false;
    }

    merchantnodeman.SetMerchantnodeLastPing(pubKeyMerchantnode, mnp);

    LogPrintf("%s -- Relaying ping, collateral=%s\n", __func__, HexStr(pubKeyMerchantnode.GetID().ToString()));
    mnp.Relay(connman);

    return true;
}

bool CActiveMerchantnode::UpdateSentinelPing(int version)
{
    nSentinelVersion = version;
    nSentinelPingTime = GetAdjustedTime();

    return true;
}

void CActiveMerchantnode::ManageStateInitial(CConnman& connman)
{
    LogPrint(BCLog::MERCHANTNODE, "CActiveMerchantnode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        strNotCapableReason = "Merchantnode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveMerchantnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(service) && CMerchantnode::IsValidNetAddr(service);
    if(!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        connman.ForEachNode([&fFoundLocal, &empty, this](CNode* pnode) {
            empty = false;
            if (!fFoundLocal && pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(service, &pnode->addr) && CMerchantnode::IsValidNetAddr(service);
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
            strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
            LogPrintf("CActiveMerchantnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }

    if(!fFoundLocal) {
        nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveMerchantnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    auto mainChainParams = CreateChainParams(CBaseChainParams::MAIN);
    int mainnetDefaultPort = mainChainParams->GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(), mainnetDefaultPort);
            LogPrintf("CActiveMerchantnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if(service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(), mainnetDefaultPort);
        LogPrintf("CActiveMerchantnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveMerchantnode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());

    if(!connman.OpenMerchantnodeConnection(CAddress(service, NODE_NETWORK))) {
        nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveMerchantnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = MERCHANTNODE_REMOTE;

    LogPrint(BCLog::MERCHANTNODE, "CActiveMerchantnode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveMerchantnode::ManageStateRemote()
{
    LogPrint(BCLog::MERCHANTNODE, "CActiveMerchantnode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyMerchantnode.GetID() = %s\n",
             GetStatus(), GetTypeString(), fPingerEnabled, pubKeyMerchantnode.GetID().ToString());

    merchantnodeman.CheckMerchantnode(pubKeyMerchantnode, true);
    merchantnode_info_t infoMn;
    if(merchantnodeman.GetMerchantnodeInfo(pubKeyMerchantnode, infoMn)) {
        if(infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveMerchantnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(service != infoMn.addr) {
            nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this masternode changed recently.";
            LogPrintf("CActiveMerchantnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(!CMerchantnode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Merchantnode in %s state", CMerchantnode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveMerchantnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(nState != ACTIVE_MERCHANTNODE_STARTED) {
            LogPrintf("CActiveMerchantnode::ManageStateRemote -- STARTED!\n");
            pubKeyMerchantnode = infoMn.pubKeyMerchantnode;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_MERCHANTNODE_STARTED;
        }
    }
    else {
        nState = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        strNotCapableReason = "Merchantnode not in masternode list";
        LogPrintf("CActiveMerchantnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}
