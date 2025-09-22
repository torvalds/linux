//===- MCAsmInfoGOFF.cpp - MCGOFFAsmInfo properties -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines certain target specific asm properties for GOFF (z/OS)
/// based targets.
///
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfoGOFF.h"

using namespace llvm;

void MCAsmInfoGOFF::anchor() {}

MCAsmInfoGOFF::MCAsmInfoGOFF() {
  Data64bitsDirective = "\t.quad\t";
  HasDotTypeDotSizeDirective = false;
  PrivateGlobalPrefix = "L#";
  PrivateLabelPrefix = "L#";
  ZeroDirective = "\t.space\t";
}
