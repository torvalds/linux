//===- lib/MC/MCWinEH.cpp - Windows EH implementation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCWinEH.h"

namespace llvm {
namespace WinEH {

UnwindEmitter::~UnwindEmitter() = default;

}
}

