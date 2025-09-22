//===-- R600ISelLowering.cpp - R600 DAG Lowering Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Custom DAG lowering for R600
//
//===----------------------------------------------------------------------===//

#include "R600ISelLowering.h"
#include "AMDGPU.h"
#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600Defines.h"
#include "R600InstrInfo.h"
#include "R600MachineFunctionInfo.h"
#include "R600Subtarget.h"
#include "R600TargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsR600.h"

using namespace llvm;

#include "R600GenCallingConv.inc"

R600TargetLowering::R600TargetLowering(const TargetMachine &TM,
                                       const R600Subtarget &STI)
    : AMDGPUTargetLowering(TM, STI), Subtarget(&STI), Gen(STI.getGeneration()) {
  addRegisterClass(MVT::f32, &R600::R600_Reg32RegClass);
  addRegisterClass(MVT::i32, &R600::R600_Reg32RegClass);
  addRegisterClass(MVT::v2f32, &R600::R600_Reg64RegClass);
  addRegisterClass(MVT::v2i32, &R600::R600_Reg64RegClass);
  addRegisterClass(MVT::v4f32, &R600::R600_Reg128RegClass);
  addRegisterClass(MVT::v4i32, &R600::R600_Reg128RegClass);

  setBooleanContents(ZeroOrNegativeOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  computeRegisterProperties(Subtarget->getRegisterInfo());

  // Legalize loads and stores to the private address space.
  setOperationAction(ISD::LOAD, {MVT::i32, MVT::v2i32, MVT::v4i32}, Custom);

  // EXTLOAD should be the same as ZEXTLOAD. It is legal for some address
  // spaces, so it is custom lowered to handle those where it isn't.
  for (auto Op : {ISD::SEXTLOAD, ISD::ZEXTLOAD, ISD::EXTLOAD})
    for (MVT VT : MVT::integer_valuetypes()) {
      setLoadExtAction(Op, VT, MVT::i1, Promote);
      setLoadExtAction(Op, VT, MVT::i8, Custom);
      setLoadExtAction(Op, VT, MVT::i16, Custom);
    }

  // Workaround for LegalizeDAG asserting on expansion of i1 vector loads.
  setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, MVT::v2i32,
                   MVT::v2i1, Expand);

  setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, MVT::v4i32,
                   MVT::v4i1, Expand);

  setOperationAction(ISD::STORE, {MVT::i8, MVT::i32, MVT::v2i32, MVT::v4i32},
                     Custom);

  setTruncStoreAction(MVT::i32, MVT::i8, Custom);
  setTruncStoreAction(MVT::i32, MVT::i16, Custom);
  // We need to include these since trunc STORES to PRIVATE need
  // special handling to accommodate RMW
  setTruncStoreAction(MVT::v2i32, MVT::v2i16, Custom);
  setTruncStoreAction(MVT::v4i32, MVT::v4i16, Custom);
  setTruncStoreAction(MVT::v8i32, MVT::v8i16, Custom);
  setTruncStoreAction(MVT::v16i32, MVT::v16i16, Custom);
  setTruncStoreAction(MVT::v32i32, MVT::v32i16, Custom);
  setTruncStoreAction(MVT::v2i32, MVT::v2i8, Custom);
  setTruncStoreAction(MVT::v4i32, MVT::v4i8, Custom);
  setTruncStoreAction(MVT::v8i32, MVT::v8i8, Custom);
  setTruncStoreAction(MVT::v16i32, MVT::v16i8, Custom);
  setTruncStoreAction(MVT::v32i32, MVT::v32i8, Custom);

  // Workaround for LegalizeDAG asserting on expansion of i1 vector stores.
  setTruncStoreAction(MVT::v2i32, MVT::v2i1, Expand);
  setTruncStoreAction(MVT::v4i32, MVT::v4i1, Expand);

  // Set condition code actions
  setCondCodeAction({ISD::SETO, ISD::SETUO, ISD::SETLT, ISD::SETLE, ISD::SETOLT,
                     ISD::SETOLE, ISD::SETONE, ISD::SETUEQ, ISD::SETUGE,
                     ISD::SETUGT, ISD::SETULT, ISD::SETULE},
                    MVT::f32, Expand);

  setCondCodeAction({ISD::SETLE, ISD::SETLT, ISD::SETULE, ISD::SETULT},
                    MVT::i32, Expand);

  setOperationAction({ISD::FCOS, ISD::FSIN}, MVT::f32, Custom);

  setOperationAction(ISD::SETCC, {MVT::v4i32, MVT::v2i32}, Expand);

  setOperationAction(ISD::BR_CC, {MVT::i32, MVT::f32}, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);

  setOperationAction(ISD::FSUB, MVT::f32, Expand);

  setOperationAction({ISD::FCEIL, ISD::FTRUNC, ISD::FROUNDEVEN, ISD::FFLOOR},
                     MVT::f64, Custom);

  setOperationAction(ISD::SELECT_CC, {MVT::f32, MVT::i32}, Custom);

  setOperationAction(ISD::SETCC, {MVT::i32, MVT::f32}, Expand);
  setOperationAction({ISD::FP_TO_UINT, ISD::FP_TO_SINT}, {MVT::i1, MVT::i64},
                     Custom);

  setOperationAction(ISD::SELECT, {MVT::i32, MVT::f32, MVT::v2i32, MVT::v4i32},
                     Expand);

  // ADD, SUB overflow.
  // TODO: turn these into Legal?
  if (Subtarget->hasCARRY())
    setOperationAction(ISD::UADDO, MVT::i32, Custom);

  if (Subtarget->hasBORROW())
    setOperationAction(ISD::USUBO, MVT::i32, Custom);

  // Expand sign extension of vectors
  if (!Subtarget->hasBFE())
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, {MVT::v2i1, MVT::v4i1}, Expand);

  if (!Subtarget->hasBFE())
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, {MVT::v2i8, MVT::v4i8}, Expand);

  if (!Subtarget->hasBFE())
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, {MVT::v2i16, MVT::v4i16}, Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, {MVT::v2i32, MVT::v4i32}, Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::Other, Expand);

  setOperationAction(ISD::FrameIndex, MVT::i32, Custom);

  setOperationAction(ISD::EXTRACT_VECTOR_ELT,
                     {MVT::v2i32, MVT::v2f32, MVT::v4i32, MVT::v4f32}, Custom);

  setOperationAction(ISD::INSERT_VECTOR_ELT,
                     {MVT::v2i32, MVT::v2f32, MVT::v4i32, MVT::v4f32}, Custom);

  // We don't have 64-bit shifts. Thus we need either SHX i64 or SHX_PARTS i32
  //  to be Legal/Custom in order to avoid library calls.
  setOperationAction({ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS}, MVT::i32,
                     Custom);

  if (!Subtarget->hasFMA())
    setOperationAction(ISD::FMA, {MVT::f32, MVT::f64}, Expand);

  // FIXME: May need no denormals check
  setOperationAction(ISD::FMAD, MVT::f32, Legal);

  if (!Subtarget->hasBFI())
    // fcopysign can be done in a single instruction with BFI.
    setOperationAction(ISD::FCOPYSIGN, {MVT::f32, MVT::f64}, Expand);

  if (!Subtarget->hasBCNT(32))
    setOperationAction(ISD::CTPOP, MVT::i32, Expand);

  if (!Subtarget->hasBCNT(64))
    setOperationAction(ISD::CTPOP, MVT::i64, Expand);

  if (Subtarget->hasFFBH())
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, Custom);

  if (Subtarget->hasFFBL())
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i32, Custom);

  // FIXME: This was moved from AMDGPUTargetLowering, I'm not sure if we
  // need it for R600.
  if (Subtarget->hasBFE())
    setHasExtractBitsInsn(true);

  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::ADDRSPACECAST, MVT::i32, Custom);

  const MVT ScalarIntVTs[] = { MVT::i32, MVT::i64 };
  for (MVT VT : ScalarIntVTs)
    setOperationAction({ISD::ADDC, ISD::SUBC, ISD::ADDE, ISD::SUBE}, VT,
                       Expand);

  // LLVM will expand these to atomic_cmp_swap(0)
  // and atomic_swap, respectively.
  setOperationAction({ISD::ATOMIC_LOAD, ISD::ATOMIC_STORE}, MVT::i32, Expand);

  // We need to custom lower some of the intrinsics
  setOperationAction({ISD::INTRINSIC_VOID, ISD::INTRINSIC_WO_CHAIN}, MVT::Other,
                     Custom);

  setSchedulingPreference(Sched::Source);

  setTargetDAGCombine({ISD::FP_ROUND, ISD::FP_TO_SINT, ISD::EXTRACT_VECTOR_ELT,
                       ISD::SELECT_CC, ISD::INSERT_VECTOR_ELT, ISD::LOAD});
}

static inline bool isEOP(MachineBasicBlock::iterator I) {
  if (std::next(I) == I->getParent()->end())
    return false;
  return std::next(I)->getOpcode() == R600::RETURN;
}

MachineBasicBlock *
R600TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  MachineFunction *MF = BB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  MachineBasicBlock::iterator I = MI;
  const R600InstrInfo *TII = Subtarget->getInstrInfo();

  switch (MI.getOpcode()) {
  default:
    // Replace LDS_*_RET instruction that don't have any uses with the
    // equivalent LDS_*_NORET instruction.
    if (TII->isLDSRetInstr(MI.getOpcode())) {
      int DstIdx = TII->getOperandIdx(MI.getOpcode(), R600::OpName::dst);
      assert(DstIdx != -1);
      MachineInstrBuilder NewMI;
      // FIXME: getLDSNoRetOp method only handles LDS_1A1D LDS ops. Add
      //        LDS_1A2D support and remove this special case.
      if (!MRI.use_empty(MI.getOperand(DstIdx).getReg()) ||
          MI.getOpcode() == R600::LDS_CMPST_RET)
        return BB;

      NewMI = BuildMI(*BB, I, BB->findDebugLoc(I),
                      TII->get(R600::getLDSNoRetOp(MI.getOpcode())));
      for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
        NewMI.add(MO);
    } else {
      return AMDGPUTargetLowering::EmitInstrWithCustomInserter(MI, BB);
    }
    break;

  case R600::FABS_R600: {
    MachineInstr *NewMI = TII->buildDefaultInstruction(
        *BB, I, R600::MOV, MI.getOperand(0).getReg(),
        MI.getOperand(1).getReg());
    TII->addFlag(*NewMI, 0, MO_FLAG_ABS);
    break;
  }

  case R600::FNEG_R600: {
    MachineInstr *NewMI = TII->buildDefaultInstruction(
        *BB, I, R600::MOV, MI.getOperand(0).getReg(),
        MI.getOperand(1).getReg());
    TII->addFlag(*NewMI, 0, MO_FLAG_NEG);
    break;
  }

  case R600::MASK_WRITE: {
    Register maskedRegister = MI.getOperand(0).getReg();
    assert(maskedRegister.isVirtual());
    MachineInstr * defInstr = MRI.getVRegDef(maskedRegister);
    TII->addFlag(*defInstr, 0, MO_FLAG_MASK);
    break;
  }

  case R600::MOV_IMM_F32:
    TII->buildMovImm(*BB, I, MI.getOperand(0).getReg(), MI.getOperand(1)
                                                            .getFPImm()
                                                            ->getValueAPF()
                                                            .bitcastToAPInt()
                                                            .getZExtValue());
    break;

  case R600::MOV_IMM_I32:
    TII->buildMovImm(*BB, I, MI.getOperand(0).getReg(),
                     MI.getOperand(1).getImm());
    break;

  case R600::MOV_IMM_GLOBAL_ADDR: {
    //TODO: Perhaps combine this instruction with the next if possible
    auto MIB = TII->buildDefaultInstruction(
        *BB, MI, R600::MOV, MI.getOperand(0).getReg(), R600::ALU_LITERAL_X);
    int Idx = TII->getOperandIdx(*MIB, R600::OpName::literal);
    //TODO: Ugh this is rather ugly
    const MachineOperand &MO = MI.getOperand(1);
    MIB->getOperand(Idx).ChangeToGA(MO.getGlobal(), MO.getOffset(),
                                    MO.getTargetFlags());
    break;
  }

  case R600::CONST_COPY: {
    MachineInstr *NewMI = TII->buildDefaultInstruction(
        *BB, MI, R600::MOV, MI.getOperand(0).getReg(), R600::ALU_CONST);
    TII->setImmOperand(*NewMI, R600::OpName::src0_sel,
                       MI.getOperand(1).getImm());
    break;
  }

  case R600::RAT_WRITE_CACHELESS_32_eg:
  case R600::RAT_WRITE_CACHELESS_64_eg:
  case R600::RAT_WRITE_CACHELESS_128_eg:
    BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(MI.getOpcode()))
        .add(MI.getOperand(0))
        .add(MI.getOperand(1))
        .addImm(isEOP(I)); // Set End of program bit
    break;

  case R600::RAT_STORE_TYPED_eg:
    BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(MI.getOpcode()))
        .add(MI.getOperand(0))
        .add(MI.getOperand(1))
        .add(MI.getOperand(2))
        .addImm(isEOP(I)); // Set End of program bit
    break;

  case R600::BRANCH:
    BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(R600::JUMP))
        .add(MI.getOperand(0));
    break;

  case R600::BRANCH_COND_f32: {
    MachineInstr *NewMI =
        BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(R600::PRED_X),
                R600::PREDICATE_BIT)
            .add(MI.getOperand(1))
            .addImm(R600::PRED_SETNE)
            .addImm(0); // Flags
    TII->addFlag(*NewMI, 0, MO_FLAG_PUSH);
    BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(R600::JUMP_COND))
        .add(MI.getOperand(0))
        .addReg(R600::PREDICATE_BIT, RegState::Kill);
    break;
  }

  case R600::BRANCH_COND_i32: {
    MachineInstr *NewMI =
        BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(R600::PRED_X),
                R600::PREDICATE_BIT)
            .add(MI.getOperand(1))
            .addImm(R600::PRED_SETNE_INT)
            .addImm(0); // Flags
    TII->addFlag(*NewMI, 0, MO_FLAG_PUSH);
    BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(R600::JUMP_COND))
        .add(MI.getOperand(0))
        .addReg(R600::PREDICATE_BIT, RegState::Kill);
    break;
  }

  case R600::EG_ExportSwz:
  case R600::R600_ExportSwz: {
    // Instruction is left unmodified if its not the last one of its type
    bool isLastInstructionOfItsType = true;
    unsigned InstExportType = MI.getOperand(1).getImm();
    for (MachineBasicBlock::iterator NextExportInst = std::next(I),
         EndBlock = BB->end(); NextExportInst != EndBlock;
         NextExportInst = std::next(NextExportInst)) {
      if (NextExportInst->getOpcode() == R600::EG_ExportSwz ||
          NextExportInst->getOpcode() == R600::R600_ExportSwz) {
        unsigned CurrentInstExportType = NextExportInst->getOperand(1)
            .getImm();
        if (CurrentInstExportType == InstExportType) {
          isLastInstructionOfItsType = false;
          break;
        }
      }
    }
    bool EOP = isEOP(I);
    if (!EOP && !isLastInstructionOfItsType)
      return BB;
    unsigned CfInst = (MI.getOpcode() == R600::EG_ExportSwz) ? 84 : 40;
    BuildMI(*BB, I, BB->findDebugLoc(I), TII->get(MI.getOpcode()))
        .add(MI.getOperand(0))
        .add(MI.getOperand(1))
        .add(MI.getOperand(2))
        .add(MI.getOperand(3))
        .add(MI.getOperand(4))
        .add(MI.getOperand(5))
        .add(MI.getOperand(6))
        .addImm(CfInst)
        .addImm(EOP);
    break;
  }
  case R600::RETURN: {
    return BB;
  }
  }

  MI.eraseFromParent();
  return BB;
}

//===----------------------------------------------------------------------===//
// Custom DAG Lowering Operations
//===----------------------------------------------------------------------===//

SDValue R600TargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  R600MachineFunctionInfo *MFI = MF.getInfo<R600MachineFunctionInfo>();
  switch (Op.getOpcode()) {
  default: return AMDGPUTargetLowering::LowerOperation(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT: return LowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::INSERT_VECTOR_ELT: return LowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::SHL_PARTS:
  case ISD::SRA_PARTS:
  case ISD::SRL_PARTS: return LowerShiftParts(Op, DAG);
  case ISD::UADDO: return LowerUADDSUBO(Op, DAG, ISD::ADD, AMDGPUISD::CARRY);
  case ISD::USUBO: return LowerUADDSUBO(Op, DAG, ISD::SUB, AMDGPUISD::BORROW);
  case ISD::FCOS:
  case ISD::FSIN: return LowerTrig(Op, DAG);
  case ISD::SELECT_CC: return LowerSELECT_CC(Op, DAG);
  case ISD::STORE: return LowerSTORE(Op, DAG);
  case ISD::LOAD: {
    SDValue Result = LowerLOAD(Op, DAG);
    assert((!Result.getNode() ||
            Result.getNode()->getNumValues() == 2) &&
           "Load should return a value and a chain");
    return Result;
  }

  case ISD::BRCOND: return LowerBRCOND(Op, DAG);
  case ISD::GlobalAddress: return LowerGlobalAddress(MFI, Op, DAG);
  case ISD::FrameIndex: return lowerFrameIndex(Op, DAG);
  case ISD::ADDRSPACECAST:
    return lowerADDRSPACECAST(Op, DAG);
  case ISD::INTRINSIC_VOID: {
    SDValue Chain = Op.getOperand(0);
    unsigned IntrinsicID = Op.getConstantOperandVal(1);
    switch (IntrinsicID) {
    case Intrinsic::r600_store_swizzle: {
      SDLoc DL(Op);
      const SDValue Args[8] = {
        Chain,
        Op.getOperand(2), // Export Value
        Op.getOperand(3), // ArrayBase
        Op.getOperand(4), // Type
        DAG.getConstant(0, DL, MVT::i32), // SWZ_X
        DAG.getConstant(1, DL, MVT::i32), // SWZ_Y
        DAG.getConstant(2, DL, MVT::i32), // SWZ_Z
        DAG.getConstant(3, DL, MVT::i32) // SWZ_W
      };
      return DAG.getNode(AMDGPUISD::R600_EXPORT, DL, Op.getValueType(), Args);
    }

    // default for switch(IntrinsicID)
    default: break;
    }
    // break out of case ISD::INTRINSIC_VOID in switch(Op.getOpcode())
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntrinsicID = Op.getConstantOperandVal(0);
    EVT VT = Op.getValueType();
    SDLoc DL(Op);
    switch (IntrinsicID) {
    case Intrinsic::r600_tex:
    case Intrinsic::r600_texc: {
      unsigned TextureOp;
      switch (IntrinsicID) {
      case Intrinsic::r600_tex:
        TextureOp = 0;
        break;
      case Intrinsic::r600_texc:
        TextureOp = 1;
        break;
      default:
        llvm_unreachable("unhandled texture operation");
      }

      SDValue TexArgs[19] = {
        DAG.getConstant(TextureOp, DL, MVT::i32),
        Op.getOperand(1),
        DAG.getConstant(0, DL, MVT::i32),
        DAG.getConstant(1, DL, MVT::i32),
        DAG.getConstant(2, DL, MVT::i32),
        DAG.getConstant(3, DL, MVT::i32),
        Op.getOperand(2),
        Op.getOperand(3),
        Op.getOperand(4),
        DAG.getConstant(0, DL, MVT::i32),
        DAG.getConstant(1, DL, MVT::i32),
        DAG.getConstant(2, DL, MVT::i32),
        DAG.getConstant(3, DL, MVT::i32),
        Op.getOperand(5),
        Op.getOperand(6),
        Op.getOperand(7),
        Op.getOperand(8),
        Op.getOperand(9),
        Op.getOperand(10)
      };
      return DAG.getNode(AMDGPUISD::TEXTURE_FETCH, DL, MVT::v4f32, TexArgs);
    }
    case Intrinsic::r600_dot4: {
      SDValue Args[8] = {
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(1),
          DAG.getConstant(0, DL, MVT::i32)),
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(2),
          DAG.getConstant(0, DL, MVT::i32)),
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(1),
          DAG.getConstant(1, DL, MVT::i32)),
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(2),
          DAG.getConstant(1, DL, MVT::i32)),
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(1),
          DAG.getConstant(2, DL, MVT::i32)),
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(2),
          DAG.getConstant(2, DL, MVT::i32)),
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(1),
          DAG.getConstant(3, DL, MVT::i32)),
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, Op.getOperand(2),
          DAG.getConstant(3, DL, MVT::i32))
      };
      return DAG.getNode(AMDGPUISD::DOT4, DL, MVT::f32, Args);
    }

    case Intrinsic::r600_implicitarg_ptr: {
      MVT PtrVT = getPointerTy(DAG.getDataLayout(), AMDGPUAS::PARAM_I_ADDRESS);
      uint32_t ByteOffset = getImplicitParameterOffset(MF, FIRST_IMPLICIT);
      return DAG.getConstant(ByteOffset, DL, PtrVT);
    }
    case Intrinsic::r600_read_ngroups_x:
      return LowerImplicitParameter(DAG, VT, DL, 0);
    case Intrinsic::r600_read_ngroups_y:
      return LowerImplicitParameter(DAG, VT, DL, 1);
    case Intrinsic::r600_read_ngroups_z:
      return LowerImplicitParameter(DAG, VT, DL, 2);
    case Intrinsic::r600_read_global_size_x:
      return LowerImplicitParameter(DAG, VT, DL, 3);
    case Intrinsic::r600_read_global_size_y:
      return LowerImplicitParameter(DAG, VT, DL, 4);
    case Intrinsic::r600_read_global_size_z:
      return LowerImplicitParameter(DAG, VT, DL, 5);
    case Intrinsic::r600_read_local_size_x:
      return LowerImplicitParameter(DAG, VT, DL, 6);
    case Intrinsic::r600_read_local_size_y:
      return LowerImplicitParameter(DAG, VT, DL, 7);
    case Intrinsic::r600_read_local_size_z:
      return LowerImplicitParameter(DAG, VT, DL, 8);

    case Intrinsic::r600_read_tgid_x:
    case Intrinsic::amdgcn_workgroup_id_x:
      return CreateLiveInRegisterRaw(DAG, &R600::R600_TReg32RegClass,
                                     R600::T1_X, VT);
    case Intrinsic::r600_read_tgid_y:
    case Intrinsic::amdgcn_workgroup_id_y:
      return CreateLiveInRegisterRaw(DAG, &R600::R600_TReg32RegClass,
                                     R600::T1_Y, VT);
    case Intrinsic::r600_read_tgid_z:
    case Intrinsic::amdgcn_workgroup_id_z:
      return CreateLiveInRegisterRaw(DAG, &R600::R600_TReg32RegClass,
                                     R600::T1_Z, VT);
    case Intrinsic::r600_read_tidig_x:
    case Intrinsic::amdgcn_workitem_id_x:
      return CreateLiveInRegisterRaw(DAG, &R600::R600_TReg32RegClass,
                                     R600::T0_X, VT);
    case Intrinsic::r600_read_tidig_y:
    case Intrinsic::amdgcn_workitem_id_y:
      return CreateLiveInRegisterRaw(DAG, &R600::R600_TReg32RegClass,
                                     R600::T0_Y, VT);
    case Intrinsic::r600_read_tidig_z:
    case Intrinsic::amdgcn_workitem_id_z:
      return CreateLiveInRegisterRaw(DAG, &R600::R600_TReg32RegClass,
                                     R600::T0_Z, VT);

    case Intrinsic::r600_recipsqrt_ieee:
      return DAG.getNode(AMDGPUISD::RSQ, DL, VT, Op.getOperand(1));

    case Intrinsic::r600_recipsqrt_clamped:
      return DAG.getNode(AMDGPUISD::RSQ_CLAMP, DL, VT, Op.getOperand(1));
    default:
      return Op;
    }

    // break out of case ISD::INTRINSIC_WO_CHAIN in switch(Op.getOpcode())
    break;
  }
  } // end switch(Op.getOpcode())
  return SDValue();
}

void R600TargetLowering::ReplaceNodeResults(SDNode *N,
                                            SmallVectorImpl<SDValue> &Results,
                                            SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  default:
    AMDGPUTargetLowering::ReplaceNodeResults(N, Results, DAG);
    return;
  case ISD::FP_TO_UINT:
    if (N->getValueType(0) == MVT::i1) {
      Results.push_back(lowerFP_TO_UINT(N->getOperand(0), DAG));
      return;
    }
    // Since we don't care about out of bounds values we can use FP_TO_SINT for
    // uints too. The DAGLegalizer code for uint considers some extra cases
    // which are not necessary here.
    [[fallthrough]];
  case ISD::FP_TO_SINT: {
    if (N->getValueType(0) == MVT::i1) {
      Results.push_back(lowerFP_TO_SINT(N->getOperand(0), DAG));
      return;
    }

    SDValue Result;
    if (expandFP_TO_SINT(N, Result, DAG))
      Results.push_back(Result);
    return;
  }
  case ISD::SDIVREM: {
    SDValue Op = SDValue(N, 1);
    SDValue RES = LowerSDIVREM(Op, DAG);
    Results.push_back(RES);
    Results.push_back(RES.getValue(1));
    break;
  }
  case ISD::UDIVREM: {
    SDValue Op = SDValue(N, 0);
    LowerUDIVREM64(Op, DAG, Results);
    break;
  }
  }
}

SDValue R600TargetLowering::vectorToVerticalVector(SelectionDAG &DAG,
                                                   SDValue Vector) const {
  SDLoc DL(Vector);
  EVT VecVT = Vector.getValueType();
  EVT EltVT = VecVT.getVectorElementType();
  SmallVector<SDValue, 8> Args;

  for (unsigned i = 0, e = VecVT.getVectorNumElements(); i != e; ++i) {
    Args.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Vector,
                               DAG.getVectorIdxConstant(i, DL)));
  }

  return DAG.getNode(AMDGPUISD::BUILD_VERTICAL_VECTOR, DL, VecVT, Args);
}

SDValue R600TargetLowering::LowerEXTRACT_VECTOR_ELT(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Vector = Op.getOperand(0);
  SDValue Index = Op.getOperand(1);

  if (isa<ConstantSDNode>(Index) ||
      Vector.getOpcode() == AMDGPUISD::BUILD_VERTICAL_VECTOR)
    return Op;

  Vector = vectorToVerticalVector(DAG, Vector);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, Op.getValueType(),
                     Vector, Index);
}

SDValue R600TargetLowering::LowerINSERT_VECTOR_ELT(SDValue Op,
                                                   SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Vector = Op.getOperand(0);
  SDValue Value = Op.getOperand(1);
  SDValue Index = Op.getOperand(2);

  if (isa<ConstantSDNode>(Index) ||
      Vector.getOpcode() == AMDGPUISD::BUILD_VERTICAL_VECTOR)
    return Op;

  Vector = vectorToVerticalVector(DAG, Vector);
  SDValue Insert = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, Op.getValueType(),
                               Vector, Value, Index);
  return vectorToVerticalVector(DAG, Insert);
}

SDValue R600TargetLowering::LowerGlobalAddress(AMDGPUMachineFunction *MFI,
                                               SDValue Op,
                                               SelectionDAG &DAG) const {
  GlobalAddressSDNode *GSD = cast<GlobalAddressSDNode>(Op);
  if (GSD->getAddressSpace() != AMDGPUAS::CONSTANT_ADDRESS)
    return AMDGPUTargetLowering::LowerGlobalAddress(MFI, Op, DAG);

  const DataLayout &DL = DAG.getDataLayout();
  const GlobalValue *GV = GSD->getGlobal();
  MVT ConstPtrVT = getPointerTy(DL, AMDGPUAS::CONSTANT_ADDRESS);

  SDValue GA = DAG.getTargetGlobalAddress(GV, SDLoc(GSD), ConstPtrVT);
  return DAG.getNode(AMDGPUISD::CONST_DATA_PTR, SDLoc(GSD), ConstPtrVT, GA);
}

SDValue R600TargetLowering::LowerTrig(SDValue Op, SelectionDAG &DAG) const {
  // On hw >= R700, COS/SIN input must be between -1. and 1.
  // Thus we lower them to TRIG ( FRACT ( x / 2Pi + 0.5) - 0.5)
  EVT VT = Op.getValueType();
  SDValue Arg = Op.getOperand(0);
  SDLoc DL(Op);

  // TODO: Should this propagate fast-math-flags?
  SDValue FractPart = DAG.getNode(AMDGPUISD::FRACT, DL, VT,
      DAG.getNode(ISD::FADD, DL, VT,
        DAG.getNode(ISD::FMUL, DL, VT, Arg,
          DAG.getConstantFP(0.15915494309, DL, MVT::f32)),
        DAG.getConstantFP(0.5, DL, MVT::f32)));
  unsigned TrigNode;
  switch (Op.getOpcode()) {
  case ISD::FCOS:
    TrigNode = AMDGPUISD::COS_HW;
    break;
  case ISD::FSIN:
    TrigNode = AMDGPUISD::SIN_HW;
    break;
  default:
    llvm_unreachable("Wrong trig opcode");
  }
  SDValue TrigVal = DAG.getNode(TrigNode, DL, VT,
      DAG.getNode(ISD::FADD, DL, VT, FractPart,
        DAG.getConstantFP(-0.5, DL, MVT::f32)));
  if (Gen >= AMDGPUSubtarget::R700)
    return TrigVal;
  // On R600 hw, COS/SIN input must be between -Pi and Pi.
  return DAG.getNode(ISD::FMUL, DL, VT, TrigVal,
      DAG.getConstantFP(numbers::pif, DL, MVT::f32));
}

SDValue R600TargetLowering::LowerShiftParts(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDValue Lo, Hi;
  expandShiftParts(Op.getNode(), Lo, Hi, DAG);
  return DAG.getMergeValues({Lo, Hi}, SDLoc(Op));
}

SDValue R600TargetLowering::LowerUADDSUBO(SDValue Op, SelectionDAG &DAG,
                                          unsigned mainop, unsigned ovf) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);

  SDValue OVF = DAG.getNode(ovf, DL, VT, Lo, Hi);
  // Extend sign.
  OVF = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, OVF,
                    DAG.getValueType(MVT::i1));

  SDValue Res = DAG.getNode(mainop, DL, VT, Lo, Hi);

  return DAG.getNode(ISD::MERGE_VALUES, DL, DAG.getVTList(VT, VT), Res, OVF);
}

SDValue R600TargetLowering::lowerFP_TO_UINT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(
      ISD::SETCC,
      DL,
      MVT::i1,
      Op, DAG.getConstantFP(1.0f, DL, MVT::f32),
      DAG.getCondCode(ISD::SETEQ));
}

SDValue R600TargetLowering::lowerFP_TO_SINT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(
      ISD::SETCC,
      DL,
      MVT::i1,
      Op, DAG.getConstantFP(-1.0f, DL, MVT::f32),
      DAG.getCondCode(ISD::SETEQ));
}

SDValue R600TargetLowering::LowerImplicitParameter(SelectionDAG &DAG, EVT VT,
                                                   const SDLoc &DL,
                                                   unsigned DwordOffset) const {
  unsigned ByteOffset = DwordOffset * 4;
  PointerType * PtrType = PointerType::get(VT.getTypeForEVT(*DAG.getContext()),
                                      AMDGPUAS::PARAM_I_ADDRESS);

  // We shouldn't be using an offset wider than 16-bits for implicit parameters.
  assert(isInt<16>(ByteOffset));

  return DAG.getLoad(VT, DL, DAG.getEntryNode(),
                     DAG.getConstant(ByteOffset, DL, MVT::i32), // PTR
                     MachinePointerInfo(ConstantPointerNull::get(PtrType)));
}

bool R600TargetLowering::isZero(SDValue Op) const {
  if (ConstantSDNode *Cst = dyn_cast<ConstantSDNode>(Op))
    return Cst->isZero();
  if (ConstantFPSDNode *CstFP = dyn_cast<ConstantFPSDNode>(Op))
    return CstFP->isZero();
  return false;
}

bool R600TargetLowering::isHWTrueValue(SDValue Op) const {
  if (ConstantFPSDNode * CFP = dyn_cast<ConstantFPSDNode>(Op)) {
    return CFP->isExactlyValue(1.0);
  }
  return isAllOnesConstant(Op);
}

bool R600TargetLowering::isHWFalseValue(SDValue Op) const {
  if (ConstantFPSDNode * CFP = dyn_cast<ConstantFPSDNode>(Op)) {
    return CFP->getValueAPF().isZero();
  }
  return isNullConstant(Op);
}

SDValue R600TargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue True = Op.getOperand(2);
  SDValue False = Op.getOperand(3);
  SDValue CC = Op.getOperand(4);
  SDValue Temp;

  if (VT == MVT::f32) {
    DAGCombinerInfo DCI(DAG, AfterLegalizeVectorOps, true, nullptr);
    SDValue MinMax = combineFMinMaxLegacy(DL, VT, LHS, RHS, True, False, CC, DCI);
    if (MinMax)
      return MinMax;
  }

  // LHS and RHS are guaranteed to be the same value type
  EVT CompareVT = LHS.getValueType();

  // Check if we can lower this to a native operation.

  // Try to lower to a SET* instruction:
  //
  // SET* can match the following patterns:
  //
  // select_cc f32, f32, -1,  0, cc_supported
  // select_cc f32, f32, 1.0f, 0.0f, cc_supported
  // select_cc i32, i32, -1,  0, cc_supported
  //

  // Move hardware True/False values to the correct operand.
  if (isHWTrueValue(False) && isHWFalseValue(True)) {
    ISD::CondCode CCOpcode = cast<CondCodeSDNode>(CC)->get();
    ISD::CondCode InverseCC = ISD::getSetCCInverse(CCOpcode, CompareVT);
    if (isCondCodeLegal(InverseCC, CompareVT.getSimpleVT())) {
      std::swap(False, True);
      CC = DAG.getCondCode(InverseCC);
    } else {
      ISD::CondCode SwapInvCC = ISD::getSetCCSwappedOperands(InverseCC);
      if (isCondCodeLegal(SwapInvCC, CompareVT.getSimpleVT())) {
        std::swap(False, True);
        std::swap(LHS, RHS);
        CC = DAG.getCondCode(SwapInvCC);
      }
    }
  }

  if (isHWTrueValue(True) && isHWFalseValue(False) &&
      (CompareVT == VT || VT == MVT::i32)) {
    // This can be matched by a SET* instruction.
    return DAG.getNode(ISD::SELECT_CC, DL, VT, LHS, RHS, True, False, CC);
  }

  // Try to lower to a CND* instruction:
  //
  // CND* can match the following patterns:
  //
  // select_cc f32, 0.0, f32, f32, cc_supported
  // select_cc f32, 0.0, i32, i32, cc_supported
  // select_cc i32, 0,   f32, f32, cc_supported
  // select_cc i32, 0,   i32, i32, cc_supported
  //

  // Try to move the zero value to the RHS
  if (isZero(LHS)) {
    ISD::CondCode CCOpcode = cast<CondCodeSDNode>(CC)->get();
    // Try swapping the operands
    ISD::CondCode CCSwapped = ISD::getSetCCSwappedOperands(CCOpcode);
    if (isCondCodeLegal(CCSwapped, CompareVT.getSimpleVT())) {
      std::swap(LHS, RHS);
      CC = DAG.getCondCode(CCSwapped);
    } else {
      // Try inverting the condition and then swapping the operands
      ISD::CondCode CCInv = ISD::getSetCCInverse(CCOpcode, CompareVT);
      CCSwapped = ISD::getSetCCSwappedOperands(CCInv);
      if (isCondCodeLegal(CCSwapped, CompareVT.getSimpleVT())) {
        std::swap(True, False);
        std::swap(LHS, RHS);
        CC = DAG.getCondCode(CCSwapped);
      }
    }
  }
  if (isZero(RHS)) {
    SDValue Cond = LHS;
    SDValue Zero = RHS;
    ISD::CondCode CCOpcode = cast<CondCodeSDNode>(CC)->get();
    if (CompareVT != VT) {
      // Bitcast True / False to the correct types.  This will end up being
      // a nop, but it allows us to define only a single pattern in the
      // .TD files for each CND* instruction rather than having to have
      // one pattern for integer True/False and one for fp True/False
      True = DAG.getNode(ISD::BITCAST, DL, CompareVT, True);
      False = DAG.getNode(ISD::BITCAST, DL, CompareVT, False);
    }

    switch (CCOpcode) {
    case ISD::SETONE:
    case ISD::SETUNE:
    case ISD::SETNE:
      CCOpcode = ISD::getSetCCInverse(CCOpcode, CompareVT);
      Temp = True;
      True = False;
      False = Temp;
      break;
    default:
      break;
    }
    SDValue SelectNode = DAG.getNode(ISD::SELECT_CC, DL, CompareVT,
        Cond, Zero,
        True, False,
        DAG.getCondCode(CCOpcode));
    return DAG.getNode(ISD::BITCAST, DL, VT, SelectNode);
  }

  // If we make it this for it means we have no native instructions to handle
  // this SELECT_CC, so we must lower it.
  SDValue HWTrue, HWFalse;

  if (CompareVT == MVT::f32) {
    HWTrue = DAG.getConstantFP(1.0f, DL, CompareVT);
    HWFalse = DAG.getConstantFP(0.0f, DL, CompareVT);
  } else if (CompareVT == MVT::i32) {
    HWTrue = DAG.getConstant(-1, DL, CompareVT);
    HWFalse = DAG.getConstant(0, DL, CompareVT);
  }
  else {
    llvm_unreachable("Unhandled value type in LowerSELECT_CC");
  }

  // Lower this unsupported SELECT_CC into a combination of two supported
  // SELECT_CC operations.
  SDValue Cond = DAG.getNode(ISD::SELECT_CC, DL, CompareVT, LHS, RHS, HWTrue, HWFalse, CC);

  return DAG.getNode(ISD::SELECT_CC, DL, VT,
      Cond, HWFalse,
      True, False,
      DAG.getCondCode(ISD::SETNE));
}

SDValue R600TargetLowering::lowerADDRSPACECAST(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc SL(Op);
  EVT VT = Op.getValueType();

  const R600TargetMachine &TM =
      static_cast<const R600TargetMachine &>(getTargetMachine());

  const AddrSpaceCastSDNode *ASC = cast<AddrSpaceCastSDNode>(Op);
  unsigned SrcAS = ASC->getSrcAddressSpace();
  unsigned DestAS = ASC->getDestAddressSpace();

  if (isNullConstant(Op.getOperand(0)) && SrcAS == AMDGPUAS::FLAT_ADDRESS)
    return DAG.getConstant(TM.getNullPointerValue(DestAS), SL, VT);

  return Op;
}

/// LLVM generates byte-addressed pointers.  For indirect addressing, we need to
/// convert these pointers to a register index.  Each register holds
/// 16 bytes, (4 x 32bit sub-register), but we need to take into account the
/// \p StackWidth, which tells us how many of the 4 sub-registers will be used
/// for indirect addressing.
SDValue R600TargetLowering::stackPtrToRegIndex(SDValue Ptr,
                                               unsigned StackWidth,
                                               SelectionDAG &DAG) const {
  unsigned SRLPad;
  switch(StackWidth) {
  case 1:
    SRLPad = 2;
    break;
  case 2:
    SRLPad = 3;
    break;
  case 4:
    SRLPad = 4;
    break;
  default: llvm_unreachable("Invalid stack width");
  }

  SDLoc DL(Ptr);
  return DAG.getNode(ISD::SRL, DL, Ptr.getValueType(), Ptr,
                     DAG.getConstant(SRLPad, DL, MVT::i32));
}

void R600TargetLowering::getStackAddress(unsigned StackWidth,
                                         unsigned ElemIdx,
                                         unsigned &Channel,
                                         unsigned &PtrIncr) const {
  switch (StackWidth) {
  default:
  case 1:
    Channel = 0;
    if (ElemIdx > 0) {
      PtrIncr = 1;
    } else {
      PtrIncr = 0;
    }
    break;
  case 2:
    Channel = ElemIdx % 2;
    if (ElemIdx == 2) {
      PtrIncr = 1;
    } else {
      PtrIncr = 0;
    }
    break;
  case 4:
    Channel = ElemIdx;
    PtrIncr = 0;
    break;
  }
}

SDValue R600TargetLowering::lowerPrivateTruncStore(StoreSDNode *Store,
                                                   SelectionDAG &DAG) const {
  SDLoc DL(Store);
  //TODO: Who creates the i8 stores?
  assert(Store->isTruncatingStore()
         || Store->getValue().getValueType() == MVT::i8);
  assert(Store->getAddressSpace() == AMDGPUAS::PRIVATE_ADDRESS);

  SDValue Mask;
  if (Store->getMemoryVT() == MVT::i8) {
    assert(Store->getAlign() >= 1);
    Mask = DAG.getConstant(0xff, DL, MVT::i32);
  } else if (Store->getMemoryVT() == MVT::i16) {
    assert(Store->getAlign() >= 2);
    Mask = DAG.getConstant(0xffff, DL, MVT::i32);
  } else {
    llvm_unreachable("Unsupported private trunc store");
  }

  SDValue OldChain = Store->getChain();
  bool VectorTrunc = (OldChain.getOpcode() == AMDGPUISD::DUMMY_CHAIN);
  // Skip dummy
  SDValue Chain = VectorTrunc ? OldChain->getOperand(0) : OldChain;
  SDValue BasePtr = Store->getBasePtr();
  SDValue Offset = Store->getOffset();
  EVT MemVT = Store->getMemoryVT();

  SDValue LoadPtr = BasePtr;
  if (!Offset.isUndef()) {
    LoadPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, BasePtr, Offset);
  }

  // Get dword location
  // TODO: this should be eliminated by the future SHR ptr, 2
  SDValue Ptr = DAG.getNode(ISD::AND, DL, MVT::i32, LoadPtr,
                            DAG.getConstant(0xfffffffc, DL, MVT::i32));

  // Load dword
  // TODO: can we be smarter about machine pointer info?
  MachinePointerInfo PtrInfo(AMDGPUAS::PRIVATE_ADDRESS);
  SDValue Dst = DAG.getLoad(MVT::i32, DL, Chain, Ptr, PtrInfo);

  Chain = Dst.getValue(1);

  // Get offset in dword
  SDValue ByteIdx = DAG.getNode(ISD::AND, DL, MVT::i32, LoadPtr,
                                DAG.getConstant(0x3, DL, MVT::i32));

  // Convert byte offset to bit shift
  SDValue ShiftAmt = DAG.getNode(ISD::SHL, DL, MVT::i32, ByteIdx,
                                 DAG.getConstant(3, DL, MVT::i32));

  // TODO: Contrary to the name of the function,
  // it also handles sub i32 non-truncating stores (like i1)
  SDValue SExtValue = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32,
                                  Store->getValue());

  // Mask the value to the right type
  SDValue MaskedValue = DAG.getZeroExtendInReg(SExtValue, DL, MemVT);

  // Shift the value in place
  SDValue ShiftedValue = DAG.getNode(ISD::SHL, DL, MVT::i32,
                                     MaskedValue, ShiftAmt);

  // Shift the mask in place
  SDValue DstMask = DAG.getNode(ISD::SHL, DL, MVT::i32, Mask, ShiftAmt);

  // Invert the mask. NOTE: if we had native ROL instructions we could
  // use inverted mask
  DstMask = DAG.getNOT(DL, DstMask, MVT::i32);

  // Cleanup the target bits
  Dst = DAG.getNode(ISD::AND, DL, MVT::i32, Dst, DstMask);

  // Add the new bits
  SDValue Value = DAG.getNode(ISD::OR, DL, MVT::i32, Dst, ShiftedValue);

  // Store dword
  // TODO: Can we be smarter about MachinePointerInfo?
  SDValue NewStore = DAG.getStore(Chain, DL, Value, Ptr, PtrInfo);

  // If we are part of expanded vector, make our neighbors depend on this store
  if (VectorTrunc) {
    // Make all other vector elements depend on this store
    Chain = DAG.getNode(AMDGPUISD::DUMMY_CHAIN, DL, MVT::Other, NewStore);
    DAG.ReplaceAllUsesOfValueWith(OldChain, Chain);
  }
  return NewStore;
}

SDValue R600TargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  StoreSDNode *StoreNode = cast<StoreSDNode>(Op);
  unsigned AS = StoreNode->getAddressSpace();

  SDValue Chain = StoreNode->getChain();
  SDValue Ptr = StoreNode->getBasePtr();
  SDValue Value = StoreNode->getValue();

  EVT VT = Value.getValueType();
  EVT MemVT = StoreNode->getMemoryVT();
  EVT PtrVT = Ptr.getValueType();

  SDLoc DL(Op);

  const bool TruncatingStore = StoreNode->isTruncatingStore();

  // Neither LOCAL nor PRIVATE can do vectors at the moment
  if ((AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::PRIVATE_ADDRESS ||
       TruncatingStore) &&
      VT.isVector()) {
    if ((AS == AMDGPUAS::PRIVATE_ADDRESS) && TruncatingStore) {
      // Add an extra level of chain to isolate this vector
      SDValue NewChain = DAG.getNode(AMDGPUISD::DUMMY_CHAIN, DL, MVT::Other, Chain);
      // TODO: can the chain be replaced without creating a new store?
      SDValue NewStore = DAG.getTruncStore(
          NewChain, DL, Value, Ptr, StoreNode->getPointerInfo(), MemVT,
          StoreNode->getAlign(), StoreNode->getMemOperand()->getFlags(),
          StoreNode->getAAInfo());
      StoreNode = cast<StoreSDNode>(NewStore);
    }

    return scalarizeVectorStore(StoreNode, DAG);
  }

  Align Alignment = StoreNode->getAlign();
  if (Alignment < MemVT.getStoreSize() &&
      !allowsMisalignedMemoryAccesses(MemVT, AS, Alignment,
                                      StoreNode->getMemOperand()->getFlags(),
                                      nullptr)) {
    return expandUnalignedStore(StoreNode, DAG);
  }

  SDValue DWordAddr = DAG.getNode(ISD::SRL, DL, PtrVT, Ptr,
                                  DAG.getConstant(2, DL, PtrVT));

  if (AS == AMDGPUAS::GLOBAL_ADDRESS) {
    // It is beneficial to create MSKOR here instead of combiner to avoid
    // artificial dependencies introduced by RMW
    if (TruncatingStore) {
      assert(VT.bitsLE(MVT::i32));
      SDValue MaskConstant;
      if (MemVT == MVT::i8) {
        MaskConstant = DAG.getConstant(0xFF, DL, MVT::i32);
      } else {
        assert(MemVT == MVT::i16);
        assert(StoreNode->getAlign() >= 2);
        MaskConstant = DAG.getConstant(0xFFFF, DL, MVT::i32);
      }

      SDValue ByteIndex = DAG.getNode(ISD::AND, DL, PtrVT, Ptr,
                                      DAG.getConstant(0x00000003, DL, PtrVT));
      SDValue BitShift = DAG.getNode(ISD::SHL, DL, VT, ByteIndex,
                                     DAG.getConstant(3, DL, VT));

      // Put the mask in correct place
      SDValue Mask = DAG.getNode(ISD::SHL, DL, VT, MaskConstant, BitShift);

      // Put the value bits in correct place
      SDValue TruncValue = DAG.getNode(ISD::AND, DL, VT, Value, MaskConstant);
      SDValue ShiftedValue = DAG.getNode(ISD::SHL, DL, VT, TruncValue, BitShift);

      // XXX: If we add a 64-bit ZW register class, then we could use a 2 x i32
      // vector instead.
      SDValue Src[4] = {
        ShiftedValue,
        DAG.getConstant(0, DL, MVT::i32),
        DAG.getConstant(0, DL, MVT::i32),
        Mask
      };
      SDValue Input = DAG.getBuildVector(MVT::v4i32, DL, Src);
      SDValue Args[3] = { Chain, Input, DWordAddr };
      return DAG.getMemIntrinsicNode(AMDGPUISD::STORE_MSKOR, DL,
                                     Op->getVTList(), Args, MemVT,
                                     StoreNode->getMemOperand());
    }
    if (Ptr->getOpcode() != AMDGPUISD::DWORDADDR && VT.bitsGE(MVT::i32)) {
      // Convert pointer from byte address to dword address.
      Ptr = DAG.getNode(AMDGPUISD::DWORDADDR, DL, PtrVT, DWordAddr);

      if (StoreNode->isIndexed()) {
        llvm_unreachable("Indexed stores not supported yet");
      } else {
        Chain = DAG.getStore(Chain, DL, Value, Ptr, StoreNode->getMemOperand());
      }
      return Chain;
    }
  }

  // GLOBAL_ADDRESS has been handled above, LOCAL_ADDRESS allows all sizes
  if (AS != AMDGPUAS::PRIVATE_ADDRESS)
    return SDValue();

  if (MemVT.bitsLT(MVT::i32))
    return lowerPrivateTruncStore(StoreNode, DAG);

  // Standard i32+ store, tag it with DWORDADDR to note that the address
  // has been shifted
  if (Ptr.getOpcode() != AMDGPUISD::DWORDADDR) {
    Ptr = DAG.getNode(AMDGPUISD::DWORDADDR, DL, PtrVT, DWordAddr);
    return DAG.getStore(Chain, DL, Value, Ptr, StoreNode->getMemOperand());
  }

  // Tagged i32+ stores will be matched by patterns
  return SDValue();
}

// return (512 + (kc_bank << 12)
static int
ConstantAddressBlock(unsigned AddressSpace) {
  switch (AddressSpace) {
  case AMDGPUAS::CONSTANT_BUFFER_0:
    return 512;
  case AMDGPUAS::CONSTANT_BUFFER_1:
    return 512 + 4096;
  case AMDGPUAS::CONSTANT_BUFFER_2:
    return 512 + 4096 * 2;
  case AMDGPUAS::CONSTANT_BUFFER_3:
    return 512 + 4096 * 3;
  case AMDGPUAS::CONSTANT_BUFFER_4:
    return 512 + 4096 * 4;
  case AMDGPUAS::CONSTANT_BUFFER_5:
    return 512 + 4096 * 5;
  case AMDGPUAS::CONSTANT_BUFFER_6:
    return 512 + 4096 * 6;
  case AMDGPUAS::CONSTANT_BUFFER_7:
    return 512 + 4096 * 7;
  case AMDGPUAS::CONSTANT_BUFFER_8:
    return 512 + 4096 * 8;
  case AMDGPUAS::CONSTANT_BUFFER_9:
    return 512 + 4096 * 9;
  case AMDGPUAS::CONSTANT_BUFFER_10:
    return 512 + 4096 * 10;
  case AMDGPUAS::CONSTANT_BUFFER_11:
    return 512 + 4096 * 11;
  case AMDGPUAS::CONSTANT_BUFFER_12:
    return 512 + 4096 * 12;
  case AMDGPUAS::CONSTANT_BUFFER_13:
    return 512 + 4096 * 13;
  case AMDGPUAS::CONSTANT_BUFFER_14:
    return 512 + 4096 * 14;
  case AMDGPUAS::CONSTANT_BUFFER_15:
    return 512 + 4096 * 15;
  default:
    return -1;
  }
}

SDValue R600TargetLowering::lowerPrivateExtLoad(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  LoadSDNode *Load = cast<LoadSDNode>(Op);
  ISD::LoadExtType ExtType = Load->getExtensionType();
  EVT MemVT = Load->getMemoryVT();
  assert(Load->getAlign() >= MemVT.getStoreSize());

  SDValue BasePtr = Load->getBasePtr();
  SDValue Chain = Load->getChain();
  SDValue Offset = Load->getOffset();

  SDValue LoadPtr = BasePtr;
  if (!Offset.isUndef()) {
    LoadPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, BasePtr, Offset);
  }

  // Get dword location
  // NOTE: this should be eliminated by the future SHR ptr, 2
  SDValue Ptr = DAG.getNode(ISD::AND, DL, MVT::i32, LoadPtr,
                            DAG.getConstant(0xfffffffc, DL, MVT::i32));

  // Load dword
  // TODO: can we be smarter about machine pointer info?
  MachinePointerInfo PtrInfo(AMDGPUAS::PRIVATE_ADDRESS);
  SDValue Read = DAG.getLoad(MVT::i32, DL, Chain, Ptr, PtrInfo);

  // Get offset within the register.
  SDValue ByteIdx = DAG.getNode(ISD::AND, DL, MVT::i32,
                                LoadPtr, DAG.getConstant(0x3, DL, MVT::i32));

  // Bit offset of target byte (byteIdx * 8).
  SDValue ShiftAmt = DAG.getNode(ISD::SHL, DL, MVT::i32, ByteIdx,
                                 DAG.getConstant(3, DL, MVT::i32));

  // Shift to the right.
  SDValue Ret = DAG.getNode(ISD::SRL, DL, MVT::i32, Read, ShiftAmt);

  // Eliminate the upper bits by setting them to ...
  EVT MemEltVT = MemVT.getScalarType();

  if (ExtType == ISD::SEXTLOAD) { // ... ones.
    SDValue MemEltVTNode = DAG.getValueType(MemEltVT);
    Ret = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i32, Ret, MemEltVTNode);
  } else { // ... or zeros.
    Ret = DAG.getZeroExtendInReg(Ret, DL, MemEltVT);
  }

  SDValue Ops[] = {
    Ret,
    Read.getValue(1) // This should be our output chain
  };

  return DAG.getMergeValues(Ops, DL);
}

SDValue R600TargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  LoadSDNode *LoadNode = cast<LoadSDNode>(Op);
  unsigned AS = LoadNode->getAddressSpace();
  EVT MemVT = LoadNode->getMemoryVT();
  ISD::LoadExtType ExtType = LoadNode->getExtensionType();

  if (AS == AMDGPUAS::PRIVATE_ADDRESS &&
      ExtType != ISD::NON_EXTLOAD && MemVT.bitsLT(MVT::i32)) {
    return lowerPrivateExtLoad(Op, DAG);
  }

  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Chain = LoadNode->getChain();
  SDValue Ptr = LoadNode->getBasePtr();

  if ((LoadNode->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS ||
      LoadNode->getAddressSpace() == AMDGPUAS::PRIVATE_ADDRESS) &&
      VT.isVector()) {
    SDValue Ops[2];
    std::tie(Ops[0], Ops[1]) = scalarizeVectorLoad(LoadNode, DAG);
    return DAG.getMergeValues(Ops, DL);
  }

  // This is still used for explicit load from addrspace(8)
  int ConstantBlock = ConstantAddressBlock(LoadNode->getAddressSpace());
  if (ConstantBlock > -1 &&
      ((LoadNode->getExtensionType() == ISD::NON_EXTLOAD) ||
       (LoadNode->getExtensionType() == ISD::ZEXTLOAD))) {
    SDValue Result;
    if (isa<Constant>(LoadNode->getMemOperand()->getValue()) ||
        isa<ConstantSDNode>(Ptr)) {
      return constBufferLoad(LoadNode, LoadNode->getAddressSpace(), DAG);
    }
    // TODO: Does this even work?
    //  non-constant ptr can't be folded, keeps it as a v4f32 load
    Result = DAG.getNode(AMDGPUISD::CONST_ADDRESS, DL, MVT::v4i32,
                         DAG.getNode(ISD::SRL, DL, MVT::i32, Ptr,
                                     DAG.getConstant(4, DL, MVT::i32)),
                         DAG.getConstant(LoadNode->getAddressSpace() -
                                             AMDGPUAS::CONSTANT_BUFFER_0,
                                         DL, MVT::i32));

    if (!VT.isVector()) {
      Result = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, Result,
                           DAG.getConstant(0, DL, MVT::i32));
    }

    SDValue MergedValues[2] = {
      Result,
      Chain
    };
    return DAG.getMergeValues(MergedValues, DL);
  }

  // For most operations returning SDValue() will result in the node being
  // expanded by the DAG Legalizer. This is not the case for ISD::LOAD, so we
  // need to manually expand loads that may be legal in some address spaces and
  // illegal in others. SEXT loads from CONSTANT_BUFFER_0 are supported for
  // compute shaders, since the data is sign extended when it is uploaded to the
  // buffer. However SEXT loads from other address spaces are not supported, so
  // we need to expand them here.
  if (LoadNode->getExtensionType() == ISD::SEXTLOAD) {
    assert(!MemVT.isVector() && (MemVT == MVT::i16 || MemVT == MVT::i8));
    SDValue NewLoad = DAG.getExtLoad(
        ISD::EXTLOAD, DL, VT, Chain, Ptr, LoadNode->getPointerInfo(), MemVT,
        LoadNode->getAlign(), LoadNode->getMemOperand()->getFlags());
    SDValue Res = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, NewLoad,
                              DAG.getValueType(MemVT));

    SDValue MergedValues[2] = { Res, Chain };
    return DAG.getMergeValues(MergedValues, DL);
  }

  if (LoadNode->getAddressSpace() != AMDGPUAS::PRIVATE_ADDRESS) {
    return SDValue();
  }

  // DWORDADDR ISD marks already shifted address
  if (Ptr.getOpcode() != AMDGPUISD::DWORDADDR) {
    assert(VT == MVT::i32);
    Ptr = DAG.getNode(ISD::SRL, DL, MVT::i32, Ptr, DAG.getConstant(2, DL, MVT::i32));
    Ptr = DAG.getNode(AMDGPUISD::DWORDADDR, DL, MVT::i32, Ptr);
    return DAG.getLoad(MVT::i32, DL, Chain, Ptr, LoadNode->getMemOperand());
  }
  return SDValue();
}

SDValue R600TargetLowering::LowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Cond  = Op.getOperand(1);
  SDValue Jump  = Op.getOperand(2);

  return DAG.getNode(AMDGPUISD::BRANCH_COND, SDLoc(Op), Op.getValueType(),
                     Chain, Jump, Cond);
}

SDValue R600TargetLowering::lowerFrameIndex(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const R600FrameLowering *TFL = Subtarget->getFrameLowering();

  FrameIndexSDNode *FIN = cast<FrameIndexSDNode>(Op);

  unsigned FrameIndex = FIN->getIndex();
  Register IgnoredFrameReg;
  StackOffset Offset =
      TFL->getFrameIndexReference(MF, FrameIndex, IgnoredFrameReg);
  return DAG.getConstant(Offset.getFixed() * 4 * TFL->getStackWidth(MF),
                         SDLoc(Op), Op.getValueType());
}

CCAssignFn *R600TargetLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                  bool IsVarArg) const {
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    llvm_unreachable("kernels should not be handled here");
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_LS:
    return CC_R600;
  default:
    report_fatal_error("Unsupported calling convention.");
  }
}

/// XXX Only kernel functions are supported, so we can assume for now that
/// every function is a kernel function, but in the future we should use
/// separate calling conventions for kernel and non-kernel functions.
SDValue R600TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<ISD::InputArg, 8> LocalIns;

  if (AMDGPU::isShader(CallConv)) {
    CCInfo.AnalyzeFormalArguments(Ins, CCAssignFnForCall(CallConv, isVarArg));
  } else {
    analyzeFormalArgumentsCompute(CCInfo, Ins);
  }

  for (unsigned i = 0, e = Ins.size(); i < e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    const ISD::InputArg &In = Ins[i];
    EVT VT = In.VT;
    EVT MemVT = VA.getLocVT();
    if (!VT.isVector() && MemVT.isVector()) {
      // Get load source type if scalarized.
      MemVT = MemVT.getVectorElementType();
    }

    if (AMDGPU::isShader(CallConv)) {
      Register Reg = MF.addLiveIn(VA.getLocReg(), &R600::R600_Reg128RegClass);
      SDValue Register = DAG.getCopyFromReg(Chain, DL, Reg, VT);
      InVals.push_back(Register);
      continue;
    }

    // i64 isn't a legal type, so the register type used ends up as i32, which
    // isn't expected here. It attempts to create this sextload, but it ends up
    // being invalid. Somehow this seems to work with i64 arguments, but breaks
    // for <1 x i64>.

    // The first 36 bytes of the input buffer contains information about
    // thread group and global sizes.
    ISD::LoadExtType Ext = ISD::NON_EXTLOAD;
    if (MemVT.getScalarSizeInBits() != VT.getScalarSizeInBits()) {
      // FIXME: This should really check the extload type, but the handling of
      // extload vector parameters seems to be broken.

      // Ext = In.Flags.isSExt() ? ISD::SEXTLOAD : ISD::ZEXTLOAD;
      Ext = ISD::SEXTLOAD;
    }

    // Compute the offset from the value.
    // XXX - I think PartOffset should give you this, but it seems to give the
    // size of the register which isn't useful.

    unsigned PartOffset = VA.getLocMemOffset();
    Align Alignment = commonAlignment(Align(VT.getStoreSize()), PartOffset);

    MachinePointerInfo PtrInfo(AMDGPUAS::PARAM_I_ADDRESS);
    SDValue Arg = DAG.getLoad(
        ISD::UNINDEXED, Ext, VT, DL, Chain,
        DAG.getConstant(PartOffset, DL, MVT::i32), DAG.getUNDEF(MVT::i32),
        PtrInfo,
        MemVT, Alignment, MachineMemOperand::MONonTemporal |
                                        MachineMemOperand::MODereferenceable |
                                        MachineMemOperand::MOInvariant);

    InVals.push_back(Arg);
  }
  return Chain;
}

EVT R600TargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &,
                                           EVT VT) const {
   if (!VT.isVector())
     return MVT::i32;
   return VT.changeVectorElementTypeToInteger();
}

bool R600TargetLowering::canMergeStoresTo(unsigned AS, EVT MemVT,
                                          const MachineFunction &MF) const {
  // Local and Private addresses do not handle vectors. Limit to i32
  if ((AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::PRIVATE_ADDRESS)) {
    return (MemVT.getSizeInBits() <= 32);
  }
  return true;
}

bool R600TargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned AddrSpace, Align Alignment, MachineMemOperand::Flags Flags,
    unsigned *IsFast) const {
  if (IsFast)
    *IsFast = 0;

  if (!VT.isSimple() || VT == MVT::Other)
    return false;

  if (VT.bitsLT(MVT::i32))
    return false;

  // TODO: This is a rough estimate.
  if (IsFast)
    *IsFast = 1;

  return VT.bitsGT(MVT::i32) && Alignment >= Align(4);
}

static SDValue CompactSwizzlableVector(
  SelectionDAG &DAG, SDValue VectorEntry,
  DenseMap<unsigned, unsigned> &RemapSwizzle) {
  assert(RemapSwizzle.empty());

  SDLoc DL(VectorEntry);
  EVT EltTy = VectorEntry.getValueType().getVectorElementType();

  SDValue NewBldVec[4];
  for (unsigned i = 0; i < 4; i++)
    NewBldVec[i] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltTy, VectorEntry,
                               DAG.getIntPtrConstant(i, DL));

  for (unsigned i = 0; i < 4; i++) {
    if (NewBldVec[i].isUndef())
      // We mask write here to teach later passes that the ith element of this
      // vector is undef. Thus we can use it to reduce 128 bits reg usage,
      // break false dependencies and additionally make assembly easier to read.
      RemapSwizzle[i] = 7; // SEL_MASK_WRITE
    if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(NewBldVec[i])) {
      if (C->isZero()) {
        RemapSwizzle[i] = 4; // SEL_0
        NewBldVec[i] = DAG.getUNDEF(MVT::f32);
      } else if (C->isExactlyValue(1.0)) {
        RemapSwizzle[i] = 5; // SEL_1
        NewBldVec[i] = DAG.getUNDEF(MVT::f32);
      }
    }

    if (NewBldVec[i].isUndef())
      continue;

    for (unsigned j = 0; j < i; j++) {
      if (NewBldVec[i] == NewBldVec[j]) {
        NewBldVec[i] = DAG.getUNDEF(NewBldVec[i].getValueType());
        RemapSwizzle[i] = j;
        break;
      }
    }
  }

  return DAG.getBuildVector(VectorEntry.getValueType(), SDLoc(VectorEntry),
                            NewBldVec);
}

static SDValue ReorganizeVector(SelectionDAG &DAG, SDValue VectorEntry,
                                DenseMap<unsigned, unsigned> &RemapSwizzle) {
  assert(RemapSwizzle.empty());

  SDLoc DL(VectorEntry);
  EVT EltTy = VectorEntry.getValueType().getVectorElementType();

  SDValue NewBldVec[4];
  bool isUnmovable[4] = {false, false, false, false};
  for (unsigned i = 0; i < 4; i++)
    NewBldVec[i] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltTy, VectorEntry,
                               DAG.getIntPtrConstant(i, DL));

  for (unsigned i = 0; i < 4; i++) {
    RemapSwizzle[i] = i;
    if (NewBldVec[i].getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
      unsigned Idx = NewBldVec[i].getConstantOperandVal(1);
      if (i == Idx)
        isUnmovable[Idx] = true;
    }
  }

  for (unsigned i = 0; i < 4; i++) {
    if (NewBldVec[i].getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
      unsigned Idx = NewBldVec[i].getConstantOperandVal(1);
      if (isUnmovable[Idx])
        continue;
      // Swap i and Idx
      std::swap(NewBldVec[Idx], NewBldVec[i]);
      std::swap(RemapSwizzle[i], RemapSwizzle[Idx]);
      break;
    }
  }

  return DAG.getBuildVector(VectorEntry.getValueType(), SDLoc(VectorEntry),
                            NewBldVec);
}

SDValue R600TargetLowering::OptimizeSwizzle(SDValue BuildVector, SDValue Swz[],
                                            SelectionDAG &DAG,
                                            const SDLoc &DL) const {
  // Old -> New swizzle values
  DenseMap<unsigned, unsigned> SwizzleRemap;

  BuildVector = CompactSwizzlableVector(DAG, BuildVector, SwizzleRemap);
  for (unsigned i = 0; i < 4; i++) {
    unsigned Idx = Swz[i]->getAsZExtVal();
    if (SwizzleRemap.contains(Idx))
      Swz[i] = DAG.getConstant(SwizzleRemap[Idx], DL, MVT::i32);
  }

  SwizzleRemap.clear();
  BuildVector = ReorganizeVector(DAG, BuildVector, SwizzleRemap);
  for (unsigned i = 0; i < 4; i++) {
    unsigned Idx = Swz[i]->getAsZExtVal();
    if (SwizzleRemap.contains(Idx))
      Swz[i] = DAG.getConstant(SwizzleRemap[Idx], DL, MVT::i32);
  }

  return BuildVector;
}

SDValue R600TargetLowering::constBufferLoad(LoadSDNode *LoadNode, int Block,
                                            SelectionDAG &DAG) const {
  SDLoc DL(LoadNode);
  EVT VT = LoadNode->getValueType(0);
  SDValue Chain = LoadNode->getChain();
  SDValue Ptr = LoadNode->getBasePtr();
  assert (isa<ConstantSDNode>(Ptr));

  //TODO: Support smaller loads
  if (LoadNode->getMemoryVT().getScalarType() != MVT::i32 || !ISD::isNON_EXTLoad(LoadNode))
    return SDValue();

  if (LoadNode->getAlign() < Align(4))
    return SDValue();

  int ConstantBlock = ConstantAddressBlock(Block);

  SDValue Slots[4];
  for (unsigned i = 0; i < 4; i++) {
    // We want Const position encoded with the following formula :
    // (((512 + (kc_bank << 12) + const_index) << 2) + chan)
    // const_index is Ptr computed by llvm using an alignment of 16.
    // Thus we add (((512 + (kc_bank << 12)) + chan ) * 4 here and
    // then div by 4 at the ISel step
    SDValue NewPtr = DAG.getNode(ISD::ADD, DL, Ptr.getValueType(), Ptr,
        DAG.getConstant(4 * i + ConstantBlock * 16, DL, MVT::i32));
    Slots[i] = DAG.getNode(AMDGPUISD::CONST_ADDRESS, DL, MVT::i32, NewPtr);
  }
  EVT NewVT = MVT::v4i32;
  unsigned NumElements = 4;
  if (VT.isVector()) {
    NewVT = VT;
    NumElements = VT.getVectorNumElements();
  }
  SDValue Result = DAG.getBuildVector(NewVT, DL, ArrayRef(Slots, NumElements));
  if (!VT.isVector()) {
    Result = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, Result,
                         DAG.getConstant(0, DL, MVT::i32));
  }
  SDValue MergedValues[2] = {
    Result,
    Chain
  };
  return DAG.getMergeValues(MergedValues, DL);
}

//===----------------------------------------------------------------------===//
// Custom DAG Optimizations
//===----------------------------------------------------------------------===//

SDValue R600TargetLowering::PerformDAGCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  switch (N->getOpcode()) {
  // (f32 fp_round (f64 uint_to_fp a)) -> (f32 uint_to_fp a)
  case ISD::FP_ROUND: {
      SDValue Arg = N->getOperand(0);
      if (Arg.getOpcode() == ISD::UINT_TO_FP && Arg.getValueType() == MVT::f64) {
        return DAG.getNode(ISD::UINT_TO_FP, DL, N->getValueType(0),
                           Arg.getOperand(0));
      }
      break;
    }

  // (i32 fp_to_sint (fneg (select_cc f32, f32, 1.0, 0.0 cc))) ->
  // (i32 select_cc f32, f32, -1, 0 cc)
  //
  // Mesa's GLSL frontend generates the above pattern a lot and we can lower
  // this to one of the SET*_DX10 instructions.
  case ISD::FP_TO_SINT: {
    SDValue FNeg = N->getOperand(0);
    if (FNeg.getOpcode() != ISD::FNEG) {
      return SDValue();
    }
    SDValue SelectCC = FNeg.getOperand(0);
    if (SelectCC.getOpcode() != ISD::SELECT_CC ||
        SelectCC.getOperand(0).getValueType() != MVT::f32 || // LHS
        SelectCC.getOperand(2).getValueType() != MVT::f32 || // True
        !isHWTrueValue(SelectCC.getOperand(2)) ||
        !isHWFalseValue(SelectCC.getOperand(3))) {
      return SDValue();
    }

    return DAG.getNode(ISD::SELECT_CC, DL, N->getValueType(0),
                           SelectCC.getOperand(0), // LHS
                           SelectCC.getOperand(1), // RHS
                           DAG.getConstant(-1, DL, MVT::i32), // True
                           DAG.getConstant(0, DL, MVT::i32),  // False
                           SelectCC.getOperand(4)); // CC
  }

  // insert_vector_elt (build_vector elt0, ... , eltN), NewEltIdx, idx
  // => build_vector elt0, ... , NewEltIdx, ... , eltN
  case ISD::INSERT_VECTOR_ELT: {
    SDValue InVec = N->getOperand(0);
    SDValue InVal = N->getOperand(1);
    SDValue EltNo = N->getOperand(2);

    // If the inserted element is an UNDEF, just use the input vector.
    if (InVal.isUndef())
      return InVec;

    EVT VT = InVec.getValueType();

    // If we can't generate a legal BUILD_VECTOR, exit
    if (!isOperationLegal(ISD::BUILD_VECTOR, VT))
      return SDValue();

    // Check that we know which element is being inserted
    if (!isa<ConstantSDNode>(EltNo))
      return SDValue();
    unsigned Elt = EltNo->getAsZExtVal();

    // Check that the operand is a BUILD_VECTOR (or UNDEF, which can essentially
    // be converted to a BUILD_VECTOR).  Fill in the Ops vector with the
    // vector elements.
    SmallVector<SDValue, 8> Ops;
    if (InVec.getOpcode() == ISD::BUILD_VECTOR) {
      Ops.append(InVec.getNode()->op_begin(),
                 InVec.getNode()->op_end());
    } else if (InVec.isUndef()) {
      unsigned NElts = VT.getVectorNumElements();
      Ops.append(NElts, DAG.getUNDEF(InVal.getValueType()));
    } else {
      return SDValue();
    }

    // Insert the element
    if (Elt < Ops.size()) {
      // All the operands of BUILD_VECTOR must have the same type;
      // we enforce that here.
      EVT OpVT = Ops[0].getValueType();
      if (InVal.getValueType() != OpVT)
        InVal = OpVT.bitsGT(InVal.getValueType()) ?
          DAG.getNode(ISD::ANY_EXTEND, DL, OpVT, InVal) :
          DAG.getNode(ISD::TRUNCATE, DL, OpVT, InVal);
      Ops[Elt] = InVal;
    }

    // Return the new vector
    return DAG.getBuildVector(VT, DL, Ops);
  }

  // Extract_vec (Build_vector) generated by custom lowering
  // also needs to be customly combined
  case ISD::EXTRACT_VECTOR_ELT: {
    SDValue Arg = N->getOperand(0);
    if (Arg.getOpcode() == ISD::BUILD_VECTOR) {
      if (ConstantSDNode *Const = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
        unsigned Element = Const->getZExtValue();
        return Arg->getOperand(Element);
      }
    }
    if (Arg.getOpcode() == ISD::BITCAST &&
        Arg.getOperand(0).getOpcode() == ISD::BUILD_VECTOR &&
        (Arg.getOperand(0).getValueType().getVectorNumElements() ==
         Arg.getValueType().getVectorNumElements())) {
      if (ConstantSDNode *Const = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
        unsigned Element = Const->getZExtValue();
        return DAG.getNode(ISD::BITCAST, DL, N->getVTList(),
                           Arg->getOperand(0).getOperand(Element));
      }
    }
    break;
  }

  case ISD::SELECT_CC: {
    // Try common optimizations
    if (SDValue Ret = AMDGPUTargetLowering::PerformDAGCombine(N, DCI))
      return Ret;

    // fold selectcc (selectcc x, y, a, b, cc), b, a, b, seteq ->
    //      selectcc x, y, a, b, inv(cc)
    //
    // fold selectcc (selectcc x, y, a, b, cc), b, a, b, setne ->
    //      selectcc x, y, a, b, cc
    SDValue LHS = N->getOperand(0);
    if (LHS.getOpcode() != ISD::SELECT_CC) {
      return SDValue();
    }

    SDValue RHS = N->getOperand(1);
    SDValue True = N->getOperand(2);
    SDValue False = N->getOperand(3);
    ISD::CondCode NCC = cast<CondCodeSDNode>(N->getOperand(4))->get();

    if (LHS.getOperand(2).getNode() != True.getNode() ||
        LHS.getOperand(3).getNode() != False.getNode() ||
        RHS.getNode() != False.getNode()) {
      return SDValue();
    }

    switch (NCC) {
    default: return SDValue();
    case ISD::SETNE: return LHS;
    case ISD::SETEQ: {
      ISD::CondCode LHSCC = cast<CondCodeSDNode>(LHS.getOperand(4))->get();
      LHSCC = ISD::getSetCCInverse(LHSCC, LHS.getOperand(0).getValueType());
      if (DCI.isBeforeLegalizeOps() ||
          isCondCodeLegal(LHSCC, LHS.getOperand(0).getSimpleValueType()))
        return DAG.getSelectCC(DL,
                               LHS.getOperand(0),
                               LHS.getOperand(1),
                               LHS.getOperand(2),
                               LHS.getOperand(3),
                               LHSCC);
      break;
    }
    }
    return SDValue();
  }

  case AMDGPUISD::R600_EXPORT: {
    SDValue Arg = N->getOperand(1);
    if (Arg.getOpcode() != ISD::BUILD_VECTOR)
      break;

    SDValue NewArgs[8] = {
      N->getOperand(0), // Chain
      SDValue(),
      N->getOperand(2), // ArrayBase
      N->getOperand(3), // Type
      N->getOperand(4), // SWZ_X
      N->getOperand(5), // SWZ_Y
      N->getOperand(6), // SWZ_Z
      N->getOperand(7) // SWZ_W
    };
    NewArgs[1] = OptimizeSwizzle(N->getOperand(1), &NewArgs[4], DAG, DL);
    return DAG.getNode(AMDGPUISD::R600_EXPORT, DL, N->getVTList(), NewArgs);
  }
  case AMDGPUISD::TEXTURE_FETCH: {
    SDValue Arg = N->getOperand(1);
    if (Arg.getOpcode() != ISD::BUILD_VECTOR)
      break;

    SDValue NewArgs[19] = {
      N->getOperand(0),
      N->getOperand(1),
      N->getOperand(2),
      N->getOperand(3),
      N->getOperand(4),
      N->getOperand(5),
      N->getOperand(6),
      N->getOperand(7),
      N->getOperand(8),
      N->getOperand(9),
      N->getOperand(10),
      N->getOperand(11),
      N->getOperand(12),
      N->getOperand(13),
      N->getOperand(14),
      N->getOperand(15),
      N->getOperand(16),
      N->getOperand(17),
      N->getOperand(18),
    };
    NewArgs[1] = OptimizeSwizzle(N->getOperand(1), &NewArgs[2], DAG, DL);
    return DAG.getNode(AMDGPUISD::TEXTURE_FETCH, DL, N->getVTList(), NewArgs);
  }

  case ISD::LOAD: {
    LoadSDNode *LoadNode = cast<LoadSDNode>(N);
    SDValue Ptr = LoadNode->getBasePtr();
    if (LoadNode->getAddressSpace() == AMDGPUAS::PARAM_I_ADDRESS &&
         isa<ConstantSDNode>(Ptr))
      return constBufferLoad(LoadNode, AMDGPUAS::CONSTANT_BUFFER_0, DAG);
    break;
  }

  default: break;
  }

  return AMDGPUTargetLowering::PerformDAGCombine(N, DCI);
}

bool R600TargetLowering::FoldOperand(SDNode *ParentNode, unsigned SrcIdx,
                                     SDValue &Src, SDValue &Neg, SDValue &Abs,
                                     SDValue &Sel, SDValue &Imm,
                                     SelectionDAG &DAG) const {
  const R600InstrInfo *TII = Subtarget->getInstrInfo();
  if (!Src.isMachineOpcode())
    return false;

  switch (Src.getMachineOpcode()) {
  case R600::FNEG_R600:
    if (!Neg.getNode())
      return false;
    Src = Src.getOperand(0);
    Neg = DAG.getTargetConstant(1, SDLoc(ParentNode), MVT::i32);
    return true;
  case R600::FABS_R600:
    if (!Abs.getNode())
      return false;
    Src = Src.getOperand(0);
    Abs = DAG.getTargetConstant(1, SDLoc(ParentNode), MVT::i32);
    return true;
  case R600::CONST_COPY: {
    unsigned Opcode = ParentNode->getMachineOpcode();
    bool HasDst = TII->getOperandIdx(Opcode, R600::OpName::dst) > -1;

    if (!Sel.getNode())
      return false;

    SDValue CstOffset = Src.getOperand(0);
    if (ParentNode->getValueType(0).isVector())
      return false;

    // Gather constants values
    int SrcIndices[] = {
      TII->getOperandIdx(Opcode, R600::OpName::src0),
      TII->getOperandIdx(Opcode, R600::OpName::src1),
      TII->getOperandIdx(Opcode, R600::OpName::src2),
      TII->getOperandIdx(Opcode, R600::OpName::src0_X),
      TII->getOperandIdx(Opcode, R600::OpName::src0_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src0_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src0_W),
      TII->getOperandIdx(Opcode, R600::OpName::src1_X),
      TII->getOperandIdx(Opcode, R600::OpName::src1_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src1_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src1_W)
    };
    std::vector<unsigned> Consts;
    for (int OtherSrcIdx : SrcIndices) {
      int OtherSelIdx = TII->getSelIdx(Opcode, OtherSrcIdx);
      if (OtherSrcIdx < 0 || OtherSelIdx < 0)
        continue;
      if (HasDst) {
        OtherSrcIdx--;
        OtherSelIdx--;
      }
      if (RegisterSDNode *Reg =
          dyn_cast<RegisterSDNode>(ParentNode->getOperand(OtherSrcIdx))) {
        if (Reg->getReg() == R600::ALU_CONST) {
          Consts.push_back(ParentNode->getConstantOperandVal(OtherSelIdx));
        }
      }
    }

    ConstantSDNode *Cst = cast<ConstantSDNode>(CstOffset);
    Consts.push_back(Cst->getZExtValue());
    if (!TII->fitsConstReadLimitations(Consts)) {
      return false;
    }

    Sel = CstOffset;
    Src = DAG.getRegister(R600::ALU_CONST, MVT::f32);
    return true;
  }
  case R600::MOV_IMM_GLOBAL_ADDR:
    // Check if the Imm slot is used. Taken from below.
    if (Imm->getAsZExtVal())
      return false;
    Imm = Src.getOperand(0);
    Src = DAG.getRegister(R600::ALU_LITERAL_X, MVT::i32);
    return true;
  case R600::MOV_IMM_I32:
  case R600::MOV_IMM_F32: {
    unsigned ImmReg = R600::ALU_LITERAL_X;
    uint64_t ImmValue = 0;

    if (Src.getMachineOpcode() == R600::MOV_IMM_F32) {
      ConstantFPSDNode *FPC = cast<ConstantFPSDNode>(Src.getOperand(0));
      float FloatValue = FPC->getValueAPF().convertToFloat();
      if (FloatValue == 0.0) {
        ImmReg = R600::ZERO;
      } else if (FloatValue == 0.5) {
        ImmReg = R600::HALF;
      } else if (FloatValue == 1.0) {
        ImmReg = R600::ONE;
      } else {
        ImmValue = FPC->getValueAPF().bitcastToAPInt().getZExtValue();
      }
    } else {
      uint64_t Value = Src.getConstantOperandVal(0);
      if (Value == 0) {
        ImmReg = R600::ZERO;
      } else if (Value == 1) {
        ImmReg = R600::ONE_INT;
      } else {
        ImmValue = Value;
      }
    }

    // Check that we aren't already using an immediate.
    // XXX: It's possible for an instruction to have more than one
    // immediate operand, but this is not supported yet.
    if (ImmReg == R600::ALU_LITERAL_X) {
      if (!Imm.getNode())
        return false;
      ConstantSDNode *C = cast<ConstantSDNode>(Imm);
      if (C->getZExtValue())
        return false;
      Imm = DAG.getTargetConstant(ImmValue, SDLoc(ParentNode), MVT::i32);
    }
    Src = DAG.getRegister(ImmReg, MVT::i32);
    return true;
  }
  default:
    return false;
  }
}

/// Fold the instructions after selecting them
SDNode *R600TargetLowering::PostISelFolding(MachineSDNode *Node,
                                            SelectionDAG &DAG) const {
  const R600InstrInfo *TII = Subtarget->getInstrInfo();
  if (!Node->isMachineOpcode())
    return Node;

  unsigned Opcode = Node->getMachineOpcode();
  SDValue FakeOp;

  std::vector<SDValue> Ops(Node->op_begin(), Node->op_end());

  if (Opcode == R600::DOT_4) {
    int OperandIdx[] = {
      TII->getOperandIdx(Opcode, R600::OpName::src0_X),
      TII->getOperandIdx(Opcode, R600::OpName::src0_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src0_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src0_W),
      TII->getOperandIdx(Opcode, R600::OpName::src1_X),
      TII->getOperandIdx(Opcode, R600::OpName::src1_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src1_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src1_W)
        };
    int NegIdx[] = {
      TII->getOperandIdx(Opcode, R600::OpName::src0_neg_X),
      TII->getOperandIdx(Opcode, R600::OpName::src0_neg_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src0_neg_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src0_neg_W),
      TII->getOperandIdx(Opcode, R600::OpName::src1_neg_X),
      TII->getOperandIdx(Opcode, R600::OpName::src1_neg_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src1_neg_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src1_neg_W)
    };
    int AbsIdx[] = {
      TII->getOperandIdx(Opcode, R600::OpName::src0_abs_X),
      TII->getOperandIdx(Opcode, R600::OpName::src0_abs_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src0_abs_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src0_abs_W),
      TII->getOperandIdx(Opcode, R600::OpName::src1_abs_X),
      TII->getOperandIdx(Opcode, R600::OpName::src1_abs_Y),
      TII->getOperandIdx(Opcode, R600::OpName::src1_abs_Z),
      TII->getOperandIdx(Opcode, R600::OpName::src1_abs_W)
    };
    for (unsigned i = 0; i < 8; i++) {
      if (OperandIdx[i] < 0)
        return Node;
      SDValue &Src = Ops[OperandIdx[i] - 1];
      SDValue &Neg = Ops[NegIdx[i] - 1];
      SDValue &Abs = Ops[AbsIdx[i] - 1];
      bool HasDst = TII->getOperandIdx(Opcode, R600::OpName::dst) > -1;
      int SelIdx = TII->getSelIdx(Opcode, OperandIdx[i]);
      if (HasDst)
        SelIdx--;
      SDValue &Sel = (SelIdx > -1) ? Ops[SelIdx] : FakeOp;
      if (FoldOperand(Node, i, Src, Neg, Abs, Sel, FakeOp, DAG))
        return DAG.getMachineNode(Opcode, SDLoc(Node), Node->getVTList(), Ops);
    }
  } else if (Opcode == R600::REG_SEQUENCE) {
    for (unsigned i = 1, e = Node->getNumOperands(); i < e; i += 2) {
      SDValue &Src = Ops[i];
      if (FoldOperand(Node, i, Src, FakeOp, FakeOp, FakeOp, FakeOp, DAG))
        return DAG.getMachineNode(Opcode, SDLoc(Node), Node->getVTList(), Ops);
    }
  } else {
    if (!TII->hasInstrModifiers(Opcode))
      return Node;
    int OperandIdx[] = {
      TII->getOperandIdx(Opcode, R600::OpName::src0),
      TII->getOperandIdx(Opcode, R600::OpName::src1),
      TII->getOperandIdx(Opcode, R600::OpName::src2)
    };
    int NegIdx[] = {
      TII->getOperandIdx(Opcode, R600::OpName::src0_neg),
      TII->getOperandIdx(Opcode, R600::OpName::src1_neg),
      TII->getOperandIdx(Opcode, R600::OpName::src2_neg)
    };
    int AbsIdx[] = {
      TII->getOperandIdx(Opcode, R600::OpName::src0_abs),
      TII->getOperandIdx(Opcode, R600::OpName::src1_abs),
      -1
    };
    for (unsigned i = 0; i < 3; i++) {
      if (OperandIdx[i] < 0)
        return Node;
      SDValue &Src = Ops[OperandIdx[i] - 1];
      SDValue &Neg = Ops[NegIdx[i] - 1];
      SDValue FakeAbs;
      SDValue &Abs = (AbsIdx[i] > -1) ? Ops[AbsIdx[i] - 1] : FakeAbs;
      bool HasDst = TII->getOperandIdx(Opcode, R600::OpName::dst) > -1;
      int SelIdx = TII->getSelIdx(Opcode, OperandIdx[i]);
      int ImmIdx = TII->getOperandIdx(Opcode, R600::OpName::literal);
      if (HasDst) {
        SelIdx--;
        ImmIdx--;
      }
      SDValue &Sel = (SelIdx > -1) ? Ops[SelIdx] : FakeOp;
      SDValue &Imm = Ops[ImmIdx];
      if (FoldOperand(Node, i, Src, Neg, Abs, Sel, Imm, DAG))
        return DAG.getMachineNode(Opcode, SDLoc(Node), Node->getVTList(), Ops);
    }
  }

  return Node;
}

TargetLowering::AtomicExpansionKind
R600TargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  switch (RMW->getOperation()) {
  case AtomicRMWInst::UIncWrap:
  case AtomicRMWInst::UDecWrap:
    // FIXME: Cayman at least appears to have instructions for this, but the
    // instruction defintions appear to be missing.
    return AtomicExpansionKind::CmpXChg;
  default:
    break;
  }

  return AMDGPUTargetLowering::shouldExpandAtomicRMWInIR(RMW);
}
