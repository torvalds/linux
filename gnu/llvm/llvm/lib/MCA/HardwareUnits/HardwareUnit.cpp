//===------------------------- HardwareUnit.cpp -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the anchor for the base class that describes
/// simulated hardware units.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/HardwareUnits/HardwareUnit.h"

namespace llvm {
namespace mca {

// Pin the vtable with this method.
HardwareUnit::~HardwareUnit() = default;

} // namespace mca
} // namespace llvm
