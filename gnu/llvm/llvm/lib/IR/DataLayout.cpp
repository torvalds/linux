//===- DataLayout.cpp - Data size & alignment routines ---------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines layout properties related to datatype size/offset/alignment
// information.
//
// This structure should be created once, filled in if the defaults are not
// correct and then passed around by const&.  None of the members functions
// require modification to the object.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemAlloc.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Support for StructLayout
//===----------------------------------------------------------------------===//

StructLayout::StructLayout(StructType *ST, const DataLayout &DL)
    : StructSize(TypeSize::getFixed(0)) {
  assert(!ST->isOpaque() && "Cannot get layout of opaque structs");
  IsPadded = false;
  NumElements = ST->getNumElements();

  // Loop over each of the elements, placing them in memory.
  for (unsigned i = 0, e = NumElements; i != e; ++i) {
    Type *Ty = ST->getElementType(i);
    if (i == 0 && Ty->isScalableTy())
      StructSize = TypeSize::getScalable(0);

    const Align TyAlign = ST->isPacked() ? Align(1) : DL.getABITypeAlign(Ty);

    // Add padding if necessary to align the data element properly.
    // Currently the only structure with scalable size will be the homogeneous
    // scalable vector types. Homogeneous scalable vector types have members of
    // the same data type so no alignment issue will happen. The condition here
    // assumes so and needs to be adjusted if this assumption changes (e.g. we
    // support structures with arbitrary scalable data type, or structure that
    // contains both fixed size and scalable size data type members).
    if (!StructSize.isScalable() && !isAligned(TyAlign, StructSize)) {
      IsPadded = true;
      StructSize = TypeSize::getFixed(alignTo(StructSize, TyAlign));
    }

    // Keep track of maximum alignment constraint.
    StructAlignment = std::max(TyAlign, StructAlignment);

    getMemberOffsets()[i] = StructSize;
    // Consume space for this data item
    StructSize += DL.getTypeAllocSize(Ty);
  }

  // Add padding to the end of the struct so that it could be put in an array
  // and all array elements would be aligned correctly.
  if (!StructSize.isScalable() && !isAligned(StructAlignment, StructSize)) {
    IsPadded = true;
    StructSize = TypeSize::getFixed(alignTo(StructSize, StructAlignment));
  }
}

/// getElementContainingOffset - Given a valid offset into the structure,
/// return the structure index that contains it.
unsigned StructLayout::getElementContainingOffset(uint64_t FixedOffset) const {
  assert(!StructSize.isScalable() &&
         "Cannot get element at offset for structure containing scalable "
         "vector types");
  TypeSize Offset = TypeSize::getFixed(FixedOffset);
  ArrayRef<TypeSize> MemberOffsets = getMemberOffsets();

  const auto *SI =
      std::upper_bound(MemberOffsets.begin(), MemberOffsets.end(), Offset,
                       [](TypeSize LHS, TypeSize RHS) -> bool {
                         return TypeSize::isKnownLT(LHS, RHS);
                       });
  assert(SI != MemberOffsets.begin() && "Offset not in structure type!");
  --SI;
  assert(TypeSize::isKnownLE(*SI, Offset) && "upper_bound didn't work");
  assert(
      (SI == MemberOffsets.begin() || TypeSize::isKnownLE(*(SI - 1), Offset)) &&
      (SI + 1 == MemberOffsets.end() ||
       TypeSize::isKnownGT(*(SI + 1), Offset)) &&
      "Upper bound didn't work!");

  // Multiple fields can have the same offset if any of them are zero sized.
  // For example, in { i32, [0 x i32], i32 }, searching for offset 4 will stop
  // at the i32 element, because it is the last element at that offset.  This is
  // the right one to return, because anything after it will have a higher
  // offset, implying that this element is non-empty.
  return SI - MemberOffsets.begin();
}

//===----------------------------------------------------------------------===//
// LayoutAlignElem, LayoutAlign support
//===----------------------------------------------------------------------===//

LayoutAlignElem LayoutAlignElem::get(Align ABIAlign, Align PrefAlign,
                                     uint32_t BitWidth) {
  assert(ABIAlign <= PrefAlign && "Preferred alignment worse than ABI!");
  LayoutAlignElem retval;
  retval.ABIAlign = ABIAlign;
  retval.PrefAlign = PrefAlign;
  retval.TypeBitWidth = BitWidth;
  return retval;
}

bool LayoutAlignElem::operator==(const LayoutAlignElem &rhs) const {
  return ABIAlign == rhs.ABIAlign && PrefAlign == rhs.PrefAlign &&
         TypeBitWidth == rhs.TypeBitWidth;
}

//===----------------------------------------------------------------------===//
// PointerAlignElem, PointerAlign support
//===----------------------------------------------------------------------===//

PointerAlignElem PointerAlignElem::getInBits(uint32_t AddressSpace,
                                             Align ABIAlign, Align PrefAlign,
                                             uint32_t TypeBitWidth,
                                             uint32_t IndexBitWidth) {
  assert(ABIAlign <= PrefAlign && "Preferred alignment worse than ABI!");
  PointerAlignElem retval;
  retval.AddressSpace = AddressSpace;
  retval.ABIAlign = ABIAlign;
  retval.PrefAlign = PrefAlign;
  retval.TypeBitWidth = TypeBitWidth;
  retval.IndexBitWidth = IndexBitWidth;
  return retval;
}

bool
PointerAlignElem::operator==(const PointerAlignElem &rhs) const {
  return (ABIAlign == rhs.ABIAlign && AddressSpace == rhs.AddressSpace &&
          PrefAlign == rhs.PrefAlign && TypeBitWidth == rhs.TypeBitWidth &&
          IndexBitWidth == rhs.IndexBitWidth);
}

//===----------------------------------------------------------------------===//
//                       DataLayout Class Implementation
//===----------------------------------------------------------------------===//

const char *DataLayout::getManglingComponent(const Triple &T) {
  if (T.isOSBinFormatGOFF())
    return "-m:l";
  if (T.isOSBinFormatMachO())
    return "-m:o";
  if ((T.isOSWindows() || T.isUEFI()) && T.isOSBinFormatCOFF())
    return T.getArch() == Triple::x86 ? "-m:x" : "-m:w";
  if (T.isOSBinFormatXCOFF())
    return "-m:a";
  return "-m:e";
}

static const std::pair<AlignTypeEnum, LayoutAlignElem> DefaultAlignments[] = {
    {INTEGER_ALIGN, {1, Align(1), Align(1)}},    // i1
    {INTEGER_ALIGN, {8, Align(1), Align(1)}},    // i8
    {INTEGER_ALIGN, {16, Align(2), Align(2)}},   // i16
    {INTEGER_ALIGN, {32, Align(4), Align(4)}},   // i32
    {INTEGER_ALIGN, {64, Align(4), Align(8)}},   // i64
    {FLOAT_ALIGN, {16, Align(2), Align(2)}},     // half, bfloat
    {FLOAT_ALIGN, {32, Align(4), Align(4)}},     // float
    {FLOAT_ALIGN, {64, Align(8), Align(8)}},     // double
    {FLOAT_ALIGN, {128, Align(16), Align(16)}},  // ppcf128, quad, ...
    {VECTOR_ALIGN, {64, Align(8), Align(8)}},    // v2i32, v1i64, ...
    {VECTOR_ALIGN, {128, Align(16), Align(16)}}, // v16i8, v8i16, v4i32, ...
};

void DataLayout::reset(StringRef Desc) {
  clear();

  LayoutMap = nullptr;
  BigEndian = false;
  AllocaAddrSpace = 0;
  StackNaturalAlign.reset();
  ProgramAddrSpace = 0;
  DefaultGlobalsAddrSpace = 0;
  FunctionPtrAlign.reset();
  TheFunctionPtrAlignType = FunctionPtrAlignType::Independent;
  ManglingMode = MM_None;
  NonIntegralAddressSpaces.clear();
  StructAlignment = LayoutAlignElem::get(Align(1), Align(8), 0);

  // Default alignments
  for (const auto &[Kind, Layout] : DefaultAlignments) {
    if (Error Err = setAlignment(Kind, Layout.ABIAlign, Layout.PrefAlign,
                                 Layout.TypeBitWidth))
      return report_fatal_error(std::move(Err));
  }
  if (Error Err = setPointerAlignmentInBits(0, Align(8), Align(8), 64, 64))
    return report_fatal_error(std::move(Err));

  if (Error Err = parseSpecifier(Desc))
    return report_fatal_error(std::move(Err));
}

Expected<DataLayout> DataLayout::parse(StringRef LayoutDescription) {
  DataLayout Layout("");
  if (Error Err = Layout.parseSpecifier(LayoutDescription))
    return std::move(Err);
  return Layout;
}

static Error reportError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

/// Checked version of split, to ensure mandatory subparts.
static Error split(StringRef Str, char Separator,
                   std::pair<StringRef, StringRef> &Split) {
  assert(!Str.empty() && "parse error, string can't be empty here");
  Split = Str.split(Separator);
  if (Split.second.empty() && Split.first != Str)
    return reportError("Trailing separator in datalayout string");
  if (!Split.second.empty() && Split.first.empty())
    return reportError("Expected token before separator in datalayout string");
  return Error::success();
}

/// Get an unsigned integer, including error checks.
template <typename IntTy> static Error getInt(StringRef R, IntTy &Result) {
  bool error = R.getAsInteger(10, Result); (void)error;
  if (error)
    return reportError("not a number, or does not fit in an unsigned int");
  return Error::success();
}

/// Get an unsigned integer representing the number of bits and convert it into
/// bytes. Error out of not a byte width multiple.
template <typename IntTy>
static Error getIntInBytes(StringRef R, IntTy &Result) {
  if (Error Err = getInt<IntTy>(R, Result))
    return Err;
  if (Result % 8)
    return reportError("number of bits must be a byte width multiple");
  Result /= 8;
  return Error::success();
}

static Error getAddrSpace(StringRef R, unsigned &AddrSpace) {
  if (Error Err = getInt(R, AddrSpace))
    return Err;
  if (!isUInt<24>(AddrSpace))
    return reportError("Invalid address space, must be a 24-bit integer");
  return Error::success();
}

Error DataLayout::parseSpecifier(StringRef Desc) {
  StringRepresentation = std::string(Desc);
  while (!Desc.empty()) {
    // Split at '-'.
    std::pair<StringRef, StringRef> Split;
    if (Error Err = ::split(Desc, '-', Split))
      return Err;
    Desc = Split.second;

    // Split at ':'.
    if (Error Err = ::split(Split.first, ':', Split))
      return Err;

    // Aliases used below.
    StringRef &Tok  = Split.first;  // Current token.
    StringRef &Rest = Split.second; // The rest of the string.

    if (Tok == "ni") {
      do {
        if (Error Err = ::split(Rest, ':', Split))
          return Err;
        Rest = Split.second;
        unsigned AS;
        if (Error Err = getInt(Split.first, AS))
          return Err;
        if (AS == 0)
          return reportError("Address space 0 can never be non-integral");
        NonIntegralAddressSpaces.push_back(AS);
      } while (!Rest.empty());

      continue;
    }

    char Specifier = Tok.front();
    Tok = Tok.substr(1);

    switch (Specifier) {
    case 's':
      // Deprecated, but ignoring here to preserve loading older textual llvm
      // ASM file
      break;
    case 'E':
      BigEndian = true;
      break;
    case 'e':
      BigEndian = false;
      break;
    case 'p': {
      // Address space.
      unsigned AddrSpace = 0;
      if (!Tok.empty())
        if (Error Err = getInt(Tok, AddrSpace))
          return Err;
      if (!isUInt<24>(AddrSpace))
        return reportError("Invalid address space, must be a 24-bit integer");

      // Size.
      if (Rest.empty())
        return reportError(
            "Missing size specification for pointer in datalayout string");
      if (Error Err = ::split(Rest, ':', Split))
        return Err;
      unsigned PointerMemSize;
      if (Error Err = getInt(Tok, PointerMemSize))
        return Err;
      if (!PointerMemSize)
        return reportError("Invalid pointer size of 0 bytes");

      // ABI alignment.
      if (Rest.empty())
        return reportError(
            "Missing alignment specification for pointer in datalayout string");
      if (Error Err = ::split(Rest, ':', Split))
        return Err;
      unsigned PointerABIAlign;
      if (Error Err = getIntInBytes(Tok, PointerABIAlign))
        return Err;
      if (!isPowerOf2_64(PointerABIAlign))
        return reportError("Pointer ABI alignment must be a power of 2");

      // Size of index used in GEP for address calculation.
      // The parameter is optional. By default it is equal to size of pointer.
      unsigned IndexSize = PointerMemSize;

      // Preferred alignment.
      unsigned PointerPrefAlign = PointerABIAlign;
      if (!Rest.empty()) {
        if (Error Err = ::split(Rest, ':', Split))
          return Err;
        if (Error Err = getIntInBytes(Tok, PointerPrefAlign))
          return Err;
        if (!isPowerOf2_64(PointerPrefAlign))
          return reportError(
              "Pointer preferred alignment must be a power of 2");

        // Now read the index. It is the second optional parameter here.
        if (!Rest.empty()) {
          if (Error Err = ::split(Rest, ':', Split))
            return Err;
          if (Error Err = getInt(Tok, IndexSize))
            return Err;
          if (!IndexSize)
            return reportError("Invalid index size of 0 bytes");
        }
      }
      if (Error Err = setPointerAlignmentInBits(
              AddrSpace, assumeAligned(PointerABIAlign),
              assumeAligned(PointerPrefAlign), PointerMemSize, IndexSize))
        return Err;
      break;
    }
    case 'i':
    case 'v':
    case 'f':
    case 'a': {
      AlignTypeEnum AlignType;
      switch (Specifier) {
      default: llvm_unreachable("Unexpected specifier!");
      case 'i': AlignType = INTEGER_ALIGN; break;
      case 'v': AlignType = VECTOR_ALIGN; break;
      case 'f': AlignType = FLOAT_ALIGN; break;
      case 'a': AlignType = AGGREGATE_ALIGN; break;
      }

      // Bit size.
      unsigned Size = 0;
      if (!Tok.empty())
        if (Error Err = getInt(Tok, Size))
          return Err;

      if (AlignType == AGGREGATE_ALIGN && Size != 0)
        return reportError(
            "Sized aggregate specification in datalayout string");

      // ABI alignment.
      if (Rest.empty())
        return reportError(
            "Missing alignment specification in datalayout string");
      if (Error Err = ::split(Rest, ':', Split))
        return Err;
      unsigned ABIAlign;
      if (Error Err = getIntInBytes(Tok, ABIAlign))
        return Err;
      if (AlignType != AGGREGATE_ALIGN && !ABIAlign)
        return reportError(
            "ABI alignment specification must be >0 for non-aggregate types");

      if (!isUInt<16>(ABIAlign))
        return reportError("Invalid ABI alignment, must be a 16bit integer");
      if (ABIAlign != 0 && !isPowerOf2_64(ABIAlign))
        return reportError("Invalid ABI alignment, must be a power of 2");
      if (AlignType == INTEGER_ALIGN && Size == 8 && ABIAlign != 1)
        return reportError(
            "Invalid ABI alignment, i8 must be naturally aligned");

      // Preferred alignment.
      unsigned PrefAlign = ABIAlign;
      if (!Rest.empty()) {
        if (Error Err = ::split(Rest, ':', Split))
          return Err;
        if (Error Err = getIntInBytes(Tok, PrefAlign))
          return Err;
      }

      if (!isUInt<16>(PrefAlign))
        return reportError(
            "Invalid preferred alignment, must be a 16bit integer");
      if (PrefAlign != 0 && !isPowerOf2_64(PrefAlign))
        return reportError("Invalid preferred alignment, must be a power of 2");

      if (Error Err = setAlignment(AlignType, assumeAligned(ABIAlign),
                                   assumeAligned(PrefAlign), Size))
        return Err;

      break;
    }
    case 'n':  // Native integer types.
      while (true) {
        unsigned Width;
        if (Error Err = getInt(Tok, Width))
          return Err;
        if (Width == 0)
          return reportError(
              "Zero width native integer type in datalayout string");
        LegalIntWidths.push_back(Width);
        if (Rest.empty())
          break;
        if (Error Err = ::split(Rest, ':', Split))
          return Err;
      }
      break;
    case 'S': { // Stack natural alignment.
      uint64_t Alignment;
      if (Error Err = getIntInBytes(Tok, Alignment))
        return Err;
      if (Alignment != 0 && !llvm::isPowerOf2_64(Alignment))
        return reportError("Alignment is neither 0 nor a power of 2");
      StackNaturalAlign = MaybeAlign(Alignment);
      break;
    }
    case 'F': {
      switch (Tok.front()) {
      case 'i':
        TheFunctionPtrAlignType = FunctionPtrAlignType::Independent;
        break;
      case 'n':
        TheFunctionPtrAlignType = FunctionPtrAlignType::MultipleOfFunctionAlign;
        break;
      default:
        return reportError("Unknown function pointer alignment type in "
                           "datalayout string");
      }
      Tok = Tok.substr(1);
      uint64_t Alignment;
      if (Error Err = getIntInBytes(Tok, Alignment))
        return Err;
      if (Alignment != 0 && !llvm::isPowerOf2_64(Alignment))
        return reportError("Alignment is neither 0 nor a power of 2");
      FunctionPtrAlign = MaybeAlign(Alignment);
      break;
    }
    case 'P': { // Function address space.
      if (Error Err = getAddrSpace(Tok, ProgramAddrSpace))
        return Err;
      break;
    }
    case 'A': { // Default stack/alloca address space.
      if (Error Err = getAddrSpace(Tok, AllocaAddrSpace))
        return Err;
      break;
    }
    case 'G': { // Default address space for global variables.
      if (Error Err = getAddrSpace(Tok, DefaultGlobalsAddrSpace))
        return Err;
      break;
    }
    case 'm':
      if (!Tok.empty())
        return reportError("Unexpected trailing characters after mangling "
                           "specifier in datalayout string");
      if (Rest.empty())
        return reportError("Expected mangling specifier in datalayout string");
      if (Rest.size() > 1)
        return reportError("Unknown mangling specifier in datalayout string");
      switch(Rest[0]) {
      default:
        return reportError("Unknown mangling in datalayout string");
      case 'e':
        ManglingMode = MM_ELF;
        break;
      case 'l':
        ManglingMode = MM_GOFF;
        break;
      case 'o':
        ManglingMode = MM_MachO;
        break;
      case 'm':
        ManglingMode = MM_Mips;
        break;
      case 'w':
        ManglingMode = MM_WinCOFF;
        break;
      case 'x':
        ManglingMode = MM_WinCOFFX86;
        break;
      case 'a':
        ManglingMode = MM_XCOFF;
        break;
      }
      break;
    default:
      return reportError("Unknown specifier in datalayout string");
      break;
    }
  }

  return Error::success();
}

DataLayout::DataLayout(const Module *M) {
  init(M);
}

void DataLayout::init(const Module *M) { *this = M->getDataLayout(); }

bool DataLayout::operator==(const DataLayout &Other) const {
  bool Ret = BigEndian == Other.BigEndian &&
             AllocaAddrSpace == Other.AllocaAddrSpace &&
             StackNaturalAlign == Other.StackNaturalAlign &&
             ProgramAddrSpace == Other.ProgramAddrSpace &&
             DefaultGlobalsAddrSpace == Other.DefaultGlobalsAddrSpace &&
             FunctionPtrAlign == Other.FunctionPtrAlign &&
             TheFunctionPtrAlignType == Other.TheFunctionPtrAlignType &&
             ManglingMode == Other.ManglingMode &&
             LegalIntWidths == Other.LegalIntWidths &&
             IntAlignments == Other.IntAlignments &&
             FloatAlignments == Other.FloatAlignments &&
             VectorAlignments == Other.VectorAlignments &&
             StructAlignment == Other.StructAlignment &&
             Pointers == Other.Pointers;
  // Note: getStringRepresentation() might differs, it is not canonicalized
  return Ret;
}

static SmallVectorImpl<LayoutAlignElem>::const_iterator
findAlignmentLowerBound(const SmallVectorImpl<LayoutAlignElem> &Alignments,
                        uint32_t BitWidth) {
  return partition_point(Alignments, [BitWidth](const LayoutAlignElem &E) {
    return E.TypeBitWidth < BitWidth;
  });
}

Error DataLayout::setAlignment(AlignTypeEnum AlignType, Align ABIAlign,
                               Align PrefAlign, uint32_t BitWidth) {
  // AlignmentsTy::ABIAlign and AlignmentsTy::PrefAlign were once stored as
  // uint16_t, it is unclear if there are requirements for alignment to be less
  // than 2^16 other than storage. In the meantime we leave the restriction as
  // an assert. See D67400 for context.
  assert(Log2(ABIAlign) < 16 && Log2(PrefAlign) < 16 && "Alignment too big");
  if (!isUInt<24>(BitWidth))
    return reportError("Invalid bit width, must be a 24-bit integer");
  if (PrefAlign < ABIAlign)
    return reportError(
        "Preferred alignment cannot be less than the ABI alignment");

  SmallVectorImpl<LayoutAlignElem> *Alignments;
  switch (AlignType) {
  case AGGREGATE_ALIGN:
    StructAlignment.ABIAlign = ABIAlign;
    StructAlignment.PrefAlign = PrefAlign;
    return Error::success();
  case INTEGER_ALIGN:
    Alignments = &IntAlignments;
    break;
  case FLOAT_ALIGN:
    Alignments = &FloatAlignments;
    break;
  case VECTOR_ALIGN:
    Alignments = &VectorAlignments;
    break;
  }

  auto I = partition_point(*Alignments, [BitWidth](const LayoutAlignElem &E) {
    return E.TypeBitWidth < BitWidth;
  });
  if (I != Alignments->end() && I->TypeBitWidth == BitWidth) {
    // Update the abi, preferred alignments.
    I->ABIAlign = ABIAlign;
    I->PrefAlign = PrefAlign;
  } else {
    // Insert before I to keep the vector sorted.
    Alignments->insert(I, LayoutAlignElem::get(ABIAlign, PrefAlign, BitWidth));
  }
  return Error::success();
}

const PointerAlignElem &
DataLayout::getPointerAlignElem(uint32_t AddressSpace) const {
  if (AddressSpace != 0) {
    auto I = lower_bound(Pointers, AddressSpace,
                         [](const PointerAlignElem &A, uint32_t AddressSpace) {
      return A.AddressSpace < AddressSpace;
    });
    if (I != Pointers.end() && I->AddressSpace == AddressSpace)
      return *I;
  }

  assert(Pointers[0].AddressSpace == 0);
  return Pointers[0];
}

Error DataLayout::setPointerAlignmentInBits(uint32_t AddrSpace, Align ABIAlign,
                                            Align PrefAlign,
                                            uint32_t TypeBitWidth,
                                            uint32_t IndexBitWidth) {
  if (PrefAlign < ABIAlign)
    return reportError(
        "Preferred alignment cannot be less than the ABI alignment");
  if (IndexBitWidth > TypeBitWidth)
    return reportError("Index width cannot be larger than pointer width");

  auto I = lower_bound(Pointers, AddrSpace,
                       [](const PointerAlignElem &A, uint32_t AddressSpace) {
    return A.AddressSpace < AddressSpace;
  });
  if (I == Pointers.end() || I->AddressSpace != AddrSpace) {
    Pointers.insert(I,
                    PointerAlignElem::getInBits(AddrSpace, ABIAlign, PrefAlign,
                                                TypeBitWidth, IndexBitWidth));
  } else {
    I->ABIAlign = ABIAlign;
    I->PrefAlign = PrefAlign;
    I->TypeBitWidth = TypeBitWidth;
    I->IndexBitWidth = IndexBitWidth;
  }
  return Error::success();
}

Align DataLayout::getIntegerAlignment(uint32_t BitWidth,
                                      bool abi_or_pref) const {
  auto I = findAlignmentLowerBound(IntAlignments, BitWidth);
  // If we don't have an exact match, use alignment of next larger integer
  // type. If there is none, use alignment of largest integer type by going
  // back one element.
  if (I == IntAlignments.end())
    --I;
  return abi_or_pref ? I->ABIAlign : I->PrefAlign;
}

namespace {

class StructLayoutMap {
  using LayoutInfoTy = DenseMap<StructType*, StructLayout*>;
  LayoutInfoTy LayoutInfo;

public:
  ~StructLayoutMap() {
    // Remove any layouts.
    for (const auto &I : LayoutInfo) {
      StructLayout *Value = I.second;
      Value->~StructLayout();
      free(Value);
    }
  }

  StructLayout *&operator[](StructType *STy) {
    return LayoutInfo[STy];
  }
};

} // end anonymous namespace

void DataLayout::clear() {
  LegalIntWidths.clear();
  IntAlignments.clear();
  FloatAlignments.clear();
  VectorAlignments.clear();
  Pointers.clear();
  delete static_cast<StructLayoutMap *>(LayoutMap);
  LayoutMap = nullptr;
}

DataLayout::~DataLayout() {
  clear();
}

const StructLayout *DataLayout::getStructLayout(StructType *Ty) const {
  if (!LayoutMap)
    LayoutMap = new StructLayoutMap();

  StructLayoutMap *STM = static_cast<StructLayoutMap*>(LayoutMap);
  StructLayout *&SL = (*STM)[Ty];
  if (SL) return SL;

  // Otherwise, create the struct layout.  Because it is variable length, we
  // malloc it, then use placement new.
  StructLayout *L = (StructLayout *)safe_malloc(
      StructLayout::totalSizeToAlloc<TypeSize>(Ty->getNumElements()));

  // Set SL before calling StructLayout's ctor.  The ctor could cause other
  // entries to be added to TheMap, invalidating our reference.
  SL = L;

  new (L) StructLayout(Ty, *this);

  return L;
}

Align DataLayout::getPointerABIAlignment(unsigned AS) const {
  return getPointerAlignElem(AS).ABIAlign;
}

Align DataLayout::getPointerPrefAlignment(unsigned AS) const {
  return getPointerAlignElem(AS).PrefAlign;
}

unsigned DataLayout::getPointerSize(unsigned AS) const {
  return divideCeil(getPointerAlignElem(AS).TypeBitWidth, 8);
}

unsigned DataLayout::getMaxIndexSize() const {
  unsigned MaxIndexSize = 0;
  for (auto &P : Pointers)
    MaxIndexSize =
        std::max(MaxIndexSize, (unsigned)divideCeil(P.TypeBitWidth, 8));

  return MaxIndexSize;
}

unsigned DataLayout::getPointerTypeSizeInBits(Type *Ty) const {
  assert(Ty->isPtrOrPtrVectorTy() &&
         "This should only be called with a pointer or pointer vector type");
  Ty = Ty->getScalarType();
  return getPointerSizeInBits(cast<PointerType>(Ty)->getAddressSpace());
}

unsigned DataLayout::getIndexSize(unsigned AS) const {
  return divideCeil(getPointerAlignElem(AS).IndexBitWidth, 8);
}

unsigned DataLayout::getIndexTypeSizeInBits(Type *Ty) const {
  assert(Ty->isPtrOrPtrVectorTy() &&
         "This should only be called with a pointer or pointer vector type");
  Ty = Ty->getScalarType();
  return getIndexSizeInBits(cast<PointerType>(Ty)->getAddressSpace());
}

/*!
  \param abi_or_pref Flag that determines which alignment is returned. true
  returns the ABI alignment, false returns the preferred alignment.
  \param Ty The underlying type for which alignment is determined.

  Get the ABI (\a abi_or_pref == true) or preferred alignment (\a abi_or_pref
  == false) for the requested type \a Ty.
 */
Align DataLayout::getAlignment(Type *Ty, bool abi_or_pref) const {
  assert(Ty->isSized() && "Cannot getTypeInfo() on a type that is unsized!");
  switch (Ty->getTypeID()) {
  // Early escape for the non-numeric types.
  case Type::LabelTyID:
    return abi_or_pref ? getPointerABIAlignment(0) : getPointerPrefAlignment(0);
  case Type::PointerTyID: {
    unsigned AS = cast<PointerType>(Ty)->getAddressSpace();
    return abi_or_pref ? getPointerABIAlignment(AS)
                       : getPointerPrefAlignment(AS);
    }
  case Type::ArrayTyID:
    return getAlignment(cast<ArrayType>(Ty)->getElementType(), abi_or_pref);

  case Type::StructTyID: {
    // Packed structure types always have an ABI alignment of one.
    if (cast<StructType>(Ty)->isPacked() && abi_or_pref)
      return Align(1);

    // Get the layout annotation... which is lazily created on demand.
    const StructLayout *Layout = getStructLayout(cast<StructType>(Ty));
    const Align Align =
        abi_or_pref ? StructAlignment.ABIAlign : StructAlignment.PrefAlign;
    return std::max(Align, Layout->getAlignment());
  }
  case Type::IntegerTyID:
    return getIntegerAlignment(Ty->getIntegerBitWidth(), abi_or_pref);
  case Type::HalfTyID:
  case Type::BFloatTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
  // PPC_FP128TyID and FP128TyID have different data contents, but the
  // same size and alignment, so they look the same here.
  case Type::PPC_FP128TyID:
  case Type::FP128TyID:
  case Type::X86_FP80TyID: {
    unsigned BitWidth = getTypeSizeInBits(Ty).getFixedValue();
    auto I = findAlignmentLowerBound(FloatAlignments, BitWidth);
    if (I != FloatAlignments.end() && I->TypeBitWidth == BitWidth)
      return abi_or_pref ? I->ABIAlign : I->PrefAlign;

    // If we still couldn't find a reasonable default alignment, fall back
    // to a simple heuristic that the alignment is the first power of two
    // greater-or-equal to the store size of the type.  This is a reasonable
    // approximation of reality, and if the user wanted something less
    // less conservative, they should have specified it explicitly in the data
    // layout.
    return Align(PowerOf2Ceil(BitWidth / 8));
  }
  case Type::X86_MMXTyID:
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID: {
    unsigned BitWidth = getTypeSizeInBits(Ty).getKnownMinValue();
    auto I = findAlignmentLowerBound(VectorAlignments, BitWidth);
    if (I != VectorAlignments.end() && I->TypeBitWidth == BitWidth)
      return abi_or_pref ? I->ABIAlign : I->PrefAlign;

    // By default, use natural alignment for vector types. This is consistent
    // with what clang and llvm-gcc do.
    //
    // We're only calculating a natural alignment, so it doesn't have to be
    // based on the full size for scalable vectors. Using the minimum element
    // count should be enough here.
    return Align(PowerOf2Ceil(getTypeStoreSize(Ty).getKnownMinValue()));
  }
  case Type::X86_AMXTyID:
    return Align(64);
  case Type::TargetExtTyID: {
    Type *LayoutTy = cast<TargetExtType>(Ty)->getLayoutType();
    return getAlignment(LayoutTy, abi_or_pref);
  }
  default:
    llvm_unreachable("Bad type for getAlignment!!!");
  }
}

Align DataLayout::getABITypeAlign(Type *Ty) const {
  return getAlignment(Ty, true);
}

/// TODO: Remove this function once the transition to Align is over.
uint64_t DataLayout::getPrefTypeAlignment(Type *Ty) const {
  return getPrefTypeAlign(Ty).value();
}

Align DataLayout::getPrefTypeAlign(Type *Ty) const {
  return getAlignment(Ty, false);
}

IntegerType *DataLayout::getIntPtrType(LLVMContext &C,
                                       unsigned AddressSpace) const {
  return IntegerType::get(C, getPointerSizeInBits(AddressSpace));
}

Type *DataLayout::getIntPtrType(Type *Ty) const {
  assert(Ty->isPtrOrPtrVectorTy() &&
         "Expected a pointer or pointer vector type.");
  unsigned NumBits = getPointerTypeSizeInBits(Ty);
  IntegerType *IntTy = IntegerType::get(Ty->getContext(), NumBits);
  if (VectorType *VecTy = dyn_cast<VectorType>(Ty))
    return VectorType::get(IntTy, VecTy);
  return IntTy;
}

Type *DataLayout::getSmallestLegalIntType(LLVMContext &C, unsigned Width) const {
  for (unsigned LegalIntWidth : LegalIntWidths)
    if (Width <= LegalIntWidth)
      return Type::getIntNTy(C, LegalIntWidth);
  return nullptr;
}

unsigned DataLayout::getLargestLegalIntTypeSizeInBits() const {
  auto Max = llvm::max_element(LegalIntWidths);
  return Max != LegalIntWidths.end() ? *Max : 0;
}

IntegerType *DataLayout::getIndexType(LLVMContext &C,
                                      unsigned AddressSpace) const {
  return IntegerType::get(C, getIndexSizeInBits(AddressSpace));
}

Type *DataLayout::getIndexType(Type *Ty) const {
  assert(Ty->isPtrOrPtrVectorTy() &&
         "Expected a pointer or pointer vector type.");
  unsigned NumBits = getIndexTypeSizeInBits(Ty);
  IntegerType *IntTy = IntegerType::get(Ty->getContext(), NumBits);
  if (VectorType *VecTy = dyn_cast<VectorType>(Ty))
    return VectorType::get(IntTy, VecTy);
  return IntTy;
}

int64_t DataLayout::getIndexedOffsetInType(Type *ElemTy,
                                           ArrayRef<Value *> Indices) const {
  int64_t Result = 0;

  generic_gep_type_iterator<Value* const*>
    GTI = gep_type_begin(ElemTy, Indices),
    GTE = gep_type_end(ElemTy, Indices);
  for (; GTI != GTE; ++GTI) {
    Value *Idx = GTI.getOperand();
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      assert(Idx->getType()->isIntegerTy(32) && "Illegal struct idx");
      unsigned FieldNo = cast<ConstantInt>(Idx)->getZExtValue();

      // Get structure layout information...
      const StructLayout *Layout = getStructLayout(STy);

      // Add in the offset, as calculated by the structure layout info...
      Result += Layout->getElementOffset(FieldNo);
    } else {
      if (int64_t ArrayIdx = cast<ConstantInt>(Idx)->getSExtValue())
        Result += ArrayIdx * GTI.getSequentialElementStride(*this);
    }
  }

  return Result;
}

static APInt getElementIndex(TypeSize ElemSize, APInt &Offset) {
  // Skip over scalable or zero size elements. Also skip element sizes larger
  // than the positive index space, because the arithmetic below may not be
  // correct in that case.
  unsigned BitWidth = Offset.getBitWidth();
  if (ElemSize.isScalable() || ElemSize == 0 ||
      !isUIntN(BitWidth - 1, ElemSize)) {
    return APInt::getZero(BitWidth);
  }

  APInt Index = Offset.sdiv(ElemSize);
  Offset -= Index * ElemSize;
  if (Offset.isNegative()) {
    // Prefer a positive remaining offset to allow struct indexing.
    --Index;
    Offset += ElemSize;
    assert(Offset.isNonNegative() && "Remaining offset shouldn't be negative");
  }
  return Index;
}

std::optional<APInt> DataLayout::getGEPIndexForOffset(Type *&ElemTy,
                                                      APInt &Offset) const {
  if (auto *ArrTy = dyn_cast<ArrayType>(ElemTy)) {
    ElemTy = ArrTy->getElementType();
    return getElementIndex(getTypeAllocSize(ElemTy), Offset);
  }

  if (isa<VectorType>(ElemTy)) {
    // Vector GEPs are partially broken (e.g. for overaligned element types),
    // and may be forbidden in the future, so avoid generating GEPs into
    // vectors. See https://discourse.llvm.org/t/67497
    return std::nullopt;
  }

  if (auto *STy = dyn_cast<StructType>(ElemTy)) {
    const StructLayout *SL = getStructLayout(STy);
    uint64_t IntOffset = Offset.getZExtValue();
    if (IntOffset >= SL->getSizeInBytes())
      return std::nullopt;

    unsigned Index = SL->getElementContainingOffset(IntOffset);
    Offset -= SL->getElementOffset(Index);
    ElemTy = STy->getElementType(Index);
    return APInt(32, Index);
  }

  // Non-aggregate type.
  return std::nullopt;
}

SmallVector<APInt> DataLayout::getGEPIndicesForOffset(Type *&ElemTy,
                                                      APInt &Offset) const {
  assert(ElemTy->isSized() && "Element type must be sized");
  SmallVector<APInt> Indices;
  Indices.push_back(getElementIndex(getTypeAllocSize(ElemTy), Offset));
  while (Offset != 0) {
    std::optional<APInt> Index = getGEPIndexForOffset(ElemTy, Offset);
    if (!Index)
      break;
    Indices.push_back(*Index);
  }

  return Indices;
}

/// getPreferredAlign - Return the preferred alignment of the specified global.
/// This includes an explicitly requested alignment (if the global has one).
Align DataLayout::getPreferredAlign(const GlobalVariable *GV) const {
  MaybeAlign GVAlignment = GV->getAlign();
  // If a section is specified, always precisely honor explicit alignment,
  // so we don't insert padding into a section we don't control.
  if (GVAlignment && GV->hasSection())
    return *GVAlignment;

  // If no explicit alignment is specified, compute the alignment based on
  // the IR type. If an alignment is specified, increase it to match the ABI
  // alignment of the IR type.
  //
  // FIXME: Not sure it makes sense to use the alignment of the type if
  // there's already an explicit alignment specification.
  Type *ElemType = GV->getValueType();
  Align Alignment = getPrefTypeAlign(ElemType);
  if (GVAlignment) {
    if (*GVAlignment >= Alignment)
      Alignment = *GVAlignment;
    else
      Alignment = std::max(*GVAlignment, getABITypeAlign(ElemType));
  }

  // If no explicit alignment is specified, and the global is large, increase
  // the alignment to 16.
  // FIXME: Why 16, specifically?
  if (GV->hasInitializer() && !GVAlignment) {
    if (Alignment < Align(16)) {
      // If the global is not external, see if it is large.  If so, give it a
      // larger alignment.
      if (getTypeSizeInBits(ElemType) > 128)
        Alignment = Align(16); // 16-byte alignment.
    }
  }
  return Alignment;
}
