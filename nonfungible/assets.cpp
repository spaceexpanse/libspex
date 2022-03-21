// Copyright (C) 2020 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets.hpp"

#include "xutil/jsonutils.hpp"

namespace nf
{

Json::Value
AmountToJson (const Amount n)
{
  return static_cast<Json::Int64> (n);
}

bool
AmountFromJson (const Json::Value& val, Amount& n)
{
  if (!spacexpanse::IsIntegerValue (val) || !val.isInt64 ())
    return false;

  n = val.asInt64 ();
  return n >= 0 && n <= MAX_AMOUNT;
}

void
Asset::BindToParams (spacexpanse::SQLiteDatabase::Statement& stmt,
                     const int indMinter, const int indName) const
{
  stmt.Bind (indMinter, minter);
  stmt.Bind (indName, name);
}

Json::Value
Asset::ToJson () const
{
  Json::Value res(Json::objectValue);
  res["m"] = minter;
  res["a"] = name;
  return res;
}

Asset
Asset::FromColumns (const spacexpanse::SQLiteDatabase::Statement& stmt,
                    const int indMinter, const int indName)
{
  return Asset (stmt.Get<std::string> (indMinter),
                stmt.Get<std::string> (indName));
}

bool
Asset::IsValidName (const std::string& nm)
{
  for (const unsigned char c : nm)
    if (c < 0x20)
      return false;

  return true;
}

namespace
{

/**
 * Returns true and extracts the result as string if the given JSON value
 * is a string and does not contain any non-printable characters.
 */
bool
GetPrintableString (const Json::Value& val, std::string& res)
{
  if (!val.isString ())
    return false;

  res = val.asString ();
  return Asset::IsValidName (res);
}

} // anonymous namespace

bool
Asset::FromJson (const Json::Value& val)
{
  if (!val.isObject () || val.size () != 2)
    return false;

  if (!GetPrintableString (val["m"], minter))
    return false;
  if (!GetPrintableString (val["a"], name))
    return false;

  return true;
}

std::ostream&
operator<< (std::ostream& out, const Asset& a)
{
  out << a.minter << "/" << a.name;
  return out;
}

} // namespace nf
