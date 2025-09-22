//===- NativeLineNumber.cpp - Native line number implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeLineNumber.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

using namespace llvm;
using namespace llvm::pdb;

NativeLineNumber::NativeLineNumber(const NativeSession &Session,
                                   const codeview::LineInfo Line,
                                   uint32_t ColumnNumber, uint32_t Section,
                                   uint32_t Offset, uint32_t Length,
                                   uint32_t SrcFileId, uint32_t CompilandId)
    : Session(Session), Line(Line), ColumnNumber(ColumnNumber),
      Section(Section), Offset(Offset), Length(Length), SrcFileId(SrcFileId),
      CompilandId(CompilandId) {}

uint32_t NativeLineNumber::getLineNumber() const { return Line.getStartLine(); }

uint32_t NativeLineNumber::getLineNumberEnd() const {
  return Line.getEndLine();
}

uint32_t NativeLineNumber::getColumnNumber() const { return ColumnNumber; }

uint32_t NativeLineNumber::getColumnNumberEnd() const { return 0; }

uint32_t NativeLineNumber::getAddressSection() const { return Section; }

uint32_t NativeLineNumber::getAddressOffset() const { return Offset; }

uint32_t NativeLineNumber::getRelativeVirtualAddress() const {
  return Session.getRVAFromSectOffset(Section, Offset);
}

uint64_t NativeLineNumber::getVirtualAddress() const {
  return Session.getVAFromSectOffset(Section, Offset);
}

uint32_t NativeLineNumber::getLength() const { return Length; }

uint32_t NativeLineNumber::getSourceFileId() const { return SrcFileId; }

uint32_t NativeLineNumber::getCompilandId() const { return CompilandId; }

bool NativeLineNumber::isStatement() const { return Line.isStatement(); }
