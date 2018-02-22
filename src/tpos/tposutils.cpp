#include "tposutils.h"
#include "wallet/wallet.h"
#include "utilmoneystr.h"
#include "policy/policy.h"
#include "coincontrol.h"
#include <sstream>

static const std::string TPOSEXPORTHEADER("TPOSOWNERINFO");
static const int TPOSEXPORTHEADERWIDTH = 40;

static const int TPOS_CONTRACT_COLATERAL = 1 * COIN;

std::string TPoSUtils::PrepareTPoSExportBlock(std::string content)
{
    std::stringstream stream;

    std::string footer(TPOSEXPORTHEADERWIDTH, '=');
    auto header = footer;
    header = header.replace(5, TPOSEXPORTHEADER.size(), TPOSEXPORTHEADER);
    stream << header << std::endl << content << std::endl << footer;
    stream.flush();

    return stream.str();
}

std::string TPoSUtils::ParseTPoSExportBlock(std::string block)
{

    std::stringstream stream(block);

    std::string header, encodedData, footer;
    stream >> header;
    stream >> encodedData;
    stream >> footer;

    if(!header.empty() && header.size() == footer.size() &&
            header.substr(0, 5 + TPOSEXPORTHEADER.size()) == std::string(5, '=') + TPOSEXPORTHEADER)
    {
        return encodedData;
    }
    else
    {
        return std::string();
    }
}

bool TPoSUtils::IsTPoSContract(const CTransaction &tx)
{
    return TPoSContract::FromTPoSContractTx(tx).IsValid();
}

#ifdef ENABLE_WALLET

bool TPoSUtils::GetTPoSPayments(const CWallet *wallet,
                                const CWalletTx &wtx,
                                CAmount &stakeAmount,
                                CAmount &commissionAmount,
                                CBitcoinAddress &tposAddress)
{
    if(!wtx.IsCoinStake())
        return false;

    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;

    std::vector<TPoSContract> tposContracts;

    for(auto &&pair : wallet->tposOwnerContracts)
        tposContracts.emplace_back(pair.second);

    for(auto &&pair : wallet->tposMerchantContracts)
        tposContracts.emplace_back(pair.second);

    for(size_t i = 2; i < std::min<size_t>(4, wtx.vout.size()); ++i)
    {
        CTxDestination address;
        if(ExtractDestination(wtx.vout.at(i).scriptPubKey, address))
        {

            CBitcoinAddress tmpAddress(address);

            auto it = std::find_if(std::begin(tposContracts), std::end(tposContracts), [tmpAddress](const TPoSContract &entry) {
                return entry.tposAddress == tmpAddress;
            });

            if(it == std::end(tposContracts))
                continue;

            stakeAmount = wtx.vout[i].nValue;

            // at this moment nNet contains net stake reward
            // commission was sent to merchant address, so it was base of tx
            commissionAmount = nNet;
            // stake amount is just what was sent to tpos address

            tposAddress = tmpAddress;

            return true;
        }
    }

    return false;

}

bool TPoSUtils::IsTPoSMerchantContract(CWallet *wallet, const CTransaction &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);
    auto walletTx = wallet->GetWalletTx(contract.merchantOutPoint.hash);

    if(!walletTx)
        return false;

    CTxDestination txDestination;
    if(!ExtractDestination(walletTx->vout[contract.merchantOutPoint.n].scriptPubKey, txDestination))
        return false;

    return contract.IsValid() && IsMine(*wallet, txDestination) == ISMINE_SPENDABLE;
}

bool TPoSUtils::IsTPoSOwnerContract(CWallet *wallet, const CTransaction &tx)
{
    TPoSContract contract = TPoSContract::FromTPoSContractTx(tx);
    auto txDestination = contract.tposAddress.Get();

    std::cout << contract.tposAddress.ToString() << std::endl;

    return contract.IsValid() &&
            IsMine(*wallet, txDestination) == ISMINE_SPENDABLE;
}

std::unique_ptr<CWalletTx> TPoSUtils::CreateTPoSTransaction(CWallet *wallet,
                                                            CReserveKey& reservekey,
                                                            const CBitcoinAddress &tposAddress,
                                                            const CAmount &nValue,
                                                            const COutPoint &merchantTxOutPoint,
                                                            int merchantCommission,
                                                            std::string &strError)
{
    std::unique_ptr<CWalletTx> result(new CWalletTx);
    auto &wtxNew = *result;

    auto tposAddressAsStr = tposAddress.ToString();

    CScript metadataScriptPubKey;
    metadataScriptPubKey << OP_RETURN
                         << std::vector<unsigned char>(tposAddressAsStr.begin(), tposAddressAsStr.end())
                         << (100 - merchantCommission)
                         << ParseHex(merchantTxOutPoint.hash.GetHex())
                         << merchantTxOutPoint.n;

    std::stringstream stringStream(metadataScriptPubKey.ToString());

    std::string tokens[5];

    for(auto &token : tokens)
    {
        stringStream >> token;
//        std::cout << token << " ";
    }

//    std::cout << std::endl;

    std::vector<CRecipient> vecSend {
        { metadataScriptPubKey, 0, false },
        { GetScriptForDestination(tposAddress.Get()), 1 * COIN, false }
    };

    if(wallet->IsLocked())
    {
        strError = "Error: Wallet is locked";
        return nullptr;
    }

    CAmount nFeeRequired;
    int nChangePos;

    if (!wallet->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePos, strError))
    {
        if (nValue + nFeeRequired > wallet->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("Error() : %s\n", strError);
        return nullptr;
    }

    std::string reason;
    if(!IsStandardTx(wtxNew, reason))
    {
        strError = strprintf("Error: Not standard tx: %s\n", reason.c_str());
        LogPrintf(strError.c_str());
        return nullptr;
    }

    return result;
}

#endif

TPoSContract::TPoSContract(CTransaction tx, COutPoint merchantOutPoint, CBitcoinAddress tposAddress, short stakePercentage)
{
    this->rawTx = tx;
    this->merchantOutPoint = merchantOutPoint;
    this->tposAddress = tposAddress;
    this->stakePercentage = stakePercentage;
}

bool TPoSContract::IsValid() const
{
    return !rawTx.IsNull() && !merchantOutPoint.hash.IsNull() &&
            tposAddress.IsValid() &&
            stakePercentage > 0 && stakePercentage < 100;
}

TPoSContract TPoSContract::FromTPoSContractTx(const CTransaction &tx)
{
    try
    {
        if(tx.vout.size() >= 2 && tx.vout.size() <= 3 )
        {
            const CTxOut *metadataOutPtr = nullptr;
            bool colateralFound = false;
            for(const CTxOut &txOut : tx.vout)
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
                        //                        std::cout << token << " ";
                    }
                    //                    std::cout << std::endl;

                    auto tposAddressRaw = ParseHex(tokens[1]);
                    std::string tposAddressAsStr(tposAddressRaw.size(), '0');

                    for(size_t i = 0; i < tposAddressRaw.size(); ++i)
                        tposAddressAsStr[i] = static_cast<char>(tposAddressRaw[i]);

                    int commission = std::stoi(tokens[2]);
                    uint256 merchantTxId;
                    merchantTxId.SetHex(tokens[3]);
                    int outIndex = std::stoi(tokens[4]);
                    CBitcoinAddress tposAddress(tposAddressAsStr);
                    if(tokens[0] == GetOpName(OP_RETURN) && tposAddress.IsValid() &&
                            commission > 0 && commission < 100 && !merchantTxId.IsNull() && outIndex >= 0)
                    {

                        // if we get to this point, it means that we have found tpos contract that was created for us to act as merchant.
                        return TPoSContract(tx, COutPoint(merchantTxId, outIndex), tposAddress, commission);
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
