//===-- RuntimeDyldCOFF.cpp - Run-time dynamic linker for MC-JIT -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of COFF support for the MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#include "RuntimeDyldCOFF.h"
#include "Targets/RuntimeDyldCOFFAArch64.h"
#include "Targets/RuntimeDyldCOFFI386.h"
#include "Targets/RuntimeDyldCOFFThumb.h"
#include "Targets/RuntimeDyldCOFFX86_64.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "dyld"

namespace {

class LoadedCOFFObjectInfo final
    : public LoadedObjectInfoHelper<LoadedCOFFObjectInfo,
                                    RuntimeDyld::LoadedObjectInfo> {
public:
  LoadedCOFFObjectInfo(
      RuntimeDyldImpl &RTDyld,
      RuntimeDyld::LoadedObjectInfo::ObjSectionToIDMap ObjSecToIDMap)
      : LoadedObjectInfoHelper(RTDyld, std::move(ObjSecToIDMap)) {}

  OwningBinary<ObjectFile>
  getObjectForDebug(const ObjectFile &Obj) const override {
    return OwningBinary<ObjectFile>();
  }
};
}

namespace llvm {

std::unique_ptr<RuntimeDyldCOFF>
llvm::RuntimeDyldCOFF::create(Triple::ArchType Arch,
                              RuntimeDyld::MemoryManager &MemMgr,
                              JITSymbolResolver &Resolver) {
  switch (Arch) {
  default: llvm_unreachable("Unsupported target for RuntimeDyldCOFF.");
  case Triple::x86:
    return std::make_unique<RuntimeDyldCOFFI386>(MemMgr, Resolver);
  case Triple::thumb:
    return std::make_unique<RuntimeDyldCOFFThumb>(MemMgr, Resolver);
  case Triple::x86_64:
    return std::make_unique<RuntimeDyldCOFFX86_64>(MemMgr, Resolver);
  case Triple::aarch64:
    return std::make_unique<RuntimeDyldCOFFAArch64>(MemMgr, Resolver);
  }
}

std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
RuntimeDyldCOFF::loadObject(const object::ObjectFile &O) {
  if (auto ObjSectionToIDOrErr = loadObjectImpl(O)) {
    return std::make_unique<LoadedCOFFObjectInfo>(*this, *ObjSectionToIDOrErr);
  } else {
    HasError = true;
    raw_string_ostream ErrStream(ErrorStr);
    logAllUnhandledErrors(ObjSectionToIDOrErr.takeError(), ErrStream);
    return nullptr;
  }
}

uint64_t RuntimeDyldCOFF::getSymbolOffset(const SymbolRef &Sym) {
  // The value in a relocatable COFF object is the offset.
  return cantFail(Sym.getValue());
}

uint64_t RuntimeDyldCOFF::getDLLImportOffset(unsigned SectionID, StubMap &Stubs,
                                             StringRef Name,
                                             bool SetSectionIDMinus1) {
  LLVM_DEBUG(dbgs() << "Getting DLLImport entry for " << Name << "... ");
  assert(Name.starts_with(getImportSymbolPrefix()) &&
         "Not a DLLImport symbol?");
  RelocationValueRef Reloc;
  Reloc.SymbolName = Name.data();
  auto I = Stubs.find(Reloc);
  if (I != Stubs.end()) {
    LLVM_DEBUG(dbgs() << format("{0:x8}", I->second) << "\n");
    return I->second;
  }

  assert(SectionID < Sections.size() && "SectionID out of range");
  auto &Sec = Sections[SectionID];
  auto EntryOffset = alignTo(Sec.getStubOffset(), PointerSize);
  Sec.advanceStubOffset(EntryOffset + PointerSize - Sec.getStubOffset());
  Stubs[Reloc] = EntryOffset;

  RelocationEntry RE(SectionID, EntryOffset, PointerReloc, 0, false,
                     Log2_64(PointerSize));
  // Hack to tell I386/Thumb resolveRelocation that this isn't section relative.
  if (SetSectionIDMinus1)
    RE.Sections.SectionA = -1;
  addRelocationForSymbol(RE, Name.drop_front(getImportSymbolPrefix().size()));

  LLVM_DEBUG({
    dbgs() << "Creating entry at "
           << formatv("{0:x16} + {1:x8} ( {2:x16} )", Sec.getLoadAddress(),
                      EntryOffset, Sec.getLoadAddress() + EntryOffset)
           << "\n";
  });
  return EntryOffset;
}

bool RuntimeDyldCOFF::isCompatibleFile(const object::ObjectFile &Obj) const {
  return Obj.isCOFF();
}

} // namespace llvm
