//===--- RuntimeDyldCOFFI386.h --- COFF/X86_64 specific code ---*- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// COFF x86 support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFI386_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFI386_H

#include "../RuntimeDyldCOFF.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"

#define DEBUG_TYPE "dyld"

namespace llvm {

class RuntimeDyldCOFFI386 : public RuntimeDyldCOFF {
public:
  RuntimeDyldCOFFI386(RuntimeDyld::MemoryManager &MM,
                      JITSymbolResolver &Resolver)
      : RuntimeDyldCOFF(MM, Resolver, 4, COFF::IMAGE_REL_I386_DIR32) {}

  unsigned getMaxStubSize() const override {
    return 8; // 2-byte jmp instruction + 32-bit relative address + 2 byte pad
  }

  Align getStubAlignment() override { return Align(1); }

  Expected<object::relocation_iterator>
  processRelocationRef(unsigned SectionID,
                       object::relocation_iterator RelI,
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
    bool IsExtern = Section == Obj.section_end();

    uint64_t RelType = RelI->getType();
    uint64_t Offset = RelI->getOffset();

    unsigned TargetSectionID = -1;
    uint64_t TargetOffset = -1;
    if (TargetName.starts_with(getImportSymbolPrefix())) {
      TargetSectionID = SectionID;
      TargetOffset = getDLLImportOffset(SectionID, Stubs, TargetName, true);
      TargetName = StringRef();
      IsExtern = false;
    } else if (!IsExtern) {
      if (auto TargetSectionIDOrErr = findOrEmitSection(
              Obj, *Section, Section->isText(), ObjSectionToID))
        TargetSectionID = *TargetSectionIDOrErr;
      else
        return TargetSectionIDOrErr.takeError();
      if (RelType != COFF::IMAGE_REL_I386_SECTION)
        TargetOffset = getSymbolOffset(*Symbol);
    }

    // Determine the Addend used to adjust the relocation value.
    uint64_t Addend = 0;
    SectionEntry &AddendSection = Sections[SectionID];
    uintptr_t ObjTarget = AddendSection.getObjAddress() + Offset;
    uint8_t *Displacement = (uint8_t *)ObjTarget;

    switch (RelType) {
    case COFF::IMAGE_REL_I386_DIR32:
    case COFF::IMAGE_REL_I386_DIR32NB:
    case COFF::IMAGE_REL_I386_SECREL:
    case COFF::IMAGE_REL_I386_REL32: {
      Addend = readBytesUnaligned(Displacement, 4);
      break;
    }
    default:
      break;
    }

#if !defined(NDEBUG)
    SmallString<32> RelTypeName;
    RelI->getTypeName(RelTypeName);
#endif
    LLVM_DEBUG(dbgs() << "\t\tIn Section " << SectionID << " Offset " << Offset
                      << " RelType: " << RelTypeName << " TargetName: "
                      << TargetName << " Addend " << Addend << "\n");

    if (IsExtern) {
      RelocationEntry RE(SectionID, Offset, RelType, 0, -1, 0, 0, 0, false, 0);
      addRelocationForSymbol(RE, TargetName);
    } else {

      switch (RelType) {
      case COFF::IMAGE_REL_I386_ABSOLUTE:
        // This relocation is ignored.
        break;
      case COFF::IMAGE_REL_I386_DIR32:
      case COFF::IMAGE_REL_I386_DIR32NB:
      case COFF::IMAGE_REL_I386_REL32: {
        RelocationEntry RE =
            RelocationEntry(SectionID, Offset, RelType, Addend, TargetSectionID,
                            TargetOffset, 0, 0, false, 0);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      case COFF::IMAGE_REL_I386_SECTION: {
        RelocationEntry RE =
            RelocationEntry(TargetSectionID, Offset, RelType, 0);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      case COFF::IMAGE_REL_I386_SECREL: {
        RelocationEntry RE =
            RelocationEntry(SectionID, Offset, RelType, TargetOffset + Addend);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      default:
        llvm_unreachable("unsupported relocation type");
      }
    }

    return ++RelI;
  }

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override {
    const auto Section = Sections[RE.SectionID];
    uint8_t *Target = Section.getAddressWithOffset(RE.Offset);

    switch (RE.RelType) {
    case COFF::IMAGE_REL_I386_ABSOLUTE:
      // This relocation is ignored.
      break;
    case COFF::IMAGE_REL_I386_DIR32: {
      // The target's 32-bit VA.
      uint64_t Result =
          RE.Sections.SectionA == static_cast<uint32_t>(-1)
              ? Value
              : Sections[RE.Sections.SectionA].getLoadAddressWithOffset(
                    RE.Addend);
      assert(Result <= UINT32_MAX && "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_I386_DIR32"
                        << " TargetSection: " << RE.Sections.SectionA
                        << " Value: " << format("0x%08" PRIx32, Result)
                        << '\n');
      writeBytesUnaligned(Result, Target, 4);
      break;
    }
    case COFF::IMAGE_REL_I386_DIR32NB: {
      // The target's 32-bit RVA.
      // NOTE: use Section[0].getLoadAddress() as an approximation of ImageBase
      uint64_t Result =
          Sections[RE.Sections.SectionA].getLoadAddressWithOffset(RE.Addend) -
          Sections[0].getLoadAddress();
      assert(Result <= UINT32_MAX && "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_I386_DIR32NB"
                        << " TargetSection: " << RE.Sections.SectionA
                        << " Value: " << format("0x%08" PRIx32, Result)
                        << '\n');
      writeBytesUnaligned(Result, Target, 4);
      break;
    }
    case COFF::IMAGE_REL_I386_REL32: {
      // 32-bit relative displacement to the target.
      uint64_t Result = RE.Sections.SectionA == static_cast<uint32_t>(-1)
                            ? Value
                            : Sections[RE.Sections.SectionA].getLoadAddress();
      Result = Result - Section.getLoadAddress() + RE.Addend - 4 - RE.Offset;
      assert(static_cast<int64_t>(Result) <= INT32_MAX &&
             "relocation overflow");
      assert(static_cast<int64_t>(Result) >= INT32_MIN &&
             "relocation underflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_I386_REL32"
                        << " TargetSection: " << RE.Sections.SectionA
                        << " Value: " << format("0x%08" PRIx32, Result)
                        << '\n');
      writeBytesUnaligned(Result, Target, 4);
      break;
    }
    case COFF::IMAGE_REL_I386_SECTION:
      // 16-bit section index of the section that contains the target.
      assert(static_cast<uint32_t>(RE.SectionID) <= UINT16_MAX &&
             "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_I386_SECTION Value: "
                        << RE.SectionID << '\n');
      writeBytesUnaligned(RE.SectionID, Target, 2);
      break;
    case COFF::IMAGE_REL_I386_SECREL:
      // 32-bit offset of the target from the beginning of its section.
      assert(static_cast<uint64_t>(RE.Addend) <= UINT32_MAX &&
             "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_I386_SECREL Value: "
                        << RE.Addend << '\n');
      writeBytesUnaligned(RE.Addend, Target, 4);
      break;
    default:
      llvm_unreachable("unsupported relocation type");
    }
  }

  void registerEHFrames() override {}
};

}

#endif

