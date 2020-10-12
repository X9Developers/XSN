// Copyright (c) 2020 The XSN Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <txmempool.h>
#include <amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/test_xsn.h>
#include <tpos/tposutils.h>
#include <keystore.h>
#include <messagesigner.h>
#include <masternode-payments.h>

#include <numeric>
#include <boost/test/unit_test.hpp>

using SimpleUTXOMap = std::map<COutPoint, std::pair<int, CAmount>>;

static SimpleUTXOMap BuildSimpleUtxoMap(const std::vector<CTransactionRef>& txs)
{
    SimpleUTXOMap utxos;
    for (size_t i = 0; i < txs.size(); i++) {
        auto& tx = txs[i];
        for (size_t j = 0; j < tx->vout.size(); j++) {
            utxos.emplace(COutPoint(tx->GetHash(), j), std::make_pair((int)i + 1, tx->vout[j].nValue));
        }
    }
    return utxos;
}

static std::vector<COutPoint> SelectUTXOs(SimpleUTXOMap& utoxs, CAmount amount, CAmount& changeRet)
{
    changeRet = 0;

    std::vector<COutPoint> selectedUtxos;
    CAmount selectedAmount = 0;
    while (!utoxs.empty()) {
        bool found = false;
        for (auto it = utoxs.begin(); it != utoxs.end(); ++it) {
            if (chainActive.Height() - it->second.first < COINBASE_MATURITY) {
                continue;
            }

            found = true;
            selectedAmount += it->second.second;
            selectedUtxos.emplace_back(it->first);
            utoxs.erase(it);
            break;
        }
        BOOST_ASSERT(found);
        if (selectedAmount >= amount) {
            changeRet = selectedAmount - amount;
            break;
        }
    }

    return selectedUtxos;
}

static void FundTransaction(CMutableTransaction& tx, SimpleUTXOMap& utoxs, const CScript& scriptPayout, CAmount amount)
{
    CAmount change;
    auto inputs = SelectUTXOs(utoxs, amount, change);
    for (size_t i = 0; i < inputs.size(); i++) {
        tx.vin.emplace_back(CTxIn(inputs[i]));
    }
    tx.vout.emplace_back(CTxOut(amount, scriptPayout));
    if (change != 0) {
        tx.vout.emplace_back(CTxOut(change, scriptPayout));
    }
}

static void FundTransaction(CMutableTransaction& tx, SimpleUTXOMap& utoxs, CScript scriptChange)
{
    CAmount change;
    CAmount amount = std::accumulate(tx.vout.begin(), tx.vout.end(), CAmount(0), [](CAmount accum, const CTxOut &out) {
        return accum + out.nValue;
    });

    auto inputs = SelectUTXOs(utoxs, amount, change);
    for (size_t i = 0; i < inputs.size(); i++) {
        tx.vin.emplace_back(CTxIn(inputs[i]));
    }
    if (change != 0) {
        tx.vout.emplace_back(CTxOut(change, scriptChange));
    }
}

static void SignTransaction(CMutableTransaction& tx, const CKey& coinbaseKey)
{
    CBasicKeyStore tempKeystore;
    tempKeystore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

    for (size_t i = 0; i < tx.vin.size(); i++) {
        CTransactionRef txFrom;
        uint256 hashBlock;
        BOOST_ASSERT(GetTransaction(tx.vin[i].prevout.hash, txFrom, Params().GetConsensus(), hashBlock));
        BOOST_ASSERT(SignSignature(tempKeystore, *txFrom, tx, i, SIGHASH_ALL));
    }
}

static CMutableTransaction CreateContractTx(const CTxDestination &tposAddress, const CTxDestination &merchantAddress, uint16_t nOperatorReward)
{
    CMutableTransaction tx;
    std::string strError;
    TPoSUtils::CreateTPoSTransaction(tx, tposAddress, merchantAddress, nOperatorReward, strError);
    return tx;
}

static void SignContract(CMutableTransaction &tx, const CKey &key, bool legacyContract)
{
    CBasicKeyStore keystore;
    keystore.AddKeyPubKey(key, key.GetPubKey());
    auto contract = TPoSContract::FromTPoSContractTx(MakeTransactionRef(tx));
    contract.nVersion = legacyContract ? 1 : 2;
    TPoSUtils::SignTPoSContract(tx, &keystore, contract);
}

BOOST_AUTO_TEST_SUITE(tpos_tests)

BOOST_FIXTURE_TEST_CASE(tpos_create_new_contract, TestChain100Setup)
{
    m_coinbase_txns.emplace_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    m_coinbase_txns.emplace_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);

    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

    CKey tposAddressKey;
    tposAddressKey.MakeNewKey(true);
    CKey merchantAddressKey;
    merchantAddressKey.MakeNewKey(true);
    auto unsignedContract = CreateContractTx(WitnessV0KeyHash(tposAddressKey.GetPubKey().GetID()), WitnessV0KeyHash(merchantAddressKey.GetPubKey().GetID()), 1);
    TPoSContract tmp;
    std::string strError;
    BOOST_ASSERT(TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, 1, false, false, strError));
    FundTransaction(unsignedContract, utxos, unsignedContract.vout[1].scriptPubKey);
    SignContract(unsignedContract, tposAddressKey, false);
    // shouldn't be allowed before HF.
    BOOST_ASSERT(!TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, 1, true, false, strError));
    // but should be ok after HF.
    BOOST_ASSERT(TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, Params().GetConsensus().nTPoSSignatureUpgradeHFHeight, true, false, strError));
}

BOOST_FIXTURE_TEST_CASE(tpos_contract_payment, TestChain100Setup)
{
    CAmount basePayment = 10 * COIN;
    BOOST_CHECK_EQUAL(basePayment, TPoSUtils::GetOwnerPayment(basePayment, 0));
    BOOST_CHECK_EQUAL(0, TPoSUtils::GetOperatorPayment(basePayment, 0));

    BOOST_CHECK_EQUAL(0, TPoSUtils::GetOwnerPayment(basePayment, 100));
    BOOST_CHECK_EQUAL(basePayment, TPoSUtils::GetOperatorPayment(basePayment, 100));

    BOOST_CHECK_EQUAL(5 * COIN, TPoSUtils::GetOwnerPayment(basePayment, 50));
    BOOST_CHECK_EQUAL(5 * COIN, TPoSUtils::GetOperatorPayment(basePayment, 50));

    BOOST_CHECK_EQUAL(9 * COIN, TPoSUtils::GetOwnerPayment(basePayment, 10));
    BOOST_CHECK_EQUAL(1 * COIN, TPoSUtils::GetOperatorPayment(basePayment, 10));
}

BOOST_FIXTURE_TEST_CASE(tpos_no_contract_adjust_mn_payment, TestChain100Setup)
{
    CMutableTransaction txCoinstake;
    txCoinstake.vout.resize(1);
    auto scriptCoinstake = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
    CKey keyMNPayment;
    keyMNPayment.MakeNewKey(true);
    auto scriptMnPayment = GetScriptForDestination(keyMNPayment.GetPubKey().GetID());
    auto nCoinstakePayment = 10 * COIN;
    txCoinstake.vout.emplace_back(nCoinstakePayment, scriptCoinstake);

    auto sum = [](const CMutableTransaction &tx) {
        return std::accumulate(tx.vout.begin(), tx.vout.end(), CAmount(0), [](CAmount accum, const CTxOut &out) {
            return accum + out.nValue;
        });
    };

    // no contract, MN payment, no split
    {
        CMutableTransaction txNoContractCoinstake = txCoinstake;
        auto nMNPayment = nCoinstakePayment / 2;
        CTxOut outMNPayment(nMNPayment, scriptMnPayment);
        txNoContractCoinstake.vout.push_back(outMNPayment);
        AdjustMasternodePayment(txNoContractCoinstake, outMNPayment, TPoSContract{});
        BOOST_CHECK_EQUAL(scriptCoinstake.ToString(), txNoContractCoinstake.vout[1].scriptPubKey.ToString());
        BOOST_CHECK_EQUAL(nCoinstakePayment - nMNPayment, txNoContractCoinstake.vout[1].nValue);
        BOOST_CHECK_EQUAL(outMNPayment.nValue, txNoContractCoinstake.vout[2].nValue);
        BOOST_CHECK_EQUAL(outMNPayment.scriptPubKey.ToString(), txNoContractCoinstake.vout[2].scriptPubKey.ToString());
        BOOST_ASSERT(nCoinstakePayment >= sum(txNoContractCoinstake));
        BOOST_CHECK_EQUAL(3, txNoContractCoinstake.vout.size());
    }

    // no contract, MN payment, split
    {
        CMutableTransaction txNoContractCoinstake = txCoinstake;
        auto nMNPayment = nCoinstakePayment / 2;
        CTxOut outMNPayment(nMNPayment, scriptMnPayment);
        txNoContractCoinstake.vout.back().nValue /= 2;
        txNoContractCoinstake.vout.push_back(CTxOut(txNoContractCoinstake.vout.back().nValue, scriptCoinstake));
        txNoContractCoinstake.vout.push_back(outMNPayment);
        AdjustMasternodePayment(txNoContractCoinstake, outMNPayment, TPoSContract{});
        CAmount amount = 0;
        for (int i = 1; i < 3; ++i) {
            BOOST_CHECK_EQUAL(scriptCoinstake.ToString(), txNoContractCoinstake.vout[i].scriptPubKey.ToString());
            amount += txNoContractCoinstake.vout[i].nValue;
        }

        BOOST_CHECK_EQUAL(nCoinstakePayment - nMNPayment, amount);
        BOOST_CHECK_EQUAL(outMNPayment.nValue, txNoContractCoinstake.vout[3].nValue);
        BOOST_CHECK_EQUAL(outMNPayment.scriptPubKey.ToString(), txNoContractCoinstake.vout[3].scriptPubKey.ToString());
        BOOST_ASSERT(nCoinstakePayment >= sum(txNoContractCoinstake));
        BOOST_CHECK_EQUAL(4, txNoContractCoinstake.vout.size());
    }

    // no contract, no MN payment, no split
    {
        CMutableTransaction txNoContractNoMnCoinstake = txCoinstake;
        AdjustMasternodePayment(txNoContractNoMnCoinstake, {}, TPoSContract{});
        BOOST_CHECK_EQUAL(scriptCoinstake.ToString(), txNoContractNoMnCoinstake.vout[1].scriptPubKey.ToString());
        BOOST_CHECK_EQUAL(nCoinstakePayment, txNoContractNoMnCoinstake.vout[1].nValue);
        BOOST_CHECK_EQUAL(2, txNoContractNoMnCoinstake.vout.size());
        BOOST_ASSERT(nCoinstakePayment >= sum(txNoContractNoMnCoinstake));
    }

    // no contract, no MN payment, split
    {
        CMutableTransaction txNoContractNoMnCoinstake = txCoinstake;
        txNoContractNoMnCoinstake.vout.back().nValue /= 2;
        txNoContractNoMnCoinstake.vout.push_back(CTxOut(txNoContractNoMnCoinstake.vout.back().nValue, scriptCoinstake));
        AdjustMasternodePayment(txNoContractNoMnCoinstake, {}, TPoSContract{});
        BOOST_CHECK_EQUAL(3, txNoContractNoMnCoinstake.vout.size());
        BOOST_ASSERT(nCoinstakePayment >= sum(txNoContractNoMnCoinstake));

        for (int i = 1; i < 3; ++i) {
            BOOST_CHECK_EQUAL(scriptCoinstake.ToString(), txNoContractNoMnCoinstake.vout[i].scriptPubKey.ToString());
        }
    }
}

BOOST_FIXTURE_TEST_CASE(tpos_contract_adjust_mn_payment, TestChain100Setup)
{
    CMutableTransaction txCoinstake;
    txCoinstake.vout.resize(1);
    auto scriptCoinstake = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
    CKey keyMNPayment;
    keyMNPayment.MakeNewKey(true);
    auto scriptMnPayment = GetScriptForDestination(keyMNPayment.GetPubKey().GetID());
    auto nCoinstakePayment = 10 * COIN;
    txCoinstake.vout.emplace_back(nCoinstakePayment, scriptCoinstake);

    CMutableTransaction txSplitCoinstake;
    txSplitCoinstake.vout.resize(1);
    txSplitCoinstake.vout.emplace_back(nCoinstakePayment / 2, scriptCoinstake);

    auto sum = [](const CMutableTransaction &tx) {
        return std::accumulate(tx.vout.begin(), tx.vout.end(), CAmount(0), [](CAmount accum, const CTxOut &out) {
            return accum + out.nValue;
        });
    };

    CKey keyMerhchant;
    keyMerhchant.MakeNewKey(true);
    auto merchantAddress = WitnessV0KeyHash(keyMerhchant.GetPubKey().GetID());
    auto scriptMerchant = GetScriptForDestination(merchantAddress);

    auto txContract = CreateContractTx(coinbaseKey.GetPubKey().GetID(), merchantAddress, 50);
    auto contract = TPoSContract::FromTPoSContractTx(MakeTransactionRef(txContract));

    BOOST_ASSERT(contract.IsValid());

    auto nOwnerReward = TPoSUtils::GetOwnerPayment(nCoinstakePayment, contract.nOperatorReward);
    auto nOperatorReward = TPoSUtils::GetOperatorPayment(nCoinstakePayment, contract.nOperatorReward);

    // prepare no split tx
    txCoinstake.vout.back().nValue = nOwnerReward;
    txCoinstake.vout.emplace_back(nOperatorReward, scriptMerchant);

    // prepare split tx
    txSplitCoinstake.vout.back().nValue = nOwnerReward / 2;
    txSplitCoinstake.vout.emplace_back(txSplitCoinstake.vout.back());
    txSplitCoinstake.vout.emplace_back(nOperatorReward, scriptMerchant);

    // contract, MN payment, no split
    {
        auto nAdjustedReward = nCoinstakePayment / 2;
        CMutableTransaction txTPoSCoinstake = txCoinstake;
        auto nMNPayment = nCoinstakePayment / 2;
        CTxOut outMNPayment(nMNPayment, scriptMnPayment);
        txTPoSCoinstake.vout.push_back(outMNPayment);
        AdjustMasternodePayment(txTPoSCoinstake, outMNPayment, contract);
        BOOST_CHECK_EQUAL(4, txTPoSCoinstake.vout.size());
        BOOST_CHECK_EQUAL(contract.scriptTPoSAddress.ToString(), txTPoSCoinstake.vout[1].scriptPubKey.ToString());
        BOOST_CHECK_EQUAL(TPoSUtils::GetOwnerPayment(nAdjustedReward, contract.nOperatorReward), txTPoSCoinstake.vout[1].nValue);
        BOOST_CHECK_EQUAL(TPoSUtils::GetOperatorPayment(nAdjustedReward, contract.nOperatorReward), txTPoSCoinstake.vout[2].nValue);
        BOOST_CHECK_EQUAL(scriptMerchant.ToString(), txTPoSCoinstake.vout[2].scriptPubKey.ToString());
        BOOST_CHECK_EQUAL(outMNPayment.nValue, txTPoSCoinstake.vout[3].nValue);
        BOOST_CHECK_EQUAL(outMNPayment.scriptPubKey.ToString(), txTPoSCoinstake.vout[3].scriptPubKey.ToString());
        BOOST_ASSERT(nCoinstakePayment >= sum(txTPoSCoinstake));
    }

    // contract, MN payment, split
    {
        auto nAdjustedReward = nCoinstakePayment / 2;
        CMutableTransaction txContractSplitCoinstake = txSplitCoinstake;
        auto nMNPayment = nCoinstakePayment / 2;
        CTxOut outMNPayment(nMNPayment, scriptMnPayment);
        txContractSplitCoinstake.vout.push_back(outMNPayment);
        AdjustMasternodePayment(txContractSplitCoinstake, outMNPayment, contract);
        BOOST_CHECK_EQUAL(5, txContractSplitCoinstake.vout.size());
        CAmount amount = 0;
        for (int i = 1; i < 3; ++i) {
            BOOST_CHECK_EQUAL(contract.scriptTPoSAddress.ToString(), txContractSplitCoinstake.vout[i].scriptPubKey.ToString());
            amount += txContractSplitCoinstake.vout[i].nValue;
        }

        BOOST_CHECK_EQUAL(TPoSUtils::GetOwnerPayment(nAdjustedReward, contract.nOperatorReward), amount);
        BOOST_CHECK_EQUAL(TPoSUtils::GetOperatorPayment(nAdjustedReward, contract.nOperatorReward), txContractSplitCoinstake.vout[3].nValue);
        BOOST_CHECK_EQUAL(scriptMerchant.ToString(), txContractSplitCoinstake.vout[3].scriptPubKey.ToString());
        BOOST_CHECK_EQUAL(outMNPayment.nValue, txContractSplitCoinstake.vout[4].nValue);
        BOOST_CHECK_EQUAL(outMNPayment.scriptPubKey.ToString(), txContractSplitCoinstake.vout[4].scriptPubKey.ToString());
        BOOST_ASSERT(nCoinstakePayment >= sum(txContractSplitCoinstake));
    }

    // contract, no MN payment, no split
    {
        CMutableTransaction txNoMnCoinstake = txCoinstake;
        AdjustMasternodePayment(txNoMnCoinstake, {}, contract);
        BOOST_CHECK_EQUAL(3, txNoMnCoinstake.vout.size());
        BOOST_CHECK_EQUAL(contract.scriptTPoSAddress.ToString(), txNoMnCoinstake.vout[1].scriptPubKey.ToString());
        BOOST_CHECK_EQUAL(TPoSUtils::GetOwnerPayment(nCoinstakePayment, contract.nOperatorReward), txNoMnCoinstake.vout[1].nValue);
        BOOST_CHECK_EQUAL(TPoSUtils::GetOperatorPayment(nCoinstakePayment, contract.nOperatorReward), txNoMnCoinstake.vout[2].nValue);
        BOOST_CHECK_EQUAL(scriptMerchant.ToString(), txNoMnCoinstake.vout[2].scriptPubKey.ToString());
        BOOST_ASSERT(nCoinstakePayment >= sum(txNoMnCoinstake));
    }

    // contract, no MN payment, split
    {
        CMutableTransaction txSplitNoMnCoinstake = txSplitCoinstake;
        AdjustMasternodePayment(txSplitNoMnCoinstake, {}, contract);
        BOOST_CHECK_EQUAL(4, txSplitNoMnCoinstake.vout.size());
        BOOST_ASSERT(nCoinstakePayment >= sum(txSplitNoMnCoinstake));

        CAmount amount = 0;
        for (int i = 1; i < 3; ++i) {
            BOOST_CHECK_EQUAL(contract.scriptTPoSAddress.ToString(), txSplitNoMnCoinstake.vout[i].scriptPubKey.ToString());
            amount += txSplitNoMnCoinstake.vout[i].nValue;
        }

        BOOST_CHECK_EQUAL(TPoSUtils::GetOwnerPayment(nCoinstakePayment, contract.nOperatorReward), amount);
        BOOST_CHECK_EQUAL(TPoSUtils::GetOperatorPayment(nCoinstakePayment, contract.nOperatorReward), txSplitNoMnCoinstake.vout[3].nValue);
        BOOST_CHECK_EQUAL(scriptMerchant.ToString(), txSplitNoMnCoinstake.vout[3].scriptPubKey.ToString());
    }
}

BOOST_AUTO_TEST_SUITE_END()
