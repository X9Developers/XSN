// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"
#include "script/standard.h"
#include "keystore.h"
#include "util.h"

uint256 CBlockHeader::GetHash() const
{
    return HashX11(BEGIN(nVersion), END(nNonce));
}

CBlock::CBlock()
{
    SetNull();
}

CBlock::CBlock(const CBlockHeader &header)
{
    SetNull();
    *((CBlockHeader*)this) = header;
}

uint256 CBlock::GetTPoSHash()
{
    return HashX11(BEGIN(nVersion), END(hashTPoSContractTx));
}

void CBlock::SetNull()
{
    CBlockHeader::SetNull();
    vtx.clear();
    txoutMasternode = CTxOut();
    voutSuperblock.clear();
    fChecked = false;
    vchBlockSig.clear();
}

CBlockHeader CBlock::GetBlockHeader() const
{
    CBlockHeader block;
    block.nVersion       = nVersion;
    block.nProofOfStake  = nProofOfStake;
    block.hashPrevBlock  = hashPrevBlock;
    block.hashMerkleRoot = hashMerkleRoot;
    block.nTime          = nTime;
    block.nBits          = nBits;
    block.nNonce         = nNonce;
    return block;
}

bool CBlockHeader::IsProofOfStake() const
{
    return nProofOfStake > 0;
}

bool CBlock::IsTPoSBlock() const
{
    return IsProofOfStake() && !hashTPoSContractTx.IsNull();
}

bool CBlock::IsProofOfWork() const
{
    return !IsProofOfStake();
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
                   GetHash().ToString(),
                   nVersion,
                   hashPrevBlock.ToString(),
                   hashMerkleRoot.ToString(),
                   nTime, nBits, nNonce,
                   vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    return s.str();
}

#if 0

bool CBlock::SignBlock(const CKeyStore& keystore)
{
    std::vector<std::vector<unsigned char>> vSolutions;
    txnouttype whichType;

    if(!IsProofOfStake())
    {
        for(unsigned int i = 0; i < vtx[0].vout.size(); i++)
        {
            const CTxOut& txout = vtx[0].vout[i];

            if (!Solver(txout.scriptPubKey, whichType, vSolutions))
                continue;

            if (whichType == TX_PUBKEY)
            {
                // Sign
                CKeyID keyID;
                keyID = CKeyID(uint160(vSolutions[0]));

                CKey key;
                if (!keystore.GetKey(keyID, key))
                    return false;

                //vector<unsigned char> vchSig;
                if (!key.Sign(GetHash(), vchBlockSig))
                    return false;

                return true;
            }
        }
    }
    else
    {
        const CTxOut& txout = vtx[1].vout[1];

        if (!Solver(txout.scriptPubKey, whichType, vSolutions))
            return false;

        if (whichType == TX_PUBKEYHASH)
        {

            CKeyID keyID;
            keyID = CKeyID(uint160(vSolutions[0]));

            CKey key;
            if (!keystore.GetKey(keyID, key))
                return false;

            //            if(IsTProofOfStake()){
            //              if(!key.Sign(GetTPoSHash(), vchBlockSig))
            //                return false;
            //            }

            //vector<unsigned char> vchSig;
            if (!key.Sign(GetHash(), vchBlockSig))
                return false;

            return true;

        }
        else if(whichType == TX_PUBKEY)
        {
            CKeyID keyID;
            keyID = CPubKey(vSolutions[0]).GetID();
            CKey key;
            if (!keystore.GetKey(keyID, key))
                return false;

            //            if(IsTProofOfStake()){
            //              if(!key.Sign(GetTPoSHash(), vchBlockSig))
            //                return false;
            //            }

            if (!key.Sign(GetHash(), vchBlockSig))
                return false;

            return true;
        }
    }

    LogPrintf("Sign failed\n");
    return false;
}
#endif


