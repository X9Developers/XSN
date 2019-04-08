#ifndef TPOSUTILS_H
#define TPOSUTILS_H

#include <string>
#include <memory>
#include <amount.h>
#include <script/standard.h>
#include <pubkey.h>
#include <key_io.h>

class CWallet;
class CWalletTx;
class CMutableTransaction;
class CReserveKey;
class CValidationState;

struct TPoSContract
{
    TPoSContract() = default;
    TPoSContract(CTransactionRef tx,
                 CBitcoinAddress merchantAddress,
                 CBitcoinAddress tposAddress,
                 short stakePercentage,
                 std::vector<unsigned char> vchSignature);

    bool IsValid() const;

    static TPoSContract FromTPoSContractTx(const CTransactionRef tx);

    CTransactionRef rawTx;
    CBitcoinAddress merchantAddress;
    CBitcoinAddress tposAddress;
    std::vector<unsigned char> vchSignature;
    int stakePercentage = 0;
};

class TPoSUtils
{
public:
    TPoSUtils() = delete;
    ~TPoSUtils() = delete;

    static std::string PrepareTPoSExportBlock(std::string content);
    static std::string ParseTPoSExportBlock(std::string block);

    static bool IsTPoSContract(const CTransactionRef &tx);

#ifdef ENABLE_WALLET
    static bool GetTPoSPayments(const CWallet *wallet,
                                const CTransactionRef &tx,
                                CAmount &stakeAmount,
                                CAmount &commissionAmount,
                                CTxDestination &tposAddress, CTxDestination &merchantAddress);

    static bool IsTPoSOwnerContract(CWallet *wallet, const CTransactionRef &tx);
    static bool IsTPoSMerchantContract(CWallet *wallet, const CTransactionRef &tx);

    static bool CreateTPoSTransaction(CWallet *wallet,
                                      CTransactionRef &transactionOut,
                                      CReserveKey &reserveKey,
                                      const CBitcoinAddress &tposAddress,
                                      const CBitcoinAddress &merchantAddress,
                                      int merchantCommission,
                                      std::string &strError);

    static bool CreateCancelContractTransaction(CWallet *wallet,
                                                CTransactionRef &txOut,
                                                CReserveKey &reserveKey,
                                                const TPoSContract &contract,
                                                std::string &strError);

    static COutPoint GetContractCollateralOutpoint(const TPoSContract &contract);
    static bool CheckContract(const uint256 &hashContractTx, TPoSContract &contract, bool fCheckSignature, bool fCheckContractOutpoint, std::string &strError);
    static bool CheckContract(const CTransactionRef &txContract, TPoSContract &contract, bool fCheckSignature, bool fCheckContractOutpoint, std::string &strError);
    static bool IsMerchantPaymentValid(CValidationState &state, const CBlock &block, int nBlockHeight, CAmount expectedReward, CAmount actualReward);

#endif

};

#endif // TPOSUTILS_H
