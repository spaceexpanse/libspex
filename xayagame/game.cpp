// Copyright (C) 2018-2021 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "game.hpp"

#include <jsonrpccpp/common/errors.h>
#include <jsonrpccpp/common/exception.h>

#include <glog/logging.h>

#include <chrono>
#include <sstream>
#include <thread>

namespace xaya
{

namespace
{

/** Clock used for timing the callbacks.  */
using PerformanceTimer = std::chrono::high_resolution_clock;

/** Duration type used for reporting callback timings.  */
using CallbackDuration = std::chrono::microseconds;
/** Unit (as string) for the callback timings.  */
constexpr const char* CALLBACK_DURATION_UNIT = "us";

/**
 * Timeout for WaitForChange (i.e. return after this time even if there
 * has not been any change).  Having a timeout in the first place avoids
 * collecting more and more blocked threads in the worst case.
 */
constexpr auto WAITFORCHANGE_TIMEOUT = std::chrono::seconds (5);

} // anonymous namespace

Game::Game (const std::string& id)
  : gameId(id)
{
  genesisHash.SetNull ();
  zmq.AddListener (gameId, this);
}

std::string
Game::StateToString (const State s)
{
  switch (s)
    {
    case State::UNKNOWN:
      return "unknown";
    case State::PREGENESIS:
      return "pregenesis";
    case State::OUT_OF_SYNC:
      return "out-of-sync";
    case State::CATCHING_UP:
      return "catching-up";
    case State::UP_TO_DATE:
      return "up-to-date";
    }

  std::ostringstream out;
  out << "invalid-" << static_cast<int> (s);
  return out.str ();
}

bool
Game::UpdateStateForAttach (const uint256& parent, const uint256& hash,
                            const Json::Value& blockData)
{
  uint256 currentHash;
  CHECK (storage->GetCurrentBlockHash (currentHash));
  if (currentHash != parent)
    {
      LOG (WARNING)
          << "Game state hash " << currentHash.ToHex ()
          << " does not match attached block's parent " << parent.ToHex ();
      return false;
    }

  const GameStateData oldState = storage->GetCurrentGameState ();
  const unsigned height = blockData["block"]["height"].asUInt ();

  {
    internal::ActiveTransaction tx(transactionManager);

    UndoData undo;
    const auto start = PerformanceTimer::now ();
    const GameStateData newState
        = rules->ProcessForward (oldState, blockData, undo);
    const auto end = PerformanceTimer::now ();
    LOG (INFO)
        << "Processing block " << height << " forward took "
        << std::chrono::duration_cast<CallbackDuration> (end - start).count ()
        << " " << CALLBACK_DURATION_UNIT;

    storage->AddUndoData (hash, height, undo);
    storage->SetCurrentGameStateWithHeight (hash, height, newState);

    tx.Commit ();
  }

  LOG (INFO)
      << "Current game state is at height " << height
      << " (block " << hash.ToHex () << ")";
  NotifyStateChange ();

  return true;
}

bool
Game::UpdateStateForDetach (const uint256& parent, const uint256& hash,
                            const Json::Value& blockData)
{
  uint256 currentHash;
  CHECK (storage->GetCurrentBlockHash (currentHash));
  if (currentHash != hash)
    {
      LOG (WARNING)
          << "Game state hash " << currentHash.ToHex ()
          << " does not match detached block's hash " << hash.ToHex ();
      return false;
    }

  UndoData undo;
  if (!storage->GetUndoData (hash, undo))
    {
      LOG (ERROR)
          << "Failed to retrieve undo data for block " << hash.ToHex ()
          << ".  Need to resync from scratch.";
      transactionManager.TryAbortTransaction ();
      storage->Clear ();
      return false;
    }

  const GameStateData newState = storage->GetCurrentGameState ();

  {
    internal::ActiveTransaction tx(transactionManager);

    const auto start = PerformanceTimer::now ();
    const GameStateData oldState
        = rules->ProcessBackwards (newState, blockData, undo);
    const auto end = PerformanceTimer::now ();

    const unsigned height = blockData["block"]["height"].asUInt ();
    CHECK_GT (height, 0);

    LOG (INFO)
        << "Undoing block " << height << " took "
        << std::chrono::duration_cast<CallbackDuration> (end - start).count ()
        << " " << CALLBACK_DURATION_UNIT;

    storage->SetCurrentGameStateWithHeight (parent, height - 1, oldState);
    storage->ReleaseUndoData (hash);

    tx.Commit ();
  }

  LOG (INFO)
      << "Detached " << hash.ToHex () << ", restored state for block "
      << parent.ToHex ();
  NotifyStateChange ();

  return true;
}

bool
Game::IsReqtokenRelevant (const Json::Value& data) const
{
  std::string msgReqToken;
  if (data.isMember ("reqtoken"))
    msgReqToken = data["reqtoken"].asString ();

  if (state == State::CATCHING_UP)
    return msgReqToken == reqToken;

  return msgReqToken.empty ();
}

void
Game::BlockAttach (const std::string& id, const Json::Value& data,
                   const bool seqMismatch)
{
  CHECK_EQ (id, gameId);
  VLOG (2) << "Attached:\n" << data;

  uint256 parent;
  CHECK (parent.FromHex (data["block"]["parent"].asString ()));
  uint256 hash;
  CHECK (hash.FromHex (data["block"]["hash"].asString ()));
  VLOG (1) << "Attaching block " << hash.ToHex ();

  std::lock_guard<std::mutex> lock(mut);

  /* If we missed notifications, always reinitialise the state to make sure
     that all is again consistent.  */
  if (seqMismatch)
    {
      LOG (WARNING) << "Missed ZMQ notifications, reinitialising state";
      ReinitialiseState ();
      if (pruningQueue != nullptr)
        pruningQueue->Reset ();
      return;
    }

  /* Ignore notifications that are not relevant at the moment.  */
  if (!IsReqtokenRelevant (data))
    {
      VLOG (1) << "Ignoring irrelevant attach notification";
      return;
    }

  const unsigned height = data["block"]["height"].asUInt ();

  bool needReinit = false;
  try
    {
      /* Handle the notification depending on the current state.  */
      switch (state)
        {
        case State::PREGENESIS:
          /* Check if we have reached the game's genesis height.  If we have,
             reinitialise which will store the initial game state.  */
          if (hash == targetBlockHash)
            needReinit = true;
          break;

        case State::CATCHING_UP:
          if (!UpdateStateForAttach (parent, hash, data))
            needReinit = true;

          /* If we are now at the last catching-up's target block hash,
             reinitialise the state as well.  This will check the current best
             tip and set the state to UP_TO_DATE or request more updates.  */
          if (hash == targetBlockHash)
            needReinit = true;

          break;

        case State::UP_TO_DATE:
          if (!UpdateStateForAttach (parent, hash, data))
            needReinit = true;
          break;

        case State::UNKNOWN:
        case State::OUT_OF_SYNC:
        default:
          LOG (FATAL) << "Unexpected state: " << StateToString (state);
          break;
        }

      /* Attach the block in the pruning queue.  This is done after updating the
         state so that a potential pruning with nBlocks=0 can take place.  */
      if (pruningQueue != nullptr)
        pruningQueue->AttachBlock (hash, height);
    }
  catch (const StorageInterface::RetryWithNewTransaction& exc)
    {
      LOG (WARNING) << "Storage update failed, retrying: " << exc.what ();
      needReinit = true;
    }

  if (needReinit)
    ReinitialiseState ();

  if (state == State::UP_TO_DATE && pending != nullptr)
    {
      pending->ProcessAttachedBlock (storage->GetCurrentGameState (), data);
      NotifyPendingStateChange ();
    }
}

void
Game::BlockDetach (const std::string& id, const Json::Value& data,
                   const bool seqMismatch)
{
  CHECK_EQ (id, gameId);
  VLOG (2) << "Detached:\n" << data;

  uint256 parent;
  CHECK (parent.FromHex (data["block"]["parent"].asString ()));
  uint256 hash;
  CHECK (hash.FromHex (data["block"]["hash"].asString ()));
  VLOG (1) << "Detaching block " << hash.ToHex ();

  std::lock_guard<std::mutex> lock(mut);

  /* If we missed notifications, always reinitialise the state to make sure
     that all is again consistent.  */
  if (seqMismatch)
    {
      LOG (WARNING) << "Missed ZMQ notifications, reinitialising state";
      ReinitialiseState ();
      if (pruningQueue != nullptr)
        pruningQueue->Reset ();
      return;
    }

  /* Ignore notifications that are not relevant at the moment.  */
  if (!IsReqtokenRelevant (data))
    {
      VLOG (1) << "Ignoring irrelevant detach notification";
      return;
    }

  bool needReinit = false;
  try
    {
      /* Handle the notification depending on the current state.  */
      switch (state)
        {
        case State::PREGENESIS:
          /* Detaches are irrelevant (and unlikely).  */
          break;

        case State::CATCHING_UP:
          if (!UpdateStateForDetach (parent, hash, data))
            needReinit = true;

          /* We may reach a catching-up target also when detaching blocks.  This
             happens, for instance, when a block was declared invalid and a
             couple of blocks was just detached.  If a ZMQ message is missed
             at the same time (*or if this was the very first detach
             notification*!), then the client is catching-up while only
             detaching.  */
          if (parent == targetBlockHash)
            needReinit = true;

          break;

        case State::UP_TO_DATE:
          if (!UpdateStateForDetach (parent, hash, data))
            needReinit = true;
          break;

        case State::UNKNOWN:
        case State::OUT_OF_SYNC:
        default:
          LOG (FATAL) << "Unexpected state: " << StateToString (state);
          break;
        }

      /* Detach the block in the pruning queue as well.  */
      if (pruningQueue != nullptr)
        pruningQueue->DetachBlock ();
    }
  catch (const StorageInterface::RetryWithNewTransaction& exc)
    {
      LOG (WARNING) << "Storage update failed, retrying: " << exc.what ();
      needReinit = true;
    }

  if (needReinit)
    ReinitialiseState ();

  if (state == State::UP_TO_DATE && pending != nullptr)
    {
      /* The heigh tpassed to the PendingMoveProcessor should be the "confirmed"
         height for processing moves, which means that it is one less than the
         currently detached height.  */
      const unsigned height = data["block"]["height"].asUInt ();
      CHECK_GT (height, 0);

      pending->ProcessDetachedBlock (storage->GetCurrentGameState (), data);
      NotifyPendingStateChange ();
    }
}

void
Game::PendingMove (const std::string& id, const Json::Value& data)
{
  CHECK_EQ (id, gameId);
  VLOG (2) << "Pending move:\n" << data;

  uint256 txid;
  CHECK (txid.FromHex (data["txid"].asString ()));
  VLOG (1) << "Processing pending move " << txid.ToHex ();

  std::lock_guard<std::mutex> lock(mut);
  if (state == State::UP_TO_DATE)
    {
      uint256 hash;
      CHECK (storage->GetCurrentBlockHash (hash));

      CHECK (pending != nullptr);
      pending->ProcessMove (storage->GetCurrentGameState (), data);
      NotifyPendingStateChange ();
    }
  else
    VLOG (1) << "Ignoring pending move while not up-to-date: " << data;
}

void
Game::ConnectRpcClient (jsonrpc::IClientConnector& conn,
                        const jsonrpc::clientVersion_t version)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (rpcClient == nullptr) << "RPC client is already connected";
  CHECK (chain == Chain::UNKNOWN);

  rpcClient = std::make_unique<XayaRpcClient> (conn, version);

  const Json::Value info = rpcClient->getblockchaininfo ();
  const std::string chainStr = info["chain"].asString ();
  if (chainStr == "main")
    chain = Chain::MAIN;
  else if (chainStr == "test")
    chain = Chain::TEST;
  else if (chainStr == "regtest")
    chain = Chain::REGTEST;
  else
    LOG (FATAL)
        << "Unexpected chain type returned by Xaya Core: " << chainStr;

  LOG (INFO) << "Connected to RPC daemon with chain " << ChainToString (chain);
  if (rules != nullptr)
    rules->InitialiseGameContext (chain, gameId, rpcClient.get ());
  if (pending != nullptr)
    pending->InitialiseGameContext (chain, gameId, rpcClient.get ());
}

unsigned
Game::GetXayaVersion () const
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (rpcClient != nullptr);

  const auto info = rpcClient->getnetworkinfo ();
  CHECK (info.isObject ());
  const auto& version = info["version"];
  CHECK (version.isUInt ());

  return version.asUInt ();
}

Chain
Game::GetChain () const
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (chain != Chain::UNKNOWN);
  return chain;
}

namespace
{

/**
 * Hash-to-height translation function that calls Xaya Core by RPC.
 */
unsigned
GetHeightForBlockHash (XayaRpcClient& rpc, const uint256& hash)
{
  const Json::Value data = rpc.getblockheader (hash.ToHex ());
  CHECK (data.isMember ("height"));
  const int height = data["height"].asInt ();
  CHECK_GE (height, 0);
  return height;
}

} // anonymous namespace

void
Game::SetStorage (StorageInterface& s)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (!mainLoop.IsRunning ());
  CHECK (pruningQueue == nullptr);

  storage = std::make_unique<internal::StorageWithCachedHeight> (s,
      [this] (const uint256& hash) {
        CHECK (rpcClient != nullptr);
        return GetHeightForBlockHash (*rpcClient, hash);
      });

  LOG (INFO) << "Storage has been added to Game, initialising it now";
  storage->Initialise ();

  if (chain == Chain::REGTEST)
    {
      LOG (INFO) << "Enabling height-cache cross-checks for regtest mode";
      storage->EnableCrossChecks ();
    }

  transactionManager.SetStorage (*storage);
}

void
Game::SetGameLogic (GameLogic& gl)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (!mainLoop.IsRunning ());
  rules = &gl;
  if (chain != Chain::UNKNOWN)
    rules->InitialiseGameContext (chain, gameId, rpcClient.get ());
}

void
Game::SetPendingMoveProcessor (PendingMoveProcessor& p)
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (!mainLoop.IsRunning ());
  pending = &p;
  if (chain != Chain::UNKNOWN)
    pending->InitialiseGameContext (chain, gameId, rpcClient.get ());
}

void
Game::EnablePruning (const unsigned nBlocks)
{
  LOG (INFO) << "Enabling pruning with " << nBlocks << " blocks to keep";

  std::lock_guard<std::mutex> lock(mut);
  CHECK (storage != nullptr);

  if (pruningQueue == nullptr)
    pruningQueue = std::make_unique<internal::PruningQueue> (*storage,
                                                             transactionManager,
                                                             nBlocks);
  else
    pruningQueue->SetDesiredSize (nBlocks);
}

bool
Game::DetectZmqEndpoint ()
{
  Json::Value notifications;

  {
    std::lock_guard<std::mutex> lock(mut);
    CHECK (rpcClient != nullptr) << "RPC client is not yet set up";
    notifications = rpcClient->getzmqnotifications ();
  }
  VLOG (1) << "Configured ZMQ notifications:\n" << notifications;

  bool foundBlocks = false;
  for (const auto& val : notifications)
    {
      const auto& typeVal = val["type"];
      if (!typeVal.isString ())
        continue;

      const auto& addrVal = val["address"];
      CHECK (addrVal.isString ());
      const std::string address = addrVal.asString ();
      CHECK (!address.empty ());

      const std::string type = typeVal.asString ();
      if (type == "pubgameblocks")
        {
          LOG (INFO) << "Detected ZMQ blocks endpoint: " << address;
          zmq.SetEndpoint (address);
          foundBlocks = true;
          continue;
        }
      if (type == "pubgamepending")
        {
          LOG (INFO) << "Detected ZMQ pending endpoint: " << address;
          zmq.SetEndpointForPending (address);
          continue;
        }
    }

  if (foundBlocks)
    return true;

  LOG (WARNING) << "No -zmqpubgameblocks notifier seems to be set up";
  return false;
}

Json::Value
Game::GetCustomStateData (
    const std::string& jsonField,
    const ExtractJsonFromStateWithLock& cb) const
{
  std::unique_lock<std::mutex> lock(mut);

  Json::Value res(Json::objectValue);
  res["gameid"] = gameId;
  res["chain"] = ChainToString (chain);
  res["state"] = StateToString (state);

  uint256 hash;
  unsigned height;
  if (!storage->GetCurrentBlockHashWithHeight (hash, height))
    return res;

  res["blockhash"] = hash.ToHex ();
  res["height"] = height;

  const GameStateData gameState = storage->GetCurrentGameState ();
  res[jsonField] = cb (gameState, hash, height, std::move (lock));

  return res;
}

Json::Value
Game::GetCustomStateData (
    const std::string& jsonField,
    const ExtractJsonFromStateWithBlock& cb) const
{
  return GetCustomStateData (jsonField,
    [&cb] (const GameStateData& state, const uint256& hash,
           const unsigned height,
           std::unique_lock<std::mutex> lock)
    {
      lock.unlock ();
      return cb (state, hash, height);
    });
}

Json::Value
Game::GetCustomStateData (
    const std::string& jsonField,
    const ExtractJsonFromState& cb) const
{
  return GetCustomStateData (jsonField,
    [&cb] (const GameStateData& state, const uint256& hash,
           const unsigned height)
    {
      return cb (state);
    });
}

Json::Value
Game::GetCurrentJsonState () const
{
  return GetCustomStateData ("gamestate",
      [this] (const GameStateData& state,
              const uint256& hash, const unsigned height,
              std::unique_lock<std::mutex> lock)
        {
          /* We keep the lock for the callback here, since e.g. SQLiteGame
             requires the state to be locked during GameStateToJson.  This
             method is not meant for performance critical tasks anyway, in
             which case specific data should be extracted using
             GetCustomStateData instead.  */
          return rules->GameStateToJson (state);
        });
}

Json::Value
Game::GetNullJsonState () const
{
  Json::Value res = GetCustomStateData ("data",
      [] (const GameStateData& state)
        {
          return Json::Value ();
        });
  res.removeMember ("data");
  return res;
}

Json::Value
Game::GetPendingJsonState () const
{
  std::unique_lock<std::mutex> lock(mut);
  return UnlockedPendingJsonState ();
}

Json::Value
Game::UnlockedPendingJsonState () const
{
  if (!zmq.IsPendingEnabled ())
    throw jsonrpc::JsonRpcException (jsonrpc::Errors::ERROR_RPC_INTERNAL_ERROR,
                                     "pending moves are not tracked");
  CHECK (pending != nullptr);

  Json::Value res(Json::objectValue);
  res["version"] = pendingStateVersion;
  res["gameid"] = gameId;
  res["chain"] = ChainToString (chain);
  res["state"] = StateToString (state);

  uint256 hash;
  unsigned height;
  if (storage->GetCurrentBlockHashWithHeight (hash, height))
    {
      res["blockhash"] = hash.ToHex ();
      res["height"] = height;
    }

  res["pending"] = pending->ToJson ();

  return res;
}

void
Game::NotifyStateChange () const
{
  /* Callers are expected to already hold the mut lock here (as that is the
     typical case when they make changes to the state anyway).  */
  VLOG (1) << "Notifying waiting threads about state change...";
  cvStateChanged.notify_all ();
}

void
Game::NotifyPendingStateChange ()
{
  /* Callers are expected to already hold the mut lock here (as that is the
     typical case when they make changes to the state anyway).  */
  CHECK_GT (pendingStateVersion, WAITFORCHANGE_ALWAYS_BLOCK);
  ++pendingStateVersion;
  VLOG (1)
      << "Notifying waiting threads about change of pending state,"
      << " new version: " << pendingStateVersion;
  cvPendingStateChanged.notify_all ();
}

void
Game::WaitForChange (const uint256& oldBlock, uint256& newBlock) const
{
  std::unique_lock<std::mutex> lock(mut);

  if (!oldBlock.IsNull () && storage->GetCurrentBlockHash (newBlock)
          && newBlock != oldBlock)
    {
      VLOG (1)
          << "Current block is different from old block,"
             " immediate return from WaitForChange";
      return;
    }

  if (zmq.IsRunning ())
    {
      VLOG (1) << "Waiting for state change on condition variable...";
      cvStateChanged.wait_for (lock, WAITFORCHANGE_TIMEOUT);
      VLOG (1) << "Potential state change detected in WaitForChange";
    }
  else
    LOG (WARNING)
        << "WaitForChange called with no active ZMQ listener,"
           " returning immediately";

  if (!storage->GetCurrentBlockHash (newBlock))
    newBlock.SetNull ();
}

Json::Value
Game::WaitForPendingChange (const int oldVersion) const
{
  std::unique_lock<std::mutex> lock(mut);

  if (oldVersion != WAITFORCHANGE_ALWAYS_BLOCK
        && oldVersion != pendingStateVersion)
    {
      VLOG (1)
          << "Known version differs from current one,"
             " returning immediately from WaitForPendingState";
      return UnlockedPendingJsonState ();
    }

  if (zmq.IsRunning () && zmq.IsPendingEnabled ())
    {
      VLOG (1) << "Waiting for pending state change on condition variable...";
      cvPendingStateChanged.wait_for (lock, WAITFORCHANGE_TIMEOUT);
      VLOG (1) << "Potential state change detected in WaitForPendingChange";
    }
  else
    LOG (WARNING)
        << "WaitForPendingChange called with no ZMQ listener on pending moves,"
           " returning immediately";

  return UnlockedPendingJsonState ();
}

void
Game::TrackGame ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (rpcClient != nullptr) << "RPC client is not yet set up";
  rpcClient->trackedgames ("add", gameId);
  LOG (INFO) << "Added " << gameId << " to tracked games";
}

void
Game::UntrackGame ()
{
  std::lock_guard<std::mutex> lock(mut);
  CHECK (rpcClient != nullptr) << "RPC client is not yet set up";
  rpcClient->trackedgames ("remove", gameId);
  LOG (INFO) << "Removed " << gameId << " from tracked games";
}

void
Game::Start ()
{
  if (pending == nullptr)
    {
      LOG (WARNING)
          << "No PendingMoveProcessor has been set, disabling pending moves"
             " in the ZMQ subscriber";
      zmq.SetEndpointForPending ("");
    }

  TrackGame ();
  zmq.Start ();

  std::lock_guard<std::mutex> lock(mut);
  ReinitialiseState ();
}

void
Game::Stop ()
{
  zmq.Stop ();
  UntrackGame ();

  /* Make sure to wake up all listeners waiting for a state update (as there
     won't be one anymore).  */
  NotifyStateChange ();
  NotifyPendingStateChange ();

  /* Give the RPC server some more time to return still active calls.  */
  std::this_thread::sleep_for (std::chrono::milliseconds (100));
}

void
Game::Run ()
{
  CHECK (storage != nullptr && rules != nullptr)
      << "Storage and GameLogic must be set before starting the main loop";

  internal::MainLoop::Functor startAction = [this] () { Start (); };
  internal::MainLoop::Functor stopAction = [this] () { Stop (); };

  mainLoop.Run (startAction, stopAction);
}

void
Game::SyncFromCurrentState (const Json::Value& blockchainInfo,
                            const uint256& currentHash)
{
  CHECK (state == State::OUT_OF_SYNC);

  uint256 daemonBestHash;
  CHECK (daemonBestHash.FromHex (blockchainInfo["bestblockhash"].asString ()));

  if (daemonBestHash == currentHash)
    {
      LOG (INFO) << "Game state matches current tip, we are up-to-date";
      state = State::UP_TO_DATE;
      transactionManager.SetBatchSize (1);
      return;
    }

  LOG (INFO)
      << "Game state does not match current tip, requesting updates from "
      << currentHash.ToHex ();
  const Json::Value upd
      = rpcClient->game_sendupdates (currentHash.ToHex (), gameId);

  LOG (INFO)
      << "Retrieving " << upd["steps"]["detach"].asInt () << " detach and "
      << upd["steps"]["attach"].asInt () << " attach steps with reqtoken = "
      << upd["reqtoken"].asString ()
      << ", leading to block " << upd["toblock"].asString ();

  state = State::CATCHING_UP;
  transactionManager.SetBatchSize (transactionBatchSize);

  CHECK (targetBlockHash.FromHex (upd["toblock"].asString ()));
  reqToken = upd["reqtoken"].asString ();
}

void
Game::ReinitialiseState ()
{
  state = State::UNKNOWN;
  LOG (INFO) << "Reinitialising game state";

  const Json::Value data = rpcClient->getblockchaininfo ();

  uint256 currentHash;
  if (storage->GetCurrentBlockHash (currentHash))
    {
      LOG (INFO) << "We have a current game state, syncing from there";
      state = State::OUT_OF_SYNC;
      SyncFromCurrentState (data, currentHash);
      return;
    }

  /* We do not have a current state in the storage.  This means that we have
     to reset to the initial state.  */

  if (genesisHash.IsNull ())
    {
      /* GetInitialState may be expensive, and it may do things like update
         some external game state (setting it to the initial one) as we do
         e.g. with SQLiteGame.  Hence we should avoid calling it often, and
         cache the genesis height once known.  That way, we call the function
         exactly twice, independent of how many blocks / reinitialisations
         we process in the mean time.  */

      std::string genesisHashHex;
      rules->GetInitialState (genesisHeight, genesisHashHex);

      CHECK (genesisHash.FromHex (genesisHashHex));
      LOG (INFO) << "Got genesis height from game: " << genesisHeight;
    }

  /* If the current block height in the daemon is not yet the game's genesis
     height, simply wait for the genesis hash to be attached.  */
  if (data["blocks"].asUInt () < genesisHeight)
    {
      LOG (INFO)
          << "Block height " << data["blocks"].asInt ()
          << " is before the genesis height " << genesisHeight;
      state = State::PREGENESIS;
      targetBlockHash = genesisHash;
      return;
    }

  /* Otherwise, we can store the initial state and start to sync from there.
     Note that we need to clear the storage *before* we call GetInitialState
     again here, because the latter will update the external state for
     the initial game state with SQLiteGame (and potentially other
     storage modules in the future).  */

  transactionManager.TryAbortTransaction ();
  storage->Clear ();

  std::string genesisHashHex;
  const GameStateData genesisData
      = rules->GetInitialState (genesisHeight, genesisHashHex);
  CHECK (genesisHash.FromHex (genesisHashHex));

  const std::string blockHashHex = rpcClient->getblockhash (genesisHeight);
  uint256 blockHash;
  CHECK (blockHash.FromHex (blockHashHex));
  CHECK (blockHash == genesisHash)
    << "The game's genesis block hash and height do not match";

  while (true)
    try
      {
        internal::ActiveTransaction tx(transactionManager);
        storage->SetCurrentGameStateWithHeight (genesisHash, genesisHeight,
                                                genesisData);
        tx.Commit ();
        break;
      }
    catch (const StorageInterface::RetryWithNewTransaction& exc)
      {
        LOG (WARNING) << "Storage update failed, retrying: " << exc.what ();
      }

  LOG (INFO)
      << "We are at the genesis height, stored initial game state for block "
      << genesisHash.ToHex ();
  NotifyStateChange ();

  state = State::OUT_OF_SYNC;
  SyncFromCurrentState (data, genesisHash);
}

} // namespace xaya
