//===-- RuntimeDyldMachO.h - Run-time dynamic linker for MC-JIT ---*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MachO support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDMACHO_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDMACHO_H

#include "RuntimeDyldImpl.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Format.h"

#define DEBUG_TYPE "dyld"

using namespace llvm;
using namespace llvm::object;

namespace llvm {
class RuntimeDyldMachO : public RuntimeDyldImpl {
protected:
  struct SectionOffsetPair {
    unsigned SectionID;
    uint64_t Offset;
  };

  struct EHFrameRelatedSections {
    EHFrameRelatedSections()
        : EHFrameSID(RTDYLD_INVALID_SECTION_ID),
          TextSID(RTDYLD_INVALID_SECTION_ID),
          ExceptTabSID(RTDYLD_INVALID_SECTION_ID) {}

    EHFrameRelatedSections(SID EH, SID T, SID Ex)
        : EHFrameSID(EH), TextSID(T), ExceptTabSID(Ex) {}
    SID EHFrameSID;
    SID TextSID;
    SID ExceptTabSID;
  };

  // When a module is loaded we save the SectionID of the EH frame section
  // in a table until we receive a request to register all unregistered
  // EH frame sections with the memory manager.
  SmallVector<EHFrameRelatedSections, 2> UnregisteredEHFrameSections;

  RuntimeDyldMachO(RuntimeDyld::MemoryManager &MemMgr,
                   JITSymbolResolver &Resolver)
      : RuntimeDyldImpl(MemMgr, Resolver) {}

  /// This convenience method uses memcpy to extract a contiguous addend (the
  /// addend size and offset are taken from the corresponding fields of the RE).
  int64_t memcpyAddend(const RelocationEntry &RE) const;

  /// Given a relocation_iterator for a non-scattered relocation, construct a
  /// RelocationEntry and fill in the common fields. The 'Addend' field is *not*
  /// filled in, since immediate encodings are highly target/opcode specific.
  /// For targets/opcodes with simple, contiguous immediates (e.g. X86) the
  /// memcpyAddend method can be used to read the immediate.
  RelocationEntry getRelocationEntry(unsigned SectionID,
                                     const ObjectFile &BaseTObj,
                                     const relocation_iterator &RI) const {
    const MachOObjectFile &Obj =
      static_cast<const MachOObjectFile &>(BaseTObj);
    MachO::any_relocation_info RelInfo =
      Obj.getRelocation(RI->getRawDataRefImpl());

    bool IsPCRel = Obj.getAnyRelocationPCRel(RelInfo);
    unsigned Size = Obj.getAnyRelocationLength(RelInfo);
    uint64_t Offset = RI->getOffset();
    MachO::RelocationInfoType RelType =
      static_cast<MachO::RelocationInfoType>(Obj.getAnyRelocationType(RelInfo));

    return RelocationEntry(SectionID, Offset, RelType, 0, IsPCRel, Size);
  }

  /// Process a scattered vanilla relocation.
  Expected<relocation_iterator>
  processScatteredVANILLA(unsigned SectionID, relocation_iterator RelI,
                          const ObjectFile &BaseObjT,
                          RuntimeDyldMachO::ObjSectionToIDMap &ObjSectionToID,
                          bool TargetIsLocalThumbFunc = false);

  /// Construct a RelocationValueRef representing the relocation target.
  /// For Symbols in known sections, this will return a RelocationValueRef
  /// representing a (SectionID, Offset) pair.
  /// For Symbols whose section is not known, this will return a
  /// (SymbolName, Offset) pair, where the Offset is taken from the instruction
  /// immediate (held in RE.Addend).
  /// In both cases the Addend field is *NOT* fixed up to be PC-relative. That
  /// should be done by the caller where appropriate by calling makePCRel on
  /// the RelocationValueRef.
  Expected<RelocationValueRef>
  getRelocationValueRef(const ObjectFile &BaseTObj,
                        const relocation_iterator &RI,
                        const RelocationEntry &RE,
                        ObjSectionToIDMap &ObjSectionToID);

  /// Make the RelocationValueRef addend PC-relative.
  void makeValueAddendPCRel(RelocationValueRef &Value,
                            const relocation_iterator &RI,
                            unsigned OffsetToNextPC);

  /// Dump information about the relocation entry (RE) and resolved value.
  void dumpRelocationToResolve(const RelocationEntry &RE, uint64_t Value) const;

  // Return a section iterator for the section containing the given address.
  static section_iterator getSectionByAddress(const MachOObjectFile &Obj,
                                              uint64_t Addr);


  // Populate __pointers section.
  Error populateIndirectSymbolPointersSection(const MachOObjectFile &Obj,
                                              const SectionRef &PTSection,
                                              unsigned PTSectionID);

public:

  /// Create a RuntimeDyldMachO instance for the given target architecture.
  static std::unique_ptr<RuntimeDyldMachO>
  create(Triple::ArchType Arch,
         RuntimeDyld::MemoryManager &MemMgr,
         JITSymbolResolver &Resolver);

  std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
  loadObject(const object::ObjectFile &O) override;

  SectionEntry &getSection(unsigned SectionID) { return Sections[SectionID]; }

  bool isCompatibleFile(const object::ObjectFile &Obj) const override;
};

/// RuntimeDyldMachOTarget - Templated base class for generic MachO linker
/// algorithms and data structures.
///
/// Concrete, target specific sub-classes can be accessed via the impl()
/// methods. (i.e. the RuntimeDyldMachO hierarchy uses the Curiously
/// Recurring Template Idiom). Concrete subclasses for each target
/// can be found in ./Targets.
template <typename Impl>
class RuntimeDyldMachOCRTPBase : public RuntimeDyldMachO {
private:
  Impl &impl() { return static_cast<Impl &>(*this); }
  const Impl &impl() const { return static_cast<const Impl &>(*this); }

  unsigned char *processFDE(uint8_t *P, int64_t DeltaForText,
                            int64_t DeltaForEH);

public:
  RuntimeDyldMachOCRTPBase(RuntimeDyld::MemoryManager &MemMgr,
                           JITSymbolResolver &Resolver)
    : RuntimeDyldMachO(MemMgr, Resolver) {}

  Error finalizeLoad(const ObjectFile &Obj,
                     ObjSectionToIDMap &SectionMap) override;
  void registerEHFrames() override;
};

} // end namespace llvm

#undef DEBUG_TYPE

#endif
