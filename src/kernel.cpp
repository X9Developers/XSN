/* @flow */
// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>

#include "db.h"
#include "kernel.h"
#include "script/interpreter.h"
#include "timedata.h"
#include "util.h"
#include "wallet/wallet.h"
#include "validation.h"
#include "policy/policy.h"
#include "init.h"
#include "validation.h"
#include "tpos/tposutils.h"

#include <numeric>

#define PRI64x  "llx"

using namespace std;

bool fTestNet = false; //Params().NetworkID() == CBaseChainParams::TESTNET;

// Modifier interval: time to elapse before new modifier is computed
// Set to 3-hour for production network and 20-minute for test network
unsigned int nModifierInterval = MODIFIER_INTERVAL;
unsigned int getIntervalVersion(bool fTestNet)
{
    if (fTestNet)
        return MODIFIER_INTERVAL_TESTNET;
    else
        return MODIFIER_INTERVAL;
}

//static int GetCoinUnspentTxCount(const uint256 &hash)
//{
//    CCoins coins;
//    if(!pcoinsTip->GetCoins(hash, coins))
//    {
//        return 0;
//    }

//    return std::max(1, std::accumulate(std::begin(coins.vout), std::end(coins.vout), 0, [](int accum, const CTxOut &txOut) {
//        return accum + (txOut.IsNull() ? 0 : 1);
//    }) - 1);
//}

// Hard checkpoints of stake modifiers to ensure they are deterministic
static std::map<int, unsigned int> mapStakeModifierCheckpoints =
        boost::assign::map_list_of(0, 0xfd11f4e7u);

// Get time weight
int64_t GetWeight(int64_t nIntervalBeginning, int64_t nIntervalEnd)
{
    return nIntervalEnd - nIntervalBeginning - Params().GetConsensus().nStakeMinAge;
}

// Get the last stake modifier and its generation time from a given block
static bool GetLastStakeModifier(const CBlockIndex* pindex, uint64_t& nStakeModifier, int64_t& nModifierTime)
{
    if (!pindex)
        return error("GetLastStakeModifier: null pindex");
    while (pindex && pindex->pprev && !pindex->GeneratedStakeModifier())
        pindex = pindex->pprev;
    if (!pindex->GeneratedStakeModifier())
        return error("GetLastStakeModifier: no generation at genesis block");
    nStakeModifier = pindex->nStakeModifier;
    nModifierTime = pindex->GetBlockTime();
    return true;
}

// Get selection interval section (in seconds)
static int64_t GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert(nSection >= 0 && nSection < 64);
    int64_t a = nModifierInterval * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1)));
    return a;
}

// Get stake modifier selection interval (in seconds)
static int64_t GetStakeModifierSelectionInterval()
{
    int64_t nSelectionInterval = 0;
    for (int nSection = 0; nSection < 64; nSection++) {
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    }
    return nSelectionInterval;
}

// select a block from the candidate blocks in vSortedByTimestamp, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// nSelectionIntervalStop.
static bool SelectBlockFromCandidates(
        vector<pair<int64_t, uint256> >& vSortedByTimestamp,
        map<uint256, const CBlockIndex*>& mapSelectedBlocks,
        int64_t nSelectionIntervalStop, uint64_t nStakeModifierPrev,
        const CBlockIndex** pindexSelected)
{
    bool fSelected = false;
    arith_uint256 hashBest;
    *pindexSelected = nullptr;
    for(const auto &item : vSortedByTimestamp)
    {
        if (!mapBlockIndex.count(item.second))
            return error("SelectBlockFromCandidates: failed to find block index for candidate block %s", item.second.ToString().c_str());
        const CBlockIndex* pindex = mapBlockIndex[item.second];
        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop)
            break;
        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0)
            continue;
        // compute the selection hash by hashing its proof-hash and the
        // previous proof-of-stake modifier
        uint256 hashProof = pindex->IsProofOfStake()? pindex->hashProofOfStake : pindex->GetBlockHash();
        CDataStream ss(SER_GETHASH, 0);
        ss << hashProof << nStakeModifierPrev;
        arith_uint256 hashSelection = UintToArith256(Hash(ss.begin(), ss.end()));
        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        if (pindex->IsProofOfStake())
            hashSelection >>= 32;
        if (fSelected && hashSelection < hashBest)
        {
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*) pindex;
        }
        else if (!fSelected)
        {
            fSelected = true;
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*) pindex;
        }
    }
    if (fDebug && GetBoolArg("-printstakemodifier", false))
        LogPrintf("SelectBlockFromCandidates: selection hash=%s\n", hashBest.ToString().c_str());
    return fSelected;
}

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
// Stake modifier consists of bits each of which is contributed from a
// selected block of a given block group in the past.
// The selection of a block is based on a hash of the block's proof-hash and
// the previous stake modifier.
// Stake modifier is recomputed at a fixed time interval instead of every
// block. This is to make it difficult for an attacker to gain control of
// additional bits in the stake modifier, even after generating a chain of
// blocks.
bool ComputeNextStakeModifier(const CBlockIndex* pindexCurrent, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier)
{
    const CBlockIndex* pindexPrev = pindexCurrent->pprev;
    nStakeModifier = 0;
    fGeneratedStakeModifier = false;
    if (!pindexPrev)
    {
        fGeneratedStakeModifier = true;
        return true;  // genesis block's modifier is 0
    }
    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    int64_t nModifierTime = 0;
    if (!GetLastStakeModifier(pindexPrev, nStakeModifier, nModifierTime))
        return error("ComputeNextStakeModifier: unable to get last modifier");

    if (fDebug)
    {
        LogPrintf("ComputeNextStakeModifier: prev modifier=0x%016" PRI64x" time=%s epoch=%u\n", nStakeModifier, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nModifierTime).c_str(), nModifierTime);
    }
    if (nModifierTime / nModifierInterval >= pindexPrev->GetBlockTime() / nModifierInterval)
    {
        if (fDebug)
        {
            LogPrintf("ComputeNextStakeModifier: no new interval keep current modifier: pindexPrev nHeight=%d nTime=%u\n", pindexPrev->nHeight, (unsigned int)pindexPrev->GetBlockTime());
        }
        return true;
    }
    if (nModifierTime / nModifierInterval >= pindexCurrent->GetBlockTime() / nModifierInterval)
    {
        // v0.4+ requires current block timestamp also be in a different modifier interval
        if (fDebug)
        {
            LogPrintf("ComputeNextStakeModifier: (v0.4+) no new interval keep current modifier: pindexCurrent nHeight=%d nTime=%u\n", pindexCurrent->nHeight, (unsigned int)pindexCurrent->GetBlockTime());
        }
        return true;
    }

    // Sort candidate blocks by timestamp
    vector<pair<int64_t, uint256> > vSortedByTimestamp;
    vSortedByTimestamp.reserve(64 * nModifierInterval / Params().GetConsensus().nPosTargetSpacing);
    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / nModifierInterval) * nModifierInterval - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;
    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart)
    {
        vSortedByTimestamp.push_back(make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->pprev;
    }
    int nHeightFirstCandidate = pindex ? (pindex->nHeight + 1) : 0;
    reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end());
    sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    // Select 64 blocks from candidate blocks to generate stake modifier
    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = nSelectionIntervalStart;
    map<uint256, const CBlockIndex*> mapSelectedBlocks;
    for (int nRound=0; nRound<min(64, (int)vSortedByTimestamp.size()); nRound++)
    {
        // add an interval section to the current selection round
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);
        // select a block from the candidates of current round
        if (!SelectBlockFromCandidates(vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier, &pindex))
            return error("ComputeNextStakeModifier: unable to select block at round %d", nRound);
        // write the entropy bit of the selected block
        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);
        // add the selected block from candidates to selected list
        mapSelectedBlocks.insert(make_pair(pindex->GetBlockHash(), pindex));
        if (fDebug && GetBoolArg("-printstakemodifier", false))
            LogPrintf("ComputeNextStakeModifier: selected round %d stop=%s height=%d bit=%d\n",
                      nRound, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nSelectionIntervalStop).c_str(), pindex->nHeight, pindex->GetStakeEntropyBit());
    }

    // Print selection map for visualization of the selected blocks
    if (fDebug && GetBoolArg("-printstakemodifier", false))
    {
        string strSelectionMap = "";
        // '-' indicates proof-of-work blocks not selected
        strSelectionMap.insert(0, pindexPrev->nHeight - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;
        while (pindex && pindex->nHeight >= nHeightFirstCandidate)
        {
            // '=' indicates proof-of-stake blocks not selected
            if (pindex->IsProofOfStake())
                strSelectionMap.replace(pindex->nHeight - nHeightFirstCandidate, 1, "=");
            pindex = pindex->pprev;
        }
        for(const auto& item : mapSelectedBlocks)
        {
            // 'S' indicates selected proof-of-stake blocks
            // 'W' indicates selected proof-of-work blocks
            strSelectionMap.replace(item.second->nHeight - nHeightFirstCandidate, 1, item.second->IsProofOfStake()? "S" : "W");
        }
        LogPrintf("ComputeNextStakeModifier: selection height [%d, %d] map %s\n", nHeightFirstCandidate, pindexPrev->nHeight, strSelectionMap.c_str());
    }
    if (fDebug)
    {
        LogPrintf("ComputeNextStakeModifier: new modifier=0x%016" PRI64x" time=%s\n", nStakeModifierNew, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexPrev->GetBlockTime()).c_str());
    }

    nStakeModifier = nStakeModifierNew;
    fGeneratedStakeModifier = true;
    return true;
}

static bool GetKernlStakeModifierV03(uint256 hashBlockFrom, unsigned int nTimeTx, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime, bool fPrintProofOfStake)
{
    nStakeModifier = 0;
    if (!mapBlockIndex.count(hashBlockFrom))
        return error("GetKernelStakeModifier() : block not indexed");

    const CBlockIndex* pindexFrom = mapBlockIndex[hashBlockFrom];
    nStakeModifierHeight = pindexFrom->nHeight;
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = pindexFrom;
    CBlockIndex* pindexNext = chainActive[pindexFrom->nHeight + 1];

    // loop to find the stake modifier later by a selection interval
    while (nStakeModifierTime < pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval) {
        if (!pindexNext) {
            // Should never happen
            return error("Null pindexNext\n");
        }

        pindex = pindexNext;
        pindexNext = chainActive[pindexNext->nHeight + 1];
        if (pindex->GeneratedStakeModifier()) {
            nStakeModifierHeight = pindex->nHeight;
            nStakeModifierTime = pindex->GetBlockTime();
        }
    }
    nStakeModifier = pindex->nStakeModifier;
    return true;
}

// V0.5: Stake modifier used to hash for a stake kernel is chosen as the stake
// modifier that is (nStakeMinAge minus a selection interval) earlier than the
// stake, thus at least a selection interval later than the coin generating the // kernel, as the generating coin is from at least nStakeMinAge ago.
static bool GetKernelStakeModifierV05(unsigned int nTimeTx, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime, bool fPrintProofOfStake)
{
    auto nStakeMinAge = Params().GetConsensus().nStakeMinAge;
    const CBlockIndex* pindex = pindexBestHeader;
    nStakeModifierHeight = pindex->nHeight;
    nStakeModifierTime = pindex->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();

    if(fPrintProofOfStake)
    {
        LogPrintf("stake modifier time: %s interval: %d, time: %s\n",
                  DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nStakeModifierTime).c_str(),
                  nStakeModifierSelectionInterval,
                  DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTimeTx).c_str());
    }

    //    TODO_POS: check this
    //    if (nStakeModifierTime + nStakeMinAge - nStakeModifierSelectionInterval <= nTimeTx)
    //    {
    //        // Best block is still more than
    //        // (nStakeMinAge minus a selection interval) older than kernel timestamp
    //        if (fPrintProofOfStake)
    //            return error("GetKernelStakeModifier() : best block %s at height %d too old for stake",
    //                         pindex->GetBlockHash().ToString().c_str(), pindex->nHeight);
    //        else
    //            return false;
    //    }
    // loop to find the stake modifier earlier by
    // (nStakeMinAge minus a selection interval)
    while (nStakeModifierTime + nStakeMinAge - nStakeModifierSelectionInterval > nTimeTx)
    {
        if (!pindex->pprev)
        {   // reached genesis block; should not happen
            return error("GetKernelStakeModifier() : reached genesis block");
        }
        pindex = pindex->pprev;
        if (pindex->GeneratedStakeModifier())
        {
            nStakeModifierHeight = pindex->nHeight;
            nStakeModifierTime = pindex->GetBlockTime();
        }
    }
    nStakeModifier = pindex->nStakeModifier;
    return true;
}

// Get the stake modifier specified by the protocol to hash for a stake kernel
static bool GetKernelStakeModifier(uint256 hashBlockFrom, unsigned int nTimeTx, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime, bool fPrintProofOfStake)
{
    return GetKernlStakeModifierV03(hashBlockFrom, nTimeTx, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake);
//    return GetKernelStakeModifierV05(nTimeTx, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake);
}

uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom)
{
    //PoSW will hash in the transaction hash and the index number in order to make sure each hash is unique
    ss << nTimeBlockFrom << prevoutIndex << prevoutHash << nTimeTx;
    return Hash(ss.begin(), ss.end());
}

//test hash vs target
bool stakeTargetHit(uint256 hashProofOfStake, int64_t nValueIn, arith_uint256 bnTargetPerCoinDay, bool fTPoS)
{
    //get the stake weight - weight is equal to coin amount
    arith_uint256 bnCoinDayWeight = arith_uint256(nValueIn) / 100;

    //Tpos weight penalty -- first try. This might need to be modified later depending on testing
    //    if(fTPoS){
    //        bnCoinDayWeight = bnCoinDayWeight / 80;
    //    }
    // Now check if proof-of-stake hash meets target protocol
    return UintToArith256(hashProofOfStake) < bnCoinDayWeight * bnTargetPerCoinDay;
}

// ppcoin kernel protocol
// coinstake must meet hash target according to the protocol:
// kernel (input 0) must meet the formula
//     hash(nStakeModifier + txPrev.block.nTime + txPrev.offset + txPrev.nTime + txPrev.vout.n + nTime) < bnTarget * nCoinDayWeight
// this ensures that the chance of getting a coinstake is proportional to the
// amount of coin age one owns.
// The reason this hash is chosen is the following:
//   nStakeModifier:
//       (v0.5) uses dynamic stake modifier around 21 days before the kernel,
//              versus static stake modifier about 9 days after the staked
//              coin (txPrev) used in v0.3
//       (v0.3) scrambles computation to make it very difficult to precompute
//              future proof-of-stake at the time of the coin's confirmation
//       (v0.2) nBits (deprecated): encodes all past block timestamps
//   txPrev.block.nTime: prevent nodes from guessing a good timestamp to
//                       generate transaction for future advantage
//   txPrev.offset: offset of txPrev inside block, to reduce the chance of
//                  nodes generating coinstake at the same time
//   txPrev.nTime: reduce the chance of nodes generating coinstake at the same
//                 time
//   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
//                  generating coinstake at the same time
//   block/tx hash should not be used here as they can be generated in vast
//   quantities so as to generate blocks faster, degrading the system back into
//   a proof-of-work situation.
//
bool CheckStakeKernelHash(unsigned int nBits, const CBlock& blockFrom, unsigned int nTxPrevOffset, const CTransaction& txPrev, const COutPoint& prevout, unsigned int nTimeTx, uint256& hashProofOfStake, bool fPrintProofOfStake)
{
    auto txPrevTime = blockFrom.GetBlockTime();
    if (nTimeTx < txPrevTime)  // Transaction timestamp violation
        return error("CheckStakeKernelHash() : nTime violation");

    auto nStakeMinAge = Params().GetConsensus().nStakeMinAge;
    auto nStakeMaxAge = Params().GetConsensus().nStakeMaxAge;
    unsigned int nTimeBlockFrom = blockFrom.GetBlockTime();
    if (nTimeBlockFrom + nStakeMinAge > nTimeTx) // Min age requirement
        return error("CheckStakeKernelHash() : min age violation");

    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    CAmount nValueIn = txPrev.vout[prevout.n].nValue;
    // v0.3 protocol kernel hash weight starts from 0 at the 30-day min age
    // this change increases active coins participating the hash and helps
    // to secure the network when proof-of-stake difficulty is low
    int64_t nTimeWeight = std::min<int64_t>(nTimeTx - txPrevTime, nStakeMaxAge - nStakeMinAge);
    arith_uint256 bnCoinDayWeight = nValueIn * nTimeWeight / COIN / 200;
    // Calculate hash
    CDataStream ss(SER_GETHASH, 0);
    uint64_t nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64_t nStakeModifierTime = 0;

    if (!GetKernelStakeModifier(blockFrom.GetHash(), nTimeTx, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake))
        return error("Failed to get kernel stake modifier");

    ss << nStakeModifier;

    ss << nTimeBlockFrom << nTxPrevOffset << txPrevTime << prevout.n << nTimeTx;
    hashProofOfStake = Hash(ss.begin(), ss.end());
    if (fPrintProofOfStake)
    {
        LogPrintf("CheckStakeKernelHash() : using modifier 0x%016" PRI64x" at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
                  nStakeModifier, nStakeModifierHeight,
                  DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nStakeModifierTime).c_str(),
                  mapBlockIndex[blockFrom.GetHash()]->nHeight,
                DateTimeStrFormat("%Y-%m-%d %H:%M:%S", blockFrom.GetBlockTime()).c_str());

        LogPrintf("CheckStakeKernelHash() : check protocol=%s modifier=0x%016" PRI64x" nTimeBlockFrom=%u nTxPrevOffset=%u nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
                  "0.5",
                  nStakeModifier,
                  nTimeBlockFrom, nTxPrevOffset,
                  txPrevTime, prevout.n, nTimeTx,
                  hashProofOfStake.ToString().c_str());
    }

    // Now check if proof-of-stake hash meets target protocol
    if (UintToArith256(hashProofOfStake) > bnCoinDayWeight * bnTargetPerCoinDay)
        return false;

    if (fDebug && fPrintProofOfStake)
    {
        LogPrintf("CheckStakeKernelHash() : Generated using modifier 0x%016" PRI64x" at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
                  nStakeModifier, nStakeModifierHeight,
                  DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nStakeModifierTime).c_str(),
                  mapBlockIndex[blockFrom.GetHash()]->nHeight,
                DateTimeStrFormat("%Y-%m-%d %H:%M:%S", blockFrom.GetBlockTime()).c_str());
        LogPrintf("CheckStakeKernelHash() : Generated pass protocol=%s modifier=0x%016" PRI64x" nTimeBlockFrom=%u nTxPrevOffset=%u nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
                  "0.5",
                  nStakeModifier,
                  nTimeBlockFrom, nTxPrevOffset, txPrevTime, prevout.n, nTimeTx,
                  hashProofOfStake.ToString().c_str());
    }
    return true;
}

bool CheckKernelScript(CScript scriptVin, CScript scriptVout)
{
    auto extractKeyID = [](CScript scriptPubKey) {

        std::vector<std::vector<unsigned char>> vSolutions;
        txnouttype whichType;

        CKeyID keyID;
        if (Solver(scriptPubKey, whichType, vSolutions))
        {
            if (whichType == TX_PUBKEYHASH)
            {
                keyID = CKeyID(uint160(vSolutions[0]));
            }
            else if(whichType == TX_PUBKEY)
            {
                keyID = CPubKey(vSolutions[0]).GetID();
            }
        }

        return keyID;
    };

    return extractKeyID(scriptVin) == extractKeyID(scriptVout);
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(const CBlock &block, uint256& hashProofOfStake)
{
    const CTransaction tx = block.vtx[1];
    if (!tx.IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx.GetHash().ToString().c_str());

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn& txin = tx.vin[0];

    // First try finding the previous transaction in database
    uint256 hashBlock;
    CTransaction txPrev;

    const auto &cons = Params().GetConsensus();

    if (!GetTransaction(txin.prevout.hash, txPrev, cons, hashBlock, true))
        return error("CheckProofOfStake() : INFO: read txPrev failed");

    //verify signature and script, don't check script if it's tpos block, signature check will happen in different place
    if (!block.IsTPoSBlock() && !VerifyScript(txin.scriptSig, txPrev.vout[txin.prevout.n].scriptPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0)))
        return error("CheckProofOfStake() : VerifySignature failed on coinstake %s", tx.GetHash().ToString().c_str());

    CBlockIndex* pindex = NULL;
    BlockMap::iterator it = mapBlockIndex.find(hashBlock);
    if (it != mapBlockIndex.end())
        pindex = it->second;
    else
        return error("CheckProofOfStake() : read block failed");

    // Read block header
    CBlock blockprev;
    if (!ReadBlockFromDisk(blockprev, pindex->GetBlockPos(), cons))
        return error("CheckProofOfStake(): INFO: failed to find block");

    if(!CheckKernelScript(txPrev.vout[txin.prevout.n].scriptPubKey, tx.vout[1].scriptPubKey))
        return error("CheckProofOfStake() : INFO: check kernel script failed on coinstake %s, hashProof=%s \n", tx.GetHash().ToString().c_str(), hashProofOfStake.ToString().c_str());

    unsigned int nTime = block.nTime;
    if (!CheckStakeKernelHash(block.nBits, blockprev, /*postx.nTxOffset + */sizeof(CBlock), txPrev, txin.prevout, nTime, hashProofOfStake, fDebug))
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s \n", tx.GetHash().ToString().c_str(), hashProofOfStake.ToString().c_str()); // may occur during initial download or if behind on block chain sync

    return true;
}

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx)
{
    return (nTimeBlock == nTimeTx);
}

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex)
{
    assert (pindex->pprev || pindex->GetBlockHash() == Params().GetConsensus().hashGenesisBlock);
    // Hash previous checksum with flags, hashProofOfStake and nStakeModifier
    CDataStream ss(SER_GETHASH, 0);
    if (pindex->pprev)
        ss << pindex->pprev->nStakeModifierChecksum;
    ss << pindex->nFlags << pindex->hashProofOfStake << pindex->nStakeModifier;
    arith_uint256 hashChecksum = UintToArith256(Hash(ss.begin(), ss.end()));
    hashChecksum >>= (256 - 32);
    return hashChecksum.GetLow64();
}

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum)
{
    if (fTestNet) return true; // Testnet has no checkpoints
    if (mapStakeModifierCheckpoints.count(nHeight))
        return nStakeModifierChecksum == mapStakeModifierCheckpoints[nHeight];
    return true;
}
