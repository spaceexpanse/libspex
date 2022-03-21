#!/usr/bin/env python3
# Copyright (C) 2019-2022 The XAYA developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from shipstest import ShipsTest

"""
Tests a reorg together with dispute handling.
"""


class ReorgTest (ShipsTest):

  def run (self):
    self.generate (110)

    # Create a test channel with two participants.
    self.mainLogger.info ("Opening a test channel...")
    cid, _ = self.openChannel (["foo", "bar"])
    self.expectChannelState (cid, "first commitment", None)

    # Open a dispute.
    self.mainLogger.info ("Filing a dispute...")
    state = self.getStateProof (cid, "turn: 0")
    self.sendMove ("xyz", {"d": {"id": cid, "state": state}})
    self.generate (1)
    _, disputeHeight = self.env.getChainTip ()
    self.expectChannelState (cid, "first commitment", disputeHeight)
    self.generate (1)
    reorgBlock = self.rpc.spacexpanse.getbestblockhash ()

    # Let the dispute expire.
    self.mainLogger.info ("Letting the dispute expire...")
    self.generate (50)
    originalState = self.getGameState ()
    self.assertEqual (originalState, {
      "gamestats":
        {
          "foo": {"won": 0, "lost": 1},
          "bar": {"won": 1, "lost": 0},
        },
      "channels": {},
    })

    # Reorg back and close the channel through a loss declaration.
    self.mainLogger.info ("Reorg and create alternate reality...")
    self.rpc.spacexpanse.invalidateblock (reorgBlock)
    self.expectChannelState (cid, "first commitment", disputeHeight)
    ch = self.getGameState ()["channels"][cid]
    self.sendMove ("bar", {"l": {"id": cid, "r": ch["meta"]["reinit"]}})
    self.generate (10)
    self.expectGameState ({
      "gamestats":
        {
          "foo": {"won": 1, "lost": 0},
          "bar": {"won": 0, "lost": 1},
        },
      "channels": {},
    })

    # Get back to the original chain.
    self.mainLogger.info ("Revert to original reality...")
    self.rpc.spacexpanse.reconsiderblock (reorgBlock)
    self.expectGameState (originalState)


if __name__ == "__main__":
  ReorgTest ().main ()
