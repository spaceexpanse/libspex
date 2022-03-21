// Copyright (C) 2018-2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gamerpcserver.hpp"

#include <glog/logging.h>

namespace spacexpanse
{

void
GameRpcServer::stop ()
{
  LOG (INFO) << "RPC method called: stop";
  game.RequestStop ();
}

Json::Value
GameRpcServer::getcurrentstate ()
{
  LOG (INFO) << "RPC method called: getcurrentstate";
  return game.GetCurrentJsonState ();
}

Json::Value
GameRpcServer::getnullstate ()
{
  LOG (INFO) << "RPC method called: getnullstate";
  return game.GetNullJsonState ();
}

Json::Value
GameRpcServer::getpendingstate ()
{
  LOG (INFO) << "RPC method called: getpendingstate";
  return game.GetPendingJsonState ();
}

std::string
GameRpcServer::waitforchange (const std::string& knownBlock)
{
  LOG (INFO) << "RPC method called: waitforchange " << knownBlock;
  return DefaultWaitForChange (game, knownBlock);
}

Json::Value
GameRpcServer::waitforpendingchange (const int oldVersion)
{
  LOG (INFO) << "RPC method called: waitforpendingchange " << oldVersion;
  return game.WaitForPendingChange (oldVersion);
}

std::string
GameRpcServer::DefaultWaitForChange (const Game& g,
                                     const std::string& knownBlock)
{
  uint256 oldBlock;
  oldBlock.SetNull ();
  if (!knownBlock.empty () && !oldBlock.FromHex (knownBlock))
    LOG (ERROR)
        << "Invalid block hash passed as known block: " << knownBlock;

  uint256 newBlock;
  g.WaitForChange (oldBlock, newBlock);

  /* If there is no best block so far, return empty string.  */
  if (newBlock.IsNull ())
    return "";

  /* Otherwise, return the block hash.  */
  return newBlock.ToHex ();
}

} // namespace spacexpanse
