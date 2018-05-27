// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEMERCHANTNODE_H
#define ACTIVEMERCHANTNODE_H

#include <chainparams.h>
#include <key.h>
#include <net.h>
#include <primitives/transaction.h>

class CActiveMerchantnode;

static const int ACTIVE_MERCHANTNODE_INITIAL          = 0; // initial state
static const int ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_MERCHANTNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_MERCHANTNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_MERCHANTNODE_STARTED          = 4;

extern CActiveMerchantnode activeMerchantnode;

// Responsible for activating the Merchantnode and pinging the network
class CActiveMerchantnode
{
public:
    enum masternode_type_enum_t {
        MERCHANTNODE_UNKNOWN = 0,
        MERCHANTNODE_REMOTE  = 1
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    masternode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Merchantnode
    bool SendMerchantnodePing(CConnman &connman);

    //  sentinel ping data
    int64_t nSentinelPingTime;
    uint32_t nSentinelVersion;

public:
    // Keys for the active Merchantnode
    CPubKey pubKeyMerchantnode;
    CKey keyMerchantnode;

    // Initialized while registering Merchantnode
    CService service;

    int nState; // should be one of ACTIVE_MERCHANTNODE_XXXX
    std::string strNotCapableReason;


    CActiveMerchantnode()
        : eType(MERCHANTNODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyMerchantnode(),
          keyMerchantnode(),
          service(),
          nState(ACTIVE_MERCHANTNODE_INITIAL)
    {}

    /// Manage state of active Merchantnode
    void ManageState(CConnman &connman);

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

    bool UpdateSentinelPing(int version);

private:
    void ManageStateInitial(CConnman& connman);
    void ManageStateRemote();
};

#endif
