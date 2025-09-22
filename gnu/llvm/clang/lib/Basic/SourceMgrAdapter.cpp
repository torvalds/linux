//=== SourceMgrAdapter.cpp - SourceMgr to SourceManager Adapter -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the adapter that maps diagnostics from llvm::SourceMgr
// to Clang's SourceManager.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceMgrAdapter.h"
#include "clang/Basic/Diagnostic.h"

using namespace clang;

void SourceMgrAdapter::handleDiag(const llvm::SMDiagnostic &Diag,
                                  void *Context) {
  static_cast<SourceMgrAdapter *>(Context)->handleDiag(Diag);
}

SourceMgrAdapter::SourceMgrAdapter(SourceManager &SM,
                                   DiagnosticsEngine &Diagnostics,
                                   unsigned ErrorDiagID, unsigned WarningDiagID,
                                   unsigned NoteDiagID,
                                   OptionalFileEntryRef DefaultFile)
    : SrcMgr(SM), Diagnostics(Diagnostics), ErrorDiagID(ErrorDiagID),
      WarningDiagID(WarningDiagID), NoteDiagID(NoteDiagID),
      DefaultFile(DefaultFile) {}

SourceMgrAdapter::~SourceMgrAdapter() {}

SourceLocation SourceMgrAdapter::mapLocation(const llvm::SourceMgr &LLVMSrcMgr,
                                             llvm::SMLoc Loc) {
  // Map invalid locations.
  if (!Loc.isValid())
    return SourceLocation();

  // Find the buffer containing the location.
  unsigned BufferID = LLVMSrcMgr.FindBufferContainingLoc(Loc);
  if (!BufferID)
    return SourceLocation();

  // If we haven't seen this buffer before, copy it over.
  auto Buffer = LLVMSrcMgr.getMemoryBuffer(BufferID);
  auto KnownBuffer = FileIDMapping.find(std::make_pair(&LLVMSrcMgr, BufferID));
  if (KnownBuffer == FileIDMapping.end()) {
    FileID FileID;
    if (DefaultFile) {
      // Map to the default file.
      FileID = SrcMgr.getOrCreateFileID(*DefaultFile, SrcMgr::C_User);

      // Only do this once.
      DefaultFile = std::nullopt;
    } else {
      // Make a copy of the memory buffer.
      StringRef bufferName = Buffer->getBufferIdentifier();
      auto bufferCopy = std::unique_ptr<llvm::MemoryBuffer>(
          llvm::MemoryBuffer::getMemBufferCopy(Buffer->getBuffer(),
                                               bufferName));

      // Add this memory buffer to the Clang source manager.
      FileID = SrcMgr.createFileID(std::move(bufferCopy));
    }

    // Save the mapping.
    KnownBuffer = FileIDMapping
                      .insert(std::make_pair(
                          std::make_pair(&LLVMSrcMgr, BufferID), FileID))
                      .first;
  }

  // Translate the offset into the file.
  unsigned Offset = Loc.getPointer() - Buffer->getBufferStart();
  return SrcMgr.getLocForStartOfFile(KnownBuffer->second)
      .getLocWithOffset(Offset);
}

SourceRange SourceMgrAdapter::mapRange(const llvm::SourceMgr &LLVMSrcMgr,
                                       llvm::SMRange Range) {
  if (!Range.isValid())
    return SourceRange();

  SourceLocation Start = mapLocation(LLVMSrcMgr, Range.Start);
  SourceLocation End = mapLocation(LLVMSrcMgr, Range.End);
  return SourceRange(Start, End);
}

void SourceMgrAdapter::handleDiag(const llvm::SMDiagnostic &Diag) {
  // Map the location.
  SourceLocation Loc;
  if (auto *LLVMSrcMgr = Diag.getSourceMgr())
    Loc = mapLocation(*LLVMSrcMgr, Diag.getLoc());

  // Extract the message.
  StringRef Message = Diag.getMessage();

  // Map the diagnostic kind.
  unsigned DiagID;
  switch (Diag.getKind()) {
  case llvm::SourceMgr::DK_Error:
    DiagID = ErrorDiagID;
    break;

  case llvm::SourceMgr::DK_Warning:
    DiagID = WarningDiagID;
    break;

  case llvm::SourceMgr::DK_Remark:
    llvm_unreachable("remarks not implemented");

  case llvm::SourceMgr::DK_Note:
    DiagID = NoteDiagID;
    break;
  }

  // Report the diagnostic.
  DiagnosticBuilder Builder = Diagnostics.Report(Loc, DiagID) << Message;

  if (auto *LLVMSrcMgr = Diag.getSourceMgr()) {
    // Translate ranges.
    SourceLocation StartOfLine = Loc.getLocWithOffset(-Diag.getColumnNo());
    for (auto Range : Diag.getRanges()) {
      Builder << SourceRange(StartOfLine.getLocWithOffset(Range.first),
                             StartOfLine.getLocWithOffset(Range.second));
    }

    // Translate Fix-Its.
    for (const llvm::SMFixIt &FixIt : Diag.getFixIts()) {
      CharSourceRange Range(mapRange(*LLVMSrcMgr, FixIt.getRange()), false);
      Builder << FixItHint::CreateReplacement(Range, FixIt.getText());
    }
  }
}
