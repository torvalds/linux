//===- MipsABIFlagsSection.cpp - Mips ELF ABI Flags Section ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MipsABIFlagsSection.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MipsABIFlags.h"

using namespace llvm;

uint8_t MipsABIFlagsSection::getFpABIValue() {
  switch (FpABI) {
  case FpABIKind::ANY:
    return Mips::Val_GNU_MIPS_ABI_FP_ANY;
  case FpABIKind::SOFT:
    return Mips::Val_GNU_MIPS_ABI_FP_SOFT;
  case FpABIKind::XX:
    return Mips::Val_GNU_MIPS_ABI_FP_XX;
  case FpABIKind::S32:
    return Mips::Val_GNU_MIPS_ABI_FP_DOUBLE;
  case FpABIKind::S64:
    if (Is32BitABI)
      return OddSPReg ? Mips::Val_GNU_MIPS_ABI_FP_64
                      : Mips::Val_GNU_MIPS_ABI_FP_64A;
    return Mips::Val_GNU_MIPS_ABI_FP_DOUBLE;
  }

  llvm_unreachable("unexpected fp abi value");
}

StringRef MipsABIFlagsSection::getFpABIString(FpABIKind Value) {
  switch (Value) {
  case FpABIKind::XX:
    return "xx";
  case FpABIKind::S32:
    return "32";
  case FpABIKind::S64:
    return "64";
  default:
    llvm_unreachable("unsupported fp abi value");
  }
}

uint8_t MipsABIFlagsSection::getCPR1SizeValue() {
  if (FpABI == FpABIKind::XX)
    return (uint8_t)Mips::AFL_REG_32;
  return (uint8_t)CPR1Size;
}

namespace llvm {

MCStreamer &operator<<(MCStreamer &OS, MipsABIFlagsSection &ABIFlagsSection) {
  // Write out a Elf_Internal_ABIFlags_v0 struct
  OS.emitIntValue(ABIFlagsSection.getVersionValue(), 2);      // version
  OS.emitIntValue(ABIFlagsSection.getISALevelValue(), 1);     // isa_level
  OS.emitIntValue(ABIFlagsSection.getISARevisionValue(), 1);  // isa_rev
  OS.emitIntValue(ABIFlagsSection.getGPRSizeValue(), 1);      // gpr_size
  OS.emitIntValue(ABIFlagsSection.getCPR1SizeValue(), 1);     // cpr1_size
  OS.emitIntValue(ABIFlagsSection.getCPR2SizeValue(), 1);     // cpr2_size
  OS.emitIntValue(ABIFlagsSection.getFpABIValue(), 1);        // fp_abi
  OS.emitIntValue(ABIFlagsSection.getISAExtensionValue(), 4); // isa_ext
  OS.emitIntValue(ABIFlagsSection.getASESetValue(), 4);       // ases
  OS.emitIntValue(ABIFlagsSection.getFlags1Value(), 4);       // flags1
  OS.emitIntValue(ABIFlagsSection.getFlags2Value(), 4);       // flags2
  return OS;
}

} // end namespace llvm
