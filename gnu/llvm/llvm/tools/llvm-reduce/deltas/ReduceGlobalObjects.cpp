//===- ReduceGlobalObjects.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ReduceGlobalObjects.h"
#include "llvm/IR/GlobalObject.h"

using namespace llvm;

static bool shouldReduceSection(GlobalObject &GO) { return GO.hasSection(); }

static bool shouldReduceAlign(GlobalObject &GO) {
  return GO.getAlign().has_value();
}

static bool shouldReduceComdat(GlobalObject &GO) { return GO.hasComdat(); }

static void reduceGOs(Oracle &O, ReducerWorkItem &Program) {
  for (auto &GO : Program.getModule().global_objects()) {
    if (shouldReduceSection(GO) && !O.shouldKeep())
      GO.setSection("");
    if (shouldReduceAlign(GO) && !O.shouldKeep())
      GO.setAlignment(MaybeAlign());
    if (shouldReduceComdat(GO) && !O.shouldKeep())
      GO.setComdat(nullptr);
  }
}

void llvm::reduceGlobalObjectsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceGOs, "Reducing GlobalObjects");
}
