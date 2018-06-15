Shared Libraries
================

## xsnconsensus

The purpose of this library is to make the verification functionality that is critical to XSN's consensus available to other applications, e.g. to language bindings.

### API

The interface is defined in the C header `xsnconsensus.h` located in  `src/script/xsnconsensus.h`.

#### Version

`xsnconsensus_version` returns an `unsigned int` with the API version *(currently at an experimental `0`)*.

#### Script Validation

`xsnconsensus_verify_script` returns an `int` with the status of the verification. It will be `1` if the input script correctly spends the previous output `scriptPubKey`.

##### Parameters
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `xsnconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

##### Script Flags
- `xsnconsensus_SCRIPT_FLAGS_VERIFY_NONE`
- `xsnconsensus_SCRIPT_FLAGS_VERIFY_P2SH` - Evaluate P2SH ([BIP16](https://github.com/xsn/bips/blob/master/bip-0016.mediawiki)) subscripts
- `xsnconsensus_SCRIPT_FLAGS_VERIFY_DERSIG` - Enforce strict DER ([BIP66](https://github.com/xsn/bips/blob/master/bip-0066.mediawiki)) compliance
- `xsnconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY` - Enforce NULLDUMMY ([BIP147](https://github.com/xsn/bips/blob/master/bip-0147.mediawiki))
- `xsnconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY` - Enable CHECKLOCKTIMEVERIFY ([BIP65](https://github.com/xsn/bips/blob/master/bip-0065.mediawiki))
- `xsnconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY` - Enable CHECKSEQUENCEVERIFY ([BIP112](https://github.com/xsn/bips/blob/master/bip-0112.mediawiki))
- `xsnconsensus_SCRIPT_FLAGS_VERIFY_WITNESS` - Enable WITNESS ([BIP141](https://github.com/xsn/bips/blob/master/bip-0141.mediawiki))

##### Errors
- `xsnconsensus_ERR_OK` - No errors with input parameters *(see the return value of `xsnconsensus_verify_script` for the verification status)*
- `xsnconsensus_ERR_TX_INDEX` - An invalid index for `txTo`
- `xsnconsensus_ERR_TX_SIZE_MISMATCH` - `txToLen` did not match with the size of `txTo`
- `xsnconsensus_ERR_DESERIALIZE` - An error deserializing `txTo`
- `xsnconsensus_ERR_AMOUNT_REQUIRED` - Input amount is required if WITNESS is used

### Example Implementations
- [NXSN](https://github.com/NicolasDorier/NXSN/blob/master/NXSN/Script.cs#L814) (.NET Bindings)
- [node-libxsnconsensus](https://github.com/bitpay/node-libxsnconsensus) (Node.js Bindings)
- [java-libxsnconsensus](https://github.com/dexX7/java-libxsnconsensus) (Java Bindings)
- [xsnconsensus-php](https://github.com/Bit-Wasp/xsnconsensus-php) (PHP Bindings)
