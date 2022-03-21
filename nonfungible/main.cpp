// Copyright (C) 2020-2021 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"

#include "logic.hpp"
#include "pending.hpp"
#include "rpcserver.hpp"

#include "xgame/defaultmain.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <iostream>

namespace
{

DEFINE_string (x_rpc_url, "",
               "URL at which SpaceXpanse Core's JSON-RPC interface is available");
DEFINE_int32 (x_rpc_protocol, 1,
              "JSON-RPC version for connecting to SpaceXpanse Core");
DEFINE_bool (x_rpc_wait, false,
             "whether to wait on startup for SpaceXpanse Core to be available");

DEFINE_int32 (game_rpc_port, 0,
              "the port at which the GSP JSON-RPC server will be started"
              " (if non-zero)");
DEFINE_bool (game_rpc_listen_locally, true,
             "whether the GSP's JSON-RPC server should listen locally");

DEFINE_int32 (enable_pruning, -1,
              "if non-negative (including zero), old undo data will be pruned"
              " and only as many blocks as specified will be kept");

DEFINE_string (datadir, "",
               "base data directory for state data"
               " (will be extended by 'nf' and the chain)");

DEFINE_bool (pending_moves, true,
             "whether or not pending moves should be tracked");

class NFInstanceFactory : public spacexpanse::CustomisedInstanceFactory
{

private:

  /**
   * Reference to the NonFungibleLogic instance.  This is needed to construct
   * the RPC server.
   */
  nf::NonFungibleLogic& logic;

public:

  explicit NFInstanceFactory (nf::NonFungibleLogic& l)
    : logic(l)
  {}

  std::unique_ptr<spacexpanse::RpcServerInterface>
  BuildRpcServer (spacexpanse::Game& game,
                  jsonrpc::AbstractServerConnector& conn) override
  {
    std::unique_ptr<spacexpanse::RpcServerInterface> res;
    res.reset (new spacexpanse::WrappedRpcServer<nf::RpcServer> (game, logic, conn));
    return res;
  }

};

} // anonymous namespace

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Run nonfungible GSP");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  if (FLAGS_x_rpc_url.empty ())
    {
      std::cerr << "Error: --x_rpc_url must be set" << std::endl;
      return EXIT_FAILURE;
    }
  if (FLAGS_datadir.empty ())
    {
      std::cerr << "Error: --datadir must be specified" << std::endl;
      return EXIT_FAILURE;
    }

  spacexpanse::GameDaemonConfiguration config;
  config.XRpcUrl = FLAGS_x_rpc_url;
  config.XJsonRpcProtocol = FLAGS_x_rpc_protocol;
  config.XRpcWait = FLAGS_x_rpc_wait;
  if (FLAGS_game_rpc_port != 0)
    {
      config.GameRpcServer = spacexpanse::RpcServerType::HTTP;
      config.GameRpcPort = FLAGS_game_rpc_port;
      config.GameRpcListenLocally = FLAGS_game_rpc_listen_locally;
    }
  config.EnablePruning = FLAGS_enable_pruning;
  config.DataDirectory = FLAGS_datadir;

  nf::NonFungibleLogic logic;

  NFInstanceFactory fact(logic);
  config.InstanceFactory = &fact;

  nf::PendingMoves pending(logic);
  if (FLAGS_pending_moves)
    config.PendingMoves = &pending;

  return spacexpanse::SQLiteMain (config, "nf", logic);
}
