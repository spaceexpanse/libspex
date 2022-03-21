# Copyright (C) 2018 The XAYA developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from xgametest.testcase import XGameTest

import os
import os.path


class MoverTest (XGameTest):
  """
  An integration test for the Mover game.
  """

  def __init__ (self):
    top_builddir = os.getenv ("top_builddir")
    if top_builddir is None:
      top_builddir = "../.."
    moverd = os.path.join (top_builddir, "mover", "moverd")
    super (MoverTest, self).__init__ ("mv", moverd)

  def move (self, name, direction, steps):
    """
    Utility method to send a Mover move.
    """

    return self.sendMove (name, {"d": direction, "n": steps})
