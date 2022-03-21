// Copyright (C) 2018-2019 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAGAME_STORAGE_HPP
#define XAYAGAME_STORAGE_HPP

#include <xutil/uint256.hpp>

#include <map>
#include <stdexcept>
#include <string>

namespace spacexpanse
{

/**
 * The game-specific data that encodes a game state.  std::string is used
 * as a convenient container, but games are advised to actually use binary
 * encoding for more compact storage.  Protocol Buffers may be a good
 * way to encode state data (although of course not mandatory).
 */
using GameStateData = std::string;
/** The game-specific undo data for a block.  */
using UndoData = std::string;

/**
 * Interface for the storage layer used by the game.  This is used to
 * hold undo data for every block in the currently active chain as well
 * as the current game state (and its associated block hash).
 *
 * This class is not thread-safe; if used from multiple threads at the same
 * time (e.g. the ZMQ listener and the main thread during start-up), it has
 * to be properly synchronised (also to get a consistent view).
 */
class StorageInterface
{

public:

  class RetryWithNewTransaction;

  virtual ~StorageInterface () = default;

  /**
   * Called after the storage has been attached to a game.  This can be used
   * to open external resources if necessary.
   */
  virtual void Initialise ();

  /**
   * Removes all data, corresponding to a full reset of the state
   * (e.g. for starting a sync from scratch).
   *
   * In contrast to the other functions that modify data, *no* transaction is
   * started with BeginTransaction before this function is called.  It should
   * by itself make sure that all data is completely removed atomically.
   */
  virtual void Clear () = 0;

  /**
   * Retrieves the block hash to which the current game state belongs.  Returns
   * false if there is no "current" game state.
   */
  virtual bool GetCurrentBlockHash (uint256& hash) const = 0;

  /**
   * Retrieves the current game state.  Must not be called if there is none
   * (i.e. if GetCurrentBlockHash returns false).
   */
  virtual GameStateData GetCurrentGameState () const = 0;

  /**
   * Updates the current game state and associated block hash.
   */
  virtual void SetCurrentGameState (const uint256& hash,
                                    const GameStateData& data) = 0;

  /**
   * Retrieves undo data for the given block hash.  Returns false if none is
   * stored with that key.
   */
  virtual bool GetUndoData (const uint256& hash, UndoData& data) const = 0;

  /**
   * Adds undo data for the given block hash.  If there is already undo data
   * for the given hash, then the passed-in data must be equivalent from the
   * game's point of view.  It is undefined which one is kept.
   *
   * Also the height can be stored by the implementation, to be used with
   * PruneUndoData.  Apart from the ability to implement this function,
   * the height is not needed for anything else.
   */
  virtual void AddUndoData (const uint256& hash,
                            unsigned height, const UndoData& data) = 0;

  /**
   * Allows the storage implementation to delete the undo data associated to
   * the given block hash.
   */
  virtual void
  ReleaseUndoData (const uint256& hash)
  {
    /* Do nothing by default.  This function can be overridden to free space
       for no longer required data (e.g. undo data of blocks that have been
       detached).  */
  }

  /**
   * Allows the storage to release all undo data with heights up to (including)
   * the given height.
   */
  virtual void
  PruneUndoData (const unsigned height)
  {
    /* Do nothing by default.  This function can be overridden to free space
       for very old undo data, which is unlikely to be needed again in the
       future (because the blocks involved have many confirmations).  */
  }

  /**
   * Tells the storage that a change to the state is about to be made
   * (because a new block is being attached or detached).
   *
   * Transactions will not be nested, i.e. this function is only called when
   * the last transaction has either been committed or rolled back.
   *
   * By default, this function does nothing.  If the storage implementation
   * supports a transaction mechanism to keep multiple changes consistent,
   * it can override the method to start such a transaction.
   */
  virtual void BeginTransaction ();

  /**
   * Tells the storage that all state changes related to the previously started
   * transaction have been completed successfully.
   *
   * This function may fail (throw an exception).  In that case,
   * RollbackTransaction will be called during the cleanup.
   */
  virtual void CommitTransaction ();

  /**
   * Tells the storage that there was an error during the state changes for the
   * previously started transaction, and all changes made since then should
   * be reverted if possible.
   */
  virtual void RollbackTransaction ();

};

/**
 * Exception that can be thrown by StorageInterface implementations if some
 * operation (e.g. an update) fails but the Game instance may retry and that
 * may succeed.  After the storage throws this exception, the Game instance
 * still calls RollbackTransaction() on the storage (as it always does in
 * case of errors) before retrying.  It may also decide to not retry and
 * just crash the process.
 *
 * An example situation where this is useful is LMDB map resizing:  When the
 * size of an LMDB database gets too large, update operations fail.  In this
 * case, the map needs to be resized.  The storage cannot do that directly
 * itself, though, as it needs to cancel the current transction first and
 * then the Game instance would be left in an inconsistent state.  Thus it can
 * just throw RetryWithNewTransaction, let the Game abort the current
 * transaction, then resize the map and finally let the Game reinitialise
 * itself and continue updating.
 */
class StorageInterface::RetryWithNewTransaction : public std::runtime_error
{

public:

  using std::runtime_error::runtime_error;

};

/**
 * An implementation of the StorageInterface that holds all data just in
 * memory.  This means that it has to resync on every restart, but may be
 * quick and easy for testing / prototyping.
 *
 * Besides needing to sync from scratch on every restart, this is actually
 * a fully functional implementation.
 */
class MemoryStorage : public StorageInterface
{

private:

  /** Whether or not we have a current block hash / state.  */
  bool hasState = false;

  /** The current block hash, if we have one.  */
  uint256 currentBlock;
  /** The current game state.  */
  GameStateData currentState;

  /**
   * Convenience struct to hold a block height together with undo data.
   */
  struct HeightAndUndoData
  {
    unsigned height;
    UndoData data;
  };

  /** Type of the map holding undo data.  */
  using UndoMap = std::map<uint256, HeightAndUndoData>;

  /** Undo data associated to block hashes we know about.  */
  UndoMap undoData;

  /**
   * Whether or not a transaction has currently been started.  The storage
   * itself does not support transaction rollbacks, but it keeps track of
   * whether or not transactions have been started.  This is used to verify
   * correct transaction state for the various operations, to ensure that
   * the calling code works fine in tests (for instance).
   */
  bool startedTxn = false;

public:

  MemoryStorage () = default;
  MemoryStorage (const MemoryStorage&) = delete;

  void operator= (const MemoryStorage&) = delete;

  void Clear () override;

  bool GetCurrentBlockHash (uint256& hash) const override;
  GameStateData GetCurrentGameState () const override;
  void SetCurrentGameState (const uint256& hash,
                            const GameStateData& data) override;

  bool GetUndoData (const uint256& hash, UndoData& data) const override;
  void AddUndoData (const uint256& hash,
                    unsigned height, const UndoData& data) override;
  void ReleaseUndoData (const uint256& hash) override;
  void PruneUndoData (unsigned height) override;

  void BeginTransaction () override;
  void CommitTransaction () override;
  void RollbackTransaction () override;

};

} // namespace spacexpanse

#endif // XAYAGAME_STORAGE_HPP
