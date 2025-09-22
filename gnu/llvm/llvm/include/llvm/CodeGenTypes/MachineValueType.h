//===- CodeGenTypes/MachineValueType.h - Machine-Level types ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the set of machine-level target independent types which
// legal values in the code generator use.
//
// Constants and properties are defined in ValueTypes.td.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEVALUETYPE_H
#define LLVM_CODEGEN_MACHINEVALUETYPE_H

#include "llvm/ADT/Sequence.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>

namespace llvm {

  class Type;
  class raw_ostream;

  /// Machine Value Type. Every type that is supported natively by some
  /// processor targeted by LLVM occurs here. This means that any legal value
  /// type can be represented by an MVT.
  class MVT {
  public:
    enum SimpleValueType : uint8_t {
      // Simple value types that aren't explicitly part of this enumeration
      // are considered extended value types.
      INVALID_SIMPLE_VALUE_TYPE = 0,

#define GET_VT_ATTR(Ty, n, sz, Any, Int, FP, Vec, Sc, NElem, EltTy) Ty = n,
#define GET_VT_RANGES
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
#undef GET_VT_RANGES

      VALUETYPE_SIZE = LAST_VALUETYPE + 1,
    };

    static_assert(FIRST_VALUETYPE > 0);
    static_assert(LAST_VALUETYPE < token);

    SimpleValueType SimpleTy = INVALID_SIMPLE_VALUE_TYPE;

    constexpr MVT() = default;
    constexpr MVT(SimpleValueType SVT) : SimpleTy(SVT) {}

    bool operator>(const MVT& S)  const { return SimpleTy >  S.SimpleTy; }
    bool operator<(const MVT& S)  const { return SimpleTy <  S.SimpleTy; }
    bool operator==(const MVT& S) const { return SimpleTy == S.SimpleTy; }
    bool operator!=(const MVT& S) const { return SimpleTy != S.SimpleTy; }
    bool operator>=(const MVT& S) const { return SimpleTy >= S.SimpleTy; }
    bool operator<=(const MVT& S) const { return SimpleTy <= S.SimpleTy; }

    /// Support for debugging, callable in GDB: VT.dump()
    void dump() const;

    /// Implement operator<<.
    void print(raw_ostream &OS) const;

    /// Return true if this is a valid simple valuetype.
    bool isValid() const {
      return (SimpleTy >= MVT::FIRST_VALUETYPE &&
              SimpleTy <= MVT::LAST_VALUETYPE);
    }

    /// Return true if this is a FP or a vector FP type.
    bool isFloatingPoint() const {
      return ((SimpleTy >= MVT::FIRST_FP_VALUETYPE &&
               SimpleTy <= MVT::LAST_FP_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_FP_FIXEDLEN_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_FP_FIXEDLEN_VECTOR_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_FP_SCALABLE_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_FP_SCALABLE_VECTOR_VALUETYPE));
    }

    /// Return true if this is an integer or a vector integer type.
    bool isInteger() const {
      return ((SimpleTy >= MVT::FIRST_INTEGER_VALUETYPE &&
               SimpleTy <= MVT::LAST_INTEGER_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE) ||
              (SimpleTy >= MVT::FIRST_INTEGER_SCALABLE_VECTOR_VALUETYPE &&
               SimpleTy <= MVT::LAST_INTEGER_SCALABLE_VECTOR_VALUETYPE));
    }

    /// Return true if this is an integer, not including vectors.
    bool isScalarInteger() const {
      return (SimpleTy >= MVT::FIRST_INTEGER_VALUETYPE &&
              SimpleTy <= MVT::LAST_INTEGER_VALUETYPE);
    }

    /// Return true if this is a vector value type.
    bool isVector() const {
      return (SimpleTy >= MVT::FIRST_VECTOR_VALUETYPE &&
              SimpleTy <= MVT::LAST_VECTOR_VALUETYPE);
    }

    /// Return true if this is a vector value type where the
    /// runtime length is machine dependent
    bool isScalableVector() const {
      return (SimpleTy >= MVT::FIRST_SCALABLE_VECTOR_VALUETYPE &&
              SimpleTy <= MVT::LAST_SCALABLE_VECTOR_VALUETYPE);
    }

    /// Return true if this is a custom target type that has a scalable size.
    bool isScalableTargetExtVT() const {
      return SimpleTy == MVT::aarch64svcount;
    }

    /// Return true if the type is a scalable type.
    bool isScalableVT() const {
      return isScalableVector() || isScalableTargetExtVT();
    }

    bool isFixedLengthVector() const {
      return (SimpleTy >= MVT::FIRST_FIXEDLEN_VECTOR_VALUETYPE &&
              SimpleTy <= MVT::LAST_FIXEDLEN_VECTOR_VALUETYPE);
    }

    /// Return true if this is a 16-bit vector type.
    bool is16BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 16);
    }

    /// Return true if this is a 32-bit vector type.
    bool is32BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 32);
    }

    /// Return true if this is a 64-bit vector type.
    bool is64BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 64);
    }

    /// Return true if this is a 128-bit vector type.
    bool is128BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 128);
    }

    /// Return true if this is a 256-bit vector type.
    bool is256BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 256);
    }

    /// Return true if this is a 512-bit vector type.
    bool is512BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 512);
    }

    /// Return true if this is a 1024-bit vector type.
    bool is1024BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 1024);
    }

    /// Return true if this is a 2048-bit vector type.
    bool is2048BitVector() const {
      return (isFixedLengthVector() && getFixedSizeInBits() == 2048);
    }

    /// Return true if this is an overloaded type for TableGen.
    bool isOverloaded() const {
      switch (SimpleTy) {
#define GET_VT_ATTR(Ty, n, sz, Any, Int, FP, Vec, Sc, NElem, EltTy)          \
    case Ty:                                                                   \
      return Any;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      default:
        return false;
      }
    }

    /// Return a vector with the same number of elements as this vector, but
    /// with the element type converted to an integer type with the same
    /// bitwidth.
    MVT changeVectorElementTypeToInteger() const {
      MVT EltTy = getVectorElementType();
      MVT IntTy = MVT::getIntegerVT(EltTy.getSizeInBits());
      MVT VecTy = MVT::getVectorVT(IntTy, getVectorElementCount());
      assert(VecTy.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE &&
             "Simple vector VT not representable by simple integer vector VT!");
      return VecTy;
    }

    /// Return a VT for a vector type whose attributes match ourselves
    /// with the exception of the element type that is chosen by the caller.
    MVT changeVectorElementType(MVT EltVT) const {
      MVT VecTy = MVT::getVectorVT(EltVT, getVectorElementCount());
      assert(VecTy.SimpleTy != MVT::INVALID_SIMPLE_VALUE_TYPE &&
             "Simple vector VT not representable by simple integer vector VT!");
      return VecTy;
    }

    /// Return the type converted to an equivalently sized integer or vector
    /// with integer element type. Similar to changeVectorElementTypeToInteger,
    /// but also handles scalars.
    MVT changeTypeToInteger() {
      if (isVector())
        return changeVectorElementTypeToInteger();
      return MVT::getIntegerVT(getSizeInBits());
    }

    /// Return a VT for a vector type with the same element type but
    /// half the number of elements.
    MVT getHalfNumVectorElementsVT() const {
      MVT EltVT = getVectorElementType();
      auto EltCnt = getVectorElementCount();
      assert(EltCnt.isKnownEven() && "Splitting vector, but not in half!");
      return getVectorVT(EltVT, EltCnt.divideCoefficientBy(2));
    }

    // Return a VT for a vector type with the same element type but
    // double the number of elements.
    MVT getDoubleNumVectorElementsVT() const {
      MVT EltVT = getVectorElementType();
      auto EltCnt = getVectorElementCount();
      return MVT::getVectorVT(EltVT, EltCnt * 2);
    }

    /// Returns true if the given vector is a power of 2.
    bool isPow2VectorType() const {
      unsigned NElts = getVectorMinNumElements();
      return !(NElts & (NElts - 1));
    }

    /// Widens the length of the given vector MVT up to the nearest power of 2
    /// and returns that type.
    MVT getPow2VectorType() const {
      if (isPow2VectorType())
        return *this;

      ElementCount NElts = getVectorElementCount();
      unsigned NewMinCount = 1 << Log2_32_Ceil(NElts.getKnownMinValue());
      NElts = ElementCount::get(NewMinCount, NElts.isScalable());
      return MVT::getVectorVT(getVectorElementType(), NElts);
    }

    /// If this is a vector, return the element type, otherwise return this.
    MVT getScalarType() const {
      return isVector() ? getVectorElementType() : *this;
    }

    MVT getVectorElementType() const {
      assert(SimpleTy >= FIRST_VALUETYPE && SimpleTy <= LAST_VALUETYPE);
      static constexpr SimpleValueType EltTyTable[] = {
#define GET_VT_ATTR(Ty, N, Sz, Any, Int, FP, Vec, Sc, NElem, EltTy) EltTy,
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      };
      SimpleValueType VT = EltTyTable[SimpleTy - FIRST_VALUETYPE];
      assert(VT != INVALID_SIMPLE_VALUE_TYPE && "Not a vector MVT!");
      return VT;
    }

    /// Given a vector type, return the minimum number of elements it contains.
    unsigned getVectorMinNumElements() const {
      assert(SimpleTy >= FIRST_VALUETYPE && SimpleTy <= LAST_VALUETYPE);
      static constexpr uint16_t NElemTable[] = {
#define GET_VT_ATTR(Ty, N, Sz, Any, Int, FP, Vec, Sc, NElem, EltTy) NElem,
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      };
      unsigned NElem = NElemTable[SimpleTy - FIRST_VALUETYPE];
      assert(NElem != 0 && "Not a vector MVT!");
      return NElem;
    }

    ElementCount getVectorElementCount() const {
      return ElementCount::get(getVectorMinNumElements(), isScalableVector());
    }

    unsigned getVectorNumElements() const {
      if (isScalableVector())
        llvm::reportInvalidSizeRequest(
            "Possible incorrect use of MVT::getVectorNumElements() for "
            "scalable vector. Scalable flag may be dropped, use "
            "MVT::getVectorElementCount() instead");
      return getVectorMinNumElements();
    }

    /// Returns the size of the specified MVT in bits.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    TypeSize getSizeInBits() const {
      static constexpr TypeSize SizeTable[] = {
#define GET_VT_ATTR(Ty, N, Sz, Any, Int, FP, Vec, Sc, NElem, EltTy)          \
    TypeSize(Sz, Sc || Ty == aarch64svcount /* FIXME: Not in the td. */),
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR
      };

      switch (SimpleTy) {
      case INVALID_SIMPLE_VALUE_TYPE:
        llvm_unreachable("getSizeInBits called on extended MVT.");
      case Other:
        llvm_unreachable("Value type is non-standard value, Other.");
      case iPTR:
        llvm_unreachable("Value type size is target-dependent. Ask TLI.");
      case iPTRAny:
      case iAny:
      case fAny:
      case vAny:
      case Any:
        llvm_unreachable("Value type is overloaded.");
      case token:
        llvm_unreachable("Token type is a sentinel that cannot be used "
                         "in codegen and has no size");
      case Metadata:
        llvm_unreachable("Value type is metadata.");
      default:
        assert(SimpleTy < VALUETYPE_SIZE && "Unexpected value type!");
        return SizeTable[SimpleTy - FIRST_VALUETYPE];
      }
    }

    /// Return the size of the specified fixed width value type in bits. The
    /// function will assert if the type is scalable.
    uint64_t getFixedSizeInBits() const {
      return getSizeInBits().getFixedValue();
    }

    uint64_t getScalarSizeInBits() const {
      return getScalarType().getSizeInBits().getFixedValue();
    }

    /// Return the number of bytes overwritten by a store of the specified value
    /// type.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    TypeSize getStoreSize() const {
      TypeSize BaseSize = getSizeInBits();
      return {(BaseSize.getKnownMinValue() + 7) / 8, BaseSize.isScalable()};
    }

    // Return the number of bytes overwritten by a store of this value type or
    // this value type's element type in the case of a vector.
    uint64_t getScalarStoreSize() const {
      return getScalarType().getStoreSize().getFixedValue();
    }

    /// Return the number of bits overwritten by a store of the specified value
    /// type.
    ///
    /// If the value type is a scalable vector type, the scalable property will
    /// be set and the runtime size will be a positive integer multiple of the
    /// base size.
    TypeSize getStoreSizeInBits() const {
      return getStoreSize() * 8;
    }

    /// Returns true if the number of bits for the type is a multiple of an
    /// 8-bit byte.
    bool isByteSized() const { return getSizeInBits().isKnownMultipleOf(8); }

    /// Return true if we know at compile time this has more bits than VT.
    bool knownBitsGT(MVT VT) const {
      return TypeSize::isKnownGT(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has more than or the same
    /// bits as VT.
    bool knownBitsGE(MVT VT) const {
      return TypeSize::isKnownGE(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has fewer bits than VT.
    bool knownBitsLT(MVT VT) const {
      return TypeSize::isKnownLT(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if we know at compile time this has fewer than or the same
    /// bits as VT.
    bool knownBitsLE(MVT VT) const {
      return TypeSize::isKnownLE(getSizeInBits(), VT.getSizeInBits());
    }

    /// Return true if this has more bits than VT.
    bool bitsGT(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsGT(VT);
    }

    /// Return true if this has no less bits than VT.
    bool bitsGE(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsGE(VT);
    }

    /// Return true if this has less bits than VT.
    bool bitsLT(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsLT(VT);
    }

    /// Return true if this has no more bits than VT.
    bool bitsLE(MVT VT) const {
      assert(isScalableVector() == VT.isScalableVector() &&
             "Comparison between scalable and fixed types");
      return knownBitsLE(VT);
    }

    static MVT getFloatingPointVT(unsigned BitWidth) {
#define GET_VT_ATTR(Ty, n, sz, Any, Int, FP, Vec, Sc, NElem, EltTy)          \
    if (FP == 3 && sz == BitWidth)                                             \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR

      llvm_unreachable("Bad bit width!");
    }

    static MVT getIntegerVT(unsigned BitWidth) {
#define GET_VT_ATTR(Ty, n, sz, Any, Int, FP, Vec, Sc, NElem, EltTy)          \
    if (Int == 3 && sz == BitWidth)                                            \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_ATTR

      return (MVT::SimpleValueType)(MVT::INVALID_SIMPLE_VALUE_TYPE);
    }

    static MVT getVectorVT(MVT VT, unsigned NumElements) {
#define GET_VT_VECATTR(Ty, Sc, nElem, ElTy)                                  \
    if (!Sc && VT.SimpleTy == ElTy && NumElements == nElem)                    \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_VECATTR

      return (MVT::SimpleValueType)(MVT::INVALID_SIMPLE_VALUE_TYPE);
    }

    static MVT getScalableVectorVT(MVT VT, unsigned NumElements) {
#define GET_VT_VECATTR(Ty, Sc, nElem, ElTy)                                  \
    if (Sc && VT.SimpleTy == ElTy && NumElements == nElem)                     \
      return Ty;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_VECATTR

      return (MVT::SimpleValueType)(MVT::INVALID_SIMPLE_VALUE_TYPE);
    }

    static MVT getVectorVT(MVT VT, unsigned NumElements, bool IsScalable) {
      if (IsScalable)
        return getScalableVectorVT(VT, NumElements);
      return getVectorVT(VT, NumElements);
    }

    static MVT getVectorVT(MVT VT, ElementCount EC) {
      if (EC.isScalable())
        return getScalableVectorVT(VT, EC.getKnownMinValue());
      return getVectorVT(VT, EC.getKnownMinValue());
    }

    /// Return the value type corresponding to the specified type.
    /// If HandleUnknown is true, unknown types are returned as Other,
    /// otherwise they are invalid.
    /// NB: This includes pointer types, which require a DataLayout to convert
    /// to a concrete value type.
    static MVT getVT(Type *Ty, bool HandleUnknown = false);

  public:
    /// SimpleValueType Iteration
    /// @{
    static auto all_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_VALUETYPE, MVT::LAST_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto integer_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_INTEGER_VALUETYPE,
                                MVT::LAST_INTEGER_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto fp_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FP_VALUETYPE, MVT::LAST_FP_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_VECTOR_VALUETYPE,
                                MVT::LAST_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto fixedlen_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FIXEDLEN_VECTOR_VALUETYPE,
                                MVT::LAST_FIXEDLEN_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto scalable_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_SCALABLE_VECTOR_VALUETYPE,
                                MVT::LAST_SCALABLE_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto integer_fixedlen_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE,
                                MVT::LAST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto fp_fixedlen_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FP_FIXEDLEN_VECTOR_VALUETYPE,
                                MVT::LAST_FP_FIXEDLEN_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto integer_scalable_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_INTEGER_SCALABLE_VECTOR_VALUETYPE,
                                MVT::LAST_INTEGER_SCALABLE_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }

    static auto fp_scalable_vector_valuetypes() {
      return enum_seq_inclusive(MVT::FIRST_FP_SCALABLE_VECTOR_VALUETYPE,
                                MVT::LAST_FP_SCALABLE_VECTOR_VALUETYPE,
                                force_iteration_on_noniterable_enum);
    }
    /// @}
  };

  inline raw_ostream &operator<<(raw_ostream &OS, const MVT &VT) {
    VT.print(OS);
    return OS;
  }

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEVALUETYPE_H
