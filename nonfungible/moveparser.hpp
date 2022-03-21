// Copyright (C) 2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NONFUNGIBLE_MOVEPARSER_HPP
#define NONFUNGIBLE_MOVEPARSER_HPP

#include "assets.hpp"

#include <xgame/sqlitestorage.hpp>

#include <json/json.h>

#include <string>

namespace nf
{

/**
 * Extracts the balance of a given asset and user from the database.
 * Returns 0 if there is no entry.
 */
Amount GetDbBalance (const spacexpanse::SQLiteDatabase& db, const Asset& a,
                     const std::string& name);

/**
 * Core implementation of parsing and validating moves received either
 * in new blocks or as pending transactions.  The actual processing of them
 * (i.e. updating the game-state database or pending state) is done by
 * subclasses.
 */
class MoveParser
{

private:

  /**
   * Handles an individual operation (i.e. a move that is a JSON object,
   * or an element of an array move).
   */
  void HandleOperation (const std::string& name, const Json::Value& mv);

  /**
   * Handles a mint operation, i.e. a move's "m" part if any.
   */
  void HandleMint (const std::string& name, const Json::Value& op);

  /**
   * Handles a transfer operation, i.e. a move's "t" part if any.
   */
  void HandleTransfer (const std::string& name, const Json::Value& op);

  /**
   * Handles a burn operation, i.e. a move's "b" part if any.
   */
  void HandleBurn (const std::string& name, const Json::Value& op);

protected:

  /**
   * The database we use.  It is used for reading the current state
   * when validating moves.
   */
  const spacexpanse::SQLiteDatabase& db;

  /**
   * Called when a valid move to mint an asset has been found.  If there
   * is custom data specified with it, the data pointer will be non-null.
   */
  virtual void ProcessMint (const Asset& a, Amount supply,
                            const std::string* data) = 0;

  /**
   * Called when a valid transfer move has been found.
   */
  virtual void ProcessTransfer (const Asset& a, Amount num,
                                const std::string& sender,
                                const std::string& recipient) = 0;

  /**
   * Called when a valid burn move has been found.
   */
  virtual void ProcessBurn (const Asset& a, Amount num,
                            const std::string& sender) = 0;

  /**
   * Determine if an asset of this type exists already.  By default, it
   * looks up in the database.  Subclasses may extend this function (e.g. to
   * take pending state into account).
   */
  virtual bool AssetExists (const Asset& a) const;

  /**
   * Get the current balance of some name and asset.  By default, this
   * checks in the database.  Subclasses may extend this, e.g. to look at
   * the pending state in addition.
   */
  virtual Amount GetBalance (const Asset& a, const std::string& name) const;

public:

  explicit MoveParser (const spacexpanse::SQLiteDatabase& d)
    : db(d)
  {}

  virtual ~MoveParser () = default;

  MoveParser () = delete;
  MoveParser (const MoveParser&) = delete;
  void operator= (const MoveParser&) = delete;

  /**
   * Processes a single move given as JSON object as per the ZMQ
   * interface (i.e. containing both the name and actual move).
   */
  void ProcessOne (const Json::Value& obj);

};

} // namespace nf

#endif // NONFUNGIBLE_MOVEPARSER_HPP
