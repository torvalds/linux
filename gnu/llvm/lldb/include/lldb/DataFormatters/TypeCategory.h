//===-- TypeCategory.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_TYPECATEGORY_H
#define LLDB_DATAFORMATTERS_TYPECATEGORY_H

#include <array>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/FormattersContainer.h"

namespace lldb_private {

// A formatter container with sub-containers for different priority tiers, that
// also exposes a flat view of all formatters in it.
//
// Formatters have different priority during matching, depending on the type of
// matching specified at registration. Exact matchers are processed first, then
// regex, and finally callback matchers. However, the scripting API presents a
// flat view of formatters in a category, with methods like `GetNumFormats()`
// and `GetFormatAtIndex(i)`. So we need something that can behave like both
// representations.
template <typename FormatterImpl> class TieredFormatterContainer {
public:
  using Subcontainer = FormattersContainer<FormatterImpl>;
  using SubcontainerSP = std::shared_ptr<Subcontainer>;
  using ForEachCallback = typename Subcontainer::ForEachCallback;
  using MapValueType = typename Subcontainer::ValueSP;

  TieredFormatterContainer(IFormatChangeListener *change_listener) {
    for (auto& sc : m_subcontainers)
      sc = std::make_shared<Subcontainer>(change_listener);
  }

  /// Clears all subcontainers.
  void Clear() {
    for (auto sc : m_subcontainers)
      sc->Clear();
  }

  /// Adds a formatter to the right subcontainer depending on the matching type
  /// specified by `type_sp`.
  void Add(lldb::TypeNameSpecifierImplSP type_sp,
           std::shared_ptr<FormatterImpl> format_sp) {
    m_subcontainers[type_sp->GetMatchType()]->Add(TypeMatcher(type_sp),
                                                  format_sp);
  }

  /// Deletes the formatter specified by `type_sp`.
  bool Delete(lldb::TypeNameSpecifierImplSP type_sp) {
    return m_subcontainers[type_sp->GetMatchType()]->Delete(
        TypeMatcher(type_sp));
  }

  /// Deletes all formatters registered with the string `name`, in all
  /// subcontainers.
  bool Delete(ConstString name) {
    bool success = false;
    for (auto sc : m_subcontainers)
      success = sc->Delete(name) || success;
    return success;
  }

  /// Returns the total count of elements across all subcontainers.
  uint32_t GetCount() {
    uint32_t result = 0;
    for (auto sc : m_subcontainers)
      result += sc->GetCount();
    return result;
  }

  /// Returns the formatter at `index`, simulating a flattened view of all
  /// subcontainers in priority order.
  MapValueType GetAtIndex(size_t index) {
    for (auto sc : m_subcontainers) {
      if (index < sc->GetCount())
        return sc->GetAtIndex(index);
      index -= sc->GetCount();
    }
    return MapValueType();
  }

  /// Looks for a matching candidate across all priority tiers, in priority
  /// order. If a match is found, returns `true` and puts the matching entry in
  /// `entry`.
  bool Get(const FormattersMatchVector &candidates,
           std::shared_ptr<FormatterImpl> &entry) {
    for (auto sc : m_subcontainers) {
      if (sc->Get(candidates, entry))
        return true;
    }
    return false;
  }

  bool AnyMatches(const FormattersMatchCandidate &candidate) {
    std::shared_ptr<FormatterImpl> entry;
    for (auto sc : m_subcontainers) {
      if (sc->Get(FormattersMatchVector{candidate}, entry))
        return true;
    }
    return false;
  }

  /// Returns a formatter that is an exact match for `type_specifier_sp`. It
  /// looks for a formatter with the same matching type that was created from
  /// the same string. This is useful so we can refer to a formatter using the
  /// same string used to register it.
  ///
  /// For example, `type_specifier_sp` can be something like
  /// {"std::vector<.*>", eFormatterMatchRegex}, and we'd look for a regex
  /// matcher with that exact regex string, NOT try to match that string using
  /// regex.
  MapValueType
  GetForTypeNameSpecifier(lldb::TypeNameSpecifierImplSP type_specifier_sp) {
    MapValueType retval;
    if (type_specifier_sp) {
      m_subcontainers[type_specifier_sp->GetMatchType()]->GetExact(
          ConstString(type_specifier_sp->GetName()), retval);
    }
    return retval;
  }

  /// Returns the type name specifier at `index`, simulating a flattened view of
  /// all subcontainers in priority order.
  lldb::TypeNameSpecifierImplSP GetTypeNameSpecifierAtIndex(size_t index) {
    for (auto sc : m_subcontainers) {
      if (index < sc->GetCount())
        return sc->GetTypeNameSpecifierAtIndex(index);
      index -= sc->GetCount();
    }
    return lldb::TypeNameSpecifierImplSP();
  }

  /// Iterates through tiers in order, running `callback` on each element of
  /// each tier.
  void ForEach(std::function<bool(const TypeMatcher &,
                                  const std::shared_ptr<FormatterImpl> &)>
                   callback) {
    for (auto sc : m_subcontainers) {
      sc->ForEach(callback);
    }
  }

  void AutoComplete(CompletionRequest &request) {
    for (auto sc: m_subcontainers)
      sc->AutoComplete(request);
  }

 private:
  std::array<std::shared_ptr<Subcontainer>, lldb::eLastFormatterMatchType + 1>
      m_subcontainers;
};

class TypeCategoryImpl {
private:
  typedef TieredFormatterContainer<TypeFormatImpl> FormatContainer;
  typedef TieredFormatterContainer<TypeSummaryImpl> SummaryContainer;
  typedef TieredFormatterContainer<TypeFilterImpl> FilterContainer;
  typedef TieredFormatterContainer<SyntheticChildren> SynthContainer;

public:
  typedef uint16_t FormatCategoryItems;
  static const uint16_t ALL_ITEM_TYPES = UINT16_MAX;

  // TypeFilterImpl inherits from SyntheticChildren, so we can't simply overload
  // ForEach on the type of the callback because it would result in "call to
  // member function 'ForEach' is ambiguous" errors. Instead we use this
  // templated struct to hold the formatter type and the callback.
  template<typename T>
  struct ForEachCallback {
    // Make it constructible from any callable that fits. This allows us to use
    // lambdas a bit more easily at the call site. For example:
    // ForEachCallback<TypeFormatImpl> callback = [](...) {...};
    template <typename Callable> ForEachCallback(Callable c) : callback(c) {}
    std::function<bool(const TypeMatcher &, const std::shared_ptr<T> &)>
        callback;
  };

  TypeCategoryImpl(IFormatChangeListener *clist, ConstString name);

  void ForEach(ForEachCallback<TypeFormatImpl> callback) {
    m_format_cont.ForEach(callback.callback);
  }

  void ForEach(ForEachCallback<TypeSummaryImpl> callback) {
    m_summary_cont.ForEach(callback.callback);
  }

  void ForEach(ForEachCallback<TypeFilterImpl> callback) {
    m_filter_cont.ForEach(callback.callback);
  }

  void ForEach(ForEachCallback<SyntheticChildren> callback) {
    m_synth_cont.ForEach(callback.callback);
  }

  FormatContainer::MapValueType
  GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp);

  SummaryContainer::MapValueType
  GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp);

  FilterContainer::MapValueType
  GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp);

  SynthContainer::MapValueType
  GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp);

  void AddTypeFormat(lldb::TypeNameSpecifierImplSP type_sp,
                     lldb::TypeFormatImplSP format_sp) {
    m_format_cont.Add(type_sp, format_sp);
  }

  void AddTypeFormat(llvm::StringRef name, lldb::FormatterMatchType match_type,
                     lldb::TypeFormatImplSP format_sp) {
    AddTypeFormat(
        std::make_shared<lldb_private::TypeNameSpecifierImpl>(name, match_type),
        format_sp);
  }

  void AddTypeSummary(lldb::TypeNameSpecifierImplSP type_sp,
                      lldb::TypeSummaryImplSP summary_sp) {
    m_summary_cont.Add(type_sp, summary_sp);
  }

  void AddTypeSummary(llvm::StringRef name, lldb::FormatterMatchType match_type,
                      lldb::TypeSummaryImplSP summary_sp) {
    AddTypeSummary(
        std::make_shared<lldb_private::TypeNameSpecifierImpl>(name, match_type),
        summary_sp);
  }

  void AddTypeFilter(lldb::TypeNameSpecifierImplSP type_sp,
                     lldb::TypeFilterImplSP filter_sp) {
    m_filter_cont.Add(type_sp, filter_sp);
  }

  void AddTypeFilter(llvm::StringRef name, lldb::FormatterMatchType match_type,
                     lldb::TypeFilterImplSP filter_sp) {
    AddTypeFilter(
        std::make_shared<lldb_private::TypeNameSpecifierImpl>(name, match_type),
        filter_sp);
  }

  void AddTypeSynthetic(lldb::TypeNameSpecifierImplSP type_sp,
                        lldb::SyntheticChildrenSP synth_sp) {
    m_synth_cont.Add(type_sp, synth_sp);
  }

  void AddTypeSynthetic(llvm::StringRef name,
                        lldb::FormatterMatchType match_type,
                        lldb::SyntheticChildrenSP synth_sp) {
    AddTypeSynthetic(
        std::make_shared<lldb_private::TypeNameSpecifierImpl>(name, match_type),
        synth_sp);
  }

  bool DeleteTypeFormat(lldb::TypeNameSpecifierImplSP type_sp) {
    return m_format_cont.Delete(type_sp);
  }

  bool DeleteTypeSummary(lldb::TypeNameSpecifierImplSP type_sp) {
    return m_summary_cont.Delete(type_sp);
  }

  bool DeleteTypeFilter(lldb::TypeNameSpecifierImplSP type_sp) {
    return m_filter_cont.Delete(type_sp);
  }

  bool DeleteTypeSynthetic(lldb::TypeNameSpecifierImplSP type_sp) {
    return m_synth_cont.Delete(type_sp);
  }

  uint32_t GetNumFormats() { return m_format_cont.GetCount(); }

  uint32_t GetNumSummaries() { return m_summary_cont.GetCount(); }

  uint32_t GetNumFilters() { return m_filter_cont.GetCount(); }

  uint32_t GetNumSynthetics() { return m_synth_cont.GetCount(); }

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForFormatAtIndex(size_t index);

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForSummaryAtIndex(size_t index);

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForFilterAtIndex(size_t index);

  lldb::TypeNameSpecifierImplSP
  GetTypeNameSpecifierForSyntheticAtIndex(size_t index);

  FormatContainer::MapValueType GetFormatAtIndex(size_t index);

  SummaryContainer::MapValueType GetSummaryAtIndex(size_t index);

  FilterContainer::MapValueType GetFilterAtIndex(size_t index);

  SynthContainer::MapValueType GetSyntheticAtIndex(size_t index);

  bool IsEnabled() const { return m_enabled; }

  uint32_t GetEnabledPosition() {
    if (!m_enabled)
      return UINT32_MAX;
    else
      return m_enabled_position;
  }

  bool Get(lldb::LanguageType lang, const FormattersMatchVector &candidates,
           lldb::TypeFormatImplSP &entry);

  bool Get(lldb::LanguageType lang, const FormattersMatchVector &candidates,
           lldb::TypeSummaryImplSP &entry);

  bool Get(lldb::LanguageType lang, const FormattersMatchVector &candidates,
           lldb::SyntheticChildrenSP &entry);

  void Clear(FormatCategoryItems items = ALL_ITEM_TYPES);

  bool Delete(ConstString name, FormatCategoryItems items = ALL_ITEM_TYPES);

  uint32_t GetCount(FormatCategoryItems items = ALL_ITEM_TYPES);

  const char *GetName() { return m_name.GetCString(); }

  size_t GetNumLanguages();

  lldb::LanguageType GetLanguageAtIndex(size_t idx);

  void AddLanguage(lldb::LanguageType lang);

  std::string GetDescription();

  bool AnyMatches(const FormattersMatchCandidate &candidate_type,
                  FormatCategoryItems items = ALL_ITEM_TYPES,
                  bool only_enabled = true,
                  const char **matching_category = nullptr,
                  FormatCategoryItems *matching_type = nullptr);

  void AutoComplete(CompletionRequest &request, FormatCategoryItems items);

  typedef std::shared_ptr<TypeCategoryImpl> SharedPointer;

private:
  FormatContainer m_format_cont;
  SummaryContainer m_summary_cont;
  FilterContainer m_filter_cont;
  SynthContainer m_synth_cont;

  bool m_enabled;

  IFormatChangeListener *m_change_listener;

  std::recursive_mutex m_mutex;

  ConstString m_name;

  std::vector<lldb::LanguageType> m_languages;

  uint32_t m_enabled_position = 0;

  void Enable(bool value, uint32_t position);

  void Disable() { Enable(false, UINT32_MAX); }

  bool IsApplicable(lldb::LanguageType lang);

  uint32_t GetLastEnabledPosition() { return m_enabled_position; }

  void SetEnabledPosition(uint32_t p) { m_enabled_position = p; }

  friend class FormatManager;
  friend class LanguageCategory;
  friend class TypeCategoryMap;

  friend class FormattersContainer<TypeFormatImpl>;

  friend class FormattersContainer<TypeSummaryImpl>;

  friend class FormattersContainer<TypeFilterImpl>;

  friend class FormattersContainer<ScriptedSyntheticChildren>;
};

} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_TYPECATEGORY_H
