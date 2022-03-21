# Copyright (C) 2018 The XAYA developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from mover import MoverTest

"""
Tests moverd with persistent storage (and in particular, that the data is
really persisted and not synced again on a restart).
"""

# Regexp for the log that is printed when we sync from scratch.
SYNCING_FROM_SCRATCH = 'stored initial game state'


class PersistenceTest (MoverTest):

  def __init__ (self, storageType):
    super (PersistenceTest, self).__init__ ()
    self.storageType = storageType

  def run (self):
    self.log.info ("Testing storage type '%s'..." % self.storageType)

    self.generate (101)
    self.move ("a", "k", 2)
    self.move ("b", "y", 1)
    self.generate (1)
    expectedState = {"players": {
      "a": {"x": 0, "y": 1, "dir": "up", "steps": 1},
      "b": {"x": -1, "y": 1},
    }}
    self.expectGameState (expectedState)

    # Restart with persistent storage.  Since we had memory storage before,
    # this is expected to sync from scratch.
    self.log.info ("Enabling persistent storage, should sync from scratch")
    self.restartWithStorageType (self.storageType)
    self.expectGameState (expectedState)
    self.stopGameDaemon ()
    assert self.gamenode.logMatches (SYNCING_FROM_SCRATCH)

    # Restart again, this time it should no longer sync from scratch.
    self.log.info ("Restarting game daemon, should have kept data")
    self.restartWithStorageType (self.storageType)
    self.expectGameState (expectedState)
    self.stopGameDaemon ()
    assert not self.gamenode.logMatches (SYNCING_FROM_SCRATCH)

  def restartWithStorageType (self, value):
    """
    Restarts the game daemon with the given storage type.
    """

    self.log.info ("Restarting with --storage_type=%s" % value)
    self.stopGameDaemon ()
    self.startGameDaemon (extraArgs=["--storage_type=%s" % value])
