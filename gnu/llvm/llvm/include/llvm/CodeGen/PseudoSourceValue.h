//===-- llvm/CodeGen/PseudoSourceValue.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the PseudoSourceValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PSEUDOSOURCEVALUE_H
#define LLVM_CODEGEN_PSEUDOSOURCEVALUE_H

namespace llvm {

class GlobalValue;
class MachineFrameInfo;
class MachineMemOperand;
class MIRFormatter;
class PseudoSourceValue;
class raw_ostream;
class TargetMachine;

raw_ostream &operator<<(raw_ostream &OS, const PseudoSourceValue* PSV);

/// Special value supplied for machine level alias analysis. It indicates that
/// a memory access references the functions stack frame (e.g., a spill slot),
/// below the stack frame (e.g., argument space), or constant pool.
class PseudoSourceValue {
public:
  enum PSVKind : unsigned {
    Stack,
    GOT,
    JumpTable,
    ConstantPool,
    FixedStack,
    GlobalValueCallEntry,
    ExternalSymbolCallEntry,
    TargetCustom
  };

private:
  unsigned Kind;
  unsigned AddressSpace;
  friend raw_ostream &llvm::operator<<(raw_ostream &OS,
                                       const PseudoSourceValue* PSV);

  friend class MachineMemOperand; // For printCustom().
  friend class MIRFormatter;      // For printCustom().

  /// Implement printing for PseudoSourceValue. This is called from
  /// Value::print or Value's operator<<.
  virtual void printCustom(raw_ostream &O) const;

public:
  explicit PseudoSourceValue(unsigned Kind, const TargetMachine &TM);

  virtual ~PseudoSourceValue();

  unsigned kind() const { return Kind; }

  bool isStack() const { return Kind == Stack; }
  bool isGOT() const { return Kind == GOT; }
  bool isConstantPool() const { return Kind == ConstantPool; }
  bool isJumpTable() const { return Kind == JumpTable; }

  unsigned getAddressSpace() const { return AddressSpace; }

  unsigned getTargetCustom() const {
    return (Kind >= TargetCustom) ? ((Kind+1) - TargetCustom) : 0;
  }

  /// Test whether the memory pointed to by this PseudoSourceValue has a
  /// constant value.
  virtual bool isConstant(const MachineFrameInfo *) const;

  /// Test whether the memory pointed to by this PseudoSourceValue may also be
  /// pointed to by an LLVM IR Value.
  virtual bool isAliased(const MachineFrameInfo *) const;

  /// Return true if the memory pointed to by this PseudoSourceValue can ever
  /// alias an LLVM IR Value.
  virtual bool mayAlias(const MachineFrameInfo *) const;
};

/// A specialized PseudoSourceValue for holding FixedStack values, which must
/// include a frame index.
class FixedStackPseudoSourceValue : public PseudoSourceValue {
  const int FI;

public:
  explicit FixedStackPseudoSourceValue(int FI, const TargetMachine &TM)
      : PseudoSourceValue(FixedStack, TM), FI(FI) {}

  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == FixedStack;
  }

  bool isConstant(const MachineFrameInfo *MFI) const override;

  bool isAliased(const MachineFrameInfo *MFI) const override;

  bool mayAlias(const MachineFrameInfo *) const override;

  void printCustom(raw_ostream &OS) const override;

  int getFrameIndex() const { return FI; }
};

class CallEntryPseudoSourceValue : public PseudoSourceValue {
protected:
  CallEntryPseudoSourceValue(unsigned Kind, const TargetMachine &TM);

public:
  bool isConstant(const MachineFrameInfo *) const override;
  bool isAliased(const MachineFrameInfo *) const override;
  bool mayAlias(const MachineFrameInfo *) const override;
};

/// A specialized pseudo source value for holding GlobalValue values.
class GlobalValuePseudoSourceValue : public CallEntryPseudoSourceValue {
  const GlobalValue *GV;

public:
  GlobalValuePseudoSourceValue(const GlobalValue *GV, const TargetMachine &TM);

  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == GlobalValueCallEntry;
  }

  const GlobalValue *getValue() const { return GV; }
};

/// A specialized pseudo source value for holding external symbol values.
class ExternalSymbolPseudoSourceValue : public CallEntryPseudoSourceValue {
  const char *ES;

public:
  ExternalSymbolPseudoSourceValue(const char *ES, const TargetMachine &TM);

  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == ExternalSymbolCallEntry;
  }

  const char *getSymbol() const { return ES; }
};

} // end namespace llvm

#endif
