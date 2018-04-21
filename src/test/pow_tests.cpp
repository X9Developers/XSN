// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "util.h"
#include "test/test_xsn.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with DGW */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    // build the chain of 24 blocks
    CBlockIndex blockIndexLast;
    blockIndexLast.nHeight = 56;
    blockIndexLast.nTime = 1408732489;
    blockIndexLast.nBits = 0x1b1418d4;
    CBlockIndex blockIndexPrev1 = CBlockIndex();
    blockIndexPrev1.nTime = 1408732257;  // Block #55
    blockIndexPrev1.nBits = 0x1b13b83f;
    blockIndexLast.pprev = &blockIndexPrev1;
    CBlockIndex blockIndexPrev2 = CBlockIndex();
    blockIndexPrev2.nTime = 1408732229;  // Block #54
    blockIndexPrev2.nBits = 0x1b10460b;
    blockIndexPrev1.pprev = &blockIndexPrev2;
    CBlockIndex blockIndexPrev3 = CBlockIndex();
    blockIndexPrev3.nTime = 1408731256;  // Block #53
    blockIndexPrev3.nBits = 0x1b113ff1;
    blockIndexPrev2.pprev = &blockIndexPrev3;
    CBlockIndex blockIndexPrev4 = CBlockIndex();
    blockIndexPrev4.nTime = 1408731242;  // Block #52
    blockIndexPrev4.nBits = 0x1b0fed89;
    blockIndexPrev3.pprev = &blockIndexPrev4;
    CBlockIndex blockIndexPrev5 = CBlockIndex();
    blockIndexPrev5.nTime = 1408730914;  // Block #51
    blockIndexPrev5.nBits = 0x1b10b864;
    blockIndexPrev4.pprev = &blockIndexPrev5;
    CBlockIndex blockIndexPrev6 = CBlockIndex();
    blockIndexPrev6.nTime = 1408730862;  // Block #50
    blockIndexPrev6.nBits = 0x1b0dd168;
    blockIndexPrev5.pprev = &blockIndexPrev6;
    CBlockIndex blockIndexPrev7 = CBlockIndex();
    blockIndexPrev7.nTime = 1408730179;  // Block #49
    blockIndexPrev7.nBits = 0x1b0c03d6;
    blockIndexPrev6.pprev = &blockIndexPrev7;
    CBlockIndex blockIndexPrev8 = CBlockIndex();
    blockIndexPrev8.nTime = 1408729678;  // Block #48
    blockIndexPrev8.nBits = 0x1b0c9ab8;
    blockIndexPrev7.pprev = &blockIndexPrev8;
    CBlockIndex blockIndexPrev9 = CBlockIndex();
    blockIndexPrev9.nTime = 1408729647;  // Block #47
    blockIndexPrev9.nBits = 0x1b0dfaff;
    blockIndexPrev8.pprev = &blockIndexPrev9;
    CBlockIndex blockIndexPrev10 = CBlockIndex();
    blockIndexPrev10.nTime = 1408729587;  // Block #46
    blockIndexPrev10.nBits = 0x1b10e878;
    blockIndexPrev9.pprev = &blockIndexPrev10;
    CBlockIndex blockIndexPrev11 = CBlockIndex();
    blockIndexPrev11.nTime = 1408729576;  // Block #45
    blockIndexPrev11.nBits = 0x1b1063d0;
    blockIndexPrev10.pprev = &blockIndexPrev11;
    CBlockIndex blockIndexPrev12 = CBlockIndex();
    blockIndexPrev12.nTime = 1408729474;  // Block #44
    blockIndexPrev12.nBits = 0x1b104297;
    blockIndexPrev11.pprev = &blockIndexPrev12;
    CBlockIndex blockIndexPrev13 = CBlockIndex();
    blockIndexPrev13.nTime = 1408729305;  // Block #43
    blockIndexPrev13.nBits = 0x1b107556;
    blockIndexPrev12.pprev = &blockIndexPrev13;
    CBlockIndex blockIndexPrev14 = CBlockIndex();
    blockIndexPrev14.nTime = 1408729179;  // Block #42
    blockIndexPrev14.nBits = 0x1b110764;
    blockIndexPrev13.pprev = &blockIndexPrev14;
    CBlockIndex blockIndexPrev15 = CBlockIndex();
    blockIndexPrev15.nTime = 1408729116;  // Block #41
    blockIndexPrev15.nBits = 0x1b1141bf;
    blockIndexPrev14.pprev = &blockIndexPrev15;
    CBlockIndex blockIndexPrev16 = CBlockIndex();
    blockIndexPrev16.nTime = 1408728950;  // Block #40
    blockIndexPrev16.nBits = 0x1b1123f9;
    blockIndexPrev15.pprev = &blockIndexPrev16;
    CBlockIndex blockIndexPrev17 = CBlockIndex();
    blockIndexPrev17.nTime = 1408728756;  // Block #39
    blockIndexPrev17.nBits = 0x1b118d9c;
    blockIndexPrev16.pprev = &blockIndexPrev17;
    CBlockIndex blockIndexPrev18 = CBlockIndex();
    blockIndexPrev18.nTime = 1408728744;  // Block #38
    blockIndexPrev18.nBits = 0x1b11abac;
    blockIndexPrev17.pprev = &blockIndexPrev18;
    CBlockIndex blockIndexPrev19 = CBlockIndex();
    blockIndexPrev19.nTime = 1408728608;  // Block #37
    blockIndexPrev19.nBits = 0x1b11951e;
    blockIndexPrev18.pprev = &blockIndexPrev19;
    CBlockIndex blockIndexPrev20 = CBlockIndex();
    blockIndexPrev20.nTime = 1408728495;  // Block #36
    blockIndexPrev20.nBits = 0x1b121cf3;
    blockIndexPrev19.pprev = &blockIndexPrev20;
    CBlockIndex blockIndexPrev21 = CBlockIndex();
    blockIndexPrev21.nTime = 1408728479;  // Block #35
    blockIndexPrev21.nBits = 0x1b11a33c;
    blockIndexPrev20.pprev = &blockIndexPrev21;
    CBlockIndex blockIndexPrev22 = CBlockIndex();
    blockIndexPrev22.nTime = 1408728332;  // Block #34
    blockIndexPrev22.nBits = 0x1b10e09e;
    blockIndexPrev21.pprev = &blockIndexPrev22;
    CBlockIndex blockIndexPrev23 = CBlockIndex();
    blockIndexPrev23.nTime = 1408728124;  // Block #33
    blockIndexPrev23.nBits = 0x1b104be1;
    blockIndexPrev22.pprev = &blockIndexPrev23;

    CBlockHeader blockHeader;
    blockHeader.nTime = 1408732505; // Block #57
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndexLast, &blockHeader, params), 0x1B321F05); // Block #57 has 0x1B321F05
}

/* Test the constraint on the upper bound for next work */
// BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
// {
//     SelectParams(CBaseChainParams::MAIN);
//     const Consensus::Params& params = Params().GetConsensus();

//     int64_t nLastRetargetTime = 1231006505; // Block #0
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 2015;
//     pindexLast.nTime = 1233061996;  // Block #2015
//     pindexLast.nBits = 0x1d00ffff;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1d00ffff);
// }

/* Test the constraint on the lower bound for actual time taken */
// BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
// {
//     SelectParams(CBaseChainParams::MAIN);
//     const Consensus::Params& params = Params().GetConsensus();

//     int64_t nLastRetargetTime = 1279008237; // Block #66528
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 68543;
//     pindexLast.nTime = 1279297671;  // Block #68543
//     pindexLast.nBits = 0x1c05a3f4;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1c0168fd);
// }

/* Test the constraint on the upper bound for actual time taken */
// BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
// {
//     SelectParams(CBaseChainParams::MAIN);
//     const Consensus::Params& params = Params().GetConsensus();

//     int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 46367;
//     pindexLast.nTime = 1269211443;  // Block #46367
//     pindexLast.nBits = 0x1c387f6f;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1d00e1fd);
// }

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : NULL;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[GetRand(10000)];
        CBlockIndex *p2 = &blocks[GetRand(10000)];
        CBlockIndex *p3 = &blocks[GetRand(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

BOOST_AUTO_TEST_SUITE_END()
