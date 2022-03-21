lib_LTLIBRARIES = libxgame.la
xgamedir = $(includedir)/xgame
rpcstubdir = $(xgamedir)/rpc-stubs

pkgconfig_DATA = libxgame.pc

EXTRA_DIST = rpc-stubs/game.json rpc-stubs/spacexpanse.json rpc-stubs/rod-core-wallet.json

RPC_STUBS = \
  rpc-stubs/gamerpcclient.h \
  rpc-stubs/gamerpcserverstub.h \
  rpc-stubs/xrpcclient.h \
  rpc-stubs/xrpcserverstub.h \
  rpc-stubs/xwalletrpcclient.h \
  rpc-stubs/xwalletrpcserverstub.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

libxgame_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(OPENSSL_CFLAGS) \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(ZLIB_CFLAGS) $(CURL_CFLAGS) $(MHD_CFLAGS) \
  $(GLOG_CFLAGS) $(SQLITE3_CFLAGS) $(LMDB_CFLAGS) $(ZMQ_CFLAGS)
libxgame_la_LIBADD = \
  $(top_builddir)/xutil/libxutil.la \
  $(OPENSSL_LIBS) \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(ZLIB_LIBS) $(CURL_LIBS) $(MHD_LIBS) \
  $(GLOG_LIBS) $(SQLITE3_LIBS) $(LMDB_LIBS) $(ZMQ_LIBS) \
  -lstdc++fs
libxgame_la_SOURCES = \
  defaultmain.cpp \
  game.cpp \
  gamelogic.cpp \
  gamerpcserver.cpp \
  heightcache.cpp \
  lmdbstorage.cpp \
  mainloop.cpp \
  pendingmoves.cpp \
  pruningqueue.cpp \
  rest.cpp \
  signatures.cpp \
  sqlitegame.cpp \
  sqlitestorage.cpp \
  storage.cpp \
  transactionmanager.cpp \
  zmqsubscriber.cpp
xgame_HEADERS = \
  defaultmain.hpp \
  game.hpp \
  gamelogic.hpp \
  gamerpcserver.hpp \
  heightcache.hpp \
  lmdbstorage.hpp \
  mainloop.hpp \
  pendingmoves.hpp \
  pruningqueue.hpp \
  rest.hpp \
  signatures.hpp \
  sqlitegame.hpp \
  sqlitestorage.hpp \
  storage.hpp \
  transactionmanager.hpp \
  zmqsubscriber.hpp
rpcstub_HEADERS = $(RPC_STUBS)

check_LTLIBRARIES = libtestutils.la
check_PROGRAMS = tests
TESTS = tests

libtestutils_la_CXXFLAGS = \
  -I$(top_srcdir) \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GTEST_CFLAGS)
libtestutils_la_LIBADD = \
  $(builddir)/libxgame.la \
  $(top_builddir)/xutil/libxutil.la \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GTEST_LIBS)
libtestutils_la_SOURCES = \
  testutils.cpp
TESTUTILHEADERS = testutils.hpp

tests_CXXFLAGS = \
  -I$(top_srcdir) \
  $(JSONCPP_CFLAGS) $(JSONRPCCLIENT_CFLAGS) $(JSONRPCSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GTEST_CFLAGS) $(SQLITE3_CFLAGS) $(LMDB_CFLAGS) $(ZMQ_CFLAGS)
tests_LDADD = \
  $(builddir)/libtestutils.la \
  $(builddir)/libxgame.la \
  $(top_builddir)/xutil/libxutil.la \
  $(JSONCPP_LIBS) $(JSONRPCCLIENT_LIBS) $(JSONRPCSERVER_LIBS) \
  $(GLOG_LIBS) $(GTEST_LIBS) $(SQLITE3_LIBS) $(LMDB_LIBS) $(ZMQ_LIBS)
tests_SOURCES = \
  game_tests.cpp \
  gamelogic_tests.cpp \
  heightcache_tests.cpp \
  lmdbstorage_tests.cpp \
  mainloop_tests.cpp \
  pendingmoves_tests.cpp \
  pruningqueue_tests.cpp \
  rest_tests.cpp \
  signatures_tests.cpp \
  sqlitegame_tests.cpp \
  sqlitestorage_tests.cpp \
  storage_tests.cpp \
  transactionmanager_tests.cpp \
  zmqsubscriber_tests.cpp
TESTHEADERS = storage_tests.hpp

check_HEADERS = $(TESTUTILHEADERS) $(TESTHEADERS)

rpc-stubs/gamerpcclient.h: $(srcdir)/rpc-stubs/game.json
	jsonrpcstub "$<" --cpp-client=GameRpcClient --cpp-client-file="$@"
rpc-stubs/gamerpcserverstub.h: $(srcdir)/rpc-stubs/game.json
	jsonrpcstub "$<" --cpp-server=GameRpcServerStub --cpp-server-file="$@"

rpc-stubs/xrpcclient.h: $(srcdir)/rpc-stubs/spacexpanse.json
	jsonrpcstub "$<" --cpp-client=XRpcClient --cpp-client-file="$@"
rpc-stubs/xrpcserverstub.h: $(srcdir)/rpc-stubs/spacexpanse.json
	jsonrpcstub "$<" --cpp-server=XRpcServerStub --cpp-server-file="$@"

rpc-stubs/xwalletrpcclient.h: $(srcdir)/rpc-stubs/rod-core-wallet.json
	jsonrpcstub "$<" --cpp-client=XWalletRpcClient --cpp-client-file="$@"
rpc-stubs/xwalletrpcserverstub.h: $(srcdir)/rpc-stubs/rod-core-wallet.json
	jsonrpcstub "$<" \
	  --cpp-server=XWalletRpcServerStub \
	  --cpp-server-file="$@"
