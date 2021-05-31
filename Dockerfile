FROM debian:stretch

# Install required system packages
RUN apt-get update && apt-get install -y \
    apt-utils \
    automake \
    g++ \
    make \
    libtool \
    pkg-config \
    doxygen \
    libdb++-dev \
    curl \
    bsdmainutils \
    libboost-all-dev \
    libssl-dev \
    libevent-dev

# Install Berkeley DB 4.8
RUN curl -L http://download.oracle.com/berkeley-db/db-4.8.30.tar.gz | tar -xz -C /tmp && \
    cd /tmp/db-4.8.30/build_unix && \
    ../dist/configure --enable-cxx --includedir=/usr/include/bdb4.8 --libdir=/usr/lib && \
    make -j$(nproc) && make install && \
    cd / && rm -rf /tmp/db-4.8.30

RUN useradd -mU xsn

COPY . /tmp/xsncore/

RUN cd /tmp/xsncore && \
    ./autogen.sh && \
    ./configure --disable-tests --without-gui --prefix=/usr && \
    make -j$(nproc) && \
    make check && \
    make install && \
    cd / && rm -rf /tmp/xsncore

# Remove unused packages
RUN apt-get remove -y \
    automake \
    g++ \
    make \
    libtool \
    pkg-config \
    doxygen \
    libdb++-dev \
    curl \
    bsdmainutils \
    libboost-all-dev \
    libssl-dev \
    libevent-dev

USER xsn:xsn

RUN mkdir /home/xsn/.xsncore && \
    touch /home/xsn/.xsncore/xsn.conf

VOLUME [ "/home/xsn/.xsncore" ]

EXPOSE 62583
EXPOSE 8332
EXPOSE 18332

ENTRYPOINT ["/usr/bin/xsnd", "--conf=/home/xsn/.xsncore/xsn.conf"]

HEALTHCHECK CMD curl --fail http://127.0.0.1:$DEFAULT_PORT/ || exit 1
