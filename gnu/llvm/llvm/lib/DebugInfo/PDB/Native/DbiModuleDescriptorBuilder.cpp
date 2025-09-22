//===- DbiModuleDescriptorBuilder.cpp - PDB Mod Info Creation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

namespace llvm {
namespace codeview {
class DebugSubsection;
}
} // namespace llvm

static uint32_t calculateDiSymbolStreamSize(uint32_t SymbolByteSize,
                                            uint32_t C13Size) {
  uint32_t Size = sizeof(uint32_t);   // Signature
  Size += alignTo(SymbolByteSize, 4); // Symbol Data
  Size += 0;                          // TODO: Layout.C11Bytes
  Size += C13Size;                    // C13 Debug Info Size
  Size += sizeof(uint32_t);           // GlobalRefs substream size (always 0)
  Size += 0;                          // GlobalRefs substream bytes
  return Size;
}

DbiModuleDescriptorBuilder::DbiModuleDescriptorBuilder(StringRef ModuleName,
                                                       uint32_t ModIndex,
                                                       msf::MSFBuilder &Msf)
    : MSF(Msf), ModuleName(std::string(ModuleName)) {
  ::memset(&Layout, 0, sizeof(Layout));
  Layout.Mod = ModIndex;
}

DbiModuleDescriptorBuilder::~DbiModuleDescriptorBuilder() = default;

uint16_t DbiModuleDescriptorBuilder::getStreamIndex() const {
  return Layout.ModDiStream;
}

void DbiModuleDescriptorBuilder::setObjFileName(StringRef Name) {
  ObjFileName = std::string(Name);
}

void DbiModuleDescriptorBuilder::setPdbFilePathNI(uint32_t NI) {
  PdbFilePathNI = NI;
}

void DbiModuleDescriptorBuilder::setFirstSectionContrib(
    const SectionContrib &SC) {
  Layout.SC = SC;
}

void DbiModuleDescriptorBuilder::addSymbol(CVSymbol Symbol) {
  // Defer to the bulk API. It does the same thing.
  addSymbolsInBulk(Symbol.data());
}

void DbiModuleDescriptorBuilder::addSymbolsInBulk(
    ArrayRef<uint8_t> BulkSymbols) {
  // Do nothing for empty runs of symbols.
  if (BulkSymbols.empty())
    return;

  Symbols.push_back(SymbolListWrapper(BulkSymbols));
  // Symbols written to a PDB file are required to be 4 byte aligned. The same
  // is not true of object files.
  assert(BulkSymbols.size() % alignOf(CodeViewContainer::Pdb) == 0 &&
         "Invalid Symbol alignment!");
  SymbolByteSize += BulkSymbols.size();
}

void DbiModuleDescriptorBuilder::addUnmergedSymbols(void *SymSrc,
                                                    uint32_t SymLength) {
  assert(SymLength > 0);
  Symbols.push_back(SymbolListWrapper(SymSrc, SymLength));

  // Symbols written to a PDB file are required to be 4 byte aligned. The same
  // is not true of object files.
  assert(SymLength % alignOf(CodeViewContainer::Pdb) == 0 &&
         "Invalid Symbol alignment!");
  SymbolByteSize += SymLength;
}

void DbiModuleDescriptorBuilder::addSourceFile(StringRef Path) {
  SourceFiles.push_back(std::string(Path));
}

uint32_t DbiModuleDescriptorBuilder::calculateC13DebugInfoSize() const {
  uint32_t Result = 0;
  for (const auto &Builder : C13Builders) {
    Result += Builder.calculateSerializedLength();
  }
  return Result;
}

uint32_t DbiModuleDescriptorBuilder::calculateSerializedLength() const {
  uint32_t L = sizeof(Layout);
  uint32_t M = ModuleName.size() + 1;
  uint32_t O = ObjFileName.size() + 1;
  return alignTo(L + M + O, sizeof(uint32_t));
}

void DbiModuleDescriptorBuilder::finalize() {
  Layout.FileNameOffs = 0; // TODO: Fix this
  Layout.Flags = 0;        // TODO: Fix this
  Layout.C11Bytes = 0;
  Layout.C13Bytes = calculateC13DebugInfoSize();
  (void)Layout.Mod;         // Set in constructor
  (void)Layout.ModDiStream; // Set in finalizeMsfLayout
  Layout.NumFiles = SourceFiles.size();
  Layout.PdbFilePathNI = PdbFilePathNI;
  Layout.SrcFileNameNI = 0;

  // This value includes both the signature field as well as the record bytes
  // from the symbol stream.
  Layout.SymBytes =
      Layout.ModDiStream == kInvalidStreamIndex ? 0 : getNextSymbolOffset();
}

Error DbiModuleDescriptorBuilder::finalizeMsfLayout() {
  this->Layout.ModDiStream = kInvalidStreamIndex;
  uint32_t C13Size = calculateC13DebugInfoSize();
  if (!C13Size && !SymbolByteSize)
    return Error::success();
  auto ExpectedSN =
      MSF.addStream(calculateDiSymbolStreamSize(SymbolByteSize, C13Size));
  if (!ExpectedSN)
    return ExpectedSN.takeError();
  Layout.ModDiStream = *ExpectedSN;
  return Error::success();
}

Error DbiModuleDescriptorBuilder::commit(BinaryStreamWriter &ModiWriter) {
  // We write the Modi record to the `ModiWriter`, but we additionally write its
  // symbol stream to a brand new stream.
  if (auto EC = ModiWriter.writeObject(Layout))
    return EC;
  if (auto EC = ModiWriter.writeCString(ModuleName))
    return EC;
  if (auto EC = ModiWriter.writeCString(ObjFileName))
    return EC;
  if (auto EC = ModiWriter.padToAlignment(sizeof(uint32_t)))
    return EC;
  return Error::success();
}

Error DbiModuleDescriptorBuilder::commitSymbolStream(
    const msf::MSFLayout &MsfLayout, WritableBinaryStreamRef MsfBuffer) {
  if (Layout.ModDiStream == kInvalidStreamIndex)
    return Error::success();

  auto NS = WritableMappedBlockStream::createIndexedStream(
      MsfLayout, MsfBuffer, Layout.ModDiStream, MSF.getAllocator());
  WritableBinaryStreamRef Ref(*NS);
  BinaryStreamWriter SymbolWriter(Ref);
  // Write the symbols.
  if (auto EC = SymbolWriter.writeInteger<uint32_t>(COFF::DEBUG_SECTION_MAGIC))
    return EC;
  for (const SymbolListWrapper &Sym : Symbols) {
    if (Sym.NeedsToBeMerged) {
      assert(MergeSymsCallback);
      if (auto EC = MergeSymsCallback(MergeSymsCtx, Sym.SymPtr, SymbolWriter))
        return EC;
    } else {
      if (auto EC = SymbolWriter.writeBytes(Sym.asArray()))
        return EC;
    }
  }

  // Apply the string table fixups.
  auto SavedOffset = SymbolWriter.getOffset();
  for (const StringTableFixup &Fixup : StringTableFixups) {
    SymbolWriter.setOffset(Fixup.SymOffsetOfReference);
    if (auto E = SymbolWriter.writeInteger<uint32_t>(Fixup.StrTabOffset))
      return E;
  }
  SymbolWriter.setOffset(SavedOffset);

  assert(SymbolWriter.getOffset() % alignOf(CodeViewContainer::Pdb) == 0 &&
         "Invalid debug section alignment!");
  // TODO: Write C11 Line data
  for (const auto &Builder : C13Builders) {
    if (auto EC = Builder.commit(SymbolWriter, CodeViewContainer::Pdb))
      return EC;
  }

  // TODO: Figure out what GlobalRefs substream actually is and populate it.
  if (auto EC = SymbolWriter.writeInteger<uint32_t>(0))
    return EC;
  if (SymbolWriter.bytesRemaining() > 0)
    return make_error<RawError>(raw_error_code::stream_too_long);

  return Error::success();
}

void DbiModuleDescriptorBuilder::addDebugSubsection(
    std::shared_ptr<DebugSubsection> Subsection) {
  assert(Subsection);
  C13Builders.push_back(DebugSubsectionRecordBuilder(std::move(Subsection)));
}

void DbiModuleDescriptorBuilder::addDebugSubsection(
    const DebugSubsectionRecord &SubsectionContents) {
  C13Builders.push_back(DebugSubsectionRecordBuilder(SubsectionContents));
}
