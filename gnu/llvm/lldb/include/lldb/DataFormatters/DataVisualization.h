//===-- DataVisualization.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_DATAVISUALIZATION_H
#define LLDB_DATAFORMATTERS_DATAVISUALIZATION_H

#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Utility/ConstString.h"

namespace lldb_private {

// this class is the high-level front-end of LLDB Data Visualization code in
// FormatManager.h/cpp is the low-level implementation of this feature clients
// should refer to this class as the entry-point into the data formatters
// unless they have a good reason to bypass this and go to the backend
class DataVisualization {
public:
  // use this call to force the FM to consider itself updated even when there
  // is no apparent reason for that
  static void ForceUpdate();

  static uint32_t GetCurrentRevision();

  static bool ShouldPrintAsOneLiner(ValueObject &valobj);

  static lldb::TypeFormatImplSP GetFormat(ValueObject &valobj,
                                          lldb::DynamicValueType use_dynamic);

  static lldb::TypeFormatImplSP
  GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp);

  static lldb::TypeSummaryImplSP
  GetSummaryFormat(ValueObject &valobj, lldb::DynamicValueType use_dynamic);

  static lldb::TypeSummaryImplSP
  GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp);

  static lldb::TypeFilterImplSP
  GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp);

  static lldb::ScriptedSyntheticChildrenSP
  GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp);

  static lldb::SyntheticChildrenSP
  GetSyntheticChildren(ValueObject &valobj, lldb::DynamicValueType use_dynamic);

  static bool
  AnyMatches(const FormattersMatchCandidate &candidate_type,
             TypeCategoryImpl::FormatCategoryItems items =
                 TypeCategoryImpl::ALL_ITEM_TYPES,
             bool only_enabled = true, const char **matching_category = nullptr,
             TypeCategoryImpl::FormatCategoryItems *matching_type = nullptr);

  class NamedSummaryFormats {
  public:
    static bool GetSummaryFormat(ConstString type,
                                 lldb::TypeSummaryImplSP &entry);

    static void Add(ConstString type,
                    const lldb::TypeSummaryImplSP &entry);

    static bool Delete(ConstString type);

    static void Clear();

    static void ForEach(std::function<bool(const TypeMatcher &,
                                           const lldb::TypeSummaryImplSP &)>
                            callback);

    static uint32_t GetCount();
  };

  class Categories {
  public:
    static bool GetCategory(ConstString category,
                            lldb::TypeCategoryImplSP &entry,
                            bool allow_create = true);

    static bool GetCategory(lldb::LanguageType language,
                            lldb::TypeCategoryImplSP &entry);

    static void Add(ConstString category);

    static bool Delete(ConstString category);

    static void Clear();

    static void Clear(ConstString category);

    static void Enable(ConstString category,
                       TypeCategoryMap::Position = TypeCategoryMap::Default);

    static void Enable(lldb::LanguageType lang_type);

    static void Disable(ConstString category);

    static void Disable(lldb::LanguageType lang_type);

    static void Enable(const lldb::TypeCategoryImplSP &category,
                       TypeCategoryMap::Position = TypeCategoryMap::Default);

    static void Disable(const lldb::TypeCategoryImplSP &category);

    static void EnableStar();

    static void DisableStar();

    static void ForEach(TypeCategoryMap::ForEachCallback callback);

    static uint32_t GetCount();

    static lldb::TypeCategoryImplSP GetCategoryAtIndex(size_t);
  };
};

} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_DATAVISUALIZATION_H
