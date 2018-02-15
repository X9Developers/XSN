
#include "netbase.h"
#include "merchantnodeconfig.h"
#include "util.h"
#include "chainparams.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

CMerchantnodeConfig merchantnodeConfig;

void CMerchantnodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
    CMerchantnodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CMerchantnodeConfig::read(std::string& strErr) {
    int linenumber = 1;
    boost::filesystem::path pathMerchantnodeConfigFile = GetMerchantnodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathMerchantnodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathMerchantnodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Merchantnode config file\n"
                          "# Format: alias IP:port merchantnodeprivkey collateral_output_txid collateral_output_index\n"
                          "# Example: mn1 127.0.0.2:19999 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for(std::string line; std::getline(streamConfig, line); linenumber++)
    {
        if(line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if(comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse merchantnode.conf") + "\n" +
                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        int port = 0;
        std::string hostname = "";
        SplitHostPort(ip, port, hostname);
        if(port == 0 || hostname == "") {
            strErr = _("Failed to parse host:port string") + "\n"+
                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }
        int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
        if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if(port != mainnetDefaultPort) {
                strErr = _("Invalid port detected in merchantnode.conf") + "\n" +
                        strprintf(_("Port: %d"), port) + "\n" +
                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                        strprintf(_("(must be %d for mainnet)"), mainnetDefaultPort);
                streamConfig.close();
                return false;
            }
        } else if(port == mainnetDefaultPort) {
            strErr = _("Invalid port detected in merchantnode.conf") + "\n" +
                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                    strprintf(_("(%d could be used only on mainnet)"), mainnetDefaultPort);
            streamConfig.close();
            return false;
        }


        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}
