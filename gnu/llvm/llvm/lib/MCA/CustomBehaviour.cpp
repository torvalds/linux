//===--------------------- CustomBehaviour.cpp ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods from the CustomBehaviour interface.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/CustomBehaviour.h"

namespace llvm {
namespace mca {

CustomBehaviour::~CustomBehaviour() = default;

unsigned CustomBehaviour::checkCustomHazard(ArrayRef<InstRef> IssuedInst,
                                            const InstRef &IR) {
  // 0 signifies that there are no hazards that need to be waited on
  return 0;
}

std::vector<std::unique_ptr<View>>
CustomBehaviour::getStartViews(llvm::MCInstPrinter &IP,
                               llvm::ArrayRef<llvm::MCInst> Insts) {
  return std::vector<std::unique_ptr<View>>();
}

std::vector<std::unique_ptr<View>>
CustomBehaviour::getPostInstrInfoViews(llvm::MCInstPrinter &IP,
                                       llvm::ArrayRef<llvm::MCInst> Insts) {
  return std::vector<std::unique_ptr<View>>();
}

std::vector<std::unique_ptr<View>>
CustomBehaviour::getEndViews(llvm::MCInstPrinter &IP,
                             llvm::ArrayRef<llvm::MCInst> Insts) {
  return std::vector<std::unique_ptr<View>>();
}

UniqueInstrument InstrumentManager::createInstrument(llvm::StringRef Desc,
                                                     llvm::StringRef Data) {
  return std::make_unique<Instrument>(Desc, Data);
}

SmallVector<UniqueInstrument>
InstrumentManager::createInstruments(const MCInst &Inst) {
  return SmallVector<UniqueInstrument>();
}

unsigned InstrumentManager::getSchedClassID(
    const MCInstrInfo &MCII, const MCInst &MCI,
    const llvm::SmallVector<Instrument *> &IVec) const {
  return MCII.get(MCI.getOpcode()).getSchedClass();
}

} // namespace mca
} // namespace llvm
