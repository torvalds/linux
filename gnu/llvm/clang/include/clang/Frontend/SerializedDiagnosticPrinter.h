//===--- SerializedDiagnosticPrinter.h - Diagnostics serializer -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_SERIALIZEDDIAGNOSTICPRINTER_H
#define LLVM_CLANG_FRONTEND_SERIALIZEDDIAGNOSTICPRINTER_H

#include "clang/Basic/LLVM.h"
#include "clang/Frontend/SerializedDiagnostics.h"
#include "llvm/Bitstream/BitstreamWriter.h"

namespace llvm {
class raw_ostream;
}

namespace clang {
class DiagnosticConsumer;
class DiagnosticOptions;

namespace serialized_diags {

/// Returns a DiagnosticConsumer that serializes diagnostics to
///  a bitcode file.
///
/// The created DiagnosticConsumer is designed for quick and lightweight
/// transfer of diagnostics to the enclosing build system (e.g., an IDE).
/// This allows wrapper tools for Clang to get diagnostics from Clang
/// (via libclang) without needing to parse Clang's command line output.
///
std::unique_ptr<DiagnosticConsumer> create(StringRef OutputFile,
                                           DiagnosticOptions *Diags,
                                           bool MergeChildRecords = false);

} // end serialized_diags namespace
} // end clang namespace

#endif
