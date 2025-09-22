//===-- CSKYConstantPoolValue.h - CSKY constantpool value -----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CSKY specific constantpool value class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_CSKY_CONSTANTPOOLVALUE_H
#define LLVM_TARGET_CSKY_CONSTANTPOOLVALUE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>

namespace llvm {

class BlockAddress;
class Constant;
class GlobalValue;
class LLVMContext;
class MachineBasicBlock;

namespace CSKYCP {
enum CSKYCPKind {
  CPValue,
  CPExtSymbol,
  CPBlockAddress,
  CPMachineBasicBlock,
  CPJT,
  CPConstPool
};

enum CSKYCPModifier { NO_MOD, ADDR, GOT, GOTOFF, PLT, TLSLE, TLSIE, TLSGD };
} // namespace CSKYCP

/// CSKYConstantPoolValue - CSKY specific constantpool value. This is used to
/// represent PC-relative displacement between the address of the load
/// instruction and the constant being loaded, i.e. (&GV-(LPIC+8)).
class CSKYConstantPoolValue : public MachineConstantPoolValue {
protected:
  CSKYCP::CSKYCPKind Kind; // Kind of constant.
  unsigned PCAdjust;       // Extra adjustment if constantpool is pc-relative.
  CSKYCP::CSKYCPModifier Modifier; // GV modifier
  bool AddCurrentAddress;

  unsigned LabelId = 0;

  CSKYConstantPoolValue(Type *Ty, CSKYCP::CSKYCPKind Kind, unsigned PCAdjust,
                        CSKYCP::CSKYCPModifier Modifier, bool AddCurrentAddress,
                        unsigned ID = 0);

public:
  const char *getModifierText() const;
  unsigned getPCAdjustment() const { return PCAdjust; }
  bool mustAddCurrentAddress() const { return AddCurrentAddress; }
  CSKYCP::CSKYCPModifier getModifier() const { return Modifier; }
  unsigned getLabelID() const { return LabelId; }

  bool isGlobalValue() const { return Kind == CSKYCP::CPValue; }
  bool isExtSymbol() const { return Kind == CSKYCP::CPExtSymbol; }
  bool isBlockAddress() const { return Kind == CSKYCP::CPBlockAddress; }
  bool isMachineBasicBlock() const {
    return Kind == CSKYCP::CPMachineBasicBlock;
  }
  bool isJT() const { return Kind == CSKYCP::CPJT; }
  bool isConstPool() const { return Kind == CSKYCP::CPConstPool; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;

  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;

  void print(raw_ostream &O) const override;

  bool equals(const CSKYConstantPoolValue *A) const {
    return this->LabelId == A->LabelId && this->PCAdjust == A->PCAdjust &&
           this->Modifier == A->Modifier;
  }

  template <typename Derived>
  int getExistingMachineCPValueImpl(MachineConstantPool *CP, Align Alignment) {
    const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
    for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
      if (Constants[i].isMachineConstantPoolEntry() &&
          Constants[i].getAlign() >= Alignment) {
        auto *CPV =
            static_cast<CSKYConstantPoolValue *>(Constants[i].Val.MachineCPVal);
        if (Derived *APC = dyn_cast<Derived>(CPV))
          if (cast<Derived>(this)->equals(APC))
            return i;
      }
    }

    return -1;
  }
};

/// CSKY-specific constant pool values for Constants,
/// Functions, and BlockAddresses.
class CSKYConstantPoolConstant : public CSKYConstantPoolValue {
  const Constant *CVal; // Constant being loaded.

  CSKYConstantPoolConstant(const Constant *C, Type *Ty, CSKYCP::CSKYCPKind Kind,
                           unsigned PCAdjust, CSKYCP::CSKYCPModifier Modifier,
                           bool AddCurrentAddress, unsigned ID);

public:
  static CSKYConstantPoolConstant *
  Create(const Constant *C, CSKYCP::CSKYCPKind Kind, unsigned PCAdjust,
         CSKYCP::CSKYCPModifier Modifier, bool AddCurrentAddress,
         unsigned ID = 0);
  static CSKYConstantPoolConstant *
  Create(const Constant *C, Type *Ty, CSKYCP::CSKYCPKind Kind,
         unsigned PCAdjust, CSKYCP::CSKYCPModifier Modifier,
         bool AddCurrentAddress, unsigned ID = 0);
  const GlobalValue *getGV() const;
  const BlockAddress *getBlockAddress() const;
  const Constant *getConstantPool() const;

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  bool equals(const CSKYConstantPoolConstant *A) const {
    return CVal == A->CVal && CSKYConstantPoolValue::equals(A);
  }

  static bool classof(const CSKYConstantPoolValue *APV) {
    return APV->isGlobalValue() || APV->isBlockAddress() || APV->isConstPool();
  }
};

/// CSKYConstantPoolSymbol - CSKY-specific constantpool values for external
/// symbols.
class CSKYConstantPoolSymbol : public CSKYConstantPoolValue {
  const std::string S; // ExtSymbol being loaded.

  CSKYConstantPoolSymbol(Type *Ty, const char *S, unsigned PCAdjust,
                         CSKYCP::CSKYCPModifier Modifier,
                         bool AddCurrentAddress);

public:
  static CSKYConstantPoolSymbol *Create(Type *Ty, const char *S,
                                        unsigned PCAdjust,
                                        CSKYCP::CSKYCPModifier Modifier);

  StringRef getSymbol() const { return S; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  bool equals(const CSKYConstantPoolSymbol *A) const {
    return S == A->S && CSKYConstantPoolValue::equals(A);
  }

  static bool classof(const CSKYConstantPoolValue *ACPV) {
    return ACPV->isExtSymbol();
  }
};

/// CSKYConstantPoolMBB - CSKY-specific constantpool value of a machine basic
/// block.
class CSKYConstantPoolMBB : public CSKYConstantPoolValue {
  const MachineBasicBlock *MBB; // Machine basic block.

  CSKYConstantPoolMBB(Type *Ty, const MachineBasicBlock *Mbb, unsigned PCAdjust,
                      CSKYCP::CSKYCPModifier Modifier, bool AddCurrentAddress);

public:
  static CSKYConstantPoolMBB *Create(Type *Ty, const MachineBasicBlock *Mbb,
                                     unsigned PCAdjust);

  const MachineBasicBlock *getMBB() const { return MBB; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  bool equals(const CSKYConstantPoolMBB *A) const {
    return MBB == A->MBB && CSKYConstantPoolValue::equals(A);
  }

  static bool classof(const CSKYConstantPoolValue *ACPV) {
    return ACPV->isMachineBasicBlock();
  }
};

/// CSKY-specific constantpool value of a jump table.
class CSKYConstantPoolJT : public CSKYConstantPoolValue {
  signed JTI; // Machine basic block.

  CSKYConstantPoolJT(Type *Ty, int JTIndex, unsigned PCAdj,
                     CSKYCP::CSKYCPModifier Modifier, bool AddCurrentAddress);

public:
  static CSKYConstantPoolJT *Create(Type *Ty, int JTI, unsigned PCAdj,
                                    CSKYCP::CSKYCPModifier Modifier);

  signed getJTI() { return JTI; }

  int getExistingMachineCPValue(MachineConstantPool *CP,
                                Align Alignment) override;
  void addSelectionDAGCSEId(FoldingSetNodeID &ID) override;
  void print(raw_ostream &O) const override;

  bool equals(const CSKYConstantPoolJT *A) const {
    return JTI == A->JTI && CSKYConstantPoolValue::equals(A);
  }

  static bool classof(const CSKYConstantPoolValue *ACPV) {
    return ACPV->isJT();
  }
};

} // namespace llvm

#endif
