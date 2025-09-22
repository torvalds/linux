//===-- JSONGenerator.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_JSONGENERATOR_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_JSONGENERATOR_H

#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/// \class JSONGenerator JSONGenerator.h
/// A class which can construct structured data for the sole purpose
/// of printing it in JSON format.
///
/// A stripped down version of lldb's StructuredData objects which are much
/// general purpose.  This variant is intended only for assembling information
/// and printing it as a JSON string.

class JSONGenerator {
public:
  class Object;
  class Array;
  class Integer;
  class Float;
  class Boolean;
  class String;
  class Dictionary;
  class Generic;

  typedef std::shared_ptr<Object> ObjectSP;
  typedef std::shared_ptr<Array> ArraySP;
  typedef std::shared_ptr<Integer> IntegerSP;
  typedef std::shared_ptr<Float> FloatSP;
  typedef std::shared_ptr<Boolean> BooleanSP;
  typedef std::shared_ptr<String> StringSP;
  typedef std::shared_ptr<Dictionary> DictionarySP;
  typedef std::shared_ptr<Generic> GenericSP;

  enum class Type {
    eTypeInvalid = -1,
    eTypeNull = 0,
    eTypeGeneric,
    eTypeArray,
    eTypeInteger,
    eTypeFloat,
    eTypeBoolean,
    eTypeString,
    eTypeDictionary
  };

  class Object : public std::enable_shared_from_this<Object> {
  public:
    Object(Type t = Type::eTypeInvalid) : m_type(t) {}

    virtual ~Object() {}

    virtual bool IsValid() const { return true; }

    virtual void Clear() { m_type = Type::eTypeInvalid; }

    Type GetType() const { return m_type; }

    void SetType(Type t) { m_type = t; }

    Array *GetAsArray() {
      if (m_type == Type::eTypeArray)
        return (Array *)this;
      return NULL;
    }

    Dictionary *GetAsDictionary() {
      if (m_type == Type::eTypeDictionary)
        return (Dictionary *)this;
      return NULL;
    }

    Integer *GetAsInteger() {
      if (m_type == Type::eTypeInteger)
        return (Integer *)this;
      return NULL;
    }

    Float *GetAsFloat() {
      if (m_type == Type::eTypeFloat)
        return (Float *)this;
      return NULL;
    }

    Boolean *GetAsBoolean() {
      if (m_type == Type::eTypeBoolean)
        return (Boolean *)this;
      return NULL;
    }

    String *GetAsString() {
      if (m_type == Type::eTypeString)
        return (String *)this;
      return NULL;
    }

    Generic *GetAsGeneric() {
      if (m_type == Type::eTypeGeneric)
        return (Generic *)this;
      return NULL;
    }

    virtual void Dump(std::ostream &s) const = 0;

    virtual void DumpBinaryEscaped(std::ostream &s) const = 0;

  private:
    Type m_type;
  };

  class Array : public Object {
  public:
    Array() : Object(Type::eTypeArray) {}

    virtual ~Array() {}

    void AddItem(ObjectSP item) { m_items.push_back(item); }

    void Dump(std::ostream &s) const override {
      s << "[";
      const size_t arrsize = m_items.size();
      for (size_t i = 0; i < arrsize; ++i) {
        m_items[i]->Dump(s);
        if (i + 1 < arrsize)
          s << ",";
      }
      s << "]";
    }

    void DumpBinaryEscaped(std::ostream &s) const override {
      s << "[";
      const size_t arrsize = m_items.size();
      for (size_t i = 0; i < arrsize; ++i) {
        m_items[i]->DumpBinaryEscaped(s);
        if (i + 1 < arrsize)
          s << ",";
      }
      s << "]";
    }

  protected:
    typedef std::vector<ObjectSP> collection;
    collection m_items;
  };

  class Integer : public Object {
  public:
    Integer(uint64_t value = 0) : Object(Type::eTypeInteger), m_value(value) {}

    virtual ~Integer() {}

    void SetValue(uint64_t value) { m_value = value; }

    void Dump(std::ostream &s) const override { s << m_value; }

    void DumpBinaryEscaped(std::ostream &s) const override { Dump(s); }

  protected:
    uint64_t m_value;
  };

  class Float : public Object {
  public:
    Float(double d = 0.0) : Object(Type::eTypeFloat), m_value(d) {}

    virtual ~Float() {}

    void SetValue(double value) { m_value = value; }

    void Dump(std::ostream &s) const override { s << m_value; }

    void DumpBinaryEscaped(std::ostream &s) const override { Dump(s); }

  protected:
    double m_value;
  };

  class Boolean : public Object {
  public:
    Boolean(bool b = false) : Object(Type::eTypeBoolean), m_value(b) {}

    virtual ~Boolean() {}

    void SetValue(bool value) { m_value = value; }

    void Dump(std::ostream &s) const override {
      if (m_value)
        s << "true";
      else
        s << "false";
    }

    void DumpBinaryEscaped(std::ostream &s) const override { Dump(s); }

  protected:
    bool m_value;
  };

  class String : public Object {
  public:
    String() : Object(Type::eTypeString), m_value() {}

    String(const std::string &s) : Object(Type::eTypeString), m_value(s) {}

    String(const std::string &&s) : Object(Type::eTypeString), m_value(s) {}

    void SetValue(const std::string &string) { m_value = string; }

    void Dump(std::ostream &s) const override {
      s << '"';
      const size_t strsize = m_value.size();
      for (size_t i = 0; i < strsize; ++i) {
        char ch = m_value[i];
        if (ch == '"')
          s << '\\';
        s << ch;
      }
      s << '"';
    }

    void DumpBinaryEscaped(std::ostream &s) const override {
      s << '"';
      const size_t strsize = m_value.size();
      for (size_t i = 0; i < strsize; ++i) {
        char ch = m_value[i];
        if (ch == '"')
          s << '\\';
        // gdb remote serial protocol binary escaping
        if (ch == '#' || ch == '$' || ch == '}' || ch == '*') {
          s << '}'; // 0x7d next character is escaped
          s << static_cast<char>(ch ^ 0x20);
        } else {
          s << ch;
        }
      }
      s << '"';
    }

  protected:
    std::string m_value;
  };

  class Dictionary : public Object {
  public:
    Dictionary() : Object(Type::eTypeDictionary), m_dict() {}

    virtual ~Dictionary() {}

    void AddItem(std::string key, ObjectSP value) {
      m_dict.push_back(Pair(key, value));
    }

    void AddIntegerItem(std::string key, uint64_t value) {
      AddItem(key, ObjectSP(new Integer(value)));
    }

    void AddFloatItem(std::string key, double value) {
      AddItem(key, ObjectSP(new Float(value)));
    }

    void AddStringItem(std::string key, std::string value) {
      AddItem(key, ObjectSP(new String(std::move(value))));
    }

    void AddBytesAsHexASCIIString(std::string key, const uint8_t *src,
                                  size_t src_len) {
      if (src && src_len) {
        std::ostringstream strm;
        for (size_t i = 0; i < src_len; i++)
          strm << std::setfill('0') << std::hex << std::right << std::setw(2)
               << ((uint32_t)(src[i]));
        AddItem(key, ObjectSP(new String(std::move(strm.str()))));
      } else {
        AddItem(key, ObjectSP(new String()));
      }
    }

    void AddBooleanItem(std::string key, bool value) {
      AddItem(key, ObjectSP(new Boolean(value)));
    }

    void Dump(std::ostream &s) const override {
      bool have_printed_one_elem = false;
      s << "{";
      for (collection::const_iterator iter = m_dict.begin();
           iter != m_dict.end(); ++iter) {
        if (!have_printed_one_elem) {
          have_printed_one_elem = true;
        } else {
          s << ",";
        }
        s << "\"" << iter->first.c_str() << "\":";
        iter->second->Dump(s);
      }
      s << "}";
    }

    void DumpBinaryEscaped(std::ostream &s) const override {
      bool have_printed_one_elem = false;
      s << "{";
      for (collection::const_iterator iter = m_dict.begin();
           iter != m_dict.end(); ++iter) {
        if (!have_printed_one_elem) {
          have_printed_one_elem = true;
        } else {
          s << ",";
        }
        s << "\"" << binary_encode_string(iter->first) << "\":";
        iter->second->DumpBinaryEscaped(s);
      }
      // '}' must be escaped for the gdb remote serial
      // protocol.
      s << "}";
      s << static_cast<char>('}' ^ 0x20);
    }

  protected:
    std::string binary_encode_string(const std::string &s) const {
      std::string output;
      const size_t s_size = s.size();
      const char *s_chars = s.c_str();

      for (size_t i = 0; i < s_size; i++) {
        unsigned char ch = *(s_chars + i);
        if (ch == '#' || ch == '$' || ch == '}' || ch == '*') {
          output.push_back('}'); // 0x7d
          output.push_back(ch ^ 0x20);
        } else {
          output.push_back(ch);
        }
      }
      return output;
    }

    // Keep the dictionary as a vector so the dictionary doesn't reorder itself
    // when you dump it
    // We aren't accessing keys by name, so this won't affect performance
    typedef std::pair<std::string, ObjectSP> Pair;
    typedef std::vector<Pair> collection;
    collection m_dict;
  };

  class Null : public Object {
  public:
    Null() : Object(Type::eTypeNull) {}

    virtual ~Null() {}

    bool IsValid() const override { return false; }

    void Dump(std::ostream &s) const override { s << "null"; }

    void DumpBinaryEscaped(std::ostream &s) const override { Dump(s); }

  protected:
  };

  class Generic : public Object {
  public:
    explicit Generic(void *object = nullptr)
        : Object(Type::eTypeGeneric), m_object(object) {}

    void SetValue(void *value) { m_object = value; }

    void *GetValue() const { return m_object; }

    bool IsValid() const override { return m_object != nullptr; }

    void Dump(std::ostream &s) const override;

    void DumpBinaryEscaped(std::ostream &s) const override;

  private:
    void *m_object;
  };

}; // class JSONGenerator

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_JSONGENERATOR_H
