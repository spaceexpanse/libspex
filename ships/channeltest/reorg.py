#!/usr/bin/env python3
# Copyright (C) 2019-2022 The XAYA developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Tests how a game channel reacts to reorgs of important on-chain transactions
(channel creation, join, resolutions, loss declarations).
"""

from shipstest import ShipsTest


class ReorgTest (ShipsTest):

  def run (self):
    self.generate (110)

    # Pre-generate the names.  This ensures that pending tracking on EVM
    # chains works (and doesn't hurt in other cases).
    for nm in ["foo", "bar", "baz"]:
      self.env.register ("p", nm)
    self.generate (1)

    # Create a test channel with two participants.  Remember the block hashes
    # where it was created and joined by the second one, so that we can
    # later invalidate those.
    self.mainLogger.info ("Creating test channel...")
    addr = [self.newSigningAddress () for _ in range (3)]
    channelId = self.sendMove ("foo", {"c": {
      "addr": addr[0],
    }})
    self.generate (1)
    createBlk, _ = self.env.getChainTip ()
    self.sendMove ("bar", {"j": {
      "id": channelId,
      "addr": addr[1],
    }})
    self.generate (1)
    joinBlk, _ = self.env.getChainTip ()

    # Start up three channel daemons:  The two participants and
    # a third one, which will join the channel later in a reorged
    # alternate reality.
    self.mainLogger.info ("Starting channel daemons...")
    with self.runChannelDaemon (channelId, "foo", addr[0]) as foo, \
         self.runChannelDaemon (channelId, "bar", addr[1]) as bar, \
         self.runChannelDaemon (channelId, "baz", addr[2]) as baz:

      daemons = [foo, bar, baz]

      self.mainLogger.info ("Running initialisation sequence...")
      pos = """
        xxxx..xx
        ........
        xxx.xxx.
        ........
        xx.xx.xx
        ........
        ........
        ........
      """
      foo.rpc._notify.setposition (pos)
      bar.rpc._notify.setposition (pos)
      baz.rpc._notify.setposition (pos)

      # Play a couple of turns and save the resulting "original" state.
      for c in range (8):
        _, state = self.waitForPhase (daemons, ["shoot"])
        turn = state["current"]["state"]["whoseturn"]
        with self.waitForTurnIncrease (daemons, 2):
          daemons[turn].rpc._notify.shoot (row=7, column=c)
      _, originalState = self.waitForPhase (daemons, ["shoot"])

      # Generate a couple of blocks to make sure this will be the longest
      # chain in the end.
      self.generate (10)

      self.mainLogger.info ("Reorg to channel creation...")
      self.rpc.spacexpanse.invalidateblock (createBlk)
      state = self.getSyncedChannelState (daemons)
      self.assertEqual (state["existsonchain"], False)
      self.rpc.spacexpanse.reconsiderblock (createBlk)
      self.rpc.spacexpanse.invalidateblock (joinBlk)
      self.assertEqual (self.env.getChainTip ()[0], createBlk)
      state = self.getSyncedChannelState (daemons)
      self.assertEqual (state["existsonchain"], True)
      self.assertEqual (state["current"]["state"]["parsed"]["phase"],
                        "single participant")

      self.mainLogger.info ("Alternate join...")
      self.sendMove ("baz", {"j": {
        "id": channelId,
        "addr": addr[2],
      }})
      self.expectPendingMoves ("bar", [])
      self.expectPendingMoves ("baz", ["j"])
      self.generate (1)

      # Make sure it is baz' turn.  If not, miss a shot with foo.
      self.mainLogger.info ("Building alternate reality...")
      _, state = self.waitForPhase (daemons, ["shoot"])
      if state["current"]["state"]["whoseturn"] == 0:
        with self.waitForTurnIncrease (daemons, 2):
          foo.rpc._notify.shoot (row=6, column=0)
        _, state = self.waitForPhase (daemons, ["shoot"])
      self.assertEqual (state["current"]["state"]["whoseturn"], 1)

      # Finish the game and let foo win.
      baz.rpc._notify.revealposition ()
      _, state = self.waitForPhase (daemons, ["finished"])
      self.assertEqual (state["current"]["state"]["parsed"]["winner"], 0)
      self.generate (1)
      self.expectGameState ({
        "channels": {},
        "gamestats": {
          "foo": {"won": 1, "lost": 0},
          "baz": {"won": 0, "lost": 1},
        },
      })

      self.mainLogger.info ("Restoring original state...")
      self.rpc.spacexpanse.reconsiderblock (joinBlk)
      state = self.getSyncedChannelState (daemons)
      self.assertEqual (state["current"]["meta"],
                        originalState["current"]["meta"])
      self.assertEqual (state["current"]["state"]["parsed"],
                        originalState["current"]["state"]["parsed"])

      # Make sure it is foo's turn.
      _, state = self.waitForPhase (daemons, ["shoot"])
      if state["current"]["state"]["whoseturn"] == 1:
        with self.waitForTurnIncrease (daemons, 2):
          bar.rpc._notify.shoot (row=6, column=1)
        _, state = self.waitForPhase (daemons, ["shoot"])
      self.assertEqual (state["current"]["state"]["whoseturn"], 0)

      # Test what happens if a resolution move gets reorged away.  Then the
      # player who resolved should still make sure it gets into the chain
      # again (since they obviously still know a better state).
      self.mainLogger.info ("Reorg of a resolution move...")
      bar.rpc.filedispute ()
      self.expectPendingMoves ("bar", ["d"])
      self.generate (1)
      state = self.getSyncedChannelState (daemons)
      self.assertEqual (state["dispute"], {
        "whoseturn": 0,
        "canresolve": False,
        "height": self.env.getChainTip ()[1],
      })
      with self.waitForTurnIncrease (daemons, 2):
        foo.rpc._notify.shoot (row=0, column=0)
      self.expectPendingMoves ("foo", ["r"])
      self.generate (1)
      resolutionBlk, _ = self.env.getChainTip ()
      state = self.getSyncedChannelState (daemons)
      assert "dispute" not in state

      self.rpc.spacexpanse.invalidateblock (resolutionBlk)
      state = foo.getCurrentState ()
      self.assertEqual (state["dispute"], {
        "whoseturn": 0,
        "canresolve": True,
        "height": self.env.getChainTip ()[1],
      })

      # A resolution is not resent if the previous transaction remained in
      # the mempool.  But in our case here, we actually resolved the dispute
      # and thus cleared the pending flag, and only then "reopened" it due
      # to the reorg.  For this case, at least the current implementation
      # sends a second resolution.  (To fix this, we would have to keep
      # track of previous resolutions even if their corresponding disputes
      # have been cleared already, which seems not worth the trouble for
      # the little extra potential benefit in some edge cases.)
      self.expectPendingMoves ("foo", ["r", "r"])
      self.generate (1)
      state = self.getSyncedChannelState (daemons)
      assert "dispute" not in state

      self.mainLogger.info ("Letting the game end with a dispute...")
      bar.rpc.filedispute ()
      self.expectPendingMoves ("bar", ["d"])
      self.generate (11)
      self.expectGameState ({
        "channels": {},
        "gamestats": {
          "foo": {"won": 0, "lost": 1},
          "bar": {"won": 1, "lost": 0},
        },
      })

      self.mainLogger.info ("Detaching a block and ending the game normally...")
      disputeTimeoutBlk, _ = self.env.getChainTip ()
      self.rpc.spacexpanse.invalidateblock (disputeTimeoutBlk)
      state = self.getSyncedChannelState (daemons)
      self.assertEqual (state["existsonchain"], True)

      # We miss a shot with foo and reveal with bar, so that this time
      # foo wins the channel (but ordinarily).
      with self.waitForTurnIncrease (daemons, 2):
        foo.rpc._notify.shoot (row=6, column=2)
      self.expectPendingMoves ("foo", ["r"])
      self.generate (1)
      self.waitForPhase (daemons, ["shoot"])
      bar.rpc._notify.revealposition ()
      _, state = self.waitForPhase (daemons, ["finished"])
      self.assertEqual (state["current"]["state"]["parsed"]["winner"], 0)
      txids = self.expectPendingMoves ("bar", ["l"])
      self.generate (1)
      self.expectGameState ({
        "channels": {},
        "gamestats": {
          "foo": {"won": 1, "lost": 0},
          "bar": {"won": 0, "lost": 1},
        },
      })

      self.mainLogger.info ("Reorg and resending of loss declaration...")
      winnerStmtBlk, _ = self.env.getChainTip ()
      self.rpc.spacexpanse.invalidateblock (winnerStmtBlk)
      state = self.getSyncedChannelState (daemons)
      self.assertEqual (state["current"]["state"]["parsed"]["phase"],
                        "finished")
      self.assertEqual (state["current"]["state"]["parsed"]["winner"], 0)
      # The old transaction should have been restored to the mempool, and
      # we should not resend another one (but detect that it is still there
      # and just wait).
      newTxids = self.expectPendingMoves ("bar", ["l"])
      self.assertEqual (newTxids, txids)
      self.generate (1)
      self.expectGameState ({
        "channels": {},
        "gamestats": {
          "foo": {"won": 1, "lost": 0},
          "bar": {"won": 0, "lost": 1},
        },
      })

      # Do a longer reorg, so that the original move will not be restored
      # to the mempool.  Verify that we send a new one.
      winnerStmtBlk, _ = self.env.getChainTip ()
      self.generate (10)
      self.rpc.spacexpanse.invalidateblock (winnerStmtBlk)
      state = self.getSyncedChannelState (daemons)
      self.assertEqual (state["current"]["state"]["parsed"]["phase"],
                        "finished")
      self.assertEqual (state["current"]["state"]["parsed"]["winner"], 0)
      newTxids = self.expectPendingMoves ("bar", ["l"])
      assert txids != newTxids
      self.generate (1)
      self.expectGameState ({
        "channels": {},
        "gamestats": {
          "foo": {"won": 1, "lost": 0},
          "bar": {"won": 0, "lost": 1},
        },
      })


if __name__ == "__main__":
  ReorgTest ().main ()
