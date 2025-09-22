//===- FastISel.h - Definition of the FastISel class ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the FastISel class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FASTISEL_H
#define LLVM_CODEGEN_FASTISEL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include <cstdint>
#include <utility>

namespace llvm {

class AllocaInst;
class Instruction;
class IntrinsicInst;
class BasicBlock;
class CallInst;
class Constant;
class ConstantFP;
class DataLayout;
class FunctionLoweringInfo;
class LoadInst;
class MachineConstantPool;
class MachineFrameInfo;
class MachineFunction;
class MachineInstr;
class MachineMemOperand;
class MachineOperand;
class MachineRegisterInfo;
class MCContext;
class MCInstrDesc;
class MCSymbol;
class TargetInstrInfo;
class TargetLibraryInfo;
class TargetMachine;
class TargetRegisterClass;
class TargetRegisterInfo;
class Type;
class User;
class Value;

/// This is a fast-path instruction selection class that generates poor
/// code and doesn't support illegal types or non-trivial lowering, but runs
/// quickly.
class FastISel {
public:
  using ArgListEntry = TargetLoweringBase::ArgListEntry;
  using ArgListTy = TargetLoweringBase::ArgListTy;
  struct CallLoweringInfo {
    Type *RetTy = nullptr;
    bool RetSExt : 1;
    bool RetZExt : 1;
    bool IsVarArg : 1;
    bool IsInReg : 1;
    bool DoesNotReturn : 1;
    bool IsReturnValueUsed : 1;
    bool IsPatchPoint : 1;

    // IsTailCall Should be modified by implementations of FastLowerCall
    // that perform tail call conversions.
    bool IsTailCall = false;

    unsigned NumFixedArgs = -1;
    CallingConv::ID CallConv = CallingConv::C;
    const Value *Callee = nullptr;
    MCSymbol *Symbol = nullptr;
    ArgListTy Args;
    const CallBase *CB = nullptr;
    MachineInstr *Call = nullptr;
    Register ResultReg;
    unsigned NumResultRegs = 0;

    SmallVector<Value *, 16> OutVals;
    SmallVector<ISD::ArgFlagsTy, 16> OutFlags;
    SmallVector<Register, 16> OutRegs;
    SmallVector<ISD::InputArg, 4> Ins;
    SmallVector<Register, 4> InRegs;

    CallLoweringInfo()
        : RetSExt(false), RetZExt(false), IsVarArg(false), IsInReg(false),
          DoesNotReturn(false), IsReturnValueUsed(true), IsPatchPoint(false) {}

    CallLoweringInfo &setCallee(Type *ResultTy, FunctionType *FuncTy,
                                const Value *Target, ArgListTy &&ArgsList,
                                const CallBase &Call) {
      RetTy = ResultTy;
      Callee = Target;

      IsInReg = Call.hasRetAttr(Attribute::InReg);
      DoesNotReturn = Call.doesNotReturn();
      IsVarArg = FuncTy->isVarArg();
      IsReturnValueUsed = !Call.use_empty();
      RetSExt = Call.hasRetAttr(Attribute::SExt);
      RetZExt = Call.hasRetAttr(Attribute::ZExt);

      CallConv = Call.getCallingConv();
      Args = std::move(ArgsList);
      NumFixedArgs = FuncTy->getNumParams();

      CB = &Call;

      return *this;
    }

    CallLoweringInfo &setCallee(Type *ResultTy, FunctionType *FuncTy,
                                MCSymbol *Target, ArgListTy &&ArgsList,
                                const CallBase &Call,
                                unsigned FixedArgs = ~0U) {
      RetTy = ResultTy;
      Callee = Call.getCalledOperand();
      Symbol = Target;

      IsInReg = Call.hasRetAttr(Attribute::InReg);
      DoesNotReturn = Call.doesNotReturn();
      IsVarArg = FuncTy->isVarArg();
      IsReturnValueUsed = !Call.use_empty();
      RetSExt = Call.hasRetAttr(Attribute::SExt);
      RetZExt = Call.hasRetAttr(Attribute::ZExt);

      CallConv = Call.getCallingConv();
      Args = std::move(ArgsList);
      NumFixedArgs = (FixedArgs == ~0U) ? FuncTy->getNumParams() : FixedArgs;

      CB = &Call;

      return *this;
    }

    CallLoweringInfo &setCallee(CallingConv::ID CC, Type *ResultTy,
                                const Value *Target, ArgListTy &&ArgsList,
                                unsigned FixedArgs = ~0U) {
      RetTy = ResultTy;
      Callee = Target;
      CallConv = CC;
      Args = std::move(ArgsList);
      NumFixedArgs = (FixedArgs == ~0U) ? Args.size() : FixedArgs;
      return *this;
    }

    CallLoweringInfo &setCallee(const DataLayout &DL, MCContext &Ctx,
                                CallingConv::ID CC, Type *ResultTy,
                                StringRef Target, ArgListTy &&ArgsList,
                                unsigned FixedArgs = ~0U);

    CallLoweringInfo &setCallee(CallingConv::ID CC, Type *ResultTy,
                                MCSymbol *Target, ArgListTy &&ArgsList,
                                unsigned FixedArgs = ~0U) {
      RetTy = ResultTy;
      Symbol = Target;
      CallConv = CC;
      Args = std::move(ArgsList);
      NumFixedArgs = (FixedArgs == ~0U) ? Args.size() : FixedArgs;
      return *this;
    }

    CallLoweringInfo &setTailCall(bool Value = true) {
      IsTailCall = Value;
      return *this;
    }

    CallLoweringInfo &setIsPatchPoint(bool Value = true) {
      IsPatchPoint = Value;
      return *this;
    }

    ArgListTy &getArgs() { return Args; }

    void clearOuts() {
      OutVals.clear();
      OutFlags.clear();
      OutRegs.clear();
    }

    void clearIns() {
      Ins.clear();
      InRegs.clear();
    }
  };

protected:
  DenseMap<const Value *, Register> LocalValueMap;
  FunctionLoweringInfo &FuncInfo;
  MachineFunction *MF;
  MachineRegisterInfo &MRI;
  MachineFrameInfo &MFI;
  MachineConstantPool &MCP;
  MIMetadata MIMD;
  const TargetMachine &TM;
  const DataLayout &DL;
  const TargetInstrInfo &TII;
  const TargetLowering &TLI;
  const TargetRegisterInfo &TRI;
  const TargetLibraryInfo *LibInfo;
  bool SkipTargetIndependentISel;

  /// The position of the last instruction for materializing constants
  /// for use in the current block. It resets to EmitStartPt when it makes sense
  /// (for example, it's usually profitable to avoid function calls between the
  /// definition and the use)
  MachineInstr *LastLocalValue = nullptr;

  /// The top most instruction in the current block that is allowed for
  /// emitting local variables. LastLocalValue resets to EmitStartPt when it
  /// makes sense (for example, on function calls)
  MachineInstr *EmitStartPt = nullptr;

public:
  virtual ~FastISel();

  /// Return the position of the last instruction emitted for
  /// materializing constants for use in the current block.
  MachineInstr *getLastLocalValue() { return LastLocalValue; }

  /// Update the position of the last instruction emitted for
  /// materializing constants for use in the current block.
  void setLastLocalValue(MachineInstr *I) {
    EmitStartPt = I;
    LastLocalValue = I;
  }

  /// Set the current block to which generated machine instructions will
  /// be appended.
  void startNewBlock();

  /// Flush the local value map.
  void finishBasicBlock();

  /// Return current debug location information.
  DebugLoc getCurDebugLoc() const { return MIMD.getDL(); }

  /// Do "fast" instruction selection for function arguments and append
  /// the machine instructions to the current block. Returns true when
  /// successful.
  bool lowerArguments();

  /// Do "fast" instruction selection for the given LLVM IR instruction
  /// and append the generated machine instructions to the current block.
  /// Returns true if selection was successful.
  bool selectInstruction(const Instruction *I);

  /// Do "fast" instruction selection for the given LLVM IR operator
  /// (Instruction or ConstantExpr), and append generated machine instructions
  /// to the current block. Return true if selection was successful.
  bool selectOperator(const User *I, unsigned Opcode);

  /// Create a virtual register and arrange for it to be assigned the
  /// value for the given LLVM value.
  Register getRegForValue(const Value *V);

  /// Look up the value to see if its value is already cached in a
  /// register. It may be defined by instructions across blocks or defined
  /// locally.
  Register lookUpRegForValue(const Value *V);

  /// This is a wrapper around getRegForValue that also takes care of
  /// truncating or sign-extending the given getelementptr index value.
  Register getRegForGEPIndex(MVT PtrVT, const Value *Idx);

  /// Retained for ABI compatibility in release branch.
  Register getRegForGEPIndex(const Value *Idx);

  /// We're checking to see if we can fold \p LI into \p FoldInst. Note
  /// that we could have a sequence where multiple LLVM IR instructions are
  /// folded into the same machineinstr.  For example we could have:
  ///
  ///   A: x = load i32 *P
  ///   B: y = icmp A, 42
  ///   C: br y, ...
  ///
  /// In this scenario, \p LI is "A", and \p FoldInst is "C".  We know about "B"
  /// (and any other folded instructions) because it is between A and C.
  ///
  /// If we succeed folding, return true.
  bool tryToFoldLoad(const LoadInst *LI, const Instruction *FoldInst);

  /// The specified machine instr operand is a vreg, and that vreg is
  /// being provided by the specified load instruction.  If possible, try to
  /// fold the load as an operand to the instruction, returning true if
  /// possible.
  ///
  /// This method should be implemented by targets.
  virtual bool tryToFoldLoadIntoMI(MachineInstr * /*MI*/, unsigned /*OpNo*/,
                                   const LoadInst * /*LI*/) {
    return false;
  }

  /// Reset InsertPt to prepare for inserting instructions into the
  /// current block.
  void recomputeInsertPt();

  /// Remove all dead instructions between the I and E.
  void removeDeadCode(MachineBasicBlock::iterator I,
                      MachineBasicBlock::iterator E);

  using SavePoint = MachineBasicBlock::iterator;

  /// Prepare InsertPt to begin inserting instructions into the local
  /// value area and return the old insert position.
  SavePoint enterLocalValueArea();

  /// Reset InsertPt to the given old insert position.
  void leaveLocalValueArea(SavePoint Old);

  /// Target-independent lowering of non-instruction debug info associated with
  /// this instruction.
  void handleDbgInfo(const Instruction *II);

protected:
  explicit FastISel(FunctionLoweringInfo &FuncInfo,
                    const TargetLibraryInfo *LibInfo,
                    bool SkipTargetIndependentISel = false);

  /// This method is called by target-independent code when the normal
  /// FastISel process fails to select an instruction. This gives targets a
  /// chance to emit code for anything that doesn't fit into FastISel's
  /// framework. It returns true if it was successful.
  virtual bool fastSelectInstruction(const Instruction *I) = 0;

  /// This method is called by target-independent code to do target-
  /// specific argument lowering. It returns true if it was successful.
  virtual bool fastLowerArguments();

  /// This method is called by target-independent code to do target-
  /// specific call lowering. It returns true if it was successful.
  virtual bool fastLowerCall(CallLoweringInfo &CLI);

  /// This method is called by target-independent code to do target-
  /// specific intrinsic lowering. It returns true if it was successful.
  virtual bool fastLowerIntrinsicCall(const IntrinsicInst *II);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type and opcode be emitted.
  virtual unsigned fastEmit_(MVT VT, MVT RetVT, unsigned Opcode);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and register operand be emitted.
  virtual unsigned fastEmit_r(MVT VT, MVT RetVT, unsigned Opcode, unsigned Op0);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and register operands be emitted.
  virtual unsigned fastEmit_rr(MVT VT, MVT RetVT, unsigned Opcode, unsigned Op0,
                               unsigned Op1);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and register and immediate
  /// operands be emitted.
  virtual unsigned fastEmit_ri(MVT VT, MVT RetVT, unsigned Opcode, unsigned Op0,
                               uint64_t Imm);

  /// This method is a wrapper of fastEmit_ri.
  ///
  /// It first tries to emit an instruction with an immediate operand using
  /// fastEmit_ri.  If that fails, it materializes the immediate into a register
  /// and try fastEmit_rr instead.
  Register fastEmit_ri_(MVT VT, unsigned Opcode, unsigned Op0, uint64_t Imm,
                        MVT ImmType);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and immediate operand be emitted.
  virtual unsigned fastEmit_i(MVT VT, MVT RetVT, unsigned Opcode, uint64_t Imm);

  /// This method is called by target-independent code to request that an
  /// instruction with the given type, opcode, and floating-point immediate
  /// operand be emitted.
  virtual unsigned fastEmit_f(MVT VT, MVT RetVT, unsigned Opcode,
                              const ConstantFP *FPImm);

  /// Emit a MachineInstr with no operands and a result register in the
  /// given register class.
  Register fastEmitInst_(unsigned MachineInstOpcode,
                         const TargetRegisterClass *RC);

  /// Emit a MachineInstr with one register operand and a result register
  /// in the given register class.
  Register fastEmitInst_r(unsigned MachineInstOpcode,
                          const TargetRegisterClass *RC, unsigned Op0);

  /// Emit a MachineInstr with two register operands and a result
  /// register in the given register class.
  Register fastEmitInst_rr(unsigned MachineInstOpcode,
                           const TargetRegisterClass *RC, unsigned Op0,
                           unsigned Op1);

  /// Emit a MachineInstr with three register operands and a result
  /// register in the given register class.
  Register fastEmitInst_rrr(unsigned MachineInstOpcode,
                            const TargetRegisterClass *RC, unsigned Op0,
                            unsigned Op1, unsigned Op2);

  /// Emit a MachineInstr with a register operand, an immediate, and a
  /// result register in the given register class.
  Register fastEmitInst_ri(unsigned MachineInstOpcode,
                           const TargetRegisterClass *RC, unsigned Op0,
                           uint64_t Imm);

  /// Emit a MachineInstr with one register operand and two immediate
  /// operands.
  Register fastEmitInst_rii(unsigned MachineInstOpcode,
                            const TargetRegisterClass *RC, unsigned Op0,
                            uint64_t Imm1, uint64_t Imm2);

  /// Emit a MachineInstr with a floating point immediate, and a result
  /// register in the given register class.
  Register fastEmitInst_f(unsigned MachineInstOpcode,
                          const TargetRegisterClass *RC,
                          const ConstantFP *FPImm);

  /// Emit a MachineInstr with two register operands, an immediate, and a
  /// result register in the given register class.
  Register fastEmitInst_rri(unsigned MachineInstOpcode,
                            const TargetRegisterClass *RC, unsigned Op0,
                            unsigned Op1, uint64_t Imm);

  /// Emit a MachineInstr with a single immediate operand, and a result
  /// register in the given register class.
  Register fastEmitInst_i(unsigned MachineInstOpcode,
                          const TargetRegisterClass *RC, uint64_t Imm);

  /// Emit a MachineInstr for an extract_subreg from a specified index of
  /// a superregister to a specified type.
  Register fastEmitInst_extractsubreg(MVT RetVT, unsigned Op0, uint32_t Idx);

  /// Emit MachineInstrs to compute the value of Op with all but the
  /// least significant bit set to zero.
  Register fastEmitZExtFromI1(MVT VT, unsigned Op0);

  /// Emit an unconditional branch to the given block, unless it is the
  /// immediate (fall-through) successor, and update the CFG.
  void fastEmitBranch(MachineBasicBlock *MSucc, const DebugLoc &DbgLoc);

  /// Emit an unconditional branch to \p FalseMBB, obtains the branch weight
  /// and adds TrueMBB and FalseMBB to the successor list.
  void finishCondBranch(const BasicBlock *BranchBB, MachineBasicBlock *TrueMBB,
                        MachineBasicBlock *FalseMBB);

  /// Update the value map to include the new mapping for this
  /// instruction, or insert an extra copy to get the result in a previous
  /// determined register.
  ///
  /// NOTE: This is only necessary because we might select a block that uses a
  /// value before we select the block that defines the value. It might be
  /// possible to fix this by selecting blocks in reverse postorder.
  void updateValueMap(const Value *I, Register Reg, unsigned NumRegs = 1);

  Register createResultReg(const TargetRegisterClass *RC);

  /// Try to constrain Op so that it is usable by argument OpNum of the
  /// provided MCInstrDesc. If this fails, create a new virtual register in the
  /// correct class and COPY the value there.
  Register constrainOperandRegClass(const MCInstrDesc &II, Register Op,
                                    unsigned OpNum);

  /// Emit a constant in a register using target-specific logic, such as
  /// constant pool loads.
  virtual unsigned fastMaterializeConstant(const Constant *C) { return 0; }

  /// Emit an alloca address in a register using target-specific logic.
  virtual unsigned fastMaterializeAlloca(const AllocaInst *C) { return 0; }

  /// Emit the floating-point constant +0.0 in a register using target-
  /// specific logic.
  virtual unsigned fastMaterializeFloatZero(const ConstantFP *CF) {
    return 0;
  }

  /// Check if \c Add is an add that can be safely folded into \c GEP.
  ///
  /// \c Add can be folded into \c GEP if:
  /// - \c Add is an add,
  /// - \c Add's size matches \c GEP's,
  /// - \c Add is in the same basic block as \c GEP, and
  /// - \c Add has a constant operand.
  bool canFoldAddIntoGEP(const User *GEP, const Value *Add);

  /// Create a machine mem operand from the given instruction.
  MachineMemOperand *createMachineMemOperandFor(const Instruction *I) const;

  CmpInst::Predicate optimizeCmpPredicate(const CmpInst *CI) const;

  bool lowerCallTo(const CallInst *CI, MCSymbol *Symbol, unsigned NumArgs);
  bool lowerCallTo(const CallInst *CI, const char *SymName,
                   unsigned NumArgs);
  bool lowerCallTo(CallLoweringInfo &CLI);

  bool lowerCall(const CallInst *I);
  /// Select and emit code for a binary operator instruction, which has
  /// an opcode which directly corresponds to the given ISD opcode.
  bool selectBinaryOp(const User *I, unsigned ISDOpcode);
  bool selectFNeg(const User *I, const Value *In);
  bool selectGetElementPtr(const User *I);
  bool selectStackmap(const CallInst *I);
  bool selectPatchpoint(const CallInst *I);
  bool selectCall(const User *I);
  bool selectIntrinsicCall(const IntrinsicInst *II);
  bool selectBitCast(const User *I);
  bool selectFreeze(const User *I);
  bool selectCast(const User *I, unsigned Opcode);
  bool selectExtractValue(const User *U);
  bool selectXRayCustomEvent(const CallInst *II);
  bool selectXRayTypedEvent(const CallInst *II);

  bool shouldOptForSize(const MachineFunction *MF) const {
    // TODO: Implement PGSO.
    return MF->getFunction().hasOptSize();
  }

  /// Target-independent lowering of debug information. Returns false if the
  /// debug information couldn't be lowered and was instead discarded.
  virtual bool lowerDbgValue(const Value *V, DIExpression *Expr,
                             DILocalVariable *Var, const DebugLoc &DL);

  /// Target-independent lowering of debug information. Returns false if the
  /// debug information couldn't be lowered and was instead discarded.
  virtual bool lowerDbgDeclare(const Value *V, DIExpression *Expr,
                               DILocalVariable *Var, const DebugLoc &DL);

private:
  /// Handle PHI nodes in successor blocks.
  ///
  /// Emit code to ensure constants are copied into registers when needed.
  /// Remember the virtual registers that need to be added to the Machine PHI
  /// nodes as input.  We cannot just directly add them, because expansion might
  /// result in multiple MBB's for one BB.  As such, the start of the BB might
  /// correspond to a different MBB than the end.
  bool handlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB);

  /// Helper for materializeRegForValue to materialize a constant in a
  /// target-independent way.
  Register materializeConstant(const Value *V, MVT VT);

  /// Helper for getRegForVale. This function is called when the value
  /// isn't already available in a register and must be materialized with new
  /// instructions.
  Register materializeRegForValue(const Value *V, MVT VT);

  /// Clears LocalValueMap and moves the area for the new local variables
  /// to the beginning of the block. It helps to avoid spilling cached variables
  /// across heavy instructions like calls.
  void flushLocalValueMap();

  /// Removes dead local value instructions after SavedLastLocalvalue.
  void removeDeadLocalValueCode(MachineInstr *SavedLastLocalValue);

  /// Insertion point before trying to select the current instruction.
  MachineBasicBlock::iterator SavedInsertPt;

  /// Add a stackmap or patchpoint intrinsic call's live variable
  /// operands to a stackmap or patchpoint machine instruction.
  bool addStackMapLiveVars(SmallVectorImpl<MachineOperand> &Ops,
                           const CallInst *CI, unsigned StartIdx);
  bool lowerCallOperands(const CallInst *CI, unsigned ArgIdx, unsigned NumArgs,
                         const Value *Callee, bool ForceRetVoidTy,
                         CallLoweringInfo &CLI);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_FASTISEL_H
