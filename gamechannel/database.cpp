// Copyright (C) 2019-2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "database.hpp"

#include "stateproof.hpp"

#include <glog/logging.h>

#include <set>

namespace spacexpanse
{

namespace
{

/* Indices of the columns for the channels table from SELECT's.  */
constexpr int COLUMN_ID = 0;
constexpr int COLUMN_METADATA = 1;
constexpr int COLUMN_REINIT = 2;
constexpr int COLUMN_STATEPROOF = 3;
constexpr int COLUMN_DISPUTEHEIGHT = 4;

/**
 * Binds a protocol buffer message to a BLOB parameter.
 */
template <typename Proto>
  void
  BindBlobProto (SQLiteDatabase::Statement& stmt, const int ind,
                 const Proto& msg)
{
  std::string serialised;
  CHECK (msg.SerializeToString (&serialised));
  stmt.BindBlob (ind, serialised);
}

/**
 * Sets a state proof to be just based on the reinitialisation state.
 */
void
StateProofFromReinit (const BoardState& reinit, proto::StateProof& proof)
{
  proof.Clear ();
  proof.mutable_initial_state ()->set_data (reinit);
}

} // anonymous namespace

ChannelData::ChannelData (SQLiteDatabase& d, const uint256& i)
  : db(d), id(i), initialised(false), disputeHeight(0), dirty(true)
{
  LOG (INFO) << "Created new ChannelData instance for ID " << id.ToHex ();
}

ChannelData::ChannelData (SQLiteDatabase& d,
                          const SQLiteDatabase::Statement& row)
  : db(d), initialised(true), dirty(false)
{
  id = row.Get<uint256> (COLUMN_ID);

  CHECK (metadata.ParseFromString (row.GetBlob (COLUMN_METADATA)));
  reinit = row.GetBlob (COLUMN_REINIT);

  /* See if there is an explicit state proof in the database.  If not, we just
     set it to one based on the reinit state.  */
  if (row.IsNull (COLUMN_STATEPROOF))
    StateProofFromReinit (reinit, proof);
  else
    CHECK (proof.ParseFromString (row.GetBlob (COLUMN_STATEPROOF)));

  if (row.IsNull (COLUMN_DISPUTEHEIGHT))
    disputeHeight = 0;
  else
    disputeHeight = row.Get<int64_t> (COLUMN_DISPUTEHEIGHT);

  LOG (INFO)
      << "Created ChannelData instance from result row, ID " << id.ToHex ();
}

ChannelData::~ChannelData ()
{
  CHECK (initialised);

  if (!dirty)
    {
      LOG (INFO) << "ChannelData " << id.ToHex () << " is not dirty";
      return;
    }

  LOG (INFO) << "ChannelData " << id.ToHex () << " is dirty, updating...";

  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `xgame_game_channels`
      (`id`, `metadata`, `reinit`, `stateproof`, `disputeHeight`)
      VALUES (?1, ?2, ?3, ?4, ?5)
  )");

  stmt.Bind (1, id);
  BindBlobProto (stmt, 2, metadata);
  stmt.Bind (3, reinit);

  if (GetLatestState () == reinit)
    stmt.BindNull (4);
  else
    BindBlobProto (stmt, 4, proof);

  if (disputeHeight == 0)
    stmt.BindNull (5);
  else
    stmt.Bind (5, disputeHeight);

  stmt.Execute ();
}

const proto::ChannelMetadata&
ChannelData::GetMetadata () const
{
  CHECK (initialised);
  return metadata;
}

const BoardState&
ChannelData::GetReinitState () const
{
  CHECK (initialised);
  return reinit;
}

void
ChannelData::Reinitialise (const proto::ChannelMetadata& m,
                           const BoardState& initialisedState)
{
  LOG (INFO)
      << "Reinitialising channel " << id.ToHex ()
      << " to new state: " << initialisedState;

  if (initialised)
    CHECK_NE (metadata.reinit (), m.reinit ())
        << "Metadata reinitialisation ID is not changed in reinit of channel";

  metadata = m;
  reinit = initialisedState;
  StateProofFromReinit (reinit, proof);

  initialised = true;
  dirty = true;
}

const proto::StateProof&
ChannelData::GetStateProof () const
{
  CHECK (initialised);
  return proof;
}

const BoardState&
ChannelData::GetLatestState () const
{
  CHECK (initialised);
  return UnverifiedProofEndState (proof);
}

void
ChannelData::SetStateProof (const proto::StateProof& p)
{
  CHECK (initialised);
  dirty = true;
  proof = p;
}

unsigned
ChannelData::GetDisputeHeight () const
{
  CHECK_GT (disputeHeight, 0);
  return disputeHeight;
}

void
ChannelData::SetDisputeHeight (const unsigned h)
{
  CHECK_GT (h, 0);
  dirty = true;
  disputeHeight = h;
}

ChannelsTable::Handle
ChannelsTable::GetFromResult (const SQLiteDatabase::Statement& row)
{
  return Handle (new ChannelData (db, row));
}

ChannelsTable::Handle
ChannelsTable::GetById (const uint256& id)
{
  auto stmt = db.PrepareRo (R"(
    SELECT `id`, `metadata`, `reinit`, `stateproof`, `disputeHeight`
      FROM `xgame_game_channels`
      WHERE `id` = ?1
  )");

  stmt.Bind (1, id);

  if (!stmt.Step ())
    return nullptr;

  auto h = GetFromResult (stmt);
  CHECK (!stmt.Step ());

  return h;
}

ChannelsTable::Handle
ChannelsTable::CreateNew (const uint256& id)
{
  return Handle (new ChannelData (db, id));
}

void
ChannelsTable::DeleteById (const uint256& id)
{
  auto stmt = db.Prepare (R"(
    DELETE FROM `xgame_game_channels`
      WHERE `id` = ?1
  )");
  stmt.Bind (1, id);
  stmt.Execute ();
}

SQLiteDatabase::Statement
ChannelsTable::QueryAll ()
{
  return db.PrepareRo (R"(
    SELECT `id`, `metadata`, `reinit`, `stateproof`, `disputeHeight`
      FROM `xgame_game_channels`
      ORDER BY `id`
  )");
}

SQLiteDatabase::Statement
ChannelsTable::QueryForDisputeHeight (const unsigned height)
{
  auto stmt = db.PrepareRo (R"(
    SELECT `id`, `metadata`, `reinit`, `stateproof`, `disputeHeight`
      FROM `xgame_game_channels`
      WHERE `disputeHeight` <= ?1
      ORDER BY `id`
  )");

  stmt.Bind (1, height);

  return stmt;
}

} // namespace spacexpanse
