//===-------------------- RISCVCustomBehaviour.h -----------------*-C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the RISCVCustomBehaviour class which inherits from
/// CustomBehaviour. This class is used by the tool llvm-mca to enforce
/// target specific behaviour that is not expressed well enough in the
/// scheduling model for mca to enforce it automatically.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MCA_RISCVCUSTOMBEHAVIOUR_H
#define LLVM_LIB_TARGET_RISCV_MCA_RISCVCUSTOMBEHAVIOUR_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MCA/CustomBehaviour.h"

namespace llvm {
namespace mca {

class RISCVLMULInstrument : public Instrument {
public:
  static const StringRef DESC_NAME;
  static bool isDataValid(StringRef Data);

  explicit RISCVLMULInstrument(StringRef Data) : Instrument(DESC_NAME, Data) {}

  ~RISCVLMULInstrument() = default;

  uint8_t getLMUL() const;
};

class RISCVSEWInstrument : public Instrument {
public:
  static const StringRef DESC_NAME;
  static bool isDataValid(StringRef Data);

  explicit RISCVSEWInstrument(StringRef Data) : Instrument(DESC_NAME, Data) {}

  ~RISCVSEWInstrument() = default;

  uint8_t getSEW() const;
};

class RISCVInstrumentManager : public InstrumentManager {
public:
  RISCVInstrumentManager(const MCSubtargetInfo &STI, const MCInstrInfo &MCII)
      : InstrumentManager(STI, MCII) {}

  bool shouldIgnoreInstruments() const override { return false; }
  bool supportsInstrumentType(StringRef Type) const override;

  /// Create a Instrument for RISC-V target
  UniqueInstrument createInstrument(StringRef Desc, StringRef Data) override;

  SmallVector<UniqueInstrument> createInstruments(const MCInst &Inst) override;

  /// Using the Instrument, returns a SchedClassID to use instead of
  /// the SchedClassID that belongs to the MCI or the original SchedClassID.
  unsigned
  getSchedClassID(const MCInstrInfo &MCII, const MCInst &MCI,
                  const SmallVector<Instrument *> &IVec) const override;
};

} // namespace mca
} // namespace llvm

#endif
