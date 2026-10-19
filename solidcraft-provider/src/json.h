// A small JSON reader and writer for the SolidCraft geometry provider protocol: one object per
// line, holding strings, numbers, booleans, null, arrays and objects. Enough for the protocol,
// nothing more.
//
// Part of solidcraft-occt-provider. Copyright the contributors. Distributed under the GNU Lesser
// General Public License version 2.1 with the Open CASCADE exception, as Open CASCADE Technology.

#ifndef SOLIDCRAFT_PROVIDER_JSON_H
#define SOLIDCRAFT_PROVIDER_JSON_H

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace json
{

struct Value
{
  enum class Kind
  {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
  };

  Kind                         kind = Kind::Null;
  bool                         boolean = false;
  double                       number = 0.0;
  std::string                  text;
  std::vector<Value>           items;
  std::map<std::string, Value> fields;

  //! The field `key` of an object, or null.
  const Value& operator[](const std::string& key) const;
  //! The text of a string, or `fallback`.
  std::string str(const std::string& fallback = std::string()) const;
  //! The number, or `fallback`.
  double num(double fallback = 0.0) const;
};

//! Read one JSON value from `text` (surrounding whitespace allowed). Throws std::runtime_error.
Value parse(const std::string& text);

//! `text` as a JSON string, quotes included.
std::string quote(const std::string& text);

} // namespace json

#endif
