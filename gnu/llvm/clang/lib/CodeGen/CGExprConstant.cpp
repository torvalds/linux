//===--- CGExprConstant.cpp - Emit LLVM Code from Constant Expressions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Constant Expr nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "CGCXXABI.h"
#include "CGObjCRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include <optional>
using namespace clang;
using namespace CodeGen;

//===----------------------------------------------------------------------===//
//                            ConstantAggregateBuilder
//===----------------------------------------------------------------------===//

namespace {
class ConstExprEmitter;

struct ConstantAggregateBuilderUtils {
  CodeGenModule &CGM;

  ConstantAggregateBuilderUtils(CodeGenModule &CGM) : CGM(CGM) {}

  CharUnits getAlignment(const llvm::Constant *C) const {
    return CharUnits::fromQuantity(
        CGM.getDataLayout().getABITypeAlign(C->getType()));
  }

  CharUnits getSize(llvm::Type *Ty) const {
    return CharUnits::fromQuantity(CGM.getDataLayout().getTypeAllocSize(Ty));
  }

  CharUnits getSize(const llvm::Constant *C) const {
    return getSize(C->getType());
  }

  llvm::Constant *getPadding(CharUnits PadSize) const {
    llvm::Type *Ty = CGM.CharTy;
    if (PadSize > CharUnits::One())
      Ty = llvm::ArrayType::get(Ty, PadSize.getQuantity());
    return llvm::UndefValue::get(Ty);
  }

  llvm::Constant *getZeroes(CharUnits ZeroSize) const {
    llvm::Type *Ty = llvm::ArrayType::get(CGM.CharTy, ZeroSize.getQuantity());
    return llvm::ConstantAggregateZero::get(Ty);
  }
};

/// Incremental builder for an llvm::Constant* holding a struct or array
/// constant.
class ConstantAggregateBuilder : private ConstantAggregateBuilderUtils {
  /// The elements of the constant. These two arrays must have the same size;
  /// Offsets[i] describes the offset of Elems[i] within the constant. The
  /// elements are kept in increasing offset order, and we ensure that there
  /// is no overlap: Offsets[i+1] >= Offsets[i] + getSize(Elemes[i]).
  ///
  /// This may contain explicit padding elements (in order to create a
  /// natural layout), but need not. Gaps between elements are implicitly
  /// considered to be filled with undef.
  llvm::SmallVector<llvm::Constant*, 32> Elems;
  llvm::SmallVector<CharUnits, 32> Offsets;

  /// The size of the constant (the maximum end offset of any added element).
  /// May be larger than the end of Elems.back() if we split the last element
  /// and removed some trailing undefs.
  CharUnits Size = CharUnits::Zero();

  /// This is true only if laying out Elems in order as the elements of a
  /// non-packed LLVM struct will give the correct layout.
  bool NaturalLayout = true;

  bool split(size_t Index, CharUnits Hint);
  std::optional<size_t> splitAt(CharUnits Pos);

  static llvm::Constant *buildFrom(CodeGenModule &CGM,
                                   ArrayRef<llvm::Constant *> Elems,
                                   ArrayRef<CharUnits> Offsets,
                                   CharUnits StartOffset, CharUnits Size,
                                   bool NaturalLayout, llvm::Type *DesiredTy,
                                   bool AllowOversized);

public:
  ConstantAggregateBuilder(CodeGenModule &CGM)
      : ConstantAggregateBuilderUtils(CGM) {}

  /// Update or overwrite the value starting at \p Offset with \c C.
  ///
  /// \param AllowOverwrite If \c true, this constant might overwrite (part of)
  ///        a constant that has already been added. This flag is only used to
  ///        detect bugs.
  bool add(llvm::Constant *C, CharUnits Offset, bool AllowOverwrite);

  /// Update or overwrite the bits starting at \p OffsetInBits with \p Bits.
  bool addBits(llvm::APInt Bits, uint64_t OffsetInBits, bool AllowOverwrite);

  /// Attempt to condense the value starting at \p Offset to a constant of type
  /// \p DesiredTy.
  void condense(CharUnits Offset, llvm::Type *DesiredTy);

  /// Produce a constant representing the entire accumulated value, ideally of
  /// the specified type. If \p AllowOversized, the constant might be larger
  /// than implied by \p DesiredTy (eg, if there is a flexible array member).
  /// Otherwise, the constant will be of exactly the same size as \p DesiredTy
  /// even if we can't represent it as that type.
  llvm::Constant *build(llvm::Type *DesiredTy, bool AllowOversized) const {
    return buildFrom(CGM, Elems, Offsets, CharUnits::Zero(), Size,
                     NaturalLayout, DesiredTy, AllowOversized);
  }
};

template<typename Container, typename Range = std::initializer_list<
                                 typename Container::value_type>>
static void replace(Container &C, size_t BeginOff, size_t EndOff, Range Vals) {
  assert(BeginOff <= EndOff && "invalid replacement range");
  llvm::replace(C, C.begin() + BeginOff, C.begin() + EndOff, Vals);
}

bool ConstantAggregateBuilder::add(llvm::Constant *C, CharUnits Offset,
                          bool AllowOverwrite) {
  // Common case: appending to a layout.
  if (Offset >= Size) {
    CharUnits Align = getAlignment(C);
    CharUnits AlignedSize = Size.alignTo(Align);
    if (AlignedSize > Offset || Offset.alignTo(Align) != Offset)
      NaturalLayout = false;
    else if (AlignedSize < Offset) {
      Elems.push_back(getPadding(Offset - Size));
      Offsets.push_back(Size);
    }
    Elems.push_back(C);
    Offsets.push_back(Offset);
    Size = Offset + getSize(C);
    return true;
  }

  // Uncommon case: constant overlaps what we've already created.
  std::optional<size_t> FirstElemToReplace = splitAt(Offset);
  if (!FirstElemToReplace)
    return false;

  CharUnits CSize = getSize(C);
  std::optional<size_t> LastElemToReplace = splitAt(Offset + CSize);
  if (!LastElemToReplace)
    return false;

  assert((FirstElemToReplace == LastElemToReplace || AllowOverwrite) &&
         "unexpectedly overwriting field");

  replace(Elems, *FirstElemToReplace, *LastElemToReplace, {C});
  replace(Offsets, *FirstElemToReplace, *LastElemToReplace, {Offset});
  Size = std::max(Size, Offset + CSize);
  NaturalLayout = false;
  return true;
}

bool ConstantAggregateBuilder::addBits(llvm::APInt Bits, uint64_t OffsetInBits,
                              bool AllowOverwrite) {
  const ASTContext &Context = CGM.getContext();
  const uint64_t CharWidth = CGM.getContext().getCharWidth();

  // Offset of where we want the first bit to go within the bits of the
  // current char.
  unsigned OffsetWithinChar = OffsetInBits % CharWidth;

  // We split bit-fields up into individual bytes. Walk over the bytes and
  // update them.
  for (CharUnits OffsetInChars =
           Context.toCharUnitsFromBits(OffsetInBits - OffsetWithinChar);
       /**/; ++OffsetInChars) {
    // Number of bits we want to fill in this char.
    unsigned WantedBits =
        std::min((uint64_t)Bits.getBitWidth(), CharWidth - OffsetWithinChar);

    // Get a char containing the bits we want in the right places. The other
    // bits have unspecified values.
    llvm::APInt BitsThisChar = Bits;
    if (BitsThisChar.getBitWidth() < CharWidth)
      BitsThisChar = BitsThisChar.zext(CharWidth);
    if (CGM.getDataLayout().isBigEndian()) {
      // Figure out how much to shift by. We may need to left-shift if we have
      // less than one byte of Bits left.
      int Shift = Bits.getBitWidth() - CharWidth + OffsetWithinChar;
      if (Shift > 0)
        BitsThisChar.lshrInPlace(Shift);
      else if (Shift < 0)
        BitsThisChar = BitsThisChar.shl(-Shift);
    } else {
      BitsThisChar = BitsThisChar.shl(OffsetWithinChar);
    }
    if (BitsThisChar.getBitWidth() > CharWidth)
      BitsThisChar = BitsThisChar.trunc(CharWidth);

    if (WantedBits == CharWidth) {
      // Got a full byte: just add it directly.
      add(llvm::ConstantInt::get(CGM.getLLVMContext(), BitsThisChar),
          OffsetInChars, AllowOverwrite);
    } else {
      // Partial byte: update the existing integer if there is one. If we
      // can't split out a 1-CharUnit range to update, then we can't add
      // these bits and fail the entire constant emission.
      std::optional<size_t> FirstElemToUpdate = splitAt(OffsetInChars);
      if (!FirstElemToUpdate)
        return false;
      std::optional<size_t> LastElemToUpdate =
          splitAt(OffsetInChars + CharUnits::One());
      if (!LastElemToUpdate)
        return false;
      assert(*LastElemToUpdate - *FirstElemToUpdate < 2 &&
             "should have at most one element covering one byte");

      // Figure out which bits we want and discard the rest.
      llvm::APInt UpdateMask(CharWidth, 0);
      if (CGM.getDataLayout().isBigEndian())
        UpdateMask.setBits(CharWidth - OffsetWithinChar - WantedBits,
                           CharWidth - OffsetWithinChar);
      else
        UpdateMask.setBits(OffsetWithinChar, OffsetWithinChar + WantedBits);
      BitsThisChar &= UpdateMask;

      if (*FirstElemToUpdate == *LastElemToUpdate ||
          Elems[*FirstElemToUpdate]->isNullValue() ||
          isa<llvm::UndefValue>(Elems[*FirstElemToUpdate])) {
        // All existing bits are either zero or undef.
        add(llvm::ConstantInt::get(CGM.getLLVMContext(), BitsThisChar),
            OffsetInChars, /*AllowOverwrite*/ true);
      } else {
        llvm::Constant *&ToUpdate = Elems[*FirstElemToUpdate];
        // In order to perform a partial update, we need the existing bitwise
        // value, which we can only extract for a constant int.
        auto *CI = dyn_cast<llvm::ConstantInt>(ToUpdate);
        if (!CI)
          return false;
        // Because this is a 1-CharUnit range, the constant occupying it must
        // be exactly one CharUnit wide.
        assert(CI->getBitWidth() == CharWidth && "splitAt failed");
        assert((!(CI->getValue() & UpdateMask) || AllowOverwrite) &&
               "unexpectedly overwriting bitfield");
        BitsThisChar |= (CI->getValue() & ~UpdateMask);
        ToUpdate = llvm::ConstantInt::get(CGM.getLLVMContext(), BitsThisChar);
      }
    }

    // Stop if we've added all the bits.
    if (WantedBits == Bits.getBitWidth())
      break;

    // Remove the consumed bits from Bits.
    if (!CGM.getDataLayout().isBigEndian())
      Bits.lshrInPlace(WantedBits);
    Bits = Bits.trunc(Bits.getBitWidth() - WantedBits);

    // The remanining bits go at the start of the following bytes.
    OffsetWithinChar = 0;
  }

  return true;
}

/// Returns a position within Elems and Offsets such that all elements
/// before the returned index end before Pos and all elements at or after
/// the returned index begin at or after Pos. Splits elements as necessary
/// to ensure this. Returns std::nullopt if we find something we can't split.
std::optional<size_t> ConstantAggregateBuilder::splitAt(CharUnits Pos) {
  if (Pos >= Size)
    return Offsets.size();

  while (true) {
    auto FirstAfterPos = llvm::upper_bound(Offsets, Pos);
    if (FirstAfterPos == Offsets.begin())
      return 0;

    // If we already have an element starting at Pos, we're done.
    size_t LastAtOrBeforePosIndex = FirstAfterPos - Offsets.begin() - 1;
    if (Offsets[LastAtOrBeforePosIndex] == Pos)
      return LastAtOrBeforePosIndex;

    // We found an element starting before Pos. Check for overlap.
    if (Offsets[LastAtOrBeforePosIndex] +
        getSize(Elems[LastAtOrBeforePosIndex]) <= Pos)
      return LastAtOrBeforePosIndex + 1;

    // Try to decompose it into smaller constants.
    if (!split(LastAtOrBeforePosIndex, Pos))
      return std::nullopt;
  }
}

/// Split the constant at index Index, if possible. Return true if we did.
/// Hint indicates the location at which we'd like to split, but may be
/// ignored.
bool ConstantAggregateBuilder::split(size_t Index, CharUnits Hint) {
  NaturalLayout = false;
  llvm::Constant *C = Elems[Index];
  CharUnits Offset = Offsets[Index];

  if (auto *CA = dyn_cast<llvm::ConstantAggregate>(C)) {
    // Expand the sequence into its contained elements.
    // FIXME: This assumes vector elements are byte-sized.
    replace(Elems, Index, Index + 1,
            llvm::map_range(llvm::seq(0u, CA->getNumOperands()),
                            [&](unsigned Op) { return CA->getOperand(Op); }));
    if (isa<llvm::ArrayType>(CA->getType()) ||
        isa<llvm::VectorType>(CA->getType())) {
      // Array or vector.
      llvm::Type *ElemTy =
          llvm::GetElementPtrInst::getTypeAtIndex(CA->getType(), (uint64_t)0);
      CharUnits ElemSize = getSize(ElemTy);
      replace(
          Offsets, Index, Index + 1,
          llvm::map_range(llvm::seq(0u, CA->getNumOperands()),
                          [&](unsigned Op) { return Offset + Op * ElemSize; }));
    } else {
      // Must be a struct.
      auto *ST = cast<llvm::StructType>(CA->getType());
      const llvm::StructLayout *Layout =
          CGM.getDataLayout().getStructLayout(ST);
      replace(Offsets, Index, Index + 1,
              llvm::map_range(
                  llvm::seq(0u, CA->getNumOperands()), [&](unsigned Op) {
                    return Offset + CharUnits::fromQuantity(
                                        Layout->getElementOffset(Op));
                  }));
    }
    return true;
  }

  if (auto *CDS = dyn_cast<llvm::ConstantDataSequential>(C)) {
    // Expand the sequence into its contained elements.
    // FIXME: This assumes vector elements are byte-sized.
    // FIXME: If possible, split into two ConstantDataSequentials at Hint.
    CharUnits ElemSize = getSize(CDS->getElementType());
    replace(Elems, Index, Index + 1,
            llvm::map_range(llvm::seq(0u, CDS->getNumElements()),
                            [&](unsigned Elem) {
                              return CDS->getElementAsConstant(Elem);
                            }));
    replace(Offsets, Index, Index + 1,
            llvm::map_range(
                llvm::seq(0u, CDS->getNumElements()),
                [&](unsigned Elem) { return Offset + Elem * ElemSize; }));
    return true;
  }

  if (isa<llvm::ConstantAggregateZero>(C)) {
    // Split into two zeros at the hinted offset.
    CharUnits ElemSize = getSize(C);
    assert(Hint > Offset && Hint < Offset + ElemSize && "nothing to split");
    replace(Elems, Index, Index + 1,
            {getZeroes(Hint - Offset), getZeroes(Offset + ElemSize - Hint)});
    replace(Offsets, Index, Index + 1, {Offset, Hint});
    return true;
  }

  if (isa<llvm::UndefValue>(C)) {
    // Drop undef; it doesn't contribute to the final layout.
    replace(Elems, Index, Index + 1, {});
    replace(Offsets, Index, Index + 1, {});
    return true;
  }

  // FIXME: We could split a ConstantInt if the need ever arose.
  // We don't need to do this to handle bit-fields because we always eagerly
  // split them into 1-byte chunks.

  return false;
}

static llvm::Constant *
EmitArrayConstant(CodeGenModule &CGM, llvm::ArrayType *DesiredType,
                  llvm::Type *CommonElementType, uint64_t ArrayBound,
                  SmallVectorImpl<llvm::Constant *> &Elements,
                  llvm::Constant *Filler);

llvm::Constant *ConstantAggregateBuilder::buildFrom(
    CodeGenModule &CGM, ArrayRef<llvm::Constant *> Elems,
    ArrayRef<CharUnits> Offsets, CharUnits StartOffset, CharUnits Size,
    bool NaturalLayout, llvm::Type *DesiredTy, bool AllowOversized) {
  ConstantAggregateBuilderUtils Utils(CGM);

  if (Elems.empty())
    return llvm::UndefValue::get(DesiredTy);

  auto Offset = [&](size_t I) { return Offsets[I] - StartOffset; };

  // If we want an array type, see if all the elements are the same type and
  // appropriately spaced.
  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(DesiredTy)) {
    assert(!AllowOversized && "oversized array emission not supported");

    bool CanEmitArray = true;
    llvm::Type *CommonType = Elems[0]->getType();
    llvm::Constant *Filler = llvm::Constant::getNullValue(CommonType);
    CharUnits ElemSize = Utils.getSize(ATy->getElementType());
    SmallVector<llvm::Constant*, 32> ArrayElements;
    for (size_t I = 0; I != Elems.size(); ++I) {
      // Skip zeroes; we'll use a zero value as our array filler.
      if (Elems[I]->isNullValue())
        continue;

      // All remaining elements must be the same type.
      if (Elems[I]->getType() != CommonType ||
          Offset(I) % ElemSize != 0) {
        CanEmitArray = false;
        break;
      }
      ArrayElements.resize(Offset(I) / ElemSize + 1, Filler);
      ArrayElements.back() = Elems[I];
    }

    if (CanEmitArray) {
      return EmitArrayConstant(CGM, ATy, CommonType, ATy->getNumElements(),
                               ArrayElements, Filler);
    }

    // Can't emit as an array, carry on to emit as a struct.
  }

  // The size of the constant we plan to generate.  This is usually just
  // the size of the initialized type, but in AllowOversized mode (i.e.
  // flexible array init), it can be larger.
  CharUnits DesiredSize = Utils.getSize(DesiredTy);
  if (Size > DesiredSize) {
    assert(AllowOversized && "Elems are oversized");
    DesiredSize = Size;
  }

  // The natural alignment of an unpacked LLVM struct with the given elements.
  CharUnits Align = CharUnits::One();
  for (llvm::Constant *C : Elems)
    Align = std::max(Align, Utils.getAlignment(C));

  // The natural size of an unpacked LLVM struct with the given elements.
  CharUnits AlignedSize = Size.alignTo(Align);

  bool Packed = false;
  ArrayRef<llvm::Constant*> UnpackedElems = Elems;
  llvm::SmallVector<llvm::Constant*, 32> UnpackedElemStorage;
  if (DesiredSize < AlignedSize || DesiredSize.alignTo(Align) != DesiredSize) {
    // The natural layout would be too big; force use of a packed layout.
    NaturalLayout = false;
    Packed = true;
  } else if (DesiredSize > AlignedSize) {
    // The natural layout would be too small. Add padding to fix it. (This
    // is ignored if we choose a packed layout.)
    UnpackedElemStorage.assign(Elems.begin(), Elems.end());
    UnpackedElemStorage.push_back(Utils.getPadding(DesiredSize - Size));
    UnpackedElems = UnpackedElemStorage;
  }

  // If we don't have a natural layout, insert padding as necessary.
  // As we go, double-check to see if we can actually just emit Elems
  // as a non-packed struct and do so opportunistically if possible.
  llvm::SmallVector<llvm::Constant*, 32> PackedElems;
  if (!NaturalLayout) {
    CharUnits SizeSoFar = CharUnits::Zero();
    for (size_t I = 0; I != Elems.size(); ++I) {
      CharUnits Align = Utils.getAlignment(Elems[I]);
      CharUnits NaturalOffset = SizeSoFar.alignTo(Align);
      CharUnits DesiredOffset = Offset(I);
      assert(DesiredOffset >= SizeSoFar && "elements out of order");

      if (DesiredOffset != NaturalOffset)
        Packed = true;
      if (DesiredOffset != SizeSoFar)
        PackedElems.push_back(Utils.getPadding(DesiredOffset - SizeSoFar));
      PackedElems.push_back(Elems[I]);
      SizeSoFar = DesiredOffset + Utils.getSize(Elems[I]);
    }
    // If we're using the packed layout, pad it out to the desired size if
    // necessary.
    if (Packed) {
      assert(SizeSoFar <= DesiredSize &&
             "requested size is too small for contents");
      if (SizeSoFar < DesiredSize)
        PackedElems.push_back(Utils.getPadding(DesiredSize - SizeSoFar));
    }
  }

  llvm::StructType *STy = llvm::ConstantStruct::getTypeForElements(
      CGM.getLLVMContext(), Packed ? PackedElems : UnpackedElems, Packed);

  // Pick the type to use.  If the type is layout identical to the desired
  // type then use it, otherwise use whatever the builder produced for us.
  if (llvm::StructType *DesiredSTy = dyn_cast<llvm::StructType>(DesiredTy)) {
    if (DesiredSTy->isLayoutIdentical(STy))
      STy = DesiredSTy;
  }

  return llvm::ConstantStruct::get(STy, Packed ? PackedElems : UnpackedElems);
}

void ConstantAggregateBuilder::condense(CharUnits Offset,
                                        llvm::Type *DesiredTy) {
  CharUnits Size = getSize(DesiredTy);

  std::optional<size_t> FirstElemToReplace = splitAt(Offset);
  if (!FirstElemToReplace)
    return;
  size_t First = *FirstElemToReplace;

  std::optional<size_t> LastElemToReplace = splitAt(Offset + Size);
  if (!LastElemToReplace)
    return;
  size_t Last = *LastElemToReplace;

  size_t Length = Last - First;
  if (Length == 0)
    return;

  if (Length == 1 && Offsets[First] == Offset &&
      getSize(Elems[First]) == Size) {
    // Re-wrap single element structs if necessary. Otherwise, leave any single
    // element constant of the right size alone even if it has the wrong type.
    auto *STy = dyn_cast<llvm::StructType>(DesiredTy);
    if (STy && STy->getNumElements() == 1 &&
        STy->getElementType(0) == Elems[First]->getType())
      Elems[First] = llvm::ConstantStruct::get(STy, Elems[First]);
    return;
  }

  llvm::Constant *Replacement = buildFrom(
      CGM, ArrayRef(Elems).slice(First, Length),
      ArrayRef(Offsets).slice(First, Length), Offset, getSize(DesiredTy),
      /*known to have natural layout=*/false, DesiredTy, false);
  replace(Elems, First, Last, {Replacement});
  replace(Offsets, First, Last, {Offset});
}

//===----------------------------------------------------------------------===//
//                            ConstStructBuilder
//===----------------------------------------------------------------------===//

class ConstStructBuilder {
  CodeGenModule &CGM;
  ConstantEmitter &Emitter;
  ConstantAggregateBuilder &Builder;
  CharUnits StartOffset;

public:
  static llvm::Constant *BuildStruct(ConstantEmitter &Emitter,
                                     const InitListExpr *ILE,
                                     QualType StructTy);
  static llvm::Constant *BuildStruct(ConstantEmitter &Emitter,
                                     const APValue &Value, QualType ValTy);
  static bool UpdateStruct(ConstantEmitter &Emitter,
                           ConstantAggregateBuilder &Const, CharUnits Offset,
                           const InitListExpr *Updater);

private:
  ConstStructBuilder(ConstantEmitter &Emitter,
                     ConstantAggregateBuilder &Builder, CharUnits StartOffset)
      : CGM(Emitter.CGM), Emitter(Emitter), Builder(Builder),
        StartOffset(StartOffset) {}

  bool AppendField(const FieldDecl *Field, uint64_t FieldOffset,
                   llvm::Constant *InitExpr, bool AllowOverwrite = false);

  bool AppendBytes(CharUnits FieldOffsetInChars, llvm::Constant *InitCst,
                   bool AllowOverwrite = false);

  bool AppendBitField(const FieldDecl *Field, uint64_t FieldOffset,
                      llvm::Constant *InitExpr, bool AllowOverwrite = false);

  bool Build(const InitListExpr *ILE, bool AllowOverwrite);
  bool Build(const APValue &Val, const RecordDecl *RD, bool IsPrimaryBase,
             const CXXRecordDecl *VTableClass, CharUnits BaseOffset);
  llvm::Constant *Finalize(QualType Ty);
};

bool ConstStructBuilder::AppendField(
    const FieldDecl *Field, uint64_t FieldOffset, llvm::Constant *InitCst,
    bool AllowOverwrite) {
  const ASTContext &Context = CGM.getContext();

  CharUnits FieldOffsetInChars = Context.toCharUnitsFromBits(FieldOffset);

  return AppendBytes(FieldOffsetInChars, InitCst, AllowOverwrite);
}

bool ConstStructBuilder::AppendBytes(CharUnits FieldOffsetInChars,
                                     llvm::Constant *InitCst,
                                     bool AllowOverwrite) {
  return Builder.add(InitCst, StartOffset + FieldOffsetInChars, AllowOverwrite);
}

bool ConstStructBuilder::AppendBitField(const FieldDecl *Field,
                                        uint64_t FieldOffset, llvm::Constant *C,
                                        bool AllowOverwrite) {

  llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(C);
  if (!CI) {
    // Constants for long _BitInt types are sometimes split into individual
    // bytes. Try to fold these back into an integer constant. If that doesn't
    // work out, then we are trying to initialize a bitfield with a non-trivial
    // constant, this must require run-time code.
    llvm::Type *LoadType =
        CGM.getTypes().convertTypeForLoadStore(Field->getType(), C->getType());
    llvm::Constant *FoldedConstant = llvm::ConstantFoldLoadFromConst(
        C, LoadType, llvm::APInt::getZero(32), CGM.getDataLayout());
    CI = dyn_cast_if_present<llvm::ConstantInt>(FoldedConstant);
    if (!CI)
      return false;
  }

  const CGRecordLayout &RL =
      CGM.getTypes().getCGRecordLayout(Field->getParent());
  const CGBitFieldInfo &Info = RL.getBitFieldInfo(Field);
  llvm::APInt FieldValue = CI->getValue();

  // Promote the size of FieldValue if necessary
  // FIXME: This should never occur, but currently it can because initializer
  // constants are cast to bool, and because clang is not enforcing bitfield
  // width limits.
  if (Info.Size > FieldValue.getBitWidth())
    FieldValue = FieldValue.zext(Info.Size);

  // Truncate the size of FieldValue to the bit field size.
  if (Info.Size < FieldValue.getBitWidth())
    FieldValue = FieldValue.trunc(Info.Size);

  return Builder.addBits(FieldValue,
                         CGM.getContext().toBits(StartOffset) + FieldOffset,
                         AllowOverwrite);
}

static bool EmitDesignatedInitUpdater(ConstantEmitter &Emitter,
                                      ConstantAggregateBuilder &Const,
                                      CharUnits Offset, QualType Type,
                                      const InitListExpr *Updater) {
  if (Type->isRecordType())
    return ConstStructBuilder::UpdateStruct(Emitter, Const, Offset, Updater);

  auto CAT = Emitter.CGM.getContext().getAsConstantArrayType(Type);
  if (!CAT)
    return false;
  QualType ElemType = CAT->getElementType();
  CharUnits ElemSize = Emitter.CGM.getContext().getTypeSizeInChars(ElemType);
  llvm::Type *ElemTy = Emitter.CGM.getTypes().ConvertTypeForMem(ElemType);

  llvm::Constant *FillC = nullptr;
  if (const Expr *Filler = Updater->getArrayFiller()) {
    if (!isa<NoInitExpr>(Filler)) {
      FillC = Emitter.tryEmitAbstractForMemory(Filler, ElemType);
      if (!FillC)
        return false;
    }
  }

  unsigned NumElementsToUpdate =
      FillC ? CAT->getZExtSize() : Updater->getNumInits();
  for (unsigned I = 0; I != NumElementsToUpdate; ++I, Offset += ElemSize) {
    const Expr *Init = nullptr;
    if (I < Updater->getNumInits())
      Init = Updater->getInit(I);

    if (!Init && FillC) {
      if (!Const.add(FillC, Offset, true))
        return false;
    } else if (!Init || isa<NoInitExpr>(Init)) {
      continue;
    } else if (const auto *ChildILE = dyn_cast<InitListExpr>(Init)) {
      if (!EmitDesignatedInitUpdater(Emitter, Const, Offset, ElemType,
                                     ChildILE))
        return false;
      // Attempt to reduce the array element to a single constant if necessary.
      Const.condense(Offset, ElemTy);
    } else {
      llvm::Constant *Val = Emitter.tryEmitPrivateForMemory(Init, ElemType);
      if (!Const.add(Val, Offset, true))
        return false;
    }
  }

  return true;
}

bool ConstStructBuilder::Build(const InitListExpr *ILE, bool AllowOverwrite) {
  RecordDecl *RD = ILE->getType()->castAs<RecordType>()->getDecl();
  const ASTRecordLayout &Layout = CGM.getContext().getASTRecordLayout(RD);

  unsigned FieldNo = -1;
  unsigned ElementNo = 0;

  // Bail out if we have base classes. We could support these, but they only
  // arise in C++1z where we will have already constant folded most interesting
  // cases. FIXME: There are still a few more cases we can handle this way.
  if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    if (CXXRD->getNumBases())
      return false;

  for (FieldDecl *Field : RD->fields()) {
    ++FieldNo;

    // If this is a union, skip all the fields that aren't being initialized.
    if (RD->isUnion() &&
        !declaresSameEntity(ILE->getInitializedFieldInUnion(), Field))
      continue;

    // Don't emit anonymous bitfields.
    if (Field->isUnnamedBitField())
      continue;

    // Get the initializer.  A struct can include fields without initializers,
    // we just use explicit null values for them.
    const Expr *Init = nullptr;
    if (ElementNo < ILE->getNumInits())
      Init = ILE->getInit(ElementNo++);
    if (isa_and_nonnull<NoInitExpr>(Init))
      continue;

    // Zero-sized fields are not emitted, but their initializers may still
    // prevent emission of this struct as a constant.
    if (isEmptyFieldForLayout(CGM.getContext(), Field)) {
      if (Init->HasSideEffects(CGM.getContext()))
        return false;
      continue;
    }

    // When emitting a DesignatedInitUpdateExpr, a nested InitListExpr
    // represents additional overwriting of our current constant value, and not
    // a new constant to emit independently.
    if (AllowOverwrite &&
        (Field->getType()->isArrayType() || Field->getType()->isRecordType())) {
      if (auto *SubILE = dyn_cast<InitListExpr>(Init)) {
        CharUnits Offset = CGM.getContext().toCharUnitsFromBits(
            Layout.getFieldOffset(FieldNo));
        if (!EmitDesignatedInitUpdater(Emitter, Builder, StartOffset + Offset,
                                       Field->getType(), SubILE))
          return false;
        // If we split apart the field's value, try to collapse it down to a
        // single value now.
        Builder.condense(StartOffset + Offset,
                         CGM.getTypes().ConvertTypeForMem(Field->getType()));
        continue;
      }
    }

    llvm::Constant *EltInit =
        Init ? Emitter.tryEmitPrivateForMemory(Init, Field->getType())
             : Emitter.emitNullForMemory(Field->getType());
    if (!EltInit)
      return false;

    if (!Field->isBitField()) {
      // Handle non-bitfield members.
      if (!AppendField(Field, Layout.getFieldOffset(FieldNo), EltInit,
                       AllowOverwrite))
        return false;
      // After emitting a non-empty field with [[no_unique_address]], we may
      // need to overwrite its tail padding.
      if (Field->hasAttr<NoUniqueAddressAttr>())
        AllowOverwrite = true;
    } else {
      // Otherwise we have a bitfield.
      if (!AppendBitField(Field, Layout.getFieldOffset(FieldNo), EltInit,
                          AllowOverwrite))
        return false;
    }
  }

  return true;
}

namespace {
struct BaseInfo {
  BaseInfo(const CXXRecordDecl *Decl, CharUnits Offset, unsigned Index)
    : Decl(Decl), Offset(Offset), Index(Index) {
  }

  const CXXRecordDecl *Decl;
  CharUnits Offset;
  unsigned Index;

  bool operator<(const BaseInfo &O) const { return Offset < O.Offset; }
};
}

bool ConstStructBuilder::Build(const APValue &Val, const RecordDecl *RD,
                               bool IsPrimaryBase,
                               const CXXRecordDecl *VTableClass,
                               CharUnits Offset) {
  const ASTRecordLayout &Layout = CGM.getContext().getASTRecordLayout(RD);

  if (const CXXRecordDecl *CD = dyn_cast<CXXRecordDecl>(RD)) {
    // Add a vtable pointer, if we need one and it hasn't already been added.
    if (Layout.hasOwnVFPtr()) {
      llvm::Constant *VTableAddressPoint =
          CGM.getCXXABI().getVTableAddressPoint(BaseSubobject(CD, Offset),
                                                VTableClass);
      if (auto Authentication = CGM.getVTablePointerAuthentication(CD)) {
        VTableAddressPoint = Emitter.tryEmitConstantSignedPointer(
            VTableAddressPoint, *Authentication);
        if (!VTableAddressPoint)
          return false;
      }
      if (!AppendBytes(Offset, VTableAddressPoint))
        return false;
    }

    // Accumulate and sort bases, in order to visit them in address order, which
    // may not be the same as declaration order.
    SmallVector<BaseInfo, 8> Bases;
    Bases.reserve(CD->getNumBases());
    unsigned BaseNo = 0;
    for (CXXRecordDecl::base_class_const_iterator Base = CD->bases_begin(),
         BaseEnd = CD->bases_end(); Base != BaseEnd; ++Base, ++BaseNo) {
      assert(!Base->isVirtual() && "should not have virtual bases here");
      const CXXRecordDecl *BD = Base->getType()->getAsCXXRecordDecl();
      CharUnits BaseOffset = Layout.getBaseClassOffset(BD);
      Bases.push_back(BaseInfo(BD, BaseOffset, BaseNo));
    }
    llvm::stable_sort(Bases);

    for (unsigned I = 0, N = Bases.size(); I != N; ++I) {
      BaseInfo &Base = Bases[I];

      bool IsPrimaryBase = Layout.getPrimaryBase() == Base.Decl;
      Build(Val.getStructBase(Base.Index), Base.Decl, IsPrimaryBase,
            VTableClass, Offset + Base.Offset);
    }
  }

  unsigned FieldNo = 0;
  uint64_t OffsetBits = CGM.getContext().toBits(Offset);

  bool AllowOverwrite = false;
  for (RecordDecl::field_iterator Field = RD->field_begin(),
       FieldEnd = RD->field_end(); Field != FieldEnd; ++Field, ++FieldNo) {
    // If this is a union, skip all the fields that aren't being initialized.
    if (RD->isUnion() && !declaresSameEntity(Val.getUnionField(), *Field))
      continue;

    // Don't emit anonymous bitfields or zero-sized fields.
    if (Field->isUnnamedBitField() ||
        isEmptyFieldForLayout(CGM.getContext(), *Field))
      continue;

    // Emit the value of the initializer.
    const APValue &FieldValue =
      RD->isUnion() ? Val.getUnionValue() : Val.getStructField(FieldNo);
    llvm::Constant *EltInit =
      Emitter.tryEmitPrivateForMemory(FieldValue, Field->getType());
    if (!EltInit)
      return false;

    if (!Field->isBitField()) {
      // Handle non-bitfield members.
      if (!AppendField(*Field, Layout.getFieldOffset(FieldNo) + OffsetBits,
                       EltInit, AllowOverwrite))
        return false;
      // After emitting a non-empty field with [[no_unique_address]], we may
      // need to overwrite its tail padding.
      if (Field->hasAttr<NoUniqueAddressAttr>())
        AllowOverwrite = true;
    } else {
      // Otherwise we have a bitfield.
      if (!AppendBitField(*Field, Layout.getFieldOffset(FieldNo) + OffsetBits,
                          EltInit, AllowOverwrite))
        return false;
    }
  }

  return true;
}

llvm::Constant *ConstStructBuilder::Finalize(QualType Type) {
  Type = Type.getNonReferenceType();
  RecordDecl *RD = Type->castAs<RecordType>()->getDecl();
  llvm::Type *ValTy = CGM.getTypes().ConvertType(Type);
  return Builder.build(ValTy, RD->hasFlexibleArrayMember());
}

llvm::Constant *ConstStructBuilder::BuildStruct(ConstantEmitter &Emitter,
                                                const InitListExpr *ILE,
                                                QualType ValTy) {
  ConstantAggregateBuilder Const(Emitter.CGM);
  ConstStructBuilder Builder(Emitter, Const, CharUnits::Zero());

  if (!Builder.Build(ILE, /*AllowOverwrite*/false))
    return nullptr;

  return Builder.Finalize(ValTy);
}

llvm::Constant *ConstStructBuilder::BuildStruct(ConstantEmitter &Emitter,
                                                const APValue &Val,
                                                QualType ValTy) {
  ConstantAggregateBuilder Const(Emitter.CGM);
  ConstStructBuilder Builder(Emitter, Const, CharUnits::Zero());

  const RecordDecl *RD = ValTy->castAs<RecordType>()->getDecl();
  const CXXRecordDecl *CD = dyn_cast<CXXRecordDecl>(RD);
  if (!Builder.Build(Val, RD, false, CD, CharUnits::Zero()))
    return nullptr;

  return Builder.Finalize(ValTy);
}

bool ConstStructBuilder::UpdateStruct(ConstantEmitter &Emitter,
                                      ConstantAggregateBuilder &Const,
                                      CharUnits Offset,
                                      const InitListExpr *Updater) {
  return ConstStructBuilder(Emitter, Const, Offset)
      .Build(Updater, /*AllowOverwrite*/ true);
}

//===----------------------------------------------------------------------===//
//                             ConstExprEmitter
//===----------------------------------------------------------------------===//

static ConstantAddress
tryEmitGlobalCompoundLiteral(ConstantEmitter &emitter,
                             const CompoundLiteralExpr *E) {
  CodeGenModule &CGM = emitter.CGM;
  CharUnits Align = CGM.getContext().getTypeAlignInChars(E->getType());
  if (llvm::GlobalVariable *Addr =
          CGM.getAddrOfConstantCompoundLiteralIfEmitted(E))
    return ConstantAddress(Addr, Addr->getValueType(), Align);

  LangAS addressSpace = E->getType().getAddressSpace();
  llvm::Constant *C = emitter.tryEmitForInitializer(E->getInitializer(),
                                                    addressSpace, E->getType());
  if (!C) {
    assert(!E->isFileScope() &&
           "file-scope compound literal did not have constant initializer!");
    return ConstantAddress::invalid();
  }

  auto GV = new llvm::GlobalVariable(
      CGM.getModule(), C->getType(),
      E->getType().isConstantStorage(CGM.getContext(), true, false),
      llvm::GlobalValue::InternalLinkage, C, ".compoundliteral", nullptr,
      llvm::GlobalVariable::NotThreadLocal,
      CGM.getContext().getTargetAddressSpace(addressSpace));
  emitter.finalize(GV);
  GV->setAlignment(Align.getAsAlign());
  CGM.setAddrOfConstantCompoundLiteral(E, GV);
  return ConstantAddress(GV, GV->getValueType(), Align);
}

static llvm::Constant *
EmitArrayConstant(CodeGenModule &CGM, llvm::ArrayType *DesiredType,
                  llvm::Type *CommonElementType, uint64_t ArrayBound,
                  SmallVectorImpl<llvm::Constant *> &Elements,
                  llvm::Constant *Filler) {
  // Figure out how long the initial prefix of non-zero elements is.
  uint64_t NonzeroLength = ArrayBound;
  if (Elements.size() < NonzeroLength && Filler->isNullValue())
    NonzeroLength = Elements.size();
  if (NonzeroLength == Elements.size()) {
    while (NonzeroLength > 0 && Elements[NonzeroLength - 1]->isNullValue())
      --NonzeroLength;
  }

  if (NonzeroLength == 0)
    return llvm::ConstantAggregateZero::get(DesiredType);

  // Add a zeroinitializer array filler if we have lots of trailing zeroes.
  uint64_t TrailingZeroes = ArrayBound - NonzeroLength;
  if (TrailingZeroes >= 8) {
    assert(Elements.size() >= NonzeroLength &&
           "missing initializer for non-zero element");

    // If all the elements had the same type up to the trailing zeroes, emit a
    // struct of two arrays (the nonzero data and the zeroinitializer).
    if (CommonElementType && NonzeroLength >= 8) {
      llvm::Constant *Initial = llvm::ConstantArray::get(
          llvm::ArrayType::get(CommonElementType, NonzeroLength),
          ArrayRef(Elements).take_front(NonzeroLength));
      Elements.resize(2);
      Elements[0] = Initial;
    } else {
      Elements.resize(NonzeroLength + 1);
    }

    auto *FillerType =
        CommonElementType ? CommonElementType : DesiredType->getElementType();
    FillerType = llvm::ArrayType::get(FillerType, TrailingZeroes);
    Elements.back() = llvm::ConstantAggregateZero::get(FillerType);
    CommonElementType = nullptr;
  } else if (Elements.size() != ArrayBound) {
    // Otherwise pad to the right size with the filler if necessary.
    Elements.resize(ArrayBound, Filler);
    if (Filler->getType() != CommonElementType)
      CommonElementType = nullptr;
  }

  // If all elements have the same type, just emit an array constant.
  if (CommonElementType)
    return llvm::ConstantArray::get(
        llvm::ArrayType::get(CommonElementType, ArrayBound), Elements);

  // We have mixed types. Use a packed struct.
  llvm::SmallVector<llvm::Type *, 16> Types;
  Types.reserve(Elements.size());
  for (llvm::Constant *Elt : Elements)
    Types.push_back(Elt->getType());
  llvm::StructType *SType =
      llvm::StructType::get(CGM.getLLVMContext(), Types, true);
  return llvm::ConstantStruct::get(SType, Elements);
}

// This class only needs to handle arrays, structs and unions. Outside C++11
// mode, we don't currently constant fold those types.  All other types are
// handled by constant folding.
//
// Constant folding is currently missing support for a few features supported
// here: CK_ToUnion, CK_ReinterpretMemberPointer, and DesignatedInitUpdateExpr.
class ConstExprEmitter
    : public ConstStmtVisitor<ConstExprEmitter, llvm::Constant *, QualType> {
  CodeGenModule &CGM;
  ConstantEmitter &Emitter;
  llvm::LLVMContext &VMContext;
public:
  ConstExprEmitter(ConstantEmitter &emitter)
    : CGM(emitter.CGM), Emitter(emitter), VMContext(CGM.getLLVMContext()) {
  }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  llvm::Constant *VisitStmt(const Stmt *S, QualType T) { return nullptr; }

  llvm::Constant *VisitConstantExpr(const ConstantExpr *CE, QualType T) {
    if (llvm::Constant *Result = Emitter.tryEmitConstantExpr(CE))
      return Result;
    return Visit(CE->getSubExpr(), T);
  }

  llvm::Constant *VisitParenExpr(const ParenExpr *PE, QualType T) {
    return Visit(PE->getSubExpr(), T);
  }

  llvm::Constant *
  VisitSubstNonTypeTemplateParmExpr(const SubstNonTypeTemplateParmExpr *PE,
                                    QualType T) {
    return Visit(PE->getReplacement(), T);
  }

  llvm::Constant *VisitGenericSelectionExpr(const GenericSelectionExpr *GE,
                                            QualType T) {
    return Visit(GE->getResultExpr(), T);
  }

  llvm::Constant *VisitChooseExpr(const ChooseExpr *CE, QualType T) {
    return Visit(CE->getChosenSubExpr(), T);
  }

  llvm::Constant *VisitCompoundLiteralExpr(const CompoundLiteralExpr *E,
                                           QualType T) {
    return Visit(E->getInitializer(), T);
  }

  llvm::Constant *ProduceIntToIntCast(const Expr *E, QualType DestType) {
    QualType FromType = E->getType();
    // See also HandleIntToIntCast in ExprConstant.cpp
    if (FromType->isIntegerType())
      if (llvm::Constant *C = Visit(E, FromType))
        if (auto *CI = dyn_cast<llvm::ConstantInt>(C)) {
          unsigned SrcWidth = CGM.getContext().getIntWidth(FromType);
          unsigned DstWidth = CGM.getContext().getIntWidth(DestType);
          if (DstWidth == SrcWidth)
            return CI;
          llvm::APInt A = FromType->isSignedIntegerType()
                              ? CI->getValue().sextOrTrunc(DstWidth)
                              : CI->getValue().zextOrTrunc(DstWidth);
          return llvm::ConstantInt::get(CGM.getLLVMContext(), A);
        }
    return nullptr;
  }

  llvm::Constant *VisitCastExpr(const CastExpr *E, QualType destType) {
    if (const auto *ECE = dyn_cast<ExplicitCastExpr>(E))
      CGM.EmitExplicitCastExprType(ECE, Emitter.CGF);
    const Expr *subExpr = E->getSubExpr();

    switch (E->getCastKind()) {
    case CK_ToUnion: {
      // GCC cast to union extension
      assert(E->getType()->isUnionType() &&
             "Destination type is not union type!");

      auto field = E->getTargetUnionField();

      auto C = Emitter.tryEmitPrivateForMemory(subExpr, field->getType());
      if (!C) return nullptr;

      auto destTy = ConvertType(destType);
      if (C->getType() == destTy) return C;

      // Build a struct with the union sub-element as the first member,
      // and padded to the appropriate size.
      SmallVector<llvm::Constant*, 2> Elts;
      SmallVector<llvm::Type*, 2> Types;
      Elts.push_back(C);
      Types.push_back(C->getType());
      unsigned CurSize = CGM.getDataLayout().getTypeAllocSize(C->getType());
      unsigned TotalSize = CGM.getDataLayout().getTypeAllocSize(destTy);

      assert(CurSize <= TotalSize && "Union size mismatch!");
      if (unsigned NumPadBytes = TotalSize - CurSize) {
        llvm::Type *Ty = CGM.CharTy;
        if (NumPadBytes > 1)
          Ty = llvm::ArrayType::get(Ty, NumPadBytes);

        Elts.push_back(llvm::UndefValue::get(Ty));
        Types.push_back(Ty);
      }

      llvm::StructType *STy = llvm::StructType::get(VMContext, Types, false);
      return llvm::ConstantStruct::get(STy, Elts);
    }

    case CK_AddressSpaceConversion: {
      auto C = Emitter.tryEmitPrivate(subExpr, subExpr->getType());
      if (!C) return nullptr;
      LangAS destAS = E->getType()->getPointeeType().getAddressSpace();
      LangAS srcAS = subExpr->getType()->getPointeeType().getAddressSpace();
      llvm::Type *destTy = ConvertType(E->getType());
      return CGM.getTargetCodeGenInfo().performAddrSpaceCast(CGM, C, srcAS,
                                                             destAS, destTy);
    }

    case CK_LValueToRValue: {
      // We don't really support doing lvalue-to-rvalue conversions here; any
      // interesting conversions should be done in Evaluate().  But as a
      // special case, allow compound literals to support the gcc extension
      // allowing "struct x {int x;} x = (struct x) {};".
      if (const auto *E =
              dyn_cast<CompoundLiteralExpr>(subExpr->IgnoreParens()))
        return Visit(E->getInitializer(), destType);
      return nullptr;
    }

    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
    case CK_NoOp:
    case CK_ConstructorConversion:
      return Visit(subExpr, destType);

    case CK_ArrayToPointerDecay:
      if (const auto *S = dyn_cast<StringLiteral>(subExpr))
        return CGM.GetAddrOfConstantStringFromLiteral(S).getPointer();
      return nullptr;
    case CK_NullToPointer:
      if (Visit(subExpr, destType))
        return CGM.EmitNullConstant(destType);
      return nullptr;

    case CK_IntToOCLSampler:
      llvm_unreachable("global sampler variables are not generated");

    case CK_IntegralCast:
      return ProduceIntToIntCast(subExpr, destType);

    case CK_Dependent: llvm_unreachable("saw dependent cast!");

    case CK_BuiltinFnToFnPtr:
      llvm_unreachable("builtin functions are handled elsewhere");

    case CK_ReinterpretMemberPointer:
    case CK_DerivedToBaseMemberPointer:
    case CK_BaseToDerivedMemberPointer: {
      auto C = Emitter.tryEmitPrivate(subExpr, subExpr->getType());
      if (!C) return nullptr;
      return CGM.getCXXABI().EmitMemberPointerConversion(E, C);
    }

    // These will never be supported.
    case CK_ObjCObjectLValueCast:
    case CK_ARCProduceObject:
    case CK_ARCConsumeObject:
    case CK_ARCReclaimReturnedObject:
    case CK_ARCExtendBlockObject:
    case CK_CopyAndAutoreleaseBlockObject:
      return nullptr;

    // These don't need to be handled here because Evaluate knows how to
    // evaluate them in the cases where they can be folded.
    case CK_BitCast:
    case CK_ToVoid:
    case CK_Dynamic:
    case CK_LValueBitCast:
    case CK_LValueToRValueBitCast:
    case CK_NullToMemberPointer:
    case CK_UserDefinedConversion:
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_FunctionToPointerDecay:
    case CK_BaseToDerived:
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
    case CK_MemberPointerToBoolean:
    case CK_VectorSplat:
    case CK_FloatingRealToComplex:
    case CK_FloatingComplexToReal:
    case CK_FloatingComplexToBoolean:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToIntegralComplex:
    case CK_IntegralRealToComplex:
    case CK_IntegralComplexToReal:
    case CK_IntegralComplexToBoolean:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToFloatingComplex:
    case CK_PointerToIntegral:
    case CK_PointerToBoolean:
    case CK_BooleanToSignedIntegral:
    case CK_IntegralToPointer:
    case CK_IntegralToBoolean:
    case CK_IntegralToFloating:
    case CK_FloatingToIntegral:
    case CK_FloatingToBoolean:
    case CK_FloatingCast:
    case CK_FloatingToFixedPoint:
    case CK_FixedPointToFloating:
    case CK_FixedPointCast:
    case CK_FixedPointToBoolean:
    case CK_FixedPointToIntegral:
    case CK_IntegralToFixedPoint:
    case CK_ZeroToOCLOpaqueType:
    case CK_MatrixCast:
    case CK_HLSLVectorTruncation:
    case CK_HLSLArrayRValue:
      return nullptr;
    }
    llvm_unreachable("Invalid CastKind");
  }

  llvm::Constant *VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *DIE,
                                          QualType T) {
    // No need for a DefaultInitExprScope: we don't handle 'this' in a
    // constant expression.
    return Visit(DIE->getExpr(), T);
  }

  llvm::Constant *VisitExprWithCleanups(const ExprWithCleanups *E, QualType T) {
    return Visit(E->getSubExpr(), T);
  }

  llvm::Constant *VisitIntegerLiteral(const IntegerLiteral *I, QualType T) {
    return llvm::ConstantInt::get(CGM.getLLVMContext(), I->getValue());
  }

  static APValue withDestType(ASTContext &Ctx, const Expr *E, QualType SrcType,
                              QualType DestType, const llvm::APSInt &Value) {
    if (!Ctx.hasSameType(SrcType, DestType)) {
      if (DestType->isFloatingType()) {
        llvm::APFloat Result =
            llvm::APFloat(Ctx.getFloatTypeSemantics(DestType), 1);
        llvm::RoundingMode RM =
            E->getFPFeaturesInEffect(Ctx.getLangOpts()).getRoundingMode();
        if (RM == llvm::RoundingMode::Dynamic)
          RM = llvm::RoundingMode::NearestTiesToEven;
        Result.convertFromAPInt(Value, Value.isSigned(), RM);
        return APValue(Result);
      }
    }
    return APValue(Value);
  }

  llvm::Constant *EmitArrayInitialization(const InitListExpr *ILE, QualType T) {
    auto *CAT = CGM.getContext().getAsConstantArrayType(ILE->getType());
    assert(CAT && "can't emit array init for non-constant-bound array");
    uint64_t NumInitElements = ILE->getNumInits();
    const uint64_t NumElements = CAT->getZExtSize();
    for (const auto *Init : ILE->inits()) {
      if (const auto *Embed =
              dyn_cast<EmbedExpr>(Init->IgnoreParenImpCasts())) {
        NumInitElements += Embed->getDataElementCount() - 1;
        if (NumInitElements > NumElements) {
          NumInitElements = NumElements;
          break;
        }
      }
    }

    // Initialising an array requires us to automatically
    // initialise any elements that have not been initialised explicitly
    uint64_t NumInitableElts = std::min<uint64_t>(NumInitElements, NumElements);

    QualType EltType = CAT->getElementType();

    // Initialize remaining array elements.
    llvm::Constant *fillC = nullptr;
    if (const Expr *filler = ILE->getArrayFiller()) {
      fillC = Emitter.tryEmitAbstractForMemory(filler, EltType);
      if (!fillC)
        return nullptr;
    }

    // Copy initializer elements.
    SmallVector<llvm::Constant *, 16> Elts;
    if (fillC && fillC->isNullValue())
      Elts.reserve(NumInitableElts + 1);
    else
      Elts.reserve(NumElements);

    llvm::Type *CommonElementType = nullptr;
    auto Emit = [&](const Expr *Init, unsigned ArrayIndex) {
      llvm::Constant *C = nullptr;
      C = Emitter.tryEmitPrivateForMemory(Init, EltType);
      if (!C)
        return false;
      if (ArrayIndex == 0)
        CommonElementType = C->getType();
      else if (C->getType() != CommonElementType)
        CommonElementType = nullptr;
      Elts.push_back(C);
      return true;
    };

    unsigned ArrayIndex = 0;
    QualType DestTy = CAT->getElementType();
    for (unsigned i = 0; i < ILE->getNumInits(); ++i) {
      const Expr *Init = ILE->getInit(i);
      if (auto *EmbedS = dyn_cast<EmbedExpr>(Init->IgnoreParenImpCasts())) {
        StringLiteral *SL = EmbedS->getDataStringLiteral();
        llvm::APSInt Value(CGM.getContext().getTypeSize(DestTy),
                           DestTy->isUnsignedIntegerType());
        llvm::Constant *C;
        for (unsigned I = EmbedS->getStartingElementPos(),
                      N = EmbedS->getDataElementCount();
             I != EmbedS->getStartingElementPos() + N; ++I) {
          Value = SL->getCodeUnit(I);
          if (DestTy->isIntegerType()) {
            C = llvm::ConstantInt::get(CGM.getLLVMContext(), Value);
          } else {
            C = Emitter.tryEmitPrivateForMemory(
                withDestType(CGM.getContext(), Init, EmbedS->getType(), DestTy,
                             Value),
                EltType);
          }
          if (!C)
            return nullptr;
          Elts.push_back(C);
          ArrayIndex++;
        }
        if ((ArrayIndex - EmbedS->getDataElementCount()) == 0)
          CommonElementType = C->getType();
        else if (C->getType() != CommonElementType)
          CommonElementType = nullptr;
      } else {
        if (!Emit(Init, ArrayIndex))
          return nullptr;
        ArrayIndex++;
      }
    }

    llvm::ArrayType *Desired =
        cast<llvm::ArrayType>(CGM.getTypes().ConvertType(ILE->getType()));
    return EmitArrayConstant(CGM, Desired, CommonElementType, NumElements, Elts,
                             fillC);
  }

  llvm::Constant *EmitRecordInitialization(const InitListExpr *ILE,
                                           QualType T) {
    return ConstStructBuilder::BuildStruct(Emitter, ILE, T);
  }

  llvm::Constant *VisitImplicitValueInitExpr(const ImplicitValueInitExpr *E,
                                             QualType T) {
    return CGM.EmitNullConstant(T);
  }

  llvm::Constant *VisitInitListExpr(const InitListExpr *ILE, QualType T) {
    if (ILE->isTransparent())
      return Visit(ILE->getInit(0), T);

    if (ILE->getType()->isArrayType())
      return EmitArrayInitialization(ILE, T);

    if (ILE->getType()->isRecordType())
      return EmitRecordInitialization(ILE, T);

    return nullptr;
  }

  llvm::Constant *
  VisitDesignatedInitUpdateExpr(const DesignatedInitUpdateExpr *E,
                                QualType destType) {
    auto C = Visit(E->getBase(), destType);
    if (!C)
      return nullptr;

    ConstantAggregateBuilder Const(CGM);
    Const.add(C, CharUnits::Zero(), false);

    if (!EmitDesignatedInitUpdater(Emitter, Const, CharUnits::Zero(), destType,
                                   E->getUpdater()))
      return nullptr;

    llvm::Type *ValTy = CGM.getTypes().ConvertType(destType);
    bool HasFlexibleArray = false;
    if (const auto *RT = destType->getAs<RecordType>())
      HasFlexibleArray = RT->getDecl()->hasFlexibleArrayMember();
    return Const.build(ValTy, HasFlexibleArray);
  }

  llvm::Constant *VisitCXXConstructExpr(const CXXConstructExpr *E,
                                        QualType Ty) {
    if (!E->getConstructor()->isTrivial())
      return nullptr;

    // Only default and copy/move constructors can be trivial.
    if (E->getNumArgs()) {
      assert(E->getNumArgs() == 1 && "trivial ctor with > 1 argument");
      assert(E->getConstructor()->isCopyOrMoveConstructor() &&
             "trivial ctor has argument but isn't a copy/move ctor");

      const Expr *Arg = E->getArg(0);
      assert(CGM.getContext().hasSameUnqualifiedType(Ty, Arg->getType()) &&
             "argument to copy ctor is of wrong type");

      // Look through the temporary; it's just converting the value to an
      // lvalue to pass it to the constructor.
      if (const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(Arg))
        return Visit(MTE->getSubExpr(), Ty);
      // Don't try to support arbitrary lvalue-to-rvalue conversions for now.
      return nullptr;
    }

    return CGM.EmitNullConstant(Ty);
  }

  llvm::Constant *VisitStringLiteral(const StringLiteral *E, QualType T) {
    // This is a string literal initializing an array in an initializer.
    return CGM.GetConstantArrayFromStringLiteral(E);
  }

  llvm::Constant *VisitObjCEncodeExpr(const ObjCEncodeExpr *E, QualType T) {
    // This must be an @encode initializing an array in a static initializer.
    // Don't emit it as the address of the string, emit the string data itself
    // as an inline array.
    std::string Str;
    CGM.getContext().getObjCEncodingForType(E->getEncodedType(), Str);
    const ConstantArrayType *CAT = CGM.getContext().getAsConstantArrayType(T);
    assert(CAT && "String data not of constant array type!");

    // Resize the string to the right size, adding zeros at the end, or
    // truncating as needed.
    Str.resize(CAT->getZExtSize(), '\0');
    return llvm::ConstantDataArray::getString(VMContext, Str, false);
  }

  llvm::Constant *VisitUnaryExtension(const UnaryOperator *E, QualType T) {
    return Visit(E->getSubExpr(), T);
  }

  llvm::Constant *VisitUnaryMinus(const UnaryOperator *U, QualType T) {
    if (llvm::Constant *C = Visit(U->getSubExpr(), T))
      if (auto *CI = dyn_cast<llvm::ConstantInt>(C))
        return llvm::ConstantInt::get(CGM.getLLVMContext(), -CI->getValue());
    return nullptr;
  }

  llvm::Constant *VisitPackIndexingExpr(const PackIndexingExpr *E, QualType T) {
    return Visit(E->getSelectedExpr(), T);
  }

  // Utility methods
  llvm::Type *ConvertType(QualType T) {
    return CGM.getTypes().ConvertType(T);
  }
};

}  // end anonymous namespace.

llvm::Constant *ConstantEmitter::validateAndPopAbstract(llvm::Constant *C,
                                                        AbstractState saved) {
  Abstract = saved.OldValue;

  assert(saved.OldPlaceholdersSize == PlaceholderAddresses.size() &&
         "created a placeholder while doing an abstract emission?");

  // No validation necessary for now.
  // No cleanup to do for now.
  return C;
}

llvm::Constant *
ConstantEmitter::tryEmitAbstractForInitializer(const VarDecl &D) {
  auto state = pushAbstract();
  auto C = tryEmitPrivateForVarInit(D);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *
ConstantEmitter::tryEmitAbstract(const Expr *E, QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(E, destType);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *
ConstantEmitter::tryEmitAbstract(const APValue &value, QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(value, destType);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *ConstantEmitter::tryEmitConstantExpr(const ConstantExpr *CE) {
  if (!CE->hasAPValueResult())
    return nullptr;

  QualType RetType = CE->getType();
  if (CE->isGLValue())
    RetType = CGM.getContext().getLValueReferenceType(RetType);

  return emitAbstract(CE->getBeginLoc(), CE->getAPValueResult(), RetType);
}

llvm::Constant *
ConstantEmitter::emitAbstract(const Expr *E, QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(E, destType);
  C = validateAndPopAbstract(C, state);
  if (!C) {
    CGM.Error(E->getExprLoc(),
              "internal error: could not emit constant value \"abstractly\"");
    C = CGM.EmitNullConstant(destType);
  }
  return C;
}

llvm::Constant *
ConstantEmitter::emitAbstract(SourceLocation loc, const APValue &value,
                              QualType destType,
                              bool EnablePtrAuthFunctionTypeDiscrimination) {
  auto state = pushAbstract();
  auto C =
      tryEmitPrivate(value, destType, EnablePtrAuthFunctionTypeDiscrimination);
  C = validateAndPopAbstract(C, state);
  if (!C) {
    CGM.Error(loc,
              "internal error: could not emit constant value \"abstractly\"");
    C = CGM.EmitNullConstant(destType);
  }
  return C;
}

llvm::Constant *ConstantEmitter::tryEmitForInitializer(const VarDecl &D) {
  initializeNonAbstract(D.getType().getAddressSpace());
  return markIfFailed(tryEmitPrivateForVarInit(D));
}

llvm::Constant *ConstantEmitter::tryEmitForInitializer(const Expr *E,
                                                       LangAS destAddrSpace,
                                                       QualType destType) {
  initializeNonAbstract(destAddrSpace);
  return markIfFailed(tryEmitPrivateForMemory(E, destType));
}

llvm::Constant *ConstantEmitter::emitForInitializer(const APValue &value,
                                                    LangAS destAddrSpace,
                                                    QualType destType) {
  initializeNonAbstract(destAddrSpace);
  auto C = tryEmitPrivateForMemory(value, destType);
  assert(C && "couldn't emit constant value non-abstractly?");
  return C;
}

llvm::GlobalValue *ConstantEmitter::getCurrentAddrPrivate() {
  assert(!Abstract && "cannot get current address for abstract constant");



  // Make an obviously ill-formed global that should blow up compilation
  // if it survives.
  auto global = new llvm::GlobalVariable(CGM.getModule(), CGM.Int8Ty, true,
                                         llvm::GlobalValue::PrivateLinkage,
                                         /*init*/ nullptr,
                                         /*name*/ "",
                                         /*before*/ nullptr,
                                         llvm::GlobalVariable::NotThreadLocal,
                                         CGM.getContext().getTargetAddressSpace(DestAddressSpace));

  PlaceholderAddresses.push_back(std::make_pair(nullptr, global));

  return global;
}

void ConstantEmitter::registerCurrentAddrPrivate(llvm::Constant *signal,
                                           llvm::GlobalValue *placeholder) {
  assert(!PlaceholderAddresses.empty());
  assert(PlaceholderAddresses.back().first == nullptr);
  assert(PlaceholderAddresses.back().second == placeholder);
  PlaceholderAddresses.back().first = signal;
}

namespace {
  struct ReplacePlaceholders {
    CodeGenModule &CGM;

    /// The base address of the global.
    llvm::Constant *Base;
    llvm::Type *BaseValueTy = nullptr;

    /// The placeholder addresses that were registered during emission.
    llvm::DenseMap<llvm::Constant*, llvm::GlobalVariable*> PlaceholderAddresses;

    /// The locations of the placeholder signals.
    llvm::DenseMap<llvm::GlobalVariable*, llvm::Constant*> Locations;

    /// The current index stack.  We use a simple unsigned stack because
    /// we assume that placeholders will be relatively sparse in the
    /// initializer, but we cache the index values we find just in case.
    llvm::SmallVector<unsigned, 8> Indices;
    llvm::SmallVector<llvm::Constant*, 8> IndexValues;

    ReplacePlaceholders(CodeGenModule &CGM, llvm::Constant *base,
                        ArrayRef<std::pair<llvm::Constant*,
                                           llvm::GlobalVariable*>> addresses)
        : CGM(CGM), Base(base),
          PlaceholderAddresses(addresses.begin(), addresses.end()) {
    }

    void replaceInInitializer(llvm::Constant *init) {
      // Remember the type of the top-most initializer.
      BaseValueTy = init->getType();

      // Initialize the stack.
      Indices.push_back(0);
      IndexValues.push_back(nullptr);

      // Recurse into the initializer.
      findLocations(init);

      // Check invariants.
      assert(IndexValues.size() == Indices.size() && "mismatch");
      assert(Indices.size() == 1 && "didn't pop all indices");

      // Do the replacement; this basically invalidates 'init'.
      assert(Locations.size() == PlaceholderAddresses.size() &&
             "missed a placeholder?");

      // We're iterating over a hashtable, so this would be a source of
      // non-determinism in compiler output *except* that we're just
      // messing around with llvm::Constant structures, which never itself
      // does anything that should be visible in compiler output.
      for (auto &entry : Locations) {
        assert(entry.first->getName() == "" && "not a placeholder!");
        entry.first->replaceAllUsesWith(entry.second);
        entry.first->eraseFromParent();
      }
    }

  private:
    void findLocations(llvm::Constant *init) {
      // Recurse into aggregates.
      if (auto agg = dyn_cast<llvm::ConstantAggregate>(init)) {
        for (unsigned i = 0, e = agg->getNumOperands(); i != e; ++i) {
          Indices.push_back(i);
          IndexValues.push_back(nullptr);

          findLocations(agg->getOperand(i));

          IndexValues.pop_back();
          Indices.pop_back();
        }
        return;
      }

      // Otherwise, check for registered constants.
      while (true) {
        auto it = PlaceholderAddresses.find(init);
        if (it != PlaceholderAddresses.end()) {
          setLocation(it->second);
          break;
        }

        // Look through bitcasts or other expressions.
        if (auto expr = dyn_cast<llvm::ConstantExpr>(init)) {
          init = expr->getOperand(0);
        } else {
          break;
        }
      }
    }

    void setLocation(llvm::GlobalVariable *placeholder) {
      assert(!Locations.contains(placeholder) &&
             "already found location for placeholder!");

      // Lazily fill in IndexValues with the values from Indices.
      // We do this in reverse because we should always have a strict
      // prefix of indices from the start.
      assert(Indices.size() == IndexValues.size());
      for (size_t i = Indices.size() - 1; i != size_t(-1); --i) {
        if (IndexValues[i]) {
#ifndef NDEBUG
          for (size_t j = 0; j != i + 1; ++j) {
            assert(IndexValues[j] &&
                   isa<llvm::ConstantInt>(IndexValues[j]) &&
                   cast<llvm::ConstantInt>(IndexValues[j])->getZExtValue()
                     == Indices[j]);
          }
#endif
          break;
        }

        IndexValues[i] = llvm::ConstantInt::get(CGM.Int32Ty, Indices[i]);
      }

      llvm::Constant *location = llvm::ConstantExpr::getInBoundsGetElementPtr(
          BaseValueTy, Base, IndexValues);

      Locations.insert({placeholder, location});
    }
  };
}

void ConstantEmitter::finalize(llvm::GlobalVariable *global) {
  assert(InitializedNonAbstract &&
         "finalizing emitter that was used for abstract emission?");
  assert(!Finalized && "finalizing emitter multiple times");
  assert(global->getInitializer());

  // Note that we might also be Failed.
  Finalized = true;

  if (!PlaceholderAddresses.empty()) {
    ReplacePlaceholders(CGM, global, PlaceholderAddresses)
      .replaceInInitializer(global->getInitializer());
    PlaceholderAddresses.clear(); // satisfy
  }
}

ConstantEmitter::~ConstantEmitter() {
  assert((!InitializedNonAbstract || Finalized || Failed) &&
         "not finalized after being initialized for non-abstract emission");
  assert(PlaceholderAddresses.empty() && "unhandled placeholders");
}

static QualType getNonMemoryType(CodeGenModule &CGM, QualType type) {
  if (auto AT = type->getAs<AtomicType>()) {
    return CGM.getContext().getQualifiedType(AT->getValueType(),
                                             type.getQualifiers());
  }
  return type;
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForVarInit(const VarDecl &D) {
  // Make a quick check if variable can be default NULL initialized
  // and avoid going through rest of code which may do, for c++11,
  // initialization of memory to all NULLs.
  if (!D.hasLocalStorage()) {
    QualType Ty = CGM.getContext().getBaseElementType(D.getType());
    if (Ty->isRecordType())
      if (const CXXConstructExpr *E =
          dyn_cast_or_null<CXXConstructExpr>(D.getInit())) {
        const CXXConstructorDecl *CD = E->getConstructor();
        if (CD->isTrivial() && CD->isDefaultConstructor())
          return CGM.EmitNullConstant(D.getType());
      }
  }
  InConstantContext = D.hasConstantInitialization();

  QualType destType = D.getType();
  const Expr *E = D.getInit();
  assert(E && "No initializer to emit");

  if (!destType->isReferenceType()) {
    QualType nonMemoryDestType = getNonMemoryType(CGM, destType);
    if (llvm::Constant *C = ConstExprEmitter(*this).Visit(E, nonMemoryDestType))
      return emitForMemory(C, destType);
  }

  // Try to emit the initializer.  Note that this can allow some things that
  // are not allowed by tryEmitPrivateForMemory alone.
  if (APValue *value = D.evaluateValue())
    return tryEmitPrivateForMemory(*value, destType);

  return nullptr;
}

llvm::Constant *
ConstantEmitter::tryEmitAbstractForMemory(const Expr *E, QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  auto C = tryEmitAbstract(E, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *
ConstantEmitter::tryEmitAbstractForMemory(const APValue &value,
                                          QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  auto C = tryEmitAbstract(value, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForMemory(const Expr *E,
                                                         QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  llvm::Constant *C = tryEmitPrivate(E, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForMemory(const APValue &value,
                                                         QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  auto C = tryEmitPrivate(value, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

/// Try to emit a constant signed pointer, given a raw pointer and the
/// destination ptrauth qualifier.
///
/// This can fail if the qualifier needs address discrimination and the
/// emitter is in an abstract mode.
llvm::Constant *
ConstantEmitter::tryEmitConstantSignedPointer(llvm::Constant *UnsignedPointer,
                                              PointerAuthQualifier Schema) {
  assert(Schema && "applying trivial ptrauth schema");

  if (Schema.hasKeyNone())
    return UnsignedPointer;

  unsigned Key = Schema.getKey();

  // Create an address placeholder if we're using address discrimination.
  llvm::GlobalValue *StorageAddress = nullptr;
  if (Schema.isAddressDiscriminated()) {
    // We can't do this if the emitter is in an abstract state.
    if (isAbstract())
      return nullptr;

    StorageAddress = getCurrentAddrPrivate();
  }

  llvm::ConstantInt *Discriminator =
      llvm::ConstantInt::get(CGM.IntPtrTy, Schema.getExtraDiscriminator());

  llvm::Constant *SignedPointer = CGM.getConstantSignedPointer(
      UnsignedPointer, Key, StorageAddress, Discriminator);

  if (Schema.isAddressDiscriminated())
    registerCurrentAddrPrivate(SignedPointer, StorageAddress);

  return SignedPointer;
}

llvm::Constant *ConstantEmitter::emitForMemory(CodeGenModule &CGM,
                                               llvm::Constant *C,
                                               QualType destType) {
  // For an _Atomic-qualified constant, we may need to add tail padding.
  if (auto AT = destType->getAs<AtomicType>()) {
    QualType destValueType = AT->getValueType();
    C = emitForMemory(CGM, C, destValueType);

    uint64_t innerSize = CGM.getContext().getTypeSize(destValueType);
    uint64_t outerSize = CGM.getContext().getTypeSize(destType);
    if (innerSize == outerSize)
      return C;

    assert(innerSize < outerSize && "emitted over-large constant for atomic");
    llvm::Constant *elts[] = {
      C,
      llvm::ConstantAggregateZero::get(
          llvm::ArrayType::get(CGM.Int8Ty, (outerSize - innerSize) / 8))
    };
    return llvm::ConstantStruct::getAnon(elts);
  }

  // Zero-extend bool.
  if (C->getType()->isIntegerTy(1) && !destType->isBitIntType()) {
    llvm::Type *boolTy = CGM.getTypes().ConvertTypeForMem(destType);
    llvm::Constant *Res = llvm::ConstantFoldCastOperand(
        llvm::Instruction::ZExt, C, boolTy, CGM.getDataLayout());
    assert(Res && "Constant folding must succeed");
    return Res;
  }

  if (destType->isBitIntType()) {
    ConstantAggregateBuilder Builder(CGM);
    llvm::Type *LoadStoreTy = CGM.getTypes().convertTypeForLoadStore(destType);
    // ptrtoint/inttoptr should not involve _BitInt in constant expressions, so
    // casting to ConstantInt is safe here.
    auto *CI = cast<llvm::ConstantInt>(C);
    llvm::Constant *Res = llvm::ConstantFoldCastOperand(
        destType->isSignedIntegerOrEnumerationType() ? llvm::Instruction::SExt
                                                     : llvm::Instruction::ZExt,
        CI, LoadStoreTy, CGM.getDataLayout());
    if (CGM.getTypes().typeRequiresSplitIntoByteArray(destType, C->getType())) {
      // Long _BitInt has array of bytes as in-memory type.
      // So, split constant into individual bytes.
      llvm::Type *DesiredTy = CGM.getTypes().ConvertTypeForMem(destType);
      llvm::APInt Value = cast<llvm::ConstantInt>(Res)->getValue();
      Builder.addBits(Value, /*OffsetInBits=*/0, /*AllowOverwrite=*/false);
      return Builder.build(DesiredTy, /*AllowOversized*/ false);
    }
    return Res;
  }

  return C;
}

llvm::Constant *ConstantEmitter::tryEmitPrivate(const Expr *E,
                                                QualType destType) {
  assert(!destType->isVoidType() && "can't emit a void constant");

  if (!destType->isReferenceType())
    if (llvm::Constant *C = ConstExprEmitter(*this).Visit(E, destType))
      return C;

  Expr::EvalResult Result;

  bool Success = false;

  if (destType->isReferenceType())
    Success = E->EvaluateAsLValue(Result, CGM.getContext());
  else
    Success = E->EvaluateAsRValue(Result, CGM.getContext(), InConstantContext);

  if (Success && !Result.HasSideEffects)
    return tryEmitPrivate(Result.Val, destType);

  return nullptr;
}

llvm::Constant *CodeGenModule::getNullPointer(llvm::PointerType *T, QualType QT) {
  return getTargetCodeGenInfo().getNullPointer(*this, T, QT);
}

namespace {
/// A struct which can be used to peephole certain kinds of finalization
/// that normally happen during l-value emission.
struct ConstantLValue {
  llvm::Constant *Value;
  bool HasOffsetApplied;

  /*implicit*/ ConstantLValue(llvm::Constant *value,
                              bool hasOffsetApplied = false)
    : Value(value), HasOffsetApplied(hasOffsetApplied) {}

  /*implicit*/ ConstantLValue(ConstantAddress address)
    : ConstantLValue(address.getPointer()) {}
};

/// A helper class for emitting constant l-values.
class ConstantLValueEmitter : public ConstStmtVisitor<ConstantLValueEmitter,
                                                      ConstantLValue> {
  CodeGenModule &CGM;
  ConstantEmitter &Emitter;
  const APValue &Value;
  QualType DestType;
  bool EnablePtrAuthFunctionTypeDiscrimination;

  // Befriend StmtVisitorBase so that we don't have to expose Visit*.
  friend StmtVisitorBase;

public:
  ConstantLValueEmitter(ConstantEmitter &emitter, const APValue &value,
                        QualType destType,
                        bool EnablePtrAuthFunctionTypeDiscrimination = true)
      : CGM(emitter.CGM), Emitter(emitter), Value(value), DestType(destType),
        EnablePtrAuthFunctionTypeDiscrimination(
            EnablePtrAuthFunctionTypeDiscrimination) {}

  llvm::Constant *tryEmit();

private:
  llvm::Constant *tryEmitAbsolute(llvm::Type *destTy);
  ConstantLValue tryEmitBase(const APValue::LValueBase &base);

  ConstantLValue VisitStmt(const Stmt *S) { return nullptr; }
  ConstantLValue VisitConstantExpr(const ConstantExpr *E);
  ConstantLValue VisitCompoundLiteralExpr(const CompoundLiteralExpr *E);
  ConstantLValue VisitStringLiteral(const StringLiteral *E);
  ConstantLValue VisitObjCBoxedExpr(const ObjCBoxedExpr *E);
  ConstantLValue VisitObjCEncodeExpr(const ObjCEncodeExpr *E);
  ConstantLValue VisitObjCStringLiteral(const ObjCStringLiteral *E);
  ConstantLValue VisitPredefinedExpr(const PredefinedExpr *E);
  ConstantLValue VisitAddrLabelExpr(const AddrLabelExpr *E);
  ConstantLValue VisitCallExpr(const CallExpr *E);
  ConstantLValue VisitBlockExpr(const BlockExpr *E);
  ConstantLValue VisitCXXTypeidExpr(const CXXTypeidExpr *E);
  ConstantLValue VisitMaterializeTemporaryExpr(
                                         const MaterializeTemporaryExpr *E);

  ConstantLValue emitPointerAuthSignConstant(const CallExpr *E);
  llvm::Constant *emitPointerAuthPointer(const Expr *E);
  unsigned emitPointerAuthKey(const Expr *E);
  std::pair<llvm::Constant *, llvm::ConstantInt *>
  emitPointerAuthDiscriminator(const Expr *E);

  bool hasNonZeroOffset() const {
    return !Value.getLValueOffset().isZero();
  }

  /// Return the value offset.
  llvm::Constant *getOffset() {
    return llvm::ConstantInt::get(CGM.Int64Ty,
                                  Value.getLValueOffset().getQuantity());
  }

  /// Apply the value offset to the given constant.
  llvm::Constant *applyOffset(llvm::Constant *C) {
    if (!hasNonZeroOffset())
      return C;

    return llvm::ConstantExpr::getGetElementPtr(CGM.Int8Ty, C, getOffset());
  }
};

}

llvm::Constant *ConstantLValueEmitter::tryEmit() {
  const APValue::LValueBase &base = Value.getLValueBase();

  // The destination type should be a pointer or reference
  // type, but it might also be a cast thereof.
  //
  // FIXME: the chain of casts required should be reflected in the APValue.
  // We need this in order to correctly handle things like a ptrtoint of a
  // non-zero null pointer and addrspace casts that aren't trivially
  // represented in LLVM IR.
  auto destTy = CGM.getTypes().ConvertTypeForMem(DestType);
  assert(isa<llvm::IntegerType>(destTy) || isa<llvm::PointerType>(destTy));

  // If there's no base at all, this is a null or absolute pointer,
  // possibly cast back to an integer type.
  if (!base) {
    return tryEmitAbsolute(destTy);
  }

  // Otherwise, try to emit the base.
  ConstantLValue result = tryEmitBase(base);

  // If that failed, we're done.
  llvm::Constant *value = result.Value;
  if (!value) return nullptr;

  // Apply the offset if necessary and not already done.
  if (!result.HasOffsetApplied) {
    value = applyOffset(value);
  }

  // Convert to the appropriate type; this could be an lvalue for
  // an integer.  FIXME: performAddrSpaceCast
  if (isa<llvm::PointerType>(destTy))
    return llvm::ConstantExpr::getPointerCast(value, destTy);

  return llvm::ConstantExpr::getPtrToInt(value, destTy);
}

/// Try to emit an absolute l-value, such as a null pointer or an integer
/// bitcast to pointer type.
llvm::Constant *
ConstantLValueEmitter::tryEmitAbsolute(llvm::Type *destTy) {
  // If we're producing a pointer, this is easy.
  auto destPtrTy = cast<llvm::PointerType>(destTy);
  if (Value.isNullPointer()) {
    // FIXME: integer offsets from non-zero null pointers.
    return CGM.getNullPointer(destPtrTy, DestType);
  }

  // Convert the integer to a pointer-sized integer before converting it
  // to a pointer.
  // FIXME: signedness depends on the original integer type.
  auto intptrTy = CGM.getDataLayout().getIntPtrType(destPtrTy);
  llvm::Constant *C;
  C = llvm::ConstantFoldIntegerCast(getOffset(), intptrTy, /*isSigned*/ false,
                                    CGM.getDataLayout());
  assert(C && "Must have folded, as Offset is a ConstantInt");
  C = llvm::ConstantExpr::getIntToPtr(C, destPtrTy);
  return C;
}

ConstantLValue
ConstantLValueEmitter::tryEmitBase(const APValue::LValueBase &base) {
  // Handle values.
  if (const ValueDecl *D = base.dyn_cast<const ValueDecl*>()) {
    // The constant always points to the canonical declaration. We want to look
    // at properties of the most recent declaration at the point of emission.
    D = cast<ValueDecl>(D->getMostRecentDecl());

    if (D->hasAttr<WeakRefAttr>())
      return CGM.GetWeakRefReference(D).getPointer();

    auto PtrAuthSign = [&](llvm::Constant *C) {
      CGPointerAuthInfo AuthInfo;

      if (EnablePtrAuthFunctionTypeDiscrimination)
        AuthInfo = CGM.getFunctionPointerAuthInfo(DestType);

      if (AuthInfo) {
        if (hasNonZeroOffset())
          return ConstantLValue(nullptr);

        C = applyOffset(C);
        C = CGM.getConstantSignedPointer(
            C, AuthInfo.getKey(), nullptr,
            cast_or_null<llvm::ConstantInt>(AuthInfo.getDiscriminator()));
        return ConstantLValue(C, /*applied offset*/ true);
      }

      return ConstantLValue(C);
    };

    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      return PtrAuthSign(CGM.getRawFunctionPointer(FD));

    if (const auto *VD = dyn_cast<VarDecl>(D)) {
      // We can never refer to a variable with local storage.
      if (!VD->hasLocalStorage()) {
        if (VD->isFileVarDecl() || VD->hasExternalStorage())
          return CGM.GetAddrOfGlobalVar(VD);

        if (VD->isLocalVarDecl()) {
          return CGM.getOrCreateStaticVarDecl(
              *VD, CGM.getLLVMLinkageVarDefinition(VD));
        }
      }
    }

    if (const auto *GD = dyn_cast<MSGuidDecl>(D))
      return CGM.GetAddrOfMSGuidDecl(GD);

    if (const auto *GCD = dyn_cast<UnnamedGlobalConstantDecl>(D))
      return CGM.GetAddrOfUnnamedGlobalConstantDecl(GCD);

    if (const auto *TPO = dyn_cast<TemplateParamObjectDecl>(D))
      return CGM.GetAddrOfTemplateParamObject(TPO);

    return nullptr;
  }

  // Handle typeid(T).
  if (TypeInfoLValue TI = base.dyn_cast<TypeInfoLValue>())
    return CGM.GetAddrOfRTTIDescriptor(QualType(TI.getType(), 0));

  // Otherwise, it must be an expression.
  return Visit(base.get<const Expr*>());
}

ConstantLValue
ConstantLValueEmitter::VisitConstantExpr(const ConstantExpr *E) {
  if (llvm::Constant *Result = Emitter.tryEmitConstantExpr(E))
    return Result;
  return Visit(E->getSubExpr());
}

ConstantLValue
ConstantLValueEmitter::VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
  ConstantEmitter CompoundLiteralEmitter(CGM, Emitter.CGF);
  CompoundLiteralEmitter.setInConstantContext(Emitter.isInConstantContext());
  return tryEmitGlobalCompoundLiteral(CompoundLiteralEmitter, E);
}

ConstantLValue
ConstantLValueEmitter::VisitStringLiteral(const StringLiteral *E) {
  return CGM.GetAddrOfConstantStringFromLiteral(E);
}

ConstantLValue
ConstantLValueEmitter::VisitObjCEncodeExpr(const ObjCEncodeExpr *E) {
  return CGM.GetAddrOfConstantStringFromObjCEncode(E);
}

static ConstantLValue emitConstantObjCStringLiteral(const StringLiteral *S,
                                                    QualType T,
                                                    CodeGenModule &CGM) {
  auto C = CGM.getObjCRuntime().GenerateConstantString(S);
  return C.withElementType(CGM.getTypes().ConvertTypeForMem(T));
}

ConstantLValue
ConstantLValueEmitter::VisitObjCStringLiteral(const ObjCStringLiteral *E) {
  return emitConstantObjCStringLiteral(E->getString(), E->getType(), CGM);
}

ConstantLValue
ConstantLValueEmitter::VisitObjCBoxedExpr(const ObjCBoxedExpr *E) {
  assert(E->isExpressibleAsConstantInitializer() &&
         "this boxed expression can't be emitted as a compile-time constant");
  const auto *SL = cast<StringLiteral>(E->getSubExpr()->IgnoreParenCasts());
  return emitConstantObjCStringLiteral(SL, E->getType(), CGM);
}

ConstantLValue
ConstantLValueEmitter::VisitPredefinedExpr(const PredefinedExpr *E) {
  return CGM.GetAddrOfConstantStringFromLiteral(E->getFunctionName());
}

ConstantLValue
ConstantLValueEmitter::VisitAddrLabelExpr(const AddrLabelExpr *E) {
  assert(Emitter.CGF && "Invalid address of label expression outside function");
  llvm::Constant *Ptr = Emitter.CGF->GetAddrOfLabel(E->getLabel());
  return Ptr;
}

ConstantLValue
ConstantLValueEmitter::VisitCallExpr(const CallExpr *E) {
  unsigned builtin = E->getBuiltinCallee();
  if (builtin == Builtin::BI__builtin_function_start)
    return CGM.GetFunctionStart(
        E->getArg(0)->getAsBuiltinConstantDeclRef(CGM.getContext()));

  if (builtin == Builtin::BI__builtin_ptrauth_sign_constant)
    return emitPointerAuthSignConstant(E);

  if (builtin != Builtin::BI__builtin___CFStringMakeConstantString &&
      builtin != Builtin::BI__builtin___NSStringMakeConstantString)
    return nullptr;

  const auto *Literal = cast<StringLiteral>(E->getArg(0)->IgnoreParenCasts());
  if (builtin == Builtin::BI__builtin___NSStringMakeConstantString) {
    return CGM.getObjCRuntime().GenerateConstantString(Literal);
  } else {
    // FIXME: need to deal with UCN conversion issues.
    return CGM.GetAddrOfConstantCFString(Literal);
  }
}

ConstantLValue
ConstantLValueEmitter::emitPointerAuthSignConstant(const CallExpr *E) {
  llvm::Constant *UnsignedPointer = emitPointerAuthPointer(E->getArg(0));
  unsigned Key = emitPointerAuthKey(E->getArg(1));
  auto [StorageAddress, OtherDiscriminator] =
      emitPointerAuthDiscriminator(E->getArg(2));

  llvm::Constant *SignedPointer = CGM.getConstantSignedPointer(
      UnsignedPointer, Key, StorageAddress, OtherDiscriminator);
  return SignedPointer;
}

llvm::Constant *ConstantLValueEmitter::emitPointerAuthPointer(const Expr *E) {
  Expr::EvalResult Result;
  bool Succeeded = E->EvaluateAsRValue(Result, CGM.getContext());
  assert(Succeeded);
  (void)Succeeded;

  // The assertions here are all checked by Sema.
  assert(Result.Val.isLValue());
  if (isa<FunctionDecl>(Result.Val.getLValueBase().get<const ValueDecl *>()))
    assert(Result.Val.getLValueOffset().isZero());
  return ConstantEmitter(CGM, Emitter.CGF)
      .emitAbstract(E->getExprLoc(), Result.Val, E->getType(), false);
}

unsigned ConstantLValueEmitter::emitPointerAuthKey(const Expr *E) {
  return E->EvaluateKnownConstInt(CGM.getContext()).getZExtValue();
}

std::pair<llvm::Constant *, llvm::ConstantInt *>
ConstantLValueEmitter::emitPointerAuthDiscriminator(const Expr *E) {
  E = E->IgnoreParens();

  if (const auto *Call = dyn_cast<CallExpr>(E)) {
    if (Call->getBuiltinCallee() ==
        Builtin::BI__builtin_ptrauth_blend_discriminator) {
      llvm::Constant *Pointer = ConstantEmitter(CGM).emitAbstract(
          Call->getArg(0), Call->getArg(0)->getType());
      auto *Extra = cast<llvm::ConstantInt>(ConstantEmitter(CGM).emitAbstract(
          Call->getArg(1), Call->getArg(1)->getType()));
      return {Pointer, Extra};
    }
  }

  llvm::Constant *Result = ConstantEmitter(CGM).emitAbstract(E, E->getType());
  if (Result->getType()->isPointerTy())
    return {Result, nullptr};
  return {nullptr, cast<llvm::ConstantInt>(Result)};
}

ConstantLValue
ConstantLValueEmitter::VisitBlockExpr(const BlockExpr *E) {
  StringRef functionName;
  if (auto CGF = Emitter.CGF)
    functionName = CGF->CurFn->getName();
  else
    functionName = "global";

  return CGM.GetAddrOfGlobalBlock(E, functionName);
}

ConstantLValue
ConstantLValueEmitter::VisitCXXTypeidExpr(const CXXTypeidExpr *E) {
  QualType T;
  if (E->isTypeOperand())
    T = E->getTypeOperand(CGM.getContext());
  else
    T = E->getExprOperand()->getType();
  return CGM.GetAddrOfRTTIDescriptor(T);
}

ConstantLValue
ConstantLValueEmitter::VisitMaterializeTemporaryExpr(
                                            const MaterializeTemporaryExpr *E) {
  assert(E->getStorageDuration() == SD_Static);
  const Expr *Inner = E->getSubExpr()->skipRValueSubobjectAdjustments();
  return CGM.GetAddrOfGlobalTemporary(E, Inner);
}

llvm::Constant *
ConstantEmitter::tryEmitPrivate(const APValue &Value, QualType DestType,
                                bool EnablePtrAuthFunctionTypeDiscrimination) {
  switch (Value.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
    // Out-of-lifetime and indeterminate values can be modeled as 'undef'.
    return llvm::UndefValue::get(CGM.getTypes().ConvertType(DestType));
  case APValue::LValue:
    return ConstantLValueEmitter(*this, Value, DestType,
                                 EnablePtrAuthFunctionTypeDiscrimination)
        .tryEmit();
  case APValue::Int:
    return llvm::ConstantInt::get(CGM.getLLVMContext(), Value.getInt());
  case APValue::FixedPoint:
    return llvm::ConstantInt::get(CGM.getLLVMContext(),
                                  Value.getFixedPoint().getValue());
  case APValue::ComplexInt: {
    llvm::Constant *Complex[2];

    Complex[0] = llvm::ConstantInt::get(CGM.getLLVMContext(),
                                        Value.getComplexIntReal());
    Complex[1] = llvm::ConstantInt::get(CGM.getLLVMContext(),
                                        Value.getComplexIntImag());

    // FIXME: the target may want to specify that this is packed.
    llvm::StructType *STy =
        llvm::StructType::get(Complex[0]->getType(), Complex[1]->getType());
    return llvm::ConstantStruct::get(STy, Complex);
  }
  case APValue::Float: {
    const llvm::APFloat &Init = Value.getFloat();
    if (&Init.getSemantics() == &llvm::APFloat::IEEEhalf() &&
        !CGM.getContext().getLangOpts().NativeHalfType &&
        CGM.getContext().getTargetInfo().useFP16ConversionIntrinsics())
      return llvm::ConstantInt::get(CGM.getLLVMContext(),
                                    Init.bitcastToAPInt());
    else
      return llvm::ConstantFP::get(CGM.getLLVMContext(), Init);
  }
  case APValue::ComplexFloat: {
    llvm::Constant *Complex[2];

    Complex[0] = llvm::ConstantFP::get(CGM.getLLVMContext(),
                                       Value.getComplexFloatReal());
    Complex[1] = llvm::ConstantFP::get(CGM.getLLVMContext(),
                                       Value.getComplexFloatImag());

    // FIXME: the target may want to specify that this is packed.
    llvm::StructType *STy =
        llvm::StructType::get(Complex[0]->getType(), Complex[1]->getType());
    return llvm::ConstantStruct::get(STy, Complex);
  }
  case APValue::Vector: {
    unsigned NumElts = Value.getVectorLength();
    SmallVector<llvm::Constant *, 4> Inits(NumElts);

    for (unsigned I = 0; I != NumElts; ++I) {
      const APValue &Elt = Value.getVectorElt(I);
      if (Elt.isInt())
        Inits[I] = llvm::ConstantInt::get(CGM.getLLVMContext(), Elt.getInt());
      else if (Elt.isFloat())
        Inits[I] = llvm::ConstantFP::get(CGM.getLLVMContext(), Elt.getFloat());
      else if (Elt.isIndeterminate())
        Inits[I] = llvm::UndefValue::get(CGM.getTypes().ConvertType(
            DestType->castAs<VectorType>()->getElementType()));
      else
        llvm_unreachable("unsupported vector element type");
    }
    return llvm::ConstantVector::get(Inits);
  }
  case APValue::AddrLabelDiff: {
    const AddrLabelExpr *LHSExpr = Value.getAddrLabelDiffLHS();
    const AddrLabelExpr *RHSExpr = Value.getAddrLabelDiffRHS();
    llvm::Constant *LHS = tryEmitPrivate(LHSExpr, LHSExpr->getType());
    llvm::Constant *RHS = tryEmitPrivate(RHSExpr, RHSExpr->getType());
    if (!LHS || !RHS) return nullptr;

    // Compute difference
    llvm::Type *ResultType = CGM.getTypes().ConvertType(DestType);
    LHS = llvm::ConstantExpr::getPtrToInt(LHS, CGM.IntPtrTy);
    RHS = llvm::ConstantExpr::getPtrToInt(RHS, CGM.IntPtrTy);
    llvm::Constant *AddrLabelDiff = llvm::ConstantExpr::getSub(LHS, RHS);

    // LLVM is a bit sensitive about the exact format of the
    // address-of-label difference; make sure to truncate after
    // the subtraction.
    return llvm::ConstantExpr::getTruncOrBitCast(AddrLabelDiff, ResultType);
  }
  case APValue::Struct:
  case APValue::Union:
    return ConstStructBuilder::BuildStruct(*this, Value, DestType);
  case APValue::Array: {
    const ArrayType *ArrayTy = CGM.getContext().getAsArrayType(DestType);
    unsigned NumElements = Value.getArraySize();
    unsigned NumInitElts = Value.getArrayInitializedElts();

    // Emit array filler, if there is one.
    llvm::Constant *Filler = nullptr;
    if (Value.hasArrayFiller()) {
      Filler = tryEmitAbstractForMemory(Value.getArrayFiller(),
                                        ArrayTy->getElementType());
      if (!Filler)
        return nullptr;
    }

    // Emit initializer elements.
    SmallVector<llvm::Constant*, 16> Elts;
    if (Filler && Filler->isNullValue())
      Elts.reserve(NumInitElts + 1);
    else
      Elts.reserve(NumElements);

    llvm::Type *CommonElementType = nullptr;
    for (unsigned I = 0; I < NumInitElts; ++I) {
      llvm::Constant *C = tryEmitPrivateForMemory(
          Value.getArrayInitializedElt(I), ArrayTy->getElementType());
      if (!C) return nullptr;

      if (I == 0)
        CommonElementType = C->getType();
      else if (C->getType() != CommonElementType)
        CommonElementType = nullptr;
      Elts.push_back(C);
    }

    llvm::ArrayType *Desired =
        cast<llvm::ArrayType>(CGM.getTypes().ConvertType(DestType));

    // Fix the type of incomplete arrays if the initializer isn't empty.
    if (DestType->isIncompleteArrayType() && !Elts.empty())
      Desired = llvm::ArrayType::get(Desired->getElementType(), Elts.size());

    return EmitArrayConstant(CGM, Desired, CommonElementType, NumElements, Elts,
                             Filler);
  }
  case APValue::MemberPointer:
    return CGM.getCXXABI().EmitMemberPointer(Value, DestType);
  }
  llvm_unreachable("Unknown APValue kind");
}

llvm::GlobalVariable *CodeGenModule::getAddrOfConstantCompoundLiteralIfEmitted(
    const CompoundLiteralExpr *E) {
  return EmittedCompoundLiterals.lookup(E);
}

void CodeGenModule::setAddrOfConstantCompoundLiteral(
    const CompoundLiteralExpr *CLE, llvm::GlobalVariable *GV) {
  bool Ok = EmittedCompoundLiterals.insert(std::make_pair(CLE, GV)).second;
  (void)Ok;
  assert(Ok && "CLE has already been emitted!");
}

ConstantAddress
CodeGenModule::GetAddrOfConstantCompoundLiteral(const CompoundLiteralExpr *E) {
  assert(E->isFileScope() && "not a file-scope compound literal expr");
  ConstantEmitter emitter(*this);
  return tryEmitGlobalCompoundLiteral(emitter, E);
}

llvm::Constant *
CodeGenModule::getMemberPointerConstant(const UnaryOperator *uo) {
  // Member pointer constants always have a very particular form.
  const MemberPointerType *type = cast<MemberPointerType>(uo->getType());
  const ValueDecl *decl = cast<DeclRefExpr>(uo->getSubExpr())->getDecl();

  // A member function pointer.
  if (const CXXMethodDecl *method = dyn_cast<CXXMethodDecl>(decl))
    return getCXXABI().EmitMemberFunctionPointer(method);

  // Otherwise, a member data pointer.
  uint64_t fieldOffset = getContext().getFieldOffset(decl);
  CharUnits chars = getContext().toCharUnitsFromBits((int64_t) fieldOffset);
  return getCXXABI().EmitMemberDataPointer(type, chars);
}

static llvm::Constant *EmitNullConstantForBase(CodeGenModule &CGM,
                                               llvm::Type *baseType,
                                               const CXXRecordDecl *base);

static llvm::Constant *EmitNullConstant(CodeGenModule &CGM,
                                        const RecordDecl *record,
                                        bool asCompleteObject) {
  const CGRecordLayout &layout = CGM.getTypes().getCGRecordLayout(record);
  llvm::StructType *structure =
    (asCompleteObject ? layout.getLLVMType()
                      : layout.getBaseSubobjectLLVMType());

  unsigned numElements = structure->getNumElements();
  std::vector<llvm::Constant *> elements(numElements);

  auto CXXR = dyn_cast<CXXRecordDecl>(record);
  // Fill in all the bases.
  if (CXXR) {
    for (const auto &I : CXXR->bases()) {
      if (I.isVirtual()) {
        // Ignore virtual bases; if we're laying out for a complete
        // object, we'll lay these out later.
        continue;
      }

      const CXXRecordDecl *base =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

      // Ignore empty bases.
      if (isEmptyRecordForLayout(CGM.getContext(), I.getType()) ||
          CGM.getContext()
              .getASTRecordLayout(base)
              .getNonVirtualSize()
              .isZero())
        continue;

      unsigned fieldIndex = layout.getNonVirtualBaseLLVMFieldNo(base);
      llvm::Type *baseType = structure->getElementType(fieldIndex);
      elements[fieldIndex] = EmitNullConstantForBase(CGM, baseType, base);
    }
  }

  // Fill in all the fields.
  for (const auto *Field : record->fields()) {
    // Fill in non-bitfields. (Bitfields always use a zero pattern, which we
    // will fill in later.)
    if (!Field->isBitField() &&
        !isEmptyFieldForLayout(CGM.getContext(), Field)) {
      unsigned fieldIndex = layout.getLLVMFieldNo(Field);
      elements[fieldIndex] = CGM.EmitNullConstant(Field->getType());
    }

    // For unions, stop after the first named field.
    if (record->isUnion()) {
      if (Field->getIdentifier())
        break;
      if (const auto *FieldRD = Field->getType()->getAsRecordDecl())
        if (FieldRD->findFirstNamedDataMember())
          break;
    }
  }

  // Fill in the virtual bases, if we're working with the complete object.
  if (CXXR && asCompleteObject) {
    for (const auto &I : CXXR->vbases()) {
      const CXXRecordDecl *base =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

      // Ignore empty bases.
      if (isEmptyRecordForLayout(CGM.getContext(), I.getType()))
        continue;

      unsigned fieldIndex = layout.getVirtualBaseIndex(base);

      // We might have already laid this field out.
      if (elements[fieldIndex]) continue;

      llvm::Type *baseType = structure->getElementType(fieldIndex);
      elements[fieldIndex] = EmitNullConstantForBase(CGM, baseType, base);
    }
  }

  // Now go through all other fields and zero them out.
  for (unsigned i = 0; i != numElements; ++i) {
    if (!elements[i])
      elements[i] = llvm::Constant::getNullValue(structure->getElementType(i));
  }

  return llvm::ConstantStruct::get(structure, elements);
}

/// Emit the null constant for a base subobject.
static llvm::Constant *EmitNullConstantForBase(CodeGenModule &CGM,
                                               llvm::Type *baseType,
                                               const CXXRecordDecl *base) {
  const CGRecordLayout &baseLayout = CGM.getTypes().getCGRecordLayout(base);

  // Just zero out bases that don't have any pointer to data members.
  if (baseLayout.isZeroInitializableAsBase())
    return llvm::Constant::getNullValue(baseType);

  // Otherwise, we can just use its null constant.
  return EmitNullConstant(CGM, base, /*asCompleteObject=*/false);
}

llvm::Constant *ConstantEmitter::emitNullForMemory(CodeGenModule &CGM,
                                                   QualType T) {
  return emitForMemory(CGM, CGM.EmitNullConstant(T), T);
}

llvm::Constant *CodeGenModule::EmitNullConstant(QualType T) {
  if (T->getAs<PointerType>())
    return getNullPointer(
        cast<llvm::PointerType>(getTypes().ConvertTypeForMem(T)), T);

  if (getTypes().isZeroInitializable(T))
    return llvm::Constant::getNullValue(getTypes().ConvertTypeForMem(T));

  if (const ConstantArrayType *CAT = Context.getAsConstantArrayType(T)) {
    llvm::ArrayType *ATy =
      cast<llvm::ArrayType>(getTypes().ConvertTypeForMem(T));

    QualType ElementTy = CAT->getElementType();

    llvm::Constant *Element =
      ConstantEmitter::emitNullForMemory(*this, ElementTy);
    unsigned NumElements = CAT->getZExtSize();
    SmallVector<llvm::Constant *, 8> Array(NumElements, Element);
    return llvm::ConstantArray::get(ATy, Array);
  }

  if (const RecordType *RT = T->getAs<RecordType>())
    return ::EmitNullConstant(*this, RT->getDecl(), /*complete object*/ true);

  assert(T->isMemberDataPointerType() &&
         "Should only see pointers to data members here!");

  return getCXXABI().EmitNullMemberPointer(T->castAs<MemberPointerType>());
}

llvm::Constant *
CodeGenModule::EmitNullConstantForBase(const CXXRecordDecl *Record) {
  return ::EmitNullConstant(*this, Record, false);
}
