//===- lib/MC/MCSectionDXContainer.cpp - DXContainer Section --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSectionDXContainer.h"

using namespace llvm;

void MCSectionDXContainer::printSwitchToSection(const MCAsmInfo &,
                                                const Triple &, raw_ostream &,
                                                uint32_t) const {}
