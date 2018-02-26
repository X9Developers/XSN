#include "blocksigner.h"
#include "tpos/tposutils.h"
#include "tpos/activemerchantnode.h"
#include "keystore.h"
#include "primitives/block.h"
#include "utilstrencodings.h"

CBlockSigner::CBlockSigner(CBlock &block, const CKeyStore &keystore, const TPoSContract &contract) :
    refBlock(block),
    refKeystore(keystore),
    refContract(contract)
{

}

bool CBlockSigner::SignBlock()
{
    std::vector<std::vector<unsigned char>> vSolutions;
    txnouttype whichType;

    CKey keySecret;

    if(refBlock.IsProofOfStake())
    {
        const CTxOut& txout = refBlock.vtx[1].vout[1];

        if (!Solver(txout.scriptPubKey, whichType, vSolutions))
            return false;

        if(refBlock.IsTPoSBlock())
        {
            CKeyID merchantKeyID;
            if(!refContract.merchantAddress.GetKeyID(merchantKeyID))
                return error("CBlockSigner::SignBlock() : merchant address is not P2PKH, critical error, can't accept.");

            CBitcoinSecret secret;
            secret.SetString("cSfeSxGb2tXbuYbZxJbnKqbiFwTZEWqbZhVSUKi2hdti61GdJMS8");
            auto tempKey = secret.GetKey();

            if(merchantKeyID != tempKey.GetPubKey()/*activeMerchantnode.pubKeyMerchantnode*/.GetID())
                return error("CBlockSigner::SignBlock() : contract address is different from merchantnode address, won't sign.");

            keySecret = tempKey/*activeMerchantnode.keyMerchantnode*/;
        }
        else
        {
            CKeyID keyID;

            if (whichType == TX_PUBKEYHASH)
            {
                keyID = CKeyID(uint160(vSolutions[0]));
            }
            else if(whichType == TX_PUBKEY)
            {
                keyID = CPubKey(vSolutions[0]).GetID();
            }

            if (!refKeystore.GetKey(keyID, keySecret))
                return false;
        }
    }
    else
    {
        const CTxOut& txout = refBlock.vtx[0].vout[0];

        if (!Solver(txout.scriptPubKey, whichType, vSolutions))
            return false;

        CKeyID keyID;

        if (whichType == TX_PUBKEYHASH)
        {
            keyID = CKeyID(uint160(vSolutions[0]));
        }
        else if(whichType == TX_PUBKEY)
        {
            keyID = CPubKey(vSolutions[0]).GetID();
        }

        if (!refKeystore.GetKey(keyID, keySecret))
            return false;
    }


    return keySecret.SignCompact(refBlock.IsTPoSBlock() ? refBlock.GetTPoSHash() : refBlock.GetHash(), refBlock.vchBlockSig);
}

bool CBlockSigner::CheckBlockSignature() const
{
    if(refBlock.IsProofOfWork())
        return true;

    if(refBlock.vchBlockSig.empty())
        return false;

    std::vector<std::vector<unsigned char>> vSolutions;
    txnouttype whichType;

    const CTxOut& txout = refBlock.vtx[1].vout[1];

    if (!Solver(txout.scriptPubKey, whichType, vSolutions))
        return false;

    CKeyID signatureKeyID;
    CPubKey recoveredKey;

    auto hashMessage = refBlock.IsTPoSBlock() ? refBlock.GetTPoSHash() : refBlock.GetHash();

    if(!recoveredKey.RecoverCompact(hashMessage, refBlock.vchBlockSig))
        return error("CBlockSigner::CheckBlockSignature() : failed to recover public key from signature");

    if(refBlock.IsProofOfStake())
    {
        if(refBlock.IsTPoSBlock())
        {
            if(!refContract.merchantAddress.GetKeyID(signatureKeyID))
                return error("CBlockSigner::CheckBlockSignature() : merchant address is not P2PKH, critical error, can't accept.");
        }
        else
        {
            if (whichType == TX_PUBKEYHASH)
            {
                signatureKeyID = CKeyID(uint160(vSolutions[0]));
            }
            else if(whichType == TX_PUBKEY)
            {
                signatureKeyID = CPubKey(vSolutions[0]).GetID();
            }
        }
    }
    else
    {
        const CTxOut& txout = refBlock.vtx[0].vout[0];

        if (!Solver(txout.scriptPubKey, whichType, vSolutions))
            return false;

        if (whichType == TX_PUBKEYHASH)
        {
            signatureKeyID = CKeyID(uint160(vSolutions[0]));
        }
        else if(whichType == TX_PUBKEY)
        {
            signatureKeyID = CPubKey(vSolutions[0]).GetID();
        }
    }

    return recoveredKey.GetID() == signatureKeyID;
}
