#ifndef NET_PROCESSING_XSN_H
#define NET_PROCESSING_XSN_H

#include <chainparams.h>

class CNode;
class CInv;
class CConnman;
class CNetMsgMaker;
class CDataStream;

namespace net_processing_xsn
{

bool ProcessGetData(CNode* pfrom, const Consensus::Params& consensusParams, CConnman* connman,
                    const CInv &inv);

void ProcessExtension(CNode* pfrom, const std::string &strCommand, CDataStream& vRecv, CConnman *connman);
}

#endif // NET_PROCESSING_XSN_H
