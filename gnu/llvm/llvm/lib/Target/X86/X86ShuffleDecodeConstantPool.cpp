//===-- X86ShuffleDecodeConstantPool.cpp - X86 shuffle decode -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Define several functions to decode x86 specific shuffle semantics using
// constants from the constant pool.
//
//===----------------------------------------------------------------------===//

#include "X86ShuffleDecodeConstantPool.h"
#include "MCTargetDesc/X86ShuffleDecode.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"

//===----------------------------------------------------------------------===//
//  Vector Mask Decoding
//===----------------------------------------------------------------------===//

namespace llvm {

static bool extractConstantMask(const Constant *C, unsigned MaskEltSizeInBits,
                                APInt &UndefElts,
                                SmallVectorImpl<uint64_t> &RawMask) {
  // It is not an error for shuffle masks to not be a vector of
  // MaskEltSizeInBits because the constant pool uniques constants by their
  // bit representation.
  // e.g. the following take up the same space in the constant pool:
  //   i128 -170141183420855150465331762880109871104
  //
  //   <2 x i64> <i64 -9223372034707292160, i64 -9223372034707292160>
  //
  //   <4 x i32> <i32 -2147483648, i32 -2147483648,
  //              i32 -2147483648, i32 -2147483648>
  auto *CstTy = dyn_cast<FixedVectorType>(C->getType());
  if (!CstTy)
    return false;

  Type *CstEltTy = CstTy->getElementType();
  if (!CstEltTy->isIntegerTy())
    return false;

  unsigned CstSizeInBits = CstTy->getPrimitiveSizeInBits();
  unsigned CstEltSizeInBits = CstTy->getScalarSizeInBits();
  unsigned NumCstElts = CstTy->getNumElements();

  assert((CstSizeInBits % MaskEltSizeInBits) == 0 &&
         "Unaligned shuffle mask size");

  unsigned NumMaskElts = CstSizeInBits / MaskEltSizeInBits;
  UndefElts = APInt(NumMaskElts, 0);
  RawMask.resize(NumMaskElts, 0);

  // Fast path - if the constants match the mask size then copy direct.
  if (MaskEltSizeInBits == CstEltSizeInBits) {
    assert(NumCstElts == NumMaskElts && "Unaligned shuffle mask size");
    for (unsigned i = 0; i != NumMaskElts; ++i) {
      Constant *COp = C->getAggregateElement(i);
      if (!COp || (!isa<UndefValue>(COp) && !isa<ConstantInt>(COp)))
        return false;

      if (isa<UndefValue>(COp)) {
        UndefElts.setBit(i);
        RawMask[i] = 0;
        continue;
      }

      auto *Elt = cast<ConstantInt>(COp);
      RawMask[i] = Elt->getValue().getZExtValue();
    }
    return true;
  }

  // Extract all the undef/constant element data and pack into single bitsets.
  APInt UndefBits(CstSizeInBits, 0);
  APInt MaskBits(CstSizeInBits, 0);
  for (unsigned i = 0; i != NumCstElts; ++i) {
    Constant *COp = C->getAggregateElement(i);
    if (!COp || (!isa<UndefValue>(COp) && !isa<ConstantInt>(COp)))
      return false;

    unsigned BitOffset = i * CstEltSizeInBits;

    if (isa<UndefValue>(COp)) {
      UndefBits.setBits(BitOffset, BitOffset + CstEltSizeInBits);
      continue;
    }

    MaskBits.insertBits(cast<ConstantInt>(COp)->getValue(), BitOffset);
  }

  // Now extract the undef/constant bit data into the raw shuffle masks.
  for (unsigned i = 0; i != NumMaskElts; ++i) {
    unsigned BitOffset = i * MaskEltSizeInBits;
    APInt EltUndef = UndefBits.extractBits(MaskEltSizeInBits, BitOffset);

    // Only treat the element as UNDEF if all bits are UNDEF, otherwise
    // treat it as zero.
    if (EltUndef.isAllOnes()) {
      UndefElts.setBit(i);
      RawMask[i] = 0;
      continue;
    }

    APInt EltBits = MaskBits.extractBits(MaskEltSizeInBits, BitOffset);
    RawMask[i] = EltBits.getZExtValue();
  }

  return true;
}

void DecodePSHUFBMask(const Constant *C, unsigned Width,
                      SmallVectorImpl<int> &ShuffleMask) {
  assert((Width == 128 || Width == 256 || Width == 512) &&
         C->getType()->getPrimitiveSizeInBits() >= Width &&
         "Unexpected vector size.");

  // The shuffle mask requires a byte vector.
  APInt UndefElts;
  SmallVector<uint64_t, 64> RawMask;
  if (!extractConstantMask(C, 8, UndefElts, RawMask))
    return;

  unsigned NumElts = Width / 8;
  assert((NumElts == 16 || NumElts == 32 || NumElts == 64) &&
         "Unexpected number of vector elements.");

  for (unsigned i = 0; i != NumElts; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }

    uint64_t Element = RawMask[i];
    // If the high bit (7) of the byte is set, the element is zeroed.
    if (Element & (1 << 7))
      ShuffleMask.push_back(SM_SentinelZero);
    else {
      // For AVX vectors with 32 bytes the base of the shuffle is the 16-byte
      // lane of the vector we're inside.
      unsigned Base = i & ~0xf;

      // Only the least significant 4 bits of the byte are used.
      int Index = Base + (Element & 0xf);
      ShuffleMask.push_back(Index);
    }
  }
}

void DecodeVPERMILPMask(const Constant *C, unsigned ElSize, unsigned Width,
                        SmallVectorImpl<int> &ShuffleMask) {
  assert((Width == 128 || Width == 256 || Width == 512) &&
         C->getType()->getPrimitiveSizeInBits() >= Width &&
         "Unexpected vector size.");
  assert((ElSize == 32 || ElSize == 64) && "Unexpected vector element size.");

  // The shuffle mask requires elements the same size as the target.
  APInt UndefElts;
  SmallVector<uint64_t, 16> RawMask;
  if (!extractConstantMask(C, ElSize, UndefElts, RawMask))
    return;

  unsigned NumElts = Width / ElSize;
  unsigned NumEltsPerLane = 128 / ElSize;
  assert((NumElts == 2 || NumElts == 4 || NumElts == 8 || NumElts == 16) &&
         "Unexpected number of vector elements.");

  for (unsigned i = 0; i != NumElts; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }

    int Index = i & ~(NumEltsPerLane - 1);
    uint64_t Element = RawMask[i];
    if (ElSize == 64)
      Index += (Element >> 1) & 0x1;
    else
      Index += Element & 0x3;

    ShuffleMask.push_back(Index);
  }
}

void DecodeVPERMIL2PMask(const Constant *C, unsigned M2Z, unsigned ElSize,
                         unsigned Width, SmallVectorImpl<int> &ShuffleMask) {
  Type *MaskTy = C->getType();
  unsigned MaskTySize = MaskTy->getPrimitiveSizeInBits();
  (void)MaskTySize;
  assert((MaskTySize == 128 || MaskTySize == 256) && Width >= MaskTySize &&
         "Unexpected vector size.");

  // The shuffle mask requires elements the same size as the target.
  APInt UndefElts;
  SmallVector<uint64_t, 8> RawMask;
  if (!extractConstantMask(C, ElSize, UndefElts, RawMask))
    return;

  unsigned NumElts = Width / ElSize;
  unsigned NumEltsPerLane = 128 / ElSize;
  assert((NumElts == 2 || NumElts == 4 || NumElts == 8) &&
         "Unexpected number of vector elements.");

  for (unsigned i = 0; i != NumElts; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }

    // VPERMIL2 Operation.
    // Bits[3] - Match Bit.
    // Bits[2:1] - (Per Lane) PD Shuffle Mask.
    // Bits[2:0] - (Per Lane) PS Shuffle Mask.
    uint64_t Selector = RawMask[i];
    unsigned MatchBit = (Selector >> 3) & 0x1;

    // M2Z[0:1]     MatchBit
    //   0Xb           X        Source selected by Selector index.
    //   10b           0        Source selected by Selector index.
    //   10b           1        Zero.
    //   11b           0        Zero.
    //   11b           1        Source selected by Selector index.
    if ((M2Z & 0x2) != 0u && MatchBit != (M2Z & 0x1)) {
      ShuffleMask.push_back(SM_SentinelZero);
      continue;
    }

    int Index = i & ~(NumEltsPerLane - 1);
    if (ElSize == 64)
      Index += (Selector >> 1) & 0x1;
    else
      Index += Selector & 0x3;

    int Src = (Selector >> 2) & 0x1;
    Index += Src * NumElts;
    ShuffleMask.push_back(Index);
  }
}

void DecodeVPPERMMask(const Constant *C, unsigned Width,
                      SmallVectorImpl<int> &ShuffleMask) {
  Type *MaskTy = C->getType();
  unsigned MaskTySize = MaskTy->getPrimitiveSizeInBits();
  (void)MaskTySize;
  assert(Width == 128 && Width >= MaskTySize && "Unexpected vector size.");

  // The shuffle mask requires a byte vector.
  APInt UndefElts;
  SmallVector<uint64_t, 16> RawMask;
  if (!extractConstantMask(C, 8, UndefElts, RawMask))
    return;

  unsigned NumElts = Width / 8;
  assert(NumElts == 16 && "Unexpected number of vector elements.");

  for (unsigned i = 0; i != NumElts; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }

    // VPPERM Operation
    // Bits[4:0] - Byte Index (0 - 31)
    // Bits[7:5] - Permute Operation
    //
    // Permute Operation:
    // 0 - Source byte (no logical operation).
    // 1 - Invert source byte.
    // 2 - Bit reverse of source byte.
    // 3 - Bit reverse of inverted source byte.
    // 4 - 00h (zero - fill).
    // 5 - FFh (ones - fill).
    // 6 - Most significant bit of source byte replicated in all bit positions.
    // 7 - Invert most significant bit of source byte and replicate in all bit
    // positions.
    uint64_t Element = RawMask[i];
    uint64_t Index = Element & 0x1F;
    uint64_t PermuteOp = (Element >> 5) & 0x7;

    if (PermuteOp == 4) {
      ShuffleMask.push_back(SM_SentinelZero);
      continue;
    }
    if (PermuteOp != 0) {
      ShuffleMask.clear();
      return;
    }
    ShuffleMask.push_back((int)Index);
  }
}

} // namespace llvm
