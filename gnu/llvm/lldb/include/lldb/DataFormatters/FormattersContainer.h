//===-- FormattersContainer.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_FORMATTERSCONTAINER_H
#define LLDB_DATAFORMATTERS_FORMATTERSCONTAINER_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "lldb/lldb-public.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/TypeFormat.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StringLexer.h"

namespace lldb_private {

class IFormatChangeListener {
public:
  virtual ~IFormatChangeListener() = default;

  virtual void Changed() = 0;

  virtual uint32_t GetCurrentRevision() = 0;
};

/// Class for matching type names.
class TypeMatcher {
  /// Type name for exact match, or name of the python callback if m_match_type
  /// is `eFormatterMatchCallback`.
  ConstString m_name;
  RegularExpression m_type_name_regex;
  /// Indicates what kind of matching strategy should be used:
  /// - eFormatterMatchExact: match the exact type name in m_name.
  /// - eFormatterMatchRegex: match using the RegularExpression object
  ///   `m_type_name_regex` instead.
  /// - eFormatterMatchCallback: run the function in m_name to decide if a type
  ///   matches or not.
  lldb::FormatterMatchType m_match_type;

  // if the user tries to add formatters for, say, "struct Foo" those will not
  // match any type because of the way we strip qualifiers from typenames this
  // method looks for the case where the user is adding a
  // "class","struct","enum" or "union" Foo and strips the unnecessary qualifier
  static ConstString StripTypeName(ConstString type) {
    if (type.IsEmpty())
      return type;

    std::string type_cstr(type.AsCString());
    StringLexer type_lexer(type_cstr);

    type_lexer.AdvanceIf("class ");
    type_lexer.AdvanceIf("enum ");
    type_lexer.AdvanceIf("struct ");
    type_lexer.AdvanceIf("union ");

    while (type_lexer.NextIf({' ', '\t', '\v', '\f'}).first)
      ;

    return ConstString(type_lexer.GetUnlexed());
  }

public:
  TypeMatcher() = delete;
  /// Creates a matcher that accepts any type with exactly the given type name.
  TypeMatcher(ConstString type_name)
      : m_name(type_name), m_match_type(lldb::eFormatterMatchExact) {}
  /// Creates a matcher that accepts any type matching the given regex.
  TypeMatcher(RegularExpression regex)
      : m_type_name_regex(std::move(regex)),
        m_match_type(lldb::eFormatterMatchRegex) {}
  /// Creates a matcher using the matching type and string from the given type
  /// name specifier.
  TypeMatcher(lldb::TypeNameSpecifierImplSP type_specifier)
      : m_name(type_specifier->GetName()),
        m_match_type(type_specifier->GetMatchType()) {
    if (m_match_type == lldb::eFormatterMatchRegex)
      m_type_name_regex = RegularExpression(type_specifier->GetName());
  }

  /// True iff this matches the given type.
  bool Matches(FormattersMatchCandidate candidate_type) const {
    ConstString type_name = candidate_type.GetTypeName();
    switch (m_match_type) {
    case lldb::eFormatterMatchExact:
      return m_name == type_name ||
             StripTypeName(m_name) == StripTypeName(type_name);
    case lldb::eFormatterMatchRegex:
      return m_type_name_regex.Execute(type_name.GetStringRef());
    case lldb::eFormatterMatchCallback:
      // CommandObjectType{Synth,Filter}Add tries to prevent the user from
      // creating both a synthetic child provider and a filter for the same type
      // in the same category, but we don't have a type object at that point, so
      // it creates a dummy candidate without type or script interpreter.
      // Skip callback matching in these cases.
      if (candidate_type.GetScriptInterpreter())
        return candidate_type.GetScriptInterpreter()->FormatterCallbackFunction(
            m_name.AsCString(),
            std::make_shared<TypeImpl>(candidate_type.GetType()));
    }
    return false;
  }

  lldb::FormatterMatchType GetMatchType() const { return m_match_type; }

  /// Returns the underlying match string for this TypeMatcher.
  ConstString GetMatchString() const {
    if (m_match_type == lldb::eFormatterMatchExact)
        return StripTypeName(m_name);
    if (m_match_type == lldb::eFormatterMatchRegex)
        return ConstString(m_type_name_regex.GetText());
    return m_name;
  }

  /// Returns true if this TypeMatcher and the given one were most created by
  /// the same match string.
  /// The main purpose of this function is to find existing TypeMatcher
  /// instances by the user input that created them. This is necessary as LLDB
  /// allows referencing existing TypeMatchers in commands by the user input
  /// that originally created them:
  /// (lldb) type summary add --summary-string \"A\" -x TypeName
  /// (lldb) type summary delete TypeName
  bool CreatedBySameMatchString(TypeMatcher other) const {
    return GetMatchString() == other.GetMatchString();
  }
};

template <typename ValueType> class FormattersContainer {
public:
  typedef typename std::shared_ptr<ValueType> ValueSP;
  typedef std::vector<std::pair<TypeMatcher, ValueSP>> MapType;
  typedef std::function<bool(const TypeMatcher &, const ValueSP &)>
      ForEachCallback;
  typedef typename std::shared_ptr<FormattersContainer<ValueType>>
      SharedPointer;

  friend class TypeCategoryImpl;

  FormattersContainer(IFormatChangeListener *lst) : listener(lst) {}

  void Add(TypeMatcher matcher, const ValueSP &entry) {
    if (listener)
      entry->GetRevision() = listener->GetCurrentRevision();
    else
      entry->GetRevision() = 0;

    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    Delete(matcher);
    m_map.emplace_back(std::move(matcher), std::move(entry));
    if (listener)
      listener->Changed();
  }

  bool Delete(TypeMatcher matcher) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    for (auto iter = m_map.begin(); iter != m_map.end(); ++iter)
      if (iter->first.CreatedBySameMatchString(matcher)) {
        m_map.erase(iter);
        if (listener)
          listener->Changed();
        return true;
      }
    return false;
  }

  // Finds the first formatter in the container that matches `candidate`.
  bool Get(FormattersMatchCandidate candidate, ValueSP &entry) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    for (auto &formatter : llvm::reverse(m_map)) {
      if (formatter.first.Matches(candidate)) {
        entry = formatter.second;
        return true;
      }
    }
    return false;
  }

  // Finds the first match between candidate types in `candidates` and
  // formatters in this container.
  bool Get(const FormattersMatchVector &candidates, ValueSP &entry) {
    for (const FormattersMatchCandidate &candidate : candidates) {
      if (Get(candidate, entry)) {
        if (candidate.IsMatch(entry) == false) {
          entry.reset();
          continue;
        } else {
          return true;
        }
      }
    }
    return false;
  }

  bool GetExact(TypeMatcher matcher, ValueSP &entry) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    for (const auto &pos : m_map)
      if (pos.first.CreatedBySameMatchString(matcher)) {
        entry = pos.second;
        return true;
      }
    return false;
  }

  ValueSP GetAtIndex(size_t index) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    if (index >= m_map.size())
      return ValueSP();
    return m_map[index].second;
  }

  lldb::TypeNameSpecifierImplSP GetTypeNameSpecifierAtIndex(size_t index) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    if (index >= m_map.size())
      return lldb::TypeNameSpecifierImplSP();
    TypeMatcher type_matcher = m_map[index].first;
    return std::make_shared<TypeNameSpecifierImpl>(
        type_matcher.GetMatchString().GetStringRef(),
        type_matcher.GetMatchType());
  }

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    m_map.clear();
    if (listener)
      listener->Changed();
  }

  void ForEach(ForEachCallback callback) {
    if (callback) {
      std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
      for (const auto &pos : m_map) {
        const TypeMatcher &type = pos.first;
        if (!callback(type, pos.second))
          break;
      }
    }
  }

  uint32_t GetCount() {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    return m_map.size();
  }

  void AutoComplete(CompletionRequest &request) {
    ForEach([&request](const TypeMatcher &matcher, const ValueSP &value) {
      request.TryCompleteCurrentArg(matcher.GetMatchString().GetStringRef());
      return true;
    });
  }

protected:
  FormattersContainer(const FormattersContainer &) = delete;
  const FormattersContainer &operator=(const FormattersContainer &) = delete;

  MapType m_map;
  std::recursive_mutex m_map_mutex;
  IFormatChangeListener *listener;
};

} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_FORMATTERSCONTAINER_H
