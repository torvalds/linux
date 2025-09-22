//===-- StructuredData.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cerrno>
#include <cinttypes>
#include <cstdlib>

using namespace lldb_private;
using namespace llvm;

static StructuredData::ObjectSP ParseJSONValue(json::Value &value);
static StructuredData::ObjectSP ParseJSONObject(json::Object *object);
static StructuredData::ObjectSP ParseJSONArray(json::Array *array);

StructuredData::ObjectSP StructuredData::ParseJSON(llvm::StringRef json_text) {
  llvm::Expected<json::Value> value = json::parse(json_text);
  if (!value) {
    llvm::consumeError(value.takeError());
    return nullptr;
  }
  return ParseJSONValue(*value);
}

StructuredData::ObjectSP
StructuredData::ParseJSONFromFile(const FileSpec &input_spec, Status &error) {
  StructuredData::ObjectSP return_sp;

  auto buffer_or_error = llvm::MemoryBuffer::getFile(input_spec.GetPath());
  if (!buffer_or_error) {
    error.SetErrorStringWithFormatv("could not open input file: {0} - {1}.",
                                    input_spec.GetPath(),
                                    buffer_or_error.getError().message());
    return return_sp;
  }
  llvm::Expected<json::Value> value =
      json::parse(buffer_or_error.get()->getBuffer().str());
  if (value)
    return ParseJSONValue(*value);
  error.SetErrorString(toString(value.takeError()));
  return StructuredData::ObjectSP();
}

bool StructuredData::IsRecordType(const ObjectSP object_sp) {
  return object_sp->GetType() == lldb::eStructuredDataTypeArray ||
         object_sp->GetType() == lldb::eStructuredDataTypeDictionary;
}

static StructuredData::ObjectSP ParseJSONValue(json::Value &value) {
  if (json::Object *O = value.getAsObject())
    return ParseJSONObject(O);

  if (json::Array *A = value.getAsArray())
    return ParseJSONArray(A);

  if (auto s = value.getAsString())
    return std::make_shared<StructuredData::String>(*s);

  if (auto b = value.getAsBoolean())
    return std::make_shared<StructuredData::Boolean>(*b);

  if (auto u = value.getAsUINT64())
    return std::make_shared<StructuredData::UnsignedInteger>(*u);

  if (auto i = value.getAsInteger())
    return std::make_shared<StructuredData::SignedInteger>(*i);

  if (auto d = value.getAsNumber())
    return std::make_shared<StructuredData::Float>(*d);

  if (auto n = value.getAsNull())
    return std::make_shared<StructuredData::Null>();

  return StructuredData::ObjectSP();
}

static StructuredData::ObjectSP ParseJSONObject(json::Object *object) {
  auto dict_up = std::make_unique<StructuredData::Dictionary>();
  for (auto &KV : *object) {
    StringRef key = KV.first;
    json::Value value = KV.second;
    if (StructuredData::ObjectSP value_sp = ParseJSONValue(value))
      dict_up->AddItem(key, value_sp);
  }
  return std::move(dict_up);
}

static StructuredData::ObjectSP ParseJSONArray(json::Array *array) {
  auto array_up = std::make_unique<StructuredData::Array>();
  for (json::Value &value : *array) {
    if (StructuredData::ObjectSP value_sp = ParseJSONValue(value))
      array_up->AddItem(value_sp);
  }
  return std::move(array_up);
}

StructuredData::ObjectSP
StructuredData::Object::GetObjectForDotSeparatedPath(llvm::StringRef path) {
  if (GetType() == lldb::eStructuredDataTypeDictionary) {
    std::pair<llvm::StringRef, llvm::StringRef> match = path.split('.');
    llvm::StringRef key = match.first;
    ObjectSP value = GetAsDictionary()->GetValueForKey(key);
    if (!value)
      return {};

    // Do we have additional words to descend?  If not, return the value
    // we're at right now.
    if (match.second.empty())
      return value;

    return value->GetObjectForDotSeparatedPath(match.second);
  }

  if (GetType() == lldb::eStructuredDataTypeArray) {
    std::pair<llvm::StringRef, llvm::StringRef> match = path.split('[');
    if (match.second.empty())
      return shared_from_this();

    uint64_t val = 0;
    if (!llvm::to_integer(match.second, val, /* Base = */ 10))
      return {};

    return GetAsArray()->GetItemAtIndex(val);
  }

  return shared_from_this();
}

void StructuredData::Object::DumpToStdout(bool pretty_print) const {
  json::OStream stream(llvm::outs(), pretty_print ? 2 : 0);
  Serialize(stream);
}

void StructuredData::Array::Serialize(json::OStream &s) const {
  s.arrayBegin();
  for (const auto &item_sp : m_items) {
    item_sp->Serialize(s);
  }
  s.arrayEnd();
}

void StructuredData::Float::Serialize(json::OStream &s) const {
  s.value(m_value);
}

void StructuredData::Boolean::Serialize(json::OStream &s) const {
  s.value(m_value);
}

void StructuredData::String::Serialize(json::OStream &s) const {
  s.value(m_value);
}

void StructuredData::Dictionary::Serialize(json::OStream &s) const {
  s.objectBegin();

  // To ensure the output format is always stable, we sort the dictionary by key
  // first.
  using Entry = std::pair<llvm::StringRef, ObjectSP>;
  std::vector<Entry> sorted_entries;
  for (const auto &pair : m_dict)
    sorted_entries.push_back({pair.first(), pair.second});

  llvm::sort(sorted_entries);

  for (const auto &pair : sorted_entries) {
    s.attributeBegin(pair.first);
    pair.second->Serialize(s);
    s.attributeEnd();
  }
  s.objectEnd();
}

void StructuredData::Null::Serialize(json::OStream &s) const {
  s.value(nullptr);
}

void StructuredData::Generic::Serialize(json::OStream &s) const {
  s.value(llvm::formatv("{0:X}", m_object));
}

void StructuredData::Float::GetDescription(lldb_private::Stream &s) const {
  s.Printf("%f", m_value);
}

void StructuredData::Boolean::GetDescription(lldb_private::Stream &s) const {
  s.Printf(m_value ? "True" : "False");
}

void StructuredData::String::GetDescription(lldb_private::Stream &s) const {
  s.Printf("%s", m_value.empty() ? "\"\"" : m_value.c_str());
}

void StructuredData::Array::GetDescription(lldb_private::Stream &s) const {
  size_t index = 0;
  size_t indentation_level = s.GetIndentLevel();
  for (const auto &item_sp : m_items) {
    // Sanitize.
    if (!item_sp)
      continue;

    // Reset original indentation level.
    s.SetIndentLevel(indentation_level);
    s.Indent();

    // Print key
    s.Printf("[%zu]:", index++);

    // Return to new line and increase indentation if value is record type.
    // Otherwise add spacing.
    bool should_indent = IsRecordType(item_sp);
    if (should_indent) {
      s.EOL();
      s.IndentMore();
    } else {
      s.PutChar(' ');
    }

    // Print value and new line if now last pair.
    item_sp->GetDescription(s);
    if (item_sp != *(--m_items.end()))
      s.EOL();

    // Reset indentation level if it was incremented previously.
    if (should_indent)
      s.IndentLess();
  }
}

void StructuredData::Dictionary::GetDescription(lldb_private::Stream &s) const {
  size_t indentation_level = s.GetIndentLevel();

  // To ensure the output format is always stable, we sort the dictionary by key
  // first.
  using Entry = std::pair<llvm::StringRef, ObjectSP>;
  std::vector<Entry> sorted_entries;
  for (const auto &pair : m_dict)
    sorted_entries.push_back({pair.first(), pair.second});

  llvm::sort(sorted_entries);

  for (auto iter = sorted_entries.begin(); iter != sorted_entries.end();
       iter++) {
    // Sanitize.
    if (iter->first.empty() || !iter->second)
      continue;

    // Reset original indentation level.
    s.SetIndentLevel(indentation_level);
    s.Indent();

    // Print key.
    s.Format("{0}:", iter->first);

    // Return to new line and increase indentation if value is record type.
    // Otherwise add spacing.
    bool should_indent = IsRecordType(iter->second);
    if (should_indent) {
      s.EOL();
      s.IndentMore();
    } else {
      s.PutChar(' ');
    }

    // Print value and new line if now last pair.
    iter->second->GetDescription(s);
    if (std::next(iter) != sorted_entries.end())
      s.EOL();

    // Reset indentation level if it was incremented previously.
    if (should_indent)
      s.IndentLess();
  }
}

void StructuredData::Null::GetDescription(lldb_private::Stream &s) const {
  s.Printf("NULL");
}

void StructuredData::Generic::GetDescription(lldb_private::Stream &s) const {
  s.Printf("%p", m_object);
}
