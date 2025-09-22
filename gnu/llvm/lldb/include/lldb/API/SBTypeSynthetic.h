//===-- SBTypeSynthetic.h -----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTYPESYNTHETIC_H
#define LLDB_API_SBTYPESYNTHETIC_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBTypeSynthetic {
public:
  SBTypeSynthetic();

  static SBTypeSynthetic
  CreateWithClassName(const char *data,
                      uint32_t options = 0); // see lldb::eTypeOption values

  static SBTypeSynthetic
  CreateWithScriptCode(const char *data,
                       uint32_t options = 0); // see lldb::eTypeOption values

  SBTypeSynthetic(const lldb::SBTypeSynthetic &rhs);

  ~SBTypeSynthetic();

  explicit operator bool() const;

  bool IsValid() const;

  bool IsClassCode();

  bool IsClassName();

  const char *GetData();

  void SetClassName(const char *data);

  void SetClassCode(const char *data);

  uint32_t GetOptions();

  void SetOptions(uint32_t);

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  lldb::SBTypeSynthetic &operator=(const lldb::SBTypeSynthetic &rhs);

  bool IsEqualTo(lldb::SBTypeSynthetic &rhs);

  bool operator==(lldb::SBTypeSynthetic &rhs);

  bool operator!=(lldb::SBTypeSynthetic &rhs);

protected:
  friend class SBDebugger;
  friend class SBTypeCategory;
  friend class SBValue;

  lldb::ScriptedSyntheticChildrenSP GetSP();

  void SetSP(const lldb::ScriptedSyntheticChildrenSP &typefilter_impl_sp);

  lldb::ScriptedSyntheticChildrenSP m_opaque_sp;

  SBTypeSynthetic(const lldb::ScriptedSyntheticChildrenSP &);

  bool CopyOnWrite_Impl();
};

} // namespace lldb

#endif // LLDB_API_SBTYPESYNTHETIC_H
