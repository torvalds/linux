//===- MipsISelLowering.h - Mips DAG Lowering Interface ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Mips uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSISELLOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPSISELLOWERING_H

#include "MCTargetDesc/MipsABIInfo.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "Mips.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Type.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <deque>
#include <utility>
#include <vector>

namespace llvm {

class Argument;
class FastISel;
class FunctionLoweringInfo;
class MachineBasicBlock;
class MachineFrameInfo;
class MachineInstr;
class MipsCCState;
class MipsFunctionInfo;
class MipsSubtarget;
class MipsTargetMachine;
class TargetLibraryInfo;
class TargetRegisterClass;

  namespace MipsISD {

    enum NodeType : unsigned {
      // Start the numbering from where ISD NodeType finishes.
      FIRST_NUMBER = ISD::BUILTIN_OP_END,

      // Jump and link (call)
      JmpLink,

      // Tail call
      TailCall,

      // Get the Highest (63-48) 16 bits from a 64-bit immediate
      Highest,

      // Get the Higher (47-32) 16 bits from a 64-bit immediate
      Higher,

      // Get the High 16 bits from a 32/64-bit immediate
      // No relation with Mips Hi register
      Hi,

      // Get the Lower 16 bits from a 32/64-bit immediate
      // No relation with Mips Lo register
      Lo,

      // Get the High 16 bits from a 32 bit immediate for accessing the GOT.
      GotHi,

      // Get the High 16 bits from a 32-bit immediate for accessing TLS.
      TlsHi,

      // Handle gp_rel (small data/bss sections) relocation.
      GPRel,

      // Thread Pointer
      ThreadPointer,

      // Vector Floating Point Multiply and Subtract
      FMS,

      // Floating Point Branch Conditional
      FPBrcond,

      // Floating Point Compare
      FPCmp,

      // Floating point Abs
      FAbs,

      // Floating point select
      FSELECT,

      // Node used to generate an MTC1 i32 to f64 instruction
      MTC1_D64,

      // Floating Point Conditional Moves
      CMovFP_T,
      CMovFP_F,

      // FP-to-int truncation node.
      TruncIntFP,

      // Return
      Ret,

      // Interrupt, exception, error trap Return
      ERet,

      // Software Exception Return.
      EH_RETURN,

      // Node used to extract integer from accumulator.
      MFHI,
      MFLO,

      // Node used to insert integers to accumulator.
      MTLOHI,

      // Mult nodes.
      Mult,
      Multu,

      // MAdd/Sub nodes
      MAdd,
      MAddu,
      MSub,
      MSubu,

      // DivRem(u)
      DivRem,
      DivRemU,
      DivRem16,
      DivRemU16,

      BuildPairF64,
      ExtractElementF64,

      Wrapper,

      DynAlloc,

      Sync,

      Ext,
      Ins,
      CIns,

      // EXTR.W intrinsic nodes.
      EXTP,
      EXTPDP,
      EXTR_S_H,
      EXTR_W,
      EXTR_R_W,
      EXTR_RS_W,
      SHILO,
      MTHLIP,

      // DPA.W intrinsic nodes.
      MULSAQ_S_W_PH,
      MAQ_S_W_PHL,
      MAQ_S_W_PHR,
      MAQ_SA_W_PHL,
      MAQ_SA_W_PHR,
      DPAU_H_QBL,
      DPAU_H_QBR,
      DPSU_H_QBL,
      DPSU_H_QBR,
      DPAQ_S_W_PH,
      DPSQ_S_W_PH,
      DPAQ_SA_L_W,
      DPSQ_SA_L_W,
      DPA_W_PH,
      DPS_W_PH,
      DPAQX_S_W_PH,
      DPAQX_SA_W_PH,
      DPAX_W_PH,
      DPSX_W_PH,
      DPSQX_S_W_PH,
      DPSQX_SA_W_PH,
      MULSA_W_PH,

      MULT,
      MULTU,
      MADD_DSP,
      MADDU_DSP,
      MSUB_DSP,
      MSUBU_DSP,

      // DSP shift nodes.
      SHLL_DSP,
      SHRA_DSP,
      SHRL_DSP,

      // DSP setcc and select_cc nodes.
      SETCC_DSP,
      SELECT_CC_DSP,

      // Vector comparisons.
      // These take a vector and return a boolean.
      VALL_ZERO,
      VANY_ZERO,
      VALL_NONZERO,
      VANY_NONZERO,

      // These take a vector and return a vector bitmask.
      VCEQ,
      VCLE_S,
      VCLE_U,
      VCLT_S,
      VCLT_U,

      // Vector Shuffle with mask as an operand
      VSHF,  // Generic shuffle
      SHF,   // 4-element set shuffle.
      ILVEV, // Interleave even elements
      ILVOD, // Interleave odd elements
      ILVL,  // Interleave left elements
      ILVR,  // Interleave right elements
      PCKEV, // Pack even elements
      PCKOD, // Pack odd elements

      // Vector Lane Copy
      INSVE, // Copy element from one vector to another

      // Combined (XOR (OR $a, $b), -1)
      VNOR,

      // Extended vector element extraction
      VEXTRACT_SEXT_ELT,
      VEXTRACT_ZEXT_ELT,

      // Double select nodes for machines without conditional-move.
      DOUBLE_SELECT_I,
      DOUBLE_SELECT_I64,

      // Load/Store Left/Right nodes.
      LWL = ISD::FIRST_TARGET_MEMORY_OPCODE,
      LWR,
      SWL,
      SWR,
      LDL,
      LDR,
      SDL,
      SDR
    };

  } // ene namespace MipsISD

  //===--------------------------------------------------------------------===//
  // TargetLowering Implementation
  //===--------------------------------------------------------------------===//

  class MipsTargetLowering : public TargetLowering  {
    bool isMicroMips;

  public:
    explicit MipsTargetLowering(const MipsTargetMachine &TM,
                                const MipsSubtarget &STI);

    static const MipsTargetLowering *create(const MipsTargetMachine &TM,
                                            const MipsSubtarget &STI);

    /// createFastISel - This method returns a target specific FastISel object,
    /// or null if the target does not support "fast" ISel.
    FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                             const TargetLibraryInfo *libInfo) const override;

    MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override {
      return MVT::i32;
    }

    EVT getTypeForExtReturn(LLVMContext &Context, EVT VT,
                            ISD::NodeType) const override;

    bool isCheapToSpeculateCttz(Type *Ty) const override;
    bool isCheapToSpeculateCtlz(Type *Ty) const override;
    bool hasBitTest(SDValue X, SDValue Y) const override;
    bool shouldFoldConstantShiftPairToMask(const SDNode *N,
                                           CombineLevel Level) const override;

    /// Return the register type for a given MVT, ensuring vectors are treated
    /// as a series of gpr sized integers.
    MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                      EVT VT) const override;

    /// Return the number of registers for a given MVT, ensuring vectors are
    /// treated as a series of gpr sized integers.
    unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                           CallingConv::ID CC,
                                           EVT VT) const override;

    /// Break down vectors to the correct number of gpr sized integers.
    unsigned getVectorTypeBreakdownForCallingConv(
        LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
        unsigned &NumIntermediates, MVT &RegisterVT) const override;

    /// Return the correct alignment for the current calling convention.
    Align getABIAlignmentForCallingConv(Type *ArgTy,
                                        const DataLayout &DL) const override {
      const Align ABIAlign = DL.getABITypeAlign(ArgTy);
      if (ArgTy->isVectorTy())
        return std::min(ABIAlign, Align(8));
      return ABIAlign;
    }

    ISD::NodeType getExtendForAtomicOps() const override {
      return ISD::SIGN_EXTEND;
    }

    /// LowerOperation - Provide custom lowering hooks for some operations.
    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    /// ReplaceNodeResults - Replace the results of node with an illegal result
    /// type with new values built out of custom code.
    ///
    void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue>&Results,
                            SelectionDAG &DAG) const override;

    /// getTargetNodeName - This method returns the name of a target specific
    //  DAG node.
    const char *getTargetNodeName(unsigned Opcode) const override;

    /// getSetCCResultType - get the ISD::SETCC result ValueType
    EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                           EVT VT) const override;

    SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

    MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr &MI,
                                MachineBasicBlock *MBB) const override;

    void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                       SDNode *Node) const override;

    void HandleByVal(CCState *, unsigned &, Align) const override;

    Register getRegisterByName(const char* RegName, LLT VT,
                               const MachineFunction &MF) const override;

    /// If a physical register, this returns the register that receives the
    /// exception address on entry to an EH pad.
    Register
    getExceptionPointerRegister(const Constant *PersonalityFn) const override {
      return ABI.IsN64() ? Mips::A0_64 : Mips::A0;
    }

    /// If a physical register, this returns the register that receives the
    /// exception typeid on entry to a landing pad.
    Register
    getExceptionSelectorRegister(const Constant *PersonalityFn) const override {
      return ABI.IsN64() ? Mips::A1_64 : Mips::A1;
    }

    bool isJumpTableRelative() const override {
      return getTargetMachine().isPositionIndependent();
    }

   CCAssignFn *CCAssignFnForCall() const;

   CCAssignFn *CCAssignFnForReturn() const;

  protected:
    SDValue getGlobalReg(SelectionDAG &DAG, EVT Ty) const;

    // This method creates the following nodes, which are necessary for
    // computing a local symbol's address:
    //
    // (add (load (wrapper $gp, %got(sym)), %lo(sym))
    template <class NodeTy>
    SDValue getAddrLocal(NodeTy *N, const SDLoc &DL, EVT Ty, SelectionDAG &DAG,
                         bool IsN32OrN64) const {
      unsigned GOTFlag = IsN32OrN64 ? MipsII::MO_GOT_PAGE : MipsII::MO_GOT;
      SDValue GOT = DAG.getNode(MipsISD::Wrapper, DL, Ty, getGlobalReg(DAG, Ty),
                                getTargetNode(N, Ty, DAG, GOTFlag));
      SDValue Load =
          DAG.getLoad(Ty, DL, DAG.getEntryNode(), GOT,
                      MachinePointerInfo::getGOT(DAG.getMachineFunction()));
      unsigned LoFlag = IsN32OrN64 ? MipsII::MO_GOT_OFST : MipsII::MO_ABS_LO;
      SDValue Lo = DAG.getNode(MipsISD::Lo, DL, Ty,
                               getTargetNode(N, Ty, DAG, LoFlag));
      return DAG.getNode(ISD::ADD, DL, Ty, Load, Lo);
    }

    // This method creates the following nodes, which are necessary for
    // computing a global symbol's address:
    //
    // (load (wrapper $gp, %got(sym)))
    template <class NodeTy>
    SDValue getAddrGlobal(NodeTy *N, const SDLoc &DL, EVT Ty, SelectionDAG &DAG,
                          unsigned Flag, SDValue Chain,
                          const MachinePointerInfo &PtrInfo) const {
      SDValue Tgt = DAG.getNode(MipsISD::Wrapper, DL, Ty, getGlobalReg(DAG, Ty),
                                getTargetNode(N, Ty, DAG, Flag));
      return DAG.getLoad(Ty, DL, Chain, Tgt, PtrInfo);
    }

    // This method creates the following nodes, which are necessary for
    // computing a global symbol's address in large-GOT mode:
    //
    // (load (wrapper (add %hi(sym), $gp), %lo(sym)))
    template <class NodeTy>
    SDValue getAddrGlobalLargeGOT(NodeTy *N, const SDLoc &DL, EVT Ty,
                                  SelectionDAG &DAG, unsigned HiFlag,
                                  unsigned LoFlag, SDValue Chain,
                                  const MachinePointerInfo &PtrInfo) const {
      SDValue Hi = DAG.getNode(MipsISD::GotHi, DL, Ty,
                               getTargetNode(N, Ty, DAG, HiFlag));
      Hi = DAG.getNode(ISD::ADD, DL, Ty, Hi, getGlobalReg(DAG, Ty));
      SDValue Wrapper = DAG.getNode(MipsISD::Wrapper, DL, Ty, Hi,
                                    getTargetNode(N, Ty, DAG, LoFlag));
      return DAG.getLoad(Ty, DL, Chain, Wrapper, PtrInfo);
    }

    // This method creates the following nodes, which are necessary for
    // computing a symbol's address in non-PIC mode:
    //
    // (add %hi(sym), %lo(sym))
    //
    // This method covers O32, N32 and N64 in sym32 mode.
    template <class NodeTy>
    SDValue getAddrNonPIC(NodeTy *N, const SDLoc &DL, EVT Ty,
                          SelectionDAG &DAG) const {
      SDValue Hi = getTargetNode(N, Ty, DAG, MipsII::MO_ABS_HI);
      SDValue Lo = getTargetNode(N, Ty, DAG, MipsII::MO_ABS_LO);
      return DAG.getNode(ISD::ADD, DL, Ty,
                         DAG.getNode(MipsISD::Hi, DL, Ty, Hi),
                         DAG.getNode(MipsISD::Lo, DL, Ty, Lo));
   }

   // This method creates the following nodes, which are necessary for
   // computing a symbol's address in non-PIC mode for N64.
   //
   // (add (shl (add (shl (add %highest(sym), %higher(sim)), 16), %high(sym)),
   //            16), %lo(%sym))
   //
   // FIXME: This method is not efficent for (micro)MIPS64R6.
   template <class NodeTy>
   SDValue getAddrNonPICSym64(NodeTy *N, const SDLoc &DL, EVT Ty,
                          SelectionDAG &DAG) const {
      SDValue Hi = getTargetNode(N, Ty, DAG, MipsII::MO_ABS_HI);
      SDValue Lo = getTargetNode(N, Ty, DAG, MipsII::MO_ABS_LO);

      SDValue Highest =
          DAG.getNode(MipsISD::Highest, DL, Ty,
                      getTargetNode(N, Ty, DAG, MipsII::MO_HIGHEST));
      SDValue Higher = getTargetNode(N, Ty, DAG, MipsII::MO_HIGHER);
      SDValue HigherPart =
          DAG.getNode(ISD::ADD, DL, Ty, Highest,
                      DAG.getNode(MipsISD::Higher, DL, Ty, Higher));
      SDValue Cst = DAG.getConstant(16, DL, MVT::i32);
      SDValue Shift = DAG.getNode(ISD::SHL, DL, Ty, HigherPart, Cst);
      SDValue Add = DAG.getNode(ISD::ADD, DL, Ty, Shift,
                                DAG.getNode(MipsISD::Hi, DL, Ty, Hi));
      SDValue Shift2 = DAG.getNode(ISD::SHL, DL, Ty, Add, Cst);

      return DAG.getNode(ISD::ADD, DL, Ty, Shift2,
                         DAG.getNode(MipsISD::Lo, DL, Ty, Lo));
   }

    // This method creates the following nodes, which are necessary for
    // computing a symbol's address using gp-relative addressing:
    //
    // (add $gp, %gp_rel(sym))
    template <class NodeTy>
    SDValue getAddrGPRel(NodeTy *N, const SDLoc &DL, EVT Ty,
                         SelectionDAG &DAG, bool IsN64) const {
      SDValue GPRel = getTargetNode(N, Ty, DAG, MipsII::MO_GPREL);
      return DAG.getNode(
          ISD::ADD, DL, Ty,
          DAG.getRegister(IsN64 ? Mips::GP_64 : Mips::GP, Ty),
          DAG.getNode(MipsISD::GPRel, DL, DAG.getVTList(Ty), GPRel));
    }

    /// This function fills Ops, which is the list of operands that will later
    /// be used when a function call node is created. It also generates
    /// copyToReg nodes to set up argument registers.
    virtual void
    getOpndList(SmallVectorImpl<SDValue> &Ops,
                std::deque<std::pair<unsigned, SDValue>> &RegsToPass,
                bool IsPICCall, bool GlobalOrExternal, bool InternalLinkage,
                bool IsCallReloc, CallLoweringInfo &CLI, SDValue Callee,
                SDValue Chain) const;

  protected:
    SDValue lowerLOAD(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerSTORE(SDValue Op, SelectionDAG &DAG) const;

    // Subtarget Info
    const MipsSubtarget &Subtarget;
    // Cache the ABI from the TargetMachine, we use it everywhere.
    const MipsABIInfo &ABI;

  private:
    // Create a TargetGlobalAddress node.
    SDValue getTargetNode(GlobalAddressSDNode *N, EVT Ty, SelectionDAG &DAG,
                          unsigned Flag) const;

    // Create a TargetExternalSymbol node.
    SDValue getTargetNode(ExternalSymbolSDNode *N, EVT Ty, SelectionDAG &DAG,
                          unsigned Flag) const;

    // Create a TargetBlockAddress node.
    SDValue getTargetNode(BlockAddressSDNode *N, EVT Ty, SelectionDAG &DAG,
                          unsigned Flag) const;

    // Create a TargetJumpTable node.
    SDValue getTargetNode(JumpTableSDNode *N, EVT Ty, SelectionDAG &DAG,
                          unsigned Flag) const;

    // Create a TargetConstantPool node.
    SDValue getTargetNode(ConstantPoolSDNode *N, EVT Ty, SelectionDAG &DAG,
                          unsigned Flag) const;

    // Lower Operand helpers
    SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            const SDLoc &dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals,
                            TargetLowering::CallLoweringInfo &CLI) const;

    // Lower Operand specifics
    SDValue lowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerSELECT(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerSETCC(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerVAARG(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerFABS(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerFABS32(SDValue Op, SelectionDAG &DAG,
                        bool HasExtractInsert) const;
    SDValue lowerFABS64(SDValue Op, SelectionDAG &DAG,
                        bool HasExtractInsert) const;
    SDValue lowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerEH_RETURN(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerATOMIC_FENCE(SDValue Op, SelectionDAG& DAG) const;
    SDValue lowerShiftLeftParts(SDValue Op, SelectionDAG& DAG) const;
    SDValue lowerShiftRightParts(SDValue Op, SelectionDAG& DAG,
                                 bool IsSRA) const;
    SDValue lowerEH_DWARF_CFA(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerFP_TO_SINT(SDValue Op, SelectionDAG &DAG) const;

    /// isEligibleForTailCallOptimization - Check whether the call is eligible
    /// for tail call optimization.
    virtual bool
    isEligibleForTailCallOptimization(const CCState &CCInfo,
                                      unsigned NextStackOffset,
                                      const MipsFunctionInfo &FI) const = 0;

    /// copyByValArg - Copy argument registers which were used to pass a byval
    /// argument to the stack. Create a stack frame object for the byval
    /// argument.
    void copyByValRegs(SDValue Chain, const SDLoc &DL,
                       std::vector<SDValue> &OutChains, SelectionDAG &DAG,
                       const ISD::ArgFlagsTy &Flags,
                       SmallVectorImpl<SDValue> &InVals,
                       const Argument *FuncArg, unsigned FirstReg,
                       unsigned LastReg, const CCValAssign &VA,
                       MipsCCState &State) const;

    /// passByValArg - Pass a byval argument in registers or on stack.
    void passByValArg(SDValue Chain, const SDLoc &DL,
                      std::deque<std::pair<unsigned, SDValue>> &RegsToPass,
                      SmallVectorImpl<SDValue> &MemOpChains, SDValue StackPtr,
                      MachineFrameInfo &MFI, SelectionDAG &DAG, SDValue Arg,
                      unsigned FirstReg, unsigned LastReg,
                      const ISD::ArgFlagsTy &Flags, bool isLittle,
                      const CCValAssign &VA) const;

    /// writeVarArgRegs - Write variable function arguments passed in registers
    /// to the stack. Also create a stack frame object for the first variable
    /// argument.
    void writeVarArgRegs(std::vector<SDValue> &OutChains, SDValue Chain,
                         const SDLoc &DL, SelectionDAG &DAG,
                         CCState &State) const;

    SDValue
    LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                         const SmallVectorImpl<ISD::InputArg> &Ins,
                         const SDLoc &dl, SelectionDAG &DAG,
                         SmallVectorImpl<SDValue> &InVals) const override;

    SDValue passArgOnStack(SDValue StackPtr, unsigned Offset, SDValue Chain,
                           SDValue Arg, const SDLoc &DL, bool IsTailCall,
                           SelectionDAG &DAG) const;

    SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                      SmallVectorImpl<SDValue> &InVals) const override;

    bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                        bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        LLVMContext &Context) const override;

    SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        const SmallVectorImpl<SDValue> &OutVals,
                        const SDLoc &dl, SelectionDAG &DAG) const override;

    SDValue LowerInterruptReturn(SmallVectorImpl<SDValue> &RetOps,
                                 const SDLoc &DL, SelectionDAG &DAG) const;

    bool shouldSignExtendTypeInLibCall(EVT Type, bool IsSigned) const override;

    // Inline asm support
    ConstraintType getConstraintType(StringRef Constraint) const override;

    /// Examine constraint string and operand type and determine a weight value.
    /// The operand object must already have been set up with the operand type.
    ConstraintWeight getSingleConstraintMatchWeight(
      AsmOperandInfo &info, const char *constraint) const override;

    /// This function parses registers that appear in inline-asm constraints.
    /// It returns pair (0, 0) on failure.
    std::pair<unsigned, const TargetRegisterClass *>
    parseRegForInlineAsmConstraint(StringRef C, MVT VT) const;

    std::pair<unsigned, const TargetRegisterClass *>
    getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                 StringRef Constraint, MVT VT) const override;

    /// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
    /// vector.  If it is invalid, don't add anything to Ops. If hasMemory is
    /// true it means one of the asm constraint of the inline asm instruction
    /// being processed is 'm'.
    void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                      std::vector<SDValue> &Ops,
                                      SelectionDAG &DAG) const override;

    InlineAsm::ConstraintCode
    getInlineAsmMemConstraint(StringRef ConstraintCode) const override {
      if (ConstraintCode == "o")
        return InlineAsm::ConstraintCode::o;
      if (ConstraintCode == "R")
        return InlineAsm::ConstraintCode::R;
      if (ConstraintCode == "ZC")
        return InlineAsm::ConstraintCode::ZC;
      return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
    }

    bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                               Type *Ty, unsigned AS,
                               Instruction *I = nullptr) const override;

    bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

    EVT getOptimalMemOpType(const MemOp &Op,
                            const AttributeList &FuncAttributes) const override;

    /// isFPImmLegal - Returns true if the target can instruction select the
    /// specified FP immediate natively. If false, the legalizer will
    /// materialize the FP immediate as a load from a constant pool.
    bool isFPImmLegal(const APFloat &Imm, EVT VT,
                      bool ForCodeSize) const override;

    unsigned getJumpTableEncoding() const override;
    bool useSoftFloat() const override;

    bool shouldInsertFencesForAtomic(const Instruction *I) const override {
      return true;
    }

    /// Emit a sign-extension using sll/sra, seb, or seh appropriately.
    MachineBasicBlock *emitSignExtendToI32InReg(MachineInstr &MI,
                                                MachineBasicBlock *BB,
                                                unsigned Size, unsigned DstReg,
                                                unsigned SrcRec) const;

    MachineBasicBlock *emitAtomicBinary(MachineInstr &MI,
                                        MachineBasicBlock *BB) const;
    MachineBasicBlock *emitAtomicBinaryPartword(MachineInstr &MI,
                                                MachineBasicBlock *BB,
                                                unsigned Size) const;
    MachineBasicBlock *emitAtomicCmpSwap(MachineInstr &MI,
                                         MachineBasicBlock *BB) const;
    MachineBasicBlock *emitAtomicCmpSwapPartword(MachineInstr &MI,
                                                 MachineBasicBlock *BB,
                                                 unsigned Size) const;
    MachineBasicBlock *emitSEL_D(MachineInstr &MI, MachineBasicBlock *BB) const;
    MachineBasicBlock *emitPseudoSELECT(MachineInstr &MI, MachineBasicBlock *BB,
                                        bool isFPCmp, unsigned Opc) const;
    MachineBasicBlock *emitPseudoD_SELECT(MachineInstr &MI,
                                          MachineBasicBlock *BB) const;
    MachineBasicBlock *emitLDR_W(MachineInstr &MI, MachineBasicBlock *BB) const;
    MachineBasicBlock *emitLDR_D(MachineInstr &MI, MachineBasicBlock *BB) const;
    MachineBasicBlock *emitSTR_W(MachineInstr &MI, MachineBasicBlock *BB) const;
    MachineBasicBlock *emitSTR_D(MachineInstr &MI, MachineBasicBlock *BB) const;
  };

  /// Create MipsTargetLowering objects.
  const MipsTargetLowering *
  createMips16TargetLowering(const MipsTargetMachine &TM,
                             const MipsSubtarget &STI);
  const MipsTargetLowering *
  createMipsSETargetLowering(const MipsTargetMachine &TM,
                             const MipsSubtarget &STI);

namespace Mips {

FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                         const TargetLibraryInfo *libInfo);

} // end namespace Mips

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSISELLOWERING_H
