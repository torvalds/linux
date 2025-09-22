//===-- TypeCategoryMap.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_TYPECATEGORYMAP_H
#define LLDB_DATAFORMATTERS_TYPECATEGORYMAP_H

#include <functional>
#include <list>
#include <map>
#include <mutex>

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/FormattersContainer.h"
#include "lldb/DataFormatters/TypeCategory.h"

namespace lldb_private {
class TypeCategoryMap {
private:
  typedef std::list<lldb::TypeCategoryImplSP> ActiveCategoriesList;
  typedef ActiveCategoriesList::iterator ActiveCategoriesIterator;

public:
  typedef ConstString KeyType;
  typedef std::map<KeyType, lldb::TypeCategoryImplSP> MapType;
  typedef MapType::iterator MapIterator;
  typedef std::function<bool(const lldb::TypeCategoryImplSP &)> ForEachCallback;

  typedef uint32_t Position;

  static const Position First = 0;
  static const Position Default = 1;
  static const Position Last = UINT32_MAX;

  TypeCategoryMap(IFormatChangeListener *lst);

  void Add(KeyType name, const lldb::TypeCategoryImplSP &entry);

  bool Delete(KeyType name);

  bool Enable(KeyType category_name, Position pos = Default);

  bool Disable(KeyType category_name);

  bool Enable(lldb::TypeCategoryImplSP category, Position pos = Default);

  bool Disable(lldb::TypeCategoryImplSP category);

  void EnableAllCategories();

  void DisableAllCategories();

  void Clear();

  bool Get(KeyType name, lldb::TypeCategoryImplSP &entry);

  void ForEach(ForEachCallback callback);

  lldb::TypeCategoryImplSP GetAtIndex(uint32_t);

  bool
  AnyMatches(const FormattersMatchCandidate &candidate_type,
             TypeCategoryImpl::FormatCategoryItems items =
                 TypeCategoryImpl::ALL_ITEM_TYPES,
             bool only_enabled = true, const char **matching_category = nullptr,
             TypeCategoryImpl::FormatCategoryItems *matching_type = nullptr);

  uint32_t GetCount() { return m_map.size(); }

  template <typename ImplSP> void Get(FormattersMatchData &, ImplSP &);

private:
  class delete_matching_categories {
    lldb::TypeCategoryImplSP ptr;

  public:
    delete_matching_categories(lldb::TypeCategoryImplSP p)
        : ptr(std::move(p)) {}

    bool operator()(const lldb::TypeCategoryImplSP &other) {
      return ptr.get() == other.get();
    }
  };

  std::recursive_mutex m_map_mutex;
  IFormatChangeListener *listener;

  MapType m_map;
  ActiveCategoriesList m_active_categories;
};
} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_TYPECATEGORYMAP_H
