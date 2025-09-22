//===-- PPCISelLowering.cpp - PPC DAG Lowering Implementation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the PPCISelLowering class.
//
//===----------------------------------------------------------------------===//

#include "PPCISelLowering.h"
#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCCCState.h"
#include "PPCCallingConv.h"
#include "PPCFrameLowering.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCPerfectShuffle.h"
#include "PPCRegisterInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsPowerPC.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCSymbolXCOFF.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <list>
#include <optional>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "ppc-lowering"

static cl::opt<bool> DisablePPCPreinc("disable-ppc-preinc",
cl::desc("disable preincrement load/store generation on PPC"), cl::Hidden);

static cl::opt<bool> DisableILPPref("disable-ppc-ilp-pref",
cl::desc("disable setting the node scheduling preference to ILP on PPC"), cl::Hidden);

static cl::opt<bool> DisablePPCUnaligned("disable-ppc-unaligned",
cl::desc("disable unaligned load/store generation on PPC"), cl::Hidden);

static cl::opt<bool> DisableSCO("disable-ppc-sco",
cl::desc("disable sibling call optimization on ppc"), cl::Hidden);

static cl::opt<bool> DisableInnermostLoopAlign32("disable-ppc-innermost-loop-align32",
cl::desc("don't always align innermost loop to 32 bytes on ppc"), cl::Hidden);

static cl::opt<bool> UseAbsoluteJumpTables("ppc-use-absolute-jumptables",
cl::desc("use absolute jump tables on ppc"), cl::Hidden);

static cl::opt<bool>
    DisablePerfectShuffle("ppc-disable-perfect-shuffle",
                          cl::desc("disable vector permute decomposition"),
                          cl::init(true), cl::Hidden);

cl::opt<bool> DisableAutoPairedVecSt(
    "disable-auto-paired-vec-st",
    cl::desc("disable automatically generated 32byte paired vector stores"),
    cl::init(true), cl::Hidden);

static cl::opt<unsigned> PPCMinimumJumpTableEntries(
    "ppc-min-jump-table-entries", cl::init(64), cl::Hidden,
    cl::desc("Set minimum number of entries to use a jump table on PPC"));

static cl::opt<unsigned> PPCGatherAllAliasesMaxDepth(
    "ppc-gather-alias-max-depth", cl::init(18), cl::Hidden,
    cl::desc("max depth when checking alias info in GatherAllAliases()"));

static cl::opt<unsigned> PPCAIXTLSModelOptUseIEForLDLimit(
    "ppc-aix-shared-lib-tls-model-opt-limit", cl::init(1), cl::Hidden,
    cl::desc("Set inclusive limit count of TLS local-dynamic access(es) in a "
             "function to use initial-exec"));

STATISTIC(NumTailCalls, "Number of tail calls");
STATISTIC(NumSiblingCalls, "Number of sibling calls");
STATISTIC(ShufflesHandledWithVPERM,
          "Number of shuffles lowered to a VPERM or XXPERM");
STATISTIC(NumDynamicAllocaProbed, "Number of dynamic stack allocation probed");

static bool isNByteElemShuffleMask(ShuffleVectorSDNode *, unsigned, int);

static SDValue widenVec(SelectionDAG &DAG, SDValue Vec, const SDLoc &dl);

static const char AIXSSPCanaryWordName[] = "__ssp_canary_word";

// A faster local-[exec|dynamic] TLS access sequence (enabled with the
// -maix-small-local-[exec|dynamic]-tls option) can be produced for TLS
// variables; consistent with the IBM XL compiler, we apply a max size of
// slightly under 32KB.
constexpr uint64_t AIXSmallTlsPolicySizeLimit = 32751;

// FIXME: Remove this once the bug has been fixed!
extern cl::opt<bool> ANDIGlueBug;

PPCTargetLowering::PPCTargetLowering(const PPCTargetMachine &TM,
                                     const PPCSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {
  // Initialize map that relates the PPC addressing modes to the computed flags
  // of a load/store instruction. The map is used to determine the optimal
  // addressing mode when selecting load and stores.
  initializeAddrModeMap();
  // On PPC32/64, arguments smaller than 4/8 bytes are extended, so all
  // arguments are at least 4/8 bytes aligned.
  bool isPPC64 = Subtarget.isPPC64();
  setMinStackArgumentAlignment(isPPC64 ? Align(8) : Align(4));

  // Set up the register classes.
  addRegisterClass(MVT::i32, &PPC::GPRCRegClass);
  if (!useSoftFloat()) {
    if (hasSPE()) {
      addRegisterClass(MVT::f32, &PPC::GPRCRegClass);
      // EFPU2 APU only supports f32
      if (!Subtarget.hasEFPU2())
        addRegisterClass(MVT::f64, &PPC::SPERCRegClass);
    } else {
      addRegisterClass(MVT::f32, &PPC::F4RCRegClass);
      addRegisterClass(MVT::f64, &PPC::F8RCRegClass);
    }
  }

  // Match BITREVERSE to customized fast code sequence in the td file.
  setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);
  setOperationAction(ISD::BITREVERSE, MVT::i64, Legal);

  // Sub-word ATOMIC_CMP_SWAP need to ensure that the input is zero-extended.
  setOperationAction(ISD::ATOMIC_CMP_SWAP, MVT::i32, Custom);

  // Custom lower inline assembly to check for special registers.
  setOperationAction(ISD::INLINEASM, MVT::Other, Custom);
  setOperationAction(ISD::INLINEASM_BR, MVT::Other, Custom);

  // PowerPC has an i16 but no i8 (or i1) SEXTLOAD.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Expand);
  }

  if (Subtarget.isISA3_0()) {
    setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Legal);
    setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Legal);
    setTruncStoreAction(MVT::f64, MVT::f16, Legal);
    setTruncStoreAction(MVT::f32, MVT::f16, Legal);
  } else {
    // No extending loads from f16 or HW conversions back and forth.
    setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Expand);
    setOperationAction(ISD::FP16_TO_FP, MVT::f64, Expand);
    setOperationAction(ISD::FP_TO_FP16, MVT::f64, Expand);
    setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Expand);
    setOperationAction(ISD::FP16_TO_FP, MVT::f32, Expand);
    setOperationAction(ISD::FP_TO_FP16, MVT::f32, Expand);
    setTruncStoreAction(MVT::f64, MVT::f16, Expand);
    setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  }

  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // PowerPC has pre-inc load and store's.
  setIndexedLoadAction(ISD::PRE_INC, MVT::i1, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i8, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i16, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i32, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i64, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i1, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i8, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i16, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i32, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i64, Legal);
  if (!Subtarget.hasSPE()) {
    setIndexedLoadAction(ISD::PRE_INC, MVT::f32, Legal);
    setIndexedLoadAction(ISD::PRE_INC, MVT::f64, Legal);
    setIndexedStoreAction(ISD::PRE_INC, MVT::f32, Legal);
    setIndexedStoreAction(ISD::PRE_INC, MVT::f64, Legal);
  }

  // PowerPC uses ADDC/ADDE/SUBC/SUBE to propagate carry.
  const MVT ScalarIntVTs[] = { MVT::i32, MVT::i64 };
  for (MVT VT : ScalarIntVTs) {
    setOperationAction(ISD::ADDC, VT, Legal);
    setOperationAction(ISD::ADDE, VT, Legal);
    setOperationAction(ISD::SUBC, VT, Legal);
    setOperationAction(ISD::SUBE, VT, Legal);
  }

  if (Subtarget.useCRBits()) {
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

    if (isPPC64 || Subtarget.hasFPCVT()) {
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i1, Promote);
      AddPromotedToType(ISD::STRICT_SINT_TO_FP, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::i1, Promote);
      AddPromotedToType(ISD::STRICT_UINT_TO_FP, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);

      setOperationAction(ISD::SINT_TO_FP, MVT::i1, Promote);
      AddPromotedToType (ISD::SINT_TO_FP, MVT::i1,
                         isPPC64 ? MVT::i64 : MVT::i32);
      setOperationAction(ISD::UINT_TO_FP, MVT::i1, Promote);
      AddPromotedToType(ISD::UINT_TO_FP, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);

      setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i1, Promote);
      AddPromotedToType(ISD::STRICT_FP_TO_SINT, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);
      setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i1, Promote);
      AddPromotedToType(ISD::STRICT_FP_TO_UINT, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);

      setOperationAction(ISD::FP_TO_SINT, MVT::i1, Promote);
      AddPromotedToType(ISD::FP_TO_SINT, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);
      setOperationAction(ISD::FP_TO_UINT, MVT::i1, Promote);
      AddPromotedToType(ISD::FP_TO_UINT, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);
    } else {
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i1, Custom);
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::i1, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::i1, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::i1, Custom);
    }

    // PowerPC does not support direct load/store of condition registers.
    setOperationAction(ISD::LOAD, MVT::i1, Custom);
    setOperationAction(ISD::STORE, MVT::i1, Custom);

    // FIXME: Remove this once the ANDI glue bug is fixed:
    if (ANDIGlueBug)
      setOperationAction(ISD::TRUNCATE, MVT::i1, Custom);

    for (MVT VT : MVT::integer_valuetypes()) {
      setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
      setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
      setTruncStoreAction(VT, MVT::i1, Expand);
    }

    addRegisterClass(MVT::i1, &PPC::CRBITRCRegClass);
  }

  // Expand ppcf128 to i32 by hand for the benefit of llvm-gcc bootstrap on
  // PPC (the libcall is not available).
  setOperationAction(ISD::FP_TO_SINT, MVT::ppcf128, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::ppcf128, Custom);
  setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::ppcf128, Custom);
  setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::ppcf128, Custom);

  // We do not currently implement these libm ops for PowerPC.
  setOperationAction(ISD::FFLOOR, MVT::ppcf128, Expand);
  setOperationAction(ISD::FCEIL,  MVT::ppcf128, Expand);
  setOperationAction(ISD::FTRUNC, MVT::ppcf128, Expand);
  setOperationAction(ISD::FRINT,  MVT::ppcf128, Expand);
  setOperationAction(ISD::FNEARBYINT, MVT::ppcf128, Expand);
  setOperationAction(ISD::FREM, MVT::ppcf128, Expand);

  // PowerPC has no SREM/UREM instructions unless we are on P9
  // On P9 we may use a hardware instruction to compute the remainder.
  // When the result of both the remainder and the division is required it is
  // more efficient to compute the remainder from the result of the division
  // rather than use the remainder instruction. The instructions are legalized
  // directly because the DivRemPairsPass performs the transformation at the IR
  // level.
  if (Subtarget.isISA3_0()) {
    setOperationAction(ISD::SREM, MVT::i32, Legal);
    setOperationAction(ISD::UREM, MVT::i32, Legal);
    setOperationAction(ISD::SREM, MVT::i64, Legal);
    setOperationAction(ISD::UREM, MVT::i64, Legal);
  } else {
    setOperationAction(ISD::SREM, MVT::i32, Expand);
    setOperationAction(ISD::UREM, MVT::i32, Expand);
    setOperationAction(ISD::SREM, MVT::i64, Expand);
    setOperationAction(ISD::UREM, MVT::i64, Expand);
  }

  // Don't use SMUL_LOHI/UMUL_LOHI or SDIVREM/UDIVREM to lower SREM/UREM.
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i64, Expand);

  // Handle constrained floating-point operations of scalar.
  // TODO: Handle SPE specific operation.
  setOperationAction(ISD::STRICT_FADD, MVT::f32, Legal);
  setOperationAction(ISD::STRICT_FSUB, MVT::f32, Legal);
  setOperationAction(ISD::STRICT_FMUL, MVT::f32, Legal);
  setOperationAction(ISD::STRICT_FDIV, MVT::f32, Legal);
  setOperationAction(ISD::STRICT_FP_ROUND, MVT::f32, Legal);

  setOperationAction(ISD::STRICT_FADD, MVT::f64, Legal);
  setOperationAction(ISD::STRICT_FSUB, MVT::f64, Legal);
  setOperationAction(ISD::STRICT_FMUL, MVT::f64, Legal);
  setOperationAction(ISD::STRICT_FDIV, MVT::f64, Legal);

  if (!Subtarget.hasSPE()) {
    setOperationAction(ISD::STRICT_FMA, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FMA, MVT::f64, Legal);
  }

  if (Subtarget.hasVSX()) {
    setOperationAction(ISD::STRICT_FRINT, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FRINT, MVT::f64, Legal);
  }

  if (Subtarget.hasFSQRT()) {
    setOperationAction(ISD::STRICT_FSQRT, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FSQRT, MVT::f64, Legal);
  }

  if (Subtarget.hasFPRND()) {
    setOperationAction(ISD::STRICT_FFLOOR, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FCEIL,  MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FTRUNC, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FROUND, MVT::f32, Legal);

    setOperationAction(ISD::STRICT_FFLOOR, MVT::f64, Legal);
    setOperationAction(ISD::STRICT_FCEIL,  MVT::f64, Legal);
    setOperationAction(ISD::STRICT_FTRUNC, MVT::f64, Legal);
    setOperationAction(ISD::STRICT_FROUND, MVT::f64, Legal);
  }

  // We don't support sin/cos/sqrt/fmod/pow
  setOperationAction(ISD::FSIN , MVT::f64, Expand);
  setOperationAction(ISD::FCOS , MVT::f64, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f64, Expand);
  setOperationAction(ISD::FREM , MVT::f64, Expand);
  setOperationAction(ISD::FPOW , MVT::f64, Expand);
  setOperationAction(ISD::FSIN , MVT::f32, Expand);
  setOperationAction(ISD::FCOS , MVT::f32, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f32, Expand);
  setOperationAction(ISD::FREM , MVT::f32, Expand);
  setOperationAction(ISD::FPOW , MVT::f32, Expand);

  // MASS transformation for LLVM intrinsics with replicating fast-math flag
  // to be consistent to PPCGenScalarMASSEntries pass
  if (TM.getOptLevel() == CodeGenOptLevel::Aggressive) {
    setOperationAction(ISD::FSIN , MVT::f64, Custom);
    setOperationAction(ISD::FCOS , MVT::f64, Custom);
    setOperationAction(ISD::FPOW , MVT::f64, Custom);
    setOperationAction(ISD::FLOG, MVT::f64, Custom);
    setOperationAction(ISD::FLOG10, MVT::f64, Custom);
    setOperationAction(ISD::FEXP, MVT::f64, Custom);
    setOperationAction(ISD::FSIN , MVT::f32, Custom);
    setOperationAction(ISD::FCOS , MVT::f32, Custom);
    setOperationAction(ISD::FPOW , MVT::f32, Custom);
    setOperationAction(ISD::FLOG, MVT::f32, Custom);
    setOperationAction(ISD::FLOG10, MVT::f32, Custom);
    setOperationAction(ISD::FEXP, MVT::f32, Custom);
  }

  if (Subtarget.hasSPE()) {
    setOperationAction(ISD::FMA  , MVT::f64, Expand);
    setOperationAction(ISD::FMA  , MVT::f32, Expand);
  } else {
    setOperationAction(ISD::FMA  , MVT::f64, Legal);
    setOperationAction(ISD::FMA  , MVT::f32, Legal);
  }

  if (Subtarget.hasSPE())
    setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);

  setOperationAction(ISD::GET_ROUNDING, MVT::i32, Custom);

  // If we're enabling GP optimizations, use hardware square root
  if (!Subtarget.hasFSQRT() &&
      !(TM.Options.UnsafeFPMath && Subtarget.hasFRSQRTE() &&
        Subtarget.hasFRE()))
    setOperationAction(ISD::FSQRT, MVT::f64, Expand);

  if (!Subtarget.hasFSQRT() &&
      !(TM.Options.UnsafeFPMath && Subtarget.hasFRSQRTES() &&
        Subtarget.hasFRES()))
    setOperationAction(ISD::FSQRT, MVT::f32, Expand);

  if (Subtarget.hasFCPSGN()) {
    setOperationAction(ISD::FCOPYSIGN, MVT::f64, Legal);
    setOperationAction(ISD::FCOPYSIGN, MVT::f32, Legal);
  } else {
    setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);
    setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  }

  if (Subtarget.hasFPRND()) {
    setOperationAction(ISD::FFLOOR, MVT::f64, Legal);
    setOperationAction(ISD::FCEIL,  MVT::f64, Legal);
    setOperationAction(ISD::FTRUNC, MVT::f64, Legal);
    setOperationAction(ISD::FROUND, MVT::f64, Legal);

    setOperationAction(ISD::FFLOOR, MVT::f32, Legal);
    setOperationAction(ISD::FCEIL,  MVT::f32, Legal);
    setOperationAction(ISD::FTRUNC, MVT::f32, Legal);
    setOperationAction(ISD::FROUND, MVT::f32, Legal);
  }

  // Prior to P10, PowerPC does not have BSWAP, but we can use vector BSWAP
  // instruction xxbrd to speed up scalar BSWAP64.
  if (Subtarget.isISA3_1()) {
    setOperationAction(ISD::BSWAP, MVT::i32, Legal);
    setOperationAction(ISD::BSWAP, MVT::i64, Legal);
  } else {
    setOperationAction(ISD::BSWAP, MVT::i32, Expand);
    setOperationAction(
        ISD::BSWAP, MVT::i64,
        (Subtarget.hasP9Vector() && Subtarget.isPPC64()) ? Custom : Expand);
  }

  // CTPOP or CTTZ were introduced in P8/P9 respectively
  if (Subtarget.isISA3_0()) {
    setOperationAction(ISD::CTTZ , MVT::i32  , Legal);
    setOperationAction(ISD::CTTZ , MVT::i64  , Legal);
  } else {
    setOperationAction(ISD::CTTZ , MVT::i32  , Expand);
    setOperationAction(ISD::CTTZ , MVT::i64  , Expand);
  }

  if (Subtarget.hasPOPCNTD() == PPCSubtarget::POPCNTD_Fast) {
    setOperationAction(ISD::CTPOP, MVT::i32  , Legal);
    setOperationAction(ISD::CTPOP, MVT::i64  , Legal);
  } else {
    setOperationAction(ISD::CTPOP, MVT::i32  , Expand);
    setOperationAction(ISD::CTPOP, MVT::i64  , Expand);
  }

  // PowerPC does not have ROTR
  setOperationAction(ISD::ROTR, MVT::i32   , Expand);
  setOperationAction(ISD::ROTR, MVT::i64   , Expand);

  if (!Subtarget.useCRBits()) {
    // PowerPC does not have Select
    setOperationAction(ISD::SELECT, MVT::i32, Expand);
    setOperationAction(ISD::SELECT, MVT::i64, Expand);
    setOperationAction(ISD::SELECT, MVT::f32, Expand);
    setOperationAction(ISD::SELECT, MVT::f64, Expand);
  }

  // PowerPC wants to turn select_cc of FP into fsel when possible.
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Custom);

  // PowerPC wants to optimize integer setcc a bit
  if (!Subtarget.useCRBits())
    setOperationAction(ISD::SETCC, MVT::i32, Custom);

  if (Subtarget.hasFPU()) {
    setOperationAction(ISD::STRICT_FSETCC, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FSETCC, MVT::f64, Legal);
    setOperationAction(ISD::STRICT_FSETCC, MVT::f128, Legal);

    setOperationAction(ISD::STRICT_FSETCCS, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FSETCCS, MVT::f64, Legal);
    setOperationAction(ISD::STRICT_FSETCCS, MVT::f128, Legal);
  }

  // PowerPC does not have BRCOND which requires SetCC
  if (!Subtarget.useCRBits())
    setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  setOperationAction(ISD::BR_JT,  MVT::Other, Expand);

  if (Subtarget.hasSPE()) {
    // SPE has built-in conversions
    setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i32, Legal);
    setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i32, Legal);
    setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::i32, Legal);
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Legal);
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Legal);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Legal);

    // SPE supports signaling compare of f32/f64.
    setOperationAction(ISD::STRICT_FSETCCS, MVT::f32, Legal);
    setOperationAction(ISD::STRICT_FSETCCS, MVT::f64, Legal);
  } else {
    // PowerPC turns FP_TO_SINT into FCTIWZ and some load/stores.
    setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);

    // PowerPC does not have [U|S]INT_TO_FP
    setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i32, Expand);
    setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::i32, Expand);
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Expand);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);
  }

  if (Subtarget.hasDirectMove() && isPPC64) {
    setOperationAction(ISD::BITCAST, MVT::f32, Legal);
    setOperationAction(ISD::BITCAST, MVT::i32, Legal);
    setOperationAction(ISD::BITCAST, MVT::i64, Legal);
    setOperationAction(ISD::BITCAST, MVT::f64, Legal);
    if (TM.Options.UnsafeFPMath) {
      setOperationAction(ISD::LRINT, MVT::f64, Legal);
      setOperationAction(ISD::LRINT, MVT::f32, Legal);
      setOperationAction(ISD::LLRINT, MVT::f64, Legal);
      setOperationAction(ISD::LLRINT, MVT::f32, Legal);
      setOperationAction(ISD::LROUND, MVT::f64, Legal);
      setOperationAction(ISD::LROUND, MVT::f32, Legal);
      setOperationAction(ISD::LLROUND, MVT::f64, Legal);
      setOperationAction(ISD::LLROUND, MVT::f32, Legal);
    }
  } else {
    setOperationAction(ISD::BITCAST, MVT::f32, Expand);
    setOperationAction(ISD::BITCAST, MVT::i32, Expand);
    setOperationAction(ISD::BITCAST, MVT::i64, Expand);
    setOperationAction(ISD::BITCAST, MVT::f64, Expand);
  }

  // We cannot sextinreg(i1).  Expand to shifts.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // NOTE: EH_SJLJ_SETJMP/_LONGJMP supported here is NOT intended to support
  // SjLj exception handling but a light-weight setjmp/longjmp replacement to
  // support continuation, user-level threading, and etc.. As a result, no
  // other SjLj exception interfaces are implemented and please don't build
  // your own exception handling based on them.
  // LLVM/Clang supports zero-cost DWARF exception handling.
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction(ISD::EH_SJLJ_LONGJMP, MVT::Other, Custom);

  // We want to legalize GlobalAddress and ConstantPool nodes into the
  // appropriate instructions to materialize the address.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress,  MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i32, Custom);
  setOperationAction(ISD::JumpTable,     MVT::i32, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::BlockAddress,  MVT::i64, Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i64, Custom);
  setOperationAction(ISD::JumpTable,     MVT::i64, Custom);

  // TRAP is legal.
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  // TRAMPOLINE is custom lowered.
  setOperationAction(ISD::INIT_TRAMPOLINE, MVT::Other, Custom);
  setOperationAction(ISD::ADJUST_TRAMPOLINE, MVT::Other, Custom);

  // VASTART needs to be custom lowered to use the VarArgsFrameIndex
  setOperationAction(ISD::VASTART           , MVT::Other, Custom);

  if (Subtarget.is64BitELFABI()) {
    // VAARG always uses double-word chunks, so promote anything smaller.
    setOperationAction(ISD::VAARG, MVT::i1, Promote);
    AddPromotedToType(ISD::VAARG, MVT::i1, MVT::i64);
    setOperationAction(ISD::VAARG, MVT::i8, Promote);
    AddPromotedToType(ISD::VAARG, MVT::i8, MVT::i64);
    setOperationAction(ISD::VAARG, MVT::i16, Promote);
    AddPromotedToType(ISD::VAARG, MVT::i16, MVT::i64);
    setOperationAction(ISD::VAARG, MVT::i32, Promote);
    AddPromotedToType(ISD::VAARG, MVT::i32, MVT::i64);
    setOperationAction(ISD::VAARG, MVT::Other, Expand);
  } else if (Subtarget.is32BitELFABI()) {
    // VAARG is custom lowered with the 32-bit SVR4 ABI.
    setOperationAction(ISD::VAARG, MVT::Other, Custom);
    setOperationAction(ISD::VAARG, MVT::i64, Custom);
  } else
    setOperationAction(ISD::VAARG, MVT::Other, Expand);

  // VACOPY is custom lowered with the 32-bit SVR4 ABI.
  if (Subtarget.is32BitELFABI())
    setOperationAction(ISD::VACOPY            , MVT::Other, Custom);
  else
    setOperationAction(ISD::VACOPY            , MVT::Other, Expand);

  // Use the default implementation.
  setOperationAction(ISD::VAEND             , MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE         , MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE      , MVT::Other, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32  , Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64  , Custom);
  setOperationAction(ISD::GET_DYNAMIC_AREA_OFFSET, MVT::i32, Custom);
  setOperationAction(ISD::GET_DYNAMIC_AREA_OFFSET, MVT::i64, Custom);
  setOperationAction(ISD::EH_DWARF_CFA, MVT::i32, Custom);
  setOperationAction(ISD::EH_DWARF_CFA, MVT::i64, Custom);

  // We want to custom lower some of our intrinsics.
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::f64, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::ppcf128, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::v4f32, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::v2f64, Custom);

  // To handle counter-based loop conditions.
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::i1, Custom);

  setOperationAction(ISD::INTRINSIC_VOID, MVT::i8, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::i16, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::i32, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::Other, Custom);

  // Comparisons that require checking two conditions.
  if (Subtarget.hasSPE()) {
    setCondCodeAction(ISD::SETO, MVT::f32, Expand);
    setCondCodeAction(ISD::SETO, MVT::f64, Expand);
    setCondCodeAction(ISD::SETUO, MVT::f32, Expand);
    setCondCodeAction(ISD::SETUO, MVT::f64, Expand);
  }
  setCondCodeAction(ISD::SETULT, MVT::f32, Expand);
  setCondCodeAction(ISD::SETULT, MVT::f64, Expand);
  setCondCodeAction(ISD::SETUGT, MVT::f32, Expand);
  setCondCodeAction(ISD::SETUGT, MVT::f64, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f32, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f64, Expand);
  setCondCodeAction(ISD::SETOGE, MVT::f32, Expand);
  setCondCodeAction(ISD::SETOGE, MVT::f64, Expand);
  setCondCodeAction(ISD::SETOLE, MVT::f32, Expand);
  setCondCodeAction(ISD::SETOLE, MVT::f64, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f32, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f64, Expand);

  setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f32, Legal);
  setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f64, Legal);

  if (Subtarget.has64BitSupport()) {
    // They also have instructions for converting between i64 and fp.
    setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i64, Custom);
    setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i64, Expand);
    setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i64, Custom);
    setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::i64, Expand);
    setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i64, Expand);
    setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::i64, Expand);
    // This is just the low 32 bits of a (signed) fp->i64 conversion.
    // We cannot do this with Promote because i64 is not a legal type.
    setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);

    if (Subtarget.hasLFIWAX() || Subtarget.isPPC64()) {
      setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i32, Custom);
    }
  } else {
    // PowerPC does not have FP_TO_UINT on 32-bit implementations.
    if (Subtarget.hasSPE()) {
      setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i32, Legal);
      setOperationAction(ISD::FP_TO_UINT, MVT::i32, Legal);
    } else {
      setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i32, Expand);
      setOperationAction(ISD::FP_TO_UINT, MVT::i32, Expand);
    }
  }

  // With the instructions enabled under FPCVT, we can do everything.
  if (Subtarget.hasFPCVT()) {
    if (Subtarget.has64BitSupport()) {
      setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i64, Custom);
      setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i64, Custom);
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i64, Custom);
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::i64, Custom);
      setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
      setOperationAction(ISD::FP_TO_UINT, MVT::i64, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);
    }

    setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
  }

  if (Subtarget.use64BitRegs()) {
    // 64-bit PowerPC implementations can support i64 types directly
    addRegisterClass(MVT::i64, &PPC::G8RCRegClass);
    // BUILD_PAIR can't be handled natively, and should be expanded to shl/or
    setOperationAction(ISD::BUILD_PAIR, MVT::i64, Expand);
    // 64-bit PowerPC wants to expand i128 shifts itself.
    setOperationAction(ISD::SHL_PARTS, MVT::i64, Custom);
    setOperationAction(ISD::SRA_PARTS, MVT::i64, Custom);
    setOperationAction(ISD::SRL_PARTS, MVT::i64, Custom);
  } else {
    // 32-bit PowerPC wants to expand i64 shifts itself.
    setOperationAction(ISD::SHL_PARTS, MVT::i32, Custom);
    setOperationAction(ISD::SRA_PARTS, MVT::i32, Custom);
    setOperationAction(ISD::SRL_PARTS, MVT::i32, Custom);
  }

  // PowerPC has better expansions for funnel shifts than the generic
  // TargetLowering::expandFunnelShift.
  if (Subtarget.has64BitSupport()) {
    setOperationAction(ISD::FSHL, MVT::i64, Custom);
    setOperationAction(ISD::FSHR, MVT::i64, Custom);
  }
  setOperationAction(ISD::FSHL, MVT::i32, Custom);
  setOperationAction(ISD::FSHR, MVT::i32, Custom);

  if (Subtarget.hasVSX()) {
    setOperationAction(ISD::FMAXNUM_IEEE, MVT::f64, Legal);
    setOperationAction(ISD::FMAXNUM_IEEE, MVT::f32, Legal);
    setOperationAction(ISD::FMINNUM_IEEE, MVT::f64, Legal);
    setOperationAction(ISD::FMINNUM_IEEE, MVT::f32, Legal);
  }

  if (Subtarget.hasAltivec()) {
    for (MVT VT : { MVT::v16i8, MVT::v8i16, MVT::v4i32 }) {
      setOperationAction(ISD::SADDSAT, VT, Legal);
      setOperationAction(ISD::SSUBSAT, VT, Legal);
      setOperationAction(ISD::UADDSAT, VT, Legal);
      setOperationAction(ISD::USUBSAT, VT, Legal);
    }
    // First set operation action for all vector types to expand. Then we
    // will selectively turn on ones that can be effectively codegen'd.
    for (MVT VT : MVT::fixedlen_vector_valuetypes()) {
      // add/sub are legal for all supported vector VT's.
      setOperationAction(ISD::ADD, VT, Legal);
      setOperationAction(ISD::SUB, VT, Legal);

      // For v2i64, these are only valid with P8Vector. This is corrected after
      // the loop.
      if (VT.getSizeInBits() <= 128 && VT.getScalarSizeInBits() <= 64) {
        setOperationAction(ISD::SMAX, VT, Legal);
        setOperationAction(ISD::SMIN, VT, Legal);
        setOperationAction(ISD::UMAX, VT, Legal);
        setOperationAction(ISD::UMIN, VT, Legal);
      }
      else {
        setOperationAction(ISD::SMAX, VT, Expand);
        setOperationAction(ISD::SMIN, VT, Expand);
        setOperationAction(ISD::UMAX, VT, Expand);
        setOperationAction(ISD::UMIN, VT, Expand);
      }

      if (Subtarget.hasVSX()) {
        setOperationAction(ISD::FMAXNUM, VT, Legal);
        setOperationAction(ISD::FMINNUM, VT, Legal);
      }

      // Vector instructions introduced in P8
      if (Subtarget.hasP8Altivec() && (VT.SimpleTy != MVT::v1i128)) {
        setOperationAction(ISD::CTPOP, VT, Legal);
        setOperationAction(ISD::CTLZ, VT, Legal);
      }
      else {
        setOperationAction(ISD::CTPOP, VT, Expand);
        setOperationAction(ISD::CTLZ, VT, Expand);
      }

      // Vector instructions introduced in P9
      if (Subtarget.hasP9Altivec() && (VT.SimpleTy != MVT::v1i128))
        setOperationAction(ISD::CTTZ, VT, Legal);
      else
        setOperationAction(ISD::CTTZ, VT, Expand);

      // We promote all shuffles to v16i8.
      setOperationAction(ISD::VECTOR_SHUFFLE, VT, Promote);
      AddPromotedToType (ISD::VECTOR_SHUFFLE, VT, MVT::v16i8);

      // We promote all non-typed operations to v4i32.
      setOperationAction(ISD::AND   , VT, Promote);
      AddPromotedToType (ISD::AND   , VT, MVT::v4i32);
      setOperationAction(ISD::OR    , VT, Promote);
      AddPromotedToType (ISD::OR    , VT, MVT::v4i32);
      setOperationAction(ISD::XOR   , VT, Promote);
      AddPromotedToType (ISD::XOR   , VT, MVT::v4i32);
      setOperationAction(ISD::LOAD  , VT, Promote);
      AddPromotedToType (ISD::LOAD  , VT, MVT::v4i32);
      setOperationAction(ISD::SELECT, VT, Promote);
      AddPromotedToType (ISD::SELECT, VT, MVT::v4i32);
      setOperationAction(ISD::VSELECT, VT, Legal);
      setOperationAction(ISD::SELECT_CC, VT, Promote);
      AddPromotedToType (ISD::SELECT_CC, VT, MVT::v4i32);
      setOperationAction(ISD::STORE, VT, Promote);
      AddPromotedToType (ISD::STORE, VT, MVT::v4i32);

      // No other operations are legal.
      setOperationAction(ISD::MUL , VT, Expand);
      setOperationAction(ISD::SDIV, VT, Expand);
      setOperationAction(ISD::SREM, VT, Expand);
      setOperationAction(ISD::UDIV, VT, Expand);
      setOperationAction(ISD::UREM, VT, Expand);
      setOperationAction(ISD::FDIV, VT, Expand);
      setOperationAction(ISD::FREM, VT, Expand);
      setOperationAction(ISD::FNEG, VT, Expand);
      setOperationAction(ISD::FSQRT, VT, Expand);
      setOperationAction(ISD::FLOG, VT, Expand);
      setOperationAction(ISD::FLOG10, VT, Expand);
      setOperationAction(ISD::FLOG2, VT, Expand);
      setOperationAction(ISD::FEXP, VT, Expand);
      setOperationAction(ISD::FEXP2, VT, Expand);
      setOperationAction(ISD::FSIN, VT, Expand);
      setOperationAction(ISD::FCOS, VT, Expand);
      setOperationAction(ISD::FABS, VT, Expand);
      setOperationAction(ISD::FFLOOR, VT, Expand);
      setOperationAction(ISD::FCEIL,  VT, Expand);
      setOperationAction(ISD::FTRUNC, VT, Expand);
      setOperationAction(ISD::FRINT,  VT, Expand);
      setOperationAction(ISD::FLDEXP, VT, Expand);
      setOperationAction(ISD::FNEARBYINT, VT, Expand);
      setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Expand);
      setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Expand);
      setOperationAction(ISD::BUILD_VECTOR, VT, Expand);
      setOperationAction(ISD::MULHU, VT, Expand);
      setOperationAction(ISD::MULHS, VT, Expand);
      setOperationAction(ISD::UMUL_LOHI, VT, Expand);
      setOperationAction(ISD::SMUL_LOHI, VT, Expand);
      setOperationAction(ISD::UDIVREM, VT, Expand);
      setOperationAction(ISD::SDIVREM, VT, Expand);
      setOperationAction(ISD::SCALAR_TO_VECTOR, VT, Expand);
      setOperationAction(ISD::FPOW, VT, Expand);
      setOperationAction(ISD::BSWAP, VT, Expand);
      setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);
      setOperationAction(ISD::ROTL, VT, Expand);
      setOperationAction(ISD::ROTR, VT, Expand);

      for (MVT InnerVT : MVT::fixedlen_vector_valuetypes()) {
        setTruncStoreAction(VT, InnerVT, Expand);
        setLoadExtAction(ISD::SEXTLOAD, VT, InnerVT, Expand);
        setLoadExtAction(ISD::ZEXTLOAD, VT, InnerVT, Expand);
        setLoadExtAction(ISD::EXTLOAD, VT, InnerVT, Expand);
      }
    }
    setOperationAction(ISD::SELECT_CC, MVT::v4i32, Expand);
    if (!Subtarget.hasP8Vector()) {
      setOperationAction(ISD::SMAX, MVT::v2i64, Expand);
      setOperationAction(ISD::SMIN, MVT::v2i64, Expand);
      setOperationAction(ISD::UMAX, MVT::v2i64, Expand);
      setOperationAction(ISD::UMIN, MVT::v2i64, Expand);
    }

    // We can custom expand all VECTOR_SHUFFLEs to VPERM, others we can handle
    // with merges, splats, etc.
    setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v16i8, Custom);

    // Vector truncates to sub-word integer that fit in an Altivec/VSX register
    // are cheap, so handle them before they get expanded to scalar.
    setOperationAction(ISD::TRUNCATE, MVT::v8i8, Custom);
    setOperationAction(ISD::TRUNCATE, MVT::v4i8, Custom);
    setOperationAction(ISD::TRUNCATE, MVT::v2i8, Custom);
    setOperationAction(ISD::TRUNCATE, MVT::v4i16, Custom);
    setOperationAction(ISD::TRUNCATE, MVT::v2i16, Custom);

    setOperationAction(ISD::AND   , MVT::v4i32, Legal);
    setOperationAction(ISD::OR    , MVT::v4i32, Legal);
    setOperationAction(ISD::XOR   , MVT::v4i32, Legal);
    setOperationAction(ISD::LOAD  , MVT::v4i32, Legal);
    setOperationAction(ISD::SELECT, MVT::v4i32,
                       Subtarget.useCRBits() ? Legal : Expand);
    setOperationAction(ISD::STORE , MVT::v4i32, Legal);
    setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::v4i32, Legal);
    setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::v4i32, Legal);
    setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::v4i32, Legal);
    setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::v4i32, Legal);
    setOperationAction(ISD::FP_TO_SINT, MVT::v4i32, Legal);
    setOperationAction(ISD::FP_TO_UINT, MVT::v4i32, Legal);
    setOperationAction(ISD::SINT_TO_FP, MVT::v4i32, Legal);
    setOperationAction(ISD::UINT_TO_FP, MVT::v4i32, Legal);
    setOperationAction(ISD::FFLOOR, MVT::v4f32, Legal);
    setOperationAction(ISD::FCEIL, MVT::v4f32, Legal);
    setOperationAction(ISD::FTRUNC, MVT::v4f32, Legal);
    setOperationAction(ISD::FNEARBYINT, MVT::v4f32, Legal);

    // Custom lowering ROTL v1i128 to VECTOR_SHUFFLE v16i8.
    setOperationAction(ISD::ROTL, MVT::v1i128, Custom);
    // With hasAltivec set, we can lower ISD::ROTL to vrl(b|h|w).
    if (Subtarget.hasAltivec())
      for (auto VT : {MVT::v4i32, MVT::v8i16, MVT::v16i8})
        setOperationAction(ISD::ROTL, VT, Legal);
    // With hasP8Altivec set, we can lower ISD::ROTL to vrld.
    if (Subtarget.hasP8Altivec())
      setOperationAction(ISD::ROTL, MVT::v2i64, Legal);

    addRegisterClass(MVT::v4f32, &PPC::VRRCRegClass);
    addRegisterClass(MVT::v4i32, &PPC::VRRCRegClass);
    addRegisterClass(MVT::v8i16, &PPC::VRRCRegClass);
    addRegisterClass(MVT::v16i8, &PPC::VRRCRegClass);

    setOperationAction(ISD::MUL, MVT::v4f32, Legal);
    setOperationAction(ISD::FMA, MVT::v4f32, Legal);

    if (Subtarget.hasVSX()) {
      setOperationAction(ISD::FDIV, MVT::v4f32, Legal);
      setOperationAction(ISD::FSQRT, MVT::v4f32, Legal);
      setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v2f64, Custom);
    }

    if (Subtarget.hasP8Altivec())
      setOperationAction(ISD::MUL, MVT::v4i32, Legal);
    else
      setOperationAction(ISD::MUL, MVT::v4i32, Custom);

    if (Subtarget.isISA3_1()) {
      setOperationAction(ISD::MUL, MVT::v2i64, Legal);
      setOperationAction(ISD::MULHS, MVT::v2i64, Legal);
      setOperationAction(ISD::MULHU, MVT::v2i64, Legal);
      setOperationAction(ISD::MULHS, MVT::v4i32, Legal);
      setOperationAction(ISD::MULHU, MVT::v4i32, Legal);
      setOperationAction(ISD::UDIV, MVT::v2i64, Legal);
      setOperationAction(ISD::SDIV, MVT::v2i64, Legal);
      setOperationAction(ISD::UDIV, MVT::v4i32, Legal);
      setOperationAction(ISD::SDIV, MVT::v4i32, Legal);
      setOperationAction(ISD::UREM, MVT::v2i64, Legal);
      setOperationAction(ISD::SREM, MVT::v2i64, Legal);
      setOperationAction(ISD::UREM, MVT::v4i32, Legal);
      setOperationAction(ISD::SREM, MVT::v4i32, Legal);
      setOperationAction(ISD::UREM, MVT::v1i128, Legal);
      setOperationAction(ISD::SREM, MVT::v1i128, Legal);
      setOperationAction(ISD::UDIV, MVT::v1i128, Legal);
      setOperationAction(ISD::SDIV, MVT::v1i128, Legal);
      setOperationAction(ISD::ROTL, MVT::v1i128, Legal);
    }

    setOperationAction(ISD::MUL, MVT::v8i16, Legal);
    setOperationAction(ISD::MUL, MVT::v16i8, Custom);

    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4f32, Custom);
    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4i32, Custom);

    setOperationAction(ISD::BUILD_VECTOR, MVT::v16i8, Custom);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v8i16, Custom);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v4i32, Custom);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v4f32, Custom);

    // Altivec does not contain unordered floating-point compare instructions
    setCondCodeAction(ISD::SETUO, MVT::v4f32, Expand);
    setCondCodeAction(ISD::SETUEQ, MVT::v4f32, Expand);
    setCondCodeAction(ISD::SETO,   MVT::v4f32, Expand);
    setCondCodeAction(ISD::SETONE, MVT::v4f32, Expand);

    if (Subtarget.hasVSX()) {
      setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v2f64, Legal);
      setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2f64, Legal);
      if (Subtarget.hasP8Vector()) {
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4f32, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v4f32, Legal);
      }
      if (Subtarget.hasDirectMove() && isPPC64) {
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v16i8, Legal);
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v8i16, Legal);
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4i32, Legal);
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v2i64, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v16i8, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v8i16, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v4i32, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2i64, Legal);
      }
      setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2f64, Legal);

      // The nearbyint variants are not allowed to raise the inexact exception
      // so we can only code-gen them with unsafe math.
      if (TM.Options.UnsafeFPMath) {
        setOperationAction(ISD::FNEARBYINT, MVT::f64, Legal);
        setOperationAction(ISD::FNEARBYINT, MVT::f32, Legal);
      }

      setOperationAction(ISD::FFLOOR, MVT::v2f64, Legal);
      setOperationAction(ISD::FCEIL, MVT::v2f64, Legal);
      setOperationAction(ISD::FTRUNC, MVT::v2f64, Legal);
      setOperationAction(ISD::FNEARBYINT, MVT::v2f64, Legal);
      setOperationAction(ISD::FRINT, MVT::v2f64, Legal);
      setOperationAction(ISD::FROUND, MVT::v2f64, Legal);
      setOperationAction(ISD::FROUND, MVT::f64, Legal);
      setOperationAction(ISD::FRINT, MVT::f64, Legal);

      setOperationAction(ISD::FNEARBYINT, MVT::v4f32, Legal);
      setOperationAction(ISD::FRINT, MVT::v4f32, Legal);
      setOperationAction(ISD::FROUND, MVT::v4f32, Legal);
      setOperationAction(ISD::FROUND, MVT::f32, Legal);
      setOperationAction(ISD::FRINT, MVT::f32, Legal);

      setOperationAction(ISD::MUL, MVT::v2f64, Legal);
      setOperationAction(ISD::FMA, MVT::v2f64, Legal);

      setOperationAction(ISD::FDIV, MVT::v2f64, Legal);
      setOperationAction(ISD::FSQRT, MVT::v2f64, Legal);

      // Share the Altivec comparison restrictions.
      setCondCodeAction(ISD::SETUO, MVT::v2f64, Expand);
      setCondCodeAction(ISD::SETUEQ, MVT::v2f64, Expand);
      setCondCodeAction(ISD::SETO,   MVT::v2f64, Expand);
      setCondCodeAction(ISD::SETONE, MVT::v2f64, Expand);

      setOperationAction(ISD::LOAD, MVT::v2f64, Legal);
      setOperationAction(ISD::STORE, MVT::v2f64, Legal);

      setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2f64, Custom);

      if (Subtarget.hasP8Vector())
        addRegisterClass(MVT::f32, &PPC::VSSRCRegClass);

      addRegisterClass(MVT::f64, &PPC::VSFRCRegClass);

      addRegisterClass(MVT::v4i32, &PPC::VSRCRegClass);
      addRegisterClass(MVT::v4f32, &PPC::VSRCRegClass);
      addRegisterClass(MVT::v2f64, &PPC::VSRCRegClass);

      if (Subtarget.hasP8Altivec()) {
        setOperationAction(ISD::SHL, MVT::v2i64, Legal);
        setOperationAction(ISD::SRA, MVT::v2i64, Legal);
        setOperationAction(ISD::SRL, MVT::v2i64, Legal);

        // 128 bit shifts can be accomplished via 3 instructions for SHL and
        // SRL, but not for SRA because of the instructions available:
        // VS{RL} and VS{RL}O. However due to direct move costs, it's not worth
        // doing
        setOperationAction(ISD::SHL, MVT::v1i128, Expand);
        setOperationAction(ISD::SRL, MVT::v1i128, Expand);
        setOperationAction(ISD::SRA, MVT::v1i128, Expand);

        setOperationAction(ISD::SETCC, MVT::v2i64, Legal);
      }
      else {
        setOperationAction(ISD::SHL, MVT::v2i64, Expand);
        setOperationAction(ISD::SRA, MVT::v2i64, Expand);
        setOperationAction(ISD::SRL, MVT::v2i64, Expand);

        setOperationAction(ISD::SETCC, MVT::v2i64, Custom);

        // VSX v2i64 only supports non-arithmetic operations.
        setOperationAction(ISD::ADD, MVT::v2i64, Expand);
        setOperationAction(ISD::SUB, MVT::v2i64, Expand);
      }

      if (Subtarget.isISA3_1())
        setOperationAction(ISD::SETCC, MVT::v1i128, Legal);
      else
        setOperationAction(ISD::SETCC, MVT::v1i128, Expand);

      setOperationAction(ISD::LOAD, MVT::v2i64, Promote);
      AddPromotedToType (ISD::LOAD, MVT::v2i64, MVT::v2f64);
      setOperationAction(ISD::STORE, MVT::v2i64, Promote);
      AddPromotedToType (ISD::STORE, MVT::v2i64, MVT::v2f64);

      setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2i64, Custom);

      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::v2i64, Legal);
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::v2i64, Legal);
      setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::v2i64, Legal);
      setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::v2i64, Legal);
      setOperationAction(ISD::SINT_TO_FP, MVT::v2i64, Legal);
      setOperationAction(ISD::UINT_TO_FP, MVT::v2i64, Legal);
      setOperationAction(ISD::FP_TO_SINT, MVT::v2i64, Legal);
      setOperationAction(ISD::FP_TO_UINT, MVT::v2i64, Legal);

      // Custom handling for partial vectors of integers converted to
      // floating point. We already have optimal handling for v2i32 through
      // the DAG combine, so those aren't necessary.
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::v2i8, Custom);
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::v4i8, Custom);
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::v2i16, Custom);
      setOperationAction(ISD::STRICT_UINT_TO_FP, MVT::v4i16, Custom);
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::v2i8, Custom);
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::v4i8, Custom);
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::v2i16, Custom);
      setOperationAction(ISD::STRICT_SINT_TO_FP, MVT::v4i16, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::v2i8, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::v4i8, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::v2i16, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::v4i16, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v2i8, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v4i8, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v2i16, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v4i16, Custom);

      setOperationAction(ISD::FNEG, MVT::v4f32, Legal);
      setOperationAction(ISD::FNEG, MVT::v2f64, Legal);
      setOperationAction(ISD::FABS, MVT::v4f32, Legal);
      setOperationAction(ISD::FABS, MVT::v2f64, Legal);
      setOperationAction(ISD::FCOPYSIGN, MVT::v4f32, Legal);
      setOperationAction(ISD::FCOPYSIGN, MVT::v2f64, Legal);

      setOperationAction(ISD::BUILD_VECTOR, MVT::v2i64, Custom);
      setOperationAction(ISD::BUILD_VECTOR, MVT::v2f64, Custom);

      // Handle constrained floating-point operations of vector.
      // The predictor is `hasVSX` because altivec instruction has
      // no exception but VSX vector instruction has.
      setOperationAction(ISD::STRICT_FADD, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FSUB, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FMUL, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FDIV, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FMA, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FSQRT, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FMAXNUM, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FMINNUM, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FRINT, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FFLOOR, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FCEIL,  MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FTRUNC, MVT::v4f32, Legal);
      setOperationAction(ISD::STRICT_FROUND, MVT::v4f32, Legal);

      setOperationAction(ISD::STRICT_FADD, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FSUB, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FMUL, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FDIV, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FMA, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FSQRT, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FMAXNUM, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FMINNUM, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FRINT, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FFLOOR, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FCEIL,  MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FTRUNC, MVT::v2f64, Legal);
      setOperationAction(ISD::STRICT_FROUND, MVT::v2f64, Legal);

      addRegisterClass(MVT::v2i64, &PPC::VSRCRegClass);
      addRegisterClass(MVT::f128, &PPC::VRRCRegClass);

      for (MVT FPT : MVT::fp_valuetypes())
        setLoadExtAction(ISD::EXTLOAD, MVT::f128, FPT, Expand);

      // Expand the SELECT to SELECT_CC
      setOperationAction(ISD::SELECT, MVT::f128, Expand);

      setTruncStoreAction(MVT::f128, MVT::f64, Expand);
      setTruncStoreAction(MVT::f128, MVT::f32, Expand);

      // No implementation for these ops for PowerPC.
      setOperationAction(ISD::FSINCOS, MVT::f128, Expand);
      setOperationAction(ISD::FSIN, MVT::f128, Expand);
      setOperationAction(ISD::FCOS, MVT::f128, Expand);
      setOperationAction(ISD::FPOW, MVT::f128, Expand);
      setOperationAction(ISD::FPOWI, MVT::f128, Expand);
      setOperationAction(ISD::FREM, MVT::f128, Expand);
    }

    if (Subtarget.hasP8Altivec()) {
      addRegisterClass(MVT::v2i64, &PPC::VRRCRegClass);
      addRegisterClass(MVT::v1i128, &PPC::VRRCRegClass);
    }

    if (Subtarget.hasP9Vector()) {
      setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v4i32, Custom);
      setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v4f32, Custom);

      // Test data class instructions store results in CR bits.
      if (Subtarget.useCRBits()) {
        setOperationAction(ISD::IS_FPCLASS, MVT::f32, Custom);
        setOperationAction(ISD::IS_FPCLASS, MVT::f64, Custom);
        setOperationAction(ISD::IS_FPCLASS, MVT::f128, Custom);
      }

      // 128 bit shifts can be accomplished via 3 instructions for SHL and
      // SRL, but not for SRA because of the instructions available:
      // VS{RL} and VS{RL}O.
      setOperationAction(ISD::SHL, MVT::v1i128, Legal);
      setOperationAction(ISD::SRL, MVT::v1i128, Legal);
      setOperationAction(ISD::SRA, MVT::v1i128, Expand);

      setOperationAction(ISD::FADD, MVT::f128, Legal);
      setOperationAction(ISD::FSUB, MVT::f128, Legal);
      setOperationAction(ISD::FDIV, MVT::f128, Legal);
      setOperationAction(ISD::FMUL, MVT::f128, Legal);
      setOperationAction(ISD::FP_EXTEND, MVT::f128, Legal);

      setOperationAction(ISD::FMA, MVT::f128, Legal);
      setCondCodeAction(ISD::SETULT, MVT::f128, Expand);
      setCondCodeAction(ISD::SETUGT, MVT::f128, Expand);
      setCondCodeAction(ISD::SETUEQ, MVT::f128, Expand);
      setCondCodeAction(ISD::SETOGE, MVT::f128, Expand);
      setCondCodeAction(ISD::SETOLE, MVT::f128, Expand);
      setCondCodeAction(ISD::SETONE, MVT::f128, Expand);

      setOperationAction(ISD::FTRUNC, MVT::f128, Legal);
      setOperationAction(ISD::FRINT, MVT::f128, Legal);
      setOperationAction(ISD::FFLOOR, MVT::f128, Legal);
      setOperationAction(ISD::FCEIL, MVT::f128, Legal);
      setOperationAction(ISD::FNEARBYINT, MVT::f128, Legal);
      setOperationAction(ISD::FROUND, MVT::f128, Legal);

      setOperationAction(ISD::FP_ROUND, MVT::f64, Legal);
      setOperationAction(ISD::FP_ROUND, MVT::f32, Legal);
      setOperationAction(ISD::BITCAST, MVT::i128, Custom);

      // Handle constrained floating-point operations of fp128
      setOperationAction(ISD::STRICT_FADD, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FSUB, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FMUL, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FDIV, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FMA, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FSQRT, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FP_ROUND, MVT::f64, Legal);
      setOperationAction(ISD::STRICT_FP_ROUND, MVT::f32, Legal);
      setOperationAction(ISD::STRICT_FRINT, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FNEARBYINT, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FFLOOR, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FCEIL, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FTRUNC, MVT::f128, Legal);
      setOperationAction(ISD::STRICT_FROUND, MVT::f128, Legal);
      setOperationAction(ISD::FP_EXTEND, MVT::v2f32, Custom);
      setOperationAction(ISD::BSWAP, MVT::v8i16, Legal);
      setOperationAction(ISD::BSWAP, MVT::v4i32, Legal);
      setOperationAction(ISD::BSWAP, MVT::v2i64, Legal);
      setOperationAction(ISD::BSWAP, MVT::v1i128, Legal);
    } else if (Subtarget.hasVSX()) {
      setOperationAction(ISD::LOAD, MVT::f128, Promote);
      setOperationAction(ISD::STORE, MVT::f128, Promote);

      AddPromotedToType(ISD::LOAD, MVT::f128, MVT::v4i32);
      AddPromotedToType(ISD::STORE, MVT::f128, MVT::v4i32);

      // Set FADD/FSUB as libcall to avoid the legalizer to expand the
      // fp_to_uint and int_to_fp.
      setOperationAction(ISD::FADD, MVT::f128, LibCall);
      setOperationAction(ISD::FSUB, MVT::f128, LibCall);

      setOperationAction(ISD::FMUL, MVT::f128, Expand);
      setOperationAction(ISD::FDIV, MVT::f128, Expand);
      setOperationAction(ISD::FNEG, MVT::f128, Expand);
      setOperationAction(ISD::FABS, MVT::f128, Expand);
      setOperationAction(ISD::FSQRT, MVT::f128, Expand);
      setOperationAction(ISD::FMA, MVT::f128, Expand);
      setOperationAction(ISD::FCOPYSIGN, MVT::f128, Expand);

      // Expand the fp_extend if the target type is fp128.
      setOperationAction(ISD::FP_EXTEND, MVT::f128, Expand);
      setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f128, Expand);

      // Expand the fp_round if the source type is fp128.
      for (MVT VT : {MVT::f32, MVT::f64}) {
        setOperationAction(ISD::FP_ROUND, VT, Custom);
        setOperationAction(ISD::STRICT_FP_ROUND, VT, Custom);
      }

      setOperationAction(ISD::SETCC, MVT::f128, Custom);
      setOperationAction(ISD::STRICT_FSETCC, MVT::f128, Custom);
      setOperationAction(ISD::STRICT_FSETCCS, MVT::f128, Custom);
      setOperationAction(ISD::BR_CC, MVT::f128, Expand);

      // Lower following f128 select_cc pattern:
      // select_cc x, y, tv, fv, cc -> select_cc (setcc x, y, cc), 0, tv, fv, NE
      setOperationAction(ISD::SELECT_CC, MVT::f128, Custom);

      // We need to handle f128 SELECT_CC with integer result type.
      setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
      setOperationAction(ISD::SELECT_CC, MVT::i64, isPPC64 ? Custom : Expand);
    }

    if (Subtarget.hasP9Altivec()) {
      if (Subtarget.isISA3_1()) {
        setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v2i64, Legal);
        setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v8i16, Legal);
        setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v16i8, Legal);
        setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v4i32, Legal);
      } else {
        setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v8i16, Custom);
        setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v16i8, Custom);
      }
      setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v4i8,  Legal);
      setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v4i16, Legal);
      setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v4i32, Legal);
      setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v2i8,  Legal);
      setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v2i16, Legal);
      setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v2i32, Legal);
      setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v2i64, Legal);

      setOperationAction(ISD::ABDU, MVT::v16i8, Legal);
      setOperationAction(ISD::ABDU, MVT::v8i16, Legal);
      setOperationAction(ISD::ABDU, MVT::v4i32, Legal);
      setOperationAction(ISD::ABDS, MVT::v4i32, Legal);
    }

    if (Subtarget.hasP10Vector()) {
      setOperationAction(ISD::SELECT_CC, MVT::f128, Custom);
    }
  }

  if (Subtarget.pairedVectorMemops()) {
    addRegisterClass(MVT::v256i1, &PPC::VSRpRCRegClass);
    setOperationAction(ISD::LOAD, MVT::v256i1, Custom);
    setOperationAction(ISD::STORE, MVT::v256i1, Custom);
  }
  if (Subtarget.hasMMA()) {
    if (Subtarget.isISAFuture())
      addRegisterClass(MVT::v512i1, &PPC::WACCRCRegClass);
    else
      addRegisterClass(MVT::v512i1, &PPC::UACCRCRegClass);
    setOperationAction(ISD::LOAD, MVT::v512i1, Custom);
    setOperationAction(ISD::STORE, MVT::v512i1, Custom);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v512i1, Custom);
  }

  if (Subtarget.has64BitSupport())
    setOperationAction(ISD::PREFETCH, MVT::Other, Legal);

  if (Subtarget.isISA3_1())
    setOperationAction(ISD::SRA, MVT::v1i128, Legal);

  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, isPPC64 ? Legal : Custom);

  if (!isPPC64) {
    setOperationAction(ISD::ATOMIC_LOAD,  MVT::i64, Expand);
    setOperationAction(ISD::ATOMIC_STORE, MVT::i64, Expand);
  }

  if (shouldInlineQuadwordAtomics()) {
    setOperationAction(ISD::ATOMIC_LOAD, MVT::i128, Custom);
    setOperationAction(ISD::ATOMIC_STORE, MVT::i128, Custom);
    setOperationAction(ISD::INTRINSIC_VOID, MVT::i128, Custom);
  }

  setBooleanContents(ZeroOrOneBooleanContent);

  if (Subtarget.hasAltivec()) {
    // Altivec instructions set fields to all zeros or all ones.
    setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);
  }

  if (shouldInlineQuadwordAtomics())
    setMaxAtomicSizeInBitsSupported(128);
  else if (isPPC64)
    setMaxAtomicSizeInBitsSupported(64);
  else
    setMaxAtomicSizeInBitsSupported(32);

  setStackPointerRegisterToSaveRestore(isPPC64 ? PPC::X1 : PPC::R1);

  // We have target-specific dag combine patterns for the following nodes:
  setTargetDAGCombine({ISD::AND, ISD::ADD, ISD::SHL, ISD::SRA, ISD::SRL,
                       ISD::MUL, ISD::FMA, ISD::SINT_TO_FP, ISD::BUILD_VECTOR});
  if (Subtarget.hasFPCVT())
    setTargetDAGCombine(ISD::UINT_TO_FP);
  setTargetDAGCombine({ISD::LOAD, ISD::STORE, ISD::BR_CC});
  if (Subtarget.useCRBits())
    setTargetDAGCombine(ISD::BRCOND);
  setTargetDAGCombine({ISD::BSWAP, ISD::INTRINSIC_WO_CHAIN,
                       ISD::INTRINSIC_W_CHAIN, ISD::INTRINSIC_VOID});

  setTargetDAGCombine({ISD::SIGN_EXTEND, ISD::ZERO_EXTEND, ISD::ANY_EXTEND});

  setTargetDAGCombine({ISD::TRUNCATE, ISD::VECTOR_SHUFFLE});

  if (Subtarget.useCRBits()) {
    setTargetDAGCombine({ISD::TRUNCATE, ISD::SETCC, ISD::SELECT_CC});
  }

  setLibcallName(RTLIB::LOG_F128, "logf128");
  setLibcallName(RTLIB::LOG2_F128, "log2f128");
  setLibcallName(RTLIB::LOG10_F128, "log10f128");
  setLibcallName(RTLIB::EXP_F128, "expf128");
  setLibcallName(RTLIB::EXP2_F128, "exp2f128");
  setLibcallName(RTLIB::SIN_F128, "sinf128");
  setLibcallName(RTLIB::COS_F128, "cosf128");
  setLibcallName(RTLIB::SINCOS_F128, "sincosf128");
  setLibcallName(RTLIB::POW_F128, "powf128");
  setLibcallName(RTLIB::FMIN_F128, "fminf128");
  setLibcallName(RTLIB::FMAX_F128, "fmaxf128");
  setLibcallName(RTLIB::REM_F128, "fmodf128");
  setLibcallName(RTLIB::SQRT_F128, "sqrtf128");
  setLibcallName(RTLIB::CEIL_F128, "ceilf128");
  setLibcallName(RTLIB::FLOOR_F128, "floorf128");
  setLibcallName(RTLIB::TRUNC_F128, "truncf128");
  setLibcallName(RTLIB::ROUND_F128, "roundf128");
  setLibcallName(RTLIB::LROUND_F128, "lroundf128");
  setLibcallName(RTLIB::LLROUND_F128, "llroundf128");
  setLibcallName(RTLIB::RINT_F128, "rintf128");
  setLibcallName(RTLIB::LRINT_F128, "lrintf128");
  setLibcallName(RTLIB::LLRINT_F128, "llrintf128");
  setLibcallName(RTLIB::NEARBYINT_F128, "nearbyintf128");
  setLibcallName(RTLIB::FMA_F128, "fmaf128");
  setLibcallName(RTLIB::FREXP_F128, "frexpf128");

  if (Subtarget.isAIXABI()) {
    setLibcallName(RTLIB::MEMCPY, isPPC64 ? "___memmove64" : "___memmove");
    setLibcallName(RTLIB::MEMMOVE, isPPC64 ? "___memmove64" : "___memmove");
    setLibcallName(RTLIB::MEMSET, isPPC64 ? "___memset64" : "___memset");
    setLibcallName(RTLIB::BZERO, isPPC64 ? "___bzero64" : "___bzero");
  }

  // With 32 condition bits, we don't need to sink (and duplicate) compares
  // aggressively in CodeGenPrep.
  if (Subtarget.useCRBits()) {
    setHasMultipleConditionRegisters();
    setJumpIsExpensive();
  }

  // TODO: The default entry number is set to 64. This stops most jump table
  // generation on PPC. But it is good for current PPC HWs because the indirect
  // branch instruction mtctr to the jump table may lead to bad branch predict.
  // Re-evaluate this value on future HWs that can do better with mtctr.
  setMinimumJumpTableEntries(PPCMinimumJumpTableEntries);

  setMinFunctionAlignment(Align(4));

  switch (Subtarget.getCPUDirective()) {
  default: break;
  case PPC::DIR_970:
  case PPC::DIR_A2:
  case PPC::DIR_E500:
  case PPC::DIR_E500mc:
  case PPC::DIR_E5500:
  case PPC::DIR_PWR4:
  case PPC::DIR_PWR5:
  case PPC::DIR_PWR5X:
  case PPC::DIR_PWR6:
  case PPC::DIR_PWR6X:
  case PPC::DIR_PWR7:
  case PPC::DIR_PWR8:
  case PPC::DIR_PWR9:
  case PPC::DIR_PWR10:
  case PPC::DIR_PWR11:
  case PPC::DIR_PWR_FUTURE:
    setPrefLoopAlignment(Align(16));
    setPrefFunctionAlignment(Align(16));
    break;
  }

  if (Subtarget.enableMachineScheduler())
    setSchedulingPreference(Sched::Source);
  else
    setSchedulingPreference(Sched::Hybrid);

  computeRegisterProperties(STI.getRegisterInfo());

  // The Freescale cores do better with aggressive inlining of memcpy and
  // friends. GCC uses same threshold of 128 bytes (= 32 word stores).
  if (Subtarget.getCPUDirective() == PPC::DIR_E500mc ||
      Subtarget.getCPUDirective() == PPC::DIR_E5500) {
    MaxStoresPerMemset = 32;
    MaxStoresPerMemsetOptSize = 16;
    MaxStoresPerMemcpy = 32;
    MaxStoresPerMemcpyOptSize = 8;
    MaxStoresPerMemmove = 32;
    MaxStoresPerMemmoveOptSize = 8;
  } else if (Subtarget.getCPUDirective() == PPC::DIR_A2) {
    // The A2 also benefits from (very) aggressive inlining of memcpy and
    // friends. The overhead of a the function call, even when warm, can be
    // over one hundred cycles.
    MaxStoresPerMemset = 128;
    MaxStoresPerMemcpy = 128;
    MaxStoresPerMemmove = 128;
    MaxLoadsPerMemcmp = 128;
  } else {
    MaxLoadsPerMemcmp = 8;
    MaxLoadsPerMemcmpOptSize = 4;
  }

  IsStrictFPEnabled = true;

  // Let the subtarget (CPU) decide if a predictable select is more expensive
  // than the corresponding branch. This information is used in CGP to decide
  // when to convert selects into branches.
  PredictableSelectIsExpensive = Subtarget.isPredictableSelectIsExpensive();

  GatherAllAliasesMaxDepth = PPCGatherAllAliasesMaxDepth;
}

// *********************************** NOTE ************************************
// For selecting load and store instructions, the addressing modes are defined
// as ComplexPatterns in PPCInstrInfo.td, which are then utilized in the TD
// patterns to match the load the store instructions.
//
// The TD definitions for the addressing modes correspond to their respective
// Select<AddrMode>Form() function in PPCISelDAGToDAG.cpp. These functions rely
// on SelectOptimalAddrMode(), which calls computeMOFlags() to compute the
// address mode flags of a particular node. Afterwards, the computed address
// flags are passed into getAddrModeForFlags() in order to retrieve the optimal
// addressing mode. SelectOptimalAddrMode() then sets the Base and Displacement
// accordingly, based on the preferred addressing mode.
//
// Within PPCISelLowering.h, there are two enums: MemOpFlags and AddrMode.
// MemOpFlags contains all the possible flags that can be used to compute the
// optimal addressing mode for load and store instructions.
// AddrMode contains all the possible load and store addressing modes available
// on Power (such as DForm, DSForm, DQForm, XForm, etc.)
//
// When adding new load and store instructions, it is possible that new address
// flags may need to be added into MemOpFlags, and a new addressing mode will
// need to be added to AddrMode. An entry of the new addressing mode (consisting
// of the minimal and main distinguishing address flags for the new load/store
// instructions) will need to be added into initializeAddrModeMap() below.
// Finally, when adding new addressing modes, the getAddrModeForFlags() will
// need to be updated to account for selecting the optimal addressing mode.
// *****************************************************************************
/// Initialize the map that relates the different addressing modes of the load
/// and store instructions to a set of flags. This ensures the load/store
/// instruction is correctly matched during instruction selection.
void PPCTargetLowering::initializeAddrModeMap() {
  AddrModesMap[PPC::AM_DForm] = {
      // LWZ, STW
      PPC::MOF_ZExt | PPC::MOF_RPlusSImm16 | PPC::MOF_WordInt,
      PPC::MOF_ZExt | PPC::MOF_RPlusLo | PPC::MOF_WordInt,
      PPC::MOF_ZExt | PPC::MOF_NotAddNorCst | PPC::MOF_WordInt,
      PPC::MOF_ZExt | PPC::MOF_AddrIsSImm32 | PPC::MOF_WordInt,
      // LBZ, LHZ, STB, STH
      PPC::MOF_ZExt | PPC::MOF_RPlusSImm16 | PPC::MOF_SubWordInt,
      PPC::MOF_ZExt | PPC::MOF_RPlusLo | PPC::MOF_SubWordInt,
      PPC::MOF_ZExt | PPC::MOF_NotAddNorCst | PPC::MOF_SubWordInt,
      PPC::MOF_ZExt | PPC::MOF_AddrIsSImm32 | PPC::MOF_SubWordInt,
      // LHA
      PPC::MOF_SExt | PPC::MOF_RPlusSImm16 | PPC::MOF_SubWordInt,
      PPC::MOF_SExt | PPC::MOF_RPlusLo | PPC::MOF_SubWordInt,
      PPC::MOF_SExt | PPC::MOF_NotAddNorCst | PPC::MOF_SubWordInt,
      PPC::MOF_SExt | PPC::MOF_AddrIsSImm32 | PPC::MOF_SubWordInt,
      // LFS, LFD, STFS, STFD
      PPC::MOF_RPlusSImm16 | PPC::MOF_ScalarFloat | PPC::MOF_SubtargetBeforeP9,
      PPC::MOF_RPlusLo | PPC::MOF_ScalarFloat | PPC::MOF_SubtargetBeforeP9,
      PPC::MOF_NotAddNorCst | PPC::MOF_ScalarFloat | PPC::MOF_SubtargetBeforeP9,
      PPC::MOF_AddrIsSImm32 | PPC::MOF_ScalarFloat | PPC::MOF_SubtargetBeforeP9,
  };
  AddrModesMap[PPC::AM_DSForm] = {
      // LWA
      PPC::MOF_SExt | PPC::MOF_RPlusSImm16Mult4 | PPC::MOF_WordInt,
      PPC::MOF_SExt | PPC::MOF_NotAddNorCst | PPC::MOF_WordInt,
      PPC::MOF_SExt | PPC::MOF_AddrIsSImm32 | PPC::MOF_WordInt,
      // LD, STD
      PPC::MOF_RPlusSImm16Mult4 | PPC::MOF_DoubleWordInt,
      PPC::MOF_NotAddNorCst | PPC::MOF_DoubleWordInt,
      PPC::MOF_AddrIsSImm32 | PPC::MOF_DoubleWordInt,
      // DFLOADf32, DFLOADf64, DSTOREf32, DSTOREf64
      PPC::MOF_RPlusSImm16Mult4 | PPC::MOF_ScalarFloat | PPC::MOF_SubtargetP9,
      PPC::MOF_NotAddNorCst | PPC::MOF_ScalarFloat | PPC::MOF_SubtargetP9,
      PPC::MOF_AddrIsSImm32 | PPC::MOF_ScalarFloat | PPC::MOF_SubtargetP9,
  };
  AddrModesMap[PPC::AM_DQForm] = {
      // LXV, STXV
      PPC::MOF_RPlusSImm16Mult16 | PPC::MOF_Vector | PPC::MOF_SubtargetP9,
      PPC::MOF_NotAddNorCst | PPC::MOF_Vector | PPC::MOF_SubtargetP9,
      PPC::MOF_AddrIsSImm32 | PPC::MOF_Vector | PPC::MOF_SubtargetP9,
  };
  AddrModesMap[PPC::AM_PrefixDForm] = {PPC::MOF_RPlusSImm34 |
                                       PPC::MOF_SubtargetP10};
  // TODO: Add mapping for quadword load/store.
}

/// getMaxByValAlign - Helper for getByValTypeAlignment to determine
/// the desired ByVal argument alignment.
static void getMaxByValAlign(Type *Ty, Align &MaxAlign, Align MaxMaxAlign) {
  if (MaxAlign == MaxMaxAlign)
    return;
  if (VectorType *VTy = dyn_cast<VectorType>(Ty)) {
    if (MaxMaxAlign >= 32 &&
        VTy->getPrimitiveSizeInBits().getFixedValue() >= 256)
      MaxAlign = Align(32);
    else if (VTy->getPrimitiveSizeInBits().getFixedValue() >= 128 &&
             MaxAlign < 16)
      MaxAlign = Align(16);
  } else if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    Align EltAlign;
    getMaxByValAlign(ATy->getElementType(), EltAlign, MaxMaxAlign);
    if (EltAlign > MaxAlign)
      MaxAlign = EltAlign;
  } else if (StructType *STy = dyn_cast<StructType>(Ty)) {
    for (auto *EltTy : STy->elements()) {
      Align EltAlign;
      getMaxByValAlign(EltTy, EltAlign, MaxMaxAlign);
      if (EltAlign > MaxAlign)
        MaxAlign = EltAlign;
      if (MaxAlign == MaxMaxAlign)
        break;
    }
  }
}

/// getByValTypeAlignment - Return the desired alignment for ByVal aggregate
/// function arguments in the caller parameter area.
uint64_t PPCTargetLowering::getByValTypeAlignment(Type *Ty,
                                                  const DataLayout &DL) const {
  // 16byte and wider vectors are passed on 16byte boundary.
  // The rest is 8 on PPC64 and 4 on PPC32 boundary.
  Align Alignment = Subtarget.isPPC64() ? Align(8) : Align(4);
  if (Subtarget.hasAltivec())
    getMaxByValAlign(Ty, Alignment, Align(16));
  return Alignment.value();
}

bool PPCTargetLowering::useSoftFloat() const {
  return Subtarget.useSoftFloat();
}

bool PPCTargetLowering::hasSPE() const {
  return Subtarget.hasSPE();
}

bool PPCTargetLowering::preferIncOfAddToSubOfNot(EVT VT) const {
  return VT.isScalarInteger();
}

bool PPCTargetLowering::shallExtractConstSplatVectorElementToStore(
    Type *VectorTy, unsigned ElemSizeInBits, unsigned &Index) const {
  if (!Subtarget.isPPC64() || !Subtarget.hasVSX())
    return false;

  if (auto *VTy = dyn_cast<VectorType>(VectorTy)) {
    if (VTy->getScalarType()->isIntegerTy()) {
      // ElemSizeInBits 8/16 can fit in immediate field, not needed here.
      if (ElemSizeInBits == 32) {
        Index = Subtarget.isLittleEndian() ? 2 : 1;
        return true;
      }
      if (ElemSizeInBits == 64) {
        Index = Subtarget.isLittleEndian() ? 1 : 0;
        return true;
      }
    }
  }
  return false;
}

const char *PPCTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((PPCISD::NodeType)Opcode) {
  case PPCISD::FIRST_NUMBER:    break;
  case PPCISD::FSEL:            return "PPCISD::FSEL";
  case PPCISD::XSMAXC:          return "PPCISD::XSMAXC";
  case PPCISD::XSMINC:          return "PPCISD::XSMINC";
  case PPCISD::FCFID:           return "PPCISD::FCFID";
  case PPCISD::FCFIDU:          return "PPCISD::FCFIDU";
  case PPCISD::FCFIDS:          return "PPCISD::FCFIDS";
  case PPCISD::FCFIDUS:         return "PPCISD::FCFIDUS";
  case PPCISD::FCTIDZ:          return "PPCISD::FCTIDZ";
  case PPCISD::FCTIWZ:          return "PPCISD::FCTIWZ";
  case PPCISD::FCTIDUZ:         return "PPCISD::FCTIDUZ";
  case PPCISD::FCTIWUZ:         return "PPCISD::FCTIWUZ";
  case PPCISD::FRE:             return "PPCISD::FRE";
  case PPCISD::FRSQRTE:         return "PPCISD::FRSQRTE";
  case PPCISD::FTSQRT:
    return "PPCISD::FTSQRT";
  case PPCISD::FSQRT:
    return "PPCISD::FSQRT";
  case PPCISD::STFIWX:          return "PPCISD::STFIWX";
  case PPCISD::VPERM:           return "PPCISD::VPERM";
  case PPCISD::XXSPLT:          return "PPCISD::XXSPLT";
  case PPCISD::XXSPLTI_SP_TO_DP:
    return "PPCISD::XXSPLTI_SP_TO_DP";
  case PPCISD::XXSPLTI32DX:
    return "PPCISD::XXSPLTI32DX";
  case PPCISD::VECINSERT:       return "PPCISD::VECINSERT";
  case PPCISD::XXPERMDI:        return "PPCISD::XXPERMDI";
  case PPCISD::XXPERM:
    return "PPCISD::XXPERM";
  case PPCISD::VECSHL:          return "PPCISD::VECSHL";
  case PPCISD::CMPB:            return "PPCISD::CMPB";
  case PPCISD::Hi:              return "PPCISD::Hi";
  case PPCISD::Lo:              return "PPCISD::Lo";
  case PPCISD::TOC_ENTRY:       return "PPCISD::TOC_ENTRY";
  case PPCISD::ATOMIC_CMP_SWAP_8: return "PPCISD::ATOMIC_CMP_SWAP_8";
  case PPCISD::ATOMIC_CMP_SWAP_16: return "PPCISD::ATOMIC_CMP_SWAP_16";
  case PPCISD::DYNALLOC:        return "PPCISD::DYNALLOC";
  case PPCISD::DYNAREAOFFSET:   return "PPCISD::DYNAREAOFFSET";
  case PPCISD::PROBED_ALLOCA:   return "PPCISD::PROBED_ALLOCA";
  case PPCISD::GlobalBaseReg:   return "PPCISD::GlobalBaseReg";
  case PPCISD::SRL:             return "PPCISD::SRL";
  case PPCISD::SRA:             return "PPCISD::SRA";
  case PPCISD::SHL:             return "PPCISD::SHL";
  case PPCISD::SRA_ADDZE:       return "PPCISD::SRA_ADDZE";
  case PPCISD::CALL:            return "PPCISD::CALL";
  case PPCISD::CALL_NOP:        return "PPCISD::CALL_NOP";
  case PPCISD::CALL_NOTOC:      return "PPCISD::CALL_NOTOC";
  case PPCISD::CALL_RM:
    return "PPCISD::CALL_RM";
  case PPCISD::CALL_NOP_RM:
    return "PPCISD::CALL_NOP_RM";
  case PPCISD::CALL_NOTOC_RM:
    return "PPCISD::CALL_NOTOC_RM";
  case PPCISD::MTCTR:           return "PPCISD::MTCTR";
  case PPCISD::BCTRL:           return "PPCISD::BCTRL";
  case PPCISD::BCTRL_LOAD_TOC:  return "PPCISD::BCTRL_LOAD_TOC";
  case PPCISD::BCTRL_RM:
    return "PPCISD::BCTRL_RM";
  case PPCISD::BCTRL_LOAD_TOC_RM:
    return "PPCISD::BCTRL_LOAD_TOC_RM";
  case PPCISD::RET_GLUE:        return "PPCISD::RET_GLUE";
  case PPCISD::READ_TIME_BASE:  return "PPCISD::READ_TIME_BASE";
  case PPCISD::EH_SJLJ_SETJMP:  return "PPCISD::EH_SJLJ_SETJMP";
  case PPCISD::EH_SJLJ_LONGJMP: return "PPCISD::EH_SJLJ_LONGJMP";
  case PPCISD::MFOCRF:          return "PPCISD::MFOCRF";
  case PPCISD::MFVSR:           return "PPCISD::MFVSR";
  case PPCISD::MTVSRA:          return "PPCISD::MTVSRA";
  case PPCISD::MTVSRZ:          return "PPCISD::MTVSRZ";
  case PPCISD::SINT_VEC_TO_FP:  return "PPCISD::SINT_VEC_TO_FP";
  case PPCISD::UINT_VEC_TO_FP:  return "PPCISD::UINT_VEC_TO_FP";
  case PPCISD::SCALAR_TO_VECTOR_PERMUTED:
    return "PPCISD::SCALAR_TO_VECTOR_PERMUTED";
  case PPCISD::ANDI_rec_1_EQ_BIT:
    return "PPCISD::ANDI_rec_1_EQ_BIT";
  case PPCISD::ANDI_rec_1_GT_BIT:
    return "PPCISD::ANDI_rec_1_GT_BIT";
  case PPCISD::VCMP:            return "PPCISD::VCMP";
  case PPCISD::VCMP_rec:        return "PPCISD::VCMP_rec";
  case PPCISD::LBRX:            return "PPCISD::LBRX";
  case PPCISD::STBRX:           return "PPCISD::STBRX";
  case PPCISD::LFIWAX:          return "PPCISD::LFIWAX";
  case PPCISD::LFIWZX:          return "PPCISD::LFIWZX";
  case PPCISD::LXSIZX:          return "PPCISD::LXSIZX";
  case PPCISD::STXSIX:          return "PPCISD::STXSIX";
  case PPCISD::VEXTS:           return "PPCISD::VEXTS";
  case PPCISD::LXVD2X:          return "PPCISD::LXVD2X";
  case PPCISD::STXVD2X:         return "PPCISD::STXVD2X";
  case PPCISD::LOAD_VEC_BE:     return "PPCISD::LOAD_VEC_BE";
  case PPCISD::STORE_VEC_BE:    return "PPCISD::STORE_VEC_BE";
  case PPCISD::ST_VSR_SCAL_INT:
                                return "PPCISD::ST_VSR_SCAL_INT";
  case PPCISD::COND_BRANCH:     return "PPCISD::COND_BRANCH";
  case PPCISD::BDNZ:            return "PPCISD::BDNZ";
  case PPCISD::BDZ:             return "PPCISD::BDZ";
  case PPCISD::MFFS:            return "PPCISD::MFFS";
  case PPCISD::FADDRTZ:         return "PPCISD::FADDRTZ";
  case PPCISD::TC_RETURN:       return "PPCISD::TC_RETURN";
  case PPCISD::CR6SET:          return "PPCISD::CR6SET";
  case PPCISD::CR6UNSET:        return "PPCISD::CR6UNSET";
  case PPCISD::PPC32_GOT:       return "PPCISD::PPC32_GOT";
  case PPCISD::PPC32_PICGOT:    return "PPCISD::PPC32_PICGOT";
  case PPCISD::ADDIS_GOT_TPREL_HA: return "PPCISD::ADDIS_GOT_TPREL_HA";
  case PPCISD::LD_GOT_TPREL_L:  return "PPCISD::LD_GOT_TPREL_L";
  case PPCISD::ADD_TLS:         return "PPCISD::ADD_TLS";
  case PPCISD::ADDIS_TLSGD_HA:  return "PPCISD::ADDIS_TLSGD_HA";
  case PPCISD::ADDI_TLSGD_L:    return "PPCISD::ADDI_TLSGD_L";
  case PPCISD::GET_TLS_ADDR:    return "PPCISD::GET_TLS_ADDR";
  case PPCISD::GET_TLS_MOD_AIX: return "PPCISD::GET_TLS_MOD_AIX";
  case PPCISD::GET_TPOINTER:    return "PPCISD::GET_TPOINTER";
  case PPCISD::ADDI_TLSGD_L_ADDR: return "PPCISD::ADDI_TLSGD_L_ADDR";
  case PPCISD::TLSGD_AIX:       return "PPCISD::TLSGD_AIX";
  case PPCISD::TLSLD_AIX:       return "PPCISD::TLSLD_AIX";
  case PPCISD::ADDIS_TLSLD_HA:  return "PPCISD::ADDIS_TLSLD_HA";
  case PPCISD::ADDI_TLSLD_L:    return "PPCISD::ADDI_TLSLD_L";
  case PPCISD::GET_TLSLD_ADDR:  return "PPCISD::GET_TLSLD_ADDR";
  case PPCISD::ADDI_TLSLD_L_ADDR: return "PPCISD::ADDI_TLSLD_L_ADDR";
  case PPCISD::ADDIS_DTPREL_HA: return "PPCISD::ADDIS_DTPREL_HA";
  case PPCISD::ADDI_DTPREL_L:   return "PPCISD::ADDI_DTPREL_L";
  case PPCISD::PADDI_DTPREL:
    return "PPCISD::PADDI_DTPREL";
  case PPCISD::VADD_SPLAT:      return "PPCISD::VADD_SPLAT";
  case PPCISD::SC:              return "PPCISD::SC";
  case PPCISD::CLRBHRB:         return "PPCISD::CLRBHRB";
  case PPCISD::MFBHRBE:         return "PPCISD::MFBHRBE";
  case PPCISD::RFEBB:           return "PPCISD::RFEBB";
  case PPCISD::XXSWAPD:         return "PPCISD::XXSWAPD";
  case PPCISD::SWAP_NO_CHAIN:   return "PPCISD::SWAP_NO_CHAIN";
  case PPCISD::BUILD_FP128:     return "PPCISD::BUILD_FP128";
  case PPCISD::BUILD_SPE64:     return "PPCISD::BUILD_SPE64";
  case PPCISD::EXTRACT_SPE:     return "PPCISD::EXTRACT_SPE";
  case PPCISD::EXTSWSLI:        return "PPCISD::EXTSWSLI";
  case PPCISD::LD_VSX_LH:       return "PPCISD::LD_VSX_LH";
  case PPCISD::FP_EXTEND_HALF:  return "PPCISD::FP_EXTEND_HALF";
  case PPCISD::MAT_PCREL_ADDR:  return "PPCISD::MAT_PCREL_ADDR";
  case PPCISD::TLS_DYNAMIC_MAT_PCREL_ADDR:
    return "PPCISD::TLS_DYNAMIC_MAT_PCREL_ADDR";
  case PPCISD::TLS_LOCAL_EXEC_MAT_ADDR:
    return "PPCISD::TLS_LOCAL_EXEC_MAT_ADDR";
  case PPCISD::ACC_BUILD:       return "PPCISD::ACC_BUILD";
  case PPCISD::PAIR_BUILD:      return "PPCISD::PAIR_BUILD";
  case PPCISD::EXTRACT_VSX_REG: return "PPCISD::EXTRACT_VSX_REG";
  case PPCISD::XXMFACC:         return "PPCISD::XXMFACC";
  case PPCISD::LD_SPLAT:        return "PPCISD::LD_SPLAT";
  case PPCISD::ZEXT_LD_SPLAT:   return "PPCISD::ZEXT_LD_SPLAT";
  case PPCISD::SEXT_LD_SPLAT:   return "PPCISD::SEXT_LD_SPLAT";
  case PPCISD::FNMSUB:          return "PPCISD::FNMSUB";
  case PPCISD::STRICT_FADDRTZ:
    return "PPCISD::STRICT_FADDRTZ";
  case PPCISD::STRICT_FCTIDZ:
    return "PPCISD::STRICT_FCTIDZ";
  case PPCISD::STRICT_FCTIWZ:
    return "PPCISD::STRICT_FCTIWZ";
  case PPCISD::STRICT_FCTIDUZ:
    return "PPCISD::STRICT_FCTIDUZ";
  case PPCISD::STRICT_FCTIWUZ:
    return "PPCISD::STRICT_FCTIWUZ";
  case PPCISD::STRICT_FCFID:
    return "PPCISD::STRICT_FCFID";
  case PPCISD::STRICT_FCFIDU:
    return "PPCISD::STRICT_FCFIDU";
  case PPCISD::STRICT_FCFIDS:
    return "PPCISD::STRICT_FCFIDS";
  case PPCISD::STRICT_FCFIDUS:
    return "PPCISD::STRICT_FCFIDUS";
  case PPCISD::LXVRZX:          return "PPCISD::LXVRZX";
  case PPCISD::STORE_COND:
    return "PPCISD::STORE_COND";
  }
  return nullptr;
}

EVT PPCTargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &C,
                                          EVT VT) const {
  if (!VT.isVector())
    return Subtarget.useCRBits() ? MVT::i1 : MVT::i32;

  return VT.changeVectorElementTypeToInteger();
}

bool PPCTargetLowering::enableAggressiveFMAFusion(EVT VT) const {
  assert(VT.isFloatingPoint() && "Non-floating-point FMA?");
  return true;
}

//===----------------------------------------------------------------------===//
// Node matching predicates, for use by the tblgen matching code.
//===----------------------------------------------------------------------===//

/// isFloatingPointZero - Return true if this is 0.0 or -0.0.
static bool isFloatingPointZero(SDValue Op) {
  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(Op))
    return CFP->getValueAPF().isZero();
  else if (ISD::isEXTLoad(Op.getNode()) || ISD::isNON_EXTLoad(Op.getNode())) {
    // Maybe this has already been legalized into the constant pool?
    if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(Op.getOperand(1)))
      if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CP->getConstVal()))
        return CFP->getValueAPF().isZero();
  }
  return false;
}

/// isConstantOrUndef - Op is either an undef node or a ConstantSDNode.  Return
/// true if Op is undef or if it matches the specified value.
static bool isConstantOrUndef(int Op, int Val) {
  return Op < 0 || Op == Val;
}

/// isVPKUHUMShuffleMask - Return true if this is the shuffle mask for a
/// VPKUHUM instruction.
/// The ShuffleKind distinguishes between big-endian operations with
/// two different inputs (0), either-endian operations with two identical
/// inputs (1), and little-endian operations with two different inputs (2).
/// For the latter, the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVPKUHUMShuffleMask(ShuffleVectorSDNode *N, unsigned ShuffleKind,
                               SelectionDAG &DAG) {
  bool IsLE = DAG.getDataLayout().isLittleEndian();
  if (ShuffleKind == 0) {
    if (IsLE)
      return false;
    for (unsigned i = 0; i != 16; ++i)
      if (!isConstantOrUndef(N->getMaskElt(i), i*2+1))
        return false;
  } else if (ShuffleKind == 2) {
    if (!IsLE)
      return false;
    for (unsigned i = 0; i != 16; ++i)
      if (!isConstantOrUndef(N->getMaskElt(i), i*2))
        return false;
  } else if (ShuffleKind == 1) {
    unsigned j = IsLE ? 0 : 1;
    for (unsigned i = 0; i != 8; ++i)
      if (!isConstantOrUndef(N->getMaskElt(i),    i*2+j) ||
          !isConstantOrUndef(N->getMaskElt(i+8),  i*2+j))
        return false;
  }
  return true;
}

/// isVPKUWUMShuffleMask - Return true if this is the shuffle mask for a
/// VPKUWUM instruction.
/// The ShuffleKind distinguishes between big-endian operations with
/// two different inputs (0), either-endian operations with two identical
/// inputs (1), and little-endian operations with two different inputs (2).
/// For the latter, the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVPKUWUMShuffleMask(ShuffleVectorSDNode *N, unsigned ShuffleKind,
                               SelectionDAG &DAG) {
  bool IsLE = DAG.getDataLayout().isLittleEndian();
  if (ShuffleKind == 0) {
    if (IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 2)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+2) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+3))
        return false;
  } else if (ShuffleKind == 2) {
    if (!IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 2)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+1))
        return false;
  } else if (ShuffleKind == 1) {
    unsigned j = IsLE ? 0 : 2;
    for (unsigned i = 0; i != 8; i += 2)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+j+1) ||
          !isConstantOrUndef(N->getMaskElt(i+8),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+9),  i*2+j+1))
        return false;
  }
  return true;
}

/// isVPKUDUMShuffleMask - Return true if this is the shuffle mask for a
/// VPKUDUM instruction, AND the VPKUDUM instruction exists for the
/// current subtarget.
///
/// The ShuffleKind distinguishes between big-endian operations with
/// two different inputs (0), either-endian operations with two identical
/// inputs (1), and little-endian operations with two different inputs (2).
/// For the latter, the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVPKUDUMShuffleMask(ShuffleVectorSDNode *N, unsigned ShuffleKind,
                               SelectionDAG &DAG) {
  const PPCSubtarget &Subtarget = DAG.getSubtarget<PPCSubtarget>();
  if (!Subtarget.hasP8Vector())
    return false;

  bool IsLE = DAG.getDataLayout().isLittleEndian();
  if (ShuffleKind == 0) {
    if (IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 4)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+4) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+5) ||
          !isConstantOrUndef(N->getMaskElt(i+2),  i*2+6) ||
          !isConstantOrUndef(N->getMaskElt(i+3),  i*2+7))
        return false;
  } else if (ShuffleKind == 2) {
    if (!IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 4)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+1) ||
          !isConstantOrUndef(N->getMaskElt(i+2),  i*2+2) ||
          !isConstantOrUndef(N->getMaskElt(i+3),  i*2+3))
        return false;
  } else if (ShuffleKind == 1) {
    unsigned j = IsLE ? 0 : 4;
    for (unsigned i = 0; i != 8; i += 4)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+j+1) ||
          !isConstantOrUndef(N->getMaskElt(i+2),  i*2+j+2) ||
          !isConstantOrUndef(N->getMaskElt(i+3),  i*2+j+3) ||
          !isConstantOrUndef(N->getMaskElt(i+8),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+9),  i*2+j+1) ||
          !isConstantOrUndef(N->getMaskElt(i+10), i*2+j+2) ||
          !isConstantOrUndef(N->getMaskElt(i+11), i*2+j+3))
        return false;
  }
  return true;
}

/// isVMerge - Common function, used to match vmrg* shuffles.
///
static bool isVMerge(ShuffleVectorSDNode *N, unsigned UnitSize,
                     unsigned LHSStart, unsigned RHSStart) {
  if (N->getValueType(0) != MVT::v16i8)
    return false;
  assert((UnitSize == 1 || UnitSize == 2 || UnitSize == 4) &&
         "Unsupported merge size!");

  for (unsigned i = 0; i != 8/UnitSize; ++i)     // Step over units
    for (unsigned j = 0; j != UnitSize; ++j) {   // Step over bytes within unit
      if (!isConstantOrUndef(N->getMaskElt(i*UnitSize*2+j),
                             LHSStart+j+i*UnitSize) ||
          !isConstantOrUndef(N->getMaskElt(i*UnitSize*2+UnitSize+j),
                             RHSStart+j+i*UnitSize))
        return false;
    }
  return true;
}

/// isVMRGLShuffleMask - Return true if this is a shuffle mask suitable for
/// a VMRGL* instruction with the specified unit size (1,2 or 4 bytes).
/// The ShuffleKind distinguishes between big-endian merges with two
/// different inputs (0), either-endian merges with two identical inputs (1),
/// and little-endian merges with two different inputs (2).  For the latter,
/// the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVMRGLShuffleMask(ShuffleVectorSDNode *N, unsigned UnitSize,
                             unsigned ShuffleKind, SelectionDAG &DAG) {
  if (DAG.getDataLayout().isLittleEndian()) {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 0, 0);
    else if (ShuffleKind == 2) // swapped
      return isVMerge(N, UnitSize, 0, 16);
    else
      return false;
  } else {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 8, 8);
    else if (ShuffleKind == 0) // normal
      return isVMerge(N, UnitSize, 8, 24);
    else
      return false;
  }
}

/// isVMRGHShuffleMask - Return true if this is a shuffle mask suitable for
/// a VMRGH* instruction with the specified unit size (1,2 or 4 bytes).
/// The ShuffleKind distinguishes between big-endian merges with two
/// different inputs (0), either-endian merges with two identical inputs (1),
/// and little-endian merges with two different inputs (2).  For the latter,
/// the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVMRGHShuffleMask(ShuffleVectorSDNode *N, unsigned UnitSize,
                             unsigned ShuffleKind, SelectionDAG &DAG) {
  if (DAG.getDataLayout().isLittleEndian()) {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 8, 8);
    else if (ShuffleKind == 2) // swapped
      return isVMerge(N, UnitSize, 8, 24);
    else
      return false;
  } else {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 0, 0);
    else if (ShuffleKind == 0) // normal
      return isVMerge(N, UnitSize, 0, 16);
    else
      return false;
  }
}

/**
 * Common function used to match vmrgew and vmrgow shuffles
 *
 * The indexOffset determines whether to look for even or odd words in
 * the shuffle mask. This is based on the of the endianness of the target
 * machine.
 *   - Little Endian:
 *     - Use offset of 0 to check for odd elements
 *     - Use offset of 4 to check for even elements
 *   - Big Endian:
 *     - Use offset of 0 to check for even elements
 *     - Use offset of 4 to check for odd elements
 * A detailed description of the vector element ordering for little endian and
 * big endian can be found at
 * http://www.ibm.com/developerworks/library/l-ibm-xl-c-cpp-compiler/index.html
 * Targeting your applications - what little endian and big endian IBM XL C/C++
 * compiler differences mean to you
 *
 * The mask to the shuffle vector instruction specifies the indices of the
 * elements from the two input vectors to place in the result. The elements are
 * numbered in array-access order, starting with the first vector. These vectors
 * are always of type v16i8, thus each vector will contain 16 elements of size
 * 8. More info on the shuffle vector can be found in the
 * http://llvm.org/docs/LangRef.html#shufflevector-instruction
 * Language Reference.
 *
 * The RHSStartValue indicates whether the same input vectors are used (unary)
 * or two different input vectors are used, based on the following:
 *   - If the instruction uses the same vector for both inputs, the range of the
 *     indices will be 0 to 15. In this case, the RHSStart value passed should
 *     be 0.
 *   - If the instruction has two different vectors then the range of the
 *     indices will be 0 to 31. In this case, the RHSStart value passed should
 *     be 16 (indices 0-15 specify elements in the first vector while indices 16
 *     to 31 specify elements in the second vector).
 *
 * \param[in] N The shuffle vector SD Node to analyze
 * \param[in] IndexOffset Specifies whether to look for even or odd elements
 * \param[in] RHSStartValue Specifies the starting index for the righthand input
 * vector to the shuffle_vector instruction
 * \return true iff this shuffle vector represents an even or odd word merge
 */
static bool isVMerge(ShuffleVectorSDNode *N, unsigned IndexOffset,
                     unsigned RHSStartValue) {
  if (N->getValueType(0) != MVT::v16i8)
    return false;

  for (unsigned i = 0; i < 2; ++i)
    for (unsigned j = 0; j < 4; ++j)
      if (!isConstantOrUndef(N->getMaskElt(i*4+j),
                             i*RHSStartValue+j+IndexOffset) ||
          !isConstantOrUndef(N->getMaskElt(i*4+j+8),
                             i*RHSStartValue+j+IndexOffset+8))
        return false;
  return true;
}

/**
 * Determine if the specified shuffle mask is suitable for the vmrgew or
 * vmrgow instructions.
 *
 * \param[in] N The shuffle vector SD Node to analyze
 * \param[in] CheckEven Check for an even merge (true) or an odd merge (false)
 * \param[in] ShuffleKind Identify the type of merge:
 *   - 0 = big-endian merge with two different inputs;
 *   - 1 = either-endian merge with two identical inputs;
 *   - 2 = little-endian merge with two different inputs (inputs are swapped for
 *     little-endian merges).
 * \param[in] DAG The current SelectionDAG
 * \return true iff this shuffle mask
 */
bool PPC::isVMRGEOShuffleMask(ShuffleVectorSDNode *N, bool CheckEven,
                              unsigned ShuffleKind, SelectionDAG &DAG) {
  if (DAG.getDataLayout().isLittleEndian()) {
    unsigned indexOffset = CheckEven ? 4 : 0;
    if (ShuffleKind == 1) // Unary
      return isVMerge(N, indexOffset, 0);
    else if (ShuffleKind == 2) // swapped
      return isVMerge(N, indexOffset, 16);
    else
      return false;
  }
  else {
    unsigned indexOffset = CheckEven ? 0 : 4;
    if (ShuffleKind == 1) // Unary
      return isVMerge(N, indexOffset, 0);
    else if (ShuffleKind == 0) // Normal
      return isVMerge(N, indexOffset, 16);
    else
      return false;
  }
  return false;
}

/// isVSLDOIShuffleMask - If this is a vsldoi shuffle mask, return the shift
/// amount, otherwise return -1.
/// The ShuffleKind distinguishes between big-endian operations with two
/// different inputs (0), either-endian operations with two identical inputs
/// (1), and little-endian operations with two different inputs (2).  For the
/// latter, the input operands are swapped (see PPCInstrAltivec.td).
int PPC::isVSLDOIShuffleMask(SDNode *N, unsigned ShuffleKind,
                             SelectionDAG &DAG) {
  if (N->getValueType(0) != MVT::v16i8)
    return -1;

  ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(N);

  // Find the first non-undef value in the shuffle mask.
  unsigned i;
  for (i = 0; i != 16 && SVOp->getMaskElt(i) < 0; ++i)
    /*search*/;

  if (i == 16) return -1;  // all undef.

  // Otherwise, check to see if the rest of the elements are consecutively
  // numbered from this value.
  unsigned ShiftAmt = SVOp->getMaskElt(i);
  if (ShiftAmt < i) return -1;

  ShiftAmt -= i;
  bool isLE = DAG.getDataLayout().isLittleEndian();

  if ((ShuffleKind == 0 && !isLE) || (ShuffleKind == 2 && isLE)) {
    // Check the rest of the elements to see if they are consecutive.
    for (++i; i != 16; ++i)
      if (!isConstantOrUndef(SVOp->getMaskElt(i), ShiftAmt+i))
        return -1;
  } else if (ShuffleKind == 1) {
    // Check the rest of the elements to see if they are consecutive.
    for (++i; i != 16; ++i)
      if (!isConstantOrUndef(SVOp->getMaskElt(i), (ShiftAmt+i) & 15))
        return -1;
  } else
    return -1;

  if (isLE)
    ShiftAmt = 16 - ShiftAmt;

  return ShiftAmt;
}

/// isSplatShuffleMask - Return true if the specified VECTOR_SHUFFLE operand
/// specifies a splat of a single element that is suitable for input to
/// one of the splat operations (VSPLTB/VSPLTH/VSPLTW/XXSPLTW/LXVDSX/etc.).
bool PPC::isSplatShuffleMask(ShuffleVectorSDNode *N, unsigned EltSize) {
  EVT VT = N->getValueType(0);
  if (VT == MVT::v2i64 || VT == MVT::v2f64)
    return EltSize == 8 && N->getMaskElt(0) == N->getMaskElt(1);

  assert(VT == MVT::v16i8 && isPowerOf2_32(EltSize) &&
         EltSize <= 8 && "Can only handle 1,2,4,8 byte element sizes");

  // The consecutive indices need to specify an element, not part of two
  // different elements.  So abandon ship early if this isn't the case.
  if (N->getMaskElt(0) % EltSize != 0)
    return false;

  // This is a splat operation if each element of the permute is the same, and
  // if the value doesn't reference the second vector.
  unsigned ElementBase = N->getMaskElt(0);

  // FIXME: Handle UNDEF elements too!
  if (ElementBase >= 16)
    return false;

  // Check that the indices are consecutive, in the case of a multi-byte element
  // splatted with a v16i8 mask.
  for (unsigned i = 1; i != EltSize; ++i)
    if (N->getMaskElt(i) < 0 || N->getMaskElt(i) != (int)(i+ElementBase))
      return false;

  for (unsigned i = EltSize, e = 16; i != e; i += EltSize) {
    if (N->getMaskElt(i) < 0) continue;
    for (unsigned j = 0; j != EltSize; ++j)
      if (N->getMaskElt(i+j) != N->getMaskElt(j))
        return false;
  }
  return true;
}

/// Check that the mask is shuffling N byte elements. Within each N byte
/// element of the mask, the indices could be either in increasing or
/// decreasing order as long as they are consecutive.
/// \param[in] N the shuffle vector SD Node to analyze
/// \param[in] Width the element width in bytes, could be 2/4/8/16 (HalfWord/
/// Word/DoubleWord/QuadWord).
/// \param[in] StepLen the delta indices number among the N byte element, if
/// the mask is in increasing/decreasing order then it is 1/-1.
/// \return true iff the mask is shuffling N byte elements.
static bool isNByteElemShuffleMask(ShuffleVectorSDNode *N, unsigned Width,
                                   int StepLen) {
  assert((Width == 2 || Width == 4 || Width == 8 || Width == 16) &&
         "Unexpected element width.");
  assert((StepLen == 1 || StepLen == -1) && "Unexpected element width.");

  unsigned NumOfElem = 16 / Width;
  unsigned MaskVal[16]; //  Width is never greater than 16
  for (unsigned i = 0; i < NumOfElem; ++i) {
    MaskVal[0] = N->getMaskElt(i * Width);
    if ((StepLen == 1) && (MaskVal[0] % Width)) {
      return false;
    } else if ((StepLen == -1) && ((MaskVal[0] + 1) % Width)) {
      return false;
    }

    for (unsigned int j = 1; j < Width; ++j) {
      MaskVal[j] = N->getMaskElt(i * Width + j);
      if (MaskVal[j] != MaskVal[j-1] + StepLen) {
        return false;
      }
    }
  }

  return true;
}

bool PPC::isXXINSERTWMask(ShuffleVectorSDNode *N, unsigned &ShiftElts,
                          unsigned &InsertAtByte, bool &Swap, bool IsLE) {
  if (!isNByteElemShuffleMask(N, 4, 1))
    return false;

  // Now we look at mask elements 0,4,8,12
  unsigned M0 = N->getMaskElt(0) / 4;
  unsigned M1 = N->getMaskElt(4) / 4;
  unsigned M2 = N->getMaskElt(8) / 4;
  unsigned M3 = N->getMaskElt(12) / 4;
  unsigned LittleEndianShifts[] = { 2, 1, 0, 3 };
  unsigned BigEndianShifts[] = { 3, 0, 1, 2 };

  // Below, let H and L be arbitrary elements of the shuffle mask
  // where H is in the range [4,7] and L is in the range [0,3].
  // H, 1, 2, 3 or L, 5, 6, 7
  if ((M0 > 3 && M1 == 1 && M2 == 2 && M3 == 3) ||
      (M0 < 4 && M1 == 5 && M2 == 6 && M3 == 7)) {
    ShiftElts = IsLE ? LittleEndianShifts[M0 & 0x3] : BigEndianShifts[M0 & 0x3];
    InsertAtByte = IsLE ? 12 : 0;
    Swap = M0 < 4;
    return true;
  }
  // 0, H, 2, 3 or 4, L, 6, 7
  if ((M1 > 3 && M0 == 0 && M2 == 2 && M3 == 3) ||
      (M1 < 4 && M0 == 4 && M2 == 6 && M3 == 7)) {
    ShiftElts = IsLE ? LittleEndianShifts[M1 & 0x3] : BigEndianShifts[M1 & 0x3];
    InsertAtByte = IsLE ? 8 : 4;
    Swap = M1 < 4;
    return true;
  }
  // 0, 1, H, 3 or 4, 5, L, 7
  if ((M2 > 3 && M0 == 0 && M1 == 1 && M3 == 3) ||
      (M2 < 4 && M0 == 4 && M1 == 5 && M3 == 7)) {
    ShiftElts = IsLE ? LittleEndianShifts[M2 & 0x3] : BigEndianShifts[M2 & 0x3];
    InsertAtByte = IsLE ? 4 : 8;
    Swap = M2 < 4;
    return true;
  }
  // 0, 1, 2, H or 4, 5, 6, L
  if ((M3 > 3 && M0 == 0 && M1 == 1 && M2 == 2) ||
      (M3 < 4 && M0 == 4 && M1 == 5 && M2 == 6)) {
    ShiftElts = IsLE ? LittleEndianShifts[M3 & 0x3] : BigEndianShifts[M3 & 0x3];
    InsertAtByte = IsLE ? 0 : 12;
    Swap = M3 < 4;
    return true;
  }

  // If both vector operands for the shuffle are the same vector, the mask will
  // contain only elements from the first one and the second one will be undef.
  if (N->getOperand(1).isUndef()) {
    ShiftElts = 0;
    Swap = true;
    unsigned XXINSERTWSrcElem = IsLE ? 2 : 1;
    if (M0 == XXINSERTWSrcElem && M1 == 1 && M2 == 2 && M3 == 3) {
      InsertAtByte = IsLE ? 12 : 0;
      return true;
    }
    if (M0 == 0 && M1 == XXINSERTWSrcElem && M2 == 2 && M3 == 3) {
      InsertAtByte = IsLE ? 8 : 4;
      return true;
    }
    if (M0 == 0 && M1 == 1 && M2 == XXINSERTWSrcElem && M3 == 3) {
      InsertAtByte = IsLE ? 4 : 8;
      return true;
    }
    if (M0 == 0 && M1 == 1 && M2 == 2 && M3 == XXINSERTWSrcElem) {
      InsertAtByte = IsLE ? 0 : 12;
      return true;
    }
  }

  return false;
}

bool PPC::isXXSLDWIShuffleMask(ShuffleVectorSDNode *N, unsigned &ShiftElts,
                               bool &Swap, bool IsLE) {
  assert(N->getValueType(0) == MVT::v16i8 && "Shuffle vector expects v16i8");
  // Ensure each byte index of the word is consecutive.
  if (!isNByteElemShuffleMask(N, 4, 1))
    return false;

  // Now we look at mask elements 0,4,8,12, which are the beginning of words.
  unsigned M0 = N->getMaskElt(0) / 4;
  unsigned M1 = N->getMaskElt(4) / 4;
  unsigned M2 = N->getMaskElt(8) / 4;
  unsigned M3 = N->getMaskElt(12) / 4;

  // If both vector operands for the shuffle are the same vector, the mask will
  // contain only elements from the first one and the second one will be undef.
  if (N->getOperand(1).isUndef()) {
    assert(M0 < 4 && "Indexing into an undef vector?");
    if (M1 != (M0 + 1) % 4 || M2 != (M1 + 1) % 4 || M3 != (M2 + 1) % 4)
      return false;

    ShiftElts = IsLE ? (4 - M0) % 4 : M0;
    Swap = false;
    return true;
  }

  // Ensure each word index of the ShuffleVector Mask is consecutive.
  if (M1 != (M0 + 1) % 8 || M2 != (M1 + 1) % 8 || M3 != (M2 + 1) % 8)
    return false;

  if (IsLE) {
    if (M0 == 0 || M0 == 7 || M0 == 6 || M0 == 5) {
      // Input vectors don't need to be swapped if the leading element
      // of the result is one of the 3 left elements of the second vector
      // (or if there is no shift to be done at all).
      Swap = false;
      ShiftElts = (8 - M0) % 8;
    } else if (M0 == 4 || M0 == 3 || M0 == 2 || M0 == 1) {
      // Input vectors need to be swapped if the leading element
      // of the result is one of the 3 left elements of the first vector
      // (or if we're shifting by 4 - thereby simply swapping the vectors).
      Swap = true;
      ShiftElts = (4 - M0) % 4;
    }

    return true;
  } else {                                          // BE
    if (M0 == 0 || M0 == 1 || M0 == 2 || M0 == 3) {
      // Input vectors don't need to be swapped if the leading element
      // of the result is one of the 4 elements of the first vector.
      Swap = false;
      ShiftElts = M0;
    } else if (M0 == 4 || M0 == 5 || M0 == 6 || M0 == 7) {
      // Input vectors need to be swapped if the leading element
      // of the result is one of the 4 elements of the right vector.
      Swap = true;
      ShiftElts = M0 - 4;
    }

    return true;
  }
}

bool static isXXBRShuffleMaskHelper(ShuffleVectorSDNode *N, int Width) {
  assert(N->getValueType(0) == MVT::v16i8 && "Shuffle vector expects v16i8");

  if (!isNByteElemShuffleMask(N, Width, -1))
    return false;

  for (int i = 0; i < 16; i += Width)
    if (N->getMaskElt(i) != i + Width - 1)
      return false;

  return true;
}

bool PPC::isXXBRHShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 2);
}

bool PPC::isXXBRWShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 4);
}

bool PPC::isXXBRDShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 8);
}

bool PPC::isXXBRQShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 16);
}

/// Can node \p N be lowered to an XXPERMDI instruction? If so, set \p Swap
/// if the inputs to the instruction should be swapped and set \p DM to the
/// value for the immediate.
/// Specifically, set \p Swap to true only if \p N can be lowered to XXPERMDI
/// AND element 0 of the result comes from the first input (LE) or second input
/// (BE). Set \p DM to the calculated result (0-3) only if \p N can be lowered.
/// \return true iff the given mask of shuffle node \p N is a XXPERMDI shuffle
/// mask.
bool PPC::isXXPERMDIShuffleMask(ShuffleVectorSDNode *N, unsigned &DM,
                               bool &Swap, bool IsLE) {
  assert(N->getValueType(0) == MVT::v16i8 && "Shuffle vector expects v16i8");

  // Ensure each byte index of the double word is consecutive.
  if (!isNByteElemShuffleMask(N, 8, 1))
    return false;

  unsigned M0 = N->getMaskElt(0) / 8;
  unsigned M1 = N->getMaskElt(8) / 8;
  assert(((M0 | M1) < 4) && "A mask element out of bounds?");

  // If both vector operands for the shuffle are the same vector, the mask will
  // contain only elements from the first one and the second one will be undef.
  if (N->getOperand(1).isUndef()) {
    if ((M0 | M1) < 2) {
      DM = IsLE ? (((~M1) & 1) << 1) + ((~M0) & 1) : (M0 << 1) + (M1 & 1);
      Swap = false;
      return true;
    } else
      return false;
  }

  if (IsLE) {
    if (M0 > 1 && M1 < 2) {
      Swap = false;
    } else if (M0 < 2 && M1 > 1) {
      M0 = (M0 + 2) % 4;
      M1 = (M1 + 2) % 4;
      Swap = true;
    } else
      return false;

    // Note: if control flow comes here that means Swap is already set above
    DM = (((~M1) & 1) << 1) + ((~M0) & 1);
    return true;
  } else { // BE
    if (M0 < 2 && M1 > 1) {
      Swap = false;
    } else if (M0 > 1 && M1 < 2) {
      M0 = (M0 + 2) % 4;
      M1 = (M1 + 2) % 4;
      Swap = true;
    } else
      return false;

    // Note: if control flow comes here that means Swap is already set above
    DM = (M0 << 1) + (M1 & 1);
    return true;
  }
}


/// getSplatIdxForPPCMnemonics - Return the splat index as a value that is
/// appropriate for PPC mnemonics (which have a big endian bias - namely
/// elements are counted from the left of the vector register).
unsigned PPC::getSplatIdxForPPCMnemonics(SDNode *N, unsigned EltSize,
                                         SelectionDAG &DAG) {
  ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(N);
  assert(isSplatShuffleMask(SVOp, EltSize));
  EVT VT = SVOp->getValueType(0);

  if (VT == MVT::v2i64 || VT == MVT::v2f64)
    return DAG.getDataLayout().isLittleEndian() ? 1 - SVOp->getMaskElt(0)
                                                : SVOp->getMaskElt(0);

  if (DAG.getDataLayout().isLittleEndian())
    return (16 / EltSize) - 1 - (SVOp->getMaskElt(0) / EltSize);
  else
    return SVOp->getMaskElt(0) / EltSize;
}

/// get_VSPLTI_elt - If this is a build_vector of constants which can be formed
/// by using a vspltis[bhw] instruction of the specified element size, return
/// the constant being splatted.  The ByteSize field indicates the number of
/// bytes of each element [124] -> [bhw].
SDValue PPC::get_VSPLTI_elt(SDNode *N, unsigned ByteSize, SelectionDAG &DAG) {
  SDValue OpVal;

  // If ByteSize of the splat is bigger than the element size of the
  // build_vector, then we have a case where we are checking for a splat where
  // multiple elements of the buildvector are folded together into a single
  // logical element of the splat (e.g. "vsplish 1" to splat {0,1}*8).
  unsigned EltSize = 16/N->getNumOperands();
  if (EltSize < ByteSize) {
    unsigned Multiple = ByteSize/EltSize;   // Number of BV entries per spltval.
    SDValue UniquedVals[4];
    assert(Multiple > 1 && Multiple <= 4 && "How can this happen?");

    // See if all of the elements in the buildvector agree across.
    for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
      if (N->getOperand(i).isUndef()) continue;
      // If the element isn't a constant, bail fully out.
      if (!isa<ConstantSDNode>(N->getOperand(i))) return SDValue();

      if (!UniquedVals[i&(Multiple-1)].getNode())
        UniquedVals[i&(Multiple-1)] = N->getOperand(i);
      else if (UniquedVals[i&(Multiple-1)] != N->getOperand(i))
        return SDValue();  // no match.
    }

    // Okay, if we reached this point, UniquedVals[0..Multiple-1] contains
    // either constant or undef values that are identical for each chunk.  See
    // if these chunks can form into a larger vspltis*.

    // Check to see if all of the leading entries are either 0 or -1.  If
    // neither, then this won't fit into the immediate field.
    bool LeadingZero = true;
    bool LeadingOnes = true;
    for (unsigned i = 0; i != Multiple-1; ++i) {
      if (!UniquedVals[i].getNode()) continue;  // Must have been undefs.

      LeadingZero &= isNullConstant(UniquedVals[i]);
      LeadingOnes &= isAllOnesConstant(UniquedVals[i]);
    }
    // Finally, check the least significant entry.
    if (LeadingZero) {
      if (!UniquedVals[Multiple-1].getNode())
        return DAG.getTargetConstant(0, SDLoc(N), MVT::i32);  // 0,0,0,undef
      int Val = UniquedVals[Multiple - 1]->getAsZExtVal();
      if (Val < 16)                                   // 0,0,0,4 -> vspltisw(4)
        return DAG.getTargetConstant(Val, SDLoc(N), MVT::i32);
    }
    if (LeadingOnes) {
      if (!UniquedVals[Multiple-1].getNode())
        return DAG.getTargetConstant(~0U, SDLoc(N), MVT::i32); // -1,-1,-1,undef
      int Val =cast<ConstantSDNode>(UniquedVals[Multiple-1])->getSExtValue();
      if (Val >= -16)                            // -1,-1,-1,-2 -> vspltisw(-2)
        return DAG.getTargetConstant(Val, SDLoc(N), MVT::i32);
    }

    return SDValue();
  }

  // Check to see if this buildvec has a single non-undef value in its elements.
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    if (N->getOperand(i).isUndef()) continue;
    if (!OpVal.getNode())
      OpVal = N->getOperand(i);
    else if (OpVal != N->getOperand(i))
      return SDValue();
  }

  if (!OpVal.getNode()) return SDValue();  // All UNDEF: use implicit def.

  unsigned ValSizeInBytes = EltSize;
  uint64_t Value = 0;
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(OpVal)) {
    Value = CN->getZExtValue();
  } else if (ConstantFPSDNode *CN = dyn_cast<ConstantFPSDNode>(OpVal)) {
    assert(CN->getValueType(0) == MVT::f32 && "Only one legal FP vector type!");
    Value = llvm::bit_cast<uint32_t>(CN->getValueAPF().convertToFloat());
  }

  // If the splat value is larger than the element value, then we can never do
  // this splat.  The only case that we could fit the replicated bits into our
  // immediate field for would be zero, and we prefer to use vxor for it.
  if (ValSizeInBytes < ByteSize) return SDValue();

  // If the element value is larger than the splat value, check if it consists
  // of a repeated bit pattern of size ByteSize.
  if (!APInt(ValSizeInBytes * 8, Value).isSplat(ByteSize * 8))
    return SDValue();

  // Properly sign extend the value.
  int MaskVal = SignExtend32(Value, ByteSize * 8);

  // If this is zero, don't match, zero matches ISD::isBuildVectorAllZeros.
  if (MaskVal == 0) return SDValue();

  // Finally, if this value fits in a 5 bit sext field, return it
  if (SignExtend32<5>(MaskVal) == MaskVal)
    return DAG.getTargetConstant(MaskVal, SDLoc(N), MVT::i32);
  return SDValue();
}

//===----------------------------------------------------------------------===//
//  Addressing Mode Selection
//===----------------------------------------------------------------------===//

/// isIntS16Immediate - This method tests to see if the node is either a 32-bit
/// or 64-bit immediate, and if the value can be accurately represented as a
/// sign extension from a 16-bit value.  If so, this returns true and the
/// immediate.
bool llvm::isIntS16Immediate(SDNode *N, int16_t &Imm) {
  if (!isa<ConstantSDNode>(N))
    return false;

  Imm = (int16_t)N->getAsZExtVal();
  if (N->getValueType(0) == MVT::i32)
    return Imm == (int32_t)N->getAsZExtVal();
  else
    return Imm == (int64_t)N->getAsZExtVal();
}
bool llvm::isIntS16Immediate(SDValue Op, int16_t &Imm) {
  return isIntS16Immediate(Op.getNode(), Imm);
}

/// Used when computing address flags for selecting loads and stores.
/// If we have an OR, check if the LHS and RHS are provably disjoint.
/// An OR of two provably disjoint values is equivalent to an ADD.
/// Most PPC load/store instructions compute the effective address as a sum,
/// so doing this conversion is useful.
static bool provablyDisjointOr(SelectionDAG &DAG, const SDValue &N) {
  if (N.getOpcode() != ISD::OR)
    return false;
  KnownBits LHSKnown = DAG.computeKnownBits(N.getOperand(0));
  if (!LHSKnown.Zero.getBoolValue())
    return false;
  KnownBits RHSKnown = DAG.computeKnownBits(N.getOperand(1));
  return (~(LHSKnown.Zero | RHSKnown.Zero) == 0);
}

/// SelectAddressEVXRegReg - Given the specified address, check to see if it can
/// be represented as an indexed [r+r] operation.
bool PPCTargetLowering::SelectAddressEVXRegReg(SDValue N, SDValue &Base,
                                               SDValue &Index,
                                               SelectionDAG &DAG) const {
  for (SDNode *U : N->uses()) {
    if (MemSDNode *Memop = dyn_cast<MemSDNode>(U)) {
      if (Memop->getMemoryVT() == MVT::f64) {
          Base = N.getOperand(0);
          Index = N.getOperand(1);
          return true;
      }
    }
  }
  return false;
}

/// isIntS34Immediate - This method tests if value of node given can be
/// accurately represented as a sign extension from a 34-bit value.  If so,
/// this returns true and the immediate.
bool llvm::isIntS34Immediate(SDNode *N, int64_t &Imm) {
  if (!isa<ConstantSDNode>(N))
    return false;

  Imm = (int64_t)N->getAsZExtVal();
  return isInt<34>(Imm);
}
bool llvm::isIntS34Immediate(SDValue Op, int64_t &Imm) {
  return isIntS34Immediate(Op.getNode(), Imm);
}

/// SelectAddressRegReg - Given the specified addressed, check to see if it
/// can be represented as an indexed [r+r] operation.  Returns false if it
/// can be more efficiently represented as [r+imm]. If \p EncodingAlignment is
/// non-zero and N can be represented by a base register plus a signed 16-bit
/// displacement, make a more precise judgement by checking (displacement % \p
/// EncodingAlignment).
bool PPCTargetLowering::SelectAddressRegReg(
    SDValue N, SDValue &Base, SDValue &Index, SelectionDAG &DAG,
    MaybeAlign EncodingAlignment) const {
  // If we have a PC Relative target flag don't select as [reg+reg]. It will be
  // a [pc+imm].
  if (SelectAddressPCRel(N, Base))
    return false;

  int16_t Imm = 0;
  if (N.getOpcode() == ISD::ADD) {
    // Is there any SPE load/store (f64), which can't handle 16bit offset?
    // SPE load/store can only handle 8-bit offsets.
    if (hasSPE() && SelectAddressEVXRegReg(N, Base, Index, DAG))
        return true;
    if (isIntS16Immediate(N.getOperand(1), Imm) &&
        (!EncodingAlignment || isAligned(*EncodingAlignment, Imm)))
      return false; // r+i
    if (N.getOperand(1).getOpcode() == PPCISD::Lo)
      return false;    // r+i

    Base = N.getOperand(0);
    Index = N.getOperand(1);
    return true;
  } else if (N.getOpcode() == ISD::OR) {
    if (isIntS16Immediate(N.getOperand(1), Imm) &&
        (!EncodingAlignment || isAligned(*EncodingAlignment, Imm)))
      return false; // r+i can fold it if we can.

    // If this is an or of disjoint bitfields, we can codegen this as an add
    // (for better address arithmetic) if the LHS and RHS of the OR are provably
    // disjoint.
    KnownBits LHSKnown = DAG.computeKnownBits(N.getOperand(0));

    if (LHSKnown.Zero.getBoolValue()) {
      KnownBits RHSKnown = DAG.computeKnownBits(N.getOperand(1));
      // If all of the bits are known zero on the LHS or RHS, the add won't
      // carry.
      if (~(LHSKnown.Zero | RHSKnown.Zero) == 0) {
        Base = N.getOperand(0);
        Index = N.getOperand(1);
        return true;
      }
    }
  }

  return false;
}

// If we happen to be doing an i64 load or store into a stack slot that has
// less than a 4-byte alignment, then the frame-index elimination may need to
// use an indexed load or store instruction (because the offset may not be a
// multiple of 4). The extra register needed to hold the offset comes from the
// register scavenger, and it is possible that the scavenger will need to use
// an emergency spill slot. As a result, we need to make sure that a spill slot
// is allocated when doing an i64 load/store into a less-than-4-byte-aligned
// stack slot.
static void fixupFuncForFI(SelectionDAG &DAG, int FrameIdx, EVT VT) {
  // FIXME: This does not handle the LWA case.
  if (VT != MVT::i64)
    return;

  // NOTE: We'll exclude negative FIs here, which come from argument
  // lowering, because there are no known test cases triggering this problem
  // using packed structures (or similar). We can remove this exclusion if
  // we find such a test case. The reason why this is so test-case driven is
  // because this entire 'fixup' is only to prevent crashes (from the
  // register scavenger) on not-really-valid inputs. For example, if we have:
  //   %a = alloca i1
  //   %b = bitcast i1* %a to i64*
  //   store i64* a, i64 b
  // then the store should really be marked as 'align 1', but is not. If it
  // were marked as 'align 1' then the indexed form would have been
  // instruction-selected initially, and the problem this 'fixup' is preventing
  // won't happen regardless.
  if (FrameIdx < 0)
    return;

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  if (MFI.getObjectAlign(FrameIdx) >= Align(4))
    return;

  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setHasNonRISpills();
}

/// Returns true if the address N can be represented by a base register plus
/// a signed 16-bit displacement [r+imm], and if it is not better
/// represented as reg+reg.  If \p EncodingAlignment is non-zero, only accept
/// displacements that are multiples of that value.
bool PPCTargetLowering::SelectAddressRegImm(
    SDValue N, SDValue &Disp, SDValue &Base, SelectionDAG &DAG,
    MaybeAlign EncodingAlignment) const {
  // FIXME dl should come from parent load or store, not from address
  SDLoc dl(N);

  // If we have a PC Relative target flag don't select as [reg+imm]. It will be
  // a [pc+imm].
  if (SelectAddressPCRel(N, Base))
    return false;

  // If this can be more profitably realized as r+r, fail.
  if (SelectAddressRegReg(N, Disp, Base, DAG, EncodingAlignment))
    return false;

  if (N.getOpcode() == ISD::ADD) {
    int16_t imm = 0;
    if (isIntS16Immediate(N.getOperand(1), imm) &&
        (!EncodingAlignment || isAligned(*EncodingAlignment, imm))) {
      Disp = DAG.getTargetConstant(imm, dl, N.getValueType());
      if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N.getOperand(0))) {
        Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
        fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
      } else {
        Base = N.getOperand(0);
      }
      return true; // [r+i]
    } else if (N.getOperand(1).getOpcode() == PPCISD::Lo) {
      // Match LOAD (ADD (X, Lo(G))).
      assert(!N.getOperand(1).getConstantOperandVal(1) &&
             "Cannot handle constant offsets yet!");
      Disp = N.getOperand(1).getOperand(0);  // The global address.
      assert(Disp.getOpcode() == ISD::TargetGlobalAddress ||
             Disp.getOpcode() == ISD::TargetGlobalTLSAddress ||
             Disp.getOpcode() == ISD::TargetConstantPool ||
             Disp.getOpcode() == ISD::TargetJumpTable);
      Base = N.getOperand(0);
      return true;  // [&g+r]
    }
  } else if (N.getOpcode() == ISD::OR) {
    int16_t imm = 0;
    if (isIntS16Immediate(N.getOperand(1), imm) &&
        (!EncodingAlignment || isAligned(*EncodingAlignment, imm))) {
      // If this is an or of disjoint bitfields, we can codegen this as an add
      // (for better address arithmetic) if the LHS and RHS of the OR are
      // provably disjoint.
      KnownBits LHSKnown = DAG.computeKnownBits(N.getOperand(0));

      if ((LHSKnown.Zero.getZExtValue()|~(uint64_t)imm) == ~0ULL) {
        // If all of the bits are known zero on the LHS or RHS, the add won't
        // carry.
        if (FrameIndexSDNode *FI =
              dyn_cast<FrameIndexSDNode>(N.getOperand(0))) {
          Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
          fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
        } else {
          Base = N.getOperand(0);
        }
        Disp = DAG.getTargetConstant(imm, dl, N.getValueType());
        return true;
      }
    }
  } else if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N)) {
    // Loading from a constant address.

    // If this address fits entirely in a 16-bit sext immediate field, codegen
    // this as "d, 0"
    int16_t Imm;
    if (isIntS16Immediate(CN, Imm) &&
        (!EncodingAlignment || isAligned(*EncodingAlignment, Imm))) {
      Disp = DAG.getTargetConstant(Imm, dl, CN->getValueType(0));
      Base = DAG.getRegister(Subtarget.isPPC64() ? PPC::ZERO8 : PPC::ZERO,
                             CN->getValueType(0));
      return true;
    }

    // Handle 32-bit sext immediates with LIS + addr mode.
    if ((CN->getValueType(0) == MVT::i32 ||
         (int64_t)CN->getZExtValue() == (int)CN->getZExtValue()) &&
        (!EncodingAlignment ||
         isAligned(*EncodingAlignment, CN->getZExtValue()))) {
      int Addr = (int)CN->getZExtValue();

      // Otherwise, break this down into an LIS + disp.
      Disp = DAG.getTargetConstant((short)Addr, dl, MVT::i32);

      Base = DAG.getTargetConstant((Addr - (signed short)Addr) >> 16, dl,
                                   MVT::i32);
      unsigned Opc = CN->getValueType(0) == MVT::i32 ? PPC::LIS : PPC::LIS8;
      Base = SDValue(DAG.getMachineNode(Opc, dl, CN->getValueType(0), Base), 0);
      return true;
    }
  }

  Disp = DAG.getTargetConstant(0, dl, getPointerTy(DAG.getDataLayout()));
  if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N)) {
    Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
    fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
  } else
    Base = N;
  return true;      // [r+0]
}

/// Similar to the 16-bit case but for instructions that take a 34-bit
/// displacement field (prefixed loads/stores).
bool PPCTargetLowering::SelectAddressRegImm34(SDValue N, SDValue &Disp,
                                              SDValue &Base,
                                              SelectionDAG &DAG) const {
  // Only on 64-bit targets.
  if (N.getValueType() != MVT::i64)
    return false;

  SDLoc dl(N);
  int64_t Imm = 0;

  if (N.getOpcode() == ISD::ADD) {
    if (!isIntS34Immediate(N.getOperand(1), Imm))
      return false;
    Disp = DAG.getTargetConstant(Imm, dl, N.getValueType());
    if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N.getOperand(0)))
      Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
    else
      Base = N.getOperand(0);
    return true;
  }

  if (N.getOpcode() == ISD::OR) {
    if (!isIntS34Immediate(N.getOperand(1), Imm))
      return false;
    // If this is an or of disjoint bitfields, we can codegen this as an add
    // (for better address arithmetic) if the LHS and RHS of the OR are
    // provably disjoint.
    KnownBits LHSKnown = DAG.computeKnownBits(N.getOperand(0));
    if ((LHSKnown.Zero.getZExtValue() | ~(uint64_t)Imm) != ~0ULL)
      return false;
    if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N.getOperand(0)))
      Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
    else
      Base = N.getOperand(0);
    Disp = DAG.getTargetConstant(Imm, dl, N.getValueType());
    return true;
  }

  if (isIntS34Immediate(N, Imm)) { // If the address is a 34-bit const.
    Disp = DAG.getTargetConstant(Imm, dl, N.getValueType());
    Base = DAG.getRegister(PPC::ZERO8, N.getValueType());
    return true;
  }

  return false;
}

/// SelectAddressRegRegOnly - Given the specified addressed, force it to be
/// represented as an indexed [r+r] operation.
bool PPCTargetLowering::SelectAddressRegRegOnly(SDValue N, SDValue &Base,
                                                SDValue &Index,
                                                SelectionDAG &DAG) const {
  // Check to see if we can easily represent this as an [r+r] address.  This
  // will fail if it thinks that the address is more profitably represented as
  // reg+imm, e.g. where imm = 0.
  if (SelectAddressRegReg(N, Base, Index, DAG))
    return true;

  // If the address is the result of an add, we will utilize the fact that the
  // address calculation includes an implicit add.  However, we can reduce
  // register pressure if we do not materialize a constant just for use as the
  // index register.  We only get rid of the add if it is not an add of a
  // value and a 16-bit signed constant and both have a single use.
  int16_t imm = 0;
  if (N.getOpcode() == ISD::ADD &&
      (!isIntS16Immediate(N.getOperand(1), imm) ||
       !N.getOperand(1).hasOneUse() || !N.getOperand(0).hasOneUse())) {
    Base = N.getOperand(0);
    Index = N.getOperand(1);
    return true;
  }

  // Otherwise, do it the hard way, using R0 as the base register.
  Base = DAG.getRegister(Subtarget.isPPC64() ? PPC::ZERO8 : PPC::ZERO,
                         N.getValueType());
  Index = N;
  return true;
}

template <typename Ty> static bool isValidPCRelNode(SDValue N) {
  Ty *PCRelCand = dyn_cast<Ty>(N);
  return PCRelCand && (PPCInstrInfo::hasPCRelFlag(PCRelCand->getTargetFlags()));
}

/// Returns true if this address is a PC Relative address.
/// PC Relative addresses are marked with the flag PPCII::MO_PCREL_FLAG
/// or if the node opcode is PPCISD::MAT_PCREL_ADDR.
bool PPCTargetLowering::SelectAddressPCRel(SDValue N, SDValue &Base) const {
  // This is a materialize PC Relative node. Always select this as PC Relative.
  Base = N;
  if (N.getOpcode() == PPCISD::MAT_PCREL_ADDR)
    return true;
  if (isValidPCRelNode<ConstantPoolSDNode>(N) ||
      isValidPCRelNode<GlobalAddressSDNode>(N) ||
      isValidPCRelNode<JumpTableSDNode>(N) ||
      isValidPCRelNode<BlockAddressSDNode>(N))
    return true;
  return false;
}

/// Returns true if we should use a direct load into vector instruction
/// (such as lxsd or lfd), instead of a load into gpr + direct move sequence.
static bool usePartialVectorLoads(SDNode *N, const PPCSubtarget& ST) {

  // If there are any other uses other than scalar to vector, then we should
  // keep it as a scalar load -> direct move pattern to prevent multiple
  // loads.
  LoadSDNode *LD = dyn_cast<LoadSDNode>(N);
  if (!LD)
    return false;

  EVT MemVT = LD->getMemoryVT();
  if (!MemVT.isSimple())
    return false;
  switch(MemVT.getSimpleVT().SimpleTy) {
  case MVT::i64:
    break;
  case MVT::i32:
    if (!ST.hasP8Vector())
      return false;
    break;
  case MVT::i16:
  case MVT::i8:
    if (!ST.hasP9Vector())
      return false;
    break;
  default:
    return false;
  }

  SDValue LoadedVal(N, 0);
  if (!LoadedVal.hasOneUse())
    return false;

  for (SDNode::use_iterator UI = LD->use_begin(), UE = LD->use_end();
       UI != UE; ++UI)
    if (UI.getUse().get().getResNo() == 0 &&
        UI->getOpcode() != ISD::SCALAR_TO_VECTOR &&
        UI->getOpcode() != PPCISD::SCALAR_TO_VECTOR_PERMUTED)
      return false;

  return true;
}

/// getPreIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if the node's address
/// can be legally represented as pre-indexed load / store address.
bool PPCTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                  SDValue &Offset,
                                                  ISD::MemIndexedMode &AM,
                                                  SelectionDAG &DAG) const {
  if (DisablePPCPreinc) return false;

  bool isLoad = true;
  SDValue Ptr;
  EVT VT;
  Align Alignment;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    Ptr = LD->getBasePtr();
    VT = LD->getMemoryVT();
    Alignment = LD->getAlign();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    Ptr = ST->getBasePtr();
    VT  = ST->getMemoryVT();
    Alignment = ST->getAlign();
    isLoad = false;
  } else
    return false;

  // Do not generate pre-inc forms for specific loads that feed scalar_to_vector
  // instructions because we can fold these into a more efficient instruction
  // instead, (such as LXSD).
  if (isLoad && usePartialVectorLoads(N, Subtarget)) {
    return false;
  }

  // PowerPC doesn't have preinc load/store instructions for vectors
  if (VT.isVector())
    return false;

  if (SelectAddressRegReg(Ptr, Base, Offset, DAG)) {
    // Common code will reject creating a pre-inc form if the base pointer
    // is a frame index, or if N is a store and the base pointer is either
    // the same as or a predecessor of the value being stored.  Check for
    // those situations here, and try with swapped Base/Offset instead.
    bool Swap = false;

    if (isa<FrameIndexSDNode>(Base) || isa<RegisterSDNode>(Base))
      Swap = true;
    else if (!isLoad) {
      SDValue Val = cast<StoreSDNode>(N)->getValue();
      if (Val == Base || Base.getNode()->isPredecessorOf(Val.getNode()))
        Swap = true;
    }

    if (Swap)
      std::swap(Base, Offset);

    AM = ISD::PRE_INC;
    return true;
  }

  // LDU/STU can only handle immediates that are a multiple of 4.
  if (VT != MVT::i64) {
    if (!SelectAddressRegImm(Ptr, Offset, Base, DAG, std::nullopt))
      return false;
  } else {
    // LDU/STU need an address with at least 4-byte alignment.
    if (Alignment < Align(4))
      return false;

    if (!SelectAddressRegImm(Ptr, Offset, Base, DAG, Align(4)))
      return false;
  }

  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    // PPC64 doesn't have lwau, but it does have lwaux.  Reject preinc load of
    // sext i32 to i64 when addr mode is r+i.
    if (LD->getValueType(0) == MVT::i64 && LD->getMemoryVT() == MVT::i32 &&
        LD->getExtensionType() == ISD::SEXTLOAD &&
        isa<ConstantSDNode>(Offset))
      return false;
  }

  AM = ISD::PRE_INC;
  return true;
}

//===----------------------------------------------------------------------===//
//  LowerOperation implementation
//===----------------------------------------------------------------------===//

/// Return true if we should reference labels using a PICBase, set the HiOpFlags
/// and LoOpFlags to the target MO flags.
static void getLabelAccessInfo(bool IsPIC, const PPCSubtarget &Subtarget,
                               unsigned &HiOpFlags, unsigned &LoOpFlags,
                               const GlobalValue *GV = nullptr) {
  HiOpFlags = PPCII::MO_HA;
  LoOpFlags = PPCII::MO_LO;

  // Don't use the pic base if not in PIC relocation model.
  if (IsPIC) {
    HiOpFlags = PPCII::MO_PIC_HA_FLAG;
    LoOpFlags = PPCII::MO_PIC_LO_FLAG;
  }
}

static SDValue LowerLabelRef(SDValue HiPart, SDValue LoPart, bool isPIC,
                             SelectionDAG &DAG) {
  SDLoc DL(HiPart);
  EVT PtrVT = HiPart.getValueType();
  SDValue Zero = DAG.getConstant(0, DL, PtrVT);

  SDValue Hi = DAG.getNode(PPCISD::Hi, DL, PtrVT, HiPart, Zero);
  SDValue Lo = DAG.getNode(PPCISD::Lo, DL, PtrVT, LoPart, Zero);

  // With PIC, the first instruction is actually "GR+hi(&G)".
  if (isPIC)
    Hi = DAG.getNode(ISD::ADD, DL, PtrVT,
                     DAG.getNode(PPCISD::GlobalBaseReg, DL, PtrVT), Hi);

  // Generate non-pic code that has direct accesses to the constant pool.
  // The address of the global is just (hi(&g)+lo(&g)).
  return DAG.getNode(ISD::ADD, DL, PtrVT, Hi, Lo);
}

static void setUsesTOCBasePtr(MachineFunction &MF) {
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setUsesTOCBasePtr();
}

static void setUsesTOCBasePtr(SelectionDAG &DAG) {
  setUsesTOCBasePtr(DAG.getMachineFunction());
}

SDValue PPCTargetLowering::getTOCEntry(SelectionDAG &DAG, const SDLoc &dl,
                                       SDValue GA) const {
  const bool Is64Bit = Subtarget.isPPC64();
  EVT VT = Is64Bit ? MVT::i64 : MVT::i32;
  SDValue Reg = Is64Bit ? DAG.getRegister(PPC::X2, VT)
                        : Subtarget.isAIXABI()
                              ? DAG.getRegister(PPC::R2, VT)
                              : DAG.getNode(PPCISD::GlobalBaseReg, dl, VT);
  SDValue Ops[] = { GA, Reg };
  return DAG.getMemIntrinsicNode(
      PPCISD::TOC_ENTRY, dl, DAG.getVTList(VT, MVT::Other), Ops, VT,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()), std::nullopt,
      MachineMemOperand::MOLoad);
}

SDValue PPCTargetLowering::LowerConstantPool(SDValue Op,
                                             SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
  const Constant *C = CP->getConstVal();

  // 64-bit SVR4 ABI and AIX ABI code are always position-independent.
  // The actual address of the GlobalValue is stored in the TOC.
  if (Subtarget.is64BitELFABI() || Subtarget.isAIXABI()) {
    if (Subtarget.isUsingPCRelativeCalls()) {
      SDLoc DL(CP);
      EVT Ty = getPointerTy(DAG.getDataLayout());
      SDValue ConstPool = DAG.getTargetConstantPool(
          C, Ty, CP->getAlign(), CP->getOffset(), PPCII::MO_PCREL_FLAG);
      return DAG.getNode(PPCISD::MAT_PCREL_ADDR, DL, Ty, ConstPool);
    }
    setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetConstantPool(C, PtrVT, CP->getAlign(), 0);
    return getTOCEntry(DAG, SDLoc(CP), GA);
  }

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag);

  if (IsPIC && Subtarget.isSVR4ABI()) {
    SDValue GA =
        DAG.getTargetConstantPool(C, PtrVT, CP->getAlign(), PPCII::MO_PIC_FLAG);
    return getTOCEntry(DAG, SDLoc(CP), GA);
  }

  SDValue CPIHi =
      DAG.getTargetConstantPool(C, PtrVT, CP->getAlign(), 0, MOHiFlag);
  SDValue CPILo =
      DAG.getTargetConstantPool(C, PtrVT, CP->getAlign(), 0, MOLoFlag);
  return LowerLabelRef(CPIHi, CPILo, IsPIC, DAG);
}

// For 64-bit PowerPC, prefer the more compact relative encodings.
// This trades 32 bits per jump table entry for one or two instructions
// on the jump site.
unsigned PPCTargetLowering::getJumpTableEncoding() const {
  if (isJumpTableRelative())
    return MachineJumpTableInfo::EK_LabelDifference32;

  return TargetLowering::getJumpTableEncoding();
}

bool PPCTargetLowering::isJumpTableRelative() const {
  if (UseAbsoluteJumpTables)
    return false;
  if (Subtarget.isPPC64() || Subtarget.isAIXABI())
    return true;
  return TargetLowering::isJumpTableRelative();
}

SDValue PPCTargetLowering::getPICJumpTableRelocBase(SDValue Table,
                                                    SelectionDAG &DAG) const {
  if (!Subtarget.isPPC64() || Subtarget.isAIXABI())
    return TargetLowering::getPICJumpTableRelocBase(Table, DAG);

  switch (getTargetMachine().getCodeModel()) {
  case CodeModel::Small:
  case CodeModel::Medium:
    return TargetLowering::getPICJumpTableRelocBase(Table, DAG);
  default:
    return DAG.getNode(PPCISD::GlobalBaseReg, SDLoc(),
                       getPointerTy(DAG.getDataLayout()));
  }
}

const MCExpr *
PPCTargetLowering::getPICJumpTableRelocBaseExpr(const MachineFunction *MF,
                                                unsigned JTI,
                                                MCContext &Ctx) const {
  if (!Subtarget.isPPC64() || Subtarget.isAIXABI())
    return TargetLowering::getPICJumpTableRelocBaseExpr(MF, JTI, Ctx);

  switch (getTargetMachine().getCodeModel()) {
  case CodeModel::Small:
  case CodeModel::Medium:
    return TargetLowering::getPICJumpTableRelocBaseExpr(MF, JTI, Ctx);
  default:
    return MCSymbolRefExpr::create(MF->getPICBaseSymbol(), Ctx);
  }
}

SDValue PPCTargetLowering::LowerJumpTable(SDValue Op, SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);

  // isUsingPCRelativeCalls() returns true when PCRelative is enabled
  if (Subtarget.isUsingPCRelativeCalls()) {
    SDLoc DL(JT);
    EVT Ty = getPointerTy(DAG.getDataLayout());
    SDValue GA =
        DAG.getTargetJumpTable(JT->getIndex(), Ty, PPCII::MO_PCREL_FLAG);
    SDValue MatAddr = DAG.getNode(PPCISD::MAT_PCREL_ADDR, DL, Ty, GA);
    return MatAddr;
  }

  // 64-bit SVR4 ABI and AIX ABI code are always position-independent.
  // The actual address of the GlobalValue is stored in the TOC.
  if (Subtarget.is64BitELFABI() || Subtarget.isAIXABI()) {
    setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);
    return getTOCEntry(DAG, SDLoc(JT), GA);
  }

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag);

  if (IsPIC && Subtarget.isSVR4ABI()) {
    SDValue GA = DAG.getTargetJumpTable(JT->getIndex(), PtrVT,
                                        PPCII::MO_PIC_FLAG);
    return getTOCEntry(DAG, SDLoc(GA), GA);
  }

  SDValue JTIHi = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, MOHiFlag);
  SDValue JTILo = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, MOLoFlag);
  return LowerLabelRef(JTIHi, JTILo, IsPIC, DAG);
}

SDValue PPCTargetLowering::LowerBlockAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  BlockAddressSDNode *BASDN = cast<BlockAddressSDNode>(Op);
  const BlockAddress *BA = BASDN->getBlockAddress();

  // isUsingPCRelativeCalls() returns true when PCRelative is enabled
  if (Subtarget.isUsingPCRelativeCalls()) {
    SDLoc DL(BASDN);
    EVT Ty = getPointerTy(DAG.getDataLayout());
    SDValue GA = DAG.getTargetBlockAddress(BA, Ty, BASDN->getOffset(),
                                           PPCII::MO_PCREL_FLAG);
    SDValue MatAddr = DAG.getNode(PPCISD::MAT_PCREL_ADDR, DL, Ty, GA);
    return MatAddr;
  }

  // 64-bit SVR4 ABI and AIX ABI code are always position-independent.
  // The actual BlockAddress is stored in the TOC.
  if (Subtarget.is64BitELFABI() || Subtarget.isAIXABI()) {
    setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetBlockAddress(BA, PtrVT, BASDN->getOffset());
    return getTOCEntry(DAG, SDLoc(BASDN), GA);
  }

  // 32-bit position-independent ELF stores the BlockAddress in the .got.
  if (Subtarget.is32BitELFABI() && isPositionIndependent())
    return getTOCEntry(
        DAG, SDLoc(BASDN),
        DAG.getTargetBlockAddress(BA, PtrVT, BASDN->getOffset()));

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag);
  SDValue TgtBAHi = DAG.getTargetBlockAddress(BA, PtrVT, 0, MOHiFlag);
  SDValue TgtBALo = DAG.getTargetBlockAddress(BA, PtrVT, 0, MOLoFlag);
  return LowerLabelRef(TgtBAHi, TgtBALo, IsPIC, DAG);
}

SDValue PPCTargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  if (Subtarget.isAIXABI())
    return LowerGlobalTLSAddressAIX(Op, DAG);

  return LowerGlobalTLSAddressLinux(Op, DAG);
}

/// updateForAIXShLibTLSModelOpt - Helper to initialize TLS model opt settings,
/// and then apply the update.
static void updateForAIXShLibTLSModelOpt(TLSModel::Model &Model,
                                         SelectionDAG &DAG,
                                         const TargetMachine &TM) {
  // Initialize TLS model opt setting lazily:
  // (1) Use initial-exec for single TLS var references within current function.
  // (2) Use local-dynamic for multiple TLS var references within current
  // function.
  PPCFunctionInfo *FuncInfo =
      DAG.getMachineFunction().getInfo<PPCFunctionInfo>();
  if (!FuncInfo->isAIXFuncTLSModelOptInitDone()) {
    SmallPtrSet<const GlobalValue *, 8> TLSGV;
    // Iterate over all instructions within current function, collect all TLS
    // global variables (global variables taken as the first parameter to
    // Intrinsic::threadlocal_address).
    const Function &Func = DAG.getMachineFunction().getFunction();
    for (Function::const_iterator BI = Func.begin(), BE = Func.end(); BI != BE;
         ++BI)
      for (BasicBlock::const_iterator II = BI->begin(), IE = BI->end();
           II != IE; ++II)
        if (II->getOpcode() == Instruction::Call)
          if (const CallInst *CI = dyn_cast<const CallInst>(&*II))
            if (Function *CF = CI->getCalledFunction())
              if (CF->isDeclaration() &&
                  CF->getIntrinsicID() == Intrinsic::threadlocal_address)
                if (const GlobalValue *GV =
                        dyn_cast<GlobalValue>(II->getOperand(0))) {
                  TLSModel::Model GVModel = TM.getTLSModel(GV);
                  if (GVModel == TLSModel::LocalDynamic)
                    TLSGV.insert(GV);
                }

    unsigned TLSGVCnt = TLSGV.size();
    LLVM_DEBUG(dbgs() << format("LocalDynamic TLSGV count:%d\n", TLSGVCnt));
    if (TLSGVCnt <= PPCAIXTLSModelOptUseIEForLDLimit)
      FuncInfo->setAIXFuncUseTLSIEForLD();
    FuncInfo->setAIXFuncTLSModelOptInitDone();
  }

  if (FuncInfo->isAIXFuncUseTLSIEForLD()) {
    LLVM_DEBUG(
        dbgs() << DAG.getMachineFunction().getName()
               << " function is using the TLS-IE model for TLS-LD access.\n");
    Model = TLSModel::InitialExec;
  }
}

SDValue PPCTargetLowering::LowerGlobalTLSAddressAIX(SDValue Op,
                                                    SelectionDAG &DAG) const {
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);

  if (DAG.getTarget().useEmulatedTLS())
    report_fatal_error("Emulated TLS is not yet supported on AIX");

  SDLoc dl(GA);
  const GlobalValue *GV = GA->getGlobal();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  bool Is64Bit = Subtarget.isPPC64();
  TLSModel::Model Model = getTargetMachine().getTLSModel(GV);

  // Apply update to the TLS model.
  if (Subtarget.hasAIXShLibTLSModelOpt())
    updateForAIXShLibTLSModelOpt(Model, DAG, getTargetMachine());

  bool IsTLSLocalExecModel = Model == TLSModel::LocalExec;

  if (IsTLSLocalExecModel || Model == TLSModel::InitialExec) {
    bool HasAIXSmallLocalExecTLS = Subtarget.hasAIXSmallLocalExecTLS();
    bool HasAIXSmallTLSGlobalAttr = false;
    SDValue VariableOffsetTGA =
        DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, PPCII::MO_TPREL_FLAG);
    SDValue VariableOffset = getTOCEntry(DAG, dl, VariableOffsetTGA);
    SDValue TLSReg;

    if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV))
      if (GVar->hasAttribute("aix-small-tls"))
        HasAIXSmallTLSGlobalAttr = true;

    if (Is64Bit) {
      // For local-exec and initial-exec on AIX (64-bit), the sequence generated
      // involves a load of the variable offset (from the TOC), followed by an
      // add of the loaded variable offset to R13 (the thread pointer).
      // This code sequence looks like:
      //    ld reg1,var[TC](2)
      //    add reg2, reg1, r13     // r13 contains the thread pointer
      TLSReg = DAG.getRegister(PPC::X13, MVT::i64);

      // With the -maix-small-local-exec-tls option, or with the "aix-small-tls"
      // global variable attribute, produce a faster access sequence for
      // local-exec TLS variables where the offset from the TLS base is encoded
      // as an immediate operand.
      //
      // We only utilize the faster local-exec access sequence when the TLS
      // variable has a size within the policy limit. We treat types that are
      // not sized or are empty as being over the policy size limit.
      if ((HasAIXSmallLocalExecTLS || HasAIXSmallTLSGlobalAttr) &&
          IsTLSLocalExecModel) {
        Type *GVType = GV->getValueType();
        if (GVType->isSized() && !GVType->isEmptyTy() &&
            GV->getDataLayout().getTypeAllocSize(GVType) <=
                AIXSmallTlsPolicySizeLimit)
          return DAG.getNode(PPCISD::Lo, dl, PtrVT, VariableOffsetTGA, TLSReg);
      }
    } else {
      // For local-exec and initial-exec on AIX (32-bit), the sequence generated
      // involves loading the variable offset from the TOC, generating a call to
      // .__get_tpointer to get the thread pointer (which will be in R3), and
      // adding the two together:
      //    lwz reg1,var[TC](2)
      //    bla .__get_tpointer
      //    add reg2, reg1, r3
      TLSReg = DAG.getNode(PPCISD::GET_TPOINTER, dl, PtrVT);

      // We do not implement the 32-bit version of the faster access sequence
      // for local-exec that is controlled by the -maix-small-local-exec-tls
      // option, or the "aix-small-tls" global variable attribute.
      if (HasAIXSmallLocalExecTLS || HasAIXSmallTLSGlobalAttr)
        report_fatal_error("The small-local-exec TLS access sequence is "
                           "currently only supported on AIX (64-bit mode).");
    }
    return DAG.getNode(PPCISD::ADD_TLS, dl, PtrVT, TLSReg, VariableOffset);
  }

  if (Model == TLSModel::LocalDynamic) {
    bool HasAIXSmallLocalDynamicTLS = Subtarget.hasAIXSmallLocalDynamicTLS();

    // We do not implement the 32-bit version of the faster access sequence
    // for local-dynamic that is controlled by -maix-small-local-dynamic-tls.
    if (!Is64Bit && HasAIXSmallLocalDynamicTLS)
      report_fatal_error("The small-local-dynamic TLS access sequence is "
                         "currently only supported on AIX (64-bit mode).");

    // For local-dynamic on AIX, we need to generate one TOC entry for each
    // variable offset, and a single module-handle TOC entry for the entire
    // file.

    SDValue VariableOffsetTGA =
        DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, PPCII::MO_TLSLD_FLAG);
    SDValue VariableOffset = getTOCEntry(DAG, dl, VariableOffsetTGA);

    Module *M = DAG.getMachineFunction().getFunction().getParent();
    GlobalVariable *TLSGV =
        dyn_cast_or_null<GlobalVariable>(M->getOrInsertGlobal(
            StringRef("_$TLSML"), PointerType::getUnqual(*DAG.getContext())));
    TLSGV->setThreadLocalMode(GlobalVariable::LocalDynamicTLSModel);
    assert(TLSGV && "Not able to create GV for _$TLSML.");
    SDValue ModuleHandleTGA =
        DAG.getTargetGlobalAddress(TLSGV, dl, PtrVT, 0, PPCII::MO_TLSLDM_FLAG);
    SDValue ModuleHandleTOC = getTOCEntry(DAG, dl, ModuleHandleTGA);
    SDValue ModuleHandle =
        DAG.getNode(PPCISD::TLSLD_AIX, dl, PtrVT, ModuleHandleTOC);

    // With the -maix-small-local-dynamic-tls option, produce a faster access
    // sequence for local-dynamic TLS variables where the offset from the
    // module-handle is encoded as an immediate operand.
    //
    // We only utilize the faster local-dynamic access sequence when the TLS
    // variable has a size within the policy limit. We treat types that are
    // not sized or are empty as being over the policy size limit.
    if (HasAIXSmallLocalDynamicTLS) {
      Type *GVType = GV->getValueType();
      if (GVType->isSized() && !GVType->isEmptyTy() &&
          GV->getDataLayout().getTypeAllocSize(GVType) <=
              AIXSmallTlsPolicySizeLimit)
        return DAG.getNode(PPCISD::Lo, dl, PtrVT, VariableOffsetTGA,
                           ModuleHandle);
    }

    return DAG.getNode(ISD::ADD, dl, PtrVT, ModuleHandle, VariableOffset);
  }

  // If Local- or Initial-exec or Local-dynamic is not possible or specified,
  // all GlobalTLSAddress nodes are lowered using the general-dynamic model. We
  // need to generate two TOC entries, one for the variable offset, one for the
  // region handle. The global address for the TOC entry of the region handle is
  // created with the MO_TLSGDM_FLAG flag and the global address for the TOC
  // entry of the variable offset is created with MO_TLSGD_FLAG.
  SDValue VariableOffsetTGA =
      DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, PPCII::MO_TLSGD_FLAG);
  SDValue RegionHandleTGA =
      DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, PPCII::MO_TLSGDM_FLAG);
  SDValue VariableOffset = getTOCEntry(DAG, dl, VariableOffsetTGA);
  SDValue RegionHandle = getTOCEntry(DAG, dl, RegionHandleTGA);
  return DAG.getNode(PPCISD::TLSGD_AIX, dl, PtrVT, VariableOffset,
                     RegionHandle);
}

SDValue PPCTargetLowering::LowerGlobalTLSAddressLinux(SDValue Op,
                                                      SelectionDAG &DAG) const {
  // FIXME: TLS addresses currently use medium model code sequences,
  // which is the most useful form.  Eventually support for small and
  // large models could be added if users need it, at the cost of
  // additional complexity.
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(GA, DAG);

  SDLoc dl(GA);
  const GlobalValue *GV = GA->getGlobal();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  bool is64bit = Subtarget.isPPC64();
  const Module *M = DAG.getMachineFunction().getFunction().getParent();
  PICLevel::Level picLevel = M->getPICLevel();

  const TargetMachine &TM = getTargetMachine();
  TLSModel::Model Model = TM.getTLSModel(GV);

  if (Model == TLSModel::LocalExec) {
    if (Subtarget.isUsingPCRelativeCalls()) {
      SDValue TLSReg = DAG.getRegister(PPC::X13, MVT::i64);
      SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                               PPCII::MO_TPREL_PCREL_FLAG);
      SDValue MatAddr =
          DAG.getNode(PPCISD::TLS_LOCAL_EXEC_MAT_ADDR, dl, PtrVT, TGA);
      return DAG.getNode(PPCISD::ADD_TLS, dl, PtrVT, TLSReg, MatAddr);
    }

    SDValue TGAHi = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                               PPCII::MO_TPREL_HA);
    SDValue TGALo = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                               PPCII::MO_TPREL_LO);
    SDValue TLSReg = is64bit ? DAG.getRegister(PPC::X13, MVT::i64)
                             : DAG.getRegister(PPC::R2, MVT::i32);

    SDValue Hi = DAG.getNode(PPCISD::Hi, dl, PtrVT, TGAHi, TLSReg);
    return DAG.getNode(PPCISD::Lo, dl, PtrVT, TGALo, Hi);
  }

  if (Model == TLSModel::InitialExec) {
    bool IsPCRel = Subtarget.isUsingPCRelativeCalls();
    SDValue TGA = DAG.getTargetGlobalAddress(
        GV, dl, PtrVT, 0, IsPCRel ? PPCII::MO_GOT_TPREL_PCREL_FLAG : 0);
    SDValue TGATLS = DAG.getTargetGlobalAddress(
        GV, dl, PtrVT, 0, IsPCRel ? PPCII::MO_TLS_PCREL_FLAG : PPCII::MO_TLS);
    SDValue TPOffset;
    if (IsPCRel) {
      SDValue MatPCRel = DAG.getNode(PPCISD::MAT_PCREL_ADDR, dl, PtrVT, TGA);
      TPOffset = DAG.getLoad(MVT::i64, dl, DAG.getEntryNode(), MatPCRel,
                             MachinePointerInfo());
    } else {
      SDValue GOTPtr;
      if (is64bit) {
        setUsesTOCBasePtr(DAG);
        SDValue GOTReg = DAG.getRegister(PPC::X2, MVT::i64);
        GOTPtr =
            DAG.getNode(PPCISD::ADDIS_GOT_TPREL_HA, dl, PtrVT, GOTReg, TGA);
      } else {
        if (!TM.isPositionIndependent())
          GOTPtr = DAG.getNode(PPCISD::PPC32_GOT, dl, PtrVT);
        else if (picLevel == PICLevel::SmallPIC)
          GOTPtr = DAG.getNode(PPCISD::GlobalBaseReg, dl, PtrVT);
        else
          GOTPtr = DAG.getNode(PPCISD::PPC32_PICGOT, dl, PtrVT);
      }
      TPOffset = DAG.getNode(PPCISD::LD_GOT_TPREL_L, dl, PtrVT, TGA, GOTPtr);
    }
    return DAG.getNode(PPCISD::ADD_TLS, dl, PtrVT, TPOffset, TGATLS);
  }

  if (Model == TLSModel::GeneralDynamic) {
    if (Subtarget.isUsingPCRelativeCalls()) {
      SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                               PPCII::MO_GOT_TLSGD_PCREL_FLAG);
      return DAG.getNode(PPCISD::TLS_DYNAMIC_MAT_PCREL_ADDR, dl, PtrVT, TGA);
    }

    SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, 0);
    SDValue GOTPtr;
    if (is64bit) {
      setUsesTOCBasePtr(DAG);
      SDValue GOTReg = DAG.getRegister(PPC::X2, MVT::i64);
      GOTPtr = DAG.getNode(PPCISD::ADDIS_TLSGD_HA, dl, PtrVT,
                                   GOTReg, TGA);
    } else {
      if (picLevel == PICLevel::SmallPIC)
        GOTPtr = DAG.getNode(PPCISD::GlobalBaseReg, dl, PtrVT);
      else
        GOTPtr = DAG.getNode(PPCISD::PPC32_PICGOT, dl, PtrVT);
    }
    return DAG.getNode(PPCISD::ADDI_TLSGD_L_ADDR, dl, PtrVT,
                       GOTPtr, TGA, TGA);
  }

  if (Model == TLSModel::LocalDynamic) {
    if (Subtarget.isUsingPCRelativeCalls()) {
      SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                               PPCII::MO_GOT_TLSLD_PCREL_FLAG);
      SDValue MatPCRel =
          DAG.getNode(PPCISD::TLS_DYNAMIC_MAT_PCREL_ADDR, dl, PtrVT, TGA);
      return DAG.getNode(PPCISD::PADDI_DTPREL, dl, PtrVT, MatPCRel, TGA);
    }

    SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, 0);
    SDValue GOTPtr;
    if (is64bit) {
      setUsesTOCBasePtr(DAG);
      SDValue GOTReg = DAG.getRegister(PPC::X2, MVT::i64);
      GOTPtr = DAG.getNode(PPCISD::ADDIS_TLSLD_HA, dl, PtrVT,
                           GOTReg, TGA);
    } else {
      if (picLevel == PICLevel::SmallPIC)
        GOTPtr = DAG.getNode(PPCISD::GlobalBaseReg, dl, PtrVT);
      else
        GOTPtr = DAG.getNode(PPCISD::PPC32_PICGOT, dl, PtrVT);
    }
    SDValue TLSAddr = DAG.getNode(PPCISD::ADDI_TLSLD_L_ADDR, dl,
                                  PtrVT, GOTPtr, TGA, TGA);
    SDValue DtvOffsetHi = DAG.getNode(PPCISD::ADDIS_DTPREL_HA, dl,
                                      PtrVT, TLSAddr, TGA);
    return DAG.getNode(PPCISD::ADDI_DTPREL_L, dl, PtrVT, DtvOffsetHi, TGA);
  }

  llvm_unreachable("Unknown TLS model!");
}

SDValue PPCTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  GlobalAddressSDNode *GSDN = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(GSDN);
  const GlobalValue *GV = GSDN->getGlobal();

  // 64-bit SVR4 ABI & AIX ABI code is always position-independent.
  // The actual address of the GlobalValue is stored in the TOC.
  if (Subtarget.is64BitELFABI() || Subtarget.isAIXABI()) {
    if (Subtarget.isUsingPCRelativeCalls()) {
      EVT Ty = getPointerTy(DAG.getDataLayout());
      if (isAccessedAsGotIndirect(Op)) {
        SDValue GA = DAG.getTargetGlobalAddress(GV, DL, Ty, GSDN->getOffset(),
                                                PPCII::MO_GOT_PCREL_FLAG);
        SDValue MatPCRel = DAG.getNode(PPCISD::MAT_PCREL_ADDR, DL, Ty, GA);
        SDValue Load = DAG.getLoad(MVT::i64, DL, DAG.getEntryNode(), MatPCRel,
                                   MachinePointerInfo());
        return Load;
      } else {
        SDValue GA = DAG.getTargetGlobalAddress(GV, DL, Ty, GSDN->getOffset(),
                                                PPCII::MO_PCREL_FLAG);
        return DAG.getNode(PPCISD::MAT_PCREL_ADDR, DL, Ty, GA);
      }
    }
    setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetGlobalAddress(GV, DL, PtrVT, GSDN->getOffset());
    return getTOCEntry(DAG, DL, GA);
  }

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag, GV);

  if (IsPIC && Subtarget.isSVR4ABI()) {
    SDValue GA = DAG.getTargetGlobalAddress(GV, DL, PtrVT,
                                            GSDN->getOffset(),
                                            PPCII::MO_PIC_FLAG);
    return getTOCEntry(DAG, DL, GA);
  }

  SDValue GAHi =
    DAG.getTargetGlobalAddress(GV, DL, PtrVT, GSDN->getOffset(), MOHiFlag);
  SDValue GALo =
    DAG.getTargetGlobalAddress(GV, DL, PtrVT, GSDN->getOffset(), MOLoFlag);

  return LowerLabelRef(GAHi, GALo, IsPIC, DAG);
}

SDValue PPCTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  bool IsStrict = Op->isStrictFPOpcode();
  ISD::CondCode CC =
      cast<CondCodeSDNode>(Op.getOperand(IsStrict ? 3 : 2))->get();
  SDValue LHS = Op.getOperand(IsStrict ? 1 : 0);
  SDValue RHS = Op.getOperand(IsStrict ? 2 : 1);
  SDValue Chain = IsStrict ? Op.getOperand(0) : SDValue();
  EVT LHSVT = LHS.getValueType();
  SDLoc dl(Op);

  // Soften the setcc with libcall if it is fp128.
  if (LHSVT == MVT::f128) {
    assert(!Subtarget.hasP9Vector() &&
           "SETCC for f128 is already legal under Power9!");
    softenSetCCOperands(DAG, LHSVT, LHS, RHS, CC, dl, LHS, RHS, Chain,
                        Op->getOpcode() == ISD::STRICT_FSETCCS);
    if (RHS.getNode())
      LHS = DAG.getNode(ISD::SETCC, dl, Op.getValueType(), LHS, RHS,
                        DAG.getCondCode(CC));
    if (IsStrict)
      return DAG.getMergeValues({LHS, Chain}, dl);
    return LHS;
  }

  assert(!IsStrict && "Don't know how to handle STRICT_FSETCC!");

  if (Op.getValueType() == MVT::v2i64) {
    // When the operands themselves are v2i64 values, we need to do something
    // special because VSX has no underlying comparison operations for these.
    if (LHS.getValueType() == MVT::v2i64) {
      // Equality can be handled by casting to the legal type for Altivec
      // comparisons, everything else needs to be expanded.
      if (CC != ISD::SETEQ && CC != ISD::SETNE)
        return SDValue();
      SDValue SetCC32 = DAG.getSetCC(
          dl, MVT::v4i32, DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, LHS),
          DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, RHS), CC);
      int ShuffV[] = {1, 0, 3, 2};
      SDValue Shuff =
          DAG.getVectorShuffle(MVT::v4i32, dl, SetCC32, SetCC32, ShuffV);
      return DAG.getBitcast(MVT::v2i64,
                            DAG.getNode(CC == ISD::SETEQ ? ISD::AND : ISD::OR,
                                        dl, MVT::v4i32, Shuff, SetCC32));
    }

    // We handle most of these in the usual way.
    return Op;
  }

  // If we're comparing for equality to zero, expose the fact that this is
  // implemented as a ctlz/srl pair on ppc, so that the dag combiner can
  // fold the new nodes.
  if (SDValue V = lowerCmpEqZeroToCtlzSrl(Op, DAG))
    return V;

  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS)) {
    // Leave comparisons against 0 and -1 alone for now, since they're usually
    // optimized.  FIXME: revisit this when we can custom lower all setcc
    // optimizations.
    if (C->isAllOnes() || C->isZero())
      return SDValue();
  }

  // If we have an integer seteq/setne, turn it into a compare against zero
  // by xor'ing the rhs with the lhs, which is faster than setting a
  // condition register, reading it back out, and masking the correct bit.  The
  // normal approach here uses sub to do this instead of xor.  Using xor exposes
  // the result to other bit-twiddling opportunities.
  if (LHSVT.isInteger() && (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    EVT VT = Op.getValueType();
    SDValue Sub = DAG.getNode(ISD::XOR, dl, LHSVT, LHS, RHS);
    return DAG.getSetCC(dl, VT, Sub, DAG.getConstant(0, dl, LHSVT), CC);
  }
  return SDValue();
}

SDValue PPCTargetLowering::LowerVAARG(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  EVT VT = Node->getValueType(0);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue InChain = Node->getOperand(0);
  SDValue VAListPtr = Node->getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
  SDLoc dl(Node);

  assert(!Subtarget.isPPC64() && "LowerVAARG is PPC32 only");

  // gpr_index
  SDValue GprIndex = DAG.getExtLoad(ISD::ZEXTLOAD, dl, MVT::i32, InChain,
                                    VAListPtr, MachinePointerInfo(SV), MVT::i8);
  InChain = GprIndex.getValue(1);

  if (VT == MVT::i64) {
    // Check if GprIndex is even
    SDValue GprAnd = DAG.getNode(ISD::AND, dl, MVT::i32, GprIndex,
                                 DAG.getConstant(1, dl, MVT::i32));
    SDValue CC64 = DAG.getSetCC(dl, MVT::i32, GprAnd,
                                DAG.getConstant(0, dl, MVT::i32), ISD::SETNE);
    SDValue GprIndexPlusOne = DAG.getNode(ISD::ADD, dl, MVT::i32, GprIndex,
                                          DAG.getConstant(1, dl, MVT::i32));
    // Align GprIndex to be even if it isn't
    GprIndex = DAG.getNode(ISD::SELECT, dl, MVT::i32, CC64, GprIndexPlusOne,
                           GprIndex);
  }

  // fpr index is 1 byte after gpr
  SDValue FprPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAListPtr,
                               DAG.getConstant(1, dl, MVT::i32));

  // fpr
  SDValue FprIndex = DAG.getExtLoad(ISD::ZEXTLOAD, dl, MVT::i32, InChain,
                                    FprPtr, MachinePointerInfo(SV), MVT::i8);
  InChain = FprIndex.getValue(1);

  SDValue RegSaveAreaPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAListPtr,
                                       DAG.getConstant(8, dl, MVT::i32));

  SDValue OverflowAreaPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAListPtr,
                                        DAG.getConstant(4, dl, MVT::i32));

  // areas
  SDValue OverflowArea =
      DAG.getLoad(MVT::i32, dl, InChain, OverflowAreaPtr, MachinePointerInfo());
  InChain = OverflowArea.getValue(1);

  SDValue RegSaveArea =
      DAG.getLoad(MVT::i32, dl, InChain, RegSaveAreaPtr, MachinePointerInfo());
  InChain = RegSaveArea.getValue(1);

  // select overflow_area if index > 8
  SDValue CC = DAG.getSetCC(dl, MVT::i32, VT.isInteger() ? GprIndex : FprIndex,
                            DAG.getConstant(8, dl, MVT::i32), ISD::SETLT);

  // adjustment constant gpr_index * 4/8
  SDValue RegConstant = DAG.getNode(ISD::MUL, dl, MVT::i32,
                                    VT.isInteger() ? GprIndex : FprIndex,
                                    DAG.getConstant(VT.isInteger() ? 4 : 8, dl,
                                                    MVT::i32));

  // OurReg = RegSaveArea + RegConstant
  SDValue OurReg = DAG.getNode(ISD::ADD, dl, PtrVT, RegSaveArea,
                               RegConstant);

  // Floating types are 32 bytes into RegSaveArea
  if (VT.isFloatingPoint())
    OurReg = DAG.getNode(ISD::ADD, dl, PtrVT, OurReg,
                         DAG.getConstant(32, dl, MVT::i32));

  // increase {f,g}pr_index by 1 (or 2 if VT is i64)
  SDValue IndexPlus1 = DAG.getNode(ISD::ADD, dl, MVT::i32,
                                   VT.isInteger() ? GprIndex : FprIndex,
                                   DAG.getConstant(VT == MVT::i64 ? 2 : 1, dl,
                                                   MVT::i32));

  InChain = DAG.getTruncStore(InChain, dl, IndexPlus1,
                              VT.isInteger() ? VAListPtr : FprPtr,
                              MachinePointerInfo(SV), MVT::i8);

  // determine if we should load from reg_save_area or overflow_area
  SDValue Result = DAG.getNode(ISD::SELECT, dl, PtrVT, CC, OurReg, OverflowArea);

  // increase overflow_area by 4/8 if gpr/fpr > 8
  SDValue OverflowAreaPlusN = DAG.getNode(ISD::ADD, dl, PtrVT, OverflowArea,
                                          DAG.getConstant(VT.isInteger() ? 4 : 8,
                                          dl, MVT::i32));

  OverflowArea = DAG.getNode(ISD::SELECT, dl, MVT::i32, CC, OverflowArea,
                             OverflowAreaPlusN);

  InChain = DAG.getTruncStore(InChain, dl, OverflowArea, OverflowAreaPtr,
                              MachinePointerInfo(), MVT::i32);

  return DAG.getLoad(VT, dl, InChain, Result, MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerVACOPY(SDValue Op, SelectionDAG &DAG) const {
  assert(!Subtarget.isPPC64() && "LowerVACOPY is PPC32 only");

  // We have to copy the entire va_list struct:
  // 2*sizeof(char) + 2 Byte alignment + 2*sizeof(char*) = 12 Byte
  return DAG.getMemcpy(Op.getOperand(0), Op, Op.getOperand(1), Op.getOperand(2),
                       DAG.getConstant(12, SDLoc(Op), MVT::i32), Align(8),
                       false, true, /*CI=*/nullptr, std::nullopt,
                       MachinePointerInfo(), MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerADJUST_TRAMPOLINE(SDValue Op,
                                                  SelectionDAG &DAG) const {
  if (Subtarget.isAIXABI())
    report_fatal_error("ADJUST_TRAMPOLINE operation is not supported on AIX.");

  return Op.getOperand(0);
}

SDValue PPCTargetLowering::LowerINLINEASM(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  PPCFunctionInfo &MFI = *MF.getInfo<PPCFunctionInfo>();

  assert((Op.getOpcode() == ISD::INLINEASM ||
          Op.getOpcode() == ISD::INLINEASM_BR) &&
         "Expecting Inline ASM node.");

  // If an LR store is already known to be required then there is not point in
  // checking this ASM as well.
  if (MFI.isLRStoreRequired())
    return Op;

  // Inline ASM nodes have an optional last operand that is an incoming Flag of
  // type MVT::Glue. We want to ignore this last operand if that is the case.
  unsigned NumOps = Op.getNumOperands();
  if (Op.getOperand(NumOps - 1).getValueType() == MVT::Glue)
    --NumOps;

  // Check all operands that may contain the LR.
  for (unsigned i = InlineAsm::Op_FirstOperand; i != NumOps;) {
    const InlineAsm::Flag Flags(Op.getConstantOperandVal(i));
    unsigned NumVals = Flags.getNumOperandRegisters();
    ++i; // Skip the ID value.

    switch (Flags.getKind()) {
    default:
      llvm_unreachable("Bad flags!");
    case InlineAsm::Kind::RegUse:
    case InlineAsm::Kind::Imm:
    case InlineAsm::Kind::Mem:
      i += NumVals;
      break;
    case InlineAsm::Kind::Clobber:
    case InlineAsm::Kind::RegDef:
    case InlineAsm::Kind::RegDefEarlyClobber: {
      for (; NumVals; --NumVals, ++i) {
        Register Reg = cast<RegisterSDNode>(Op.getOperand(i))->getReg();
        if (Reg != PPC::LR && Reg != PPC::LR8)
          continue;
        MFI.setLRStoreRequired();
        return Op;
      }
      break;
    }
    }
  }

  return Op;
}

SDValue PPCTargetLowering::LowerINIT_TRAMPOLINE(SDValue Op,
                                                SelectionDAG &DAG) const {
  if (Subtarget.isAIXABI())
    report_fatal_error("INIT_TRAMPOLINE operation is not supported on AIX.");

  SDValue Chain = Op.getOperand(0);
  SDValue Trmp = Op.getOperand(1); // trampoline
  SDValue FPtr = Op.getOperand(2); // nested function
  SDValue Nest = Op.getOperand(3); // 'nest' parameter value
  SDLoc dl(Op);

  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  bool isPPC64 = (PtrVT == MVT::i64);
  Type *IntPtrTy = DAG.getDataLayout().getIntPtrType(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Entry.Ty = IntPtrTy;
  Entry.Node = Trmp; Args.push_back(Entry);

  // TrampSize == (isPPC64 ? 48 : 40);
  Entry.Node = DAG.getConstant(isPPC64 ? 48 : 40, dl,
                               isPPC64 ? MVT::i64 : MVT::i32);
  Args.push_back(Entry);

  Entry.Node = FPtr; Args.push_back(Entry);
  Entry.Node = Nest; Args.push_back(Entry);

  // Lower to a call to __trampoline_setup(Trmp, TrampSize, FPtr, ctx_reg)
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(Chain).setLibCallee(
      CallingConv::C, Type::getVoidTy(*DAG.getContext()),
      DAG.getExternalSymbol("__trampoline_setup", PtrVT), std::move(Args));

  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);
  return CallResult.second;
}

SDValue PPCTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  SDLoc dl(Op);

  if (Subtarget.isPPC64() || Subtarget.isAIXABI()) {
    // vastart just stores the address of the VarArgsFrameIndex slot into the
    // memory location argument.
    SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
    const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
    return DAG.getStore(Op.getOperand(0), dl, FR, Op.getOperand(1),
                        MachinePointerInfo(SV));
  }

  // For the 32-bit SVR4 ABI we follow the layout of the va_list struct.
  // We suppose the given va_list is already allocated.
  //
  // typedef struct {
  //  char gpr;     /* index into the array of 8 GPRs
  //                 * stored in the register save area
  //                 * gpr=0 corresponds to r3,
  //                 * gpr=1 to r4, etc.
  //                 */
  //  char fpr;     /* index into the array of 8 FPRs
  //                 * stored in the register save area
  //                 * fpr=0 corresponds to f1,
  //                 * fpr=1 to f2, etc.
  //                 */
  //  char *overflow_arg_area;
  //                /* location on stack that holds
  //                 * the next overflow argument
  //                 */
  //  char *reg_save_area;
  //               /* where r3:r10 and f1:f8 (if saved)
  //                * are stored
  //                */
  // } va_list[1];

  SDValue ArgGPR = DAG.getConstant(FuncInfo->getVarArgsNumGPR(), dl, MVT::i32);
  SDValue ArgFPR = DAG.getConstant(FuncInfo->getVarArgsNumFPR(), dl, MVT::i32);
  SDValue StackOffsetFI = DAG.getFrameIndex(FuncInfo->getVarArgsStackOffset(),
                                            PtrVT);
  SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                 PtrVT);

  uint64_t FrameOffset = PtrVT.getSizeInBits()/8;
  SDValue ConstFrameOffset = DAG.getConstant(FrameOffset, dl, PtrVT);

  uint64_t StackOffset = PtrVT.getSizeInBits()/8 - 1;
  SDValue ConstStackOffset = DAG.getConstant(StackOffset, dl, PtrVT);

  uint64_t FPROffset = 1;
  SDValue ConstFPROffset = DAG.getConstant(FPROffset, dl, PtrVT);

  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

  // Store first byte : number of int regs
  SDValue firstStore =
      DAG.getTruncStore(Op.getOperand(0), dl, ArgGPR, Op.getOperand(1),
                        MachinePointerInfo(SV), MVT::i8);
  uint64_t nextOffset = FPROffset;
  SDValue nextPtr = DAG.getNode(ISD::ADD, dl, PtrVT, Op.getOperand(1),
                                  ConstFPROffset);

  // Store second byte : number of float regs
  SDValue secondStore =
      DAG.getTruncStore(firstStore, dl, ArgFPR, nextPtr,
                        MachinePointerInfo(SV, nextOffset), MVT::i8);
  nextOffset += StackOffset;
  nextPtr = DAG.getNode(ISD::ADD, dl, PtrVT, nextPtr, ConstStackOffset);

  // Store second word : arguments given on stack
  SDValue thirdStore = DAG.getStore(secondStore, dl, StackOffsetFI, nextPtr,
                                    MachinePointerInfo(SV, nextOffset));
  nextOffset += FrameOffset;
  nextPtr = DAG.getNode(ISD::ADD, dl, PtrVT, nextPtr, ConstFrameOffset);

  // Store third word : arguments given in registers
  return DAG.getStore(thirdStore, dl, FR, nextPtr,
                      MachinePointerInfo(SV, nextOffset));
}

/// FPR - The set of FP registers that should be allocated for arguments
/// on Darwin and AIX.
static const MCPhysReg FPR[] = {PPC::F1,  PPC::F2,  PPC::F3, PPC::F4, PPC::F5,
                                PPC::F6,  PPC::F7,  PPC::F8, PPC::F9, PPC::F10,
                                PPC::F11, PPC::F12, PPC::F13};

/// CalculateStackSlotSize - Calculates the size reserved for this argument on
/// the stack.
static unsigned CalculateStackSlotSize(EVT ArgVT, ISD::ArgFlagsTy Flags,
                                       unsigned PtrByteSize) {
  unsigned ArgSize = ArgVT.getStoreSize();
  if (Flags.isByVal())
    ArgSize = Flags.getByValSize();

  // Round up to multiples of the pointer size, except for array members,
  // which are always packed.
  if (!Flags.isInConsecutiveRegs())
    ArgSize = ((ArgSize + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;

  return ArgSize;
}

/// CalculateStackSlotAlignment - Calculates the alignment of this argument
/// on the stack.
static Align CalculateStackSlotAlignment(EVT ArgVT, EVT OrigVT,
                                         ISD::ArgFlagsTy Flags,
                                         unsigned PtrByteSize) {
  Align Alignment(PtrByteSize);

  // Altivec parameters are padded to a 16 byte boundary.
  if (ArgVT == MVT::v4f32 || ArgVT == MVT::v4i32 ||
      ArgVT == MVT::v8i16 || ArgVT == MVT::v16i8 ||
      ArgVT == MVT::v2f64 || ArgVT == MVT::v2i64 ||
      ArgVT == MVT::v1i128 || ArgVT == MVT::f128)
    Alignment = Align(16);

  // ByVal parameters are aligned as requested.
  if (Flags.isByVal()) {
    auto BVAlign = Flags.getNonZeroByValAlign();
    if (BVAlign > PtrByteSize) {
      if (BVAlign.value() % PtrByteSize != 0)
        llvm_unreachable(
            "ByVal alignment is not a multiple of the pointer size");

      Alignment = BVAlign;
    }
  }

  // Array members are always packed to their original alignment.
  if (Flags.isInConsecutiveRegs()) {
    // If the array member was split into multiple registers, the first
    // needs to be aligned to the size of the full type.  (Except for
    // ppcf128, which is only aligned as its f64 components.)
    if (Flags.isSplit() && OrigVT != MVT::ppcf128)
      Alignment = Align(OrigVT.getStoreSize());
    else
      Alignment = Align(ArgVT.getStoreSize());
  }

  return Alignment;
}

/// CalculateStackSlotUsed - Return whether this argument will use its
/// stack slot (instead of being passed in registers).  ArgOffset,
/// AvailableFPRs, and AvailableVRs must hold the current argument
/// position, and will be updated to account for this argument.
static bool CalculateStackSlotUsed(EVT ArgVT, EVT OrigVT, ISD::ArgFlagsTy Flags,
                                   unsigned PtrByteSize, unsigned LinkageSize,
                                   unsigned ParamAreaSize, unsigned &ArgOffset,
                                   unsigned &AvailableFPRs,
                                   unsigned &AvailableVRs) {
  bool UseMemory = false;

  // Respect alignment of argument on the stack.
  Align Alignment =
      CalculateStackSlotAlignment(ArgVT, OrigVT, Flags, PtrByteSize);
  ArgOffset = alignTo(ArgOffset, Alignment);
  // If there's no space left in the argument save area, we must
  // use memory (this check also catches zero-sized arguments).
  if (ArgOffset >= LinkageSize + ParamAreaSize)
    UseMemory = true;

  // Allocate argument on the stack.
  ArgOffset += CalculateStackSlotSize(ArgVT, Flags, PtrByteSize);
  if (Flags.isInConsecutiveRegsLast())
    ArgOffset = ((ArgOffset + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
  // If we overran the argument save area, we must use memory
  // (this check catches arguments passed partially in memory)
  if (ArgOffset > LinkageSize + ParamAreaSize)
    UseMemory = true;

  // However, if the argument is actually passed in an FPR or a VR,
  // we don't use memory after all.
  if (!Flags.isByVal()) {
    if (ArgVT == MVT::f32 || ArgVT == MVT::f64)
      if (AvailableFPRs > 0) {
        --AvailableFPRs;
        return false;
      }
    if (ArgVT == MVT::v4f32 || ArgVT == MVT::v4i32 ||
        ArgVT == MVT::v8i16 || ArgVT == MVT::v16i8 ||
        ArgVT == MVT::v2f64 || ArgVT == MVT::v2i64 ||
        ArgVT == MVT::v1i128 || ArgVT == MVT::f128)
      if (AvailableVRs > 0) {
        --AvailableVRs;
        return false;
      }
  }

  return UseMemory;
}

/// EnsureStackAlignment - Round stack frame size up from NumBytes to
/// ensure minimum alignment required for target.
static unsigned EnsureStackAlignment(const PPCFrameLowering *Lowering,
                                     unsigned NumBytes) {
  return alignTo(NumBytes, Lowering->getStackAlign());
}

SDValue PPCTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (Subtarget.isAIXABI())
    return LowerFormalArguments_AIX(Chain, CallConv, isVarArg, Ins, dl, DAG,
                                    InVals);
  if (Subtarget.is64BitELFABI())
    return LowerFormalArguments_64SVR4(Chain, CallConv, isVarArg, Ins, dl, DAG,
                                       InVals);
  assert(Subtarget.is32BitELFABI());
  return LowerFormalArguments_32SVR4(Chain, CallConv, isVarArg, Ins, dl, DAG,
                                     InVals);
}

SDValue PPCTargetLowering::LowerFormalArguments_32SVR4(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  // 32-bit SVR4 ABI Stack Frame Layout:
  //              +-----------------------------------+
  //        +-->  |            Back chain             |
  //        |     +-----------------------------------+
  //        |     | Floating-point register save area |
  //        |     +-----------------------------------+
  //        |     |    General register save area     |
  //        |     +-----------------------------------+
  //        |     |          CR save word             |
  //        |     +-----------------------------------+
  //        |     |         VRSAVE save word          |
  //        |     +-----------------------------------+
  //        |     |         Alignment padding         |
  //        |     +-----------------------------------+
  //        |     |     Vector register save area     |
  //        |     +-----------------------------------+
  //        |     |       Local variable space        |
  //        |     +-----------------------------------+
  //        |     |        Parameter list area        |
  //        |     +-----------------------------------+
  //        |     |           LR save word            |
  //        |     +-----------------------------------+
  // SP-->  +---  |            Back chain             |
  //              +-----------------------------------+
  //
  // Specifications:
  //   System V Application Binary Interface PowerPC Processor Supplement
  //   AltiVec Technology Programming Interface Manual

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();

  EVT PtrVT = getPointerTy(MF.getDataLayout());
  // Potential tail calls could cause overwriting of argument stack slots.
  bool isImmutable = !(getTargetMachine().Options.GuaranteedTailCallOpt &&
                       (CallConv == CallingConv::Fast));
  const Align PtrAlign(4);

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  PPCCCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());

  // Reserve space for the linkage area on the stack.
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  CCInfo.AllocateStack(LinkageSize, PtrAlign);
  if (useSoftFloat())
    CCInfo.PreAnalyzeFormalArguments(Ins);

  CCInfo.AnalyzeFormalArguments(Ins, CC_PPC32_SVR4);
  CCInfo.clearWasPPCF128();

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    // Arguments stored in registers.
    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC;
      EVT ValVT = VA.getValVT();

      switch (ValVT.getSimpleVT().SimpleTy) {
        default:
          llvm_unreachable("ValVT not supported by formal arguments Lowering");
        case MVT::i1:
        case MVT::i32:
          RC = &PPC::GPRCRegClass;
          break;
        case MVT::f32:
          if (Subtarget.hasP8Vector())
            RC = &PPC::VSSRCRegClass;
          else if (Subtarget.hasSPE())
            RC = &PPC::GPRCRegClass;
          else
            RC = &PPC::F4RCRegClass;
          break;
        case MVT::f64:
          if (Subtarget.hasVSX())
            RC = &PPC::VSFRCRegClass;
          else if (Subtarget.hasSPE())
            // SPE passes doubles in GPR pairs.
            RC = &PPC::GPRCRegClass;
          else
            RC = &PPC::F8RCRegClass;
          break;
        case MVT::v16i8:
        case MVT::v8i16:
        case MVT::v4i32:
          RC = &PPC::VRRCRegClass;
          break;
        case MVT::v4f32:
          RC = &PPC::VRRCRegClass;
          break;
        case MVT::v2f64:
        case MVT::v2i64:
          RC = &PPC::VRRCRegClass;
          break;
      }

      SDValue ArgValue;
      // Transform the arguments stored in physical registers into
      // virtual ones.
      if (VA.getLocVT() == MVT::f64 && Subtarget.hasSPE()) {
        assert(i + 1 < e && "No second half of double precision argument");
        Register RegLo = MF.addLiveIn(VA.getLocReg(), RC);
        Register RegHi = MF.addLiveIn(ArgLocs[++i].getLocReg(), RC);
        SDValue ArgValueLo = DAG.getCopyFromReg(Chain, dl, RegLo, MVT::i32);
        SDValue ArgValueHi = DAG.getCopyFromReg(Chain, dl, RegHi, MVT::i32);
        if (!Subtarget.isLittleEndian())
          std::swap (ArgValueLo, ArgValueHi);
        ArgValue = DAG.getNode(PPCISD::BUILD_SPE64, dl, MVT::f64, ArgValueLo,
                               ArgValueHi);
      } else {
        Register Reg = MF.addLiveIn(VA.getLocReg(), RC);
        ArgValue = DAG.getCopyFromReg(Chain, dl, Reg,
                                      ValVT == MVT::i1 ? MVT::i32 : ValVT);
        if (ValVT == MVT::i1)
          ArgValue = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, ArgValue);
      }

      InVals.push_back(ArgValue);
    } else {
      // Argument stored in memory.
      assert(VA.isMemLoc());

      // Get the extended size of the argument type in stack
      unsigned ArgSize = VA.getLocVT().getStoreSize();
      // Get the actual size of the argument type
      unsigned ObjSize = VA.getValVT().getStoreSize();
      unsigned ArgOffset = VA.getLocMemOffset();
      // Stack objects in PPC32 are right justified.
      ArgOffset += ArgSize - ObjSize;
      int FI = MFI.CreateFixedObject(ArgSize, ArgOffset, isImmutable);

      // Create load nodes to retrieve arguments from the stack.
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      InVals.push_back(
          DAG.getLoad(VA.getValVT(), dl, Chain, FIN, MachinePointerInfo()));
    }
  }

  // Assign locations to all of the incoming aggregate by value arguments.
  // Aggregates passed by value are stored in the local variable space of the
  // caller's stack frame, right above the parameter list area.
  SmallVector<CCValAssign, 16> ByValArgLocs;
  CCState CCByValInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                      ByValArgLocs, *DAG.getContext());

  // Reserve stack space for the allocations in CCInfo.
  CCByValInfo.AllocateStack(CCInfo.getStackSize(), PtrAlign);

  CCByValInfo.AnalyzeFormalArguments(Ins, CC_PPC32_SVR4_ByVal);

  // Area that is at least reserved in the caller of this function.
  unsigned MinReservedArea = CCByValInfo.getStackSize();
  MinReservedArea = std::max(MinReservedArea, LinkageSize);

  // Set the size that is at least reserved in caller of this function.  Tail
  // call optimized function's reserved stack space needs to be aligned so that
  // taking the difference between two stack areas will result in an aligned
  // stack.
  MinReservedArea =
      EnsureStackAlignment(Subtarget.getFrameLowering(), MinReservedArea);
  FuncInfo->setMinReservedArea(MinReservedArea);

  SmallVector<SDValue, 8> MemOps;

  // If the function takes variable number of arguments, make a frame index for
  // the start of the first vararg value... for expansion of llvm.va_start.
  if (isVarArg) {
    static const MCPhysReg GPArgRegs[] = {
      PPC::R3, PPC::R4, PPC::R5, PPC::R6,
      PPC::R7, PPC::R8, PPC::R9, PPC::R10,
    };
    const unsigned NumGPArgRegs = std::size(GPArgRegs);

    static const MCPhysReg FPArgRegs[] = {
      PPC::F1, PPC::F2, PPC::F3, PPC::F4, PPC::F5, PPC::F6, PPC::F7,
      PPC::F8
    };
    unsigned NumFPArgRegs = std::size(FPArgRegs);

    if (useSoftFloat() || hasSPE())
       NumFPArgRegs = 0;

    FuncInfo->setVarArgsNumGPR(CCInfo.getFirstUnallocated(GPArgRegs));
    FuncInfo->setVarArgsNumFPR(CCInfo.getFirstUnallocated(FPArgRegs));

    // Make room for NumGPArgRegs and NumFPArgRegs.
    int Depth = NumGPArgRegs * PtrVT.getSizeInBits()/8 +
                NumFPArgRegs * MVT(MVT::f64).getSizeInBits()/8;

    FuncInfo->setVarArgsStackOffset(MFI.CreateFixedObject(
        PtrVT.getSizeInBits() / 8, CCInfo.getStackSize(), true));

    FuncInfo->setVarArgsFrameIndex(
        MFI.CreateStackObject(Depth, Align(8), false));
    SDValue FIN = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

    // The fixed integer arguments of a variadic function are stored to the
    // VarArgsFrameIndex on the stack so that they may be loaded by
    // dereferencing the result of va_next.
    for (unsigned GPRIndex = 0; GPRIndex != NumGPArgRegs; ++GPRIndex) {
      // Get an existing live-in vreg, or add a new one.
      Register VReg = MF.getRegInfo().getLiveInVirtReg(GPArgRegs[GPRIndex]);
      if (!VReg)
        VReg = MF.addLiveIn(GPArgRegs[GPRIndex], &PPC::GPRCRegClass);

      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address by four for the next argument to store
      SDValue PtrOff = DAG.getConstant(PtrVT.getSizeInBits()/8, dl, PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }

    // FIXME 32-bit SVR4: We only need to save FP argument registers if CR bit 6
    // is set.
    // The double arguments are stored to the VarArgsFrameIndex
    // on the stack.
    for (unsigned FPRIndex = 0; FPRIndex != NumFPArgRegs; ++FPRIndex) {
      // Get an existing live-in vreg, or add a new one.
      Register VReg = MF.getRegInfo().getLiveInVirtReg(FPArgRegs[FPRIndex]);
      if (!VReg)
        VReg = MF.addLiveIn(FPArgRegs[FPRIndex], &PPC::F8RCRegClass);

      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, MVT::f64);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address by eight for the next argument to store
      SDValue PtrOff = DAG.getConstant(MVT(MVT::f64).getSizeInBits()/8, dl,
                                         PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);

  return Chain;
}

// PPC64 passes i8, i16, and i32 values in i64 registers. Promote
// value to MVT::i64 and then truncate to the correct register size.
SDValue PPCTargetLowering::extendArgForPPC64(ISD::ArgFlagsTy Flags,
                                             EVT ObjectVT, SelectionDAG &DAG,
                                             SDValue ArgVal,
                                             const SDLoc &dl) const {
  if (Flags.isSExt())
    ArgVal = DAG.getNode(ISD::AssertSext, dl, MVT::i64, ArgVal,
                         DAG.getValueType(ObjectVT));
  else if (Flags.isZExt())
    ArgVal = DAG.getNode(ISD::AssertZext, dl, MVT::i64, ArgVal,
                         DAG.getValueType(ObjectVT));

  return DAG.getNode(ISD::TRUNCATE, dl, ObjectVT, ArgVal);
}

SDValue PPCTargetLowering::LowerFormalArguments_64SVR4(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // TODO: add description of PPC stack frame format, or at least some docs.
  //
  bool isELFv2ABI = Subtarget.isELFv2ABI();
  bool isLittleEndian = Subtarget.isLittleEndian();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();

  assert(!(CallConv == CallingConv::Fast && isVarArg) &&
         "fastcc not supported on varargs functions");

  EVT PtrVT = getPointerTy(MF.getDataLayout());
  // Potential tail calls could cause overwriting of argument stack slots.
  bool isImmutable = !(getTargetMachine().Options.GuaranteedTailCallOpt &&
                       (CallConv == CallingConv::Fast));
  unsigned PtrByteSize = 8;
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();

  static const MCPhysReg GPR[] = {
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };

  const unsigned Num_GPR_Regs = std::size(GPR);
  const unsigned Num_FPR_Regs = useSoftFloat() ? 0 : 13;
  const unsigned Num_VR_Regs = std::size(VR);

  // Do a first pass over the arguments to determine whether the ABI
  // guarantees that our caller has allocated the parameter save area
  // on its stack frame.  In the ELFv1 ABI, this is always the case;
  // in the ELFv2 ABI, it is true if this is a vararg function or if
  // any parameter is located in a stack slot.

  bool HasParameterArea = !isELFv2ABI || isVarArg;
  unsigned ParamAreaSize = Num_GPR_Regs * PtrByteSize;
  unsigned NumBytes = LinkageSize;
  unsigned AvailableFPRs = Num_FPR_Regs;
  unsigned AvailableVRs = Num_VR_Regs;
  for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
    if (Ins[i].Flags.isNest())
      continue;

    if (CalculateStackSlotUsed(Ins[i].VT, Ins[i].ArgVT, Ins[i].Flags,
                               PtrByteSize, LinkageSize, ParamAreaSize,
                               NumBytes, AvailableFPRs, AvailableVRs))
      HasParameterArea = true;
  }

  // Add DAG nodes to load the arguments or copy them out of registers.  On
  // entry to a function on PPC, the arguments start after the linkage area,
  // although the first ones are often in registers.

  unsigned ArgOffset = LinkageSize;
  unsigned GPR_idx = 0, FPR_idx = 0, VR_idx = 0;
  SmallVector<SDValue, 8> MemOps;
  Function::const_arg_iterator FuncArg = MF.getFunction().arg_begin();
  unsigned CurArgIdx = 0;
  for (unsigned ArgNo = 0, e = Ins.size(); ArgNo != e; ++ArgNo) {
    SDValue ArgVal;
    bool needsLoad = false;
    EVT ObjectVT = Ins[ArgNo].VT;
    EVT OrigVT = Ins[ArgNo].ArgVT;
    unsigned ObjSize = ObjectVT.getStoreSize();
    unsigned ArgSize = ObjSize;
    ISD::ArgFlagsTy Flags = Ins[ArgNo].Flags;
    if (Ins[ArgNo].isOrigArg()) {
      std::advance(FuncArg, Ins[ArgNo].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[ArgNo].getOrigArgIndex();
    }
    // We re-align the argument offset for each argument, except when using the
    // fast calling convention, when we need to make sure we do that only when
    // we'll actually use a stack slot.
    unsigned CurArgOffset;
    Align Alignment;
    auto ComputeArgOffset = [&]() {
      /* Respect alignment of argument on the stack.  */
      Alignment =
          CalculateStackSlotAlignment(ObjectVT, OrigVT, Flags, PtrByteSize);
      ArgOffset = alignTo(ArgOffset, Alignment);
      CurArgOffset = ArgOffset;
    };

    if (CallConv != CallingConv::Fast) {
      ComputeArgOffset();

      /* Compute GPR index associated with argument offset.  */
      GPR_idx = (ArgOffset - LinkageSize) / PtrByteSize;
      GPR_idx = std::min(GPR_idx, Num_GPR_Regs);
    }

    // FIXME the codegen can be much improved in some cases.
    // We do not have to keep everything in memory.
    if (Flags.isByVal()) {
      assert(Ins[ArgNo].isOrigArg() && "Byval arguments cannot be implicit");

      if (CallConv == CallingConv::Fast)
        ComputeArgOffset();

      // ObjSize is the true size, ArgSize rounded up to multiple of registers.
      ObjSize = Flags.getByValSize();
      ArgSize = ((ObjSize + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
      // Empty aggregate parameters do not take up registers.  Examples:
      //   struct { } a;
      //   union  { } b;
      //   int c[0];
      // etc.  However, we have to provide a place-holder in InVals, so
      // pretend we have an 8-byte item at the current address for that
      // purpose.
      if (!ObjSize) {
        int FI = MFI.CreateFixedObject(PtrByteSize, ArgOffset, true);
        SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
        InVals.push_back(FIN);
        continue;
      }

      // Create a stack object covering all stack doublewords occupied
      // by the argument.  If the argument is (fully or partially) on
      // the stack, or if the argument is fully in registers but the
      // caller has allocated the parameter save anyway, we can refer
      // directly to the caller's stack frame.  Otherwise, create a
      // local copy in our own frame.
      int FI;
      if (HasParameterArea ||
          ArgSize + ArgOffset > LinkageSize + Num_GPR_Regs * PtrByteSize)
        FI = MFI.CreateFixedObject(ArgSize, ArgOffset, false, true);
      else
        FI = MFI.CreateStackObject(ArgSize, Alignment, false);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);

      // Handle aggregates smaller than 8 bytes.
      if (ObjSize < PtrByteSize) {
        // The value of the object is its address, which differs from the
        // address of the enclosing doubleword on big-endian systems.
        SDValue Arg = FIN;
        if (!isLittleEndian) {
          SDValue ArgOff = DAG.getConstant(PtrByteSize - ObjSize, dl, PtrVT);
          Arg = DAG.getNode(ISD::ADD, dl, ArgOff.getValueType(), Arg, ArgOff);
        }
        InVals.push_back(Arg);

        if (GPR_idx != Num_GPR_Regs) {
          Register VReg = MF.addLiveIn(GPR[GPR_idx++], &PPC::G8RCRegClass);
          FuncInfo->addLiveInAttr(VReg, Flags);
          SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
          EVT ObjType = EVT::getIntegerVT(*DAG.getContext(), ObjSize * 8);
          SDValue Store =
              DAG.getTruncStore(Val.getValue(1), dl, Val, Arg,
                                MachinePointerInfo(&*FuncArg), ObjType);
          MemOps.push_back(Store);
        }
        // Whether we copied from a register or not, advance the offset
        // into the parameter save area by a full doubleword.
        ArgOffset += PtrByteSize;
        continue;
      }

      // The value of the object is its address, which is the address of
      // its first stack doubleword.
      InVals.push_back(FIN);

      // Store whatever pieces of the object are in registers to memory.
      for (unsigned j = 0; j < ArgSize; j += PtrByteSize) {
        if (GPR_idx == Num_GPR_Regs)
          break;

        Register VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
        FuncInfo->addLiveInAttr(VReg, Flags);
        SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
        SDValue Addr = FIN;
        if (j) {
          SDValue Off = DAG.getConstant(j, dl, PtrVT);
          Addr = DAG.getNode(ISD::ADD, dl, Off.getValueType(), Addr, Off);
        }
        unsigned StoreSizeInBits = std::min(PtrByteSize, (ObjSize - j)) * 8;
        EVT ObjType = EVT::getIntegerVT(*DAG.getContext(), StoreSizeInBits);
        SDValue Store =
            DAG.getTruncStore(Val.getValue(1), dl, Val, Addr,
                              MachinePointerInfo(&*FuncArg, j), ObjType);
        MemOps.push_back(Store);
        ++GPR_idx;
      }
      ArgOffset += ArgSize;
      continue;
    }

    switch (ObjectVT.getSimpleVT().SimpleTy) {
    default: llvm_unreachable("Unhandled argument type!");
    case MVT::i1:
    case MVT::i32:
    case MVT::i64:
      if (Flags.isNest()) {
        // The 'nest' parameter, if any, is passed in R11.
        Register VReg = MF.addLiveIn(PPC::X11, &PPC::G8RCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);

        if (ObjectVT == MVT::i32 || ObjectVT == MVT::i1)
          ArgVal = extendArgForPPC64(Flags, ObjectVT, DAG, ArgVal, dl);

        break;
      }

      // These can be scalar arguments or elements of an integer array type
      // passed directly.  Clang may use those instead of "byval" aggregate
      // types to avoid forcing arguments to memory unnecessarily.
      if (GPR_idx != Num_GPR_Regs) {
        Register VReg = MF.addLiveIn(GPR[GPR_idx++], &PPC::G8RCRegClass);
        FuncInfo->addLiveInAttr(VReg, Flags);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);

        if (ObjectVT == MVT::i32 || ObjectVT == MVT::i1)
          // PPC64 passes i8, i16, and i32 values in i64 registers. Promote
          // value to MVT::i64 and then truncate to the correct register size.
          ArgVal = extendArgForPPC64(Flags, ObjectVT, DAG, ArgVal, dl);
      } else {
        if (CallConv == CallingConv::Fast)
          ComputeArgOffset();

        needsLoad = true;
        ArgSize = PtrByteSize;
      }
      if (CallConv != CallingConv::Fast || needsLoad)
        ArgOffset += 8;
      break;

    case MVT::f32:
    case MVT::f64:
      // These can be scalar arguments or elements of a float array type
      // passed directly.  The latter are used to implement ELFv2 homogenous
      // float aggregates.
      if (FPR_idx != Num_FPR_Regs) {
        unsigned VReg;

        if (ObjectVT == MVT::f32)
          VReg = MF.addLiveIn(FPR[FPR_idx],
                              Subtarget.hasP8Vector()
                                  ? &PPC::VSSRCRegClass
                                  : &PPC::F4RCRegClass);
        else
          VReg = MF.addLiveIn(FPR[FPR_idx], Subtarget.hasVSX()
                                                ? &PPC::VSFRCRegClass
                                                : &PPC::F8RCRegClass);

        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, ObjectVT);
        ++FPR_idx;
      } else if (GPR_idx != Num_GPR_Regs && CallConv != CallingConv::Fast) {
        // FIXME: We may want to re-enable this for CallingConv::Fast on the P8
        // once we support fp <-> gpr moves.

        // This can only ever happen in the presence of f32 array types,
        // since otherwise we never run out of FPRs before running out
        // of GPRs.
        Register VReg = MF.addLiveIn(GPR[GPR_idx++], &PPC::G8RCRegClass);
        FuncInfo->addLiveInAttr(VReg, Flags);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);

        if (ObjectVT == MVT::f32) {
          if ((ArgOffset % PtrByteSize) == (isLittleEndian ? 4 : 0))
            ArgVal = DAG.getNode(ISD::SRL, dl, MVT::i64, ArgVal,
                                 DAG.getConstant(32, dl, MVT::i32));
          ArgVal = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, ArgVal);
        }

        ArgVal = DAG.getNode(ISD::BITCAST, dl, ObjectVT, ArgVal);
      } else {
        if (CallConv == CallingConv::Fast)
          ComputeArgOffset();

        needsLoad = true;
      }

      // When passing an array of floats, the array occupies consecutive
      // space in the argument area; only round up to the next doubleword
      // at the end of the array.  Otherwise, each float takes 8 bytes.
      if (CallConv != CallingConv::Fast || needsLoad) {
        ArgSize = Flags.isInConsecutiveRegs() ? ObjSize : PtrByteSize;
        ArgOffset += ArgSize;
        if (Flags.isInConsecutiveRegsLast())
          ArgOffset = ((ArgOffset + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
      }
      break;
    case MVT::v4f32:
    case MVT::v4i32:
    case MVT::v8i16:
    case MVT::v16i8:
    case MVT::v2f64:
    case MVT::v2i64:
    case MVT::v1i128:
    case MVT::f128:
      // These can be scalar arguments or elements of a vector array type
      // passed directly.  The latter are used to implement ELFv2 homogenous
      // vector aggregates.
      if (VR_idx != Num_VR_Regs) {
        Register VReg = MF.addLiveIn(VR[VR_idx], &PPC::VRRCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, ObjectVT);
        ++VR_idx;
      } else {
        if (CallConv == CallingConv::Fast)
          ComputeArgOffset();
        needsLoad = true;
      }
      if (CallConv != CallingConv::Fast || needsLoad)
        ArgOffset += 16;
      break;
    }

    // We need to load the argument to a virtual register if we determined
    // above that we ran out of physical registers of the appropriate type.
    if (needsLoad) {
      if (ObjSize < ArgSize && !isLittleEndian)
        CurArgOffset += ArgSize - ObjSize;
      int FI = MFI.CreateFixedObject(ObjSize, CurArgOffset, isImmutable);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      ArgVal = DAG.getLoad(ObjectVT, dl, Chain, FIN, MachinePointerInfo());
    }

    InVals.push_back(ArgVal);
  }

  // Area that is at least reserved in the caller of this function.
  unsigned MinReservedArea;
  if (HasParameterArea)
    MinReservedArea = std::max(ArgOffset, LinkageSize + 8 * PtrByteSize);
  else
    MinReservedArea = LinkageSize;

  // Set the size that is at least reserved in caller of this function.  Tail
  // call optimized functions' reserved stack space needs to be aligned so that
  // taking the difference between two stack areas will result in an aligned
  // stack.
  MinReservedArea =
      EnsureStackAlignment(Subtarget.getFrameLowering(), MinReservedArea);
  FuncInfo->setMinReservedArea(MinReservedArea);

  // If the function takes variable number of arguments, make a frame index for
  // the start of the first vararg value... for expansion of llvm.va_start.
  // On ELFv2ABI spec, it writes:
  // C programs that are intended to be *portable* across different compilers
  // and architectures must use the header file <stdarg.h> to deal with variable
  // argument lists.
  if (isVarArg && MFI.hasVAStart()) {
    int Depth = ArgOffset;

    FuncInfo->setVarArgsFrameIndex(
      MFI.CreateFixedObject(PtrByteSize, Depth, true));
    SDValue FIN = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

    // If this function is vararg, store any remaining integer argument regs
    // to their spots on the stack so that they may be loaded by dereferencing
    // the result of va_next.
    for (GPR_idx = (ArgOffset - LinkageSize) / PtrByteSize;
         GPR_idx < Num_GPR_Regs; ++GPR_idx) {
      Register VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address by four for the next argument to store
      SDValue PtrOff = DAG.getConstant(PtrByteSize, dl, PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);

  return Chain;
}

/// CalculateTailCallSPDiff - Get the amount the stack pointer has to be
/// adjusted to accommodate the arguments for the tailcall.
static int CalculateTailCallSPDiff(SelectionDAG& DAG, bool isTailCall,
                                   unsigned ParamSize) {

  if (!isTailCall) return 0;

  PPCFunctionInfo *FI = DAG.getMachineFunction().getInfo<PPCFunctionInfo>();
  unsigned CallerMinReservedArea = FI->getMinReservedArea();
  int SPDiff = (int)CallerMinReservedArea - (int)ParamSize;
  // Remember only if the new adjustment is bigger.
  if (SPDiff < FI->getTailCallSPDelta())
    FI->setTailCallSPDelta(SPDiff);

  return SPDiff;
}

static bool isFunctionGlobalAddress(const GlobalValue *CalleeGV);

static bool callsShareTOCBase(const Function *Caller,
                              const GlobalValue *CalleeGV,
                              const TargetMachine &TM) {
  // It does not make sense to call callsShareTOCBase() with a caller that
  // is PC Relative since PC Relative callers do not have a TOC.
#ifndef NDEBUG
  const PPCSubtarget *STICaller = &TM.getSubtarget<PPCSubtarget>(*Caller);
  assert(!STICaller->isUsingPCRelativeCalls() &&
         "PC Relative callers do not have a TOC and cannot share a TOC Base");
#endif

  // Callee is either a GlobalAddress or an ExternalSymbol. ExternalSymbols
  // don't have enough information to determine if the caller and callee share
  // the same  TOC base, so we have to pessimistically assume they don't for
  // correctness.
  if (!CalleeGV)
    return false;

  // If the callee is preemptable, then the static linker will use a plt-stub
  // which saves the toc to the stack, and needs a nop after the call
  // instruction to convert to a toc-restore.
  if (!TM.shouldAssumeDSOLocal(CalleeGV))
    return false;

  // Functions with PC Relative enabled may clobber the TOC in the same DSO.
  // We may need a TOC restore in the situation where the caller requires a
  // valid TOC but the callee is PC Relative and does not.
  const Function *F = dyn_cast<Function>(CalleeGV);
  const GlobalAlias *Alias = dyn_cast<GlobalAlias>(CalleeGV);

  // If we have an Alias we can try to get the function from there.
  if (Alias) {
    const GlobalObject *GlobalObj = Alias->getAliaseeObject();
    F = dyn_cast<Function>(GlobalObj);
  }

  // If we still have no valid function pointer we do not have enough
  // information to determine if the callee uses PC Relative calls so we must
  // assume that it does.
  if (!F)
    return false;

  // If the callee uses PC Relative we cannot guarantee that the callee won't
  // clobber the TOC of the caller and so we must assume that the two
  // functions do not share a TOC base.
  const PPCSubtarget *STICallee = &TM.getSubtarget<PPCSubtarget>(*F);
  if (STICallee->isUsingPCRelativeCalls())
    return false;

  // If the GV is not a strong definition then we need to assume it can be
  // replaced by another function at link time. The function that replaces
  // it may not share the same TOC as the caller since the callee may be
  // replaced by a PC Relative version of the same function.
  if (!CalleeGV->isStrongDefinitionForLinker())
    return false;

  // The medium and large code models are expected to provide a sufficiently
  // large TOC to provide all data addressing needs of a module with a
  // single TOC.
  if (CodeModel::Medium == TM.getCodeModel() ||
      CodeModel::Large == TM.getCodeModel())
    return true;

  // Any explicitly-specified sections and section prefixes must also match.
  // Also, if we're using -ffunction-sections, then each function is always in
  // a different section (the same is true for COMDAT functions).
  if (TM.getFunctionSections() || CalleeGV->hasComdat() ||
      Caller->hasComdat() || CalleeGV->getSection() != Caller->getSection())
    return false;
  if (const auto *F = dyn_cast<Function>(CalleeGV)) {
    if (F->getSectionPrefix() != Caller->getSectionPrefix())
      return false;
  }

  return true;
}

static bool
needStackSlotPassParameters(const PPCSubtarget &Subtarget,
                            const SmallVectorImpl<ISD::OutputArg> &Outs) {
  assert(Subtarget.is64BitELFABI());

  const unsigned PtrByteSize = 8;
  const unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();

  static const MCPhysReg GPR[] = {
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };

  const unsigned NumGPRs = std::size(GPR);
  const unsigned NumFPRs = 13;
  const unsigned NumVRs = std::size(VR);
  const unsigned ParamAreaSize = NumGPRs * PtrByteSize;

  unsigned NumBytes = LinkageSize;
  unsigned AvailableFPRs = NumFPRs;
  unsigned AvailableVRs = NumVRs;

  for (const ISD::OutputArg& Param : Outs) {
    if (Param.Flags.isNest()) continue;

    if (CalculateStackSlotUsed(Param.VT, Param.ArgVT, Param.Flags, PtrByteSize,
                               LinkageSize, ParamAreaSize, NumBytes,
                               AvailableFPRs, AvailableVRs))
      return true;
  }
  return false;
}

static bool hasSameArgumentList(const Function *CallerFn, const CallBase &CB) {
  if (CB.arg_size() != CallerFn->arg_size())
    return false;

  auto CalleeArgIter = CB.arg_begin();
  auto CalleeArgEnd = CB.arg_end();
  Function::const_arg_iterator CallerArgIter = CallerFn->arg_begin();

  for (; CalleeArgIter != CalleeArgEnd; ++CalleeArgIter, ++CallerArgIter) {
    const Value* CalleeArg = *CalleeArgIter;
    const Value* CallerArg = &(*CallerArgIter);
    if (CalleeArg == CallerArg)
      continue;

    // e.g. @caller([4 x i64] %a, [4 x i64] %b) {
    //        tail call @callee([4 x i64] undef, [4 x i64] %b)
    //      }
    // 1st argument of callee is undef and has the same type as caller.
    if (CalleeArg->getType() == CallerArg->getType() &&
        isa<UndefValue>(CalleeArg))
      continue;

    return false;
  }

  return true;
}

// Returns true if TCO is possible between the callers and callees
// calling conventions.
static bool
areCallingConvEligibleForTCO_64SVR4(CallingConv::ID CallerCC,
                                    CallingConv::ID CalleeCC) {
  // Tail calls are possible with fastcc and ccc.
  auto isTailCallableCC  = [] (CallingConv::ID CC){
      return  CC == CallingConv::C || CC == CallingConv::Fast;
  };
  if (!isTailCallableCC(CallerCC) || !isTailCallableCC(CalleeCC))
    return false;

  // We can safely tail call both fastcc and ccc callees from a c calling
  // convention caller. If the caller is fastcc, we may have less stack space
  // than a non-fastcc caller with the same signature so disable tail-calls in
  // that case.
  return CallerCC == CallingConv::C || CallerCC == CalleeCC;
}

bool PPCTargetLowering::IsEligibleForTailCallOptimization_64SVR4(
    const GlobalValue *CalleeGV, CallingConv::ID CalleeCC,
    CallingConv::ID CallerCC, const CallBase *CB, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<ISD::InputArg> &Ins, const Function *CallerFunc,
    bool isCalleeExternalSymbol) const {
  bool TailCallOpt = getTargetMachine().Options.GuaranteedTailCallOpt;

  if (DisableSCO && !TailCallOpt) return false;

  // Variadic argument functions are not supported.
  if (isVarArg) return false;

  // Check that the calling conventions are compatible for tco.
  if (!areCallingConvEligibleForTCO_64SVR4(CallerCC, CalleeCC))
    return false;

  // Caller contains any byval parameter is not supported.
  if (any_of(Ins, [](const ISD::InputArg &IA) { return IA.Flags.isByVal(); }))
    return false;

  // Callee contains any byval parameter is not supported, too.
  // Note: This is a quick work around, because in some cases, e.g.
  // caller's stack size > callee's stack size, we are still able to apply
  // sibling call optimization. For example, gcc is able to do SCO for caller1
  // in the following example, but not for caller2.
  //   struct test {
  //     long int a;
  //     char ary[56];
  //   } gTest;
  //   __attribute__((noinline)) int callee(struct test v, struct test *b) {
  //     b->a = v.a;
  //     return 0;
  //   }
  //   void caller1(struct test a, struct test c, struct test *b) {
  //     callee(gTest, b); }
  //   void caller2(struct test *b) { callee(gTest, b); }
  if (any_of(Outs, [](const ISD::OutputArg& OA) { return OA.Flags.isByVal(); }))
    return false;

  // If callee and caller use different calling conventions, we cannot pass
  // parameters on stack since offsets for the parameter area may be different.
  if (CallerCC != CalleeCC && needStackSlotPassParameters(Subtarget, Outs))
    return false;

  // All variants of 64-bit ELF ABIs without PC-Relative addressing require that
  // the caller and callee share the same TOC for TCO/SCO. If the caller and
  // callee potentially have different TOC bases then we cannot tail call since
  // we need to restore the TOC pointer after the call.
  // ref: https://bugzilla.mozilla.org/show_bug.cgi?id=973977
  // We cannot guarantee this for indirect calls or calls to external functions.
  // When PC-Relative addressing is used, the concept of the TOC is no longer
  // applicable so this check is not required.
  // Check first for indirect calls.
  if (!Subtarget.isUsingPCRelativeCalls() &&
      !isFunctionGlobalAddress(CalleeGV) && !isCalleeExternalSymbol)
    return false;

  // Check if we share the TOC base.
  if (!Subtarget.isUsingPCRelativeCalls() &&
      !callsShareTOCBase(CallerFunc, CalleeGV, getTargetMachine()))
    return false;

  // TCO allows altering callee ABI, so we don't have to check further.
  if (CalleeCC == CallingConv::Fast && TailCallOpt)
    return true;

  if (DisableSCO) return false;

  // If callee use the same argument list that caller is using, then we can
  // apply SCO on this case. If it is not, then we need to check if callee needs
  // stack for passing arguments.
  // PC Relative tail calls may not have a CallBase.
  // If there is no CallBase we cannot verify if we have the same argument
  // list so assume that we don't have the same argument list.
  if (CB && !hasSameArgumentList(CallerFunc, *CB) &&
      needStackSlotPassParameters(Subtarget, Outs))
    return false;
  else if (!CB && needStackSlotPassParameters(Subtarget, Outs))
    return false;

  return true;
}

/// IsEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization. Targets which want to do tail call
/// optimization should implement this function.
bool PPCTargetLowering::IsEligibleForTailCallOptimization(
    const GlobalValue *CalleeGV, CallingConv::ID CalleeCC,
    CallingConv::ID CallerCC, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins) const {
  if (!getTargetMachine().Options.GuaranteedTailCallOpt)
    return false;

  // Variable argument functions are not supported.
  if (isVarArg)
    return false;

  if (CalleeCC == CallingConv::Fast && CallerCC == CalleeCC) {
    // Functions containing by val parameters are not supported.
    if (any_of(Ins, [](const ISD::InputArg &IA) { return IA.Flags.isByVal(); }))
      return false;

    // Non-PIC/GOT tail calls are supported.
    if (getTargetMachine().getRelocationModel() != Reloc::PIC_)
      return true;

    // At the moment we can only do local tail calls (in same module, hidden
    // or protected) if we are generating PIC.
    if (CalleeGV)
      return CalleeGV->hasHiddenVisibility() ||
             CalleeGV->hasProtectedVisibility();
  }

  return false;
}

/// isCallCompatibleAddress - Return the immediate to use if the specified
/// 32-bit value is representable in the immediate field of a BxA instruction.
static SDNode *isBLACompatibleAddress(SDValue Op, SelectionDAG &DAG) {
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op);
  if (!C) return nullptr;

  int Addr = C->getZExtValue();
  if ((Addr & 3) != 0 ||  // Low 2 bits are implicitly zero.
      SignExtend32<26>(Addr) != Addr)
    return nullptr;  // Top 6 bits have to be sext of immediate.

  return DAG
      .getConstant(
          (int)C->getZExtValue() >> 2, SDLoc(Op),
          DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout()))
      .getNode();
}

namespace {

struct TailCallArgumentInfo {
  SDValue Arg;
  SDValue FrameIdxOp;
  int FrameIdx = 0;

  TailCallArgumentInfo() = default;
};

} // end anonymous namespace

/// StoreTailCallArgumentsToStackSlot - Stores arguments to their stack slot.
static void StoreTailCallArgumentsToStackSlot(
    SelectionDAG &DAG, SDValue Chain,
    const SmallVectorImpl<TailCallArgumentInfo> &TailCallArgs,
    SmallVectorImpl<SDValue> &MemOpChains, const SDLoc &dl) {
  for (unsigned i = 0, e = TailCallArgs.size(); i != e; ++i) {
    SDValue Arg = TailCallArgs[i].Arg;
    SDValue FIN = TailCallArgs[i].FrameIdxOp;
    int FI = TailCallArgs[i].FrameIdx;
    // Store relative to framepointer.
    MemOpChains.push_back(DAG.getStore(
        Chain, dl, Arg, FIN,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI)));
  }
}

/// EmitTailCallStoreFPAndRetAddr - Move the frame pointer and return address to
/// the appropriate stack slot for the tail call optimized function call.
static SDValue EmitTailCallStoreFPAndRetAddr(SelectionDAG &DAG, SDValue Chain,
                                             SDValue OldRetAddr, SDValue OldFP,
                                             int SPDiff, const SDLoc &dl) {
  if (SPDiff) {
    // Calculate the new stack slot for the return address.
    MachineFunction &MF = DAG.getMachineFunction();
    const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
    const PPCFrameLowering *FL = Subtarget.getFrameLowering();
    bool isPPC64 = Subtarget.isPPC64();
    int SlotSize = isPPC64 ? 8 : 4;
    int NewRetAddrLoc = SPDiff + FL->getReturnSaveOffset();
    int NewRetAddr = MF.getFrameInfo().CreateFixedObject(SlotSize,
                                                         NewRetAddrLoc, true);
    EVT VT = isPPC64 ? MVT::i64 : MVT::i32;
    SDValue NewRetAddrFrIdx = DAG.getFrameIndex(NewRetAddr, VT);
    Chain = DAG.getStore(Chain, dl, OldRetAddr, NewRetAddrFrIdx,
                         MachinePointerInfo::getFixedStack(MF, NewRetAddr));
  }
  return Chain;
}

/// CalculateTailCallArgDest - Remember Argument for later processing. Calculate
/// the position of the argument.
static void
CalculateTailCallArgDest(SelectionDAG &DAG, MachineFunction &MF, bool isPPC64,
                         SDValue Arg, int SPDiff, unsigned ArgOffset,
                     SmallVectorImpl<TailCallArgumentInfo>& TailCallArguments) {
  int Offset = ArgOffset + SPDiff;
  uint32_t OpSize = (Arg.getValueSizeInBits() + 7) / 8;
  int FI = MF.getFrameInfo().CreateFixedObject(OpSize, Offset, true);
  EVT VT = isPPC64 ? MVT::i64 : MVT::i32;
  SDValue FIN = DAG.getFrameIndex(FI, VT);
  TailCallArgumentInfo Info;
  Info.Arg = Arg;
  Info.FrameIdxOp = FIN;
  Info.FrameIdx = FI;
  TailCallArguments.push_back(Info);
}

/// EmitTCFPAndRetAddrLoad - Emit load from frame pointer and return address
/// stack slot. Returns the chain as result and the loaded frame pointers in
/// LROpOut/FPOpout. Used when tail calling.
SDValue PPCTargetLowering::EmitTailCallLoadFPAndRetAddr(
    SelectionDAG &DAG, int SPDiff, SDValue Chain, SDValue &LROpOut,
    SDValue &FPOpOut, const SDLoc &dl) const {
  if (SPDiff) {
    // Load the LR and FP stack slot for later adjusting.
    EVT VT = Subtarget.isPPC64() ? MVT::i64 : MVT::i32;
    LROpOut = getReturnAddrFrameIndex(DAG);
    LROpOut = DAG.getLoad(VT, dl, Chain, LROpOut, MachinePointerInfo());
    Chain = SDValue(LROpOut.getNode(), 1);
  }
  return Chain;
}

/// CreateCopyOfByValArgument - Make a copy of an aggregate at address specified
/// by "Src" to address "Dst" of size "Size".  Alignment information is
/// specified by the specific parameter attribute. The copy will be passed as
/// a byval function parameter.
/// Sometimes what we are copying is the end of a larger object, the part that
/// does not fit in registers.
static SDValue CreateCopyOfByValArgument(SDValue Src, SDValue Dst,
                                         SDValue Chain, ISD::ArgFlagsTy Flags,
                                         SelectionDAG &DAG, const SDLoc &dl) {
  SDValue SizeNode = DAG.getConstant(Flags.getByValSize(), dl, MVT::i32);
  return DAG.getMemcpy(
      Chain, dl, Dst, Src, SizeNode, Flags.getNonZeroByValAlign(), false, false,
      /*CI=*/nullptr, std::nullopt, MachinePointerInfo(), MachinePointerInfo());
}

/// LowerMemOpCallTo - Store the argument to the stack or remember it in case of
/// tail calls.
static void LowerMemOpCallTo(
    SelectionDAG &DAG, MachineFunction &MF, SDValue Chain, SDValue Arg,
    SDValue PtrOff, int SPDiff, unsigned ArgOffset, bool isPPC64,
    bool isTailCall, bool isVector, SmallVectorImpl<SDValue> &MemOpChains,
    SmallVectorImpl<TailCallArgumentInfo> &TailCallArguments, const SDLoc &dl) {
  EVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  if (!isTailCall) {
    if (isVector) {
      SDValue StackPtr;
      if (isPPC64)
        StackPtr = DAG.getRegister(PPC::X1, MVT::i64);
      else
        StackPtr = DAG.getRegister(PPC::R1, MVT::i32);
      PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr,
                           DAG.getConstant(ArgOffset, dl, PtrVT));
    }
    MemOpChains.push_back(
        DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
    // Calculate and remember argument location.
  } else CalculateTailCallArgDest(DAG, MF, isPPC64, Arg, SPDiff, ArgOffset,
                                  TailCallArguments);
}

static void
PrepareTailCall(SelectionDAG &DAG, SDValue &InGlue, SDValue &Chain,
                const SDLoc &dl, int SPDiff, unsigned NumBytes, SDValue LROp,
                SDValue FPOp,
                SmallVectorImpl<TailCallArgumentInfo> &TailCallArguments) {
  // Emit a sequence of copyto/copyfrom virtual registers for arguments that
  // might overwrite each other in case of tail call optimization.
  SmallVector<SDValue, 8> MemOpChains2;
  // Do not flag preceding copytoreg stuff together with the following stuff.
  InGlue = SDValue();
  StoreTailCallArgumentsToStackSlot(DAG, Chain, TailCallArguments,
                                    MemOpChains2, dl);
  if (!MemOpChains2.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains2);

  // Store the return address to the appropriate stack slot.
  Chain = EmitTailCallStoreFPAndRetAddr(DAG, Chain, LROp, FPOp, SPDiff, dl);

  // Emit callseq_end just before tailcall node.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, dl);
  InGlue = Chain.getValue(1);
}

// Is this global address that of a function that can be called by name? (as
// opposed to something that must hold a descriptor for an indirect call).
static bool isFunctionGlobalAddress(const GlobalValue *GV) {
  if (GV) {
    if (GV->isThreadLocal())
      return false;

    return GV->getValueType()->isFunctionTy();
  }

  return false;
}

SDValue PPCTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCRetInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                    *DAG.getContext());

  CCRetInfo.AnalyzeCallResult(
      Ins, (Subtarget.isSVR4ABI() && CallConv == CallingConv::Cold)
               ? RetCC_PPC_Cold
               : RetCC_PPC);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    SDValue Val;

    if (Subtarget.hasSPE() && VA.getLocVT() == MVT::f64) {
      SDValue Lo = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InGlue);
      Chain = Lo.getValue(1);
      InGlue = Lo.getValue(2);
      VA = RVLocs[++i]; // skip ahead to next loc
      SDValue Hi = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InGlue);
      Chain = Hi.getValue(1);
      InGlue = Hi.getValue(2);
      if (!Subtarget.isLittleEndian())
        std::swap (Lo, Hi);
      Val = DAG.getNode(PPCISD::BUILD_SPE64, dl, MVT::f64, Lo, Hi);
    } else {
      Val = DAG.getCopyFromReg(Chain, dl,
                               VA.getLocReg(), VA.getLocVT(), InGlue);
      Chain = Val.getValue(1);
      InGlue = Val.getValue(2);
    }

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::AExt:
      Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);
      break;
    case CCValAssign::ZExt:
      Val = DAG.getNode(ISD::AssertZext, dl, VA.getLocVT(), Val,
                        DAG.getValueType(VA.getValVT()));
      Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);
      break;
    case CCValAssign::SExt:
      Val = DAG.getNode(ISD::AssertSext, dl, VA.getLocVT(), Val,
                        DAG.getValueType(VA.getValVT()));
      Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);
      break;
    }

    InVals.push_back(Val);
  }

  return Chain;
}

static bool isIndirectCall(const SDValue &Callee, SelectionDAG &DAG,
                           const PPCSubtarget &Subtarget, bool isPatchPoint) {
  auto *G = dyn_cast<GlobalAddressSDNode>(Callee);
  const GlobalValue *GV = G ? G->getGlobal() : nullptr;

  // PatchPoint calls are not indirect.
  if (isPatchPoint)
    return false;

  if (isFunctionGlobalAddress(GV) || isa<ExternalSymbolSDNode>(Callee))
    return false;

  // Darwin, and 32-bit ELF can use a BLA. The descriptor based ABIs can not
  // becuase the immediate function pointer points to a descriptor instead of
  // a function entry point. The ELFv2 ABI cannot use a BLA because the function
  // pointer immediate points to the global entry point, while the BLA would
  // need to jump to the local entry point (see rL211174).
  if (!Subtarget.usesFunctionDescriptors() && !Subtarget.isELFv2ABI() &&
      isBLACompatibleAddress(Callee, DAG))
    return false;

  return true;
}

// AIX and 64-bit ELF ABIs w/o PCRel require a TOC save/restore around calls.
static inline bool isTOCSaveRestoreRequired(const PPCSubtarget &Subtarget) {
  return Subtarget.isAIXABI() ||
         (Subtarget.is64BitELFABI() && !Subtarget.isUsingPCRelativeCalls());
}

static unsigned getCallOpcode(PPCTargetLowering::CallFlags CFlags,
                              const Function &Caller, const SDValue &Callee,
                              const PPCSubtarget &Subtarget,
                              const TargetMachine &TM,
                              bool IsStrictFPCall = false) {
  if (CFlags.IsTailCall)
    return PPCISD::TC_RETURN;

  unsigned RetOpc = 0;
  // This is a call through a function pointer.
  if (CFlags.IsIndirect) {
    // AIX and the 64-bit ELF ABIs need to maintain the TOC pointer accross
    // indirect calls. The save of the caller's TOC pointer to the stack will be
    // inserted into the DAG as part of call lowering. The restore of the TOC
    // pointer is modeled by using a pseudo instruction for the call opcode that
    // represents the 2 instruction sequence of an indirect branch and link,
    // immediately followed by a load of the TOC pointer from the stack save
    // slot into gpr2. For 64-bit ELFv2 ABI with PCRel, do not restore the TOC
    // as it is not saved or used.
    RetOpc = isTOCSaveRestoreRequired(Subtarget) ? PPCISD::BCTRL_LOAD_TOC
                                                 : PPCISD::BCTRL;
  } else if (Subtarget.isUsingPCRelativeCalls()) {
    assert(Subtarget.is64BitELFABI() && "PC Relative is only on ELF ABI.");
    RetOpc = PPCISD::CALL_NOTOC;
  } else if (Subtarget.isAIXABI() || Subtarget.is64BitELFABI()) {
    // The ABIs that maintain a TOC pointer accross calls need to have a nop
    // immediately following the call instruction if the caller and callee may
    // have different TOC bases. At link time if the linker determines the calls
    // may not share a TOC base, the call is redirected to a trampoline inserted
    // by the linker. The trampoline will (among other things) save the callers
    // TOC pointer at an ABI designated offset in the linkage area and the
    // linker will rewrite the nop to be a load of the TOC pointer from the
    // linkage area into gpr2.
    auto *G = dyn_cast<GlobalAddressSDNode>(Callee);
    const GlobalValue *GV = G ? G->getGlobal() : nullptr;
    RetOpc =
        callsShareTOCBase(&Caller, GV, TM) ? PPCISD::CALL : PPCISD::CALL_NOP;
  } else
    RetOpc = PPCISD::CALL;
  if (IsStrictFPCall) {
    switch (RetOpc) {
    default:
      llvm_unreachable("Unknown call opcode");
    case PPCISD::BCTRL_LOAD_TOC:
      RetOpc = PPCISD::BCTRL_LOAD_TOC_RM;
      break;
    case PPCISD::BCTRL:
      RetOpc = PPCISD::BCTRL_RM;
      break;
    case PPCISD::CALL_NOTOC:
      RetOpc = PPCISD::CALL_NOTOC_RM;
      break;
    case PPCISD::CALL:
      RetOpc = PPCISD::CALL_RM;
      break;
    case PPCISD::CALL_NOP:
      RetOpc = PPCISD::CALL_NOP_RM;
      break;
    }
  }
  return RetOpc;
}

static SDValue transformCallee(const SDValue &Callee, SelectionDAG &DAG,
                               const SDLoc &dl, const PPCSubtarget &Subtarget) {
  if (!Subtarget.usesFunctionDescriptors() && !Subtarget.isELFv2ABI())
    if (SDNode *Dest = isBLACompatibleAddress(Callee, DAG))
      return SDValue(Dest, 0);

  // Returns true if the callee is local, and false otherwise.
  auto isLocalCallee = [&]() {
    const GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee);
    const GlobalValue *GV = G ? G->getGlobal() : nullptr;

    return DAG.getTarget().shouldAssumeDSOLocal(GV) &&
           !isa_and_nonnull<GlobalIFunc>(GV);
  };

  // The PLT is only used in 32-bit ELF PIC mode.  Attempting to use the PLT in
  // a static relocation model causes some versions of GNU LD (2.17.50, at
  // least) to force BSS-PLT, instead of secure-PLT, even if all objects are
  // built with secure-PLT.
  bool UsePlt =
      Subtarget.is32BitELFABI() && !isLocalCallee() &&
      Subtarget.getTargetMachine().getRelocationModel() == Reloc::PIC_;

  const auto getAIXFuncEntryPointSymbolSDNode = [&](const GlobalValue *GV) {
    const TargetMachine &TM = Subtarget.getTargetMachine();
    const TargetLoweringObjectFile *TLOF = TM.getObjFileLowering();
    MCSymbolXCOFF *S =
        cast<MCSymbolXCOFF>(TLOF->getFunctionEntryPointSymbol(GV, TM));

    MVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
    return DAG.getMCSymbol(S, PtrVT);
  };

  auto *G = dyn_cast<GlobalAddressSDNode>(Callee);
  const GlobalValue *GV = G ? G->getGlobal() : nullptr;
  if (isFunctionGlobalAddress(GV)) {
    const GlobalValue *GV = cast<GlobalAddressSDNode>(Callee)->getGlobal();

    if (Subtarget.isAIXABI()) {
      assert(!isa<GlobalIFunc>(GV) && "IFunc is not supported on AIX.");
      return getAIXFuncEntryPointSymbolSDNode(GV);
    }
    return DAG.getTargetGlobalAddress(GV, dl, Callee.getValueType(), 0,
                                      UsePlt ? PPCII::MO_PLT : 0);
  }

  if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    const char *SymName = S->getSymbol();
    if (Subtarget.isAIXABI()) {
      // If there exists a user-declared function whose name is the same as the
      // ExternalSymbol's, then we pick up the user-declared version.
      const Module *Mod = DAG.getMachineFunction().getFunction().getParent();
      if (const Function *F =
              dyn_cast_or_null<Function>(Mod->getNamedValue(SymName)))
        return getAIXFuncEntryPointSymbolSDNode(F);

      // On AIX, direct function calls reference the symbol for the function's
      // entry point, which is named by prepending a "." before the function's
      // C-linkage name. A Qualname is returned here because an external
      // function entry point is a csect with XTY_ER property.
      const auto getExternalFunctionEntryPointSymbol = [&](StringRef SymName) {
        auto &Context = DAG.getMachineFunction().getContext();
        MCSectionXCOFF *Sec = Context.getXCOFFSection(
            (Twine(".") + Twine(SymName)).str(), SectionKind::getMetadata(),
            XCOFF::CsectProperties(XCOFF::XMC_PR, XCOFF::XTY_ER));
        return Sec->getQualNameSymbol();
      };

      SymName = getExternalFunctionEntryPointSymbol(SymName)->getName().data();
    }
    return DAG.getTargetExternalSymbol(SymName, Callee.getValueType(),
                                       UsePlt ? PPCII::MO_PLT : 0);
  }

  // No transformation needed.
  assert(Callee.getNode() && "What no callee?");
  return Callee;
}

static SDValue getOutputChainFromCallSeq(SDValue CallSeqStart) {
  assert(CallSeqStart.getOpcode() == ISD::CALLSEQ_START &&
         "Expected a CALLSEQ_STARTSDNode.");

  // The last operand is the chain, except when the node has glue. If the node
  // has glue, then the last operand is the glue, and the chain is the second
  // last operand.
  SDValue LastValue = CallSeqStart.getValue(CallSeqStart->getNumValues() - 1);
  if (LastValue.getValueType() != MVT::Glue)
    return LastValue;

  return CallSeqStart.getValue(CallSeqStart->getNumValues() - 2);
}

// Creates the node that moves a functions address into the count register
// to prepare for an indirect call instruction.
static void prepareIndirectCall(SelectionDAG &DAG, SDValue &Callee,
                                SDValue &Glue, SDValue &Chain,
                                const SDLoc &dl) {
  SDValue MTCTROps[] = {Chain, Callee, Glue};
  EVT ReturnTypes[] = {MVT::Other, MVT::Glue};
  Chain = DAG.getNode(PPCISD::MTCTR, dl, ReturnTypes,
                      ArrayRef(MTCTROps, Glue.getNode() ? 3 : 2));
  // The glue is the second value produced.
  Glue = Chain.getValue(1);
}

static void prepareDescriptorIndirectCall(SelectionDAG &DAG, SDValue &Callee,
                                          SDValue &Glue, SDValue &Chain,
                                          SDValue CallSeqStart,
                                          const CallBase *CB, const SDLoc &dl,
                                          bool hasNest,
                                          const PPCSubtarget &Subtarget) {
  // Function pointers in the 64-bit SVR4 ABI do not point to the function
  // entry point, but to the function descriptor (the function entry point
  // address is part of the function descriptor though).
  // The function descriptor is a three doubleword structure with the
  // following fields: function entry point, TOC base address and
  // environment pointer.
  // Thus for a call through a function pointer, the following actions need
  // to be performed:
  //   1. Save the TOC of the caller in the TOC save area of its stack
  //      frame (this is done in LowerCall_Darwin() or LowerCall_64SVR4()).
  //   2. Load the address of the function entry point from the function
  //      descriptor.
  //   3. Load the TOC of the callee from the function descriptor into r2.
  //   4. Load the environment pointer from the function descriptor into
  //      r11.
  //   5. Branch to the function entry point address.
  //   6. On return of the callee, the TOC of the caller needs to be
  //      restored (this is done in FinishCall()).
  //
  // The loads are scheduled at the beginning of the call sequence, and the
  // register copies are flagged together to ensure that no other
  // operations can be scheduled in between. E.g. without flagging the
  // copies together, a TOC access in the caller could be scheduled between
  // the assignment of the callee TOC and the branch to the callee, which leads
  // to incorrect code.

  // Start by loading the function address from the descriptor.
  SDValue LDChain = getOutputChainFromCallSeq(CallSeqStart);
  auto MMOFlags = Subtarget.hasInvariantFunctionDescriptors()
                      ? (MachineMemOperand::MODereferenceable |
                         MachineMemOperand::MOInvariant)
                      : MachineMemOperand::MONone;

  MachinePointerInfo MPI(CB ? CB->getCalledOperand() : nullptr);

  // Registers used in building the DAG.
  const MCRegister EnvPtrReg = Subtarget.getEnvironmentPointerRegister();
  const MCRegister TOCReg = Subtarget.getTOCPointerRegister();

  // Offsets of descriptor members.
  const unsigned TOCAnchorOffset = Subtarget.descriptorTOCAnchorOffset();
  const unsigned EnvPtrOffset = Subtarget.descriptorEnvironmentPointerOffset();

  const MVT RegVT = Subtarget.isPPC64() ? MVT::i64 : MVT::i32;
  const Align Alignment = Subtarget.isPPC64() ? Align(8) : Align(4);

  // One load for the functions entry point address.
  SDValue LoadFuncPtr = DAG.getLoad(RegVT, dl, LDChain, Callee, MPI,
                                    Alignment, MMOFlags);

  // One for loading the TOC anchor for the module that contains the called
  // function.
  SDValue TOCOff = DAG.getIntPtrConstant(TOCAnchorOffset, dl);
  SDValue AddTOC = DAG.getNode(ISD::ADD, dl, RegVT, Callee, TOCOff);
  SDValue TOCPtr =
      DAG.getLoad(RegVT, dl, LDChain, AddTOC,
                  MPI.getWithOffset(TOCAnchorOffset), Alignment, MMOFlags);

  // One for loading the environment pointer.
  SDValue PtrOff = DAG.getIntPtrConstant(EnvPtrOffset, dl);
  SDValue AddPtr = DAG.getNode(ISD::ADD, dl, RegVT, Callee, PtrOff);
  SDValue LoadEnvPtr =
      DAG.getLoad(RegVT, dl, LDChain, AddPtr,
                  MPI.getWithOffset(EnvPtrOffset), Alignment, MMOFlags);


  // Then copy the newly loaded TOC anchor to the TOC pointer.
  SDValue TOCVal = DAG.getCopyToReg(Chain, dl, TOCReg, TOCPtr, Glue);
  Chain = TOCVal.getValue(0);
  Glue = TOCVal.getValue(1);

  // If the function call has an explicit 'nest' parameter, it takes the
  // place of the environment pointer.
  assert((!hasNest || !Subtarget.isAIXABI()) &&
         "Nest parameter is not supported on AIX.");
  if (!hasNest) {
    SDValue EnvVal = DAG.getCopyToReg(Chain, dl, EnvPtrReg, LoadEnvPtr, Glue);
    Chain = EnvVal.getValue(0);
    Glue = EnvVal.getValue(1);
  }

  // The rest of the indirect call sequence is the same as the non-descriptor
  // DAG.
  prepareIndirectCall(DAG, LoadFuncPtr, Glue, Chain, dl);
}

static void
buildCallOperands(SmallVectorImpl<SDValue> &Ops,
                  PPCTargetLowering::CallFlags CFlags, const SDLoc &dl,
                  SelectionDAG &DAG,
                  SmallVector<std::pair<unsigned, SDValue>, 8> &RegsToPass,
                  SDValue Glue, SDValue Chain, SDValue &Callee, int SPDiff,
                  const PPCSubtarget &Subtarget) {
  const bool IsPPC64 = Subtarget.isPPC64();
  // MVT for a general purpose register.
  const MVT RegVT = IsPPC64 ? MVT::i64 : MVT::i32;

  // First operand is always the chain.
  Ops.push_back(Chain);

  // If it's a direct call pass the callee as the second operand.
  if (!CFlags.IsIndirect)
    Ops.push_back(Callee);
  else {
    assert(!CFlags.IsPatchPoint && "Patch point calls are not indirect.");

    // For the TOC based ABIs, we have saved the TOC pointer to the linkage area
    // on the stack (this would have been done in `LowerCall_64SVR4` or
    // `LowerCall_AIX`). The call instruction is a pseudo instruction that
    // represents both the indirect branch and a load that restores the TOC
    // pointer from the linkage area. The operand for the TOC restore is an add
    // of the TOC save offset to the stack pointer. This must be the second
    // operand: after the chain input but before any other variadic arguments.
    // For 64-bit ELFv2 ABI with PCRel, do not restore the TOC as it is not
    // saved or used.
    if (isTOCSaveRestoreRequired(Subtarget)) {
      const MCRegister StackPtrReg = Subtarget.getStackPointerRegister();

      SDValue StackPtr = DAG.getRegister(StackPtrReg, RegVT);
      unsigned TOCSaveOffset = Subtarget.getFrameLowering()->getTOCSaveOffset();
      SDValue TOCOff = DAG.getIntPtrConstant(TOCSaveOffset, dl);
      SDValue AddTOC = DAG.getNode(ISD::ADD, dl, RegVT, StackPtr, TOCOff);
      Ops.push_back(AddTOC);
    }

    // Add the register used for the environment pointer.
    if (Subtarget.usesFunctionDescriptors() && !CFlags.HasNest)
      Ops.push_back(DAG.getRegister(Subtarget.getEnvironmentPointerRegister(),
                                    RegVT));


    // Add CTR register as callee so a bctr can be emitted later.
    if (CFlags.IsTailCall)
      Ops.push_back(DAG.getRegister(IsPPC64 ? PPC::CTR8 : PPC::CTR, RegVT));
  }

  // If this is a tail call add stack pointer delta.
  if (CFlags.IsTailCall)
    Ops.push_back(DAG.getConstant(SPDiff, dl, MVT::i32));

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // We cannot add R2/X2 as an operand here for PATCHPOINT, because there is
  // no way to mark dependencies as implicit here.
  // We will add the R2/X2 dependency in EmitInstrWithCustomInserter.
  if ((Subtarget.is64BitELFABI() || Subtarget.isAIXABI()) &&
       !CFlags.IsPatchPoint && !Subtarget.isUsingPCRelativeCalls())
    Ops.push_back(DAG.getRegister(Subtarget.getTOCPointerRegister(), RegVT));

  // Add implicit use of CR bit 6 for 32-bit SVR4 vararg calls
  if (CFlags.IsVarArg && Subtarget.is32BitELFABI())
    Ops.push_back(DAG.getRegister(PPC::CR1EQ, MVT::i32));

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask =
      TRI->getCallPreservedMask(DAG.getMachineFunction(), CFlags.CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  // If the glue is valid, it is the last operand.
  if (Glue.getNode())
    Ops.push_back(Glue);
}

SDValue PPCTargetLowering::FinishCall(
    CallFlags CFlags, const SDLoc &dl, SelectionDAG &DAG,
    SmallVector<std::pair<unsigned, SDValue>, 8> &RegsToPass, SDValue Glue,
    SDValue Chain, SDValue CallSeqStart, SDValue &Callee, int SPDiff,
    unsigned NumBytes, const SmallVectorImpl<ISD::InputArg> &Ins,
    SmallVectorImpl<SDValue> &InVals, const CallBase *CB) const {

  if ((Subtarget.is64BitELFABI() && !Subtarget.isUsingPCRelativeCalls()) ||
      Subtarget.isAIXABI())
    setUsesTOCBasePtr(DAG);

  unsigned CallOpc =
      getCallOpcode(CFlags, DAG.getMachineFunction().getFunction(), Callee,
                    Subtarget, DAG.getTarget(), CB ? CB->isStrictFP() : false);

  if (!CFlags.IsIndirect)
    Callee = transformCallee(Callee, DAG, dl, Subtarget);
  else if (Subtarget.usesFunctionDescriptors())
    prepareDescriptorIndirectCall(DAG, Callee, Glue, Chain, CallSeqStart, CB,
                                  dl, CFlags.HasNest, Subtarget);
  else
    prepareIndirectCall(DAG, Callee, Glue, Chain, dl);

  // Build the operand list for the call instruction.
  SmallVector<SDValue, 8> Ops;
  buildCallOperands(Ops, CFlags, dl, DAG, RegsToPass, Glue, Chain, Callee,
                    SPDiff, Subtarget);

  // Emit tail call.
  if (CFlags.IsTailCall) {
    // Indirect tail call when using PC Relative calls do not have the same
    // constraints.
    assert(((Callee.getOpcode() == ISD::Register &&
             cast<RegisterSDNode>(Callee)->getReg() == PPC::CTR) ||
            Callee.getOpcode() == ISD::TargetExternalSymbol ||
            Callee.getOpcode() == ISD::TargetGlobalAddress ||
            isa<ConstantSDNode>(Callee) ||
            (CFlags.IsIndirect && Subtarget.isUsingPCRelativeCalls())) &&
           "Expecting a global address, external symbol, absolute value, "
           "register or an indirect tail call when PC Relative calls are "
           "used.");
    // PC Relative calls also use TC_RETURN as the way to mark tail calls.
    assert(CallOpc == PPCISD::TC_RETURN &&
           "Unexpected call opcode for a tail call.");
    DAG.getMachineFunction().getFrameInfo().setHasTailCall();
    SDValue Ret = DAG.getNode(CallOpc, dl, MVT::Other, Ops);
    DAG.addNoMergeSiteInfo(Ret.getNode(), CFlags.NoMerge);
    return Ret;
  }

  std::array<EVT, 2> ReturnTypes = {{MVT::Other, MVT::Glue}};
  Chain = DAG.getNode(CallOpc, dl, ReturnTypes, Ops);
  DAG.addNoMergeSiteInfo(Chain.getNode(), CFlags.NoMerge);
  Glue = Chain.getValue(1);

  // When performing tail call optimization the callee pops its arguments off
  // the stack. Account for this here so these bytes can be pushed back on in
  // PPCFrameLowering::eliminateCallFramePseudoInstr.
  int BytesCalleePops = (CFlags.CallConv == CallingConv::Fast &&
                         getTargetMachine().Options.GuaranteedTailCallOpt)
                            ? NumBytes
                            : 0;

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, BytesCalleePops, Glue, dl);
  Glue = Chain.getValue(1);

  return LowerCallResult(Chain, Glue, CFlags.CallConv, CFlags.IsVarArg, Ins, dl,
                         DAG, InVals);
}

bool PPCTargetLowering::supportsTailCallFor(const CallBase *CB) const {
  CallingConv::ID CalleeCC = CB->getCallingConv();
  const Function *CallerFunc = CB->getCaller();
  CallingConv::ID CallerCC = CallerFunc->getCallingConv();
  const Function *CalleeFunc = CB->getCalledFunction();
  if (!CalleeFunc)
    return false;
  const GlobalValue *CalleeGV = dyn_cast<GlobalValue>(CalleeFunc);

  SmallVector<ISD::OutputArg, 2> Outs;
  SmallVector<ISD::InputArg, 2> Ins;

  GetReturnInfo(CalleeCC, CalleeFunc->getReturnType(),
                CalleeFunc->getAttributes(), Outs, *this,
                CalleeFunc->getDataLayout());

  return isEligibleForTCO(CalleeGV, CalleeCC, CallerCC, CB,
                          CalleeFunc->isVarArg(), Outs, Ins, CallerFunc,
                          false /*isCalleeExternalSymbol*/);
}

bool PPCTargetLowering::isEligibleForTCO(
    const GlobalValue *CalleeGV, CallingConv::ID CalleeCC,
    CallingConv::ID CallerCC, const CallBase *CB, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<ISD::InputArg> &Ins, const Function *CallerFunc,
    bool isCalleeExternalSymbol) const {
  if (Subtarget.useLongCalls() && !(CB && CB->isMustTailCall()))
    return false;

  if (Subtarget.isSVR4ABI() && Subtarget.isPPC64())
    return IsEligibleForTailCallOptimization_64SVR4(
        CalleeGV, CalleeCC, CallerCC, CB, isVarArg, Outs, Ins, CallerFunc,
        isCalleeExternalSymbol);
  else
    return IsEligibleForTailCallOptimization(CalleeGV, CalleeCC, CallerCC,
                                             isVarArg, Ins);
}

SDValue
PPCTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                             SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals     = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool isVarArg                         = CLI.IsVarArg;
  bool isPatchPoint                     = CLI.IsPatchPoint;
  const CallBase *CB                    = CLI.CB;

  if (isTailCall) {
    MachineFunction &MF = DAG.getMachineFunction();
    CallingConv::ID CallerCC = MF.getFunction().getCallingConv();
    auto *G = dyn_cast<GlobalAddressSDNode>(Callee);
    const GlobalValue *GV = G ? G->getGlobal() : nullptr;
    bool IsCalleeExternalSymbol = isa<ExternalSymbolSDNode>(Callee);

    isTailCall =
        isEligibleForTCO(GV, CallConv, CallerCC, CB, isVarArg, Outs, Ins,
                         &(MF.getFunction()), IsCalleeExternalSymbol);
    if (isTailCall) {
      ++NumTailCalls;
      if (!getTargetMachine().Options.GuaranteedTailCallOpt)
        ++NumSiblingCalls;

      // PC Relative calls no longer guarantee that the callee is a Global
      // Address Node. The callee could be an indirect tail call in which
      // case the SDValue for the callee could be a load (to load the address
      // of a function pointer) or it may be a register copy (to move the
      // address of the callee from a function parameter into a virtual
      // register). It may also be an ExternalSymbolSDNode (ex memcopy).
      assert((Subtarget.isUsingPCRelativeCalls() ||
              isa<GlobalAddressSDNode>(Callee)) &&
             "Callee should be an llvm::Function object.");

      LLVM_DEBUG(dbgs() << "TCO caller: " << DAG.getMachineFunction().getName()
                        << "\nTCO callee: ");
      LLVM_DEBUG(Callee.dump());
    }
  }

  if (!isTailCall && CB && CB->isMustTailCall())
    report_fatal_error("failed to perform tail call elimination on a call "
                       "site marked musttail");

  // When long calls (i.e. indirect calls) are always used, calls are always
  // made via function pointer. If we have a function name, first translate it
  // into a pointer.
  if (Subtarget.useLongCalls() && isa<GlobalAddressSDNode>(Callee) &&
      !isTailCall)
    Callee = LowerGlobalAddress(Callee, DAG);

  CallFlags CFlags(
      CallConv, isTailCall, isVarArg, isPatchPoint,
      isIndirectCall(Callee, DAG, Subtarget, isPatchPoint),
      // hasNest
      Subtarget.is64BitELFABI() &&
          any_of(Outs, [](ISD::OutputArg Arg) { return Arg.Flags.isNest(); }),
      CLI.NoMerge);

  if (Subtarget.isAIXABI())
    return LowerCall_AIX(Chain, Callee, CFlags, Outs, OutVals, Ins, dl, DAG,
                         InVals, CB);

  assert(Subtarget.isSVR4ABI());
  if (Subtarget.isPPC64())
    return LowerCall_64SVR4(Chain, Callee, CFlags, Outs, OutVals, Ins, dl, DAG,
                            InVals, CB);
  return LowerCall_32SVR4(Chain, Callee, CFlags, Outs, OutVals, Ins, dl, DAG,
                          InVals, CB);
}

SDValue PPCTargetLowering::LowerCall_32SVR4(
    SDValue Chain, SDValue Callee, CallFlags CFlags,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals,
    const CallBase *CB) const {
  // See PPCTargetLowering::LowerFormalArguments_32SVR4() for a description
  // of the 32-bit SVR4 ABI stack frame layout.

  const CallingConv::ID CallConv = CFlags.CallConv;
  const bool IsVarArg = CFlags.IsVarArg;
  const bool IsTailCall = CFlags.IsTailCall;

  assert((CallConv == CallingConv::C ||
          CallConv == CallingConv::Cold ||
          CallConv == CallingConv::Fast) && "Unknown calling convention!");

  const Align PtrAlign(4);

  MachineFunction &MF = DAG.getMachineFunction();

  // Mark this function as potentially containing a function that contains a
  // tail call. As a consequence the frame pointer will be used for dynamicalloc
  // and restoring the callers stack pointer in this functions epilog. This is
  // done because by tail calling the called function might overwrite the value
  // in this function's (MF) stack pointer stack slot 0(SP).
  if (getTargetMachine().Options.GuaranteedTailCallOpt &&
      CallConv == CallingConv::Fast)
    MF.getInfo<PPCFunctionInfo>()->setHasFastCall();

  // Count how many bytes are to be pushed on the stack, including the linkage
  // area, parameter list area and the part of the local variable space which
  // contains copies of aggregates which are passed by value.

  // Assign locations to all of the outgoing arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  PPCCCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  // Reserve space for the linkage area on the stack.
  CCInfo.AllocateStack(Subtarget.getFrameLowering()->getLinkageSize(),
                       PtrAlign);
  if (useSoftFloat())
    CCInfo.PreAnalyzeCallOperands(Outs);

  if (IsVarArg) {
    // Handle fixed and variable vector arguments differently.
    // Fixed vector arguments go into registers as long as registers are
    // available. Variable vector arguments always go into memory.
    unsigned NumArgs = Outs.size();

    for (unsigned i = 0; i != NumArgs; ++i) {
      MVT ArgVT = Outs[i].VT;
      ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
      bool Result;

      if (Outs[i].IsFixed) {
        Result = CC_PPC32_SVR4(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags,
                               CCInfo);
      } else {
        Result = CC_PPC32_SVR4_VarArg(i, ArgVT, ArgVT, CCValAssign::Full,
                                      ArgFlags, CCInfo);
      }

      if (Result) {
#ifndef NDEBUG
        errs() << "Call operand #" << i << " has unhandled type "
               << ArgVT << "\n";
#endif
        llvm_unreachable(nullptr);
      }
    }
  } else {
    // All arguments are treated the same.
    CCInfo.AnalyzeCallOperands(Outs, CC_PPC32_SVR4);
  }
  CCInfo.clearWasPPCF128();

  // Assign locations to all of the outgoing aggregate by value arguments.
  SmallVector<CCValAssign, 16> ByValArgLocs;
  CCState CCByValInfo(CallConv, IsVarArg, MF, ByValArgLocs, *DAG.getContext());

  // Reserve stack space for the allocations in CCInfo.
  CCByValInfo.AllocateStack(CCInfo.getStackSize(), PtrAlign);

  CCByValInfo.AnalyzeCallOperands(Outs, CC_PPC32_SVR4_ByVal);

  // Size of the linkage area, parameter list area and the part of the local
  // space variable where copies of aggregates which are passed by value are
  // stored.
  unsigned NumBytes = CCByValInfo.getStackSize();

  // Calculate by how many bytes the stack has to be adjusted in case of tail
  // call optimization.
  int SPDiff = CalculateTailCallSPDiff(DAG, IsTailCall, NumBytes);

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
  SDValue CallSeqStart = Chain;

  // Load the return address and frame pointer so it can be moved somewhere else
  // later.
  SDValue LROp, FPOp;
  Chain = EmitTailCallLoadFPAndRetAddr(DAG, SPDiff, Chain, LROp, FPOp, dl);

  // Set up a copy of the stack pointer for use loading and storing any
  // arguments that may not fit in the registers available for argument
  // passing.
  SDValue StackPtr = DAG.getRegister(PPC::R1, MVT::i32);

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<TailCallArgumentInfo, 8> TailCallArguments;
  SmallVector<SDValue, 8> MemOpChains;

  bool seenFloatArg = false;
  // Walk the register/memloc assignments, inserting copies/loads.
  // i - Tracks the index into the list of registers allocated for the call
  // RealArgIdx - Tracks the index into the list of actual function arguments
  // j - Tracks the index into the list of byval arguments
  for (unsigned i = 0, RealArgIdx = 0, j = 0, e = ArgLocs.size();
       i != e;
       ++i, ++RealArgIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[RealArgIdx];
    ISD::ArgFlagsTy Flags = Outs[RealArgIdx].Flags;

    if (Flags.isByVal()) {
      // Argument is an aggregate which is passed by value, thus we need to
      // create a copy of it in the local variable space of the current stack
      // frame (which is the stack frame of the caller) and pass the address of
      // this copy to the callee.
      assert((j < ByValArgLocs.size()) && "Index out of bounds!");
      CCValAssign &ByValVA = ByValArgLocs[j++];
      assert((VA.getValNo() == ByValVA.getValNo()) && "ValNo mismatch!");

      // Memory reserved in the local variable space of the callers stack frame.
      unsigned LocMemOffset = ByValVA.getLocMemOffset();

      SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset, dl);
      PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(MF.getDataLayout()),
                           StackPtr, PtrOff);

      // Create a copy of the argument in the local area of the current
      // stack frame.
      SDValue MemcpyCall =
        CreateCopyOfByValArgument(Arg, PtrOff,
                                  CallSeqStart.getNode()->getOperand(0),
                                  Flags, DAG, dl);

      // This must go outside the CALLSEQ_START..END.
      SDValue NewCallSeqStart = DAG.getCALLSEQ_START(MemcpyCall, NumBytes, 0,
                                                     SDLoc(MemcpyCall));
      DAG.ReplaceAllUsesWith(CallSeqStart.getNode(),
                             NewCallSeqStart.getNode());
      Chain = CallSeqStart = NewCallSeqStart;

      // Pass the address of the aggregate copy on the stack either in a
      // physical register or in the parameter list area of the current stack
      // frame to the callee.
      Arg = PtrOff;
    }

    // When useCRBits() is true, there can be i1 arguments.
    // It is because getRegisterType(MVT::i1) => MVT::i1,
    // and for other integer types getRegisterType() => MVT::i32.
    // Extend i1 and ensure callee will get i32.
    if (Arg.getValueType() == MVT::i1)
      Arg = DAG.getNode(Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                        dl, MVT::i32, Arg);

    if (VA.isRegLoc()) {
      seenFloatArg |= VA.getLocVT().isFloatingPoint();
      // Put argument in a physical register.
      if (Subtarget.hasSPE() && Arg.getValueType() == MVT::f64) {
        bool IsLE = Subtarget.isLittleEndian();
        SDValue SVal = DAG.getNode(PPCISD::EXTRACT_SPE, dl, MVT::i32, Arg,
                        DAG.getIntPtrConstant(IsLE ? 0 : 1, dl));
        RegsToPass.push_back(std::make_pair(VA.getLocReg(), SVal.getValue(0)));
        SVal = DAG.getNode(PPCISD::EXTRACT_SPE, dl, MVT::i32, Arg,
                           DAG.getIntPtrConstant(IsLE ? 1 : 0, dl));
        RegsToPass.push_back(std::make_pair(ArgLocs[++i].getLocReg(),
                             SVal.getValue(0)));
      } else
        RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      // Put argument in the parameter list area of the current stack frame.
      assert(VA.isMemLoc());
      unsigned LocMemOffset = VA.getLocMemOffset();

      if (!IsTailCall) {
        SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset, dl);
        PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(MF.getDataLayout()),
                             StackPtr, PtrOff);

        MemOpChains.push_back(
            DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
      } else {
        // Calculate and remember argument location.
        CalculateTailCallArgDest(DAG, MF, false, Arg, SPDiff, LocMemOffset,
                                 TailCallArguments);
      }
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InGlue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Set CR bit 6 to true if this is a vararg call with floating args passed in
  // registers.
  if (IsVarArg) {
    SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, InGlue };

    Chain = DAG.getNode(seenFloatArg ? PPCISD::CR6SET : PPCISD::CR6UNSET, dl,
                        VTs, ArrayRef(Ops, InGlue.getNode() ? 2 : 1));

    InGlue = Chain.getValue(1);
  }

  if (IsTailCall)
    PrepareTailCall(DAG, InGlue, Chain, dl, SPDiff, NumBytes, LROp, FPOp,
                    TailCallArguments);

  return FinishCall(CFlags, dl, DAG, RegsToPass, InGlue, Chain, CallSeqStart,
                    Callee, SPDiff, NumBytes, Ins, InVals, CB);
}

// Copy an argument into memory, being careful to do this outside the
// call sequence for the call to which the argument belongs.
SDValue PPCTargetLowering::createMemcpyOutsideCallSeq(
    SDValue Arg, SDValue PtrOff, SDValue CallSeqStart, ISD::ArgFlagsTy Flags,
    SelectionDAG &DAG, const SDLoc &dl) const {
  SDValue MemcpyCall = CreateCopyOfByValArgument(Arg, PtrOff,
                        CallSeqStart.getNode()->getOperand(0),
                        Flags, DAG, dl);
  // The MEMCPY must go outside the CALLSEQ_START..END.
  int64_t FrameSize = CallSeqStart.getConstantOperandVal(1);
  SDValue NewCallSeqStart = DAG.getCALLSEQ_START(MemcpyCall, FrameSize, 0,
                                                 SDLoc(MemcpyCall));
  DAG.ReplaceAllUsesWith(CallSeqStart.getNode(),
                         NewCallSeqStart.getNode());
  return NewCallSeqStart;
}

SDValue PPCTargetLowering::LowerCall_64SVR4(
    SDValue Chain, SDValue Callee, CallFlags CFlags,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals,
    const CallBase *CB) const {
  bool isELFv2ABI = Subtarget.isELFv2ABI();
  bool isLittleEndian = Subtarget.isLittleEndian();
  unsigned NumOps = Outs.size();
  bool IsSibCall = false;
  bool IsFastCall = CFlags.CallConv == CallingConv::Fast;

  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  unsigned PtrByteSize = 8;

  MachineFunction &MF = DAG.getMachineFunction();

  if (CFlags.IsTailCall && !getTargetMachine().Options.GuaranteedTailCallOpt)
    IsSibCall = true;

  // Mark this function as potentially containing a function that contains a
  // tail call. As a consequence the frame pointer will be used for dynamicalloc
  // and restoring the callers stack pointer in this functions epilog. This is
  // done because by tail calling the called function might overwrite the value
  // in this function's (MF) stack pointer stack slot 0(SP).
  if (getTargetMachine().Options.GuaranteedTailCallOpt && IsFastCall)
    MF.getInfo<PPCFunctionInfo>()->setHasFastCall();

  assert(!(IsFastCall && CFlags.IsVarArg) &&
         "fastcc not supported on varargs functions");

  // Count how many bytes are to be pushed on the stack, including the linkage
  // area, and parameter passing area.  On ELFv1, the linkage area is 48 bytes
  // reserved space for [SP][CR][LR][2 x unused][TOC]; on ELFv2, the linkage
  // area is 32 bytes reserved space for [SP][CR][LR][TOC].
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  unsigned NumBytes = LinkageSize;
  unsigned GPR_idx = 0, FPR_idx = 0, VR_idx = 0;

  static const MCPhysReg GPR[] = {
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };

  const unsigned NumGPRs = std::size(GPR);
  const unsigned NumFPRs = useSoftFloat() ? 0 : 13;
  const unsigned NumVRs = std::size(VR);

  // On ELFv2, we can avoid allocating the parameter area if all the arguments
  // can be passed to the callee in registers.
  // For the fast calling convention, there is another check below.
  // Note: We should keep consistent with LowerFormalArguments_64SVR4()
  bool HasParameterArea = !isELFv2ABI || CFlags.IsVarArg || IsFastCall;
  if (!HasParameterArea) {
    unsigned ParamAreaSize = NumGPRs * PtrByteSize;
    unsigned AvailableFPRs = NumFPRs;
    unsigned AvailableVRs = NumVRs;
    unsigned NumBytesTmp = NumBytes;
    for (unsigned i = 0; i != NumOps; ++i) {
      if (Outs[i].Flags.isNest()) continue;
      if (CalculateStackSlotUsed(Outs[i].VT, Outs[i].ArgVT, Outs[i].Flags,
                                 PtrByteSize, LinkageSize, ParamAreaSize,
                                 NumBytesTmp, AvailableFPRs, AvailableVRs))
        HasParameterArea = true;
    }
  }

  // When using the fast calling convention, we don't provide backing for
  // arguments that will be in registers.
  unsigned NumGPRsUsed = 0, NumFPRsUsed = 0, NumVRsUsed = 0;

  // Avoid allocating parameter area for fastcc functions if all the arguments
  // can be passed in the registers.
  if (IsFastCall)
    HasParameterArea = false;

  // Add up all the space actually used.
  for (unsigned i = 0; i != NumOps; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    EVT ArgVT = Outs[i].VT;
    EVT OrigVT = Outs[i].ArgVT;

    if (Flags.isNest())
      continue;

    if (IsFastCall) {
      if (Flags.isByVal()) {
        NumGPRsUsed += (Flags.getByValSize()+7)/8;
        if (NumGPRsUsed > NumGPRs)
          HasParameterArea = true;
      } else {
        switch (ArgVT.getSimpleVT().SimpleTy) {
        default: llvm_unreachable("Unexpected ValueType for argument!");
        case MVT::i1:
        case MVT::i32:
        case MVT::i64:
          if (++NumGPRsUsed <= NumGPRs)
            continue;
          break;
        case MVT::v4i32:
        case MVT::v8i16:
        case MVT::v16i8:
        case MVT::v2f64:
        case MVT::v2i64:
        case MVT::v1i128:
        case MVT::f128:
          if (++NumVRsUsed <= NumVRs)
            continue;
          break;
        case MVT::v4f32:
          if (++NumVRsUsed <= NumVRs)
            continue;
          break;
        case MVT::f32:
        case MVT::f64:
          if (++NumFPRsUsed <= NumFPRs)
            continue;
          break;
        }
        HasParameterArea = true;
      }
    }

    /* Respect alignment of argument on the stack.  */
    auto Alignement =
        CalculateStackSlotAlignment(ArgVT, OrigVT, Flags, PtrByteSize);
    NumBytes = alignTo(NumBytes, Alignement);

    NumBytes += CalculateStackSlotSize(ArgVT, Flags, PtrByteSize);
    if (Flags.isInConsecutiveRegsLast())
      NumBytes = ((NumBytes + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
  }

  unsigned NumBytesActuallyUsed = NumBytes;

  // In the old ELFv1 ABI,
  // the prolog code of the callee may store up to 8 GPR argument registers to
  // the stack, allowing va_start to index over them in memory if its varargs.
  // Because we cannot tell if this is needed on the caller side, we have to
  // conservatively assume that it is needed.  As such, make sure we have at
  // least enough stack space for the caller to store the 8 GPRs.
  // In the ELFv2 ABI, we allocate the parameter area iff a callee
  // really requires memory operands, e.g. a vararg function.
  if (HasParameterArea)
    NumBytes = std::max(NumBytes, LinkageSize + 8 * PtrByteSize);
  else
    NumBytes = LinkageSize;

  // Tail call needs the stack to be aligned.
  if (getTargetMachine().Options.GuaranteedTailCallOpt && IsFastCall)
    NumBytes = EnsureStackAlignment(Subtarget.getFrameLowering(), NumBytes);

  int SPDiff = 0;

  // Calculate by how many bytes the stack has to be adjusted in case of tail
  // call optimization.
  if (!IsSibCall)
    SPDiff = CalculateTailCallSPDiff(DAG, CFlags.IsTailCall, NumBytes);

  // To protect arguments on the stack from being clobbered in a tail call,
  // force all the loads to happen before doing any other lowering.
  if (CFlags.IsTailCall)
    Chain = DAG.getStackArgumentTokenFactor(Chain);

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  if (!IsSibCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
  SDValue CallSeqStart = Chain;

  // Load the return address and frame pointer so it can be move somewhere else
  // later.
  SDValue LROp, FPOp;
  Chain = EmitTailCallLoadFPAndRetAddr(DAG, SPDiff, Chain, LROp, FPOp, dl);

  // Set up a copy of the stack pointer for use loading and storing any
  // arguments that may not fit in the registers available for argument
  // passing.
  SDValue StackPtr = DAG.getRegister(PPC::X1, MVT::i64);

  // Figure out which arguments are going to go in registers, and which in
  // memory.  Also, if this is a vararg function, floating point operations
  // must be stored to our stack, and loaded into integer regs as well, if
  // any integer regs are available for argument passing.
  unsigned ArgOffset = LinkageSize;

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<TailCallArgumentInfo, 8> TailCallArguments;

  SmallVector<SDValue, 8> MemOpChains;
  for (unsigned i = 0; i != NumOps; ++i) {
    SDValue Arg = OutVals[i];
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    EVT ArgVT = Outs[i].VT;
    EVT OrigVT = Outs[i].ArgVT;

    // PtrOff will be used to store the current argument to the stack if a
    // register cannot be found for it.
    SDValue PtrOff;

    // We re-align the argument offset for each argument, except when using the
    // fast calling convention, when we need to make sure we do that only when
    // we'll actually use a stack slot.
    auto ComputePtrOff = [&]() {
      /* Respect alignment of argument on the stack.  */
      auto Alignment =
          CalculateStackSlotAlignment(ArgVT, OrigVT, Flags, PtrByteSize);
      ArgOffset = alignTo(ArgOffset, Alignment);

      PtrOff = DAG.getConstant(ArgOffset, dl, StackPtr.getValueType());

      PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);
    };

    if (!IsFastCall) {
      ComputePtrOff();

      /* Compute GPR index associated with argument offset.  */
      GPR_idx = (ArgOffset - LinkageSize) / PtrByteSize;
      GPR_idx = std::min(GPR_idx, NumGPRs);
    }

    // Promote integers to 64-bit values.
    if (Arg.getValueType() == MVT::i32 || Arg.getValueType() == MVT::i1) {
      // FIXME: Should this use ANY_EXTEND if neither sext nor zext?
      unsigned ExtOp = Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
      Arg = DAG.getNode(ExtOp, dl, MVT::i64, Arg);
    }

    // FIXME memcpy is used way more than necessary.  Correctness first.
    // Note: "by value" is code for passing a structure by value, not
    // basic types.
    if (Flags.isByVal()) {
      // Note: Size includes alignment padding, so
      //   struct x { short a; char b; }
      // will have Size = 4.  With #pragma pack(1), it will have Size = 3.
      // These are the proper values we need for right-justifying the
      // aggregate in a parameter register.
      unsigned Size = Flags.getByValSize();

      // An empty aggregate parameter takes up no storage and no
      // registers.
      if (Size == 0)
        continue;

      if (IsFastCall)
        ComputePtrOff();

      // All aggregates smaller than 8 bytes must be passed right-justified.
      if (Size==1 || Size==2 || Size==4) {
        EVT VT = (Size==1) ? MVT::i8 : ((Size==2) ? MVT::i16 : MVT::i32);
        if (GPR_idx != NumGPRs) {
          SDValue Load = DAG.getExtLoad(ISD::EXTLOAD, dl, PtrVT, Chain, Arg,
                                        MachinePointerInfo(), VT);
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));

          ArgOffset += PtrByteSize;
          continue;
        }
      }

      if (GPR_idx == NumGPRs && Size < 8) {
        SDValue AddPtr = PtrOff;
        if (!isLittleEndian) {
          SDValue Const = DAG.getConstant(PtrByteSize - Size, dl,
                                          PtrOff.getValueType());
          AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, Const);
        }
        Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, AddPtr,
                                                          CallSeqStart,
                                                          Flags, DAG, dl);
        ArgOffset += PtrByteSize;
        continue;
      }
      // Copy the object to parameter save area if it can not be entirely passed 
      // by registers.
      // FIXME: we only need to copy the parts which need to be passed in
      // parameter save area. For the parts passed by registers, we don't need
      // to copy them to the stack although we need to allocate space for them
      // in parameter save area.
      if ((NumGPRs - GPR_idx) * PtrByteSize < Size)
        Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, PtrOff,
                                                          CallSeqStart,
                                                          Flags, DAG, dl);

      // When a register is available, pass a small aggregate right-justified.
      if (Size < 8 && GPR_idx != NumGPRs) {
        // The easiest way to get this right-justified in a register
        // is to copy the structure into the rightmost portion of a
        // local variable slot, then load the whole slot into the
        // register.
        // FIXME: The memcpy seems to produce pretty awful code for
        // small aggregates, particularly for packed ones.
        // FIXME: It would be preferable to use the slot in the
        // parameter save area instead of a new local variable.
        SDValue AddPtr = PtrOff;
        if (!isLittleEndian) {
          SDValue Const = DAG.getConstant(8 - Size, dl, PtrOff.getValueType());
          AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, Const);
        }
        Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, AddPtr,
                                                          CallSeqStart,
                                                          Flags, DAG, dl);

        // Load the slot into the register.
        SDValue Load =
            DAG.getLoad(PtrVT, dl, Chain, PtrOff, MachinePointerInfo());
        MemOpChains.push_back(Load.getValue(1));
        RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));

        // Done with this argument.
        ArgOffset += PtrByteSize;
        continue;
      }

      // For aggregates larger than PtrByteSize, copy the pieces of the
      // object that fit into registers from the parameter save area.
      for (unsigned j=0; j<Size; j+=PtrByteSize) {
        SDValue Const = DAG.getConstant(j, dl, PtrOff.getValueType());
        SDValue AddArg = DAG.getNode(ISD::ADD, dl, PtrVT, Arg, Const);
        if (GPR_idx != NumGPRs) {
          unsigned LoadSizeInBits = std::min(PtrByteSize, (Size - j)) * 8;
          EVT ObjType = EVT::getIntegerVT(*DAG.getContext(), LoadSizeInBits);
          SDValue Load = DAG.getExtLoad(ISD::EXTLOAD, dl, PtrVT, Chain, AddArg,
                                        MachinePointerInfo(), ObjType);

          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
          ArgOffset += PtrByteSize;
        } else {
          ArgOffset += ((Size - j + PtrByteSize-1)/PtrByteSize)*PtrByteSize;
          break;
        }
      }
      continue;
    }

    switch (Arg.getSimpleValueType().SimpleTy) {
    default: llvm_unreachable("Unexpected ValueType for argument!");
    case MVT::i1:
    case MVT::i32:
    case MVT::i64:
      if (Flags.isNest()) {
        // The 'nest' parameter, if any, is passed in R11.
        RegsToPass.push_back(std::make_pair(PPC::X11, Arg));
        break;
      }

      // These can be scalar arguments or elements of an integer array type
      // passed directly.  Clang may use those instead of "byval" aggregate
      // types to avoid forcing arguments to memory unnecessarily.
      if (GPR_idx != NumGPRs) {
        RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Arg));
      } else {
        if (IsFastCall)
          ComputePtrOff();

        assert(HasParameterArea &&
               "Parameter area must exist to pass an argument in memory.");
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         true, CFlags.IsTailCall, false, MemOpChains,
                         TailCallArguments, dl);
        if (IsFastCall)
          ArgOffset += PtrByteSize;
      }
      if (!IsFastCall)
        ArgOffset += PtrByteSize;
      break;
    case MVT::f32:
    case MVT::f64: {
      // These can be scalar arguments or elements of a float array type
      // passed directly.  The latter are used to implement ELFv2 homogenous
      // float aggregates.

      // Named arguments go into FPRs first, and once they overflow, the
      // remaining arguments go into GPRs and then the parameter save area.
      // Unnamed arguments for vararg functions always go to GPRs and
      // then the parameter save area.  For now, put all arguments to vararg
      // routines always in both locations (FPR *and* GPR or stack slot).
      bool NeedGPROrStack = CFlags.IsVarArg || FPR_idx == NumFPRs;
      bool NeededLoad = false;

      // First load the argument into the next available FPR.
      if (FPR_idx != NumFPRs)
        RegsToPass.push_back(std::make_pair(FPR[FPR_idx++], Arg));

      // Next, load the argument into GPR or stack slot if needed.
      if (!NeedGPROrStack)
        ;
      else if (GPR_idx != NumGPRs && !IsFastCall) {
        // FIXME: We may want to re-enable this for CallingConv::Fast on the P8
        // once we support fp <-> gpr moves.

        // In the non-vararg case, this can only ever happen in the
        // presence of f32 array types, since otherwise we never run
        // out of FPRs before running out of GPRs.
        SDValue ArgVal;

        // Double values are always passed in a single GPR.
        if (Arg.getValueType() != MVT::f32) {
          ArgVal = DAG.getNode(ISD::BITCAST, dl, MVT::i64, Arg);

        // Non-array float values are extended and passed in a GPR.
        } else if (!Flags.isInConsecutiveRegs()) {
          ArgVal = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Arg);
          ArgVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i64, ArgVal);

        // If we have an array of floats, we collect every odd element
        // together with its predecessor into one GPR.
        } else if (ArgOffset % PtrByteSize != 0) {
          SDValue Lo, Hi;
          Lo = DAG.getNode(ISD::BITCAST, dl, MVT::i32, OutVals[i - 1]);
          Hi = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Arg);
          if (!isLittleEndian)
            std::swap(Lo, Hi);
          ArgVal = DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);

        // The final element, if even, goes into the first half of a GPR.
        } else if (Flags.isInConsecutiveRegsLast()) {
          ArgVal = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Arg);
          ArgVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i64, ArgVal);
          if (!isLittleEndian)
            ArgVal = DAG.getNode(ISD::SHL, dl, MVT::i64, ArgVal,
                                 DAG.getConstant(32, dl, MVT::i32));

        // Non-final even elements are skipped; they will be handled
        // together the with subsequent argument on the next go-around.
        } else
          ArgVal = SDValue();

        if (ArgVal.getNode())
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], ArgVal));
      } else {
        if (IsFastCall)
          ComputePtrOff();

        // Single-precision floating-point values are mapped to the
        // second (rightmost) word of the stack doubleword.
        if (Arg.getValueType() == MVT::f32 &&
            !isLittleEndian && !Flags.isInConsecutiveRegs()) {
          SDValue ConstFour = DAG.getConstant(4, dl, PtrOff.getValueType());
          PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, ConstFour);
        }

        assert(HasParameterArea &&
               "Parameter area must exist to pass an argument in memory.");
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         true, CFlags.IsTailCall, false, MemOpChains,
                         TailCallArguments, dl);

        NeededLoad = true;
      }
      // When passing an array of floats, the array occupies consecutive
      // space in the argument area; only round up to the next doubleword
      // at the end of the array.  Otherwise, each float takes 8 bytes.
      if (!IsFastCall || NeededLoad) {
        ArgOffset += (Arg.getValueType() == MVT::f32 &&
                      Flags.isInConsecutiveRegs()) ? 4 : 8;
        if (Flags.isInConsecutiveRegsLast())
          ArgOffset = ((ArgOffset + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
      }
      break;
    }
    case MVT::v4f32:
    case MVT::v4i32:
    case MVT::v8i16:
    case MVT::v16i8:
    case MVT::v2f64:
    case MVT::v2i64:
    case MVT::v1i128:
    case MVT::f128:
      // These can be scalar arguments or elements of a vector array type
      // passed directly.  The latter are used to implement ELFv2 homogenous
      // vector aggregates.

      // For a varargs call, named arguments go into VRs or on the stack as
      // usual; unnamed arguments always go to the stack or the corresponding
      // GPRs when within range.  For now, we always put the value in both
      // locations (or even all three).
      if (CFlags.IsVarArg) {
        assert(HasParameterArea &&
               "Parameter area must exist if we have a varargs call.");
        // We could elide this store in the case where the object fits
        // entirely in R registers.  Maybe later.
        SDValue Store =
            DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo());
        MemOpChains.push_back(Store);
        if (VR_idx != NumVRs) {
          SDValue Load =
              DAG.getLoad(MVT::v4f32, dl, Store, PtrOff, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(VR[VR_idx++], Load));
        }
        ArgOffset += 16;
        for (unsigned i=0; i<16; i+=PtrByteSize) {
          if (GPR_idx == NumGPRs)
            break;
          SDValue Ix = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff,
                                   DAG.getConstant(i, dl, PtrVT));
          SDValue Load =
              DAG.getLoad(PtrVT, dl, Store, Ix, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
        }
        break;
      }

      // Non-varargs Altivec params go into VRs or on the stack.
      if (VR_idx != NumVRs) {
        RegsToPass.push_back(std::make_pair(VR[VR_idx++], Arg));
      } else {
        if (IsFastCall)
          ComputePtrOff();

        assert(HasParameterArea &&
               "Parameter area must exist to pass an argument in memory.");
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         true, CFlags.IsTailCall, true, MemOpChains,
                         TailCallArguments, dl);
        if (IsFastCall)
          ArgOffset += 16;
      }

      if (!IsFastCall)
        ArgOffset += 16;
      break;
    }
  }

  assert((!HasParameterArea || NumBytesActuallyUsed == ArgOffset) &&
         "mismatch in size of parameter area");
  (void)NumBytesActuallyUsed;

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Check if this is an indirect call (MTCTR/BCTRL).
  // See prepareDescriptorIndirectCall and buildCallOperands for more
  // information about calls through function pointers in the 64-bit SVR4 ABI.
  if (CFlags.IsIndirect) {
    // For 64-bit ELFv2 ABI with PCRel, do not save the TOC of the
    // caller in the TOC save area.
    if (isTOCSaveRestoreRequired(Subtarget)) {
      assert(!CFlags.IsTailCall && "Indirect tails calls not supported");
      // Load r2 into a virtual register and store it to the TOC save area.
      setUsesTOCBasePtr(DAG);
      SDValue Val = DAG.getCopyFromReg(Chain, dl, PPC::X2, MVT::i64);
      // TOC save area offset.
      unsigned TOCSaveOffset = Subtarget.getFrameLowering()->getTOCSaveOffset();
      SDValue PtrOff = DAG.getIntPtrConstant(TOCSaveOffset, dl);
      SDValue AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);
      Chain = DAG.getStore(Val.getValue(1), dl, Val, AddPtr,
                           MachinePointerInfo::getStack(
                               DAG.getMachineFunction(), TOCSaveOffset));
    }
    // In the ELFv2 ABI, R12 must contain the address of an indirect callee.
    // This does not mean the MTCTR instruction must use R12; it's easier
    // to model this as an extra parameter, so do that.
    if (isELFv2ABI && !CFlags.IsPatchPoint)
      RegsToPass.push_back(std::make_pair((unsigned)PPC::X12, Callee));
  }

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InGlue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InGlue);
    InGlue = Chain.getValue(1);
  }

  if (CFlags.IsTailCall && !IsSibCall)
    PrepareTailCall(DAG, InGlue, Chain, dl, SPDiff, NumBytes, LROp, FPOp,
                    TailCallArguments);

  return FinishCall(CFlags, dl, DAG, RegsToPass, InGlue, Chain, CallSeqStart,
                    Callee, SPDiff, NumBytes, Ins, InVals, CB);
}

// Returns true when the shadow of a general purpose argument register
// in the parameter save area is aligned to at least 'RequiredAlign'.
static bool isGPRShadowAligned(MCPhysReg Reg, Align RequiredAlign) {
  assert(RequiredAlign.value() <= 16 &&
         "Required alignment greater than stack alignment.");
  switch (Reg) {
  default:
    report_fatal_error("called on invalid register.");
  case PPC::R5:
  case PPC::R9:
  case PPC::X3:
  case PPC::X5:
  case PPC::X7:
  case PPC::X9:
    // These registers are 16 byte aligned which is the most strict aligment
    // we can support.
    return true;
  case PPC::R3:
  case PPC::R7:
  case PPC::X4:
  case PPC::X6:
  case PPC::X8:
  case PPC::X10:
    // The shadow of these registers in the PSA is 8 byte aligned.
    return RequiredAlign <= 8;
  case PPC::R4:
  case PPC::R6:
  case PPC::R8:
  case PPC::R10:
    return RequiredAlign <= 4;
  }
}

static bool CC_AIX(unsigned ValNo, MVT ValVT, MVT LocVT,
                   CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                   CCState &S) {
  AIXCCState &State = static_cast<AIXCCState &>(S);
  const PPCSubtarget &Subtarget = static_cast<const PPCSubtarget &>(
      State.getMachineFunction().getSubtarget());
  const bool IsPPC64 = Subtarget.isPPC64();
  const unsigned PtrSize = IsPPC64 ? 8 : 4;
  const Align PtrAlign(PtrSize);
  const Align StackAlign(16);
  const MVT RegVT = IsPPC64 ? MVT::i64 : MVT::i32;

  if (ValVT == MVT::f128)
    report_fatal_error("f128 is unimplemented on AIX.");

  if (ArgFlags.isNest())
    report_fatal_error("Nest arguments are unimplemented.");

  static const MCPhysReg GPR_32[] = {// 32-bit registers.
                                     PPC::R3, PPC::R4, PPC::R5, PPC::R6,
                                     PPC::R7, PPC::R8, PPC::R9, PPC::R10};
  static const MCPhysReg GPR_64[] = {// 64-bit registers.
                                     PPC::X3, PPC::X4, PPC::X5, PPC::X6,
                                     PPC::X7, PPC::X8, PPC::X9, PPC::X10};

  static const MCPhysReg VR[] = {// Vector registers.
                                 PPC::V2,  PPC::V3,  PPC::V4,  PPC::V5,
                                 PPC::V6,  PPC::V7,  PPC::V8,  PPC::V9,
                                 PPC::V10, PPC::V11, PPC::V12, PPC::V13};

  const ArrayRef<MCPhysReg> GPRs = IsPPC64 ? GPR_64 : GPR_32;

  if (ArgFlags.isByVal()) {
    const Align ByValAlign(ArgFlags.getNonZeroByValAlign());
    if (ByValAlign > StackAlign)
      report_fatal_error("Pass-by-value arguments with alignment greater than "
                         "16 are not supported.");

    const unsigned ByValSize = ArgFlags.getByValSize();
    const Align ObjAlign = ByValAlign > PtrAlign ? ByValAlign : PtrAlign;

    // An empty aggregate parameter takes up no storage and no registers,
    // but needs a MemLoc for a stack slot for the formal arguments side.
    if (ByValSize == 0) {
      State.addLoc(CCValAssign::getMem(ValNo, MVT::INVALID_SIMPLE_VALUE_TYPE,
                                       State.getStackSize(), RegVT, LocInfo));
      return false;
    }

    // Shadow allocate any registers that are not properly aligned.
    unsigned NextReg = State.getFirstUnallocated(GPRs);
    while (NextReg != GPRs.size() &&
           !isGPRShadowAligned(GPRs[NextReg], ObjAlign)) {
      // Shadow allocate next registers since its aligment is not strict enough.
      unsigned Reg = State.AllocateReg(GPRs);
      // Allocate the stack space shadowed by said register.
      State.AllocateStack(PtrSize, PtrAlign);
      assert(Reg && "Alocating register unexpectedly failed.");
      (void)Reg;
      NextReg = State.getFirstUnallocated(GPRs);
    }

    const unsigned StackSize = alignTo(ByValSize, ObjAlign);
    unsigned Offset = State.AllocateStack(StackSize, ObjAlign);
    for (const unsigned E = Offset + StackSize; Offset < E; Offset += PtrSize) {
      if (unsigned Reg = State.AllocateReg(GPRs))
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, RegVT, LocInfo));
      else {
        State.addLoc(CCValAssign::getMem(ValNo, MVT::INVALID_SIMPLE_VALUE_TYPE,
                                         Offset, MVT::INVALID_SIMPLE_VALUE_TYPE,
                                         LocInfo));
        break;
      }
    }
    return false;
  }

  // Arguments always reserve parameter save area.
  switch (ValVT.SimpleTy) {
  default:
    report_fatal_error("Unhandled value type for argument.");
  case MVT::i64:
    // i64 arguments should have been split to i32 for PPC32.
    assert(IsPPC64 && "PPC32 should have split i64 values.");
    [[fallthrough]];
  case MVT::i1:
  case MVT::i32: {
    const unsigned Offset = State.AllocateStack(PtrSize, PtrAlign);
    // AIX integer arguments are always passed in register width.
    if (ValVT.getFixedSizeInBits() < RegVT.getFixedSizeInBits())
      LocInfo = ArgFlags.isSExt() ? CCValAssign::LocInfo::SExt
                                  : CCValAssign::LocInfo::ZExt;
    if (unsigned Reg = State.AllocateReg(GPRs))
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, RegVT, LocInfo));
    else
      State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, RegVT, LocInfo));

    return false;
  }
  case MVT::f32:
  case MVT::f64: {
    // Parameter save area (PSA) is reserved even if the float passes in fpr.
    const unsigned StoreSize = LocVT.getStoreSize();
    // Floats are always 4-byte aligned in the PSA on AIX.
    // This includes f64 in 64-bit mode for ABI compatibility.
    const unsigned Offset =
        State.AllocateStack(IsPPC64 ? 8 : StoreSize, Align(4));
    unsigned FReg = State.AllocateReg(FPR);
    if (FReg)
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, FReg, LocVT, LocInfo));

    // Reserve and initialize GPRs or initialize the PSA as required.
    for (unsigned I = 0; I < StoreSize; I += PtrSize) {
      if (unsigned Reg = State.AllocateReg(GPRs)) {
        assert(FReg && "An FPR should be available when a GPR is reserved.");
        if (State.isVarArg()) {
          // Successfully reserved GPRs are only initialized for vararg calls.
          // Custom handling is required for:
          //   f64 in PPC32 needs to be split into 2 GPRs.
          //   f32 in PPC64 needs to occupy only lower 32 bits of 64-bit GPR.
          State.addLoc(
              CCValAssign::getCustomReg(ValNo, ValVT, Reg, RegVT, LocInfo));
        }
      } else {
        // If there are insufficient GPRs, the PSA needs to be initialized.
        // Initialization occurs even if an FPR was initialized for
        // compatibility with the AIX XL compiler. The full memory for the
        // argument will be initialized even if a prior word is saved in GPR.
        // A custom memLoc is used when the argument also passes in FPR so
        // that the callee handling can skip over it easily.
        State.addLoc(
            FReg ? CCValAssign::getCustomMem(ValNo, ValVT, Offset, LocVT,
                                             LocInfo)
                 : CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
        break;
      }
    }

    return false;
  }
  case MVT::v4f32:
  case MVT::v4i32:
  case MVT::v8i16:
  case MVT::v16i8:
  case MVT::v2i64:
  case MVT::v2f64:
  case MVT::v1i128: {
    const unsigned VecSize = 16;
    const Align VecAlign(VecSize);

    if (!State.isVarArg()) {
      // If there are vector registers remaining we don't consume any stack
      // space.
      if (unsigned VReg = State.AllocateReg(VR)) {
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, VReg, LocVT, LocInfo));
        return false;
      }
      // Vectors passed on the stack do not shadow GPRs or FPRs even though they
      // might be allocated in the portion of the PSA that is shadowed by the
      // GPRs.
      const unsigned Offset = State.AllocateStack(VecSize, VecAlign);
      State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
      return false;
    }

    unsigned NextRegIndex = State.getFirstUnallocated(GPRs);
    // Burn any underaligned registers and their shadowed stack space until
    // we reach the required alignment.
    while (NextRegIndex != GPRs.size() &&
           !isGPRShadowAligned(GPRs[NextRegIndex], VecAlign)) {
      // Shadow allocate register and its stack shadow.
      unsigned Reg = State.AllocateReg(GPRs);
      State.AllocateStack(PtrSize, PtrAlign);
      assert(Reg && "Allocating register unexpectedly failed.");
      (void)Reg;
      NextRegIndex = State.getFirstUnallocated(GPRs);
    }

    // Vectors that are passed as fixed arguments are handled differently.
    // They are passed in VRs if any are available (unlike arguments passed
    // through ellipses) and shadow GPRs (unlike arguments to non-vaarg
    // functions)
    if (State.isFixed(ValNo)) {
      if (unsigned VReg = State.AllocateReg(VR)) {
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, VReg, LocVT, LocInfo));
        // Shadow allocate GPRs and stack space even though we pass in a VR.
        for (unsigned I = 0; I != VecSize; I += PtrSize)
          State.AllocateReg(GPRs);
        State.AllocateStack(VecSize, VecAlign);
        return false;
      }
      // No vector registers remain so pass on the stack.
      const unsigned Offset = State.AllocateStack(VecSize, VecAlign);
      State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
      return false;
    }

    // If all GPRS are consumed then we pass the argument fully on the stack.
    if (NextRegIndex == GPRs.size()) {
      const unsigned Offset = State.AllocateStack(VecSize, VecAlign);
      State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
      return false;
    }

    // Corner case for 32-bit codegen. We have 2 registers to pass the first
    // half of the argument, and then need to pass the remaining half on the
    // stack.
    if (GPRs[NextRegIndex] == PPC::R9) {
      const unsigned Offset = State.AllocateStack(VecSize, VecAlign);
      State.addLoc(
          CCValAssign::getCustomMem(ValNo, ValVT, Offset, LocVT, LocInfo));

      const unsigned FirstReg = State.AllocateReg(PPC::R9);
      const unsigned SecondReg = State.AllocateReg(PPC::R10);
      assert(FirstReg && SecondReg &&
             "Allocating R9 or R10 unexpectedly failed.");
      State.addLoc(
          CCValAssign::getCustomReg(ValNo, ValVT, FirstReg, RegVT, LocInfo));
      State.addLoc(
          CCValAssign::getCustomReg(ValNo, ValVT, SecondReg, RegVT, LocInfo));
      return false;
    }

    // We have enough GPRs to fully pass the vector argument, and we have
    // already consumed any underaligned registers. Start with the custom
    // MemLoc and then the custom RegLocs.
    const unsigned Offset = State.AllocateStack(VecSize, VecAlign);
    State.addLoc(
        CCValAssign::getCustomMem(ValNo, ValVT, Offset, LocVT, LocInfo));
    for (unsigned I = 0; I != VecSize; I += PtrSize) {
      const unsigned Reg = State.AllocateReg(GPRs);
      assert(Reg && "Failed to allocated register for vararg vector argument");
      State.addLoc(
          CCValAssign::getCustomReg(ValNo, ValVT, Reg, RegVT, LocInfo));
    }
    return false;
  }
  }
  return true;
}

// So far, this function is only used by LowerFormalArguments_AIX()
static const TargetRegisterClass *getRegClassForSVT(MVT::SimpleValueType SVT,
                                                    bool IsPPC64,
                                                    bool HasP8Vector,
                                                    bool HasVSX) {
  assert((IsPPC64 || SVT != MVT::i64) &&
         "i64 should have been split for 32-bit codegen.");

  switch (SVT) {
  default:
    report_fatal_error("Unexpected value type for formal argument");
  case MVT::i1:
  case MVT::i32:
  case MVT::i64:
    return IsPPC64 ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;
  case MVT::f32:
    return HasP8Vector ? &PPC::VSSRCRegClass : &PPC::F4RCRegClass;
  case MVT::f64:
    return HasVSX ? &PPC::VSFRCRegClass : &PPC::F8RCRegClass;
  case MVT::v4f32:
  case MVT::v4i32:
  case MVT::v8i16:
  case MVT::v16i8:
  case MVT::v2i64:
  case MVT::v2f64:
  case MVT::v1i128:
    return &PPC::VRRCRegClass;
  }
}

static SDValue truncateScalarIntegerArg(ISD::ArgFlagsTy Flags, EVT ValVT,
                                        SelectionDAG &DAG, SDValue ArgValue,
                                        MVT LocVT, const SDLoc &dl) {
  assert(ValVT.isScalarInteger() && LocVT.isScalarInteger());
  assert(ValVT.getFixedSizeInBits() < LocVT.getFixedSizeInBits());

  if (Flags.isSExt())
    ArgValue = DAG.getNode(ISD::AssertSext, dl, LocVT, ArgValue,
                           DAG.getValueType(ValVT));
  else if (Flags.isZExt())
    ArgValue = DAG.getNode(ISD::AssertZext, dl, LocVT, ArgValue,
                           DAG.getValueType(ValVT));

  return DAG.getNode(ISD::TRUNCATE, dl, ValVT, ArgValue);
}

static unsigned mapArgRegToOffsetAIX(unsigned Reg, const PPCFrameLowering *FL) {
  const unsigned LASize = FL->getLinkageSize();

  if (PPC::GPRCRegClass.contains(Reg)) {
    assert(Reg >= PPC::R3 && Reg <= PPC::R10 &&
           "Reg must be a valid argument register!");
    return LASize + 4 * (Reg - PPC::R3);
  }

  if (PPC::G8RCRegClass.contains(Reg)) {
    assert(Reg >= PPC::X3 && Reg <= PPC::X10 &&
           "Reg must be a valid argument register!");
    return LASize + 8 * (Reg - PPC::X3);
  }

  llvm_unreachable("Only general purpose registers expected.");
}

//   AIX ABI Stack Frame Layout:
//
//   Low Memory +--------------------------------------------+
//   SP   +---> | Back chain                                 | ---+
//        |     +--------------------------------------------+    |   
//        |     | Saved Condition Register                   |    |
//        |     +--------------------------------------------+    |
//        |     | Saved Linkage Register                     |    |
//        |     +--------------------------------------------+    | Linkage Area
//        |     | Reserved for compilers                     |    |
//        |     +--------------------------------------------+    |
//        |     | Reserved for binders                       |    |
//        |     +--------------------------------------------+    |
//        |     | Saved TOC pointer                          | ---+
//        |     +--------------------------------------------+
//        |     | Parameter save area                        |
//        |     +--------------------------------------------+
//        |     | Alloca space                               |
//        |     +--------------------------------------------+
//        |     | Local variable space                       |
//        |     +--------------------------------------------+
//        |     | Float/int conversion temporary             |
//        |     +--------------------------------------------+
//        |     | Save area for AltiVec registers            |
//        |     +--------------------------------------------+
//        |     | AltiVec alignment padding                  |
//        |     +--------------------------------------------+
//        |     | Save area for VRSAVE register              |
//        |     +--------------------------------------------+
//        |     | Save area for General Purpose registers    |
//        |     +--------------------------------------------+
//        |     | Save area for Floating Point registers     |
//        |     +--------------------------------------------+
//        +---- | Back chain                                 |
// High Memory  +--------------------------------------------+
//
//  Specifications:
//  AIX 7.2 Assembler Language Reference
//  Subroutine linkage convention

SDValue PPCTargetLowering::LowerFormalArguments_AIX(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  assert((CallConv == CallingConv::C || CallConv == CallingConv::Cold ||
          CallConv == CallingConv::Fast) &&
         "Unexpected calling convention!");

  if (getTargetMachine().Options.GuaranteedTailCallOpt)
    report_fatal_error("Tail call support is unimplemented on AIX.");

  if (useSoftFloat())
    report_fatal_error("Soft float support is unimplemented on AIX.");

  const PPCSubtarget &Subtarget = DAG.getSubtarget<PPCSubtarget>();

  const bool IsPPC64 = Subtarget.isPPC64();
  const unsigned PtrByteSize = IsPPC64 ? 8 : 4;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  AIXCCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());

  const EVT PtrVT = getPointerTy(MF.getDataLayout());
  // Reserve space for the linkage area on the stack.
  const unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  CCInfo.AllocateStack(LinkageSize, Align(PtrByteSize));
  CCInfo.AnalyzeFormalArguments(Ins, CC_AIX);

  SmallVector<SDValue, 8> MemOps;

  for (size_t I = 0, End = ArgLocs.size(); I != End; /* No increment here */) {
    CCValAssign &VA = ArgLocs[I++];
    MVT LocVT = VA.getLocVT();
    MVT ValVT = VA.getValVT();
    ISD::ArgFlagsTy Flags = Ins[VA.getValNo()].Flags;
    // For compatibility with the AIX XL compiler, the float args in the
    // parameter save area are initialized even if the argument is available
    // in register.  The caller is required to initialize both the register
    // and memory, however, the callee can choose to expect it in either.
    // The memloc is dismissed here because the argument is retrieved from
    // the register.
    if (VA.isMemLoc() && VA.needsCustom() && ValVT.isFloatingPoint())
      continue;

    auto HandleMemLoc = [&]() {
      const unsigned LocSize = LocVT.getStoreSize();
      const unsigned ValSize = ValVT.getStoreSize();
      assert((ValSize <= LocSize) &&
             "Object size is larger than size of MemLoc");
      int CurArgOffset = VA.getLocMemOffset();
      // Objects are right-justified because AIX is big-endian.
      if (LocSize > ValSize)
        CurArgOffset += LocSize - ValSize;
      // Potential tail calls could cause overwriting of argument stack slots.
      const bool IsImmutable =
          !(getTargetMachine().Options.GuaranteedTailCallOpt &&
            (CallConv == CallingConv::Fast));
      int FI = MFI.CreateFixedObject(ValSize, CurArgOffset, IsImmutable);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      SDValue ArgValue =
          DAG.getLoad(ValVT, dl, Chain, FIN, MachinePointerInfo());
      InVals.push_back(ArgValue);
    };

    // Vector arguments to VaArg functions are passed both on the stack, and
    // in any available GPRs. Load the value from the stack and add the GPRs
    // as live ins.
    if (VA.isMemLoc() && VA.needsCustom()) {
      assert(ValVT.isVector() && "Unexpected Custom MemLoc type.");
      assert(isVarArg && "Only use custom memloc for vararg.");
      // ValNo of the custom MemLoc, so we can compare it to the ValNo of the
      // matching custom RegLocs.
      const unsigned OriginalValNo = VA.getValNo();
      (void)OriginalValNo;

      auto HandleCustomVecRegLoc = [&]() {
        assert(I != End && ArgLocs[I].isRegLoc() && ArgLocs[I].needsCustom() &&
               "Missing custom RegLoc.");
        VA = ArgLocs[I++];
        assert(VA.getValVT().isVector() &&
               "Unexpected Val type for custom RegLoc.");
        assert(VA.getValNo() == OriginalValNo &&
               "ValNo mismatch between custom MemLoc and RegLoc.");
        MVT::SimpleValueType SVT = VA.getLocVT().SimpleTy;
        MF.addLiveIn(VA.getLocReg(),
                     getRegClassForSVT(SVT, IsPPC64, Subtarget.hasP8Vector(),
                                       Subtarget.hasVSX()));
      };

      HandleMemLoc();
      // In 64-bit there will be exactly 2 custom RegLocs that follow, and in
      // in 32-bit there will be 2 custom RegLocs if we are passing in R9 and
      // R10.
      HandleCustomVecRegLoc();
      HandleCustomVecRegLoc();

      // If we are targeting 32-bit, there might be 2 extra custom RegLocs if
      // we passed the vector in R5, R6, R7 and R8.
      if (I != End && ArgLocs[I].isRegLoc() && ArgLocs[I].needsCustom()) {
        assert(!IsPPC64 &&
               "Only 2 custom RegLocs expected for 64-bit codegen.");
        HandleCustomVecRegLoc();
        HandleCustomVecRegLoc();
      }

      continue;
    }

    if (VA.isRegLoc()) {
      if (VA.getValVT().isScalarInteger())
        FuncInfo->appendParameterType(PPCFunctionInfo::FixedType);
      else if (VA.getValVT().isFloatingPoint() && !VA.getValVT().isVector()) {
        switch (VA.getValVT().SimpleTy) {
        default:
          report_fatal_error("Unhandled value type for argument.");
        case MVT::f32:
          FuncInfo->appendParameterType(PPCFunctionInfo::ShortFloatingPoint);
          break;
        case MVT::f64:
          FuncInfo->appendParameterType(PPCFunctionInfo::LongFloatingPoint);
          break;
        }
      } else if (VA.getValVT().isVector()) {
        switch (VA.getValVT().SimpleTy) {
        default:
          report_fatal_error("Unhandled value type for argument.");
        case MVT::v16i8:
          FuncInfo->appendParameterType(PPCFunctionInfo::VectorChar);
          break;
        case MVT::v8i16:
          FuncInfo->appendParameterType(PPCFunctionInfo::VectorShort);
          break;
        case MVT::v4i32:
        case MVT::v2i64:
        case MVT::v1i128:
          FuncInfo->appendParameterType(PPCFunctionInfo::VectorInt);
          break;
        case MVT::v4f32:
        case MVT::v2f64:
          FuncInfo->appendParameterType(PPCFunctionInfo::VectorFloat);
          break;
        }
      }
    }

    if (Flags.isByVal() && VA.isMemLoc()) {
      const unsigned Size =
          alignTo(Flags.getByValSize() ? Flags.getByValSize() : PtrByteSize,
                  PtrByteSize);
      const int FI = MF.getFrameInfo().CreateFixedObject(
          Size, VA.getLocMemOffset(), /* IsImmutable */ false,
          /* IsAliased */ true);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      InVals.push_back(FIN);

      continue;
    }

    if (Flags.isByVal()) {
      assert(VA.isRegLoc() && "MemLocs should already be handled.");

      const MCPhysReg ArgReg = VA.getLocReg();
      const PPCFrameLowering *FL = Subtarget.getFrameLowering();

      const unsigned StackSize = alignTo(Flags.getByValSize(), PtrByteSize);
      const int FI = MF.getFrameInfo().CreateFixedObject(
          StackSize, mapArgRegToOffsetAIX(ArgReg, FL), /* IsImmutable */ false,
          /* IsAliased */ true);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      InVals.push_back(FIN);

      // Add live ins for all the RegLocs for the same ByVal.
      const TargetRegisterClass *RegClass =
          IsPPC64 ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;

      auto HandleRegLoc = [&, RegClass, LocVT](const MCPhysReg PhysReg,
                                               unsigned Offset) {
        const Register VReg = MF.addLiveIn(PhysReg, RegClass);
        // Since the callers side has left justified the aggregate in the
        // register, we can simply store the entire register into the stack
        // slot.
        SDValue CopyFrom = DAG.getCopyFromReg(Chain, dl, VReg, LocVT);
        // The store to the fixedstack object is needed becuase accessing a
        // field of the ByVal will use a gep and load. Ideally we will optimize
        // to extracting the value from the register directly, and elide the
        // stores when the arguments address is not taken, but that will need to
        // be future work.
        SDValue Store = DAG.getStore(
            CopyFrom.getValue(1), dl, CopyFrom,
            DAG.getObjectPtrOffset(dl, FIN, TypeSize::getFixed(Offset)),
            MachinePointerInfo::getFixedStack(MF, FI, Offset));

        MemOps.push_back(Store);
      };

      unsigned Offset = 0;
      HandleRegLoc(VA.getLocReg(), Offset);
      Offset += PtrByteSize;
      for (; Offset != StackSize && ArgLocs[I].isRegLoc();
           Offset += PtrByteSize) {
        assert(ArgLocs[I].getValNo() == VA.getValNo() &&
               "RegLocs should be for ByVal argument.");

        const CCValAssign RL = ArgLocs[I++];
        HandleRegLoc(RL.getLocReg(), Offset);
        FuncInfo->appendParameterType(PPCFunctionInfo::FixedType);
      }

      if (Offset != StackSize) {
        assert(ArgLocs[I].getValNo() == VA.getValNo() &&
               "Expected MemLoc for remaining bytes.");
        assert(ArgLocs[I].isMemLoc() && "Expected MemLoc for remaining bytes.");
        // Consume the MemLoc.The InVal has already been emitted, so nothing
        // more needs to be done.
        ++I;
      }

      continue;
    }

    if (VA.isRegLoc() && !VA.needsCustom()) {
      MVT::SimpleValueType SVT = ValVT.SimpleTy;
      Register VReg =
          MF.addLiveIn(VA.getLocReg(),
                       getRegClassForSVT(SVT, IsPPC64, Subtarget.hasP8Vector(),
                                         Subtarget.hasVSX()));
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, VReg, LocVT);
      if (ValVT.isScalarInteger() &&
          (ValVT.getFixedSizeInBits() < LocVT.getFixedSizeInBits())) {
        ArgValue =
            truncateScalarIntegerArg(Flags, ValVT, DAG, ArgValue, LocVT, dl);
      }
      InVals.push_back(ArgValue);
      continue;
    }
    if (VA.isMemLoc()) {
      HandleMemLoc();
      continue;
    }
  }

  // On AIX a minimum of 8 words is saved to the parameter save area.
  const unsigned MinParameterSaveArea = 8 * PtrByteSize;
  // Area that is at least reserved in the caller of this function.
  unsigned CallerReservedArea = std::max<unsigned>(
      CCInfo.getStackSize(), LinkageSize + MinParameterSaveArea);

  // Set the size that is at least reserved in caller of this function. Tail
  // call optimized function's reserved stack space needs to be aligned so
  // that taking the difference between two stack areas will result in an
  // aligned stack.
  CallerReservedArea =
      EnsureStackAlignment(Subtarget.getFrameLowering(), CallerReservedArea);
  FuncInfo->setMinReservedArea(CallerReservedArea);

  if (isVarArg) {
    FuncInfo->setVarArgsFrameIndex(
        MFI.CreateFixedObject(PtrByteSize, CCInfo.getStackSize(), true));
    SDValue FIN = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

    static const MCPhysReg GPR_32[] = {PPC::R3, PPC::R4, PPC::R5, PPC::R6,
                                       PPC::R7, PPC::R8, PPC::R9, PPC::R10};

    static const MCPhysReg GPR_64[] = {PPC::X3, PPC::X4, PPC::X5, PPC::X6,
                                       PPC::X7, PPC::X8, PPC::X9, PPC::X10};
    const unsigned NumGPArgRegs = std::size(IsPPC64 ? GPR_64 : GPR_32);

    // The fixed integer arguments of a variadic function are stored to the
    // VarArgsFrameIndex on the stack so that they may be loaded by
    // dereferencing the result of va_next.
    for (unsigned GPRIndex =
             (CCInfo.getStackSize() - LinkageSize) / PtrByteSize;
         GPRIndex < NumGPArgRegs; ++GPRIndex) {

      const Register VReg =
          IsPPC64 ? MF.addLiveIn(GPR_64[GPRIndex], &PPC::G8RCRegClass)
                  : MF.addLiveIn(GPR_32[GPRIndex], &PPC::GPRCRegClass);

      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address for the next argument to store.
      SDValue PtrOff = DAG.getConstant(PtrByteSize, dl, PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);

  return Chain;
}

SDValue PPCTargetLowering::LowerCall_AIX(
    SDValue Chain, SDValue Callee, CallFlags CFlags,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals,
    const CallBase *CB) const {
  // See PPCTargetLowering::LowerFormalArguments_AIX() for a description of the
  // AIX ABI stack frame layout.

  assert((CFlags.CallConv == CallingConv::C ||
          CFlags.CallConv == CallingConv::Cold ||
          CFlags.CallConv == CallingConv::Fast) &&
         "Unexpected calling convention!");

  if (CFlags.IsPatchPoint)
    report_fatal_error("This call type is unimplemented on AIX.");

  const PPCSubtarget &Subtarget = DAG.getSubtarget<PPCSubtarget>();

  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<CCValAssign, 16> ArgLocs;
  AIXCCState CCInfo(CFlags.CallConv, CFlags.IsVarArg, MF, ArgLocs,
                    *DAG.getContext());

  // Reserve space for the linkage save area (LSA) on the stack.
  // In both PPC32 and PPC64 there are 6 reserved slots in the LSA:
  //   [SP][CR][LR][2 x reserved][TOC].
  // The LSA is 24 bytes (6x4) in PPC32 and 48 bytes (6x8) in PPC64.
  const unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  const bool IsPPC64 = Subtarget.isPPC64();
  const EVT PtrVT = getPointerTy(DAG.getDataLayout());
  const unsigned PtrByteSize = IsPPC64 ? 8 : 4;
  CCInfo.AllocateStack(LinkageSize, Align(PtrByteSize));
  CCInfo.AnalyzeCallOperands(Outs, CC_AIX);

  // The prolog code of the callee may store up to 8 GPR argument registers to
  // the stack, allowing va_start to index over them in memory if the callee
  // is variadic.
  // Because we cannot tell if this is needed on the caller side, we have to
  // conservatively assume that it is needed.  As such, make sure we have at
  // least enough stack space for the caller to store the 8 GPRs.
  const unsigned MinParameterSaveAreaSize = 8 * PtrByteSize;
  const unsigned NumBytes = std::max<unsigned>(
      LinkageSize + MinParameterSaveAreaSize, CCInfo.getStackSize());

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass.
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
  SDValue CallSeqStart = Chain;

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  // Set up a copy of the stack pointer for loading and storing any
  // arguments that may not fit in the registers available for argument
  // passing.
  const SDValue StackPtr = IsPPC64 ? DAG.getRegister(PPC::X1, MVT::i64)
                                   : DAG.getRegister(PPC::R1, MVT::i32);

  for (unsigned I = 0, E = ArgLocs.size(); I != E;) {
    const unsigned ValNo = ArgLocs[I].getValNo();
    SDValue Arg = OutVals[ValNo];
    ISD::ArgFlagsTy Flags = Outs[ValNo].Flags;

    if (Flags.isByVal()) {
      const unsigned ByValSize = Flags.getByValSize();

      // Nothing to do for zero-sized ByVals on the caller side.
      if (!ByValSize) {
        ++I;
        continue;
      }

      auto GetLoad = [&](EVT VT, unsigned LoadOffset) {
        return DAG.getExtLoad(ISD::ZEXTLOAD, dl, PtrVT, Chain,
                              (LoadOffset != 0)
                                  ? DAG.getObjectPtrOffset(
                                        dl, Arg, TypeSize::getFixed(LoadOffset))
                                  : Arg,
                              MachinePointerInfo(), VT);
      };

      unsigned LoadOffset = 0;

      // Initialize registers, which are fully occupied by the by-val argument.
      while (LoadOffset + PtrByteSize <= ByValSize && ArgLocs[I].isRegLoc()) {
        SDValue Load = GetLoad(PtrVT, LoadOffset);
        MemOpChains.push_back(Load.getValue(1));
        LoadOffset += PtrByteSize;
        const CCValAssign &ByValVA = ArgLocs[I++];
        assert(ByValVA.getValNo() == ValNo &&
               "Unexpected location for pass-by-value argument.");
        RegsToPass.push_back(std::make_pair(ByValVA.getLocReg(), Load));
      }

      if (LoadOffset == ByValSize)
        continue;

      // There must be one more loc to handle the remainder.
      assert(ArgLocs[I].getValNo() == ValNo &&
             "Expected additional location for by-value argument.");

      if (ArgLocs[I].isMemLoc()) {
        assert(LoadOffset < ByValSize && "Unexpected memloc for by-val arg.");
        const CCValAssign &ByValVA = ArgLocs[I++];
        ISD::ArgFlagsTy MemcpyFlags = Flags;
        // Only memcpy the bytes that don't pass in register.
        MemcpyFlags.setByValSize(ByValSize - LoadOffset);
        Chain = CallSeqStart = createMemcpyOutsideCallSeq(
            (LoadOffset != 0) ? DAG.getObjectPtrOffset(
                                    dl, Arg, TypeSize::getFixed(LoadOffset))
                              : Arg,
            DAG.getObjectPtrOffset(
                dl, StackPtr, TypeSize::getFixed(ByValVA.getLocMemOffset())),
            CallSeqStart, MemcpyFlags, DAG, dl);
        continue;
      }

      // Initialize the final register residue.
      // Any residue that occupies the final by-val arg register must be
      // left-justified on AIX. Loads must be a power-of-2 size and cannot be
      // larger than the ByValSize. For example: a 7 byte by-val arg requires 4,
      // 2 and 1 byte loads.
      const unsigned ResidueBytes = ByValSize % PtrByteSize;
      assert(ResidueBytes != 0 && LoadOffset + PtrByteSize > ByValSize &&
             "Unexpected register residue for by-value argument.");
      SDValue ResidueVal;
      for (unsigned Bytes = 0; Bytes != ResidueBytes;) {
        const unsigned N = llvm::bit_floor(ResidueBytes - Bytes);
        const MVT VT =
            N == 1 ? MVT::i8
                   : ((N == 2) ? MVT::i16 : (N == 4 ? MVT::i32 : MVT::i64));
        SDValue Load = GetLoad(VT, LoadOffset);
        MemOpChains.push_back(Load.getValue(1));
        LoadOffset += N;
        Bytes += N;

        // By-val arguments are passed left-justfied in register.
        // Every load here needs to be shifted, otherwise a full register load
        // should have been used.
        assert(PtrVT.getSimpleVT().getSizeInBits() > (Bytes * 8) &&
               "Unexpected load emitted during handling of pass-by-value "
               "argument.");
        unsigned NumSHLBits = PtrVT.getSimpleVT().getSizeInBits() - (Bytes * 8);
        EVT ShiftAmountTy =
            getShiftAmountTy(Load->getValueType(0), DAG.getDataLayout());
        SDValue SHLAmt = DAG.getConstant(NumSHLBits, dl, ShiftAmountTy);
        SDValue ShiftedLoad =
            DAG.getNode(ISD::SHL, dl, Load.getValueType(), Load, SHLAmt);
        ResidueVal = ResidueVal ? DAG.getNode(ISD::OR, dl, PtrVT, ResidueVal,
                                              ShiftedLoad)
                                : ShiftedLoad;
      }

      const CCValAssign &ByValVA = ArgLocs[I++];
      RegsToPass.push_back(std::make_pair(ByValVA.getLocReg(), ResidueVal));
      continue;
    }

    CCValAssign &VA = ArgLocs[I++];
    const MVT LocVT = VA.getLocVT();
    const MVT ValVT = VA.getValVT();

    switch (VA.getLocInfo()) {
    default:
      report_fatal_error("Unexpected argument extension type.");
    case CCValAssign::Full:
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    }

    if (VA.isRegLoc() && !VA.needsCustom()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
      continue;
    }

    // Vector arguments passed to VarArg functions need custom handling when
    // they are passed (at least partially) in GPRs.
    if (VA.isMemLoc() && VA.needsCustom() && ValVT.isVector()) {
      assert(CFlags.IsVarArg && "Custom MemLocs only used for Vector args.");
      // Store value to its stack slot.
      SDValue PtrOff =
          DAG.getConstant(VA.getLocMemOffset(), dl, StackPtr.getValueType());
      PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);
      SDValue Store =
          DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo());
      MemOpChains.push_back(Store);
      const unsigned OriginalValNo = VA.getValNo();
      // Then load the GPRs from the stack
      unsigned LoadOffset = 0;
      auto HandleCustomVecRegLoc = [&]() {
        assert(I != E && "Unexpected end of CCvalAssigns.");
        assert(ArgLocs[I].isRegLoc() && ArgLocs[I].needsCustom() &&
               "Expected custom RegLoc.");
        CCValAssign RegVA = ArgLocs[I++];
        assert(RegVA.getValNo() == OriginalValNo &&
               "Custom MemLoc ValNo and custom RegLoc ValNo must match.");
        SDValue Add = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff,
                                  DAG.getConstant(LoadOffset, dl, PtrVT));
        SDValue Load = DAG.getLoad(PtrVT, dl, Store, Add, MachinePointerInfo());
        MemOpChains.push_back(Load.getValue(1));
        RegsToPass.push_back(std::make_pair(RegVA.getLocReg(), Load));
        LoadOffset += PtrByteSize;
      };

      // In 64-bit there will be exactly 2 custom RegLocs that follow, and in
      // in 32-bit there will be 2 custom RegLocs if we are passing in R9 and
      // R10.
      HandleCustomVecRegLoc();
      HandleCustomVecRegLoc();

      if (I != E && ArgLocs[I].isRegLoc() && ArgLocs[I].needsCustom() &&
          ArgLocs[I].getValNo() == OriginalValNo) {
        assert(!IsPPC64 &&
               "Only 2 custom RegLocs expected for 64-bit codegen.");
        HandleCustomVecRegLoc();
        HandleCustomVecRegLoc();
      }

      continue;
    }

    if (VA.isMemLoc()) {
      SDValue PtrOff =
          DAG.getConstant(VA.getLocMemOffset(), dl, StackPtr.getValueType());
      PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);
      MemOpChains.push_back(
          DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));

      continue;
    }

    if (!ValVT.isFloatingPoint())
      report_fatal_error(
          "Unexpected register handling for calling convention.");

    // Custom handling is used for GPR initializations for vararg float
    // arguments.
    assert(VA.isRegLoc() && VA.needsCustom() && CFlags.IsVarArg &&
           LocVT.isInteger() &&
           "Custom register handling only expected for VarArg.");

    SDValue ArgAsInt =
        DAG.getBitcast(MVT::getIntegerVT(ValVT.getSizeInBits()), Arg);

    if (Arg.getValueType().getStoreSize() == LocVT.getStoreSize())
      // f32 in 32-bit GPR
      // f64 in 64-bit GPR
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), ArgAsInt));
    else if (Arg.getValueType().getFixedSizeInBits() <
             LocVT.getFixedSizeInBits())
      // f32 in 64-bit GPR.
      RegsToPass.push_back(std::make_pair(
          VA.getLocReg(), DAG.getZExtOrTrunc(ArgAsInt, dl, LocVT)));
    else {
      // f64 in two 32-bit GPRs
      // The 2 GPRs are marked custom and expected to be adjacent in ArgLocs.
      assert(Arg.getValueType() == MVT::f64 && CFlags.IsVarArg && !IsPPC64 &&
             "Unexpected custom register for argument!");
      CCValAssign &GPR1 = VA;
      SDValue MSWAsI64 = DAG.getNode(ISD::SRL, dl, MVT::i64, ArgAsInt,
                                     DAG.getConstant(32, dl, MVT::i8));
      RegsToPass.push_back(std::make_pair(
          GPR1.getLocReg(), DAG.getZExtOrTrunc(MSWAsI64, dl, MVT::i32)));

      if (I != E) {
        // If only 1 GPR was available, there will only be one custom GPR and
        // the argument will also pass in memory.
        CCValAssign &PeekArg = ArgLocs[I];
        if (PeekArg.isRegLoc() && PeekArg.getValNo() == PeekArg.getValNo()) {
          assert(PeekArg.needsCustom() && "A second custom GPR is expected.");
          CCValAssign &GPR2 = ArgLocs[I++];
          RegsToPass.push_back(std::make_pair(
              GPR2.getLocReg(), DAG.getZExtOrTrunc(ArgAsInt, dl, MVT::i32)));
        }
      }
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // For indirect calls, we need to save the TOC base to the stack for
  // restoration after the call.
  if (CFlags.IsIndirect) {
    assert(!CFlags.IsTailCall && "Indirect tail-calls not supported.");
    const MCRegister TOCBaseReg = Subtarget.getTOCPointerRegister();
    const MCRegister StackPtrReg = Subtarget.getStackPointerRegister();
    const MVT PtrVT = Subtarget.isPPC64() ? MVT::i64 : MVT::i32;
    const unsigned TOCSaveOffset =
        Subtarget.getFrameLowering()->getTOCSaveOffset();

    setUsesTOCBasePtr(DAG);
    SDValue Val = DAG.getCopyFromReg(Chain, dl, TOCBaseReg, PtrVT);
    SDValue PtrOff = DAG.getIntPtrConstant(TOCSaveOffset, dl);
    SDValue StackPtr = DAG.getRegister(StackPtrReg, PtrVT);
    SDValue AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);
    Chain = DAG.getStore(
        Val.getValue(1), dl, Val, AddPtr,
        MachinePointerInfo::getStack(DAG.getMachineFunction(), TOCSaveOffset));
  }

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InGlue;
  for (auto Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, dl, Reg.first, Reg.second, InGlue);
    InGlue = Chain.getValue(1);
  }

  const int SPDiff = 0;
  return FinishCall(CFlags, dl, DAG, RegsToPass, InGlue, Chain, CallSeqStart,
                    Callee, SPDiff, NumBytes, Ins, InVals, CB);
}

bool
PPCTargetLowering::CanLowerReturn(CallingConv::ID CallConv,
                                  MachineFunction &MF, bool isVarArg,
                                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                                  LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(
      Outs, (Subtarget.isSVR4ABI() && CallConv == CallingConv::Cold)
                ? RetCC_PPC_Cold
                : RetCC_PPC);
}

SDValue
PPCTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &dl, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs,
                       (Subtarget.isSVR4ABI() && CallConv == CallingConv::Cold)
                           ? RetCC_PPC_Cold
                           : RetCC_PPC);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0, RealResIdx = 0; i != RVLocs.size(); ++i, ++RealResIdx) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    SDValue Arg = OutVals[RealResIdx];

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    }
    if (Subtarget.hasSPE() && VA.getLocVT() == MVT::f64) {
      bool isLittleEndian = Subtarget.isLittleEndian();
      // Legalize ret f64 -> ret 2 x i32.
      SDValue SVal =
          DAG.getNode(PPCISD::EXTRACT_SPE, dl, MVT::i32, Arg,
                      DAG.getIntPtrConstant(isLittleEndian ? 0 : 1, dl));
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), SVal, Glue);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
      SVal = DAG.getNode(PPCISD::EXTRACT_SPE, dl, MVT::i32, Arg,
                         DAG.getIntPtrConstant(isLittleEndian ? 1 : 0, dl));
      Glue = Chain.getValue(1);
      VA = RVLocs[++i]; // skip ahead to next loc
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), SVal, Glue);
    } else
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Arg, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;  // Update chain.

  // Add the glue if we have it.
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(PPCISD::RET_GLUE, dl, MVT::Other, RetOps);
}

SDValue
PPCTargetLowering::LowerGET_DYNAMIC_AREA_OFFSET(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc dl(Op);

  // Get the correct type for integers.
  EVT IntVT = Op.getValueType();

  // Get the inputs.
  SDValue Chain = Op.getOperand(0);
  SDValue FPSIdx = getFramePointerFrameIndex(DAG);
  // Build a DYNAREAOFFSET node.
  SDValue Ops[2] = {Chain, FPSIdx};
  SDVTList VTs = DAG.getVTList(IntVT);
  return DAG.getNode(PPCISD::DYNAREAOFFSET, dl, VTs, Ops);
}

SDValue PPCTargetLowering::LowerSTACKRESTORE(SDValue Op,
                                             SelectionDAG &DAG) const {
  // When we pop the dynamic allocation we need to restore the SP link.
  SDLoc dl(Op);

  // Get the correct type for pointers.
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  // Construct the stack pointer operand.
  bool isPPC64 = Subtarget.isPPC64();
  unsigned SP = isPPC64 ? PPC::X1 : PPC::R1;
  SDValue StackPtr = DAG.getRegister(SP, PtrVT);

  // Get the operands for the STACKRESTORE.
  SDValue Chain = Op.getOperand(0);
  SDValue SaveSP = Op.getOperand(1);

  // Load the old link SP.
  SDValue LoadLinkSP =
      DAG.getLoad(PtrVT, dl, Chain, StackPtr, MachinePointerInfo());

  // Restore the stack pointer.
  Chain = DAG.getCopyToReg(LoadLinkSP.getValue(1), dl, SP, SaveSP);

  // Store the old link SP.
  return DAG.getStore(Chain, dl, LoadLinkSP, StackPtr, MachinePointerInfo());
}

SDValue PPCTargetLowering::getReturnAddrFrameIndex(SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  bool isPPC64 = Subtarget.isPPC64();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  // Get current frame pointer save index.  The users of this index will be
  // primarily DYNALLOC instructions.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  int RASI = FI->getReturnAddrSaveIndex();

  // If the frame pointer save index hasn't been defined yet.
  if (!RASI) {
    // Find out what the fix offset of the frame pointer save area.
    int LROffset = Subtarget.getFrameLowering()->getReturnSaveOffset();
    // Allocate the frame index for frame pointer save area.
    RASI = MF.getFrameInfo().CreateFixedObject(isPPC64? 8 : 4, LROffset, false);
    // Save the result.
    FI->setReturnAddrSaveIndex(RASI);
  }
  return DAG.getFrameIndex(RASI, PtrVT);
}

SDValue
PPCTargetLowering::getFramePointerFrameIndex(SelectionDAG & DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  bool isPPC64 = Subtarget.isPPC64();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  // Get current frame pointer save index.  The users of this index will be
  // primarily DYNALLOC instructions.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  int FPSI = FI->getFramePointerSaveIndex();

  // If the frame pointer save index hasn't been defined yet.
  if (!FPSI) {
    // Find out what the fix offset of the frame pointer save area.
    int FPOffset = Subtarget.getFrameLowering()->getFramePointerSaveOffset();
    // Allocate the frame index for frame pointer save area.
    FPSI = MF.getFrameInfo().CreateFixedObject(isPPC64? 8 : 4, FPOffset, true);
    // Save the result.
    FI->setFramePointerSaveIndex(FPSI);
  }
  return DAG.getFrameIndex(FPSI, PtrVT);
}

SDValue PPCTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                   SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  // Get the inputs.
  SDValue Chain = Op.getOperand(0);
  SDValue Size  = Op.getOperand(1);
  SDLoc dl(Op);

  // Get the correct type for pointers.
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  // Negate the size.
  SDValue NegSize = DAG.getNode(ISD::SUB, dl, PtrVT,
                                DAG.getConstant(0, dl, PtrVT), Size);
  // Construct a node for the frame pointer save index.
  SDValue FPSIdx = getFramePointerFrameIndex(DAG);
  SDValue Ops[3] = { Chain, NegSize, FPSIdx };
  SDVTList VTs = DAG.getVTList(PtrVT, MVT::Other);
  if (hasInlineStackProbe(MF))
    return DAG.getNode(PPCISD::PROBED_ALLOCA, dl, VTs, Ops);
  return DAG.getNode(PPCISD::DYNALLOC, dl, VTs, Ops);
}

SDValue PPCTargetLowering::LowerEH_DWARF_CFA(SDValue Op,
                                                     SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  bool isPPC64 = Subtarget.isPPC64();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  int FI = MF.getFrameInfo().CreateFixedObject(isPPC64 ? 8 : 4, 0, false);
  return DAG.getFrameIndex(FI, PtrVT);
}

SDValue PPCTargetLowering::lowerEH_SJLJ_SETJMP(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(PPCISD::EH_SJLJ_SETJMP, DL,
                     DAG.getVTList(MVT::i32, MVT::Other),
                     Op.getOperand(0), Op.getOperand(1));
}

SDValue PPCTargetLowering::lowerEH_SJLJ_LONGJMP(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(PPCISD::EH_SJLJ_LONGJMP, DL, MVT::Other,
                     Op.getOperand(0), Op.getOperand(1));
}

SDValue PPCTargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getValueType().isVector())
    return LowerVectorLoad(Op, DAG);

  assert(Op.getValueType() == MVT::i1 &&
         "Custom lowering only for i1 loads");

  // First, load 8 bits into 32 bits, then truncate to 1 bit.

  SDLoc dl(Op);
  LoadSDNode *LD = cast<LoadSDNode>(Op);

  SDValue Chain = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  MachineMemOperand *MMO = LD->getMemOperand();

  SDValue NewLD =
      DAG.getExtLoad(ISD::EXTLOAD, dl, getPointerTy(DAG.getDataLayout()), Chain,
                     BasePtr, MVT::i8, MMO);
  SDValue Result = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, NewLD);

  SDValue Ops[] = { Result, SDValue(NewLD.getNode(), 1) };
  return DAG.getMergeValues(Ops, dl);
}

SDValue PPCTargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getOperand(1).getValueType().isVector())
    return LowerVectorStore(Op, DAG);

  assert(Op.getOperand(1).getValueType() == MVT::i1 &&
         "Custom lowering only for i1 stores");

  // First, zero extend to 32 bits, then use a truncating store to 8 bits.

  SDLoc dl(Op);
  StoreSDNode *ST = cast<StoreSDNode>(Op);

  SDValue Chain = ST->getChain();
  SDValue BasePtr = ST->getBasePtr();
  SDValue Value = ST->getValue();
  MachineMemOperand *MMO = ST->getMemOperand();

  Value = DAG.getNode(ISD::ZERO_EXTEND, dl, getPointerTy(DAG.getDataLayout()),
                      Value);
  return DAG.getTruncStore(Chain, dl, Value, BasePtr, MVT::i8, MMO);
}

// FIXME: Remove this once the ANDI glue bug is fixed:
SDValue PPCTargetLowering::LowerTRUNCATE(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getValueType() == MVT::i1 &&
         "Custom lowering only for i1 results");

  SDLoc DL(Op);
  return DAG.getNode(PPCISD::ANDI_rec_1_GT_BIT, DL, MVT::i1, Op.getOperand(0));
}

SDValue PPCTargetLowering::LowerTRUNCATEVector(SDValue Op,
                                               SelectionDAG &DAG) const {

  // Implements a vector truncate that fits in a vector register as a shuffle.
  // We want to legalize vector truncates down to where the source fits in
  // a vector register (and target is therefore smaller than vector register
  // size).  At that point legalization will try to custom lower the sub-legal
  // result and get here - where we can contain the truncate as a single target
  // operation.

  // For example a trunc <2 x i16> to <2 x i8> could be visualized as follows:
  //   <MSB1|LSB1, MSB2|LSB2> to <LSB1, LSB2>
  //
  // We will implement it for big-endian ordering as this (where x denotes
  // undefined):
  //   < MSB1|LSB1, MSB2|LSB2, uu, uu, uu, uu, uu, uu> to
  //   < LSB1, LSB2, u, u, u, u, u, u, u, u, u, u, u, u, u, u>
  //
  // The same operation in little-endian ordering will be:
  //   <uu, uu, uu, uu, uu, uu, LSB2|MSB2, LSB1|MSB1> to
  //   <u, u, u, u, u, u, u, u, u, u, u, u, u, u, LSB2, LSB1>

  EVT TrgVT = Op.getValueType();
  assert(TrgVT.isVector() && "Vector type expected.");
  unsigned TrgNumElts = TrgVT.getVectorNumElements();
  EVT EltVT = TrgVT.getVectorElementType();
  if (!isOperationCustom(Op.getOpcode(), TrgVT) ||
      TrgVT.getSizeInBits() > 128 || !isPowerOf2_32(TrgNumElts) ||
      !llvm::has_single_bit<uint32_t>(EltVT.getSizeInBits()))
    return SDValue();

  SDValue N1 = Op.getOperand(0);
  EVT SrcVT = N1.getValueType();  
  unsigned SrcSize = SrcVT.getSizeInBits();
  if (SrcSize > 256 || !isPowerOf2_32(SrcVT.getVectorNumElements()) ||
      !llvm::has_single_bit<uint32_t>(
          SrcVT.getVectorElementType().getSizeInBits()))
    return SDValue();
  if (SrcSize == 256 && SrcVT.getVectorNumElements() < 2)
    return SDValue();

  unsigned WideNumElts = 128 / EltVT.getSizeInBits();
  EVT WideVT = EVT::getVectorVT(*DAG.getContext(), EltVT, WideNumElts);

  SDLoc DL(Op);
  SDValue Op1, Op2;
  if (SrcSize == 256) {
    EVT VecIdxTy = getVectorIdxTy(DAG.getDataLayout());
    EVT SplitVT =
        N1.getValueType().getHalfNumVectorElementsVT(*DAG.getContext());
    unsigned SplitNumElts = SplitVT.getVectorNumElements();
    Op1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SplitVT, N1,
                      DAG.getConstant(0, DL, VecIdxTy));
    Op2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SplitVT, N1,
                      DAG.getConstant(SplitNumElts, DL, VecIdxTy));
  }
  else {
    Op1 = SrcSize == 128 ? N1 : widenVec(DAG, N1, DL);
    Op2 = DAG.getUNDEF(WideVT);
  }

  // First list the elements we want to keep.
  unsigned SizeMult = SrcSize / TrgVT.getSizeInBits();
  SmallVector<int, 16> ShuffV;
  if (Subtarget.isLittleEndian())
    for (unsigned i = 0; i < TrgNumElts; ++i)
      ShuffV.push_back(i * SizeMult);
  else
    for (unsigned i = 1; i <= TrgNumElts; ++i)
      ShuffV.push_back(i * SizeMult - 1);

  // Populate the remaining elements with undefs.
  for (unsigned i = TrgNumElts; i < WideNumElts; ++i)
    // ShuffV.push_back(i + WideNumElts);
    ShuffV.push_back(WideNumElts + 1);

  Op1 = DAG.getNode(ISD::BITCAST, DL, WideVT, Op1);
  Op2 = DAG.getNode(ISD::BITCAST, DL, WideVT, Op2);
  return DAG.getVectorShuffle(WideVT, DL, Op1, Op2, ShuffV);
}

/// LowerSELECT_CC - Lower floating point select_cc's into fsel instruction when
/// possible.
SDValue PPCTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  EVT ResVT = Op.getValueType();
  EVT CmpVT = Op.getOperand(0).getValueType();
  SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
  SDValue TV  = Op.getOperand(2), FV  = Op.getOperand(3);
  SDLoc dl(Op);

  // Without power9-vector, we don't have native instruction for f128 comparison.
  // Following transformation to libcall is needed for setcc:
  // select_cc lhs, rhs, tv, fv, cc -> select_cc (setcc cc, x, y), 0, tv, fv, NE
  if (!Subtarget.hasP9Vector() && CmpVT == MVT::f128) {
    SDValue Z = DAG.getSetCC(
        dl, getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), CmpVT),
        LHS, RHS, CC);
    SDValue Zero = DAG.getConstant(0, dl, Z.getValueType());
    return DAG.getSelectCC(dl, Z, Zero, TV, FV, ISD::SETNE);
  }

  // Not FP, or using SPE? Not a fsel.
  if (!CmpVT.isFloatingPoint() || !TV.getValueType().isFloatingPoint() ||
      Subtarget.hasSPE())
    return Op;

  SDNodeFlags Flags = Op.getNode()->getFlags();

  // We have xsmaxc[dq]p/xsminc[dq]p which are OK to emit even in the
  // presence of infinities.
  if (Subtarget.hasP9Vector() && LHS == TV && RHS == FV) {
    switch (CC) {
    default:
      break;
    case ISD::SETOGT:
    case ISD::SETGT:
      return DAG.getNode(PPCISD::XSMAXC, dl, Op.getValueType(), LHS, RHS);
    case ISD::SETOLT:
    case ISD::SETLT:
      return DAG.getNode(PPCISD::XSMINC, dl, Op.getValueType(), LHS, RHS);
    }
  }

  // We might be able to do better than this under some circumstances, but in
  // general, fsel-based lowering of select is a finite-math-only optimization.
  // For more information, see section F.3 of the 2.06 ISA specification.
  // With ISA 3.0
  if ((!DAG.getTarget().Options.NoInfsFPMath && !Flags.hasNoInfs()) ||
      (!DAG.getTarget().Options.NoNaNsFPMath && !Flags.hasNoNaNs()) ||
      ResVT == MVT::f128)
    return Op;

  // If the RHS of the comparison is a 0.0, we don't need to do the
  // subtraction at all.
  SDValue Sel1;
  if (isFloatingPointZero(RHS))
    switch (CC) {
    default: break;       // SETUO etc aren't handled by fsel.
    case ISD::SETNE:
      std::swap(TV, FV);
      [[fallthrough]];
    case ISD::SETEQ:
      if (LHS.getValueType() == MVT::f32)   // Comparison is always 64-bits
        LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, LHS);
      Sel1 = DAG.getNode(PPCISD::FSEL, dl, ResVT, LHS, TV, FV);
      if (Sel1.getValueType() == MVT::f32)   // Comparison is always 64-bits
        Sel1 = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Sel1);
      return DAG.getNode(PPCISD::FSEL, dl, ResVT,
                         DAG.getNode(ISD::FNEG, dl, MVT::f64, LHS), Sel1, FV);
    case ISD::SETULT:
    case ISD::SETLT:
      std::swap(TV, FV);  // fsel is natively setge, swap operands for setlt
      [[fallthrough]];
    case ISD::SETOGE:
    case ISD::SETGE:
      if (LHS.getValueType() == MVT::f32)   // Comparison is always 64-bits
        LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, LHS);
      return DAG.getNode(PPCISD::FSEL, dl, ResVT, LHS, TV, FV);
    case ISD::SETUGT:
    case ISD::SETGT:
      std::swap(TV, FV);  // fsel is natively setge, swap operands for setlt
      [[fallthrough]];
    case ISD::SETOLE:
    case ISD::SETLE:
      if (LHS.getValueType() == MVT::f32)   // Comparison is always 64-bits
        LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, LHS);
      return DAG.getNode(PPCISD::FSEL, dl, ResVT,
                         DAG.getNode(ISD::FNEG, dl, MVT::f64, LHS), TV, FV);
    }

  SDValue Cmp;
  switch (CC) {
  default: break;       // SETUO etc aren't handled by fsel.
  case ISD::SETNE:
    std::swap(TV, FV);
    [[fallthrough]];
  case ISD::SETEQ:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, LHS, RHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    Sel1 = DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, TV, FV);
    if (Sel1.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Sel1 = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Sel1);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT,
                       DAG.getNode(ISD::FNEG, dl, MVT::f64, Cmp), Sel1, FV);
  case ISD::SETULT:
  case ISD::SETLT:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, LHS, RHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, FV, TV);
  case ISD::SETOGE:
  case ISD::SETGE:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, LHS, RHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, TV, FV);
  case ISD::SETUGT:
  case ISD::SETGT:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, RHS, LHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, FV, TV);
  case ISD::SETOLE:
  case ISD::SETLE:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, RHS, LHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, TV, FV);
  }
  return Op;
}

static unsigned getPPCStrictOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("No strict version of this opcode!");
  case PPCISD::FCTIDZ:
    return PPCISD::STRICT_FCTIDZ;
  case PPCISD::FCTIWZ:
    return PPCISD::STRICT_FCTIWZ;
  case PPCISD::FCTIDUZ:
    return PPCISD::STRICT_FCTIDUZ;
  case PPCISD::FCTIWUZ:
    return PPCISD::STRICT_FCTIWUZ;
  case PPCISD::FCFID:
    return PPCISD::STRICT_FCFID;
  case PPCISD::FCFIDU:
    return PPCISD::STRICT_FCFIDU;
  case PPCISD::FCFIDS:
    return PPCISD::STRICT_FCFIDS;
  case PPCISD::FCFIDUS:
    return PPCISD::STRICT_FCFIDUS;
  }
}

static SDValue convertFPToInt(SDValue Op, SelectionDAG &DAG,
                              const PPCSubtarget &Subtarget) {
  SDLoc dl(Op);
  bool IsStrict = Op->isStrictFPOpcode();
  bool IsSigned = Op.getOpcode() == ISD::FP_TO_SINT ||
                  Op.getOpcode() == ISD::STRICT_FP_TO_SINT;

  // TODO: Any other flags to propagate?
  SDNodeFlags Flags;
  Flags.setNoFPExcept(Op->getFlags().hasNoFPExcept());

  // For strict nodes, source is the second operand.
  SDValue Src = Op.getOperand(IsStrict ? 1 : 0);
  SDValue Chain = IsStrict ? Op.getOperand(0) : SDValue();
  MVT DestTy = Op.getSimpleValueType();
  assert(Src.getValueType().isFloatingPoint() &&
         (DestTy == MVT::i8 || DestTy == MVT::i16 || DestTy == MVT::i32 ||
          DestTy == MVT::i64) &&
         "Invalid FP_TO_INT types");
  if (Src.getValueType() == MVT::f32) {
    if (IsStrict) {
      Src =
          DAG.getNode(ISD::STRICT_FP_EXTEND, dl,
                      DAG.getVTList(MVT::f64, MVT::Other), {Chain, Src}, Flags);
      Chain = Src.getValue(1);
    } else
      Src = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Src);
  }
  if ((DestTy == MVT::i8 || DestTy == MVT::i16) && Subtarget.hasP9Vector())
    DestTy = Subtarget.isPPC64() ? MVT::i64 : MVT::i32;
  unsigned Opc = ISD::DELETED_NODE;
  switch (DestTy.SimpleTy) {
  default: llvm_unreachable("Unhandled FP_TO_INT type in custom expander!");
  case MVT::i32:
    Opc = IsSigned ? PPCISD::FCTIWZ
                   : (Subtarget.hasFPCVT() ? PPCISD::FCTIWUZ : PPCISD::FCTIDZ);
    break;
  case MVT::i64:
    assert((IsSigned || Subtarget.hasFPCVT()) &&
           "i64 FP_TO_UINT is supported only with FPCVT");
    Opc = IsSigned ? PPCISD::FCTIDZ : PPCISD::FCTIDUZ;
  }
  EVT ConvTy = Src.getValueType() == MVT::f128 ? MVT::f128 : MVT::f64;
  SDValue Conv;
  if (IsStrict) {
    Opc = getPPCStrictOpcode(Opc);
    Conv = DAG.getNode(Opc, dl, DAG.getVTList(ConvTy, MVT::Other), {Chain, Src},
                       Flags);
  } else {
    Conv = DAG.getNode(Opc, dl, ConvTy, Src);
  }
  return Conv;
}

void PPCTargetLowering::LowerFP_TO_INTForReuse(SDValue Op, ReuseLoadInfo &RLI,
                                               SelectionDAG &DAG,
                                               const SDLoc &dl) const {
  SDValue Tmp = convertFPToInt(Op, DAG, Subtarget);
  bool IsSigned = Op.getOpcode() == ISD::FP_TO_SINT ||
                  Op.getOpcode() == ISD::STRICT_FP_TO_SINT;
  bool IsStrict = Op->isStrictFPOpcode();

  // Convert the FP value to an int value through memory.
  bool i32Stack = Op.getValueType() == MVT::i32 && Subtarget.hasSTFIWX() &&
                  (IsSigned || Subtarget.hasFPCVT());
  SDValue FIPtr = DAG.CreateStackTemporary(i32Stack ? MVT::i32 : MVT::f64);
  int FI = cast<FrameIndexSDNode>(FIPtr)->getIndex();
  MachinePointerInfo MPI =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

  // Emit a store to the stack slot.
  SDValue Chain = IsStrict ? Tmp.getValue(1) : DAG.getEntryNode();
  Align Alignment(DAG.getEVTAlign(Tmp.getValueType()));
  if (i32Stack) {
    MachineFunction &MF = DAG.getMachineFunction();
    Alignment = Align(4);
    MachineMemOperand *MMO =
        MF.getMachineMemOperand(MPI, MachineMemOperand::MOStore, 4, Alignment);
    SDValue Ops[] = { Chain, Tmp, FIPtr };
    Chain = DAG.getMemIntrinsicNode(PPCISD::STFIWX, dl,
              DAG.getVTList(MVT::Other), Ops, MVT::i32, MMO);
  } else
    Chain = DAG.getStore(Chain, dl, Tmp, FIPtr, MPI, Alignment);

  // Result is a load from the stack slot.  If loading 4 bytes, make sure to
  // add in a bias on big endian.
  if (Op.getValueType() == MVT::i32 && !i32Stack) {
    FIPtr = DAG.getNode(ISD::ADD, dl, FIPtr.getValueType(), FIPtr,
                        DAG.getConstant(4, dl, FIPtr.getValueType()));
    MPI = MPI.getWithOffset(Subtarget.isLittleEndian() ? 0 : 4);
  }

  RLI.Chain = Chain;
  RLI.Ptr = FIPtr;
  RLI.MPI = MPI;
  RLI.Alignment = Alignment;
}

/// Custom lowers floating point to integer conversions to use
/// the direct move instructions available in ISA 2.07 to avoid the
/// need for load/store combinations.
SDValue PPCTargetLowering::LowerFP_TO_INTDirectMove(SDValue Op,
                                                    SelectionDAG &DAG,
                                                    const SDLoc &dl) const {
  SDValue Conv = convertFPToInt(Op, DAG, Subtarget);
  SDValue Mov = DAG.getNode(PPCISD::MFVSR, dl, Op.getValueType(), Conv);
  if (Op->isStrictFPOpcode())
    return DAG.getMergeValues({Mov, Conv.getValue(1)}, dl);
  else
    return Mov;
}

SDValue PPCTargetLowering::LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG,
                                          const SDLoc &dl) const {
  bool IsStrict = Op->isStrictFPOpcode();
  bool IsSigned = Op.getOpcode() == ISD::FP_TO_SINT ||
                  Op.getOpcode() == ISD::STRICT_FP_TO_SINT;
  SDValue Src = Op.getOperand(IsStrict ? 1 : 0);
  EVT SrcVT = Src.getValueType();
  EVT DstVT = Op.getValueType();

  // FP to INT conversions are legal for f128.
  if (SrcVT == MVT::f128)
    return Subtarget.hasP9Vector() ? Op : SDValue();

  // Expand ppcf128 to i32 by hand for the benefit of llvm-gcc bootstrap on
  // PPC (the libcall is not available).
  if (SrcVT == MVT::ppcf128) {
    if (DstVT == MVT::i32) {
      // TODO: Conservatively pass only nofpexcept flag here. Need to check and
      // set other fast-math flags to FP operations in both strict and
      // non-strict cases. (FP_TO_SINT, FSUB)
      SDNodeFlags Flags;
      Flags.setNoFPExcept(Op->getFlags().hasNoFPExcept());

      if (IsSigned) {
        SDValue Lo, Hi;
        std::tie(Lo, Hi) = DAG.SplitScalar(Src, dl, MVT::f64, MVT::f64);

        // Add the two halves of the long double in round-to-zero mode, and use
        // a smaller FP_TO_SINT.
        if (IsStrict) {
          SDValue Res = DAG.getNode(PPCISD::STRICT_FADDRTZ, dl,
                                    DAG.getVTList(MVT::f64, MVT::Other),
                                    {Op.getOperand(0), Lo, Hi}, Flags);
          return DAG.getNode(ISD::STRICT_FP_TO_SINT, dl,
                             DAG.getVTList(MVT::i32, MVT::Other),
                             {Res.getValue(1), Res}, Flags);
        } else {
          SDValue Res = DAG.getNode(PPCISD::FADDRTZ, dl, MVT::f64, Lo, Hi);
          return DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32, Res);
        }
      } else {
        const uint64_t TwoE31[] = {0x41e0000000000000LL, 0};
        APFloat APF = APFloat(APFloat::PPCDoubleDouble(), APInt(128, TwoE31));
        SDValue Cst = DAG.getConstantFP(APF, dl, SrcVT);
        SDValue SignMask = DAG.getConstant(0x80000000, dl, DstVT);
        if (IsStrict) {
          // Sel = Src < 0x80000000
          // FltOfs = select Sel, 0.0, 0x80000000
          // IntOfs = select Sel, 0, 0x80000000
          // Result = fp_to_sint(Src - FltOfs) ^ IntOfs
          SDValue Chain = Op.getOperand(0);
          EVT SetCCVT =
              getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), SrcVT);
          EVT DstSetCCVT =
              getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), DstVT);
          SDValue Sel = DAG.getSetCC(dl, SetCCVT, Src, Cst, ISD::SETLT,
                                     Chain, true);
          Chain = Sel.getValue(1);

          SDValue FltOfs = DAG.getSelect(
              dl, SrcVT, Sel, DAG.getConstantFP(0.0, dl, SrcVT), Cst);
          Sel = DAG.getBoolExtOrTrunc(Sel, dl, DstSetCCVT, DstVT);

          SDValue Val = DAG.getNode(ISD::STRICT_FSUB, dl,
                                    DAG.getVTList(SrcVT, MVT::Other),
                                    {Chain, Src, FltOfs}, Flags);
          Chain = Val.getValue(1);
          SDValue SInt = DAG.getNode(ISD::STRICT_FP_TO_SINT, dl,
                                     DAG.getVTList(DstVT, MVT::Other),
                                     {Chain, Val}, Flags);
          Chain = SInt.getValue(1);
          SDValue IntOfs = DAG.getSelect(
              dl, DstVT, Sel, DAG.getConstant(0, dl, DstVT), SignMask);
          SDValue Result = DAG.getNode(ISD::XOR, dl, DstVT, SInt, IntOfs);
          return DAG.getMergeValues({Result, Chain}, dl);
        } else {
          // X>=2^31 ? (int)(X-2^31)+0x80000000 : (int)X
          // FIXME: generated code sucks.
          SDValue True = DAG.getNode(ISD::FSUB, dl, MVT::ppcf128, Src, Cst);
          True = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32, True);
          True = DAG.getNode(ISD::ADD, dl, MVT::i32, True, SignMask);
          SDValue False = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32, Src);
          return DAG.getSelectCC(dl, Src, Cst, True, False, ISD::SETGE);
        }
      }
    }

    return SDValue();
  }

  if (Subtarget.hasDirectMove() && Subtarget.isPPC64())
    return LowerFP_TO_INTDirectMove(Op, DAG, dl);

  ReuseLoadInfo RLI;
  LowerFP_TO_INTForReuse(Op, RLI, DAG, dl);

  return DAG.getLoad(Op.getValueType(), dl, RLI.Chain, RLI.Ptr, RLI.MPI,
                     RLI.Alignment, RLI.MMOFlags(), RLI.AAInfo, RLI.Ranges);
}

// We're trying to insert a regular store, S, and then a load, L. If the
// incoming value, O, is a load, we might just be able to have our load use the
// address used by O. However, we don't know if anything else will store to
// that address before we can load from it. To prevent this situation, we need
// to insert our load, L, into the chain as a peer of O. To do this, we give L
// the same chain operand as O, we create a token factor from the chain results
// of O and L, and we replace all uses of O's chain result with that token
// factor (see spliceIntoChain below for this last part).
bool PPCTargetLowering::canReuseLoadAddress(SDValue Op, EVT MemVT,
                                            ReuseLoadInfo &RLI,
                                            SelectionDAG &DAG,
                                            ISD::LoadExtType ET) const {
  // Conservatively skip reusing for constrained FP nodes.
  if (Op->isStrictFPOpcode())
    return false;

  SDLoc dl(Op);
  bool ValidFPToUint = Op.getOpcode() == ISD::FP_TO_UINT &&
                       (Subtarget.hasFPCVT() || Op.getValueType() == MVT::i32);
  if (ET == ISD::NON_EXTLOAD &&
      (ValidFPToUint || Op.getOpcode() == ISD::FP_TO_SINT) &&
      isOperationLegalOrCustom(Op.getOpcode(),
                               Op.getOperand(0).getValueType())) {

    LowerFP_TO_INTForReuse(Op, RLI, DAG, dl);
    return true;
  }

  LoadSDNode *LD = dyn_cast<LoadSDNode>(Op);
  if (!LD || LD->getExtensionType() != ET || LD->isVolatile() ||
      LD->isNonTemporal())
    return false;
  if (LD->getMemoryVT() != MemVT)
    return false;

  // If the result of the load is an illegal type, then we can't build a
  // valid chain for reuse since the legalised loads and token factor node that
  // ties the legalised loads together uses a different output chain then the
  // illegal load.
  if (!isTypeLegal(LD->getValueType(0)))
    return false;

  RLI.Ptr = LD->getBasePtr();
  if (LD->isIndexed() && !LD->getOffset().isUndef()) {
    assert(LD->getAddressingMode() == ISD::PRE_INC &&
           "Non-pre-inc AM on PPC?");
    RLI.Ptr = DAG.getNode(ISD::ADD, dl, RLI.Ptr.getValueType(), RLI.Ptr,
                          LD->getOffset());
  }

  RLI.Chain = LD->getChain();
  RLI.MPI = LD->getPointerInfo();
  RLI.IsDereferenceable = LD->isDereferenceable();
  RLI.IsInvariant = LD->isInvariant();
  RLI.Alignment = LD->getAlign();
  RLI.AAInfo = LD->getAAInfo();
  RLI.Ranges = LD->getRanges();

  RLI.ResChain = SDValue(LD, LD->isIndexed() ? 2 : 1);
  return true;
}

// Given the head of the old chain, ResChain, insert a token factor containing
// it and NewResChain, and make users of ResChain now be users of that token
// factor.
// TODO: Remove and use DAG::makeEquivalentMemoryOrdering() instead.
void PPCTargetLowering::spliceIntoChain(SDValue ResChain,
                                        SDValue NewResChain,
                                        SelectionDAG &DAG) const {
  if (!ResChain)
    return;

  SDLoc dl(NewResChain);

  SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                           NewResChain, DAG.getUNDEF(MVT::Other));
  assert(TF.getNode() != NewResChain.getNode() &&
         "A new TF really is required here");

  DAG.ReplaceAllUsesOfValueWith(ResChain, TF);
  DAG.UpdateNodeOperands(TF.getNode(), ResChain, NewResChain);
}

/// Analyze profitability of direct move
/// prefer float load to int load plus direct move
/// when there is no integer use of int load
bool PPCTargetLowering::directMoveIsProfitable(const SDValue &Op) const {
  SDNode *Origin = Op.getOperand(Op->isStrictFPOpcode() ? 1 : 0).getNode();
  if (Origin->getOpcode() != ISD::LOAD)
    return true;

  // If there is no LXSIBZX/LXSIHZX, like Power8,
  // prefer direct move if the memory size is 1 or 2 bytes.
  MachineMemOperand *MMO = cast<LoadSDNode>(Origin)->getMemOperand();
  if (!Subtarget.hasP9Vector() &&
      (!MMO->getSize().hasValue() || MMO->getSize().getValue() <= 2))
    return true;

  for (SDNode::use_iterator UI = Origin->use_begin(),
                            UE = Origin->use_end();
       UI != UE; ++UI) {

    // Only look at the users of the loaded value.
    if (UI.getUse().get().getResNo() != 0)
      continue;

    if (UI->getOpcode() != ISD::SINT_TO_FP &&
        UI->getOpcode() != ISD::UINT_TO_FP &&
        UI->getOpcode() != ISD::STRICT_SINT_TO_FP &&
        UI->getOpcode() != ISD::STRICT_UINT_TO_FP)
      return true;
  }

  return false;
}

static SDValue convertIntToFP(SDValue Op, SDValue Src, SelectionDAG &DAG,
                              const PPCSubtarget &Subtarget,
                              SDValue Chain = SDValue()) {
  bool IsSigned = Op.getOpcode() == ISD::SINT_TO_FP ||
                  Op.getOpcode() == ISD::STRICT_SINT_TO_FP;
  SDLoc dl(Op);

  // TODO: Any other flags to propagate?
  SDNodeFlags Flags;
  Flags.setNoFPExcept(Op->getFlags().hasNoFPExcept());

  // If we have FCFIDS, then use it when converting to single-precision.
  // Otherwise, convert to double-precision and then round.
  bool IsSingle = Op.getValueType() == MVT::f32 && Subtarget.hasFPCVT();
  unsigned ConvOpc = IsSingle ? (IsSigned ? PPCISD::FCFIDS : PPCISD::FCFIDUS)
                              : (IsSigned ? PPCISD::FCFID : PPCISD::FCFIDU);
  EVT ConvTy = IsSingle ? MVT::f32 : MVT::f64;
  if (Op->isStrictFPOpcode()) {
    if (!Chain)
      Chain = Op.getOperand(0);
    return DAG.getNode(getPPCStrictOpcode(ConvOpc), dl,
                       DAG.getVTList(ConvTy, MVT::Other), {Chain, Src}, Flags);
  } else
    return DAG.getNode(ConvOpc, dl, ConvTy, Src);
}

/// Custom lowers integer to floating point conversions to use
/// the direct move instructions available in ISA 2.07 to avoid the
/// need for load/store combinations.
SDValue PPCTargetLowering::LowerINT_TO_FPDirectMove(SDValue Op,
                                                    SelectionDAG &DAG,
                                                    const SDLoc &dl) const {
  assert((Op.getValueType() == MVT::f32 ||
          Op.getValueType() == MVT::f64) &&
         "Invalid floating point type as target of conversion");
  assert(Subtarget.hasFPCVT() &&
         "Int to FP conversions with direct moves require FPCVT");
  SDValue Src = Op.getOperand(Op->isStrictFPOpcode() ? 1 : 0);
  bool WordInt = Src.getSimpleValueType().SimpleTy == MVT::i32;
  bool Signed = Op.getOpcode() == ISD::SINT_TO_FP ||
                Op.getOpcode() == ISD::STRICT_SINT_TO_FP;
  unsigned MovOpc = (WordInt && !Signed) ? PPCISD::MTVSRZ : PPCISD::MTVSRA;
  SDValue Mov = DAG.getNode(MovOpc, dl, MVT::f64, Src);
  return convertIntToFP(Op, Mov, DAG, Subtarget);
}

static SDValue widenVec(SelectionDAG &DAG, SDValue Vec, const SDLoc &dl) {

  EVT VecVT = Vec.getValueType();
  assert(VecVT.isVector() && "Expected a vector type.");
  assert(VecVT.getSizeInBits() < 128 && "Vector is already full width.");

  EVT EltVT = VecVT.getVectorElementType();
  unsigned WideNumElts = 128 / EltVT.getSizeInBits();
  EVT WideVT = EVT::getVectorVT(*DAG.getContext(), EltVT, WideNumElts);

  unsigned NumConcat = WideNumElts / VecVT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(NumConcat);
  Ops[0] = Vec;
  SDValue UndefVec = DAG.getUNDEF(VecVT);
  for (unsigned i = 1; i < NumConcat; ++i)
    Ops[i] = UndefVec;

  return DAG.getNode(ISD::CONCAT_VECTORS, dl, WideVT, Ops);
}

SDValue PPCTargetLowering::LowerINT_TO_FPVector(SDValue Op, SelectionDAG &DAG,
                                                const SDLoc &dl) const {
  bool IsStrict = Op->isStrictFPOpcode();
  unsigned Opc = Op.getOpcode();
  SDValue Src = Op.getOperand(IsStrict ? 1 : 0);
  assert((Opc == ISD::UINT_TO_FP || Opc == ISD::SINT_TO_FP ||
          Opc == ISD::STRICT_UINT_TO_FP || Opc == ISD::STRICT_SINT_TO_FP) &&
         "Unexpected conversion type");
  assert((Op.getValueType() == MVT::v2f64 || Op.getValueType() == MVT::v4f32) &&
         "Supports conversions to v2f64/v4f32 only.");

  // TODO: Any other flags to propagate?
  SDNodeFlags Flags;
  Flags.setNoFPExcept(Op->getFlags().hasNoFPExcept());

  bool SignedConv = Opc == ISD::SINT_TO_FP || Opc == ISD::STRICT_SINT_TO_FP;
  bool FourEltRes = Op.getValueType() == MVT::v4f32;

  SDValue Wide = widenVec(DAG, Src, dl);
  EVT WideVT = Wide.getValueType();
  unsigned WideNumElts = WideVT.getVectorNumElements();
  MVT IntermediateVT = FourEltRes ? MVT::v4i32 : MVT::v2i64;

  SmallVector<int, 16> ShuffV;
  for (unsigned i = 0; i < WideNumElts; ++i)
    ShuffV.push_back(i + WideNumElts);

  int Stride = FourEltRes ? WideNumElts / 4 : WideNumElts / 2;
  int SaveElts = FourEltRes ? 4 : 2;
  if (Subtarget.isLittleEndian())
    for (int i = 0; i < SaveElts; i++)
      ShuffV[i * Stride] = i;
  else
    for (int i = 1; i <= SaveElts; i++)
      ShuffV[i * Stride - 1] = i - 1;

  SDValue ShuffleSrc2 =
      SignedConv ? DAG.getUNDEF(WideVT) : DAG.getConstant(0, dl, WideVT);
  SDValue Arrange = DAG.getVectorShuffle(WideVT, dl, Wide, ShuffleSrc2, ShuffV);

  SDValue Extend;
  if (SignedConv) {
    Arrange = DAG.getBitcast(IntermediateVT, Arrange);
    EVT ExtVT = Src.getValueType();
    if (Subtarget.hasP9Altivec())
      ExtVT = EVT::getVectorVT(*DAG.getContext(), WideVT.getVectorElementType(),
                               IntermediateVT.getVectorNumElements());

    Extend = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, IntermediateVT, Arrange,
                         DAG.getValueType(ExtVT));
  } else
    Extend = DAG.getNode(ISD::BITCAST, dl, IntermediateVT, Arrange);

  if (IsStrict)
    return DAG.getNode(Opc, dl, DAG.getVTList(Op.getValueType(), MVT::Other),
                       {Op.getOperand(0), Extend}, Flags);

  return DAG.getNode(Opc, dl, Op.getValueType(), Extend);
}

SDValue PPCTargetLowering::LowerINT_TO_FP(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDLoc dl(Op);
  bool IsSigned = Op.getOpcode() == ISD::SINT_TO_FP ||
                  Op.getOpcode() == ISD::STRICT_SINT_TO_FP;
  bool IsStrict = Op->isStrictFPOpcode();
  SDValue Src = Op.getOperand(IsStrict ? 1 : 0);
  SDValue Chain = IsStrict ? Op.getOperand(0) : DAG.getEntryNode();

  // TODO: Any other flags to propagate?
  SDNodeFlags Flags;
  Flags.setNoFPExcept(Op->getFlags().hasNoFPExcept());

  EVT InVT = Src.getValueType();
  EVT OutVT = Op.getValueType();
  if (OutVT.isVector() && OutVT.isFloatingPoint() &&
      isOperationCustom(Op.getOpcode(), InVT))
    return LowerINT_TO_FPVector(Op, DAG, dl);

  // Conversions to f128 are legal.
  if (Op.getValueType() == MVT::f128)
    return Subtarget.hasP9Vector() ? Op : SDValue();

  // Don't handle ppc_fp128 here; let it be lowered to a libcall.
  if (Op.getValueType() != MVT::f32 && Op.getValueType() != MVT::f64)
    return SDValue();

  if (Src.getValueType() == MVT::i1) {
    SDValue Sel = DAG.getNode(ISD::SELECT, dl, Op.getValueType(), Src,
                              DAG.getConstantFP(1.0, dl, Op.getValueType()),
                              DAG.getConstantFP(0.0, dl, Op.getValueType()));
    if (IsStrict)
      return DAG.getMergeValues({Sel, Chain}, dl);
    else
      return Sel;
  }

  // If we have direct moves, we can do all the conversion, skip the store/load
  // however, without FPCVT we can't do most conversions.
  if (Subtarget.hasDirectMove() && directMoveIsProfitable(Op) &&
      Subtarget.isPPC64() && Subtarget.hasFPCVT())
    return LowerINT_TO_FPDirectMove(Op, DAG, dl);

  assert((IsSigned || Subtarget.hasFPCVT()) &&
         "UINT_TO_FP is supported only with FPCVT");

  if (Src.getValueType() == MVT::i64) {
    SDValue SINT = Src;
    // When converting to single-precision, we actually need to convert
    // to double-precision first and then round to single-precision.
    // To avoid double-rounding effects during that operation, we have
    // to prepare the input operand.  Bits that might be truncated when
    // converting to double-precision are replaced by a bit that won't
    // be lost at this stage, but is below the single-precision rounding
    // position.
    //
    // However, if -enable-unsafe-fp-math is in effect, accept double
    // rounding to avoid the extra overhead.
    if (Op.getValueType() == MVT::f32 &&
        !Subtarget.hasFPCVT() &&
        !DAG.getTarget().Options.UnsafeFPMath) {

      // Twiddle input to make sure the low 11 bits are zero.  (If this
      // is the case, we are guaranteed the value will fit into the 53 bit
      // mantissa of an IEEE double-precision value without rounding.)
      // If any of those low 11 bits were not zero originally, make sure
      // bit 12 (value 2048) is set instead, so that the final rounding
      // to single-precision gets the correct result.
      SDValue Round = DAG.getNode(ISD::AND, dl, MVT::i64,
                                  SINT, DAG.getConstant(2047, dl, MVT::i64));
      Round = DAG.getNode(ISD::ADD, dl, MVT::i64,
                          Round, DAG.getConstant(2047, dl, MVT::i64));
      Round = DAG.getNode(ISD::OR, dl, MVT::i64, Round, SINT);
      Round = DAG.getNode(ISD::AND, dl, MVT::i64,
                          Round, DAG.getConstant(-2048, dl, MVT::i64));

      // However, we cannot use that value unconditionally: if the magnitude
      // of the input value is small, the bit-twiddling we did above might
      // end up visibly changing the output.  Fortunately, in that case, we
      // don't need to twiddle bits since the original input will convert
      // exactly to double-precision floating-point already.  Therefore,
      // construct a conditional to use the original value if the top 11
      // bits are all sign-bit copies, and use the rounded value computed
      // above otherwise.
      SDValue Cond = DAG.getNode(ISD::SRA, dl, MVT::i64,
                                 SINT, DAG.getConstant(53, dl, MVT::i32));
      Cond = DAG.getNode(ISD::ADD, dl, MVT::i64,
                         Cond, DAG.getConstant(1, dl, MVT::i64));
      Cond = DAG.getSetCC(
          dl,
          getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::i64),
          Cond, DAG.getConstant(1, dl, MVT::i64), ISD::SETUGT);

      SINT = DAG.getNode(ISD::SELECT, dl, MVT::i64, Cond, Round, SINT);
    }

    ReuseLoadInfo RLI;
    SDValue Bits;

    MachineFunction &MF = DAG.getMachineFunction();
    if (canReuseLoadAddress(SINT, MVT::i64, RLI, DAG)) {
      Bits = DAG.getLoad(MVT::f64, dl, RLI.Chain, RLI.Ptr, RLI.MPI,
                         RLI.Alignment, RLI.MMOFlags(), RLI.AAInfo, RLI.Ranges);
      spliceIntoChain(RLI.ResChain, Bits.getValue(1), DAG);
    } else if (Subtarget.hasLFIWAX() &&
               canReuseLoadAddress(SINT, MVT::i32, RLI, DAG, ISD::SEXTLOAD)) {
      MachineMemOperand *MMO =
        MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                                RLI.Alignment, RLI.AAInfo, RLI.Ranges);
      SDValue Ops[] = { RLI.Chain, RLI.Ptr };
      Bits = DAG.getMemIntrinsicNode(PPCISD::LFIWAX, dl,
                                     DAG.getVTList(MVT::f64, MVT::Other),
                                     Ops, MVT::i32, MMO);
      spliceIntoChain(RLI.ResChain, Bits.getValue(1), DAG);
    } else if (Subtarget.hasFPCVT() &&
               canReuseLoadAddress(SINT, MVT::i32, RLI, DAG, ISD::ZEXTLOAD)) {
      MachineMemOperand *MMO =
        MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                                RLI.Alignment, RLI.AAInfo, RLI.Ranges);
      SDValue Ops[] = { RLI.Chain, RLI.Ptr };
      Bits = DAG.getMemIntrinsicNode(PPCISD::LFIWZX, dl,
                                     DAG.getVTList(MVT::f64, MVT::Other),
                                     Ops, MVT::i32, MMO);
      spliceIntoChain(RLI.ResChain, Bits.getValue(1), DAG);
    } else if (((Subtarget.hasLFIWAX() &&
                 SINT.getOpcode() == ISD::SIGN_EXTEND) ||
                (Subtarget.hasFPCVT() &&
                 SINT.getOpcode() == ISD::ZERO_EXTEND)) &&
               SINT.getOperand(0).getValueType() == MVT::i32) {
      MachineFrameInfo &MFI = MF.getFrameInfo();
      EVT PtrVT = getPointerTy(DAG.getDataLayout());

      int FrameIdx = MFI.CreateStackObject(4, Align(4), false);
      SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

      SDValue Store = DAG.getStore(Chain, dl, SINT.getOperand(0), FIdx,
                                   MachinePointerInfo::getFixedStack(
                                       DAG.getMachineFunction(), FrameIdx));
      Chain = Store;

      assert(cast<StoreSDNode>(Store)->getMemoryVT() == MVT::i32 &&
             "Expected an i32 store");

      RLI.Ptr = FIdx;
      RLI.Chain = Chain;
      RLI.MPI =
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);
      RLI.Alignment = Align(4);

      MachineMemOperand *MMO =
        MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                                RLI.Alignment, RLI.AAInfo, RLI.Ranges);
      SDValue Ops[] = { RLI.Chain, RLI.Ptr };
      Bits = DAG.getMemIntrinsicNode(SINT.getOpcode() == ISD::ZERO_EXTEND ?
                                     PPCISD::LFIWZX : PPCISD::LFIWAX,
                                     dl, DAG.getVTList(MVT::f64, MVT::Other),
                                     Ops, MVT::i32, MMO);
      Chain = Bits.getValue(1);
    } else
      Bits = DAG.getNode(ISD::BITCAST, dl, MVT::f64, SINT);

    SDValue FP = convertIntToFP(Op, Bits, DAG, Subtarget, Chain);
    if (IsStrict)
      Chain = FP.getValue(1);

    if (Op.getValueType() == MVT::f32 && !Subtarget.hasFPCVT()) {
      if (IsStrict)
        FP = DAG.getNode(ISD::STRICT_FP_ROUND, dl,
                         DAG.getVTList(MVT::f32, MVT::Other),
                         {Chain, FP, DAG.getIntPtrConstant(0, dl)}, Flags);
      else
        FP = DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, FP,
                         DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
    }
    return FP;
  }

  assert(Src.getValueType() == MVT::i32 &&
         "Unhandled INT_TO_FP type in custom expander!");
  // Since we only generate this in 64-bit mode, we can take advantage of
  // 64-bit registers.  In particular, sign extend the input value into the
  // 64-bit register with extsw, store the WHOLE 64-bit value into the stack
  // then lfd it and fcfid it.
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  SDValue Ld;
  if (Subtarget.hasLFIWAX() || Subtarget.hasFPCVT()) {
    ReuseLoadInfo RLI;
    bool ReusingLoad;
    if (!(ReusingLoad = canReuseLoadAddress(Src, MVT::i32, RLI, DAG))) {
      int FrameIdx = MFI.CreateStackObject(4, Align(4), false);
      SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

      SDValue Store = DAG.getStore(Chain, dl, Src, FIdx,
                                   MachinePointerInfo::getFixedStack(
                                       DAG.getMachineFunction(), FrameIdx));
      Chain = Store;

      assert(cast<StoreSDNode>(Store)->getMemoryVT() == MVT::i32 &&
             "Expected an i32 store");

      RLI.Ptr = FIdx;
      RLI.Chain = Chain;
      RLI.MPI =
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);
      RLI.Alignment = Align(4);
    }

    MachineMemOperand *MMO =
      MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                              RLI.Alignment, RLI.AAInfo, RLI.Ranges);
    SDValue Ops[] = { RLI.Chain, RLI.Ptr };
    Ld = DAG.getMemIntrinsicNode(IsSigned ? PPCISD::LFIWAX : PPCISD::LFIWZX, dl,
                                 DAG.getVTList(MVT::f64, MVT::Other), Ops,
                                 MVT::i32, MMO);
    Chain = Ld.getValue(1);
    if (ReusingLoad)
      spliceIntoChain(RLI.ResChain, Ld.getValue(1), DAG);
  } else {
    assert(Subtarget.isPPC64() &&
           "i32->FP without LFIWAX supported only on PPC64");

    int FrameIdx = MFI.CreateStackObject(8, Align(8), false);
    SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

    SDValue Ext64 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::i64, Src);

    // STD the extended value into the stack slot.
    SDValue Store = DAG.getStore(
        Chain, dl, Ext64, FIdx,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx));
    Chain = Store;

    // Load the value as a double.
    Ld = DAG.getLoad(
        MVT::f64, dl, Chain, FIdx,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx));
    Chain = Ld.getValue(1);
  }

  // FCFID it and return it.
  SDValue FP = convertIntToFP(Op, Ld, DAG, Subtarget, Chain);
  if (IsStrict)
    Chain = FP.getValue(1);
  if (Op.getValueType() == MVT::f32 && !Subtarget.hasFPCVT()) {
    if (IsStrict)
      FP = DAG.getNode(ISD::STRICT_FP_ROUND, dl,
                       DAG.getVTList(MVT::f32, MVT::Other),
                       {Chain, FP, DAG.getIntPtrConstant(0, dl)}, Flags);
    else
      FP = DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, FP,
                       DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
  }
  return FP;
}

SDValue PPCTargetLowering::LowerGET_ROUNDING(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc dl(Op);
  /*
   The rounding mode is in bits 30:31 of FPSR, and has the following
   settings:
     00 Round to nearest
     01 Round to 0
     10 Round to +inf
     11 Round to -inf

  GET_ROUNDING, on the other hand, expects the following:
    -1 Undefined
     0 Round to 0
     1 Round to nearest
     2 Round to +inf
     3 Round to -inf

  To perform the conversion, we do:
    ((FPSCR & 0x3) ^ ((~FPSCR & 0x3) >> 1))
  */

  MachineFunction &MF = DAG.getMachineFunction();
  EVT VT = Op.getValueType();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  // Save FP Control Word to register
  SDValue Chain = Op.getOperand(0);
  SDValue MFFS = DAG.getNode(PPCISD::MFFS, dl, {MVT::f64, MVT::Other}, Chain);
  Chain = MFFS.getValue(1);

  SDValue CWD;
  if (isTypeLegal(MVT::i64)) {
    CWD = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32,
                      DAG.getNode(ISD::BITCAST, dl, MVT::i64, MFFS));
  } else {
    // Save FP register to stack slot
    int SSFI = MF.getFrameInfo().CreateStackObject(8, Align(8), false);
    SDValue StackSlot = DAG.getFrameIndex(SSFI, PtrVT);
    Chain = DAG.getStore(Chain, dl, MFFS, StackSlot, MachinePointerInfo());

    // Load FP Control Word from low 32 bits of stack slot.
    assert(hasBigEndianPartOrdering(MVT::i64, MF.getDataLayout()) &&
           "Stack slot adjustment is valid only on big endian subtargets!");
    SDValue Four = DAG.getConstant(4, dl, PtrVT);
    SDValue Addr = DAG.getNode(ISD::ADD, dl, PtrVT, StackSlot, Four);
    CWD = DAG.getLoad(MVT::i32, dl, Chain, Addr, MachinePointerInfo());
    Chain = CWD.getValue(1);
  }

  // Transform as necessary
  SDValue CWD1 =
    DAG.getNode(ISD::AND, dl, MVT::i32,
                CWD, DAG.getConstant(3, dl, MVT::i32));
  SDValue CWD2 =
    DAG.getNode(ISD::SRL, dl, MVT::i32,
                DAG.getNode(ISD::AND, dl, MVT::i32,
                            DAG.getNode(ISD::XOR, dl, MVT::i32,
                                        CWD, DAG.getConstant(3, dl, MVT::i32)),
                            DAG.getConstant(3, dl, MVT::i32)),
                DAG.getConstant(1, dl, MVT::i32));

  SDValue RetVal =
    DAG.getNode(ISD::XOR, dl, MVT::i32, CWD1, CWD2);

  RetVal =
      DAG.getNode((VT.getSizeInBits() < 16 ? ISD::TRUNCATE : ISD::ZERO_EXTEND),
                  dl, VT, RetVal);

  return DAG.getMergeValues({RetVal, Chain}, dl);
}

SDValue PPCTargetLowering::LowerSHL_PARTS(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  unsigned BitWidth = VT.getSizeInBits();
  SDLoc dl(Op);
  assert(Op.getNumOperands() == 3 &&
         VT == Op.getOperand(1).getValueType() &&
         "Unexpected SHL!");

  // Expand into a bunch of logical ops.  Note that these ops
  // depend on the PPC behavior for oversized shift amounts.
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  EVT AmtVT = Amt.getValueType();

  SDValue Tmp1 = DAG.getNode(ISD::SUB, dl, AmtVT,
                             DAG.getConstant(BitWidth, dl, AmtVT), Amt);
  SDValue Tmp2 = DAG.getNode(PPCISD::SHL, dl, VT, Hi, Amt);
  SDValue Tmp3 = DAG.getNode(PPCISD::SRL, dl, VT, Lo, Tmp1);
  SDValue Tmp4 = DAG.getNode(ISD::OR , dl, VT, Tmp2, Tmp3);
  SDValue Tmp5 = DAG.getNode(ISD::ADD, dl, AmtVT, Amt,
                             DAG.getConstant(-BitWidth, dl, AmtVT));
  SDValue Tmp6 = DAG.getNode(PPCISD::SHL, dl, VT, Lo, Tmp5);
  SDValue OutHi = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp6);
  SDValue OutLo = DAG.getNode(PPCISD::SHL, dl, VT, Lo, Amt);
  SDValue OutOps[] = { OutLo, OutHi };
  return DAG.getMergeValues(OutOps, dl);
}

SDValue PPCTargetLowering::LowerSRL_PARTS(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned BitWidth = VT.getSizeInBits();
  assert(Op.getNumOperands() == 3 &&
         VT == Op.getOperand(1).getValueType() &&
         "Unexpected SRL!");

  // Expand into a bunch of logical ops.  Note that these ops
  // depend on the PPC behavior for oversized shift amounts.
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  EVT AmtVT = Amt.getValueType();

  SDValue Tmp1 = DAG.getNode(ISD::SUB, dl, AmtVT,
                             DAG.getConstant(BitWidth, dl, AmtVT), Amt);
  SDValue Tmp2 = DAG.getNode(PPCISD::SRL, dl, VT, Lo, Amt);
  SDValue Tmp3 = DAG.getNode(PPCISD::SHL, dl, VT, Hi, Tmp1);
  SDValue Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);
  SDValue Tmp5 = DAG.getNode(ISD::ADD, dl, AmtVT, Amt,
                             DAG.getConstant(-BitWidth, dl, AmtVT));
  SDValue Tmp6 = DAG.getNode(PPCISD::SRL, dl, VT, Hi, Tmp5);
  SDValue OutLo = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp6);
  SDValue OutHi = DAG.getNode(PPCISD::SRL, dl, VT, Hi, Amt);
  SDValue OutOps[] = { OutLo, OutHi };
  return DAG.getMergeValues(OutOps, dl);
}

SDValue PPCTargetLowering::LowerSRA_PARTS(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  unsigned BitWidth = VT.getSizeInBits();
  assert(Op.getNumOperands() == 3 &&
         VT == Op.getOperand(1).getValueType() &&
         "Unexpected SRA!");

  // Expand into a bunch of logical ops, followed by a select_cc.
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  EVT AmtVT = Amt.getValueType();

  SDValue Tmp1 = DAG.getNode(ISD::SUB, dl, AmtVT,
                             DAG.getConstant(BitWidth, dl, AmtVT), Amt);
  SDValue Tmp2 = DAG.getNode(PPCISD::SRL, dl, VT, Lo, Amt);
  SDValue Tmp3 = DAG.getNode(PPCISD::SHL, dl, VT, Hi, Tmp1);
  SDValue Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);
  SDValue Tmp5 = DAG.getNode(ISD::ADD, dl, AmtVT, Amt,
                             DAG.getConstant(-BitWidth, dl, AmtVT));
  SDValue Tmp6 = DAG.getNode(PPCISD::SRA, dl, VT, Hi, Tmp5);
  SDValue OutHi = DAG.getNode(PPCISD::SRA, dl, VT, Hi, Amt);
  SDValue OutLo = DAG.getSelectCC(dl, Tmp5, DAG.getConstant(0, dl, AmtVT),
                                  Tmp4, Tmp6, ISD::SETLE);
  SDValue OutOps[] = { OutLo, OutHi };
  return DAG.getMergeValues(OutOps, dl);
}

SDValue PPCTargetLowering::LowerFunnelShift(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  unsigned BitWidth = VT.getSizeInBits();

  bool IsFSHL = Op.getOpcode() == ISD::FSHL;
  SDValue X = Op.getOperand(0);
  SDValue Y = Op.getOperand(1);
  SDValue Z = Op.getOperand(2);
  EVT AmtVT = Z.getValueType();

  // fshl: (X << (Z % BW)) | (Y >> (BW - (Z % BW)))
  // fshr: (X << (BW - (Z % BW))) | (Y >> (Z % BW))
  // This is simpler than TargetLowering::expandFunnelShift because we can rely
  // on PowerPC shift by BW being well defined.
  Z = DAG.getNode(ISD::AND, dl, AmtVT, Z,
                  DAG.getConstant(BitWidth - 1, dl, AmtVT));
  SDValue SubZ =
      DAG.getNode(ISD::SUB, dl, AmtVT, DAG.getConstant(BitWidth, dl, AmtVT), Z);
  X = DAG.getNode(PPCISD::SHL, dl, VT, X, IsFSHL ? Z : SubZ);
  Y = DAG.getNode(PPCISD::SRL, dl, VT, Y, IsFSHL ? SubZ : Z);
  return DAG.getNode(ISD::OR, dl, VT, X, Y);
}

//===----------------------------------------------------------------------===//
// Vector related lowering.
//

/// getCanonicalConstSplat - Build a canonical splat immediate of Val with an
/// element size of SplatSize. Cast the result to VT.
static SDValue getCanonicalConstSplat(uint64_t Val, unsigned SplatSize, EVT VT,
                                      SelectionDAG &DAG, const SDLoc &dl) {
  static const MVT VTys[] = { // canonical VT to use for each size.
    MVT::v16i8, MVT::v8i16, MVT::Other, MVT::v4i32
  };

  EVT ReqVT = VT != MVT::Other ? VT : VTys[SplatSize-1];

  // For a splat with all ones, turn it to vspltisb 0xFF to canonicalize.
  if (Val == ((1LLU << (SplatSize * 8)) - 1)) {
    SplatSize = 1;
    Val = 0xFF;
  }

  EVT CanonicalVT = VTys[SplatSize-1];

  // Build a canonical splat for this value.
  return DAG.getBitcast(ReqVT, DAG.getConstant(Val, dl, CanonicalVT));
}

/// BuildIntrinsicOp - Return a unary operator intrinsic node with the
/// specified intrinsic ID.
static SDValue BuildIntrinsicOp(unsigned IID, SDValue Op, SelectionDAG &DAG,
                                const SDLoc &dl, EVT DestVT = MVT::Other) {
  if (DestVT == MVT::Other) DestVT = Op.getValueType();
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, DestVT,
                     DAG.getConstant(IID, dl, MVT::i32), Op);
}

/// BuildIntrinsicOp - Return a binary operator intrinsic node with the
/// specified intrinsic ID.
static SDValue BuildIntrinsicOp(unsigned IID, SDValue LHS, SDValue RHS,
                                SelectionDAG &DAG, const SDLoc &dl,
                                EVT DestVT = MVT::Other) {
  if (DestVT == MVT::Other) DestVT = LHS.getValueType();
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, DestVT,
                     DAG.getConstant(IID, dl, MVT::i32), LHS, RHS);
}

/// BuildIntrinsicOp - Return a ternary operator intrinsic node with the
/// specified intrinsic ID.
static SDValue BuildIntrinsicOp(unsigned IID, SDValue Op0, SDValue Op1,
                                SDValue Op2, SelectionDAG &DAG, const SDLoc &dl,
                                EVT DestVT = MVT::Other) {
  if (DestVT == MVT::Other) DestVT = Op0.getValueType();
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, DestVT,
                     DAG.getConstant(IID, dl, MVT::i32), Op0, Op1, Op2);
}

/// BuildVSLDOI - Return a VECTOR_SHUFFLE that is a vsldoi of the specified
/// amount.  The result has the specified value type.
static SDValue BuildVSLDOI(SDValue LHS, SDValue RHS, unsigned Amt, EVT VT,
                           SelectionDAG &DAG, const SDLoc &dl) {
  // Force LHS/RHS to be the right type.
  LHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, LHS);
  RHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, RHS);

  int Ops[16];
  for (unsigned i = 0; i != 16; ++i)
    Ops[i] = i + Amt;
  SDValue T = DAG.getVectorShuffle(MVT::v16i8, dl, LHS, RHS, Ops);
  return DAG.getNode(ISD::BITCAST, dl, VT, T);
}

/// Do we have an efficient pattern in a .td file for this node?
///
/// \param V - pointer to the BuildVectorSDNode being matched
/// \param HasDirectMove - does this subtarget have VSR <-> GPR direct moves?
///
/// There are some patterns where it is beneficial to keep a BUILD_VECTOR
/// node as a BUILD_VECTOR node rather than expanding it. The patterns where
/// the opposite is true (expansion is beneficial) are:
/// - The node builds a vector out of integers that are not 32 or 64-bits
/// - The node builds a vector out of constants
/// - The node is a "load-and-splat"
/// In all other cases, we will choose to keep the BUILD_VECTOR.
static bool haveEfficientBuildVectorPattern(BuildVectorSDNode *V,
                                            bool HasDirectMove,
                                            bool HasP8Vector) {
  EVT VecVT = V->getValueType(0);
  bool RightType = VecVT == MVT::v2f64 ||
    (HasP8Vector && VecVT == MVT::v4f32) ||
    (HasDirectMove && (VecVT == MVT::v2i64 || VecVT == MVT::v4i32));
  if (!RightType)
    return false;

  bool IsSplat = true;
  bool IsLoad = false;
  SDValue Op0 = V->getOperand(0);

  // This function is called in a block that confirms the node is not a constant
  // splat. So a constant BUILD_VECTOR here means the vector is built out of
  // different constants.
  if (V->isConstant())
    return false;
  for (int i = 0, e = V->getNumOperands(); i < e; ++i) {
    if (V->getOperand(i).isUndef())
      return false;
    // We want to expand nodes that represent load-and-splat even if the
    // loaded value is a floating point truncation or conversion to int.
    if (V->getOperand(i).getOpcode() == ISD::LOAD ||
        (V->getOperand(i).getOpcode() == ISD::FP_ROUND &&
         V->getOperand(i).getOperand(0).getOpcode() == ISD::LOAD) ||
        (V->getOperand(i).getOpcode() == ISD::FP_TO_SINT &&
         V->getOperand(i).getOperand(0).getOpcode() == ISD::LOAD) ||
        (V->getOperand(i).getOpcode() == ISD::FP_TO_UINT &&
         V->getOperand(i).getOperand(0).getOpcode() == ISD::LOAD))
      IsLoad = true;
    // If the operands are different or the input is not a load and has more
    // uses than just this BV node, then it isn't a splat.
    if (V->getOperand(i) != Op0 ||
        (!IsLoad && !V->isOnlyUserOf(V->getOperand(i).getNode())))
      IsSplat = false;
  }
  return !(IsSplat && IsLoad);
}

// Lower BITCAST(f128, (build_pair i64, i64)) to BUILD_FP128.
SDValue PPCTargetLowering::LowerBITCAST(SDValue Op, SelectionDAG &DAG) const {

  SDLoc dl(Op);
  SDValue Op0 = Op->getOperand(0);

  if (!Subtarget.isPPC64() || (Op0.getOpcode() != ISD::BUILD_PAIR) ||
      (Op.getValueType() != MVT::f128))
    return SDValue();

  SDValue Lo = Op0.getOperand(0);
  SDValue Hi = Op0.getOperand(1);
  if ((Lo.getValueType() != MVT::i64) || (Hi.getValueType() != MVT::i64))
    return SDValue();

  if (!Subtarget.isLittleEndian())
    std::swap(Lo, Hi);

  return DAG.getNode(PPCISD::BUILD_FP128, dl, MVT::f128, Lo, Hi);
}

static const SDValue *getNormalLoadInput(const SDValue &Op, bool &IsPermuted) {
  const SDValue *InputLoad = &Op;
  while (InputLoad->getOpcode() == ISD::BITCAST)
    InputLoad = &InputLoad->getOperand(0);
  if (InputLoad->getOpcode() == ISD::SCALAR_TO_VECTOR ||
      InputLoad->getOpcode() == PPCISD::SCALAR_TO_VECTOR_PERMUTED) {
    IsPermuted = InputLoad->getOpcode() == PPCISD::SCALAR_TO_VECTOR_PERMUTED;
    InputLoad = &InputLoad->getOperand(0);
  }
  if (InputLoad->getOpcode() != ISD::LOAD)
    return nullptr;
  LoadSDNode *LD = cast<LoadSDNode>(*InputLoad);
  return ISD::isNormalLoad(LD) ? InputLoad : nullptr;
}

// Convert the argument APFloat to a single precision APFloat if there is no
// loss in information during the conversion to single precision APFloat and the
// resulting number is not a denormal number. Return true if successful.
bool llvm::convertToNonDenormSingle(APFloat &ArgAPFloat) {
  APFloat APFloatToConvert = ArgAPFloat;
  bool LosesInfo = true;
  APFloatToConvert.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven,
                           &LosesInfo);
  bool Success = (!LosesInfo && !APFloatToConvert.isDenormal());
  if (Success)
    ArgAPFloat = APFloatToConvert;
  return Success;
}

// Bitcast the argument APInt to a double and convert it to a single precision
// APFloat, bitcast the APFloat to an APInt and assign it to the original
// argument if there is no loss in information during the conversion from
// double to single precision APFloat and the resulting number is not a denormal
// number. Return true if successful.
bool llvm::convertToNonDenormSingle(APInt &ArgAPInt) {
  double DpValue = ArgAPInt.bitsToDouble();
  APFloat APFloatDp(DpValue);
  bool Success = convertToNonDenormSingle(APFloatDp);
  if (Success)
    ArgAPInt = APFloatDp.bitcastToAPInt();
  return Success;
}

// Nondestructive check for convertTonNonDenormSingle.
bool llvm::checkConvertToNonDenormSingle(APFloat &ArgAPFloat) {
  // Only convert if it loses info, since XXSPLTIDP should
  // handle the other case.
  APFloat APFloatToConvert = ArgAPFloat;
  bool LosesInfo = true;
  APFloatToConvert.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven,
                           &LosesInfo);

  return (!LosesInfo && !APFloatToConvert.isDenormal());
}

static bool isValidSplatLoad(const PPCSubtarget &Subtarget, const SDValue &Op,
                             unsigned &Opcode) {
  LoadSDNode *InputNode = dyn_cast<LoadSDNode>(Op.getOperand(0));
  if (!InputNode || !Subtarget.hasVSX() || !ISD::isUNINDEXEDLoad(InputNode))
    return false;

  EVT Ty = Op->getValueType(0);
  // For v2f64, v4f32 and v4i32 types, we require the load to be non-extending
  // as we cannot handle extending loads for these types.
  if ((Ty == MVT::v2f64 || Ty == MVT::v4f32 || Ty == MVT::v4i32) &&
      ISD::isNON_EXTLoad(InputNode))
    return true;

  EVT MemVT = InputNode->getMemoryVT();
  // For v8i16 and v16i8 types, extending loads can be handled as long as the
  // memory VT is the same vector element VT type.
  // The loads feeding into the v8i16 and v16i8 types will be extending because
  // scalar i8/i16 are not legal types.
  if ((Ty == MVT::v8i16 || Ty == MVT::v16i8) && ISD::isEXTLoad(InputNode) &&
      (MemVT == Ty.getVectorElementType()))
    return true;

  if (Ty == MVT::v2i64) {
    // Check the extend type, when the input type is i32, and the output vector
    // type is v2i64.
    if (MemVT == MVT::i32) {
      if (ISD::isZEXTLoad(InputNode))
        Opcode = PPCISD::ZEXT_LD_SPLAT;
      if (ISD::isSEXTLoad(InputNode))
        Opcode = PPCISD::SEXT_LD_SPLAT;
    }
    return true;
  }
  return false;
}

// If this is a case we can't handle, return null and let the default
// expansion code take care of it.  If we CAN select this case, and if it
// selects to a single instruction, return Op.  Otherwise, if we can codegen
// this case more efficiently than a constant pool load, lower it to the
// sequence of ops that should be used.
SDValue PPCTargetLowering::LowerBUILD_VECTOR(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc dl(Op);
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(Op.getNode());
  assert(BVN && "Expected a BuildVectorSDNode in LowerBUILD_VECTOR");

  // Check if this is a splat of a constant value.
  APInt APSplatBits, APSplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  bool BVNIsConstantSplat =
      BVN->isConstantSplat(APSplatBits, APSplatUndef, SplatBitSize,
                           HasAnyUndefs, 0, !Subtarget.isLittleEndian());

  // If it is a splat of a double, check if we can shrink it to a 32 bit
  // non-denormal float which when converted back to double gives us the same
  // double. This is to exploit the XXSPLTIDP instruction.
  // If we lose precision, we use XXSPLTI32DX.
  if (BVNIsConstantSplat && (SplatBitSize == 64) &&
      Subtarget.hasPrefixInstrs() && Subtarget.hasP10Vector()) {
    // Check the type first to short-circuit so we don't modify APSplatBits if
    // this block isn't executed.
    if ((Op->getValueType(0) == MVT::v2f64) &&
        convertToNonDenormSingle(APSplatBits)) {
      SDValue SplatNode = DAG.getNode(
          PPCISD::XXSPLTI_SP_TO_DP, dl, MVT::v2f64,
          DAG.getTargetConstant(APSplatBits.getZExtValue(), dl, MVT::i32));
      return DAG.getBitcast(Op.getValueType(), SplatNode);
    } else {
      // We may lose precision, so we have to use XXSPLTI32DX.

      uint32_t Hi =
          (uint32_t)((APSplatBits.getZExtValue() & 0xFFFFFFFF00000000LL) >> 32);
      uint32_t Lo =
          (uint32_t)(APSplatBits.getZExtValue() & 0xFFFFFFFF);
      SDValue SplatNode = DAG.getUNDEF(MVT::v2i64);

      if (!Hi || !Lo)
        // If either load is 0, then we should generate XXLXOR to set to 0.
        SplatNode = DAG.getTargetConstant(0, dl, MVT::v2i64);

      if (Hi)
        SplatNode = DAG.getNode(
            PPCISD::XXSPLTI32DX, dl, MVT::v2i64, SplatNode,
            DAG.getTargetConstant(0, dl, MVT::i32),
            DAG.getTargetConstant(Hi, dl, MVT::i32));

      if (Lo)
        SplatNode =
            DAG.getNode(PPCISD::XXSPLTI32DX, dl, MVT::v2i64, SplatNode,
                        DAG.getTargetConstant(1, dl, MVT::i32),
                        DAG.getTargetConstant(Lo, dl, MVT::i32));

      return DAG.getBitcast(Op.getValueType(), SplatNode);
    }
  }

  if (!BVNIsConstantSplat || SplatBitSize > 32) {
    unsigned NewOpcode = PPCISD::LD_SPLAT;

    // Handle load-and-splat patterns as we have instructions that will do this
    // in one go.
    if (DAG.isSplatValue(Op, true) &&
        isValidSplatLoad(Subtarget, Op, NewOpcode)) {
      const SDValue *InputLoad = &Op.getOperand(0);
      LoadSDNode *LD = cast<LoadSDNode>(*InputLoad);

      // If the input load is an extending load, it will be an i32 -> i64
      // extending load and isValidSplatLoad() will update NewOpcode.
      unsigned MemorySize = LD->getMemoryVT().getScalarSizeInBits();
      unsigned ElementSize =
          MemorySize * ((NewOpcode == PPCISD::LD_SPLAT) ? 1 : 2);

      assert(((ElementSize == 2 * MemorySize)
                  ? (NewOpcode == PPCISD::ZEXT_LD_SPLAT ||
                     NewOpcode == PPCISD::SEXT_LD_SPLAT)
                  : (NewOpcode == PPCISD::LD_SPLAT)) &&
             "Unmatched element size and opcode!\n");

      // Checking for a single use of this load, we have to check for vector
      // width (128 bits) / ElementSize uses (since each operand of the
      // BUILD_VECTOR is a separate use of the value.
      unsigned NumUsesOfInputLD = 128 / ElementSize;
      for (SDValue BVInOp : Op->ops())
        if (BVInOp.isUndef())
          NumUsesOfInputLD--;

      // Exclude somes case where LD_SPLAT is worse than scalar_to_vector:
      // Below cases should also happen for "lfiwzx/lfiwax + LE target + index
      // 1" and "lxvrhx + BE target + index 7" and "lxvrbx + BE target + index
      // 15", but function IsValidSplatLoad() now will only return true when
      // the data at index 0 is not nullptr. So we will not get into trouble for
      // these cases.
      //
      // case 1 - lfiwzx/lfiwax
      // 1.1: load result is i32 and is sign/zero extend to i64;
      // 1.2: build a v2i64 vector type with above loaded value;
      // 1.3: the vector has only one value at index 0, others are all undef;
      // 1.4: on BE target, so that lfiwzx/lfiwax does not need any permute.
      if (NumUsesOfInputLD == 1 &&
          (Op->getValueType(0) == MVT::v2i64 && NewOpcode != PPCISD::LD_SPLAT &&
           !Subtarget.isLittleEndian() && Subtarget.hasVSX() &&
           Subtarget.hasLFIWAX()))
        return SDValue();

      // case 2 - lxvr[hb]x
      // 2.1: load result is at most i16;
      // 2.2: build a vector with above loaded value;
      // 2.3: the vector has only one value at index 0, others are all undef;
      // 2.4: on LE target, so that lxvr[hb]x does not need any permute.
      if (NumUsesOfInputLD == 1 && Subtarget.isLittleEndian() &&
          Subtarget.isISA3_1() && ElementSize <= 16)
        return SDValue();

      assert(NumUsesOfInputLD > 0 && "No uses of input LD of a build_vector?");
      if (InputLoad->getNode()->hasNUsesOfValue(NumUsesOfInputLD, 0) &&
          Subtarget.hasVSX()) {
        SDValue Ops[] = {
          LD->getChain(),    // Chain
          LD->getBasePtr(),  // Ptr
          DAG.getValueType(Op.getValueType()) // VT
        };
        SDValue LdSplt = DAG.getMemIntrinsicNode(
            NewOpcode, dl, DAG.getVTList(Op.getValueType(), MVT::Other), Ops,
            LD->getMemoryVT(), LD->getMemOperand());
        // Replace all uses of the output chain of the original load with the
        // output chain of the new load.
        DAG.ReplaceAllUsesOfValueWith(InputLoad->getValue(1),
                                      LdSplt.getValue(1));
        return LdSplt;
      }
    }

    // In 64BIT mode BUILD_VECTOR nodes that are not constant splats of up to
    // 32-bits can be lowered to VSX instructions under certain conditions.
    // Without VSX, there is no pattern more efficient than expanding the node.
    if (Subtarget.hasVSX() && Subtarget.isPPC64() &&
        haveEfficientBuildVectorPattern(BVN, Subtarget.hasDirectMove(),
                                        Subtarget.hasP8Vector()))
      return Op;
    return SDValue();
  }

  uint64_t SplatBits = APSplatBits.getZExtValue();
  uint64_t SplatUndef = APSplatUndef.getZExtValue();
  unsigned SplatSize = SplatBitSize / 8;

  // First, handle single instruction cases.

  // All zeros?
  if (SplatBits == 0) {
    // Canonicalize all zero vectors to be v4i32.
    if (Op.getValueType() != MVT::v4i32 || HasAnyUndefs) {
      SDValue Z = DAG.getConstant(0, dl, MVT::v4i32);
      Op = DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Z);
    }
    return Op;
  }

  // We have XXSPLTIW for constant splats four bytes wide.
  // Given vector length is a multiple of 4, 2-byte splats can be replaced
  // with 4-byte splats. We replicate the SplatBits in case of 2-byte splat to
  // make a 4-byte splat element. For example: 2-byte splat of 0xABAB can be
  // turned into a 4-byte splat of 0xABABABAB.
  if (Subtarget.hasPrefixInstrs() && Subtarget.hasP10Vector() && SplatSize == 2)
    return getCanonicalConstSplat(SplatBits | (SplatBits << 16), SplatSize * 2,
                                  Op.getValueType(), DAG, dl);

  if (Subtarget.hasPrefixInstrs() && Subtarget.hasP10Vector() && SplatSize == 4)
    return getCanonicalConstSplat(SplatBits, SplatSize, Op.getValueType(), DAG,
                                  dl);

  // We have XXSPLTIB for constant splats one byte wide.
  if (Subtarget.hasP9Vector() && SplatSize == 1)
    return getCanonicalConstSplat(SplatBits, SplatSize, Op.getValueType(), DAG,
                                  dl);

  // If the sign extended value is in the range [-16,15], use VSPLTI[bhw].
  int32_t SextVal= (int32_t(SplatBits << (32-SplatBitSize)) >>
                    (32-SplatBitSize));
  if (SextVal >= -16 && SextVal <= 15)
    return getCanonicalConstSplat(SextVal, SplatSize, Op.getValueType(), DAG,
                                  dl);

  // Two instruction sequences.

  // If this value is in the range [-32,30] and is even, use:
  //     VSPLTI[bhw](val/2) + VSPLTI[bhw](val/2)
  // If this value is in the range [17,31] and is odd, use:
  //     VSPLTI[bhw](val-16) - VSPLTI[bhw](-16)
  // If this value is in the range [-31,-17] and is odd, use:
  //     VSPLTI[bhw](val+16) + VSPLTI[bhw](-16)
  // Note the last two are three-instruction sequences.
  if (SextVal >= -32 && SextVal <= 31) {
    // To avoid having these optimizations undone by constant folding,
    // we convert to a pseudo that will be expanded later into one of
    // the above forms.
    SDValue Elt = DAG.getConstant(SextVal, dl, MVT::i32);
    EVT VT = (SplatSize == 1 ? MVT::v16i8 :
              (SplatSize == 2 ? MVT::v8i16 : MVT::v4i32));
    SDValue EltSize = DAG.getConstant(SplatSize, dl, MVT::i32);
    SDValue RetVal = DAG.getNode(PPCISD::VADD_SPLAT, dl, VT, Elt, EltSize);
    if (VT == Op.getValueType())
      return RetVal;
    else
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), RetVal);
  }

  // If this is 0x8000_0000 x 4, turn into vspltisw + vslw.  If it is
  // 0x7FFF_FFFF x 4, turn it into not(0x8000_0000).  This is important
  // for fneg/fabs.
  if (SplatSize == 4 && SplatBits == (0x7FFFFFFF&~SplatUndef)) {
    // Make -1 and vspltisw -1:
    SDValue OnesV = getCanonicalConstSplat(-1, 4, MVT::v4i32, DAG, dl);

    // Make the VSLW intrinsic, computing 0x8000_0000.
    SDValue Res = BuildIntrinsicOp(Intrinsic::ppc_altivec_vslw, OnesV,
                                   OnesV, DAG, dl);

    // xor by OnesV to invert it.
    Res = DAG.getNode(ISD::XOR, dl, MVT::v4i32, Res, OnesV);
    return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
  }

  // Check to see if this is a wide variety of vsplti*, binop self cases.
  static const signed char SplatCsts[] = {
    -1, 1, -2, 2, -3, 3, -4, 4, -5, 5, -6, 6, -7, 7,
    -8, 8, -9, 9, -10, 10, -11, 11, -12, 12, -13, 13, 14, -14, 15, -15, -16
  };

  for (unsigned idx = 0; idx < std::size(SplatCsts); ++idx) {
    // Indirect through the SplatCsts array so that we favor 'vsplti -1' for
    // cases which are ambiguous (e.g. formation of 0x8000_0000).  'vsplti -1'
    int i = SplatCsts[idx];

    // Figure out what shift amount will be used by altivec if shifted by i in
    // this splat size.
    unsigned TypeShiftAmt = i & (SplatBitSize-1);

    // vsplti + shl self.
    if (SextVal == (int)((unsigned)i << TypeShiftAmt)) {
      SDValue Res = getCanonicalConstSplat(i, SplatSize, MVT::Other, DAG, dl);
      static const unsigned IIDs[] = { // Intrinsic to use for each size.
        Intrinsic::ppc_altivec_vslb, Intrinsic::ppc_altivec_vslh, 0,
        Intrinsic::ppc_altivec_vslw
      };
      Res = BuildIntrinsicOp(IIDs[SplatSize-1], Res, Res, DAG, dl);
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
    }

    // vsplti + srl self.
    if (SextVal == (int)((unsigned)i >> TypeShiftAmt)) {
      SDValue Res = getCanonicalConstSplat(i, SplatSize, MVT::Other, DAG, dl);
      static const unsigned IIDs[] = { // Intrinsic to use for each size.
        Intrinsic::ppc_altivec_vsrb, Intrinsic::ppc_altivec_vsrh, 0,
        Intrinsic::ppc_altivec_vsrw
      };
      Res = BuildIntrinsicOp(IIDs[SplatSize-1], Res, Res, DAG, dl);
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
    }

    // vsplti + rol self.
    if (SextVal == (int)(((unsigned)i << TypeShiftAmt) |
                         ((unsigned)i >> (SplatBitSize-TypeShiftAmt)))) {
      SDValue Res = getCanonicalConstSplat(i, SplatSize, MVT::Other, DAG, dl);
      static const unsigned IIDs[] = { // Intrinsic to use for each size.
        Intrinsic::ppc_altivec_vrlb, Intrinsic::ppc_altivec_vrlh, 0,
        Intrinsic::ppc_altivec_vrlw
      };
      Res = BuildIntrinsicOp(IIDs[SplatSize-1], Res, Res, DAG, dl);
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
    }

    // t = vsplti c, result = vsldoi t, t, 1
    if (SextVal == (int)(((unsigned)i << 8) | (i < 0 ? 0xFF : 0))) {
      SDValue T = getCanonicalConstSplat(i, SplatSize, MVT::v16i8, DAG, dl);
      unsigned Amt = Subtarget.isLittleEndian() ? 15 : 1;
      return BuildVSLDOI(T, T, Amt, Op.getValueType(), DAG, dl);
    }
    // t = vsplti c, result = vsldoi t, t, 2
    if (SextVal == (int)(((unsigned)i << 16) | (i < 0 ? 0xFFFF : 0))) {
      SDValue T = getCanonicalConstSplat(i, SplatSize, MVT::v16i8, DAG, dl);
      unsigned Amt = Subtarget.isLittleEndian() ? 14 : 2;
      return BuildVSLDOI(T, T, Amt, Op.getValueType(), DAG, dl);
    }
    // t = vsplti c, result = vsldoi t, t, 3
    if (SextVal == (int)(((unsigned)i << 24) | (i < 0 ? 0xFFFFFF : 0))) {
      SDValue T = getCanonicalConstSplat(i, SplatSize, MVT::v16i8, DAG, dl);
      unsigned Amt = Subtarget.isLittleEndian() ? 13 : 3;
      return BuildVSLDOI(T, T, Amt, Op.getValueType(), DAG, dl);
    }
  }

  return SDValue();
}

/// GeneratePerfectShuffle - Given an entry in the perfect-shuffle table, emit
/// the specified operations to build the shuffle.
static SDValue GeneratePerfectShuffle(unsigned PFEntry, SDValue LHS,
                                      SDValue RHS, SelectionDAG &DAG,
                                      const SDLoc &dl) {
  unsigned OpNum = (PFEntry >> 26) & 0x0F;
  unsigned LHSID = (PFEntry >> 13) & ((1 << 13)-1);
  unsigned RHSID = (PFEntry >>  0) & ((1 << 13)-1);

  enum {
    OP_COPY = 0,  // Copy, used for things like <u,u,u,3> to say it is <0,1,2,3>
    OP_VMRGHW,
    OP_VMRGLW,
    OP_VSPLTISW0,
    OP_VSPLTISW1,
    OP_VSPLTISW2,
    OP_VSPLTISW3,
    OP_VSLDOI4,
    OP_VSLDOI8,
    OP_VSLDOI12
  };

  if (OpNum == OP_COPY) {
    if (LHSID == (1*9+2)*9+3) return LHS;
    assert(LHSID == ((4*9+5)*9+6)*9+7 && "Illegal OP_COPY!");
    return RHS;
  }

  SDValue OpLHS, OpRHS;
  OpLHS = GeneratePerfectShuffle(PerfectShuffleTable[LHSID], LHS, RHS, DAG, dl);
  OpRHS = GeneratePerfectShuffle(PerfectShuffleTable[RHSID], LHS, RHS, DAG, dl);

  int ShufIdxs[16];
  switch (OpNum) {
  default: llvm_unreachable("Unknown i32 permute!");
  case OP_VMRGHW:
    ShufIdxs[ 0] =  0; ShufIdxs[ 1] =  1; ShufIdxs[ 2] =  2; ShufIdxs[ 3] =  3;
    ShufIdxs[ 4] = 16; ShufIdxs[ 5] = 17; ShufIdxs[ 6] = 18; ShufIdxs[ 7] = 19;
    ShufIdxs[ 8] =  4; ShufIdxs[ 9] =  5; ShufIdxs[10] =  6; ShufIdxs[11] =  7;
    ShufIdxs[12] = 20; ShufIdxs[13] = 21; ShufIdxs[14] = 22; ShufIdxs[15] = 23;
    break;
  case OP_VMRGLW:
    ShufIdxs[ 0] =  8; ShufIdxs[ 1] =  9; ShufIdxs[ 2] = 10; ShufIdxs[ 3] = 11;
    ShufIdxs[ 4] = 24; ShufIdxs[ 5] = 25; ShufIdxs[ 6] = 26; ShufIdxs[ 7] = 27;
    ShufIdxs[ 8] = 12; ShufIdxs[ 9] = 13; ShufIdxs[10] = 14; ShufIdxs[11] = 15;
    ShufIdxs[12] = 28; ShufIdxs[13] = 29; ShufIdxs[14] = 30; ShufIdxs[15] = 31;
    break;
  case OP_VSPLTISW0:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+0;
    break;
  case OP_VSPLTISW1:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+4;
    break;
  case OP_VSPLTISW2:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+8;
    break;
  case OP_VSPLTISW3:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+12;
    break;
  case OP_VSLDOI4:
    return BuildVSLDOI(OpLHS, OpRHS, 4, OpLHS.getValueType(), DAG, dl);
  case OP_VSLDOI8:
    return BuildVSLDOI(OpLHS, OpRHS, 8, OpLHS.getValueType(), DAG, dl);
  case OP_VSLDOI12:
    return BuildVSLDOI(OpLHS, OpRHS, 12, OpLHS.getValueType(), DAG, dl);
  }
  EVT VT = OpLHS.getValueType();
  OpLHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, OpLHS);
  OpRHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, OpRHS);
  SDValue T = DAG.getVectorShuffle(MVT::v16i8, dl, OpLHS, OpRHS, ShufIdxs);
  return DAG.getNode(ISD::BITCAST, dl, VT, T);
}

/// lowerToVINSERTB - Return the SDValue if this VECTOR_SHUFFLE can be handled
/// by the VINSERTB instruction introduced in ISA 3.0, else just return default
/// SDValue.
SDValue PPCTargetLowering::lowerToVINSERTB(ShuffleVectorSDNode *N,
                                           SelectionDAG &DAG) const {
  const unsigned BytesInVector = 16;
  bool IsLE = Subtarget.isLittleEndian();
  SDLoc dl(N);
  SDValue V1 = N->getOperand(0);
  SDValue V2 = N->getOperand(1);
  unsigned ShiftElts = 0, InsertAtByte = 0;
  bool Swap = false;

  // Shifts required to get the byte we want at element 7.
  unsigned LittleEndianShifts[] = {8, 7,  6,  5,  4,  3,  2,  1,
                                   0, 15, 14, 13, 12, 11, 10, 9};
  unsigned BigEndianShifts[] = {9, 10, 11, 12, 13, 14, 15, 0,
                                1, 2,  3,  4,  5,  6,  7,  8};

  ArrayRef<int> Mask = N->getMask();
  int OriginalOrder[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

  // For each mask element, find out if we're just inserting something
  // from V2 into V1 or vice versa.
  // Possible permutations inserting an element from V2 into V1:
  //   X, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
  //   0, X, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
  //   ...
  //   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, X
  // Inserting from V1 into V2 will be similar, except mask range will be
  // [16,31].

  bool FoundCandidate = false;
  // If both vector operands for the shuffle are the same vector, the mask
  // will contain only elements from the first one and the second one will be
  // undef.
  unsigned VINSERTBSrcElem = IsLE ? 8 : 7;
  // Go through the mask of half-words to find an element that's being moved
  // from one vector to the other.
  for (unsigned i = 0; i < BytesInVector; ++i) {
    unsigned CurrentElement = Mask[i];
    // If 2nd operand is undefined, we should only look for element 7 in the
    // Mask.
    if (V2.isUndef() && CurrentElement != VINSERTBSrcElem)
      continue;

    bool OtherElementsInOrder = true;
    // Examine the other elements in the Mask to see if they're in original
    // order.
    for (unsigned j = 0; j < BytesInVector; ++j) {
      if (j == i)
        continue;
      // If CurrentElement is from V1 [0,15], then we the rest of the Mask to be
      // from V2 [16,31] and vice versa.  Unless the 2nd operand is undefined,
      // in which we always assume we're always picking from the 1st operand.
      int MaskOffset =
          (!V2.isUndef() && CurrentElement < BytesInVector) ? BytesInVector : 0;
      if (Mask[j] != OriginalOrder[j] + MaskOffset) {
        OtherElementsInOrder = false;
        break;
      }
    }
    // If other elements are in original order, we record the number of shifts
    // we need to get the element we want into element 7. Also record which byte
    // in the vector we should insert into.
    if (OtherElementsInOrder) {
      // If 2nd operand is undefined, we assume no shifts and no swapping.
      if (V2.isUndef()) {
        ShiftElts = 0;
        Swap = false;
      } else {
        // Only need the last 4-bits for shifts because operands will be swapped if CurrentElement is >= 2^4.
        ShiftElts = IsLE ? LittleEndianShifts[CurrentElement & 0xF]
                         : BigEndianShifts[CurrentElement & 0xF];
        Swap = CurrentElement < BytesInVector;
      }
      InsertAtByte = IsLE ? BytesInVector - (i + 1) : i;
      FoundCandidate = true;
      break;
    }
  }

  if (!FoundCandidate)
    return SDValue();

  // Candidate found, construct the proper SDAG sequence with VINSERTB,
  // optionally with VECSHL if shift is required.
  if (Swap)
    std::swap(V1, V2);
  if (V2.isUndef())
    V2 = V1;
  if (ShiftElts) {
    SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v16i8, V2, V2,
                              DAG.getConstant(ShiftElts, dl, MVT::i32));
    return DAG.getNode(PPCISD::VECINSERT, dl, MVT::v16i8, V1, Shl,
                       DAG.getConstant(InsertAtByte, dl, MVT::i32));
  }
  return DAG.getNode(PPCISD::VECINSERT, dl, MVT::v16i8, V1, V2,
                     DAG.getConstant(InsertAtByte, dl, MVT::i32));
}

/// lowerToVINSERTH - Return the SDValue if this VECTOR_SHUFFLE can be handled
/// by the VINSERTH instruction introduced in ISA 3.0, else just return default
/// SDValue.
SDValue PPCTargetLowering::lowerToVINSERTH(ShuffleVectorSDNode *N,
                                           SelectionDAG &DAG) const {
  const unsigned NumHalfWords = 8;
  const unsigned BytesInVector = NumHalfWords * 2;
  // Check that the shuffle is on half-words.
  if (!isNByteElemShuffleMask(N, 2, 1))
    return SDValue();

  bool IsLE = Subtarget.isLittleEndian();
  SDLoc dl(N);
  SDValue V1 = N->getOperand(0);
  SDValue V2 = N->getOperand(1);
  unsigned ShiftElts = 0, InsertAtByte = 0;
  bool Swap = false;

  // Shifts required to get the half-word we want at element 3.
  unsigned LittleEndianShifts[] = {4, 3, 2, 1, 0, 7, 6, 5};
  unsigned BigEndianShifts[] = {5, 6, 7, 0, 1, 2, 3, 4};

  uint32_t Mask = 0;
  uint32_t OriginalOrderLow = 0x1234567;
  uint32_t OriginalOrderHigh = 0x89ABCDEF;
  // Now we look at mask elements 0,2,4,6,8,10,12,14.  Pack the mask into a
  // 32-bit space, only need 4-bit nibbles per element.
  for (unsigned i = 0; i < NumHalfWords; ++i) {
    unsigned MaskShift = (NumHalfWords - 1 - i) * 4;
    Mask |= ((uint32_t)(N->getMaskElt(i * 2) / 2) << MaskShift);
  }

  // For each mask element, find out if we're just inserting something
  // from V2 into V1 or vice versa.  Possible permutations inserting an element
  // from V2 into V1:
  //   X, 1, 2, 3, 4, 5, 6, 7
  //   0, X, 2, 3, 4, 5, 6, 7
  //   0, 1, X, 3, 4, 5, 6, 7
  //   0, 1, 2, X, 4, 5, 6, 7
  //   0, 1, 2, 3, X, 5, 6, 7
  //   0, 1, 2, 3, 4, X, 6, 7
  //   0, 1, 2, 3, 4, 5, X, 7
  //   0, 1, 2, 3, 4, 5, 6, X
  // Inserting from V1 into V2 will be similar, except mask range will be [8,15].

  bool FoundCandidate = false;
  // Go through the mask of half-words to find an element that's being moved
  // from one vector to the other.
  for (unsigned i = 0; i < NumHalfWords; ++i) {
    unsigned MaskShift = (NumHalfWords - 1 - i) * 4;
    uint32_t MaskOneElt = (Mask >> MaskShift) & 0xF;
    uint32_t MaskOtherElts = ~(0xF << MaskShift);
    uint32_t TargetOrder = 0x0;

    // If both vector operands for the shuffle are the same vector, the mask
    // will contain only elements from the first one and the second one will be
    // undef.
    if (V2.isUndef()) {
      ShiftElts = 0;
      unsigned VINSERTHSrcElem = IsLE ? 4 : 3;
      TargetOrder = OriginalOrderLow;
      Swap = false;
      // Skip if not the correct element or mask of other elements don't equal
      // to our expected order.
      if (MaskOneElt == VINSERTHSrcElem &&
          (Mask & MaskOtherElts) == (TargetOrder & MaskOtherElts)) {
        InsertAtByte = IsLE ? BytesInVector - (i + 1) * 2 : i * 2;
        FoundCandidate = true;
        break;
      }
    } else { // If both operands are defined.
      // Target order is [8,15] if the current mask is between [0,7].
      TargetOrder =
          (MaskOneElt < NumHalfWords) ? OriginalOrderHigh : OriginalOrderLow;
      // Skip if mask of other elements don't equal our expected order.
      if ((Mask & MaskOtherElts) == (TargetOrder & MaskOtherElts)) {
        // We only need the last 3 bits for the number of shifts.
        ShiftElts = IsLE ? LittleEndianShifts[MaskOneElt & 0x7]
                         : BigEndianShifts[MaskOneElt & 0x7];
        InsertAtByte = IsLE ? BytesInVector - (i + 1) * 2 : i * 2;
        Swap = MaskOneElt < NumHalfWords;
        FoundCandidate = true;
        break;
      }
    }
  }

  if (!FoundCandidate)
    return SDValue();

  // Candidate found, construct the proper SDAG sequence with VINSERTH,
  // optionally with VECSHL if shift is required.
  if (Swap)
    std::swap(V1, V2);
  if (V2.isUndef())
    V2 = V1;
  SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, V1);
  if (ShiftElts) {
    // Double ShiftElts because we're left shifting on v16i8 type.
    SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v16i8, V2, V2,
                              DAG.getConstant(2 * ShiftElts, dl, MVT::i32));
    SDValue Conv2 = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, Shl);
    SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v8i16, Conv1, Conv2,
                              DAG.getConstant(InsertAtByte, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
  }
  SDValue Conv2 = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, V2);
  SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v8i16, Conv1, Conv2,
                            DAG.getConstant(InsertAtByte, dl, MVT::i32));
  return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
}

/// lowerToXXSPLTI32DX - Return the SDValue if this VECTOR_SHUFFLE can be
/// handled by the XXSPLTI32DX instruction introduced in ISA 3.1, otherwise
/// return the default SDValue.
SDValue PPCTargetLowering::lowerToXXSPLTI32DX(ShuffleVectorSDNode *SVN,
                                              SelectionDAG &DAG) const {
  // The LHS and RHS may be bitcasts to v16i8 as we canonicalize shuffles
  // to v16i8. Peek through the bitcasts to get the actual operands.
  SDValue LHS = peekThroughBitcasts(SVN->getOperand(0));
  SDValue RHS = peekThroughBitcasts(SVN->getOperand(1));

  auto ShuffleMask = SVN->getMask();
  SDValue VecShuffle(SVN, 0);
  SDLoc DL(SVN);

  // Check that we have a four byte shuffle.
  if (!isNByteElemShuffleMask(SVN, 4, 1))
    return SDValue();

  // Canonicalize the RHS being a BUILD_VECTOR when lowering to xxsplti32dx.
  if (RHS->getOpcode() != ISD::BUILD_VECTOR) {
    std::swap(LHS, RHS);
    VecShuffle = peekThroughBitcasts(DAG.getCommutedVectorShuffle(*SVN));
    ShuffleVectorSDNode *CommutedSV = dyn_cast<ShuffleVectorSDNode>(VecShuffle);
    if (!CommutedSV)
      return SDValue();
    ShuffleMask = CommutedSV->getMask();
  }

  // Ensure that the RHS is a vector of constants.
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(RHS.getNode());
  if (!BVN)
    return SDValue();

  // Check if RHS is a splat of 4-bytes (or smaller).
  APInt APSplatValue, APSplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (!BVN->isConstantSplat(APSplatValue, APSplatUndef, SplatBitSize,
                            HasAnyUndefs, 0, !Subtarget.isLittleEndian()) ||
      SplatBitSize > 32)
    return SDValue();

  // Check that the shuffle mask matches the semantics of XXSPLTI32DX.
  // The instruction splats a constant C into two words of the source vector
  // producing { C, Unchanged, C, Unchanged } or { Unchanged, C, Unchanged, C }.
  // Thus we check that the shuffle mask is the equivalent  of
  // <0, [4-7], 2, [4-7]> or <[4-7], 1, [4-7], 3> respectively.
  // Note: the check above of isNByteElemShuffleMask() ensures that the bytes
  // within each word are consecutive, so we only need to check the first byte.
  SDValue Index;
  bool IsLE = Subtarget.isLittleEndian();
  if ((ShuffleMask[0] == 0 && ShuffleMask[8] == 8) &&
      (ShuffleMask[4] % 4 == 0 && ShuffleMask[12] % 4 == 0 &&
       ShuffleMask[4] > 15 && ShuffleMask[12] > 15))
    Index = DAG.getTargetConstant(IsLE ? 0 : 1, DL, MVT::i32);
  else if ((ShuffleMask[4] == 4 && ShuffleMask[12] == 12) &&
           (ShuffleMask[0] % 4 == 0 && ShuffleMask[8] % 4 == 0 &&
            ShuffleMask[0] > 15 && ShuffleMask[8] > 15))
    Index = DAG.getTargetConstant(IsLE ? 1 : 0, DL, MVT::i32);
  else
    return SDValue();

  // If the splat is narrower than 32-bits, we need to get the 32-bit value
  // for XXSPLTI32DX.
  unsigned SplatVal = APSplatValue.getZExtValue();
  for (; SplatBitSize < 32; SplatBitSize <<= 1)
    SplatVal |= (SplatVal << SplatBitSize);

  SDValue SplatNode = DAG.getNode(
      PPCISD::XXSPLTI32DX, DL, MVT::v2i64, DAG.getBitcast(MVT::v2i64, LHS),
      Index, DAG.getTargetConstant(SplatVal, DL, MVT::i32));
  return DAG.getNode(ISD::BITCAST, DL, MVT::v16i8, SplatNode);
}

/// LowerROTL - Custom lowering for ROTL(v1i128) to vector_shuffle(v16i8).
/// We lower ROTL(v1i128) to vector_shuffle(v16i8) only if shift amount is
/// a multiple of 8. Otherwise convert it to a scalar rotation(i128)
/// i.e (or (shl x, C1), (srl x, 128-C1)).
SDValue PPCTargetLowering::LowerROTL(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::ROTL && "Should only be called for ISD::ROTL");
  assert(Op.getValueType() == MVT::v1i128 &&
         "Only set v1i128 as custom, other type shouldn't reach here!");
  SDLoc dl(Op);
  SDValue N0 = peekThroughBitcasts(Op.getOperand(0));
  SDValue N1 = peekThroughBitcasts(Op.getOperand(1));
  unsigned SHLAmt = N1.getConstantOperandVal(0);
  if (SHLAmt % 8 == 0) {
    std::array<int, 16> Mask;
    std::iota(Mask.begin(), Mask.end(), 0);
    std::rotate(Mask.begin(), Mask.begin() + SHLAmt / 8, Mask.end());
    if (SDValue Shuffle =
            DAG.getVectorShuffle(MVT::v16i8, dl, DAG.getBitcast(MVT::v16i8, N0),
                                 DAG.getUNDEF(MVT::v16i8), Mask))
      return DAG.getNode(ISD::BITCAST, dl, MVT::v1i128, Shuffle);
  }
  SDValue ArgVal = DAG.getBitcast(MVT::i128, N0);
  SDValue SHLOp = DAG.getNode(ISD::SHL, dl, MVT::i128, ArgVal,
                              DAG.getConstant(SHLAmt, dl, MVT::i32));
  SDValue SRLOp = DAG.getNode(ISD::SRL, dl, MVT::i128, ArgVal,
                              DAG.getConstant(128 - SHLAmt, dl, MVT::i32));
  SDValue OROp = DAG.getNode(ISD::OR, dl, MVT::i128, SHLOp, SRLOp);
  return DAG.getNode(ISD::BITCAST, dl, MVT::v1i128, OROp);
}

/// LowerVECTOR_SHUFFLE - Return the code we lower for VECTOR_SHUFFLE.  If this
/// is a shuffle we can handle in a single instruction, return it.  Otherwise,
/// return the code it can be lowered into.  Worst case, it can always be
/// lowered into a vperm.
SDValue PPCTargetLowering::LowerVECTOR_SHUFFLE(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc dl(Op);
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(Op);

  // Any nodes that were combined in the target-independent combiner prior
  // to vector legalization will not be sent to the target combine. Try to
  // combine it here.
  if (SDValue NewShuffle = combineVectorShuffle(SVOp, DAG)) {
    if (!isa<ShuffleVectorSDNode>(NewShuffle))
      return NewShuffle;
    Op = NewShuffle;
    SVOp = cast<ShuffleVectorSDNode>(Op);
    V1 = Op.getOperand(0);
    V2 = Op.getOperand(1);
  }
  EVT VT = Op.getValueType();
  bool isLittleEndian = Subtarget.isLittleEndian();

  unsigned ShiftElts, InsertAtByte;
  bool Swap = false;

  // If this is a load-and-splat, we can do that with a single instruction
  // in some cases. However if the load has multiple uses, we don't want to
  // combine it because that will just produce multiple loads.
  bool IsPermutedLoad = false;
  const SDValue *InputLoad = getNormalLoadInput(V1, IsPermutedLoad);
  if (InputLoad && Subtarget.hasVSX() && V2.isUndef() &&
      (PPC::isSplatShuffleMask(SVOp, 4) || PPC::isSplatShuffleMask(SVOp, 8)) &&
      InputLoad->hasOneUse()) {
    bool IsFourByte = PPC::isSplatShuffleMask(SVOp, 4);
    int SplatIdx =
      PPC::getSplatIdxForPPCMnemonics(SVOp, IsFourByte ? 4 : 8, DAG);

    // The splat index for permuted loads will be in the left half of the vector
    // which is strictly wider than the loaded value by 8 bytes. So we need to
    // adjust the splat index to point to the correct address in memory.
    if (IsPermutedLoad) {
      assert((isLittleEndian || IsFourByte) &&
             "Unexpected size for permuted load on big endian target");
      SplatIdx += IsFourByte ? 2 : 1;
      assert((SplatIdx < (IsFourByte ? 4 : 2)) &&
             "Splat of a value outside of the loaded memory");
    }

    LoadSDNode *LD = cast<LoadSDNode>(*InputLoad);
    // For 4-byte load-and-splat, we need Power9.
    if ((IsFourByte && Subtarget.hasP9Vector()) || !IsFourByte) {
      uint64_t Offset = 0;
      if (IsFourByte)
        Offset = isLittleEndian ? (3 - SplatIdx) * 4 : SplatIdx * 4;
      else
        Offset = isLittleEndian ? (1 - SplatIdx) * 8 : SplatIdx * 8;

      // If the width of the load is the same as the width of the splat,
      // loading with an offset would load the wrong memory.
      if (LD->getValueType(0).getSizeInBits() == (IsFourByte ? 32 : 64))
        Offset = 0;

      SDValue BasePtr = LD->getBasePtr();
      if (Offset != 0)
        BasePtr = DAG.getNode(ISD::ADD, dl, getPointerTy(DAG.getDataLayout()),
                              BasePtr, DAG.getIntPtrConstant(Offset, dl));
      SDValue Ops[] = {
        LD->getChain(),    // Chain
        BasePtr,           // BasePtr
        DAG.getValueType(Op.getValueType()) // VT
      };
      SDVTList VTL =
        DAG.getVTList(IsFourByte ? MVT::v4i32 : MVT::v2i64, MVT::Other);
      SDValue LdSplt =
        DAG.getMemIntrinsicNode(PPCISD::LD_SPLAT, dl, VTL,
                                Ops, LD->getMemoryVT(), LD->getMemOperand());
      DAG.ReplaceAllUsesOfValueWith(InputLoad->getValue(1), LdSplt.getValue(1));
      if (LdSplt.getValueType() != SVOp->getValueType(0))
        LdSplt = DAG.getBitcast(SVOp->getValueType(0), LdSplt);
      return LdSplt;
    }
  }

  // All v2i64 and v2f64 shuffles are legal
  if (VT == MVT::v2i64 || VT == MVT::v2f64)
    return Op;

  if (Subtarget.hasP9Vector() &&
      PPC::isXXINSERTWMask(SVOp, ShiftElts, InsertAtByte, Swap,
                           isLittleEndian)) {
    if (V2.isUndef())
      V2 = V1;
    else if (Swap)
      std::swap(V1, V2);
    SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
    SDValue Conv2 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V2);
    if (ShiftElts) {
      SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v4i32, Conv2, Conv2,
                                DAG.getConstant(ShiftElts, dl, MVT::i32));
      SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v4i32, Conv1, Shl,
                                DAG.getConstant(InsertAtByte, dl, MVT::i32));
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
    }
    SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v4i32, Conv1, Conv2,
                              DAG.getConstant(InsertAtByte, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
  }

  if (Subtarget.hasPrefixInstrs() && Subtarget.hasP10Vector()) {
    SDValue SplatInsertNode;
    if ((SplatInsertNode = lowerToXXSPLTI32DX(SVOp, DAG)))
      return SplatInsertNode;
  }

  if (Subtarget.hasP9Altivec()) {
    SDValue NewISDNode;
    if ((NewISDNode = lowerToVINSERTH(SVOp, DAG)))
      return NewISDNode;

    if ((NewISDNode = lowerToVINSERTB(SVOp, DAG)))
      return NewISDNode;
  }

  if (Subtarget.hasVSX() &&
      PPC::isXXSLDWIShuffleMask(SVOp, ShiftElts, Swap, isLittleEndian)) {
    if (Swap)
      std::swap(V1, V2);
    SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
    SDValue Conv2 =
        DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V2.isUndef() ? V1 : V2);

    SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v4i32, Conv1, Conv2,
                              DAG.getConstant(ShiftElts, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Shl);
  }

  if (Subtarget.hasVSX() &&
    PPC::isXXPERMDIShuffleMask(SVOp, ShiftElts, Swap, isLittleEndian)) {
    if (Swap)
      std::swap(V1, V2);
    SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v2i64, V1);
    SDValue Conv2 =
        DAG.getNode(ISD::BITCAST, dl, MVT::v2i64, V2.isUndef() ? V1 : V2);

    SDValue PermDI = DAG.getNode(PPCISD::XXPERMDI, dl, MVT::v2i64, Conv1, Conv2,
                              DAG.getConstant(ShiftElts, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, PermDI);
  }

  if (Subtarget.hasP9Vector()) {
     if (PPC::isXXBRHShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, V1);
      SDValue ReveHWord = DAG.getNode(ISD::BSWAP, dl, MVT::v8i16, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveHWord);
    } else if (PPC::isXXBRWShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
      SDValue ReveWord = DAG.getNode(ISD::BSWAP, dl, MVT::v4i32, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveWord);
    } else if (PPC::isXXBRDShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v2i64, V1);
      SDValue ReveDWord = DAG.getNode(ISD::BSWAP, dl, MVT::v2i64, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveDWord);
    } else if (PPC::isXXBRQShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v1i128, V1);
      SDValue ReveQWord = DAG.getNode(ISD::BSWAP, dl, MVT::v1i128, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveQWord);
    }
  }

  if (Subtarget.hasVSX()) {
    if (V2.isUndef() && PPC::isSplatShuffleMask(SVOp, 4)) {
      int SplatIdx = PPC::getSplatIdxForPPCMnemonics(SVOp, 4, DAG);

      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
      SDValue Splat = DAG.getNode(PPCISD::XXSPLT, dl, MVT::v4i32, Conv,
                                  DAG.getConstant(SplatIdx, dl, MVT::i32));
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Splat);
    }

    // Left shifts of 8 bytes are actually swaps. Convert accordingly.
    if (V2.isUndef() && PPC::isVSLDOIShuffleMask(SVOp, 1, DAG) == 8) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v2f64, V1);
      SDValue Swap = DAG.getNode(PPCISD::SWAP_NO_CHAIN, dl, MVT::v2f64, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Swap);
    }
  }

  // Cases that are handled by instructions that take permute immediates
  // (such as vsplt*) should be left as VECTOR_SHUFFLE nodes so they can be
  // selected by the instruction selector.
  if (V2.isUndef()) {
    if (PPC::isSplatShuffleMask(SVOp, 1) ||
        PPC::isSplatShuffleMask(SVOp, 2) ||
        PPC::isSplatShuffleMask(SVOp, 4) ||
        PPC::isVPKUWUMShuffleMask(SVOp, 1, DAG) ||
        PPC::isVPKUHUMShuffleMask(SVOp, 1, DAG) ||
        PPC::isVSLDOIShuffleMask(SVOp, 1, DAG) != -1 ||
        PPC::isVMRGLShuffleMask(SVOp, 1, 1, DAG) ||
        PPC::isVMRGLShuffleMask(SVOp, 2, 1, DAG) ||
        PPC::isVMRGLShuffleMask(SVOp, 4, 1, DAG) ||
        PPC::isVMRGHShuffleMask(SVOp, 1, 1, DAG) ||
        PPC::isVMRGHShuffleMask(SVOp, 2, 1, DAG) ||
        PPC::isVMRGHShuffleMask(SVOp, 4, 1, DAG) ||
        (Subtarget.hasP8Altivec() && (
         PPC::isVPKUDUMShuffleMask(SVOp, 1, DAG) ||
         PPC::isVMRGEOShuffleMask(SVOp, true, 1, DAG) ||
         PPC::isVMRGEOShuffleMask(SVOp, false, 1, DAG)))) {
      return Op;
    }
  }

  // Altivec has a variety of "shuffle immediates" that take two vector inputs
  // and produce a fixed permutation.  If any of these match, do not lower to
  // VPERM.
  unsigned int ShuffleKind = isLittleEndian ? 2 : 0;
  if (PPC::isVPKUWUMShuffleMask(SVOp, ShuffleKind, DAG) ||
      PPC::isVPKUHUMShuffleMask(SVOp, ShuffleKind, DAG) ||
      PPC::isVSLDOIShuffleMask(SVOp, ShuffleKind, DAG) != -1 ||
      PPC::isVMRGLShuffleMask(SVOp, 1, ShuffleKind, DAG) ||
      PPC::isVMRGLShuffleMask(SVOp, 2, ShuffleKind, DAG) ||
      PPC::isVMRGLShuffleMask(SVOp, 4, ShuffleKind, DAG) ||
      PPC::isVMRGHShuffleMask(SVOp, 1, ShuffleKind, DAG) ||
      PPC::isVMRGHShuffleMask(SVOp, 2, ShuffleKind, DAG) ||
      PPC::isVMRGHShuffleMask(SVOp, 4, ShuffleKind, DAG) ||
      (Subtarget.hasP8Altivec() && (
       PPC::isVPKUDUMShuffleMask(SVOp, ShuffleKind, DAG) ||
       PPC::isVMRGEOShuffleMask(SVOp, true, ShuffleKind, DAG) ||
       PPC::isVMRGEOShuffleMask(SVOp, false, ShuffleKind, DAG))))
    return Op;

  // Check to see if this is a shuffle of 4-byte values.  If so, we can use our
  // perfect shuffle table to emit an optimal matching sequence.
  ArrayRef<int> PermMask = SVOp->getMask();

  if (!DisablePerfectShuffle && !isLittleEndian) {
    unsigned PFIndexes[4];
    bool isFourElementShuffle = true;
    for (unsigned i = 0; i != 4 && isFourElementShuffle;
         ++i) {                           // Element number
      unsigned EltNo = 8;                 // Start out undef.
      for (unsigned j = 0; j != 4; ++j) { // Intra-element byte.
        if (PermMask[i * 4 + j] < 0)
          continue; // Undef, ignore it.

        unsigned ByteSource = PermMask[i * 4 + j];
        if ((ByteSource & 3) != j) {
          isFourElementShuffle = false;
          break;
        }

        if (EltNo == 8) {
          EltNo = ByteSource / 4;
        } else if (EltNo != ByteSource / 4) {
          isFourElementShuffle = false;
          break;
        }
      }
      PFIndexes[i] = EltNo;
    }

    // If this shuffle can be expressed as a shuffle of 4-byte elements, use the
    // perfect shuffle vector to determine if it is cost effective to do this as
    // discrete instructions, or whether we should use a vperm.
    // For now, we skip this for little endian until such time as we have a
    // little-endian perfect shuffle table.
    if (isFourElementShuffle) {
      // Compute the index in the perfect shuffle table.
      unsigned PFTableIndex = PFIndexes[0] * 9 * 9 * 9 + PFIndexes[1] * 9 * 9 +
                              PFIndexes[2] * 9 + PFIndexes[3];

      unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
      unsigned Cost = (PFEntry >> 30);

      // Determining when to avoid vperm is tricky.  Many things affect the cost
      // of vperm, particularly how many times the perm mask needs to be
      // computed. For example, if the perm mask can be hoisted out of a loop or
      // is already used (perhaps because there are multiple permutes with the
      // same shuffle mask?) the vperm has a cost of 1.  OTOH, hoisting the
      // permute mask out of the loop requires an extra register.
      //
      // As a compromise, we only emit discrete instructions if the shuffle can
      // be generated in 3 or fewer operations.  When we have loop information
      // available, if this block is within a loop, we should avoid using vperm
      // for 3-operation perms and use a constant pool load instead.
      if (Cost < 3)
        return GeneratePerfectShuffle(PFEntry, V1, V2, DAG, dl);
    }
  }

  // Lower this to a VPERM(V1, V2, V3) expression, where V3 is a constant
  // vector that will get spilled to the constant pool.
  if (V2.isUndef()) V2 = V1;

  return LowerVPERM(Op, DAG, PermMask, VT, V1, V2);
}

SDValue PPCTargetLowering::LowerVPERM(SDValue Op, SelectionDAG &DAG,
                                      ArrayRef<int> PermMask, EVT VT,
                                      SDValue V1, SDValue V2) const {
  unsigned Opcode = PPCISD::VPERM;
  EVT ValType = V1.getValueType();
  SDLoc dl(Op);
  bool NeedSwap = false;
  bool isLittleEndian = Subtarget.isLittleEndian();
  bool isPPC64 = Subtarget.isPPC64();

  if (Subtarget.hasVSX() && Subtarget.hasP9Vector() &&
      (V1->hasOneUse() || V2->hasOneUse())) {
    LLVM_DEBUG(dbgs() << "At least one of two input vectors are dead - using "
                         "XXPERM instead\n");
    Opcode = PPCISD::XXPERM;

    // The second input to XXPERM is also an output so if the second input has
    // multiple uses then copying is necessary, as a result we want the
    // single-use operand to be used as the second input to prevent copying.
    if ((!isLittleEndian && !V2->hasOneUse() && V1->hasOneUse()) ||
        (isLittleEndian && !V1->hasOneUse() && V2->hasOneUse())) {
      std::swap(V1, V2);
      NeedSwap = !NeedSwap;
    }
  }

  // The SHUFFLE_VECTOR mask is almost exactly what we want for vperm, except
  // that it is in input element units, not in bytes.  Convert now.

  // For little endian, the order of the input vectors is reversed, and
  // the permutation mask is complemented with respect to 31.  This is
  // necessary to produce proper semantics with the big-endian-based vperm
  // instruction.
  EVT EltVT = V1.getValueType().getVectorElementType();
  unsigned BytesPerElement = EltVT.getSizeInBits() / 8;

  bool V1HasXXSWAPD = V1->getOperand(0)->getOpcode() == PPCISD::XXSWAPD;
  bool V2HasXXSWAPD = V2->getOperand(0)->getOpcode() == PPCISD::XXSWAPD;

  /*
  Vectors will be appended like so: [ V1 | v2 ]
  XXSWAPD on V1:
  [   A   |   B   |   C   |   D   ] -> [   C   |   D   |   A   |   B   ]
     0-3     4-7     8-11   12-15         0-3     4-7     8-11   12-15
  i.e.  index of A, B += 8, and index of C, D -= 8.
  XXSWAPD on V2:
  [   E   |   F   |   G   |   H   ] -> [   G   |   H   |   E   |   F   ]
    16-19   20-23   24-27   28-31        16-19   20-23   24-27   28-31
  i.e.  index of E, F += 8, index of G, H -= 8
  Swap V1 and V2:
  [   V1   |   V2  ] -> [   V2   |   V1   ]
     0-15     16-31        0-15     16-31
  i.e.  index of V1 += 16, index of V2 -= 16
  */

  SmallVector<SDValue, 16> ResultMask;
  for (unsigned i = 0, e = VT.getVectorNumElements(); i != e; ++i) {
    unsigned SrcElt = PermMask[i] < 0 ? 0 : PermMask[i];

    if (V1HasXXSWAPD) {
      if (SrcElt < 8)
        SrcElt += 8;
      else if (SrcElt < 16)
        SrcElt -= 8;
    }
    if (V2HasXXSWAPD) {
      if (SrcElt > 23)
        SrcElt -= 8;
      else if (SrcElt > 15)
        SrcElt += 8;
    }
    if (NeedSwap) {
      if (SrcElt < 16)
        SrcElt += 16;
      else
        SrcElt -= 16;
    }
    for (unsigned j = 0; j != BytesPerElement; ++j)
      if (isLittleEndian)
        ResultMask.push_back(
            DAG.getConstant(31 - (SrcElt * BytesPerElement + j), dl, MVT::i32));
      else
        ResultMask.push_back(
            DAG.getConstant(SrcElt * BytesPerElement + j, dl, MVT::i32));
  }

  if (V1HasXXSWAPD) {
    dl = SDLoc(V1->getOperand(0));
    V1 = V1->getOperand(0)->getOperand(1);
  }
  if (V2HasXXSWAPD) {
    dl = SDLoc(V2->getOperand(0));
    V2 = V2->getOperand(0)->getOperand(1);
  }

  if (isPPC64 && (V1HasXXSWAPD || V2HasXXSWAPD)) {
    if (ValType != MVT::v2f64)
      V1 = DAG.getBitcast(MVT::v2f64, V1);
    if (V2.getValueType() != MVT::v2f64)
      V2 = DAG.getBitcast(MVT::v2f64, V2);
  }

  ShufflesHandledWithVPERM++;
  SDValue VPermMask = DAG.getBuildVector(MVT::v16i8, dl, ResultMask);
  LLVM_DEBUG({
    ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(Op);
    if (Opcode == PPCISD::XXPERM) {
      dbgs() << "Emitting a XXPERM for the following shuffle:\n";
    } else {
      dbgs() << "Emitting a VPERM for the following shuffle:\n";
    }
    SVOp->dump();
    dbgs() << "With the following permute control vector:\n";
    VPermMask.dump();
  });

  if (Opcode == PPCISD::XXPERM)
    VPermMask = DAG.getBitcast(MVT::v4i32, VPermMask);

  // Only need to place items backwards in LE,
  // the mask was properly calculated.
  if (isLittleEndian)
    std::swap(V1, V2);

  SDValue VPERMNode =
      DAG.getNode(Opcode, dl, V1.getValueType(), V1, V2, VPermMask);

  VPERMNode = DAG.getBitcast(ValType, VPERMNode);
  return VPERMNode;
}

/// getVectorCompareInfo - Given an intrinsic, return false if it is not a
/// vector comparison.  If it is, return true and fill in Opc/isDot with
/// information about the intrinsic.
static bool getVectorCompareInfo(SDValue Intrin, int &CompareOpc,
                                 bool &isDot, const PPCSubtarget &Subtarget) {
  unsigned IntrinsicID = Intrin.getConstantOperandVal(0);
  CompareOpc = -1;
  isDot = false;
  switch (IntrinsicID) {
  default:
    return false;
  // Comparison predicates.
  case Intrinsic::ppc_altivec_vcmpbfp_p:
    CompareOpc = 966;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpeqfp_p:
    CompareOpc = 198;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequb_p:
    CompareOpc = 6;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequh_p:
    CompareOpc = 70;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequw_p:
    CompareOpc = 134;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequd_p:
    if (Subtarget.hasVSX() || Subtarget.hasP8Altivec()) {
      CompareOpc = 199;
      isDot = true;
    } else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpneb_p:
  case Intrinsic::ppc_altivec_vcmpneh_p:
  case Intrinsic::ppc_altivec_vcmpnew_p:
  case Intrinsic::ppc_altivec_vcmpnezb_p:
  case Intrinsic::ppc_altivec_vcmpnezh_p:
  case Intrinsic::ppc_altivec_vcmpnezw_p:
    if (Subtarget.hasP9Altivec()) {
      switch (IntrinsicID) {
      default:
        llvm_unreachable("Unknown comparison intrinsic.");
      case Intrinsic::ppc_altivec_vcmpneb_p:
        CompareOpc = 7;
        break;
      case Intrinsic::ppc_altivec_vcmpneh_p:
        CompareOpc = 71;
        break;
      case Intrinsic::ppc_altivec_vcmpnew_p:
        CompareOpc = 135;
        break;
      case Intrinsic::ppc_altivec_vcmpnezb_p:
        CompareOpc = 263;
        break;
      case Intrinsic::ppc_altivec_vcmpnezh_p:
        CompareOpc = 327;
        break;
      case Intrinsic::ppc_altivec_vcmpnezw_p:
        CompareOpc = 391;
        break;
      }
      isDot = true;
    } else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgefp_p:
    CompareOpc = 454;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtfp_p:
    CompareOpc = 710;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsb_p:
    CompareOpc = 774;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsh_p:
    CompareOpc = 838;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsw_p:
    CompareOpc = 902;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsd_p:
    if (Subtarget.hasVSX() || Subtarget.hasP8Altivec()) {
      CompareOpc = 967;
      isDot = true;
    } else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgtub_p:
    CompareOpc = 518;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuh_p:
    CompareOpc = 582;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuw_p:
    CompareOpc = 646;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtud_p:
    if (Subtarget.hasVSX() || Subtarget.hasP8Altivec()) {
      CompareOpc = 711;
      isDot = true;
    } else
      return false;
    break;

  case Intrinsic::ppc_altivec_vcmpequq:
  case Intrinsic::ppc_altivec_vcmpgtsq:
  case Intrinsic::ppc_altivec_vcmpgtuq:
    if (!Subtarget.isISA3_1())
      return false;
    switch (IntrinsicID) {
    default:
      llvm_unreachable("Unknown comparison intrinsic.");
    case Intrinsic::ppc_altivec_vcmpequq:
      CompareOpc = 455;
      break;
    case Intrinsic::ppc_altivec_vcmpgtsq:
      CompareOpc = 903;
      break;
    case Intrinsic::ppc_altivec_vcmpgtuq:
      CompareOpc = 647;
      break;
    }
    break;

  // VSX predicate comparisons use the same infrastructure
  case Intrinsic::ppc_vsx_xvcmpeqdp_p:
  case Intrinsic::ppc_vsx_xvcmpgedp_p:
  case Intrinsic::ppc_vsx_xvcmpgtdp_p:
  case Intrinsic::ppc_vsx_xvcmpeqsp_p:
  case Intrinsic::ppc_vsx_xvcmpgesp_p:
  case Intrinsic::ppc_vsx_xvcmpgtsp_p:
    if (Subtarget.hasVSX()) {
      switch (IntrinsicID) {
      case Intrinsic::ppc_vsx_xvcmpeqdp_p:
        CompareOpc = 99;
        break;
      case Intrinsic::ppc_vsx_xvcmpgedp_p:
        CompareOpc = 115;
        break;
      case Intrinsic::ppc_vsx_xvcmpgtdp_p:
        CompareOpc = 107;
        break;
      case Intrinsic::ppc_vsx_xvcmpeqsp_p:
        CompareOpc = 67;
        break;
      case Intrinsic::ppc_vsx_xvcmpgesp_p:
        CompareOpc = 83;
        break;
      case Intrinsic::ppc_vsx_xvcmpgtsp_p:
        CompareOpc = 75;
        break;
      }
      isDot = true;
    } else
      return false;
    break;

  // Normal Comparisons.
  case Intrinsic::ppc_altivec_vcmpbfp:
    CompareOpc = 966;
    break;
  case Intrinsic::ppc_altivec_vcmpeqfp:
    CompareOpc = 198;
    break;
  case Intrinsic::ppc_altivec_vcmpequb:
    CompareOpc = 6;
    break;
  case Intrinsic::ppc_altivec_vcmpequh:
    CompareOpc = 70;
    break;
  case Intrinsic::ppc_altivec_vcmpequw:
    CompareOpc = 134;
    break;
  case Intrinsic::ppc_altivec_vcmpequd:
    if (Subtarget.hasP8Altivec())
      CompareOpc = 199;
    else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpneb:
  case Intrinsic::ppc_altivec_vcmpneh:
  case Intrinsic::ppc_altivec_vcmpnew:
  case Intrinsic::ppc_altivec_vcmpnezb:
  case Intrinsic::ppc_altivec_vcmpnezh:
  case Intrinsic::ppc_altivec_vcmpnezw:
    if (Subtarget.hasP9Altivec())
      switch (IntrinsicID) {
      default:
        llvm_unreachable("Unknown comparison intrinsic.");
      case Intrinsic::ppc_altivec_vcmpneb:
        CompareOpc = 7;
        break;
      case Intrinsic::ppc_altivec_vcmpneh:
        CompareOpc = 71;
        break;
      case Intrinsic::ppc_altivec_vcmpnew:
        CompareOpc = 135;
        break;
      case Intrinsic::ppc_altivec_vcmpnezb:
        CompareOpc = 263;
        break;
      case Intrinsic::ppc_altivec_vcmpnezh:
        CompareOpc = 327;
        break;
      case Intrinsic::ppc_altivec_vcmpnezw:
        CompareOpc = 391;
        break;
      }
    else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgefp:
    CompareOpc = 454;
    break;
  case Intrinsic::ppc_altivec_vcmpgtfp:
    CompareOpc = 710;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsb:
    CompareOpc = 774;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsh:
    CompareOpc = 838;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsw:
    CompareOpc = 902;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsd:
    if (Subtarget.hasP8Altivec())
      CompareOpc = 967;
    else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgtub:
    CompareOpc = 518;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuh:
    CompareOpc = 582;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuw:
    CompareOpc = 646;
    break;
  case Intrinsic::ppc_altivec_vcmpgtud:
    if (Subtarget.hasP8Altivec())
      CompareOpc = 711;
    else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpequq_p:
  case Intrinsic::ppc_altivec_vcmpgtsq_p:
  case Intrinsic::ppc_altivec_vcmpgtuq_p:
    if (!Subtarget.isISA3_1())
      return false;
    switch (IntrinsicID) {
    default:
      llvm_unreachable("Unknown comparison intrinsic.");
    case Intrinsic::ppc_altivec_vcmpequq_p:
      CompareOpc = 455;
      break;
    case Intrinsic::ppc_altivec_vcmpgtsq_p:
      CompareOpc = 903;
      break;
    case Intrinsic::ppc_altivec_vcmpgtuq_p:
      CompareOpc = 647;
      break;
    }
    isDot = true;
    break;
  }
  return true;
}

/// LowerINTRINSIC_WO_CHAIN - If this is an intrinsic that we want to custom
/// lower, do it, otherwise return null.
SDValue PPCTargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                   SelectionDAG &DAG) const {
  unsigned IntrinsicID = Op.getConstantOperandVal(0);

  SDLoc dl(Op);

  switch (IntrinsicID) {
  case Intrinsic::thread_pointer:
    // Reads the thread pointer register, used for __builtin_thread_pointer.
    if (Subtarget.isPPC64())
      return DAG.getRegister(PPC::X13, MVT::i64);
    return DAG.getRegister(PPC::R2, MVT::i32);

  case Intrinsic::ppc_rldimi: {
    assert(Subtarget.isPPC64() && "rldimi is only available in 64-bit!");
    SDValue Src = Op.getOperand(1);
    APInt Mask = Op.getConstantOperandAPInt(4);
    if (Mask.isZero())
      return Op.getOperand(2);
    if (Mask.isAllOnes())
      return DAG.getNode(ISD::ROTL, dl, MVT::i64, Src, Op.getOperand(3));
    uint64_t SH = Op.getConstantOperandVal(3);
    unsigned MB = 0, ME = 0;
    if (!isRunOfOnes64(Mask.getZExtValue(), MB, ME))
      report_fatal_error("invalid rldimi mask!");
    // rldimi requires ME=63-SH, otherwise rotation is needed before rldimi.
    if (ME < 63 - SH) {
      Src = DAG.getNode(ISD::ROTL, dl, MVT::i64, Src,
                        DAG.getConstant(ME + SH + 1, dl, MVT::i32));
    } else if (ME > 63 - SH) {
      Src = DAG.getNode(ISD::ROTL, dl, MVT::i64, Src,
                        DAG.getConstant(ME + SH - 63, dl, MVT::i32));
    }
    return SDValue(
        DAG.getMachineNode(PPC::RLDIMI, dl, MVT::i64,
                           {Op.getOperand(2), Src,
                            DAG.getTargetConstant(63 - ME, dl, MVT::i32),
                            DAG.getTargetConstant(MB, dl, MVT::i32)}),
        0);
  }

  case Intrinsic::ppc_rlwimi: {
    APInt Mask = Op.getConstantOperandAPInt(4);
    if (Mask.isZero())
      return Op.getOperand(2);
    if (Mask.isAllOnes())
      return DAG.getNode(ISD::ROTL, dl, MVT::i32, Op.getOperand(1),
                         Op.getOperand(3));
    unsigned MB = 0, ME = 0;
    if (!isRunOfOnes(Mask.getZExtValue(), MB, ME))
      report_fatal_error("invalid rlwimi mask!");
    return SDValue(DAG.getMachineNode(
                       PPC::RLWIMI, dl, MVT::i32,
                       {Op.getOperand(2), Op.getOperand(1), Op.getOperand(3),
                        DAG.getTargetConstant(MB, dl, MVT::i32),
                        DAG.getTargetConstant(ME, dl, MVT::i32)}),
                   0);
  }

  case Intrinsic::ppc_rlwnm: {
    if (Op.getConstantOperandVal(3) == 0)
      return DAG.getConstant(0, dl, MVT::i32);
    unsigned MB = 0, ME = 0;
    if (!isRunOfOnes(Op.getConstantOperandVal(3), MB, ME))
      report_fatal_error("invalid rlwnm mask!");
    return SDValue(
        DAG.getMachineNode(PPC::RLWNM, dl, MVT::i32,
                           {Op.getOperand(1), Op.getOperand(2),
                            DAG.getTargetConstant(MB, dl, MVT::i32),
                            DAG.getTargetConstant(ME, dl, MVT::i32)}),
        0);
  }

  case Intrinsic::ppc_mma_disassemble_acc: {
    if (Subtarget.isISAFuture()) {
      EVT ReturnTypes[] = {MVT::v256i1, MVT::v256i1};
      SDValue WideVec =
          SDValue(DAG.getMachineNode(PPC::DMXXEXTFDMR512, dl, ReturnTypes,
                                     Op.getOperand(1)),
                  0);
      SmallVector<SDValue, 4> RetOps;
      SDValue Value = SDValue(WideVec.getNode(), 0);
      SDValue Value2 = SDValue(WideVec.getNode(), 1);

      SDValue Extract;
      Extract = DAG.getNode(
          PPCISD::EXTRACT_VSX_REG, dl, MVT::v16i8,
          Subtarget.isLittleEndian() ? Value2 : Value,
          DAG.getConstant(Subtarget.isLittleEndian() ? 1 : 0,
                          dl, getPointerTy(DAG.getDataLayout())));
      RetOps.push_back(Extract);
      Extract = DAG.getNode(
          PPCISD::EXTRACT_VSX_REG, dl, MVT::v16i8,
          Subtarget.isLittleEndian() ? Value2 : Value,
          DAG.getConstant(Subtarget.isLittleEndian() ? 0 : 1,
                          dl, getPointerTy(DAG.getDataLayout())));
      RetOps.push_back(Extract);
      Extract = DAG.getNode(
          PPCISD::EXTRACT_VSX_REG, dl, MVT::v16i8,
          Subtarget.isLittleEndian() ? Value : Value2,
          DAG.getConstant(Subtarget.isLittleEndian() ? 1 : 0,
                          dl, getPointerTy(DAG.getDataLayout())));
      RetOps.push_back(Extract);
      Extract = DAG.getNode(
          PPCISD::EXTRACT_VSX_REG, dl, MVT::v16i8,
          Subtarget.isLittleEndian() ? Value : Value2,
          DAG.getConstant(Subtarget.isLittleEndian() ? 0 : 1,
                          dl, getPointerTy(DAG.getDataLayout())));
      RetOps.push_back(Extract);
      return DAG.getMergeValues(RetOps, dl);
    }
    [[fallthrough]];
  }
  case Intrinsic::ppc_vsx_disassemble_pair: {
    int NumVecs = 2;
    SDValue WideVec = Op.getOperand(1);
    if (IntrinsicID == Intrinsic::ppc_mma_disassemble_acc) {
      NumVecs = 4;
      WideVec = DAG.getNode(PPCISD::XXMFACC, dl, MVT::v512i1, WideVec);
    }
    SmallVector<SDValue, 4> RetOps;
    for (int VecNo = 0; VecNo < NumVecs; VecNo++) {
      SDValue Extract = DAG.getNode(
          PPCISD::EXTRACT_VSX_REG, dl, MVT::v16i8, WideVec,
          DAG.getConstant(Subtarget.isLittleEndian() ? NumVecs - 1 - VecNo
                                                     : VecNo,
                          dl, getPointerTy(DAG.getDataLayout())));
      RetOps.push_back(Extract);
    }
    return DAG.getMergeValues(RetOps, dl);
  }

  case Intrinsic::ppc_mma_xxmfacc:
  case Intrinsic::ppc_mma_xxmtacc: {
    // Allow pre-isa-future subtargets to lower as normal.
    if (!Subtarget.isISAFuture())
      return SDValue();
    // The intrinsics for xxmtacc and xxmfacc take one argument of
    // type v512i1, for future cpu the corresponding wacc instruction
    // dmxx[inst|extf]dmr512 is always generated for type v512i1, negating
    // the need to produce the xxm[t|f]acc.
    SDValue WideVec = Op.getOperand(1);
    DAG.ReplaceAllUsesWith(Op, WideVec);
    return SDValue();
  }

  case Intrinsic::ppc_unpack_longdouble: {
    auto *Idx = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    assert(Idx && (Idx->getSExtValue() == 0 || Idx->getSExtValue() == 1) &&
           "Argument of long double unpack must be 0 or 1!");
    return DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::f64, Op.getOperand(1),
                       DAG.getConstant(!!(Idx->getSExtValue()), dl,
                                       Idx->getValueType(0)));
  }

  case Intrinsic::ppc_compare_exp_lt:
  case Intrinsic::ppc_compare_exp_gt:
  case Intrinsic::ppc_compare_exp_eq:
  case Intrinsic::ppc_compare_exp_uo: {
    unsigned Pred;
    switch (IntrinsicID) {
    case Intrinsic::ppc_compare_exp_lt:
      Pred = PPC::PRED_LT;
      break;
    case Intrinsic::ppc_compare_exp_gt:
      Pred = PPC::PRED_GT;
      break;
    case Intrinsic::ppc_compare_exp_eq:
      Pred = PPC::PRED_EQ;
      break;
    case Intrinsic::ppc_compare_exp_uo:
      Pred = PPC::PRED_UN;
      break;
    }
    return SDValue(
        DAG.getMachineNode(
            PPC::SELECT_CC_I4, dl, MVT::i32,
            {SDValue(DAG.getMachineNode(PPC::XSCMPEXPDP, dl, MVT::i32,
                                        Op.getOperand(1), Op.getOperand(2)),
                     0),
             DAG.getConstant(1, dl, MVT::i32), DAG.getConstant(0, dl, MVT::i32),
             DAG.getTargetConstant(Pred, dl, MVT::i32)}),
        0);
  }
  case Intrinsic::ppc_test_data_class: {
    EVT OpVT = Op.getOperand(1).getValueType();
    unsigned CmprOpc = OpVT == MVT::f128 ? PPC::XSTSTDCQP
                                         : (OpVT == MVT::f64 ? PPC::XSTSTDCDP
                                                             : PPC::XSTSTDCSP);
    return SDValue(
        DAG.getMachineNode(
            PPC::SELECT_CC_I4, dl, MVT::i32,
            {SDValue(DAG.getMachineNode(CmprOpc, dl, MVT::i32, Op.getOperand(2),
                                        Op.getOperand(1)),
                     0),
             DAG.getConstant(1, dl, MVT::i32), DAG.getConstant(0, dl, MVT::i32),
             DAG.getTargetConstant(PPC::PRED_EQ, dl, MVT::i32)}),
        0);
  }
  case Intrinsic::ppc_fnmsub: {
    EVT VT = Op.getOperand(1).getValueType();
    if (!Subtarget.hasVSX() || (!Subtarget.hasFloat128() && VT == MVT::f128))
      return DAG.getNode(
          ISD::FNEG, dl, VT,
          DAG.getNode(ISD::FMA, dl, VT, Op.getOperand(1), Op.getOperand(2),
                      DAG.getNode(ISD::FNEG, dl, VT, Op.getOperand(3))));
    return DAG.getNode(PPCISD::FNMSUB, dl, VT, Op.getOperand(1),
                       Op.getOperand(2), Op.getOperand(3));
  }
  case Intrinsic::ppc_convert_f128_to_ppcf128:
  case Intrinsic::ppc_convert_ppcf128_to_f128: {
    RTLIB::Libcall LC = IntrinsicID == Intrinsic::ppc_convert_ppcf128_to_f128
                            ? RTLIB::CONVERT_PPCF128_F128
                            : RTLIB::CONVERT_F128_PPCF128;
    MakeLibCallOptions CallOptions;
    std::pair<SDValue, SDValue> Result =
        makeLibCall(DAG, LC, Op.getValueType(), Op.getOperand(1), CallOptions,
                    dl, SDValue());
    return Result.first;
  }
  case Intrinsic::ppc_maxfe:
  case Intrinsic::ppc_maxfl:
  case Intrinsic::ppc_maxfs:
  case Intrinsic::ppc_minfe:
  case Intrinsic::ppc_minfl:
  case Intrinsic::ppc_minfs: {
    EVT VT = Op.getValueType();
    assert(
        all_of(Op->ops().drop_front(4),
               [VT](const SDUse &Use) { return Use.getValueType() == VT; }) &&
        "ppc_[max|min]f[e|l|s] must have uniform type arguments");
    (void)VT;
    ISD::CondCode CC = ISD::SETGT;
    if (IntrinsicID == Intrinsic::ppc_minfe ||
        IntrinsicID == Intrinsic::ppc_minfl ||
        IntrinsicID == Intrinsic::ppc_minfs)
      CC = ISD::SETLT;
    unsigned I = Op.getNumOperands() - 2, Cnt = I;
    SDValue Res = Op.getOperand(I);
    for (--I; Cnt != 0; --Cnt, I = (--I == 0 ? (Op.getNumOperands() - 1) : I)) {
      Res =
          DAG.getSelectCC(dl, Res, Op.getOperand(I), Res, Op.getOperand(I), CC);
    }
    return Res;
  }
  }

  // If this is a lowered altivec predicate compare, CompareOpc is set to the
  // opcode number of the comparison.
  int CompareOpc;
  bool isDot;
  if (!getVectorCompareInfo(Op, CompareOpc, isDot, Subtarget))
    return SDValue();    // Don't custom lower most intrinsics.

  // If this is a non-dot comparison, make the VCMP node and we are done.
  if (!isDot) {
    SDValue Tmp = DAG.getNode(PPCISD::VCMP, dl, Op.getOperand(2).getValueType(),
                              Op.getOperand(1), Op.getOperand(2),
                              DAG.getConstant(CompareOpc, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Tmp);
  }

  // Create the PPCISD altivec 'dot' comparison node.
  SDValue Ops[] = {
    Op.getOperand(2),  // LHS
    Op.getOperand(3),  // RHS
    DAG.getConstant(CompareOpc, dl, MVT::i32)
  };
  EVT VTs[] = { Op.getOperand(2).getValueType(), MVT::Glue };
  SDValue CompNode = DAG.getNode(PPCISD::VCMP_rec, dl, VTs, Ops);

  // Now that we have the comparison, emit a copy from the CR to a GPR.
  // This is flagged to the above dot comparison.
  SDValue Flags = DAG.getNode(PPCISD::MFOCRF, dl, MVT::i32,
                                DAG.getRegister(PPC::CR6, MVT::i32),
                                CompNode.getValue(1));

  // Unpack the result based on how the target uses it.
  unsigned BitNo;   // Bit # of CR6.
  bool InvertBit;   // Invert result?
  switch (Op.getConstantOperandVal(1)) {
  default:  // Can't happen, don't crash on invalid number though.
  case 0:   // Return the value of the EQ bit of CR6.
    BitNo = 0; InvertBit = false;
    break;
  case 1:   // Return the inverted value of the EQ bit of CR6.
    BitNo = 0; InvertBit = true;
    break;
  case 2:   // Return the value of the LT bit of CR6.
    BitNo = 2; InvertBit = false;
    break;
  case 3:   // Return the inverted value of the LT bit of CR6.
    BitNo = 2; InvertBit = true;
    break;
  }

  // Shift the bit into the low position.
  Flags = DAG.getNode(ISD::SRL, dl, MVT::i32, Flags,
                      DAG.getConstant(8 - (3 - BitNo), dl, MVT::i32));
  // Isolate the bit.
  Flags = DAG.getNode(ISD::AND, dl, MVT::i32, Flags,
                      DAG.getConstant(1, dl, MVT::i32));

  // If we are supposed to, toggle the bit.
  if (InvertBit)
    Flags = DAG.getNode(ISD::XOR, dl, MVT::i32, Flags,
                        DAG.getConstant(1, dl, MVT::i32));
  return Flags;
}

SDValue PPCTargetLowering::LowerINTRINSIC_VOID(SDValue Op,
                                               SelectionDAG &DAG) const {
  // SelectionDAGBuilder::visitTargetIntrinsic may insert one extra chain to
  // the beginning of the argument list.
  int ArgStart = isa<ConstantSDNode>(Op.getOperand(0)) ? 0 : 1;
  SDLoc DL(Op);
  switch (Op.getConstantOperandVal(ArgStart)) {
  case Intrinsic::ppc_cfence: {
    assert(ArgStart == 1 && "llvm.ppc.cfence must carry a chain argument.");
    SDValue Val = Op.getOperand(ArgStart + 1);
    EVT Ty = Val.getValueType();
    if (Ty == MVT::i128) {
      // FIXME: Testing one of two paired registers is sufficient to guarantee
      // ordering?
      Val = DAG.getNode(ISD::TRUNCATE, DL, MVT::i64, Val);
    }
    unsigned Opcode = Subtarget.isPPC64() ? PPC::CFENCE8 : PPC::CFENCE;
    EVT FTy = Subtarget.isPPC64() ? MVT::i64 : MVT::i32;
    return SDValue(
        DAG.getMachineNode(Opcode, DL, MVT::Other,
                           DAG.getNode(ISD::ANY_EXTEND, DL, FTy, Val),
                           Op.getOperand(0)),
        0);
  }
  default:
    break;
  }
  return SDValue();
}

// Lower scalar BSWAP64 to xxbrd.
SDValue PPCTargetLowering::LowerBSWAP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  if (!Subtarget.isPPC64())
    return Op;
  // MTVSRDD
  Op = DAG.getNode(ISD::BUILD_VECTOR, dl, MVT::v2i64, Op.getOperand(0),
                   Op.getOperand(0));
  // XXBRD
  Op = DAG.getNode(ISD::BSWAP, dl, MVT::v2i64, Op);
  // MFVSRD
  int VectorIndex = 0;
  if (Subtarget.isLittleEndian())
    VectorIndex = 1;
  Op = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i64, Op,
                   DAG.getTargetConstant(VectorIndex, dl, MVT::i32));
  return Op;
}

// ATOMIC_CMP_SWAP for i8/i16 needs to zero-extend its input since it will be
// compared to a value that is atomically loaded (atomic loads zero-extend).
SDValue PPCTargetLowering::LowerATOMIC_CMP_SWAP(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::ATOMIC_CMP_SWAP &&
         "Expecting an atomic compare-and-swap here.");
  SDLoc dl(Op);
  auto *AtomicNode = cast<AtomicSDNode>(Op.getNode());
  EVT MemVT = AtomicNode->getMemoryVT();
  if (MemVT.getSizeInBits() >= 32)
    return Op;

  SDValue CmpOp = Op.getOperand(2);
  // If this is already correctly zero-extended, leave it alone.
  auto HighBits = APInt::getHighBitsSet(32, 32 - MemVT.getSizeInBits());
  if (DAG.MaskedValueIsZero(CmpOp, HighBits))
    return Op;

  // Clear the high bits of the compare operand.
  unsigned MaskVal = (1 << MemVT.getSizeInBits()) - 1;
  SDValue NewCmpOp =
    DAG.getNode(ISD::AND, dl, MVT::i32, CmpOp,
                DAG.getConstant(MaskVal, dl, MVT::i32));

  // Replace the existing compare operand with the properly zero-extended one.
  SmallVector<SDValue, 4> Ops;
  for (int i = 0, e = AtomicNode->getNumOperands(); i < e; i++)
    Ops.push_back(AtomicNode->getOperand(i));
  Ops[2] = NewCmpOp;
  MachineMemOperand *MMO = AtomicNode->getMemOperand();
  SDVTList Tys = DAG.getVTList(MVT::i32, MVT::Other);
  auto NodeTy =
    (MemVT == MVT::i8) ? PPCISD::ATOMIC_CMP_SWAP_8 : PPCISD::ATOMIC_CMP_SWAP_16;
  return DAG.getMemIntrinsicNode(NodeTy, dl, Tys, Ops, MemVT, MMO);
}

SDValue PPCTargetLowering::LowerATOMIC_LOAD_STORE(SDValue Op,
                                                  SelectionDAG &DAG) const {
  AtomicSDNode *N = cast<AtomicSDNode>(Op.getNode());
  EVT MemVT = N->getMemoryVT();
  assert(MemVT.getSimpleVT() == MVT::i128 &&
         "Expect quadword atomic operations");
  SDLoc dl(N);
  unsigned Opc = N->getOpcode();
  switch (Opc) {
  case ISD::ATOMIC_LOAD: {
    // Lower quadword atomic load to int_ppc_atomic_load_i128 which will be
    // lowered to ppc instructions by pattern matching instruction selector.
    SDVTList Tys = DAG.getVTList(MVT::i64, MVT::i64, MVT::Other);
    SmallVector<SDValue, 4> Ops{
        N->getOperand(0),
        DAG.getConstant(Intrinsic::ppc_atomic_load_i128, dl, MVT::i32)};
    for (int I = 1, E = N->getNumOperands(); I < E; ++I)
      Ops.push_back(N->getOperand(I));
    SDValue LoadedVal = DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, dl, Tys,
                                                Ops, MemVT, N->getMemOperand());
    SDValue ValLo = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i128, LoadedVal);
    SDValue ValHi =
        DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i128, LoadedVal.getValue(1));
    ValHi = DAG.getNode(ISD::SHL, dl, MVT::i128, ValHi,
                        DAG.getConstant(64, dl, MVT::i32));
    SDValue Val =
        DAG.getNode(ISD::OR, dl, {MVT::i128, MVT::Other}, {ValLo, ValHi});
    return DAG.getNode(ISD::MERGE_VALUES, dl, {MVT::i128, MVT::Other},
                       {Val, LoadedVal.getValue(2)});
  }
  case ISD::ATOMIC_STORE: {
    // Lower quadword atomic store to int_ppc_atomic_store_i128 which will be
    // lowered to ppc instructions by pattern matching instruction selector.
    SDVTList Tys = DAG.getVTList(MVT::Other);
    SmallVector<SDValue, 4> Ops{
        N->getOperand(0),
        DAG.getConstant(Intrinsic::ppc_atomic_store_i128, dl, MVT::i32)};
    SDValue Val = N->getOperand(1);
    SDValue ValLo = DAG.getNode(ISD::TRUNCATE, dl, MVT::i64, Val);
    SDValue ValHi = DAG.getNode(ISD::SRL, dl, MVT::i128, Val,
                                DAG.getConstant(64, dl, MVT::i32));
    ValHi = DAG.getNode(ISD::TRUNCATE, dl, MVT::i64, ValHi);
    Ops.push_back(ValLo);
    Ops.push_back(ValHi);
    Ops.push_back(N->getOperand(2));
    return DAG.getMemIntrinsicNode(ISD::INTRINSIC_VOID, dl, Tys, Ops, MemVT,
                                   N->getMemOperand());
  }
  default:
    llvm_unreachable("Unexpected atomic opcode");
  }
}

static SDValue getDataClassTest(SDValue Op, FPClassTest Mask, const SDLoc &Dl,
                                SelectionDAG &DAG,
                                const PPCSubtarget &Subtarget) {
  assert(Mask <= fcAllFlags && "Invalid fp_class flags!");

  enum DataClassMask {
    DC_NAN = 1 << 6,
    DC_NEG_INF = 1 << 4,
    DC_POS_INF = 1 << 5,
    DC_NEG_ZERO = 1 << 2,
    DC_POS_ZERO = 1 << 3,
    DC_NEG_SUBNORM = 1,
    DC_POS_SUBNORM = 1 << 1,
  };

  EVT VT = Op.getValueType();

  unsigned TestOp = VT == MVT::f128  ? PPC::XSTSTDCQP
                    : VT == MVT::f64 ? PPC::XSTSTDCDP
                                     : PPC::XSTSTDCSP;

  if (Mask == fcAllFlags)
    return DAG.getBoolConstant(true, Dl, MVT::i1, VT);
  if (Mask == 0)
    return DAG.getBoolConstant(false, Dl, MVT::i1, VT);

  // When it's cheaper or necessary to test reverse flags.
  if ((Mask & fcNormal) == fcNormal || Mask == ~fcQNan || Mask == ~fcSNan) {
    SDValue Rev = getDataClassTest(Op, ~Mask, Dl, DAG, Subtarget);
    return DAG.getNOT(Dl, Rev, MVT::i1);
  }

  // Power doesn't support testing whether a value is 'normal'. Test the rest
  // first, and test if it's 'not not-normal' with expected sign.
  if (Mask & fcNormal) {
    SDValue Rev(DAG.getMachineNode(
                    TestOp, Dl, MVT::i32,
                    DAG.getTargetConstant(DC_NAN | DC_NEG_INF | DC_POS_INF |
                                              DC_NEG_ZERO | DC_POS_ZERO |
                                              DC_NEG_SUBNORM | DC_POS_SUBNORM,
                                          Dl, MVT::i32),
                    Op),
                0);
    // Sign are stored in CR bit 0, result are in CR bit 2.
    SDValue Sign(
        DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, Dl, MVT::i1, Rev,
                           DAG.getTargetConstant(PPC::sub_lt, Dl, MVT::i32)),
        0);
    SDValue Normal(DAG.getNOT(
        Dl,
        SDValue(DAG.getMachineNode(
                    TargetOpcode::EXTRACT_SUBREG, Dl, MVT::i1, Rev,
                    DAG.getTargetConstant(PPC::sub_eq, Dl, MVT::i32)),
                0),
        MVT::i1));
    if (Mask & fcPosNormal)
      Sign = DAG.getNOT(Dl, Sign, MVT::i1);
    SDValue Result = DAG.getNode(ISD::AND, Dl, MVT::i1, Sign, Normal);
    if (Mask == fcPosNormal || Mask == fcNegNormal)
      return Result;

    return DAG.getNode(
        ISD::OR, Dl, MVT::i1,
        getDataClassTest(Op, Mask & ~fcNormal, Dl, DAG, Subtarget), Result);
  }

  // The instruction doesn't differentiate between signaling or quiet NaN. Test
  // the rest first, and test if it 'is NaN and is signaling/quiet'.
  if ((Mask & fcNan) == fcQNan || (Mask & fcNan) == fcSNan) {
    bool IsQuiet = Mask & fcQNan;
    SDValue NanCheck = getDataClassTest(Op, fcNan, Dl, DAG, Subtarget);

    // Quietness is determined by the first bit in fraction field.
    uint64_t QuietMask = 0;
    SDValue HighWord;
    if (VT == MVT::f128) {
      HighWord = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, Dl, MVT::i32, DAG.getBitcast(MVT::v4i32, Op),
          DAG.getVectorIdxConstant(Subtarget.isLittleEndian() ? 3 : 0, Dl));
      QuietMask = 0x8000;
    } else if (VT == MVT::f64) {
      if (Subtarget.isPPC64()) {
        HighWord = DAG.getNode(ISD::EXTRACT_ELEMENT, Dl, MVT::i32,
                               DAG.getBitcast(MVT::i64, Op),
                               DAG.getConstant(1, Dl, MVT::i32));
      } else {
        SDValue Vec = DAG.getBitcast(
            MVT::v4i32, DAG.getNode(ISD::SCALAR_TO_VECTOR, Dl, MVT::v2f64, Op));
        HighWord = DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, Dl, MVT::i32, Vec,
            DAG.getVectorIdxConstant(Subtarget.isLittleEndian() ? 1 : 0, Dl));
      }
      QuietMask = 0x80000;
    } else if (VT == MVT::f32) {
      HighWord = DAG.getBitcast(MVT::i32, Op);
      QuietMask = 0x400000;
    }
    SDValue NanRes = DAG.getSetCC(
        Dl, MVT::i1,
        DAG.getNode(ISD::AND, Dl, MVT::i32, HighWord,
                    DAG.getConstant(QuietMask, Dl, MVT::i32)),
        DAG.getConstant(0, Dl, MVT::i32), IsQuiet ? ISD::SETNE : ISD::SETEQ);
    NanRes = DAG.getNode(ISD::AND, Dl, MVT::i1, NanCheck, NanRes);
    if (Mask == fcQNan || Mask == fcSNan)
      return NanRes;

    return DAG.getNode(ISD::OR, Dl, MVT::i1,
                       getDataClassTest(Op, Mask & ~fcNan, Dl, DAG, Subtarget),
                       NanRes);
  }

  unsigned NativeMask = 0;
  if ((Mask & fcNan) == fcNan)
    NativeMask |= DC_NAN;
  if (Mask & fcNegInf)
    NativeMask |= DC_NEG_INF;
  if (Mask & fcPosInf)
    NativeMask |= DC_POS_INF;
  if (Mask & fcNegZero)
    NativeMask |= DC_NEG_ZERO;
  if (Mask & fcPosZero)
    NativeMask |= DC_POS_ZERO;
  if (Mask & fcNegSubnormal)
    NativeMask |= DC_NEG_SUBNORM;
  if (Mask & fcPosSubnormal)
    NativeMask |= DC_POS_SUBNORM;
  return SDValue(
      DAG.getMachineNode(
          TargetOpcode::EXTRACT_SUBREG, Dl, MVT::i1,
          SDValue(DAG.getMachineNode(
                      TestOp, Dl, MVT::i32,
                      DAG.getTargetConstant(NativeMask, Dl, MVT::i32), Op),
                  0),
          DAG.getTargetConstant(PPC::sub_eq, Dl, MVT::i32)),
      0);
}

SDValue PPCTargetLowering::LowerIS_FPCLASS(SDValue Op,
                                           SelectionDAG &DAG) const {
  assert(Subtarget.hasP9Vector() && "Test data class requires Power9");
  SDValue LHS = Op.getOperand(0);
  uint64_t RHSC = Op.getConstantOperandVal(1);
  SDLoc Dl(Op);
  FPClassTest Category = static_cast<FPClassTest>(RHSC);
  return getDataClassTest(LHS, Category, Dl, DAG, Subtarget);
}

SDValue PPCTargetLowering::LowerSCALAR_TO_VECTOR(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc dl(Op);
  // Create a stack slot that is 16-byte aligned.
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  int FrameIdx = MFI.CreateStackObject(16, Align(16), false);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

  // Store the input value into Value#0 of the stack slot.
  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Op.getOperand(0), FIdx,
                               MachinePointerInfo());
  // Load it out.
  return DAG.getLoad(Op.getValueType(), dl, Store, FIdx, MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerINSERT_VECTOR_ELT(SDValue Op,
                                                  SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::INSERT_VECTOR_ELT &&
         "Should only be called for ISD::INSERT_VECTOR_ELT");

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(2));

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);

  if (VT == MVT::v2f64 && C)
    return Op;

  if (Subtarget.hasP9Vector()) {
    // A f32 load feeding into a v4f32 insert_vector_elt is handled in this way
    // because on P10, it allows this specific insert_vector_elt load pattern to
    // utilize the refactored load and store infrastructure in order to exploit
    // prefixed loads.
    // On targets with inexpensive direct moves (Power9 and up), a
    // (insert_vector_elt v4f32:$vec, (f32 load)) is always better as an integer
    // load since a single precision load will involve conversion to double
    // precision on the load followed by another conversion to single precision.
    if ((VT == MVT::v4f32) && (V2.getValueType() == MVT::f32) &&
        (isa<LoadSDNode>(V2))) {
      SDValue BitcastVector = DAG.getBitcast(MVT::v4i32, V1);
      SDValue BitcastLoad = DAG.getBitcast(MVT::i32, V2);
      SDValue InsVecElt =
          DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v4i32, BitcastVector,
                      BitcastLoad, Op.getOperand(2));
      return DAG.getBitcast(MVT::v4f32, InsVecElt);
    }
  }

  if (Subtarget.isISA3_1()) {
    if ((VT == MVT::v2i64 || VT == MVT::v2f64) && !Subtarget.isPPC64())
      return SDValue();
    // On P10, we have legal lowering for constant and variable indices for
    // all vectors.
    if (VT == MVT::v16i8 || VT == MVT::v8i16 || VT == MVT::v4i32 ||
        VT == MVT::v2i64 || VT == MVT::v4f32 || VT == MVT::v2f64)
      return Op;
  }

  // Before P10, we have legal lowering for constant indices but not for
  // variable ones.
  if (!C)
    return SDValue();

  // We can use MTVSRZ + VECINSERT for v8i16 and v16i8 types.
  if (VT == MVT::v8i16 || VT == MVT::v16i8) {
    SDValue Mtvsrz = DAG.getNode(PPCISD::MTVSRZ, dl, VT, V2);
    unsigned BytesInEachElement = VT.getVectorElementType().getSizeInBits() / 8;
    unsigned InsertAtElement = C->getZExtValue();
    unsigned InsertAtByte = InsertAtElement * BytesInEachElement;
    if (Subtarget.isLittleEndian()) {
      InsertAtByte = (16 - BytesInEachElement) - InsertAtByte;
    }
    return DAG.getNode(PPCISD::VECINSERT, dl, VT, V1, Mtvsrz,
                       DAG.getConstant(InsertAtByte, dl, MVT::i32));
  }
  return Op;
}

SDValue PPCTargetLowering::LowerVectorLoad(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc dl(Op);
  LoadSDNode *LN = cast<LoadSDNode>(Op.getNode());
  SDValue LoadChain = LN->getChain();
  SDValue BasePtr = LN->getBasePtr();
  EVT VT = Op.getValueType();

  if (VT != MVT::v256i1 && VT != MVT::v512i1)
    return Op;

  // Type v256i1 is used for pairs and v512i1 is used for accumulators.
  // Here we create 2 or 4 v16i8 loads to load the pair or accumulator value in
  // 2 or 4 vsx registers.
  assert((VT != MVT::v512i1 || Subtarget.hasMMA()) &&
         "Type unsupported without MMA");
  assert((VT != MVT::v256i1 || Subtarget.pairedVectorMemops()) &&
         "Type unsupported without paired vector support");
  Align Alignment = LN->getAlign();
  SmallVector<SDValue, 4> Loads;
  SmallVector<SDValue, 4> LoadChains;
  unsigned NumVecs = VT.getSizeInBits() / 128;
  for (unsigned Idx = 0; Idx < NumVecs; ++Idx) {
    SDValue Load =
        DAG.getLoad(MVT::v16i8, dl, LoadChain, BasePtr,
                    LN->getPointerInfo().getWithOffset(Idx * 16),
                    commonAlignment(Alignment, Idx * 16),
                    LN->getMemOperand()->getFlags(), LN->getAAInfo());
    BasePtr = DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr,
                          DAG.getConstant(16, dl, BasePtr.getValueType()));
    Loads.push_back(Load);
    LoadChains.push_back(Load.getValue(1));
  }
  if (Subtarget.isLittleEndian()) {
    std::reverse(Loads.begin(), Loads.end());
    std::reverse(LoadChains.begin(), LoadChains.end());
  }
  SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, LoadChains);
  SDValue Value =
      DAG.getNode(VT == MVT::v512i1 ? PPCISD::ACC_BUILD : PPCISD::PAIR_BUILD,
                  dl, VT, Loads);
  SDValue RetOps[] = {Value, TF};
  return DAG.getMergeValues(RetOps, dl);
}

SDValue PPCTargetLowering::LowerVectorStore(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc dl(Op);
  StoreSDNode *SN = cast<StoreSDNode>(Op.getNode());
  SDValue StoreChain = SN->getChain();
  SDValue BasePtr = SN->getBasePtr();
  SDValue Value = SN->getValue();
  SDValue Value2 = SN->getValue();
  EVT StoreVT = Value.getValueType();

  if (StoreVT != MVT::v256i1 && StoreVT != MVT::v512i1)
    return Op;

  // Type v256i1 is used for pairs and v512i1 is used for accumulators.
  // Here we create 2 or 4 v16i8 stores to store the pair or accumulator
  // underlying registers individually.
  assert((StoreVT != MVT::v512i1 || Subtarget.hasMMA()) &&
         "Type unsupported without MMA");
  assert((StoreVT != MVT::v256i1 || Subtarget.pairedVectorMemops()) &&
         "Type unsupported without paired vector support");
  Align Alignment = SN->getAlign();
  SmallVector<SDValue, 4> Stores;
  unsigned NumVecs = 2;
  if (StoreVT == MVT::v512i1) {
    if (Subtarget.isISAFuture()) {
      EVT ReturnTypes[] = {MVT::v256i1, MVT::v256i1};
      MachineSDNode *ExtNode = DAG.getMachineNode(
          PPC::DMXXEXTFDMR512, dl, ReturnTypes, Op.getOperand(1));

      Value = SDValue(ExtNode, 0);
      Value2 = SDValue(ExtNode, 1);
    } else
      Value = DAG.getNode(PPCISD::XXMFACC, dl, MVT::v512i1, Value);
    NumVecs = 4;
  }
  for (unsigned Idx = 0; Idx < NumVecs; ++Idx) {
    unsigned VecNum = Subtarget.isLittleEndian() ? NumVecs - 1 - Idx : Idx;
    SDValue Elt;
    if (Subtarget.isISAFuture()) {
      VecNum = Subtarget.isLittleEndian() ? 1 - (Idx % 2) : (Idx % 2);
      Elt = DAG.getNode(PPCISD::EXTRACT_VSX_REG, dl, MVT::v16i8,
                        Idx > 1 ? Value2 : Value,
                        DAG.getConstant(VecNum, dl, getPointerTy(DAG.getDataLayout())));
    } else
      Elt = DAG.getNode(PPCISD::EXTRACT_VSX_REG, dl, MVT::v16i8, Value,
                        DAG.getConstant(VecNum, dl, getPointerTy(DAG.getDataLayout())));

    SDValue Store =
        DAG.getStore(StoreChain, dl, Elt, BasePtr,
                     SN->getPointerInfo().getWithOffset(Idx * 16),
                     commonAlignment(Alignment, Idx * 16),
                     SN->getMemOperand()->getFlags(), SN->getAAInfo());
    BasePtr = DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr,
                          DAG.getConstant(16, dl, BasePtr.getValueType()));
    Stores.push_back(Store);
  }
  SDValue TF = DAG.getTokenFactor(dl, Stores);
  return TF;
}

SDValue PPCTargetLowering::LowerMUL(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  if (Op.getValueType() == MVT::v4i32) {
    SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);

    SDValue Zero = getCanonicalConstSplat(0, 1, MVT::v4i32, DAG, dl);
    // +16 as shift amt.
    SDValue Neg16 = getCanonicalConstSplat(-16, 4, MVT::v4i32, DAG, dl);
    SDValue RHSSwap =   // = vrlw RHS, 16
      BuildIntrinsicOp(Intrinsic::ppc_altivec_vrlw, RHS, Neg16, DAG, dl);

    // Shrinkify inputs to v8i16.
    LHS = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, LHS);
    RHS = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, RHS);
    RHSSwap = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, RHSSwap);

    // Low parts multiplied together, generating 32-bit results (we ignore the
    // top parts).
    SDValue LoProd = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmulouh,
                                        LHS, RHS, DAG, dl, MVT::v4i32);

    SDValue HiProd = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmsumuhm,
                                      LHS, RHSSwap, Zero, DAG, dl, MVT::v4i32);
    // Shift the high parts up 16 bits.
    HiProd = BuildIntrinsicOp(Intrinsic::ppc_altivec_vslw, HiProd,
                              Neg16, DAG, dl);
    return DAG.getNode(ISD::ADD, dl, MVT::v4i32, LoProd, HiProd);
  } else if (Op.getValueType() == MVT::v16i8) {
    SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
    bool isLittleEndian = Subtarget.isLittleEndian();

    // Multiply the even 8-bit parts, producing 16-bit sums.
    SDValue EvenParts = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmuleub,
                                           LHS, RHS, DAG, dl, MVT::v8i16);
    EvenParts = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, EvenParts);

    // Multiply the odd 8-bit parts, producing 16-bit sums.
    SDValue OddParts = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmuloub,
                                          LHS, RHS, DAG, dl, MVT::v8i16);
    OddParts = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, OddParts);

    // Merge the results together.  Because vmuleub and vmuloub are
    // instructions with a big-endian bias, we must reverse the
    // element numbering and reverse the meaning of "odd" and "even"
    // when generating little endian code.
    int Ops[16];
    for (unsigned i = 0; i != 8; ++i) {
      if (isLittleEndian) {
        Ops[i*2  ] = 2*i;
        Ops[i*2+1] = 2*i+16;
      } else {
        Ops[i*2  ] = 2*i+1;
        Ops[i*2+1] = 2*i+1+16;
      }
    }
    if (isLittleEndian)
      return DAG.getVectorShuffle(MVT::v16i8, dl, OddParts, EvenParts, Ops);
    else
      return DAG.getVectorShuffle(MVT::v16i8, dl, EvenParts, OddParts, Ops);
  } else {
    llvm_unreachable("Unknown mul to lower!");
  }
}

SDValue PPCTargetLowering::LowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const {
  bool IsStrict = Op->isStrictFPOpcode();
  if (Op.getOperand(IsStrict ? 1 : 0).getValueType() == MVT::f128 &&
      !Subtarget.hasP9Vector())
    return SDValue();

  return Op;
}

// Custom lowering for fpext vf32 to v2f64
SDValue PPCTargetLowering::LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const {

  assert(Op.getOpcode() == ISD::FP_EXTEND &&
         "Should only be called for ISD::FP_EXTEND");

  // FIXME: handle extends from half precision float vectors on P9.
  // We only want to custom lower an extend from v2f32 to v2f64.
  if (Op.getValueType() != MVT::v2f64 ||
      Op.getOperand(0).getValueType() != MVT::v2f32)
    return SDValue();

  SDLoc dl(Op);
  SDValue Op0 = Op.getOperand(0);

  switch (Op0.getOpcode()) {
  default:
    return SDValue();
  case ISD::EXTRACT_SUBVECTOR: {
    assert(Op0.getNumOperands() == 2 &&
           isa<ConstantSDNode>(Op0->getOperand(1)) &&
           "Node should have 2 operands with second one being a constant!");

    if (Op0.getOperand(0).getValueType() != MVT::v4f32)
      return SDValue();

    // Custom lower is only done for high or low doubleword.
    int Idx = Op0.getConstantOperandVal(1);
    if (Idx % 2 != 0)
      return SDValue();

    // Since input is v4f32, at this point Idx is either 0 or 2.
    // Shift to get the doubleword position we want.
    int DWord = Idx >> 1;

    // High and low word positions are different on little endian.
    if (Subtarget.isLittleEndian())
      DWord ^= 0x1;

    return DAG.getNode(PPCISD::FP_EXTEND_HALF, dl, MVT::v2f64,
                       Op0.getOperand(0), DAG.getConstant(DWord, dl, MVT::i32));
  }
  case ISD::FADD:
  case ISD::FMUL:
  case ISD::FSUB: {
    SDValue NewLoad[2];
    for (unsigned i = 0, ie = Op0.getNumOperands(); i != ie; ++i) {
      // Ensure both input are loads.
      SDValue LdOp = Op0.getOperand(i);
      if (LdOp.getOpcode() != ISD::LOAD)
        return SDValue();
      // Generate new load node.
      LoadSDNode *LD = cast<LoadSDNode>(LdOp);
      SDValue LoadOps[] = {LD->getChain(), LD->getBasePtr()};
      NewLoad[i] = DAG.getMemIntrinsicNode(
          PPCISD::LD_VSX_LH, dl, DAG.getVTList(MVT::v4f32, MVT::Other), LoadOps,
          LD->getMemoryVT(), LD->getMemOperand());
    }
    SDValue NewOp =
        DAG.getNode(Op0.getOpcode(), SDLoc(Op0), MVT::v4f32, NewLoad[0],
                    NewLoad[1], Op0.getNode()->getFlags());
    return DAG.getNode(PPCISD::FP_EXTEND_HALF, dl, MVT::v2f64, NewOp,
                       DAG.getConstant(0, dl, MVT::i32));
  }
  case ISD::LOAD: {
    LoadSDNode *LD = cast<LoadSDNode>(Op0);
    SDValue LoadOps[] = {LD->getChain(), LD->getBasePtr()};
    SDValue NewLd = DAG.getMemIntrinsicNode(
        PPCISD::LD_VSX_LH, dl, DAG.getVTList(MVT::v4f32, MVT::Other), LoadOps,
        LD->getMemoryVT(), LD->getMemOperand());
    return DAG.getNode(PPCISD::FP_EXTEND_HALF, dl, MVT::v2f64, NewLd,
                       DAG.getConstant(0, dl, MVT::i32));
  }
  }
  llvm_unreachable("ERROR:Should return for all cases within swtich.");
}

/// LowerOperation - Provide custom lowering hooks for some operations.
///
SDValue PPCTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Wasn't expecting to be able to lower this!");
  case ISD::FPOW:               return lowerPow(Op, DAG);
  case ISD::FSIN:               return lowerSin(Op, DAG);
  case ISD::FCOS:               return lowerCos(Op, DAG);
  case ISD::FLOG:               return lowerLog(Op, DAG);
  case ISD::FLOG10:             return lowerLog10(Op, DAG);
  case ISD::FEXP:               return lowerExp(Op, DAG);
  case ISD::ConstantPool:       return LowerConstantPool(Op, DAG);
  case ISD::BlockAddress:       return LowerBlockAddress(Op, DAG);
  case ISD::GlobalAddress:      return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:   return LowerGlobalTLSAddress(Op, DAG);
  case ISD::JumpTable:          return LowerJumpTable(Op, DAG);
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
  case ISD::SETCC:              return LowerSETCC(Op, DAG);
  case ISD::INIT_TRAMPOLINE:    return LowerINIT_TRAMPOLINE(Op, DAG);
  case ISD::ADJUST_TRAMPOLINE:  return LowerADJUST_TRAMPOLINE(Op, DAG);

  case ISD::INLINEASM:
  case ISD::INLINEASM_BR:       return LowerINLINEASM(Op, DAG);
  // Variable argument lowering.
  case ISD::VASTART:            return LowerVASTART(Op, DAG);
  case ISD::VAARG:              return LowerVAARG(Op, DAG);
  case ISD::VACOPY:             return LowerVACOPY(Op, DAG);

  case ISD::STACKRESTORE:       return LowerSTACKRESTORE(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC: return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::GET_DYNAMIC_AREA_OFFSET:
    return LowerGET_DYNAMIC_AREA_OFFSET(Op, DAG);

  // Exception handling lowering.
  case ISD::EH_DWARF_CFA:       return LowerEH_DWARF_CFA(Op, DAG);
  case ISD::EH_SJLJ_SETJMP:     return lowerEH_SJLJ_SETJMP(Op, DAG);
  case ISD::EH_SJLJ_LONGJMP:    return lowerEH_SJLJ_LONGJMP(Op, DAG);

  case ISD::LOAD:               return LowerLOAD(Op, DAG);
  case ISD::STORE:              return LowerSTORE(Op, DAG);
  case ISD::TRUNCATE:           return LowerTRUNCATE(Op, DAG);
  case ISD::SELECT_CC:          return LowerSELECT_CC(Op, DAG);
  case ISD::STRICT_FP_TO_UINT:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::FP_TO_SINT:         return LowerFP_TO_INT(Op, DAG, SDLoc(Op));
  case ISD::STRICT_UINT_TO_FP:
  case ISD::STRICT_SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::SINT_TO_FP:         return LowerINT_TO_FP(Op, DAG);
  case ISD::GET_ROUNDING:       return LowerGET_ROUNDING(Op, DAG);

  // Lower 64-bit shifts.
  case ISD::SHL_PARTS:          return LowerSHL_PARTS(Op, DAG);
  case ISD::SRL_PARTS:          return LowerSRL_PARTS(Op, DAG);
  case ISD::SRA_PARTS:          return LowerSRA_PARTS(Op, DAG);

  case ISD::FSHL:               return LowerFunnelShift(Op, DAG);
  case ISD::FSHR:               return LowerFunnelShift(Op, DAG);

  // Vector-related lowering.
  case ISD::BUILD_VECTOR:       return LowerBUILD_VECTOR(Op, DAG);
  case ISD::VECTOR_SHUFFLE:     return LowerVECTOR_SHUFFLE(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN: return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::SCALAR_TO_VECTOR:   return LowerSCALAR_TO_VECTOR(Op, DAG);
  case ISD::INSERT_VECTOR_ELT:  return LowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::MUL:                return LowerMUL(Op, DAG);
  case ISD::FP_EXTEND:          return LowerFP_EXTEND(Op, DAG);
  case ISD::STRICT_FP_ROUND:
  case ISD::FP_ROUND:
    return LowerFP_ROUND(Op, DAG);
  case ISD::ROTL:               return LowerROTL(Op, DAG);

  // For counter-based loop handling.
  case ISD::INTRINSIC_W_CHAIN:  return SDValue();

  case ISD::BITCAST:            return LowerBITCAST(Op, DAG);

  // Frame & Return address.
  case ISD::RETURNADDR:         return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:          return LowerFRAMEADDR(Op, DAG);

  case ISD::INTRINSIC_VOID:
    return LowerINTRINSIC_VOID(Op, DAG);
  case ISD::BSWAP:
    return LowerBSWAP(Op, DAG);
  case ISD::ATOMIC_CMP_SWAP:
    return LowerATOMIC_CMP_SWAP(Op, DAG);
  case ISD::ATOMIC_STORE:
    return LowerATOMIC_LOAD_STORE(Op, DAG);
  case ISD::IS_FPCLASS:
    return LowerIS_FPCLASS(Op, DAG);
  }
}

void PPCTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue>&Results,
                                           SelectionDAG &DAG) const {
  SDLoc dl(N);
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Do not know how to custom type legalize this operation!");
  case ISD::ATOMIC_LOAD: {
    SDValue Res = LowerATOMIC_LOAD_STORE(SDValue(N, 0), DAG);
    Results.push_back(Res);
    Results.push_back(Res.getValue(1));
    break;
  }
  case ISD::READCYCLECOUNTER: {
    SDVTList VTs = DAG.getVTList(MVT::i32, MVT::i32, MVT::Other);
    SDValue RTB = DAG.getNode(PPCISD::READ_TIME_BASE, dl, VTs, N->getOperand(0));

    Results.push_back(
        DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, RTB, RTB.getValue(1)));
    Results.push_back(RTB.getValue(2));
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    if (N->getConstantOperandVal(1) != Intrinsic::loop_decrement)
      break;

    assert(N->getValueType(0) == MVT::i1 &&
           "Unexpected result type for CTR decrement intrinsic");
    EVT SVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                 N->getValueType(0));
    SDVTList VTs = DAG.getVTList(SVT, MVT::Other);
    SDValue NewInt = DAG.getNode(N->getOpcode(), dl, VTs, N->getOperand(0),
                                 N->getOperand(1));

    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, NewInt));
    Results.push_back(NewInt.getValue(1));
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    switch (N->getConstantOperandVal(0)) {
    case Intrinsic::ppc_pack_longdouble:
      Results.push_back(DAG.getNode(ISD::BUILD_PAIR, dl, MVT::ppcf128,
                                    N->getOperand(2), N->getOperand(1)));
      break;
    case Intrinsic::ppc_maxfe:
    case Intrinsic::ppc_minfe:
    case Intrinsic::ppc_fnmsub:
    case Intrinsic::ppc_convert_f128_to_ppcf128:
      Results.push_back(LowerINTRINSIC_WO_CHAIN(SDValue(N, 0), DAG));
      break;
    }
    break;
  }
  case ISD::VAARG: {
    if (!Subtarget.isSVR4ABI() || Subtarget.isPPC64())
      return;

    EVT VT = N->getValueType(0);

    if (VT == MVT::i64) {
      SDValue NewNode = LowerVAARG(SDValue(N, 1), DAG);

      Results.push_back(NewNode);
      Results.push_back(NewNode.getValue(1));
    }
    return;
  }
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT: {
    // LowerFP_TO_INT() can only handle f32 and f64.
    if (N->getOperand(N->isStrictFPOpcode() ? 1 : 0).getValueType() ==
        MVT::ppcf128)
      return;
    SDValue LoweredValue = LowerFP_TO_INT(SDValue(N, 0), DAG, dl);
    Results.push_back(LoweredValue);
    if (N->isStrictFPOpcode())
      Results.push_back(LoweredValue.getValue(1));
    return;
  }
  case ISD::TRUNCATE: {
    if (!N->getValueType(0).isVector())
      return;
    SDValue Lowered = LowerTRUNCATEVector(SDValue(N, 0), DAG);
    if (Lowered)
      Results.push_back(Lowered);
    return;
  }
  case ISD::FSHL:
  case ISD::FSHR:
    // Don't handle funnel shifts here.
    return;
  case ISD::BITCAST:
    // Don't handle bitcast here.
    return;
  case ISD::FP_EXTEND:
    SDValue Lowered = LowerFP_EXTEND(SDValue(N, 0), DAG);
    if (Lowered)
      Results.push_back(Lowered);
    return;
  }
}

//===----------------------------------------------------------------------===//
//  Other Lowering Code
//===----------------------------------------------------------------------===//

static Instruction *callIntrinsic(IRBuilderBase &Builder, Intrinsic::ID Id) {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Function *Func = Intrinsic::getDeclaration(M, Id);
  return Builder.CreateCall(Func, {});
}

// The mappings for emitLeading/TrailingFence is taken from
// http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
Instruction *PPCTargetLowering::emitLeadingFence(IRBuilderBase &Builder,
                                                 Instruction *Inst,
                                                 AtomicOrdering Ord) const {
  if (Ord == AtomicOrdering::SequentiallyConsistent)
    return callIntrinsic(Builder, Intrinsic::ppc_sync);
  if (isReleaseOrStronger(Ord))
    return callIntrinsic(Builder, Intrinsic::ppc_lwsync);
  return nullptr;
}

Instruction *PPCTargetLowering::emitTrailingFence(IRBuilderBase &Builder,
                                                  Instruction *Inst,
                                                  AtomicOrdering Ord) const {
  if (Inst->hasAtomicLoad() && isAcquireOrStronger(Ord)) {
    // See http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html and
    // http://www.rdrop.com/users/paulmck/scalability/paper/N2745r.2011.03.04a.html
    // and http://www.cl.cam.ac.uk/~pes20/cppppc/ for justification.
    if (isa<LoadInst>(Inst))
      return Builder.CreateCall(
          Intrinsic::getDeclaration(
              Builder.GetInsertBlock()->getParent()->getParent(),
              Intrinsic::ppc_cfence, {Inst->getType()}),
          {Inst});
    // FIXME: Can use isync for rmw operation.
    return callIntrinsic(Builder, Intrinsic::ppc_lwsync);
  }
  return nullptr;
}

MachineBasicBlock *
PPCTargetLowering::EmitAtomicBinary(MachineInstr &MI, MachineBasicBlock *BB,
                                    unsigned AtomicSize,
                                    unsigned BinOpcode,
                                    unsigned CmpOpcode,
                                    unsigned CmpPred) const {
  // This also handles ATOMIC_SWAP, indicated by BinOpcode==0.
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();

  auto LoadMnemonic = PPC::LDARX;
  auto StoreMnemonic = PPC::STDCX;
  switch (AtomicSize) {
  default:
    llvm_unreachable("Unexpected size of atomic entity");
  case 1:
    LoadMnemonic = PPC::LBARX;
    StoreMnemonic = PPC::STBCX;
    assert(Subtarget.hasPartwordAtomics() && "Call this only with size >=4");
    break;
  case 2:
    LoadMnemonic = PPC::LHARX;
    StoreMnemonic = PPC::STHCX;
    assert(Subtarget.hasPartwordAtomics() && "Call this only with size >=4");
    break;
  case 4:
    LoadMnemonic = PPC::LWARX;
    StoreMnemonic = PPC::STWCX;
    break;
  case 8:
    LoadMnemonic = PPC::LDARX;
    StoreMnemonic = PPC::STDCX;
    break;
  }

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction *F = BB->getParent();
  MachineFunction::iterator It = ++BB->getIterator();

  Register dest = MI.getOperand(0).getReg();
  Register ptrA = MI.getOperand(1).getReg();
  Register ptrB = MI.getOperand(2).getReg();
  Register incr = MI.getOperand(3).getReg();
  DebugLoc dl = MI.getDebugLoc();

  MachineBasicBlock *loopMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *loop2MBB =
    CmpOpcode ? F->CreateMachineBasicBlock(LLVM_BB) : nullptr;
  MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, loopMBB);
  if (CmpOpcode)
    F->insert(It, loop2MBB);
  F->insert(It, exitMBB);
  exitMBB->splice(exitMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  exitMBB->transferSuccessorsAndUpdatePHIs(BB);

  MachineRegisterInfo &RegInfo = F->getRegInfo();
  Register TmpReg = (!BinOpcode) ? incr :
    RegInfo.createVirtualRegister( AtomicSize == 8 ? &PPC::G8RCRegClass
                                           : &PPC::GPRCRegClass);

  //  thisMBB:
  //   ...
  //   fallthrough --> loopMBB
  BB->addSuccessor(loopMBB);

  //  loopMBB:
  //   l[wd]arx dest, ptr
  //   add r0, dest, incr
  //   st[wd]cx. r0, ptr
  //   bne- loopMBB
  //   fallthrough --> exitMBB

  // For max/min...
  //  loopMBB:
  //   l[wd]arx dest, ptr
  //   cmpl?[wd] dest, incr
  //   bgt exitMBB
  //  loop2MBB:
  //   st[wd]cx. dest, ptr
  //   bne- loopMBB
  //   fallthrough --> exitMBB

  BB = loopMBB;
  BuildMI(BB, dl, TII->get(LoadMnemonic), dest)
    .addReg(ptrA).addReg(ptrB);
  if (BinOpcode)
    BuildMI(BB, dl, TII->get(BinOpcode), TmpReg).addReg(incr).addReg(dest);
  if (CmpOpcode) {
    Register CrReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);
    // Signed comparisons of byte or halfword values must be sign-extended.
    if (CmpOpcode == PPC::CMPW && AtomicSize < 4) {
      Register ExtReg = RegInfo.createVirtualRegister(&PPC::GPRCRegClass);
      BuildMI(BB, dl, TII->get(AtomicSize == 1 ? PPC::EXTSB : PPC::EXTSH),
              ExtReg).addReg(dest);
      BuildMI(BB, dl, TII->get(CmpOpcode), CrReg).addReg(ExtReg).addReg(incr);
    } else
      BuildMI(BB, dl, TII->get(CmpOpcode), CrReg).addReg(dest).addReg(incr);

    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(CmpPred)
        .addReg(CrReg)
        .addMBB(exitMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(exitMBB);
    BB = loop2MBB;
  }
  BuildMI(BB, dl, TII->get(StoreMnemonic))
    .addReg(TmpReg).addReg(ptrA).addReg(ptrB);
  BuildMI(BB, dl, TII->get(PPC::BCC))
    .addImm(PPC::PRED_NE).addReg(PPC::CR0).addMBB(loopMBB);
  BB->addSuccessor(loopMBB);
  BB->addSuccessor(exitMBB);

  //  exitMBB:
  //   ...
  BB = exitMBB;
  return BB;
}

static bool isSignExtended(MachineInstr &MI, const PPCInstrInfo *TII) {
  switch(MI.getOpcode()) {
  default:
    return false;
  case PPC::COPY:
    return TII->isSignExtended(MI.getOperand(1).getReg(),
                               &MI.getMF()->getRegInfo());
  case PPC::LHA:
  case PPC::LHA8:
  case PPC::LHAU:
  case PPC::LHAU8:
  case PPC::LHAUX:
  case PPC::LHAUX8:
  case PPC::LHAX:
  case PPC::LHAX8:
  case PPC::LWA:
  case PPC::LWAUX:
  case PPC::LWAX:
  case PPC::LWAX_32:
  case PPC::LWA_32:
  case PPC::PLHA:
  case PPC::PLHA8:
  case PPC::PLHA8pc:
  case PPC::PLHApc:
  case PPC::PLWA:
  case PPC::PLWA8:
  case PPC::PLWA8pc:
  case PPC::PLWApc:
  case PPC::EXTSB:
  case PPC::EXTSB8:
  case PPC::EXTSB8_32_64:
  case PPC::EXTSB8_rec:
  case PPC::EXTSB_rec:
  case PPC::EXTSH:
  case PPC::EXTSH8:
  case PPC::EXTSH8_32_64:
  case PPC::EXTSH8_rec:
  case PPC::EXTSH_rec:
  case PPC::EXTSW:
  case PPC::EXTSWSLI:
  case PPC::EXTSWSLI_32_64:
  case PPC::EXTSWSLI_32_64_rec:
  case PPC::EXTSWSLI_rec:
  case PPC::EXTSW_32:
  case PPC::EXTSW_32_64:
  case PPC::EXTSW_32_64_rec:
  case PPC::EXTSW_rec:
  case PPC::SRAW:
  case PPC::SRAWI:
  case PPC::SRAWI_rec:
  case PPC::SRAW_rec:
    return true;
  }
  return false;
}

MachineBasicBlock *PPCTargetLowering::EmitPartwordAtomicBinary(
    MachineInstr &MI, MachineBasicBlock *BB,
    bool is8bit, // operation
    unsigned BinOpcode, unsigned CmpOpcode, unsigned CmpPred) const {
  // This also handles ATOMIC_SWAP, indicated by BinOpcode==0.
  const PPCInstrInfo *TII = Subtarget.getInstrInfo();

  // If this is a signed comparison and the value being compared is not known
  // to be sign extended, sign extend it here.
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *F = BB->getParent();
  MachineRegisterInfo &RegInfo = F->getRegInfo();
  Register incr = MI.getOperand(3).getReg();
  bool IsSignExtended =
      incr.isVirtual() && isSignExtended(*RegInfo.getVRegDef(incr), TII);

  if (CmpOpcode == PPC::CMPW && !IsSignExtended) {
    Register ValueReg = RegInfo.createVirtualRegister(&PPC::GPRCRegClass);
    BuildMI(*BB, MI, dl, TII->get(is8bit ? PPC::EXTSB : PPC::EXTSH), ValueReg)
        .addReg(MI.getOperand(3).getReg());
    MI.getOperand(3).setReg(ValueReg);
    incr = ValueReg;
  }
  // If we support part-word atomic mnemonics, just use them
  if (Subtarget.hasPartwordAtomics())
    return EmitAtomicBinary(MI, BB, is8bit ? 1 : 2, BinOpcode, CmpOpcode,
                            CmpPred);

  // In 64 bit mode we have to use 64 bits for addresses, even though the
  // lwarx/stwcx are 32 bits.  With the 32-bit atomics we can use address
  // registers without caring whether they're 32 or 64, but here we're
  // doing actual arithmetic on the addresses.
  bool is64bit = Subtarget.isPPC64();
  bool isLittleEndian = Subtarget.isLittleEndian();
  unsigned ZeroReg = is64bit ? PPC::ZERO8 : PPC::ZERO;

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  Register dest = MI.getOperand(0).getReg();
  Register ptrA = MI.getOperand(1).getReg();
  Register ptrB = MI.getOperand(2).getReg();

  MachineBasicBlock *loopMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *loop2MBB =
      CmpOpcode ? F->CreateMachineBasicBlock(LLVM_BB) : nullptr;
  MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, loopMBB);
  if (CmpOpcode)
    F->insert(It, loop2MBB);
  F->insert(It, exitMBB);
  exitMBB->splice(exitMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  exitMBB->transferSuccessorsAndUpdatePHIs(BB);

  const TargetRegisterClass *RC =
      is64bit ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  Register PtrReg = RegInfo.createVirtualRegister(RC);
  Register Shift1Reg = RegInfo.createVirtualRegister(GPRC);
  Register ShiftReg =
      isLittleEndian ? Shift1Reg : RegInfo.createVirtualRegister(GPRC);
  Register Incr2Reg = RegInfo.createVirtualRegister(GPRC);
  Register MaskReg = RegInfo.createVirtualRegister(GPRC);
  Register Mask2Reg = RegInfo.createVirtualRegister(GPRC);
  Register Mask3Reg = RegInfo.createVirtualRegister(GPRC);
  Register Tmp2Reg = RegInfo.createVirtualRegister(GPRC);
  Register Tmp3Reg = RegInfo.createVirtualRegister(GPRC);
  Register Tmp4Reg = RegInfo.createVirtualRegister(GPRC);
  Register TmpDestReg = RegInfo.createVirtualRegister(GPRC);
  Register SrwDestReg = RegInfo.createVirtualRegister(GPRC);
  Register Ptr1Reg;
  Register TmpReg =
      (!BinOpcode) ? Incr2Reg : RegInfo.createVirtualRegister(GPRC);

  //  thisMBB:
  //   ...
  //   fallthrough --> loopMBB
  BB->addSuccessor(loopMBB);

  // The 4-byte load must be aligned, while a char or short may be
  // anywhere in the word.  Hence all this nasty bookkeeping code.
  //   add ptr1, ptrA, ptrB [copy if ptrA==0]
  //   rlwinm shift1, ptr1, 3, 27, 28 [3, 27, 27]
  //   xori shift, shift1, 24 [16]
  //   rlwinm ptr, ptr1, 0, 0, 29
  //   slw incr2, incr, shift
  //   li mask2, 255 [li mask3, 0; ori mask2, mask3, 65535]
  //   slw mask, mask2, shift
  //  loopMBB:
  //   lwarx tmpDest, ptr
  //   add tmp, tmpDest, incr2
  //   andc tmp2, tmpDest, mask
  //   and tmp3, tmp, mask
  //   or tmp4, tmp3, tmp2
  //   stwcx. tmp4, ptr
  //   bne- loopMBB
  //   fallthrough --> exitMBB
  //   srw SrwDest, tmpDest, shift
  //   rlwinm SrwDest, SrwDest, 0, 24 [16], 31
  if (ptrA != ZeroReg) {
    Ptr1Reg = RegInfo.createVirtualRegister(RC);
    BuildMI(BB, dl, TII->get(is64bit ? PPC::ADD8 : PPC::ADD4), Ptr1Reg)
        .addReg(ptrA)
        .addReg(ptrB);
  } else {
    Ptr1Reg = ptrB;
  }
  // We need use 32-bit subregister to avoid mismatch register class in 64-bit
  // mode.
  BuildMI(BB, dl, TII->get(PPC::RLWINM), Shift1Reg)
      .addReg(Ptr1Reg, 0, is64bit ? PPC::sub_32 : 0)
      .addImm(3)
      .addImm(27)
      .addImm(is8bit ? 28 : 27);
  if (!isLittleEndian)
    BuildMI(BB, dl, TII->get(PPC::XORI), ShiftReg)
        .addReg(Shift1Reg)
        .addImm(is8bit ? 24 : 16);
  if (is64bit)
    BuildMI(BB, dl, TII->get(PPC::RLDICR), PtrReg)
        .addReg(Ptr1Reg)
        .addImm(0)
        .addImm(61);
  else
    BuildMI(BB, dl, TII->get(PPC::RLWINM), PtrReg)
        .addReg(Ptr1Reg)
        .addImm(0)
        .addImm(0)
        .addImm(29);
  BuildMI(BB, dl, TII->get(PPC::SLW), Incr2Reg).addReg(incr).addReg(ShiftReg);
  if (is8bit)
    BuildMI(BB, dl, TII->get(PPC::LI), Mask2Reg).addImm(255);
  else {
    BuildMI(BB, dl, TII->get(PPC::LI), Mask3Reg).addImm(0);
    BuildMI(BB, dl, TII->get(PPC::ORI), Mask2Reg)
        .addReg(Mask3Reg)
        .addImm(65535);
  }
  BuildMI(BB, dl, TII->get(PPC::SLW), MaskReg)
      .addReg(Mask2Reg)
      .addReg(ShiftReg);

  BB = loopMBB;
  BuildMI(BB, dl, TII->get(PPC::LWARX), TmpDestReg)
      .addReg(ZeroReg)
      .addReg(PtrReg);
  if (BinOpcode)
    BuildMI(BB, dl, TII->get(BinOpcode), TmpReg)
        .addReg(Incr2Reg)
        .addReg(TmpDestReg);
  BuildMI(BB, dl, TII->get(PPC::ANDC), Tmp2Reg)
      .addReg(TmpDestReg)
      .addReg(MaskReg);
  BuildMI(BB, dl, TII->get(PPC::AND), Tmp3Reg).addReg(TmpReg).addReg(MaskReg);
  if (CmpOpcode) {
    // For unsigned comparisons, we can directly compare the shifted values.
    // For signed comparisons we shift and sign extend.
    Register SReg = RegInfo.createVirtualRegister(GPRC);
    Register CrReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);
    BuildMI(BB, dl, TII->get(PPC::AND), SReg)
        .addReg(TmpDestReg)
        .addReg(MaskReg);
    unsigned ValueReg = SReg;
    unsigned CmpReg = Incr2Reg;
    if (CmpOpcode == PPC::CMPW) {
      ValueReg = RegInfo.createVirtualRegister(GPRC);
      BuildMI(BB, dl, TII->get(PPC::SRW), ValueReg)
          .addReg(SReg)
          .addReg(ShiftReg);
      Register ValueSReg = RegInfo.createVirtualRegister(GPRC);
      BuildMI(BB, dl, TII->get(is8bit ? PPC::EXTSB : PPC::EXTSH), ValueSReg)
          .addReg(ValueReg);
      ValueReg = ValueSReg;
      CmpReg = incr;
    }
    BuildMI(BB, dl, TII->get(CmpOpcode), CrReg).addReg(ValueReg).addReg(CmpReg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(CmpPred)
        .addReg(CrReg)
        .addMBB(exitMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(exitMBB);
    BB = loop2MBB;
  }
  BuildMI(BB, dl, TII->get(PPC::OR), Tmp4Reg).addReg(Tmp3Reg).addReg(Tmp2Reg);
  BuildMI(BB, dl, TII->get(PPC::STWCX))
      .addReg(Tmp4Reg)
      .addReg(ZeroReg)
      .addReg(PtrReg);
  BuildMI(BB, dl, TII->get(PPC::BCC))
      .addImm(PPC::PRED_NE)
      .addReg(PPC::CR0)
      .addMBB(loopMBB);
  BB->addSuccessor(loopMBB);
  BB->addSuccessor(exitMBB);

  //  exitMBB:
  //   ...
  BB = exitMBB;
  // Since the shift amount is not a constant, we need to clear
  // the upper bits with a separate RLWINM.
  BuildMI(*BB, BB->begin(), dl, TII->get(PPC::RLWINM), dest)
      .addReg(SrwDestReg)
      .addImm(0)
      .addImm(is8bit ? 24 : 16)
      .addImm(31);
  BuildMI(*BB, BB->begin(), dl, TII->get(PPC::SRW), SrwDestReg)
      .addReg(TmpDestReg)
      .addReg(ShiftReg);
  return BB;
}

llvm::MachineBasicBlock *
PPCTargetLowering::emitEHSjLjSetJmp(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  const PPCRegisterInfo *TRI = Subtarget.getRegisterInfo();

  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator I = ++MBB->getIterator();

  Register DstReg = MI.getOperand(0).getReg();
  const TargetRegisterClass *RC = MRI.getRegClass(DstReg);
  assert(TRI->isTypeLegalForClass(*RC, MVT::i32) && "Invalid destination!");
  Register mainDstReg = MRI.createVirtualRegister(RC);
  Register restoreDstReg = MRI.createVirtualRegister(RC);

  MVT PVT = getPointerTy(MF->getDataLayout());
  assert((PVT == MVT::i64 || PVT == MVT::i32) &&
         "Invalid Pointer Size!");
  // For v = setjmp(buf), we generate
  //
  // thisMBB:
  //  SjLjSetup mainMBB
  //  bl mainMBB
  //  v_restore = 1
  //  b sinkMBB
  //
  // mainMBB:
  //  buf[LabelOffset] = LR
  //  v_main = 0
  //
  // sinkMBB:
  //  v = phi(main, restore)
  //

  MachineBasicBlock *thisMBB = MBB;
  MachineBasicBlock *mainMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *sinkMBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(I, mainMBB);
  MF->insert(I, sinkMBB);

  MachineInstrBuilder MIB;

  // Transfer the remainder of BB and its successor edges to sinkMBB.
  sinkMBB->splice(sinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  sinkMBB->transferSuccessorsAndUpdatePHIs(MBB);

  // Note that the structure of the jmp_buf used here is not compatible
  // with that used by libc, and is not designed to be. Specifically, it
  // stores only those 'reserved' registers that LLVM does not otherwise
  // understand how to spill. Also, by convention, by the time this
  // intrinsic is called, Clang has already stored the frame address in the
  // first slot of the buffer and stack address in the third. Following the
  // X86 target code, we'll store the jump address in the second slot. We also
  // need to save the TOC pointer (R2) to handle jumps between shared
  // libraries, and that will be stored in the fourth slot. The thread
  // identifier (R13) is not affected.

  // thisMBB:
  const int64_t LabelOffset = 1 * PVT.getStoreSize();
  const int64_t TOCOffset   = 3 * PVT.getStoreSize();
  const int64_t BPOffset    = 4 * PVT.getStoreSize();

  // Prepare IP either in reg.
  const TargetRegisterClass *PtrRC = getRegClassFor(PVT);
  Register LabelReg = MRI.createVirtualRegister(PtrRC);
  Register BufReg = MI.getOperand(1).getReg();

  if (Subtarget.is64BitELFABI()) {
    setUsesTOCBasePtr(*MBB->getParent());
    MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::STD))
              .addReg(PPC::X2)
              .addImm(TOCOffset)
              .addReg(BufReg)
              .cloneMemRefs(MI);
  }

  // Naked functions never have a base pointer, and so we use r1. For all
  // other functions, this decision must be delayed until during PEI.
  unsigned BaseReg;
  if (MF->getFunction().hasFnAttribute(Attribute::Naked))
    BaseReg = Subtarget.isPPC64() ? PPC::X1 : PPC::R1;
  else
    BaseReg = Subtarget.isPPC64() ? PPC::BP8 : PPC::BP;

  MIB = BuildMI(*thisMBB, MI, DL,
                TII->get(Subtarget.isPPC64() ? PPC::STD : PPC::STW))
            .addReg(BaseReg)
            .addImm(BPOffset)
            .addReg(BufReg)
            .cloneMemRefs(MI);

  // Setup
  MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::BCLalways)).addMBB(mainMBB);
  MIB.addRegMask(TRI->getNoPreservedMask());

  BuildMI(*thisMBB, MI, DL, TII->get(PPC::LI), restoreDstReg).addImm(1);

  MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::EH_SjLj_Setup))
          .addMBB(mainMBB);
  MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::B)).addMBB(sinkMBB);

  thisMBB->addSuccessor(mainMBB, BranchProbability::getZero());
  thisMBB->addSuccessor(sinkMBB, BranchProbability::getOne());

  // mainMBB:
  //  mainDstReg = 0
  MIB =
      BuildMI(mainMBB, DL,
              TII->get(Subtarget.isPPC64() ? PPC::MFLR8 : PPC::MFLR), LabelReg);

  // Store IP
  if (Subtarget.isPPC64()) {
    MIB = BuildMI(mainMBB, DL, TII->get(PPC::STD))
            .addReg(LabelReg)
            .addImm(LabelOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(mainMBB, DL, TII->get(PPC::STW))
            .addReg(LabelReg)
            .addImm(LabelOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  BuildMI(mainMBB, DL, TII->get(PPC::LI), mainDstReg).addImm(0);
  mainMBB->addSuccessor(sinkMBB);

  // sinkMBB:
  BuildMI(*sinkMBB, sinkMBB->begin(), DL,
          TII->get(PPC::PHI), DstReg)
    .addReg(mainDstReg).addMBB(mainMBB)
    .addReg(restoreDstReg).addMBB(thisMBB);

  MI.eraseFromParent();
  return sinkMBB;
}

MachineBasicBlock *
PPCTargetLowering::emitEHSjLjLongJmp(MachineInstr &MI,
                                     MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();

  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  MVT PVT = getPointerTy(MF->getDataLayout());
  assert((PVT == MVT::i64 || PVT == MVT::i32) &&
         "Invalid Pointer Size!");

  const TargetRegisterClass *RC =
    (PVT == MVT::i64) ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;
  Register Tmp = MRI.createVirtualRegister(RC);
  // Since FP is only updated here but NOT referenced, it's treated as GPR.
  unsigned FP  = (PVT == MVT::i64) ? PPC::X31 : PPC::R31;
  unsigned SP  = (PVT == MVT::i64) ? PPC::X1 : PPC::R1;
  unsigned BP =
      (PVT == MVT::i64)
          ? PPC::X30
          : (Subtarget.isSVR4ABI() && isPositionIndependent() ? PPC::R29
                                                              : PPC::R30);

  MachineInstrBuilder MIB;

  const int64_t LabelOffset = 1 * PVT.getStoreSize();
  const int64_t SPOffset    = 2 * PVT.getStoreSize();
  const int64_t TOCOffset   = 3 * PVT.getStoreSize();
  const int64_t BPOffset    = 4 * PVT.getStoreSize();

  Register BufReg = MI.getOperand(0).getReg();

  // Reload FP (the jumped-to function may not have had a
  // frame pointer, and if so, then its r31 will be restored
  // as necessary).
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), FP)
            .addImm(0)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), FP)
            .addImm(0)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload IP
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), Tmp)
            .addImm(LabelOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), Tmp)
            .addImm(LabelOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload SP
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), SP)
            .addImm(SPOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), SP)
            .addImm(SPOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload BP
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), BP)
            .addImm(BPOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), BP)
            .addImm(BPOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload TOC
  if (PVT == MVT::i64 && Subtarget.isSVR4ABI()) {
    setUsesTOCBasePtr(*MBB->getParent());
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), PPC::X2)
              .addImm(TOCOffset)
              .addReg(BufReg)
              .cloneMemRefs(MI);
  }

  // Jump
  BuildMI(*MBB, MI, DL,
          TII->get(PVT == MVT::i64 ? PPC::MTCTR8 : PPC::MTCTR)).addReg(Tmp);
  BuildMI(*MBB, MI, DL, TII->get(PVT == MVT::i64 ? PPC::BCTR8 : PPC::BCTR));

  MI.eraseFromParent();
  return MBB;
}

bool PPCTargetLowering::hasInlineStackProbe(const MachineFunction &MF) const {
  // If the function specifically requests inline stack probes, emit them.
  if (MF.getFunction().hasFnAttribute("probe-stack"))
    return MF.getFunction().getFnAttribute("probe-stack").getValueAsString() ==
           "inline-asm";
  return false;
}

unsigned PPCTargetLowering::getStackProbeSize(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = Subtarget.getFrameLowering();
  unsigned StackAlign = TFI->getStackAlignment();
  assert(StackAlign >= 1 && isPowerOf2_32(StackAlign) &&
         "Unexpected stack alignment");
  // The default stack probe size is 4096 if the function has no
  // stack-probe-size attribute.
  const Function &Fn = MF.getFunction();
  unsigned StackProbeSize =
      Fn.getFnAttributeAsParsedInteger("stack-probe-size", 4096);
  // Round down to the stack alignment.
  StackProbeSize &= ~(StackAlign - 1);
  return StackProbeSize ? StackProbeSize : StackAlign;
}

// Lower dynamic stack allocation with probing. `emitProbedAlloca` is splitted
// into three phases. In the first phase, it uses pseudo instruction
// PREPARE_PROBED_ALLOCA to get the future result of actual FramePointer and
// FinalStackPtr. In the second phase, it generates a loop for probing blocks.
// At last, it uses pseudo instruction DYNAREAOFFSET to get the future result of
// MaxCallFrameSize so that it can calculate correct data area pointer.
MachineBasicBlock *
PPCTargetLowering::emitProbedAlloca(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const {
  const bool isPPC64 = Subtarget.isPPC64();
  MachineFunction *MF = MBB->getParent();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  const unsigned ProbeSize = getStackProbeSize(*MF);
  const BasicBlock *ProbedBB = MBB->getBasicBlock();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  // The CFG of probing stack looks as
  //         +-----+
  //         | MBB |
  //         +--+--+
  //            |
  //       +----v----+
  //  +--->+ TestMBB +---+
  //  |    +----+----+   |
  //  |         |        |
  //  |   +-----v----+   |
  //  +---+ BlockMBB |   |
  //      +----------+   |
  //                     |
  //       +---------+   |
  //       | TailMBB +<--+
  //       +---------+
  // In MBB, calculate previous frame pointer and final stack pointer.
  // In TestMBB, test if sp is equal to final stack pointer, if so, jump to
  // TailMBB. In BlockMBB, update the sp atomically and jump back to TestMBB.
  // TailMBB is spliced via \p MI.
  MachineBasicBlock *TestMBB = MF->CreateMachineBasicBlock(ProbedBB);
  MachineBasicBlock *TailMBB = MF->CreateMachineBasicBlock(ProbedBB);
  MachineBasicBlock *BlockMBB = MF->CreateMachineBasicBlock(ProbedBB);

  MachineFunction::iterator MBBIter = ++MBB->getIterator();
  MF->insert(MBBIter, TestMBB);
  MF->insert(MBBIter, BlockMBB);
  MF->insert(MBBIter, TailMBB);

  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  Register DstReg = MI.getOperand(0).getReg();
  Register NegSizeReg = MI.getOperand(1).getReg();
  Register SPReg = isPPC64 ? PPC::X1 : PPC::R1;
  Register FinalStackPtr = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
  Register FramePointer = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
  Register ActualNegSizeReg = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);

  // Since value of NegSizeReg might be realigned in prologepilog, insert a
  // PREPARE_PROBED_ALLOCA pseudo instruction to get actual FramePointer and
  // NegSize.
  unsigned ProbeOpc;
  if (!MRI.hasOneNonDBGUse(NegSizeReg))
    ProbeOpc =
        isPPC64 ? PPC::PREPARE_PROBED_ALLOCA_64 : PPC::PREPARE_PROBED_ALLOCA_32;
  else
    // By introducing PREPARE_PROBED_ALLOCA_NEGSIZE_OPT, ActualNegSizeReg
    // and NegSizeReg will be allocated in the same phyreg to avoid
    // redundant copy when NegSizeReg has only one use which is current MI and
    // will be replaced by PREPARE_PROBED_ALLOCA then.
    ProbeOpc = isPPC64 ? PPC::PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_64
                       : PPC::PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_32;
  BuildMI(*MBB, {MI}, DL, TII->get(ProbeOpc), FramePointer)
      .addDef(ActualNegSizeReg)
      .addReg(NegSizeReg)
      .add(MI.getOperand(2))
      .add(MI.getOperand(3));

  // Calculate final stack pointer, which equals to SP + ActualNegSize.
  BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::ADD8 : PPC::ADD4),
          FinalStackPtr)
      .addReg(SPReg)
      .addReg(ActualNegSizeReg);

  // Materialize a scratch register for update.
  int64_t NegProbeSize = -(int64_t)ProbeSize;
  assert(isInt<32>(NegProbeSize) && "Unhandled probe size!");
  Register ScratchReg = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
  if (!isInt<16>(NegProbeSize)) {
    Register TempReg = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
    BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::LIS8 : PPC::LIS), TempReg)
        .addImm(NegProbeSize >> 16);
    BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::ORI8 : PPC::ORI),
            ScratchReg)
        .addReg(TempReg)
        .addImm(NegProbeSize & 0xFFFF);
  } else
    BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::LI8 : PPC::LI), ScratchReg)
        .addImm(NegProbeSize);

  {
    // Probing leading residual part.
    Register Div = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
    BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::DIVD : PPC::DIVW), Div)
        .addReg(ActualNegSizeReg)
        .addReg(ScratchReg);
    Register Mul = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
    BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::MULLD : PPC::MULLW), Mul)
        .addReg(Div)
        .addReg(ScratchReg);
    Register NegMod = MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
    BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::SUBF8 : PPC::SUBF), NegMod)
        .addReg(Mul)
        .addReg(ActualNegSizeReg);
    BuildMI(*MBB, {MI}, DL, TII->get(isPPC64 ? PPC::STDUX : PPC::STWUX), SPReg)
        .addReg(FramePointer)
        .addReg(SPReg)
        .addReg(NegMod);
  }

  {
    // Remaining part should be multiple of ProbeSize.
    Register CmpResult = MRI.createVirtualRegister(&PPC::CRRCRegClass);
    BuildMI(TestMBB, DL, TII->get(isPPC64 ? PPC::CMPD : PPC::CMPW), CmpResult)
        .addReg(SPReg)
        .addReg(FinalStackPtr);
    BuildMI(TestMBB, DL, TII->get(PPC::BCC))
        .addImm(PPC::PRED_EQ)
        .addReg(CmpResult)
        .addMBB(TailMBB);
    TestMBB->addSuccessor(BlockMBB);
    TestMBB->addSuccessor(TailMBB);
  }

  {
    // Touch the block.
    // |P...|P...|P...
    BuildMI(BlockMBB, DL, TII->get(isPPC64 ? PPC::STDUX : PPC::STWUX), SPReg)
        .addReg(FramePointer)
        .addReg(SPReg)
        .addReg(ScratchReg);
    BuildMI(BlockMBB, DL, TII->get(PPC::B)).addMBB(TestMBB);
    BlockMBB->addSuccessor(TestMBB);
  }

  // Calculation of MaxCallFrameSize is deferred to prologepilog, use
  // DYNAREAOFFSET pseudo instruction to get the future result.
  Register MaxCallFrameSizeReg =
      MRI.createVirtualRegister(isPPC64 ? G8RC : GPRC);
  BuildMI(TailMBB, DL,
          TII->get(isPPC64 ? PPC::DYNAREAOFFSET8 : PPC::DYNAREAOFFSET),
          MaxCallFrameSizeReg)
      .add(MI.getOperand(2))
      .add(MI.getOperand(3));
  BuildMI(TailMBB, DL, TII->get(isPPC64 ? PPC::ADD8 : PPC::ADD4), DstReg)
      .addReg(SPReg)
      .addReg(MaxCallFrameSizeReg);

  // Splice instructions after MI to TailMBB.
  TailMBB->splice(TailMBB->end(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  TailMBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(TestMBB);

  // Delete the pseudo instruction.
  MI.eraseFromParent();

  ++NumDynamicAllocaProbed;
  return TailMBB;
}

static bool IsSelectCC(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case PPC::SELECT_CC_I4:
  case PPC::SELECT_CC_I8:
  case PPC::SELECT_CC_F4:
  case PPC::SELECT_CC_F8:
  case PPC::SELECT_CC_F16:
  case PPC::SELECT_CC_VRRC:
  case PPC::SELECT_CC_VSFRC:
  case PPC::SELECT_CC_VSSRC:
  case PPC::SELECT_CC_VSRC:
  case PPC::SELECT_CC_SPE4:
  case PPC::SELECT_CC_SPE:
    return true;
  default:
    return false;
  }
}

static bool IsSelect(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case PPC::SELECT_I4:
  case PPC::SELECT_I8:
  case PPC::SELECT_F4:
  case PPC::SELECT_F8:
  case PPC::SELECT_F16:
  case PPC::SELECT_SPE:
  case PPC::SELECT_SPE4:
  case PPC::SELECT_VRRC:
  case PPC::SELECT_VSFRC:
  case PPC::SELECT_VSSRC:
  case PPC::SELECT_VSRC:
    return true;
  default:
    return false;
  }
}

MachineBasicBlock *
PPCTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  if (MI.getOpcode() == TargetOpcode::STACKMAP ||
      MI.getOpcode() == TargetOpcode::PATCHPOINT) {
    if (Subtarget.is64BitELFABI() &&
        MI.getOpcode() == TargetOpcode::PATCHPOINT &&
        !Subtarget.isUsingPCRelativeCalls()) {
      // Call lowering should have added an r2 operand to indicate a dependence
      // on the TOC base pointer value. It can't however, because there is no
      // way to mark the dependence as implicit there, and so the stackmap code
      // will confuse it with a regular operand. Instead, add the dependence
      // here.
      MI.addOperand(MachineOperand::CreateReg(PPC::X2, false, true));
    }

    return emitPatchPoint(MI, BB);
  }

  if (MI.getOpcode() == PPC::EH_SjLj_SetJmp32 ||
      MI.getOpcode() == PPC::EH_SjLj_SetJmp64) {
    return emitEHSjLjSetJmp(MI, BB);
  } else if (MI.getOpcode() == PPC::EH_SjLj_LongJmp32 ||
             MI.getOpcode() == PPC::EH_SjLj_LongJmp64) {
    return emitEHSjLjLongJmp(MI, BB);
  }

  const TargetInstrInfo *TII = Subtarget.getInstrInfo();

  // To "insert" these instructions we actually have to insert their
  // control-flow patterns.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  MachineFunction *F = BB->getParent();
  MachineRegisterInfo &MRI = F->getRegInfo();

  if (Subtarget.hasISEL() &&
      (MI.getOpcode() == PPC::SELECT_CC_I4 ||
       MI.getOpcode() == PPC::SELECT_CC_I8 ||
       MI.getOpcode() == PPC::SELECT_I4 || MI.getOpcode() == PPC::SELECT_I8)) {
    SmallVector<MachineOperand, 2> Cond;
    if (MI.getOpcode() == PPC::SELECT_CC_I4 ||
        MI.getOpcode() == PPC::SELECT_CC_I8)
      Cond.push_back(MI.getOperand(4));
    else
      Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_SET));
    Cond.push_back(MI.getOperand(1));

    DebugLoc dl = MI.getDebugLoc();
    TII->insertSelect(*BB, MI, dl, MI.getOperand(0).getReg(), Cond,
                      MI.getOperand(2).getReg(), MI.getOperand(3).getReg());
  } else if (IsSelectCC(MI) || IsSelect(MI)) {
    // The incoming instruction knows the destination vreg to set, the
    // condition code register to branch on, the true/false values to
    // select between, and a branch opcode to use.

    //  thisMBB:
    //  ...
    //   TrueVal = ...
    //   cmpTY ccX, r1, r2
    //   bCC sinkMBB
    //   fallthrough --> copy0MBB
    MachineBasicBlock *thisMBB = BB;
    MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *sinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
    DebugLoc dl = MI.getDebugLoc();
    F->insert(It, copy0MBB);
    F->insert(It, sinkMBB);

    // Set the call frame size on entry to the new basic blocks.
    // See https://reviews.llvm.org/D156113.
    unsigned CallFrameSize = TII->getCallFrameSizeAt(MI);
    copy0MBB->setCallFrameSize(CallFrameSize);
    sinkMBB->setCallFrameSize(CallFrameSize);

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    sinkMBB->splice(sinkMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

    // Next, add the true and fallthrough blocks as its successors.
    BB->addSuccessor(copy0MBB);
    BB->addSuccessor(sinkMBB);

    if (IsSelect(MI)) {
      BuildMI(BB, dl, TII->get(PPC::BC))
          .addReg(MI.getOperand(1).getReg())
          .addMBB(sinkMBB);
    } else {
      unsigned SelectPred = MI.getOperand(4).getImm();
      BuildMI(BB, dl, TII->get(PPC::BCC))
          .addImm(SelectPred)
          .addReg(MI.getOperand(1).getReg())
          .addMBB(sinkMBB);
    }

    //  copy0MBB:
    //   %FalseValue = ...
    //   # fallthrough to sinkMBB
    BB = copy0MBB;

    // Update machine-CFG edges
    BB->addSuccessor(sinkMBB);

    //  sinkMBB:
    //   %Result = phi [ %FalseValue, copy0MBB ], [ %TrueValue, thisMBB ]
    //  ...
    BB = sinkMBB;
    BuildMI(*BB, BB->begin(), dl, TII->get(PPC::PHI), MI.getOperand(0).getReg())
        .addReg(MI.getOperand(3).getReg())
        .addMBB(copy0MBB)
        .addReg(MI.getOperand(2).getReg())
        .addMBB(thisMBB);
  } else if (MI.getOpcode() == PPC::ReadTB) {
    // To read the 64-bit time-base register on a 32-bit target, we read the
    // two halves. Should the counter have wrapped while it was being read, we
    // need to try again.
    // ...
    // readLoop:
    // mfspr Rx,TBU # load from TBU
    // mfspr Ry,TB  # load from TB
    // mfspr Rz,TBU # load from TBU
    // cmpw crX,Rx,Rz # check if 'old'='new'
    // bne readLoop   # branch if they're not equal
    // ...

    MachineBasicBlock *readMBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *sinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
    DebugLoc dl = MI.getDebugLoc();
    F->insert(It, readMBB);
    F->insert(It, sinkMBB);

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    sinkMBB->splice(sinkMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

    BB->addSuccessor(readMBB);
    BB = readMBB;

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    Register ReadAgainReg = RegInfo.createVirtualRegister(&PPC::GPRCRegClass);
    Register LoReg = MI.getOperand(0).getReg();
    Register HiReg = MI.getOperand(1).getReg();

    BuildMI(BB, dl, TII->get(PPC::MFSPR), HiReg).addImm(269);
    BuildMI(BB, dl, TII->get(PPC::MFSPR), LoReg).addImm(268);
    BuildMI(BB, dl, TII->get(PPC::MFSPR), ReadAgainReg).addImm(269);

    Register CmpReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);

    BuildMI(BB, dl, TII->get(PPC::CMPW), CmpReg)
        .addReg(HiReg)
        .addReg(ReadAgainReg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(CmpReg)
        .addMBB(readMBB);

    BB->addSuccessor(readMBB);
    BB->addSuccessor(sinkMBB);
  } else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::ADD4);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::ADD4);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::ADD4);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::ADD8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::AND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::AND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::AND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::AND8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::OR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::OR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::OR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::OR8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::XOR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::XOR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::XOR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::XOR8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::NAND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::NAND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::NAND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::NAND8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::SUBF);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::SUBF);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::SUBF);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::SUBF8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPW, PPC::PRED_LT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPW, PPC::PRED_LT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPW, PPC::PRED_LT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPD, PPC::PRED_LT);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPW, PPC::PRED_GT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPW, PPC::PRED_GT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPW, PPC::PRED_GT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPD, PPC::PRED_GT);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPLW, PPC::PRED_LT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPLW, PPC::PRED_LT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPLW, PPC::PRED_LT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPLD, PPC::PRED_LT);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPLW, PPC::PRED_GT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPLW, PPC::PRED_GT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPLW, PPC::PRED_GT);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPLD, PPC::PRED_GT);

  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I32 ||
           MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I64 ||
           (Subtarget.hasPartwordAtomics() &&
            MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I8) ||
           (Subtarget.hasPartwordAtomics() &&
            MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I16)) {
    bool is64bit = MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I64;

    auto LoadMnemonic = PPC::LDARX;
    auto StoreMnemonic = PPC::STDCX;
    switch (MI.getOpcode()) {
    default:
      llvm_unreachable("Compare and swap of unknown size");
    case PPC::ATOMIC_CMP_SWAP_I8:
      LoadMnemonic = PPC::LBARX;
      StoreMnemonic = PPC::STBCX;
      assert(Subtarget.hasPartwordAtomics() && "No support partword atomics.");
      break;
    case PPC::ATOMIC_CMP_SWAP_I16:
      LoadMnemonic = PPC::LHARX;
      StoreMnemonic = PPC::STHCX;
      assert(Subtarget.hasPartwordAtomics() && "No support partword atomics.");
      break;
    case PPC::ATOMIC_CMP_SWAP_I32:
      LoadMnemonic = PPC::LWARX;
      StoreMnemonic = PPC::STWCX;
      break;
    case PPC::ATOMIC_CMP_SWAP_I64:
      LoadMnemonic = PPC::LDARX;
      StoreMnemonic = PPC::STDCX;
      break;
    }
    MachineRegisterInfo &RegInfo = F->getRegInfo();
    Register dest = MI.getOperand(0).getReg();
    Register ptrA = MI.getOperand(1).getReg();
    Register ptrB = MI.getOperand(2).getReg();
    Register CrReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);
    Register oldval = MI.getOperand(3).getReg();
    Register newval = MI.getOperand(4).getReg();
    DebugLoc dl = MI.getDebugLoc();

    MachineBasicBlock *loop1MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *loop2MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
    F->insert(It, loop1MBB);
    F->insert(It, loop2MBB);
    F->insert(It, exitMBB);
    exitMBB->splice(exitMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    exitMBB->transferSuccessorsAndUpdatePHIs(BB);

    //  thisMBB:
    //   ...
    //   fallthrough --> loopMBB
    BB->addSuccessor(loop1MBB);

    // loop1MBB:
    //   l[bhwd]arx dest, ptr
    //   cmp[wd] dest, oldval
    //   bne- exitBB
    // loop2MBB:
    //   st[bhwd]cx. newval, ptr
    //   bne- loopMBB
    //   b exitBB
    // exitBB:
    BB = loop1MBB;
    BuildMI(BB, dl, TII->get(LoadMnemonic), dest).addReg(ptrA).addReg(ptrB);
    BuildMI(BB, dl, TII->get(is64bit ? PPC::CMPD : PPC::CMPW), CrReg)
        .addReg(dest)
        .addReg(oldval);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(CrReg)
        .addMBB(exitMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(exitMBB);

    BB = loop2MBB;
    BuildMI(BB, dl, TII->get(StoreMnemonic))
        .addReg(newval)
        .addReg(ptrA)
        .addReg(ptrB);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(PPC::CR0)
        .addMBB(loop1MBB);
    BuildMI(BB, dl, TII->get(PPC::B)).addMBB(exitMBB);
    BB->addSuccessor(loop1MBB);
    BB->addSuccessor(exitMBB);

    //  exitMBB:
    //   ...
    BB = exitMBB;
  } else if (MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I8 ||
             MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I16) {
    // We must use 64-bit registers for addresses when targeting 64-bit,
    // since we're actually doing arithmetic on them.  Other registers
    // can be 32-bit.
    bool is64bit = Subtarget.isPPC64();
    bool isLittleEndian = Subtarget.isLittleEndian();
    bool is8bit = MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I8;

    Register dest = MI.getOperand(0).getReg();
    Register ptrA = MI.getOperand(1).getReg();
    Register ptrB = MI.getOperand(2).getReg();
    Register oldval = MI.getOperand(3).getReg();
    Register newval = MI.getOperand(4).getReg();
    DebugLoc dl = MI.getDebugLoc();

    MachineBasicBlock *loop1MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *loop2MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
    F->insert(It, loop1MBB);
    F->insert(It, loop2MBB);
    F->insert(It, exitMBB);
    exitMBB->splice(exitMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    exitMBB->transferSuccessorsAndUpdatePHIs(BB);

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    const TargetRegisterClass *RC =
        is64bit ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;
    const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

    Register PtrReg = RegInfo.createVirtualRegister(RC);
    Register Shift1Reg = RegInfo.createVirtualRegister(GPRC);
    Register ShiftReg =
        isLittleEndian ? Shift1Reg : RegInfo.createVirtualRegister(GPRC);
    Register NewVal2Reg = RegInfo.createVirtualRegister(GPRC);
    Register NewVal3Reg = RegInfo.createVirtualRegister(GPRC);
    Register OldVal2Reg = RegInfo.createVirtualRegister(GPRC);
    Register OldVal3Reg = RegInfo.createVirtualRegister(GPRC);
    Register MaskReg = RegInfo.createVirtualRegister(GPRC);
    Register Mask2Reg = RegInfo.createVirtualRegister(GPRC);
    Register Mask3Reg = RegInfo.createVirtualRegister(GPRC);
    Register Tmp2Reg = RegInfo.createVirtualRegister(GPRC);
    Register Tmp4Reg = RegInfo.createVirtualRegister(GPRC);
    Register TmpDestReg = RegInfo.createVirtualRegister(GPRC);
    Register Ptr1Reg;
    Register TmpReg = RegInfo.createVirtualRegister(GPRC);
    Register ZeroReg = is64bit ? PPC::ZERO8 : PPC::ZERO;
    Register CrReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);
    //  thisMBB:
    //   ...
    //   fallthrough --> loopMBB
    BB->addSuccessor(loop1MBB);

    // The 4-byte load must be aligned, while a char or short may be
    // anywhere in the word.  Hence all this nasty bookkeeping code.
    //   add ptr1, ptrA, ptrB [copy if ptrA==0]
    //   rlwinm shift1, ptr1, 3, 27, 28 [3, 27, 27]
    //   xori shift, shift1, 24 [16]
    //   rlwinm ptr, ptr1, 0, 0, 29
    //   slw newval2, newval, shift
    //   slw oldval2, oldval,shift
    //   li mask2, 255 [li mask3, 0; ori mask2, mask3, 65535]
    //   slw mask, mask2, shift
    //   and newval3, newval2, mask
    //   and oldval3, oldval2, mask
    // loop1MBB:
    //   lwarx tmpDest, ptr
    //   and tmp, tmpDest, mask
    //   cmpw tmp, oldval3
    //   bne- exitBB
    // loop2MBB:
    //   andc tmp2, tmpDest, mask
    //   or tmp4, tmp2, newval3
    //   stwcx. tmp4, ptr
    //   bne- loop1MBB
    //   b exitBB
    // exitBB:
    //   srw dest, tmpDest, shift
    if (ptrA != ZeroReg) {
      Ptr1Reg = RegInfo.createVirtualRegister(RC);
      BuildMI(BB, dl, TII->get(is64bit ? PPC::ADD8 : PPC::ADD4), Ptr1Reg)
          .addReg(ptrA)
          .addReg(ptrB);
    } else {
      Ptr1Reg = ptrB;
    }

    // We need use 32-bit subregister to avoid mismatch register class in 64-bit
    // mode.
    BuildMI(BB, dl, TII->get(PPC::RLWINM), Shift1Reg)
        .addReg(Ptr1Reg, 0, is64bit ? PPC::sub_32 : 0)
        .addImm(3)
        .addImm(27)
        .addImm(is8bit ? 28 : 27);
    if (!isLittleEndian)
      BuildMI(BB, dl, TII->get(PPC::XORI), ShiftReg)
          .addReg(Shift1Reg)
          .addImm(is8bit ? 24 : 16);
    if (is64bit)
      BuildMI(BB, dl, TII->get(PPC::RLDICR), PtrReg)
          .addReg(Ptr1Reg)
          .addImm(0)
          .addImm(61);
    else
      BuildMI(BB, dl, TII->get(PPC::RLWINM), PtrReg)
          .addReg(Ptr1Reg)
          .addImm(0)
          .addImm(0)
          .addImm(29);
    BuildMI(BB, dl, TII->get(PPC::SLW), NewVal2Reg)
        .addReg(newval)
        .addReg(ShiftReg);
    BuildMI(BB, dl, TII->get(PPC::SLW), OldVal2Reg)
        .addReg(oldval)
        .addReg(ShiftReg);
    if (is8bit)
      BuildMI(BB, dl, TII->get(PPC::LI), Mask2Reg).addImm(255);
    else {
      BuildMI(BB, dl, TII->get(PPC::LI), Mask3Reg).addImm(0);
      BuildMI(BB, dl, TII->get(PPC::ORI), Mask2Reg)
          .addReg(Mask3Reg)
          .addImm(65535);
    }
    BuildMI(BB, dl, TII->get(PPC::SLW), MaskReg)
        .addReg(Mask2Reg)
        .addReg(ShiftReg);
    BuildMI(BB, dl, TII->get(PPC::AND), NewVal3Reg)
        .addReg(NewVal2Reg)
        .addReg(MaskReg);
    BuildMI(BB, dl, TII->get(PPC::AND), OldVal3Reg)
        .addReg(OldVal2Reg)
        .addReg(MaskReg);

    BB = loop1MBB;
    BuildMI(BB, dl, TII->get(PPC::LWARX), TmpDestReg)
        .addReg(ZeroReg)
        .addReg(PtrReg);
    BuildMI(BB, dl, TII->get(PPC::AND), TmpReg)
        .addReg(TmpDestReg)
        .addReg(MaskReg);
    BuildMI(BB, dl, TII->get(PPC::CMPW), CrReg)
        .addReg(TmpReg)
        .addReg(OldVal3Reg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(CrReg)
        .addMBB(exitMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(exitMBB);

    BB = loop2MBB;
    BuildMI(BB, dl, TII->get(PPC::ANDC), Tmp2Reg)
        .addReg(TmpDestReg)
        .addReg(MaskReg);
    BuildMI(BB, dl, TII->get(PPC::OR), Tmp4Reg)
        .addReg(Tmp2Reg)
        .addReg(NewVal3Reg);
    BuildMI(BB, dl, TII->get(PPC::STWCX))
        .addReg(Tmp4Reg)
        .addReg(ZeroReg)
        .addReg(PtrReg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(PPC::CR0)
        .addMBB(loop1MBB);
    BuildMI(BB, dl, TII->get(PPC::B)).addMBB(exitMBB);
    BB->addSuccessor(loop1MBB);
    BB->addSuccessor(exitMBB);

    //  exitMBB:
    //   ...
    BB = exitMBB;
    BuildMI(*BB, BB->begin(), dl, TII->get(PPC::SRW), dest)
        .addReg(TmpReg)
        .addReg(ShiftReg);
  } else if (MI.getOpcode() == PPC::FADDrtz) {
    // This pseudo performs an FADD with rounding mode temporarily forced
    // to round-to-zero.  We emit this via custom inserter since the FPSCR
    // is not modeled at the SelectionDAG level.
    Register Dest = MI.getOperand(0).getReg();
    Register Src1 = MI.getOperand(1).getReg();
    Register Src2 = MI.getOperand(2).getReg();
    DebugLoc dl = MI.getDebugLoc();

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    Register MFFSReg = RegInfo.createVirtualRegister(&PPC::F8RCRegClass);

    // Save FPSCR value.
    BuildMI(*BB, MI, dl, TII->get(PPC::MFFS), MFFSReg);

    // Set rounding mode to round-to-zero.
    BuildMI(*BB, MI, dl, TII->get(PPC::MTFSB1))
        .addImm(31)
        .addReg(PPC::RM, RegState::ImplicitDefine);

    BuildMI(*BB, MI, dl, TII->get(PPC::MTFSB0))
        .addImm(30)
        .addReg(PPC::RM, RegState::ImplicitDefine);

    // Perform addition.
    auto MIB = BuildMI(*BB, MI, dl, TII->get(PPC::FADD), Dest)
                   .addReg(Src1)
                   .addReg(Src2);
    if (MI.getFlag(MachineInstr::NoFPExcept))
      MIB.setMIFlag(MachineInstr::NoFPExcept);

    // Restore FPSCR value.
    BuildMI(*BB, MI, dl, TII->get(PPC::MTFSFb)).addImm(1).addReg(MFFSReg);
  } else if (MI.getOpcode() == PPC::ANDI_rec_1_EQ_BIT ||
             MI.getOpcode() == PPC::ANDI_rec_1_GT_BIT ||
             MI.getOpcode() == PPC::ANDI_rec_1_EQ_BIT8 ||
             MI.getOpcode() == PPC::ANDI_rec_1_GT_BIT8) {
    unsigned Opcode = (MI.getOpcode() == PPC::ANDI_rec_1_EQ_BIT8 ||
                       MI.getOpcode() == PPC::ANDI_rec_1_GT_BIT8)
                          ? PPC::ANDI8_rec
                          : PPC::ANDI_rec;
    bool IsEQ = (MI.getOpcode() == PPC::ANDI_rec_1_EQ_BIT ||
                 MI.getOpcode() == PPC::ANDI_rec_1_EQ_BIT8);

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    Register Dest = RegInfo.createVirtualRegister(
        Opcode == PPC::ANDI_rec ? &PPC::GPRCRegClass : &PPC::G8RCRegClass);

    DebugLoc Dl = MI.getDebugLoc();
    BuildMI(*BB, MI, Dl, TII->get(Opcode), Dest)
        .addReg(MI.getOperand(1).getReg())
        .addImm(1);
    BuildMI(*BB, MI, Dl, TII->get(TargetOpcode::COPY),
            MI.getOperand(0).getReg())
        .addReg(IsEQ ? PPC::CR0EQ : PPC::CR0GT);
  } else if (MI.getOpcode() == PPC::TCHECK_RET) {
    DebugLoc Dl = MI.getDebugLoc();
    MachineRegisterInfo &RegInfo = F->getRegInfo();
    Register CRReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);
    BuildMI(*BB, MI, Dl, TII->get(PPC::TCHECK), CRReg);
    BuildMI(*BB, MI, Dl, TII->get(TargetOpcode::COPY),
            MI.getOperand(0).getReg())
        .addReg(CRReg);
  } else if (MI.getOpcode() == PPC::TBEGIN_RET) {
    DebugLoc Dl = MI.getDebugLoc();
    unsigned Imm = MI.getOperand(1).getImm();
    BuildMI(*BB, MI, Dl, TII->get(PPC::TBEGIN)).addImm(Imm);
    BuildMI(*BB, MI, Dl, TII->get(TargetOpcode::COPY),
            MI.getOperand(0).getReg())
        .addReg(PPC::CR0EQ);
  } else if (MI.getOpcode() == PPC::SETRNDi) {
    DebugLoc dl = MI.getDebugLoc();
    Register OldFPSCRReg = MI.getOperand(0).getReg();

    // Save FPSCR value.
    if (MRI.use_empty(OldFPSCRReg))
      BuildMI(*BB, MI, dl, TII->get(TargetOpcode::IMPLICIT_DEF), OldFPSCRReg);
    else
      BuildMI(*BB, MI, dl, TII->get(PPC::MFFS), OldFPSCRReg);

    // The floating point rounding mode is in the bits 62:63 of FPCSR, and has
    // the following settings:
    //   00 Round to nearest
    //   01 Round to 0
    //   10 Round to +inf
    //   11 Round to -inf

    // When the operand is immediate, using the two least significant bits of
    // the immediate to set the bits 62:63 of FPSCR.
    unsigned Mode = MI.getOperand(1).getImm();
    BuildMI(*BB, MI, dl, TII->get((Mode & 1) ? PPC::MTFSB1 : PPC::MTFSB0))
        .addImm(31)
        .addReg(PPC::RM, RegState::ImplicitDefine);

    BuildMI(*BB, MI, dl, TII->get((Mode & 2) ? PPC::MTFSB1 : PPC::MTFSB0))
        .addImm(30)
        .addReg(PPC::RM, RegState::ImplicitDefine);
  } else if (MI.getOpcode() == PPC::SETRND) {
    DebugLoc dl = MI.getDebugLoc();

    // Copy register from F8RCRegClass::SrcReg to G8RCRegClass::DestReg
    // or copy register from G8RCRegClass::SrcReg to F8RCRegClass::DestReg.
    // If the target doesn't have DirectMove, we should use stack to do the
    // conversion, because the target doesn't have the instructions like mtvsrd
    // or mfvsrd to do this conversion directly.
    auto copyRegFromG8RCOrF8RC = [&] (unsigned DestReg, unsigned SrcReg) {
      if (Subtarget.hasDirectMove()) {
        BuildMI(*BB, MI, dl, TII->get(TargetOpcode::COPY), DestReg)
          .addReg(SrcReg);
      } else {
        // Use stack to do the register copy.
        unsigned StoreOp = PPC::STD, LoadOp = PPC::LFD;
        MachineRegisterInfo &RegInfo = F->getRegInfo();
        const TargetRegisterClass *RC = RegInfo.getRegClass(SrcReg);
        if (RC == &PPC::F8RCRegClass) {
          // Copy register from F8RCRegClass to G8RCRegclass.
          assert((RegInfo.getRegClass(DestReg) == &PPC::G8RCRegClass) &&
                 "Unsupported RegClass.");

          StoreOp = PPC::STFD;
          LoadOp = PPC::LD;
        } else {
          // Copy register from G8RCRegClass to F8RCRegclass.
          assert((RegInfo.getRegClass(SrcReg) == &PPC::G8RCRegClass) &&
                 (RegInfo.getRegClass(DestReg) == &PPC::F8RCRegClass) &&
                 "Unsupported RegClass.");
        }

        MachineFrameInfo &MFI = F->getFrameInfo();
        int FrameIdx = MFI.CreateStackObject(8, Align(8), false);

        MachineMemOperand *MMOStore = F->getMachineMemOperand(
            MachinePointerInfo::getFixedStack(*F, FrameIdx, 0),
            MachineMemOperand::MOStore, MFI.getObjectSize(FrameIdx),
            MFI.getObjectAlign(FrameIdx));

        // Store the SrcReg into the stack.
        BuildMI(*BB, MI, dl, TII->get(StoreOp))
          .addReg(SrcReg)
          .addImm(0)
          .addFrameIndex(FrameIdx)
          .addMemOperand(MMOStore);

        MachineMemOperand *MMOLoad = F->getMachineMemOperand(
            MachinePointerInfo::getFixedStack(*F, FrameIdx, 0),
            MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIdx),
            MFI.getObjectAlign(FrameIdx));

        // Load from the stack where SrcReg is stored, and save to DestReg,
        // so we have done the RegClass conversion from RegClass::SrcReg to
        // RegClass::DestReg.
        BuildMI(*BB, MI, dl, TII->get(LoadOp), DestReg)
          .addImm(0)
          .addFrameIndex(FrameIdx)
          .addMemOperand(MMOLoad);
      }
    };

    Register OldFPSCRReg = MI.getOperand(0).getReg();

    // Save FPSCR value.
    BuildMI(*BB, MI, dl, TII->get(PPC::MFFS), OldFPSCRReg);

    // When the operand is gprc register, use two least significant bits of the
    // register and mtfsf instruction to set the bits 62:63 of FPSCR.
    //
    // copy OldFPSCRTmpReg, OldFPSCRReg
    // (INSERT_SUBREG ExtSrcReg, (IMPLICIT_DEF ImDefReg), SrcOp, 1)
    // rldimi NewFPSCRTmpReg, ExtSrcReg, OldFPSCRReg, 0, 62
    // copy NewFPSCRReg, NewFPSCRTmpReg
    // mtfsf 255, NewFPSCRReg
    MachineOperand SrcOp = MI.getOperand(1);
    MachineRegisterInfo &RegInfo = F->getRegInfo();
    Register OldFPSCRTmpReg = RegInfo.createVirtualRegister(&PPC::G8RCRegClass);

    copyRegFromG8RCOrF8RC(OldFPSCRTmpReg, OldFPSCRReg);

    Register ImDefReg = RegInfo.createVirtualRegister(&PPC::G8RCRegClass);
    Register ExtSrcReg = RegInfo.createVirtualRegister(&PPC::G8RCRegClass);

    // The first operand of INSERT_SUBREG should be a register which has
    // subregisters, we only care about its RegClass, so we should use an
    // IMPLICIT_DEF register.
    BuildMI(*BB, MI, dl, TII->get(TargetOpcode::IMPLICIT_DEF), ImDefReg);
    BuildMI(*BB, MI, dl, TII->get(PPC::INSERT_SUBREG), ExtSrcReg)
      .addReg(ImDefReg)
      .add(SrcOp)
      .addImm(1);

    Register NewFPSCRTmpReg = RegInfo.createVirtualRegister(&PPC::G8RCRegClass);
    BuildMI(*BB, MI, dl, TII->get(PPC::RLDIMI), NewFPSCRTmpReg)
      .addReg(OldFPSCRTmpReg)
      .addReg(ExtSrcReg)
      .addImm(0)
      .addImm(62);

    Register NewFPSCRReg = RegInfo.createVirtualRegister(&PPC::F8RCRegClass);
    copyRegFromG8RCOrF8RC(NewFPSCRReg, NewFPSCRTmpReg);

    // The mask 255 means that put the 32:63 bits of NewFPSCRReg to the 32:63
    // bits of FPSCR.
    BuildMI(*BB, MI, dl, TII->get(PPC::MTFSF))
      .addImm(255)
      .addReg(NewFPSCRReg)
      .addImm(0)
      .addImm(0);
  } else if (MI.getOpcode() == PPC::SETFLM) {
    DebugLoc Dl = MI.getDebugLoc();

    // Result of setflm is previous FPSCR content, so we need to save it first.
    Register OldFPSCRReg = MI.getOperand(0).getReg();
    if (MRI.use_empty(OldFPSCRReg))
      BuildMI(*BB, MI, Dl, TII->get(TargetOpcode::IMPLICIT_DEF), OldFPSCRReg);
    else
      BuildMI(*BB, MI, Dl, TII->get(PPC::MFFS), OldFPSCRReg);

    // Put bits in 32:63 to FPSCR.
    Register NewFPSCRReg = MI.getOperand(1).getReg();
    BuildMI(*BB, MI, Dl, TII->get(PPC::MTFSF))
        .addImm(255)
        .addReg(NewFPSCRReg)
        .addImm(0)
        .addImm(0);
  } else if (MI.getOpcode() == PPC::PROBED_ALLOCA_32 ||
             MI.getOpcode() == PPC::PROBED_ALLOCA_64) {
    return emitProbedAlloca(MI, BB);
  } else if (MI.getOpcode() == PPC::SPLIT_QUADWORD) {
    DebugLoc DL = MI.getDebugLoc();
    Register Src = MI.getOperand(2).getReg();
    Register Lo = MI.getOperand(0).getReg();
    Register Hi = MI.getOperand(1).getReg();
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY))
        .addDef(Lo)
        .addUse(Src, 0, PPC::sub_gp8_x1);
    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::COPY))
        .addDef(Hi)
        .addUse(Src, 0, PPC::sub_gp8_x0);
  } else if (MI.getOpcode() == PPC::LQX_PSEUDO ||
             MI.getOpcode() == PPC::STQX_PSEUDO) {
    DebugLoc DL = MI.getDebugLoc();
    // Ptr is used as the ptr_rc_no_r0 part
    // of LQ/STQ's memory operand and adding result of RA and RB,
    // so it has to be g8rc_and_g8rc_nox0.
    Register Ptr =
        F->getRegInfo().createVirtualRegister(&PPC::G8RC_and_G8RC_NOX0RegClass);
    Register Val = MI.getOperand(0).getReg();
    Register RA = MI.getOperand(1).getReg();
    Register RB = MI.getOperand(2).getReg();
    BuildMI(*BB, MI, DL, TII->get(PPC::ADD8), Ptr).addReg(RA).addReg(RB);
    BuildMI(*BB, MI, DL,
            MI.getOpcode() == PPC::LQX_PSEUDO ? TII->get(PPC::LQ)
                                              : TII->get(PPC::STQ))
        .addReg(Val, MI.getOpcode() == PPC::LQX_PSEUDO ? RegState::Define : 0)
        .addImm(0)
        .addReg(Ptr);
  } else {
    llvm_unreachable("Unexpected instr type to insert");
  }

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

//===----------------------------------------------------------------------===//
// Target Optimization Hooks
//===----------------------------------------------------------------------===//

static int getEstimateRefinementSteps(EVT VT, const PPCSubtarget &Subtarget) {
  // For the estimates, convergence is quadratic, so we essentially double the
  // number of digits correct after every iteration. For both FRE and FRSQRTE,
  // the minimum architected relative accuracy is 2^-5. When hasRecipPrec(),
  // this is 2^-14. IEEE float has 23 digits and double has 52 digits.
  int RefinementSteps = Subtarget.hasRecipPrec() ? 1 : 3;
  if (VT.getScalarType() == MVT::f64)
    RefinementSteps++;
  return RefinementSteps;
}

SDValue PPCTargetLowering::getSqrtInputTest(SDValue Op, SelectionDAG &DAG,
                                            const DenormalMode &Mode) const {
  // We only have VSX Vector Test for software Square Root.
  EVT VT = Op.getValueType();
  if (!isTypeLegal(MVT::i1) ||
      (VT != MVT::f64 &&
       ((VT != MVT::v2f64 && VT != MVT::v4f32) || !Subtarget.hasVSX())))
    return TargetLowering::getSqrtInputTest(Op, DAG, Mode);

  SDLoc DL(Op);
  // The output register of FTSQRT is CR field.
  SDValue FTSQRT = DAG.getNode(PPCISD::FTSQRT, DL, MVT::i32, Op);
  // ftsqrt BF,FRB
  // Let e_b be the unbiased exponent of the double-precision
  // floating-point operand in register FRB.
  // fe_flag is set to 1 if either of the following conditions occurs.
  //   - The double-precision floating-point operand in register FRB is a zero,
  //     a NaN, or an infinity, or a negative value.
  //   - e_b is less than or equal to -970.
  // Otherwise fe_flag is set to 0.
  // Both VSX and non-VSX versions would set EQ bit in the CR if the number is
  // not eligible for iteration. (zero/negative/infinity/nan or unbiased
  // exponent is less than -970)
  SDValue SRIdxVal = DAG.getTargetConstant(PPC::sub_eq, DL, MVT::i32);
  return SDValue(DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL, MVT::i1,
                                    FTSQRT, SRIdxVal),
                 0);
}

SDValue
PPCTargetLowering::getSqrtResultForDenormInput(SDValue Op,
                                               SelectionDAG &DAG) const {
  // We only have VSX Vector Square Root.
  EVT VT = Op.getValueType();
  if (VT != MVT::f64 &&
      ((VT != MVT::v2f64 && VT != MVT::v4f32) || !Subtarget.hasVSX()))
    return TargetLowering::getSqrtResultForDenormInput(Op, DAG);

  return DAG.getNode(PPCISD::FSQRT, SDLoc(Op), VT, Op);
}

SDValue PPCTargetLowering::getSqrtEstimate(SDValue Operand, SelectionDAG &DAG,
                                           int Enabled, int &RefinementSteps,
                                           bool &UseOneConstNR,
                                           bool Reciprocal) const {
  EVT VT = Operand.getValueType();
  if ((VT == MVT::f32 && Subtarget.hasFRSQRTES()) ||
      (VT == MVT::f64 && Subtarget.hasFRSQRTE()) ||
      (VT == MVT::v4f32 && Subtarget.hasAltivec()) ||
      (VT == MVT::v2f64 && Subtarget.hasVSX())) {
    if (RefinementSteps == ReciprocalEstimate::Unspecified)
      RefinementSteps = getEstimateRefinementSteps(VT, Subtarget);

    // The Newton-Raphson computation with a single constant does not provide
    // enough accuracy on some CPUs.
    UseOneConstNR = !Subtarget.needsTwoConstNR();
    return DAG.getNode(PPCISD::FRSQRTE, SDLoc(Operand), VT, Operand);
  }
  return SDValue();
}

SDValue PPCTargetLowering::getRecipEstimate(SDValue Operand, SelectionDAG &DAG,
                                            int Enabled,
                                            int &RefinementSteps) const {
  EVT VT = Operand.getValueType();
  if ((VT == MVT::f32 && Subtarget.hasFRES()) ||
      (VT == MVT::f64 && Subtarget.hasFRE()) ||
      (VT == MVT::v4f32 && Subtarget.hasAltivec()) ||
      (VT == MVT::v2f64 && Subtarget.hasVSX())) {
    if (RefinementSteps == ReciprocalEstimate::Unspecified)
      RefinementSteps = getEstimateRefinementSteps(VT, Subtarget);
    return DAG.getNode(PPCISD::FRE, SDLoc(Operand), VT, Operand);
  }
  return SDValue();
}

unsigned PPCTargetLowering::combineRepeatedFPDivisors() const {
  // Note: This functionality is used only when unsafe-fp-math is enabled, and
  // on cores with reciprocal estimates (which are used when unsafe-fp-math is
  // enabled for division), this functionality is redundant with the default
  // combiner logic (once the division -> reciprocal/multiply transformation
  // has taken place). As a result, this matters more for older cores than for
  // newer ones.

  // Combine multiple FDIVs with the same divisor into multiple FMULs by the
  // reciprocal if there are two or more FDIVs (for embedded cores with only
  // one FP pipeline) for three or more FDIVs (for generic OOO cores).
  switch (Subtarget.getCPUDirective()) {
  default:
    return 3;
  case PPC::DIR_440:
  case PPC::DIR_A2:
  case PPC::DIR_E500:
  case PPC::DIR_E500mc:
  case PPC::DIR_E5500:
    return 2;
  }
}

// isConsecutiveLSLoc needs to work even if all adds have not yet been
// collapsed, and so we need to look through chains of them.
static void getBaseWithConstantOffset(SDValue Loc, SDValue &Base,
                                     int64_t& Offset, SelectionDAG &DAG) {
  if (DAG.isBaseWithConstantOffset(Loc)) {
    Base = Loc.getOperand(0);
    Offset += cast<ConstantSDNode>(Loc.getOperand(1))->getSExtValue();

    // The base might itself be a base plus an offset, and if so, accumulate
    // that as well.
    getBaseWithConstantOffset(Loc.getOperand(0), Base, Offset, DAG);
  }
}

static bool isConsecutiveLSLoc(SDValue Loc, EVT VT, LSBaseSDNode *Base,
                            unsigned Bytes, int Dist,
                            SelectionDAG &DAG) {
  if (VT.getSizeInBits() / 8 != Bytes)
    return false;

  SDValue BaseLoc = Base->getBasePtr();
  if (Loc.getOpcode() == ISD::FrameIndex) {
    if (BaseLoc.getOpcode() != ISD::FrameIndex)
      return false;
    const MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    int FI  = cast<FrameIndexSDNode>(Loc)->getIndex();
    int BFI = cast<FrameIndexSDNode>(BaseLoc)->getIndex();
    int FS  = MFI.getObjectSize(FI);
    int BFS = MFI.getObjectSize(BFI);
    if (FS != BFS || FS != (int)Bytes) return false;
    return MFI.getObjectOffset(FI) == (MFI.getObjectOffset(BFI) + Dist*Bytes);
  }

  SDValue Base1 = Loc, Base2 = BaseLoc;
  int64_t Offset1 = 0, Offset2 = 0;
  getBaseWithConstantOffset(Loc, Base1, Offset1, DAG);
  getBaseWithConstantOffset(BaseLoc, Base2, Offset2, DAG);
  if (Base1 == Base2 && Offset1 == (Offset2 + Dist * Bytes))
    return true;

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const GlobalValue *GV1 = nullptr;
  const GlobalValue *GV2 = nullptr;
  Offset1 = 0;
  Offset2 = 0;
  bool isGA1 = TLI.isGAPlusOffset(Loc.getNode(), GV1, Offset1);
  bool isGA2 = TLI.isGAPlusOffset(BaseLoc.getNode(), GV2, Offset2);
  if (isGA1 && isGA2 && GV1 == GV2)
    return Offset1 == (Offset2 + Dist*Bytes);
  return false;
}

// Like SelectionDAG::isConsecutiveLoad, but also works for stores, and does
// not enforce equality of the chain operands.
static bool isConsecutiveLS(SDNode *N, LSBaseSDNode *Base,
                            unsigned Bytes, int Dist,
                            SelectionDAG &DAG) {
  if (LSBaseSDNode *LS = dyn_cast<LSBaseSDNode>(N)) {
    EVT VT = LS->getMemoryVT();
    SDValue Loc = LS->getBasePtr();
    return isConsecutiveLSLoc(Loc, VT, Base, Bytes, Dist, DAG);
  }

  if (N->getOpcode() == ISD::INTRINSIC_W_CHAIN) {
    EVT VT;
    switch (N->getConstantOperandVal(1)) {
    default: return false;
    case Intrinsic::ppc_altivec_lvx:
    case Intrinsic::ppc_altivec_lvxl:
    case Intrinsic::ppc_vsx_lxvw4x:
    case Intrinsic::ppc_vsx_lxvw4x_be:
      VT = MVT::v4i32;
      break;
    case Intrinsic::ppc_vsx_lxvd2x:
    case Intrinsic::ppc_vsx_lxvd2x_be:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_altivec_lvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_lvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_lvewx:
      VT = MVT::i32;
      break;
    }

    return isConsecutiveLSLoc(N->getOperand(2), VT, Base, Bytes, Dist, DAG);
  }

  if (N->getOpcode() == ISD::INTRINSIC_VOID) {
    EVT VT;
    switch (N->getConstantOperandVal(1)) {
    default: return false;
    case Intrinsic::ppc_altivec_stvx:
    case Intrinsic::ppc_altivec_stvxl:
    case Intrinsic::ppc_vsx_stxvw4x:
      VT = MVT::v4i32;
      break;
    case Intrinsic::ppc_vsx_stxvd2x:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_vsx_stxvw4x_be:
      VT = MVT::v4i32;
      break;
    case Intrinsic::ppc_vsx_stxvd2x_be:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_altivec_stvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_stvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_stvewx:
      VT = MVT::i32;
      break;
    }

    return isConsecutiveLSLoc(N->getOperand(3), VT, Base, Bytes, Dist, DAG);
  }

  return false;
}

// Return true is there is a nearyby consecutive load to the one provided
// (regardless of alignment). We search up and down the chain, looking though
// token factors and other loads (but nothing else). As a result, a true result
// indicates that it is safe to create a new consecutive load adjacent to the
// load provided.
static bool findConsecutiveLoad(LoadSDNode *LD, SelectionDAG &DAG) {
  SDValue Chain = LD->getChain();
  EVT VT = LD->getMemoryVT();

  SmallSet<SDNode *, 16> LoadRoots;
  SmallVector<SDNode *, 8> Queue(1, Chain.getNode());
  SmallSet<SDNode *, 16> Visited;

  // First, search up the chain, branching to follow all token-factor operands.
  // If we find a consecutive load, then we're done, otherwise, record all
  // nodes just above the top-level loads and token factors.
  while (!Queue.empty()) {
    SDNode *ChainNext = Queue.pop_back_val();
    if (!Visited.insert(ChainNext).second)
      continue;

    if (MemSDNode *ChainLD = dyn_cast<MemSDNode>(ChainNext)) {
      if (isConsecutiveLS(ChainLD, LD, VT.getStoreSize(), 1, DAG))
        return true;

      if (!Visited.count(ChainLD->getChain().getNode()))
        Queue.push_back(ChainLD->getChain().getNode());
    } else if (ChainNext->getOpcode() == ISD::TokenFactor) {
      for (const SDUse &O : ChainNext->ops())
        if (!Visited.count(O.getNode()))
          Queue.push_back(O.getNode());
    } else
      LoadRoots.insert(ChainNext);
  }

  // Second, search down the chain, starting from the top-level nodes recorded
  // in the first phase. These top-level nodes are the nodes just above all
  // loads and token factors. Starting with their uses, recursively look though
  // all loads (just the chain uses) and token factors to find a consecutive
  // load.
  Visited.clear();
  Queue.clear();

  for (SDNode *I : LoadRoots) {
    Queue.push_back(I);

    while (!Queue.empty()) {
      SDNode *LoadRoot = Queue.pop_back_val();
      if (!Visited.insert(LoadRoot).second)
        continue;

      if (MemSDNode *ChainLD = dyn_cast<MemSDNode>(LoadRoot))
        if (isConsecutiveLS(ChainLD, LD, VT.getStoreSize(), 1, DAG))
          return true;

      for (SDNode *U : LoadRoot->uses())
        if (((isa<MemSDNode>(U) &&
              cast<MemSDNode>(U)->getChain().getNode() == LoadRoot) ||
             U->getOpcode() == ISD::TokenFactor) &&
            !Visited.count(U))
          Queue.push_back(U);
    }
  }

  return false;
}

/// This function is called when we have proved that a SETCC node can be replaced
/// by subtraction (and other supporting instructions) so that the result of
/// comparison is kept in a GPR instead of CR. This function is purely for
/// codegen purposes and has some flags to guide the codegen process.
static SDValue generateEquivalentSub(SDNode *N, int Size, bool Complement,
                                     bool Swap, SDLoc &DL, SelectionDAG &DAG) {
  assert(N->getOpcode() == ISD::SETCC && "ISD::SETCC Expected.");

  // Zero extend the operands to the largest legal integer. Originally, they
  // must be of a strictly smaller size.
  auto Op0 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N->getOperand(0),
                         DAG.getConstant(Size, DL, MVT::i32));
  auto Op1 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N->getOperand(1),
                         DAG.getConstant(Size, DL, MVT::i32));

  // Swap if needed. Depends on the condition code.
  if (Swap)
    std::swap(Op0, Op1);

  // Subtract extended integers.
  auto SubNode = DAG.getNode(ISD::SUB, DL, MVT::i64, Op0, Op1);

  // Move the sign bit to the least significant position and zero out the rest.
  // Now the least significant bit carries the result of original comparison.
  auto Shifted = DAG.getNode(ISD::SRL, DL, MVT::i64, SubNode,
                             DAG.getConstant(Size - 1, DL, MVT::i32));
  auto Final = Shifted;

  // Complement the result if needed. Based on the condition code.
  if (Complement)
    Final = DAG.getNode(ISD::XOR, DL, MVT::i64, Shifted,
                        DAG.getConstant(1, DL, MVT::i64));

  return DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Final);
}

SDValue PPCTargetLowering::ConvertSETCCToSubtract(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::SETCC && "ISD::SETCC Expected.");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // Size of integers being compared has a critical role in the following
  // analysis, so we prefer to do this when all types are legal.
  if (!DCI.isAfterLegalizeDAG())
    return SDValue();

  // If all users of SETCC extend its value to a legal integer type
  // then we replace SETCC with a subtraction
  for (const SDNode *U : N->uses())
    if (U->getOpcode() != ISD::ZERO_EXTEND)
      return SDValue();

  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  auto OpSize = N->getOperand(0).getValueSizeInBits();

  unsigned Size = DAG.getDataLayout().getLargestLegalIntTypeSizeInBits();

  if (OpSize < Size) {
    switch (CC) {
    default: break;
    case ISD::SETULT:
      return generateEquivalentSub(N, Size, false, false, DL, DAG);
    case ISD::SETULE:
      return generateEquivalentSub(N, Size, true, true, DL, DAG);
    case ISD::SETUGT:
      return generateEquivalentSub(N, Size, false, true, DL, DAG);
    case ISD::SETUGE:
      return generateEquivalentSub(N, Size, true, false, DL, DAG);
    }
  }

  return SDValue();
}

SDValue PPCTargetLowering::DAGCombineTruncBoolExt(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  assert(Subtarget.useCRBits() && "Expecting to be tracking CR bits");
  // If we're tracking CR bits, we need to be careful that we don't have:
  //   trunc(binary-ops(zext(x), zext(y)))
  // or
  //   trunc(binary-ops(binary-ops(zext(x), zext(y)), ...)
  // such that we're unnecessarily moving things into GPRs when it would be
  // better to keep them in CR bits.

  // Note that trunc here can be an actual i1 trunc, or can be the effective
  // truncation that comes from a setcc or select_cc.
  if (N->getOpcode() == ISD::TRUNCATE &&
      N->getValueType(0) != MVT::i1)
    return SDValue();

  if (N->getOperand(0).getValueType() != MVT::i32 &&
      N->getOperand(0).getValueType() != MVT::i64)
    return SDValue();

  if (N->getOpcode() == ISD::SETCC ||
      N->getOpcode() == ISD::SELECT_CC) {
    // If we're looking at a comparison, then we need to make sure that the
    // high bits (all except for the first) don't matter the result.
    ISD::CondCode CC =
      cast<CondCodeSDNode>(N->getOperand(
        N->getOpcode() == ISD::SETCC ? 2 : 4))->get();
    unsigned OpBits = N->getOperand(0).getValueSizeInBits();

    if (ISD::isSignedIntSetCC(CC)) {
      if (DAG.ComputeNumSignBits(N->getOperand(0)) != OpBits ||
          DAG.ComputeNumSignBits(N->getOperand(1)) != OpBits)
        return SDValue();
    } else if (ISD::isUnsignedIntSetCC(CC)) {
      if (!DAG.MaskedValueIsZero(N->getOperand(0),
                                 APInt::getHighBitsSet(OpBits, OpBits-1)) ||
          !DAG.MaskedValueIsZero(N->getOperand(1),
                                 APInt::getHighBitsSet(OpBits, OpBits-1)))
        return (N->getOpcode() == ISD::SETCC ? ConvertSETCCToSubtract(N, DCI)
                                             : SDValue());
    } else {
      // This is neither a signed nor an unsigned comparison, just make sure
      // that the high bits are equal.
      KnownBits Op1Known = DAG.computeKnownBits(N->getOperand(0));
      KnownBits Op2Known = DAG.computeKnownBits(N->getOperand(1));

      // We don't really care about what is known about the first bit (if
      // anything), so pretend that it is known zero for both to ensure they can
      // be compared as constants.
      Op1Known.Zero.setBit(0); Op1Known.One.clearBit(0);
      Op2Known.Zero.setBit(0); Op2Known.One.clearBit(0);

      if (!Op1Known.isConstant() || !Op2Known.isConstant() ||
          Op1Known.getConstant() != Op2Known.getConstant())
        return SDValue();
    }
  }

  // We now know that the higher-order bits are irrelevant, we just need to
  // make sure that all of the intermediate operations are bit operations, and
  // all inputs are extensions.
  if (N->getOperand(0).getOpcode() != ISD::AND &&
      N->getOperand(0).getOpcode() != ISD::OR  &&
      N->getOperand(0).getOpcode() != ISD::XOR &&
      N->getOperand(0).getOpcode() != ISD::SELECT &&
      N->getOperand(0).getOpcode() != ISD::SELECT_CC &&
      N->getOperand(0).getOpcode() != ISD::TRUNCATE &&
      N->getOperand(0).getOpcode() != ISD::SIGN_EXTEND &&
      N->getOperand(0).getOpcode() != ISD::ZERO_EXTEND &&
      N->getOperand(0).getOpcode() != ISD::ANY_EXTEND)
    return SDValue();

  if ((N->getOpcode() == ISD::SETCC || N->getOpcode() == ISD::SELECT_CC) &&
      N->getOperand(1).getOpcode() != ISD::AND &&
      N->getOperand(1).getOpcode() != ISD::OR  &&
      N->getOperand(1).getOpcode() != ISD::XOR &&
      N->getOperand(1).getOpcode() != ISD::SELECT &&
      N->getOperand(1).getOpcode() != ISD::SELECT_CC &&
      N->getOperand(1).getOpcode() != ISD::TRUNCATE &&
      N->getOperand(1).getOpcode() != ISD::SIGN_EXTEND &&
      N->getOperand(1).getOpcode() != ISD::ZERO_EXTEND &&
      N->getOperand(1).getOpcode() != ISD::ANY_EXTEND)
    return SDValue();

  SmallVector<SDValue, 4> Inputs;
  SmallVector<SDValue, 8> BinOps, PromOps;
  SmallPtrSet<SDNode *, 16> Visited;

  for (unsigned i = 0; i < 2; ++i) {
    if (((N->getOperand(i).getOpcode() == ISD::SIGN_EXTEND ||
          N->getOperand(i).getOpcode() == ISD::ZERO_EXTEND ||
          N->getOperand(i).getOpcode() == ISD::ANY_EXTEND) &&
          N->getOperand(i).getOperand(0).getValueType() == MVT::i1) ||
        isa<ConstantSDNode>(N->getOperand(i)))
      Inputs.push_back(N->getOperand(i));
    else
      BinOps.push_back(N->getOperand(i));

    if (N->getOpcode() == ISD::TRUNCATE)
      break;
  }

  // Visit all inputs, collect all binary operations (and, or, xor and
  // select) that are all fed by extensions.
  while (!BinOps.empty()) {
    SDValue BinOp = BinOps.pop_back_val();

    if (!Visited.insert(BinOp.getNode()).second)
      continue;

    PromOps.push_back(BinOp);

    for (unsigned i = 0, ie = BinOp.getNumOperands(); i != ie; ++i) {
      // The condition of the select is not promoted.
      if (BinOp.getOpcode() == ISD::SELECT && i == 0)
        continue;
      if (BinOp.getOpcode() == ISD::SELECT_CC && i != 2 && i != 3)
        continue;

      if (((BinOp.getOperand(i).getOpcode() == ISD::SIGN_EXTEND ||
            BinOp.getOperand(i).getOpcode() == ISD::ZERO_EXTEND ||
            BinOp.getOperand(i).getOpcode() == ISD::ANY_EXTEND) &&
           BinOp.getOperand(i).getOperand(0).getValueType() == MVT::i1) ||
          isa<ConstantSDNode>(BinOp.getOperand(i))) {
        Inputs.push_back(BinOp.getOperand(i));
      } else if (BinOp.getOperand(i).getOpcode() == ISD::AND ||
                 BinOp.getOperand(i).getOpcode() == ISD::OR  ||
                 BinOp.getOperand(i).getOpcode() == ISD::XOR ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT_CC ||
                 BinOp.getOperand(i).getOpcode() == ISD::TRUNCATE ||
                 BinOp.getOperand(i).getOpcode() == ISD::SIGN_EXTEND ||
                 BinOp.getOperand(i).getOpcode() == ISD::ZERO_EXTEND ||
                 BinOp.getOperand(i).getOpcode() == ISD::ANY_EXTEND) {
        BinOps.push_back(BinOp.getOperand(i));
      } else {
        // We have an input that is not an extension or another binary
        // operation; we'll abort this transformation.
        return SDValue();
      }
    }
  }

  // Make sure that this is a self-contained cluster of operations (which
  // is not quite the same thing as saying that everything has only one
  // use).
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;

    for (const SDNode *User : Inputs[i].getNode()->uses()) {
      if (User != N && !Visited.count(User))
        return SDValue();

      // Make sure that we're not going to promote the non-output-value
      // operand(s) or SELECT or SELECT_CC.
      // FIXME: Although we could sometimes handle this, and it does occur in
      // practice that one of the condition inputs to the select is also one of
      // the outputs, we currently can't deal with this.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == Inputs[i])
          return SDValue();
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == Inputs[i] ||
            User->getOperand(1) == Inputs[i])
          return SDValue();
      }
    }
  }

  for (unsigned i = 0, ie = PromOps.size(); i != ie; ++i) {
    for (const SDNode *User : PromOps[i].getNode()->uses()) {
      if (User != N && !Visited.count(User))
        return SDValue();

      // Make sure that we're not going to promote the non-output-value
      // operand(s) or SELECT or SELECT_CC.
      // FIXME: Although we could sometimes handle this, and it does occur in
      // practice that one of the condition inputs to the select is also one of
      // the outputs, we currently can't deal with this.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == PromOps[i])
          return SDValue();
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == PromOps[i] ||
            User->getOperand(1) == PromOps[i])
          return SDValue();
      }
    }
  }

  // Replace all inputs with the extension operand.
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    // Constants may have users outside the cluster of to-be-promoted nodes,
    // and so we need to replace those as we do the promotions.
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;
    else
      DAG.ReplaceAllUsesOfValueWith(Inputs[i], Inputs[i].getOperand(0));
  }

  std::list<HandleSDNode> PromOpHandles;
  for (auto &PromOp : PromOps)
    PromOpHandles.emplace_back(PromOp);

  // Replace all operations (these are all the same, but have a different
  // (i1) return type). DAG.getNode will validate that the types of
  // a binary operator match, so go through the list in reverse so that
  // we've likely promoted both operands first. Any intermediate truncations or
  // extensions disappear.
  while (!PromOpHandles.empty()) {
    SDValue PromOp = PromOpHandles.back().getValue();
    PromOpHandles.pop_back();

    if (PromOp.getOpcode() == ISD::TRUNCATE ||
        PromOp.getOpcode() == ISD::SIGN_EXTEND ||
        PromOp.getOpcode() == ISD::ZERO_EXTEND ||
        PromOp.getOpcode() == ISD::ANY_EXTEND) {
      if (!isa<ConstantSDNode>(PromOp.getOperand(0)) &&
          PromOp.getOperand(0).getValueType() != MVT::i1) {
        // The operand is not yet ready (see comment below).
        PromOpHandles.emplace_front(PromOp);
        continue;
      }

      SDValue RepValue = PromOp.getOperand(0);
      if (isa<ConstantSDNode>(RepValue))
        RepValue = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, RepValue);

      DAG.ReplaceAllUsesOfValueWith(PromOp, RepValue);
      continue;
    }

    unsigned C;
    switch (PromOp.getOpcode()) {
    default:             C = 0; break;
    case ISD::SELECT:    C = 1; break;
    case ISD::SELECT_CC: C = 2; break;
    }

    if ((!isa<ConstantSDNode>(PromOp.getOperand(C)) &&
         PromOp.getOperand(C).getValueType() != MVT::i1) ||
        (!isa<ConstantSDNode>(PromOp.getOperand(C+1)) &&
         PromOp.getOperand(C+1).getValueType() != MVT::i1)) {
      // The to-be-promoted operands of this node have not yet been
      // promoted (this should be rare because we're going through the
      // list backward, but if one of the operands has several users in
      // this cluster of to-be-promoted nodes, it is possible).
      PromOpHandles.emplace_front(PromOp);
      continue;
    }

    SmallVector<SDValue, 3> Ops(PromOp.getNode()->op_begin(),
                                PromOp.getNode()->op_end());

    // If there are any constant inputs, make sure they're replaced now.
    for (unsigned i = 0; i < 2; ++i)
      if (isa<ConstantSDNode>(Ops[C+i]))
        Ops[C+i] = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, Ops[C+i]);

    DAG.ReplaceAllUsesOfValueWith(PromOp,
      DAG.getNode(PromOp.getOpcode(), dl, MVT::i1, Ops));
  }

  // Now we're left with the initial truncation itself.
  if (N->getOpcode() == ISD::TRUNCATE)
    return N->getOperand(0);

  // Otherwise, this is a comparison. The operands to be compared have just
  // changed type (to i1), but everything else is the same.
  return SDValue(N, 0);
}

SDValue PPCTargetLowering::DAGCombineExtBoolTrunc(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  // If we're tracking CR bits, we need to be careful that we don't have:
  //   zext(binary-ops(trunc(x), trunc(y)))
  // or
  //   zext(binary-ops(binary-ops(trunc(x), trunc(y)), ...)
  // such that we're unnecessarily moving things into CR bits that can more
  // efficiently stay in GPRs. Note that if we're not certain that the high
  // bits are set as required by the final extension, we still may need to do
  // some masking to get the proper behavior.

  // This same functionality is important on PPC64 when dealing with
  // 32-to-64-bit extensions; these occur often when 32-bit values are used as
  // the return values of functions. Because it is so similar, it is handled
  // here as well.

  if (N->getValueType(0) != MVT::i32 &&
      N->getValueType(0) != MVT::i64)
    return SDValue();

  if (!((N->getOperand(0).getValueType() == MVT::i1 && Subtarget.useCRBits()) ||
        (N->getOperand(0).getValueType() == MVT::i32 && Subtarget.isPPC64())))
    return SDValue();

  if (N->getOperand(0).getOpcode() != ISD::AND &&
      N->getOperand(0).getOpcode() != ISD::OR  &&
      N->getOperand(0).getOpcode() != ISD::XOR &&
      N->getOperand(0).getOpcode() != ISD::SELECT &&
      N->getOperand(0).getOpcode() != ISD::SELECT_CC)
    return SDValue();

  SmallVector<SDValue, 4> Inputs;
  SmallVector<SDValue, 8> BinOps(1, N->getOperand(0)), PromOps;
  SmallPtrSet<SDNode *, 16> Visited;

  // Visit all inputs, collect all binary operations (and, or, xor and
  // select) that are all fed by truncations.
  while (!BinOps.empty()) {
    SDValue BinOp = BinOps.pop_back_val();

    if (!Visited.insert(BinOp.getNode()).second)
      continue;

    PromOps.push_back(BinOp);

    for (unsigned i = 0, ie = BinOp.getNumOperands(); i != ie; ++i) {
      // The condition of the select is not promoted.
      if (BinOp.getOpcode() == ISD::SELECT && i == 0)
        continue;
      if (BinOp.getOpcode() == ISD::SELECT_CC && i != 2 && i != 3)
        continue;

      if (BinOp.getOperand(i).getOpcode() == ISD::TRUNCATE ||
          isa<ConstantSDNode>(BinOp.getOperand(i))) {
        Inputs.push_back(BinOp.getOperand(i));
      } else if (BinOp.getOperand(i).getOpcode() == ISD::AND ||
                 BinOp.getOperand(i).getOpcode() == ISD::OR  ||
                 BinOp.getOperand(i).getOpcode() == ISD::XOR ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT_CC) {
        BinOps.push_back(BinOp.getOperand(i));
      } else {
        // We have an input that is not a truncation or another binary
        // operation; we'll abort this transformation.
        return SDValue();
      }
    }
  }

  // The operands of a select that must be truncated when the select is
  // promoted because the operand is actually part of the to-be-promoted set.
  DenseMap<SDNode *, EVT> SelectTruncOp[2];

  // Make sure that this is a self-contained cluster of operations (which
  // is not quite the same thing as saying that everything has only one
  // use).
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;

    for (SDNode *User : Inputs[i].getNode()->uses()) {
      if (User != N && !Visited.count(User))
        return SDValue();

      // If we're going to promote the non-output-value operand(s) or SELECT or
      // SELECT_CC, record them for truncation.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == Inputs[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == Inputs[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
        if (User->getOperand(1) == Inputs[i])
          SelectTruncOp[1].insert(std::make_pair(User,
                                    User->getOperand(1).getValueType()));
      }
    }
  }

  for (unsigned i = 0, ie = PromOps.size(); i != ie; ++i) {
    for (SDNode *User : PromOps[i].getNode()->uses()) {
      if (User != N && !Visited.count(User))
        return SDValue();

      // If we're going to promote the non-output-value operand(s) or SELECT or
      // SELECT_CC, record them for truncation.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == PromOps[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == PromOps[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
        if (User->getOperand(1) == PromOps[i])
          SelectTruncOp[1].insert(std::make_pair(User,
                                    User->getOperand(1).getValueType()));
      }
    }
  }

  unsigned PromBits = N->getOperand(0).getValueSizeInBits();
  bool ReallyNeedsExt = false;
  if (N->getOpcode() != ISD::ANY_EXTEND) {
    // If all of the inputs are not already sign/zero extended, then
    // we'll still need to do that at the end.
    for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
      if (isa<ConstantSDNode>(Inputs[i]))
        continue;

      unsigned OpBits =
        Inputs[i].getOperand(0).getValueSizeInBits();
      assert(PromBits < OpBits && "Truncation not to a smaller bit count?");

      if ((N->getOpcode() == ISD::ZERO_EXTEND &&
           !DAG.MaskedValueIsZero(Inputs[i].getOperand(0),
                                  APInt::getHighBitsSet(OpBits,
                                                        OpBits-PromBits))) ||
          (N->getOpcode() == ISD::SIGN_EXTEND &&
           DAG.ComputeNumSignBits(Inputs[i].getOperand(0)) <
             (OpBits-(PromBits-1)))) {
        ReallyNeedsExt = true;
        break;
      }
    }
  }

  // Replace all inputs, either with the truncation operand, or a
  // truncation or extension to the final output type.
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    // Constant inputs need to be replaced with the to-be-promoted nodes that
    // use them because they might have users outside of the cluster of
    // promoted nodes.
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;

    SDValue InSrc = Inputs[i].getOperand(0);
    if (Inputs[i].getValueType() == N->getValueType(0))
      DAG.ReplaceAllUsesOfValueWith(Inputs[i], InSrc);
    else if (N->getOpcode() == ISD::SIGN_EXTEND)
      DAG.ReplaceAllUsesOfValueWith(Inputs[i],
        DAG.getSExtOrTrunc(InSrc, dl, N->getValueType(0)));
    else if (N->getOpcode() == ISD::ZERO_EXTEND)
      DAG.ReplaceAllUsesOfValueWith(Inputs[i],
        DAG.getZExtOrTrunc(InSrc, dl, N->getValueType(0)));
    else
      DAG.ReplaceAllUsesOfValueWith(Inputs[i],
        DAG.getAnyExtOrTrunc(InSrc, dl, N->getValueType(0)));
  }

  std::list<HandleSDNode> PromOpHandles;
  for (auto &PromOp : PromOps)
    PromOpHandles.emplace_back(PromOp);

  // Replace all operations (these are all the same, but have a different
  // (promoted) return type). DAG.getNode will validate that the types of
  // a binary operator match, so go through the list in reverse so that
  // we've likely promoted both operands first.
  while (!PromOpHandles.empty()) {
    SDValue PromOp = PromOpHandles.back().getValue();
    PromOpHandles.pop_back();

    unsigned C;
    switch (PromOp.getOpcode()) {
    default:             C = 0; break;
    case ISD::SELECT:    C = 1; break;
    case ISD::SELECT_CC: C = 2; break;
    }

    if ((!isa<ConstantSDNode>(PromOp.getOperand(C)) &&
         PromOp.getOperand(C).getValueType() != N->getValueType(0)) ||
        (!isa<ConstantSDNode>(PromOp.getOperand(C+1)) &&
         PromOp.getOperand(C+1).getValueType() != N->getValueType(0))) {
      // The to-be-promoted operands of this node have not yet been
      // promoted (this should be rare because we're going through the
      // list backward, but if one of the operands has several users in
      // this cluster of to-be-promoted nodes, it is possible).
      PromOpHandles.emplace_front(PromOp);
      continue;
    }

    // For SELECT and SELECT_CC nodes, we do a similar check for any
    // to-be-promoted comparison inputs.
    if (PromOp.getOpcode() == ISD::SELECT ||
        PromOp.getOpcode() == ISD::SELECT_CC) {
      if ((SelectTruncOp[0].count(PromOp.getNode()) &&
           PromOp.getOperand(0).getValueType() != N->getValueType(0)) ||
          (SelectTruncOp[1].count(PromOp.getNode()) &&
           PromOp.getOperand(1).getValueType() != N->getValueType(0))) {
        PromOpHandles.emplace_front(PromOp);
        continue;
      }
    }

    SmallVector<SDValue, 3> Ops(PromOp.getNode()->op_begin(),
                                PromOp.getNode()->op_end());

    // If this node has constant inputs, then they'll need to be promoted here.
    for (unsigned i = 0; i < 2; ++i) {
      if (!isa<ConstantSDNode>(Ops[C+i]))
        continue;
      if (Ops[C+i].getValueType() == N->getValueType(0))
        continue;

      if (N->getOpcode() == ISD::SIGN_EXTEND)
        Ops[C+i] = DAG.getSExtOrTrunc(Ops[C+i], dl, N->getValueType(0));
      else if (N->getOpcode() == ISD::ZERO_EXTEND)
        Ops[C+i] = DAG.getZExtOrTrunc(Ops[C+i], dl, N->getValueType(0));
      else
        Ops[C+i] = DAG.getAnyExtOrTrunc(Ops[C+i], dl, N->getValueType(0));
    }

    // If we've promoted the comparison inputs of a SELECT or SELECT_CC,
    // truncate them again to the original value type.
    if (PromOp.getOpcode() == ISD::SELECT ||
        PromOp.getOpcode() == ISD::SELECT_CC) {
      auto SI0 = SelectTruncOp[0].find(PromOp.getNode());
      if (SI0 != SelectTruncOp[0].end())
        Ops[0] = DAG.getNode(ISD::TRUNCATE, dl, SI0->second, Ops[0]);
      auto SI1 = SelectTruncOp[1].find(PromOp.getNode());
      if (SI1 != SelectTruncOp[1].end())
        Ops[1] = DAG.getNode(ISD::TRUNCATE, dl, SI1->second, Ops[1]);
    }

    DAG.ReplaceAllUsesOfValueWith(PromOp,
      DAG.getNode(PromOp.getOpcode(), dl, N->getValueType(0), Ops));
  }

  // Now we're left with the initial extension itself.
  if (!ReallyNeedsExt)
    return N->getOperand(0);

  // To zero extend, just mask off everything except for the first bit (in the
  // i1 case).
  if (N->getOpcode() == ISD::ZERO_EXTEND)
    return DAG.getNode(ISD::AND, dl, N->getValueType(0), N->getOperand(0),
                       DAG.getConstant(APInt::getLowBitsSet(
                                         N->getValueSizeInBits(0), PromBits),
                                       dl, N->getValueType(0)));

  assert(N->getOpcode() == ISD::SIGN_EXTEND &&
         "Invalid extension type");
  EVT ShiftAmountTy = getShiftAmountTy(N->getValueType(0), DAG.getDataLayout());
  SDValue ShiftCst =
      DAG.getConstant(N->getValueSizeInBits(0) - PromBits, dl, ShiftAmountTy);
  return DAG.getNode(
      ISD::SRA, dl, N->getValueType(0),
      DAG.getNode(ISD::SHL, dl, N->getValueType(0), N->getOperand(0), ShiftCst),
      ShiftCst);
}

SDValue PPCTargetLowering::combineSetCC(SDNode *N,
                                        DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::SETCC &&
         "Should be called with a SETCC node");

  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  if (CC == ISD::SETNE || CC == ISD::SETEQ) {
    SDValue LHS = N->getOperand(0);
    SDValue RHS = N->getOperand(1);

    // If there is a '0 - y' pattern, canonicalize the pattern to the RHS.
    if (LHS.getOpcode() == ISD::SUB && isNullConstant(LHS.getOperand(0)) &&
        LHS.hasOneUse())
      std::swap(LHS, RHS);

    // x == 0-y --> x+y == 0
    // x != 0-y --> x+y != 0
    if (RHS.getOpcode() == ISD::SUB && isNullConstant(RHS.getOperand(0)) &&
        RHS.hasOneUse()) {
      SDLoc DL(N);
      SelectionDAG &DAG = DCI.DAG;
      EVT VT = N->getValueType(0);
      EVT OpVT = LHS.getValueType();
      SDValue Add = DAG.getNode(ISD::ADD, DL, OpVT, LHS, RHS.getOperand(1));
      return DAG.getSetCC(DL, VT, Add, DAG.getConstant(0, DL, OpVT), CC);
    }
  }

  return DAGCombineTruncBoolExt(N, DCI);
}

// Is this an extending load from an f32 to an f64?
static bool isFPExtLoad(SDValue Op) {
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(Op.getNode()))
    return LD->getExtensionType() == ISD::EXTLOAD &&
      Op.getValueType() == MVT::f64;
  return false;
}

/// Reduces the number of fp-to-int conversion when building a vector.
///
/// If this vector is built out of floating to integer conversions,
/// transform it to a vector built out of floating point values followed by a
/// single floating to integer conversion of the vector.
/// Namely  (build_vector (fptosi $A), (fptosi $B), ...)
/// becomes (fptosi (build_vector ($A, $B, ...)))
SDValue PPCTargetLowering::
combineElementTruncationToVectorTruncation(SDNode *N,
                                           DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::BUILD_VECTOR &&
         "Should be called with a BUILD_VECTOR node");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  SDValue FirstInput = N->getOperand(0);
  assert(FirstInput.getOpcode() == PPCISD::MFVSR &&
         "The input operand must be an fp-to-int conversion.");

  // This combine happens after legalization so the fp_to_[su]i nodes are
  // already converted to PPCSISD nodes.
  unsigned FirstConversion = FirstInput.getOperand(0).getOpcode();
  if (FirstConversion == PPCISD::FCTIDZ ||
      FirstConversion == PPCISD::FCTIDUZ ||
      FirstConversion == PPCISD::FCTIWZ ||
      FirstConversion == PPCISD::FCTIWUZ) {
    bool IsSplat = true;
    bool Is32Bit = FirstConversion == PPCISD::FCTIWZ ||
      FirstConversion == PPCISD::FCTIWUZ;
    EVT SrcVT = FirstInput.getOperand(0).getValueType();
    SmallVector<SDValue, 4> Ops;
    EVT TargetVT = N->getValueType(0);
    for (int i = 0, e = N->getNumOperands(); i < e; ++i) {
      SDValue NextOp = N->getOperand(i);
      if (NextOp.getOpcode() != PPCISD::MFVSR)
        return SDValue();
      unsigned NextConversion = NextOp.getOperand(0).getOpcode();
      if (NextConversion != FirstConversion)
        return SDValue();
      // If we are converting to 32-bit integers, we need to add an FP_ROUND.
      // This is not valid if the input was originally double precision. It is
      // also not profitable to do unless this is an extending load in which
      // case doing this combine will allow us to combine consecutive loads.
      if (Is32Bit && !isFPExtLoad(NextOp.getOperand(0).getOperand(0)))
        return SDValue();
      if (N->getOperand(i) != FirstInput)
        IsSplat = false;
    }

    // If this is a splat, we leave it as-is since there will be only a single
    // fp-to-int conversion followed by a splat of the integer. This is better
    // for 32-bit and smaller ints and neutral for 64-bit ints.
    if (IsSplat)
      return SDValue();

    // Now that we know we have the right type of node, get its operands
    for (int i = 0, e = N->getNumOperands(); i < e; ++i) {
      SDValue In = N->getOperand(i).getOperand(0);
      if (Is32Bit) {
        // For 32-bit values, we need to add an FP_ROUND node (if we made it
        // here, we know that all inputs are extending loads so this is safe).
        if (In.isUndef())
          Ops.push_back(DAG.getUNDEF(SrcVT));
        else {
          SDValue Trunc =
              DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, In.getOperand(0),
                          DAG.getIntPtrConstant(1, dl, /*isTarget=*/true));
          Ops.push_back(Trunc);
        }
      } else
        Ops.push_back(In.isUndef() ? DAG.getUNDEF(SrcVT) : In.getOperand(0));
    }

    unsigned Opcode;
    if (FirstConversion == PPCISD::FCTIDZ ||
        FirstConversion == PPCISD::FCTIWZ)
      Opcode = ISD::FP_TO_SINT;
    else
      Opcode = ISD::FP_TO_UINT;

    EVT NewVT = TargetVT == MVT::v2i64 ? MVT::v2f64 : MVT::v4f32;
    SDValue BV = DAG.getBuildVector(NewVT, dl, Ops);
    return DAG.getNode(Opcode, dl, TargetVT, BV);
  }
  return SDValue();
}

/// Reduce the number of loads when building a vector.
///
/// Building a vector out of multiple loads can be converted to a load
/// of the vector type if the loads are consecutive. If the loads are
/// consecutive but in descending order, a shuffle is added at the end
/// to reorder the vector.
static SDValue combineBVOfConsecutiveLoads(SDNode *N, SelectionDAG &DAG) {
  assert(N->getOpcode() == ISD::BUILD_VECTOR &&
         "Should be called with a BUILD_VECTOR node");

  SDLoc dl(N);

  // Return early for non byte-sized type, as they can't be consecutive.
  if (!N->getValueType(0).getVectorElementType().isByteSized())
    return SDValue();

  bool InputsAreConsecutiveLoads = true;
  bool InputsAreReverseConsecutive = true;
  unsigned ElemSize = N->getValueType(0).getScalarType().getStoreSize();
  SDValue FirstInput = N->getOperand(0);
  bool IsRoundOfExtLoad = false;
  LoadSDNode *FirstLoad = nullptr;

  if (FirstInput.getOpcode() == ISD::FP_ROUND &&
      FirstInput.getOperand(0).getOpcode() == ISD::LOAD) {
    FirstLoad = cast<LoadSDNode>(FirstInput.getOperand(0));
    IsRoundOfExtLoad = FirstLoad->getExtensionType() == ISD::EXTLOAD;
  }
  // Not a build vector of (possibly fp_rounded) loads.
  if ((!IsRoundOfExtLoad && FirstInput.getOpcode() != ISD::LOAD) ||
      N->getNumOperands() == 1)
    return SDValue();

  if (!IsRoundOfExtLoad)
    FirstLoad = cast<LoadSDNode>(FirstInput);

  SmallVector<LoadSDNode *, 4> InputLoads;
  InputLoads.push_back(FirstLoad);
  for (int i = 1, e = N->getNumOperands(); i < e; ++i) {
    // If any inputs are fp_round(extload), they all must be.
    if (IsRoundOfExtLoad && N->getOperand(i).getOpcode() != ISD::FP_ROUND)
      return SDValue();

    SDValue NextInput = IsRoundOfExtLoad ? N->getOperand(i).getOperand(0) :
      N->getOperand(i);
    if (NextInput.getOpcode() != ISD::LOAD)
      return SDValue();

    SDValue PreviousInput =
      IsRoundOfExtLoad ? N->getOperand(i-1).getOperand(0) : N->getOperand(i-1);
    LoadSDNode *LD1 = cast<LoadSDNode>(PreviousInput);
    LoadSDNode *LD2 = cast<LoadSDNode>(NextInput);

    // If any inputs are fp_round(extload), they all must be.
    if (IsRoundOfExtLoad && LD2->getExtensionType() != ISD::EXTLOAD)
      return SDValue();

    // We only care about regular loads. The PPC-specific load intrinsics
    // will not lead to a merge opportunity.
    if (!DAG.areNonVolatileConsecutiveLoads(LD2, LD1, ElemSize, 1))
      InputsAreConsecutiveLoads = false;
    if (!DAG.areNonVolatileConsecutiveLoads(LD1, LD2, ElemSize, 1))
      InputsAreReverseConsecutive = false;

    // Exit early if the loads are neither consecutive nor reverse consecutive.
    if (!InputsAreConsecutiveLoads && !InputsAreReverseConsecutive)
      return SDValue();
    InputLoads.push_back(LD2);
  }

  assert(!(InputsAreConsecutiveLoads && InputsAreReverseConsecutive) &&
         "The loads cannot be both consecutive and reverse consecutive.");

  SDValue WideLoad;
  SDValue ReturnSDVal;
  if (InputsAreConsecutiveLoads) {
    assert(FirstLoad && "Input needs to be a LoadSDNode.");
    WideLoad = DAG.getLoad(N->getValueType(0), dl, FirstLoad->getChain(),
                           FirstLoad->getBasePtr(), FirstLoad->getPointerInfo(),
                           FirstLoad->getAlign());
    ReturnSDVal = WideLoad;
  } else if (InputsAreReverseConsecutive) {
    LoadSDNode *LastLoad = InputLoads.back();
    assert(LastLoad && "Input needs to be a LoadSDNode.");
    WideLoad = DAG.getLoad(N->getValueType(0), dl, LastLoad->getChain(),
                           LastLoad->getBasePtr(), LastLoad->getPointerInfo(),
                           LastLoad->getAlign());
    SmallVector<int, 16> Ops;
    for (int i = N->getNumOperands() - 1; i >= 0; i--)
      Ops.push_back(i);

    ReturnSDVal = DAG.getVectorShuffle(N->getValueType(0), dl, WideLoad,
                                       DAG.getUNDEF(N->getValueType(0)), Ops);
  } else
    return SDValue();

  for (auto *LD : InputLoads)
    DAG.makeEquivalentMemoryOrdering(LD, WideLoad);
  return ReturnSDVal;
}

// This function adds the required vector_shuffle needed to get
// the elements of the vector extract in the correct position
// as specified by the CorrectElems encoding.
static SDValue addShuffleForVecExtend(SDNode *N, SelectionDAG &DAG,
                                      SDValue Input, uint64_t Elems,
                                      uint64_t CorrectElems) {
  SDLoc dl(N);

  unsigned NumElems = Input.getValueType().getVectorNumElements();
  SmallVector<int, 16> ShuffleMask(NumElems, -1);

  // Knowing the element indices being extracted from the original
  // vector and the order in which they're being inserted, just put
  // them at element indices required for the instruction.
  for (unsigned i = 0; i < N->getNumOperands(); i++) {
    if (DAG.getDataLayout().isLittleEndian())
      ShuffleMask[CorrectElems & 0xF] = Elems & 0xF;
    else
      ShuffleMask[(CorrectElems & 0xF0) >> 4] = (Elems & 0xF0) >> 4;
    CorrectElems = CorrectElems >> 8;
    Elems = Elems >> 8;
  }

  SDValue Shuffle =
      DAG.getVectorShuffle(Input.getValueType(), dl, Input,
                           DAG.getUNDEF(Input.getValueType()), ShuffleMask);

  EVT VT = N->getValueType(0);
  SDValue Conv = DAG.getBitcast(VT, Shuffle);

  EVT ExtVT = EVT::getVectorVT(*DAG.getContext(),
                               Input.getValueType().getVectorElementType(),
                               VT.getVectorNumElements());
  return DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, VT, Conv,
                     DAG.getValueType(ExtVT));
}

// Look for build vector patterns where input operands come from sign
// extended vector_extract elements of specific indices. If the correct indices
// aren't used, add a vector shuffle to fix up the indices and create
// SIGN_EXTEND_INREG node which selects the vector sign extend instructions
// during instruction selection.
static SDValue combineBVOfVecSExt(SDNode *N, SelectionDAG &DAG) {
  // This array encodes the indices that the vector sign extend instructions
  // extract from when extending from one type to another for both BE and LE.
  // The right nibble of each byte corresponds to the LE incides.
  // and the left nibble of each byte corresponds to the BE incides.
  // For example: 0x3074B8FC  byte->word
  // For LE: the allowed indices are: 0x0,0x4,0x8,0xC
  // For BE: the allowed indices are: 0x3,0x7,0xB,0xF
  // For example: 0x000070F8  byte->double word
  // For LE: the allowed indices are: 0x0,0x8
  // For BE: the allowed indices are: 0x7,0xF
  uint64_t TargetElems[] = {
      0x3074B8FC, // b->w
      0x000070F8, // b->d
      0x10325476, // h->w
      0x00003074, // h->d
      0x00001032, // w->d
  };

  uint64_t Elems = 0;
  int Index;
  SDValue Input;

  auto isSExtOfVecExtract = [&](SDValue Op) -> bool {
    if (!Op)
      return false;
    if (Op.getOpcode() != ISD::SIGN_EXTEND &&
        Op.getOpcode() != ISD::SIGN_EXTEND_INREG)
      return false;

    // A SIGN_EXTEND_INREG might be fed by an ANY_EXTEND to produce a value
    // of the right width.
    SDValue Extract = Op.getOperand(0);
    if (Extract.getOpcode() == ISD::ANY_EXTEND)
      Extract = Extract.getOperand(0);
    if (Extract.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
      return false;

    ConstantSDNode *ExtOp = dyn_cast<ConstantSDNode>(Extract.getOperand(1));
    if (!ExtOp)
      return false;

    Index = ExtOp->getZExtValue();
    if (Input && Input != Extract.getOperand(0))
      return false;

    if (!Input)
      Input = Extract.getOperand(0);

    Elems = Elems << 8;
    Index = DAG.getDataLayout().isLittleEndian() ? Index : Index << 4;
    Elems |= Index;

    return true;
  };

  // If the build vector operands aren't sign extended vector extracts,
  // of the same input vector, then return.
  for (unsigned i = 0; i < N->getNumOperands(); i++) {
    if (!isSExtOfVecExtract(N->getOperand(i))) {
      return SDValue();
    }
  }

  // If the vector extract indices are not correct, add the appropriate
  // vector_shuffle.
  int TgtElemArrayIdx;
  int InputSize = Input.getValueType().getScalarSizeInBits();
  int OutputSize = N->getValueType(0).getScalarSizeInBits();
  if (InputSize + OutputSize == 40)
    TgtElemArrayIdx = 0;
  else if (InputSize + OutputSize == 72)
    TgtElemArrayIdx = 1;
  else if (InputSize + OutputSize == 48)
    TgtElemArrayIdx = 2;
  else if (InputSize + OutputSize == 80)
    TgtElemArrayIdx = 3;
  else if (InputSize + OutputSize == 96)
    TgtElemArrayIdx = 4;
  else
    return SDValue();

  uint64_t CorrectElems = TargetElems[TgtElemArrayIdx];
  CorrectElems = DAG.getDataLayout().isLittleEndian()
                     ? CorrectElems & 0x0F0F0F0F0F0F0F0F
                     : CorrectElems & 0xF0F0F0F0F0F0F0F0;
  if (Elems != CorrectElems) {
    return addShuffleForVecExtend(N, DAG, Input, Elems, CorrectElems);
  }

  // Regular lowering will catch cases where a shuffle is not needed.
  return SDValue();
}

// Look for the pattern of a load from a narrow width to i128, feeding
// into a BUILD_VECTOR of v1i128. Replace this sequence with a PPCISD node
// (LXVRZX). This node represents a zero extending load that will be matched
// to the Load VSX Vector Rightmost instructions.
static SDValue combineBVZEXTLOAD(SDNode *N, SelectionDAG &DAG) {
  SDLoc DL(N);

  // This combine is only eligible for a BUILD_VECTOR of v1i128.
  if (N->getValueType(0) != MVT::v1i128)
    return SDValue();

  SDValue Operand = N->getOperand(0);
  // Proceed with the transformation if the operand to the BUILD_VECTOR
  // is a load instruction.
  if (Operand.getOpcode() != ISD::LOAD)
    return SDValue();

  auto *LD = cast<LoadSDNode>(Operand);
  EVT MemoryType = LD->getMemoryVT();

  // This transformation is only valid if the we are loading either a byte,
  // halfword, word, or doubleword.
  bool ValidLDType = MemoryType == MVT::i8 || MemoryType == MVT::i16 ||
                     MemoryType == MVT::i32 || MemoryType == MVT::i64;

  // Ensure that the load from the narrow width is being zero extended to i128.
  if (!ValidLDType ||
      (LD->getExtensionType() != ISD::ZEXTLOAD &&
       LD->getExtensionType() != ISD::EXTLOAD))
    return SDValue();

  SDValue LoadOps[] = {
      LD->getChain(), LD->getBasePtr(),
      DAG.getIntPtrConstant(MemoryType.getScalarSizeInBits(), DL)};

  return DAG.getMemIntrinsicNode(PPCISD::LXVRZX, DL,
                                 DAG.getVTList(MVT::v1i128, MVT::Other),
                                 LoadOps, MemoryType, LD->getMemOperand());
}

SDValue PPCTargetLowering::DAGCombineBuildVector(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::BUILD_VECTOR &&
         "Should be called with a BUILD_VECTOR node");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  if (!Subtarget.hasVSX())
    return SDValue();

  // The target independent DAG combiner will leave a build_vector of
  // float-to-int conversions intact. We can generate MUCH better code for
  // a float-to-int conversion of a vector of floats.
  SDValue FirstInput = N->getOperand(0);
  if (FirstInput.getOpcode() == PPCISD::MFVSR) {
    SDValue Reduced = combineElementTruncationToVectorTruncation(N, DCI);
    if (Reduced)
      return Reduced;
  }

  // If we're building a vector out of consecutive loads, just load that
  // vector type.
  SDValue Reduced = combineBVOfConsecutiveLoads(N, DAG);
  if (Reduced)
    return Reduced;

  // If we're building a vector out of extended elements from another vector
  // we have P9 vector integer extend instructions. The code assumes legal
  // input types (i.e. it can't handle things like v4i16) so do not run before
  // legalization.
  if (Subtarget.hasP9Altivec() && !DCI.isBeforeLegalize()) {
    Reduced = combineBVOfVecSExt(N, DAG);
    if (Reduced)
      return Reduced;
  }

  // On Power10, the Load VSX Vector Rightmost instructions can be utilized
  // if this is a BUILD_VECTOR of v1i128, and if the operand to the BUILD_VECTOR
  // is a load from <valid narrow width> to i128.
  if (Subtarget.isISA3_1()) {
    SDValue BVOfZLoad = combineBVZEXTLOAD(N, DAG);
    if (BVOfZLoad)
      return BVOfZLoad;
  }

  if (N->getValueType(0) != MVT::v2f64)
    return SDValue();

  // Looking for:
  // (build_vector ([su]int_to_fp (extractelt 0)), [su]int_to_fp (extractelt 1))
  if (FirstInput.getOpcode() != ISD::SINT_TO_FP &&
      FirstInput.getOpcode() != ISD::UINT_TO_FP)
    return SDValue();
  if (N->getOperand(1).getOpcode() != ISD::SINT_TO_FP &&
      N->getOperand(1).getOpcode() != ISD::UINT_TO_FP)
    return SDValue();
  if (FirstInput.getOpcode() != N->getOperand(1).getOpcode())
    return SDValue();

  SDValue Ext1 = FirstInput.getOperand(0);
  SDValue Ext2 = N->getOperand(1).getOperand(0);
  if(Ext1.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
     Ext2.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    return SDValue();

  ConstantSDNode *Ext1Op = dyn_cast<ConstantSDNode>(Ext1.getOperand(1));
  ConstantSDNode *Ext2Op = dyn_cast<ConstantSDNode>(Ext2.getOperand(1));
  if (!Ext1Op || !Ext2Op)
    return SDValue();
  if (Ext1.getOperand(0).getValueType() != MVT::v4i32 ||
      Ext1.getOperand(0) != Ext2.getOperand(0))
    return SDValue();

  int FirstElem = Ext1Op->getZExtValue();
  int SecondElem = Ext2Op->getZExtValue();
  int SubvecIdx;
  if (FirstElem == 0 && SecondElem == 1)
    SubvecIdx = Subtarget.isLittleEndian() ? 1 : 0;
  else if (FirstElem == 2 && SecondElem == 3)
    SubvecIdx = Subtarget.isLittleEndian() ? 0 : 1;
  else
    return SDValue();

  SDValue SrcVec = Ext1.getOperand(0);
  auto NodeType = (N->getOperand(1).getOpcode() == ISD::SINT_TO_FP) ?
    PPCISD::SINT_VEC_TO_FP : PPCISD::UINT_VEC_TO_FP;
  return DAG.getNode(NodeType, dl, MVT::v2f64,
                     SrcVec, DAG.getIntPtrConstant(SubvecIdx, dl));
}

SDValue PPCTargetLowering::combineFPToIntToFP(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  assert((N->getOpcode() == ISD::SINT_TO_FP ||
          N->getOpcode() == ISD::UINT_TO_FP) &&
         "Need an int -> FP conversion node here");

  if (useSoftFloat() || !Subtarget.has64BitSupport())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue Op(N, 0);

  // Don't handle ppc_fp128 here or conversions that are out-of-range capable
  // from the hardware.
  if (Op.getValueType() != MVT::f32 && Op.getValueType() != MVT::f64)
    return SDValue();
  if (!Op.getOperand(0).getValueType().isSimple())
    return SDValue();
  if (Op.getOperand(0).getValueType().getSimpleVT() <= MVT(MVT::i1) ||
      Op.getOperand(0).getValueType().getSimpleVT() > MVT(MVT::i64))
    return SDValue();

  SDValue FirstOperand(Op.getOperand(0));
  bool SubWordLoad = FirstOperand.getOpcode() == ISD::LOAD &&
    (FirstOperand.getValueType() == MVT::i8 ||
     FirstOperand.getValueType() == MVT::i16);
  if (Subtarget.hasP9Vector() && Subtarget.hasP9Altivec() && SubWordLoad) {
    bool Signed = N->getOpcode() == ISD::SINT_TO_FP;
    bool DstDouble = Op.getValueType() == MVT::f64;
    unsigned ConvOp = Signed ?
      (DstDouble ? PPCISD::FCFID  : PPCISD::FCFIDS) :
      (DstDouble ? PPCISD::FCFIDU : PPCISD::FCFIDUS);
    SDValue WidthConst =
      DAG.getIntPtrConstant(FirstOperand.getValueType() == MVT::i8 ? 1 : 2,
                            dl, false);
    LoadSDNode *LDN = cast<LoadSDNode>(FirstOperand.getNode());
    SDValue Ops[] = { LDN->getChain(), LDN->getBasePtr(), WidthConst };
    SDValue Ld = DAG.getMemIntrinsicNode(PPCISD::LXSIZX, dl,
                                         DAG.getVTList(MVT::f64, MVT::Other),
                                         Ops, MVT::i8, LDN->getMemOperand());
    DAG.makeEquivalentMemoryOrdering(LDN, Ld);

    // For signed conversion, we need to sign-extend the value in the VSR
    if (Signed) {
      SDValue ExtOps[] = { Ld, WidthConst };
      SDValue Ext = DAG.getNode(PPCISD::VEXTS, dl, MVT::f64, ExtOps);
      return DAG.getNode(ConvOp, dl, DstDouble ? MVT::f64 : MVT::f32, Ext);
    } else
      return DAG.getNode(ConvOp, dl, DstDouble ? MVT::f64 : MVT::f32, Ld);
  }


  // For i32 intermediate values, unfortunately, the conversion functions
  // leave the upper 32 bits of the value are undefined. Within the set of
  // scalar instructions, we have no method for zero- or sign-extending the
  // value. Thus, we cannot handle i32 intermediate values here.
  if (Op.getOperand(0).getValueType() == MVT::i32)
    return SDValue();

  assert((Op.getOpcode() == ISD::SINT_TO_FP || Subtarget.hasFPCVT()) &&
         "UINT_TO_FP is supported only with FPCVT");

  // If we have FCFIDS, then use it when converting to single-precision.
  // Otherwise, convert to double-precision and then round.
  unsigned FCFOp = (Subtarget.hasFPCVT() && Op.getValueType() == MVT::f32)
                       ? (Op.getOpcode() == ISD::UINT_TO_FP ? PPCISD::FCFIDUS
                                                            : PPCISD::FCFIDS)
                       : (Op.getOpcode() == ISD::UINT_TO_FP ? PPCISD::FCFIDU
                                                            : PPCISD::FCFID);
  MVT FCFTy = (Subtarget.hasFPCVT() && Op.getValueType() == MVT::f32)
                  ? MVT::f32
                  : MVT::f64;

  // If we're converting from a float, to an int, and back to a float again,
  // then we don't need the store/load pair at all.
  if ((Op.getOperand(0).getOpcode() == ISD::FP_TO_UINT &&
       Subtarget.hasFPCVT()) ||
      (Op.getOperand(0).getOpcode() == ISD::FP_TO_SINT)) {
    SDValue Src = Op.getOperand(0).getOperand(0);
    if (Src.getValueType() == MVT::f32) {
      Src = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Src);
      DCI.AddToWorklist(Src.getNode());
    } else if (Src.getValueType() != MVT::f64) {
      // Make sure that we don't pick up a ppc_fp128 source value.
      return SDValue();
    }

    unsigned FCTOp =
      Op.getOperand(0).getOpcode() == ISD::FP_TO_SINT ? PPCISD::FCTIDZ :
                                                        PPCISD::FCTIDUZ;

    SDValue Tmp = DAG.getNode(FCTOp, dl, MVT::f64, Src);
    SDValue FP = DAG.getNode(FCFOp, dl, FCFTy, Tmp);

    if (Op.getValueType() == MVT::f32 && !Subtarget.hasFPCVT()) {
      FP = DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, FP,
                       DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
      DCI.AddToWorklist(FP.getNode());
    }

    return FP;
  }

  return SDValue();
}

// expandVSXLoadForLE - Convert VSX loads (which may be intrinsics for
// builtins) into loads with swaps.
SDValue PPCTargetLowering::expandVSXLoadForLE(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  // Delay VSX load for LE combine until after LegalizeOps to prioritize other
  // load combines.
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue Chain;
  SDValue Base;
  MachineMemOperand *MMO;

  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode for little endian VSX load");
  case ISD::LOAD: {
    LoadSDNode *LD = cast<LoadSDNode>(N);
    Chain = LD->getChain();
    Base = LD->getBasePtr();
    MMO = LD->getMemOperand();
    // If the MMO suggests this isn't a load of a full vector, leave
    // things alone.  For a built-in, we have to make the change for
    // correctness, so if there is a size problem that will be a bug.
    if (!MMO->getSize().hasValue() || MMO->getSize().getValue() < 16)
      return SDValue();
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    MemIntrinsicSDNode *Intrin = cast<MemIntrinsicSDNode>(N);
    Chain = Intrin->getChain();
    // Similarly to the store case below, Intrin->getBasePtr() doesn't get
    // us what we want. Get operand 2 instead.
    Base = Intrin->getOperand(2);
    MMO = Intrin->getMemOperand();
    break;
  }
  }

  MVT VecTy = N->getValueType(0).getSimpleVT();

  SDValue LoadOps[] = { Chain, Base };
  SDValue Load = DAG.getMemIntrinsicNode(PPCISD::LXVD2X, dl,
                                         DAG.getVTList(MVT::v2f64, MVT::Other),
                                         LoadOps, MVT::v2f64, MMO);

  DCI.AddToWorklist(Load.getNode());
  Chain = Load.getValue(1);
  SDValue Swap = DAG.getNode(
      PPCISD::XXSWAPD, dl, DAG.getVTList(MVT::v2f64, MVT::Other), Chain, Load);
  DCI.AddToWorklist(Swap.getNode());

  // Add a bitcast if the resulting load type doesn't match v2f64.
  if (VecTy != MVT::v2f64) {
    SDValue N = DAG.getNode(ISD::BITCAST, dl, VecTy, Swap);
    DCI.AddToWorklist(N.getNode());
    // Package {bitcast value, swap's chain} to match Load's shape.
    return DAG.getNode(ISD::MERGE_VALUES, dl, DAG.getVTList(VecTy, MVT::Other),
                       N, Swap.getValue(1));
  }

  return Swap;
}

// expandVSXStoreForLE - Convert VSX stores (which may be intrinsics for
// builtins) into stores with swaps.
SDValue PPCTargetLowering::expandVSXStoreForLE(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  // Delay VSX store for LE combine until after LegalizeOps to prioritize other
  // store combines.
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue Chain;
  SDValue Base;
  unsigned SrcOpnd;
  MachineMemOperand *MMO;

  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode for little endian VSX store");
  case ISD::STORE: {
    StoreSDNode *ST = cast<StoreSDNode>(N);
    Chain = ST->getChain();
    Base = ST->getBasePtr();
    MMO = ST->getMemOperand();
    SrcOpnd = 1;
    // If the MMO suggests this isn't a store of a full vector, leave
    // things alone.  For a built-in, we have to make the change for
    // correctness, so if there is a size problem that will be a bug.
    if (!MMO->getSize().hasValue() || MMO->getSize().getValue() < 16)
      return SDValue();
    break;
  }
  case ISD::INTRINSIC_VOID: {
    MemIntrinsicSDNode *Intrin = cast<MemIntrinsicSDNode>(N);
    Chain = Intrin->getChain();
    // Intrin->getBasePtr() oddly does not get what we want.
    Base = Intrin->getOperand(3);
    MMO = Intrin->getMemOperand();
    SrcOpnd = 2;
    break;
  }
  }

  SDValue Src = N->getOperand(SrcOpnd);
  MVT VecTy = Src.getValueType().getSimpleVT();

  // All stores are done as v2f64 and possible bit cast.
  if (VecTy != MVT::v2f64) {
    Src = DAG.getNode(ISD::BITCAST, dl, MVT::v2f64, Src);
    DCI.AddToWorklist(Src.getNode());
  }

  SDValue Swap = DAG.getNode(PPCISD::XXSWAPD, dl,
                             DAG.getVTList(MVT::v2f64, MVT::Other), Chain, Src);
  DCI.AddToWorklist(Swap.getNode());
  Chain = Swap.getValue(1);
  SDValue StoreOps[] = { Chain, Swap, Base };
  SDValue Store = DAG.getMemIntrinsicNode(PPCISD::STXVD2X, dl,
                                          DAG.getVTList(MVT::Other),
                                          StoreOps, VecTy, MMO);
  DCI.AddToWorklist(Store.getNode());
  return Store;
}

// Handle DAG combine for STORE (FP_TO_INT F).
SDValue PPCTargetLowering::combineStoreFPToInt(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  unsigned Opcode = N->getOperand(1).getOpcode();
  (void)Opcode;
  bool Strict = N->getOperand(1)->isStrictFPOpcode();

  assert((Opcode == ISD::FP_TO_SINT || Opcode == ISD::FP_TO_UINT ||
          Opcode == ISD::STRICT_FP_TO_SINT || Opcode == ISD::STRICT_FP_TO_UINT)
         && "Not a FP_TO_INT Instruction!");

  SDValue Val = N->getOperand(1).getOperand(Strict ? 1 : 0);
  EVT Op1VT = N->getOperand(1).getValueType();
  EVT ResVT = Val.getValueType();

  if (!Subtarget.hasVSX() || !Subtarget.hasFPCVT() || !isTypeLegal(ResVT))
    return SDValue();

  // Only perform combine for conversion to i64/i32 or power9 i16/i8.
  bool ValidTypeForStoreFltAsInt =
        (Op1VT == MVT::i32 || (Op1VT == MVT::i64 && Subtarget.isPPC64()) ||
         (Subtarget.hasP9Vector() && (Op1VT == MVT::i16 || Op1VT == MVT::i8)));

  // TODO: Lower conversion from f128 on all VSX targets
  if (ResVT == MVT::ppcf128 || (ResVT == MVT::f128 && !Subtarget.hasP9Vector()))
    return SDValue();

  if ((Op1VT != MVT::i64 && !Subtarget.hasP8Vector()) ||
      cast<StoreSDNode>(N)->isTruncatingStore() || !ValidTypeForStoreFltAsInt)
    return SDValue();

  Val = convertFPToInt(N->getOperand(1), DAG, Subtarget);

  // Set number of bytes being converted.
  unsigned ByteSize = Op1VT.getScalarSizeInBits() / 8;
  SDValue Ops[] = {N->getOperand(0), Val, N->getOperand(2),
                   DAG.getIntPtrConstant(ByteSize, dl, false),
                   DAG.getValueType(Op1VT)};

  Val = DAG.getMemIntrinsicNode(PPCISD::ST_VSR_SCAL_INT, dl,
          DAG.getVTList(MVT::Other), Ops,
          cast<StoreSDNode>(N)->getMemoryVT(),
          cast<StoreSDNode>(N)->getMemOperand());

  return Val;
}

static bool isAlternatingShuffMask(const ArrayRef<int> &Mask, int NumElts) {
  // Check that the source of the element keeps flipping
  // (i.e. Mask[i] < NumElts -> Mask[i+i] >= NumElts).
  bool PrevElemFromFirstVec = Mask[0] < NumElts;
  for (int i = 1, e = Mask.size(); i < e; i++) {
    if (PrevElemFromFirstVec && Mask[i] < NumElts)
      return false;
    if (!PrevElemFromFirstVec && Mask[i] >= NumElts)
      return false;
    PrevElemFromFirstVec = !PrevElemFromFirstVec;
  }
  return true;
}

static bool isSplatBV(SDValue Op) {
  if (Op.getOpcode() != ISD::BUILD_VECTOR)
    return false;
  SDValue FirstOp;

  // Find first non-undef input.
  for (int i = 0, e = Op.getNumOperands(); i < e; i++) {
    FirstOp = Op.getOperand(i);
    if (!FirstOp.isUndef())
      break;
  }

  // All inputs are undef or the same as the first non-undef input.
  for (int i = 1, e = Op.getNumOperands(); i < e; i++)
    if (Op.getOperand(i) != FirstOp && !Op.getOperand(i).isUndef())
      return false;
  return true;
}

static SDValue isScalarToVec(SDValue Op) {
  if (Op.getOpcode() == ISD::SCALAR_TO_VECTOR)
    return Op;
  if (Op.getOpcode() != ISD::BITCAST)
    return SDValue();
  Op = Op.getOperand(0);
  if (Op.getOpcode() == ISD::SCALAR_TO_VECTOR)
    return Op;
  return SDValue();
}

// Fix up the shuffle mask to account for the fact that the result of
// scalar_to_vector is not in lane zero. This just takes all values in
// the ranges specified by the min/max indices and adds the number of
// elements required to ensure each element comes from the respective
// position in the valid lane.
// On little endian, that's just the corresponding element in the other
// half of the vector. On big endian, it is in the same half but right
// justified rather than left justified in that half.
static void fixupShuffleMaskForPermutedSToV(SmallVectorImpl<int> &ShuffV,
                                            int LHSMaxIdx, int RHSMinIdx,
                                            int RHSMaxIdx, int HalfVec,
                                            unsigned ValidLaneWidth,
                                            const PPCSubtarget &Subtarget) {
  for (int i = 0, e = ShuffV.size(); i < e; i++) {
    int Idx = ShuffV[i];
    if ((Idx >= 0 && Idx < LHSMaxIdx) || (Idx >= RHSMinIdx && Idx < RHSMaxIdx))
      ShuffV[i] +=
          Subtarget.isLittleEndian() ? HalfVec : HalfVec - ValidLaneWidth;
  }
}

// Replace a SCALAR_TO_VECTOR with a SCALAR_TO_VECTOR_PERMUTED except if
// the original is:
// (<n x Ty> (scalar_to_vector (Ty (extract_elt <n x Ty> %a, C))))
// In such a case, just change the shuffle mask to extract the element
// from the permuted index.
static SDValue getSToVPermuted(SDValue OrigSToV, SelectionDAG &DAG,
                               const PPCSubtarget &Subtarget) {
  SDLoc dl(OrigSToV);
  EVT VT = OrigSToV.getValueType();
  assert(OrigSToV.getOpcode() == ISD::SCALAR_TO_VECTOR &&
         "Expecting a SCALAR_TO_VECTOR here");
  SDValue Input = OrigSToV.getOperand(0);

  if (Input.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
    ConstantSDNode *Idx = dyn_cast<ConstantSDNode>(Input.getOperand(1));
    SDValue OrigVector = Input.getOperand(0);

    // Can't handle non-const element indices or different vector types
    // for the input to the extract and the output of the scalar_to_vector.
    if (Idx && VT == OrigVector.getValueType()) {
      unsigned NumElts = VT.getVectorNumElements();
      assert(
          NumElts > 1 &&
          "Cannot produce a permuted scalar_to_vector for one element vector");
      SmallVector<int, 16> NewMask(NumElts, -1);
      unsigned ResultInElt = NumElts / 2;
      ResultInElt -= Subtarget.isLittleEndian() ? 0 : 1;
      NewMask[ResultInElt] = Idx->getZExtValue();
      return DAG.getVectorShuffle(VT, dl, OrigVector, OrigVector, NewMask);
    }
  }
  return DAG.getNode(PPCISD::SCALAR_TO_VECTOR_PERMUTED, dl, VT,
                     OrigSToV.getOperand(0));
}

// On little endian subtargets, combine shuffles such as:
// vector_shuffle<16,1,17,3,18,5,19,7,20,9,21,11,22,13,23,15>, <zero>, %b
// into:
// vector_shuffle<16,0,17,1,18,2,19,3,20,4,21,5,22,6,23,7>, <zero>, %b
// because the latter can be matched to a single instruction merge.
// Furthermore, SCALAR_TO_VECTOR on little endian always involves a permute
// to put the value into element zero. Adjust the shuffle mask so that the
// vector can remain in permuted form (to prevent a swap prior to a shuffle).
// On big endian targets, this is still useful for SCALAR_TO_VECTOR
// nodes with elements smaller than doubleword because all the ways
// of getting scalar data into a vector register put the value in the
// rightmost element of the left half of the vector.
SDValue PPCTargetLowering::combineVectorShuffle(ShuffleVectorSDNode *SVN,
                                                SelectionDAG &DAG) const {
  SDValue LHS = SVN->getOperand(0);
  SDValue RHS = SVN->getOperand(1);
  auto Mask = SVN->getMask();
  int NumElts = LHS.getValueType().getVectorNumElements();
  SDValue Res(SVN, 0);
  SDLoc dl(SVN);
  bool IsLittleEndian = Subtarget.isLittleEndian();

  // On big endian targets this is only useful for subtargets with direct moves.
  // On little endian targets it would be useful for all subtargets with VSX.
  // However adding special handling for LE subtargets without direct moves
  // would be wasted effort since the minimum arch for LE is ISA 2.07 (Power8)
  // which includes direct moves.
  if (!Subtarget.hasDirectMove())
    return Res;

  // If this is not a shuffle of a shuffle and the first element comes from
  // the second vector, canonicalize to the commuted form. This will make it
  // more likely to match one of the single instruction patterns.
  if (Mask[0] >= NumElts && LHS.getOpcode() != ISD::VECTOR_SHUFFLE &&
      RHS.getOpcode() != ISD::VECTOR_SHUFFLE) {
    std::swap(LHS, RHS);
    Res = DAG.getCommutedVectorShuffle(*SVN);
    Mask = cast<ShuffleVectorSDNode>(Res)->getMask();
  }

  // Adjust the shuffle mask if either input vector comes from a
  // SCALAR_TO_VECTOR and keep the respective input vector in permuted
  // form (to prevent the need for a swap).
  SmallVector<int, 16> ShuffV(Mask);
  SDValue SToVLHS = isScalarToVec(LHS);
  SDValue SToVRHS = isScalarToVec(RHS);
  if (SToVLHS || SToVRHS) {
    // FIXME: If both LHS and RHS are SCALAR_TO_VECTOR, but are not the
    // same type and have differing element sizes, then do not perform
    // the following transformation. The current transformation for
    // SCALAR_TO_VECTOR assumes that both input vectors have the same
    // element size. This will be updated in the future to account for
    // differing sizes of the LHS and RHS.
    if (SToVLHS && SToVRHS &&
        (SToVLHS.getValueType().getScalarSizeInBits() !=
         SToVRHS.getValueType().getScalarSizeInBits()))
      return Res;

    int NumEltsIn = SToVLHS ? SToVLHS.getValueType().getVectorNumElements()
                            : SToVRHS.getValueType().getVectorNumElements();
    int NumEltsOut = ShuffV.size();
    // The width of the "valid lane" (i.e. the lane that contains the value that
    // is vectorized) needs to be expressed in terms of the number of elements
    // of the shuffle. It is thereby the ratio of the values before and after
    // any bitcast.
    unsigned ValidLaneWidth =
        SToVLHS ? SToVLHS.getValueType().getScalarSizeInBits() /
                      LHS.getValueType().getScalarSizeInBits()
                : SToVRHS.getValueType().getScalarSizeInBits() /
                      RHS.getValueType().getScalarSizeInBits();

    // Initially assume that neither input is permuted. These will be adjusted
    // accordingly if either input is.
    int LHSMaxIdx = -1;
    int RHSMinIdx = -1;
    int RHSMaxIdx = -1;
    int HalfVec = LHS.getValueType().getVectorNumElements() / 2;

    // Get the permuted scalar to vector nodes for the source(s) that come from
    // ISD::SCALAR_TO_VECTOR.
    // On big endian systems, this only makes sense for element sizes smaller
    // than 64 bits since for 64-bit elements, all instructions already put
    // the value into element zero. Since scalar size of LHS and RHS may differ
    // after isScalarToVec, this should be checked using their own sizes.
    if (SToVLHS) {
      if (!IsLittleEndian && SToVLHS.getValueType().getScalarSizeInBits() >= 64)
        return Res;
      // Set up the values for the shuffle vector fixup.
      LHSMaxIdx = NumEltsOut / NumEltsIn;
      SToVLHS = getSToVPermuted(SToVLHS, DAG, Subtarget);
      if (SToVLHS.getValueType() != LHS.getValueType())
        SToVLHS = DAG.getBitcast(LHS.getValueType(), SToVLHS);
      LHS = SToVLHS;
    }
    if (SToVRHS) {
      if (!IsLittleEndian && SToVRHS.getValueType().getScalarSizeInBits() >= 64)
        return Res;
      RHSMinIdx = NumEltsOut;
      RHSMaxIdx = NumEltsOut / NumEltsIn + RHSMinIdx;
      SToVRHS = getSToVPermuted(SToVRHS, DAG, Subtarget);
      if (SToVRHS.getValueType() != RHS.getValueType())
        SToVRHS = DAG.getBitcast(RHS.getValueType(), SToVRHS);
      RHS = SToVRHS;
    }

    // Fix up the shuffle mask to reflect where the desired element actually is.
    // The minimum and maximum indices that correspond to element zero for both
    // the LHS and RHS are computed and will control which shuffle mask entries
    // are to be changed. For example, if the RHS is permuted, any shuffle mask
    // entries in the range [RHSMinIdx,RHSMaxIdx) will be adjusted.
    fixupShuffleMaskForPermutedSToV(ShuffV, LHSMaxIdx, RHSMinIdx, RHSMaxIdx,
                                    HalfVec, ValidLaneWidth, Subtarget);
    Res = DAG.getVectorShuffle(SVN->getValueType(0), dl, LHS, RHS, ShuffV);

    // We may have simplified away the shuffle. We won't be able to do anything
    // further with it here.
    if (!isa<ShuffleVectorSDNode>(Res))
      return Res;
    Mask = cast<ShuffleVectorSDNode>(Res)->getMask();
  }

  SDValue TheSplat = IsLittleEndian ? RHS : LHS;
  // The common case after we commuted the shuffle is that the RHS is a splat
  // and we have elements coming in from the splat at indices that are not
  // conducive to using a merge.
  // Example:
  // vector_shuffle<0,17,1,19,2,21,3,23,4,25,5,27,6,29,7,31> t1, <zero>
  if (!isSplatBV(TheSplat))
    return Res;

  // We are looking for a mask such that all even elements are from
  // one vector and all odd elements from the other.
  if (!isAlternatingShuffMask(Mask, NumElts))
    return Res;

  // Adjust the mask so we are pulling in the same index from the splat
  // as the index from the interesting vector in consecutive elements.
  if (IsLittleEndian) {
    // Example (even elements from first vector):
    // vector_shuffle<0,16,1,17,2,18,3,19,4,20,5,21,6,22,7,23> t1, <zero>
    if (Mask[0] < NumElts)
      for (int i = 1, e = Mask.size(); i < e; i += 2) {
        if (ShuffV[i] < 0)
          continue;
        // If element from non-splat is undef, pick first element from splat.
        ShuffV[i] = (ShuffV[i - 1] >= 0 ? ShuffV[i - 1] : 0) + NumElts;
      }
    // Example (odd elements from first vector):
    // vector_shuffle<16,0,17,1,18,2,19,3,20,4,21,5,22,6,23,7> t1, <zero>
    else
      for (int i = 0, e = Mask.size(); i < e; i += 2) {
        if (ShuffV[i] < 0)
          continue;
        // If element from non-splat is undef, pick first element from splat.
        ShuffV[i] = (ShuffV[i + 1] >= 0 ? ShuffV[i + 1] : 0) + NumElts;
      }
  } else {
    // Example (even elements from first vector):
    // vector_shuffle<0,16,1,17,2,18,3,19,4,20,5,21,6,22,7,23> <zero>, t1
    if (Mask[0] < NumElts)
      for (int i = 0, e = Mask.size(); i < e; i += 2) {
        if (ShuffV[i] < 0)
          continue;
        // If element from non-splat is undef, pick first element from splat.
        ShuffV[i] = ShuffV[i + 1] >= 0 ? ShuffV[i + 1] - NumElts : 0;
      }
    // Example (odd elements from first vector):
    // vector_shuffle<16,0,17,1,18,2,19,3,20,4,21,5,22,6,23,7> <zero>, t1
    else
      for (int i = 1, e = Mask.size(); i < e; i += 2) {
        if (ShuffV[i] < 0)
          continue;
        // If element from non-splat is undef, pick first element from splat.
        ShuffV[i] = ShuffV[i - 1] >= 0 ? ShuffV[i - 1] - NumElts : 0;
      }
  }

  // If the RHS has undefs, we need to remove them since we may have created
  // a shuffle that adds those instead of the splat value.
  SDValue SplatVal =
      cast<BuildVectorSDNode>(TheSplat.getNode())->getSplatValue();
  TheSplat = DAG.getSplatBuildVector(TheSplat.getValueType(), dl, SplatVal);

  if (IsLittleEndian)
    RHS = TheSplat;
  else
    LHS = TheSplat;
  return DAG.getVectorShuffle(SVN->getValueType(0), dl, LHS, RHS, ShuffV);
}

SDValue PPCTargetLowering::combineVReverseMemOP(ShuffleVectorSDNode *SVN,
                                                LSBaseSDNode *LSBase,
                                                DAGCombinerInfo &DCI) const {
  assert((ISD::isNormalLoad(LSBase) || ISD::isNormalStore(LSBase)) &&
        "Not a reverse memop pattern!");

  auto IsElementReverse = [](const ShuffleVectorSDNode *SVN) -> bool {
    auto Mask = SVN->getMask();
    int i = 0;
    auto I = Mask.rbegin();
    auto E = Mask.rend();

    for (; I != E; ++I) {
      if (*I != i)
        return false;
      i++;
    }
    return true;
  };

  SelectionDAG &DAG = DCI.DAG;
  EVT VT = SVN->getValueType(0);

  if (!isTypeLegal(VT) || !Subtarget.isLittleEndian() || !Subtarget.hasVSX())
    return SDValue();

  // Before P9, we have PPCVSXSwapRemoval pass to hack the element order.
  // See comment in PPCVSXSwapRemoval.cpp.
  // It is conflict with PPCVSXSwapRemoval opt. So we don't do it.
  if (!Subtarget.hasP9Vector())
    return SDValue();

  if(!IsElementReverse(SVN))
    return SDValue();

  if (LSBase->getOpcode() == ISD::LOAD) {
    // If the load return value 0 has more than one user except the
    // shufflevector instruction, it is not profitable to replace the
    // shufflevector with a reverse load.
    for (SDNode::use_iterator UI = LSBase->use_begin(), UE = LSBase->use_end();
         UI != UE; ++UI)
      if (UI.getUse().getResNo() == 0 && UI->getOpcode() != ISD::VECTOR_SHUFFLE)
        return SDValue();

    SDLoc dl(LSBase);
    SDValue LoadOps[] = {LSBase->getChain(), LSBase->getBasePtr()};
    return DAG.getMemIntrinsicNode(
        PPCISD::LOAD_VEC_BE, dl, DAG.getVTList(VT, MVT::Other), LoadOps,
        LSBase->getMemoryVT(), LSBase->getMemOperand());
  }

  if (LSBase->getOpcode() == ISD::STORE) {
    // If there are other uses of the shuffle, the swap cannot be avoided.
    // Forcing the use of an X-Form (since swapped stores only have
    // X-Forms) without removing the swap is unprofitable.
    if (!SVN->hasOneUse())
      return SDValue();

    SDLoc dl(LSBase);
    SDValue StoreOps[] = {LSBase->getChain(), SVN->getOperand(0),
                          LSBase->getBasePtr()};
    return DAG.getMemIntrinsicNode(
        PPCISD::STORE_VEC_BE, dl, DAG.getVTList(MVT::Other), StoreOps,
        LSBase->getMemoryVT(), LSBase->getMemOperand());
  }

  llvm_unreachable("Expected a load or store node here");
}

static bool isStoreConditional(SDValue Intrin, unsigned &StoreWidth) {
  unsigned IntrinsicID = Intrin.getConstantOperandVal(1);
  if (IntrinsicID == Intrinsic::ppc_stdcx)
    StoreWidth = 8;
  else if (IntrinsicID == Intrinsic::ppc_stwcx)
    StoreWidth = 4;
  else if (IntrinsicID == Intrinsic::ppc_sthcx)
    StoreWidth = 2;
  else if (IntrinsicID == Intrinsic::ppc_stbcx)
    StoreWidth = 1;
  else
    return false;
  return true;
}

SDValue PPCTargetLowering::PerformDAGCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  switch (N->getOpcode()) {
  default: break;
  case ISD::ADD:
    return combineADD(N, DCI);
  case ISD::AND: {
    // We don't want (and (zext (shift...)), C) if C fits in the width of the
    // original input as that will prevent us from selecting optimal rotates.
    // This only matters if the input to the extend is i32 widened to i64.
    SDValue Op1 = N->getOperand(0);
    SDValue Op2 = N->getOperand(1);
    if ((Op1.getOpcode() != ISD::ZERO_EXTEND &&
         Op1.getOpcode() != ISD::ANY_EXTEND) ||
        !isa<ConstantSDNode>(Op2) || N->getValueType(0) != MVT::i64 ||
        Op1.getOperand(0).getValueType() != MVT::i32)
      break;
    SDValue NarrowOp = Op1.getOperand(0);
    if (NarrowOp.getOpcode() != ISD::SHL && NarrowOp.getOpcode() != ISD::SRL &&
        NarrowOp.getOpcode() != ISD::ROTL && NarrowOp.getOpcode() != ISD::ROTR)
      break;

    uint64_t Imm = Op2->getAsZExtVal();
    // Make sure that the constant is narrow enough to fit in the narrow type.
    if (!isUInt<32>(Imm))
      break;
    SDValue ConstOp = DAG.getConstant(Imm, dl, MVT::i32);
    SDValue NarrowAnd = DAG.getNode(ISD::AND, dl, MVT::i32, NarrowOp, ConstOp);
    return DAG.getZExtOrTrunc(NarrowAnd, dl, N->getValueType(0));
  }
  case ISD::SHL:
    return combineSHL(N, DCI);
  case ISD::SRA:
    return combineSRA(N, DCI);
  case ISD::SRL:
    return combineSRL(N, DCI);
  case ISD::MUL:
    return combineMUL(N, DCI);
  case ISD::FMA:
  case PPCISD::FNMSUB:
    return combineFMALike(N, DCI);
  case PPCISD::SHL:
    if (isNullConstant(N->getOperand(0))) // 0 << V -> 0.
        return N->getOperand(0);
    break;
  case PPCISD::SRL:
    if (isNullConstant(N->getOperand(0))) // 0 >>u V -> 0.
        return N->getOperand(0);
    break;
  case PPCISD::SRA:
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(0))) {
      if (C->isZero() ||  //  0 >>s V -> 0.
          C->isAllOnes()) // -1 >>s V -> -1.
        return N->getOperand(0);
    }
    break;
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    return DAGCombineExtBoolTrunc(N, DCI);
  case ISD::TRUNCATE:
    return combineTRUNCATE(N, DCI);
  case ISD::SETCC:
    if (SDValue CSCC = combineSetCC(N, DCI))
      return CSCC;
    [[fallthrough]];
  case ISD::SELECT_CC:
    return DAGCombineTruncBoolExt(N, DCI);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    return combineFPToIntToFP(N, DCI);
  case ISD::VECTOR_SHUFFLE:
    if (ISD::isNormalLoad(N->getOperand(0).getNode())) {
      LSBaseSDNode* LSBase = cast<LSBaseSDNode>(N->getOperand(0));
      return combineVReverseMemOP(cast<ShuffleVectorSDNode>(N), LSBase, DCI);
    }
    return combineVectorShuffle(cast<ShuffleVectorSDNode>(N), DCI.DAG);
  case ISD::STORE: {

    EVT Op1VT = N->getOperand(1).getValueType();
    unsigned Opcode = N->getOperand(1).getOpcode();

    if (Opcode == ISD::FP_TO_SINT || Opcode == ISD::FP_TO_UINT ||
        Opcode == ISD::STRICT_FP_TO_SINT || Opcode == ISD::STRICT_FP_TO_UINT) {
      SDValue Val = combineStoreFPToInt(N, DCI);
      if (Val)
        return Val;
    }

    if (Opcode == ISD::VECTOR_SHUFFLE && ISD::isNormalStore(N)) {
      ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(N->getOperand(1));
      SDValue Val= combineVReverseMemOP(SVN, cast<LSBaseSDNode>(N), DCI);
      if (Val)
        return Val;
    }

    // Turn STORE (BSWAP) -> sthbrx/stwbrx.
    if (cast<StoreSDNode>(N)->isUnindexed() && Opcode == ISD::BSWAP &&
        N->getOperand(1).getNode()->hasOneUse() &&
        (Op1VT == MVT::i32 || Op1VT == MVT::i16 ||
         (Subtarget.hasLDBRX() && Subtarget.isPPC64() && Op1VT == MVT::i64))) {

      // STBRX can only handle simple types and it makes no sense to store less
      // two bytes in byte-reversed order.
      EVT mVT = cast<StoreSDNode>(N)->getMemoryVT();
      if (mVT.isExtended() || mVT.getSizeInBits() < 16)
        break;

      SDValue BSwapOp = N->getOperand(1).getOperand(0);
      // Do an any-extend to 32-bits if this is a half-word input.
      if (BSwapOp.getValueType() == MVT::i16)
        BSwapOp = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i32, BSwapOp);

      // If the type of BSWAP operand is wider than stored memory width
      // it need to be shifted to the right side before STBRX.
      if (Op1VT.bitsGT(mVT)) {
        int Shift = Op1VT.getSizeInBits() - mVT.getSizeInBits();
        BSwapOp = DAG.getNode(ISD::SRL, dl, Op1VT, BSwapOp,
                              DAG.getConstant(Shift, dl, MVT::i32));
        // Need to truncate if this is a bswap of i64 stored as i32/i16.
        if (Op1VT == MVT::i64)
          BSwapOp = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, BSwapOp);
      }

      SDValue Ops[] = {
        N->getOperand(0), BSwapOp, N->getOperand(2), DAG.getValueType(mVT)
      };
      return
        DAG.getMemIntrinsicNode(PPCISD::STBRX, dl, DAG.getVTList(MVT::Other),
                                Ops, cast<StoreSDNode>(N)->getMemoryVT(),
                                cast<StoreSDNode>(N)->getMemOperand());
    }

    // STORE Constant:i32<0>  ->  STORE<trunc to i32> Constant:i64<0>
    // So it can increase the chance of CSE constant construction.
    if (Subtarget.isPPC64() && !DCI.isBeforeLegalize() &&
        isa<ConstantSDNode>(N->getOperand(1)) && Op1VT == MVT::i32) {
      // Need to sign-extended to 64-bits to handle negative values.
      EVT MemVT = cast<StoreSDNode>(N)->getMemoryVT();
      uint64_t Val64 = SignExtend64(N->getConstantOperandVal(1),
                                    MemVT.getSizeInBits());
      SDValue Const64 = DAG.getConstant(Val64, dl, MVT::i64);

      // DAG.getTruncStore() can't be used here because it doesn't accept
      // the general (base + offset) addressing mode.
      // So we use UpdateNodeOperands and setTruncatingStore instead.
      DAG.UpdateNodeOperands(N, N->getOperand(0), Const64, N->getOperand(2),
                             N->getOperand(3));
      cast<StoreSDNode>(N)->setTruncatingStore(true);
      return SDValue(N, 0);
    }

    // For little endian, VSX stores require generating xxswapd/lxvd2x.
    // Not needed on ISA 3.0 based CPUs since we have a non-permuting store.
    if (Op1VT.isSimple()) {
      MVT StoreVT = Op1VT.getSimpleVT();
      if (Subtarget.needsSwapsForVSXMemOps() &&
          (StoreVT == MVT::v2f64 || StoreVT == MVT::v2i64 ||
           StoreVT == MVT::v4f32 || StoreVT == MVT::v4i32))
        return expandVSXStoreForLE(N, DCI);
    }
    break;
  }
  case ISD::LOAD: {
    LoadSDNode *LD = cast<LoadSDNode>(N);
    EVT VT = LD->getValueType(0);

    // For little endian, VSX loads require generating lxvd2x/xxswapd.
    // Not needed on ISA 3.0 based CPUs since we have a non-permuting load.
    if (VT.isSimple()) {
      MVT LoadVT = VT.getSimpleVT();
      if (Subtarget.needsSwapsForVSXMemOps() &&
          (LoadVT == MVT::v2f64 || LoadVT == MVT::v2i64 ||
           LoadVT == MVT::v4f32 || LoadVT == MVT::v4i32))
        return expandVSXLoadForLE(N, DCI);
    }

    // We sometimes end up with a 64-bit integer load, from which we extract
    // two single-precision floating-point numbers. This happens with
    // std::complex<float>, and other similar structures, because of the way we
    // canonicalize structure copies. However, if we lack direct moves,
    // then the final bitcasts from the extracted integer values to the
    // floating-point numbers turn into store/load pairs. Even with direct moves,
    // just loading the two floating-point numbers is likely better.
    auto ReplaceTwoFloatLoad = [&]() {
      if (VT != MVT::i64)
        return false;

      if (LD->getExtensionType() != ISD::NON_EXTLOAD ||
          LD->isVolatile())
        return false;

      //  We're looking for a sequence like this:
      //  t13: i64,ch = load<LD8[%ref.tmp]> t0, t6, undef:i64
      //      t16: i64 = srl t13, Constant:i32<32>
      //    t17: i32 = truncate t16
      //  t18: f32 = bitcast t17
      //    t19: i32 = truncate t13
      //  t20: f32 = bitcast t19

      if (!LD->hasNUsesOfValue(2, 0))
        return false;

      auto UI = LD->use_begin();
      while (UI.getUse().getResNo() != 0) ++UI;
      SDNode *Trunc = *UI++;
      while (UI.getUse().getResNo() != 0) ++UI;
      SDNode *RightShift = *UI;
      if (Trunc->getOpcode() != ISD::TRUNCATE)
        std::swap(Trunc, RightShift);

      if (Trunc->getOpcode() != ISD::TRUNCATE ||
          Trunc->getValueType(0) != MVT::i32 ||
          !Trunc->hasOneUse())
        return false;
      if (RightShift->getOpcode() != ISD::SRL ||
          !isa<ConstantSDNode>(RightShift->getOperand(1)) ||
          RightShift->getConstantOperandVal(1) != 32 ||
          !RightShift->hasOneUse())
        return false;

      SDNode *Trunc2 = *RightShift->use_begin();
      if (Trunc2->getOpcode() != ISD::TRUNCATE ||
          Trunc2->getValueType(0) != MVT::i32 ||
          !Trunc2->hasOneUse())
        return false;

      SDNode *Bitcast = *Trunc->use_begin();
      SDNode *Bitcast2 = *Trunc2->use_begin();

      if (Bitcast->getOpcode() != ISD::BITCAST ||
          Bitcast->getValueType(0) != MVT::f32)
        return false;
      if (Bitcast2->getOpcode() != ISD::BITCAST ||
          Bitcast2->getValueType(0) != MVT::f32)
        return false;

      if (Subtarget.isLittleEndian())
        std::swap(Bitcast, Bitcast2);

      // Bitcast has the second float (in memory-layout order) and Bitcast2
      // has the first one.

      SDValue BasePtr = LD->getBasePtr();
      if (LD->isIndexed()) {
        assert(LD->getAddressingMode() == ISD::PRE_INC &&
               "Non-pre-inc AM on PPC?");
        BasePtr =
          DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr,
                      LD->getOffset());
      }

      auto MMOFlags =
          LD->getMemOperand()->getFlags() & ~MachineMemOperand::MOVolatile;
      SDValue FloatLoad = DAG.getLoad(MVT::f32, dl, LD->getChain(), BasePtr,
                                      LD->getPointerInfo(), LD->getAlign(),
                                      MMOFlags, LD->getAAInfo());
      SDValue AddPtr =
        DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(),
                    BasePtr, DAG.getIntPtrConstant(4, dl));
      SDValue FloatLoad2 = DAG.getLoad(
          MVT::f32, dl, SDValue(FloatLoad.getNode(), 1), AddPtr,
          LD->getPointerInfo().getWithOffset(4),
          commonAlignment(LD->getAlign(), 4), MMOFlags, LD->getAAInfo());

      if (LD->isIndexed()) {
        // Note that DAGCombine should re-form any pre-increment load(s) from
        // what is produced here if that makes sense.
        DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), BasePtr);
      }

      DCI.CombineTo(Bitcast2, FloatLoad);
      DCI.CombineTo(Bitcast, FloatLoad2);

      DAG.ReplaceAllUsesOfValueWith(SDValue(LD, LD->isIndexed() ? 2 : 1),
                                    SDValue(FloatLoad2.getNode(), 1));
      return true;
    };

    if (ReplaceTwoFloatLoad())
      return SDValue(N, 0);

    EVT MemVT = LD->getMemoryVT();
    Type *Ty = MemVT.getTypeForEVT(*DAG.getContext());
    Align ABIAlignment = DAG.getDataLayout().getABITypeAlign(Ty);
    if (LD->isUnindexed() && VT.isVector() &&
        ((Subtarget.hasAltivec() && ISD::isNON_EXTLoad(N) &&
          // P8 and later hardware should just use LOAD.
          !Subtarget.hasP8Vector() &&
          (VT == MVT::v16i8 || VT == MVT::v8i16 || VT == MVT::v4i32 ||
           VT == MVT::v4f32))) &&
        LD->getAlign() < ABIAlignment) {
      // This is a type-legal unaligned Altivec load.
      SDValue Chain = LD->getChain();
      SDValue Ptr = LD->getBasePtr();
      bool isLittleEndian = Subtarget.isLittleEndian();

      // This implements the loading of unaligned vectors as described in
      // the venerable Apple Velocity Engine overview. Specifically:
      // https://developer.apple.com/hardwaredrivers/ve/alignment.html
      // https://developer.apple.com/hardwaredrivers/ve/code_optimization.html
      //
      // The general idea is to expand a sequence of one or more unaligned
      // loads into an alignment-based permutation-control instruction (lvsl
      // or lvsr), a series of regular vector loads (which always truncate
      // their input address to an aligned address), and a series of
      // permutations.  The results of these permutations are the requested
      // loaded values.  The trick is that the last "extra" load is not taken
      // from the address you might suspect (sizeof(vector) bytes after the
      // last requested load), but rather sizeof(vector) - 1 bytes after the
      // last requested vector. The point of this is to avoid a page fault if
      // the base address happened to be aligned. This works because if the
      // base address is aligned, then adding less than a full vector length
      // will cause the last vector in the sequence to be (re)loaded.
      // Otherwise, the next vector will be fetched as you might suspect was
      // necessary.

      // We might be able to reuse the permutation generation from
      // a different base address offset from this one by an aligned amount.
      // The INTRINSIC_WO_CHAIN DAG combine will attempt to perform this
      // optimization later.
      Intrinsic::ID Intr, IntrLD, IntrPerm;
      MVT PermCntlTy, PermTy, LDTy;
      Intr = isLittleEndian ? Intrinsic::ppc_altivec_lvsr
                            : Intrinsic::ppc_altivec_lvsl;
      IntrLD = Intrinsic::ppc_altivec_lvx;
      IntrPerm = Intrinsic::ppc_altivec_vperm;
      PermCntlTy = MVT::v16i8;
      PermTy = MVT::v4i32;
      LDTy = MVT::v4i32;

      SDValue PermCntl = BuildIntrinsicOp(Intr, Ptr, DAG, dl, PermCntlTy);

      // Create the new MMO for the new base load. It is like the original MMO,
      // but represents an area in memory almost twice the vector size centered
      // on the original address. If the address is unaligned, we might start
      // reading up to (sizeof(vector)-1) bytes below the address of the
      // original unaligned load.
      MachineFunction &MF = DAG.getMachineFunction();
      MachineMemOperand *BaseMMO =
        MF.getMachineMemOperand(LD->getMemOperand(),
                                -(int64_t)MemVT.getStoreSize()+1,
                                2*MemVT.getStoreSize()-1);

      // Create the new base load.
      SDValue LDXIntID =
          DAG.getTargetConstant(IntrLD, dl, getPointerTy(MF.getDataLayout()));
      SDValue BaseLoadOps[] = { Chain, LDXIntID, Ptr };
      SDValue BaseLoad =
        DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, dl,
                                DAG.getVTList(PermTy, MVT::Other),
                                BaseLoadOps, LDTy, BaseMMO);

      // Note that the value of IncOffset (which is provided to the next
      // load's pointer info offset value, and thus used to calculate the
      // alignment), and the value of IncValue (which is actually used to
      // increment the pointer value) are different! This is because we
      // require the next load to appear to be aligned, even though it
      // is actually offset from the base pointer by a lesser amount.
      int IncOffset = VT.getSizeInBits() / 8;
      int IncValue = IncOffset;

      // Walk (both up and down) the chain looking for another load at the real
      // (aligned) offset (the alignment of the other load does not matter in
      // this case). If found, then do not use the offset reduction trick, as
      // that will prevent the loads from being later combined (as they would
      // otherwise be duplicates).
      if (!findConsecutiveLoad(LD, DAG))
        --IncValue;

      SDValue Increment =
          DAG.getConstant(IncValue, dl, getPointerTy(MF.getDataLayout()));
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr, Increment);

      MachineMemOperand *ExtraMMO =
        MF.getMachineMemOperand(LD->getMemOperand(),
                                1, 2*MemVT.getStoreSize()-1);
      SDValue ExtraLoadOps[] = { Chain, LDXIntID, Ptr };
      SDValue ExtraLoad =
        DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, dl,
                                DAG.getVTList(PermTy, MVT::Other),
                                ExtraLoadOps, LDTy, ExtraMMO);

      SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
        BaseLoad.getValue(1), ExtraLoad.getValue(1));

      // Because vperm has a big-endian bias, we must reverse the order
      // of the input vectors and complement the permute control vector
      // when generating little endian code.  We have already handled the
      // latter by using lvsr instead of lvsl, so just reverse BaseLoad
      // and ExtraLoad here.
      SDValue Perm;
      if (isLittleEndian)
        Perm = BuildIntrinsicOp(IntrPerm,
                                ExtraLoad, BaseLoad, PermCntl, DAG, dl);
      else
        Perm = BuildIntrinsicOp(IntrPerm,
                                BaseLoad, ExtraLoad, PermCntl, DAG, dl);

      if (VT != PermTy)
        Perm = Subtarget.hasAltivec()
                   ? DAG.getNode(ISD::BITCAST, dl, VT, Perm)
                   : DAG.getNode(ISD::FP_ROUND, dl, VT, Perm,
                                 DAG.getTargetConstant(1, dl, MVT::i64));
                               // second argument is 1 because this rounding
                               // is always exact.

      // The output of the permutation is our loaded result, the TokenFactor is
      // our new chain.
      DCI.CombineTo(N, Perm, TF);
      return SDValue(N, 0);
    }
    }
    break;
    case ISD::INTRINSIC_WO_CHAIN: {
      bool isLittleEndian = Subtarget.isLittleEndian();
      unsigned IID = N->getConstantOperandVal(0);
      Intrinsic::ID Intr = (isLittleEndian ? Intrinsic::ppc_altivec_lvsr
                                           : Intrinsic::ppc_altivec_lvsl);
      if (IID == Intr && N->getOperand(1)->getOpcode() == ISD::ADD) {
        SDValue Add = N->getOperand(1);

        int Bits = 4 /* 16 byte alignment */;

        if (DAG.MaskedValueIsZero(Add->getOperand(1),
                                  APInt::getAllOnes(Bits /* alignment */)
                                      .zext(Add.getScalarValueSizeInBits()))) {
          SDNode *BasePtr = Add->getOperand(0).getNode();
          for (SDNode *U : BasePtr->uses()) {
          if (U->getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
              U->getConstantOperandVal(0) == IID) {
            // We've found another LVSL/LVSR, and this address is an aligned
            // multiple of that one. The results will be the same, so use the
            // one we've just found instead.

            return SDValue(U, 0);
          }
          }
        }

        if (isa<ConstantSDNode>(Add->getOperand(1))) {
          SDNode *BasePtr = Add->getOperand(0).getNode();
          for (SDNode *U : BasePtr->uses()) {
          if (U->getOpcode() == ISD::ADD &&
              isa<ConstantSDNode>(U->getOperand(1)) &&
              (Add->getConstantOperandVal(1) - U->getConstantOperandVal(1)) %
                      (1ULL << Bits) ==
                  0) {
            SDNode *OtherAdd = U;
            for (SDNode *V : OtherAdd->uses()) {
              if (V->getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
                  V->getConstantOperandVal(0) == IID) {
                return SDValue(V, 0);
              }
            }
          }
          }
        }
      }

      // Combine vmaxsw/h/b(a, a's negation) to abs(a)
      // Expose the vabsduw/h/b opportunity for down stream
      if (!DCI.isAfterLegalizeDAG() && Subtarget.hasP9Altivec() &&
          (IID == Intrinsic::ppc_altivec_vmaxsw ||
           IID == Intrinsic::ppc_altivec_vmaxsh ||
           IID == Intrinsic::ppc_altivec_vmaxsb)) {
        SDValue V1 = N->getOperand(1);
        SDValue V2 = N->getOperand(2);
        if ((V1.getSimpleValueType() == MVT::v4i32 ||
             V1.getSimpleValueType() == MVT::v8i16 ||
             V1.getSimpleValueType() == MVT::v16i8) &&
            V1.getSimpleValueType() == V2.getSimpleValueType()) {
          // (0-a, a)
          if (V1.getOpcode() == ISD::SUB &&
              ISD::isBuildVectorAllZeros(V1.getOperand(0).getNode()) &&
              V1.getOperand(1) == V2) {
            return DAG.getNode(ISD::ABS, dl, V2.getValueType(), V2);
          }
          // (a, 0-a)
          if (V2.getOpcode() == ISD::SUB &&
              ISD::isBuildVectorAllZeros(V2.getOperand(0).getNode()) &&
              V2.getOperand(1) == V1) {
            return DAG.getNode(ISD::ABS, dl, V1.getValueType(), V1);
          }
          // (x-y, y-x)
          if (V1.getOpcode() == ISD::SUB && V2.getOpcode() == ISD::SUB &&
              V1.getOperand(0) == V2.getOperand(1) &&
              V1.getOperand(1) == V2.getOperand(0)) {
            return DAG.getNode(ISD::ABS, dl, V1.getValueType(), V1);
          }
        }
      }
    }

    break;
  case ISD::INTRINSIC_W_CHAIN:
      switch (N->getConstantOperandVal(1)) {
      default:
        break;
      case Intrinsic::ppc_altivec_vsum4sbs:
      case Intrinsic::ppc_altivec_vsum4shs:
      case Intrinsic::ppc_altivec_vsum4ubs: {
        // These sum-across intrinsics only have a chain due to the side effect
        // that they may set the SAT bit. If we know the SAT bit will not be set
        // for some inputs, we can replace any uses of their chain with the
        // input chain.
        if (BuildVectorSDNode *BVN =
                dyn_cast<BuildVectorSDNode>(N->getOperand(3))) {
          APInt APSplatBits, APSplatUndef;
          unsigned SplatBitSize;
          bool HasAnyUndefs;
          bool BVNIsConstantSplat = BVN->isConstantSplat(
              APSplatBits, APSplatUndef, SplatBitSize, HasAnyUndefs, 0,
              !Subtarget.isLittleEndian());
          // If the constant splat vector is 0, the SAT bit will not be set.
          if (BVNIsConstantSplat && APSplatBits == 0)
            DAG.ReplaceAllUsesOfValueWith(SDValue(N, 1), N->getOperand(0));
        }
        return SDValue();
      }
    case Intrinsic::ppc_vsx_lxvw4x:
    case Intrinsic::ppc_vsx_lxvd2x:
      // For little endian, VSX loads require generating lxvd2x/xxswapd.
      // Not needed on ISA 3.0 based CPUs since we have a non-permuting load.
      if (Subtarget.needsSwapsForVSXMemOps())
        return expandVSXLoadForLE(N, DCI);
      break;
    }
    break;
  case ISD::INTRINSIC_VOID:
    // For little endian, VSX stores require generating xxswapd/stxvd2x.
    // Not needed on ISA 3.0 based CPUs since we have a non-permuting store.
    if (Subtarget.needsSwapsForVSXMemOps()) {
      switch (N->getConstantOperandVal(1)) {
      default:
        break;
      case Intrinsic::ppc_vsx_stxvw4x:
      case Intrinsic::ppc_vsx_stxvd2x:
        return expandVSXStoreForLE(N, DCI);
      }
    }
    break;
  case ISD::BSWAP: {
    // Turn BSWAP (LOAD) -> lhbrx/lwbrx.
    // For subtargets without LDBRX, we can still do better than the default
    // expansion even for 64-bit BSWAP (LOAD).
    bool Is64BitBswapOn64BitTgt =
        Subtarget.isPPC64() && N->getValueType(0) == MVT::i64;
    bool IsSingleUseNormalLd = ISD::isNormalLoad(N->getOperand(0).getNode()) &&
                               N->getOperand(0).hasOneUse();
    if (IsSingleUseNormalLd &&
        (N->getValueType(0) == MVT::i32 || N->getValueType(0) == MVT::i16 ||
         (Subtarget.hasLDBRX() && Is64BitBswapOn64BitTgt))) {
      SDValue Load = N->getOperand(0);
      LoadSDNode *LD = cast<LoadSDNode>(Load);
      // Create the byte-swapping load.
      SDValue Ops[] = {
        LD->getChain(),    // Chain
        LD->getBasePtr(),  // Ptr
        DAG.getValueType(N->getValueType(0)) // VT
      };
      SDValue BSLoad =
        DAG.getMemIntrinsicNode(PPCISD::LBRX, dl,
                                DAG.getVTList(N->getValueType(0) == MVT::i64 ?
                                              MVT::i64 : MVT::i32, MVT::Other),
                                Ops, LD->getMemoryVT(), LD->getMemOperand());

      // If this is an i16 load, insert the truncate.
      SDValue ResVal = BSLoad;
      if (N->getValueType(0) == MVT::i16)
        ResVal = DAG.getNode(ISD::TRUNCATE, dl, MVT::i16, BSLoad);

      // First, combine the bswap away.  This makes the value produced by the
      // load dead.
      DCI.CombineTo(N, ResVal);

      // Next, combine the load away, we give it a bogus result value but a real
      // chain result.  The result value is dead because the bswap is dead.
      DCI.CombineTo(Load.getNode(), ResVal, BSLoad.getValue(1));

      // Return N so it doesn't get rechecked!
      return SDValue(N, 0);
    }
    // Convert this to two 32-bit bswap loads and a BUILD_PAIR. Do this only
    // before legalization so that the BUILD_PAIR is handled correctly.
    if (!DCI.isBeforeLegalize() || !Is64BitBswapOn64BitTgt ||
        !IsSingleUseNormalLd)
      return SDValue();
    LoadSDNode *LD = cast<LoadSDNode>(N->getOperand(0));

    // Can't split volatile or atomic loads.
    if (!LD->isSimple())
      return SDValue();
    SDValue BasePtr = LD->getBasePtr();
    SDValue Lo = DAG.getLoad(MVT::i32, dl, LD->getChain(), BasePtr,
                             LD->getPointerInfo(), LD->getAlign());
    Lo = DAG.getNode(ISD::BSWAP, dl, MVT::i32, Lo);
    BasePtr = DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr,
                          DAG.getIntPtrConstant(4, dl));
    MachineMemOperand *NewMMO = DAG.getMachineFunction().getMachineMemOperand(
        LD->getMemOperand(), 4, 4);
    SDValue Hi = DAG.getLoad(MVT::i32, dl, LD->getChain(), BasePtr, NewMMO);
    Hi = DAG.getNode(ISD::BSWAP, dl, MVT::i32, Hi);
    SDValue Res;
    if (Subtarget.isLittleEndian())
      Res = DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Hi, Lo);
    else
      Res = DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);
    SDValue TF =
        DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                    Hi.getOperand(0).getValue(1), Lo.getOperand(0).getValue(1));
    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), TF);
    return Res;
  }
  case PPCISD::VCMP:
    // If a VCMP_rec node already exists with exactly the same operands as this
    // node, use its result instead of this node (VCMP_rec computes both a CR6
    // and a normal output).
    //
    if (!N->getOperand(0).hasOneUse() &&
        !N->getOperand(1).hasOneUse() &&
        !N->getOperand(2).hasOneUse()) {

      // Scan all of the users of the LHS, looking for VCMP_rec's that match.
      SDNode *VCMPrecNode = nullptr;

      SDNode *LHSN = N->getOperand(0).getNode();
      for (SDNode::use_iterator UI = LHSN->use_begin(), E = LHSN->use_end();
           UI != E; ++UI)
        if (UI->getOpcode() == PPCISD::VCMP_rec &&
            UI->getOperand(1) == N->getOperand(1) &&
            UI->getOperand(2) == N->getOperand(2) &&
            UI->getOperand(0) == N->getOperand(0)) {
          VCMPrecNode = *UI;
          break;
        }

      // If there is no VCMP_rec node, or if the flag value has a single use,
      // don't transform this.
      if (!VCMPrecNode || VCMPrecNode->hasNUsesOfValue(0, 1))
        break;

      // Look at the (necessarily single) use of the flag value.  If it has a
      // chain, this transformation is more complex.  Note that multiple things
      // could use the value result, which we should ignore.
      SDNode *FlagUser = nullptr;
      for (SDNode::use_iterator UI = VCMPrecNode->use_begin();
           FlagUser == nullptr; ++UI) {
        assert(UI != VCMPrecNode->use_end() && "Didn't find user!");
        SDNode *User = *UI;
        for (unsigned i = 0, e = User->getNumOperands(); i != e; ++i) {
          if (User->getOperand(i) == SDValue(VCMPrecNode, 1)) {
            FlagUser = User;
            break;
          }
        }
      }

      // If the user is a MFOCRF instruction, we know this is safe.
      // Otherwise we give up for right now.
      if (FlagUser->getOpcode() == PPCISD::MFOCRF)
        return SDValue(VCMPrecNode, 0);
    }
    break;
  case ISD::BR_CC: {
    // If this is a branch on an altivec predicate comparison, lower this so
    // that we don't have to do a MFOCRF: instead, branch directly on CR6.  This
    // lowering is done pre-legalize, because the legalizer lowers the predicate
    // compare down to code that is difficult to reassemble.
    // This code also handles branches that depend on the result of a store
    // conditional.
    ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(1))->get();
    SDValue LHS = N->getOperand(2), RHS = N->getOperand(3);

    int CompareOpc;
    bool isDot;

    if (!isa<ConstantSDNode>(RHS) || (CC != ISD::SETEQ && CC != ISD::SETNE))
      break;

    // Since we are doing this pre-legalize, the RHS can be a constant of
    // arbitrary bitwidth which may cause issues when trying to get the value
    // from the underlying APInt.
    auto RHSAPInt = RHS->getAsAPIntVal();
    if (!RHSAPInt.isIntN(64))
      break;

    unsigned Val = RHSAPInt.getZExtValue();
    auto isImpossibleCompare = [&]() {
      // If this is a comparison against something other than 0/1, then we know
      // that the condition is never/always true.
      if (Val != 0 && Val != 1) {
        if (CC == ISD::SETEQ)      // Cond never true, remove branch.
          return N->getOperand(0);
        // Always !=, turn it into an unconditional branch.
        return DAG.getNode(ISD::BR, dl, MVT::Other,
                           N->getOperand(0), N->getOperand(4));
      }
      return SDValue();
    };
    // Combine branches fed by store conditional instructions (st[bhwd]cx).
    unsigned StoreWidth = 0;
    if (LHS.getOpcode() == ISD::INTRINSIC_W_CHAIN &&
        isStoreConditional(LHS, StoreWidth)) {
      if (SDValue Impossible = isImpossibleCompare())
        return Impossible;
      PPC::Predicate CompOpc;
      // eq 0 => ne
      // ne 0 => eq
      // eq 1 => eq
      // ne 1 => ne
      if (Val == 0)
        CompOpc = CC == ISD::SETEQ ? PPC::PRED_NE : PPC::PRED_EQ;
      else
        CompOpc = CC == ISD::SETEQ ? PPC::PRED_EQ : PPC::PRED_NE;

      SDValue Ops[] = {LHS.getOperand(0), LHS.getOperand(2), LHS.getOperand(3),
                       DAG.getConstant(StoreWidth, dl, MVT::i32)};
      auto *MemNode = cast<MemSDNode>(LHS);
      SDValue ConstSt = DAG.getMemIntrinsicNode(
          PPCISD::STORE_COND, dl,
          DAG.getVTList(MVT::i32, MVT::Other, MVT::Glue), Ops,
          MemNode->getMemoryVT(), MemNode->getMemOperand());

      SDValue InChain;
      // Unchain the branch from the original store conditional.
      if (N->getOperand(0) == LHS.getValue(1))
        InChain = LHS.getOperand(0);
      else if (N->getOperand(0).getOpcode() == ISD::TokenFactor) {
        SmallVector<SDValue, 4> InChains;
        SDValue InTF = N->getOperand(0);
        for (int i = 0, e = InTF.getNumOperands(); i < e; i++)
          if (InTF.getOperand(i) != LHS.getValue(1))
            InChains.push_back(InTF.getOperand(i));
        InChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, InChains);
      }

      return DAG.getNode(PPCISD::COND_BRANCH, dl, MVT::Other, InChain,
                         DAG.getConstant(CompOpc, dl, MVT::i32),
                         DAG.getRegister(PPC::CR0, MVT::i32), N->getOperand(4),
                         ConstSt.getValue(2));
    }

    if (LHS.getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
        getVectorCompareInfo(LHS, CompareOpc, isDot, Subtarget)) {
      assert(isDot && "Can't compare against a vector result!");

      if (SDValue Impossible = isImpossibleCompare())
        return Impossible;

      bool BranchOnWhenPredTrue = (CC == ISD::SETEQ) ^ (Val == 0);
      // Create the PPCISD altivec 'dot' comparison node.
      SDValue Ops[] = {
        LHS.getOperand(2),  // LHS of compare
        LHS.getOperand(3),  // RHS of compare
        DAG.getConstant(CompareOpc, dl, MVT::i32)
      };
      EVT VTs[] = { LHS.getOperand(2).getValueType(), MVT::Glue };
      SDValue CompNode = DAG.getNode(PPCISD::VCMP_rec, dl, VTs, Ops);

      // Unpack the result based on how the target uses it.
      PPC::Predicate CompOpc;
      switch (LHS.getConstantOperandVal(1)) {
      default:  // Can't happen, don't crash on invalid number though.
      case 0:   // Branch on the value of the EQ bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_EQ : PPC::PRED_NE;
        break;
      case 1:   // Branch on the inverted value of the EQ bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_NE : PPC::PRED_EQ;
        break;
      case 2:   // Branch on the value of the LT bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_LT : PPC::PRED_GE;
        break;
      case 3:   // Branch on the inverted value of the LT bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_GE : PPC::PRED_LT;
        break;
      }

      return DAG.getNode(PPCISD::COND_BRANCH, dl, MVT::Other, N->getOperand(0),
                         DAG.getConstant(CompOpc, dl, MVT::i32),
                         DAG.getRegister(PPC::CR6, MVT::i32),
                         N->getOperand(4), CompNode.getValue(1));
    }
    break;
  }
  case ISD::BUILD_VECTOR:
    return DAGCombineBuildVector(N, DCI);
  }

  return SDValue();
}

SDValue
PPCTargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                 SelectionDAG &DAG,
                                 SmallVectorImpl<SDNode *> &Created) const {
  // fold (sdiv X, pow2)
  EVT VT = N->getValueType(0);
  if (VT == MVT::i64 && !Subtarget.isPPC64())
    return SDValue();
  if ((VT != MVT::i32 && VT != MVT::i64) ||
      !(Divisor.isPowerOf2() || Divisor.isNegatedPowerOf2()))
    return SDValue();

  SDLoc DL(N);
  SDValue N0 = N->getOperand(0);

  bool IsNegPow2 = Divisor.isNegatedPowerOf2();
  unsigned Lg2 = (IsNegPow2 ? -Divisor : Divisor).countr_zero();
  SDValue ShiftAmt = DAG.getConstant(Lg2, DL, VT);

  SDValue Op = DAG.getNode(PPCISD::SRA_ADDZE, DL, VT, N0, ShiftAmt);
  Created.push_back(Op.getNode());

  if (IsNegPow2) {
    Op = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), Op);
    Created.push_back(Op.getNode());
  }

  return Op;
}

//===----------------------------------------------------------------------===//
// Inline Assembly Support
//===----------------------------------------------------------------------===//

void PPCTargetLowering::computeKnownBitsForTargetNode(const SDValue Op,
                                                      KnownBits &Known,
                                                      const APInt &DemandedElts,
                                                      const SelectionDAG &DAG,
                                                      unsigned Depth) const {
  Known.resetAll();
  switch (Op.getOpcode()) {
  default: break;
  case PPCISD::LBRX: {
    // lhbrx is known to have the top bits cleared out.
    if (cast<VTSDNode>(Op.getOperand(2))->getVT() == MVT::i16)
      Known.Zero = 0xFFFF0000;
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    switch (Op.getConstantOperandVal(0)) {
    default: break;
    case Intrinsic::ppc_altivec_vcmpbfp_p:
    case Intrinsic::ppc_altivec_vcmpeqfp_p:
    case Intrinsic::ppc_altivec_vcmpequb_p:
    case Intrinsic::ppc_altivec_vcmpequh_p:
    case Intrinsic::ppc_altivec_vcmpequw_p:
    case Intrinsic::ppc_altivec_vcmpequd_p:
    case Intrinsic::ppc_altivec_vcmpequq_p:
    case Intrinsic::ppc_altivec_vcmpgefp_p:
    case Intrinsic::ppc_altivec_vcmpgtfp_p:
    case Intrinsic::ppc_altivec_vcmpgtsb_p:
    case Intrinsic::ppc_altivec_vcmpgtsh_p:
    case Intrinsic::ppc_altivec_vcmpgtsw_p:
    case Intrinsic::ppc_altivec_vcmpgtsd_p:
    case Intrinsic::ppc_altivec_vcmpgtsq_p:
    case Intrinsic::ppc_altivec_vcmpgtub_p:
    case Intrinsic::ppc_altivec_vcmpgtuh_p:
    case Intrinsic::ppc_altivec_vcmpgtuw_p:
    case Intrinsic::ppc_altivec_vcmpgtud_p:
    case Intrinsic::ppc_altivec_vcmpgtuq_p:
      Known.Zero = ~1U;  // All bits but the low one are known to be zero.
      break;
    }
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    switch (Op.getConstantOperandVal(1)) {
    default:
      break;
    case Intrinsic::ppc_load2r:
      // Top bits are cleared for load2r (which is the same as lhbrx).
      Known.Zero = 0xFFFF0000;
      break;
    }
    break;
  }
  }
}

Align PPCTargetLowering::getPrefLoopAlignment(MachineLoop *ML) const {
  switch (Subtarget.getCPUDirective()) {
  default: break;
  case PPC::DIR_970:
  case PPC::DIR_PWR4:
  case PPC::DIR_PWR5:
  case PPC::DIR_PWR5X:
  case PPC::DIR_PWR6:
  case PPC::DIR_PWR6X:
  case PPC::DIR_PWR7:
  case PPC::DIR_PWR8:
  case PPC::DIR_PWR9:
  case PPC::DIR_PWR10:
  case PPC::DIR_PWR11:
  case PPC::DIR_PWR_FUTURE: {
    if (!ML)
      break;

    if (!DisableInnermostLoopAlign32) {
      // If the nested loop is an innermost loop, prefer to a 32-byte alignment,
      // so that we can decrease cache misses and branch-prediction misses.
      // Actual alignment of the loop will depend on the hotness check and other
      // logic in alignBlocks.
      if (ML->getLoopDepth() > 1 && ML->getSubLoops().empty())
        return Align(32);
    }

    const PPCInstrInfo *TII = Subtarget.getInstrInfo();

    // For small loops (between 5 and 8 instructions), align to a 32-byte
    // boundary so that the entire loop fits in one instruction-cache line.
    uint64_t LoopSize = 0;
    for (auto I = ML->block_begin(), IE = ML->block_end(); I != IE; ++I)
      for (const MachineInstr &J : **I) {
        LoopSize += TII->getInstSizeInBytes(J);
        if (LoopSize > 32)
          break;
      }

    if (LoopSize > 16 && LoopSize <= 32)
      return Align(32);

    break;
  }
  }

  return TargetLowering::getPrefLoopAlignment(ML);
}

/// getConstraintType - Given a constraint, return the type of
/// constraint it is for this target.
PPCTargetLowering::ConstraintType
PPCTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default: break;
    case 'b':
    case 'r':
    case 'f':
    case 'd':
    case 'v':
    case 'y':
      return C_RegisterClass;
    case 'Z':
      // FIXME: While Z does indicate a memory constraint, it specifically
      // indicates an r+r address (used in conjunction with the 'y' modifier
      // in the replacement string). Currently, we're forcing the base
      // register to be r0 in the asm printer (which is interpreted as zero)
      // and forming the complete address in the second register. This is
      // suboptimal.
      return C_Memory;
    }
  } else if (Constraint == "wc") { // individual CR bits.
    return C_RegisterClass;
  } else if (Constraint == "wa" || Constraint == "wd" ||
             Constraint == "wf" || Constraint == "ws" ||
             Constraint == "wi" || Constraint == "ww") {
    return C_RegisterClass; // VSX registers.
  }
  return TargetLowering::getConstraintType(Constraint);
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
PPCTargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &info, const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
    // If we don't have a value, we can't do a match,
    // but allow it at the lowest weight.
  if (!CallOperandVal)
    return CW_Default;
  Type *type = CallOperandVal->getType();

  // Look at the constraint type.
  if (StringRef(constraint) == "wc" && type->isIntegerTy(1))
    return CW_Register; // an individual CR bit.
  else if ((StringRef(constraint) == "wa" ||
            StringRef(constraint) == "wd" ||
            StringRef(constraint) == "wf") &&
           type->isVectorTy())
    return CW_Register;
  else if (StringRef(constraint) == "wi" && type->isIntegerTy(64))
    return CW_Register; // just hold 64-bit integers data.
  else if (StringRef(constraint) == "ws" && type->isDoubleTy())
    return CW_Register;
  else if (StringRef(constraint) == "ww" && type->isFloatTy())
    return CW_Register;

  switch (*constraint) {
  default:
    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;
  case 'b':
    if (type->isIntegerTy())
      weight = CW_Register;
    break;
  case 'f':
    if (type->isFloatTy())
      weight = CW_Register;
    break;
  case 'd':
    if (type->isDoubleTy())
      weight = CW_Register;
    break;
  case 'v':
    if (type->isVectorTy())
      weight = CW_Register;
    break;
  case 'y':
    weight = CW_Register;
    break;
  case 'Z':
    weight = CW_Memory;
    break;
  }
  return weight;
}

std::pair<unsigned, const TargetRegisterClass *>
PPCTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                StringRef Constraint,
                                                MVT VT) const {
  if (Constraint.size() == 1) {
    // GCC RS6000 Constraint Letters
    switch (Constraint[0]) {
    case 'b':   // R1-R31
      if (VT == MVT::i64 && Subtarget.isPPC64())
        return std::make_pair(0U, &PPC::G8RC_NOX0RegClass);
      return std::make_pair(0U, &PPC::GPRC_NOR0RegClass);
    case 'r':   // R0-R31
      if (VT == MVT::i64 && Subtarget.isPPC64())
        return std::make_pair(0U, &PPC::G8RCRegClass);
      return std::make_pair(0U, &PPC::GPRCRegClass);
    // 'd' and 'f' constraints are both defined to be "the floating point
    // registers", where one is for 32-bit and the other for 64-bit. We don't
    // really care overly much here so just give them all the same reg classes.
    case 'd':
    case 'f':
      if (Subtarget.hasSPE()) {
        if (VT == MVT::f32 || VT == MVT::i32)
          return std::make_pair(0U, &PPC::GPRCRegClass);
        if (VT == MVT::f64 || VT == MVT::i64)
          return std::make_pair(0U, &PPC::SPERCRegClass);
      } else {
        if (VT == MVT::f32 || VT == MVT::i32)
          return std::make_pair(0U, &PPC::F4RCRegClass);
        if (VT == MVT::f64 || VT == MVT::i64)
          return std::make_pair(0U, &PPC::F8RCRegClass);
      }
      break;
    case 'v':
      if (Subtarget.hasAltivec() && VT.isVector())
        return std::make_pair(0U, &PPC::VRRCRegClass);
      else if (Subtarget.hasVSX())
        // Scalars in Altivec registers only make sense with VSX.
        return std::make_pair(0U, &PPC::VFRCRegClass);
      break;
    case 'y':   // crrc
      return std::make_pair(0U, &PPC::CRRCRegClass);
    }
  } else if (Constraint == "wc" && Subtarget.useCRBits()) {
    // An individual CR bit.
    return std::make_pair(0U, &PPC::CRBITRCRegClass);
  } else if ((Constraint == "wa" || Constraint == "wd" ||
             Constraint == "wf" || Constraint == "wi") &&
             Subtarget.hasVSX()) {
    // A VSX register for either a scalar (FP) or vector. There is no
    // support for single precision scalars on subtargets prior to Power8.
    if (VT.isVector())
      return std::make_pair(0U, &PPC::VSRCRegClass);
    if (VT == MVT::f32 && Subtarget.hasP8Vector())
      return std::make_pair(0U, &PPC::VSSRCRegClass);
    return std::make_pair(0U, &PPC::VSFRCRegClass);
  } else if ((Constraint == "ws" || Constraint == "ww") && Subtarget.hasVSX()) {
    if (VT == MVT::f32 && Subtarget.hasP8Vector())
      return std::make_pair(0U, &PPC::VSSRCRegClass);
    else
      return std::make_pair(0U, &PPC::VSFRCRegClass);
  } else if (Constraint == "lr") {
    if (VT == MVT::i64)
      return std::make_pair(0U, &PPC::LR8RCRegClass);
    else
      return std::make_pair(0U, &PPC::LRRCRegClass);
  }

  // Handle special cases of physical registers that are not properly handled
  // by the base class.
  if (Constraint[0] == '{' && Constraint[Constraint.size() - 1] == '}') {
    // If we name a VSX register, we can't defer to the base class because it
    // will not recognize the correct register (their names will be VSL{0-31}
    // and V{0-31} so they won't match). So we match them here.
    if (Constraint.size() > 3 && Constraint[1] == 'v' && Constraint[2] == 's') {
      int VSNum = atoi(Constraint.data() + 3);
      assert(VSNum >= 0 && VSNum <= 63 &&
             "Attempted to access a vsr out of range");
      if (VSNum < 32)
        return std::make_pair(PPC::VSL0 + VSNum, &PPC::VSRCRegClass);
      return std::make_pair(PPC::V0 + VSNum - 32, &PPC::VSRCRegClass);
    }

    // For float registers, we can't defer to the base class as it will match
    // the SPILLTOVSRRC class.
    if (Constraint.size() > 3 && Constraint[1] == 'f') {
      int RegNum = atoi(Constraint.data() + 2);
      if (RegNum > 31 || RegNum < 0)
        report_fatal_error("Invalid floating point register number");
      if (VT == MVT::f32 || VT == MVT::i32)
        return Subtarget.hasSPE()
                   ? std::make_pair(PPC::R0 + RegNum, &PPC::GPRCRegClass)
                   : std::make_pair(PPC::F0 + RegNum, &PPC::F4RCRegClass);
      if (VT == MVT::f64 || VT == MVT::i64)
        return Subtarget.hasSPE()
                   ? std::make_pair(PPC::S0 + RegNum, &PPC::SPERCRegClass)
                   : std::make_pair(PPC::F0 + RegNum, &PPC::F8RCRegClass);
    }
  }

  std::pair<unsigned, const TargetRegisterClass *> R =
      TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);

  // r[0-9]+ are used, on PPC64, to refer to the corresponding 64-bit registers
  // (which we call X[0-9]+). If a 64-bit value has been requested, and a
  // 32-bit GPR has been selected, then 'upgrade' it to the 64-bit parent
  // register.
  // FIXME: If TargetLowering::getRegForInlineAsmConstraint could somehow use
  // the AsmName field from *RegisterInfo.td, then this would not be necessary.
  if (R.first && VT == MVT::i64 && Subtarget.isPPC64() &&
      PPC::GPRCRegClass.contains(R.first))
    return std::make_pair(TRI->getMatchingSuperReg(R.first,
                            PPC::sub_32, &PPC::G8RCRegClass),
                          &PPC::G8RCRegClass);

  // GCC accepts 'cc' as an alias for 'cr0', and we need to do the same.
  if (!R.second && StringRef("{cc}").equals_insensitive(Constraint)) {
    R.first = PPC::CR0;
    R.second = &PPC::CRRCRegClass;
  }
  // FIXME: This warning should ideally be emitted in the front end.
  const auto &TM = getTargetMachine();
  if (Subtarget.isAIXABI() && !TM.getAIXExtendedAltivecABI()) {
    if (((R.first >= PPC::V20 && R.first <= PPC::V31) ||
         (R.first >= PPC::VF20 && R.first <= PPC::VF31)) &&
        (R.second == &PPC::VSRCRegClass || R.second == &PPC::VSFRCRegClass))
      errs() << "warning: vector registers 20 to 32 are reserved in the "
                "default AIX AltiVec ABI and cannot be used\n";
  }

  return R;
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void PPCTargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                     StringRef Constraint,
                                                     std::vector<SDValue> &Ops,
                                                     SelectionDAG &DAG) const {
  SDValue Result;

  // Only support length 1 constraints.
  if (Constraint.size() > 1)
    return;

  char Letter = Constraint[0];
  switch (Letter) {
  default: break;
  case 'I':
  case 'J':
  case 'K':
  case 'L':
  case 'M':
  case 'N':
  case 'O':
  case 'P': {
    ConstantSDNode *CST = dyn_cast<ConstantSDNode>(Op);
    if (!CST) return; // Must be an immediate to match.
    SDLoc dl(Op);
    int64_t Value = CST->getSExtValue();
    EVT TCVT = MVT::i64; // All constants taken to be 64 bits so that negative
                         // numbers are printed as such.
    switch (Letter) {
    default: llvm_unreachable("Unknown constraint letter!");
    case 'I':  // "I" is a signed 16-bit constant.
      if (isInt<16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'J':  // "J" is a constant with only the high-order 16 bits nonzero.
      if (isShiftedUInt<16, 16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'L':  // "L" is a signed 16-bit constant shifted left 16 bits.
      if (isShiftedInt<16, 16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'K':  // "K" is a constant with only the low-order 16 bits nonzero.
      if (isUInt<16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'M':  // "M" is a constant that is greater than 31.
      if (Value > 31)
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'N':  // "N" is a positive constant that is an exact power of two.
      if (Value > 0 && isPowerOf2_64(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'O':  // "O" is the constant zero.
      if (Value == 0)
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'P':  // "P" is a constant whose negation is a signed 16-bit constant.
      if (isInt<16>(-Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    }
    break;
  }
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }

  // Handle standard constraint letters.
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

void PPCTargetLowering::CollectTargetIntrinsicOperands(const CallInst &I,
                                              SmallVectorImpl<SDValue> &Ops,
                                              SelectionDAG &DAG) const {
  if (I.getNumOperands() <= 1)
    return;
  if (!isa<ConstantSDNode>(Ops[1].getNode()))
    return;
  auto IntrinsicID = Ops[1].getNode()->getAsZExtVal();
  if (IntrinsicID != Intrinsic::ppc_tdw && IntrinsicID != Intrinsic::ppc_tw &&
      IntrinsicID != Intrinsic::ppc_trapd && IntrinsicID != Intrinsic::ppc_trap)
    return;

  if (MDNode *MDN = I.getMetadata(LLVMContext::MD_annotation))
    Ops.push_back(DAG.getMDNode(MDN));
}

// isLegalAddressingMode - Return true if the addressing mode represented
// by AM is legal for this target, for a load/store of the specified type.
bool PPCTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                              const AddrMode &AM, Type *Ty,
                                              unsigned AS,
                                              Instruction *I) const {
  // Vector type r+i form is supported since power9 as DQ form. We don't check
  // the offset matching DQ form requirement(off % 16 == 0), because on PowerPC,
  // imm form is preferred and the offset can be adjusted to use imm form later
  // in pass PPCLoopInstrFormPrep. Also in LSR, for one LSRUse, it uses min and
  // max offset to check legal addressing mode, we should be a little aggressive
  // to contain other offsets for that LSRUse.
  if (Ty->isVectorTy() && AM.BaseOffs != 0 && !Subtarget.hasP9Vector())
    return false;

  // PPC allows a sign-extended 16-bit immediate field.
  if (AM.BaseOffs <= -(1LL << 16) || AM.BaseOffs >= (1LL << 16)-1)
    return false;

  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  // PPC only support r+r,
  switch (AM.Scale) {
  case 0:  // "r+i" or just "i", depending on HasBaseReg.
    break;
  case 1:
    if (AM.HasBaseReg && AM.BaseOffs)  // "r+r+i" is not allowed.
      return false;
    // Otherwise we have r+r or r+i.
    break;
  case 2:
    if (AM.HasBaseReg || AM.BaseOffs)  // 2*r+r  or  2*r+i is not allowed.
      return false;
    // Allow 2*r as r+r.
    break;
  default:
    // No other scales are supported.
    return false;
  }

  return true;
}

SDValue PPCTargetLowering::LowerRETURNADDR(SDValue Op,
                                           SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  SDLoc dl(Op);
  unsigned Depth = Op.getConstantOperandVal(0);

  // Make sure the function does not optimize away the store of the RA to
  // the stack.
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setLRStoreRequired();
  bool isPPC64 = Subtarget.isPPC64();
  auto PtrVT = getPointerTy(MF.getDataLayout());

  if (Depth > 0) {
    // The link register (return address) is saved in the caller's frame
    // not the callee's stack frame. So we must get the caller's frame
    // address and load the return address at the LR offset from there.
    SDValue FrameAddr =
        DAG.getLoad(Op.getValueType(), dl, DAG.getEntryNode(),
                    LowerFRAMEADDR(Op, DAG), MachinePointerInfo());
    SDValue Offset =
        DAG.getConstant(Subtarget.getFrameLowering()->getReturnSaveOffset(), dl,
                        isPPC64 ? MVT::i64 : MVT::i32);
    return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, PtrVT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Just load the return address off the stack.
  SDValue RetAddrFI = getReturnAddrFrameIndex(DAG);
  return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), RetAddrFI,
                     MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerFRAMEADDR(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDLoc dl(Op);
  unsigned Depth = Op.getConstantOperandVal(0);

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT PtrVT = getPointerTy(MF.getDataLayout());
  bool isPPC64 = PtrVT == MVT::i64;

  // Naked functions never have a frame pointer, and so we use r1. For all
  // other functions, this decision must be delayed until during PEI.
  unsigned FrameReg;
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    FrameReg = isPPC64 ? PPC::X1 : PPC::R1;
  else
    FrameReg = isPPC64 ? PPC::FP8 : PPC::FP;

  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl, FrameReg,
                                         PtrVT);
  while (Depth--)
    FrameAddr = DAG.getLoad(Op.getValueType(), dl, DAG.getEntryNode(),
                            FrameAddr, MachinePointerInfo());
  return FrameAddr;
}

// FIXME? Maybe this could be a TableGen attribute on some registers and
// this table could be generated automatically from RegInfo.
Register PPCTargetLowering::getRegisterByName(const char* RegName, LLT VT,
                                              const MachineFunction &MF) const {
  bool isPPC64 = Subtarget.isPPC64();

  bool is64Bit = isPPC64 && VT == LLT::scalar(64);
  if (!is64Bit && VT != LLT::scalar(32))
    report_fatal_error("Invalid register global variable type");

  Register Reg = StringSwitch<Register>(RegName)
                     .Case("r1", is64Bit ? PPC::X1 : PPC::R1)
                     .Case("r2", isPPC64 ? Register() : PPC::R2)
                     .Case("r13", (is64Bit ? PPC::X13 : PPC::R13))
                     .Default(Register());

  if (Reg)
    return Reg;
  report_fatal_error("Invalid register name global variable");
}

bool PPCTargetLowering::isAccessedAsGotIndirect(SDValue GA) const {
  // 32-bit SVR4 ABI access everything as got-indirect.
  if (Subtarget.is32BitELFABI())
    return true;

  // AIX accesses everything indirectly through the TOC, which is similar to
  // the GOT.
  if (Subtarget.isAIXABI())
    return true;

  CodeModel::Model CModel = getTargetMachine().getCodeModel();
  // If it is small or large code model, module locals are accessed
  // indirectly by loading their address from .toc/.got.
  if (CModel == CodeModel::Small || CModel == CodeModel::Large)
    return true;

  // JumpTable and BlockAddress are accessed as got-indirect.
  if (isa<JumpTableSDNode>(GA) || isa<BlockAddressSDNode>(GA))
    return true;

  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(GA))
    return Subtarget.isGVIndirectSymbol(G->getGlobal());

  return false;
}

bool
PPCTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The PowerPC target isn't yet aware of offsets.
  return false;
}

bool PPCTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                           const CallInst &I,
                                           MachineFunction &MF,
                                           unsigned Intrinsic) const {
  switch (Intrinsic) {
  case Intrinsic::ppc_atomicrmw_xchg_i128:
  case Intrinsic::ppc_atomicrmw_add_i128:
  case Intrinsic::ppc_atomicrmw_sub_i128:
  case Intrinsic::ppc_atomicrmw_nand_i128:
  case Intrinsic::ppc_atomicrmw_and_i128:
  case Intrinsic::ppc_atomicrmw_or_i128:
  case Intrinsic::ppc_atomicrmw_xor_i128:
  case Intrinsic::ppc_cmpxchg_i128:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i128;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Align(16);
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore |
                 MachineMemOperand::MOVolatile;
    return true;
  case Intrinsic::ppc_atomic_load_i128:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i128;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Align(16);
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
    return true;
  case Intrinsic::ppc_atomic_store_i128:
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::i128;
    Info.ptrVal = I.getArgOperand(2);
    Info.offset = 0;
    Info.align = Align(16);
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;
  case Intrinsic::ppc_altivec_lvx:
  case Intrinsic::ppc_altivec_lvxl:
  case Intrinsic::ppc_altivec_lvebx:
  case Intrinsic::ppc_altivec_lvehx:
  case Intrinsic::ppc_altivec_lvewx:
  case Intrinsic::ppc_vsx_lxvd2x:
  case Intrinsic::ppc_vsx_lxvw4x:
  case Intrinsic::ppc_vsx_lxvd2x_be:
  case Intrinsic::ppc_vsx_lxvw4x_be:
  case Intrinsic::ppc_vsx_lxvl:
  case Intrinsic::ppc_vsx_lxvll: {
    EVT VT;
    switch (Intrinsic) {
    case Intrinsic::ppc_altivec_lvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_lvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_lvewx:
      VT = MVT::i32;
      break;
    case Intrinsic::ppc_vsx_lxvd2x:
    case Intrinsic::ppc_vsx_lxvd2x_be:
      VT = MVT::v2f64;
      break;
    default:
      VT = MVT::v4i32;
      break;
    }

    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = VT;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = -VT.getStoreSize()+1;
    Info.size = 2*VT.getStoreSize()-1;
    Info.align = Align(1);
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::ppc_altivec_stvx:
  case Intrinsic::ppc_altivec_stvxl:
  case Intrinsic::ppc_altivec_stvebx:
  case Intrinsic::ppc_altivec_stvehx:
  case Intrinsic::ppc_altivec_stvewx:
  case Intrinsic::ppc_vsx_stxvd2x:
  case Intrinsic::ppc_vsx_stxvw4x:
  case Intrinsic::ppc_vsx_stxvd2x_be:
  case Intrinsic::ppc_vsx_stxvw4x_be:
  case Intrinsic::ppc_vsx_stxvl:
  case Intrinsic::ppc_vsx_stxvll: {
    EVT VT;
    switch (Intrinsic) {
    case Intrinsic::ppc_altivec_stvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_stvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_stvewx:
      VT = MVT::i32;
      break;
    case Intrinsic::ppc_vsx_stxvd2x:
    case Intrinsic::ppc_vsx_stxvd2x_be:
      VT = MVT::v2f64;
      break;
    default:
      VT = MVT::v4i32;
      break;
    }

    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = VT;
    Info.ptrVal = I.getArgOperand(1);
    Info.offset = -VT.getStoreSize()+1;
    Info.size = 2*VT.getStoreSize()-1;
    Info.align = Align(1);
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::ppc_stdcx:
  case Intrinsic::ppc_stwcx:
  case Intrinsic::ppc_sthcx:
  case Intrinsic::ppc_stbcx: {
    EVT VT;
    auto Alignment = Align(8);
    switch (Intrinsic) {
    case Intrinsic::ppc_stdcx:
      VT = MVT::i64;
      break;
    case Intrinsic::ppc_stwcx:
      VT = MVT::i32;
      Alignment = Align(4);
      break;
    case Intrinsic::ppc_sthcx:
      VT = MVT::i16;
      Alignment = Align(2);
      break;
    case Intrinsic::ppc_stbcx:
      VT = MVT::i8;
      Alignment = Align(1);
      break;
    }
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = VT;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Alignment;
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;
  }
  default:
    break;
  }

  return false;
}

/// It returns EVT::Other if the type should be determined using generic
/// target-independent logic.
EVT PPCTargetLowering::getOptimalMemOpType(
    const MemOp &Op, const AttributeList &FuncAttributes) const {
  if (getTargetMachine().getOptLevel() != CodeGenOptLevel::None) {
    // We should use Altivec/VSX loads and stores when available. For unaligned
    // addresses, unaligned VSX loads are only fast starting with the P8.
    if (Subtarget.hasAltivec() && Op.size() >= 16) {
      if (Op.isMemset() && Subtarget.hasVSX()) {
        uint64_t TailSize = Op.size() % 16;
        // For memset lowering, EXTRACT_VECTOR_ELT tries to return constant
        // element if vector element type matches tail store. For tail size
        // 3/4, the tail store is i32, v4i32 cannot be used, need a legal one.
        if (TailSize > 2 && TailSize <= 4) {
          return MVT::v8i16;
        }
        return MVT::v4i32;
      }
      if (Op.isAligned(Align(16)) || Subtarget.hasP8Vector())
        return MVT::v4i32;
    }
  }

  if (Subtarget.isPPC64()) {
    return MVT::i64;
  }

  return MVT::i32;
}

/// Returns true if it is beneficial to convert a load of a constant
/// to just the constant itself.
bool PPCTargetLowering::shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                          Type *Ty) const {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  return !(BitSize == 0 || BitSize > 64);
}

bool PPCTargetLowering::isTruncateFree(Type *Ty1, Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;
  unsigned NumBits1 = Ty1->getPrimitiveSizeInBits();
  unsigned NumBits2 = Ty2->getPrimitiveSizeInBits();
  return NumBits1 == 64 && NumBits2 == 32;
}

bool PPCTargetLowering::isTruncateFree(EVT VT1, EVT VT2) const {
  if (!VT1.isInteger() || !VT2.isInteger())
    return false;
  unsigned NumBits1 = VT1.getSizeInBits();
  unsigned NumBits2 = VT2.getSizeInBits();
  return NumBits1 == 64 && NumBits2 == 32;
}

bool PPCTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  // Generally speaking, zexts are not free, but they are free when they can be
  // folded with other operations.
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(Val)) {
    EVT MemVT = LD->getMemoryVT();
    if ((MemVT == MVT::i1 || MemVT == MVT::i8 || MemVT == MVT::i16 ||
         (Subtarget.isPPC64() && MemVT == MVT::i32)) &&
        (LD->getExtensionType() == ISD::NON_EXTLOAD ||
         LD->getExtensionType() == ISD::ZEXTLOAD))
      return true;
  }

  // FIXME: Add other cases...
  //  - 32-bit shifts with a zext to i64
  //  - zext after ctlz, bswap, etc.
  //  - zext after and by a constant mask

  return TargetLowering::isZExtFree(Val, VT2);
}

bool PPCTargetLowering::isFPExtFree(EVT DestVT, EVT SrcVT) const {
  assert(DestVT.isFloatingPoint() && SrcVT.isFloatingPoint() &&
         "invalid fpext types");
  // Extending to float128 is not free.
  if (DestVT == MVT::f128)
    return false;
  return true;
}

bool PPCTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  return isInt<16>(Imm) || isUInt<16>(Imm);
}

bool PPCTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  return isInt<16>(Imm) || isUInt<16>(Imm);
}

bool PPCTargetLowering::allowsMisalignedMemoryAccesses(EVT VT, unsigned, Align,
                                                       MachineMemOperand::Flags,
                                                       unsigned *Fast) const {
  if (DisablePPCUnaligned)
    return false;

  // PowerPC supports unaligned memory access for simple non-vector types.
  // Although accessing unaligned addresses is not as efficient as accessing
  // aligned addresses, it is generally more efficient than manual expansion,
  // and generally only traps for software emulation when crossing page
  // boundaries.

  if (!VT.isSimple())
    return false;

  if (VT.isFloatingPoint() && !VT.isVector() &&
      !Subtarget.allowsUnalignedFPAccess())
    return false;

  if (VT.getSimpleVT().isVector()) {
    if (Subtarget.hasVSX()) {
      if (VT != MVT::v2f64 && VT != MVT::v2i64 &&
          VT != MVT::v4f32 && VT != MVT::v4i32)
        return false;
    } else {
      return false;
    }
  }

  if (VT == MVT::ppcf128)
    return false;

  if (Fast)
    *Fast = 1;

  return true;
}

bool PPCTargetLowering::decomposeMulByConstant(LLVMContext &Context, EVT VT,
                                               SDValue C) const {
  // Check integral scalar types.
  if (!VT.isScalarInteger())
    return false;
  if (auto *ConstNode = dyn_cast<ConstantSDNode>(C.getNode())) {
    if (!ConstNode->getAPIntValue().isSignedIntN(64))
      return false;
    // This transformation will generate >= 2 operations. But the following
    // cases will generate <= 2 instructions during ISEL. So exclude them.
    // 1. If the constant multiplier fits 16 bits, it can be handled by one
    // HW instruction, ie. MULLI
    // 2. If the multiplier after shifted fits 16 bits, an extra shift
    // instruction is needed than case 1, ie. MULLI and RLDICR
    int64_t Imm = ConstNode->getSExtValue();
    unsigned Shift = llvm::countr_zero<uint64_t>(Imm);
    Imm >>= Shift;
    if (isInt<16>(Imm))
      return false;
    uint64_t UImm = static_cast<uint64_t>(Imm);
    if (isPowerOf2_64(UImm + 1) || isPowerOf2_64(UImm - 1) ||
        isPowerOf2_64(1 - UImm) || isPowerOf2_64(-1 - UImm))
      return true;
  }
  return false;
}

bool PPCTargetLowering::isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                                   EVT VT) const {
  return isFMAFasterThanFMulAndFAdd(
      MF.getFunction(), VT.getTypeForEVT(MF.getFunction().getContext()));
}

bool PPCTargetLowering::isFMAFasterThanFMulAndFAdd(const Function &F,
                                                   Type *Ty) const {
  if (Subtarget.hasSPE() || Subtarget.useSoftFloat())
    return false;
  switch (Ty->getScalarType()->getTypeID()) {
  case Type::FloatTyID:
  case Type::DoubleTyID:
    return true;
  case Type::FP128TyID:
    return Subtarget.hasP9Vector();
  default:
    return false;
  }
}

// FIXME: add more patterns which are not profitable to hoist.
bool PPCTargetLowering::isProfitableToHoist(Instruction *I) const {
  if (!I->hasOneUse())
    return true;

  Instruction *User = I->user_back();
  assert(User && "A single use instruction with no uses.");

  switch (I->getOpcode()) {
  case Instruction::FMul: {
    // Don't break FMA, PowerPC prefers FMA.
    if (User->getOpcode() != Instruction::FSub &&
        User->getOpcode() != Instruction::FAdd)
      return true;

    const TargetOptions &Options = getTargetMachine().Options;
    const Function *F = I->getFunction();
    const DataLayout &DL = F->getDataLayout();
    Type *Ty = User->getOperand(0)->getType();

    return !(
        isFMAFasterThanFMulAndFAdd(*F, Ty) &&
        isOperationLegalOrCustom(ISD::FMA, getValueType(DL, Ty)) &&
        (Options.AllowFPOpFusion == FPOpFusion::Fast || Options.UnsafeFPMath));
  }
  case Instruction::Load: {
    // Don't break "store (load float*)" pattern, this pattern will be combined
    // to "store (load int32)" in later InstCombine pass. See function
    // combineLoadToOperationType. On PowerPC, loading a float point takes more
    // cycles than loading a 32 bit integer.
    LoadInst *LI = cast<LoadInst>(I);
    // For the loads that combineLoadToOperationType does nothing, like
    // ordered load, it should be profitable to hoist them.
    // For swifterror load, it can only be used for pointer to pointer type, so
    // later type check should get rid of this case.
    if (!LI->isUnordered())
      return true;

    if (User->getOpcode() != Instruction::Store)
      return true;

    if (I->getType()->getTypeID() != Type::FloatTyID)
      return true;

    return false;
  }
  default:
    return true;
  }
  return true;
}

const MCPhysReg *
PPCTargetLowering::getScratchRegisters(CallingConv::ID) const {
  // LR is a callee-save register, but we must treat it as clobbered by any call
  // site. Hence we include LR in the scratch registers, which are in turn added
  // as implicit-defs for stackmaps and patchpoints. The same reasoning applies
  // to CTR, which is used by any indirect call.
  static const MCPhysReg ScratchRegs[] = {
    PPC::X12, PPC::LR8, PPC::CTR8, 0
  };

  return ScratchRegs;
}

Register PPCTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return Subtarget.isPPC64() ? PPC::X3 : PPC::R3;
}

Register PPCTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return Subtarget.isPPC64() ? PPC::X4 : PPC::R4;
}

bool
PPCTargetLowering::shouldExpandBuildVectorWithShuffles(
                     EVT VT , unsigned DefinedValues) const {
  if (VT == MVT::v2i64)
    return Subtarget.hasDirectMove(); // Don't need stack ops with direct moves

  if (Subtarget.hasVSX())
    return true;

  return TargetLowering::shouldExpandBuildVectorWithShuffles(VT, DefinedValues);
}

Sched::Preference PPCTargetLowering::getSchedulingPreference(SDNode *N) const {
  if (DisableILPPref || Subtarget.enableMachineScheduler())
    return TargetLowering::getSchedulingPreference(N);

  return Sched::ILP;
}

// Create a fast isel object.
FastISel *
PPCTargetLowering::createFastISel(FunctionLoweringInfo &FuncInfo,
                                  const TargetLibraryInfo *LibInfo) const {
  return PPC::createFastISel(FuncInfo, LibInfo);
}

// 'Inverted' means the FMA opcode after negating one multiplicand.
// For example, (fma -a b c) = (fnmsub a b c)
static unsigned invertFMAOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Invalid FMA opcode for PowerPC!");
  case ISD::FMA:
    return PPCISD::FNMSUB;
  case PPCISD::FNMSUB:
    return ISD::FMA;
  }
}

SDValue PPCTargetLowering::getNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                                bool LegalOps, bool OptForSize,
                                                NegatibleCost &Cost,
                                                unsigned Depth) const {
  if (Depth > SelectionDAG::MaxRecursionDepth)
    return SDValue();

  unsigned Opc = Op.getOpcode();
  EVT VT = Op.getValueType();
  SDNodeFlags Flags = Op.getNode()->getFlags();

  switch (Opc) {
  case PPCISD::FNMSUB:
    if (!Op.hasOneUse() || !isTypeLegal(VT))
      break;

    const TargetOptions &Options = getTargetMachine().Options;
    SDValue N0 = Op.getOperand(0);
    SDValue N1 = Op.getOperand(1);
    SDValue N2 = Op.getOperand(2);
    SDLoc Loc(Op);

    NegatibleCost N2Cost = NegatibleCost::Expensive;
    SDValue NegN2 =
        getNegatedExpression(N2, DAG, LegalOps, OptForSize, N2Cost, Depth + 1);

    if (!NegN2)
      return SDValue();

    // (fneg (fnmsub a b c)) => (fnmsub (fneg a) b (fneg c))
    // (fneg (fnmsub a b c)) => (fnmsub a (fneg b) (fneg c))
    // These transformations may change sign of zeroes. For example,
    // -(-ab-(-c))=-0 while -(-(ab-c))=+0 when a=b=c=1.
    if (Flags.hasNoSignedZeros() || Options.NoSignedZerosFPMath) {
      // Try and choose the cheaper one to negate.
      NegatibleCost N0Cost = NegatibleCost::Expensive;
      SDValue NegN0 = getNegatedExpression(N0, DAG, LegalOps, OptForSize,
                                           N0Cost, Depth + 1);

      NegatibleCost N1Cost = NegatibleCost::Expensive;
      SDValue NegN1 = getNegatedExpression(N1, DAG, LegalOps, OptForSize,
                                           N1Cost, Depth + 1);

      if (NegN0 && N0Cost <= N1Cost) {
        Cost = std::min(N0Cost, N2Cost);
        return DAG.getNode(Opc, Loc, VT, NegN0, N1, NegN2, Flags);
      } else if (NegN1) {
        Cost = std::min(N1Cost, N2Cost);
        return DAG.getNode(Opc, Loc, VT, N0, NegN1, NegN2, Flags);
      }
    }

    // (fneg (fnmsub a b c)) => (fma a b (fneg c))
    if (isOperationLegal(ISD::FMA, VT)) {
      Cost = N2Cost;
      return DAG.getNode(ISD::FMA, Loc, VT, N0, N1, NegN2, Flags);
    }

    break;
  }

  return TargetLowering::getNegatedExpression(Op, DAG, LegalOps, OptForSize,
                                              Cost, Depth);
}

// Override to enable LOAD_STACK_GUARD lowering on Linux.
bool PPCTargetLowering::useLoadStackGuardNode() const {
  if (!Subtarget.isTargetLinux())
    return TargetLowering::useLoadStackGuardNode();
  return true;
}

// Override to disable global variable loading on Linux and insert AIX canary
// word declaration.
void PPCTargetLowering::insertSSPDeclarations(Module &M) const {
  if (Subtarget.isAIXABI()) {
    M.getOrInsertGlobal(AIXSSPCanaryWordName,
                        PointerType::getUnqual(M.getContext()));
    return;
  }
  if (!Subtarget.isTargetLinux())
    return TargetLowering::insertSSPDeclarations(M);
}

Value *PPCTargetLowering::getSDagStackGuard(const Module &M) const {
  if (Subtarget.isAIXABI())
    return M.getGlobalVariable(AIXSSPCanaryWordName);
  return TargetLowering::getSDagStackGuard(M);
}

bool PPCTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT,
                                     bool ForCodeSize) const {
  if (!VT.isSimple() || !Subtarget.hasVSX())
    return false;

  switch(VT.getSimpleVT().SimpleTy) {
  default:
    // For FP types that are currently not supported by PPC backend, return
    // false. Examples: f16, f80.
    return false;
  case MVT::f32:
  case MVT::f64: {
    if (Subtarget.hasPrefixInstrs() && Subtarget.hasP10Vector()) {
      // we can materialize all immediatess via XXSPLTI32DX and XXSPLTIDP.
      return true;
    }
    bool IsExact;
    APSInt IntResult(16, false);
    // The rounding mode doesn't really matter because we only care about floats
    // that can be converted to integers exactly.
    Imm.convertToInteger(IntResult, APFloat::rmTowardZero, &IsExact);
    // For exact values in the range [-16, 15] we can materialize the float.
    if (IsExact && IntResult <= 15 && IntResult >= -16)
      return true;
    return Imm.isZero();
  }
  case MVT::ppcf128:
    return Imm.isPosZero();
  }
}

// For vector shift operation op, fold
// (op x, (and y, ((1 << numbits(x)) - 1))) -> (target op x, y)
static SDValue stripModuloOnShift(const TargetLowering &TLI, SDNode *N,
                                  SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  unsigned OpSizeInBits = VT.getScalarSizeInBits();
  unsigned Opcode = N->getOpcode();
  unsigned TargetOpcode;

  switch (Opcode) {
  default:
    llvm_unreachable("Unexpected shift operation");
  case ISD::SHL:
    TargetOpcode = PPCISD::SHL;
    break;
  case ISD::SRL:
    TargetOpcode = PPCISD::SRL;
    break;
  case ISD::SRA:
    TargetOpcode = PPCISD::SRA;
    break;
  }

  if (VT.isVector() && TLI.isOperationLegal(Opcode, VT) &&
      N1->getOpcode() == ISD::AND)
    if (ConstantSDNode *Mask = isConstOrConstSplat(N1->getOperand(1)))
      if (Mask->getZExtValue() == OpSizeInBits - 1)
        return DAG.getNode(TargetOpcode, SDLoc(N), VT, N0, N1->getOperand(0));

  return SDValue();
}

SDValue PPCTargetLowering::combineSHL(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = stripModuloOnShift(*this, N, DCI.DAG))
    return Value;

  SDValue N0 = N->getOperand(0);
  ConstantSDNode *CN1 = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!Subtarget.isISA3_0() || !Subtarget.isPPC64() ||
      N0.getOpcode() != ISD::SIGN_EXTEND ||
      N0.getOperand(0).getValueType() != MVT::i32 || CN1 == nullptr ||
      N->getValueType(0) != MVT::i64)
    return SDValue();

  // We can't save an operation here if the value is already extended, and
  // the existing shift is easier to combine.
  SDValue ExtsSrc = N0.getOperand(0);
  if (ExtsSrc.getOpcode() == ISD::TRUNCATE &&
      ExtsSrc.getOperand(0).getOpcode() == ISD::AssertSext)
    return SDValue();

  SDLoc DL(N0);
  SDValue ShiftBy = SDValue(CN1, 0);
  // We want the shift amount to be i32 on the extswli, but the shift could
  // have an i64.
  if (ShiftBy.getValueType() == MVT::i64)
    ShiftBy = DCI.DAG.getConstant(CN1->getZExtValue(), DL, MVT::i32);

  return DCI.DAG.getNode(PPCISD::EXTSWSLI, DL, MVT::i64, N0->getOperand(0),
                         ShiftBy);
}

SDValue PPCTargetLowering::combineSRA(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = stripModuloOnShift(*this, N, DCI.DAG))
    return Value;

  return SDValue();
}

SDValue PPCTargetLowering::combineSRL(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = stripModuloOnShift(*this, N, DCI.DAG))
    return Value;

  return SDValue();
}

// Transform (add X, (zext(setne Z, C))) -> (addze X, (addic (addi Z, -C), -1))
// Transform (add X, (zext(sete  Z, C))) -> (addze X, (subfic (addi Z, -C), 0))
// When C is zero, the equation (addi Z, -C) can be simplified to Z
// Requirement: -C in [-32768, 32767], X and Z are MVT::i64 types
static SDValue combineADDToADDZE(SDNode *N, SelectionDAG &DAG,
                                 const PPCSubtarget &Subtarget) {
  if (!Subtarget.isPPC64())
    return SDValue();

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  auto isZextOfCompareWithConstant = [](SDValue Op) {
    if (Op.getOpcode() != ISD::ZERO_EXTEND || !Op.hasOneUse() ||
        Op.getValueType() != MVT::i64)
      return false;

    SDValue Cmp = Op.getOperand(0);
    if (Cmp.getOpcode() != ISD::SETCC || !Cmp.hasOneUse() ||
        Cmp.getOperand(0).getValueType() != MVT::i64)
      return false;

    if (auto *Constant = dyn_cast<ConstantSDNode>(Cmp.getOperand(1))) {
      int64_t NegConstant = 0 - Constant->getSExtValue();
      // Due to the limitations of the addi instruction,
      // -C is required to be [-32768, 32767].
      return isInt<16>(NegConstant);
    }

    return false;
  };

  bool LHSHasPattern = isZextOfCompareWithConstant(LHS);
  bool RHSHasPattern = isZextOfCompareWithConstant(RHS);

  // If there is a pattern, canonicalize a zext operand to the RHS.
  if (LHSHasPattern && !RHSHasPattern)
    std::swap(LHS, RHS);
  else if (!LHSHasPattern && !RHSHasPattern)
    return SDValue();

  SDLoc DL(N);
  SDVTList VTs = DAG.getVTList(MVT::i64, MVT::Glue);
  SDValue Cmp = RHS.getOperand(0);
  SDValue Z = Cmp.getOperand(0);
  auto *Constant = cast<ConstantSDNode>(Cmp.getOperand(1));
  int64_t NegConstant = 0 - Constant->getSExtValue();

  switch(cast<CondCodeSDNode>(Cmp.getOperand(2))->get()) {
  default: break;
  case ISD::SETNE: {
    //                                 when C == 0
    //                             --> addze X, (addic Z, -1).carry
    //                            /
    // add X, (zext(setne Z, C))--
    //                            \    when -32768 <= -C <= 32767 && C != 0
    //                             --> addze X, (addic (addi Z, -C), -1).carry
    SDValue Add = DAG.getNode(ISD::ADD, DL, MVT::i64, Z,
                              DAG.getConstant(NegConstant, DL, MVT::i64));
    SDValue AddOrZ = NegConstant != 0 ? Add : Z;
    SDValue Addc = DAG.getNode(ISD::ADDC, DL, DAG.getVTList(MVT::i64, MVT::Glue),
                               AddOrZ, DAG.getConstant(-1ULL, DL, MVT::i64));
    return DAG.getNode(ISD::ADDE, DL, VTs, LHS, DAG.getConstant(0, DL, MVT::i64),
                       SDValue(Addc.getNode(), 1));
    }
  case ISD::SETEQ: {
    //                                 when C == 0
    //                             --> addze X, (subfic Z, 0).carry
    //                            /
    // add X, (zext(sete  Z, C))--
    //                            \    when -32768 <= -C <= 32767 && C != 0
    //                             --> addze X, (subfic (addi Z, -C), 0).carry
    SDValue Add = DAG.getNode(ISD::ADD, DL, MVT::i64, Z,
                              DAG.getConstant(NegConstant, DL, MVT::i64));
    SDValue AddOrZ = NegConstant != 0 ? Add : Z;
    SDValue Subc = DAG.getNode(ISD::SUBC, DL, DAG.getVTList(MVT::i64, MVT::Glue),
                               DAG.getConstant(0, DL, MVT::i64), AddOrZ);
    return DAG.getNode(ISD::ADDE, DL, VTs, LHS, DAG.getConstant(0, DL, MVT::i64),
                       SDValue(Subc.getNode(), 1));
    }
  }

  return SDValue();
}

// Transform
// (add C1, (MAT_PCREL_ADDR GlobalAddr+C2)) to
// (MAT_PCREL_ADDR GlobalAddr+(C1+C2))
// In this case both C1 and C2 must be known constants.
// C1+C2 must fit into a 34 bit signed integer.
static SDValue combineADDToMAT_PCREL_ADDR(SDNode *N, SelectionDAG &DAG,
                                          const PPCSubtarget &Subtarget) {
  if (!Subtarget.isUsingPCRelativeCalls())
    return SDValue();

  // Check both Operand 0 and Operand 1 of the ADD node for the PCRel node.
  // If we find that node try to cast the Global Address and the Constant.
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  if (LHS.getOpcode() != PPCISD::MAT_PCREL_ADDR)
    std::swap(LHS, RHS);

  if (LHS.getOpcode() != PPCISD::MAT_PCREL_ADDR)
    return SDValue();

  // Operand zero of PPCISD::MAT_PCREL_ADDR is the GA node.
  GlobalAddressSDNode *GSDN = dyn_cast<GlobalAddressSDNode>(LHS.getOperand(0));
  ConstantSDNode* ConstNode = dyn_cast<ConstantSDNode>(RHS);

  // Check that both casts succeeded.
  if (!GSDN || !ConstNode)
    return SDValue();

  int64_t NewOffset = GSDN->getOffset() + ConstNode->getSExtValue();
  SDLoc DL(GSDN);

  // The signed int offset needs to fit in 34 bits.
  if (!isInt<34>(NewOffset))
    return SDValue();

  // The new global address is a copy of the old global address except
  // that it has the updated Offset.
  SDValue GA =
      DAG.getTargetGlobalAddress(GSDN->getGlobal(), DL, GSDN->getValueType(0),
                                 NewOffset, GSDN->getTargetFlags());
  SDValue MatPCRel =
      DAG.getNode(PPCISD::MAT_PCREL_ADDR, DL, GSDN->getValueType(0), GA);
  return MatPCRel;
}

SDValue PPCTargetLowering::combineADD(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = combineADDToADDZE(N, DCI.DAG, Subtarget))
    return Value;

  if (auto Value = combineADDToMAT_PCREL_ADDR(N, DCI.DAG, Subtarget))
    return Value;

  return SDValue();
}

// Detect TRUNCATE operations on bitcasts of float128 values.
// What we are looking for here is the situtation where we extract a subset
// of bits from a 128 bit float.
// This can be of two forms:
// 1) BITCAST of f128 feeding TRUNCATE
// 2) BITCAST of f128 feeding SRL (a shift) feeding TRUNCATE
// The reason this is required is because we do not have a legal i128 type
// and so we want to prevent having to store the f128 and then reload part
// of it.
SDValue PPCTargetLowering::combineTRUNCATE(SDNode *N,
                                           DAGCombinerInfo &DCI) const {
  // If we are using CRBits then try that first.
  if (Subtarget.useCRBits()) {
    // Check if CRBits did anything and return that if it did.
    if (SDValue CRTruncValue = DAGCombineTruncBoolExt(N, DCI))
      return CRTruncValue;
  }

  SDLoc dl(N);
  SDValue Op0 = N->getOperand(0);

  // Looking for a truncate of i128 to i64.
  if (Op0.getValueType() != MVT::i128 || N->getValueType(0) != MVT::i64)
    return SDValue();

  int EltToExtract = DCI.DAG.getDataLayout().isBigEndian() ? 1 : 0;

  // SRL feeding TRUNCATE.
  if (Op0.getOpcode() == ISD::SRL) {
    ConstantSDNode *ConstNode = dyn_cast<ConstantSDNode>(Op0.getOperand(1));
    // The right shift has to be by 64 bits.
    if (!ConstNode || ConstNode->getZExtValue() != 64)
      return SDValue();

    // Switch the element number to extract.
    EltToExtract = EltToExtract ? 0 : 1;
    // Update Op0 past the SRL.
    Op0 = Op0.getOperand(0);
  }

  // BITCAST feeding a TRUNCATE possibly via SRL.
  if (Op0.getOpcode() == ISD::BITCAST &&
      Op0.getValueType() == MVT::i128 &&
      Op0.getOperand(0).getValueType() == MVT::f128) {
    SDValue Bitcast = DCI.DAG.getBitcast(MVT::v2i64, Op0.getOperand(0));
    return DCI.DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, dl, MVT::i64, Bitcast,
        DCI.DAG.getTargetConstant(EltToExtract, dl, MVT::i32));
  }
  return SDValue();
}

SDValue PPCTargetLowering::combineMUL(SDNode *N, DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;

  ConstantSDNode *ConstOpOrElement = isConstOrConstSplat(N->getOperand(1));
  if (!ConstOpOrElement)
    return SDValue();

  // An imul is usually smaller than the alternative sequence for legal type.
  if (DAG.getMachineFunction().getFunction().hasMinSize() &&
      isOperationLegal(ISD::MUL, N->getValueType(0)))
    return SDValue();

  auto IsProfitable = [this](bool IsNeg, bool IsAddOne, EVT VT) -> bool {
    switch (this->Subtarget.getCPUDirective()) {
    default:
      // TODO: enhance the condition for subtarget before pwr8
      return false;
    case PPC::DIR_PWR8:
      //  type        mul     add    shl
      // scalar        4       1      1
      // vector        7       2      2
      return true;
    case PPC::DIR_PWR9:
    case PPC::DIR_PWR10:
    case PPC::DIR_PWR11:
    case PPC::DIR_PWR_FUTURE:
      //  type        mul     add    shl
      // scalar        5       2      2
      // vector        7       2      2

      // The cycle RATIO of related operations are showed as a table above.
      // Because mul is 5(scalar)/7(vector), add/sub/shl are all 2 for both
      // scalar and vector type. For 2 instrs patterns, add/sub + shl
      // are 4, it is always profitable; but for 3 instrs patterns
      // (mul x, -(2^N + 1)) => -(add (shl x, N), x), sub + add + shl are 6.
      // So we should only do it for vector type.
      return IsAddOne && IsNeg ? VT.isVector() : true;
    }
  };

  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  const APInt &MulAmt = ConstOpOrElement->getAPIntValue();
  bool IsNeg = MulAmt.isNegative();
  APInt MulAmtAbs = MulAmt.abs();

  if ((MulAmtAbs - 1).isPowerOf2()) {
    // (mul x, 2^N + 1) => (add (shl x, N), x)
    // (mul x, -(2^N + 1)) => -(add (shl x, N), x)

    if (!IsProfitable(IsNeg, true, VT))
      return SDValue();

    SDValue Op0 = N->getOperand(0);
    SDValue Op1 =
        DAG.getNode(ISD::SHL, DL, VT, N->getOperand(0),
                    DAG.getConstant((MulAmtAbs - 1).logBase2(), DL, VT));
    SDValue Res = DAG.getNode(ISD::ADD, DL, VT, Op0, Op1);

    if (!IsNeg)
      return Res;

    return DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), Res);
  } else if ((MulAmtAbs + 1).isPowerOf2()) {
    // (mul x, 2^N - 1) => (sub (shl x, N), x)
    // (mul x, -(2^N - 1)) => (sub x, (shl x, N))

    if (!IsProfitable(IsNeg, false, VT))
      return SDValue();

    SDValue Op0 = N->getOperand(0);
    SDValue Op1 =
        DAG.getNode(ISD::SHL, DL, VT, N->getOperand(0),
                    DAG.getConstant((MulAmtAbs + 1).logBase2(), DL, VT));

    if (!IsNeg)
      return DAG.getNode(ISD::SUB, DL, VT, Op1, Op0);
    else
      return DAG.getNode(ISD::SUB, DL, VT, Op0, Op1);

  } else {
    return SDValue();
  }
}

// Combine fma-like op (like fnmsub) with fnegs to appropriate op. Do this
// in combiner since we need to check SD flags and other subtarget features.
SDValue PPCTargetLowering::combineFMALike(SDNode *N,
                                          DAGCombinerInfo &DCI) const {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDValue N2 = N->getOperand(2);
  SDNodeFlags Flags = N->getFlags();
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;
  const TargetOptions &Options = getTargetMachine().Options;
  unsigned Opc = N->getOpcode();
  bool CodeSize = DAG.getMachineFunction().getFunction().hasOptSize();
  bool LegalOps = !DCI.isBeforeLegalizeOps();
  SDLoc Loc(N);

  if (!isOperationLegal(ISD::FMA, VT))
    return SDValue();

  // Allowing transformation to FNMSUB may change sign of zeroes when ab-c=0
  // since (fnmsub a b c)=-0 while c-ab=+0.
  if (!Flags.hasNoSignedZeros() && !Options.NoSignedZerosFPMath)
    return SDValue();

  // (fma (fneg a) b c) => (fnmsub a b c)
  // (fnmsub (fneg a) b c) => (fma a b c)
  if (SDValue NegN0 = getCheaperNegatedExpression(N0, DAG, LegalOps, CodeSize))
    return DAG.getNode(invertFMAOpcode(Opc), Loc, VT, NegN0, N1, N2, Flags);

  // (fma a (fneg b) c) => (fnmsub a b c)
  // (fnmsub a (fneg b) c) => (fma a b c)
  if (SDValue NegN1 = getCheaperNegatedExpression(N1, DAG, LegalOps, CodeSize))
    return DAG.getNode(invertFMAOpcode(Opc), Loc, VT, N0, NegN1, N2, Flags);

  return SDValue();
}

bool PPCTargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  // Only duplicate to increase tail-calls for the 64bit SysV ABIs.
  if (!Subtarget.is64BitELFABI())
    return false;

  // If not a tail call then no need to proceed.
  if (!CI->isTailCall())
    return false;

  // If sibling calls have been disabled and tail-calls aren't guaranteed
  // there is no reason to duplicate.
  auto &TM = getTargetMachine();
  if (!TM.Options.GuaranteedTailCallOpt && DisableSCO)
    return false;

  // Can't tail call a function called indirectly, or if it has variadic args.
  const Function *Callee = CI->getCalledFunction();
  if (!Callee || Callee->isVarArg())
    return false;

  // Make sure the callee and caller calling conventions are eligible for tco.
  const Function *Caller = CI->getParent()->getParent();
  if (!areCallingConvEligibleForTCO_64SVR4(Caller->getCallingConv(),
                                           CI->getCallingConv()))
      return false;

  // If the function is local then we have a good chance at tail-calling it
  return getTargetMachine().shouldAssumeDSOLocal(Callee);
}

bool PPCTargetLowering::
isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const {
  const Value *Mask = AndI.getOperand(1);
  // If the mask is suitable for andi. or andis. we should sink the and.
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(Mask)) {
    // Can't handle constants wider than 64-bits.
    if (CI->getBitWidth() > 64)
      return false;
    int64_t ConstVal = CI->getZExtValue();
    return isUInt<16>(ConstVal) ||
      (isUInt<16>(ConstVal >> 16) && !(ConstVal & 0xFFFF));
  }

  // For non-constant masks, we can always use the record-form and.
  return true;
}

/// getAddrModeForFlags - Based on the set of address flags, select the most
/// optimal instruction format to match by.
PPC::AddrMode PPCTargetLowering::getAddrModeForFlags(unsigned Flags) const {
  // This is not a node we should be handling here.
  if (Flags == PPC::MOF_None)
    return PPC::AM_None;
  // Unaligned D-Forms are tried first, followed by the aligned D-Forms.
  for (auto FlagSet : AddrModesMap.at(PPC::AM_DForm))
    if ((Flags & FlagSet) == FlagSet)
      return PPC::AM_DForm;
  for (auto FlagSet : AddrModesMap.at(PPC::AM_DSForm))
    if ((Flags & FlagSet) == FlagSet)
      return PPC::AM_DSForm;
  for (auto FlagSet : AddrModesMap.at(PPC::AM_DQForm))
    if ((Flags & FlagSet) == FlagSet)
      return PPC::AM_DQForm;
  for (auto FlagSet : AddrModesMap.at(PPC::AM_PrefixDForm))
    if ((Flags & FlagSet) == FlagSet)
      return PPC::AM_PrefixDForm;
  // If no other forms are selected, return an X-Form as it is the most
  // general addressing mode.
  return PPC::AM_XForm;
}

/// Set alignment flags based on whether or not the Frame Index is aligned.
/// Utilized when computing flags for address computation when selecting
/// load and store instructions.
static void setAlignFlagsForFI(SDValue N, unsigned &FlagSet,
                               SelectionDAG &DAG) {
  bool IsAdd = ((N.getOpcode() == ISD::ADD) || (N.getOpcode() == ISD::OR));
  FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(IsAdd ? N.getOperand(0) : N);
  if (!FI)
    return;
  const MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  unsigned FrameIndexAlign = MFI.getObjectAlign(FI->getIndex()).value();
  // If this is (add $FI, $S16Imm), the alignment flags are already set
  // based on the immediate. We just need to clear the alignment flags
  // if the FI alignment is weaker.
  if ((FrameIndexAlign % 4) != 0)
    FlagSet &= ~PPC::MOF_RPlusSImm16Mult4;
  if ((FrameIndexAlign % 16) != 0)
    FlagSet &= ~PPC::MOF_RPlusSImm16Mult16;
  // If the address is a plain FrameIndex, set alignment flags based on
  // FI alignment.
  if (!IsAdd) {
    if ((FrameIndexAlign % 4) == 0)
      FlagSet |= PPC::MOF_RPlusSImm16Mult4;
    if ((FrameIndexAlign % 16) == 0)
      FlagSet |= PPC::MOF_RPlusSImm16Mult16;
  }
}

/// Given a node, compute flags that are used for address computation when
/// selecting load and store instructions. The flags computed are stored in
/// FlagSet. This function takes into account whether the node is a constant,
/// an ADD, OR, or a constant, and computes the address flags accordingly.
static void computeFlagsForAddressComputation(SDValue N, unsigned &FlagSet,
                                              SelectionDAG &DAG) {
  // Set the alignment flags for the node depending on if the node is
  // 4-byte or 16-byte aligned.
  auto SetAlignFlagsForImm = [&](uint64_t Imm) {
    if ((Imm & 0x3) == 0)
      FlagSet |= PPC::MOF_RPlusSImm16Mult4;
    if ((Imm & 0xf) == 0)
      FlagSet |= PPC::MOF_RPlusSImm16Mult16;
  };

  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N)) {
    // All 32-bit constants can be computed as LIS + Disp.
    const APInt &ConstImm = CN->getAPIntValue();
    if (ConstImm.isSignedIntN(32)) { // Flag to handle 32-bit constants.
      FlagSet |= PPC::MOF_AddrIsSImm32;
      SetAlignFlagsForImm(ConstImm.getZExtValue());
      setAlignFlagsForFI(N, FlagSet, DAG);
    }
    if (ConstImm.isSignedIntN(34)) // Flag to handle 34-bit constants.
      FlagSet |= PPC::MOF_RPlusSImm34;
    else // Let constant materialization handle large constants.
      FlagSet |= PPC::MOF_NotAddNorCst;
  } else if (N.getOpcode() == ISD::ADD || provablyDisjointOr(DAG, N)) {
    // This address can be represented as an addition of:
    // - Register + Imm16 (possibly a multiple of 4/16)
    // - Register + Imm34
    // - Register + PPCISD::Lo
    // - Register + Register
    // In any case, we won't have to match this as Base + Zero.
    SDValue RHS = N.getOperand(1);
    if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(RHS)) {
      const APInt &ConstImm = CN->getAPIntValue();
      if (ConstImm.isSignedIntN(16)) {
        FlagSet |= PPC::MOF_RPlusSImm16; // Signed 16-bit immediates.
        SetAlignFlagsForImm(ConstImm.getZExtValue());
        setAlignFlagsForFI(N, FlagSet, DAG);
      }
      if (ConstImm.isSignedIntN(34))
        FlagSet |= PPC::MOF_RPlusSImm34; // Signed 34-bit immediates.
      else
        FlagSet |= PPC::MOF_RPlusR; // Register.
    } else if (RHS.getOpcode() == PPCISD::Lo && !RHS.getConstantOperandVal(1))
      FlagSet |= PPC::MOF_RPlusLo; // PPCISD::Lo.
    else
      FlagSet |= PPC::MOF_RPlusR;
  } else { // The address computation is not a constant or an addition.
    setAlignFlagsForFI(N, FlagSet, DAG);
    FlagSet |= PPC::MOF_NotAddNorCst;
  }
}

static bool isPCRelNode(SDValue N) {
  return (N.getOpcode() == PPCISD::MAT_PCREL_ADDR ||
      isValidPCRelNode<ConstantPoolSDNode>(N) ||
      isValidPCRelNode<GlobalAddressSDNode>(N) ||
      isValidPCRelNode<JumpTableSDNode>(N) ||
      isValidPCRelNode<BlockAddressSDNode>(N));
}

/// computeMOFlags - Given a node N and it's Parent (a MemSDNode), compute
/// the address flags of the load/store instruction that is to be matched.
unsigned PPCTargetLowering::computeMOFlags(const SDNode *Parent, SDValue N,
                                           SelectionDAG &DAG) const {
  unsigned FlagSet = PPC::MOF_None;

  // Compute subtarget flags.
  if (!Subtarget.hasP9Vector())
    FlagSet |= PPC::MOF_SubtargetBeforeP9;
  else
    FlagSet |= PPC::MOF_SubtargetP9;

  if (Subtarget.hasPrefixInstrs())
    FlagSet |= PPC::MOF_SubtargetP10;

  if (Subtarget.hasSPE())
    FlagSet |= PPC::MOF_SubtargetSPE;

  // Check if we have a PCRel node and return early.
  if ((FlagSet & PPC::MOF_SubtargetP10) && isPCRelNode(N))
    return FlagSet;

  // If the node is the paired load/store intrinsics, compute flags for
  // address computation and return early.
  unsigned ParentOp = Parent->getOpcode();
  if (Subtarget.isISA3_1() && ((ParentOp == ISD::INTRINSIC_W_CHAIN) ||
                               (ParentOp == ISD::INTRINSIC_VOID))) {
    unsigned ID = Parent->getConstantOperandVal(1);
    if ((ID == Intrinsic::ppc_vsx_lxvp) || (ID == Intrinsic::ppc_vsx_stxvp)) {
      SDValue IntrinOp = (ID == Intrinsic::ppc_vsx_lxvp)
                             ? Parent->getOperand(2)
                             : Parent->getOperand(3);
      computeFlagsForAddressComputation(IntrinOp, FlagSet, DAG);
      FlagSet |= PPC::MOF_Vector;
      return FlagSet;
    }
  }

  // Mark this as something we don't want to handle here if it is atomic
  // or pre-increment instruction.
  if (const LSBaseSDNode *LSB = dyn_cast<LSBaseSDNode>(Parent))
    if (LSB->isIndexed())
      return PPC::MOF_None;

  // Compute in-memory type flags. This is based on if there are scalars,
  // floats or vectors.
  const MemSDNode *MN = dyn_cast<MemSDNode>(Parent);
  assert(MN && "Parent should be a MemSDNode!");
  EVT MemVT = MN->getMemoryVT();
  unsigned Size = MemVT.getSizeInBits();
  if (MemVT.isScalarInteger()) {
    assert(Size <= 128 &&
           "Not expecting scalar integers larger than 16 bytes!");
    if (Size < 32)
      FlagSet |= PPC::MOF_SubWordInt;
    else if (Size == 32)
      FlagSet |= PPC::MOF_WordInt;
    else
      FlagSet |= PPC::MOF_DoubleWordInt;
  } else if (MemVT.isVector() && !MemVT.isFloatingPoint()) { // Integer vectors.
    if (Size == 128)
      FlagSet |= PPC::MOF_Vector;
    else if (Size == 256) {
      assert(Subtarget.pairedVectorMemops() &&
             "256-bit vectors are only available when paired vector memops is "
             "enabled!");
      FlagSet |= PPC::MOF_Vector;
    } else
      llvm_unreachable("Not expecting illegal vectors!");
  } else { // Floating point type: can be scalar, f128 or vector types.
    if (Size == 32 || Size == 64)
      FlagSet |= PPC::MOF_ScalarFloat;
    else if (MemVT == MVT::f128 || MemVT.isVector())
      FlagSet |= PPC::MOF_Vector;
    else
      llvm_unreachable("Not expecting illegal scalar floats!");
  }

  // Compute flags for address computation.
  computeFlagsForAddressComputation(N, FlagSet, DAG);

  // Compute type extension flags.
  if (const LoadSDNode *LN = dyn_cast<LoadSDNode>(Parent)) {
    switch (LN->getExtensionType()) {
    case ISD::SEXTLOAD:
      FlagSet |= PPC::MOF_SExt;
      break;
    case ISD::EXTLOAD:
    case ISD::ZEXTLOAD:
      FlagSet |= PPC::MOF_ZExt;
      break;
    case ISD::NON_EXTLOAD:
      FlagSet |= PPC::MOF_NoExt;
      break;
    }
  } else
    FlagSet |= PPC::MOF_NoExt;

  // For integers, no extension is the same as zero extension.
  // We set the extension mode to zero extension so we don't have
  // to add separate entries in AddrModesMap for loads and stores.
  if (MemVT.isScalarInteger() && (FlagSet & PPC::MOF_NoExt)) {
    FlagSet |= PPC::MOF_ZExt;
    FlagSet &= ~PPC::MOF_NoExt;
  }

  // If we don't have prefixed instructions, 34-bit constants should be
  // treated as PPC::MOF_NotAddNorCst so they can match D-Forms.
  bool IsNonP1034BitConst =
      ((PPC::MOF_RPlusSImm34 | PPC::MOF_AddrIsSImm32 | PPC::MOF_SubtargetP10) &
       FlagSet) == PPC::MOF_RPlusSImm34;
  if (N.getOpcode() != ISD::ADD && N.getOpcode() != ISD::OR &&
      IsNonP1034BitConst)
    FlagSet |= PPC::MOF_NotAddNorCst;

  return FlagSet;
}

/// SelectForceXFormMode - Given the specified address, force it to be
/// represented as an indexed [r+r] operation (an XForm instruction).
PPC::AddrMode PPCTargetLowering::SelectForceXFormMode(SDValue N, SDValue &Disp,
                                                      SDValue &Base,
                                                      SelectionDAG &DAG) const {

  PPC::AddrMode Mode = PPC::AM_XForm;
  int16_t ForceXFormImm = 0;
  if (provablyDisjointOr(DAG, N) &&
      !isIntS16Immediate(N.getOperand(1), ForceXFormImm)) {
    Disp = N.getOperand(0);
    Base = N.getOperand(1);
    return Mode;
  }

  // If the address is the result of an add, we will utilize the fact that the
  // address calculation includes an implicit add.  However, we can reduce
  // register pressure if we do not materialize a constant just for use as the
  // index register.  We only get rid of the add if it is not an add of a
  // value and a 16-bit signed constant and both have a single use.
  if (N.getOpcode() == ISD::ADD &&
      (!isIntS16Immediate(N.getOperand(1), ForceXFormImm) ||
       !N.getOperand(1).hasOneUse() || !N.getOperand(0).hasOneUse())) {
    Disp = N.getOperand(0);
    Base = N.getOperand(1);
    return Mode;
  }

  // Otherwise, use R0 as the base register.
  Disp = DAG.getRegister(Subtarget.isPPC64() ? PPC::ZERO8 : PPC::ZERO,
                         N.getValueType());
  Base = N;

  return Mode;
}

bool PPCTargetLowering::splitValueIntoRegisterParts(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
    unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC) const {
  EVT ValVT = Val.getValueType();
  // If we are splitting a scalar integer into f64 parts (i.e. so they
  // can be placed into VFRC registers), we need to zero extend and
  // bitcast the values. This will ensure the value is placed into a
  // VSR using direct moves or stack operations as needed.
  if (PartVT == MVT::f64 &&
      (ValVT == MVT::i32 || ValVT == MVT::i16 || ValVT == MVT::i8)) {
    Val = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Val);
    Val = DAG.getNode(ISD::BITCAST, DL, MVT::f64, Val);
    Parts[0] = Val;
    return true;
  }
  return false;
}

SDValue PPCTargetLowering::lowerToLibCall(const char *LibCallName, SDValue Op,
                                          SelectionDAG &DAG) const {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  TargetLowering::CallLoweringInfo CLI(DAG);
  EVT RetVT = Op.getValueType();
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());
  SDValue Callee =
      DAG.getExternalSymbol(LibCallName, TLI.getPointerTy(DAG.getDataLayout()));
  bool SignExtend = TLI.shouldSignExtendTypeInLibCall(RetVT, false);
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (const SDValue &N : Op->op_values()) {
    EVT ArgVT = N.getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = N;
    Entry.Ty = ArgTy;
    Entry.IsSExt = TLI.shouldSignExtendTypeInLibCall(ArgVT, SignExtend);
    Entry.IsZExt = !Entry.IsSExt;
    Args.push_back(Entry);
  }

  SDValue InChain = DAG.getEntryNode();
  SDValue TCChain = InChain;
  const Function &F = DAG.getMachineFunction().getFunction();
  bool isTailCall =
      TLI.isInTailCallPosition(DAG, Op.getNode(), TCChain) &&
      (RetTy == F.getReturnType() || F.getReturnType()->isVoidTy());
  if (isTailCall)
    InChain = TCChain;
  CLI.setDebugLoc(SDLoc(Op))
      .setChain(InChain)
      .setLibCallee(CallingConv::C, RetTy, Callee, std::move(Args))
      .setTailCall(isTailCall)
      .setSExtResult(SignExtend)
      .setZExtResult(!SignExtend)
      .setIsPostTypeLegalization(true);
  return TLI.LowerCallTo(CLI).first;
}

SDValue PPCTargetLowering::lowerLibCallBasedOnType(
    const char *LibCallFloatName, const char *LibCallDoubleName, SDValue Op,
    SelectionDAG &DAG) const {
  if (Op.getValueType() == MVT::f32)
    return lowerToLibCall(LibCallFloatName, Op, DAG);

  if (Op.getValueType() == MVT::f64)
    return lowerToLibCall(LibCallDoubleName, Op, DAG);

  return SDValue();
}

bool PPCTargetLowering::isLowringToMASSFiniteSafe(SDValue Op) const {
  SDNodeFlags Flags = Op.getNode()->getFlags();
  return isLowringToMASSSafe(Op) && Flags.hasNoSignedZeros() &&
         Flags.hasNoNaNs() && Flags.hasNoInfs();
}

bool PPCTargetLowering::isLowringToMASSSafe(SDValue Op) const {
  return Op.getNode()->getFlags().hasApproximateFuncs();
}

bool PPCTargetLowering::isScalarMASSConversionEnabled() const {
  return getTargetMachine().Options.PPCGenScalarMASSEntries;
}

SDValue PPCTargetLowering::lowerLibCallBase(const char *LibCallDoubleName,
                                            const char *LibCallFloatName,
                                            const char *LibCallDoubleNameFinite,
                                            const char *LibCallFloatNameFinite,
                                            SDValue Op,
                                            SelectionDAG &DAG) const {
  if (!isScalarMASSConversionEnabled() || !isLowringToMASSSafe(Op))
    return SDValue();

  if (!isLowringToMASSFiniteSafe(Op))
    return lowerLibCallBasedOnType(LibCallFloatName, LibCallDoubleName, Op,
                                   DAG);

  return lowerLibCallBasedOnType(LibCallFloatNameFinite,
                                 LibCallDoubleNameFinite, Op, DAG);
}

SDValue PPCTargetLowering::lowerPow(SDValue Op, SelectionDAG &DAG) const {
  return lowerLibCallBase("__xl_pow", "__xl_powf", "__xl_pow_finite",
                          "__xl_powf_finite", Op, DAG);
}

SDValue PPCTargetLowering::lowerSin(SDValue Op, SelectionDAG &DAG) const {
  return lowerLibCallBase("__xl_sin", "__xl_sinf", "__xl_sin_finite",
                          "__xl_sinf_finite", Op, DAG);
}

SDValue PPCTargetLowering::lowerCos(SDValue Op, SelectionDAG &DAG) const {
  return lowerLibCallBase("__xl_cos", "__xl_cosf", "__xl_cos_finite",
                          "__xl_cosf_finite", Op, DAG);
}

SDValue PPCTargetLowering::lowerLog(SDValue Op, SelectionDAG &DAG) const {
  return lowerLibCallBase("__xl_log", "__xl_logf", "__xl_log_finite",
                          "__xl_logf_finite", Op, DAG);
}

SDValue PPCTargetLowering::lowerLog10(SDValue Op, SelectionDAG &DAG) const {
  return lowerLibCallBase("__xl_log10", "__xl_log10f", "__xl_log10_finite",
                          "__xl_log10f_finite", Op, DAG);
}

SDValue PPCTargetLowering::lowerExp(SDValue Op, SelectionDAG &DAG) const {
  return lowerLibCallBase("__xl_exp", "__xl_expf", "__xl_exp_finite",
                          "__xl_expf_finite", Op, DAG);
}

// If we happen to match to an aligned D-Form, check if the Frame Index is
// adequately aligned. If it is not, reset the mode to match to X-Form.
static void setXFormForUnalignedFI(SDValue N, unsigned Flags,
                                   PPC::AddrMode &Mode) {
  if (!isa<FrameIndexSDNode>(N))
    return;
  if ((Mode == PPC::AM_DSForm && !(Flags & PPC::MOF_RPlusSImm16Mult4)) ||
      (Mode == PPC::AM_DQForm && !(Flags & PPC::MOF_RPlusSImm16Mult16)))
    Mode = PPC::AM_XForm;
}

/// SelectOptimalAddrMode - Based on a node N and it's Parent (a MemSDNode),
/// compute the address flags of the node, get the optimal address mode based
/// on the flags, and set the Base and Disp based on the address mode.
PPC::AddrMode PPCTargetLowering::SelectOptimalAddrMode(const SDNode *Parent,
                                                       SDValue N, SDValue &Disp,
                                                       SDValue &Base,
                                                       SelectionDAG &DAG,
                                                       MaybeAlign Align) const {
  SDLoc DL(Parent);

  // Compute the address flags.
  unsigned Flags = computeMOFlags(Parent, N, DAG);

  // Get the optimal address mode based on the Flags.
  PPC::AddrMode Mode = getAddrModeForFlags(Flags);

  // If the address mode is DS-Form or DQ-Form, check if the FI is aligned.
  // Select an X-Form load if it is not.
  setXFormForUnalignedFI(N, Flags, Mode);

  // Set the mode to PC-Relative addressing mode if we have a valid PC-Rel node.
  if ((Mode == PPC::AM_XForm) && isPCRelNode(N)) {
    assert(Subtarget.isUsingPCRelativeCalls() &&
           "Must be using PC-Relative calls when a valid PC-Relative node is "
           "present!");
    Mode = PPC::AM_PCRel;
  }

  // Set Base and Disp accordingly depending on the address mode.
  switch (Mode) {
  case PPC::AM_DForm:
  case PPC::AM_DSForm:
  case PPC::AM_DQForm: {
    // This is a register plus a 16-bit immediate. The base will be the
    // register and the displacement will be the immediate unless it
    // isn't sufficiently aligned.
    if (Flags & PPC::MOF_RPlusSImm16) {
      SDValue Op0 = N.getOperand(0);
      SDValue Op1 = N.getOperand(1);
      int16_t Imm = Op1->getAsZExtVal();
      if (!Align || isAligned(*Align, Imm)) {
        Disp = DAG.getTargetConstant(Imm, DL, N.getValueType());
        Base = Op0;
        if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(Op0)) {
          Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
          fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
        }
        break;
      }
    }
    // This is a register plus the @lo relocation. The base is the register
    // and the displacement is the global address.
    else if (Flags & PPC::MOF_RPlusLo) {
      Disp = N.getOperand(1).getOperand(0); // The global address.
      assert(Disp.getOpcode() == ISD::TargetGlobalAddress ||
             Disp.getOpcode() == ISD::TargetGlobalTLSAddress ||
             Disp.getOpcode() == ISD::TargetConstantPool ||
             Disp.getOpcode() == ISD::TargetJumpTable);
      Base = N.getOperand(0);
      break;
    }
    // This is a constant address at most 32 bits. The base will be
    // zero or load-immediate-shifted and the displacement will be
    // the low 16 bits of the address.
    else if (Flags & PPC::MOF_AddrIsSImm32) {
      auto *CN = cast<ConstantSDNode>(N);
      EVT CNType = CN->getValueType(0);
      uint64_t CNImm = CN->getZExtValue();
      // If this address fits entirely in a 16-bit sext immediate field, codegen
      // this as "d, 0".
      int16_t Imm;
      if (isIntS16Immediate(CN, Imm) && (!Align || isAligned(*Align, Imm))) {
        Disp = DAG.getTargetConstant(Imm, DL, CNType);
        Base = DAG.getRegister(Subtarget.isPPC64() ? PPC::ZERO8 : PPC::ZERO,
                               CNType);
        break;
      }
      // Handle 32-bit sext immediate with LIS + Addr mode.
      if ((CNType == MVT::i32 || isInt<32>(CNImm)) &&
          (!Align || isAligned(*Align, CNImm))) {
        int32_t Addr = (int32_t)CNImm;
        // Otherwise, break this down into LIS + Disp.
        Disp = DAG.getTargetConstant((int16_t)Addr, DL, MVT::i32);
        Base =
            DAG.getTargetConstant((Addr - (int16_t)Addr) >> 16, DL, MVT::i32);
        uint32_t LIS = CNType == MVT::i32 ? PPC::LIS : PPC::LIS8;
        Base = SDValue(DAG.getMachineNode(LIS, DL, CNType, Base), 0);
        break;
      }
    }
    // Otherwise, the PPC:MOF_NotAdd flag is set. Load/Store is Non-foldable.
    Disp = DAG.getTargetConstant(0, DL, getPointerTy(DAG.getDataLayout()));
    if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N)) {
      Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
      fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
    } else
      Base = N;
    break;
  }
  case PPC::AM_PrefixDForm: {
    int64_t Imm34 = 0;
    unsigned Opcode = N.getOpcode();
    if (((Opcode == ISD::ADD) || (Opcode == ISD::OR)) &&
        (isIntS34Immediate(N.getOperand(1), Imm34))) {
      // N is an Add/OR Node, and it's operand is a 34-bit signed immediate.
      Disp = DAG.getTargetConstant(Imm34, DL, N.getValueType());
      if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N.getOperand(0)))
        Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
      else
        Base = N.getOperand(0);
    } else if (isIntS34Immediate(N, Imm34)) {
      // The address is a 34-bit signed immediate.
      Disp = DAG.getTargetConstant(Imm34, DL, N.getValueType());
      Base = DAG.getRegister(PPC::ZERO8, N.getValueType());
    }
    break;
  }
  case PPC::AM_PCRel: {
    // When selecting PC-Relative instructions, "Base" is not utilized as
    // we select the address as [PC+imm].
    Disp = N;
    break;
  }
  case PPC::AM_None:
    break;
  default: { // By default, X-Form is always available to be selected.
    // When a frame index is not aligned, we also match by XForm.
    FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N);
    Base = FI ? N : N.getOperand(1);
    Disp = FI ? DAG.getRegister(Subtarget.isPPC64() ? PPC::ZERO8 : PPC::ZERO,
                                N.getValueType())
              : N.getOperand(0);
    break;
  }
  }
  return Mode;
}

CCAssignFn *PPCTargetLowering::ccAssignFnForCall(CallingConv::ID CC,
                                                 bool Return,
                                                 bool IsVarArg) const {
  switch (CC) {
  case CallingConv::Cold:
    return (Return ? RetCC_PPC_Cold : CC_PPC64_ELF);
  default:
    return CC_PPC64_ELF;
  }
}

bool PPCTargetLowering::shouldInlineQuadwordAtomics() const {
  return Subtarget.isPPC64() && Subtarget.hasQuadwordAtomics();
}

TargetLowering::AtomicExpansionKind
PPCTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  unsigned Size = AI->getType()->getPrimitiveSizeInBits();
  if (shouldInlineQuadwordAtomics() && Size == 128)
    return AtomicExpansionKind::MaskedIntrinsic;

  switch (AI->getOperation()) {
  case AtomicRMWInst::UIncWrap:
  case AtomicRMWInst::UDecWrap:
    return AtomicExpansionKind::CmpXChg;
  default:
    return TargetLowering::shouldExpandAtomicRMWInIR(AI);
  }

  llvm_unreachable("unreachable atomicrmw operation");
}

TargetLowering::AtomicExpansionKind
PPCTargetLowering::shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const {
  unsigned Size = AI->getNewValOperand()->getType()->getPrimitiveSizeInBits();
  if (shouldInlineQuadwordAtomics() && Size == 128)
    return AtomicExpansionKind::MaskedIntrinsic;
  return TargetLowering::shouldExpandAtomicCmpXchgInIR(AI);
}

static Intrinsic::ID
getIntrinsicForAtomicRMWBinOp128(AtomicRMWInst::BinOp BinOp) {
  switch (BinOp) {
  default:
    llvm_unreachable("Unexpected AtomicRMW BinOp");
  case AtomicRMWInst::Xchg:
    return Intrinsic::ppc_atomicrmw_xchg_i128;
  case AtomicRMWInst::Add:
    return Intrinsic::ppc_atomicrmw_add_i128;
  case AtomicRMWInst::Sub:
    return Intrinsic::ppc_atomicrmw_sub_i128;
  case AtomicRMWInst::And:
    return Intrinsic::ppc_atomicrmw_and_i128;
  case AtomicRMWInst::Or:
    return Intrinsic::ppc_atomicrmw_or_i128;
  case AtomicRMWInst::Xor:
    return Intrinsic::ppc_atomicrmw_xor_i128;
  case AtomicRMWInst::Nand:
    return Intrinsic::ppc_atomicrmw_nand_i128;
  }
}

Value *PPCTargetLowering::emitMaskedAtomicRMWIntrinsic(
    IRBuilderBase &Builder, AtomicRMWInst *AI, Value *AlignedAddr, Value *Incr,
    Value *Mask, Value *ShiftAmt, AtomicOrdering Ord) const {
  assert(shouldInlineQuadwordAtomics() && "Only support quadword now");
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Type *ValTy = Incr->getType();
  assert(ValTy->getPrimitiveSizeInBits() == 128);
  Function *RMW = Intrinsic::getDeclaration(
      M, getIntrinsicForAtomicRMWBinOp128(AI->getOperation()));
  Type *Int64Ty = Type::getInt64Ty(M->getContext());
  Value *IncrLo = Builder.CreateTrunc(Incr, Int64Ty, "incr_lo");
  Value *IncrHi =
      Builder.CreateTrunc(Builder.CreateLShr(Incr, 64), Int64Ty, "incr_hi");
  Value *LoHi = Builder.CreateCall(RMW, {AlignedAddr, IncrLo, IncrHi});
  Value *Lo = Builder.CreateExtractValue(LoHi, 0, "lo");
  Value *Hi = Builder.CreateExtractValue(LoHi, 1, "hi");
  Lo = Builder.CreateZExt(Lo, ValTy, "lo64");
  Hi = Builder.CreateZExt(Hi, ValTy, "hi64");
  return Builder.CreateOr(
      Lo, Builder.CreateShl(Hi, ConstantInt::get(ValTy, 64)), "val64");
}

Value *PPCTargetLowering::emitMaskedAtomicCmpXchgIntrinsic(
    IRBuilderBase &Builder, AtomicCmpXchgInst *CI, Value *AlignedAddr,
    Value *CmpVal, Value *NewVal, Value *Mask, AtomicOrdering Ord) const {
  assert(shouldInlineQuadwordAtomics() && "Only support quadword now");
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Type *ValTy = CmpVal->getType();
  assert(ValTy->getPrimitiveSizeInBits() == 128);
  Function *IntCmpXchg =
      Intrinsic::getDeclaration(M, Intrinsic::ppc_cmpxchg_i128);
  Type *Int64Ty = Type::getInt64Ty(M->getContext());
  Value *CmpLo = Builder.CreateTrunc(CmpVal, Int64Ty, "cmp_lo");
  Value *CmpHi =
      Builder.CreateTrunc(Builder.CreateLShr(CmpVal, 64), Int64Ty, "cmp_hi");
  Value *NewLo = Builder.CreateTrunc(NewVal, Int64Ty, "new_lo");
  Value *NewHi =
      Builder.CreateTrunc(Builder.CreateLShr(NewVal, 64), Int64Ty, "new_hi");
  emitLeadingFence(Builder, CI, Ord);
  Value *LoHi =
      Builder.CreateCall(IntCmpXchg, {AlignedAddr, CmpLo, CmpHi, NewLo, NewHi});
  emitTrailingFence(Builder, CI, Ord);
  Value *Lo = Builder.CreateExtractValue(LoHi, 0, "lo");
  Value *Hi = Builder.CreateExtractValue(LoHi, 1, "hi");
  Lo = Builder.CreateZExt(Lo, ValTy, "lo64");
  Hi = Builder.CreateZExt(Hi, ValTy, "hi64");
  return Builder.CreateOr(
      Lo, Builder.CreateShl(Hi, ConstantInt::get(ValTy, 64)), "val64");
}
