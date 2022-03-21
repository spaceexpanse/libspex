// Copyright (C) 2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NONFUNGIBLE_SCHEMA_HPP
#define NONFUNGIBLE_SCHEMA_HPP

#include "xgame/sqlitestorage.hpp"

namespace nf
{

/**
 * Sets up the database schema (if it is not already present) on the given
 * SQLite connection.
 */
void SetupDatabaseSchema (spacexpanse::SQLiteDatabase& db);

} // namespace xid

#endif // NONFUNGIBLE_SCHEMA_HPP
