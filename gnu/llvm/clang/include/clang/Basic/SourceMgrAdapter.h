//=== SourceMgrAdapter.h - SourceMgr to SourceManager Adapter ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an adapter that maps diagnostics from llvm::SourceMgr
// to Clang's SourceManager.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SOURCEMGRADAPTER_H
#define LLVM_CLANG_SOURCEMGRADAPTER_H

#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/SourceMgr.h"
#include <string>
#include <utility>

namespace clang {

class DiagnosticsEngine;
class FileEntry;

/// An adapter that can be used to translate diagnostics from one or more
/// llvm::SourceMgr instances to a ,
class SourceMgrAdapter {
  /// Clang source manager.
  SourceManager &SrcMgr;

  /// Clang diagnostics engine.
  DiagnosticsEngine &Diagnostics;

  /// Diagnostic IDs for errors, warnings, and notes.
  unsigned ErrorDiagID, WarningDiagID, NoteDiagID;

  /// The default file to use when mapping buffers.
  OptionalFileEntryRef DefaultFile;

  /// A mapping from (LLVM source manager, buffer ID) pairs to the
  /// corresponding file ID within the Clang source manager.
  llvm::DenseMap<std::pair<const llvm::SourceMgr *, unsigned>, FileID>
      FileIDMapping;

  /// Diagnostic handler.
  static void handleDiag(const llvm::SMDiagnostic &Diag, void *Context);

public:
  /// Create a new \c SourceMgr adaptor that maps to the given source
  /// manager and diagnostics engine.
  SourceMgrAdapter(SourceManager &SM, DiagnosticsEngine &Diagnostics,
                   unsigned ErrorDiagID, unsigned WarningDiagID,
                   unsigned NoteDiagID,
                   OptionalFileEntryRef DefaultFile = std::nullopt);

  ~SourceMgrAdapter();

  /// Map a source location in the given LLVM source manager to its
  /// corresponding location in the Clang source manager.
  SourceLocation mapLocation(const llvm::SourceMgr &LLVMSrcMgr,
                             llvm::SMLoc Loc);

  /// Map a source range in the given LLVM source manager to its corresponding
  /// range in the Clang source manager.
  SourceRange mapRange(const llvm::SourceMgr &LLVMSrcMgr, llvm::SMRange Range);

  /// Handle the given diagnostic from an LLVM source manager.
  void handleDiag(const llvm::SMDiagnostic &Diag);

  /// Retrieve the diagnostic handler to use with the underlying SourceMgr.
  llvm::SourceMgr::DiagHandlerTy getDiagHandler() {
    return &SourceMgrAdapter::handleDiag;
  }

  /// Retrieve the context to use with the diagnostic handler produced by
  /// \c getDiagHandler().
  void *getDiagContext() { return this; }
};

} // end namespace clang

#endif
