#ifndef BLOCKSIGNER_H
#define BLOCKSIGNER_H

class CBlock;
class TPoSContract;
class CPubKey;
class CKey;
class CKeyStore;

struct CBlockSigner {

    CBlockSigner(CBlock &block, const CKeyStore *keystore, const TPoSContract &contract);

    bool SignBlock();
    bool CheckBlockSignature() const;

    CBlock &refBlock;
    const CKeyStore *refKeystore;
    const TPoSContract &refContract;
};
#endif // BLOCKSIGNER_H
