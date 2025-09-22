//===- NativeSourceFile.h - Native source file implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVESOURCEFILE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVESOURCEFILE_H

#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {
class PDBSymbolCompiland;
template <typename ChildType> class IPDBEnumChildren;
class NativeSession;

class NativeSourceFile : public IPDBSourceFile {
public:
  explicit NativeSourceFile(NativeSession &Session, uint32_t FileId,
                            const codeview::FileChecksumEntry &Checksum);

  std::string getFileName() const override;
  uint32_t getUniqueId() const override;
  std::string getChecksum() const override;
  PDB_Checksum getChecksumType() const override;
  std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  getCompilands() const override;

private:
  NativeSession &Session;
  uint32_t FileId;
  const codeview::FileChecksumEntry Checksum;
};
} // namespace pdb
} // namespace llvm
#endif
