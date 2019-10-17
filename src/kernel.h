// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include <uint256.h>
#include <streams.h>
#include <arith_uint256.h>
#include <primitives/transaction.h>

namespace Consensus {
struct Params;
}

class CBlock;
class CWallet;
class COutPoint;
class CBlockIndex;

// MODIFIER_INTERVAL: time to elapse before new modifier is computed
static const unsigned int MODIFIER_INTERVAL = 60;
static const unsigned int MODIFIER_INTERVAL_TESTNET = 20;
extern unsigned int nModifierInterval;
extern unsigned int getIntervalVersion(bool fTestNet);

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;

uint256 ComputeStakeModifierV3(const CBlockIndex* pindexPrev, const uint256& kernel);
bool ComputeNextStakeModifier(const CBlockIndex* pindexCurrent, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
bool CheckStakeKernelHash(const CBlockIndex *pindexPrev, unsigned int nBits, uint256 hashBlockFrom, int64_t blockFromTime, const CTransactionRef& txPrev,
                          const COutPoint& prevout, unsigned int nTimeTx,
                          uint256& hashProofOfStake, bool fPoSV3, bool fPrintProofOfStake);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(const CBlockIndex *pindexPrev, const CBlock &block, uint256& hashProofOfStake, const Consensus::Params &params);

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum);

#endif // BITCOIN_KERNEL_H
