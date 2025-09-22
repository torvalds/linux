//===-- llvm/CodeGen/GlobalISel/CSEMIRBuilder.cpp - MIBuilder--*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the CSEMIRBuilder class which CSEs as it builds
/// instructions.
//===----------------------------------------------------------------------===//
//

#include "llvm/CodeGen/GlobalISel/CSEMIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"

using namespace llvm;

bool CSEMIRBuilder::dominates(MachineBasicBlock::const_iterator A,
                              MachineBasicBlock::const_iterator B) const {
  auto MBBEnd = getMBB().end();
  if (B == MBBEnd)
    return true;
  assert(A->getParent() == B->getParent() &&
         "Iterators should be in same block");
  const MachineBasicBlock *BBA = A->getParent();
  MachineBasicBlock::const_iterator I = BBA->begin();
  for (; &*I != A && &*I != B; ++I)
    ;
  return &*I == A;
}

MachineInstrBuilder
CSEMIRBuilder::getDominatingInstrForID(FoldingSetNodeID &ID,
                                       void *&NodeInsertPos) {
  GISelCSEInfo *CSEInfo = getCSEInfo();
  assert(CSEInfo && "Can't get here without setting CSEInfo");
  MachineBasicBlock *CurMBB = &getMBB();
  MachineInstr *MI =
      CSEInfo->getMachineInstrIfExists(ID, CurMBB, NodeInsertPos);
  if (MI) {
    CSEInfo->countOpcodeHit(MI->getOpcode());
    auto CurrPos = getInsertPt();
    auto MII = MachineBasicBlock::iterator(MI);
    if (MII == CurrPos) {
      // Move the insert point ahead of the instruction so any future uses of
      // this builder will have the def ready.
      setInsertPt(*CurMBB, std::next(MII));
    } else if (!dominates(MI, CurrPos)) {
      // Update the spliced machineinstr's debug location by merging it with the
      // debug location of the instruction at the insertion point.
      auto *Loc = DILocation::getMergedLocation(getDebugLoc().get(),
                                                MI->getDebugLoc().get());
      MI->setDebugLoc(Loc);
      CurMBB->splice(CurrPos, CurMBB, MI);
    }
    return MachineInstrBuilder(getMF(), MI);
  }
  return MachineInstrBuilder();
}

bool CSEMIRBuilder::canPerformCSEForOpc(unsigned Opc) const {
  const GISelCSEInfo *CSEInfo = getCSEInfo();
  if (!CSEInfo || !CSEInfo->shouldCSE(Opc))
    return false;
  return true;
}

void CSEMIRBuilder::profileDstOp(const DstOp &Op,
                                 GISelInstProfileBuilder &B) const {
  switch (Op.getDstOpKind()) {
  case DstOp::DstType::Ty_RC:
    B.addNodeIDRegType(Op.getRegClass());
    break;
  case DstOp::DstType::Ty_Reg: {
    // Regs can have LLT&(RB|RC). If those exist, profile them as well.
    B.addNodeIDReg(Op.getReg());
    break;
  }
  default:
    B.addNodeIDRegType(Op.getLLTTy(*getMRI()));
    break;
  }
}

void CSEMIRBuilder::profileSrcOp(const SrcOp &Op,
                                 GISelInstProfileBuilder &B) const {
  switch (Op.getSrcOpKind()) {
  case SrcOp::SrcType::Ty_Imm:
    B.addNodeIDImmediate(static_cast<int64_t>(Op.getImm()));
    break;
  case SrcOp::SrcType::Ty_Predicate:
    B.addNodeIDImmediate(static_cast<int64_t>(Op.getPredicate()));
    break;
  default:
    B.addNodeIDRegType(Op.getReg());
    break;
  }
}

void CSEMIRBuilder::profileMBBOpcode(GISelInstProfileBuilder &B,
                                     unsigned Opc) const {
  // First add the MBB (Local CSE).
  B.addNodeIDMBB(&getMBB());
  // Then add the opcode.
  B.addNodeIDOpcode(Opc);
}

void CSEMIRBuilder::profileEverything(unsigned Opc, ArrayRef<DstOp> DstOps,
                                      ArrayRef<SrcOp> SrcOps,
                                      std::optional<unsigned> Flags,
                                      GISelInstProfileBuilder &B) const {

  profileMBBOpcode(B, Opc);
  // Then add the DstOps.
  profileDstOps(DstOps, B);
  // Then add the SrcOps.
  profileSrcOps(SrcOps, B);
  // Add Flags if passed in.
  if (Flags)
    B.addNodeIDFlag(*Flags);
}

MachineInstrBuilder CSEMIRBuilder::memoizeMI(MachineInstrBuilder MIB,
                                             void *NodeInsertPos) {
  assert(canPerformCSEForOpc(MIB->getOpcode()) &&
         "Attempting to CSE illegal op");
  MachineInstr *MIBInstr = MIB;
  getCSEInfo()->insertInstr(MIBInstr, NodeInsertPos);
  return MIB;
}

bool CSEMIRBuilder::checkCopyToDefsPossible(ArrayRef<DstOp> DstOps) {
  if (DstOps.size() == 1)
    return true; // always possible to emit copy to just 1 vreg.

  return llvm::all_of(DstOps, [](const DstOp &Op) {
    DstOp::DstType DT = Op.getDstOpKind();
    return DT == DstOp::DstType::Ty_LLT || DT == DstOp::DstType::Ty_RC;
  });
}

MachineInstrBuilder
CSEMIRBuilder::generateCopiesIfRequired(ArrayRef<DstOp> DstOps,
                                        MachineInstrBuilder &MIB) {
  assert(checkCopyToDefsPossible(DstOps) &&
         "Impossible return a single MIB with copies to multiple defs");
  if (DstOps.size() == 1) {
    const DstOp &Op = DstOps[0];
    if (Op.getDstOpKind() == DstOp::DstType::Ty_Reg)
      return buildCopy(Op.getReg(), MIB.getReg(0));
  }

  // If we didn't generate a copy then we're re-using an existing node directly
  // instead of emitting any code. Merge the debug location we wanted to emit
  // into the instruction we're CSE'ing with. Debug locations arent part of the
  // profile so we don't need to recompute it.
  if (getDebugLoc()) {
    GISelChangeObserver *Observer = getState().Observer;
    if (Observer)
      Observer->changingInstr(*MIB);
    MIB->setDebugLoc(
        DILocation::getMergedLocation(MIB->getDebugLoc(), getDebugLoc()));
    if (Observer)
      Observer->changedInstr(*MIB);
  }

  return MIB;
}

MachineInstrBuilder CSEMIRBuilder::buildInstr(unsigned Opc,
                                              ArrayRef<DstOp> DstOps,
                                              ArrayRef<SrcOp> SrcOps,
                                              std::optional<unsigned> Flag) {
  switch (Opc) {
  default:
    break;
  case TargetOpcode::G_ICMP: {
    assert(SrcOps.size() == 3 && "Invalid sources");
    assert(DstOps.size() == 1 && "Invalid dsts");
    LLT SrcTy = SrcOps[1].getLLTTy(*getMRI());

    if (std::optional<SmallVector<APInt>> Cst =
            ConstantFoldICmp(SrcOps[0].getPredicate(), SrcOps[1].getReg(),
                             SrcOps[2].getReg(), *getMRI())) {
      if (SrcTy.isVector())
        return buildBuildVectorConstant(DstOps[0], *Cst);
      return buildConstant(DstOps[0], Cst->front());
    }
    break;
  }
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_PTR_ADD:
  case TargetOpcode::G_AND:
  case TargetOpcode::G_ASHR:
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_MUL:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_SHL:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_XOR:
  case TargetOpcode::G_UDIV:
  case TargetOpcode::G_SDIV:
  case TargetOpcode::G_UREM:
  case TargetOpcode::G_SREM:
  case TargetOpcode::G_SMIN:
  case TargetOpcode::G_SMAX:
  case TargetOpcode::G_UMIN:
  case TargetOpcode::G_UMAX: {
    // Try to constant fold these.
    assert(SrcOps.size() == 2 && "Invalid sources");
    assert(DstOps.size() == 1 && "Invalid dsts");
    LLT SrcTy = SrcOps[0].getLLTTy(*getMRI());

    if (Opc == TargetOpcode::G_PTR_ADD &&
        getDataLayout().isNonIntegralAddressSpace(SrcTy.getAddressSpace()))
      break;

    if (SrcTy.isVector()) {
      // Try to constant fold vector constants.
      SmallVector<APInt> VecCst = ConstantFoldVectorBinop(
          Opc, SrcOps[0].getReg(), SrcOps[1].getReg(), *getMRI());
      if (!VecCst.empty())
        return buildBuildVectorConstant(DstOps[0], VecCst);
      break;
    }

    if (std::optional<APInt> Cst = ConstantFoldBinOp(
            Opc, SrcOps[0].getReg(), SrcOps[1].getReg(), *getMRI()))
      return buildConstant(DstOps[0], *Cst);
    break;
  }
  case TargetOpcode::G_FADD:
  case TargetOpcode::G_FSUB:
  case TargetOpcode::G_FMUL:
  case TargetOpcode::G_FDIV:
  case TargetOpcode::G_FREM:
  case TargetOpcode::G_FMINNUM:
  case TargetOpcode::G_FMAXNUM:
  case TargetOpcode::G_FMINNUM_IEEE:
  case TargetOpcode::G_FMAXNUM_IEEE:
  case TargetOpcode::G_FMINIMUM:
  case TargetOpcode::G_FMAXIMUM:
  case TargetOpcode::G_FCOPYSIGN: {
    // Try to constant fold these.
    assert(SrcOps.size() == 2 && "Invalid sources");
    assert(DstOps.size() == 1 && "Invalid dsts");
    if (std::optional<APFloat> Cst = ConstantFoldFPBinOp(
            Opc, SrcOps[0].getReg(), SrcOps[1].getReg(), *getMRI()))
      return buildFConstant(DstOps[0], *Cst);
    break;
  }
  case TargetOpcode::G_SEXT_INREG: {
    assert(DstOps.size() == 1 && "Invalid dst ops");
    assert(SrcOps.size() == 2 && "Invalid src ops");
    const DstOp &Dst = DstOps[0];
    const SrcOp &Src0 = SrcOps[0];
    const SrcOp &Src1 = SrcOps[1];
    if (auto MaybeCst =
            ConstantFoldExtOp(Opc, Src0.getReg(), Src1.getImm(), *getMRI()))
      return buildConstant(Dst, *MaybeCst);
    break;
  }
  case TargetOpcode::G_SITOFP:
  case TargetOpcode::G_UITOFP: {
    // Try to constant fold these.
    assert(SrcOps.size() == 1 && "Invalid sources");
    assert(DstOps.size() == 1 && "Invalid dsts");
    if (std::optional<APFloat> Cst = ConstantFoldIntToFloat(
            Opc, DstOps[0].getLLTTy(*getMRI()), SrcOps[0].getReg(), *getMRI()))
      return buildFConstant(DstOps[0], *Cst);
    break;
  }
  case TargetOpcode::G_CTLZ:
  case TargetOpcode::G_CTTZ: {
    assert(SrcOps.size() == 1 && "Expected one source");
    assert(DstOps.size() == 1 && "Expected one dest");
    std::function<unsigned(APInt)> CB;
    if (Opc == TargetOpcode::G_CTLZ)
      CB = [](APInt V) -> unsigned { return V.countl_zero(); };
    else
      CB = [](APInt V) -> unsigned { return V.countTrailingZeros(); };
    auto MaybeCsts = ConstantFoldCountZeros(SrcOps[0].getReg(), *getMRI(), CB);
    if (!MaybeCsts)
      break;
    if (MaybeCsts->size() == 1)
      return buildConstant(DstOps[0], (*MaybeCsts)[0]);
    // This was a vector constant. Build a G_BUILD_VECTOR for them.
    SmallVector<Register> ConstantRegs;
    LLT VecTy = DstOps[0].getLLTTy(*getMRI());
    for (unsigned Cst : *MaybeCsts)
      ConstantRegs.emplace_back(
          buildConstant(VecTy.getScalarType(), Cst).getReg(0));
    return buildBuildVector(DstOps[0], ConstantRegs);
  }
  }
  bool CanCopy = checkCopyToDefsPossible(DstOps);
  if (!canPerformCSEForOpc(Opc))
    return MachineIRBuilder::buildInstr(Opc, DstOps, SrcOps, Flag);
  // If we can CSE this instruction, but involves generating copies to multiple
  // regs, give up. This frequently happens to UNMERGEs.
  if (!CanCopy) {
    auto MIB = MachineIRBuilder::buildInstr(Opc, DstOps, SrcOps, Flag);
    // CSEInfo would have tracked this instruction. Remove it from the temporary
    // insts.
    getCSEInfo()->handleRemoveInst(&*MIB);
    return MIB;
  }
  FoldingSetNodeID ID;
  GISelInstProfileBuilder ProfBuilder(ID, *getMRI());
  void *InsertPos = nullptr;
  profileEverything(Opc, DstOps, SrcOps, Flag, ProfBuilder);
  MachineInstrBuilder MIB = getDominatingInstrForID(ID, InsertPos);
  if (MIB) {
    // Handle generating copies here.
    return generateCopiesIfRequired(DstOps, MIB);
  }
  // This instruction does not exist in the CSEInfo. Build it and CSE it.
  MachineInstrBuilder NewMIB =
      MachineIRBuilder::buildInstr(Opc, DstOps, SrcOps, Flag);
  return memoizeMI(NewMIB, InsertPos);
}

MachineInstrBuilder CSEMIRBuilder::buildConstant(const DstOp &Res,
                                                 const ConstantInt &Val) {
  constexpr unsigned Opc = TargetOpcode::G_CONSTANT;
  if (!canPerformCSEForOpc(Opc))
    return MachineIRBuilder::buildConstant(Res, Val);

  // For vectors, CSE the element only for now.
  LLT Ty = Res.getLLTTy(*getMRI());
  if (Ty.isVector())
    return buildSplatBuildVector(Res, buildConstant(Ty.getElementType(), Val));

  FoldingSetNodeID ID;
  GISelInstProfileBuilder ProfBuilder(ID, *getMRI());
  void *InsertPos = nullptr;
  profileMBBOpcode(ProfBuilder, Opc);
  profileDstOp(Res, ProfBuilder);
  ProfBuilder.addNodeIDMachineOperand(MachineOperand::CreateCImm(&Val));
  MachineInstrBuilder MIB = getDominatingInstrForID(ID, InsertPos);
  if (MIB) {
    // Handle generating copies here.
    return generateCopiesIfRequired({Res}, MIB);
  }

  MachineInstrBuilder NewMIB = MachineIRBuilder::buildConstant(Res, Val);
  return memoizeMI(NewMIB, InsertPos);
}

MachineInstrBuilder CSEMIRBuilder::buildFConstant(const DstOp &Res,
                                                  const ConstantFP &Val) {
  constexpr unsigned Opc = TargetOpcode::G_FCONSTANT;
  if (!canPerformCSEForOpc(Opc))
    return MachineIRBuilder::buildFConstant(Res, Val);

  // For vectors, CSE the element only for now.
  LLT Ty = Res.getLLTTy(*getMRI());
  if (Ty.isVector())
    return buildSplatBuildVector(Res, buildFConstant(Ty.getElementType(), Val));

  FoldingSetNodeID ID;
  GISelInstProfileBuilder ProfBuilder(ID, *getMRI());
  void *InsertPos = nullptr;
  profileMBBOpcode(ProfBuilder, Opc);
  profileDstOp(Res, ProfBuilder);
  ProfBuilder.addNodeIDMachineOperand(MachineOperand::CreateFPImm(&Val));
  MachineInstrBuilder MIB = getDominatingInstrForID(ID, InsertPos);
  if (MIB) {
    // Handle generating copies here.
    return generateCopiesIfRequired({Res}, MIB);
  }
  MachineInstrBuilder NewMIB = MachineIRBuilder::buildFConstant(Res, Val);
  return memoizeMI(NewMIB, InsertPos);
}
