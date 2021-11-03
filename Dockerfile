# Start by setting up a base image with all packages that we need
# both for the build but also in the final image.  These are the dependencies
# that are required as dev packages also for using libxayagame.
FROM ubuntu:20.04 AS base
RUN apt -y update
ENV TZ="Europe/London"
ARG DEBIAN_FRONTEND="noninteractive"
RUN apt -y install \
  libargtable2-dev \
  libzmq3-dev \
  zlib1g-dev \
  libsqlite3-dev \
  liblmdb-dev \
  libgoogle-glog-dev \
  libgflags-dev \
  libprotobuf-dev \
  protobuf-compiler \
  python3 \
  python-protobuf \
  autoconf \
  autoconf-archive \
  automake \
  build-essential \
  cmake \
  git \
  libtool \
  pkg-config \
  libcurl4-openssl-dev \
  libssl-dev \
  libmicrohttpd-dev

# Build and install jsoncpp from source.  We need at least version >= 1.7.5,
# which includes an important fix for JSON parsing in some GSPs.
ARG JSONCPP_VERSION="1.9.4"
WORKDIR /usr/src/jsoncpp
RUN git clone -b ${JSONCPP_VERSION} \
  https://github.com/open-source-parsers/jsoncpp .
# FIXME: Version 1.9.4 has a broken pkg-config file, which we need
# to fix specifically.  Once a new version is released, we can get
# rid of this hack.
RUN git config user.email "test@example.com"
RUN git config user.name "Cherry Picker"
RUN git cherry-pick ac2870298ed5b5a96a688d9df07461b31f83e906
RUN cmake . \
  -DJSONCPP_WITH_PKGCONFIG_SUPPORT=ON \
  -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF
RUN make && make install/strip

# We need to install libjson-rpc-cpp from source.
WORKDIR /usr/src/libjson-rpc-cpp
RUN git clone https://github.com/cinemast/libjson-rpc-cpp .
RUN cmake . \
  -DREDIS_SERVER=NO -DREDIS_CLIENT=NO \
  -DCOMPILE_TESTS=NO -DCOMPILE_EXAMPLES=NO \
  -DWITH_COVERAGE=NO
RUN make && make install/strip

# We also need to install googletest from source.
WORKDIR /usr/src/googletest
RUN git clone https://github.com/google/googletest .
RUN cmake . && make && make install/strip

# The ZMQ C++ bindings need to be installed from source.
ARG CPPZMQ_VERSION="4.7.1"
WORKDIR /usr/src/cppzmq
RUN git clone -b v${CPPZMQ_VERSION} https://github.com/zeromq/cppzmq .
RUN cp zmq.hpp /usr/local/include

# Make sure all installed dependencies are visible.
RUN ldconfig

# Build and install libxayagame itself.  Make sure to clean out any
# potential garbage copied over in the build context.
WORKDIR /usr/src/libxayagame
COPY . .
RUN make distclean || true
RUN ./autogen.sh && ./configure && make && make install-strip
