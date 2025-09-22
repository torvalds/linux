//===- MachOWriter.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_MACHO_MACHOWRITER_H
#define LLVM_LIB_OBJCOPY_MACHO_MACHOWRITER_H

#include "MachOLayoutBuilder.h"
#include "MachOObject.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/ObjCopy/MachO/MachOObjcopy.h"
#include "llvm/Object/MachO.h"

namespace llvm {
class Error;

namespace objcopy {
namespace macho {

class MachOWriter {
  Object &O;
  bool Is64Bit;
  bool IsLittleEndian;
  uint64_t PageSize;
  std::unique_ptr<WritableMemoryBuffer> Buf;
  raw_ostream &Out;
  MachOLayoutBuilder LayoutBuilder;

  size_t headerSize() const;
  size_t loadCommandsSize() const;
  size_t symTableSize() const;
  size_t strTableSize() const;

  void writeHeader();
  void writeLoadCommands();
  template <typename StructType>
  void writeSectionInLoadCommand(const Section &Sec, uint8_t *&Out);
  void writeSections();
  void writeSymbolTable();
  void writeStringTable();
  void writeRebaseInfo();
  void writeBindInfo();
  void writeWeakBindInfo();
  void writeLazyBindInfo();
  void writeExportInfo();
  void writeIndirectSymbolTable();
  void writeLinkData(std::optional<size_t> LCIndex, const LinkData &LD);
  void writeCodeSignatureData();
  void writeDataInCodeData();
  void writeLinkerOptimizationHint();
  void writeFunctionStartsData();
  void writeDylibCodeSignDRsData();
  void writeChainedFixupsData();
  void writeExportsTrieData();
  void writeTail();

public:
  MachOWriter(Object &O, bool Is64Bit, bool IsLittleEndian,
              StringRef OutputFileName, uint64_t PageSize, raw_ostream &Out)
      : O(O), Is64Bit(Is64Bit), IsLittleEndian(IsLittleEndian),
        PageSize(PageSize), Out(Out),
        LayoutBuilder(O, Is64Bit, OutputFileName, PageSize) {}

  size_t totalSize() const;
  Error finalize();
  Error write();
};

} // end namespace macho
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_MACHO_MACHOWRITER_H
