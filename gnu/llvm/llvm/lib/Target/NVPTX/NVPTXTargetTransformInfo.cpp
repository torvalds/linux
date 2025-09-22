//===-- NVPTXTargetTransformInfo.cpp - NVPTX specific TTI -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NVPTXTargetTransformInfo.h"
#include "NVPTXUtilities.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/Support/Debug.h"
#include <optional>
using namespace llvm;

#define DEBUG_TYPE "NVPTXtti"

// Whether the given intrinsic reads threadIdx.x/y/z.
static bool readsThreadIndex(const IntrinsicInst *II) {
  switch (II->getIntrinsicID()) {
    default: return false;
    case Intrinsic::nvvm_read_ptx_sreg_tid_x:
    case Intrinsic::nvvm_read_ptx_sreg_tid_y:
    case Intrinsic::nvvm_read_ptx_sreg_tid_z:
      return true;
  }
}

static bool readsLaneId(const IntrinsicInst *II) {
  return II->getIntrinsicID() == Intrinsic::nvvm_read_ptx_sreg_laneid;
}

// Whether the given intrinsic is an atomic instruction in PTX.
static bool isNVVMAtomic(const IntrinsicInst *II) {
  switch (II->getIntrinsicID()) {
    default: return false;
    case Intrinsic::nvvm_atomic_load_inc_32:
    case Intrinsic::nvvm_atomic_load_dec_32:

    case Intrinsic::nvvm_atomic_add_gen_f_cta:
    case Intrinsic::nvvm_atomic_add_gen_f_sys:
    case Intrinsic::nvvm_atomic_add_gen_i_cta:
    case Intrinsic::nvvm_atomic_add_gen_i_sys:
    case Intrinsic::nvvm_atomic_and_gen_i_cta:
    case Intrinsic::nvvm_atomic_and_gen_i_sys:
    case Intrinsic::nvvm_atomic_cas_gen_i_cta:
    case Intrinsic::nvvm_atomic_cas_gen_i_sys:
    case Intrinsic::nvvm_atomic_dec_gen_i_cta:
    case Intrinsic::nvvm_atomic_dec_gen_i_sys:
    case Intrinsic::nvvm_atomic_inc_gen_i_cta:
    case Intrinsic::nvvm_atomic_inc_gen_i_sys:
    case Intrinsic::nvvm_atomic_max_gen_i_cta:
    case Intrinsic::nvvm_atomic_max_gen_i_sys:
    case Intrinsic::nvvm_atomic_min_gen_i_cta:
    case Intrinsic::nvvm_atomic_min_gen_i_sys:
    case Intrinsic::nvvm_atomic_or_gen_i_cta:
    case Intrinsic::nvvm_atomic_or_gen_i_sys:
    case Intrinsic::nvvm_atomic_exch_gen_i_cta:
    case Intrinsic::nvvm_atomic_exch_gen_i_sys:
    case Intrinsic::nvvm_atomic_xor_gen_i_cta:
    case Intrinsic::nvvm_atomic_xor_gen_i_sys:
      return true;
  }
}

bool NVPTXTTIImpl::isSourceOfDivergence(const Value *V) {
  // Without inter-procedural analysis, we conservatively assume that arguments
  // to __device__ functions are divergent.
  if (const Argument *Arg = dyn_cast<Argument>(V))
    return !isKernelFunction(*Arg->getParent());

  if (const Instruction *I = dyn_cast<Instruction>(V)) {
    // Without pointer analysis, we conservatively assume values loaded from
    // generic or local address space are divergent.
    if (const LoadInst *LI = dyn_cast<LoadInst>(I)) {
      unsigned AS = LI->getPointerAddressSpace();
      return AS == ADDRESS_SPACE_GENERIC || AS == ADDRESS_SPACE_LOCAL;
    }
    // Atomic instructions may cause divergence. Atomic instructions are
    // executed sequentially across all threads in a warp. Therefore, an earlier
    // executed thread may see different memory inputs than a later executed
    // thread. For example, suppose *a = 0 initially.
    //
    //   atom.global.add.s32 d, [a], 1
    //
    // returns 0 for the first thread that enters the critical region, and 1 for
    // the second thread.
    if (I->isAtomic())
      return true;
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      // Instructions that read threadIdx are obviously divergent.
      if (readsThreadIndex(II) || readsLaneId(II))
        return true;
      // Handle the NVPTX atomic intrinsics that cannot be represented as an
      // atomic IR instruction.
      if (isNVVMAtomic(II))
        return true;
    }
    // Conservatively consider the return value of function calls as divergent.
    // We could analyze callees with bodies more precisely using
    // inter-procedural analysis.
    if (isa<CallInst>(I))
      return true;
  }

  return false;
}

// Convert NVVM intrinsics to target-generic LLVM code where possible.
static Instruction *simplifyNvvmIntrinsic(IntrinsicInst *II, InstCombiner &IC) {
  // Each NVVM intrinsic we can simplify can be replaced with one of:
  //
  //  * an LLVM intrinsic,
  //  * an LLVM cast operation,
  //  * an LLVM binary operation, or
  //  * ad-hoc LLVM IR for the particular operation.

  // Some transformations are only valid when the module's
  // flush-denormals-to-zero (ftz) setting is true/false, whereas other
  // transformations are valid regardless of the module's ftz setting.
  enum FtzRequirementTy {
    FTZ_Any,       // Any ftz setting is ok.
    FTZ_MustBeOn,  // Transformation is valid only if ftz is on.
    FTZ_MustBeOff, // Transformation is valid only if ftz is off.
  };
  // Classes of NVVM intrinsics that can't be replaced one-to-one with a
  // target-generic intrinsic, cast op, or binary op but that we can nonetheless
  // simplify.
  enum SpecialCase {
    SPC_Reciprocal,
  };

  // SimplifyAction is a poor-man's variant (plus an additional flag) that
  // represents how to replace an NVVM intrinsic with target-generic LLVM IR.
  struct SimplifyAction {
    // Invariant: At most one of these Optionals has a value.
    std::optional<Intrinsic::ID> IID;
    std::optional<Instruction::CastOps> CastOp;
    std::optional<Instruction::BinaryOps> BinaryOp;
    std::optional<SpecialCase> Special;

    FtzRequirementTy FtzRequirement = FTZ_Any;
    // Denormal handling is guarded by different attributes depending on the
    // type (denormal-fp-math vs denormal-fp-math-f32), take note of halfs.
    bool IsHalfTy = false;

    SimplifyAction() = default;

    SimplifyAction(Intrinsic::ID IID, FtzRequirementTy FtzReq,
                   bool IsHalfTy = false)
        : IID(IID), FtzRequirement(FtzReq), IsHalfTy(IsHalfTy) {}

    // Cast operations don't have anything to do with FTZ, so we skip that
    // argument.
    SimplifyAction(Instruction::CastOps CastOp) : CastOp(CastOp) {}

    SimplifyAction(Instruction::BinaryOps BinaryOp, FtzRequirementTy FtzReq)
        : BinaryOp(BinaryOp), FtzRequirement(FtzReq) {}

    SimplifyAction(SpecialCase Special, FtzRequirementTy FtzReq)
        : Special(Special), FtzRequirement(FtzReq) {}
  };

  // Try to generate a SimplifyAction describing how to replace our
  // IntrinsicInstr with target-generic LLVM IR.
  const SimplifyAction Action = [II]() -> SimplifyAction {
    switch (II->getIntrinsicID()) {
    // NVVM intrinsics that map directly to LLVM intrinsics.
    case Intrinsic::nvvm_ceil_d:
      return {Intrinsic::ceil, FTZ_Any};
    case Intrinsic::nvvm_ceil_f:
      return {Intrinsic::ceil, FTZ_MustBeOff};
    case Intrinsic::nvvm_ceil_ftz_f:
      return {Intrinsic::ceil, FTZ_MustBeOn};
    case Intrinsic::nvvm_fabs_d:
      return {Intrinsic::fabs, FTZ_Any};
    case Intrinsic::nvvm_floor_d:
      return {Intrinsic::floor, FTZ_Any};
    case Intrinsic::nvvm_floor_f:
      return {Intrinsic::floor, FTZ_MustBeOff};
    case Intrinsic::nvvm_floor_ftz_f:
      return {Intrinsic::floor, FTZ_MustBeOn};
    case Intrinsic::nvvm_fma_rn_d:
      return {Intrinsic::fma, FTZ_Any};
    case Intrinsic::nvvm_fma_rn_f:
      return {Intrinsic::fma, FTZ_MustBeOff};
    case Intrinsic::nvvm_fma_rn_ftz_f:
      return {Intrinsic::fma, FTZ_MustBeOn};
    case Intrinsic::nvvm_fma_rn_f16:
      return {Intrinsic::fma, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fma_rn_ftz_f16:
      return {Intrinsic::fma, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fma_rn_f16x2:
      return {Intrinsic::fma, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fma_rn_ftz_f16x2:
      return {Intrinsic::fma, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fma_rn_bf16:
      return {Intrinsic::fma, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fma_rn_ftz_bf16:
      return {Intrinsic::fma, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fma_rn_bf16x2:
      return {Intrinsic::fma, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fma_rn_ftz_bf16x2:
      return {Intrinsic::fma, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmax_d:
      return {Intrinsic::maxnum, FTZ_Any};
    case Intrinsic::nvvm_fmax_f:
      return {Intrinsic::maxnum, FTZ_MustBeOff};
    case Intrinsic::nvvm_fmax_ftz_f:
      return {Intrinsic::maxnum, FTZ_MustBeOn};
    case Intrinsic::nvvm_fmax_nan_f:
      return {Intrinsic::maximum, FTZ_MustBeOff};
    case Intrinsic::nvvm_fmax_ftz_nan_f:
      return {Intrinsic::maximum, FTZ_MustBeOn};
    case Intrinsic::nvvm_fmax_f16:
      return {Intrinsic::maxnum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmax_ftz_f16:
      return {Intrinsic::maxnum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmax_f16x2:
      return {Intrinsic::maxnum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmax_ftz_f16x2:
      return {Intrinsic::maxnum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmax_nan_f16:
      return {Intrinsic::maximum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmax_ftz_nan_f16:
      return {Intrinsic::maximum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmax_nan_f16x2:
      return {Intrinsic::maximum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmax_ftz_nan_f16x2:
      return {Intrinsic::maximum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmin_d:
      return {Intrinsic::minnum, FTZ_Any};
    case Intrinsic::nvvm_fmin_f:
      return {Intrinsic::minnum, FTZ_MustBeOff};
    case Intrinsic::nvvm_fmin_ftz_f:
      return {Intrinsic::minnum, FTZ_MustBeOn};
    case Intrinsic::nvvm_fmin_nan_f:
      return {Intrinsic::minimum, FTZ_MustBeOff};
    case Intrinsic::nvvm_fmin_ftz_nan_f:
      return {Intrinsic::minimum, FTZ_MustBeOn};
    case Intrinsic::nvvm_fmin_f16:
      return {Intrinsic::minnum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmin_ftz_f16:
      return {Intrinsic::minnum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmin_f16x2:
      return {Intrinsic::minnum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmin_ftz_f16x2:
      return {Intrinsic::minnum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmin_nan_f16:
      return {Intrinsic::minimum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmin_ftz_nan_f16:
      return {Intrinsic::minimum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_fmin_nan_f16x2:
      return {Intrinsic::minimum, FTZ_MustBeOff, true};
    case Intrinsic::nvvm_fmin_ftz_nan_f16x2:
      return {Intrinsic::minimum, FTZ_MustBeOn, true};
    case Intrinsic::nvvm_sqrt_rn_d:
      return {Intrinsic::sqrt, FTZ_Any};
    case Intrinsic::nvvm_sqrt_f:
      // nvvm_sqrt_f is a special case.  For  most intrinsics, foo_ftz_f is the
      // ftz version, and foo_f is the non-ftz version.  But nvvm_sqrt_f adopts
      // the ftz-ness of the surrounding code.  sqrt_rn_f and sqrt_rn_ftz_f are
      // the versions with explicit ftz-ness.
      return {Intrinsic::sqrt, FTZ_Any};
    case Intrinsic::nvvm_trunc_d:
      return {Intrinsic::trunc, FTZ_Any};
    case Intrinsic::nvvm_trunc_f:
      return {Intrinsic::trunc, FTZ_MustBeOff};
    case Intrinsic::nvvm_trunc_ftz_f:
      return {Intrinsic::trunc, FTZ_MustBeOn};

    // NVVM intrinsics that map to LLVM cast operations.
    //
    // Note that llvm's target-generic conversion operators correspond to the rz
    // (round to zero) versions of the nvvm conversion intrinsics, even though
    // most everything else here uses the rn (round to nearest even) nvvm ops.
    case Intrinsic::nvvm_d2i_rz:
    case Intrinsic::nvvm_f2i_rz:
    case Intrinsic::nvvm_d2ll_rz:
    case Intrinsic::nvvm_f2ll_rz:
      return {Instruction::FPToSI};
    case Intrinsic::nvvm_d2ui_rz:
    case Intrinsic::nvvm_f2ui_rz:
    case Intrinsic::nvvm_d2ull_rz:
    case Intrinsic::nvvm_f2ull_rz:
      return {Instruction::FPToUI};
    case Intrinsic::nvvm_i2d_rz:
    case Intrinsic::nvvm_i2f_rz:
    case Intrinsic::nvvm_ll2d_rz:
    case Intrinsic::nvvm_ll2f_rz:
      return {Instruction::SIToFP};
    case Intrinsic::nvvm_ui2d_rz:
    case Intrinsic::nvvm_ui2f_rz:
    case Intrinsic::nvvm_ull2d_rz:
    case Intrinsic::nvvm_ull2f_rz:
      return {Instruction::UIToFP};

    // NVVM intrinsics that map to LLVM binary ops.
    case Intrinsic::nvvm_div_rn_d:
      return {Instruction::FDiv, FTZ_Any};

    // The remainder of cases are NVVM intrinsics that map to LLVM idioms, but
    // need special handling.
    //
    // We seem to be missing intrinsics for rcp.approx.{ftz.}f32, which is just
    // as well.
    case Intrinsic::nvvm_rcp_rn_d:
      return {SPC_Reciprocal, FTZ_Any};

      // We do not currently simplify intrinsics that give an approximate
      // answer. These include:
      //
      //   - nvvm_cos_approx_{f,ftz_f}
      //   - nvvm_ex2_approx_{d,f,ftz_f}
      //   - nvvm_lg2_approx_{d,f,ftz_f}
      //   - nvvm_sin_approx_{f,ftz_f}
      //   - nvvm_sqrt_approx_{f,ftz_f}
      //   - nvvm_rsqrt_approx_{d,f,ftz_f}
      //   - nvvm_div_approx_{ftz_d,ftz_f,f}
      //   - nvvm_rcp_approx_ftz_d
      //
      // Ideally we'd encode them as e.g. "fast call @llvm.cos", where "fast"
      // means that fastmath is enabled in the intrinsic.  Unfortunately only
      // binary operators (currently) have a fastmath bit in SelectionDAG, so
      // this information gets lost and we can't select on it.
      //
      // TODO: div and rcp are lowered to a binary op, so these we could in
      // theory lower them to "fast fdiv".

    default:
      return {};
    }
  }();

  // If Action.FtzRequirementTy is not satisfied by the module's ftz state, we
  // can bail out now.  (Notice that in the case that IID is not an NVVM
  // intrinsic, we don't have to look up any module metadata, as
  // FtzRequirementTy will be FTZ_Any.)
  if (Action.FtzRequirement != FTZ_Any) {
    // FIXME: Broken for f64
    DenormalMode Mode = II->getFunction()->getDenormalMode(
        Action.IsHalfTy ? APFloat::IEEEhalf() : APFloat::IEEEsingle());
    bool FtzEnabled = Mode.Output == DenormalMode::PreserveSign;

    if (FtzEnabled != (Action.FtzRequirement == FTZ_MustBeOn))
      return nullptr;
  }

  // Simplify to target-generic intrinsic.
  if (Action.IID) {
    SmallVector<Value *, 4> Args(II->args());
    // All the target-generic intrinsics currently of interest to us have one
    // type argument, equal to that of the nvvm intrinsic's argument.
    Type *Tys[] = {II->getArgOperand(0)->getType()};
    return CallInst::Create(
        Intrinsic::getDeclaration(II->getModule(), *Action.IID, Tys), Args);
  }

  // Simplify to target-generic binary op.
  if (Action.BinaryOp)
    return BinaryOperator::Create(*Action.BinaryOp, II->getArgOperand(0),
                                  II->getArgOperand(1), II->getName());

  // Simplify to target-generic cast op.
  if (Action.CastOp)
    return CastInst::Create(*Action.CastOp, II->getArgOperand(0), II->getType(),
                            II->getName());

  // All that's left are the special cases.
  if (!Action.Special)
    return nullptr;

  switch (*Action.Special) {
  case SPC_Reciprocal:
    // Simplify reciprocal.
    return BinaryOperator::Create(
        Instruction::FDiv, ConstantFP::get(II->getArgOperand(0)->getType(), 1),
        II->getArgOperand(0), II->getName());
  }
  llvm_unreachable("All SpecialCase enumerators should be handled in switch.");
}

std::optional<Instruction *>
NVPTXTTIImpl::instCombineIntrinsic(InstCombiner &IC, IntrinsicInst &II) const {
  if (Instruction *I = simplifyNvvmIntrinsic(&II, IC)) {
    return I;
  }
  return std::nullopt;
}

InstructionCost NVPTXTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {
  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);

  int ISD = TLI->InstructionOpcodeToISD(Opcode);

  switch (ISD) {
  default:
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info);
  case ISD::ADD:
  case ISD::MUL:
  case ISD::XOR:
  case ISD::OR:
  case ISD::AND:
    // The machine code (SASS) simulates an i64 with two i32. Therefore, we
    // estimate that arithmetic operations on i64 are twice as expensive as
    // those on types that can fit into one machine register.
    if (LT.second.SimpleTy == MVT::i64)
      return 2 * LT.first;
    // Delegate other cases to the basic TTI.
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info);
  }
}

void NVPTXTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                           TTI::UnrollingPreferences &UP,
                                           OptimizationRemarkEmitter *ORE) {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);

  // Enable partial unrolling and runtime unrolling, but reduce the
  // threshold.  This partially unrolls small loops which are often
  // unrolled by the PTX to SASS compiler and unrolling earlier can be
  // beneficial.
  UP.Partial = UP.Runtime = true;
  UP.PartialThreshold = UP.Threshold / 4;
}

void NVPTXTTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                         TTI::PeelingPreferences &PP) {
  BaseT::getPeelingPreferences(L, SE, PP);
}
