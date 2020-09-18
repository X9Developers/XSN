#ifndef BLOCKSIGNER_H
#define BLOCKSIGNER_H

class CBlock;
class TPoSContract;
class CPubKey;
class CKey;
class CKeyStore;

struct CBlockSigner {

    CBlockSigner(CBlock &block, const CKeyStore *keystore, const TPoSContract &contract, int chainHeight);

    bool SignBlock();
    bool CheckBlockSignature() const;

    CBlock &refBlock;
    const CKeyStore *refKeystore;
    const TPoSContract &refContract;
    int nChainHeight{0};
};
#endif // BLOCKSIGNER_H
