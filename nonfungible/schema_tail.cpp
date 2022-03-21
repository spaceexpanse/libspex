)";

} // anonymous namespace

void
SetupDatabaseSchema (spacexpanse::SQLiteDatabase& db)
{
  db.Execute (SCHEMA_SQL);
}

} // namespace nf
