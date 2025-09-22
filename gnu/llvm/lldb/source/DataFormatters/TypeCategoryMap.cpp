//===-- TypeCategoryMap.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/TypeCategoryMap.h"

#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

TypeCategoryMap::TypeCategoryMap(IFormatChangeListener *lst)
    : m_map_mutex(), listener(lst), m_map(), m_active_categories() {
  ConstString default_cs("default");
  lldb::TypeCategoryImplSP default_sp =
      lldb::TypeCategoryImplSP(new TypeCategoryImpl(listener, default_cs));
  Add(default_cs, default_sp);
  Enable(default_cs, First);
}

void TypeCategoryMap::Add(KeyType name, const TypeCategoryImplSP &entry) {
  {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    m_map[name] = entry;
  }
  // Release the mutex to avoid a potential deadlock between
  // TypeCategoryMap::m_map_mutex and
  // FormatManager::m_language_categories_mutex which can be acquired in
  // reverse order when calling FormatManager::Changed.
  if (listener)
    listener->Changed();
}

bool TypeCategoryMap::Delete(KeyType name) {
  {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    MapIterator iter = m_map.find(name);
    if (iter == m_map.end())
      return false;
    m_map.erase(name);
    Disable(name);
  }
  // Release the mutex to avoid a potential deadlock between
  // TypeCategoryMap::m_map_mutex and
  // FormatManager::m_language_categories_mutex which can be acquired in
  // reverse order when calling FormatManager::Changed.
  if (listener)
    listener->Changed();
  return true;
}

bool TypeCategoryMap::Enable(KeyType category_name, Position pos) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
  TypeCategoryImplSP category;
  if (!Get(category_name, category))
    return false;
  return Enable(category, pos);
}

bool TypeCategoryMap::Disable(KeyType category_name) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
  TypeCategoryImplSP category;
  if (!Get(category_name, category))
    return false;
  return Disable(category);
}

bool TypeCategoryMap::Enable(TypeCategoryImplSP category, Position pos) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
  if (category.get()) {
    Position pos_w = pos;
    if (pos == First || m_active_categories.size() == 0)
      m_active_categories.push_front(category);
    else if (pos == Last || pos == m_active_categories.size())
      m_active_categories.push_back(category);
    else if (pos < m_active_categories.size()) {
      ActiveCategoriesList::iterator iter = m_active_categories.begin();
      while (pos_w) {
        pos_w--, iter++;
      }
      m_active_categories.insert(iter, category);
    } else
      return false;
    category->Enable(true, pos);
    return true;
  }
  return false;
}

bool TypeCategoryMap::Disable(TypeCategoryImplSP category) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
  if (category.get()) {
    m_active_categories.remove_if(delete_matching_categories(category));
    category->Disable();
    return true;
  }
  return false;
}

void TypeCategoryMap::EnableAllCategories() {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
  std::vector<TypeCategoryImplSP> sorted_categories(m_map.size(), TypeCategoryImplSP());
  MapType::iterator iter = m_map.begin(), end = m_map.end();
  for (; iter != end; ++iter) {
    if (iter->second->IsEnabled())
      continue;
    auto pos = iter->second->GetLastEnabledPosition();
    if (pos >= sorted_categories.size()) {
      auto iter = std::find_if(
          sorted_categories.begin(), sorted_categories.end(),
          [](const TypeCategoryImplSP &sp) -> bool { return sp.get() == nullptr; });
      pos = std::distance(sorted_categories.begin(), iter);
    }
    sorted_categories.at(pos) = iter->second;
  }
  decltype(sorted_categories)::iterator viter = sorted_categories.begin(),
                                        vend = sorted_categories.end();
  for (; viter != vend; viter++)
    if (*viter)
      Enable(*viter, Last);
}

void TypeCategoryMap::DisableAllCategories() {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
  for (Position p = First; !m_active_categories.empty(); p++) {
    m_active_categories.front()->SetEnabledPosition(p);
    Disable(m_active_categories.front());
  }
}

void TypeCategoryMap::Clear() {
  {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
    m_map.clear();
    m_active_categories.clear();
  }
  // Release the mutex to avoid a potential deadlock between
  // TypeCategoryMap::m_map_mutex and
  // FormatManager::m_language_categories_mutex which can be acquired in
  // reverse order when calling FormatManager::Changed.
  if (listener)
    listener->Changed();
}

bool TypeCategoryMap::Get(KeyType name, TypeCategoryImplSP &entry) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);
  MapIterator iter = m_map.find(name);
  if (iter == m_map.end())
    return false;
  entry = iter->second;
  return true;
}

bool TypeCategoryMap::AnyMatches(
    const FormattersMatchCandidate &candidate_type,
    TypeCategoryImpl::FormatCategoryItems items, bool only_enabled,
    const char **matching_category,
    TypeCategoryImpl::FormatCategoryItems *matching_type) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);

  MapIterator pos, end = m_map.end();
  for (pos = m_map.begin(); pos != end; pos++) {
    if (pos->second->AnyMatches(candidate_type, items, only_enabled,
                                matching_category, matching_type))
      return true;
  }
  return false;
}

template <typename ImplSP>
void TypeCategoryMap::Get(FormattersMatchData &match_data, ImplSP &retval) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);

  ActiveCategoriesIterator begin, end = m_active_categories.end();

  Log *log = GetLog(LLDBLog::DataFormatters);

  if (log) {
    for (auto match : match_data.GetMatchesVector()) {
      LLDB_LOGF(
          log,
          "[%s] candidate match = %s %s %s %s",
          __FUNCTION__,
          match.GetTypeName().GetCString(),
          match.DidStripPointer() ? "strip-pointers" : "no-strip-pointers",
          match.DidStripReference() ? "strip-reference" : "no-strip-reference",
          match.DidStripTypedef() ? "strip-typedef" : "no-strip-typedef");
    }
  }

  for (begin = m_active_categories.begin(); begin != end; begin++) {
    lldb::TypeCategoryImplSP category_sp = *begin;
    ImplSP current_format;
    LLDB_LOGF(log, "[%s] Trying to use category %s", __FUNCTION__,
              category_sp->GetName());
    if (!category_sp->Get(
            match_data.GetValueObject().GetObjectRuntimeLanguage(),
            match_data.GetMatchesVector(), current_format))
      continue;

    retval = std::move(current_format);
    return;
  }
  LLDB_LOGF(log, "[%s] nothing found - returning empty SP", __FUNCTION__);
}

/// Explicit instantiations for the three types.
/// \{
template void
TypeCategoryMap::Get<lldb::TypeFormatImplSP>(FormattersMatchData &match_data,
                                             lldb::TypeFormatImplSP &retval);
template void
TypeCategoryMap::Get<lldb::TypeSummaryImplSP>(FormattersMatchData &match_data,
                                              lldb::TypeSummaryImplSP &retval);
template void TypeCategoryMap::Get<lldb::SyntheticChildrenSP>(
    FormattersMatchData &match_data, lldb::SyntheticChildrenSP &retval);
/// \}

void TypeCategoryMap::ForEach(ForEachCallback callback) {
  if (callback) {
    std::lock_guard<std::recursive_mutex> guard(m_map_mutex);

    // loop through enabled categories in respective order
    {
      ActiveCategoriesIterator begin, end = m_active_categories.end();
      for (begin = m_active_categories.begin(); begin != end; begin++) {
        lldb::TypeCategoryImplSP category = *begin;
        if (!callback(category))
          break;
      }
    }

    // loop through disabled categories in just any order
    {
      MapIterator pos, end = m_map.end();
      for (pos = m_map.begin(); pos != end; pos++) {
        if (pos->second->IsEnabled())
          continue;
        if (!callback(pos->second))
          break;
      }
    }
  }
}

TypeCategoryImplSP TypeCategoryMap::GetAtIndex(uint32_t index) {
  std::lock_guard<std::recursive_mutex> guard(m_map_mutex);

  if (index < m_map.size()) {
    MapIterator pos, end = m_map.end();
    for (pos = m_map.begin(); pos != end; pos++) {
      if (index == 0)
        return pos->second;
      index--;
    }
  }

  return TypeCategoryImplSP();
}
