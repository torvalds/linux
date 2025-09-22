//===-- X86TargetTransformInfo.cpp - X86 specific TTI pass ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements a TargetTransformInfo analysis pass specific to the
/// X86 target machine. It uses the target's detailed information to provide
/// more precise answers to certain TTI queries, while letting the target
/// independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//
/// About Cost Model numbers used below it's necessary to say the following:
/// the numbers correspond to some "generic" X86 CPU instead of usage of a
/// specific CPU model. Usually the numbers correspond to the CPU where the
/// feature first appeared. For example, if we do Subtarget.hasSSE42() in
/// the lookups below the cost is based on Nehalem as that was the first CPU
/// to support that feature level and thus has most likely the worst case cost,
/// although we may discard an outlying worst cost from one CPU (e.g. Atom).
///
/// Some examples of other technologies/CPUs:
///   SSE 3   - Pentium4 / Athlon64
///   SSE 4.1 - Penryn
///   SSE 4.2 - Nehalem / Silvermont
///   AVX     - Sandy Bridge / Jaguar / Bulldozer
///   AVX2    - Haswell / Ryzen
///   AVX-512 - Xeon Phi / Skylake
///
/// And some examples of instruction target dependent costs (latency)
///                   divss     sqrtss          rsqrtss
///   AMD K7          11-16     19              3
///   Piledriver      9-24      13-15           5
///   Jaguar          14        16              2
///   Pentium II,III  18        30              2
///   Nehalem         7-14      7-18            3
///   Haswell         10-13     11              5
///
/// Interpreting the 4 TargetCostKind types:
/// TCK_RecipThroughput and TCK_Latency should try to match the worst case
/// values reported by the CPU scheduler models (and llvm-mca).
/// TCK_CodeSize should match the instruction count (e.g. divss = 1), NOT the
/// actual encoding size of the instruction.
/// TCK_SizeAndLatency should match the worst case micro-op counts reported by
/// by the CPU scheduler models (and llvm-mca), to ensure that they are
/// compatible with the MicroOpBufferSize and LoopMicroOpBufferSize values which are
/// often used as the cost thresholds where TCK_SizeAndLatency is requested.
//===----------------------------------------------------------------------===//

#include "X86TargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "x86tti"

//===----------------------------------------------------------------------===//
//
// X86 cost model.
//
//===----------------------------------------------------------------------===//

// Helper struct to store/access costs for each cost kind.
// TODO: Move this to allow other targets to use it?
struct CostKindCosts {
  unsigned RecipThroughputCost = ~0U;
  unsigned LatencyCost = ~0U;
  unsigned CodeSizeCost = ~0U;
  unsigned SizeAndLatencyCost = ~0U;

  std::optional<unsigned>
  operator[](TargetTransformInfo::TargetCostKind Kind) const {
    unsigned Cost = ~0U;
    switch (Kind) {
    case TargetTransformInfo::TCK_RecipThroughput:
      Cost = RecipThroughputCost;
      break;
    case TargetTransformInfo::TCK_Latency:
      Cost = LatencyCost;
      break;
    case TargetTransformInfo::TCK_CodeSize:
      Cost = CodeSizeCost;
      break;
    case TargetTransformInfo::TCK_SizeAndLatency:
      Cost = SizeAndLatencyCost;
      break;
    }
    if (Cost == ~0U)
      return std::nullopt;
    return Cost;
  }
};
using CostKindTblEntry = CostTblEntryT<CostKindCosts>;
using TypeConversionCostKindTblEntry = TypeConversionCostTblEntryT<CostKindCosts>;

TargetTransformInfo::PopcntSupportKind
X86TTIImpl::getPopcntSupport(unsigned TyWidth) {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  // TODO: Currently the __builtin_popcount() implementation using SSE3
  //   instructions is inefficient. Once the problem is fixed, we should
  //   call ST->hasSSE3() instead of ST->hasPOPCNT().
  return ST->hasPOPCNT() ? TTI::PSK_FastHardware : TTI::PSK_Software;
}

std::optional<unsigned> X86TTIImpl::getCacheSize(
  TargetTransformInfo::CacheLevel Level) const {
  switch (Level) {
  case TargetTransformInfo::CacheLevel::L1D:
    //   - Penryn
    //   - Nehalem
    //   - Westmere
    //   - Sandy Bridge
    //   - Ivy Bridge
    //   - Haswell
    //   - Broadwell
    //   - Skylake
    //   - Kabylake
    return 32 * 1024;  //  32 KByte
  case TargetTransformInfo::CacheLevel::L2D:
    //   - Penryn
    //   - Nehalem
    //   - Westmere
    //   - Sandy Bridge
    //   - Ivy Bridge
    //   - Haswell
    //   - Broadwell
    //   - Skylake
    //   - Kabylake
    return 256 * 1024; // 256 KByte
  }

  llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
}

std::optional<unsigned> X86TTIImpl::getCacheAssociativity(
  TargetTransformInfo::CacheLevel Level) const {
  //   - Penryn
  //   - Nehalem
  //   - Westmere
  //   - Sandy Bridge
  //   - Ivy Bridge
  //   - Haswell
  //   - Broadwell
  //   - Skylake
  //   - Kabylake
  switch (Level) {
  case TargetTransformInfo::CacheLevel::L1D:
    [[fallthrough]];
  case TargetTransformInfo::CacheLevel::L2D:
    return 8;
  }

  llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
}

unsigned X86TTIImpl::getNumberOfRegisters(unsigned ClassID) const {
  bool Vector = (ClassID == 1);
  if (Vector && !ST->hasSSE1())
    return 0;

  if (ST->is64Bit()) {
    if (Vector && ST->hasAVX512())
      return 32;
    if (!Vector && ST->hasEGPR())
      return 32;
    return 16;
  }
  return 8;
}

bool X86TTIImpl::hasConditionalLoadStoreForType(Type *Ty) const {
  if (!ST->hasCF())
    return false;
  if (!Ty)
    return true;
  // Conditional faulting is supported by CFCMOV, which only accepts
  // 16/32/64-bit operands.
  // TODO: Support f32/f64 with VMOVSS/VMOVSD with zero mask when it's
  // profitable.
  auto *VTy = dyn_cast<FixedVectorType>(Ty);
  if (!Ty->isIntegerTy() && (!VTy || VTy->getNumElements() != 1))
    return false;
  auto *ScalarTy = Ty->getScalarType();
  switch (cast<IntegerType>(ScalarTy)->getBitWidth()) {
  default:
    return false;
  case 16:
  case 32:
  case 64:
    return true;
  }
}

TypeSize
X86TTIImpl::getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
  unsigned PreferVectorWidth = ST->getPreferVectorWidth();
  switch (K) {
  case TargetTransformInfo::RGK_Scalar:
    return TypeSize::getFixed(ST->is64Bit() ? 64 : 32);
  case TargetTransformInfo::RGK_FixedWidthVector:
    if (ST->hasAVX512() && ST->hasEVEX512() && PreferVectorWidth >= 512)
      return TypeSize::getFixed(512);
    if (ST->hasAVX() && PreferVectorWidth >= 256)
      return TypeSize::getFixed(256);
    if (ST->hasSSE1() && PreferVectorWidth >= 128)
      return TypeSize::getFixed(128);
    return TypeSize::getFixed(0);
  case TargetTransformInfo::RGK_ScalableVector:
    return TypeSize::getScalable(0);
  }

  llvm_unreachable("Unsupported register kind");
}

unsigned X86TTIImpl::getLoadStoreVecRegBitWidth(unsigned) const {
  return getRegisterBitWidth(TargetTransformInfo::RGK_FixedWidthVector)
      .getFixedValue();
}

unsigned X86TTIImpl::getMaxInterleaveFactor(ElementCount VF) {
  // If the loop will not be vectorized, don't interleave the loop.
  // Let regular unroll to unroll the loop, which saves the overflow
  // check and memory check cost.
  if (VF.isScalar())
    return 1;

  if (ST->isAtom())
    return 1;

  // Sandybridge and Haswell have multiple execution ports and pipelined
  // vector units.
  if (ST->hasAVX())
    return 4;

  return 2;
}

InstructionCost X86TTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {

  // vXi8 multiplications are always promoted to vXi16.
  // Sub-128-bit types can be extended/packed more efficiently.
  if (Opcode == Instruction::Mul && Ty->isVectorTy() &&
      Ty->getPrimitiveSizeInBits() <= 64 && Ty->getScalarSizeInBits() == 8) {
    Type *WideVecTy =
        VectorType::getExtendedElementVectorType(cast<VectorType>(Ty));
    return getCastInstrCost(Instruction::ZExt, WideVecTy, Ty,
                            TargetTransformInfo::CastContextHint::None,
                            CostKind) +
           getCastInstrCost(Instruction::Trunc, Ty, WideVecTy,
                            TargetTransformInfo::CastContextHint::None,
                            CostKind) +
           getArithmeticInstrCost(Opcode, WideVecTy, CostKind, Op1Info, Op2Info);
  }

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  if (ISD == ISD::MUL && Args.size() == 2 && LT.second.isVector() &&
      (LT.second.getScalarType() == MVT::i32 ||
       LT.second.getScalarType() == MVT::i64)) {
    // Check if the operands can be represented as a smaller datatype.
    bool Op1Signed = false, Op2Signed = false;
    unsigned Op1MinSize = BaseT::minRequiredElementSize(Args[0], Op1Signed);
    unsigned Op2MinSize = BaseT::minRequiredElementSize(Args[1], Op2Signed);
    unsigned OpMinSize = std::max(Op1MinSize, Op2MinSize);
    bool SignedMode = Op1Signed || Op2Signed;

    // If both vXi32 are representable as i15 and at least one is constant,
    // zero-extended, or sign-extended from vXi16 (or less pre-SSE41) then we
    // can treat this as PMADDWD which has the same costs as a vXi16 multiply.
    if (OpMinSize <= 15 && !ST->isPMADDWDSlow() &&
        LT.second.getScalarType() == MVT::i32) {
      bool Op1Constant =
          isa<ConstantDataVector>(Args[0]) || isa<ConstantVector>(Args[0]);
      bool Op2Constant =
          isa<ConstantDataVector>(Args[1]) || isa<ConstantVector>(Args[1]);
      bool Op1Sext = isa<SExtInst>(Args[0]) &&
                     (Op1MinSize == 15 || (Op1MinSize < 15 && !ST->hasSSE41()));
      bool Op2Sext = isa<SExtInst>(Args[1]) &&
                     (Op2MinSize == 15 || (Op2MinSize < 15 && !ST->hasSSE41()));

      bool IsZeroExtended = !Op1Signed || !Op2Signed;
      bool IsConstant = Op1Constant || Op2Constant;
      bool IsSext = Op1Sext || Op2Sext;
      if (IsConstant || IsZeroExtended || IsSext)
        LT.second =
            MVT::getVectorVT(MVT::i16, 2 * LT.second.getVectorNumElements());
    }

    // Check if the vXi32 operands can be shrunk into a smaller datatype.
    // This should match the codegen from reduceVMULWidth.
    // TODO: Make this generic (!ST->SSE41 || ST->isPMULLDSlow()).
    if (ST->useSLMArithCosts() && LT.second == MVT::v4i32) {
      if (OpMinSize <= 7)
        return LT.first * 3; // pmullw/sext
      if (!SignedMode && OpMinSize <= 8)
        return LT.first * 3; // pmullw/zext
      if (OpMinSize <= 15)
        return LT.first * 5; // pmullw/pmulhw/pshuf
      if (!SignedMode && OpMinSize <= 16)
        return LT.first * 5; // pmullw/pmulhw/pshuf
    }

    // If both vXi64 are representable as (unsigned) i32, then we can perform
    // the multiple with a single PMULUDQ instruction.
    // TODO: Add (SSE41+) PMULDQ handling for signed extensions.
    if (!SignedMode && OpMinSize <= 32 && LT.second.getScalarType() == MVT::i64)
      ISD = X86ISD::PMULUDQ;
  }

  // Vector multiply by pow2 will be simplified to shifts.
  // Vector multiply by -pow2 will be simplified to shifts/negates.
  if (ISD == ISD::MUL && Op2Info.isConstant() &&
      (Op2Info.isPowerOf2() || Op2Info.isNegatedPowerOf2())) {
    InstructionCost Cost =
        getArithmeticInstrCost(Instruction::Shl, Ty, CostKind,
                               Op1Info.getNoProps(), Op2Info.getNoProps());
    if (Op2Info.isNegatedPowerOf2())
      Cost += getArithmeticInstrCost(Instruction::Sub, Ty, CostKind);
    return Cost;
  }

  // On X86, vector signed division by constants power-of-two are
  // normally expanded to the sequence SRA + SRL + ADD + SRA.
  // The OperandValue properties may not be the same as that of the previous
  // operation; conservatively assume OP_None.
  if ((ISD == ISD::SDIV || ISD == ISD::SREM) &&
      Op2Info.isConstant() && Op2Info.isPowerOf2()) {
    InstructionCost Cost =
        2 * getArithmeticInstrCost(Instruction::AShr, Ty, CostKind,
                                   Op1Info.getNoProps(), Op2Info.getNoProps());
    Cost += getArithmeticInstrCost(Instruction::LShr, Ty, CostKind,
                                   Op1Info.getNoProps(), Op2Info.getNoProps());
    Cost += getArithmeticInstrCost(Instruction::Add, Ty, CostKind,
                                   Op1Info.getNoProps(), Op2Info.getNoProps());

    if (ISD == ISD::SREM) {
      // For SREM: (X % C) is the equivalent of (X - (X/C)*C)
      Cost += getArithmeticInstrCost(Instruction::Mul, Ty, CostKind, Op1Info.getNoProps(),
                                     Op2Info.getNoProps());
      Cost += getArithmeticInstrCost(Instruction::Sub, Ty, CostKind, Op1Info.getNoProps(),
                                     Op2Info.getNoProps());
    }

    return Cost;
  }

  // Vector unsigned division/remainder will be simplified to shifts/masks.
  if ((ISD == ISD::UDIV || ISD == ISD::UREM) &&
      Op2Info.isConstant() && Op2Info.isPowerOf2()) {
    if (ISD == ISD::UDIV)
      return getArithmeticInstrCost(Instruction::LShr, Ty, CostKind,
                                    Op1Info.getNoProps(), Op2Info.getNoProps());
    // UREM
    return getArithmeticInstrCost(Instruction::And, Ty, CostKind,
                                  Op1Info.getNoProps(), Op2Info.getNoProps());
  }

  static const CostKindTblEntry GFNIUniformConstCostTable[] = {
    { ISD::SHL,  MVT::v16i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SRL,  MVT::v16i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SRA,  MVT::v16i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SHL,  MVT::v32i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SRL,  MVT::v32i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SRA,  MVT::v32i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SHL,  MVT::v64i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SRL,  MVT::v64i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
    { ISD::SRA,  MVT::v64i8,  { 1, 6, 1, 2 } }, // gf2p8affineqb
  };

  if (Op2Info.isUniform() && Op2Info.isConstant() && ST->hasGFNI())
    if (const auto *Entry =
            CostTableLookup(GFNIUniformConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512BWUniformConstCostTable[] = {
    { ISD::SHL,  MVT::v16i8,  { 1, 7, 2, 3 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8,  { 1, 7, 2, 3 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8,  { 1, 8, 4, 5 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v32i8,  { 1, 8, 2, 3 } }, // psllw + pand.
    { ISD::SRL,  MVT::v32i8,  { 1, 8, 2, 3 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v32i8,  { 1, 9, 4, 5 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v64i8,  { 1, 8, 2, 3 } }, // psllw + pand.
    { ISD::SRL,  MVT::v64i8,  { 1, 8, 2, 3 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v64i8,  { 1, 9, 4, 6 } }, // psrlw, pand, pxor, psubb.

    { ISD::SHL,  MVT::v16i16, { 1, 1, 1, 1 } }, // psllw
    { ISD::SRL,  MVT::v16i16, { 1, 1, 1, 1 } }, // psrlw
    { ISD::SRA,  MVT::v16i16, { 1, 1, 1, 1 } }, // psrlw
    { ISD::SHL,  MVT::v32i16, { 1, 1, 1, 1 } }, // psllw
    { ISD::SRL,  MVT::v32i16, { 1, 1, 1, 1 } }, // psrlw
    { ISD::SRA,  MVT::v32i16, { 1, 1, 1, 1 } }, // psrlw
  };

  if (Op2Info.isUniform() && Op2Info.isConstant() && ST->hasBWI())
    if (const auto *Entry =
            CostTableLookup(AVX512BWUniformConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512UniformConstCostTable[] = {
    { ISD::SHL,  MVT::v64i8,  {  2, 12,  5,  6 } }, // psllw + pand.
    { ISD::SRL,  MVT::v64i8,  {  2, 12,  5,  6 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v64i8,  {  3, 10, 12, 12 } }, // psrlw, pand, pxor, psubb.

    { ISD::SHL,  MVT::v16i16, {  2,  7,  4,  4 } }, // psllw + split.
    { ISD::SRL,  MVT::v16i16, {  2,  7,  4,  4 } }, // psrlw + split.
    { ISD::SRA,  MVT::v16i16, {  2,  7,  4,  4 } }, // psraw + split.

    { ISD::SHL,  MVT::v8i32,  {  1,  1,  1,  1 } }, // pslld
    { ISD::SRL,  MVT::v8i32,  {  1,  1,  1,  1 } }, // psrld
    { ISD::SRA,  MVT::v8i32,  {  1,  1,  1,  1 } }, // psrad
    { ISD::SHL,  MVT::v16i32, {  1,  1,  1,  1 } }, // pslld
    { ISD::SRL,  MVT::v16i32, {  1,  1,  1,  1 } }, // psrld
    { ISD::SRA,  MVT::v16i32, {  1,  1,  1,  1 } }, // psrad

    { ISD::SRA,  MVT::v2i64,  {  1,  1,  1,  1 } }, // psraq
    { ISD::SHL,  MVT::v4i64,  {  1,  1,  1,  1 } }, // psllq
    { ISD::SRL,  MVT::v4i64,  {  1,  1,  1,  1 } }, // psrlq
    { ISD::SRA,  MVT::v4i64,  {  1,  1,  1,  1 } }, // psraq
    { ISD::SHL,  MVT::v8i64,  {  1,  1,  1,  1 } }, // psllq
    { ISD::SRL,  MVT::v8i64,  {  1,  1,  1,  1 } }, // psrlq
    { ISD::SRA,  MVT::v8i64,  {  1,  1,  1,  1 } }, // psraq

    { ISD::SDIV, MVT::v16i32, {  6 } }, // pmuludq sequence
    { ISD::SREM, MVT::v16i32, {  8 } }, // pmuludq+mul+sub sequence
    { ISD::UDIV, MVT::v16i32, {  5 } }, // pmuludq sequence
    { ISD::UREM, MVT::v16i32, {  7 } }, // pmuludq+mul+sub sequence
  };

  if (Op2Info.isUniform() && Op2Info.isConstant() && ST->hasAVX512())
    if (const auto *Entry =
            CostTableLookup(AVX512UniformConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX2UniformConstCostTable[] = {
    { ISD::SHL,  MVT::v16i8, {  1,  8,  2,  3 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8, {  1,  8,  2,  3 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8, {  2, 10,  5,  6 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v32i8, {  2,  8,  2,  4 } }, // psllw + pand.
    { ISD::SRL,  MVT::v32i8, {  2,  8,  2,  4 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v32i8, {  3, 10,  5,  9 } }, // psrlw, pand, pxor, psubb.

    { ISD::SHL,  MVT::v8i16, {  1,  1,  1,  1 } }, // psllw
    { ISD::SRL,  MVT::v8i16, {  1,  1,  1,  1 } }, // psrlw
    { ISD::SRA,  MVT::v8i16, {  1,  1,  1,  1 } }, // psraw
    { ISD::SHL,  MVT::v16i16,{  2,  2,  1,  2 } }, // psllw
    { ISD::SRL,  MVT::v16i16,{  2,  2,  1,  2 } }, // psrlw
    { ISD::SRA,  MVT::v16i16,{  2,  2,  1,  2 } }, // psraw

    { ISD::SHL,  MVT::v4i32, {  1,  1,  1,  1 } }, // pslld
    { ISD::SRL,  MVT::v4i32, {  1,  1,  1,  1 } }, // psrld
    { ISD::SRA,  MVT::v4i32, {  1,  1,  1,  1 } }, // psrad
    { ISD::SHL,  MVT::v8i32, {  2,  2,  1,  2 } }, // pslld
    { ISD::SRL,  MVT::v8i32, {  2,  2,  1,  2 } }, // psrld
    { ISD::SRA,  MVT::v8i32, {  2,  2,  1,  2 } }, // psrad

    { ISD::SHL,  MVT::v2i64, {  1,  1,  1,  1 } }, // psllq
    { ISD::SRL,  MVT::v2i64, {  1,  1,  1,  1 } }, // psrlq
    { ISD::SRA,  MVT::v2i64, {  2,  3,  3,  3 } }, // psrad + shuffle.
    { ISD::SHL,  MVT::v4i64, {  2,  2,  1,  2 } }, // psllq
    { ISD::SRL,  MVT::v4i64, {  2,  2,  1,  2 } }, // psrlq
    { ISD::SRA,  MVT::v4i64, {  4,  4,  3,  6 } }, // psrad + shuffle + split.

    { ISD::SDIV, MVT::v8i32, {  6 } }, // pmuludq sequence
    { ISD::SREM, MVT::v8i32, {  8 } }, // pmuludq+mul+sub sequence
    { ISD::UDIV, MVT::v8i32, {  5 } }, // pmuludq sequence
    { ISD::UREM, MVT::v8i32, {  7 } }, // pmuludq+mul+sub sequence
  };

  if (Op2Info.isUniform() && Op2Info.isConstant() && ST->hasAVX2())
    if (const auto *Entry =
            CostTableLookup(AVX2UniformConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVXUniformConstCostTable[] = {
    { ISD::SHL,  MVT::v16i8, {  2,  7,  2,  3 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8, {  2,  7,  2,  3 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8, {  3,  9,  5,  6 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v32i8, {  4,  7,  7,  8 } }, // 2*(psllw + pand) + split.
    { ISD::SRL,  MVT::v32i8, {  4,  7,  7,  8 } }, // 2*(psrlw + pand) + split.
    { ISD::SRA,  MVT::v32i8, {  7,  7, 12, 13 } }, // 2*(psrlw, pand, pxor, psubb) + split.

    { ISD::SHL,  MVT::v8i16, {  1,  2,  1,  1 } }, // psllw.
    { ISD::SRL,  MVT::v8i16, {  1,  2,  1,  1 } }, // psrlw.
    { ISD::SRA,  MVT::v8i16, {  1,  2,  1,  1 } }, // psraw.
    { ISD::SHL,  MVT::v16i16,{  3,  6,  4,  5 } }, // psllw + split.
    { ISD::SRL,  MVT::v16i16,{  3,  6,  4,  5 } }, // psrlw + split.
    { ISD::SRA,  MVT::v16i16,{  3,  6,  4,  5 } }, // psraw + split.

    { ISD::SHL,  MVT::v4i32, {  1,  2,  1,  1 } }, // pslld.
    { ISD::SRL,  MVT::v4i32, {  1,  2,  1,  1 } }, // psrld.
    { ISD::SRA,  MVT::v4i32, {  1,  2,  1,  1 } }, // psrad.
    { ISD::SHL,  MVT::v8i32, {  3,  6,  4,  5 } }, // pslld + split.
    { ISD::SRL,  MVT::v8i32, {  3,  6,  4,  5 } }, // psrld + split.
    { ISD::SRA,  MVT::v8i32, {  3,  6,  4,  5 } }, // psrad + split.

    { ISD::SHL,  MVT::v2i64, {  1,  2,  1,  1 } }, // psllq.
    { ISD::SRL,  MVT::v2i64, {  1,  2,  1,  1 } }, // psrlq.
    { ISD::SRA,  MVT::v2i64, {  2,  3,  3,  3 } }, // psrad + shuffle.
    { ISD::SHL,  MVT::v4i64, {  3,  6,  4,  5 } }, // 2 x psllq + split.
    { ISD::SRL,  MVT::v4i64, {  3,  6,  4,  5 } }, // 2 x psllq + split.
    { ISD::SRA,  MVT::v4i64, {  5,  7,  8,  9 } }, // 2 x psrad + shuffle + split.

    { ISD::SDIV, MVT::v8i32, { 14 } }, // 2*pmuludq sequence + split.
    { ISD::SREM, MVT::v8i32, { 18 } }, // 2*pmuludq+mul+sub sequence + split.
    { ISD::UDIV, MVT::v8i32, { 12 } }, // 2*pmuludq sequence + split.
    { ISD::UREM, MVT::v8i32, { 16 } }, // 2*pmuludq+mul+sub sequence + split.
  };

  // XOP has faster vXi8 shifts.
  if (Op2Info.isUniform() && Op2Info.isConstant() && ST->hasAVX() &&
      (!ST->hasXOP() || LT.second.getScalarSizeInBits() != 8))
    if (const auto *Entry =
            CostTableLookup(AVXUniformConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE2UniformConstCostTable[] = {
    { ISD::SHL,  MVT::v16i8, {  1,  7,  2,  3 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8, {  1,  7,  2,  3 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8, {  3,  9,  5,  6 } }, // psrlw, pand, pxor, psubb.

    { ISD::SHL,  MVT::v8i16, {  1,  1,  1,  1 } }, // psllw.
    { ISD::SRL,  MVT::v8i16, {  1,  1,  1,  1 } }, // psrlw.
    { ISD::SRA,  MVT::v8i16, {  1,  1,  1,  1 } }, // psraw.

    { ISD::SHL,  MVT::v4i32, {  1,  1,  1,  1 } }, // pslld
    { ISD::SRL,  MVT::v4i32, {  1,  1,  1,  1 } }, // psrld.
    { ISD::SRA,  MVT::v4i32, {  1,  1,  1,  1 } }, // psrad.

    { ISD::SHL,  MVT::v2i64, {  1,  1,  1,  1 } }, // psllq.
    { ISD::SRL,  MVT::v2i64, {  1,  1,  1,  1 } }, // psrlq.
    { ISD::SRA,  MVT::v2i64, {  3,  5,  6,  6 } }, // 2 x psrad + shuffle.

    { ISD::SDIV, MVT::v4i32, {  6 } }, // pmuludq sequence
    { ISD::SREM, MVT::v4i32, {  8 } }, // pmuludq+mul+sub sequence
    { ISD::UDIV, MVT::v4i32, {  5 } }, // pmuludq sequence
    { ISD::UREM, MVT::v4i32, {  7 } }, // pmuludq+mul+sub sequence
  };

  // XOP has faster vXi8 shifts.
  if (Op2Info.isUniform() && Op2Info.isConstant() && ST->hasSSE2() &&
      (!ST->hasXOP() || LT.second.getScalarSizeInBits() != 8))
    if (const auto *Entry =
            CostTableLookup(SSE2UniformConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512BWConstCostTable[] = {
    { ISD::SDIV, MVT::v64i8,  { 14 } }, // 2*ext+2*pmulhw sequence
    { ISD::SREM, MVT::v64i8,  { 16 } }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v64i8,  { 14 } }, // 2*ext+2*pmulhw sequence
    { ISD::UREM, MVT::v64i8,  { 16 } }, // 2*ext+2*pmulhw+mul+sub sequence

    { ISD::SDIV, MVT::v32i16, {  6 } }, // vpmulhw sequence
    { ISD::SREM, MVT::v32i16, {  8 } }, // vpmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v32i16, {  6 } }, // vpmulhuw sequence
    { ISD::UREM, MVT::v32i16, {  8 } }, // vpmulhuw+mul+sub sequence
  };

  if (Op2Info.isConstant() && ST->hasBWI())
    if (const auto *Entry =
            CostTableLookup(AVX512BWConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512ConstCostTable[] = {
    { ISD::SDIV, MVT::v64i8,  { 28 } }, // 4*ext+4*pmulhw sequence
    { ISD::SREM, MVT::v64i8,  { 32 } }, // 4*ext+4*pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v64i8,  { 28 } }, // 4*ext+4*pmulhw sequence
    { ISD::UREM, MVT::v64i8,  { 32 } }, // 4*ext+4*pmulhw+mul+sub sequence

    { ISD::SDIV, MVT::v32i16, { 12 } }, // 2*vpmulhw sequence
    { ISD::SREM, MVT::v32i16, { 16 } }, // 2*vpmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v32i16, { 12 } }, // 2*vpmulhuw sequence
    { ISD::UREM, MVT::v32i16, { 16 } }, // 2*vpmulhuw+mul+sub sequence

    { ISD::SDIV, MVT::v16i32, { 15 } }, // vpmuldq sequence
    { ISD::SREM, MVT::v16i32, { 17 } }, // vpmuldq+mul+sub sequence
    { ISD::UDIV, MVT::v16i32, { 15 } }, // vpmuludq sequence
    { ISD::UREM, MVT::v16i32, { 17 } }, // vpmuludq+mul+sub sequence
  };

  if (Op2Info.isConstant() && ST->hasAVX512())
    if (const auto *Entry =
            CostTableLookup(AVX512ConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX2ConstCostTable[] = {
    { ISD::SDIV, MVT::v32i8,  { 14 } }, // 2*ext+2*pmulhw sequence
    { ISD::SREM, MVT::v32i8,  { 16 } }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v32i8,  { 14 } }, // 2*ext+2*pmulhw sequence
    { ISD::UREM, MVT::v32i8,  { 16 } }, // 2*ext+2*pmulhw+mul+sub sequence

    { ISD::SDIV, MVT::v16i16, {  6 } }, // vpmulhw sequence
    { ISD::SREM, MVT::v16i16, {  8 } }, // vpmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v16i16, {  6 } }, // vpmulhuw sequence
    { ISD::UREM, MVT::v16i16, {  8 } }, // vpmulhuw+mul+sub sequence

    { ISD::SDIV, MVT::v8i32,  { 15 } }, // vpmuldq sequence
    { ISD::SREM, MVT::v8i32,  { 19 } }, // vpmuldq+mul+sub sequence
    { ISD::UDIV, MVT::v8i32,  { 15 } }, // vpmuludq sequence
    { ISD::UREM, MVT::v8i32,  { 19 } }, // vpmuludq+mul+sub sequence
  };

  if (Op2Info.isConstant() && ST->hasAVX2())
    if (const auto *Entry = CostTableLookup(AVX2ConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVXConstCostTable[] = {
    { ISD::SDIV, MVT::v32i8,  { 30 } }, // 4*ext+4*pmulhw sequence + split.
    { ISD::SREM, MVT::v32i8,  { 34 } }, // 4*ext+4*pmulhw+mul+sub sequence + split.
    { ISD::UDIV, MVT::v32i8,  { 30 } }, // 4*ext+4*pmulhw sequence + split.
    { ISD::UREM, MVT::v32i8,  { 34 } }, // 4*ext+4*pmulhw+mul+sub sequence + split.

    { ISD::SDIV, MVT::v16i16, { 14 } }, // 2*pmulhw sequence + split.
    { ISD::SREM, MVT::v16i16, { 18 } }, // 2*pmulhw+mul+sub sequence + split.
    { ISD::UDIV, MVT::v16i16, { 14 } }, // 2*pmulhuw sequence + split.
    { ISD::UREM, MVT::v16i16, { 18 } }, // 2*pmulhuw+mul+sub sequence + split.

    { ISD::SDIV, MVT::v8i32,  { 32 } }, // vpmuludq sequence
    { ISD::SREM, MVT::v8i32,  { 38 } }, // vpmuludq+mul+sub sequence
    { ISD::UDIV, MVT::v8i32,  { 32 } }, // 2*pmuludq sequence + split.
    { ISD::UREM, MVT::v8i32,  { 42 } }, // 2*pmuludq+mul+sub sequence + split.
  };

  if (Op2Info.isConstant() && ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVXConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE41ConstCostTable[] = {
    { ISD::SDIV, MVT::v4i32,  { 15 } }, // vpmuludq sequence
    { ISD::SREM, MVT::v4i32,  { 20 } }, // vpmuludq+mul+sub sequence
  };

  if (Op2Info.isConstant() && ST->hasSSE41())
    if (const auto *Entry =
            CostTableLookup(SSE41ConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE2ConstCostTable[] = {
    { ISD::SDIV, MVT::v16i8,  { 14 } }, // 2*ext+2*pmulhw sequence
    { ISD::SREM, MVT::v16i8,  { 16 } }, // 2*ext+2*pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v16i8,  { 14 } }, // 2*ext+2*pmulhw sequence
    { ISD::UREM, MVT::v16i8,  { 16 } }, // 2*ext+2*pmulhw+mul+sub sequence

    { ISD::SDIV, MVT::v8i16,  {  6 } }, // pmulhw sequence
    { ISD::SREM, MVT::v8i16,  {  8 } }, // pmulhw+mul+sub sequence
    { ISD::UDIV, MVT::v8i16,  {  6 } }, // pmulhuw sequence
    { ISD::UREM, MVT::v8i16,  {  8 } }, // pmulhuw+mul+sub sequence

    { ISD::SDIV, MVT::v4i32,  { 19 } }, // pmuludq sequence
    { ISD::SREM, MVT::v4i32,  { 24 } }, // pmuludq+mul+sub sequence
    { ISD::UDIV, MVT::v4i32,  { 15 } }, // pmuludq sequence
    { ISD::UREM, MVT::v4i32,  { 20 } }, // pmuludq+mul+sub sequence
  };

  if (Op2Info.isConstant() && ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2ConstCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512BWUniformCostTable[] = {
    { ISD::SHL,  MVT::v16i8,  { 3, 5, 5, 7 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8,  { 3,10, 5, 8 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8,  { 4,12, 8,12 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v32i8,  { 4, 7, 6, 8 } }, // psllw + pand.
    { ISD::SRL,  MVT::v32i8,  { 4, 8, 7, 9 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v32i8,  { 5,10,10,13 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v64i8,  { 4, 7, 6, 8 } }, // psllw + pand.
    { ISD::SRL,  MVT::v64i8,  { 4, 8, 7,10 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v64i8,  { 5,10,10,15 } }, // psrlw, pand, pxor, psubb.

    { ISD::SHL,  MVT::v32i16, { 2, 4, 2, 3 } }, // psllw
    { ISD::SRL,  MVT::v32i16, { 2, 4, 2, 3 } }, // psrlw
    { ISD::SRA,  MVT::v32i16, { 2, 4, 2, 3 } }, // psrqw
  };

  if (ST->hasBWI() && Op2Info.isUniform())
    if (const auto *Entry =
            CostTableLookup(AVX512BWUniformCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512UniformCostTable[] = {
    { ISD::SHL,  MVT::v32i16, { 5,10, 5, 7 } }, // psllw + split.
    { ISD::SRL,  MVT::v32i16, { 5,10, 5, 7 } }, // psrlw + split.
    { ISD::SRA,  MVT::v32i16, { 5,10, 5, 7 } }, // psraw + split.

    { ISD::SHL,  MVT::v16i32, { 2, 4, 2, 3 } }, // pslld
    { ISD::SRL,  MVT::v16i32, { 2, 4, 2, 3 } }, // psrld
    { ISD::SRA,  MVT::v16i32, { 2, 4, 2, 3 } }, // psrad

    { ISD::SRA,  MVT::v2i64,  { 1, 2, 1, 2 } }, // psraq
    { ISD::SHL,  MVT::v4i64,  { 1, 4, 1, 2 } }, // psllq
    { ISD::SRL,  MVT::v4i64,  { 1, 4, 1, 2 } }, // psrlq
    { ISD::SRA,  MVT::v4i64,  { 1, 4, 1, 2 } }, // psraq
    { ISD::SHL,  MVT::v8i64,  { 1, 4, 1, 2 } }, // psllq
    { ISD::SRL,  MVT::v8i64,  { 1, 4, 1, 2 } }, // psrlq
    { ISD::SRA,  MVT::v8i64,  { 1, 4, 1, 2 } }, // psraq
  };

  if (ST->hasAVX512() && Op2Info.isUniform())
    if (const auto *Entry =
            CostTableLookup(AVX512UniformCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX2UniformCostTable[] = {
    // Uniform splats are cheaper for the following instructions.
    { ISD::SHL,  MVT::v16i8,  { 3, 5, 5, 7 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8,  { 3, 9, 5, 8 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8,  { 4, 5, 9,13 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v32i8,  { 4, 7, 6, 8 } }, // psllw + pand.
    { ISD::SRL,  MVT::v32i8,  { 4, 8, 7, 9 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v32i8,  { 6, 9,11,16 } }, // psrlw, pand, pxor, psubb.

    { ISD::SHL,  MVT::v8i16,  { 1, 2, 1, 2 } }, // psllw.
    { ISD::SRL,  MVT::v8i16,  { 1, 2, 1, 2 } }, // psrlw.
    { ISD::SRA,  MVT::v8i16,  { 1, 2, 1, 2 } }, // psraw.
    { ISD::SHL,  MVT::v16i16, { 2, 4, 2, 3 } }, // psllw.
    { ISD::SRL,  MVT::v16i16, { 2, 4, 2, 3 } }, // psrlw.
    { ISD::SRA,  MVT::v16i16, { 2, 4, 2, 3 } }, // psraw.

    { ISD::SHL,  MVT::v4i32,  { 1, 2, 1, 2 } }, // pslld
    { ISD::SRL,  MVT::v4i32,  { 1, 2, 1, 2 } }, // psrld
    { ISD::SRA,  MVT::v4i32,  { 1, 2, 1, 2 } }, // psrad
    { ISD::SHL,  MVT::v8i32,  { 2, 4, 2, 3 } }, // pslld
    { ISD::SRL,  MVT::v8i32,  { 2, 4, 2, 3 } }, // psrld
    { ISD::SRA,  MVT::v8i32,  { 2, 4, 2, 3 } }, // psrad

    { ISD::SHL,  MVT::v2i64,  { 1, 2, 1, 2 } }, // psllq
    { ISD::SRL,  MVT::v2i64,  { 1, 2, 1, 2 } }, // psrlq
    { ISD::SRA,  MVT::v2i64,  { 2, 4, 5, 7 } }, // 2 x psrad + shuffle.
    { ISD::SHL,  MVT::v4i64,  { 2, 4, 1, 2 } }, // psllq
    { ISD::SRL,  MVT::v4i64,  { 2, 4, 1, 2 } }, // psrlq
    { ISD::SRA,  MVT::v4i64,  { 4, 6, 5, 9 } }, // 2 x psrad + shuffle.
  };

  if (ST->hasAVX2() && Op2Info.isUniform())
    if (const auto *Entry =
            CostTableLookup(AVX2UniformCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVXUniformCostTable[] = {
    { ISD::SHL,  MVT::v16i8,  {  4, 4, 6, 8 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8,  {  4, 8, 5, 8 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8,  {  6, 6, 9,13 } }, // psrlw, pand, pxor, psubb.
    { ISD::SHL,  MVT::v32i8,  {  7, 8,11,14 } }, // psllw + pand + split.
    { ISD::SRL,  MVT::v32i8,  {  7, 9,10,14 } }, // psrlw + pand + split.
    { ISD::SRA,  MVT::v32i8,  { 10,11,16,21 } }, // psrlw, pand, pxor, psubb + split.

    { ISD::SHL,  MVT::v8i16,  {  1, 3, 1, 2 } }, // psllw.
    { ISD::SRL,  MVT::v8i16,  {  1, 3, 1, 2 } }, // psrlw.
    { ISD::SRA,  MVT::v8i16,  {  1, 3, 1, 2 } }, // psraw.
    { ISD::SHL,  MVT::v16i16, {  3, 7, 5, 7 } }, // psllw + split.
    { ISD::SRL,  MVT::v16i16, {  3, 7, 5, 7 } }, // psrlw + split.
    { ISD::SRA,  MVT::v16i16, {  3, 7, 5, 7 } }, // psraw + split.

    { ISD::SHL,  MVT::v4i32,  {  1, 3, 1, 2 } }, // pslld.
    { ISD::SRL,  MVT::v4i32,  {  1, 3, 1, 2 } }, // psrld.
    { ISD::SRA,  MVT::v4i32,  {  1, 3, 1, 2 } }, // psrad.
    { ISD::SHL,  MVT::v8i32,  {  3, 7, 5, 7 } }, // pslld + split.
    { ISD::SRL,  MVT::v8i32,  {  3, 7, 5, 7 } }, // psrld + split.
    { ISD::SRA,  MVT::v8i32,  {  3, 7, 5, 7 } }, // psrad + split.

    { ISD::SHL,  MVT::v2i64,  {  1, 3, 1, 2 } }, // psllq.
    { ISD::SRL,  MVT::v2i64,  {  1, 3, 1, 2 } }, // psrlq.
    { ISD::SRA,  MVT::v2i64,  {  3, 4, 5, 7 } }, // 2 x psrad + shuffle.
    { ISD::SHL,  MVT::v4i64,  {  3, 7, 4, 6 } }, // psllq + split.
    { ISD::SRL,  MVT::v4i64,  {  3, 7, 4, 6 } }, // psrlq + split.
    { ISD::SRA,  MVT::v4i64,  {  6, 7,10,13 } }, // 2 x (2 x psrad + shuffle) + split.
  };

  // XOP has faster vXi8 shifts.
  if (ST->hasAVX() && Op2Info.isUniform() &&
      (!ST->hasXOP() || LT.second.getScalarSizeInBits() != 8))
    if (const auto *Entry =
            CostTableLookup(AVXUniformCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE2UniformCostTable[] = {
    // Uniform splats are cheaper for the following instructions.
    { ISD::SHL,  MVT::v16i8, {  9, 10, 6, 9 } }, // psllw + pand.
    { ISD::SRL,  MVT::v16i8, {  9, 13, 5, 9 } }, // psrlw + pand.
    { ISD::SRA,  MVT::v16i8, { 11, 15, 9,13 } }, // pcmpgtb sequence.

    { ISD::SHL,  MVT::v8i16, {  2, 2, 1, 2 } }, // psllw.
    { ISD::SRL,  MVT::v8i16, {  2, 2, 1, 2 } }, // psrlw.
    { ISD::SRA,  MVT::v8i16, {  2, 2, 1, 2 } }, // psraw.

    { ISD::SHL,  MVT::v4i32, {  2, 2, 1, 2 } }, // pslld
    { ISD::SRL,  MVT::v4i32, {  2, 2, 1, 2 } }, // psrld.
    { ISD::SRA,  MVT::v4i32, {  2, 2, 1, 2 } }, // psrad.

    { ISD::SHL,  MVT::v2i64, {  2, 2, 1, 2 } }, // psllq.
    { ISD::SRL,  MVT::v2i64, {  2, 2, 1, 2 } }, // psrlq.
    { ISD::SRA,  MVT::v2i64, {  5, 9, 5, 7 } }, // 2*psrlq + xor + sub.
  };

  if (ST->hasSSE2() && Op2Info.isUniform() &&
      (!ST->hasXOP() || LT.second.getScalarSizeInBits() != 8))
    if (const auto *Entry =
            CostTableLookup(SSE2UniformCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512DQCostTable[] = {
    { ISD::MUL,  MVT::v2i64, { 2, 15, 1, 3 } }, // pmullq
    { ISD::MUL,  MVT::v4i64, { 2, 15, 1, 3 } }, // pmullq
    { ISD::MUL,  MVT::v8i64, { 3, 15, 1, 3 } }  // pmullq
  };

  // Look for AVX512DQ lowering tricks for custom cases.
  if (ST->hasDQI())
    if (const auto *Entry = CostTableLookup(AVX512DQCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512BWCostTable[] = {
    { ISD::SHL,   MVT::v16i8,   {  4,  8, 4, 5 } }, // extend/vpsllvw/pack sequence.
    { ISD::SRL,   MVT::v16i8,   {  4,  8, 4, 5 } }, // extend/vpsrlvw/pack sequence.
    { ISD::SRA,   MVT::v16i8,   {  4,  8, 4, 5 } }, // extend/vpsravw/pack sequence.
    { ISD::SHL,   MVT::v32i8,   {  4, 23,11,16 } }, // extend/vpsllvw/pack sequence.
    { ISD::SRL,   MVT::v32i8,   {  4, 30,12,18 } }, // extend/vpsrlvw/pack sequence.
    { ISD::SRA,   MVT::v32i8,   {  6, 13,24,30 } }, // extend/vpsravw/pack sequence.
    { ISD::SHL,   MVT::v64i8,   {  6, 19,13,15 } }, // extend/vpsllvw/pack sequence.
    { ISD::SRL,   MVT::v64i8,   {  7, 27,15,18 } }, // extend/vpsrlvw/pack sequence.
    { ISD::SRA,   MVT::v64i8,   { 15, 15,30,30 } }, // extend/vpsravw/pack sequence.

    { ISD::SHL,   MVT::v8i16,   {  1,  1, 1, 1 } }, // vpsllvw
    { ISD::SRL,   MVT::v8i16,   {  1,  1, 1, 1 } }, // vpsrlvw
    { ISD::SRA,   MVT::v8i16,   {  1,  1, 1, 1 } }, // vpsravw
    { ISD::SHL,   MVT::v16i16,  {  1,  1, 1, 1 } }, // vpsllvw
    { ISD::SRL,   MVT::v16i16,  {  1,  1, 1, 1 } }, // vpsrlvw
    { ISD::SRA,   MVT::v16i16,  {  1,  1, 1, 1 } }, // vpsravw
    { ISD::SHL,   MVT::v32i16,  {  1,  1, 1, 1 } }, // vpsllvw
    { ISD::SRL,   MVT::v32i16,  {  1,  1, 1, 1 } }, // vpsrlvw
    { ISD::SRA,   MVT::v32i16,  {  1,  1, 1, 1 } }, // vpsravw

    { ISD::ADD,   MVT::v64i8,   {  1,  1, 1, 1 } }, // paddb
    { ISD::ADD,   MVT::v32i16,  {  1,  1, 1, 1 } }, // paddw

    { ISD::ADD,   MVT::v32i8,   {  1,  1, 1, 1 } }, // paddb
    { ISD::ADD,   MVT::v16i16,  {  1,  1, 1, 1 } }, // paddw
    { ISD::ADD,   MVT::v8i32,   {  1,  1, 1, 1 } }, // paddd
    { ISD::ADD,   MVT::v4i64,   {  1,  1, 1, 1 } }, // paddq

    { ISD::SUB,   MVT::v64i8,   {  1,  1, 1, 1 } }, // psubb
    { ISD::SUB,   MVT::v32i16,  {  1,  1, 1, 1 } }, // psubw

    { ISD::MUL,   MVT::v16i8,   {  4, 12, 4, 5 } }, // extend/pmullw/trunc
    { ISD::MUL,   MVT::v32i8,   {  3, 10, 7,10 } }, // pmaddubsw
    { ISD::MUL,   MVT::v64i8,   {  3, 11, 7,10 } }, // pmaddubsw
    { ISD::MUL,   MVT::v32i16,  {  1,  5, 1, 1 } }, // pmullw

    { ISD::SUB,   MVT::v32i8,   {  1,  1, 1, 1 } }, // psubb
    { ISD::SUB,   MVT::v16i16,  {  1,  1, 1, 1 } }, // psubw
    { ISD::SUB,   MVT::v8i32,   {  1,  1, 1, 1 } }, // psubd
    { ISD::SUB,   MVT::v4i64,   {  1,  1, 1, 1 } }, // psubq
  };

  // Look for AVX512BW lowering tricks for custom cases.
  if (ST->hasBWI())
    if (const auto *Entry = CostTableLookup(AVX512BWCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX512CostTable[] = {
    { ISD::SHL,     MVT::v64i8,   { 15, 19,27,33 } }, // vpblendv+split sequence.
    { ISD::SRL,     MVT::v64i8,   { 15, 19,30,36 } }, // vpblendv+split sequence.
    { ISD::SRA,     MVT::v64i8,   { 37, 37,51,63 } }, // vpblendv+split sequence.

    { ISD::SHL,     MVT::v32i16,  { 11, 16,11,15 } }, // 2*extend/vpsrlvd/pack sequence.
    { ISD::SRL,     MVT::v32i16,  { 11, 16,11,15 } }, // 2*extend/vpsrlvd/pack sequence.
    { ISD::SRA,     MVT::v32i16,  { 11, 16,11,15 } }, // 2*extend/vpsravd/pack sequence.

    { ISD::SHL,     MVT::v4i32,   {  1,  1, 1, 1 } },
    { ISD::SRL,     MVT::v4i32,   {  1,  1, 1, 1 } },
    { ISD::SRA,     MVT::v4i32,   {  1,  1, 1, 1 } },
    { ISD::SHL,     MVT::v8i32,   {  1,  1, 1, 1 } },
    { ISD::SRL,     MVT::v8i32,   {  1,  1, 1, 1 } },
    { ISD::SRA,     MVT::v8i32,   {  1,  1, 1, 1 } },
    { ISD::SHL,     MVT::v16i32,  {  1,  1, 1, 1 } },
    { ISD::SRL,     MVT::v16i32,  {  1,  1, 1, 1 } },
    { ISD::SRA,     MVT::v16i32,  {  1,  1, 1, 1 } },

    { ISD::SHL,     MVT::v2i64,   {  1,  1, 1, 1 } },
    { ISD::SRL,     MVT::v2i64,   {  1,  1, 1, 1 } },
    { ISD::SRA,     MVT::v2i64,   {  1,  1, 1, 1 } },
    { ISD::SHL,     MVT::v4i64,   {  1,  1, 1, 1 } },
    { ISD::SRL,     MVT::v4i64,   {  1,  1, 1, 1 } },
    { ISD::SRA,     MVT::v4i64,   {  1,  1, 1, 1 } },
    { ISD::SHL,     MVT::v8i64,   {  1,  1, 1, 1 } },
    { ISD::SRL,     MVT::v8i64,   {  1,  1, 1, 1 } },
    { ISD::SRA,     MVT::v8i64,   {  1,  1, 1, 1 } },

    { ISD::ADD,     MVT::v64i8,   {  3,  7, 5, 5 } }, // 2*paddb + split
    { ISD::ADD,     MVT::v32i16,  {  3,  7, 5, 5 } }, // 2*paddw + split

    { ISD::SUB,     MVT::v64i8,   {  3,  7, 5, 5 } }, // 2*psubb + split
    { ISD::SUB,     MVT::v32i16,  {  3,  7, 5, 5 } }, // 2*psubw + split

    { ISD::AND,     MVT::v32i8,   {  1,  1, 1, 1 } },
    { ISD::AND,     MVT::v16i16,  {  1,  1, 1, 1 } },
    { ISD::AND,     MVT::v8i32,   {  1,  1, 1, 1 } },
    { ISD::AND,     MVT::v4i64,   {  1,  1, 1, 1 } },

    { ISD::OR,      MVT::v32i8,   {  1,  1, 1, 1 } },
    { ISD::OR,      MVT::v16i16,  {  1,  1, 1, 1 } },
    { ISD::OR,      MVT::v8i32,   {  1,  1, 1, 1 } },
    { ISD::OR,      MVT::v4i64,   {  1,  1, 1, 1 } },

    { ISD::XOR,     MVT::v32i8,   {  1,  1, 1, 1 } },
    { ISD::XOR,     MVT::v16i16,  {  1,  1, 1, 1 } },
    { ISD::XOR,     MVT::v8i32,   {  1,  1, 1, 1 } },
    { ISD::XOR,     MVT::v4i64,   {  1,  1, 1, 1 } },

    { ISD::MUL,     MVT::v16i32,  {  1, 10, 1, 2 } }, // pmulld (Skylake from agner.org)
    { ISD::MUL,     MVT::v8i32,   {  1, 10, 1, 2 } }, // pmulld (Skylake from agner.org)
    { ISD::MUL,     MVT::v4i32,   {  1, 10, 1, 2 } }, // pmulld (Skylake from agner.org)
    { ISD::MUL,     MVT::v8i64,   {  6,  9, 8, 8 } }, // 3*pmuludq/3*shift/2*add
    { ISD::MUL,     MVT::i64,     {  1 } }, // Skylake from http://www.agner.org/

    { X86ISD::PMULUDQ, MVT::v8i64, { 1,  5, 1, 1 } },

    { ISD::FNEG,    MVT::v8f64,   {  1,  1, 1, 2 } }, // Skylake from http://www.agner.org/
    { ISD::FADD,    MVT::v8f64,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FADD,    MVT::v4f64,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FSUB,    MVT::v8f64,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FSUB,    MVT::v4f64,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v8f64,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v4f64,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v2f64,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::f64,     {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/

    { ISD::FDIV,    MVT::f64,     {  4, 14, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FDIV,    MVT::v2f64,   {  4, 14, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FDIV,    MVT::v4f64,   {  8, 14, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FDIV,    MVT::v8f64,   { 16, 23, 1, 3 } }, // Skylake from http://www.agner.org/

    { ISD::FNEG,    MVT::v16f32,  {  1,  1, 1, 2 } }, // Skylake from http://www.agner.org/
    { ISD::FADD,    MVT::v16f32,  {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FADD,    MVT::v8f32,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FSUB,    MVT::v16f32,  {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FSUB,    MVT::v8f32,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v16f32,  {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v8f32,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::v4f32,   {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FMUL,    MVT::f32,     {  1,  4, 1, 1 } }, // Skylake from http://www.agner.org/

    { ISD::FDIV,    MVT::f32,     {  3, 11, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FDIV,    MVT::v4f32,   {  3, 11, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FDIV,    MVT::v8f32,   {  5, 11, 1, 1 } }, // Skylake from http://www.agner.org/
    { ISD::FDIV,    MVT::v16f32,  { 10, 18, 1, 3 } }, // Skylake from http://www.agner.org/
  };

  if (ST->hasAVX512())
    if (const auto *Entry = CostTableLookup(AVX512CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX2ShiftCostTable[] = {
    // Shifts on vXi64/vXi32 on AVX2 is legal even though we declare to
    // customize them to detect the cases where shift amount is a scalar one.
    { ISD::SHL,     MVT::v4i32,  { 2, 3, 1, 3 } }, // vpsllvd (Haswell from agner.org)
    { ISD::SRL,     MVT::v4i32,  { 2, 3, 1, 3 } }, // vpsrlvd (Haswell from agner.org)
    { ISD::SRA,     MVT::v4i32,  { 2, 3, 1, 3 } }, // vpsravd (Haswell from agner.org)
    { ISD::SHL,     MVT::v8i32,  { 4, 4, 1, 3 } }, // vpsllvd (Haswell from agner.org)
    { ISD::SRL,     MVT::v8i32,  { 4, 4, 1, 3 } }, // vpsrlvd (Haswell from agner.org)
    { ISD::SRA,     MVT::v8i32,  { 4, 4, 1, 3 } }, // vpsravd (Haswell from agner.org)
    { ISD::SHL,     MVT::v2i64,  { 2, 3, 1, 1 } }, // vpsllvq (Haswell from agner.org)
    { ISD::SRL,     MVT::v2i64,  { 2, 3, 1, 1 } }, // vpsrlvq (Haswell from agner.org)
    { ISD::SHL,     MVT::v4i64,  { 4, 4, 1, 2 } }, // vpsllvq (Haswell from agner.org)
    { ISD::SRL,     MVT::v4i64,  { 4, 4, 1, 2 } }, // vpsrlvq (Haswell from agner.org)
  };

  if (ST->hasAVX512()) {
    if (ISD == ISD::SHL && LT.second == MVT::v32i16 && Op2Info.isConstant())
      // On AVX512, a packed v32i16 shift left by a constant build_vector
      // is lowered into a vector multiply (vpmullw).
      return getArithmeticInstrCost(Instruction::Mul, Ty, CostKind,
                                    Op1Info.getNoProps(), Op2Info.getNoProps());
  }

  // Look for AVX2 lowering tricks (XOP is always better at v4i32 shifts).
  if (ST->hasAVX2() && !(ST->hasXOP() && LT.second == MVT::v4i32)) {
    if (ISD == ISD::SHL && LT.second == MVT::v16i16 &&
        Op2Info.isConstant())
      // On AVX2, a packed v16i16 shift left by a constant build_vector
      // is lowered into a vector multiply (vpmullw).
      return getArithmeticInstrCost(Instruction::Mul, Ty, CostKind,
                                    Op1Info.getNoProps(), Op2Info.getNoProps());

    if (const auto *Entry = CostTableLookup(AVX2ShiftCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;
  }

  static const CostKindTblEntry XOPShiftCostTable[] = {
    // 128bit shifts take 1cy, but right shifts require negation beforehand.
    { ISD::SHL,     MVT::v16i8,  { 1, 3, 1, 1 } },
    { ISD::SRL,     MVT::v16i8,  { 2, 3, 1, 1 } },
    { ISD::SRA,     MVT::v16i8,  { 2, 3, 1, 1 } },
    { ISD::SHL,     MVT::v8i16,  { 1, 3, 1, 1 } },
    { ISD::SRL,     MVT::v8i16,  { 2, 3, 1, 1 } },
    { ISD::SRA,     MVT::v8i16,  { 2, 3, 1, 1 } },
    { ISD::SHL,     MVT::v4i32,  { 1, 3, 1, 1 } },
    { ISD::SRL,     MVT::v4i32,  { 2, 3, 1, 1 } },
    { ISD::SRA,     MVT::v4i32,  { 2, 3, 1, 1 } },
    { ISD::SHL,     MVT::v2i64,  { 1, 3, 1, 1 } },
    { ISD::SRL,     MVT::v2i64,  { 2, 3, 1, 1 } },
    { ISD::SRA,     MVT::v2i64,  { 2, 3, 1, 1 } },
    // 256bit shifts require splitting if AVX2 didn't catch them above.
    { ISD::SHL,     MVT::v32i8,  { 4, 7, 5, 6 } },
    { ISD::SRL,     MVT::v32i8,  { 6, 7, 5, 6 } },
    { ISD::SRA,     MVT::v32i8,  { 6, 7, 5, 6 } },
    { ISD::SHL,     MVT::v16i16, { 4, 7, 5, 6 } },
    { ISD::SRL,     MVT::v16i16, { 6, 7, 5, 6 } },
    { ISD::SRA,     MVT::v16i16, { 6, 7, 5, 6 } },
    { ISD::SHL,     MVT::v8i32,  { 4, 7, 5, 6 } },
    { ISD::SRL,     MVT::v8i32,  { 6, 7, 5, 6 } },
    { ISD::SRA,     MVT::v8i32,  { 6, 7, 5, 6 } },
    { ISD::SHL,     MVT::v4i64,  { 4, 7, 5, 6 } },
    { ISD::SRL,     MVT::v4i64,  { 6, 7, 5, 6 } },
    { ISD::SRA,     MVT::v4i64,  { 6, 7, 5, 6 } },
  };

  // Look for XOP lowering tricks.
  if (ST->hasXOP()) {
    // If the right shift is constant then we'll fold the negation so
    // it's as cheap as a left shift.
    int ShiftISD = ISD;
    if ((ShiftISD == ISD::SRL || ShiftISD == ISD::SRA) && Op2Info.isConstant())
      ShiftISD = ISD::SHL;
    if (const auto *Entry =
            CostTableLookup(XOPShiftCostTable, ShiftISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;
  }

  if (ISD == ISD::SHL && !Op2Info.isUniform() && Op2Info.isConstant()) {
    MVT VT = LT.second;
    // Vector shift left by non uniform constant can be lowered
    // into vector multiply.
    if (((VT == MVT::v8i16 || VT == MVT::v4i32) && ST->hasSSE2()) ||
        ((VT == MVT::v16i16 || VT == MVT::v8i32) && ST->hasAVX()))
      ISD = ISD::MUL;
  }

  static const CostKindTblEntry GLMCostTable[] = {
    { ISD::FDIV,  MVT::f32,   { 18, 19, 1, 1 } }, // divss
    { ISD::FDIV,  MVT::v4f32, { 35, 36, 1, 1 } }, // divps
    { ISD::FDIV,  MVT::f64,   { 33, 34, 1, 1 } }, // divsd
    { ISD::FDIV,  MVT::v2f64, { 65, 66, 1, 1 } }, // divpd
  };

  if (ST->useGLMDivSqrtCosts())
    if (const auto *Entry = CostTableLookup(GLMCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SLMCostTable[] = {
    { ISD::MUL,   MVT::v4i32, { 11, 11, 1, 7 } }, // pmulld
    { ISD::MUL,   MVT::v8i16, {  2,  5, 1, 1 } }, // pmullw
    { ISD::FMUL,  MVT::f64,   {  2,  5, 1, 1 } }, // mulsd
    { ISD::FMUL,  MVT::f32,   {  1,  4, 1, 1 } }, // mulss
    { ISD::FMUL,  MVT::v2f64, {  4,  7, 1, 1 } }, // mulpd
    { ISD::FMUL,  MVT::v4f32, {  2,  5, 1, 1 } }, // mulps
    { ISD::FDIV,  MVT::f32,   { 17, 19, 1, 1 } }, // divss
    { ISD::FDIV,  MVT::v4f32, { 39, 39, 1, 6 } }, // divps
    { ISD::FDIV,  MVT::f64,   { 32, 34, 1, 1 } }, // divsd
    { ISD::FDIV,  MVT::v2f64, { 69, 69, 1, 6 } }, // divpd
    { ISD::FADD,  MVT::v2f64, {  2,  4, 1, 1 } }, // addpd
    { ISD::FSUB,  MVT::v2f64, {  2,  4, 1, 1 } }, // subpd
    // v2i64/v4i64 mul is custom lowered as a series of long:
    // multiplies(3), shifts(3) and adds(2)
    // slm muldq version throughput is 2 and addq throughput 4
    // thus: 3X2 (muldq throughput) + 3X1 (shift throughput) +
    //       3X4 (addq throughput) = 17
    { ISD::MUL,   MVT::v2i64, { 17, 22, 9, 9 } },
    // slm addq\subq throughput is 4
    { ISD::ADD,   MVT::v2i64, {  4,  2, 1, 2 } },
    { ISD::SUB,   MVT::v2i64, {  4,  2, 1, 2 } },
  };

  if (ST->useSLMArithCosts())
    if (const auto *Entry = CostTableLookup(SLMCostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX2CostTable[] = {
    { ISD::SHL,  MVT::v16i8,   {  6, 21,11,16 } }, // vpblendvb sequence.
    { ISD::SHL,  MVT::v32i8,   {  6, 23,11,22 } }, // vpblendvb sequence.
    { ISD::SHL,  MVT::v8i16,   {  5, 18, 5,10 } }, // extend/vpsrlvd/pack sequence.
    { ISD::SHL,  MVT::v16i16,  {  8, 10,10,14 } }, // extend/vpsrlvd/pack sequence.

    { ISD::SRL,  MVT::v16i8,   {  6, 27,12,18 } }, // vpblendvb sequence.
    { ISD::SRL,  MVT::v32i8,   {  8, 30,12,24 } }, // vpblendvb sequence.
    { ISD::SRL,  MVT::v8i16,   {  5, 11, 5,10 } }, // extend/vpsrlvd/pack sequence.
    { ISD::SRL,  MVT::v16i16,  {  8, 10,10,14 } }, // extend/vpsrlvd/pack sequence.

    { ISD::SRA,  MVT::v16i8,   { 17, 17,24,30 } }, // vpblendvb sequence.
    { ISD::SRA,  MVT::v32i8,   { 18, 20,24,43 } }, // vpblendvb sequence.
    { ISD::SRA,  MVT::v8i16,   {  5, 11, 5,10 } }, // extend/vpsravd/pack sequence.
    { ISD::SRA,  MVT::v16i16,  {  8, 10,10,14 } }, // extend/vpsravd/pack sequence.
    { ISD::SRA,  MVT::v2i64,   {  4,  5, 5, 5 } }, // srl/xor/sub sequence.
    { ISD::SRA,  MVT::v4i64,   {  8,  8, 5, 9 } }, // srl/xor/sub sequence.

    { ISD::SUB,  MVT::v32i8,   {  1,  1, 1, 2 } }, // psubb
    { ISD::ADD,  MVT::v32i8,   {  1,  1, 1, 2 } }, // paddb
    { ISD::SUB,  MVT::v16i16,  {  1,  1, 1, 2 } }, // psubw
    { ISD::ADD,  MVT::v16i16,  {  1,  1, 1, 2 } }, // paddw
    { ISD::SUB,  MVT::v8i32,   {  1,  1, 1, 2 } }, // psubd
    { ISD::ADD,  MVT::v8i32,   {  1,  1, 1, 2 } }, // paddd
    { ISD::SUB,  MVT::v4i64,   {  1,  1, 1, 2 } }, // psubq
    { ISD::ADD,  MVT::v4i64,   {  1,  1, 1, 2 } }, // paddq

    { ISD::MUL,  MVT::v16i8,   {  5, 18, 6,12 } }, // extend/pmullw/pack
    { ISD::MUL,  MVT::v32i8,   {  4,  8, 8,16 } }, // pmaddubsw
    { ISD::MUL,  MVT::v16i16,  {  2,  5, 1, 2 } }, // pmullw
    { ISD::MUL,  MVT::v8i32,   {  4, 10, 1, 2 } }, // pmulld
    { ISD::MUL,  MVT::v4i32,   {  2, 10, 1, 2 } }, // pmulld
    { ISD::MUL,  MVT::v4i64,   {  6, 10, 8,13 } }, // 3*pmuludq/3*shift/2*add
    { ISD::MUL,  MVT::v2i64,   {  6, 10, 8, 8 } }, // 3*pmuludq/3*shift/2*add

    { X86ISD::PMULUDQ, MVT::v4i64, { 1,  5, 1, 1 } },

    { ISD::FNEG, MVT::v4f64,   {  1,  1, 1, 2 } }, // vxorpd
    { ISD::FNEG, MVT::v8f32,   {  1,  1, 1, 2 } }, // vxorps

    { ISD::FADD, MVT::f64,     {  1,  4, 1, 1 } }, // vaddsd
    { ISD::FADD, MVT::f32,     {  1,  4, 1, 1 } }, // vaddss
    { ISD::FADD, MVT::v2f64,   {  1,  4, 1, 1 } }, // vaddpd
    { ISD::FADD, MVT::v4f32,   {  1,  4, 1, 1 } }, // vaddps
    { ISD::FADD, MVT::v4f64,   {  1,  4, 1, 2 } }, // vaddpd
    { ISD::FADD, MVT::v8f32,   {  1,  4, 1, 2 } }, // vaddps

    { ISD::FSUB, MVT::f64,     {  1,  4, 1, 1 } }, // vsubsd
    { ISD::FSUB, MVT::f32,     {  1,  4, 1, 1 } }, // vsubss
    { ISD::FSUB, MVT::v2f64,   {  1,  4, 1, 1 } }, // vsubpd
    { ISD::FSUB, MVT::v4f32,   {  1,  4, 1, 1 } }, // vsubps
    { ISD::FSUB, MVT::v4f64,   {  1,  4, 1, 2 } }, // vsubpd
    { ISD::FSUB, MVT::v8f32,   {  1,  4, 1, 2 } }, // vsubps

    { ISD::FMUL, MVT::f64,     {  1,  5, 1, 1 } }, // vmulsd
    { ISD::FMUL, MVT::f32,     {  1,  5, 1, 1 } }, // vmulss
    { ISD::FMUL, MVT::v2f64,   {  1,  5, 1, 1 } }, // vmulpd
    { ISD::FMUL, MVT::v4f32,   {  1,  5, 1, 1 } }, // vmulps
    { ISD::FMUL, MVT::v4f64,   {  1,  5, 1, 2 } }, // vmulpd
    { ISD::FMUL, MVT::v8f32,   {  1,  5, 1, 2 } }, // vmulps

    { ISD::FDIV, MVT::f32,     {  7, 13, 1, 1 } }, // vdivss
    { ISD::FDIV, MVT::v4f32,   {  7, 13, 1, 1 } }, // vdivps
    { ISD::FDIV, MVT::v8f32,   { 14, 21, 1, 3 } }, // vdivps
    { ISD::FDIV, MVT::f64,     { 14, 20, 1, 1 } }, // vdivsd
    { ISD::FDIV, MVT::v2f64,   { 14, 20, 1, 1 } }, // vdivpd
    { ISD::FDIV, MVT::v4f64,   { 28, 35, 1, 3 } }, // vdivpd
  };

  // Look for AVX2 lowering tricks for custom cases.
  if (ST->hasAVX2())
    if (const auto *Entry = CostTableLookup(AVX2CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry AVX1CostTable[] = {
    // We don't have to scalarize unsupported ops. We can issue two half-sized
    // operations and we only need to extract the upper YMM half.
    // Two ops + 1 extract + 1 insert = 4.
    { ISD::MUL,     MVT::v32i8,   { 10, 11, 18, 19 } }, // pmaddubsw + split
    { ISD::MUL,     MVT::v16i8,   {  5,  6,  8, 12 } }, // 2*pmaddubsw/3*and/psllw/or
    { ISD::MUL,     MVT::v16i16,  {  4,  8,  5,  6 } }, // pmullw + split
    { ISD::MUL,     MVT::v8i32,   {  5,  8,  5, 10 } }, // pmulld + split
    { ISD::MUL,     MVT::v4i32,   {  2,  5,  1,  3 } }, // pmulld
    { ISD::MUL,     MVT::v4i64,   { 12, 15, 19, 20 } },

    { ISD::AND,     MVT::v32i8,   {  1,  1, 1, 2 } }, // vandps
    { ISD::AND,     MVT::v16i16,  {  1,  1, 1, 2 } }, // vandps
    { ISD::AND,     MVT::v8i32,   {  1,  1, 1, 2 } }, // vandps
    { ISD::AND,     MVT::v4i64,   {  1,  1, 1, 2 } }, // vandps

    { ISD::OR,      MVT::v32i8,   {  1,  1, 1, 2 } }, // vorps
    { ISD::OR,      MVT::v16i16,  {  1,  1, 1, 2 } }, // vorps
    { ISD::OR,      MVT::v8i32,   {  1,  1, 1, 2 } }, // vorps
    { ISD::OR,      MVT::v4i64,   {  1,  1, 1, 2 } }, // vorps

    { ISD::XOR,     MVT::v32i8,   {  1,  1, 1, 2 } }, // vxorps
    { ISD::XOR,     MVT::v16i16,  {  1,  1, 1, 2 } }, // vxorps
    { ISD::XOR,     MVT::v8i32,   {  1,  1, 1, 2 } }, // vxorps
    { ISD::XOR,     MVT::v4i64,   {  1,  1, 1, 2 } }, // vxorps

    { ISD::SUB,     MVT::v32i8,   {  4,  2, 5, 6 } }, // psubb + split
    { ISD::ADD,     MVT::v32i8,   {  4,  2, 5, 6 } }, // paddb + split
    { ISD::SUB,     MVT::v16i16,  {  4,  2, 5, 6 } }, // psubw + split
    { ISD::ADD,     MVT::v16i16,  {  4,  2, 5, 6 } }, // paddw + split
    { ISD::SUB,     MVT::v8i32,   {  4,  2, 5, 6 } }, // psubd + split
    { ISD::ADD,     MVT::v8i32,   {  4,  2, 5, 6 } }, // paddd + split
    { ISD::SUB,     MVT::v4i64,   {  4,  2, 5, 6 } }, // psubq + split
    { ISD::ADD,     MVT::v4i64,   {  4,  2, 5, 6 } }, // paddq + split
    { ISD::SUB,     MVT::v2i64,   {  1,  1, 1, 1 } }, // psubq
    { ISD::ADD,     MVT::v2i64,   {  1,  1, 1, 1 } }, // paddq

    { ISD::SHL,     MVT::v16i8,   { 10, 21,11,17 } }, // pblendvb sequence.
    { ISD::SHL,     MVT::v32i8,   { 22, 22,27,40 } }, // pblendvb sequence + split.
    { ISD::SHL,     MVT::v8i16,   {  6,  9,11,11 } }, // pblendvb sequence.
    { ISD::SHL,     MVT::v16i16,  { 13, 16,24,25 } }, // pblendvb sequence + split.
    { ISD::SHL,     MVT::v4i32,   {  3, 11, 4, 6 } }, // pslld/paddd/cvttps2dq/pmulld
    { ISD::SHL,     MVT::v8i32,   {  9, 11,12,17 } }, // pslld/paddd/cvttps2dq/pmulld + split
    { ISD::SHL,     MVT::v2i64,   {  2,  4, 4, 6 } }, // Shift each lane + blend.
    { ISD::SHL,     MVT::v4i64,   {  6,  7,11,15 } }, // Shift each lane + blend + split.

    { ISD::SRL,     MVT::v16i8,   { 11, 27,12,18 } }, // pblendvb sequence.
    { ISD::SRL,     MVT::v32i8,   { 23, 23,30,43 } }, // pblendvb sequence + split.
    { ISD::SRL,     MVT::v8i16,   { 13, 16,14,22 } }, // pblendvb sequence.
    { ISD::SRL,     MVT::v16i16,  { 28, 30,31,48 } }, // pblendvb sequence + split.
    { ISD::SRL,     MVT::v4i32,   {  6,  7,12,16 } }, // Shift each lane + blend.
    { ISD::SRL,     MVT::v8i32,   { 14, 14,26,34 } }, // Shift each lane + blend + split.
    { ISD::SRL,     MVT::v2i64,   {  2,  4, 4, 6 } }, // Shift each lane + blend.
    { ISD::SRL,     MVT::v4i64,   {  6,  7,11,15 } }, // Shift each lane + blend + split.

    { ISD::SRA,     MVT::v16i8,   { 21, 22,24,36 } }, // pblendvb sequence.
    { ISD::SRA,     MVT::v32i8,   { 44, 45,51,76 } }, // pblendvb sequence + split.
    { ISD::SRA,     MVT::v8i16,   { 13, 16,14,22 } }, // pblendvb sequence.
    { ISD::SRA,     MVT::v16i16,  { 28, 30,31,48 } }, // pblendvb sequence + split.
    { ISD::SRA,     MVT::v4i32,   {  6,  7,12,16 } }, // Shift each lane + blend.
    { ISD::SRA,     MVT::v8i32,   { 14, 14,26,34 } }, // Shift each lane + blend + split.
    { ISD::SRA,     MVT::v2i64,   {  5,  6,10,14 } }, // Shift each lane + blend.
    { ISD::SRA,     MVT::v4i64,   { 12, 12,22,30 } }, // Shift each lane + blend + split.

    { ISD::FNEG,    MVT::v4f64,   {  2,  2, 1, 2 } }, // BTVER2 from http://www.agner.org/
    { ISD::FNEG,    MVT::v8f32,   {  2,  2, 1, 2 } }, // BTVER2 from http://www.agner.org/

    { ISD::FADD,    MVT::f64,     {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FADD,    MVT::f32,     {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FADD,    MVT::v2f64,   {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FADD,    MVT::v4f32,   {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FADD,    MVT::v4f64,   {  2,  5, 1, 2 } }, // BDVER2 from http://www.agner.org/
    { ISD::FADD,    MVT::v8f32,   {  2,  5, 1, 2 } }, // BDVER2 from http://www.agner.org/

    { ISD::FSUB,    MVT::f64,     {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FSUB,    MVT::f32,     {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FSUB,    MVT::v2f64,   {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FSUB,    MVT::v4f32,   {  1,  5, 1, 1 } }, // BDVER2 from http://www.agner.org/
    { ISD::FSUB,    MVT::v4f64,   {  2,  5, 1, 2 } }, // BDVER2 from http://www.agner.org/
    { ISD::FSUB,    MVT::v8f32,   {  2,  5, 1, 2 } }, // BDVER2 from http://www.agner.org/

    { ISD::FMUL,    MVT::f64,     {  2,  5, 1, 1 } }, // BTVER2 from http://www.agner.org/
    { ISD::FMUL,    MVT::f32,     {  1,  5, 1, 1 } }, // BTVER2 from http://www.agner.org/
    { ISD::FMUL,    MVT::v2f64,   {  2,  5, 1, 1 } }, // BTVER2 from http://www.agner.org/
    { ISD::FMUL,    MVT::v4f32,   {  1,  5, 1, 1 } }, // BTVER2 from http://www.agner.org/
    { ISD::FMUL,    MVT::v4f64,   {  4,  5, 1, 2 } }, // BTVER2 from http://www.agner.org/
    { ISD::FMUL,    MVT::v8f32,   {  2,  5, 1, 2 } }, // BTVER2 from http://www.agner.org/

    { ISD::FDIV,    MVT::f32,     { 14, 14, 1, 1 } }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v4f32,   { 14, 14, 1, 1 } }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v8f32,   { 28, 29, 1, 3 } }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::f64,     { 22, 22, 1, 1 } }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v2f64,   { 22, 22, 1, 1 } }, // SNB from http://www.agner.org/
    { ISD::FDIV,    MVT::v4f64,   { 44, 45, 1, 3 } }, // SNB from http://www.agner.org/
  };

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE42CostTable[] = {
    { ISD::FADD, MVT::f64,    {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FADD, MVT::f32,    {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FADD, MVT::v2f64,  {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FADD, MVT::v4f32,  {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/

    { ISD::FSUB, MVT::f64,    {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FSUB, MVT::f32 ,   {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FSUB, MVT::v2f64,  {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FSUB, MVT::v4f32,  {  1,  3, 1, 1 } }, // Nehalem from http://www.agner.org/

    { ISD::FMUL, MVT::f64,    {  1,  5, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FMUL, MVT::f32,    {  1,  5, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FMUL, MVT::v2f64,  {  1,  5, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FMUL, MVT::v4f32,  {  1,  5, 1, 1 } }, // Nehalem from http://www.agner.org/

    { ISD::FDIV,  MVT::f32,   { 14, 14, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FDIV,  MVT::v4f32, { 14, 14, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FDIV,  MVT::f64,   { 22, 22, 1, 1 } }, // Nehalem from http://www.agner.org/
    { ISD::FDIV,  MVT::v2f64, { 22, 22, 1, 1 } }, // Nehalem from http://www.agner.org/

    { ISD::MUL,   MVT::v2i64, {  6, 10,10,10 } }  // 3*pmuludq/3*shift/2*add
  };

  if (ST->hasSSE42())
    if (const auto *Entry = CostTableLookup(SSE42CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE41CostTable[] = {
    { ISD::SHL,  MVT::v16i8,  { 15, 24,17,22 } }, // pblendvb sequence.
    { ISD::SHL,  MVT::v8i16,  { 11, 14,11,11 } }, // pblendvb sequence.
    { ISD::SHL,  MVT::v4i32,  { 14, 20, 4,10 } }, // pslld/paddd/cvttps2dq/pmulld

    { ISD::SRL,  MVT::v16i8,  { 16, 27,18,24 } }, // pblendvb sequence.
    { ISD::SRL,  MVT::v8i16,  { 22, 26,23,27 } }, // pblendvb sequence.
    { ISD::SRL,  MVT::v4i32,  { 16, 17,15,19 } }, // Shift each lane + blend.
    { ISD::SRL,  MVT::v2i64,  {  4,  6, 5, 7 } }, // splat+shuffle sequence.

    { ISD::SRA,  MVT::v16i8,  { 38, 41,30,36 } }, // pblendvb sequence.
    { ISD::SRA,  MVT::v8i16,  { 22, 26,23,27 } }, // pblendvb sequence.
    { ISD::SRA,  MVT::v4i32,  { 16, 17,15,19 } }, // Shift each lane + blend.
    { ISD::SRA,  MVT::v2i64,  {  8, 17, 5, 7 } }, // splat+shuffle sequence.

    { ISD::MUL,  MVT::v4i32,  {  2, 11, 1, 1 } }  // pmulld (Nehalem from agner.org)
  };

  if (ST->hasSSE41())
    if (const auto *Entry = CostTableLookup(SSE41CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSSE3CostTable[] = {
    { ISD::MUL,  MVT::v16i8,  {  5, 18,10,12 } }, // 2*pmaddubsw/3*and/psllw/or
  };

  if (ST->hasSSSE3())
    if (const auto *Entry = CostTableLookup(SSSE3CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE2CostTable[] = {
    // We don't correctly identify costs of casts because they are marked as
    // custom.
    { ISD::SHL,  MVT::v16i8,  { 13, 21,26,28 } }, // cmpgtb sequence.
    { ISD::SHL,  MVT::v8i16,  { 24, 27,16,20 } }, // cmpgtw sequence.
    { ISD::SHL,  MVT::v4i32,  { 17, 19,10,12 } }, // pslld/paddd/cvttps2dq/pmuludq.
    { ISD::SHL,  MVT::v2i64,  {  4,  6, 5, 7 } }, // splat+shuffle sequence.

    { ISD::SRL,  MVT::v16i8,  { 14, 28,27,30 } }, // cmpgtb sequence.
    { ISD::SRL,  MVT::v8i16,  { 16, 19,31,31 } }, // cmpgtw sequence.
    { ISD::SRL,  MVT::v4i32,  { 12, 12,15,19 } }, // Shift each lane + blend.
    { ISD::SRL,  MVT::v2i64,  {  4,  6, 5, 7 } }, // splat+shuffle sequence.

    { ISD::SRA,  MVT::v16i8,  { 27, 30,54,54 } }, // unpacked cmpgtb sequence.
    { ISD::SRA,  MVT::v8i16,  { 16, 19,31,31 } }, // cmpgtw sequence.
    { ISD::SRA,  MVT::v4i32,  { 12, 12,15,19 } }, // Shift each lane + blend.
    { ISD::SRA,  MVT::v2i64,  {  8, 11,12,16 } }, // srl/xor/sub splat+shuffle sequence.

    { ISD::AND,  MVT::v16i8,  {  1,  1, 1, 1 } }, // pand
    { ISD::AND,  MVT::v8i16,  {  1,  1, 1, 1 } }, // pand
    { ISD::AND,  MVT::v4i32,  {  1,  1, 1, 1 } }, // pand
    { ISD::AND,  MVT::v2i64,  {  1,  1, 1, 1 } }, // pand

    { ISD::OR,   MVT::v16i8,  {  1,  1, 1, 1 } }, // por
    { ISD::OR,   MVT::v8i16,  {  1,  1, 1, 1 } }, // por
    { ISD::OR,   MVT::v4i32,  {  1,  1, 1, 1 } }, // por
    { ISD::OR,   MVT::v2i64,  {  1,  1, 1, 1 } }, // por

    { ISD::XOR,  MVT::v16i8,  {  1,  1, 1, 1 } }, // pxor
    { ISD::XOR,  MVT::v8i16,  {  1,  1, 1, 1 } }, // pxor
    { ISD::XOR,  MVT::v4i32,  {  1,  1, 1, 1 } }, // pxor
    { ISD::XOR,  MVT::v2i64,  {  1,  1, 1, 1 } }, // pxor

    { ISD::ADD,  MVT::v2i64,  {  1,  2, 1, 2 } }, // paddq
    { ISD::SUB,  MVT::v2i64,  {  1,  2, 1, 2 } }, // psubq

    { ISD::MUL,  MVT::v16i8,  {  6, 18,12,12 } }, // 2*unpack/2*pmullw/2*and/pack
    { ISD::MUL,  MVT::v8i16,  {  1,  5, 1, 1 } }, // pmullw
    { ISD::MUL,  MVT::v4i32,  {  6,  8, 7, 7 } }, // 3*pmuludq/4*shuffle
    { ISD::MUL,  MVT::v2i64,  {  7, 10,10,10 } }, // 3*pmuludq/3*shift/2*add

    { X86ISD::PMULUDQ, MVT::v2i64, { 1,  5, 1, 1 } },

    { ISD::FDIV, MVT::f32,    { 23, 23, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FDIV, MVT::v4f32,  { 39, 39, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FDIV, MVT::f64,    { 38, 38, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FDIV, MVT::v2f64,  { 69, 69, 1, 1 } }, // Pentium IV from http://www.agner.org/

    { ISD::FNEG, MVT::f32,    {  1,  1, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FNEG, MVT::f64,    {  1,  1, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FNEG, MVT::v4f32,  {  1,  1, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FNEG, MVT::v2f64,  {  1,  1, 1, 1 } }, // Pentium IV from http://www.agner.org/

    { ISD::FADD, MVT::f32,    {  2,  3, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FADD, MVT::f64,    {  2,  3, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FADD, MVT::v2f64,  {  2,  3, 1, 1 } }, // Pentium IV from http://www.agner.org/

    { ISD::FSUB, MVT::f32,    {  2,  3, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FSUB, MVT::f64,    {  2,  3, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FSUB, MVT::v2f64,  {  2,  3, 1, 1 } }, // Pentium IV from http://www.agner.org/

    { ISD::FMUL, MVT::f64,    {  2,  5, 1, 1 } }, // Pentium IV from http://www.agner.org/
    { ISD::FMUL, MVT::v2f64,  {  2,  5, 1, 1 } }, // Pentium IV from http://www.agner.org/
  };

  if (ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry SSE1CostTable[] = {
    { ISD::FDIV, MVT::f32,   { 17, 18, 1, 1 } }, // Pentium III from http://www.agner.org/
    { ISD::FDIV, MVT::v4f32, { 34, 48, 1, 1 } }, // Pentium III from http://www.agner.org/

    { ISD::FNEG, MVT::f32,   {  2,  2, 1, 2 } }, // Pentium III from http://www.agner.org/
    { ISD::FNEG, MVT::v4f32, {  2,  2, 1, 2 } }, // Pentium III from http://www.agner.org/

    { ISD::FADD, MVT::f32,   {  1,  3, 1, 1 } }, // Pentium III from http://www.agner.org/
    { ISD::FADD, MVT::v4f32, {  2,  3, 1, 1 } }, // Pentium III from http://www.agner.org/

    { ISD::FSUB, MVT::f32,   {  1,  3, 1, 1 } }, // Pentium III from http://www.agner.org/
    { ISD::FSUB, MVT::v4f32, {  2,  3, 1, 1 } }, // Pentium III from http://www.agner.org/

    { ISD::FMUL, MVT::f32,   {  2,  5, 1, 1 } }, // Pentium III from http://www.agner.org/
    { ISD::FMUL, MVT::v4f32, {  2,  5, 1, 1 } }, // Pentium III from http://www.agner.org/
  };

  if (ST->hasSSE1())
    if (const auto *Entry = CostTableLookup(SSE1CostTable, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry X64CostTbl[] = { // 64-bit targets
    { ISD::ADD,  MVT::i64,  {  1 } }, // Core (Merom) from http://www.agner.org/
    { ISD::SUB,  MVT::i64,  {  1 } }, // Core (Merom) from http://www.agner.org/
    { ISD::MUL,  MVT::i64,  {  2,  6,  1,  2 } },
  };

  if (ST->is64Bit())
    if (const auto *Entry = CostTableLookup(X64CostTbl, ISD, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostKindTblEntry X86CostTbl[] = { // 32 or 64-bit targets
    { ISD::ADD,  MVT::i8,  {  1 } }, // Pentium III from http://www.agner.org/
    { ISD::ADD,  MVT::i16, {  1 } }, // Pentium III from http://www.agner.org/
    { ISD::ADD,  MVT::i32, {  1 } }, // Pentium III from http://www.agner.org/

    { ISD::SUB,  MVT::i8,  {  1 } }, // Pentium III from http://www.agner.org/
    { ISD::SUB,  MVT::i16, {  1 } }, // Pentium III from http://www.agner.org/
    { ISD::SUB,  MVT::i32, {  1 } }, // Pentium III from http://www.agner.org/

    { ISD::MUL,  MVT::i8,  {  3,  4, 1, 1 } },
    { ISD::MUL,  MVT::i16, {  2,  4, 1, 1 } },
    { ISD::MUL,  MVT::i32, {  1,  4, 1, 1 } },

    { ISD::FNEG, MVT::f64, {  2,  2, 1, 3 } }, // (x87)
    { ISD::FADD, MVT::f64, {  2,  3, 1, 1 } }, // (x87)
    { ISD::FSUB, MVT::f64, {  2,  3, 1, 1 } }, // (x87)
    { ISD::FMUL, MVT::f64, {  2,  5, 1, 1 } }, // (x87)
    { ISD::FDIV, MVT::f64, { 38, 38, 1, 1 } }, // (x87)
  };

  if (const auto *Entry = CostTableLookup(X86CostTbl, ISD, LT.second))
    if (auto KindCost = Entry->Cost[CostKind])
      return LT.first * *KindCost;

  // It is not a good idea to vectorize division. We have to scalarize it and
  // in the process we will often end up having to spilling regular
  // registers. The overhead of division is going to dominate most kernels
  // anyways so try hard to prevent vectorization of division - it is
  // generally a bad idea. Assume somewhat arbitrarily that we have to be able
  // to hide "20 cycles" for each lane.
  if (CostKind == TTI::TCK_RecipThroughput && LT.second.isVector() &&
      (ISD == ISD::SDIV || ISD == ISD::SREM || ISD == ISD::UDIV ||
       ISD == ISD::UREM)) {
    InstructionCost ScalarCost =
        getArithmeticInstrCost(Opcode, Ty->getScalarType(), CostKind,
                               Op1Info.getNoProps(), Op2Info.getNoProps());
    return 20 * LT.first * LT.second.getVectorNumElements() * ScalarCost;
  }

  // Handle some basic single instruction code size cases.
  if (CostKind == TTI::TCK_CodeSize) {
    switch (ISD) {
    case ISD::FADD:
    case ISD::FSUB:
    case ISD::FMUL:
    case ISD::FDIV:
    case ISD::FNEG:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
      return LT.first;
      break;
    }
  }

  // Fallback to the default implementation.
  return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                       Args, CxtI);
}

InstructionCost
X86TTIImpl::getAltInstrCost(VectorType *VecTy, unsigned Opcode0,
                            unsigned Opcode1, const SmallBitVector &OpcodeMask,
                            TTI::TargetCostKind CostKind) const {
  if (isLegalAltInstr(VecTy, Opcode0, Opcode1, OpcodeMask))
    return TTI::TCC_Basic;
  return InstructionCost::getInvalid();
}

InstructionCost X86TTIImpl::getShuffleCost(
    TTI::ShuffleKind Kind, VectorType *BaseTp, ArrayRef<int> Mask,
    TTI::TargetCostKind CostKind, int Index, VectorType *SubTp,
    ArrayRef<const Value *> Args, const Instruction *CxtI) {
  // 64-bit packed float vectors (v2f32) are widened to type v4f32.
  // 64-bit packed integer vectors (v2i32) are widened to type v4i32.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(BaseTp);

  Kind = improveShuffleKindFromMask(Kind, Mask, BaseTp, Index, SubTp);

  // Recognize a basic concat_vector shuffle.
  if (Kind == TTI::SK_PermuteTwoSrc &&
      Mask.size() == (2 * BaseTp->getElementCount().getKnownMinValue()) &&
      ShuffleVectorInst::isIdentityMask(Mask, Mask.size()))
    return getShuffleCost(TTI::SK_InsertSubvector,
                          VectorType::getDoubleElementsVectorType(BaseTp), Mask,
                          CostKind, Mask.size() / 2, BaseTp);

  // Treat Transpose as 2-op shuffles - there's no difference in lowering.
  if (Kind == TTI::SK_Transpose)
    Kind = TTI::SK_PermuteTwoSrc;

  if (Kind == TTI::SK_Broadcast) {
    // For Broadcasts we are splatting the first element from the first input
    // register, so only need to reference that input and all the output
    // registers are the same.
    LT.first = 1;

    // If we're broadcasting a load then AVX/AVX2 can do this for free.
    using namespace PatternMatch;
    if (!Args.empty() && match(Args[0], m_OneUse(m_Load(m_Value()))) &&
        (ST->hasAVX2() ||
         (ST->hasAVX() && LT.second.getScalarSizeInBits() >= 32)))
      return TTI::TCC_Free;
  }

  // Treat <X x bfloat> shuffles as <X x half>.
  if (LT.second.isVector() && LT.second.getScalarType() == MVT::bf16)
    LT.second = LT.second.changeVectorElementType(MVT::f16);

  // Subvector extractions are free if they start at the beginning of a
  // vector and cheap if the subvectors are aligned.
  if (Kind == TTI::SK_ExtractSubvector && LT.second.isVector()) {
    int NumElts = LT.second.getVectorNumElements();
    if ((Index % NumElts) == 0)
      return 0;
    std::pair<InstructionCost, MVT> SubLT = getTypeLegalizationCost(SubTp);
    if (SubLT.second.isVector()) {
      int NumSubElts = SubLT.second.getVectorNumElements();
      if ((Index % NumSubElts) == 0 && (NumElts % NumSubElts) == 0)
        return SubLT.first;
      // Handle some cases for widening legalization. For now we only handle
      // cases where the original subvector was naturally aligned and evenly
      // fit in its legalized subvector type.
      // FIXME: Remove some of the alignment restrictions.
      // FIXME: We can use permq for 64-bit or larger extracts from 256-bit
      // vectors.
      int OrigSubElts = cast<FixedVectorType>(SubTp)->getNumElements();
      if (NumSubElts > OrigSubElts && (Index % OrigSubElts) == 0 &&
          (NumSubElts % OrigSubElts) == 0 &&
          LT.second.getVectorElementType() ==
              SubLT.second.getVectorElementType() &&
          LT.second.getVectorElementType().getSizeInBits() ==
              BaseTp->getElementType()->getPrimitiveSizeInBits()) {
        assert(NumElts >= NumSubElts && NumElts > OrigSubElts &&
               "Unexpected number of elements!");
        auto *VecTy = FixedVectorType::get(BaseTp->getElementType(),
                                           LT.second.getVectorNumElements());
        auto *SubTy = FixedVectorType::get(BaseTp->getElementType(),
                                           SubLT.second.getVectorNumElements());
        int ExtractIndex = alignDown((Index % NumElts), NumSubElts);
        InstructionCost ExtractCost =
            getShuffleCost(TTI::SK_ExtractSubvector, VecTy, std::nullopt,
                           CostKind, ExtractIndex, SubTy);

        // If the original size is 32-bits or more, we can use pshufd. Otherwise
        // if we have SSSE3 we can use pshufb.
        if (SubTp->getPrimitiveSizeInBits() >= 32 || ST->hasSSSE3())
          return ExtractCost + 1; // pshufd or pshufb

        assert(SubTp->getPrimitiveSizeInBits() == 16 &&
               "Unexpected vector size");

        return ExtractCost + 2; // worst case pshufhw + pshufd
      }
    }
    // If the extract subvector is not optimal, treat it as single op shuffle.
    Kind = TTI::SK_PermuteSingleSrc;
  }

  // Subvector insertions are cheap if the subvectors are aligned.
  // Note that in general, the insertion starting at the beginning of a vector
  // isn't free, because we need to preserve the rest of the wide vector.
  if (Kind == TTI::SK_InsertSubvector && LT.second.isVector()) {
    int NumElts = LT.second.getVectorNumElements();
    std::pair<InstructionCost, MVT> SubLT = getTypeLegalizationCost(SubTp);
    if (SubLT.second.isVector()) {
      int NumSubElts = SubLT.second.getVectorNumElements();
      if ((Index % NumSubElts) == 0 && (NumElts % NumSubElts) == 0)
        return SubLT.first;
    }

    // If the insertion isn't aligned, treat it like a 2-op shuffle.
    Kind = TTI::SK_PermuteTwoSrc;
  }

  // Handle some common (illegal) sub-vector types as they are often very cheap
  // to shuffle even on targets without PSHUFB.
  EVT VT = TLI->getValueType(DL, BaseTp);
  if (VT.isSimple() && VT.isVector() && VT.getSizeInBits() < 128 &&
      !ST->hasSSSE3()) {
     static const CostTblEntry SSE2SubVectorShuffleTbl[] = {
      {TTI::SK_Broadcast,        MVT::v4i16, 1}, // pshuflw
      {TTI::SK_Broadcast,        MVT::v2i16, 1}, // pshuflw
      {TTI::SK_Broadcast,        MVT::v8i8,  2}, // punpck/pshuflw
      {TTI::SK_Broadcast,        MVT::v4i8,  2}, // punpck/pshuflw
      {TTI::SK_Broadcast,        MVT::v2i8,  1}, // punpck

      {TTI::SK_Reverse,          MVT::v4i16, 1}, // pshuflw
      {TTI::SK_Reverse,          MVT::v2i16, 1}, // pshuflw
      {TTI::SK_Reverse,          MVT::v4i8,  3}, // punpck/pshuflw/packus
      {TTI::SK_Reverse,          MVT::v2i8,  1}, // punpck

      {TTI::SK_Splice,           MVT::v4i16, 2}, // punpck+psrldq
      {TTI::SK_Splice,           MVT::v2i16, 2}, // punpck+psrldq
      {TTI::SK_Splice,           MVT::v4i8,  2}, // punpck+psrldq
      {TTI::SK_Splice,           MVT::v2i8,  2}, // punpck+psrldq

      {TTI::SK_PermuteTwoSrc,    MVT::v4i16, 2}, // punpck/pshuflw
      {TTI::SK_PermuteTwoSrc,    MVT::v2i16, 2}, // punpck/pshuflw
      {TTI::SK_PermuteTwoSrc,    MVT::v8i8,  7}, // punpck/pshuflw
      {TTI::SK_PermuteTwoSrc,    MVT::v4i8,  4}, // punpck/pshuflw
      {TTI::SK_PermuteTwoSrc,    MVT::v2i8,  2}, // punpck

      {TTI::SK_PermuteSingleSrc, MVT::v4i16, 1}, // pshuflw
      {TTI::SK_PermuteSingleSrc, MVT::v2i16, 1}, // pshuflw
      {TTI::SK_PermuteSingleSrc, MVT::v8i8,  5}, // punpck/pshuflw
      {TTI::SK_PermuteSingleSrc, MVT::v4i8,  3}, // punpck/pshuflw
      {TTI::SK_PermuteSingleSrc, MVT::v2i8,  1}, // punpck
    };

    if (ST->hasSSE2())
      if (const auto *Entry =
              CostTableLookup(SSE2SubVectorShuffleTbl, Kind, VT.getSimpleVT()))
        return Entry->Cost;
  }

  // We are going to permute multiple sources and the result will be in multiple
  // destinations. Providing an accurate cost only for splits where the element
  // type remains the same.
  if (Kind == TTI::SK_PermuteSingleSrc && LT.first != 1) {
    MVT LegalVT = LT.second;
    if (LegalVT.isVector() &&
        LegalVT.getVectorElementType().getSizeInBits() ==
            BaseTp->getElementType()->getPrimitiveSizeInBits() &&
        LegalVT.getVectorNumElements() <
            cast<FixedVectorType>(BaseTp)->getNumElements()) {
      unsigned VecTySize = DL.getTypeStoreSize(BaseTp);
      unsigned LegalVTSize = LegalVT.getStoreSize();
      // Number of source vectors after legalization:
      unsigned NumOfSrcs = (VecTySize + LegalVTSize - 1) / LegalVTSize;
      // Number of destination vectors after legalization:
      InstructionCost NumOfDests = LT.first;

      auto *SingleOpTy = FixedVectorType::get(BaseTp->getElementType(),
                                              LegalVT.getVectorNumElements());

      if (!Mask.empty() && NumOfDests.isValid()) {
        // Try to perform better estimation of the permutation.
        // 1. Split the source/destination vectors into real registers.
        // 2. Do the mask analysis to identify which real registers are
        // permuted. If more than 1 source registers are used for the
        // destination register building, the cost for this destination register
        // is (Number_of_source_register - 1) * Cost_PermuteTwoSrc. If only one
        // source register is used, build mask and calculate the cost as a cost
        // of PermuteSingleSrc.
        // Also, for the single register permute we try to identify if the
        // destination register is just a copy of the source register or the
        // copy of the previous destination register (the cost is
        // TTI::TCC_Basic). If the source register is just reused, the cost for
        // this operation is 0.
        NumOfDests =
            getTypeLegalizationCost(
                FixedVectorType::get(BaseTp->getElementType(), Mask.size()))
                .first;
        unsigned E = *NumOfDests.getValue();
        unsigned NormalizedVF =
            LegalVT.getVectorNumElements() * std::max(NumOfSrcs, E);
        unsigned NumOfSrcRegs = NormalizedVF / LegalVT.getVectorNumElements();
        unsigned NumOfDestRegs = NormalizedVF / LegalVT.getVectorNumElements();
        SmallVector<int> NormalizedMask(NormalizedVF, PoisonMaskElem);
        copy(Mask, NormalizedMask.begin());
        unsigned PrevSrcReg = 0;
        ArrayRef<int> PrevRegMask;
        InstructionCost Cost = 0;
        processShuffleMasks(
            NormalizedMask, NumOfSrcRegs, NumOfDestRegs, NumOfDestRegs, []() {},
            [this, SingleOpTy, CostKind, &PrevSrcReg, &PrevRegMask,
             &Cost](ArrayRef<int> RegMask, unsigned SrcReg, unsigned DestReg) {
              if (!ShuffleVectorInst::isIdentityMask(RegMask, RegMask.size())) {
                // Check if the previous register can be just copied to the next
                // one.
                if (PrevRegMask.empty() || PrevSrcReg != SrcReg ||
                    PrevRegMask != RegMask)
                  Cost += getShuffleCost(TTI::SK_PermuteSingleSrc, SingleOpTy,
                                         RegMask, CostKind, 0, nullptr);
                else
                  // Just a copy of previous destination register.
                  Cost += TTI::TCC_Basic;
                return;
              }
              if (SrcReg != DestReg &&
                  any_of(RegMask, [](int I) { return I != PoisonMaskElem; })) {
                // Just a copy of the source register.
                Cost += TTI::TCC_Basic;
              }
              PrevSrcReg = SrcReg;
              PrevRegMask = RegMask;
            },
            [this, SingleOpTy, CostKind, &Cost](ArrayRef<int> RegMask,
                                                unsigned /*Unused*/,
                                                unsigned /*Unused*/) {
              Cost += getShuffleCost(TTI::SK_PermuteTwoSrc, SingleOpTy, RegMask,
                                     CostKind, 0, nullptr);
            });
        return Cost;
      }

      InstructionCost NumOfShuffles = (NumOfSrcs - 1) * NumOfDests;
      return NumOfShuffles * getShuffleCost(TTI::SK_PermuteTwoSrc, SingleOpTy,
                                            std::nullopt, CostKind, 0, nullptr);
    }

    return BaseT::getShuffleCost(Kind, BaseTp, Mask, CostKind, Index, SubTp);
  }

  // For 2-input shuffles, we must account for splitting the 2 inputs into many.
  if (Kind == TTI::SK_PermuteTwoSrc && LT.first != 1) {
    // We assume that source and destination have the same vector type.
    InstructionCost NumOfDests = LT.first;
    InstructionCost NumOfShufflesPerDest = LT.first * 2 - 1;
    LT.first = NumOfDests * NumOfShufflesPerDest;
  }

  static const CostTblEntry AVX512VBMIShuffleTbl[] = {
      {TTI::SK_Reverse, MVT::v64i8, 1}, // vpermb
      {TTI::SK_Reverse, MVT::v32i8, 1}, // vpermb

      {TTI::SK_PermuteSingleSrc, MVT::v64i8, 1}, // vpermb
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 1}, // vpermb

      {TTI::SK_PermuteTwoSrc, MVT::v64i8, 2}, // vpermt2b
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 2}, // vpermt2b
      {TTI::SK_PermuteTwoSrc, MVT::v16i8, 2}  // vpermt2b
  };

  if (ST->hasVBMI())
    if (const auto *Entry =
            CostTableLookup(AVX512VBMIShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX512BWShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v32i16, 1}, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v32f16, 1}, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v64i8, 1},  // vpbroadcastb

      {TTI::SK_Reverse, MVT::v32i16, 2}, // vpermw
      {TTI::SK_Reverse, MVT::v32f16, 2}, // vpermw
      {TTI::SK_Reverse, MVT::v16i16, 2}, // vpermw
      {TTI::SK_Reverse, MVT::v64i8, 2},  // pshufb + vshufi64x2

      {TTI::SK_PermuteSingleSrc, MVT::v32i16, 2}, // vpermw
      {TTI::SK_PermuteSingleSrc, MVT::v32f16, 2}, // vpermw
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 2}, // vpermw
      {TTI::SK_PermuteSingleSrc, MVT::v16f16, 2}, // vpermw
      {TTI::SK_PermuteSingleSrc, MVT::v64i8, 8},  // extend to v32i16

      {TTI::SK_PermuteTwoSrc, MVT::v32i16, 2}, // vpermt2w
      {TTI::SK_PermuteTwoSrc, MVT::v32f16, 2}, // vpermt2w
      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 2}, // vpermt2w
      {TTI::SK_PermuteTwoSrc, MVT::v8i16, 2},  // vpermt2w
      {TTI::SK_PermuteTwoSrc, MVT::v64i8, 19}, // 6 * v32i8 + 1

      {TTI::SK_Select, MVT::v32i16, 1}, // vblendmw
      {TTI::SK_Select, MVT::v64i8,  1}, // vblendmb

      {TTI::SK_Splice, MVT::v32i16, 2}, // vshufi64x2 + palignr
      {TTI::SK_Splice, MVT::v32f16, 2}, // vshufi64x2 + palignr
      {TTI::SK_Splice, MVT::v64i8,  2}, // vshufi64x2 + palignr
  };

  if (ST->hasBWI())
    if (const auto *Entry =
            CostTableLookup(AVX512BWShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostKindTblEntry AVX512ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v8f64,  { 1, 1, 1, 1 } }, // vbroadcastsd
      {TTI::SK_Broadcast, MVT::v16f32, { 1, 1, 1, 1 } }, // vbroadcastss
      {TTI::SK_Broadcast, MVT::v8i64,  { 1, 1, 1, 1 } }, // vpbroadcastq
      {TTI::SK_Broadcast, MVT::v16i32, { 1, 1, 1, 1 } }, // vpbroadcastd
      {TTI::SK_Broadcast, MVT::v32i16, { 1, 1, 1, 1 } }, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v32f16, { 1, 1, 1, 1 } }, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v64i8,  { 1, 1, 1, 1 } }, // vpbroadcastb

      {TTI::SK_Reverse, MVT::v8f64,  { 1, 3, 1, 1 } }, // vpermpd
      {TTI::SK_Reverse, MVT::v16f32, { 1, 3, 1, 1 } }, // vpermps
      {TTI::SK_Reverse, MVT::v8i64,  { 1, 3, 1, 1 } }, // vpermq
      {TTI::SK_Reverse, MVT::v16i32, { 1, 3, 1, 1 } }, // vpermd
      {TTI::SK_Reverse, MVT::v32i16, { 7, 7, 7, 7 } }, // per mca
      {TTI::SK_Reverse, MVT::v32f16, { 7, 7, 7, 7 } }, // per mca
      {TTI::SK_Reverse, MVT::v64i8,  { 7, 7, 7, 7 } }, // per mca

      {TTI::SK_Splice, MVT::v8f64,  { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v4f64,  { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v16f32, { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v8f32,  { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v8i64,  { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v4i64,  { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v16i32, { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v8i32,  { 1, 1, 1, 1 } }, // vpalignd
      {TTI::SK_Splice, MVT::v32i16, { 4, 4, 4, 4 } }, // split + palignr
      {TTI::SK_Splice, MVT::v32f16, { 4, 4, 4, 4 } }, // split + palignr
      {TTI::SK_Splice, MVT::v64i8,  { 4, 4, 4, 4 } }, // split + palignr

      {TTI::SK_PermuteSingleSrc, MVT::v8f64,  { 1, 3, 1, 1 } }, // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v4f64,  { 1, 3, 1, 1 } }, // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v2f64,  { 1, 3, 1, 1 } }, // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v16f32, { 1, 3, 1, 1 } }, // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v8f32,  { 1, 3, 1, 1 } }, // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v4f32,  { 1, 3, 1, 1 } }, // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v8i64,  { 1, 3, 1, 1 } }, // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v4i64,  { 1, 3, 1, 1 } }, // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v2i64,  { 1, 3, 1, 1 } }, // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v16i32, { 1, 3, 1, 1 } }, // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v8i32,  { 1, 3, 1, 1 } }, // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v4i32,  { 1, 3, 1, 1 } }, // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v16i8,  { 1, 3, 1, 1 } }, // pshufb

      {TTI::SK_PermuteTwoSrc, MVT::v8f64,  { 1, 3, 1, 1 } }, // vpermt2pd
      {TTI::SK_PermuteTwoSrc, MVT::v16f32, { 1, 3, 1, 1 } }, // vpermt2ps
      {TTI::SK_PermuteTwoSrc, MVT::v8i64,  { 1, 3, 1, 1 } }, // vpermt2q
      {TTI::SK_PermuteTwoSrc, MVT::v16i32, { 1, 3, 1, 1 } }, // vpermt2d
      {TTI::SK_PermuteTwoSrc, MVT::v4f64,  { 1, 3, 1, 1 } }, // vpermt2pd
      {TTI::SK_PermuteTwoSrc, MVT::v8f32,  { 1, 3, 1, 1 } }, // vpermt2ps
      {TTI::SK_PermuteTwoSrc, MVT::v4i64,  { 1, 3, 1, 1 } }, // vpermt2q
      {TTI::SK_PermuteTwoSrc, MVT::v8i32,  { 1, 3, 1, 1 } }, // vpermt2d
      {TTI::SK_PermuteTwoSrc, MVT::v2f64,  { 1, 3, 1, 1 } }, // vpermt2pd
      {TTI::SK_PermuteTwoSrc, MVT::v4f32,  { 1, 3, 1, 1 } }, // vpermt2ps
      {TTI::SK_PermuteTwoSrc, MVT::v2i64,  { 1, 3, 1, 1 } }, // vpermt2q
      {TTI::SK_PermuteTwoSrc, MVT::v4i32,  { 1, 3, 1, 1 } }, // vpermt2d

      // FIXME: This just applies the type legalization cost rules above
      // assuming these completely split.
      {TTI::SK_PermuteSingleSrc, MVT::v32i16, { 14, 14, 14, 14 } },
      {TTI::SK_PermuteSingleSrc, MVT::v32f16, { 14, 14, 14, 14 } },
      {TTI::SK_PermuteSingleSrc, MVT::v64i8,  { 14, 14, 14, 14 } },
      {TTI::SK_PermuteTwoSrc,    MVT::v32i16, { 42, 42, 42, 42 } },
      {TTI::SK_PermuteTwoSrc,    MVT::v32f16, { 42, 42, 42, 42 } },
      {TTI::SK_PermuteTwoSrc,    MVT::v64i8,  { 42, 42, 42, 42 } },

      {TTI::SK_Select, MVT::v32i16, { 1, 1, 1, 1 } }, // vpternlogq
      {TTI::SK_Select, MVT::v32f16, { 1, 1, 1, 1 } }, // vpternlogq
      {TTI::SK_Select, MVT::v64i8,  { 1, 1, 1, 1 } }, // vpternlogq
      {TTI::SK_Select, MVT::v8f64,  { 1, 1, 1, 1 } }, // vblendmpd
      {TTI::SK_Select, MVT::v16f32, { 1, 1, 1, 1 } }, // vblendmps
      {TTI::SK_Select, MVT::v8i64,  { 1, 1, 1, 1 } }, // vblendmq
      {TTI::SK_Select, MVT::v16i32, { 1, 1, 1, 1 } }, // vblendmd
  };

  if (ST->hasAVX512())
    if (const auto *Entry = CostTableLookup(AVX512ShuffleTbl, Kind, LT.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * *KindCost;

  static const CostTblEntry AVX2ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v4f64, 1},  // vbroadcastpd
      {TTI::SK_Broadcast, MVT::v8f32, 1},  // vbroadcastps
      {TTI::SK_Broadcast, MVT::v4i64, 1},  // vpbroadcastq
      {TTI::SK_Broadcast, MVT::v8i32, 1},  // vpbroadcastd
      {TTI::SK_Broadcast, MVT::v16i16, 1}, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v16f16, 1}, // vpbroadcastw
      {TTI::SK_Broadcast, MVT::v32i8, 1},  // vpbroadcastb

      {TTI::SK_Reverse, MVT::v4f64, 1},  // vpermpd
      {TTI::SK_Reverse, MVT::v8f32, 1},  // vpermps
      {TTI::SK_Reverse, MVT::v4i64, 1},  // vpermq
      {TTI::SK_Reverse, MVT::v8i32, 1},  // vpermd
      {TTI::SK_Reverse, MVT::v16i16, 2}, // vperm2i128 + pshufb
      {TTI::SK_Reverse, MVT::v16f16, 2}, // vperm2i128 + pshufb
      {TTI::SK_Reverse, MVT::v32i8, 2},  // vperm2i128 + pshufb

      {TTI::SK_Select, MVT::v16i16, 1}, // vpblendvb
      {TTI::SK_Select, MVT::v16f16, 1}, // vpblendvb
      {TTI::SK_Select, MVT::v32i8,  1}, // vpblendvb

      {TTI::SK_Splice, MVT::v8i32,  2}, // vperm2i128 + vpalignr
      {TTI::SK_Splice, MVT::v8f32,  2}, // vperm2i128 + vpalignr
      {TTI::SK_Splice, MVT::v16i16, 2}, // vperm2i128 + vpalignr
      {TTI::SK_Splice, MVT::v16f16, 2}, // vperm2i128 + vpalignr
      {TTI::SK_Splice, MVT::v32i8,  2}, // vperm2i128 + vpalignr

      {TTI::SK_PermuteSingleSrc, MVT::v4f64, 1},  // vpermpd
      {TTI::SK_PermuteSingleSrc, MVT::v8f32, 1},  // vpermps
      {TTI::SK_PermuteSingleSrc, MVT::v4i64, 1},  // vpermq
      {TTI::SK_PermuteSingleSrc, MVT::v8i32, 1},  // vpermd
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 4}, // vperm2i128 + 2*vpshufb
                                                  // + vpblendvb
      {TTI::SK_PermuteSingleSrc, MVT::v16f16, 4}, // vperm2i128 + 2*vpshufb
                                                  // + vpblendvb
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 4},  // vperm2i128 + 2*vpshufb
                                                  // + vpblendvb

      {TTI::SK_PermuteTwoSrc, MVT::v4f64, 3},  // 2*vpermpd + vblendpd
      {TTI::SK_PermuteTwoSrc, MVT::v8f32, 3},  // 2*vpermps + vblendps
      {TTI::SK_PermuteTwoSrc, MVT::v4i64, 3},  // 2*vpermq + vpblendd
      {TTI::SK_PermuteTwoSrc, MVT::v8i32, 3},  // 2*vpermd + vpblendd
      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 7}, // 2*vperm2i128 + 4*vpshufb
                                               // + vpblendvb
      {TTI::SK_PermuteTwoSrc, MVT::v16f16, 7}, // 2*vperm2i128 + 4*vpshufb
                                               // + vpblendvb
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 7},  // 2*vperm2i128 + 4*vpshufb
                                               // + vpblendvb
  };

  if (ST->hasAVX2())
    if (const auto *Entry = CostTableLookup(AVX2ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry XOPShuffleTbl[] = {
      {TTI::SK_PermuteSingleSrc, MVT::v4f64, 2},  // vperm2f128 + vpermil2pd
      {TTI::SK_PermuteSingleSrc, MVT::v8f32, 2},  // vperm2f128 + vpermil2ps
      {TTI::SK_PermuteSingleSrc, MVT::v4i64, 2},  // vperm2f128 + vpermil2pd
      {TTI::SK_PermuteSingleSrc, MVT::v8i32, 2},  // vperm2f128 + vpermil2ps
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 4}, // vextractf128 + 2*vpperm
                                                  // + vinsertf128
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 4},  // vextractf128 + 2*vpperm
                                                  // + vinsertf128

      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 9}, // 2*vextractf128 + 6*vpperm
                                               // + vinsertf128
      {TTI::SK_PermuteTwoSrc, MVT::v8i16, 1},  // vpperm
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 9},  // 2*vextractf128 + 6*vpperm
                                               // + vinsertf128
      {TTI::SK_PermuteTwoSrc, MVT::v16i8, 1},  // vpperm
  };

  if (ST->hasXOP())
    if (const auto *Entry = CostTableLookup(XOPShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry AVX1ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v4f64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Broadcast, MVT::v8f32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Broadcast, MVT::v4i64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Broadcast, MVT::v8i32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Broadcast, MVT::v16i16, 3}, // vpshuflw + vpshufd + vinsertf128
      {TTI::SK_Broadcast, MVT::v16f16, 3}, // vpshuflw + vpshufd + vinsertf128
      {TTI::SK_Broadcast, MVT::v32i8, 2},  // vpshufb + vinsertf128

      {TTI::SK_Reverse, MVT::v4f64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Reverse, MVT::v8f32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Reverse, MVT::v4i64, 2},  // vperm2f128 + vpermilpd
      {TTI::SK_Reverse, MVT::v8i32, 2},  // vperm2f128 + vpermilps
      {TTI::SK_Reverse, MVT::v16i16, 4}, // vextractf128 + 2*pshufb
                                         // + vinsertf128
      {TTI::SK_Reverse, MVT::v16f16, 4}, // vextractf128 + 2*pshufb
                                         // + vinsertf128
      {TTI::SK_Reverse, MVT::v32i8, 4},  // vextractf128 + 2*pshufb
                                         // + vinsertf128

      {TTI::SK_Select, MVT::v4i64, 1},  // vblendpd
      {TTI::SK_Select, MVT::v4f64, 1},  // vblendpd
      {TTI::SK_Select, MVT::v8i32, 1},  // vblendps
      {TTI::SK_Select, MVT::v8f32, 1},  // vblendps
      {TTI::SK_Select, MVT::v16i16, 3}, // vpand + vpandn + vpor
      {TTI::SK_Select, MVT::v16f16, 3}, // vpand + vpandn + vpor
      {TTI::SK_Select, MVT::v32i8, 3},  // vpand + vpandn + vpor

      {TTI::SK_Splice, MVT::v4i64,  2}, // vperm2f128 + shufpd
      {TTI::SK_Splice, MVT::v4f64,  2}, // vperm2f128 + shufpd
      {TTI::SK_Splice, MVT::v8i32,  4}, // 2*vperm2f128 + 2*vshufps
      {TTI::SK_Splice, MVT::v8f32,  4}, // 2*vperm2f128 + 2*vshufps
      {TTI::SK_Splice, MVT::v16i16, 5}, // 2*vperm2f128 + 2*vpalignr + vinsertf128
      {TTI::SK_Splice, MVT::v16f16, 5}, // 2*vperm2f128 + 2*vpalignr + vinsertf128
      {TTI::SK_Splice, MVT::v32i8,  5}, // 2*vperm2f128 + 2*vpalignr + vinsertf128

      {TTI::SK_PermuteSingleSrc, MVT::v4f64, 2},  // vperm2f128 + vshufpd
      {TTI::SK_PermuteSingleSrc, MVT::v4i64, 2},  // vperm2f128 + vshufpd
      {TTI::SK_PermuteSingleSrc, MVT::v8f32, 4},  // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteSingleSrc, MVT::v8i32, 4},  // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteSingleSrc, MVT::v16i16, 8}, // vextractf128 + 4*pshufb
                                                  // + 2*por + vinsertf128
      {TTI::SK_PermuteSingleSrc, MVT::v16f16, 8}, // vextractf128 + 4*pshufb
                                                  // + 2*por + vinsertf128
      {TTI::SK_PermuteSingleSrc, MVT::v32i8, 8},  // vextractf128 + 4*pshufb
                                                  // + 2*por + vinsertf128

      {TTI::SK_PermuteTwoSrc, MVT::v4f64, 3},   // 2*vperm2f128 + vshufpd
      {TTI::SK_PermuteTwoSrc, MVT::v4i64, 3},   // 2*vperm2f128 + vshufpd
      {TTI::SK_PermuteTwoSrc, MVT::v8f32, 4},   // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteTwoSrc, MVT::v8i32, 4},   // 2*vperm2f128 + 2*vshufps
      {TTI::SK_PermuteTwoSrc, MVT::v16i16, 15}, // 2*vextractf128 + 8*pshufb
                                                // + 4*por + vinsertf128
      {TTI::SK_PermuteTwoSrc, MVT::v16f16, 15}, // 2*vextractf128 + 8*pshufb
                                                // + 4*por + vinsertf128
      {TTI::SK_PermuteTwoSrc, MVT::v32i8, 15},  // 2*vextractf128 + 8*pshufb
                                                // + 4*por + vinsertf128
  };

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE41ShuffleTbl[] = {
      {TTI::SK_Select, MVT::v2i64, 1}, // pblendw
      {TTI::SK_Select, MVT::v2f64, 1}, // movsd
      {TTI::SK_Select, MVT::v4i32, 1}, // pblendw
      {TTI::SK_Select, MVT::v4f32, 1}, // blendps
      {TTI::SK_Select, MVT::v8i16, 1}, // pblendw
      {TTI::SK_Select, MVT::v8f16, 1}, // pblendw
      {TTI::SK_Select, MVT::v16i8, 1}  // pblendvb
  };

  if (ST->hasSSE41())
    if (const auto *Entry = CostTableLookup(SSE41ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSSE3ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v8i16, 1}, // pshufb
      {TTI::SK_Broadcast, MVT::v8f16, 1}, // pshufb
      {TTI::SK_Broadcast, MVT::v16i8, 1}, // pshufb

      {TTI::SK_Reverse, MVT::v8i16, 1}, // pshufb
      {TTI::SK_Reverse, MVT::v8f16, 1}, // pshufb
      {TTI::SK_Reverse, MVT::v16i8, 1}, // pshufb

      {TTI::SK_Select, MVT::v8i16, 3}, // 2*pshufb + por
      {TTI::SK_Select, MVT::v8f16, 3}, // 2*pshufb + por
      {TTI::SK_Select, MVT::v16i8, 3}, // 2*pshufb + por

      {TTI::SK_Splice, MVT::v4i32, 1}, // palignr
      {TTI::SK_Splice, MVT::v4f32, 1}, // palignr
      {TTI::SK_Splice, MVT::v8i16, 1}, // palignr
      {TTI::SK_Splice, MVT::v8f16, 1}, // palignr
      {TTI::SK_Splice, MVT::v16i8, 1}, // palignr

      {TTI::SK_PermuteSingleSrc, MVT::v8i16, 1}, // pshufb
      {TTI::SK_PermuteSingleSrc, MVT::v8f16, 1}, // pshufb
      {TTI::SK_PermuteSingleSrc, MVT::v16i8, 1}, // pshufb

      {TTI::SK_PermuteTwoSrc, MVT::v8i16, 3}, // 2*pshufb + por
      {TTI::SK_PermuteTwoSrc, MVT::v8f16, 3}, // 2*pshufb + por
      {TTI::SK_PermuteTwoSrc, MVT::v16i8, 3}, // 2*pshufb + por
  };

  if (ST->hasSSSE3())
    if (const auto *Entry = CostTableLookup(SSSE3ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  static const CostTblEntry SSE2ShuffleTbl[] = {
      {TTI::SK_Broadcast, MVT::v2f64, 1}, // shufpd
      {TTI::SK_Broadcast, MVT::v2i64, 1}, // pshufd
      {TTI::SK_Broadcast, MVT::v4i32, 1}, // pshufd
      {TTI::SK_Broadcast, MVT::v8i16, 2}, // pshuflw + pshufd
      {TTI::SK_Broadcast, MVT::v8f16, 2}, // pshuflw + pshufd
      {TTI::SK_Broadcast, MVT::v16i8, 3}, // unpck + pshuflw + pshufd

      {TTI::SK_Reverse, MVT::v2f64, 1}, // shufpd
      {TTI::SK_Reverse, MVT::v2i64, 1}, // pshufd
      {TTI::SK_Reverse, MVT::v4i32, 1}, // pshufd
      {TTI::SK_Reverse, MVT::v8i16, 3}, // pshuflw + pshufhw + pshufd
      {TTI::SK_Reverse, MVT::v8f16, 3}, // pshuflw + pshufhw + pshufd
      {TTI::SK_Reverse, MVT::v16i8, 9}, // 2*pshuflw + 2*pshufhw
                                        // + 2*pshufd + 2*unpck + packus

      {TTI::SK_Select, MVT::v2i64, 1}, // movsd
      {TTI::SK_Select, MVT::v2f64, 1}, // movsd
      {TTI::SK_Select, MVT::v4i32, 2}, // 2*shufps
      {TTI::SK_Select, MVT::v8i16, 3}, // pand + pandn + por
      {TTI::SK_Select, MVT::v8f16, 3}, // pand + pandn + por
      {TTI::SK_Select, MVT::v16i8, 3}, // pand + pandn + por

      {TTI::SK_Splice, MVT::v2i64, 1}, // shufpd
      {TTI::SK_Splice, MVT::v2f64, 1}, // shufpd
      {TTI::SK_Splice, MVT::v4i32, 2}, // 2*{unpck,movsd,pshufd}
      {TTI::SK_Splice, MVT::v8i16, 3}, // psrldq + psrlldq + por
      {TTI::SK_Splice, MVT::v8f16, 3}, // psrldq + psrlldq + por
      {TTI::SK_Splice, MVT::v16i8, 3}, // psrldq + psrlldq + por

      {TTI::SK_PermuteSingleSrc, MVT::v2f64, 1}, // shufpd
      {TTI::SK_PermuteSingleSrc, MVT::v2i64, 1}, // pshufd
      {TTI::SK_PermuteSingleSrc, MVT::v4i32, 1}, // pshufd
      {TTI::SK_PermuteSingleSrc, MVT::v8i16, 5}, // 2*pshuflw + 2*pshufhw
                                                  // + pshufd/unpck
      {TTI::SK_PermuteSingleSrc, MVT::v8f16, 5}, // 2*pshuflw + 2*pshufhw
                                                  // + pshufd/unpck
    { TTI::SK_PermuteSingleSrc, MVT::v16i8, 10 }, // 2*pshuflw + 2*pshufhw
                                                  // + 2*pshufd + 2*unpck + 2*packus

    { TTI::SK_PermuteTwoSrc,    MVT::v2f64,  1 }, // shufpd
    { TTI::SK_PermuteTwoSrc,    MVT::v2i64,  1 }, // shufpd
    { TTI::SK_PermuteTwoSrc,    MVT::v4i32,  2 }, // 2*{unpck,movsd,pshufd}
    { TTI::SK_PermuteTwoSrc,    MVT::v8i16,  8 }, // blend+permute
    { TTI::SK_PermuteTwoSrc,    MVT::v8f16,  8 }, // blend+permute
    { TTI::SK_PermuteTwoSrc,    MVT::v16i8, 13 }, // blend+permute
  };

  static const CostTblEntry SSE3BroadcastLoadTbl[] = {
      {TTI::SK_Broadcast, MVT::v2f64, 0}, // broadcast handled by movddup
  };

  if (ST->hasSSE2()) {
    bool IsLoad =
        llvm::any_of(Args, [](const auto &V) { return isa<LoadInst>(V); });
    if (ST->hasSSE3() && IsLoad)
      if (const auto *Entry =
              CostTableLookup(SSE3BroadcastLoadTbl, Kind, LT.second)) {
        assert(isLegalBroadcastLoad(BaseTp->getElementType(),
                                    LT.second.getVectorElementCount()) &&
               "Table entry missing from isLegalBroadcastLoad()");
        return LT.first * Entry->Cost;
      }

    if (const auto *Entry = CostTableLookup(SSE2ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;
  }

  static const CostTblEntry SSE1ShuffleTbl[] = {
    { TTI::SK_Broadcast,        MVT::v4f32, 1 }, // shufps
    { TTI::SK_Reverse,          MVT::v4f32, 1 }, // shufps
    { TTI::SK_Select,           MVT::v4f32, 2 }, // 2*shufps
    { TTI::SK_Splice,           MVT::v4f32, 2 }, // 2*shufps
    { TTI::SK_PermuteSingleSrc, MVT::v4f32, 1 }, // shufps
    { TTI::SK_PermuteTwoSrc,    MVT::v4f32, 2 }, // 2*shufps
  };

  if (ST->hasSSE1())
    if (const auto *Entry = CostTableLookup(SSE1ShuffleTbl, Kind, LT.second))
      return LT.first * Entry->Cost;

  return BaseT::getShuffleCost(Kind, BaseTp, Mask, CostKind, Index, SubTp);
}

InstructionCost X86TTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst,
                                             Type *Src,
                                             TTI::CastContextHint CCH,
                                             TTI::TargetCostKind CostKind,
                                             const Instruction *I) {
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // The cost tables include both specific, custom (non-legal) src/dst type
  // conversions and generic, legalized types. We test for customs first, before
  // falling back to legalization.
  // FIXME: Need a better design of the cost table to handle non-simple types of
  // potential massive combinations (elem_num x src_type x dst_type).
  static const TypeConversionCostKindTblEntry AVX512BWConversionTbl[]{
    { ISD::SIGN_EXTEND, MVT::v32i16, MVT::v32i8,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v32i16, MVT::v32i8,  { 1, 1, 1, 1 } },

    // Mask sign extend has an instruction.
    { ISD::SIGN_EXTEND, MVT::v2i8,   MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i16,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i8,   MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i16,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i8,   MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v32i8,  MVT::v32i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v32i16, MVT::v32i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v64i8,  MVT::v64i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v32i16, MVT::v64i1,  { 1, 1, 1, 1 } },

    // Mask zero extend is a sext + shift.
    { ISD::ZERO_EXTEND, MVT::v2i8,   MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i16,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i8,   MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i16,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i8,   MVT::v8i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v8i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v8i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v16i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v32i8,  MVT::v32i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v32i16, MVT::v32i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v64i8,  MVT::v64i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v32i16, MVT::v64i1,  { 2, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i8,   { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i8,   { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i8,   { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v16i16, { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v32i1,  MVT::v32i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v32i1,  MVT::v32i16, { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v64i1,  MVT::v64i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v64i1,  MVT::v32i16, { 2, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v32i8,  MVT::v32i16, { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i16, { 2, 1, 1, 1 } }, // widen to zmm
    { ISD::TRUNCATE,    MVT::v2i8,   MVT::v2i16,  { 2, 1, 1, 1 } }, // vpmovwb
    { ISD::TRUNCATE,    MVT::v4i8,   MVT::v4i16,  { 2, 1, 1, 1 } }, // vpmovwb
    { ISD::TRUNCATE,    MVT::v8i8,   MVT::v8i16,  { 2, 1, 1, 1 } }, // vpmovwb
  };

  static const TypeConversionCostKindTblEntry AVX512DQConversionTbl[] = {
    // Mask sign extend has an instruction.
    { ISD::SIGN_EXTEND, MVT::v2i64,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i1,  { 1, 1, 1, 1 } },

    // Mask zero extend is a sext + shift.
    { ISD::ZERO_EXTEND, MVT::v2i64,  MVT::v2i1,   { 2, 1, 1, 1, } },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v2i1,   { 2, 1, 1, 1, } },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i1,   { 2, 1, 1, 1, } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,   { 2, 1, 1, 1, } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,   { 2, 1, 1, 1, } },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v16i1,  { 2, 1, 1, 1, } },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i1,   { 2, 1, 1, 1, } },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i1,  { 2, 1, 1, 1, } },

    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i64,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i64,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i64,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v16i32, { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v8i64,  { 2, 1, 1, 1 } },

    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i64,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i64,  { 1, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i64,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i64,  { 1, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::v8i64,  MVT::v8f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i64,  MVT::v8f64,  { 1, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::v8i64,  MVT::v8f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i64,  MVT::v8f64,  { 1, 1, 1, 1 } },
  };

  // TODO: For AVX512DQ + AVX512VL, we also have cheap casts for 128-bit and
  // 256-bit wide vectors.

  static const TypeConversionCostKindTblEntry AVX512FConversionTbl[] = {
    { ISD::FP_EXTEND, MVT::v8f64,   MVT::v8f32,   { 1, 1, 1, 1 } },
    { ISD::FP_EXTEND, MVT::v8f64,   MVT::v16f32,  { 3, 1, 1, 1 } },
    { ISD::FP_EXTEND, MVT::v16f64,  MVT::v16f32,  { 4, 1, 1, 1 } }, // 2*vcvtps2pd+vextractf64x4
    { ISD::FP_ROUND,  MVT::v8f32,   MVT::v8f64,   { 1, 1, 1, 1 } },

    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i8,    { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i8,    { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v8i1,    MVT::v8i8,    { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v16i1,   MVT::v16i8,   { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i16,   { 3, 1, 1, 1 } }, // sext+vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i16,   { 3, 1, 1, 1 } }, // sext+vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v8i1,    MVT::v8i16,   { 3, 1, 1, 1 } }, // sext+vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v16i1,   MVT::v16i16,  { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i32,   { 2, 1, 1, 1 } }, // zmm vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i32,   { 2, 1, 1, 1 } }, // zmm vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v8i1,    MVT::v8i32,   { 2, 1, 1, 1 } }, // zmm vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v16i1,   MVT::v16i32,  { 2, 1, 1, 1 } }, // vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i64,   { 2, 1, 1, 1 } }, // zmm vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i64,   { 2, 1, 1, 1 } }, // zmm vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v8i1,    MVT::v8i64,   { 2, 1, 1, 1 } }, // vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v2i8,    MVT::v2i32,   { 2, 1, 1, 1 } }, // vpmovdb
    { ISD::TRUNCATE,  MVT::v4i8,    MVT::v4i32,   { 2, 1, 1, 1 } }, // vpmovdb
    { ISD::TRUNCATE,  MVT::v16i8,   MVT::v16i32,  { 2, 1, 1, 1 } }, // vpmovdb
    { ISD::TRUNCATE,  MVT::v32i8,   MVT::v16i32,  { 2, 1, 1, 1 } }, // vpmovdb
    { ISD::TRUNCATE,  MVT::v64i8,   MVT::v16i32,  { 2, 1, 1, 1 } }, // vpmovdb
    { ISD::TRUNCATE,  MVT::v16i16,  MVT::v16i32,  { 2, 1, 1, 1 } }, // vpmovdw
    { ISD::TRUNCATE,  MVT::v32i16,  MVT::v16i32,  { 2, 1, 1, 1 } }, // vpmovdw
    { ISD::TRUNCATE,  MVT::v2i8,    MVT::v2i64,   { 2, 1, 1, 1 } }, // vpmovqb
    { ISD::TRUNCATE,  MVT::v2i16,   MVT::v2i64,   { 1, 1, 1, 1 } }, // vpshufb
    { ISD::TRUNCATE,  MVT::v8i8,    MVT::v8i64,   { 2, 1, 1, 1 } }, // vpmovqb
    { ISD::TRUNCATE,  MVT::v16i8,   MVT::v8i64,   { 2, 1, 1, 1 } }, // vpmovqb
    { ISD::TRUNCATE,  MVT::v32i8,   MVT::v8i64,   { 2, 1, 1, 1 } }, // vpmovqb
    { ISD::TRUNCATE,  MVT::v64i8,   MVT::v8i64,   { 2, 1, 1, 1 } }, // vpmovqb
    { ISD::TRUNCATE,  MVT::v8i16,   MVT::v8i64,   { 2, 1, 1, 1 } }, // vpmovqw
    { ISD::TRUNCATE,  MVT::v16i16,  MVT::v8i64,   { 2, 1, 1, 1 } }, // vpmovqw
    { ISD::TRUNCATE,  MVT::v32i16,  MVT::v8i64,   { 2, 1, 1, 1 } }, // vpmovqw
    { ISD::TRUNCATE,  MVT::v8i32,   MVT::v8i64,   { 1, 1, 1, 1 } }, // vpmovqd
    { ISD::TRUNCATE,  MVT::v4i32,   MVT::v4i64,   { 1, 1, 1, 1 } }, // zmm vpmovqd
    { ISD::TRUNCATE,  MVT::v16i8,   MVT::v16i64,  { 5, 1, 1, 1 } },// 2*vpmovqd+concat+vpmovdb

    { ISD::TRUNCATE,  MVT::v16i8,  MVT::v16i16,   { 3, 1, 1, 1 } }, // extend to v16i32
    { ISD::TRUNCATE,  MVT::v32i8,  MVT::v32i16,   { 8, 1, 1, 1 } },
    { ISD::TRUNCATE,  MVT::v64i8,  MVT::v32i16,   { 8, 1, 1, 1 } },

    // Sign extend is zmm vpternlogd+vptruncdb.
    // Zero extend is zmm broadcast load+vptruncdw.
    { ISD::SIGN_EXTEND, MVT::v2i8,   MVT::v2i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i8,   MVT::v2i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i8,   MVT::v4i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i8,   MVT::v4i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i8,   MVT::v8i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i8,   MVT::v8i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v16i1,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v16i1,  { 4, 1, 1, 1 } },

    // Sign extend is zmm vpternlogd+vptruncdw.
    // Zero extend is zmm vpternlogd+vptruncdw+vpsrlw.
    { ISD::SIGN_EXTEND, MVT::v2i16,  MVT::v2i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i16,  MVT::v2i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i16,  MVT::v4i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i16,  MVT::v4i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v8i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v8i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i1,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i1,  { 4, 1, 1, 1 } },

    { ISD::SIGN_EXTEND, MVT::v2i32,  MVT::v2i1,   { 1, 1, 1, 1 } }, // zmm vpternlogd
    { ISD::ZERO_EXTEND, MVT::v2i32,  MVT::v2i1,   { 2, 1, 1, 1 } }, // zmm vpternlogd+psrld
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i1,   { 1, 1, 1, 1 } }, // zmm vpternlogd
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i1,   { 2, 1, 1, 1 } }, // zmm vpternlogd+psrld
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,   { 1, 1, 1, 1 } }, // zmm vpternlogd
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,   { 2, 1, 1, 1 } }, // zmm vpternlogd+psrld
    { ISD::SIGN_EXTEND, MVT::v2i64,  MVT::v2i1,   { 1, 1, 1, 1 } }, // zmm vpternlogq
    { ISD::ZERO_EXTEND, MVT::v2i64,  MVT::v2i1,   { 2, 1, 1, 1 } }, // zmm vpternlogq+psrlq
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,   { 1, 1, 1, 1 } }, // zmm vpternlogq
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,   { 2, 1, 1, 1 } }, // zmm vpternlogq+psrlq

    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i1,  { 1, 1, 1, 1 } }, // vpternlogd
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i1,  { 2, 1, 1, 1 } }, // vpternlogd+psrld
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i1,   { 1, 1, 1, 1 } }, // vpternlogq
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i1,   { 2, 1, 1, 1 } }, // vpternlogq+psrlq

    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i16, { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i16, { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i8,   { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i8,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i64,  MVT::v8i32,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i64,  MVT::v8i32,  { 1, 1, 1, 1 } },

    { ISD::SIGN_EXTEND, MVT::v32i16, MVT::v32i8,  { 3, 1, 1, 1 } }, // FIXME: May not be right
    { ISD::ZERO_EXTEND, MVT::v32i16, MVT::v32i8,  { 3, 1, 1, 1 } }, // FIXME: May not be right

    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i1,   { 4, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i1,  { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i16, { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i32, { 1, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i1,   { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i1,  { 3, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i16, { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i32, { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i64,  {26, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i64,  { 5, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v16f32, { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v16f64, { 7, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v32i8,  MVT::v32f64, {15, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v64i8,  MVT::v64f32, {11, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v64i8,  MVT::v64f64, {31, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i16,  MVT::v8f64,  { 3, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i16, MVT::v16f64, { 7, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v32i16, MVT::v32f32, { 5, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v32i16, MVT::v32f64, {15, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i32,  MVT::v8f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i32, MVT::v16f64, { 3, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v8f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v8f64,  { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i8,   MVT::v8f64,  { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i32, MVT::v16f32, { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i16, MVT::v16f32, { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v16f32, { 3, 1, 1, 1 } },
  };

  static const TypeConversionCostKindTblEntry AVX512BWVLConversionTbl[] {
    // Mask sign extend has an instruction.
    { ISD::SIGN_EXTEND, MVT::v2i8,   MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i16,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i16,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i8,   MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i8,   MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v32i8,  MVT::v32i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v32i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v32i8,  MVT::v64i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v64i1,  { 1, 1, 1, 1 } },

    // Mask zero extend is a sext + shift.
    { ISD::ZERO_EXTEND, MVT::v2i8,   MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i16,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i8,   MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i16,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i8,   MVT::v8i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v8i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v8i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v16i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v32i8,  MVT::v32i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v32i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v32i8,  MVT::v64i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v64i1,  { 2, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i8,   { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i8,   { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i8,   { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v16i16, { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v32i1,  MVT::v32i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v32i1,  MVT::v16i16, { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v64i1,  MVT::v32i8,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v64i1,  MVT::v16i16, { 2, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i16, { 2, 1, 1, 1 } },
  };

  static const TypeConversionCostKindTblEntry AVX512DQVLConversionTbl[] = {
    // Mask sign extend has an instruction.
    { ISD::SIGN_EXTEND, MVT::v2i64,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v2i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v8i1,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,   { 1, 1, 1, 1 } },

    // Mask zero extend is a sext + shift.
    { ISD::ZERO_EXTEND, MVT::v2i64,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v2i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v16i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v8i1,   { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v16i1,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,   { 2, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v4i64,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v8i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i64,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i64,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v4i64,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i32,  { 2, 1, 1, 1 } },

    { ISD::SINT_TO_FP,  MVT::v2f32,  MVT::v2i64,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v4i64,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v4i64,  { 1, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i64,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i64,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i64,  { 1, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::v2i64,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i64,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v2i64,  MVT::v2f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i64,  MVT::v4f64,  { 1, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::v2i64,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i64,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v2i64,  MVT::v2f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i64,  MVT::v4f64,  { 1, 1, 1, 1 } },
  };

  static const TypeConversionCostKindTblEntry AVX512VLConversionTbl[] = {
    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i8,    { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i8,    { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v8i1,    MVT::v8i8,    { 3, 1, 1, 1 } }, // sext+vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v16i1,   MVT::v16i8,   { 8, 1, 1, 1 } }, // split+2*v8i8
    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i16,   { 3, 1, 1, 1 } }, // sext+vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i16,   { 3, 1, 1, 1 } }, // sext+vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v8i1,    MVT::v8i16,   { 3, 1, 1, 1 } }, // sext+vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v16i1,   MVT::v16i16,  { 8, 1, 1, 1 } }, // split+2*v8i16
    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i32,   { 2, 1, 1, 1 } }, // vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i32,   { 2, 1, 1, 1 } }, // vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v8i1,    MVT::v8i32,   { 2, 1, 1, 1 } }, // vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v16i1,   MVT::v8i32,   { 2, 1, 1, 1 } }, // vpslld+vptestmd
    { ISD::TRUNCATE,  MVT::v2i1,    MVT::v2i64,   { 2, 1, 1, 1 } }, // vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v4i1,    MVT::v4i64,   { 2, 1, 1, 1 } }, // vpsllq+vptestmq
    { ISD::TRUNCATE,  MVT::v4i32,   MVT::v4i64,   { 1, 1, 1, 1 } }, // vpmovqd
    { ISD::TRUNCATE,  MVT::v4i8,    MVT::v4i64,   { 2, 1, 1, 1 } }, // vpmovqb
    { ISD::TRUNCATE,  MVT::v4i16,   MVT::v4i64,   { 2, 1, 1, 1 } }, // vpmovqw
    { ISD::TRUNCATE,  MVT::v8i8,    MVT::v8i32,   { 2, 1, 1, 1 } }, // vpmovwb

    // sign extend is vpcmpeq+maskedmove+vpmovdw+vpacksswb
    // zero extend is vpcmpeq+maskedmove+vpmovdw+vpsrlw+vpackuswb
    { ISD::SIGN_EXTEND, MVT::v2i8,   MVT::v2i1,   { 5, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i8,   MVT::v2i1,   { 6, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i8,   MVT::v4i1,   { 5, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i8,   MVT::v4i1,   { 6, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i8,   MVT::v8i1,   { 5, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i8,   MVT::v8i1,   { 6, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i8,  MVT::v16i1,  {10, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i8,  MVT::v16i1,  {12, 1, 1, 1 } },

    // sign extend is vpcmpeq+maskedmove+vpmovdw
    // zero extend is vpcmpeq+maskedmove+vpmovdw+vpsrlw
    { ISD::SIGN_EXTEND, MVT::v2i16,  MVT::v2i1,   { 4, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i16,  MVT::v2i1,   { 5, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i16,  MVT::v4i1,   { 4, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i16,  MVT::v4i1,   { 5, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v8i1,   { 4, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v8i1,   { 5, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i1,  {10, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i1,  {12, 1, 1, 1 } },

    { ISD::SIGN_EXTEND, MVT::v2i32,  MVT::v2i1,   { 1, 1, 1, 1 } }, // vpternlogd
    { ISD::ZERO_EXTEND, MVT::v2i32,  MVT::v2i1,   { 2, 1, 1, 1 } }, // vpternlogd+psrld
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v4i1,   { 1, 1, 1, 1 } }, // vpternlogd
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v4i1,   { 2, 1, 1, 1 } }, // vpternlogd+psrld
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,   { 1, 1, 1, 1 } }, // vpternlogd
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,   { 2, 1, 1, 1 } }, // vpternlogd+psrld
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v16i1,  { 1, 1, 1, 1 } }, // vpternlogd
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v16i1,  { 2, 1, 1, 1 } }, // vpternlogd+psrld

    { ISD::SIGN_EXTEND, MVT::v2i64,  MVT::v2i1,   { 1, 1, 1, 1 } }, // vpternlogq
    { ISD::ZERO_EXTEND, MVT::v2i64,  MVT::v2i1,   { 2, 1, 1, 1 } }, // vpternlogq+psrlq
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,   { 1, 1, 1, 1 } }, // vpternlogq
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,   { 2, 1, 1, 1 } }, // vpternlogq+psrlq

    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32,  { 1, 1, 1, 1 } },

    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i16,  { 1, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::f32,    MVT::i64,    { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i64,    { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i32,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i32,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i64,  { 5, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  { 5, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i64,  { 5, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v16f32, { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v32i8,  MVT::v32f32, { 5, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f32,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f64,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v2f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v8f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v8f64,  { 1, 1, 1, 1 } },
  };

  static const TypeConversionCostKindTblEntry AVX2ConversionTbl[] = {
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,   { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,   { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,   { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i1,  { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i1,  { 1, 1, 1, 1 } },

    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i16, { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i16, { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32,  { 2, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i32,  { 2, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v16i16, MVT::v16i32, { 4, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i32, { 4, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v2i64,  { 1, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v8i32,  { 4, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v4i64,  { 4, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v2i64,  { 1, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v4i64,  { 5, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v4i32,  MVT::v4i64,  { 1, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v8i32,  { 2, 1, 1, 1 } },

    { ISD::FP_EXTEND,   MVT::v8f64,  MVT::v8f32,  { 3, 1, 1, 1 } },
    { ISD::FP_ROUND,    MVT::v8f32,  MVT::v8f64,  { 3, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::v16i16, MVT::v8f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i32,  MVT::v4f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i32,  MVT::v8f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i32,  MVT::v8f64,  { 3, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f32,    { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f64,    { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i16, MVT::v8f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f32,  { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v2f64,  { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f64,  { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v8f32,  { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v4f64,  { 4, 1, 1, 1 } },

    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i32,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  { 3, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i32,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i32,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i32,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  { 4, 1, 1, 1 } },
  };

  static const TypeConversionCostKindTblEntry AVXConversionTbl[] = {
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i1,   { 4, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i1,   { 4, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i1,   { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i1,  { 4, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i1,  { 4, 1, 1, 1 } },

    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v8i16,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v8i16,  { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i32,  MVT::v8i16,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i32,  MVT::v8i16,  { 3, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i64,  MVT::v4i32,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i64,  MVT::v4i32,  { 3, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i64,  { 4, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i32,  { 5, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v16i16, { 4, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i64,  { 9, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i1,  MVT::v16i64, {11, 1, 1, 1 } },

    { ISD::TRUNCATE,    MVT::v16i16, MVT::v16i32, { 6, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i32, { 6, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i16, { 2, 1, 1, 1 } }, // and+extract+packuswb
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v8i32,  { 5, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v8i32,  { 5, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v4i64,  { 5, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v4i64,  { 3, 1, 1, 1 } }, // and+extract+2*packusdw
    { ISD::TRUNCATE,    MVT::v4i32,  MVT::v4i64,  { 2, 1, 1, 1 } },

    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v4i1,   { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v4i1,   { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i1,   { 8, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v16i8,  { 4, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i16,  { 4, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f32,  MVT::v8i32,  { 2, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  { 4, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v2i64,  { 5, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v4i64,  { 8, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i1,   { 7, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i1,   { 7, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i1,   { 6, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v16i8,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i16,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i32,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i32,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  { 5, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i32,  { 6, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f32,  MVT::v8i32,  { 8, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v8f64,  MVT::v8i32,  {10, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i64,  {10, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i64,  {18, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  { 5, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f64,  MVT::v4i64,  {10, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v32i8,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v32i8,  MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i16,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i16,  MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i16, MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i16, MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i32,  MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i32,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i32,  MVT::v8f64,  { 5, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v32i8,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v32i8,  MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i16, MVT::v8f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i16, MVT::v4f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f32,  { 3, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v2f64,  { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f64,  { 6, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v8f32,  { 7, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i32,  MVT::v4f64,  { 7, 1, 1, 1 } },

    { ISD::FP_EXTEND,   MVT::v4f64,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_ROUND,    MVT::v4f32,  MVT::v4f64,  { 1, 1, 1, 1 } },
  };

  static const TypeConversionCostKindTblEntry SSE41ConversionTbl[] = {
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v16i8,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v16i8,   { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i32, MVT::v16i8,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32, MVT::v16i8,   { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16, MVT::v16i8,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16, MVT::v16i8,   { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v8i16,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v8i16,   { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i32, MVT::v8i16,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32, MVT::v8i16,   { 1, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v4i32,   { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v4i32,   { 1, 1, 1, 1 } },

    // These truncates end up widening elements.
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i8,   { 1, 1, 1, 1 } }, // PMOVXZBQ
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i16,  { 1, 1, 1, 1 } }, // PMOVXZWQ
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i8,   { 1, 1, 1, 1 } }, // PMOVXZBD

    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v2i64,  { 2, 1, 1, 1 } },

    { ISD::SINT_TO_FP,  MVT::f32,    MVT::i32,    { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::f64,    MVT::i32,    { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::f32,    MVT::i64,    { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::f64,    MVT::i64,    { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f64,  MVT::v4i32,  { 2, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::f32,    MVT::i32,    { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i32,    { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::f32,    MVT::i64,    { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i64,    { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i32,  { 3, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  { 3, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v4i32,  { 2, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v2i64,  {12, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i64,  {22, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  { 4, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::i32,    MVT::f32,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::i64,    MVT::f32,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::i32,    MVT::f64,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::i64,    MVT::f64,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v4f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v2f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i16,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i16,  MVT::v2f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i32,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i32,  MVT::v2f64,  { 1, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::i32,    MVT::f32,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f32,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i32,    MVT::f64,    { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f64,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v4f32,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v2f64,  { 2, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v4f32,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v2f64,  { 1, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f32,  { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v2f64,  { 4, 1, 1, 1 } },
  };

  static const TypeConversionCostKindTblEntry SSE2ConversionTbl[] = {
    // These are somewhat magic numbers justified by comparing the
    // output of llvm-mca for our various supported scheduler models
    // and basing it off the worst case scenario.
    { ISD::SINT_TO_FP,  MVT::f32,    MVT::i32,    { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::f64,    MVT::i32,    { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::f32,    MVT::i64,    { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::f64,    MVT::i64,    { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 4, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v8i16,  { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 4, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  { 3, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v4i32,  { 4, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v4f32,  MVT::v2i64,  { 8, 1, 1, 1 } },
    { ISD::SINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  { 8, 1, 1, 1 } },

    { ISD::UINT_TO_FP,  MVT::f32,    MVT::i32,    { 3, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i32,    { 3, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::f32,    MVT::i64,    { 8, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::f64,    MVT::i64,    { 9, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v16i8,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v16i8,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v8i16,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v8i16,  { 4, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f32,  MVT::v2i32,  { 7, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v4i32,  { 7, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v4i32,  { 5, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v2f64,  MVT::v2i64,  {15, 1, 1, 1 } },
    { ISD::UINT_TO_FP,  MVT::v4f32,  MVT::v2i64,  {18, 1, 1, 1 } },

    { ISD::FP_TO_SINT,  MVT::i32,    MVT::f32,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::i64,    MVT::f32,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::i32,    MVT::f64,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::i64,    MVT::f64,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v4f32,  { 6, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v16i8,  MVT::v2f64,  { 6, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i16,  MVT::v4f32,  { 5, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v8i16,  MVT::v2f64,  { 5, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i32,  MVT::v4f32,  { 4, 1, 1, 1 } },
    { ISD::FP_TO_SINT,  MVT::v4i32,  MVT::v2f64,  { 4, 1, 1, 1 } },

    { ISD::FP_TO_UINT,  MVT::i32,    MVT::f32,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f32,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i32,    MVT::f64,    { 4, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::i64,    MVT::f64,    {15, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v4f32,  { 6, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v16i8,  MVT::v2f64,  { 6, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v4f32,  { 5, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v8i16,  MVT::v2f64,  { 5, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v4f32,  { 8, 1, 1, 1 } },
    { ISD::FP_TO_UINT,  MVT::v4i32,  MVT::v2f64,  { 8, 1, 1, 1 } },

    { ISD::ZERO_EXTEND, MVT::v2i64,  MVT::v16i8,  { 4, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i64,  MVT::v16i8,  { 4, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v16i8,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v8i16,  MVT::v16i8,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v8i16,  MVT::v16i8,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i64,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i64,  MVT::v8i16,  { 3, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v4i32,  MVT::v8i16,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v4i32,  MVT::v8i16,  { 2, 1, 1, 1 } },
    { ISD::ZERO_EXTEND, MVT::v2i64,  MVT::v4i32,  { 1, 1, 1, 1 } },
    { ISD::SIGN_EXTEND, MVT::v2i64,  MVT::v4i32,  { 2, 1, 1, 1 } },

    // These truncates are really widening elements.
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i32,  { 1, 1, 1, 1 } }, // PSHUFD
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i16,  { 2, 1, 1, 1 } }, // PUNPCKLWD+DQ
    { ISD::TRUNCATE,    MVT::v2i1,   MVT::v2i8,   { 3, 1, 1, 1 } }, // PUNPCKLBW+WD+PSHUFD
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i16,  { 1, 1, 1, 1 } }, // PUNPCKLWD
    { ISD::TRUNCATE,    MVT::v4i1,   MVT::v4i8,   { 2, 1, 1, 1 } }, // PUNPCKLBW+WD
    { ISD::TRUNCATE,    MVT::v8i1,   MVT::v8i8,   { 1, 1, 1, 1 } }, // PUNPCKLBW

    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v8i16,  { 2, 1, 1, 1 } }, // PAND+PACKUSWB
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i16, { 3, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v4i32,  { 3, 1, 1, 1 } }, // PAND+2*PACKUSWB
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v16i32, { 7, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v2i16,  MVT::v2i32,  { 1, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v4i32,  { 3, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v8i32,  { 5, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i16, MVT::v16i32, {10, 1, 1, 1 } },
    { ISD::TRUNCATE,    MVT::v16i8,  MVT::v2i64,  { 4, 1, 1, 1 } }, // PAND+3*PACKUSWB
    { ISD::TRUNCATE,    MVT::v8i16,  MVT::v2i64,  { 2, 1, 1, 1 } }, // PSHUFD+PSHUFLW
    { ISD::TRUNCATE,    MVT::v4i32,  MVT::v2i64,  { 1, 1, 1, 1 } }, // PSHUFD
  };

  // Attempt to map directly to (simple) MVT types to let us match custom entries.
  EVT SrcTy = TLI->getValueType(DL, Src);
  EVT DstTy = TLI->getValueType(DL, Dst);

  // The function getSimpleVT only handles simple value types.
  if (SrcTy.isSimple() && DstTy.isSimple()) {
    MVT SimpleSrcTy = SrcTy.getSimpleVT();
    MVT SimpleDstTy = DstTy.getSimpleVT();

    if (ST->useAVX512Regs()) {
      if (ST->hasBWI())
        if (const auto *Entry = ConvertCostTableLookup(
                AVX512BWConversionTbl, ISD, SimpleDstTy, SimpleSrcTy))
          if (auto KindCost = Entry->Cost[CostKind])
            return *KindCost;

      if (ST->hasDQI())
        if (const auto *Entry = ConvertCostTableLookup(
                AVX512DQConversionTbl, ISD, SimpleDstTy, SimpleSrcTy))
          if (auto KindCost = Entry->Cost[CostKind])
            return *KindCost;

      if (ST->hasAVX512())
        if (const auto *Entry = ConvertCostTableLookup(
                AVX512FConversionTbl, ISD, SimpleDstTy, SimpleSrcTy))
          if (auto KindCost = Entry->Cost[CostKind])
            return *KindCost;
    }

    if (ST->hasBWI())
      if (const auto *Entry = ConvertCostTableLookup(
              AVX512BWVLConversionTbl, ISD, SimpleDstTy, SimpleSrcTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return *KindCost;

    if (ST->hasDQI())
      if (const auto *Entry = ConvertCostTableLookup(
              AVX512DQVLConversionTbl, ISD, SimpleDstTy, SimpleSrcTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return *KindCost;

    if (ST->hasAVX512())
      if (const auto *Entry = ConvertCostTableLookup(AVX512VLConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return *KindCost;

    if (ST->hasAVX2()) {
      if (const auto *Entry = ConvertCostTableLookup(AVX2ConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return *KindCost;
    }

    if (ST->hasAVX()) {
      if (const auto *Entry = ConvertCostTableLookup(AVXConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return *KindCost;
    }

    if (ST->hasSSE41()) {
      if (const auto *Entry = ConvertCostTableLookup(SSE41ConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return *KindCost;
    }

    if (ST->hasSSE2()) {
      if (const auto *Entry = ConvertCostTableLookup(SSE2ConversionTbl, ISD,
                                                     SimpleDstTy, SimpleSrcTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return *KindCost;
    }
  }

  // Fall back to legalized types.
  std::pair<InstructionCost, MVT> LTSrc = getTypeLegalizationCost(Src);
  std::pair<InstructionCost, MVT> LTDest = getTypeLegalizationCost(Dst);

  // If we're truncating to the same legalized type - just assume its free.
  if (ISD == ISD::TRUNCATE && LTSrc.second == LTDest.second)
    return TTI::TCC_Free;

  if (ST->useAVX512Regs()) {
    if (ST->hasBWI())
      if (const auto *Entry = ConvertCostTableLookup(
              AVX512BWConversionTbl, ISD, LTDest.second, LTSrc.second))
        if (auto KindCost = Entry->Cost[CostKind])
          return std::max(LTSrc.first, LTDest.first) * *KindCost;

    if (ST->hasDQI())
      if (const auto *Entry = ConvertCostTableLookup(
              AVX512DQConversionTbl, ISD, LTDest.second, LTSrc.second))
        if (auto KindCost = Entry->Cost[CostKind])
          return std::max(LTSrc.first, LTDest.first) * *KindCost;

    if (ST->hasAVX512())
      if (const auto *Entry = ConvertCostTableLookup(
              AVX512FConversionTbl, ISD, LTDest.second, LTSrc.second))
        if (auto KindCost = Entry->Cost[CostKind])
          return std::max(LTSrc.first, LTDest.first) * *KindCost;
  }

  if (ST->hasBWI())
    if (const auto *Entry = ConvertCostTableLookup(AVX512BWVLConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return std::max(LTSrc.first, LTDest.first) * *KindCost;

  if (ST->hasDQI())
    if (const auto *Entry = ConvertCostTableLookup(AVX512DQVLConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return std::max(LTSrc.first, LTDest.first) * *KindCost;

  if (ST->hasAVX512())
    if (const auto *Entry = ConvertCostTableLookup(AVX512VLConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return std::max(LTSrc.first, LTDest.first) * *KindCost;

  if (ST->hasAVX2())
    if (const auto *Entry = ConvertCostTableLookup(AVX2ConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return std::max(LTSrc.first, LTDest.first) * *KindCost;

  if (ST->hasAVX())
    if (const auto *Entry = ConvertCostTableLookup(AVXConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return std::max(LTSrc.first, LTDest.first) * *KindCost;

  if (ST->hasSSE41())
    if (const auto *Entry = ConvertCostTableLookup(SSE41ConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return std::max(LTSrc.first, LTDest.first) * *KindCost;

  if (ST->hasSSE2())
    if (const auto *Entry = ConvertCostTableLookup(SSE2ConversionTbl, ISD,
                                                   LTDest.second, LTSrc.second))
      if (auto KindCost = Entry->Cost[CostKind])
        return std::max(LTSrc.first, LTDest.first) * *KindCost;

  // Fallback, for i8/i16 sitofp/uitofp cases we need to extend to i32 for
  // sitofp.
  if ((ISD == ISD::SINT_TO_FP || ISD == ISD::UINT_TO_FP) &&
      1 < Src->getScalarSizeInBits() && Src->getScalarSizeInBits() < 32) {
    Type *ExtSrc = Src->getWithNewBitWidth(32);
    unsigned ExtOpc =
        (ISD == ISD::SINT_TO_FP) ? Instruction::SExt : Instruction::ZExt;

    // For scalar loads the extend would be free.
    InstructionCost ExtCost = 0;
    if (!(Src->isIntegerTy() && I && isa<LoadInst>(I->getOperand(0))))
      ExtCost = getCastInstrCost(ExtOpc, ExtSrc, Src, CCH, CostKind);

    return ExtCost + getCastInstrCost(Instruction::SIToFP, Dst, ExtSrc,
                                      TTI::CastContextHint::None, CostKind);
  }

  // Fallback for fptosi/fptoui i8/i16 cases we need to truncate from fptosi
  // i32.
  if ((ISD == ISD::FP_TO_SINT || ISD == ISD::FP_TO_UINT) &&
      1 < Dst->getScalarSizeInBits() && Dst->getScalarSizeInBits() < 32) {
    Type *TruncDst = Dst->getWithNewBitWidth(32);
    return getCastInstrCost(Instruction::FPToSI, TruncDst, Src, CCH, CostKind) +
           getCastInstrCost(Instruction::Trunc, Dst, TruncDst,
                            TTI::CastContextHint::None, CostKind);
  }

  // TODO: Allow non-throughput costs that aren't binary.
  auto AdjustCost = [&CostKind](InstructionCost Cost,
                                InstructionCost N = 1) -> InstructionCost {
    if (CostKind != TTI::TCK_RecipThroughput)
      return Cost == 0 ? 0 : N;
    return Cost * N;
  };
  return AdjustCost(
      BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I));
}

InstructionCost X86TTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                               Type *CondTy,
                                               CmpInst::Predicate VecPred,
                                               TTI::TargetCostKind CostKind,
                                               const Instruction *I) {
  // Early out if this type isn't scalar/vector integer/float.
  if (!(ValTy->isIntOrIntVectorTy() || ValTy->isFPOrFPVectorTy()))
    return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind,
                                     I);

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);

  MVT MTy = LT.second;

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  InstructionCost ExtraCost = 0;
  if (Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) {
    // Some vector comparison predicates cost extra instructions.
    // TODO: Adjust ExtraCost based on CostKind?
    // TODO: Should we invert this and assume worst case cmp costs
    // and reduce for particular predicates?
    if (MTy.isVector() &&
        !((ST->hasXOP() && (!ST->hasAVX2() || MTy.is128BitVector())) ||
          (ST->hasAVX512() && 32 <= MTy.getScalarSizeInBits()) ||
          ST->hasBWI())) {
      // Fallback to I if a specific predicate wasn't specified.
      CmpInst::Predicate Pred = VecPred;
      if (I && (Pred == CmpInst::BAD_ICMP_PREDICATE ||
                Pred == CmpInst::BAD_FCMP_PREDICATE))
        Pred = cast<CmpInst>(I)->getPredicate();

      bool CmpWithConstant = false;
      if (auto *CmpInstr = dyn_cast_or_null<CmpInst>(I))
        CmpWithConstant = isa<Constant>(CmpInstr->getOperand(1));

      switch (Pred) {
      case CmpInst::Predicate::ICMP_NE:
        // xor(cmpeq(x,y),-1)
        ExtraCost = CmpWithConstant ? 0 : 1;
        break;
      case CmpInst::Predicate::ICMP_SGE:
      case CmpInst::Predicate::ICMP_SLE:
        // xor(cmpgt(x,y),-1)
        ExtraCost = CmpWithConstant ? 0 : 1;
        break;
      case CmpInst::Predicate::ICMP_ULT:
      case CmpInst::Predicate::ICMP_UGT:
        // cmpgt(xor(x,signbit),xor(y,signbit))
        // xor(cmpeq(pmaxu(x,y),x),-1)
        ExtraCost = CmpWithConstant ? 1 : 2;
        break;
      case CmpInst::Predicate::ICMP_ULE:
      case CmpInst::Predicate::ICMP_UGE:
        if ((ST->hasSSE41() && MTy.getScalarSizeInBits() == 32) ||
            (ST->hasSSE2() && MTy.getScalarSizeInBits() < 32)) {
          // cmpeq(psubus(x,y),0)
          // cmpeq(pminu(x,y),x)
          ExtraCost = 1;
        } else {
          // xor(cmpgt(xor(x,signbit),xor(y,signbit)),-1)
          ExtraCost = CmpWithConstant ? 2 : 3;
        }
        break;
      case CmpInst::Predicate::FCMP_ONE:
      case CmpInst::Predicate::FCMP_UEQ:
        // Without AVX we need to expand FCMP_ONE/FCMP_UEQ cases.
        // Use FCMP_UEQ expansion - FCMP_ONE should be the same.
        if (CondTy && !ST->hasAVX())
          return getCmpSelInstrCost(Opcode, ValTy, CondTy,
                                    CmpInst::Predicate::FCMP_UNO, CostKind) +
                 getCmpSelInstrCost(Opcode, ValTy, CondTy,
                                    CmpInst::Predicate::FCMP_OEQ, CostKind) +
                 getArithmeticInstrCost(Instruction::Or, CondTy, CostKind);

        break;
      case CmpInst::Predicate::BAD_ICMP_PREDICATE:
      case CmpInst::Predicate::BAD_FCMP_PREDICATE:
        // Assume worst case scenario and add the maximum extra cost.
        ExtraCost = 3;
        break;
      default:
        break;
      }
    }
  }

  static const CostKindTblEntry SLMCostTbl[] = {
    // slm pcmpeq/pcmpgt throughput is 2
    { ISD::SETCC,   MVT::v2i64,   { 2, 5, 1, 2 } },
    // slm pblendvb/blendvpd/blendvps throughput is 4
    { ISD::SELECT,  MVT::v2f64,   { 4, 4, 1, 3 } }, // vblendvpd
    { ISD::SELECT,  MVT::v4f32,   { 4, 4, 1, 3 } }, // vblendvps
    { ISD::SELECT,  MVT::v2i64,   { 4, 4, 1, 3 } }, // pblendvb
    { ISD::SELECT,  MVT::v8i32,   { 4, 4, 1, 3 } }, // pblendvb
    { ISD::SELECT,  MVT::v8i16,   { 4, 4, 1, 3 } }, // pblendvb
    { ISD::SELECT,  MVT::v16i8,   { 4, 4, 1, 3 } }, // pblendvb
  };

  static const CostKindTblEntry AVX512BWCostTbl[] = {
    { ISD::SETCC,   MVT::v32i16,  { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v16i16,  { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v64i8,   { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v32i8,   { 1, 1, 1, 1 } },

    { ISD::SELECT,  MVT::v32i16,  { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v64i8,   { 1, 1, 1, 1 } },
  };

  static const CostKindTblEntry AVX512CostTbl[] = {
    { ISD::SETCC,   MVT::v8f64,   { 1, 4, 1, 1 } },
    { ISD::SETCC,   MVT::v4f64,   { 1, 4, 1, 1 } },
    { ISD::SETCC,   MVT::v16f32,  { 1, 4, 1, 1 } },
    { ISD::SETCC,   MVT::v8f32,   { 1, 4, 1, 1 } },

    { ISD::SETCC,   MVT::v8i64,   { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v4i64,   { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v2i64,   { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v16i32,  { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v8i32,   { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v32i16,  { 3, 7, 5, 5 } },
    { ISD::SETCC,   MVT::v64i8,   { 3, 7, 5, 5 } },

    { ISD::SELECT,  MVT::v8i64,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v4i64,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v2i64,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v16i32,  { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v8i32,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v4i32,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v8f64,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v4f64,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v2f64,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::f64,     { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v16f32,  { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v8f32 ,  { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v4f32,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::f32  ,   { 1, 1, 1, 1 } },

    { ISD::SELECT,  MVT::v32i16,  { 2, 2, 4, 4 } },
    { ISD::SELECT,  MVT::v16i16,  { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v8i16,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v64i8,   { 2, 2, 4, 4 } },
    { ISD::SELECT,  MVT::v32i8,   { 1, 1, 1, 1 } },
    { ISD::SELECT,  MVT::v16i8,   { 1, 1, 1, 1 } },
  };

  static const CostKindTblEntry AVX2CostTbl[] = {
    { ISD::SETCC,   MVT::v4f64,   { 1, 4, 1, 2 } },
    { ISD::SETCC,   MVT::v2f64,   { 1, 4, 1, 1 } },
    { ISD::SETCC,   MVT::f64,     { 1, 4, 1, 1 } },
    { ISD::SETCC,   MVT::v8f32,   { 1, 4, 1, 2 } },
    { ISD::SETCC,   MVT::v4f32,   { 1, 4, 1, 1 } },
    { ISD::SETCC,   MVT::f32,     { 1, 4, 1, 1 } },

    { ISD::SETCC,   MVT::v4i64,   { 1, 1, 1, 2 } },
    { ISD::SETCC,   MVT::v8i32,   { 1, 1, 1, 2 } },
    { ISD::SETCC,   MVT::v16i16,  { 1, 1, 1, 2 } },
    { ISD::SETCC,   MVT::v32i8,   { 1, 1, 1, 2 } },

    { ISD::SELECT,  MVT::v4f64,   { 2, 2, 1, 2 } }, // vblendvpd
    { ISD::SELECT,  MVT::v8f32,   { 2, 2, 1, 2 } }, // vblendvps
    { ISD::SELECT,  MVT::v4i64,   { 2, 2, 1, 2 } }, // pblendvb
    { ISD::SELECT,  MVT::v8i32,   { 2, 2, 1, 2 } }, // pblendvb
    { ISD::SELECT,  MVT::v16i16,  { 2, 2, 1, 2 } }, // pblendvb
    { ISD::SELECT,  MVT::v32i8,   { 2, 2, 1, 2 } }, // pblendvb
  };

  static const CostKindTblEntry XOPCostTbl[] = {
    { ISD::SETCC,   MVT::v4i64,   { 4, 2, 5, 6 } },
    { ISD::SETCC,   MVT::v2i64,   { 1, 1, 1, 1 } },
  };

  static const CostKindTblEntry AVX1CostTbl[] = {
    { ISD::SETCC,   MVT::v4f64,   { 2, 3, 1, 2 } },
    { ISD::SETCC,   MVT::v2f64,   { 1, 3, 1, 1 } },
    { ISD::SETCC,   MVT::f64,     { 1, 3, 1, 1 } },
    { ISD::SETCC,   MVT::v8f32,   { 2, 3, 1, 2 } },
    { ISD::SETCC,   MVT::v4f32,   { 1, 3, 1, 1 } },
    { ISD::SETCC,   MVT::f32,     { 1, 3, 1, 1 } },

    // AVX1 does not support 8-wide integer compare.
    { ISD::SETCC,   MVT::v4i64,   { 4, 2, 5, 6 } },
    { ISD::SETCC,   MVT::v8i32,   { 4, 2, 5, 6 } },
    { ISD::SETCC,   MVT::v16i16,  { 4, 2, 5, 6 } },
    { ISD::SETCC,   MVT::v32i8,   { 4, 2, 5, 6 } },

    { ISD::SELECT,  MVT::v4f64,   { 3, 3, 1, 2 } }, // vblendvpd
    { ISD::SELECT,  MVT::v8f32,   { 3, 3, 1, 2 } }, // vblendvps
    { ISD::SELECT,  MVT::v4i64,   { 3, 3, 1, 2 } }, // vblendvpd
    { ISD::SELECT,  MVT::v8i32,   { 3, 3, 1, 2 } }, // vblendvps
    { ISD::SELECT,  MVT::v16i16,  { 3, 3, 3, 3 } }, // vandps + vandnps + vorps
    { ISD::SELECT,  MVT::v32i8,   { 3, 3, 3, 3 } }, // vandps + vandnps + vorps
  };

  static const CostKindTblEntry SSE42CostTbl[] = {
    { ISD::SETCC,   MVT::v2i64,   { 1, 2, 1, 2 } },
  };

  static const CostKindTblEntry SSE41CostTbl[] = {
    { ISD::SETCC,   MVT::v2f64,   { 1, 5, 1, 1 } },
    { ISD::SETCC,   MVT::v4f32,   { 1, 5, 1, 1 } },

    { ISD::SELECT,  MVT::v2f64,   { 2, 2, 1, 2 } }, // blendvpd
    { ISD::SELECT,  MVT::f64,     { 2, 2, 1, 2 } }, // blendvpd
    { ISD::SELECT,  MVT::v4f32,   { 2, 2, 1, 2 } }, // blendvps
    { ISD::SELECT,  MVT::f32  ,   { 2, 2, 1, 2 } }, // blendvps
    { ISD::SELECT,  MVT::v2i64,   { 2, 2, 1, 2 } }, // pblendvb
    { ISD::SELECT,  MVT::v4i32,   { 2, 2, 1, 2 } }, // pblendvb
    { ISD::SELECT,  MVT::v8i16,   { 2, 2, 1, 2 } }, // pblendvb
    { ISD::SELECT,  MVT::v16i8,   { 2, 2, 1, 2 } }, // pblendvb
  };

  static const CostKindTblEntry SSE2CostTbl[] = {
    { ISD::SETCC,   MVT::v2f64,   { 2, 5, 1, 1 } },
    { ISD::SETCC,   MVT::f64,     { 1, 5, 1, 1 } },

    { ISD::SETCC,   MVT::v2i64,   { 5, 4, 5, 5 } }, // pcmpeqd/pcmpgtd expansion
    { ISD::SETCC,   MVT::v4i32,   { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v8i16,   { 1, 1, 1, 1 } },
    { ISD::SETCC,   MVT::v16i8,   { 1, 1, 1, 1 } },

    { ISD::SELECT,  MVT::v2f64,   { 2, 2, 3, 3 } }, // andpd + andnpd + orpd
    { ISD::SELECT,  MVT::f64,     { 2, 2, 3, 3 } }, // andpd + andnpd + orpd
    { ISD::SELECT,  MVT::v2i64,   { 2, 2, 3, 3 } }, // pand + pandn + por
    { ISD::SELECT,  MVT::v4i32,   { 2, 2, 3, 3 } }, // pand + pandn + por
    { ISD::SELECT,  MVT::v8i16,   { 2, 2, 3, 3 } }, // pand + pandn + por
    { ISD::SELECT,  MVT::v16i8,   { 2, 2, 3, 3 } }, // pand + pandn + por
  };

  static const CostKindTblEntry SSE1CostTbl[] = {
    { ISD::SETCC,   MVT::v4f32,   { 2, 5, 1, 1 } },
    { ISD::SETCC,   MVT::f32,     { 1, 5, 1, 1 } },

    { ISD::SELECT,  MVT::v4f32,   { 2, 2, 3, 3 } }, // andps + andnps + orps
    { ISD::SELECT,  MVT::f32,     { 2, 2, 3, 3 } }, // andps + andnps + orps
  };

  if (ST->useSLMArithCosts())
    if (const auto *Entry = CostTableLookup(SLMCostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasBWI())
    if (const auto *Entry = CostTableLookup(AVX512BWCostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasAVX512())
    if (const auto *Entry = CostTableLookup(AVX512CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasAVX2())
    if (const auto *Entry = CostTableLookup(AVX2CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasXOP())
    if (const auto *Entry = CostTableLookup(XOPCostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasSSE42())
    if (const auto *Entry = CostTableLookup(SSE42CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasSSE41())
    if (const auto *Entry = CostTableLookup(SSE41CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  if (ST->hasSSE1())
    if (const auto *Entry = CostTableLookup(SSE1CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return LT.first * (ExtraCost + *KindCost);

  // Assume a 3cy latency for fp select ops.
  if (CostKind == TTI::TCK_Latency && Opcode == Instruction::Select)
    if (ValTy->getScalarType()->isFloatingPointTy())
      return 3;

  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind, I);
}

unsigned X86TTIImpl::getAtomicMemIntrinsicMaxElementSize() const { return 16; }

InstructionCost
X86TTIImpl::getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                  TTI::TargetCostKind CostKind) {
  // Costs should match the codegen from:
  // BITREVERSE: llvm\test\CodeGen\X86\vector-bitreverse.ll
  // BSWAP: llvm\test\CodeGen\X86\bswap-vector.ll
  // CTLZ: llvm\test\CodeGen\X86\vector-lzcnt-*.ll
  // CTPOP: llvm\test\CodeGen\X86\vector-popcnt-*.ll
  // CTTZ: llvm\test\CodeGen\X86\vector-tzcnt-*.ll

  // TODO: Overflow intrinsics (*ADDO, *SUBO, *MULO) with vector types are not
  //       specialized in these tables yet.
  static const CostKindTblEntry AVX512VBMI2CostTbl[] = {
    { ISD::FSHL,       MVT::v8i64,   {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v4i64,   {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v2i64,   {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v8i32,   {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v16i16,  {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::v8i16,   {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v16i16,  {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v8i16,   {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v16i16,  {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v8i16,   {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v32i16,  {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v16i16,  {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v8i16,   {  1,  1,  1,  1 } },
  };
  static const CostKindTblEntry AVX512BITALGCostTbl[] = {
    { ISD::CTPOP,      MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v64i8,   {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v16i16,  {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v32i8,   {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v8i16,   {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v16i8,   {  1,  1,  1,  1 } },
  };
  static const CostKindTblEntry AVX512VPOPCNTDQCostTbl[] = {
    { ISD::CTPOP,      MVT::v8i64,   {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v4i64,   {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v8i32,   {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v2i64,   {  1,  1,  1,  1 } },
    { ISD::CTPOP,      MVT::v4i32,   {  1,  1,  1,  1 } },
  };
  static const CostKindTblEntry AVX512CDCostTbl[] = {
    { ISD::CTLZ,       MVT::v8i64,   {  1,  5,  1,  1 } },
    { ISD::CTLZ,       MVT::v16i32,  {  1,  5,  1,  1 } },
    { ISD::CTLZ,       MVT::v32i16,  { 18, 27, 23, 27 } },
    { ISD::CTLZ,       MVT::v64i8,   {  3, 16,  9, 11 } },
    { ISD::CTLZ,       MVT::v4i64,   {  1,  5,  1,  1 } },
    { ISD::CTLZ,       MVT::v8i32,   {  1,  5,  1,  1 } },
    { ISD::CTLZ,       MVT::v16i16,  {  8, 19, 11, 13 } },
    { ISD::CTLZ,       MVT::v32i8,   {  2, 11,  9, 10 } },
    { ISD::CTLZ,       MVT::v2i64,   {  1,  5,  1,  1 } },
    { ISD::CTLZ,       MVT::v4i32,   {  1,  5,  1,  1 } },
    { ISD::CTLZ,       MVT::v8i16,   {  3, 15,  4,  6 } },
    { ISD::CTLZ,       MVT::v16i8,   {  2, 10,  9, 10 } },

    { ISD::CTTZ,       MVT::v8i64,   {  2,  8,  6,  7 } },
    { ISD::CTTZ,       MVT::v16i32,  {  2,  8,  6,  7 } },
    { ISD::CTTZ,       MVT::v4i64,   {  1,  8,  6,  6 } },
    { ISD::CTTZ,       MVT::v8i32,   {  1,  8,  6,  6 } },
    { ISD::CTTZ,       MVT::v2i64,   {  1,  8,  6,  6 } },
    { ISD::CTTZ,       MVT::v4i32,   {  1,  8,  6,  6 } },
  };
  static const CostKindTblEntry AVX512BWCostTbl[] = {
    { ISD::ABS,        MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v64i8,   {  1,  1,  1,  1 } },
    { ISD::BITREVERSE, MVT::v2i64,   {  3, 10, 10, 11 } },
    { ISD::BITREVERSE, MVT::v4i64,   {  3, 11, 10, 11 } },
    { ISD::BITREVERSE, MVT::v8i64,   {  3, 12, 10, 14 } },
    { ISD::BITREVERSE, MVT::v4i32,   {  3, 10, 10, 11 } },
    { ISD::BITREVERSE, MVT::v8i32,   {  3, 11, 10, 11 } },
    { ISD::BITREVERSE, MVT::v16i32,  {  3, 12, 10, 14 } },
    { ISD::BITREVERSE, MVT::v8i16,   {  3, 10, 10, 11 } },
    { ISD::BITREVERSE, MVT::v16i16,  {  3, 11, 10, 11 } },
    { ISD::BITREVERSE, MVT::v32i16,  {  3, 12, 10, 14 } },
    { ISD::BITREVERSE, MVT::v16i8,   {  2,  5,  9,  9 } },
    { ISD::BITREVERSE, MVT::v32i8,   {  2,  5,  9,  9 } },
    { ISD::BITREVERSE, MVT::v64i8,   {  2,  5,  9, 12 } },
    { ISD::BSWAP,      MVT::v2i64,   {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v4i64,   {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v8i64,   {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v4i32,   {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v8i32,   {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v16i32,  {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v8i16,   {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v16i16,  {  1,  1,  1,  2 } },
    { ISD::BSWAP,      MVT::v32i16,  {  1,  1,  1,  2 } },
    { ISD::CTLZ,       MVT::v8i64,   {  8, 22, 23, 23 } },
    { ISD::CTLZ,       MVT::v16i32,  {  8, 23, 25, 25 } },
    { ISD::CTLZ,       MVT::v32i16,  {  4, 15, 15, 16 } },
    { ISD::CTLZ,       MVT::v64i8,   {  3, 12, 10,  9 } },
    { ISD::CTPOP,      MVT::v2i64,   {  3,  7, 10, 10 } },
    { ISD::CTPOP,      MVT::v4i64,   {  3,  7, 10, 10 } },
    { ISD::CTPOP,      MVT::v8i64,   {  3,  8, 10, 12 } },
    { ISD::CTPOP,      MVT::v4i32,   {  7, 11, 14, 14 } },
    { ISD::CTPOP,      MVT::v8i32,   {  7, 11, 14, 14 } },
    { ISD::CTPOP,      MVT::v16i32,  {  7, 12, 14, 16 } },
    { ISD::CTPOP,      MVT::v8i16,   {  2,  7, 11, 11 } },
    { ISD::CTPOP,      MVT::v16i16,  {  2,  7, 11, 11 } },
    { ISD::CTPOP,      MVT::v32i16,  {  3,  7, 11, 13 } },
    { ISD::CTPOP,      MVT::v16i8,   {  2,  4,  8,  8 } },
    { ISD::CTPOP,      MVT::v32i8,   {  2,  4,  8,  8 } },
    { ISD::CTPOP,      MVT::v64i8,   {  2,  5,  8, 10 } },
    { ISD::CTTZ,       MVT::v8i16,   {  3,  9, 14, 14 } },
    { ISD::CTTZ,       MVT::v16i16,  {  3,  9, 14, 14 } },
    { ISD::CTTZ,       MVT::v32i16,  {  3, 10, 14, 16 } },
    { ISD::CTTZ,       MVT::v16i8,   {  2,  6, 11, 11 } },
    { ISD::CTTZ,       MVT::v32i8,   {  2,  6, 11, 11 } },
    { ISD::CTTZ,       MVT::v64i8,   {  3,  7, 11, 13 } },
    { ISD::ROTL,       MVT::v32i16,  {  2,  8,  6,  8 } },
    { ISD::ROTL,       MVT::v16i16,  {  2,  8,  6,  7 } },
    { ISD::ROTL,       MVT::v8i16,   {  2,  7,  6,  7 } },
    { ISD::ROTL,       MVT::v64i8,   {  5,  6, 11, 12 } },
    { ISD::ROTL,       MVT::v32i8,   {  5, 15,  7, 10 } },
    { ISD::ROTL,       MVT::v16i8,   {  5, 15,  7, 10 } },
    { ISD::ROTR,       MVT::v32i16,  {  2,  8,  6,  8 } },
    { ISD::ROTR,       MVT::v16i16,  {  2,  8,  6,  7 } },
    { ISD::ROTR,       MVT::v8i16,   {  2,  7,  6,  7 } },
    { ISD::ROTR,       MVT::v64i8,   {  5,  6, 12, 14 } },
    { ISD::ROTR,       MVT::v32i8,   {  5, 14,  6,  9 } },
    { ISD::ROTR,       MVT::v16i8,   {  5, 14,  6,  9 } },
    { X86ISD::VROTLI,  MVT::v32i16,  {  2,  5,  3,  3 } },
    { X86ISD::VROTLI,  MVT::v16i16,  {  1,  5,  3,  3 } },
    { X86ISD::VROTLI,  MVT::v8i16,   {  1,  5,  3,  3 } },
    { X86ISD::VROTLI,  MVT::v64i8,   {  2,  9,  3,  4 } },
    { X86ISD::VROTLI,  MVT::v32i8,   {  1,  9,  3,  4 } },
    { X86ISD::VROTLI,  MVT::v16i8,   {  1,  8,  3,  4 } },
    { ISD::SADDSAT,    MVT::v32i16,  {  1 } },
    { ISD::SADDSAT,    MVT::v64i8,   {  1 } },
    { ISD::SMAX,       MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::SMAX,       MVT::v64i8,   {  1,  1,  1,  1 } },
    { ISD::SMIN,       MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::SMIN,       MVT::v64i8,   {  1,  1,  1,  1 } },
    { ISD::SSUBSAT,    MVT::v32i16,  {  1 } },
    { ISD::SSUBSAT,    MVT::v64i8,   {  1 } },
    { ISD::UADDSAT,    MVT::v32i16,  {  1 } },
    { ISD::UADDSAT,    MVT::v64i8,   {  1 } },
    { ISD::UMAX,       MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::UMAX,       MVT::v64i8,   {  1,  1,  1,  1 } },
    { ISD::UMIN,       MVT::v32i16,  {  1,  1,  1,  1 } },
    { ISD::UMIN,       MVT::v64i8,   {  1,  1,  1,  1 } },
    { ISD::USUBSAT,    MVT::v32i16,  {  1 } },
    { ISD::USUBSAT,    MVT::v64i8,   {  1 } },
  };
  static const CostKindTblEntry AVX512CostTbl[] = {
    { ISD::ABS,        MVT::v8i64,   {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v4i64,   {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v2i64,   {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v8i32,   {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v32i16,  {  2,  7,  4,  4 } },
    { ISD::ABS,        MVT::v16i16,  {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v64i8,   {  2,  7,  4,  4 } },
    { ISD::ABS,        MVT::v32i8,   {  1,  1,  1,  1 } },
    { ISD::BITREVERSE, MVT::v8i64,   {  9, 13, 20, 20 } },
    { ISD::BITREVERSE, MVT::v16i32,  {  9, 13, 20, 20 } },
    { ISD::BITREVERSE, MVT::v32i16,  {  9, 13, 20, 20 } },
    { ISD::BITREVERSE, MVT::v64i8,   {  6, 11, 17, 17 } },
    { ISD::BSWAP,      MVT::v8i64,   {  4,  7,  5,  5 } },
    { ISD::BSWAP,      MVT::v16i32,  {  4,  7,  5,  5 } },
    { ISD::BSWAP,      MVT::v32i16,  {  4,  7,  5,  5 } },
    { ISD::CTLZ,       MVT::v8i64,   { 10, 28, 32, 32 } },
    { ISD::CTLZ,       MVT::v16i32,  { 12, 30, 38, 38 } },
    { ISD::CTLZ,       MVT::v32i16,  {  8, 15, 29, 29 } },
    { ISD::CTLZ,       MVT::v64i8,   {  6, 11, 19, 19 } },
    { ISD::CTPOP,      MVT::v8i64,   { 16, 16, 19, 19 } },
    { ISD::CTPOP,      MVT::v16i32,  { 24, 19, 27, 27 } },
    { ISD::CTPOP,      MVT::v32i16,  { 18, 15, 22, 22 } },
    { ISD::CTPOP,      MVT::v64i8,   { 12, 11, 16, 16 } },
    { ISD::CTTZ,       MVT::v8i64,   {  2,  8,  6,  7 } },
    { ISD::CTTZ,       MVT::v16i32,  {  2,  8,  6,  7 } },
    { ISD::CTTZ,       MVT::v32i16,  {  7, 17, 27, 27 } },
    { ISD::CTTZ,       MVT::v64i8,   {  6, 13, 21, 21 } },
    { ISD::ROTL,       MVT::v8i64,   {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v4i64,   {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v2i64,   {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v8i32,   {  1,  1,  1,  1 } },
    { ISD::ROTL,       MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v8i64,   {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v4i64,   {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v2i64,   {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v8i32,   {  1,  1,  1,  1 } },
    { ISD::ROTR,       MVT::v4i32,   {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v8i64,   {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v4i64,   {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v2i64,   {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v16i32,  {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v8i32,   {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::SMAX,       MVT::v8i64,   {  1,  3,  1,  1 } },
    { ISD::SMAX,       MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::SMAX,       MVT::v32i16,  {  3,  7,  5,  5 } },
    { ISD::SMAX,       MVT::v64i8,   {  3,  7,  5,  5 } },
    { ISD::SMAX,       MVT::v4i64,   {  1,  3,  1,  1 } },
    { ISD::SMAX,       MVT::v2i64,   {  1,  3,  1,  1 } },
    { ISD::SMIN,       MVT::v8i64,   {  1,  3,  1,  1 } },
    { ISD::SMIN,       MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::SMIN,       MVT::v32i16,  {  3,  7,  5,  5 } },
    { ISD::SMIN,       MVT::v64i8,   {  3,  7,  5,  5 } },
    { ISD::SMIN,       MVT::v4i64,   {  1,  3,  1,  1 } },
    { ISD::SMIN,       MVT::v2i64,   {  1,  3,  1,  1 } },
    { ISD::UMAX,       MVT::v8i64,   {  1,  3,  1,  1 } },
    { ISD::UMAX,       MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::UMAX,       MVT::v32i16,  {  3,  7,  5,  5 } },
    { ISD::UMAX,       MVT::v64i8,   {  3,  7,  5,  5 } },
    { ISD::UMAX,       MVT::v4i64,   {  1,  3,  1,  1 } },
    { ISD::UMAX,       MVT::v2i64,   {  1,  3,  1,  1 } },
    { ISD::UMIN,       MVT::v8i64,   {  1,  3,  1,  1 } },
    { ISD::UMIN,       MVT::v16i32,  {  1,  1,  1,  1 } },
    { ISD::UMIN,       MVT::v32i16,  {  3,  7,  5,  5 } },
    { ISD::UMIN,       MVT::v64i8,   {  3,  7,  5,  5 } },
    { ISD::UMIN,       MVT::v4i64,   {  1,  3,  1,  1 } },
    { ISD::UMIN,       MVT::v2i64,   {  1,  3,  1,  1 } },
    { ISD::USUBSAT,    MVT::v16i32,  {  2 } }, // pmaxud + psubd
    { ISD::USUBSAT,    MVT::v2i64,   {  2 } }, // pmaxuq + psubq
    { ISD::USUBSAT,    MVT::v4i64,   {  2 } }, // pmaxuq + psubq
    { ISD::USUBSAT,    MVT::v8i64,   {  2 } }, // pmaxuq + psubq
    { ISD::UADDSAT,    MVT::v16i32,  {  3 } }, // not + pminud + paddd
    { ISD::UADDSAT,    MVT::v2i64,   {  3 } }, // not + pminuq + paddq
    { ISD::UADDSAT,    MVT::v4i64,   {  3 } }, // not + pminuq + paddq
    { ISD::UADDSAT,    MVT::v8i64,   {  3 } }, // not + pminuq + paddq
    { ISD::SADDSAT,    MVT::v32i16,  {  2 } },
    { ISD::SADDSAT,    MVT::v64i8,   {  2 } },
    { ISD::SSUBSAT,    MVT::v32i16,  {  2 } },
    { ISD::SSUBSAT,    MVT::v64i8,   {  2 } },
    { ISD::UADDSAT,    MVT::v32i16,  {  2 } },
    { ISD::UADDSAT,    MVT::v64i8,   {  2 } },
    { ISD::USUBSAT,    MVT::v32i16,  {  2 } },
    { ISD::USUBSAT,    MVT::v64i8,   {  2 } },
    { ISD::FMAXNUM,    MVT::f32,     {  2,  2,  3,  3 } },
    { ISD::FMAXNUM,    MVT::v4f32,   {  1,  1,  3,  3 } },
    { ISD::FMAXNUM,    MVT::v8f32,   {  2,  2,  3,  3 } },
    { ISD::FMAXNUM,    MVT::v16f32,  {  4,  4,  3,  3 } },
    { ISD::FMAXNUM,    MVT::f64,     {  2,  2,  3,  3 } },
    { ISD::FMAXNUM,    MVT::v2f64,   {  1,  1,  3,  3 } },
    { ISD::FMAXNUM,    MVT::v4f64,   {  2,  2,  3,  3 } },
    { ISD::FMAXNUM,    MVT::v8f64,   {  3,  3,  3,  3 } },
    { ISD::FSQRT,      MVT::f32,     {  3, 12,  1,  1 } }, // Skylake from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f32,   {  3, 12,  1,  1 } }, // Skylake from http://www.agner.org/
    { ISD::FSQRT,      MVT::v8f32,   {  6, 12,  1,  1 } }, // Skylake from http://www.agner.org/
    { ISD::FSQRT,      MVT::v16f32,  { 12, 20,  1,  3 } }, // Skylake from http://www.agner.org/
    { ISD::FSQRT,      MVT::f64,     {  6, 18,  1,  1 } }, // Skylake from http://www.agner.org/
    { ISD::FSQRT,      MVT::v2f64,   {  6, 18,  1,  1 } }, // Skylake from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f64,   { 12, 18,  1,  1 } }, // Skylake from http://www.agner.org/
    { ISD::FSQRT,      MVT::v8f64,   { 24, 32,  1,  3 } }, // Skylake from http://www.agner.org/
  };
  static const CostKindTblEntry XOPCostTbl[] = {
    { ISD::BITREVERSE, MVT::v4i64,   {  3,  6,  5,  6 } },
    { ISD::BITREVERSE, MVT::v8i32,   {  3,  6,  5,  6 } },
    { ISD::BITREVERSE, MVT::v16i16,  {  3,  6,  5,  6 } },
    { ISD::BITREVERSE, MVT::v32i8,   {  3,  6,  5,  6 } },
    { ISD::BITREVERSE, MVT::v2i64,   {  2,  7,  1,  1 } },
    { ISD::BITREVERSE, MVT::v4i32,   {  2,  7,  1,  1 } },
    { ISD::BITREVERSE, MVT::v8i16,   {  2,  7,  1,  1 } },
    { ISD::BITREVERSE, MVT::v16i8,   {  2,  7,  1,  1 } },
    { ISD::BITREVERSE, MVT::i64,     {  2,  2,  3,  4 } },
    { ISD::BITREVERSE, MVT::i32,     {  2,  2,  3,  4 } },
    { ISD::BITREVERSE, MVT::i16,     {  2,  2,  3,  4 } },
    { ISD::BITREVERSE, MVT::i8,      {  2,  2,  3,  4 } },
    // XOP: ROTL = VPROT(X,Y), ROTR = VPROT(X,SUB(0,Y))
    { ISD::ROTL,       MVT::v4i64,   {  4,  7,  5,  6 } },
    { ISD::ROTL,       MVT::v8i32,   {  4,  7,  5,  6 } },
    { ISD::ROTL,       MVT::v16i16,  {  4,  7,  5,  6 } },
    { ISD::ROTL,       MVT::v32i8,   {  4,  7,  5,  6 } },
    { ISD::ROTL,       MVT::v2i64,   {  1,  3,  1,  1 } },
    { ISD::ROTL,       MVT::v4i32,   {  1,  3,  1,  1 } },
    { ISD::ROTL,       MVT::v8i16,   {  1,  3,  1,  1 } },
    { ISD::ROTL,       MVT::v16i8,   {  1,  3,  1,  1 } },
    { ISD::ROTR,       MVT::v4i64,   {  4,  7,  8,  9 } },
    { ISD::ROTR,       MVT::v8i32,   {  4,  7,  8,  9 } },
    { ISD::ROTR,       MVT::v16i16,  {  4,  7,  8,  9 } },
    { ISD::ROTR,       MVT::v32i8,   {  4,  7,  8,  9 } },
    { ISD::ROTR,       MVT::v2i64,   {  1,  3,  3,  3 } },
    { ISD::ROTR,       MVT::v4i32,   {  1,  3,  3,  3 } },
    { ISD::ROTR,       MVT::v8i16,   {  1,  3,  3,  3 } },
    { ISD::ROTR,       MVT::v16i8,   {  1,  3,  3,  3 } },
    { X86ISD::VROTLI,  MVT::v4i64,   {  4,  7,  5,  6 } },
    { X86ISD::VROTLI,  MVT::v8i32,   {  4,  7,  5,  6 } },
    { X86ISD::VROTLI,  MVT::v16i16,  {  4,  7,  5,  6 } },
    { X86ISD::VROTLI,  MVT::v32i8,   {  4,  7,  5,  6 } },
    { X86ISD::VROTLI,  MVT::v2i64,   {  1,  3,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v4i32,   {  1,  3,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v8i16,   {  1,  3,  1,  1 } },
    { X86ISD::VROTLI,  MVT::v16i8,   {  1,  3,  1,  1 } },
  };
  static const CostKindTblEntry AVX2CostTbl[] = {
    { ISD::ABS,        MVT::v2i64,   {  2,  4,  3,  5 } }, // VBLENDVPD(X,VPSUBQ(0,X),X)
    { ISD::ABS,        MVT::v4i64,   {  2,  4,  3,  5 } }, // VBLENDVPD(X,VPSUBQ(0,X),X)
    { ISD::ABS,        MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v8i32,   {  1,  1,  1,  2 } },
    { ISD::ABS,        MVT::v8i16,   {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v16i16,  {  1,  1,  1,  2 } },
    { ISD::ABS,        MVT::v16i8,   {  1,  1,  1,  1 } },
    { ISD::ABS,        MVT::v32i8,   {  1,  1,  1,  2 } },
    { ISD::BITREVERSE, MVT::v2i64,   {  3, 11, 10, 11 } },
    { ISD::BITREVERSE, MVT::v4i64,   {  5, 11, 10, 17 } },
    { ISD::BITREVERSE, MVT::v4i32,   {  3, 11, 10, 11 } },
    { ISD::BITREVERSE, MVT::v8i32,   {  5, 11, 10, 17 } },
    { ISD::BITREVERSE, MVT::v8i16,   {  3, 11, 10, 11 } },
    { ISD::BITREVERSE, MVT::v16i16,  {  5, 11, 10, 17 } },
    { ISD::BITREVERSE, MVT::v16i8,   {  3,  6,  9,  9 } },
    { ISD::BITREVERSE, MVT::v32i8,   {  4,  5,  9, 15 } },
    { ISD::BSWAP,      MVT::v2i64,   {  1,  2,  1,  2 } },
    { ISD::BSWAP,      MVT::v4i64,   {  1,  3,  1,  2 } },
    { ISD::BSWAP,      MVT::v4i32,   {  1,  2,  1,  2 } },
    { ISD::BSWAP,      MVT::v8i32,   {  1,  3,  1,  2 } },
    { ISD::BSWAP,      MVT::v8i16,   {  1,  2,  1,  2 } },
    { ISD::BSWAP,      MVT::v16i16,  {  1,  3,  1,  2 } },
    { ISD::CTLZ,       MVT::v2i64,   {  7, 18, 24, 25 } },
    { ISD::CTLZ,       MVT::v4i64,   { 14, 18, 24, 44 } },
    { ISD::CTLZ,       MVT::v4i32,   {  5, 16, 19, 20 } },
    { ISD::CTLZ,       MVT::v8i32,   { 10, 16, 19, 34 } },
    { ISD::CTLZ,       MVT::v8i16,   {  4, 13, 14, 15 } },
    { ISD::CTLZ,       MVT::v16i16,  {  6, 14, 14, 24 } },
    { ISD::CTLZ,       MVT::v16i8,   {  3, 12,  9, 10 } },
    { ISD::CTLZ,       MVT::v32i8,   {  4, 12,  9, 14 } },
    { ISD::CTPOP,      MVT::v2i64,   {  3,  9, 10, 10 } },
    { ISD::CTPOP,      MVT::v4i64,   {  4,  9, 10, 14 } },
    { ISD::CTPOP,      MVT::v4i32,   {  7, 12, 14, 14 } },
    { ISD::CTPOP,      MVT::v8i32,   {  7, 12, 14, 18 } },
    { ISD::CTPOP,      MVT::v8i16,   {  3,  7, 11, 11 } },
    { ISD::CTPOP,      MVT::v16i16,  {  6,  8, 11, 18 } },
    { ISD::CTPOP,      MVT::v16i8,   {  2,  5,  8,  8 } },
    { ISD::CTPOP,      MVT::v32i8,   {  3,  5,  8, 12 } },
    { ISD::CTTZ,       MVT::v2i64,   {  4, 11, 13, 13 } },
    { ISD::CTTZ,       MVT::v4i64,   {  5, 11, 13, 20 } },
    { ISD::CTTZ,       MVT::v4i32,   {  7, 14, 17, 17 } },
    { ISD::CTTZ,       MVT::v8i32,   {  7, 15, 17, 24 } },
    { ISD::CTTZ,       MVT::v8i16,   {  4,  9, 14, 14 } },
    { ISD::CTTZ,       MVT::v16i16,  {  6,  9, 14, 24 } },
    { ISD::CTTZ,       MVT::v16i8,   {  3,  7, 11, 11 } },
    { ISD::CTTZ,       MVT::v32i8,   {  5,  7, 11, 18 } },
    { ISD::SADDSAT,    MVT::v16i16,  {  1 } },
    { ISD::SADDSAT,    MVT::v32i8,   {  1 } },
    { ISD::SMAX,       MVT::v2i64,   {  2,  7,  2,  3 } },
    { ISD::SMAX,       MVT::v4i64,   {  2,  7,  2,  3 } },
    { ISD::SMAX,       MVT::v8i32,   {  1,  1,  1,  2 } },
    { ISD::SMAX,       MVT::v16i16,  {  1,  1,  1,  2 } },
    { ISD::SMAX,       MVT::v32i8,   {  1,  1,  1,  2 } },
    { ISD::SMIN,       MVT::v2i64,   {  2,  7,  2,  3 } },
    { ISD::SMIN,       MVT::v4i64,   {  2,  7,  2,  3 } },
    { ISD::SMIN,       MVT::v8i32,   {  1,  1,  1,  2 } },
    { ISD::SMIN,       MVT::v16i16,  {  1,  1,  1,  2 } },
    { ISD::SMIN,       MVT::v32i8,   {  1,  1,  1,  2 } },
    { ISD::SSUBSAT,    MVT::v16i16,  {  1 } },
    { ISD::SSUBSAT,    MVT::v32i8,   {  1 } },
    { ISD::UADDSAT,    MVT::v16i16,  {  1 } },
    { ISD::UADDSAT,    MVT::v32i8,   {  1 } },
    { ISD::UADDSAT,    MVT::v8i32,   {  3 } }, // not + pminud + paddd
    { ISD::UMAX,       MVT::v2i64,   {  2,  8,  5,  6 } },
    { ISD::UMAX,       MVT::v4i64,   {  2,  8,  5,  8 } },
    { ISD::UMAX,       MVT::v8i32,   {  1,  1,  1,  2 } },
    { ISD::UMAX,       MVT::v16i16,  {  1,  1,  1,  2 } },
    { ISD::UMAX,       MVT::v32i8,   {  1,  1,  1,  2 } },
    { ISD::UMIN,       MVT::v2i64,   {  2,  8,  5,  6 } },
    { ISD::UMIN,       MVT::v4i64,   {  2,  8,  5,  8 } },
    { ISD::UMIN,       MVT::v8i32,   {  1,  1,  1,  2 } },
    { ISD::UMIN,       MVT::v16i16,  {  1,  1,  1,  2 } },
    { ISD::UMIN,       MVT::v32i8,   {  1,  1,  1,  2 } },
    { ISD::USUBSAT,    MVT::v16i16,  {  1 } },
    { ISD::USUBSAT,    MVT::v32i8,   {  1 } },
    { ISD::USUBSAT,    MVT::v8i32,   {  2 } }, // pmaxud + psubd
    { ISD::FMAXNUM,    MVT::f32,     {  2,  7,  3,  5 } }, // MAXSS + CMPUNORDSS + BLENDVPS
    { ISD::FMAXNUM,    MVT::v4f32,   {  2,  7,  3,  5 } }, // MAXPS + CMPUNORDPS + BLENDVPS
    { ISD::FMAXNUM,    MVT::v8f32,   {  3,  7,  3,  6 } }, // MAXPS + CMPUNORDPS + BLENDVPS
    { ISD::FMAXNUM,    MVT::f64,     {  2,  7,  3,  5 } }, // MAXSD + CMPUNORDSD + BLENDVPD
    { ISD::FMAXNUM,    MVT::v2f64,   {  2,  7,  3,  5 } }, // MAXPD + CMPUNORDPD + BLENDVPD
    { ISD::FMAXNUM,    MVT::v4f64,   {  3,  7,  3,  6 } }, // MAXPD + CMPUNORDPD + BLENDVPD
    { ISD::FSQRT,      MVT::f32,     {  7, 15,  1,  1 } }, // vsqrtss
    { ISD::FSQRT,      MVT::v4f32,   {  7, 15,  1,  1 } }, // vsqrtps
    { ISD::FSQRT,      MVT::v8f32,   { 14, 21,  1,  3 } }, // vsqrtps
    { ISD::FSQRT,      MVT::f64,     { 14, 21,  1,  1 } }, // vsqrtsd
    { ISD::FSQRT,      MVT::v2f64,   { 14, 21,  1,  1 } }, // vsqrtpd
    { ISD::FSQRT,      MVT::v4f64,   { 28, 35,  1,  3 } }, // vsqrtpd
  };
  static const CostKindTblEntry AVX1CostTbl[] = {
    { ISD::ABS,        MVT::v4i64,   {  6,  8,  6, 12 } }, // VBLENDVPD(X,VPSUBQ(0,X),X)
    { ISD::ABS,        MVT::v8i32,   {  3,  6,  4,  5 } },
    { ISD::ABS,        MVT::v16i16,  {  3,  6,  4,  5 } },
    { ISD::ABS,        MVT::v32i8,   {  3,  6,  4,  5 } },
    { ISD::BITREVERSE, MVT::v4i64,   { 17, 20, 20, 33 } }, // 2 x 128-bit Op + extract/insert
    { ISD::BITREVERSE, MVT::v2i64,   {  8, 13, 10, 16 } },
    { ISD::BITREVERSE, MVT::v8i32,   { 17, 20, 20, 33 } }, // 2 x 128-bit Op + extract/insert
    { ISD::BITREVERSE, MVT::v4i32,   {  8, 13, 10, 16 } },
    { ISD::BITREVERSE, MVT::v16i16,  { 17, 20, 20, 33 } }, // 2 x 128-bit Op + extract/insert
    { ISD::BITREVERSE, MVT::v8i16,   {  8, 13, 10, 16 } },
    { ISD::BITREVERSE, MVT::v32i8,   { 13, 15, 17, 26 } }, // 2 x 128-bit Op + extract/insert
    { ISD::BITREVERSE, MVT::v16i8,   {  7,  7,  9, 13 } },
    { ISD::BSWAP,      MVT::v4i64,   {  5,  6,  5, 10 } },
    { ISD::BSWAP,      MVT::v2i64,   {  2,  2,  1,  3 } },
    { ISD::BSWAP,      MVT::v8i32,   {  5,  6,  5, 10 } },
    { ISD::BSWAP,      MVT::v4i32,   {  2,  2,  1,  3 } },
    { ISD::BSWAP,      MVT::v16i16,  {  5,  6,  5, 10 } },
    { ISD::BSWAP,      MVT::v8i16,   {  2,  2,  1,  3 } },
    { ISD::CTLZ,       MVT::v4i64,   { 29, 33, 49, 58 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTLZ,       MVT::v2i64,   { 14, 24, 24, 28 } },
    { ISD::CTLZ,       MVT::v8i32,   { 24, 28, 39, 48 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTLZ,       MVT::v4i32,   { 12, 20, 19, 23 } },
    { ISD::CTLZ,       MVT::v16i16,  { 19, 22, 29, 38 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTLZ,       MVT::v8i16,   {  9, 16, 14, 18 } },
    { ISD::CTLZ,       MVT::v32i8,   { 14, 15, 19, 28 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTLZ,       MVT::v16i8,   {  7, 12,  9, 13 } },
    { ISD::CTPOP,      MVT::v4i64,   { 14, 18, 19, 28 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v2i64,   {  7, 14, 10, 14 } },
    { ISD::CTPOP,      MVT::v8i32,   { 18, 24, 27, 36 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v4i32,   {  9, 20, 14, 18 } },
    { ISD::CTPOP,      MVT::v16i16,  { 16, 21, 22, 31 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v8i16,   {  8, 18, 11, 15 } },
    { ISD::CTPOP,      MVT::v32i8,   { 13, 15, 16, 25 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTPOP,      MVT::v16i8,   {  6, 12,  8, 12 } },
    { ISD::CTTZ,       MVT::v4i64,   { 17, 22, 24, 33 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v2i64,   {  9, 19, 13, 17 } },
    { ISD::CTTZ,       MVT::v8i32,   { 21, 27, 32, 41 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v4i32,   { 11, 24, 17, 21 } },
    { ISD::CTTZ,       MVT::v16i16,  { 18, 24, 27, 36 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v8i16,   {  9, 21, 14, 18 } },
    { ISD::CTTZ,       MVT::v32i8,   { 15, 18, 21, 30 } }, // 2 x 128-bit Op + extract/insert
    { ISD::CTTZ,       MVT::v16i8,   {  8, 16, 11, 15 } },
    { ISD::SADDSAT,    MVT::v16i16,  {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SADDSAT,    MVT::v32i8,   {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMAX,       MVT::v4i64,   {  6,  9,  6, 12 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMAX,       MVT::v2i64,   {  3,  7,  2,  4 } },
    { ISD::SMAX,       MVT::v8i32,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMAX,       MVT::v16i16,  {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMAX,       MVT::v32i8,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMIN,       MVT::v4i64,   {  6,  9,  6, 12 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMIN,       MVT::v2i64,   {  3,  7,  2,  3 } },
    { ISD::SMIN,       MVT::v8i32,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMIN,       MVT::v16i16,  {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SMIN,       MVT::v32i8,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SSUBSAT,    MVT::v16i16,  {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::SSUBSAT,    MVT::v32i8,   {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UADDSAT,    MVT::v16i16,  {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UADDSAT,    MVT::v32i8,   {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UADDSAT,    MVT::v8i32,   {  8 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMAX,       MVT::v4i64,   {  9, 10, 11, 17 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMAX,       MVT::v2i64,   {  4,  8,  5,  7 } },
    { ISD::UMAX,       MVT::v8i32,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMAX,       MVT::v16i16,  {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMAX,       MVT::v32i8,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMIN,       MVT::v4i64,   {  9, 10, 11, 17 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMIN,       MVT::v2i64,   {  4,  8,  5,  7 } },
    { ISD::UMIN,       MVT::v8i32,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMIN,       MVT::v16i16,  {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::UMIN,       MVT::v32i8,   {  4,  6,  5,  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::USUBSAT,    MVT::v16i16,  {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::USUBSAT,    MVT::v32i8,   {  4 } }, // 2 x 128-bit Op + extract/insert
    { ISD::USUBSAT,    MVT::v8i32,   {  6 } }, // 2 x 128-bit Op + extract/insert
    { ISD::FMAXNUM,    MVT::f32,     {  3,  6,  3,  5 } }, // MAXSS + CMPUNORDSS + BLENDVPS
    { ISD::FMAXNUM,    MVT::v4f32,   {  3,  6,  3,  5 } }, // MAXPS + CMPUNORDPS + BLENDVPS
    { ISD::FMAXNUM,    MVT::v8f32,   {  5,  7,  3, 10 } }, // MAXPS + CMPUNORDPS + BLENDVPS
    { ISD::FMAXNUM,    MVT::f64,     {  3,  6,  3,  5 } }, // MAXSD + CMPUNORDSD + BLENDVPD
    { ISD::FMAXNUM,    MVT::v2f64,   {  3,  6,  3,  5 } }, // MAXPD + CMPUNORDPD + BLENDVPD
    { ISD::FMAXNUM,    MVT::v4f64,   {  5,  7,  3, 10 } }, // MAXPD + CMPUNORDPD + BLENDVPD
    { ISD::FSQRT,      MVT::f32,     { 21, 21,  1,  1 } }, // vsqrtss
    { ISD::FSQRT,      MVT::v4f32,   { 21, 21,  1,  1 } }, // vsqrtps
    { ISD::FSQRT,      MVT::v8f32,   { 42, 42,  1,  3 } }, // vsqrtps
    { ISD::FSQRT,      MVT::f64,     { 27, 27,  1,  1 } }, // vsqrtsd
    { ISD::FSQRT,      MVT::v2f64,   { 27, 27,  1,  1 } }, // vsqrtpd
    { ISD::FSQRT,      MVT::v4f64,   { 54, 54,  1,  3 } }, // vsqrtpd
  };
  static const CostKindTblEntry GFNICostTbl[] = {
    { ISD::BITREVERSE, MVT::i8,      {  3,  3,  3,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::i16,     {  3,  3,  4,  6 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::i32,     {  3,  3,  4,  5 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::i64,     {  3,  3,  4,  6 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v16i8,   {  1,  6,  1,  2 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v32i8,   {  1,  6,  1,  2 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v64i8,   {  1,  6,  1,  2 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v8i16,   {  1,  8,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v16i16,  {  1,  9,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v32i16,  {  1,  9,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v4i32,   {  1,  8,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v8i32,   {  1,  9,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v16i32,  {  1,  9,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v2i64,   {  1,  8,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v4i64,   {  1,  9,  2,  4 } }, // gf2p8affineqb
    { ISD::BITREVERSE, MVT::v8i64,   {  1,  9,  2,  4 } }, // gf2p8affineqb
    { X86ISD::VROTLI,  MVT::v16i8,   {  1,  6,  1,  2 } }, // gf2p8affineqb
    { X86ISD::VROTLI,  MVT::v32i8,   {  1,  6,  1,  2 } }, // gf2p8affineqb
    { X86ISD::VROTLI,  MVT::v64i8,   {  1,  6,  1,  2 } }, // gf2p8affineqb
  };
  static const CostKindTblEntry GLMCostTbl[] = {
    { ISD::FSQRT,      MVT::f32,     { 19, 20, 1, 1 } }, // sqrtss
    { ISD::FSQRT,      MVT::v4f32,   { 37, 41, 1, 5 } }, // sqrtps
    { ISD::FSQRT,      MVT::f64,     { 34, 35, 1, 1 } }, // sqrtsd
    { ISD::FSQRT,      MVT::v2f64,   { 67, 71, 1, 5 } }, // sqrtpd
  };
  static const CostKindTblEntry SLMCostTbl[] = {
    { ISD::BSWAP,      MVT::v2i64,   {  5,  5, 1, 5 } },
    { ISD::BSWAP,      MVT::v4i32,   {  5,  5, 1, 5 } },
    { ISD::BSWAP,      MVT::v8i16,   {  5,  5, 1, 5 } },
    { ISD::FSQRT,      MVT::f32,     { 20, 20, 1, 1 } }, // sqrtss
    { ISD::FSQRT,      MVT::v4f32,   { 40, 41, 1, 5 } }, // sqrtps
    { ISD::FSQRT,      MVT::f64,     { 35, 35, 1, 1 } }, // sqrtsd
    { ISD::FSQRT,      MVT::v2f64,   { 70, 71, 1, 5 } }, // sqrtpd
  };
  static const CostKindTblEntry SSE42CostTbl[] = {
    { ISD::USUBSAT,    MVT::v4i32,   {  2 } }, // pmaxud + psubd
    { ISD::UADDSAT,    MVT::v4i32,   {  3 } }, // not + pminud + paddd
    { ISD::FMAXNUM,    MVT::f32,     {  5,  5,  7,  7 } }, // MAXSS + CMPUNORDSS + BLENDVPS
    { ISD::FMAXNUM,    MVT::v4f32,   {  4,  4,  4,  5 } }, // MAXPS + CMPUNORDPS + BLENDVPS
    { ISD::FMAXNUM,    MVT::f64,     {  5,  5,  7,  7 } }, // MAXSD + CMPUNORDSD + BLENDVPD
    { ISD::FMAXNUM,    MVT::v2f64,   {  4,  4,  4,  5 } }, // MAXPD + CMPUNORDPD + BLENDVPD
    { ISD::FSQRT,      MVT::f32,     { 18, 18,  1,  1 } }, // Nehalem from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f32,   { 18, 18,  1,  1 } }, // Nehalem from http://www.agner.org/
  };
  static const CostKindTblEntry SSE41CostTbl[] = {
    { ISD::ABS,        MVT::v2i64,   {  3,  4,  3,  5 } }, // BLENDVPD(X,PSUBQ(0,X),X)
    { ISD::SMAX,       MVT::v2i64,   {  3,  7,  2,  3 } },
    { ISD::SMAX,       MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::SMAX,       MVT::v16i8,   {  1,  1,  1,  1 } },
    { ISD::SMIN,       MVT::v2i64,   {  3,  7,  2,  3 } },
    { ISD::SMIN,       MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::SMIN,       MVT::v16i8,   {  1,  1,  1,  1 } },
    { ISD::UMAX,       MVT::v2i64,   {  2, 11,  6,  7 } },
    { ISD::UMAX,       MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::UMAX,       MVT::v8i16,   {  1,  1,  1,  1 } },
    { ISD::UMIN,       MVT::v2i64,   {  2, 11,  6,  7 } },
    { ISD::UMIN,       MVT::v4i32,   {  1,  1,  1,  1 } },
    { ISD::UMIN,       MVT::v8i16,   {  1,  1,  1,  1 } },
  };
  static const CostKindTblEntry SSSE3CostTbl[] = {
    { ISD::ABS,        MVT::v4i32,   {  1,  2,  1,  1 } },
    { ISD::ABS,        MVT::v8i16,   {  1,  2,  1,  1 } },
    { ISD::ABS,        MVT::v16i8,   {  1,  2,  1,  1 } },
    { ISD::BITREVERSE, MVT::v2i64,   { 16, 20, 11, 21 } },
    { ISD::BITREVERSE, MVT::v4i32,   { 16, 20, 11, 21 } },
    { ISD::BITREVERSE, MVT::v8i16,   { 16, 20, 11, 21 } },
    { ISD::BITREVERSE, MVT::v16i8,   { 11, 12, 10, 16 } },
    { ISD::BSWAP,      MVT::v2i64,   {  2,  3,  1,  5 } },
    { ISD::BSWAP,      MVT::v4i32,   {  2,  3,  1,  5 } },
    { ISD::BSWAP,      MVT::v8i16,   {  2,  3,  1,  5 } },
    { ISD::CTLZ,       MVT::v2i64,   { 18, 28, 28, 35 } },
    { ISD::CTLZ,       MVT::v4i32,   { 15, 20, 22, 28 } },
    { ISD::CTLZ,       MVT::v8i16,   { 13, 17, 16, 22 } },
    { ISD::CTLZ,       MVT::v16i8,   { 11, 15, 10, 16 } },
    { ISD::CTPOP,      MVT::v2i64,   { 13, 19, 12, 18 } },
    { ISD::CTPOP,      MVT::v4i32,   { 18, 24, 16, 22 } },
    { ISD::CTPOP,      MVT::v8i16,   { 13, 18, 14, 20 } },
    { ISD::CTPOP,      MVT::v16i8,   { 11, 12, 10, 16 } },
    { ISD::CTTZ,       MVT::v2i64,   { 13, 25, 15, 22 } },
    { ISD::CTTZ,       MVT::v4i32,   { 18, 26, 19, 25 } },
    { ISD::CTTZ,       MVT::v8i16,   { 13, 20, 17, 23 } },
    { ISD::CTTZ,       MVT::v16i8,   { 11, 16, 13, 19 } }
  };
  static const CostKindTblEntry SSE2CostTbl[] = {
    { ISD::ABS,        MVT::v2i64,   {  3,  6,  5,  5 } },
    { ISD::ABS,        MVT::v4i32,   {  1,  4,  4,  4 } },
    { ISD::ABS,        MVT::v8i16,   {  1,  2,  3,  3 } },
    { ISD::ABS,        MVT::v16i8,   {  1,  2,  3,  3 } },
    { ISD::BITREVERSE, MVT::v2i64,   { 16, 20, 32, 32 } },
    { ISD::BITREVERSE, MVT::v4i32,   { 16, 20, 30, 30 } },
    { ISD::BITREVERSE, MVT::v8i16,   { 16, 20, 25, 25 } },
    { ISD::BITREVERSE, MVT::v16i8,   { 11, 12, 21, 21 } },
    { ISD::BSWAP,      MVT::v2i64,   {  5,  6, 11, 11 } },
    { ISD::BSWAP,      MVT::v4i32,   {  5,  5,  9,  9 } },
    { ISD::BSWAP,      MVT::v8i16,   {  5,  5,  4,  5 } },
    { ISD::CTLZ,       MVT::v2i64,   { 10, 45, 36, 38 } },
    { ISD::CTLZ,       MVT::v4i32,   { 10, 45, 38, 40 } },
    { ISD::CTLZ,       MVT::v8i16,   {  9, 38, 32, 34 } },
    { ISD::CTLZ,       MVT::v16i8,   {  8, 39, 29, 32 } },
    { ISD::CTPOP,      MVT::v2i64,   { 12, 26, 16, 18 } },
    { ISD::CTPOP,      MVT::v4i32,   { 15, 29, 21, 23 } },
    { ISD::CTPOP,      MVT::v8i16,   { 13, 25, 18, 20 } },
    { ISD::CTPOP,      MVT::v16i8,   { 10, 21, 14, 16 } },
    { ISD::CTTZ,       MVT::v2i64,   { 14, 28, 19, 21 } },
    { ISD::CTTZ,       MVT::v4i32,   { 18, 31, 24, 26 } },
    { ISD::CTTZ,       MVT::v8i16,   { 16, 27, 21, 23 } },
    { ISD::CTTZ,       MVT::v16i8,   { 13, 23, 17, 19 } },
    { ISD::SADDSAT,    MVT::v8i16,   {  1 } },
    { ISD::SADDSAT,    MVT::v16i8,   {  1 } },
    { ISD::SMAX,       MVT::v2i64,   {  4,  8, 15, 15 } },
    { ISD::SMAX,       MVT::v4i32,   {  2,  4,  5,  5 } },
    { ISD::SMAX,       MVT::v8i16,   {  1,  1,  1,  1 } },
    { ISD::SMAX,       MVT::v16i8,   {  2,  4,  5,  5 } },
    { ISD::SMIN,       MVT::v2i64,   {  4,  8, 15, 15 } },
    { ISD::SMIN,       MVT::v4i32,   {  2,  4,  5,  5 } },
    { ISD::SMIN,       MVT::v8i16,   {  1,  1,  1,  1 } },
    { ISD::SMIN,       MVT::v16i8,   {  2,  4,  5,  5 } },
    { ISD::SSUBSAT,    MVT::v8i16,   {  1 } },
    { ISD::SSUBSAT,    MVT::v16i8,   {  1 } },
    { ISD::UADDSAT,    MVT::v8i16,   {  1 } },
    { ISD::UADDSAT,    MVT::v16i8,   {  1 } },
    { ISD::UMAX,       MVT::v2i64,   {  4,  8, 15, 15 } },
    { ISD::UMAX,       MVT::v4i32,   {  2,  5,  8,  8 } },
    { ISD::UMAX,       MVT::v8i16,   {  1,  3,  3,  3 } },
    { ISD::UMAX,       MVT::v16i8,   {  1,  1,  1,  1 } },
    { ISD::UMIN,       MVT::v2i64,   {  4,  8, 15, 15 } },
    { ISD::UMIN,       MVT::v4i32,   {  2,  5,  8,  8 } },
    { ISD::UMIN,       MVT::v8i16,   {  1,  3,  3,  3 } },
    { ISD::UMIN,       MVT::v16i8,   {  1,  1,  1,  1 } },
    { ISD::USUBSAT,    MVT::v8i16,   {  1 } },
    { ISD::USUBSAT,    MVT::v16i8,   {  1 } },
    { ISD::FMAXNUM,    MVT::f64,     {  5,  5,  7,  7 } },
    { ISD::FMAXNUM,    MVT::v2f64,   {  4,  6,  6,  6 } },
    { ISD::FSQRT,      MVT::f64,     { 32, 32,  1,  1 } }, // Nehalem from http://www.agner.org/
    { ISD::FSQRT,      MVT::v2f64,   { 32, 32,  1,  1 } }, // Nehalem from http://www.agner.org/
  };
  static const CostKindTblEntry SSE1CostTbl[] = {
    { ISD::FMAXNUM,    MVT::f32,     {  5,  5,  7,  7 } },
    { ISD::FMAXNUM,    MVT::v4f32,   {  4,  6,  6,  6 } },
    { ISD::FSQRT,      MVT::f32,     { 28, 30,  1,  2 } }, // Pentium III from http://www.agner.org/
    { ISD::FSQRT,      MVT::v4f32,   { 56, 56,  1,  2 } }, // Pentium III from http://www.agner.org/
  };
  static const CostKindTblEntry BMI64CostTbl[] = { // 64-bit targets
    { ISD::CTTZ,       MVT::i64,     {  1 } },
  };
  static const CostKindTblEntry BMI32CostTbl[] = { // 32 or 64-bit targets
    { ISD::CTTZ,       MVT::i32,     {  1 } },
    { ISD::CTTZ,       MVT::i16,     {  1 } },
    { ISD::CTTZ,       MVT::i8,      {  1 } },
  };
  static const CostKindTblEntry LZCNT64CostTbl[] = { // 64-bit targets
    { ISD::CTLZ,       MVT::i64,     {  1 } },
  };
  static const CostKindTblEntry LZCNT32CostTbl[] = { // 32 or 64-bit targets
    { ISD::CTLZ,       MVT::i32,     {  1 } },
    { ISD::CTLZ,       MVT::i16,     {  2 } },
    { ISD::CTLZ,       MVT::i8,      {  2 } },
  };
  static const CostKindTblEntry POPCNT64CostTbl[] = { // 64-bit targets
    { ISD::CTPOP,      MVT::i64,     {  1, 1, 1, 1 } }, // popcnt
  };
  static const CostKindTblEntry POPCNT32CostTbl[] = { // 32 or 64-bit targets
    { ISD::CTPOP,      MVT::i32,     {  1, 1, 1, 1 } }, // popcnt
    { ISD::CTPOP,      MVT::i16,     {  1, 1, 2, 2 } }, // popcnt(zext())
    { ISD::CTPOP,      MVT::i8,      {  1, 1, 2, 2 } }, // popcnt(zext())
  };
  static const CostKindTblEntry X64CostTbl[] = { // 64-bit targets
    { ISD::ABS,        MVT::i64,     {  1,  2,  3,  3 } }, // SUB+CMOV
    { ISD::BITREVERSE, MVT::i64,     { 10, 12, 20, 22 } },
    { ISD::BSWAP,      MVT::i64,     {  1,  2,  1,  2 } },
    { ISD::CTLZ,       MVT::i64,     {  4 } }, // BSR+XOR or BSR+XOR+CMOV
    { ISD::CTLZ_ZERO_UNDEF, MVT::i64,{  1,  1,  1,  1 } }, // BSR+XOR
    { ISD::CTTZ,       MVT::i64,     {  3 } }, // TEST+BSF+CMOV/BRANCH
    { ISD::CTTZ_ZERO_UNDEF, MVT::i64,{  1,  1,  1,  1 } }, // BSR
    { ISD::CTPOP,      MVT::i64,     { 10,  6, 19, 19 } },
    { ISD::ROTL,       MVT::i64,     {  2, 3, 1, 3 } },
    { ISD::ROTR,       MVT::i64,     {  2, 3, 1, 3 } },
    { X86ISD::VROTLI,  MVT::i64,     {  1, 1, 1, 1 } },
    { ISD::FSHL,       MVT::i64,     {  4, 4, 1, 4 } },
    { ISD::SMAX,       MVT::i64,     {  1,  3,  2,  3 } },
    { ISD::SMIN,       MVT::i64,     {  1,  3,  2,  3 } },
    { ISD::UMAX,       MVT::i64,     {  1,  3,  2,  3 } },
    { ISD::UMIN,       MVT::i64,     {  1,  3,  2,  3 } },
    { ISD::SADDO,      MVT::i64,     {  1 } },
    { ISD::UADDO,      MVT::i64,     {  1 } },
    { ISD::UMULO,      MVT::i64,     {  2 } }, // mulq + seto
  };
  static const CostKindTblEntry X86CostTbl[] = { // 32 or 64-bit targets
    { ISD::ABS,        MVT::i32,     {  1,  2,  3,  3 } }, // SUB+XOR+SRA or SUB+CMOV
    { ISD::ABS,        MVT::i16,     {  2,  2,  3,  3 } }, // SUB+XOR+SRA or SUB+CMOV
    { ISD::ABS,        MVT::i8,      {  2,  4,  4,  3 } }, // SUB+XOR+SRA
    { ISD::BITREVERSE, MVT::i32,     {  9, 12, 17, 19 } },
    { ISD::BITREVERSE, MVT::i16,     {  9, 12, 17, 19 } },
    { ISD::BITREVERSE, MVT::i8,      {  7,  9, 13, 14 } },
    { ISD::BSWAP,      MVT::i32,     {  1,  1,  1,  1 } },
    { ISD::BSWAP,      MVT::i16,     {  1,  2,  1,  2 } }, // ROL
    { ISD::CTLZ,       MVT::i32,     {  4 } }, // BSR+XOR or BSR+XOR+CMOV
    { ISD::CTLZ,       MVT::i16,     {  4 } }, // BSR+XOR or BSR+XOR+CMOV
    { ISD::CTLZ,       MVT::i8,      {  4 } }, // BSR+XOR or BSR+XOR+CMOV
    { ISD::CTLZ_ZERO_UNDEF, MVT::i32,{  1,  1,  1,  1 } }, // BSR+XOR
    { ISD::CTLZ_ZERO_UNDEF, MVT::i16,{  2,  2,  3,  3 } }, // BSR+XOR
    { ISD::CTLZ_ZERO_UNDEF, MVT::i8, {  2,  2,  3,  3 } }, // BSR+XOR
    { ISD::CTTZ,       MVT::i32,     {  3 } }, // TEST+BSF+CMOV/BRANCH
    { ISD::CTTZ,       MVT::i16,     {  3 } }, // TEST+BSF+CMOV/BRANCH
    { ISD::CTTZ,       MVT::i8,      {  3 } }, // TEST+BSF+CMOV/BRANCH
    { ISD::CTTZ_ZERO_UNDEF, MVT::i32,{  1,  1,  1,  1 } }, // BSF
    { ISD::CTTZ_ZERO_UNDEF, MVT::i16,{  2,  2,  1,  1 } }, // BSF
    { ISD::CTTZ_ZERO_UNDEF, MVT::i8, {  2,  2,  1,  1 } }, // BSF
    { ISD::CTPOP,      MVT::i32,     {  8,  7, 15, 15 } },
    { ISD::CTPOP,      MVT::i16,     {  9,  8, 17, 17 } },
    { ISD::CTPOP,      MVT::i8,      {  7,  6,  6,  6 } },
    { ISD::ROTL,       MVT::i32,     {  2,  3,  1,  3 } },
    { ISD::ROTL,       MVT::i16,     {  2,  3,  1,  3 } },
    { ISD::ROTL,       MVT::i8,      {  2,  3,  1,  3 } },
    { ISD::ROTR,       MVT::i32,     {  2,  3,  1,  3 } },
    { ISD::ROTR,       MVT::i16,     {  2,  3,  1,  3 } },
    { ISD::ROTR,       MVT::i8,      {  2,  3,  1,  3 } },
    { X86ISD::VROTLI,  MVT::i32,     {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::i16,     {  1,  1,  1,  1 } },
    { X86ISD::VROTLI,  MVT::i8,      {  1,  1,  1,  1 } },
    { ISD::FSHL,       MVT::i32,     {  4,  4,  1,  4 } },
    { ISD::FSHL,       MVT::i16,     {  4,  4,  2,  5 } },
    { ISD::FSHL,       MVT::i8,      {  4,  4,  2,  5 } },
    { ISD::SMAX,       MVT::i32,     {  1,  2,  2,  3 } },
    { ISD::SMAX,       MVT::i16,     {  1,  4,  2,  4 } },
    { ISD::SMAX,       MVT::i8,      {  1,  4,  2,  4 } },
    { ISD::SMIN,       MVT::i32,     {  1,  2,  2,  3 } },
    { ISD::SMIN,       MVT::i16,     {  1,  4,  2,  4 } },
    { ISD::SMIN,       MVT::i8,      {  1,  4,  2,  4 } },
    { ISD::UMAX,       MVT::i32,     {  1,  2,  2,  3 } },
    { ISD::UMAX,       MVT::i16,     {  1,  4,  2,  4 } },
    { ISD::UMAX,       MVT::i8,      {  1,  4,  2,  4 } },
    { ISD::UMIN,       MVT::i32,     {  1,  2,  2,  3 } },
    { ISD::UMIN,       MVT::i16,     {  1,  4,  2,  4 } },
    { ISD::UMIN,       MVT::i8,      {  1,  4,  2,  4 } },
    { ISD::SADDO,      MVT::i32,     {  1 } },
    { ISD::SADDO,      MVT::i16,     {  1 } },
    { ISD::SADDO,      MVT::i8,      {  1 } },
    { ISD::UADDO,      MVT::i32,     {  1 } },
    { ISD::UADDO,      MVT::i16,     {  1 } },
    { ISD::UADDO,      MVT::i8,      {  1 } },
    { ISD::UMULO,      MVT::i32,     {  2 } }, // mul + seto
    { ISD::UMULO,      MVT::i16,     {  2 } },
    { ISD::UMULO,      MVT::i8,      {  2 } },
  };

  Type *RetTy = ICA.getReturnType();
  Type *OpTy = RetTy;
  Intrinsic::ID IID = ICA.getID();
  unsigned ISD = ISD::DELETED_NODE;
  switch (IID) {
  default:
    break;
  case Intrinsic::abs:
    ISD = ISD::ABS;
    break;
  case Intrinsic::bitreverse:
    ISD = ISD::BITREVERSE;
    break;
  case Intrinsic::bswap:
    ISD = ISD::BSWAP;
    break;
  case Intrinsic::ctlz:
    ISD = ISD::CTLZ;
    break;
  case Intrinsic::ctpop:
    ISD = ISD::CTPOP;
    break;
  case Intrinsic::cttz:
    ISD = ISD::CTTZ;
    break;
  case Intrinsic::fshl:
    ISD = ISD::FSHL;
    if (!ICA.isTypeBasedOnly()) {
      const SmallVectorImpl<const Value *> &Args = ICA.getArgs();
      if (Args[0] == Args[1]) {
        ISD = ISD::ROTL;
        // Handle uniform constant rotation amounts.
        // TODO: Handle funnel-shift cases.
        const APInt *Amt;
        if (Args[2] &&
            PatternMatch::match(Args[2], PatternMatch::m_APIntAllowPoison(Amt)))
          ISD = X86ISD::VROTLI;
      }
    }
    break;
  case Intrinsic::fshr:
    // FSHR has same costs so don't duplicate.
    ISD = ISD::FSHL;
    if (!ICA.isTypeBasedOnly()) {
      const SmallVectorImpl<const Value *> &Args = ICA.getArgs();
      if (Args[0] == Args[1]) {
        ISD = ISD::ROTR;
        // Handle uniform constant rotation amount.
        // TODO: Handle funnel-shift cases.
        const APInt *Amt;
        if (Args[2] &&
            PatternMatch::match(Args[2], PatternMatch::m_APIntAllowPoison(Amt)))
          ISD = X86ISD::VROTLI;
      }
    }
    break;
  case Intrinsic::lrint:
  case Intrinsic::llrint:
    // X86 can use the CVTP2SI instructions to lower lrint/llrint calls, which
    // have the same costs as the CVTTP2SI (fptosi) instructions
    if (!ICA.isTypeBasedOnly()) {
      const SmallVectorImpl<Type *> &ArgTys = ICA.getArgTypes();
      return getCastInstrCost(Instruction::FPToSI, RetTy, ArgTys[0],
                              TTI::CastContextHint::None, CostKind);
    }
    break;
  case Intrinsic::maxnum:
  case Intrinsic::minnum:
    // FMINNUM has same costs so don't duplicate.
    ISD = ISD::FMAXNUM;
    break;
  case Intrinsic::sadd_sat:
    ISD = ISD::SADDSAT;
    break;
  case Intrinsic::smax:
    ISD = ISD::SMAX;
    break;
  case Intrinsic::smin:
    ISD = ISD::SMIN;
    break;
  case Intrinsic::ssub_sat:
    ISD = ISD::SSUBSAT;
    break;
  case Intrinsic::uadd_sat:
    ISD = ISD::UADDSAT;
    break;
  case Intrinsic::umax:
    ISD = ISD::UMAX;
    break;
  case Intrinsic::umin:
    ISD = ISD::UMIN;
    break;
  case Intrinsic::usub_sat:
    ISD = ISD::USUBSAT;
    break;
  case Intrinsic::sqrt:
    ISD = ISD::FSQRT;
    break;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
    // SSUBO has same costs so don't duplicate.
    ISD = ISD::SADDO;
    OpTy = RetTy->getContainedType(0);
    break;
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::usub_with_overflow:
    // USUBO has same costs so don't duplicate.
    ISD = ISD::UADDO;
    OpTy = RetTy->getContainedType(0);
    break;
  case Intrinsic::umul_with_overflow:
  case Intrinsic::smul_with_overflow:
    // SMULO has same costs so don't duplicate.
    ISD = ISD::UMULO;
    OpTy = RetTy->getContainedType(0);
    break;
  }

  if (ISD != ISD::DELETED_NODE) {
    auto adjustTableCost = [&](int ISD, unsigned Cost,
                               std::pair<InstructionCost, MVT> LT,
                               FastMathFlags FMF) -> InstructionCost {
      InstructionCost LegalizationCost = LT.first;
      MVT MTy = LT.second;

      // If there are no NANs to deal with, then these are reduced to a
      // single MIN** or MAX** instruction instead of the MIN/CMP/SELECT that we
      // assume is used in the non-fast case.
      if (ISD == ISD::FMAXNUM || ISD == ISD::FMINNUM) {
        if (FMF.noNaNs())
          return LegalizationCost * 1;
      }

      // For cases where some ops can be folded into a load/store, assume free.
      if (MTy.isScalarInteger()) {
        if (ISD == ISD::BSWAP && ST->hasMOVBE() && ST->hasFastMOVBE()) {
          if (const Instruction *II = ICA.getInst()) {
            if (II->hasOneUse() && isa<StoreInst>(II->user_back()))
              return TTI::TCC_Free;
            if (auto *LI = dyn_cast<LoadInst>(II->getOperand(0))) {
              if (LI->hasOneUse())
                return TTI::TCC_Free;
            }
          }
        }
      }

      return LegalizationCost * (int)Cost;
    };

    // Legalize the type.
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(OpTy);
    MVT MTy = LT.second;

    // Without BMI/LZCNT see if we're only looking for a *_ZERO_UNDEF cost.
    if (((ISD == ISD::CTTZ && !ST->hasBMI()) ||
         (ISD == ISD::CTLZ && !ST->hasLZCNT())) &&
        !MTy.isVector() && !ICA.isTypeBasedOnly()) {
      const SmallVectorImpl<const Value *> &Args = ICA.getArgs();
      if (auto *Cst = dyn_cast<ConstantInt>(Args[1]))
        if (Cst->isAllOnesValue())
          ISD = ISD == ISD::CTTZ ? ISD::CTTZ_ZERO_UNDEF : ISD::CTLZ_ZERO_UNDEF;
    }

    // FSQRT is a single instruction.
    if (ISD == ISD::FSQRT && CostKind == TTI::TCK_CodeSize)
      return LT.first;

    if (ST->useGLMDivSqrtCosts())
      if (const auto *Entry = CostTableLookup(GLMCostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->useSLMArithCosts())
      if (const auto *Entry = CostTableLookup(SLMCostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasVBMI2())
      if (const auto *Entry = CostTableLookup(AVX512VBMI2CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasBITALG())
      if (const auto *Entry = CostTableLookup(AVX512BITALGCostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasVPOPCNTDQ())
      if (const auto *Entry = CostTableLookup(AVX512VPOPCNTDQCostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasGFNI())
      if (const auto *Entry = CostTableLookup(GFNICostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasCDI())
      if (const auto *Entry = CostTableLookup(AVX512CDCostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasBWI())
      if (const auto *Entry = CostTableLookup(AVX512BWCostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasAVX512())
      if (const auto *Entry = CostTableLookup(AVX512CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasXOP())
      if (const auto *Entry = CostTableLookup(XOPCostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasAVX2())
      if (const auto *Entry = CostTableLookup(AVX2CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasSSE42())
      if (const auto *Entry = CostTableLookup(SSE42CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasSSE41())
      if (const auto *Entry = CostTableLookup(SSE41CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasSSSE3())
      if (const auto *Entry = CostTableLookup(SSSE3CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasSSE2())
      if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasSSE1())
      if (const auto *Entry = CostTableLookup(SSE1CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (ST->hasBMI()) {
      if (ST->is64Bit())
        if (const auto *Entry = CostTableLookup(BMI64CostTbl, ISD, MTy))
          if (auto KindCost = Entry->Cost[CostKind])
            return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

      if (const auto *Entry = CostTableLookup(BMI32CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());
    }

    if (ST->hasLZCNT()) {
      if (ST->is64Bit())
        if (const auto *Entry = CostTableLookup(LZCNT64CostTbl, ISD, MTy))
          if (auto KindCost = Entry->Cost[CostKind])
            return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

      if (const auto *Entry = CostTableLookup(LZCNT32CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());
    }

    if (ST->hasPOPCNT()) {
      if (ST->is64Bit())
        if (const auto *Entry = CostTableLookup(POPCNT64CostTbl, ISD, MTy))
          if (auto KindCost = Entry->Cost[CostKind])
            return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

      if (const auto *Entry = CostTableLookup(POPCNT32CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());
    }

    if (ST->is64Bit())
      if (const auto *Entry = CostTableLookup(X64CostTbl, ISD, MTy))
        if (auto KindCost = Entry->Cost[CostKind])
          return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());

    if (const auto *Entry = CostTableLookup(X86CostTbl, ISD, MTy))
      if (auto KindCost = Entry->Cost[CostKind])
        return adjustTableCost(Entry->ISD, *KindCost, LT, ICA.getFlags());
  }

  return BaseT::getIntrinsicInstrCost(ICA, CostKind);
}

InstructionCost X86TTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val,
                                               TTI::TargetCostKind CostKind,
                                               unsigned Index, Value *Op0,
                                               Value *Op1) {
  static const CostTblEntry SLMCostTbl[] = {
     { ISD::EXTRACT_VECTOR_ELT,       MVT::i8,      4 },
     { ISD::EXTRACT_VECTOR_ELT,       MVT::i16,     4 },
     { ISD::EXTRACT_VECTOR_ELT,       MVT::i32,     4 },
     { ISD::EXTRACT_VECTOR_ELT,       MVT::i64,     7 }
   };

  assert(Val->isVectorTy() && "This must be a vector type");
  Type *ScalarType = Val->getScalarType();
  InstructionCost RegisterFileMoveCost = 0;

  // Non-immediate extraction/insertion can be handled as a sequence of
  // aliased loads+stores via the stack.
  if (Index == -1U && (Opcode == Instruction::ExtractElement ||
                       Opcode == Instruction::InsertElement)) {
    // TODO: On some SSE41+ targets, we expand to cmp+splat+select patterns:
    // inselt N0, N1, N2 --> select (SplatN2 == {0,1,2...}) ? SplatN1 : N0.

    // TODO: Move this to BasicTTIImpl.h? We'd need better gep + index handling.
    assert(isa<FixedVectorType>(Val) && "Fixed vector type expected");
    Align VecAlign = DL.getPrefTypeAlign(Val);
    Align SclAlign = DL.getPrefTypeAlign(ScalarType);

    // Extract - store vector to stack, load scalar.
    if (Opcode == Instruction::ExtractElement) {
      return getMemoryOpCost(Instruction::Store, Val, VecAlign, 0, CostKind) +
             getMemoryOpCost(Instruction::Load, ScalarType, SclAlign, 0,
                             CostKind);
    }
    // Insert - store vector to stack, store scalar, load vector.
    if (Opcode == Instruction::InsertElement) {
      return getMemoryOpCost(Instruction::Store, Val, VecAlign, 0, CostKind) +
             getMemoryOpCost(Instruction::Store, ScalarType, SclAlign, 0,
                             CostKind) +
             getMemoryOpCost(Instruction::Load, Val, VecAlign, 0, CostKind);
    }
  }

  if (Index != -1U && (Opcode == Instruction::ExtractElement ||
                       Opcode == Instruction::InsertElement)) {
    // Extraction of vXi1 elements are now efficiently handled by MOVMSK.
    if (Opcode == Instruction::ExtractElement &&
        ScalarType->getScalarSizeInBits() == 1 &&
        cast<FixedVectorType>(Val)->getNumElements() > 1)
      return 1;

    // Legalize the type.
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Val);

    // This type is legalized to a scalar type.
    if (!LT.second.isVector())
      return 0;

    // The type may be split. Normalize the index to the new type.
    unsigned SizeInBits = LT.second.getSizeInBits();
    unsigned NumElts = LT.second.getVectorNumElements();
    unsigned SubNumElts = NumElts;
    Index = Index % NumElts;

    // For >128-bit vectors, we need to extract higher 128-bit subvectors.
    // For inserts, we also need to insert the subvector back.
    if (SizeInBits > 128) {
      assert((SizeInBits % 128) == 0 && "Illegal vector");
      unsigned NumSubVecs = SizeInBits / 128;
      SubNumElts = NumElts / NumSubVecs;
      if (SubNumElts <= Index) {
        RegisterFileMoveCost += (Opcode == Instruction::InsertElement ? 2 : 1);
        Index %= SubNumElts;
      }
    }

    MVT MScalarTy = LT.second.getScalarType();
    auto IsCheapPInsrPExtrInsertPS = [&]() {
      // Assume pinsr/pextr XMM <-> GPR is relatively cheap on all targets.
      // Also, assume insertps is relatively cheap on all >= SSE41 targets.
      return (MScalarTy == MVT::i16 && ST->hasSSE2()) ||
             (MScalarTy.isInteger() && ST->hasSSE41()) ||
             (MScalarTy == MVT::f32 && ST->hasSSE41() &&
              Opcode == Instruction::InsertElement);
    };

    if (Index == 0) {
      // Floating point scalars are already located in index #0.
      // Many insertions to #0 can fold away for scalar fp-ops, so let's assume
      // true for all.
      if (ScalarType->isFloatingPointTy() &&
          (Opcode != Instruction::InsertElement || !Op0 ||
           isa<UndefValue>(Op0)))
        return RegisterFileMoveCost;

      if (Opcode == Instruction::InsertElement &&
          isa_and_nonnull<UndefValue>(Op0)) {
        // Consider the gather cost to be cheap.
        if (isa_and_nonnull<LoadInst>(Op1))
          return RegisterFileMoveCost;
        if (!IsCheapPInsrPExtrInsertPS()) {
          // mov constant-to-GPR + movd/movq GPR -> XMM.
          if (isa_and_nonnull<Constant>(Op1) && Op1->getType()->isIntegerTy())
            return 2 + RegisterFileMoveCost;
          // Assume movd/movq GPR -> XMM is relatively cheap on all targets.
          return 1 + RegisterFileMoveCost;
        }
      }

      // Assume movd/movq XMM -> GPR is relatively cheap on all targets.
      if (ScalarType->isIntegerTy() && Opcode == Instruction::ExtractElement)
        return 1 + RegisterFileMoveCost;
    }

    int ISD = TLI->InstructionOpcodeToISD(Opcode);
    assert(ISD && "Unexpected vector opcode");
    if (ST->useSLMArithCosts())
      if (auto *Entry = CostTableLookup(SLMCostTbl, ISD, MScalarTy))
        return Entry->Cost + RegisterFileMoveCost;

    // Consider cheap cases.
    if (IsCheapPInsrPExtrInsertPS())
      return 1 + RegisterFileMoveCost;

    // For extractions we just need to shuffle the element to index 0, which
    // should be very cheap (assume cost = 1). For insertions we need to shuffle
    // the elements to its destination. In both cases we must handle the
    // subvector move(s).
    // If the vector type is already less than 128-bits then don't reduce it.
    // TODO: Under what circumstances should we shuffle using the full width?
    InstructionCost ShuffleCost = 1;
    if (Opcode == Instruction::InsertElement) {
      auto *SubTy = cast<VectorType>(Val);
      EVT VT = TLI->getValueType(DL, Val);
      if (VT.getScalarType() != MScalarTy || VT.getSizeInBits() >= 128)
        SubTy = FixedVectorType::get(ScalarType, SubNumElts);
      ShuffleCost = getShuffleCost(TTI::SK_PermuteTwoSrc, SubTy, std::nullopt,
                                   CostKind, 0, SubTy);
    }
    int IntOrFpCost = ScalarType->isFloatingPointTy() ? 0 : 1;
    return ShuffleCost + IntOrFpCost + RegisterFileMoveCost;
  }

  return BaseT::getVectorInstrCost(Opcode, Val, CostKind, Index, Op0, Op1) +
         RegisterFileMoveCost;
}

InstructionCost
X86TTIImpl::getScalarizationOverhead(VectorType *Ty, const APInt &DemandedElts,
                                     bool Insert, bool Extract,
                                     TTI::TargetCostKind CostKind) {
  assert(DemandedElts.getBitWidth() ==
             cast<FixedVectorType>(Ty)->getNumElements() &&
         "Vector size mismatch");

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);
  MVT MScalarTy = LT.second.getScalarType();
  unsigned LegalVectorBitWidth = LT.second.getSizeInBits();
  InstructionCost Cost = 0;

  constexpr unsigned LaneBitWidth = 128;
  assert((LegalVectorBitWidth < LaneBitWidth ||
          (LegalVectorBitWidth % LaneBitWidth) == 0) &&
         "Illegal vector");

  const int NumLegalVectors = *LT.first.getValue();
  assert(NumLegalVectors >= 0 && "Negative cost!");

  // For insertions, a ISD::BUILD_VECTOR style vector initialization can be much
  // cheaper than an accumulation of ISD::INSERT_VECTOR_ELT.
  if (Insert) {
    if ((MScalarTy == MVT::i16 && ST->hasSSE2()) ||
        (MScalarTy.isInteger() && ST->hasSSE41()) ||
        (MScalarTy == MVT::f32 && ST->hasSSE41())) {
      // For types we can insert directly, insertion into 128-bit sub vectors is
      // cheap, followed by a cheap chain of concatenations.
      if (LegalVectorBitWidth <= LaneBitWidth) {
        Cost += BaseT::getScalarizationOverhead(Ty, DemandedElts, Insert,
                                                /*Extract*/ false, CostKind);
      } else {
        // In each 128-lane, if at least one index is demanded but not all
        // indices are demanded and this 128-lane is not the first 128-lane of
        // the legalized-vector, then this 128-lane needs a extracti128; If in
        // each 128-lane, there is at least one demanded index, this 128-lane
        // needs a inserti128.

        // The following cases will help you build a better understanding:
        // Assume we insert several elements into a v8i32 vector in avx2,
        // Case#1: inserting into 1th index needs vpinsrd + inserti128.
        // Case#2: inserting into 5th index needs extracti128 + vpinsrd +
        // inserti128.
        // Case#3: inserting into 4,5,6,7 index needs 4*vpinsrd + inserti128.
        assert((LegalVectorBitWidth % LaneBitWidth) == 0 && "Illegal vector");
        unsigned NumLegalLanes = LegalVectorBitWidth / LaneBitWidth;
        unsigned NumLanesTotal = NumLegalLanes * NumLegalVectors;
        unsigned NumLegalElts =
            LT.second.getVectorNumElements() * NumLegalVectors;
        assert(NumLegalElts >= DemandedElts.getBitWidth() &&
               "Vector has been legalized to smaller element count");
        assert((NumLegalElts % NumLanesTotal) == 0 &&
               "Unexpected elts per lane");
        unsigned NumEltsPerLane = NumLegalElts / NumLanesTotal;

        APInt WidenedDemandedElts = DemandedElts.zext(NumLegalElts);
        auto *LaneTy =
            FixedVectorType::get(Ty->getElementType(), NumEltsPerLane);

        for (unsigned I = 0; I != NumLanesTotal; ++I) {
          APInt LaneEltMask = WidenedDemandedElts.extractBits(
              NumEltsPerLane, NumEltsPerLane * I);
          if (LaneEltMask.isZero())
            continue;
          // FIXME: we don't need to extract if all non-demanded elements
          //        are legalization-inserted padding.
          if (!LaneEltMask.isAllOnes())
            Cost += getShuffleCost(TTI::SK_ExtractSubvector, Ty, std::nullopt,
                                   CostKind, I * NumEltsPerLane, LaneTy);
          Cost += BaseT::getScalarizationOverhead(LaneTy, LaneEltMask, Insert,
                                                  /*Extract*/ false, CostKind);
        }

        APInt AffectedLanes =
            APIntOps::ScaleBitMask(WidenedDemandedElts, NumLanesTotal);
        APInt FullyAffectedLegalVectors = APIntOps::ScaleBitMask(
            AffectedLanes, NumLegalVectors, /*MatchAllBits=*/true);
        for (int LegalVec = 0; LegalVec != NumLegalVectors; ++LegalVec) {
          for (unsigned Lane = 0; Lane != NumLegalLanes; ++Lane) {
            unsigned I = NumLegalLanes * LegalVec + Lane;
            // No need to insert unaffected lane; or lane 0 of each legal vector
            // iff ALL lanes of that vector were affected and will be inserted.
            if (!AffectedLanes[I] ||
                (Lane == 0 && FullyAffectedLegalVectors[LegalVec]))
              continue;
            Cost += getShuffleCost(TTI::SK_InsertSubvector, Ty, std::nullopt,
                                   CostKind, I * NumEltsPerLane, LaneTy);
          }
        }
      }
    } else if (LT.second.isVector()) {
      // Without fast insertion, we need to use MOVD/MOVQ to pass each demanded
      // integer element as a SCALAR_TO_VECTOR, then we build the vector as a
      // series of UNPCK followed by CONCAT_VECTORS - all of these can be
      // considered cheap.
      if (Ty->isIntOrIntVectorTy())
        Cost += DemandedElts.popcount();

      // Get the smaller of the legalized or original pow2-extended number of
      // vector elements, which represents the number of unpacks we'll end up
      // performing.
      unsigned NumElts = LT.second.getVectorNumElements();
      unsigned Pow2Elts =
          PowerOf2Ceil(cast<FixedVectorType>(Ty)->getNumElements());
      Cost += (std::min<unsigned>(NumElts, Pow2Elts) - 1) * LT.first;
    }
  }

  if (Extract) {
    // vXi1 can be efficiently extracted with MOVMSK.
    // TODO: AVX512 predicate mask handling.
    // NOTE: This doesn't work well for roundtrip scalarization.
    if (!Insert && Ty->getScalarSizeInBits() == 1 && !ST->hasAVX512()) {
      unsigned NumElts = cast<FixedVectorType>(Ty)->getNumElements();
      unsigned MaxElts = ST->hasAVX2() ? 32 : 16;
      unsigned MOVMSKCost = (NumElts + MaxElts - 1) / MaxElts;
      return MOVMSKCost;
    }

    if (LT.second.isVector()) {
      unsigned NumLegalElts =
          LT.second.getVectorNumElements() * NumLegalVectors;
      assert(NumLegalElts >= DemandedElts.getBitWidth() &&
             "Vector has been legalized to smaller element count");

      // If we're extracting elements from a 128-bit subvector lane,
      // we only need to extract each lane once, not for every element.
      if (LegalVectorBitWidth > LaneBitWidth) {
        unsigned NumLegalLanes = LegalVectorBitWidth / LaneBitWidth;
        unsigned NumLanesTotal = NumLegalLanes * NumLegalVectors;
        assert((NumLegalElts % NumLanesTotal) == 0 &&
               "Unexpected elts per lane");
        unsigned NumEltsPerLane = NumLegalElts / NumLanesTotal;

        // Add cost for each demanded 128-bit subvector extraction.
        // Luckily this is a lot easier than for insertion.
        APInt WidenedDemandedElts = DemandedElts.zext(NumLegalElts);
        auto *LaneTy =
            FixedVectorType::get(Ty->getElementType(), NumEltsPerLane);

        for (unsigned I = 0; I != NumLanesTotal; ++I) {
          APInt LaneEltMask = WidenedDemandedElts.extractBits(
              NumEltsPerLane, I * NumEltsPerLane);
          if (LaneEltMask.isZero())
            continue;
          Cost += getShuffleCost(TTI::SK_ExtractSubvector, Ty, std::nullopt,
                                 CostKind, I * NumEltsPerLane, LaneTy);
          Cost += BaseT::getScalarizationOverhead(
              LaneTy, LaneEltMask, /*Insert*/ false, Extract, CostKind);
        }

        return Cost;
      }
    }

    // Fallback to default extraction.
    Cost += BaseT::getScalarizationOverhead(Ty, DemandedElts, /*Insert*/ false,
                                            Extract, CostKind);
  }

  return Cost;
}

InstructionCost
X86TTIImpl::getReplicationShuffleCost(Type *EltTy, int ReplicationFactor,
                                      int VF, const APInt &DemandedDstElts,
                                      TTI::TargetCostKind CostKind) {
  const unsigned EltTyBits = DL.getTypeSizeInBits(EltTy);
  // We don't differentiate element types here, only element bit width.
  EltTy = IntegerType::getIntNTy(EltTy->getContext(), EltTyBits);

  auto bailout = [&]() {
    return BaseT::getReplicationShuffleCost(EltTy, ReplicationFactor, VF,
                                            DemandedDstElts, CostKind);
  };

  // For now, only deal with AVX512 cases.
  if (!ST->hasAVX512())
    return bailout();

  // Do we have a native shuffle for this element type, or should we promote?
  unsigned PromEltTyBits = EltTyBits;
  switch (EltTyBits) {
  case 32:
  case 64:
    break; // AVX512F.
  case 16:
    if (!ST->hasBWI())
      PromEltTyBits = 32; // promote to i32, AVX512F.
    break;                // AVX512BW
  case 8:
    if (!ST->hasVBMI())
      PromEltTyBits = 32; // promote to i32, AVX512F.
    break;                // AVX512VBMI
  case 1:
    // There is no support for shuffling i1 elements. We *must* promote.
    if (ST->hasBWI()) {
      if (ST->hasVBMI())
        PromEltTyBits = 8; // promote to i8, AVX512VBMI.
      else
        PromEltTyBits = 16; // promote to i16, AVX512BW.
      break;
    }
    PromEltTyBits = 32; // promote to i32, AVX512F.
    break;
  default:
    return bailout();
  }
  auto *PromEltTy = IntegerType::getIntNTy(EltTy->getContext(), PromEltTyBits);

  auto *SrcVecTy = FixedVectorType::get(EltTy, VF);
  auto *PromSrcVecTy = FixedVectorType::get(PromEltTy, VF);

  int NumDstElements = VF * ReplicationFactor;
  auto *PromDstVecTy = FixedVectorType::get(PromEltTy, NumDstElements);
  auto *DstVecTy = FixedVectorType::get(EltTy, NumDstElements);

  // Legalize the types.
  MVT LegalSrcVecTy = getTypeLegalizationCost(SrcVecTy).second;
  MVT LegalPromSrcVecTy = getTypeLegalizationCost(PromSrcVecTy).second;
  MVT LegalPromDstVecTy = getTypeLegalizationCost(PromDstVecTy).second;
  MVT LegalDstVecTy = getTypeLegalizationCost(DstVecTy).second;
  // They should have legalized into vector types.
  if (!LegalSrcVecTy.isVector() || !LegalPromSrcVecTy.isVector() ||
      !LegalPromDstVecTy.isVector() || !LegalDstVecTy.isVector())
    return bailout();

  if (PromEltTyBits != EltTyBits) {
    // If we have to perform the shuffle with wider elt type than our data type,
    // then we will first need to anyext (we don't care about the new bits)
    // the source elements, and then truncate Dst elements.
    InstructionCost PromotionCost;
    PromotionCost += getCastInstrCost(
        Instruction::SExt, /*Dst=*/PromSrcVecTy, /*Src=*/SrcVecTy,
        TargetTransformInfo::CastContextHint::None, CostKind);
    PromotionCost +=
        getCastInstrCost(Instruction::Trunc, /*Dst=*/DstVecTy,
                         /*Src=*/PromDstVecTy,
                         TargetTransformInfo::CastContextHint::None, CostKind);
    return PromotionCost + getReplicationShuffleCost(PromEltTy,
                                                     ReplicationFactor, VF,
                                                     DemandedDstElts, CostKind);
  }

  assert(LegalSrcVecTy.getScalarSizeInBits() == EltTyBits &&
         LegalSrcVecTy.getScalarType() == LegalDstVecTy.getScalarType() &&
         "We expect that the legalization doesn't affect the element width, "
         "doesn't coalesce/split elements.");

  unsigned NumEltsPerDstVec = LegalDstVecTy.getVectorNumElements();
  unsigned NumDstVectors =
      divideCeil(DstVecTy->getNumElements(), NumEltsPerDstVec);

  auto *SingleDstVecTy = FixedVectorType::get(EltTy, NumEltsPerDstVec);

  // Not all the produced Dst elements may be demanded. In our case,
  // given that a single Dst vector is formed by a single shuffle,
  // if all elements that will form a single Dst vector aren't demanded,
  // then we won't need to do that shuffle, so adjust the cost accordingly.
  APInt DemandedDstVectors = APIntOps::ScaleBitMask(
      DemandedDstElts.zext(NumDstVectors * NumEltsPerDstVec), NumDstVectors);
  unsigned NumDstVectorsDemanded = DemandedDstVectors.popcount();

  InstructionCost SingleShuffleCost = getShuffleCost(
      TTI::SK_PermuteSingleSrc, SingleDstVecTy, /*Mask=*/std::nullopt, CostKind,
      /*Index=*/0, /*SubTp=*/nullptr);
  return NumDstVectorsDemanded * SingleShuffleCost;
}

InstructionCost X86TTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src,
                                            MaybeAlign Alignment,
                                            unsigned AddressSpace,
                                            TTI::TargetCostKind CostKind,
                                            TTI::OperandValueInfo OpInfo,
                                            const Instruction *I) {
  // TODO: Handle other cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput) {
    if (auto *SI = dyn_cast_or_null<StoreInst>(I)) {
      // Store instruction with index and scale costs 2 Uops.
      // Check the preceding GEP to identify non-const indices.
      if (auto *GEP = dyn_cast<GetElementPtrInst>(SI->getPointerOperand())) {
        if (!all_of(GEP->indices(), [](Value *V) { return isa<Constant>(V); }))
          return TTI::TCC_Basic * 2;
      }
    }
    return TTI::TCC_Basic;
  }

  assert((Opcode == Instruction::Load || Opcode == Instruction::Store) &&
         "Invalid Opcode");
  // Type legalization can't handle structs
  if (TLI->getValueType(DL, Src, true) == MVT::Other)
    return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                  CostKind);

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Src);

  auto *VTy = dyn_cast<FixedVectorType>(Src);

  InstructionCost Cost = 0;

  // Add a cost for constant load to vector.
  if (Opcode == Instruction::Store && OpInfo.isConstant())
    Cost += getMemoryOpCost(Instruction::Load, Src, DL.getABITypeAlign(Src),
                            /*AddressSpace=*/0, CostKind);

  // Handle the simple case of non-vectors.
  // NOTE: this assumes that legalization never creates vector from scalars!
  if (!VTy || !LT.second.isVector()) {
    // Each load/store unit costs 1.
    return (LT.second.isFloatingPoint() ? Cost : 0) + LT.first * 1;
  }

  bool IsLoad = Opcode == Instruction::Load;

  Type *EltTy = VTy->getElementType();

  const int EltTyBits = DL.getTypeSizeInBits(EltTy);

  // Source of truth: how many elements were there in the original IR vector?
  const unsigned SrcNumElt = VTy->getNumElements();

  // How far have we gotten?
  int NumEltRemaining = SrcNumElt;
  // Note that we intentionally capture by-reference, NumEltRemaining changes.
  auto NumEltDone = [&]() { return SrcNumElt - NumEltRemaining; };

  const int MaxLegalOpSizeBytes = divideCeil(LT.second.getSizeInBits(), 8);

  // Note that even if we can store 64 bits of an XMM, we still operate on XMM.
  const unsigned XMMBits = 128;
  if (XMMBits % EltTyBits != 0)
    // Vector size must be a multiple of the element size. I.e. no padding.
    return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                  CostKind);
  const int NumEltPerXMM = XMMBits / EltTyBits;

  auto *XMMVecTy = FixedVectorType::get(EltTy, NumEltPerXMM);

  for (int CurrOpSizeBytes = MaxLegalOpSizeBytes, SubVecEltsLeft = 0;
       NumEltRemaining > 0; CurrOpSizeBytes /= 2) {
    // How many elements would a single op deal with at once?
    if ((8 * CurrOpSizeBytes) % EltTyBits != 0)
      // Vector size must be a multiple of the element size. I.e. no padding.
      return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                    CostKind);
    int CurrNumEltPerOp = (8 * CurrOpSizeBytes) / EltTyBits;

    assert(CurrOpSizeBytes > 0 && CurrNumEltPerOp > 0 && "How'd we get here?");
    assert((((NumEltRemaining * EltTyBits) < (2 * 8 * CurrOpSizeBytes)) ||
            (CurrOpSizeBytes == MaxLegalOpSizeBytes)) &&
           "Unless we haven't halved the op size yet, "
           "we have less than two op's sized units of work left.");

    auto *CurrVecTy = CurrNumEltPerOp > NumEltPerXMM
                          ? FixedVectorType::get(EltTy, CurrNumEltPerOp)
                          : XMMVecTy;

    assert(CurrVecTy->getNumElements() % CurrNumEltPerOp == 0 &&
           "After halving sizes, the vector elt count is no longer a multiple "
           "of number of elements per operation?");
    auto *CoalescedVecTy =
        CurrNumEltPerOp == 1
            ? CurrVecTy
            : FixedVectorType::get(
                  IntegerType::get(Src->getContext(),
                                   EltTyBits * CurrNumEltPerOp),
                  CurrVecTy->getNumElements() / CurrNumEltPerOp);
    assert(DL.getTypeSizeInBits(CoalescedVecTy) ==
               DL.getTypeSizeInBits(CurrVecTy) &&
           "coalesciing elements doesn't change vector width.");

    while (NumEltRemaining > 0) {
      assert(SubVecEltsLeft >= 0 && "Subreg element count overconsumtion?");

      // Can we use this vector size, as per the remaining element count?
      // Iff the vector is naturally aligned, we can do a wide load regardless.
      if (NumEltRemaining < CurrNumEltPerOp &&
          (!IsLoad || Alignment.valueOrOne() < CurrOpSizeBytes) &&
          CurrOpSizeBytes != 1)
        break; // Try smalled vector size.

      bool Is0thSubVec = (NumEltDone() % LT.second.getVectorNumElements()) == 0;

      // If we have fully processed the previous reg, we need to replenish it.
      if (SubVecEltsLeft == 0) {
        SubVecEltsLeft += CurrVecTy->getNumElements();
        // And that's free only for the 0'th subvector of a legalized vector.
        if (!Is0thSubVec)
          Cost += getShuffleCost(IsLoad ? TTI::ShuffleKind::SK_InsertSubvector
                                        : TTI::ShuffleKind::SK_ExtractSubvector,
                                 VTy, std::nullopt, CostKind, NumEltDone(),
                                 CurrVecTy);
      }

      // While we can directly load/store ZMM, YMM, and 64-bit halves of XMM,
      // for smaller widths (32/16/8) we have to insert/extract them separately.
      // Again, it's free for the 0'th subreg (if op is 32/64 bit wide,
      // but let's pretend that it is also true for 16/8 bit wide ops...)
      if (CurrOpSizeBytes <= 32 / 8 && !Is0thSubVec) {
        int NumEltDoneInCurrXMM = NumEltDone() % NumEltPerXMM;
        assert(NumEltDoneInCurrXMM % CurrNumEltPerOp == 0 && "");
        int CoalescedVecEltIdx = NumEltDoneInCurrXMM / CurrNumEltPerOp;
        APInt DemandedElts =
            APInt::getBitsSet(CoalescedVecTy->getNumElements(),
                              CoalescedVecEltIdx, CoalescedVecEltIdx + 1);
        assert(DemandedElts.popcount() == 1 && "Inserting single value");
        Cost += getScalarizationOverhead(CoalescedVecTy, DemandedElts, IsLoad,
                                         !IsLoad, CostKind);
      }

      // This isn't exactly right. We're using slow unaligned 32-byte accesses
      // as a proxy for a double-pumped AVX memory interface such as on
      // Sandybridge.
      // Sub-32-bit loads/stores will be slower either with PINSR*/PEXTR* or
      // will be scalarized.
      if (CurrOpSizeBytes == 32 && ST->isUnalignedMem32Slow())
        Cost += 2;
      else if (CurrOpSizeBytes < 4)
        Cost += 2;
      else
        Cost += 1;

      SubVecEltsLeft -= CurrNumEltPerOp;
      NumEltRemaining -= CurrNumEltPerOp;
      Alignment = commonAlignment(Alignment.valueOrOne(), CurrOpSizeBytes);
    }
  }

  assert(NumEltRemaining <= 0 && "Should have processed all the elements.");

  return Cost;
}

InstructionCost
X86TTIImpl::getMaskedMemoryOpCost(unsigned Opcode, Type *SrcTy, Align Alignment,
                                  unsigned AddressSpace,
                                  TTI::TargetCostKind CostKind) {
  bool IsLoad = (Instruction::Load == Opcode);
  bool IsStore = (Instruction::Store == Opcode);

  auto *SrcVTy = dyn_cast<FixedVectorType>(SrcTy);
  if (!SrcVTy)
    // To calculate scalar take the regular cost, without mask
    return getMemoryOpCost(Opcode, SrcTy, Alignment, AddressSpace, CostKind);

  unsigned NumElem = SrcVTy->getNumElements();
  auto *MaskTy =
      FixedVectorType::get(Type::getInt8Ty(SrcVTy->getContext()), NumElem);
  if ((IsLoad && !isLegalMaskedLoad(SrcVTy, Alignment)) ||
      (IsStore && !isLegalMaskedStore(SrcVTy, Alignment))) {
    // Scalarization
    APInt DemandedElts = APInt::getAllOnes(NumElem);
    InstructionCost MaskSplitCost = getScalarizationOverhead(
        MaskTy, DemandedElts, /*Insert*/ false, /*Extract*/ true, CostKind);
    InstructionCost ScalarCompareCost = getCmpSelInstrCost(
        Instruction::ICmp, Type::getInt8Ty(SrcVTy->getContext()), nullptr,
        CmpInst::BAD_ICMP_PREDICATE, CostKind);
    InstructionCost BranchCost = getCFInstrCost(Instruction::Br, CostKind);
    InstructionCost MaskCmpCost = NumElem * (BranchCost + ScalarCompareCost);
    InstructionCost ValueSplitCost = getScalarizationOverhead(
        SrcVTy, DemandedElts, IsLoad, IsStore, CostKind);
    InstructionCost MemopCost =
        NumElem * BaseT::getMemoryOpCost(Opcode, SrcVTy->getScalarType(),
                                         Alignment, AddressSpace, CostKind);
    return MemopCost + ValueSplitCost + MaskSplitCost + MaskCmpCost;
  }

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(SrcVTy);
  auto VT = TLI->getValueType(DL, SrcVTy);
  InstructionCost Cost = 0;
  MVT Ty = LT.second;
  if (Ty == MVT::i16 || Ty == MVT::i32 || Ty == MVT::i64)
    // APX masked load/store for scalar is cheap.
    return Cost + LT.first;

  if (VT.isSimple() && Ty != VT.getSimpleVT() &&
      LT.second.getVectorNumElements() == NumElem)
    // Promotion requires extend/truncate for data and a shuffle for mask.
    Cost += getShuffleCost(TTI::SK_PermuteTwoSrc, SrcVTy, std::nullopt,
                           CostKind, 0, nullptr) +
            getShuffleCost(TTI::SK_PermuteTwoSrc, MaskTy, std::nullopt,
                           CostKind, 0, nullptr);

  else if (LT.first * Ty.getVectorNumElements() > NumElem) {
    auto *NewMaskTy = FixedVectorType::get(MaskTy->getElementType(),
                                           Ty.getVectorNumElements());
    // Expanding requires fill mask with zeroes
    Cost += getShuffleCost(TTI::SK_InsertSubvector, NewMaskTy, std::nullopt,
                           CostKind, 0, MaskTy);
  }

  // Pre-AVX512 - each maskmov load costs 2 + store costs ~8.
  if (!ST->hasAVX512())
    return Cost + LT.first * (IsLoad ? 2 : 8);

  // AVX-512 masked load/store is cheaper
  return Cost + LT.first;
}

InstructionCost
X86TTIImpl::getPointersChainCost(ArrayRef<const Value *> Ptrs,
                                 const Value *Base,
                                 const TTI::PointersChainInfo &Info,
                                 Type *AccessTy, TTI::TargetCostKind CostKind) {
  if (Info.isSameBase() && Info.isKnownStride()) {
    // If all the pointers have known stride all the differences are translated
    // into constants. X86 memory addressing allows encoding it into
    // displacement. So we just need to take the base GEP cost.
    if (const auto *BaseGEP = dyn_cast<GetElementPtrInst>(Base)) {
      SmallVector<const Value *> Indices(BaseGEP->indices());
      return getGEPCost(BaseGEP->getSourceElementType(),
                        BaseGEP->getPointerOperand(), Indices, nullptr,
                        CostKind);
    }
    return TTI::TCC_Free;
  }
  return BaseT::getPointersChainCost(Ptrs, Base, Info, AccessTy, CostKind);
}

InstructionCost X86TTIImpl::getAddressComputationCost(Type *Ty,
                                                      ScalarEvolution *SE,
                                                      const SCEV *Ptr) {
  // Address computations in vectorized code with non-consecutive addresses will
  // likely result in more instructions compared to scalar code where the
  // computation can more often be merged into the index mode. The resulting
  // extra micro-ops can significantly decrease throughput.
  const unsigned NumVectorInstToHideOverhead = 10;

  // Cost modeling of Strided Access Computation is hidden by the indexing
  // modes of X86 regardless of the stride value. We dont believe that there
  // is a difference between constant strided access in gerenal and constant
  // strided value which is less than or equal to 64.
  // Even in the case of (loop invariant) stride whose value is not known at
  // compile time, the address computation will not incur more than one extra
  // ADD instruction.
  if (Ty->isVectorTy() && SE && !ST->hasAVX2()) {
    // TODO: AVX2 is the current cut-off because we don't have correct
    //       interleaving costs for prior ISA's.
    if (!BaseT::isStridedAccess(Ptr))
      return NumVectorInstToHideOverhead;
    if (!BaseT::getConstantStrideStep(SE, Ptr))
      return 1;
  }

  return BaseT::getAddressComputationCost(Ty, SE, Ptr);
}

InstructionCost
X86TTIImpl::getArithmeticReductionCost(unsigned Opcode, VectorType *ValTy,
                                       std::optional<FastMathFlags> FMF,
                                       TTI::TargetCostKind CostKind) {
  if (TTI::requiresOrderedReduction(FMF))
    return BaseT::getArithmeticReductionCost(Opcode, ValTy, FMF, CostKind);

  // We use the Intel Architecture Code Analyzer(IACA) to measure the throughput
  // and make it as the cost.

  static const CostTblEntry SLMCostTbl[] = {
    { ISD::FADD,  MVT::v2f64,   3 },
    { ISD::ADD,   MVT::v2i64,   5 },
  };

  static const CostTblEntry SSE2CostTbl[] = {
    { ISD::FADD,  MVT::v2f64,   2 },
    { ISD::FADD,  MVT::v2f32,   2 },
    { ISD::FADD,  MVT::v4f32,   4 },
    { ISD::ADD,   MVT::v2i64,   2 },      // The data reported by the IACA tool is "1.6".
    { ISD::ADD,   MVT::v2i32,   2 }, // FIXME: chosen to be less than v4i32
    { ISD::ADD,   MVT::v4i32,   3 },      // The data reported by the IACA tool is "3.3".
    { ISD::ADD,   MVT::v2i16,   2 },      // The data reported by the IACA tool is "4.3".
    { ISD::ADD,   MVT::v4i16,   3 },      // The data reported by the IACA tool is "4.3".
    { ISD::ADD,   MVT::v8i16,   4 },      // The data reported by the IACA tool is "4.3".
    { ISD::ADD,   MVT::v2i8,    2 },
    { ISD::ADD,   MVT::v4i8,    2 },
    { ISD::ADD,   MVT::v8i8,    2 },
    { ISD::ADD,   MVT::v16i8,   3 },
  };

  static const CostTblEntry AVX1CostTbl[] = {
    { ISD::FADD,  MVT::v4f64,   3 },
    { ISD::FADD,  MVT::v4f32,   3 },
    { ISD::FADD,  MVT::v8f32,   4 },
    { ISD::ADD,   MVT::v2i64,   1 },      // The data reported by the IACA tool is "1.5".
    { ISD::ADD,   MVT::v4i64,   3 },
    { ISD::ADD,   MVT::v8i32,   5 },
    { ISD::ADD,   MVT::v16i16,  5 },
    { ISD::ADD,   MVT::v32i8,   4 },
  };

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // Before legalizing the type, give a chance to look up illegal narrow types
  // in the table.
  // FIXME: Is there a better way to do this?
  EVT VT = TLI->getValueType(DL, ValTy);
  if (VT.isSimple()) {
    MVT MTy = VT.getSimpleVT();
    if (ST->useSLMArithCosts())
      if (const auto *Entry = CostTableLookup(SLMCostTbl, ISD, MTy))
        return Entry->Cost;

    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
        return Entry->Cost;

    if (ST->hasSSE2())
      if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
        return Entry->Cost;
  }

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);

  MVT MTy = LT.second;

  auto *ValVTy = cast<FixedVectorType>(ValTy);

  // Special case: vXi8 mul reductions are performed as vXi16.
  if (ISD == ISD::MUL && MTy.getScalarType() == MVT::i8) {
    auto *WideSclTy = IntegerType::get(ValVTy->getContext(), 16);
    auto *WideVecTy = FixedVectorType::get(WideSclTy, ValVTy->getNumElements());
    return getCastInstrCost(Instruction::ZExt, WideVecTy, ValTy,
                            TargetTransformInfo::CastContextHint::None,
                            CostKind) +
           getArithmeticReductionCost(Opcode, WideVecTy, FMF, CostKind);
  }

  InstructionCost ArithmeticCost = 0;
  if (LT.first != 1 && MTy.isVector() &&
      MTy.getVectorNumElements() < ValVTy->getNumElements()) {
    // Type needs to be split. We need LT.first - 1 arithmetic ops.
    auto *SingleOpTy = FixedVectorType::get(ValVTy->getElementType(),
                                            MTy.getVectorNumElements());
    ArithmeticCost = getArithmeticInstrCost(Opcode, SingleOpTy, CostKind);
    ArithmeticCost *= LT.first - 1;
  }

  if (ST->useSLMArithCosts())
    if (const auto *Entry = CostTableLookup(SLMCostTbl, ISD, MTy))
      return ArithmeticCost + Entry->Cost;

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
      return ArithmeticCost + Entry->Cost;

  if (ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
      return ArithmeticCost + Entry->Cost;

  // FIXME: These assume a naive kshift+binop lowering, which is probably
  // conservative in most cases.
  static const CostTblEntry AVX512BoolReduction[] = {
    { ISD::AND,  MVT::v2i1,   3 },
    { ISD::AND,  MVT::v4i1,   5 },
    { ISD::AND,  MVT::v8i1,   7 },
    { ISD::AND,  MVT::v16i1,  9 },
    { ISD::AND,  MVT::v32i1, 11 },
    { ISD::AND,  MVT::v64i1, 13 },
    { ISD::OR,   MVT::v2i1,   3 },
    { ISD::OR,   MVT::v4i1,   5 },
    { ISD::OR,   MVT::v8i1,   7 },
    { ISD::OR,   MVT::v16i1,  9 },
    { ISD::OR,   MVT::v32i1, 11 },
    { ISD::OR,   MVT::v64i1, 13 },
  };

  static const CostTblEntry AVX2BoolReduction[] = {
    { ISD::AND,  MVT::v16i16,  2 }, // vpmovmskb + cmp
    { ISD::AND,  MVT::v32i8,   2 }, // vpmovmskb + cmp
    { ISD::OR,   MVT::v16i16,  2 }, // vpmovmskb + cmp
    { ISD::OR,   MVT::v32i8,   2 }, // vpmovmskb + cmp
  };

  static const CostTblEntry AVX1BoolReduction[] = {
    { ISD::AND,  MVT::v4i64,   2 }, // vmovmskpd + cmp
    { ISD::AND,  MVT::v8i32,   2 }, // vmovmskps + cmp
    { ISD::AND,  MVT::v16i16,  4 }, // vextractf128 + vpand + vpmovmskb + cmp
    { ISD::AND,  MVT::v32i8,   4 }, // vextractf128 + vpand + vpmovmskb + cmp
    { ISD::OR,   MVT::v4i64,   2 }, // vmovmskpd + cmp
    { ISD::OR,   MVT::v8i32,   2 }, // vmovmskps + cmp
    { ISD::OR,   MVT::v16i16,  4 }, // vextractf128 + vpor + vpmovmskb + cmp
    { ISD::OR,   MVT::v32i8,   4 }, // vextractf128 + vpor + vpmovmskb + cmp
  };

  static const CostTblEntry SSE2BoolReduction[] = {
    { ISD::AND,  MVT::v2i64,   2 }, // movmskpd + cmp
    { ISD::AND,  MVT::v4i32,   2 }, // movmskps + cmp
    { ISD::AND,  MVT::v8i16,   2 }, // pmovmskb + cmp
    { ISD::AND,  MVT::v16i8,   2 }, // pmovmskb + cmp
    { ISD::OR,   MVT::v2i64,   2 }, // movmskpd + cmp
    { ISD::OR,   MVT::v4i32,   2 }, // movmskps + cmp
    { ISD::OR,   MVT::v8i16,   2 }, // pmovmskb + cmp
    { ISD::OR,   MVT::v16i8,   2 }, // pmovmskb + cmp
  };

  // Handle bool allof/anyof patterns.
  if (ValVTy->getElementType()->isIntegerTy(1)) {
    InstructionCost ArithmeticCost = 0;
    if (LT.first != 1 && MTy.isVector() &&
        MTy.getVectorNumElements() < ValVTy->getNumElements()) {
      // Type needs to be split. We need LT.first - 1 arithmetic ops.
      auto *SingleOpTy = FixedVectorType::get(ValVTy->getElementType(),
                                              MTy.getVectorNumElements());
      ArithmeticCost = getArithmeticInstrCost(Opcode, SingleOpTy, CostKind);
      ArithmeticCost *= LT.first - 1;
    }

    if (ST->hasAVX512())
      if (const auto *Entry = CostTableLookup(AVX512BoolReduction, ISD, MTy))
        return ArithmeticCost + Entry->Cost;
    if (ST->hasAVX2())
      if (const auto *Entry = CostTableLookup(AVX2BoolReduction, ISD, MTy))
        return ArithmeticCost + Entry->Cost;
    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1BoolReduction, ISD, MTy))
        return ArithmeticCost + Entry->Cost;
    if (ST->hasSSE2())
      if (const auto *Entry = CostTableLookup(SSE2BoolReduction, ISD, MTy))
        return ArithmeticCost + Entry->Cost;

    return BaseT::getArithmeticReductionCost(Opcode, ValVTy, FMF, CostKind);
  }

  unsigned NumVecElts = ValVTy->getNumElements();
  unsigned ScalarSize = ValVTy->getScalarSizeInBits();

  // Special case power of 2 reductions where the scalar type isn't changed
  // by type legalization.
  if (!isPowerOf2_32(NumVecElts) || ScalarSize != MTy.getScalarSizeInBits())
    return BaseT::getArithmeticReductionCost(Opcode, ValVTy, FMF, CostKind);

  InstructionCost ReductionCost = 0;

  auto *Ty = ValVTy;
  if (LT.first != 1 && MTy.isVector() &&
      MTy.getVectorNumElements() < ValVTy->getNumElements()) {
    // Type needs to be split. We need LT.first - 1 arithmetic ops.
    Ty = FixedVectorType::get(ValVTy->getElementType(),
                              MTy.getVectorNumElements());
    ReductionCost = getArithmeticInstrCost(Opcode, Ty, CostKind);
    ReductionCost *= LT.first - 1;
    NumVecElts = MTy.getVectorNumElements();
  }

  // Now handle reduction with the legal type, taking into account size changes
  // at each level.
  while (NumVecElts > 1) {
    // Determine the size of the remaining vector we need to reduce.
    unsigned Size = NumVecElts * ScalarSize;
    NumVecElts /= 2;
    // If we're reducing from 256/512 bits, use an extract_subvector.
    if (Size > 128) {
      auto *SubTy = FixedVectorType::get(ValVTy->getElementType(), NumVecElts);
      ReductionCost +=
          getShuffleCost(TTI::SK_ExtractSubvector, Ty, std::nullopt, CostKind,
                         NumVecElts, SubTy);
      Ty = SubTy;
    } else if (Size == 128) {
      // Reducing from 128 bits is a permute of v2f64/v2i64.
      FixedVectorType *ShufTy;
      if (ValVTy->isFloatingPointTy())
        ShufTy =
            FixedVectorType::get(Type::getDoubleTy(ValVTy->getContext()), 2);
      else
        ShufTy =
            FixedVectorType::get(Type::getInt64Ty(ValVTy->getContext()), 2);
      ReductionCost += getShuffleCost(TTI::SK_PermuteSingleSrc, ShufTy,
                                      std::nullopt, CostKind, 0, nullptr);
    } else if (Size == 64) {
      // Reducing from 64 bits is a shuffle of v4f32/v4i32.
      FixedVectorType *ShufTy;
      if (ValVTy->isFloatingPointTy())
        ShufTy =
            FixedVectorType::get(Type::getFloatTy(ValVTy->getContext()), 4);
      else
        ShufTy =
            FixedVectorType::get(Type::getInt32Ty(ValVTy->getContext()), 4);
      ReductionCost += getShuffleCost(TTI::SK_PermuteSingleSrc, ShufTy,
                                      std::nullopt, CostKind, 0, nullptr);
    } else {
      // Reducing from smaller size is a shift by immediate.
      auto *ShiftTy = FixedVectorType::get(
          Type::getIntNTy(ValVTy->getContext(), Size), 128 / Size);
      ReductionCost += getArithmeticInstrCost(
          Instruction::LShr, ShiftTy, CostKind,
          {TargetTransformInfo::OK_AnyValue, TargetTransformInfo::OP_None},
          {TargetTransformInfo::OK_UniformConstantValue, TargetTransformInfo::OP_None});
    }

    // Add the arithmetic op for this level.
    ReductionCost += getArithmeticInstrCost(Opcode, Ty, CostKind);
  }

  // Add the final extract element to the cost.
  return ReductionCost + getVectorInstrCost(Instruction::ExtractElement, Ty,
                                            CostKind, 0, nullptr, nullptr);
}

InstructionCost X86TTIImpl::getMinMaxCost(Intrinsic::ID IID, Type *Ty,
                                          TTI::TargetCostKind CostKind,
                                          FastMathFlags FMF) {
  IntrinsicCostAttributes ICA(IID, Ty, {Ty, Ty}, FMF);
  return getIntrinsicInstrCost(ICA, CostKind);
}

InstructionCost
X86TTIImpl::getMinMaxReductionCost(Intrinsic::ID IID, VectorType *ValTy,
                                   FastMathFlags FMF,
                                   TTI::TargetCostKind CostKind) {
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);

  MVT MTy = LT.second;

  int ISD;
  if (ValTy->isIntOrIntVectorTy()) {
    ISD = (IID == Intrinsic::umin || IID == Intrinsic::umax) ? ISD::UMIN
                                                             : ISD::SMIN;
  } else {
    assert(ValTy->isFPOrFPVectorTy() &&
           "Expected float point or integer vector type.");
    ISD = (IID == Intrinsic::minnum || IID == Intrinsic::maxnum)
              ? ISD::FMINNUM
              : ISD::FMINIMUM;
  }

  // We use the Intel Architecture Code Analyzer(IACA) to measure the throughput
  // and make it as the cost.

  static const CostTblEntry SSE2CostTbl[] = {
      {ISD::UMIN, MVT::v2i16, 5}, // need pxors to use pminsw/pmaxsw
      {ISD::UMIN, MVT::v4i16, 7}, // need pxors to use pminsw/pmaxsw
      {ISD::UMIN, MVT::v8i16, 9}, // need pxors to use pminsw/pmaxsw
  };

  static const CostTblEntry SSE41CostTbl[] = {
      {ISD::SMIN, MVT::v2i16, 3}, // same as sse2
      {ISD::SMIN, MVT::v4i16, 5}, // same as sse2
      {ISD::UMIN, MVT::v2i16, 5}, // same as sse2
      {ISD::UMIN, MVT::v4i16, 7}, // same as sse2
      {ISD::SMIN, MVT::v8i16, 4}, // phminposuw+xor
      {ISD::UMIN, MVT::v8i16, 4}, // FIXME: umin is cheaper than umax
      {ISD::SMIN, MVT::v2i8,  3}, // pminsb
      {ISD::SMIN, MVT::v4i8,  5}, // pminsb
      {ISD::SMIN, MVT::v8i8,  7}, // pminsb
      {ISD::SMIN, MVT::v16i8, 6},
      {ISD::UMIN, MVT::v2i8,  3}, // same as sse2
      {ISD::UMIN, MVT::v4i8,  5}, // same as sse2
      {ISD::UMIN, MVT::v8i8,  7}, // same as sse2
      {ISD::UMIN, MVT::v16i8, 6}, // FIXME: umin is cheaper than umax
  };

  static const CostTblEntry AVX1CostTbl[] = {
      {ISD::SMIN, MVT::v16i16, 6},
      {ISD::UMIN, MVT::v16i16, 6}, // FIXME: umin is cheaper than umax
      {ISD::SMIN, MVT::v32i8, 8},
      {ISD::UMIN, MVT::v32i8, 8},
  };

  static const CostTblEntry AVX512BWCostTbl[] = {
      {ISD::SMIN, MVT::v32i16, 8},
      {ISD::UMIN, MVT::v32i16, 8}, // FIXME: umin is cheaper than umax
      {ISD::SMIN, MVT::v64i8, 10},
      {ISD::UMIN, MVT::v64i8, 10},
  };

  // Before legalizing the type, give a chance to look up illegal narrow types
  // in the table.
  // FIXME: Is there a better way to do this?
  EVT VT = TLI->getValueType(DL, ValTy);
  if (VT.isSimple()) {
    MVT MTy = VT.getSimpleVT();
    if (ST->hasBWI())
      if (const auto *Entry = CostTableLookup(AVX512BWCostTbl, ISD, MTy))
        return Entry->Cost;

    if (ST->hasAVX())
      if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
        return Entry->Cost;

    if (ST->hasSSE41())
      if (const auto *Entry = CostTableLookup(SSE41CostTbl, ISD, MTy))
        return Entry->Cost;

    if (ST->hasSSE2())
      if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
        return Entry->Cost;
  }

  auto *ValVTy = cast<FixedVectorType>(ValTy);
  unsigned NumVecElts = ValVTy->getNumElements();

  auto *Ty = ValVTy;
  InstructionCost MinMaxCost = 0;
  if (LT.first != 1 && MTy.isVector() &&
      MTy.getVectorNumElements() < ValVTy->getNumElements()) {
    // Type needs to be split. We need LT.first - 1 operations ops.
    Ty = FixedVectorType::get(ValVTy->getElementType(),
                              MTy.getVectorNumElements());
    MinMaxCost = getMinMaxCost(IID, Ty, CostKind, FMF);
    MinMaxCost *= LT.first - 1;
    NumVecElts = MTy.getVectorNumElements();
  }

  if (ST->hasBWI())
    if (const auto *Entry = CostTableLookup(AVX512BWCostTbl, ISD, MTy))
      return MinMaxCost + Entry->Cost;

  if (ST->hasAVX())
    if (const auto *Entry = CostTableLookup(AVX1CostTbl, ISD, MTy))
      return MinMaxCost + Entry->Cost;

  if (ST->hasSSE41())
    if (const auto *Entry = CostTableLookup(SSE41CostTbl, ISD, MTy))
      return MinMaxCost + Entry->Cost;

  if (ST->hasSSE2())
    if (const auto *Entry = CostTableLookup(SSE2CostTbl, ISD, MTy))
      return MinMaxCost + Entry->Cost;

  unsigned ScalarSize = ValTy->getScalarSizeInBits();

  // Special case power of 2 reductions where the scalar type isn't changed
  // by type legalization.
  if (!isPowerOf2_32(ValVTy->getNumElements()) ||
      ScalarSize != MTy.getScalarSizeInBits())
    return BaseT::getMinMaxReductionCost(IID, ValTy, FMF, CostKind);

  // Now handle reduction with the legal type, taking into account size changes
  // at each level.
  while (NumVecElts > 1) {
    // Determine the size of the remaining vector we need to reduce.
    unsigned Size = NumVecElts * ScalarSize;
    NumVecElts /= 2;
    // If we're reducing from 256/512 bits, use an extract_subvector.
    if (Size > 128) {
      auto *SubTy = FixedVectorType::get(ValVTy->getElementType(), NumVecElts);
      MinMaxCost += getShuffleCost(TTI::SK_ExtractSubvector, Ty, std::nullopt,
                                   CostKind, NumVecElts, SubTy);
      Ty = SubTy;
    } else if (Size == 128) {
      // Reducing from 128 bits is a permute of v2f64/v2i64.
      VectorType *ShufTy;
      if (ValTy->isFloatingPointTy())
        ShufTy =
            FixedVectorType::get(Type::getDoubleTy(ValTy->getContext()), 2);
      else
        ShufTy = FixedVectorType::get(Type::getInt64Ty(ValTy->getContext()), 2);
      MinMaxCost += getShuffleCost(TTI::SK_PermuteSingleSrc, ShufTy,
                                   std::nullopt, CostKind, 0, nullptr);
    } else if (Size == 64) {
      // Reducing from 64 bits is a shuffle of v4f32/v4i32.
      FixedVectorType *ShufTy;
      if (ValTy->isFloatingPointTy())
        ShufTy = FixedVectorType::get(Type::getFloatTy(ValTy->getContext()), 4);
      else
        ShufTy = FixedVectorType::get(Type::getInt32Ty(ValTy->getContext()), 4);
      MinMaxCost += getShuffleCost(TTI::SK_PermuteSingleSrc, ShufTy,
                                   std::nullopt, CostKind, 0, nullptr);
    } else {
      // Reducing from smaller size is a shift by immediate.
      auto *ShiftTy = FixedVectorType::get(
          Type::getIntNTy(ValTy->getContext(), Size), 128 / Size);
      MinMaxCost += getArithmeticInstrCost(
          Instruction::LShr, ShiftTy, TTI::TCK_RecipThroughput,
          {TargetTransformInfo::OK_AnyValue, TargetTransformInfo::OP_None},
          {TargetTransformInfo::OK_UniformConstantValue, TargetTransformInfo::OP_None});
    }

    // Add the arithmetic op for this level.
    MinMaxCost += getMinMaxCost(IID, Ty, CostKind, FMF);
  }

  // Add the final extract element to the cost.
  return MinMaxCost + getVectorInstrCost(Instruction::ExtractElement, Ty,
                                         CostKind, 0, nullptr, nullptr);
}

/// Calculate the cost of materializing a 64-bit value. This helper
/// method might only calculate a fraction of a larger immediate. Therefore it
/// is valid to return a cost of ZERO.
InstructionCost X86TTIImpl::getIntImmCost(int64_t Val) {
  if (Val == 0)
    return TTI::TCC_Free;

  if (isInt<32>(Val))
    return TTI::TCC_Basic;

  return 2 * TTI::TCC_Basic;
}

InstructionCost X86TTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                                          TTI::TargetCostKind CostKind) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  // Never hoist constants larger than 128bit, because this might lead to
  // incorrect code generation or assertions in codegen.
  // Fixme: Create a cost model for types larger than i128 once the codegen
  // issues have been fixed.
  if (BitSize > 128)
    return TTI::TCC_Free;

  if (Imm == 0)
    return TTI::TCC_Free;

  // Sign-extend all constants to a multiple of 64-bit.
  APInt ImmVal = Imm;
  if (BitSize % 64 != 0)
    ImmVal = Imm.sext(alignTo(BitSize, 64));

  // Split the constant into 64-bit chunks and calculate the cost for each
  // chunk.
  InstructionCost Cost = 0;
  for (unsigned ShiftVal = 0; ShiftVal < BitSize; ShiftVal += 64) {
    APInt Tmp = ImmVal.ashr(ShiftVal).sextOrTrunc(64);
    int64_t Val = Tmp.getSExtValue();
    Cost += getIntImmCost(Val);
  }
  // We need at least one instruction to materialize the constant.
  return std::max<InstructionCost>(1, Cost);
}

InstructionCost X86TTIImpl::getIntImmCostInst(unsigned Opcode, unsigned Idx,
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
    // Always hoist the base address of a GetElementPtr. This prevents the
    // creation of new constants for every base constant that gets constant
    // folded with the offset.
    if (Idx == 0)
      return 2 * TTI::TCC_Basic;
    return TTI::TCC_Free;
  case Instruction::Store:
    ImmIdx = 0;
    break;
  case Instruction::ICmp:
    // This is an imperfect hack to prevent constant hoisting of
    // compares that might be trying to check if a 64-bit value fits in
    // 32-bits. The backend can optimize these cases using a right shift by 32.
    // Ideally we would check the compare predicate here. There also other
    // similar immediates the backend can use shifts for.
    if (Idx == 1 && Imm.getBitWidth() == 64) {
      uint64_t ImmVal = Imm.getZExtValue();
      if (ImmVal == 0x100000000ULL || ImmVal == 0xffffffff)
        return TTI::TCC_Free;
    }
    ImmIdx = 1;
    break;
  case Instruction::And:
    // We support 64-bit ANDs with immediates with 32-bits of leading zeroes
    // by using a 32-bit operation with implicit zero extension. Detect such
    // immediates here as the normal path expects bit 31 to be sign extended.
    if (Idx == 1 && Imm.getBitWidth() == 64 && Imm.isIntN(32))
      return TTI::TCC_Free;
    ImmIdx = 1;
    break;
  case Instruction::Add:
  case Instruction::Sub:
    // For add/sub, we can use the opposite instruction for INT32_MIN.
    if (Idx == 1 && Imm.getBitWidth() == 64 && Imm.getZExtValue() == 0x80000000)
      return TTI::TCC_Free;
    ImmIdx = 1;
    break;
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    // Division by constant is typically expanded later into a different
    // instruction sequence. This completely changes the constants.
    // Report them as "free" to stop ConstantHoist from marking them as opaque.
    return TTI::TCC_Free;
  case Instruction::Mul:
  case Instruction::Or:
  case Instruction::Xor:
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
    uint64_t NumConstants = divideCeil(BitSize, 64);
    InstructionCost Cost = X86TTIImpl::getIntImmCost(Imm, Ty, CostKind);
    return (Cost <= NumConstants * TTI::TCC_Basic)
               ? static_cast<int>(TTI::TCC_Free)
               : Cost;
  }

  return X86TTIImpl::getIntImmCost(Imm, Ty, CostKind);
}

InstructionCost X86TTIImpl::getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                                const APInt &Imm, Type *Ty,
                                                TTI::TargetCostKind CostKind) {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // There is no cost model for constants with a bit size of 0. Return TCC_Free
  // here, so that constant hoisting will ignore this constant.
  if (BitSize == 0)
    return TTI::TCC_Free;

  switch (IID) {
  default:
    return TTI::TCC_Free;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::smul_with_overflow:
  case Intrinsic::umul_with_overflow:
    if ((Idx == 1) && Imm.getBitWidth() <= 64 && Imm.isSignedIntN(32))
      return TTI::TCC_Free;
    break;
  case Intrinsic::experimental_stackmap:
    if ((Idx < 2) || (Imm.getBitWidth() <= 64 && Imm.isSignedIntN(64)))
      return TTI::TCC_Free;
    break;
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint:
    if ((Idx < 4) || (Imm.getBitWidth() <= 64 && Imm.isSignedIntN(64)))
      return TTI::TCC_Free;
    break;
  }
  return X86TTIImpl::getIntImmCost(Imm, Ty, CostKind);
}

InstructionCost X86TTIImpl::getCFInstrCost(unsigned Opcode,
                                           TTI::TargetCostKind CostKind,
                                           const Instruction *I) {
  if (CostKind != TTI::TCK_RecipThroughput)
    return Opcode == Instruction::PHI ? 0 : 1;
  // Branches are assumed to be predicted.
  return 0;
}

int X86TTIImpl::getGatherOverhead() const {
  // Some CPUs have more overhead for gather. The specified overhead is relative
  // to the Load operation. "2" is the number provided by Intel architects. This
  // parameter is used for cost estimation of Gather Op and comparison with
  // other alternatives.
  // TODO: Remove the explicit hasAVX512()?, That would mean we would only
  // enable gather with a -march.
  if (ST->hasAVX512() || (ST->hasAVX2() && ST->hasFastGather()))
    return 2;

  return 1024;
}

int X86TTIImpl::getScatterOverhead() const {
  if (ST->hasAVX512())
    return 2;

  return 1024;
}

// Return an average cost of Gather / Scatter instruction, maybe improved later.
InstructionCost X86TTIImpl::getGSVectorCost(unsigned Opcode,
                                            TTI::TargetCostKind CostKind,
                                            Type *SrcVTy, const Value *Ptr,
                                            Align Alignment,
                                            unsigned AddressSpace) {

  assert(isa<VectorType>(SrcVTy) && "Unexpected type in getGSVectorCost");
  unsigned VF = cast<FixedVectorType>(SrcVTy)->getNumElements();

  // Try to reduce index size from 64 bit (default for GEP)
  // to 32. It is essential for VF 16. If the index can't be reduced to 32, the
  // operation will use 16 x 64 indices which do not fit in a zmm and needs
  // to split. Also check that the base pointer is the same for all lanes,
  // and that there's at most one variable index.
  auto getIndexSizeInBits = [](const Value *Ptr, const DataLayout &DL) {
    unsigned IndexSize = DL.getPointerSizeInBits();
    const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
    if (IndexSize < 64 || !GEP)
      return IndexSize;

    unsigned NumOfVarIndices = 0;
    const Value *Ptrs = GEP->getPointerOperand();
    if (Ptrs->getType()->isVectorTy() && !getSplatValue(Ptrs))
      return IndexSize;
    for (unsigned I = 1, E = GEP->getNumOperands(); I != E; ++I) {
      if (isa<Constant>(GEP->getOperand(I)))
        continue;
      Type *IndxTy = GEP->getOperand(I)->getType();
      if (auto *IndexVTy = dyn_cast<VectorType>(IndxTy))
        IndxTy = IndexVTy->getElementType();
      if ((IndxTy->getPrimitiveSizeInBits() == 64 &&
           !isa<SExtInst>(GEP->getOperand(I))) ||
          ++NumOfVarIndices > 1)
        return IndexSize; // 64
    }
    return (unsigned)32;
  };

  // Trying to reduce IndexSize to 32 bits for vector 16.
  // By default the IndexSize is equal to pointer size.
  unsigned IndexSize = (ST->hasAVX512() && VF >= 16)
                           ? getIndexSizeInBits(Ptr, DL)
                           : DL.getPointerSizeInBits();

  auto *IndexVTy = FixedVectorType::get(
      IntegerType::get(SrcVTy->getContext(), IndexSize), VF);
  std::pair<InstructionCost, MVT> IdxsLT = getTypeLegalizationCost(IndexVTy);
  std::pair<InstructionCost, MVT> SrcLT = getTypeLegalizationCost(SrcVTy);
  InstructionCost::CostType SplitFactor =
      *std::max(IdxsLT.first, SrcLT.first).getValue();
  if (SplitFactor > 1) {
    // Handle splitting of vector of pointers
    auto *SplitSrcTy =
        FixedVectorType::get(SrcVTy->getScalarType(), VF / SplitFactor);
    return SplitFactor * getGSVectorCost(Opcode, CostKind, SplitSrcTy, Ptr,
                                         Alignment, AddressSpace);
  }

  // If we didn't split, this will be a single gather/scatter instruction.
  if (CostKind == TTI::TCK_CodeSize)
    return 1;

  // The gather / scatter cost is given by Intel architects. It is a rough
  // number since we are looking at one instruction in a time.
  const int GSOverhead = (Opcode == Instruction::Load) ? getGatherOverhead()
                                                       : getScatterOverhead();
  return GSOverhead + VF * getMemoryOpCost(Opcode, SrcVTy->getScalarType(),
                                           MaybeAlign(Alignment), AddressSpace,
                                           CostKind);
}

/// Calculate the cost of Gather / Scatter operation
InstructionCost X86TTIImpl::getGatherScatterOpCost(
    unsigned Opcode, Type *SrcVTy, const Value *Ptr, bool VariableMask,
    Align Alignment, TTI::TargetCostKind CostKind,
    const Instruction *I = nullptr) {
  if ((Opcode == Instruction::Load &&
       (!isLegalMaskedGather(SrcVTy, Align(Alignment)) ||
        forceScalarizeMaskedGather(cast<VectorType>(SrcVTy),
                                   Align(Alignment)))) ||
      (Opcode == Instruction::Store &&
       (!isLegalMaskedScatter(SrcVTy, Align(Alignment)) ||
        forceScalarizeMaskedScatter(cast<VectorType>(SrcVTy),
                                    Align(Alignment)))))
    return BaseT::getGatherScatterOpCost(Opcode, SrcVTy, Ptr, VariableMask,
                                         Alignment, CostKind, I);

  assert(SrcVTy->isVectorTy() && "Unexpected data type for Gather/Scatter");
  PointerType *PtrTy = dyn_cast<PointerType>(Ptr->getType());
  if (!PtrTy && Ptr->getType()->isVectorTy())
    PtrTy = dyn_cast<PointerType>(
        cast<VectorType>(Ptr->getType())->getElementType());
  assert(PtrTy && "Unexpected type for Ptr argument");
  unsigned AddressSpace = PtrTy->getAddressSpace();
  return getGSVectorCost(Opcode, CostKind, SrcVTy, Ptr, Alignment,
                         AddressSpace);
}

bool X86TTIImpl::isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                               const TargetTransformInfo::LSRCost &C2) {
    // X86 specific here are "instruction number 1st priority".
    return std::tie(C1.Insns, C1.NumRegs, C1.AddRecCost,
                    C1.NumIVMuls, C1.NumBaseAdds,
                    C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
           std::tie(C2.Insns, C2.NumRegs, C2.AddRecCost,
                    C2.NumIVMuls, C2.NumBaseAdds,
                    C2.ScaleCost, C2.ImmCost, C2.SetupCost);
}

bool X86TTIImpl::canMacroFuseCmp() {
  return ST->hasMacroFusion() || ST->hasBranchFusion();
}

bool X86TTIImpl::isLegalMaskedLoad(Type *DataTy, Align Alignment) {
  Type *ScalarTy = DataTy->getScalarType();

  // The backend can't handle a single element vector w/o CFCMOV.
  if (isa<VectorType>(DataTy) && cast<FixedVectorType>(DataTy)->getNumElements() == 1)
    return ST->hasCF() && hasConditionalLoadStoreForType(ScalarTy);

  if (!ST->hasAVX())
    return false;

  if (ScalarTy->isPointerTy())
    return true;

  if (ScalarTy->isFloatTy() || ScalarTy->isDoubleTy())
    return true;

  if (ScalarTy->isHalfTy() && ST->hasBWI())
    return true;

  if (ScalarTy->isBFloatTy() && ST->hasBF16())
    return true;

  if (!ScalarTy->isIntegerTy())
    return false;

  unsigned IntWidth = ScalarTy->getIntegerBitWidth();
  return IntWidth == 32 || IntWidth == 64 ||
         ((IntWidth == 8 || IntWidth == 16) && ST->hasBWI());
}

bool X86TTIImpl::isLegalMaskedStore(Type *DataType, Align Alignment) {
  return isLegalMaskedLoad(DataType, Alignment);
}

bool X86TTIImpl::isLegalNTLoad(Type *DataType, Align Alignment) {
  unsigned DataSize = DL.getTypeStoreSize(DataType);
  // The only supported nontemporal loads are for aligned vectors of 16 or 32
  // bytes.  Note that 32-byte nontemporal vector loads are supported by AVX2
  // (the equivalent stores only require AVX).
  if (Alignment >= DataSize && (DataSize == 16 || DataSize == 32))
    return DataSize == 16 ?  ST->hasSSE1() : ST->hasAVX2();

  return false;
}

bool X86TTIImpl::isLegalNTStore(Type *DataType, Align Alignment) {
  unsigned DataSize = DL.getTypeStoreSize(DataType);

  // SSE4A supports nontemporal stores of float and double at arbitrary
  // alignment.
  if (ST->hasSSE4A() && (DataType->isFloatTy() || DataType->isDoubleTy()))
    return true;

  // Besides the SSE4A subtarget exception above, only aligned stores are
  // available nontemporaly on any other subtarget.  And only stores with a size
  // of 4..32 bytes (powers of 2, only) are permitted.
  if (Alignment < DataSize || DataSize < 4 || DataSize > 32 ||
      !isPowerOf2_32(DataSize))
    return false;

  // 32-byte vector nontemporal stores are supported by AVX (the equivalent
  // loads require AVX2).
  if (DataSize == 32)
    return ST->hasAVX();
  if (DataSize == 16)
    return ST->hasSSE1();
  return true;
}

bool X86TTIImpl::isLegalBroadcastLoad(Type *ElementTy,
                                      ElementCount NumElements) const {
  // movddup
  return ST->hasSSE3() && !NumElements.isScalable() &&
         NumElements.getFixedValue() == 2 &&
         ElementTy == Type::getDoubleTy(ElementTy->getContext());
}

bool X86TTIImpl::isLegalMaskedExpandLoad(Type *DataTy, Align Alignment) {
  if (!isa<VectorType>(DataTy))
    return false;

  if (!ST->hasAVX512())
    return false;

  // The backend can't handle a single element vector.
  if (cast<FixedVectorType>(DataTy)->getNumElements() == 1)
    return false;

  Type *ScalarTy = cast<VectorType>(DataTy)->getElementType();

  if (ScalarTy->isFloatTy() || ScalarTy->isDoubleTy())
    return true;

  if (!ScalarTy->isIntegerTy())
    return false;

  unsigned IntWidth = ScalarTy->getIntegerBitWidth();
  return IntWidth == 32 || IntWidth == 64 ||
         ((IntWidth == 8 || IntWidth == 16) && ST->hasVBMI2());
}

bool X86TTIImpl::isLegalMaskedCompressStore(Type *DataTy, Align Alignment) {
  return isLegalMaskedExpandLoad(DataTy, Alignment);
}

bool X86TTIImpl::supportsGather() const {
  // Some CPUs have better gather performance than others.
  // TODO: Remove the explicit ST->hasAVX512()?, That would mean we would only
  // enable gather with a -march.
  return ST->hasAVX512() || (ST->hasFastGather() && ST->hasAVX2());
}

bool X86TTIImpl::forceScalarizeMaskedGather(VectorType *VTy, Align Alignment) {
  // Gather / Scatter for vector 2 is not profitable on KNL / SKX
  // Vector-4 of gather/scatter instruction does not exist on KNL. We can extend
  // it to 8 elements, but zeroing upper bits of the mask vector will add more
  // instructions. Right now we give the scalar cost of vector-4 for KNL. TODO:
  // Check, maybe the gather/scatter instruction is better in the VariableMask
  // case.
  unsigned NumElts = cast<FixedVectorType>(VTy)->getNumElements();
  return NumElts == 1 ||
         (ST->hasAVX512() && (NumElts == 2 || (NumElts == 4 && !ST->hasVLX())));
}

bool X86TTIImpl::isLegalMaskedGatherScatter(Type *DataTy, Align Alignment) {
  Type *ScalarTy = DataTy->getScalarType();
  if (ScalarTy->isPointerTy())
    return true;

  if (ScalarTy->isFloatTy() || ScalarTy->isDoubleTy())
    return true;

  if (!ScalarTy->isIntegerTy())
    return false;

  unsigned IntWidth = ScalarTy->getIntegerBitWidth();
  return IntWidth == 32 || IntWidth == 64;
}

bool X86TTIImpl::isLegalMaskedGather(Type *DataTy, Align Alignment) {
  if (!supportsGather() || !ST->preferGather())
    return false;
  return isLegalMaskedGatherScatter(DataTy, Alignment);
}

bool X86TTIImpl::isLegalAltInstr(VectorType *VecTy, unsigned Opcode0,
                                 unsigned Opcode1,
                                 const SmallBitVector &OpcodeMask) const {
  // ADDSUBPS  4xf32 SSE3
  // VADDSUBPS 4xf32 AVX
  // VADDSUBPS 8xf32 AVX2
  // ADDSUBPD  2xf64 SSE3
  // VADDSUBPD 2xf64 AVX
  // VADDSUBPD 4xf64 AVX2

  unsigned NumElements = cast<FixedVectorType>(VecTy)->getNumElements();
  assert(OpcodeMask.size() == NumElements && "Mask and VecTy are incompatible");
  if (!isPowerOf2_32(NumElements))
    return false;
  // Check the opcode pattern. We apply the mask on the opcode arguments and
  // then check if it is what we expect.
  for (int Lane : seq<int>(0, NumElements)) {
    unsigned Opc = OpcodeMask.test(Lane) ? Opcode1 : Opcode0;
    // We expect FSub for even lanes and FAdd for odd lanes.
    if (Lane % 2 == 0 && Opc != Instruction::FSub)
      return false;
    if (Lane % 2 == 1 && Opc != Instruction::FAdd)
      return false;
  }
  // Now check that the pattern is supported by the target ISA.
  Type *ElemTy = cast<VectorType>(VecTy)->getElementType();
  if (ElemTy->isFloatTy())
    return ST->hasSSE3() && NumElements % 4 == 0;
  if (ElemTy->isDoubleTy())
    return ST->hasSSE3() && NumElements % 2 == 0;
  return false;
}

bool X86TTIImpl::isLegalMaskedScatter(Type *DataType, Align Alignment) {
  // AVX2 doesn't support scatter
  if (!ST->hasAVX512() || !ST->preferScatter())
    return false;
  return isLegalMaskedGatherScatter(DataType, Alignment);
}

bool X86TTIImpl::hasDivRemOp(Type *DataType, bool IsSigned) {
  EVT VT = TLI->getValueType(DL, DataType);
  return TLI->isOperationLegal(IsSigned ? ISD::SDIVREM : ISD::UDIVREM, VT);
}

bool X86TTIImpl::isExpensiveToSpeculativelyExecute(const Instruction* I) {
  // FDIV is always expensive, even if it has a very low uop count.
  // TODO: Still necessary for recent CPUs with low latency/throughput fdiv?
  if (I->getOpcode() == Instruction::FDiv)
    return true;

  return BaseT::isExpensiveToSpeculativelyExecute(I);
}

bool X86TTIImpl::isFCmpOrdCheaperThanFCmpZero(Type *Ty) {
  return false;
}

bool X86TTIImpl::areInlineCompatible(const Function *Caller,
                                     const Function *Callee) const {
  const TargetMachine &TM = getTLI()->getTargetMachine();

  // Work this as a subsetting of subtarget features.
  const FeatureBitset &CallerBits =
      TM.getSubtargetImpl(*Caller)->getFeatureBits();
  const FeatureBitset &CalleeBits =
      TM.getSubtargetImpl(*Callee)->getFeatureBits();

  // Check whether features are the same (apart from the ignore list).
  FeatureBitset RealCallerBits = CallerBits & ~InlineFeatureIgnoreList;
  FeatureBitset RealCalleeBits = CalleeBits & ~InlineFeatureIgnoreList;
  if (RealCallerBits == RealCalleeBits)
    return true;

  // If the features are a subset, we need to additionally check for calls
  // that may become ABI-incompatible as a result of inlining.
  if ((RealCallerBits & RealCalleeBits) != RealCalleeBits)
    return false;

  for (const Instruction &I : instructions(Callee)) {
    if (const auto *CB = dyn_cast<CallBase>(&I)) {
      // Having more target features is fine for inline ASM.
      if (CB->isInlineAsm())
        continue;

      SmallVector<Type *, 8> Types;
      for (Value *Arg : CB->args())
        Types.push_back(Arg->getType());
      if (!CB->getType()->isVoidTy())
        Types.push_back(CB->getType());

      // Simple types are always ABI compatible.
      auto IsSimpleTy = [](Type *Ty) {
        return !Ty->isVectorTy() && !Ty->isAggregateType();
      };
      if (all_of(Types, IsSimpleTy))
        continue;

      if (Function *NestedCallee = CB->getCalledFunction()) {
        // Assume that intrinsics are always ABI compatible.
        if (NestedCallee->isIntrinsic())
          continue;

        // Do a precise compatibility check.
        if (!areTypesABICompatible(Caller, NestedCallee, Types))
          return false;
      } else {
        // We don't know the target features of the callee,
        // assume it is incompatible.
        return false;
      }
    }
  }
  return true;
}

bool X86TTIImpl::areTypesABICompatible(const Function *Caller,
                                       const Function *Callee,
                                       const ArrayRef<Type *> &Types) const {
  if (!BaseT::areTypesABICompatible(Caller, Callee, Types))
    return false;

  // If we get here, we know the target features match. If one function
  // considers 512-bit vectors legal and the other does not, consider them
  // incompatible.
  const TargetMachine &TM = getTLI()->getTargetMachine();

  if (TM.getSubtarget<X86Subtarget>(*Caller).useAVX512Regs() ==
      TM.getSubtarget<X86Subtarget>(*Callee).useAVX512Regs())
    return true;

  // Consider the arguments compatible if they aren't vectors or aggregates.
  // FIXME: Look at the size of vectors.
  // FIXME: Look at the element types of aggregates to see if there are vectors.
  return llvm::none_of(Types,
      [](Type *T) { return T->isVectorTy() || T->isAggregateType(); });
}

X86TTIImpl::TTI::MemCmpExpansionOptions
X86TTIImpl::enableMemCmpExpansion(bool OptSize, bool IsZeroCmp) const {
  TTI::MemCmpExpansionOptions Options;
  Options.MaxNumLoads = TLI->getMaxExpandSizeMemcmp(OptSize);
  Options.NumLoadsPerBlock = 2;
  // All GPR and vector loads can be unaligned.
  Options.AllowOverlappingLoads = true;
  if (IsZeroCmp) {
    // Only enable vector loads for equality comparison. Right now the vector
    // version is not as fast for three way compare (see #33329).
    const unsigned PreferredWidth = ST->getPreferVectorWidth();
    if (PreferredWidth >= 512 && ST->hasAVX512() && ST->hasEVEX512())
      Options.LoadSizes.push_back(64);
    if (PreferredWidth >= 256 && ST->hasAVX()) Options.LoadSizes.push_back(32);
    if (PreferredWidth >= 128 && ST->hasSSE2()) Options.LoadSizes.push_back(16);
  }
  if (ST->is64Bit()) {
    Options.LoadSizes.push_back(8);
  }
  Options.LoadSizes.push_back(4);
  Options.LoadSizes.push_back(2);
  Options.LoadSizes.push_back(1);
  return Options;
}

bool X86TTIImpl::prefersVectorizedAddressing() const {
  return supportsGather();
}

bool X86TTIImpl::supportsEfficientVectorElementLoadStore() const {
  return false;
}

bool X86TTIImpl::enableInterleavedAccessVectorization() {
  // TODO: We expect this to be beneficial regardless of arch,
  // but there are currently some unexplained performance artifacts on Atom.
  // As a temporary solution, disable on Atom.
  return !(ST->isAtom());
}

// Get estimation for interleaved load/store operations and strided load.
// \p Indices contains indices for strided load.
// \p Factor - the factor of interleaving.
// AVX-512 provides 3-src shuffles that significantly reduces the cost.
InstructionCost X86TTIImpl::getInterleavedMemoryOpCostAVX512(
    unsigned Opcode, FixedVectorType *VecTy, unsigned Factor,
    ArrayRef<unsigned> Indices, Align Alignment, unsigned AddressSpace,
    TTI::TargetCostKind CostKind, bool UseMaskForCond, bool UseMaskForGaps) {
  // VecTy for interleave memop is <VF*Factor x Elt>.
  // So, for VF=4, Interleave Factor = 3, Element type = i32 we have
  // VecTy = <12 x i32>.

  // Calculate the number of memory operations (NumOfMemOps), required
  // for load/store the VecTy.
  MVT LegalVT = getTypeLegalizationCost(VecTy).second;
  unsigned VecTySize = DL.getTypeStoreSize(VecTy);
  unsigned LegalVTSize = LegalVT.getStoreSize();
  unsigned NumOfMemOps = (VecTySize + LegalVTSize - 1) / LegalVTSize;

  // Get the cost of one memory operation.
  auto *SingleMemOpTy = FixedVectorType::get(VecTy->getElementType(),
                                             LegalVT.getVectorNumElements());
  InstructionCost MemOpCost;
  bool UseMaskedMemOp = UseMaskForCond || UseMaskForGaps;
  if (UseMaskedMemOp)
    MemOpCost = getMaskedMemoryOpCost(Opcode, SingleMemOpTy, Alignment,
                                      AddressSpace, CostKind);
  else
    MemOpCost = getMemoryOpCost(Opcode, SingleMemOpTy, MaybeAlign(Alignment),
                                AddressSpace, CostKind);

  unsigned VF = VecTy->getNumElements() / Factor;
  MVT VT =
      MVT::getVectorVT(TLI->getSimpleValueType(DL, VecTy->getScalarType()), VF);

  InstructionCost MaskCost;
  if (UseMaskedMemOp) {
    APInt DemandedLoadStoreElts = APInt::getZero(VecTy->getNumElements());
    for (unsigned Index : Indices) {
      assert(Index < Factor && "Invalid index for interleaved memory op");
      for (unsigned Elm = 0; Elm < VF; Elm++)
        DemandedLoadStoreElts.setBit(Index + Elm * Factor);
    }

    Type *I1Type = Type::getInt1Ty(VecTy->getContext());

    MaskCost = getReplicationShuffleCost(
        I1Type, Factor, VF,
        UseMaskForGaps ? DemandedLoadStoreElts
                       : APInt::getAllOnes(VecTy->getNumElements()),
        CostKind);

    // The Gaps mask is invariant and created outside the loop, therefore the
    // cost of creating it is not accounted for here. However if we have both
    // a MaskForGaps and some other mask that guards the execution of the
    // memory access, we need to account for the cost of And-ing the two masks
    // inside the loop.
    if (UseMaskForGaps) {
      auto *MaskVT = FixedVectorType::get(I1Type, VecTy->getNumElements());
      MaskCost += getArithmeticInstrCost(BinaryOperator::And, MaskVT, CostKind);
    }
  }

  if (Opcode == Instruction::Load) {
    // The tables (AVX512InterleavedLoadTbl and AVX512InterleavedStoreTbl)
    // contain the cost of the optimized shuffle sequence that the
    // X86InterleavedAccess pass will generate.
    // The cost of loads and stores are computed separately from the table.

    // X86InterleavedAccess support only the following interleaved-access group.
    static const CostTblEntry AVX512InterleavedLoadTbl[] = {
        {3, MVT::v16i8, 12}, //(load 48i8 and) deinterleave into 3 x 16i8
        {3, MVT::v32i8, 14}, //(load 96i8 and) deinterleave into 3 x 32i8
        {3, MVT::v64i8, 22}, //(load 96i8 and) deinterleave into 3 x 32i8
    };

    if (const auto *Entry =
            CostTableLookup(AVX512InterleavedLoadTbl, Factor, VT))
      return MaskCost + NumOfMemOps * MemOpCost + Entry->Cost;
    //If an entry does not exist, fallback to the default implementation.

    // Kind of shuffle depends on number of loaded values.
    // If we load the entire data in one register, we can use a 1-src shuffle.
    // Otherwise, we'll merge 2 sources in each operation.
    TTI::ShuffleKind ShuffleKind =
        (NumOfMemOps > 1) ? TTI::SK_PermuteTwoSrc : TTI::SK_PermuteSingleSrc;

    InstructionCost ShuffleCost = getShuffleCost(
        ShuffleKind, SingleMemOpTy, std::nullopt, CostKind, 0, nullptr);

    unsigned NumOfLoadsInInterleaveGrp =
        Indices.size() ? Indices.size() : Factor;
    auto *ResultTy = FixedVectorType::get(VecTy->getElementType(),
                                          VecTy->getNumElements() / Factor);
    InstructionCost NumOfResults =
        getTypeLegalizationCost(ResultTy).first * NumOfLoadsInInterleaveGrp;

    // About a half of the loads may be folded in shuffles when we have only
    // one result. If we have more than one result, or the loads are masked,
    // we do not fold loads at all.
    unsigned NumOfUnfoldedLoads =
        UseMaskedMemOp || NumOfResults > 1 ? NumOfMemOps : NumOfMemOps / 2;

    // Get a number of shuffle operations per result.
    unsigned NumOfShufflesPerResult =
        std::max((unsigned)1, (unsigned)(NumOfMemOps - 1));

    // The SK_MergeTwoSrc shuffle clobbers one of src operands.
    // When we have more than one destination, we need additional instructions
    // to keep sources.
    InstructionCost NumOfMoves = 0;
    if (NumOfResults > 1 && ShuffleKind == TTI::SK_PermuteTwoSrc)
      NumOfMoves = NumOfResults * NumOfShufflesPerResult / 2;

    InstructionCost Cost = NumOfResults * NumOfShufflesPerResult * ShuffleCost +
                           MaskCost + NumOfUnfoldedLoads * MemOpCost +
                           NumOfMoves;

    return Cost;
  }

  // Store.
  assert(Opcode == Instruction::Store &&
         "Expected Store Instruction at this  point");
  // X86InterleavedAccess support only the following interleaved-access group.
  static const CostTblEntry AVX512InterleavedStoreTbl[] = {
      {3, MVT::v16i8, 12}, // interleave 3 x 16i8 into 48i8 (and store)
      {3, MVT::v32i8, 14}, // interleave 3 x 32i8 into 96i8 (and store)
      {3, MVT::v64i8, 26}, // interleave 3 x 64i8 into 96i8 (and store)

      {4, MVT::v8i8, 10},  // interleave 4 x 8i8  into 32i8  (and store)
      {4, MVT::v16i8, 11}, // interleave 4 x 16i8 into 64i8  (and store)
      {4, MVT::v32i8, 14}, // interleave 4 x 32i8 into 128i8 (and store)
      {4, MVT::v64i8, 24}  // interleave 4 x 32i8 into 256i8 (and store)
  };

  if (const auto *Entry =
          CostTableLookup(AVX512InterleavedStoreTbl, Factor, VT))
    return MaskCost + NumOfMemOps * MemOpCost + Entry->Cost;
  //If an entry does not exist, fallback to the default implementation.

  // There is no strided stores meanwhile. And store can't be folded in
  // shuffle.
  unsigned NumOfSources = Factor; // The number of values to be merged.
  InstructionCost ShuffleCost = getShuffleCost(
      TTI::SK_PermuteTwoSrc, SingleMemOpTy, std::nullopt, CostKind, 0, nullptr);
  unsigned NumOfShufflesPerStore = NumOfSources - 1;

  // The SK_MergeTwoSrc shuffle clobbers one of src operands.
  // We need additional instructions to keep sources.
  unsigned NumOfMoves = NumOfMemOps * NumOfShufflesPerStore / 2;
  InstructionCost Cost =
      MaskCost +
      NumOfMemOps * (MemOpCost + NumOfShufflesPerStore * ShuffleCost) +
      NumOfMoves;
  return Cost;
}

InstructionCost X86TTIImpl::getInterleavedMemoryOpCost(
    unsigned Opcode, Type *BaseTy, unsigned Factor, ArrayRef<unsigned> Indices,
    Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
    bool UseMaskForCond, bool UseMaskForGaps) {
  auto *VecTy = cast<FixedVectorType>(BaseTy);

  auto isSupportedOnAVX512 = [&](Type *VecTy) {
    Type *EltTy = cast<VectorType>(VecTy)->getElementType();
    if (EltTy->isFloatTy() || EltTy->isDoubleTy() || EltTy->isIntegerTy(64) ||
        EltTy->isIntegerTy(32) || EltTy->isPointerTy())
      return true;
    if (EltTy->isIntegerTy(16) || EltTy->isIntegerTy(8) || EltTy->isHalfTy())
      return ST->hasBWI();
    if (EltTy->isBFloatTy())
      return ST->hasBF16();
    return false;
  };
  if (ST->hasAVX512() && isSupportedOnAVX512(VecTy))
    return getInterleavedMemoryOpCostAVX512(
        Opcode, VecTy, Factor, Indices, Alignment,
        AddressSpace, CostKind, UseMaskForCond, UseMaskForGaps);

  if (UseMaskForCond || UseMaskForGaps)
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace, CostKind,
                                             UseMaskForCond, UseMaskForGaps);

  // Get estimation for interleaved load/store operations for SSE-AVX2.
  // As opposed to AVX-512, SSE-AVX2 do not have generic shuffles that allow
  // computing the cost using a generic formula as a function of generic
  // shuffles. We therefore use a lookup table instead, filled according to
  // the instruction sequences that codegen currently generates.

  // VecTy for interleave memop is <VF*Factor x Elt>.
  // So, for VF=4, Interleave Factor = 3, Element type = i32 we have
  // VecTy = <12 x i32>.
  MVT LegalVT = getTypeLegalizationCost(VecTy).second;

  // This function can be called with VecTy=<6xi128>, Factor=3, in which case
  // the VF=2, while v2i128 is an unsupported MVT vector type
  // (see MachineValueType.h::getVectorVT()).
  if (!LegalVT.isVector())
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace, CostKind);

  unsigned VF = VecTy->getNumElements() / Factor;
  Type *ScalarTy = VecTy->getElementType();
  // Deduplicate entries, model floats/pointers as appropriately-sized integers.
  if (!ScalarTy->isIntegerTy())
    ScalarTy =
        Type::getIntNTy(ScalarTy->getContext(), DL.getTypeSizeInBits(ScalarTy));

  // Get the cost of all the memory operations.
  // FIXME: discount dead loads.
  InstructionCost MemOpCosts = getMemoryOpCost(
      Opcode, VecTy, MaybeAlign(Alignment), AddressSpace, CostKind);

  auto *VT = FixedVectorType::get(ScalarTy, VF);
  EVT ETy = TLI->getValueType(DL, VT);
  if (!ETy.isSimple())
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace, CostKind);

  // TODO: Complete for other data-types and strides.
  // Each combination of Stride, element bit width and VF results in a different
  // sequence; The cost tables are therefore accessed with:
  // Factor (stride) and VectorType=VFxiN.
  // The Cost accounts only for the shuffle sequence;
  // The cost of the loads/stores is accounted for separately.
  //
  static const CostTblEntry AVX2InterleavedLoadTbl[] = {
      {2, MVT::v2i8, 2},  // (load 4i8 and) deinterleave into 2 x 2i8
      {2, MVT::v4i8, 2},  // (load 8i8 and) deinterleave into 2 x 4i8
      {2, MVT::v8i8, 2},  // (load 16i8 and) deinterleave into 2 x 8i8
      {2, MVT::v16i8, 4}, // (load 32i8 and) deinterleave into 2 x 16i8
      {2, MVT::v32i8, 6}, // (load 64i8 and) deinterleave into 2 x 32i8

      {2, MVT::v8i16, 6},   // (load 16i16 and) deinterleave into 2 x 8i16
      {2, MVT::v16i16, 9},  // (load 32i16 and) deinterleave into 2 x 16i16
      {2, MVT::v32i16, 18}, // (load 64i16 and) deinterleave into 2 x 32i16

      {2, MVT::v8i32, 4},   // (load 16i32 and) deinterleave into 2 x 8i32
      {2, MVT::v16i32, 8},  // (load 32i32 and) deinterleave into 2 x 16i32
      {2, MVT::v32i32, 16}, // (load 64i32 and) deinterleave into 2 x 32i32

      {2, MVT::v4i64, 4},   // (load 8i64 and) deinterleave into 2 x 4i64
      {2, MVT::v8i64, 8},   // (load 16i64 and) deinterleave into 2 x 8i64
      {2, MVT::v16i64, 16}, // (load 32i64 and) deinterleave into 2 x 16i64
      {2, MVT::v32i64, 32}, // (load 64i64 and) deinterleave into 2 x 32i64

      {3, MVT::v2i8, 3},   // (load 6i8 and) deinterleave into 3 x 2i8
      {3, MVT::v4i8, 3},   // (load 12i8 and) deinterleave into 3 x 4i8
      {3, MVT::v8i8, 6},   // (load 24i8 and) deinterleave into 3 x 8i8
      {3, MVT::v16i8, 11}, // (load 48i8 and) deinterleave into 3 x 16i8
      {3, MVT::v32i8, 14}, // (load 96i8 and) deinterleave into 3 x 32i8

      {3, MVT::v2i16, 5},   // (load 6i16 and) deinterleave into 3 x 2i16
      {3, MVT::v4i16, 7},   // (load 12i16 and) deinterleave into 3 x 4i16
      {3, MVT::v8i16, 9},   // (load 24i16 and) deinterleave into 3 x 8i16
      {3, MVT::v16i16, 28}, // (load 48i16 and) deinterleave into 3 x 16i16
      {3, MVT::v32i16, 56}, // (load 96i16 and) deinterleave into 3 x 32i16

      {3, MVT::v2i32, 3},   // (load 6i32 and) deinterleave into 3 x 2i32
      {3, MVT::v4i32, 3},   // (load 12i32 and) deinterleave into 3 x 4i32
      {3, MVT::v8i32, 7},   // (load 24i32 and) deinterleave into 3 x 8i32
      {3, MVT::v16i32, 14}, // (load 48i32 and) deinterleave into 3 x 16i32
      {3, MVT::v32i32, 32}, // (load 96i32 and) deinterleave into 3 x 32i32

      {3, MVT::v2i64, 1},   // (load 6i64 and) deinterleave into 3 x 2i64
      {3, MVT::v4i64, 5},   // (load 12i64 and) deinterleave into 3 x 4i64
      {3, MVT::v8i64, 10},  // (load 24i64 and) deinterleave into 3 x 8i64
      {3, MVT::v16i64, 20}, // (load 48i64 and) deinterleave into 3 x 16i64

      {4, MVT::v2i8, 4},   // (load 8i8 and) deinterleave into 4 x 2i8
      {4, MVT::v4i8, 4},   // (load 16i8 and) deinterleave into 4 x 4i8
      {4, MVT::v8i8, 12},  // (load 32i8 and) deinterleave into 4 x 8i8
      {4, MVT::v16i8, 24}, // (load 64i8 and) deinterleave into 4 x 16i8
      {4, MVT::v32i8, 56}, // (load 128i8 and) deinterleave into 4 x 32i8

      {4, MVT::v2i16, 6},    // (load 8i16 and) deinterleave into 4 x 2i16
      {4, MVT::v4i16, 17},   // (load 16i16 and) deinterleave into 4 x 4i16
      {4, MVT::v8i16, 33},   // (load 32i16 and) deinterleave into 4 x 8i16
      {4, MVT::v16i16, 75},  // (load 64i16 and) deinterleave into 4 x 16i16
      {4, MVT::v32i16, 150}, // (load 128i16 and) deinterleave into 4 x 32i16

      {4, MVT::v2i32, 4},   // (load 8i32 and) deinterleave into 4 x 2i32
      {4, MVT::v4i32, 8},   // (load 16i32 and) deinterleave into 4 x 4i32
      {4, MVT::v8i32, 16},  // (load 32i32 and) deinterleave into 4 x 8i32
      {4, MVT::v16i32, 32}, // (load 64i32 and) deinterleave into 4 x 16i32
      {4, MVT::v32i32, 68}, // (load 128i32 and) deinterleave into 4 x 32i32

      {4, MVT::v2i64, 6},  // (load 8i64 and) deinterleave into 4 x 2i64
      {4, MVT::v4i64, 8},  // (load 16i64 and) deinterleave into 4 x 4i64
      {4, MVT::v8i64, 20}, // (load 32i64 and) deinterleave into 4 x 8i64
      {4, MVT::v16i64, 40}, // (load 64i64 and) deinterleave into 4 x 16i64

      {6, MVT::v2i8, 6},   // (load 12i8 and) deinterleave into 6 x 2i8
      {6, MVT::v4i8, 14},  // (load 24i8 and) deinterleave into 6 x 4i8
      {6, MVT::v8i8, 18},  // (load 48i8 and) deinterleave into 6 x 8i8
      {6, MVT::v16i8, 43}, // (load 96i8 and) deinterleave into 6 x 16i8
      {6, MVT::v32i8, 82}, // (load 192i8 and) deinterleave into 6 x 32i8

      {6, MVT::v2i16, 13},   // (load 12i16 and) deinterleave into 6 x 2i16
      {6, MVT::v4i16, 9},    // (load 24i16 and) deinterleave into 6 x 4i16
      {6, MVT::v8i16, 39},   // (load 48i16 and) deinterleave into 6 x 8i16
      {6, MVT::v16i16, 106}, // (load 96i16 and) deinterleave into 6 x 16i16
      {6, MVT::v32i16, 212}, // (load 192i16 and) deinterleave into 6 x 32i16

      {6, MVT::v2i32, 6},   // (load 12i32 and) deinterleave into 6 x 2i32
      {6, MVT::v4i32, 15},  // (load 24i32 and) deinterleave into 6 x 4i32
      {6, MVT::v8i32, 31},  // (load 48i32 and) deinterleave into 6 x 8i32
      {6, MVT::v16i32, 64}, // (load 96i32 and) deinterleave into 6 x 16i32

      {6, MVT::v2i64, 6},  // (load 12i64 and) deinterleave into 6 x 2i64
      {6, MVT::v4i64, 18}, // (load 24i64 and) deinterleave into 6 x 4i64
      {6, MVT::v8i64, 36}, // (load 48i64 and) deinterleave into 6 x 8i64

      {8, MVT::v8i32, 40} // (load 64i32 and) deinterleave into 8 x 8i32
  };

  static const CostTblEntry SSSE3InterleavedLoadTbl[] = {
      {2, MVT::v4i16, 2},   // (load 8i16 and) deinterleave into 2 x 4i16
  };

  static const CostTblEntry SSE2InterleavedLoadTbl[] = {
      {2, MVT::v2i16, 2},   // (load 4i16 and) deinterleave into 2 x 2i16
      {2, MVT::v4i16, 7},   // (load 8i16 and) deinterleave into 2 x 4i16

      {2, MVT::v2i32, 2},   // (load 4i32 and) deinterleave into 2 x 2i32
      {2, MVT::v4i32, 2},   // (load 8i32 and) deinterleave into 2 x 4i32

      {2, MVT::v2i64, 2},   // (load 4i64 and) deinterleave into 2 x 2i64
  };

  static const CostTblEntry AVX2InterleavedStoreTbl[] = {
      {2, MVT::v16i8, 3}, // interleave 2 x 16i8 into 32i8 (and store)
      {2, MVT::v32i8, 4}, // interleave 2 x 32i8 into 64i8 (and store)

      {2, MVT::v8i16, 3},  // interleave 2 x 8i16 into 16i16 (and store)
      {2, MVT::v16i16, 4}, // interleave 2 x 16i16 into 32i16 (and store)
      {2, MVT::v32i16, 8}, // interleave 2 x 32i16 into 64i16 (and store)

      {2, MVT::v4i32, 2},   // interleave 2 x 4i32 into 8i32 (and store)
      {2, MVT::v8i32, 4},   // interleave 2 x 8i32 into 16i32 (and store)
      {2, MVT::v16i32, 8},  // interleave 2 x 16i32 into 32i32 (and store)
      {2, MVT::v32i32, 16}, // interleave 2 x 32i32 into 64i32 (and store)

      {2, MVT::v2i64, 2},   // interleave 2 x 2i64 into 4i64 (and store)
      {2, MVT::v4i64, 4},   // interleave 2 x 4i64 into 8i64 (and store)
      {2, MVT::v8i64, 8},   // interleave 2 x 8i64 into 16i64 (and store)
      {2, MVT::v16i64, 16}, // interleave 2 x 16i64 into 32i64 (and store)
      {2, MVT::v32i64, 32}, // interleave 2 x 32i64 into 64i64 (and store)

      {3, MVT::v2i8, 4},   // interleave 3 x 2i8 into 6i8 (and store)
      {3, MVT::v4i8, 4},   // interleave 3 x 4i8 into 12i8 (and store)
      {3, MVT::v8i8, 6},   // interleave 3 x 8i8 into 24i8 (and store)
      {3, MVT::v16i8, 11}, // interleave 3 x 16i8 into 48i8 (and store)
      {3, MVT::v32i8, 13}, // interleave 3 x 32i8 into 96i8 (and store)

      {3, MVT::v2i16, 4},   // interleave 3 x 2i16 into 6i16 (and store)
      {3, MVT::v4i16, 6},   // interleave 3 x 4i16 into 12i16 (and store)
      {3, MVT::v8i16, 12},  // interleave 3 x 8i16 into 24i16 (and store)
      {3, MVT::v16i16, 27}, // interleave 3 x 16i16 into 48i16 (and store)
      {3, MVT::v32i16, 54}, // interleave 3 x 32i16 into 96i16 (and store)

      {3, MVT::v2i32, 4},   // interleave 3 x 2i32 into 6i32 (and store)
      {3, MVT::v4i32, 5},   // interleave 3 x 4i32 into 12i32 (and store)
      {3, MVT::v8i32, 11},  // interleave 3 x 8i32 into 24i32 (and store)
      {3, MVT::v16i32, 22}, // interleave 3 x 16i32 into 48i32 (and store)
      {3, MVT::v32i32, 48}, // interleave 3 x 32i32 into 96i32 (and store)

      {3, MVT::v2i64, 4},   // interleave 3 x 2i64 into 6i64 (and store)
      {3, MVT::v4i64, 6},   // interleave 3 x 4i64 into 12i64 (and store)
      {3, MVT::v8i64, 12},  // interleave 3 x 8i64 into 24i64 (and store)
      {3, MVT::v16i64, 24}, // interleave 3 x 16i64 into 48i64 (and store)

      {4, MVT::v2i8, 4},   // interleave 4 x 2i8 into 8i8 (and store)
      {4, MVT::v4i8, 4},   // interleave 4 x 4i8 into 16i8 (and store)
      {4, MVT::v8i8, 4},   // interleave 4 x 8i8 into 32i8 (and store)
      {4, MVT::v16i8, 8},  // interleave 4 x 16i8 into 64i8 (and store)
      {4, MVT::v32i8, 12}, // interleave 4 x 32i8 into 128i8 (and store)

      {4, MVT::v2i16, 2},   // interleave 4 x 2i16 into 8i16 (and store)
      {4, MVT::v4i16, 6},   // interleave 4 x 4i16 into 16i16 (and store)
      {4, MVT::v8i16, 10},  // interleave 4 x 8i16 into 32i16 (and store)
      {4, MVT::v16i16, 32}, // interleave 4 x 16i16 into 64i16 (and store)
      {4, MVT::v32i16, 64}, // interleave 4 x 32i16 into 128i16 (and store)

      {4, MVT::v2i32, 5},   // interleave 4 x 2i32 into 8i32 (and store)
      {4, MVT::v4i32, 6},   // interleave 4 x 4i32 into 16i32 (and store)
      {4, MVT::v8i32, 16},  // interleave 4 x 8i32 into 32i32 (and store)
      {4, MVT::v16i32, 32}, // interleave 4 x 16i32 into 64i32 (and store)
      {4, MVT::v32i32, 64}, // interleave 4 x 32i32 into 128i32 (and store)

      {4, MVT::v2i64, 6},  // interleave 4 x 2i64 into 8i64 (and store)
      {4, MVT::v4i64, 8},  // interleave 4 x 4i64 into 16i64 (and store)
      {4, MVT::v8i64, 20}, // interleave 4 x 8i64 into 32i64 (and store)
      {4, MVT::v16i64, 40}, // interleave 4 x 16i64 into 64i64 (and store)

      {6, MVT::v2i8, 7},   // interleave 6 x 2i8 into 12i8 (and store)
      {6, MVT::v4i8, 9},   // interleave 6 x 4i8 into 24i8 (and store)
      {6, MVT::v8i8, 16},  // interleave 6 x 8i8 into 48i8 (and store)
      {6, MVT::v16i8, 27}, // interleave 6 x 16i8 into 96i8 (and store)
      {6, MVT::v32i8, 90}, // interleave 6 x 32i8 into 192i8 (and store)

      {6, MVT::v2i16, 10},  // interleave 6 x 2i16 into 12i16 (and store)
      {6, MVT::v4i16, 15},  // interleave 6 x 4i16 into 24i16 (and store)
      {6, MVT::v8i16, 21},  // interleave 6 x 8i16 into 48i16 (and store)
      {6, MVT::v16i16, 58}, // interleave 6 x 16i16 into 96i16 (and store)
      {6, MVT::v32i16, 90}, // interleave 6 x 32i16 into 192i16 (and store)

      {6, MVT::v2i32, 9},   // interleave 6 x 2i32 into 12i32 (and store)
      {6, MVT::v4i32, 12},  // interleave 6 x 4i32 into 24i32 (and store)
      {6, MVT::v8i32, 33},  // interleave 6 x 8i32 into 48i32 (and store)
      {6, MVT::v16i32, 66}, // interleave 6 x 16i32 into 96i32 (and store)

      {6, MVT::v2i64, 8},  // interleave 6 x 2i64 into 12i64 (and store)
      {6, MVT::v4i64, 15}, // interleave 6 x 4i64 into 24i64 (and store)
      {6, MVT::v8i64, 30}, // interleave 6 x 8i64 into 48i64 (and store)
  };

  static const CostTblEntry SSE2InterleavedStoreTbl[] = {
      {2, MVT::v2i8, 1},   // interleave 2 x 2i8 into 4i8 (and store)
      {2, MVT::v4i8, 1},   // interleave 2 x 4i8 into 8i8 (and store)
      {2, MVT::v8i8, 1},   // interleave 2 x 8i8 into 16i8 (and store)

      {2, MVT::v2i16, 1},  // interleave 2 x 2i16 into 4i16 (and store)
      {2, MVT::v4i16, 1},  // interleave 2 x 4i16 into 8i16 (and store)

      {2, MVT::v2i32, 1},  // interleave 2 x 2i32 into 4i32 (and store)
  };

  if (Opcode == Instruction::Load) {
    auto GetDiscountedCost = [Factor, NumMembers = Indices.size(),
                              MemOpCosts](const CostTblEntry *Entry) {
      // NOTE: this is just an approximation!
      //       It can over/under -estimate the cost!
      return MemOpCosts + divideCeil(NumMembers * Entry->Cost, Factor);
    };

    if (ST->hasAVX2())
      if (const auto *Entry = CostTableLookup(AVX2InterleavedLoadTbl, Factor,
                                              ETy.getSimpleVT()))
        return GetDiscountedCost(Entry);

    if (ST->hasSSSE3())
      if (const auto *Entry = CostTableLookup(SSSE3InterleavedLoadTbl, Factor,
                                              ETy.getSimpleVT()))
        return GetDiscountedCost(Entry);

    if (ST->hasSSE2())
      if (const auto *Entry = CostTableLookup(SSE2InterleavedLoadTbl, Factor,
                                              ETy.getSimpleVT()))
        return GetDiscountedCost(Entry);
  } else {
    assert(Opcode == Instruction::Store &&
           "Expected Store Instruction at this point");
    assert((!Indices.size() || Indices.size() == Factor) &&
           "Interleaved store only supports fully-interleaved groups.");
    if (ST->hasAVX2())
      if (const auto *Entry = CostTableLookup(AVX2InterleavedStoreTbl, Factor,
                                              ETy.getSimpleVT()))
        return MemOpCosts + Entry->Cost;

    if (ST->hasSSE2())
      if (const auto *Entry = CostTableLookup(SSE2InterleavedStoreTbl, Factor,
                                              ETy.getSimpleVT()))
        return MemOpCosts + Entry->Cost;
  }

  return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                           Alignment, AddressSpace, CostKind,
                                           UseMaskForCond, UseMaskForGaps);
}

InstructionCost X86TTIImpl::getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                                 StackOffset BaseOffset,
                                                 bool HasBaseReg, int64_t Scale,
                                                 unsigned AddrSpace) const {
  // Scaling factors are not free at all.
  // An indexed folded instruction, i.e., inst (reg1, reg2, scale),
  // will take 2 allocations in the out of order engine instead of 1
  // for plain addressing mode, i.e. inst (reg1).
  // E.g.,
  // vaddps (%rsi,%rdx), %ymm0, %ymm1
  // Requires two allocations (one for the load, one for the computation)
  // whereas:
  // vaddps (%rsi), %ymm0, %ymm1
  // Requires just 1 allocation, i.e., freeing allocations for other operations
  // and having less micro operations to execute.
  //
  // For some X86 architectures, this is even worse because for instance for
  // stores, the complex addressing mode forces the instruction to use the
  // "load" ports instead of the dedicated "store" port.
  // E.g., on Haswell:
  // vmovaps %ymm1, (%r8, %rdi) can use port 2 or 3.
  // vmovaps %ymm1, (%r8) can use port 2, 3, or 7.
  TargetLoweringBase::AddrMode AM;
  AM.BaseGV = BaseGV;
  AM.BaseOffs = BaseOffset.getFixed();
  AM.HasBaseReg = HasBaseReg;
  AM.Scale = Scale;
  AM.ScalableOffset = BaseOffset.getScalable();
  if (getTLI()->isLegalAddressingMode(DL, AM, Ty, AddrSpace))
    // Scale represents reg2 * scale, thus account for 1
    // as soon as we use a second register.
    return AM.Scale != 0;
  return -1;
}

InstructionCost X86TTIImpl::getBranchMispredictPenalty() const {
  // TODO: Hook MispredictPenalty of SchedMachineModel into this.
  return 14;
}
