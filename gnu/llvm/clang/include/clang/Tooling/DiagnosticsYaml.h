//===-- DiagnosticsYaml.h -- Serialiazation for Diagnosticss ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the structure of a YAML document for serializing
/// diagnostics.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_DIAGNOSTICSYAML_H
#define LLVM_CLANG_TOOLING_DIAGNOSTICSYAML_H

#include "clang/Tooling/Core/Diagnostic.h"
#include "clang/Tooling/ReplacementsYaml.h"
#include "llvm/Support/YAMLTraits.h"
#include <string>

LLVM_YAML_IS_SEQUENCE_VECTOR(clang::tooling::Diagnostic)
LLVM_YAML_IS_SEQUENCE_VECTOR(clang::tooling::DiagnosticMessage)
LLVM_YAML_IS_SEQUENCE_VECTOR(clang::tooling::FileByteRange)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<clang::tooling::FileByteRange> {
  static void mapping(IO &Io, clang::tooling::FileByteRange &R) {
    Io.mapRequired("FilePath", R.FilePath);
    Io.mapRequired("FileOffset", R.FileOffset);
    Io.mapRequired("Length", R.Length);
  }
};

template <> struct MappingTraits<clang::tooling::DiagnosticMessage> {
  static void mapping(IO &Io, clang::tooling::DiagnosticMessage &M) {
    Io.mapRequired("Message", M.Message);
    Io.mapOptional("FilePath", M.FilePath);
    Io.mapOptional("FileOffset", M.FileOffset);
    std::vector<clang::tooling::Replacement> Fixes;
    for (auto &Replacements : M.Fix) {
      llvm::append_range(Fixes, Replacements.second);
    }
    Io.mapRequired("Replacements", Fixes);
    for (auto &Fix : Fixes) {
      llvm::Error Err = M.Fix[Fix.getFilePath()].add(Fix);
      if (Err) {
        // FIXME: Implement better conflict handling.
        llvm::errs() << "Fix conflicts with existing fix: "
                     << llvm::toString(std::move(Err)) << "\n";
      }
    }
    Io.mapOptional("Ranges", M.Ranges);
  }
};

template <> struct MappingTraits<clang::tooling::Diagnostic> {
  /// Helper to (de)serialize a Diagnostic since we don't have direct
  /// access to its data members.
  class NormalizedDiagnostic {
  public:
    NormalizedDiagnostic(const IO &)
        : DiagLevel(clang::tooling::Diagnostic::Level::Warning) {}

    NormalizedDiagnostic(const IO &, const clang::tooling::Diagnostic &D)
        : DiagnosticName(D.DiagnosticName), Message(D.Message), Notes(D.Notes),
          DiagLevel(D.DiagLevel), BuildDirectory(D.BuildDirectory) {}

    clang::tooling::Diagnostic denormalize(const IO &) {
      return clang::tooling::Diagnostic(DiagnosticName, Message, Notes,
                                        DiagLevel, BuildDirectory);
    }

    std::string DiagnosticName;
    clang::tooling::DiagnosticMessage Message;
    SmallVector<clang::tooling::DiagnosticMessage, 1> Notes;
    clang::tooling::Diagnostic::Level DiagLevel;
    std::string BuildDirectory;
  };

  static void mapping(IO &Io, clang::tooling::Diagnostic &D) {
    MappingNormalization<NormalizedDiagnostic, clang::tooling::Diagnostic> Keys(
        Io, D);
    Io.mapRequired("DiagnosticName", Keys->DiagnosticName);
    Io.mapRequired("DiagnosticMessage", Keys->Message);
    Io.mapOptional("Notes", Keys->Notes);
    Io.mapOptional("Level", Keys->DiagLevel);
    Io.mapOptional("BuildDirectory", Keys->BuildDirectory);
  }
};

/// Specialized MappingTraits to describe how a
/// TranslationUnitDiagnostics is (de)serialized.
template <> struct MappingTraits<clang::tooling::TranslationUnitDiagnostics> {
  static void mapping(IO &Io, clang::tooling::TranslationUnitDiagnostics &Doc) {
    Io.mapRequired("MainSourceFile", Doc.MainSourceFile);
    Io.mapRequired("Diagnostics", Doc.Diagnostics);
  }
};

template <> struct ScalarEnumerationTraits<clang::tooling::Diagnostic::Level> {
  static void enumeration(IO &IO, clang::tooling::Diagnostic::Level &Value) {
    IO.enumCase(Value, "Warning", clang::tooling::Diagnostic::Warning);
    IO.enumCase(Value, "Error", clang::tooling::Diagnostic::Error);
    IO.enumCase(Value, "Remark", clang::tooling::Diagnostic::Remark);
  }
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_CLANG_TOOLING_DIAGNOSTICSYAML_H
