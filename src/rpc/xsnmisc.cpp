#include <chain.h>
#include <clientversion.h>
#include <core_io.h>
#include <crypto/ripemd160.h>
#include <init.h>
#include <key_io.h>
#include <validation.h>
#include <httpserver.h>
#include <net.h>
#include <netbase.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <timedata.h>
#include <util.h>
#include <utilstrencodings.h>
#include <spork.h>
#include <netmessagemaker.h>


/*
    Used for updating/reading spork settings on the network
*/
static UniValue spork(const JSONRPCRequest& request)
{
    if(request.params.size() == 0) {
        g_connman->ForEachNode([](CNode *node) {
            const CNetMsgMaker msgMaker(node->GetSendVersion());
            g_connman->PushMessage(node, msgMaker.Make(NetMsgType::GETSPORKS));
        });
        return "Asked";
    }

    if(request.params.size() == 1 && request.params[0].get_str() == "show"){
        UniValue ret(UniValue::VOBJ);
        for(int nSporkID = Spork::SPORK_START; nSporkID < Spork::SPORK_END; nSporkID++){
            if(sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), sporkManager.GetSporkValue(nSporkID)));
        }
        return ret;
    } else if(request.params.size() == 1 && request.params[0].get_str() == "active"){
        UniValue ret(UniValue::VOBJ);
        for(int nSporkID = Spork::SPORK_START; nSporkID < Spork::SPORK_END; nSporkID++){
            if(sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), sporkManager.IsSporkActive(nSporkID)));
        }
        return ret;
    }
#if 0
#ifdef ENABLE_WALLET
    else if (params.size() == 2){
        int nSporkID = sporkManager.GetSporkIDByName(params[0].get_str());
        if(nSporkID == -1){
            return "Invalid spork name";
        }

        if (!g_connman)
            throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

        // SPORK VALUE
        int64_t nValue = params[1].get_int64();

        //broadcast new spork
        if(sporkManager.UpdateSpork(nSporkID, nValue, *g_connman)){
            sporkManager.ExecuteSpork(nSporkID, nValue);
            return "success";
        } else {
            return "failure";
        }

    }

    throw runtime_error(
                "spork <name> [<value>]\n"
                "<name> is the corresponding spork name, or 'show' to show all current spork settings, active to show which sporks are active\n"
                "<value> is a epoch datetime to enable or disable spork\n"
                + HelpRequiringPassphrase());
#else // ENABLE_WALLET
    throw runtime_error(
                "spork <name>\n"
                "<name> is the corresponding spork name, or 'show' to show all current spork settings, active to show which sporks are active\n");
#endif // ENABLE_WALLET
#endif

}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
  { "xsn",            "spork",          &spork,          {"mode"} },
};

void RegisterXSNMiscCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
