//===- NativeLineNumber.h - Native line number implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVELINENUMBER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVELINENUMBER_H

#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"

namespace llvm {
namespace pdb {

class NativeSession;

class NativeLineNumber : public IPDBLineNumber {
public:
  explicit NativeLineNumber(const NativeSession &Session,
                            const codeview::LineInfo Line,
                            uint32_t ColumnNumber, uint32_t Length,
                            uint32_t Section, uint32_t Offset,
                            uint32_t SrcFileId, uint32_t CompilandId);

  uint32_t getLineNumber() const override;
  uint32_t getLineNumberEnd() const override;
  uint32_t getColumnNumber() const override;
  uint32_t getColumnNumberEnd() const override;
  uint32_t getAddressSection() const override;
  uint32_t getAddressOffset() const override;
  uint32_t getRelativeVirtualAddress() const override;
  uint64_t getVirtualAddress() const override;
  uint32_t getLength() const override;
  uint32_t getSourceFileId() const override;
  uint32_t getCompilandId() const override;
  bool isStatement() const override;

private:
  const NativeSession &Session;
  const codeview::LineInfo Line;
  uint32_t ColumnNumber;
  uint32_t Section;
  uint32_t Offset;
  uint32_t Length;
  uint32_t SrcFileId;
  uint32_t CompilandId;
};
} // namespace pdb
} // namespace llvm
#endif
