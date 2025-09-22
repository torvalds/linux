//===-- BPFMCAsmInfo.h - BPF asm properties -------------------*- C++ -*--====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the BPFMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_MCTARGETDESC_BPFMCASMINFO_H
#define LLVM_LIB_TARGET_BPF_MCTARGETDESC_BPFMCASMINFO_H

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class BPFMCAsmInfo : public MCAsmInfo {
public:
  explicit BPFMCAsmInfo(const Triple &TT, const MCTargetOptions &Options) {
    if (TT.getArch() == Triple::bpfeb)
      IsLittleEndian = false;

    PrivateGlobalPrefix = ".L";
    WeakRefDirective = "\t.weak\t";

    UsesELFSectionDirectiveForBSS = true;
    HasSingleParameterDotFile = true;
    HasDotTypeDotSizeDirective = true;

    SupportsDebugInformation = true;
    ExceptionsType = ExceptionHandling::DwarfCFI;
    MinInstAlignment = 8;

    // the default is 4 and it only affects dwarf elf output
    // so if not set correctly, the dwarf data will be
    // messed up in random places by 4 bytes. .debug_line
    // section will be parsable, but with odd offsets and
    // line numbers, etc.
    CodePointerSize = 8;
  }

  void setDwarfUsesRelocationsAcrossSections(bool enable) {
    DwarfUsesRelocationsAcrossSections = enable;
  }
};
}

#endif
