//===---- MipsABIInfo.h - Information about MIPS ABI's --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSABIINFO_H
#define LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSABIINFO_H

#include "llvm/IR/CallingConv.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

template <typename T> class ArrayRef;
class MCTargetOptions;
class StringRef;

class MipsABIInfo {
public:
  enum class ABI { Unknown, O32, N32, N64 };

protected:
  ABI ThisABI;

public:
  MipsABIInfo(ABI ThisABI) : ThisABI(ThisABI) {}

  static MipsABIInfo Unknown() { return MipsABIInfo(ABI::Unknown); }
  static MipsABIInfo O32() { return MipsABIInfo(ABI::O32); }
  static MipsABIInfo N32() { return MipsABIInfo(ABI::N32); }
  static MipsABIInfo N64() { return MipsABIInfo(ABI::N64); }
  static MipsABIInfo computeTargetABI(const Triple &TT, StringRef CPU,
                                      const MCTargetOptions &Options);

  bool IsKnown() const { return ThisABI != ABI::Unknown; }
  bool IsO32() const { return ThisABI == ABI::O32; }
  bool IsN32() const { return ThisABI == ABI::N32; }
  bool IsN64() const { return ThisABI == ABI::N64; }
  ABI GetEnumValue() const { return ThisABI; }

  /// The registers to use for byval arguments.
  ArrayRef<MCPhysReg> GetByValArgRegs() const;

  /// The registers to use for the variable argument list.
  ArrayRef<MCPhysReg> GetVarArgRegs() const;

  /// Obtain the size of the area allocated by the callee for arguments.
  /// CallingConv::FastCall affects the value for O32.
  unsigned GetCalleeAllocdArgSizeInBytes(CallingConv::ID CC) const;

  /// Ordering of ABI's
  /// MipsGenSubtargetInfo.inc will use this to resolve conflicts when given
  /// multiple ABI options.
  bool operator<(const MipsABIInfo Other) const {
    return ThisABI < Other.GetEnumValue();
  }

  unsigned GetStackPtr() const;
  unsigned GetFramePtr() const;
  unsigned GetBasePtr() const;
  unsigned GetGlobalPtr() const;
  unsigned GetNullPtr() const;
  unsigned GetZeroReg() const;
  unsigned GetPtrAdduOp() const;
  unsigned GetPtrAddiuOp() const;
  unsigned GetPtrSubuOp() const;
  unsigned GetPtrAndOp() const;
  unsigned GetGPRMoveOp() const;
  inline bool ArePtrs64bit() const { return IsN64(); }
  inline bool AreGprs64bit() const { return IsN32() || IsN64(); }

  unsigned GetEhDataReg(unsigned I) const;
};
}

#endif
