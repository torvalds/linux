//===-- lib/CodeGen/GlobalISel/InlineAsmLowering.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the lowering from LLVM IR inline asm to MIR INLINEASM
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/InlineAsmLowering.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/Module.h"

#define DEBUG_TYPE "inline-asm-lowering"

using namespace llvm;

void InlineAsmLowering::anchor() {}

namespace {

/// GISelAsmOperandInfo - This contains information for each constraint that we
/// are lowering.
class GISelAsmOperandInfo : public TargetLowering::AsmOperandInfo {
public:
  /// Regs - If this is a register or register class operand, this
  /// contains the set of assigned registers corresponding to the operand.
  SmallVector<Register, 1> Regs;

  explicit GISelAsmOperandInfo(const TargetLowering::AsmOperandInfo &Info)
      : TargetLowering::AsmOperandInfo(Info) {}
};

using GISelAsmOperandInfoVector = SmallVector<GISelAsmOperandInfo, 16>;

class ExtraFlags {
  unsigned Flags = 0;

public:
  explicit ExtraFlags(const CallBase &CB) {
    const InlineAsm *IA = cast<InlineAsm>(CB.getCalledOperand());
    if (IA->hasSideEffects())
      Flags |= InlineAsm::Extra_HasSideEffects;
    if (IA->isAlignStack())
      Flags |= InlineAsm::Extra_IsAlignStack;
    if (CB.isConvergent())
      Flags |= InlineAsm::Extra_IsConvergent;
    Flags |= IA->getDialect() * InlineAsm::Extra_AsmDialect;
  }

  void update(const TargetLowering::AsmOperandInfo &OpInfo) {
    // Ideally, we would only check against memory constraints.  However, the
    // meaning of an Other constraint can be target-specific and we can't easily
    // reason about it.  Therefore, be conservative and set MayLoad/MayStore
    // for Other constraints as well.
    if (OpInfo.ConstraintType == TargetLowering::C_Memory ||
        OpInfo.ConstraintType == TargetLowering::C_Other) {
      if (OpInfo.Type == InlineAsm::isInput)
        Flags |= InlineAsm::Extra_MayLoad;
      else if (OpInfo.Type == InlineAsm::isOutput)
        Flags |= InlineAsm::Extra_MayStore;
      else if (OpInfo.Type == InlineAsm::isClobber)
        Flags |= (InlineAsm::Extra_MayLoad | InlineAsm::Extra_MayStore);
    }
  }

  unsigned get() const { return Flags; }
};

} // namespace

/// Assign virtual/physical registers for the specified register operand.
static void getRegistersForValue(MachineFunction &MF,
                                 MachineIRBuilder &MIRBuilder,
                                 GISelAsmOperandInfo &OpInfo,
                                 GISelAsmOperandInfo &RefOpInfo) {

  const TargetLowering &TLI = *MF.getSubtarget().getTargetLowering();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // No work to do for memory operations.
  if (OpInfo.ConstraintType == TargetLowering::C_Memory)
    return;

  // If this is a constraint for a single physreg, or a constraint for a
  // register class, find it.
  Register AssignedReg;
  const TargetRegisterClass *RC;
  std::tie(AssignedReg, RC) = TLI.getRegForInlineAsmConstraint(
      &TRI, RefOpInfo.ConstraintCode, RefOpInfo.ConstraintVT);
  // RC is unset only on failure. Return immediately.
  if (!RC)
    return;

  // No need to allocate a matching input constraint since the constraint it's
  // matching to has already been allocated.
  if (OpInfo.isMatchingInputConstraint())
    return;

  // Initialize NumRegs.
  unsigned NumRegs = 1;
  if (OpInfo.ConstraintVT != MVT::Other)
    NumRegs =
        TLI.getNumRegisters(MF.getFunction().getContext(), OpInfo.ConstraintVT);

  // If this is a constraint for a specific physical register, but the type of
  // the operand requires more than one register to be passed, we allocate the
  // required amount of physical registers, starting from the selected physical
  // register.
  // For this, first retrieve a register iterator for the given register class
  TargetRegisterClass::iterator I = RC->begin();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  // Advance the iterator to the assigned register (if set)
  if (AssignedReg) {
    for (; *I != AssignedReg; ++I)
      assert(I != RC->end() && "AssignedReg should be a member of provided RC");
  }

  // Finally, assign the registers. If the AssignedReg isn't set, create virtual
  // registers with the provided register class
  for (; NumRegs; --NumRegs, ++I) {
    assert(I != RC->end() && "Ran out of registers to allocate!");
    Register R = AssignedReg ? Register(*I) : RegInfo.createVirtualRegister(RC);
    OpInfo.Regs.push_back(R);
  }
}

static void computeConstraintToUse(const TargetLowering *TLI,
                                   TargetLowering::AsmOperandInfo &OpInfo) {
  assert(!OpInfo.Codes.empty() && "Must have at least one constraint");

  // Single-letter constraints ('r') are very common.
  if (OpInfo.Codes.size() == 1) {
    OpInfo.ConstraintCode = OpInfo.Codes[0];
    OpInfo.ConstraintType = TLI->getConstraintType(OpInfo.ConstraintCode);
  } else {
    TargetLowering::ConstraintGroup G = TLI->getConstraintPreferences(OpInfo);
    if (G.empty())
      return;
    // FIXME: prefer immediate constraints if the target allows it
    unsigned BestIdx = 0;
    for (const unsigned E = G.size();
         BestIdx < E && (G[BestIdx].second == TargetLowering::C_Other ||
                         G[BestIdx].second == TargetLowering::C_Immediate);
         ++BestIdx)
      ;
    OpInfo.ConstraintCode = G[BestIdx].first;
    OpInfo.ConstraintType = G[BestIdx].second;
  }

  // 'X' matches anything.
  if (OpInfo.ConstraintCode == "X" && OpInfo.CallOperandVal) {
    // Labels and constants are handled elsewhere ('X' is the only thing
    // that matches labels).  For Functions, the type here is the type of
    // the result, which is not what we want to look at; leave them alone.
    Value *Val = OpInfo.CallOperandVal;
    if (isa<BasicBlock>(Val) || isa<ConstantInt>(Val) || isa<Function>(Val))
      return;

    // Otherwise, try to resolve it to something we know about by looking at
    // the actual operand type.
    if (const char *Repl = TLI->LowerXConstraint(OpInfo.ConstraintVT)) {
      OpInfo.ConstraintCode = Repl;
      OpInfo.ConstraintType = TLI->getConstraintType(OpInfo.ConstraintCode);
    }
  }
}

static unsigned getNumOpRegs(const MachineInstr &I, unsigned OpIdx) {
  const InlineAsm::Flag F(I.getOperand(OpIdx).getImm());
  return F.getNumOperandRegisters();
}

static bool buildAnyextOrCopy(Register Dst, Register Src,
                              MachineIRBuilder &MIRBuilder) {
  const TargetRegisterInfo *TRI =
      MIRBuilder.getMF().getSubtarget().getRegisterInfo();
  MachineRegisterInfo *MRI = MIRBuilder.getMRI();

  auto SrcTy = MRI->getType(Src);
  if (!SrcTy.isValid()) {
    LLVM_DEBUG(dbgs() << "Source type for copy is not valid\n");
    return false;
  }
  unsigned SrcSize = TRI->getRegSizeInBits(Src, *MRI);
  unsigned DstSize = TRI->getRegSizeInBits(Dst, *MRI);

  if (DstSize < SrcSize) {
    LLVM_DEBUG(dbgs() << "Input can't fit in destination reg class\n");
    return false;
  }

  // Attempt to anyext small scalar sources.
  if (DstSize > SrcSize) {
    if (!SrcTy.isScalar()) {
      LLVM_DEBUG(dbgs() << "Can't extend non-scalar input to size of"
                           "destination register class\n");
      return false;
    }
    Src = MIRBuilder.buildAnyExt(LLT::scalar(DstSize), Src).getReg(0);
  }

  MIRBuilder.buildCopy(Dst, Src);
  return true;
}

bool InlineAsmLowering::lowerInlineAsm(
    MachineIRBuilder &MIRBuilder, const CallBase &Call,
    std::function<ArrayRef<Register>(const Value &Val)> GetOrCreateVRegs)
    const {
  const InlineAsm *IA = cast<InlineAsm>(Call.getCalledOperand());

  /// ConstraintOperands - Information about all of the constraints.
  GISelAsmOperandInfoVector ConstraintOperands;

  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getDataLayout();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  MachineRegisterInfo *MRI = MIRBuilder.getMRI();

  TargetLowering::AsmOperandInfoVector TargetConstraints =
      TLI->ParseConstraints(DL, TRI, Call);

  ExtraFlags ExtraInfo(Call);
  unsigned ArgNo = 0; // ArgNo - The argument of the CallInst.
  unsigned ResNo = 0; // ResNo - The result number of the next output.
  for (auto &T : TargetConstraints) {
    ConstraintOperands.push_back(GISelAsmOperandInfo(T));
    GISelAsmOperandInfo &OpInfo = ConstraintOperands.back();

    // Compute the value type for each operand.
    if (OpInfo.hasArg()) {
      OpInfo.CallOperandVal = const_cast<Value *>(Call.getArgOperand(ArgNo));

      if (isa<BasicBlock>(OpInfo.CallOperandVal)) {
        LLVM_DEBUG(dbgs() << "Basic block input operands not supported yet\n");
        return false;
      }

      Type *OpTy = OpInfo.CallOperandVal->getType();

      // If this is an indirect operand, the operand is a pointer to the
      // accessed type.
      if (OpInfo.isIndirect) {
        OpTy = Call.getParamElementType(ArgNo);
        assert(OpTy && "Indirect operand must have elementtype attribute");
      }

      // FIXME: Support aggregate input operands
      if (!OpTy->isSingleValueType()) {
        LLVM_DEBUG(
            dbgs() << "Aggregate input operands are not supported yet\n");
        return false;
      }

      OpInfo.ConstraintVT =
          TLI->getAsmOperandValueType(DL, OpTy, true).getSimpleVT();
      ++ArgNo;
    } else if (OpInfo.Type == InlineAsm::isOutput && !OpInfo.isIndirect) {
      assert(!Call.getType()->isVoidTy() && "Bad inline asm!");
      if (StructType *STy = dyn_cast<StructType>(Call.getType())) {
        OpInfo.ConstraintVT =
            TLI->getSimpleValueType(DL, STy->getElementType(ResNo));
      } else {
        assert(ResNo == 0 && "Asm only has one result!");
        OpInfo.ConstraintVT =
            TLI->getAsmOperandValueType(DL, Call.getType()).getSimpleVT();
      }
      ++ResNo;
    } else {
      assert(OpInfo.Type != InlineAsm::isLabel &&
             "GlobalISel currently doesn't support callbr");
      OpInfo.ConstraintVT = MVT::Other;
    }

    if (OpInfo.ConstraintVT == MVT::i64x8)
      return false;

    // Compute the constraint code and ConstraintType to use.
    computeConstraintToUse(TLI, OpInfo);

    // The selected constraint type might expose new sideeffects
    ExtraInfo.update(OpInfo);
  }

  // At this point, all operand types are decided.
  // Create the MachineInstr, but don't insert it yet since input
  // operands still need to insert instructions before this one
  auto Inst = MIRBuilder.buildInstrNoInsert(TargetOpcode::INLINEASM)
                  .addExternalSymbol(IA->getAsmString().c_str())
                  .addImm(ExtraInfo.get());

  // Starting from this operand: flag followed by register(s) will be added as
  // operands to Inst for each constraint. Used for matching input constraints.
  unsigned StartIdx = Inst->getNumOperands();

  // Collects the output operands for later processing
  GISelAsmOperandInfoVector OutputOperands;

  for (auto &OpInfo : ConstraintOperands) {
    GISelAsmOperandInfo &RefOpInfo =
        OpInfo.isMatchingInputConstraint()
            ? ConstraintOperands[OpInfo.getMatchedOperand()]
            : OpInfo;

    // Assign registers for register operands
    getRegistersForValue(MF, MIRBuilder, OpInfo, RefOpInfo);

    switch (OpInfo.Type) {
    case InlineAsm::isOutput:
      if (OpInfo.ConstraintType == TargetLowering::C_Memory) {
        const InlineAsm::ConstraintCode ConstraintID =
            TLI->getInlineAsmMemConstraint(OpInfo.ConstraintCode);
        assert(ConstraintID != InlineAsm::ConstraintCode::Unknown &&
               "Failed to convert memory constraint code to constraint id.");

        // Add information to the INLINEASM instruction to know about this
        // output.
        InlineAsm::Flag Flag(InlineAsm::Kind::Mem, 1);
        Flag.setMemConstraint(ConstraintID);
        Inst.addImm(Flag);
        ArrayRef<Register> SourceRegs =
            GetOrCreateVRegs(*OpInfo.CallOperandVal);
        assert(
            SourceRegs.size() == 1 &&
            "Expected the memory output to fit into a single virtual register");
        Inst.addReg(SourceRegs[0]);
      } else {
        // Otherwise, this outputs to a register (directly for C_Register /
        // C_RegisterClass/C_Other.
        assert(OpInfo.ConstraintType == TargetLowering::C_Register ||
               OpInfo.ConstraintType == TargetLowering::C_RegisterClass ||
               OpInfo.ConstraintType == TargetLowering::C_Other);

        // Find a register that we can use.
        if (OpInfo.Regs.empty()) {
          LLVM_DEBUG(dbgs()
                     << "Couldn't allocate output register for constraint\n");
          return false;
        }

        // Add information to the INLINEASM instruction to know that this
        // register is set.
        InlineAsm::Flag Flag(OpInfo.isEarlyClobber
                                 ? InlineAsm::Kind::RegDefEarlyClobber
                                 : InlineAsm::Kind::RegDef,
                             OpInfo.Regs.size());
        if (OpInfo.Regs.front().isVirtual()) {
          // Put the register class of the virtual registers in the flag word.
          // That way, later passes can recompute register class constraints for
          // inline assembly as well as normal instructions. Don't do this for
          // tied operands that can use the regclass information from the def.
          const TargetRegisterClass *RC = MRI->getRegClass(OpInfo.Regs.front());
          Flag.setRegClass(RC->getID());
        }

        Inst.addImm(Flag);

        for (Register Reg : OpInfo.Regs) {
          Inst.addReg(Reg,
                      RegState::Define | getImplRegState(Reg.isPhysical()) |
                          (OpInfo.isEarlyClobber ? RegState::EarlyClobber : 0));
        }

        // Remember this output operand for later processing
        OutputOperands.push_back(OpInfo);
      }

      break;
    case InlineAsm::isInput:
    case InlineAsm::isLabel: {
      if (OpInfo.isMatchingInputConstraint()) {
        unsigned DefIdx = OpInfo.getMatchedOperand();
        // Find operand with register def that corresponds to DefIdx.
        unsigned InstFlagIdx = StartIdx;
        for (unsigned i = 0; i < DefIdx; ++i)
          InstFlagIdx += getNumOpRegs(*Inst, InstFlagIdx) + 1;
        assert(getNumOpRegs(*Inst, InstFlagIdx) == 1 && "Wrong flag");

        const InlineAsm::Flag MatchedOperandFlag(Inst->getOperand(InstFlagIdx).getImm());
        if (MatchedOperandFlag.isMemKind()) {
          LLVM_DEBUG(dbgs() << "Matching input constraint to mem operand not "
                               "supported. This should be target specific.\n");
          return false;
        }
        if (!MatchedOperandFlag.isRegDefKind() && !MatchedOperandFlag.isRegDefEarlyClobberKind()) {
          LLVM_DEBUG(dbgs() << "Unknown matching constraint\n");
          return false;
        }

        // We want to tie input to register in next operand.
        unsigned DefRegIdx = InstFlagIdx + 1;
        Register Def = Inst->getOperand(DefRegIdx).getReg();

        ArrayRef<Register> SrcRegs = GetOrCreateVRegs(*OpInfo.CallOperandVal);
        assert(SrcRegs.size() == 1 && "Single register is expected here");

        // When Def is physreg: use given input.
        Register In = SrcRegs[0];
        // When Def is vreg: copy input to new vreg with same reg class as Def.
        if (Def.isVirtual()) {
          In = MRI->createVirtualRegister(MRI->getRegClass(Def));
          if (!buildAnyextOrCopy(In, SrcRegs[0], MIRBuilder))
            return false;
        }

        // Add Flag and input register operand (In) to Inst. Tie In to Def.
        InlineAsm::Flag UseFlag(InlineAsm::Kind::RegUse, 1);
        UseFlag.setMatchingOp(DefIdx);
        Inst.addImm(UseFlag);
        Inst.addReg(In);
        Inst->tieOperands(DefRegIdx, Inst->getNumOperands() - 1);
        break;
      }

      if (OpInfo.ConstraintType == TargetLowering::C_Other &&
          OpInfo.isIndirect) {
        LLVM_DEBUG(dbgs() << "Indirect input operands with unknown constraint "
                             "not supported yet\n");
        return false;
      }

      if (OpInfo.ConstraintType == TargetLowering::C_Immediate ||
          OpInfo.ConstraintType == TargetLowering::C_Other) {

        std::vector<MachineOperand> Ops;
        if (!lowerAsmOperandForConstraint(OpInfo.CallOperandVal,
                                          OpInfo.ConstraintCode, Ops,
                                          MIRBuilder)) {
          LLVM_DEBUG(dbgs() << "Don't support constraint: "
                            << OpInfo.ConstraintCode << " yet\n");
          return false;
        }

        assert(Ops.size() > 0 &&
               "Expected constraint to be lowered to at least one operand");

        // Add information to the INLINEASM node to know about this input.
        const unsigned OpFlags =
            InlineAsm::Flag(InlineAsm::Kind::Imm, Ops.size());
        Inst.addImm(OpFlags);
        Inst.add(Ops);
        break;
      }

      if (OpInfo.ConstraintType == TargetLowering::C_Memory) {

        if (!OpInfo.isIndirect) {
          LLVM_DEBUG(dbgs()
                     << "Cannot indirectify memory input operands yet\n");
          return false;
        }

        assert(OpInfo.isIndirect && "Operand must be indirect to be a mem!");

        const InlineAsm::ConstraintCode ConstraintID =
            TLI->getInlineAsmMemConstraint(OpInfo.ConstraintCode);
        InlineAsm::Flag OpFlags(InlineAsm::Kind::Mem, 1);
        OpFlags.setMemConstraint(ConstraintID);
        Inst.addImm(OpFlags);
        ArrayRef<Register> SourceRegs =
            GetOrCreateVRegs(*OpInfo.CallOperandVal);
        assert(
            SourceRegs.size() == 1 &&
            "Expected the memory input to fit into a single virtual register");
        Inst.addReg(SourceRegs[0]);
        break;
      }

      assert((OpInfo.ConstraintType == TargetLowering::C_RegisterClass ||
              OpInfo.ConstraintType == TargetLowering::C_Register) &&
             "Unknown constraint type!");

      if (OpInfo.isIndirect) {
        LLVM_DEBUG(dbgs() << "Can't handle indirect register inputs yet "
                             "for constraint '"
                          << OpInfo.ConstraintCode << "'\n");
        return false;
      }

      // Copy the input into the appropriate registers.
      if (OpInfo.Regs.empty()) {
        LLVM_DEBUG(
            dbgs()
            << "Couldn't allocate input register for register constraint\n");
        return false;
      }

      unsigned NumRegs = OpInfo.Regs.size();
      ArrayRef<Register> SourceRegs = GetOrCreateVRegs(*OpInfo.CallOperandVal);
      assert(NumRegs == SourceRegs.size() &&
             "Expected the number of input registers to match the number of "
             "source registers");

      if (NumRegs > 1) {
        LLVM_DEBUG(dbgs() << "Input operands with multiple input registers are "
                             "not supported yet\n");
        return false;
      }

      InlineAsm::Flag Flag(InlineAsm::Kind::RegUse, NumRegs);
      if (OpInfo.Regs.front().isVirtual()) {
        // Put the register class of the virtual registers in the flag word.
        const TargetRegisterClass *RC = MRI->getRegClass(OpInfo.Regs.front());
        Flag.setRegClass(RC->getID());
      }
      Inst.addImm(Flag);
      if (!buildAnyextOrCopy(OpInfo.Regs[0], SourceRegs[0], MIRBuilder))
        return false;
      Inst.addReg(OpInfo.Regs[0]);
      break;
    }

    case InlineAsm::isClobber: {

      const unsigned NumRegs = OpInfo.Regs.size();
      if (NumRegs > 0) {
        unsigned Flag = InlineAsm::Flag(InlineAsm::Kind::Clobber, NumRegs);
        Inst.addImm(Flag);

        for (Register Reg : OpInfo.Regs) {
          Inst.addReg(Reg, RegState::Define | RegState::EarlyClobber |
                               getImplRegState(Reg.isPhysical()));
        }
      }
      break;
    }
    }
  }

  // Add rounding control registers as implicit def for inline asm.
  if (MF.getFunction().hasFnAttribute(Attribute::StrictFP)) {
    ArrayRef<MCPhysReg> RCRegs = TLI->getRoundingControlRegisters();
    for (MCPhysReg Reg : RCRegs)
      Inst.addReg(Reg, RegState::ImplicitDefine);
  }

  if (auto Bundle = Call.getOperandBundle(LLVMContext::OB_convergencectrl)) {
    auto *Token = Bundle->Inputs[0].get();
    ArrayRef<Register> SourceRegs = GetOrCreateVRegs(*Token);
    assert(SourceRegs.size() == 1 &&
           "Expected the control token to fit into a single virtual register");
    Inst.addUse(SourceRegs[0], RegState::Implicit);
  }

  if (const MDNode *SrcLoc = Call.getMetadata("srcloc"))
    Inst.addMetadata(SrcLoc);

  // All inputs are handled, insert the instruction now
  MIRBuilder.insertInstr(Inst);

  // Finally, copy the output operands into the output registers
  ArrayRef<Register> ResRegs = GetOrCreateVRegs(Call);
  if (ResRegs.size() != OutputOperands.size()) {
    LLVM_DEBUG(dbgs() << "Expected the number of output registers to match the "
                         "number of destination registers\n");
    return false;
  }
  for (unsigned int i = 0, e = ResRegs.size(); i < e; i++) {
    GISelAsmOperandInfo &OpInfo = OutputOperands[i];

    if (OpInfo.Regs.empty())
      continue;

    switch (OpInfo.ConstraintType) {
    case TargetLowering::C_Register:
    case TargetLowering::C_RegisterClass: {
      if (OpInfo.Regs.size() > 1) {
        LLVM_DEBUG(dbgs() << "Output operands with multiple defining "
                             "registers are not supported yet\n");
        return false;
      }

      Register SrcReg = OpInfo.Regs[0];
      unsigned SrcSize = TRI->getRegSizeInBits(SrcReg, *MRI);
      LLT ResTy = MRI->getType(ResRegs[i]);
      if (ResTy.isScalar() && ResTy.getSizeInBits() < SrcSize) {
        // First copy the non-typed virtual register into a generic virtual
        // register
        Register Tmp1Reg =
            MRI->createGenericVirtualRegister(LLT::scalar(SrcSize));
        MIRBuilder.buildCopy(Tmp1Reg, SrcReg);
        // Need to truncate the result of the register
        MIRBuilder.buildTrunc(ResRegs[i], Tmp1Reg);
      } else if (ResTy.getSizeInBits() == SrcSize) {
        MIRBuilder.buildCopy(ResRegs[i], SrcReg);
      } else {
        LLVM_DEBUG(dbgs() << "Unhandled output operand with "
                             "mismatched register size\n");
        return false;
      }

      break;
    }
    case TargetLowering::C_Immediate:
    case TargetLowering::C_Other:
      LLVM_DEBUG(
          dbgs() << "Cannot lower target specific output constraints yet\n");
      return false;
    case TargetLowering::C_Memory:
      break; // Already handled.
    case TargetLowering::C_Address:
      break; // Silence warning.
    case TargetLowering::C_Unknown:
      LLVM_DEBUG(dbgs() << "Unexpected unknown constraint\n");
      return false;
    }
  }

  return true;
}

bool InlineAsmLowering::lowerAsmOperandForConstraint(
    Value *Val, StringRef Constraint, std::vector<MachineOperand> &Ops,
    MachineIRBuilder &MIRBuilder) const {
  if (Constraint.size() > 1)
    return false;

  char ConstraintLetter = Constraint[0];
  switch (ConstraintLetter) {
  default:
    return false;
  case 'i': // Simple Integer or Relocatable Constant
  case 'n': // immediate integer with a known value.
    if (ConstantInt *CI = dyn_cast<ConstantInt>(Val)) {
      assert(CI->getBitWidth() <= 64 &&
             "expected immediate to fit into 64-bits");
      // Boolean constants should be zero-extended, others are sign-extended
      bool IsBool = CI->getBitWidth() == 1;
      int64_t ExtVal = IsBool ? CI->getZExtValue() : CI->getSExtValue();
      Ops.push_back(MachineOperand::CreateImm(ExtVal));
      return true;
    }
    return false;
  }
}
