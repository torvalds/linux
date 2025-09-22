//===---------------------JSON.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_JSON_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_JSON_H

#include "StdStringExtractor.h"

// C includes
#include <cinttypes>
#include <cstdint>

// C++ includes
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

class JSONValue {
public:
  virtual void Write(std::ostream &s) = 0;

  typedef std::shared_ptr<JSONValue> SP;

  enum class Kind { String, Number, True, False, Null, Object, Array };

  JSONValue(Kind k) : m_kind(k) {}

  Kind GetKind() const { return m_kind; }

  virtual ~JSONValue() = default;

private:
  const Kind m_kind;
};

class JSONString : public JSONValue {
public:
  JSONString();
  JSONString(const char *s);
  JSONString(const std::string &s);

  JSONString(const JSONString &s) = delete;
  JSONString &operator=(const JSONString &s) = delete;

  void Write(std::ostream &s) override;

  typedef std::shared_ptr<JSONString> SP;

  std::string GetData() { return m_data; }

  static bool classof(const JSONValue *V) {
    return V->GetKind() == JSONValue::Kind::String;
  }

  ~JSONString() override = default;

private:
  static std::string json_string_quote_metachars(const std::string &);

  std::string m_data;
};

class JSONNumber : public JSONValue {
public:
  typedef std::shared_ptr<JSONNumber> SP;

  // We create a constructor for all integer and floating point type with using
  // templates and
  // SFINAE to avoid having ambiguous overloads because of the implicit type
  // promotion. If we
  // would have constructors only with int64_t, uint64_t and double types then
  // constructing a
  // JSONNumber from an int32_t (or any other similar type) would fail to
  // compile.

  template <typename T, typename std::enable_if<
                            std::is_integral<T>::value &&
                            std::is_unsigned<T>::value>::type * = nullptr>
  explicit JSONNumber(T u)
      : JSONValue(JSONValue::Kind::Number), m_data_type(DataType::Unsigned) {
    m_data.m_unsigned = u;
  }

  template <typename T,
            typename std::enable_if<std::is_integral<T>::value &&
                                    std::is_signed<T>::value>::type * = nullptr>
  explicit JSONNumber(T s)
      : JSONValue(JSONValue::Kind::Number), m_data_type(DataType::Signed) {
    m_data.m_signed = s;
  }

  template <typename T, typename std::enable_if<
                            std::is_floating_point<T>::value>::type * = nullptr>
  explicit JSONNumber(T d)
      : JSONValue(JSONValue::Kind::Number), m_data_type(DataType::Double) {
    m_data.m_double = d;
  }

  ~JSONNumber() override = default;

  JSONNumber(const JSONNumber &s) = delete;
  JSONNumber &operator=(const JSONNumber &s) = delete;

  void Write(std::ostream &s) override;

  uint64_t GetAsUnsigned() const;

  int64_t GetAsSigned() const;

  double GetAsDouble() const;

  static bool classof(const JSONValue *V) {
    return V->GetKind() == JSONValue::Kind::Number;
  }

private:
  enum class DataType : uint8_t { Unsigned, Signed, Double } m_data_type;

  union {
    uint64_t m_unsigned;
    int64_t m_signed;
    double m_double;
  } m_data;
};

class JSONTrue : public JSONValue {
public:
  JSONTrue();

  JSONTrue(const JSONTrue &s) = delete;
  JSONTrue &operator=(const JSONTrue &s) = delete;

  void Write(std::ostream &s) override;

  typedef std::shared_ptr<JSONTrue> SP;

  static bool classof(const JSONValue *V) {
    return V->GetKind() == JSONValue::Kind::True;
  }

  ~JSONTrue() override = default;
};

class JSONFalse : public JSONValue {
public:
  JSONFalse();

  JSONFalse(const JSONFalse &s) = delete;
  JSONFalse &operator=(const JSONFalse &s) = delete;

  void Write(std::ostream &s) override;

  typedef std::shared_ptr<JSONFalse> SP;

  static bool classof(const JSONValue *V) {
    return V->GetKind() == JSONValue::Kind::False;
  }

  ~JSONFalse() override = default;
};

class JSONNull : public JSONValue {
public:
  JSONNull();

  JSONNull(const JSONNull &s) = delete;
  JSONNull &operator=(const JSONNull &s) = delete;

  void Write(std::ostream &s) override;

  typedef std::shared_ptr<JSONNull> SP;

  static bool classof(const JSONValue *V) {
    return V->GetKind() == JSONValue::Kind::Null;
  }

  ~JSONNull() override = default;
};

class JSONObject : public JSONValue {
public:
  JSONObject();

  JSONObject(const JSONObject &s) = delete;
  JSONObject &operator=(const JSONObject &s) = delete;

  void Write(std::ostream &s) override;

  typedef std::shared_ptr<JSONObject> SP;

  static bool classof(const JSONValue *V) {
    return V->GetKind() == JSONValue::Kind::Object;
  }

  bool SetObject(const std::string &key, JSONValue::SP value);

  JSONValue::SP GetObject(const std::string &key) const;

  /// Return keyed value as bool
  ///
  /// \param[in] key
  ///     The value of the key to lookup
  ///
  /// \param[out] value
  ///     The value of the key as a bool.  Undefined if the key doesn't
  ///     exist or if the key is not either true or false.
  ///
  /// \return
  ///     true if the key existed as was a bool value; false otherwise.
  ///     Note the return value is *not* the value of the bool, use
  ///     \b value for that.
  bool GetObjectAsBool(const std::string &key, bool &value) const;

  bool GetObjectAsString(const std::string &key, std::string &value) const;

  ~JSONObject() override = default;

private:
  typedef std::map<std::string, JSONValue::SP> Map;
  typedef Map::iterator Iterator;
  Map m_elements;
};

class JSONArray : public JSONValue {
public:
  JSONArray();

  JSONArray(const JSONArray &s) = delete;
  JSONArray &operator=(const JSONArray &s) = delete;

  void Write(std::ostream &s) override;

  typedef std::shared_ptr<JSONArray> SP;

  static bool classof(const JSONValue *V) {
    return V->GetKind() == JSONValue::Kind::Array;
  }

private:
  typedef std::vector<JSONValue::SP> Vector;
  typedef Vector::iterator Iterator;
  typedef Vector::size_type Index;
  typedef Vector::size_type Size;

public:
  bool SetObject(Index i, JSONValue::SP value);

  bool AppendObject(JSONValue::SP value);

  JSONValue::SP GetObject(Index i);

  Size GetNumElements();

  ~JSONArray() override = default;

  Vector m_elements;
};

class JSONParser : public StdStringExtractor {
public:
  enum Token {
    Invalid,
    Status,
    ObjectStart,
    ObjectEnd,
    ArrayStart,
    ArrayEnd,
    Comma,
    Colon,
    String,
    Integer,
    Float,
    True,
    False,
    Null,
    EndOfFile
  };

  JSONParser(const char *cstr);

  int GetEscapedChar(bool &was_escaped);

  Token GetToken(std::string &value);

  JSONValue::SP ParseJSONValue();

protected:
  JSONValue::SP ParseJSONValue(const std::string &value, const Token &token);

  JSONValue::SP ParseJSONObject();

  JSONValue::SP ParseJSONArray();
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_JSON_H
