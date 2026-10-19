// Part of solidcraft-occt-provider. Copyright the contributors. Distributed under the GNU Lesser
// General Public License version 2.1 with the Open CASCADE exception, as Open CASCADE Technology.

#include "json.h"

#include <cstdio>
#include <cstdlib>

namespace json
{

namespace
{

// Deeper nesting than any request has is refused rather than followed.
constexpr int MaxDepth = 64;

struct Reader
{
  const std::string& s;
  size_t             i = 0;

  explicit Reader(const std::string& text)
      : s(text)
  {
  }

  [[noreturn]] void fail(const char* what) const
  {
    throw std::runtime_error(std::string("bad JSON at byte ") + std::to_string(i) + ": " + what);
  }

  void space()
  {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
      ++i;
  }

  bool take(char c)
  {
    space();
    if (i < s.size() && s[i] == c)
    {
      ++i;
      return true;
    }
    return false;
  }

  void expect(char c)
  {
    if (!take(c))
      fail("unexpected character");
  }

  void word(const char* w)
  {
    for (const char* p = w; *p != '\0'; ++p, ++i)
      if (i >= s.size() || s[i] != *p)
        fail("unknown word");
  }

  static void utf8(std::string& out, unsigned long c)
  {
    if (c < 0x80)
      out += static_cast<char>(c);
    else if (c < 0x800)
    {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else
    {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }

  unsigned long hex4()
  {
    if (i + 4 > s.size())
      fail("short \\u escape");
    unsigned long c = 0;
    for (int k = 0; k < 4; ++k, ++i)
    {
      const char h = s[i];
      c <<= 4;
      if (h >= '0' && h <= '9')
        c |= static_cast<unsigned long>(h - '0');
      else if (h >= 'a' && h <= 'f')
        c |= static_cast<unsigned long>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F')
        c |= static_cast<unsigned long>(h - 'A' + 10);
      else
        fail("bad \\u escape");
    }
    return c;
  }

  std::string string()
  {
    expect('"');
    std::string out;
    while (true)
    {
      if (i >= s.size())
        fail("unterminated string");
      const char c = s[i++];
      if (c == '"')
        return out;
      if (c != '\\')
      {
        out += c;
        continue;
      }
      if (i >= s.size())
        fail("unterminated escape");
      const char e = s[i++];
      switch (e)
      {
        case '"':
        case '\\':
        case '/':
          out += e;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          unsigned long c1 = hex4();
          if (c1 >= 0xD800 && c1 < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u')
          {
            i += 2;
            const unsigned long c2 = hex4();
            c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
          }
          utf8(out, c1);
          break;
        }
        default:
          fail("unknown escape");
      }
    }
  }

  Value value(int depth)
  {
    if (depth > MaxDepth)
      fail("nested too deeply");
    space();
    if (i >= s.size())
      fail("missing value");
    Value v;
    const char c = s[i];
    if (c == '{')
    {
      ++i;
      v.kind = Value::Kind::Object;
      if (take('}'))
        return v;
      do
      {
        space();
        const std::string key = string();
        expect(':');
        v.fields[key] = value(depth + 1);
      } while (take(','));
      expect('}');
    }
    else if (c == '[')
    {
      ++i;
      v.kind = Value::Kind::Array;
      if (take(']'))
        return v;
      do
        v.items.push_back(value(depth + 1));
      while (take(','));
      expect(']');
    }
    else if (c == '"')
    {
      v.kind = Value::Kind::String;
      v.text = string();
    }
    else if (c == 't')
    {
      word("true");
      v.kind    = Value::Kind::Bool;
      v.boolean = true;
    }
    else if (c == 'f')
    {
      word("false");
      v.kind = Value::Kind::Bool;
    }
    else if (c == 'n')
      word("null");
    else
    {
      const char* start = s.c_str() + i;
      char*       end   = nullptr;
      v.number          = std::strtod(start, &end);
      if (end == start)
        fail("bad number");
      i += static_cast<size_t>(end - start);
      v.kind = Value::Kind::Number;
    }
    return v;
  }
};

const Value& null_value()
{
  static const Value v;
  return v;
}

} // namespace

const Value& Value::operator[](const std::string& key) const
{
  const auto it = fields.find(key);
  return it == fields.end() ? null_value() : it->second;
}

std::string Value::str(const std::string& fallback) const
{
  return kind == Kind::String ? text : fallback;
}

double Value::num(double fallback) const
{
  return kind == Kind::Number ? number : fallback;
}

Value parse(const std::string& text)
{
  Reader r(text);
  Value  v = r.value(0);
  r.space();
  if (r.i != text.size())
    r.fail("text after the value");
  return v;
}

std::string quote(const std::string& text)
{
  std::string out;
  out.reserve(text.size() + text.size() / 16 + 2);
  out += '"';
  for (const char c : text)
  {
    switch (c)
    {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20)
        {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buf;
        }
        else
          out += c;
    }
  }
  out += '"';
  return out;
}

} // namespace json
