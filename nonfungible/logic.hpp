// Copyright (C) 2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NONFUNGIBLE_LOGIC_HPP
#define NONFUNGIBLE_LOGIC_HPP

#include "statejson.hpp"

#include <xgame/sqlitegame.hpp>
#include <xgame/sqlitestorage.hpp>

#include <json/json.h>

#include <functional>
#include <string>

namespace nf
{

/**
 * The game logic implementation for the non-fungible game-state processor.
 */
class NonFungibleLogic : public spacexpanse::SQLiteGame
{

protected:

  void SetupSchema (spacexpanse::SQLiteDatabase& db) override;

  void GetInitialStateBlock (unsigned& height,
                             std::string& hashHex) const override;
  void InitialiseState (spacexpanse::SQLiteDatabase& db) override;

  void UpdateState (spacexpanse::SQLiteDatabase& db,
                    const Json::Value& blockData) override;

  Json::Value GetStateAsJson (const spacexpanse::SQLiteDatabase& db) override;

public:

  /**
   * Type for a callback that extracts custom JSON from the game state
   * (through a StateJsonExtractor instance).
   */
  using StateCallback
      = std::function<Json::Value (const StateJsonExtractor& ext)>;

  NonFungibleLogic () = default;

  NonFungibleLogic (const NonFungibleLogic&) = delete;
  void operator= (const NonFungibleLogic&) = delete;

  /**
   * Extracts some custom JSON from the current game-state database, using
   * the provided extractor callback, which can then operate through a
   * StateJsonExtractor instance.
   */
  Json::Value GetCustomStateData (spacexpanse::Game& game, const StateCallback& cb);

};

} // namespace nf

#endif // NONFUNGIBLE_LOGIC_HPP
