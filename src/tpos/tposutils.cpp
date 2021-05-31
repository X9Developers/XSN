#include <tpos/tposutils.h>

#include <wallet/wallet.h>
#include <utilmoneystr.h>
#include <policy/policy.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <tpos/merchantnode-sync.h>
#include <tpos/merchantnodeman.h>
#include <tpos/activemerchantnode.h>
#include <consensus/validation.h>
#include <messagesigner.h>
#include <spork.h>
#include <sstream>
#include <numeric>

static const std::string TPOSEXPORTHEADER("TPOSOWNERINFO");
static const int TPOSEXPORTHEADERWIDTH = 40;
static const int TPOS_CONTRACT_COLATERAL = 1 * COIN;

static std::string ParseAddressFromMetadata(std::string str)
{
    auto tposAddressRaw = ParseHex(str);
    std::string addressAsStr(tposAddressRaw.size(), '0');

    for(size_t i = 0; i < tposAddressRaw.size(); ++i)
        addressAsStr[i] = static_cast<char>(tposAddressRaw[i]);

    return addressAsStr;
}

bool TPoSUtils::IsTPoSContract(const CTransactionRef &tx)
{
    return TPoSContract::FromTPoSContractTx(tx).IsValid();
}

#ifdef ENABLE_WALLET

bool TPoSUtils::GetTPoSPayments(const CWallet *wallet,
                                const CTransactionRef &tx,
                                CAmount &stakeAmount,
                                CAmount &commissionAmount,
                                CTxDestination &tposAddress,
                                CTxDestination &merchantAddress)
{
    if(!tx->IsCoinStake())
        return false;

    CAmount nCredit = wallet->GetCredit(*tx, ISMINE_ALL);
    CAmount nDebit = wallet->GetDebit(*tx, ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;

    std::vector<TPoSContract> tposContracts;

    for(auto &&pair : wallet->tposOwnerContracts)
        tposContracts.emplace_back(pair.second);

    for(auto &&pair : wallet->tposMerchantContracts)
        tposContracts.emplace_back(pair.second);

    CTxDestination address;
    auto scriptKernel = tx->vout.at(1).scriptPubKey;
    commissionAmount = stakeAmount = 0;
    auto it = std::find_if(std::begin(tposContracts), std::end(tposContracts), [scriptKernel](const TPoSContract &entry) {
        return entry.scriptTPoSAddress == scriptKernel;
    });

    if(it != std::end(tposContracts))
    {
        auto merchantScript = it->scriptMerchantAddress;
        auto commissionIt = std::find_if(std::begin(tx->vout), std::end(tx->vout), [merchantScript](const CTxOut &txOut) {
            return txOut.scriptPubKey == merchantScript;
        });

        if(commissionIt != tx->vout.end())
        {
            stakeAmount = nNet;
            commissionAmount = commissionIt->nValue;
            ExtractDestination(it->scriptTPoSAddress, tposAddress);
            ExtractDestination(it->scriptMerchantAddress, merchantAddress);

            return true;
        }
    }

    return false;

}

bool TPoSUtils::IsTPoSMerchantContract(CWallet *wallet, const CTransactionRef &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);

    bool isMerchantNode = contract.scriptMerchantAddress ==
            GetScriptForDestination(activeMerchantnode.pubKeyMerchantnode.GetID());

    CTxDestination dest;
    ExtractDestination(contract.scriptMerchantAddress, dest);

    return contract.IsValid() && (isMerchantNode ||
                                  IsMine(*wallet, dest) == ISMINE_SPENDABLE);
}

bool TPoSUtils::IsTPoSOwnerContract(CWallet *wallet, const CTransactionRef &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);

    CTxDestination dest;
    ExtractDestination(contract.scriptTPoSAddress, dest);

    return contract.IsValid() &&
            IsMine(*wallet, dest) == ISMINE_SPENDABLE;
}

bool TPoSUtils::CreateTPoSTransaction(CWallet *wallet,
                                      CTransactionRef &transactionOut,
                                      CReserveKey& reservekey,
                                      const CTxDestination &tposAddress,
                                      const CTxDestination &merchantAddress,
                                      int merchantCommission,
                                      std::string &strError)
{
    if (wallet->IsLocked()) {
        strError = "Error: Wallet is locked";
        return false;
    }

    CAmount nFeeRequired;

    CMutableTransaction baseTx;
    if(!CreateTPoSTransaction(baseTx, tposAddress, merchantAddress, merchantCommission, strError)) {
        return false;
    }

    std::vector<CRecipient> vecSend;

    for (auto &&out : baseTx.vout) {
        vecSend.push_back(CRecipient {out.scriptPubKey, out.nValue, false});
    }

    int nChangePos = -1;
    if (!wallet->CreateTransaction(vecSend, transactionOut, reservekey, nFeeRequired, nChangePos, strError, {}, false)) {
        if (TPOS_CONTRACT_COLATERAL + nFeeRequired > wallet->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("Error() : %s\n", strError);
        return false;
    }

    baseTx = *transactionOut;

    if (!SignTPoSContract(baseTx, wallet, TPoSContract::FromTPoSContractTx(transactionOut))) {
        strError = "Failed to sign tpos contract";
        return false;
    }

    if (!strError.empty()) {
        return false;
    }

    LOCK2(cs_main, wallet->cs_wallet);
    if(!wallet->SignTransaction(baseTx)) {
        strError = "Failed to sign transaction after filling";
        return false;
    }

    std::string reason;
    if (!IsStandardTx(baseTx, reason, true)) {
        strError = strprintf("Error: Not standard tx: %s\n", reason.c_str());
        LogPrintf(strError.c_str());
        return false;
    }

    // swap signed tx with partial
    transactionOut = MakeTransactionRef(baseTx);

    return true;
}

bool TPoSUtils::CreateCancelContractTransaction(CWallet *wallet, CTransactionRef &txOut, CReserveKey &reserveKey, const TPoSContract &contract, string &strError)
{
    if (wallet->IsLocked()) {
        strError = "Error: Wallet is locked";
        return false;
    }

    COutPoint prevOutpoint = GetContractCollateralOutpoint(contract);
    if (prevOutpoint.IsNull()) {
        strError = "Error: Contract collateral is invalid";
        return false;
    }

    Coin coin;
    if (!pcoinsTip->GetCoin(prevOutpoint, coin) || coin.IsSpent()) {
        strError = "Error: Collateral is already spent";
        return false;
    }

    auto &prevOutput = contract.txContract->vout.at(prevOutpoint.n);

    CAmount nFeeRet;
    int nChangePosRet;
    CCoinControl coinControl;
    coinControl.nCoinType = ONLY_MERCHANTNODE_COLLATERAL;
    coinControl.Select(prevOutpoint);
    if (!wallet->CreateTransaction({ { prevOutput.scriptPubKey, prevOutput.nValue, true } }, txOut,
                                   reserveKey, nFeeRet, nChangePosRet,
                                   strError, coinControl, true)) {
        LogPrintf("Error() : %s\n", strError.c_str());
        return false;
    }

    return true;
}

#endif

COutPoint TPoSUtils::GetContractCollateralOutpoint(const TPoSContract &contract)
{
    COutPoint result;
    if (!contract.txContract) {
        return result;
    }

    const auto &vout = contract.txContract->vout;
    for (size_t i = 0; i < vout.size(); ++i) {
        if(vout[i].scriptPubKey == contract.scriptTPoSAddress &&
                vout[i].nValue == TPOS_CONTRACT_COLATERAL) {
            result = COutPoint(contract.txContract->GetHash(), i);
            break;
        }
    }

    return result;
}

bool TPoSUtils::CheckContract(const uint256 &hashContractTx, TPoSContract &contract, int nBlockHeight, bool fCheckSignature, bool fCheckContractOutpoint, std::string &strError)
{
    CTransactionRef tx;
    uint256 hashBlock;
    if (!GetTransaction(hashContractTx, tx, Params().GetConsensus(), hashBlock, true)) {
        strError = strprintf("%s : failed to get transaction for tpos contract %s", __func__,
                             hashContractTx.ToString());

        return error(strError.c_str());
    }

    return CheckContract(tx, contract, nBlockHeight, fCheckSignature, fCheckContractOutpoint, strError);

}

bool TPoSUtils::CheckContract(const CTransactionRef &txContract, TPoSContract &contract, int nBlockHeight, bool fCheckSignature, bool fCheckContractOutpoint, std::string &strError)
{
    TPoSContract tmpContract = TPoSContract::FromTPoSContractTx(txContract);

    if (!tmpContract.IsValid()) {
        strError = "CheckContract() : invalid transaction for tpos contract";
        return error(strError.c_str());
    }

    if(fCheckSignature)
    {
        if (tmpContract.txContract->vin.empty()) {
            return false;
        }

        auto hashMessage = SerializeHash(tmpContract.txContract->vin.front().prevout);
        std::string strVerifyHashError;

        CTxDestination tposAddress;
        if(!ExtractDestination(tmpContract.scriptTPoSAddress, tposAddress)) {
            strError = strprintf("%s : TPoS contract invalid tpos address", __func__);
            return error(strError.c_str());
        }

        if (IsTPoSNewSignaturesHardForkActivated(nBlockHeight)) {
            auto prevout = tmpContract.txContract->vin.front().prevout;
            std::string newHashMessage = prevout.hash.ToString() + ":" + std::to_string(prevout.n);
            if(!CMessageSigner::VerifyMessage(tposAddress, tmpContract.vchSig, newHashMessage, strVerifyHashError)) {
                if(!CHashSigner::VerifyHash(hashMessage, tposAddress, tmpContract.vchSig, strVerifyHashError)) {
                    strError = strprintf("%s : TPoS contract signature is invalid %s", __func__, strVerifyHashError);
                    return error(strError.c_str());
                }
            }
        } else {
            if(!CHashSigner::VerifyHash(hashMessage, tposAddress, tmpContract.vchSig, strVerifyHashError)) {
                strError = strprintf("%s : TPoS contract signature is invalid %s", __func__, strVerifyHashError);
                return error(strError.c_str());
            }
        }
    }

    if(fCheckContractOutpoint)
    {
        auto tposContractOutpoint = TPoSUtils::GetContractCollateralOutpoint(tmpContract);
        Coin coin;
        if(!pcoinsTip->GetCoin(tposContractOutpoint, coin) || coin.IsSpent())
        {
            strError = "CheckContract() : tpos contract invalid, collateral is spent";
            return error(strError.c_str());
        }
    }

    contract = tmpContract;

    return true;
}

bool TPoSUtils::IsMerchantPaymentValid(CValidationState &state, const CBlock &block, int nBlockHeight, CAmount expectedReward, CAmount actualReward)
{
    auto contract = TPoSContract::FromTPoSContractTx(block.txTPoSContract);

    const auto &coinstake = block.vtx[1];

    CTxDestination tposAddress;
    ExtractDestination(contract.scriptTPoSAddress, tposAddress);

    CTxDestination merchantAddress;
    ExtractDestination(contract.scriptMerchantAddress, merchantAddress);

    if(coinstake->vout[1].scriptPubKey != contract.scriptTPoSAddress)
    {
        CTxDestination dest;
        if(!ExtractDestination(coinstake->vout[1].scriptPubKey, dest)) {
            return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: coinstake extract destination failed"), REJECT_INVALID, "bad-merchant-payee");
        }

        // ban him, something is incorrect completely
        return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: coinstake is invalid expected: %s, actual %s\n",
                                    EncodeDestination(tposAddress).c_str(), EncodeDestination(dest).c_str()), REJECT_INVALID, "bad-merchant-payee");
    }

    CAmount merchantPayment = 0;
    auto scriptMerchantPubKey = contract.scriptMerchantAddress;
    merchantPayment = std::accumulate(std::begin(coinstake->vout) + 2, std::end(coinstake->vout), CAmount(0), [scriptMerchantPubKey](CAmount accum, const CTxOut &txOut) {
            return txOut.scriptPubKey == scriptMerchantPubKey ? accum + txOut.nValue : accum;
});

    if(merchantPayment > 0) {
        auto maxAllowedValue = GetOperatorPayment(expectedReward, contract.nOperatorReward);
        // ban, we know fur sure that merchant tries to get more than he is allowed
        if(merchantPayment > maxAllowedValue) {
            return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: merchant was paid more than allowed: %s\n", EncodeDestination(merchantAddress).c_str()),
                             REJECT_INVALID, "bad-merchant-payee");
        }
    }

    if(!merchantnodeSync.IsSynced())
    {
        //there is no merchant node info to check anything, let's just accept the longest chain
        //        if(fDebug)
        LogPrintf("IsMerchantPaymentValid -- WARNING: Client not synced, skipping block payee checks\n");

        return true;
    }

    if(!sporkManager.IsSporkActive(Spork::SPORK_15_TPOS_ENABLED))
    {
        return state.DoS(0, error("IsBlockPayeeValid -- ERROR: Invalid merchantnode payment detected at height %d\n", nBlockHeight),
                         REJECT_INVALID, "bad-merchant-payee", true);
    }

    CKeyID coinstakeKeyID;
    // legacy way supports only P2PKH, after HF support both P2PKH and P2WPKH
    if (IsTPoSNewSignaturesHardForkActivated(nBlockHeight)) {
        coinstakeKeyID = GetKeyForDestination(CBasicKeyStore{}, merchantAddress);
        if (coinstakeKeyID.IsNull()) {
            return state.DoS(0, error("IsMerchantPaymentValid -- ERROR: coin stake was paid to invalid address\n"),
                             REJECT_INVALID, "bad-merchant-payee", true);
        }
    } else {
        if (auto keyID = boost::get<CKeyID>(&merchantAddress)) {
            coinstakeKeyID = *keyID;
        } else {
            return state.DoS(0, error("IsMerchantPaymentValid -- ERROR: coin stake was paid to invalid address\n"),
                             REJECT_INVALID, "bad-merchant-payee", true);
        }
    }

    CMerchantnode merchantNode;
    if (!merchantnodeman.Get(coinstakeKeyID, merchantNode)) {
        return state.DoS(0, error("IsMerchantPaymentValid -- ERROR: failed to find merchantnode with address: %s\n", EncodeDestination(merchantAddress).c_str()),
                         REJECT_INVALID, "bad-merchant-payee", true);
    }

    if (merchantNode.hashTPoSContractTx != block.hashTPoSContractTx) {
        return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: merchantnode contract is invalid expected: %s, actual %s\n",
                                    block.hashTPoSContractTx.ToString().c_str(), merchantNode.hashTPoSContractTx.ToString().c_str()),
                         REJECT_INVALID, "bad-merchant-payee");
    }

    if (!merchantNode.IsValidForPayment()) {
        return state.DoS(0, error("IsMerchantPaymentValid -- ERROR: merchantnode with address: %s is not valid for payment\n", EncodeDestination(merchantAddress).c_str()),
                         REJECT_INVALID, "bad-merchant-payee", true);
    }

    return true;
}

std::vector<unsigned char> TPoSUtils::GenerateContractPayload(const TPoSContract &contract)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << contract;
    std::vector<unsigned char> payload(ds.begin(), ds.end());
    return payload;
}

CScript TPoSUtils::GenerateLegacyContractScript(const CTxDestination &tposAddress,
                                                const CTxDestination &merchantAddress,
                                                uint16_t nOperatorReward,
                                                const std::vector<unsigned char> &vchSignature)
{
    CScript metadataScriptPubKey;
    auto tposAddressAsStr = EncodeDestination(tposAddress);
    auto merchantAddressAsStr = EncodeDestination(merchantAddress);
    metadataScriptPubKey << OP_RETURN
                         << std::vector<unsigned char>(tposAddressAsStr.begin(), tposAddressAsStr.end())
                         << std::vector<unsigned char>(merchantAddressAsStr.begin(), merchantAddressAsStr.end())
                         << (100 - nOperatorReward)
                         << vchSignature;
    return metadataScriptPubKey;
}

bool TPoSUtils::SignTPoSContract(CMutableTransaction &tx, CKeyStore *keystore, TPoSContract contract)
{
    auto it = std::find_if(tx.vout.begin(), tx.vout.end(), [](const CTxOut &txOut) {
        return txOut.scriptPubKey.IsUnspendable();
    });

    CTxDestination tposAddress, merchantAddress;
    ExtractDestination(contract.scriptTPoSAddress, tposAddress);
    ExtractDestination(contract.scriptMerchantAddress, merchantAddress);
    auto keyID = GetKeyForDestination(CBasicKeyStore{}, tposAddress);

    CKey key;
    if (!keystore->GetKey(keyID, key)) {
        return false;
    }

    if (it != tx.vout.end()) {
        auto hashMessage = SerializeHash(tx.vin.front().prevout);

        if (contract.nVersion == 1) {
            if (!key.SignCompact(hashMessage, contract.vchSig)) {
                return false;
            }

            it->scriptPubKey = GenerateLegacyContractScript(tposAddress, merchantAddress, contract.nOperatorReward, contract.vchSig);
            return true;
        } else if (contract.nVersion == 2) {

            auto prevout = tx.vin.front().prevout;
            std::string newHashMessage = prevout.hash.ToString() + ":" + std::to_string(prevout.n);
            auto type = contract.scriptTPoSAddress.IsPayToPublicKeyHash() ? CPubKey::InputScriptType::SPENDP2PKH : CPubKey::InputScriptType::SPENDWITNESS;
            if (!CMessageSigner::SignMessage(newHashMessage, contract.vchSig, key, type)) {
                return false;
            }

            it->scriptPubKey.clear();
            it->scriptPubKey << OP_RETURN << GenerateContractPayload(contract);
            return true;
        }
    }

    return false;
}

bool TPoSUtils::CreateTPoSTransaction(CMutableTransaction &txOut, const CTxDestination &tposAddress,
                                      const CTxDestination &merchantAddress, int nOperatorReward, std::string &strError)
{
    CKey key;
    CKeyID keyID;

    keyID = GetKeyForDestination(CBasicKeyStore{}, tposAddress);
    if (keyID.IsNull()) {
        strError = "Error: TPoS Address is not supported";
        return false;
    }

    // dummy signature, just to know size
    std::vector<unsigned char> vchSignature(CPubKey::COMPACT_SIGNATURE_SIZE, '0');

    CScript metadataScriptPubKey;
    metadataScriptPubKey << OP_RETURN
                         << GenerateContractPayload(TPoSContract({}, merchantAddress,
                                                                 tposAddress, nOperatorReward,
                                                                 vchSignature));

    txOut = CMutableTransaction{};
    txOut.vout.emplace_back(0, metadataScriptPubKey);
    txOut.vout.emplace_back(TPOS_CONTRACT_COLATERAL, GetScriptForDestination(tposAddress));

    return true;
}

CAmount TPoSUtils::GetOperatorPayment(CAmount basePayment, int nOperatorReward)
{
    return (basePayment / 100) * nOperatorReward;
}

CAmount TPoSUtils::GetOwnerPayment(CAmount basePayment, int nOperatorReward)
{
    return (basePayment / 100) * (100 - nOperatorReward);
}

TPoSContract::TPoSContract(CTransactionRef tx, CTxDestination merchantAddress, CTxDestination tposAddress, uint16_t nOperatorReward, std::vector<unsigned char> vchSignature)
{
    this->txContract = tx;

    CBasicKeyStore dummyKeystore;

    if (!GetKeyForDestination(dummyKeystore, merchantAddress).IsNull()) {
        this->scriptMerchantAddress = GetScriptForDestination(merchantAddress);
    }

    if (!GetKeyForDestination(dummyKeystore, tposAddress).IsNull()) {
        this->scriptTPoSAddress = GetScriptForDestination(tposAddress);
    }

    this->vchSig = vchSignature;
    this->nOperatorReward = nOperatorReward;
}

bool TPoSContract::IsValid() const
{
    if (txContract && !txContract->IsNull() && !scriptMerchantAddress.empty() && !scriptTPoSAddress.empty()) {
        switch(nVersion) {
        case 1: return nOperatorReward > 0 && nOperatorReward < 100; // legacy contract
        case 2: return nOperatorReward >= 0 && nOperatorReward <= 100; // new contract
        default:
            break;
        }
    }
    return false;
}

TPoSContract TPoSContract::FromTPoSContractTx(const CTransactionRef tx)
{
    try {
        if(tx->vout.size() >= 2 && tx->vout.size() <= 3) {
            const CTxOut *metadataOutPtr = nullptr;
            bool colateralFound = false;
            for(const CTxOut &txOut : tx->vout) {
                if (txOut.scriptPubKey.IsUnspendable()) {
                    metadataOutPtr = &txOut;
                } else if(txOut.nValue == TPOS_CONTRACT_COLATERAL) {
                    colateralFound = true;
                }
            }

            if (metadataOutPtr && colateralFound) {
                const auto &metadataOut = *metadataOutPtr;
                std::vector<std::vector<unsigned char>> vSolutions;
                txnouttype whichType;
                if (Solver(metadataOut.scriptPubKey, whichType, vSolutions) && whichType == TX_NULL_DATA) {
                    // Here we can have a chance that it is transaction which is a tpos contract, let's check if it has
                    std::stringstream stringStream(metadataOut.scriptPubKey.ToString());

                    std::vector<std::string> tokens;
                    std::string token;
                    while(stringStream >> token) {
                        tokens.emplace_back(token);
                    }

                    if(tokens.at(0) == GetOpName(OP_RETURN)) {

                        if (tokens.size() == 2) {
                            // check for new contract
                            CDataStream ds(ParseHex(tokens.at(1)), SER_NETWORK, PROTOCOL_VERSION);
                                    TPoSContract contract;
                            ds >> contract;
                            contract.txContract = tx;
                            if (contract.IsValid()) {
                                return contract;
                            }
                        } else if (tokens.size() > 3 && tokens.size() < 6) {

                            CBitcoinAddress tposAddress(ParseAddressFromMetadata(tokens[1]));
                            CBitcoinAddress merchantAddress(ParseAddressFromMetadata(tokens[2]));
                            int nStakePercentage = std::stoi(tokens[3]);
                            std::vector<unsigned char> vchSignature;

                            if (tokens.size() > 4) {
                                vchSignature = ParseHex(tokens[4]);
                            }

                            // legacy contract
                            TPoSContract contract(tx,
                                                  merchantAddress.Get(),
                                                  tposAddress.Get(),
                                                  (100 - nStakePercentage), vchSignature);
                            contract.nVersion = 1; // legacy version

                            if(contract.IsValid()) {
                                return contract;
                            }
                        }
                    }
                }
            }
        }
    } catch(std::exception &ex) {
        LogPrintf("Failed to parse tpos which had to be tpos, %s\n", ex.what());
    }

    return TPoSContract{};
}

bool IsTPoSNewSignaturesHardForkActivated(int nChainHeight)
{
    return nChainHeight >= Params().GetConsensus().nTPoSSignatureUpgradeHFHeight;
}
