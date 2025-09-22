//===-- SBLanguageRuntime.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBLANGUAGERUNTIME_H
#define LLDB_API_SBLANGUAGERUNTIME_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class SBLanguageRuntime {
public:
  static lldb::LanguageType GetLanguageTypeFromString(const char *string);

  static const char *GetNameForLanguageType(lldb::LanguageType language);

  /// Returns whether the given language is any version of C++.
  static bool LanguageIsCPlusPlus(lldb::LanguageType language);

  /// Returns whether the given language is Obj-C or Obj-C++.
  static bool LanguageIsObjC(lldb::LanguageType language);

  /// Returns whether the given language is any version of C, C++ or Obj-C.
  static bool LanguageIsCFamily(lldb::LanguageType language);

  /// Returns whether the given language supports exception breakpoints on
  /// throw statements.
  static bool SupportsExceptionBreakpointsOnThrow(lldb::LanguageType language);

  /// Returns whether the given language supports exception breakpoints on
  /// catch statements.
  static bool SupportsExceptionBreakpointsOnCatch(lldb::LanguageType language);

  /// Returns the keyword used for throw statements in the given language, e.g.
  /// Python uses \b raise. Returns \b nullptr if the language is not supported.
  static const char *GetThrowKeywordForLanguage(lldb::LanguageType language);

  /// Returns the keyword used for catch statements in the given language, e.g.
  /// Python uses \b except. Returns \b nullptr if the language is not
  /// supported.
  static const char *GetCatchKeywordForLanguage(lldb::LanguageType language);
};

} // namespace lldb

#endif // LLDB_API_SBLANGUAGERUNTIME_H
