XSN Core integration/staging tree
=====================================

[![Build Status](https://api.travis-ci.org/X9Developers/XSN.svg?branch=master)](https://travis-ci.org/X9Developers/XSN)

NOTE: Stakenet works extensively on off-chain Layer 2 (L2), as such development is across mutliple public and private repositories other than the main blockchain repository XSN.

Below are a collection of links for our current public development work.

- Web DEX interface: https://github.com/X9Developers/stakenet-web-ui

- Infrastructure/Blockchain Explorer : https://github.com/X9Developers/block-explorer

- DEX API Documentation for bot development: https://github.com/X9Developers/DexAPI

- If you are interested in finding out more about our current closed Beta L2 Light Wallet, please contact us in our Discord found here: https://discord.gg/cyF5yCA. Please note, this is currently being developed in a private repository.

Stakenet Cloud
----------------

https://stakenet.io


What is XSN?
----------------

XSN is an experimental digital currency that enables instant payments to
anyone, anywhere in the world. XSN uses peer-to-peer technology to operate
with no central authority: managing transactions and issuing money are carried
out collectively by the network. XSN Core is the name of open source
software which enables the use of this currency.

For more information, as well as an immediately useable, binary version of
the XSN Core software, see https://stakenet.io/, or read the
[original whitepaper](https://stakenet.io/Whitepaper_Stakenet_V3.0_EN.pdf).

License
-------

XSN Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/X9Developers/XSN/tags) are created
regularly to indicate new official, stable release versions of XSN Core.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python, that are run automatically on the build server.
These tests can be run (if the [test dependencies](/test) are installed) with: `test/functional/test_runner.py`

The Travis CI system makes sure that every pull request is built for Windows, Linux, and OS X, and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

Running with Docker
-------

If you are already familiar with Docker, then running XSN with Docker might be the the easier method for you. To run XSN using this method, first install [Docker](https://docs.docker.com/install/). After this you may
continue with the following instructions.

Please note that we currently don't support the GUI when running with Docker. Therefore, you can only use RPC (via HTTP or the `xsn-cli` utility) to interact with XSN via this method.

Right now we don't store the image in docker hub, so you need to build it on your own:

```sh
docker build . -t xsn
```

Start XSN daemon: ( you might consider to bind custom ports in case you want to run a node or make usage of the json-rpc api [https://docs.docker.com/engine/reference/run/](https://docs.docker.com/engine/reference/run/))

```sh
docker run -d -P --name xsn xsn:latest
```

View current block count (this might take a while since the daemon needs to find other nodes and download blocks first):

```sh
docker exec xsn xsn-cli getblockcount
```

View connected nodes:

```sh
docker exec xsn xsn-cli getpeerinfo
```

Stop daemon:

```sh
docker stop xsn
```

Backup wallet:

```sh
docker cp xsn:/home/xsn/.xsncore/wallet.dat .
```

Start daemon again:

```sh
docker start xsn
```

Translations
------------

Please reach out to us in Discord if you wish to offer translations and see the below translation process for further details [translation process](doc/translation_process.md).

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.
