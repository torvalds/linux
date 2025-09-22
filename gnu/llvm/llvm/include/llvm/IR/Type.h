//===- llvm/Type.h - Classes for handling data types ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Type class.  For more "Type"
// stuff, look in DerivedTypes.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TYPE_H
#define LLVM_IR_TYPE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>
#include <iterator>

namespace llvm {

class IntegerType;
struct fltSemantics;
class LLVMContext;
class PointerType;
class raw_ostream;
class StringRef;
template <typename PtrType> class SmallPtrSetImpl;

/// The instances of the Type class are immutable: once they are created,
/// they are never changed.  Also note that only one instance of a particular
/// type is ever created.  Thus seeing if two types are equal is a matter of
/// doing a trivial pointer comparison. To enforce that no two equal instances
/// are created, Type instances can only be created via static factory methods
/// in class Type and in derived classes.  Once allocated, Types are never
/// free'd.
///
class Type {
public:
  //===--------------------------------------------------------------------===//
  /// Definitions of all of the base types for the Type system.  Based on this
  /// value, you can cast to a class defined in DerivedTypes.h.
  /// Note: If you add an element to this, you need to add an element to the
  /// Type::getPrimitiveType function, or else things will break!
  /// Also update LLVMTypeKind and LLVMGetTypeKind () in the C binding.
  ///
  enum TypeID {
    // PrimitiveTypes
    HalfTyID = 0,  ///< 16-bit floating point type
    BFloatTyID,    ///< 16-bit floating point type (7-bit significand)
    FloatTyID,     ///< 32-bit floating point type
    DoubleTyID,    ///< 64-bit floating point type
    X86_FP80TyID,  ///< 80-bit floating point type (X87)
    FP128TyID,     ///< 128-bit floating point type (112-bit significand)
    PPC_FP128TyID, ///< 128-bit floating point type (two 64-bits, PowerPC)
    VoidTyID,      ///< type with no size
    LabelTyID,     ///< Labels
    MetadataTyID,  ///< Metadata
    X86_MMXTyID,   ///< MMX vectors (64 bits, X86 specific)
    X86_AMXTyID,   ///< AMX vectors (8192 bits, X86 specific)
    TokenTyID,     ///< Tokens

    // Derived types... see DerivedTypes.h file.
    IntegerTyID,        ///< Arbitrary bit width integers
    FunctionTyID,       ///< Functions
    PointerTyID,        ///< Pointers
    StructTyID,         ///< Structures
    ArrayTyID,          ///< Arrays
    FixedVectorTyID,    ///< Fixed width SIMD vector type
    ScalableVectorTyID, ///< Scalable SIMD vector type
    TypedPointerTyID,   ///< Typed pointer used by some GPU targets
    TargetExtTyID,      ///< Target extension type
  };

private:
  /// This refers to the LLVMContext in which this type was uniqued.
  LLVMContext &Context;

  TypeID   ID : 8;            // The current base type of this type.
  unsigned SubclassData : 24; // Space for subclasses to store data.
                              // Note that this should be synchronized with
                              // MAX_INT_BITS value in IntegerType class.

protected:
  friend class LLVMContextImpl;

  explicit Type(LLVMContext &C, TypeID tid)
    : Context(C), ID(tid), SubclassData(0) {}
  ~Type() = default;

  unsigned getSubclassData() const { return SubclassData; }

  void setSubclassData(unsigned val) {
    SubclassData = val;
    // Ensure we don't have any accidental truncation.
    assert(getSubclassData() == val && "Subclass data too large for field");
  }

  /// Keeps track of how many Type*'s there are in the ContainedTys list.
  unsigned NumContainedTys = 0;

  /// A pointer to the array of Types contained by this Type. For example, this
  /// includes the arguments of a function type, the elements of a structure,
  /// the pointee of a pointer, the element type of an array, etc. This pointer
  /// may be 0 for types that don't contain other types (Integer, Double,
  /// Float).
  Type * const *ContainedTys = nullptr;

public:
  /// Print the current type.
  /// Omit the type details if \p NoDetails == true.
  /// E.g., let %st = type { i32, i16 }
  /// When \p NoDetails is true, we only print %st.
  /// Put differently, \p NoDetails prints the type as if
  /// inlined with the operands when printing an instruction.
  void print(raw_ostream &O, bool IsForDebug = false,
             bool NoDetails = false) const;

  void dump() const;

  /// Return the LLVMContext in which this type was uniqued.
  LLVMContext &getContext() const { return Context; }

  //===--------------------------------------------------------------------===//
  // Accessors for working with types.
  //

  /// Return the type id for the type. This will return one of the TypeID enum
  /// elements defined above.
  TypeID getTypeID() const { return ID; }

  /// Return true if this is 'void'.
  bool isVoidTy() const { return getTypeID() == VoidTyID; }

  /// Return true if this is 'half', a 16-bit IEEE fp type.
  bool isHalfTy() const { return getTypeID() == HalfTyID; }

  /// Return true if this is 'bfloat', a 16-bit bfloat type.
  bool isBFloatTy() const { return getTypeID() == BFloatTyID; }

  /// Return true if this is a 16-bit float type.
  bool is16bitFPTy() const {
    return getTypeID() == BFloatTyID || getTypeID() == HalfTyID;
  }

  /// Return true if this is 'float', a 32-bit IEEE fp type.
  bool isFloatTy() const { return getTypeID() == FloatTyID; }

  /// Return true if this is 'double', a 64-bit IEEE fp type.
  bool isDoubleTy() const { return getTypeID() == DoubleTyID; }

  /// Return true if this is x86 long double.
  bool isX86_FP80Ty() const { return getTypeID() == X86_FP80TyID; }

  /// Return true if this is 'fp128'.
  bool isFP128Ty() const { return getTypeID() == FP128TyID; }

  /// Return true if this is powerpc long double.
  bool isPPC_FP128Ty() const { return getTypeID() == PPC_FP128TyID; }

  /// Return true if this is a well-behaved IEEE-like type, which has a IEEE
  /// compatible layout as defined by APFloat::isIEEE(), and does not have
  /// non-IEEE values, such as x86_fp80's unnormal values.
  bool isIEEELikeFPTy() const {
    switch (getTypeID()) {
    case DoubleTyID:
    case FloatTyID:
    case HalfTyID:
    case BFloatTyID:
    case FP128TyID:
      return true;
    default:
      return false;
    }
  }

  /// Return true if this is one of the floating-point types
  bool isFloatingPointTy() const {
    return isIEEELikeFPTy() || getTypeID() == X86_FP80TyID ||
           getTypeID() == PPC_FP128TyID;
  }

  /// Returns true if this is a floating-point type that is an unevaluated sum
  /// of multiple floating-point units.
  /// An example of such a type is ppc_fp128, also known as double-double, which
  /// consists of two IEEE 754 doubles.
  bool isMultiUnitFPType() const {
    return getTypeID() == PPC_FP128TyID;
  }

  const fltSemantics &getFltSemantics() const;

  /// Return true if this is X86 MMX.
  bool isX86_MMXTy() const { return getTypeID() == X86_MMXTyID; }

  /// Return true if this is X86 AMX.
  bool isX86_AMXTy() const { return getTypeID() == X86_AMXTyID; }

  /// Return true if this is a target extension type.
  bool isTargetExtTy() const { return getTypeID() == TargetExtTyID; }

  /// Return true if this is a target extension type with a scalable layout.
  bool isScalableTargetExtTy() const;

  /// Return true if this is a type whose size is a known multiple of vscale.
  bool isScalableTy() const;

  /// Return true if this is a FP type or a vector of FP.
  bool isFPOrFPVectorTy() const { return getScalarType()->isFloatingPointTy(); }

  /// Return true if this is 'label'.
  bool isLabelTy() const { return getTypeID() == LabelTyID; }

  /// Return true if this is 'metadata'.
  bool isMetadataTy() const { return getTypeID() == MetadataTyID; }

  /// Return true if this is 'token'.
  bool isTokenTy() const { return getTypeID() == TokenTyID; }

  /// True if this is an instance of IntegerType.
  bool isIntegerTy() const { return getTypeID() == IntegerTyID; }

  /// Return true if this is an IntegerType of the given width.
  bool isIntegerTy(unsigned Bitwidth) const;

  /// Return true if this is an integer type or a vector of integer types.
  bool isIntOrIntVectorTy() const { return getScalarType()->isIntegerTy(); }

  /// Return true if this is an integer type or a vector of integer types of
  /// the given width.
  bool isIntOrIntVectorTy(unsigned BitWidth) const {
    return getScalarType()->isIntegerTy(BitWidth);
  }

  /// Return true if this is an integer type or a pointer type.
  bool isIntOrPtrTy() const { return isIntegerTy() || isPointerTy(); }

  /// True if this is an instance of FunctionType.
  bool isFunctionTy() const { return getTypeID() == FunctionTyID; }

  /// True if this is an instance of StructType.
  bool isStructTy() const { return getTypeID() == StructTyID; }

  /// True if this is an instance of ArrayType.
  bool isArrayTy() const { return getTypeID() == ArrayTyID; }

  /// True if this is an instance of PointerType.
  bool isPointerTy() const { return getTypeID() == PointerTyID; }

  /// True if this is an instance of an opaque PointerType.
  LLVM_DEPRECATED("Use isPointerTy() instead", "isPointerTy")
  bool isOpaquePointerTy() const { return isPointerTy(); };

  /// Return true if this is a pointer type or a vector of pointer types.
  bool isPtrOrPtrVectorTy() const { return getScalarType()->isPointerTy(); }

  /// True if this is an instance of VectorType.
  inline bool isVectorTy() const {
    return getTypeID() == ScalableVectorTyID || getTypeID() == FixedVectorTyID;
  }

  /// Return true if this type could be converted with a lossless BitCast to
  /// type 'Ty'. For example, i8* to i32*. BitCasts are valid for types of the
  /// same size only where no re-interpretation of the bits is done.
  /// Determine if this type could be losslessly bitcast to Ty
  bool canLosslesslyBitCastTo(Type *Ty) const;

  /// Return true if this type is empty, that is, it has no elements or all of
  /// its elements are empty.
  bool isEmptyTy() const;

  /// Return true if the type is "first class", meaning it is a valid type for a
  /// Value.
  bool isFirstClassType() const {
    return getTypeID() != FunctionTyID && getTypeID() != VoidTyID;
  }

  /// Return true if the type is a valid type for a register in codegen. This
  /// includes all first-class types except struct and array types.
  bool isSingleValueType() const {
    return isFloatingPointTy() || isX86_MMXTy() || isIntegerTy() ||
           isPointerTy() || isVectorTy() || isX86_AMXTy() || isTargetExtTy();
  }

  /// Return true if the type is an aggregate type. This means it is valid as
  /// the first operand of an insertvalue or extractvalue instruction. This
  /// includes struct and array types, but does not include vector types.
  bool isAggregateType() const {
    return getTypeID() == StructTyID || getTypeID() == ArrayTyID;
  }

  /// Return true if it makes sense to take the size of this type. To get the
  /// actual size for a particular target, it is reasonable to use the
  /// DataLayout subsystem to do this.
  bool isSized(SmallPtrSetImpl<Type*> *Visited = nullptr) const {
    // If it's a primitive, it is always sized.
    if (getTypeID() == IntegerTyID || isFloatingPointTy() ||
        getTypeID() == PointerTyID || getTypeID() == X86_MMXTyID ||
        getTypeID() == X86_AMXTyID)
      return true;
    // If it is not something that can have a size (e.g. a function or label),
    // it doesn't have a size.
    if (getTypeID() != StructTyID && getTypeID() != ArrayTyID &&
        !isVectorTy() && getTypeID() != TargetExtTyID)
      return false;
    // Otherwise we have to try harder to decide.
    return isSizedDerivedType(Visited);
  }

  /// Return the basic size of this type if it is a primitive type. These are
  /// fixed by LLVM and are not target-dependent.
  /// This will return zero if the type does not have a size or is not a
  /// primitive type.
  ///
  /// If this is a scalable vector type, the scalable property will be set and
  /// the runtime size will be a positive integer multiple of the base size.
  ///
  /// Note that this may not reflect the size of memory allocated for an
  /// instance of the type or the number of bytes that are written when an
  /// instance of the type is stored to memory. The DataLayout class provides
  /// additional query functions to provide this information.
  ///
  TypeSize getPrimitiveSizeInBits() const LLVM_READONLY;

  /// If this is a vector type, return the getPrimitiveSizeInBits value for the
  /// element type. Otherwise return the getPrimitiveSizeInBits value for this
  /// type.
  unsigned getScalarSizeInBits() const LLVM_READONLY;

  /// Return the width of the mantissa of this type. This is only valid on
  /// floating-point types. If the FP type does not have a stable mantissa (e.g.
  /// ppc long double), this method returns -1.
  int getFPMantissaWidth() const;

  /// Return whether the type is IEEE compatible, as defined by the eponymous
  /// method in APFloat.
  bool isIEEE() const;

  /// If this is a vector type, return the element type, otherwise return
  /// 'this'.
  inline Type *getScalarType() const {
    if (isVectorTy())
      return getContainedType(0);
    return const_cast<Type *>(this);
  }

  //===--------------------------------------------------------------------===//
  // Type Iteration support.
  //
  using subtype_iterator = Type * const *;

  subtype_iterator subtype_begin() const { return ContainedTys; }
  subtype_iterator subtype_end() const { return &ContainedTys[NumContainedTys];}
  ArrayRef<Type*> subtypes() const {
    return ArrayRef(subtype_begin(), subtype_end());
  }

  using subtype_reverse_iterator = std::reverse_iterator<subtype_iterator>;

  subtype_reverse_iterator subtype_rbegin() const {
    return subtype_reverse_iterator(subtype_end());
  }
  subtype_reverse_iterator subtype_rend() const {
    return subtype_reverse_iterator(subtype_begin());
  }

  /// This method is used to implement the type iterator (defined at the end of
  /// the file). For derived types, this returns the types 'contained' in the
  /// derived type.
  Type *getContainedType(unsigned i) const {
    assert(i < NumContainedTys && "Index out of range!");
    return ContainedTys[i];
  }

  /// Return the number of types in the derived type.
  unsigned getNumContainedTypes() const { return NumContainedTys; }

  //===--------------------------------------------------------------------===//
  // Helper methods corresponding to subclass methods.  This forces a cast to
  // the specified subclass and calls its accessor.  "getArrayNumElements" (for
  // example) is shorthand for cast<ArrayType>(Ty)->getNumElements().  This is
  // only intended to cover the core methods that are frequently used, helper
  // methods should not be added here.

  inline unsigned getIntegerBitWidth() const;

  inline Type *getFunctionParamType(unsigned i) const;
  inline unsigned getFunctionNumParams() const;
  inline bool isFunctionVarArg() const;

  inline StringRef getStructName() const;
  inline unsigned getStructNumElements() const;
  inline Type *getStructElementType(unsigned N) const;

  inline uint64_t getArrayNumElements() const;

  Type *getArrayElementType() const {
    assert(getTypeID() == ArrayTyID);
    return ContainedTys[0];
  }

  inline StringRef getTargetExtName() const;

  /// Only use this method in code that is not reachable with opaque pointers,
  /// or part of deprecated methods that will be removed as part of the opaque
  /// pointers transition.
  [[deprecated("Pointers no longer have element types")]]
  Type *getNonOpaquePointerElementType() const {
    llvm_unreachable("Pointers no longer have element types");
  }

  /// Given vector type, change the element type,
  /// whilst keeping the old number of elements.
  /// For non-vectors simply returns \p EltTy.
  inline Type *getWithNewType(Type *EltTy) const;

  /// Given an integer or vector type, change the lane bitwidth to NewBitwidth,
  /// whilst keeping the old number of lanes.
  inline Type *getWithNewBitWidth(unsigned NewBitWidth) const;

  /// Given scalar/vector integer type, returns a type with elements twice as
  /// wide as in the original type. For vectors, preserves element count.
  inline Type *getExtendedType() const;

  /// Get the address space of this pointer or pointer vector type.
  inline unsigned getPointerAddressSpace() const;

  //===--------------------------------------------------------------------===//
  // Static members exported by the Type class itself.  Useful for getting
  // instances of Type.
  //

  /// Return a type based on an identifier.
  static Type *getPrimitiveType(LLVMContext &C, TypeID IDNumber);

  //===--------------------------------------------------------------------===//
  // These are the builtin types that are always available.
  //
  static Type *getVoidTy(LLVMContext &C);
  static Type *getLabelTy(LLVMContext &C);
  static Type *getHalfTy(LLVMContext &C);
  static Type *getBFloatTy(LLVMContext &C);
  static Type *getFloatTy(LLVMContext &C);
  static Type *getDoubleTy(LLVMContext &C);
  static Type *getMetadataTy(LLVMContext &C);
  static Type *getX86_FP80Ty(LLVMContext &C);
  static Type *getFP128Ty(LLVMContext &C);
  static Type *getPPC_FP128Ty(LLVMContext &C);
  static Type *getX86_MMXTy(LLVMContext &C);
  static Type *getX86_AMXTy(LLVMContext &C);
  static Type *getTokenTy(LLVMContext &C);
  static IntegerType *getIntNTy(LLVMContext &C, unsigned N);
  static IntegerType *getInt1Ty(LLVMContext &C);
  static IntegerType *getInt8Ty(LLVMContext &C);
  static IntegerType *getInt16Ty(LLVMContext &C);
  static IntegerType *getInt32Ty(LLVMContext &C);
  static IntegerType *getInt64Ty(LLVMContext &C);
  static IntegerType *getInt128Ty(LLVMContext &C);
  template <typename ScalarTy> static Type *getScalarTy(LLVMContext &C) {
    int noOfBits = sizeof(ScalarTy) * CHAR_BIT;
    if (std::is_integral<ScalarTy>::value) {
      return (Type*) Type::getIntNTy(C, noOfBits);
    } else if (std::is_floating_point<ScalarTy>::value) {
      switch (noOfBits) {
      case 32:
        return Type::getFloatTy(C);
      case 64:
        return Type::getDoubleTy(C);
      }
    }
    llvm_unreachable("Unsupported type in Type::getScalarTy");
  }
  static Type *getFloatingPointTy(LLVMContext &C, const fltSemantics &S);

  //===--------------------------------------------------------------------===//
  // Convenience methods for getting pointer types.
  //
  static Type *getWasm_ExternrefTy(LLVMContext &C);
  static Type *getWasm_FuncrefTy(LLVMContext &C);

  /// Return a pointer to the current type. This is equivalent to
  /// PointerType::get(Foo, AddrSpace).
  /// TODO: Remove this after opaque pointer transition is complete.
  PointerType *getPointerTo(unsigned AddrSpace = 0) const;

private:
  /// Derived types like structures and arrays are sized iff all of the members
  /// of the type are sized as well. Since asking for their size is relatively
  /// uncommon, move this operation out-of-line.
  bool isSizedDerivedType(SmallPtrSetImpl<Type*> *Visited = nullptr) const;
};

// Printing of types.
inline raw_ostream &operator<<(raw_ostream &OS, const Type &T) {
  T.print(OS);
  return OS;
}

// allow isa<PointerType>(x) to work without DerivedTypes.h included.
template <> struct isa_impl<PointerType, Type> {
  static inline bool doit(const Type &Ty) {
    return Ty.getTypeID() == Type::PointerTyID;
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_ISA_CONVERSION_FUNCTIONS(Type, LLVMTypeRef)

/* Specialized opaque type conversions.
 */
inline Type **unwrap(LLVMTypeRef* Tys) {
  return reinterpret_cast<Type**>(Tys);
}

inline LLVMTypeRef *wrap(Type **Tys) {
  return reinterpret_cast<LLVMTypeRef*>(const_cast<Type**>(Tys));
}

} // end namespace llvm

#endif // LLVM_IR_TYPE_H
