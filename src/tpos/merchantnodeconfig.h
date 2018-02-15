
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_MERCHANTNODECONFIG_H_
#define SRC_MERCHANTNODECONFIG_H_

class CMerchantnodeConfig;
extern CMerchantnodeConfig masternodeConfig;

class CMerchantnodeConfig
{

public:

    class CMerchantnodeEntry {

    private:
        std::string alias;
        std::string ip;
        std::string merchantAddress;

    public:

        CMerchantnodeEntry(std::string alias, std::string ip, std::string merchantAddress) {
            this->alias = alias;
            this->ip = ip;
            this->merchantAddress = merchantAddress;
        }

        const std::string& getAlias() const {
            return alias;
        }

        void setAlias(const std::string& alias) {
            this->alias = alias;
        }

        const std::string& getMerchantAddress() const {
            return merchantAddress;
        }

        void setMerchantAddress(const std::string& merchantAddress) {
            this->merchantAddress = merchantAddress;
        }

        const std::string& getIp() const {
            return ip;
        }

        void setIp(const std::string& ip) {
            this->ip = ip;
        }
    };

    CMerchantnodeConfig() {
        entries = std::vector<CMerchantnodeEntry>();
    }

    void clear();
    bool read(std::string& strErr);
    void add(std::string alias, std::string ip, std::string merchantAddress);

    std::vector<CMerchantnodeEntry>& getEntries() {
        return entries;
    }

    int getCount() {
        return (int)entries.size();
    }

private:
    std::vector<CMerchantnodeEntry> entries;


};


#endif /* SRC_MERCHANTNODECONFIG_H_ */
