# start-many Setup Guide

## Setting up your Controll Wallet

### Create New Wallet Addresses

1. Open the QT Wallet.
2. Click the Receive tab.
3. Fill in the form to request a payment.
    * Label: mn01
    * Amount: 15000 (optional)
    * Click *Request payment* button
5. Click the *Copy Address* button

Create a new wallet address for each Masternode.

### Send 15000 XSN to New Addresses

Send exactly 15000 XSN to each new address created above.

### Create New Masternode Private Keys

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Issue the following:

```masternode genkey```

*Note: A masternode private key will need to be created for each Masternode you run. You should not use the same masternode private key for multiple Masternodes.*

## <a name="masternodeconf"></a>Create masternode.conf file

Remember... this is local. Make sure your QT is not running.

Create the `masternode.conf` file in the same directory as your `wallet.dat`.

Copy the masternode private key and correspondig collateral output transaction that holds the 15000 XSN.

*Note: The masternode private key is **not** the same as a wallet private key. **Never** put your wallet private key in the masternode.conf file. That is almost equivalent to putting your 15000 XSN on the remote server and defeats the purpose of a hot/cold setup.*

### Get the collateral output

Open your QT Wallet and go to console (from the menu select `Tools` => `Debug Console`)

Issue the following:

```masternode outputs```

Make note of the hash (which is your collateral_output) and index.

### Enter your Masternode details into your masternode.conf file
[From the xsn github repo](https://github.com/X9Developers/XSN/blob/master/doc/masternode_conf.md)

`masternode.conf` format is a space seperated text file. Each line consisting of an alias, IP address followed by port, masternode private key, collateral output transaction id and collateral output index.

```
alias ipaddress:port masternode_private_key collateral_output collateral_output_index
```

Example:

```
mn01 127.0.0.1:62583 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
mn02 127.0.0.2:62583 93WaAb3htPJEV8E9aQcN23Jt97bPex7YvWfgMDTUdWJvzmrMqey aa9f1034d973377a5e733272c3d0eced1de22555ad45d6b24abadff8087948d4 0
```

Close your Qt wallet.

## Update xsn.conf on server

If you generated a new masternode private key, you will need to update the remote `xsn.conf` files.

Shut down the daemon and then edit the file.

File is located under same path as [masternode.conf](https://github.com/X9Developers/XSN/blob/master/doc/masternode_conf.md) and wallet.dat. 

Fill `rpcuser` and `rpcpassword` with random values, this will grant better security. 

### Edit the masternodeprivkey
If you generated a new masternode private key, you will need to update the `masternodeprivkey` value in your remote `xsn.conf` file.

### Sample of editited file:

```
rpcuser=XXXXXXXXXXXXX
rpcpassword=XXXXXXXXXXXXXXXXXXXXXXXXXXXX
rpcallowip=127.0.0.1
listen=1
server=1
daemon=1
maxconnections=24
masternode=1
masternodeprivkey=XXXXXXXXXXXXXXXXXXXXXXX
externalip=XXX.XXX.XXX.XXX
```

XXX.XXX.XXX.XXX - needs to be replaced with static ip address of masternode.

## Start your Masternodes

### Remote

If your remote server is not running, start your remote daemon as you normally would. 

You can confirm that remote server is on the correct block by issuing

```xsn-cli getinfo```

and comparing with the official explorer at https://explorer.xsn.org/chain/XSN

### Local

Finally... time to start from local.

#### Open up your QT Wallet

From the menu select `Tools` => `Debug Console`

If you want to review your `masternode.conf` setting before starting Masternodes, issue the following in the Debug Console:

```masternode list-conf```

Give it the eye-ball test. If satisfied, you can start your Masternodes.

1. `masternode start-alias [alias_from_masternode.conf]`  
Example ```masternode start-alias mn01```
OR
2. `masternode start-many`
OR
3. Use separate UI page to start/stop masternodes. (If you don't see, go to Options to enable it).

## Verify that Masternodes actually started

### Remote

Issue command `masternode status`
It should return you something like that:
```
xsn-cli masternode status
{
    "outpoint" : "<collateral_output>-<collateral_output_index>",
    "service" : "<ipaddress>:<port>",
    "pubkey" : "<15000 XSN address>",
    "status" : "Masternode successfully started"
}
```
Command output should have "_Masternode successfully started_" in its `status` field now. If it says "_not capable_" instead, you should check your config again.

### Local

_Hint: Bookmark it, you definitely will be using this site a lot._
