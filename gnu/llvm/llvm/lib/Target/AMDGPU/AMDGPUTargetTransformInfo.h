//===- AMDGPUTargetTransformInfo.h - AMDGPU specific TTI --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// AMDGPU target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETTRANSFORMINFO_H

#include "AMDGPU.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include <optional>

namespace llvm {

class AMDGPUTargetMachine;
class GCNSubtarget;
class InstCombiner;
class Loop;
class ScalarEvolution;
class SITargetLowering;
class Type;
class Value;

class AMDGPUTTIImpl final : public BasicTTIImplBase<AMDGPUTTIImpl> {
  using BaseT = BasicTTIImplBase<AMDGPUTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  Triple TargetTriple;

  const TargetSubtargetInfo *ST;
  const TargetLoweringBase *TLI;

  const TargetSubtargetInfo *getST() const { return ST; }
  const TargetLoweringBase *getTLI() const { return TLI; }

public:
  explicit AMDGPUTTIImpl(const AMDGPUTargetMachine *TM, const Function &F);

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  int64_t getMaxMemIntrinsicInlineSizeThreshold() const;
};

class GCNTTIImpl final : public BasicTTIImplBase<GCNTTIImpl> {
  using BaseT = BasicTTIImplBase<GCNTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const GCNSubtarget *ST;
  const SITargetLowering *TLI;
  AMDGPUTTIImpl CommonTTI;
  bool IsGraphics;
  bool HasFP32Denormals;
  bool HasFP64FP16Denormals;
  static constexpr bool InlinerVectorBonusPercent = 0;

  static const FeatureBitset InlineFeatureIgnoreList;

  const GCNSubtarget *getST() const { return ST; }
  const SITargetLowering *getTLI() const { return TLI; }

  static inline int getFullRateInstrCost() {
    return TargetTransformInfo::TCC_Basic;
  }

  static inline int getHalfRateInstrCost(TTI::TargetCostKind CostKind) {
    return CostKind == TTI::TCK_CodeSize ? 2
                                         : 2 * TargetTransformInfo::TCC_Basic;
  }

  // TODO: The size is usually 8 bytes, but takes 4x as many cycles. Maybe
  // should be 2 or 4.
  static inline int getQuarterRateInstrCost(TTI::TargetCostKind CostKind) {
    return CostKind == TTI::TCK_CodeSize ? 2
                                         : 4 * TargetTransformInfo::TCC_Basic;
  }

  // On some parts, normal fp64 operations are half rate, and others
  // quarter. This also applies to some integer operations.
  int get64BitInstrCost(TTI::TargetCostKind CostKind) const;

  std::pair<InstructionCost, MVT> getTypeLegalizationCost(Type *Ty) const;

public:
  explicit GCNTTIImpl(const AMDGPUTargetMachine *TM, const Function &F);

  bool hasBranchDivergence(const Function *F = nullptr) const;

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  TTI::PopcntSupportKind getPopcntSupport(unsigned TyWidth) {
    assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
    return TTI::PSK_FastHardware;
  }

  unsigned getNumberOfRegisters(unsigned RCID) const;
  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind Vector) const;
  unsigned getMinVectorRegisterBitWidth() const;
  unsigned getMaximumVF(unsigned ElemWidth, unsigned Opcode) const;
  unsigned getLoadVectorFactor(unsigned VF, unsigned LoadSize,
                               unsigned ChainSizeInBytes,
                               VectorType *VecTy) const;
  unsigned getStoreVectorFactor(unsigned VF, unsigned StoreSize,
                                unsigned ChainSizeInBytes,
                                VectorType *VecTy) const;
  unsigned getLoadStoreVecRegBitWidth(unsigned AddrSpace) const;

  bool isLegalToVectorizeMemChain(unsigned ChainSizeInBytes, Align Alignment,
                                  unsigned AddrSpace) const;
  bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes, Align Alignment,
                                   unsigned AddrSpace) const;
  bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes, Align Alignment,
                                    unsigned AddrSpace) const;

  int64_t getMaxMemIntrinsicInlineSizeThreshold() const;
  Type *getMemcpyLoopLoweringType(
      LLVMContext & Context, Value * Length, unsigned SrcAddrSpace,
      unsigned DestAddrSpace, unsigned SrcAlign, unsigned DestAlign,
      std::optional<uint32_t> AtomicElementSize) const;

  void getMemcpyLoopResidualLoweringType(
      SmallVectorImpl<Type *> &OpsOut, LLVMContext &Context,
      unsigned RemainingBytes, unsigned SrcAddrSpace, unsigned DestAddrSpace,
      unsigned SrcAlign, unsigned DestAlign,
      std::optional<uint32_t> AtomicCpySize) const;
  unsigned getMaxInterleaveFactor(ElementCount VF);

  bool getTgtMemIntrinsic(IntrinsicInst *Inst, MemIntrinsicInfo &Info) const;

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr);

  InstructionCost getCFInstrCost(unsigned Opcode, TTI::TargetCostKind CostKind,
                                 const Instruction *I = nullptr);

  bool isInlineAsmSourceOfDivergence(const CallInst *CI,
                                     ArrayRef<unsigned> Indices = {}) const;

  using BaseT::getVectorInstrCost;
  InstructionCost getVectorInstrCost(unsigned Opcode, Type *ValTy,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index, Value *Op0, Value *Op1);

  bool isReadRegisterSourceOfDivergence(const IntrinsicInst *ReadReg) const;
  bool isSourceOfDivergence(const Value *V) const;
  bool isAlwaysUniform(const Value *V) const;

  bool isValidAddrSpaceCast(unsigned FromAS, unsigned ToAS) const {
    if (ToAS == AMDGPUAS::FLAT_ADDRESS) {
      switch (FromAS) {
      case AMDGPUAS::GLOBAL_ADDRESS:
      case AMDGPUAS::CONSTANT_ADDRESS:
      case AMDGPUAS::CONSTANT_ADDRESS_32BIT:
      case AMDGPUAS::LOCAL_ADDRESS:
      case AMDGPUAS::PRIVATE_ADDRESS:
        return true;
      default:
        break;
      }
      return false;
    }
    if ((FromAS == AMDGPUAS::CONSTANT_ADDRESS_32BIT &&
         ToAS == AMDGPUAS::CONSTANT_ADDRESS) ||
        (FromAS == AMDGPUAS::CONSTANT_ADDRESS &&
         ToAS == AMDGPUAS::CONSTANT_ADDRESS_32BIT))
      return true;
    return false;
  }

  bool addrspacesMayAlias(unsigned AS0, unsigned AS1) const {
    return AMDGPU::addrspacesMayAlias(AS0, AS1);
  }

  unsigned getFlatAddressSpace() const {
    // Don't bother running InferAddressSpaces pass on graphics shaders which
    // don't use flat addressing.
    if (IsGraphics)
      return -1;
    return AMDGPUAS::FLAT_ADDRESS;
  }

  bool collectFlatAddressOperands(SmallVectorImpl<int> &OpIndexes,
                                  Intrinsic::ID IID) const;

  bool canHaveNonUndefGlobalInitializerInAddressSpace(unsigned AS) const {
    return AS != AMDGPUAS::LOCAL_ADDRESS && AS != AMDGPUAS::REGION_ADDRESS &&
           AS != AMDGPUAS::PRIVATE_ADDRESS;
  }

  Value *rewriteIntrinsicWithAddressSpace(IntrinsicInst *II, Value *OldV,
                                          Value *NewV) const;

  bool canSimplifyLegacyMulToMul(const Instruction &I, const Value *Op0,
                                 const Value *Op1, InstCombiner &IC) const;
  std::optional<Instruction *> instCombineIntrinsic(InstCombiner &IC,
                                                    IntrinsicInst &II) const;
  std::optional<Value *> simplifyDemandedVectorEltsIntrinsic(
      InstCombiner &IC, IntrinsicInst &II, APInt DemandedElts, APInt &UndefElts,
      APInt &UndefElts2, APInt &UndefElts3,
      std::function<void(Instruction *, unsigned, APInt, APInt &)>
          SimplifyAndSetOp) const;

  InstructionCost getVectorSplitCost() { return 0; }

  InstructionCost getShuffleCost(TTI::ShuffleKind Kind, VectorType *Tp,
                                 ArrayRef<int> Mask,
                                 TTI::TargetCostKind CostKind, int Index,
                                 VectorType *SubTp,
                                 ArrayRef<const Value *> Args = std::nullopt,
                                 const Instruction *CxtI = nullptr);

  bool areInlineCompatible(const Function *Caller,
                           const Function *Callee) const;

  unsigned getInliningThresholdMultiplier() const { return 11; }
  unsigned adjustInliningThreshold(const CallBase *CB) const;
  unsigned getCallerAllocaCost(const CallBase *CB, const AllocaInst *AI) const;

  int getInlinerVectorBonusPercent() const { return InlinerVectorBonusPercent; }

  InstructionCost getArithmeticReductionCost(
      unsigned Opcode, VectorType *Ty, std::optional<FastMathFlags> FMF,
      TTI::TargetCostKind CostKind);

  InstructionCost getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                        TTI::TargetCostKind CostKind);
  InstructionCost getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                         FastMathFlags FMF,
                                         TTI::TargetCostKind CostKind);

  /// Data cache line size for LoopDataPrefetch pass. Has no use before GFX12.
  unsigned getCacheLineSize() const override { return 128; }

  /// How much before a load we should place the prefetch instruction.
  /// This is currently measured in number of IR instructions.
  unsigned getPrefetchDistance() const override;

  /// \return if target want to issue a prefetch in address space \p AS.
  bool shouldPrefetchAddressSpace(unsigned AS) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETTRANSFORMINFO_H
