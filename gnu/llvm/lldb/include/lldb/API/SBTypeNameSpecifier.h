//===-- SBTypeNameSpecifier.h --------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTYPENAMESPECIFIER_H
#define LLDB_API_SBTYPENAMESPECIFIER_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBTypeNameSpecifier {
public:
  SBTypeNameSpecifier();

  SBTypeNameSpecifier(const char *name, bool is_regex = false);

  SBTypeNameSpecifier(const char *name,
                      lldb::FormatterMatchType match_type);

  SBTypeNameSpecifier(SBType type);

  SBTypeNameSpecifier(const lldb::SBTypeNameSpecifier &rhs);

  ~SBTypeNameSpecifier();

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetName();

  SBType GetType();

  lldb::FormatterMatchType GetMatchType();

  bool IsRegex();

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  lldb::SBTypeNameSpecifier &operator=(const lldb::SBTypeNameSpecifier &rhs);

  bool IsEqualTo(lldb::SBTypeNameSpecifier &rhs);

  bool operator==(lldb::SBTypeNameSpecifier &rhs);

  bool operator!=(lldb::SBTypeNameSpecifier &rhs);

protected:
  friend class SBDebugger;
  friend class SBTypeCategory;

  lldb::TypeNameSpecifierImplSP GetSP();

  void SetSP(const lldb::TypeNameSpecifierImplSP &type_namespec_sp);

  lldb::TypeNameSpecifierImplSP m_opaque_sp;

  SBTypeNameSpecifier(const lldb::TypeNameSpecifierImplSP &);
};

} // namespace lldb

#endif // LLDB_API_SBTYPENAMESPECIFIER_H
