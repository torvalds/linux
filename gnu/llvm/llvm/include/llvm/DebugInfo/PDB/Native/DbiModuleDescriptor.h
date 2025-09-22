//===- DbiModuleDescriptor.h - PDB module information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTOR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
template <typename T> struct VarStreamArrayExtractor;

namespace pdb {
struct ModuleInfoHeader;
struct SectionContrib;
class DbiModuleDescriptor {
  friend class DbiStreamBuilder;

public:
  DbiModuleDescriptor() = default;
  DbiModuleDescriptor(const DbiModuleDescriptor &Info) = default;
  DbiModuleDescriptor &operator=(const DbiModuleDescriptor &Info) = default;

  static Error initialize(BinaryStreamRef Stream, DbiModuleDescriptor &Info);

  bool hasECInfo() const;
  uint16_t getTypeServerIndex() const;
  uint16_t getModuleStreamIndex() const;
  uint32_t getSymbolDebugInfoByteSize() const;
  uint32_t getC11LineInfoByteSize() const;
  uint32_t getC13LineInfoByteSize() const;
  uint32_t getNumberOfFiles() const;
  uint32_t getSourceFileNameIndex() const;
  uint32_t getPdbFilePathNameIndex() const;

  StringRef getModuleName() const;
  StringRef getObjFileName() const;

  uint32_t getRecordLength() const;

  const SectionContrib &getSectionContrib() const;

private:
  StringRef ModuleName;
  StringRef ObjFileName;
  const ModuleInfoHeader *Layout = nullptr;
};

} // end namespace pdb

template <> struct VarStreamArrayExtractor<pdb::DbiModuleDescriptor> {
  Error operator()(BinaryStreamRef Stream, uint32_t &Length,
                   pdb::DbiModuleDescriptor &Info) {
    if (auto EC = pdb::DbiModuleDescriptor::initialize(Stream, Info))
      return EC;
    Length = Info.getRecordLength();
    return Error::success();
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTOR_H
