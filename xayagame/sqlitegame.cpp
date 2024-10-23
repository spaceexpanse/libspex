// Copyright (C) 2018-2024 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sqlitegame.hpp"

#include <glog/logging.h>

#include <cstring>

namespace xaya
{

/* ************************************************************************** */

/**
 * Definition for ActiveAutoIds of SQLiteGame.  This class holds a set of
 * currently-active AutoId instances together with their string keys.  It
 * also manages the construction and destruction through RAII.
 *
 * The object also manages the activeIds reference in an owning
 * SQLiteGame instance:  It is set to "this" when constructed and
 * reset to null when destructed.
 */
class SQLiteGame::ActiveAutoIds
{

private:

  /** Reference to owning SQLiteGame instance.  */
  SQLiteGame& game;

  /** The map of AutoId values.  */
  std::map<std::string, std::unique_ptr<AutoId>> instances;

public:

  explicit ActiveAutoIds (SQLiteGame& g);
  ~ActiveAutoIds ();

  ActiveAutoIds () = delete;
  ActiveAutoIds (const ActiveAutoIds&) = delete;
  void operator= (const ActiveAutoIds&) = delete;

  AutoId& Get (const std::string& key);

};

SQLiteGame::ActiveAutoIds::ActiveAutoIds (SQLiteGame& g)
  : game(g)
{
  CHECK (g.activeIds == nullptr);
  g.activeIds = this;
}

SQLiteGame::ActiveAutoIds::~ActiveAutoIds ()
{
  CHECK (game.activeIds == this);
  game.activeIds = nullptr;

  for (auto& entry : instances)
    entry.second->Sync (game, entry.first);
}

SQLiteGame::AutoId&
SQLiteGame::ActiveAutoIds::Get (const std::string& key)
{
  const auto mit = instances.find (key);
  if (mit != instances.end ())
    return *mit->second;

  std::unique_ptr<AutoId> newId(new AutoId (game, key));
  const auto res = instances.emplace (key, std::move (newId));
  CHECK (res.second);
  return *res.first->second;
}

/* ************************************************************************** */

/**
 * This class holds an updatable snapshot of the full instance state (including
 * a read-only database snapshot), which can be used to answer state reads
 * without requiring the Game main lock.  It has its own lock to synchronise
 * just updates to the snapshot with readers requesting access to it.  But
 * it will not be blocked during computation of the state update in the GSP,
 * nor during the entire operation extracting some state from it.
 */
class SQLiteGame::StateSnapshot
{

private:

  /** Mutex for locking this instance.  */
  mutable std::mutex mut;

  /** Whether or not there is a current snapshot.  */
  bool hasSnapshot = false;

  /** The instance state JSON.  */
  Json::Value instanceState;

  /**
   * The database snapshot for reading the game state.  May be null even if
   * there is a current snapshot, in which case it means that there is no game
   * state, just an instance state.
   */
  std::shared_ptr<const SQLiteDatabase> database;

  /** If there is a game state, the corresponding height.  */
  uint64_t height;

  /** If there is a game state, the corresponding hash.  */
  uint256 hash;

public:

  explicit StateSnapshot () = default;

  /**
   * Returns a "copy" (that can then be used without locks) of the current
   * snapshot data.  If no snapshot is stored, returns false.
   */
  bool
  Get (Json::Value& inst, std::shared_ptr<const SQLiteDatabase>& db,
       uint64_t& he, uint256& ha) const
  {
    std::lock_guard<std::mutex> lock(mut);

    if (!hasSnapshot)
      return false;

    inst = instanceState;
    db = database;

    if (database != nullptr)
      {
        he = height;
        ha = hash;
      }

    return true;
  }

  /**
   * Clears the snapshot, such that afterwards there will be none inside.
   */
  void
  Clear ()
  {
    std::lock_guard<std::mutex> lock(mut);
    hasSnapshot = false;
    database.reset ();
  }

  /**
   * Sets the state to be based on the given database snapshot (which is taken
   * over from the unique_ptr).
   */
  void
  Set (const Json::Value& inst, std::unique_ptr<SQLiteDatabase> db,
       const uint64_t he, const uint256& ha)
  {
    std::lock_guard<std::mutex> lock(mut);
    hasSnapshot = true;
    instanceState = inst;
    height = he;
    hash = ha;
    database.reset (db.release ());
  }

};

/* ************************************************************************** */

namespace
{

/** Keyword string for the initial game state.  */
constexpr const char* INITIAL_STATE = "initial";

/** Prefix for the block hash "game state" keywords.  */
constexpr const char* BLOCKHASH_STATE = "block ";

} // anonymous namespace

/**
 * Definition of SQLiteGame::Storage.  It is a basic subclass of SQLiteStorage
 * that has a reference to the SQLiteGame, so that it can call SetupSchema
 * there when the database is opened.
 */
class SQLiteGame::Storage : public SQLiteStorage
{

private:

  /** Reference to the SQLiteGame that is using this instance.  */
  SQLiteGame& game;

  /**
   * Checks whether the game-state is marked as "initialised" in the
   * internal bookkeeping table.
   */
  static bool IsGameInitialised (const SQLiteDatabase& db);

  /**
   * Initialises the game state in the database by calling InitialiseState
   * on the underlying game.
   */
  void InitialiseGame ();

  /**
   * Returns the schema version from the database.
   */
  std::string GetSchemaVersion () const;

  /**
   * Sets the schema version in the database.
   */
  void SetSchemaVersion (const std::string& version);

  /**
   * Verifies that the database state corresponds to the given "current state"
   * from libxayagame.  Returns false if not.
   */
  bool CheckCurrentState (const SQLiteDatabase& db,
                          const GameStateData& state) const;

  friend class SQLiteGame;

protected:

  void
  CloseDatabase () override
  {
    for (auto* p : game.processors)
      p->Finish (GetDatabase ());
    SQLiteStorage::CloseDatabase ();
  }

  void
  WaitForSnapshots () override
  {
    game.stateSnapshot->Clear ();
    SQLiteStorage::WaitForSnapshots ();
  }

  void
  SetupSchema () override
  {
    SQLiteStorage::SetupSchema ();

    auto& db = GetDatabase ();
    db.Execute (R"(
      CREATE TABLE IF NOT EXISTS `xayagame_gamevars`
          (`onlyonerow` INTEGER PRIMARY KEY,
           `gamestate_initialised` INTEGER NOT NULL);
      INSERT OR IGNORE INTO `xayagame_gamevars`
          (`onlyonerow`, `gamestate_initialised`) VALUES (1, 0);

      CREATE TABLE IF NOT EXISTS `xayagame_autoids` (
          `key` TEXT PRIMARY KEY,
          `nextid` INTEGER NOT NULL
      );
    )");

    /* If the `schema_version` volumn is missing from `xayagame_gamevars`,
       add it in with the initial version value of "".  We do this here in
       a separate step rather than directly in the SQL above, so that it
       also works with databases created in previous versions of libxayagame
       (and just adds it to them as well now).  */
    auto stmt = db.PrepareRo (R"(
      SELECT `name`
        FROM pragma_table_info ('xayagame_gamevars')
        WHERE `name` = 'schema_version'
    )");
    if (stmt.Step ())
      {
        CHECK (!stmt.Step ());
      }
    else
      db.Execute (R"(
        ALTER TABLE `xayagame_gamevars`
          ADD COLUMN `schema_version` TEXT NOT NULL DEFAULT ''
      )");

    /* Since we use the session extension to handle rollbacks, only the main
       database should be used.  To enforce this (at least partially), disallow
       any attached databases.  */
    db.AccessDatabase ([] (sqlite3* h)
      {
        sqlite3_limit (h, SQLITE_LIMIT_ATTACHED, 0);
        LOG (INFO) << "Set allowed number of attached databases to zero";
      });

    if (game.messForDebug)
      {
        db.Execute (R"(
          PRAGMA `reverse_unordered_selects` = 1;
        )");
        LOG (INFO) << "Enabled mess-for-debug in the database";
      }

    /* Set up the processor schemas here explicitly.  This protects against
       a SQLiteGame subclass not calling the parent class' SetupSchema.  */
    for (auto* p : game.processors)
      p->SetupSchema (db);

    ActiveAutoIds ids(game);
    game.SetupSchema (db);
  }

public:

  explicit Storage (SQLiteGame& g, const std::string& f)
    : SQLiteStorage (f), game(g)
  {}

};

bool
SQLiteGame::Storage::IsGameInitialised (const SQLiteDatabase& db)
{
  auto stmt = db.PrepareRo (R"(
    SELECT `gamestate_initialised`
      FROM `xayagame_gamevars`
  )");

  CHECK (stmt.Step ())
      << "Failed to fetch result from xayagame_gamevars";
  const bool res = stmt.Get<bool> (0);
  CHECK (!stmt.Step ());

  return res;
}

void
SQLiteGame::Storage::InitialiseGame ()
{
  auto& db = GetDatabase ();

  if (IsGameInitialised (db))
    {
      VLOG (1) << "Game state is already initialised in the database";
      return;
    }

  LOG (INFO) << "Setting initial state in the DB";
  db.Prepare ("SAVEPOINT `xayagame-stateinit`").Execute ();
  try
    {
      ActiveAutoIds ids(game);
      game.InitialiseState (db);
      db.Prepare (R"(
        UPDATE `xayagame_gamevars`
          SET `gamestate_initialised` = 1
      )").Execute ();
      db.Prepare ("RELEASE `xayagame-stateinit`").Execute ();
      LOG (INFO) << "Initialised the DB state successfully";
    }
  catch (...)
    {
      LOG (ERROR) << "Initialising state failed, rolling back the DB change";
      db.Prepare ("ROLLBACK TO `xayagame-stateinit`").Execute ();
      throw;
    }
}

std::string
SQLiteGame::Storage::GetSchemaVersion () const
{
  auto stmt = GetDatabase ().PrepareRo (R"(
    SELECT `schema_version`
      FROM `xayagame_gamevars`
  )");

  CHECK (stmt.Step ())
      << "Failed to fetch result from xayagame_gamevars";
  const auto res = stmt.Get<std::string> (0);
  CHECK (!stmt.Step ());

  return res;
}

void
SQLiteGame::Storage::SetSchemaVersion (const std::string& version)
{
  LOG (INFO) << "Setting schema version to " << version;
  auto stmt = GetDatabase ().Prepare (R"(
    UPDATE `xayagame_gamevars`
      SET `schema_version` = ?1
  )");
  stmt.Bind (1, version);
  stmt.Execute ();
}

bool
SQLiteGame::Storage::CheckCurrentState (const SQLiteDatabase& db,
                                        const GameStateData& state) const
{
  VLOG (1) << "Checking if current database matches game state: " << state;

  /* In any case, state-based methods of GameLogic are only ever called when
     there is already a "current state" in the storage.  */
  uint256 hash;
  if (!GetCurrentBlockHash (db, hash))
    {
      VLOG (1) << "No current block hash in the database";
      return false;
    }
  const std::string hashHex = hash.ToHex ();

  /* Handle the case of a regular block hash (no initial state).  */
  const size_t prefixLen = std::strlen (BLOCKHASH_STATE);
  if (state.substr (0, prefixLen) == BLOCKHASH_STATE)
    {
      if (hashHex != state.substr (prefixLen))
        {
          VLOG (1)
              << "Current best block in the database (" << hashHex
              << ") does not match claimed current game state";
          return false;
        }
      CHECK (IsGameInitialised (db));
      return true;
    }

  /* Verify initial state.  */
  CHECK (state == INITIAL_STATE) << "Unexpected game state value: " << state;
  unsigned height;
  std::string initialHashHex;
  game.GetInitialStateBlock (height, initialHashHex);
  if (!initialHashHex.empty () && hashHex != initialHashHex)
    {
      VLOG (1)
          << "Current best block in the database (" << hashHex
          << ") does not match the game's initial block " << initialHashHex;
      return false;
    }
  CHECK (IsGameInitialised (db));
  return true;
}

/* ************************************************************************** */

SQLiteGame::SQLiteGame ()
  : stateSnapshot(std::make_unique<StateSnapshot> ())
{}

SQLiteGame::~SQLiteGame () = default;

void
SQLiteGame::EnsureCurrentState (const GameStateData& state)
{
  CHECK (database != nullptr) << "SQLiteGame has not been initialised";
  CHECK (database->CheckCurrentState (database->GetDatabase (), state))
      << "Game state is inconsistent to database";
}

void
SQLiteGame::Initialise (const std::string& dbFile)
{
  database = std::make_unique<Storage> (*this, dbFile);
}

void
SQLiteGame::SetupSchema (SQLiteDatabase& db)
{
  /* Nothing needs to be set up here, but subclasses probably do some setup
     in an overridden method.  The set up of the schema we need for SQLiteGame
     is done in Storage::SetupSchema already before calling here.  */
}

SQLiteStorage&
SQLiteGame::GetStorage ()
{
  CHECK (database != nullptr) << "SQLiteGame has not been initialised";
  return *database;
}

GameStateData
SQLiteGame::GetInitialStateInternal (unsigned& height, std::string& hashHex)
{
  GetInitialStateBlock (height, hashHex);

  CHECK (database != nullptr) << "SQLiteGame has not been initialised";
  database->InitialiseGame ();

  return INITIAL_STATE;
}

void
SQLiteGame::AddProcessor (SQLiteProcessor& proc)
{
  CHECK (database == nullptr) << "SQLiteGame has already been initialised";
  processors.insert (&proc);
}

void
SQLiteGame::SetMessForDebug (const bool val)
{
  CHECK (database == nullptr) << "SQLiteGame has already been initialised";
  messForDebug = val;
}

namespace
{

/**
 * Helper class wrapping aroung sqlite3_session and using RAII to ensure
 * proper management of its lifecycle.
 */
class SQLiteSession
{

private:

  /** The underlying sqlite3_session handle.  */
  sqlite3_session* session = nullptr;

public:

  /**
   * Construct a new session, monitoring the "main" database on the given
   * DB connection.
   */
  explicit SQLiteSession (sqlite3* db)
  {
    VLOG (1) << "Starting SQLite session to record undo data";

    CHECK_EQ (sqlite3session_create (db, "main", &session), SQLITE_OK)
        << "Failed to start SQLite session";
    CHECK (session != nullptr);
    CHECK_EQ (sqlite3session_attach (session, nullptr), SQLITE_OK)
        << "Failed to attach all tables to the SQLite session";
  }

  ~SQLiteSession ()
  {
    if (session != nullptr)
      sqlite3session_delete (session);
  }

  /**
   * Extracts the current changeset of the session as UndoData string.
   */
  UndoData
  ExtractChangeset ()
  {
    VLOG (1) << "Extracting recorded undo data from SQLite session";
    CHECK (session != nullptr);

    int changeSize;
    void* changeBytes;
    CHECK_EQ (sqlite3session_changeset (session, &changeSize, &changeBytes),
              SQLITE_OK)
        << "Failed to extract current session changeset";

    UndoData result(static_cast<const char*> (changeBytes), changeSize);
    sqlite3_free (changeBytes);

    return result;
  }

};

} // anonymous namespace

GameStateData
SQLiteGame::ProcessForwardInternal (const GameStateData& oldState,
                                    const Json::Value& blockData,
                                    UndoData& undo)
{
  EnsureCurrentState (oldState);

  auto& db = database->GetDatabase ();
  std::unique_ptr<SQLiteSession> session;
  db.AccessDatabase ([&session] (sqlite3* h)
    {
      session = std::make_unique<SQLiteSession> (h);
    });
  {
    ActiveAutoIds ids(*this);
    UpdateState (db, blockData);
  }
  undo = session->ExtractChangeset ();

  return BLOCKHASH_STATE + blockData["block"]["hash"].asString ();
}

namespace
{

/**
 * Conflict resolution function for sqlite3changeset_apply that simply
 * tells to abort the transaction.  (If all goes correct, then conflicts should
 * never happen as we simply rollback *the last* change and are not "merging"
 * changes in any way.)
 */
int
AbortOnConflict (void* ctx, const int conflict, sqlite3_changeset_iter* it)
{
  LOG (ERROR) << "Changeset application has a conflict of type " << conflict;
  return SQLITE_CHANGESET_ABORT;
}

/**
 * Utility class to manage an inverted changeset (based on undo data
 * representing an original one).  The main use of the class is to manage
 * the associated memory using RAII.
 */
class InvertedChangeset
{

private:

  /** Size of the inverted changeset.  */
  int size;
  /** Buffer holding the data for the inverted changeset.  */
  void* data = nullptr;

public:

  /**
   * Constructs the changeset by inverting the UndoData that represents
   * the original "forward" changeset.
   */
  explicit InvertedChangeset (const UndoData& undo)
  {
    CHECK_EQ (sqlite3changeset_invert (undo.size (), &undo[0], &size, &data),
              SQLITE_OK)
        << "Failed to invert SQLite changeset";
  }

  ~InvertedChangeset ()
  {
    sqlite3_free (data);
  }

  /**
   * Applies the inverted changeset to the database handle.  If conflicts
   * appear, the transaction is aborted and the function CHECK-fails.
   */
  void
  Apply (sqlite3* db)
  {
    CHECK_EQ (sqlite3changeset_apply (db, size, data, nullptr,
                                      &AbortOnConflict, nullptr),
              SQLITE_OK)
        << "Failed to apply undo changeset";
  }

};

} // anonymous namespace

GameStateData
SQLiteGame::ProcessBackwardsInternal (const GameStateData& newState,
                                      const Json::Value& blockData,
                                      const UndoData& undo)
{
  EnsureCurrentState (newState);

  /* Note that the undo data holds the *forward* changeset, not the inverted
     one.  Thus we have to invert it here before applying.  It might seem
     more intuitive for the undo data to already hold the inverted changeset,
     but as it is expected that most undo data values are never actually
     used to roll any changes back, it is more efficient to do the inversion
     only when actually needed.  */

  InvertedChangeset changeset(undo);
  database->GetDatabase ().AccessDatabase ([&changeset] (sqlite3* h)
    {
      changeset.Apply (h);
    });

  return BLOCKHASH_STATE + blockData["block"]["parent"].asString ();
}

SQLiteGame::AutoId&
SQLiteGame::Ids (const std::string& key)
{
  CHECK (activeIds != nullptr)
      << "Ids() can only be used while the game logic is active";
  return activeIds->Get (key);
}

std::string
SQLiteGame::GetSchemaVersion () const
{
  CHECK (database != nullptr) << "SQLiteGame has not been initialised";
  return database->GetSchemaVersion ();
}

void
SQLiteGame::SetSchemaVersion (const std::string& version)
{
  CHECK (database != nullptr) << "SQLiteGame has not been initialised";
  database->SetSchemaVersion (version);
}

void
SQLiteGame::GameStateUpdated (const GameStateData& state,
                              const Json::Value& blockData)
{
  EnsureCurrentState (state);

  /* See if we can get a snapshot.  */
  std::shared_ptr<SQLiteDatabase> snapshot;
  if (upToDate)
    {
      auto uniqueSnapshot = database->GetSnapshot ();
      if (uniqueSnapshot != nullptr
            && database->CheckCurrentState (*uniqueSnapshot, state))
        snapshot.reset (uniqueSnapshot.release ());
    }
  else
    VLOG (1)
        << "Not attempting to create a snapshot for processors"
        << " as the game is not up-to-date";

  for (auto* p : processors)
    p->Process (blockData, database->GetDatabase (), snapshot);
}

void
SQLiteGame::InstanceStateChanged (const Json::Value& state)
{
  CHECK (database != nullptr) << "SQLiteGame has not been initialised";

  CHECK (state.isObject ()) << "Invalid instance state: " << state;
  const auto& stateStr = state["state"];
  CHECK (stateStr.isString ());
  upToDate = (stateStr.asString () == "up-to-date");

  if (!upToDate)
    {
      VLOG (1) << "Not taking state snapshot as the game is not up-to-date";
      stateSnapshot->Clear ();
      return;
    }

  std::unique_ptr<SQLiteDatabase> snapshot;
  uint64_t height = std::numeric_limits<uint64_t>::max ();
  uint256 hash;
  if (database->GetCurrentBlockHash (hash))
    {
      CHECK_EQ (hash.ToHex (), state["blockhash"].asString ());
      CHECK (state.isMember ("height"));
      height = state["height"].asUInt64 ();

      const auto state = database->GetCurrentGameState ();
      EnsureCurrentState (state);
      snapshot = database->GetSnapshot ();
      if (snapshot == nullptr
            || !database->CheckCurrentState (*snapshot, state))
        {
          /* We weren't able to get a state snapshot, even though there should
             be a game state.  In this case, record that we do not have
             a state snapshot to use.  */
          stateSnapshot->Clear ();
          return;
        }
    }

  stateSnapshot->Set (state, std::move (snapshot), height, hash);
}

Json::Value
SQLiteGame::GameStateToJson (const GameStateData& state)
{
  EnsureCurrentState (state);
  return GetStateAsJson (database->GetDatabase ());
}

Json::Value
SQLiteGame::GetCustomStateData (
    const Game& game, const std::string& jsonField,
    const ExtractJsonFromDbWithBlock& cb)
{
  /* If we have a state snapshot, use it to retrieve the data without
     locking the Game's lock at all.  */
  Json::Value res;
  std::shared_ptr<const SQLiteDatabase> snapshotDb;
  uint64_t snapshotHeight = std::numeric_limits<uint64_t>::max ();
  uint256 snapshotHash;
  if (stateSnapshot->Get (res, snapshotDb, snapshotHeight, snapshotHash))
    {
      VLOG (1) << "Using state snapshot for GetCustomStateData";

      if (snapshotDb != nullptr)
        {
          CHECK_LT (snapshotHeight, std::numeric_limits<uint64_t>::max ());
          res[jsonField] = cb (*snapshotDb, snapshotHash, snapshotHeight);
        }

      return res;
    }

  /* Otherwise use Game::GetCustomStateData to retrieve the state
     directly from the Game instance and main database.  */
  return game.GetCustomStateData (jsonField,
      [this, &cb] (const GameStateData& state, const uint256& hash,
                   const unsigned height)
        {
          CHECK (database != nullptr) << "SQLiteGame has not been initialised";
          LOG (WARNING) << "Using main database for GetCustomStateData";
          EnsureCurrentState (state);
          return cb (database->GetDatabase (), hash, height);
        });
}

Json::Value
SQLiteGame::GetCustomStateData (
    const Game& game, const std::string& jsonField,
    const ExtractJsonFromDb& cb)
{
  return GetCustomStateData (game, jsonField,
    [&cb] (const SQLiteDatabase& db, const uint256& hash, const unsigned height)
    {
      return cb (db);
    });
}

SQLiteDatabase&
SQLiteGame::GetDatabaseForTesting ()
{
  CHECK (database != nullptr) << "SQLiteGame has not been initialised";
  return database->GetDatabase ();
}

/* ************************************************************************** */

SQLiteGame::AutoId::AutoId (SQLiteGame& game, const std::string& key)
{
  auto stmt = game.database->GetDatabase ().Prepare (R"(
    SELECT `nextid` FROM `xayagame_autoids` WHERE `key` = ?1
  )");
  stmt.Bind (1, key);

  if (stmt.Step ())
    {
      nextValue = stmt.Get<int64_t> (0);
      dbValue = nextValue;
      LOG (INFO) << "Fetched next value " << nextValue << " for AutoId " << key;
      CHECK (!stmt.Step ());
    }
  else
    {
      LOG (INFO) << "No next value for AutoId " << key;
      nextValue = 1;
    }

  CHECK_NE (nextValue, EMPTY_ID);
}

SQLiteGame::AutoId::~AutoId ()
{
  CHECK_EQ (dbValue, nextValue) << "AutoId has not been synced";
}

void
SQLiteGame::AutoId::Sync (SQLiteGame& game, const std::string& key)
{
  if (nextValue == dbValue)
    {
      LOG (INFO) << "No need to sync AutoId " << key;
      return;
    }

  auto stmt = game.database->GetDatabase ().Prepare (R"(
    INSERT OR REPLACE INTO `xayagame_autoids`
      (`key`, `nextid`) VALUES (?1, ?2)
  )");
  stmt.Bind (1, key);
  stmt.Bind (2, nextValue);
  stmt.Execute ();

  LOG (INFO) << "Synced AutoId " << key << " to database";
  dbValue = nextValue;
}

/* ************************************************************************** */

const SQLiteDatabase&
SQLiteGame::PendingMoves::AccessConfirmedState () const
{
  game.EnsureCurrentState (GetConfirmedState ());
  return game.database->GetDatabase ();
}

/* ************************************************************************** */

} // namespace xaya
