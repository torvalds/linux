//===- SystemZConstantPoolValue.h - SystemZ constant-pool value -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZCONSTANTPOOLVALUE_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZCONSTANTPOOLVALUE_H

#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {

class GlobalValue;

namespace SystemZCP {
enum SystemZCPModifier {
  TLSGD,
  TLSLDM,
  DTPOFF,
  NTPOFF
};
} // end namespace SystemZCP

/// A SystemZ-specific constant pool value.  At present, the only
/// defined constant pool values are module IDs or offsets of
/// thread-local variables (written x@TLSGD, x@TLSLDM, x@DTPOFF,
/// or x@NTPOFF).
class SystemZConstantPoolValue : public MachineConstantPoolValue {
  const GlobalValue *GV;
  SystemZCP::SystemZCPModifier Modifier;

protected:
  SystemZConstantPoolValue(const GlobalValue *GV,
                           SystemZCP::SystemZCPModifier Modifier);

public:
  static SystemZConstantPoolValue *
    Create(const GlobalValue *GV, SystemZCP::SystemZCPModifier Modifier);

  // Override MachineConstantPoolValue.
  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  // Access SystemZ-specific fields.
  const GlobalValue *getGlobalValue() const { return GV; }
  SystemZCP::SystemZCPModifier getModifier() const { return Modifier; }
};

} // end namespace llvm

#endif
