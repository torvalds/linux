//===-- AVRTargetObjectFile.cpp - AVR Object Files ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AVRTargetObjectFile.h"
#include "AVRTargetMachine.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"

#include "AVR.h"

namespace llvm {
void AVRTargetObjectFile::Initialize(MCContext &Ctx, const TargetMachine &TM) {
  Base::Initialize(Ctx, TM);
  ProgmemDataSection =
      Ctx.getELFSection(".progmem.data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  Progmem1DataSection =
      Ctx.getELFSection(".progmem1.data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  Progmem2DataSection =
      Ctx.getELFSection(".progmem2.data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  Progmem3DataSection =
      Ctx.getELFSection(".progmem3.data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  Progmem4DataSection =
      Ctx.getELFSection(".progmem4.data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  Progmem5DataSection =
      Ctx.getELFSection(".progmem5.data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
}

MCSection *AVRTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // Global values in flash memory are placed in the progmem*.data section
  // unless they already have a user assigned section.
  const auto &AVRTM = static_cast<const AVRTargetMachine &>(TM);
  if (AVR::isProgramMemoryAddress(GO) && !GO->hasSection() &&
      Kind.isReadOnly()) {
    // The AVR subtarget should support LPM to access section '.progmem*.data'.
    if (!AVRTM.getSubtargetImpl()->hasLPM()) {
      // TODO: Get the global object's location in source file.
      getContext().reportError(
          SMLoc(),
          "Current AVR subtarget does not support accessing program memory");
      return Base::SelectSectionForGlobal(GO, Kind, TM);
    }
    // The AVR subtarget should support ELPM to access section
    // '.progmem[1|2|3|4|5].data'.
    if (!AVRTM.getSubtargetImpl()->hasELPM() &&
        AVR::getAddressSpace(GO) != AVR::ProgramMemory) {
      // TODO: Get the global object's location in source file.
      getContext().reportError(SMLoc(),
                               "Current AVR subtarget does not support "
                               "accessing extended program memory");
      return ProgmemDataSection;
    }
    switch (AVR::getAddressSpace(GO)) {
    case AVR::ProgramMemory: // address space 1
      return ProgmemDataSection;
    case AVR::ProgramMemory1: // address space 2
      return Progmem1DataSection;
    case AVR::ProgramMemory2: // address space 3
      return Progmem2DataSection;
    case AVR::ProgramMemory3: // address space 4
      return Progmem3DataSection;
    case AVR::ProgramMemory4: // address space 5
      return Progmem4DataSection;
    case AVR::ProgramMemory5: // address space 6
      return Progmem5DataSection;
    default:
      llvm_unreachable("unexpected program memory index");
    }
  }

  // Otherwise, we work the same way as ELF.
  return Base::SelectSectionForGlobal(GO, Kind, TM);
}
} // end of namespace llvm
