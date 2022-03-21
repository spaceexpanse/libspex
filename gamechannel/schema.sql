-- Copyright (C) 2019 The XAYA developers
-- Distributed under the MIT software license, see the accompanying
-- file COPYING or http://www.opensource.org/licenses/mit-license.php.

-- The data for all open game channels that is part of the global game state.
CREATE TABLE IF NOT EXISTS `xgame_game_channels` (

  -- The ID of the channel, which is typically the txid that created it.
  `id` BLOB PRIMARY KEY,

  -- The current metadata (mainly list of participants), as a serialised
  -- ChannelMetadata protocol buffer instance.
  `metadata` BLOB NOT NULL,

  -- The encoded state at the last reinitialisation.
  `reinit` BLOB NOT NULL,

  -- The latest known state as a full serialised state proof.  Can be NULL
  -- if there is no state beyond the reinit state.
  `stateproof` BLOB NULL,

  -- If there is a dispute open (based on the current proven state), then
  -- the block height when it was filed.
  `disputeheight` INTEGER NULL

);

-- We need to look up disputed channels by height, so that we can iterate
-- over all timed out channels and process them.
CREATE INDEX IF NOT EXISTS `xgame_game_channels_by_disputeheight`
    ON `xgame_game_channels` (`disputeheight`);
