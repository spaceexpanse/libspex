// Copyright (C) 2018-2019 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MOVER_LOGIC_HPP
#define MOVER_LOGIC_HPP

#include "proto/mover.pb.h"

#include "xgame/gamelogic.hpp"
#include "xgame/storage.hpp"

#include <json/json.h>

#include <string>

namespace mover
{

/**
 * The actual implementation of the game rules.
 */
class MoverLogic : public spacexpanse::GameLogic
{

protected:

  spacexpanse::GameStateData GetInitialStateInternal (unsigned& height,
                                               std::string& hashHex) override;

  spacexpanse::GameStateData ProcessForwardInternal (
      const spacexpanse::GameStateData& oldState, const Json::Value& blockData,
      spacexpanse::UndoData& undo) override;

  spacexpanse::GameStateData ProcessBackwardsInternal (
      const spacexpanse::GameStateData& newState, const Json::Value& blockData,
      const spacexpanse::UndoData& undo) override;

public:

  Json::Value GameStateToJson (const spacexpanse::GameStateData& state) override;

};

} // namespace mover

#endif // MOVER_LOGIC_HPP
