//===- llvm/MC/MachineLocation.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// The MachineLocation class is used to represent a simple location in a machine
// frame.  Locations will be one of two forms; a register or an address formed
// from a base address plus an offset.  Register indirection can be specified by
// explicitly passing an offset to the constructor.
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MACHINELOCATION_H
#define LLVM_MC_MACHINELOCATION_H

#include <cstdint>
#include <cassert>

namespace llvm {

class MachineLocation {
private:
  bool IsRegister = false;              ///< True if location is a register.
  unsigned Register = 0;                ///< gcc/gdb register number.

public:
  enum : uint32_t {
    // The target register number for an abstract frame pointer. The value is
    // an arbitrary value that doesn't collide with any real target register.
    VirtualFP = ~0U
  };

  MachineLocation() = default;
  /// Create a direct register location.
  explicit MachineLocation(unsigned R, bool Indirect = false)
      : IsRegister(!Indirect), Register(R) {}

  bool operator==(const MachineLocation &Other) const {
    return IsRegister == Other.IsRegister && Register == Other.Register;
  }

  // Accessors.
  /// \return true iff this is a register-indirect location.
  bool isIndirect()      const { return !IsRegister; }
  bool isReg()           const { return IsRegister; }
  unsigned getReg()      const { return Register; }
  void setIsRegister(bool Is)  { IsRegister = Is; }
  void setRegister(unsigned R) { Register = R; }
};

inline bool operator!=(const MachineLocation &LHS, const MachineLocation &RHS) {
  return !(LHS == RHS);
}

} // end namespace llvm

#endif // LLVM_MC_MACHINELOCATION_H
