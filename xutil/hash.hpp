// Copyright (C) 2019 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAUTIL_HASH_HPP
#define XAYAUTIL_HASH_HPP

#include "uint256.hpp"

#include <memory>
#include <string>

namespace spacexpanse
{

/**
 * Utility class to hash data using SHA-256.  This is used for random numbers
 * in libxgame, but may also be used for games directly e.g. to implement
 * hash commitments.
 */
class SHA256
{

private:

  /**
   * Holder for the internal state.  This is not exposed here in the header,
   * so that the dependency on the underlying library (e.g. OpenSSL) is
   * kept as a pure implementation detail.
   */
  class State;

  /** The underlying current state of the hasher.  */
  std::unique_ptr<State> state;

public:

  SHA256 ();
  ~SHA256 ();

  SHA256 (const SHA256&) = delete;
  void operator= (const SHA256&) = delete;

  SHA256& operator<< (const std::string& data);
  SHA256& operator<< (const uint256& data);

  /**
   * Finalises the hash and returns the resulting value as uint256.  After
   * this function has been called, no more operations on the SHA256 instance
   * are allowed.
   */
  uint256 Finalise ();

  /**
   * Utility method to hash just a string directory.
   */
  static uint256 Hash (const std::string& data);

};

} // namespace spacexpanse

#endif // XAYAUTIL_HASH_HPP
