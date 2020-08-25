#ifndef TPOSUTILS_H
#define TPOSUTILS_H

#include <string>
#include <memory>
#include <amount.h>
#include <script/standard.h>
#include <pubkey.h>
#include <key_io.h>

class CKeyStore;
class CWallet;
class CWalletTx;
class CMutableTransaction;
class CReserveKey;
class CValidationState;

class TPoSContract
{
public:
    static const uint16_t CURRENT_VERSION = 2;

public:
    uint16_t nVersion{CURRENT_VERSION};                    // message version
    uint16_t nType{0};                                     // only 0 supported for now
    uint16_t nOperatorReward { 0 };                        // operator reward in % from 0 to 100
    CScript scriptTPoSAddress;
    CScript scriptMerchantAddress;
    std::vector<unsigned char> vchSig;

    CTransactionRef txContract; // memonly

    TPoSContract() = default;
    TPoSContract(CTransactionRef tx,
                 CTxDestination merchantAddress,
                 CTxDestination tposAddress,
                 uint16_t nOperatorReward,
                 std::vector<unsigned char> vchSignature);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(nType);
        READWRITE(nOperatorReward);
        READWRITE(scriptTPoSAddress);
        READWRITE(scriptMerchantAddress);
        READWRITE(vchSig);
    }

    bool IsValid() const;

    static TPoSContract FromTPoSContractTx(const CTransactionRef tx);
};

class TPoSUtils
{
public:
    TPoSUtils() = delete;
    ~TPoSUtils() = delete;

    static std::string PrepareTPoSExportBlock(std::string content);
    static std::string ParseTPoSExportBlock(std::string block);

    static bool IsTPoSContract(const CTransactionRef &tx);

    static COutPoint GetContractCollateralOutpoint(const TPoSContract &contract);
    static bool CheckContract(const uint256 &hashContractTx, TPoSContract &contract, int nBlockHeight, bool fCheckSignature, bool fCheckContractOutpoint, std::string &strError);
    static bool CheckContract(const CTransactionRef &txContract, TPoSContract &contract, int nBlockHeight, bool fCheckSignature, bool fCheckContractOutpoint, std::string &strError);
    static bool IsMerchantPaymentValid(CValidationState &state, const CBlock &block, int nBlockHeight, CAmount expectedReward, CAmount actualReward);

    static std::vector<unsigned char> GenerateContractPayload(const TPoSContract &contract);
    static CScript GenerateLegacyContractScript(const CTxDestination &tposAddress,
                                                const CTxDestination &merchantAddress,
                                                uint16_t nOperatorReward,
                                                const std::vector<unsigned char> &vchSignature);

    static bool SignTPoSContract(CMutableTransaction &tx, CKeyStore *keystore,
                                 TPoSContract contract);
    static bool CreateTPoSTransaction(CMutableTransaction &txOut,
                                      const CTxDestination &tposAddress,
                                      const CTxDestination &merchantAddress,
                                      int nOperatorReward,
                                      bool createLegacyContract, std::string &strError);

    static CAmount GetOperatorPayment(CAmount basePayment, int nOperatorReward);
    static CAmount GetOwnerPayment(CAmount basePayment, int nOperatorReward);

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
                                      const CTxDestination &tposAddress,
                                      const CTxDestination &merchantAddress,
                                      int merchantCommission,
                                      bool createLegacyContract,
                                      std::string &strError);

    static bool CreateCancelContractTransaction(CWallet *wallet,
                                                CTransactionRef &txOut,
                                                CReserveKey &reserveKey,
                                                const TPoSContract &contract,
                                                std::string &strError);


#endif


};

bool IsTPoSNewSignaturesHardForkActivated(int nChainHeight);

#endif // TPOSUTILS_H
