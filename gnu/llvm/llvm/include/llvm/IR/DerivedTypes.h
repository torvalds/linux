//===- llvm/DerivedTypes.h - Classes for handling data types ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of classes that represent "derived
// types".  These are things like "arrays of x" or "structure of x, y, z" or
// "function returning x taking (y,z) as parameters", etc...
//
// The implementations of these classes live in the Type.cpp file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DERIVEDTYPES_H
#define LLVM_IR_DERIVEDTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class Value;
class APInt;
class LLVMContext;

/// Class to represent integer types. Note that this class is also used to
/// represent the built-in integer types: Int1Ty, Int8Ty, Int16Ty, Int32Ty and
/// Int64Ty.
/// Integer representation type
class IntegerType : public Type {
  friend class LLVMContextImpl;

protected:
  explicit IntegerType(LLVMContext &C, unsigned NumBits) : Type(C, IntegerTyID){
    setSubclassData(NumBits);
  }

public:
  /// This enum is just used to hold constants we need for IntegerType.
  enum {
    MIN_INT_BITS = 1,        ///< Minimum number of bits that can be specified
    MAX_INT_BITS = (1<<23)   ///< Maximum number of bits that can be specified
      ///< Note that bit width is stored in the Type classes SubclassData field
      ///< which has 24 bits. SelectionDAG type legalization can require a
      ///< power of 2 IntegerType, so limit to the largest representable power
      ///< of 2, 8388608.
  };

  /// This static method is the primary way of constructing an IntegerType.
  /// If an IntegerType with the same NumBits value was previously instantiated,
  /// that instance will be returned. Otherwise a new one will be created. Only
  /// one instance with a given NumBits value is ever created.
  /// Get or create an IntegerType instance.
  static IntegerType *get(LLVMContext &C, unsigned NumBits);

  /// Returns type twice as wide the input type.
  IntegerType *getExtendedType() const {
    return Type::getIntNTy(getContext(), 2 * getScalarSizeInBits());
  }

  /// Get the number of bits in this IntegerType
  unsigned getBitWidth() const { return getSubclassData(); }

  /// Return a bitmask with ones set for all of the bits that can be set by an
  /// unsigned version of this type. This is 0xFF for i8, 0xFFFF for i16, etc.
  uint64_t getBitMask() const {
    return ~uint64_t(0UL) >> (64-getBitWidth());
  }

  /// Return a uint64_t with just the most significant bit set (the sign bit, if
  /// the value is treated as a signed number).
  uint64_t getSignBit() const {
    return 1ULL << (getBitWidth()-1);
  }

  /// For example, this is 0xFF for an 8 bit integer, 0xFFFF for i16, etc.
  /// @returns a bit mask with ones set for all the bits of this type.
  /// Get a bit mask for this type.
  APInt getMask() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == IntegerTyID;
  }
};

unsigned Type::getIntegerBitWidth() const {
  return cast<IntegerType>(this)->getBitWidth();
}

/// Class to represent function types
///
class FunctionType : public Type {
  FunctionType(Type *Result, ArrayRef<Type*> Params, bool IsVarArgs);

public:
  FunctionType(const FunctionType &) = delete;
  FunctionType &operator=(const FunctionType &) = delete;

  /// This static method is the primary way of constructing a FunctionType.
  static FunctionType *get(Type *Result,
                           ArrayRef<Type*> Params, bool isVarArg);

  /// Create a FunctionType taking no parameters.
  static FunctionType *get(Type *Result, bool isVarArg);

  /// Return true if the specified type is valid as a return type.
  static bool isValidReturnType(Type *RetTy);

  /// Return true if the specified type is valid as an argument type.
  static bool isValidArgumentType(Type *ArgTy);

  bool isVarArg() const { return getSubclassData()!=0; }
  Type *getReturnType() const { return ContainedTys[0]; }

  using param_iterator = Type::subtype_iterator;

  param_iterator param_begin() const { return ContainedTys + 1; }
  param_iterator param_end() const { return &ContainedTys[NumContainedTys]; }
  ArrayRef<Type *> params() const {
    return ArrayRef(param_begin(), param_end());
  }

  /// Parameter type accessors.
  Type *getParamType(unsigned i) const {
    assert(i < getNumParams() && "getParamType() out of range!");
    return ContainedTys[i + 1];
  }

  /// Return the number of fixed parameters this function type requires.
  /// This does not consider varargs.
  unsigned getNumParams() const { return NumContainedTys - 1; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == FunctionTyID;
  }
};
static_assert(alignof(FunctionType) >= alignof(Type *),
              "Alignment sufficient for objects appended to FunctionType");

bool Type::isFunctionVarArg() const {
  return cast<FunctionType>(this)->isVarArg();
}

Type *Type::getFunctionParamType(unsigned i) const {
  return cast<FunctionType>(this)->getParamType(i);
}

unsigned Type::getFunctionNumParams() const {
  return cast<FunctionType>(this)->getNumParams();
}

/// A handy container for a FunctionType+Callee-pointer pair, which can be
/// passed around as a single entity. This assists in replacing the use of
/// PointerType::getElementType() to access the function's type, since that's
/// slated for removal as part of the [opaque pointer types] project.
class FunctionCallee {
public:
  // Allow implicit conversion from types which have a getFunctionType member
  // (e.g. Function and InlineAsm).
  template <typename T, typename U = decltype(&T::getFunctionType)>
  FunctionCallee(T *Fn)
      : FnTy(Fn ? Fn->getFunctionType() : nullptr), Callee(Fn) {}

  FunctionCallee(FunctionType *FnTy, Value *Callee)
      : FnTy(FnTy), Callee(Callee) {
    assert((FnTy == nullptr) == (Callee == nullptr));
  }

  FunctionCallee(std::nullptr_t) {}

  FunctionCallee() = default;

  FunctionType *getFunctionType() { return FnTy; }

  Value *getCallee() { return Callee; }

  explicit operator bool() { return Callee; }

private:
  FunctionType *FnTy = nullptr;
  Value *Callee = nullptr;
};

/// Class to represent struct types. There are two different kinds of struct
/// types: Literal structs and Identified structs.
///
/// Literal struct types (e.g. { i32, i32 }) are uniqued structurally, and must
/// always have a body when created.  You can get one of these by using one of
/// the StructType::get() forms.
///
/// Identified structs (e.g. %foo or %42) may optionally have a name and are not
/// uniqued.  The names for identified structs are managed at the LLVMContext
/// level, so there can only be a single identified struct with a given name in
/// a particular LLVMContext.  Identified structs may also optionally be opaque
/// (have no body specified).  You get one of these by using one of the
/// StructType::create() forms.
///
/// Independent of what kind of struct you have, the body of a struct type are
/// laid out in memory consecutively with the elements directly one after the
/// other (if the struct is packed) or (if not packed) with padding between the
/// elements as defined by DataLayout (which is required to match what the code
/// generator for a target expects).
///
class StructType : public Type {
  StructType(LLVMContext &C) : Type(C, StructTyID) {}

  enum {
    /// This is the contents of the SubClassData field.
    SCDB_HasBody = 1,
    SCDB_Packed = 2,
    SCDB_IsLiteral = 4,
    SCDB_IsSized = 8,
    SCDB_ContainsScalableVector = 16,
    SCDB_NotContainsScalableVector = 32
  };

  /// For a named struct that actually has a name, this is a pointer to the
  /// symbol table entry (maintained by LLVMContext) for the struct.
  /// This is null if the type is an literal struct or if it is a identified
  /// type that has an empty name.
  void *SymbolTableEntry = nullptr;

public:
  StructType(const StructType &) = delete;
  StructType &operator=(const StructType &) = delete;

  /// This creates an identified struct.
  static StructType *create(LLVMContext &Context, StringRef Name);
  static StructType *create(LLVMContext &Context);

  static StructType *create(ArrayRef<Type *> Elements, StringRef Name,
                            bool isPacked = false);
  static StructType *create(ArrayRef<Type *> Elements);
  static StructType *create(LLVMContext &Context, ArrayRef<Type *> Elements,
                            StringRef Name, bool isPacked = false);
  static StructType *create(LLVMContext &Context, ArrayRef<Type *> Elements);
  template <class... Tys>
  static std::enable_if_t<are_base_of<Type, Tys...>::value, StructType *>
  create(StringRef Name, Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    return create(ArrayRef<Type *>({elt1, elts...}), Name);
  }

  /// This static method is the primary way to create a literal StructType.
  static StructType *get(LLVMContext &Context, ArrayRef<Type*> Elements,
                         bool isPacked = false);

  /// Create an empty structure type.
  static StructType *get(LLVMContext &Context, bool isPacked = false);

  /// This static method is a convenience method for creating structure types by
  /// specifying the elements as arguments. Note that this method always returns
  /// a non-packed struct, and requires at least one element type.
  template <class... Tys>
  static std::enable_if_t<are_base_of<Type, Tys...>::value, StructType *>
  get(Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    LLVMContext &Ctx = elt1->getContext();
    return StructType::get(Ctx, ArrayRef<Type *>({elt1, elts...}));
  }

  /// Return the type with the specified name, or null if there is none by that
  /// name.
  static StructType *getTypeByName(LLVMContext &C, StringRef Name);

  bool isPacked() const { return (getSubclassData() & SCDB_Packed) != 0; }

  /// Return true if this type is uniqued by structural equivalence, false if it
  /// is a struct definition.
  bool isLiteral() const { return (getSubclassData() & SCDB_IsLiteral) != 0; }

  /// Return true if this is a type with an identity that has no body specified
  /// yet. These prints as 'opaque' in .ll files.
  bool isOpaque() const { return (getSubclassData() & SCDB_HasBody) == 0; }

  /// isSized - Return true if this is a sized type.
  bool isSized(SmallPtrSetImpl<Type *> *Visited = nullptr) const;

  /// Returns true if this struct contains a scalable vector.
  bool
  containsScalableVectorType(SmallPtrSetImpl<Type *> *Visited = nullptr) const;

  /// Returns true if this struct contains homogeneous scalable vector types.
  /// Note that the definition of homogeneous scalable vector type is not
  /// recursive here. That means the following structure will return false
  /// when calling this function.
  /// {{<vscale x 2 x i32>, <vscale x 4 x i64>},
  ///  {<vscale x 2 x i32>, <vscale x 4 x i64>}}
  bool containsHomogeneousScalableVectorTypes() const;

  /// Return true if this is a named struct that has a non-empty name.
  bool hasName() const { return SymbolTableEntry != nullptr; }

  /// Return the name for this struct type if it has an identity.
  /// This may return an empty string for an unnamed struct type.  Do not call
  /// this on an literal type.
  StringRef getName() const;

  /// Change the name of this type to the specified name, or to a name with a
  /// suffix if there is a collision. Do not call this on an literal type.
  void setName(StringRef Name);

  /// Specify a body for an opaque identified type.
  void setBody(ArrayRef<Type*> Elements, bool isPacked = false);

  template <typename... Tys>
  std::enable_if_t<are_base_of<Type, Tys...>::value, void>
  setBody(Type *elt1, Tys *... elts) {
    assert(elt1 && "Cannot create a struct type with no elements with this");
    setBody(ArrayRef<Type *>({elt1, elts...}));
  }

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  // Iterator access to the elements.
  using element_iterator = Type::subtype_iterator;

  element_iterator element_begin() const { return ContainedTys; }
  element_iterator element_end() const { return &ContainedTys[NumContainedTys];}
  ArrayRef<Type *> elements() const {
    return ArrayRef(element_begin(), element_end());
  }

  /// Return true if this is layout identical to the specified struct.
  bool isLayoutIdentical(StructType *Other) const;

  /// Random access to the elements
  unsigned getNumElements() const { return NumContainedTys; }
  Type *getElementType(unsigned N) const {
    assert(N < NumContainedTys && "Element number out of range!");
    return ContainedTys[N];
  }
  /// Given an index value into the type, return the type of the element.
  Type *getTypeAtIndex(const Value *V) const;
  Type *getTypeAtIndex(unsigned N) const { return getElementType(N); }
  bool indexValid(const Value *V) const;
  bool indexValid(unsigned Idx) const { return Idx < getNumElements(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == StructTyID;
  }
};

StringRef Type::getStructName() const {
  return cast<StructType>(this)->getName();
}

unsigned Type::getStructNumElements() const {
  return cast<StructType>(this)->getNumElements();
}

Type *Type::getStructElementType(unsigned N) const {
  return cast<StructType>(this)->getElementType(N);
}

/// Class to represent array types.
class ArrayType : public Type {
  /// The element type of the array.
  Type *ContainedType;
  /// Number of elements in the array.
  uint64_t NumElements;

  ArrayType(Type *ElType, uint64_t NumEl);

public:
  ArrayType(const ArrayType &) = delete;
  ArrayType &operator=(const ArrayType &) = delete;

  uint64_t getNumElements() const { return NumElements; }
  Type *getElementType() const { return ContainedType; }

  /// This static method is the primary way to construct an ArrayType
  static ArrayType *get(Type *ElementType, uint64_t NumElements);

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == ArrayTyID;
  }
};

uint64_t Type::getArrayNumElements() const {
  return cast<ArrayType>(this)->getNumElements();
}

/// Base class of all SIMD vector types
class VectorType : public Type {
  /// A fully specified VectorType is of the form <vscale x n x Ty>. 'n' is the
  /// minimum number of elements of type Ty contained within the vector, and
  /// 'vscale x' indicates that the total element count is an integer multiple
  /// of 'n', where the multiple is either guaranteed to be one, or is
  /// statically unknown at compile time.
  ///
  /// If the multiple is known to be 1, then the extra term is discarded in
  /// textual IR:
  ///
  /// <4 x i32>          - a vector containing 4 i32s
  /// <vscale x 4 x i32> - a vector containing an unknown integer multiple
  ///                      of 4 i32s

  /// The element type of the vector.
  Type *ContainedType;

protected:
  /// The element quantity of this vector. The meaning of this value depends
  /// on the type of vector:
  /// - For FixedVectorType = <ElementQuantity x ty>, there are
  ///   exactly ElementQuantity elements in this vector.
  /// - For ScalableVectorType = <vscale x ElementQuantity x ty>,
  ///   there are vscale * ElementQuantity elements in this vector, where
  ///   vscale is a runtime-constant integer greater than 0.
  const unsigned ElementQuantity;

  VectorType(Type *ElType, unsigned EQ, Type::TypeID TID);

public:
  VectorType(const VectorType &) = delete;
  VectorType &operator=(const VectorType &) = delete;

  Type *getElementType() const { return ContainedType; }

  /// This static method is the primary way to construct an VectorType.
  static VectorType *get(Type *ElementType, ElementCount EC);

  static VectorType *get(Type *ElementType, unsigned NumElements,
                         bool Scalable) {
    return VectorType::get(ElementType,
                           ElementCount::get(NumElements, Scalable));
  }

  static VectorType *get(Type *ElementType, const VectorType *Other) {
    return VectorType::get(ElementType, Other->getElementCount());
  }

  /// This static method gets a VectorType with the same number of elements as
  /// the input type, and the element type is an integer type of the same width
  /// as the input element type.
  static VectorType *getInteger(VectorType *VTy) {
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    assert(EltBits && "Element size must be of a non-zero size");
    Type *EltTy = IntegerType::get(VTy->getContext(), EltBits);
    return VectorType::get(EltTy, VTy->getElementCount());
  }

  /// This static method is like getInteger except that the element types are
  /// twice as wide as the elements in the input type.
  static VectorType *getExtendedElementVectorType(VectorType *VTy) {
    assert(VTy->isIntOrIntVectorTy() && "VTy expected to be a vector of ints.");
    auto *EltTy = cast<IntegerType>(VTy->getElementType());
    return VectorType::get(EltTy->getExtendedType(), VTy->getElementCount());
  }

  // This static method gets a VectorType with the same number of elements as
  // the input type, and the element type is an integer or float type which
  // is half as wide as the elements in the input type.
  static VectorType *getTruncatedElementVectorType(VectorType *VTy) {
    Type *EltTy;
    if (VTy->getElementType()->isFloatingPointTy()) {
      switch(VTy->getElementType()->getTypeID()) {
      case DoubleTyID:
        EltTy = Type::getFloatTy(VTy->getContext());
        break;
      case FloatTyID:
        EltTy = Type::getHalfTy(VTy->getContext());
        break;
      default:
        llvm_unreachable("Cannot create narrower fp vector element type");
      }
    } else {
      unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
      assert((EltBits & 1) == 0 &&
             "Cannot truncate vector element with odd bit-width");
      EltTy = IntegerType::get(VTy->getContext(), EltBits / 2);
    }
    return VectorType::get(EltTy, VTy->getElementCount());
  }

  // This static method returns a VectorType with a smaller number of elements
  // of a larger type than the input element type. For example, a <16 x i8>
  // subdivided twice would return <4 x i32>
  static VectorType *getSubdividedVectorType(VectorType *VTy, int NumSubdivs) {
    for (int i = 0; i < NumSubdivs; ++i) {
      VTy = VectorType::getDoubleElementsVectorType(VTy);
      VTy = VectorType::getTruncatedElementVectorType(VTy);
    }
    return VTy;
  }

  /// This static method returns a VectorType with half as many elements as the
  /// input type and the same element type.
  static VectorType *getHalfElementsVectorType(VectorType *VTy) {
    auto EltCnt = VTy->getElementCount();
    assert(EltCnt.isKnownEven() &&
           "Cannot halve vector with odd number of elements.");
    return VectorType::get(VTy->getElementType(),
                           EltCnt.divideCoefficientBy(2));
  }

  /// This static method returns a VectorType with twice as many elements as the
  /// input type and the same element type.
  static VectorType *getDoubleElementsVectorType(VectorType *VTy) {
    auto EltCnt = VTy->getElementCount();
    assert((EltCnt.getKnownMinValue() * 2ull) <= UINT_MAX &&
           "Too many elements in vector");
    return VectorType::get(VTy->getElementType(), EltCnt * 2);
  }

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  /// Return an ElementCount instance to represent the (possibly scalable)
  /// number of elements in the vector.
  inline ElementCount getElementCount() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == FixedVectorTyID ||
           T->getTypeID() == ScalableVectorTyID;
  }
};

/// Class to represent fixed width SIMD vectors
class FixedVectorType : public VectorType {
protected:
  FixedVectorType(Type *ElTy, unsigned NumElts)
      : VectorType(ElTy, NumElts, FixedVectorTyID) {}

public:
  static FixedVectorType *get(Type *ElementType, unsigned NumElts);

  static FixedVectorType *get(Type *ElementType, const FixedVectorType *FVTy) {
    return get(ElementType, FVTy->getNumElements());
  }

  static FixedVectorType *getInteger(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getInteger(VTy));
  }

  static FixedVectorType *getExtendedElementVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getExtendedElementVectorType(VTy));
  }

  static FixedVectorType *getTruncatedElementVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(
        VectorType::getTruncatedElementVectorType(VTy));
  }

  static FixedVectorType *getSubdividedVectorType(FixedVectorType *VTy,
                                                  int NumSubdivs) {
    return cast<FixedVectorType>(
        VectorType::getSubdividedVectorType(VTy, NumSubdivs));
  }

  static FixedVectorType *getHalfElementsVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getHalfElementsVectorType(VTy));
  }

  static FixedVectorType *getDoubleElementsVectorType(FixedVectorType *VTy) {
    return cast<FixedVectorType>(VectorType::getDoubleElementsVectorType(VTy));
  }

  static bool classof(const Type *T) {
    return T->getTypeID() == FixedVectorTyID;
  }

  unsigned getNumElements() const { return ElementQuantity; }
};

/// Class to represent scalable SIMD vectors
class ScalableVectorType : public VectorType {
protected:
  ScalableVectorType(Type *ElTy, unsigned MinNumElts)
      : VectorType(ElTy, MinNumElts, ScalableVectorTyID) {}

public:
  static ScalableVectorType *get(Type *ElementType, unsigned MinNumElts);

  static ScalableVectorType *get(Type *ElementType,
                                 const ScalableVectorType *SVTy) {
    return get(ElementType, SVTy->getMinNumElements());
  }

  static ScalableVectorType *getInteger(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(VectorType::getInteger(VTy));
  }

  static ScalableVectorType *
  getExtendedElementVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getExtendedElementVectorType(VTy));
  }

  static ScalableVectorType *
  getTruncatedElementVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getTruncatedElementVectorType(VTy));
  }

  static ScalableVectorType *getSubdividedVectorType(ScalableVectorType *VTy,
                                                     int NumSubdivs) {
    return cast<ScalableVectorType>(
        VectorType::getSubdividedVectorType(VTy, NumSubdivs));
  }

  static ScalableVectorType *
  getHalfElementsVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(VectorType::getHalfElementsVectorType(VTy));
  }

  static ScalableVectorType *
  getDoubleElementsVectorType(ScalableVectorType *VTy) {
    return cast<ScalableVectorType>(
        VectorType::getDoubleElementsVectorType(VTy));
  }

  /// Get the minimum number of elements in this vector. The actual number of
  /// elements in the vector is an integer multiple of this value.
  unsigned getMinNumElements() const { return ElementQuantity; }

  static bool classof(const Type *T) {
    return T->getTypeID() == ScalableVectorTyID;
  }
};

inline ElementCount VectorType::getElementCount() const {
  return ElementCount::get(ElementQuantity, isa<ScalableVectorType>(this));
}

/// Class to represent pointers.
class PointerType : public Type {
  explicit PointerType(LLVMContext &C, unsigned AddrSpace);

public:
  PointerType(const PointerType &) = delete;
  PointerType &operator=(const PointerType &) = delete;

  /// This constructs a pointer to an object of the specified type in a numbered
  /// address space.
  static PointerType *get(Type *ElementType, unsigned AddressSpace);
  /// This constructs an opaque pointer to an object in a numbered address
  /// space.
  static PointerType *get(LLVMContext &C, unsigned AddressSpace);

  /// This constructs a pointer to an object of the specified type in the
  /// default address space (address space zero).
  static PointerType *getUnqual(Type *ElementType) {
    return PointerType::get(ElementType, 0);
  }

  /// This constructs an opaque pointer to an object in the
  /// default address space (address space zero).
  static PointerType *getUnqual(LLVMContext &C) {
    return PointerType::get(C, 0);
  }

  /// Return true if the specified type is valid as a element type.
  static bool isValidElementType(Type *ElemTy);

  /// Return true if we can load or store from a pointer to this type.
  static bool isLoadableOrStorableType(Type *ElemTy);

  /// Return the address space of the Pointer type.
  inline unsigned getAddressSpace() const { return getSubclassData(); }

  /// Implement support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) {
    return T->getTypeID() == PointerTyID;
  }
};

Type *Type::getExtendedType() const {
  assert(
      isIntOrIntVectorTy() &&
      "Original type expected to be a vector of integers or a scalar integer.");
  if (auto *VTy = dyn_cast<VectorType>(this))
    return VectorType::getExtendedElementVectorType(
        const_cast<VectorType *>(VTy));
  return cast<IntegerType>(this)->getExtendedType();
}

Type *Type::getWithNewType(Type *EltTy) const {
  if (auto *VTy = dyn_cast<VectorType>(this))
    return VectorType::get(EltTy, VTy->getElementCount());
  return EltTy;
}

Type *Type::getWithNewBitWidth(unsigned NewBitWidth) const {
  assert(
      isIntOrIntVectorTy() &&
      "Original type expected to be a vector of integers or a scalar integer.");
  return getWithNewType(getIntNTy(getContext(), NewBitWidth));
}

unsigned Type::getPointerAddressSpace() const {
  return cast<PointerType>(getScalarType())->getAddressSpace();
}

/// Class to represent target extensions types, which are generally
/// unintrospectable from target-independent optimizations.
///
/// Target extension types have a string name, and optionally have type and/or
/// integer parameters. The exact meaning of any parameters is dependent on the
/// target.
class TargetExtType : public Type {
  TargetExtType(LLVMContext &C, StringRef Name, ArrayRef<Type *> Types,
                ArrayRef<unsigned> Ints);

  // These strings are ultimately owned by the context.
  StringRef Name;
  unsigned *IntParams;

public:
  TargetExtType(const TargetExtType &) = delete;
  TargetExtType &operator=(const TargetExtType &) = delete;

  /// Return a target extension type having the specified name and optional
  /// type and integer parameters.
  static TargetExtType *get(LLVMContext &Context, StringRef Name,
                            ArrayRef<Type *> Types = std::nullopt,
                            ArrayRef<unsigned> Ints = std::nullopt);

  /// Return the name for this target extension type. Two distinct target
  /// extension types may have the same name if their type or integer parameters
  /// differ.
  StringRef getName() const { return Name; }

  /// Return the type parameters for this particular target extension type. If
  /// there are no parameters, an empty array is returned.
  ArrayRef<Type *> type_params() const {
    return ArrayRef(type_param_begin(), type_param_end());
  }

  using type_param_iterator = Type::subtype_iterator;
  type_param_iterator type_param_begin() const { return ContainedTys; }
  type_param_iterator type_param_end() const {
    return &ContainedTys[NumContainedTys];
  }

  Type *getTypeParameter(unsigned i) const { return getContainedType(i); }
  unsigned getNumTypeParameters() const { return getNumContainedTypes(); }

  /// Return the integer parameters for this particular target extension type.
  /// If there are no parameters, an empty array is returned.
  ArrayRef<unsigned> int_params() const {
    return ArrayRef(IntParams, getNumIntParameters());
  }

  unsigned getIntParameter(unsigned i) const { return IntParams[i]; }
  unsigned getNumIntParameters() const { return getSubclassData(); }

  enum Property {
    /// zeroinitializer is valid for this target extension type.
    HasZeroInit = 1U << 0,
    /// This type may be used as the value type of a global variable.
    CanBeGlobal = 1U << 1,
  };

  /// Returns true if the target extension type contains the given property.
  bool hasProperty(Property Prop) const;

  /// Returns an underlying layout type for the target extension type. This
  /// type can be used to query size and alignment information, if it is
  /// appropriate (although note that the layout type may also be void). It is
  /// not legal to bitcast between this type and the layout type, however.
  Type *getLayoutType() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Type *T) { return T->getTypeID() == TargetExtTyID; }
};

StringRef Type::getTargetExtName() const {
  return cast<TargetExtType>(this)->getName();
}

} // end namespace llvm

#endif // LLVM_IR_DERIVEDTYPES_H
