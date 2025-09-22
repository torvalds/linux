//===-- DiagnosticManager.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_DIAGNOSTICMANAGER_H
#define LLDB_EXPRESSION_DIAGNOSTICMANAGER_H

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace lldb_private {

enum DiagnosticOrigin {
  eDiagnosticOriginUnknown = 0,
  eDiagnosticOriginLLDB,
  eDiagnosticOriginClang,
  eDiagnosticOriginSwift,
  eDiagnosticOriginLLVM
};

const uint32_t LLDB_INVALID_COMPILER_ID = UINT32_MAX;

class Diagnostic {
  friend class DiagnosticManager;

public:
  DiagnosticOrigin getKind() const { return m_origin; }

  static bool classof(const Diagnostic *diag) {
    DiagnosticOrigin kind = diag->getKind();
    switch (kind) {
    case eDiagnosticOriginUnknown:
    case eDiagnosticOriginLLDB:
    case eDiagnosticOriginLLVM:
      return true;
    case eDiagnosticOriginClang:
    case eDiagnosticOriginSwift:
      return false;
    }
  }

  Diagnostic(llvm::StringRef message, lldb::Severity severity,
             DiagnosticOrigin origin, uint32_t compiler_id)
      : m_message(message), m_severity(severity), m_origin(origin),
        m_compiler_id(compiler_id) {}

  Diagnostic(const Diagnostic &rhs)
      : m_message(rhs.m_message), m_severity(rhs.m_severity),
        m_origin(rhs.m_origin), m_compiler_id(rhs.m_compiler_id) {}

  virtual ~Diagnostic() = default;

  virtual bool HasFixIts() const { return false; }

  lldb::Severity GetSeverity() const { return m_severity; }

  uint32_t GetCompilerID() const { return m_compiler_id; }

  llvm::StringRef GetMessage() const { return m_message; }

  void AppendMessage(llvm::StringRef message,
                     bool precede_with_newline = true) {
    if (precede_with_newline)
      m_message.push_back('\n');
    m_message += message;
  }

protected:
  std::string m_message;
  lldb::Severity m_severity;
  DiagnosticOrigin m_origin;
  uint32_t m_compiler_id; // Compiler-specific diagnostic ID
};

typedef std::vector<std::unique_ptr<Diagnostic>> DiagnosticList;

class DiagnosticManager {
public:
  void Clear() {
    m_diagnostics.clear();
    m_fixed_expression.clear();
  }

  const DiagnosticList &Diagnostics() { return m_diagnostics; }

  bool HasFixIts() const {
    return llvm::any_of(m_diagnostics,
                        [](const std::unique_ptr<Diagnostic> &diag) {
                          return diag->HasFixIts();
                        });
  }

  void AddDiagnostic(llvm::StringRef message, lldb::Severity severity,
                     DiagnosticOrigin origin,
                     uint32_t compiler_id = LLDB_INVALID_COMPILER_ID) {
    m_diagnostics.emplace_back(
        std::make_unique<Diagnostic>(message, severity, origin, compiler_id));
  }

  void AddDiagnostic(std::unique_ptr<Diagnostic> diagnostic) {
    if (diagnostic)
      m_diagnostics.push_back(std::move(diagnostic));
  }

  /// Moves over the contents of a second diagnostic manager over. Leaves other
  /// diagnostic manager in an empty state.
  void Consume(DiagnosticManager &&other) {
    std::move(other.m_diagnostics.begin(), other.m_diagnostics.end(),
              std::back_inserter(m_diagnostics));
    m_fixed_expression = std::move(other.m_fixed_expression);
    other.Clear();
  }

  size_t Printf(lldb::Severity severity, const char *format, ...)
      __attribute__((format(printf, 3, 4)));
  void PutString(lldb::Severity severity, llvm::StringRef str);

  void AppendMessageToDiagnostic(llvm::StringRef str) {
    if (!m_diagnostics.empty())
      m_diagnostics.back()->AppendMessage(str);
  }

  // Returns a string containing errors in this format:
  //
  // "error: error text\n
  // warning: warning text\n
  // remark text\n"
  std::string GetString(char separator = '\n');

  void Dump(Log *log);

  const std::string &GetFixedExpression() { return m_fixed_expression; }

  // Moves fixed_expression to the internal storage.
  void SetFixedExpression(std::string fixed_expression) {
    m_fixed_expression = std::move(fixed_expression);
  }

protected:
  DiagnosticList m_diagnostics;
  std::string m_fixed_expression;
};
}

#endif // LLDB_EXPRESSION_DIAGNOSTICMANAGER_H
