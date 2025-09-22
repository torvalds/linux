//===-- ClangDiagnostic.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGDIAGNOSTIC_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGDIAGNOSTIC_H

#include <vector>

#include "clang/Basic/Diagnostic.h"

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "lldb/Expression/DiagnosticManager.h"

namespace lldb_private {

class ClangDiagnostic : public Diagnostic {
public:
  typedef std::vector<clang::FixItHint> FixItList;

  static inline bool classof(const ClangDiagnostic *) { return true; }
  static inline bool classof(const Diagnostic *diag) {
    return diag->getKind() == eDiagnosticOriginClang;
  }

  ClangDiagnostic(llvm::StringRef message, lldb::Severity severity,
                  uint32_t compiler_id)
      : Diagnostic(message, severity, eDiagnosticOriginClang, compiler_id) {}

  ~ClangDiagnostic() override = default;

  bool HasFixIts() const override { return !m_fixit_vec.empty(); }

  void AddFixitHint(const clang::FixItHint &fixit) {
    m_fixit_vec.push_back(fixit);
  }

  const FixItList &FixIts() const { return m_fixit_vec; }
private:
  FixItList m_fixit_vec;
};

} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGDIAGNOSTIC_H
