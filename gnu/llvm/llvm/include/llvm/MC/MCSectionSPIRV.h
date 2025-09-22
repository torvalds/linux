//===- MCSectionSPIRV.h - SPIR-V Machine Code Sections ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionSPIRV class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONSPIRV_H
#define LLVM_MC_MCSECTIONSPIRV_H

#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"

namespace llvm {

class MCSymbol;

class MCSectionSPIRV final : public MCSection {
  friend class MCContext;

  MCSectionSPIRV()
      : MCSection(SV_SPIRV, "", /*IsText=*/true, /*IsVirtual=*/false,
                  /*Begin=*/nullptr) {}
  // TODO: Add StringRef Name to MCSectionSPIRV.

public:
  ~MCSectionSPIRV() = default;
  void printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            uint32_t Subsection) const override {}
  bool useCodeAlign() const override { return false; }
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTIONSPIRV_H
