FROM debian:stretch-slim as build-layer

LABEL maintainer="Peponi <pep0ni@pm.com>" \
      description="will compile & run (Stakenet) xsnd"

ARG BERKELEYDB_URL="http://download.oracle.com/berkeley-db/"
ARG BERKELEYDB_VERSION="db-4.8.30"
ARG DEFAULT_PORT="62583"
ARG MAINNET_PORT="8332"
ARG TESTNET_PORT="18332"

ENV BERKELEYDB_VERSION=$BERKELEYDB_VERSION \
    BERKELEYDB_URL=$BERKELEYDB_URL \
    PKG_URL=$BERKELEYDB_URL$BERKELEYDB_VERSION \
    DEFAULT_PORT=$DEFAULT_PORT \
    MAINNET_PORT=$MAINNET_PORT \
    TESTNET_PORT=$TESTNET_PORT

COPY . /tmp/xsncore/

# Install required system packages
RUN apt-get update \
    && apt-get upgrade -y \
    && apt-get install --no-install-recommends --no-install-suggests -y \
        apt-utils \
        curl \
        g++ \
        make \
        automake \
        pkg-config \
        doxygen \
        bsdmainutils \
        libtool \
        libdb++-dev \
        libboost-system-dev \
        libboost-filesystem-dev \
        libboost-chrono-dev \
        libboost-program-options-dev \
        libboost-test-dev \
        libboost-thread-dev \
        libssl-dev \
        libevent-dev \
# Install Berkeley DB
    && curl -kL $PKG_URL.tar.gz | tar -xz -C /tmp \
    && cd /tmp/$BERKELEYDB_VERSION/build_unix \
    && ../dist/configure --enable-cxx --includedir=/usr/include/bdb4.8 --libdir=/usr/lib \
# fix error in compile log, as done in https://hub.docker.com/r/lncm/berkeleydb/dockerfile
    && sed s/__atomic_compare_exchange/__atomic_compare_exchange_db/g -i /tmp/$BERKELEYDB_VERSION/dbinc/atomic.h \
    && make -j$(nproc) \
    && make install \
    && cd /tmp/xsncore \
# Install xsnd
    && ./autogen.sh \
    && ./configure --disable-tests --without-gui --prefix=/usr \
    && make -j$(nproc) \
    && make check \
    && make install \
    && apt-get purge -y \
        apt-utils \
        curl \
        g++ \
        make \
        automake \
        pkg-config \
        doxygen \
        bsdmainutils \
        libtool \
        libdb++-dev \
        libboost-system-dev \
        libboost-filesystem-dev \
        libboost-chrono-dev \
        libboost-program-options-dev \
        libboost-test-dev \
        libboost-thread-dev \
        libssl-dev \
        libevent-dev

FROM debian:stretch-slim

COPY --from=build-layer /usr/bin/*xsn /usr/bin/
COPY --from=build-layer /usr/bin/xsn* /usr/bin/
COPY --from=build-layer /usr/lib/libdb* /usr/lib/
COPY --from=build-layer /usr/lib/pkgconfig/* /usr/lib/pkgconfig/
COPY --from=build-layer /usr/include/xsn* /usr/include/

RUN apt-get update \
    && apt-get upgrade -y \
    && apt-get install --no-install-recommends --no-install-suggests -y curl \
    && useradd -mU xsn \
    && mkdir /home/xsn/.xsncore \
    && touch /home/xsn/.xsncore/xsn.conf \
    && chown -R xsn:xsn /home/xsn/.xsncore

USER xsn:xsn    

VOLUME [ "/home/xsn/.xsncore" ]

EXPOSE $DEFAULT_PORT $MAINNET_PORT $TESTNET_PORT

ENTRYPOINT ["/usr/bin/xsnd", "--conf=/home/xsn/.xsncore/xsn.conf"]

HEALTHCHECK CMD curl --fail http://127.0.0.1:$DEFAULT_PORT/ || exit 1
