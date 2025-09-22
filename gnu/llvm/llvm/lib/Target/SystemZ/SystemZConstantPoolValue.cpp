//===-- SystemZConstantPoolValue.cpp - SystemZ constant-pool value --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemZConstantPoolValue.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

SystemZConstantPoolValue::
SystemZConstantPoolValue(const GlobalValue *gv,
                         SystemZCP::SystemZCPModifier modifier)
  : MachineConstantPoolValue(gv->getType()), GV(gv), Modifier(modifier) {}

SystemZConstantPoolValue *
SystemZConstantPoolValue::Create(const GlobalValue *GV,
                                 SystemZCP::SystemZCPModifier Modifier) {
  return new SystemZConstantPoolValue(GV, Modifier);
}

int SystemZConstantPoolValue::getExistingMachineCPValue(MachineConstantPool *CP,
                                                        Align Alignment) {
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned I = 0, E = Constants.size(); I != E; ++I) {
    if (Constants[I].isMachineConstantPoolEntry() &&
        Constants[I].getAlign() >= Alignment) {
      auto *ZCPV =
        static_cast<SystemZConstantPoolValue *>(Constants[I].Val.MachineCPVal);
      if (ZCPV->GV == GV && ZCPV->Modifier == Modifier)
        return I;
    }
  }
  return -1;
}

void SystemZConstantPoolValue::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddPointer(GV);
  ID.AddInteger(Modifier);
}

void SystemZConstantPoolValue::print(raw_ostream &O) const {
  O << GV << "@" << int(Modifier);
}
