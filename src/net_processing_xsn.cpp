#include "net_processing_xsn.h"

#include <spork.h>
#include <netmessagemaker.h>
#include <map>
#include <functional>

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
    sporkManager.ProcessSpork(pfrom, strCommand, vRecv, connman);
}
