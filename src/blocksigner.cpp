#include <blocksigner.h>
#include <tpos/tposutils.h>
#include <tpos/activemerchantnode.h>
#include <keystore.h>
#include <primitives/block.h>
#include <utilstrencodings.h>
#include <messagesigner.h>

static CPubKey::InputScriptType GetScriptTypeFromDestination(const CTxDestination &dest)
{
    if (boost::get<CKeyID>(&dest)) {
        return CPubKey::InputScriptType::SPENDP2PKH;
    }
    if (boost::get<WitnessV0KeyHash>(&dest)) {
        return CPubKey::InputScriptType::SPENDWITNESS;
    }
    if (boost::get<CScriptID>(&dest)) {
        return CPubKey::InputScriptType::SPENDP2SHWITNESS;
    }
    return CPubKey::InputScriptType::SPENDUNKNOWN;
}

CBlockSigner::CBlockSigner(CBlock &block, const CKeyStore *keystore, const TPoSContract &contract, int chainHeight) :
    refBlock(block),
    refKeystore(keystore),
    refContract(contract),
    nChainHeight(chainHeight)
{

}

bool CBlockSigner::SignBlock()
{
    CKey keySecret;
    CPubKey::InputScriptType scriptType { CPubKey::InputScriptType::SPENDUNKNOWN };

    if(refBlock.IsProofOfStake())
    {
        const CTxOut& txout = refBlock.vtx[1]->vout[1];

        CTxDestination destination;
        if(!ExtractDestination(txout.scriptPubKey, destination))
        {
            return error("Failed to extract destination while signing: %s\n", txout.ToString());
        }

        if(refBlock.IsTPoSBlock())
        {
            CKeyID merchantKeyID;
            if(!refContract.merchantAddress.GetKeyID(merchantKeyID))
                return error("CBlockSigner::SignBlock() : merchant address is not P2PKH, critical error, can't accept.");

            if(merchantKeyID != activeMerchantnode.pubKeyMerchantnode.GetID())
                return error("CBlockSigner::SignBlock() : contract address is different from merchantnode address, won't sign.");

            scriptType = CPubKey::InputScriptType::SPENDP2PKH;

            keySecret = activeMerchantnode.keyMerchantnode;
        }
        else
        {
            auto keyid = GetKeyForDestination(*refKeystore, destination);
            if (keyid.IsNull()) {
                return error("CBlockSigner::SignBlock() : failed to get key for destination, won't sign.");
            }
            if (!refKeystore->GetKey(keyid, keySecret)) {
                return error("CBlockSigner::SignBlock() : Private key for address %s not known", EncodeDestination(destination));
            }

            scriptType = GetScriptTypeFromDestination(destination);
        }
    }

    if (nChainHeight >= Params().GetConsensus().nTPoSSignatureUpgradeHFHeight) {
        const auto &hash = refBlock.IsTPoSBlock() ? refBlock.GetTPoSHash() : refBlock.GetHash();
        return CMessageSigner::SignMessage(std::string(hash.begin(), hash.end()), refBlock.vchBlockSig, keySecret, scriptType);
    } else {
        return CHashSigner::SignHash(refBlock.IsTPoSBlock() ? refBlock.GetTPoSHash() : refBlock.GetHash(), keySecret, scriptType, refBlock.vchBlockSig);
    }
}

bool CBlockSigner::CheckBlockSignature() const
{
    if(refBlock.IsProofOfWork())
        return true;

    if(refBlock.vchBlockSig.empty())
        return false;

    const CTxOut& txout = refBlock.vtx[1]->vout[1];

    CTxDestination destination;

    if(!ExtractDestination(txout.scriptPubKey, destination)) {
        return error("CBlockSigner::CheckBlockSignature() : failed to extract destination from script: %s", txout.scriptPubKey.ToString());
    }

    auto hashMessage = refBlock.IsTPoSBlock() ? refBlock.GetTPoSHash() : refBlock.GetHash();
    if(refBlock.IsProofOfStake()) {
        if(refBlock.IsTPoSBlock()) {
            destination = refContract.merchantAddress.Get();
        }
    }
    else {
        return true;
    }

    std::string strError;
    if (nChainHeight >= Params().GetConsensus().nTPoSSignatureUpgradeHFHeight) {
        return CMessageSigner::VerifyMessage(destination, refBlock.vchBlockSig, std::string(hashMessage.begin(), hashMessage.end()), strError);
    } else {
        return CHashSigner::VerifyHash(hashMessage, destination, refBlock.vchBlockSig, strError);
    }
}
