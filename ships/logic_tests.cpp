// Copyright (C) 2019-2025 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logic.hpp"

#include "proto/boardstate.pb.h"
#include "testutils.hpp"

#include <gamechannel/database.hpp>
#include <gamechannel/proto/stateproof.pb.h>
#include <gamechannel/protoutils.hpp>
#include <gamechannel/signatures.hpp>
#include <xayautil/base64.hpp>
#include <xayautil/hash.hpp>

#include <google/protobuf/text_format.h>

#include <gtest/gtest.h>

#include <vector>

using google::protobuf::TextFormat;

namespace ships
{

class StateUpdateTests : public InMemoryLogicFixture
{

protected:

  xaya::ChannelsTable tbl;

  StateUpdateTests ()
    : tbl(GetDb ())
  {}

  /**
   * This calls UpdateState with the given sequence of moves and claiming
   * the given block height.
   */
  void
  UpdateState (const unsigned height, const std::vector<Json::Value>& moves)
  {
    Json::Value block(Json::objectValue);
    block["height"] = height;

    Json::Value moveJson(Json::arrayValue);
    for (const auto& mv : moves)
      moveJson.append (mv);

    Json::Value blockData(Json::objectValue);
    blockData["block"] = block;
    blockData["moves"] = moveJson;

    game.UpdateState (GetDb (), blockData);
  }

  /**
   * Expects that the number of open channels is the given one.  This can be
   * used to verify there are no unexpected channels.
   */
  void
  ExpectNumberOfChannels (const unsigned expected)
  {
    unsigned actual = 0;
    auto stmt = tbl.QueryAll ();
    while (stmt.Step ())
      ++actual;

    EXPECT_EQ (actual, expected);
  }

  /**
   * Expects that a channel with the given ID exists and returns the handle
   * to it.
   */
  xaya::ChannelsTable::Handle
  ExpectChannel (const xaya::uint256& id)
  {
    auto h = tbl.GetById (id);
    CHECK (h != nullptr);
    return h;
  }

  /**
   * Exposes UpdateStats for testing.
   */
  void
  UpdateStats (const xaya::proto::ChannelMetadata& meta, const int winner)
  {
    game.UpdateStats (GetDb (), meta, winner);
  }

  /**
   * Inserts a row into the game_stats table (to define pre-existing data
   * for testing updates to it).
   */
  void
  AddStatsRow (const std::string& name, const int won, const int lost)
  {
    auto stmt = GetDb ().Prepare (R"(
      INSERT INTO `game_stats`
        (`name`, `won`, `lost`) VALUES (?1, ?2, ?3)
    )");
    stmt.Bind (1, name);
    stmt.Bind (2, won);
    stmt.Bind (3, lost);
    stmt.Execute ();
  }

  /**
   * Verifies that the game stats for the given name match the given values.
   */
  void
  ExpectStatsRow (const std::string& name, const int won, const int lost)
  {
    auto stmt = GetDb ().Prepare (R"(
      SELECT `won`, `lost`
        FROM `game_stats`
        WHERE `name` = ?1
    )");
    stmt.Bind (1, name);

    CHECK (stmt.Step ()) << "No stats row for: " << name;
    EXPECT_EQ (stmt.Get<int> (0), won);
    EXPECT_EQ (stmt.Get<int> (1), lost);

    CHECK (!stmt.Step ());
  }

};

namespace
{

/**
 * Utility method to construct a move object from the ingredients (name,
 * txid and actual move data).
 */
Json::Value
Move (const std::string& name, const xaya::uint256& txid,
      const Json::Value& data)
{
  Json::Value res(Json::objectValue);
  res["name"] = name;
  res["txid"] = txid.ToHex ();
  res["move"] = data;

  return res;
}

/**
 * Returns a serialised state for the given text proto.
 */
xaya::BoardState
SerialisedState (const std::string& str)
{
  proto::BoardState state;
  CHECK (TextFormat::ParseFromString (str, &state));

  xaya::BoardState res;
  CHECK (state.SerializeToString (&res));

  return res;
}

/**
 * Returns a JSON dispute/resolution move object for the channel which has
 * a state proof for the state parsed from text and signed by the given
 * signatures.  Key should be either "r" or "d" to build resolutions or
 * disputes, respectively.
 */
Json::Value
BuildDisputeResolutionMove (const xaya::uint256& channelId,
                            const xaya::uint256& txid,
                            const std::string& key, const std::string& stateStr,
                            const std::vector<std::string>& signatures)
{
  xaya::proto::StateProof proof;
  auto* is = proof.mutable_initial_state ();
  *is->mutable_data () = SerialisedState (stateStr);
  for (const auto& sgn : signatures)
    is->add_signatures (sgn);

  Json::Value data(Json::objectValue);
  data[key] = Json::Value (Json::objectValue);
  data[key]["id"] = channelId.ToHex ();
  data[key]["state"] = xaya::ProtoToBase64 (proof);

  return Move ("xyz", txid, data);
}

TEST_F (StateUpdateTests, MoveNotAnObject)
{
  const auto txid = xaya::SHA256::Hash ("foo");

  std::vector<Json::Value> moves;
  for (const std::string mv : {"10", "\"foo\"", "null", "true", "[42]"})
    moves.push_back (Move ("foo", txid, ParseJson (mv)));

  UpdateState (10, moves);
  ExpectNumberOfChannels (0);
}

TEST_F (StateUpdateTests, MultipleActions)
{
  UpdateState (10, {Move ("foo", xaya::uint256 (), ParseJson (R"(
    {
      "c": {"addr": "my address"},
      "x": "something else"
    }
  )"))});
  ExpectNumberOfChannels (0);
}

TEST_F (StateUpdateTests, InvalidMoveContinuesProcessing)
{
  UpdateState (10, {
    Move ("foo", xaya::SHA256::Hash ("foo"), ParseJson ("\"foo\"")),
    Move ("bar", xaya::SHA256::Hash ("bar"), ParseJson (R"(
      {
        "c": {"addr": "my address"}
      }
    )")),
  });
  ExpectNumberOfChannels (1);
}

/* ************************************************************************** */

using CreateChannelTests = StateUpdateTests;

TEST_F (CreateChannelTests, InvalidCreates)
{
  const auto txid = xaya::SHA256::Hash ("foo");

  std::vector<Json::Value> moves;
  for (const std::string create : {"42", "null", "{}",
                                   R"({"addr": 100})",
                                   R"({"addr": "foo", "x": 5})"})
    {
      Json::Value data(Json::objectValue);
      data["c"] = ParseJson (create);
      moves.push_back (Move ("foo", txid, data));
    }

  UpdateState (10, moves);
  ExpectNumberOfChannels (0);
}

TEST_F (CreateChannelTests, CreationSuccessful)
{
  UpdateState (10, {
    Move ("foo", xaya::SHA256::Hash ("foo"), ParseJson ("\"invalid\"")),
    Move ("bar", xaya::SHA256::Hash ("bar"), ParseJson (R"(
      {"c": {"addr": "address 1"}}
    )")),
    Move ("bar", xaya::SHA256::Hash ("baz"), ParseJson (R"(
      {"c": {"addr": "address 2"}}
    )")),
    Move ("bar", xaya::SHA256::Hash ("bah"), ParseJson (R"(
      {"c": {"addr": "address 2"}}
    )")),
  });

  ExpectNumberOfChannels (3);

  auto h = ExpectChannel (xaya::SHA256::Hash ("bar"));
  ASSERT_EQ (h->GetMetadata ().participants_size (), 1);
  EXPECT_EQ (h->GetMetadata ().participants (0).name (), "bar");
  EXPECT_EQ (h->GetMetadata ().participants (0).address (), "address 1");
  EXPECT_EQ (h->GetLatestState (), "");
  EXPECT_FALSE (h->HasDispute ());

  h = ExpectChannel (xaya::SHA256::Hash ("baz"));
  ASSERT_EQ (h->GetMetadata ().participants_size (), 1);
  EXPECT_EQ (h->GetMetadata ().participants (0).name (), "bar");
  EXPECT_EQ (h->GetMetadata ().participants (0).address (), "address 2");

  h = ExpectChannel (xaya::SHA256::Hash ("bah"));
  ASSERT_EQ (h->GetMetadata ().participants_size (), 1);
  EXPECT_EQ (h->GetMetadata ().participants (0).name (), "bar");
  EXPECT_EQ (h->GetMetadata ().participants (0).address (), "address 2");
}

TEST_F (CreateChannelTests, MvidIfAvailable)
{
  const auto txid = xaya::SHA256::Hash ("txid");
  const auto id1 = xaya::SHA256::Hash ("mvid 1");
  const auto id2 = xaya::SHA256::Hash ("mvid 2");

  auto mv1 = Move ("domob", txid, ParseJson (R"(
    {"c": {"addr": "address 1"}}
  )"));
  mv1["mvid"] = id1.ToHex ();

  auto mv2 = Move ("domob", txid, ParseJson (R"(
    {"c": {"addr": "address 2"}}
  )"));
  mv2["mvid"] = id2.ToHex ();

  UpdateState (10, {mv1, mv2});

  ExpectNumberOfChannels (2);
  ExpectChannel (id1);
  ExpectChannel (id2);
}

TEST_F (CreateChannelTests, FailsForIdCollision)
{
  const auto data = ParseJson (R"(
    {"c": {"addr": "address"}}
  )");
  EXPECT_DEATH (UpdateState (10, {
    Move ("foo", xaya::SHA256::Hash ("foo"), data),
    Move ("bar", xaya::SHA256::Hash ("foo"), data),
  }), "Already have channel with ID");
}

/* ************************************************************************** */

using JoinChannelTests = StateUpdateTests;

TEST_F (JoinChannelTests, Malformed)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ();
  h->Reinitialise (meta, "");
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");

  std::vector<Json::Value> moves;
  for (const std::string create : {"42", "null", "{}",
                                   R"({"addr": 100, "id": "00"})",
                                   R"({"addr": "addr", "id": 100})",
                                   R"({"addr": "addr", "id": "00"})",
                                   R"({"addr": "foo", "id": "00", "x": 5})"})
    {
      Json::Value data(Json::objectValue);
      data["j"] = ParseJson (create);
      moves.push_back (Move ("foo", txid, data));
    }
  UpdateState (10, moves);

  ExpectNumberOfChannels (1);
  EXPECT_EQ (ExpectChannel (existing)->GetMetadata ().participants_size (), 1);
}

TEST_F (JoinChannelTests, NonExistantChannel)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ();
  h->Reinitialise (meta, "");
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");
  Json::Value data (Json::objectValue);
  data["j"] = ParseJson (R"({"addr": "address"})");
  data["j"]["id"] = txid.ToHex ();
  UpdateState (10, {Move ("foo", txid, data)});

  ExpectNumberOfChannels (1);
  EXPECT_EQ (ExpectChannel (existing)->GetMetadata ().participants_size (), 1);
}

TEST_F (JoinChannelTests, AlreadyTwoParticipants)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ()->set_name ("foo");
  meta.add_participants ()->set_name ("bar");
  h->Reinitialise (meta, SerialisedState ("turn: 0"));
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");
  Json::Value data (Json::objectValue);
  data["j"] = ParseJson (R"({"addr": "address"})");
  data["j"]["id"] = existing.ToHex ();
  UpdateState (10, {Move ("baz", txid, data)});

  ExpectNumberOfChannels (1);
  h = ExpectChannel (existing);
  ASSERT_EQ (h->GetMetadata ().participants_size (), 2);
  EXPECT_EQ (h->GetMetadata ().participants (0).name (), "foo");
  EXPECT_EQ (h->GetMetadata ().participants (1).name (), "bar");
}

TEST_F (JoinChannelTests, SameNameInChannel)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ()->set_name ("foo");
  h->Reinitialise (meta, "");
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");
  Json::Value data (Json::objectValue);
  data["j"] = ParseJson (R"({"addr": "address"})");
  data["j"]["id"] = existing.ToHex ();
  UpdateState (10, {Move ("foo", txid, data)});

  ExpectNumberOfChannels (1);
  h = ExpectChannel (existing);
  ASSERT_EQ (h->GetMetadata ().participants_size (), 1);
  EXPECT_EQ (h->GetMetadata ().participants (0).name (), "foo");
}

TEST_F (JoinChannelTests, SuccessfulJoin)
{
  const auto id1 = xaya::SHA256::Hash ("foo");
  const auto id2 = xaya::SHA256::Hash ("bar");

  std::vector<Json::Value> moves;
  moves.push_back (Move ("foo", id1, ParseJson (R"({"c": {"addr": "a"}})")));

  Json::Value data(Json::objectValue);
  data["j"] = ParseJson (R"({"addr": "b"})");
  data["j"]["id"] = id1.ToHex ();
  moves.push_back (Move ("bar", id2, data));

  UpdateState (10, moves);
  ExpectNumberOfChannels (1);
  auto h = ExpectChannel (id1);
  ASSERT_EQ (h->GetMetadata ().participants_size (), 2);
  EXPECT_EQ (h->GetMetadata ().participants (0).name (), "foo");
  EXPECT_EQ (h->GetMetadata ().participants (0).address (), "a");
  EXPECT_EQ (h->GetMetadata ().participants (1).name (), "bar");
  EXPECT_EQ (h->GetMetadata ().participants (1).address (), "b");
  EXPECT_FALSE (h->HasDispute ());

  proto::BoardState state;
  CHECK (state.ParseFromString (h->GetLatestState ()));
  EXPECT_TRUE (state.has_turn ());
  EXPECT_EQ (state.turn (), 0);
}

/* ************************************************************************** */

using AbortChannelTests = StateUpdateTests;

TEST_F (AbortChannelTests, Malformed)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ();
  h->Reinitialise (meta, "");
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");

  std::vector<Json::Value> moves;
  for (const std::string create : {"42", "null", "{}",
                                   R"({"id": "00"})",
                                   R"({"id": 100})",
                                   R"({"id": "00", "x": 5})"})
    {
      Json::Value data(Json::objectValue);
      data["a"] = ParseJson (create);
      moves.push_back (Move ("foo", txid, data));
    }
  UpdateState (10, moves);

  ExpectNumberOfChannels (1);
  ExpectChannel (existing);
}

TEST_F (AbortChannelTests, NonExistantChannel)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ();
  h->Reinitialise (meta, "");
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");
  Json::Value data (Json::objectValue);
  data["a"] = Json::Value (Json::objectValue);
  data["a"]["id"] = txid.ToHex ();
  UpdateState (10, {Move ("foo", txid, data)});

  ExpectNumberOfChannels (1);
  ExpectChannel (existing);
}

TEST_F (AbortChannelTests, AlreadyTwoParticipants)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ()->set_name ("foo");
  meta.add_participants ()->set_name ("bar");
  h->Reinitialise (meta, SerialisedState ("turn: 0"));
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");
  Json::Value data (Json::objectValue);
  data["a"] = Json::Value (Json::objectValue);
  data["a"]["id"] = existing.ToHex ();
  UpdateState (10, {Move ("baz", txid, data)});

  ExpectNumberOfChannels (1);
  ExpectChannel (existing);
}

TEST_F (AbortChannelTests, DifferentName)
{
  const auto existing = xaya::SHA256::Hash ("foo");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ()->set_name ("foo");
  h->Reinitialise (meta, "");
  h.reset ();

  const auto txid = xaya::SHA256::Hash ("bar");
  Json::Value data (Json::objectValue);
  data["a"] = Json::Value (Json::objectValue);
  data["a"]["id"] = existing.ToHex ();
  UpdateState (10, {Move ("bar", txid, data)});

  ExpectNumberOfChannels (1);
  ExpectChannel (existing);
}

TEST_F (AbortChannelTests, SuccessfulAbort)
{
  const auto existing = xaya::SHA256::Hash ("existing channel");
  auto h = tbl.CreateNew (existing);
  xaya::proto::ChannelMetadata meta;
  meta.add_participants ();
  h->Reinitialise (meta, "");
  h.reset ();

  const auto id1 = xaya::SHA256::Hash ("foo");
  const auto id2 = xaya::SHA256::Hash ("bar");

  std::vector<Json::Value> moves;
  moves.push_back (Move ("foo", id1, ParseJson (R"({"c": {"addr": "a"}})")));

  Json::Value data(Json::objectValue);
  data["a"] = Json::Value (Json::objectValue);
  data["a"]["id"] = id1.ToHex ();
  moves.push_back (Move ("foo", id2, data));

  UpdateState (10, moves);
  ExpectNumberOfChannels (1);
  ExpectChannel (existing);
}

/* ************************************************************************** */

class DeclareLossTests : public StateUpdateTests
{

protected:

  xaya::proto::ChannelMetadata meta;

  /**
   * ID of the channel closed in tests (or not).  This channel is set up
   * with players "name 0" and "name 1".
   */
  const xaya::uint256 channelId = xaya::SHA256::Hash ("test channel");

  /** ID of a channel that should not be affected by any close.  */
  const xaya::uint256 otherId = xaya::SHA256::Hash ("other channel");

  /** Txid for use with the move.  */
  const xaya::uint256 txid = xaya::SHA256::Hash ("txid");

  DeclareLossTests ()
  {
    CHECK (TextFormat::ParseFromString (R"(
      participants:
        {
          name: "name 0"
          address: "addr 0"
        }
      participants:
        {
          name: "name 1"
          address: "addr 1"
        }
      reinit: "foo"
    )", &meta));

    auto h = tbl.CreateNew (channelId);
    h->Reinitialise (meta, SerialisedState ("turn: 0"));
    h.reset ();

    h = tbl.CreateNew (otherId);
    h->Reinitialise (meta, SerialisedState ("turn: 0"));
    h.reset ();
  }

  /**
   * Constructs a JSON move for declaring loss in the given channel
   * and based on the reinit value of our metadata instance.
   */
  Json::Value
  LossMove (const std::string& name, const xaya::uint256& channelId) const
  {
    Json::Value data(Json::objectValue);
    data["l"] = Json::Value (Json::objectValue);
    data["l"]["id"] = channelId.ToHex ();
    data["l"]["r"] = xaya::EncodeBase64 (meta.reinit ());

    return Move (name, txid, data);
  }

};

TEST_F (DeclareLossTests, UpdateStats)
{
  AddStatsRow ("foo", 10, 5);
  AddStatsRow ("bar", 1, 2);
  ExpectStatsRow ("foo", 10, 5);
  ExpectStatsRow ("bar", 1, 2);

  xaya::proto::ChannelMetadata meta;
  meta.add_participants ()->set_name ("foo");
  meta.add_participants ()->set_name ("baz");

  UpdateStats (meta, 0);
  ExpectStatsRow ("foo", 11, 5);
  ExpectStatsRow ("bar", 1, 2);
  ExpectStatsRow ("baz", 0, 1);

  UpdateStats (meta, 1);
  ExpectStatsRow ("foo", 11, 6);
  ExpectStatsRow ("bar", 1, 2);
  ExpectStatsRow ("baz", 1, 1);
}

TEST_F (DeclareLossTests, Malformed)
{
  std::vector<Json::Value> moves;
  for (const std::string create : {"42", "null", "{}"})
    {
      Json::Value data(Json::objectValue);
      data["l"] = ParseJson (create);
      moves.push_back (Move ("name 0", txid, data));
    }

  const auto valid = LossMove ("name 0", channelId);

  auto mv = valid;
  mv["move"]["l"]["id"] = 42;
  moves.push_back (mv);
  mv["move"]["l"].removeMember ("id");
  moves.push_back (mv);

  mv = valid;
  mv["move"]["l"]["r"] = "invalid";
  moves.push_back (mv);
  mv["move"]["l"]["r"] = 42;
  moves.push_back (mv);
  mv["move"]["l"].removeMember ("r");
  moves.push_back (mv);

  mv = valid;
  mv["move"]["l"]["x"] = 5;
  moves.push_back (mv);

  UpdateState (10, moves);

  ExpectNumberOfChannels (2);
  ExpectChannel (channelId);
  ExpectChannel (otherId);
}

TEST_F (DeclareLossTests, NonExistantChannel)
{
  UpdateState (10, {LossMove ("foo", xaya::SHA256::Hash ("does not exist"))});

  ExpectNumberOfChannels (2);
  ExpectChannel (channelId);
  ExpectChannel (otherId);
}

TEST_F (DeclareLossTests, WrongNumberOfParticipants)
{
  auto h = ExpectChannel (channelId);
  xaya::proto::ChannelMetadata meta = h->GetMetadata ();
  meta.mutable_participants ()->RemoveLast ();
  meta.set_reinit ("init 2");
  h->Reinitialise (meta, "");
  h.reset ();

  UpdateState (10, {LossMove ("name 0", channelId)});

  ExpectNumberOfChannels (2);
  ExpectChannel (channelId);
  ExpectChannel (otherId);
}

TEST_F (DeclareLossTests, NotAParticipant)
{
  UpdateState (10, {LossMove ("foo", channelId)});

  ExpectNumberOfChannels (2);
  ExpectChannel (channelId);
  ExpectChannel (otherId);
}

TEST_F (DeclareLossTests, InvalidReinit)
{
  auto mv = LossMove ("name 0", channelId);
  mv["move"]["l"]["r"] = xaya::EncodeBase64 ("wrong reinit");

  UpdateState (10, {mv});

  ExpectNumberOfChannels (2);
  ExpectChannel (channelId);
  ExpectChannel (otherId);
}

TEST_F (DeclareLossTests, Valid)
{
  UpdateState (10, {LossMove ("name 0", channelId)});
  ExpectNumberOfChannels (1);
  ExpectChannel (otherId);
  ExpectStatsRow ("name 0", 0, 1);
  ExpectStatsRow ("name 1", 1, 0);

  UpdateState (10, {LossMove ("name 1", otherId)});
  ExpectNumberOfChannels (0);
  ExpectStatsRow ("name 0", 1, 1);
  ExpectStatsRow ("name 1", 1, 1);
}

/* ************************************************************************** */

class DisputeResolutionTests : public StateUpdateTests
{

protected:

  /**
   * ID of the channel closed in tests (or not).  This channel is set up
   * with players "name 0" and "name 1".
   */
  const xaya::uint256 channelId = xaya::SHA256::Hash ("test channel");

  /** Txid for use with the move.  */
  const xaya::uint256 txid = xaya::SHA256::Hash ("txid");

  DisputeResolutionTests ()
  {
    auto h = tbl.CreateNew (channelId);

    xaya::proto::ChannelMetadata meta;
    CHECK (TextFormat::ParseFromString (R"(
      participants:
        {
          name: "name 0"
          address: "addr 0"
        }
      participants:
        {
          name: "name 1"
          address: "addr 1"
        }
    )", &meta));

    h->Reinitialise (meta, SerialisedState ("turn: 0"));
    h.reset ();

    verifier.SetValid ("sgn 0", "addr 0");
    verifier.SetValid ("sgn 1", "addr 1");

    /* Explicitly add stats rows so we can use ExpectStatsRow even if there
       were no changes.  */
    AddStatsRow ("name 0", 0, 0);
    AddStatsRow ("name 1", 0, 0);
  }

  Json::Value
  BuildMove (const std::string& key, const std::string& stateStr,
             const std::vector<std::string>& signatures)
  {
    return BuildDisputeResolutionMove (channelId, txid,
                                       key, stateStr, signatures);
  }

};

TEST_F (DisputeResolutionTests, ExpiringDisputes)
{
  ExpectChannel (channelId)->SetDisputeHeight (100);

  UpdateState (109, {});
  ExpectNumberOfChannels (1);
  ExpectChannel (channelId);
  ExpectStatsRow ("name 0", 0, 0);
  ExpectStatsRow ("name 1", 0, 0);

  UpdateState (110, {});
  ExpectNumberOfChannels (0);
  ExpectStatsRow ("name 0", 0, 1);
  ExpectStatsRow ("name 1", 1, 0);
}

TEST_F (DisputeResolutionTests, Malformed)
{
  std::vector<Json::Value> moves;
  for (const std::string str : {"42", "null", "{}",
                                R"({"id": "00"})",
                                R"({"id": 100, "state": ""})",
                                R"({"id": "00", "state": ""})",
                                R"({"id": "00", "state": "", "x": 5})"})
    {
      Json::Value data(Json::objectValue);
      data["r"] = ParseJson (str);
      moves.push_back (Move ("xyz", txid, data));

      data.clear ();
      data["d"] = ParseJson (str);
      moves.push_back (Move ("xyz", txid, data));
    }
  UpdateState (10, moves);

  ExpectNumberOfChannels (1);
  EXPECT_FALSE (ExpectChannel (channelId)->HasDispute ());
}

TEST_F (DisputeResolutionTests, InvalidStateData)
{
  Json::Value data(Json::objectValue);
  data["d"] = Json::Value (Json::objectValue);
  data["d"]["id"] = channelId.ToHex ();
  data["d"]["state"] = "invalid base64";
  UpdateState (10, {Move ("xyz", txid, data)});

  data["d"]["state"] = xaya::EncodeBase64 ("invalid proto");
  UpdateState (11, {Move ("xyz", txid, data)});

  ExpectNumberOfChannels (1);
  EXPECT_FALSE (ExpectChannel (channelId)->HasDispute ());
}

TEST_F (DisputeResolutionTests, NonExistantChannel)
{
  auto mv = BuildMove ("d", "turn: 0", {"sgn 0", "sgn 1"});
  mv["move"]["d"]["id"] = xaya::SHA256::Hash ("invalid channel").ToHex ();
  UpdateState (10, {mv});

  ExpectNumberOfChannels (1);
  EXPECT_FALSE (ExpectChannel (channelId)->HasDispute ());
}

TEST_F (DisputeResolutionTests, WrongNumberOfParticipants)
{
  auto h = ExpectChannel (channelId);
  xaya::proto::ChannelMetadata meta = h->GetMetadata ();
  meta.mutable_participants ()->RemoveLast ();
  meta.set_reinit ("init 2");
  h->Reinitialise (meta, h->GetLatestState ());
  h.reset ();

  UpdateState (10, {BuildMove ("d", "turn: 0", {"sgn 0", "sgn 1"})});

  ExpectNumberOfChannels (1);
  EXPECT_FALSE (ExpectChannel (channelId)->HasDispute ());
}

TEST_F (DisputeResolutionTests, InvalidStateProof)
{
  UpdateState (10, {BuildMove ("d", R"(
    turn: 1
    position_hashes: "foo"
    seed_hash_0: "bar"
  )", {})});

  ExpectNumberOfChannels (1);
  EXPECT_FALSE (ExpectChannel (channelId)->HasDispute ());
}

TEST_F (DisputeResolutionTests, ValidDispute)
{
  UpdateState (10, {BuildMove ("d", "turn: 0", {})});

  ExpectNumberOfChannels (1);
  auto h = ExpectChannel (channelId);
  ASSERT_TRUE (h->HasDispute ());
  EXPECT_EQ (h->GetDisputeHeight (), 10);
}

TEST_F (DisputeResolutionTests, ValidResolution)
{
  ExpectChannel (channelId)->SetDisputeHeight (100);

  UpdateState (110, {BuildMove ("r", R"(
    turn: 1
    position_hashes: "foo"
    seed_hash_0: "bar"
  )", {"sgn 0", "sgn 1"})});

  ExpectNumberOfChannels (1);
  ASSERT_FALSE (ExpectChannel (channelId)->HasDispute ());
}

TEST_F (DisputeResolutionTests, ResolutionClosesChannel)
{
  UpdateState (100, {BuildMove ("r", R"(
    winner: 1
  )", {"sgn 0", "sgn 1"})});

  ExpectNumberOfChannels (0);
  ExpectStatsRow ("name 0", 0, 1);
  ExpectStatsRow ("name 1", 1, 0);
}

/* ************************************************************************** */

using ChannelTimeoutTests = StateUpdateTests;

TEST_F (ChannelTimeoutTests, Works)
{
  const auto id1 = xaya::SHA256::Hash ("foo");
  const auto id2 = xaya::SHA256::Hash ("bar");

  std::vector<Json::Value> moves;
  moves.push_back (Move ("foo", id1, ParseJson (R"({"c": {"addr": "a"}})")));
  moves.push_back (Move ("foo", id2, ParseJson (R"({"c": {"addr": "a"}})")));
  UpdateState (10, moves);

  /* Until the height is reached, nothing should happen.  */
  for (unsigned i = 1; i < CHANNEL_TIMEOUT_BLOCKS; ++i)
    UpdateState (10 + i, {});

  ExpectNumberOfChannels (2);
  EXPECT_EQ (ExpectChannel (id1)->GetMetadata ().participants_size (), 1);
  EXPECT_EQ (ExpectChannel (id2)->GetMetadata ().participants_size (), 1);

  /* At the timeout height, join one of the channels, and let the
     other actually time out.  */
  moves.clear ();
  Json::Value data(Json::objectValue);
  data["j"] = ParseJson (R"({"addr": "b"})");
  data["j"]["id"] = id2.ToHex ();
  moves.push_back (Move ("bar", id2, data));
  UpdateState (10 + CHANNEL_TIMEOUT_BLOCKS, moves);

  ExpectNumberOfChannels (1);
  EXPECT_EQ (ExpectChannel (id2)->GetMetadata ().participants_size (), 2);
}

/* ************************************************************************** */

} // anonymous namespace

class PendingTests : public InMemoryLogicFixture
{

private:

  ShipsPending proc;

protected:

  xaya::proto::ChannelMetadata meta;

  xaya::ChannelsTable tbl;

  PendingTests ()
    : proc(game), tbl(GetDb ())
  {
    proc.InitialiseGameContext (xaya::Chain::MAIN, "xs", nullptr);

    CHECK (TextFormat::ParseFromString (R"(
      participants:
        {
          name: "name 0"
          address: "addr 0"
        }
      participants:
        {
          name: "name 1"
          address: "addr 1"
        }
    )", &meta));

    verifier.SetValid ("sgn 0", "addr 0");
    verifier.SetValid ("sgn 1", "addr 1");
  }

  /**
   * Submits a pending move to the processor.
   */
  void
  AddPendingMove (const Json::Value& mv)
  {
    proc.AddPendingMoveUnsafe (GetDb (), mv);
  }

  /**
   * Returns the given field in the current pending JSON.
   */
  Json::Value
  GetPendingField (const std::string& name)
  {
    return proc.ToJson ()[name];
  }

  /**
   * Expects that the pending state has exactly the given channels (by ID)
   * with updates of their state proofs.
   *
   * We do not care about the data for each channel, as this test is mostly
   * about move parsing and forwarding of data.
   */
  void
  ExpectPendingChannels (const std::set<xaya::uint256>& expected)
  {
    const auto actualJson = GetPendingField ("channels");
    ASSERT_TRUE (actualJson.isObject ());

    std::set<xaya::uint256> actual;
    for (auto it = actualJson.begin (); it != actualJson.end (); ++it)
      {
        xaya::uint256 txid;
        ASSERT_TRUE (txid.FromHex (it.key ().asString ()));
        actual.insert (txid);
      }
    ASSERT_EQ (actual.size (), actualJson.size ());

    EXPECT_EQ (actual, expected);
  }

};

namespace
{

TEST_F (PendingTests, NonObjectMove)
{
  const auto cid = xaya::SHA256::Hash ("channel");
  auto h = tbl.CreateNew (cid);
  h->Reinitialise (meta, SerialisedState ("turn: 0"));
  h.reset ();

  AddPendingMove (Move ("foo", xaya::SHA256::Hash ("foo"), 42));
  ExpectPendingChannels ({});
  EXPECT_EQ (GetPendingField ("create"), ParseJson ("[]"));
}

TEST_F (PendingTests, MultipleCommands)
{
  meta.mutable_participants ()->RemoveLast ();
  ASSERT_EQ (meta.participants_size (), 1);

  const auto cid = xaya::SHA256::Hash ("channel");
  tbl.CreateNew (cid)->Reinitialise (meta, "");

  auto joinMove = ParseJson (R"(
    {
      "c": {"addr": "address"},
      "j": {"addr": "address"}
    }
  )");
  joinMove["j"]["id"] = cid.ToHex ();

  const auto txid = xaya::SHA256::Hash ("txid");
  AddPendingMove (Move ("domob", txid, joinMove));

  EXPECT_EQ (GetPendingField ("create"), ParseJson ("[]"));
  EXPECT_EQ (GetPendingField ("join"), ParseJson ("[]"));
  EXPECT_EQ (GetPendingField ("abort"), ParseJson ("[]"));
}

TEST_F (PendingTests, CreateChannel)
{
  const auto txid1 = xaya::SHA256::Hash ("txid 1");
  const auto txid2 = xaya::SHA256::Hash ("txid 2");
  const auto txid3 = xaya::SHA256::Hash ("txid 3");

  AddPendingMove (Move ("domob", txid1, ParseJson (R"(
    {"c": {"addr": "addr 1"}}
  )")));
  AddPendingMove (Move ("andy", txid2, ParseJson (R"(
    {"c": {"invalid": true}}
  )")));
  AddPendingMove (Move ("domob", txid3, ParseJson (R"(
    {"c": {"addr": "addr 2"}}
  )")));

  auto expected = ParseJson (R"(
    [
      {"name": "domob", "address": "addr 1"},
      {"name": "domob", "address": "addr 2"}
    ]
  )");
  expected[0]["id"] = txid1.ToHex ();
  expected[1]["id"] = txid3.ToHex ();

  EXPECT_EQ (GetPendingField ("create"), expected);
}

TEST_F (PendingTests, JoinChannel)
{
  meta.mutable_participants ()->RemoveLast ();
  ASSERT_EQ (meta.participants_size (), 1);

  const auto cid = xaya::SHA256::Hash ("channel");
  tbl.CreateNew (cid)->Reinitialise (meta, "");

  auto joinMove = ParseJson (R"(
    {"j": {"addr": "address"}}
  )");
  joinMove["j"]["id"] = cid.ToHex ();

  const auto txid = xaya::SHA256::Hash ("txid");
  AddPendingMove (Move ("domob", txid, joinMove));
  AddPendingMove (Move ("name 0", txid, joinMove));
  AddPendingMove (Move ("andy", txid, joinMove));

  auto expected = ParseJson (R"(
    [
      {"name": "domob", "address": "address"},
      {"name": "andy", "address": "address"}
    ]
  )");
  expected[0]["id"] = cid.ToHex ();
  expected[1]["id"] = cid.ToHex ();

  EXPECT_EQ (GetPendingField ("join"), expected);
}

TEST_F (PendingTests, AbortChannel)
{
  meta.mutable_participants ()->RemoveLast ();
  ASSERT_EQ (meta.participants_size (), 1);

  const auto cid1 = xaya::SHA256::Hash ("channel 1");
  const auto cid2 = xaya::SHA256::Hash ("channel 2");
  tbl.CreateNew (cid1)->Reinitialise (meta, "");
  tbl.CreateNew (cid2)->Reinitialise (meta, "");

  const auto txid = xaya::SHA256::Hash ("txid");
  auto abortMove = ParseJson (R"({"a": {}})");
  abortMove["a"]["id"] = cid1.ToHex ();
  AddPendingMove (Move ("name 0", txid, abortMove));

  abortMove["a"]["id"] = cid2.ToHex ();
  AddPendingMove (Move ("domob", txid, abortMove));

  auto expected = ParseJson ("[]");
  expected.append (cid1.ToHex ());

  EXPECT_EQ (GetPendingField ("abort"), expected);
}

TEST_F (PendingTests, ValidStateProof)
{
  const auto cid1 = xaya::SHA256::Hash ("channel 1");
  auto h = tbl.CreateNew (cid1);
  h->Reinitialise (meta, SerialisedState ("turn: 0"));
  h.reset ();

  const auto cid2 = xaya::SHA256::Hash ("channel 2");
  h = tbl.CreateNew (cid2);
  h->Reinitialise (meta, SerialisedState ("turn: 0"));
  h.reset ();

  const auto mv1 = BuildDisputeResolutionMove (
      cid1, xaya::SHA256::Hash ("tx 1"), "d",
      R"(
        turn: 1
        position_hashes: "foo 1"
        seed_hash_0: "bar"
      )", {"sgn 0", "sgn 1"});
  AddPendingMove (mv1);

  const auto mv2 = BuildDisputeResolutionMove (
      cid2, xaya::SHA256::Hash ("tx 2"), "r",
      R"(
        turn: 0
        position_hashes: "foo 1"
        position_hashes: "foo 2"
        seed_hash_0: "bar"
        seed_1: "baz"
      )", {"sgn 0", "sgn 1"});
  AddPendingMove (mv2);

  ExpectPendingChannels ({cid1, cid2});
}

TEST_F (PendingTests, StateForNonExistantChannel)
{
  const auto cid = xaya::SHA256::Hash ("channel");
  auto h = tbl.CreateNew (cid);
  h->Reinitialise (meta, SerialisedState ("turn: 0"));
  h.reset ();

  const auto wrongCid = xaya::SHA256::Hash ("other channel");
  const auto mv = BuildDisputeResolutionMove (
      wrongCid, xaya::SHA256::Hash ("tx"), "r",
      R"(
        turn: 0
      )", {"sgn 0", "sgn 1"});
  AddPendingMove (mv);

  ExpectPendingChannels ({});
}

TEST_F (PendingTests, InvalidStateProof)
{
  const auto cid = xaya::SHA256::Hash ("channel");
  auto h = tbl.CreateNew (cid);
  h->Reinitialise (meta, SerialisedState ("turn: 0"));
  h.reset ();

  auto mv = ParseJson (R"(
    {
      "d":
        {
          "state": "invalid base64 proto"
        }
    }
  )");
  mv["move"]["d"]["id"] = cid.ToHex ();
  AddPendingMove (Move ("xyz", xaya::SHA256::Hash ("foo"), mv));

  ExpectPendingChannels ({});
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace ships
