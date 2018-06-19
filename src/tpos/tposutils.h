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
                                const CWalletTx& wtx,
                                CAmount &stakeAmount,
                                CAmount &commissionAmount,
                                CBitcoinAddress &tposAddress, CBitcoinAddress &merchantAddress);

    static bool IsTPoSOwnerContract(CWallet *wallet, const CTransactionRef &tx);
    static bool IsTPoSMerchantContract(CWallet *wallet, const CTransactionRef &tx);

    static std::unique_ptr<CWalletTx> CreateTPoSTransaction(CWallet *wallet,
                                                            CReserveKey &reserveKey,
                                                            const CBitcoinAddress &tposAddress,
                                                            const CBitcoinAddress &merchantAddress,
                                                            int merchantCommission,
                                                            std::string &strError);

    static std::unique_ptr<CWalletTx> CreateCancelContractTransaction(CWallet *wallet,
                                                                      CReserveKey &reserveKey,
                                                                      const TPoSContract &contract,
                                                                      std::string &strError);

    static COutPoint GetContractCollateralOutpoint(const TPoSContract &contract);
    static bool CheckContract(const uint256 &hashContractTx, TPoSContract &contract, bool fCheckSignature, bool fCheckContractOutpoint);
    static bool IsMerchantPaymentValid(CValidationState &state, const CBlock &block, int nBlockHeight, CAmount expectedReward, CAmount actualReward);

#endif

};

#endif // TPOSUTILS_H
