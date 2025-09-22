//===-- RuntimeDyldCOFF.h - Run-time dynamic linker for MC-JIT ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// COFF support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_RUNTIME_DYLD_COFF_H
#define LLVM_RUNTIME_DYLD_COFF_H

#include "RuntimeDyldImpl.h"

#define DEBUG_TYPE "dyld"

using namespace llvm;

namespace llvm {

// Common base class for COFF dynamic linker support.
// Concrete subclasses for each target can be found in ./Targets.
class RuntimeDyldCOFF : public RuntimeDyldImpl {

public:
  std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
  loadObject(const object::ObjectFile &Obj) override;
  bool isCompatibleFile(const object::ObjectFile &Obj) const override;

  static std::unique_ptr<RuntimeDyldCOFF>
  create(Triple::ArchType Arch, RuntimeDyld::MemoryManager &MemMgr,
         JITSymbolResolver &Resolver);

protected:
  RuntimeDyldCOFF(RuntimeDyld::MemoryManager &MemMgr,
                  JITSymbolResolver &Resolver, unsigned PointerSize,
                  uint32_t PointerReloc)
      : RuntimeDyldImpl(MemMgr, Resolver), PointerSize(PointerSize),
        PointerReloc(PointerReloc) {
    assert((PointerSize == 4 || PointerSize == 8) && "Unexpected pointer size");
  }

  uint64_t getSymbolOffset(const SymbolRef &Sym);
  uint64_t getDLLImportOffset(unsigned SectionID, StubMap &Stubs,
                              StringRef Name, bool SetSectionIDMinus1 = false);

  static constexpr StringRef getImportSymbolPrefix() { return "__imp_"; }

private:
  unsigned PointerSize;
  uint32_t PointerReloc;
};

} // end namespace llvm

#undef DEBUG_TYPE

#endif
