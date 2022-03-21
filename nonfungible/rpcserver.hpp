// Copyright (C) 2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NONFUNGIBLE_RPCSERVER_HPP
#define NONFUNGIBLE_RPCSERVER_HPP

#include "logic.hpp"
#include "rpc-stubs/nfrpcserverstub.h"

#include "xgame/game.hpp"

#include <json/json.h>
#include <jsonrpccpp/server.h>

#include <string>

namespace nf
{

/**
 * RPC interface for nonfungibled.
 */
class RpcServer : public NFRpcServerStub
{

private:

  /** The underlying Game instance that manages everything.  */
  spacexpanse::Game& game;

  /** The NF logic instance for the SQLite database.  */
  NonFungibleLogic& logic;

public:

  explicit RpcServer (spacexpanse::Game& g, NonFungibleLogic& l,
                      jsonrpc::AbstractServerConnector& conn)
    : NFRpcServerStub(conn), game(g), logic(l)
  {}

  void stop () override;

  Json::Value getcurrentstate () override;
  Json::Value getnullstate () override;
  Json::Value getpendingstate () override;
  std::string waitforchange (const std::string& knownBlock) override;
  Json::Value waitforpendingchange (int knownVersion) override;

  Json::Value listassets () override;
  Json::Value getassetdetails (const Json::Value& asset) override;
  Json::Value getbalance (const Json::Value& asset,
                          const std::string& name) override;
  Json::Value getuserbalances (const std::string& name) override;

};

} // namespace nf

#endif // NONFUNGIBLE_RPCSERVER_HPP
