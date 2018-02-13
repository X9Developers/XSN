// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERCHANTNODE_H
#define MERCHANTNODE_H

#include "key.h"
#include "validation.h"
#include "spork.h"

class CMerchantnode;
class CMerchantnodeBroadcast;
class CConnman;

static const int MERCHANTNODE_CHECK_SECONDS               =   5;
static const int MERCHANTNODE_MIN_MNB_SECONDS             =   5 * 60;
static const int MERCHANTNODE_MIN_MNP_SECONDS             =   1 * 60;
static const int MERCHANTNODE_EXPIRATION_SECONDS          =  65 * 60;
static const int MERCHANTNODE_WATCHDOG_MAX_SECONDS        = 120 * 60;
static const int MERCHANTNODE_NEW_START_REQUIRED_SECONDS  = 180 * 60;
static const int MERCHANTNODE_POSE_BAN_MAX_SCORE          = 5;

//
// The Merchantnode Ping Class : Contains a different serialize method for sending pings from merchantnodes throughout the network
//

// sentinel version before sentinel ping implementation
#define DEFAULT_SENTINEL_VERSION 0x010001

class CMerchantnodePing
{
public:
    CTxIn vin{};
    uint256 blockHash{};
    int64_t sigTime{}; //mnb message times
    std::vector<unsigned char> vchSig{};
    bool fSentinelIsCurrent = false; // true if last sentinel ping was actual
    // MSB is always 0, other 3 bits corresponds to x.x.x version scheme
    uint32_t nSentinelVersion{DEFAULT_SENTINEL_VERSION};

    CMerchantnodePing() = default;

    CMerchantnodePing(const COutPoint& outpoint);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
        if(ser_action.ForRead() && (s.size() == 0))
        {
            fSentinelIsCurrent = false;
            nSentinelVersion = DEFAULT_SENTINEL_VERSION;
            return;
        }
        READWRITE(fSentinelIsCurrent);
        READWRITE(nSentinelVersion);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    bool IsExpired() const { return GetAdjustedTime() - sigTime > MERCHANTNODE_NEW_START_REQUIRED_SECONDS; }

    bool Sign(const CKey& keyMerchantnode, const CPubKey& pubKeyMerchantnode);
    bool CheckSignature(CPubKey& pubKeyMerchantnode, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CMerchantnode* pmn, bool fFromNewBroadcast, int& nDos, CConnman& connman);
    void Relay(CConnman& connman);
};

inline bool operator==(const CMerchantnodePing& a, const CMerchantnodePing& b)
{
    return a.vin == b.vin && a.blockHash == b.blockHash;
}
inline bool operator!=(const CMerchantnodePing& a, const CMerchantnodePing& b)
{
    return !(a == b);
}

struct merchantnode_info_t
{
    // Note: all these constructors can be removed once C++14 is enabled.
    // (in C++11 the member initializers wrongly disqualify this as an aggregate)
    merchantnode_info_t() = default;
    merchantnode_info_t(merchantnode_info_t const&) = default;

    merchantnode_info_t(int activeState, int protoVer, int64_t sTime) :
        nActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime} {}

    merchantnode_info_t(int activeState, int protoVer, int64_t sTime,
                      COutPoint const& outpoint, CService const& addr,
                      CPubKey const& pkCollAddr, CPubKey const& pkMN,
                      int64_t tWatchdogV = 0) :
        nActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime},
        vin{outpoint}, addr{addr},
        pubKeyCollateralAddress{pkCollAddr}, pubKeyMerchantnode{pkMN},
        nTimeLastWatchdogVote{tWatchdogV} {}

    int nActiveState = 0;
    int nProtocolVersion = 0;
    int64_t sigTime = 0; //mnb message time

    CTxIn vin{};
    CService addr{};
    CPubKey pubKeyCollateralAddress{};
    CPubKey pubKeyMerchantnode{};
    int64_t nTimeLastWatchdogVote = 0;

    int64_t nLastDsq = 0; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked = 0;
    int64_t nTimeLastPing = 0; //* not in CMN
    bool fInfoValid = false; //* not in CMN
};

//
// The Merchantnode Class. For managing the Darksend process. It contains the input of the 1000DRK, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMerchantnode : public merchantnode_info_t
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:
    enum state {
        MERCHANTNODE_PRE_ENABLED,
        MERCHANTNODE_ENABLED,
        MERCHANTNODE_EXPIRED,
        MERCHANTNODE_OUTPOINT_SPENT,
        MERCHANTNODE_UPDATE_REQUIRED,
        MERCHANTNODE_WATCHDOG_EXPIRED,
        MERCHANTNODE_NEW_START_REQUIRED,
        MERCHANTNODE_POSE_BAN
    };

    enum CollateralStatus {
        COLLATERAL_OK,
        COLLATERAL_UTXO_NOT_FOUND,
        COLLATERAL_INVALID_AMOUNT
    };


    CMerchantnodePing lastPing{};
    std::vector<unsigned char> vchSig{};

    uint256 nCollateralMinConfBlockHash{};
    int nBlockLastPaid{};
    int nPoSeBanScore{};
    int nPoSeBanHeight{};
    bool fUnitTest = false;

    // KEEP TRACK OF GOVERNANCE ITEMS EACH MERCHANTNODE HAS VOTE UPON FOR RECALCULATION
    std::map<uint256, int> mapGovernanceObjectsVotedOn;

    CMerchantnode();
    CMerchantnode(const CMerchantnode& other);
    CMerchantnode(const CMerchantnodeBroadcast& mnb);
    CMerchantnode(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMerchantnodeNew, int nProtocolVersionIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMerchantnode);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nLastDsq);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastWatchdogVote);
        READWRITE(nActiveState);
        READWRITE(nCollateralMinConfBlockHash);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        READWRITE(nPoSeBanScore);
        READWRITE(nPoSeBanHeight);
        READWRITE(fUnitTest);
        READWRITE(mapGovernanceObjectsVotedOn);
    }

    bool UpdateFromNewBroadcast(CMerchantnodeBroadcast& mnb, CConnman& connman);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);
    void Check(bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) const { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsPingedWithin(int nSeconds, int64_t nTimeToCheckAt = -1) const
    {
        if(lastPing == CMerchantnodePing()) return false;

        if(nTimeToCheckAt == -1) {
            nTimeToCheckAt = GetAdjustedTime();
        }
        return nTimeToCheckAt - lastPing.sigTime < nSeconds;
    }

    bool IsEnabled() const { return nActiveState == MERCHANTNODE_ENABLED; }
    bool IsPreEnabled() const { return nActiveState == MERCHANTNODE_PRE_ENABLED; }
    bool IsPoSeBanned() const { return nActiveState == MERCHANTNODE_POSE_BAN; }
    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified() const { return nPoSeBanScore <= -MERCHANTNODE_POSE_BAN_MAX_SCORE; }
    bool IsExpired() const { return nActiveState == MERCHANTNODE_EXPIRED; }
    bool IsOutpointSpent() const { return nActiveState == MERCHANTNODE_OUTPOINT_SPENT; }
    bool IsUpdateRequired() const { return nActiveState == MERCHANTNODE_UPDATE_REQUIRED; }
    bool IsWatchdogExpired() const { return nActiveState == MERCHANTNODE_WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() const { return nActiveState == MERCHANTNODE_NEW_START_REQUIRED; }

    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return  nActiveStateIn == MERCHANTNODE_ENABLED ||
                nActiveStateIn == MERCHANTNODE_PRE_ENABLED ||
                nActiveStateIn == MERCHANTNODE_EXPIRED ||
                nActiveStateIn == MERCHANTNODE_WATCHDOG_EXPIRED;
    }

    bool IsValidForPayment() const
    {
        if(nActiveState == MERCHANTNODE_ENABLED) {
            return true;
        }
        if(!sporkManager.IsSporkActive(SPORK_14_REQUIRE_SENTINEL_FLAG) &&
           (nActiveState == MERCHANTNODE_WATCHDOG_EXPIRED)) {
            return true;
        }

        return false;
    }

    /// Is the input associated with collateral public key? (and there is 1000 DASH - checking if valid merchantnode)
    bool IsInputAssociatedWithPubkey() const;

    bool IsValidNetAddr() const;
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore() { if(nPoSeBanScore < MERCHANTNODE_POSE_BAN_MAX_SCORE) nPoSeBanScore++; }
    void DecreasePoSeBanScore() { if(nPoSeBanScore > -MERCHANTNODE_POSE_BAN_MAX_SCORE) nPoSeBanScore--; }
    void PoSeBan() { nPoSeBanScore = MERCHANTNODE_POSE_BAN_MAX_SCORE; }

    merchantnode_info_t GetInfo() const;

    static std::string StateToString(int nStateIn);
    std::string GetStateString() const;
    std::string GetStatus() const;

    void UpdateWatchdogVoteTime(uint64_t nVoteTime = 0);

    CMerchantnode& operator=(CMerchantnode const& from)
    {
        static_cast<merchantnode_info_t&>(*this)=from;
        lastPing = from.lastPing;
        vchSig = from.vchSig;
        nCollateralMinConfBlockHash = from.nCollateralMinConfBlockHash;
        nBlockLastPaid = from.nBlockLastPaid;
        nPoSeBanScore = from.nPoSeBanScore;
        nPoSeBanHeight = from.nPoSeBanHeight;
        fUnitTest = from.fUnitTest;
        mapGovernanceObjectsVotedOn = from.mapGovernanceObjectsVotedOn;
        return *this;
    }
};

inline bool operator==(const CMerchantnode& a, const CMerchantnode& b)
{
    return a.vin == b.vin;
}
inline bool operator!=(const CMerchantnode& a, const CMerchantnode& b)
{
    return !(a.vin == b.vin);
}


//
// The Merchantnode Broadcast Class : Contains a different serialize method for sending merchantnodes through the network
//

class CMerchantnodeBroadcast : public CMerchantnode
{
public:

    bool fRecovery;

    CMerchantnodeBroadcast() : CMerchantnode(), fRecovery(false) {}
    CMerchantnodeBroadcast(const CMerchantnode& mn) : CMerchantnode(mn), fRecovery(false) {}
    CMerchantnodeBroadcast(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMerchantnodeNew, int nProtocolVersionIn) :
        CMerchantnode(addrNew, outpointNew, pubKeyCollateralAddressNew, pubKeyMerchantnodeNew, nProtocolVersionIn), fRecovery(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMerchantnode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(lastPing);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
        return ss.GetHash();
    }

    /// Create Merchantnode broadcast, needs to be relayed manually after that
    static bool Create(const COutPoint& outpoint, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyMerchantnodeNew, const CPubKey& pubKeyMerchantnodeNew, std::string &strErrorRet, CMerchantnodeBroadcast &mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CMerchantnodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CMerchantnode* pmn, int& nDos, CConnman& connman);
    bool CheckOutpoint(int& nDos);

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void Relay(CConnman& connman);
};

class CMerchantnodeVerification
{
public:
    CTxIn vin1{};
    CTxIn vin2{};
    CService addr{};
    int nonce{};
    int nBlockHeight{};
    std::vector<unsigned char> vchSig1{};
    std::vector<unsigned char> vchSig2{};

    CMerchantnodeVerification() = default;

    CMerchantnodeVerification(CService addr, int nonce, int nBlockHeight) :
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin1);
        READWRITE(vin2);
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin1;
        ss << vin2;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    void Relay() const
    {
        CInv inv(MSG_MERCHANTNODE_VERIFY, GetHash());
        g_connman->RelayInv(inv);
    }
};

#endif
