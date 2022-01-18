// Copyright (C) 2019-2022 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GAMECHANNEL_ROLLINGSTATE_HPP
#define GAMECHANNEL_ROLLINGSTATE_HPP

#include "boardrules.hpp"
#include "signatures.hpp"

#include "proto/metadata.pb.h"
#include "proto/stateproof.pb.h"

#include <xayautil/uint256.hpp>

#include <map>
#include <memory>

namespace xaya
{

/**
 * All data about the current board state of a channel game.  This keeps track
 * of the latest known state including full proof for each reinitialisation
 * of the channel.  It is updated when new on-chain or off-chain data
 * is provided, and can return the current best state (proof) for use
 * in frontends or also e.g. for disputes and resolutions.
 *
 * We need to keep track of all known reinitialisations rather than only
 * the "current" one so that we can handle situations in which a move that
 * reinitialised the channel is rolled back.  Then we want to make sure that
 * we still have the "latest" state (and proof) for the resulting previous
 * reinitialisation as well.
 */
class RollingState
{

private:

  /**
   * The data corresponding to one reinitialisation.
   */
  struct ReinitData
  {

    /**
     * The metadata for this reinitialisation.  We keep a pointer to it rather
     * than the instance itself, because a reference to the proto is encoded
     * in latestState and we need it to remain valid even if the instance
     * gets moved around.
     */
    std::unique_ptr<proto::ChannelMetadata> meta;

    /** The initial state for that reinitialisation.  */
    BoardState reinitState;

    /** The turn count for the latest state known on chain.  */
    unsigned onChainTurn;

    /** The state proof for the latest state.  */
    proto::StateProof proof;

    /** The latest state as parsed object.  */
    std::unique_ptr<ParsedBoardState> latestState;

    ReinitData () = default;
    ReinitData (ReinitData&&) = default;
    ReinitData& operator= (ReinitData&&) = default;

    ReinitData (const ReinitData&) = delete;
    ReinitData& operator= (const ReinitData&) = delete;

  };

  /** Board rules to use for our game.  */
  const BoardRules& rules;

  /** Signature verifier for state proofs.  */
  const SignatureVerifier& verifier;

  /** The ID of the channel this is for.  */
  const uint256& channelId;

  /**
   * All known data about reinitsialisations we have.  At the very beginning,
   * this map will be empty until the first block data is provided.  Until
   * this is done, GetLatestState and GetStateProof must not be called.
   */
  std::map<std::string, ReinitData> reinits;

  /** The reinit ID of the current reinitialisation.  */
  std::string reinitId;

public:

  explicit RollingState (const BoardRules& r, const SignatureVerifier& v,
                         const uint256& id)
    : rules(r), verifier(v), channelId(id)
  {}

  RollingState () = delete;
  RollingState (const RollingState&) = delete;
  void operator= (const RollingState&) = delete;

  /**
   * Returns the current latest state.
   */
  const ParsedBoardState& GetLatestState () const;

  /**
   * Returns a proof for the current latest state.
   */
  const proto::StateProof& GetStateProof () const;

  /**
   * Returns the turn count of the best state known on chain.
   */
  unsigned GetOnChainTurnCount () const;

  /**
   * Returns the reinitialisation ID of the channel for which the current
   * latest state (as returned by GetLatestState and GetStateProof) is.
   */
  const std::string& GetReinitId () const;

  /**
   * Returns the channel metadata corresponding to the currently best
   * reinitId.
   */
  const proto::ChannelMetadata& GetMetadata () const;

  /**
   * Updates the state for a newly received on-chain update.  This assumes
   * that the state proof is valid, and it also updates the "current"
   * reinitialisation to the one seen in the update.
   *
   * Returns true if an actual change has been made (i.e. the provided
   * state proof was valid and newer than what we had so far).
   */
  bool UpdateOnChain (const proto::ChannelMetadata& meta,
                      const BoardState& reinitState,
                      const proto::StateProof& proof);

  /**
   * Updates the state for a newly received off-chain state with the
   * given reinitialisation ID (if we know it).  This verifies the state proof,
   * and ignores invalid updates.
   *
   * Returns true if an actual change has been made, i.e. the reinit was
   * known and the state advanced forward with the new state proof.
   */
  bool UpdateWithMove (const std::string& updReinit,
                       const proto::StateProof& proof);

};

} // namespace xaya

#endif // GAMECHANNEL_ROLLINGSTATE_HPP
