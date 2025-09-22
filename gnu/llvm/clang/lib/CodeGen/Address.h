//===-- Address.h - An aligned address -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class provides a simple wrapper for a pair of a pointer and an
// alignment.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_ADDRESS_H
#define LLVM_CLANG_LIB_CODEGEN_ADDRESS_H

#include "CGPointerAuthInfo.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/MathExtras.h"

namespace clang {
namespace CodeGen {

class Address;
class CGBuilderTy;
class CodeGenFunction;
class CodeGenModule;

// Indicates whether a pointer is known not to be null.
enum KnownNonNull_t { NotKnownNonNull, KnownNonNull };

/// An abstract representation of an aligned address. This is designed to be an
/// IR-level abstraction, carrying just the information necessary to perform IR
/// operations on an address like loads and stores.  In particular, it doesn't
/// carry C type information or allow the representation of things like
/// bit-fields; clients working at that level should generally be using
/// `LValue`.
/// The pointer contained in this class is known to be unsigned.
class RawAddress {
  llvm::PointerIntPair<llvm::Value *, 1, bool> PointerAndKnownNonNull;
  llvm::Type *ElementType;
  CharUnits Alignment;

protected:
  RawAddress(std::nullptr_t) : ElementType(nullptr) {}

public:
  RawAddress(llvm::Value *Pointer, llvm::Type *ElementType, CharUnits Alignment,
             KnownNonNull_t IsKnownNonNull = NotKnownNonNull)
      : PointerAndKnownNonNull(Pointer, IsKnownNonNull),
        ElementType(ElementType), Alignment(Alignment) {
    assert(Pointer != nullptr && "Pointer cannot be null");
    assert(ElementType != nullptr && "Element type cannot be null");
  }

  inline RawAddress(Address Addr);

  static RawAddress invalid() { return RawAddress(nullptr); }
  bool isValid() const {
    return PointerAndKnownNonNull.getPointer() != nullptr;
  }

  llvm::Value *getPointer() const {
    assert(isValid());
    return PointerAndKnownNonNull.getPointer();
  }

  /// Return the type of the pointer value.
  llvm::PointerType *getType() const {
    return llvm::cast<llvm::PointerType>(getPointer()->getType());
  }

  /// Return the type of the values stored in this address.
  llvm::Type *getElementType() const {
    assert(isValid());
    return ElementType;
  }

  /// Return the address space that this address resides in.
  unsigned getAddressSpace() const {
    return getType()->getAddressSpace();
  }

  /// Return the IR name of the pointer value.
  llvm::StringRef getName() const {
    return getPointer()->getName();
  }

  /// Return the alignment of this pointer.
  CharUnits getAlignment() const {
    assert(isValid());
    return Alignment;
  }

  /// Return address with different element type, but same pointer and
  /// alignment.
  RawAddress withElementType(llvm::Type *ElemTy) const {
    return RawAddress(getPointer(), ElemTy, getAlignment(), isKnownNonNull());
  }

  KnownNonNull_t isKnownNonNull() const {
    assert(isValid());
    return (KnownNonNull_t)PointerAndKnownNonNull.getInt();
  }
};

/// Like RawAddress, an abstract representation of an aligned address, but the
/// pointer contained in this class is possibly signed.
///
/// This is designed to be an IR-level abstraction, carrying just the
/// information necessary to perform IR operations on an address like loads and
/// stores.  In particular, it doesn't carry C type information or allow the
/// representation of things like bit-fields; clients working at that level
/// should generally be using `LValue`.
///
/// An address may be either *raw*, meaning that it's an ordinary machine
/// pointer, or *signed*, meaning that the pointer carries an embedded
/// pointer-authentication signature. Representing signed pointers directly in
/// this abstraction allows the authentication to be delayed as long as possible
/// without forcing IRGen to use totally different code paths for signed and
/// unsigned values or to separately propagate signature information through
/// every API that manipulates addresses. Pointer arithmetic on signed addresses
/// (e.g. drilling down to a struct field) is accumulated into a separate offset
/// which is applied when the address is finally accessed.
class Address {
  friend class CGBuilderTy;

  // The boolean flag indicates whether the pointer is known to be non-null.
  llvm::PointerIntPair<llvm::Value *, 1, bool> Pointer;

  /// The expected IR type of the pointer. Carrying accurate element type
  /// information in Address makes it more convenient to work with Address
  /// values and allows frontend assertions to catch simple mistakes.
  llvm::Type *ElementType = nullptr;

  CharUnits Alignment;

  /// The ptrauth information needed to authenticate the base pointer.
  CGPointerAuthInfo PtrAuthInfo;

  /// Offset from the base pointer. This is non-null only when the base
  /// pointer is signed.
  llvm::Value *Offset = nullptr;

  llvm::Value *emitRawPointerSlow(CodeGenFunction &CGF) const;

protected:
  Address(std::nullptr_t) : ElementType(nullptr) {}

public:
  Address(llvm::Value *pointer, llvm::Type *elementType, CharUnits alignment,
          KnownNonNull_t IsKnownNonNull = NotKnownNonNull)
      : Pointer(pointer, IsKnownNonNull), ElementType(elementType),
        Alignment(alignment) {
    assert(pointer != nullptr && "Pointer cannot be null");
    assert(elementType != nullptr && "Element type cannot be null");
    assert(!alignment.isZero() && "Alignment cannot be zero");
  }

  Address(llvm::Value *BasePtr, llvm::Type *ElementType, CharUnits Alignment,
          CGPointerAuthInfo PtrAuthInfo, llvm::Value *Offset,
          KnownNonNull_t IsKnownNonNull = NotKnownNonNull)
      : Pointer(BasePtr, IsKnownNonNull), ElementType(ElementType),
        Alignment(Alignment), PtrAuthInfo(PtrAuthInfo), Offset(Offset) {}

  Address(RawAddress RawAddr)
      : Pointer(RawAddr.isValid() ? RawAddr.getPointer() : nullptr,
                RawAddr.isValid() ? RawAddr.isKnownNonNull() : NotKnownNonNull),
        ElementType(RawAddr.isValid() ? RawAddr.getElementType() : nullptr),
        Alignment(RawAddr.isValid() ? RawAddr.getAlignment()
                                    : CharUnits::Zero()) {}

  static Address invalid() { return Address(nullptr); }
  bool isValid() const { return Pointer.getPointer() != nullptr; }

  /// This function is used in situations where the caller is doing some sort of
  /// opaque "laundering" of the pointer.
  void replaceBasePointer(llvm::Value *P) {
    assert(isValid() && "pointer isn't valid");
    assert(P->getType() == Pointer.getPointer()->getType() &&
           "Pointer's type changed");
    Pointer.setPointer(P);
    assert(isValid() && "pointer is invalid after replacement");
  }

  CharUnits getAlignment() const { return Alignment; }

  void setAlignment(CharUnits Value) { Alignment = Value; }

  llvm::Value *getBasePointer() const {
    assert(isValid() && "pointer isn't valid");
    return Pointer.getPointer();
  }

  /// Return the type of the pointer value.
  llvm::PointerType *getType() const {
    return llvm::PointerType::get(
        ElementType,
        llvm::cast<llvm::PointerType>(Pointer.getPointer()->getType())
            ->getAddressSpace());
  }

  /// Return the type of the values stored in this address.
  llvm::Type *getElementType() const {
    assert(isValid());
    return ElementType;
  }

  /// Return the address space that this address resides in.
  unsigned getAddressSpace() const { return getType()->getAddressSpace(); }

  /// Return the IR name of the pointer value.
  llvm::StringRef getName() const { return Pointer.getPointer()->getName(); }

  const CGPointerAuthInfo &getPointerAuthInfo() const { return PtrAuthInfo; }
  void setPointerAuthInfo(const CGPointerAuthInfo &Info) { PtrAuthInfo = Info; }

  // This function is called only in CGBuilderBaseTy::CreateElementBitCast.
  void setElementType(llvm::Type *Ty) {
    assert(hasOffset() &&
           "this funcion shouldn't be called when there is no offset");
    ElementType = Ty;
  }

  bool isSigned() const { return PtrAuthInfo.isSigned(); }

  /// Whether the pointer is known not to be null.
  KnownNonNull_t isKnownNonNull() const {
    assert(isValid());
    return (KnownNonNull_t)Pointer.getInt();
  }

  Address setKnownNonNull() {
    assert(isValid());
    Pointer.setInt(KnownNonNull);
    return *this;
  }

  bool hasOffset() const { return Offset; }

  llvm::Value *getOffset() const { return Offset; }

  Address getResignedAddress(const CGPointerAuthInfo &NewInfo,
                             CodeGenFunction &CGF) const;

  /// Return the pointer contained in this class after authenticating it and
  /// adding offset to it if necessary.
  llvm::Value *emitRawPointer(CodeGenFunction &CGF) const {
    if (!isSigned())
      return getBasePointer();
    return emitRawPointerSlow(CGF);
  }

  /// Return address with different pointer, but same element type and
  /// alignment.
  Address withPointer(llvm::Value *NewPointer,
                      KnownNonNull_t IsKnownNonNull) const {
    return Address(NewPointer, getElementType(), getAlignment(),
                   IsKnownNonNull);
  }

  /// Return address with different alignment, but same pointer and element
  /// type.
  Address withAlignment(CharUnits NewAlignment) const {
    return Address(Pointer.getPointer(), getElementType(), NewAlignment,
                   isKnownNonNull());
  }

  /// Return address with different element type, but same pointer and
  /// alignment.
  Address withElementType(llvm::Type *ElemTy) const {
    if (!hasOffset())
      return Address(getBasePointer(), ElemTy, getAlignment(),
                     getPointerAuthInfo(), /*Offset=*/nullptr,
                     isKnownNonNull());
    Address A(*this);
    A.ElementType = ElemTy;
    return A;
  }
};

inline RawAddress::RawAddress(Address Addr)
    : PointerAndKnownNonNull(Addr.isValid() ? Addr.getBasePointer() : nullptr,
                             Addr.isValid() ? Addr.isKnownNonNull()
                                            : NotKnownNonNull),
      ElementType(Addr.isValid() ? Addr.getElementType() : nullptr),
      Alignment(Addr.isValid() ? Addr.getAlignment() : CharUnits::Zero()) {}

/// A specialization of Address that requires the address to be an
/// LLVM Constant.
class ConstantAddress : public RawAddress {
  ConstantAddress(std::nullptr_t) : RawAddress(nullptr) {}

public:
  ConstantAddress(llvm::Constant *pointer, llvm::Type *elementType,
                  CharUnits alignment)
      : RawAddress(pointer, elementType, alignment) {}

  static ConstantAddress invalid() {
    return ConstantAddress(nullptr);
  }

  llvm::Constant *getPointer() const {
    return llvm::cast<llvm::Constant>(RawAddress::getPointer());
  }

  ConstantAddress withElementType(llvm::Type *ElemTy) const {
    return ConstantAddress(getPointer(), ElemTy, getAlignment());
  }

  static bool isaImpl(RawAddress addr) {
    return llvm::isa<llvm::Constant>(addr.getPointer());
  }
  static ConstantAddress castImpl(RawAddress addr) {
    return ConstantAddress(llvm::cast<llvm::Constant>(addr.getPointer()),
                           addr.getElementType(), addr.getAlignment());
  }
};
}

// Present a minimal LLVM-like casting interface.
template <class U> inline U cast(CodeGen::Address addr) {
  return U::castImpl(addr);
}
template <class U> inline bool isa(CodeGen::Address addr) {
  return U::isaImpl(addr);
}

}

#endif
