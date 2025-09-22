//===- AMDGPUTargetTransformInfo.cpp - AMDGPU specific TTI pass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements a TargetTransformInfo analysis pass specific to the
// AMDGPU target machine. It uses the target's detailed information to provide
// more precise answers to certain TTI queries, while letting the target
// independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUTargetTransformInfo.h"
#include "AMDGPUTargetMachine.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIModeRegisterDefaults.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/KnownBits.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "AMDGPUtti"

static cl::opt<unsigned> UnrollThresholdPrivate(
  "amdgpu-unroll-threshold-private",
  cl::desc("Unroll threshold for AMDGPU if private memory used in a loop"),
  cl::init(2700), cl::Hidden);

static cl::opt<unsigned> UnrollThresholdLocal(
  "amdgpu-unroll-threshold-local",
  cl::desc("Unroll threshold for AMDGPU if local memory used in a loop"),
  cl::init(1000), cl::Hidden);

static cl::opt<unsigned> UnrollThresholdIf(
  "amdgpu-unroll-threshold-if",
  cl::desc("Unroll threshold increment for AMDGPU for each if statement inside loop"),
  cl::init(200), cl::Hidden);

static cl::opt<bool> UnrollRuntimeLocal(
  "amdgpu-unroll-runtime-local",
  cl::desc("Allow runtime unroll for AMDGPU if local memory used in a loop"),
  cl::init(true), cl::Hidden);

static cl::opt<unsigned> UnrollMaxBlockToAnalyze(
    "amdgpu-unroll-max-block-to-analyze",
    cl::desc("Inner loop block size threshold to analyze in unroll for AMDGPU"),
    cl::init(32), cl::Hidden);

static cl::opt<unsigned> ArgAllocaCost("amdgpu-inline-arg-alloca-cost",
                                       cl::Hidden, cl::init(4000),
                                       cl::desc("Cost of alloca argument"));

// If the amount of scratch memory to eliminate exceeds our ability to allocate
// it into registers we gain nothing by aggressively inlining functions for that
// heuristic.
static cl::opt<unsigned>
    ArgAllocaCutoff("amdgpu-inline-arg-alloca-cutoff", cl::Hidden,
                    cl::init(256),
                    cl::desc("Maximum alloca size to use for inline cost"));

// Inliner constraint to achieve reasonable compilation time.
static cl::opt<size_t> InlineMaxBB(
    "amdgpu-inline-max-bb", cl::Hidden, cl::init(1100),
    cl::desc("Maximum number of BBs allowed in a function after inlining"
             " (compile time constraint)"));

static bool dependsOnLocalPhi(const Loop *L, const Value *Cond,
                              unsigned Depth = 0) {
  const Instruction *I = dyn_cast<Instruction>(Cond);
  if (!I)
    return false;

  for (const Value *V : I->operand_values()) {
    if (!L->contains(I))
      continue;
    if (const PHINode *PHI = dyn_cast<PHINode>(V)) {
      if (llvm::none_of(L->getSubLoops(), [PHI](const Loop* SubLoop) {
                  return SubLoop->contains(PHI); }))
        return true;
    } else if (Depth < 10 && dependsOnLocalPhi(L, V, Depth+1))
      return true;
  }
  return false;
}

AMDGPUTTIImpl::AMDGPUTTIImpl(const AMDGPUTargetMachine *TM, const Function &F)
    : BaseT(TM, F.getDataLayout()),
      TargetTriple(TM->getTargetTriple()),
      ST(static_cast<const GCNSubtarget *>(TM->getSubtargetImpl(F))),
      TLI(ST->getTargetLowering()) {}

void AMDGPUTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                            TTI::UnrollingPreferences &UP,
                                            OptimizationRemarkEmitter *ORE) {
  const Function &F = *L->getHeader()->getParent();
  UP.Threshold =
      F.getFnAttributeAsParsedInteger("amdgpu-unroll-threshold", 300);
  UP.MaxCount = std::numeric_limits<unsigned>::max();
  UP.Partial = true;

  // Conditional branch in a loop back edge needs 3 additional exec
  // manipulations in average.
  UP.BEInsns += 3;

  // We want to run unroll even for the loops which have been vectorized.
  UP.UnrollVectorizedLoop = true;

  // TODO: Do we want runtime unrolling?

  // Maximum alloca size than can fit registers. Reserve 16 registers.
  const unsigned MaxAlloca = (256 - 16) * 4;
  unsigned ThresholdPrivate = UnrollThresholdPrivate;
  unsigned ThresholdLocal = UnrollThresholdLocal;

  // If this loop has the amdgpu.loop.unroll.threshold metadata we will use the
  // provided threshold value as the default for Threshold
  if (MDNode *LoopUnrollThreshold =
          findOptionMDForLoop(L, "amdgpu.loop.unroll.threshold")) {
    if (LoopUnrollThreshold->getNumOperands() == 2) {
      ConstantInt *MetaThresholdValue = mdconst::extract_or_null<ConstantInt>(
          LoopUnrollThreshold->getOperand(1));
      if (MetaThresholdValue) {
        // We will also use the supplied value for PartialThreshold for now.
        // We may introduce additional metadata if it becomes necessary in the
        // future.
        UP.Threshold = MetaThresholdValue->getSExtValue();
        UP.PartialThreshold = UP.Threshold;
        ThresholdPrivate = std::min(ThresholdPrivate, UP.Threshold);
        ThresholdLocal = std::min(ThresholdLocal, UP.Threshold);
      }
    }
  }

  unsigned MaxBoost = std::max(ThresholdPrivate, ThresholdLocal);
  for (const BasicBlock *BB : L->getBlocks()) {
    const DataLayout &DL = BB->getDataLayout();
    unsigned LocalGEPsSeen = 0;

    if (llvm::any_of(L->getSubLoops(), [BB](const Loop* SubLoop) {
               return SubLoop->contains(BB); }))
        continue; // Block belongs to an inner loop.

    for (const Instruction &I : *BB) {
      // Unroll a loop which contains an "if" statement whose condition
      // defined by a PHI belonging to the loop. This may help to eliminate
      // if region and potentially even PHI itself, saving on both divergence
      // and registers used for the PHI.
      // Add a small bonus for each of such "if" statements.
      if (const BranchInst *Br = dyn_cast<BranchInst>(&I)) {
        if (UP.Threshold < MaxBoost && Br->isConditional()) {
          BasicBlock *Succ0 = Br->getSuccessor(0);
          BasicBlock *Succ1 = Br->getSuccessor(1);
          if ((L->contains(Succ0) && L->isLoopExiting(Succ0)) ||
              (L->contains(Succ1) && L->isLoopExiting(Succ1)))
            continue;
          if (dependsOnLocalPhi(L, Br->getCondition())) {
            UP.Threshold += UnrollThresholdIf;
            LLVM_DEBUG(dbgs() << "Set unroll threshold " << UP.Threshold
                              << " for loop:\n"
                              << *L << " due to " << *Br << '\n');
            if (UP.Threshold >= MaxBoost)
              return;
          }
        }
        continue;
      }

      const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&I);
      if (!GEP)
        continue;

      unsigned AS = GEP->getAddressSpace();
      unsigned Threshold = 0;
      if (AS == AMDGPUAS::PRIVATE_ADDRESS)
        Threshold = ThresholdPrivate;
      else if (AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::REGION_ADDRESS)
        Threshold = ThresholdLocal;
      else
        continue;

      if (UP.Threshold >= Threshold)
        continue;

      if (AS == AMDGPUAS::PRIVATE_ADDRESS) {
        const Value *Ptr = GEP->getPointerOperand();
        const AllocaInst *Alloca =
            dyn_cast<AllocaInst>(getUnderlyingObject(Ptr));
        if (!Alloca || !Alloca->isStaticAlloca())
          continue;
        Type *Ty = Alloca->getAllocatedType();
        unsigned AllocaSize = Ty->isSized() ? DL.getTypeAllocSize(Ty) : 0;
        if (AllocaSize > MaxAlloca)
          continue;
      } else if (AS == AMDGPUAS::LOCAL_ADDRESS ||
                 AS == AMDGPUAS::REGION_ADDRESS) {
        LocalGEPsSeen++;
        // Inhibit unroll for local memory if we have seen addressing not to
        // a variable, most likely we will be unable to combine it.
        // Do not unroll too deep inner loops for local memory to give a chance
        // to unroll an outer loop for a more important reason.
        if (LocalGEPsSeen > 1 || L->getLoopDepth() > 2 ||
            (!isa<GlobalVariable>(GEP->getPointerOperand()) &&
             !isa<Argument>(GEP->getPointerOperand())))
          continue;
        LLVM_DEBUG(dbgs() << "Allow unroll runtime for loop:\n"
                          << *L << " due to LDS use.\n");
        UP.Runtime = UnrollRuntimeLocal;
      }

      // Check if GEP depends on a value defined by this loop itself.
      bool HasLoopDef = false;
      for (const Value *Op : GEP->operands()) {
        const Instruction *Inst = dyn_cast<Instruction>(Op);
        if (!Inst || L->isLoopInvariant(Op))
          continue;

        if (llvm::any_of(L->getSubLoops(), [Inst](const Loop* SubLoop) {
             return SubLoop->contains(Inst); }))
          continue;
        HasLoopDef = true;
        break;
      }
      if (!HasLoopDef)
        continue;

      // We want to do whatever we can to limit the number of alloca
      // instructions that make it through to the code generator.  allocas
      // require us to use indirect addressing, which is slow and prone to
      // compiler bugs.  If this loop does an address calculation on an
      // alloca ptr, then we want to use a higher than normal loop unroll
      // threshold. This will give SROA a better chance to eliminate these
      // allocas.
      //
      // We also want to have more unrolling for local memory to let ds
      // instructions with different offsets combine.
      //
      // Don't use the maximum allowed value here as it will make some
      // programs way too big.
      UP.Threshold = Threshold;
      LLVM_DEBUG(dbgs() << "Set unroll threshold " << Threshold
                        << " for loop:\n"
                        << *L << " due to " << *GEP << '\n');
      if (UP.Threshold >= MaxBoost)
        return;
    }

    // If we got a GEP in a small BB from inner loop then increase max trip
    // count to analyze for better estimation cost in unroll
    if (L->isInnermost() && BB->size() < UnrollMaxBlockToAnalyze)
      UP.MaxIterationsCountToAnalyze = 32;
  }
}

void AMDGPUTTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                          TTI::PeelingPreferences &PP) {
  BaseT::getPeelingPreferences(L, SE, PP);
}

int64_t AMDGPUTTIImpl::getMaxMemIntrinsicInlineSizeThreshold() const {
  return 1024;
}

const FeatureBitset GCNTTIImpl::InlineFeatureIgnoreList = {
    // Codegen control options which don't matter.
    AMDGPU::FeatureEnableLoadStoreOpt, AMDGPU::FeatureEnableSIScheduler,
    AMDGPU::FeatureEnableUnsafeDSOffsetFolding, AMDGPU::FeatureFlatForGlobal,
    AMDGPU::FeaturePromoteAlloca, AMDGPU::FeatureUnalignedScratchAccess,
    AMDGPU::FeatureUnalignedAccessMode,

    AMDGPU::FeatureAutoWaitcntBeforeBarrier,

    // Property of the kernel/environment which can't actually differ.
    AMDGPU::FeatureSGPRInitBug, AMDGPU::FeatureXNACK,
    AMDGPU::FeatureTrapHandler,

    // The default assumption needs to be ecc is enabled, but no directly
    // exposed operations depend on it, so it can be safely inlined.
    AMDGPU::FeatureSRAMECC,

    // Perf-tuning features
    AMDGPU::FeatureFastFMAF32, AMDGPU::HalfRate64Ops};

GCNTTIImpl::GCNTTIImpl(const AMDGPUTargetMachine *TM, const Function &F)
    : BaseT(TM, F.getDataLayout()),
      ST(static_cast<const GCNSubtarget *>(TM->getSubtargetImpl(F))),
      TLI(ST->getTargetLowering()), CommonTTI(TM, F),
      IsGraphics(AMDGPU::isGraphics(F.getCallingConv())) {
  SIModeRegisterDefaults Mode(F, *ST);
  HasFP32Denormals = Mode.FP32Denormals != DenormalMode::getPreserveSign();
  HasFP64FP16Denormals =
      Mode.FP64FP16Denormals != DenormalMode::getPreserveSign();
}

bool GCNTTIImpl::hasBranchDivergence(const Function *F) const {
  return !F || !ST->isSingleLaneExecution(*F);
}

unsigned GCNTTIImpl::getNumberOfRegisters(unsigned RCID) const {
  // NB: RCID is not an RCID. In fact it is 0 or 1 for scalar or vector
  // registers. See getRegisterClassForType for the implementation.
  // In this case vector registers are not vector in terms of
  // VGPRs, but those which can hold multiple values.

  // This is really the number of registers to fill when vectorizing /
  // interleaving loops, so we lie to avoid trying to use all registers.
  return 4;
}

TypeSize
GCNTTIImpl::getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
  switch (K) {
  case TargetTransformInfo::RGK_Scalar:
    return TypeSize::getFixed(32);
  case TargetTransformInfo::RGK_FixedWidthVector:
    return TypeSize::getFixed(ST->hasPackedFP32Ops() ? 64 : 32);
  case TargetTransformInfo::RGK_ScalableVector:
    return TypeSize::getScalable(0);
  }
  llvm_unreachable("Unsupported register kind");
}

unsigned GCNTTIImpl::getMinVectorRegisterBitWidth() const {
  return 32;
}

unsigned GCNTTIImpl::getMaximumVF(unsigned ElemWidth, unsigned Opcode) const {
  if (Opcode == Instruction::Load || Opcode == Instruction::Store)
    return 32 * 4 / ElemWidth;
  return (ElemWidth == 16 && ST->has16BitInsts()) ? 2
       : (ElemWidth == 32 && ST->hasPackedFP32Ops()) ? 2
       : 1;
}

unsigned GCNTTIImpl::getLoadVectorFactor(unsigned VF, unsigned LoadSize,
                                         unsigned ChainSizeInBytes,
                                         VectorType *VecTy) const {
  unsigned VecRegBitWidth = VF * LoadSize;
  if (VecRegBitWidth > 128 && VecTy->getScalarSizeInBits() < 32)
    // TODO: Support element-size less than 32bit?
    return 128 / LoadSize;

  return VF;
}

unsigned GCNTTIImpl::getStoreVectorFactor(unsigned VF, unsigned StoreSize,
                                             unsigned ChainSizeInBytes,
                                             VectorType *VecTy) const {
  unsigned VecRegBitWidth = VF * StoreSize;
  if (VecRegBitWidth > 128)
    return 128 / StoreSize;

  return VF;
}

unsigned GCNTTIImpl::getLoadStoreVecRegBitWidth(unsigned AddrSpace) const {
  if (AddrSpace == AMDGPUAS::GLOBAL_ADDRESS ||
      AddrSpace == AMDGPUAS::CONSTANT_ADDRESS ||
      AddrSpace == AMDGPUAS::CONSTANT_ADDRESS_32BIT ||
      AddrSpace == AMDGPUAS::BUFFER_FAT_POINTER ||
      AddrSpace == AMDGPUAS::BUFFER_RESOURCE ||
      AddrSpace == AMDGPUAS::BUFFER_STRIDED_POINTER) {
    return 512;
  }

  if (AddrSpace == AMDGPUAS::PRIVATE_ADDRESS)
    return 8 * ST->getMaxPrivateElementSize();

  // Common to flat, global, local and region. Assume for unknown addrspace.
  return 128;
}

bool GCNTTIImpl::isLegalToVectorizeMemChain(unsigned ChainSizeInBytes,
                                            Align Alignment,
                                            unsigned AddrSpace) const {
  // We allow vectorization of flat stores, even though we may need to decompose
  // them later if they may access private memory. We don't have enough context
  // here, and legalization can handle it.
  if (AddrSpace == AMDGPUAS::PRIVATE_ADDRESS) {
    return (Alignment >= 4 || ST->hasUnalignedScratchAccess()) &&
      ChainSizeInBytes <= ST->getMaxPrivateElementSize();
  }
  return true;
}

bool GCNTTIImpl::isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                             Align Alignment,
                                             unsigned AddrSpace) const {
  return isLegalToVectorizeMemChain(ChainSizeInBytes, Alignment, AddrSpace);
}

bool GCNTTIImpl::isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                              Align Alignment,
                                              unsigned AddrSpace) const {
  return isLegalToVectorizeMemChain(ChainSizeInBytes, Alignment, AddrSpace);
}

int64_t GCNTTIImpl::getMaxMemIntrinsicInlineSizeThreshold() const {
  return 1024;
}

// FIXME: Really we would like to issue multiple 128-bit loads and stores per
// iteration. Should we report a larger size and let it legalize?
//
// FIXME: Should we use narrower types for local/region, or account for when
// unaligned access is legal?
//
// FIXME: This could use fine tuning and microbenchmarks.
Type *GCNTTIImpl::getMemcpyLoopLoweringType(
    LLVMContext &Context, Value *Length, unsigned SrcAddrSpace,
    unsigned DestAddrSpace, unsigned SrcAlign, unsigned DestAlign,
    std::optional<uint32_t> AtomicElementSize) const {

  if (AtomicElementSize)
    return Type::getIntNTy(Context, *AtomicElementSize * 8);

  unsigned MinAlign = std::min(SrcAlign, DestAlign);

  // A (multi-)dword access at an address == 2 (mod 4) will be decomposed by the
  // hardware into byte accesses. If you assume all alignments are equally
  // probable, it's more efficient on average to use short accesses for this
  // case.
  if (MinAlign == 2)
    return Type::getInt16Ty(Context);

  // Not all subtargets have 128-bit DS instructions, and we currently don't
  // form them by default.
  if (SrcAddrSpace == AMDGPUAS::LOCAL_ADDRESS ||
      SrcAddrSpace == AMDGPUAS::REGION_ADDRESS ||
      DestAddrSpace == AMDGPUAS::LOCAL_ADDRESS ||
      DestAddrSpace == AMDGPUAS::REGION_ADDRESS) {
    return FixedVectorType::get(Type::getInt32Ty(Context), 2);
  }

  // Global memory works best with 16-byte accesses. Private memory will also
  // hit this, although they'll be decomposed.
  return FixedVectorType::get(Type::getInt32Ty(Context), 4);
}

void GCNTTIImpl::getMemcpyLoopResidualLoweringType(
    SmallVectorImpl<Type *> &OpsOut, LLVMContext &Context,
    unsigned RemainingBytes, unsigned SrcAddrSpace, unsigned DestAddrSpace,
    unsigned SrcAlign, unsigned DestAlign,
    std::optional<uint32_t> AtomicCpySize) const {
  assert(RemainingBytes < 16);

  if (AtomicCpySize)
    BaseT::getMemcpyLoopResidualLoweringType(
        OpsOut, Context, RemainingBytes, SrcAddrSpace, DestAddrSpace, SrcAlign,
        DestAlign, AtomicCpySize);

  unsigned MinAlign = std::min(SrcAlign, DestAlign);

  if (MinAlign != 2) {
    Type *I64Ty = Type::getInt64Ty(Context);
    while (RemainingBytes >= 8) {
      OpsOut.push_back(I64Ty);
      RemainingBytes -= 8;
    }

    Type *I32Ty = Type::getInt32Ty(Context);
    while (RemainingBytes >= 4) {
      OpsOut.push_back(I32Ty);
      RemainingBytes -= 4;
    }
  }

  Type *I16Ty = Type::getInt16Ty(Context);
  while (RemainingBytes >= 2) {
    OpsOut.push_back(I16Ty);
    RemainingBytes -= 2;
  }

  Type *I8Ty = Type::getInt8Ty(Context);
  while (RemainingBytes) {
    OpsOut.push_back(I8Ty);
    --RemainingBytes;
  }
}

unsigned GCNTTIImpl::getMaxInterleaveFactor(ElementCount VF) {
  // Disable unrolling if the loop is not vectorized.
  // TODO: Enable this again.
  if (VF.isScalar())
    return 1;

  return 8;
}

bool GCNTTIImpl::getTgtMemIntrinsic(IntrinsicInst *Inst,
                                       MemIntrinsicInfo &Info) const {
  switch (Inst->getIntrinsicID()) {
  case Intrinsic::amdgcn_ds_ordered_add:
  case Intrinsic::amdgcn_ds_ordered_swap: {
    auto *Ordering = dyn_cast<ConstantInt>(Inst->getArgOperand(2));
    auto *Volatile = dyn_cast<ConstantInt>(Inst->getArgOperand(4));
    if (!Ordering || !Volatile)
      return false; // Invalid.

    unsigned OrderingVal = Ordering->getZExtValue();
    if (OrderingVal > static_cast<unsigned>(AtomicOrdering::SequentiallyConsistent))
      return false;

    Info.PtrVal = Inst->getArgOperand(0);
    Info.Ordering = static_cast<AtomicOrdering>(OrderingVal);
    Info.ReadMem = true;
    Info.WriteMem = true;
    Info.IsVolatile = !Volatile->isZero();
    return true;
  }
  default:
    return false;
  }
}

InstructionCost GCNTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);
  int ISD = TLI->InstructionOpcodeToISD(Opcode);

  // Because we don't have any legal vector operations, but the legal types, we
  // need to account for split vectors.
  unsigned NElts = LT.second.isVector() ?
    LT.second.getVectorNumElements() : 1;

  MVT::SimpleValueType SLT = LT.second.getScalarType().SimpleTy;

  switch (ISD) {
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:
    if (SLT == MVT::i64)
      return get64BitInstrCost(CostKind) * LT.first * NElts;

    if (ST->has16BitInsts() && SLT == MVT::i16)
      NElts = (NElts + 1) / 2;

    // i32
    return getFullRateInstrCost() * LT.first * NElts;
  case ISD::ADD:
  case ISD::SUB:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    if (SLT == MVT::i64) {
      // and, or and xor are typically split into 2 VALU instructions.
      return 2 * getFullRateInstrCost() * LT.first * NElts;
    }

    if (ST->has16BitInsts() && SLT == MVT::i16)
      NElts = (NElts + 1) / 2;

    return LT.first * NElts * getFullRateInstrCost();
  case ISD::MUL: {
    const int QuarterRateCost = getQuarterRateInstrCost(CostKind);
    if (SLT == MVT::i64) {
      const int FullRateCost = getFullRateInstrCost();
      return (4 * QuarterRateCost + (2 * 2) * FullRateCost) * LT.first * NElts;
    }

    if (ST->has16BitInsts() && SLT == MVT::i16)
      NElts = (NElts + 1) / 2;

    // i32
    return QuarterRateCost * NElts * LT.first;
  }
  case ISD::FMUL:
    // Check possible fuse {fadd|fsub}(a,fmul(b,c)) and return zero cost for
    // fmul(b,c) supposing the fadd|fsub will get estimated cost for the whole
    // fused operation.
    if (CxtI && CxtI->hasOneUse())
      if (const auto *FAdd = dyn_cast<BinaryOperator>(*CxtI->user_begin())) {
        const int OPC = TLI->InstructionOpcodeToISD(FAdd->getOpcode());
        if (OPC == ISD::FADD || OPC == ISD::FSUB) {
          if (ST->hasMadMacF32Insts() && SLT == MVT::f32 && !HasFP32Denormals)
            return TargetTransformInfo::TCC_Free;
          if (ST->has16BitInsts() && SLT == MVT::f16 && !HasFP64FP16Denormals)
            return TargetTransformInfo::TCC_Free;

          // Estimate all types may be fused with contract/unsafe flags
          const TargetOptions &Options = TLI->getTargetMachine().Options;
          if (Options.AllowFPOpFusion == FPOpFusion::Fast ||
              Options.UnsafeFPMath ||
              (FAdd->hasAllowContract() && CxtI->hasAllowContract()))
            return TargetTransformInfo::TCC_Free;
        }
      }
    [[fallthrough]];
  case ISD::FADD:
  case ISD::FSUB:
    if (ST->hasPackedFP32Ops() && SLT == MVT::f32)
      NElts = (NElts + 1) / 2;
    if (SLT == MVT::f64)
      return LT.first * NElts * get64BitInstrCost(CostKind);

    if (ST->has16BitInsts() && SLT == MVT::f16)
      NElts = (NElts + 1) / 2;

    if (SLT == MVT::f32 || SLT == MVT::f16)
      return LT.first * NElts * getFullRateInstrCost();
    break;
  case ISD::FDIV:
  case ISD::FREM:
    // FIXME: frem should be handled separately. The fdiv in it is most of it,
    // but the current lowering is also not entirely correct.
    if (SLT == MVT::f64) {
      int Cost = 7 * get64BitInstrCost(CostKind) +
                 getQuarterRateInstrCost(CostKind) +
                 3 * getHalfRateInstrCost(CostKind);
      // Add cost of workaround.
      if (!ST->hasUsableDivScaleConditionOutput())
        Cost += 3 * getFullRateInstrCost();

      return LT.first * Cost * NElts;
    }

    if (!Args.empty() && match(Args[0], PatternMatch::m_FPOne())) {
      // TODO: This is more complicated, unsafe flags etc.
      if ((SLT == MVT::f32 && !HasFP32Denormals) ||
          (SLT == MVT::f16 && ST->has16BitInsts())) {
        return LT.first * getQuarterRateInstrCost(CostKind) * NElts;
      }
    }

    if (SLT == MVT::f16 && ST->has16BitInsts()) {
      // 2 x v_cvt_f32_f16
      // f32 rcp
      // f32 fmul
      // v_cvt_f16_f32
      // f16 div_fixup
      int Cost =
          4 * getFullRateInstrCost() + 2 * getQuarterRateInstrCost(CostKind);
      return LT.first * Cost * NElts;
    }

    if (SLT == MVT::f32 && ((CxtI && CxtI->hasApproxFunc()) ||
                            TLI->getTargetMachine().Options.UnsafeFPMath)) {
      // Fast unsafe fdiv lowering:
      // f32 rcp
      // f32 fmul
      int Cost = getQuarterRateInstrCost(CostKind) + getFullRateInstrCost();
      return LT.first * Cost * NElts;
    }

    if (SLT == MVT::f32 || SLT == MVT::f16) {
      // 4 more v_cvt_* insts without f16 insts support
      int Cost = (SLT == MVT::f16 ? 14 : 10) * getFullRateInstrCost() +
                 1 * getQuarterRateInstrCost(CostKind);

      if (!HasFP32Denormals) {
        // FP mode switches.
        Cost += 2 * getFullRateInstrCost();
      }

      return LT.first * NElts * Cost;
    }
    break;
  case ISD::FNEG:
    // Use the backend' estimation. If fneg is not free each element will cost
    // one additional instruction.
    return TLI->isFNegFree(SLT) ? 0 : NElts;
  default:
    break;
  }

  return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                       Args, CxtI);
}

// Return true if there's a potential benefit from using v2f16/v2i16
// instructions for an intrinsic, even if it requires nontrivial legalization.
static bool intrinsicHasPackedVectorBenefit(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::fma: // TODO: fmuladd
  // There's a small benefit to using vector ops in the legalized code.
  case Intrinsic::round:
  case Intrinsic::uadd_sat:
  case Intrinsic::usub_sat:
  case Intrinsic::sadd_sat:
  case Intrinsic::ssub_sat:
    return true;
  default:
    return false;
  }
}

InstructionCost
GCNTTIImpl::getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                  TTI::TargetCostKind CostKind) {
  if (ICA.getID() == Intrinsic::fabs)
    return 0;

  if (!intrinsicHasPackedVectorBenefit(ICA.getID()))
    return BaseT::getIntrinsicInstrCost(ICA, CostKind);

  Type *RetTy = ICA.getReturnType();

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(RetTy);

  unsigned NElts = LT.second.isVector() ?
    LT.second.getVectorNumElements() : 1;

  MVT::SimpleValueType SLT = LT.second.getScalarType().SimpleTy;

  if (SLT == MVT::f64)
    return LT.first * NElts * get64BitInstrCost(CostKind);

  if ((ST->has16BitInsts() && SLT == MVT::f16) ||
      (ST->hasPackedFP32Ops() && SLT == MVT::f32))
    NElts = (NElts + 1) / 2;

  // TODO: Get more refined intrinsic costs?
  unsigned InstRate = getQuarterRateInstrCost(CostKind);

  switch (ICA.getID()) {
  case Intrinsic::fma:
    InstRate = ST->hasFastFMAF32() ? getHalfRateInstrCost(CostKind)
                                   : getQuarterRateInstrCost(CostKind);
    break;
  case Intrinsic::uadd_sat:
  case Intrinsic::usub_sat:
  case Intrinsic::sadd_sat:
  case Intrinsic::ssub_sat:
    static const auto ValidSatTys = {MVT::v2i16, MVT::v4i16};
    if (any_of(ValidSatTys, [&LT](MVT M) { return M == LT.second; }))
      NElts = 1;
    break;
  }

  return LT.first * NElts * InstRate;
}

InstructionCost GCNTTIImpl::getCFInstrCost(unsigned Opcode,
                                           TTI::TargetCostKind CostKind,
                                           const Instruction *I) {
  assert((I == nullptr || I->getOpcode() == Opcode) &&
         "Opcode should reflect passed instruction.");
  const bool SCost =
      (CostKind == TTI::TCK_CodeSize || CostKind == TTI::TCK_SizeAndLatency);
  const int CBrCost = SCost ? 5 : 7;
  switch (Opcode) {
  case Instruction::Br: {
    // Branch instruction takes about 4 slots on gfx900.
    auto BI = dyn_cast_or_null<BranchInst>(I);
    if (BI && BI->isUnconditional())
      return SCost ? 1 : 4;
    // Suppose conditional branch takes additional 3 exec manipulations
    // instructions in average.
    return CBrCost;
  }
  case Instruction::Switch: {
    auto SI = dyn_cast_or_null<SwitchInst>(I);
    // Each case (including default) takes 1 cmp + 1 cbr instructions in
    // average.
    return (SI ? (SI->getNumCases() + 1) : 4) * (CBrCost + 1);
  }
  case Instruction::Ret:
    return SCost ? 1 : 10;
  }
  return BaseT::getCFInstrCost(Opcode, CostKind, I);
}

InstructionCost
GCNTTIImpl::getArithmeticReductionCost(unsigned Opcode, VectorType *Ty,
                                       std::optional<FastMathFlags> FMF,
                                       TTI::TargetCostKind CostKind) {
  if (TTI::requiresOrderedReduction(FMF))
    return BaseT::getArithmeticReductionCost(Opcode, Ty, FMF, CostKind);

  EVT OrigTy = TLI->getValueType(DL, Ty);

  // Computes cost on targets that have packed math instructions(which support
  // 16-bit types only).
  if (!ST->hasVOP3PInsts() || OrigTy.getScalarSizeInBits() != 16)
    return BaseT::getArithmeticReductionCost(Opcode, Ty, FMF, CostKind);

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);
  return LT.first * getFullRateInstrCost();
}

InstructionCost
GCNTTIImpl::getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                   FastMathFlags FMF,
                                   TTI::TargetCostKind CostKind) {
  EVT OrigTy = TLI->getValueType(DL, Ty);

  // Computes cost on targets that have packed math instructions(which support
  // 16-bit types only).
  if (!ST->hasVOP3PInsts() || OrigTy.getScalarSizeInBits() != 16)
    return BaseT::getMinMaxReductionCost(IID, Ty, FMF, CostKind);

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);
  return LT.first * getHalfRateInstrCost(CostKind);
}

InstructionCost GCNTTIImpl::getVectorInstrCost(unsigned Opcode, Type *ValTy,
                                               TTI::TargetCostKind CostKind,
                                               unsigned Index, Value *Op0,
                                               Value *Op1) {
  switch (Opcode) {
  case Instruction::ExtractElement:
  case Instruction::InsertElement: {
    unsigned EltSize
      = DL.getTypeSizeInBits(cast<VectorType>(ValTy)->getElementType());
    if (EltSize < 32) {
      if (EltSize == 16 && Index == 0 && ST->has16BitInsts())
        return 0;
      return BaseT::getVectorInstrCost(Opcode, ValTy, CostKind, Index, Op0,
                                       Op1);
    }

    // Extracts are just reads of a subregister, so are free. Inserts are
    // considered free because we don't want to have any cost for scalarizing
    // operations, and we don't have to copy into a different register class.

    // Dynamic indexing isn't free and is best avoided.
    return Index == ~0u ? 2 : 0;
  }
  default:
    return BaseT::getVectorInstrCost(Opcode, ValTy, CostKind, Index, Op0, Op1);
  }
}

/// Analyze if the results of inline asm are divergent. If \p Indices is empty,
/// this is analyzing the collective result of all output registers. Otherwise,
/// this is only querying a specific result index if this returns multiple
/// registers in a struct.
bool GCNTTIImpl::isInlineAsmSourceOfDivergence(
  const CallInst *CI, ArrayRef<unsigned> Indices) const {
  // TODO: Handle complex extract indices
  if (Indices.size() > 1)
    return true;

  const DataLayout &DL = CI->getDataLayout();
  const SIRegisterInfo *TRI = ST->getRegisterInfo();
  TargetLowering::AsmOperandInfoVector TargetConstraints =
      TLI->ParseConstraints(DL, ST->getRegisterInfo(), *CI);

  const int TargetOutputIdx = Indices.empty() ? -1 : Indices[0];

  int OutputIdx = 0;
  for (auto &TC : TargetConstraints) {
    if (TC.Type != InlineAsm::isOutput)
      continue;

    // Skip outputs we don't care about.
    if (TargetOutputIdx != -1 && TargetOutputIdx != OutputIdx++)
      continue;

    TLI->ComputeConstraintToUse(TC, SDValue());

    const TargetRegisterClass *RC = TLI->getRegForInlineAsmConstraint(
        TRI, TC.ConstraintCode, TC.ConstraintVT).second;

    // For AGPR constraints null is returned on subtargets without AGPRs, so
    // assume divergent for null.
    if (!RC || !TRI->isSGPRClass(RC))
      return true;
  }

  return false;
}

bool GCNTTIImpl::isReadRegisterSourceOfDivergence(
    const IntrinsicInst *ReadReg) const {
  Metadata *MD =
      cast<MetadataAsValue>(ReadReg->getArgOperand(0))->getMetadata();
  StringRef RegName =
      cast<MDString>(cast<MDNode>(MD)->getOperand(0))->getString();

  // Special case registers that look like VCC.
  MVT VT = MVT::getVT(ReadReg->getType());
  if (VT == MVT::i1)
    return true;

  // Special case scalar registers that start with 'v'.
  if (RegName.starts_with("vcc") || RegName.empty())
    return false;

  // VGPR or AGPR is divergent. There aren't any specially named vector
  // registers.
  return RegName[0] == 'v' || RegName[0] == 'a';
}

/// \returns true if the result of the value could potentially be
/// different across workitems in a wavefront.
bool GCNTTIImpl::isSourceOfDivergence(const Value *V) const {
  if (const Argument *A = dyn_cast<Argument>(V))
    return !AMDGPU::isArgPassedInSGPR(A);

  // Loads from the private and flat address spaces are divergent, because
  // threads can execute the load instruction with the same inputs and get
  // different results.
  //
  // All other loads are not divergent, because if threads issue loads with the
  // same arguments, they will always get the same result.
  if (const LoadInst *Load = dyn_cast<LoadInst>(V))
    return Load->getPointerAddressSpace() == AMDGPUAS::PRIVATE_ADDRESS ||
           Load->getPointerAddressSpace() == AMDGPUAS::FLAT_ADDRESS;

  // Atomics are divergent because they are executed sequentially: when an
  // atomic operation refers to the same address in each thread, then each
  // thread after the first sees the value written by the previous thread as
  // original value.
  if (isa<AtomicRMWInst>(V) || isa<AtomicCmpXchgInst>(V))
    return true;

  if (const IntrinsicInst *Intrinsic = dyn_cast<IntrinsicInst>(V)) {
    if (Intrinsic->getIntrinsicID() == Intrinsic::read_register)
      return isReadRegisterSourceOfDivergence(Intrinsic);

    return AMDGPU::isIntrinsicSourceOfDivergence(Intrinsic->getIntrinsicID());
  }

  // Assume all function calls are a source of divergence.
  if (const CallInst *CI = dyn_cast<CallInst>(V)) {
    if (CI->isInlineAsm())
      return isInlineAsmSourceOfDivergence(CI);
    return true;
  }

  // Assume all function calls are a source of divergence.
  if (isa<InvokeInst>(V))
    return true;

  return false;
}

bool GCNTTIImpl::isAlwaysUniform(const Value *V) const {
  if (const IntrinsicInst *Intrinsic = dyn_cast<IntrinsicInst>(V))
    return AMDGPU::isIntrinsicAlwaysUniform(Intrinsic->getIntrinsicID());

  if (const CallInst *CI = dyn_cast<CallInst>(V)) {
    if (CI->isInlineAsm())
      return !isInlineAsmSourceOfDivergence(CI);
    return false;
  }

  // In most cases TID / wavefrontsize is uniform.
  //
  // However, if a kernel has uneven dimesions we can have a value of
  // workitem-id-x divided by the wavefrontsize non-uniform. For example
  // dimensions (65, 2) will have workitems with address (64, 0) and (0, 1)
  // packed into a same wave which gives 1 and 0 after the division by 64
  // respectively.
  //
  // FIXME: limit it to 1D kernels only, although that shall be possible
  // to perform this optimization is the size of the X dimension is a power
  // of 2, we just do not currently have infrastructure to query it.
  using namespace llvm::PatternMatch;
  uint64_t C;
  if (match(V, m_LShr(m_Intrinsic<Intrinsic::amdgcn_workitem_id_x>(),
                      m_ConstantInt(C))) ||
      match(V, m_AShr(m_Intrinsic<Intrinsic::amdgcn_workitem_id_x>(),
                      m_ConstantInt(C)))) {
    const Function *F = cast<Instruction>(V)->getFunction();
    return C >= ST->getWavefrontSizeLog2() &&
           ST->getMaxWorkitemID(*F, 1) == 0 && ST->getMaxWorkitemID(*F, 2) == 0;
  }

  Value *Mask;
  if (match(V, m_c_And(m_Intrinsic<Intrinsic::amdgcn_workitem_id_x>(),
                       m_Value(Mask)))) {
    const Function *F = cast<Instruction>(V)->getFunction();
    const DataLayout &DL = F->getDataLayout();
    return computeKnownBits(Mask, DL).countMinTrailingZeros() >=
               ST->getWavefrontSizeLog2() &&
           ST->getMaxWorkitemID(*F, 1) == 0 && ST->getMaxWorkitemID(*F, 2) == 0;
  }

  const ExtractValueInst *ExtValue = dyn_cast<ExtractValueInst>(V);
  if (!ExtValue)
    return false;

  const CallInst *CI = dyn_cast<CallInst>(ExtValue->getOperand(0));
  if (!CI)
    return false;

  if (const IntrinsicInst *Intrinsic = dyn_cast<IntrinsicInst>(CI)) {
    switch (Intrinsic->getIntrinsicID()) {
    default:
      return false;
    case Intrinsic::amdgcn_if:
    case Intrinsic::amdgcn_else: {
      ArrayRef<unsigned> Indices = ExtValue->getIndices();
      return Indices.size() == 1 && Indices[0] == 1;
    }
    }
  }

  // If we have inline asm returning mixed SGPR and VGPR results, we inferred
  // divergent for the overall struct return. We need to override it in the
  // case we're extracting an SGPR component here.
  if (CI->isInlineAsm())
    return !isInlineAsmSourceOfDivergence(CI, ExtValue->getIndices());

  return false;
}

bool GCNTTIImpl::collectFlatAddressOperands(SmallVectorImpl<int> &OpIndexes,
                                            Intrinsic::ID IID) const {
  switch (IID) {
  case Intrinsic::amdgcn_is_shared:
  case Intrinsic::amdgcn_is_private:
  case Intrinsic::amdgcn_flat_atomic_fadd:
  case Intrinsic::amdgcn_flat_atomic_fmax:
  case Intrinsic::amdgcn_flat_atomic_fmin:
  case Intrinsic::amdgcn_flat_atomic_fmax_num:
  case Intrinsic::amdgcn_flat_atomic_fmin_num:
    OpIndexes.push_back(0);
    return true;
  default:
    return false;
  }
}

Value *GCNTTIImpl::rewriteIntrinsicWithAddressSpace(IntrinsicInst *II,
                                                    Value *OldV,
                                                    Value *NewV) const {
  auto IntrID = II->getIntrinsicID();
  switch (IntrID) {
  case Intrinsic::amdgcn_is_shared:
  case Intrinsic::amdgcn_is_private: {
    unsigned TrueAS = IntrID == Intrinsic::amdgcn_is_shared ?
      AMDGPUAS::LOCAL_ADDRESS : AMDGPUAS::PRIVATE_ADDRESS;
    unsigned NewAS = NewV->getType()->getPointerAddressSpace();
    LLVMContext &Ctx = NewV->getType()->getContext();
    ConstantInt *NewVal = (TrueAS == NewAS) ?
      ConstantInt::getTrue(Ctx) : ConstantInt::getFalse(Ctx);
    return NewVal;
  }
  case Intrinsic::ptrmask: {
    unsigned OldAS = OldV->getType()->getPointerAddressSpace();
    unsigned NewAS = NewV->getType()->getPointerAddressSpace();
    Value *MaskOp = II->getArgOperand(1);
    Type *MaskTy = MaskOp->getType();

    bool DoTruncate = false;

    const GCNTargetMachine &TM =
        static_cast<const GCNTargetMachine &>(getTLI()->getTargetMachine());
    if (!TM.isNoopAddrSpaceCast(OldAS, NewAS)) {
      // All valid 64-bit to 32-bit casts work by chopping off the high
      // bits. Any masking only clearing the low bits will also apply in the new
      // address space.
      if (DL.getPointerSizeInBits(OldAS) != 64 ||
          DL.getPointerSizeInBits(NewAS) != 32)
        return nullptr;

      // TODO: Do we need to thread more context in here?
      KnownBits Known = computeKnownBits(MaskOp, DL, 0, nullptr, II);
      if (Known.countMinLeadingOnes() < 32)
        return nullptr;

      DoTruncate = true;
    }

    IRBuilder<> B(II);
    if (DoTruncate) {
      MaskTy = B.getInt32Ty();
      MaskOp = B.CreateTrunc(MaskOp, MaskTy);
    }

    return B.CreateIntrinsic(Intrinsic::ptrmask, {NewV->getType(), MaskTy},
                             {NewV, MaskOp});
  }
  case Intrinsic::amdgcn_flat_atomic_fadd:
  case Intrinsic::amdgcn_flat_atomic_fmax:
  case Intrinsic::amdgcn_flat_atomic_fmin:
  case Intrinsic::amdgcn_flat_atomic_fmax_num:
  case Intrinsic::amdgcn_flat_atomic_fmin_num: {
    Type *DestTy = II->getType();
    Type *SrcTy = NewV->getType();
    unsigned NewAS = SrcTy->getPointerAddressSpace();
    if (!AMDGPU::isExtendedGlobalAddrSpace(NewAS))
      return nullptr;
    Module *M = II->getModule();
    Function *NewDecl = Intrinsic::getDeclaration(M, II->getIntrinsicID(),
                                                  {DestTy, SrcTy, DestTy});
    II->setArgOperand(0, NewV);
    II->setCalledFunction(NewDecl);
    return II;
  }
  default:
    return nullptr;
  }
}

InstructionCost GCNTTIImpl::getShuffleCost(TTI::ShuffleKind Kind,
                                           VectorType *VT, ArrayRef<int> Mask,
                                           TTI::TargetCostKind CostKind,
                                           int Index, VectorType *SubTp,
                                           ArrayRef<const Value *> Args,
                                           const Instruction *CxtI) {
  if (!isa<FixedVectorType>(VT))
    return BaseT::getShuffleCost(Kind, VT, Mask, CostKind, Index, SubTp);

  Kind = improveShuffleKindFromMask(Kind, Mask, VT, Index, SubTp);

  // Larger vector widths may require additional instructions, but are
  // typically cheaper than scalarized versions.
  unsigned NumVectorElts = cast<FixedVectorType>(VT)->getNumElements();
  if (ST->getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS &&
      DL.getTypeSizeInBits(VT->getElementType()) == 16) {
    bool HasVOP3P = ST->hasVOP3PInsts();
    unsigned RequestedElts =
        count_if(Mask, [](int MaskElt) { return MaskElt != -1; });
    if (RequestedElts == 0)
      return 0;
    switch (Kind) {
    case TTI::SK_Broadcast:
    case TTI::SK_Reverse:
    case TTI::SK_PermuteSingleSrc: {
      // With op_sel VOP3P instructions freely can access the low half or high
      // half of a register, so any swizzle of two elements is free.
      if (HasVOP3P && NumVectorElts == 2)
        return 0;
      unsigned NumPerms = alignTo(RequestedElts, 2) / 2;
      // SK_Broadcast just reuses the same mask
      unsigned NumPermMasks = Kind == TTI::SK_Broadcast ? 1 : NumPerms;
      return NumPerms + NumPermMasks;
    }
    case TTI::SK_ExtractSubvector:
    case TTI::SK_InsertSubvector: {
      // Even aligned accesses are free
      if (!(Index % 2))
        return 0;
      // Insert/extract subvectors only require shifts / extract code to get the
      // relevant bits
      return alignTo(RequestedElts, 2) / 2;
    }
    case TTI::SK_PermuteTwoSrc:
    case TTI::SK_Splice:
    case TTI::SK_Select: {
      unsigned NumPerms = alignTo(RequestedElts, 2) / 2;
      // SK_Select just reuses the same mask
      unsigned NumPermMasks = Kind == TTI::SK_Select ? 1 : NumPerms;
      return NumPerms + NumPermMasks;
    }

    default:
      break;
    }
  }

  return BaseT::getShuffleCost(Kind, VT, Mask, CostKind, Index, SubTp);
}

bool GCNTTIImpl::areInlineCompatible(const Function *Caller,
                                     const Function *Callee) const {
  const TargetMachine &TM = getTLI()->getTargetMachine();
  const GCNSubtarget *CallerST
    = static_cast<const GCNSubtarget *>(TM.getSubtargetImpl(*Caller));
  const GCNSubtarget *CalleeST
    = static_cast<const GCNSubtarget *>(TM.getSubtargetImpl(*Callee));

  const FeatureBitset &CallerBits = CallerST->getFeatureBits();
  const FeatureBitset &CalleeBits = CalleeST->getFeatureBits();

  FeatureBitset RealCallerBits = CallerBits & ~InlineFeatureIgnoreList;
  FeatureBitset RealCalleeBits = CalleeBits & ~InlineFeatureIgnoreList;
  if ((RealCallerBits & RealCalleeBits) != RealCalleeBits)
    return false;

  // FIXME: dx10_clamp can just take the caller setting, but there seems to be
  // no way to support merge for backend defined attributes.
  SIModeRegisterDefaults CallerMode(*Caller, *CallerST);
  SIModeRegisterDefaults CalleeMode(*Callee, *CalleeST);
  if (!CallerMode.isInlineCompatible(CalleeMode))
    return false;

  if (Callee->hasFnAttribute(Attribute::AlwaysInline) ||
      Callee->hasFnAttribute(Attribute::InlineHint))
    return true;

  // Hack to make compile times reasonable.
  if (InlineMaxBB) {
    // Single BB does not increase total BB amount.
    if (Callee->size() == 1)
      return true;
    size_t BBSize = Caller->size() + Callee->size() - 1;
    return BBSize <= InlineMaxBB;
  }

  return true;
}

static unsigned adjustInliningThresholdUsingCallee(const CallBase *CB,
                                                   const SITargetLowering *TLI,
                                                   const GCNTTIImpl *TTIImpl) {
  const int NrOfSGPRUntilSpill = 26;
  const int NrOfVGPRUntilSpill = 32;

  const DataLayout &DL = TTIImpl->getDataLayout();

  unsigned adjustThreshold = 0;
  int SGPRsInUse = 0;
  int VGPRsInUse = 0;
  for (const Use &A : CB->args()) {
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(*TLI, DL, A.get()->getType(), ValueVTs);
    for (auto ArgVT : ValueVTs) {
      unsigned CCRegNum = TLI->getNumRegistersForCallingConv(
          CB->getContext(), CB->getCallingConv(), ArgVT);
      if (AMDGPU::isArgPassedInSGPR(CB, CB->getArgOperandNo(&A)))
        SGPRsInUse += CCRegNum;
      else
        VGPRsInUse += CCRegNum;
    }
  }

  // The cost of passing function arguments through the stack:
  //  1 instruction to put a function argument on the stack in the caller.
  //  1 instruction to take a function argument from the stack in callee.
  //  1 instruction is explicitly take care of data dependencies in callee
  //  function.
  InstructionCost ArgStackCost(1);
  ArgStackCost += const_cast<GCNTTIImpl *>(TTIImpl)->getMemoryOpCost(
      Instruction::Store, Type::getInt32Ty(CB->getContext()), Align(4),
      AMDGPUAS::PRIVATE_ADDRESS, TTI::TCK_SizeAndLatency);
  ArgStackCost += const_cast<GCNTTIImpl *>(TTIImpl)->getMemoryOpCost(
      Instruction::Load, Type::getInt32Ty(CB->getContext()), Align(4),
      AMDGPUAS::PRIVATE_ADDRESS, TTI::TCK_SizeAndLatency);

  // The penalty cost is computed relative to the cost of instructions and does
  // not model any storage costs.
  adjustThreshold += std::max(0, SGPRsInUse - NrOfSGPRUntilSpill) *
                     *ArgStackCost.getValue() * InlineConstants::getInstrCost();
  adjustThreshold += std::max(0, VGPRsInUse - NrOfVGPRUntilSpill) *
                     *ArgStackCost.getValue() * InlineConstants::getInstrCost();
  return adjustThreshold;
}

static unsigned getCallArgsTotalAllocaSize(const CallBase *CB,
                                           const DataLayout &DL) {
  // If we have a pointer to a private array passed into a function
  // it will not be optimized out, leaving scratch usage.
  // This function calculates the total size in bytes of the memory that would
  // end in scratch if the call was not inlined.
  unsigned AllocaSize = 0;
  SmallPtrSet<const AllocaInst *, 8> AIVisited;
  for (Value *PtrArg : CB->args()) {
    PointerType *Ty = dyn_cast<PointerType>(PtrArg->getType());
    if (!Ty)
      continue;

    unsigned AddrSpace = Ty->getAddressSpace();
    if (AddrSpace != AMDGPUAS::FLAT_ADDRESS &&
        AddrSpace != AMDGPUAS::PRIVATE_ADDRESS)
      continue;

    const AllocaInst *AI = dyn_cast<AllocaInst>(getUnderlyingObject(PtrArg));
    if (!AI || !AI->isStaticAlloca() || !AIVisited.insert(AI).second)
      continue;

    AllocaSize += DL.getTypeAllocSize(AI->getAllocatedType());
  }
  return AllocaSize;
}

unsigned GCNTTIImpl::adjustInliningThreshold(const CallBase *CB) const {
  unsigned Threshold = adjustInliningThresholdUsingCallee(CB, TLI, this);

  // Private object passed as arguments may end up in scratch usage if the call
  // is not inlined. Increase the inline threshold to promote inlining.
  unsigned AllocaSize = getCallArgsTotalAllocaSize(CB, DL);
  if (AllocaSize > 0)
    Threshold += ArgAllocaCost;
  return Threshold;
}

unsigned GCNTTIImpl::getCallerAllocaCost(const CallBase *CB,
                                         const AllocaInst *AI) const {

  // Below the cutoff, assume that the private memory objects would be
  // optimized
  auto AllocaSize = getCallArgsTotalAllocaSize(CB, DL);
  if (AllocaSize <= ArgAllocaCutoff)
    return 0;

  // Above the cutoff, we give a cost to each private memory object
  // depending its size. If the array can be optimized by SROA this cost is not
  // added to the total-cost in the inliner cost analysis.
  //
  // We choose the total cost of the alloca such that their sum cancels the
  // bonus given in the threshold (ArgAllocaCost).
  //
  //   Cost_Alloca_0 + ... + Cost_Alloca_N == ArgAllocaCost
  //
  // Awkwardly, the ArgAllocaCost bonus is multiplied by threshold-multiplier,
  // the single-bb bonus and the vector-bonus.
  //
  // We compensate the first two multipliers, by repeating logic from the
  // inliner-cost in here. The vector-bonus is 0 on AMDGPU.
  static_assert(InlinerVectorBonusPercent == 0, "vector bonus assumed to be 0");
  unsigned Threshold = ArgAllocaCost * getInliningThresholdMultiplier();

  bool SingleBB = none_of(*CB->getCalledFunction(), [](const BasicBlock &BB) {
    return BB.getTerminator()->getNumSuccessors() > 1;
  });
  if (SingleBB) {
    Threshold += Threshold / 2;
  }

  auto ArgAllocaSize = DL.getTypeAllocSize(AI->getAllocatedType());

  // Attribute the bonus proportionally to the alloca size
  unsigned AllocaThresholdBonus = (Threshold * ArgAllocaSize) / AllocaSize;

  return AllocaThresholdBonus;
}

void GCNTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                         TTI::UnrollingPreferences &UP,
                                         OptimizationRemarkEmitter *ORE) {
  CommonTTI.getUnrollingPreferences(L, SE, UP, ORE);
}

void GCNTTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                       TTI::PeelingPreferences &PP) {
  CommonTTI.getPeelingPreferences(L, SE, PP);
}

int GCNTTIImpl::get64BitInstrCost(TTI::TargetCostKind CostKind) const {
  return ST->hasFullRate64Ops()
             ? getFullRateInstrCost()
             : ST->hasHalfRate64Ops() ? getHalfRateInstrCost(CostKind)
                                      : getQuarterRateInstrCost(CostKind);
}

std::pair<InstructionCost, MVT>
GCNTTIImpl::getTypeLegalizationCost(Type *Ty) const {
  std::pair<InstructionCost, MVT> Cost = BaseT::getTypeLegalizationCost(Ty);
  auto Size = DL.getTypeSizeInBits(Ty);
  // Maximum load or store can handle 8 dwords for scalar and 4 for
  // vector ALU. Let's assume anything above 8 dwords is expensive
  // even if legal.
  if (Size <= 256)
    return Cost;

  Cost.first += (Size + 255) / 256;
  return Cost;
}

unsigned GCNTTIImpl::getPrefetchDistance() const {
  return ST->hasPrefetch() ? 128 : 0;
}

bool GCNTTIImpl::shouldPrefetchAddressSpace(unsigned AS) const {
  return AMDGPU::isFlatGlobalAddrSpace(AS);
}
