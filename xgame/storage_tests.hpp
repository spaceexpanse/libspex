// Copyright (C) 2018-2019 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef XAYAGAME_STORAGE_TESTS_HPP
#define XAYAGAME_STORAGE_TESTS_HPP

#include "storage.hpp"

#include <xutil/uint256.hpp>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <string>

namespace spacexpanse
{

/**
 * Type-parametrised test for basic properties of the StorageInterface.  It can
 * be used to test implementations.
 */
template <typename T>
  class BasicStorageTests : public testing::Test
{

private:

  /**
   * Extend the given string with some "binary data" (nul bytes, non-ASCII
   * bytes) to make sure that binary is handled properly.
   */
  static std::string
  ExtendWithBinary (const std::string& str)
  {
    std::string res = str;
    res.append (1, '\0');
    res.append (1, '\xFF');
    res.append ("postfix");
    return res;
  }

protected:

  uint256 hash1, hash2;

  const GameStateData state1 = ExtendWithBinary ("state 1");
  const GameStateData state2 = ExtendWithBinary ("state 2");

  const UndoData undo1 = ExtendWithBinary ("undo 1");
  const UndoData undo2 = ExtendWithBinary ("undo 2");

  T storage;

  BasicStorageTests ()
  {
    CHECK (hash1.FromHex ("01" + std::string (62, '0')));
    CHECK (hash2.FromHex ("02" + std::string (62, '0')));
    this->storage.Initialise ();
  }

};

TYPED_TEST_CASE_P (BasicStorageTests);

TYPED_TEST_P (BasicStorageTests, Empty)
{
  uint256 hash;
  EXPECT_FALSE (this->storage.GetCurrentBlockHash (hash));
  UndoData undo;
  EXPECT_FALSE (this->storage.GetUndoData (this->hash1, undo));
}

TYPED_TEST_P (BasicStorageTests, CurrentState)
{
  uint256 hash;

  this->storage.BeginTransaction ();
  this->storage.SetCurrentGameState (this->hash1, this->state1);
  this->storage.CommitTransaction ();
  ASSERT_TRUE (this->storage.GetCurrentBlockHash (hash));
  EXPECT_EQ (hash, this->hash1);
  EXPECT_EQ (this->storage.GetCurrentGameState (), this->state1);

  this->storage.BeginTransaction ();
  this->storage.SetCurrentGameState (this->hash2, this->state2);
  this->storage.CommitTransaction ();
  ASSERT_TRUE (this->storage.GetCurrentBlockHash (hash));
  EXPECT_EQ (hash, this->hash2);
  EXPECT_EQ (this->storage.GetCurrentGameState (), this->state2);
}

TYPED_TEST_P (BasicStorageTests, StoringUndoData)
{
  UndoData undo;
  EXPECT_FALSE (this->storage.GetUndoData (this->hash1, undo));

  this->storage.BeginTransaction ();
  this->storage.AddUndoData (this->hash1, 42, this->undo1);
  this->storage.CommitTransaction ();
  ASSERT_TRUE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_EQ (undo, this->undo1);
  EXPECT_FALSE (this->storage.GetUndoData (this->hash2, undo));

  /* Adding twice should be fine (just have no effect but also not crash).  */
  this->storage.BeginTransaction ();
  this->storage.AddUndoData (this->hash1, 50, this->undo1);
  this->storage.AddUndoData (this->hash2, 10, this->undo2);
  this->storage.CommitTransaction ();

  ASSERT_TRUE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_EQ (undo, this->undo1);
  ASSERT_TRUE (this->storage.GetUndoData (this->hash2, undo));
  EXPECT_EQ (undo, this->undo2);

  /* Removing should be ok (not crash), but otherwise no effect is guaranteed
     (in particular, not that it will actually be removed).  */
  this->storage.BeginTransaction ();
  this->storage.ReleaseUndoData (this->hash1);
  this->storage.CommitTransaction ();
  ASSERT_TRUE (this->storage.GetUndoData (this->hash2, undo));
  EXPECT_EQ (undo, this->undo2);
  this->storage.BeginTransaction ();
  this->storage.ReleaseUndoData (this->hash2);
  this->storage.CommitTransaction ();
}

TYPED_TEST_P (BasicStorageTests, Clear)
{
  this->storage.BeginTransaction ();
  this->storage.SetCurrentGameState (this->hash1, this->state1);
  this->storage.AddUndoData (this->hash1, 18, this->undo1);
  this->storage.CommitTransaction ();

  uint256 hash;
  EXPECT_TRUE (this->storage.GetCurrentBlockHash (hash));
  UndoData undo;
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));

  this->storage.Clear ();
  EXPECT_FALSE (this->storage.GetCurrentBlockHash (hash));
  EXPECT_FALSE (this->storage.GetUndoData (this->hash1, undo));
}

TYPED_TEST_P (BasicStorageTests, ReadInTransaction)
{
  this->storage.BeginTransaction ();
  this->storage.SetCurrentGameState (this->hash1, this->state1);
  this->storage.AddUndoData (this->hash1, 18, this->undo1);

  uint256 hash;
  EXPECT_TRUE (this->storage.GetCurrentBlockHash (hash));
  UndoData undo;
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));

  this->storage.RollbackTransaction ();
}

REGISTER_TYPED_TEST_CASE_P (BasicStorageTests,
                            Empty, CurrentState, StoringUndoData,
                            Clear, ReadInTransaction);

/**
 * Tests specific for the pruning/removing of undo data in a storage.  Since
 * the Storage interface itself does not require undo data to be removed when
 * possible, this functionality is not tested as part of the core storage tests.
 * The PruningStorageTests can be applied to implementations that wish to
 * guarantee immediate removal of released undo data.
 */
template <typename T>
  using PruningStorageTests = BasicStorageTests<T>;

TYPED_TEST_CASE_P (PruningStorageTests);

TYPED_TEST_P (PruningStorageTests, ReleaseUndoData)
{
  this->storage.BeginTransaction ();
  this->storage.AddUndoData (this->hash1, 20, this->undo1);
  this->storage.CommitTransaction ();

  UndoData undo;
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));

  this->storage.BeginTransaction ();
  this->storage.ReleaseUndoData (this->hash1);
  this->storage.CommitTransaction ();
  EXPECT_FALSE (this->storage.GetUndoData (this->hash1, undo));
}

TYPED_TEST_P (PruningStorageTests, PruneUndoData)
{
  this->storage.BeginTransaction ();
  this->storage.AddUndoData (this->hash1, 42, this->undo1);
  this->storage.AddUndoData (this->hash2, 43, this->undo2);
  this->storage.CommitTransaction ();

  UndoData undo;
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_TRUE (this->storage.GetUndoData (this->hash2, undo));

  this->storage.BeginTransaction ();
  this->storage.PruneUndoData (41);
  this->storage.CommitTransaction ();
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_TRUE (this->storage.GetUndoData (this->hash2, undo));

  this->storage.BeginTransaction ();
  this->storage.PruneUndoData (42);
  this->storage.CommitTransaction ();
  EXPECT_FALSE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_TRUE (this->storage.GetUndoData (this->hash2, undo));

  /* Add back hash1, so that we can test pruning of multiple elements.  */
  this->storage.BeginTransaction ();
  this->storage.AddUndoData (this->hash1, 42, this->undo1);
  this->storage.CommitTransaction ();
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_TRUE (this->storage.GetUndoData (this->hash2, undo));

  this->storage.BeginTransaction ();
  this->storage.PruneUndoData (43);
  this->storage.CommitTransaction ();
  EXPECT_FALSE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_FALSE (this->storage.GetUndoData (this->hash2, undo));
}

TYPED_TEST_P (PruningStorageTests, MultibyteHeight)
{
  /* In this test, we store undo data for heights that require multiple
     bytes and bytes larger than 127 to encode.  This verifies that storing
     works fine for cases where the height is encoded into bytes manually.  */

  const unsigned height = (42 << 24) + 250;

  this->storage.BeginTransaction ();
  this->storage.AddUndoData (this->hash1, height, this->undo1);
  this->storage.AddUndoData (this->hash2, height + 1, this->undo2);
  this->storage.CommitTransaction ();

  UndoData undo;
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_TRUE (this->storage.GetUndoData (this->hash2, undo));

  this->storage.BeginTransaction ();
  this->storage.PruneUndoData (height);
  this->storage.CommitTransaction ();
  EXPECT_FALSE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_TRUE (this->storage.GetUndoData (this->hash2, undo));
}

REGISTER_TYPED_TEST_CASE_P (PruningStorageTests,
                            ReleaseUndoData, PruneUndoData, MultibyteHeight);

/**
 * Tests the transaction mechanism in a storage implementation.  This can
 * be applied to every implementation that has a fully working mechanism
 * to create atomic transactions and commit or roll them back.
 */
template <typename T>
  using TransactingStorageTests = BasicStorageTests<T>;

TYPED_TEST_CASE_P (TransactingStorageTests);

TYPED_TEST_P (TransactingStorageTests, Commit)
{
  this->storage.BeginTransaction ();
  this->storage.SetCurrentGameState (this->hash1, this->state1);
  this->storage.AddUndoData (this->hash1, 10, this->undo1);
  this->storage.CommitTransaction ();

  uint256 hash;
  ASSERT_TRUE (this->storage.GetCurrentBlockHash (hash));
  EXPECT_EQ (hash, this->hash1);
  EXPECT_EQ (this->storage.GetCurrentGameState (), this->state1);

  UndoData undo;
  EXPECT_TRUE (this->storage.GetUndoData (this->hash1, undo));
  EXPECT_EQ (undo, this->undo1);
}

TYPED_TEST_P (TransactingStorageTests, Rollback)
{
  this->storage.BeginTransaction ();
  this->storage.SetCurrentGameState (this->hash1, this->state1);
  this->storage.CommitTransaction ();
  EXPECT_EQ (this->storage.GetCurrentGameState (), this->state1);

  this->storage.BeginTransaction ();
  this->storage.SetCurrentGameState (this->hash1, this->state2);
  this->storage.RollbackTransaction ();
  EXPECT_EQ (this->storage.GetCurrentGameState (), this->state1);
}

REGISTER_TYPED_TEST_CASE_P (TransactingStorageTests, Commit, Rollback);

} // namespace spacexpanse

#endif // XAYAGAME_STORAGE_TESTS_HPP
