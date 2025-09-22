//===-- RuntimeDyldCOFFAArch64.h --- COFF/AArch64 specific code ---*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// COFF AArch64 support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFAARCH64_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFAARCH64_H

#include "../RuntimeDyldCOFF.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Endian.h"

#define DEBUG_TYPE "dyld"

using namespace llvm::support::endian;

namespace llvm {

// This relocation type is used for handling long branch instruction
// through the Stub.
enum InternalRelocationType : unsigned {
  INTERNAL_REL_ARM64_LONG_BRANCH26 = 0x111,
};

static void add16(uint8_t *p, int16_t v) { write16le(p, read16le(p) + v); }
static void or32le(void *P, int32_t V) { write32le(P, read32le(P) | V); }

static void write32AArch64Imm(uint8_t *T, uint64_t imm, uint32_t rangeLimit) {
  uint32_t orig = read32le(T);
  orig &= ~(0xFFF << 10);
  write32le(T, orig | ((imm & (0xFFF >> rangeLimit)) << 10));
}

static void write32AArch64Ldr(uint8_t *T, uint64_t imm) {
  uint32_t orig = read32le(T);
  uint32_t size = orig >> 30;
  // 0x04000000 indicates SIMD/FP registers
  // 0x00800000 indicates 128 bit
  if ((orig & 0x04800000) == 0x04800000)
    size += 4;
  if ((imm & ((1 << size) - 1)) != 0)
    assert(0 && "misaligned ldr/str offset");
  write32AArch64Imm(T, imm >> size, size);
}

static void write32AArch64Addr(void *T, uint64_t s, uint64_t p, int shift) {
  uint64_t Imm = (s >> shift) - (p >> shift);
  uint32_t ImmLo = (Imm & 0x3) << 29;
  uint32_t ImmHi = (Imm & 0x1FFFFC) << 3;
  uint64_t Mask = (0x3 << 29) | (0x1FFFFC << 3);
  write32le(T, (read32le(T) & ~Mask) | ImmLo | ImmHi);
}

class RuntimeDyldCOFFAArch64 : public RuntimeDyldCOFF {

private:
  // When a module is loaded we save the SectionID of the unwind
  // sections in a table until we receive a request to register all
  // unregisteredEH frame sections with the memory manager.
  SmallVector<SID, 2> UnregisteredEHFrameSections;
  SmallVector<SID, 2> RegisteredEHFrameSections;
  uint64_t ImageBase;

  // Fake an __ImageBase pointer by returning the section with the lowest adress
  uint64_t getImageBase() {
    if (!ImageBase) {
      ImageBase = std::numeric_limits<uint64_t>::max();
      for (const SectionEntry &Section : Sections)
        // The Sections list may contain sections that weren't loaded for
        // whatever reason: they may be debug sections, and ProcessAllSections
        // is false, or they may be sections that contain 0 bytes. If the
        // section isn't loaded, the load address will be 0, and it should not
        // be included in the ImageBase calculation.
        if (Section.getLoadAddress() != 0)
          ImageBase = std::min(ImageBase, Section.getLoadAddress());
    }
    return ImageBase;
  }

public:
  RuntimeDyldCOFFAArch64(RuntimeDyld::MemoryManager &MM,
                         JITSymbolResolver &Resolver)
      : RuntimeDyldCOFF(MM, Resolver, 8, COFF::IMAGE_REL_ARM64_ADDR64),
        ImageBase(0) {}

  Align getStubAlignment() override { return Align(8); }

  unsigned getMaxStubSize() const override { return 20; }

  std::tuple<uint64_t, uint64_t, uint64_t>
  generateRelocationStub(unsigned SectionID, StringRef TargetName,
                         uint64_t Offset, uint64_t RelType, uint64_t Addend,
                         StubMap &Stubs) {
    uintptr_t StubOffset;
    SectionEntry &Section = Sections[SectionID];

    RelocationValueRef OriginalRelValueRef;
    OriginalRelValueRef.SectionID = SectionID;
    OriginalRelValueRef.Offset = Offset;
    OriginalRelValueRef.Addend = Addend;
    OriginalRelValueRef.SymbolName = TargetName.data();

    auto Stub = Stubs.find(OriginalRelValueRef);
    if (Stub == Stubs.end()) {
      LLVM_DEBUG(dbgs() << " Create a new stub function for "
                        << TargetName.data() << "\n");

      StubOffset = Section.getStubOffset();
      Stubs[OriginalRelValueRef] = StubOffset;
      createStubFunction(Section.getAddressWithOffset(StubOffset));
      Section.advanceStubOffset(getMaxStubSize());
    } else {
      LLVM_DEBUG(dbgs() << " Stub function found for " << TargetName.data()
                        << "\n");
      StubOffset = Stub->second;
    }

    // Resolve original relocation to stub function.
    const RelocationEntry RE(SectionID, Offset, RelType, Addend);
    resolveRelocation(RE, Section.getLoadAddressWithOffset(StubOffset));

    // adjust relocation info so resolution writes to the stub function
    // Here an internal relocation type is used for resolving long branch via
    // stub instruction.
    Addend = 0;
    Offset = StubOffset;
    RelType = INTERNAL_REL_ARM64_LONG_BRANCH26;

    return std::make_tuple(Offset, RelType, Addend);
  }

  Expected<object::relocation_iterator>
  processRelocationRef(unsigned SectionID, object::relocation_iterator RelI,
                       const object::ObjectFile &Obj,
                       ObjSectionToIDMap &ObjSectionToID,
                       StubMap &Stubs) override {

    auto Symbol = RelI->getSymbol();
    if (Symbol == Obj.symbol_end())
      report_fatal_error("Unknown symbol in relocation");

    Expected<StringRef> TargetNameOrErr = Symbol->getName();
    if (!TargetNameOrErr)
      return TargetNameOrErr.takeError();
    StringRef TargetName = *TargetNameOrErr;

    auto SectionOrErr = Symbol->getSection();
    if (!SectionOrErr)
      return SectionOrErr.takeError();
    auto Section = *SectionOrErr;

    uint64_t RelType = RelI->getType();
    uint64_t Offset = RelI->getOffset();

    // If there is no section, this must be an external reference.
    bool IsExtern = Section == Obj.section_end();

    // Determine the Addend used to adjust the relocation value.
    uint64_t Addend = 0;
    SectionEntry &AddendSection = Sections[SectionID];
    uintptr_t ObjTarget = AddendSection.getObjAddress() + Offset;
    uint8_t *Displacement = (uint8_t *)ObjTarget;

    unsigned TargetSectionID = -1;
    uint64_t TargetOffset = -1;

    if (TargetName.starts_with(getImportSymbolPrefix())) {
      TargetSectionID = SectionID;
      TargetOffset = getDLLImportOffset(SectionID, Stubs, TargetName);
      TargetName = StringRef();
      IsExtern = false;
    } else if (!IsExtern) {
      if (auto TargetSectionIDOrErr = findOrEmitSection(
              Obj, *Section, Section->isText(), ObjSectionToID))
        TargetSectionID = *TargetSectionIDOrErr;
      else
        return TargetSectionIDOrErr.takeError();

      TargetOffset = getSymbolOffset(*Symbol);
    }

    switch (RelType) {
    case COFF::IMAGE_REL_ARM64_ADDR32:
    case COFF::IMAGE_REL_ARM64_ADDR32NB:
    case COFF::IMAGE_REL_ARM64_REL32:
    case COFF::IMAGE_REL_ARM64_SECREL:
      Addend = read32le(Displacement);
      break;
    case COFF::IMAGE_REL_ARM64_BRANCH26: {
      uint32_t orig = read32le(Displacement);
      Addend = (orig & 0x03FFFFFF) << 2;

      if (IsExtern)
        std::tie(Offset, RelType, Addend) = generateRelocationStub(
            SectionID, TargetName, Offset, RelType, Addend, Stubs);
      break;
    }
    case COFF::IMAGE_REL_ARM64_BRANCH19: {
      uint32_t orig = read32le(Displacement);
      Addend = (orig & 0x00FFFFE0) >> 3;
      break;
    }
    case COFF::IMAGE_REL_ARM64_BRANCH14: {
      uint32_t orig = read32le(Displacement);
      Addend = (orig & 0x000FFFE0) >> 3;
      break;
    }
    case COFF::IMAGE_REL_ARM64_REL21:
    case COFF::IMAGE_REL_ARM64_PAGEBASE_REL21: {
      uint32_t orig = read32le(Displacement);
      Addend = ((orig >> 29) & 0x3) | ((orig >> 3) & 0x1FFFFC);
      break;
    }
    case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L:
    case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A: {
      uint32_t orig = read32le(Displacement);
      Addend = ((orig >> 10) & 0xFFF);
      break;
    }
    case COFF::IMAGE_REL_ARM64_ADDR64: {
      Addend = read64le(Displacement);
      break;
    }
    default:
      break;
    }

#if !defined(NDEBUG)
    SmallString<32> RelTypeName;
    RelI->getTypeName(RelTypeName);

    LLVM_DEBUG(dbgs() << "\t\tIn Section " << SectionID << " Offset " << Offset
                      << " RelType: " << RelTypeName << " TargetName: "
                      << TargetName << " Addend " << Addend << "\n");
#endif

    if (IsExtern) {
      RelocationEntry RE(SectionID, Offset, RelType, Addend);
      addRelocationForSymbol(RE, TargetName);
    } else {
      RelocationEntry RE(SectionID, Offset, RelType, TargetOffset + Addend);
      addRelocationForSection(RE, TargetSectionID);
    }
    return ++RelI;
  }

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override {
    const auto Section = Sections[RE.SectionID];
    uint8_t *Target = Section.getAddressWithOffset(RE.Offset);
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(RE.Offset);

    switch (RE.RelType) {
    default:
      llvm_unreachable("unsupported relocation type");
    case COFF::IMAGE_REL_ARM64_ABSOLUTE: {
      // This relocation is ignored.
      break;
    }
    case COFF::IMAGE_REL_ARM64_PAGEBASE_REL21: {
      // The page base of the target, for ADRP instruction.
      Value += RE.Addend;
      write32AArch64Addr(Target, Value, FinalAddress, 12);
      break;
    }
    case COFF::IMAGE_REL_ARM64_REL21: {
      // The 12-bit relative displacement to the target, for instruction ADR
      Value += RE.Addend;
      write32AArch64Addr(Target, Value, FinalAddress, 0);
      break;
    }
    case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A: {
      // The 12-bit page offset of the target,
      // for instructions ADD/ADDS (immediate) with zero shift.
      Value += RE.Addend;
      write32AArch64Imm(Target, Value & 0xFFF, 0);
      break;
    }
    case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L: {
      // The 12-bit page offset of the target,
      // for instruction LDR (indexed, unsigned immediate).
      Value += RE.Addend;
      write32AArch64Ldr(Target, Value & 0xFFF);
      break;
    }
    case COFF::IMAGE_REL_ARM64_ADDR32: {
      // The 32-bit VA of the target.
      uint32_t VA = Value + RE.Addend;
      write32le(Target, VA);
      break;
    }
    case COFF::IMAGE_REL_ARM64_ADDR32NB: {
      // The target's 32-bit RVA.
      uint64_t RVA = Value + RE.Addend - getImageBase();
      write32le(Target, RVA);
      break;
    }
    case INTERNAL_REL_ARM64_LONG_BRANCH26: {
      // Encode the immadiate value for generated Stub instruction (MOVZ)
      or32le(Target + 12, ((Value + RE.Addend) & 0xFFFF) << 5);
      or32le(Target + 8, ((Value + RE.Addend) & 0xFFFF0000) >> 11);
      or32le(Target + 4, ((Value + RE.Addend) & 0xFFFF00000000) >> 27);
      or32le(Target + 0, ((Value + RE.Addend) & 0xFFFF000000000000) >> 43);
      break;
    }
    case COFF::IMAGE_REL_ARM64_BRANCH26: {
      // The 26-bit relative displacement to the target, for B and BL
      // instructions.
      uint64_t PCRelVal = Value + RE.Addend - FinalAddress;
      assert(isInt<28>(PCRelVal) && "Branch target is out of range.");
      write32le(Target, (read32le(Target) & ~(0x03FFFFFF)) |
                            (PCRelVal & 0x0FFFFFFC) >> 2);
      break;
    }
    case COFF::IMAGE_REL_ARM64_BRANCH19: {
      // The 19-bit offset to the relocation target,
      // for conditional B instruction.
      uint64_t PCRelVal = Value + RE.Addend - FinalAddress;
      assert(isInt<21>(PCRelVal) && "Branch target is out of range.");
      write32le(Target, (read32le(Target) & ~(0x00FFFFE0)) |
                            (PCRelVal & 0x001FFFFC) << 3);
      break;
    }
    case COFF::IMAGE_REL_ARM64_BRANCH14: {
      // The 14-bit offset to the relocation target,
      // for instructions TBZ and TBNZ.
      uint64_t PCRelVal = Value + RE.Addend - FinalAddress;
      assert(isInt<16>(PCRelVal) && "Branch target is out of range.");
      write32le(Target, (read32le(Target) & ~(0x000FFFE0)) |
                            (PCRelVal & 0x0000FFFC) << 3);
      break;
    }
    case COFF::IMAGE_REL_ARM64_ADDR64: {
      // The 64-bit VA of the relocation target.
      write64le(Target, Value + RE.Addend);
      break;
    }
    case COFF::IMAGE_REL_ARM64_SECTION: {
      // 16-bit section index of the section that contains the target.
      assert(static_cast<uint32_t>(RE.SectionID) <= UINT16_MAX &&
             "relocation overflow");
      add16(Target, RE.SectionID);
      break;
    }
    case COFF::IMAGE_REL_ARM64_SECREL: {
      // 32-bit offset of the target from the beginning of its section.
      assert(static_cast<int64_t>(RE.Addend) <= INT32_MAX &&
             "Relocation overflow");
      assert(static_cast<int64_t>(RE.Addend) >= INT32_MIN &&
             "Relocation underflow");
      write32le(Target, RE.Addend);
      break;
    }
    case COFF::IMAGE_REL_ARM64_REL32: {
      // The 32-bit relative address from the byte following the relocation.
      uint64_t Result = Value - FinalAddress - 4;
      write32le(Target, Result + RE.Addend);
      break;
    }
    }
  }

  void registerEHFrames() override {}
};

} // End namespace llvm

#endif
