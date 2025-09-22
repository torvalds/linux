//===- ExplainOutputStyle.h ----------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_EXPLAINOUTPUTSTYLE_H
#define LLVM_TOOLS_LLVMPDBDUMP_EXPLAINOUTPUTSTYLE_H

#include "OutputStyle.h"

#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"

namespace llvm {

namespace pdb {

class DbiStream;
class InfoStream;
class InputFile;

class ExplainOutputStyle : public OutputStyle {

public:
  ExplainOutputStyle(InputFile &File, uint64_t FileOffset);

  Error dump() override;

private:
  Error explainPdbFile();
  Error explainBinaryFile();

  bool explainPdbBlockStatus();

  bool isPdbFpm1() const;
  bool isPdbFpm2() const;

  bool isPdbSuperBlock() const;
  bool isPdbFpmBlock() const;
  bool isPdbBlockMapBlock() const;
  bool isPdbStreamDirectoryBlock() const;
  std::optional<uint32_t> getPdbBlockStreamIndex() const;

  void explainPdbSuperBlockOffset();
  void explainPdbFpmBlockOffset();
  void explainPdbBlockMapOffset();
  void explainPdbStreamDirectoryOffset();
  void explainPdbStreamOffset(uint32_t Stream);
  void explainPdbUnknownBlock();

  void explainStreamOffset(DbiStream &Stream, uint32_t OffsetInStream);
  void explainStreamOffset(InfoStream &Stream, uint32_t OffsetInStream);

  uint32_t pdbBlockIndex() const;
  uint32_t pdbBlockOffset() const;

  InputFile &File;
  const uint64_t FileOffset;
  LinePrinter P;
};
} // namespace pdb
} // namespace llvm

#endif
