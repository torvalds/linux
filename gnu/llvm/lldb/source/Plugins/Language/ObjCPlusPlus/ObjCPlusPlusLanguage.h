//===-- ObjCPlusPlusLanguage.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_OBJCPLUSPLUS_OBJCPLUSPLUSLANGUAGE_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_OBJCPLUSPLUS_OBJCPLUSPLUSLANGUAGE_H

#include "Plugins/Language/ClangCommon/ClangHighlighter.h"
#include "lldb/Target/Language.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ObjCPlusPlusLanguage : public Language {
  ClangHighlighter m_highlighter;

public:
  ObjCPlusPlusLanguage() = default;

  ~ObjCPlusPlusLanguage() override = default;

  lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeObjC_plus_plus;
  }

  llvm::StringRef GetUserEntryPointName() const override { return "main"; }

  llvm::StringRef GetNilReferenceSummaryString() override { return "nil"; }

  bool IsSourceFile(llvm::StringRef file_path) const override;

  const Highlighter *GetHighlighter() const override { return &m_highlighter; }

  // Static Functions
  static void Initialize();

  static void Terminate();

  static lldb_private::Language *CreateInstance(lldb::LanguageType language);

  llvm::StringRef GetInstanceVariableName() override { return "self"; }

  static llvm::StringRef GetPluginNameStatic() { return "objcplusplus"; }

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_OBJCPLUSPLUS_OBJCPLUSPLUSLANGUAGE_H
