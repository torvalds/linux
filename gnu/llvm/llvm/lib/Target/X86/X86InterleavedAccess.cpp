//===- X86InterleavedAccess.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains the X86 implementation of the interleaved accesses
/// optimization generating X86-specific instructions/intrinsics for
/// interleaved access groups.
//
//===----------------------------------------------------------------------===//

#include "X86ISelLowering.h"
#include "X86Subtarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

using namespace llvm;

namespace {

/// This class holds necessary information to represent an interleaved
/// access group and supports utilities to lower the group into
/// X86-specific instructions/intrinsics.
///  E.g. A group of interleaving access loads (Factor = 2; accessing every
///       other element)
///        %wide.vec = load <8 x i32>, <8 x i32>* %ptr
///        %v0 = shuffle <8 x i32> %wide.vec, <8 x i32> poison, <0, 2, 4, 6>
///        %v1 = shuffle <8 x i32> %wide.vec, <8 x i32> poison, <1, 3, 5, 7>
class X86InterleavedAccessGroup {
  /// Reference to the wide-load instruction of an interleaved access
  /// group.
  Instruction *const Inst;

  /// Reference to the shuffle(s), consumer(s) of the (load) 'Inst'.
  ArrayRef<ShuffleVectorInst *> Shuffles;

  /// Reference to the starting index of each user-shuffle.
  ArrayRef<unsigned> Indices;

  /// Reference to the interleaving stride in terms of elements.
  const unsigned Factor;

  /// Reference to the underlying target.
  const X86Subtarget &Subtarget;

  const DataLayout &DL;

  IRBuilder<> &Builder;

  /// Breaks down a vector \p 'Inst' of N elements into \p NumSubVectors
  /// sub vectors of type \p T. Returns the sub-vectors in \p DecomposedVectors.
  void decompose(Instruction *Inst, unsigned NumSubVectors, FixedVectorType *T,
                 SmallVectorImpl<Instruction *> &DecomposedVectors);

  /// Performs matrix transposition on a 4x4 matrix \p InputVectors and
  /// returns the transposed-vectors in \p TransposedVectors.
  /// E.g.
  /// InputVectors:
  ///   In-V0 = p1, p2, p3, p4
  ///   In-V1 = q1, q2, q3, q4
  ///   In-V2 = r1, r2, r3, r4
  ///   In-V3 = s1, s2, s3, s4
  /// OutputVectors:
  ///   Out-V0 = p1, q1, r1, s1
  ///   Out-V1 = p2, q2, r2, s2
  ///   Out-V2 = p3, q3, r3, s3
  ///   Out-V3 = P4, q4, r4, s4
  void transpose_4x4(ArrayRef<Instruction *> InputVectors,
                     SmallVectorImpl<Value *> &TransposedMatrix);
  void interleave8bitStride4(ArrayRef<Instruction *> InputVectors,
                             SmallVectorImpl<Value *> &TransposedMatrix,
                             unsigned NumSubVecElems);
  void interleave8bitStride4VF8(ArrayRef<Instruction *> InputVectors,
                                SmallVectorImpl<Value *> &TransposedMatrix);
  void interleave8bitStride3(ArrayRef<Instruction *> InputVectors,
                             SmallVectorImpl<Value *> &TransposedMatrix,
                             unsigned NumSubVecElems);
  void deinterleave8bitStride3(ArrayRef<Instruction *> InputVectors,
                               SmallVectorImpl<Value *> &TransposedMatrix,
                               unsigned NumSubVecElems);

public:
  /// In order to form an interleaved access group X86InterleavedAccessGroup
  /// requires a wide-load instruction \p 'I', a group of interleaved-vectors
  /// \p Shuffs, reference to the first indices of each interleaved-vector
  /// \p 'Ind' and the interleaving stride factor \p F. In order to generate
  /// X86-specific instructions/intrinsics it also requires the underlying
  /// target information \p STarget.
  explicit X86InterleavedAccessGroup(Instruction *I,
                                     ArrayRef<ShuffleVectorInst *> Shuffs,
                                     ArrayRef<unsigned> Ind, const unsigned F,
                                     const X86Subtarget &STarget,
                                     IRBuilder<> &B)
      : Inst(I), Shuffles(Shuffs), Indices(Ind), Factor(F), Subtarget(STarget),
        DL(Inst->getDataLayout()), Builder(B) {}

  /// Returns true if this interleaved access group can be lowered into
  /// x86-specific instructions/intrinsics, false otherwise.
  bool isSupported() const;

  /// Lowers this interleaved access group into X86-specific
  /// instructions/intrinsics.
  bool lowerIntoOptimizedSequence();
};

} // end anonymous namespace

bool X86InterleavedAccessGroup::isSupported() const {
  VectorType *ShuffleVecTy = Shuffles[0]->getType();
  Type *ShuffleEltTy = ShuffleVecTy->getElementType();
  unsigned ShuffleElemSize = DL.getTypeSizeInBits(ShuffleEltTy);
  unsigned WideInstSize;

  // Currently, lowering is supported for the following vectors:
  // Stride 4:
  //    1. Store and load of 4-element vectors of 64 bits on AVX.
  //    2. Store of 16/32-element vectors of 8 bits on AVX.
  // Stride 3:
  //    1. Load of 16/32-element vectors of 8 bits on AVX.
  if (!Subtarget.hasAVX() || (Factor != 4 && Factor != 3))
    return false;

  if (isa<LoadInst>(Inst)) {
    WideInstSize = DL.getTypeSizeInBits(Inst->getType());
    if (cast<LoadInst>(Inst)->getPointerAddressSpace())
      return false;
  } else
    WideInstSize = DL.getTypeSizeInBits(Shuffles[0]->getType());

  // We support shuffle represents stride 4 for byte type with size of
  // WideInstSize.
  if (ShuffleElemSize == 64 && WideInstSize == 1024 && Factor == 4)
    return true;

  if (ShuffleElemSize == 8 && isa<StoreInst>(Inst) && Factor == 4 &&
      (WideInstSize == 256 || WideInstSize == 512 || WideInstSize == 1024 ||
       WideInstSize == 2048))
    return true;

  if (ShuffleElemSize == 8 && Factor == 3 &&
      (WideInstSize == 384 || WideInstSize == 768 || WideInstSize == 1536))
    return true;

  return false;
}

void X86InterleavedAccessGroup::decompose(
    Instruction *VecInst, unsigned NumSubVectors, FixedVectorType *SubVecTy,
    SmallVectorImpl<Instruction *> &DecomposedVectors) {
  assert((isa<LoadInst>(VecInst) || isa<ShuffleVectorInst>(VecInst)) &&
         "Expected Load or Shuffle");

  Type *VecWidth = VecInst->getType();
  (void)VecWidth;
  assert(VecWidth->isVectorTy() &&
         DL.getTypeSizeInBits(VecWidth) >=
             DL.getTypeSizeInBits(SubVecTy) * NumSubVectors &&
         "Invalid Inst-size!!!");

  if (auto *SVI = dyn_cast<ShuffleVectorInst>(VecInst)) {
    Value *Op0 = SVI->getOperand(0);
    Value *Op1 = SVI->getOperand(1);

    // Generate N(= NumSubVectors) shuffles of T(= SubVecTy) type.
    for (unsigned i = 0; i < NumSubVectors; ++i)
      DecomposedVectors.push_back(
          cast<ShuffleVectorInst>(Builder.CreateShuffleVector(
              Op0, Op1,
              createSequentialMask(Indices[i], SubVecTy->getNumElements(),
                                   0))));
    return;
  }

  // Decompose the load instruction.
  LoadInst *LI = cast<LoadInst>(VecInst);
  Type *VecBaseTy;
  unsigned int NumLoads = NumSubVectors;
  // In the case of stride 3 with a vector of 32 elements load the information
  // in the following way:
  // [0,1...,VF/2-1,VF/2+VF,VF/2+VF+1,...,2VF-1]
  unsigned VecLength = DL.getTypeSizeInBits(VecWidth);
  Value *VecBasePtr = LI->getPointerOperand();
  if (VecLength == 768 || VecLength == 1536) {
    VecBaseTy = FixedVectorType::get(Type::getInt8Ty(LI->getContext()), 16);
    NumLoads = NumSubVectors * (VecLength / 384);
  } else {
    VecBaseTy = SubVecTy;
  }
  // Generate N loads of T type.
  assert(VecBaseTy->getPrimitiveSizeInBits().isKnownMultipleOf(8) &&
         "VecBaseTy's size must be a multiple of 8");
  const Align FirstAlignment = LI->getAlign();
  const Align SubsequentAlignment = commonAlignment(
      FirstAlignment, VecBaseTy->getPrimitiveSizeInBits().getFixedValue() / 8);
  Align Alignment = FirstAlignment;
  for (unsigned i = 0; i < NumLoads; i++) {
    // TODO: Support inbounds GEP.
    Value *NewBasePtr =
        Builder.CreateGEP(VecBaseTy, VecBasePtr, Builder.getInt32(i));
    Instruction *NewLoad =
        Builder.CreateAlignedLoad(VecBaseTy, NewBasePtr, Alignment);
    DecomposedVectors.push_back(NewLoad);
    Alignment = SubsequentAlignment;
  }
}

// Changing the scale of the vector type by reducing the number of elements and
// doubling the scalar size.
static MVT scaleVectorType(MVT VT) {
  unsigned ScalarSize = VT.getVectorElementType().getScalarSizeInBits() * 2;
  return MVT::getVectorVT(MVT::getIntegerVT(ScalarSize),
                          VT.getVectorNumElements() / 2);
}

static constexpr int Concat[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};

// genShuffleBland - Creates shuffle according to two vectors.This function is
// only works on instructions with lane inside 256 registers. According to
// the mask 'Mask' creates a new Mask 'Out' by the offset of the mask. The
// offset amount depends on the two integer, 'LowOffset' and 'HighOffset'.
// Where the 'LowOffset' refers to the first vector and the highOffset refers to
// the second vector.
// |a0....a5,b0....b4,c0....c4|a16..a21,b16..b20,c16..c20|
// |c5...c10,a5....a9,b5....b9|c21..c26,a22..a26,b21..b25|
// |b10..b15,c11..c15,a10..a15|b26..b31,c27..c31,a27..a31|
// For the sequence to work as a mirror to the load.
// We must consider the elements order as above.
// In this function we are combining two types of shuffles.
// The first one is vpshufed and the second is a type of "blend" shuffle.
// By computing the shuffle on a sequence of 16 elements(one lane) and add the
// correct offset. We are creating a vpsuffed + blend sequence between two
// shuffles.
static void genShuffleBland(MVT VT, ArrayRef<int> Mask,
                            SmallVectorImpl<int> &Out, int LowOffset,
                            int HighOffset) {
  assert(VT.getSizeInBits() >= 256 &&
         "This function doesn't accept width smaller then 256");
  unsigned NumOfElm = VT.getVectorNumElements();
  for (int I : Mask)
    Out.push_back(I + LowOffset);
  for (int I : Mask)
    Out.push_back(I + HighOffset + NumOfElm);
}

// reorderSubVector returns the data to is the original state. And de-facto is
// the opposite of  the function concatSubVector.

// For VecElems = 16
// Invec[0] -  |0|      TransposedMatrix[0] - |0|
// Invec[1] -  |1|  =>  TransposedMatrix[1] - |1|
// Invec[2] -  |2|      TransposedMatrix[2] - |2|

// For VecElems = 32
// Invec[0] -  |0|3|      TransposedMatrix[0] - |0|1|
// Invec[1] -  |1|4|  =>  TransposedMatrix[1] - |2|3|
// Invec[2] -  |2|5|      TransposedMatrix[2] - |4|5|

// For VecElems = 64
// Invec[0] -  |0|3|6|9 |     TransposedMatrix[0] - |0|1|2 |3 |
// Invec[1] -  |1|4|7|10| =>  TransposedMatrix[1] - |4|5|6 |7 |
// Invec[2] -  |2|5|8|11|     TransposedMatrix[2] - |8|9|10|11|

static void reorderSubVector(MVT VT, SmallVectorImpl<Value *> &TransposedMatrix,
                             ArrayRef<Value *> Vec, ArrayRef<int> VPShuf,
                             unsigned VecElems, unsigned Stride,
                             IRBuilder<> &Builder) {

  if (VecElems == 16) {
    for (unsigned i = 0; i < Stride; i++)
      TransposedMatrix[i] = Builder.CreateShuffleVector(Vec[i], VPShuf);
    return;
  }

  SmallVector<int, 32> OptimizeShuf;
  Value *Temp[8];

  for (unsigned i = 0; i < (VecElems / 16) * Stride; i += 2) {
    genShuffleBland(VT, VPShuf, OptimizeShuf, (i / Stride) * 16,
                    (i + 1) / Stride * 16);
    Temp[i / 2] = Builder.CreateShuffleVector(
        Vec[i % Stride], Vec[(i + 1) % Stride], OptimizeShuf);
    OptimizeShuf.clear();
  }

  if (VecElems == 32) {
    std::copy(Temp, Temp + Stride, TransposedMatrix.begin());
    return;
  } else
    for (unsigned i = 0; i < Stride; i++)
      TransposedMatrix[i] =
          Builder.CreateShuffleVector(Temp[2 * i], Temp[2 * i + 1], Concat);
}

void X86InterleavedAccessGroup::interleave8bitStride4VF8(
    ArrayRef<Instruction *> Matrix,
    SmallVectorImpl<Value *> &TransposedMatrix) {
  // Assuming we start from the following vectors:
  // Matrix[0]= c0 c1 c2 c3 c4 ... c7
  // Matrix[1]= m0 m1 m2 m3 m4 ... m7
  // Matrix[2]= y0 y1 y2 y3 y4 ... y7
  // Matrix[3]= k0 k1 k2 k3 k4 ... k7

  MVT VT = MVT::v8i16;
  TransposedMatrix.resize(2);
  SmallVector<int, 16> MaskLow;
  SmallVector<int, 32> MaskLowTemp1, MaskLowWord;
  SmallVector<int, 32> MaskHighTemp1, MaskHighWord;

  for (unsigned i = 0; i < 8; ++i) {
    MaskLow.push_back(i);
    MaskLow.push_back(i + 8);
  }

  createUnpackShuffleMask(VT, MaskLowTemp1, true, false);
  createUnpackShuffleMask(VT, MaskHighTemp1, false, false);
  narrowShuffleMaskElts(2, MaskHighTemp1, MaskHighWord);
  narrowShuffleMaskElts(2, MaskLowTemp1, MaskLowWord);
  // IntrVec1Low = c0 m0 c1 m1 c2 m2 c3 m3 c4 m4 c5 m5 c6 m6 c7 m7
  // IntrVec2Low = y0 k0 y1 k1 y2 k2 y3 k3 y4 k4 y5 k5 y6 k6 y7 k7
  Value *IntrVec1Low =
      Builder.CreateShuffleVector(Matrix[0], Matrix[1], MaskLow);
  Value *IntrVec2Low =
      Builder.CreateShuffleVector(Matrix[2], Matrix[3], MaskLow);

  // TransposedMatrix[0] = c0 m0 y0 k0 c1 m1 y1 k1 c2 m2 y2 k2 c3 m3 y3 k3
  // TransposedMatrix[1] = c4 m4 y4 k4 c5 m5 y5 k5 c6 m6 y6 k6 c7 m7 y7 k7

  TransposedMatrix[0] =
      Builder.CreateShuffleVector(IntrVec1Low, IntrVec2Low, MaskLowWord);
  TransposedMatrix[1] =
      Builder.CreateShuffleVector(IntrVec1Low, IntrVec2Low, MaskHighWord);
}

void X86InterleavedAccessGroup::interleave8bitStride4(
    ArrayRef<Instruction *> Matrix, SmallVectorImpl<Value *> &TransposedMatrix,
    unsigned NumOfElm) {
  // Example: Assuming we start from the following vectors:
  // Matrix[0]= c0 c1 c2 c3 c4 ... c31
  // Matrix[1]= m0 m1 m2 m3 m4 ... m31
  // Matrix[2]= y0 y1 y2 y3 y4 ... y31
  // Matrix[3]= k0 k1 k2 k3 k4 ... k31

  MVT VT = MVT::getVectorVT(MVT::i8, NumOfElm);
  MVT HalfVT = scaleVectorType(VT);

  TransposedMatrix.resize(4);
  SmallVector<int, 32> MaskHigh;
  SmallVector<int, 32> MaskLow;
  SmallVector<int, 32> LowHighMask[2];
  SmallVector<int, 32> MaskHighTemp;
  SmallVector<int, 32> MaskLowTemp;

  // MaskHighTemp and MaskLowTemp built in the vpunpckhbw and vpunpcklbw X86
  // shuffle pattern.

  createUnpackShuffleMask(VT, MaskLow, true, false);
  createUnpackShuffleMask(VT, MaskHigh, false, false);

  // MaskHighTemp1 and MaskLowTemp1 built in the vpunpckhdw and vpunpckldw X86
  // shuffle pattern.

  createUnpackShuffleMask(HalfVT, MaskLowTemp, true, false);
  createUnpackShuffleMask(HalfVT, MaskHighTemp, false, false);
  narrowShuffleMaskElts(2, MaskLowTemp, LowHighMask[0]);
  narrowShuffleMaskElts(2, MaskHighTemp, LowHighMask[1]);

  // IntrVec1Low  = c0  m0  c1  m1 ... c7  m7  | c16 m16 c17 m17 ... c23 m23
  // IntrVec1High = c8  m8  c9  m9 ... c15 m15 | c24 m24 c25 m25 ... c31 m31
  // IntrVec2Low  = y0  k0  y1  k1 ... y7  k7  | y16 k16 y17 k17 ... y23 k23
  // IntrVec2High = y8  k8  y9  k9 ... y15 k15 | y24 k24 y25 k25 ... y31 k31
  Value *IntrVec[4];

  IntrVec[0] = Builder.CreateShuffleVector(Matrix[0], Matrix[1], MaskLow);
  IntrVec[1] = Builder.CreateShuffleVector(Matrix[0], Matrix[1], MaskHigh);
  IntrVec[2] = Builder.CreateShuffleVector(Matrix[2], Matrix[3], MaskLow);
  IntrVec[3] = Builder.CreateShuffleVector(Matrix[2], Matrix[3], MaskHigh);

  // cmyk4  cmyk5  cmyk6   cmyk7  | cmyk20 cmyk21 cmyk22 cmyk23
  // cmyk12 cmyk13 cmyk14  cmyk15 | cmyk28 cmyk29 cmyk30 cmyk31
  // cmyk0  cmyk1  cmyk2   cmyk3  | cmyk16 cmyk17 cmyk18 cmyk19
  // cmyk8  cmyk9  cmyk10  cmyk11 | cmyk24 cmyk25 cmyk26 cmyk27

  Value *VecOut[4];
  for (int i = 0; i < 4; i++)
    VecOut[i] = Builder.CreateShuffleVector(IntrVec[i / 2], IntrVec[i / 2 + 2],
                                            LowHighMask[i % 2]);

  // cmyk0  cmyk1  cmyk2  cmyk3   | cmyk4  cmyk5  cmyk6  cmyk7
  // cmyk8  cmyk9  cmyk10 cmyk11  | cmyk12 cmyk13 cmyk14 cmyk15
  // cmyk16 cmyk17 cmyk18 cmyk19  | cmyk20 cmyk21 cmyk22 cmyk23
  // cmyk24 cmyk25 cmyk26 cmyk27  | cmyk28 cmyk29 cmyk30 cmyk31

  if (VT == MVT::v16i8) {
    std::copy(VecOut, VecOut + 4, TransposedMatrix.begin());
    return;
  }

  reorderSubVector(VT, TransposedMatrix, VecOut, ArrayRef(Concat, 16), NumOfElm,
                   4, Builder);
}

//  createShuffleStride returns shuffle mask of size N.
//  The shuffle pattern is as following :
//  {0, Stride%(VF/Lane), (2*Stride%(VF/Lane))...(VF*Stride/Lane)%(VF/Lane),
//  (VF/ Lane) ,(VF / Lane)+Stride%(VF/Lane),...,
//  (VF / Lane)+(VF*Stride/Lane)%(VF/Lane)}
//  Where Lane is the # of lanes in a register:
//  VectorSize = 128 => Lane = 1
//  VectorSize = 256 => Lane = 2
//  For example shuffle pattern for VF 16 register size 256 -> lanes = 2
//  {<[0|3|6|1|4|7|2|5]-[8|11|14|9|12|15|10|13]>}
static void createShuffleStride(MVT VT, int Stride,
                                SmallVectorImpl<int> &Mask) {
  int VectorSize = VT.getSizeInBits();
  int VF = VT.getVectorNumElements();
  int LaneCount = std::max(VectorSize / 128, 1);
  for (int Lane = 0; Lane < LaneCount; Lane++)
    for (int i = 0, LaneSize = VF / LaneCount; i != LaneSize; ++i)
      Mask.push_back((i * Stride) % LaneSize + LaneSize * Lane);
}

//  setGroupSize sets 'SizeInfo' to the size(number of elements) of group
//  inside mask a shuffleMask. A mask contains exactly 3 groups, where
//  each group is a monotonically increasing sequence with stride 3.
//  For example shuffleMask {0,3,6,1,4,7,2,5} => {3,3,2}
static void setGroupSize(MVT VT, SmallVectorImpl<int> &SizeInfo) {
  int VectorSize = VT.getSizeInBits();
  int VF = VT.getVectorNumElements() / std::max(VectorSize / 128, 1);
  for (int i = 0, FirstGroupElement = 0; i < 3; i++) {
    int GroupSize = std::ceil((VF - FirstGroupElement) / 3.0);
    SizeInfo.push_back(GroupSize);
    FirstGroupElement = ((GroupSize)*3 + FirstGroupElement) % VF;
  }
}

//  DecodePALIGNRMask returns the shuffle mask of vpalign instruction.
//  vpalign works according to lanes
//  Where Lane is the # of lanes in a register:
//  VectorWide = 128 => Lane = 1
//  VectorWide = 256 => Lane = 2
//  For Lane = 1 shuffle pattern is: {DiffToJump,...,DiffToJump+VF-1}.
//  For Lane = 2 shuffle pattern is:
//  {DiffToJump,...,VF/2-1,VF,...,DiffToJump+VF-1}.
//  Imm variable sets the offset amount. The result of the
//  function is stored inside ShuffleMask vector and it built as described in
//  the begin of the description. AlignDirection is a boolean that indicates the
//  direction of the alignment. (false - align to the "right" side while true -
//  align to the "left" side)
static void DecodePALIGNRMask(MVT VT, unsigned Imm,
                              SmallVectorImpl<int> &ShuffleMask,
                              bool AlignDirection = true, bool Unary = false) {
  unsigned NumElts = VT.getVectorNumElements();
  unsigned NumLanes = std::max((int)VT.getSizeInBits() / 128, 1);
  unsigned NumLaneElts = NumElts / NumLanes;

  Imm = AlignDirection ? Imm : (NumLaneElts - Imm);
  unsigned Offset = Imm * (VT.getScalarSizeInBits() / 8);

  for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
    for (unsigned i = 0; i != NumLaneElts; ++i) {
      unsigned Base = i + Offset;
      // if i+offset is out of this lane then we actually need the other source
      // If Unary the other source is the first source.
      if (Base >= NumLaneElts)
        Base = Unary ? Base % NumLaneElts : Base + NumElts - NumLaneElts;
      ShuffleMask.push_back(Base + l);
    }
  }
}

// concatSubVector - The function rebuilds the data to a correct expected
// order. An assumption(The shape of the matrix) was taken for the
// deinterleaved to work with lane's instructions like 'vpalign' or 'vphuf'.
// This function ensures that the data is built in correct way for the lane
// instructions. Each lane inside the vector is a 128-bit length.
//
// The 'InVec' argument contains the data in increasing order. In InVec[0] You
// can find the first 128 bit data. The number of different lanes inside a
// vector depends on the 'VecElems'.In general, the formula is
// VecElems * type / 128. The size of the array 'InVec' depends and equal to
// 'VecElems'.

// For VecElems = 16
// Invec[0] - |0|      Vec[0] - |0|
// Invec[1] - |1|  =>  Vec[1] - |1|
// Invec[2] - |2|      Vec[2] - |2|

// For VecElems = 32
// Invec[0] - |0|1|      Vec[0] - |0|3|
// Invec[1] - |2|3|  =>  Vec[1] - |1|4|
// Invec[2] - |4|5|      Vec[2] - |2|5|

// For VecElems = 64
// Invec[0] - |0|1|2 |3 |      Vec[0] - |0|3|6|9 |
// Invec[1] - |4|5|6 |7 |  =>  Vec[1] - |1|4|7|10|
// Invec[2] - |8|9|10|11|      Vec[2] - |2|5|8|11|

static void concatSubVector(Value **Vec, ArrayRef<Instruction *> InVec,
                            unsigned VecElems, IRBuilder<> &Builder) {
  if (VecElems == 16) {
    for (int i = 0; i < 3; i++)
      Vec[i] = InVec[i];
    return;
  }

  for (unsigned j = 0; j < VecElems / 32; j++)
    for (int i = 0; i < 3; i++)
      Vec[i + j * 3] = Builder.CreateShuffleVector(
          InVec[j * 6 + i], InVec[j * 6 + i + 3], ArrayRef(Concat, 32));

  if (VecElems == 32)
    return;

  for (int i = 0; i < 3; i++)
    Vec[i] = Builder.CreateShuffleVector(Vec[i], Vec[i + 3], Concat);
}

void X86InterleavedAccessGroup::deinterleave8bitStride3(
    ArrayRef<Instruction *> InVec, SmallVectorImpl<Value *> &TransposedMatrix,
    unsigned VecElems) {
  // Example: Assuming we start from the following vectors:
  // Matrix[0]= a0 b0 c0 a1 b1 c1 a2 b2
  // Matrix[1]= c2 a3 b3 c3 a4 b4 c4 a5
  // Matrix[2]= b5 c5 a6 b6 c6 a7 b7 c7

  TransposedMatrix.resize(3);
  SmallVector<int, 32> VPShuf;
  SmallVector<int, 32> VPAlign[2];
  SmallVector<int, 32> VPAlign2;
  SmallVector<int, 32> VPAlign3;
  SmallVector<int, 3> GroupSize;
  Value *Vec[6], *TempVector[3];

  MVT VT = MVT::getVT(Shuffles[0]->getType());

  createShuffleStride(VT, 3, VPShuf);
  setGroupSize(VT, GroupSize);

  for (int i = 0; i < 2; i++)
    DecodePALIGNRMask(VT, GroupSize[2 - i], VPAlign[i], false);

  DecodePALIGNRMask(VT, GroupSize[2] + GroupSize[1], VPAlign2, true, true);
  DecodePALIGNRMask(VT, GroupSize[1], VPAlign3, true, true);

  concatSubVector(Vec, InVec, VecElems, Builder);
  // Vec[0]= a0 a1 a2 b0 b1 b2 c0 c1
  // Vec[1]= c2 c3 c4 a3 a4 a5 b3 b4
  // Vec[2]= b5 b6 b7 c5 c6 c7 a6 a7

  for (int i = 0; i < 3; i++)
    Vec[i] = Builder.CreateShuffleVector(Vec[i], VPShuf);

  // TempVector[0]= a6 a7 a0 a1 a2 b0 b1 b2
  // TempVector[1]= c0 c1 c2 c3 c4 a3 a4 a5
  // TempVector[2]= b3 b4 b5 b6 b7 c5 c6 c7

  for (int i = 0; i < 3; i++)
    TempVector[i] =
        Builder.CreateShuffleVector(Vec[(i + 2) % 3], Vec[i], VPAlign[0]);

  // Vec[0]= a3 a4 a5 a6 a7 a0 a1 a2
  // Vec[1]= c5 c6 c7 c0 c1 c2 c3 c4
  // Vec[2]= b0 b1 b2 b3 b4 b5 b6 b7

  for (int i = 0; i < 3; i++)
    Vec[i] = Builder.CreateShuffleVector(TempVector[(i + 1) % 3], TempVector[i],
                                         VPAlign[1]);

  // TransposedMatrix[0]= a0 a1 a2 a3 a4 a5 a6 a7
  // TransposedMatrix[1]= b0 b1 b2 b3 b4 b5 b6 b7
  // TransposedMatrix[2]= c0 c1 c2 c3 c4 c5 c6 c7

  Value *TempVec = Builder.CreateShuffleVector(Vec[1], VPAlign3);
  TransposedMatrix[0] = Builder.CreateShuffleVector(Vec[0], VPAlign2);
  TransposedMatrix[1] = VecElems == 8 ? Vec[2] : TempVec;
  TransposedMatrix[2] = VecElems == 8 ? TempVec : Vec[2];
}

// group2Shuffle reorder the shuffle stride back into continuous order.
// For example For VF16 with Mask1 = {0,3,6,9,12,15,2,5,8,11,14,1,4,7,10,13} =>
// MaskResult = {0,11,6,1,12,7,2,13,8,3,14,9,4,15,10,5}.
static void group2Shuffle(MVT VT, SmallVectorImpl<int> &Mask,
                          SmallVectorImpl<int> &Output) {
  int IndexGroup[3] = {0, 0, 0};
  int Index = 0;
  int VectorWidth = VT.getSizeInBits();
  int VF = VT.getVectorNumElements();
  // Find the index of the different groups.
  int Lane = (VectorWidth / 128 > 0) ? VectorWidth / 128 : 1;
  for (int i = 0; i < 3; i++) {
    IndexGroup[(Index * 3) % (VF / Lane)] = Index;
    Index += Mask[i];
  }
  // According to the index compute the convert mask.
  for (int i = 0; i < VF / Lane; i++) {
    Output.push_back(IndexGroup[i % 3]);
    IndexGroup[i % 3]++;
  }
}

void X86InterleavedAccessGroup::interleave8bitStride3(
    ArrayRef<Instruction *> InVec, SmallVectorImpl<Value *> &TransposedMatrix,
    unsigned VecElems) {
  // Example: Assuming we start from the following vectors:
  // Matrix[0]= a0 a1 a2 a3 a4 a5 a6 a7
  // Matrix[1]= b0 b1 b2 b3 b4 b5 b6 b7
  // Matrix[2]= c0 c1 c2 c3 c3 a7 b7 c7

  TransposedMatrix.resize(3);
  SmallVector<int, 3> GroupSize;
  SmallVector<int, 32> VPShuf;
  SmallVector<int, 32> VPAlign[3];
  SmallVector<int, 32> VPAlign2;
  SmallVector<int, 32> VPAlign3;

  Value *Vec[3], *TempVector[3];
  MVT VT = MVT::getVectorVT(MVT::i8, VecElems);

  setGroupSize(VT, GroupSize);

  for (int i = 0; i < 3; i++)
    DecodePALIGNRMask(VT, GroupSize[i], VPAlign[i]);

  DecodePALIGNRMask(VT, GroupSize[1] + GroupSize[2], VPAlign2, false, true);
  DecodePALIGNRMask(VT, GroupSize[1], VPAlign3, false, true);

  // Vec[0]= a3 a4 a5 a6 a7 a0 a1 a2
  // Vec[1]= c5 c6 c7 c0 c1 c2 c3 c4
  // Vec[2]= b0 b1 b2 b3 b4 b5 b6 b7

  Vec[0] = Builder.CreateShuffleVector(InVec[0], VPAlign2);
  Vec[1] = Builder.CreateShuffleVector(InVec[1], VPAlign3);
  Vec[2] = InVec[2];

  // Vec[0]= a6 a7 a0 a1 a2 b0 b1 b2
  // Vec[1]= c0 c1 c2 c3 c4 a3 a4 a5
  // Vec[2]= b3 b4 b5 b6 b7 c5 c6 c7

  for (int i = 0; i < 3; i++)
    TempVector[i] =
        Builder.CreateShuffleVector(Vec[i], Vec[(i + 2) % 3], VPAlign[1]);

  // Vec[0]= a0 a1 a2 b0 b1 b2 c0 c1
  // Vec[1]= c2 c3 c4 a3 a4 a5 b3 b4
  // Vec[2]= b5 b6 b7 c5 c6 c7 a6 a7

  for (int i = 0; i < 3; i++)
    Vec[i] = Builder.CreateShuffleVector(TempVector[i], TempVector[(i + 1) % 3],
                                         VPAlign[2]);

  // TransposedMatrix[0] = a0 b0 c0 a1 b1 c1 a2 b2
  // TransposedMatrix[1] = c2 a3 b3 c3 a4 b4 c4 a5
  // TransposedMatrix[2] = b5 c5 a6 b6 c6 a7 b7 c7

  unsigned NumOfElm = VT.getVectorNumElements();
  group2Shuffle(VT, GroupSize, VPShuf);
  reorderSubVector(VT, TransposedMatrix, Vec, VPShuf, NumOfElm, 3, Builder);
}

void X86InterleavedAccessGroup::transpose_4x4(
    ArrayRef<Instruction *> Matrix,
    SmallVectorImpl<Value *> &TransposedMatrix) {
  assert(Matrix.size() == 4 && "Invalid matrix size");
  TransposedMatrix.resize(4);

  // dst = src1[0,1],src2[0,1]
  static constexpr int IntMask1[] = {0, 1, 4, 5};
  ArrayRef<int> Mask = ArrayRef(IntMask1, 4);
  Value *IntrVec1 = Builder.CreateShuffleVector(Matrix[0], Matrix[2], Mask);
  Value *IntrVec2 = Builder.CreateShuffleVector(Matrix[1], Matrix[3], Mask);

  // dst = src1[2,3],src2[2,3]
  static constexpr int IntMask2[] = {2, 3, 6, 7};
  Mask = ArrayRef(IntMask2, 4);
  Value *IntrVec3 = Builder.CreateShuffleVector(Matrix[0], Matrix[2], Mask);
  Value *IntrVec4 = Builder.CreateShuffleVector(Matrix[1], Matrix[3], Mask);

  // dst = src1[0],src2[0],src1[2],src2[2]
  static constexpr int IntMask3[] = {0, 4, 2, 6};
  Mask = ArrayRef(IntMask3, 4);
  TransposedMatrix[0] = Builder.CreateShuffleVector(IntrVec1, IntrVec2, Mask);
  TransposedMatrix[2] = Builder.CreateShuffleVector(IntrVec3, IntrVec4, Mask);

  // dst = src1[1],src2[1],src1[3],src2[3]
  static constexpr int IntMask4[] = {1, 5, 3, 7};
  Mask = ArrayRef(IntMask4, 4);
  TransposedMatrix[1] = Builder.CreateShuffleVector(IntrVec1, IntrVec2, Mask);
  TransposedMatrix[3] = Builder.CreateShuffleVector(IntrVec3, IntrVec4, Mask);
}

// Lowers this interleaved access group into X86-specific
// instructions/intrinsics.
bool X86InterleavedAccessGroup::lowerIntoOptimizedSequence() {
  SmallVector<Instruction *, 4> DecomposedVectors;
  SmallVector<Value *, 4> TransposedVectors;
  auto *ShuffleTy = cast<FixedVectorType>(Shuffles[0]->getType());

  if (isa<LoadInst>(Inst)) {
    auto *ShuffleEltTy = cast<FixedVectorType>(Inst->getType());
    unsigned NumSubVecElems = ShuffleEltTy->getNumElements() / Factor;
    switch (NumSubVecElems) {
    default:
      return false;
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
      if (ShuffleTy->getNumElements() != NumSubVecElems)
        return false;
      break;
    }

    // Try to generate target-sized register(/instruction).
    decompose(Inst, Factor, ShuffleTy, DecomposedVectors);

    // Perform matrix-transposition in order to compute interleaved
    // results by generating some sort of (optimized) target-specific
    // instructions.

    if (NumSubVecElems == 4)
      transpose_4x4(DecomposedVectors, TransposedVectors);
    else
      deinterleave8bitStride3(DecomposedVectors, TransposedVectors,
                              NumSubVecElems);

    // Now replace the unoptimized-interleaved-vectors with the
    // transposed-interleaved vectors.
    for (unsigned i = 0, e = Shuffles.size(); i < e; ++i)
      Shuffles[i]->replaceAllUsesWith(TransposedVectors[Indices[i]]);

    return true;
  }

  Type *ShuffleEltTy = ShuffleTy->getElementType();
  unsigned NumSubVecElems = ShuffleTy->getNumElements() / Factor;

  // Lower the interleaved stores:
  //   1. Decompose the interleaved wide shuffle into individual shuffle
  //   vectors.
  decompose(Shuffles[0], Factor,
            FixedVectorType::get(ShuffleEltTy, NumSubVecElems),
            DecomposedVectors);

  //   2. Transpose the interleaved-vectors into vectors of contiguous
  //      elements.
  switch (NumSubVecElems) {
  case 4:
    transpose_4x4(DecomposedVectors, TransposedVectors);
    break;
  case 8:
    interleave8bitStride4VF8(DecomposedVectors, TransposedVectors);
    break;
  case 16:
  case 32:
  case 64:
    if (Factor == 4)
      interleave8bitStride4(DecomposedVectors, TransposedVectors,
                            NumSubVecElems);
    if (Factor == 3)
      interleave8bitStride3(DecomposedVectors, TransposedVectors,
                            NumSubVecElems);
    break;
  default:
    return false;
  }

  //   3. Concatenate the contiguous-vectors back into a wide vector.
  Value *WideVec = concatenateVectors(Builder, TransposedVectors);

  //   4. Generate a store instruction for wide-vec.
  StoreInst *SI = cast<StoreInst>(Inst);
  Builder.CreateAlignedStore(WideVec, SI->getPointerOperand(), SI->getAlign());

  return true;
}

// Lower interleaved load(s) into target specific instructions/
// intrinsics. Lowering sequence varies depending on the vector-types, factor,
// number of shuffles and ISA.
// Currently, lowering is supported for 4x64 bits with Factor = 4 on AVX.
bool X86TargetLowering::lowerInterleavedLoad(
    LoadInst *LI, ArrayRef<ShuffleVectorInst *> Shuffles,
    ArrayRef<unsigned> Indices, unsigned Factor) const {
  assert(Factor >= 2 && Factor <= getMaxSupportedInterleaveFactor() &&
         "Invalid interleave factor");
  assert(!Shuffles.empty() && "Empty shufflevector input");
  assert(Shuffles.size() == Indices.size() &&
         "Unmatched number of shufflevectors and indices");

  // Create an interleaved access group.
  IRBuilder<> Builder(LI);
  X86InterleavedAccessGroup Grp(LI, Shuffles, Indices, Factor, Subtarget,
                                Builder);

  return Grp.isSupported() && Grp.lowerIntoOptimizedSequence();
}

bool X86TargetLowering::lowerInterleavedStore(StoreInst *SI,
                                              ShuffleVectorInst *SVI,
                                              unsigned Factor) const {
  assert(Factor >= 2 && Factor <= getMaxSupportedInterleaveFactor() &&
         "Invalid interleave factor");

  assert(cast<FixedVectorType>(SVI->getType())->getNumElements() % Factor ==
             0 &&
         "Invalid interleaved store");

  // Holds the indices of SVI that correspond to the starting index of each
  // interleaved shuffle.
  SmallVector<unsigned, 4> Indices;
  auto Mask = SVI->getShuffleMask();
  for (unsigned i = 0; i < Factor; i++)
    Indices.push_back(Mask[i]);

  ArrayRef<ShuffleVectorInst *> Shuffles = ArrayRef(SVI);

  // Create an interleaved access group.
  IRBuilder<> Builder(SI);
  X86InterleavedAccessGroup Grp(SI, Shuffles, Indices, Factor, Subtarget,
                                Builder);

  return Grp.isSupported() && Grp.lowerIntoOptimizedSequence();
}
