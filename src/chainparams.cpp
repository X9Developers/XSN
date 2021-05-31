// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>
#include <arith_uint256.h>

#include <assert.h>

#include <chainparamsseeds.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 *   CBlock(hash=00000c822abdbb23e28f79a49d29b41429737c6c7e15df40d1b1f1b35907ae34, ver=1, hashPrevBlock=0000000000000000000000000000000000000000000000000000000000000000, hashMerkleRoot=922ab2360f766457416dfc59c6594248c5b79e33c8785bce491c0e01930738f6, nTime=1520274471, nBits=1e0ffff0, nNonce=914267, vtx=1)
 *     CTransaction(hash=922ab2360f, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(0000000000000000000000000000000000000000000000000000000000000000, 4294967295), coinbase 04ffff001d01044c5957697265642030312f4d61722f3230313820546865205345432069732070726f62696e672063727970746f63757272656e637920636f6d70616e696573207769746820696e697469616c20636f696e206f66666572696e6773)
 *     CTxOut(nValue=50.00000000, scriptPubKey=2103042a235a39a72d7b1296313b01)
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Wired 01/Mar/2018 The SEC is probing cryptocurrency companies with initial coin offerings";
    const CScript genesisOutputScript = CScript() << ParseHex("03042a235a39a72d7b1296313b0193ba3f93ad1a1fa2d72f1cab4f21d342c9d5b8") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nLastPoWBlock = 75;
        consensus.nFirstBlocksEmpty = 20000;
        consensus.nSubsidyHalvingInterval = 43200; // Note: actual number of blocks per calendar year with DGW v3 is ~200700 (for example 449750 - 249050)
        consensus.nMasternodePaymentsStartBlock = 20100; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 158000; // actual historical value
        consensus.nMasternodePaymentsIncreasePeriod = 576*30; // 17280 - actual historical value
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 0; // actual historical value
        consensus.nBudgetPaymentsCycleBlocks = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nBudgetProposalEstablishingTime = 60*60*24;
        consensus.nSuperblockCycle = 43200; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nSuperblockStartBlock = consensus.nSuperblockCycle; // The block at which 12.1 goes live (end of final 12.0 budget cycle)
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.BIP34Height = consensus.nLastPoWBlock;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = consensus.nLastPoWBlock; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = consensus.nLastPoWBlock; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // XSN: 1 day
        consensus.nPowTargetSpacing = 1 * 60; // XSN: 1 minutes
        consensus.nPosTargetSpacing = 1 * 60; // XSN: 1 minutes
        consensus.nPosTargetTimespan = 60 * 40;
        consensus.nPoSUpdgradeHFHeight = 898488; // 4 December 2019
        consensus.nTPoSSignatureUpgradeHFHeight = 1348224; // tpos signature update HF, 8 October 2020
        consensus.nCoinstakeExtraInputsValidationHFHeight = 1688696; // coinstake validation HF, 29 May 2021
        consensus.nMerchantnodeMinimumConfirmations = 1;
        consensus.nMasternodeMinimumConfirmations = 15;
        consensus.nStakeMinAge = 60 * 60;
        consensus.nStakeMaxAge = 60 * 60 * 24; // one day
        consensus.nCoinbaseMaturity = 20;
        consensus.nTPoSContractSignatureDeploymentTime = 1523127600;
        consensus.nPowKGWHeight = 15200;
        consensus.nPowDGWHeight = 24;
        consensus.nMaxBlockSpacingFixDeploymentHeight = 674980; // apprx 28 of June
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.fPoSNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1080; // 75% of 2016
        consensus.nMinerConfirmationWindow = 1440; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1533167940; // August 1st, 2018.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1564012740; // July 24th, 2019.

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0"); //506067

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xbf;
        pchMessageStart[1] = 0x0c;
        pchMessageStart[2] = 0x6c;
        pchMessageStart[3] = 0xbd;
        nDefaultPort = 62583;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1520274471, 627829, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000c822abdbb23e28f79a49d29b41429737c6c7e15df40d1b1f1b35907ae34"));
        assert(genesis.hashMerkleRoot == uint256S("0x922ab2360f766457416dfc59c6594248c5b79e33c8785bce491c0e01930738f6"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
//        vSeeds.emplace_back("seed.xsn.sipa.be"); // Pieter Wuille, only supports x1, x5, x9, and xd
//        vSeeds.emplace_back("dnsseed.bluematt.me"); // Matt Corallo, only supports x9
//        vSeeds.emplace_back("dnsseed.xsn.dashjr.org"); // Luke Dashjr
//        vSeeds.emplace_back("seed.xsnstats.com"); // Christian Decker, supports x1 - xf
//        vSeeds.emplace_back("seed.xsn.jonasschnelli.ch"); // Jonas Schnelli, only supports x1, x5, x9, and xd
        vSeeds.emplace_back("autoseeds.xsnseed.xyz");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,76);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,16);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,204);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "xc";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "030a2b7fdf1f123f3686ebc00f1226a20275bc785570ef069e2c2d81b61d616e91";

        checkpointData = {
            {
                { 0, uint256S("0x00000c822abdbb23e28f79a49d29b41429737c6c7e15df40d1b1f1b35907ae34")},
                { 310, uint256S("0x5f2e88d57b09fbcf8983edbf5d297ca491bac9d85e477426891e281d107dd31d")},
                { 1000, uint256S("0x25762bf01143f7fe34912c926e0b95528b082c6323de35516de0fc321f5d8058")},
            }
        };

        chainTxData = ChainTxData{
            // Data as of block 0000000000000000002d6cca6761c99b3c2e936f9a0e304b7c7651a993f461de (height 506081).
            1520274471, // * UNIX timestamp of last known number of transactions
            2000,  // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            1.0         // * estimated number of transactions per second after that timestamp
        };

        /* disable fallback fee on mainnet */
        m_fallback_fee_enabled = true;
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nLastPoWBlock = 75;
        consensus.nSubsidyHalvingInterval = 10000;
        consensus.nFirstBlocksEmpty = 20;
        consensus.nMasternodePaymentsStartBlock = 4010; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 4030;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 1010; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on testnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.BIP34Height = consensus.nLastPoWBlock;
        consensus.BIP34Hash = uint256S("0x0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8");
        consensus.BIP65Height = 0; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 0; // 000000002104c8c45e99a8853285a3b592602a3ccde2b832481da85e9e4ba182
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // XSN: 1 day
        consensus.nPowTargetSpacing = 1 * 60; // XSN: 1 minutes
        consensus.nPosTargetSpacing = 1 * 60; // PoSW: 1 minutes
        consensus.nPosTargetTimespan = 60 * 40;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.nMerchantnodeMinimumConfirmations = 1;
        consensus.fPowNoRetargeting = false;
        consensus.fPoSNoRetargeting = false;
        consensus.nPowKGWHeight = 4001; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 4001;
        consensus.nMaxBlockSpacingFixDeploymentHeight = 0;
        consensus.nStakeMinAge = 60 * 60;
        consensus.nStakeMaxAge = 60 * 60 * 24; // one day
        consensus.nCoinbaseMaturity = 20;
        consensus.nTPoSContractSignatureDeploymentTime = 1522782000;
        consensus.nCoinstakeExtraInputsValidationHFHeight = 0;
        consensus.nRuleChangeActivationThreshold = 30; // 75% for testchains
        consensus.nMinerConfirmationWindow = 40; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

//        // Deployment of BIP68, BIP112, and BIP113.
//        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
//        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
//        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1533004200; // June 31 2018 @ 02:30 hours UTC
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1560284399; // June 11 2019

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // May 1st, 2017

//        // Deployment of SegWit (BIP141, BIP143, and BIP147)
//        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
//        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE; // May 1st 2016
//        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT; // May 1st 2017

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000004f5aef732d572ff514af99a995702c92e4452c7af10858231668b1f"); //1135275

        pchMessageStart[0] = 0xce;
        pchMessageStart[1] = 0xe2;
        pchMessageStart[2] = 0xca;
        pchMessageStart[3] = 0xff;
        nDefaultPort = 29999;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1537872671, 335051, 0x1e0ffff0, 1, 50 * COIN);

        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0000090dda92f4e2f31c0bf7630ff4e15f46b6d1813d64b3ef4f2ea300a0f0cc"));
        assert(genesis.hashMerkleRoot == uint256S("0x922ab2360f766457416dfc59c6594248c5b79e33c8785bce491c0e01930738f6"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));
    vFixedSeeds.clear();

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "0209c1fb78849b929732cbda125bbb905ffc01aeffb5333badf20a9aef2d539b17";

        checkpointData = {
            {
                {0, uint256S("0000090dda92f4e2f31c0bf7630ff4e15f46b6d1813d64b3ef4f2ea300a0f0cc")},
            }
        };

        chainTxData = ChainTxData{
            // Data as of block 000000000000033cfa3c975eb83ecf2bb4aaedf68e6d279f6ed2b427c64caff9 (height 1260526)
            1516111682,
            0,
            0.09
        };

        /* enable fallback fee on testnet */
        m_fallback_fee_enabled = true;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMasternodePaymentsStartBlock = 4010; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 4030;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;
        consensus.nSuperblockStartBlock = 1010; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on testnet
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // XSN: 1 day
        consensus.nPowTargetSpacing = 1 * 60; // XSN: 1 minutes
        consensus.nPosTargetSpacing = 1 * 60; // PoSW: 1 minutes
        consensus.nPosTargetTimespan = 60 * 40;
        consensus.nPoSUpdgradeHFHeight = 0;
        consensus.nTPoSSignatureUpgradeHFHeight = 80;
        consensus.nCoinstakeExtraInputsValidationHFHeight = 0;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.fPoSNoRetargeting = true;
        consensus.nPowKGWHeight = 4001; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 4001;
        consensus.nLastPoWBlock = 75;
        consensus.nMaxBlockSpacingFixDeploymentHeight = 0;
        consensus.nStakeMinAge = 60;
        consensus.nStakeMaxAge = 60 * 60 * 24; // one day
        consensus.nCoinbaseMaturity = 20;
        consensus.nFirstBlocksEmpty = 0;
        consensus.nTPoSContractSignatureDeploymentTime = 1522782000;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 29999;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1417713337, 1, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x7229c32afcd2400f71cd72f55955c73bd384e41ed2e290c5a0488d0e6a15ddb4"));
        assert(genesis.hashMerkleRoot == uint256S("0x922ab2360f766457416dfc59c6594248c5b79e33c8785bce491c0e01930738f6"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        checkpointData = {
            {
                {0, uint256S("0x7229c32afcd2400f71cd72f55955c73bd384e41ed2e290c5a0488d0e6a15ddb4")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,76);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,16);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,204);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "xc";

        /* enable fallback fee on regtest */
        m_fallback_fee_enabled = true;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
