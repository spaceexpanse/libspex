// Copyright (C) 2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logic.hpp"

#include "moveprocessor.hpp"
#include "schema.hpp"

#include <glog/logging.h>

namespace nf
{

void
NonFungibleLogic::SetupSchema (spacexpanse::SQLiteDatabase& db)
{
  SetupDatabaseSchema (db);
}

void
NonFungibleLogic::GetInitialStateBlock (unsigned& height,
                                        std::string& hashHex) const
{
  const spacexpanse::Chain chain = GetChain ();
  switch (chain)
    {
    case spacexpanse::Chain::MAIN:
      height = 2'199'000;
      hashHex
          = "321ee13b84b0e5b9f07d43bcd3924c2a03006b043f687044807c4d66b4ac217f";
      break;

    case spacexpanse::Chain::TEST:
      height = 112'300;
      hashHex
          = "700f14e07b5d2a8d6836195d8a5f7ecd0aa4bf99d88631e99d29fd8ebb01a63f";
      break;

    case spacexpanse::Chain::REGTEST:
      height = 0;
      hashHex
          = "6f750b36d22f1dc3d0a6e483af45301022646dfc3b3ba2187865f5a7d6d83ab1";
      break;

    default:
      LOG (FATAL) << "Invalid chain value: " << static_cast<int> (chain);
    }
}

void
NonFungibleLogic::InitialiseState (spacexpanse::SQLiteDatabase& db)
{
  /* The initial state is simply an empty database with no assets or
     balances yet.  */
}

void
NonFungibleLogic::UpdateState (spacexpanse::SQLiteDatabase& db,
                               const Json::Value& blockData)
{
  MoveProcessor proc(db);
  proc.ProcessAll (blockData["moves"]);
}

Json::Value
NonFungibleLogic::GetStateAsJson (const spacexpanse::SQLiteDatabase& db)
{
  return StateJsonExtractor (db).FullState ();
}

Json::Value
NonFungibleLogic::GetCustomStateData (spacexpanse::Game& game, const StateCallback& cb)
{
  return SQLiteGame::GetCustomStateData (game, "data",
      [this, &cb] (const spacexpanse::SQLiteDatabase& db)
        {
          const StateJsonExtractor ext(db);
          return cb(ext);
        });
}

} // namespace nf
