#include <net_processing_xsn.h>

#include <spork.h>
#include <netmessagemaker.h>
#include <map>
#include <functional>
#include <masternodeman.h>
#include <masternode-sync.h>
#include <masternode-payments.h>
#include <governance/governance.h>
#include <tpos/merchantnode-sync.h>
#include <tpos/merchantnodeman.h>
#include <activemasternode.h>
#include <tpos/activemerchantnode.h>
#include <instantx.h>
#include <init.h>

namespace LegacyInvMsg {
enum {
    MSG_TX = 1,
    MSG_BLOCK,
    // Nodes may always request a MSG_FILTERED_BLOCK in a getdata, however,
    // MSG_FILTERED_BLOCK should not appear in any invs except as a part of getdata.
    MSG_FILTERED_BLOCK,
    // XSN message types
    // NOTE: declare non-implmented here, we must keep this enum consistent and backwards compatible
    MSG_TXLOCK_REQUEST,
    MSG_TXLOCK_VOTE,
    MSG_SPORK,
    MSG_MASTERNODE_PAYMENT_VOTE,
    MSG_MASTERNODE_PAYMENT_BLOCK, // reusing, was MSG_MASTERNODE_SCANNING_ERROR previousely, was NOT used in 12.0
    MSG_BUDGET_VOTE, // depreciated since 12.1
    MSG_BUDGET_PROPOSAL, // depreciated since 12.1
    MSG_BUDGET_FINALIZED, // depreciated since 12.1
    MSG_BUDGET_FINALIZED_VOTE, // depreciated since 12.1
    MSG_MASTERNODE_QUORUM, // not implemented
    MSG_MASTERNODE_ANNOUNCE,
    MSG_MASTERNODE_PING,
    MSG_DSTX,
    MSG_GOVERNANCE_OBJECT,
    MSG_GOVERNANCE_OBJECT_VOTE,
    MSG_MASTERNODE_VERIFY,
    MSG_MERCHANTNODE_VERIFY,
    MSG_MERCHANTNODE_ANNOUNCE,
    MSG_MERCHANTNODE_PING
};
}

using SporkHandler = std::function<CSerializedNetMsg(const CNetMsgMaker &, const uint256 &)>;
using MapSporkHandlers = std::map<int, SporkHandler>;

#define ADD_HANDLER(sporkID, handler) sporkHandlers.emplace(sporkID, [](const CNetMsgMaker &msgMaker, const uint256 &hash) -> CSerializedNetMsg handler)

static const MapSporkHandlers &GetMapGetDataHandlers()
{
    static MapSporkHandlers sporkHandlers;

    if(sporkHandlers.empty())
    {
        ADD_HANDLER(MSG_SPORK, {
                        if(mapSporks.count(hash)) {
                            return msgMaker.Make(NetMsgType::SPORK, mapSporks[hash]);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_TXLOCK_REQUEST, {
                        CTxLockRequestRef txLockRequest;
                        if(instantsend.GetTxLockRequest(hash, txLockRequest)) {
                            return msgMaker.Make(NetMsgType::TXLOCKREQUEST, *txLockRequest);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_TXLOCK_VOTE, {
                        CTxLockVote vote;
                        if(instantsend.GetTxLockVote(hash, vote)) {
                            return msgMaker.Make(NetMsgType::TXLOCKVOTE, vote);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MASTERNODE_PAYMENT_BLOCK, {
                        BlockMap::iterator mi = mapBlockIndex.find(hash);
                        LOCK(cs_mapMasternodeBlocks);
                        if (mi != mapBlockIndex.end() && mnpayments.mapMasternodeBlocks.count(mi->second->nHeight)) {
                            for(const CMasternodePayee& payee : mnpayments.mapMasternodeBlocks[mi->second->nHeight].vecPayees) {
                                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                                for(const uint256& hash : vecVoteHashes) {
                                    if(mnpayments.HasVerifiedPaymentVote(hash)) {
                                        return msgMaker.Make(NetMsgType::MASTERNODEPAYMENTVOTE, mnpayments.mapMasternodePaymentVotes[hash]);
                                    }
                                }
                            }
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MASTERNODE_PAYMENT_VOTE, {
                        if(mnpayments.HasVerifiedPaymentVote(hash)) {
                            return msgMaker.Make(NetMsgType::MASTERNODEPAYMENTVOTE, mnpayments.mapMasternodePaymentVotes[hash]);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MASTERNODE_ANNOUNCE, {
                        if(mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
                            return msgMaker.Make(NetMsgType::MNANNOUNCE, mnodeman.mapSeenMasternodeBroadcast[hash].second);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MERCHANTNODE_ANNOUNCE, {
                        if(merchantnodeman.mapSeenMerchantnodeBroadcast.count(hash)){
                            return msgMaker.Make(NetMsgType::MERCHANTNODEANNOUNCE, merchantnodeman.mapSeenMerchantnodeBroadcast[hash].second);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MASTERNODE_PING, {
                        if(mnodeman.mapSeenMasternodePing.count(hash)) {
                            return msgMaker.Make(NetMsgType::MNPING, mnodeman.mapSeenMasternodePing[hash]);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MERCHANTNODE_PING, {
                        if(merchantnodeman.mapSeenMerchantnodePing.count(hash)) {
                            return msgMaker.Make(NetMsgType::MERCHANTNODEPING, merchantnodeman.mapSeenMerchantnodePing[hash]);
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_GOVERNANCE_OBJECT, {
                        if(governance.HaveObjectForHash(hash)) {
                            CGovernanceObject obj;
                            if(governance.SerializeObjectForHash(hash, obj)) {
                                return msgMaker.Make(NetMsgType::MNGOVERNANCEOBJECT, obj);
                            }
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_GOVERNANCE_OBJECT_VOTE, {
                        if(governance.HaveVoteForHash(hash)) {
                            CGovernanceVote vote;
                            if(governance.SerializeVoteForHash(hash, vote)) {
                                return msgMaker.Make(NetMsgType::MNGOVERNANCEOBJECTVOTE, vote);

                            }
                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MASTERNODE_VERIFY, {
                        if(mnodeman.mapSeenMasternodeVerification.count(hash)) {
                            return msgMaker.Make(NetMsgType::MNVERIFY, mnodeman.mapSeenMasternodeVerification[hash]);

                        }
                        return {};
                    });
        ADD_HANDLER(MSG_MERCHANTNODE_VERIFY, {
                        if(merchantnodeman.mapSeenMerchantnodeVerification.count(hash)) {
                            return msgMaker.Make(NetMsgType::MERCHANTNODEVERIFY, merchantnodeman.mapSeenMerchantnodeVerification[hash]);
                        }
                        return {};
                    });
    }

    return sporkHandlers;
}

bool net_processing_xsn::ProcessGetData(CNode *pfrom, const Consensus::Params &consensusParams, CConnman *connman, const CInv &inv)
{
    const auto &handlersMap = GetMapGetDataHandlers();
    auto it = handlersMap.find(inv.type);
    if(it != std::end(handlersMap))
    {
        const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
        auto &&msg = it->second(msgMaker, inv.hash);
        if(!msg.command.empty())
        {
            connman->PushMessage(pfrom, std::move(msg));
            return true;
        }
    }

    return false;
}

void net_processing_xsn::ProcessExtension(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv, CConnman *connman)
{
    mnodeman.ProcessMessage(pfrom, strCommand, vRecv, *connman);
    mnpayments.ProcessMessage(pfrom, strCommand, vRecv, *connman);
    merchantnodeman.ProcessMessage(pfrom, strCommand, vRecv, *connman);
    instantsend.ProcessMessage(pfrom, strCommand, vRecv, *connman);
    sporkManager.ProcessSpork(pfrom, strCommand, vRecv, connman);
    masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);
    merchantnodeSync.ProcessMessage(pfrom, strCommand, vRecv);
    governance.ProcessMessage(pfrom, strCommand, vRecv, *connman);
}

void net_processing_xsn::ThreadProcessExtensions(CConnman *pConnman)
{
    if(fLiteMode) return; // disable all XSN specific functionality

    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    // Make this thread recognisable as the PrivateSend thread
    RenameThread("xsn-ps");

    unsigned int nTick = 0;

    auto &connman = *pConnman;
    while (!ShutdownRequested())
    {
        boost::this_thread::interruption_point();
        MilliSleep(1000);

        // try to sync from all available nodes, one step at a time
        masternodeSync.ProcessTick(connman);
        merchantnodeSync.ProcessTick(connman);

        if(!ShutdownRequested()) {

            nTick++;

            if(masternodeSync.IsBlockchainSynced()) {
                // make sure to check all masternodes first
                mnodeman.Check();

                // check if we should activate or ping every few minutes,
                // slightly postpone first run to give net thread a chance to connect to some peers
                if(nTick % MASTERNODE_MIN_MNP_SECONDS == 15)
                    activeMasternode.ManageState(connman);

                if(nTick % 60 == 0) {
                    mnodeman.ProcessMasternodeConnections(connman);
                    mnodeman.CheckAndRemove(connman);
                    mnpayments.CheckAndRemove();
                    instantsend.CheckAndRemove();
                }
                if(fMasterNode && (nTick % (60 * 5) == 0)) {
                    mnodeman.DoFullVerificationStep(connman);
                }

                if(nTick % (60 * 5) == 0) {
                    governance.DoMaintenance(connman);
                }
            }

            if(merchantnodeSync.IsBlockchainSynced()) {

                merchantnodeman.Check();
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
}


bool net_processing_xsn::AlreadyHave(const CInv &inv)
{
    switch(inv.type)
    {
    /*
    XSN Related Inventory Messages

    --

    We shouldn't update the sync times for each of the messages when we already have it.
    We're going to be asking many nodes upfront for the full inventory list, so we'll get duplicates of these.
    We want to only update the time on new hits, so that we can time out appropriately if needed.
    */
    case MSG_TXLOCK_REQUEST:
        return instantsend.AlreadyHave(inv.hash);

    case MSG_TXLOCK_VOTE:
        return instantsend.AlreadyHave(inv.hash);

    case MSG_SPORK:
        return mapSporks.count(inv.hash);

    case MSG_MASTERNODE_PAYMENT_VOTE:
        return mnpayments.mapMasternodePaymentVotes.count(inv.hash);

    case MSG_MASTERNODE_PAYMENT_BLOCK:
    {
        BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
        return mi != mapBlockIndex.end() && mnpayments.mapMasternodeBlocks.find(mi->second->nHeight) != mnpayments.mapMasternodeBlocks.end();
    }

    case MSG_MASTERNODE_ANNOUNCE:
        return mnodeman.mapSeenMasternodeBroadcast.count(inv.hash) && !mnodeman.IsMnbRecoveryRequested(inv.hash);
    case MSG_MERCHANTNODE_ANNOUNCE:
        return merchantnodeman.mapSeenMerchantnodeBroadcast.count(inv.hash) && !merchantnodeman.IsMnbRecoveryRequested(inv.hash);

    case MSG_MASTERNODE_PING:
        return mnodeman.mapSeenMasternodePing.count(inv.hash);
    case MSG_MERCHANTNODE_PING:
        return merchantnodeman.mapSeenMerchantnodePing.count(inv.hash);

    case MSG_DSTX: {
        //        return static_cast<bool>(CPrivateSend::GetDSTX(inv.hash));
        return true;
    }

    case MSG_GOVERNANCE_OBJECT:
    case MSG_GOVERNANCE_OBJECT_VOTE:
        return !governance.ConfirmInventoryRequest(inv);

    case MSG_MASTERNODE_VERIFY:
        return mnodeman.mapSeenMasternodeVerification.count(inv.hash);
    case MSG_MERCHANTNODE_VERIFY:
        return merchantnodeman.mapSeenMerchantnodeVerification.count(inv.hash);
    }
    return true;
}

static int MapLegacyToCurrent(int nLegacyType)
{
    switch(nLegacyType)
    {
    case LegacyInvMsg::MSG_TXLOCK_REQUEST: return MSG_TXLOCK_REQUEST;
    case LegacyInvMsg::MSG_TXLOCK_VOTE: return MSG_TXLOCK_VOTE;
    case LegacyInvMsg::MSG_SPORK: return MSG_SPORK;
    case LegacyInvMsg::MSG_MASTERNODE_PAYMENT_VOTE: return MSG_MASTERNODE_PAYMENT_VOTE;
    case LegacyInvMsg::MSG_MASTERNODE_PAYMENT_BLOCK: return MSG_MASTERNODE_PAYMENT_BLOCK;
    case LegacyInvMsg::MSG_MASTERNODE_ANNOUNCE: return MSG_MASTERNODE_ANNOUNCE;
    case LegacyInvMsg::MSG_MASTERNODE_PING: return MSG_MASTERNODE_PING;
    case LegacyInvMsg::MSG_DSTX: return MSG_DSTX;
    case LegacyInvMsg::MSG_GOVERNANCE_OBJECT: return MSG_GOVERNANCE_OBJECT;
    case LegacyInvMsg::MSG_GOVERNANCE_OBJECT_VOTE: return MSG_GOVERNANCE_OBJECT_VOTE;
    case LegacyInvMsg::MSG_MASTERNODE_VERIFY: return MSG_MASTERNODE_VERIFY;
    case LegacyInvMsg::MSG_MERCHANTNODE_VERIFY: return MSG_MERCHANTNODE_VERIFY;
    case LegacyInvMsg::MSG_MERCHANTNODE_ANNOUNCE: return MSG_MERCHANTNODE_ANNOUNCE;
    case LegacyInvMsg::MSG_MERCHANTNODE_PING: return MSG_MERCHANTNODE_PING;
    }

    return nLegacyType;
}

static int MapCurrentToLegacy(int nCurrentType)
{
    switch(nCurrentType)
    {
    case MSG_TXLOCK_REQUEST: return LegacyInvMsg::MSG_TXLOCK_REQUEST;
    case MSG_TXLOCK_VOTE: return LegacyInvMsg::MSG_TXLOCK_VOTE;
    case MSG_SPORK: return LegacyInvMsg::MSG_SPORK;
    case MSG_MASTERNODE_PAYMENT_VOTE: return LegacyInvMsg::MSG_MASTERNODE_PAYMENT_VOTE;
    case MSG_MASTERNODE_PAYMENT_BLOCK: return LegacyInvMsg::MSG_MASTERNODE_PAYMENT_BLOCK;
    case MSG_MASTERNODE_ANNOUNCE: return LegacyInvMsg::MSG_MASTERNODE_ANNOUNCE;
    case MSG_MASTERNODE_PING: return LegacyInvMsg::MSG_MASTERNODE_PING;
    case MSG_DSTX: return LegacyInvMsg::MSG_DSTX;
    case MSG_GOVERNANCE_OBJECT: return LegacyInvMsg::MSG_GOVERNANCE_OBJECT;
    case MSG_GOVERNANCE_OBJECT_VOTE: return LegacyInvMsg::MSG_GOVERNANCE_OBJECT_VOTE;
    case MSG_MASTERNODE_VERIFY: return LegacyInvMsg::MSG_MASTERNODE_VERIFY;
    case MSG_MERCHANTNODE_VERIFY: return LegacyInvMsg::MSG_MERCHANTNODE_VERIFY;
    case MSG_MERCHANTNODE_ANNOUNCE: return LegacyInvMsg::MSG_MERCHANTNODE_ANNOUNCE;
    case MSG_MERCHANTNODE_PING: return LegacyInvMsg::MSG_MERCHANTNODE_PING;
    }

    return nCurrentType;
}

bool net_processing_xsn::TransformInvForLegacyVersion(CInv &inv, CNode *pfrom, bool fForSending)
{

    if(pfrom->GetSendVersion() == PRESEGWIT_PROTO_VERSION)
    {
        LogPrint(BCLog::NET, "Before %d, send version: %d, recv version: %d\n", inv.type, pfrom->GetSendVersion(), pfrom->GetRecvVersion());
        if(fForSending)
            inv.type = MapCurrentToLegacy(inv.type);
        else
            inv.type = MapLegacyToCurrent(inv.type);

        LogPrint(BCLog::NET, "After %d, send version: %d, recv version: %d\n", inv.type, pfrom->GetSendVersion(), pfrom->GetRecvVersion());
    }

    return true;
}
