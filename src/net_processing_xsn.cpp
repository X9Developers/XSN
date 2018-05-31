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

using SporkHandler = std::function<CSerializedNetMsg(const CNetMsgMaker &, const uint256 &)>;
using MapSporkHandlers = std::map<int, SporkHandler>;

#define ADD_HANDLER(sporkID, handler) sporkHandlers.emplace(sporkID, [](const CNetMsgMaker &msgMaker, const uint256 &hash) -> CSerializedNetMsg handler)

static const MapSporkHandlers &GetMapSporkHandlers()
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
    }

    return sporkHandlers;
}

bool net_processing_xsn::ProcessGetData(CNode *pfrom, const Consensus::Params &consensusParams, CConnman *connman, const CInv &inv)
{
    const auto &handlersMap = GetMapSporkHandlers();
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

        if(masternodeSync.IsBlockchainSynced() &&
                merchantnodeSync.IsBlockchainSynced() &&
                !ShutdownRequested()) {

            nTick++;

            // make sure to check all masternodes first
            mnodeman.Check();
            merchantnodeman.Check();

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
