// Copyright (C) 2019 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "boardrules.hpp"

namespace spacexpanse
{

constexpr int ParsedBoardState::NO_TURN;

Json::Value
ParsedBoardState::ToJson () const
{
  return Json::Value ();
}

} // namespace spacexpanse
