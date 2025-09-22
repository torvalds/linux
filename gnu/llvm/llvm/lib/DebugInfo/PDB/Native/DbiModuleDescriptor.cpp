//===- DbiModuleDescriptor.cpp - PDB module information -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>

using namespace llvm;
using namespace llvm::pdb;
using namespace llvm::support;

Error DbiModuleDescriptor::initialize(BinaryStreamRef Stream,
                                      DbiModuleDescriptor &Info) {
  BinaryStreamReader Reader(Stream);
  if (auto EC = Reader.readObject(Info.Layout))
    return EC;

  if (auto EC = Reader.readCString(Info.ModuleName))
    return EC;

  if (auto EC = Reader.readCString(Info.ObjFileName))
    return EC;
  return Error::success();
}

bool DbiModuleDescriptor::hasECInfo() const {
  return (Layout->Flags & ModInfoFlags::HasECFlagMask) != 0;
}

uint16_t DbiModuleDescriptor::getTypeServerIndex() const {
  return (Layout->Flags & ModInfoFlags::TypeServerIndexMask) >>
         ModInfoFlags::TypeServerIndexShift;
}

const SectionContrib &DbiModuleDescriptor::getSectionContrib() const {
  return Layout->SC;
}

uint16_t DbiModuleDescriptor::getModuleStreamIndex() const {
  return Layout->ModDiStream;
}

uint32_t DbiModuleDescriptor::getSymbolDebugInfoByteSize() const {
  return Layout->SymBytes;
}

uint32_t DbiModuleDescriptor::getC11LineInfoByteSize() const {
  return Layout->C11Bytes;
}

uint32_t DbiModuleDescriptor::getC13LineInfoByteSize() const {
  return Layout->C13Bytes;
}

uint32_t DbiModuleDescriptor::getNumberOfFiles() const {
  return Layout->NumFiles;
}

uint32_t DbiModuleDescriptor::getSourceFileNameIndex() const {
  return Layout->SrcFileNameNI;
}

uint32_t DbiModuleDescriptor::getPdbFilePathNameIndex() const {
  return Layout->PdbFilePathNI;
}

StringRef DbiModuleDescriptor::getModuleName() const { return ModuleName; }

StringRef DbiModuleDescriptor::getObjFileName() const { return ObjFileName; }

uint32_t DbiModuleDescriptor::getRecordLength() const {
  uint32_t M = ModuleName.str().size() + 1;
  uint32_t O = ObjFileName.str().size() + 1;
  uint32_t Size = sizeof(ModuleInfoHeader) + M + O;
  Size = alignTo(Size, 4);
  return Size;
}
