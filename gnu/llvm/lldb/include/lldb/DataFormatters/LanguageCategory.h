//===-- LanguageCategory.h----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_LANGUAGECATEGORY_H
#define LLDB_DATAFORMATTERS_LANGUAGECATEGORY_H

#include "lldb/DataFormatters/FormatCache.h"
#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/lldb-public.h"

#include <memory>

namespace lldb_private {

class LanguageCategory {
public:
  typedef std::unique_ptr<LanguageCategory> UniquePointer;

  LanguageCategory(lldb::LanguageType lang_type);

  template <typename ImplSP>
  bool Get(FormattersMatchData &match_data, ImplSP &format_sp);
  template <typename ImplSP>
  bool GetHardcoded(FormatManager &fmt_mgr, FormattersMatchData &match_data,
                    ImplSP &format_sp);

  lldb::TypeCategoryImplSP GetCategory() const;

  FormatCache &GetFormatCache();

  void Enable();

  void Disable();

  bool IsEnabled();

private:
  lldb::TypeCategoryImplSP m_category_sp;

  HardcodedFormatters::HardcodedFormatFinder m_hardcoded_formats;
  HardcodedFormatters::HardcodedSummaryFinder m_hardcoded_summaries;
  HardcodedFormatters::HardcodedSyntheticFinder m_hardcoded_synthetics;

  template <typename ImplSP>
  auto &GetHardcodedFinder();

  lldb_private::FormatCache m_format_cache;

  bool m_enabled;
};

} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_LANGUAGECATEGORY_H
