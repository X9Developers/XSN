#ifndef TPOSUTILS_H
#define TPOSUTILS_H

#include <string>
#include <memory>
#include "amount.h"
#include "script/standard.h"
#include "pubkey.h"
#include "base58.h"

class CWallet;
class CWalletTx;
class CMutableTransaction;
class CReserveKey;

struct TPoSContract {
    TPoSContract() = default;
    TPoSContract(CTransaction tx,
                 COutPoint merchantOutPoint,
                 CBitcoinAddress merchantAddress,
                 CBitcoinAddress tposAddress,
                 short stakePercentage);

    bool IsValid() const;

    static TPoSContract FromTPoSContractTx(const CTransaction &tx);

    CTransaction rawTx;
    COutPoint merchantOutPoint;
    CBitcoinAddress merchantAddress;
    CBitcoinAddress tposAddress;
    int stakePercentage = 0;
};

class TPoSUtils
{
public:
    TPoSUtils() = delete;
    ~TPoSUtils() = delete;

    static std::string PrepareTPoSExportBlock(std::string content);
    static std::string ParseTPoSExportBlock(std::string block);

    static bool IsTPoSContract(const CTransaction &tx);

#ifdef ENABLE_WALLET
    static bool GetTPoSPayments(const CWallet *wallet,
                                const CWalletTx& wtx,
                                CAmount &stakeAmount,
                                CAmount &commissionAmount,
                                CBitcoinAddress &tposAddress);

    static bool IsTPoSOwnerContract(CWallet *wallet, const CTransaction &tx);
    static bool IsTPoSMerchantContract(CWallet *wallet, const CTransaction &tx);

    static std::unique_ptr<CWalletTx> CreateTPoSTransaction(CWallet *wallet, CReserveKey &reserveKey,
                                                            const CScript &tposDestination,
                                                            const CAmount &nValue,
                                                            const CBitcoinAddress &merchantAddress,
                                                            const COutPoint &merchantTxOutPoint,
                                                            int merchantCommission,
                                                            std::string &strError);
#endif

};

#endif // TPOSUTILS_H
