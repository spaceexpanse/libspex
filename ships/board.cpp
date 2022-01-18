// Copyright (C) 2019-2022 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "board.hpp"

#include "coord.hpp"
#include "grid.hpp"

#include <gamechannel/protoversion.hpp>
#include <xayautil/hash.hpp>
#include <xayautil/random.hpp>
#include <xayautil/uint256.hpp>

#include <glog/logging.h>

#include <sstream>

namespace ships
{

namespace
{

/**
 * Checks whether a hash value encoded in a string of bytes (as stored in
 * the protocol buffers) matches the given uint256.  This gracefully handles
 * a situation where the stored bytes have a wrong length, in which case the
 * hash simply mismatches.
 */
bool
CheckHashValue (const xaya::uint256& actual, const std::string& expected)
{
  if (expected.size () != xaya::uint256::NUM_BYTES)
    {
      LOG (WARNING) << "Committed hash has wrong size: " << expected.size ();
      return false;
    }

  return actual.GetBinaryString () == expected;
}

} // anonymous namespace

bool
ShipsBoardState::IsValid () const
{
  /* As a special case, all single-participant states are valid without any
     more processing.  Those are only ever created while waiting for the second
     player to join, and during that time no changes to the state are made at
     all.  So just allow them, as needed e.g. for JSON game state.  */
  if (GetMetadata ().participants_size () == 1)
    return true;

  CHECK_EQ (GetMetadata ().participants_size (), 2);

  /* If the phase is not well-defined, then the state is invalid.  */
  const auto phase = GetPhase ();
  if (phase == Phase::INVALID)
    return false;

  /* Unless the game is finished, we should have a turn set.  */
  const auto& pb = GetState ();
  if (!pb.has_turn () || phase == Phase::FINISHED)
    return !pb.has_turn () && phase == Phase::FINISHED;

  /* Since we have two players, turn should be zero or one.  */
  const int turn = pb.turn ();
  if (turn < 0 || turn > 1)
    return false;

  /* Verify some phase-dependent rules.  Especially check that turn is set
     to the correct values for phases where the turn is redundant.  */
  switch (phase)
    {
    case Phase::FIRST_COMMITMENT:
    case Phase::FIRST_REVEAL_SEED:
      if (turn != 0)
        return false;
      break;

    case Phase::SECOND_COMMITMENT:
      if (turn != 1)
        return false;
      break;

    case Phase::SHOOT:
    case Phase::ANSWER:
      /* It can be any player's turn in this case.  This is when we really
         need the turn field and it is not redundant.  */
      break;

    case Phase::SECOND_REVEAL_POSITION:
      {
        CHECK_EQ (pb.positions_size (), 2);
        const int otherTurn = 1 - turn;
        if (pb.positions (turn) != 0 || pb.positions (otherTurn) == 0)
          return false;
        break;
      }

    default:
      LOG (FATAL) << "Unexpected phase: " << static_cast<int> (phase);
      return false;
    }

  return true;
}

ShipsBoardState::Phase
ShipsBoardState::GetPhase () const
{
  const auto& pb = GetState ();

  if (pb.has_winner ())
    return Phase::FINISHED;

  switch (pb.position_hashes_size ())
    {
    case 0:
      return Phase::FIRST_COMMITMENT;
    case 1:
      return Phase::SECOND_COMMITMENT;
    case 2:
      break;
    default:
      return Phase::INVALID;
    }

  switch (pb.known_ships_size ())
    {
    case 0:
      return Phase::FIRST_REVEAL_SEED;
    case 2:
      break;
    default:
      return Phase::INVALID;
    }

  switch (pb.positions_size ())
    {
    case 0:
      break;
    case 2:
      return Phase::SECOND_REVEAL_POSITION;
    default:
      return Phase::INVALID;
    }

  if (pb.has_current_shot ())
    return Phase::ANSWER;
  return Phase::SHOOT;
}

Json::Value
ShipsBoardState::ToJson () const
{
  auto res = BaseProtoBoardState::ToJson ();

  if (GetMetadata ().participants_size () == 1)
    {
      res["phase"] = "single participant";
      return res;
    }

  const auto phase = GetPhase ();
  switch (phase)
    {
    case Phase::FIRST_COMMITMENT:
      res["phase"] = "first commitment";
      break;

    case Phase::SECOND_COMMITMENT:
      res["phase"] = "second commitment";
      break;

    case Phase::FIRST_REVEAL_SEED:
      res["phase"] = "first reveal seed";
      break;

    case Phase::SHOOT:
      res["phase"] = "shoot";
      break;

    case Phase::ANSWER:
      res["phase"] = "answer";
      break;

    case Phase::SECOND_REVEAL_POSITION:
      res["phase"] = "second reveal position";
      break;

    case Phase::FINISHED:
      res["phase"] = "finished";
      break;

    default:
      LOG (FATAL) << "Invalid phase: " << static_cast<int> (phase);
      break;
    }

  const auto& pb = GetState ();
  if (pb.has_winner ())
    res["winner"] = pb.winner ();

  if (pb.positions_size () > 0)
    {
      CHECK_EQ (pb.positions_size (), 2);
      Json::Value positions(Json::arrayValue);

      for (const auto p : pb.positions ())
        if (p == 0)
          positions.append (Json::Value ());
        else
          positions.append (Grid(p).ToString ());

      res["ships"] = positions;
    }

  if (pb.known_ships_size () > 0)
    {
      CHECK_EQ (pb.known_ships_size (), 2);
      Json::Value knownShips(Json::arrayValue);
      for (const auto& ks : pb.known_ships ())
        {
          const Grid guessed(ks.guessed ());
          const Grid hits(ks.hits ());

          std::ostringstream str;
          for (int r = 0; r < Coord::SIDE; ++r)
            {
              for (int c = 0; c < Coord::SIDE; ++c)
                {
                  const Coord cell(r, c);
                  if (hits.Get (cell))
                    {
                      CHECK (guessed.Get (cell));
                      str << 'x';
                    }
                  else if (guessed.Get (cell))
                    str << 'm';
                  else
                    str << '.';
                }
              str << '\n';
            }

          knownShips.append (str.str ());
        }

      res["guesses"] = knownShips;
    }

  return res;
}

int
ShipsBoardState::WhoseTurn () const
{
  if (GetMetadata ().participants_size () == 1)
    return xaya::ParsedBoardState::NO_TURN;

  if (!GetState ().has_turn ())
    return xaya::ParsedBoardState::NO_TURN;

  const int res = GetState ().turn ();
  CHECK_GE (res, 0);
  CHECK_LE (res, 1);

  return res;
}

unsigned
ShipsBoardState::TurnCount () const
{
  if (GetMetadata ().participants_size () == 1)
    return 0;

  const auto& pb = GetState ();
  unsigned shots = 0;
  for (const auto& known : pb.known_ships ())
    {
      const Grid guesses(known.guessed ());
      shots += guesses.CountOnes ();
    }

  const auto phase = GetPhase ();
  switch (phase)
    {
    case Phase::FIRST_COMMITMENT:
      return 1;

    case Phase::SECOND_COMMITMENT:
      return 2;

    case Phase::FIRST_REVEAL_SEED:
      return 3;

    case Phase::SHOOT:
      return 4 + 2 * shots;

    case Phase::ANSWER:
      /* In the answer phase, the count of shots is already incremented, but
         we have not yet made the "second move" of the last "shot cycle".  */
      return 4 + 2 * shots - 1;

    case Phase::SECOND_REVEAL_POSITION:
    case Phase::FINISHED:
      /* Flow is not fully determined in these phases.  For example, when
         the first player reveals their position, the game may go to either
         SECOND_REVEAL_POSITION or FINISHED.  Thus those phases are
         handled together.  */
      {
        unsigned cnt = 4 + 2 * shots;
        if (pb.has_current_shot ())
          cnt -= 1;

        /* We can look at the number of known positions to determine how many
           reveal moves there were really made.  */
        for (const int p : pb.positions ())
          if (p != 0)
            cnt += 1;

        return cnt;
      }

    default:
      LOG (FATAL) << "Invalid phase: " << static_cast<int> (phase);
    }
}

bool
ShipsBoardState::ApplyPositionCommitment (
    const proto::PositionCommitmentMove& mv, const Phase phase,
    proto::BoardState& newState)
{
  if (mv.position_hash ().size () != xaya::uint256::NUM_BYTES)
    {
      LOG (WARNING) << "position_hash has wrong size";
      return false;
    }

  switch (phase)
    {
    case Phase::FIRST_COMMITMENT:
      if (mv.seed_hash ().size () != xaya::uint256::NUM_BYTES)
        {
          LOG (WARNING) << "seed_hash has wrong size";
          return false;
        }
      if (mv.has_seed ())
        {
          LOG (WARNING) << "First commitment has preimage seed";
          return false;
        }

      newState.set_turn (1);
      newState.add_position_hashes (mv.position_hash ());
      CHECK_EQ (newState.position_hashes_size (), 1);
      newState.set_seed_hash_0 (mv.seed_hash ());
      return true;

    case Phase::SECOND_COMMITMENT:
      if (mv.has_seed_hash ())
        {
          LOG (WARNING) << "Second commitment has seed hash";
          return false;
        }
      if (mv.seed ().size () > xaya::uint256::NUM_BYTES)
        {
          LOG (WARNING) << "seed is too large: " << mv.seed ().size ();
          return false;
        }

      newState.set_turn (0);
      newState.add_position_hashes (mv.position_hash ());
      CHECK_EQ (newState.position_hashes_size (), 2);
      newState.set_seed_1 (mv.seed ());
      return true;

    default:
      LOG (WARNING)
          << "Invalid phase for position commitment: "
          << static_cast<int> (phase);
      return false;
    }
}

bool
ShipsBoardState::ApplySeedReveal (const proto::SeedRevealMove& mv,
                                  const Phase phase,
                                  proto::BoardState& newState)
{
  if (phase != Phase::FIRST_REVEAL_SEED)
    {
      LOG (WARNING)
          << "Invalid phase for seed reveal: " << static_cast<int> (phase);
      return false;
    }

  if (mv.seed ().size () > xaya::uint256::NUM_BYTES)
    {
      LOG (WARNING) << "seed is too large: " << mv.seed ().size ();
      return false;
    }
  if (!CheckHashValue (xaya::SHA256::Hash (mv.seed ()),
                       newState.seed_hash_0 ()))
    {
      LOG (WARNING) << "seed does not match committed hash";
      return false;
    }

  /* The starting player is determined by computing a single random bit,
     seeded from the hash of both seed strings together.  */
  xaya::SHA256 hasher;
  hasher << mv.seed () << newState.seed_1 ();
  xaya::Random rnd;
  rnd.Seed (hasher.Finalise ());
  newState.set_turn (rnd.Next<bool> () ? 1 : 0);

  newState.clear_seed_hash_0 ();
  newState.clear_seed_1 ();

  for (int i = 0; i < 2; ++i)
    {
      auto* known = newState.add_known_ships ();
      known->set_guessed (0);
      known->set_hits (0);
    }

  return true;
}

bool
ShipsBoardState::ApplyShot (const proto::ShotMove& mv, const Phase phase,
                            proto::BoardState& newState)
{
  if (phase != Phase::SHOOT)
    {
      LOG (WARNING) << "Invalid phase for shot: " << static_cast<int> (phase);
      return false;
    }

  if (!mv.has_location ())
    {
      LOG (WARNING) << "Shot move has no location";
      return false;
    }
  const Coord target(mv.location ());
  if (!target.IsOnBoard ())
    {
      LOG (WARNING) << "Shot target is not on the board";
      return false;
    }

  const int otherPlayer = 1 - newState.turn ();
  CHECK_GE (otherPlayer, 0);
  CHECK_LE (otherPlayer, 1);

  Grid guessed(newState.known_ships (otherPlayer).guessed ());
  if (guessed.Get (target))
    {
      LOG (WARNING) << "Shot target has already been guessed";
      return false;
    }
  guessed.Set (target);

  newState.set_turn (otherPlayer);
  newState.set_current_shot (target.GetIndex ());
  newState.mutable_known_ships (otherPlayer)->set_guessed (guessed.GetBits ());

  return true;
}

bool
ShipsBoardState::ApplyReply (const proto::ReplyMove& mv, const Phase phase,
                             proto::BoardState& newState)
{
  if (phase != Phase::ANSWER)
    {
      LOG (WARNING) << "Invalid phase for reply: " << static_cast<int> (phase);
      return false;
    }

  if (!mv.has_reply ())
    {
      LOG (WARNING) << "Reply move has no actual reply";
      return false;
    }

  CHECK (newState.has_current_shot ());
  const Coord target(newState.current_shot ());
  if (!target.IsOnBoard ())
    {
      /* This check is not part of the state validation, so we have to make sure
         that an invalid state (e.g. committed to chain by signatures of both
         players) is handled gracefully.  */
      LOG (WARNING) << "Invalid current shot target";
      return false;
    }
  newState.clear_current_shot ();

  switch (mv.reply ())
    {
    case proto::ReplyMove::HIT:
      {
        /* If this is a hit, then we have to mark it in known_ships and also
           the turn changes (as the next player is who made the shot, not who is
           currently replying).  */

        Grid hits(newState.known_ships (newState.turn ()).hits ());
        if (hits.Get (target))
          {
            LOG (WARNING) << "Previous shot targeted already known position";
            return false;
          }
        hits.Set (target);

        newState
            .mutable_known_ships (newState.turn ())
            ->set_hits (hits.GetBits ());
        newState.set_turn (1 - newState.turn ());

        return true;
      }

    case proto::ReplyMove::MISS:
      /* If the shot was a miss, then it remains the current player's turn
         (as that's who replied) and no other update to the state is needed.  */
      return true;

    default:
      LOG (WARNING) << "Invalid reply in move: " << mv.reply ();
      return false;
    }
}

bool
ShipsBoardState::ApplyPositionReveal (const proto::PositionRevealMove& mv,
                                      const Phase phase,
                                      proto::BoardState& newState)
{
  switch (phase)
    {
    case Phase::SHOOT:
    case Phase::ANSWER:
      /* In these two phases, the player can reveal their position rather
         than shoot/reply.  */
      break;

    case Phase::SECOND_REVEAL_POSITION:
      /* In this phase, a position revelation is actually the only valid
         move that can be made.  */
      break;

    default:
      LOG (WARNING)
          << "Invalid phase for position reveal: " << static_cast<int> (phase);
      return false;
    }

  if (!mv.has_position ())
    {
      LOG (WARNING) << "Position reveal has no position data";
      return false;
    }
  if (mv.salt ().size () > xaya::uint256::NUM_BYTES)
    {
      LOG (WARNING)
          << "Position reveal has invalid salt size: " << mv.salt ().size ();
      return false;
    }

  const Grid g(mv.position ());

  /* If the position does not match the committed hash, then the move is
     outright invalid.  */
  xaya::SHA256 hasher;
  hasher << g.Blob () << mv.salt ();
  if (!CheckHashValue (hasher.Finalise (),
                       newState.position_hashes (newState.turn ())))
    {
      LOG (WARNING) << "Revealed position does not match committed hash";
      return false;
    }

  /* Record the revealed position and clear the committing hash.  */
  if (newState.positions_size () == 0)
    {
      newState.add_positions (0);
      newState.add_positions (0);
    }
  CHECK_EQ (newState.positions_size (), 2);
  CHECK_EQ (newState.positions (newState.turn ()), 0);
  newState.set_positions (newState.turn (), mv.position ());
  newState.mutable_position_hashes (newState.turn ())->clear ();

  /* If the position is invalid or does not match given answers, then the
     player whose turn it is lost.  */
  const int otherPlayer = 1 - newState.turn ();
  CHECK_GE (otherPlayer, 0);
  CHECK_LE (otherPlayer, 1);
  if (!VerifyPositionOfShips (g))
    {
      LOG (INFO) << "Player had invalid position of ships";
      newState.set_winner (otherPlayer);
    }
  else
    {
      /* If hits is not a subset of the guessed positions, then the state
         is invalid.  This could happen through committing it to the chain
         with signatures of both players.  Make sure to gracefully handle that
         situation.  */
      const auto& known = newState.known_ships (newState.turn ());
      if ((known.hits () & ~known.guessed ()) != 0)
        {
          LOG (WARNING) << "Hits are not a subset of guessed positions";
          return false;
        }

      const Grid targeted(known.guessed ());
      const Grid hits(known.hits ());
      if (!VerifyPositionForAnswers (g, targeted, hits))
        {
          LOG (INFO) << "Player position does not match answers";
          newState.set_winner (otherPlayer);
        }
    }

  /* If all was fine and this is the first player to reveal, then they win
     if all opponent ships have been hit.  */
  if (!newState.has_winner () && phase != Phase::SECOND_REVEAL_POSITION)
    {
      const Grid hits(newState.known_ships (otherPlayer).hits ());
      const int ones = hits.CountOnes ();
      VLOG (1) << "Ships hit by the revealing player: " << ones;
      if (ones >= Grid::TotalShipCells ())
        {
          VLOG (1) << "All opponent ships have been hit";
          newState.set_winner (newState.turn ());
        }
    }

  /* If the second player answers and all is still fine, then the first player
     did not sink all ships and thus loses.  */
  if (!newState.has_winner () && phase == Phase::SECOND_REVEAL_POSITION)
    {
      VLOG (1) << "Not all ships have been sunk";
      newState.set_winner (newState.turn ());
    }

  /* If we have a winner, the game is finished and no more turns are takn.
     Also make sure to clear all position hashes, if not yet done completely
     above.  */
  if (newState.has_winner ())
    {
      newState.clear_turn ();
      for (auto& ph : *newState.mutable_position_hashes ())
        ph.clear ();
      return true;
    }

  /* Finally, if we still do not have a winner, then it means that this was
     just the first position reveal.  The other player is next to reveal.  */
  CHECK (phase != Phase::SECOND_REVEAL_POSITION);
  newState.set_turn (otherPlayer);
  return true;
}

bool
ShipsBoardState::ApplyMoveProto (const proto::BoardMove& mv,
                                 proto::BoardState& newState) const
{
  /* Moves do typically incremental changes, so we start by copying the
     current state and then modify it (rather than constructing the new
     state from scratch).  */
  const auto& pb = GetState ();
  newState = pb;

  const int turn = WhoseTurn ();
  CHECK_NE (turn, xaya::ParsedBoardState::NO_TURN);

  const auto phase = GetPhase ();
  switch (mv.move_case ())
    {
    case proto::BoardMove::kPositionCommitment:
      return ApplyPositionCommitment (mv.position_commitment (), phase,
                                      newState);

    case proto::BoardMove::kSeedReveal:
      return ApplySeedReveal (mv.seed_reveal (), phase, newState);

    case proto::BoardMove::kShot:
      return ApplyShot (mv.shot (), phase, newState);

    case proto::BoardMove::kReply:
      return ApplyReply (mv.reply (), phase, newState);

    case proto::BoardMove::kPositionReveal:
      return ApplyPositionReveal (mv.position_reveal (), phase, newState);

    default:
    case proto::BoardMove::MOVE_NOT_SET:
      LOG (WARNING) << "Move does not specify any one-of case";
      return false;
    }

  LOG (FATAL) << "Unexpected move case: " << mv.move_case ();
  return false;
}

xaya::ChannelProtoVersion
ShipsBoardRules::GetProtoVersion (const xaya::proto::ChannelMetadata& m) const
{
  return xaya::ChannelProtoVersion::ORIGINAL;
}

proto::BoardState
InitialBoardState ()
{
  proto::BoardState res;
  res.set_turn (0);
  return res;
}

} // namespace ships
