#!/usr/bin/env python3
# Copyright (C) 2018-2021 The XAYA developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from mover import MoverTest

import time

"""
Tests how the moverd reacts if xd is stopped and restarted intermittantly.
"""


class StoppedXdTest (MoverTest):

  def run (self):
    self.generate (101)
    self.expectGameState ({"players": {}})

    self.move ("a", "k", 2)
    self.generate (1)
    self.expectGameState ({"players": {
      "a": {"x": 0, "y": 1, "dir": "up", "steps": 1},
    }})

    # Stop and restart the SpaceXpanse Core daemon.  This should not impact the game
    # daemon, at least not as long as it does not try to send a JSON-RPC
    # message while xd is down.  The ZMQ subscription should be back up
    # again automatically.
    self.log.info ("Restarting SpaceXpanse daemon")
    self.xnode.stop ()
    self.xnode.start ()
    self.rpc.spacexpanse = self.xnode.rpc

    # Track the game again and sleep a short time, which ensures that the
    # ZMQ subscription has indeed had time to catch up again.
    self.rpc.spacexpanse.trackedgames ("add", "mv")
    time.sleep (0.1)

    # Mine another block and verify that the game updates.
    self.generate (1)
    self.expectGameState ({"players": {
      "a": {"x": 0, "y": 2},
    }})


if __name__ == "__main__":
  StoppedXdTest ().main ()
