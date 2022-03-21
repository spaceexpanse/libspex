// Copyright (C) 2018 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionmanager.hpp"

#include <glog/logging.h>

namespace spacexpanse
{
namespace internal
{

TransactionManager::~TransactionManager ()
{
  /* The code in Game should be written to make sure that all transactions
     are either committed or aborted using RAII, so that it should never
     happen that a transaction stays "in progress" until the manager instance
     itself is destroyed (together with Game).  */
  CHECK (!inTransaction);

  Flush ();
}

void
TransactionManager::Flush ()
{
  CHECK (!inTransaction);
  CHECK (!commitFailed);

  LOG (INFO)
      << "Committing " << batchedCommits
      << " batched transactions to the underlying storage instance";

  if (batchedCommits > 0)
    {
      if (storage != nullptr)
        try
          {
            storage->CommitTransaction ();
          }
        catch (...)
          {
            commitFailed = true;
            throw;
          }
      batchedCommits = 0;
    }
}

void
TransactionManager::SetStorage (StorageInterface& s)
{
  Flush ();
  storage = &s;
}

void
TransactionManager::SetBatchSize (const unsigned sz)
{
  CHECK (sz >= 1);
  batchSize = sz;
  LOG (INFO) << "Set batch size for TransactionManager to " << batchSize;

  if (batchedCommits >= batchSize)
    {
      LOG (INFO)
          << "We have " << batchedCommits
          << " batched transactions, trying to commit the batch now";
      if (inTransaction)
        LOG (INFO) << "There is a pending transaction, not committing";
      else
        Flush ();
    }
}

void
TransactionManager::BeginTransaction ()
{
  CHECK (storage != nullptr);
  CHECK (!commitFailed);

  CHECK (!inTransaction);
  inTransaction = true;

  VLOG (1) << "Starting new transaction on the TransactionManager";

  if (batchedCommits == 0)
    {
      LOG (INFO) << "No pending commits, starting new underlying transaction";
      storage->BeginTransaction ();
    }
}

void
TransactionManager::CommitTransaction ()
{
  CHECK (storage != nullptr);
  CHECK (!commitFailed);

  CHECK (inTransaction);
  inTransaction = false;

  ++batchedCommits;
  VLOG (1)
      << "Committing current transaction on TransactionManager, now we have "
      << batchedCommits << " batched transactions";

  if (batchedCommits >= batchSize)
    Flush ();
}

void
TransactionManager::RollbackTransaction ()
{
  CHECK (storage != nullptr);

  CHECK (inTransaction || commitFailed);
  inTransaction = false;
  commitFailed = false;

  LOG (INFO)
      << "Rolling back current and " << batchedCommits
      << " batched transactions";

  storage->RollbackTransaction ();
  batchedCommits = 0;
}

void
TransactionManager::TryAbortTransaction ()
{
  CHECK (storage != nullptr);

  if (inTransaction || commitFailed || batchedCommits > 0)
    {
      LOG (INFO) << "Aborting current transaction and batched commits";
      storage->RollbackTransaction ();
    }

  inTransaction = false;
  commitFailed = false;
  batchedCommits = 0;
}

ActiveTransaction::ActiveTransaction (TransactionManager& m)
  : manager(m)
{
  manager.BeginTransaction ();
}

void
ActiveTransaction::Commit ()
{
  CHECK (!committed);
  manager.CommitTransaction ();
  committed = true;
}

ActiveTransaction::~ActiveTransaction ()
{
  if (!committed)
    manager.RollbackTransaction ();
}

} // namespace internal
} // namespace spacexpanse
