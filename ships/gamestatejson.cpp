// Copyright (C) 2019-2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gamestatejson.hpp"

#include <gamechannel/database.hpp>
#include <gamechannel/gamestatejson.hpp>

#include <glog/logging.h>

namespace ships
{

Json::Value
GameStateJson::GetFullJson () const
{
  Json::Value stats(Json::objectValue);
  auto stmt = db.PrepareRo (R"(
    SELECT `name`, `won`, `lost`
      FROM `game_stats`
  )");
  while (stmt.Step ())
    {
      const auto name = stmt.Get<std::string> (0);

      Json::Value cur(Json::objectValue);
      cur["won"] = static_cast<Json::Int64> (stmt.Get<int64_t> (1));
      cur["lost"] = static_cast<Json::Int64> (stmt.Get<int64_t> (2));

      stats[name] = cur;
    }

  Json::Value res(Json::objectValue);
  res["gamestats"] = stats;

  spacexpanse::ChannelsTable tbl(const_cast<spacexpanse::SQLiteDatabase&> (db));
  res["channels"] = spacexpanse::AllChannelsGameStateJson (tbl, rules);

  return res;
}

} // namespace ships
