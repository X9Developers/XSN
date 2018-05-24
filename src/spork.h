// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include <hash.h>
#include <net.h>
#include <utilstrencodings.h>

class CSporkMessage;
class CSporkManager;

namespace Spork {

static const int SPORK_START                                            = 10001;

enum {
    /*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/
    SPORK_2_INSTANTSEND_ENABLED = SPORK_START,
    SPORK_3_INSTANTSEND_BLOCK_FILTERING = SPORK_START + 1,
    SPORK_5_INSTANTSEND_MAX_VALUE = SPORK_START + 3,
    SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT = SPORK_START + 6,
    SPORK_9_SUPERBLOCKS_ENABLED,
    SPORK_10_MASTERNODE_PAY_UPDATED_NODES,
    SPORK_12_RECONSIDER_BLOCKS = SPORK_START + 10,
    SPORK_13_OLD_SUPERBLOCK_FLAG,
    SPORK_14_REQUIRE_SENTINEL_FLAG,
    SPORK_15_TPOS_ENABLED,
    SPORK_END
};

}

extern std::map<uint256, CSporkMessage> mapSporks;
extern CSporkManager sporkManager;

//
// Spork classes
// Keep track of all of the network spork settings
//

class CSporkMessage
{
private:
    std::vector<unsigned char> vchSig;

public:
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;

    CSporkMessage(int nSporkID, int64_t nValue, int64_t nTimeSigned) :
        nSporkID(nSporkID),
        nValue(nValue),
        nTimeSigned(nTimeSigned)
        {}

    CSporkMessage() :
        nSporkID(0),
        nValue(0),
        nTimeSigned(0)
        {}


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << nSporkID;
        ss << nValue;
        ss << nTimeSigned;
        return ss.GetHash();
    }

    bool Sign(std::string strSignKey);
    bool CheckSignature();
    void Relay(CConnman *connman);
};


class CSporkManager
{
private:
    std::vector<unsigned char> vchSig;
    std::string strMasterPrivKey;
    std::map<int, CSporkMessage> mapSporksActive;

public:
    using Executor = std::function<void(void)>;
    CSporkManager() {}

    void ProcessSpork(CNode* pfrom, const std::string &strCommand, CDataStream& vRecv, CConnman *connman);
    bool UpdateSpork(int nSporkID, int64_t nValue, CConnman *connman);
    void ExecuteSpork(int nSporkID, int nValue);

    bool IsSporkActive(int nSporkID);
    int64_t GetSporkValue(int nSporkID);
    int GetSporkIDByName(std::string strName);
    std::string GetSporkNameByID(int nSporkID);

    bool SetPrivKey(std::string strPrivKey);
};

#endif
