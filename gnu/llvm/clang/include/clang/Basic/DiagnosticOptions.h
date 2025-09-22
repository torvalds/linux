//===- DiagnosticOptions.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DIAGNOSTICOPTIONS_H
#define LLVM_CLANG_BASIC_DIAGNOSTICOPTIONS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include <string>
#include <type_traits>
#include <vector>

namespace llvm {
namespace opt {
class ArgList;
} // namespace opt
} // namespace llvm

namespace clang {
class DiagnosticsEngine;

/// Specifies which overload candidates to display when overload
/// resolution fails.
enum OverloadsShown : unsigned {
  /// Show all overloads.
  Ovl_All,

  /// Show just the "best" overload candidates.
  Ovl_Best
};

/// A bitmask representing the diagnostic levels used by
/// VerifyDiagnosticConsumer.
enum class DiagnosticLevelMask : unsigned {
  None    = 0,
  Note    = 1 << 0,
  Remark  = 1 << 1,
  Warning = 1 << 2,
  Error   = 1 << 3,
  All     = Note | Remark | Warning | Error
};

inline DiagnosticLevelMask operator~(DiagnosticLevelMask M) {
  using UT = std::underlying_type_t<DiagnosticLevelMask>;
  return static_cast<DiagnosticLevelMask>(~static_cast<UT>(M));
}

inline DiagnosticLevelMask operator|(DiagnosticLevelMask LHS,
                                     DiagnosticLevelMask RHS) {
  using UT = std::underlying_type_t<DiagnosticLevelMask>;
  return static_cast<DiagnosticLevelMask>(
    static_cast<UT>(LHS) | static_cast<UT>(RHS));
}

inline DiagnosticLevelMask operator&(DiagnosticLevelMask LHS,
                                     DiagnosticLevelMask RHS) {
  using UT = std::underlying_type_t<DiagnosticLevelMask>;
  return static_cast<DiagnosticLevelMask>(
    static_cast<UT>(LHS) & static_cast<UT>(RHS));
}

raw_ostream& operator<<(raw_ostream& Out, DiagnosticLevelMask M);

/// Options for controlling the compiler diagnostics engine.
class DiagnosticOptions : public RefCountedBase<DiagnosticOptions>{
  friend bool ParseDiagnosticArgs(DiagnosticOptions &, llvm::opt::ArgList &,
                                  clang::DiagnosticsEngine *, bool);

  friend class CompilerInvocation;
  friend class CompilerInvocationBase;

public:
  enum TextDiagnosticFormat { Clang, MSVC, Vi, SARIF };

  // Default values.
  enum {
    DefaultTabStop = 8,
    MaxTabStop = 100,
    DefaultMacroBacktraceLimit = 6,
    DefaultTemplateBacktraceLimit = 10,
    DefaultConstexprBacktraceLimit = 10,
    DefaultSpellCheckingLimit = 50,
    DefaultSnippetLineLimit = 16,
    DefaultShowLineNumbers = 1,
  };

  // Define simple diagnostic options (with no accessors).
#define DIAGOPT(Name, Bits, Default) unsigned Name : Bits;
#define ENUM_DIAGOPT(Name, Type, Bits, Default)
#include "clang/Basic/DiagnosticOptions.def"

protected:
  // Define diagnostic options of enumeration type. These are private, and will
  // have accessors (below).
#define DIAGOPT(Name, Bits, Default)
#define ENUM_DIAGOPT(Name, Type, Bits, Default) unsigned Name : Bits;
#include "clang/Basic/DiagnosticOptions.def"

public:
  /// The file to log diagnostic output to.
  std::string DiagnosticLogFile;

  /// The file to serialize diagnostics to (non-appending).
  std::string DiagnosticSerializationFile;

  /// The list of -W... options used to alter the diagnostic mappings, with the
  /// prefixes removed.
  std::vector<std::string> Warnings;

  /// The list of prefixes from -Wundef-prefix=... used to generate warnings
  /// for undefined macros.
  std::vector<std::string> UndefPrefixes;

  /// The list of -R... options used to alter the diagnostic mappings, with the
  /// prefixes removed.
  std::vector<std::string> Remarks;

  /// The prefixes for comment directives sought by -verify ("expected" by
  /// default).
  std::vector<std::string> VerifyPrefixes;

  /// The list of -Wsystem-headers-in-module=... options used to override
  /// whether -Wsystem-headers is enabled on a per-module basis.
  std::vector<std::string> SystemHeaderWarningsModules;

public:
  // Define accessors/mutators for diagnostic options of enumeration type.
#define DIAGOPT(Name, Bits, Default)
#define ENUM_DIAGOPT(Name, Type, Bits, Default) \
  Type get##Name() const { return static_cast<Type>(Name); } \
  void set##Name(Type Value) { Name = static_cast<unsigned>(Value); }
#include "clang/Basic/DiagnosticOptions.def"

  DiagnosticOptions() {
#define DIAGOPT(Name, Bits, Default) Name = Default;
#define ENUM_DIAGOPT(Name, Type, Bits, Default) set##Name(Default);
#include "clang/Basic/DiagnosticOptions.def"
  }
};

using TextDiagnosticFormat = DiagnosticOptions::TextDiagnosticFormat;

} // namespace clang

#endif // LLVM_CLANG_BASIC_DIAGNOSTICOPTIONS_H
