//===--------------------- JSON.cpp -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "JSON.h"

// C includes
#include <cassert>
#include <climits>

// C++ includes
#include "StringConvert.h"
#include <iomanip>
#include <sstream>

std::string JSONString::json_string_quote_metachars(const std::string &s) {
  if (s.find('"') == std::string::npos)
    return s;

  std::string output;
  const size_t s_size = s.size();
  const char *s_chars = s.c_str();
  for (size_t i = 0; i < s_size; i++) {
    unsigned char ch = *(s_chars + i);
    if (ch == '"') {
      output.push_back('\\');
    }
    output.push_back(ch);
  }
  return output;
}

JSONString::JSONString() : JSONValue(JSONValue::Kind::String), m_data() {}

JSONString::JSONString(const char *s)
    : JSONValue(JSONValue::Kind::String), m_data(s ? s : "") {}

JSONString::JSONString(const std::string &s)
    : JSONValue(JSONValue::Kind::String), m_data(s) {}

void JSONString::Write(std::ostream &s) {
  s << "\"" << json_string_quote_metachars(m_data) << "\"";
}

uint64_t JSONNumber::GetAsUnsigned() const {
  switch (m_data_type) {
  case DataType::Unsigned:
    return m_data.m_unsigned;
  case DataType::Signed:
    return (uint64_t)m_data.m_signed;
  case DataType::Double:
    return (uint64_t)m_data.m_double;
  }
}

int64_t JSONNumber::GetAsSigned() const {
  switch (m_data_type) {
  case DataType::Unsigned:
    return (int64_t)m_data.m_unsigned;
  case DataType::Signed:
    return m_data.m_signed;
  case DataType::Double:
    return (int64_t)m_data.m_double;
  }
}

double JSONNumber::GetAsDouble() const {
  switch (m_data_type) {
  case DataType::Unsigned:
    return (double)m_data.m_unsigned;
  case DataType::Signed:
    return (double)m_data.m_signed;
  case DataType::Double:
    return m_data.m_double;
  }
}

void JSONNumber::Write(std::ostream &s) {
  switch (m_data_type) {
  case DataType::Unsigned:
    s << m_data.m_unsigned;
    break;
  case DataType::Signed:
    s << m_data.m_signed;
    break;
  case DataType::Double:
    // Set max precision to emulate %g.
    s << std::setprecision(std::numeric_limits<double>::digits10 + 1);
    s << m_data.m_double;
    break;
  }
}

JSONTrue::JSONTrue() : JSONValue(JSONValue::Kind::True) {}

void JSONTrue::Write(std::ostream &s) { s << "true"; }

JSONFalse::JSONFalse() : JSONValue(JSONValue::Kind::False) {}

void JSONFalse::Write(std::ostream &s) { s << "false"; }

JSONNull::JSONNull() : JSONValue(JSONValue::Kind::Null) {}

void JSONNull::Write(std::ostream &s) { s << "null"; }

JSONObject::JSONObject() : JSONValue(JSONValue::Kind::Object) {}

void JSONObject::Write(std::ostream &s) {
  bool first = true;
  s << '{';
  auto iter = m_elements.begin(), end = m_elements.end();
  for (; iter != end; iter++) {
    if (first)
      first = false;
    else
      s << ',';
    JSONString key(iter->first);
    JSONValue::SP value(iter->second);
    key.Write(s);
    s << ':';
    value->Write(s);
  }
  s << '}';
}

bool JSONObject::SetObject(const std::string &key, JSONValue::SP value) {
  if (key.empty() || nullptr == value.get())
    return false;
  m_elements[key] = value;
  return true;
}

JSONValue::SP JSONObject::GetObject(const std::string &key) const {
  auto iter = m_elements.find(key), end = m_elements.end();
  if (iter == end)
    return JSONValue::SP();
  return iter->second;
}

bool JSONObject::GetObjectAsBool(const std::string &key, bool &value) const {
  auto value_sp = GetObject(key);
  if (!value_sp) {
    // The given key doesn't exist, so we have no value.
    return false;
  }

  if (JSONTrue::classof(value_sp.get())) {
    // We have the value, and it is true.
    value = true;
    return true;
  } else if (JSONFalse::classof(value_sp.get())) {
    // We have the value, and it is false.
    value = false;
    return true;
  } else {
    // We don't have a valid bool value for the given key.
    return false;
  }
}

bool JSONObject::GetObjectAsString(const std::string &key,
                                   std::string &value) const {
  auto value_sp = GetObject(key);
  if (!value_sp) {
    // The given key doesn't exist, so we have no value.
    return false;
  }

  if (!JSONString::classof(value_sp.get()))
    return false;

  value = static_cast<JSONString *>(value_sp.get())->GetData();
  return true;
}

JSONArray::JSONArray() : JSONValue(JSONValue::Kind::Array) {}

void JSONArray::Write(std::ostream &s) {
  bool first = true;
  s << '[';
  auto iter = m_elements.begin(), end = m_elements.end();
  for (; iter != end; iter++) {
    if (first)
      first = false;
    else
      s << ',';
    (*iter)->Write(s);
  }
  s << ']';
}

bool JSONArray::SetObject(Index i, JSONValue::SP value) {
  if (value.get() == nullptr)
    return false;
  if (i < m_elements.size()) {
    m_elements[i] = value;
    return true;
  }
  if (i == m_elements.size()) {
    m_elements.push_back(value);
    return true;
  }
  return false;
}

bool JSONArray::AppendObject(JSONValue::SP value) {
  if (value.get() == nullptr)
    return false;
  m_elements.push_back(value);
  return true;
}

JSONValue::SP JSONArray::GetObject(Index i) {
  if (i < m_elements.size())
    return m_elements[i];
  return JSONValue::SP();
}

JSONArray::Size JSONArray::GetNumElements() { return m_elements.size(); }

JSONParser::JSONParser(const char *cstr) : StdStringExtractor(cstr) {}

JSONParser::Token JSONParser::GetToken(std::string &value) {
  std::ostringstream error;

  value.clear();
  SkipSpaces();
  const uint64_t start_index = m_index;
  const char ch = GetChar();
  switch (ch) {
  case '{':
    return Token::ObjectStart;
  case '}':
    return Token::ObjectEnd;
  case '[':
    return Token::ArrayStart;
  case ']':
    return Token::ArrayEnd;
  case ',':
    return Token::Comma;
  case ':':
    return Token::Colon;
  case '\0':
    return Token::EndOfFile;
  case 't':
    if (GetChar() == 'r')
      if (GetChar() == 'u')
        if (GetChar() == 'e')
          return Token::True;
    break;

  case 'f':
    if (GetChar() == 'a')
      if (GetChar() == 'l')
        if (GetChar() == 's')
          if (GetChar() == 'e')
            return Token::False;
    break;

  case 'n':
    if (GetChar() == 'u')
      if (GetChar() == 'l')
        if (GetChar() == 'l')
          return Token::Null;
    break;

  case '"': {
    while (true) {
      bool was_escaped = false;
      int escaped_ch = GetEscapedChar(was_escaped);
      if (escaped_ch == -1) {
        error << "error: an error occurred getting a character from offset "
              << start_index;
        value = error.str();
        return Token::Status;

      } else {
        const bool is_end_quote = escaped_ch == '"';
        const bool is_null = escaped_ch == 0;
        if (was_escaped || (!is_end_quote && !is_null)) {
          if (CHAR_MIN <= escaped_ch && escaped_ch <= CHAR_MAX) {
            value.append(1, (char)escaped_ch);
          } else {
            error << "error: wide character support is needed for unicode "
                     "character 0x"
                  << std::setprecision(4) << std::hex << escaped_ch;
            error << " at offset " << start_index;
            value = error.str();
            return Token::Status;
          }
        } else if (is_end_quote) {
          return Token::String;
        } else if (is_null) {
          value = "error: missing end quote for string";
          return Token::Status;
        }
      }
    }
  } break;

  case '-':
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': {
    bool done = false;
    bool got_decimal_point = false;
    uint64_t exp_index = 0;
    bool got_int_digits = (ch >= '0') && (ch <= '9');
    bool got_frac_digits = false;
    bool got_exp_digits = false;
    while (!done) {
      const char next_ch = PeekChar();
      switch (next_ch) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        if (exp_index != 0) {
          got_exp_digits = true;
        } else if (got_decimal_point) {
          got_frac_digits = true;
        } else {
          got_int_digits = true;
        }
        ++m_index; // Skip this character
        break;

      case '.':
        if (got_decimal_point) {
          error << "error: extra decimal point found at offset " << start_index;
          value = error.str();
          return Token::Status;
        } else {
          got_decimal_point = true;
          ++m_index; // Skip this character
        }
        break;

      case 'e':
      case 'E':
        if (exp_index != 0) {
          error << "error: extra exponent character found at offset "
                << start_index;
          value = error.str();
          return Token::Status;
        } else {
          exp_index = m_index;
          ++m_index; // Skip this character
        }
        break;

      case '+':
      case '-':
        // The '+' and '-' can only come after an exponent character...
        if (exp_index == m_index - 1) {
          ++m_index; // Skip the exponent sign character
        } else {
          error << "error: unexpected " << next_ch << " character at offset "
                << start_index;
          value = error.str();
          return Token::Status;
        }
        break;

      default:
        done = true;
        break;
      }
    }

    if (m_index > start_index) {
      value = m_packet.substr(start_index, m_index - start_index);
      if (got_decimal_point) {
        if (exp_index != 0) {
          // We have an exponent, make sure we got exponent digits
          if (got_exp_digits) {
            return Token::Float;
          } else {
            error << "error: got exponent character but no exponent digits at "
                     "offset in float value \""
                  << value << "\"";
            value = error.str();
            return Token::Status;
          }
        } else {
          // No exponent, but we need at least one decimal after the decimal
          // point
          if (got_frac_digits) {
            return Token::Float;
          } else {
            error << "error: no digits after decimal point \"" << value << "\"";
            value = error.str();
            return Token::Status;
          }
        }
      } else {
        // No decimal point
        if (got_int_digits) {
          // We need at least some integer digits to make an integer
          return Token::Integer;
        } else {
          error << "error: no digits negate sign \"" << value << "\"";
          value = error.str();
          return Token::Status;
        }
      }
    } else {
      error << "error: invalid number found at offset " << start_index;
      value = error.str();
      return Token::Status;
    }
  } break;
  default:
    break;
  }
  error << "error: failed to parse token at offset " << start_index
        << " (around character '" << ch << "')";
  value = error.str();
  return Token::Status;
}

int JSONParser::GetEscapedChar(bool &was_escaped) {
  was_escaped = false;
  const char ch = GetChar();
  if (ch == '\\') {
    was_escaped = true;
    const char ch2 = GetChar();
    switch (ch2) {
    case '"':
    case '\\':
    case '/':
    default:
      break;

    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    case 'u': {
      const int hi_byte = DecodeHexU8();
      const int lo_byte = DecodeHexU8();
      if (hi_byte >= 0 && lo_byte >= 0)
        return hi_byte << 8 | lo_byte;
      return -1;
    } break;
    }
    return ch2;
  }
  return ch;
}

JSONValue::SP JSONParser::ParseJSONObject() {
  // The "JSONParser::Token::ObjectStart" token should have already been
  // consumed
  // by the time this function is called
  std::unique_ptr<JSONObject> dict_up(new JSONObject());

  std::string value;
  std::string key;
  while (true) {
    JSONParser::Token token = GetToken(value);

    if (token == JSONParser::Token::String) {
      key.swap(value);
      token = GetToken(value);
      if (token == JSONParser::Token::Colon) {
        JSONValue::SP value_sp = ParseJSONValue();
        if (value_sp)
          dict_up->SetObject(key, value_sp);
        else
          break;
      }
    } else if (token == JSONParser::Token::ObjectEnd) {
      return JSONValue::SP(dict_up.release());
    } else if (token == JSONParser::Token::Comma) {
      continue;
    } else {
      break;
    }
  }
  return JSONValue::SP();
}

JSONValue::SP JSONParser::ParseJSONArray() {
  // The "JSONParser::Token::ObjectStart" token should have already been
  // consumed
  // by the time this function is called
  std::unique_ptr<JSONArray> array_up(new JSONArray());

  std::string value;
  std::string key;
  while (true) {
    JSONParser::Token token = GetToken(value);
    if (token == JSONParser::Token::ArrayEnd)
      return JSONValue::SP(array_up.release());
    JSONValue::SP value_sp = ParseJSONValue(value, token);
    if (value_sp)
      array_up->AppendObject(value_sp);
    else
      break;

    token = GetToken(value);
    if (token == JSONParser::Token::Comma) {
      continue;
    } else if (token == JSONParser::Token::ArrayEnd) {
      return JSONValue::SP(array_up.release());
    } else {
      break;
    }
  }
  return JSONValue::SP();
}

JSONValue::SP JSONParser::ParseJSONValue() {
  std::string value;
  const JSONParser::Token token = GetToken(value);
  return ParseJSONValue(value, token);
}

JSONValue::SP JSONParser::ParseJSONValue(const std::string &value,
                                         const Token &token) {
  switch (token) {
  case JSONParser::Token::ObjectStart:
    return ParseJSONObject();

  case JSONParser::Token::ArrayStart:
    return ParseJSONArray();

  case JSONParser::Token::Integer: {
    if (value.front() == '-') {
      bool success = false;
      int64_t sval = StringConvert::ToSInt64(value.c_str(), 0, 0, &success);
      if (success)
        return JSONValue::SP(new JSONNumber(sval));
    } else {
      bool success = false;
      uint64_t uval = StringConvert::ToUInt64(value.c_str(), 0, 0, &success);
      if (success)
        return JSONValue::SP(new JSONNumber(uval));
    }
  } break;

  case JSONParser::Token::Float: {
    bool success = false;
    double val = StringConvert::ToDouble(value.c_str(), 0.0, &success);
    if (success)
      return JSONValue::SP(new JSONNumber(val));
  } break;

  case JSONParser::Token::String:
    return JSONValue::SP(new JSONString(value));

  case JSONParser::Token::True:
    return JSONValue::SP(new JSONTrue());

  case JSONParser::Token::False:
    return JSONValue::SP(new JSONFalse());

  case JSONParser::Token::Null:
    return JSONValue::SP(new JSONNull());

  default:
    break;
  }
  return JSONValue::SP();
}
