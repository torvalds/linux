//===- ARMConstantPoolValue.cpp - ARM constantpool value ------------------===//
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

#include "ARMConstantPoolValue.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// ARMConstantPoolValue
//===----------------------------------------------------------------------===//

ARMConstantPoolValue::ARMConstantPoolValue(Type *Ty, unsigned id,
                                           ARMCP::ARMCPKind kind,
                                           unsigned char PCAdj,
                                           ARMCP::ARMCPModifier modifier,
                                           bool addCurrentAddress)
  : MachineConstantPoolValue(Ty), LabelId(id), Kind(kind),
    PCAdjust(PCAdj), Modifier(modifier),
    AddCurrentAddress(addCurrentAddress) {}

ARMConstantPoolValue::ARMConstantPoolValue(LLVMContext &C, unsigned id,
                                           ARMCP::ARMCPKind kind,
                                           unsigned char PCAdj,
                                           ARMCP::ARMCPModifier modifier,
                                           bool addCurrentAddress)
  : MachineConstantPoolValue((Type*)Type::getInt32Ty(C)),
    LabelId(id), Kind(kind), PCAdjust(PCAdj), Modifier(modifier),
    AddCurrentAddress(addCurrentAddress) {}

ARMConstantPoolValue::~ARMConstantPoolValue() = default;

StringRef ARMConstantPoolValue::getModifierText() const {
  switch (Modifier) {
    // FIXME: Are these case sensitive? It'd be nice to lower-case all the
    // strings if that's legal.
  case ARMCP::no_modifier:
    return "none";
  case ARMCP::TLSGD:
    return "tlsgd";
  case ARMCP::GOT_PREL:
    return "GOT_PREL";
  case ARMCP::GOTTPOFF:
    return "gottpoff";
  case ARMCP::TPOFF:
    return "tpoff";
  case ARMCP::SBREL:
    return "SBREL";
  case ARMCP::SECREL:
    return "secrel32";
  }
  llvm_unreachable("Unknown modifier!");
}

int ARMConstantPoolValue::getExistingMachineCPValue(MachineConstantPool *CP,
                                                    Align Alignment) {
  llvm_unreachable("Shouldn't be calling this directly!");
}

void
ARMConstantPoolValue::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddInteger(LabelId);
  ID.AddInteger(PCAdjust);
}

bool
ARMConstantPoolValue::hasSameValue(ARMConstantPoolValue *ACPV) {
  if (ACPV->Kind == Kind &&
      ACPV->PCAdjust == PCAdjust &&
      ACPV->Modifier == Modifier &&
      ACPV->LabelId == LabelId &&
      ACPV->AddCurrentAddress == AddCurrentAddress) {
    // Two PC relative constpool entries containing the same GV address or
    // external symbols. FIXME: What about blockaddress?
    if (Kind == ARMCP::CPValue || Kind == ARMCP::CPExtSymbol)
      return true;
  }
  return false;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ARMConstantPoolValue::dump() const {
  errs() << "  " << *this;
}
#endif

void ARMConstantPoolValue::print(raw_ostream &O) const {
  if (Modifier) O << "(" << getModifierText() << ")";
  if (PCAdjust != 0) {
    O << "-(LPC" << LabelId << "+" << (unsigned)PCAdjust;
    if (AddCurrentAddress) O << "-.";
    O << ")";
  }
}

//===----------------------------------------------------------------------===//
// ARMConstantPoolConstant
//===----------------------------------------------------------------------===//

ARMConstantPoolConstant::ARMConstantPoolConstant(Type *Ty,
                                                 const Constant *C,
                                                 unsigned ID,
                                                 ARMCP::ARMCPKind Kind,
                                                 unsigned char PCAdj,
                                                 ARMCP::ARMCPModifier Modifier,
                                                 bool AddCurrentAddress)
  : ARMConstantPoolValue(Ty, ID, Kind, PCAdj, Modifier, AddCurrentAddress),
    CVal(C) {}

ARMConstantPoolConstant::ARMConstantPoolConstant(const Constant *C,
                                                 unsigned ID,
                                                 ARMCP::ARMCPKind Kind,
                                                 unsigned char PCAdj,
                                                 ARMCP::ARMCPModifier Modifier,
                                                 bool AddCurrentAddress)
  : ARMConstantPoolValue((Type*)C->getType(), ID, Kind, PCAdj, Modifier,
                         AddCurrentAddress),
    CVal(C) {}

ARMConstantPoolConstant::ARMConstantPoolConstant(const GlobalVariable *GV,
                                                 const Constant *C)
    : ARMConstantPoolValue((Type *)C->getType(), 0, ARMCP::CPPromotedGlobal, 0,
                           ARMCP::no_modifier, false), CVal(C) {
  GVars.insert(GV);
}

ARMConstantPoolConstant *
ARMConstantPoolConstant::Create(const Constant *C, unsigned ID) {
  return new ARMConstantPoolConstant(C, ID, ARMCP::CPValue, 0,
                                     ARMCP::no_modifier, false);
}

ARMConstantPoolConstant *
ARMConstantPoolConstant::Create(const GlobalVariable *GVar,
                                const Constant *Initializer) {
  return new ARMConstantPoolConstant(GVar, Initializer);
}

ARMConstantPoolConstant *
ARMConstantPoolConstant::Create(const GlobalValue *GV,
                                ARMCP::ARMCPModifier Modifier) {
  return new ARMConstantPoolConstant((Type*)Type::getInt32Ty(GV->getContext()),
                                     GV, 0, ARMCP::CPValue, 0,
                                     Modifier, false);
}

ARMConstantPoolConstant *
ARMConstantPoolConstant::Create(const Constant *C, unsigned ID,
                                ARMCP::ARMCPKind Kind, unsigned char PCAdj) {
  return new ARMConstantPoolConstant(C, ID, Kind, PCAdj,
                                     ARMCP::no_modifier, false);
}

ARMConstantPoolConstant *
ARMConstantPoolConstant::Create(const Constant *C, unsigned ID,
                                ARMCP::ARMCPKind Kind, unsigned char PCAdj,
                                ARMCP::ARMCPModifier Modifier,
                                bool AddCurrentAddress) {
  return new ARMConstantPoolConstant(C, ID, Kind, PCAdj, Modifier,
                                     AddCurrentAddress);
}

const GlobalValue *ARMConstantPoolConstant::getGV() const {
  return dyn_cast_or_null<GlobalValue>(CVal);
}

const BlockAddress *ARMConstantPoolConstant::getBlockAddress() const {
  return dyn_cast_or_null<BlockAddress>(CVal);
}

int ARMConstantPoolConstant::getExistingMachineCPValue(MachineConstantPool *CP,
                                                       Align Alignment) {
  int index =
    getExistingMachineCPValueImpl<ARMConstantPoolConstant>(CP, Alignment);
  if (index != -1) {
    auto *CPV = static_cast<ARMConstantPoolValue*>(
        CP->getConstants()[index].Val.MachineCPVal);
    auto *Constant = cast<ARMConstantPoolConstant>(CPV);
    Constant->GVars.insert(GVars.begin(), GVars.end());
  }
  return index;
}

bool ARMConstantPoolConstant::hasSameValue(ARMConstantPoolValue *ACPV) {
  const ARMConstantPoolConstant *ACPC = dyn_cast<ARMConstantPoolConstant>(ACPV);
  return ACPC && ACPC->CVal == CVal && ARMConstantPoolValue::hasSameValue(ACPV);
}

void ARMConstantPoolConstant::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddPointer(CVal);
  for (const auto *GV : GVars)
    ID.AddPointer(GV);
  ARMConstantPoolValue::addSelectionDAGCSEId(ID);
}

void ARMConstantPoolConstant::print(raw_ostream &O) const {
  O << CVal->getName();
  ARMConstantPoolValue::print(O);
}

//===----------------------------------------------------------------------===//
// ARMConstantPoolSymbol
//===----------------------------------------------------------------------===//

ARMConstantPoolSymbol::ARMConstantPoolSymbol(LLVMContext &C, StringRef s,
                                             unsigned id, unsigned char PCAdj,
                                             ARMCP::ARMCPModifier Modifier,
                                             bool AddCurrentAddress)
    : ARMConstantPoolValue(C, id, ARMCP::CPExtSymbol, PCAdj, Modifier,
                           AddCurrentAddress),
      S(std::string(s)) {}

ARMConstantPoolSymbol *ARMConstantPoolSymbol::Create(LLVMContext &C,
                                                     StringRef s, unsigned ID,
                                                     unsigned char PCAdj) {
  return new ARMConstantPoolSymbol(C, s, ID, PCAdj, ARMCP::no_modifier, false);
}

int ARMConstantPoolSymbol::getExistingMachineCPValue(MachineConstantPool *CP,
                                                     Align Alignment) {
  return getExistingMachineCPValueImpl<ARMConstantPoolSymbol>(CP, Alignment);
}

bool ARMConstantPoolSymbol::hasSameValue(ARMConstantPoolValue *ACPV) {
  const ARMConstantPoolSymbol *ACPS = dyn_cast<ARMConstantPoolSymbol>(ACPV);
  return ACPS && ACPS->S == S && ARMConstantPoolValue::hasSameValue(ACPV);
}

void ARMConstantPoolSymbol::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddString(S);
  ARMConstantPoolValue::addSelectionDAGCSEId(ID);
}

void ARMConstantPoolSymbol::print(raw_ostream &O) const {
  O << S;
  ARMConstantPoolValue::print(O);
}

//===----------------------------------------------------------------------===//
// ARMConstantPoolMBB
//===----------------------------------------------------------------------===//

ARMConstantPoolMBB::ARMConstantPoolMBB(LLVMContext &C,
                                       const MachineBasicBlock *mbb,
                                       unsigned id, unsigned char PCAdj,
                                       ARMCP::ARMCPModifier Modifier,
                                       bool AddCurrentAddress)
  : ARMConstantPoolValue(C, id, ARMCP::CPMachineBasicBlock, PCAdj,
                         Modifier, AddCurrentAddress),
    MBB(mbb) {}

ARMConstantPoolMBB *ARMConstantPoolMBB::Create(LLVMContext &C,
                                               const MachineBasicBlock *mbb,
                                               unsigned ID,
                                               unsigned char PCAdj) {
  return new ARMConstantPoolMBB(C, mbb, ID, PCAdj, ARMCP::no_modifier, false);
}

int ARMConstantPoolMBB::getExistingMachineCPValue(MachineConstantPool *CP,
                                                  Align Alignment) {
  return getExistingMachineCPValueImpl<ARMConstantPoolMBB>(CP, Alignment);
}

bool ARMConstantPoolMBB::hasSameValue(ARMConstantPoolValue *ACPV) {
  const ARMConstantPoolMBB *ACPMBB = dyn_cast<ARMConstantPoolMBB>(ACPV);
  return ACPMBB && ACPMBB->MBB == MBB &&
    ARMConstantPoolValue::hasSameValue(ACPV);
}

void ARMConstantPoolMBB::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddPointer(MBB);
  ARMConstantPoolValue::addSelectionDAGCSEId(ID);
}

void ARMConstantPoolMBB::print(raw_ostream &O) const {
  O << printMBBReference(*MBB);
  ARMConstantPoolValue::print(O);
}
