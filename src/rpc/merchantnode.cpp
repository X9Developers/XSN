// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tpos/activemerchantnode.h"
#include "base58.h"
#include "init.h"
#include "netbase.h"
#include "validation.h"
#include "tpos/merchantnode-sync.h"
#include "tpos/merchantnodeman.h"
#include "tpos/merchantnode.h"
#include "tpos/merchantnodeconfig.h"
#include "rpc/server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/wallet.h"
#include "core_io.h"

#include <fstream>
#include <iomanip>
#include <univalue.h>


UniValue merchantnode(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (fHelp  ||
            (
            #ifdef ENABLE_WALLET
                strCommand != "start-alias" && strCommand != "start-all" && strCommand != "start-missing" &&
                strCommand != "start-disabled" && strCommand != "outputs" &&
            #endif // ENABLE_WALLET
                strCommand != "list" && strCommand != "list-conf" && strCommand != "count" &&
                strCommand != "debug" && strCommand != "current" && strCommand != "winner" && strCommand != "winners" && strCommand != "genkey" &&
                strCommand != "connect" && strCommand != "status"))
        throw std::runtime_error(
                "merchantnode \"command\"...\n"
                "Set of commands to execute merchantnode related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  count        - Print number of all known merchantnodes (optional: 'ps', 'enabled', 'all', 'qualify')\n"
                "  current      - Print info on current merchantnode winner to be paid the next block (calculated locally)\n"
                "  genkey       - Generate new merchantnodeprivkey\n"
            #ifdef ENABLE_WALLET
                "  outputs      - Print merchantnode compatible outputs\n"
                "  start-alias  - Start single remote merchantnode by assigned alias configured in merchantnode.conf\n"
                "  start-<mode> - Start remote merchantnodes configured in merchantnode.conf (<mode>: 'all', 'missing', 'disabled')\n"
            #endif // ENABLE_WALLET
                "  status       - Print merchantnode status information\n"
                "  list         - Print list of all known merchantnodes (see merchantnodelist for more info)\n"
                "  list-conf    - Print merchantnode.conf in JSON format\n"
                "  winner       - Print info on next merchantnode winner to vote for\n"
                "  winners      - Print list of merchantnode winners\n"
                );

    if (strCommand == "list")
    {
        UniValue newParams(UniValue::VARR);
        // forward params but skip "list"
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return merchantnodelist(newParams, fHelp);
    }

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VOBJ);

        for(auto &&mne : merchantnodeConfig.getEntries())
        {
            CMerchantnode mn;
            CBitcoinSecret privKey;
            privKey.SetString(mne.getMerchantPrivKey());
            CPubKey pubKey = privKey.GetKey().GetPubKey();
            bool fFound = merchantnodeman.Get(pubKey, mn);

            std::string strStatus = fFound ? mn.GetStatus() : "MISSING";

            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getMerchantPrivKey()));
            mnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(Pair("merchantnode", mnObj));
        }

        return resultObj;
    }

    if(strCommand == "connect")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Merchantnode address required");

        std::string strAddress = params[1].get_str();

        CService addr;
        if (!Lookup(strAddress.c_str(), addr, 0, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect merchantnode address %s", strAddress));

        // TODO: Pass CConnman instance somehow and don't use global variable.
        CNode *pnode = g_connman->ConnectNode(CAddress(addr, NODE_NETWORK), NULL);
        if(!pnode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to merchantnode %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count")
    {
        if (params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        if (params.size() == 1)
            return merchantnodeman.size();

        std::string strMode = params[1].get_str();

        if (strMode == "ps")
            return merchantnodeman.CountEnabled();

        if (strMode == "enabled")
            return merchantnodeman.CountEnabled();


        if (strMode == "all")
            return strprintf("Total: %d (PS Compatible: %d / Enabled: %d)",
                             merchantnodeman.size(), merchantnodeman.CountEnabled(),
                             merchantnodeman.CountEnabled());
    }

    if (strCommand == "start-alias")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::string strAlias = params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", strAlias));

        for(auto && mrne : merchantnodeConfig.getEntries()) {
            if(mrne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMerchantnodeBroadcast mnb;

                bool fResult = CMerchantnodeBroadcast::Create(mrne.getIp(), mrne.getMerchantPrivKey(), strError, mnb);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(fResult) {
                    merchantnodeman.UpdateMerchantnodeList(mnb, *g_connman);
                    mnb.Relay(*g_connman);
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }

                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;
        //        bool fResult = CMerchantnodeBroadcast::Create("77.120.42.4:29999",
        //                                                      "928g5ADKbe33FtXyNbNW7mwfGSxyZpRKTgPD4S6ekVS2K9M1vmP",
        //                                                      "caccdbab8f60973009cf295e29f26dc7cc26e7e49de9f54b3306db041fc121c9",
        //                                                      "0",
        //                                                      strError, mnb);
    }

    if (strCommand == "status")
    {
        if (!fMerchantNode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a merchantnode");

        UniValue mnObj(UniValue::VOBJ);

        mnObj.push_back(Pair("pubkey", HexStr(activeMerchantnode.pubKeyMerchantnode.Raw())));
        mnObj.push_back(Pair("service", activeMerchantnode.service.ToString()));

        CMerchantnode mn;
        auto pubKey = activeMerchantnode.pubKeyMerchantnode;
        if(merchantnodeman.Get(pubKey, mn)) {
            mnObj.push_back(Pair("merchantAddress", CBitcoinAddress(pubKey.GetID()).ToString()));
        }

        mnObj.push_back(Pair("status", activeMerchantnode.GetStatus()));
        return mnObj;
    }

    return NullUniValue;
}

UniValue merchantnodelist(const UniValue& params, bool fHelp)
{
    std::string strMode = "status";
    std::string strFilter = "";

    if (params.size() >= 1) strMode = params[0].get_str();
    if (params.size() == 2) strFilter = params[1].get_str();

    if (fHelp || (
                strMode != "activeseconds" && strMode != "addr" && strMode != "full" && strMode != "info" &&
                strMode != "lastseen" && strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "protocol" && strMode != "payee" && strMode != "pubkey" &&
                strMode != "rank" && strMode != "status"))
    {
        throw std::runtime_error(
                    "merchantnodelist ( \"mode\" \"filter\" )\n"
                    "Get a list of merchantnodes in different modes\n"
                    "\nArguments:\n"
                    "1. \"mode\"      (string, optional/required to use filter, defaults = status) The mode to run list in\n"
                    "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
                    "                                    additional matches in some modes are also available\n"
                    "\nAvailable modes:\n"
                    "  activeseconds  - Print number of seconds merchantnode recognized by the network as enabled\n"
                    "                   (since latest issued \"merchantnode start/start-many/start-alias\")\n"
                    "  addr           - Print ip address associated with a merchantnode (can be additionally filtered, partial match)\n"
                    "  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'\n"
                    "                   (can be additionally filtered, partial match)\n"
                    "  info           - Print info in format 'status protocol payee lastseen activeseconds sentinelversion sentinelstate IP'\n"
                    "                   (can be additionally filtered, partial match)\n"
                    "  lastpaidblock  - Print the last block height a node was paid on the network\n"
                    "  lastpaidtime   - Print the last time a node was paid on the network\n"
                    "  lastseen       - Print timestamp of when a merchantnode was last seen on the network\n"
                    "  payee          - Print Xsn address associated with a merchantnode (can be additionally filtered,\n"
                    "                   partial match)\n"
                    "  protocol       - Print protocol of a merchantnode (can be additionally filtered, exact match)\n"
                    "  pubkey         - Print the merchantnode (not collateral) public key\n"
                    "  rank           - Print rank of a merchantnode based on current block\n"
                    "  status         - Print merchantnode status: PRE_ENABLED / ENABLED / EXPIRED / WATCHDOG_EXPIRED / NEW_START_REQUIRED /\n"
                    "                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)\n"
                    );
    }

    if (strMode == "full" || strMode == "lastpaidtime" || strMode == "lastpaidblock") {
        CBlockIndex* pindex = NULL;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
    }

    UniValue obj(UniValue::VOBJ);
    auto mapMerchantnodes = merchantnodeman.GetFullMerchantnodeMap();
    for (auto& mnpair : mapMerchantnodes) {
        CMerchantnode mn = mnpair.second;
        std::string strOutpoint = HexStr(mnpair.first.Raw());
        if (strMode == "activeseconds") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, (int64_t)(mn.lastPing.sigTime - mn.sigTime)));
        } else if (strMode == "addr") {
            std::string strAddress = mn.addr.ToString();
            if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, strAddress));
        } else if (strMode == "full") {
            std::ostringstream streamFull;
            streamFull << std::setw(18) <<
                          mn.GetStatus() << " " <<
                          mn.nProtocolVersion << " " <<
                          CBitcoinAddress(mn.pubKeyMerchantnode.GetID()).ToString() << " " <<
                          (int64_t)mn.lastPing.sigTime << " " << std::setw(8) <<
                          (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " << std::setw(10) <<
                          mn.addr.ToString();
            std::string strFull = streamFull.str();
            if (strFilter !="" && strFull.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, strFull));
        } else if (strMode == "info") {
            std::ostringstream streamInfo;
            streamInfo << std::setw(18) <<
                          mn.GetStatus() << " " <<
                          mn.nProtocolVersion << " " <<
                          CBitcoinAddress(mn.pubKeyMerchantnode.GetID()).ToString() << " " <<
                          (int64_t)mn.lastPing.sigTime << " " << std::setw(8) <<
                          (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " <<
                          SafeIntVersionToString(mn.lastPing.nSentinelVersion) << " "  <<
                          (mn.lastPing.fSentinelIsCurrent ? "current" : "expired") << " " <<
                          mn.addr.ToString();
            std::string strInfo = streamInfo.str();
            if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, strInfo));
        } else if (strMode == "lastseen") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, (int64_t)mn.lastPing.sigTime));
        } else if (strMode == "payee") {
            CBitcoinAddress address(mn.pubKeyMerchantnode.GetID());
            std::string strPayee = address.ToString();
            if (strFilter !="" && strPayee.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, strPayee));
        } else if (strMode == "protocol") {
            if (strFilter !="" && strFilter != strprintf("%d", mn.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, (int64_t)mn.nProtocolVersion));
        } else if (strMode == "pubkey") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, HexStr(mn.pubKeyMerchantnode)));
        } else if (strMode == "status") {
            std::string strStatus = mn.GetStatus();
            if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, strStatus));
        }
    }
    return obj;
}

bool DecodeHexVecMnb(std::vector<CMerchantnodeBroadcast>& vecMnb, std::string strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    std::vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecMnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}

UniValue merchantsentinelping(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1) {
        throw std::runtime_error(
                    "sentinelping version\n"
                    "\nSentinel ping.\n"
                    "\nArguments:\n"
                    "1. version           (string, required) Sentinel version in the form \"x.x.x\"\n"
                    "\nResult:\n"
                    "state                (boolean) Ping result\n"
                    "\nExamples:\n"
                    + HelpExampleCli("sentinelping", "1.0.2")
                    + HelpExampleRpc("sentinelping", "1.0.2")
                    );
    }

    activeMerchantnode.UpdateSentinelPing(StringVersionToInt(params[0].get_str()));
    return true;
}

#ifdef ENABLE_WALLET
UniValue tposcontract(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

    if (fHelp  || (strCommand != "list" && strCommand != "create" && strCommand != "merchant-prepare"))
        throw std::runtime_error(
                "tposcontract \"command\"...\n"
                "Set of commands to execute merchantnode related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  create           - Create tpos transaction\n"
                "  list             - Print list of all tpos contracts that you are owner or merchant\n"
                "  merchant-prepare - Prepare a magic string which constists of txid and outpoint index of merchant address\n"
                );

    if (strCommand == "list")
    {
        UniValue result(UniValue::VOBJ);
        UniValue merchantArray(UniValue::VARR);
        UniValue ownerArray(UniValue::VARR);

        auto parseContract = [](const TPoSContract &contract) {
            UniValue object(UniValue::VOBJ);

            object.push_back(Pair("txid", contract.rawTx.GetHash().ToString()));
            object.push_back(Pair("tposAddress", contract.tposAddress.ToString()));
//            object.push_back(Pair("merchantAddress", contract.merchantAddress.ToString()));
            object.push_back(Pair("merchantTxId", HexStr(contract.merchantOutPoint.hash)));
            object.push_back(Pair("merchantTxOut", static_cast<int>(contract.merchantOutPoint.n)));
            object.push_back(Pair("commission", contract.stakePercentage));

            return object;
        };

        for(auto &&it : pwalletMain->tposMerchantContracts)
        {
            merchantArray.push_back(parseContract(it.second));
        }

        for(auto &&it : pwalletMain->tposOwnerContracts)
        {
            ownerArray.push_back(parseContract(it.second));
        }

        result.push_back(Pair("as_merchant", merchantArray));
        result.push_back(Pair("as_owner", ownerArray));

        return result;
    }
    else if(strCommand == "create")
    {
        if (params.size() < 5)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Expected format: tposcontract create tpos_address magic_string 5000 5");

        CBitcoinAddress tposAddress(params[1].get_str());
        std::string merchantMagicStr = DecodeBase64(params[2].get_str());
        CAmount amount = AmountFromValue(params[3]);
        int commission = std::stoi(params[4].get_str());

        if(!tposAddress.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "tpos address is not valid, won't continue");

        std::stringstream ss(merchantMagicStr);
        std::string hashId;
        std::string outpointIndex;
        if(!(ss >> hashId >> outpointIndex) || !IsHex(hashId))
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Malformed magic string");


        uint256 txHash(ParseHex(hashId));
        COutPoint merchantOutpoint(txHash, std::stoi(outpointIndex));
        CReserveKey reserveKey(pwalletMain);

        std::string strError;
        auto walletTx = TPoSUtils::CreateTPoSTransaction(pwalletMain, reserveKey,
                                                         tposAddress, amount,
                                                         merchantOutpoint, commission, strError);

        if(walletTx)
        {
            return EncodeHexTx(*walletTx);
        }
        else
        {
            return "Failed to create tpos transaction, reason: " + strError;
        }
    }
    else if(strCommand == "merchant-prepare")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Transaction id and outpoint index required for merchantnode address.");

        auto txId = ParseHashV(params[1], "parameter 1");
        std::string outpointIndexStr = params[2].get_str();

        int outpointIndex = std::stoi(outpointIndexStr);

        assert(pwalletMain != NULL);
        LOCK2(cs_main, pwalletMain->cs_wallet);

        auto walletTx = pwalletMain->GetWalletTx(txId);

        if (!walletTx)
            return "invalid hash tx id, this transaction is not recorded into wallet.";

        const CTxOut& txOut = walletTx->vout.at(outpointIndex);
        if (pwalletMain->IsMine(txOut) != ISMINE_SPENDABLE)
            return "coin is not spendable";

        if (pwalletMain->IsSpent(txId, outpointIndex))
            return "coin is already spent";

        return EncodeBase64(HexStr(txId) + " " + outpointIndexStr);
    }

    return NullUniValue;
}

#endif
