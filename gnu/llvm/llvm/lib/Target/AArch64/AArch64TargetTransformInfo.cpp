//===-- AArch64TargetTransformInfo.cpp - AArch64 specific TTI -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetTransformInfo.h"
#include "AArch64ExpandImm.h"
#include "AArch64PerfectShuffle.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Vectorize/LoopVectorizationLegality.h"
#include <algorithm>
#include <optional>
using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "aarch64tti"

static cl::opt<bool> EnableFalkorHWPFUnrollFix("enable-falkor-hwpf-unroll-fix",
                                               cl::init(true), cl::Hidden);

static cl::opt<unsigned> SVEGatherOverhead("sve-gather-overhead", cl::init(10),
                                           cl::Hidden);

static cl::opt<unsigned> SVEScatterOverhead("sve-scatter-overhead",
                                            cl::init(10), cl::Hidden);

static cl::opt<unsigned> SVETailFoldInsnThreshold("sve-tail-folding-insn-threshold",
                                                  cl::init(15), cl::Hidden);

static cl::opt<unsigned>
    NeonNonConstStrideOverhead("neon-nonconst-stride-overhead", cl::init(10),
                               cl::Hidden);

static cl::opt<unsigned> CallPenaltyChangeSM(
    "call-penalty-sm-change", cl::init(5), cl::Hidden,
    cl::desc(
        "Penalty of calling a function that requires a change to PSTATE.SM"));

static cl::opt<unsigned> InlineCallPenaltyChangeSM(
    "inline-call-penalty-sm-change", cl::init(10), cl::Hidden,
    cl::desc("Penalty of inlining a call that requires a change to PSTATE.SM"));

static cl::opt<bool> EnableOrLikeSelectOpt("enable-aarch64-or-like-select",
                                           cl::init(true), cl::Hidden);

static cl::opt<bool> EnableLSRCostOpt("enable-aarch64-lsr-cost-opt",
                                      cl::init(true), cl::Hidden);

// A complete guess as to a reasonable cost.
static cl::opt<unsigned>
    BaseHistCntCost("aarch64-base-histcnt-cost", cl::init(8), cl::Hidden,
                    cl::desc("The cost of a histcnt instruction"));

namespace {
class TailFoldingOption {
  // These bitfields will only ever be set to something non-zero in operator=,
  // when setting the -sve-tail-folding option. This option should always be of
  // the form (default|simple|all|disable)[+(Flag1|Flag2|etc)], where here
  // InitialBits is one of (disabled|all|simple). EnableBits represents
  // additional flags we're enabling, and DisableBits for those flags we're
  // disabling. The default flag is tracked in the variable NeedsDefault, since
  // at the time of setting the option we may not know what the default value
  // for the CPU is.
  TailFoldingOpts InitialBits = TailFoldingOpts::Disabled;
  TailFoldingOpts EnableBits = TailFoldingOpts::Disabled;
  TailFoldingOpts DisableBits = TailFoldingOpts::Disabled;

  // This value needs to be initialised to true in case the user does not
  // explicitly set the -sve-tail-folding option.
  bool NeedsDefault = true;

  void setInitialBits(TailFoldingOpts Bits) { InitialBits = Bits; }

  void setNeedsDefault(bool V) { NeedsDefault = V; }

  void setEnableBit(TailFoldingOpts Bit) {
    EnableBits |= Bit;
    DisableBits &= ~Bit;
  }

  void setDisableBit(TailFoldingOpts Bit) {
    EnableBits &= ~Bit;
    DisableBits |= Bit;
  }

  TailFoldingOpts getBits(TailFoldingOpts DefaultBits) const {
    TailFoldingOpts Bits = TailFoldingOpts::Disabled;

    assert((InitialBits == TailFoldingOpts::Disabled || !NeedsDefault) &&
           "Initial bits should only include one of "
           "(disabled|all|simple|default)");
    Bits = NeedsDefault ? DefaultBits : InitialBits;
    Bits |= EnableBits;
    Bits &= ~DisableBits;

    return Bits;
  }

  void reportError(std::string Opt) {
    errs() << "invalid argument '" << Opt
           << "' to -sve-tail-folding=; the option should be of the form\n"
              "  (disabled|all|default|simple)[+(reductions|recurrences"
              "|reverse|noreductions|norecurrences|noreverse)]\n";
    report_fatal_error("Unrecognised tail-folding option");
  }

public:

  void operator=(const std::string &Val) {
    // If the user explicitly sets -sve-tail-folding= then treat as an error.
    if (Val.empty()) {
      reportError("");
      return;
    }

    // Since the user is explicitly setting the option we don't automatically
    // need the default unless they require it.
    setNeedsDefault(false);

    SmallVector<StringRef, 4> TailFoldTypes;
    StringRef(Val).split(TailFoldTypes, '+', -1, false);

    unsigned StartIdx = 1;
    if (TailFoldTypes[0] == "disabled")
      setInitialBits(TailFoldingOpts::Disabled);
    else if (TailFoldTypes[0] == "all")
      setInitialBits(TailFoldingOpts::All);
    else if (TailFoldTypes[0] == "default")
      setNeedsDefault(true);
    else if (TailFoldTypes[0] == "simple")
      setInitialBits(TailFoldingOpts::Simple);
    else {
      StartIdx = 0;
      setInitialBits(TailFoldingOpts::Disabled);
    }

    for (unsigned I = StartIdx; I < TailFoldTypes.size(); I++) {
      if (TailFoldTypes[I] == "reductions")
        setEnableBit(TailFoldingOpts::Reductions);
      else if (TailFoldTypes[I] == "recurrences")
        setEnableBit(TailFoldingOpts::Recurrences);
      else if (TailFoldTypes[I] == "reverse")
        setEnableBit(TailFoldingOpts::Reverse);
      else if (TailFoldTypes[I] == "noreductions")
        setDisableBit(TailFoldingOpts::Reductions);
      else if (TailFoldTypes[I] == "norecurrences")
        setDisableBit(TailFoldingOpts::Recurrences);
      else if (TailFoldTypes[I] == "noreverse")
        setDisableBit(TailFoldingOpts::Reverse);
      else
        reportError(Val);
    }
  }

  bool satisfies(TailFoldingOpts DefaultBits, TailFoldingOpts Required) const {
    return (getBits(DefaultBits) & Required) == Required;
  }
};
} // namespace

TailFoldingOption TailFoldingOptionLoc;

cl::opt<TailFoldingOption, true, cl::parser<std::string>> SVETailFolding(
    "sve-tail-folding",
    cl::desc(
        "Control the use of vectorisation using tail-folding for SVE where the"
        " option is specified in the form (Initial)[+(Flag1|Flag2|...)]:"
        "\ndisabled      (Initial) No loop types will vectorize using "
        "tail-folding"
        "\ndefault       (Initial) Uses the default tail-folding settings for "
        "the target CPU"
        "\nall           (Initial) All legal loop types will vectorize using "
        "tail-folding"
        "\nsimple        (Initial) Use tail-folding for simple loops (not "
        "reductions or recurrences)"
        "\nreductions    Use tail-folding for loops containing reductions"
        "\nnoreductions  Inverse of above"
        "\nrecurrences   Use tail-folding for loops containing fixed order "
        "recurrences"
        "\nnorecurrences Inverse of above"
        "\nreverse       Use tail-folding for loops requiring reversed "
        "predicates"
        "\nnoreverse     Inverse of above"),
    cl::location(TailFoldingOptionLoc));

// Experimental option that will only be fully functional when the
// code-generator is changed to use SVE instead of NEON for all fixed-width
// operations.
static cl::opt<bool> EnableFixedwidthAutovecInStreamingMode(
    "enable-fixedwidth-autovec-in-streaming-mode", cl::init(false), cl::Hidden);

// Experimental option that will only be fully functional when the cost-model
// and code-generator have been changed to avoid using scalable vector
// instructions that are not legal in streaming SVE mode.
static cl::opt<bool> EnableScalableAutovecInStreamingMode(
    "enable-scalable-autovec-in-streaming-mode", cl::init(false), cl::Hidden);

static bool isSMEABIRoutineCall(const CallInst &CI) {
  const auto *F = CI.getCalledFunction();
  return F && StringSwitch<bool>(F->getName())
                  .Case("__arm_sme_state", true)
                  .Case("__arm_tpidr2_save", true)
                  .Case("__arm_tpidr2_restore", true)
                  .Case("__arm_za_disable", true)
                  .Default(false);
}

/// Returns true if the function has explicit operations that can only be
/// lowered using incompatible instructions for the selected mode. This also
/// returns true if the function F may use or modify ZA state.
static bool hasPossibleIncompatibleOps(const Function *F) {
  for (const BasicBlock &BB : *F) {
    for (const Instruction &I : BB) {
      // Be conservative for now and assume that any call to inline asm or to
      // intrinsics could could result in non-streaming ops (e.g. calls to
      // @llvm.aarch64.* or @llvm.gather/scatter intrinsics). We can assume that
      // all native LLVM instructions can be lowered to compatible instructions.
      if (isa<CallInst>(I) && !I.isDebugOrPseudoInst() &&
          (cast<CallInst>(I).isInlineAsm() || isa<IntrinsicInst>(I) ||
           isSMEABIRoutineCall(cast<CallInst>(I))))
        return true;
    }
  }
  return false;
}

bool AArch64TTIImpl::areInlineCompatible(const Function *Caller,
                                         const Function *Callee) const {
  SMEAttrs CallerAttrs(*Caller), CalleeAttrs(*Callee);

  // When inlining, we should consider the body of the function, not the
  // interface.
  if (CalleeAttrs.hasStreamingBody()) {
    CalleeAttrs.set(SMEAttrs::SM_Compatible, false);
    CalleeAttrs.set(SMEAttrs::SM_Enabled, true);
  }

  if (CalleeAttrs.isNewZA())
    return false;

  if (CallerAttrs.requiresLazySave(CalleeAttrs) ||
      CallerAttrs.requiresSMChange(CalleeAttrs) ||
      CallerAttrs.requiresPreservingZT0(CalleeAttrs)) {
    if (hasPossibleIncompatibleOps(Callee))
      return false;
  }

  const TargetMachine &TM = getTLI()->getTargetMachine();

  const FeatureBitset &CallerBits =
      TM.getSubtargetImpl(*Caller)->getFeatureBits();
  const FeatureBitset &CalleeBits =
      TM.getSubtargetImpl(*Callee)->getFeatureBits();

  // Inline a callee if its target-features are a subset of the callers
  // target-features.
  return (CallerBits & CalleeBits) == CalleeBits;
}

bool AArch64TTIImpl::areTypesABICompatible(
    const Function *Caller, const Function *Callee,
    const ArrayRef<Type *> &Types) const {
  if (!BaseT::areTypesABICompatible(Caller, Callee, Types))
    return false;

  // We need to ensure that argument promotion does not attempt to promote
  // pointers to fixed-length vector types larger than 128 bits like
  // <8 x float> (and pointers to aggregate types which have such fixed-length
  // vector type members) into the values of the pointees. Such vector types
  // are used for SVE VLS but there is no ABI for SVE VLS arguments and the
  // backend cannot lower such value arguments. The 128-bit fixed-length SVE
  // types can be safely treated as 128-bit NEON types and they cannot be
  // distinguished in IR.
  if (ST->useSVEForFixedLengthVectors() && llvm::any_of(Types, [](Type *Ty) {
        auto FVTy = dyn_cast<FixedVectorType>(Ty);
        return FVTy &&
               FVTy->getScalarSizeInBits() * FVTy->getNumElements() > 128;
      }))
    return false;

  return true;
}

unsigned
AArch64TTIImpl::getInlineCallPenalty(const Function *F, const CallBase &Call,
                                     unsigned DefaultCallPenalty) const {
  // This function calculates a penalty for executing Call in F.
  //
  // There are two ways this function can be called:
  // (1)  F:
  //       call from F -> G (the call here is Call)
  //
  // For (1), Call.getCaller() == F, so it will always return a high cost if
  // a streaming-mode change is required (thus promoting the need to inline the
  // function)
  //
  // (2)  F:
  //       call from F -> G (the call here is not Call)
  //      G:
  //       call from G -> H (the call here is Call)
  //
  // For (2), if after inlining the body of G into F the call to H requires a
  // streaming-mode change, and the call to G from F would also require a
  // streaming-mode change, then there is benefit to do the streaming-mode
  // change only once and avoid inlining of G into F.
  SMEAttrs FAttrs(*F);
  SMEAttrs CalleeAttrs(Call);
  if (FAttrs.requiresSMChange(CalleeAttrs)) {
    if (F == Call.getCaller()) // (1)
      return CallPenaltyChangeSM * DefaultCallPenalty;
    if (FAttrs.requiresSMChange(SMEAttrs(*Call.getCaller()))) // (2)
      return InlineCallPenaltyChangeSM * DefaultCallPenalty;
  }

  return DefaultCallPenalty;
}

bool AArch64TTIImpl::shouldMaximizeVectorBandwidth(
    TargetTransformInfo::RegisterKind K) const {
  assert(K != TargetTransformInfo::RGK_Scalar);
  return (K == TargetTransformInfo::RGK_FixedWidthVector &&
          ST->isNeonAvailable());
}

/// Calculate the cost of materializing a 64-bit value. This helper
/// method might only calculate a fraction of a larger immediate. Therefore it
/// is valid to return a cost of ZERO.
InstructionCost AArch64TTIImpl::getIntImmCost(int64_t Val) {
  // Check if the immediate can be encoded within an instruction.
  if (Val == 0 || AArch64_AM::isLogicalImmediate(Val, 64))
    return 0;

  if (Val < 0)
    Val = ~Val;

  // Calculate how many moves we will need to materialize this constant.
  SmallVector<AArch64_IMM::ImmInsnModel, 4> Insn;
  AArch64_IMM::expandMOVImm(Val, 64, Insn);
  return Insn.size();
}

/// Calculate the cost of materializing the given constant.
InstructionCost AArch64TTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                                              TTI::TargetCostKind CostKind) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  // Sign-extend all constants to a multiple of 64-bit.
  APInt ImmVal = Imm;
  if (BitSize & 0x3f)
    ImmVal = Imm.sext((BitSize + 63) & ~0x3fU);

  // Split the constant into 64-bit chunks and calculate the cost for each
  // chunk.
  InstructionCost Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < BitSize; ShiftVal += 64) {
    APInt Tmp = ImmVal.ashr(ShiftVal).sextOrTrunc(64);
    int64_t Val = Tmp.getSExtValue();
    Cost += getIntImmCost(Val);
  }
  // We need at least one instruction to materialze the constant.
  return std::max<InstructionCost>(1, Cost);
}

InstructionCost AArch64TTIImpl::getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                                  const APInt &Imm, Type *Ty,
                                                  TTI::TargetCostKind CostKind,
                                                  Instruction *Inst) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;

  unsigned ImmIdx = ~0U;
  switch (Opcode) {
  default:
    return TTI::TCC_Free;
  case Instruction::GetElementPtr:
    // Always hoist the base address of a GetElementPtr.
    if (Idx == 0)
      return 2 * TTI::TCC_Basic;
    return TTI::TCC_Free;
  case Instruction::Store:
    ImmIdx = 0;
    break;
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::ICmp:
    ImmIdx = 1;
    break;
  // Always return TCC_Free for the shift value of a shift instruction.
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    if (Idx == 1)
      return TTI::TCC_Free;
    break;
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
  case Instruction::BitCast:
  case Instruction::PHI:
  case Instruction::Call:
  case Instruction::Select:
  case Instruction::Ret:
  case Instruction::Load:
    break;
  }

  if (Idx == ImmIdx) {
    int NumConstants = (BitSize + 63) / 64;
    InstructionCost Cost = AArch64TTIImpl::getIntImmCost(Imm, Ty, CostKind);
    return (Cost <= NumConstants * TTI::TCC_Basic)
               ? static_cast<int>(TTI::TCC_Free)
               : Cost;
  }
  return AArch64TTIImpl::getIntImmCost(Imm, Ty, CostKind);
}

InstructionCost
AArch64TTIImpl::getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                    const APInt &Imm, Type *Ty,
                                    TTI::TargetCostKind CostKind) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;

  // Most (all?) AArch64 intrinsics do not support folding immediates into the
  // selected instruction, so we compute the materialization cost for the
  // immediate directly.
  if (IID >= Intrinsic::aarch64_addg && IID <= Intrinsic::aarch64_udiv)
    return AArch64TTIImpl::getIntImmCost(Imm, Ty, CostKind);

  switch (IID) {
  default:
    return TTI::TCC_Free;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::smul_with_overflow:
  case Intrinsic::umul_with_overflow:
    if (Idx == 1) {
      int NumConstants = (BitSize + 63) / 64;
      InstructionCost Cost = AArch64TTIImpl::getIntImmCost(Imm, Ty, CostKind);
      return (Cost <= NumConstants * TTI::TCC_Basic)
                 ? static_cast<int>(TTI::TCC_Free)
                 : Cost;
    }
    break;
  case Intrinsic::experimental_stackmap:
    if ((Idx < 2) || (Imm.getBitWidth() <= 64 && isInt<64>(Imm.getSExtValue())))
      return TTI::TCC_Free;
    break;
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint:
    if ((Idx < 4) || (Imm.getBitWidth() <= 64 && isInt<64>(Imm.getSExtValue())))
      return TTI::TCC_Free;
    break;
  case Intrinsic::experimental_gc_statepoint:
    if ((Idx < 5) || (Imm.getBitWidth() <= 64 && isInt<64>(Imm.getSExtValue())))
      return TTI::TCC_Free;
    break;
  }
  return AArch64TTIImpl::getIntImmCost(Imm, Ty, CostKind);
}

TargetTransformInfo::PopcntSupportKind
AArch64TTIImpl::getPopcntSupport(unsigned TyWidth) {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  if (TyWidth == 32 || TyWidth == 64)
    return TTI::PSK_FastHardware;
  // TODO: AArch64TargetLowering::LowerCTPOP() supports 128bit popcount.
  return TTI::PSK_Software;
}

static bool isUnpackedVectorVT(EVT VecVT) {
  return VecVT.isScalableVector() &&
         VecVT.getSizeInBits().getKnownMinValue() < AArch64::SVEBitsPerBlock;
}

static InstructionCost getHistogramCost(const IntrinsicCostAttributes &ICA) {
  Type *BucketPtrsTy = ICA.getArgTypes()[0]; // Type of vector of pointers
  Type *EltTy = ICA.getArgTypes()[1];        // Type of bucket elements

  // Only allow (32b and 64b) integers or pointers for now...
  if ((!EltTy->isIntegerTy() && !EltTy->isPointerTy()) ||
      (EltTy->getScalarSizeInBits() != 32 &&
       EltTy->getScalarSizeInBits() != 64))
    return InstructionCost::getInvalid();

  // FIXME: Hacky check for legal vector types. We can promote smaller types
  //        but we cannot legalize vectors via splitting for histcnt.
  // FIXME: We should be able to generate histcnt for fixed-length vectors
  //        using ptrue with a specific VL.
  if (VectorType *VTy = dyn_cast<VectorType>(BucketPtrsTy))
    if ((VTy->getElementCount().getKnownMinValue() != 2 &&
         VTy->getElementCount().getKnownMinValue() != 4) ||
        VTy->getPrimitiveSizeInBits().getKnownMinValue() > 128 ||
        !VTy->isScalableTy())
      return InstructionCost::getInvalid();

  return InstructionCost(BaseHistCntCost);
}

InstructionCost
AArch64TTIImpl::getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                      TTI::TargetCostKind CostKind) {
  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  auto *RetTy = ICA.getReturnType();
  if (auto *VTy = dyn_cast<ScalableVectorType>(RetTy))
    if (VTy->getElementCount() == ElementCount::getScalable(1))
      return InstructionCost::getInvalid();

  switch (ICA.getID()) {
  case Intrinsic::experimental_vector_histogram_add:
    if (!ST->hasSVE2())
      return InstructionCost::getInvalid();
    return getHistogramCost(ICA);
  case Intrinsic::umin:
  case Intrinsic::umax:
  case Intrinsic::smin:
  case Intrinsic::smax: {
    static const auto ValidMinMaxTys = {MVT::v8i8,  MVT::v16i8, MVT::v4i16,
                                        MVT::v8i16, MVT::v2i32, MVT::v4i32,
                                        MVT::nxv16i8, MVT::nxv8i16, MVT::nxv4i32,
                                        MVT::nxv2i64};
    auto LT = getTypeLegalizationCost(RetTy);
    // v2i64 types get converted to cmp+bif hence the cost of 2
    if (LT.second == MVT::v2i64)
      return LT.first * 2;
    if (any_of(ValidMinMaxTys, [&LT](MVT M) { return M == LT.second; }))
      return LT.first;
    break;
  }
  case Intrinsic::sadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::usub_sat: {
    static const auto ValidSatTys = {MVT::v8i8,  MVT::v16i8, MVT::v4i16,
                                     MVT::v8i16, MVT::v2i32, MVT::v4i32,
                                     MVT::v2i64};
    auto LT = getTypeLegalizationCost(RetTy);
    // This is a base cost of 1 for the vadd, plus 3 extract shifts if we
    // need to extend the type, as it uses shr(qadd(shl, shl)).
    unsigned Instrs =
        LT.second.getScalarSizeInBits() == RetTy->getScalarSizeInBits() ? 1 : 4;
    if (any_of(ValidSatTys, [&LT](MVT M) { return M == LT.second; }))
      return LT.first * Instrs;
    break;
  }
  case Intrinsic::abs: {
    static const auto ValidAbsTys = {MVT::v8i8,  MVT::v16i8, MVT::v4i16,
                                     MVT::v8i16, MVT::v2i32, MVT::v4i32,
                                     MVT::v2i64};
    auto LT = getTypeLegalizationCost(RetTy);
    if (any_of(ValidAbsTys, [&LT](MVT M) { return M == LT.second; }))
      return LT.first;
    break;
  }
  case Intrinsic::bswap: {
    static const auto ValidAbsTys = {MVT::v4i16, MVT::v8i16, MVT::v2i32,
                                     MVT::v4i32, MVT::v2i64};
    auto LT = getTypeLegalizationCost(RetTy);
    if (any_of(ValidAbsTys, [&LT](MVT M) { return M == LT.second; }) &&
        LT.second.getScalarSizeInBits() == RetTy->getScalarSizeInBits())
      return LT.first;
    break;
  }
  case Intrinsic::experimental_stepvector: {
    InstructionCost Cost = 1; // Cost of the `index' instruction
    auto LT = getTypeLegalizationCost(RetTy);
    // Legalisation of illegal vectors involves an `index' instruction plus
    // (LT.first - 1) vector adds.
    if (LT.first > 1) {
      Type *LegalVTy = EVT(LT.second).getTypeForEVT(RetTy->getContext());
      InstructionCost AddCost =
          getArithmeticInstrCost(Instruction::Add, LegalVTy, CostKind);
      Cost += AddCost * (LT.first - 1);
    }
    return Cost;
  }
  case Intrinsic::vector_extract:
  case Intrinsic::vector_insert: {
    // If both the vector and subvector types are legal types and the index
    // is 0, then this should be a no-op or simple operation; return a
    // relatively low cost.

    // If arguments aren't actually supplied, then we cannot determine the
    // value of the index. We also want to skip predicate types.
    if (ICA.getArgs().size() != ICA.getArgTypes().size() ||
        ICA.getReturnType()->getScalarType()->isIntegerTy(1))
      break;

    LLVMContext &C = RetTy->getContext();
    EVT VecVT = getTLI()->getValueType(DL, ICA.getArgTypes()[0]);
    bool IsExtract = ICA.getID() == Intrinsic::vector_extract;
    EVT SubVecVT = IsExtract ? getTLI()->getValueType(DL, RetTy)
                             : getTLI()->getValueType(DL, ICA.getArgTypes()[1]);
    // Skip this if either the vector or subvector types are unpacked
    // SVE types; they may get lowered to stack stores and loads.
    if (isUnpackedVectorVT(VecVT) || isUnpackedVectorVT(SubVecVT))
      break;

    TargetLoweringBase::LegalizeKind SubVecLK =
        getTLI()->getTypeConversion(C, SubVecVT);
    TargetLoweringBase::LegalizeKind VecLK =
        getTLI()->getTypeConversion(C, VecVT);
    const Value *Idx = IsExtract ? ICA.getArgs()[1] : ICA.getArgs()[2];
    const ConstantInt *CIdx = cast<ConstantInt>(Idx);
    if (SubVecLK.first == TargetLoweringBase::TypeLegal &&
        VecLK.first == TargetLoweringBase::TypeLegal && CIdx->isZero())
      return TTI::TCC_Free;
    break;
  }
  case Intrinsic::bitreverse: {
    static const CostTblEntry BitreverseTbl[] = {
        {Intrinsic::bitreverse, MVT::i32, 1},
        {Intrinsic::bitreverse, MVT::i64, 1},
        {Intrinsic::bitreverse, MVT::v8i8, 1},
        {Intrinsic::bitreverse, MVT::v16i8, 1},
        {Intrinsic::bitreverse, MVT::v4i16, 2},
        {Intrinsic::bitreverse, MVT::v8i16, 2},
        {Intrinsic::bitreverse, MVT::v2i32, 2},
        {Intrinsic::bitreverse, MVT::v4i32, 2},
        {Intrinsic::bitreverse, MVT::v1i64, 2},
        {Intrinsic::bitreverse, MVT::v2i64, 2},
    };
    const auto LegalisationCost = getTypeLegalizationCost(RetTy);
    const auto *Entry =
        CostTableLookup(BitreverseTbl, ICA.getID(), LegalisationCost.second);
    if (Entry) {
      // Cost Model is using the legal type(i32) that i8 and i16 will be
      // converted to +1 so that we match the actual lowering cost
      if (TLI->getValueType(DL, RetTy, true) == MVT::i8 ||
          TLI->getValueType(DL, RetTy, true) == MVT::i16)
        return LegalisationCost.first * Entry->Cost + 1;

      return LegalisationCost.first * Entry->Cost;
    }
    break;
  }
  case Intrinsic::ctpop: {
    if (!ST->hasNEON()) {
      // 32-bit or 64-bit ctpop without NEON is 12 instructions.
      return getTypeLegalizationCost(RetTy).first * 12;
    }
    static const CostTblEntry CtpopCostTbl[] = {
        {ISD::CTPOP, MVT::v2i64, 4},
        {ISD::CTPOP, MVT::v4i32, 3},
        {ISD::CTPOP, MVT::v8i16, 2},
        {ISD::CTPOP, MVT::v16i8, 1},
        {ISD::CTPOP, MVT::i64,   4},
        {ISD::CTPOP, MVT::v2i32, 3},
        {ISD::CTPOP, MVT::v4i16, 2},
        {ISD::CTPOP, MVT::v8i8,  1},
        {ISD::CTPOP, MVT::i32,   5},
    };
    auto LT = getTypeLegalizationCost(RetTy);
    MVT MTy = LT.second;
    if (const auto *Entry = CostTableLookup(CtpopCostTbl, ISD::CTPOP, MTy)) {
      // Extra cost of +1 when illegal vector types are legalized by promoting
      // the integer type.
      int ExtraCost = MTy.isVector() && MTy.getScalarSizeInBits() !=
                                            RetTy->getScalarSizeInBits()
                          ? 1
                          : 0;
      return LT.first * Entry->Cost + ExtraCost;
    }
    break;
  }
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::smul_with_overflow:
  case Intrinsic::umul_with_overflow: {
    static const CostTblEntry WithOverflowCostTbl[] = {
        {Intrinsic::sadd_with_overflow, MVT::i8, 3},
        {Intrinsic::uadd_with_overflow, MVT::i8, 3},
        {Intrinsic::sadd_with_overflow, MVT::i16, 3},
        {Intrinsic::uadd_with_overflow, MVT::i16, 3},
        {Intrinsic::sadd_with_overflow, MVT::i32, 1},
        {Intrinsic::uadd_with_overflow, MVT::i32, 1},
        {Intrinsic::sadd_with_overflow, MVT::i64, 1},
        {Intrinsic::uadd_with_overflow, MVT::i64, 1},
        {Intrinsic::ssub_with_overflow, MVT::i8, 3},
        {Intrinsic::usub_with_overflow, MVT::i8, 3},
        {Intrinsic::ssub_with_overflow, MVT::i16, 3},
        {Intrinsic::usub_with_overflow, MVT::i16, 3},
        {Intrinsic::ssub_with_overflow, MVT::i32, 1},
        {Intrinsic::usub_with_overflow, MVT::i32, 1},
        {Intrinsic::ssub_with_overflow, MVT::i64, 1},
        {Intrinsic::usub_with_overflow, MVT::i64, 1},
        {Intrinsic::smul_with_overflow, MVT::i8, 5},
        {Intrinsic::umul_with_overflow, MVT::i8, 4},
        {Intrinsic::smul_with_overflow, MVT::i16, 5},
        {Intrinsic::umul_with_overflow, MVT::i16, 4},
        {Intrinsic::smul_with_overflow, MVT::i32, 2}, // eg umull;tst
        {Intrinsic::umul_with_overflow, MVT::i32, 2}, // eg umull;cmp sxtw
        {Intrinsic::smul_with_overflow, MVT::i64, 3}, // eg mul;smulh;cmp
        {Intrinsic::umul_with_overflow, MVT::i64, 3}, // eg mul;umulh;cmp asr
    };
    EVT MTy = TLI->getValueType(DL, RetTy->getContainedType(0), true);
    if (MTy.isSimple())
      if (const auto *Entry = CostTableLookup(WithOverflowCostTbl, ICA.getID(),
                                              MTy.getSimpleVT()))
        return Entry->Cost;
    break;
  }
  case Intrinsic::fptosi_sat:
  case Intrinsic::fptoui_sat: {
    if (ICA.getArgTypes().empty())
      break;
    bool IsSigned = ICA.getID() == Intrinsic::fptosi_sat;
    auto LT = getTypeLegalizationCost(ICA.getArgTypes()[0]);
    EVT MTy = TLI->getValueType(DL, RetTy);
    // Check for the legal types, which are where the size of the input and the
    // output are the same, or we are using cvt f64->i32 or f32->i64.
    if ((LT.second == MVT::f32 || LT.second == MVT::f64 ||
         LT.second == MVT::v2f32 || LT.second == MVT::v4f32 ||
         LT.second == MVT::v2f64) &&
        (LT.second.getScalarSizeInBits() == MTy.getScalarSizeInBits() ||
         (LT.second == MVT::f64 && MTy == MVT::i32) ||
         (LT.second == MVT::f32 && MTy == MVT::i64)))
      return LT.first;
    // Similarly for fp16 sizes
    if (ST->hasFullFP16() &&
        ((LT.second == MVT::f16 && MTy == MVT::i32) ||
         ((LT.second == MVT::v4f16 || LT.second == MVT::v8f16) &&
          (LT.second.getScalarSizeInBits() == MTy.getScalarSizeInBits()))))
      return LT.first;

    // Otherwise we use a legal convert followed by a min+max
    if ((LT.second.getScalarType() == MVT::f32 ||
         LT.second.getScalarType() == MVT::f64 ||
         (ST->hasFullFP16() && LT.second.getScalarType() == MVT::f16)) &&
        LT.second.getScalarSizeInBits() >= MTy.getScalarSizeInBits()) {
      Type *LegalTy =
          Type::getIntNTy(RetTy->getContext(), LT.second.getScalarSizeInBits());
      if (LT.second.isVector())
        LegalTy = VectorType::get(LegalTy, LT.second.getVectorElementCount());
      InstructionCost Cost = 1;
      IntrinsicCostAttributes Attrs1(IsSigned ? Intrinsic::smin : Intrinsic::umin,
                                    LegalTy, {LegalTy, LegalTy});
      Cost += getIntrinsicInstrCost(Attrs1, CostKind);
      IntrinsicCostAttributes Attrs2(IsSigned ? Intrinsic::smax : Intrinsic::umax,
                                    LegalTy, {LegalTy, LegalTy});
      Cost += getIntrinsicInstrCost(Attrs2, CostKind);
      return LT.first * Cost;
    }
    break;
  }
  case Intrinsic::fshl:
  case Intrinsic::fshr: {
    if (ICA.getArgs().empty())
      break;

    // TODO: Add handling for fshl where third argument is not a constant.
    const TTI::OperandValueInfo OpInfoZ = TTI::getOperandInfo(ICA.getArgs()[2]);
    if (!OpInfoZ.isConstant())
      break;

    const auto LegalisationCost = getTypeLegalizationCost(RetTy);
    if (OpInfoZ.isUniform()) {
      // FIXME: The costs could be lower if the codegen is better.
      static const CostTblEntry FshlTbl[] = {
          {Intrinsic::fshl, MVT::v4i32, 3}, // ushr + shl + orr
          {Intrinsic::fshl, MVT::v2i64, 3}, {Intrinsic::fshl, MVT::v16i8, 4},
          {Intrinsic::fshl, MVT::v8i16, 4}, {Intrinsic::fshl, MVT::v2i32, 3},
          {Intrinsic::fshl, MVT::v8i8, 4},  {Intrinsic::fshl, MVT::v4i16, 4}};
      // Costs for both fshl & fshr are the same, so just pass Intrinsic::fshl
      // to avoid having to duplicate the costs.
      const auto *Entry =
          CostTableLookup(FshlTbl, Intrinsic::fshl, LegalisationCost.second);
      if (Entry)
        return LegalisationCost.first * Entry->Cost;
    }

    auto TyL = getTypeLegalizationCost(RetTy);
    if (!RetTy->isIntegerTy())
      break;

    // Estimate cost manually, as types like i8 and i16 will get promoted to
    // i32 and CostTableLookup will ignore the extra conversion cost.
    bool HigherCost = (RetTy->getScalarSizeInBits() != 32 &&
                       RetTy->getScalarSizeInBits() < 64) ||
                      (RetTy->getScalarSizeInBits() % 64 != 0);
    unsigned ExtraCost = HigherCost ? 1 : 0;
    if (RetTy->getScalarSizeInBits() == 32 ||
        RetTy->getScalarSizeInBits() == 64)
      ExtraCost = 0; // fhsl/fshr for i32 and i64 can be lowered to a single
                     // extr instruction.
    else if (HigherCost)
      ExtraCost = 1;
    else
      break;
    return TyL.first + ExtraCost;
  }
  case Intrinsic::get_active_lane_mask: {
    auto *RetTy = dyn_cast<FixedVectorType>(ICA.getReturnType());
    if (RetTy) {
      EVT RetVT = getTLI()->getValueType(DL, RetTy);
      EVT OpVT = getTLI()->getValueType(DL, ICA.getArgTypes()[0]);
      if (!getTLI()->shouldExpandGetActiveLaneMask(RetVT, OpVT) &&
          !getTLI()->isTypeLegal(RetVT)) {
        // We don't have enough context at this point to determine if the mask
        // is going to be kept live after the block, which will force the vXi1
        // type to be expanded to legal vectors of integers, e.g. v4i1->v4i32.
        // For now, we just assume the vectorizer created this intrinsic and
        // the result will be the input for a PHI. In this case the cost will
        // be extremely high for fixed-width vectors.
        // NOTE: getScalarizationOverhead returns a cost that's far too
        // pessimistic for the actual generated codegen. In reality there are
        // two instructions generated per lane.
        return RetTy->getNumElements() * 2;
      }
    }
    break;
  }
  default:
    break;
  }
  return BaseT::getIntrinsicInstrCost(ICA, CostKind);
}

/// The function will remove redundant reinterprets casting in the presence
/// of the control flow
static std::optional<Instruction *> processPhiNode(InstCombiner &IC,
                                                   IntrinsicInst &II) {
  SmallVector<Instruction *, 32> Worklist;
  auto RequiredType = II.getType();

  auto *PN = dyn_cast<PHINode>(II.getArgOperand(0));
  assert(PN && "Expected Phi Node!");

  // Don't create a new Phi unless we can remove the old one.
  if (!PN->hasOneUse())
    return std::nullopt;

  for (Value *IncValPhi : PN->incoming_values()) {
    auto *Reinterpret = dyn_cast<IntrinsicInst>(IncValPhi);
    if (!Reinterpret ||
        Reinterpret->getIntrinsicID() !=
            Intrinsic::aarch64_sve_convert_to_svbool ||
        RequiredType != Reinterpret->getArgOperand(0)->getType())
      return std::nullopt;
  }

  // Create the new Phi
  IC.Builder.SetInsertPoint(PN);
  PHINode *NPN = IC.Builder.CreatePHI(RequiredType, PN->getNumIncomingValues());
  Worklist.push_back(PN);

  for (unsigned I = 0; I < PN->getNumIncomingValues(); I++) {
    auto *Reinterpret = cast<Instruction>(PN->getIncomingValue(I));
    NPN->addIncoming(Reinterpret->getOperand(0), PN->getIncomingBlock(I));
    Worklist.push_back(Reinterpret);
  }

  // Cleanup Phi Node and reinterprets
  return IC.replaceInstUsesWith(II, NPN);
}

// (from_svbool (binop (to_svbool pred) (svbool_t _) (svbool_t _))))
// => (binop (pred) (from_svbool _) (from_svbool _))
//
// The above transformation eliminates a `to_svbool` in the predicate
// operand of bitwise operation `binop` by narrowing the vector width of
// the operation. For example, it would convert a `<vscale x 16 x i1>
// and` into a `<vscale x 4 x i1> and`. This is profitable because
// to_svbool must zero the new lanes during widening, whereas
// from_svbool is free.
static std::optional<Instruction *>
tryCombineFromSVBoolBinOp(InstCombiner &IC, IntrinsicInst &II) {
  auto BinOp = dyn_cast<IntrinsicInst>(II.getOperand(0));
  if (!BinOp)
    return std::nullopt;

  auto IntrinsicID = BinOp->getIntrinsicID();
  switch (IntrinsicID) {
  case Intrinsic::aarch64_sve_and_z:
  case Intrinsic::aarch64_sve_bic_z:
  case Intrinsic::aarch64_sve_eor_z:
  case Intrinsic::aarch64_sve_nand_z:
  case Intrinsic::aarch64_sve_nor_z:
  case Intrinsic::aarch64_sve_orn_z:
  case Intrinsic::aarch64_sve_orr_z:
    break;
  default:
    return std::nullopt;
  }

  auto BinOpPred = BinOp->getOperand(0);
  auto BinOpOp1 = BinOp->getOperand(1);
  auto BinOpOp2 = BinOp->getOperand(2);

  auto PredIntr = dyn_cast<IntrinsicInst>(BinOpPred);
  if (!PredIntr ||
      PredIntr->getIntrinsicID() != Intrinsic::aarch64_sve_convert_to_svbool)
    return std::nullopt;

  auto PredOp = PredIntr->getOperand(0);
  auto PredOpTy = cast<VectorType>(PredOp->getType());
  if (PredOpTy != II.getType())
    return std::nullopt;

  SmallVector<Value *> NarrowedBinOpArgs = {PredOp};
  auto NarrowBinOpOp1 = IC.Builder.CreateIntrinsic(
      Intrinsic::aarch64_sve_convert_from_svbool, {PredOpTy}, {BinOpOp1});
  NarrowedBinOpArgs.push_back(NarrowBinOpOp1);
  if (BinOpOp1 == BinOpOp2)
    NarrowedBinOpArgs.push_back(NarrowBinOpOp1);
  else
    NarrowedBinOpArgs.push_back(IC.Builder.CreateIntrinsic(
        Intrinsic::aarch64_sve_convert_from_svbool, {PredOpTy}, {BinOpOp2}));

  auto NarrowedBinOp =
      IC.Builder.CreateIntrinsic(IntrinsicID, {PredOpTy}, NarrowedBinOpArgs);
  return IC.replaceInstUsesWith(II, NarrowedBinOp);
}

static std::optional<Instruction *>
instCombineConvertFromSVBool(InstCombiner &IC, IntrinsicInst &II) {
  // If the reinterpret instruction operand is a PHI Node
  if (isa<PHINode>(II.getArgOperand(0)))
    return processPhiNode(IC, II);

  if (auto BinOpCombine = tryCombineFromSVBoolBinOp(IC, II))
    return BinOpCombine;

  // Ignore converts to/from svcount_t.
  if (isa<TargetExtType>(II.getArgOperand(0)->getType()) ||
      isa<TargetExtType>(II.getType()))
    return std::nullopt;

  SmallVector<Instruction *, 32> CandidatesForRemoval;
  Value *Cursor = II.getOperand(0), *EarliestReplacement = nullptr;

  const auto *IVTy = cast<VectorType>(II.getType());

  // Walk the chain of conversions.
  while (Cursor) {
    // If the type of the cursor has fewer lanes than the final result, zeroing
    // must take place, which breaks the equivalence chain.
    const auto *CursorVTy = cast<VectorType>(Cursor->getType());
    if (CursorVTy->getElementCount().getKnownMinValue() <
        IVTy->getElementCount().getKnownMinValue())
      break;

    // If the cursor has the same type as I, it is a viable replacement.
    if (Cursor->getType() == IVTy)
      EarliestReplacement = Cursor;

    auto *IntrinsicCursor = dyn_cast<IntrinsicInst>(Cursor);

    // If this is not an SVE conversion intrinsic, this is the end of the chain.
    if (!IntrinsicCursor || !(IntrinsicCursor->getIntrinsicID() ==
                                  Intrinsic::aarch64_sve_convert_to_svbool ||
                              IntrinsicCursor->getIntrinsicID() ==
                                  Intrinsic::aarch64_sve_convert_from_svbool))
      break;

    CandidatesForRemoval.insert(CandidatesForRemoval.begin(), IntrinsicCursor);
    Cursor = IntrinsicCursor->getOperand(0);
  }

  // If no viable replacement in the conversion chain was found, there is
  // nothing to do.
  if (!EarliestReplacement)
    return std::nullopt;

  return IC.replaceInstUsesWith(II, EarliestReplacement);
}

static bool isAllActivePredicate(Value *Pred) {
  // Look through convert.from.svbool(convert.to.svbool(...) chain.
  Value *UncastedPred;
  if (match(Pred, m_Intrinsic<Intrinsic::aarch64_sve_convert_from_svbool>(
                      m_Intrinsic<Intrinsic::aarch64_sve_convert_to_svbool>(
                          m_Value(UncastedPred)))))
    // If the predicate has the same or less lanes than the uncasted
    // predicate then we know the casting has no effect.
    if (cast<ScalableVectorType>(Pred->getType())->getMinNumElements() <=
        cast<ScalableVectorType>(UncastedPred->getType())->getMinNumElements())
      Pred = UncastedPred;

  return match(Pred, m_Intrinsic<Intrinsic::aarch64_sve_ptrue>(
                         m_ConstantInt<AArch64SVEPredPattern::all>()));
}

// Erase unary operation where predicate has all inactive lanes
static std::optional<Instruction *>
instCombineSVENoActiveUnaryErase(InstCombiner &IC, IntrinsicInst &II,
                                 int PredPos) {
  if (match(II.getOperand(PredPos), m_ZeroInt())) {
    return IC.eraseInstFromFunction(II);
  }
  return std::nullopt;
}

// Simplify unary operation where predicate has all inactive lanes by replacing
// instruction with zeroed object
static std::optional<Instruction *>
instCombineSVENoActiveUnaryZero(InstCombiner &IC, IntrinsicInst &II) {
  if (match(II.getOperand(0), m_ZeroInt())) {
    Constant *Node;
    Type *RetTy = II.getType();
    if (RetTy->isStructTy()) {
      auto StructT = cast<StructType>(RetTy);
      auto VecT = StructT->getElementType(0);
      SmallVector<llvm::Constant *, 4> ZerVec;
      for (unsigned i = 0; i < StructT->getNumElements(); i++) {
        ZerVec.push_back(VecT->isFPOrFPVectorTy() ? ConstantFP::get(VecT, 0.0)
                                                  : ConstantInt::get(VecT, 0));
      }
      Node = ConstantStruct::get(StructT, ZerVec);
    } else if (RetTy->isFPOrFPVectorTy())
      Node = ConstantFP::get(RetTy, 0.0);
    else
      Node = ConstantInt::get(II.getType(), 0);

    IC.replaceInstUsesWith(II, Node);
    return IC.eraseInstFromFunction(II);
  }
  return std::nullopt;
}

static std::optional<Instruction *> instCombineSVESel(InstCombiner &IC,
                                                      IntrinsicInst &II) {
  // svsel(ptrue, x, y) => x
  auto *OpPredicate = II.getOperand(0);
  if (isAllActivePredicate(OpPredicate))
    return IC.replaceInstUsesWith(II, II.getOperand(1));

  auto Select =
      IC.Builder.CreateSelect(OpPredicate, II.getOperand(1), II.getOperand(2));
  return IC.replaceInstUsesWith(II, Select);
}

static std::optional<Instruction *> instCombineSVEDup(InstCombiner &IC,
                                                      IntrinsicInst &II) {
  IntrinsicInst *Pg = dyn_cast<IntrinsicInst>(II.getArgOperand(1));
  if (!Pg)
    return std::nullopt;

  if (Pg->getIntrinsicID() != Intrinsic::aarch64_sve_ptrue)
    return std::nullopt;

  const auto PTruePattern =
      cast<ConstantInt>(Pg->getOperand(0))->getZExtValue();
  if (PTruePattern != AArch64SVEPredPattern::vl1)
    return std::nullopt;

  // The intrinsic is inserting into lane zero so use an insert instead.
  auto *IdxTy = Type::getInt64Ty(II.getContext());
  auto *Insert = InsertElementInst::Create(
      II.getArgOperand(0), II.getArgOperand(2), ConstantInt::get(IdxTy, 0));
  Insert->insertBefore(&II);
  Insert->takeName(&II);

  return IC.replaceInstUsesWith(II, Insert);
}

static std::optional<Instruction *> instCombineSVEDupX(InstCombiner &IC,
                                                       IntrinsicInst &II) {
  // Replace DupX with a regular IR splat.
  auto *RetTy = cast<ScalableVectorType>(II.getType());
  Value *Splat = IC.Builder.CreateVectorSplat(RetTy->getElementCount(),
                                              II.getArgOperand(0));
  Splat->takeName(&II);
  return IC.replaceInstUsesWith(II, Splat);
}

static std::optional<Instruction *> instCombineSVECmpNE(InstCombiner &IC,
                                                        IntrinsicInst &II) {
  LLVMContext &Ctx = II.getContext();

  // Check that the predicate is all active
  auto *Pg = dyn_cast<IntrinsicInst>(II.getArgOperand(0));
  if (!Pg || Pg->getIntrinsicID() != Intrinsic::aarch64_sve_ptrue)
    return std::nullopt;

  const auto PTruePattern =
      cast<ConstantInt>(Pg->getOperand(0))->getZExtValue();
  if (PTruePattern != AArch64SVEPredPattern::all)
    return std::nullopt;

  // Check that we have a compare of zero..
  auto *SplatValue =
      dyn_cast_or_null<ConstantInt>(getSplatValue(II.getArgOperand(2)));
  if (!SplatValue || !SplatValue->isZero())
    return std::nullopt;

  // ..against a dupq
  auto *DupQLane = dyn_cast<IntrinsicInst>(II.getArgOperand(1));
  if (!DupQLane ||
      DupQLane->getIntrinsicID() != Intrinsic::aarch64_sve_dupq_lane)
    return std::nullopt;

  // Where the dupq is a lane 0 replicate of a vector insert
  if (!cast<ConstantInt>(DupQLane->getArgOperand(1))->isZero())
    return std::nullopt;

  auto *VecIns = dyn_cast<IntrinsicInst>(DupQLane->getArgOperand(0));
  if (!VecIns || VecIns->getIntrinsicID() != Intrinsic::vector_insert)
    return std::nullopt;

  // Where the vector insert is a fixed constant vector insert into undef at
  // index zero
  if (!isa<UndefValue>(VecIns->getArgOperand(0)))
    return std::nullopt;

  if (!cast<ConstantInt>(VecIns->getArgOperand(2))->isZero())
    return std::nullopt;

  auto *ConstVec = dyn_cast<Constant>(VecIns->getArgOperand(1));
  if (!ConstVec)
    return std::nullopt;

  auto *VecTy = dyn_cast<FixedVectorType>(ConstVec->getType());
  auto *OutTy = dyn_cast<ScalableVectorType>(II.getType());
  if (!VecTy || !OutTy || VecTy->getNumElements() != OutTy->getMinNumElements())
    return std::nullopt;

  unsigned NumElts = VecTy->getNumElements();
  unsigned PredicateBits = 0;

  // Expand intrinsic operands to a 16-bit byte level predicate
  for (unsigned I = 0; I < NumElts; ++I) {
    auto *Arg = dyn_cast<ConstantInt>(ConstVec->getAggregateElement(I));
    if (!Arg)
      return std::nullopt;
    if (!Arg->isZero())
      PredicateBits |= 1 << (I * (16 / NumElts));
  }

  // If all bits are zero bail early with an empty predicate
  if (PredicateBits == 0) {
    auto *PFalse = Constant::getNullValue(II.getType());
    PFalse->takeName(&II);
    return IC.replaceInstUsesWith(II, PFalse);
  }

  // Calculate largest predicate type used (where byte predicate is largest)
  unsigned Mask = 8;
  for (unsigned I = 0; I < 16; ++I)
    if ((PredicateBits & (1 << I)) != 0)
      Mask |= (I % 8);

  unsigned PredSize = Mask & -Mask;
  auto *PredType = ScalableVectorType::get(
      Type::getInt1Ty(Ctx), AArch64::SVEBitsPerBlock / (PredSize * 8));

  // Ensure all relevant bits are set
  for (unsigned I = 0; I < 16; I += PredSize)
    if ((PredicateBits & (1 << I)) == 0)
      return std::nullopt;

  auto *PTruePat =
      ConstantInt::get(Type::getInt32Ty(Ctx), AArch64SVEPredPattern::all);
  auto *PTrue = IC.Builder.CreateIntrinsic(Intrinsic::aarch64_sve_ptrue,
                                           {PredType}, {PTruePat});
  auto *ConvertToSVBool = IC.Builder.CreateIntrinsic(
      Intrinsic::aarch64_sve_convert_to_svbool, {PredType}, {PTrue});
  auto *ConvertFromSVBool =
      IC.Builder.CreateIntrinsic(Intrinsic::aarch64_sve_convert_from_svbool,
                                 {II.getType()}, {ConvertToSVBool});

  ConvertFromSVBool->takeName(&II);
  return IC.replaceInstUsesWith(II, ConvertFromSVBool);
}

static std::optional<Instruction *> instCombineSVELast(InstCombiner &IC,
                                                       IntrinsicInst &II) {
  Value *Pg = II.getArgOperand(0);
  Value *Vec = II.getArgOperand(1);
  auto IntrinsicID = II.getIntrinsicID();
  bool IsAfter = IntrinsicID == Intrinsic::aarch64_sve_lasta;

  // lastX(splat(X)) --> X
  if (auto *SplatVal = getSplatValue(Vec))
    return IC.replaceInstUsesWith(II, SplatVal);

  // If x and/or y is a splat value then:
  // lastX (binop (x, y)) --> binop(lastX(x), lastX(y))
  Value *LHS, *RHS;
  if (match(Vec, m_OneUse(m_BinOp(m_Value(LHS), m_Value(RHS))))) {
    if (isSplatValue(LHS) || isSplatValue(RHS)) {
      auto *OldBinOp = cast<BinaryOperator>(Vec);
      auto OpC = OldBinOp->getOpcode();
      auto *NewLHS =
          IC.Builder.CreateIntrinsic(IntrinsicID, {Vec->getType()}, {Pg, LHS});
      auto *NewRHS =
          IC.Builder.CreateIntrinsic(IntrinsicID, {Vec->getType()}, {Pg, RHS});
      auto *NewBinOp = BinaryOperator::CreateWithCopiedFlags(
          OpC, NewLHS, NewRHS, OldBinOp, OldBinOp->getName(), II.getIterator());
      return IC.replaceInstUsesWith(II, NewBinOp);
    }
  }

  auto *C = dyn_cast<Constant>(Pg);
  if (IsAfter && C && C->isNullValue()) {
    // The intrinsic is extracting lane 0 so use an extract instead.
    auto *IdxTy = Type::getInt64Ty(II.getContext());
    auto *Extract = ExtractElementInst::Create(Vec, ConstantInt::get(IdxTy, 0));
    Extract->insertBefore(&II);
    Extract->takeName(&II);
    return IC.replaceInstUsesWith(II, Extract);
  }

  auto *IntrPG = dyn_cast<IntrinsicInst>(Pg);
  if (!IntrPG)
    return std::nullopt;

  if (IntrPG->getIntrinsicID() != Intrinsic::aarch64_sve_ptrue)
    return std::nullopt;

  const auto PTruePattern =
      cast<ConstantInt>(IntrPG->getOperand(0))->getZExtValue();

  // Can the intrinsic's predicate be converted to a known constant index?
  unsigned MinNumElts = getNumElementsFromSVEPredPattern(PTruePattern);
  if (!MinNumElts)
    return std::nullopt;

  unsigned Idx = MinNumElts - 1;
  // Increment the index if extracting the element after the last active
  // predicate element.
  if (IsAfter)
    ++Idx;

  // Ignore extracts whose index is larger than the known minimum vector
  // length. NOTE: This is an artificial constraint where we prefer to
  // maintain what the user asked for until an alternative is proven faster.
  auto *PgVTy = cast<ScalableVectorType>(Pg->getType());
  if (Idx >= PgVTy->getMinNumElements())
    return std::nullopt;

  // The intrinsic is extracting a fixed lane so use an extract instead.
  auto *IdxTy = Type::getInt64Ty(II.getContext());
  auto *Extract = ExtractElementInst::Create(Vec, ConstantInt::get(IdxTy, Idx));
  Extract->insertBefore(&II);
  Extract->takeName(&II);
  return IC.replaceInstUsesWith(II, Extract);
}

static std::optional<Instruction *> instCombineSVECondLast(InstCombiner &IC,
                                                           IntrinsicInst &II) {
  // The SIMD&FP variant of CLAST[AB] is significantly faster than the scalar
  // integer variant across a variety of micro-architectures. Replace scalar
  // integer CLAST[AB] intrinsic with optimal SIMD&FP variant. A simple
  // bitcast-to-fp + clast[ab] + bitcast-to-int will cost a cycle or two more
  // depending on the micro-architecture, but has been observed as generally
  // being faster, particularly when the CLAST[AB] op is a loop-carried
  // dependency.
  Value *Pg = II.getArgOperand(0);
  Value *Fallback = II.getArgOperand(1);
  Value *Vec = II.getArgOperand(2);
  Type *Ty = II.getType();

  if (!Ty->isIntegerTy())
    return std::nullopt;

  Type *FPTy;
  switch (cast<IntegerType>(Ty)->getBitWidth()) {
  default:
    return std::nullopt;
  case 16:
    FPTy = IC.Builder.getHalfTy();
    break;
  case 32:
    FPTy = IC.Builder.getFloatTy();
    break;
  case 64:
    FPTy = IC.Builder.getDoubleTy();
    break;
  }

  Value *FPFallBack = IC.Builder.CreateBitCast(Fallback, FPTy);
  auto *FPVTy = VectorType::get(
      FPTy, cast<VectorType>(Vec->getType())->getElementCount());
  Value *FPVec = IC.Builder.CreateBitCast(Vec, FPVTy);
  auto *FPII = IC.Builder.CreateIntrinsic(
      II.getIntrinsicID(), {FPVec->getType()}, {Pg, FPFallBack, FPVec});
  Value *FPIItoInt = IC.Builder.CreateBitCast(FPII, II.getType());
  return IC.replaceInstUsesWith(II, FPIItoInt);
}

static std::optional<Instruction *> instCombineRDFFR(InstCombiner &IC,
                                                     IntrinsicInst &II) {
  LLVMContext &Ctx = II.getContext();
  // Replace rdffr with predicated rdffr.z intrinsic, so that optimizePTestInstr
  // can work with RDFFR_PP for ptest elimination.
  auto *AllPat =
      ConstantInt::get(Type::getInt32Ty(Ctx), AArch64SVEPredPattern::all);
  auto *PTrue = IC.Builder.CreateIntrinsic(Intrinsic::aarch64_sve_ptrue,
                                           {II.getType()}, {AllPat});
  auto *RDFFR =
      IC.Builder.CreateIntrinsic(Intrinsic::aarch64_sve_rdffr_z, {}, {PTrue});
  RDFFR->takeName(&II);
  return IC.replaceInstUsesWith(II, RDFFR);
}

static std::optional<Instruction *>
instCombineSVECntElts(InstCombiner &IC, IntrinsicInst &II, unsigned NumElts) {
  const auto Pattern = cast<ConstantInt>(II.getArgOperand(0))->getZExtValue();

  if (Pattern == AArch64SVEPredPattern::all) {
    Constant *StepVal = ConstantInt::get(II.getType(), NumElts);
    auto *VScale = IC.Builder.CreateVScale(StepVal);
    VScale->takeName(&II);
    return IC.replaceInstUsesWith(II, VScale);
  }

  unsigned MinNumElts = getNumElementsFromSVEPredPattern(Pattern);

  return MinNumElts && NumElts >= MinNumElts
             ? std::optional<Instruction *>(IC.replaceInstUsesWith(
                   II, ConstantInt::get(II.getType(), MinNumElts)))
             : std::nullopt;
}

static std::optional<Instruction *> instCombineSVEPTest(InstCombiner &IC,
                                                        IntrinsicInst &II) {
  Value *PgVal = II.getArgOperand(0);
  Value *OpVal = II.getArgOperand(1);

  // PTEST_<FIRST|LAST>(X, X) is equivalent to PTEST_ANY(X, X).
  // Later optimizations prefer this form.
  if (PgVal == OpVal &&
      (II.getIntrinsicID() == Intrinsic::aarch64_sve_ptest_first ||
       II.getIntrinsicID() == Intrinsic::aarch64_sve_ptest_last)) {
    Value *Ops[] = {PgVal, OpVal};
    Type *Tys[] = {PgVal->getType()};

    auto *PTest =
        IC.Builder.CreateIntrinsic(Intrinsic::aarch64_sve_ptest_any, Tys, Ops);
    PTest->takeName(&II);

    return IC.replaceInstUsesWith(II, PTest);
  }

  IntrinsicInst *Pg = dyn_cast<IntrinsicInst>(PgVal);
  IntrinsicInst *Op = dyn_cast<IntrinsicInst>(OpVal);

  if (!Pg || !Op)
    return std::nullopt;

  Intrinsic::ID OpIID = Op->getIntrinsicID();

  if (Pg->getIntrinsicID() == Intrinsic::aarch64_sve_convert_to_svbool &&
      OpIID == Intrinsic::aarch64_sve_convert_to_svbool &&
      Pg->getArgOperand(0)->getType() == Op->getArgOperand(0)->getType()) {
    Value *Ops[] = {Pg->getArgOperand(0), Op->getArgOperand(0)};
    Type *Tys[] = {Pg->getArgOperand(0)->getType()};

    auto *PTest = IC.Builder.CreateIntrinsic(II.getIntrinsicID(), Tys, Ops);

    PTest->takeName(&II);
    return IC.replaceInstUsesWith(II, PTest);
  }

  // Transform PTEST_ANY(X=OP(PG,...), X) -> PTEST_ANY(PG, X)).
  // Later optimizations may rewrite sequence to use the flag-setting variant
  // of instruction X to remove PTEST.
  if ((Pg == Op) && (II.getIntrinsicID() == Intrinsic::aarch64_sve_ptest_any) &&
      ((OpIID == Intrinsic::aarch64_sve_brka_z) ||
       (OpIID == Intrinsic::aarch64_sve_brkb_z) ||
       (OpIID == Intrinsic::aarch64_sve_brkpa_z) ||
       (OpIID == Intrinsic::aarch64_sve_brkpb_z) ||
       (OpIID == Intrinsic::aarch64_sve_rdffr_z) ||
       (OpIID == Intrinsic::aarch64_sve_and_z) ||
       (OpIID == Intrinsic::aarch64_sve_bic_z) ||
       (OpIID == Intrinsic::aarch64_sve_eor_z) ||
       (OpIID == Intrinsic::aarch64_sve_nand_z) ||
       (OpIID == Intrinsic::aarch64_sve_nor_z) ||
       (OpIID == Intrinsic::aarch64_sve_orn_z) ||
       (OpIID == Intrinsic::aarch64_sve_orr_z))) {
    Value *Ops[] = {Pg->getArgOperand(0), Pg};
    Type *Tys[] = {Pg->getType()};

    auto *PTest = IC.Builder.CreateIntrinsic(II.getIntrinsicID(), Tys, Ops);
    PTest->takeName(&II);

    return IC.replaceInstUsesWith(II, PTest);
  }

  return std::nullopt;
}

template <Intrinsic::ID MulOpc, typename Intrinsic::ID FuseOpc>
static std::optional<Instruction *>
instCombineSVEVectorFuseMulAddSub(InstCombiner &IC, IntrinsicInst &II,
                                  bool MergeIntoAddendOp) {
  Value *P = II.getOperand(0);
  Value *MulOp0, *MulOp1, *AddendOp, *Mul;
  if (MergeIntoAddendOp) {
    AddendOp = II.getOperand(1);
    Mul = II.getOperand(2);
  } else {
    AddendOp = II.getOperand(2);
    Mul = II.getOperand(1);
  }

  if (!match(Mul, m_Intrinsic<MulOpc>(m_Specific(P), m_Value(MulOp0),
                                      m_Value(MulOp1))))
    return std::nullopt;

  if (!Mul->hasOneUse())
    return std::nullopt;

  Instruction *FMFSource = nullptr;
  if (II.getType()->isFPOrFPVectorTy()) {
    llvm::FastMathFlags FAddFlags = II.getFastMathFlags();
    // Stop the combine when the flags on the inputs differ in case dropping
    // flags would lead to us missing out on more beneficial optimizations.
    if (FAddFlags != cast<CallInst>(Mul)->getFastMathFlags())
      return std::nullopt;
    if (!FAddFlags.allowContract())
      return std::nullopt;
    FMFSource = &II;
  }

  CallInst *Res;
  if (MergeIntoAddendOp)
    Res = IC.Builder.CreateIntrinsic(FuseOpc, {II.getType()},
                                     {P, AddendOp, MulOp0, MulOp1}, FMFSource);
  else
    Res = IC.Builder.CreateIntrinsic(FuseOpc, {II.getType()},
                                     {P, MulOp0, MulOp1, AddendOp}, FMFSource);

  return IC.replaceInstUsesWith(II, Res);
}

static std::optional<Instruction *>
instCombineSVELD1(InstCombiner &IC, IntrinsicInst &II, const DataLayout &DL) {
  Value *Pred = II.getOperand(0);
  Value *PtrOp = II.getOperand(1);
  Type *VecTy = II.getType();

  // Replace by zero constant when all lanes are inactive
  if (auto II_NA = instCombineSVENoActiveUnaryZero(IC, II))
    return II_NA;

  if (isAllActivePredicate(Pred)) {
    LoadInst *Load = IC.Builder.CreateLoad(VecTy, PtrOp);
    Load->copyMetadata(II);
    return IC.replaceInstUsesWith(II, Load);
  }

  CallInst *MaskedLoad =
      IC.Builder.CreateMaskedLoad(VecTy, PtrOp, PtrOp->getPointerAlignment(DL),
                                  Pred, ConstantAggregateZero::get(VecTy));
  MaskedLoad->copyMetadata(II);
  return IC.replaceInstUsesWith(II, MaskedLoad);
}

static std::optional<Instruction *>
instCombineSVEST1(InstCombiner &IC, IntrinsicInst &II, const DataLayout &DL) {
  Value *VecOp = II.getOperand(0);
  Value *Pred = II.getOperand(1);
  Value *PtrOp = II.getOperand(2);

  if (isAllActivePredicate(Pred)) {
    StoreInst *Store = IC.Builder.CreateStore(VecOp, PtrOp);
    Store->copyMetadata(II);
    return IC.eraseInstFromFunction(II);
  }

  CallInst *MaskedStore = IC.Builder.CreateMaskedStore(
      VecOp, PtrOp, PtrOp->getPointerAlignment(DL), Pred);
  MaskedStore->copyMetadata(II);
  return IC.eraseInstFromFunction(II);
}

static Instruction::BinaryOps intrinsicIDToBinOpCode(unsigned Intrinsic) {
  switch (Intrinsic) {
  case Intrinsic::aarch64_sve_fmul_u:
    return Instruction::BinaryOps::FMul;
  case Intrinsic::aarch64_sve_fadd_u:
    return Instruction::BinaryOps::FAdd;
  case Intrinsic::aarch64_sve_fsub_u:
    return Instruction::BinaryOps::FSub;
  default:
    return Instruction::BinaryOpsEnd;
  }
}

static std::optional<Instruction *>
instCombineSVEVectorBinOp(InstCombiner &IC, IntrinsicInst &II) {
  // Bail due to missing support for ISD::STRICT_ scalable vector operations.
  if (II.isStrictFP())
    return std::nullopt;

  auto *OpPredicate = II.getOperand(0);
  auto BinOpCode = intrinsicIDToBinOpCode(II.getIntrinsicID());
  if (BinOpCode == Instruction::BinaryOpsEnd ||
      !match(OpPredicate, m_Intrinsic<Intrinsic::aarch64_sve_ptrue>(
                              m_ConstantInt<AArch64SVEPredPattern::all>())))
    return std::nullopt;
  IRBuilderBase::FastMathFlagGuard FMFGuard(IC.Builder);
  IC.Builder.setFastMathFlags(II.getFastMathFlags());
  auto BinOp =
      IC.Builder.CreateBinOp(BinOpCode, II.getOperand(1), II.getOperand(2));
  return IC.replaceInstUsesWith(II, BinOp);
}

// Canonicalise operations that take an all active predicate (e.g. sve.add ->
// sve.add_u).
static std::optional<Instruction *> instCombineSVEAllActive(IntrinsicInst &II,
                                                            Intrinsic::ID IID) {
  auto *OpPredicate = II.getOperand(0);
  if (!match(OpPredicate, m_Intrinsic<Intrinsic::aarch64_sve_ptrue>(
                              m_ConstantInt<AArch64SVEPredPattern::all>())))
    return std::nullopt;

  auto *Mod = II.getModule();
  auto *NewDecl = Intrinsic::getDeclaration(Mod, IID, {II.getType()});
  II.setCalledFunction(NewDecl);

  return &II;
}

// Simplify operations where predicate has all inactive lanes or try to replace
// with _u form when all lanes are active
static std::optional<Instruction *>
instCombineSVEAllOrNoActive(InstCombiner &IC, IntrinsicInst &II,
                            Intrinsic::ID IID) {
  if (match(II.getOperand(0), m_ZeroInt())) {
    //  llvm_ir, pred(0), op1, op2 - Spec says to return op1 when all lanes are
    //  inactive for sv[func]_m
    return IC.replaceInstUsesWith(II, II.getOperand(1));
  }
  return instCombineSVEAllActive(II, IID);
}

static std::optional<Instruction *> instCombineSVEVectorAdd(InstCombiner &IC,
                                                            IntrinsicInst &II) {
  if (auto II_U =
          instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_add_u))
    return II_U;
  if (auto MLA = instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_mul,
                                                   Intrinsic::aarch64_sve_mla>(
          IC, II, true))
    return MLA;
  if (auto MAD = instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_mul,
                                                   Intrinsic::aarch64_sve_mad>(
          IC, II, false))
    return MAD;
  return std::nullopt;
}

static std::optional<Instruction *>
instCombineSVEVectorFAdd(InstCombiner &IC, IntrinsicInst &II) {
  if (auto II_U =
          instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fadd_u))
    return II_U;
  if (auto FMLA =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fmla>(IC, II,
                                                                         true))
    return FMLA;
  if (auto FMAD =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fmad>(IC, II,
                                                                         false))
    return FMAD;
  if (auto FMLA =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul_u,
                                            Intrinsic::aarch64_sve_fmla>(IC, II,
                                                                         true))
    return FMLA;
  return std::nullopt;
}

static std::optional<Instruction *>
instCombineSVEVectorFAddU(InstCombiner &IC, IntrinsicInst &II) {
  if (auto FMLA =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fmla>(IC, II,
                                                                         true))
    return FMLA;
  if (auto FMAD =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fmad>(IC, II,
                                                                         false))
    return FMAD;
  if (auto FMLA_U =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul_u,
                                            Intrinsic::aarch64_sve_fmla_u>(
              IC, II, true))
    return FMLA_U;
  return instCombineSVEVectorBinOp(IC, II);
}

static std::optional<Instruction *>
instCombineSVEVectorFSub(InstCombiner &IC, IntrinsicInst &II) {
  if (auto II_U =
          instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fsub_u))
    return II_U;
  if (auto FMLS =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fmls>(IC, II,
                                                                         true))
    return FMLS;
  if (auto FMSB =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fnmsb>(
              IC, II, false))
    return FMSB;
  if (auto FMLS =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul_u,
                                            Intrinsic::aarch64_sve_fmls>(IC, II,
                                                                         true))
    return FMLS;
  return std::nullopt;
}

static std::optional<Instruction *>
instCombineSVEVectorFSubU(InstCombiner &IC, IntrinsicInst &II) {
  if (auto FMLS =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fmls>(IC, II,
                                                                         true))
    return FMLS;
  if (auto FMSB =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul,
                                            Intrinsic::aarch64_sve_fnmsb>(
              IC, II, false))
    return FMSB;
  if (auto FMLS_U =
          instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_fmul_u,
                                            Intrinsic::aarch64_sve_fmls_u>(
              IC, II, true))
    return FMLS_U;
  return instCombineSVEVectorBinOp(IC, II);
}

static std::optional<Instruction *> instCombineSVEVectorSub(InstCombiner &IC,
                                                            IntrinsicInst &II) {
  if (auto II_U =
          instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_sub_u))
    return II_U;
  if (auto MLS = instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_mul,
                                                   Intrinsic::aarch64_sve_mls>(
          IC, II, true))
    return MLS;
  return std::nullopt;
}

static std::optional<Instruction *> instCombineSVEVectorMul(InstCombiner &IC,
                                                            IntrinsicInst &II,
                                                            Intrinsic::ID IID) {
  auto *OpPredicate = II.getOperand(0);
  auto *OpMultiplicand = II.getOperand(1);
  auto *OpMultiplier = II.getOperand(2);

  // Return true if a given instruction is a unit splat value, false otherwise.
  auto IsUnitSplat = [](auto *I) {
    auto *SplatValue = getSplatValue(I);
    if (!SplatValue)
      return false;
    return match(SplatValue, m_FPOne()) || match(SplatValue, m_One());
  };

  // Return true if a given instruction is an aarch64_sve_dup intrinsic call
  // with a unit splat value, false otherwise.
  auto IsUnitDup = [](auto *I) {
    auto *IntrI = dyn_cast<IntrinsicInst>(I);
    if (!IntrI || IntrI->getIntrinsicID() != Intrinsic::aarch64_sve_dup)
      return false;

    auto *SplatValue = IntrI->getOperand(2);
    return match(SplatValue, m_FPOne()) || match(SplatValue, m_One());
  };

  if (IsUnitSplat(OpMultiplier)) {
    // [f]mul pg %n, (dupx 1) => %n
    OpMultiplicand->takeName(&II);
    return IC.replaceInstUsesWith(II, OpMultiplicand);
  } else if (IsUnitDup(OpMultiplier)) {
    // [f]mul pg %n, (dup pg 1) => %n
    auto *DupInst = cast<IntrinsicInst>(OpMultiplier);
    auto *DupPg = DupInst->getOperand(1);
    // TODO: this is naive. The optimization is still valid if DupPg
    // 'encompasses' OpPredicate, not only if they're the same predicate.
    if (OpPredicate == DupPg) {
      OpMultiplicand->takeName(&II);
      return IC.replaceInstUsesWith(II, OpMultiplicand);
    }
  }

  return instCombineSVEVectorBinOp(IC, II);
}

static std::optional<Instruction *> instCombineSVEUnpack(InstCombiner &IC,
                                                         IntrinsicInst &II) {
  Value *UnpackArg = II.getArgOperand(0);
  auto *RetTy = cast<ScalableVectorType>(II.getType());
  bool IsSigned = II.getIntrinsicID() == Intrinsic::aarch64_sve_sunpkhi ||
                  II.getIntrinsicID() == Intrinsic::aarch64_sve_sunpklo;

  // Hi = uunpkhi(splat(X)) --> Hi = splat(extend(X))
  // Lo = uunpklo(splat(X)) --> Lo = splat(extend(X))
  if (auto *ScalarArg = getSplatValue(UnpackArg)) {
    ScalarArg =
        IC.Builder.CreateIntCast(ScalarArg, RetTy->getScalarType(), IsSigned);
    Value *NewVal =
        IC.Builder.CreateVectorSplat(RetTy->getElementCount(), ScalarArg);
    NewVal->takeName(&II);
    return IC.replaceInstUsesWith(II, NewVal);
  }

  return std::nullopt;
}
static std::optional<Instruction *> instCombineSVETBL(InstCombiner &IC,
                                                      IntrinsicInst &II) {
  auto *OpVal = II.getOperand(0);
  auto *OpIndices = II.getOperand(1);
  VectorType *VTy = cast<VectorType>(II.getType());

  // Check whether OpIndices is a constant splat value < minimal element count
  // of result.
  auto *SplatValue = dyn_cast_or_null<ConstantInt>(getSplatValue(OpIndices));
  if (!SplatValue ||
      SplatValue->getValue().uge(VTy->getElementCount().getKnownMinValue()))
    return std::nullopt;

  // Convert sve_tbl(OpVal sve_dup_x(SplatValue)) to
  // splat_vector(extractelement(OpVal, SplatValue)) for further optimization.
  auto *Extract = IC.Builder.CreateExtractElement(OpVal, SplatValue);
  auto *VectorSplat =
      IC.Builder.CreateVectorSplat(VTy->getElementCount(), Extract);

  VectorSplat->takeName(&II);
  return IC.replaceInstUsesWith(II, VectorSplat);
}

static std::optional<Instruction *> instCombineSVEUzp1(InstCombiner &IC,
                                                       IntrinsicInst &II) {
  Value *A, *B;
  Type *RetTy = II.getType();
  constexpr Intrinsic::ID FromSVB = Intrinsic::aarch64_sve_convert_from_svbool;
  constexpr Intrinsic::ID ToSVB = Intrinsic::aarch64_sve_convert_to_svbool;

  // uzp1(to_svbool(A), to_svbool(B)) --> <A, B>
  // uzp1(from_svbool(to_svbool(A)), from_svbool(to_svbool(B))) --> <A, B>
  if ((match(II.getArgOperand(0),
             m_Intrinsic<FromSVB>(m_Intrinsic<ToSVB>(m_Value(A)))) &&
       match(II.getArgOperand(1),
             m_Intrinsic<FromSVB>(m_Intrinsic<ToSVB>(m_Value(B))))) ||
      (match(II.getArgOperand(0), m_Intrinsic<ToSVB>(m_Value(A))) &&
       match(II.getArgOperand(1), m_Intrinsic<ToSVB>(m_Value(B))))) {
    auto *TyA = cast<ScalableVectorType>(A->getType());
    if (TyA == B->getType() &&
        RetTy == ScalableVectorType::getDoubleElementsVectorType(TyA)) {
      auto *SubVec = IC.Builder.CreateInsertVector(
          RetTy, PoisonValue::get(RetTy), A, IC.Builder.getInt64(0));
      auto *ConcatVec = IC.Builder.CreateInsertVector(
          RetTy, SubVec, B, IC.Builder.getInt64(TyA->getMinNumElements()));
      ConcatVec->takeName(&II);
      return IC.replaceInstUsesWith(II, ConcatVec);
    }
  }

  return std::nullopt;
}

static std::optional<Instruction *> instCombineSVEZip(InstCombiner &IC,
                                                      IntrinsicInst &II) {
  // zip1(uzp1(A, B), uzp2(A, B)) --> A
  // zip2(uzp1(A, B), uzp2(A, B)) --> B
  Value *A, *B;
  if (match(II.getArgOperand(0),
            m_Intrinsic<Intrinsic::aarch64_sve_uzp1>(m_Value(A), m_Value(B))) &&
      match(II.getArgOperand(1), m_Intrinsic<Intrinsic::aarch64_sve_uzp2>(
                                     m_Specific(A), m_Specific(B))))
    return IC.replaceInstUsesWith(
        II, (II.getIntrinsicID() == Intrinsic::aarch64_sve_zip1 ? A : B));

  return std::nullopt;
}

static std::optional<Instruction *>
instCombineLD1GatherIndex(InstCombiner &IC, IntrinsicInst &II) {
  Value *Mask = II.getOperand(0);
  Value *BasePtr = II.getOperand(1);
  Value *Index = II.getOperand(2);
  Type *Ty = II.getType();
  Value *PassThru = ConstantAggregateZero::get(Ty);

  // Replace by zero constant when all lanes are inactive
  if (auto II_NA = instCombineSVENoActiveUnaryZero(IC, II))
    return II_NA;

  // Contiguous gather => masked load.
  // (sve.ld1.gather.index Mask BasePtr (sve.index IndexBase 1))
  // => (masked.load (gep BasePtr IndexBase) Align Mask zeroinitializer)
  Value *IndexBase;
  if (match(Index, m_Intrinsic<Intrinsic::aarch64_sve_index>(
                       m_Value(IndexBase), m_SpecificInt(1)))) {
    Align Alignment =
        BasePtr->getPointerAlignment(II.getDataLayout());

    Type *VecPtrTy = PointerType::getUnqual(Ty);
    Value *Ptr = IC.Builder.CreateGEP(cast<VectorType>(Ty)->getElementType(),
                                      BasePtr, IndexBase);
    Ptr = IC.Builder.CreateBitCast(Ptr, VecPtrTy);
    CallInst *MaskedLoad =
        IC.Builder.CreateMaskedLoad(Ty, Ptr, Alignment, Mask, PassThru);
    MaskedLoad->takeName(&II);
    return IC.replaceInstUsesWith(II, MaskedLoad);
  }

  return std::nullopt;
}

static std::optional<Instruction *>
instCombineST1ScatterIndex(InstCombiner &IC, IntrinsicInst &II) {
  Value *Val = II.getOperand(0);
  Value *Mask = II.getOperand(1);
  Value *BasePtr = II.getOperand(2);
  Value *Index = II.getOperand(3);
  Type *Ty = Val->getType();

  // Contiguous scatter => masked store.
  // (sve.st1.scatter.index Value Mask BasePtr (sve.index IndexBase 1))
  // => (masked.store Value (gep BasePtr IndexBase) Align Mask)
  Value *IndexBase;
  if (match(Index, m_Intrinsic<Intrinsic::aarch64_sve_index>(
                       m_Value(IndexBase), m_SpecificInt(1)))) {
    Align Alignment =
        BasePtr->getPointerAlignment(II.getDataLayout());

    Value *Ptr = IC.Builder.CreateGEP(cast<VectorType>(Ty)->getElementType(),
                                      BasePtr, IndexBase);
    Type *VecPtrTy = PointerType::getUnqual(Ty);
    Ptr = IC.Builder.CreateBitCast(Ptr, VecPtrTy);

    (void)IC.Builder.CreateMaskedStore(Val, Ptr, Alignment, Mask);

    return IC.eraseInstFromFunction(II);
  }

  return std::nullopt;
}

static std::optional<Instruction *> instCombineSVESDIV(InstCombiner &IC,
                                                       IntrinsicInst &II) {
  Type *Int32Ty = IC.Builder.getInt32Ty();
  Value *Pred = II.getOperand(0);
  Value *Vec = II.getOperand(1);
  Value *DivVec = II.getOperand(2);

  Value *SplatValue = getSplatValue(DivVec);
  ConstantInt *SplatConstantInt = dyn_cast_or_null<ConstantInt>(SplatValue);
  if (!SplatConstantInt)
    return std::nullopt;
  APInt Divisor = SplatConstantInt->getValue();

  if (Divisor.isPowerOf2()) {
    Constant *DivisorLog2 = ConstantInt::get(Int32Ty, Divisor.logBase2());
    auto ASRD = IC.Builder.CreateIntrinsic(
        Intrinsic::aarch64_sve_asrd, {II.getType()}, {Pred, Vec, DivisorLog2});
    return IC.replaceInstUsesWith(II, ASRD);
  }
  if (Divisor.isNegatedPowerOf2()) {
    Divisor.negate();
    Constant *DivisorLog2 = ConstantInt::get(Int32Ty, Divisor.logBase2());
    auto ASRD = IC.Builder.CreateIntrinsic(
        Intrinsic::aarch64_sve_asrd, {II.getType()}, {Pred, Vec, DivisorLog2});
    auto NEG = IC.Builder.CreateIntrinsic(
        Intrinsic::aarch64_sve_neg, {ASRD->getType()}, {ASRD, Pred, ASRD});
    return IC.replaceInstUsesWith(II, NEG);
  }

  return std::nullopt;
}

bool SimplifyValuePattern(SmallVector<Value *> &Vec, bool AllowPoison) {
  size_t VecSize = Vec.size();
  if (VecSize == 1)
    return true;
  if (!isPowerOf2_64(VecSize))
    return false;
  size_t HalfVecSize = VecSize / 2;

  for (auto LHS = Vec.begin(), RHS = Vec.begin() + HalfVecSize;
       RHS != Vec.end(); LHS++, RHS++) {
    if (*LHS != nullptr && *RHS != nullptr) {
      if (*LHS == *RHS)
        continue;
      else
        return false;
    }
    if (!AllowPoison)
      return false;
    if (*LHS == nullptr && *RHS != nullptr)
      *LHS = *RHS;
  }

  Vec.resize(HalfVecSize);
  SimplifyValuePattern(Vec, AllowPoison);
  return true;
}

// Try to simplify dupqlane patterns like dupqlane(f32 A, f32 B, f32 A, f32 B)
// to dupqlane(f64(C)) where C is A concatenated with B
static std::optional<Instruction *> instCombineSVEDupqLane(InstCombiner &IC,
                                                           IntrinsicInst &II) {
  Value *CurrentInsertElt = nullptr, *Default = nullptr;
  if (!match(II.getOperand(0),
             m_Intrinsic<Intrinsic::vector_insert>(
                 m_Value(Default), m_Value(CurrentInsertElt), m_Value())) ||
      !isa<FixedVectorType>(CurrentInsertElt->getType()))
    return std::nullopt;
  auto IIScalableTy = cast<ScalableVectorType>(II.getType());

  // Insert the scalars into a container ordered by InsertElement index
  SmallVector<Value *> Elts(IIScalableTy->getMinNumElements(), nullptr);
  while (auto InsertElt = dyn_cast<InsertElementInst>(CurrentInsertElt)) {
    auto Idx = cast<ConstantInt>(InsertElt->getOperand(2));
    Elts[Idx->getValue().getZExtValue()] = InsertElt->getOperand(1);
    CurrentInsertElt = InsertElt->getOperand(0);
  }

  bool AllowPoison =
      isa<PoisonValue>(CurrentInsertElt) && isa<PoisonValue>(Default);
  if (!SimplifyValuePattern(Elts, AllowPoison))
    return std::nullopt;

  // Rebuild the simplified chain of InsertElements. e.g. (a, b, a, b) as (a, b)
  Value *InsertEltChain = PoisonValue::get(CurrentInsertElt->getType());
  for (size_t I = 0; I < Elts.size(); I++) {
    if (Elts[I] == nullptr)
      continue;
    InsertEltChain = IC.Builder.CreateInsertElement(InsertEltChain, Elts[I],
                                                    IC.Builder.getInt64(I));
  }
  if (InsertEltChain == nullptr)
    return std::nullopt;

  // Splat the simplified sequence, e.g. (f16 a, f16 b, f16 c, f16 d) as one i64
  // value or (f16 a, f16 b) as one i32 value. This requires an InsertSubvector
  // be bitcast to a type wide enough to fit the sequence, be splatted, and then
  // be narrowed back to the original type.
  unsigned PatternWidth = IIScalableTy->getScalarSizeInBits() * Elts.size();
  unsigned PatternElementCount = IIScalableTy->getScalarSizeInBits() *
                                 IIScalableTy->getMinNumElements() /
                                 PatternWidth;

  IntegerType *WideTy = IC.Builder.getIntNTy(PatternWidth);
  auto *WideScalableTy = ScalableVectorType::get(WideTy, PatternElementCount);
  auto *WideShuffleMaskTy =
      ScalableVectorType::get(IC.Builder.getInt32Ty(), PatternElementCount);

  auto ZeroIdx = ConstantInt::get(IC.Builder.getInt64Ty(), APInt(64, 0));
  auto InsertSubvector = IC.Builder.CreateInsertVector(
      II.getType(), PoisonValue::get(II.getType()), InsertEltChain, ZeroIdx);
  auto WideBitcast =
      IC.Builder.CreateBitOrPointerCast(InsertSubvector, WideScalableTy);
  auto WideShuffleMask = ConstantAggregateZero::get(WideShuffleMaskTy);
  auto WideShuffle = IC.Builder.CreateShuffleVector(
      WideBitcast, PoisonValue::get(WideScalableTy), WideShuffleMask);
  auto NarrowBitcast =
      IC.Builder.CreateBitOrPointerCast(WideShuffle, II.getType());

  return IC.replaceInstUsesWith(II, NarrowBitcast);
}

static std::optional<Instruction *> instCombineMaxMinNM(InstCombiner &IC,
                                                        IntrinsicInst &II) {
  Value *A = II.getArgOperand(0);
  Value *B = II.getArgOperand(1);
  if (A == B)
    return IC.replaceInstUsesWith(II, A);

  return std::nullopt;
}

static std::optional<Instruction *> instCombineSVESrshl(InstCombiner &IC,
                                                        IntrinsicInst &II) {
  Value *Pred = II.getOperand(0);
  Value *Vec = II.getOperand(1);
  Value *Shift = II.getOperand(2);

  // Convert SRSHL into the simpler LSL intrinsic when fed by an ABS intrinsic.
  Value *AbsPred, *MergedValue;
  if (!match(Vec, m_Intrinsic<Intrinsic::aarch64_sve_sqabs>(
                      m_Value(MergedValue), m_Value(AbsPred), m_Value())) &&
      !match(Vec, m_Intrinsic<Intrinsic::aarch64_sve_abs>(
                      m_Value(MergedValue), m_Value(AbsPred), m_Value())))

    return std::nullopt;

  // Transform is valid if any of the following are true:
  // * The ABS merge value is an undef or non-negative
  // * The ABS predicate is all active
  // * The ABS predicate and the SRSHL predicates are the same
  if (!isa<UndefValue>(MergedValue) && !match(MergedValue, m_NonNegative()) &&
      AbsPred != Pred && !isAllActivePredicate(AbsPred))
    return std::nullopt;

  // Only valid when the shift amount is non-negative, otherwise the rounding
  // behaviour of SRSHL cannot be ignored.
  if (!match(Shift, m_NonNegative()))
    return std::nullopt;

  auto LSL = IC.Builder.CreateIntrinsic(Intrinsic::aarch64_sve_lsl,
                                        {II.getType()}, {Pred, Vec, Shift});

  return IC.replaceInstUsesWith(II, LSL);
}

std::optional<Instruction *>
AArch64TTIImpl::instCombineIntrinsic(InstCombiner &IC,
                                     IntrinsicInst &II) const {
  Intrinsic::ID IID = II.getIntrinsicID();
  switch (IID) {
  default:
    break;

  case Intrinsic::aarch64_sve_st1_scatter:
  case Intrinsic::aarch64_sve_st1_scatter_scalar_offset:
  case Intrinsic::aarch64_sve_st1_scatter_sxtw:
  case Intrinsic::aarch64_sve_st1_scatter_sxtw_index:
  case Intrinsic::aarch64_sve_st1_scatter_uxtw:
  case Intrinsic::aarch64_sve_st1_scatter_uxtw_index:
  case Intrinsic::aarch64_sve_st1dq:
  case Intrinsic::aarch64_sve_st1q_scatter_index:
  case Intrinsic::aarch64_sve_st1q_scatter_scalar_offset:
  case Intrinsic::aarch64_sve_st1q_scatter_vector_offset:
  case Intrinsic::aarch64_sve_st1wq:
  case Intrinsic::aarch64_sve_stnt1:
  case Intrinsic::aarch64_sve_stnt1_scatter:
  case Intrinsic::aarch64_sve_stnt1_scatter_index:
  case Intrinsic::aarch64_sve_stnt1_scatter_scalar_offset:
  case Intrinsic::aarch64_sve_stnt1_scatter_uxtw:
    return instCombineSVENoActiveUnaryErase(IC, II, 1);
  case Intrinsic::aarch64_sve_st2:
  case Intrinsic::aarch64_sve_st2q:
    return instCombineSVENoActiveUnaryErase(IC, II, 2);
  case Intrinsic::aarch64_sve_st3:
  case Intrinsic::aarch64_sve_st3q:
    return instCombineSVENoActiveUnaryErase(IC, II, 3);
  case Intrinsic::aarch64_sve_st4:
  case Intrinsic::aarch64_sve_st4q:
    return instCombineSVENoActiveUnaryErase(IC, II, 4);
  case Intrinsic::aarch64_sve_ld1_gather:
  case Intrinsic::aarch64_sve_ld1_gather_scalar_offset:
  case Intrinsic::aarch64_sve_ld1_gather_sxtw:
  case Intrinsic::aarch64_sve_ld1_gather_sxtw_index:
  case Intrinsic::aarch64_sve_ld1_gather_uxtw:
  case Intrinsic::aarch64_sve_ld1_gather_uxtw_index:
  case Intrinsic::aarch64_sve_ld1q_gather_index:
  case Intrinsic::aarch64_sve_ld1q_gather_scalar_offset:
  case Intrinsic::aarch64_sve_ld1q_gather_vector_offset:
  case Intrinsic::aarch64_sve_ld1ro:
  case Intrinsic::aarch64_sve_ld1rq:
  case Intrinsic::aarch64_sve_ld1udq:
  case Intrinsic::aarch64_sve_ld1uwq:
  case Intrinsic::aarch64_sve_ld2_sret:
  case Intrinsic::aarch64_sve_ld2q_sret:
  case Intrinsic::aarch64_sve_ld3_sret:
  case Intrinsic::aarch64_sve_ld3q_sret:
  case Intrinsic::aarch64_sve_ld4_sret:
  case Intrinsic::aarch64_sve_ld4q_sret:
  case Intrinsic::aarch64_sve_ldff1:
  case Intrinsic::aarch64_sve_ldff1_gather:
  case Intrinsic::aarch64_sve_ldff1_gather_index:
  case Intrinsic::aarch64_sve_ldff1_gather_scalar_offset:
  case Intrinsic::aarch64_sve_ldff1_gather_sxtw:
  case Intrinsic::aarch64_sve_ldff1_gather_sxtw_index:
  case Intrinsic::aarch64_sve_ldff1_gather_uxtw:
  case Intrinsic::aarch64_sve_ldff1_gather_uxtw_index:
  case Intrinsic::aarch64_sve_ldnf1:
  case Intrinsic::aarch64_sve_ldnt1:
  case Intrinsic::aarch64_sve_ldnt1_gather:
  case Intrinsic::aarch64_sve_ldnt1_gather_index:
  case Intrinsic::aarch64_sve_ldnt1_gather_scalar_offset:
  case Intrinsic::aarch64_sve_ldnt1_gather_uxtw:
    return instCombineSVENoActiveUnaryZero(IC, II);
  case Intrinsic::aarch64_neon_fmaxnm:
  case Intrinsic::aarch64_neon_fminnm:
    return instCombineMaxMinNM(IC, II);
  case Intrinsic::aarch64_sve_convert_from_svbool:
    return instCombineConvertFromSVBool(IC, II);
  case Intrinsic::aarch64_sve_dup:
    return instCombineSVEDup(IC, II);
  case Intrinsic::aarch64_sve_dup_x:
    return instCombineSVEDupX(IC, II);
  case Intrinsic::aarch64_sve_cmpne:
  case Intrinsic::aarch64_sve_cmpne_wide:
    return instCombineSVECmpNE(IC, II);
  case Intrinsic::aarch64_sve_rdffr:
    return instCombineRDFFR(IC, II);
  case Intrinsic::aarch64_sve_lasta:
  case Intrinsic::aarch64_sve_lastb:
    return instCombineSVELast(IC, II);
  case Intrinsic::aarch64_sve_clasta_n:
  case Intrinsic::aarch64_sve_clastb_n:
    return instCombineSVECondLast(IC, II);
  case Intrinsic::aarch64_sve_cntd:
    return instCombineSVECntElts(IC, II, 2);
  case Intrinsic::aarch64_sve_cntw:
    return instCombineSVECntElts(IC, II, 4);
  case Intrinsic::aarch64_sve_cnth:
    return instCombineSVECntElts(IC, II, 8);
  case Intrinsic::aarch64_sve_cntb:
    return instCombineSVECntElts(IC, II, 16);
  case Intrinsic::aarch64_sve_ptest_any:
  case Intrinsic::aarch64_sve_ptest_first:
  case Intrinsic::aarch64_sve_ptest_last:
    return instCombineSVEPTest(IC, II);
  case Intrinsic::aarch64_sve_fabd:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fabd_u);
  case Intrinsic::aarch64_sve_fadd:
    return instCombineSVEVectorFAdd(IC, II);
  case Intrinsic::aarch64_sve_fadd_u:
    return instCombineSVEVectorFAddU(IC, II);
  case Intrinsic::aarch64_sve_fdiv:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fdiv_u);
  case Intrinsic::aarch64_sve_fmax:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fmax_u);
  case Intrinsic::aarch64_sve_fmaxnm:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fmaxnm_u);
  case Intrinsic::aarch64_sve_fmin:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fmin_u);
  case Intrinsic::aarch64_sve_fminnm:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fminnm_u);
  case Intrinsic::aarch64_sve_fmla:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fmla_u);
  case Intrinsic::aarch64_sve_fmls:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fmls_u);
  case Intrinsic::aarch64_sve_fmul:
    if (auto II_U =
            instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fmul_u))
      return II_U;
    return instCombineSVEVectorMul(IC, II, Intrinsic::aarch64_sve_fmul_u);
  case Intrinsic::aarch64_sve_fmul_u:
    return instCombineSVEVectorMul(IC, II, Intrinsic::aarch64_sve_fmul_u);
  case Intrinsic::aarch64_sve_fmulx:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fmulx_u);
  case Intrinsic::aarch64_sve_fnmla:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fnmla_u);
  case Intrinsic::aarch64_sve_fnmls:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_fnmls_u);
  case Intrinsic::aarch64_sve_fsub:
    return instCombineSVEVectorFSub(IC, II);
  case Intrinsic::aarch64_sve_fsub_u:
    return instCombineSVEVectorFSubU(IC, II);
  case Intrinsic::aarch64_sve_add:
    return instCombineSVEVectorAdd(IC, II);
  case Intrinsic::aarch64_sve_add_u:
    return instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_mul_u,
                                             Intrinsic::aarch64_sve_mla_u>(
        IC, II, true);
  case Intrinsic::aarch64_sve_mla:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_mla_u);
  case Intrinsic::aarch64_sve_mls:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_mls_u);
  case Intrinsic::aarch64_sve_mul:
    if (auto II_U =
            instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_mul_u))
      return II_U;
    return instCombineSVEVectorMul(IC, II, Intrinsic::aarch64_sve_mul_u);
  case Intrinsic::aarch64_sve_mul_u:
    return instCombineSVEVectorMul(IC, II, Intrinsic::aarch64_sve_mul_u);
  case Intrinsic::aarch64_sve_sabd:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_sabd_u);
  case Intrinsic::aarch64_sve_smax:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_smax_u);
  case Intrinsic::aarch64_sve_smin:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_smin_u);
  case Intrinsic::aarch64_sve_smulh:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_smulh_u);
  case Intrinsic::aarch64_sve_sub:
    return instCombineSVEVectorSub(IC, II);
  case Intrinsic::aarch64_sve_sub_u:
    return instCombineSVEVectorFuseMulAddSub<Intrinsic::aarch64_sve_mul_u,
                                             Intrinsic::aarch64_sve_mls_u>(
        IC, II, true);
  case Intrinsic::aarch64_sve_uabd:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_uabd_u);
  case Intrinsic::aarch64_sve_umax:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_umax_u);
  case Intrinsic::aarch64_sve_umin:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_umin_u);
  case Intrinsic::aarch64_sve_umulh:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_umulh_u);
  case Intrinsic::aarch64_sve_asr:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_asr_u);
  case Intrinsic::aarch64_sve_lsl:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_lsl_u);
  case Intrinsic::aarch64_sve_lsr:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_lsr_u);
  case Intrinsic::aarch64_sve_and:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_and_u);
  case Intrinsic::aarch64_sve_bic:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_bic_u);
  case Intrinsic::aarch64_sve_eor:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_eor_u);
  case Intrinsic::aarch64_sve_orr:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_orr_u);
  case Intrinsic::aarch64_sve_sqsub:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_sqsub_u);
  case Intrinsic::aarch64_sve_uqsub:
    return instCombineSVEAllOrNoActive(IC, II, Intrinsic::aarch64_sve_uqsub_u);
  case Intrinsic::aarch64_sve_tbl:
    return instCombineSVETBL(IC, II);
  case Intrinsic::aarch64_sve_uunpkhi:
  case Intrinsic::aarch64_sve_uunpklo:
  case Intrinsic::aarch64_sve_sunpkhi:
  case Intrinsic::aarch64_sve_sunpklo:
    return instCombineSVEUnpack(IC, II);
  case Intrinsic::aarch64_sve_uzp1:
    return instCombineSVEUzp1(IC, II);
  case Intrinsic::aarch64_sve_zip1:
  case Intrinsic::aarch64_sve_zip2:
    return instCombineSVEZip(IC, II);
  case Intrinsic::aarch64_sve_ld1_gather_index:
    return instCombineLD1GatherIndex(IC, II);
  case Intrinsic::aarch64_sve_st1_scatter_index:
    return instCombineST1ScatterIndex(IC, II);
  case Intrinsic::aarch64_sve_ld1:
    return instCombineSVELD1(IC, II, DL);
  case Intrinsic::aarch64_sve_st1:
    return instCombineSVEST1(IC, II, DL);
  case Intrinsic::aarch64_sve_sdiv:
    return instCombineSVESDIV(IC, II);
  case Intrinsic::aarch64_sve_sel:
    return instCombineSVESel(IC, II);
  case Intrinsic::aarch64_sve_srshl:
    return instCombineSVESrshl(IC, II);
  case Intrinsic::aarch64_sve_dupq_lane:
    return instCombineSVEDupqLane(IC, II);
  }

  return std::nullopt;
}

std::optional<Value *> AArch64TTIImpl::simplifyDemandedVectorEltsIntrinsic(
    InstCombiner &IC, IntrinsicInst &II, APInt OrigDemandedElts,
    APInt &UndefElts, APInt &UndefElts2, APInt &UndefElts3,
    std::function<void(Instruction *, unsigned, APInt, APInt &)>
        SimplifyAndSetOp) const {
  switch (II.getIntrinsicID()) {
  default:
    break;
  case Intrinsic::aarch64_neon_fcvtxn:
  case Intrinsic::aarch64_neon_rshrn:
  case Intrinsic::aarch64_neon_sqrshrn:
  case Intrinsic::aarch64_neon_sqrshrun:
  case Intrinsic::aarch64_neon_sqshrn:
  case Intrinsic::aarch64_neon_sqshrun:
  case Intrinsic::aarch64_neon_sqxtn:
  case Intrinsic::aarch64_neon_sqxtun:
  case Intrinsic::aarch64_neon_uqrshrn:
  case Intrinsic::aarch64_neon_uqshrn:
  case Intrinsic::aarch64_neon_uqxtn:
    SimplifyAndSetOp(&II, 0, OrigDemandedElts, UndefElts);
    break;
  }

  return std::nullopt;
}

bool AArch64TTIImpl::enableScalableVectorization() const {
  return ST->isSVEAvailable() || (ST->isSVEorStreamingSVEAvailable() &&
                                  EnableScalableAutovecInStreamingMode);
}

TypeSize
AArch64TTIImpl::getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
  switch (K) {
  case TargetTransformInfo::RGK_Scalar:
    return TypeSize::getFixed(64);
  case TargetTransformInfo::RGK_FixedWidthVector:
    if (ST->useSVEForFixedLengthVectors() &&
        (ST->isSVEAvailable() || EnableFixedwidthAutovecInStreamingMode))
      return TypeSize::getFixed(
          std::max(ST->getMinSVEVectorSizeInBits(), 128u));
    else if (ST->isNeonAvailable())
      return TypeSize::getFixed(128);
    else
      return TypeSize::getFixed(0);
  case TargetTransformInfo::RGK_ScalableVector:
    if (ST->isSVEAvailable() || (ST->isSVEorStreamingSVEAvailable() &&
                                 EnableScalableAutovecInStreamingMode))
      return TypeSize::getScalable(128);
    else
      return TypeSize::getScalable(0);
  }
  llvm_unreachable("Unsupported register kind");
}

bool AArch64TTIImpl::isWideningInstruction(Type *DstTy, unsigned Opcode,
                                           ArrayRef<const Value *> Args,
                                           Type *SrcOverrideTy) {
  // A helper that returns a vector type from the given type. The number of
  // elements in type Ty determines the vector width.
  auto toVectorTy = [&](Type *ArgTy) {
    return VectorType::get(ArgTy->getScalarType(),
                           cast<VectorType>(DstTy)->getElementCount());
  };

  // Exit early if DstTy is not a vector type whose elements are one of [i16,
  // i32, i64]. SVE doesn't generally have the same set of instructions to
  // perform an extend with the add/sub/mul. There are SMULLB style
  // instructions, but they operate on top/bottom, requiring some sort of lane
  // interleaving to be used with zext/sext.
  unsigned DstEltSize = DstTy->getScalarSizeInBits();
  if (!useNeonVector(DstTy) || Args.size() != 2 ||
      (DstEltSize != 16 && DstEltSize != 32 && DstEltSize != 64))
    return false;

  // Determine if the operation has a widening variant. We consider both the
  // "long" (e.g., usubl) and "wide" (e.g., usubw) versions of the
  // instructions.
  //
  // TODO: Add additional widening operations (e.g., shl, etc.) once we
  //       verify that their extending operands are eliminated during code
  //       generation.
  Type *SrcTy = SrcOverrideTy;
  switch (Opcode) {
  case Instruction::Add: // UADDL(2), SADDL(2), UADDW(2), SADDW(2).
  case Instruction::Sub: // USUBL(2), SSUBL(2), USUBW(2), SSUBW(2).
    // The second operand needs to be an extend
    if (isa<SExtInst>(Args[1]) || isa<ZExtInst>(Args[1])) {
      if (!SrcTy)
        SrcTy =
            toVectorTy(cast<Instruction>(Args[1])->getOperand(0)->getType());
    } else
      return false;
    break;
  case Instruction::Mul: { // SMULL(2), UMULL(2)
    // Both operands need to be extends of the same type.
    if ((isa<SExtInst>(Args[0]) && isa<SExtInst>(Args[1])) ||
        (isa<ZExtInst>(Args[0]) && isa<ZExtInst>(Args[1]))) {
      if (!SrcTy)
        SrcTy =
            toVectorTy(cast<Instruction>(Args[0])->getOperand(0)->getType());
    } else if (isa<ZExtInst>(Args[0]) || isa<ZExtInst>(Args[1])) {
      // If one of the operands is a Zext and the other has enough zero bits to
      // be treated as unsigned, we can still general a umull, meaning the zext
      // is free.
      KnownBits Known =
          computeKnownBits(isa<ZExtInst>(Args[0]) ? Args[1] : Args[0], DL);
      if (Args[0]->getType()->getScalarSizeInBits() -
              Known.Zero.countLeadingOnes() >
          DstTy->getScalarSizeInBits() / 2)
        return false;
      if (!SrcTy)
        SrcTy = toVectorTy(Type::getIntNTy(DstTy->getContext(),
                                           DstTy->getScalarSizeInBits() / 2));
    } else
      return false;
    break;
  }
  default:
    return false;
  }

  // Legalize the destination type and ensure it can be used in a widening
  // operation.
  auto DstTyL = getTypeLegalizationCost(DstTy);
  if (!DstTyL.second.isVector() || DstEltSize != DstTy->getScalarSizeInBits())
    return false;

  // Legalize the source type and ensure it can be used in a widening
  // operation.
  assert(SrcTy && "Expected some SrcTy");
  auto SrcTyL = getTypeLegalizationCost(SrcTy);
  unsigned SrcElTySize = SrcTyL.second.getScalarSizeInBits();
  if (!SrcTyL.second.isVector() || SrcElTySize != SrcTy->getScalarSizeInBits())
    return false;

  // Get the total number of vector elements in the legalized types.
  InstructionCost NumDstEls =
      DstTyL.first * DstTyL.second.getVectorMinNumElements();
  InstructionCost NumSrcEls =
      SrcTyL.first * SrcTyL.second.getVectorMinNumElements();

  // Return true if the legalized types have the same number of vector elements
  // and the destination element type size is twice that of the source type.
  return NumDstEls == NumSrcEls && 2 * SrcElTySize == DstEltSize;
}

// s/urhadd instructions implement the following pattern, making the
// extends free:
//   %x = add ((zext i8 -> i16), 1)
//   %y = (zext i8 -> i16)
//   trunc i16 (lshr (add %x, %y), 1) -> i8
//
bool AArch64TTIImpl::isExtPartOfAvgExpr(const Instruction *ExtUser, Type *Dst,
                                        Type *Src) {
  // The source should be a legal vector type.
  if (!Src->isVectorTy() || !TLI->isTypeLegal(TLI->getValueType(DL, Src)) ||
      (Src->isScalableTy() && !ST->hasSVE2()))
    return false;

  if (ExtUser->getOpcode() != Instruction::Add || !ExtUser->hasOneUse())
    return false;

  // Look for trunc/shl/add before trying to match the pattern.
  const Instruction *Add = ExtUser;
  auto *AddUser =
      dyn_cast_or_null<Instruction>(Add->getUniqueUndroppableUser());
  if (AddUser && AddUser->getOpcode() == Instruction::Add)
    Add = AddUser;

  auto *Shr = dyn_cast_or_null<Instruction>(Add->getUniqueUndroppableUser());
  if (!Shr || Shr->getOpcode() != Instruction::LShr)
    return false;

  auto *Trunc = dyn_cast_or_null<Instruction>(Shr->getUniqueUndroppableUser());
  if (!Trunc || Trunc->getOpcode() != Instruction::Trunc ||
      Src->getScalarSizeInBits() !=
          cast<CastInst>(Trunc)->getDestTy()->getScalarSizeInBits())
    return false;

  // Try to match the whole pattern. Ext could be either the first or second
  // m_ZExtOrSExt matched.
  Instruction *Ex1, *Ex2;
  if (!(match(Add, m_c_Add(m_Instruction(Ex1),
                           m_c_Add(m_Instruction(Ex2), m_SpecificInt(1))))))
    return false;

  // Ensure both extends are of the same type
  if (match(Ex1, m_ZExtOrSExt(m_Value())) &&
      Ex1->getOpcode() == Ex2->getOpcode())
    return true;

  return false;
}

InstructionCost AArch64TTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst,
                                                 Type *Src,
                                                 TTI::CastContextHint CCH,
                                                 TTI::TargetCostKind CostKind,
                                                 const Instruction *I) {
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");
  // If the cast is observable, and it is used by a widening instruction (e.g.,
  // uaddl, saddw, etc.), it may be free.
  if (I && I->hasOneUser()) {
    auto *SingleUser = cast<Instruction>(*I->user_begin());
    SmallVector<const Value *, 4> Operands(SingleUser->operand_values());
    if (isWideningInstruction(Dst, SingleUser->getOpcode(), Operands, Src)) {
      // For adds only count the second operand as free if both operands are
      // extends but not the same operation. (i.e both operands are not free in
      // add(sext, zext)).
      if (SingleUser->getOpcode() == Instruction::Add) {
        if (I == SingleUser->getOperand(1) ||
            (isa<CastInst>(SingleUser->getOperand(1)) &&
             cast<CastInst>(SingleUser->getOperand(1))->getOpcode() == Opcode))
          return 0;
      } else // Others are free so long as isWideningInstruction returned true.
        return 0;
    }

    // The cast will be free for the s/urhadd instructions
    if ((isa<ZExtInst>(I) || isa<SExtInst>(I)) &&
        isExtPartOfAvgExpr(SingleUser, Dst, Src))
      return 0;
  }

  // TODO: Allow non-throughput costs that aren't binary.
  auto AdjustCost = [&CostKind](InstructionCost Cost) -> InstructionCost {
    if (CostKind != TTI::TCK_RecipThroughput)
      return Cost == 0 ? 0 : 1;
    return Cost;
  };

  EVT SrcTy = TLI->getValueType(DL, Src);
  EVT DstTy = TLI->getValueType(DL, Dst);

  if (!SrcTy.isSimple() || !DstTy.isSimple())
    return AdjustCost(
        BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I));

  static const TypeConversionCostTblEntry
  ConversionTbl[] = {
    { ISD::TRUNCATE, MVT::v2i8,   MVT::v2i64,  1},  // xtn
    { ISD::TRUNCATE, MVT::v2i16,  MVT::v2i64,  1},  // xtn
    { ISD::TRUNCATE, MVT::v2i32,  MVT::v2i64,  1},  // xtn
    { ISD::TRUNCATE, MVT::v4i8,   MVT::v4i32,  1},  // xtn
    { ISD::TRUNCATE, MVT::v4i8,   MVT::v4i64,  3},  // 2 xtn + 1 uzp1
    { ISD::TRUNCATE, MVT::v4i16,  MVT::v4i32,  1},  // xtn
    { ISD::TRUNCATE, MVT::v4i16,  MVT::v4i64,  2},  // 1 uzp1 + 1 xtn
    { ISD::TRUNCATE, MVT::v4i32,  MVT::v4i64,  1},  // 1 uzp1
    { ISD::TRUNCATE, MVT::v8i8,   MVT::v8i16,  1},  // 1 xtn
    { ISD::TRUNCATE, MVT::v8i8,   MVT::v8i32,  2},  // 1 uzp1 + 1 xtn
    { ISD::TRUNCATE, MVT::v8i8,   MVT::v8i64,  4},  // 3 x uzp1 + xtn
    { ISD::TRUNCATE, MVT::v8i16,  MVT::v8i32,  1},  // 1 uzp1
    { ISD::TRUNCATE, MVT::v8i16,  MVT::v8i64,  3},  // 3 x uzp1
    { ISD::TRUNCATE, MVT::v8i32,  MVT::v8i64,  2},  // 2 x uzp1
    { ISD::TRUNCATE, MVT::v16i8,  MVT::v16i16, 1},  // uzp1
    { ISD::TRUNCATE, MVT::v16i8,  MVT::v16i32, 3},  // (2 + 1) x uzp1
    { ISD::TRUNCATE, MVT::v16i8,  MVT::v16i64, 7},  // (4 + 2 + 1) x uzp1
    { ISD::TRUNCATE, MVT::v16i16, MVT::v16i32, 2},  // 2 x uzp1
    { ISD::TRUNCATE, MVT::v16i16, MVT::v16i64, 6},  // (4 + 2) x uzp1
    { ISD::TRUNCATE, MVT::v16i32, MVT::v16i64, 4},  // 4 x uzp1

    // Truncations on nxvmiN
    { ISD::TRUNCATE, MVT::nxv2i1, MVT::nxv2i16, 1 },
    { ISD::TRUNCATE, MVT::nxv2i1, MVT::nxv2i32, 1 },
    { ISD::TRUNCATE, MVT::nxv2i1, MVT::nxv2i64, 1 },
    { ISD::TRUNCATE, MVT::nxv4i1, MVT::nxv4i16, 1 },
    { ISD::TRUNCATE, MVT::nxv4i1, MVT::nxv4i32, 1 },
    { ISD::TRUNCATE, MVT::nxv4i1, MVT::nxv4i64, 2 },
    { ISD::TRUNCATE, MVT::nxv8i1, MVT::nxv8i16, 1 },
    { ISD::TRUNCATE, MVT::nxv8i1, MVT::nxv8i32, 3 },
    { ISD::TRUNCATE, MVT::nxv8i1, MVT::nxv8i64, 5 },
    { ISD::TRUNCATE, MVT::nxv16i1, MVT::nxv16i8, 1 },
    { ISD::TRUNCATE, MVT::nxv2i16, MVT::nxv2i32, 1 },
    { ISD::TRUNCATE, MVT::nxv2i32, MVT::nxv2i64, 1 },
    { ISD::TRUNCATE, MVT::nxv4i16, MVT::nxv4i32, 1 },
    { ISD::TRUNCATE, MVT::nxv4i32, MVT::nxv4i64, 2 },
    { ISD::TRUNCATE, MVT::nxv8i16, MVT::nxv8i32, 3 },
    { ISD::TRUNCATE, MVT::nxv8i32, MVT::nxv8i64, 6 },

    // The number of shll instructions for the extension.
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i16, 3 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i16, 3 },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32, 2 },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32, 2 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i8,  3 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i8,  3 },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16, 2 },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16, 2 },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i8,  7 },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i8,  7 },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i16, 6 },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i16, 6 },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8, 2 },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8, 2 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8, 6 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8, 6 },

    // LowerVectorINT_TO_FP:
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i32, 1 },
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v4i32, 1 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i64, 1 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i32, 1 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v4i32, 1 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i64, 1 },

    // Complex: to v2f32
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i8,  3 },
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i16, 3 },
    { ISD::SINT_TO_FP, MVT::v2f32, MVT::v2i64, 2 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i8,  3 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i16, 3 },
    { ISD::UINT_TO_FP, MVT::v2f32, MVT::v2i64, 2 },

    // Complex: to v4f32
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v4i8,  4 },
    { ISD::SINT_TO_FP, MVT::v4f32, MVT::v4i16, 2 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v4i8,  3 },
    { ISD::UINT_TO_FP, MVT::v4f32, MVT::v4i16, 2 },

    // Complex: to v8f32
    { ISD::SINT_TO_FP, MVT::v8f32, MVT::v8i8,  10 },
    { ISD::SINT_TO_FP, MVT::v8f32, MVT::v8i16, 4 },
    { ISD::UINT_TO_FP, MVT::v8f32, MVT::v8i8,  10 },
    { ISD::UINT_TO_FP, MVT::v8f32, MVT::v8i16, 4 },

    // Complex: to v16f32
    { ISD::SINT_TO_FP, MVT::v16f32, MVT::v16i8, 21 },
    { ISD::UINT_TO_FP, MVT::v16f32, MVT::v16i8, 21 },

    // Complex: to v2f64
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i8,  4 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i16, 4 },
    { ISD::SINT_TO_FP, MVT::v2f64, MVT::v2i32, 2 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i8,  4 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i16, 4 },
    { ISD::UINT_TO_FP, MVT::v2f64, MVT::v2i32, 2 },

    // Complex: to v4f64
    { ISD::SINT_TO_FP, MVT::v4f64, MVT::v4i32,  4 },
    { ISD::UINT_TO_FP, MVT::v4f64, MVT::v4i32,  4 },

    // LowerVectorFP_TO_INT
    { ISD::FP_TO_SINT, MVT::v2i32, MVT::v2f32, 1 },
    { ISD::FP_TO_SINT, MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_SINT, MVT::v2i64, MVT::v2f64, 1 },
    { ISD::FP_TO_UINT, MVT::v2i32, MVT::v2f32, 1 },
    { ISD::FP_TO_UINT, MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_UINT, MVT::v2i64, MVT::v2f64, 1 },

    // Complex, from v2f32: legal type is v2i32 (no cost) or v2i64 (1 ext).
    { ISD::FP_TO_SINT, MVT::v2i64, MVT::v2f32, 2 },
    { ISD::FP_TO_SINT, MVT::v2i16, MVT::v2f32, 1 },
    { ISD::FP_TO_SINT, MVT::v2i8,  MVT::v2f32, 1 },
    { ISD::FP_TO_UINT, MVT::v2i64, MVT::v2f32, 2 },
    { ISD::FP_TO_UINT, MVT::v2i16, MVT::v2f32, 1 },
    { ISD::FP_TO_UINT, MVT::v2i8,  MVT::v2f32, 1 },

    // Complex, from v4f32: legal type is v4i16, 1 narrowing => ~2
    { ISD::FP_TO_SINT, MVT::v4i16, MVT::v4f32, 2 },
    { ISD::FP_TO_SINT, MVT::v4i8,  MVT::v4f32, 2 },
    { ISD::FP_TO_UINT, MVT::v4i16, MVT::v4f32, 2 },
    { ISD::FP_TO_UINT, MVT::v4i8,  MVT::v4f32, 2 },

    // Complex, from nxv2f32.
    { ISD::FP_TO_SINT, MVT::nxv2i64, MVT::nxv2f32, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i32, MVT::nxv2f32, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i16, MVT::nxv2f32, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i8,  MVT::nxv2f32, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i64, MVT::nxv2f32, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i32, MVT::nxv2f32, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i16, MVT::nxv2f32, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i8,  MVT::nxv2f32, 1 },

    // Complex, from v2f64: legal type is v2i32, 1 narrowing => ~2.
    { ISD::FP_TO_SINT, MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_SINT, MVT::v2i16, MVT::v2f64, 2 },
    { ISD::FP_TO_SINT, MVT::v2i8,  MVT::v2f64, 2 },
    { ISD::FP_TO_UINT, MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_UINT, MVT::v2i16, MVT::v2f64, 2 },
    { ISD::FP_TO_UINT, MVT::v2i8,  MVT::v2f64, 2 },

    // Complex, from nxv2f64.
    { ISD::FP_TO_SINT, MVT::nxv2i64, MVT::nxv2f64, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i32, MVT::nxv2f64, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i16, MVT::nxv2f64, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i8,  MVT::nxv2f64, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i64, MVT::nxv2f64, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i32, MVT::nxv2f64, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i16, MVT::nxv2f64, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i8,  MVT::nxv2f64, 1 },

    // Complex, from nxv4f32.
    { ISD::FP_TO_SINT, MVT::nxv4i64, MVT::nxv4f32, 4 },
    { ISD::FP_TO_SINT, MVT::nxv4i32, MVT::nxv4f32, 1 },
    { ISD::FP_TO_SINT, MVT::nxv4i16, MVT::nxv4f32, 1 },
    { ISD::FP_TO_SINT, MVT::nxv4i8,  MVT::nxv4f32, 1 },
    { ISD::FP_TO_UINT, MVT::nxv4i64, MVT::nxv4f32, 4 },
    { ISD::FP_TO_UINT, MVT::nxv4i32, MVT::nxv4f32, 1 },
    { ISD::FP_TO_UINT, MVT::nxv4i16, MVT::nxv4f32, 1 },
    { ISD::FP_TO_UINT, MVT::nxv4i8,  MVT::nxv4f32, 1 },

    // Complex, from nxv8f64. Illegal -> illegal conversions not required.
    { ISD::FP_TO_SINT, MVT::nxv8i16, MVT::nxv8f64, 7 },
    { ISD::FP_TO_SINT, MVT::nxv8i8,  MVT::nxv8f64, 7 },
    { ISD::FP_TO_UINT, MVT::nxv8i16, MVT::nxv8f64, 7 },
    { ISD::FP_TO_UINT, MVT::nxv8i8,  MVT::nxv8f64, 7 },

    // Complex, from nxv4f64. Illegal -> illegal conversions not required.
    { ISD::FP_TO_SINT, MVT::nxv4i32, MVT::nxv4f64, 3 },
    { ISD::FP_TO_SINT, MVT::nxv4i16, MVT::nxv4f64, 3 },
    { ISD::FP_TO_SINT, MVT::nxv4i8,  MVT::nxv4f64, 3 },
    { ISD::FP_TO_UINT, MVT::nxv4i32, MVT::nxv4f64, 3 },
    { ISD::FP_TO_UINT, MVT::nxv4i16, MVT::nxv4f64, 3 },
    { ISD::FP_TO_UINT, MVT::nxv4i8,  MVT::nxv4f64, 3 },

    // Complex, from nxv8f32. Illegal -> illegal conversions not required.
    { ISD::FP_TO_SINT, MVT::nxv8i16, MVT::nxv8f32, 3 },
    { ISD::FP_TO_SINT, MVT::nxv8i8,  MVT::nxv8f32, 3 },
    { ISD::FP_TO_UINT, MVT::nxv8i16, MVT::nxv8f32, 3 },
    { ISD::FP_TO_UINT, MVT::nxv8i8,  MVT::nxv8f32, 3 },

    // Complex, from nxv8f16.
    { ISD::FP_TO_SINT, MVT::nxv8i64, MVT::nxv8f16, 10 },
    { ISD::FP_TO_SINT, MVT::nxv8i32, MVT::nxv8f16, 4 },
    { ISD::FP_TO_SINT, MVT::nxv8i16, MVT::nxv8f16, 1 },
    { ISD::FP_TO_SINT, MVT::nxv8i8,  MVT::nxv8f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv8i64, MVT::nxv8f16, 10 },
    { ISD::FP_TO_UINT, MVT::nxv8i32, MVT::nxv8f16, 4 },
    { ISD::FP_TO_UINT, MVT::nxv8i16, MVT::nxv8f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv8i8,  MVT::nxv8f16, 1 },

    // Complex, from nxv4f16.
    { ISD::FP_TO_SINT, MVT::nxv4i64, MVT::nxv4f16, 4 },
    { ISD::FP_TO_SINT, MVT::nxv4i32, MVT::nxv4f16, 1 },
    { ISD::FP_TO_SINT, MVT::nxv4i16, MVT::nxv4f16, 1 },
    { ISD::FP_TO_SINT, MVT::nxv4i8,  MVT::nxv4f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv4i64, MVT::nxv4f16, 4 },
    { ISD::FP_TO_UINT, MVT::nxv4i32, MVT::nxv4f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv4i16, MVT::nxv4f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv4i8,  MVT::nxv4f16, 1 },

    // Complex, from nxv2f16.
    { ISD::FP_TO_SINT, MVT::nxv2i64, MVT::nxv2f16, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i32, MVT::nxv2f16, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i16, MVT::nxv2f16, 1 },
    { ISD::FP_TO_SINT, MVT::nxv2i8,  MVT::nxv2f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i64, MVT::nxv2f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i32, MVT::nxv2f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i16, MVT::nxv2f16, 1 },
    { ISD::FP_TO_UINT, MVT::nxv2i8,  MVT::nxv2f16, 1 },

    // Truncate from nxvmf32 to nxvmf16.
    { ISD::FP_ROUND, MVT::nxv2f16, MVT::nxv2f32, 1 },
    { ISD::FP_ROUND, MVT::nxv4f16, MVT::nxv4f32, 1 },
    { ISD::FP_ROUND, MVT::nxv8f16, MVT::nxv8f32, 3 },

    // Truncate from nxvmf64 to nxvmf16.
    { ISD::FP_ROUND, MVT::nxv2f16, MVT::nxv2f64, 1 },
    { ISD::FP_ROUND, MVT::nxv4f16, MVT::nxv4f64, 3 },
    { ISD::FP_ROUND, MVT::nxv8f16, MVT::nxv8f64, 7 },

    // Truncate from nxvmf64 to nxvmf32.
    { ISD::FP_ROUND, MVT::nxv2f32, MVT::nxv2f64, 1 },
    { ISD::FP_ROUND, MVT::nxv4f32, MVT::nxv4f64, 3 },
    { ISD::FP_ROUND, MVT::nxv8f32, MVT::nxv8f64, 6 },

    // Extend from nxvmf16 to nxvmf32.
    { ISD::FP_EXTEND, MVT::nxv2f32, MVT::nxv2f16, 1},
    { ISD::FP_EXTEND, MVT::nxv4f32, MVT::nxv4f16, 1},
    { ISD::FP_EXTEND, MVT::nxv8f32, MVT::nxv8f16, 2},

    // Extend from nxvmf16 to nxvmf64.
    { ISD::FP_EXTEND, MVT::nxv2f64, MVT::nxv2f16, 1},
    { ISD::FP_EXTEND, MVT::nxv4f64, MVT::nxv4f16, 2},
    { ISD::FP_EXTEND, MVT::nxv8f64, MVT::nxv8f16, 4},

    // Extend from nxvmf32 to nxvmf64.
    { ISD::FP_EXTEND, MVT::nxv2f64, MVT::nxv2f32, 1},
    { ISD::FP_EXTEND, MVT::nxv4f64, MVT::nxv4f32, 2},
    { ISD::FP_EXTEND, MVT::nxv8f64, MVT::nxv8f32, 6},

    // Bitcasts from float to integer
    { ISD::BITCAST, MVT::nxv2f16, MVT::nxv2i16, 0 },
    { ISD::BITCAST, MVT::nxv4f16, MVT::nxv4i16, 0 },
    { ISD::BITCAST, MVT::nxv2f32, MVT::nxv2i32, 0 },

    // Bitcasts from integer to float
    { ISD::BITCAST, MVT::nxv2i16, MVT::nxv2f16, 0 },
    { ISD::BITCAST, MVT::nxv4i16, MVT::nxv4f16, 0 },
    { ISD::BITCAST, MVT::nxv2i32, MVT::nxv2f32, 0 },

    // Add cost for extending to illegal -too wide- scalable vectors.
    // zero/sign extend are implemented by multiple unpack operations,
    // where each operation has a cost of 1.
    { ISD::ZERO_EXTEND, MVT::nxv16i16, MVT::nxv16i8, 2},
    { ISD::ZERO_EXTEND, MVT::nxv16i32, MVT::nxv16i8, 6},
    { ISD::ZERO_EXTEND, MVT::nxv16i64, MVT::nxv16i8, 14},
    { ISD::ZERO_EXTEND, MVT::nxv8i32, MVT::nxv8i16, 2},
    { ISD::ZERO_EXTEND, MVT::nxv8i64, MVT::nxv8i16, 6},
    { ISD::ZERO_EXTEND, MVT::nxv4i64, MVT::nxv4i32, 2},

    { ISD::SIGN_EXTEND, MVT::nxv16i16, MVT::nxv16i8, 2},
    { ISD::SIGN_EXTEND, MVT::nxv16i32, MVT::nxv16i8, 6},
    { ISD::SIGN_EXTEND, MVT::nxv16i64, MVT::nxv16i8, 14},
    { ISD::SIGN_EXTEND, MVT::nxv8i32, MVT::nxv8i16, 2},
    { ISD::SIGN_EXTEND, MVT::nxv8i64, MVT::nxv8i16, 6},
    { ISD::SIGN_EXTEND, MVT::nxv4i64, MVT::nxv4i32, 2},
  };

  // We have to estimate a cost of fixed length operation upon
  // SVE registers(operations) with the number of registers required
  // for a fixed type to be represented upon SVE registers.
  EVT WiderTy = SrcTy.bitsGT(DstTy) ? SrcTy : DstTy;
  if (SrcTy.isFixedLengthVector() && DstTy.isFixedLengthVector() &&
      SrcTy.getVectorNumElements() == DstTy.getVectorNumElements() &&
      ST->useSVEForFixedLengthVectors(WiderTy)) {
    std::pair<InstructionCost, MVT> LT =
        getTypeLegalizationCost(WiderTy.getTypeForEVT(Dst->getContext()));
    unsigned NumElements = AArch64::SVEBitsPerBlock /
                           LT.second.getScalarSizeInBits();
    return AdjustCost(
        LT.first *
        getCastInstrCost(
            Opcode, ScalableVectorType::get(Dst->getScalarType(), NumElements),
            ScalableVectorType::get(Src->getScalarType(), NumElements), CCH,
            CostKind, I));
  }

  if (const auto *Entry = ConvertCostTableLookup(ConversionTbl, ISD,
                                                 DstTy.getSimpleVT(),
                                                 SrcTy.getSimpleVT()))
    return AdjustCost(Entry->Cost);

  static const TypeConversionCostTblEntry FP16Tbl[] = {
      {ISD::FP_TO_SINT, MVT::v4i8, MVT::v4f16, 1}, // fcvtzs
      {ISD::FP_TO_UINT, MVT::v4i8, MVT::v4f16, 1},
      {ISD::FP_TO_SINT, MVT::v4i16, MVT::v4f16, 1}, // fcvtzs
      {ISD::FP_TO_UINT, MVT::v4i16, MVT::v4f16, 1},
      {ISD::FP_TO_SINT, MVT::v4i32, MVT::v4f16, 2}, // fcvtl+fcvtzs
      {ISD::FP_TO_UINT, MVT::v4i32, MVT::v4f16, 2},
      {ISD::FP_TO_SINT, MVT::v8i8, MVT::v8f16, 2}, // fcvtzs+xtn
      {ISD::FP_TO_UINT, MVT::v8i8, MVT::v8f16, 2},
      {ISD::FP_TO_SINT, MVT::v8i16, MVT::v8f16, 1}, // fcvtzs
      {ISD::FP_TO_UINT, MVT::v8i16, MVT::v8f16, 1},
      {ISD::FP_TO_SINT, MVT::v8i32, MVT::v8f16, 4}, // 2*fcvtl+2*fcvtzs
      {ISD::FP_TO_UINT, MVT::v8i32, MVT::v8f16, 4},
      {ISD::FP_TO_SINT, MVT::v16i8, MVT::v16f16, 3}, // 2*fcvtzs+xtn
      {ISD::FP_TO_UINT, MVT::v16i8, MVT::v16f16, 3},
      {ISD::FP_TO_SINT, MVT::v16i16, MVT::v16f16, 2}, // 2*fcvtzs
      {ISD::FP_TO_UINT, MVT::v16i16, MVT::v16f16, 2},
      {ISD::FP_TO_SINT, MVT::v16i32, MVT::v16f16, 8}, // 4*fcvtl+4*fcvtzs
      {ISD::FP_TO_UINT, MVT::v16i32, MVT::v16f16, 8},
      {ISD::UINT_TO_FP, MVT::v8f16, MVT::v8i8, 2},   // ushll + ucvtf
      {ISD::SINT_TO_FP, MVT::v8f16, MVT::v8i8, 2},   // sshll + scvtf
      {ISD::UINT_TO_FP, MVT::v16f16, MVT::v16i8, 4}, // 2 * ushl(2) + 2 * ucvtf
      {ISD::SINT_TO_FP, MVT::v16f16, MVT::v16i8, 4}, // 2 * sshl(2) + 2 * scvtf
  };

  if (ST->hasFullFP16())
    if (const auto *Entry = ConvertCostTableLookup(
            FP16Tbl, ISD, DstTy.getSimpleVT(), SrcTy.getSimpleVT()))
      return AdjustCost(Entry->Cost);

  if ((ISD == ISD::ZERO_EXTEND || ISD == ISD::SIGN_EXTEND) &&
      CCH == TTI::CastContextHint::Masked &&
      ST->isSVEorStreamingSVEAvailable() &&
      TLI->getTypeAction(Src->getContext(), SrcTy) ==
          TargetLowering::TypePromoteInteger &&
      TLI->getTypeAction(Dst->getContext(), DstTy) ==
          TargetLowering::TypeSplitVector) {
    // The standard behaviour in the backend for these cases is to split the
    // extend up into two parts:
    //  1. Perform an extending load or masked load up to the legal type.
    //  2. Extend the loaded data to the final type.
    std::pair<InstructionCost, MVT> SrcLT = getTypeLegalizationCost(Src);
    Type *LegalTy = EVT(SrcLT.second).getTypeForEVT(Src->getContext());
    InstructionCost Part1 = AArch64TTIImpl::getCastInstrCost(
        Opcode, LegalTy, Src, CCH, CostKind, I);
    InstructionCost Part2 = AArch64TTIImpl::getCastInstrCost(
        Opcode, Dst, LegalTy, TTI::CastContextHint::None, CostKind, I);
    return Part1 + Part2;
  }

  // The BasicTTIImpl version only deals with CCH==TTI::CastContextHint::Normal,
  // but we also want to include the TTI::CastContextHint::Masked case too.
  if ((ISD == ISD::ZERO_EXTEND || ISD == ISD::SIGN_EXTEND) &&
      CCH == TTI::CastContextHint::Masked &&
      ST->isSVEorStreamingSVEAvailable() && TLI->isTypeLegal(DstTy))
    CCH = TTI::CastContextHint::Normal;

  return AdjustCost(
      BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I));
}

InstructionCost AArch64TTIImpl::getExtractWithExtendCost(unsigned Opcode,
                                                         Type *Dst,
                                                         VectorType *VecTy,
                                                         unsigned Index) {

  // Make sure we were given a valid extend opcode.
  assert((Opcode == Instruction::SExt || Opcode == Instruction::ZExt) &&
         "Invalid opcode");

  // We are extending an element we extract from a vector, so the source type
  // of the extend is the element type of the vector.
  auto *Src = VecTy->getElementType();

  // Sign- and zero-extends are for integer types only.
  assert(isa<IntegerType>(Dst) && isa<IntegerType>(Src) && "Invalid type");

  // Get the cost for the extract. We compute the cost (if any) for the extend
  // below.
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  InstructionCost Cost = getVectorInstrCost(Instruction::ExtractElement, VecTy,
                                            CostKind, Index, nullptr, nullptr);

  // Legalize the types.
  auto VecLT = getTypeLegalizationCost(VecTy);
  auto DstVT = TLI->getValueType(DL, Dst);
  auto SrcVT = TLI->getValueType(DL, Src);

  // If the resulting type is still a vector and the destination type is legal,
  // we may get the extension for free. If not, get the default cost for the
  // extend.
  if (!VecLT.second.isVector() || !TLI->isTypeLegal(DstVT))
    return Cost + getCastInstrCost(Opcode, Dst, Src, TTI::CastContextHint::None,
                                   CostKind);

  // The destination type should be larger than the element type. If not, get
  // the default cost for the extend.
  if (DstVT.getFixedSizeInBits() < SrcVT.getFixedSizeInBits())
    return Cost + getCastInstrCost(Opcode, Dst, Src, TTI::CastContextHint::None,
                                   CostKind);

  switch (Opcode) {
  default:
    llvm_unreachable("Opcode should be either SExt or ZExt");

  // For sign-extends, we only need a smov, which performs the extension
  // automatically.
  case Instruction::SExt:
    return Cost;

  // For zero-extends, the extend is performed automatically by a umov unless
  // the destination type is i64 and the element type is i8 or i16.
  case Instruction::ZExt:
    if (DstVT.getSizeInBits() != 64u || SrcVT.getSizeInBits() == 32u)
      return Cost;
  }

  // If we are unable to perform the extend for free, get the default cost.
  return Cost + getCastInstrCost(Opcode, Dst, Src, TTI::CastContextHint::None,
                                 CostKind);
}

InstructionCost AArch64TTIImpl::getCFInstrCost(unsigned Opcode,
                                               TTI::TargetCostKind CostKind,
                                               const Instruction *I) {
  if (CostKind != TTI::TCK_RecipThroughput)
    return Opcode == Instruction::PHI ? 0 : 1;
  assert(CostKind == TTI::TCK_RecipThroughput && "unexpected CostKind");
  // Branches are assumed to be predicted.
  return 0;
}

InstructionCost AArch64TTIImpl::getVectorInstrCostHelper(const Instruction *I,
                                                         Type *Val,
                                                         unsigned Index,
                                                         bool HasRealUse) {
  assert(Val->isVectorTy() && "This must be a vector type");

  if (Index != -1U) {
    // Legalize the type.
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Val);

    // This type is legalized to a scalar type.
    if (!LT.second.isVector())
      return 0;

    // The type may be split. For fixed-width vectors we can normalize the
    // index to the new type.
    if (LT.second.isFixedLengthVector()) {
      unsigned Width = LT.second.getVectorNumElements();
      Index = Index % Width;
    }

    // The element at index zero is already inside the vector.
    // - For a physical (HasRealUse==true) insert-element or extract-element
    // instruction that extracts integers, an explicit FPR -> GPR move is
    // needed. So it has non-zero cost.
    // - For the rest of cases (virtual instruction or element type is float),
    // consider the instruction free.
    if (Index == 0 && (!HasRealUse || !Val->getScalarType()->isIntegerTy()))
      return 0;

    // This is recognising a LD1 single-element structure to one lane of one
    // register instruction. I.e., if this is an `insertelement` instruction,
    // and its second operand is a load, then we will generate a LD1, which
    // are expensive instructions.
    if (I && dyn_cast<LoadInst>(I->getOperand(1)))
      return ST->getVectorInsertExtractBaseCost() + 1;

    // i1 inserts and extract will include an extra cset or cmp of the vector
    // value. Increase the cost by 1 to account.
    if (Val->getScalarSizeInBits() == 1)
      return ST->getVectorInsertExtractBaseCost() + 1;

    // FIXME:
    // If the extract-element and insert-element instructions could be
    // simplified away (e.g., could be combined into users by looking at use-def
    // context), they have no cost. This is not done in the first place for
    // compile-time considerations.
  }

  // All other insert/extracts cost this much.
  return ST->getVectorInsertExtractBaseCost();
}

InstructionCost AArch64TTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val,
                                                   TTI::TargetCostKind CostKind,
                                                   unsigned Index, Value *Op0,
                                                   Value *Op1) {
  bool HasRealUse =
      Opcode == Instruction::InsertElement && Op0 && !isa<UndefValue>(Op0);
  return getVectorInstrCostHelper(nullptr, Val, Index, HasRealUse);
}

InstructionCost AArch64TTIImpl::getVectorInstrCost(const Instruction &I,
                                                   Type *Val,
                                                   TTI::TargetCostKind CostKind,
                                                   unsigned Index) {
  return getVectorInstrCostHelper(&I, Val, Index, true /* HasRealUse */);
}

InstructionCost AArch64TTIImpl::getScalarizationOverhead(
    VectorType *Ty, const APInt &DemandedElts, bool Insert, bool Extract,
    TTI::TargetCostKind CostKind) {
  if (isa<ScalableVectorType>(Ty))
    return InstructionCost::getInvalid();
  if (Ty->getElementType()->isFloatingPointTy())
    return BaseT::getScalarizationOverhead(Ty, DemandedElts, Insert, Extract,
                                           CostKind);
  return DemandedElts.popcount() * (Insert + Extract) *
         ST->getVectorInsertExtractBaseCost();
}

InstructionCost AArch64TTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {

  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  if (auto *VTy = dyn_cast<ScalableVectorType>(Ty))
    if (VTy->getElementCount() == ElementCount::getScalable(1))
      return InstructionCost::getInvalid();

  // TODO: Handle more cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info, Args, CxtI);

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);
  int ISD = TLI->InstructionOpcodeToISD(Opcode);

  switch (ISD) {
  default:
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info);
  case ISD::SDIV:
    if (Op2Info.isConstant() && Op2Info.isUniform() && Op2Info.isPowerOf2()) {
      // On AArch64, scalar signed division by constants power-of-two are
      // normally expanded to the sequence ADD + CMP + SELECT + SRA.
      // The OperandValue properties many not be same as that of previous
      // operation; conservatively assume OP_None.
      InstructionCost Cost = getArithmeticInstrCost(
          Instruction::Add, Ty, CostKind,
          Op1Info.getNoProps(), Op2Info.getNoProps());
      Cost += getArithmeticInstrCost(Instruction::Sub, Ty, CostKind,
                                     Op1Info.getNoProps(), Op2Info.getNoProps());
      Cost += getArithmeticInstrCost(
          Instruction::Select, Ty, CostKind,
          Op1Info.getNoProps(), Op2Info.getNoProps());
      Cost += getArithmeticInstrCost(Instruction::AShr, Ty, CostKind,
                                     Op1Info.getNoProps(), Op2Info.getNoProps());
      return Cost;
    }
    [[fallthrough]];
  case ISD::UDIV: {
    if (Op2Info.isConstant() && Op2Info.isUniform()) {
      auto VT = TLI->getValueType(DL, Ty);
      if (TLI->isOperationLegalOrCustom(ISD::MULHU, VT)) {
        // Vector signed division by constant are expanded to the
        // sequence MULHS + ADD/SUB + SRA + SRL + ADD, and unsigned division
        // to MULHS + SUB + SRL + ADD + SRL.
        InstructionCost MulCost = getArithmeticInstrCost(
            Instruction::Mul, Ty, CostKind, Op1Info.getNoProps(), Op2Info.getNoProps());
        InstructionCost AddCost = getArithmeticInstrCost(
            Instruction::Add, Ty, CostKind, Op1Info.getNoProps(), Op2Info.getNoProps());
        InstructionCost ShrCost = getArithmeticInstrCost(
            Instruction::AShr, Ty, CostKind, Op1Info.getNoProps(), Op2Info.getNoProps());
        return MulCost * 2 + AddCost * 2 + ShrCost * 2 + 1;
      }
    }

    InstructionCost Cost = BaseT::getArithmeticInstrCost(
        Opcode, Ty, CostKind, Op1Info, Op2Info);
    if (Ty->isVectorTy()) {
      if (TLI->isOperationLegalOrCustom(ISD, LT.second) && ST->hasSVE()) {
        // SDIV/UDIV operations are lowered using SVE, then we can have less
        // costs.
        if (isa<FixedVectorType>(Ty) && cast<FixedVectorType>(Ty)
                                                ->getPrimitiveSizeInBits()
                                                .getFixedValue() < 128) {
          EVT VT = TLI->getValueType(DL, Ty);
          static const CostTblEntry DivTbl[]{
              {ISD::SDIV, MVT::v2i8, 5},  {ISD::SDIV, MVT::v4i8, 8},
              {ISD::SDIV, MVT::v8i8, 8},  {ISD::SDIV, MVT::v2i16, 5},
              {ISD::SDIV, MVT::v4i16, 5}, {ISD::SDIV, MVT::v2i32, 1},
              {ISD::UDIV, MVT::v2i8, 5},  {ISD::UDIV, MVT::v4i8, 8},
              {ISD::UDIV, MVT::v8i8, 8},  {ISD::UDIV, MVT::v2i16, 5},
              {ISD::UDIV, MVT::v4i16, 5}, {ISD::UDIV, MVT::v2i32, 1}};

          const auto *Entry = CostTableLookup(DivTbl, ISD, VT.getSimpleVT());
          if (nullptr != Entry)
            return Entry->Cost;
        }
        // For 8/16-bit elements, the cost is higher because the type
        // requires promotion and possibly splitting:
        if (LT.second.getScalarType() == MVT::i8)
          Cost *= 8;
        else if (LT.second.getScalarType() == MVT::i16)
          Cost *= 4;
        return Cost;
      } else {
        // If one of the operands is a uniform constant then the cost for each
        // element is Cost for insertion, extraction and division.
        // Insertion cost = 2, Extraction Cost = 2, Division = cost for the
        // operation with scalar type
        if ((Op1Info.isConstant() && Op1Info.isUniform()) ||
            (Op2Info.isConstant() && Op2Info.isUniform())) {
          if (auto *VTy = dyn_cast<FixedVectorType>(Ty)) {
            InstructionCost DivCost = BaseT::getArithmeticInstrCost(
                Opcode, Ty->getScalarType(), CostKind, Op1Info, Op2Info);
            return (4 + DivCost) * VTy->getNumElements();
          }
        }
        // On AArch64, without SVE, vector divisions are expanded
        // into scalar divisions of each pair of elements.
        Cost += getArithmeticInstrCost(Instruction::ExtractElement, Ty,
                                       CostKind, Op1Info, Op2Info);
        Cost += getArithmeticInstrCost(Instruction::InsertElement, Ty, CostKind,
                                       Op1Info, Op2Info);
      }

      // TODO: if one of the arguments is scalar, then it's not necessary to
      // double the cost of handling the vector elements.
      Cost += Cost;
    }
    return Cost;
  }
  case ISD::MUL:
    // When SVE is available, then we can lower the v2i64 operation using
    // the SVE mul instruction, which has a lower cost.
    if (LT.second == MVT::v2i64 && ST->hasSVE())
      return LT.first;

    // When SVE is not available, there is no MUL.2d instruction,
    // which means mul <2 x i64> is expensive as elements are extracted
    // from the vectors and the muls scalarized.
    // As getScalarizationOverhead is a bit too pessimistic, we
    // estimate the cost for a i64 vector directly here, which is:
    // - four 2-cost i64 extracts,
    // - two 2-cost i64 inserts, and
    // - two 1-cost muls.
    // So, for a v2i64 with LT.First = 1 the cost is 14, and for a v4i64 with
    // LT.first = 2 the cost is 28. If both operands are extensions it will not
    // need to scalarize so the cost can be cheaper (smull or umull).
    // so the cost can be cheaper (smull or umull).
    if (LT.second != MVT::v2i64 || isWideningInstruction(Ty, Opcode, Args))
      return LT.first;
    return LT.first * 14;
  case ISD::ADD:
  case ISD::XOR:
  case ISD::OR:
  case ISD::AND:
  case ISD::SRL:
  case ISD::SRA:
  case ISD::SHL:
    // These nodes are marked as 'custom' for combining purposes only.
    // We know that they are legal. See LowerAdd in ISelLowering.
    return LT.first;

  case ISD::FNEG:
  case ISD::FADD:
  case ISD::FSUB:
    // Increase the cost for half and bfloat types if not architecturally
    // supported.
    if ((Ty->getScalarType()->isHalfTy() && !ST->hasFullFP16()) ||
        (Ty->getScalarType()->isBFloatTy() && !ST->hasBF16()))
      return 2 * LT.first;
    if (!Ty->getScalarType()->isFP128Ty())
      return LT.first;
    [[fallthrough]];
  case ISD::FMUL:
  case ISD::FDIV:
    // These nodes are marked as 'custom' just to lower them to SVE.
    // We know said lowering will incur no additional cost.
    if (!Ty->getScalarType()->isFP128Ty())
      return 2 * LT.first;

    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info);
  case ISD::FREM:
    // Pass nullptr as fmod/fmodf calls are emitted by the backend even when
    // those functions are not declared in the module.
    if (!Ty->isVectorTy())
      return getCallInstrCost(/*Function*/ nullptr, Ty, {Ty, Ty}, CostKind);
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info);
  }
}

InstructionCost AArch64TTIImpl::getAddressComputationCost(Type *Ty,
                                                          ScalarEvolution *SE,
                                                          const SCEV *Ptr) {
  // Address computations in vectorized code with non-consecutive addresses will
  // likely result in more instructions compared to scalar code where the
  // computation can more often be merged into the index mode. The resulting
  // extra micro-ops can significantly decrease throughput.
  unsigned NumVectorInstToHideOverhead = NeonNonConstStrideOverhead;
  int MaxMergeDistance = 64;

  if (Ty->isVectorTy() && SE &&
      !BaseT::isConstantStridedAccessLessThan(SE, Ptr, MaxMergeDistance + 1))
    return NumVectorInstToHideOverhead;

  // In many cases the address computation is not merged into the instruction
  // addressing mode.
  return 1;
}

InstructionCost AArch64TTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                                   Type *CondTy,
                                                   CmpInst::Predicate VecPred,
                                                   TTI::TargetCostKind CostKind,
                                                   const Instruction *I) {
  // TODO: Handle other cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind,
                                     I);

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  // We don't lower some vector selects well that are wider than the register
  // width.
  if (isa<FixedVectorType>(ValTy) && ISD == ISD::SELECT) {
    // We would need this many instructions to hide the scalarization happening.
    const int AmortizationCost = 20;

    // If VecPred is not set, check if we can get a predicate from the context
    // instruction, if its type matches the requested ValTy.
    if (VecPred == CmpInst::BAD_ICMP_PREDICATE && I && I->getType() == ValTy) {
      CmpInst::Predicate CurrentPred;
      if (match(I, m_Select(m_Cmp(CurrentPred, m_Value(), m_Value()), m_Value(),
                            m_Value())))
        VecPred = CurrentPred;
    }
    // Check if we have a compare/select chain that can be lowered using
    // a (F)CMxx & BFI pair.
    if (CmpInst::isIntPredicate(VecPred) || VecPred == CmpInst::FCMP_OLE ||
        VecPred == CmpInst::FCMP_OLT || VecPred == CmpInst::FCMP_OGT ||
        VecPred == CmpInst::FCMP_OGE || VecPred == CmpInst::FCMP_OEQ ||
        VecPred == CmpInst::FCMP_UNE) {
      static const auto ValidMinMaxTys = {
          MVT::v8i8,  MVT::v16i8, MVT::v4i16, MVT::v8i16, MVT::v2i32,
          MVT::v4i32, MVT::v2i64, MVT::v2f32, MVT::v4f32, MVT::v2f64};
      static const auto ValidFP16MinMaxTys = {MVT::v4f16, MVT::v8f16};

      auto LT = getTypeLegalizationCost(ValTy);
      if (any_of(ValidMinMaxTys, [&LT](MVT M) { return M == LT.second; }) ||
          (ST->hasFullFP16() &&
           any_of(ValidFP16MinMaxTys, [&LT](MVT M) { return M == LT.second; })))
        return LT.first;
    }

    static const TypeConversionCostTblEntry
    VectorSelectTbl[] = {
      { ISD::SELECT, MVT::v2i1, MVT::v2f32, 2 },
      { ISD::SELECT, MVT::v2i1, MVT::v2f64, 2 },
      { ISD::SELECT, MVT::v4i1, MVT::v4f32, 2 },
      { ISD::SELECT, MVT::v4i1, MVT::v4f16, 2 },
      { ISD::SELECT, MVT::v8i1, MVT::v8f16, 2 },
      { ISD::SELECT, MVT::v16i1, MVT::v16i16, 16 },
      { ISD::SELECT, MVT::v8i1, MVT::v8i32, 8 },
      { ISD::SELECT, MVT::v16i1, MVT::v16i32, 16 },
      { ISD::SELECT, MVT::v4i1, MVT::v4i64, 4 * AmortizationCost },
      { ISD::SELECT, MVT::v8i1, MVT::v8i64, 8 * AmortizationCost },
      { ISD::SELECT, MVT::v16i1, MVT::v16i64, 16 * AmortizationCost }
    };

    EVT SelCondTy = TLI->getValueType(DL, CondTy);
    EVT SelValTy = TLI->getValueType(DL, ValTy);
    if (SelCondTy.isSimple() && SelValTy.isSimple()) {
      if (const auto *Entry = ConvertCostTableLookup(VectorSelectTbl, ISD,
                                                     SelCondTy.getSimpleVT(),
                                                     SelValTy.getSimpleVT()))
        return Entry->Cost;
    }
  }

  if (isa<FixedVectorType>(ValTy) && ISD == ISD::SETCC) {
    auto LT = getTypeLegalizationCost(ValTy);
    // Cost v4f16 FCmp without FP16 support via converting to v4f32 and back.
    if (LT.second == MVT::v4f16 && !ST->hasFullFP16())
      return LT.first * 4; // fcvtl + fcvtl + fcmp + xtn
  }

  // Treat the icmp in icmp(and, 0) as free, as we can make use of ands.
  // FIXME: This can apply to more conditions and add/sub if it can be shown to
  // be profitable.
  if (ValTy->isIntegerTy() && ISD == ISD::SETCC && I &&
      ICmpInst::isEquality(VecPred) &&
      TLI->isTypeLegal(TLI->getValueType(DL, ValTy)) &&
      match(I->getOperand(1), m_Zero()) &&
      match(I->getOperand(0), m_And(m_Value(), m_Value())))
    return 0;

  // The base case handles scalable vectors fine for now, since it treats the
  // cost as 1 * legalization cost.
  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind, I);
}

AArch64TTIImpl::TTI::MemCmpExpansionOptions
AArch64TTIImpl::enableMemCmpExpansion(bool OptSize, bool IsZeroCmp) const {
  TTI::MemCmpExpansionOptions Options;
  if (ST->requiresStrictAlign()) {
    // TODO: Add cost modeling for strict align. Misaligned loads expand to
    // a bunch of instructions when strict align is enabled.
    return Options;
  }
  Options.AllowOverlappingLoads = true;
  Options.MaxNumLoads = TLI->getMaxExpandSizeMemcmp(OptSize);
  Options.NumLoadsPerBlock = Options.MaxNumLoads;
  // TODO: Though vector loads usually perform well on AArch64, in some targets
  // they may wake up the FP unit, which raises the power consumption.  Perhaps
  // they could be used with no holds barred (-O3).
  Options.LoadSizes = {8, 4, 2, 1};
  Options.AllowedTailExpansions = {3, 5, 6};
  return Options;
}

bool AArch64TTIImpl::prefersVectorizedAddressing() const {
  return ST->hasSVE();
}

InstructionCost
AArch64TTIImpl::getMaskedMemoryOpCost(unsigned Opcode, Type *Src,
                                      Align Alignment, unsigned AddressSpace,
                                      TTI::TargetCostKind CostKind) {
  if (useNeonVector(Src))
    return BaseT::getMaskedMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                        CostKind);
  auto LT = getTypeLegalizationCost(Src);
  if (!LT.first.isValid())
    return InstructionCost::getInvalid();

  // Return an invalid cost for element types that we are unable to lower.
  auto *VT = cast<VectorType>(Src);
  if (VT->getElementType()->isIntegerTy(1))
    return InstructionCost::getInvalid();

  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  if (VT->getElementCount() == ElementCount::getScalable(1))
    return InstructionCost::getInvalid();

  return LT.first;
}

static unsigned getSVEGatherScatterOverhead(unsigned Opcode) {
  return Opcode == Instruction::Load ? SVEGatherOverhead : SVEScatterOverhead;
}

InstructionCost AArch64TTIImpl::getGatherScatterOpCost(
    unsigned Opcode, Type *DataTy, const Value *Ptr, bool VariableMask,
    Align Alignment, TTI::TargetCostKind CostKind, const Instruction *I) {
  if (useNeonVector(DataTy) || !isLegalMaskedGatherScatter(DataTy))
    return BaseT::getGatherScatterOpCost(Opcode, DataTy, Ptr, VariableMask,
                                         Alignment, CostKind, I);
  auto *VT = cast<VectorType>(DataTy);
  auto LT = getTypeLegalizationCost(DataTy);
  if (!LT.first.isValid())
    return InstructionCost::getInvalid();

  // Return an invalid cost for element types that we are unable to lower.
  if (!LT.second.isVector() ||
      !isElementTypeLegalForScalableVector(VT->getElementType()) ||
      VT->getElementType()->isIntegerTy(1))
    return InstructionCost::getInvalid();

  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  if (VT->getElementCount() == ElementCount::getScalable(1))
    return InstructionCost::getInvalid();

  ElementCount LegalVF = LT.second.getVectorElementCount();
  InstructionCost MemOpCost =
      getMemoryOpCost(Opcode, VT->getElementType(), Alignment, 0, CostKind,
                      {TTI::OK_AnyValue, TTI::OP_None}, I);
  // Add on an overhead cost for using gathers/scatters.
  // TODO: At the moment this is applied unilaterally for all CPUs, but at some
  // point we may want a per-CPU overhead.
  MemOpCost *= getSVEGatherScatterOverhead(Opcode);
  return LT.first * MemOpCost * getMaxNumElements(LegalVF);
}

bool AArch64TTIImpl::useNeonVector(const Type *Ty) const {
  return isa<FixedVectorType>(Ty) && !ST->useSVEForFixedLengthVectors();
}

InstructionCost AArch64TTIImpl::getMemoryOpCost(unsigned Opcode, Type *Ty,
                                                MaybeAlign Alignment,
                                                unsigned AddressSpace,
                                                TTI::TargetCostKind CostKind,
                                                TTI::OperandValueInfo OpInfo,
                                                const Instruction *I) {
  EVT VT = TLI->getValueType(DL, Ty, true);
  // Type legalization can't handle structs
  if (VT == MVT::Other)
    return BaseT::getMemoryOpCost(Opcode, Ty, Alignment, AddressSpace,
                                  CostKind);

  auto LT = getTypeLegalizationCost(Ty);
  if (!LT.first.isValid())
    return InstructionCost::getInvalid();

  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  // We also only support full register predicate loads and stores.
  if (auto *VTy = dyn_cast<ScalableVectorType>(Ty))
    if (VTy->getElementCount() == ElementCount::getScalable(1) ||
        (VTy->getElementType()->isIntegerTy(1) &&
         !VTy->getElementCount().isKnownMultipleOf(
             ElementCount::getScalable(16))))
      return InstructionCost::getInvalid();

  // TODO: consider latency as well for TCK_SizeAndLatency.
  if (CostKind == TTI::TCK_CodeSize || CostKind == TTI::TCK_SizeAndLatency)
    return LT.first;

  if (CostKind != TTI::TCK_RecipThroughput)
    return 1;

  if (ST->isMisaligned128StoreSlow() && Opcode == Instruction::Store &&
      LT.second.is128BitVector() && (!Alignment || *Alignment < Align(16))) {
    // Unaligned stores are extremely inefficient. We don't split all
    // unaligned 128-bit stores because the negative impact that has shown in
    // practice on inlined block copy code.
    // We make such stores expensive so that we will only vectorize if there
    // are 6 other instructions getting vectorized.
    const int AmortizationCost = 6;

    return LT.first * 2 * AmortizationCost;
  }

  // Opaque ptr or ptr vector types are i64s and can be lowered to STP/LDPs.
  if (Ty->isPtrOrPtrVectorTy())
    return LT.first;

  if (useNeonVector(Ty)) {
    // Check truncating stores and extending loads.
    if (Ty->getScalarSizeInBits() != LT.second.getScalarSizeInBits()) {
      // v4i8 types are lowered to scalar a load/store and sshll/xtn.
      if (VT == MVT::v4i8)
        return 2;
      // Otherwise we need to scalarize.
      return cast<FixedVectorType>(Ty)->getNumElements() * 2;
    }
    EVT EltVT = VT.getVectorElementType();
    unsigned EltSize = EltVT.getScalarSizeInBits();
    if (!isPowerOf2_32(EltSize) || EltSize < 8 || EltSize > 64 ||
        VT.getVectorNumElements() >= (128 / EltSize) || !Alignment ||
        *Alignment != Align(1))
      return LT.first;
    // FIXME: v3i8 lowering currently is very inefficient, due to automatic
    // widening to v4i8, which produces suboptimal results.
    if (VT.getVectorNumElements() == 3 && EltVT == MVT::i8)
      return LT.first;

    // Check non-power-of-2 loads/stores for legal vector element types with
    // NEON. Non-power-of-2 memory ops will get broken down to a set of
    // operations on smaller power-of-2 ops, including ld1/st1.
    LLVMContext &C = Ty->getContext();
    InstructionCost Cost(0);
    SmallVector<EVT> TypeWorklist;
    TypeWorklist.push_back(VT);
    while (!TypeWorklist.empty()) {
      EVT CurrVT = TypeWorklist.pop_back_val();
      unsigned CurrNumElements = CurrVT.getVectorNumElements();
      if (isPowerOf2_32(CurrNumElements)) {
        Cost += 1;
        continue;
      }

      unsigned PrevPow2 = NextPowerOf2(CurrNumElements) / 2;
      TypeWorklist.push_back(EVT::getVectorVT(C, EltVT, PrevPow2));
      TypeWorklist.push_back(
          EVT::getVectorVT(C, EltVT, CurrNumElements - PrevPow2));
    }
    return Cost;
  }

  return LT.first;
}

InstructionCost AArch64TTIImpl::getInterleavedMemoryOpCost(
    unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
    Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
    bool UseMaskForCond, bool UseMaskForGaps) {
  assert(Factor >= 2 && "Invalid interleave factor");
  auto *VecVTy = cast<VectorType>(VecTy);

  if (VecTy->isScalableTy() && (!ST->hasSVE() || Factor != 2))
    return InstructionCost::getInvalid();

  // Vectorization for masked interleaved accesses is only enabled for scalable
  // VF.
  if (!VecTy->isScalableTy() && (UseMaskForCond || UseMaskForGaps))
    return InstructionCost::getInvalid();

  if (!UseMaskForGaps && Factor <= TLI->getMaxSupportedInterleaveFactor()) {
    unsigned MinElts = VecVTy->getElementCount().getKnownMinValue();
    auto *SubVecTy =
        VectorType::get(VecVTy->getElementType(),
                        VecVTy->getElementCount().divideCoefficientBy(Factor));

    // ldN/stN only support legal vector types of size 64 or 128 in bits.
    // Accesses having vector types that are a multiple of 128 bits can be
    // matched to more than one ldN/stN instruction.
    bool UseScalable;
    if (MinElts % Factor == 0 &&
        TLI->isLegalInterleavedAccessType(SubVecTy, DL, UseScalable))
      return Factor * TLI->getNumInterleavedAccesses(SubVecTy, DL, UseScalable);
  }

  return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                           Alignment, AddressSpace, CostKind,
                                           UseMaskForCond, UseMaskForGaps);
}

InstructionCost
AArch64TTIImpl::getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys) {
  InstructionCost Cost = 0;
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  for (auto *I : Tys) {
    if (!I->isVectorTy())
      continue;
    if (I->getScalarSizeInBits() * cast<FixedVectorType>(I)->getNumElements() ==
        128)
      Cost += getMemoryOpCost(Instruction::Store, I, Align(128), 0, CostKind) +
              getMemoryOpCost(Instruction::Load, I, Align(128), 0, CostKind);
  }
  return Cost;
}

unsigned AArch64TTIImpl::getMaxInterleaveFactor(ElementCount VF) {
  return ST->getMaxInterleaveFactor();
}

// For Falkor, we want to avoid having too many strided loads in a loop since
// that can exhaust the HW prefetcher resources.  We adjust the unroller
// MaxCount preference below to attempt to ensure unrolling doesn't create too
// many strided loads.
static void
getFalkorUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                              TargetTransformInfo::UnrollingPreferences &UP) {
  enum { MaxStridedLoads = 7 };
  auto countStridedLoads = [](Loop *L, ScalarEvolution &SE) {
    int StridedLoads = 0;
    // FIXME? We could make this more precise by looking at the CFG and
    // e.g. not counting loads in each side of an if-then-else diamond.
    for (const auto BB : L->blocks()) {
      for (auto &I : *BB) {
        LoadInst *LMemI = dyn_cast<LoadInst>(&I);
        if (!LMemI)
          continue;

        Value *PtrValue = LMemI->getPointerOperand();
        if (L->isLoopInvariant(PtrValue))
          continue;

        const SCEV *LSCEV = SE.getSCEV(PtrValue);
        const SCEVAddRecExpr *LSCEVAddRec = dyn_cast<SCEVAddRecExpr>(LSCEV);
        if (!LSCEVAddRec || !LSCEVAddRec->isAffine())
          continue;

        // FIXME? We could take pairing of unrolled load copies into account
        // by looking at the AddRec, but we would probably have to limit this
        // to loops with no stores or other memory optimization barriers.
        ++StridedLoads;
        // We've seen enough strided loads that seeing more won't make a
        // difference.
        if (StridedLoads > MaxStridedLoads / 2)
          return StridedLoads;
      }
    }
    return StridedLoads;
  };

  int StridedLoads = countStridedLoads(L, SE);
  LLVM_DEBUG(dbgs() << "falkor-hwpf: detected " << StridedLoads
                    << " strided loads\n");
  // Pick the largest power of 2 unroll count that won't result in too many
  // strided loads.
  if (StridedLoads) {
    UP.MaxCount = 1 << Log2_32(MaxStridedLoads / StridedLoads);
    LLVM_DEBUG(dbgs() << "falkor-hwpf: setting unroll MaxCount to "
                      << UP.MaxCount << '\n');
  }
}

void AArch64TTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                             TTI::UnrollingPreferences &UP,
                                             OptimizationRemarkEmitter *ORE) {
  // Enable partial unrolling and runtime unrolling.
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);

  UP.UpperBound = true;

  // For inner loop, it is more likely to be a hot one, and the runtime check
  // can be promoted out from LICM pass, so the overhead is less, let's try
  // a larger threshold to unroll more loops.
  if (L->getLoopDepth() > 1)
    UP.PartialThreshold *= 2;

  // Disable partial & runtime unrolling on -Os.
  UP.PartialOptSizeThreshold = 0;

  if (ST->getProcFamily() == AArch64Subtarget::Falkor &&
      EnableFalkorHWPFUnrollFix)
    getFalkorUnrollingPreferences(L, SE, UP);

  // Scan the loop: don't unroll loops with calls as this could prevent
  // inlining. Don't unroll vector loops either, as they don't benefit much from
  // unrolling.
  for (auto *BB : L->getBlocks()) {
    for (auto &I : *BB) {
      // Don't unroll vectorised loop.
      if (I.getType()->isVectorTy())
        return;

      if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
        if (const Function *F = cast<CallBase>(I).getCalledFunction()) {
          if (!isLoweredToCall(F))
            continue;
        }
        return;
      }
    }
  }

  // Enable runtime unrolling for in-order models
  // If mcpu is omitted, getProcFamily() returns AArch64Subtarget::Others, so by
  // checking for that case, we can ensure that the default behaviour is
  // unchanged
  if (ST->getProcFamily() != AArch64Subtarget::Others &&
      !ST->getSchedModel().isOutOfOrder()) {
    UP.Runtime = true;
    UP.Partial = true;
    UP.UnrollRemainder = true;
    UP.DefaultUnrollRuntimeCount = 4;

    UP.UnrollAndJam = true;
    UP.UnrollAndJamInnerLoopThreshold = 60;
  }
}

void AArch64TTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                           TTI::PeelingPreferences &PP) {
  BaseT::getPeelingPreferences(L, SE, PP);
}

Value *AArch64TTIImpl::getOrCreateResultFromMemIntrinsic(IntrinsicInst *Inst,
                                                         Type *ExpectedType) {
  switch (Inst->getIntrinsicID()) {
  default:
    return nullptr;
  case Intrinsic::aarch64_neon_st2:
  case Intrinsic::aarch64_neon_st3:
  case Intrinsic::aarch64_neon_st4: {
    // Create a struct type
    StructType *ST = dyn_cast<StructType>(ExpectedType);
    if (!ST)
      return nullptr;
    unsigned NumElts = Inst->arg_size() - 1;
    if (ST->getNumElements() != NumElts)
      return nullptr;
    for (unsigned i = 0, e = NumElts; i != e; ++i) {
      if (Inst->getArgOperand(i)->getType() != ST->getElementType(i))
        return nullptr;
    }
    Value *Res = PoisonValue::get(ExpectedType);
    IRBuilder<> Builder(Inst);
    for (unsigned i = 0, e = NumElts; i != e; ++i) {
      Value *L = Inst->getArgOperand(i);
      Res = Builder.CreateInsertValue(Res, L, i);
    }
    return Res;
  }
  case Intrinsic::aarch64_neon_ld2:
  case Intrinsic::aarch64_neon_ld3:
  case Intrinsic::aarch64_neon_ld4:
    if (Inst->getType() == ExpectedType)
      return Inst;
    return nullptr;
  }
}

bool AArch64TTIImpl::getTgtMemIntrinsic(IntrinsicInst *Inst,
                                        MemIntrinsicInfo &Info) {
  switch (Inst->getIntrinsicID()) {
  default:
    break;
  case Intrinsic::aarch64_neon_ld2:
  case Intrinsic::aarch64_neon_ld3:
  case Intrinsic::aarch64_neon_ld4:
    Info.ReadMem = true;
    Info.WriteMem = false;
    Info.PtrVal = Inst->getArgOperand(0);
    break;
  case Intrinsic::aarch64_neon_st2:
  case Intrinsic::aarch64_neon_st3:
  case Intrinsic::aarch64_neon_st4:
    Info.ReadMem = false;
    Info.WriteMem = true;
    Info.PtrVal = Inst->getArgOperand(Inst->arg_size() - 1);
    break;
  }

  switch (Inst->getIntrinsicID()) {
  default:
    return false;
  case Intrinsic::aarch64_neon_ld2:
  case Intrinsic::aarch64_neon_st2:
    Info.MatchingId = VECTOR_LDST_TWO_ELEMENTS;
    break;
  case Intrinsic::aarch64_neon_ld3:
  case Intrinsic::aarch64_neon_st3:
    Info.MatchingId = VECTOR_LDST_THREE_ELEMENTS;
    break;
  case Intrinsic::aarch64_neon_ld4:
  case Intrinsic::aarch64_neon_st4:
    Info.MatchingId = VECTOR_LDST_FOUR_ELEMENTS;
    break;
  }
  return true;
}

/// See if \p I should be considered for address type promotion. We check if \p
/// I is a sext with right type and used in memory accesses. If it used in a
/// "complex" getelementptr, we allow it to be promoted without finding other
/// sext instructions that sign extended the same initial value. A getelementptr
/// is considered as "complex" if it has more than 2 operands.
bool AArch64TTIImpl::shouldConsiderAddressTypePromotion(
    const Instruction &I, bool &AllowPromotionWithoutCommonHeader) {
  bool Considerable = false;
  AllowPromotionWithoutCommonHeader = false;
  if (!isa<SExtInst>(&I))
    return false;
  Type *ConsideredSExtType =
      Type::getInt64Ty(I.getParent()->getParent()->getContext());
  if (I.getType() != ConsideredSExtType)
    return false;
  // See if the sext is the one with the right type and used in at least one
  // GetElementPtrInst.
  for (const User *U : I.users()) {
    if (const GetElementPtrInst *GEPInst = dyn_cast<GetElementPtrInst>(U)) {
      Considerable = true;
      // A getelementptr is considered as "complex" if it has more than 2
      // operands. We will promote a SExt used in such complex GEP as we
      // expect some computation to be merged if they are done on 64 bits.
      if (GEPInst->getNumOperands() > 2) {
        AllowPromotionWithoutCommonHeader = true;
        break;
      }
    }
  }
  return Considerable;
}

bool AArch64TTIImpl::isLegalToVectorizeReduction(
    const RecurrenceDescriptor &RdxDesc, ElementCount VF) const {
  if (!VF.isScalable())
    return true;

  Type *Ty = RdxDesc.getRecurrenceType();
  if (Ty->isBFloatTy() || !isElementTypeLegalForScalableVector(Ty))
    return false;

  switch (RdxDesc.getRecurrenceKind()) {
  case RecurKind::Add:
  case RecurKind::FAdd:
  case RecurKind::And:
  case RecurKind::Or:
  case RecurKind::Xor:
  case RecurKind::SMin:
  case RecurKind::SMax:
  case RecurKind::UMin:
  case RecurKind::UMax:
  case RecurKind::FMin:
  case RecurKind::FMax:
  case RecurKind::FMulAdd:
  case RecurKind::IAnyOf:
  case RecurKind::FAnyOf:
    return true;
  default:
    return false;
  }
}

InstructionCost
AArch64TTIImpl::getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                       FastMathFlags FMF,
                                       TTI::TargetCostKind CostKind) {
  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  if (auto *VTy = dyn_cast<ScalableVectorType>(Ty))
    if (VTy->getElementCount() == ElementCount::getScalable(1))
      return InstructionCost::getInvalid();

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);

  if (LT.second.getScalarType() == MVT::f16 && !ST->hasFullFP16())
    return BaseT::getMinMaxReductionCost(IID, Ty, FMF, CostKind);

  InstructionCost LegalizationCost = 0;
  if (LT.first > 1) {
    Type *LegalVTy = EVT(LT.second).getTypeForEVT(Ty->getContext());
    IntrinsicCostAttributes Attrs(IID, LegalVTy, {LegalVTy, LegalVTy}, FMF);
    LegalizationCost = getIntrinsicInstrCost(Attrs, CostKind) * (LT.first - 1);
  }

  return LegalizationCost + /*Cost of horizontal reduction*/ 2;
}

InstructionCost AArch64TTIImpl::getArithmeticReductionCostSVE(
    unsigned Opcode, VectorType *ValTy, TTI::TargetCostKind CostKind) {
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);
  InstructionCost LegalizationCost = 0;
  if (LT.first > 1) {
    Type *LegalVTy = EVT(LT.second).getTypeForEVT(ValTy->getContext());
    LegalizationCost = getArithmeticInstrCost(Opcode, LegalVTy, CostKind);
    LegalizationCost *= LT.first - 1;
  }

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");
  // Add the final reduction cost for the legal horizontal reduction
  switch (ISD) {
  case ISD::ADD:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::FADD:
    return LegalizationCost + 2;
  default:
    return InstructionCost::getInvalid();
  }
}

InstructionCost
AArch64TTIImpl::getArithmeticReductionCost(unsigned Opcode, VectorType *ValTy,
                                           std::optional<FastMathFlags> FMF,
                                           TTI::TargetCostKind CostKind) {
  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  if (auto *VTy = dyn_cast<ScalableVectorType>(ValTy))
    if (VTy->getElementCount() == ElementCount::getScalable(1))
      return InstructionCost::getInvalid();

  if (TTI::requiresOrderedReduction(FMF)) {
    if (auto *FixedVTy = dyn_cast<FixedVectorType>(ValTy)) {
      InstructionCost BaseCost =
          BaseT::getArithmeticReductionCost(Opcode, ValTy, FMF, CostKind);
      // Add on extra cost to reflect the extra overhead on some CPUs. We still
      // end up vectorizing for more computationally intensive loops.
      return BaseCost + FixedVTy->getNumElements();
    }

    if (Opcode != Instruction::FAdd)
      return InstructionCost::getInvalid();

    auto *VTy = cast<ScalableVectorType>(ValTy);
    InstructionCost Cost =
        getArithmeticInstrCost(Opcode, VTy->getScalarType(), CostKind);
    Cost *= getMaxNumElements(VTy->getElementCount());
    return Cost;
  }

  if (isa<ScalableVectorType>(ValTy))
    return getArithmeticReductionCostSVE(Opcode, ValTy, CostKind);

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);
  MVT MTy = LT.second;
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // Horizontal adds can use the 'addv' instruction. We model the cost of these
  // instructions as twice a normal vector add, plus 1 for each legalization
  // step (LT.first). This is the only arithmetic vector reduction operation for
  // which we have an instruction.
  // OR, XOR and AND costs should match the codegen from:
  // OR: llvm/test/CodeGen/AArch64/reduce-or.ll
  // XOR: llvm/test/CodeGen/AArch64/reduce-xor.ll
  // AND: llvm/test/CodeGen/AArch64/reduce-and.ll
  static const CostTblEntry CostTblNoPairwise[]{
      {ISD::ADD, MVT::v8i8,   2},
      {ISD::ADD, MVT::v16i8,  2},
      {ISD::ADD, MVT::v4i16,  2},
      {ISD::ADD, MVT::v8i16,  2},
      {ISD::ADD, MVT::v4i32,  2},
      {ISD::ADD, MVT::v2i64,  2},
      {ISD::OR,  MVT::v8i8,  15},
      {ISD::OR,  MVT::v16i8, 17},
      {ISD::OR,  MVT::v4i16,  7},
      {ISD::OR,  MVT::v8i16,  9},
      {ISD::OR,  MVT::v2i32,  3},
      {ISD::OR,  MVT::v4i32,  5},
      {ISD::OR,  MVT::v2i64,  3},
      {ISD::XOR, MVT::v8i8,  15},
      {ISD::XOR, MVT::v16i8, 17},
      {ISD::XOR, MVT::v4i16,  7},
      {ISD::XOR, MVT::v8i16,  9},
      {ISD::XOR, MVT::v2i32,  3},
      {ISD::XOR, MVT::v4i32,  5},
      {ISD::XOR, MVT::v2i64,  3},
      {ISD::AND, MVT::v8i8,  15},
      {ISD::AND, MVT::v16i8, 17},
      {ISD::AND, MVT::v4i16,  7},
      {ISD::AND, MVT::v8i16,  9},
      {ISD::AND, MVT::v2i32,  3},
      {ISD::AND, MVT::v4i32,  5},
      {ISD::AND, MVT::v2i64,  3},
  };
  switch (ISD) {
  default:
    break;
  case ISD::ADD:
    if (const auto *Entry = CostTableLookup(CostTblNoPairwise, ISD, MTy))
      return (LT.first - 1) + Entry->Cost;
    break;
  case ISD::XOR:
  case ISD::AND:
  case ISD::OR:
    const auto *Entry = CostTableLookup(CostTblNoPairwise, ISD, MTy);
    if (!Entry)
      break;
    auto *ValVTy = cast<FixedVectorType>(ValTy);
    if (MTy.getVectorNumElements() <= ValVTy->getNumElements() &&
        isPowerOf2_32(ValVTy->getNumElements())) {
      InstructionCost ExtraCost = 0;
      if (LT.first != 1) {
        // Type needs to be split, so there is an extra cost of LT.first - 1
        // arithmetic ops.
        auto *Ty = FixedVectorType::get(ValTy->getElementType(),
                                        MTy.getVectorNumElements());
        ExtraCost = getArithmeticInstrCost(Opcode, Ty, CostKind);
        ExtraCost *= LT.first - 1;
      }
      // All and/or/xor of i1 will be lowered with maxv/minv/addv + fmov
      auto Cost = ValVTy->getElementType()->isIntegerTy(1) ? 2 : Entry->Cost;
      return Cost + ExtraCost;
    }
    break;
  }
  return BaseT::getArithmeticReductionCost(Opcode, ValTy, FMF, CostKind);
}

InstructionCost AArch64TTIImpl::getSpliceCost(VectorType *Tp, int Index) {
  static const CostTblEntry ShuffleTbl[] = {
      { TTI::SK_Splice, MVT::nxv16i8,  1 },
      { TTI::SK_Splice, MVT::nxv8i16,  1 },
      { TTI::SK_Splice, MVT::nxv4i32,  1 },
      { TTI::SK_Splice, MVT::nxv2i64,  1 },
      { TTI::SK_Splice, MVT::nxv2f16,  1 },
      { TTI::SK_Splice, MVT::nxv4f16,  1 },
      { TTI::SK_Splice, MVT::nxv8f16,  1 },
      { TTI::SK_Splice, MVT::nxv2bf16, 1 },
      { TTI::SK_Splice, MVT::nxv4bf16, 1 },
      { TTI::SK_Splice, MVT::nxv8bf16, 1 },
      { TTI::SK_Splice, MVT::nxv2f32,  1 },
      { TTI::SK_Splice, MVT::nxv4f32,  1 },
      { TTI::SK_Splice, MVT::nxv2f64,  1 },
  };

  // The code-generator is currently not able to handle scalable vectors
  // of <vscale x 1 x eltty> yet, so return an invalid cost to avoid selecting
  // it. This change will be removed when code-generation for these types is
  // sufficiently reliable.
  if (Tp->getElementCount() == ElementCount::getScalable(1))
    return InstructionCost::getInvalid();

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);
  Type *LegalVTy = EVT(LT.second).getTypeForEVT(Tp->getContext());
  TTI::TargetCostKind CostKind = TTI::TCK_RecipThroughput;
  EVT PromotedVT = LT.second.getScalarType() == MVT::i1
                       ? TLI->getPromotedVTForPredicate(EVT(LT.second))
                       : LT.second;
  Type *PromotedVTy = EVT(PromotedVT).getTypeForEVT(Tp->getContext());
  InstructionCost LegalizationCost = 0;
  if (Index < 0) {
    LegalizationCost =
        getCmpSelInstrCost(Instruction::ICmp, PromotedVTy, PromotedVTy,
                           CmpInst::BAD_ICMP_PREDICATE, CostKind) +
        getCmpSelInstrCost(Instruction::Select, PromotedVTy, LegalVTy,
                           CmpInst::BAD_ICMP_PREDICATE, CostKind);
  }

  // Predicated splice are promoted when lowering. See AArch64ISelLowering.cpp
  // Cost performed on a promoted type.
  if (LT.second.getScalarType() == MVT::i1) {
    LegalizationCost +=
        getCastInstrCost(Instruction::ZExt, PromotedVTy, LegalVTy,
                         TTI::CastContextHint::None, CostKind) +
        getCastInstrCost(Instruction::Trunc, LegalVTy, PromotedVTy,
                         TTI::CastContextHint::None, CostKind);
  }
  const auto *Entry =
      CostTableLookup(ShuffleTbl, TTI::SK_Splice, PromotedVT.getSimpleVT());
  assert(Entry && "Illegal Type for Splice");
  LegalizationCost += Entry->Cost;
  return LegalizationCost * LT.first;
}

InstructionCost AArch64TTIImpl::getShuffleCost(
    TTI::ShuffleKind Kind, VectorType *Tp, ArrayRef<int> Mask,
    TTI::TargetCostKind CostKind, int Index, VectorType *SubTp,
    ArrayRef<const Value *> Args, const Instruction *CxtI) {
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);

  // If we have a Mask, and the LT is being legalized somehow, split the Mask
  // into smaller vectors and sum the cost of each shuffle.
  if (!Mask.empty() && isa<FixedVectorType>(Tp) && LT.second.isVector() &&
      Tp->getScalarSizeInBits() == LT.second.getScalarSizeInBits() &&
      Mask.size() > LT.second.getVectorNumElements() && !Index && !SubTp) {

    // Check for LD3/LD4 instructions, which are represented in llvm IR as
    // deinterleaving-shuffle(load). The shuffle cost could potentially be free,
    // but we model it with a cost of LT.first so that LD3/LD4 have a higher
    // cost than just the load.
    if (Args.size() >= 1 && isa<LoadInst>(Args[0]) &&
        (ShuffleVectorInst::isDeInterleaveMaskOfFactor(Mask, 3) ||
         ShuffleVectorInst::isDeInterleaveMaskOfFactor(Mask, 4)))
      return std::max<InstructionCost>(1, LT.first / 4);

    // Check for ST3/ST4 instructions, which are represented in llvm IR as
    // store(interleaving-shuffle). The shuffle cost could potentially be free,
    // but we model it with a cost of LT.first so that ST3/ST4 have a higher
    // cost than just the store.
    if (CxtI && CxtI->hasOneUse() && isa<StoreInst>(*CxtI->user_begin()) &&
        (ShuffleVectorInst::isInterleaveMask(
             Mask, 4, Tp->getElementCount().getKnownMinValue() * 2) ||
         ShuffleVectorInst::isInterleaveMask(
             Mask, 3, Tp->getElementCount().getKnownMinValue() * 2)))
      return LT.first;

    unsigned TpNumElts = Mask.size();
    unsigned LTNumElts = LT.second.getVectorNumElements();
    unsigned NumVecs = (TpNumElts + LTNumElts - 1) / LTNumElts;
    VectorType *NTp =
        VectorType::get(Tp->getScalarType(), LT.second.getVectorElementCount());
    InstructionCost Cost;
    for (unsigned N = 0; N < NumVecs; N++) {
      SmallVector<int> NMask;
      // Split the existing mask into chunks of size LTNumElts. Track the source
      // sub-vectors to ensure the result has at most 2 inputs.
      unsigned Source1, Source2;
      unsigned NumSources = 0;
      for (unsigned E = 0; E < LTNumElts; E++) {
        int MaskElt = (N * LTNumElts + E < TpNumElts) ? Mask[N * LTNumElts + E]
                                                      : PoisonMaskElem;
        if (MaskElt < 0) {
          NMask.push_back(PoisonMaskElem);
          continue;
        }

        // Calculate which source from the input this comes from and whether it
        // is new to us.
        unsigned Source = MaskElt / LTNumElts;
        if (NumSources == 0) {
          Source1 = Source;
          NumSources = 1;
        } else if (NumSources == 1 && Source != Source1) {
          Source2 = Source;
          NumSources = 2;
        } else if (NumSources >= 2 && Source != Source1 && Source != Source2) {
          NumSources++;
        }

        // Add to the new mask. For the NumSources>2 case these are not correct,
        // but are only used for the modular lane number.
        if (Source == Source1)
          NMask.push_back(MaskElt % LTNumElts);
        else if (Source == Source2)
          NMask.push_back(MaskElt % LTNumElts + LTNumElts);
        else
          NMask.push_back(MaskElt % LTNumElts);
      }
      // If the sub-mask has at most 2 input sub-vectors then re-cost it using
      // getShuffleCost. If not then cost it using the worst case.
      if (NumSources <= 2)
        Cost += getShuffleCost(NumSources <= 1 ? TTI::SK_PermuteSingleSrc
                                               : TTI::SK_PermuteTwoSrc,
                               NTp, NMask, CostKind, 0, nullptr, Args, CxtI);
      else if (any_of(enumerate(NMask), [&](const auto &ME) {
                 return ME.value() % LTNumElts == ME.index();
               }))
        Cost += LTNumElts - 1;
      else
        Cost += LTNumElts;
    }
    return Cost;
  }

  Kind = improveShuffleKindFromMask(Kind, Mask, Tp, Index, SubTp);
  // Treat extractsubvector as single op permutation.
  bool IsExtractSubvector = Kind == TTI::SK_ExtractSubvector;
  if (IsExtractSubvector && LT.second.isFixedLengthVector())
    Kind = TTI::SK_PermuteSingleSrc;

  // Check for broadcast loads, which are supported by the LD1R instruction.
  // In terms of code-size, the shuffle vector is free when a load + dup get
  // folded into a LD1R. That's what we check and return here. For performance
  // and reciprocal throughput, a LD1R is not completely free. In this case, we
  // return the cost for the broadcast below (i.e. 1 for most/all types), so
  // that we model the load + dup sequence slightly higher because LD1R is a
  // high latency instruction.
  if (CostKind == TTI::TCK_CodeSize && Kind == TTI::SK_Broadcast) {
    bool IsLoad = !Args.empty() && isa<LoadInst>(Args[0]);
    if (IsLoad && LT.second.isVector() &&
        isLegalBroadcastLoad(Tp->getElementType(),
                             LT.second.getVectorElementCount()))
      return 0;
  }

  // If we have 4 elements for the shuffle and a Mask, get the cost straight
  // from the perfect shuffle tables.
  if (Mask.size() == 4 && Tp->getElementCount() == ElementCount::getFixed(4) &&
      (Tp->getScalarSizeInBits() == 16 || Tp->getScalarSizeInBits() == 32) &&
      all_of(Mask, [](int E) { return E < 8; }))
    return getPerfectShuffleCost(Mask);

  // Check for identity masks, which we can treat as free.
  if (!Mask.empty() && LT.second.isFixedLengthVector() &&
      (Kind == TTI::SK_PermuteTwoSrc || Kind == TTI::SK_PermuteSingleSrc) &&
      all_of(enumerate(Mask), [](const auto &M) {
        return M.value() < 0 || M.value() == (int)M.index();
      }))
    return 0;

  // Check for other shuffles that are not SK_ kinds but we have native
  // instructions for, for example ZIP and UZP.
  unsigned Unused;
  if (LT.second.isFixedLengthVector() &&
      LT.second.getVectorNumElements() == Mask.size() &&
      (Kind == TTI::SK_PermuteTwoSrc || Kind == TTI::SK_PermuteSingleSrc) &&
      (isZIPMask(Mask, LT.second.getVectorNumElements(), Unused) ||
       isUZPMask(Mask, LT.second.getVectorNumElements(), Unused) ||
       // Check for non-zero lane splats
       all_of(drop_begin(Mask),
              [&Mask](int M) { return M < 0 || M == Mask[0]; })))
    return 1;

  if (Kind == TTI::SK_Broadcast || Kind == TTI::SK_Transpose ||
      Kind == TTI::SK_Select || Kind == TTI::SK_PermuteSingleSrc ||
      Kind == TTI::SK_Reverse || Kind == TTI::SK_Splice) {
    static const CostTblEntry ShuffleTbl[] = {
        // Broadcast shuffle kinds can be performed with 'dup'.
        {TTI::SK_Broadcast, MVT::v8i8, 1},
        {TTI::SK_Broadcast, MVT::v16i8, 1},
        {TTI::SK_Broadcast, MVT::v4i16, 1},
        {TTI::SK_Broadcast, MVT::v8i16, 1},
        {TTI::SK_Broadcast, MVT::v2i32, 1},
        {TTI::SK_Broadcast, MVT::v4i32, 1},
        {TTI::SK_Broadcast, MVT::v2i64, 1},
        {TTI::SK_Broadcast, MVT::v4f16, 1},
        {TTI::SK_Broadcast, MVT::v8f16, 1},
        {TTI::SK_Broadcast, MVT::v2f32, 1},
        {TTI::SK_Broadcast, MVT::v4f32, 1},
        {TTI::SK_Broadcast, MVT::v2f64, 1},
        // Transpose shuffle kinds can be performed with 'trn1/trn2' and
        // 'zip1/zip2' instructions.
        {TTI::SK_Transpose, MVT::v8i8, 1},
        {TTI::SK_Transpose, MVT::v16i8, 1},
        {TTI::SK_Transpose, MVT::v4i16, 1},
        {TTI::SK_Transpose, MVT::v8i16, 1},
        {TTI::SK_Transpose, MVT::v2i32, 1},
        {TTI::SK_Transpose, MVT::v4i32, 1},
        {TTI::SK_Transpose, MVT::v2i64, 1},
        {TTI::SK_Transpose, MVT::v4f16, 1},
        {TTI::SK_Transpose, MVT::v8f16, 1},
        {TTI::SK_Transpose, MVT::v2f32, 1},
        {TTI::SK_Transpose, MVT::v4f32, 1},
        {TTI::SK_Transpose, MVT::v2f64, 1},
        // Select shuffle kinds.
        // TODO: handle vXi8/vXi16.
        {TTI::SK_Select, MVT::v2i32, 1}, // mov.
        {TTI::SK_Select, MVT::v4i32, 2}, // rev+trn (or similar).
        {TTI::SK_Select, MVT::v2i64, 1}, // mov.
        {TTI::SK_Select, MVT::v2f32, 1}, // mov.
        {TTI::SK_Select, MVT::v4f32, 2}, // rev+trn (or similar).
        {TTI::SK_Select, MVT::v2f64, 1}, // mov.
        // PermuteSingleSrc shuffle kinds.
        {TTI::SK_PermuteSingleSrc, MVT::v2i32, 1}, // mov.
        {TTI::SK_PermuteSingleSrc, MVT::v4i32, 3}, // perfectshuffle worst case.
        {TTI::SK_PermuteSingleSrc, MVT::v2i64, 1}, // mov.
        {TTI::SK_PermuteSingleSrc, MVT::v2f32, 1}, // mov.
        {TTI::SK_PermuteSingleSrc, MVT::v4f32, 3}, // perfectshuffle worst case.
        {TTI::SK_PermuteSingleSrc, MVT::v2f64, 1}, // mov.
        {TTI::SK_PermuteSingleSrc, MVT::v4i16, 3}, // perfectshuffle worst case.
        {TTI::SK_PermuteSingleSrc, MVT::v4f16, 3}, // perfectshuffle worst case.
        {TTI::SK_PermuteSingleSrc, MVT::v4bf16, 3}, // same
        {TTI::SK_PermuteSingleSrc, MVT::v8i16, 8},  // constpool + load + tbl
        {TTI::SK_PermuteSingleSrc, MVT::v8f16, 8},  // constpool + load + tbl
        {TTI::SK_PermuteSingleSrc, MVT::v8bf16, 8}, // constpool + load + tbl
        {TTI::SK_PermuteSingleSrc, MVT::v8i8, 8},   // constpool + load + tbl
        {TTI::SK_PermuteSingleSrc, MVT::v16i8, 8},  // constpool + load + tbl
        // Reverse can be lowered with `rev`.
        {TTI::SK_Reverse, MVT::v2i32, 1}, // REV64
        {TTI::SK_Reverse, MVT::v4i32, 2}, // REV64; EXT
        {TTI::SK_Reverse, MVT::v2i64, 1}, // EXT
        {TTI::SK_Reverse, MVT::v2f32, 1}, // REV64
        {TTI::SK_Reverse, MVT::v4f32, 2}, // REV64; EXT
        {TTI::SK_Reverse, MVT::v2f64, 1}, // EXT
        {TTI::SK_Reverse, MVT::v8f16, 2}, // REV64; EXT
        {TTI::SK_Reverse, MVT::v8i16, 2}, // REV64; EXT
        {TTI::SK_Reverse, MVT::v16i8, 2}, // REV64; EXT
        {TTI::SK_Reverse, MVT::v4f16, 1}, // REV64
        {TTI::SK_Reverse, MVT::v4i16, 1}, // REV64
        {TTI::SK_Reverse, MVT::v8i8, 1},  // REV64
        // Splice can all be lowered as `ext`.
        {TTI::SK_Splice, MVT::v2i32, 1},
        {TTI::SK_Splice, MVT::v4i32, 1},
        {TTI::SK_Splice, MVT::v2i64, 1},
        {TTI::SK_Splice, MVT::v2f32, 1},
        {TTI::SK_Splice, MVT::v4f32, 1},
        {TTI::SK_Splice, MVT::v2f64, 1},
        {TTI::SK_Splice, MVT::v8f16, 1},
        {TTI::SK_Splice, MVT::v8bf16, 1},
        {TTI::SK_Splice, MVT::v8i16, 1},
        {TTI::SK_Splice, MVT::v16i8, 1},
        {TTI::SK_Splice, MVT::v4bf16, 1},
        {TTI::SK_Splice, MVT::v4f16, 1},
        {TTI::SK_Splice, MVT::v4i16, 1},
        {TTI::SK_Splice, MVT::v8i8, 1},
        // Broadcast shuffle kinds for scalable vectors
        {TTI::SK_Broadcast, MVT::nxv16i8, 1},
        {TTI::SK_Broadcast, MVT::nxv8i16, 1},
        {TTI::SK_Broadcast, MVT::nxv4i32, 1},
        {TTI::SK_Broadcast, MVT::nxv2i64, 1},
        {TTI::SK_Broadcast, MVT::nxv2f16, 1},
        {TTI::SK_Broadcast, MVT::nxv4f16, 1},
        {TTI::SK_Broadcast, MVT::nxv8f16, 1},
        {TTI::SK_Broadcast, MVT::nxv2bf16, 1},
        {TTI::SK_Broadcast, MVT::nxv4bf16, 1},
        {TTI::SK_Broadcast, MVT::nxv8bf16, 1},
        {TTI::SK_Broadcast, MVT::nxv2f32, 1},
        {TTI::SK_Broadcast, MVT::nxv4f32, 1},
        {TTI::SK_Broadcast, MVT::nxv2f64, 1},
        {TTI::SK_Broadcast, MVT::nxv16i1, 1},
        {TTI::SK_Broadcast, MVT::nxv8i1, 1},
        {TTI::SK_Broadcast, MVT::nxv4i1, 1},
        {TTI::SK_Broadcast, MVT::nxv2i1, 1},
        // Handle the cases for vector.reverse with scalable vectors
        {TTI::SK_Reverse, MVT::nxv16i8, 1},
        {TTI::SK_Reverse, MVT::nxv8i16, 1},
        {TTI::SK_Reverse, MVT::nxv4i32, 1},
        {TTI::SK_Reverse, MVT::nxv2i64, 1},
        {TTI::SK_Reverse, MVT::nxv2f16, 1},
        {TTI::SK_Reverse, MVT::nxv4f16, 1},
        {TTI::SK_Reverse, MVT::nxv8f16, 1},
        {TTI::SK_Reverse, MVT::nxv2bf16, 1},
        {TTI::SK_Reverse, MVT::nxv4bf16, 1},
        {TTI::SK_Reverse, MVT::nxv8bf16, 1},
        {TTI::SK_Reverse, MVT::nxv2f32, 1},
        {TTI::SK_Reverse, MVT::nxv4f32, 1},
        {TTI::SK_Reverse, MVT::nxv2f64, 1},
        {TTI::SK_Reverse, MVT::nxv16i1, 1},
        {TTI::SK_Reverse, MVT::nxv8i1, 1},
        {TTI::SK_Reverse, MVT::nxv4i1, 1},
        {TTI::SK_Reverse, MVT::nxv2i1, 1},
    };
    if (const auto *Entry = CostTableLookup(ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;
  }

  if (Kind == TTI::SK_Splice && isa<ScalableVectorType>(Tp))
    return getSpliceCost(Tp, Index);

  // Inserting a subvector can often be done with either a D, S or H register
  // move, so long as the inserted vector is "aligned".
  if (Kind == TTI::SK_InsertSubvector && LT.second.isFixedLengthVector() &&
      LT.second.getSizeInBits() <= 128 && SubTp) {
    std::pair<InstructionCost, MVT> SubLT = getTypeLegalizationCost(SubTp);
    if (SubLT.second.isVector()) {
      int NumElts = LT.second.getVectorNumElements();
      int NumSubElts = SubLT.second.getVectorNumElements();
      if ((Index % NumSubElts) == 0 && (NumElts % NumSubElts) == 0)
        return SubLT.first;
    }
  }

  // Restore optimal kind.
  if (IsExtractSubvector)
    Kind = TTI::SK_ExtractSubvector;
  return BaseT::getShuffleCost(Kind, Tp, Mask, CostKind, Index, SubTp, Args,
                               CxtI);
}

static bool containsDecreasingPointers(Loop *TheLoop,
                                       PredicatedScalarEvolution *PSE) {
  const auto &Strides = DenseMap<Value *, const SCEV *>();
  for (BasicBlock *BB : TheLoop->blocks()) {
    // Scan the instructions in the block and look for addresses that are
    // consecutive and decreasing.
    for (Instruction &I : *BB) {
      if (isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
        Value *Ptr = getLoadStorePointerOperand(&I);
        Type *AccessTy = getLoadStoreType(&I);
        if (getPtrStride(*PSE, AccessTy, Ptr, TheLoop, Strides, /*Assume=*/true,
                         /*ShouldCheckWrap=*/false)
                .value_or(0) < 0)
          return true;
      }
    }
  }
  return false;
}

bool AArch64TTIImpl::preferPredicateOverEpilogue(TailFoldingInfo *TFI) {
  if (!ST->hasSVE())
    return false;

  // We don't currently support vectorisation with interleaving for SVE - with
  // such loops we're better off not using tail-folding. This gives us a chance
  // to fall back on fixed-width vectorisation using NEON's ld2/st2/etc.
  if (TFI->IAI->hasGroups())
    return false;

  TailFoldingOpts Required = TailFoldingOpts::Disabled;
  if (TFI->LVL->getReductionVars().size())
    Required |= TailFoldingOpts::Reductions;
  if (TFI->LVL->getFixedOrderRecurrences().size())
    Required |= TailFoldingOpts::Recurrences;

  // We call this to discover whether any load/store pointers in the loop have
  // negative strides. This will require extra work to reverse the loop
  // predicate, which may be expensive.
  if (containsDecreasingPointers(TFI->LVL->getLoop(),
                                 TFI->LVL->getPredicatedScalarEvolution()))
    Required |= TailFoldingOpts::Reverse;
  if (Required == TailFoldingOpts::Disabled)
    Required |= TailFoldingOpts::Simple;

  if (!TailFoldingOptionLoc.satisfies(ST->getSVETailFoldingDefaultOpts(),
                                      Required))
    return false;

  // Don't tail-fold for tight loops where we would be better off interleaving
  // with an unpredicated loop.
  unsigned NumInsns = 0;
  for (BasicBlock *BB : TFI->LVL->getLoop()->blocks()) {
    NumInsns += BB->sizeWithoutDebug();
  }

  // We expect 4 of these to be a IV PHI, IV add, IV compare and branch.
  return NumInsns >= SVETailFoldInsnThreshold;
}

InstructionCost
AArch64TTIImpl::getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                     StackOffset BaseOffset, bool HasBaseReg,
                                     int64_t Scale, unsigned AddrSpace) const {
  // Scaling factors are not free at all.
  // Operands                     | Rt Latency
  // -------------------------------------------
  // Rt, [Xn, Xm]                 | 4
  // -------------------------------------------
  // Rt, [Xn, Xm, lsl #imm]       | Rn: 4 Rm: 5
  // Rt, [Xn, Wm, <extend> #imm]  |
  TargetLoweringBase::AddrMode AM;
  AM.BaseGV = BaseGV;
  AM.BaseOffs = BaseOffset.getFixed();
  AM.HasBaseReg = HasBaseReg;
  AM.Scale = Scale;
  AM.ScalableOffset = BaseOffset.getScalable();
  if (getTLI()->isLegalAddressingMode(DL, AM, Ty, AddrSpace))
    // Scale represents reg2 * scale, thus account for 1 if
    // it is not equal to 0 or 1.
    return AM.Scale != 0 && AM.Scale != 1;
  return -1;
}

bool AArch64TTIImpl::shouldTreatInstructionLikeSelect(const Instruction *I) {
  // For the binary operators (e.g. or) we need to be more careful than
  // selects, here we only transform them if they are already at a natural
  // break point in the code - the end of a block with an unconditional
  // terminator.
  if (EnableOrLikeSelectOpt && I->getOpcode() == Instruction::Or &&
      isa<BranchInst>(I->getNextNode()) &&
      cast<BranchInst>(I->getNextNode())->isUnconditional())
    return true;
  return BaseT::shouldTreatInstructionLikeSelect(I);
}

bool AArch64TTIImpl::isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                                   const TargetTransformInfo::LSRCost &C2) {
  // AArch64 specific here is adding the number of instructions to the
  // comparison (though not as the first consideration, as some targets do)
  // along with changing the priority of the base additions.
  // TODO: Maybe a more nuanced tradeoff between instruction count
  // and number of registers? To be investigated at a later date.
  if (EnableLSRCostOpt)
    return std::tie(C1.NumRegs, C1.Insns, C1.NumBaseAdds, C1.AddRecCost,
                    C1.NumIVMuls, C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
           std::tie(C2.NumRegs, C2.Insns, C2.NumBaseAdds, C2.AddRecCost,
                    C2.NumIVMuls, C2.ScaleCost, C2.ImmCost, C2.SetupCost);

  return TargetTransformInfoImplBase::isLSRCostLess(C1, C2);
}
