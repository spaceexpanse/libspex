# SpaceXpanse Game Library

`libxgame` is a C++ library that makes it easy to implement games on the
[SpaceXpanse platform](https://spacexpanse.org/).  It takes care of the interaction with
the SpaceXpanse Core daemon, so that game developers only have to implement the
rules of their game.

[`mover`](mover/README.md) is a simple game using this library, where players
can move around an infinite plane.  It is fully functional, although mainly
meant as example and/or basis for more complex games.

Similarly, [`nonfungible`](nonfungible/README.md) is a simple implementation
of non-fungible assets on the SpaceXpanse platform.  It is useful as another example
(of using the
[SQLite integration](https://github.com/spacexpanse/libxgame/blob/master/xgame/sqlitegame.hpp)),
for testing [Democrit](https://github.com/spacexpanse/democrit) but also as an actual
application on SpaceXpanse for issuing and trading fungible and non-fungible tokens.

This repository also contains a framework for [**game
channels**](https://www.ledgerjournal.org/ojs/index.php/ledger/article/view/15)
as well as [Xships](ships/README.md), which is an example game for
channels.

## Building

To build `libxgame` and the example mover game, use the standard routine
for building autotools-based software:

```autogen.sh && ./configure && make```

After a successful build, you can optionally run `make check` and/or
`make install` to run tests and install the library and `moverd` on
your system, respectively.

### Prerequisites

`libxgame` has a couple of dependencies which need to be installed
for the configuration and/or build to be successful:

- [`libjsoncpp`](https://github.com/open-source-parsers/jsoncpp):
  The Debian package `libjsoncpp-dev` can be used.
- [`jsonrpccpp`](https://github.com/cinemast/libjson-rpc-cpp/):
  For Debian, the packages `libjsonrpccpp-dev` and `libjsonrpccpp-tools`
  are not fresh enough.  They need to be built from source;
  in particular, it must be a version that includes the commit
  [`4f24acaf4c6737cd07d40a02edad0a56147e0713`](https://github.com/cinemast/libjson-rpc-cpp/commit/4f24acaf4c6737cd07d40a02edad0a56147e0713).
- [`ZeroMQ`](https://zeromq.org/) with
  [C++ bindings](https://github.com/zeromq/cppzmq):
  Core ZeroMQ is available in the Debian package `libzmq3-dev`.  The C++
  bindings need to be installed from the source repository, as the version
  in the Debian package (at least for Debain 10 "Buster") is too old.
- [`zlib`](https://zlib.net):
  Available in Debian as `zlib1g-dev`.
- [SQLite3](https://www.sqlite.org/) with the
  [session extension](https://www.sqlite.org/sessionintro.html).
  In Debian, the `libsqlite3-dev` package can be installed.
  Alternatively, build from source and configure with `--enable-session`.
- [LMDB](https://symas.com/lmdb):  Available for Debian in the
  `liblmdb-dev` package.
- [`glog`](https://github.com/google/glog):
  Available for Debian as `libgoogle-glog-dev`.
- [`gflags`](https://github.com/gflags/gflags):
  The package (`libgflags-dev`) included with Debian can be used.
- [Protocol buffers](https://developers.google.com/protocol-buffers/)
  are used both in C++ and Python.  On Debian, the packages
  `libprotobuf-dev`, `protobuf-compiler` and `python-protobuf` can be used.
- [`eth-utils`](https://github.com/spacexpanse/eth-utils), which itself depends on
  [`libsecp256k1`](https://github.com/bitcoin-core/secp256k1).
  The latter is available on Debian as `libsecp256k1-dev`.

For the unit tests, also the
[Google test framework](https://github.com/google/googletest) is needed.
The package included with Debian 10 "Buster" is not fresh enough,
it should be built and installed from source instead.

For running the integration tests based on Python, install Python3 and
the [jsonrpclib](https://github.com/tcalmant/jsonrpclib/) library.  On Debian,
this is `python3-jsonrpclib-pelix`.

### Docker Image

We also provide a
[Dockerfile](https://github.com/spacexpanse/libxgame/blob/master/Dockerfile),
which can be used to build an image based on Debian that has all dependencies
and libxgame itself prebuilt and installed.  (Hint:  The file can also
just serve as documentation for how to get all dependencies and build
libxgame yourself on a Debian system.)

[Builds of this
image](https://hub.docker.com/repository/docker/spacexpanse/libxgame)
are also published on Docker Hub.
