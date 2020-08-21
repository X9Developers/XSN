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

static CMutableTransaction CreateContractTx(const CTxDestination &tposAddress, const CTxDestination &merchantAddress, uint16_t nOperatorReward, bool legacyContract)
{
    auto scriptTPoSAddress = GetScriptForDestination(tposAddress);
    auto scriptMerchantAddress = GetScriptForDestination(CTxDestination(merchantAddress));

    CMutableTransaction tx;

    CScript scriptContractMetadata;

    // dummy signature, just to know size
    std::vector<unsigned char> vchSignature(CPubKey::COMPACT_SIGNATURE_SIZE, '0');

    if (legacyContract) {
        auto tposAddressAsStr = EncodeDestination(tposAddress);
        auto merchantAddressAsStr = EncodeDestination(merchantAddress);
        scriptContractMetadata << OP_RETURN
                             << std::vector<unsigned char>(tposAddressAsStr.begin(), tposAddressAsStr.end())
                             << std::vector<unsigned char>(merchantAddressAsStr.begin(), merchantAddressAsStr.end())
                             << (100 - nOperatorReward)
                             << vchSignature;
    }

    tx.vout.push_back(CTxOut(1 * COIN, scriptTPoSAddress));
    tx.vout.push_back(CTxOut(0, scriptContractMetadata));
    return tx;
}

static void SignContract(CMutableTransaction &tx, const CKey &key, bool legacyContract)
{
    std::vector<unsigned char> vchSignature(CPubKey::COMPACT_SIGNATURE_SIZE, '0');
    auto firstInput = tx.vin.front().prevout;

    auto it = std::find_if(tx.vout.begin(), tx.vout.end(), [](const CTxOut &txOut) {
        return txOut.scriptPubKey.IsUnspendable();
    });

    auto vchSignatureCopy = vchSignature;
    vchSignature.clear();
    auto hashMessage = SerializeHash(firstInput);
    if (legacyContract) {
        if (!key.SignCompact(hashMessage, vchSignature)) {
            return;
        }

        it->scriptPubKey.FindAndDelete(CScript(vchSignatureCopy));
        it->scriptPubKey << vchSignature;
    } else {
        if (!CMessageSigner::SignMessage(std::string(hashMessage.begin(), hashMessage.end()), vchSignature, key, CPubKey::InputScriptType::SPENDP2PKH)) {
            return;
        }

        CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
        // construct new contract replacing old payload with new one
//        ds << TPoSContract({}, merchantAddress, tposAddress, merchantCommission, vchSignature);
        std::vector<unsigned char> payload(ds.begin(), ds.end());
        it->scriptPubKey.clear();
        it->scriptPubKey << OP_RETURN << payload;
    }
}

BOOST_AUTO_TEST_SUITE(tpos_tests)

BOOST_FIXTURE_TEST_CASE(tpos_create_legacy_contract, TestChain100Setup)
{
    m_coinbase_txns.emplace_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    m_coinbase_txns.emplace_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

    CKey tposAddressKey;
    tposAddressKey.MakeNewKey(true);
    CKey merchantAddressKey;
    merchantAddressKey.MakeNewKey(true);

    auto unsignedContract = CreateContractTx(tposAddressKey.GetPubKey().GetID(),
                                             merchantAddressKey.GetPubKey().GetID(), 1, true);
    TPoSContract tmp;
    std::string strError;
    BOOST_ASSERT(TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, 1, false, false, strError));
    BOOST_ASSERT(!TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, 1, true, true, strError));

    FundTransaction(unsignedContract, utxos, unsignedContract.vout[0].scriptPubKey);
    SignContract(unsignedContract, tposAddressKey, true);
    SignTransaction(unsignedContract, coinbaseKey);
    BOOST_ASSERT(TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, 1, true, false, strError));

    auto contract = TPoSContract::FromTPoSContractTx(MakeTransactionRef(unsignedContract));
    BOOST_ASSERT(contract.IsValid());

    CValidationState state;
    LOCK(cs_main);

    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, contract.txContract,
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));
}

BOOST_FIXTURE_TEST_CASE(tpos_create_invalid_legacy_contract, TestChain100Setup)
{
    m_coinbase_txns.emplace_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    m_coinbase_txns.emplace_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    m_coinbase_txns.emplace_back(CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

    CKey tposAddressKey;
    tposAddressKey.MakeNewKey(true);
    CKey merchantAddressKey;
    merchantAddressKey.MakeNewKey(true);

    auto createContract = [&](int nOperatorReward) {
        return CreateContractTx(tposAddressKey.GetPubKey().GetID(),
                                merchantAddressKey.GetPubKey().GetID(), nOperatorReward, true);
    };

    TPoSContract tmp;
    std::string strError;
    auto invalidContract = MakeTransactionRef(createContract(101));
    BOOST_ASSERT(!TPoSContract::FromTPoSContractTx(invalidContract).IsValid());
    BOOST_ASSERT(!TPoSUtils::CheckContract(invalidContract, tmp, 1, false, false, strError));

    auto unsignedContract = createContract(1);

    FundTransaction(unsignedContract, utxos, unsignedContract.vout[0].scriptPubKey);
    unsignedContract.vin[0].prevout = COutPoint(m_coinbase_txns[2]->GetHash(), 0);
    SignContract(unsignedContract, tposAddressKey, true);
    BOOST_ASSERT(TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, 1, true, false, strError));
    // replace input invalidating the signature
    unsignedContract.vin[0].prevout = COutPoint(m_coinbase_txns[3]->GetHash(), 0);
    BOOST_ASSERT(!TPoSUtils::CheckContract(MakeTransactionRef(unsignedContract), tmp, 1, true, false, strError));

    SignTransaction(unsignedContract, coinbaseKey);

    auto contract = TPoSContract::FromTPoSContractTx(MakeTransactionRef(unsignedContract));
    BOOST_ASSERT(contract.IsValid());

    CValidationState state;
    LOCK(cs_main);

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, contract.txContract,
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));
}

BOOST_AUTO_TEST_SUITE_END()
