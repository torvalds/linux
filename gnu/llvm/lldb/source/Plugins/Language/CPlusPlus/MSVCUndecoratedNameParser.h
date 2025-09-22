//===-- MSVCUndecoratedNameParser.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_MSVCUNDECORATEDNAMEPARSER_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_MSVCUNDECORATEDNAMEPARSER_H

#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

class MSVCUndecoratedNameSpecifier {
public:
  MSVCUndecoratedNameSpecifier(llvm::StringRef full_name,
                               llvm::StringRef base_name)
      : m_full_name(full_name), m_base_name(base_name) {}

  llvm::StringRef GetFullName() const { return m_full_name; }
  llvm::StringRef GetBaseName() const { return m_base_name; }

private:
  llvm::StringRef m_full_name;
  llvm::StringRef m_base_name;
};

class MSVCUndecoratedNameParser {
public:
  explicit MSVCUndecoratedNameParser(llvm::StringRef name);

  llvm::ArrayRef<MSVCUndecoratedNameSpecifier> GetSpecifiers() const {
    return m_specifiers;
  }

  static bool IsMSVCUndecoratedName(llvm::StringRef name);
  static bool ExtractContextAndIdentifier(llvm::StringRef name,
                                          llvm::StringRef &context,
                                          llvm::StringRef &identifier);

  static llvm::StringRef DropScope(llvm::StringRef name);

private:
  std::vector<MSVCUndecoratedNameSpecifier> m_specifiers;
};

#endif
