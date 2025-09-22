//===- ARMConstantPoolValue.h - ARM constantpool value ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ARM specific constantpool value class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMCONSTANTPOOLVALUE_H
#define LLVM_LIB_TARGET_ARM_ARMCONSTANTPOOLVALUE_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/Support/Casting.h"
#include <string>
#include <vector>

namespace llvm {

class BlockAddress;
class Constant;
class GlobalValue;
class GlobalVariable;
class LLVMContext;
class MachineBasicBlock;
class raw_ostream;
class Type;

namespace ARMCP {

  enum ARMCPKind {
    CPValue,
    CPExtSymbol,
    CPBlockAddress,
    CPLSDA,
    CPMachineBasicBlock,
    CPPromotedGlobal
  };

  enum ARMCPModifier {
    no_modifier, /// None
    TLSGD,       /// Thread Local Storage (General Dynamic Mode)
    GOT_PREL,    /// Global Offset Table, PC Relative
    GOTTPOFF,    /// Global Offset Table, Thread Pointer Offset
    TPOFF,       /// Thread Pointer Offset
    SECREL,      /// Section Relative (Windows TLS)
    SBREL,       /// Static Base Relative (RWPI)
  };

} // end namespace ARMCP

/// ARMConstantPoolValue - ARM specific constantpool value. This is used to
/// represent PC-relative displacement between the address of the load
/// instruction and the constant being loaded, i.e. (&GV-(LPIC+8)).
class ARMConstantPoolValue : public MachineConstantPoolValue {
  unsigned LabelId;        // Label id of the load.
  ARMCP::ARMCPKind Kind;   // Kind of constant.
  unsigned char PCAdjust;  // Extra adjustment if constantpool is pc-relative.
                           // 8 for ARM, 4 for Thumb.
  ARMCP::ARMCPModifier Modifier;   // GV modifier i.e. (&GV(modifier)-(LPIC+8))
  bool AddCurrentAddress;

protected:
  ARMConstantPoolValue(Type *Ty, unsigned id, ARMCP::ARMCPKind Kind,
                       unsigned char PCAdj, ARMCP::ARMCPModifier Modifier,
                       bool AddCurrentAddress);

  ARMConstantPoolValue(LLVMContext &C, unsigned id, ARMCP::ARMCPKind Kind,
                       unsigned char PCAdj, ARMCP::ARMCPModifier Modifier,
                       bool AddCurrentAddress);

  template <typename Derived>
  int getExistingMachineCPValueImpl(MachineConstantPool *CP, Align Alignment) {
    const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
    for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
      if (Constants[i].isMachineConstantPoolEntry() &&
          Constants[i].getAlign() >= Alignment) {
        auto *CPV =
          static_cast<ARMConstantPoolValue*>(Constants[i].Val.MachineCPVal);
        if (Derived *APC = dyn_cast<Derived>(CPV))
          if (cast<Derived>(this)->equals(APC))
            return i;
      }
    }

    return -1;
  }

public:
  ~ARMConstantPoolValue() override;

  ARMCP::ARMCPModifier getModifier() const { return Modifier; }
  StringRef getModifierText() const;
  bool hasModifier() const { return Modifier != ARMCP::no_modifier; }

  bool mustAddCurrentAddress() const { return AddCurrentAddress; }

  unsigned getLabelId() const { return LabelId; }
  unsigned char getPCAdjustment() const { return PCAdjust; }

  bool isGlobalValue() const { return Kind == ARMCP::CPValue; }
  bool isExtSymbol() const { return Kind == ARMCP::CPExtSymbol; }
  bool isBlockAddress() const { return Kind == ARMCP::CPBlockAddress; }
  bool isLSDA() const { return Kind == ARMCP::CPLSDA; }
  bool isMachineBasicBlock() const{ return Kind == ARMCP::CPMachineBasicBlock; }
  bool isPromotedGlobal() const{ return Kind == ARMCP::CPPromotedGlobal; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;

  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;

  /// hasSameValue - Return true if this ARM constpool value can share the same
  /// constantpool entry as another ARM constpool value.
  virtual bool hasSameValue(ARMConstantPoolValue *ACPV);

  bool equals(const ARMConstantPoolValue *A) const {
    return this->LabelId == A->LabelId &&
      this->PCAdjust == A->PCAdjust &&
      this->Modifier == A->Modifier;
  }

  void print(raw_ostream &O) const override;
  void print(raw_ostream *O) const { if (O) print(*O); }
  void dump() const;
};

inline raw_ostream &operator<<(raw_ostream &O, const ARMConstantPoolValue &V) {
  V.print(O);
  return O;
}

/// ARMConstantPoolConstant - ARM-specific constant pool values for Constants,
/// Functions, and BlockAddresses.
class ARMConstantPoolConstant : public ARMConstantPoolValue {
  const Constant *CVal;         // Constant being loaded.
  SmallPtrSet<const GlobalVariable*, 1> GVars;

  ARMConstantPoolConstant(const Constant *C,
                          unsigned ID,
                          ARMCP::ARMCPKind Kind,
                          unsigned char PCAdj,
                          ARMCP::ARMCPModifier Modifier,
                          bool AddCurrentAddress);
  ARMConstantPoolConstant(Type *Ty, const Constant *C,
                          unsigned ID,
                          ARMCP::ARMCPKind Kind,
                          unsigned char PCAdj,
                          ARMCP::ARMCPModifier Modifier,
                          bool AddCurrentAddress);
  ARMConstantPoolConstant(const GlobalVariable *GV, const Constant *Init);

public:
  static ARMConstantPoolConstant *Create(const Constant *C, unsigned ID);
  static ARMConstantPoolConstant *Create(const GlobalValue *GV,
                                         ARMCP::ARMCPModifier Modifier);
  static ARMConstantPoolConstant *Create(const GlobalVariable *GV,
                                         const Constant *Initializer);
  static ARMConstantPoolConstant *Create(const Constant *C, unsigned ID,
                                         ARMCP::ARMCPKind Kind,
                                         unsigned char PCAdj);
  static ARMConstantPoolConstant *Create(const Constant *C, unsigned ID,
                                         ARMCP::ARMCPKind Kind,
                                         unsigned char PCAdj,
                                         ARMCP::ARMCPModifier Modifier,
                                         bool AddCurrentAddress);

  const GlobalValue *getGV() const;
  const BlockAddress *getBlockAddress() const;

  using promoted_iterator = SmallPtrSet<const GlobalVariable *, 1>::iterator;

  iterator_range<promoted_iterator> promotedGlobals() {
    return iterator_range<promoted_iterator>(GVars.begin(), GVars.end());
  }

  const Constant *getPromotedGlobalInit() const {
    return CVal;
  }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;

  /// hasSameValue - Return true if this ARM constpool value can share the same
  /// constantpool entry as another ARM constpool value.
  bool hasSameValue(ARMConstantPoolValue *ACPV) override;

  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;

  void print(raw_ostream &O) const override;

  static bool classof(const ARMConstantPoolValue *APV) {
    return APV->isGlobalValue() || APV->isBlockAddress() || APV->isLSDA() ||
           APV->isPromotedGlobal();
  }

  bool equals(const ARMConstantPoolConstant *A) const {
    return CVal == A->CVal && ARMConstantPoolValue::equals(A);
  }
};

/// ARMConstantPoolSymbol - ARM-specific constantpool values for external
/// symbols.
class ARMConstantPoolSymbol : public ARMConstantPoolValue {
  const std::string S;          // ExtSymbol being loaded.

  ARMConstantPoolSymbol(LLVMContext &C, StringRef s, unsigned id,
                        unsigned char PCAdj, ARMCP::ARMCPModifier Modifier,
                        bool AddCurrentAddress);

public:
  static ARMConstantPoolSymbol *Create(LLVMContext &C, StringRef s, unsigned ID,
                                       unsigned char PCAdj);

  StringRef getSymbol() const { return S; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;

  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;

  /// hasSameValue - Return true if this ARM constpool value can share the same
  /// constantpool entry as another ARM constpool value.
  bool hasSameValue(ARMConstantPoolValue *ACPV) override;

  void print(raw_ostream &O) const override;

  static bool classof(const ARMConstantPoolValue *ACPV) {
    return ACPV->isExtSymbol();
  }

  bool equals(const ARMConstantPoolSymbol *A) const {
    return S == A->S && ARMConstantPoolValue::equals(A);
  }
};

/// ARMConstantPoolMBB - ARM-specific constantpool value of a machine basic
/// block.
class ARMConstantPoolMBB : public ARMConstantPoolValue {
  const MachineBasicBlock *MBB; // Machine basic block.

  ARMConstantPoolMBB(LLVMContext &C, const MachineBasicBlock *mbb, unsigned id,
                     unsigned char PCAdj, ARMCP::ARMCPModifier Modifier,
                     bool AddCurrentAddress);

public:
  static ARMConstantPoolMBB *Create(LLVMContext &C,
                                    const MachineBasicBlock *mbb,
                                    unsigned ID, unsigned char PCAdj);

  const MachineBasicBlock *getMBB() const { return MBB; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;

  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;

  /// hasSameValue - Return true if this ARM constpool value can share the same
  /// constantpool entry as another ARM constpool value.
  bool hasSameValue(ARMConstantPoolValue *ACPV) override;

  void print(raw_ostream &O) const override;

  static bool classof(const ARMConstantPoolValue *ACPV) {
    return ACPV->isMachineBasicBlock();
  }

  bool equals(const ARMConstantPoolMBB *A) const {
    return MBB == A->MBB && ARMConstantPoolValue::equals(A);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMCONSTANTPOOLVALUE_H
