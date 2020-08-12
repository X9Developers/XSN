// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tpos/activemerchantnode.h>
#include <key_io.h>
#include <init.h>
#include <netbase.h>
#include <validation.h>
#include <tpos/merchantnode-sync.h>
#include <tpos/merchantnodeman.h>
#include <tpos/merchantnode.h>
#include <tpos/merchantnodeconfig.h>
#include <rpc/server.h>
#include <util.h>
#include <utilmoneystr.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif
#include <core_io.h>
#include <key_io.h>

#include <fstream>
#include <iomanip>
#include <univalue.h>

static UniValue merchantsync(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw runtime_error(
                "merchantsync [status|next|reset]\n"
                "Returns the sync status, updates to the next step or resets it entirely.\n"
                );

    std::string strMode = request.params[0].get_str();

    if(strMode == "status") {
        UniValue objStatus(UniValue::VOBJ);
        objStatus.push_back(Pair("AssetID", merchantnodeSync.GetAssetID()));
        objStatus.push_back(Pair("AssetName", merchantnodeSync.GetAssetName()));
        objStatus.push_back(Pair("AssetStartTime", merchantnodeSync.GetAssetStartTime()));
        objStatus.push_back(Pair("Attempt", merchantnodeSync.GetAttempt()));
        objStatus.push_back(Pair("IsBlockchainSynced", merchantnodeSync.IsBlockchainSynced()));
        objStatus.push_back(Pair("IsMasternodeListSynced", merchantnodeSync.IsMerchantnodeListSynced()));
        objStatus.push_back(Pair("IsSynced", merchantnodeSync.IsSynced()));
        objStatus.push_back(Pair("IsFailed", merchantnodeSync.IsFailed()));
        return objStatus;
    }

    if(strMode == "next")
    {
        merchantnodeSync.SwitchToNextAsset(*g_connman);
        return "sync updated to " + merchantnodeSync.GetAssetName();
    }

    if(strMode == "reset")
    {
        merchantnodeSync.Reset();
        merchantnodeSync.SwitchToNextAsset(*g_connman);
        return "success";
    }
    return "failure";
}


static UniValue ListOfMerchantNodes(const UniValue& params, std::set<CService> myMerchantNodesIps, bool showOnlyMine)
{
    std::string strMode = "status";
    std::string strFilter = "";

    if (params.size() >= 1) strMode = params[0].get_str();
    if (params.size() == 2) strFilter = params[1].get_str();

    UniValue obj(UniValue::VOBJ);

    auto mapMerchantnodes = merchantnodeman.GetFullMerchantnodeMap();
    for (auto& mnpair : mapMerchantnodes) {

        if(showOnlyMine && myMerchantNodesIps.count(mnpair.second.addr) == 0) {
            continue;
        }

        CMerchantnode mn = mnpair.second;
        std::string strOutpoint = HexStr(mnpair.first.GetID().ToString());
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
                          mn.hashTPoSContractTx.ToString() << " " <<
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

static UniValue merchantnodelist(const JSONRPCRequest& request)
{
    std::string strMode = "status";
    std::string strFilter = "";

    if (request.params.size() >= 1) strMode = request.params[0].get_str();
    if (request.params.size() == 2) strFilter = request.params[1].get_str();

    if (request.fHelp || (
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
                    "  payee          - Print XSN address associated with a merchantnode (can be additionally filtered,\n"
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

    std::set<CService> myMerchantNodesIps;
    return  ListOfMerchantNodes(request.params, myMerchantNodesIps, false);
}

static UniValue merchantnode(const JSONRPCRequest& request)
{
#ifdef ENABLE_WALLET
    auto pwallet = GetWalletForJSONRPCRequest(request);
#endif
    std::string strCommand;
    if (request.params.size() >= 1) {
        strCommand = request.params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (request.fHelp  ||
            (
            #ifdef ENABLE_WALLET
                strCommand != "start-alias" && strCommand != "start-all" && strCommand != "start-missing" &&
                strCommand != "start-disabled" && strCommand != "outputs" &&
            #endif // ENABLE_WALLET
                strCommand != "list" && strCommand != "list-conf" && strCommand != "list-mine" && strCommand != "count" &&
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
                "  list-mine    - Print own nodes"
                "  winner       - Print info on next merchantnode winner to vote for\n"
                "  winners      - Print list of merchantnode winners\n"
                );

    if (strCommand == "list")
    {
        UniValue newParams(UniValue::VARR);
        // forward request.params but skip "list"
        for (unsigned int i = 1; i < request.params.size(); i++) {
            newParams.push_back(request.params[i]);
        }

        auto newRequest = request;
        newRequest.params = newParams;

        return merchantnodelist(newRequest);
    }

    if(strCommand == "list-mine")
    {
        UniValue newParams(UniValue::VARR);
        // forward request.params but skip "list-mine"
        for (unsigned int i = 1; i < request.params.size(); i++) {
            newParams.push_back(request.params[i]);
        }

        std::set<CService> myMerchantNodesIps;
        for(auto &&mne : merchantnodeConfig.getEntries())
        {
            CService service;
            Lookup(mne.getIp().c_str(), service, 0, false);

            myMerchantNodesIps.insert(service);
        }

        return  ListOfMerchantNodes(newParams, myMerchantNodesIps, true);
    }

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VARR);

        for(auto &&mne : merchantnodeConfig.getEntries())
        {
            CMerchantnode mn;
            CKey privKey = DecodeSecret(mne.getMerchantPrivKey());
            CPubKey pubKey = privKey.GetPubKey();
            bool fFound = merchantnodeman.Get(pubKey, mn);

            std::string strStatus = fFound ? mn.GetStatus() : "MISSING";

            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getMerchantPrivKey()));
            mnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(mnObj);
        }

        return resultObj;
    }

    if(strCommand == "connect")
    {
        if (request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Merchantnode address required");

        std::string strAddress = request.params[1].get_str();

        CService addr;
        if (!Lookup(strAddress.c_str(), addr, 0, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect merchantnode address %s", strAddress));

        // TODO: Pass CConnman instance somehow and don't use global variable.
        CNode *pnode = g_connman->OpenMasternodeConnection(CAddress(addr, NODE_NETWORK));
        if(!pnode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to merchantnode %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count")
    {
        if (request.params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        if (request.params.size() == 1)
            return merchantnodeman.size();

        std::string strMode = request.params[1].get_str();

        if (strMode == "ps")
            return merchantnodeman.CountEnabled();

        if (strMode == "enabled")
            return merchantnodeman.CountEnabled();


        if (strMode == "all")
            return strprintf("Total: %d (PS Compatible: %d / Enabled: %d)",
                             merchantnodeman.size(), merchantnodeman.CountEnabled(),
                             merchantnodeman.CountEnabled());
    }
#ifdef ENABLE_WALLET
    if (strCommand == "start-alias")
    {
        if (request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwallet->cs_wallet);
            EnsureWalletIsUnlocked(pwallet);
        }

        std::string strAlias = request.params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", strAlias));

        for(auto && mrne : merchantnodeConfig.getEntries()) {
            if(mrne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CMerchantnodeBroadcast mnb;

                bool fResult = CMerchantnodeBroadcast::Create(mrne.getIp(), mrne.getMerchantPrivKey(),
                                                              mrne.getContractTxID(), strError, mnb);

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
    }
#endif

    if (strCommand == "status")
    {
        if (!fMerchantNode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a merchantnode");

        UniValue mnObj(UniValue::VOBJ);

        mnObj.push_back(Pair("pubkey", activeMerchantnode.pubKeyMerchantnode.GetID().ToString()));
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

static bool DecodeHexVecMnb(std::vector<CMerchantnodeBroadcast>& vecMnb, std::string strHexMnb) {

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

UniValue merchantsentinelping(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
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

    //    activeMerchantnode.UpdateSentinelPing(StringVersionToInt(request.params[0].get_str()));
    return true;
}

#ifdef ENABLE_WALLET
static UniValue tposcontract(const JSONRPCRequest& request)
{
    auto pwallet = GetWalletForJSONRPCRequest(request);
    std::string strCommand;
    if (request.params.size() >= 1) {
        strCommand = request.params[0].get_str();
    }

    if (request.fHelp  || (strCommand != "list" && strCommand != "create" && strCommand != "refresh" && strCommand != "cleanup" && strCommand != "validate"))
        throw std::runtime_error(
                "tposcontract \"command\"...\n"
                "Set of commands to execute merchantnode related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  create           - Create tpos transaction\n"
                "  list             - Print list of all tpos contracts that you are owner or merchant\n"
                "  refresh          - Refresh tpos contract for merchant to fetch all coins from blockchain.\n"
                "  cleanup          - Cleanup old entries of tpos contract.\n"
                "  validate         - Validates transaction checking if it's a valid contract"
                );


    if (strCommand == "list")
    {
        UniValue result(UniValue::VOBJ);
        UniValue merchantArray(UniValue::VARR);
        UniValue ownerArray(UniValue::VARR);

        auto parseContract = [](const TPoSContract &contract) {
            UniValue object(UniValue::VOBJ);

            object.push_back(Pair("txid", contract.rawTx->GetHash().ToString()));
            object.push_back(Pair("tposAddress", contract.tposAddress.ToString()));
            object.push_back(Pair("merchantAddress", contract.merchantAddress.ToString()));
            object.push_back(Pair("commission", 100 - contract.stakePercentage)); // show merchant commission
            if(contract.vchSignature.empty())
                object.push_back(Pair("deprecated", true));

            return object;
        };

        for(auto &&it : pwallet->tposMerchantContracts)
        {
            merchantArray.push_back(parseContract(it.second));
        }

        for(auto &&it : pwallet->tposOwnerContracts)
        {
            ownerArray.push_back(parseContract(it.second));
        }

        result.push_back(Pair("as_merchant", merchantArray));
        result.push_back(Pair("as_owner", ownerArray));

        return result;
    }
    else if(strCommand == "create")
    {
        if (request.params.size() < 4)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Expected format: tposcontract create tpos_address merchant_address commission");

        CBitcoinAddress tposAddress(request.params[1].get_str());
        CBitcoinAddress merchantAddress(request.params[2].get_str());
        int commission = std::stoi(request.params[3].get_str());

        if(!tposAddress.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "tpos address is not valid, won't continue");

        if(!merchantAddress.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "merchant address is not valid, won't continue");

        CReserveKey reserveKey(pwallet);

        std::string strError;
        auto transaction = MakeTransactionRef();

        if(TPoSUtils::CreateTPoSTransaction(pwallet, transaction,
                                            reserveKey, tposAddress,
                                            merchantAddress, commission, strError))
        {
            return EncodeHexTx(*transaction);
        }
        else
        {
            return "Failed to create tpos transaction, reason: " + strError;
        }
    }
    else if(strCommand == "refresh")
    {
        if(request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Expected format: tposcontract refresh tposcontract_id");

        auto it = pwallet->tposMerchantContracts.find(ParseHashV(request.params[1], "tposcontractid"));
        if(it == std::end(pwallet->tposMerchantContracts))
            return JSONRPCError(RPC_INVALID_PARAMETER, "No merchant tpos contract found");

        WalletRescanReserver reserver(pwallet);

        if (!reserver.reserve()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
        }

        pwallet->ScanForWalletTransactions(chainActive.Genesis(), chainActive.Tip(), reserver, true);
        pwallet->ReacceptWalletTransactions();
    }
    else if(strCommand == "cleanup")
    {
        if(request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Expected format: tposcontract cleanup tposcontract_id");

        auto tposContractHashID = ParseHashV(request.params[1], "tposcontractid");

        auto it = pwallet->tposMerchantContracts.find(tposContractHashID);
        if(it == std::end(pwallet->tposMerchantContracts))
            return "No merchant tpos contract found";

        CTransactionRef tx;
        uint256 hashBlock;
        if(!GetTransaction(tposContractHashID, tx, Params().GetConsensus(), hashBlock, true))
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Failed to get transaction for tpos contract ");
        }

        TPoSContract tmpContract = TPoSContract::FromTPoSContractTx(tx);

        if(!tmpContract.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Contract is invalid");

        pwallet->RemoveWatchOnly(GetScriptForDestination(tmpContract.tposAddress.Get()));
        pwallet->RemoveTPoSContract(tposContractHashID);
    }
    else if(strCommand == "validate")
    {

        if(request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Expected format: tposcontract validate { \"hex\" : \"hex_encoded_transaction\", "
                               "\n                                       \"check_spent\" : 1, (optional, default 1)"
                               "\n                                       \"check_signature\" : 1 (optional, default 1) } OR "
                               "\n                                     { \"txid\" : \"txid\", "
                               "\n                                       \"check_spent\" : 1, (optional, default 1)"
                               "\n                                       \"check_signature\" : 1 (optional, default 1) }");

        UniValue obj;
        obj.read(request.params[1].get_str());

        const UniValue &hexObj = find_value(obj, "hex");
        const UniValue &txIdObj = find_value(obj, "txid");
        const UniValue &checkSpentObj = find_value(obj, "check_spent");
        const UniValue &checkSignatureObj = find_value(obj, "check_signature");

        bool fCheckSignature = true;
        bool fCheckSpent = true;
        bool fCheckResult = false;

        if(checkSpentObj.isNum())
        {
            fCheckSpent = checkSpentObj.get_int() != 0;
        }

        if(checkSignatureObj.isNum())
        {
            fCheckSignature = checkSignatureObj.get_int() != 0;
        }

        std::string strError;
        TPoSContract contract;

        if(!hexObj.isNull())
        {
            // parse hex string from parameter
            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, hexObj.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            CTransactionRef tx(MakeTransactionRef(std::move(mtx)));

            fCheckResult = TPoSUtils::CheckContract(tx, contract, chainActive.Tip()->nHeight, fCheckSignature, fCheckSpent, strError);
        }
        else if(!txIdObj.isNull())
        {
            fCheckResult = TPoSUtils::CheckContract(ParseHashStr(txIdObj.get_str(), "txid"), contract, chainActive.Tip()->nHeight, fCheckSignature, fCheckSpent, strError);
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        }

        if(fCheckResult)
        {
            return "Contract is valid";
        }
        else
        {
            return strprintf("Contract invalid, error: %s", strError);
        }
    }

    return NullUniValue;
}

#endif

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
  { "merchantnode",            "merchantnode",            &merchantnode,            {"command"} }, /* uses wallet if enabled */
  { "merchantnode",            "merchantnodelist",        &merchantnodelist,        {"mode", "filter"} },
  #ifdef ENABLE_WALLET
  { "merchantnode",            "tposcontract",            &tposcontract,            {"command"} },
  #endif
  { "merchantnode",            "merchantsync",            &merchantsync,            {"command"} },
};

void RegisterMerchantnodeCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

