//===- SDNodeProperties.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SDNodeProperties.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

unsigned llvm::parseSDPatternOperatorProperties(Record *R) {
  unsigned Properties = 0;
  for (Record *Property : R->getValueAsListOfDefs("Properties")) {
    auto Offset = StringSwitch<unsigned>(Property->getName())
                      .Case("SDNPCommutative", SDNPCommutative)
                      .Case("SDNPAssociative", SDNPAssociative)
                      .Case("SDNPHasChain", SDNPHasChain)
                      .Case("SDNPOutGlue", SDNPOutGlue)
                      .Case("SDNPInGlue", SDNPInGlue)
                      .Case("SDNPOptInGlue", SDNPOptInGlue)
                      .Case("SDNPMayStore", SDNPMayStore)
                      .Case("SDNPMayLoad", SDNPMayLoad)
                      .Case("SDNPSideEffect", SDNPSideEffect)
                      .Case("SDNPMemOperand", SDNPMemOperand)
                      .Case("SDNPVariadic", SDNPVariadic)
                      .Default(-1u);
    if (Offset != -1u)
      Properties |= 1 << Offset;
    else
      PrintFatalError(R->getLoc(), "Unknown SD Node property '" +
                                       Property->getName() + "' on node '" +
                                       R->getName() + "'!");
  }
  return Properties;
}
