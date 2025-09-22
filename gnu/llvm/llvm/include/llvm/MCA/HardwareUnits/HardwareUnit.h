//===-------------------------- HardwareUnit.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a base class for describing a simulated hardware
/// unit.  These units are used to construct a simulated backend.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNITS_HARDWAREUNIT_H
#define LLVM_MCA_HARDWAREUNITS_HARDWAREUNIT_H

namespace llvm {
namespace mca {

class HardwareUnit {
  HardwareUnit(const HardwareUnit &H) = delete;
  HardwareUnit &operator=(const HardwareUnit &H) = delete;

public:
  HardwareUnit() = default;
  virtual ~HardwareUnit();
};

} // namespace mca
} // namespace llvm
#endif // LLVM_MCA_HARDWAREUNITS_HARDWAREUNIT_H
