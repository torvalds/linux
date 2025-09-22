//===-- APINotesYAMLCompiler.h - API Notes YAML Format Reader ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_APINOTES_APINOTESYAMLCOMPILER_H
#define LLVM_CLANG_APINOTES_APINOTESYAMLCOMPILER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {
class FileEntry;
} // namespace clang

namespace clang {
namespace api_notes {
/// Parses the APINotes YAML content and writes the representation back to the
/// specified stream.  This provides a means of testing the YAML processing of
/// the APINotes format.
bool parseAndDumpAPINotes(llvm::StringRef YI, llvm::raw_ostream &OS);

/// Converts API notes from YAML format to binary format.
bool compileAPINotes(llvm::StringRef YAMLInput, const FileEntry *SourceFile,
                     llvm::raw_ostream &OS,
                     llvm::SourceMgr::DiagHandlerTy DiagHandler = nullptr,
                     void *DiagHandlerCtxt = nullptr);
} // namespace api_notes
} // namespace clang

#endif
