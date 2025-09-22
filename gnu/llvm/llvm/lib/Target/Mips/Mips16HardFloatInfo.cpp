//===---- Mips16HardFloatInfo.cpp for Mips16 Hard Float              -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips16 implementation of Mips16HardFloatInfo
// namespace.
//
//===----------------------------------------------------------------------===//

#include "Mips16HardFloatInfo.h"
#include <string.h>

namespace llvm {

namespace Mips16HardFloatInfo {

const FuncNameSignature PredefinedFuncs[] = {
  { "__floatdidf", { NoSig, DRet } },
  { "__floatdisf", { NoSig, FRet } },
  { "__floatundidf", { NoSig, DRet } },
  { "__fixsfdi", { FSig, NoFPRet } },
  { "__fixunsdfsi", { DSig, NoFPRet } },
  { "__fixunsdfdi", { DSig, NoFPRet } },
  { "__fixdfdi", { DSig, NoFPRet } },
  { "__fixunssfsi", { FSig, NoFPRet } },
  { "__fixunssfdi", { FSig, NoFPRet } },
  { "__floatundisf", { NoSig, FRet } },
  { nullptr, { NoSig, NoFPRet } }
};

// just do a search for now. there are very few of these special cases.
//
extern FuncSignature const *findFuncSignature(const char *name) {
  const char *name_;
  int i = 0;
  while (PredefinedFuncs[i].Name) {
    name_ = PredefinedFuncs[i].Name;
    if (strcmp(name, name_) == 0)
      return &PredefinedFuncs[i].Signature;
    i++;
  }
  return nullptr;
}
}
}
