//===- MachOLayoutBuilder.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_MACHO_MACHOLAYOUTBUILDER_H
#define LLVM_LIB_OBJCOPY_MACHO_MACHOLAYOUTBUILDER_H

#include "MachOObject.h"
#include "llvm/ObjCopy/MachO/MachOObjcopy.h"

namespace llvm {
namespace objcopy {
namespace macho {

/// When MachO binaries include a LC_CODE_SIGNATURE load command,
/// the __LINKEDIT data segment will include a section corresponding
/// to the LC_CODE_SIGNATURE load command. This section serves as a signature
/// for the binary. Included in the CodeSignature section is a header followed
/// by a hash of the binary. If present, the CodeSignature section is the
/// last component of the binary.
struct CodeSignatureInfo {
  // NOTE: These values are to be kept in sync with those in
  // LLD's CodeSignatureSection class.

  static constexpr uint32_t Align = 16;
  static constexpr uint8_t BlockSizeShift = 12;
  // The binary is read in blocks of the following size.
  static constexpr size_t BlockSize = (1 << BlockSizeShift); // 4 KiB
  // For each block, a SHA256 hash (256 bits, 32 bytes) is written to
  // the CodeSignature section.
  static constexpr size_t HashSize = 256 / 8;
  static constexpr size_t BlobHeadersSize = llvm::alignTo<8>(
      sizeof(llvm::MachO::CS_SuperBlob) + sizeof(llvm::MachO::CS_BlobIndex));
  // The size of the entire header depends upon the filename the binary is being
  // written to, but the rest of the header is fixed in size.
  static constexpr uint32_t FixedHeadersSize =
      BlobHeadersSize + sizeof(llvm::MachO::CS_CodeDirectory);

  // The offset relative to the start of the binary where
  // the CodeSignature section should begin.
  uint32_t StartOffset;
  // The size of the entire header, output file name size included.
  uint32_t AllHeadersSize;
  // The number of blocks required to hash the binary.
  uint32_t BlockCount;
  StringRef OutputFileName;
  // The size of the entire CodeSignature section, including both the header and
  // hashes.
  uint32_t Size;
};

class MachOLayoutBuilder {
  Object &O;
  bool Is64Bit;
  StringRef OutputFileName;
  uint64_t PageSize;
  CodeSignatureInfo CodeSignature;

  // Points to the __LINKEDIT segment if it exists.
  MachO::macho_load_command *LinkEditLoadCommand = nullptr;
  StringTableBuilder StrTableBuilder;

  uint32_t computeSizeOfCmds() const;
  void constructStringTable();
  void updateSymbolIndexes();
  void updateDySymTab(MachO::macho_load_command &MLC);
  uint64_t layoutSegments();
  uint64_t layoutRelocations(uint64_t Offset);
  Error layoutTail(uint64_t Offset);

  static StringTableBuilder::Kind getStringTableBuilderKind(const Object &O,
                                                            bool Is64Bit);

public:
  MachOLayoutBuilder(Object &O, bool Is64Bit, StringRef OutputFileName,
                     uint64_t PageSize)
      : O(O), Is64Bit(Is64Bit), OutputFileName(OutputFileName),
        PageSize(PageSize),
        StrTableBuilder(getStringTableBuilderKind(O, Is64Bit)) {}

  // Recomputes and updates fields in the given object such as file offsets.
  Error layout();

  StringTableBuilder &getStringTableBuilder() { return StrTableBuilder; }

  const CodeSignatureInfo &getCodeSignature() const { return CodeSignature; }
};

} // end namespace macho
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_MACHO_MACHOLAYOUTBUILDER_H
