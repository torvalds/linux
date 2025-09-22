//===-- StructuredData.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_STRUCTUREDDATA_H
#define LLDB_UTILITY_STRUCTUREDDATA_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace lldb_private {
class Status;
}

namespace lldb_private {

/// \class StructuredData StructuredData.h "lldb/Utility/StructuredData.h"
/// A class which can hold structured data
///
/// The StructuredData class is designed to hold the data from a JSON or plist
/// style file -- a serialized data structure with dictionaries (maps,
/// hashes), arrays, and concrete values like integers, floating point
/// numbers, strings, booleans.
///
/// StructuredData does not presuppose any knowledge of the schema for the
/// data it is holding; it can parse JSON data, for instance, and other parts
/// of lldb can iterate through the parsed data set to find keys and values
/// that may be present.

class StructuredData {
  template <typename N> class Integer;

public:
  class Object;
  class Array;
  using UnsignedInteger = Integer<uint64_t>;
  using SignedInteger = Integer<int64_t>;
  class Float;
  class Boolean;
  class String;
  class Dictionary;
  class Generic;

  typedef std::shared_ptr<Object> ObjectSP;
  typedef std::shared_ptr<Array> ArraySP;
  typedef std::shared_ptr<UnsignedInteger> UnsignedIntegerSP;
  typedef std::shared_ptr<SignedInteger> SignedIntegerSP;
  typedef std::shared_ptr<Float> FloatSP;
  typedef std::shared_ptr<Boolean> BooleanSP;
  typedef std::shared_ptr<String> StringSP;
  typedef std::shared_ptr<Dictionary> DictionarySP;
  typedef std::shared_ptr<Generic> GenericSP;

  typedef std::variant<UnsignedIntegerSP, SignedIntegerSP> IntegerSP;

  class Object : public std::enable_shared_from_this<Object> {
  public:
    Object(lldb::StructuredDataType t = lldb::eStructuredDataTypeInvalid)
        : m_type(t) {}

    virtual ~Object() = default;

    virtual bool IsValid() const { return true; }

    virtual void Clear() { m_type = lldb::eStructuredDataTypeInvalid; }

    lldb::StructuredDataType GetType() const { return m_type; }

    void SetType(lldb::StructuredDataType t) { m_type = t; }

    Array *GetAsArray() {
      return ((m_type == lldb::eStructuredDataTypeArray)
                  ? static_cast<Array *>(this)
                  : nullptr);
    }

    Dictionary *GetAsDictionary() {
      return ((m_type == lldb::eStructuredDataTypeDictionary)
                  ? static_cast<Dictionary *>(this)
                  : nullptr);
    }

    UnsignedInteger *GetAsUnsignedInteger() {
      // NOTE: For backward compatibility, eStructuredDataTypeInteger is
      // the same as eStructuredDataTypeUnsignedInteger.
      return ((m_type == lldb::eStructuredDataTypeInteger ||
               m_type == lldb::eStructuredDataTypeUnsignedInteger)
                  ? static_cast<UnsignedInteger *>(this)
                  : nullptr);
    }

    SignedInteger *GetAsSignedInteger() {
      return ((m_type == lldb::eStructuredDataTypeSignedInteger)
                  ? static_cast<SignedInteger *>(this)
                  : nullptr);
    }

    uint64_t GetUnsignedIntegerValue(uint64_t fail_value = 0) {
      UnsignedInteger *integer = GetAsUnsignedInteger();
      return ((integer != nullptr) ? integer->GetValue() : fail_value);
    }

    int64_t GetSignedIntegerValue(int64_t fail_value = 0) {
      SignedInteger *integer = GetAsSignedInteger();
      return ((integer != nullptr) ? integer->GetValue() : fail_value);
    }

    Float *GetAsFloat() {
      return ((m_type == lldb::eStructuredDataTypeFloat)
                  ? static_cast<Float *>(this)
                  : nullptr);
    }

    double GetFloatValue(double fail_value = 0.0) {
      Float *f = GetAsFloat();
      return ((f != nullptr) ? f->GetValue() : fail_value);
    }

    Boolean *GetAsBoolean() {
      return ((m_type == lldb::eStructuredDataTypeBoolean)
                  ? static_cast<Boolean *>(this)
                  : nullptr);
    }

    bool GetBooleanValue(bool fail_value = false) {
      Boolean *b = GetAsBoolean();
      return ((b != nullptr) ? b->GetValue() : fail_value);
    }

    String *GetAsString() {
      return ((m_type == lldb::eStructuredDataTypeString)
                  ? static_cast<String *>(this)
                  : nullptr);
    }

    llvm::StringRef GetStringValue(const char *fail_value = nullptr) {
      String *s = GetAsString();
      if (s)
        return s->GetValue();

      return fail_value;
    }

    Generic *GetAsGeneric() {
      return ((m_type == lldb::eStructuredDataTypeGeneric)
                  ? static_cast<Generic *>(this)
                  : nullptr);
    }

    ObjectSP GetObjectForDotSeparatedPath(llvm::StringRef path);

    void DumpToStdout(bool pretty_print = true) const;

    virtual void Serialize(llvm::json::OStream &s) const = 0;

    void Dump(lldb_private::Stream &s, bool pretty_print = true) const {
      llvm::json::OStream jso(s.AsRawOstream(), pretty_print ? 2 : 0);
      Serialize(jso);
    }

    virtual void GetDescription(lldb_private::Stream &s) const {
      s.IndentMore();
      Dump(s, false);
      s.IndentLess();
    }

  private:
    lldb::StructuredDataType m_type;
  };

  class Array : public Object {
  public:
    Array() : Object(lldb::eStructuredDataTypeArray) {}

    ~Array() override = default;

    bool
    ForEach(std::function<bool(Object *object)> const &foreach_callback) const {
      for (const auto &object_sp : m_items) {
        if (!foreach_callback(object_sp.get()))
          return false;
      }
      return true;
    }

    size_t GetSize() const { return m_items.size(); }

    ObjectSP operator[](size_t idx) {
      if (idx < m_items.size())
        return m_items[idx];
      return ObjectSP();
    }

    ObjectSP GetItemAtIndex(size_t idx) const {
      assert(idx < GetSize());
      if (idx < m_items.size())
        return m_items[idx];
      return ObjectSP();
    }

    template <class IntType>
    std::optional<IntType> GetItemAtIndexAsInteger(size_t idx) const {
      if (auto item_sp = GetItemAtIndex(idx)) {
        if constexpr (std::numeric_limits<IntType>::is_signed) {
          if (auto *signed_value = item_sp->GetAsSignedInteger())
            return static_cast<IntType>(signed_value->GetValue());
        } else {
          if (auto *unsigned_value = item_sp->GetAsUnsignedInteger())
            return static_cast<IntType>(unsigned_value->GetValue());
        }
      }
      return {};
    }

    std::optional<llvm::StringRef> GetItemAtIndexAsString(size_t idx) const {
      if (auto item_sp = GetItemAtIndex(idx)) {
        if (auto *string_value = item_sp->GetAsString())
          return string_value->GetValue();
      }
      return {};
    }

    /// Retrieves the element at index \a idx from a StructuredData::Array if it
    /// is a Dictionary.
    ///
    /// \param[in] idx
    ///   The index of the element to retrieve.
    ///
    /// \return
    ///   If the element at index \a idx is a Dictionary, this method returns a
    ///   valid pointer to the Dictionary wrapped in a std::optional. If the
    ///   element is not a Dictionary or the index is invalid, this returns
    ///   std::nullopt. Note that the underlying Dictionary pointer is never
    ///   nullptr.
    std::optional<Dictionary *> GetItemAtIndexAsDictionary(size_t idx) const {
      if (auto item_sp = GetItemAtIndex(idx)) {
        if (auto *dict = item_sp->GetAsDictionary())
          return dict;
      }
      return {};
    }

    void Push(const ObjectSP &item) { m_items.push_back(item); }

    void AddItem(const ObjectSP &item) { m_items.push_back(item); }

    template <typename T> void AddIntegerItem(T value) {
      static_assert(std::is_integral<T>::value ||
                        std::is_floating_point<T>::value,
                    "value type should be integral");
      if constexpr (std::numeric_limits<T>::is_signed)
        AddItem(std::make_shared<SignedInteger>(value));
      else
        AddItem(std::make_shared<UnsignedInteger>(value));
    }

    void AddFloatItem(double value) { AddItem(std::make_shared<Float>(value)); }

    void AddStringItem(llvm::StringRef value) {
      AddItem(std::make_shared<String>(std::move(value)));
    }

    void AddBooleanItem(bool value) {
      AddItem(std::make_shared<Boolean>(value));
    }

    void Serialize(llvm::json::OStream &s) const override;

    void GetDescription(lldb_private::Stream &s) const override;

  protected:
    typedef std::vector<ObjectSP> collection;
    collection m_items;
  };

private:
  template <typename N> class Integer : public Object {
    static_assert(std::is_integral<N>::value, "N must be an integral type");

  public:
    Integer(N i = 0)
        : Object(std::numeric_limits<N>::is_signed
                     ? lldb::eStructuredDataTypeSignedInteger
                     : lldb::eStructuredDataTypeUnsignedInteger),
          m_value(i) {}
    ~Integer() override = default;

    void SetValue(N value) { m_value = value; }

    N GetValue() { return m_value; }

    void Serialize(llvm::json::OStream &s) const override {
      s.value(static_cast<N>(m_value));
    }

    void GetDescription(lldb_private::Stream &s) const override {
      s.Printf(std::numeric_limits<N>::is_signed ? "%" PRId64 : "%" PRIu64,
               static_cast<N>(m_value));
    }

  protected:
    N m_value;
  };

public:
  class Float : public Object {
  public:
    Float(double d = 0.0)
        : Object(lldb::eStructuredDataTypeFloat), m_value(d) {}

    ~Float() override = default;

    void SetValue(double value) { m_value = value; }

    double GetValue() { return m_value; }

    void Serialize(llvm::json::OStream &s) const override;

    void GetDescription(lldb_private::Stream &s) const override;

  protected:
    double m_value;
  };

  class Boolean : public Object {
  public:
    Boolean(bool b = false)
        : Object(lldb::eStructuredDataTypeBoolean), m_value(b) {}

    ~Boolean() override = default;

    void SetValue(bool value) { m_value = value; }

    bool GetValue() { return m_value; }

    void Serialize(llvm::json::OStream &s) const override;

    void GetDescription(lldb_private::Stream &s) const override;

  protected:
    bool m_value;
  };

  class String : public Object {
  public:
    String() : Object(lldb::eStructuredDataTypeString) {}
    explicit String(llvm::StringRef S)
        : Object(lldb::eStructuredDataTypeString), m_value(S) {}

    void SetValue(llvm::StringRef S) { m_value = std::string(S); }

    llvm::StringRef GetValue() { return m_value; }

    void Serialize(llvm::json::OStream &s) const override;

    void GetDescription(lldb_private::Stream &s) const override;

  protected:
    std::string m_value;
  };

  class Dictionary : public Object {
  public:
    Dictionary() : Object(lldb::eStructuredDataTypeDictionary) {}

    Dictionary(ObjectSP obj_sp) : Object(lldb::eStructuredDataTypeDictionary) {
      if (!obj_sp || obj_sp->GetType() != lldb::eStructuredDataTypeDictionary) {
        SetType(lldb::eStructuredDataTypeInvalid);
        return;
      }

      Dictionary *dict = obj_sp->GetAsDictionary();
      m_dict = dict->m_dict;
    }

    ~Dictionary() override = default;

    size_t GetSize() const { return m_dict.size(); }

    void ForEach(std::function<bool(llvm::StringRef key, Object *object)> const
                     &callback) const {
      for (const auto &pair : m_dict) {
        if (!callback(pair.first(), pair.second.get()))
          break;
      }
    }

    ArraySP GetKeys() const {
      auto array_sp = std::make_shared<Array>();
      for (auto iter = m_dict.begin(); iter != m_dict.end(); ++iter) {
        auto key_object_sp = std::make_shared<String>(iter->first());
        array_sp->Push(key_object_sp);
      }
      return array_sp;
    }

    ObjectSP GetValueForKey(llvm::StringRef key) const {
      return m_dict.lookup(key);
    }

    bool GetValueForKeyAsBoolean(llvm::StringRef key, bool &result) const {
      bool success = false;
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        Boolean *result_ptr = value_sp->GetAsBoolean();
        if (result_ptr) {
          result = result_ptr->GetValue();
          success = true;
        }
      }
      return success;
    }
      
    template <class IntType>
    bool GetValueForKeyAsInteger(llvm::StringRef key, IntType &result) const {
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp) {
        if constexpr (std::numeric_limits<IntType>::is_signed) {
          if (auto signed_value = value_sp->GetAsSignedInteger()) {
            result = static_cast<IntType>(signed_value->GetValue());
            return true;
          }
        } else {
          if (auto unsigned_value = value_sp->GetAsUnsignedInteger()) {
            result = static_cast<IntType>(unsigned_value->GetValue());
            return true;
          }
        }
      }
      return false;
    }

    template <class IntType>
    bool GetValueForKeyAsInteger(llvm::StringRef key, IntType &result,
                                 IntType default_val) const {
      bool success = GetValueForKeyAsInteger<IntType>(key, result);
      if (!success)
        result = default_val;
      return success;
    }

    bool GetValueForKeyAsString(llvm::StringRef key,
                                llvm::StringRef &result) const {
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        if (auto string_value = value_sp->GetAsString()) {
          result = string_value->GetValue();
          return true;
        }
      }
      return false;
    }

    bool GetValueForKeyAsString(llvm::StringRef key, llvm::StringRef &result,
                                const char *default_val) const {
      bool success = GetValueForKeyAsString(key, result);
      if (!success) {
        if (default_val)
          result = default_val;
        else
          result = llvm::StringRef();
      }
      return success;
    }

    bool GetValueForKeyAsDictionary(llvm::StringRef key,
                                    Dictionary *&result) const {
      result = nullptr;
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        result = value_sp->GetAsDictionary();
        return (result != nullptr);
      }
      return false;
    }

    bool GetValueForKeyAsArray(llvm::StringRef key, Array *&result) const {
      result = nullptr;
      ObjectSP value_sp = GetValueForKey(key);
      if (value_sp.get()) {
        result = value_sp->GetAsArray();
        return (result != nullptr);
      }
      return false;
    }

    bool HasKey(llvm::StringRef key) const { return m_dict.contains(key); }

    void AddItem(llvm::StringRef key, ObjectSP value_sp) {
      m_dict.insert_or_assign(key, std::move(value_sp));
    }

    template <typename T> void AddIntegerItem(llvm::StringRef key, T value) {
      static_assert(std::is_integral<T>::value ||
                        std::is_floating_point<T>::value,
                    "value type should be integral");
      if constexpr (std::numeric_limits<T>::is_signed)
        AddItem(key, std::make_shared<SignedInteger>(value));
      else
        AddItem(key, std::make_shared<UnsignedInteger>(value));
    }

    void AddFloatItem(llvm::StringRef key, double value) {
      AddItem(key, std::make_shared<Float>(value));
    }

    void AddStringItem(llvm::StringRef key, llvm::StringRef value) {
      AddItem(key, std::make_shared<String>(std::move(value)));
    }

    void AddBooleanItem(llvm::StringRef key, bool value) {
      AddItem(key, std::make_shared<Boolean>(value));
    }

    void Serialize(llvm::json::OStream &s) const override;

    void GetDescription(lldb_private::Stream &s) const override;

  protected:
    llvm::StringMap<ObjectSP> m_dict;
  };

  class Null : public Object {
  public:
    Null() : Object(lldb::eStructuredDataTypeNull) {}

    ~Null() override = default;

    bool IsValid() const override { return false; }

    void Serialize(llvm::json::OStream &s) const override;

    void GetDescription(lldb_private::Stream &s) const override;
  };

  class Generic : public Object {
  public:
    explicit Generic(void *object = nullptr)
        : Object(lldb::eStructuredDataTypeGeneric), m_object(object) {}

    void SetValue(void *value) { m_object = value; }

    void *GetValue() const { return m_object; }

    bool IsValid() const override { return m_object != nullptr; }

    void Serialize(llvm::json::OStream &s) const override;

    void GetDescription(lldb_private::Stream &s) const override;

  private:
    void *m_object;
  };

  static ObjectSP ParseJSON(llvm::StringRef json_text);
  static ObjectSP ParseJSONFromFile(const FileSpec &file, Status &error);
  static bool IsRecordType(const ObjectSP object_sp);
};

} // namespace lldb_private

#endif // LLDB_UTILITY_STRUCTUREDDATA_H
