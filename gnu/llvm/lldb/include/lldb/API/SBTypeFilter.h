//===-- SBTypeFilter.h --------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTYPEFILTER_H
#define LLDB_API_SBTYPEFILTER_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBTypeFilter {
public:
  SBTypeFilter();

  SBTypeFilter(uint32_t options); // see lldb::eTypeOption values

  SBTypeFilter(const lldb::SBTypeFilter &rhs);

  ~SBTypeFilter();

  explicit operator bool() const;

  bool IsValid() const;

  uint32_t GetNumberOfExpressionPaths();

  const char *GetExpressionPathAtIndex(uint32_t i);

  bool ReplaceExpressionPathAtIndex(uint32_t i, const char *item);

  void AppendExpressionPath(const char *item);

  void Clear();

  uint32_t GetOptions();

  void SetOptions(uint32_t);

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  lldb::SBTypeFilter &operator=(const lldb::SBTypeFilter &rhs);

  bool IsEqualTo(lldb::SBTypeFilter &rhs);

  bool operator==(lldb::SBTypeFilter &rhs);

  bool operator!=(lldb::SBTypeFilter &rhs);

protected:
  friend class SBDebugger;
  friend class SBTypeCategory;
  friend class SBValue;

  lldb::TypeFilterImplSP GetSP();

  void SetSP(const lldb::TypeFilterImplSP &typefilter_impl_sp);

  lldb::TypeFilterImplSP m_opaque_sp;

  SBTypeFilter(const lldb::TypeFilterImplSP &);

  bool CopyOnWrite_Impl();
};

} // namespace lldb

#endif // LLDB_API_SBTYPEFILTER_H
