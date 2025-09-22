//===- FastISel.cpp - Implementation of the FastISel class ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the FastISel class.
//
// "Fast" instruction selection is designed to emit very poor code quickly.
// Also, it is not designed to be able to do much lowering, so most illegal
// types (e.g. i64 on 32-bit targets) and operations are not supported.  It is
// also not intended to be able to do much optimization, except in a few cases
// where doing optimizations reduces overall compile time.  For example, folding
// constants into immediate fields is often done, because it's cheap and it
// reduces the number of instructions later phases have to examine.
//
// "Fast" instruction selection is able to fail gracefully and transfer
// control to the SelectionDAG selector for operations that it doesn't
// support.  In many cases, this allows us to avoid duplicating a lot of
// the complicated lowering logic that SelectionDAG currently has.
//
// The intended use for "fast" instruction selection is "-O0" mode
// compilation, where the quality of the generated code is irrelevant when
// weighed against the speed at which the code can be generated.  Also,
// at -O0, the LLVM optimizers are not running, and this makes the
// compile time of codegen a much higher portion of the overall compile
// time.  Despite its limitations, "fast" instruction selection is able to
// handle enough code on its own to provide noticeable overall speedups
// in -O0 compiles.
//
// Basic operations are supported in a target-independent way, by reading
// the same instruction descriptions that the SelectionDAG selector reads,
// and identifying simple arithmetic operations that can be directly selected
// from simple operators.  More complicated operations currently require
// target-specific code.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/FastISel.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "isel"

STATISTIC(NumFastIselSuccessIndependent, "Number of insts selected by "
                                         "target-independent selector");
STATISTIC(NumFastIselSuccessTarget, "Number of insts selected by "
                                    "target-specific selector");
STATISTIC(NumFastIselDead, "Number of dead insts removed on failure");

/// Set the current block to which generated machine instructions will be
/// appended.
void FastISel::startNewBlock() {
  assert(LocalValueMap.empty() &&
         "local values should be cleared after finishing a BB");

  // Instructions are appended to FuncInfo.MBB. If the basic block already
  // contains labels or copies, use the last instruction as the last local
  // value.
  EmitStartPt = nullptr;
  if (!FuncInfo.MBB->empty())
    EmitStartPt = &FuncInfo.MBB->back();
  LastLocalValue = EmitStartPt;
}

void FastISel::finishBasicBlock() { flushLocalValueMap(); }

bool FastISel::lowerArguments() {
  if (!FuncInfo.CanLowerReturn)
    // Fallback to SDISel argument lowering code to deal with sret pointer
    // parameter.
    return false;

  if (!fastLowerArguments())
    return false;

  // Enter arguments into ValueMap for uses in non-entry BBs.
  for (Function::const_arg_iterator I = FuncInfo.Fn->arg_begin(),
                                    E = FuncInfo.Fn->arg_end();
       I != E; ++I) {
    DenseMap<const Value *, Register>::iterator VI = LocalValueMap.find(&*I);
    assert(VI != LocalValueMap.end() && "Missed an argument?");
    FuncInfo.ValueMap[&*I] = VI->second;
  }
  return true;
}

/// Return the defined register if this instruction defines exactly one
/// virtual register and uses no other virtual registers. Otherwise return 0.
static Register findLocalRegDef(MachineInstr &MI) {
  Register RegDef;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    if (MO.isDef()) {
      if (RegDef)
        return Register();
      RegDef = MO.getReg();
    } else if (MO.getReg().isVirtual()) {
      // This is another use of a vreg. Don't delete it.
      return Register();
    }
  }
  return RegDef;
}

static bool isRegUsedByPhiNodes(Register DefReg,
                                FunctionLoweringInfo &FuncInfo) {
  for (auto &P : FuncInfo.PHINodesToUpdate)
    if (P.second == DefReg)
      return true;
  return false;
}

void FastISel::flushLocalValueMap() {
  // If FastISel bails out, it could leave local value instructions behind
  // that aren't used for anything.  Detect and erase those.
  if (LastLocalValue != EmitStartPt) {
    // Save the first instruction after local values, for later.
    MachineBasicBlock::iterator FirstNonValue(LastLocalValue);
    ++FirstNonValue;

    MachineBasicBlock::reverse_iterator RE =
        EmitStartPt ? MachineBasicBlock::reverse_iterator(EmitStartPt)
                    : FuncInfo.MBB->rend();
    MachineBasicBlock::reverse_iterator RI(LastLocalValue);
    for (MachineInstr &LocalMI :
         llvm::make_early_inc_range(llvm::make_range(RI, RE))) {
      Register DefReg = findLocalRegDef(LocalMI);
      if (!DefReg)
        continue;
      if (FuncInfo.RegsWithFixups.count(DefReg))
        continue;
      bool UsedByPHI = isRegUsedByPhiNodes(DefReg, FuncInfo);
      if (!UsedByPHI && MRI.use_nodbg_empty(DefReg)) {
        if (EmitStartPt == &LocalMI)
          EmitStartPt = EmitStartPt->getPrevNode();
        LLVM_DEBUG(dbgs() << "removing dead local value materialization"
                          << LocalMI);
        LocalMI.eraseFromParent();
      }
    }

    if (FirstNonValue != FuncInfo.MBB->end()) {
      // See if there are any local value instructions left.  If so, we want to
      // make sure the first one has a debug location; if it doesn't, use the
      // first non-value instruction's debug location.

      // If EmitStartPt is non-null, this block had copies at the top before
      // FastISel started doing anything; it points to the last one, so the
      // first local value instruction is the one after EmitStartPt.
      // If EmitStartPt is null, the first local value instruction is at the
      // top of the block.
      MachineBasicBlock::iterator FirstLocalValue =
          EmitStartPt ? ++MachineBasicBlock::iterator(EmitStartPt)
                      : FuncInfo.MBB->begin();
      if (FirstLocalValue != FirstNonValue && !FirstLocalValue->getDebugLoc())
        FirstLocalValue->setDebugLoc(FirstNonValue->getDebugLoc());
    }
  }

  LocalValueMap.clear();
  LastLocalValue = EmitStartPt;
  recomputeInsertPt();
  SavedInsertPt = FuncInfo.InsertPt;
}

Register FastISel::getRegForValue(const Value *V) {
  EVT RealVT = TLI.getValueType(DL, V->getType(), /*AllowUnknown=*/true);
  // Don't handle non-simple values in FastISel.
  if (!RealVT.isSimple())
    return Register();

  // Ignore illegal types. We must do this before looking up the value
  // in ValueMap because Arguments are given virtual registers regardless
  // of whether FastISel can handle them.
  MVT VT = RealVT.getSimpleVT();
  if (!TLI.isTypeLegal(VT)) {
    // Handle integer promotions, though, because they're common and easy.
    if (VT == MVT::i1 || VT == MVT::i8 || VT == MVT::i16)
      VT = TLI.getTypeToTransformTo(V->getContext(), VT).getSimpleVT();
    else
      return Register();
  }

  // Look up the value to see if we already have a register for it.
  Register Reg = lookUpRegForValue(V);
  if (Reg)
    return Reg;

  // In bottom-up mode, just create the virtual register which will be used
  // to hold the value. It will be materialized later.
  if (isa<Instruction>(V) &&
      (!isa<AllocaInst>(V) ||
       !FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(V))))
    return FuncInfo.InitializeRegForValue(V);

  SavePoint SaveInsertPt = enterLocalValueArea();

  // Materialize the value in a register. Emit any instructions in the
  // local value area.
  Reg = materializeRegForValue(V, VT);

  leaveLocalValueArea(SaveInsertPt);

  return Reg;
}

Register FastISel::materializeConstant(const Value *V, MVT VT) {
  Register Reg;
  if (const auto *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getValue().getActiveBits() <= 64)
      Reg = fastEmit_i(VT, VT, ISD::Constant, CI->getZExtValue());
  } else if (isa<AllocaInst>(V))
    Reg = fastMaterializeAlloca(cast<AllocaInst>(V));
  else if (isa<ConstantPointerNull>(V))
    // Translate this as an integer zero so that it can be
    // local-CSE'd with actual integer zeros.
    Reg =
        getRegForValue(Constant::getNullValue(DL.getIntPtrType(V->getType())));
  else if (const auto *CF = dyn_cast<ConstantFP>(V)) {
    if (CF->isNullValue())
      Reg = fastMaterializeFloatZero(CF);
    else
      // Try to emit the constant directly.
      Reg = fastEmit_f(VT, VT, ISD::ConstantFP, CF);

    if (!Reg) {
      // Try to emit the constant by using an integer constant with a cast.
      const APFloat &Flt = CF->getValueAPF();
      EVT IntVT = TLI.getPointerTy(DL);
      uint32_t IntBitWidth = IntVT.getSizeInBits();
      APSInt SIntVal(IntBitWidth, /*isUnsigned=*/false);
      bool isExact;
      (void)Flt.convertToInteger(SIntVal, APFloat::rmTowardZero, &isExact);
      if (isExact) {
        Register IntegerReg =
            getRegForValue(ConstantInt::get(V->getContext(), SIntVal));
        if (IntegerReg)
          Reg = fastEmit_r(IntVT.getSimpleVT(), VT, ISD::SINT_TO_FP,
                           IntegerReg);
      }
    }
  } else if (const auto *Op = dyn_cast<Operator>(V)) {
    if (!selectOperator(Op, Op->getOpcode()))
      if (!isa<Instruction>(Op) ||
          !fastSelectInstruction(cast<Instruction>(Op)))
        return 0;
    Reg = lookUpRegForValue(Op);
  } else if (isa<UndefValue>(V)) {
    Reg = createResultReg(TLI.getRegClassFor(VT));
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
            TII.get(TargetOpcode::IMPLICIT_DEF), Reg);
  }
  return Reg;
}

/// Helper for getRegForValue. This function is called when the value isn't
/// already available in a register and must be materialized with new
/// instructions.
Register FastISel::materializeRegForValue(const Value *V, MVT VT) {
  Register Reg;
  // Give the target-specific code a try first.
  if (isa<Constant>(V))
    Reg = fastMaterializeConstant(cast<Constant>(V));

  // If target-specific code couldn't or didn't want to handle the value, then
  // give target-independent code a try.
  if (!Reg)
    Reg = materializeConstant(V, VT);

  // Don't cache constant materializations in the general ValueMap.
  // To do so would require tracking what uses they dominate.
  if (Reg) {
    LocalValueMap[V] = Reg;
    LastLocalValue = MRI.getVRegDef(Reg);
  }
  return Reg;
}

Register FastISel::lookUpRegForValue(const Value *V) {
  // Look up the value to see if we already have a register for it. We
  // cache values defined by Instructions across blocks, and other values
  // only locally. This is because Instructions already have the SSA
  // def-dominates-use requirement enforced.
  DenseMap<const Value *, Register>::iterator I = FuncInfo.ValueMap.find(V);
  if (I != FuncInfo.ValueMap.end())
    return I->second;
  return LocalValueMap[V];
}

void FastISel::updateValueMap(const Value *I, Register Reg, unsigned NumRegs) {
  if (!isa<Instruction>(I)) {
    LocalValueMap[I] = Reg;
    return;
  }

  Register &AssignedReg = FuncInfo.ValueMap[I];
  if (!AssignedReg)
    // Use the new register.
    AssignedReg = Reg;
  else if (Reg != AssignedReg) {
    // Arrange for uses of AssignedReg to be replaced by uses of Reg.
    for (unsigned i = 0; i < NumRegs; i++) {
      FuncInfo.RegFixups[AssignedReg + i] = Reg + i;
      FuncInfo.RegsWithFixups.insert(Reg + i);
    }

    AssignedReg = Reg;
  }
}

Register FastISel::getRegForGEPIndex(MVT PtrVT, const Value *Idx) {
  Register IdxN = getRegForValue(Idx);
  if (!IdxN)
    // Unhandled operand. Halt "fast" selection and bail.
    return Register();

  // If the index is smaller or larger than intptr_t, truncate or extend it.
  EVT IdxVT = EVT::getEVT(Idx->getType(), /*HandleUnknown=*/false);
  if (IdxVT.bitsLT(PtrVT)) {
    IdxN = fastEmit_r(IdxVT.getSimpleVT(), PtrVT, ISD::SIGN_EXTEND, IdxN);
  } else if (IdxVT.bitsGT(PtrVT)) {
    IdxN =
        fastEmit_r(IdxVT.getSimpleVT(), PtrVT, ISD::TRUNCATE, IdxN);
  }
  return IdxN;
}

Register FastISel::getRegForGEPIndex(const Value *Idx) {
  return getRegForGEPIndex(TLI.getPointerTy(DL), Idx);
}

void FastISel::recomputeInsertPt() {
  if (getLastLocalValue()) {
    FuncInfo.InsertPt = getLastLocalValue();
    FuncInfo.MBB = FuncInfo.InsertPt->getParent();
    ++FuncInfo.InsertPt;
  } else
    FuncInfo.InsertPt = FuncInfo.MBB->getFirstNonPHI();
}

void FastISel::removeDeadCode(MachineBasicBlock::iterator I,
                              MachineBasicBlock::iterator E) {
  assert(I.isValid() && E.isValid() && std::distance(I, E) > 0 &&
         "Invalid iterator!");
  while (I != E) {
    if (SavedInsertPt == I)
      SavedInsertPt = E;
    if (EmitStartPt == I)
      EmitStartPt = E.isValid() ? &*E : nullptr;
    if (LastLocalValue == I)
      LastLocalValue = E.isValid() ? &*E : nullptr;

    MachineInstr *Dead = &*I;
    ++I;
    Dead->eraseFromParent();
    ++NumFastIselDead;
  }
  recomputeInsertPt();
}

FastISel::SavePoint FastISel::enterLocalValueArea() {
  SavePoint OldInsertPt = FuncInfo.InsertPt;
  recomputeInsertPt();
  return OldInsertPt;
}

void FastISel::leaveLocalValueArea(SavePoint OldInsertPt) {
  if (FuncInfo.InsertPt != FuncInfo.MBB->begin())
    LastLocalValue = &*std::prev(FuncInfo.InsertPt);

  // Restore the previous insert position.
  FuncInfo.InsertPt = OldInsertPt;
}

bool FastISel::selectBinaryOp(const User *I, unsigned ISDOpcode) {
  EVT VT = EVT::getEVT(I->getType(), /*HandleUnknown=*/true);
  if (VT == MVT::Other || !VT.isSimple())
    // Unhandled type. Halt "fast" selection and bail.
    return false;

  // We only handle legal types. For example, on x86-32 the instruction
  // selector contains all of the 64-bit instructions from x86-64,
  // under the assumption that i64 won't be used if the target doesn't
  // support it.
  if (!TLI.isTypeLegal(VT)) {
    // MVT::i1 is special. Allow AND, OR, or XOR because they
    // don't require additional zeroing, which makes them easy.
    if (VT == MVT::i1 && ISD::isBitwiseLogicOp(ISDOpcode))
      VT = TLI.getTypeToTransformTo(I->getContext(), VT);
    else
      return false;
  }

  // Check if the first operand is a constant, and handle it as "ri".  At -O0,
  // we don't have anything that canonicalizes operand order.
  if (const auto *CI = dyn_cast<ConstantInt>(I->getOperand(0)))
    if (isa<Instruction>(I) && cast<Instruction>(I)->isCommutative()) {
      Register Op1 = getRegForValue(I->getOperand(1));
      if (!Op1)
        return false;

      Register ResultReg =
          fastEmit_ri_(VT.getSimpleVT(), ISDOpcode, Op1, CI->getZExtValue(),
                       VT.getSimpleVT());
      if (!ResultReg)
        return false;

      // We successfully emitted code for the given LLVM Instruction.
      updateValueMap(I, ResultReg);
      return true;
    }

  Register Op0 = getRegForValue(I->getOperand(0));
  if (!Op0) // Unhandled operand. Halt "fast" selection and bail.
    return false;

  // Check if the second operand is a constant and handle it appropriately.
  if (const auto *CI = dyn_cast<ConstantInt>(I->getOperand(1))) {
    uint64_t Imm = CI->getSExtValue();

    // Transform "sdiv exact X, 8" -> "sra X, 3".
    if (ISDOpcode == ISD::SDIV && isa<BinaryOperator>(I) &&
        cast<BinaryOperator>(I)->isExact() && isPowerOf2_64(Imm)) {
      Imm = Log2_64(Imm);
      ISDOpcode = ISD::SRA;
    }

    // Transform "urem x, pow2" -> "and x, pow2-1".
    if (ISDOpcode == ISD::UREM && isa<BinaryOperator>(I) &&
        isPowerOf2_64(Imm)) {
      --Imm;
      ISDOpcode = ISD::AND;
    }

    Register ResultReg = fastEmit_ri_(VT.getSimpleVT(), ISDOpcode, Op0, Imm,
                                      VT.getSimpleVT());
    if (!ResultReg)
      return false;

    // We successfully emitted code for the given LLVM Instruction.
    updateValueMap(I, ResultReg);
    return true;
  }

  Register Op1 = getRegForValue(I->getOperand(1));
  if (!Op1) // Unhandled operand. Halt "fast" selection and bail.
    return false;

  // Now we have both operands in registers. Emit the instruction.
  Register ResultReg = fastEmit_rr(VT.getSimpleVT(), VT.getSimpleVT(),
                                   ISDOpcode, Op0, Op1);
  if (!ResultReg)
    // Target-specific code wasn't able to find a machine opcode for
    // the given ISD opcode and type. Halt "fast" selection and bail.
    return false;

  // We successfully emitted code for the given LLVM Instruction.
  updateValueMap(I, ResultReg);
  return true;
}

bool FastISel::selectGetElementPtr(const User *I) {
  Register N = getRegForValue(I->getOperand(0));
  if (!N) // Unhandled operand. Halt "fast" selection and bail.
    return false;

  // FIXME: The code below does not handle vector GEPs. Halt "fast" selection
  // and bail.
  if (isa<VectorType>(I->getType()))
    return false;

  // Keep a running tab of the total offset to coalesce multiple N = N + Offset
  // into a single N = N + TotalOffset.
  uint64_t TotalOffs = 0;
  // FIXME: What's a good SWAG number for MaxOffs?
  uint64_t MaxOffs = 2048;
  MVT VT = TLI.getValueType(DL, I->getType()).getSimpleVT();

  for (gep_type_iterator GTI = gep_type_begin(I), E = gep_type_end(I);
       GTI != E; ++GTI) {
    const Value *Idx = GTI.getOperand();
    if (StructType *StTy = GTI.getStructTypeOrNull()) {
      uint64_t Field = cast<ConstantInt>(Idx)->getZExtValue();
      if (Field) {
        // N = N + Offset
        TotalOffs += DL.getStructLayout(StTy)->getElementOffset(Field);
        if (TotalOffs >= MaxOffs) {
          N = fastEmit_ri_(VT, ISD::ADD, N, TotalOffs, VT);
          if (!N) // Unhandled operand. Halt "fast" selection and bail.
            return false;
          TotalOffs = 0;
        }
      }
    } else {
      // If this is a constant subscript, handle it quickly.
      if (const auto *CI = dyn_cast<ConstantInt>(Idx)) {
        if (CI->isZero())
          continue;
        // N = N + Offset
        uint64_t IdxN = CI->getValue().sextOrTrunc(64).getSExtValue();
        TotalOffs += GTI.getSequentialElementStride(DL) * IdxN;
        if (TotalOffs >= MaxOffs) {
          N = fastEmit_ri_(VT, ISD::ADD, N, TotalOffs, VT);
          if (!N) // Unhandled operand. Halt "fast" selection and bail.
            return false;
          TotalOffs = 0;
        }
        continue;
      }
      if (TotalOffs) {
        N = fastEmit_ri_(VT, ISD::ADD, N, TotalOffs, VT);
        if (!N) // Unhandled operand. Halt "fast" selection and bail.
          return false;
        TotalOffs = 0;
      }

      // N = N + Idx * ElementSize;
      uint64_t ElementSize = GTI.getSequentialElementStride(DL);
      Register IdxN = getRegForGEPIndex(VT, Idx);
      if (!IdxN) // Unhandled operand. Halt "fast" selection and bail.
        return false;

      if (ElementSize != 1) {
        IdxN = fastEmit_ri_(VT, ISD::MUL, IdxN, ElementSize, VT);
        if (!IdxN) // Unhandled operand. Halt "fast" selection and bail.
          return false;
      }
      N = fastEmit_rr(VT, VT, ISD::ADD, N, IdxN);
      if (!N) // Unhandled operand. Halt "fast" selection and bail.
        return false;
    }
  }
  if (TotalOffs) {
    N = fastEmit_ri_(VT, ISD::ADD, N, TotalOffs, VT);
    if (!N) // Unhandled operand. Halt "fast" selection and bail.
      return false;
  }

  // We successfully emitted code for the given LLVM Instruction.
  updateValueMap(I, N);
  return true;
}

bool FastISel::addStackMapLiveVars(SmallVectorImpl<MachineOperand> &Ops,
                                   const CallInst *CI, unsigned StartIdx) {
  for (unsigned i = StartIdx, e = CI->arg_size(); i != e; ++i) {
    Value *Val = CI->getArgOperand(i);
    // Check for constants and encode them with a StackMaps::ConstantOp prefix.
    if (const auto *C = dyn_cast<ConstantInt>(Val)) {
      Ops.push_back(MachineOperand::CreateImm(StackMaps::ConstantOp));
      Ops.push_back(MachineOperand::CreateImm(C->getSExtValue()));
    } else if (isa<ConstantPointerNull>(Val)) {
      Ops.push_back(MachineOperand::CreateImm(StackMaps::ConstantOp));
      Ops.push_back(MachineOperand::CreateImm(0));
    } else if (auto *AI = dyn_cast<AllocaInst>(Val)) {
      // Values coming from a stack location also require a special encoding,
      // but that is added later on by the target specific frame index
      // elimination implementation.
      auto SI = FuncInfo.StaticAllocaMap.find(AI);
      if (SI != FuncInfo.StaticAllocaMap.end())
        Ops.push_back(MachineOperand::CreateFI(SI->second));
      else
        return false;
    } else {
      Register Reg = getRegForValue(Val);
      if (!Reg)
        return false;
      Ops.push_back(MachineOperand::CreateReg(Reg, /*isDef=*/false));
    }
  }
  return true;
}

bool FastISel::selectStackmap(const CallInst *I) {
  // void @llvm.experimental.stackmap(i64 <id>, i32 <numShadowBytes>,
  //                                  [live variables...])
  assert(I->getCalledFunction()->getReturnType()->isVoidTy() &&
         "Stackmap cannot return a value.");

  // The stackmap intrinsic only records the live variables (the arguments
  // passed to it) and emits NOPS (if requested). Unlike the patchpoint
  // intrinsic, this won't be lowered to a function call. This means we don't
  // have to worry about calling conventions and target-specific lowering code.
  // Instead we perform the call lowering right here.
  //
  // CALLSEQ_START(0, 0...)
  // STACKMAP(id, nbytes, ...)
  // CALLSEQ_END(0, 0)
  //
  SmallVector<MachineOperand, 32> Ops;

  // Add the <id> and <numBytes> constants.
  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::IDPos)) &&
         "Expected a constant integer.");
  const auto *ID = cast<ConstantInt>(I->getOperand(PatchPointOpers::IDPos));
  Ops.push_back(MachineOperand::CreateImm(ID->getZExtValue()));

  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos)) &&
         "Expected a constant integer.");
  const auto *NumBytes =
      cast<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos));
  Ops.push_back(MachineOperand::CreateImm(NumBytes->getZExtValue()));

  // Push live variables for the stack map (skipping the first two arguments
  // <id> and <numBytes>).
  if (!addStackMapLiveVars(Ops, I, 2))
    return false;

  // We are not adding any register mask info here, because the stackmap doesn't
  // clobber anything.

  // Add scratch registers as implicit def and early clobber.
  CallingConv::ID CC = I->getCallingConv();
  const MCPhysReg *ScratchRegs = TLI.getScratchRegisters(CC);
  for (unsigned i = 0; ScratchRegs[i]; ++i)
    Ops.push_back(MachineOperand::CreateReg(
        ScratchRegs[i], /*isDef=*/true, /*isImp=*/true, /*isKill=*/false,
        /*isDead=*/false, /*isUndef=*/false, /*isEarlyClobber=*/true));

  // Issue CALLSEQ_START
  unsigned AdjStackDown = TII.getCallFrameSetupOpcode();
  auto Builder =
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(AdjStackDown));
  const MCInstrDesc &MCID = Builder.getInstr()->getDesc();
  for (unsigned I = 0, E = MCID.getNumOperands(); I < E; ++I)
    Builder.addImm(0);

  // Issue STACKMAP.
  MachineInstrBuilder MIB = BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
                                    TII.get(TargetOpcode::STACKMAP));
  for (auto const &MO : Ops)
    MIB.add(MO);

  // Issue CALLSEQ_END
  unsigned AdjStackUp = TII.getCallFrameDestroyOpcode();
  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(AdjStackUp))
      .addImm(0)
      .addImm(0);

  // Inform the Frame Information that we have a stackmap in this function.
  FuncInfo.MF->getFrameInfo().setHasStackMap();

  return true;
}

/// Lower an argument list according to the target calling convention.
///
/// This is a helper for lowering intrinsics that follow a target calling
/// convention or require stack pointer adjustment. Only a subset of the
/// intrinsic's operands need to participate in the calling convention.
bool FastISel::lowerCallOperands(const CallInst *CI, unsigned ArgIdx,
                                 unsigned NumArgs, const Value *Callee,
                                 bool ForceRetVoidTy, CallLoweringInfo &CLI) {
  ArgListTy Args;
  Args.reserve(NumArgs);

  // Populate the argument list.
  for (unsigned ArgI = ArgIdx, ArgE = ArgIdx + NumArgs; ArgI != ArgE; ++ArgI) {
    Value *V = CI->getOperand(ArgI);

    assert(!V->getType()->isEmptyTy() && "Empty type passed to intrinsic.");

    ArgListEntry Entry;
    Entry.Val = V;
    Entry.Ty = V->getType();
    Entry.setAttributes(CI, ArgI);
    Args.push_back(Entry);
  }

  Type *RetTy = ForceRetVoidTy ? Type::getVoidTy(CI->getType()->getContext())
                               : CI->getType();
  CLI.setCallee(CI->getCallingConv(), RetTy, Callee, std::move(Args), NumArgs);

  return lowerCallTo(CLI);
}

FastISel::CallLoweringInfo &FastISel::CallLoweringInfo::setCallee(
    const DataLayout &DL, MCContext &Ctx, CallingConv::ID CC, Type *ResultTy,
    StringRef Target, ArgListTy &&ArgsList, unsigned FixedArgs) {
  SmallString<32> MangledName;
  Mangler::getNameWithPrefix(MangledName, Target, DL);
  MCSymbol *Sym = Ctx.getOrCreateSymbol(MangledName);
  return setCallee(CC, ResultTy, Sym, std::move(ArgsList), FixedArgs);
}

bool FastISel::selectPatchpoint(const CallInst *I) {
  // <ty> @llvm.experimental.patchpoint.<ty>(i64 <id>,
  //                                         i32 <numBytes>,
  //                                         i8* <target>,
  //                                         i32 <numArgs>,
  //                                         [Args...],
  //                                         [live variables...])
  CallingConv::ID CC = I->getCallingConv();
  bool IsAnyRegCC = CC == CallingConv::AnyReg;
  bool HasDef = !I->getType()->isVoidTy();
  Value *Callee = I->getOperand(PatchPointOpers::TargetPos)->stripPointerCasts();

  // Check if we can lower the return type when using anyregcc.
  MVT ValueType;
  if (IsAnyRegCC && HasDef) {
    ValueType = TLI.getSimpleValueType(DL, I->getType(), /*AllowUnknown=*/true);
    if (ValueType == MVT::Other)
      return false;
  }

  // Get the real number of arguments participating in the call <numArgs>
  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::NArgPos)) &&
         "Expected a constant integer.");
  const auto *NumArgsVal =
      cast<ConstantInt>(I->getOperand(PatchPointOpers::NArgPos));
  unsigned NumArgs = NumArgsVal->getZExtValue();

  // Skip the four meta args: <id>, <numNopBytes>, <target>, <numArgs>
  // This includes all meta-operands up to but not including CC.
  unsigned NumMetaOpers = PatchPointOpers::CCPos;
  assert(I->arg_size() >= NumMetaOpers + NumArgs &&
         "Not enough arguments provided to the patchpoint intrinsic");

  // For AnyRegCC the arguments are lowered later on manually.
  unsigned NumCallArgs = IsAnyRegCC ? 0 : NumArgs;
  CallLoweringInfo CLI;
  CLI.setIsPatchPoint();
  if (!lowerCallOperands(I, NumMetaOpers, NumCallArgs, Callee, IsAnyRegCC, CLI))
    return false;

  assert(CLI.Call && "No call instruction specified.");

  SmallVector<MachineOperand, 32> Ops;

  // Add an explicit result reg if we use the anyreg calling convention.
  if (IsAnyRegCC && HasDef) {
    assert(CLI.NumResultRegs == 0 && "Unexpected result register.");
    assert(ValueType.isValid());
    CLI.ResultReg = createResultReg(TLI.getRegClassFor(ValueType));
    CLI.NumResultRegs = 1;
    Ops.push_back(MachineOperand::CreateReg(CLI.ResultReg, /*isDef=*/true));
  }

  // Add the <id> and <numBytes> constants.
  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::IDPos)) &&
         "Expected a constant integer.");
  const auto *ID = cast<ConstantInt>(I->getOperand(PatchPointOpers::IDPos));
  Ops.push_back(MachineOperand::CreateImm(ID->getZExtValue()));

  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos)) &&
         "Expected a constant integer.");
  const auto *NumBytes =
      cast<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos));
  Ops.push_back(MachineOperand::CreateImm(NumBytes->getZExtValue()));

  // Add the call target.
  if (const auto *C = dyn_cast<IntToPtrInst>(Callee)) {
    uint64_t CalleeConstAddr =
      cast<ConstantInt>(C->getOperand(0))->getZExtValue();
    Ops.push_back(MachineOperand::CreateImm(CalleeConstAddr));
  } else if (const auto *C = dyn_cast<ConstantExpr>(Callee)) {
    if (C->getOpcode() == Instruction::IntToPtr) {
      uint64_t CalleeConstAddr =
        cast<ConstantInt>(C->getOperand(0))->getZExtValue();
      Ops.push_back(MachineOperand::CreateImm(CalleeConstAddr));
    } else
      llvm_unreachable("Unsupported ConstantExpr.");
  } else if (const auto *GV = dyn_cast<GlobalValue>(Callee)) {
    Ops.push_back(MachineOperand::CreateGA(GV, 0));
  } else if (isa<ConstantPointerNull>(Callee))
    Ops.push_back(MachineOperand::CreateImm(0));
  else
    llvm_unreachable("Unsupported callee address.");

  // Adjust <numArgs> to account for any arguments that have been passed on
  // the stack instead.
  unsigned NumCallRegArgs = IsAnyRegCC ? NumArgs : CLI.OutRegs.size();
  Ops.push_back(MachineOperand::CreateImm(NumCallRegArgs));

  // Add the calling convention
  Ops.push_back(MachineOperand::CreateImm((unsigned)CC));

  // Add the arguments we omitted previously. The register allocator should
  // place these in any free register.
  if (IsAnyRegCC) {
    for (unsigned i = NumMetaOpers, e = NumMetaOpers + NumArgs; i != e; ++i) {
      Register Reg = getRegForValue(I->getArgOperand(i));
      if (!Reg)
        return false;
      Ops.push_back(MachineOperand::CreateReg(Reg, /*isDef=*/false));
    }
  }

  // Push the arguments from the call instruction.
  for (auto Reg : CLI.OutRegs)
    Ops.push_back(MachineOperand::CreateReg(Reg, /*isDef=*/false));

  // Push live variables for the stack map.
  if (!addStackMapLiveVars(Ops, I, NumMetaOpers + NumArgs))
    return false;

  // Push the register mask info.
  Ops.push_back(MachineOperand::CreateRegMask(
      TRI.getCallPreservedMask(*FuncInfo.MF, CC)));

  // Add scratch registers as implicit def and early clobber.
  const MCPhysReg *ScratchRegs = TLI.getScratchRegisters(CC);
  for (unsigned i = 0; ScratchRegs[i]; ++i)
    Ops.push_back(MachineOperand::CreateReg(
        ScratchRegs[i], /*isDef=*/true, /*isImp=*/true, /*isKill=*/false,
        /*isDead=*/false, /*isUndef=*/false, /*isEarlyClobber=*/true));

  // Add implicit defs (return values).
  for (auto Reg : CLI.InRegs)
    Ops.push_back(MachineOperand::CreateReg(Reg, /*isDef=*/true,
                                            /*isImp=*/true));

  // Insert the patchpoint instruction before the call generated by the target.
  MachineInstrBuilder MIB = BuildMI(*FuncInfo.MBB, CLI.Call, MIMD,
                                    TII.get(TargetOpcode::PATCHPOINT));

  for (auto &MO : Ops)
    MIB.add(MO);

  MIB->setPhysRegsDeadExcept(CLI.InRegs, TRI);

  // Delete the original call instruction.
  CLI.Call->eraseFromParent();

  // Inform the Frame Information that we have a patchpoint in this function.
  FuncInfo.MF->getFrameInfo().setHasPatchPoint();

  if (CLI.NumResultRegs)
    updateValueMap(I, CLI.ResultReg, CLI.NumResultRegs);
  return true;
}

bool FastISel::selectXRayCustomEvent(const CallInst *I) {
  const auto &Triple = TM.getTargetTriple();
  if (Triple.isAArch64(64) && Triple.getArch() != Triple::x86_64)
    return true; // don't do anything to this instruction.
  SmallVector<MachineOperand, 8> Ops;
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(0)),
                                          /*isDef=*/false));
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(1)),
                                          /*isDef=*/false));
  MachineInstrBuilder MIB =
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
              TII.get(TargetOpcode::PATCHABLE_EVENT_CALL));
  for (auto &MO : Ops)
    MIB.add(MO);

  // Insert the Patchable Event Call instruction, that gets lowered properly.
  return true;
}

bool FastISel::selectXRayTypedEvent(const CallInst *I) {
  const auto &Triple = TM.getTargetTriple();
  if (Triple.isAArch64(64) && Triple.getArch() != Triple::x86_64)
    return true; // don't do anything to this instruction.
  SmallVector<MachineOperand, 8> Ops;
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(0)),
                                          /*isDef=*/false));
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(1)),
                                          /*isDef=*/false));
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(2)),
                                          /*isDef=*/false));
  MachineInstrBuilder MIB =
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
              TII.get(TargetOpcode::PATCHABLE_TYPED_EVENT_CALL));
  for (auto &MO : Ops)
    MIB.add(MO);

  // Insert the Patchable Typed Event Call instruction, that gets lowered properly.
  return true;
}

/// Returns an AttributeList representing the attributes applied to the return
/// value of the given call.
static AttributeList getReturnAttrs(FastISel::CallLoweringInfo &CLI) {
  SmallVector<Attribute::AttrKind, 2> Attrs;
  if (CLI.RetSExt)
    Attrs.push_back(Attribute::SExt);
  if (CLI.RetZExt)
    Attrs.push_back(Attribute::ZExt);
  if (CLI.IsInReg)
    Attrs.push_back(Attribute::InReg);

  return AttributeList::get(CLI.RetTy->getContext(), AttributeList::ReturnIndex,
                            Attrs);
}

bool FastISel::lowerCallTo(const CallInst *CI, const char *SymName,
                           unsigned NumArgs) {
  MCContext &Ctx = MF->getContext();
  SmallString<32> MangledName;
  Mangler::getNameWithPrefix(MangledName, SymName, DL);
  MCSymbol *Sym = Ctx.getOrCreateSymbol(MangledName);
  return lowerCallTo(CI, Sym, NumArgs);
}

bool FastISel::lowerCallTo(const CallInst *CI, MCSymbol *Symbol,
                           unsigned NumArgs) {
  FunctionType *FTy = CI->getFunctionType();
  Type *RetTy = CI->getType();

  ArgListTy Args;
  Args.reserve(NumArgs);

  // Populate the argument list.
  // Attributes for args start at offset 1, after the return attribute.
  for (unsigned ArgI = 0; ArgI != NumArgs; ++ArgI) {
    Value *V = CI->getOperand(ArgI);

    assert(!V->getType()->isEmptyTy() && "Empty type passed to intrinsic.");

    ArgListEntry Entry;
    Entry.Val = V;
    Entry.Ty = V->getType();
    Entry.setAttributes(CI, ArgI);
    Args.push_back(Entry);
  }
  TLI.markLibCallAttributes(MF, CI->getCallingConv(), Args);

  CallLoweringInfo CLI;
  CLI.setCallee(RetTy, FTy, Symbol, std::move(Args), *CI, NumArgs);

  return lowerCallTo(CLI);
}

bool FastISel::lowerCallTo(CallLoweringInfo &CLI) {
  // Handle the incoming return values from the call.
  CLI.clearIns();
  SmallVector<EVT, 4> RetTys;
  ComputeValueVTs(TLI, DL, CLI.RetTy, RetTys);

  SmallVector<ISD::OutputArg, 4> Outs;
  GetReturnInfo(CLI.CallConv, CLI.RetTy, getReturnAttrs(CLI), Outs, TLI, DL);

  bool CanLowerReturn = TLI.CanLowerReturn(
      CLI.CallConv, *FuncInfo.MF, CLI.IsVarArg, Outs, CLI.RetTy->getContext());

  // FIXME: sret demotion isn't supported yet - bail out.
  if (!CanLowerReturn)
    return false;

  for (EVT VT : RetTys) {
    MVT RegisterVT = TLI.getRegisterType(CLI.RetTy->getContext(), VT);
    unsigned NumRegs = TLI.getNumRegisters(CLI.RetTy->getContext(), VT);
    for (unsigned i = 0; i != NumRegs; ++i) {
      ISD::InputArg MyFlags;
      MyFlags.VT = RegisterVT;
      MyFlags.ArgVT = VT;
      MyFlags.Used = CLI.IsReturnValueUsed;
      if (CLI.RetSExt)
        MyFlags.Flags.setSExt();
      if (CLI.RetZExt)
        MyFlags.Flags.setZExt();
      if (CLI.IsInReg)
        MyFlags.Flags.setInReg();
      CLI.Ins.push_back(MyFlags);
    }
  }

  // Handle all of the outgoing arguments.
  CLI.clearOuts();
  for (auto &Arg : CLI.getArgs()) {
    Type *FinalType = Arg.Ty;
    if (Arg.IsByVal)
      FinalType = Arg.IndirectType;
    bool NeedsRegBlock = TLI.functionArgumentNeedsConsecutiveRegisters(
        FinalType, CLI.CallConv, CLI.IsVarArg, DL);

    ISD::ArgFlagsTy Flags;
    if (Arg.IsZExt)
      Flags.setZExt();
    if (Arg.IsSExt)
      Flags.setSExt();
    if (Arg.IsInReg)
      Flags.setInReg();
    if (Arg.IsSRet)
      Flags.setSRet();
    if (Arg.IsSwiftSelf)
      Flags.setSwiftSelf();
    if (Arg.IsSwiftAsync)
      Flags.setSwiftAsync();
    if (Arg.IsSwiftError)
      Flags.setSwiftError();
    if (Arg.IsCFGuardTarget)
      Flags.setCFGuardTarget();
    if (Arg.IsByVal)
      Flags.setByVal();
    if (Arg.IsInAlloca) {
      Flags.setInAlloca();
      // Set the byval flag for CCAssignFn callbacks that don't know about
      // inalloca. This way we can know how many bytes we should've allocated
      // and how many bytes a callee cleanup function will pop.  If we port
      // inalloca to more targets, we'll have to add custom inalloca handling in
      // the various CC lowering callbacks.
      Flags.setByVal();
    }
    if (Arg.IsPreallocated) {
      Flags.setPreallocated();
      // Set the byval flag for CCAssignFn callbacks that don't know about
      // preallocated. This way we can know how many bytes we should've
      // allocated and how many bytes a callee cleanup function will pop.  If we
      // port preallocated to more targets, we'll have to add custom
      // preallocated handling in the various CC lowering callbacks.
      Flags.setByVal();
    }
    MaybeAlign MemAlign = Arg.Alignment;
    if (Arg.IsByVal || Arg.IsInAlloca || Arg.IsPreallocated) {
      unsigned FrameSize = DL.getTypeAllocSize(Arg.IndirectType);

      // For ByVal, alignment should come from FE. BE will guess if this info
      // is not there, but there are cases it cannot get right.
      if (!MemAlign)
        MemAlign = Align(TLI.getByValTypeAlignment(Arg.IndirectType, DL));
      Flags.setByValSize(FrameSize);
    } else if (!MemAlign) {
      MemAlign = DL.getABITypeAlign(Arg.Ty);
    }
    Flags.setMemAlign(*MemAlign);
    if (Arg.IsNest)
      Flags.setNest();
    if (NeedsRegBlock)
      Flags.setInConsecutiveRegs();
    Flags.setOrigAlign(DL.getABITypeAlign(Arg.Ty));
    CLI.OutVals.push_back(Arg.Val);
    CLI.OutFlags.push_back(Flags);
  }

  if (!fastLowerCall(CLI))
    return false;

  // Set all unused physreg defs as dead.
  assert(CLI.Call && "No call instruction specified.");
  CLI.Call->setPhysRegsDeadExcept(CLI.InRegs, TRI);

  if (CLI.NumResultRegs && CLI.CB)
    updateValueMap(CLI.CB, CLI.ResultReg, CLI.NumResultRegs);

  // Set labels for heapallocsite call.
  if (CLI.CB)
    if (MDNode *MD = CLI.CB->getMetadata("heapallocsite"))
      CLI.Call->setHeapAllocMarker(*MF, MD);

  return true;
}

bool FastISel::lowerCall(const CallInst *CI) {
  FunctionType *FuncTy = CI->getFunctionType();
  Type *RetTy = CI->getType();

  ArgListTy Args;
  ArgListEntry Entry;
  Args.reserve(CI->arg_size());

  for (auto i = CI->arg_begin(), e = CI->arg_end(); i != e; ++i) {
    Value *V = *i;

    // Skip empty types
    if (V->getType()->isEmptyTy())
      continue;

    Entry.Val = V;
    Entry.Ty = V->getType();

    // Skip the first return-type Attribute to get to params.
    Entry.setAttributes(CI, i - CI->arg_begin());
    Args.push_back(Entry);
  }

  // Check if target-independent constraints permit a tail call here.
  // Target-dependent constraints are checked within fastLowerCall.
  bool IsTailCall = CI->isTailCall();
  if (IsTailCall && !isInTailCallPosition(*CI, TM))
    IsTailCall = false;
  if (IsTailCall && !CI->isMustTailCall() &&
      MF->getFunction().getFnAttribute("disable-tail-calls").getValueAsBool())
    IsTailCall = false;

  CallLoweringInfo CLI;
  CLI.setCallee(RetTy, FuncTy, CI->getCalledOperand(), std::move(Args), *CI)
      .setTailCall(IsTailCall);

  diagnoseDontCall(*CI);

  return lowerCallTo(CLI);
}

bool FastISel::selectCall(const User *I) {
  const CallInst *Call = cast<CallInst>(I);

  // Handle simple inline asms.
  if (const InlineAsm *IA = dyn_cast<InlineAsm>(Call->getCalledOperand())) {
    // Don't attempt to handle constraints.
    if (!IA->getConstraintString().empty())
      return false;

    unsigned ExtraInfo = 0;
    if (IA->hasSideEffects())
      ExtraInfo |= InlineAsm::Extra_HasSideEffects;
    if (IA->isAlignStack())
      ExtraInfo |= InlineAsm::Extra_IsAlignStack;
    if (Call->isConvergent())
      ExtraInfo |= InlineAsm::Extra_IsConvergent;
    ExtraInfo |= IA->getDialect() * InlineAsm::Extra_AsmDialect;

    MachineInstrBuilder MIB = BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
                                      TII.get(TargetOpcode::INLINEASM));
    MIB.addExternalSymbol(IA->getAsmString().c_str());
    MIB.addImm(ExtraInfo);

    const MDNode *SrcLoc = Call->getMetadata("srcloc");
    if (SrcLoc)
      MIB.addMetadata(SrcLoc);

    return true;
  }

  // Handle intrinsic function calls.
  if (const auto *II = dyn_cast<IntrinsicInst>(Call))
    return selectIntrinsicCall(II);

  return lowerCall(Call);
}

void FastISel::handleDbgInfo(const Instruction *II) {
  if (!II->hasDbgRecords())
    return;

  // Clear any metadata.
  MIMD = MIMetadata();

  // Reverse order of debug records, because fast-isel walks through backwards.
  for (DbgRecord &DR : llvm::reverse(II->getDbgRecordRange())) {
    flushLocalValueMap();
    recomputeInsertPt();

    if (DbgLabelRecord *DLR = dyn_cast<DbgLabelRecord>(&DR)) {
      assert(DLR->getLabel() && "Missing label");
      if (!FuncInfo.MF->getMMI().hasDebugInfo()) {
        LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DLR << "\n");
        continue;
      }

      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DLR->getDebugLoc(),
              TII.get(TargetOpcode::DBG_LABEL))
          .addMetadata(DLR->getLabel());
      continue;
    }

    DbgVariableRecord &DVR = cast<DbgVariableRecord>(DR);

    Value *V = nullptr;
    if (!DVR.hasArgList())
      V = DVR.getVariableLocationOp(0);

    bool Res = false;
    if (DVR.getType() == DbgVariableRecord::LocationType::Value ||
        DVR.getType() == DbgVariableRecord::LocationType::Assign) {
      Res = lowerDbgValue(V, DVR.getExpression(), DVR.getVariable(),
                          DVR.getDebugLoc());
    } else {
      assert(DVR.getType() == DbgVariableRecord::LocationType::Declare);
      if (FuncInfo.PreprocessedDVRDeclares.contains(&DVR))
        continue;
      Res = lowerDbgDeclare(V, DVR.getExpression(), DVR.getVariable(),
                            DVR.getDebugLoc());
    }

    if (!Res)
      LLVM_DEBUG(dbgs() << "Dropping debug-info for " << DVR << "\n";);
  }
}

bool FastISel::lowerDbgValue(const Value *V, DIExpression *Expr,
                             DILocalVariable *Var, const DebugLoc &DL) {
  // This form of DBG_VALUE is target-independent.
  const MCInstrDesc &II = TII.get(TargetOpcode::DBG_VALUE);
  if (!V || isa<UndefValue>(V)) {
    // DI is either undef or cannot produce a valid DBG_VALUE, so produce an
    // undef DBG_VALUE to terminate any prior location.
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, false, 0U, Var, Expr);
    return true;
  }
  if (const auto *CI = dyn_cast<ConstantInt>(V)) {
    // See if there's an expression to constant-fold.
    if (Expr)
      std::tie(Expr, CI) = Expr->constantFold(CI);
    if (CI->getBitWidth() > 64)
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
          .addCImm(CI)
          .addImm(0U)
          .addMetadata(Var)
          .addMetadata(Expr);
    else
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
          .addImm(CI->getZExtValue())
          .addImm(0U)
          .addMetadata(Var)
          .addMetadata(Expr);
    return true;
  }
  if (const auto *CF = dyn_cast<ConstantFP>(V)) {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II)
        .addFPImm(CF)
        .addImm(0U)
        .addMetadata(Var)
        .addMetadata(Expr);
    return true;
  }
  if (const auto *Arg = dyn_cast<Argument>(V);
      Arg && Expr && Expr->isEntryValue()) {
    // As per the Verifier, this case is only valid for swift async Args.
    assert(Arg->hasAttribute(Attribute::AttrKind::SwiftAsync));

    Register Reg = getRegForValue(Arg);
    for (auto [PhysReg, VirtReg] : FuncInfo.RegInfo->liveins())
      if (Reg == VirtReg || Reg == PhysReg) {
        BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, false /*IsIndirect*/,
                PhysReg, Var, Expr);
        return true;
      }

    LLVM_DEBUG(dbgs() << "Dropping dbg.value: expression is entry_value but "
                         "couldn't find a physical register\n");
    return false;
  }
  if (auto SI = FuncInfo.StaticAllocaMap.find(dyn_cast<AllocaInst>(V));
      SI != FuncInfo.StaticAllocaMap.end()) {
    MachineOperand FrameIndexOp = MachineOperand::CreateFI(SI->second);
    bool IsIndirect = false;
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, IsIndirect, FrameIndexOp,
            Var, Expr);
    return true;
  }
  if (Register Reg = lookUpRegForValue(V)) {
    // FIXME: This does not handle register-indirect values at offset 0.
    if (!FuncInfo.MF->useDebugInstrRef()) {
      bool IsIndirect = false;
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, II, IsIndirect, Reg, Var,
              Expr);
      return true;
    }
    // If using instruction referencing, produce this as a DBG_INSTR_REF,
    // to be later patched up by finalizeDebugInstrRefs.
    SmallVector<MachineOperand, 1> MOs({MachineOperand::CreateReg(
        /* Reg */ Reg, /* isDef */ false, /* isImp */ false,
        /* isKill */ false, /* isDead */ false,
        /* isUndef */ false, /* isEarlyClobber */ false,
        /* SubReg */ 0, /* isDebug */ true)});
    SmallVector<uint64_t, 2> Ops({dwarf::DW_OP_LLVM_arg, 0});
    auto *NewExpr = DIExpression::prependOpcodes(Expr, Ops);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
            TII.get(TargetOpcode::DBG_INSTR_REF), /*IsIndirect*/ false, MOs,
            Var, NewExpr);
    return true;
  }
  return false;
}

bool FastISel::lowerDbgDeclare(const Value *Address, DIExpression *Expr,
                               DILocalVariable *Var, const DebugLoc &DL) {
  if (!Address || isa<UndefValue>(Address)) {
    LLVM_DEBUG(dbgs() << "Dropping debug info (bad/undef address)\n");
    return false;
  }

  std::optional<MachineOperand> Op;
  if (Register Reg = lookUpRegForValue(Address))
    Op = MachineOperand::CreateReg(Reg, false);

  // If we have a VLA that has a "use" in a metadata node that's then used
  // here but it has no other uses, then we have a problem. E.g.,
  //
  //   int foo (const int *x) {
  //     char a[*x];
  //     return 0;
  //   }
  //
  // If we assign 'a' a vreg and fast isel later on has to use the selection
  // DAG isel, it will want to copy the value to the vreg. However, there are
  // no uses, which goes counter to what selection DAG isel expects.
  if (!Op && !Address->use_empty() && isa<Instruction>(Address) &&
      (!isa<AllocaInst>(Address) ||
       !FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(Address))))
    Op = MachineOperand::CreateReg(FuncInfo.InitializeRegForValue(Address),
                                   false);

  if (Op) {
    assert(Var->isValidLocationForIntrinsic(DL) &&
           "Expected inlined-at fields to agree");
    if (FuncInfo.MF->useDebugInstrRef() && Op->isReg()) {
      // If using instruction referencing, produce this as a DBG_INSTR_REF,
      // to be later patched up by finalizeDebugInstrRefs. Tack a deref onto
      // the expression, we don't have an "indirect" flag in DBG_INSTR_REF.
      SmallVector<uint64_t, 3> Ops(
          {dwarf::DW_OP_LLVM_arg, 0, dwarf::DW_OP_deref});
      auto *NewExpr = DIExpression::prependOpcodes(Expr, Ops);
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
              TII.get(TargetOpcode::DBG_INSTR_REF), /*IsIndirect*/ false, *Op,
              Var, NewExpr);
      return true;
    }

    // A dbg.declare describes the address of a source variable, so lower it
    // into an indirect DBG_VALUE.
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
            TII.get(TargetOpcode::DBG_VALUE), /*IsIndirect*/ true, *Op, Var,
            Expr);
    return true;
  }

  // We can't yet handle anything else here because it would require
  // generating code, thus altering codegen because of debug info.
  LLVM_DEBUG(
      dbgs() << "Dropping debug info (no materialized reg for address)\n");
  return false;
}

bool FastISel::selectIntrinsicCall(const IntrinsicInst *II) {
  switch (II->getIntrinsicID()) {
  default:
    break;
  // At -O0 we don't care about the lifetime intrinsics.
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
  // The donothing intrinsic does, well, nothing.
  case Intrinsic::donothing:
  // Neither does the sideeffect intrinsic.
  case Intrinsic::sideeffect:
  // Neither does the assume intrinsic; it's also OK not to codegen its operand.
  case Intrinsic::assume:
  // Neither does the llvm.experimental.noalias.scope.decl intrinsic
  case Intrinsic::experimental_noalias_scope_decl:
    return true;
  case Intrinsic::dbg_declare: {
    const DbgDeclareInst *DI = cast<DbgDeclareInst>(II);
    assert(DI->getVariable() && "Missing variable");
    if (!FuncInfo.MF->getMMI().hasDebugInfo()) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI
                        << " (!hasDebugInfo)\n");
      return true;
    }

    if (FuncInfo.PreprocessedDbgDeclares.contains(DI))
      return true;

    const Value *Address = DI->getAddress();
    if (!lowerDbgDeclare(Address, DI->getExpression(), DI->getVariable(),
                         MIMD.getDL()))
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI);

    return true;
  }
  case Intrinsic::dbg_assign:
    // A dbg.assign is a dbg.value with more information, typically produced
    // during optimisation. If one reaches fastisel then something odd has
    // happened (such as an optimised function being always-inlined into an
    // optnone function). We will not be using the extra information in the
    // dbg.assign in that case, just use its dbg.value fields.
    [[fallthrough]];
  case Intrinsic::dbg_value: {
    // This form of DBG_VALUE is target-independent.
    const DbgValueInst *DI = cast<DbgValueInst>(II);
    const Value *V = DI->getValue();
    DIExpression *Expr = DI->getExpression();
    DILocalVariable *Var = DI->getVariable();
    if (DI->hasArgList())
      // Signal that we don't have a location for this.
      V = nullptr;

    assert(Var->isValidLocationForIntrinsic(MIMD.getDL()) &&
           "Expected inlined-at fields to agree");

    if (!lowerDbgValue(V, Expr, Var, MIMD.getDL()))
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");

    return true;
  }
  case Intrinsic::dbg_label: {
    const DbgLabelInst *DI = cast<DbgLabelInst>(II);
    assert(DI->getLabel() && "Missing label");
    if (!FuncInfo.MF->getMMI().hasDebugInfo()) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");
      return true;
    }

    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
            TII.get(TargetOpcode::DBG_LABEL)).addMetadata(DI->getLabel());
    return true;
  }
  case Intrinsic::objectsize:
    llvm_unreachable("llvm.objectsize.* should have been lowered already");

  case Intrinsic::is_constant:
    llvm_unreachable("llvm.is.constant.* should have been lowered already");

  case Intrinsic::allow_runtime_check:
  case Intrinsic::allow_ubsan_check: {
    Register ResultReg = getRegForValue(ConstantInt::getTrue(II->getType()));
    if (!ResultReg)
      return false;
    updateValueMap(II, ResultReg);
    return true;
  }

  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
  case Intrinsic::expect: {
    Register ResultReg = getRegForValue(II->getArgOperand(0));
    if (!ResultReg)
      return false;
    updateValueMap(II, ResultReg);
    return true;
  }
  case Intrinsic::experimental_stackmap:
    return selectStackmap(II);
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint:
    return selectPatchpoint(II);

  case Intrinsic::xray_customevent:
    return selectXRayCustomEvent(II);
  case Intrinsic::xray_typedevent:
    return selectXRayTypedEvent(II);
  }

  return fastLowerIntrinsicCall(II);
}

bool FastISel::selectCast(const User *I, unsigned Opcode) {
  EVT SrcVT = TLI.getValueType(DL, I->getOperand(0)->getType());
  EVT DstVT = TLI.getValueType(DL, I->getType());

  if (SrcVT == MVT::Other || !SrcVT.isSimple() || DstVT == MVT::Other ||
      !DstVT.isSimple())
    // Unhandled type. Halt "fast" selection and bail.
    return false;

  // Check if the destination type is legal.
  if (!TLI.isTypeLegal(DstVT))
    return false;

  // Check if the source operand is legal.
  if (!TLI.isTypeLegal(SrcVT))
    return false;

  Register InputReg = getRegForValue(I->getOperand(0));
  if (!InputReg)
    // Unhandled operand.  Halt "fast" selection and bail.
    return false;

  Register ResultReg = fastEmit_r(SrcVT.getSimpleVT(), DstVT.getSimpleVT(),
                                  Opcode, InputReg);
  if (!ResultReg)
    return false;

  updateValueMap(I, ResultReg);
  return true;
}

bool FastISel::selectBitCast(const User *I) {
  EVT SrcEVT = TLI.getValueType(DL, I->getOperand(0)->getType());
  EVT DstEVT = TLI.getValueType(DL, I->getType());
  if (SrcEVT == MVT::Other || DstEVT == MVT::Other ||
      !TLI.isTypeLegal(SrcEVT) || !TLI.isTypeLegal(DstEVT))
    // Unhandled type. Halt "fast" selection and bail.
    return false;

  MVT SrcVT = SrcEVT.getSimpleVT();
  MVT DstVT = DstEVT.getSimpleVT();
  Register Op0 = getRegForValue(I->getOperand(0));
  if (!Op0) // Unhandled operand. Halt "fast" selection and bail.
    return false;

  // If the bitcast doesn't change the type, just use the operand value.
  if (SrcVT == DstVT) {
    updateValueMap(I, Op0);
    return true;
  }

  // Otherwise, select a BITCAST opcode.
  Register ResultReg = fastEmit_r(SrcVT, DstVT, ISD::BITCAST, Op0);
  if (!ResultReg)
    return false;

  updateValueMap(I, ResultReg);
  return true;
}

bool FastISel::selectFreeze(const User *I) {
  Register Reg = getRegForValue(I->getOperand(0));
  if (!Reg)
    // Unhandled operand.
    return false;

  EVT ETy = TLI.getValueType(DL, I->getOperand(0)->getType());
  if (ETy == MVT::Other || !TLI.isTypeLegal(ETy))
    // Unhandled type, bail out.
    return false;

  MVT Ty = ETy.getSimpleVT();
  const TargetRegisterClass *TyRegClass = TLI.getRegClassFor(Ty);
  Register ResultReg = createResultReg(TyRegClass);
  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
          TII.get(TargetOpcode::COPY), ResultReg).addReg(Reg);

  updateValueMap(I, ResultReg);
  return true;
}

// Remove local value instructions starting from the instruction after
// SavedLastLocalValue to the current function insert point.
void FastISel::removeDeadLocalValueCode(MachineInstr *SavedLastLocalValue)
{
  MachineInstr *CurLastLocalValue = getLastLocalValue();
  if (CurLastLocalValue != SavedLastLocalValue) {
    // Find the first local value instruction to be deleted.
    // This is the instruction after SavedLastLocalValue if it is non-NULL.
    // Otherwise it's the first instruction in the block.
    MachineBasicBlock::iterator FirstDeadInst(SavedLastLocalValue);
    if (SavedLastLocalValue)
      ++FirstDeadInst;
    else
      FirstDeadInst = FuncInfo.MBB->getFirstNonPHI();
    setLastLocalValue(SavedLastLocalValue);
    removeDeadCode(FirstDeadInst, FuncInfo.InsertPt);
  }
}

bool FastISel::selectInstruction(const Instruction *I) {
  // Flush the local value map before starting each instruction.
  // This improves locality and debugging, and can reduce spills.
  // Reuse of values across IR instructions is relatively uncommon.
  flushLocalValueMap();

  MachineInstr *SavedLastLocalValue = getLastLocalValue();
  // Just before the terminator instruction, insert instructions to
  // feed PHI nodes in successor blocks.
  if (I->isTerminator()) {
    if (!handlePHINodesInSuccessorBlocks(I->getParent())) {
      // PHI node handling may have generated local value instructions,
      // even though it failed to handle all PHI nodes.
      // We remove these instructions because SelectionDAGISel will generate
      // them again.
      removeDeadLocalValueCode(SavedLastLocalValue);
      return false;
    }
  }

  // FastISel does not handle any operand bundles except OB_funclet.
  if (auto *Call = dyn_cast<CallBase>(I))
    for (unsigned i = 0, e = Call->getNumOperandBundles(); i != e; ++i)
      if (Call->getOperandBundleAt(i).getTagID() != LLVMContext::OB_funclet)
        return false;

  MIMD = MIMetadata(*I);

  SavedInsertPt = FuncInfo.InsertPt;

  if (const auto *Call = dyn_cast<CallInst>(I)) {
    const Function *F = Call->getCalledFunction();
    LibFunc Func;

    // As a special case, don't handle calls to builtin library functions that
    // may be translated directly to target instructions.
    if (F && !F->hasLocalLinkage() && F->hasName() &&
        LibInfo->getLibFunc(F->getName(), Func) &&
        LibInfo->hasOptimizedCodeGen(Func))
      return false;

    // Don't handle Intrinsic::trap if a trap function is specified.
    if (F && F->getIntrinsicID() == Intrinsic::trap &&
        Call->hasFnAttr("trap-func-name"))
      return false;
  }

  // First, try doing target-independent selection.
  if (!SkipTargetIndependentISel) {
    if (selectOperator(I, I->getOpcode())) {
      ++NumFastIselSuccessIndependent;
      MIMD = {};
      return true;
    }
    // Remove dead code.
    recomputeInsertPt();
    if (SavedInsertPt != FuncInfo.InsertPt)
      removeDeadCode(FuncInfo.InsertPt, SavedInsertPt);
    SavedInsertPt = FuncInfo.InsertPt;
  }
  // Next, try calling the target to attempt to handle the instruction.
  if (fastSelectInstruction(I)) {
    ++NumFastIselSuccessTarget;
    MIMD = {};
    return true;
  }
  // Remove dead code.
  recomputeInsertPt();
  if (SavedInsertPt != FuncInfo.InsertPt)
    removeDeadCode(FuncInfo.InsertPt, SavedInsertPt);

  MIMD = {};
  // Undo phi node updates, because they will be added again by SelectionDAG.
  if (I->isTerminator()) {
    // PHI node handling may have generated local value instructions.
    // We remove them because SelectionDAGISel will generate them again.
    removeDeadLocalValueCode(SavedLastLocalValue);
    FuncInfo.PHINodesToUpdate.resize(FuncInfo.OrigNumPHINodesToUpdate);
  }
  return false;
}

/// Emit an unconditional branch to the given block, unless it is the immediate
/// (fall-through) successor, and update the CFG.
void FastISel::fastEmitBranch(MachineBasicBlock *MSucc,
                              const DebugLoc &DbgLoc) {
  if (FuncInfo.MBB->getBasicBlock()->sizeWithoutDebug() > 1 &&
      FuncInfo.MBB->isLayoutSuccessor(MSucc)) {
    // For more accurate line information if this is the only non-debug
    // instruction in the block then emit it, otherwise we have the
    // unconditional fall-through case, which needs no instructions.
  } else {
    // The unconditional branch case.
    TII.insertBranch(*FuncInfo.MBB, MSucc, nullptr,
                     SmallVector<MachineOperand, 0>(), DbgLoc);
  }
  if (FuncInfo.BPI) {
    auto BranchProbability = FuncInfo.BPI->getEdgeProbability(
        FuncInfo.MBB->getBasicBlock(), MSucc->getBasicBlock());
    FuncInfo.MBB->addSuccessor(MSucc, BranchProbability);
  } else
    FuncInfo.MBB->addSuccessorWithoutProb(MSucc);
}

void FastISel::finishCondBranch(const BasicBlock *BranchBB,
                                MachineBasicBlock *TrueMBB,
                                MachineBasicBlock *FalseMBB) {
  // Add TrueMBB as successor unless it is equal to the FalseMBB: This can
  // happen in degenerate IR and MachineIR forbids to have a block twice in the
  // successor/predecessor lists.
  if (TrueMBB != FalseMBB) {
    if (FuncInfo.BPI) {
      auto BranchProbability =
          FuncInfo.BPI->getEdgeProbability(BranchBB, TrueMBB->getBasicBlock());
      FuncInfo.MBB->addSuccessor(TrueMBB, BranchProbability);
    } else
      FuncInfo.MBB->addSuccessorWithoutProb(TrueMBB);
  }

  fastEmitBranch(FalseMBB, MIMD.getDL());
}

/// Emit an FNeg operation.
bool FastISel::selectFNeg(const User *I, const Value *In) {
  Register OpReg = getRegForValue(In);
  if (!OpReg)
    return false;

  // If the target has ISD::FNEG, use it.
  EVT VT = TLI.getValueType(DL, I->getType());
  Register ResultReg = fastEmit_r(VT.getSimpleVT(), VT.getSimpleVT(), ISD::FNEG,
                                  OpReg);
  if (ResultReg) {
    updateValueMap(I, ResultReg);
    return true;
  }

  // Bitcast the value to integer, twiddle the sign bit with xor,
  // and then bitcast it back to floating-point.
  if (VT.getSizeInBits() > 64)
    return false;
  EVT IntVT = EVT::getIntegerVT(I->getContext(), VT.getSizeInBits());
  if (!TLI.isTypeLegal(IntVT))
    return false;

  Register IntReg = fastEmit_r(VT.getSimpleVT(), IntVT.getSimpleVT(),
                               ISD::BITCAST, OpReg);
  if (!IntReg)
    return false;

  Register IntResultReg = fastEmit_ri_(
      IntVT.getSimpleVT(), ISD::XOR, IntReg,
      UINT64_C(1) << (VT.getSizeInBits() - 1), IntVT.getSimpleVT());
  if (!IntResultReg)
    return false;

  ResultReg = fastEmit_r(IntVT.getSimpleVT(), VT.getSimpleVT(), ISD::BITCAST,
                         IntResultReg);
  if (!ResultReg)
    return false;

  updateValueMap(I, ResultReg);
  return true;
}

bool FastISel::selectExtractValue(const User *U) {
  const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(U);
  if (!EVI)
    return false;

  // Make sure we only try to handle extracts with a legal result.  But also
  // allow i1 because it's easy.
  EVT RealVT = TLI.getValueType(DL, EVI->getType(), /*AllowUnknown=*/true);
  if (!RealVT.isSimple())
    return false;
  MVT VT = RealVT.getSimpleVT();
  if (!TLI.isTypeLegal(VT) && VT != MVT::i1)
    return false;

  const Value *Op0 = EVI->getOperand(0);
  Type *AggTy = Op0->getType();

  // Get the base result register.
  unsigned ResultReg;
  DenseMap<const Value *, Register>::iterator I = FuncInfo.ValueMap.find(Op0);
  if (I != FuncInfo.ValueMap.end())
    ResultReg = I->second;
  else if (isa<Instruction>(Op0))
    ResultReg = FuncInfo.InitializeRegForValue(Op0);
  else
    return false; // fast-isel can't handle aggregate constants at the moment

  // Get the actual result register, which is an offset from the base register.
  unsigned VTIndex = ComputeLinearIndex(AggTy, EVI->getIndices());

  SmallVector<EVT, 4> AggValueVTs;
  ComputeValueVTs(TLI, DL, AggTy, AggValueVTs);

  for (unsigned i = 0; i < VTIndex; i++)
    ResultReg += TLI.getNumRegisters(FuncInfo.Fn->getContext(), AggValueVTs[i]);

  updateValueMap(EVI, ResultReg);
  return true;
}

bool FastISel::selectOperator(const User *I, unsigned Opcode) {
  switch (Opcode) {
  case Instruction::Add:
    return selectBinaryOp(I, ISD::ADD);
  case Instruction::FAdd:
    return selectBinaryOp(I, ISD::FADD);
  case Instruction::Sub:
    return selectBinaryOp(I, ISD::SUB);
  case Instruction::FSub:
    return selectBinaryOp(I, ISD::FSUB);
  case Instruction::Mul:
    return selectBinaryOp(I, ISD::MUL);
  case Instruction::FMul:
    return selectBinaryOp(I, ISD::FMUL);
  case Instruction::SDiv:
    return selectBinaryOp(I, ISD::SDIV);
  case Instruction::UDiv:
    return selectBinaryOp(I, ISD::UDIV);
  case Instruction::FDiv:
    return selectBinaryOp(I, ISD::FDIV);
  case Instruction::SRem:
    return selectBinaryOp(I, ISD::SREM);
  case Instruction::URem:
    return selectBinaryOp(I, ISD::UREM);
  case Instruction::FRem:
    return selectBinaryOp(I, ISD::FREM);
  case Instruction::Shl:
    return selectBinaryOp(I, ISD::SHL);
  case Instruction::LShr:
    return selectBinaryOp(I, ISD::SRL);
  case Instruction::AShr:
    return selectBinaryOp(I, ISD::SRA);
  case Instruction::And:
    return selectBinaryOp(I, ISD::AND);
  case Instruction::Or:
    return selectBinaryOp(I, ISD::OR);
  case Instruction::Xor:
    return selectBinaryOp(I, ISD::XOR);

  case Instruction::FNeg:
    return selectFNeg(I, I->getOperand(0));

  case Instruction::GetElementPtr:
    return selectGetElementPtr(I);

  case Instruction::Br: {
    const BranchInst *BI = cast<BranchInst>(I);

    if (BI->isUnconditional()) {
      const BasicBlock *LLVMSucc = BI->getSuccessor(0);
      MachineBasicBlock *MSucc = FuncInfo.MBBMap[LLVMSucc];
      fastEmitBranch(MSucc, BI->getDebugLoc());
      return true;
    }

    // Conditional branches are not handed yet.
    // Halt "fast" selection and bail.
    return false;
  }

  case Instruction::Unreachable:
    if (TM.Options.TrapUnreachable)
      return fastEmit_(MVT::Other, MVT::Other, ISD::TRAP) != 0;
    else
      return true;

  case Instruction::Alloca:
    // FunctionLowering has the static-sized case covered.
    if (FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(I)))
      return true;

    // Dynamic-sized alloca is not handled yet.
    return false;

  case Instruction::Call:
    // On AIX, normal call lowering uses the DAG-ISEL path currently so that the
    // callee of the direct function call instruction will be mapped to the
    // symbol for the function's entry point, which is distinct from the
    // function descriptor symbol. The latter is the symbol whose XCOFF symbol
    // name is the C-linkage name of the source level function.
    // But fast isel still has the ability to do selection for intrinsics.
    if (TM.getTargetTriple().isOSAIX() && !isa<IntrinsicInst>(I))
      return false;
    return selectCall(I);

  case Instruction::BitCast:
    return selectBitCast(I);

  case Instruction::FPToSI:
    return selectCast(I, ISD::FP_TO_SINT);
  case Instruction::ZExt:
    return selectCast(I, ISD::ZERO_EXTEND);
  case Instruction::SExt:
    return selectCast(I, ISD::SIGN_EXTEND);
  case Instruction::Trunc:
    return selectCast(I, ISD::TRUNCATE);
  case Instruction::SIToFP:
    return selectCast(I, ISD::SINT_TO_FP);

  case Instruction::IntToPtr: // Deliberate fall-through.
  case Instruction::PtrToInt: {
    EVT SrcVT = TLI.getValueType(DL, I->getOperand(0)->getType());
    EVT DstVT = TLI.getValueType(DL, I->getType());
    if (DstVT.bitsGT(SrcVT))
      return selectCast(I, ISD::ZERO_EXTEND);
    if (DstVT.bitsLT(SrcVT))
      return selectCast(I, ISD::TRUNCATE);
    Register Reg = getRegForValue(I->getOperand(0));
    if (!Reg)
      return false;
    updateValueMap(I, Reg);
    return true;
  }

  case Instruction::ExtractValue:
    return selectExtractValue(I);

  case Instruction::Freeze:
    return selectFreeze(I);

  case Instruction::PHI:
    llvm_unreachable("FastISel shouldn't visit PHI nodes!");

  default:
    // Unhandled instruction. Halt "fast" selection and bail.
    return false;
  }
}

FastISel::FastISel(FunctionLoweringInfo &FuncInfo,
                   const TargetLibraryInfo *LibInfo,
                   bool SkipTargetIndependentISel)
    : FuncInfo(FuncInfo), MF(FuncInfo.MF), MRI(FuncInfo.MF->getRegInfo()),
      MFI(FuncInfo.MF->getFrameInfo()), MCP(*FuncInfo.MF->getConstantPool()),
      TM(FuncInfo.MF->getTarget()), DL(MF->getDataLayout()),
      TII(*MF->getSubtarget().getInstrInfo()),
      TLI(*MF->getSubtarget().getTargetLowering()),
      TRI(*MF->getSubtarget().getRegisterInfo()), LibInfo(LibInfo),
      SkipTargetIndependentISel(SkipTargetIndependentISel) {}

FastISel::~FastISel() = default;

bool FastISel::fastLowerArguments() { return false; }

bool FastISel::fastLowerCall(CallLoweringInfo & /*CLI*/) { return false; }

bool FastISel::fastLowerIntrinsicCall(const IntrinsicInst * /*II*/) {
  return false;
}

unsigned FastISel::fastEmit_(MVT, MVT, unsigned) { return 0; }

unsigned FastISel::fastEmit_r(MVT, MVT, unsigned, unsigned /*Op0*/) {
  return 0;
}

unsigned FastISel::fastEmit_rr(MVT, MVT, unsigned, unsigned /*Op0*/,
                               unsigned /*Op1*/) {
  return 0;
}

unsigned FastISel::fastEmit_i(MVT, MVT, unsigned, uint64_t /*Imm*/) {
  return 0;
}

unsigned FastISel::fastEmit_f(MVT, MVT, unsigned,
                              const ConstantFP * /*FPImm*/) {
  return 0;
}

unsigned FastISel::fastEmit_ri(MVT, MVT, unsigned, unsigned /*Op0*/,
                               uint64_t /*Imm*/) {
  return 0;
}

/// This method is a wrapper of fastEmit_ri. It first tries to emit an
/// instruction with an immediate operand using fastEmit_ri.
/// If that fails, it materializes the immediate into a register and try
/// fastEmit_rr instead.
Register FastISel::fastEmit_ri_(MVT VT, unsigned Opcode, unsigned Op0,
                                uint64_t Imm, MVT ImmType) {
  // If this is a multiply by a power of two, emit this as a shift left.
  if (Opcode == ISD::MUL && isPowerOf2_64(Imm)) {
    Opcode = ISD::SHL;
    Imm = Log2_64(Imm);
  } else if (Opcode == ISD::UDIV && isPowerOf2_64(Imm)) {
    // div x, 8 -> srl x, 3
    Opcode = ISD::SRL;
    Imm = Log2_64(Imm);
  }

  // Horrible hack (to be removed), check to make sure shift amounts are
  // in-range.
  if ((Opcode == ISD::SHL || Opcode == ISD::SRA || Opcode == ISD::SRL) &&
      Imm >= VT.getSizeInBits())
    return 0;

  // First check if immediate type is legal. If not, we can't use the ri form.
  Register ResultReg = fastEmit_ri(VT, VT, Opcode, Op0, Imm);
  if (ResultReg)
    return ResultReg;
  Register MaterialReg = fastEmit_i(ImmType, ImmType, ISD::Constant, Imm);
  if (!MaterialReg) {
    // This is a bit ugly/slow, but failing here means falling out of
    // fast-isel, which would be very slow.
    IntegerType *ITy =
        IntegerType::get(FuncInfo.Fn->getContext(), VT.getSizeInBits());
    MaterialReg = getRegForValue(ConstantInt::get(ITy, Imm));
    if (!MaterialReg)
      return 0;
  }
  return fastEmit_rr(VT, VT, Opcode, Op0, MaterialReg);
}

Register FastISel::createResultReg(const TargetRegisterClass *RC) {
  return MRI.createVirtualRegister(RC);
}

Register FastISel::constrainOperandRegClass(const MCInstrDesc &II, Register Op,
                                            unsigned OpNum) {
  if (Op.isVirtual()) {
    const TargetRegisterClass *RegClass =
        TII.getRegClass(II, OpNum, &TRI, *FuncInfo.MF);
    if (!MRI.constrainRegClass(Op, RegClass)) {
      // If it's not legal to COPY between the register classes, something
      // has gone very wrong before we got here.
      Register NewOp = createResultReg(RegClass);
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD,
              TII.get(TargetOpcode::COPY), NewOp).addReg(Op);
      return NewOp;
    }
  }
  return Op;
}

Register FastISel::fastEmitInst_(unsigned MachineInstOpcode,
                                 const TargetRegisterClass *RC) {
  Register ResultReg = createResultReg(RC);
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg);
  return ResultReg;
}

Register FastISel::fastEmitInst_r(unsigned MachineInstOpcode,
                                  const TargetRegisterClass *RC, unsigned Op0) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  Register ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addReg(Op0);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II)
        .addReg(Op0);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }

  return ResultReg;
}

Register FastISel::fastEmitInst_rr(unsigned MachineInstOpcode,
                                   const TargetRegisterClass *RC, unsigned Op0,
                                   unsigned Op1) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  Register ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());
  Op1 = constrainOperandRegClass(II, Op1, II.getNumDefs() + 1);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addReg(Op0)
        .addReg(Op1);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II)
        .addReg(Op0)
        .addReg(Op1);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }
  return ResultReg;
}

Register FastISel::fastEmitInst_rrr(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC, unsigned Op0,
                                    unsigned Op1, unsigned Op2) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  Register ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());
  Op1 = constrainOperandRegClass(II, Op1, II.getNumDefs() + 1);
  Op2 = constrainOperandRegClass(II, Op2, II.getNumDefs() + 2);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addReg(Op0)
        .addReg(Op1)
        .addReg(Op2);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II)
        .addReg(Op0)
        .addReg(Op1)
        .addReg(Op2);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }
  return ResultReg;
}

Register FastISel::fastEmitInst_ri(unsigned MachineInstOpcode,
                                   const TargetRegisterClass *RC, unsigned Op0,
                                   uint64_t Imm) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  Register ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addReg(Op0)
        .addImm(Imm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II)
        .addReg(Op0)
        .addImm(Imm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }
  return ResultReg;
}

Register FastISel::fastEmitInst_rii(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC, unsigned Op0,
                                    uint64_t Imm1, uint64_t Imm2) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  Register ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addReg(Op0)
        .addImm(Imm1)
        .addImm(Imm2);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II)
        .addReg(Op0)
        .addImm(Imm1)
        .addImm(Imm2);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }
  return ResultReg;
}

Register FastISel::fastEmitInst_f(unsigned MachineInstOpcode,
                                  const TargetRegisterClass *RC,
                                  const ConstantFP *FPImm) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  Register ResultReg = createResultReg(RC);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addFPImm(FPImm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II)
        .addFPImm(FPImm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }
  return ResultReg;
}

Register FastISel::fastEmitInst_rri(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC, unsigned Op0,
                                    unsigned Op1, uint64_t Imm) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  Register ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());
  Op1 = constrainOperandRegClass(II, Op1, II.getNumDefs() + 1);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addReg(Op0)
        .addReg(Op1)
        .addImm(Imm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II)
        .addReg(Op0)
        .addReg(Op1)
        .addImm(Imm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }
  return ResultReg;
}

Register FastISel::fastEmitInst_i(unsigned MachineInstOpcode,
                                  const TargetRegisterClass *RC, uint64_t Imm) {
  Register ResultReg = createResultReg(RC);
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II, ResultReg)
        .addImm(Imm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, II).addImm(Imm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
            ResultReg)
        .addReg(II.implicit_defs()[0]);
  }
  return ResultReg;
}

Register FastISel::fastEmitInst_extractsubreg(MVT RetVT, unsigned Op0,
                                              uint32_t Idx) {
  Register ResultReg = createResultReg(TLI.getRegClassFor(RetVT));
  assert(Register::isVirtualRegister(Op0) &&
         "Cannot yet extract from physregs");
  const TargetRegisterClass *RC = MRI.getRegClass(Op0);
  MRI.constrainRegClass(Op0, TRI.getSubClassWithSubReg(RC, Idx));
  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, TII.get(TargetOpcode::COPY),
          ResultReg).addReg(Op0, 0, Idx);
  return ResultReg;
}

/// Emit MachineInstrs to compute the value of Op with all but the least
/// significant bit set to zero.
Register FastISel::fastEmitZExtFromI1(MVT VT, unsigned Op0) {
  return fastEmit_ri(VT, VT, ISD::AND, Op0, 1);
}

/// HandlePHINodesInSuccessorBlocks - Handle PHI nodes in successor blocks.
/// Emit code to ensure constants are copied into registers when needed.
/// Remember the virtual registers that need to be added to the Machine PHI
/// nodes as input.  We cannot just directly add them, because expansion
/// might result in multiple MBB's for one BB.  As such, the start of the
/// BB might correspond to a different MBB than the end.
bool FastISel::handlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB) {
  SmallPtrSet<MachineBasicBlock *, 4> SuccsHandled;
  FuncInfo.OrigNumPHINodesToUpdate = FuncInfo.PHINodesToUpdate.size();

  // Check successor nodes' PHI nodes that expect a constant to be available
  // from this block.
  for (const BasicBlock *SuccBB : successors(LLVMBB)) {
    if (!isa<PHINode>(SuccBB->begin()))
      continue;
    MachineBasicBlock *SuccMBB = FuncInfo.MBBMap[SuccBB];

    // If this terminator has multiple identical successors (common for
    // switches), only handle each succ once.
    if (!SuccsHandled.insert(SuccMBB).second)
      continue;

    MachineBasicBlock::iterator MBBI = SuccMBB->begin();

    // At this point we know that there is a 1-1 correspondence between LLVM PHI
    // nodes and Machine PHI nodes, but the incoming operands have not been
    // emitted yet.
    for (const PHINode &PN : SuccBB->phis()) {
      // Ignore dead phi's.
      if (PN.use_empty())
        continue;

      // Only handle legal types. Two interesting things to note here. First,
      // by bailing out early, we may leave behind some dead instructions,
      // since SelectionDAG's HandlePHINodesInSuccessorBlocks will insert its
      // own moves. Second, this check is necessary because FastISel doesn't
      // use CreateRegs to create registers, so it always creates
      // exactly one register for each non-void instruction.
      EVT VT = TLI.getValueType(DL, PN.getType(), /*AllowUnknown=*/true);
      if (VT == MVT::Other || !TLI.isTypeLegal(VT)) {
        // Handle integer promotions, though, because they're common and easy.
        if (!(VT == MVT::i1 || VT == MVT::i8 || VT == MVT::i16)) {
          FuncInfo.PHINodesToUpdate.resize(FuncInfo.OrigNumPHINodesToUpdate);
          return false;
        }
      }

      const Value *PHIOp = PN.getIncomingValueForBlock(LLVMBB);

      // Set the DebugLoc for the copy. Use the location of the operand if
      // there is one; otherwise no location, flushLocalValueMap will fix it.
      MIMD = {};
      if (const auto *Inst = dyn_cast<Instruction>(PHIOp))
        MIMD = MIMetadata(*Inst);

      Register Reg = getRegForValue(PHIOp);
      if (!Reg) {
        FuncInfo.PHINodesToUpdate.resize(FuncInfo.OrigNumPHINodesToUpdate);
        return false;
      }
      FuncInfo.PHINodesToUpdate.push_back(std::make_pair(&*MBBI++, Reg));
      MIMD = {};
    }
  }

  return true;
}

bool FastISel::tryToFoldLoad(const LoadInst *LI, const Instruction *FoldInst) {
  assert(LI->hasOneUse() &&
         "tryToFoldLoad expected a LoadInst with a single use");
  // We know that the load has a single use, but don't know what it is.  If it
  // isn't one of the folded instructions, then we can't succeed here.  Handle
  // this by scanning the single-use users of the load until we get to FoldInst.
  unsigned MaxUsers = 6; // Don't scan down huge single-use chains of instrs.

  const Instruction *TheUser = LI->user_back();
  while (TheUser != FoldInst && // Scan up until we find FoldInst.
         // Stay in the right block.
         TheUser->getParent() == FoldInst->getParent() &&
         --MaxUsers) { // Don't scan too far.
    // If there are multiple or no uses of this instruction, then bail out.
    if (!TheUser->hasOneUse())
      return false;

    TheUser = TheUser->user_back();
  }

  // If we didn't find the fold instruction, then we failed to collapse the
  // sequence.
  if (TheUser != FoldInst)
    return false;

  // Don't try to fold volatile loads.  Target has to deal with alignment
  // constraints.
  if (LI->isVolatile())
    return false;

  // Figure out which vreg this is going into.  If there is no assigned vreg yet
  // then there actually was no reference to it.  Perhaps the load is referenced
  // by a dead instruction.
  Register LoadReg = getRegForValue(LI);
  if (!LoadReg)
    return false;

  // We can't fold if this vreg has no uses or more than one use.  Multiple uses
  // may mean that the instruction got lowered to multiple MIs, or the use of
  // the loaded value ended up being multiple operands of the result.
  if (!MRI.hasOneUse(LoadReg))
    return false;

  // If the register has fixups, there may be additional uses through a
  // different alias of the register.
  if (FuncInfo.RegsWithFixups.contains(LoadReg))
    return false;

  MachineRegisterInfo::reg_iterator RI = MRI.reg_begin(LoadReg);
  MachineInstr *User = RI->getParent();

  // Set the insertion point properly.  Folding the load can cause generation of
  // other random instructions (like sign extends) for addressing modes; make
  // sure they get inserted in a logical place before the new instruction.
  FuncInfo.InsertPt = User;
  FuncInfo.MBB = User->getParent();

  // Ask the target to try folding the load.
  return tryToFoldLoadIntoMI(User, RI.getOperandNo(), LI);
}

bool FastISel::canFoldAddIntoGEP(const User *GEP, const Value *Add) {
  // Must be an add.
  if (!isa<AddOperator>(Add))
    return false;
  // Type size needs to match.
  if (DL.getTypeSizeInBits(GEP->getType()) !=
      DL.getTypeSizeInBits(Add->getType()))
    return false;
  // Must be in the same basic block.
  if (isa<Instruction>(Add) &&
      FuncInfo.MBBMap[cast<Instruction>(Add)->getParent()] != FuncInfo.MBB)
    return false;
  // Must have a constant operand.
  return isa<ConstantInt>(cast<AddOperator>(Add)->getOperand(1));
}

MachineMemOperand *
FastISel::createMachineMemOperandFor(const Instruction *I) const {
  const Value *Ptr;
  Type *ValTy;
  MaybeAlign Alignment;
  MachineMemOperand::Flags Flags;
  bool IsVolatile;

  if (const auto *LI = dyn_cast<LoadInst>(I)) {
    Alignment = LI->getAlign();
    IsVolatile = LI->isVolatile();
    Flags = MachineMemOperand::MOLoad;
    Ptr = LI->getPointerOperand();
    ValTy = LI->getType();
  } else if (const auto *SI = dyn_cast<StoreInst>(I)) {
    Alignment = SI->getAlign();
    IsVolatile = SI->isVolatile();
    Flags = MachineMemOperand::MOStore;
    Ptr = SI->getPointerOperand();
    ValTy = SI->getValueOperand()->getType();
  } else
    return nullptr;

  bool IsNonTemporal = I->hasMetadata(LLVMContext::MD_nontemporal);
  bool IsInvariant = I->hasMetadata(LLVMContext::MD_invariant_load);
  bool IsDereferenceable = I->hasMetadata(LLVMContext::MD_dereferenceable);
  const MDNode *Ranges = I->getMetadata(LLVMContext::MD_range);

  AAMDNodes AAInfo = I->getAAMetadata();

  if (!Alignment) // Ensure that codegen never sees alignment 0.
    Alignment = DL.getABITypeAlign(ValTy);

  unsigned Size = DL.getTypeStoreSize(ValTy);

  if (IsVolatile)
    Flags |= MachineMemOperand::MOVolatile;
  if (IsNonTemporal)
    Flags |= MachineMemOperand::MONonTemporal;
  if (IsDereferenceable)
    Flags |= MachineMemOperand::MODereferenceable;
  if (IsInvariant)
    Flags |= MachineMemOperand::MOInvariant;

  return FuncInfo.MF->getMachineMemOperand(MachinePointerInfo(Ptr), Flags, Size,
                                           *Alignment, AAInfo, Ranges);
}

CmpInst::Predicate FastISel::optimizeCmpPredicate(const CmpInst *CI) const {
  // If both operands are the same, then try to optimize or fold the cmp.
  CmpInst::Predicate Predicate = CI->getPredicate();
  if (CI->getOperand(0) != CI->getOperand(1))
    return Predicate;

  switch (Predicate) {
  default: llvm_unreachable("Invalid predicate!");
  case CmpInst::FCMP_FALSE: Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_OEQ:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_OGT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_OGE:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_OLT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_OLE:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_ONE:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_ORD:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_UNO:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_UEQ:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::FCMP_UGT:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_UGE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::FCMP_ULT:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_ULE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::FCMP_UNE:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_TRUE:  Predicate = CmpInst::FCMP_TRUE;  break;

  case CmpInst::ICMP_EQ:    Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_NE:    Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_UGT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_UGE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_ULT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_ULE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_SGT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_SGE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_SLT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_SLE:   Predicate = CmpInst::FCMP_TRUE;  break;
  }

  return Predicate;
}
