#include <tpos/tposutils.h>

#include <wallet/wallet.h>
#include <utilmoneystr.h>
#include <policy/policy.h>
#include <validation.h>
#include <tpos/merchantnode-sync.h>
#include <tpos/merchantnodeman.h>
#include <tpos/activemerchantnode.h>
#include <consensus/validation.h>
#include <spork.h>
#include <sstream>
#include <numeric>

static const std::string TPOSEXPORTHEADER("TPOSOWNERINFO");
static const int TPOSEXPORTHEADERWIDTH = 40;

static const int TPOS_CONTRACT_COLATERAL = 1 * COIN;

std::string ParseAddressFromMetadata(std::string str)
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
                                const CWalletTx &wtx,
                                CAmount &stakeAmount,
                                CAmount &commissionAmount,
                                CXSNAddress &tposAddress,
                                CXSNAddress &merchantAddress)
{
    const auto &tx = wtx.tx;
    if(!tx->IsCoinStake())
        return false;

    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;

    std::vector<TPoSContract> tposContracts;

    for(auto &&pair : wallet->tposOwnerContracts)
        tposContracts.emplace_back(pair.second);

    for(auto &&pair : wallet->tposMerchantContracts)
        tposContracts.emplace_back(pair.second);

    CTxDestination address;
    auto scriptKernel = tx->vout.at(1).scriptPubKey;
    commissionAmount = stakeAmount = 0;
    if(ExtractDestination(scriptKernel, address))
    {
        CXSNAddress tmpAddress(address);

        auto it = std::find_if(std::begin(tposContracts), std::end(tposContracts), [tmpAddress](const TPoSContract &entry) {
            return entry.tposAddress == tmpAddress;
        });

        if(it != std::end(tposContracts))
        {
            auto merchantScript = GetScriptForDestination(it->merchantAddress.Get());
            auto commissionIt = std::find_if(std::begin(tx->vout), std::end(tx->vout), [merchantScript](const CTxOut &txOut) {
                return txOut.scriptPubKey == merchantScript;
            });

            if(commissionIt != tx->vout.end())
            {
                stakeAmount = nNet;
                commissionAmount = commissionIt->nValue;
                tposAddress = tmpAddress;
                merchantAddress = it->merchantAddress;

                return true;
            }
        }
    }

    return false;

}

bool TPoSUtils::IsTPoSMerchantContract(CWallet *wallet, const CTransactionRef &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);

    bool IsMerchantNode = GetScriptForDestination(contract.merchantAddress.Get()) ==
            GetScriptForDestination(activeMerchantnode.pubKeyMerchantnode.GetID());

    return contract.IsValid() && (IsMerchantNode ||
                                  IsMine(*wallet, contract.merchantAddress.Get()) == ISMINE_SPENDABLE);
}

bool TPoSUtils::IsTPoSOwnerContract(CWallet *wallet, const CTransactionRef &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);

    return contract.IsValid() &&
            IsMine(*wallet, contract.tposAddress.Get()) == ISMINE_SPENDABLE;
}

std::unique_ptr<CWalletTx> TPoSUtils::CreateTPoSTransaction(CWallet *wallet,
                                                            CReserveKey& reservekey,
                                                            const CXSNAddress &tposAddress,
                                                            const CXSNAddress &merchantAddress,
                                                            int merchantCommission,
                                                            std::string &strError)
{
#if 0
    std::unique_ptr<CWalletTx> result(new CWalletTx);
    CWalletTx &wtxNew = *result;

    auto tposAddressAsStr = tposAddress.ToString();
    auto merchantAddressAsStr = merchantAddress.ToString();

    CScript metadataScriptPubKey;
    metadataScriptPubKey << OP_RETURN
                         << std::vector<unsigned char>(tposAddressAsStr.begin(), tposAddressAsStr.end())
                         << std::vector<unsigned char>(merchantAddressAsStr.begin(), merchantAddressAsStr.end())
                         << (100 - merchantCommission);


    if(wallet->IsLocked())
    {
        strError = "Error: Wallet is locked";
        return nullptr;
    }

    CKey key;
    CKeyID keyID;
    if(!tposAddress.GetKeyID(keyID))
    {
        strError = "Error: TPoS Address is not P2PKH";
        return nullptr;
    }
    if (!wallet->GetKey(keyID, key))
    {
        strError = "Error: Failed to get private key associated with TPoS address";
        return nullptr;
    }
    std::vector<unsigned char> vchSignature;
    key.SignCompact(SerializeHash(COutPoint()), vchSignature);
    metadataScriptPubKey << vchSignature;

    std::vector<CRecipient> vecSend {
        { metadataScriptPubKey, 0, false },
        { GetScriptForDestination(tposAddress.Get()), TPOS_CONTRACT_COLATERAL, false }
    };


    CAmount nFeeRequired;
    int nChangePos;

    // this delegate will be executed right before signing. This will allow us to tweak transaction and do
    // some tpos specific thing, like signing contract.
    auto txModifier = [&strError, &key](CMutableTransaction &tx, std::vector<unsigned char> vchSignature) {
        auto firstInput = tx.vin.front().prevout;

        auto it = std::find_if(tx.vout.begin(), tx.vout.end(), [](const CTxOut &txOut) {
            return txOut.scriptPubKey.IsUnspendable();
        });

        auto vchSignatureCopy = vchSignature;
        vchSignature.clear();
        auto hashMessage = SerializeHash(firstInput);
        if(!key.SignCompact(hashMessage, vchSignature))
        {
            strError = "Error: Failed to sign tpos contract";
        }
        it->scriptPubKey.FindAndDelete(CScript(vchSignatureCopy));
        it->scriptPubKey << vchSignature;
    };

    auto txModifierBinded = std::bind(txModifier, std::placeholders::_1, vchSignature);

    if (!wallet->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePos, strError, nullptr, true, ALL_COINS, false, txModifierBinded))
    {
        if (TPOS_CONTRACT_COLATERAL + nFeeRequired > wallet->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("Error() : %s\n", strError);
        return nullptr;
    }

    if(!strError.empty())
        return nullptr;



    //    std::stringstream stringStream(it->scriptPubKey.ToString());
    //    std::string tokens[5];

    //    for(auto &token : tokens)
    //    {
    //        stringStream >> token;
    //        std::cout << token << " ";
    //    }

    //    std::cout << std::endl;

    std::string reason;
    if(!IsStandardTx(wtxNew, reason))
    {
        strError = strprintf("Error: Not standard tx: %s\n", reason.c_str());
        LogPrintf(strError.c_str());
        return nullptr;
    }

    return result;

#endif

    return nullptr;
}

std::unique_ptr<CWalletTx> TPoSUtils::CreateCancelContractTransaction(CWallet *wallet, CReserveKey &reserveKey, const TPoSContract &contract, string &strError)
{
#if 0
    std::unique_ptr<CWalletTx> result(new CWalletTx);
    CWalletTx &wtxNew = *result;

    if(wallet->IsLocked())
    {
        strError = "Error: Wallet is locked";
        return nullptr;
    }

    COutPoint prevOutpoint = GetContractCollateralOutpoint(contract);
    if(prevOutpoint.IsNull())
    {
        strError = "Error: Contract collateral is invalid";
        return nullptr;
    }

    Coin coin;
    if(!pcoinsTip->GetCoin(prevOutpoint, coin) || coin.IsSpent())
    {
        strError = "Error: Collateral is already spent";
        return nullptr;
    }

    auto &prevOutput = contract.rawTx.vout.at(prevOutpoint.n);

    CAmount nFeeRet;
    int nChangePosRet;
    CCoinControl coinControl;
    coinControl.fUsePrivateSend = false;
    coinControl.Select(prevOutpoint);
    if(!wallet->CreateTransaction({ { prevOutput.scriptPubKey, prevOutput.nValue, true } }, wtxNew,
                              reserveKey, nFeeRet, nChangePosRet,
                              strError, &coinControl, true, ONLY_MERCHANTNODE_COLLATERAL))
    {
        LogPrintf("Error() : %s\n", strError.c_str());
        return nullptr;
    }

    return result;
#endif
    return nullptr;
}

COutPoint TPoSUtils::GetContractCollateralOutpoint(const TPoSContract &contract)
{
    COutPoint result;
    if(!contract.rawTx)
    {
        return result;
    }


    const auto &vout = contract.rawTx->vout;
    for(size_t i = 0; i < vout.size(); ++i)
    {
        if(vout[i].scriptPubKey == GetScriptForDestination(contract.tposAddress.Get()) &&
                vout[i].nValue == TPOS_CONTRACT_COLATERAL)
        {
            result = COutPoint(contract.rawTx->GetHash(), i);
            break;
        }
    }

    return result;
}

bool TPoSUtils::CheckContract(const uint256 &hashContractTx, TPoSContract &contract, bool fCheckSignature, bool fCheckContractOutpoint)
{
    CTransactionRef tx;
    uint256 hashBlock;
    if(!GetTransaction(hashContractTx, tx, Params().GetConsensus(), hashBlock, true))
    {
        return error("CheckContract() : failed to get transaction for tpos contract %s",
                     hashContractTx.ToString());
    }

    TPoSContract tmpContract = TPoSContract::FromTPoSContractTx(tx);

    if(!tmpContract.IsValid())
        return error("CheckContract() : invalid transaction for tpos contract");

    if(fCheckSignature)
    {
        CPubKey recoveredKey;
        auto hashMessage = SerializeHash(tmpContract.rawTx->vin.front().prevout);

        if(!recoveredKey.RecoverCompact(hashMessage, tmpContract.vchSignature))
            return error("CheckContract() : failed to recover public key from signature");

        CKeyID keyID;
        if(!tmpContract.tposAddress.GetKeyID(keyID))
        {
            return error("CheckContract() : TPoS address is not P2PKH");
        }

        if(keyID != recoveredKey.GetID())
            return error("CheckContract() : TPoS contract signature is invalid");
    }

    if(fCheckContractOutpoint)
    {
        auto tposContractOutpoint = TPoSUtils::GetContractCollateralOutpoint(tmpContract);
        Coin coin;
        if(!pcoinsTip->GetCoin(tposContractOutpoint, coin) || coin.IsSpent())
            return error("CheckContract() : tpos contract invalid, collateral is spent");
    }

    contract = tmpContract;

    return true;
}

bool TPoSUtils::IsMerchantPaymentValid(CValidationState &state, const CBlock &block, int nBlockHeight, CAmount expectedReward, CAmount actualReward)
{
    auto contract = TPoSContract::FromTPoSContractTx(block.txTPoSContract);
    CXSNAddress merchantAddress = contract.merchantAddress;
    CScript scriptMerchantPubKey = GetScriptForDestination(merchantAddress.Get());

    const auto &coinstake = block.vtx[1];

    if(coinstake->vout[1].scriptPubKey != GetScriptForDestination(contract.tposAddress.Get()))
    {
        CTxDestination dest;
        if(!ExtractDestination(coinstake->vout[1].scriptPubKey, dest))
            return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: coinstake extract destination failed"), REJECT_INVALID, "bad-merchant-payee");

        // ban him, something is incorrect completely
        return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: coinstake is invalid expected: %s, actual %s\n",
                                    contract.tposAddress.ToString().c_str(), CXSNAddress(dest).ToString().c_str()), REJECT_INVALID, "bad-merchant-payee");
    }

    CAmount merchantPayment = 0;
    merchantPayment = std::accumulate(std::begin(coinstake->vout) + 2, std::end(coinstake->vout), CAmount(0), [scriptMerchantPubKey](CAmount accum, const CTxOut &txOut) {
            return txOut.scriptPubKey == scriptMerchantPubKey ? accum + txOut.nValue : accum;
    });

    if(merchantPayment > 0)
    {
        auto maxAllowedValue = (expectedReward / 100) * (100 - contract.stakePercentage);
        // ban, we know fur sure that merchant tries to get more than he is allowed
        if(merchantPayment > maxAllowedValue)
            return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: merchant was paid more than allowed: %s\n", contract.merchantAddress.ToString().c_str()),
                             REJECT_INVALID, "bad-merchant-payee");
    }
    else
    {
        LogPrintf("IsMerchantPaymentValid -- WARNING: merchant wasn't paid, this is weird, but totally acceptable. Shouldn't happen.\n");
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
    if(!merchantAddress.GetKeyID(coinstakeKeyID))
        return state.DoS(0, error("IsMerchantPaymentValid -- ERROR: coin stake was paid to invalid address\n"),
                         REJECT_INVALID, "bad-merchant-payee", true);

    CMerchantnode merchantNode;
    if(!merchantnodeman.Get(coinstakeKeyID, merchantNode))
    {
        return state.DoS(0, error("IsMerchantPaymentValid -- ERROR: failed to find merchantnode with address: %s\n", merchantAddress.ToString().c_str()),
                         REJECT_INVALID, "bad-merchant-payee", true);
    }

    if(merchantNode.hashTPoSContractTx != block.hashTPoSContractTx)
    {
        return state.DoS(100, error("IsMerchantPaymentValid -- ERROR: merchantnode contract is invalid expected: %s, actual %s\n",
                                    block.hashTPoSContractTx.ToString().c_str(), merchantNode.hashTPoSContractTx.ToString().c_str()),
                         REJECT_INVALID, "bad-merchant-payee");
    }

    if(!merchantNode.IsValidForPayment())
    {
        return state.DoS(0, error("IsMerchantPaymentValid -- ERROR: merchantnode with address: %s is not valid for payment\n", merchantAddress.ToString().c_str()),
                         REJECT_INVALID, "bad-merchant-payee", true);
    }

    return true;
}

#endif

TPoSContract::TPoSContract(CTransactionRef tx, CXSNAddress merchantAddress, CXSNAddress tposAddress, short stakePercentage, std::vector<unsigned char> vchSignature)
{
    this->rawTx = tx;
    this->merchantAddress = merchantAddress;
    this->tposAddress = tposAddress;
    this->vchSignature = vchSignature;
    this->stakePercentage = stakePercentage;
}

bool TPoSContract::IsValid() const
{
    return rawTx && !rawTx->IsNull() && merchantAddress.IsValid() && tposAddress.IsValid() &&
           stakePercentage > 0 && stakePercentage < 100;
}

TPoSContract TPoSContract::FromTPoSContractTx(const CTransactionRef tx)
{
    try
    {
        if(tx->vout.size() >= 2 && tx->vout.size() <= 3 )
        {
            const CTxOut *metadataOutPtr = nullptr;
            bool colateralFound = false;
            for(const CTxOut &txOut : tx->vout)
            {
                if(txOut.scriptPubKey.IsUnspendable())
                {
                    metadataOutPtr = &txOut;
                }
                else if(txOut.nValue == TPOS_CONTRACT_COLATERAL)
                {
                    colateralFound = true;
                }
            }

            if(metadataOutPtr && colateralFound)
            {
                const auto &metadataOut = *metadataOutPtr;
                std::vector<std::vector<unsigned char>> vSolutions;
                txnouttype whichType;
                if (Solver(metadataOut.scriptPubKey, whichType, vSolutions) && whichType == TX_NULL_DATA)
                {
                    // Here we can have a chance that it is transaction which is a tpos contract, let's check if it has
                    std::stringstream stringStream(metadataOut.scriptPubKey.ToString());

                    std::string tokens[5];
                    for(auto &token : tokens)
                    {
                        stringStream >> token;
                    }

                    CXSNAddress tposAddress(ParseAddressFromMetadata(tokens[1]));
                    CXSNAddress merchantAddress(ParseAddressFromMetadata(tokens[2]));
                    int commission = std::stoi(tokens[3]);
                    std::vector<unsigned char> vchSignature = ParseHex(tokens[4]);
                    if(tokens[0] == GetOpName(OP_RETURN) && tposAddress.IsValid() && merchantAddress.IsValid() &&
                            commission > 0 && commission < 100)
                    {

                        // if we get to this point, it means that we have found tpos contract that was created for us to act as merchant.
                        return TPoSContract(tx, merchantAddress, tposAddress, commission, vchSignature);
                    }
                }
            }
        }
    }
    catch(std::exception &ex)
    {
        LogPrintf("Failed to parse tpos which had to be tpos, %s\n", ex.what());
    }

    return TPoSContract();
}
