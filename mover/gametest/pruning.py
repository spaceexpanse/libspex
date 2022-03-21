#!/usr/bin/env python3
# Copyright (C) 2018-2020 The XAYA developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from mover import MoverTest

"""
Tests basic operation with pruning enabled.
"""

# Regexp for the log that is printed when a pruned block would be needed.
FAILED_GETTING_UNDO = 'Failed to retrieve undo data'


class PruningTest (MoverTest):

  def run (self):
    self.generate (101)
    self.expectGameState ({"players": {}})

    # Test that basic forward-processing works even with no kept blocks.
    self.setPruning (0)

    self.move ("a", "k", 2)
    self.move ("b", "y", 1)
    self.generate (1)
    self.expectGameState ({"players": {
      "a": {"x": 0, "y": 1, "dir": "up", "steps": 1},
      "b": {"x": -1, "y": 1},
    }})

    self.move ("a", "l", 2)
    self.generate (1)
    self.expectGameState ({"players": {
      "a": {"x": 1, "y": 1,  "dir": "right", "steps": 1},
      "b": {"x": -1, "y": 1},
    }})

    self.stopGameDaemon ()
    assert not self.gamenode.logMatches (FAILED_GETTING_UNDO)

    # Enable pruning while keeping at least one block, so that we can reorg.
    self.setPruning (1)

    txid = self.move ("a", "j", 1)
    fullTx = self.rpc.spacexpanse.gettransaction (txid)["hex"]
    self.generate (1)
    self.expectGameState ({"players": {
      "a": {"x": 1, "y": 0},
      "b": {"x": -1, "y": 1},
    }})
    blk = self.rpc.spacexpanse.getbestblockhash ()

    self.rpc.spacexpanse.invalidateblock (blk)
    # Ensure that the move of a is in the mempool again.
    self.rpc.spacexpanse.sendrawtransaction (fullTx)
    self.assertEqual (self.rpc.spacexpanse.getrawmempool (), [txid])
    self.move ("b", "n", 1)
    self.generate (1)
    self.expectGameState ({"players": {
      "a": {"x": 1, "y": 0},
      "b": {"x": 0, "y": 0},
    }})

    self.stopGameDaemon ()
    assert not self.gamenode.logMatches (FAILED_GETTING_UNDO)

    # Produce a longer reorg, which leads to a resync from scratch.  This still
    # "works" (due to the resync), but prints a log message that we can look for
    # to ensure things work as expected.
    self.setPruning (1)

    blk = self.rpc.spacexpanse.getbestblockhash ()
    self.generate (1)
    self.expectGameState ({"players": {
      "a": {"x": 1, "y": 0},
      "b": {"x": 0, "y": 0},
    }})
    self.rpc.spacexpanse.invalidateblock (blk)
    self.syncGame ()

    self.stopGameDaemon ()
    assert self.gamenode.logMatches (FAILED_GETTING_UNDO)

  def setPruning (self, value):
    """
    Restarts the game daemon to change the pruning setting to the given value.
    """

    self.log.info ("Setting pruning to %d" % value)
    self.stopGameDaemon ()
    self.startGameDaemon (extraArgs=["--enable_pruning=%d" % value])


if __name__ == "__main__":
  PruningTest ().main ()
