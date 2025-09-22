//===--- APValue.h - Union class for APFloat/APSInt/Complex -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the APValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_APVALUE_H
#define LLVM_CLANG_AST_APVALUE_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/APFixedPoint.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/AlignOf.h"

namespace clang {
namespace serialization {
template <typename T> class BasicReaderBase;
} // end namespace serialization

  class AddrLabelExpr;
  class ASTContext;
  class CharUnits;
  class CXXRecordDecl;
  class Decl;
  class DiagnosticBuilder;
  class Expr;
  class FieldDecl;
  struct PrintingPolicy;
  class Type;
  class ValueDecl;
  class QualType;

/// Symbolic representation of typeid(T) for some type T.
class TypeInfoLValue {
  const Type *T;

public:
  TypeInfoLValue() : T() {}
  explicit TypeInfoLValue(const Type *T);

  const Type *getType() const { return T; }
  explicit operator bool() const { return T; }

  void *getOpaqueValue() { return const_cast<Type*>(T); }
  static TypeInfoLValue getFromOpaqueValue(void *Value) {
    TypeInfoLValue V;
    V.T = reinterpret_cast<const Type*>(Value);
    return V;
  }

  void print(llvm::raw_ostream &Out, const PrintingPolicy &Policy) const;
};

/// Symbolic representation of a dynamic allocation.
class DynamicAllocLValue {
  unsigned Index;

public:
  DynamicAllocLValue() : Index(0) {}
  explicit DynamicAllocLValue(unsigned Index) : Index(Index + 1) {}
  unsigned getIndex() { return Index - 1; }

  explicit operator bool() const { return Index != 0; }

  void *getOpaqueValue() {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(Index)
                                    << NumLowBitsAvailable);
  }
  static DynamicAllocLValue getFromOpaqueValue(void *Value) {
    DynamicAllocLValue V;
    V.Index = reinterpret_cast<uintptr_t>(Value) >> NumLowBitsAvailable;
    return V;
  }

  static unsigned getMaxIndex() {
    return (std::numeric_limits<unsigned>::max() >> NumLowBitsAvailable) - 1;
  }

  static constexpr int NumLowBitsAvailable = 3;
};
}

namespace llvm {
template<> struct PointerLikeTypeTraits<clang::TypeInfoLValue> {
  static void *getAsVoidPointer(clang::TypeInfoLValue V) {
    return V.getOpaqueValue();
  }
  static clang::TypeInfoLValue getFromVoidPointer(void *P) {
    return clang::TypeInfoLValue::getFromOpaqueValue(P);
  }
  // Validated by static_assert in APValue.cpp; hardcoded to avoid needing
  // to include Type.h.
  static constexpr int NumLowBitsAvailable = 3;
};

template<> struct PointerLikeTypeTraits<clang::DynamicAllocLValue> {
  static void *getAsVoidPointer(clang::DynamicAllocLValue V) {
    return V.getOpaqueValue();
  }
  static clang::DynamicAllocLValue getFromVoidPointer(void *P) {
    return clang::DynamicAllocLValue::getFromOpaqueValue(P);
  }
  static constexpr int NumLowBitsAvailable =
      clang::DynamicAllocLValue::NumLowBitsAvailable;
};
}

namespace clang {
/// APValue - This class implements a discriminated union of [uninitialized]
/// [APSInt] [APFloat], [Complex APSInt] [Complex APFloat], [Expr + Offset],
/// [Vector: N * APValue], [Array: N * APValue]
class APValue {
  typedef llvm::APFixedPoint APFixedPoint;
  typedef llvm::APSInt APSInt;
  typedef llvm::APFloat APFloat;
public:
  enum ValueKind {
    /// There is no such object (it's outside its lifetime).
    None,
    /// This object has an indeterminate value (C++ [basic.indet]).
    Indeterminate,
    Int,
    Float,
    FixedPoint,
    ComplexInt,
    ComplexFloat,
    LValue,
    Vector,
    Array,
    Struct,
    Union,
    MemberPointer,
    AddrLabelDiff
  };

  class LValueBase {
    typedef llvm::PointerUnion<const ValueDecl *, const Expr *, TypeInfoLValue,
                               DynamicAllocLValue>
        PtrTy;

  public:
    LValueBase() : Local{} {}
    LValueBase(const ValueDecl *P, unsigned I = 0, unsigned V = 0);
    LValueBase(const Expr *P, unsigned I = 0, unsigned V = 0);
    static LValueBase getDynamicAlloc(DynamicAllocLValue LV, QualType Type);
    static LValueBase getTypeInfo(TypeInfoLValue LV, QualType TypeInfo);

    void Profile(llvm::FoldingSetNodeID &ID) const;

    template <class T>
    bool is() const { return Ptr.is<T>(); }

    template <class T>
    T get() const { return Ptr.get<T>(); }

    template <class T>
    T dyn_cast() const { return Ptr.dyn_cast<T>(); }

    void *getOpaqueValue() const;

    bool isNull() const;

    explicit operator bool() const;

    unsigned getCallIndex() const;
    unsigned getVersion() const;
    QualType getTypeInfoType() const;
    QualType getDynamicAllocType() const;

    QualType getType() const;

    friend bool operator==(const LValueBase &LHS, const LValueBase &RHS);
    friend bool operator!=(const LValueBase &LHS, const LValueBase &RHS) {
      return !(LHS == RHS);
    }
    friend llvm::hash_code hash_value(const LValueBase &Base);
    friend struct llvm::DenseMapInfo<LValueBase>;

  private:
    PtrTy Ptr;
    struct LocalState {
      unsigned CallIndex, Version;
    };
    union {
      LocalState Local;
      /// The type std::type_info, if this is a TypeInfoLValue.
      void *TypeInfoType;
      /// The QualType, if this is a DynamicAllocLValue.
      void *DynamicAllocType;
    };
  };

  /// A FieldDecl or CXXRecordDecl, along with a flag indicating whether we
  /// mean a virtual or non-virtual base class subobject.
  typedef llvm::PointerIntPair<const Decl *, 1, bool> BaseOrMemberType;

  /// A non-discriminated union of a base, field, or array index.
  class LValuePathEntry {
    static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
                  "pointer doesn't fit in 64 bits?");
    uint64_t Value;

  public:
    LValuePathEntry() : Value() {}
    LValuePathEntry(BaseOrMemberType BaseOrMember);
    static LValuePathEntry ArrayIndex(uint64_t Index) {
      LValuePathEntry Result;
      Result.Value = Index;
      return Result;
    }

    BaseOrMemberType getAsBaseOrMember() const {
      return BaseOrMemberType::getFromOpaqueValue(
          reinterpret_cast<void *>(Value));
    }
    uint64_t getAsArrayIndex() const { return Value; }

    void Profile(llvm::FoldingSetNodeID &ID) const;

    friend bool operator==(LValuePathEntry A, LValuePathEntry B) {
      return A.Value == B.Value;
    }
    friend bool operator!=(LValuePathEntry A, LValuePathEntry B) {
      return A.Value != B.Value;
    }
    friend llvm::hash_code hash_value(LValuePathEntry A) {
      return llvm::hash_value(A.Value);
    }
  };
  class LValuePathSerializationHelper {
    const void *Ty;

  public:
    ArrayRef<LValuePathEntry> Path;

    LValuePathSerializationHelper(ArrayRef<LValuePathEntry>, QualType);
    QualType getType();
  };
  struct NoLValuePath {};
  struct UninitArray {};
  struct UninitStruct {};

  template <typename Impl> friend class clang::serialization::BasicReaderBase;
  friend class ASTImporter;
  friend class ASTNodeImporter;

private:
  ValueKind Kind;

  struct ComplexAPSInt {
    APSInt Real, Imag;
    ComplexAPSInt() : Real(1), Imag(1) {}
  };
  struct ComplexAPFloat {
    APFloat Real, Imag;
    ComplexAPFloat() : Real(0.0), Imag(0.0) {}
  };
  struct LV;
  struct Vec {
    APValue *Elts = nullptr;
    unsigned NumElts = 0;
    Vec() = default;
    Vec(const Vec &) = delete;
    Vec &operator=(const Vec &) = delete;
    ~Vec() { delete[] Elts; }
  };
  struct Arr {
    APValue *Elts;
    unsigned NumElts, ArrSize;
    Arr(unsigned NumElts, unsigned ArrSize);
    Arr(const Arr &) = delete;
    Arr &operator=(const Arr &) = delete;
    ~Arr();
  };
  struct StructData {
    APValue *Elts;
    unsigned NumBases;
    unsigned NumFields;
    StructData(unsigned NumBases, unsigned NumFields);
    StructData(const StructData &) = delete;
    StructData &operator=(const StructData &) = delete;
    ~StructData();
  };
  struct UnionData {
    const FieldDecl *Field;
    APValue *Value;
    UnionData();
    UnionData(const UnionData &) = delete;
    UnionData &operator=(const UnionData &) = delete;
    ~UnionData();
  };
  struct AddrLabelDiffData {
    const AddrLabelExpr* LHSExpr;
    const AddrLabelExpr* RHSExpr;
  };
  struct MemberPointerData;

  // We ensure elsewhere that Data is big enough for LV and MemberPointerData.
  typedef llvm::AlignedCharArrayUnion<void *, APSInt, APFloat, ComplexAPSInt,
                                      ComplexAPFloat, Vec, Arr, StructData,
                                      UnionData, AddrLabelDiffData> DataType;
  static const size_t DataSize = sizeof(DataType);

  DataType Data;

public:
  APValue() : Kind(None) {}
  explicit APValue(APSInt I) : Kind(None) {
    MakeInt(); setInt(std::move(I));
  }
  explicit APValue(APFloat F) : Kind(None) {
    MakeFloat(); setFloat(std::move(F));
  }
  explicit APValue(APFixedPoint FX) : Kind(None) {
    MakeFixedPoint(std::move(FX));
  }
  explicit APValue(const APValue *E, unsigned N) : Kind(None) {
    MakeVector(); setVector(E, N);
  }
  APValue(APSInt R, APSInt I) : Kind(None) {
    MakeComplexInt(); setComplexInt(std::move(R), std::move(I));
  }
  APValue(APFloat R, APFloat I) : Kind(None) {
    MakeComplexFloat(); setComplexFloat(std::move(R), std::move(I));
  }
  APValue(const APValue &RHS);
  APValue(APValue &&RHS);
  APValue(LValueBase B, const CharUnits &O, NoLValuePath N,
          bool IsNullPtr = false)
      : Kind(None) {
    MakeLValue(); setLValue(B, O, N, IsNullPtr);
  }
  APValue(LValueBase B, const CharUnits &O, ArrayRef<LValuePathEntry> Path,
          bool OnePastTheEnd, bool IsNullPtr = false)
      : Kind(None) {
    MakeLValue(); setLValue(B, O, Path, OnePastTheEnd, IsNullPtr);
  }
  APValue(UninitArray, unsigned InitElts, unsigned Size) : Kind(None) {
    MakeArray(InitElts, Size);
  }
  APValue(UninitStruct, unsigned B, unsigned M) : Kind(None) {
    MakeStruct(B, M);
  }
  explicit APValue(const FieldDecl *D, const APValue &V = APValue())
      : Kind(None) {
    MakeUnion(); setUnion(D, V);
  }
  APValue(const ValueDecl *Member, bool IsDerivedMember,
          ArrayRef<const CXXRecordDecl*> Path) : Kind(None) {
    MakeMemberPointer(Member, IsDerivedMember, Path);
  }
  APValue(const AddrLabelExpr* LHSExpr, const AddrLabelExpr* RHSExpr)
      : Kind(None) {
    MakeAddrLabelDiff(); setAddrLabelDiff(LHSExpr, RHSExpr);
  }
  static APValue IndeterminateValue() {
    APValue Result;
    Result.Kind = Indeterminate;
    return Result;
  }

  APValue &operator=(const APValue &RHS);
  APValue &operator=(APValue &&RHS);

  ~APValue() {
    if (Kind != None && Kind != Indeterminate)
      DestroyDataAndMakeUninit();
  }

  /// Returns whether the object performed allocations.
  ///
  /// If APValues are constructed via placement new, \c needsCleanup()
  /// indicates whether the destructor must be called in order to correctly
  /// free all allocated memory.
  bool needsCleanup() const;

  /// Swaps the contents of this and the given APValue.
  void swap(APValue &RHS);

  /// profile this value. There is no guarantee that values of different
  /// types will not produce the same profiled value, so the type should
  /// typically also be profiled if it's not implied by the context.
  void Profile(llvm::FoldingSetNodeID &ID) const;

  ValueKind getKind() const { return Kind; }

  bool isAbsent() const { return Kind == None; }
  bool isIndeterminate() const { return Kind == Indeterminate; }
  bool hasValue() const { return Kind != None && Kind != Indeterminate; }

  bool isInt() const { return Kind == Int; }
  bool isFloat() const { return Kind == Float; }
  bool isFixedPoint() const { return Kind == FixedPoint; }
  bool isComplexInt() const { return Kind == ComplexInt; }
  bool isComplexFloat() const { return Kind == ComplexFloat; }
  bool isLValue() const { return Kind == LValue; }
  bool isVector() const { return Kind == Vector; }
  bool isArray() const { return Kind == Array; }
  bool isStruct() const { return Kind == Struct; }
  bool isUnion() const { return Kind == Union; }
  bool isMemberPointer() const { return Kind == MemberPointer; }
  bool isAddrLabelDiff() const { return Kind == AddrLabelDiff; }

  void dump() const;
  void dump(raw_ostream &OS, const ASTContext &Context) const;

  void printPretty(raw_ostream &OS, const ASTContext &Ctx, QualType Ty) const;
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy, QualType Ty,
                   const ASTContext *Ctx = nullptr) const;

  std::string getAsString(const ASTContext &Ctx, QualType Ty) const;

  APSInt &getInt() {
    assert(isInt() && "Invalid accessor");
    return *(APSInt *)(char *)&Data;
  }
  const APSInt &getInt() const {
    return const_cast<APValue*>(this)->getInt();
  }

  /// Try to convert this value to an integral constant. This works if it's an
  /// integer, null pointer, or offset from a null pointer. Returns true on
  /// success.
  bool toIntegralConstant(APSInt &Result, QualType SrcTy,
                          const ASTContext &Ctx) const;

  APFloat &getFloat() {
    assert(isFloat() && "Invalid accessor");
    return *(APFloat *)(char *)&Data;
  }
  const APFloat &getFloat() const {
    return const_cast<APValue*>(this)->getFloat();
  }

  APFixedPoint &getFixedPoint() {
    assert(isFixedPoint() && "Invalid accessor");
    return *(APFixedPoint *)(char *)&Data;
  }
  const APFixedPoint &getFixedPoint() const {
    return const_cast<APValue *>(this)->getFixedPoint();
  }

  APSInt &getComplexIntReal() {
    assert(isComplexInt() && "Invalid accessor");
    return ((ComplexAPSInt *)(char *)&Data)->Real;
  }
  const APSInt &getComplexIntReal() const {
    return const_cast<APValue*>(this)->getComplexIntReal();
  }

  APSInt &getComplexIntImag() {
    assert(isComplexInt() && "Invalid accessor");
    return ((ComplexAPSInt *)(char *)&Data)->Imag;
  }
  const APSInt &getComplexIntImag() const {
    return const_cast<APValue*>(this)->getComplexIntImag();
  }

  APFloat &getComplexFloatReal() {
    assert(isComplexFloat() && "Invalid accessor");
    return ((ComplexAPFloat *)(char *)&Data)->Real;
  }
  const APFloat &getComplexFloatReal() const {
    return const_cast<APValue*>(this)->getComplexFloatReal();
  }

  APFloat &getComplexFloatImag() {
    assert(isComplexFloat() && "Invalid accessor");
    return ((ComplexAPFloat *)(char *)&Data)->Imag;
  }
  const APFloat &getComplexFloatImag() const {
    return const_cast<APValue*>(this)->getComplexFloatImag();
  }

  const LValueBase getLValueBase() const;
  CharUnits &getLValueOffset();
  const CharUnits &getLValueOffset() const {
    return const_cast<APValue*>(this)->getLValueOffset();
  }
  bool isLValueOnePastTheEnd() const;
  bool hasLValuePath() const;
  ArrayRef<LValuePathEntry> getLValuePath() const;
  unsigned getLValueCallIndex() const;
  unsigned getLValueVersion() const;
  bool isNullPointer() const;

  APValue &getVectorElt(unsigned I) {
    assert(isVector() && "Invalid accessor");
    assert(I < getVectorLength() && "Index out of range");
    return ((Vec *)(char *)&Data)->Elts[I];
  }
  const APValue &getVectorElt(unsigned I) const {
    return const_cast<APValue*>(this)->getVectorElt(I);
  }
  unsigned getVectorLength() const {
    assert(isVector() && "Invalid accessor");
    return ((const Vec *)(const void *)&Data)->NumElts;
  }

  APValue &getArrayInitializedElt(unsigned I) {
    assert(isArray() && "Invalid accessor");
    assert(I < getArrayInitializedElts() && "Index out of range");
    return ((Arr *)(char *)&Data)->Elts[I];
  }
  const APValue &getArrayInitializedElt(unsigned I) const {
    return const_cast<APValue*>(this)->getArrayInitializedElt(I);
  }
  bool hasArrayFiller() const {
    return getArrayInitializedElts() != getArraySize();
  }
  APValue &getArrayFiller() {
    assert(isArray() && "Invalid accessor");
    assert(hasArrayFiller() && "No array filler");
    return ((Arr *)(char *)&Data)->Elts[getArrayInitializedElts()];
  }
  const APValue &getArrayFiller() const {
    return const_cast<APValue*>(this)->getArrayFiller();
  }
  unsigned getArrayInitializedElts() const {
    assert(isArray() && "Invalid accessor");
    return ((const Arr *)(const void *)&Data)->NumElts;
  }
  unsigned getArraySize() const {
    assert(isArray() && "Invalid accessor");
    return ((const Arr *)(const void *)&Data)->ArrSize;
  }

  unsigned getStructNumBases() const {
    assert(isStruct() && "Invalid accessor");
    return ((const StructData *)(const char *)&Data)->NumBases;
  }
  unsigned getStructNumFields() const {
    assert(isStruct() && "Invalid accessor");
    return ((const StructData *)(const char *)&Data)->NumFields;
  }
  APValue &getStructBase(unsigned i) {
    assert(isStruct() && "Invalid accessor");
    assert(i < getStructNumBases() && "base class index OOB");
    return ((StructData *)(char *)&Data)->Elts[i];
  }
  APValue &getStructField(unsigned i) {
    assert(isStruct() && "Invalid accessor");
    assert(i < getStructNumFields() && "field index OOB");
    return ((StructData *)(char *)&Data)->Elts[getStructNumBases() + i];
  }
  const APValue &getStructBase(unsigned i) const {
    return const_cast<APValue*>(this)->getStructBase(i);
  }
  const APValue &getStructField(unsigned i) const {
    return const_cast<APValue*>(this)->getStructField(i);
  }

  const FieldDecl *getUnionField() const {
    assert(isUnion() && "Invalid accessor");
    return ((const UnionData *)(const char *)&Data)->Field;
  }
  APValue &getUnionValue() {
    assert(isUnion() && "Invalid accessor");
    return *((UnionData *)(char *)&Data)->Value;
  }
  const APValue &getUnionValue() const {
    return const_cast<APValue*>(this)->getUnionValue();
  }

  const ValueDecl *getMemberPointerDecl() const;
  bool isMemberPointerToDerivedMember() const;
  ArrayRef<const CXXRecordDecl*> getMemberPointerPath() const;

  const AddrLabelExpr* getAddrLabelDiffLHS() const {
    assert(isAddrLabelDiff() && "Invalid accessor");
    return ((const AddrLabelDiffData *)(const char *)&Data)->LHSExpr;
  }
  const AddrLabelExpr* getAddrLabelDiffRHS() const {
    assert(isAddrLabelDiff() && "Invalid accessor");
    return ((const AddrLabelDiffData *)(const char *)&Data)->RHSExpr;
  }

  void setInt(APSInt I) {
    assert(isInt() && "Invalid accessor");
    *(APSInt *)(char *)&Data = std::move(I);
  }
  void setFloat(APFloat F) {
    assert(isFloat() && "Invalid accessor");
    *(APFloat *)(char *)&Data = std::move(F);
  }
  void setFixedPoint(APFixedPoint FX) {
    assert(isFixedPoint() && "Invalid accessor");
    *(APFixedPoint *)(char *)&Data = std::move(FX);
  }
  void setVector(const APValue *E, unsigned N) {
    MutableArrayRef<APValue> InternalElts = setVectorUninit(N);
    for (unsigned i = 0; i != N; ++i)
      InternalElts[i] = E[i];
  }
  void setComplexInt(APSInt R, APSInt I) {
    assert(R.getBitWidth() == I.getBitWidth() &&
           "Invalid complex int (type mismatch).");
    assert(isComplexInt() && "Invalid accessor");
    ((ComplexAPSInt *)(char *)&Data)->Real = std::move(R);
    ((ComplexAPSInt *)(char *)&Data)->Imag = std::move(I);
  }
  void setComplexFloat(APFloat R, APFloat I) {
    assert(&R.getSemantics() == &I.getSemantics() &&
           "Invalid complex float (type mismatch).");
    assert(isComplexFloat() && "Invalid accessor");
    ((ComplexAPFloat *)(char *)&Data)->Real = std::move(R);
    ((ComplexAPFloat *)(char *)&Data)->Imag = std::move(I);
  }
  void setLValue(LValueBase B, const CharUnits &O, NoLValuePath,
                 bool IsNullPtr);
  void setLValue(LValueBase B, const CharUnits &O,
                 ArrayRef<LValuePathEntry> Path, bool OnePastTheEnd,
                 bool IsNullPtr);
  void setUnion(const FieldDecl *Field, const APValue &Value);
  void setAddrLabelDiff(const AddrLabelExpr* LHSExpr,
                        const AddrLabelExpr* RHSExpr) {
    ((AddrLabelDiffData *)(char *)&Data)->LHSExpr = LHSExpr;
    ((AddrLabelDiffData *)(char *)&Data)->RHSExpr = RHSExpr;
  }

private:
  void DestroyDataAndMakeUninit();
  void MakeInt() {
    assert(isAbsent() && "Bad state change");
    new ((void *)&Data) APSInt(1);
    Kind = Int;
  }
  void MakeFloat() {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) APFloat(0.0);
    Kind = Float;
  }
  void MakeFixedPoint(APFixedPoint &&FX) {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) APFixedPoint(std::move(FX));
    Kind = FixedPoint;
  }
  void MakeVector() {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) Vec();
    Kind = Vector;
  }
  void MakeComplexInt() {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) ComplexAPSInt();
    Kind = ComplexInt;
  }
  void MakeComplexFloat() {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) ComplexAPFloat();
    Kind = ComplexFloat;
  }
  void MakeLValue();
  void MakeArray(unsigned InitElts, unsigned Size);
  void MakeStruct(unsigned B, unsigned M) {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) StructData(B, M);
    Kind = Struct;
  }
  void MakeUnion() {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) UnionData();
    Kind = Union;
  }
  void MakeMemberPointer(const ValueDecl *Member, bool IsDerivedMember,
                         ArrayRef<const CXXRecordDecl*> Path);
  void MakeAddrLabelDiff() {
    assert(isAbsent() && "Bad state change");
    new ((void *)(char *)&Data) AddrLabelDiffData();
    Kind = AddrLabelDiff;
  }

private:
  /// The following functions are used as part of initialization, during
  /// deserialization and importing. Reserve the space so that it can be
  /// filled in by those steps.
  MutableArrayRef<APValue> setVectorUninit(unsigned N) {
    assert(isVector() && "Invalid accessor");
    Vec *V = ((Vec *)(char *)&Data);
    V->Elts = new APValue[N];
    V->NumElts = N;
    return {V->Elts, V->NumElts};
  }
  MutableArrayRef<LValuePathEntry>
  setLValueUninit(LValueBase B, const CharUnits &O, unsigned Size,
                  bool OnePastTheEnd, bool IsNullPtr);
  MutableArrayRef<const CXXRecordDecl *>
  setMemberPointerUninit(const ValueDecl *Member, bool IsDerivedMember,
                         unsigned Size);
};

} // end namespace clang.

namespace llvm {
template<> struct DenseMapInfo<clang::APValue::LValueBase> {
  static clang::APValue::LValueBase getEmptyKey();
  static clang::APValue::LValueBase getTombstoneKey();
  static unsigned getHashValue(const clang::APValue::LValueBase &Base);
  static bool isEqual(const clang::APValue::LValueBase &LHS,
                      const clang::APValue::LValueBase &RHS);
};
}

#endif
