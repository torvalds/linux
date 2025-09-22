//===-- AMDGPUISelDAGToDAG.cpp - A dag to dag inst selector for AMDGPU ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//==-----------------------------------------------------------------------===//
//
/// \file
/// Defines an instruction selector for the AMDGPU target.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUISelDAGToDAG.h"
#include "AMDGPU.h"
#include "AMDGPUInstrInfo.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUTargetMachine.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "MCTargetDesc/R600MCTargetDesc.h"
#include "R600RegisterInfo.h"
#include "SIISelLowering.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/ErrorHandling.h"

#ifdef EXPENSIVE_CHECKS
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#endif

#define DEBUG_TYPE "amdgpu-isel"

using namespace llvm;

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

namespace {
static SDValue stripBitcast(SDValue Val) {
  return Val.getOpcode() == ISD::BITCAST ? Val.getOperand(0) : Val;
}

// Figure out if this is really an extract of the high 16-bits of a dword.
static bool isExtractHiElt(SDValue In, SDValue &Out) {
  In = stripBitcast(In);

  if (In.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
    if (ConstantSDNode *Idx = dyn_cast<ConstantSDNode>(In.getOperand(1))) {
      if (!Idx->isOne())
        return false;
      Out = In.getOperand(0);
      return true;
    }
  }

  if (In.getOpcode() != ISD::TRUNCATE)
    return false;

  SDValue Srl = In.getOperand(0);
  if (Srl.getOpcode() == ISD::SRL) {
    if (ConstantSDNode *ShiftAmt = dyn_cast<ConstantSDNode>(Srl.getOperand(1))) {
      if (ShiftAmt->getZExtValue() == 16) {
        Out = stripBitcast(Srl.getOperand(0));
        return true;
      }
    }
  }

  return false;
}

// Look through operations that obscure just looking at the low 16-bits of the
// same register.
static SDValue stripExtractLoElt(SDValue In) {
  if (In.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
    SDValue Idx = In.getOperand(1);
    if (isNullConstant(Idx) && In.getValueSizeInBits() <= 32)
      return In.getOperand(0);
  }

  if (In.getOpcode() == ISD::TRUNCATE) {
    SDValue Src = In.getOperand(0);
    if (Src.getValueType().getSizeInBits() == 32)
      return stripBitcast(Src);
  }

  return In;
}

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(AMDGPUDAGToDAGISelLegacy, "amdgpu-isel",
                      "AMDGPU DAG->DAG Pattern Instruction Selection", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(AMDGPUArgumentUsageInfo)
INITIALIZE_PASS_DEPENDENCY(AMDGPUPerfHintAnalysis)
INITIALIZE_PASS_DEPENDENCY(UniformityInfoWrapperPass)
#ifdef EXPENSIVE_CHECKS
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
#endif
INITIALIZE_PASS_END(AMDGPUDAGToDAGISelLegacy, "amdgpu-isel",
                    "AMDGPU DAG->DAG Pattern Instruction Selection", false,
                    false)

/// This pass converts a legalized DAG into a AMDGPU-specific
// DAG, ready for instruction scheduling.
FunctionPass *llvm::createAMDGPUISelDag(TargetMachine &TM,
                                        CodeGenOptLevel OptLevel) {
  return new AMDGPUDAGToDAGISelLegacy(TM, OptLevel);
}

AMDGPUDAGToDAGISel::AMDGPUDAGToDAGISel(TargetMachine &TM,
                                       CodeGenOptLevel OptLevel)
    : SelectionDAGISel(TM, OptLevel) {
  EnableLateStructurizeCFG = AMDGPUTargetMachine::EnableLateStructurizeCFG;
}

bool AMDGPUDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<GCNSubtarget>();
  Subtarget->checkSubtargetFeatures(MF.getFunction());
  Mode = SIModeRegisterDefaults(MF.getFunction(), *Subtarget);
  return SelectionDAGISel::runOnMachineFunction(MF);
}

bool AMDGPUDAGToDAGISel::fp16SrcZerosHighBits(unsigned Opc) const {
  // XXX - only need to list legal operations.
  switch (Opc) {
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::FCANONICALIZE:
  case ISD::UINT_TO_FP:
  case ISD::SINT_TO_FP:
  case ISD::FABS:
    // Fabs is lowered to a bit operation, but it's an and which will clear the
    // high bits anyway.
  case ISD::FSQRT:
  case ISD::FSIN:
  case ISD::FCOS:
  case ISD::FPOWI:
  case ISD::FPOW:
  case ISD::FLOG:
  case ISD::FLOG2:
  case ISD::FLOG10:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FCEIL:
  case ISD::FTRUNC:
  case ISD::FRINT:
  case ISD::FNEARBYINT:
  case ISD::FROUNDEVEN:
  case ISD::FROUND:
  case ISD::FFLOOR:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FLDEXP:
  case AMDGPUISD::FRACT:
  case AMDGPUISD::CLAMP:
  case AMDGPUISD::COS_HW:
  case AMDGPUISD::SIN_HW:
  case AMDGPUISD::FMIN3:
  case AMDGPUISD::FMAX3:
  case AMDGPUISD::FMED3:
  case AMDGPUISD::FMAD_FTZ:
  case AMDGPUISD::RCP:
  case AMDGPUISD::RSQ:
  case AMDGPUISD::RCP_IFLAG:
    // On gfx10, all 16-bit instructions preserve the high bits.
    return Subtarget->getGeneration() <= AMDGPUSubtarget::GFX9;
  case ISD::FP_ROUND:
    // We may select fptrunc (fma/mad) to mad_mixlo, which does not zero the
    // high bits on gfx9.
    // TODO: If we had the source node we could see if the source was fma/mad
    return Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS;
  case ISD::FMA:
  case ISD::FMAD:
  case AMDGPUISD::DIV_FIXUP:
    return Subtarget->getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS;
  default:
    // fcopysign, select and others may be lowered to 32-bit bit operations
    // which don't zero the high bits.
    return false;
  }
}

bool AMDGPUDAGToDAGISelLegacy::runOnMachineFunction(MachineFunction &MF) {
#ifdef EXPENSIVE_CHECKS
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  for (auto &L : LI->getLoopsInPreorder()) {
    assert(L->isLCSSAForm(DT));
  }
#endif
  return SelectionDAGISelLegacy::runOnMachineFunction(MF);
}

void AMDGPUDAGToDAGISelLegacy::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AMDGPUArgumentUsageInfo>();
  AU.addRequired<UniformityInfoWrapperPass>();
#ifdef EXPENSIVE_CHECKS
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
#endif
  SelectionDAGISelLegacy::getAnalysisUsage(AU);
}

bool AMDGPUDAGToDAGISel::matchLoadD16FromBuildVector(SDNode *N) const {
  assert(Subtarget->d16PreservesUnusedBits());
  MVT VT = N->getValueType(0).getSimpleVT();
  if (VT != MVT::v2i16 && VT != MVT::v2f16)
    return false;

  SDValue Lo = N->getOperand(0);
  SDValue Hi = N->getOperand(1);

  LoadSDNode *LdHi = dyn_cast<LoadSDNode>(stripBitcast(Hi));

  // build_vector lo, (load ptr) -> load_d16_hi ptr, lo
  // build_vector lo, (zextload ptr from i8) -> load_d16_hi_u8 ptr, lo
  // build_vector lo, (sextload ptr from i8) -> load_d16_hi_i8 ptr, lo

  // Need to check for possible indirect dependencies on the other half of the
  // vector to avoid introducing a cycle.
  if (LdHi && Hi.hasOneUse() && !LdHi->isPredecessorOf(Lo.getNode())) {
    SDVTList VTList = CurDAG->getVTList(VT, MVT::Other);

    SDValue TiedIn = CurDAG->getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), VT, Lo);
    SDValue Ops[] = {
      LdHi->getChain(), LdHi->getBasePtr(), TiedIn
    };

    unsigned LoadOp = AMDGPUISD::LOAD_D16_HI;
    if (LdHi->getMemoryVT() == MVT::i8) {
      LoadOp = LdHi->getExtensionType() == ISD::SEXTLOAD ?
        AMDGPUISD::LOAD_D16_HI_I8 : AMDGPUISD::LOAD_D16_HI_U8;
    } else {
      assert(LdHi->getMemoryVT() == MVT::i16);
    }

    SDValue NewLoadHi =
      CurDAG->getMemIntrinsicNode(LoadOp, SDLoc(LdHi), VTList,
                                  Ops, LdHi->getMemoryVT(),
                                  LdHi->getMemOperand());

    CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), NewLoadHi);
    CurDAG->ReplaceAllUsesOfValueWith(SDValue(LdHi, 1), NewLoadHi.getValue(1));
    return true;
  }

  // build_vector (load ptr), hi -> load_d16_lo ptr, hi
  // build_vector (zextload ptr from i8), hi -> load_d16_lo_u8 ptr, hi
  // build_vector (sextload ptr from i8), hi -> load_d16_lo_i8 ptr, hi
  LoadSDNode *LdLo = dyn_cast<LoadSDNode>(stripBitcast(Lo));
  if (LdLo && Lo.hasOneUse()) {
    SDValue TiedIn = getHi16Elt(Hi);
    if (!TiedIn || LdLo->isPredecessorOf(TiedIn.getNode()))
      return false;

    SDVTList VTList = CurDAG->getVTList(VT, MVT::Other);
    unsigned LoadOp = AMDGPUISD::LOAD_D16_LO;
    if (LdLo->getMemoryVT() == MVT::i8) {
      LoadOp = LdLo->getExtensionType() == ISD::SEXTLOAD ?
        AMDGPUISD::LOAD_D16_LO_I8 : AMDGPUISD::LOAD_D16_LO_U8;
    } else {
      assert(LdLo->getMemoryVT() == MVT::i16);
    }

    TiedIn = CurDAG->getNode(ISD::BITCAST, SDLoc(N), VT, TiedIn);

    SDValue Ops[] = {
      LdLo->getChain(), LdLo->getBasePtr(), TiedIn
    };

    SDValue NewLoadLo =
      CurDAG->getMemIntrinsicNode(LoadOp, SDLoc(LdLo), VTList,
                                  Ops, LdLo->getMemoryVT(),
                                  LdLo->getMemOperand());

    CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), NewLoadLo);
    CurDAG->ReplaceAllUsesOfValueWith(SDValue(LdLo, 1), NewLoadLo.getValue(1));
    return true;
  }

  return false;
}

void AMDGPUDAGToDAGISel::PreprocessISelDAG() {
  if (!Subtarget->d16PreservesUnusedBits())
    return;

  SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_end();

  bool MadeChange = false;
  while (Position != CurDAG->allnodes_begin()) {
    SDNode *N = &*--Position;
    if (N->use_empty())
      continue;

    switch (N->getOpcode()) {
    case ISD::BUILD_VECTOR:
      // TODO: Match load d16 from shl (extload:i16), 16
      MadeChange |= matchLoadD16FromBuildVector(N);
      break;
    default:
      break;
    }
  }

  if (MadeChange) {
    CurDAG->RemoveDeadNodes();
    LLVM_DEBUG(dbgs() << "After PreProcess:\n";
               CurDAG->dump(););
  }
}

bool AMDGPUDAGToDAGISel::isInlineImmediate(const SDNode *N) const {
  if (N->isUndef())
    return true;

  const SIInstrInfo *TII = Subtarget->getInstrInfo();
  if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(N))
    return TII->isInlineConstant(C->getAPIntValue());

  if (const ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(N))
    return TII->isInlineConstant(C->getValueAPF());

  return false;
}

/// Determine the register class for \p OpNo
/// \returns The register class of the virtual register that will be used for
/// the given operand number \OpNo or NULL if the register class cannot be
/// determined.
const TargetRegisterClass *AMDGPUDAGToDAGISel::getOperandRegClass(SDNode *N,
                                                          unsigned OpNo) const {
  if (!N->isMachineOpcode()) {
    if (N->getOpcode() == ISD::CopyToReg) {
      Register Reg = cast<RegisterSDNode>(N->getOperand(1))->getReg();
      if (Reg.isVirtual()) {
        MachineRegisterInfo &MRI = CurDAG->getMachineFunction().getRegInfo();
        return MRI.getRegClass(Reg);
      }

      const SIRegisterInfo *TRI
        = static_cast<const GCNSubtarget *>(Subtarget)->getRegisterInfo();
      return TRI->getPhysRegBaseClass(Reg);
    }

    return nullptr;
  }

  switch (N->getMachineOpcode()) {
  default: {
    const MCInstrDesc &Desc =
        Subtarget->getInstrInfo()->get(N->getMachineOpcode());
    unsigned OpIdx = Desc.getNumDefs() + OpNo;
    if (OpIdx >= Desc.getNumOperands())
      return nullptr;
    int RegClass = Desc.operands()[OpIdx].RegClass;
    if (RegClass == -1)
      return nullptr;

    return Subtarget->getRegisterInfo()->getRegClass(RegClass);
  }
  case AMDGPU::REG_SEQUENCE: {
    unsigned RCID = N->getConstantOperandVal(0);
    const TargetRegisterClass *SuperRC =
        Subtarget->getRegisterInfo()->getRegClass(RCID);

    SDValue SubRegOp = N->getOperand(OpNo + 1);
    unsigned SubRegIdx = SubRegOp->getAsZExtVal();
    return Subtarget->getRegisterInfo()->getSubClassWithSubReg(SuperRC,
                                                              SubRegIdx);
  }
  }
}

SDNode *AMDGPUDAGToDAGISel::glueCopyToOp(SDNode *N, SDValue NewChain,
                                         SDValue Glue) const {
  SmallVector <SDValue, 8> Ops;
  Ops.push_back(NewChain); // Replace the chain.
  for (unsigned i = 1, e = N->getNumOperands(); i != e; ++i)
    Ops.push_back(N->getOperand(i));

  Ops.push_back(Glue);
  return CurDAG->MorphNodeTo(N, N->getOpcode(), N->getVTList(), Ops);
}

SDNode *AMDGPUDAGToDAGISel::glueCopyToM0(SDNode *N, SDValue Val) const {
  const SITargetLowering& Lowering =
    *static_cast<const SITargetLowering*>(getTargetLowering());

  assert(N->getOperand(0).getValueType() == MVT::Other && "Expected chain");

  SDValue M0 = Lowering.copyToM0(*CurDAG, N->getOperand(0), SDLoc(N), Val);
  return glueCopyToOp(N, M0, M0.getValue(1));
}

SDNode *AMDGPUDAGToDAGISel::glueCopyToM0LDSInit(SDNode *N) const {
  unsigned AS = cast<MemSDNode>(N)->getAddressSpace();
  if (AS == AMDGPUAS::LOCAL_ADDRESS) {
    if (Subtarget->ldsRequiresM0Init())
      return glueCopyToM0(N, CurDAG->getTargetConstant(-1, SDLoc(N), MVT::i32));
  } else if (AS == AMDGPUAS::REGION_ADDRESS) {
    MachineFunction &MF = CurDAG->getMachineFunction();
    unsigned Value = MF.getInfo<SIMachineFunctionInfo>()->getGDSSize();
    return
        glueCopyToM0(N, CurDAG->getTargetConstant(Value, SDLoc(N), MVT::i32));
  }
  return N;
}

MachineSDNode *AMDGPUDAGToDAGISel::buildSMovImm64(SDLoc &DL, uint64_t Imm,
                                                  EVT VT) const {
  SDNode *Lo = CurDAG->getMachineNode(
      AMDGPU::S_MOV_B32, DL, MVT::i32,
      CurDAG->getTargetConstant(Imm & 0xFFFFFFFF, DL, MVT::i32));
  SDNode *Hi =
      CurDAG->getMachineNode(AMDGPU::S_MOV_B32, DL, MVT::i32,
                             CurDAG->getTargetConstant(Imm >> 32, DL, MVT::i32));
  const SDValue Ops[] = {
      CurDAG->getTargetConstant(AMDGPU::SReg_64RegClassID, DL, MVT::i32),
      SDValue(Lo, 0), CurDAG->getTargetConstant(AMDGPU::sub0, DL, MVT::i32),
      SDValue(Hi, 0), CurDAG->getTargetConstant(AMDGPU::sub1, DL, MVT::i32)};

  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL, VT, Ops);
}

void AMDGPUDAGToDAGISel::SelectBuildVector(SDNode *N, unsigned RegClassID) {
  EVT VT = N->getValueType(0);
  unsigned NumVectorElts = VT.getVectorNumElements();
  EVT EltVT = VT.getVectorElementType();
  SDLoc DL(N);
  SDValue RegClass = CurDAG->getTargetConstant(RegClassID, DL, MVT::i32);

  if (NumVectorElts == 1) {
    CurDAG->SelectNodeTo(N, AMDGPU::COPY_TO_REGCLASS, EltVT, N->getOperand(0),
                         RegClass);
    return;
  }

  assert(NumVectorElts <= 32 && "Vectors with more than 32 elements not "
                                  "supported yet");
  // 32 = Max Num Vector Elements
  // 2 = 2 REG_SEQUENCE operands per element (value, subreg index)
  // 1 = Vector Register Class
  SmallVector<SDValue, 32 * 2 + 1> RegSeqArgs(NumVectorElts * 2 + 1);

  bool IsGCN = CurDAG->getSubtarget().getTargetTriple().getArch() ==
               Triple::amdgcn;
  RegSeqArgs[0] = CurDAG->getTargetConstant(RegClassID, DL, MVT::i32);
  bool IsRegSeq = true;
  unsigned NOps = N->getNumOperands();
  for (unsigned i = 0; i < NOps; i++) {
    // XXX: Why is this here?
    if (isa<RegisterSDNode>(N->getOperand(i))) {
      IsRegSeq = false;
      break;
    }
    unsigned Sub = IsGCN ? SIRegisterInfo::getSubRegFromChannel(i)
                         : R600RegisterInfo::getSubRegFromChannel(i);
    RegSeqArgs[1 + (2 * i)] = N->getOperand(i);
    RegSeqArgs[1 + (2 * i) + 1] = CurDAG->getTargetConstant(Sub, DL, MVT::i32);
  }
  if (NOps != NumVectorElts) {
    // Fill in the missing undef elements if this was a scalar_to_vector.
    assert(N->getOpcode() == ISD::SCALAR_TO_VECTOR && NOps < NumVectorElts);
    MachineSDNode *ImpDef = CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF,
                                                   DL, EltVT);
    for (unsigned i = NOps; i < NumVectorElts; ++i) {
      unsigned Sub = IsGCN ? SIRegisterInfo::getSubRegFromChannel(i)
                           : R600RegisterInfo::getSubRegFromChannel(i);
      RegSeqArgs[1 + (2 * i)] = SDValue(ImpDef, 0);
      RegSeqArgs[1 + (2 * i) + 1] =
          CurDAG->getTargetConstant(Sub, DL, MVT::i32);
    }
  }

  if (!IsRegSeq)
    SelectCode(N);
  CurDAG->SelectNodeTo(N, AMDGPU::REG_SEQUENCE, N->getVTList(), RegSeqArgs);
}

void AMDGPUDAGToDAGISel::Select(SDNode *N) {
  unsigned int Opc = N->getOpcode();
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;   // Already selected.
  }

  // isa<MemSDNode> almost works but is slightly too permissive for some DS
  // intrinsics.
  if (Opc == ISD::LOAD || Opc == ISD::STORE || isa<AtomicSDNode>(N)) {
    N = glueCopyToM0LDSInit(N);
    SelectCode(N);
    return;
  }

  switch (Opc) {
  default:
    break;
  // We are selecting i64 ADD here instead of custom lower it during
  // DAG legalization, so we can fold some i64 ADDs used for address
  // calculation into the LOAD and STORE instructions.
  case ISD::ADDC:
  case ISD::ADDE:
  case ISD::SUBC:
  case ISD::SUBE: {
    if (N->getValueType(0) != MVT::i64)
      break;

    SelectADD_SUB_I64(N);
    return;
  }
  case ISD::UADDO_CARRY:
  case ISD::USUBO_CARRY:
    if (N->getValueType(0) != MVT::i32)
      break;

    SelectAddcSubb(N);
    return;
  case ISD::UADDO:
  case ISD::USUBO: {
    SelectUADDO_USUBO(N);
    return;
  }
  case AMDGPUISD::FMUL_W_CHAIN: {
    SelectFMUL_W_CHAIN(N);
    return;
  }
  case AMDGPUISD::FMA_W_CHAIN: {
    SelectFMA_W_CHAIN(N);
    return;
  }

  case ISD::SCALAR_TO_VECTOR:
  case ISD::BUILD_VECTOR: {
    EVT VT = N->getValueType(0);
    unsigned NumVectorElts = VT.getVectorNumElements();
    if (VT.getScalarSizeInBits() == 16) {
      if (Opc == ISD::BUILD_VECTOR && NumVectorElts == 2) {
        if (SDNode *Packed = packConstantV2I16(N, *CurDAG)) {
          ReplaceNode(N, Packed);
          return;
        }
      }

      break;
    }

    assert(VT.getVectorElementType().bitsEq(MVT::i32));
    unsigned RegClassID =
        SIRegisterInfo::getSGPRClassForBitWidth(NumVectorElts * 32)->getID();
    SelectBuildVector(N, RegClassID);
    return;
  }
  case ISD::BUILD_PAIR: {
    SDValue RC, SubReg0, SubReg1;
    SDLoc DL(N);
    if (N->getValueType(0) == MVT::i128) {
      RC = CurDAG->getTargetConstant(AMDGPU::SGPR_128RegClassID, DL, MVT::i32);
      SubReg0 = CurDAG->getTargetConstant(AMDGPU::sub0_sub1, DL, MVT::i32);
      SubReg1 = CurDAG->getTargetConstant(AMDGPU::sub2_sub3, DL, MVT::i32);
    } else if (N->getValueType(0) == MVT::i64) {
      RC = CurDAG->getTargetConstant(AMDGPU::SReg_64RegClassID, DL, MVT::i32);
      SubReg0 = CurDAG->getTargetConstant(AMDGPU::sub0, DL, MVT::i32);
      SubReg1 = CurDAG->getTargetConstant(AMDGPU::sub1, DL, MVT::i32);
    } else {
      llvm_unreachable("Unhandled value type for BUILD_PAIR");
    }
    const SDValue Ops[] = { RC, N->getOperand(0), SubReg0,
                            N->getOperand(1), SubReg1 };
    ReplaceNode(N, CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL,
                                          N->getValueType(0), Ops));
    return;
  }

  case ISD::Constant:
  case ISD::ConstantFP: {
    if (N->getValueType(0).getSizeInBits() != 64 || isInlineImmediate(N))
      break;

    uint64_t Imm;
    if (ConstantFPSDNode *FP = dyn_cast<ConstantFPSDNode>(N)) {
      Imm = FP->getValueAPF().bitcastToAPInt().getZExtValue();
      if (AMDGPU::isValid32BitLiteral(Imm, true))
        break;
    } else {
      ConstantSDNode *C = cast<ConstantSDNode>(N);
      Imm = C->getZExtValue();
      if (AMDGPU::isValid32BitLiteral(Imm, false))
        break;
    }

    SDLoc DL(N);
    ReplaceNode(N, buildSMovImm64(DL, Imm, N->getValueType(0)));
    return;
  }
  case AMDGPUISD::BFE_I32:
  case AMDGPUISD::BFE_U32: {
    // There is a scalar version available, but unlike the vector version which
    // has a separate operand for the offset and width, the scalar version packs
    // the width and offset into a single operand. Try to move to the scalar
    // version if the offsets are constant, so that we can try to keep extended
    // loads of kernel arguments in SGPRs.

    // TODO: Technically we could try to pattern match scalar bitshifts of
    // dynamic values, but it's probably not useful.
    ConstantSDNode *Offset = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!Offset)
      break;

    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(N->getOperand(2));
    if (!Width)
      break;

    bool Signed = Opc == AMDGPUISD::BFE_I32;

    uint32_t OffsetVal = Offset->getZExtValue();
    uint32_t WidthVal = Width->getZExtValue();

    ReplaceNode(N, getBFE32(Signed, SDLoc(N), N->getOperand(0), OffsetVal,
                            WidthVal));
    return;
  }
  case AMDGPUISD::DIV_SCALE: {
    SelectDIV_SCALE(N);
    return;
  }
  case AMDGPUISD::MAD_I64_I32:
  case AMDGPUISD::MAD_U64_U32: {
    SelectMAD_64_32(N);
    return;
  }
  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI:
    return SelectMUL_LOHI(N);
  case ISD::CopyToReg: {
    const SITargetLowering& Lowering =
      *static_cast<const SITargetLowering*>(getTargetLowering());
    N = Lowering.legalizeTargetIndependentNode(N, *CurDAG);
    break;
  }
  case ISD::AND:
  case ISD::SRL:
  case ISD::SRA:
  case ISD::SIGN_EXTEND_INREG:
    if (N->getValueType(0) != MVT::i32)
      break;

    SelectS_BFE(N);
    return;
  case ISD::BRCOND:
    SelectBRCOND(N);
    return;
  case ISD::FP_EXTEND:
    SelectFP_EXTEND(N);
    return;
  case AMDGPUISD::CVT_PKRTZ_F16_F32:
  case AMDGPUISD::CVT_PKNORM_I16_F32:
  case AMDGPUISD::CVT_PKNORM_U16_F32:
  case AMDGPUISD::CVT_PK_U16_U32:
  case AMDGPUISD::CVT_PK_I16_I32: {
    // Hack around using a legal type if f16 is illegal.
    if (N->getValueType(0) == MVT::i32) {
      MVT NewVT = Opc == AMDGPUISD::CVT_PKRTZ_F16_F32 ? MVT::v2f16 : MVT::v2i16;
      N = CurDAG->MorphNodeTo(N, N->getOpcode(), CurDAG->getVTList(NewVT),
                              { N->getOperand(0), N->getOperand(1) });
      SelectCode(N);
      return;
    }

    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    SelectINTRINSIC_W_CHAIN(N);
    return;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    SelectINTRINSIC_WO_CHAIN(N);
    return;
  }
  case ISD::INTRINSIC_VOID: {
    SelectINTRINSIC_VOID(N);
    return;
  }
  case AMDGPUISD::WAVE_ADDRESS: {
    SelectWAVE_ADDRESS(N);
    return;
  }
  case ISD::STACKRESTORE: {
    SelectSTACKRESTORE(N);
    return;
  }
  }

  SelectCode(N);
}

bool AMDGPUDAGToDAGISel::isUniformBr(const SDNode *N) const {
  const BasicBlock *BB = FuncInfo->MBB->getBasicBlock();
  const Instruction *Term = BB->getTerminator();
  return Term->getMetadata("amdgpu.uniform") ||
         Term->getMetadata("structurizecfg.uniform");
}

bool AMDGPUDAGToDAGISel::isUnneededShiftMask(const SDNode *N,
                                             unsigned ShAmtBits) const {
  assert(N->getOpcode() == ISD::AND);

  const APInt &RHS = N->getConstantOperandAPInt(1);
  if (RHS.countr_one() >= ShAmtBits)
    return true;

  const APInt &LHSKnownZeros = CurDAG->computeKnownBits(N->getOperand(0)).Zero;
  return (LHSKnownZeros | RHS).countr_one() >= ShAmtBits;
}

static bool getBaseWithOffsetUsingSplitOR(SelectionDAG &DAG, SDValue Addr,
                                          SDValue &N0, SDValue &N1) {
  if (Addr.getValueType() == MVT::i64 && Addr.getOpcode() == ISD::BITCAST &&
      Addr.getOperand(0).getOpcode() == ISD::BUILD_VECTOR) {
    // As we split 64-bit `or` earlier, it's complicated pattern to match, i.e.
    // (i64 (bitcast (v2i32 (build_vector
    //                        (or (extract_vector_elt V, 0), OFFSET),
    //                        (extract_vector_elt V, 1)))))
    SDValue Lo = Addr.getOperand(0).getOperand(0);
    if (Lo.getOpcode() == ISD::OR && DAG.isBaseWithConstantOffset(Lo)) {
      SDValue BaseLo = Lo.getOperand(0);
      SDValue BaseHi = Addr.getOperand(0).getOperand(1);
      // Check that split base (Lo and Hi) are extracted from the same one.
      if (BaseLo.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
          BaseHi.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
          BaseLo.getOperand(0) == BaseHi.getOperand(0) &&
          // Lo is statically extracted from index 0.
          isa<ConstantSDNode>(BaseLo.getOperand(1)) &&
          BaseLo.getConstantOperandVal(1) == 0 &&
          // Hi is statically extracted from index 0.
          isa<ConstantSDNode>(BaseHi.getOperand(1)) &&
          BaseHi.getConstantOperandVal(1) == 1) {
        N0 = BaseLo.getOperand(0).getOperand(0);
        N1 = Lo.getOperand(1);
        return true;
      }
    }
  }
  return false;
}

bool AMDGPUDAGToDAGISel::isBaseWithConstantOffset64(SDValue Addr, SDValue &LHS,
                                                    SDValue &RHS) const {
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    LHS = Addr.getOperand(0);
    RHS = Addr.getOperand(1);
    return true;
  }

  if (getBaseWithOffsetUsingSplitOR(*CurDAG, Addr, LHS, RHS)) {
    assert(LHS && RHS && isa<ConstantSDNode>(RHS));
    return true;
  }

  return false;
}

StringRef AMDGPUDAGToDAGISelLegacy::getPassName() const {
  return "AMDGPU DAG->DAG Pattern Instruction Selection";
}

AMDGPUISelDAGToDAGPass::AMDGPUISelDAGToDAGPass(TargetMachine &TM)
    : SelectionDAGISelPass(
          std::make_unique<AMDGPUDAGToDAGISel>(TM, TM.getOptLevel())) {}

PreservedAnalyses
AMDGPUISelDAGToDAGPass::run(MachineFunction &MF,
                            MachineFunctionAnalysisManager &MFAM) {
#ifdef EXPENSIVE_CHECKS
  auto &FAM = MFAM.getResult<FunctionAnalysisManagerMachineFunctionProxy>(MF)
                  .getManager();
  auto &F = MF.getFunction();
  DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
  for (auto &L : LI.getLoopsInPreorder())
    assert(L->isLCSSAForm(DT) && "Loop is not in LCSSA form!");
#endif
  return SelectionDAGISelPass::run(MF, MFAM);
}

//===----------------------------------------------------------------------===//
// Complex Patterns
//===----------------------------------------------------------------------===//

bool AMDGPUDAGToDAGISel::SelectADDRVTX_READ(SDValue Addr, SDValue &Base,
                                            SDValue &Offset) {
  return false;
}

bool AMDGPUDAGToDAGISel::SelectADDRIndirect(SDValue Addr, SDValue &Base,
                                            SDValue &Offset) {
  ConstantSDNode *C;
  SDLoc DL(Addr);

  if ((C = dyn_cast<ConstantSDNode>(Addr))) {
    Base = CurDAG->getRegister(R600::INDIRECT_BASE_ADDR, MVT::i32);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else if ((Addr.getOpcode() == AMDGPUISD::DWORDADDR) &&
             (C = dyn_cast<ConstantSDNode>(Addr.getOperand(0)))) {
    Base = CurDAG->getRegister(R600::INDIRECT_BASE_ADDR, MVT::i32);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else if ((Addr.getOpcode() == ISD::ADD || Addr.getOpcode() == ISD::OR) &&
            (C = dyn_cast<ConstantSDNode>(Addr.getOperand(1)))) {
    Base = Addr.getOperand(0);
    Offset = CurDAG->getTargetConstant(C->getZExtValue(), DL, MVT::i32);
  } else {
    Base = Addr;
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  }

  return true;
}

SDValue AMDGPUDAGToDAGISel::getMaterializedScalarImm32(int64_t Val,
                                                       const SDLoc &DL) const {
  SDNode *Mov = CurDAG->getMachineNode(
    AMDGPU::S_MOV_B32, DL, MVT::i32,
    CurDAG->getTargetConstant(Val, DL, MVT::i32));
  return SDValue(Mov, 0);
}

// FIXME: Should only handle uaddo_carry/usubo_carry
void AMDGPUDAGToDAGISel::SelectADD_SUB_I64(SDNode *N) {
  SDLoc DL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  unsigned Opcode = N->getOpcode();
  bool ConsumeCarry = (Opcode == ISD::ADDE || Opcode == ISD::SUBE);
  bool ProduceCarry =
      ConsumeCarry || Opcode == ISD::ADDC || Opcode == ISD::SUBC;
  bool IsAdd = Opcode == ISD::ADD || Opcode == ISD::ADDC || Opcode == ISD::ADDE;

  SDValue Sub0 = CurDAG->getTargetConstant(AMDGPU::sub0, DL, MVT::i32);
  SDValue Sub1 = CurDAG->getTargetConstant(AMDGPU::sub1, DL, MVT::i32);

  SDNode *Lo0 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, LHS, Sub0);
  SDNode *Hi0 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, LHS, Sub1);

  SDNode *Lo1 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, RHS, Sub0);
  SDNode *Hi1 = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                       DL, MVT::i32, RHS, Sub1);

  SDVTList VTList = CurDAG->getVTList(MVT::i32, MVT::Glue);

  static const unsigned OpcMap[2][2][2] = {
      {{AMDGPU::S_SUB_U32, AMDGPU::S_ADD_U32},
       {AMDGPU::V_SUB_CO_U32_e32, AMDGPU::V_ADD_CO_U32_e32}},
      {{AMDGPU::S_SUBB_U32, AMDGPU::S_ADDC_U32},
       {AMDGPU::V_SUBB_U32_e32, AMDGPU::V_ADDC_U32_e32}}};

  unsigned Opc = OpcMap[0][N->isDivergent()][IsAdd];
  unsigned CarryOpc = OpcMap[1][N->isDivergent()][IsAdd];

  SDNode *AddLo;
  if (!ConsumeCarry) {
    SDValue Args[] = { SDValue(Lo0, 0), SDValue(Lo1, 0) };
    AddLo = CurDAG->getMachineNode(Opc, DL, VTList, Args);
  } else {
    SDValue Args[] = { SDValue(Lo0, 0), SDValue(Lo1, 0), N->getOperand(2) };
    AddLo = CurDAG->getMachineNode(CarryOpc, DL, VTList, Args);
  }
  SDValue AddHiArgs[] = {
    SDValue(Hi0, 0),
    SDValue(Hi1, 0),
    SDValue(AddLo, 1)
  };
  SDNode *AddHi = CurDAG->getMachineNode(CarryOpc, DL, VTList, AddHiArgs);

  SDValue RegSequenceArgs[] = {
    CurDAG->getTargetConstant(AMDGPU::SReg_64RegClassID, DL, MVT::i32),
    SDValue(AddLo,0),
    Sub0,
    SDValue(AddHi,0),
    Sub1,
  };
  SDNode *RegSequence = CurDAG->getMachineNode(AMDGPU::REG_SEQUENCE, DL,
                                               MVT::i64, RegSequenceArgs);

  if (ProduceCarry) {
    // Replace the carry-use
    ReplaceUses(SDValue(N, 1), SDValue(AddHi, 1));
  }

  // Replace the remaining uses.
  ReplaceNode(N, RegSequence);
}

void AMDGPUDAGToDAGISel::SelectAddcSubb(SDNode *N) {
  SDLoc DL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue CI = N->getOperand(2);

  if (N->isDivergent()) {
    unsigned Opc = N->getOpcode() == ISD::UADDO_CARRY ? AMDGPU::V_ADDC_U32_e64
                                                      : AMDGPU::V_SUBB_U32_e64;
    CurDAG->SelectNodeTo(
        N, Opc, N->getVTList(),
        {LHS, RHS, CI,
         CurDAG->getTargetConstant(0, {}, MVT::i1) /*clamp bit*/});
  } else {
    unsigned Opc = N->getOpcode() == ISD::UADDO_CARRY ? AMDGPU::S_ADD_CO_PSEUDO
                                                      : AMDGPU::S_SUB_CO_PSEUDO;
    CurDAG->SelectNodeTo(N, Opc, N->getVTList(), {LHS, RHS, CI});
  }
}

void AMDGPUDAGToDAGISel::SelectUADDO_USUBO(SDNode *N) {
  // The name of the opcodes are misleading. v_add_i32/v_sub_i32 have unsigned
  // carry out despite the _i32 name. These were renamed in VI to _U32.
  // FIXME: We should probably rename the opcodes here.
  bool IsAdd = N->getOpcode() == ISD::UADDO;
  bool IsVALU = N->isDivergent();

  for (SDNode::use_iterator UI = N->use_begin(), E = N->use_end(); UI != E;
       ++UI)
    if (UI.getUse().getResNo() == 1) {
      if ((IsAdd && (UI->getOpcode() != ISD::UADDO_CARRY)) ||
          (!IsAdd && (UI->getOpcode() != ISD::USUBO_CARRY))) {
        IsVALU = true;
        break;
      }
    }

  if (IsVALU) {
    unsigned Opc = IsAdd ? AMDGPU::V_ADD_CO_U32_e64 : AMDGPU::V_SUB_CO_U32_e64;

    CurDAG->SelectNodeTo(
        N, Opc, N->getVTList(),
        {N->getOperand(0), N->getOperand(1),
         CurDAG->getTargetConstant(0, {}, MVT::i1) /*clamp bit*/});
  } else {
    unsigned Opc = N->getOpcode() == ISD::UADDO ? AMDGPU::S_UADDO_PSEUDO
                                                : AMDGPU::S_USUBO_PSEUDO;

    CurDAG->SelectNodeTo(N, Opc, N->getVTList(),
                         {N->getOperand(0), N->getOperand(1)});
  }
}

void AMDGPUDAGToDAGISel::SelectFMA_W_CHAIN(SDNode *N) {
  SDLoc SL(N);
  //  src0_modifiers, src0,  src1_modifiers, src1, src2_modifiers, src2, clamp, omod
  SDValue Ops[10];

  SelectVOP3Mods0(N->getOperand(1), Ops[1], Ops[0], Ops[6], Ops[7]);
  SelectVOP3Mods(N->getOperand(2), Ops[3], Ops[2]);
  SelectVOP3Mods(N->getOperand(3), Ops[5], Ops[4]);
  Ops[8] = N->getOperand(0);
  Ops[9] = N->getOperand(4);

  // If there are no source modifiers, prefer fmac over fma because it can use
  // the smaller VOP2 encoding.
  bool UseFMAC = Subtarget->hasDLInsts() &&
                 cast<ConstantSDNode>(Ops[0])->isZero() &&
                 cast<ConstantSDNode>(Ops[2])->isZero() &&
                 cast<ConstantSDNode>(Ops[4])->isZero();
  unsigned Opcode = UseFMAC ? AMDGPU::V_FMAC_F32_e64 : AMDGPU::V_FMA_F32_e64;
  CurDAG->SelectNodeTo(N, Opcode, N->getVTList(), Ops);
}

void AMDGPUDAGToDAGISel::SelectFMUL_W_CHAIN(SDNode *N) {
  SDLoc SL(N);
  //    src0_modifiers, src0,  src1_modifiers, src1, clamp, omod
  SDValue Ops[8];

  SelectVOP3Mods0(N->getOperand(1), Ops[1], Ops[0], Ops[4], Ops[5]);
  SelectVOP3Mods(N->getOperand(2), Ops[3], Ops[2]);
  Ops[6] = N->getOperand(0);
  Ops[7] = N->getOperand(3);

  CurDAG->SelectNodeTo(N, AMDGPU::V_MUL_F32_e64, N->getVTList(), Ops);
}

// We need to handle this here because tablegen doesn't support matching
// instructions with multiple outputs.
void AMDGPUDAGToDAGISel::SelectDIV_SCALE(SDNode *N) {
  SDLoc SL(N);
  EVT VT = N->getValueType(0);

  assert(VT == MVT::f32 || VT == MVT::f64);

  unsigned Opc
    = (VT == MVT::f64) ? AMDGPU::V_DIV_SCALE_F64_e64 : AMDGPU::V_DIV_SCALE_F32_e64;

  // src0_modifiers, src0, src1_modifiers, src1, src2_modifiers, src2, clamp,
  // omod
  SDValue Ops[8];
  SelectVOP3BMods0(N->getOperand(0), Ops[1], Ops[0], Ops[6], Ops[7]);
  SelectVOP3BMods(N->getOperand(1), Ops[3], Ops[2]);
  SelectVOP3BMods(N->getOperand(2), Ops[5], Ops[4]);
  CurDAG->SelectNodeTo(N, Opc, N->getVTList(), Ops);
}

// We need to handle this here because tablegen doesn't support matching
// instructions with multiple outputs.
void AMDGPUDAGToDAGISel::SelectMAD_64_32(SDNode *N) {
  SDLoc SL(N);
  bool Signed = N->getOpcode() == AMDGPUISD::MAD_I64_I32;
  unsigned Opc;
  if (Subtarget->hasMADIntraFwdBug())
    Opc = Signed ? AMDGPU::V_MAD_I64_I32_gfx11_e64
                 : AMDGPU::V_MAD_U64_U32_gfx11_e64;
  else
    Opc = Signed ? AMDGPU::V_MAD_I64_I32_e64 : AMDGPU::V_MAD_U64_U32_e64;

  SDValue Clamp = CurDAG->getTargetConstant(0, SL, MVT::i1);
  SDValue Ops[] = { N->getOperand(0), N->getOperand(1), N->getOperand(2),
                    Clamp };
  CurDAG->SelectNodeTo(N, Opc, N->getVTList(), Ops);
}

// We need to handle this here because tablegen doesn't support matching
// instructions with multiple outputs.
void AMDGPUDAGToDAGISel::SelectMUL_LOHI(SDNode *N) {
  SDLoc SL(N);
  bool Signed = N->getOpcode() == ISD::SMUL_LOHI;
  unsigned Opc;
  if (Subtarget->hasMADIntraFwdBug())
    Opc = Signed ? AMDGPU::V_MAD_I64_I32_gfx11_e64
                 : AMDGPU::V_MAD_U64_U32_gfx11_e64;
  else
    Opc = Signed ? AMDGPU::V_MAD_I64_I32_e64 : AMDGPU::V_MAD_U64_U32_e64;

  SDValue Zero = CurDAG->getTargetConstant(0, SL, MVT::i64);
  SDValue Clamp = CurDAG->getTargetConstant(0, SL, MVT::i1);
  SDValue Ops[] = {N->getOperand(0), N->getOperand(1), Zero, Clamp};
  SDNode *Mad = CurDAG->getMachineNode(Opc, SL, N->getVTList(), Ops);
  if (!SDValue(N, 0).use_empty()) {
    SDValue Sub0 = CurDAG->getTargetConstant(AMDGPU::sub0, SL, MVT::i32);
    SDNode *Lo = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG, SL,
                                        MVT::i32, SDValue(Mad, 0), Sub0);
    ReplaceUses(SDValue(N, 0), SDValue(Lo, 0));
  }
  if (!SDValue(N, 1).use_empty()) {
    SDValue Sub1 = CurDAG->getTargetConstant(AMDGPU::sub1, SL, MVT::i32);
    SDNode *Hi = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG, SL,
                                        MVT::i32, SDValue(Mad, 0), Sub1);
    ReplaceUses(SDValue(N, 1), SDValue(Hi, 0));
  }
  CurDAG->RemoveDeadNode(N);
}

bool AMDGPUDAGToDAGISel::isDSOffsetLegal(SDValue Base, unsigned Offset) const {
  if (!isUInt<16>(Offset))
    return false;

  if (!Base || Subtarget->hasUsableDSOffset() ||
      Subtarget->unsafeDSOffsetFoldingEnabled())
    return true;

  // On Southern Islands instruction with a negative base value and an offset
  // don't seem to work.
  return CurDAG->SignBitIsZero(Base);
}

bool AMDGPUDAGToDAGISel::SelectDS1Addr1Offset(SDValue Addr, SDValue &Base,
                                              SDValue &Offset) const {
  SDLoc DL(Addr);
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    SDValue N0 = Addr.getOperand(0);
    SDValue N1 = Addr.getOperand(1);
    ConstantSDNode *C1 = cast<ConstantSDNode>(N1);
    if (isDSOffsetLegal(N0, C1->getSExtValue())) {
      // (add n0, c0)
      Base = N0;
      Offset = CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i16);
      return true;
    }
  } else if (Addr.getOpcode() == ISD::SUB) {
    // sub C, x -> add (sub 0, x), C
    if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Addr.getOperand(0))) {
      int64_t ByteOffset = C->getSExtValue();
      if (isDSOffsetLegal(SDValue(), ByteOffset)) {
        SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);

        // XXX - This is kind of hacky. Create a dummy sub node so we can check
        // the known bits in isDSOffsetLegal. We need to emit the selected node
        // here, so this is thrown away.
        SDValue Sub = CurDAG->getNode(ISD::SUB, DL, MVT::i32,
                                      Zero, Addr.getOperand(1));

        if (isDSOffsetLegal(Sub, ByteOffset)) {
          SmallVector<SDValue, 3> Opnds;
          Opnds.push_back(Zero);
          Opnds.push_back(Addr.getOperand(1));

          // FIXME: Select to VOP3 version for with-carry.
          unsigned SubOp = AMDGPU::V_SUB_CO_U32_e32;
          if (Subtarget->hasAddNoCarry()) {
            SubOp = AMDGPU::V_SUB_U32_e64;
            Opnds.push_back(
                CurDAG->getTargetConstant(0, {}, MVT::i1)); // clamp bit
          }

          MachineSDNode *MachineSub =
              CurDAG->getMachineNode(SubOp, DL, MVT::i32, Opnds);

          Base = SDValue(MachineSub, 0);
          Offset = CurDAG->getTargetConstant(ByteOffset, DL, MVT::i16);
          return true;
        }
      }
    }
  } else if (const ConstantSDNode *CAddr = dyn_cast<ConstantSDNode>(Addr)) {
    // If we have a constant address, prefer to put the constant into the
    // offset. This can save moves to load the constant address since multiple
    // operations can share the zero base address register, and enables merging
    // into read2 / write2 instructions.

    SDLoc DL(Addr);

    if (isDSOffsetLegal(SDValue(), CAddr->getZExtValue())) {
      SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);
      MachineSDNode *MovZero = CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32,
                                 DL, MVT::i32, Zero);
      Base = SDValue(MovZero, 0);
      Offset = CurDAG->getTargetConstant(CAddr->getZExtValue(), DL, MVT::i16);
      return true;
    }
  }

  // default case
  Base = Addr;
  Offset = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i16);
  return true;
}

bool AMDGPUDAGToDAGISel::isDSOffset2Legal(SDValue Base, unsigned Offset0,
                                          unsigned Offset1,
                                          unsigned Size) const {
  if (Offset0 % Size != 0 || Offset1 % Size != 0)
    return false;
  if (!isUInt<8>(Offset0 / Size) || !isUInt<8>(Offset1 / Size))
    return false;

  if (!Base || Subtarget->hasUsableDSOffset() ||
      Subtarget->unsafeDSOffsetFoldingEnabled())
    return true;

  // On Southern Islands instruction with a negative base value and an offset
  // don't seem to work.
  return CurDAG->SignBitIsZero(Base);
}

// Return whether the operation has NoUnsignedWrap property.
static bool isNoUnsignedWrap(SDValue Addr) {
  return (Addr.getOpcode() == ISD::ADD &&
          Addr->getFlags().hasNoUnsignedWrap()) ||
         Addr->getOpcode() == ISD::OR;
}

// Check that the base address of flat scratch load/store in the form of `base +
// offset` is legal to be put in SGPR/VGPR (i.e. unsigned per hardware
// requirement). We always treat the first operand as the base address here.
bool AMDGPUDAGToDAGISel::isFlatScratchBaseLegal(SDValue Addr) const {
  if (isNoUnsignedWrap(Addr))
    return true;

  // Starting with GFX12, VADDR and SADDR fields in VSCRATCH can use negative
  // values.
  if (Subtarget->hasSignedScratchOffsets())
    return true;

  auto LHS = Addr.getOperand(0);
  auto RHS = Addr.getOperand(1);

  // If the immediate offset is negative and within certain range, the base
  // address cannot also be negative. If the base is also negative, the sum
  // would be either negative or much larger than the valid range of scratch
  // memory a thread can access.
  ConstantSDNode *ImmOp = nullptr;
  if (Addr.getOpcode() == ISD::ADD && (ImmOp = dyn_cast<ConstantSDNode>(RHS))) {
    if (ImmOp->getSExtValue() < 0 && ImmOp->getSExtValue() > -0x40000000)
      return true;
  }

  return CurDAG->SignBitIsZero(LHS);
}

// Check address value in SGPR/VGPR are legal for flat scratch in the form
// of: SGPR + VGPR.
bool AMDGPUDAGToDAGISel::isFlatScratchBaseLegalSV(SDValue Addr) const {
  if (isNoUnsignedWrap(Addr))
    return true;

  // Starting with GFX12, VADDR and SADDR fields in VSCRATCH can use negative
  // values.
  if (Subtarget->hasSignedScratchOffsets())
    return true;

  auto LHS = Addr.getOperand(0);
  auto RHS = Addr.getOperand(1);
  return CurDAG->SignBitIsZero(RHS) && CurDAG->SignBitIsZero(LHS);
}

// Check address value in SGPR/VGPR are legal for flat scratch in the form
// of: SGPR + VGPR + Imm.
bool AMDGPUDAGToDAGISel::isFlatScratchBaseLegalSVImm(SDValue Addr) const {
  // Starting with GFX12, VADDR and SADDR fields in VSCRATCH can use negative
  // values.
  if (AMDGPU::isGFX12Plus(*Subtarget))
    return true;

  auto Base = Addr.getOperand(0);
  auto *RHSImm = cast<ConstantSDNode>(Addr.getOperand(1));
  // If the immediate offset is negative and within certain range, the base
  // address cannot also be negative. If the base is also negative, the sum
  // would be either negative or much larger than the valid range of scratch
  // memory a thread can access.
  if (isNoUnsignedWrap(Base) &&
      (isNoUnsignedWrap(Addr) ||
       (RHSImm->getSExtValue() < 0 && RHSImm->getSExtValue() > -0x40000000)))
    return true;

  auto LHS = Base.getOperand(0);
  auto RHS = Base.getOperand(1);
  return CurDAG->SignBitIsZero(RHS) && CurDAG->SignBitIsZero(LHS);
}

// TODO: If offset is too big, put low 16-bit into offset.
bool AMDGPUDAGToDAGISel::SelectDS64Bit4ByteAligned(SDValue Addr, SDValue &Base,
                                                   SDValue &Offset0,
                                                   SDValue &Offset1) const {
  return SelectDSReadWrite2(Addr, Base, Offset0, Offset1, 4);
}

bool AMDGPUDAGToDAGISel::SelectDS128Bit8ByteAligned(SDValue Addr, SDValue &Base,
                                                    SDValue &Offset0,
                                                    SDValue &Offset1) const {
  return SelectDSReadWrite2(Addr, Base, Offset0, Offset1, 8);
}

bool AMDGPUDAGToDAGISel::SelectDSReadWrite2(SDValue Addr, SDValue &Base,
                                            SDValue &Offset0, SDValue &Offset1,
                                            unsigned Size) const {
  SDLoc DL(Addr);

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    SDValue N0 = Addr.getOperand(0);
    SDValue N1 = Addr.getOperand(1);
    ConstantSDNode *C1 = cast<ConstantSDNode>(N1);
    unsigned OffsetValue0 = C1->getZExtValue();
    unsigned OffsetValue1 = OffsetValue0 + Size;

    // (add n0, c0)
    if (isDSOffset2Legal(N0, OffsetValue0, OffsetValue1, Size)) {
      Base = N0;
      Offset0 = CurDAG->getTargetConstant(OffsetValue0 / Size, DL, MVT::i8);
      Offset1 = CurDAG->getTargetConstant(OffsetValue1 / Size, DL, MVT::i8);
      return true;
    }
  } else if (Addr.getOpcode() == ISD::SUB) {
    // sub C, x -> add (sub 0, x), C
    if (const ConstantSDNode *C =
            dyn_cast<ConstantSDNode>(Addr.getOperand(0))) {
      unsigned OffsetValue0 = C->getZExtValue();
      unsigned OffsetValue1 = OffsetValue0 + Size;

      if (isDSOffset2Legal(SDValue(), OffsetValue0, OffsetValue1, Size)) {
        SDLoc DL(Addr);
        SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);

        // XXX - This is kind of hacky. Create a dummy sub node so we can check
        // the known bits in isDSOffsetLegal. We need to emit the selected node
        // here, so this is thrown away.
        SDValue Sub =
            CurDAG->getNode(ISD::SUB, DL, MVT::i32, Zero, Addr.getOperand(1));

        if (isDSOffset2Legal(Sub, OffsetValue0, OffsetValue1, Size)) {
          SmallVector<SDValue, 3> Opnds;
          Opnds.push_back(Zero);
          Opnds.push_back(Addr.getOperand(1));
          unsigned SubOp = AMDGPU::V_SUB_CO_U32_e32;
          if (Subtarget->hasAddNoCarry()) {
            SubOp = AMDGPU::V_SUB_U32_e64;
            Opnds.push_back(
                CurDAG->getTargetConstant(0, {}, MVT::i1)); // clamp bit
          }

          MachineSDNode *MachineSub = CurDAG->getMachineNode(
              SubOp, DL, MVT::getIntegerVT(Size * 8), Opnds);

          Base = SDValue(MachineSub, 0);
          Offset0 = CurDAG->getTargetConstant(OffsetValue0 / Size, DL, MVT::i8);
          Offset1 = CurDAG->getTargetConstant(OffsetValue1 / Size, DL, MVT::i8);
          return true;
        }
      }
    }
  } else if (const ConstantSDNode *CAddr = dyn_cast<ConstantSDNode>(Addr)) {
    unsigned OffsetValue0 = CAddr->getZExtValue();
    unsigned OffsetValue1 = OffsetValue0 + Size;

    if (isDSOffset2Legal(SDValue(), OffsetValue0, OffsetValue1, Size)) {
      SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);
      MachineSDNode *MovZero =
          CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32, DL, MVT::i32, Zero);
      Base = SDValue(MovZero, 0);
      Offset0 = CurDAG->getTargetConstant(OffsetValue0 / Size, DL, MVT::i8);
      Offset1 = CurDAG->getTargetConstant(OffsetValue1 / Size, DL, MVT::i8);
      return true;
    }
  }

  // default case

  Base = Addr;
  Offset0 = CurDAG->getTargetConstant(0, DL, MVT::i8);
  Offset1 = CurDAG->getTargetConstant(1, DL, MVT::i8);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectMUBUF(SDValue Addr, SDValue &Ptr, SDValue &VAddr,
                                     SDValue &SOffset, SDValue &Offset,
                                     SDValue &Offen, SDValue &Idxen,
                                     SDValue &Addr64) const {
  // Subtarget prefers to use flat instruction
  // FIXME: This should be a pattern predicate and not reach here
  if (Subtarget->useFlatForGlobal())
    return false;

  SDLoc DL(Addr);

  Idxen = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Offen = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Addr64 = CurDAG->getTargetConstant(0, DL, MVT::i1);
  SOffset = Subtarget->hasRestrictedSOffset()
                ? CurDAG->getRegister(AMDGPU::SGPR_NULL, MVT::i32)
                : CurDAG->getTargetConstant(0, DL, MVT::i32);

  ConstantSDNode *C1 = nullptr;
  SDValue N0 = Addr;
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    C1 = cast<ConstantSDNode>(Addr.getOperand(1));
    if (isUInt<32>(C1->getZExtValue()))
      N0 = Addr.getOperand(0);
    else
      C1 = nullptr;
  }

  if (N0.getOpcode() == ISD::ADD) {
    // (add N2, N3) -> addr64, or
    // (add (add N2, N3), C1) -> addr64
    SDValue N2 = N0.getOperand(0);
    SDValue N3 = N0.getOperand(1);
    Addr64 = CurDAG->getTargetConstant(1, DL, MVT::i1);

    if (N2->isDivergent()) {
      if (N3->isDivergent()) {
        // Both N2 and N3 are divergent. Use N0 (the result of the add) as the
        // addr64, and construct the resource from a 0 address.
        Ptr = SDValue(buildSMovImm64(DL, 0, MVT::v2i32), 0);
        VAddr = N0;
      } else {
        // N2 is divergent, N3 is not.
        Ptr = N3;
        VAddr = N2;
      }
    } else {
      // N2 is not divergent.
      Ptr = N2;
      VAddr = N3;
    }
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  } else if (N0->isDivergent()) {
    // N0 is divergent. Use it as the addr64, and construct the resource from a
    // 0 address.
    Ptr = SDValue(buildSMovImm64(DL, 0, MVT::v2i32), 0);
    VAddr = N0;
    Addr64 = CurDAG->getTargetConstant(1, DL, MVT::i1);
  } else {
    // N0 -> offset, or
    // (N0 + C1) -> offset
    VAddr = CurDAG->getTargetConstant(0, DL, MVT::i32);
    Ptr = N0;
  }

  if (!C1) {
    // No offset.
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
    return true;
  }

  const SIInstrInfo *TII = Subtarget->getInstrInfo();
  if (TII->isLegalMUBUFImmOffset(C1->getZExtValue())) {
    // Legal offset for instruction.
    Offset = CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i32);
    return true;
  }

  // Illegal offset, store it in soffset.
  Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  SOffset =
      SDValue(CurDAG->getMachineNode(
                  AMDGPU::S_MOV_B32, DL, MVT::i32,
                  CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i32)),
              0);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectMUBUFAddr64(SDValue Addr, SDValue &SRsrc,
                                           SDValue &VAddr, SDValue &SOffset,
                                           SDValue &Offset) const {
  SDValue Ptr, Offen, Idxen, Addr64;

  // addr64 bit was removed for volcanic islands.
  // FIXME: This should be a pattern predicate and not reach here
  if (!Subtarget->hasAddr64())
    return false;

  if (!SelectMUBUF(Addr, Ptr, VAddr, SOffset, Offset, Offen, Idxen, Addr64))
    return false;

  ConstantSDNode *C = cast<ConstantSDNode>(Addr64);
  if (C->getSExtValue()) {
    SDLoc DL(Addr);

    const SITargetLowering& Lowering =
      *static_cast<const SITargetLowering*>(getTargetLowering());

    SRsrc = SDValue(Lowering.wrapAddr64Rsrc(*CurDAG, DL, Ptr), 0);
    return true;
  }

  return false;
}

std::pair<SDValue, SDValue> AMDGPUDAGToDAGISel::foldFrameIndex(SDValue N) const {
  SDLoc DL(N);

  auto *FI = dyn_cast<FrameIndexSDNode>(N);
  SDValue TFI =
      FI ? CurDAG->getTargetFrameIndex(FI->getIndex(), FI->getValueType(0)) : N;

  // We rebase the base address into an absolute stack address and hence
  // use constant 0 for soffset. This value must be retained until
  // frame elimination and eliminateFrameIndex will choose the appropriate
  // frame register if need be.
  return std::pair(TFI, CurDAG->getTargetConstant(0, DL, MVT::i32));
}

bool AMDGPUDAGToDAGISel::SelectMUBUFScratchOffen(SDNode *Parent,
                                                 SDValue Addr, SDValue &Rsrc,
                                                 SDValue &VAddr, SDValue &SOffset,
                                                 SDValue &ImmOffset) const {

  SDLoc DL(Addr);
  MachineFunction &MF = CurDAG->getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  Rsrc = CurDAG->getRegister(Info->getScratchRSrcReg(), MVT::v4i32);

  if (ConstantSDNode *CAddr = dyn_cast<ConstantSDNode>(Addr)) {
    int64_t Imm = CAddr->getSExtValue();
    const int64_t NullPtr =
        AMDGPUTargetMachine::getNullPointerValue(AMDGPUAS::PRIVATE_ADDRESS);
    // Don't fold null pointer.
    if (Imm != NullPtr) {
      const uint32_t MaxOffset = SIInstrInfo::getMaxMUBUFImmOffset(*Subtarget);
      SDValue HighBits =
          CurDAG->getTargetConstant(Imm & ~MaxOffset, DL, MVT::i32);
      MachineSDNode *MovHighBits = CurDAG->getMachineNode(
        AMDGPU::V_MOV_B32_e32, DL, MVT::i32, HighBits);
      VAddr = SDValue(MovHighBits, 0);

      SOffset = CurDAG->getTargetConstant(0, DL, MVT::i32);
      ImmOffset = CurDAG->getTargetConstant(Imm & MaxOffset, DL, MVT::i32);
      return true;
    }
  }

  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    // (add n0, c1)

    SDValue N0 = Addr.getOperand(0);
    uint64_t C1 = Addr.getConstantOperandVal(1);

    // Offsets in vaddr must be positive if range checking is enabled.
    //
    // The total computation of vaddr + soffset + offset must not overflow.  If
    // vaddr is negative, even if offset is 0 the sgpr offset add will end up
    // overflowing.
    //
    // Prior to gfx9, MUBUF instructions with the vaddr offset enabled would
    // always perform a range check. If a negative vaddr base index was used,
    // this would fail the range check. The overall address computation would
    // compute a valid address, but this doesn't happen due to the range
    // check. For out-of-bounds MUBUF loads, a 0 is returned.
    //
    // Therefore it should be safe to fold any VGPR offset on gfx9 into the
    // MUBUF vaddr, but not on older subtargets which can only do this if the
    // sign bit is known 0.
    const SIInstrInfo *TII = Subtarget->getInstrInfo();
    if (TII->isLegalMUBUFImmOffset(C1) &&
        (!Subtarget->privateMemoryResourceIsRangeChecked() ||
         CurDAG->SignBitIsZero(N0))) {
      std::tie(VAddr, SOffset) = foldFrameIndex(N0);
      ImmOffset = CurDAG->getTargetConstant(C1, DL, MVT::i32);
      return true;
    }
  }

  // (node)
  std::tie(VAddr, SOffset) = foldFrameIndex(Addr);
  ImmOffset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  return true;
}

static bool IsCopyFromSGPR(const SIRegisterInfo &TRI, SDValue Val) {
  if (Val.getOpcode() != ISD::CopyFromReg)
    return false;
  auto Reg = cast<RegisterSDNode>(Val.getOperand(1))->getReg();
  if (!Reg.isPhysical())
    return false;
  auto RC = TRI.getPhysRegBaseClass(Reg);
  return RC && TRI.isSGPRClass(RC);
}

bool AMDGPUDAGToDAGISel::SelectMUBUFScratchOffset(SDNode *Parent,
                                                  SDValue Addr,
                                                  SDValue &SRsrc,
                                                  SDValue &SOffset,
                                                  SDValue &Offset) const {
  const SIRegisterInfo *TRI =
      static_cast<const SIRegisterInfo *>(Subtarget->getRegisterInfo());
  const SIInstrInfo *TII = Subtarget->getInstrInfo();
  MachineFunction &MF = CurDAG->getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  SDLoc DL(Addr);

  // CopyFromReg <sgpr>
  if (IsCopyFromSGPR(*TRI, Addr)) {
    SRsrc = CurDAG->getRegister(Info->getScratchRSrcReg(), MVT::v4i32);
    SOffset = Addr;
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
    return true;
  }

  ConstantSDNode *CAddr;
  if (Addr.getOpcode() == ISD::ADD) {
    // Add (CopyFromReg <sgpr>) <constant>
    CAddr = dyn_cast<ConstantSDNode>(Addr.getOperand(1));
    if (!CAddr || !TII->isLegalMUBUFImmOffset(CAddr->getZExtValue()))
      return false;
    if (!IsCopyFromSGPR(*TRI, Addr.getOperand(0)))
      return false;

    SOffset = Addr.getOperand(0);
  } else if ((CAddr = dyn_cast<ConstantSDNode>(Addr)) &&
             TII->isLegalMUBUFImmOffset(CAddr->getZExtValue())) {
    // <constant>
    SOffset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  } else {
    return false;
  }

  SRsrc = CurDAG->getRegister(Info->getScratchRSrcReg(), MVT::v4i32);

  Offset = CurDAG->getTargetConstant(CAddr->getZExtValue(), DL, MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectMUBUFOffset(SDValue Addr, SDValue &SRsrc,
                                           SDValue &SOffset, SDValue &Offset
                                           ) const {
  SDValue Ptr, VAddr, Offen, Idxen, Addr64;
  const SIInstrInfo *TII = Subtarget->getInstrInfo();

  if (!SelectMUBUF(Addr, Ptr, VAddr, SOffset, Offset, Offen, Idxen, Addr64))
    return false;

  if (!cast<ConstantSDNode>(Offen)->getSExtValue() &&
      !cast<ConstantSDNode>(Idxen)->getSExtValue() &&
      !cast<ConstantSDNode>(Addr64)->getSExtValue()) {
    uint64_t Rsrc = TII->getDefaultRsrcDataFormat() |
                    APInt::getAllOnes(32).getZExtValue(); // Size
    SDLoc DL(Addr);

    const SITargetLowering& Lowering =
      *static_cast<const SITargetLowering*>(getTargetLowering());

    SRsrc = SDValue(Lowering.buildRSRC(*CurDAG, DL, Ptr, 0, Rsrc), 0);
    return true;
  }
  return false;
}

bool AMDGPUDAGToDAGISel::SelectBUFSOffset(SDValue ByteOffsetNode,
                                          SDValue &SOffset) const {
  if (Subtarget->hasRestrictedSOffset() && isNullConstant(ByteOffsetNode)) {
    SOffset = CurDAG->getRegister(AMDGPU::SGPR_NULL, MVT::i32);
    return true;
  }

  SOffset = ByteOffsetNode;
  return true;
}

// Find a load or store from corresponding pattern root.
// Roots may be build_vector, bitconvert or their combinations.
static MemSDNode* findMemSDNode(SDNode *N) {
  N = AMDGPUTargetLowering::stripBitcast(SDValue(N,0)).getNode();
  if (MemSDNode *MN = dyn_cast<MemSDNode>(N))
    return MN;
  assert(isa<BuildVectorSDNode>(N));
  for (SDValue V : N->op_values())
    if (MemSDNode *MN =
          dyn_cast<MemSDNode>(AMDGPUTargetLowering::stripBitcast(V)))
      return MN;
  llvm_unreachable("cannot find MemSDNode in the pattern!");
}

bool AMDGPUDAGToDAGISel::SelectFlatOffsetImpl(SDNode *N, SDValue Addr,
                                              SDValue &VAddr, SDValue &Offset,
                                              uint64_t FlatVariant) const {
  int64_t OffsetVal = 0;

  unsigned AS = findMemSDNode(N)->getAddressSpace();

  bool CanHaveFlatSegmentOffsetBug =
      Subtarget->hasFlatSegmentOffsetBug() &&
      FlatVariant == SIInstrFlags::FLAT &&
      (AS == AMDGPUAS::FLAT_ADDRESS || AS == AMDGPUAS::GLOBAL_ADDRESS);

  if (Subtarget->hasFlatInstOffsets() && !CanHaveFlatSegmentOffsetBug) {
    SDValue N0, N1;
    if (isBaseWithConstantOffset64(Addr, N0, N1) &&
        (FlatVariant != SIInstrFlags::FlatScratch ||
         isFlatScratchBaseLegal(Addr))) {
      int64_t COffsetVal = cast<ConstantSDNode>(N1)->getSExtValue();

      const SIInstrInfo *TII = Subtarget->getInstrInfo();
      if (TII->isLegalFLATOffset(COffsetVal, AS, FlatVariant)) {
        Addr = N0;
        OffsetVal = COffsetVal;
      } else {
        // If the offset doesn't fit, put the low bits into the offset field and
        // add the rest.
        //
        // For a FLAT instruction the hardware decides whether to access
        // global/scratch/shared memory based on the high bits of vaddr,
        // ignoring the offset field, so we have to ensure that when we add
        // remainder to vaddr it still points into the same underlying object.
        // The easiest way to do that is to make sure that we split the offset
        // into two pieces that are both >= 0 or both <= 0.

        SDLoc DL(N);
        uint64_t RemainderOffset;

        std::tie(OffsetVal, RemainderOffset) =
            TII->splitFlatOffset(COffsetVal, AS, FlatVariant);

        SDValue AddOffsetLo =
            getMaterializedScalarImm32(Lo_32(RemainderOffset), DL);
        SDValue Clamp = CurDAG->getTargetConstant(0, DL, MVT::i1);

        if (Addr.getValueType().getSizeInBits() == 32) {
          SmallVector<SDValue, 3> Opnds;
          Opnds.push_back(N0);
          Opnds.push_back(AddOffsetLo);
          unsigned AddOp = AMDGPU::V_ADD_CO_U32_e32;
          if (Subtarget->hasAddNoCarry()) {
            AddOp = AMDGPU::V_ADD_U32_e64;
            Opnds.push_back(Clamp);
          }
          Addr = SDValue(CurDAG->getMachineNode(AddOp, DL, MVT::i32, Opnds), 0);
        } else {
          // TODO: Should this try to use a scalar add pseudo if the base address
          // is uniform and saddr is usable?
          SDValue Sub0 = CurDAG->getTargetConstant(AMDGPU::sub0, DL, MVT::i32);
          SDValue Sub1 = CurDAG->getTargetConstant(AMDGPU::sub1, DL, MVT::i32);

          SDNode *N0Lo = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                                DL, MVT::i32, N0, Sub0);
          SDNode *N0Hi = CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                                DL, MVT::i32, N0, Sub1);

          SDValue AddOffsetHi =
              getMaterializedScalarImm32(Hi_32(RemainderOffset), DL);

          SDVTList VTs = CurDAG->getVTList(MVT::i32, MVT::i1);

          SDNode *Add =
              CurDAG->getMachineNode(AMDGPU::V_ADD_CO_U32_e64, DL, VTs,
                                     {AddOffsetLo, SDValue(N0Lo, 0), Clamp});

          SDNode *Addc = CurDAG->getMachineNode(
              AMDGPU::V_ADDC_U32_e64, DL, VTs,
              {AddOffsetHi, SDValue(N0Hi, 0), SDValue(Add, 1), Clamp});

          SDValue RegSequenceArgs[] = {
              CurDAG->getTargetConstant(AMDGPU::VReg_64RegClassID, DL, MVT::i32),
              SDValue(Add, 0), Sub0, SDValue(Addc, 0), Sub1};

          Addr = SDValue(CurDAG->getMachineNode(AMDGPU::REG_SEQUENCE, DL,
                                                MVT::i64, RegSequenceArgs),
                         0);
        }
      }
    }
  }

  VAddr = Addr;
  Offset = CurDAG->getTargetConstant(OffsetVal, SDLoc(), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectFlatOffset(SDNode *N, SDValue Addr,
                                          SDValue &VAddr,
                                          SDValue &Offset) const {
  return SelectFlatOffsetImpl(N, Addr, VAddr, Offset, SIInstrFlags::FLAT);
}

bool AMDGPUDAGToDAGISel::SelectGlobalOffset(SDNode *N, SDValue Addr,
                                            SDValue &VAddr,
                                            SDValue &Offset) const {
  return SelectFlatOffsetImpl(N, Addr, VAddr, Offset, SIInstrFlags::FlatGlobal);
}

bool AMDGPUDAGToDAGISel::SelectScratchOffset(SDNode *N, SDValue Addr,
                                             SDValue &VAddr,
                                             SDValue &Offset) const {
  return SelectFlatOffsetImpl(N, Addr, VAddr, Offset,
                              SIInstrFlags::FlatScratch);
}

// If this matches zero_extend i32:x, return x
static SDValue matchZExtFromI32(SDValue Op) {
  if (Op.getOpcode() != ISD::ZERO_EXTEND)
    return SDValue();

  SDValue ExtSrc = Op.getOperand(0);
  return (ExtSrc.getValueType() == MVT::i32) ? ExtSrc : SDValue();
}

// Match (64-bit SGPR base) + (zext vgpr offset) + sext(imm offset)
bool AMDGPUDAGToDAGISel::SelectGlobalSAddr(SDNode *N,
                                           SDValue Addr,
                                           SDValue &SAddr,
                                           SDValue &VOffset,
                                           SDValue &Offset) const {
  int64_t ImmOffset = 0;

  // Match the immediate offset first, which canonically is moved as low as
  // possible.

  SDValue LHS, RHS;
  if (isBaseWithConstantOffset64(Addr, LHS, RHS)) {
    int64_t COffsetVal = cast<ConstantSDNode>(RHS)->getSExtValue();
    const SIInstrInfo *TII = Subtarget->getInstrInfo();

    if (TII->isLegalFLATOffset(COffsetVal, AMDGPUAS::GLOBAL_ADDRESS,
                               SIInstrFlags::FlatGlobal)) {
      Addr = LHS;
      ImmOffset = COffsetVal;
    } else if (!LHS->isDivergent()) {
      if (COffsetVal > 0) {
        SDLoc SL(N);
        // saddr + large_offset -> saddr +
        //                         (voffset = large_offset & ~MaxOffset) +
        //                         (large_offset & MaxOffset);
        int64_t SplitImmOffset, RemainderOffset;
        std::tie(SplitImmOffset, RemainderOffset) = TII->splitFlatOffset(
            COffsetVal, AMDGPUAS::GLOBAL_ADDRESS, SIInstrFlags::FlatGlobal);

        if (isUInt<32>(RemainderOffset)) {
          SDNode *VMov = CurDAG->getMachineNode(
              AMDGPU::V_MOV_B32_e32, SL, MVT::i32,
              CurDAG->getTargetConstant(RemainderOffset, SDLoc(), MVT::i32));
          VOffset = SDValue(VMov, 0);
          SAddr = LHS;
          Offset = CurDAG->getTargetConstant(SplitImmOffset, SDLoc(), MVT::i32);
          return true;
        }
      }

      // We are adding a 64 bit SGPR and a constant. If constant bus limit
      // is 1 we would need to perform 1 or 2 extra moves for each half of
      // the constant and it is better to do a scalar add and then issue a
      // single VALU instruction to materialize zero. Otherwise it is less
      // instructions to perform VALU adds with immediates or inline literals.
      unsigned NumLiterals =
          !TII->isInlineConstant(APInt(32, COffsetVal & 0xffffffff)) +
          !TII->isInlineConstant(APInt(32, COffsetVal >> 32));
      if (Subtarget->getConstantBusLimit(AMDGPU::V_ADD_U32_e64) > NumLiterals)
        return false;
    }
  }

  // Match the variable offset.
  if (Addr.getOpcode() == ISD::ADD) {
    LHS = Addr.getOperand(0);
    RHS = Addr.getOperand(1);

    if (!LHS->isDivergent()) {
      // add (i64 sgpr), (zero_extend (i32 vgpr))
      if (SDValue ZextRHS = matchZExtFromI32(RHS)) {
        SAddr = LHS;
        VOffset = ZextRHS;
      }
    }

    if (!SAddr && !RHS->isDivergent()) {
      // add (zero_extend (i32 vgpr)), (i64 sgpr)
      if (SDValue ZextLHS = matchZExtFromI32(LHS)) {
        SAddr = RHS;
        VOffset = ZextLHS;
      }
    }

    if (SAddr) {
      Offset = CurDAG->getTargetConstant(ImmOffset, SDLoc(), MVT::i32);
      return true;
    }
  }

  if (Addr->isDivergent() || Addr.getOpcode() == ISD::UNDEF ||
      isa<ConstantSDNode>(Addr))
    return false;

  // It's cheaper to materialize a single 32-bit zero for vaddr than the two
  // moves required to copy a 64-bit SGPR to VGPR.
  SAddr = Addr;
  SDNode *VMov =
      CurDAG->getMachineNode(AMDGPU::V_MOV_B32_e32, SDLoc(Addr), MVT::i32,
                             CurDAG->getTargetConstant(0, SDLoc(), MVT::i32));
  VOffset = SDValue(VMov, 0);
  Offset = CurDAG->getTargetConstant(ImmOffset, SDLoc(), MVT::i32);
  return true;
}

static SDValue SelectSAddrFI(SelectionDAG *CurDAG, SDValue SAddr) {
  if (auto FI = dyn_cast<FrameIndexSDNode>(SAddr)) {
    SAddr = CurDAG->getTargetFrameIndex(FI->getIndex(), FI->getValueType(0));
  } else if (SAddr.getOpcode() == ISD::ADD &&
             isa<FrameIndexSDNode>(SAddr.getOperand(0))) {
    // Materialize this into a scalar move for scalar address to avoid
    // readfirstlane.
    auto FI = cast<FrameIndexSDNode>(SAddr.getOperand(0));
    SDValue TFI = CurDAG->getTargetFrameIndex(FI->getIndex(),
                                              FI->getValueType(0));
    SAddr = SDValue(CurDAG->getMachineNode(AMDGPU::S_ADD_I32, SDLoc(SAddr),
                                           MVT::i32, TFI, SAddr.getOperand(1)),
                    0);
  }

  return SAddr;
}

// Match (32-bit SGPR base) + sext(imm offset)
bool AMDGPUDAGToDAGISel::SelectScratchSAddr(SDNode *Parent, SDValue Addr,
                                            SDValue &SAddr,
                                            SDValue &Offset) const {
  if (Addr->isDivergent())
    return false;

  SDLoc DL(Addr);

  int64_t COffsetVal = 0;

  if (CurDAG->isBaseWithConstantOffset(Addr) && isFlatScratchBaseLegal(Addr)) {
    COffsetVal = cast<ConstantSDNode>(Addr.getOperand(1))->getSExtValue();
    SAddr = Addr.getOperand(0);
  } else {
    SAddr = Addr;
  }

  SAddr = SelectSAddrFI(CurDAG, SAddr);

  const SIInstrInfo *TII = Subtarget->getInstrInfo();

  if (!TII->isLegalFLATOffset(COffsetVal, AMDGPUAS::PRIVATE_ADDRESS,
                              SIInstrFlags::FlatScratch)) {
    int64_t SplitImmOffset, RemainderOffset;
    std::tie(SplitImmOffset, RemainderOffset) = TII->splitFlatOffset(
        COffsetVal, AMDGPUAS::PRIVATE_ADDRESS, SIInstrFlags::FlatScratch);

    COffsetVal = SplitImmOffset;

    SDValue AddOffset =
        SAddr.getOpcode() == ISD::TargetFrameIndex
            ? getMaterializedScalarImm32(Lo_32(RemainderOffset), DL)
            : CurDAG->getTargetConstant(RemainderOffset, DL, MVT::i32);
    SAddr = SDValue(CurDAG->getMachineNode(AMDGPU::S_ADD_I32, DL, MVT::i32,
                                           SAddr, AddOffset),
                    0);
  }

  Offset = CurDAG->getTargetConstant(COffsetVal, DL, MVT::i32);

  return true;
}

// Check whether the flat scratch SVS swizzle bug affects this access.
bool AMDGPUDAGToDAGISel::checkFlatScratchSVSSwizzleBug(
    SDValue VAddr, SDValue SAddr, uint64_t ImmOffset) const {
  if (!Subtarget->hasFlatScratchSVSSwizzleBug())
    return false;

  // The bug affects the swizzling of SVS accesses if there is any carry out
  // from the two low order bits (i.e. from bit 1 into bit 2) when adding
  // voffset to (soffset + inst_offset).
  KnownBits VKnown = CurDAG->computeKnownBits(VAddr);
  KnownBits SKnown = KnownBits::computeForAddSub(
      /*Add=*/true, /*NSW=*/false, /*NUW=*/false,
      CurDAG->computeKnownBits(SAddr),
      KnownBits::makeConstant(APInt(32, ImmOffset)));
  uint64_t VMax = VKnown.getMaxValue().getZExtValue();
  uint64_t SMax = SKnown.getMaxValue().getZExtValue();
  return (VMax & 3) + (SMax & 3) >= 4;
}

bool AMDGPUDAGToDAGISel::SelectScratchSVAddr(SDNode *N, SDValue Addr,
                                             SDValue &VAddr, SDValue &SAddr,
                                             SDValue &Offset) const  {
  int64_t ImmOffset = 0;

  SDValue LHS, RHS;
  SDValue OrigAddr = Addr;
  if (isBaseWithConstantOffset64(Addr, LHS, RHS)) {
    int64_t COffsetVal = cast<ConstantSDNode>(RHS)->getSExtValue();
    const SIInstrInfo *TII = Subtarget->getInstrInfo();

    if (TII->isLegalFLATOffset(COffsetVal, AMDGPUAS::PRIVATE_ADDRESS, true)) {
      Addr = LHS;
      ImmOffset = COffsetVal;
    } else if (!LHS->isDivergent() && COffsetVal > 0) {
      SDLoc SL(N);
      // saddr + large_offset -> saddr + (vaddr = large_offset & ~MaxOffset) +
      //                         (large_offset & MaxOffset);
      int64_t SplitImmOffset, RemainderOffset;
      std::tie(SplitImmOffset, RemainderOffset)
        = TII->splitFlatOffset(COffsetVal, AMDGPUAS::PRIVATE_ADDRESS, true);

      if (isUInt<32>(RemainderOffset)) {
        SDNode *VMov = CurDAG->getMachineNode(
          AMDGPU::V_MOV_B32_e32, SL, MVT::i32,
          CurDAG->getTargetConstant(RemainderOffset, SDLoc(), MVT::i32));
        VAddr = SDValue(VMov, 0);
        SAddr = LHS;
        if (!isFlatScratchBaseLegal(Addr))
          return false;
        if (checkFlatScratchSVSSwizzleBug(VAddr, SAddr, SplitImmOffset))
          return false;
        Offset = CurDAG->getTargetConstant(SplitImmOffset, SDLoc(), MVT::i32);
        return true;
      }
    }
  }

  if (Addr.getOpcode() != ISD::ADD)
    return false;

  LHS = Addr.getOperand(0);
  RHS = Addr.getOperand(1);

  if (!LHS->isDivergent() && RHS->isDivergent()) {
    SAddr = LHS;
    VAddr = RHS;
  } else if (!RHS->isDivergent() && LHS->isDivergent()) {
    SAddr = RHS;
    VAddr = LHS;
  } else {
    return false;
  }

  if (OrigAddr != Addr) {
    if (!isFlatScratchBaseLegalSVImm(OrigAddr))
      return false;
  } else {
    if (!isFlatScratchBaseLegalSV(OrigAddr))
      return false;
  }

  if (checkFlatScratchSVSSwizzleBug(VAddr, SAddr, ImmOffset))
    return false;
  SAddr = SelectSAddrFI(CurDAG, SAddr);
  Offset = CurDAG->getTargetConstant(ImmOffset, SDLoc(), MVT::i32);
  return true;
}

// For unbuffered smem loads, it is illegal for the Immediate Offset to be
// negative if the resulting (Offset + (M0 or SOffset or zero) is negative.
// Handle the case where the Immediate Offset + SOffset is negative.
bool AMDGPUDAGToDAGISel::isSOffsetLegalWithImmOffset(SDValue *SOffset,
                                                     bool Imm32Only,
                                                     bool IsBuffer,
                                                     int64_t ImmOffset) const {
  if (!IsBuffer && !Imm32Only && ImmOffset < 0 &&
      AMDGPU::hasSMRDSignedImmOffset(*Subtarget)) {
    KnownBits SKnown = CurDAG->computeKnownBits(*SOffset);
    if (ImmOffset + SKnown.getMinValue().getSExtValue() < 0)
      return false;
  }

  return true;
}

// Match an immediate (if Offset is not null) or an SGPR (if SOffset is
// not null) offset. If Imm32Only is true, match only 32-bit immediate
// offsets available on CI.
bool AMDGPUDAGToDAGISel::SelectSMRDOffset(SDValue ByteOffsetNode,
                                          SDValue *SOffset, SDValue *Offset,
                                          bool Imm32Only, bool IsBuffer,
                                          bool HasSOffset,
                                          int64_t ImmOffset) const {
  assert((!SOffset || !Offset) &&
         "Cannot match both soffset and offset at the same time!");

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(ByteOffsetNode);
  if (!C) {
    if (!SOffset)
      return false;

    if (ByteOffsetNode.getValueType().isScalarInteger() &&
        ByteOffsetNode.getValueType().getSizeInBits() == 32) {
      *SOffset = ByteOffsetNode;
      return isSOffsetLegalWithImmOffset(SOffset, Imm32Only, IsBuffer,
                                         ImmOffset);
    }
    if (ByteOffsetNode.getOpcode() == ISD::ZERO_EXTEND) {
      if (ByteOffsetNode.getOperand(0).getValueType().getSizeInBits() == 32) {
        *SOffset = ByteOffsetNode.getOperand(0);
        return isSOffsetLegalWithImmOffset(SOffset, Imm32Only, IsBuffer,
                                           ImmOffset);
      }
    }
    return false;
  }

  SDLoc SL(ByteOffsetNode);

  // GFX9 and GFX10 have signed byte immediate offsets. The immediate
  // offset for S_BUFFER instructions is unsigned.
  int64_t ByteOffset = IsBuffer ? C->getZExtValue() : C->getSExtValue();
  std::optional<int64_t> EncodedOffset = AMDGPU::getSMRDEncodedOffset(
      *Subtarget, ByteOffset, IsBuffer, HasSOffset);
  if (EncodedOffset && Offset && !Imm32Only) {
    *Offset = CurDAG->getTargetConstant(*EncodedOffset, SL, MVT::i32);
    return true;
  }

  // SGPR and literal offsets are unsigned.
  if (ByteOffset < 0)
    return false;

  EncodedOffset = AMDGPU::getSMRDEncodedLiteralOffset32(*Subtarget, ByteOffset);
  if (EncodedOffset && Offset && Imm32Only) {
    *Offset = CurDAG->getTargetConstant(*EncodedOffset, SL, MVT::i32);
    return true;
  }

  if (!isUInt<32>(ByteOffset) && !isInt<32>(ByteOffset))
    return false;

  if (SOffset) {
    SDValue C32Bit = CurDAG->getTargetConstant(ByteOffset, SL, MVT::i32);
    *SOffset = SDValue(
        CurDAG->getMachineNode(AMDGPU::S_MOV_B32, SL, MVT::i32, C32Bit), 0);
    return true;
  }

  return false;
}

SDValue AMDGPUDAGToDAGISel::Expand32BitAddress(SDValue Addr) const {
  if (Addr.getValueType() != MVT::i32)
    return Addr;

  // Zero-extend a 32-bit address.
  SDLoc SL(Addr);

  const MachineFunction &MF = CurDAG->getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  unsigned AddrHiVal = Info->get32BitAddressHighBits();
  SDValue AddrHi = CurDAG->getTargetConstant(AddrHiVal, SL, MVT::i32);

  const SDValue Ops[] = {
    CurDAG->getTargetConstant(AMDGPU::SReg_64_XEXECRegClassID, SL, MVT::i32),
    Addr,
    CurDAG->getTargetConstant(AMDGPU::sub0, SL, MVT::i32),
    SDValue(CurDAG->getMachineNode(AMDGPU::S_MOV_B32, SL, MVT::i32, AddrHi),
            0),
    CurDAG->getTargetConstant(AMDGPU::sub1, SL, MVT::i32),
  };

  return SDValue(CurDAG->getMachineNode(AMDGPU::REG_SEQUENCE, SL, MVT::i64,
                                        Ops), 0);
}

// Match a base and an immediate (if Offset is not null) or an SGPR (if
// SOffset is not null) or an immediate+SGPR offset. If Imm32Only is
// true, match only 32-bit immediate offsets available on CI.
bool AMDGPUDAGToDAGISel::SelectSMRDBaseOffset(SDValue Addr, SDValue &SBase,
                                              SDValue *SOffset, SDValue *Offset,
                                              bool Imm32Only, bool IsBuffer,
                                              bool HasSOffset,
                                              int64_t ImmOffset) const {
  if (SOffset && Offset) {
    assert(!Imm32Only && !IsBuffer);
    SDValue B;

    if (!SelectSMRDBaseOffset(Addr, B, nullptr, Offset, false, false, true))
      return false;

    int64_t ImmOff = 0;
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(*Offset))
      ImmOff = C->getSExtValue();

    return SelectSMRDBaseOffset(B, SBase, SOffset, nullptr, false, false, true,
                                ImmOff);
  }

  // A 32-bit (address + offset) should not cause unsigned 32-bit integer
  // wraparound, because s_load instructions perform the addition in 64 bits.
  if (Addr.getValueType() == MVT::i32 && Addr.getOpcode() == ISD::ADD &&
      !Addr->getFlags().hasNoUnsignedWrap())
    return false;

  SDValue N0, N1;
  // Extract the base and offset if possible.
  if (CurDAG->isBaseWithConstantOffset(Addr) || Addr.getOpcode() == ISD::ADD) {
    N0 = Addr.getOperand(0);
    N1 = Addr.getOperand(1);
  } else if (getBaseWithOffsetUsingSplitOR(*CurDAG, Addr, N0, N1)) {
    assert(N0 && N1 && isa<ConstantSDNode>(N1));
  }
  if (!N0 || !N1)
    return false;

  if (SelectSMRDOffset(N1, SOffset, Offset, Imm32Only, IsBuffer, HasSOffset,
                       ImmOffset)) {
    SBase = N0;
    return true;
  }
  if (SelectSMRDOffset(N0, SOffset, Offset, Imm32Only, IsBuffer, HasSOffset,
                       ImmOffset)) {
    SBase = N1;
    return true;
  }
  return false;
}

bool AMDGPUDAGToDAGISel::SelectSMRD(SDValue Addr, SDValue &SBase,
                                    SDValue *SOffset, SDValue *Offset,
                                    bool Imm32Only) const {
  if (SelectSMRDBaseOffset(Addr, SBase, SOffset, Offset, Imm32Only)) {
    SBase = Expand32BitAddress(SBase);
    return true;
  }

  if (Addr.getValueType() == MVT::i32 && Offset && !SOffset) {
    SBase = Expand32BitAddress(Addr);
    *Offset = CurDAG->getTargetConstant(0, SDLoc(Addr), MVT::i32);
    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectSMRDImm(SDValue Addr, SDValue &SBase,
                                       SDValue &Offset) const {
  return SelectSMRD(Addr, SBase, /* SOffset */ nullptr, &Offset);
}

bool AMDGPUDAGToDAGISel::SelectSMRDImm32(SDValue Addr, SDValue &SBase,
                                         SDValue &Offset) const {
  assert(Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS);
  return SelectSMRD(Addr, SBase, /* SOffset */ nullptr, &Offset,
                    /* Imm32Only */ true);
}

bool AMDGPUDAGToDAGISel::SelectSMRDSgpr(SDValue Addr, SDValue &SBase,
                                        SDValue &SOffset) const {
  return SelectSMRD(Addr, SBase, &SOffset, /* Offset */ nullptr);
}

bool AMDGPUDAGToDAGISel::SelectSMRDSgprImm(SDValue Addr, SDValue &SBase,
                                           SDValue &SOffset,
                                           SDValue &Offset) const {
  return SelectSMRD(Addr, SBase, &SOffset, &Offset);
}

bool AMDGPUDAGToDAGISel::SelectSMRDBufferImm(SDValue N, SDValue &Offset) const {
  return SelectSMRDOffset(N, /* SOffset */ nullptr, &Offset,
                          /* Imm32Only */ false, /* IsBuffer */ true);
}

bool AMDGPUDAGToDAGISel::SelectSMRDBufferImm32(SDValue N,
                                               SDValue &Offset) const {
  assert(Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS);
  return SelectSMRDOffset(N, /* SOffset */ nullptr, &Offset,
                          /* Imm32Only */ true, /* IsBuffer */ true);
}

bool AMDGPUDAGToDAGISel::SelectSMRDBufferSgprImm(SDValue N, SDValue &SOffset,
                                                 SDValue &Offset) const {
  // Match the (soffset + offset) pair as a 32-bit register base and
  // an immediate offset.
  return N.getValueType() == MVT::i32 &&
         SelectSMRDBaseOffset(N, /* SBase */ SOffset, /* SOffset*/ nullptr,
                              &Offset, /* Imm32Only */ false,
                              /* IsBuffer */ true);
}

bool AMDGPUDAGToDAGISel::SelectMOVRELOffset(SDValue Index,
                                            SDValue &Base,
                                            SDValue &Offset) const {
  SDLoc DL(Index);

  if (CurDAG->isBaseWithConstantOffset(Index)) {
    SDValue N0 = Index.getOperand(0);
    SDValue N1 = Index.getOperand(1);
    ConstantSDNode *C1 = cast<ConstantSDNode>(N1);

    // (add n0, c0)
    // Don't peel off the offset (c0) if doing so could possibly lead
    // the base (n0) to be negative.
    // (or n0, |c0|) can never change a sign given isBaseWithConstantOffset.
    if (C1->getSExtValue() <= 0 || CurDAG->SignBitIsZero(N0) ||
        (Index->getOpcode() == ISD::OR && C1->getSExtValue() >= 0)) {
      Base = N0;
      Offset = CurDAG->getTargetConstant(C1->getZExtValue(), DL, MVT::i32);
      return true;
    }
  }

  if (isa<ConstantSDNode>(Index))
    return false;

  Base = Index;
  Offset = CurDAG->getTargetConstant(0, DL, MVT::i32);
  return true;
}

SDNode *AMDGPUDAGToDAGISel::getBFE32(bool IsSigned, const SDLoc &DL,
                                     SDValue Val, uint32_t Offset,
                                     uint32_t Width) {
  if (Val->isDivergent()) {
    unsigned Opcode = IsSigned ? AMDGPU::V_BFE_I32_e64 : AMDGPU::V_BFE_U32_e64;
    SDValue Off = CurDAG->getTargetConstant(Offset, DL, MVT::i32);
    SDValue W = CurDAG->getTargetConstant(Width, DL, MVT::i32);

    return CurDAG->getMachineNode(Opcode, DL, MVT::i32, Val, Off, W);
  }
  unsigned Opcode = IsSigned ? AMDGPU::S_BFE_I32 : AMDGPU::S_BFE_U32;
  // Transformation function, pack the offset and width of a BFE into
  // the format expected by the S_BFE_I32 / S_BFE_U32. In the second
  // source, bits [5:0] contain the offset and bits [22:16] the width.
  uint32_t PackedVal = Offset | (Width << 16);
  SDValue PackedConst = CurDAG->getTargetConstant(PackedVal, DL, MVT::i32);

  return CurDAG->getMachineNode(Opcode, DL, MVT::i32, Val, PackedConst);
}

void AMDGPUDAGToDAGISel::SelectS_BFEFromShifts(SDNode *N) {
  // "(a << b) srl c)" ---> "BFE_U32 a, (c-b), (32-c)
  // "(a << b) sra c)" ---> "BFE_I32 a, (c-b), (32-c)
  // Predicate: 0 < b <= c < 32

  const SDValue &Shl = N->getOperand(0);
  ConstantSDNode *B = dyn_cast<ConstantSDNode>(Shl->getOperand(1));
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));

  if (B && C) {
    uint32_t BVal = B->getZExtValue();
    uint32_t CVal = C->getZExtValue();

    if (0 < BVal && BVal <= CVal && CVal < 32) {
      bool Signed = N->getOpcode() == ISD::SRA;
      ReplaceNode(N, getBFE32(Signed, SDLoc(N), Shl.getOperand(0), CVal - BVal,
                  32 - CVal));
      return;
    }
  }
  SelectCode(N);
}

void AMDGPUDAGToDAGISel::SelectS_BFE(SDNode *N) {
  switch (N->getOpcode()) {
  case ISD::AND:
    if (N->getOperand(0).getOpcode() == ISD::SRL) {
      // "(a srl b) & mask" ---> "BFE_U32 a, b, popcount(mask)"
      // Predicate: isMask(mask)
      const SDValue &Srl = N->getOperand(0);
      ConstantSDNode *Shift = dyn_cast<ConstantSDNode>(Srl.getOperand(1));
      ConstantSDNode *Mask = dyn_cast<ConstantSDNode>(N->getOperand(1));

      if (Shift && Mask) {
        uint32_t ShiftVal = Shift->getZExtValue();
        uint32_t MaskVal = Mask->getZExtValue();

        if (isMask_32(MaskVal)) {
          uint32_t WidthVal = llvm::popcount(MaskVal);
          ReplaceNode(N, getBFE32(false, SDLoc(N), Srl.getOperand(0), ShiftVal,
                                  WidthVal));
          return;
        }
      }
    }
    break;
  case ISD::SRL:
    if (N->getOperand(0).getOpcode() == ISD::AND) {
      // "(a & mask) srl b)" ---> "BFE_U32 a, b, popcount(mask >> b)"
      // Predicate: isMask(mask >> b)
      const SDValue &And = N->getOperand(0);
      ConstantSDNode *Shift = dyn_cast<ConstantSDNode>(N->getOperand(1));
      ConstantSDNode *Mask = dyn_cast<ConstantSDNode>(And->getOperand(1));

      if (Shift && Mask) {
        uint32_t ShiftVal = Shift->getZExtValue();
        uint32_t MaskVal = Mask->getZExtValue() >> ShiftVal;

        if (isMask_32(MaskVal)) {
          uint32_t WidthVal = llvm::popcount(MaskVal);
          ReplaceNode(N, getBFE32(false, SDLoc(N), And.getOperand(0), ShiftVal,
                      WidthVal));
          return;
        }
      }
    } else if (N->getOperand(0).getOpcode() == ISD::SHL) {
      SelectS_BFEFromShifts(N);
      return;
    }
    break;
  case ISD::SRA:
    if (N->getOperand(0).getOpcode() == ISD::SHL) {
      SelectS_BFEFromShifts(N);
      return;
    }
    break;

  case ISD::SIGN_EXTEND_INREG: {
    // sext_inreg (srl x, 16), i8 -> bfe_i32 x, 16, 8
    SDValue Src = N->getOperand(0);
    if (Src.getOpcode() != ISD::SRL)
      break;

    const ConstantSDNode *Amt = dyn_cast<ConstantSDNode>(Src.getOperand(1));
    if (!Amt)
      break;

    unsigned Width = cast<VTSDNode>(N->getOperand(1))->getVT().getSizeInBits();
    ReplaceNode(N, getBFE32(true, SDLoc(N), Src.getOperand(0),
                            Amt->getZExtValue(), Width));
    return;
  }
  }

  SelectCode(N);
}

bool AMDGPUDAGToDAGISel::isCBranchSCC(const SDNode *N) const {
  assert(N->getOpcode() == ISD::BRCOND);
  if (!N->hasOneUse())
    return false;

  SDValue Cond = N->getOperand(1);
  if (Cond.getOpcode() == ISD::CopyToReg)
    Cond = Cond.getOperand(2);

  if (Cond.getOpcode() != ISD::SETCC || !Cond.hasOneUse())
    return false;

  MVT VT = Cond.getOperand(0).getSimpleValueType();
  if (VT == MVT::i32)
    return true;

  if (VT == MVT::i64) {
    auto ST = static_cast<const GCNSubtarget *>(Subtarget);

    ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
    return (CC == ISD::SETEQ || CC == ISD::SETNE) && ST->hasScalarCompareEq64();
  }

  return false;
}

static SDValue combineBallotPattern(SDValue VCMP, bool &Negate) {
  assert(VCMP->getOpcode() == AMDGPUISD::SETCC);
  // Special case for amdgcn.ballot:
  // %Cond = i1 (and/or combination of i1 ISD::SETCCs)
  // %VCMP = i(WaveSize) AMDGPUISD::SETCC (ext %Cond), 0, setne/seteq
  // =>
  // Use i1 %Cond value instead of i(WaveSize) %VCMP.
  // This is possible because divergent ISD::SETCC is selected as V_CMP and
  // Cond becomes a i(WaveSize) full mask value.
  // Note that ballot doesn't use SETEQ condition but its easy to support it
  // here for completeness, so in this case Negate is set true on return.
  auto VCMP_CC = cast<CondCodeSDNode>(VCMP.getOperand(2))->get();
  if ((VCMP_CC == ISD::SETEQ || VCMP_CC == ISD::SETNE) &&
      isNullConstant(VCMP.getOperand(1))) {

    auto Cond = VCMP.getOperand(0);
    if (ISD::isExtOpcode(Cond->getOpcode())) // Skip extension.
      Cond = Cond.getOperand(0);

    if (isBoolSGPR(Cond)) {
      Negate = VCMP_CC == ISD::SETEQ;
      return Cond;
    }
  }
  return SDValue();
}

void AMDGPUDAGToDAGISel::SelectBRCOND(SDNode *N) {
  SDValue Cond = N->getOperand(1);

  if (Cond.isUndef()) {
    CurDAG->SelectNodeTo(N, AMDGPU::SI_BR_UNDEF, MVT::Other,
                         N->getOperand(2), N->getOperand(0));
    return;
  }

  const GCNSubtarget *ST = static_cast<const GCNSubtarget *>(Subtarget);
  const SIRegisterInfo *TRI = ST->getRegisterInfo();

  bool UseSCCBr = isCBranchSCC(N) && isUniformBr(N);
  bool AndExec = !UseSCCBr;
  bool Negate = false;

  if (Cond.getOpcode() == ISD::SETCC &&
      Cond->getOperand(0)->getOpcode() == AMDGPUISD::SETCC) {
    SDValue VCMP = Cond->getOperand(0);
    auto CC = cast<CondCodeSDNode>(Cond->getOperand(2))->get();
    if ((CC == ISD::SETEQ || CC == ISD::SETNE) &&
        isNullConstant(Cond->getOperand(1)) &&
        // We may encounter ballot.i64 in wave32 mode on -O0.
        VCMP.getValueType().getSizeInBits() == ST->getWavefrontSize()) {
      // %VCMP = i(WaveSize) AMDGPUISD::SETCC ...
      // %C = i1 ISD::SETCC %VCMP, 0, setne/seteq
      // BRCOND i1 %C, %BB
      // =>
      // %VCMP = i(WaveSize) AMDGPUISD::SETCC ...
      // VCC = COPY i(WaveSize) %VCMP
      // S_CBRANCH_VCCNZ/VCCZ %BB
      Negate = CC == ISD::SETEQ;
      bool NegatedBallot = false;
      if (auto BallotCond = combineBallotPattern(VCMP, NegatedBallot)) {
        Cond = BallotCond;
        UseSCCBr = !BallotCond->isDivergent();
        Negate = Negate ^ NegatedBallot;
      } else {
        // TODO: don't use SCC here assuming that AMDGPUISD::SETCC is always
        // selected as V_CMP, but this may change for uniform condition.
        Cond = VCMP;
        UseSCCBr = false;
      }
    }
    // Cond is either V_CMP resulted from AMDGPUISD::SETCC or a combination of
    // V_CMPs resulted from ballot or ballot has uniform condition and SCC is
    // used.
    AndExec = false;
  }

  unsigned BrOp =
      UseSCCBr ? (Negate ? AMDGPU::S_CBRANCH_SCC0 : AMDGPU::S_CBRANCH_SCC1)
               : (Negate ? AMDGPU::S_CBRANCH_VCCZ : AMDGPU::S_CBRANCH_VCCNZ);
  Register CondReg = UseSCCBr ? AMDGPU::SCC : TRI->getVCC();
  SDLoc SL(N);

  if (AndExec) {
    // This is the case that we are selecting to S_CBRANCH_VCCNZ.  We have not
    // analyzed what generates the vcc value, so we do not know whether vcc
    // bits for disabled lanes are 0.  Thus we need to mask out bits for
    // disabled lanes.
    //
    // For the case that we select S_CBRANCH_SCC1 and it gets
    // changed to S_CBRANCH_VCCNZ in SIFixSGPRCopies, SIFixSGPRCopies calls
    // SIInstrInfo::moveToVALU which inserts the S_AND).
    //
    // We could add an analysis of what generates the vcc value here and omit
    // the S_AND when is unnecessary. But it would be better to add a separate
    // pass after SIFixSGPRCopies to do the unnecessary S_AND removal, so it
    // catches both cases.
    Cond = SDValue(CurDAG->getMachineNode(ST->isWave32() ? AMDGPU::S_AND_B32
                                                         : AMDGPU::S_AND_B64,
                     SL, MVT::i1,
                     CurDAG->getRegister(ST->isWave32() ? AMDGPU::EXEC_LO
                                                        : AMDGPU::EXEC,
                                         MVT::i1),
                    Cond),
                   0);
  }

  SDValue VCC = CurDAG->getCopyToReg(N->getOperand(0), SL, CondReg, Cond);
  CurDAG->SelectNodeTo(N, BrOp, MVT::Other,
                       N->getOperand(2), // Basic Block
                       VCC.getValue(0));
}

void AMDGPUDAGToDAGISel::SelectFP_EXTEND(SDNode *N) {
  if (Subtarget->hasSALUFloatInsts() && N->getValueType(0) == MVT::f32 &&
      !N->isDivergent()) {
    SDValue Src = N->getOperand(0);
    if (Src.getValueType() == MVT::f16) {
      if (isExtractHiElt(Src, Src)) {
        CurDAG->SelectNodeTo(N, AMDGPU::S_CVT_HI_F32_F16, N->getVTList(),
                             {Src});
        return;
      }
    }
  }

  SelectCode(N);
}

void AMDGPUDAGToDAGISel::SelectDSAppendConsume(SDNode *N, unsigned IntrID) {
  // The address is assumed to be uniform, so if it ends up in a VGPR, it will
  // be copied to an SGPR with readfirstlane.
  unsigned Opc = IntrID == Intrinsic::amdgcn_ds_append ?
    AMDGPU::DS_APPEND : AMDGPU::DS_CONSUME;

  SDValue Chain = N->getOperand(0);
  SDValue Ptr = N->getOperand(2);
  MemIntrinsicSDNode *M = cast<MemIntrinsicSDNode>(N);
  MachineMemOperand *MMO = M->getMemOperand();
  bool IsGDS = M->getAddressSpace() == AMDGPUAS::REGION_ADDRESS;

  SDValue Offset;
  if (CurDAG->isBaseWithConstantOffset(Ptr)) {
    SDValue PtrBase = Ptr.getOperand(0);
    SDValue PtrOffset = Ptr.getOperand(1);

    const APInt &OffsetVal = PtrOffset->getAsAPIntVal();
    if (isDSOffsetLegal(PtrBase, OffsetVal.getZExtValue())) {
      N = glueCopyToM0(N, PtrBase);
      Offset = CurDAG->getTargetConstant(OffsetVal, SDLoc(), MVT::i32);
    }
  }

  if (!Offset) {
    N = glueCopyToM0(N, Ptr);
    Offset = CurDAG->getTargetConstant(0, SDLoc(), MVT::i32);
  }

  SDValue Ops[] = {
    Offset,
    CurDAG->getTargetConstant(IsGDS, SDLoc(), MVT::i32),
    Chain,
    N->getOperand(N->getNumOperands() - 1) // New glue
  };

  SDNode *Selected = CurDAG->SelectNodeTo(N, Opc, N->getVTList(), Ops);
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected), {MMO});
}

// We need to handle this here because tablegen doesn't support matching
// instructions with multiple outputs.
void AMDGPUDAGToDAGISel::SelectDSBvhStackIntrinsic(SDNode *N) {
  unsigned Opc = AMDGPU::DS_BVH_STACK_RTN_B32;
  SDValue Ops[] = {N->getOperand(2), N->getOperand(3), N->getOperand(4),
                   N->getOperand(5), N->getOperand(0)};

  MemIntrinsicSDNode *M = cast<MemIntrinsicSDNode>(N);
  MachineMemOperand *MMO = M->getMemOperand();
  SDNode *Selected = CurDAG->SelectNodeTo(N, Opc, N->getVTList(), Ops);
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected), {MMO});
}

static unsigned gwsIntrinToOpcode(unsigned IntrID) {
  switch (IntrID) {
  case Intrinsic::amdgcn_ds_gws_init:
    return AMDGPU::DS_GWS_INIT;
  case Intrinsic::amdgcn_ds_gws_barrier:
    return AMDGPU::DS_GWS_BARRIER;
  case Intrinsic::amdgcn_ds_gws_sema_v:
    return AMDGPU::DS_GWS_SEMA_V;
  case Intrinsic::amdgcn_ds_gws_sema_br:
    return AMDGPU::DS_GWS_SEMA_BR;
  case Intrinsic::amdgcn_ds_gws_sema_p:
    return AMDGPU::DS_GWS_SEMA_P;
  case Intrinsic::amdgcn_ds_gws_sema_release_all:
    return AMDGPU::DS_GWS_SEMA_RELEASE_ALL;
  default:
    llvm_unreachable("not a gws intrinsic");
  }
}

void AMDGPUDAGToDAGISel::SelectDS_GWS(SDNode *N, unsigned IntrID) {
  if (!Subtarget->hasGWS() ||
      (IntrID == Intrinsic::amdgcn_ds_gws_sema_release_all &&
       !Subtarget->hasGWSSemaReleaseAll())) {
    // Let this error.
    SelectCode(N);
    return;
  }

  // Chain, intrinsic ID, vsrc, offset
  const bool HasVSrc = N->getNumOperands() == 4;
  assert(HasVSrc || N->getNumOperands() == 3);

  SDLoc SL(N);
  SDValue BaseOffset = N->getOperand(HasVSrc ? 3 : 2);
  int ImmOffset = 0;
  MemIntrinsicSDNode *M = cast<MemIntrinsicSDNode>(N);
  MachineMemOperand *MMO = M->getMemOperand();

  // Don't worry if the offset ends up in a VGPR. Only one lane will have
  // effect, so SIFixSGPRCopies will validly insert readfirstlane.

  // The resource id offset is computed as (<isa opaque base> + M0[21:16] +
  // offset field) % 64. Some versions of the programming guide omit the m0
  // part, or claim it's from offset 0.
  if (ConstantSDNode *ConstOffset = dyn_cast<ConstantSDNode>(BaseOffset)) {
    // If we have a constant offset, try to use the 0 in m0 as the base.
    // TODO: Look into changing the default m0 initialization value. If the
    // default -1 only set the low 16-bits, we could leave it as-is and add 1 to
    // the immediate offset.
    glueCopyToM0(N, CurDAG->getTargetConstant(0, SL, MVT::i32));
    ImmOffset = ConstOffset->getZExtValue();
  } else {
    if (CurDAG->isBaseWithConstantOffset(BaseOffset)) {
      ImmOffset = BaseOffset.getConstantOperandVal(1);
      BaseOffset = BaseOffset.getOperand(0);
    }

    // Prefer to do the shift in an SGPR since it should be possible to use m0
    // as the result directly. If it's already an SGPR, it will be eliminated
    // later.
    SDNode *SGPROffset
      = CurDAG->getMachineNode(AMDGPU::V_READFIRSTLANE_B32, SL, MVT::i32,
                               BaseOffset);
    // Shift to offset in m0
    SDNode *M0Base
      = CurDAG->getMachineNode(AMDGPU::S_LSHL_B32, SL, MVT::i32,
                               SDValue(SGPROffset, 0),
                               CurDAG->getTargetConstant(16, SL, MVT::i32));
    glueCopyToM0(N, SDValue(M0Base, 0));
  }

  SDValue Chain = N->getOperand(0);
  SDValue OffsetField = CurDAG->getTargetConstant(ImmOffset, SL, MVT::i32);

  const unsigned Opc = gwsIntrinToOpcode(IntrID);
  SmallVector<SDValue, 5> Ops;
  if (HasVSrc)
    Ops.push_back(N->getOperand(2));
  Ops.push_back(OffsetField);
  Ops.push_back(Chain);

  SDNode *Selected = CurDAG->SelectNodeTo(N, Opc, N->getVTList(), Ops);
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected), {MMO});
}

void AMDGPUDAGToDAGISel::SelectInterpP1F16(SDNode *N) {
  if (Subtarget->getLDSBankCount() != 16) {
    // This is a single instruction with a pattern.
    SelectCode(N);
    return;
  }

  SDLoc DL(N);

  // This requires 2 instructions. It is possible to write a pattern to support
  // this, but the generated isel emitter doesn't correctly deal with multiple
  // output instructions using the same physical register input. The copy to m0
  // is incorrectly placed before the second instruction.
  //
  // TODO: Match source modifiers.
  //
  // def : Pat <
  //   (int_amdgcn_interp_p1_f16
  //    (VOP3Mods f32:$src0, i32:$src0_modifiers),
  //                             (i32 timm:$attrchan), (i32 timm:$attr),
  //                             (i1 timm:$high), M0),
  //   (V_INTERP_P1LV_F16 $src0_modifiers, VGPR_32:$src0, timm:$attr,
  //       timm:$attrchan, 0,
  //       (V_INTERP_MOV_F32 2, timm:$attr, timm:$attrchan), timm:$high)> {
  //   let Predicates = [has16BankLDS];
  // }

  // 16 bank LDS
  SDValue ToM0 = CurDAG->getCopyToReg(CurDAG->getEntryNode(), DL, AMDGPU::M0,
                                      N->getOperand(5), SDValue());

  SDVTList VTs = CurDAG->getVTList(MVT::f32, MVT::Other);

  SDNode *InterpMov =
    CurDAG->getMachineNode(AMDGPU::V_INTERP_MOV_F32, DL, VTs, {
        CurDAG->getTargetConstant(2, DL, MVT::i32), // P0
        N->getOperand(3),  // Attr
        N->getOperand(2),  // Attrchan
        ToM0.getValue(1) // In glue
  });

  SDNode *InterpP1LV =
    CurDAG->getMachineNode(AMDGPU::V_INTERP_P1LV_F16, DL, MVT::f32, {
        CurDAG->getTargetConstant(0, DL, MVT::i32), // $src0_modifiers
        N->getOperand(1), // Src0
        N->getOperand(3), // Attr
        N->getOperand(2), // Attrchan
        CurDAG->getTargetConstant(0, DL, MVT::i32), // $src2_modifiers
        SDValue(InterpMov, 0), // Src2 - holds two f16 values selected by high
        N->getOperand(4), // high
        CurDAG->getTargetConstant(0, DL, MVT::i1), // $clamp
        CurDAG->getTargetConstant(0, DL, MVT::i32), // $omod
        SDValue(InterpMov, 1)
  });

  CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), SDValue(InterpP1LV, 0));
}

void AMDGPUDAGToDAGISel::SelectINTRINSIC_W_CHAIN(SDNode *N) {
  unsigned IntrID = N->getConstantOperandVal(1);
  switch (IntrID) {
  case Intrinsic::amdgcn_ds_append:
  case Intrinsic::amdgcn_ds_consume: {
    if (N->getValueType(0) != MVT::i32)
      break;
    SelectDSAppendConsume(N, IntrID);
    return;
  }
  case Intrinsic::amdgcn_ds_bvh_stack_rtn:
    SelectDSBvhStackIntrinsic(N);
    return;
  }

  SelectCode(N);
}

void AMDGPUDAGToDAGISel::SelectINTRINSIC_WO_CHAIN(SDNode *N) {
  unsigned IntrID = N->getConstantOperandVal(0);
  unsigned Opcode = AMDGPU::INSTRUCTION_LIST_END;
  SDNode *ConvGlueNode = N->getGluedNode();
  if (ConvGlueNode) {
    // FIXME: Possibly iterate over multiple glue nodes?
    assert(ConvGlueNode->getOpcode() == ISD::CONVERGENCECTRL_GLUE);
    ConvGlueNode = ConvGlueNode->getOperand(0).getNode();
    ConvGlueNode =
        CurDAG->getMachineNode(TargetOpcode::CONVERGENCECTRL_GLUE, {},
                               MVT::Glue, SDValue(ConvGlueNode, 0));
  } else {
    ConvGlueNode = nullptr;
  }
  switch (IntrID) {
  case Intrinsic::amdgcn_wqm:
    Opcode = AMDGPU::WQM;
    break;
  case Intrinsic::amdgcn_softwqm:
    Opcode = AMDGPU::SOFT_WQM;
    break;
  case Intrinsic::amdgcn_wwm:
  case Intrinsic::amdgcn_strict_wwm:
    Opcode = AMDGPU::STRICT_WWM;
    break;
  case Intrinsic::amdgcn_strict_wqm:
    Opcode = AMDGPU::STRICT_WQM;
    break;
  case Intrinsic::amdgcn_interp_p1_f16:
    SelectInterpP1F16(N);
    return;
  default:
    SelectCode(N);
    break;
  }

  if (Opcode != AMDGPU::INSTRUCTION_LIST_END) {
    SDValue Src = N->getOperand(1);
    CurDAG->SelectNodeTo(N, Opcode, N->getVTList(), {Src});
  }

  if (ConvGlueNode) {
    SmallVector<SDValue, 4> NewOps(N->op_begin(), N->op_end());
    NewOps.push_back(SDValue(ConvGlueNode, 0));
    CurDAG->MorphNodeTo(N, N->getOpcode(), N->getVTList(), NewOps);
  }
}

void AMDGPUDAGToDAGISel::SelectINTRINSIC_VOID(SDNode *N) {
  unsigned IntrID = N->getConstantOperandVal(1);
  switch (IntrID) {
  case Intrinsic::amdgcn_ds_gws_init:
  case Intrinsic::amdgcn_ds_gws_barrier:
  case Intrinsic::amdgcn_ds_gws_sema_v:
  case Intrinsic::amdgcn_ds_gws_sema_br:
  case Intrinsic::amdgcn_ds_gws_sema_p:
  case Intrinsic::amdgcn_ds_gws_sema_release_all:
    SelectDS_GWS(N, IntrID);
    return;
  default:
    break;
  }

  SelectCode(N);
}

void AMDGPUDAGToDAGISel::SelectWAVE_ADDRESS(SDNode *N) {
  SDValue Log2WaveSize =
    CurDAG->getTargetConstant(Subtarget->getWavefrontSizeLog2(), SDLoc(N), MVT::i32);
  CurDAG->SelectNodeTo(N, AMDGPU::S_LSHR_B32, N->getVTList(),
                       {N->getOperand(0), Log2WaveSize});
}

void AMDGPUDAGToDAGISel::SelectSTACKRESTORE(SDNode *N) {
  SDValue SrcVal = N->getOperand(1);
  if (SrcVal.getValueType() != MVT::i32) {
    SelectCode(N); // Emit default error
    return;
  }

  SDValue CopyVal;
  Register SP = TLI->getStackPointerRegisterToSaveRestore();
  SDLoc SL(N);

  if (SrcVal.getOpcode() == AMDGPUISD::WAVE_ADDRESS) {
    CopyVal = SrcVal.getOperand(0);
  } else {
    SDValue Log2WaveSize = CurDAG->getTargetConstant(
        Subtarget->getWavefrontSizeLog2(), SL, MVT::i32);

    if (N->isDivergent()) {
      SrcVal = SDValue(CurDAG->getMachineNode(AMDGPU::V_READFIRSTLANE_B32, SL,
                                              MVT::i32, SrcVal),
                       0);
    }

    CopyVal = SDValue(CurDAG->getMachineNode(AMDGPU::S_LSHL_B32, SL, MVT::i32,
                                             {SrcVal, Log2WaveSize}),
                      0);
  }

  SDValue CopyToSP = CurDAG->getCopyToReg(N->getOperand(0), SL, SP, CopyVal);
  CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), CopyToSP);
}

bool AMDGPUDAGToDAGISel::SelectVOP3ModsImpl(SDValue In, SDValue &Src,
                                            unsigned &Mods,
                                            bool IsCanonicalizing,
                                            bool AllowAbs) const {
  Mods = SISrcMods::NONE;
  Src = In;

  if (Src.getOpcode() == ISD::FNEG) {
    Mods |= SISrcMods::NEG;
    Src = Src.getOperand(0);
  } else if (Src.getOpcode() == ISD::FSUB && IsCanonicalizing) {
    // Fold fsub [+-]0 into fneg. This may not have folded depending on the
    // denormal mode, but we're implicitly canonicalizing in a source operand.
    auto *LHS = dyn_cast<ConstantFPSDNode>(Src.getOperand(0));
    if (LHS && LHS->isZero()) {
      Mods |= SISrcMods::NEG;
      Src = Src.getOperand(1);
    }
  }

  if (AllowAbs && Src.getOpcode() == ISD::FABS) {
    Mods |= SISrcMods::ABS;
    Src = Src.getOperand(0);
  }

  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3Mods(SDValue In, SDValue &Src,
                                        SDValue &SrcMods) const {
  unsigned Mods;
  if (SelectVOP3ModsImpl(In, Src, Mods, /*IsCanonicalizing=*/true,
                         /*AllowAbs=*/true)) {
    SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectVOP3ModsNonCanonicalizing(
    SDValue In, SDValue &Src, SDValue &SrcMods) const {
  unsigned Mods;
  if (SelectVOP3ModsImpl(In, Src, Mods, /*IsCanonicalizing=*/false,
                         /*AllowAbs=*/true)) {
    SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectVOP3BMods(SDValue In, SDValue &Src,
                                         SDValue &SrcMods) const {
  unsigned Mods;
  if (SelectVOP3ModsImpl(In, Src, Mods,
                         /*IsCanonicalizing=*/true,
                         /*AllowAbs=*/false)) {
    SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectVOP3NoMods(SDValue In, SDValue &Src) const {
  if (In.getOpcode() == ISD::FABS || In.getOpcode() == ISD::FNEG)
    return false;

  Src = In;
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVINTERPModsImpl(SDValue In, SDValue &Src,
                                               SDValue &SrcMods,
                                               bool OpSel) const {
  unsigned Mods;
  if (SelectVOP3ModsImpl(In, Src, Mods,
                         /*IsCanonicalizing=*/true,
                         /*AllowAbs=*/false)) {
    if (OpSel)
      Mods |= SISrcMods::OP_SEL_0;
    SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectVINTERPMods(SDValue In, SDValue &Src,
                                           SDValue &SrcMods) const {
  return SelectVINTERPModsImpl(In, Src, SrcMods, /* OpSel */ false);
}

bool AMDGPUDAGToDAGISel::SelectVINTERPModsHi(SDValue In, SDValue &Src,
                                             SDValue &SrcMods) const {
  return SelectVINTERPModsImpl(In, Src, SrcMods, /* OpSel */ true);
}

bool AMDGPUDAGToDAGISel::SelectVOP3Mods0(SDValue In, SDValue &Src,
                                         SDValue &SrcMods, SDValue &Clamp,
                                         SDValue &Omod) const {
  SDLoc DL(In);
  Clamp = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Omod = CurDAG->getTargetConstant(0, DL, MVT::i1);

  return SelectVOP3Mods(In, Src, SrcMods);
}

bool AMDGPUDAGToDAGISel::SelectVOP3BMods0(SDValue In, SDValue &Src,
                                          SDValue &SrcMods, SDValue &Clamp,
                                          SDValue &Omod) const {
  SDLoc DL(In);
  Clamp = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Omod = CurDAG->getTargetConstant(0, DL, MVT::i1);

  return SelectVOP3BMods(In, Src, SrcMods);
}

bool AMDGPUDAGToDAGISel::SelectVOP3OMods(SDValue In, SDValue &Src,
                                         SDValue &Clamp, SDValue &Omod) const {
  Src = In;

  SDLoc DL(In);
  Clamp = CurDAG->getTargetConstant(0, DL, MVT::i1);
  Omod = CurDAG->getTargetConstant(0, DL, MVT::i1);

  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3PMods(SDValue In, SDValue &Src,
                                         SDValue &SrcMods, bool IsDOT) const {
  unsigned Mods = SISrcMods::NONE;
  Src = In;

  // TODO: Handle G_FSUB 0 as fneg
  if (Src.getOpcode() == ISD::FNEG) {
    Mods ^= (SISrcMods::NEG | SISrcMods::NEG_HI);
    Src = Src.getOperand(0);
  }

  if (Src.getOpcode() == ISD::BUILD_VECTOR && Src.getNumOperands() == 2 &&
      (!IsDOT || !Subtarget->hasDOTOpSelHazard())) {
    unsigned VecMods = Mods;

    SDValue Lo = stripBitcast(Src.getOperand(0));
    SDValue Hi = stripBitcast(Src.getOperand(1));

    if (Lo.getOpcode() == ISD::FNEG) {
      Lo = stripBitcast(Lo.getOperand(0));
      Mods ^= SISrcMods::NEG;
    }

    if (Hi.getOpcode() == ISD::FNEG) {
      Hi = stripBitcast(Hi.getOperand(0));
      Mods ^= SISrcMods::NEG_HI;
    }

    if (isExtractHiElt(Lo, Lo))
      Mods |= SISrcMods::OP_SEL_0;

    if (isExtractHiElt(Hi, Hi))
      Mods |= SISrcMods::OP_SEL_1;

    unsigned VecSize = Src.getValueSizeInBits();
    Lo = stripExtractLoElt(Lo);
    Hi = stripExtractLoElt(Hi);

    if (Lo.getValueSizeInBits() > VecSize) {
      Lo = CurDAG->getTargetExtractSubreg(
        (VecSize > 32) ? AMDGPU::sub0_sub1 : AMDGPU::sub0, SDLoc(In),
        MVT::getIntegerVT(VecSize), Lo);
    }

    if (Hi.getValueSizeInBits() > VecSize) {
      Hi = CurDAG->getTargetExtractSubreg(
        (VecSize > 32) ? AMDGPU::sub0_sub1 : AMDGPU::sub0, SDLoc(In),
        MVT::getIntegerVT(VecSize), Hi);
    }

    assert(Lo.getValueSizeInBits() <= VecSize &&
           Hi.getValueSizeInBits() <= VecSize);

    if (Lo == Hi && !isInlineImmediate(Lo.getNode())) {
      // Really a scalar input. Just select from the low half of the register to
      // avoid packing.

      if (VecSize == 32 || VecSize == Lo.getValueSizeInBits()) {
        Src = Lo;
      } else {
        assert(Lo.getValueSizeInBits() == 32 && VecSize == 64);

        SDLoc SL(In);
        SDValue Undef = SDValue(
          CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, SL,
                                 Lo.getValueType()), 0);
        auto RC = Lo->isDivergent() ? AMDGPU::VReg_64RegClassID
                                    : AMDGPU::SReg_64RegClassID;
        const SDValue Ops[] = {
          CurDAG->getTargetConstant(RC, SL, MVT::i32),
          Lo, CurDAG->getTargetConstant(AMDGPU::sub0, SL, MVT::i32),
          Undef, CurDAG->getTargetConstant(AMDGPU::sub1, SL, MVT::i32) };

        Src = SDValue(CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, SL,
                                             Src.getValueType(), Ops), 0);
      }
      SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
      return true;
    }

    if (VecSize == 64 && Lo == Hi && isa<ConstantFPSDNode>(Lo)) {
      uint64_t Lit = cast<ConstantFPSDNode>(Lo)->getValueAPF()
                      .bitcastToAPInt().getZExtValue();
      if (AMDGPU::isInlinableLiteral32(Lit, Subtarget->hasInv2PiInlineImm())) {
        Src = CurDAG->getTargetConstant(Lit, SDLoc(In), MVT::i64);
        SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
        return true;
      }
    }

    Mods = VecMods;
  }

  // Packed instructions do not have abs modifiers.
  Mods |= SISrcMods::OP_SEL_1;

  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3PModsDOT(SDValue In, SDValue &Src,
                                            SDValue &SrcMods) const {
  return SelectVOP3PMods(In, Src, SrcMods, true);
}

bool AMDGPUDAGToDAGISel::SelectVOP3PModsNeg(SDValue In, SDValue &Src) const {
  const ConstantSDNode *C = cast<ConstantSDNode>(In);
  // Literal i1 value set in intrinsic, represents SrcMods for the next operand.
  // 1 promotes packed values to signed, 0 treats them as unsigned.
  assert(C->getAPIntValue().getBitWidth() == 1 && "expected i1 value");

  unsigned Mods = SISrcMods::OP_SEL_1;
  unsigned SrcSign = C->getZExtValue();
  if (SrcSign == 1)
    Mods ^= SISrcMods::NEG;

  Src = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectWMMAOpSelVOP3PMods(SDValue In,
                                                  SDValue &Src) const {
  const ConstantSDNode *C = cast<ConstantSDNode>(In);
  assert(C->getAPIntValue().getBitWidth() == 1 && "expected i1 value");

  unsigned Mods = SISrcMods::OP_SEL_1;
  unsigned SrcVal = C->getZExtValue();
  if (SrcVal == 1)
    Mods |= SISrcMods::OP_SEL_0;

  Src = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

static MachineSDNode *buildRegSequence32(SmallVectorImpl<SDValue> &Elts,
                                         llvm::SelectionDAG *CurDAG,
                                         const SDLoc &DL) {
  unsigned DstRegClass;
  EVT DstTy;
  switch (Elts.size()) {
  case 8:
    DstRegClass = AMDGPU::VReg_256RegClassID;
    DstTy = MVT::v8i32;
    break;
  case 4:
    DstRegClass = AMDGPU::VReg_128RegClassID;
    DstTy = MVT::v4i32;
    break;
  case 2:
    DstRegClass = AMDGPU::VReg_64RegClassID;
    DstTy = MVT::v2i32;
    break;
  default:
    llvm_unreachable("unhandled Reg sequence size");
  }

  SmallVector<SDValue, 17> Ops;
  Ops.push_back(CurDAG->getTargetConstant(DstRegClass, DL, MVT::i32));
  for (unsigned i = 0; i < Elts.size(); ++i) {
    Ops.push_back(Elts[i]);
    Ops.push_back(CurDAG->getTargetConstant(
        SIRegisterInfo::getSubRegFromChannel(i), DL, MVT::i32));
  }
  return CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL, DstTy, Ops);
}

static MachineSDNode *buildRegSequence16(SmallVectorImpl<SDValue> &Elts,
                                         llvm::SelectionDAG *CurDAG,
                                         const SDLoc &DL) {
  SmallVector<SDValue, 8> PackedElts;
  assert("unhandled Reg sequence size" &&
         (Elts.size() == 8 || Elts.size() == 16));

  // Pack 16-bit elements in pairs into 32-bit register. If both elements are
  // unpacked from 32-bit source use it, otherwise pack them using v_perm.
  for (unsigned i = 0; i < Elts.size(); i += 2) {
    SDValue LoSrc = stripExtractLoElt(stripBitcast(Elts[i]));
    SDValue HiSrc;
    if (isExtractHiElt(Elts[i + 1], HiSrc) && LoSrc == HiSrc) {
      PackedElts.push_back(HiSrc);
    } else {
      SDValue PackLoLo = CurDAG->getTargetConstant(0x05040100, DL, MVT::i32);
      MachineSDNode *Packed =
          CurDAG->getMachineNode(AMDGPU::V_PERM_B32_e64, DL, MVT::i32,
                                 {Elts[i + 1], Elts[i], PackLoLo});
      PackedElts.push_back(SDValue(Packed, 0));
    }
  }

  return buildRegSequence32(PackedElts, CurDAG, DL);
}

static MachineSDNode *buildRegSequence(SmallVectorImpl<SDValue> &Elts,
                                       llvm::SelectionDAG *CurDAG,
                                       const SDLoc &DL, unsigned ElementSize) {
  if (ElementSize == 16)
    return buildRegSequence16(Elts, CurDAG, DL);
  if (ElementSize == 32)
    return buildRegSequence32(Elts, CurDAG, DL);
  llvm_unreachable("Unhandled element size");
}

static void selectWMMAModsNegAbs(unsigned ModOpcode, unsigned &Mods,
                                 SmallVectorImpl<SDValue> &Elts, SDValue &Src,
                                 llvm::SelectionDAG *CurDAG, const SDLoc &DL,
                                 unsigned ElementSize) {
  if (ModOpcode == ISD::FNEG) {
    Mods |= SISrcMods::NEG;
    // Check if all elements also have abs modifier
    SmallVector<SDValue, 8> NegAbsElts;
    for (auto El : Elts) {
      if (El.getOpcode() != ISD::FABS)
        break;
      NegAbsElts.push_back(El->getOperand(0));
    }
    if (Elts.size() != NegAbsElts.size()) {
      // Neg
      Src = SDValue(buildRegSequence(Elts, CurDAG, DL, ElementSize), 0);
    } else {
      // Neg and Abs
      Mods |= SISrcMods::NEG_HI;
      Src = SDValue(buildRegSequence(NegAbsElts, CurDAG, DL, ElementSize), 0);
    }
  } else {
    assert(ModOpcode == ISD::FABS);
    // Abs
    Mods |= SISrcMods::NEG_HI;
    Src = SDValue(buildRegSequence(Elts, CurDAG, DL, ElementSize), 0);
  }
}

// Check all f16 elements for modifiers while looking through b32 and v2b16
// build vector, stop if element does not satisfy ModifierCheck.
static void
checkWMMAElementsModifiersF16(BuildVectorSDNode *BV,
                              std::function<bool(SDValue)> ModifierCheck) {
  for (unsigned i = 0; i < BV->getNumOperands(); ++i) {
    if (auto *F16Pair =
            dyn_cast<BuildVectorSDNode>(stripBitcast(BV->getOperand(i)))) {
      for (unsigned i = 0; i < F16Pair->getNumOperands(); ++i) {
        SDValue ElF16 = stripBitcast(F16Pair->getOperand(i));
        if (!ModifierCheck(ElF16))
          break;
      }
    }
  }
}

bool AMDGPUDAGToDAGISel::SelectWMMAModsF16Neg(SDValue In, SDValue &Src,
                                              SDValue &SrcMods) const {
  Src = In;
  unsigned Mods = SISrcMods::OP_SEL_1;

  // mods are on f16 elements
  if (auto *BV = dyn_cast<BuildVectorSDNode>(stripBitcast(In))) {
    SmallVector<SDValue, 8> EltsF16;

    checkWMMAElementsModifiersF16(BV, [&](SDValue Element) -> bool {
      if (Element.getOpcode() != ISD::FNEG)
        return false;
      EltsF16.push_back(Element.getOperand(0));
      return true;
    });

    // All elements have neg modifier
    if (BV->getNumOperands() * 2 == EltsF16.size()) {
      Src = SDValue(buildRegSequence16(EltsF16, CurDAG, SDLoc(In)), 0);
      Mods |= SISrcMods::NEG;
      Mods |= SISrcMods::NEG_HI;
    }
  }

  // mods are on v2f16 elements
  if (auto *BV = dyn_cast<BuildVectorSDNode>(stripBitcast(In))) {
    SmallVector<SDValue, 8> EltsV2F16;
    for (unsigned i = 0; i < BV->getNumOperands(); ++i) {
      SDValue ElV2f16 = stripBitcast(BV->getOperand(i));
      // Based on first element decide which mod we match, neg or abs
      if (ElV2f16.getOpcode() != ISD::FNEG)
        break;
      EltsV2F16.push_back(ElV2f16.getOperand(0));
    }

    // All pairs of elements have neg modifier
    if (BV->getNumOperands() == EltsV2F16.size()) {
      Src = SDValue(buildRegSequence32(EltsV2F16, CurDAG, SDLoc(In)), 0);
      Mods |= SISrcMods::NEG;
      Mods |= SISrcMods::NEG_HI;
    }
  }

  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectWMMAModsF16NegAbs(SDValue In, SDValue &Src,
                                                 SDValue &SrcMods) const {
  Src = In;
  unsigned Mods = SISrcMods::OP_SEL_1;
  unsigned ModOpcode;

  // mods are on f16 elements
  if (auto *BV = dyn_cast<BuildVectorSDNode>(stripBitcast(In))) {
    SmallVector<SDValue, 8> EltsF16;
    checkWMMAElementsModifiersF16(BV, [&](SDValue ElF16) -> bool {
      // Based on first element decide which mod we match, neg or abs
      if (EltsF16.empty())
        ModOpcode = (ElF16.getOpcode() == ISD::FNEG) ? ISD::FNEG : ISD::FABS;
      if (ElF16.getOpcode() != ModOpcode)
        return false;
      EltsF16.push_back(ElF16.getOperand(0));
      return true;
    });

    // All elements have ModOpcode modifier
    if (BV->getNumOperands() * 2 == EltsF16.size())
      selectWMMAModsNegAbs(ModOpcode, Mods, EltsF16, Src, CurDAG, SDLoc(In),
                           16);
  }

  // mods are on v2f16 elements
  if (auto *BV = dyn_cast<BuildVectorSDNode>(stripBitcast(In))) {
    SmallVector<SDValue, 8> EltsV2F16;

    for (unsigned i = 0; i < BV->getNumOperands(); ++i) {
      SDValue ElV2f16 = stripBitcast(BV->getOperand(i));
      // Based on first element decide which mod we match, neg or abs
      if (EltsV2F16.empty())
        ModOpcode = (ElV2f16.getOpcode() == ISD::FNEG) ? ISD::FNEG : ISD::FABS;
      if (ElV2f16->getOpcode() != ModOpcode)
        break;
      EltsV2F16.push_back(ElV2f16->getOperand(0));
    }

    // All elements have ModOpcode modifier
    if (BV->getNumOperands() == EltsV2F16.size())
      selectWMMAModsNegAbs(ModOpcode, Mods, EltsV2F16, Src, CurDAG, SDLoc(In),
                           32);
  }

  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectWMMAModsF32NegAbs(SDValue In, SDValue &Src,
                                                 SDValue &SrcMods) const {
  Src = In;
  unsigned Mods = SISrcMods::OP_SEL_1;
  SmallVector<SDValue, 8> EltsF32;

  if (auto *BV = dyn_cast<BuildVectorSDNode>(stripBitcast(In))) {
    assert(BV->getNumOperands() > 0);
    // Based on first element decide which mod we match, neg or abs
    SDValue ElF32 = stripBitcast(BV->getOperand(0));
    unsigned ModOpcode =
        (ElF32.getOpcode() == ISD::FNEG) ? ISD::FNEG : ISD::FABS;
    for (unsigned i = 0; i < BV->getNumOperands(); ++i) {
      SDValue ElF32 = stripBitcast(BV->getOperand(i));
      if (ElF32.getOpcode() != ModOpcode)
        break;
      EltsF32.push_back(ElF32.getOperand(0));
    }

    // All elements had ModOpcode modifier
    if (BV->getNumOperands() == EltsF32.size())
      selectWMMAModsNegAbs(ModOpcode, Mods, EltsF32, Src, CurDAG, SDLoc(In),
                           32);
  }

  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectWMMAVISrc(SDValue In, SDValue &Src) const {
  if (auto *BV = dyn_cast<BuildVectorSDNode>(In)) {
    BitVector UndefElements;
    if (SDValue Splat = BV->getSplatValue(&UndefElements))
      if (isInlineImmediate(Splat.getNode())) {
        if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Splat)) {
          unsigned Imm = C->getAPIntValue().getSExtValue();
          Src = CurDAG->getTargetConstant(Imm, SDLoc(In), MVT::i32);
          return true;
        }
        if (const ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Splat)) {
          unsigned Imm = C->getValueAPF().bitcastToAPInt().getSExtValue();
          Src = CurDAG->getTargetConstant(Imm, SDLoc(In), MVT::i32);
          return true;
        }
        llvm_unreachable("unhandled Constant node");
      }
  }

  // 16 bit splat
  SDValue SplatSrc32 = stripBitcast(In);
  if (auto *SplatSrc32BV = dyn_cast<BuildVectorSDNode>(SplatSrc32))
    if (SDValue Splat32 = SplatSrc32BV->getSplatValue()) {
      SDValue SplatSrc16 = stripBitcast(Splat32);
      if (auto *SplatSrc16BV = dyn_cast<BuildVectorSDNode>(SplatSrc16))
        if (SDValue Splat = SplatSrc16BV->getSplatValue()) {
          const SIInstrInfo *TII = Subtarget->getInstrInfo();
          std::optional<APInt> RawValue;
          if (const ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Splat))
            RawValue = C->getValueAPF().bitcastToAPInt();
          else if (const ConstantSDNode *C = dyn_cast<ConstantSDNode>(Splat))
            RawValue = C->getAPIntValue();

          if (RawValue.has_value()) {
            EVT VT = In.getValueType().getScalarType();
            if (VT.getSimpleVT() == MVT::f16 || VT.getSimpleVT() == MVT::bf16) {
              APFloat FloatVal(VT.getSimpleVT() == MVT::f16
                                   ? APFloatBase::IEEEhalf()
                                   : APFloatBase::BFloat(),
                               RawValue.value());
              if (TII->isInlineConstant(FloatVal)) {
                Src = CurDAG->getTargetConstant(RawValue.value(), SDLoc(In),
                                                MVT::i16);
                return true;
              }
            } else if (VT.getSimpleVT() == MVT::i16) {
              if (TII->isInlineConstant(RawValue.value())) {
                Src = CurDAG->getTargetConstant(RawValue.value(), SDLoc(In),
                                                MVT::i16);
                return true;
              }
            } else
              llvm_unreachable("unknown 16-bit type");
          }
        }
    }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectSWMMACIndex8(SDValue In, SDValue &Src,
                                            SDValue &IndexKey) const {
  unsigned Key = 0;
  Src = In;

  if (In.getOpcode() == ISD::SRL) {
    const llvm::SDValue &ShiftSrc = In.getOperand(0);
    ConstantSDNode *ShiftAmt = dyn_cast<ConstantSDNode>(In.getOperand(1));
    if (ShiftSrc.getValueType().getSizeInBits() == 32 && ShiftAmt &&
        ShiftAmt->getZExtValue() % 8 == 0) {
      Key = ShiftAmt->getZExtValue() / 8;
      Src = ShiftSrc;
    }
  }

  IndexKey = CurDAG->getTargetConstant(Key, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectSWMMACIndex16(SDValue In, SDValue &Src,
                                             SDValue &IndexKey) const {
  unsigned Key = 0;
  Src = In;

  if (In.getOpcode() == ISD::SRL) {
    const llvm::SDValue &ShiftSrc = In.getOperand(0);
    ConstantSDNode *ShiftAmt = dyn_cast<ConstantSDNode>(In.getOperand(1));
    if (ShiftSrc.getValueType().getSizeInBits() == 32 && ShiftAmt &&
        ShiftAmt->getZExtValue() == 16) {
      Key = 1;
      Src = ShiftSrc;
    }
  }

  IndexKey = CurDAG->getTargetConstant(Key, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3OpSel(SDValue In, SDValue &Src,
                                         SDValue &SrcMods) const {
  Src = In;
  // FIXME: Handle op_sel
  SrcMods = CurDAG->getTargetConstant(0, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3OpSelMods(SDValue In, SDValue &Src,
                                             SDValue &SrcMods) const {
  // FIXME: Handle op_sel
  return SelectVOP3Mods(In, Src, SrcMods);
}

// The return value is not whether the match is possible (which it always is),
// but whether or not it a conversion is really used.
bool AMDGPUDAGToDAGISel::SelectVOP3PMadMixModsImpl(SDValue In, SDValue &Src,
                                                   unsigned &Mods) const {
  Mods = 0;
  SelectVOP3ModsImpl(In, Src, Mods);

  if (Src.getOpcode() == ISD::FP_EXTEND) {
    Src = Src.getOperand(0);
    assert(Src.getValueType() == MVT::f16);
    Src = stripBitcast(Src);

    // Be careful about folding modifiers if we already have an abs. fneg is
    // applied last, so we don't want to apply an earlier fneg.
    if ((Mods & SISrcMods::ABS) == 0) {
      unsigned ModsTmp;
      SelectVOP3ModsImpl(Src, Src, ModsTmp);

      if ((ModsTmp & SISrcMods::NEG) != 0)
        Mods ^= SISrcMods::NEG;

      if ((ModsTmp & SISrcMods::ABS) != 0)
        Mods |= SISrcMods::ABS;
    }

    // op_sel/op_sel_hi decide the source type and source.
    // If the source's op_sel_hi is set, it indicates to do a conversion from fp16.
    // If the sources's op_sel is set, it picks the high half of the source
    // register.

    Mods |= SISrcMods::OP_SEL_1;
    if (isExtractHiElt(Src, Src)) {
      Mods |= SISrcMods::OP_SEL_0;

      // TODO: Should we try to look for neg/abs here?
    }

    return true;
  }

  return false;
}

bool AMDGPUDAGToDAGISel::SelectVOP3PMadMixModsExt(SDValue In, SDValue &Src,
                                                  SDValue &SrcMods) const {
  unsigned Mods = 0;
  if (!SelectVOP3PMadMixModsImpl(In, Src, Mods))
    return false;
  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

bool AMDGPUDAGToDAGISel::SelectVOP3PMadMixMods(SDValue In, SDValue &Src,
                                               SDValue &SrcMods) const {
  unsigned Mods = 0;
  SelectVOP3PMadMixModsImpl(In, Src, Mods);
  SrcMods = CurDAG->getTargetConstant(Mods, SDLoc(In), MVT::i32);
  return true;
}

SDValue AMDGPUDAGToDAGISel::getHi16Elt(SDValue In) const {
  if (In.isUndef())
    return CurDAG->getUNDEF(MVT::i32);

  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(In)) {
    SDLoc SL(In);
    return CurDAG->getConstant(C->getZExtValue() << 16, SL, MVT::i32);
  }

  if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(In)) {
    SDLoc SL(In);
    return CurDAG->getConstant(
      C->getValueAPF().bitcastToAPInt().getZExtValue() << 16, SL, MVT::i32);
  }

  SDValue Src;
  if (isExtractHiElt(In, Src))
    return Src;

  return SDValue();
}

bool AMDGPUDAGToDAGISel::isVGPRImm(const SDNode * N) const {
  assert(CurDAG->getTarget().getTargetTriple().getArch() == Triple::amdgcn);

  const SIRegisterInfo *SIRI =
    static_cast<const SIRegisterInfo *>(Subtarget->getRegisterInfo());
  const SIInstrInfo * SII =
    static_cast<const SIInstrInfo *>(Subtarget->getInstrInfo());

  unsigned Limit = 0;
  bool AllUsesAcceptSReg = true;
  for (SDNode::use_iterator U = N->use_begin(), E = SDNode::use_end();
    Limit < 10 && U != E; ++U, ++Limit) {
    const TargetRegisterClass *RC = getOperandRegClass(*U, U.getOperandNo());

    // If the register class is unknown, it could be an unknown
    // register class that needs to be an SGPR, e.g. an inline asm
    // constraint
    if (!RC || SIRI->isSGPRClass(RC))
      return false;

    if (RC != &AMDGPU::VS_32RegClass && RC != &AMDGPU::VS_64RegClass) {
      AllUsesAcceptSReg = false;
      SDNode * User = *U;
      if (User->isMachineOpcode()) {
        unsigned Opc = User->getMachineOpcode();
        const MCInstrDesc &Desc = SII->get(Opc);
        if (Desc.isCommutable()) {
          unsigned OpIdx = Desc.getNumDefs() + U.getOperandNo();
          unsigned CommuteIdx1 = TargetInstrInfo::CommuteAnyOperandIndex;
          if (SII->findCommutedOpIndices(Desc, OpIdx, CommuteIdx1)) {
            unsigned CommutedOpNo = CommuteIdx1 - Desc.getNumDefs();
            const TargetRegisterClass *CommutedRC = getOperandRegClass(*U, CommutedOpNo);
            if (CommutedRC == &AMDGPU::VS_32RegClass ||
                CommutedRC == &AMDGPU::VS_64RegClass)
              AllUsesAcceptSReg = true;
          }
        }
      }
      // If "AllUsesAcceptSReg == false" so far we haven't succeeded
      // commuting current user. This means have at least one use
      // that strictly require VGPR. Thus, we will not attempt to commute
      // other user instructions.
      if (!AllUsesAcceptSReg)
        break;
    }
  }
  return !AllUsesAcceptSReg && (Limit < 10);
}

bool AMDGPUDAGToDAGISel::isUniformLoad(const SDNode *N) const {
  auto Ld = cast<LoadSDNode>(N);

  const MachineMemOperand *MMO = Ld->getMemOperand();
  if (N->isDivergent() && !AMDGPUInstrInfo::isUniformMMO(MMO))
    return false;

  return MMO->getSize().hasValue() &&
         Ld->getAlign() >=
             Align(std::min(MMO->getSize().getValue().getKnownMinValue(),
                            uint64_t(4))) &&
         ((Ld->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS ||
           Ld->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS_32BIT) ||
          (Subtarget->getScalarizeGlobalBehavior() &&
           Ld->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS &&
           Ld->isSimple() &&
           static_cast<const SITargetLowering *>(getTargetLowering())
               ->isMemOpHasNoClobberedMemOperand(N)));
}

void AMDGPUDAGToDAGISel::PostprocessISelDAG() {
  const AMDGPUTargetLowering& Lowering =
    *static_cast<const AMDGPUTargetLowering*>(getTargetLowering());
  bool IsModified = false;
  do {
    IsModified = false;

    // Go over all selected nodes and try to fold them a bit more
    SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_begin();
    while (Position != CurDAG->allnodes_end()) {
      SDNode *Node = &*Position++;
      MachineSDNode *MachineNode = dyn_cast<MachineSDNode>(Node);
      if (!MachineNode)
        continue;

      SDNode *ResNode = Lowering.PostISelFolding(MachineNode, *CurDAG);
      if (ResNode != Node) {
        if (ResNode)
          ReplaceUses(Node, ResNode);
        IsModified = true;
      }
    }
    CurDAG->RemoveDeadNodes();
  } while (IsModified);
}

AMDGPUDAGToDAGISelLegacy::AMDGPUDAGToDAGISelLegacy(TargetMachine &TM,
                                                   CodeGenOptLevel OptLevel)
    : SelectionDAGISelLegacy(
          ID, std::make_unique<AMDGPUDAGToDAGISel>(TM, OptLevel)) {}

char AMDGPUDAGToDAGISelLegacy::ID = 0;
