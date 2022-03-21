// Copyright (C) 2018-2019 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logic.hpp"

#include "moves.hpp"

#include <glog/logging.h>

using spacexpanse::Chain;
using spacexpanse::GameStateData;
using spacexpanse::UndoData;

namespace mover
{

GameStateData
MoverLogic::GetInitialStateInternal (unsigned& height, std::string& hashHex)
{
  const Chain chain = GetContext ().GetChain ();
  switch (chain)
    {
    case Chain::MAIN:
      height = 125000;
      hashHex
          = "2aed5640a3be8a2f32cdea68c3d72d7196a7efbfe2cbace34435a3eef97561f2";
      break;

    case Chain::TEST:
      height = 10000;
      hashHex
          = "73d771be03c37872bc8ccd92b8acb8d7aa3ac0323195006fb3d3476784981a37";
      break;

    case Chain::REGTEST:
      height = 0;
      hashHex
          = "6f750b36d22f1dc3d0a6e483af45301022646dfc3b3ba2187865f5a7d6d83ab1";
      break;

    default:
      LOG (FATAL) << "Unexpected chain: " << ChainToString (chain);
    }

  /* In all cases, the initial game state is just empty.  */
  proto::GameState state;

  GameStateData result;
  CHECK (state.SerializeToString (&result));

  return result;
}

GameStateData
MoverLogic::ProcessForwardInternal (const GameStateData& oldState,
                                    const Json::Value& blockData,
                                    UndoData& undoData)
{
  proto::GameState state;
  CHECK (state.ParseFromString (oldState));
  proto::UndoData undo;

  /* Go over all moves, adding/updating players in the state.  */
  for (const auto& m : blockData["moves"])
    {
      const std::string& name = m["name"].asString ();
      const Json::Value& obj = m["move"];

      proto::Direction dir;
      unsigned steps;
      if (!ParseMove (obj, dir, steps))
        {
          LOG (WARNING) << "Ignoring invalid move:\n" << obj;
          continue;
        }

      const auto mi = state.mutable_players ()->find (name);
      const bool isNew = (mi == state.mutable_players ()->end ());
      proto::PlayerState* p;
      if (isNew)
        p = &(*state.mutable_players ())[name];
      else
        p = &mi->second;

      proto::PlayerUndo& u = (*undo.mutable_players ())[name];
      if (isNew)
        {
          u.set_is_new (true);
          p->set_x (0);
          p->set_y (0);
        }
      else
        {
          u.set_previous_dir (p->dir ());
          u.set_previous_steps_left (p->steps_left ());
        }

      p->set_dir (dir);
      p->set_steps_left (steps);
    }

  /* Go over all players in the state and move them.  */
  for (auto& mi : *state.mutable_players ())
    {
      const std::string& name = mi.first;
      proto::PlayerState& p = mi.second;

      if (p.dir () == proto::NONE)
        continue;

      CHECK (p.steps_left () > 0);
      int dx, dy;
      GetDirectionOffset (p.dir (), dx, dy);
      p.set_x (p.x () + dx);
      p.set_y (p.y () + dy);

      p.set_steps_left (p.steps_left () - 1);
      if (p.steps_left () == 0)
        {
          proto::PlayerUndo& u = (*undo.mutable_players ())[name];
          u.set_finished_dir (p.dir ());
          p.set_dir (proto::NONE);
        }
    }

  CHECK (undo.SerializeToString (&undoData));
  GameStateData newState;
  CHECK (state.SerializeToString (&newState));

  LOG (INFO) << "Processed " << blockData["moves"].size () << " moves forward, "
             << "new state has " << state.players_size () << " players";

  return newState;
}

GameStateData
MoverLogic::ProcessBackwardsInternal (const GameStateData& newState,
                                      const Json::Value& blockData,
                                      const UndoData& undoData)
{
  proto::GameState state;
  CHECK (state.ParseFromString (newState));
  proto::UndoData undo;
  CHECK (undo.ParseFromString (undoData));

  std::set<std::string> playersToRemove;
  for (auto& mi : *state.mutable_players ())
    {
      const std::string& name = mi.first;
      proto::PlayerState& p = mi.second;

      const proto::PlayerUndo* u = nullptr;
      const auto undoIt = undo.players ().find (name);
      if (undoIt != undo.players ().end ())
        u = &undoIt->second;

      /* If the player was new, just mark it for removal right away.  */
      if (u != nullptr && u->is_new ())
        {
          playersToRemove.insert (name);
          continue;
        }

      /* Restore "finished directions".  */
      if (u != nullptr && u->has_finished_dir ())
        {
          CHECK (p.dir () == proto::NONE && p.steps_left () == 0);
          p.set_dir (u->finished_dir ());
        }

      /* Undo move if the player is moving.  */
      if (p.dir () != proto::NONE)
        {
          p.set_steps_left (p.steps_left () + 1);

          int dx, dy;
          GetDirectionOffset (p.dir (), dx, dy);
          p.set_x (p.x () - dx);
          p.set_y (p.y () - dy);
        }

      /* Restore direction and steps_left from explicit change.  */
      if (u != nullptr)
        {
          if (u->has_previous_dir ())
            p.set_dir (u->previous_dir ());
          if (u->has_previous_steps_left ())
            p.set_steps_left (u->previous_steps_left ());
        }
    }

  /* Finalise removal of players.  This is done in a separate step to avoid
     issues with invalidated iterators.  */
  for (const auto& nm : playersToRemove)
    state.mutable_players ()->erase (nm);

  GameStateData oldState;
  CHECK (state.SerializeToString (&oldState));

  LOG (INFO) << "Processed " << blockData["moves"].size ()
             << " moves backwards, recovered old state has "
             << state.players_size () << " players";

  return oldState;
}

Json::Value
MoverLogic::GameStateToJson (const GameStateData& encodedState)
{
  proto::GameState state;
  CHECK (state.ParseFromString (encodedState));

  Json::Value players(Json::objectValue);
  for (const auto& playerEntry : state.players ())
    {
      const proto::PlayerState& p = playerEntry.second;

      Json::Value playerJson(Json::objectValue);
      playerJson["x"] = p.x ();
      playerJson["y"] = p.y ();
      if (p.dir () != proto::NONE)
        {
          playerJson["dir"] = DirectionToString (p.dir ());
          playerJson["steps"] = static_cast<int> (p.steps_left ());
        }

      players[playerEntry.first] = playerJson;
    }

  Json::Value res(Json::objectValue);
  res["players"] = players;

  return res;
}

} // namespace mover
