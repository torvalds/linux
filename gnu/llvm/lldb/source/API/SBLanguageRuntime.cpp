//===-- SBLanguageRuntime.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBLanguageRuntime.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

lldb::LanguageType
SBLanguageRuntime::GetLanguageTypeFromString(const char *string) {
  LLDB_INSTRUMENT_VA(string);

  return Language::GetLanguageTypeFromString(llvm::StringRef(string));
}

const char *
SBLanguageRuntime::GetNameForLanguageType(lldb::LanguageType language) {
  LLDB_INSTRUMENT_VA(language);

  return Language::GetNameForLanguageType(language);
}

bool SBLanguageRuntime::LanguageIsCPlusPlus(lldb::LanguageType language) {
  return Language::LanguageIsCPlusPlus(language);
}

bool SBLanguageRuntime::LanguageIsObjC(lldb::LanguageType language) {
  return Language::LanguageIsObjC(language);
}

bool SBLanguageRuntime::LanguageIsCFamily(lldb::LanguageType language) {
  return Language::LanguageIsCFamily(language);
}

bool SBLanguageRuntime::SupportsExceptionBreakpointsOnThrow(
    lldb::LanguageType language) {
  if (Language *lang_plugin = Language::FindPlugin(language))
    return lang_plugin->SupportsExceptionBreakpointsOnThrow();
  return false;
}

bool SBLanguageRuntime::SupportsExceptionBreakpointsOnCatch(
    lldb::LanguageType language) {
  if (Language *lang_plugin = Language::FindPlugin(language))
    return lang_plugin->SupportsExceptionBreakpointsOnCatch();
  return false;
}

const char *
SBLanguageRuntime::GetThrowKeywordForLanguage(lldb::LanguageType language) {
  if (Language *lang_plugin = Language::FindPlugin(language))
    return ConstString(lang_plugin->GetThrowKeyword()).AsCString();
  return nullptr;
}

const char *
SBLanguageRuntime::GetCatchKeywordForLanguage(lldb::LanguageType language) {
  if (Language *lang_plugin = Language::FindPlugin(language))
    return ConstString(lang_plugin->GetCatchKeyword()).AsCString();
  return nullptr;
}
