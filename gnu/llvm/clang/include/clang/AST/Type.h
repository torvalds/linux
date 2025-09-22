//===- Type.h - C Language Family Type Representation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// C Language Family Type Representation
///
/// This file defines the clang::Type interface and subclasses, used to
/// represent types for languages in the C family.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TYPE_H
#define LLVM_CLANG_AST_TYPE_H

#include "clang/AST/DependenceFlags.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateName.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/PointerAuthOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/Visibility.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/TrailingObjects.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace clang {

class BTFTypeTagAttr;
class ExtQuals;
class QualType;
class ConceptDecl;
class ValueDecl;
class TagDecl;
class TemplateParameterList;
class Type;

enum {
  TypeAlignmentInBits = 4,
  TypeAlignment = 1 << TypeAlignmentInBits
};

namespace serialization {
  template <class T> class AbstractTypeReader;
  template <class T> class AbstractTypeWriter;
}

} // namespace clang

namespace llvm {

  template <typename T>
  struct PointerLikeTypeTraits;
  template<>
  struct PointerLikeTypeTraits< ::clang::Type*> {
    static inline void *getAsVoidPointer(::clang::Type *P) { return P; }

    static inline ::clang::Type *getFromVoidPointer(void *P) {
      return static_cast< ::clang::Type*>(P);
    }

    static constexpr int NumLowBitsAvailable = clang::TypeAlignmentInBits;
  };

  template<>
  struct PointerLikeTypeTraits< ::clang::ExtQuals*> {
    static inline void *getAsVoidPointer(::clang::ExtQuals *P) { return P; }

    static inline ::clang::ExtQuals *getFromVoidPointer(void *P) {
      return static_cast< ::clang::ExtQuals*>(P);
    }

    static constexpr int NumLowBitsAvailable = clang::TypeAlignmentInBits;
  };

} // namespace llvm

namespace clang {

class ASTContext;
template <typename> class CanQual;
class CXXRecordDecl;
class DeclContext;
class EnumDecl;
class Expr;
class ExtQualsTypeCommonBase;
class FunctionDecl;
class FunctionEffectSet;
class IdentifierInfo;
class NamedDecl;
class ObjCInterfaceDecl;
class ObjCProtocolDecl;
class ObjCTypeParamDecl;
struct PrintingPolicy;
class RecordDecl;
class Stmt;
class TagDecl;
class TemplateArgument;
class TemplateArgumentListInfo;
class TemplateArgumentLoc;
class TemplateTypeParmDecl;
class TypedefNameDecl;
class UnresolvedUsingTypenameDecl;
class UsingShadowDecl;

using CanQualType = CanQual<Type>;

// Provide forward declarations for all of the *Type classes.
#define TYPE(Class, Base) class Class##Type;
#include "clang/AST/TypeNodes.inc"

/// Pointer-authentication qualifiers.
class PointerAuthQualifier {
  enum : uint32_t {
    EnabledShift = 0,
    EnabledBits = 1,
    EnabledMask = 1 << EnabledShift,
    AddressDiscriminatedShift = EnabledShift + EnabledBits,
    AddressDiscriminatedBits = 1,
    AddressDiscriminatedMask = 1 << AddressDiscriminatedShift,
    AuthenticationModeShift =
        AddressDiscriminatedShift + AddressDiscriminatedBits,
    AuthenticationModeBits = 2,
    AuthenticationModeMask = ((1 << AuthenticationModeBits) - 1)
                             << AuthenticationModeShift,
    IsaPointerShift = AuthenticationModeShift + AuthenticationModeBits,
    IsaPointerBits = 1,
    IsaPointerMask = ((1 << IsaPointerBits) - 1) << IsaPointerShift,
    AuthenticatesNullValuesShift = IsaPointerShift + IsaPointerBits,
    AuthenticatesNullValuesBits = 1,
    AuthenticatesNullValuesMask = ((1 << AuthenticatesNullValuesBits) - 1)
                                  << AuthenticatesNullValuesShift,
    KeyShift = AuthenticatesNullValuesShift + AuthenticatesNullValuesBits,
    KeyBits = 10,
    KeyMask = ((1 << KeyBits) - 1) << KeyShift,
    DiscriminatorShift = KeyShift + KeyBits,
    DiscriminatorBits = 16,
    DiscriminatorMask = ((1u << DiscriminatorBits) - 1) << DiscriminatorShift,
  };

  // bits:     |0      |1      |2..3              |4          |
  //           |Enabled|Address|AuthenticationMode|ISA pointer|
  // bits:     |5                |6..15|   16...31   |
  //           |AuthenticatesNull|Key  |Discriminator|
  uint32_t Data = 0;

  // The following static assertions check that each of the 32 bits is present
  // exactly in one of the constants.
  static_assert((EnabledBits + AddressDiscriminatedBits +
                 AuthenticationModeBits + IsaPointerBits +
                 AuthenticatesNullValuesBits + KeyBits + DiscriminatorBits) ==
                    32,
                "PointerAuthQualifier should be exactly 32 bits");
  static_assert((EnabledMask + AddressDiscriminatedMask +
                 AuthenticationModeMask + IsaPointerMask +
                 AuthenticatesNullValuesMask + KeyMask + DiscriminatorMask) ==
                    0xFFFFFFFF,
                "All masks should cover the entire bits");
  static_assert((EnabledMask ^ AddressDiscriminatedMask ^
                 AuthenticationModeMask ^ IsaPointerMask ^
                 AuthenticatesNullValuesMask ^ KeyMask ^ DiscriminatorMask) ==
                    0xFFFFFFFF,
                "All masks should cover the entire bits");

  PointerAuthQualifier(unsigned Key, bool IsAddressDiscriminated,
                       unsigned ExtraDiscriminator,
                       PointerAuthenticationMode AuthenticationMode,
                       bool IsIsaPointer, bool AuthenticatesNullValues)
      : Data(EnabledMask |
             (IsAddressDiscriminated
                  ? llvm::to_underlying(AddressDiscriminatedMask)
                  : 0) |
             (Key << KeyShift) |
             (llvm::to_underlying(AuthenticationMode)
              << AuthenticationModeShift) |
             (ExtraDiscriminator << DiscriminatorShift) |
             (IsIsaPointer << IsaPointerShift) |
             (AuthenticatesNullValues << AuthenticatesNullValuesShift)) {
    assert(Key <= KeyNoneInternal);
    assert(ExtraDiscriminator <= MaxDiscriminator);
    assert((Data == 0) ==
           (getAuthenticationMode() == PointerAuthenticationMode::None));
  }

public:
  enum {
    KeyNoneInternal = (1u << KeyBits) - 1,

    /// The maximum supported pointer-authentication key.
    MaxKey = KeyNoneInternal - 1,

    /// The maximum supported pointer-authentication discriminator.
    MaxDiscriminator = (1u << DiscriminatorBits) - 1
  };

public:
  PointerAuthQualifier() = default;

  static PointerAuthQualifier
  Create(unsigned Key, bool IsAddressDiscriminated, unsigned ExtraDiscriminator,
         PointerAuthenticationMode AuthenticationMode, bool IsIsaPointer,
         bool AuthenticatesNullValues) {
    if (Key == PointerAuthKeyNone)
      Key = KeyNoneInternal;
    assert(Key <= KeyNoneInternal && "out-of-range key value");
    return PointerAuthQualifier(Key, IsAddressDiscriminated, ExtraDiscriminator,
                                AuthenticationMode, IsIsaPointer,
                                AuthenticatesNullValues);
  }

  bool isPresent() const {
    assert((Data == 0) ==
           (getAuthenticationMode() == PointerAuthenticationMode::None));
    return Data != 0;
  }

  explicit operator bool() const { return isPresent(); }

  unsigned getKey() const {
    assert(isPresent());
    return (Data & KeyMask) >> KeyShift;
  }

  bool hasKeyNone() const { return isPresent() && getKey() == KeyNoneInternal; }

  bool isAddressDiscriminated() const {
    assert(isPresent());
    return (Data & AddressDiscriminatedMask) >> AddressDiscriminatedShift;
  }

  unsigned getExtraDiscriminator() const {
    assert(isPresent());
    return (Data >> DiscriminatorShift);
  }

  PointerAuthenticationMode getAuthenticationMode() const {
    return PointerAuthenticationMode((Data & AuthenticationModeMask) >>
                                     AuthenticationModeShift);
  }

  bool isIsaPointer() const {
    assert(isPresent());
    return (Data & IsaPointerMask) >> IsaPointerShift;
  }

  bool authenticatesNullValues() const {
    assert(isPresent());
    return (Data & AuthenticatesNullValuesMask) >> AuthenticatesNullValuesShift;
  }

  PointerAuthQualifier withoutKeyNone() const {
    return hasKeyNone() ? PointerAuthQualifier() : *this;
  }

  friend bool operator==(PointerAuthQualifier Lhs, PointerAuthQualifier Rhs) {
    return Lhs.Data == Rhs.Data;
  }
  friend bool operator!=(PointerAuthQualifier Lhs, PointerAuthQualifier Rhs) {
    return Lhs.Data != Rhs.Data;
  }

  bool isEquivalent(PointerAuthQualifier Other) const {
    return withoutKeyNone() == Other.withoutKeyNone();
  }

  uint32_t getAsOpaqueValue() const { return Data; }

  // Deserialize pointer-auth qualifiers from an opaque representation.
  static PointerAuthQualifier fromOpaqueValue(uint32_t Opaque) {
    PointerAuthQualifier Result;
    Result.Data = Opaque;
    assert((Result.Data == 0) ==
           (Result.getAuthenticationMode() == PointerAuthenticationMode::None));
    return Result;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(Data); }
};

/// The collection of all-type qualifiers we support.
/// Clang supports five independent qualifiers:
/// * C99: const, volatile, and restrict
/// * MS: __unaligned
/// * Embedded C (TR18037): address spaces
/// * Objective C: the GC attributes (none, weak, or strong)
class Qualifiers {
public:
  enum TQ : uint64_t {
    // NOTE: These flags must be kept in sync with DeclSpec::TQ.
    Const = 0x1,
    Restrict = 0x2,
    Volatile = 0x4,
    CVRMask = Const | Volatile | Restrict
  };

  enum GC {
    GCNone = 0,
    Weak,
    Strong
  };

  enum ObjCLifetime {
    /// There is no lifetime qualification on this type.
    OCL_None,

    /// This object can be modified without requiring retains or
    /// releases.
    OCL_ExplicitNone,

    /// Assigning into this object requires the old value to be
    /// released and the new value to be retained.  The timing of the
    /// release of the old value is inexact: it may be moved to
    /// immediately after the last known point where the value is
    /// live.
    OCL_Strong,

    /// Reading or writing from this object requires a barrier call.
    OCL_Weak,

    /// Assigning into this object requires a lifetime extension.
    OCL_Autoreleasing
  };

  enum : uint64_t {
    /// The maximum supported address space number.
    /// 23 bits should be enough for anyone.
    MaxAddressSpace = 0x7fffffu,

    /// The width of the "fast" qualifier mask.
    FastWidth = 3,

    /// The fast qualifier mask.
    FastMask = (1 << FastWidth) - 1
  };

  /// Returns the common set of qualifiers while removing them from
  /// the given sets.
  static Qualifiers removeCommonQualifiers(Qualifiers &L, Qualifiers &R) {
    Qualifiers Q;
    PointerAuthQualifier LPtrAuth = L.getPointerAuth();
    if (LPtrAuth.isPresent() &&
        LPtrAuth.getKey() != PointerAuthQualifier::KeyNoneInternal &&
        LPtrAuth == R.getPointerAuth()) {
      Q.setPointerAuth(LPtrAuth);
      PointerAuthQualifier Empty;
      L.setPointerAuth(Empty);
      R.setPointerAuth(Empty);
    }

    // If both are only CVR-qualified, bit operations are sufficient.
    if (!(L.Mask & ~CVRMask) && !(R.Mask & ~CVRMask)) {
      Q.Mask = L.Mask & R.Mask;
      L.Mask &= ~Q.Mask;
      R.Mask &= ~Q.Mask;
      return Q;
    }

    unsigned CommonCRV = L.getCVRQualifiers() & R.getCVRQualifiers();
    Q.addCVRQualifiers(CommonCRV);
    L.removeCVRQualifiers(CommonCRV);
    R.removeCVRQualifiers(CommonCRV);

    if (L.getObjCGCAttr() == R.getObjCGCAttr()) {
      Q.setObjCGCAttr(L.getObjCGCAttr());
      L.removeObjCGCAttr();
      R.removeObjCGCAttr();
    }

    if (L.getObjCLifetime() == R.getObjCLifetime()) {
      Q.setObjCLifetime(L.getObjCLifetime());
      L.removeObjCLifetime();
      R.removeObjCLifetime();
    }

    if (L.getAddressSpace() == R.getAddressSpace()) {
      Q.setAddressSpace(L.getAddressSpace());
      L.removeAddressSpace();
      R.removeAddressSpace();
    }
    return Q;
  }

  static Qualifiers fromFastMask(unsigned Mask) {
    Qualifiers Qs;
    Qs.addFastQualifiers(Mask);
    return Qs;
  }

  static Qualifiers fromCVRMask(unsigned CVR) {
    Qualifiers Qs;
    Qs.addCVRQualifiers(CVR);
    return Qs;
  }

  static Qualifiers fromCVRUMask(unsigned CVRU) {
    Qualifiers Qs;
    Qs.addCVRUQualifiers(CVRU);
    return Qs;
  }

  // Deserialize qualifiers from an opaque representation.
  static Qualifiers fromOpaqueValue(uint64_t opaque) {
    Qualifiers Qs;
    Qs.Mask = opaque;
    return Qs;
  }

  // Serialize these qualifiers into an opaque representation.
  uint64_t getAsOpaqueValue() const { return Mask; }

  bool hasConst() const { return Mask & Const; }
  bool hasOnlyConst() const { return Mask == Const; }
  void removeConst() { Mask &= ~Const; }
  void addConst() { Mask |= Const; }
  Qualifiers withConst() const {
    Qualifiers Qs = *this;
    Qs.addConst();
    return Qs;
  }

  bool hasVolatile() const { return Mask & Volatile; }
  bool hasOnlyVolatile() const { return Mask == Volatile; }
  void removeVolatile() { Mask &= ~Volatile; }
  void addVolatile() { Mask |= Volatile; }
  Qualifiers withVolatile() const {
    Qualifiers Qs = *this;
    Qs.addVolatile();
    return Qs;
  }

  bool hasRestrict() const { return Mask & Restrict; }
  bool hasOnlyRestrict() const { return Mask == Restrict; }
  void removeRestrict() { Mask &= ~Restrict; }
  void addRestrict() { Mask |= Restrict; }
  Qualifiers withRestrict() const {
    Qualifiers Qs = *this;
    Qs.addRestrict();
    return Qs;
  }

  bool hasCVRQualifiers() const { return getCVRQualifiers(); }
  unsigned getCVRQualifiers() const { return Mask & CVRMask; }
  unsigned getCVRUQualifiers() const { return Mask & (CVRMask | UMask); }

  void setCVRQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask) && "bitmask contains non-CVR bits");
    Mask = (Mask & ~CVRMask) | mask;
  }
  void removeCVRQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask) && "bitmask contains non-CVR bits");
    Mask &= ~static_cast<uint64_t>(mask);
  }
  void removeCVRQualifiers() {
    removeCVRQualifiers(CVRMask);
  }
  void addCVRQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask) && "bitmask contains non-CVR bits");
    Mask |= mask;
  }
  void addCVRUQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask & ~UMask) && "bitmask contains non-CVRU bits");
    Mask |= mask;
  }

  bool hasUnaligned() const { return Mask & UMask; }
  void setUnaligned(bool flag) {
    Mask = (Mask & ~UMask) | (flag ? UMask : 0);
  }
  void removeUnaligned() { Mask &= ~UMask; }
  void addUnaligned() { Mask |= UMask; }

  bool hasObjCGCAttr() const { return Mask & GCAttrMask; }
  GC getObjCGCAttr() const { return GC((Mask & GCAttrMask) >> GCAttrShift); }
  void setObjCGCAttr(GC type) {
    Mask = (Mask & ~GCAttrMask) | (type << GCAttrShift);
  }
  void removeObjCGCAttr() { setObjCGCAttr(GCNone); }
  void addObjCGCAttr(GC type) {
    assert(type);
    setObjCGCAttr(type);
  }
  Qualifiers withoutObjCGCAttr() const {
    Qualifiers qs = *this;
    qs.removeObjCGCAttr();
    return qs;
  }
  Qualifiers withoutObjCLifetime() const {
    Qualifiers qs = *this;
    qs.removeObjCLifetime();
    return qs;
  }
  Qualifiers withoutAddressSpace() const {
    Qualifiers qs = *this;
    qs.removeAddressSpace();
    return qs;
  }

  bool hasObjCLifetime() const { return Mask & LifetimeMask; }
  ObjCLifetime getObjCLifetime() const {
    return ObjCLifetime((Mask & LifetimeMask) >> LifetimeShift);
  }
  void setObjCLifetime(ObjCLifetime type) {
    Mask = (Mask & ~LifetimeMask) | (type << LifetimeShift);
  }
  void removeObjCLifetime() { setObjCLifetime(OCL_None); }
  void addObjCLifetime(ObjCLifetime type) {
    assert(type);
    assert(!hasObjCLifetime());
    Mask |= (type << LifetimeShift);
  }

  /// True if the lifetime is neither None or ExplicitNone.
  bool hasNonTrivialObjCLifetime() const {
    ObjCLifetime lifetime = getObjCLifetime();
    return (lifetime > OCL_ExplicitNone);
  }

  /// True if the lifetime is either strong or weak.
  bool hasStrongOrWeakObjCLifetime() const {
    ObjCLifetime lifetime = getObjCLifetime();
    return (lifetime == OCL_Strong || lifetime == OCL_Weak);
  }

  bool hasAddressSpace() const { return Mask & AddressSpaceMask; }
  LangAS getAddressSpace() const {
    return static_cast<LangAS>(Mask >> AddressSpaceShift);
  }
  bool hasTargetSpecificAddressSpace() const {
    return isTargetAddressSpace(getAddressSpace());
  }
  /// Get the address space attribute value to be printed by diagnostics.
  unsigned getAddressSpaceAttributePrintValue() const {
    auto Addr = getAddressSpace();
    // This function is not supposed to be used with language specific
    // address spaces. If that happens, the diagnostic message should consider
    // printing the QualType instead of the address space value.
    assert(Addr == LangAS::Default || hasTargetSpecificAddressSpace());
    if (Addr != LangAS::Default)
      return toTargetAddressSpace(Addr);
    // TODO: The diagnostic messages where Addr may be 0 should be fixed
    // since it cannot differentiate the situation where 0 denotes the default
    // address space or user specified __attribute__((address_space(0))).
    return 0;
  }
  void setAddressSpace(LangAS space) {
    assert((unsigned)space <= MaxAddressSpace);
    Mask = (Mask & ~AddressSpaceMask)
         | (((uint32_t) space) << AddressSpaceShift);
  }
  void removeAddressSpace() { setAddressSpace(LangAS::Default); }
  void addAddressSpace(LangAS space) {
    assert(space != LangAS::Default);
    setAddressSpace(space);
  }

  bool hasPointerAuth() const { return Mask & PtrAuthMask; }
  PointerAuthQualifier getPointerAuth() const {
    return PointerAuthQualifier::fromOpaqueValue(Mask >> PtrAuthShift);
  }
  void setPointerAuth(PointerAuthQualifier Q) {
    Mask = (Mask & ~PtrAuthMask) |
           (uint64_t(Q.getAsOpaqueValue()) << PtrAuthShift);
  }
  void removePointerAuth() { Mask &= ~PtrAuthMask; }
  void addPointerAuth(PointerAuthQualifier Q) {
    assert(Q.isPresent());
    setPointerAuth(Q);
  }

  // Fast qualifiers are those that can be allocated directly
  // on a QualType object.
  bool hasFastQualifiers() const { return getFastQualifiers(); }
  unsigned getFastQualifiers() const { return Mask & FastMask; }
  void setFastQualifiers(unsigned mask) {
    assert(!(mask & ~FastMask) && "bitmask contains non-fast qualifier bits");
    Mask = (Mask & ~FastMask) | mask;
  }
  void removeFastQualifiers(unsigned mask) {
    assert(!(mask & ~FastMask) && "bitmask contains non-fast qualifier bits");
    Mask &= ~static_cast<uint64_t>(mask);
  }
  void removeFastQualifiers() {
    removeFastQualifiers(FastMask);
  }
  void addFastQualifiers(unsigned mask) {
    assert(!(mask & ~FastMask) && "bitmask contains non-fast qualifier bits");
    Mask |= mask;
  }

  /// Return true if the set contains any qualifiers which require an ExtQuals
  /// node to be allocated.
  bool hasNonFastQualifiers() const { return Mask & ~FastMask; }
  Qualifiers getNonFastQualifiers() const {
    Qualifiers Quals = *this;
    Quals.setFastQualifiers(0);
    return Quals;
  }

  /// Return true if the set contains any qualifiers.
  bool hasQualifiers() const { return Mask; }
  bool empty() const { return !Mask; }

  /// Add the qualifiers from the given set to this set.
  void addQualifiers(Qualifiers Q) {
    // If the other set doesn't have any non-boolean qualifiers, just
    // bit-or it in.
    if (!(Q.Mask & ~CVRMask))
      Mask |= Q.Mask;
    else {
      Mask |= (Q.Mask & CVRMask);
      if (Q.hasAddressSpace())
        addAddressSpace(Q.getAddressSpace());
      if (Q.hasObjCGCAttr())
        addObjCGCAttr(Q.getObjCGCAttr());
      if (Q.hasObjCLifetime())
        addObjCLifetime(Q.getObjCLifetime());
      if (Q.hasPointerAuth())
        addPointerAuth(Q.getPointerAuth());
    }
  }

  /// Remove the qualifiers from the given set from this set.
  void removeQualifiers(Qualifiers Q) {
    // If the other set doesn't have any non-boolean qualifiers, just
    // bit-and the inverse in.
    if (!(Q.Mask & ~CVRMask))
      Mask &= ~Q.Mask;
    else {
      Mask &= ~(Q.Mask & CVRMask);
      if (getObjCGCAttr() == Q.getObjCGCAttr())
        removeObjCGCAttr();
      if (getObjCLifetime() == Q.getObjCLifetime())
        removeObjCLifetime();
      if (getAddressSpace() == Q.getAddressSpace())
        removeAddressSpace();
      if (getPointerAuth() == Q.getPointerAuth())
        removePointerAuth();
    }
  }

  /// Add the qualifiers from the given set to this set, given that
  /// they don't conflict.
  void addConsistentQualifiers(Qualifiers qs) {
    assert(getAddressSpace() == qs.getAddressSpace() ||
           !hasAddressSpace() || !qs.hasAddressSpace());
    assert(getObjCGCAttr() == qs.getObjCGCAttr() ||
           !hasObjCGCAttr() || !qs.hasObjCGCAttr());
    assert(getObjCLifetime() == qs.getObjCLifetime() ||
           !hasObjCLifetime() || !qs.hasObjCLifetime());
    assert(!hasPointerAuth() || !qs.hasPointerAuth() ||
           getPointerAuth() == qs.getPointerAuth());
    Mask |= qs.Mask;
  }

  /// Returns true if address space A is equal to or a superset of B.
  /// OpenCL v2.0 defines conversion rules (OpenCLC v2.0 s6.5.5) and notion of
  /// overlapping address spaces.
  /// CL1.1 or CL1.2:
  ///   every address space is a superset of itself.
  /// CL2.0 adds:
  ///   __generic is a superset of any address space except for __constant.
  static bool isAddressSpaceSupersetOf(LangAS A, LangAS B) {
    // Address spaces must match exactly.
    return A == B ||
           // Otherwise in OpenCLC v2.0 s6.5.5: every address space except
           // for __constant can be used as __generic.
           (A == LangAS::opencl_generic && B != LangAS::opencl_constant) ||
           // We also define global_device and global_host address spaces,
           // to distinguish global pointers allocated on host from pointers
           // allocated on device, which are a subset of __global.
           (A == LangAS::opencl_global && (B == LangAS::opencl_global_device ||
                                           B == LangAS::opencl_global_host)) ||
           (A == LangAS::sycl_global && (B == LangAS::sycl_global_device ||
                                         B == LangAS::sycl_global_host)) ||
           // Consider pointer size address spaces to be equivalent to default.
           ((isPtrSizeAddressSpace(A) || A == LangAS::Default) &&
            (isPtrSizeAddressSpace(B) || B == LangAS::Default)) ||
           // Default is a superset of SYCL address spaces.
           (A == LangAS::Default &&
            (B == LangAS::sycl_private || B == LangAS::sycl_local ||
             B == LangAS::sycl_global || B == LangAS::sycl_global_device ||
             B == LangAS::sycl_global_host)) ||
           // In HIP device compilation, any cuda address space is allowed
           // to implicitly cast into the default address space.
           (A == LangAS::Default &&
            (B == LangAS::cuda_constant || B == LangAS::cuda_device ||
             B == LangAS::cuda_shared));
  }

  /// Returns true if the address space in these qualifiers is equal to or
  /// a superset of the address space in the argument qualifiers.
  bool isAddressSpaceSupersetOf(Qualifiers other) const {
    return isAddressSpaceSupersetOf(getAddressSpace(), other.getAddressSpace());
  }

  /// Determines if these qualifiers compatibly include another set.
  /// Generally this answers the question of whether an object with the other
  /// qualifiers can be safely used as an object with these qualifiers.
  bool compatiblyIncludes(Qualifiers other) const {
    return isAddressSpaceSupersetOf(other) &&
           // ObjC GC qualifiers can match, be added, or be removed, but can't
           // be changed.
           (getObjCGCAttr() == other.getObjCGCAttr() || !hasObjCGCAttr() ||
            !other.hasObjCGCAttr()) &&
           // Pointer-auth qualifiers must match exactly.
           getPointerAuth() == other.getPointerAuth() &&
           // ObjC lifetime qualifiers must match exactly.
           getObjCLifetime() == other.getObjCLifetime() &&
           // CVR qualifiers may subset.
           (((Mask & CVRMask) | (other.Mask & CVRMask)) == (Mask & CVRMask)) &&
           // U qualifier may superset.
           (!other.hasUnaligned() || hasUnaligned());
  }

  /// Determines if these qualifiers compatibly include another set of
  /// qualifiers from the narrow perspective of Objective-C ARC lifetime.
  ///
  /// One set of Objective-C lifetime qualifiers compatibly includes the other
  /// if the lifetime qualifiers match, or if both are non-__weak and the
  /// including set also contains the 'const' qualifier, or both are non-__weak
  /// and one is None (which can only happen in non-ARC modes).
  bool compatiblyIncludesObjCLifetime(Qualifiers other) const {
    if (getObjCLifetime() == other.getObjCLifetime())
      return true;

    if (getObjCLifetime() == OCL_Weak || other.getObjCLifetime() == OCL_Weak)
      return false;

    if (getObjCLifetime() == OCL_None || other.getObjCLifetime() == OCL_None)
      return true;

    return hasConst();
  }

  /// Determine whether this set of qualifiers is a strict superset of
  /// another set of qualifiers, not considering qualifier compatibility.
  bool isStrictSupersetOf(Qualifiers Other) const;

  bool operator==(Qualifiers Other) const { return Mask == Other.Mask; }
  bool operator!=(Qualifiers Other) const { return Mask != Other.Mask; }

  explicit operator bool() const { return hasQualifiers(); }

  Qualifiers &operator+=(Qualifiers R) {
    addQualifiers(R);
    return *this;
  }

  // Union two qualifier sets.  If an enumerated qualifier appears
  // in both sets, use the one from the right.
  friend Qualifiers operator+(Qualifiers L, Qualifiers R) {
    L += R;
    return L;
  }

  Qualifiers &operator-=(Qualifiers R) {
    removeQualifiers(R);
    return *this;
  }

  /// Compute the difference between two qualifier sets.
  friend Qualifiers operator-(Qualifiers L, Qualifiers R) {
    L -= R;
    return L;
  }

  std::string getAsString() const;
  std::string getAsString(const PrintingPolicy &Policy) const;

  static std::string getAddrSpaceAsString(LangAS AS);

  bool isEmptyWhenPrinted(const PrintingPolicy &Policy) const;
  void print(raw_ostream &OS, const PrintingPolicy &Policy,
             bool appendSpaceIfNonEmpty = false) const;

  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(Mask); }

private:
  // bits:     |0 1 2|3|4 .. 5|6  ..  8|9   ...   31|32 ... 63|
  //           |C R V|U|GCAttr|Lifetime|AddressSpace| PtrAuth |
  uint64_t Mask = 0;
  static_assert(sizeof(PointerAuthQualifier) == sizeof(uint32_t),
                "PointerAuthQualifier must be 32 bits");

  static constexpr uint64_t UMask = 0x8;
  static constexpr uint64_t UShift = 3;
  static constexpr uint64_t GCAttrMask = 0x30;
  static constexpr uint64_t GCAttrShift = 4;
  static constexpr uint64_t LifetimeMask = 0x1C0;
  static constexpr uint64_t LifetimeShift = 6;
  static constexpr uint64_t AddressSpaceMask =
      ~(CVRMask | UMask | GCAttrMask | LifetimeMask);
  static constexpr uint64_t AddressSpaceShift = 9;
  static constexpr uint64_t PtrAuthShift = 32;
  static constexpr uint64_t PtrAuthMask = uint64_t(0xffffffff) << PtrAuthShift;
};

class QualifiersAndAtomic {
  Qualifiers Quals;
  bool HasAtomic;

public:
  QualifiersAndAtomic() : HasAtomic(false) {}
  QualifiersAndAtomic(Qualifiers Quals, bool HasAtomic)
      : Quals(Quals), HasAtomic(HasAtomic) {}

  operator Qualifiers() const { return Quals; }

  bool hasVolatile() const { return Quals.hasVolatile(); }
  bool hasConst() const { return Quals.hasConst(); }
  bool hasRestrict() const { return Quals.hasRestrict(); }
  bool hasAtomic() const { return HasAtomic; }

  void addVolatile() { Quals.addVolatile(); }
  void addConst() { Quals.addConst(); }
  void addRestrict() { Quals.addRestrict(); }
  void addAtomic() { HasAtomic = true; }

  void removeVolatile() { Quals.removeVolatile(); }
  void removeConst() { Quals.removeConst(); }
  void removeRestrict() { Quals.removeRestrict(); }
  void removeAtomic() { HasAtomic = false; }

  QualifiersAndAtomic withVolatile() {
    return {Quals.withVolatile(), HasAtomic};
  }
  QualifiersAndAtomic withConst() { return {Quals.withConst(), HasAtomic}; }
  QualifiersAndAtomic withRestrict() {
    return {Quals.withRestrict(), HasAtomic};
  }
  QualifiersAndAtomic withAtomic() { return {Quals, true}; }

  QualifiersAndAtomic &operator+=(Qualifiers RHS) {
    Quals += RHS;
    return *this;
  }
};

/// A std::pair-like structure for storing a qualified type split
/// into its local qualifiers and its locally-unqualified type.
struct SplitQualType {
  /// The locally-unqualified type.
  const Type *Ty = nullptr;

  /// The local qualifiers.
  Qualifiers Quals;

  SplitQualType() = default;
  SplitQualType(const Type *ty, Qualifiers qs) : Ty(ty), Quals(qs) {}

  SplitQualType getSingleStepDesugaredType() const; // end of this file

  // Make std::tie work.
  std::pair<const Type *,Qualifiers> asPair() const {
    return std::pair<const Type *, Qualifiers>(Ty, Quals);
  }

  friend bool operator==(SplitQualType a, SplitQualType b) {
    return a.Ty == b.Ty && a.Quals == b.Quals;
  }
  friend bool operator!=(SplitQualType a, SplitQualType b) {
    return a.Ty != b.Ty || a.Quals != b.Quals;
  }
};

/// The kind of type we are substituting Objective-C type arguments into.
///
/// The kind of substitution affects the replacement of type parameters when
/// no concrete type information is provided, e.g., when dealing with an
/// unspecialized type.
enum class ObjCSubstitutionContext {
  /// An ordinary type.
  Ordinary,

  /// The result type of a method or function.
  Result,

  /// The parameter type of a method or function.
  Parameter,

  /// The type of a property.
  Property,

  /// The superclass of a type.
  Superclass,
};

/// The kind of 'typeof' expression we're after.
enum class TypeOfKind : uint8_t {
  Qualified,
  Unqualified,
};

/// A (possibly-)qualified type.
///
/// For efficiency, we don't store CV-qualified types as nodes on their
/// own: instead each reference to a type stores the qualifiers.  This
/// greatly reduces the number of nodes we need to allocate for types (for
/// example we only need one for 'int', 'const int', 'volatile int',
/// 'const volatile int', etc).
///
/// As an added efficiency bonus, instead of making this a pair, we
/// just store the two bits we care about in the low bits of the
/// pointer.  To handle the packing/unpacking, we make QualType be a
/// simple wrapper class that acts like a smart pointer.  A third bit
/// indicates whether there are extended qualifiers present, in which
/// case the pointer points to a special structure.
class QualType {
  friend class QualifierCollector;

  // Thankfully, these are efficiently composable.
  llvm::PointerIntPair<llvm::PointerUnion<const Type *, const ExtQuals *>,
                       Qualifiers::FastWidth> Value;

  const ExtQuals *getExtQualsUnsafe() const {
    return Value.getPointer().get<const ExtQuals*>();
  }

  const Type *getTypePtrUnsafe() const {
    return Value.getPointer().get<const Type*>();
  }

  const ExtQualsTypeCommonBase *getCommonPtr() const {
    assert(!isNull() && "Cannot retrieve a NULL type pointer");
    auto CommonPtrVal = reinterpret_cast<uintptr_t>(Value.getOpaqueValue());
    CommonPtrVal &= ~(uintptr_t)((1 << TypeAlignmentInBits) - 1);
    return reinterpret_cast<ExtQualsTypeCommonBase*>(CommonPtrVal);
  }

public:
  QualType() = default;
  QualType(const Type *Ptr, unsigned Quals) : Value(Ptr, Quals) {}
  QualType(const ExtQuals *Ptr, unsigned Quals) : Value(Ptr, Quals) {}

  unsigned getLocalFastQualifiers() const { return Value.getInt(); }
  void setLocalFastQualifiers(unsigned Quals) { Value.setInt(Quals); }

  bool UseExcessPrecision(const ASTContext &Ctx);

  /// Retrieves a pointer to the underlying (unqualified) type.
  ///
  /// This function requires that the type not be NULL. If the type might be
  /// NULL, use the (slightly less efficient) \c getTypePtrOrNull().
  const Type *getTypePtr() const;

  const Type *getTypePtrOrNull() const;

  /// Retrieves a pointer to the name of the base type.
  const IdentifierInfo *getBaseTypeIdentifier() const;

  /// Divides a QualType into its unqualified type and a set of local
  /// qualifiers.
  SplitQualType split() const;

  void *getAsOpaquePtr() const { return Value.getOpaqueValue(); }

  static QualType getFromOpaquePtr(const void *Ptr) {
    QualType T;
    T.Value.setFromOpaqueValue(const_cast<void*>(Ptr));
    return T;
  }

  const Type &operator*() const {
    return *getTypePtr();
  }

  const Type *operator->() const {
    return getTypePtr();
  }

  bool isCanonical() const;
  bool isCanonicalAsParam() const;

  /// Return true if this QualType doesn't point to a type yet.
  bool isNull() const {
    return Value.getPointer().isNull();
  }

  // Determines if a type can form `T&`.
  bool isReferenceable() const;

  /// Determine whether this particular QualType instance has the
  /// "const" qualifier set, without looking through typedefs that may have
  /// added "const" at a different level.
  bool isLocalConstQualified() const {
    return (getLocalFastQualifiers() & Qualifiers::Const);
  }

  /// Determine whether this type is const-qualified.
  bool isConstQualified() const;

  enum class NonConstantStorageReason {
    MutableField,
    NonConstNonReferenceType,
    NonTrivialCtor,
    NonTrivialDtor,
  };
  /// Determine whether instances of this type can be placed in immutable
  /// storage.
  /// If ExcludeCtor is true, the duration when the object's constructor runs
  /// will not be considered. The caller will need to verify that the object is
  /// not written to during its construction. ExcludeDtor works similarly.
  std::optional<NonConstantStorageReason>
  isNonConstantStorage(const ASTContext &Ctx, bool ExcludeCtor,
                       bool ExcludeDtor);

  bool isConstantStorage(const ASTContext &Ctx, bool ExcludeCtor,
                         bool ExcludeDtor) {
    return !isNonConstantStorage(Ctx, ExcludeCtor, ExcludeDtor);
  }

  /// Determine whether this particular QualType instance has the
  /// "restrict" qualifier set, without looking through typedefs that may have
  /// added "restrict" at a different level.
  bool isLocalRestrictQualified() const {
    return (getLocalFastQualifiers() & Qualifiers::Restrict);
  }

  /// Determine whether this type is restrict-qualified.
  bool isRestrictQualified() const;

  /// Determine whether this particular QualType instance has the
  /// "volatile" qualifier set, without looking through typedefs that may have
  /// added "volatile" at a different level.
  bool isLocalVolatileQualified() const {
    return (getLocalFastQualifiers() & Qualifiers::Volatile);
  }

  /// Determine whether this type is volatile-qualified.
  bool isVolatileQualified() const;

  /// Determine whether this particular QualType instance has any
  /// qualifiers, without looking through any typedefs that might add
  /// qualifiers at a different level.
  bool hasLocalQualifiers() const {
    return getLocalFastQualifiers() || hasLocalNonFastQualifiers();
  }

  /// Determine whether this type has any qualifiers.
  bool hasQualifiers() const;

  /// Determine whether this particular QualType instance has any
  /// "non-fast" qualifiers, e.g., those that are stored in an ExtQualType
  /// instance.
  bool hasLocalNonFastQualifiers() const {
    return Value.getPointer().is<const ExtQuals*>();
  }

  /// Retrieve the set of qualifiers local to this particular QualType
  /// instance, not including any qualifiers acquired through typedefs or
  /// other sugar.
  Qualifiers getLocalQualifiers() const;

  /// Retrieve the set of qualifiers applied to this type.
  Qualifiers getQualifiers() const;

  /// Retrieve the set of CVR (const-volatile-restrict) qualifiers
  /// local to this particular QualType instance, not including any qualifiers
  /// acquired through typedefs or other sugar.
  unsigned getLocalCVRQualifiers() const {
    return getLocalFastQualifiers();
  }

  /// Retrieve the set of CVR (const-volatile-restrict) qualifiers
  /// applied to this type.
  unsigned getCVRQualifiers() const;

  bool isConstant(const ASTContext& Ctx) const {
    return QualType::isConstant(*this, Ctx);
  }

  /// Determine whether this is a Plain Old Data (POD) type (C++ 3.9p10).
  bool isPODType(const ASTContext &Context) const;

  /// Return true if this is a POD type according to the rules of the C++98
  /// standard, regardless of the current compilation's language.
  bool isCXX98PODType(const ASTContext &Context) const;

  /// Return true if this is a POD type according to the more relaxed rules
  /// of the C++11 standard, regardless of the current compilation's language.
  /// (C++0x [basic.types]p9). Note that, unlike
  /// CXXRecordDecl::isCXX11StandardLayout, this takes DRs into account.
  bool isCXX11PODType(const ASTContext &Context) const;

  /// Return true if this is a trivial type per (C++0x [basic.types]p9)
  bool isTrivialType(const ASTContext &Context) const;

  /// Return true if this is a trivially copyable type (C++0x [basic.types]p9)
  bool isTriviallyCopyableType(const ASTContext &Context) const;

  /// Return true if the type is safe to bitwise copy using memcpy/memmove.
  ///
  /// This is an extension in clang: bitwise cloneable types act as trivially
  /// copyable types, meaning their underlying bytes can be safely copied by
  /// memcpy or memmove. After the copy, the destination object has the same
  /// object representation.
  ///
  /// However, there are cases where it is not safe to copy:
  ///  - When sanitizers, such as AddressSanitizer, add padding with poison,
  ///    which can cause issues if those poisoned padding bits are accessed.
  ///  - Types with Objective-C lifetimes, where specific runtime
  ///    semantics may not be preserved during a bitwise copy.
  bool isBitwiseCloneableType(const ASTContext &Context) const;

  /// Return true if this is a trivially copyable type
  bool isTriviallyCopyConstructibleType(const ASTContext &Context) const;

  /// Return true if this is a trivially relocatable type.
  bool isTriviallyRelocatableType(const ASTContext &Context) const;

  /// Returns true if it is a class and it might be dynamic.
  bool mayBeDynamicClass() const;

  /// Returns true if it is not a class or if the class might not be dynamic.
  bool mayBeNotDynamicClass() const;

  /// Returns true if it is a WebAssembly Reference Type.
  bool isWebAssemblyReferenceType() const;

  /// Returns true if it is a WebAssembly Externref Type.
  bool isWebAssemblyExternrefType() const;

  /// Returns true if it is a WebAssembly Funcref Type.
  bool isWebAssemblyFuncrefType() const;

  // Don't promise in the API that anything besides 'const' can be
  // easily added.

  /// Add the `const` type qualifier to this QualType.
  void addConst() {
    addFastQualifiers(Qualifiers::Const);
  }
  QualType withConst() const {
    return withFastQualifiers(Qualifiers::Const);
  }

  /// Add the `volatile` type qualifier to this QualType.
  void addVolatile() {
    addFastQualifiers(Qualifiers::Volatile);
  }
  QualType withVolatile() const {
    return withFastQualifiers(Qualifiers::Volatile);
  }

  /// Add the `restrict` qualifier to this QualType.
  void addRestrict() {
    addFastQualifiers(Qualifiers::Restrict);
  }
  QualType withRestrict() const {
    return withFastQualifiers(Qualifiers::Restrict);
  }

  QualType withCVRQualifiers(unsigned CVR) const {
    return withFastQualifiers(CVR);
  }

  void addFastQualifiers(unsigned TQs) {
    assert(!(TQs & ~Qualifiers::FastMask)
           && "non-fast qualifier bits set in mask!");
    Value.setInt(Value.getInt() | TQs);
  }

  void removeLocalConst();
  void removeLocalVolatile();
  void removeLocalRestrict();

  void removeLocalFastQualifiers() { Value.setInt(0); }
  void removeLocalFastQualifiers(unsigned Mask) {
    assert(!(Mask & ~Qualifiers::FastMask) && "mask has non-fast qualifiers");
    Value.setInt(Value.getInt() & ~Mask);
  }

  // Creates a type with the given qualifiers in addition to any
  // qualifiers already on this type.
  QualType withFastQualifiers(unsigned TQs) const {
    QualType T = *this;
    T.addFastQualifiers(TQs);
    return T;
  }

  // Creates a type with exactly the given fast qualifiers, removing
  // any existing fast qualifiers.
  QualType withExactLocalFastQualifiers(unsigned TQs) const {
    return withoutLocalFastQualifiers().withFastQualifiers(TQs);
  }

  // Removes fast qualifiers, but leaves any extended qualifiers in place.
  QualType withoutLocalFastQualifiers() const {
    QualType T = *this;
    T.removeLocalFastQualifiers();
    return T;
  }

  QualType getCanonicalType() const;

  /// Return this type with all of the instance-specific qualifiers
  /// removed, but without removing any qualifiers that may have been applied
  /// through typedefs.
  QualType getLocalUnqualifiedType() const { return QualType(getTypePtr(), 0); }

  /// Retrieve the unqualified variant of the given type,
  /// removing as little sugar as possible.
  ///
  /// This routine looks through various kinds of sugar to find the
  /// least-desugared type that is unqualified. For example, given:
  ///
  /// \code
  /// typedef int Integer;
  /// typedef const Integer CInteger;
  /// typedef CInteger DifferenceType;
  /// \endcode
  ///
  /// Executing \c getUnqualifiedType() on the type \c DifferenceType will
  /// desugar until we hit the type \c Integer, which has no qualifiers on it.
  ///
  /// The resulting type might still be qualified if it's sugar for an array
  /// type.  To strip qualifiers even from within a sugared array type, use
  /// ASTContext::getUnqualifiedArrayType.
  ///
  /// Note: In C, the _Atomic qualifier is special (see C23 6.2.5p32 for
  /// details), and it is not stripped by this function. Use
  /// getAtomicUnqualifiedType() to strip qualifiers including _Atomic.
  inline QualType getUnqualifiedType() const;

  /// Retrieve the unqualified variant of the given type, removing as little
  /// sugar as possible.
  ///
  /// Like getUnqualifiedType(), but also returns the set of
  /// qualifiers that were built up.
  ///
  /// The resulting type might still be qualified if it's sugar for an array
  /// type.  To strip qualifiers even from within a sugared array type, use
  /// ASTContext::getUnqualifiedArrayType.
  inline SplitQualType getSplitUnqualifiedType() const;

  /// Determine whether this type is more qualified than the other
  /// given type, requiring exact equality for non-CVR qualifiers.
  bool isMoreQualifiedThan(QualType Other) const;

  /// Determine whether this type is at least as qualified as the other
  /// given type, requiring exact equality for non-CVR qualifiers.
  bool isAtLeastAsQualifiedAs(QualType Other) const;

  QualType getNonReferenceType() const;

  /// Determine the type of a (typically non-lvalue) expression with the
  /// specified result type.
  ///
  /// This routine should be used for expressions for which the return type is
  /// explicitly specified (e.g., in a cast or call) and isn't necessarily
  /// an lvalue. It removes a top-level reference (since there are no
  /// expressions of reference type) and deletes top-level cvr-qualifiers
  /// from non-class types (in C++) or all types (in C).
  QualType getNonLValueExprType(const ASTContext &Context) const;

  /// Remove an outer pack expansion type (if any) from this type. Used as part
  /// of converting the type of a declaration to the type of an expression that
  /// references that expression. It's meaningless for an expression to have a
  /// pack expansion type.
  QualType getNonPackExpansionType() const;

  /// Return the specified type with any "sugar" removed from
  /// the type.  This takes off typedefs, typeof's etc.  If the outer level of
  /// the type is already concrete, it returns it unmodified.  This is similar
  /// to getting the canonical type, but it doesn't remove *all* typedefs.  For
  /// example, it returns "T*" as "T*", (not as "int*"), because the pointer is
  /// concrete.
  ///
  /// Qualifiers are left in place.
  QualType getDesugaredType(const ASTContext &Context) const {
    return getDesugaredType(*this, Context);
  }

  SplitQualType getSplitDesugaredType() const {
    return getSplitDesugaredType(*this);
  }

  /// Return the specified type with one level of "sugar" removed from
  /// the type.
  ///
  /// This routine takes off the first typedef, typeof, etc. If the outer level
  /// of the type is already concrete, it returns it unmodified.
  QualType getSingleStepDesugaredType(const ASTContext &Context) const {
    return getSingleStepDesugaredTypeImpl(*this, Context);
  }

  /// Returns the specified type after dropping any
  /// outer-level parentheses.
  QualType IgnoreParens() const {
    if (isa<ParenType>(*this))
      return QualType::IgnoreParens(*this);
    return *this;
  }

  /// Indicate whether the specified types and qualifiers are identical.
  friend bool operator==(const QualType &LHS, const QualType &RHS) {
    return LHS.Value == RHS.Value;
  }
  friend bool operator!=(const QualType &LHS, const QualType &RHS) {
    return LHS.Value != RHS.Value;
  }
  friend bool operator<(const QualType &LHS, const QualType &RHS) {
    return LHS.Value < RHS.Value;
  }

  static std::string getAsString(SplitQualType split,
                                 const PrintingPolicy &Policy) {
    return getAsString(split.Ty, split.Quals, Policy);
  }
  static std::string getAsString(const Type *ty, Qualifiers qs,
                                 const PrintingPolicy &Policy);

  std::string getAsString() const;
  std::string getAsString(const PrintingPolicy &Policy) const;

  void print(raw_ostream &OS, const PrintingPolicy &Policy,
             const Twine &PlaceHolder = Twine(),
             unsigned Indentation = 0) const;

  static void print(SplitQualType split, raw_ostream &OS,
                    const PrintingPolicy &policy, const Twine &PlaceHolder,
                    unsigned Indentation = 0) {
    return print(split.Ty, split.Quals, OS, policy, PlaceHolder, Indentation);
  }

  static void print(const Type *ty, Qualifiers qs,
                    raw_ostream &OS, const PrintingPolicy &policy,
                    const Twine &PlaceHolder,
                    unsigned Indentation = 0);

  void getAsStringInternal(std::string &Str,
                           const PrintingPolicy &Policy) const;

  static void getAsStringInternal(SplitQualType split, std::string &out,
                                  const PrintingPolicy &policy) {
    return getAsStringInternal(split.Ty, split.Quals, out, policy);
  }

  static void getAsStringInternal(const Type *ty, Qualifiers qs,
                                  std::string &out,
                                  const PrintingPolicy &policy);

  class StreamedQualTypeHelper {
    const QualType &T;
    const PrintingPolicy &Policy;
    const Twine &PlaceHolder;
    unsigned Indentation;

  public:
    StreamedQualTypeHelper(const QualType &T, const PrintingPolicy &Policy,
                           const Twine &PlaceHolder, unsigned Indentation)
        : T(T), Policy(Policy), PlaceHolder(PlaceHolder),
          Indentation(Indentation) {}

    friend raw_ostream &operator<<(raw_ostream &OS,
                                   const StreamedQualTypeHelper &SQT) {
      SQT.T.print(OS, SQT.Policy, SQT.PlaceHolder, SQT.Indentation);
      return OS;
    }
  };

  StreamedQualTypeHelper stream(const PrintingPolicy &Policy,
                                const Twine &PlaceHolder = Twine(),
                                unsigned Indentation = 0) const {
    return StreamedQualTypeHelper(*this, Policy, PlaceHolder, Indentation);
  }

  void dump(const char *s) const;
  void dump() const;
  void dump(llvm::raw_ostream &OS, const ASTContext &Context) const;

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(getAsOpaquePtr());
  }

  /// Check if this type has any address space qualifier.
  inline bool hasAddressSpace() const;

  /// Return the address space of this type.
  inline LangAS getAddressSpace() const;

  /// Returns true if address space qualifiers overlap with T address space
  /// qualifiers.
  /// OpenCL C defines conversion rules for pointers to different address spaces
  /// and notion of overlapping address spaces.
  /// CL1.1 or CL1.2:
  ///   address spaces overlap iff they are they same.
  /// OpenCL C v2.0 s6.5.5 adds:
  ///   __generic overlaps with any address space except for __constant.
  bool isAddressSpaceOverlapping(QualType T) const {
    Qualifiers Q = getQualifiers();
    Qualifiers TQ = T.getQualifiers();
    // Address spaces overlap if at least one of them is a superset of another
    return Q.isAddressSpaceSupersetOf(TQ) || TQ.isAddressSpaceSupersetOf(Q);
  }

  /// Returns gc attribute of this type.
  inline Qualifiers::GC getObjCGCAttr() const;

  /// true when Type is objc's weak.
  bool isObjCGCWeak() const {
    return getObjCGCAttr() == Qualifiers::Weak;
  }

  /// true when Type is objc's strong.
  bool isObjCGCStrong() const {
    return getObjCGCAttr() == Qualifiers::Strong;
  }

  /// Returns lifetime attribute of this type.
  Qualifiers::ObjCLifetime getObjCLifetime() const {
    return getQualifiers().getObjCLifetime();
  }

  bool hasNonTrivialObjCLifetime() const {
    return getQualifiers().hasNonTrivialObjCLifetime();
  }

  bool hasStrongOrWeakObjCLifetime() const {
    return getQualifiers().hasStrongOrWeakObjCLifetime();
  }

  // true when Type is objc's weak and weak is enabled but ARC isn't.
  bool isNonWeakInMRRWithObjCWeak(const ASTContext &Context) const;

  PointerAuthQualifier getPointerAuth() const {
    return getQualifiers().getPointerAuth();
  }

  enum PrimitiveDefaultInitializeKind {
    /// The type does not fall into any of the following categories. Note that
    /// this case is zero-valued so that values of this enum can be used as a
    /// boolean condition for non-triviality.
    PDIK_Trivial,

    /// The type is an Objective-C retainable pointer type that is qualified
    /// with the ARC __strong qualifier.
    PDIK_ARCStrong,

    /// The type is an Objective-C retainable pointer type that is qualified
    /// with the ARC __weak qualifier.
    PDIK_ARCWeak,

    /// The type is a struct containing a field whose type is not PCK_Trivial.
    PDIK_Struct
  };

  /// Functions to query basic properties of non-trivial C struct types.

  /// Check if this is a non-trivial type that would cause a C struct
  /// transitively containing this type to be non-trivial to default initialize
  /// and return the kind.
  PrimitiveDefaultInitializeKind
  isNonTrivialToPrimitiveDefaultInitialize() const;

  enum PrimitiveCopyKind {
    /// The type does not fall into any of the following categories. Note that
    /// this case is zero-valued so that values of this enum can be used as a
    /// boolean condition for non-triviality.
    PCK_Trivial,

    /// The type would be trivial except that it is volatile-qualified. Types
    /// that fall into one of the other non-trivial cases may additionally be
    /// volatile-qualified.
    PCK_VolatileTrivial,

    /// The type is an Objective-C retainable pointer type that is qualified
    /// with the ARC __strong qualifier.
    PCK_ARCStrong,

    /// The type is an Objective-C retainable pointer type that is qualified
    /// with the ARC __weak qualifier.
    PCK_ARCWeak,

    /// The type is a struct containing a field whose type is neither
    /// PCK_Trivial nor PCK_VolatileTrivial.
    /// Note that a C++ struct type does not necessarily match this; C++ copying
    /// semantics are too complex to express here, in part because they depend
    /// on the exact constructor or assignment operator that is chosen by
    /// overload resolution to do the copy.
    PCK_Struct
  };

  /// Check if this is a non-trivial type that would cause a C struct
  /// transitively containing this type to be non-trivial to copy and return the
  /// kind.
  PrimitiveCopyKind isNonTrivialToPrimitiveCopy() const;

  /// Check if this is a non-trivial type that would cause a C struct
  /// transitively containing this type to be non-trivial to destructively
  /// move and return the kind. Destructive move in this context is a C++-style
  /// move in which the source object is placed in a valid but unspecified state
  /// after it is moved, as opposed to a truly destructive move in which the
  /// source object is placed in an uninitialized state.
  PrimitiveCopyKind isNonTrivialToPrimitiveDestructiveMove() const;

  enum DestructionKind {
    DK_none,
    DK_cxx_destructor,
    DK_objc_strong_lifetime,
    DK_objc_weak_lifetime,
    DK_nontrivial_c_struct
  };

  /// Returns a nonzero value if objects of this type require
  /// non-trivial work to clean up after.  Non-zero because it's
  /// conceivable that qualifiers (objc_gc(weak)?) could make
  /// something require destruction.
  DestructionKind isDestructedType() const {
    return isDestructedTypeImpl(*this);
  }

  /// Check if this is or contains a C union that is non-trivial to
  /// default-initialize, which is a union that has a member that is non-trivial
  /// to default-initialize. If this returns true,
  /// isNonTrivialToPrimitiveDefaultInitialize returns PDIK_Struct.
  bool hasNonTrivialToPrimitiveDefaultInitializeCUnion() const;

  /// Check if this is or contains a C union that is non-trivial to destruct,
  /// which is a union that has a member that is non-trivial to destruct. If
  /// this returns true, isDestructedType returns DK_nontrivial_c_struct.
  bool hasNonTrivialToPrimitiveDestructCUnion() const;

  /// Check if this is or contains a C union that is non-trivial to copy, which
  /// is a union that has a member that is non-trivial to copy. If this returns
  /// true, isNonTrivialToPrimitiveCopy returns PCK_Struct.
  bool hasNonTrivialToPrimitiveCopyCUnion() const;

  /// Determine whether expressions of the given type are forbidden
  /// from being lvalues in C.
  ///
  /// The expression types that are forbidden to be lvalues are:
  ///   - 'void', but not qualified void
  ///   - function types
  ///
  /// The exact rule here is C99 6.3.2.1:
  ///   An lvalue is an expression with an object type or an incomplete
  ///   type other than void.
  bool isCForbiddenLValueType() const;

  /// Substitute type arguments for the Objective-C type parameters used in the
  /// subject type.
  ///
  /// \param ctx ASTContext in which the type exists.
  ///
  /// \param typeArgs The type arguments that will be substituted for the
  /// Objective-C type parameters in the subject type, which are generally
  /// computed via \c Type::getObjCSubstitutions. If empty, the type
  /// parameters will be replaced with their bounds or id/Class, as appropriate
  /// for the context.
  ///
  /// \param context The context in which the subject type was written.
  ///
  /// \returns the resulting type.
  QualType substObjCTypeArgs(ASTContext &ctx,
                             ArrayRef<QualType> typeArgs,
                             ObjCSubstitutionContext context) const;

  /// Substitute type arguments from an object type for the Objective-C type
  /// parameters used in the subject type.
  ///
  /// This operation combines the computation of type arguments for
  /// substitution (\c Type::getObjCSubstitutions) with the actual process of
  /// substitution (\c QualType::substObjCTypeArgs) for the convenience of
  /// callers that need to perform a single substitution in isolation.
  ///
  /// \param objectType The type of the object whose member type we're
  /// substituting into. For example, this might be the receiver of a message
  /// or the base of a property access.
  ///
  /// \param dc The declaration context from which the subject type was
  /// retrieved, which indicates (for example) which type parameters should
  /// be substituted.
  ///
  /// \param context The context in which the subject type was written.
  ///
  /// \returns the subject type after replacing all of the Objective-C type
  /// parameters with their corresponding arguments.
  QualType substObjCMemberType(QualType objectType,
                               const DeclContext *dc,
                               ObjCSubstitutionContext context) const;

  /// Strip Objective-C "__kindof" types from the given type.
  QualType stripObjCKindOfType(const ASTContext &ctx) const;

  /// Remove all qualifiers including _Atomic.
  ///
  /// Like getUnqualifiedType(), the type may still be qualified if it is a
  /// sugared array type.  To strip qualifiers even from within a sugared array
  /// type, use in conjunction with ASTContext::getUnqualifiedArrayType.
  QualType getAtomicUnqualifiedType() const;

private:
  // These methods are implemented in a separate translation unit;
  // "static"-ize them to avoid creating temporary QualTypes in the
  // caller.
  static bool isConstant(QualType T, const ASTContext& Ctx);
  static QualType getDesugaredType(QualType T, const ASTContext &Context);
  static SplitQualType getSplitDesugaredType(QualType T);
  static SplitQualType getSplitUnqualifiedTypeImpl(QualType type);
  static QualType getSingleStepDesugaredTypeImpl(QualType type,
                                                 const ASTContext &C);
  static QualType IgnoreParens(QualType T);
  static DestructionKind isDestructedTypeImpl(QualType type);

  /// Check if \param RD is or contains a non-trivial C union.
  static bool hasNonTrivialToPrimitiveDefaultInitializeCUnion(const RecordDecl *RD);
  static bool hasNonTrivialToPrimitiveDestructCUnion(const RecordDecl *RD);
  static bool hasNonTrivialToPrimitiveCopyCUnion(const RecordDecl *RD);
};

raw_ostream &operator<<(raw_ostream &OS, QualType QT);

} // namespace clang

namespace llvm {

/// Implement simplify_type for QualType, so that we can dyn_cast from QualType
/// to a specific Type class.
template<> struct simplify_type< ::clang::QualType> {
  using SimpleType = const ::clang::Type *;

  static SimpleType getSimplifiedValue(::clang::QualType Val) {
    return Val.getTypePtr();
  }
};

// Teach SmallPtrSet that QualType is "basically a pointer".
template<>
struct PointerLikeTypeTraits<clang::QualType> {
  static inline void *getAsVoidPointer(clang::QualType P) {
    return P.getAsOpaquePtr();
  }

  static inline clang::QualType getFromVoidPointer(void *P) {
    return clang::QualType::getFromOpaquePtr(P);
  }

  // Various qualifiers go in low bits.
  static constexpr int NumLowBitsAvailable = 0;
};

} // namespace llvm

namespace clang {

/// Base class that is common to both the \c ExtQuals and \c Type
/// classes, which allows \c QualType to access the common fields between the
/// two.
class ExtQualsTypeCommonBase {
  friend class ExtQuals;
  friend class QualType;
  friend class Type;

  /// The "base" type of an extended qualifiers type (\c ExtQuals) or
  /// a self-referential pointer (for \c Type).
  ///
  /// This pointer allows an efficient mapping from a QualType to its
  /// underlying type pointer.
  const Type *const BaseType;

  /// The canonical type of this type.  A QualType.
  QualType CanonicalType;

  ExtQualsTypeCommonBase(const Type *baseType, QualType canon)
      : BaseType(baseType), CanonicalType(canon) {}
};

/// We can encode up to four bits in the low bits of a
/// type pointer, but there are many more type qualifiers that we want
/// to be able to apply to an arbitrary type.  Therefore we have this
/// struct, intended to be heap-allocated and used by QualType to
/// store qualifiers.
///
/// The current design tags the 'const', 'restrict', and 'volatile' qualifiers
/// in three low bits on the QualType pointer; a fourth bit records whether
/// the pointer is an ExtQuals node. The extended qualifiers (address spaces,
/// Objective-C GC attributes) are much more rare.
class alignas(TypeAlignment) ExtQuals : public ExtQualsTypeCommonBase,
                                        public llvm::FoldingSetNode {
  // NOTE: changing the fast qualifiers should be straightforward as
  // long as you don't make 'const' non-fast.
  // 1. Qualifiers:
  //    a) Modify the bitmasks (Qualifiers::TQ and DeclSpec::TQ).
  //       Fast qualifiers must occupy the low-order bits.
  //    b) Update Qualifiers::FastWidth and FastMask.
  // 2. QualType:
  //    a) Update is{Volatile,Restrict}Qualified(), defined inline.
  //    b) Update remove{Volatile,Restrict}, defined near the end of
  //       this header.
  // 3. ASTContext:
  //    a) Update get{Volatile,Restrict}Type.

  /// The immutable set of qualifiers applied by this node. Always contains
  /// extended qualifiers.
  Qualifiers Quals;

  ExtQuals *this_() { return this; }

public:
  ExtQuals(const Type *baseType, QualType canon, Qualifiers quals)
      : ExtQualsTypeCommonBase(baseType,
                               canon.isNull() ? QualType(this_(), 0) : canon),
        Quals(quals) {
    assert(Quals.hasNonFastQualifiers()
           && "ExtQuals created with no fast qualifiers");
    assert(!Quals.hasFastQualifiers()
           && "ExtQuals created with fast qualifiers");
  }

  Qualifiers getQualifiers() const { return Quals; }

  bool hasObjCGCAttr() const { return Quals.hasObjCGCAttr(); }
  Qualifiers::GC getObjCGCAttr() const { return Quals.getObjCGCAttr(); }

  bool hasObjCLifetime() const { return Quals.hasObjCLifetime(); }
  Qualifiers::ObjCLifetime getObjCLifetime() const {
    return Quals.getObjCLifetime();
  }

  bool hasAddressSpace() const { return Quals.hasAddressSpace(); }
  LangAS getAddressSpace() const { return Quals.getAddressSpace(); }

  const Type *getBaseType() const { return BaseType; }

public:
  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, getBaseType(), Quals);
  }

  static void Profile(llvm::FoldingSetNodeID &ID,
                      const Type *BaseType,
                      Qualifiers Quals) {
    assert(!Quals.hasFastQualifiers() && "fast qualifiers in ExtQuals hash!");
    ID.AddPointer(BaseType);
    Quals.Profile(ID);
  }
};

/// The kind of C++11 ref-qualifier associated with a function type.
/// This determines whether a member function's "this" object can be an
/// lvalue, rvalue, or neither.
enum RefQualifierKind {
  /// No ref-qualifier was provided.
  RQ_None = 0,

  /// An lvalue ref-qualifier was provided (\c &).
  RQ_LValue,

  /// An rvalue ref-qualifier was provided (\c &&).
  RQ_RValue
};

/// Which keyword(s) were used to create an AutoType.
enum class AutoTypeKeyword {
  /// auto
  Auto,

  /// decltype(auto)
  DecltypeAuto,

  /// __auto_type (GNU extension)
  GNUAutoType
};

enum class ArraySizeModifier;
enum class ElaboratedTypeKeyword;
enum class VectorKind;

/// The base class of the type hierarchy.
///
/// A central concept with types is that each type always has a canonical
/// type.  A canonical type is the type with any typedef names stripped out
/// of it or the types it references.  For example, consider:
///
///  typedef int  foo;
///  typedef foo* bar;
///    'int *'    'foo *'    'bar'
///
/// There will be a Type object created for 'int'.  Since int is canonical, its
/// CanonicalType pointer points to itself.  There is also a Type for 'foo' (a
/// TypedefType).  Its CanonicalType pointer points to the 'int' Type.  Next
/// there is a PointerType that represents 'int*', which, like 'int', is
/// canonical.  Finally, there is a PointerType type for 'foo*' whose canonical
/// type is 'int*', and there is a TypedefType for 'bar', whose canonical type
/// is also 'int*'.
///
/// Non-canonical types are useful for emitting diagnostics, without losing
/// information about typedefs being used.  Canonical types are useful for type
/// comparisons (they allow by-pointer equality tests) and useful for reasoning
/// about whether something has a particular form (e.g. is a function type),
/// because they implicitly, recursively, strip all typedefs out of a type.
///
/// Types, once created, are immutable.
///
class alignas(TypeAlignment) Type : public ExtQualsTypeCommonBase {
public:
  enum TypeClass {
#define TYPE(Class, Base) Class,
#define LAST_TYPE(Class) TypeLast = Class
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.inc"
  };

private:
  /// Bitfields required by the Type class.
  class TypeBitfields {
    friend class Type;
    template <class T> friend class TypePropertyCache;

    /// TypeClass bitfield - Enum that specifies what subclass this belongs to.
    LLVM_PREFERRED_TYPE(TypeClass)
    unsigned TC : 8;

    /// Store information on the type dependency.
    LLVM_PREFERRED_TYPE(TypeDependence)
    unsigned Dependence : llvm::BitWidth<TypeDependence>;

    /// True if the cache (i.e. the bitfields here starting with
    /// 'Cache') is valid.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned CacheValid : 1;

    /// Linkage of this type.
    LLVM_PREFERRED_TYPE(Linkage)
    mutable unsigned CachedLinkage : 3;

    /// Whether this type involves and local or unnamed types.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned CachedLocalOrUnnamed : 1;

    /// Whether this type comes from an AST file.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned FromAST : 1;

    bool isCacheValid() const {
      return CacheValid;
    }

    Linkage getLinkage() const {
      assert(isCacheValid() && "getting linkage from invalid cache");
      return static_cast<Linkage>(CachedLinkage);
    }

    bool hasLocalOrUnnamedType() const {
      assert(isCacheValid() && "getting linkage from invalid cache");
      return CachedLocalOrUnnamed;
    }
  };
  enum { NumTypeBits = 8 + llvm::BitWidth<TypeDependence> + 6 };

protected:
  // These classes allow subclasses to somewhat cleanly pack bitfields
  // into Type.

  class ArrayTypeBitfields {
    friend class ArrayType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// CVR qualifiers from declarations like
    /// 'int X[static restrict 4]'. For function parameters only.
    LLVM_PREFERRED_TYPE(Qualifiers)
    unsigned IndexTypeQuals : 3;

    /// Storage class qualifiers from declarations like
    /// 'int X[static restrict 4]'. For function parameters only.
    LLVM_PREFERRED_TYPE(ArraySizeModifier)
    unsigned SizeModifier : 3;
  };
  enum { NumArrayTypeBits = NumTypeBits + 6 };

  class ConstantArrayTypeBitfields {
    friend class ConstantArrayType;

    LLVM_PREFERRED_TYPE(ArrayTypeBitfields)
    unsigned : NumArrayTypeBits;

    /// Whether we have a stored size expression.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasExternalSize : 1;

    LLVM_PREFERRED_TYPE(unsigned)
    unsigned SizeWidth : 5;
  };

  class BuiltinTypeBitfields {
    friend class BuiltinType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// The kind (BuiltinType::Kind) of builtin type this is.
    static constexpr unsigned NumOfBuiltinTypeBits = 9;
    unsigned Kind : NumOfBuiltinTypeBits;
  };

  /// FunctionTypeBitfields store various bits belonging to FunctionProtoType.
  /// Only common bits are stored here. Additional uncommon bits are stored
  /// in a trailing object after FunctionProtoType.
  class FunctionTypeBitfields {
    friend class FunctionProtoType;
    friend class FunctionType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// Extra information which affects how the function is called, like
    /// regparm and the calling convention.
    LLVM_PREFERRED_TYPE(CallingConv)
    unsigned ExtInfo : 13;

    /// The ref-qualifier associated with a \c FunctionProtoType.
    ///
    /// This is a value of type \c RefQualifierKind.
    LLVM_PREFERRED_TYPE(RefQualifierKind)
    unsigned RefQualifier : 2;

    /// Used only by FunctionProtoType, put here to pack with the
    /// other bitfields.
    /// The qualifiers are part of FunctionProtoType because...
    ///
    /// C++ 8.3.5p4: The return type, the parameter type list and the
    /// cv-qualifier-seq, [...], are part of the function type.
    LLVM_PREFERRED_TYPE(Qualifiers)
    unsigned FastTypeQuals : Qualifiers::FastWidth;
    /// Whether this function has extended Qualifiers.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasExtQuals : 1;

    /// The number of parameters this function has, not counting '...'.
    /// According to [implimits] 8 bits should be enough here but this is
    /// somewhat easy to exceed with metaprogramming and so we would like to
    /// keep NumParams as wide as reasonably possible.
    unsigned NumParams : 16;

    /// The type of exception specification this function has.
    LLVM_PREFERRED_TYPE(ExceptionSpecificationType)
    unsigned ExceptionSpecType : 4;

    /// Whether this function has extended parameter information.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasExtParameterInfos : 1;

    /// Whether this function has extra bitfields for the prototype.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasExtraBitfields : 1;

    /// Whether the function is variadic.
    LLVM_PREFERRED_TYPE(bool)
    unsigned Variadic : 1;

    /// Whether this function has a trailing return type.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasTrailingReturn : 1;
  };

  class ObjCObjectTypeBitfields {
    friend class ObjCObjectType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// The number of type arguments stored directly on this object type.
    unsigned NumTypeArgs : 7;

    /// The number of protocols stored directly on this object type.
    unsigned NumProtocols : 6;

    /// Whether this is a "kindof" type.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsKindOf : 1;
  };

  class ReferenceTypeBitfields {
    friend class ReferenceType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// True if the type was originally spelled with an lvalue sigil.
    /// This is never true of rvalue references but can also be false
    /// on lvalue references because of C++0x [dcl.typedef]p9,
    /// as follows:
    ///
    ///   typedef int &ref;    // lvalue, spelled lvalue
    ///   typedef int &&rvref; // rvalue
    ///   ref &a;              // lvalue, inner ref, spelled lvalue
    ///   ref &&a;             // lvalue, inner ref
    ///   rvref &a;            // lvalue, inner ref, spelled lvalue
    ///   rvref &&a;           // rvalue, inner ref
    LLVM_PREFERRED_TYPE(bool)
    unsigned SpelledAsLValue : 1;

    /// True if the inner type is a reference type.  This only happens
    /// in non-canonical forms.
    LLVM_PREFERRED_TYPE(bool)
    unsigned InnerRef : 1;
  };

  class TypeWithKeywordBitfields {
    friend class TypeWithKeyword;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// An ElaboratedTypeKeyword.  8 bits for efficient access.
    LLVM_PREFERRED_TYPE(ElaboratedTypeKeyword)
    unsigned Keyword : 8;
  };

  enum { NumTypeWithKeywordBits = NumTypeBits + 8 };

  class ElaboratedTypeBitfields {
    friend class ElaboratedType;

    LLVM_PREFERRED_TYPE(TypeWithKeywordBitfields)
    unsigned : NumTypeWithKeywordBits;

    /// Whether the ElaboratedType has a trailing OwnedTagDecl.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasOwnedTagDecl : 1;
  };

  class VectorTypeBitfields {
    friend class VectorType;
    friend class DependentVectorType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// The kind of vector, either a generic vector type or some
    /// target-specific vector type such as for AltiVec or Neon.
    LLVM_PREFERRED_TYPE(VectorKind)
    unsigned VecKind : 4;
    /// The number of elements in the vector.
    uint32_t NumElements;
  };

  class AttributedTypeBitfields {
    friend class AttributedType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    LLVM_PREFERRED_TYPE(attr::Kind)
    unsigned AttrKind : 32 - NumTypeBits;
  };

  class AutoTypeBitfields {
    friend class AutoType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// Was this placeholder type spelled as 'auto', 'decltype(auto)',
    /// or '__auto_type'?  AutoTypeKeyword value.
    LLVM_PREFERRED_TYPE(AutoTypeKeyword)
    unsigned Keyword : 2;

    /// The number of template arguments in the type-constraints, which is
    /// expected to be able to hold at least 1024 according to [implimits].
    /// However as this limit is somewhat easy to hit with template
    /// metaprogramming we'd prefer to keep it as large as possible.
    /// At the moment it has been left as a non-bitfield since this type
    /// safely fits in 64 bits as an unsigned, so there is no reason to
    /// introduce the performance impact of a bitfield.
    unsigned NumArgs;
  };

  class TypeOfBitfields {
    friend class TypeOfType;
    friend class TypeOfExprType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;
    LLVM_PREFERRED_TYPE(TypeOfKind)
    unsigned Kind : 1;
  };

  class UsingBitfields {
    friend class UsingType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// True if the underlying type is different from the declared one.
    LLVM_PREFERRED_TYPE(bool)
    unsigned hasTypeDifferentFromDecl : 1;
  };

  class TypedefBitfields {
    friend class TypedefType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// True if the underlying type is different from the declared one.
    LLVM_PREFERRED_TYPE(bool)
    unsigned hasTypeDifferentFromDecl : 1;
  };

  class SubstTemplateTypeParmTypeBitfields {
    friend class SubstTemplateTypeParmType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    LLVM_PREFERRED_TYPE(bool)
    unsigned HasNonCanonicalUnderlyingType : 1;

    // The index of the template parameter this substitution represents.
    unsigned Index : 15;

    /// Represents the index within a pack if this represents a substitution
    /// from a pack expansion. This index starts at the end of the pack and
    /// increments towards the beginning.
    /// Positive non-zero number represents the index + 1.
    /// Zero means this is not substituted from an expansion.
    unsigned PackIndex : 16;
  };

  class SubstTemplateTypeParmPackTypeBitfields {
    friend class SubstTemplateTypeParmPackType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    // The index of the template parameter this substitution represents.
    unsigned Index : 16;

    /// The number of template arguments in \c Arguments, which is
    /// expected to be able to hold at least 1024 according to [implimits].
    /// However as this limit is somewhat easy to hit with template
    /// metaprogramming we'd prefer to keep it as large as possible.
    unsigned NumArgs : 16;
  };

  class TemplateSpecializationTypeBitfields {
    friend class TemplateSpecializationType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// Whether this template specialization type is a substituted type alias.
    LLVM_PREFERRED_TYPE(bool)
    unsigned TypeAlias : 1;

    /// The number of template arguments named in this class template
    /// specialization, which is expected to be able to hold at least 1024
    /// according to [implimits]. However, as this limit is somewhat easy to
    /// hit with template metaprogramming we'd prefer to keep it as large
    /// as possible. At the moment it has been left as a non-bitfield since
    /// this type safely fits in 64 bits as an unsigned, so there is no reason
    /// to introduce the performance impact of a bitfield.
    unsigned NumArgs;
  };

  class DependentTemplateSpecializationTypeBitfields {
    friend class DependentTemplateSpecializationType;

    LLVM_PREFERRED_TYPE(TypeWithKeywordBitfields)
    unsigned : NumTypeWithKeywordBits;

    /// The number of template arguments named in this class template
    /// specialization, which is expected to be able to hold at least 1024
    /// according to [implimits]. However, as this limit is somewhat easy to
    /// hit with template metaprogramming we'd prefer to keep it as large
    /// as possible. At the moment it has been left as a non-bitfield since
    /// this type safely fits in 64 bits as an unsigned, so there is no reason
    /// to introduce the performance impact of a bitfield.
    unsigned NumArgs;
  };

  class PackExpansionTypeBitfields {
    friend class PackExpansionType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// The number of expansions that this pack expansion will
    /// generate when substituted (+1), which is expected to be able to
    /// hold at least 1024 according to [implimits]. However, as this limit
    /// is somewhat easy to hit with template metaprogramming we'd prefer to
    /// keep it as large as possible. At the moment it has been left as a
    /// non-bitfield since this type safely fits in 64 bits as an unsigned, so
    /// there is no reason to introduce the performance impact of a bitfield.
    ///
    /// This field will only have a non-zero value when some of the parameter
    /// packs that occur within the pattern have been substituted but others
    /// have not.
    unsigned NumExpansions;
  };

  class CountAttributedTypeBitfields {
    friend class CountAttributedType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    static constexpr unsigned NumCoupledDeclsBits = 4;
    unsigned NumCoupledDecls : NumCoupledDeclsBits;
    LLVM_PREFERRED_TYPE(bool)
    unsigned CountInBytes : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned OrNull : 1;
  };
  static_assert(sizeof(CountAttributedTypeBitfields) <= sizeof(unsigned));

  union {
    TypeBitfields TypeBits;
    ArrayTypeBitfields ArrayTypeBits;
    ConstantArrayTypeBitfields ConstantArrayTypeBits;
    AttributedTypeBitfields AttributedTypeBits;
    AutoTypeBitfields AutoTypeBits;
    TypeOfBitfields TypeOfBits;
    TypedefBitfields TypedefBits;
    UsingBitfields UsingBits;
    BuiltinTypeBitfields BuiltinTypeBits;
    FunctionTypeBitfields FunctionTypeBits;
    ObjCObjectTypeBitfields ObjCObjectTypeBits;
    ReferenceTypeBitfields ReferenceTypeBits;
    TypeWithKeywordBitfields TypeWithKeywordBits;
    ElaboratedTypeBitfields ElaboratedTypeBits;
    VectorTypeBitfields VectorTypeBits;
    SubstTemplateTypeParmTypeBitfields SubstTemplateTypeParmTypeBits;
    SubstTemplateTypeParmPackTypeBitfields SubstTemplateTypeParmPackTypeBits;
    TemplateSpecializationTypeBitfields TemplateSpecializationTypeBits;
    DependentTemplateSpecializationTypeBitfields
      DependentTemplateSpecializationTypeBits;
    PackExpansionTypeBitfields PackExpansionTypeBits;
    CountAttributedTypeBitfields CountAttributedTypeBits;
  };

private:
  template <class T> friend class TypePropertyCache;

  /// Set whether this type comes from an AST file.
  void setFromAST(bool V = true) const {
    TypeBits.FromAST = V;
  }

protected:
  friend class ASTContext;

  Type(TypeClass tc, QualType canon, TypeDependence Dependence)
      : ExtQualsTypeCommonBase(this,
                               canon.isNull() ? QualType(this_(), 0) : canon) {
    static_assert(sizeof(*this) <=
                      alignof(decltype(*this)) + sizeof(ExtQualsTypeCommonBase),
                  "changing bitfields changed sizeof(Type)!");
    static_assert(alignof(decltype(*this)) % TypeAlignment == 0,
                  "Insufficient alignment!");
    TypeBits.TC = tc;
    TypeBits.Dependence = static_cast<unsigned>(Dependence);
    TypeBits.CacheValid = false;
    TypeBits.CachedLocalOrUnnamed = false;
    TypeBits.CachedLinkage = llvm::to_underlying(Linkage::Invalid);
    TypeBits.FromAST = false;
  }

  // silence VC++ warning C4355: 'this' : used in base member initializer list
  Type *this_() { return this; }

  void setDependence(TypeDependence D) {
    TypeBits.Dependence = static_cast<unsigned>(D);
  }

  void addDependence(TypeDependence D) { setDependence(getDependence() | D); }

public:
  friend class ASTReader;
  friend class ASTWriter;
  template <class T> friend class serialization::AbstractTypeReader;
  template <class T> friend class serialization::AbstractTypeWriter;

  Type(const Type &) = delete;
  Type(Type &&) = delete;
  Type &operator=(const Type &) = delete;
  Type &operator=(Type &&) = delete;

  TypeClass getTypeClass() const { return static_cast<TypeClass>(TypeBits.TC); }

  /// Whether this type comes from an AST file.
  bool isFromAST() const { return TypeBits.FromAST; }

  /// Whether this type is or contains an unexpanded parameter
  /// pack, used to support C++0x variadic templates.
  ///
  /// A type that contains a parameter pack shall be expanded by the
  /// ellipsis operator at some point. For example, the typedef in the
  /// following example contains an unexpanded parameter pack 'T':
  ///
  /// \code
  /// template<typename ...T>
  /// struct X {
  ///   typedef T* pointer_types; // ill-formed; T is a parameter pack.
  /// };
  /// \endcode
  ///
  /// Note that this routine does not specify which
  bool containsUnexpandedParameterPack() const {
    return getDependence() & TypeDependence::UnexpandedPack;
  }

  /// Determines if this type would be canonical if it had no further
  /// qualification.
  bool isCanonicalUnqualified() const {
    return CanonicalType == QualType(this, 0);
  }

  /// Pull a single level of sugar off of this locally-unqualified type.
  /// Users should generally prefer SplitQualType::getSingleStepDesugaredType()
  /// or QualType::getSingleStepDesugaredType(const ASTContext&).
  QualType getLocallyUnqualifiedSingleStepDesugaredType() const;

  /// As an extension, we classify types as one of "sized" or "sizeless";
  /// every type is one or the other.  Standard types are all sized;
  /// sizeless types are purely an extension.
  ///
  /// Sizeless types contain data with no specified size, alignment,
  /// or layout.
  bool isSizelessType() const;
  bool isSizelessBuiltinType() const;

  /// Returns true for all scalable vector types.
  bool isSizelessVectorType() const;

  /// Returns true for SVE scalable vector types.
  bool isSVESizelessBuiltinType() const;

  /// Returns true for RVV scalable vector types.
  bool isRVVSizelessBuiltinType() const;

  /// Check if this is a WebAssembly Externref Type.
  bool isWebAssemblyExternrefType() const;

  /// Returns true if this is a WebAssembly table type: either an array of
  /// reference types, or a pointer to a reference type (which can only be
  /// created by array to pointer decay).
  bool isWebAssemblyTableType() const;

  /// Determines if this is a sizeless type supported by the
  /// 'arm_sve_vector_bits' type attribute, which can be applied to a single
  /// SVE vector or predicate, excluding tuple types such as svint32x4_t.
  bool isSveVLSBuiltinType() const;

  /// Returns the representative type for the element of an SVE builtin type.
  /// This is used to represent fixed-length SVE vectors created with the
  /// 'arm_sve_vector_bits' type attribute as VectorType.
  QualType getSveEltType(const ASTContext &Ctx) const;

  /// Determines if this is a sizeless type supported by the
  /// 'riscv_rvv_vector_bits' type attribute, which can be applied to a single
  /// RVV vector or mask.
  bool isRVVVLSBuiltinType() const;

  /// Returns the representative type for the element of an RVV builtin type.
  /// This is used to represent fixed-length RVV vectors created with the
  /// 'riscv_rvv_vector_bits' type attribute as VectorType.
  QualType getRVVEltType(const ASTContext &Ctx) const;

  /// Returns the representative type for the element of a sizeless vector
  /// builtin type.
  QualType getSizelessVectorEltType(const ASTContext &Ctx) const;

  /// Types are partitioned into 3 broad categories (C99 6.2.5p1):
  /// object types, function types, and incomplete types.

  /// Return true if this is an incomplete type.
  /// A type that can describe objects, but which lacks information needed to
  /// determine its size (e.g. void, or a fwd declared struct). Clients of this
  /// routine will need to determine if the size is actually required.
  ///
  /// Def If non-null, and the type refers to some kind of declaration
  /// that can be completed (such as a C struct, C++ class, or Objective-C
  /// class), will be set to the declaration.
  bool isIncompleteType(NamedDecl **Def = nullptr) const;

  /// Return true if this is an incomplete or object
  /// type, in other words, not a function type.
  bool isIncompleteOrObjectType() const {
    return !isFunctionType();
  }

  /// Determine whether this type is an object type.
  bool isObjectType() const {
    // C++ [basic.types]p8:
    //   An object type is a (possibly cv-qualified) type that is not a
    //   function type, not a reference type, and not a void type.
    return !isReferenceType() && !isFunctionType() && !isVoidType();
  }

  /// Return true if this is a literal type
  /// (C++11 [basic.types]p10)
  bool isLiteralType(const ASTContext &Ctx) const;

  /// Determine if this type is a structural type, per C++20 [temp.param]p7.
  bool isStructuralType() const;

  /// Test if this type is a standard-layout type.
  /// (C++0x [basic.type]p9)
  bool isStandardLayoutType() const;

  /// Helper methods to distinguish type categories. All type predicates
  /// operate on the canonical type, ignoring typedefs and qualifiers.

  /// Returns true if the type is a builtin type.
  bool isBuiltinType() const;

  /// Test for a particular builtin type.
  bool isSpecificBuiltinType(unsigned K) const;

  /// Test for a type which does not represent an actual type-system type but
  /// is instead used as a placeholder for various convenient purposes within
  /// Clang.  All such types are BuiltinTypes.
  bool isPlaceholderType() const;
  const BuiltinType *getAsPlaceholderType() const;

  /// Test for a specific placeholder type.
  bool isSpecificPlaceholderType(unsigned K) const;

  /// Test for a placeholder type other than Overload; see
  /// BuiltinType::isNonOverloadPlaceholderType.
  bool isNonOverloadPlaceholderType() const;

  /// isIntegerType() does *not* include complex integers (a GCC extension).
  /// isComplexIntegerType() can be used to test for complex integers.
  bool isIntegerType() const;     // C99 6.2.5p17 (int, char, bool, enum)
  bool isEnumeralType() const;

  /// Determine whether this type is a scoped enumeration type.
  bool isScopedEnumeralType() const;
  bool isBooleanType() const;
  bool isCharType() const;
  bool isWideCharType() const;
  bool isChar8Type() const;
  bool isChar16Type() const;
  bool isChar32Type() const;
  bool isAnyCharacterType() const;
  bool isIntegralType(const ASTContext &Ctx) const;

  /// Determine whether this type is an integral or enumeration type.
  bool isIntegralOrEnumerationType() const;

  /// Determine whether this type is an integral or unscoped enumeration type.
  bool isIntegralOrUnscopedEnumerationType() const;
  bool isUnscopedEnumerationType() const;

  /// Floating point categories.
  bool isRealFloatingType() const; // C99 6.2.5p10 (float, double, long double)
  /// isComplexType() does *not* include complex integers (a GCC extension).
  /// isComplexIntegerType() can be used to test for complex integers.
  bool isComplexType() const;      // C99 6.2.5p11 (complex)
  bool isAnyComplexType() const;   // C99 6.2.5p11 (complex) + Complex Int.
  bool isFloatingType() const;     // C99 6.2.5p11 (real floating + complex)
  bool isHalfType() const;         // OpenCL 6.1.1.1, NEON (IEEE 754-2008 half)
  bool isFloat16Type() const;      // C11 extension ISO/IEC TS 18661
  bool isFloat32Type() const;
  bool isDoubleType() const;
  bool isBFloat16Type() const;
  bool isFloat128Type() const;
  bool isIbm128Type() const;
  bool isRealType() const;         // C99 6.2.5p17 (real floating + integer)
  bool isArithmeticType() const;   // C99 6.2.5p18 (integer + floating)
  bool isVoidType() const;         // C99 6.2.5p19
  bool isScalarType() const;       // C99 6.2.5p21 (arithmetic + pointers)
  bool isAggregateType() const;
  bool isFundamentalType() const;
  bool isCompoundType() const;

  // Type Predicates: Check to see if this type is structurally the specified
  // type, ignoring typedefs and qualifiers.
  bool isFunctionType() const;
  bool isFunctionNoProtoType() const { return getAs<FunctionNoProtoType>(); }
  bool isFunctionProtoType() const { return getAs<FunctionProtoType>(); }
  bool isPointerType() const;
  bool isSignableType() const;
  bool isAnyPointerType() const;   // Any C pointer or ObjC object pointer
  bool isCountAttributedType() const;
  bool isBlockPointerType() const;
  bool isVoidPointerType() const;
  bool isReferenceType() const;
  bool isLValueReferenceType() const;
  bool isRValueReferenceType() const;
  bool isObjectPointerType() const;
  bool isFunctionPointerType() const;
  bool isFunctionReferenceType() const;
  bool isMemberPointerType() const;
  bool isMemberFunctionPointerType() const;
  bool isMemberDataPointerType() const;
  bool isArrayType() const;
  bool isConstantArrayType() const;
  bool isIncompleteArrayType() const;
  bool isVariableArrayType() const;
  bool isArrayParameterType() const;
  bool isDependentSizedArrayType() const;
  bool isRecordType() const;
  bool isClassType() const;
  bool isStructureType() const;
  bool isStructureTypeWithFlexibleArrayMember() const;
  bool isObjCBoxableRecordType() const;
  bool isInterfaceType() const;
  bool isStructureOrClassType() const;
  bool isUnionType() const;
  bool isComplexIntegerType() const;            // GCC _Complex integer type.
  bool isVectorType() const;                    // GCC vector type.
  bool isExtVectorType() const;                 // Extended vector type.
  bool isExtVectorBoolType() const;             // Extended vector type with bool element.
  bool isSubscriptableVectorType() const;
  bool isMatrixType() const;                    // Matrix type.
  bool isConstantMatrixType() const;            // Constant matrix type.
  bool isDependentAddressSpaceType() const;     // value-dependent address space qualifier
  bool isObjCObjectPointerType() const;         // pointer to ObjC object
  bool isObjCRetainableType() const;            // ObjC object or block pointer
  bool isObjCLifetimeType() const;              // (array of)* retainable type
  bool isObjCIndirectLifetimeType() const;      // (pointer to)* lifetime type
  bool isObjCNSObjectType() const;              // __attribute__((NSObject))
  bool isObjCIndependentClassType() const;      // __attribute__((objc_independent_class))
  // FIXME: change this to 'raw' interface type, so we can used 'interface' type
  // for the common case.
  bool isObjCObjectType() const;                // NSString or typeof(*(id)0)
  bool isObjCQualifiedInterfaceType() const;    // NSString<foo>
  bool isObjCQualifiedIdType() const;           // id<foo>
  bool isObjCQualifiedClassType() const;        // Class<foo>
  bool isObjCObjectOrInterfaceType() const;
  bool isObjCIdType() const;                    // id
  bool isDecltypeType() const;
  /// Was this type written with the special inert-in-ARC __unsafe_unretained
  /// qualifier?
  ///
  /// This approximates the answer to the following question: if this
  /// translation unit were compiled in ARC, would this type be qualified
  /// with __unsafe_unretained?
  bool isObjCInertUnsafeUnretainedType() const {
    return hasAttr(attr::ObjCInertUnsafeUnretained);
  }

  /// Whether the type is Objective-C 'id' or a __kindof type of an
  /// object type, e.g., __kindof NSView * or __kindof id
  /// <NSCopying>.
  ///
  /// \param bound Will be set to the bound on non-id subtype types,
  /// which will be (possibly specialized) Objective-C class type, or
  /// null for 'id.
  bool isObjCIdOrObjectKindOfType(const ASTContext &ctx,
                                  const ObjCObjectType *&bound) const;

  bool isObjCClassType() const;                 // Class

  /// Whether the type is Objective-C 'Class' or a __kindof type of an
  /// Class type, e.g., __kindof Class <NSCopying>.
  ///
  /// Unlike \c isObjCIdOrObjectKindOfType, there is no relevant bound
  /// here because Objective-C's type system cannot express "a class
  /// object for a subclass of NSFoo".
  bool isObjCClassOrClassKindOfType() const;

  bool isBlockCompatibleObjCPointerType(ASTContext &ctx) const;
  bool isObjCSelType() const;                 // Class
  bool isObjCBuiltinType() const;               // 'id' or 'Class'
  bool isObjCARCBridgableType() const;
  bool isCARCBridgableType() const;
  bool isTemplateTypeParmType() const;          // C++ template type parameter
  bool isNullPtrType() const;                   // C++11 std::nullptr_t or
                                                // C23   nullptr_t
  bool isNothrowT() const;                      // C++   std::nothrow_t
  bool isAlignValT() const;                     // C++17 std::align_val_t
  bool isStdByteType() const;                   // C++17 std::byte
  bool isAtomicType() const;                    // C11 _Atomic()
  bool isUndeducedAutoType() const;             // C++11 auto or
                                                // C++14 decltype(auto)
  bool isTypedefNameType() const;               // typedef or alias template

#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  bool is##Id##Type() const;
#include "clang/Basic/OpenCLImageTypes.def"

  bool isImageType() const;                     // Any OpenCL image type

  bool isSamplerT() const;                      // OpenCL sampler_t
  bool isEventT() const;                        // OpenCL event_t
  bool isClkEventT() const;                     // OpenCL clk_event_t
  bool isQueueT() const;                        // OpenCL queue_t
  bool isReserveIDT() const;                    // OpenCL reserve_id_t

#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  bool is##Id##Type() const;
#include "clang/Basic/OpenCLExtensionTypes.def"
  // Type defined in cl_intel_device_side_avc_motion_estimation OpenCL extension
  bool isOCLIntelSubgroupAVCType() const;
  bool isOCLExtOpaqueType() const;              // Any OpenCL extension type

  bool isPipeType() const;                      // OpenCL pipe type
  bool isBitIntType() const;                    // Bit-precise integer type
  bool isOpenCLSpecificType() const;            // Any OpenCL specific type

  /// Determines if this type, which must satisfy
  /// isObjCLifetimeType(), is implicitly __unsafe_unretained rather
  /// than implicitly __strong.
  bool isObjCARCImplicitlyUnretainedType() const;

  /// Check if the type is the CUDA device builtin surface type.
  bool isCUDADeviceBuiltinSurfaceType() const;
  /// Check if the type is the CUDA device builtin texture type.
  bool isCUDADeviceBuiltinTextureType() const;

  /// Return the implicit lifetime for this type, which must not be dependent.
  Qualifiers::ObjCLifetime getObjCARCImplicitLifetime() const;

  enum ScalarTypeKind {
    STK_CPointer,
    STK_BlockPointer,
    STK_ObjCObjectPointer,
    STK_MemberPointer,
    STK_Bool,
    STK_Integral,
    STK_Floating,
    STK_IntegralComplex,
    STK_FloatingComplex,
    STK_FixedPoint
  };

  /// Given that this is a scalar type, classify it.
  ScalarTypeKind getScalarTypeKind() const;

  TypeDependence getDependence() const {
    return static_cast<TypeDependence>(TypeBits.Dependence);
  }

  /// Whether this type is an error type.
  bool containsErrors() const {
    return getDependence() & TypeDependence::Error;
  }

  /// Whether this type is a dependent type, meaning that its definition
  /// somehow depends on a template parameter (C++ [temp.dep.type]).
  bool isDependentType() const {
    return getDependence() & TypeDependence::Dependent;
  }

  /// Determine whether this type is an instantiation-dependent type,
  /// meaning that the type involves a template parameter (even if the
  /// definition does not actually depend on the type substituted for that
  /// template parameter).
  bool isInstantiationDependentType() const {
    return getDependence() & TypeDependence::Instantiation;
  }

  /// Determine whether this type is an undeduced type, meaning that
  /// it somehow involves a C++11 'auto' type or similar which has not yet been
  /// deduced.
  bool isUndeducedType() const;

  /// Whether this type is a variably-modified type (C99 6.7.5).
  bool isVariablyModifiedType() const {
    return getDependence() & TypeDependence::VariablyModified;
  }

  /// Whether this type involves a variable-length array type
  /// with a definite size.
  bool hasSizedVLAType() const;

  /// Whether this type is or contains a local or unnamed type.
  bool hasUnnamedOrLocalType() const;

  bool isOverloadableType() const;

  /// Determine wither this type is a C++ elaborated-type-specifier.
  bool isElaboratedTypeSpecifier() const;

  bool canDecayToPointerType() const;

  /// Whether this type is represented natively as a pointer.  This includes
  /// pointers, references, block pointers, and Objective-C interface,
  /// qualified id, and qualified interface types, as well as nullptr_t.
  bool hasPointerRepresentation() const;

  /// Whether this type can represent an objective pointer type for the
  /// purpose of GC'ability
  bool hasObjCPointerRepresentation() const;

  /// Determine whether this type has an integer representation
  /// of some sort, e.g., it is an integer type or a vector.
  bool hasIntegerRepresentation() const;

  /// Determine whether this type has an signed integer representation
  /// of some sort, e.g., it is an signed integer type or a vector.
  bool hasSignedIntegerRepresentation() const;

  /// Determine whether this type has an unsigned integer representation
  /// of some sort, e.g., it is an unsigned integer type or a vector.
  bool hasUnsignedIntegerRepresentation() const;

  /// Determine whether this type has a floating-point representation
  /// of some sort, e.g., it is a floating-point type or a vector thereof.
  bool hasFloatingRepresentation() const;

  // Type Checking Functions: Check to see if this type is structurally the
  // specified type, ignoring typedefs and qualifiers, and return a pointer to
  // the best type we can.
  const RecordType *getAsStructureType() const;
  /// NOTE: getAs*ArrayType are methods on ASTContext.
  const RecordType *getAsUnionType() const;
  const ComplexType *getAsComplexIntegerType() const; // GCC complex int type.
  const ObjCObjectType *getAsObjCInterfaceType() const;

  // The following is a convenience method that returns an ObjCObjectPointerType
  // for object declared using an interface.
  const ObjCObjectPointerType *getAsObjCInterfacePointerType() const;
  const ObjCObjectPointerType *getAsObjCQualifiedIdType() const;
  const ObjCObjectPointerType *getAsObjCQualifiedClassType() const;
  const ObjCObjectType *getAsObjCQualifiedInterfaceType() const;

  /// Retrieves the CXXRecordDecl that this type refers to, either
  /// because the type is a RecordType or because it is the injected-class-name
  /// type of a class template or class template partial specialization.
  CXXRecordDecl *getAsCXXRecordDecl() const;

  /// Retrieves the RecordDecl this type refers to.
  RecordDecl *getAsRecordDecl() const;

  /// Retrieves the TagDecl that this type refers to, either
  /// because the type is a TagType or because it is the injected-class-name
  /// type of a class template or class template partial specialization.
  TagDecl *getAsTagDecl() const;

  /// If this is a pointer or reference to a RecordType, return the
  /// CXXRecordDecl that the type refers to.
  ///
  /// If this is not a pointer or reference, or the type being pointed to does
  /// not refer to a CXXRecordDecl, returns NULL.
  const CXXRecordDecl *getPointeeCXXRecordDecl() const;

  /// Get the DeducedType whose type will be deduced for a variable with
  /// an initializer of this type. This looks through declarators like pointer
  /// types, but not through decltype or typedefs.
  DeducedType *getContainedDeducedType() const;

  /// Get the AutoType whose type will be deduced for a variable with
  /// an initializer of this type. This looks through declarators like pointer
  /// types, but not through decltype or typedefs.
  AutoType *getContainedAutoType() const {
    return dyn_cast_or_null<AutoType>(getContainedDeducedType());
  }

  /// Determine whether this type was written with a leading 'auto'
  /// corresponding to a trailing return type (possibly for a nested
  /// function type within a pointer to function type or similar).
  bool hasAutoForTrailingReturnType() const;

  /// Member-template getAs<specific type>'.  Look through sugar for
  /// an instance of \<specific type>.   This scheme will eventually
  /// replace the specific getAsXXXX methods above.
  ///
  /// There are some specializations of this member template listed
  /// immediately following this class.
  template <typename T> const T *getAs() const;

  /// Member-template getAsAdjusted<specific type>. Look through specific kinds
  /// of sugar (parens, attributes, etc) for an instance of \<specific type>.
  /// This is used when you need to walk over sugar nodes that represent some
  /// kind of type adjustment from a type that was written as a \<specific type>
  /// to another type that is still canonically a \<specific type>.
  template <typename T> const T *getAsAdjusted() const;

  /// A variant of getAs<> for array types which silently discards
  /// qualifiers from the outermost type.
  const ArrayType *getAsArrayTypeUnsafe() const;

  /// Member-template castAs<specific type>.  Look through sugar for
  /// the underlying instance of \<specific type>.
  ///
  /// This method has the same relationship to getAs<T> as cast<T> has
  /// to dyn_cast<T>; which is to say, the underlying type *must*
  /// have the intended type, and this method will never return null.
  template <typename T> const T *castAs() const;

  /// A variant of castAs<> for array type which silently discards
  /// qualifiers from the outermost type.
  const ArrayType *castAsArrayTypeUnsafe() const;

  /// Determine whether this type had the specified attribute applied to it
  /// (looking through top-level type sugar).
  bool hasAttr(attr::Kind AK) const;

  /// Get the base element type of this type, potentially discarding type
  /// qualifiers.  This should never be used when type qualifiers
  /// are meaningful.
  const Type *getBaseElementTypeUnsafe() const;

  /// If this is an array type, return the element type of the array,
  /// potentially with type qualifiers missing.
  /// This should never be used when type qualifiers are meaningful.
  const Type *getArrayElementTypeNoTypeQual() const;

  /// If this is a pointer type, return the pointee type.
  /// If this is an array type, return the array element type.
  /// This should never be used when type qualifiers are meaningful.
  const Type *getPointeeOrArrayElementType() const;

  /// If this is a pointer, ObjC object pointer, or block
  /// pointer, this returns the respective pointee.
  QualType getPointeeType() const;

  /// Return the specified type with any "sugar" removed from the type,
  /// removing any typedefs, typeofs, etc., as well as any qualifiers.
  const Type *getUnqualifiedDesugaredType() const;

  /// Return true if this is an integer type that is
  /// signed, according to C99 6.2.5p4 [char, signed char, short, int, long..],
  /// or an enum decl which has a signed representation.
  bool isSignedIntegerType() const;

  /// Return true if this is an integer type that is
  /// unsigned, according to C99 6.2.5p6 [which returns true for _Bool],
  /// or an enum decl which has an unsigned representation.
  bool isUnsignedIntegerType() const;

  /// Determines whether this is an integer type that is signed or an
  /// enumeration types whose underlying type is a signed integer type.
  bool isSignedIntegerOrEnumerationType() const;

  /// Determines whether this is an integer type that is unsigned or an
  /// enumeration types whose underlying type is a unsigned integer type.
  bool isUnsignedIntegerOrEnumerationType() const;

  /// Return true if this is a fixed point type according to
  /// ISO/IEC JTC1 SC22 WG14 N1169.
  bool isFixedPointType() const;

  /// Return true if this is a fixed point or integer type.
  bool isFixedPointOrIntegerType() const;

  /// Return true if this can be converted to (or from) a fixed point type.
  bool isConvertibleToFixedPointType() const;

  /// Return true if this is a saturated fixed point type according to
  /// ISO/IEC JTC1 SC22 WG14 N1169. This type can be signed or unsigned.
  bool isSaturatedFixedPointType() const;

  /// Return true if this is a saturated fixed point type according to
  /// ISO/IEC JTC1 SC22 WG14 N1169. This type can be signed or unsigned.
  bool isUnsaturatedFixedPointType() const;

  /// Return true if this is a fixed point type that is signed according
  /// to ISO/IEC JTC1 SC22 WG14 N1169. This type can also be saturated.
  bool isSignedFixedPointType() const;

  /// Return true if this is a fixed point type that is unsigned according
  /// to ISO/IEC JTC1 SC22 WG14 N1169. This type can also be saturated.
  bool isUnsignedFixedPointType() const;

  /// Return true if this is not a variable sized type,
  /// according to the rules of C99 6.7.5p3.  It is not legal to call this on
  /// incomplete types.
  bool isConstantSizeType() const;

  /// Returns true if this type can be represented by some
  /// set of type specifiers.
  bool isSpecifierType() const;

  /// Determine the linkage of this type.
  Linkage getLinkage() const;

  /// Determine the visibility of this type.
  Visibility getVisibility() const {
    return getLinkageAndVisibility().getVisibility();
  }

  /// Return true if the visibility was explicitly set is the code.
  bool isVisibilityExplicit() const {
    return getLinkageAndVisibility().isVisibilityExplicit();
  }

  /// Determine the linkage and visibility of this type.
  LinkageInfo getLinkageAndVisibility() const;

  /// True if the computed linkage is valid. Used for consistency
  /// checking. Should always return true.
  bool isLinkageValid() const;

  /// Determine the nullability of the given type.
  ///
  /// Note that nullability is only captured as sugar within the type
  /// system, not as part of the canonical type, so nullability will
  /// be lost by canonicalization and desugaring.
  std::optional<NullabilityKind> getNullability() const;

  /// Determine whether the given type can have a nullability
  /// specifier applied to it, i.e., if it is any kind of pointer type.
  ///
  /// \param ResultIfUnknown The value to return if we don't yet know whether
  ///        this type can have nullability because it is dependent.
  bool canHaveNullability(bool ResultIfUnknown = true) const;

  /// Retrieve the set of substitutions required when accessing a member
  /// of the Objective-C receiver type that is declared in the given context.
  ///
  /// \c *this is the type of the object we're operating on, e.g., the
  /// receiver for a message send or the base of a property access, and is
  /// expected to be of some object or object pointer type.
  ///
  /// \param dc The declaration context for which we are building up a
  /// substitution mapping, which should be an Objective-C class, extension,
  /// category, or method within.
  ///
  /// \returns an array of type arguments that can be substituted for
  /// the type parameters of the given declaration context in any type described
  /// within that context, or an empty optional to indicate that no
  /// substitution is required.
  std::optional<ArrayRef<QualType>>
  getObjCSubstitutions(const DeclContext *dc) const;

  /// Determines if this is an ObjC interface type that may accept type
  /// parameters.
  bool acceptsObjCTypeParams() const;

  const char *getTypeClassName() const;

  QualType getCanonicalTypeInternal() const {
    return CanonicalType;
  }

  CanQualType getCanonicalTypeUnqualified() const; // in CanonicalType.h
  void dump() const;
  void dump(llvm::raw_ostream &OS, const ASTContext &Context) const;
};

/// This will check for a TypedefType by removing any existing sugar
/// until it reaches a TypedefType or a non-sugared type.
template <> const TypedefType *Type::getAs() const;
template <> const UsingType *Type::getAs() const;

/// This will check for a TemplateSpecializationType by removing any
/// existing sugar until it reaches a TemplateSpecializationType or a
/// non-sugared type.
template <> const TemplateSpecializationType *Type::getAs() const;

/// This will check for an AttributedType by removing any existing sugar
/// until it reaches an AttributedType or a non-sugared type.
template <> const AttributedType *Type::getAs() const;

/// This will check for a BoundsAttributedType by removing any existing
/// sugar until it reaches an BoundsAttributedType or a non-sugared type.
template <> const BoundsAttributedType *Type::getAs() const;

/// This will check for a CountAttributedType by removing any existing
/// sugar until it reaches an CountAttributedType or a non-sugared type.
template <> const CountAttributedType *Type::getAs() const;

// We can do canonical leaf types faster, because we don't have to
// worry about preserving child type decoration.
#define TYPE(Class, Base)
#define LEAF_TYPE(Class) \
template <> inline const Class##Type *Type::getAs() const { \
  return dyn_cast<Class##Type>(CanonicalType); \
} \
template <> inline const Class##Type *Type::castAs() const { \
  return cast<Class##Type>(CanonicalType); \
}
#include "clang/AST/TypeNodes.inc"

/// This class is used for builtin types like 'int'.  Builtin
/// types are always canonical and have a literal name field.
class BuiltinType : public Type {
public:
  enum Kind {
// OpenCL image types
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) Id,
#include "clang/Basic/OpenCLImageTypes.def"
// OpenCL extension types
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) Id,
#include "clang/Basic/OpenCLExtensionTypes.def"
// SVE Types
#define SVE_TYPE(Name, Id, SingletonId) Id,
#include "clang/Basic/AArch64SVEACLETypes.def"
// PPC MMA Types
#define PPC_VECTOR_TYPE(Name, Id, Size) Id,
#include "clang/Basic/PPCTypes.def"
// RVV Types
#define RVV_TYPE(Name, Id, SingletonId) Id,
#include "clang/Basic/RISCVVTypes.def"
// WebAssembly reference types
#define WASM_TYPE(Name, Id, SingletonId) Id,
#include "clang/Basic/WebAssemblyReferenceTypes.def"
// AMDGPU types
#define AMDGPU_TYPE(Name, Id, SingletonId) Id,
#include "clang/Basic/AMDGPUTypes.def"
// All other builtin types
#define BUILTIN_TYPE(Id, SingletonId) Id,
#define LAST_BUILTIN_TYPE(Id) LastKind = Id
#include "clang/AST/BuiltinTypes.def"
  };

private:
  friend class ASTContext; // ASTContext creates these.

  BuiltinType(Kind K)
      : Type(Builtin, QualType(),
             K == Dependent ? TypeDependence::DependentInstantiation
                            : TypeDependence::None) {
    static_assert(Kind::LastKind <
                      (1 << BuiltinTypeBitfields::NumOfBuiltinTypeBits) &&
                  "Defined builtin type exceeds the allocated space for serial "
                  "numbering");
    BuiltinTypeBits.Kind = K;
  }

public:
  Kind getKind() const { return static_cast<Kind>(BuiltinTypeBits.Kind); }
  StringRef getName(const PrintingPolicy &Policy) const;

  const char *getNameAsCString(const PrintingPolicy &Policy) const {
    // The StringRef is null-terminated.
    StringRef str = getName(Policy);
    assert(!str.empty() && str.data()[str.size()] == '\0');
    return str.data();
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  bool isInteger() const {
    return getKind() >= Bool && getKind() <= Int128;
  }

  bool isSignedInteger() const {
    return getKind() >= Char_S && getKind() <= Int128;
  }

  bool isUnsignedInteger() const {
    return getKind() >= Bool && getKind() <= UInt128;
  }

  bool isFloatingPoint() const {
    return getKind() >= Half && getKind() <= Ibm128;
  }

  bool isSVEBool() const { return getKind() == Kind::SveBool; }

  bool isSVECount() const { return getKind() == Kind::SveCount; }

  /// Determines whether the given kind corresponds to a placeholder type.
  static bool isPlaceholderTypeKind(Kind K) {
    return K >= Overload;
  }

  /// Determines whether this type is a placeholder type, i.e. a type
  /// which cannot appear in arbitrary positions in a fully-formed
  /// expression.
  bool isPlaceholderType() const {
    return isPlaceholderTypeKind(getKind());
  }

  /// Determines whether this type is a placeholder type other than
  /// Overload.  Most placeholder types require only syntactic
  /// information about their context in order to be resolved (e.g.
  /// whether it is a call expression), which means they can (and
  /// should) be resolved in an earlier "phase" of analysis.
  /// Overload expressions sometimes pick up further information
  /// from their context, like whether the context expects a
  /// specific function-pointer type, and so frequently need
  /// special treatment.
  bool isNonOverloadPlaceholderType() const {
    return getKind() > Overload;
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Builtin; }
};

/// Complex values, per C99 6.2.5p11.  This supports the C99 complex
/// types (_Complex float etc) as well as the GCC integer complex extensions.
class ComplexType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  QualType ElementType;

  ComplexType(QualType Element, QualType CanonicalPtr)
      : Type(Complex, CanonicalPtr, Element->getDependence()),
        ElementType(Element) {}

public:
  QualType getElementType() const { return ElementType; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Element) {
    ID.AddPointer(Element.getAsOpaquePtr());
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Complex; }
};

/// Sugar for parentheses used when specifying types.
class ParenType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  QualType Inner;

  ParenType(QualType InnerType, QualType CanonType)
      : Type(Paren, CanonType, InnerType->getDependence()), Inner(InnerType) {}

public:
  QualType getInnerType() const { return Inner; }

  bool isSugared() const { return true; }
  QualType desugar() const { return getInnerType(); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getInnerType());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Inner) {
    Inner.Profile(ID);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Paren; }
};

/// PointerType - C99 6.7.5.1 - Pointer Declarators.
class PointerType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  QualType PointeeType;

  PointerType(QualType Pointee, QualType CanonicalPtr)
      : Type(Pointer, CanonicalPtr, Pointee->getDependence()),
        PointeeType(Pointee) {}

public:
  QualType getPointeeType() const { return PointeeType; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getPointeeType());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Pointee) {
    ID.AddPointer(Pointee.getAsOpaquePtr());
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Pointer; }
};

/// [BoundsSafety] Represents information of declarations referenced by the
/// arguments of the `counted_by` attribute and the likes.
class TypeCoupledDeclRefInfo {
public:
  using BaseTy = llvm::PointerIntPair<ValueDecl *, 1, unsigned>;

private:
  enum {
    DerefShift = 0,
    DerefMask = 1,
  };
  BaseTy Data;

public:
  /// \p D is to a declaration referenced by the argument of attribute. \p Deref
  /// indicates whether \p D is referenced as a dereferenced form, e.g., \p
  /// Deref is true for `*n` in `int *__counted_by(*n)`.
  TypeCoupledDeclRefInfo(ValueDecl *D = nullptr, bool Deref = false);

  bool isDeref() const;
  ValueDecl *getDecl() const;
  unsigned getInt() const;
  void *getOpaqueValue() const;
  bool operator==(const TypeCoupledDeclRefInfo &Other) const;
  void setFromOpaqueValue(void *V);
};

/// [BoundsSafety] Represents a parent type class for CountAttributedType and
/// similar sugar types that will be introduced to represent a type with a
/// bounds attribute.
///
/// Provides a common interface to navigate declarations referred to by the
/// bounds expression.

class BoundsAttributedType : public Type, public llvm::FoldingSetNode {
  QualType WrappedTy;

protected:
  ArrayRef<TypeCoupledDeclRefInfo> Decls; // stored in trailing objects

  BoundsAttributedType(TypeClass TC, QualType Wrapped, QualType Canon);

public:
  bool isSugared() const { return true; }
  QualType desugar() const { return WrappedTy; }

  using decl_iterator = const TypeCoupledDeclRefInfo *;
  using decl_range = llvm::iterator_range<decl_iterator>;

  decl_iterator dependent_decl_begin() const { return Decls.begin(); }
  decl_iterator dependent_decl_end() const { return Decls.end(); }

  unsigned getNumCoupledDecls() const { return Decls.size(); }

  decl_range dependent_decls() const {
    return decl_range(dependent_decl_begin(), dependent_decl_end());
  }

  ArrayRef<TypeCoupledDeclRefInfo> getCoupledDecls() const {
    return {dependent_decl_begin(), dependent_decl_end()};
  }

  bool referencesFieldDecls() const;

  static bool classof(const Type *T) {
    // Currently, only `class CountAttributedType` inherits
    // `BoundsAttributedType` but the subclass will grow as we add more bounds
    // annotations.
    switch (T->getTypeClass()) {
    case CountAttributed:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a sugar type with `__counted_by` or `__sized_by` annotations,
/// including their `_or_null` variants.
class CountAttributedType final
    : public BoundsAttributedType,
      public llvm::TrailingObjects<CountAttributedType,
                                   TypeCoupledDeclRefInfo> {
  friend class ASTContext;

  Expr *CountExpr;
  /// \p CountExpr represents the argument of __counted_by or the likes. \p
  /// CountInBytes indicates that \p CountExpr is a byte count (i.e.,
  /// __sized_by(_or_null)) \p OrNull means it's an or_null variant (i.e.,
  /// __counted_by_or_null or __sized_by_or_null) \p CoupledDecls contains the
  /// list of declarations referenced by \p CountExpr, which the type depends on
  /// for the bounds information.
  CountAttributedType(QualType Wrapped, QualType Canon, Expr *CountExpr,
                      bool CountInBytes, bool OrNull,
                      ArrayRef<TypeCoupledDeclRefInfo> CoupledDecls);

  unsigned numTrailingObjects(OverloadToken<TypeCoupledDeclRefInfo>) const {
    return CountAttributedTypeBits.NumCoupledDecls;
  }

public:
  enum DynamicCountPointerKind {
    CountedBy = 0,
    SizedBy,
    CountedByOrNull,
    SizedByOrNull,
  };

  Expr *getCountExpr() const { return CountExpr; }
  bool isCountInBytes() const { return CountAttributedTypeBits.CountInBytes; }
  bool isOrNull() const { return CountAttributedTypeBits.OrNull; }

  DynamicCountPointerKind getKind() const {
    if (isOrNull())
      return isCountInBytes() ? SizedByOrNull : CountedByOrNull;
    return isCountInBytes() ? SizedBy : CountedBy;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, desugar(), CountExpr, isCountInBytes(), isOrNull());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType WrappedTy,
                      Expr *CountExpr, bool CountInBytes, bool Nullable);

  static bool classof(const Type *T) {
    return T->getTypeClass() == CountAttributed;
  }
};

/// Represents a type which was implicitly adjusted by the semantic
/// engine for arbitrary reasons.  For example, array and function types can
/// decay, and function types can have their calling conventions adjusted.
class AdjustedType : public Type, public llvm::FoldingSetNode {
  QualType OriginalTy;
  QualType AdjustedTy;

protected:
  friend class ASTContext; // ASTContext creates these.

  AdjustedType(TypeClass TC, QualType OriginalTy, QualType AdjustedTy,
               QualType CanonicalPtr)
      : Type(TC, CanonicalPtr, OriginalTy->getDependence()),
        OriginalTy(OriginalTy), AdjustedTy(AdjustedTy) {}

public:
  QualType getOriginalType() const { return OriginalTy; }
  QualType getAdjustedType() const { return AdjustedTy; }

  bool isSugared() const { return true; }
  QualType desugar() const { return AdjustedTy; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, OriginalTy, AdjustedTy);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Orig, QualType New) {
    ID.AddPointer(Orig.getAsOpaquePtr());
    ID.AddPointer(New.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Adjusted || T->getTypeClass() == Decayed;
  }
};

/// Represents a pointer type decayed from an array or function type.
class DecayedType : public AdjustedType {
  friend class ASTContext; // ASTContext creates these.

  inline
  DecayedType(QualType OriginalType, QualType Decayed, QualType Canonical);

public:
  QualType getDecayedType() const { return getAdjustedType(); }

  inline QualType getPointeeType() const;

  static bool classof(const Type *T) { return T->getTypeClass() == Decayed; }
};

/// Pointer to a block type.
/// This type is to represent types syntactically represented as
/// "void (^)(int)", etc. Pointee is required to always be a function type.
class BlockPointerType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  // Block is some kind of pointer type
  QualType PointeeType;

  BlockPointerType(QualType Pointee, QualType CanonicalCls)
      : Type(BlockPointer, CanonicalCls, Pointee->getDependence()),
        PointeeType(Pointee) {}

public:
  // Get the pointee type. Pointee is required to always be a function type.
  QualType getPointeeType() const { return PointeeType; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
      Profile(ID, getPointeeType());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Pointee) {
      ID.AddPointer(Pointee.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == BlockPointer;
  }
};

/// Base for LValueReferenceType and RValueReferenceType
class ReferenceType : public Type, public llvm::FoldingSetNode {
  QualType PointeeType;

protected:
  ReferenceType(TypeClass tc, QualType Referencee, QualType CanonicalRef,
                bool SpelledAsLValue)
      : Type(tc, CanonicalRef, Referencee->getDependence()),
        PointeeType(Referencee) {
    ReferenceTypeBits.SpelledAsLValue = SpelledAsLValue;
    ReferenceTypeBits.InnerRef = Referencee->isReferenceType();
  }

public:
  bool isSpelledAsLValue() const { return ReferenceTypeBits.SpelledAsLValue; }
  bool isInnerRef() const { return ReferenceTypeBits.InnerRef; }

  QualType getPointeeTypeAsWritten() const { return PointeeType; }

  QualType getPointeeType() const {
    // FIXME: this might strip inner qualifiers; okay?
    const ReferenceType *T = this;
    while (T->isInnerRef())
      T = T->PointeeType->castAs<ReferenceType>();
    return T->PointeeType;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, PointeeType, isSpelledAsLValue());
  }

  static void Profile(llvm::FoldingSetNodeID &ID,
                      QualType Referencee,
                      bool SpelledAsLValue) {
    ID.AddPointer(Referencee.getAsOpaquePtr());
    ID.AddBoolean(SpelledAsLValue);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == LValueReference ||
           T->getTypeClass() == RValueReference;
  }
};

/// An lvalue reference type, per C++11 [dcl.ref].
class LValueReferenceType : public ReferenceType {
  friend class ASTContext; // ASTContext creates these

  LValueReferenceType(QualType Referencee, QualType CanonicalRef,
                      bool SpelledAsLValue)
      : ReferenceType(LValueReference, Referencee, CanonicalRef,
                      SpelledAsLValue) {}

public:
  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == LValueReference;
  }
};

/// An rvalue reference type, per C++11 [dcl.ref].
class RValueReferenceType : public ReferenceType {
  friend class ASTContext; // ASTContext creates these

  RValueReferenceType(QualType Referencee, QualType CanonicalRef)
       : ReferenceType(RValueReference, Referencee, CanonicalRef, false) {}

public:
  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == RValueReference;
  }
};

/// A pointer to member type per C++ 8.3.3 - Pointers to members.
///
/// This includes both pointers to data members and pointer to member functions.
class MemberPointerType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  QualType PointeeType;

  /// The class of which the pointee is a member. Must ultimately be a
  /// RecordType, but could be a typedef or a template parameter too.
  const Type *Class;

  MemberPointerType(QualType Pointee, const Type *Cls, QualType CanonicalPtr)
      : Type(MemberPointer, CanonicalPtr,
             (Cls->getDependence() & ~TypeDependence::VariablyModified) |
                 Pointee->getDependence()),
        PointeeType(Pointee), Class(Cls) {}

public:
  QualType getPointeeType() const { return PointeeType; }

  /// Returns true if the member type (i.e. the pointee type) is a
  /// function type rather than a data-member type.
  bool isMemberFunctionPointer() const {
    return PointeeType->isFunctionProtoType();
  }

  /// Returns true if the member type (i.e. the pointee type) is a
  /// data type rather than a function type.
  bool isMemberDataPointer() const {
    return !PointeeType->isFunctionProtoType();
  }

  const Type *getClass() const { return Class; }
  CXXRecordDecl *getMostRecentCXXRecordDecl() const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getPointeeType(), getClass());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Pointee,
                      const Type *Class) {
    ID.AddPointer(Pointee.getAsOpaquePtr());
    ID.AddPointer(Class);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == MemberPointer;
  }
};

/// Capture whether this is a normal array (e.g. int X[4])
/// an array with a static size (e.g. int X[static 4]), or an array
/// with a star size (e.g. int X[*]).
/// 'static' is only allowed on function parameters.
enum class ArraySizeModifier { Normal, Static, Star };

/// Represents an array type, per C99 6.7.5.2 - Array Declarators.
class ArrayType : public Type, public llvm::FoldingSetNode {
private:
  /// The element type of the array.
  QualType ElementType;

protected:
  friend class ASTContext; // ASTContext creates these.

  ArrayType(TypeClass tc, QualType et, QualType can, ArraySizeModifier sm,
            unsigned tq, const Expr *sz = nullptr);

public:
  QualType getElementType() const { return ElementType; }

  ArraySizeModifier getSizeModifier() const {
    return ArraySizeModifier(ArrayTypeBits.SizeModifier);
  }

  Qualifiers getIndexTypeQualifiers() const {
    return Qualifiers::fromCVRMask(getIndexTypeCVRQualifiers());
  }

  unsigned getIndexTypeCVRQualifiers() const {
    return ArrayTypeBits.IndexTypeQuals;
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantArray ||
           T->getTypeClass() == VariableArray ||
           T->getTypeClass() == IncompleteArray ||
           T->getTypeClass() == DependentSizedArray ||
           T->getTypeClass() == ArrayParameter;
  }
};

/// Represents the canonical version of C arrays with a specified constant size.
/// For example, the canonical type for 'int A[4 + 4*100]' is a
/// ConstantArrayType where the element type is 'int' and the size is 404.
class ConstantArrayType : public ArrayType {
  friend class ASTContext; // ASTContext creates these.

  struct ExternalSize {
    ExternalSize(const llvm::APInt &Sz, const Expr *SE)
        : Size(Sz), SizeExpr(SE) {}
    llvm::APInt Size; // Allows us to unique the type.
    const Expr *SizeExpr;
  };

  union {
    uint64_t Size;
    ExternalSize *SizePtr;
  };

  ConstantArrayType(QualType Et, QualType Can, uint64_t Width, uint64_t Sz,
                    ArraySizeModifier SM, unsigned TQ)
      : ArrayType(ConstantArray, Et, Can, SM, TQ, nullptr), Size(Sz) {
    ConstantArrayTypeBits.HasExternalSize = false;
    ConstantArrayTypeBits.SizeWidth = Width / 8;
    // The in-structure size stores the size in bytes rather than bits so we
    // drop the three least significant bits since they're always zero anyways.
    assert(Width < 0xFF && "Type width in bits must be less than 8 bits");
  }

  ConstantArrayType(QualType Et, QualType Can, ExternalSize *SzPtr,
                    ArraySizeModifier SM, unsigned TQ)
      : ArrayType(ConstantArray, Et, Can, SM, TQ, SzPtr->SizeExpr),
        SizePtr(SzPtr) {
    ConstantArrayTypeBits.HasExternalSize = true;
    ConstantArrayTypeBits.SizeWidth = 0;

    assert((SzPtr->SizeExpr == nullptr || !Can.isNull()) &&
           "canonical constant array should not have size expression");
  }

  static ConstantArrayType *Create(const ASTContext &Ctx, QualType ET,
                                   QualType Can, const llvm::APInt &Sz,
                                   const Expr *SzExpr, ArraySizeModifier SzMod,
                                   unsigned Qual);

protected:
  ConstantArrayType(TypeClass Tc, const ConstantArrayType *ATy, QualType Can)
      : ArrayType(Tc, ATy->getElementType(), Can, ATy->getSizeModifier(),
                  ATy->getIndexTypeQualifiers().getAsOpaqueValue(), nullptr) {
    ConstantArrayTypeBits.HasExternalSize =
        ATy->ConstantArrayTypeBits.HasExternalSize;
    if (!ConstantArrayTypeBits.HasExternalSize) {
      ConstantArrayTypeBits.SizeWidth = ATy->ConstantArrayTypeBits.SizeWidth;
      Size = ATy->Size;
    } else
      SizePtr = ATy->SizePtr;
  }

public:
  /// Return the constant array size as an APInt.
  llvm::APInt getSize() const {
    return ConstantArrayTypeBits.HasExternalSize
               ? SizePtr->Size
               : llvm::APInt(ConstantArrayTypeBits.SizeWidth * 8, Size);
  }

  /// Return the bit width of the size type.
  unsigned getSizeBitWidth() const {
    return ConstantArrayTypeBits.HasExternalSize
               ? SizePtr->Size.getBitWidth()
               : static_cast<unsigned>(ConstantArrayTypeBits.SizeWidth * 8);
  }

  /// Return true if the size is zero.
  bool isZeroSize() const {
    return ConstantArrayTypeBits.HasExternalSize ? SizePtr->Size.isZero()
                                                 : 0 == Size;
  }

  /// Return the size zero-extended as a uint64_t.
  uint64_t getZExtSize() const {
    return ConstantArrayTypeBits.HasExternalSize ? SizePtr->Size.getZExtValue()
                                                 : Size;
  }

  /// Return the size sign-extended as a uint64_t.
  int64_t getSExtSize() const {
    return ConstantArrayTypeBits.HasExternalSize ? SizePtr->Size.getSExtValue()
                                                 : static_cast<int64_t>(Size);
  }

  /// Return the size zero-extended to uint64_t or UINT64_MAX if the value is
  /// larger than UINT64_MAX.
  uint64_t getLimitedSize() const {
    return ConstantArrayTypeBits.HasExternalSize
               ? SizePtr->Size.getLimitedValue()
               : Size;
  }

  /// Return a pointer to the size expression.
  const Expr *getSizeExpr() const {
    return ConstantArrayTypeBits.HasExternalSize ? SizePtr->SizeExpr : nullptr;
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  /// Determine the number of bits required to address a member of
  // an array with the given element type and number of elements.
  static unsigned getNumAddressingBits(const ASTContext &Context,
                                       QualType ElementType,
                                       const llvm::APInt &NumElements);

  unsigned getNumAddressingBits(const ASTContext &Context) const;

  /// Determine the maximum number of active bits that an array's size
  /// can require, which limits the maximum size of the array.
  static unsigned getMaxSizeBits(const ASTContext &Context);

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Ctx) {
    Profile(ID, Ctx, getElementType(), getZExtSize(), getSizeExpr(),
            getSizeModifier(), getIndexTypeCVRQualifiers());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Ctx,
                      QualType ET, uint64_t ArraySize, const Expr *SizeExpr,
                      ArraySizeModifier SizeMod, unsigned TypeQuals);

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantArray ||
           T->getTypeClass() == ArrayParameter;
  }
};

/// Represents a constant array type that does not decay to a pointer when used
/// as a function parameter.
class ArrayParameterType : public ConstantArrayType {
  friend class ASTContext; // ASTContext creates these.

  ArrayParameterType(const ConstantArrayType *ATy, QualType CanTy)
      : ConstantArrayType(ArrayParameter, ATy, CanTy) {}

public:
  static bool classof(const Type *T) {
    return T->getTypeClass() == ArrayParameter;
  }
};

/// Represents a C array with an unspecified size.  For example 'int A[]' has
/// an IncompleteArrayType where the element type is 'int' and the size is
/// unspecified.
class IncompleteArrayType : public ArrayType {
  friend class ASTContext; // ASTContext creates these.

  IncompleteArrayType(QualType et, QualType can,
                      ArraySizeModifier sm, unsigned tq)
      : ArrayType(IncompleteArray, et, can, sm, tq) {}

public:
  friend class StmtIteratorBase;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == IncompleteArray;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), getSizeModifier(),
            getIndexTypeCVRQualifiers());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ET,
                      ArraySizeModifier SizeMod, unsigned TypeQuals) {
    ID.AddPointer(ET.getAsOpaquePtr());
    ID.AddInteger(llvm::to_underlying(SizeMod));
    ID.AddInteger(TypeQuals);
  }
};

/// Represents a C array with a specified size that is not an
/// integer-constant-expression.  For example, 'int s[x+foo()]'.
/// Since the size expression is an arbitrary expression, we store it as such.
///
/// Note: VariableArrayType's aren't uniqued (since the expressions aren't) and
/// should not be: two lexically equivalent variable array types could mean
/// different things, for example, these variables do not have the same type
/// dynamically:
///
/// void foo(int x) {
///   int Y[x];
///   ++x;
///   int Z[x];
/// }
class VariableArrayType : public ArrayType {
  friend class ASTContext; // ASTContext creates these.

  /// An assignment-expression. VLA's are only permitted within
  /// a function block.
  Stmt *SizeExpr;

  /// The range spanned by the left and right array brackets.
  SourceRange Brackets;

  VariableArrayType(QualType et, QualType can, Expr *e,
                    ArraySizeModifier sm, unsigned tq,
                    SourceRange brackets)
      : ArrayType(VariableArray, et, can, sm, tq, e),
        SizeExpr((Stmt*) e), Brackets(brackets) {}

public:
  friend class StmtIteratorBase;

  Expr *getSizeExpr() const {
    // We use C-style casts instead of cast<> here because we do not wish
    // to have a dependency of Type.h on Stmt.h/Expr.h.
    return (Expr*) SizeExpr;
  }

  SourceRange getBracketsRange() const { return Brackets; }
  SourceLocation getLBracketLoc() const { return Brackets.getBegin(); }
  SourceLocation getRBracketLoc() const { return Brackets.getEnd(); }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == VariableArray;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    llvm_unreachable("Cannot unique VariableArrayTypes.");
  }
};

/// Represents an array type in C++ whose size is a value-dependent expression.
///
/// For example:
/// \code
/// template<typename T, int Size>
/// class array {
///   T data[Size];
/// };
/// \endcode
///
/// For these types, we won't actually know what the array bound is
/// until template instantiation occurs, at which point this will
/// become either a ConstantArrayType or a VariableArrayType.
class DependentSizedArrayType : public ArrayType {
  friend class ASTContext; // ASTContext creates these.

  /// An assignment expression that will instantiate to the
  /// size of the array.
  ///
  /// The expression itself might be null, in which case the array
  /// type will have its size deduced from an initializer.
  Stmt *SizeExpr;

  /// The range spanned by the left and right array brackets.
  SourceRange Brackets;

  DependentSizedArrayType(QualType et, QualType can, Expr *e,
                          ArraySizeModifier sm, unsigned tq,
                          SourceRange brackets);

public:
  friend class StmtIteratorBase;

  Expr *getSizeExpr() const {
    // We use C-style casts instead of cast<> here because we do not wish
    // to have a dependency of Type.h on Stmt.h/Expr.h.
    return (Expr*) SizeExpr;
  }

  SourceRange getBracketsRange() const { return Brackets; }
  SourceLocation getLBracketLoc() const { return Brackets.getBegin(); }
  SourceLocation getRBracketLoc() const { return Brackets.getEnd(); }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentSizedArray;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getElementType(),
            getSizeModifier(), getIndexTypeCVRQualifiers(), getSizeExpr());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      QualType ET, ArraySizeModifier SizeMod,
                      unsigned TypeQuals, Expr *E);
};

/// Represents an extended address space qualifier where the input address space
/// value is dependent. Non-dependent address spaces are not represented with a
/// special Type subclass; they are stored on an ExtQuals node as part of a QualType.
///
/// For example:
/// \code
/// template<typename T, int AddrSpace>
/// class AddressSpace {
///   typedef T __attribute__((address_space(AddrSpace))) type;
/// }
/// \endcode
class DependentAddressSpaceType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext;

  Expr *AddrSpaceExpr;
  QualType PointeeType;
  SourceLocation loc;

  DependentAddressSpaceType(QualType PointeeType, QualType can,
                            Expr *AddrSpaceExpr, SourceLocation loc);

public:
  Expr *getAddrSpaceExpr() const { return AddrSpaceExpr; }
  QualType getPointeeType() const { return PointeeType; }
  SourceLocation getAttributeLoc() const { return loc; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentAddressSpace;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getPointeeType(), getAddrSpaceExpr());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      QualType PointeeType, Expr *AddrSpaceExpr);
};

/// Represents an extended vector type where either the type or size is
/// dependent.
///
/// For example:
/// \code
/// template<typename T, int Size>
/// class vector {
///   typedef T __attribute__((ext_vector_type(Size))) type;
/// }
/// \endcode
class DependentSizedExtVectorType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext;

  Expr *SizeExpr;

  /// The element type of the array.
  QualType ElementType;

  SourceLocation loc;

  DependentSizedExtVectorType(QualType ElementType, QualType can,
                              Expr *SizeExpr, SourceLocation loc);

public:
  Expr *getSizeExpr() const { return SizeExpr; }
  QualType getElementType() const { return ElementType; }
  SourceLocation getAttributeLoc() const { return loc; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentSizedExtVector;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getElementType(), getSizeExpr());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      QualType ElementType, Expr *SizeExpr);
};

enum class VectorKind {
  /// not a target-specific vector type
  Generic,

  /// is AltiVec vector
  AltiVecVector,

  /// is AltiVec 'vector Pixel'
  AltiVecPixel,

  /// is AltiVec 'vector bool ...'
  AltiVecBool,

  /// is ARM Neon vector
  Neon,

  /// is ARM Neon polynomial vector
  NeonPoly,

  /// is AArch64 SVE fixed-length data vector
  SveFixedLengthData,

  /// is AArch64 SVE fixed-length predicate vector
  SveFixedLengthPredicate,

  /// is RISC-V RVV fixed-length data vector
  RVVFixedLengthData,

  /// is RISC-V RVV fixed-length mask vector
  RVVFixedLengthMask,
};

/// Represents a GCC generic vector type. This type is created using
/// __attribute__((vector_size(n)), where "n" specifies the vector size in
/// bytes; or from an Altivec __vector or vector declaration.
/// Since the constructor takes the number of vector elements, the
/// client is responsible for converting the size into the number of elements.
class VectorType : public Type, public llvm::FoldingSetNode {
protected:
  friend class ASTContext; // ASTContext creates these.

  /// The element type of the vector.
  QualType ElementType;

  VectorType(QualType vecType, unsigned nElements, QualType canonType,
             VectorKind vecKind);

  VectorType(TypeClass tc, QualType vecType, unsigned nElements,
             QualType canonType, VectorKind vecKind);

public:
  QualType getElementType() const { return ElementType; }
  unsigned getNumElements() const { return VectorTypeBits.NumElements; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  VectorKind getVectorKind() const {
    return VectorKind(VectorTypeBits.VecKind);
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), getNumElements(),
            getTypeClass(), getVectorKind());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ElementType,
                      unsigned NumElements, TypeClass TypeClass,
                      VectorKind VecKind) {
    ID.AddPointer(ElementType.getAsOpaquePtr());
    ID.AddInteger(NumElements);
    ID.AddInteger(TypeClass);
    ID.AddInteger(llvm::to_underlying(VecKind));
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Vector || T->getTypeClass() == ExtVector;
  }
};

/// Represents a vector type where either the type or size is dependent.
////
/// For example:
/// \code
/// template<typename T, int Size>
/// class vector {
///   typedef T __attribute__((vector_size(Size))) type;
/// }
/// \endcode
class DependentVectorType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext;

  QualType ElementType;
  Expr *SizeExpr;
  SourceLocation Loc;

  DependentVectorType(QualType ElementType, QualType CanonType, Expr *SizeExpr,
                      SourceLocation Loc, VectorKind vecKind);

public:
  Expr *getSizeExpr() const { return SizeExpr; }
  QualType getElementType() const { return ElementType; }
  SourceLocation getAttributeLoc() const { return Loc; }
  VectorKind getVectorKind() const {
    return VectorKind(VectorTypeBits.VecKind);
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentVector;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getElementType(), getSizeExpr(), getVectorKind());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      QualType ElementType, const Expr *SizeExpr,
                      VectorKind VecKind);
};

/// ExtVectorType - Extended vector type. This type is created using
/// __attribute__((ext_vector_type(n)), where "n" is the number of elements.
/// Unlike vector_size, ext_vector_type is only allowed on typedef's. This
/// class enables syntactic extensions, like Vector Components for accessing
/// points (as .xyzw), colors (as .rgba), and textures (modeled after OpenGL
/// Shading Language).
class ExtVectorType : public VectorType {
  friend class ASTContext; // ASTContext creates these.

  ExtVectorType(QualType vecType, unsigned nElements, QualType canonType)
      : VectorType(ExtVector, vecType, nElements, canonType,
                   VectorKind::Generic) {}

public:
  static int getPointAccessorIdx(char c) {
    switch (c) {
    default: return -1;
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    }
  }

  static int getNumericAccessorIdx(char c) {
    switch (c) {
      default: return -1;
      case '0': return 0;
      case '1': return 1;
      case '2': return 2;
      case '3': return 3;
      case '4': return 4;
      case '5': return 5;
      case '6': return 6;
      case '7': return 7;
      case '8': return 8;
      case '9': return 9;
      case 'A':
      case 'a': return 10;
      case 'B':
      case 'b': return 11;
      case 'C':
      case 'c': return 12;
      case 'D':
      case 'd': return 13;
      case 'E':
      case 'e': return 14;
      case 'F':
      case 'f': return 15;
    }
  }

  static int getAccessorIdx(char c, bool isNumericAccessor) {
    if (isNumericAccessor)
      return getNumericAccessorIdx(c);
    else
      return getPointAccessorIdx(c);
  }

  bool isAccessorWithinNumElements(char c, bool isNumericAccessor) const {
    if (int idx = getAccessorIdx(c, isNumericAccessor)+1)
      return unsigned(idx-1) < getNumElements();
    return false;
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ExtVector;
  }
};

/// Represents a matrix type, as defined in the Matrix Types clang extensions.
/// __attribute__((matrix_type(rows, columns))), where "rows" specifies
/// number of rows and "columns" specifies the number of columns.
class MatrixType : public Type, public llvm::FoldingSetNode {
protected:
  friend class ASTContext;

  /// The element type of the matrix.
  QualType ElementType;

  MatrixType(QualType ElementTy, QualType CanonElementTy);

  MatrixType(TypeClass TypeClass, QualType ElementTy, QualType CanonElementTy,
             const Expr *RowExpr = nullptr, const Expr *ColumnExpr = nullptr);

public:
  /// Returns type of the elements being stored in the matrix
  QualType getElementType() const { return ElementType; }

  /// Valid elements types are the following:
  /// * an integer type (as in C23 6.2.5p22), but excluding enumerated types
  ///   and _Bool
  /// * the standard floating types float or double
  /// * a half-precision floating point type, if one is supported on the target
  static bool isValidElementType(QualType T) {
    return T->isDependentType() ||
           (T->isRealType() && !T->isBooleanType() && !T->isEnumeralType());
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantMatrix ||
           T->getTypeClass() == DependentSizedMatrix;
  }
};

/// Represents a concrete matrix type with constant number of rows and columns
class ConstantMatrixType final : public MatrixType {
protected:
  friend class ASTContext;

  /// Number of rows and columns.
  unsigned NumRows;
  unsigned NumColumns;

  static constexpr unsigned MaxElementsPerDimension = (1 << 20) - 1;

  ConstantMatrixType(QualType MatrixElementType, unsigned NRows,
                     unsigned NColumns, QualType CanonElementType);

  ConstantMatrixType(TypeClass typeClass, QualType MatrixType, unsigned NRows,
                     unsigned NColumns, QualType CanonElementType);

public:
  /// Returns the number of rows in the matrix.
  unsigned getNumRows() const { return NumRows; }

  /// Returns the number of columns in the matrix.
  unsigned getNumColumns() const { return NumColumns; }

  /// Returns the number of elements required to embed the matrix into a vector.
  unsigned getNumElementsFlattened() const {
    return getNumRows() * getNumColumns();
  }

  /// Returns true if \p NumElements is a valid matrix dimension.
  static constexpr bool isDimensionValid(size_t NumElements) {
    return NumElements > 0 && NumElements <= MaxElementsPerDimension;
  }

  /// Returns the maximum number of elements per dimension.
  static constexpr unsigned getMaxElementsPerDimension() {
    return MaxElementsPerDimension;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), getNumRows(), getNumColumns(),
            getTypeClass());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ElementType,
                      unsigned NumRows, unsigned NumColumns,
                      TypeClass TypeClass) {
    ID.AddPointer(ElementType.getAsOpaquePtr());
    ID.AddInteger(NumRows);
    ID.AddInteger(NumColumns);
    ID.AddInteger(TypeClass);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantMatrix;
  }
};

/// Represents a matrix type where the type and the number of rows and columns
/// is dependent on a template.
class DependentSizedMatrixType final : public MatrixType {
  friend class ASTContext;

  Expr *RowExpr;
  Expr *ColumnExpr;

  SourceLocation loc;

  DependentSizedMatrixType(QualType ElementType, QualType CanonicalType,
                           Expr *RowExpr, Expr *ColumnExpr, SourceLocation loc);

public:
  Expr *getRowExpr() const { return RowExpr; }
  Expr *getColumnExpr() const { return ColumnExpr; }
  SourceLocation getAttributeLoc() const { return loc; }

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentSizedMatrix;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getElementType(), getRowExpr(), getColumnExpr());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      QualType ElementType, Expr *RowExpr, Expr *ColumnExpr);
};

/// FunctionType - C99 6.7.5.3 - Function Declarators.  This is the common base
/// class of FunctionNoProtoType and FunctionProtoType.
class FunctionType : public Type {
  // The type returned by the function.
  QualType ResultType;

public:
  /// Interesting information about a specific parameter that can't simply
  /// be reflected in parameter's type. This is only used by FunctionProtoType
  /// but is in FunctionType to make this class available during the
  /// specification of the bases of FunctionProtoType.
  ///
  /// It makes sense to model language features this way when there's some
  /// sort of parameter-specific override (such as an attribute) that
  /// affects how the function is called.  For example, the ARC ns_consumed
  /// attribute changes whether a parameter is passed at +0 (the default)
  /// or +1 (ns_consumed).  This must be reflected in the function type,
  /// but isn't really a change to the parameter type.
  ///
  /// One serious disadvantage of modelling language features this way is
  /// that they generally do not work with language features that attempt
  /// to destructure types.  For example, template argument deduction will
  /// not be able to match a parameter declared as
  ///   T (*)(U)
  /// against an argument of type
  ///   void (*)(__attribute__((ns_consumed)) id)
  /// because the substitution of T=void, U=id into the former will
  /// not produce the latter.
  class ExtParameterInfo {
    enum {
      ABIMask = 0x0F,
      IsConsumed = 0x10,
      HasPassObjSize = 0x20,
      IsNoEscape = 0x40,
    };
    unsigned char Data = 0;

  public:
    ExtParameterInfo() = default;

    /// Return the ABI treatment of this parameter.
    ParameterABI getABI() const { return ParameterABI(Data & ABIMask); }
    ExtParameterInfo withABI(ParameterABI kind) const {
      ExtParameterInfo copy = *this;
      copy.Data = (copy.Data & ~ABIMask) | unsigned(kind);
      return copy;
    }

    /// Is this parameter considered "consumed" by Objective-C ARC?
    /// Consumed parameters must have retainable object type.
    bool isConsumed() const { return (Data & IsConsumed); }
    ExtParameterInfo withIsConsumed(bool consumed) const {
      ExtParameterInfo copy = *this;
      if (consumed)
        copy.Data |= IsConsumed;
      else
        copy.Data &= ~IsConsumed;
      return copy;
    }

    bool hasPassObjectSize() const { return Data & HasPassObjSize; }
    ExtParameterInfo withHasPassObjectSize() const {
      ExtParameterInfo Copy = *this;
      Copy.Data |= HasPassObjSize;
      return Copy;
    }

    bool isNoEscape() const { return Data & IsNoEscape; }
    ExtParameterInfo withIsNoEscape(bool NoEscape) const {
      ExtParameterInfo Copy = *this;
      if (NoEscape)
        Copy.Data |= IsNoEscape;
      else
        Copy.Data &= ~IsNoEscape;
      return Copy;
    }

    unsigned char getOpaqueValue() const { return Data; }
    static ExtParameterInfo getFromOpaqueValue(unsigned char data) {
      ExtParameterInfo result;
      result.Data = data;
      return result;
    }

    friend bool operator==(ExtParameterInfo lhs, ExtParameterInfo rhs) {
      return lhs.Data == rhs.Data;
    }

    friend bool operator!=(ExtParameterInfo lhs, ExtParameterInfo rhs) {
      return lhs.Data != rhs.Data;
    }
  };

  /// A class which abstracts out some details necessary for
  /// making a call.
  ///
  /// It is not actually used directly for storing this information in
  /// a FunctionType, although FunctionType does currently use the
  /// same bit-pattern.
  ///
  // If you add a field (say Foo), other than the obvious places (both,
  // constructors, compile failures), what you need to update is
  // * Operator==
  // * getFoo
  // * withFoo
  // * functionType. Add Foo, getFoo.
  // * ASTContext::getFooType
  // * ASTContext::mergeFunctionTypes
  // * FunctionNoProtoType::Profile
  // * FunctionProtoType::Profile
  // * TypePrinter::PrintFunctionProto
  // * AST read and write
  // * Codegen
  class ExtInfo {
    friend class FunctionType;

    // Feel free to rearrange or add bits, but if you go over 16, you'll need to
    // adjust the Bits field below, and if you add bits, you'll need to adjust
    // Type::FunctionTypeBitfields::ExtInfo as well.

    // |  CC  |noreturn|produces|nocallersavedregs|regparm|nocfcheck|cmsenscall|
    // |0 .. 4|   5    |    6   |       7         |8 .. 10|    11   |    12    |
    //
    // regparm is either 0 (no regparm attribute) or the regparm value+1.
    enum { CallConvMask = 0x1F };
    enum { NoReturnMask = 0x20 };
    enum { ProducesResultMask = 0x40 };
    enum { NoCallerSavedRegsMask = 0x80 };
    enum {
      RegParmMask =  0x700,
      RegParmOffset = 8
    };
    enum { NoCfCheckMask = 0x800 };
    enum { CmseNSCallMask = 0x1000 };
    uint16_t Bits = CC_C;

    ExtInfo(unsigned Bits) : Bits(static_cast<uint16_t>(Bits)) {}

  public:
    // Constructor with no defaults. Use this when you know that you
    // have all the elements (when reading an AST file for example).
    ExtInfo(bool noReturn, bool hasRegParm, unsigned regParm, CallingConv cc,
            bool producesResult, bool noCallerSavedRegs, bool NoCfCheck,
            bool cmseNSCall) {
      assert((!hasRegParm || regParm < 7) && "Invalid regparm value");
      Bits = ((unsigned)cc) | (noReturn ? NoReturnMask : 0) |
             (producesResult ? ProducesResultMask : 0) |
             (noCallerSavedRegs ? NoCallerSavedRegsMask : 0) |
             (hasRegParm ? ((regParm + 1) << RegParmOffset) : 0) |
             (NoCfCheck ? NoCfCheckMask : 0) |
             (cmseNSCall ? CmseNSCallMask : 0);
    }

    // Constructor with all defaults. Use when for example creating a
    // function known to use defaults.
    ExtInfo() = default;

    // Constructor with just the calling convention, which is an important part
    // of the canonical type.
    ExtInfo(CallingConv CC) : Bits(CC) {}

    bool getNoReturn() const { return Bits & NoReturnMask; }
    bool getProducesResult() const { return Bits & ProducesResultMask; }
    bool getCmseNSCall() const { return Bits & CmseNSCallMask; }
    bool getNoCallerSavedRegs() const { return Bits & NoCallerSavedRegsMask; }
    bool getNoCfCheck() const { return Bits & NoCfCheckMask; }
    bool getHasRegParm() const { return ((Bits & RegParmMask) >> RegParmOffset) != 0; }

    unsigned getRegParm() const {
      unsigned RegParm = (Bits & RegParmMask) >> RegParmOffset;
      if (RegParm > 0)
        --RegParm;
      return RegParm;
    }

    CallingConv getCC() const { return CallingConv(Bits & CallConvMask); }

    bool operator==(ExtInfo Other) const {
      return Bits == Other.Bits;
    }
    bool operator!=(ExtInfo Other) const {
      return Bits != Other.Bits;
    }

    // Note that we don't have setters. That is by design, use
    // the following with methods instead of mutating these objects.

    ExtInfo withNoReturn(bool noReturn) const {
      if (noReturn)
        return ExtInfo(Bits | NoReturnMask);
      else
        return ExtInfo(Bits & ~NoReturnMask);
    }

    ExtInfo withProducesResult(bool producesResult) const {
      if (producesResult)
        return ExtInfo(Bits | ProducesResultMask);
      else
        return ExtInfo(Bits & ~ProducesResultMask);
    }

    ExtInfo withCmseNSCall(bool cmseNSCall) const {
      if (cmseNSCall)
        return ExtInfo(Bits | CmseNSCallMask);
      else
        return ExtInfo(Bits & ~CmseNSCallMask);
    }

    ExtInfo withNoCallerSavedRegs(bool noCallerSavedRegs) const {
      if (noCallerSavedRegs)
        return ExtInfo(Bits | NoCallerSavedRegsMask);
      else
        return ExtInfo(Bits & ~NoCallerSavedRegsMask);
    }

    ExtInfo withNoCfCheck(bool noCfCheck) const {
      if (noCfCheck)
        return ExtInfo(Bits | NoCfCheckMask);
      else
        return ExtInfo(Bits & ~NoCfCheckMask);
    }

    ExtInfo withRegParm(unsigned RegParm) const {
      assert(RegParm < 7 && "Invalid regparm value");
      return ExtInfo((Bits & ~RegParmMask) |
                     ((RegParm + 1) << RegParmOffset));
    }

    ExtInfo withCallingConv(CallingConv cc) const {
      return ExtInfo((Bits & ~CallConvMask) | (unsigned) cc);
    }

    void Profile(llvm::FoldingSetNodeID &ID) const {
      ID.AddInteger(Bits);
    }
  };

  /// A simple holder for a QualType representing a type in an
  /// exception specification. Unfortunately needed by FunctionProtoType
  /// because TrailingObjects cannot handle repeated types.
  struct ExceptionType { QualType Type; };

  /// A simple holder for various uncommon bits which do not fit in
  /// FunctionTypeBitfields. Aligned to alignof(void *) to maintain the
  /// alignment of subsequent objects in TrailingObjects.
  struct alignas(void *) FunctionTypeExtraBitfields {
    /// The number of types in the exception specification.
    /// A whole unsigned is not needed here and according to
    /// [implimits] 8 bits would be enough here.
    unsigned NumExceptionType : 10;

    LLVM_PREFERRED_TYPE(bool)
    unsigned HasArmTypeAttributes : 1;

    LLVM_PREFERRED_TYPE(bool)
    unsigned EffectsHaveConditions : 1;
    unsigned NumFunctionEffects : 4;

    FunctionTypeExtraBitfields()
        : NumExceptionType(0), HasArmTypeAttributes(false),
          EffectsHaveConditions(false), NumFunctionEffects(0) {}
  };

  /// The AArch64 SME ACLE (Arm C/C++ Language Extensions) define a number
  /// of function type attributes that can be set on function types, including
  /// function pointers.
  enum AArch64SMETypeAttributes : unsigned {
    SME_NormalFunction = 0,
    SME_PStateSMEnabledMask = 1 << 0,
    SME_PStateSMCompatibleMask = 1 << 1,

    // Describes the value of the state using ArmStateValue.
    SME_ZAShift = 2,
    SME_ZAMask = 0b111 << SME_ZAShift,
    SME_ZT0Shift = 5,
    SME_ZT0Mask = 0b111 << SME_ZT0Shift,

    SME_AttributeMask =
        0b111'111'11 // We can't support more than 8 bits because of
                     // the bitmask in FunctionTypeExtraBitfields.
  };

  enum ArmStateValue : unsigned {
    ARM_None = 0,
    ARM_Preserves = 1,
    ARM_In = 2,
    ARM_Out = 3,
    ARM_InOut = 4,
  };

  static ArmStateValue getArmZAState(unsigned AttrBits) {
    return (ArmStateValue)((AttrBits & SME_ZAMask) >> SME_ZAShift);
  }

  static ArmStateValue getArmZT0State(unsigned AttrBits) {
    return (ArmStateValue)((AttrBits & SME_ZT0Mask) >> SME_ZT0Shift);
  }

  /// A holder for Arm type attributes as described in the Arm C/C++
  /// Language extensions which are not particularly common to all
  /// types and therefore accounted separately from FunctionTypeBitfields.
  struct alignas(void *) FunctionTypeArmAttributes {
    /// Any AArch64 SME ACLE type attributes that need to be propagated
    /// on declarations and function pointers.
    unsigned AArch64SMEAttributes : 8;

    FunctionTypeArmAttributes() : AArch64SMEAttributes(SME_NormalFunction) {}
  };

protected:
  FunctionType(TypeClass tc, QualType res, QualType Canonical,
               TypeDependence Dependence, ExtInfo Info)
      : Type(tc, Canonical, Dependence), ResultType(res) {
    FunctionTypeBits.ExtInfo = Info.Bits;
  }

  Qualifiers getFastTypeQuals() const {
    if (isFunctionProtoType())
      return Qualifiers::fromFastMask(FunctionTypeBits.FastTypeQuals);

    return Qualifiers();
  }

public:
  QualType getReturnType() const { return ResultType; }

  bool getHasRegParm() const { return getExtInfo().getHasRegParm(); }
  unsigned getRegParmType() const { return getExtInfo().getRegParm(); }

  /// Determine whether this function type includes the GNU noreturn
  /// attribute. The C++11 [[noreturn]] attribute does not affect the function
  /// type.
  bool getNoReturnAttr() const { return getExtInfo().getNoReturn(); }

  bool getCmseNSCallAttr() const { return getExtInfo().getCmseNSCall(); }
  CallingConv getCallConv() const { return getExtInfo().getCC(); }
  ExtInfo getExtInfo() const { return ExtInfo(FunctionTypeBits.ExtInfo); }

  static_assert((~Qualifiers::FastMask & Qualifiers::CVRMask) == 0,
                "Const, volatile and restrict are assumed to be a subset of "
                "the fast qualifiers.");

  bool isConst() const { return getFastTypeQuals().hasConst(); }
  bool isVolatile() const { return getFastTypeQuals().hasVolatile(); }
  bool isRestrict() const { return getFastTypeQuals().hasRestrict(); }

  /// Determine the type of an expression that calls a function of
  /// this type.
  QualType getCallResultType(const ASTContext &Context) const {
    return getReturnType().getNonLValueExprType(Context);
  }

  static StringRef getNameForCallConv(CallingConv CC);

  static bool classof(const Type *T) {
    return T->getTypeClass() == FunctionNoProto ||
           T->getTypeClass() == FunctionProto;
  }
};

/// Represents a K&R-style 'int foo()' function, which has
/// no information available about its arguments.
class FunctionNoProtoType : public FunctionType, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  FunctionNoProtoType(QualType Result, QualType Canonical, ExtInfo Info)
      : FunctionType(FunctionNoProto, Result, Canonical,
                     Result->getDependence() &
                         ~(TypeDependence::DependentInstantiation |
                           TypeDependence::UnexpandedPack),
                     Info) {}

public:
  // No additional state past what FunctionType provides.

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getReturnType(), getExtInfo());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ResultType,
                      ExtInfo Info) {
    Info.Profile(ID);
    ID.AddPointer(ResultType.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == FunctionNoProto;
  }
};

// ------------------------------------------------------------------------------

/// Represents an abstract function effect, using just an enumeration describing
/// its kind.
class FunctionEffect {
public:
  /// Identifies the particular effect.
  enum class Kind : uint8_t {
    None = 0,
    NonBlocking = 1,
    NonAllocating = 2,
    Blocking = 3,
    Allocating = 4
  };

  /// Flags describing some behaviors of the effect.
  using Flags = unsigned;
  enum FlagBit : Flags {
    // Can verification inspect callees' implementations? (e.g. nonblocking:
    // yes, tcb+types: no). This also implies the need for 2nd-pass
    // verification.
    FE_InferrableOnCallees = 0x1,

    // Language constructs which effects can diagnose as disallowed.
    FE_ExcludeThrow = 0x2,
    FE_ExcludeCatch = 0x4,
    FE_ExcludeObjCMessageSend = 0x8,
    FE_ExcludeStaticLocalVars = 0x10,
    FE_ExcludeThreadLocalVars = 0x20
  };

private:
  LLVM_PREFERRED_TYPE(Kind)
  unsigned FKind : 3;

  // Expansion: for hypothetical TCB+types, there could be one Kind for TCB,
  // then ~16(?) bits "SubKind" to map to a specific named TCB. SubKind would
  // be considered for uniqueness.

public:
  FunctionEffect() : FKind(unsigned(Kind::None)) {}

  explicit FunctionEffect(Kind K) : FKind(unsigned(K)) {}

  /// The kind of the effect.
  Kind kind() const { return Kind(FKind); }

  /// Return the opposite kind, for effects which have opposites.
  Kind oppositeKind() const;

  /// For serialization.
  uint32_t toOpaqueInt32() const { return FKind; }
  static FunctionEffect fromOpaqueInt32(uint32_t Value) {
    return FunctionEffect(Kind(Value));
  }

  /// Flags describing some behaviors of the effect.
  Flags flags() const {
    switch (kind()) {
    case Kind::NonBlocking:
      return FE_InferrableOnCallees | FE_ExcludeThrow | FE_ExcludeCatch |
             FE_ExcludeObjCMessageSend | FE_ExcludeStaticLocalVars |
             FE_ExcludeThreadLocalVars;
    case Kind::NonAllocating:
      // Same as NonBlocking, except without FE_ExcludeStaticLocalVars.
      return FE_InferrableOnCallees | FE_ExcludeThrow | FE_ExcludeCatch |
             FE_ExcludeObjCMessageSend | FE_ExcludeThreadLocalVars;
    case Kind::Blocking:
    case Kind::Allocating:
      return 0;
    case Kind::None:
      break;
    }
    llvm_unreachable("unknown effect kind");
  }

  /// The description printed in diagnostics, e.g. 'nonblocking'.
  StringRef name() const;

  /// Return true if the effect is allowed to be inferred on the callee,
  /// which is either a FunctionDecl or BlockDecl.
  /// Example: This allows nonblocking(false) to prevent inference for the
  /// function.
  bool canInferOnFunction(const Decl &Callee) const;

  // Return false for success. When true is returned for a direct call, then the
  // FE_InferrableOnCallees flag may trigger inference rather than an immediate
  // diagnostic. Caller should be assumed to have the effect (it may not have it
  // explicitly when inferring).
  bool shouldDiagnoseFunctionCall(bool Direct,
                                  ArrayRef<FunctionEffect> CalleeFX) const;

  friend bool operator==(const FunctionEffect &LHS, const FunctionEffect &RHS) {
    return LHS.FKind == RHS.FKind;
  }
  friend bool operator!=(const FunctionEffect &LHS, const FunctionEffect &RHS) {
    return !(LHS == RHS);
  }
  friend bool operator<(const FunctionEffect &LHS, const FunctionEffect &RHS) {
    return LHS.FKind < RHS.FKind;
  }
};

/// Wrap a function effect's condition expression in another struct so
/// that FunctionProtoType's TrailingObjects can treat it separately.
class EffectConditionExpr {
  Expr *Cond = nullptr; // if null, unconditional.

public:
  EffectConditionExpr() = default;
  EffectConditionExpr(Expr *E) : Cond(E) {}

  Expr *getCondition() const { return Cond; }

  bool operator==(const EffectConditionExpr &RHS) const {
    return Cond == RHS.Cond;
  }
};

/// A FunctionEffect plus a potential boolean expression determining whether
/// the effect is declared (e.g. nonblocking(expr)). Generally the condition
/// expression when present, is dependent.
struct FunctionEffectWithCondition {
  FunctionEffect Effect;
  EffectConditionExpr Cond;

  FunctionEffectWithCondition() = default;
  FunctionEffectWithCondition(const FunctionEffect &E,
                              const EffectConditionExpr &C)
      : Effect(E), Cond(C) {}

  /// Return a textual description of the effect, and its condition, if any.
  std::string description() const;
};

/// Support iteration in parallel through a pair of FunctionEffect and
/// EffectConditionExpr containers.
template <typename Container> class FunctionEffectIterator {
  friend Container;

  const Container *Outer = nullptr;
  size_t Idx = 0;

public:
  FunctionEffectIterator();
  FunctionEffectIterator(const Container &O, size_t I) : Outer(&O), Idx(I) {}
  bool operator==(const FunctionEffectIterator &Other) const {
    return Idx == Other.Idx;
  }
  bool operator!=(const FunctionEffectIterator &Other) const {
    return Idx != Other.Idx;
  }

  FunctionEffectIterator operator++() {
    ++Idx;
    return *this;
  }

  FunctionEffectWithCondition operator*() const {
    assert(Outer != nullptr && "invalid FunctionEffectIterator");
    bool HasConds = !Outer->Conditions.empty();
    return FunctionEffectWithCondition{Outer->Effects[Idx],
                                       HasConds ? Outer->Conditions[Idx]
                                                : EffectConditionExpr()};
  }
};

/// An immutable set of FunctionEffects and possibly conditions attached to
/// them. The effects and conditions reside in memory not managed by this object
/// (typically, trailing objects in FunctionProtoType, or borrowed references
/// from a FunctionEffectSet).
///
/// Invariants:
/// - there is never more than one instance of any given effect.
/// - the array of conditions is either empty or has the same size as the
///   array of effects.
/// - some conditions may be null expressions; each condition pertains to
///   the effect at the same array index.
///
/// Also, if there are any conditions, at least one of those expressions will be
/// dependent, but this is only asserted in the constructor of
/// FunctionProtoType.
///
/// See also FunctionEffectSet, in Sema, which provides a mutable set.
class FunctionEffectsRef {
  // Restrict classes which can call the private constructor -- these friends
  // all maintain the required invariants. FunctionEffectSet is generally the
  // only way in which the arrays are created; FunctionProtoType will not
  // reorder them.
  friend FunctionProtoType;
  friend FunctionEffectSet;

  ArrayRef<FunctionEffect> Effects;
  ArrayRef<EffectConditionExpr> Conditions;

  // The arrays are expected to have been sorted by the caller, with the
  // effects in order. The conditions array must be empty or the same size
  // as the effects array, since the conditions are associated with the effects
  // at the same array indices.
  FunctionEffectsRef(ArrayRef<FunctionEffect> FX,
                     ArrayRef<EffectConditionExpr> Conds)
      : Effects(FX), Conditions(Conds) {}

public:
  /// Extract the effects from a Type if it is a function, block, or member
  /// function pointer, or a reference or pointer to one.
  static FunctionEffectsRef get(QualType QT);

  /// Asserts invariants.
  static FunctionEffectsRef create(ArrayRef<FunctionEffect> FX,
                                   ArrayRef<EffectConditionExpr> Conds);

  FunctionEffectsRef() = default;

  bool empty() const { return Effects.empty(); }
  size_t size() const { return Effects.size(); }

  ArrayRef<FunctionEffect> effects() const { return Effects; }
  ArrayRef<EffectConditionExpr> conditions() const { return Conditions; }

  using iterator = FunctionEffectIterator<FunctionEffectsRef>;
  friend iterator;
  iterator begin() const { return iterator(*this, 0); }
  iterator end() const { return iterator(*this, size()); }

  friend bool operator==(const FunctionEffectsRef &LHS,
                         const FunctionEffectsRef &RHS) {
    return LHS.Effects == RHS.Effects && LHS.Conditions == RHS.Conditions;
  }
  friend bool operator!=(const FunctionEffectsRef &LHS,
                         const FunctionEffectsRef &RHS) {
    return !(LHS == RHS);
  }

  void dump(llvm::raw_ostream &OS) const;
};

/// A mutable set of FunctionEffects and possibly conditions attached to them.
/// Used to compare and merge effects on declarations.
///
/// Has the same invariants as FunctionEffectsRef.
class FunctionEffectSet {
  SmallVector<FunctionEffect> Effects;
  SmallVector<EffectConditionExpr> Conditions;

public:
  FunctionEffectSet() = default;

  explicit FunctionEffectSet(const FunctionEffectsRef &FX)
      : Effects(FX.effects()), Conditions(FX.conditions()) {}

  bool empty() const { return Effects.empty(); }
  size_t size() const { return Effects.size(); }

  using iterator = FunctionEffectIterator<FunctionEffectSet>;
  friend iterator;
  iterator begin() const { return iterator(*this, 0); }
  iterator end() const { return iterator(*this, size()); }

  operator FunctionEffectsRef() const { return {Effects, Conditions}; }

  void dump(llvm::raw_ostream &OS) const;

  // Mutators

  // On insertion, a conflict occurs when attempting to insert an
  // effect which is opposite an effect already in the set, or attempting
  // to insert an effect which is already in the set but with a condition
  // which is not identical.
  struct Conflict {
    FunctionEffectWithCondition Kept;
    FunctionEffectWithCondition Rejected;
  };
  using Conflicts = SmallVector<Conflict>;

  // Returns true for success (obviating a check of Errs.empty()).
  bool insert(const FunctionEffectWithCondition &NewEC, Conflicts &Errs);

  // Returns true for success (obviating a check of Errs.empty()).
  bool insert(const FunctionEffectsRef &Set, Conflicts &Errs);

  // Set operations

  static FunctionEffectSet getUnion(FunctionEffectsRef LHS,
                                    FunctionEffectsRef RHS, Conflicts &Errs);
  static FunctionEffectSet getIntersection(FunctionEffectsRef LHS,
                                           FunctionEffectsRef RHS);
};

/// Represents a prototype with parameter type info, e.g.
/// 'int foo(int)' or 'int foo(void)'.  'void' is represented as having no
/// parameters, not as having a single void parameter. Such a type can have
/// an exception specification, but this specification is not part of the
/// canonical type. FunctionProtoType has several trailing objects, some of
/// which optional. For more information about the trailing objects see
/// the first comment inside FunctionProtoType.
class FunctionProtoType final
    : public FunctionType,
      public llvm::FoldingSetNode,
      private llvm::TrailingObjects<
          FunctionProtoType, QualType, SourceLocation,
          FunctionType::FunctionTypeExtraBitfields,
          FunctionType::FunctionTypeArmAttributes, FunctionType::ExceptionType,
          Expr *, FunctionDecl *, FunctionType::ExtParameterInfo, Qualifiers,
          FunctionEffect, EffectConditionExpr> {
  friend class ASTContext; // ASTContext creates these.
  friend TrailingObjects;

  // FunctionProtoType is followed by several trailing objects, some of
  // which optional. They are in order:
  //
  // * An array of getNumParams() QualType holding the parameter types.
  //   Always present. Note that for the vast majority of FunctionProtoType,
  //   these will be the only trailing objects.
  //
  // * Optionally if the function is variadic, the SourceLocation of the
  //   ellipsis.
  //
  // * Optionally if some extra data is stored in FunctionTypeExtraBitfields
  //   (see FunctionTypeExtraBitfields and FunctionTypeBitfields):
  //   a single FunctionTypeExtraBitfields. Present if and only if
  //   hasExtraBitfields() is true.
  //
  // * Optionally exactly one of:
  //   * an array of getNumExceptions() ExceptionType,
  //   * a single Expr *,
  //   * a pair of FunctionDecl *,
  //   * a single FunctionDecl *
  //   used to store information about the various types of exception
  //   specification. See getExceptionSpecSize for the details.
  //
  // * Optionally an array of getNumParams() ExtParameterInfo holding
  //   an ExtParameterInfo for each of the parameters. Present if and
  //   only if hasExtParameterInfos() is true.
  //
  // * Optionally a Qualifiers object to represent extra qualifiers that can't
  //   be represented by FunctionTypeBitfields.FastTypeQuals. Present if and
  //   only if hasExtQualifiers() is true.
  //
  // * Optionally, an array of getNumFunctionEffects() FunctionEffect.
  //   Present only when getNumFunctionEffects() > 0
  //
  // * Optionally, an array of getNumFunctionEffects() EffectConditionExpr.
  //   Present only when getNumFunctionEffectConditions() > 0.
  //
  // The optional FunctionTypeExtraBitfields has to be before the data
  // related to the exception specification since it contains the number
  // of exception types.
  //
  // We put the ExtParameterInfos later.  If all were equal, it would make
  // more sense to put these before the exception specification, because
  // it's much easier to skip past them compared to the elaborate switch
  // required to skip the exception specification.  However, all is not
  // equal; ExtParameterInfos are used to model very uncommon features,
  // and it's better not to burden the more common paths.

public:
  /// Holds information about the various types of exception specification.
  /// ExceptionSpecInfo is not stored as such in FunctionProtoType but is
  /// used to group together the various bits of information about the
  /// exception specification.
  struct ExceptionSpecInfo {
    /// The kind of exception specification this is.
    ExceptionSpecificationType Type = EST_None;

    /// Explicitly-specified list of exception types.
    ArrayRef<QualType> Exceptions;

    /// Noexcept expression, if this is a computed noexcept specification.
    Expr *NoexceptExpr = nullptr;

    /// The function whose exception specification this is, for
    /// EST_Unevaluated and EST_Uninstantiated.
    FunctionDecl *SourceDecl = nullptr;

    /// The function template whose exception specification this is instantiated
    /// from, for EST_Uninstantiated.
    FunctionDecl *SourceTemplate = nullptr;

    ExceptionSpecInfo() = default;

    ExceptionSpecInfo(ExceptionSpecificationType EST) : Type(EST) {}

    void instantiate();
  };

  /// Extra information about a function prototype. ExtProtoInfo is not
  /// stored as such in FunctionProtoType but is used to group together
  /// the various bits of extra information about a function prototype.
  struct ExtProtoInfo {
    FunctionType::ExtInfo ExtInfo;
    unsigned Variadic : 1;
    unsigned HasTrailingReturn : 1;
    unsigned AArch64SMEAttributes : 8;
    Qualifiers TypeQuals;
    RefQualifierKind RefQualifier = RQ_None;
    ExceptionSpecInfo ExceptionSpec;
    const ExtParameterInfo *ExtParameterInfos = nullptr;
    SourceLocation EllipsisLoc;
    FunctionEffectsRef FunctionEffects;

    ExtProtoInfo()
        : Variadic(false), HasTrailingReturn(false),
          AArch64SMEAttributes(SME_NormalFunction) {}

    ExtProtoInfo(CallingConv CC)
        : ExtInfo(CC), Variadic(false), HasTrailingReturn(false),
          AArch64SMEAttributes(SME_NormalFunction) {}

    ExtProtoInfo withExceptionSpec(const ExceptionSpecInfo &ESI) {
      ExtProtoInfo Result(*this);
      Result.ExceptionSpec = ESI;
      return Result;
    }

    bool requiresFunctionProtoTypeExtraBitfields() const {
      return ExceptionSpec.Type == EST_Dynamic ||
             requiresFunctionProtoTypeArmAttributes() ||
             !FunctionEffects.empty();
    }

    bool requiresFunctionProtoTypeArmAttributes() const {
      return AArch64SMEAttributes != SME_NormalFunction;
    }

    void setArmSMEAttribute(AArch64SMETypeAttributes Kind, bool Enable = true) {
      if (Enable)
        AArch64SMEAttributes |= Kind;
      else
        AArch64SMEAttributes &= ~Kind;
    }
  };

private:
  unsigned numTrailingObjects(OverloadToken<QualType>) const {
    return getNumParams();
  }

  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return isVariadic();
  }

  unsigned numTrailingObjects(OverloadToken<FunctionTypeArmAttributes>) const {
    return hasArmTypeAttributes();
  }

  unsigned numTrailingObjects(OverloadToken<FunctionTypeExtraBitfields>) const {
    return hasExtraBitfields();
  }

  unsigned numTrailingObjects(OverloadToken<ExceptionType>) const {
    return getExceptionSpecSize().NumExceptionType;
  }

  unsigned numTrailingObjects(OverloadToken<Expr *>) const {
    return getExceptionSpecSize().NumExprPtr;
  }

  unsigned numTrailingObjects(OverloadToken<FunctionDecl *>) const {
    return getExceptionSpecSize().NumFunctionDeclPtr;
  }

  unsigned numTrailingObjects(OverloadToken<ExtParameterInfo>) const {
    return hasExtParameterInfos() ? getNumParams() : 0;
  }

  unsigned numTrailingObjects(OverloadToken<Qualifiers>) const {
    return hasExtQualifiers() ? 1 : 0;
  }

  unsigned numTrailingObjects(OverloadToken<FunctionEffect>) const {
    return getNumFunctionEffects();
  }

  unsigned numTrailingObjects(OverloadToken<EffectConditionExpr>) const {
    return getNumFunctionEffectConditions();
  }

  /// Determine whether there are any argument types that
  /// contain an unexpanded parameter pack.
  static bool containsAnyUnexpandedParameterPack(const QualType *ArgArray,
                                                 unsigned numArgs) {
    for (unsigned Idx = 0; Idx < numArgs; ++Idx)
      if (ArgArray[Idx]->containsUnexpandedParameterPack())
        return true;

    return false;
  }

  FunctionProtoType(QualType result, ArrayRef<QualType> params,
                    QualType canonical, const ExtProtoInfo &epi);

  /// This struct is returned by getExceptionSpecSize and is used to
  /// translate an ExceptionSpecificationType to the number and kind
  /// of trailing objects related to the exception specification.
  struct ExceptionSpecSizeHolder {
    unsigned NumExceptionType;
    unsigned NumExprPtr;
    unsigned NumFunctionDeclPtr;
  };

  /// Return the number and kind of trailing objects
  /// related to the exception specification.
  static ExceptionSpecSizeHolder
  getExceptionSpecSize(ExceptionSpecificationType EST, unsigned NumExceptions) {
    switch (EST) {
    case EST_None:
    case EST_DynamicNone:
    case EST_MSAny:
    case EST_BasicNoexcept:
    case EST_Unparsed:
    case EST_NoThrow:
      return {0, 0, 0};

    case EST_Dynamic:
      return {NumExceptions, 0, 0};

    case EST_DependentNoexcept:
    case EST_NoexceptFalse:
    case EST_NoexceptTrue:
      return {0, 1, 0};

    case EST_Uninstantiated:
      return {0, 0, 2};

    case EST_Unevaluated:
      return {0, 0, 1};
    }
    llvm_unreachable("bad exception specification kind");
  }

  /// Return the number and kind of trailing objects
  /// related to the exception specification.
  ExceptionSpecSizeHolder getExceptionSpecSize() const {
    return getExceptionSpecSize(getExceptionSpecType(), getNumExceptions());
  }

  /// Whether the trailing FunctionTypeExtraBitfields is present.
  bool hasExtraBitfields() const {
    assert((getExceptionSpecType() != EST_Dynamic ||
            FunctionTypeBits.HasExtraBitfields) &&
           "ExtraBitfields are required for given ExceptionSpecType");
    return FunctionTypeBits.HasExtraBitfields;

  }

  bool hasArmTypeAttributes() const {
    return FunctionTypeBits.HasExtraBitfields &&
           getTrailingObjects<FunctionTypeExtraBitfields>()
               ->HasArmTypeAttributes;
  }

  bool hasExtQualifiers() const {
    return FunctionTypeBits.HasExtQuals;
  }

public:
  unsigned getNumParams() const { return FunctionTypeBits.NumParams; }

  QualType getParamType(unsigned i) const {
    assert(i < getNumParams() && "invalid parameter index");
    return param_type_begin()[i];
  }

  ArrayRef<QualType> getParamTypes() const {
    return llvm::ArrayRef(param_type_begin(), param_type_end());
  }

  ExtProtoInfo getExtProtoInfo() const {
    ExtProtoInfo EPI;
    EPI.ExtInfo = getExtInfo();
    EPI.Variadic = isVariadic();
    EPI.EllipsisLoc = getEllipsisLoc();
    EPI.HasTrailingReturn = hasTrailingReturn();
    EPI.ExceptionSpec = getExceptionSpecInfo();
    EPI.TypeQuals = getMethodQuals();
    EPI.RefQualifier = getRefQualifier();
    EPI.ExtParameterInfos = getExtParameterInfosOrNull();
    EPI.AArch64SMEAttributes = getAArch64SMEAttributes();
    EPI.FunctionEffects = getFunctionEffects();
    return EPI;
  }

  /// Get the kind of exception specification on this function.
  ExceptionSpecificationType getExceptionSpecType() const {
    return static_cast<ExceptionSpecificationType>(
        FunctionTypeBits.ExceptionSpecType);
  }

  /// Return whether this function has any kind of exception spec.
  bool hasExceptionSpec() const { return getExceptionSpecType() != EST_None; }

  /// Return whether this function has a dynamic (throw) exception spec.
  bool hasDynamicExceptionSpec() const {
    return isDynamicExceptionSpec(getExceptionSpecType());
  }

  /// Return whether this function has a noexcept exception spec.
  bool hasNoexceptExceptionSpec() const {
    return isNoexceptExceptionSpec(getExceptionSpecType());
  }

  /// Return whether this function has a dependent exception spec.
  bool hasDependentExceptionSpec() const;

  /// Return whether this function has an instantiation-dependent exception
  /// spec.
  bool hasInstantiationDependentExceptionSpec() const;

  /// Return all the available information about this type's exception spec.
  ExceptionSpecInfo getExceptionSpecInfo() const {
    ExceptionSpecInfo Result;
    Result.Type = getExceptionSpecType();
    if (Result.Type == EST_Dynamic) {
      Result.Exceptions = exceptions();
    } else if (isComputedNoexcept(Result.Type)) {
      Result.NoexceptExpr = getNoexceptExpr();
    } else if (Result.Type == EST_Uninstantiated) {
      Result.SourceDecl = getExceptionSpecDecl();
      Result.SourceTemplate = getExceptionSpecTemplate();
    } else if (Result.Type == EST_Unevaluated) {
      Result.SourceDecl = getExceptionSpecDecl();
    }
    return Result;
  }

  /// Return the number of types in the exception specification.
  unsigned getNumExceptions() const {
    return getExceptionSpecType() == EST_Dynamic
               ? getTrailingObjects<FunctionTypeExtraBitfields>()
                     ->NumExceptionType
               : 0;
  }

  /// Return the ith exception type, where 0 <= i < getNumExceptions().
  QualType getExceptionType(unsigned i) const {
    assert(i < getNumExceptions() && "Invalid exception number!");
    return exception_begin()[i];
  }

  /// Return the expression inside noexcept(expression), or a null pointer
  /// if there is none (because the exception spec is not of this form).
  Expr *getNoexceptExpr() const {
    if (!isComputedNoexcept(getExceptionSpecType()))
      return nullptr;
    return *getTrailingObjects<Expr *>();
  }

  /// If this function type has an exception specification which hasn't
  /// been determined yet (either because it has not been evaluated or because
  /// it has not been instantiated), this is the function whose exception
  /// specification is represented by this type.
  FunctionDecl *getExceptionSpecDecl() const {
    if (getExceptionSpecType() != EST_Uninstantiated &&
        getExceptionSpecType() != EST_Unevaluated)
      return nullptr;
    return getTrailingObjects<FunctionDecl *>()[0];
  }

  /// If this function type has an uninstantiated exception
  /// specification, this is the function whose exception specification
  /// should be instantiated to find the exception specification for
  /// this type.
  FunctionDecl *getExceptionSpecTemplate() const {
    if (getExceptionSpecType() != EST_Uninstantiated)
      return nullptr;
    return getTrailingObjects<FunctionDecl *>()[1];
  }

  /// Determine whether this function type has a non-throwing exception
  /// specification.
  CanThrowResult canThrow() const;

  /// Determine whether this function type has a non-throwing exception
  /// specification. If this depends on template arguments, returns
  /// \c ResultIfDependent.
  bool isNothrow(bool ResultIfDependent = false) const {
    return ResultIfDependent ? canThrow() != CT_Can : canThrow() == CT_Cannot;
  }

  /// Whether this function prototype is variadic.
  bool isVariadic() const { return FunctionTypeBits.Variadic; }

  SourceLocation getEllipsisLoc() const {
    return isVariadic() ? *getTrailingObjects<SourceLocation>()
                        : SourceLocation();
  }

  /// Determines whether this function prototype contains a
  /// parameter pack at the end.
  ///
  /// A function template whose last parameter is a parameter pack can be
  /// called with an arbitrary number of arguments, much like a variadic
  /// function.
  bool isTemplateVariadic() const;

  /// Whether this function prototype has a trailing return type.
  bool hasTrailingReturn() const { return FunctionTypeBits.HasTrailingReturn; }

  Qualifiers getMethodQuals() const {
    if (hasExtQualifiers())
      return *getTrailingObjects<Qualifiers>();
    else
      return getFastTypeQuals();
  }

  /// Retrieve the ref-qualifier associated with this function type.
  RefQualifierKind getRefQualifier() const {
    return static_cast<RefQualifierKind>(FunctionTypeBits.RefQualifier);
  }

  using param_type_iterator = const QualType *;

  ArrayRef<QualType> param_types() const {
    return llvm::ArrayRef(param_type_begin(), param_type_end());
  }

  param_type_iterator param_type_begin() const {
    return getTrailingObjects<QualType>();
  }

  param_type_iterator param_type_end() const {
    return param_type_begin() + getNumParams();
  }

  using exception_iterator = const QualType *;

  ArrayRef<QualType> exceptions() const {
    return llvm::ArrayRef(exception_begin(), exception_end());
  }

  exception_iterator exception_begin() const {
    return reinterpret_cast<exception_iterator>(
        getTrailingObjects<ExceptionType>());
  }

  exception_iterator exception_end() const {
    return exception_begin() + getNumExceptions();
  }

  /// Is there any interesting extra information for any of the parameters
  /// of this function type?
  bool hasExtParameterInfos() const {
    return FunctionTypeBits.HasExtParameterInfos;
  }

  ArrayRef<ExtParameterInfo> getExtParameterInfos() const {
    assert(hasExtParameterInfos());
    return ArrayRef<ExtParameterInfo>(getTrailingObjects<ExtParameterInfo>(),
                                      getNumParams());
  }

  /// Return a pointer to the beginning of the array of extra parameter
  /// information, if present, or else null if none of the parameters
  /// carry it.  This is equivalent to getExtProtoInfo().ExtParameterInfos.
  const ExtParameterInfo *getExtParameterInfosOrNull() const {
    if (!hasExtParameterInfos())
      return nullptr;
    return getTrailingObjects<ExtParameterInfo>();
  }

  /// Return a bitmask describing the SME attributes on the function type, see
  /// AArch64SMETypeAttributes for their values.
  unsigned getAArch64SMEAttributes() const {
    if (!hasArmTypeAttributes())
      return SME_NormalFunction;
    return getTrailingObjects<FunctionTypeArmAttributes>()
        ->AArch64SMEAttributes;
  }

  ExtParameterInfo getExtParameterInfo(unsigned I) const {
    assert(I < getNumParams() && "parameter index out of range");
    if (hasExtParameterInfos())
      return getTrailingObjects<ExtParameterInfo>()[I];
    return ExtParameterInfo();
  }

  ParameterABI getParameterABI(unsigned I) const {
    assert(I < getNumParams() && "parameter index out of range");
    if (hasExtParameterInfos())
      return getTrailingObjects<ExtParameterInfo>()[I].getABI();
    return ParameterABI::Ordinary;
  }

  bool isParamConsumed(unsigned I) const {
    assert(I < getNumParams() && "parameter index out of range");
    if (hasExtParameterInfos())
      return getTrailingObjects<ExtParameterInfo>()[I].isConsumed();
    return false;
  }

  unsigned getNumFunctionEffects() const {
    return hasExtraBitfields()
               ? getTrailingObjects<FunctionTypeExtraBitfields>()
                     ->NumFunctionEffects
               : 0;
  }

  // For serialization.
  ArrayRef<FunctionEffect> getFunctionEffectsWithoutConditions() const {
    if (hasExtraBitfields()) {
      const auto *Bitfields = getTrailingObjects<FunctionTypeExtraBitfields>();
      if (Bitfields->NumFunctionEffects > 0)
        return {getTrailingObjects<FunctionEffect>(),
                Bitfields->NumFunctionEffects};
    }
    return {};
  }

  unsigned getNumFunctionEffectConditions() const {
    if (hasExtraBitfields()) {
      const auto *Bitfields = getTrailingObjects<FunctionTypeExtraBitfields>();
      if (Bitfields->EffectsHaveConditions)
        return Bitfields->NumFunctionEffects;
    }
    return 0;
  }

  // For serialization.
  ArrayRef<EffectConditionExpr> getFunctionEffectConditions() const {
    if (hasExtraBitfields()) {
      const auto *Bitfields = getTrailingObjects<FunctionTypeExtraBitfields>();
      if (Bitfields->EffectsHaveConditions)
        return {getTrailingObjects<EffectConditionExpr>(),
                Bitfields->NumFunctionEffects};
    }
    return {};
  }

  // Combines effects with their conditions.
  FunctionEffectsRef getFunctionEffects() const {
    if (hasExtraBitfields()) {
      const auto *Bitfields = getTrailingObjects<FunctionTypeExtraBitfields>();
      if (Bitfields->NumFunctionEffects > 0) {
        const size_t NumConds = Bitfields->EffectsHaveConditions
                                    ? Bitfields->NumFunctionEffects
                                    : 0;
        return FunctionEffectsRef(
            {getTrailingObjects<FunctionEffect>(),
             Bitfields->NumFunctionEffects},
            {NumConds ? getTrailingObjects<EffectConditionExpr>() : nullptr,
             NumConds});
      }
    }
    return {};
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void printExceptionSpecification(raw_ostream &OS,
                                   const PrintingPolicy &Policy) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == FunctionProto;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Ctx);
  static void Profile(llvm::FoldingSetNodeID &ID, QualType Result,
                      param_type_iterator ArgTys, unsigned NumArgs,
                      const ExtProtoInfo &EPI, const ASTContext &Context,
                      bool Canonical);
};

/// Represents the dependent type named by a dependently-scoped
/// typename using declaration, e.g.
///   using typename Base<T>::foo;
///
/// Template instantiation turns these into the underlying type.
class UnresolvedUsingType : public Type {
  friend class ASTContext; // ASTContext creates these.

  UnresolvedUsingTypenameDecl *Decl;

  UnresolvedUsingType(const UnresolvedUsingTypenameDecl *D)
      : Type(UnresolvedUsing, QualType(),
             TypeDependence::DependentInstantiation),
        Decl(const_cast<UnresolvedUsingTypenameDecl *>(D)) {}

public:
  UnresolvedUsingTypenameDecl *getDecl() const { return Decl; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == UnresolvedUsing;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    return Profile(ID, Decl);
  }

  static void Profile(llvm::FoldingSetNodeID &ID,
                      UnresolvedUsingTypenameDecl *D) {
    ID.AddPointer(D);
  }
};

class UsingType final : public Type,
                        public llvm::FoldingSetNode,
                        private llvm::TrailingObjects<UsingType, QualType> {
  UsingShadowDecl *Found;
  friend class ASTContext; // ASTContext creates these.
  friend TrailingObjects;

  UsingType(const UsingShadowDecl *Found, QualType Underlying, QualType Canon);

public:
  UsingShadowDecl *getFoundDecl() const { return Found; }
  QualType getUnderlyingType() const;

  bool isSugared() const { return true; }

  // This always has the 'same' type as declared, but not necessarily identical.
  QualType desugar() const { return getUnderlyingType(); }

  // Internal helper, for debugging purposes.
  bool typeMatchesDecl() const { return !UsingBits.hasTypeDifferentFromDecl; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, Found, getUnderlyingType());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, const UsingShadowDecl *Found,
                      QualType Underlying) {
    ID.AddPointer(Found);
    Underlying.Profile(ID);
  }
  static bool classof(const Type *T) { return T->getTypeClass() == Using; }
};

class TypedefType final : public Type,
                          public llvm::FoldingSetNode,
                          private llvm::TrailingObjects<TypedefType, QualType> {
  TypedefNameDecl *Decl;
  friend class ASTContext; // ASTContext creates these.
  friend TrailingObjects;

  TypedefType(TypeClass tc, const TypedefNameDecl *D, QualType underlying,
              QualType can);

public:
  TypedefNameDecl *getDecl() const { return Decl; }

  bool isSugared() const { return true; }

  // This always has the 'same' type as declared, but not necessarily identical.
  QualType desugar() const;

  // Internal helper, for debugging purposes.
  bool typeMatchesDecl() const { return !TypedefBits.hasTypeDifferentFromDecl; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, Decl, typeMatchesDecl() ? QualType() : desugar());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, const TypedefNameDecl *Decl,
                      QualType Underlying) {
    ID.AddPointer(Decl);
    if (!Underlying.isNull())
      Underlying.Profile(ID);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Typedef; }
};

/// Sugar type that represents a type that was qualified by a qualifier written
/// as a macro invocation.
class MacroQualifiedType : public Type {
  friend class ASTContext; // ASTContext creates these.

  QualType UnderlyingTy;
  const IdentifierInfo *MacroII;

  MacroQualifiedType(QualType UnderlyingTy, QualType CanonTy,
                     const IdentifierInfo *MacroII)
      : Type(MacroQualified, CanonTy, UnderlyingTy->getDependence()),
        UnderlyingTy(UnderlyingTy), MacroII(MacroII) {
    assert(isa<AttributedType>(UnderlyingTy) &&
           "Expected a macro qualified type to only wrap attributed types.");
  }

public:
  const IdentifierInfo *getMacroIdentifier() const { return MacroII; }
  QualType getUnderlyingType() const { return UnderlyingTy; }

  /// Return this attributed type's modified type with no qualifiers attached to
  /// it.
  QualType getModifiedType() const;

  bool isSugared() const { return true; }
  QualType desugar() const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == MacroQualified;
  }
};

/// Represents a `typeof` (or __typeof__) expression (a C23 feature and GCC
/// extension) or a `typeof_unqual` expression (a C23 feature).
class TypeOfExprType : public Type {
  Expr *TOExpr;
  const ASTContext &Context;

protected:
  friend class ASTContext; // ASTContext creates these.

  TypeOfExprType(const ASTContext &Context, Expr *E, TypeOfKind Kind,
                 QualType Can = QualType());

public:
  Expr *getUnderlyingExpr() const { return TOExpr; }

  /// Returns the kind of 'typeof' type this is.
  TypeOfKind getKind() const {
    return static_cast<TypeOfKind>(TypeOfBits.Kind);
  }

  /// Remove a single level of sugar.
  QualType desugar() const;

  /// Returns whether this type directly provides sugar.
  bool isSugared() const;

  static bool classof(const Type *T) { return T->getTypeClass() == TypeOfExpr; }
};

/// Internal representation of canonical, dependent
/// `typeof(expr)` types.
///
/// This class is used internally by the ASTContext to manage
/// canonical, dependent types, only. Clients will only see instances
/// of this class via TypeOfExprType nodes.
class DependentTypeOfExprType : public TypeOfExprType,
                                public llvm::FoldingSetNode {
public:
  DependentTypeOfExprType(const ASTContext &Context, Expr *E, TypeOfKind Kind)
      : TypeOfExprType(Context, E, Kind) {}

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getUnderlyingExpr(),
            getKind() == TypeOfKind::Unqualified);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      Expr *E, bool IsUnqual);
};

/// Represents `typeof(type)`, a C23 feature and GCC extension, or
/// `typeof_unqual(type), a C23 feature.
class TypeOfType : public Type {
  friend class ASTContext; // ASTContext creates these.

  QualType TOType;
  const ASTContext &Context;

  TypeOfType(const ASTContext &Context, QualType T, QualType Can,
             TypeOfKind Kind);

public:
  QualType getUnmodifiedType() const { return TOType; }

  /// Remove a single level of sugar.
  QualType desugar() const;

  /// Returns whether this type directly provides sugar.
  bool isSugared() const { return true; }

  /// Returns the kind of 'typeof' type this is.
  TypeOfKind getKind() const {
    return static_cast<TypeOfKind>(TypeOfBits.Kind);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == TypeOf; }
};

/// Represents the type `decltype(expr)` (C++11).
class DecltypeType : public Type {
  Expr *E;
  QualType UnderlyingType;

protected:
  friend class ASTContext; // ASTContext creates these.

  DecltypeType(Expr *E, QualType underlyingType, QualType can = QualType());

public:
  Expr *getUnderlyingExpr() const { return E; }
  QualType getUnderlyingType() const { return UnderlyingType; }

  /// Remove a single level of sugar.
  QualType desugar() const;

  /// Returns whether this type directly provides sugar.
  bool isSugared() const;

  static bool classof(const Type *T) { return T->getTypeClass() == Decltype; }
};

/// Internal representation of canonical, dependent
/// decltype(expr) types.
///
/// This class is used internally by the ASTContext to manage
/// canonical, dependent types, only. Clients will only see instances
/// of this class via DecltypeType nodes.
class DependentDecltypeType : public DecltypeType, public llvm::FoldingSetNode {
public:
  DependentDecltypeType(Expr *E, QualType UnderlyingTpe);

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getUnderlyingExpr());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      Expr *E);
};

class PackIndexingType final
    : public Type,
      public llvm::FoldingSetNode,
      private llvm::TrailingObjects<PackIndexingType, QualType> {
  friend TrailingObjects;

  const ASTContext &Context;
  QualType Pattern;
  Expr *IndexExpr;

  unsigned Size;

protected:
  friend class ASTContext; // ASTContext creates these.
  PackIndexingType(const ASTContext &Context, QualType Canonical,
                   QualType Pattern, Expr *IndexExpr,
                   ArrayRef<QualType> Expansions = {});

public:
  Expr *getIndexExpr() const { return IndexExpr; }
  QualType getPattern() const { return Pattern; }

  bool isSugared() const { return hasSelectedType(); }

  QualType desugar() const {
    if (hasSelectedType())
      return getSelectedType();
    return QualType(this, 0);
  }

  QualType getSelectedType() const {
    assert(hasSelectedType() && "Type is dependant");
    return *(getExpansionsPtr() + *getSelectedIndex());
  }

  std::optional<unsigned> getSelectedIndex() const;

  bool hasSelectedType() const { return getSelectedIndex() != std::nullopt; }

  ArrayRef<QualType> getExpansions() const {
    return {getExpansionsPtr(), Size};
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == PackIndexing;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    if (hasSelectedType())
      getSelectedType().Profile(ID);
    else
      Profile(ID, Context, getPattern(), getIndexExpr());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      QualType Pattern, Expr *E);

private:
  const QualType *getExpansionsPtr() const {
    return getTrailingObjects<QualType>();
  }

  static TypeDependence computeDependence(QualType Pattern, Expr *IndexExpr,
                                          ArrayRef<QualType> Expansions = {});

  unsigned numTrailingObjects(OverloadToken<QualType>) const { return Size; }
};

/// A unary type transform, which is a type constructed from another.
class UnaryTransformType : public Type {
public:
  enum UTTKind {
#define TRANSFORM_TYPE_TRAIT_DEF(Enum, _) Enum,
#include "clang/Basic/TransformTypeTraits.def"
  };

private:
  /// The untransformed type.
  QualType BaseType;

  /// The transformed type if not dependent, otherwise the same as BaseType.
  QualType UnderlyingType;

  UTTKind UKind;

protected:
  friend class ASTContext;

  UnaryTransformType(QualType BaseTy, QualType UnderlyingTy, UTTKind UKind,
                     QualType CanonicalTy);

public:
  bool isSugared() const { return !isDependentType(); }
  QualType desugar() const { return UnderlyingType; }

  QualType getUnderlyingType() const { return UnderlyingType; }
  QualType getBaseType() const { return BaseType; }

  UTTKind getUTTKind() const { return UKind; }

  static bool classof(const Type *T) {
    return T->getTypeClass() == UnaryTransform;
  }
};

/// Internal representation of canonical, dependent
/// __underlying_type(type) types.
///
/// This class is used internally by the ASTContext to manage
/// canonical, dependent types, only. Clients will only see instances
/// of this class via UnaryTransformType nodes.
class DependentUnaryTransformType : public UnaryTransformType,
                                    public llvm::FoldingSetNode {
public:
  DependentUnaryTransformType(const ASTContext &C, QualType BaseType,
                              UTTKind UKind);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getBaseType(), getUTTKind());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType BaseType,
                      UTTKind UKind) {
    ID.AddPointer(BaseType.getAsOpaquePtr());
    ID.AddInteger((unsigned)UKind);
  }
};

class TagType : public Type {
  friend class ASTReader;
  template <class T> friend class serialization::AbstractTypeReader;

  /// Stores the TagDecl associated with this type. The decl may point to any
  /// TagDecl that declares the entity.
  TagDecl *decl;

protected:
  TagType(TypeClass TC, const TagDecl *D, QualType can);

public:
  TagDecl *getDecl() const;

  /// Determines whether this type is in the process of being defined.
  bool isBeingDefined() const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == Enum || T->getTypeClass() == Record;
  }
};

/// A helper class that allows the use of isa/cast/dyncast
/// to detect TagType objects of structs/unions/classes.
class RecordType : public TagType {
protected:
  friend class ASTContext; // ASTContext creates these.

  explicit RecordType(const RecordDecl *D)
      : TagType(Record, reinterpret_cast<const TagDecl*>(D), QualType()) {}
  explicit RecordType(TypeClass TC, RecordDecl *D)
      : TagType(TC, reinterpret_cast<const TagDecl*>(D), QualType()) {}

public:
  RecordDecl *getDecl() const {
    return reinterpret_cast<RecordDecl*>(TagType::getDecl());
  }

  /// Recursively check all fields in the record for const-ness. If any field
  /// is declared const, return true. Otherwise, return false.
  bool hasConstFields() const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) { return T->getTypeClass() == Record; }
};

/// A helper class that allows the use of isa/cast/dyncast
/// to detect TagType objects of enums.
class EnumType : public TagType {
  friend class ASTContext; // ASTContext creates these.

  explicit EnumType(const EnumDecl *D)
      : TagType(Enum, reinterpret_cast<const TagDecl*>(D), QualType()) {}

public:
  EnumDecl *getDecl() const {
    return reinterpret_cast<EnumDecl*>(TagType::getDecl());
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) { return T->getTypeClass() == Enum; }
};

/// An attributed type is a type to which a type attribute has been applied.
///
/// The "modified type" is the fully-sugared type to which the attributed
/// type was applied; generally it is not canonically equivalent to the
/// attributed type. The "equivalent type" is the minimally-desugared type
/// which the type is canonically equivalent to.
///
/// For example, in the following attributed type:
///     int32_t __attribute__((vector_size(16)))
///   - the modified type is the TypedefType for int32_t
///   - the equivalent type is VectorType(16, int32_t)
///   - the canonical type is VectorType(16, int)
class AttributedType : public Type, public llvm::FoldingSetNode {
public:
  using Kind = attr::Kind;

private:
  friend class ASTContext; // ASTContext creates these

  QualType ModifiedType;
  QualType EquivalentType;

  AttributedType(QualType canon, attr::Kind attrKind, QualType modified,
                 QualType equivalent)
      : Type(Attributed, canon, equivalent->getDependence()),
        ModifiedType(modified), EquivalentType(equivalent) {
    AttributedTypeBits.AttrKind = attrKind;
  }

public:
  Kind getAttrKind() const {
    return static_cast<Kind>(AttributedTypeBits.AttrKind);
  }

  QualType getModifiedType() const { return ModifiedType; }
  QualType getEquivalentType() const { return EquivalentType; }

  bool isSugared() const { return true; }
  QualType desugar() const { return getEquivalentType(); }

  /// Does this attribute behave like a type qualifier?
  ///
  /// A type qualifier adjusts a type to provide specialized rules for
  /// a specific object, like the standard const and volatile qualifiers.
  /// This includes attributes controlling things like nullability,
  /// address spaces, and ARC ownership.  The value of the object is still
  /// largely described by the modified type.
  ///
  /// In contrast, many type attributes "rewrite" their modified type to
  /// produce a fundamentally different type, not necessarily related in any
  /// formalizable way to the original type.  For example, calling convention
  /// and vector attributes are not simple type qualifiers.
  ///
  /// Type qualifiers are often, but not always, reflected in the canonical
  /// type.
  bool isQualifier() const;

  bool isMSTypeSpec() const;

  bool isWebAssemblyFuncrefSpec() const;

  bool isCallingConv() const;

  std::optional<NullabilityKind> getImmediateNullability() const;

  /// Retrieve the attribute kind corresponding to the given
  /// nullability kind.
  static Kind getNullabilityAttrKind(NullabilityKind kind) {
    switch (kind) {
    case NullabilityKind::NonNull:
      return attr::TypeNonNull;

    case NullabilityKind::Nullable:
      return attr::TypeNullable;

    case NullabilityKind::NullableResult:
      return attr::TypeNullableResult;

    case NullabilityKind::Unspecified:
      return attr::TypeNullUnspecified;
    }
    llvm_unreachable("Unknown nullability kind.");
  }

  /// Strip off the top-level nullability annotation on the given
  /// type, if it's there.
  ///
  /// \param T The type to strip. If the type is exactly an
  /// AttributedType specifying nullability (without looking through
  /// type sugar), the nullability is returned and this type changed
  /// to the underlying modified type.
  ///
  /// \returns the top-level nullability, if present.
  static std::optional<NullabilityKind> stripOuterNullability(QualType &T);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getAttrKind(), ModifiedType, EquivalentType);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, Kind attrKind,
                      QualType modified, QualType equivalent) {
    ID.AddInteger(attrKind);
    ID.AddPointer(modified.getAsOpaquePtr());
    ID.AddPointer(equivalent.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Attributed;
  }
};

class BTFTagAttributedType : public Type, public llvm::FoldingSetNode {
private:
  friend class ASTContext; // ASTContext creates these

  QualType WrappedType;
  const BTFTypeTagAttr *BTFAttr;

  BTFTagAttributedType(QualType Canon, QualType Wrapped,
                       const BTFTypeTagAttr *BTFAttr)
      : Type(BTFTagAttributed, Canon, Wrapped->getDependence()),
        WrappedType(Wrapped), BTFAttr(BTFAttr) {}

public:
  QualType getWrappedType() const { return WrappedType; }
  const BTFTypeTagAttr *getAttr() const { return BTFAttr; }

  bool isSugared() const { return true; }
  QualType desugar() const { return getWrappedType(); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, WrappedType, BTFAttr);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Wrapped,
                      const BTFTypeTagAttr *BTFAttr) {
    ID.AddPointer(Wrapped.getAsOpaquePtr());
    ID.AddPointer(BTFAttr);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == BTFTagAttributed;
  }
};

class TemplateTypeParmType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these

  // Helper data collector for canonical types.
  struct CanonicalTTPTInfo {
    unsigned Depth : 15;
    unsigned ParameterPack : 1;
    unsigned Index : 16;
  };

  union {
    // Info for the canonical type.
    CanonicalTTPTInfo CanTTPTInfo;

    // Info for the non-canonical type.
    TemplateTypeParmDecl *TTPDecl;
  };

  /// Build a non-canonical type.
  TemplateTypeParmType(TemplateTypeParmDecl *TTPDecl, QualType Canon)
      : Type(TemplateTypeParm, Canon,
             TypeDependence::DependentInstantiation |
                 (Canon->getDependence() & TypeDependence::UnexpandedPack)),
        TTPDecl(TTPDecl) {}

  /// Build the canonical type.
  TemplateTypeParmType(unsigned D, unsigned I, bool PP)
      : Type(TemplateTypeParm, QualType(this, 0),
             TypeDependence::DependentInstantiation |
                 (PP ? TypeDependence::UnexpandedPack : TypeDependence::None)) {
    CanTTPTInfo.Depth = D;
    CanTTPTInfo.Index = I;
    CanTTPTInfo.ParameterPack = PP;
  }

  const CanonicalTTPTInfo& getCanTTPTInfo() const {
    QualType Can = getCanonicalTypeInternal();
    return Can->castAs<TemplateTypeParmType>()->CanTTPTInfo;
  }

public:
  unsigned getDepth() const { return getCanTTPTInfo().Depth; }
  unsigned getIndex() const { return getCanTTPTInfo().Index; }
  bool isParameterPack() const { return getCanTTPTInfo().ParameterPack; }

  TemplateTypeParmDecl *getDecl() const {
    return isCanonicalUnqualified() ? nullptr : TTPDecl;
  }

  IdentifierInfo *getIdentifier() const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getDepth(), getIndex(), isParameterPack(), getDecl());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, unsigned Depth,
                      unsigned Index, bool ParameterPack,
                      TemplateTypeParmDecl *TTPDecl) {
    ID.AddInteger(Depth);
    ID.AddInteger(Index);
    ID.AddBoolean(ParameterPack);
    ID.AddPointer(TTPDecl);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == TemplateTypeParm;
  }
};

/// Represents the result of substituting a type for a template
/// type parameter.
///
/// Within an instantiated template, all template type parameters have
/// been replaced with these.  They are used solely to record that a
/// type was originally written as a template type parameter;
/// therefore they are never canonical.
class SubstTemplateTypeParmType final
    : public Type,
      public llvm::FoldingSetNode,
      private llvm::TrailingObjects<SubstTemplateTypeParmType, QualType> {
  friend class ASTContext;
  friend class llvm::TrailingObjects<SubstTemplateTypeParmType, QualType>;

  Decl *AssociatedDecl;

  SubstTemplateTypeParmType(QualType Replacement, Decl *AssociatedDecl,
                            unsigned Index, std::optional<unsigned> PackIndex);

public:
  /// Gets the type that was substituted for the template
  /// parameter.
  QualType getReplacementType() const {
    return SubstTemplateTypeParmTypeBits.HasNonCanonicalUnderlyingType
               ? *getTrailingObjects<QualType>()
               : getCanonicalTypeInternal();
  }

  /// A template-like entity which owns the whole pattern being substituted.
  /// This will usually own a set of template parameters, or in some
  /// cases might even be a template parameter itself.
  Decl *getAssociatedDecl() const { return AssociatedDecl; }

  /// Gets the template parameter declaration that was substituted for.
  const TemplateTypeParmDecl *getReplacedParameter() const;

  /// Returns the index of the replaced parameter in the associated declaration.
  /// This should match the result of `getReplacedParameter()->getIndex()`.
  unsigned getIndex() const { return SubstTemplateTypeParmTypeBits.Index; }

  std::optional<unsigned> getPackIndex() const {
    if (SubstTemplateTypeParmTypeBits.PackIndex == 0)
      return std::nullopt;
    return SubstTemplateTypeParmTypeBits.PackIndex - 1;
  }

  bool isSugared() const { return true; }
  QualType desugar() const { return getReplacementType(); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getReplacementType(), getAssociatedDecl(), getIndex(),
            getPackIndex());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Replacement,
                      const Decl *AssociatedDecl, unsigned Index,
                      std::optional<unsigned> PackIndex) {
    Replacement.Profile(ID);
    ID.AddPointer(AssociatedDecl);
    ID.AddInteger(Index);
    ID.AddInteger(PackIndex ? *PackIndex - 1 : 0);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == SubstTemplateTypeParm;
  }
};

/// Represents the result of substituting a set of types for a template
/// type parameter pack.
///
/// When a pack expansion in the source code contains multiple parameter packs
/// and those parameter packs correspond to different levels of template
/// parameter lists, this type node is used to represent a template type
/// parameter pack from an outer level, which has already had its argument pack
/// substituted but that still lives within a pack expansion that itself
/// could not be instantiated. When actually performing a substitution into
/// that pack expansion (e.g., when all template parameters have corresponding
/// arguments), this type will be replaced with the \c SubstTemplateTypeParmType
/// at the current pack substitution index.
class SubstTemplateTypeParmPackType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext;

  /// A pointer to the set of template arguments that this
  /// parameter pack is instantiated with.
  const TemplateArgument *Arguments;

  llvm::PointerIntPair<Decl *, 1, bool> AssociatedDeclAndFinal;

  SubstTemplateTypeParmPackType(QualType Canon, Decl *AssociatedDecl,
                                unsigned Index, bool Final,
                                const TemplateArgument &ArgPack);

public:
  IdentifierInfo *getIdentifier() const;

  /// A template-like entity which owns the whole pattern being substituted.
  /// This will usually own a set of template parameters, or in some
  /// cases might even be a template parameter itself.
  Decl *getAssociatedDecl() const;

  /// Gets the template parameter declaration that was substituted for.
  const TemplateTypeParmDecl *getReplacedParameter() const;

  /// Returns the index of the replaced parameter in the associated declaration.
  /// This should match the result of `getReplacedParameter()->getIndex()`.
  unsigned getIndex() const { return SubstTemplateTypeParmPackTypeBits.Index; }

  // When true the substitution will be 'Final' (subst node won't be placed).
  bool getFinal() const;

  unsigned getNumArgs() const {
    return SubstTemplateTypeParmPackTypeBits.NumArgs;
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  TemplateArgument getArgumentPack() const;

  void Profile(llvm::FoldingSetNodeID &ID);
  static void Profile(llvm::FoldingSetNodeID &ID, const Decl *AssociatedDecl,
                      unsigned Index, bool Final,
                      const TemplateArgument &ArgPack);

  static bool classof(const Type *T) {
    return T->getTypeClass() == SubstTemplateTypeParmPack;
  }
};

/// Common base class for placeholders for types that get replaced by
/// placeholder type deduction: C++11 auto, C++14 decltype(auto), C++17 deduced
/// class template types, and constrained type names.
///
/// These types are usually a placeholder for a deduced type. However, before
/// the initializer is attached, or (usually) if the initializer is
/// type-dependent, there is no deduced type and the type is canonical. In
/// the latter case, it is also a dependent type.
class DeducedType : public Type {
  QualType DeducedAsType;

protected:
  DeducedType(TypeClass TC, QualType DeducedAsType,
              TypeDependence ExtraDependence, QualType Canon)
      : Type(TC, Canon,
             ExtraDependence | (DeducedAsType.isNull()
                                    ? TypeDependence::None
                                    : DeducedAsType->getDependence() &
                                          ~TypeDependence::VariablyModified)),
        DeducedAsType(DeducedAsType) {}

public:
  bool isSugared() const { return !DeducedAsType.isNull(); }
  QualType desugar() const {
    return isSugared() ? DeducedAsType : QualType(this, 0);
  }

  /// Get the type deduced for this placeholder type, or null if it
  /// has not been deduced.
  QualType getDeducedType() const { return DeducedAsType; }
  bool isDeduced() const {
    return !DeducedAsType.isNull() || isDependentType();
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Auto ||
           T->getTypeClass() == DeducedTemplateSpecialization;
  }
};

/// Represents a C++11 auto or C++14 decltype(auto) type, possibly constrained
/// by a type-constraint.
class AutoType : public DeducedType, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these

  ConceptDecl *TypeConstraintConcept;

  AutoType(QualType DeducedAsType, AutoTypeKeyword Keyword,
           TypeDependence ExtraDependence, QualType Canon, ConceptDecl *CD,
           ArrayRef<TemplateArgument> TypeConstraintArgs);

public:
  ArrayRef<TemplateArgument> getTypeConstraintArguments() const {
    return {reinterpret_cast<const TemplateArgument *>(this + 1),
            AutoTypeBits.NumArgs};
  }

  ConceptDecl *getTypeConstraintConcept() const {
    return TypeConstraintConcept;
  }

  bool isConstrained() const {
    return TypeConstraintConcept != nullptr;
  }

  bool isDecltypeAuto() const {
    return getKeyword() == AutoTypeKeyword::DecltypeAuto;
  }

  bool isGNUAutoType() const {
    return getKeyword() == AutoTypeKeyword::GNUAutoType;
  }

  AutoTypeKeyword getKeyword() const {
    return (AutoTypeKeyword)AutoTypeBits.Keyword;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context);
  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      QualType Deduced, AutoTypeKeyword Keyword,
                      bool IsDependent, ConceptDecl *CD,
                      ArrayRef<TemplateArgument> Arguments);

  static bool classof(const Type *T) {
    return T->getTypeClass() == Auto;
  }
};

/// Represents a C++17 deduced template specialization type.
class DeducedTemplateSpecializationType : public DeducedType,
                                          public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these

  /// The name of the template whose arguments will be deduced.
  TemplateName Template;

  DeducedTemplateSpecializationType(TemplateName Template,
                                    QualType DeducedAsType,
                                    bool IsDeducedAsDependent)
      : DeducedType(DeducedTemplateSpecialization, DeducedAsType,
                    toTypeDependence(Template.getDependence()) |
                        (IsDeducedAsDependent
                             ? TypeDependence::DependentInstantiation
                             : TypeDependence::None),
                    DeducedAsType.isNull() ? QualType(this, 0)
                                           : DeducedAsType.getCanonicalType()),
        Template(Template) {}

public:
  /// Retrieve the name of the template that we are deducing.
  TemplateName getTemplateName() const { return Template;}

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getTemplateName(), getDeducedType(), isDependentType());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, TemplateName Template,
                      QualType Deduced, bool IsDependent) {
    Template.Profile(ID);
    QualType CanonicalType =
        Deduced.isNull() ? Deduced : Deduced.getCanonicalType();
    ID.AddPointer(CanonicalType.getAsOpaquePtr());
    ID.AddBoolean(IsDependent || Template.isDependent());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == DeducedTemplateSpecialization;
  }
};

/// Represents a type template specialization; the template
/// must be a class template, a type alias template, or a template
/// template parameter.  A template which cannot be resolved to one of
/// these, e.g. because it is written with a dependent scope
/// specifier, is instead represented as a
/// @c DependentTemplateSpecializationType.
///
/// A non-dependent template specialization type is always "sugar",
/// typically for a \c RecordType.  For example, a class template
/// specialization type of \c vector<int> will refer to a tag type for
/// the instantiation \c std::vector<int, std::allocator<int>>
///
/// Template specializations are dependent if either the template or
/// any of the template arguments are dependent, in which case the
/// type may also be canonical.
///
/// Instances of this type are allocated with a trailing array of
/// TemplateArguments, followed by a QualType representing the
/// non-canonical aliased type when the template is a type alias
/// template.
class TemplateSpecializationType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these

  /// The name of the template being specialized.  This is
  /// either a TemplateName::Template (in which case it is a
  /// ClassTemplateDecl*, a TemplateTemplateParmDecl*, or a
  /// TypeAliasTemplateDecl*), a
  /// TemplateName::SubstTemplateTemplateParmPack, or a
  /// TemplateName::SubstTemplateTemplateParm (in which case the
  /// replacement must, recursively, be one of these).
  TemplateName Template;

  TemplateSpecializationType(TemplateName T,
                             ArrayRef<TemplateArgument> Args,
                             QualType Canon,
                             QualType Aliased);

public:
  /// Determine whether any of the given template arguments are dependent.
  ///
  /// The converted arguments should be supplied when known; whether an
  /// argument is dependent can depend on the conversions performed on it
  /// (for example, a 'const int' passed as a template argument might be
  /// dependent if the parameter is a reference but non-dependent if the
  /// parameter is an int).
  ///
  /// Note that the \p Args parameter is unused: this is intentional, to remind
  /// the caller that they need to pass in the converted arguments, not the
  /// specified arguments.
  static bool
  anyDependentTemplateArguments(ArrayRef<TemplateArgumentLoc> Args,
                                ArrayRef<TemplateArgument> Converted);
  static bool
  anyDependentTemplateArguments(const TemplateArgumentListInfo &,
                                ArrayRef<TemplateArgument> Converted);
  static bool anyInstantiationDependentTemplateArguments(
      ArrayRef<TemplateArgumentLoc> Args);

  /// True if this template specialization type matches a current
  /// instantiation in the context in which it is found.
  bool isCurrentInstantiation() const {
    return isa<InjectedClassNameType>(getCanonicalTypeInternal());
  }

  /// Determine if this template specialization type is for a type alias
  /// template that has been substituted.
  ///
  /// Nearly every template specialization type whose template is an alias
  /// template will be substituted. However, this is not the case when
  /// the specialization contains a pack expansion but the template alias
  /// does not have a corresponding parameter pack, e.g.,
  ///
  /// \code
  /// template<typename T, typename U, typename V> struct S;
  /// template<typename T, typename U> using A = S<T, int, U>;
  /// template<typename... Ts> struct X {
  ///   typedef A<Ts...> type; // not a type alias
  /// };
  /// \endcode
  bool isTypeAlias() const { return TemplateSpecializationTypeBits.TypeAlias; }

  /// Get the aliased type, if this is a specialization of a type alias
  /// template.
  QualType getAliasedType() const;

  /// Retrieve the name of the template that we are specializing.
  TemplateName getTemplateName() const { return Template; }

  ArrayRef<TemplateArgument> template_arguments() const {
    return {reinterpret_cast<const TemplateArgument *>(this + 1),
            TemplateSpecializationTypeBits.NumArgs};
  }

  bool isSugared() const {
    return !isDependentType() || isCurrentInstantiation() || isTypeAlias();
  }

  QualType desugar() const {
    return isTypeAlias() ? getAliasedType() : getCanonicalTypeInternal();
  }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Ctx);
  static void Profile(llvm::FoldingSetNodeID &ID, TemplateName T,
                      ArrayRef<TemplateArgument> Args,
                      const ASTContext &Context);

  static bool classof(const Type *T) {
    return T->getTypeClass() == TemplateSpecialization;
  }
};

/// Print a template argument list, including the '<' and '>'
/// enclosing the template arguments.
void printTemplateArgumentList(raw_ostream &OS,
                               ArrayRef<TemplateArgument> Args,
                               const PrintingPolicy &Policy,
                               const TemplateParameterList *TPL = nullptr);

void printTemplateArgumentList(raw_ostream &OS,
                               ArrayRef<TemplateArgumentLoc> Args,
                               const PrintingPolicy &Policy,
                               const TemplateParameterList *TPL = nullptr);

void printTemplateArgumentList(raw_ostream &OS,
                               const TemplateArgumentListInfo &Args,
                               const PrintingPolicy &Policy,
                               const TemplateParameterList *TPL = nullptr);

/// Make a best-effort determination of whether the type T can be produced by
/// substituting Args into the default argument of Param.
bool isSubstitutedDefaultArgument(ASTContext &Ctx, TemplateArgument Arg,
                                  const NamedDecl *Param,
                                  ArrayRef<TemplateArgument> Args,
                                  unsigned Depth);

/// The injected class name of a C++ class template or class
/// template partial specialization.  Used to record that a type was
/// spelled with a bare identifier rather than as a template-id; the
/// equivalent for non-templated classes is just RecordType.
///
/// Injected class name types are always dependent.  Template
/// instantiation turns these into RecordTypes.
///
/// Injected class name types are always canonical.  This works
/// because it is impossible to compare an injected class name type
/// with the corresponding non-injected template type, for the same
/// reason that it is impossible to directly compare template
/// parameters from different dependent contexts: injected class name
/// types can only occur within the scope of a particular templated
/// declaration, and within that scope every template specialization
/// will canonicalize to the injected class name (when appropriate
/// according to the rules of the language).
class InjectedClassNameType : public Type {
  friend class ASTContext; // ASTContext creates these.
  friend class ASTNodeImporter;
  friend class ASTReader; // FIXME: ASTContext::getInjectedClassNameType is not
                          // currently suitable for AST reading, too much
                          // interdependencies.
  template <class T> friend class serialization::AbstractTypeReader;

  CXXRecordDecl *Decl;

  /// The template specialization which this type represents.
  /// For example, in
  ///   template <class T> class A { ... };
  /// this is A<T>, whereas in
  ///   template <class X, class Y> class A<B<X,Y> > { ... };
  /// this is A<B<X,Y> >.
  ///
  /// It is always unqualified, always a template specialization type,
  /// and always dependent.
  QualType InjectedType;

  InjectedClassNameType(CXXRecordDecl *D, QualType TST)
      : Type(InjectedClassName, QualType(),
             TypeDependence::DependentInstantiation),
        Decl(D), InjectedType(TST) {
    assert(isa<TemplateSpecializationType>(TST));
    assert(!TST.hasQualifiers());
    assert(TST->isDependentType());
  }

public:
  QualType getInjectedSpecializationType() const { return InjectedType; }

  const TemplateSpecializationType *getInjectedTST() const {
    return cast<TemplateSpecializationType>(InjectedType.getTypePtr());
  }

  TemplateName getTemplateName() const {
    return getInjectedTST()->getTemplateName();
  }

  CXXRecordDecl *getDecl() const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == InjectedClassName;
  }
};

/// The elaboration keyword that precedes a qualified type name or
/// introduces an elaborated-type-specifier.
enum class ElaboratedTypeKeyword {
  /// The "struct" keyword introduces the elaborated-type-specifier.
  Struct,

  /// The "__interface" keyword introduces the elaborated-type-specifier.
  Interface,

  /// The "union" keyword introduces the elaborated-type-specifier.
  Union,

  /// The "class" keyword introduces the elaborated-type-specifier.
  Class,

  /// The "enum" keyword introduces the elaborated-type-specifier.
  Enum,

  /// The "typename" keyword precedes the qualified type name, e.g.,
  /// \c typename T::type.
  Typename,

  /// No keyword precedes the qualified type name.
  None
};

/// The kind of a tag type.
enum class TagTypeKind {
  /// The "struct" keyword.
  Struct,

  /// The "__interface" keyword.
  Interface,

  /// The "union" keyword.
  Union,

  /// The "class" keyword.
  Class,

  /// The "enum" keyword.
  Enum
};

/// A helper class for Type nodes having an ElaboratedTypeKeyword.
/// The keyword in stored in the free bits of the base class.
/// Also provides a few static helpers for converting and printing
/// elaborated type keyword and tag type kind enumerations.
class TypeWithKeyword : public Type {
protected:
  TypeWithKeyword(ElaboratedTypeKeyword Keyword, TypeClass tc,
                  QualType Canonical, TypeDependence Dependence)
      : Type(tc, Canonical, Dependence) {
    TypeWithKeywordBits.Keyword = llvm::to_underlying(Keyword);
  }

public:
  ElaboratedTypeKeyword getKeyword() const {
    return static_cast<ElaboratedTypeKeyword>(TypeWithKeywordBits.Keyword);
  }

  /// Converts a type specifier (DeclSpec::TST) into an elaborated type keyword.
  static ElaboratedTypeKeyword getKeywordForTypeSpec(unsigned TypeSpec);

  /// Converts a type specifier (DeclSpec::TST) into a tag type kind.
  /// It is an error to provide a type specifier which *isn't* a tag kind here.
  static TagTypeKind getTagTypeKindForTypeSpec(unsigned TypeSpec);

  /// Converts a TagTypeKind into an elaborated type keyword.
  static ElaboratedTypeKeyword getKeywordForTagTypeKind(TagTypeKind Tag);

  /// Converts an elaborated type keyword into a TagTypeKind.
  /// It is an error to provide an elaborated type keyword
  /// which *isn't* a tag kind here.
  static TagTypeKind getTagTypeKindForKeyword(ElaboratedTypeKeyword Keyword);

  static bool KeywordIsTagTypeKind(ElaboratedTypeKeyword Keyword);

  static StringRef getKeywordName(ElaboratedTypeKeyword Keyword);

  static StringRef getTagTypeKindName(TagTypeKind Kind) {
    return getKeywordName(getKeywordForTagTypeKind(Kind));
  }

  class CannotCastToThisType {};
  static CannotCastToThisType classof(const Type *);
};

/// Represents a type that was referred to using an elaborated type
/// keyword, e.g., struct S, or via a qualified name, e.g., N::M::type,
/// or both.
///
/// This type is used to keep track of a type name as written in the
/// source code, including tag keywords and any nested-name-specifiers.
/// The type itself is always "sugar", used to express what was written
/// in the source code but containing no additional semantic information.
class ElaboratedType final
    : public TypeWithKeyword,
      public llvm::FoldingSetNode,
      private llvm::TrailingObjects<ElaboratedType, TagDecl *> {
  friend class ASTContext; // ASTContext creates these
  friend TrailingObjects;

  /// The nested name specifier containing the qualifier.
  NestedNameSpecifier *NNS;

  /// The type that this qualified name refers to.
  QualType NamedType;

  /// The (re)declaration of this tag type owned by this occurrence is stored
  /// as a trailing object if there is one. Use getOwnedTagDecl to obtain
  /// it, or obtain a null pointer if there is none.

  ElaboratedType(ElaboratedTypeKeyword Keyword, NestedNameSpecifier *NNS,
                 QualType NamedType, QualType CanonType, TagDecl *OwnedTagDecl)
      : TypeWithKeyword(Keyword, Elaborated, CanonType,
                        // Any semantic dependence on the qualifier will have
                        // been incorporated into NamedType. We still need to
                        // track syntactic (instantiation / error / pack)
                        // dependence on the qualifier.
                        NamedType->getDependence() |
                            (NNS ? toSyntacticDependence(
                                       toTypeDependence(NNS->getDependence()))
                                 : TypeDependence::None)),
        NNS(NNS), NamedType(NamedType) {
    ElaboratedTypeBits.HasOwnedTagDecl = false;
    if (OwnedTagDecl) {
      ElaboratedTypeBits.HasOwnedTagDecl = true;
      *getTrailingObjects<TagDecl *>() = OwnedTagDecl;
    }
  }

public:
  /// Retrieve the qualification on this type.
  NestedNameSpecifier *getQualifier() const { return NNS; }

  /// Retrieve the type named by the qualified-id.
  QualType getNamedType() const { return NamedType; }

  /// Remove a single level of sugar.
  QualType desugar() const { return getNamedType(); }

  /// Returns whether this type directly provides sugar.
  bool isSugared() const { return true; }

  /// Return the (re)declaration of this type owned by this occurrence of this
  /// type, or nullptr if there is none.
  TagDecl *getOwnedTagDecl() const {
    return ElaboratedTypeBits.HasOwnedTagDecl ? *getTrailingObjects<TagDecl *>()
                                              : nullptr;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getKeyword(), NNS, NamedType, getOwnedTagDecl());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, ElaboratedTypeKeyword Keyword,
                      NestedNameSpecifier *NNS, QualType NamedType,
                      TagDecl *OwnedTagDecl) {
    ID.AddInteger(llvm::to_underlying(Keyword));
    ID.AddPointer(NNS);
    NamedType.Profile(ID);
    ID.AddPointer(OwnedTagDecl);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Elaborated; }
};

/// Represents a qualified type name for which the type name is
/// dependent.
///
/// DependentNameType represents a class of dependent types that involve a
/// possibly dependent nested-name-specifier (e.g., "T::") followed by a
/// name of a type. The DependentNameType may start with a "typename" (for a
/// typename-specifier), "class", "struct", "union", or "enum" (for a
/// dependent elaborated-type-specifier), or nothing (in contexts where we
/// know that we must be referring to a type, e.g., in a base class specifier).
/// Typically the nested-name-specifier is dependent, but in MSVC compatibility
/// mode, this type is used with non-dependent names to delay name lookup until
/// instantiation.
class DependentNameType : public TypeWithKeyword, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these

  /// The nested name specifier containing the qualifier.
  NestedNameSpecifier *NNS;

  /// The type that this typename specifier refers to.
  const IdentifierInfo *Name;

  DependentNameType(ElaboratedTypeKeyword Keyword, NestedNameSpecifier *NNS,
                    const IdentifierInfo *Name, QualType CanonType)
      : TypeWithKeyword(Keyword, DependentName, CanonType,
                        TypeDependence::DependentInstantiation |
                            toTypeDependence(NNS->getDependence())),
        NNS(NNS), Name(Name) {}

public:
  /// Retrieve the qualification on this type.
  NestedNameSpecifier *getQualifier() const { return NNS; }

  /// Retrieve the type named by the typename specifier as an identifier.
  ///
  /// This routine will return a non-NULL identifier pointer when the
  /// form of the original typename was terminated by an identifier,
  /// e.g., "typename T::type".
  const IdentifierInfo *getIdentifier() const {
    return Name;
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getKeyword(), NNS, Name);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, ElaboratedTypeKeyword Keyword,
                      NestedNameSpecifier *NNS, const IdentifierInfo *Name) {
    ID.AddInteger(llvm::to_underlying(Keyword));
    ID.AddPointer(NNS);
    ID.AddPointer(Name);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentName;
  }
};

/// Represents a template specialization type whose template cannot be
/// resolved, e.g.
///   A<T>::template B<T>
class DependentTemplateSpecializationType : public TypeWithKeyword,
                                            public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these

  /// The nested name specifier containing the qualifier.
  NestedNameSpecifier *NNS;

  /// The identifier of the template.
  const IdentifierInfo *Name;

  DependentTemplateSpecializationType(ElaboratedTypeKeyword Keyword,
                                      NestedNameSpecifier *NNS,
                                      const IdentifierInfo *Name,
                                      ArrayRef<TemplateArgument> Args,
                                      QualType Canon);

public:
  NestedNameSpecifier *getQualifier() const { return NNS; }
  const IdentifierInfo *getIdentifier() const { return Name; }

  ArrayRef<TemplateArgument> template_arguments() const {
    return {reinterpret_cast<const TemplateArgument *>(this + 1),
            DependentTemplateSpecializationTypeBits.NumArgs};
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, getKeyword(), NNS, Name, template_arguments());
  }

  static void Profile(llvm::FoldingSetNodeID &ID,
                      const ASTContext &Context,
                      ElaboratedTypeKeyword Keyword,
                      NestedNameSpecifier *Qualifier,
                      const IdentifierInfo *Name,
                      ArrayRef<TemplateArgument> Args);

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentTemplateSpecialization;
  }
};

/// Represents a pack expansion of types.
///
/// Pack expansions are part of C++11 variadic templates. A pack
/// expansion contains a pattern, which itself contains one or more
/// "unexpanded" parameter packs. When instantiated, a pack expansion
/// produces a series of types, each instantiated from the pattern of
/// the expansion, where the Ith instantiation of the pattern uses the
/// Ith arguments bound to each of the unexpanded parameter packs. The
/// pack expansion is considered to "expand" these unexpanded
/// parameter packs.
///
/// \code
/// template<typename ...Types> struct tuple;
///
/// template<typename ...Types>
/// struct tuple_of_references {
///   typedef tuple<Types&...> type;
/// };
/// \endcode
///
/// Here, the pack expansion \c Types&... is represented via a
/// PackExpansionType whose pattern is Types&.
class PackExpansionType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these

  /// The pattern of the pack expansion.
  QualType Pattern;

  PackExpansionType(QualType Pattern, QualType Canon,
                    std::optional<unsigned> NumExpansions)
      : Type(PackExpansion, Canon,
             (Pattern->getDependence() | TypeDependence::Dependent |
              TypeDependence::Instantiation) &
                 ~TypeDependence::UnexpandedPack),
        Pattern(Pattern) {
    PackExpansionTypeBits.NumExpansions =
        NumExpansions ? *NumExpansions + 1 : 0;
  }

public:
  /// Retrieve the pattern of this pack expansion, which is the
  /// type that will be repeatedly instantiated when instantiating the
  /// pack expansion itself.
  QualType getPattern() const { return Pattern; }

  /// Retrieve the number of expansions that this pack expansion will
  /// generate, if known.
  std::optional<unsigned> getNumExpansions() const {
    if (PackExpansionTypeBits.NumExpansions)
      return PackExpansionTypeBits.NumExpansions - 1;
    return std::nullopt;
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getPattern(), getNumExpansions());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Pattern,
                      std::optional<unsigned> NumExpansions) {
    ID.AddPointer(Pattern.getAsOpaquePtr());
    ID.AddBoolean(NumExpansions.has_value());
    if (NumExpansions)
      ID.AddInteger(*NumExpansions);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == PackExpansion;
  }
};

/// This class wraps the list of protocol qualifiers. For types that can
/// take ObjC protocol qualifers, they can subclass this class.
template <class T>
class ObjCProtocolQualifiers {
protected:
  ObjCProtocolQualifiers() = default;

  ObjCProtocolDecl * const *getProtocolStorage() const {
    return const_cast<ObjCProtocolQualifiers*>(this)->getProtocolStorage();
  }

  ObjCProtocolDecl **getProtocolStorage() {
    return static_cast<T*>(this)->getProtocolStorageImpl();
  }

  void setNumProtocols(unsigned N) {
    static_cast<T*>(this)->setNumProtocolsImpl(N);
  }

  void initialize(ArrayRef<ObjCProtocolDecl *> protocols) {
    setNumProtocols(protocols.size());
    assert(getNumProtocols() == protocols.size() &&
           "bitfield overflow in protocol count");
    if (!protocols.empty())
      memcpy(getProtocolStorage(), protocols.data(),
             protocols.size() * sizeof(ObjCProtocolDecl*));
  }

public:
  using qual_iterator = ObjCProtocolDecl * const *;
  using qual_range = llvm::iterator_range<qual_iterator>;

  qual_range quals() const { return qual_range(qual_begin(), qual_end()); }
  qual_iterator qual_begin() const { return getProtocolStorage(); }
  qual_iterator qual_end() const { return qual_begin() + getNumProtocols(); }

  bool qual_empty() const { return getNumProtocols() == 0; }

  /// Return the number of qualifying protocols in this type, or 0 if
  /// there are none.
  unsigned getNumProtocols() const {
    return static_cast<const T*>(this)->getNumProtocolsImpl();
  }

  /// Fetch a protocol by index.
  ObjCProtocolDecl *getProtocol(unsigned I) const {
    assert(I < getNumProtocols() && "Out-of-range protocol access");
    return qual_begin()[I];
  }

  /// Retrieve all of the protocol qualifiers.
  ArrayRef<ObjCProtocolDecl *> getProtocols() const {
    return ArrayRef<ObjCProtocolDecl *>(qual_begin(), getNumProtocols());
  }
};

/// Represents a type parameter type in Objective C. It can take
/// a list of protocols.
class ObjCTypeParamType : public Type,
                          public ObjCProtocolQualifiers<ObjCTypeParamType>,
                          public llvm::FoldingSetNode {
  friend class ASTContext;
  friend class ObjCProtocolQualifiers<ObjCTypeParamType>;

  /// The number of protocols stored on this type.
  unsigned NumProtocols : 6;

  ObjCTypeParamDecl *OTPDecl;

  /// The protocols are stored after the ObjCTypeParamType node. In the
  /// canonical type, the list of protocols are sorted alphabetically
  /// and uniqued.
  ObjCProtocolDecl **getProtocolStorageImpl();

  /// Return the number of qualifying protocols in this interface type,
  /// or 0 if there are none.
  unsigned getNumProtocolsImpl() const {
    return NumProtocols;
  }

  void setNumProtocolsImpl(unsigned N) {
    NumProtocols = N;
  }

  ObjCTypeParamType(const ObjCTypeParamDecl *D,
                    QualType can,
                    ArrayRef<ObjCProtocolDecl *> protocols);

public:
  bool isSugared() const { return true; }
  QualType desugar() const { return getCanonicalTypeInternal(); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ObjCTypeParam;
  }

  void Profile(llvm::FoldingSetNodeID &ID);
  static void Profile(llvm::FoldingSetNodeID &ID,
                      const ObjCTypeParamDecl *OTPDecl,
                      QualType CanonicalType,
                      ArrayRef<ObjCProtocolDecl *> protocols);

  ObjCTypeParamDecl *getDecl() const { return OTPDecl; }
};

/// Represents a class type in Objective C.
///
/// Every Objective C type is a combination of a base type, a set of
/// type arguments (optional, for parameterized classes) and a list of
/// protocols.
///
/// Given the following declarations:
/// \code
///   \@class C<T>;
///   \@protocol P;
/// \endcode
///
/// 'C' is an ObjCInterfaceType C.  It is sugar for an ObjCObjectType
/// with base C and no protocols.
///
/// 'C<P>' is an unspecialized ObjCObjectType with base C and protocol list [P].
/// 'C<C*>' is a specialized ObjCObjectType with type arguments 'C*' and no
/// protocol list.
/// 'C<C*><P>' is a specialized ObjCObjectType with base C, type arguments 'C*',
/// and protocol list [P].
///
/// 'id' is a TypedefType which is sugar for an ObjCObjectPointerType whose
/// pointee is an ObjCObjectType with base BuiltinType::ObjCIdType
/// and no protocols.
///
/// 'id<P>' is an ObjCObjectPointerType whose pointee is an ObjCObjectType
/// with base BuiltinType::ObjCIdType and protocol list [P].  Eventually
/// this should get its own sugar class to better represent the source.
class ObjCObjectType : public Type,
                       public ObjCProtocolQualifiers<ObjCObjectType> {
  friend class ObjCProtocolQualifiers<ObjCObjectType>;

  // ObjCObjectType.NumTypeArgs - the number of type arguments stored
  // after the ObjCObjectPointerType node.
  // ObjCObjectType.NumProtocols - the number of protocols stored
  // after the type arguments of ObjCObjectPointerType node.
  //
  // These protocols are those written directly on the type.  If
  // protocol qualifiers ever become additive, the iterators will need
  // to get kindof complicated.
  //
  // In the canonical object type, these are sorted alphabetically
  // and uniqued.

  /// Either a BuiltinType or an InterfaceType or sugar for either.
  QualType BaseType;

  /// Cached superclass type.
  mutable llvm::PointerIntPair<const ObjCObjectType *, 1, bool>
    CachedSuperClassType;

  QualType *getTypeArgStorage();
  const QualType *getTypeArgStorage() const {
    return const_cast<ObjCObjectType *>(this)->getTypeArgStorage();
  }

  ObjCProtocolDecl **getProtocolStorageImpl();
  /// Return the number of qualifying protocols in this interface type,
  /// or 0 if there are none.
  unsigned getNumProtocolsImpl() const {
    return ObjCObjectTypeBits.NumProtocols;
  }
  void setNumProtocolsImpl(unsigned N) {
    ObjCObjectTypeBits.NumProtocols = N;
  }

protected:
  enum Nonce_ObjCInterface { Nonce_ObjCInterface };

  ObjCObjectType(QualType Canonical, QualType Base,
                 ArrayRef<QualType> typeArgs,
                 ArrayRef<ObjCProtocolDecl *> protocols,
                 bool isKindOf);

  ObjCObjectType(enum Nonce_ObjCInterface)
      : Type(ObjCInterface, QualType(), TypeDependence::None),
        BaseType(QualType(this_(), 0)) {
    ObjCObjectTypeBits.NumProtocols = 0;
    ObjCObjectTypeBits.NumTypeArgs = 0;
    ObjCObjectTypeBits.IsKindOf = 0;
  }

  void computeSuperClassTypeSlow() const;

public:
  /// Gets the base type of this object type.  This is always (possibly
  /// sugar for) one of:
  ///  - the 'id' builtin type (as opposed to the 'id' type visible to the
  ///    user, which is a typedef for an ObjCObjectPointerType)
  ///  - the 'Class' builtin type (same caveat)
  ///  - an ObjCObjectType (currently always an ObjCInterfaceType)
  QualType getBaseType() const { return BaseType; }

  bool isObjCId() const {
    return getBaseType()->isSpecificBuiltinType(BuiltinType::ObjCId);
  }

  bool isObjCClass() const {
    return getBaseType()->isSpecificBuiltinType(BuiltinType::ObjCClass);
  }

  bool isObjCUnqualifiedId() const { return qual_empty() && isObjCId(); }
  bool isObjCUnqualifiedClass() const { return qual_empty() && isObjCClass(); }
  bool isObjCUnqualifiedIdOrClass() const {
    if (!qual_empty()) return false;
    if (const BuiltinType *T = getBaseType()->getAs<BuiltinType>())
      return T->getKind() == BuiltinType::ObjCId ||
             T->getKind() == BuiltinType::ObjCClass;
    return false;
  }
  bool isObjCQualifiedId() const { return !qual_empty() && isObjCId(); }
  bool isObjCQualifiedClass() const { return !qual_empty() && isObjCClass(); }

  /// Gets the interface declaration for this object type, if the base type
  /// really is an interface.
  ObjCInterfaceDecl *getInterface() const;

  /// Determine whether this object type is "specialized", meaning
  /// that it has type arguments.
  bool isSpecialized() const;

  /// Determine whether this object type was written with type arguments.
  bool isSpecializedAsWritten() const {
    return ObjCObjectTypeBits.NumTypeArgs > 0;
  }

  /// Determine whether this object type is "unspecialized", meaning
  /// that it has no type arguments.
  bool isUnspecialized() const { return !isSpecialized(); }

  /// Determine whether this object type is "unspecialized" as
  /// written, meaning that it has no type arguments.
  bool isUnspecializedAsWritten() const { return !isSpecializedAsWritten(); }

  /// Retrieve the type arguments of this object type (semantically).
  ArrayRef<QualType> getTypeArgs() const;

  /// Retrieve the type arguments of this object type as they were
  /// written.
  ArrayRef<QualType> getTypeArgsAsWritten() const {
    return llvm::ArrayRef(getTypeArgStorage(), ObjCObjectTypeBits.NumTypeArgs);
  }

  /// Whether this is a "__kindof" type as written.
  bool isKindOfTypeAsWritten() const { return ObjCObjectTypeBits.IsKindOf; }

  /// Whether this ia a "__kindof" type (semantically).
  bool isKindOfType() const;

  /// Retrieve the type of the superclass of this object type.
  ///
  /// This operation substitutes any type arguments into the
  /// superclass of the current class type, potentially producing a
  /// specialization of the superclass type. Produces a null type if
  /// there is no superclass.
  QualType getSuperClassType() const {
    if (!CachedSuperClassType.getInt())
      computeSuperClassTypeSlow();

    assert(CachedSuperClassType.getInt() && "Superclass not set?");
    return QualType(CachedSuperClassType.getPointer(), 0);
  }

  /// Strip off the Objective-C "kindof" type and (with it) any
  /// protocol qualifiers.
  QualType stripObjCKindOfTypeAndQuals(const ASTContext &ctx) const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ObjCObject ||
           T->getTypeClass() == ObjCInterface;
  }
};

/// A class providing a concrete implementation
/// of ObjCObjectType, so as to not increase the footprint of
/// ObjCInterfaceType.  Code outside of ASTContext and the core type
/// system should not reference this type.
class ObjCObjectTypeImpl : public ObjCObjectType, public llvm::FoldingSetNode {
  friend class ASTContext;

  // If anyone adds fields here, ObjCObjectType::getProtocolStorage()
  // will need to be modified.

  ObjCObjectTypeImpl(QualType Canonical, QualType Base,
                     ArrayRef<QualType> typeArgs,
                     ArrayRef<ObjCProtocolDecl *> protocols,
                     bool isKindOf)
      : ObjCObjectType(Canonical, Base, typeArgs, protocols, isKindOf) {}

public:
  void Profile(llvm::FoldingSetNodeID &ID);
  static void Profile(llvm::FoldingSetNodeID &ID,
                      QualType Base,
                      ArrayRef<QualType> typeArgs,
                      ArrayRef<ObjCProtocolDecl *> protocols,
                      bool isKindOf);
};

inline QualType *ObjCObjectType::getTypeArgStorage() {
  return reinterpret_cast<QualType *>(static_cast<ObjCObjectTypeImpl*>(this)+1);
}

inline ObjCProtocolDecl **ObjCObjectType::getProtocolStorageImpl() {
    return reinterpret_cast<ObjCProtocolDecl**>(
             getTypeArgStorage() + ObjCObjectTypeBits.NumTypeArgs);
}

inline ObjCProtocolDecl **ObjCTypeParamType::getProtocolStorageImpl() {
    return reinterpret_cast<ObjCProtocolDecl**>(
             static_cast<ObjCTypeParamType*>(this)+1);
}

/// Interfaces are the core concept in Objective-C for object oriented design.
/// They basically correspond to C++ classes.  There are two kinds of interface
/// types: normal interfaces like `NSString`, and qualified interfaces, which
/// are qualified with a protocol list like `NSString<NSCopyable, NSAmazing>`.
///
/// ObjCInterfaceType guarantees the following properties when considered
/// as a subtype of its superclass, ObjCObjectType:
///   - There are no protocol qualifiers.  To reinforce this, code which
///     tries to invoke the protocol methods via an ObjCInterfaceType will
///     fail to compile.
///   - It is its own base type.  That is, if T is an ObjCInterfaceType*,
///     T->getBaseType() == QualType(T, 0).
class ObjCInterfaceType : public ObjCObjectType {
  friend class ASTContext; // ASTContext creates these.
  friend class ASTReader;
  template <class T> friend class serialization::AbstractTypeReader;

  ObjCInterfaceDecl *Decl;

  ObjCInterfaceType(const ObjCInterfaceDecl *D)
      : ObjCObjectType(Nonce_ObjCInterface),
        Decl(const_cast<ObjCInterfaceDecl*>(D)) {}

public:
  /// Get the declaration of this interface.
  ObjCInterfaceDecl *getDecl() const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ObjCInterface;
  }

  // Nonsense to "hide" certain members of ObjCObjectType within this
  // class.  People asking for protocols on an ObjCInterfaceType are
  // not going to get what they want: ObjCInterfaceTypes are
  // guaranteed to have no protocols.
  enum {
    qual_iterator,
    qual_begin,
    qual_end,
    getNumProtocols,
    getProtocol
  };
};

inline ObjCInterfaceDecl *ObjCObjectType::getInterface() const {
  QualType baseType = getBaseType();
  while (const auto *ObjT = baseType->getAs<ObjCObjectType>()) {
    if (const auto *T = dyn_cast<ObjCInterfaceType>(ObjT))
      return T->getDecl();

    baseType = ObjT->getBaseType();
  }

  return nullptr;
}

/// Represents a pointer to an Objective C object.
///
/// These are constructed from pointer declarators when the pointee type is
/// an ObjCObjectType (or sugar for one).  In addition, the 'id' and 'Class'
/// types are typedefs for these, and the protocol-qualified types 'id<P>'
/// and 'Class<P>' are translated into these.
///
/// Pointers to pointers to Objective C objects are still PointerTypes;
/// only the first level of pointer gets it own type implementation.
class ObjCObjectPointerType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  QualType PointeeType;

  ObjCObjectPointerType(QualType Canonical, QualType Pointee)
      : Type(ObjCObjectPointer, Canonical, Pointee->getDependence()),
        PointeeType(Pointee) {}

public:
  /// Gets the type pointed to by this ObjC pointer.
  /// The result will always be an ObjCObjectType or sugar thereof.
  QualType getPointeeType() const { return PointeeType; }

  /// Gets the type pointed to by this ObjC pointer.  Always returns non-null.
  ///
  /// This method is equivalent to getPointeeType() except that
  /// it discards any typedefs (or other sugar) between this
  /// type and the "outermost" object type.  So for:
  /// \code
  ///   \@class A; \@protocol P; \@protocol Q;
  ///   typedef A<P> AP;
  ///   typedef A A1;
  ///   typedef A1<P> A1P;
  ///   typedef A1P<Q> A1PQ;
  /// \endcode
  /// For 'A*', getObjectType() will return 'A'.
  /// For 'A<P>*', getObjectType() will return 'A<P>'.
  /// For 'AP*', getObjectType() will return 'A<P>'.
  /// For 'A1*', getObjectType() will return 'A'.
  /// For 'A1<P>*', getObjectType() will return 'A1<P>'.
  /// For 'A1P*', getObjectType() will return 'A1<P>'.
  /// For 'A1PQ*', getObjectType() will return 'A1<Q>', because
  ///   adding protocols to a protocol-qualified base discards the
  ///   old qualifiers (for now).  But if it didn't, getObjectType()
  ///   would return 'A1P<Q>' (and we'd have to make iterating over
  ///   qualifiers more complicated).
  const ObjCObjectType *getObjectType() const {
    return PointeeType->castAs<ObjCObjectType>();
  }

  /// If this pointer points to an Objective C
  /// \@interface type, gets the type for that interface.  Any protocol
  /// qualifiers on the interface are ignored.
  ///
  /// \return null if the base type for this pointer is 'id' or 'Class'
  const ObjCInterfaceType *getInterfaceType() const;

  /// If this pointer points to an Objective \@interface
  /// type, gets the declaration for that interface.
  ///
  /// \return null if the base type for this pointer is 'id' or 'Class'
  ObjCInterfaceDecl *getInterfaceDecl() const {
    return getObjectType()->getInterface();
  }

  /// True if this is equivalent to the 'id' type, i.e. if
  /// its object type is the primitive 'id' type with no protocols.
  bool isObjCIdType() const {
    return getObjectType()->isObjCUnqualifiedId();
  }

  /// True if this is equivalent to the 'Class' type,
  /// i.e. if its object tive is the primitive 'Class' type with no protocols.
  bool isObjCClassType() const {
    return getObjectType()->isObjCUnqualifiedClass();
  }

  /// True if this is equivalent to the 'id' or 'Class' type,
  bool isObjCIdOrClassType() const {
    return getObjectType()->isObjCUnqualifiedIdOrClass();
  }

  /// True if this is equivalent to 'id<P>' for some non-empty set of
  /// protocols.
  bool isObjCQualifiedIdType() const {
    return getObjectType()->isObjCQualifiedId();
  }

  /// True if this is equivalent to 'Class<P>' for some non-empty set of
  /// protocols.
  bool isObjCQualifiedClassType() const {
    return getObjectType()->isObjCQualifiedClass();
  }

  /// Whether this is a "__kindof" type.
  bool isKindOfType() const { return getObjectType()->isKindOfType(); }

  /// Whether this type is specialized, meaning that it has type arguments.
  bool isSpecialized() const { return getObjectType()->isSpecialized(); }

  /// Whether this type is specialized, meaning that it has type arguments.
  bool isSpecializedAsWritten() const {
    return getObjectType()->isSpecializedAsWritten();
  }

  /// Whether this type is unspecialized, meaning that is has no type arguments.
  bool isUnspecialized() const { return getObjectType()->isUnspecialized(); }

  /// Determine whether this object type is "unspecialized" as
  /// written, meaning that it has no type arguments.
  bool isUnspecializedAsWritten() const { return !isSpecializedAsWritten(); }

  /// Retrieve the type arguments for this type.
  ArrayRef<QualType> getTypeArgs() const {
    return getObjectType()->getTypeArgs();
  }

  /// Retrieve the type arguments for this type.
  ArrayRef<QualType> getTypeArgsAsWritten() const {
    return getObjectType()->getTypeArgsAsWritten();
  }

  /// An iterator over the qualifiers on the object type.  Provided
  /// for convenience.  This will always iterate over the full set of
  /// protocols on a type, not just those provided directly.
  using qual_iterator = ObjCObjectType::qual_iterator;
  using qual_range = llvm::iterator_range<qual_iterator>;

  qual_range quals() const { return qual_range(qual_begin(), qual_end()); }

  qual_iterator qual_begin() const {
    return getObjectType()->qual_begin();
  }

  qual_iterator qual_end() const {
    return getObjectType()->qual_end();
  }

  bool qual_empty() const { return getObjectType()->qual_empty(); }

  /// Return the number of qualifying protocols on the object type.
  unsigned getNumProtocols() const {
    return getObjectType()->getNumProtocols();
  }

  /// Retrieve a qualifying protocol by index on the object type.
  ObjCProtocolDecl *getProtocol(unsigned I) const {
    return getObjectType()->getProtocol(I);
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  /// Retrieve the type of the superclass of this object pointer type.
  ///
  /// This operation substitutes any type arguments into the
  /// superclass of the current class type, potentially producing a
  /// pointer to a specialization of the superclass type. Produces a
  /// null type if there is no superclass.
  QualType getSuperClassType() const;

  /// Strip off the Objective-C "kindof" type and (with it) any
  /// protocol qualifiers.
  const ObjCObjectPointerType *stripObjCKindOfTypeAndQuals(
                                 const ASTContext &ctx) const;

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getPointeeType());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType T) {
    ID.AddPointer(T.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ObjCObjectPointer;
  }
};

class AtomicType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  QualType ValueType;

  AtomicType(QualType ValTy, QualType Canonical)
      : Type(Atomic, Canonical, ValTy->getDependence()), ValueType(ValTy) {}

public:
  /// Gets the type contained by this atomic type, i.e.
  /// the type returned by performing an atomic load of this atomic type.
  QualType getValueType() const { return ValueType; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getValueType());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType T) {
    ID.AddPointer(T.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Atomic;
  }
};

/// PipeType - OpenCL20.
class PipeType : public Type, public llvm::FoldingSetNode {
  friend class ASTContext; // ASTContext creates these.

  QualType ElementType;
  bool isRead;

  PipeType(QualType elemType, QualType CanonicalPtr, bool isRead)
      : Type(Pipe, CanonicalPtr, elemType->getDependence()),
        ElementType(elemType), isRead(isRead) {}

public:
  QualType getElementType() const { return ElementType; }

  bool isSugared() const { return false; }

  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), isReadOnly());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType T, bool isRead) {
    ID.AddPointer(T.getAsOpaquePtr());
    ID.AddBoolean(isRead);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Pipe;
  }

  bool isReadOnly() const { return isRead; }
};

/// A fixed int type of a specified bitwidth.
class BitIntType final : public Type, public llvm::FoldingSetNode {
  friend class ASTContext;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsUnsigned : 1;
  unsigned NumBits : 24;

protected:
  BitIntType(bool isUnsigned, unsigned NumBits);

public:
  bool isUnsigned() const { return IsUnsigned; }
  bool isSigned() const { return !IsUnsigned; }
  unsigned getNumBits() const { return NumBits; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, isUnsigned(), getNumBits());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, bool IsUnsigned,
                      unsigned NumBits) {
    ID.AddBoolean(IsUnsigned);
    ID.AddInteger(NumBits);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == BitInt; }
};

class DependentBitIntType final : public Type, public llvm::FoldingSetNode {
  friend class ASTContext;
  llvm::PointerIntPair<Expr*, 1, bool> ExprAndUnsigned;

protected:
  DependentBitIntType(bool IsUnsigned, Expr *NumBits);

public:
  bool isUnsigned() const;
  bool isSigned() const { return !isUnsigned(); }
  Expr *getNumBitsExpr() const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context) {
    Profile(ID, Context, isUnsigned(), getNumBitsExpr());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                      bool IsUnsigned, Expr *NumBitsExpr);

  static bool classof(const Type *T) {
    return T->getTypeClass() == DependentBitInt;
  }
};

/// A qualifier set is used to build a set of qualifiers.
class QualifierCollector : public Qualifiers {
public:
  QualifierCollector(Qualifiers Qs = Qualifiers()) : Qualifiers(Qs) {}

  /// Collect any qualifiers on the given type and return an
  /// unqualified type.  The qualifiers are assumed to be consistent
  /// with those already in the type.
  const Type *strip(QualType type) {
    addFastQualifiers(type.getLocalFastQualifiers());
    if (!type.hasLocalNonFastQualifiers())
      return type.getTypePtrUnsafe();

    const ExtQuals *extQuals = type.getExtQualsUnsafe();
    addConsistentQualifiers(extQuals->getQualifiers());
    return extQuals->getBaseType();
  }

  /// Apply the collected qualifiers to the given type.
  QualType apply(const ASTContext &Context, QualType QT) const;

  /// Apply the collected qualifiers to the given type.
  QualType apply(const ASTContext &Context, const Type* T) const;
};

/// A container of type source information.
///
/// A client can read the relevant info using TypeLoc wrappers, e.g:
/// @code
/// TypeLoc TL = TypeSourceInfo->getTypeLoc();
/// TL.getBeginLoc().print(OS, SrcMgr);
/// @endcode
class alignas(8) TypeSourceInfo {
  // Contains a memory block after the class, used for type source information,
  // allocated by ASTContext.
  friend class ASTContext;

  QualType Ty;

  TypeSourceInfo(QualType ty, size_t DataSize); // implemented in TypeLoc.h

public:
  /// Return the type wrapped by this type source info.
  QualType getType() const { return Ty; }

  /// Return the TypeLoc wrapper for the type source info.
  TypeLoc getTypeLoc() const; // implemented in TypeLoc.h

  /// Override the type stored in this TypeSourceInfo. Use with caution!
  void overrideType(QualType T) { Ty = T; }
};

// Inline function definitions.

inline SplitQualType SplitQualType::getSingleStepDesugaredType() const {
  SplitQualType desugar =
    Ty->getLocallyUnqualifiedSingleStepDesugaredType().split();
  desugar.Quals.addConsistentQualifiers(Quals);
  return desugar;
}

inline const Type *QualType::getTypePtr() const {
  return getCommonPtr()->BaseType;
}

inline const Type *QualType::getTypePtrOrNull() const {
  return (isNull() ? nullptr : getCommonPtr()->BaseType);
}

inline bool QualType::isReferenceable() const {
  // C++ [defns.referenceable]
  //   type that is either an object type, a function type that does not have
  //   cv-qualifiers or a ref-qualifier, or a reference type.
  const Type &Self = **this;
  if (Self.isObjectType() || Self.isReferenceType())
    return true;
  if (const auto *F = Self.getAs<FunctionProtoType>())
    return F->getMethodQuals().empty() && F->getRefQualifier() == RQ_None;

  return false;
}

inline SplitQualType QualType::split() const {
  if (!hasLocalNonFastQualifiers())
    return SplitQualType(getTypePtrUnsafe(),
                         Qualifiers::fromFastMask(getLocalFastQualifiers()));

  const ExtQuals *eq = getExtQualsUnsafe();
  Qualifiers qs = eq->getQualifiers();
  qs.addFastQualifiers(getLocalFastQualifiers());
  return SplitQualType(eq->getBaseType(), qs);
}

inline Qualifiers QualType::getLocalQualifiers() const {
  Qualifiers Quals;
  if (hasLocalNonFastQualifiers())
    Quals = getExtQualsUnsafe()->getQualifiers();
  Quals.addFastQualifiers(getLocalFastQualifiers());
  return Quals;
}

inline Qualifiers QualType::getQualifiers() const {
  Qualifiers quals = getCommonPtr()->CanonicalType.getLocalQualifiers();
  quals.addFastQualifiers(getLocalFastQualifiers());
  return quals;
}

inline unsigned QualType::getCVRQualifiers() const {
  unsigned cvr = getCommonPtr()->CanonicalType.getLocalCVRQualifiers();
  cvr |= getLocalCVRQualifiers();
  return cvr;
}

inline QualType QualType::getCanonicalType() const {
  QualType canon = getCommonPtr()->CanonicalType;
  return canon.withFastQualifiers(getLocalFastQualifiers());
}

inline bool QualType::isCanonical() const {
  return getTypePtr()->isCanonicalUnqualified();
}

inline bool QualType::isCanonicalAsParam() const {
  if (!isCanonical()) return false;
  if (hasLocalQualifiers()) return false;

  const Type *T = getTypePtr();
  if (T->isVariablyModifiedType() && T->hasSizedVLAType())
    return false;

  return !isa<FunctionType>(T) &&
         (!isa<ArrayType>(T) || isa<ArrayParameterType>(T));
}

inline bool QualType::isConstQualified() const {
  return isLocalConstQualified() ||
         getCommonPtr()->CanonicalType.isLocalConstQualified();
}

inline bool QualType::isRestrictQualified() const {
  return isLocalRestrictQualified() ||
         getCommonPtr()->CanonicalType.isLocalRestrictQualified();
}


inline bool QualType::isVolatileQualified() const {
  return isLocalVolatileQualified() ||
         getCommonPtr()->CanonicalType.isLocalVolatileQualified();
}

inline bool QualType::hasQualifiers() const {
  return hasLocalQualifiers() ||
         getCommonPtr()->CanonicalType.hasLocalQualifiers();
}

inline QualType QualType::getUnqualifiedType() const {
  if (!getTypePtr()->getCanonicalTypeInternal().hasLocalQualifiers())
    return QualType(getTypePtr(), 0);

  return QualType(getSplitUnqualifiedTypeImpl(*this).Ty, 0);
}

inline SplitQualType QualType::getSplitUnqualifiedType() const {
  if (!getTypePtr()->getCanonicalTypeInternal().hasLocalQualifiers())
    return split();

  return getSplitUnqualifiedTypeImpl(*this);
}

inline void QualType::removeLocalConst() {
  removeLocalFastQualifiers(Qualifiers::Const);
}

inline void QualType::removeLocalRestrict() {
  removeLocalFastQualifiers(Qualifiers::Restrict);
}

inline void QualType::removeLocalVolatile() {
  removeLocalFastQualifiers(Qualifiers::Volatile);
}

/// Check if this type has any address space qualifier.
inline bool QualType::hasAddressSpace() const {
  return getQualifiers().hasAddressSpace();
}

/// Return the address space of this type.
inline LangAS QualType::getAddressSpace() const {
  return getQualifiers().getAddressSpace();
}

/// Return the gc attribute of this type.
inline Qualifiers::GC QualType::getObjCGCAttr() const {
  return getQualifiers().getObjCGCAttr();
}

inline bool QualType::hasNonTrivialToPrimitiveDefaultInitializeCUnion() const {
  if (auto *RD = getTypePtr()->getBaseElementTypeUnsafe()->getAsRecordDecl())
    return hasNonTrivialToPrimitiveDefaultInitializeCUnion(RD);
  return false;
}

inline bool QualType::hasNonTrivialToPrimitiveDestructCUnion() const {
  if (auto *RD = getTypePtr()->getBaseElementTypeUnsafe()->getAsRecordDecl())
    return hasNonTrivialToPrimitiveDestructCUnion(RD);
  return false;
}

inline bool QualType::hasNonTrivialToPrimitiveCopyCUnion() const {
  if (auto *RD = getTypePtr()->getBaseElementTypeUnsafe()->getAsRecordDecl())
    return hasNonTrivialToPrimitiveCopyCUnion(RD);
  return false;
}

inline FunctionType::ExtInfo getFunctionExtInfo(const Type &t) {
  if (const auto *PT = t.getAs<PointerType>()) {
    if (const auto *FT = PT->getPointeeType()->getAs<FunctionType>())
      return FT->getExtInfo();
  } else if (const auto *FT = t.getAs<FunctionType>())
    return FT->getExtInfo();

  return FunctionType::ExtInfo();
}

inline FunctionType::ExtInfo getFunctionExtInfo(QualType t) {
  return getFunctionExtInfo(*t);
}

/// Determine whether this type is more
/// qualified than the Other type. For example, "const volatile int"
/// is more qualified than "const int", "volatile int", and
/// "int". However, it is not more qualified than "const volatile
/// int".
inline bool QualType::isMoreQualifiedThan(QualType other) const {
  Qualifiers MyQuals = getQualifiers();
  Qualifiers OtherQuals = other.getQualifiers();
  return (MyQuals != OtherQuals && MyQuals.compatiblyIncludes(OtherQuals));
}

/// Determine whether this type is at last
/// as qualified as the Other type. For example, "const volatile
/// int" is at least as qualified as "const int", "volatile int",
/// "int", and "const volatile int".
inline bool QualType::isAtLeastAsQualifiedAs(QualType other) const {
  Qualifiers OtherQuals = other.getQualifiers();

  // Ignore __unaligned qualifier if this type is a void.
  if (getUnqualifiedType()->isVoidType())
    OtherQuals.removeUnaligned();

  return getQualifiers().compatiblyIncludes(OtherQuals);
}

/// If Type is a reference type (e.g., const
/// int&), returns the type that the reference refers to ("const
/// int"). Otherwise, returns the type itself. This routine is used
/// throughout Sema to implement C++ 5p6:
///
///   If an expression initially has the type "reference to T" (8.3.2,
///   8.5.3), the type is adjusted to "T" prior to any further
///   analysis, the expression designates the object or function
///   denoted by the reference, and the expression is an lvalue.
inline QualType QualType::getNonReferenceType() const {
  if (const auto *RefType = (*this)->getAs<ReferenceType>())
    return RefType->getPointeeType();
  else
    return *this;
}

inline bool QualType::isCForbiddenLValueType() const {
  return ((getTypePtr()->isVoidType() && !hasQualifiers()) ||
          getTypePtr()->isFunctionType());
}

/// Tests whether the type is categorized as a fundamental type.
///
/// \returns True for types specified in C++0x [basic.fundamental].
inline bool Type::isFundamentalType() const {
  return isVoidType() ||
         isNullPtrType() ||
         // FIXME: It's really annoying that we don't have an
         // 'isArithmeticType()' which agrees with the standard definition.
         (isArithmeticType() && !isEnumeralType());
}

/// Tests whether the type is categorized as a compound type.
///
/// \returns True for types specified in C++0x [basic.compound].
inline bool Type::isCompoundType() const {
  // C++0x [basic.compound]p1:
  //   Compound types can be constructed in the following ways:
  //    -- arrays of objects of a given type [...];
  return isArrayType() ||
  //    -- functions, which have parameters of given types [...];
         isFunctionType() ||
  //    -- pointers to void or objects or functions [...];
         isPointerType() ||
  //    -- references to objects or functions of a given type. [...]
         isReferenceType() ||
  //    -- classes containing a sequence of objects of various types, [...];
         isRecordType() ||
  //    -- unions, which are classes capable of containing objects of different
  //               types at different times;
         isUnionType() ||
  //    -- enumerations, which comprise a set of named constant values. [...];
         isEnumeralType() ||
  //    -- pointers to non-static class members, [...].
         isMemberPointerType();
}

inline bool Type::isFunctionType() const {
  return isa<FunctionType>(CanonicalType);
}

inline bool Type::isPointerType() const {
  return isa<PointerType>(CanonicalType);
}

inline bool Type::isAnyPointerType() const {
  return isPointerType() || isObjCObjectPointerType();
}

inline bool Type::isSignableType() const { return isPointerType(); }

inline bool Type::isBlockPointerType() const {
  return isa<BlockPointerType>(CanonicalType);
}

inline bool Type::isReferenceType() const {
  return isa<ReferenceType>(CanonicalType);
}

inline bool Type::isLValueReferenceType() const {
  return isa<LValueReferenceType>(CanonicalType);
}

inline bool Type::isRValueReferenceType() const {
  return isa<RValueReferenceType>(CanonicalType);
}

inline bool Type::isObjectPointerType() const {
  // Note: an "object pointer type" is not the same thing as a pointer to an
  // object type; rather, it is a pointer to an object type or a pointer to cv
  // void.
  if (const auto *T = getAs<PointerType>())
    return !T->getPointeeType()->isFunctionType();
  else
    return false;
}

inline bool Type::isFunctionPointerType() const {
  if (const auto *T = getAs<PointerType>())
    return T->getPointeeType()->isFunctionType();
  else
    return false;
}

inline bool Type::isFunctionReferenceType() const {
  if (const auto *T = getAs<ReferenceType>())
    return T->getPointeeType()->isFunctionType();
  else
    return false;
}

inline bool Type::isMemberPointerType() const {
  return isa<MemberPointerType>(CanonicalType);
}

inline bool Type::isMemberFunctionPointerType() const {
  if (const auto *T = getAs<MemberPointerType>())
    return T->isMemberFunctionPointer();
  else
    return false;
}

inline bool Type::isMemberDataPointerType() const {
  if (const auto *T = getAs<MemberPointerType>())
    return T->isMemberDataPointer();
  else
    return false;
}

inline bool Type::isArrayType() const {
  return isa<ArrayType>(CanonicalType);
}

inline bool Type::isConstantArrayType() const {
  return isa<ConstantArrayType>(CanonicalType);
}

inline bool Type::isIncompleteArrayType() const {
  return isa<IncompleteArrayType>(CanonicalType);
}

inline bool Type::isVariableArrayType() const {
  return isa<VariableArrayType>(CanonicalType);
}

inline bool Type::isArrayParameterType() const {
  return isa<ArrayParameterType>(CanonicalType);
}

inline bool Type::isDependentSizedArrayType() const {
  return isa<DependentSizedArrayType>(CanonicalType);
}

inline bool Type::isBuiltinType() const {
  return isa<BuiltinType>(CanonicalType);
}

inline bool Type::isRecordType() const {
  return isa<RecordType>(CanonicalType);
}

inline bool Type::isEnumeralType() const {
  return isa<EnumType>(CanonicalType);
}

inline bool Type::isAnyComplexType() const {
  return isa<ComplexType>(CanonicalType);
}

inline bool Type::isVectorType() const {
  return isa<VectorType>(CanonicalType);
}

inline bool Type::isExtVectorType() const {
  return isa<ExtVectorType>(CanonicalType);
}

inline bool Type::isExtVectorBoolType() const {
  if (!isExtVectorType())
    return false;
  return cast<ExtVectorType>(CanonicalType)->getElementType()->isBooleanType();
}

inline bool Type::isSubscriptableVectorType() const {
  return isVectorType() || isSveVLSBuiltinType();
}

inline bool Type::isMatrixType() const {
  return isa<MatrixType>(CanonicalType);
}

inline bool Type::isConstantMatrixType() const {
  return isa<ConstantMatrixType>(CanonicalType);
}

inline bool Type::isDependentAddressSpaceType() const {
  return isa<DependentAddressSpaceType>(CanonicalType);
}

inline bool Type::isObjCObjectPointerType() const {
  return isa<ObjCObjectPointerType>(CanonicalType);
}

inline bool Type::isObjCObjectType() const {
  return isa<ObjCObjectType>(CanonicalType);
}

inline bool Type::isObjCObjectOrInterfaceType() const {
  return isa<ObjCInterfaceType>(CanonicalType) ||
    isa<ObjCObjectType>(CanonicalType);
}

inline bool Type::isAtomicType() const {
  return isa<AtomicType>(CanonicalType);
}

inline bool Type::isUndeducedAutoType() const {
  return isa<AutoType>(CanonicalType);
}

inline bool Type::isObjCQualifiedIdType() const {
  if (const auto *OPT = getAs<ObjCObjectPointerType>())
    return OPT->isObjCQualifiedIdType();
  return false;
}

inline bool Type::isObjCQualifiedClassType() const {
  if (const auto *OPT = getAs<ObjCObjectPointerType>())
    return OPT->isObjCQualifiedClassType();
  return false;
}

inline bool Type::isObjCIdType() const {
  if (const auto *OPT = getAs<ObjCObjectPointerType>())
    return OPT->isObjCIdType();
  return false;
}

inline bool Type::isObjCClassType() const {
  if (const auto *OPT = getAs<ObjCObjectPointerType>())
    return OPT->isObjCClassType();
  return false;
}

inline bool Type::isObjCSelType() const {
  if (const auto *OPT = getAs<PointerType>())
    return OPT->getPointeeType()->isSpecificBuiltinType(BuiltinType::ObjCSel);
  return false;
}

inline bool Type::isObjCBuiltinType() const {
  return isObjCIdType() || isObjCClassType() || isObjCSelType();
}

inline bool Type::isDecltypeType() const {
  return isa<DecltypeType>(this);
}

#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  inline bool Type::is##Id##Type() const { \
    return isSpecificBuiltinType(BuiltinType::Id); \
  }
#include "clang/Basic/OpenCLImageTypes.def"

inline bool Type::isSamplerT() const {
  return isSpecificBuiltinType(BuiltinType::OCLSampler);
}

inline bool Type::isEventT() const {
  return isSpecificBuiltinType(BuiltinType::OCLEvent);
}

inline bool Type::isClkEventT() const {
  return isSpecificBuiltinType(BuiltinType::OCLClkEvent);
}

inline bool Type::isQueueT() const {
  return isSpecificBuiltinType(BuiltinType::OCLQueue);
}

inline bool Type::isReserveIDT() const {
  return isSpecificBuiltinType(BuiltinType::OCLReserveID);
}

inline bool Type::isImageType() const {
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) is##Id##Type() ||
  return
#include "clang/Basic/OpenCLImageTypes.def"
      false; // end boolean or operation
}

inline bool Type::isPipeType() const {
  return isa<PipeType>(CanonicalType);
}

inline bool Type::isBitIntType() const {
  return isa<BitIntType>(CanonicalType);
}

#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  inline bool Type::is##Id##Type() const { \
    return isSpecificBuiltinType(BuiltinType::Id); \
  }
#include "clang/Basic/OpenCLExtensionTypes.def"

inline bool Type::isOCLIntelSubgroupAVCType() const {
#define INTEL_SUBGROUP_AVC_TYPE(ExtType, Id) \
  isOCLIntelSubgroupAVC##Id##Type() ||
  return
#include "clang/Basic/OpenCLExtensionTypes.def"
    false; // end of boolean or operation
}

inline bool Type::isOCLExtOpaqueType() const {
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) is##Id##Type() ||
  return
#include "clang/Basic/OpenCLExtensionTypes.def"
    false; // end of boolean or operation
}

inline bool Type::isOpenCLSpecificType() const {
  return isSamplerT() || isEventT() || isImageType() || isClkEventT() ||
         isQueueT() || isReserveIDT() || isPipeType() || isOCLExtOpaqueType();
}

inline bool Type::isTemplateTypeParmType() const {
  return isa<TemplateTypeParmType>(CanonicalType);
}

inline bool Type::isSpecificBuiltinType(unsigned K) const {
  if (const BuiltinType *BT = getAs<BuiltinType>()) {
    return BT->getKind() == static_cast<BuiltinType::Kind>(K);
  }
  return false;
}

inline bool Type::isPlaceholderType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(this))
    return BT->isPlaceholderType();
  return false;
}

inline const BuiltinType *Type::getAsPlaceholderType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(this))
    if (BT->isPlaceholderType())
      return BT;
  return nullptr;
}

inline bool Type::isSpecificPlaceholderType(unsigned K) const {
  assert(BuiltinType::isPlaceholderTypeKind((BuiltinType::Kind) K));
  return isSpecificBuiltinType(K);
}

inline bool Type::isNonOverloadPlaceholderType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(this))
    return BT->isNonOverloadPlaceholderType();
  return false;
}

inline bool Type::isVoidType() const {
  return isSpecificBuiltinType(BuiltinType::Void);
}

inline bool Type::isHalfType() const {
  // FIXME: Should we allow complex __fp16? Probably not.
  return isSpecificBuiltinType(BuiltinType::Half);
}

inline bool Type::isFloat16Type() const {
  return isSpecificBuiltinType(BuiltinType::Float16);
}

inline bool Type::isFloat32Type() const {
  return isSpecificBuiltinType(BuiltinType::Float);
}

inline bool Type::isDoubleType() const {
  return isSpecificBuiltinType(BuiltinType::Double);
}

inline bool Type::isBFloat16Type() const {
  return isSpecificBuiltinType(BuiltinType::BFloat16);
}

inline bool Type::isFloat128Type() const {
  return isSpecificBuiltinType(BuiltinType::Float128);
}

inline bool Type::isIbm128Type() const {
  return isSpecificBuiltinType(BuiltinType::Ibm128);
}

inline bool Type::isNullPtrType() const {
  return isSpecificBuiltinType(BuiltinType::NullPtr);
}

bool IsEnumDeclComplete(EnumDecl *);
bool IsEnumDeclScoped(EnumDecl *);

inline bool Type::isIntegerType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Int128;
  if (const EnumType *ET = dyn_cast<EnumType>(CanonicalType)) {
    // Incomplete enum types are not treated as integer types.
    // FIXME: In C++, enum types are never integer types.
    return IsEnumDeclComplete(ET->getDecl()) &&
      !IsEnumDeclScoped(ET->getDecl());
  }
  return isBitIntType();
}

inline bool Type::isFixedPointType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::ShortAccum &&
           BT->getKind() <= BuiltinType::SatULongFract;
  }
  return false;
}

inline bool Type::isFixedPointOrIntegerType() const {
  return isFixedPointType() || isIntegerType();
}

inline bool Type::isConvertibleToFixedPointType() const {
  return isRealFloatingType() || isFixedPointOrIntegerType();
}

inline bool Type::isSaturatedFixedPointType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::SatShortAccum &&
           BT->getKind() <= BuiltinType::SatULongFract;
  }
  return false;
}

inline bool Type::isUnsaturatedFixedPointType() const {
  return isFixedPointType() && !isSaturatedFixedPointType();
}

inline bool Type::isSignedFixedPointType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return ((BT->getKind() >= BuiltinType::ShortAccum &&
             BT->getKind() <= BuiltinType::LongAccum) ||
            (BT->getKind() >= BuiltinType::ShortFract &&
             BT->getKind() <= BuiltinType::LongFract) ||
            (BT->getKind() >= BuiltinType::SatShortAccum &&
             BT->getKind() <= BuiltinType::SatLongAccum) ||
            (BT->getKind() >= BuiltinType::SatShortFract &&
             BT->getKind() <= BuiltinType::SatLongFract));
  }
  return false;
}

inline bool Type::isUnsignedFixedPointType() const {
  return isFixedPointType() && !isSignedFixedPointType();
}

inline bool Type::isScalarType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() > BuiltinType::Void &&
           BT->getKind() <= BuiltinType::NullPtr;
  if (const EnumType *ET = dyn_cast<EnumType>(CanonicalType))
    // Enums are scalar types, but only if they are defined.  Incomplete enums
    // are not treated as scalar types.
    return IsEnumDeclComplete(ET->getDecl());
  return isa<PointerType>(CanonicalType) ||
         isa<BlockPointerType>(CanonicalType) ||
         isa<MemberPointerType>(CanonicalType) ||
         isa<ComplexType>(CanonicalType) ||
         isa<ObjCObjectPointerType>(CanonicalType) ||
         isBitIntType();
}

inline bool Type::isIntegralOrEnumerationType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Int128;

  // Check for a complete enum type; incomplete enum types are not properly an
  // enumeration type in the sense required here.
  if (const auto *ET = dyn_cast<EnumType>(CanonicalType))
    return IsEnumDeclComplete(ET->getDecl());

  return isBitIntType();
}

inline bool Type::isBooleanType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() == BuiltinType::Bool;
  return false;
}

inline bool Type::isUndeducedType() const {
  auto *DT = getContainedDeducedType();
  return DT && !DT->isDeduced();
}

/// Determines whether this is a type for which one can define
/// an overloaded operator.
inline bool Type::isOverloadableType() const {
  if (!CanonicalType->isDependentType())
    return isRecordType() || isEnumeralType();
  return !isArrayType() && !isFunctionType() && !isAnyPointerType() &&
         !isMemberPointerType();
}

/// Determines whether this type is written as a typedef-name.
inline bool Type::isTypedefNameType() const {
  if (getAs<TypedefType>())
    return true;
  if (auto *TST = getAs<TemplateSpecializationType>())
    return TST->isTypeAlias();
  return false;
}

/// Determines whether this type can decay to a pointer type.
inline bool Type::canDecayToPointerType() const {
  return isFunctionType() || (isArrayType() && !isArrayParameterType());
}

inline bool Type::hasPointerRepresentation() const {
  return (isPointerType() || isReferenceType() || isBlockPointerType() ||
          isObjCObjectPointerType() || isNullPtrType());
}

inline bool Type::hasObjCPointerRepresentation() const {
  return isObjCObjectPointerType();
}

inline const Type *Type::getBaseElementTypeUnsafe() const {
  const Type *type = this;
  while (const ArrayType *arrayType = type->getAsArrayTypeUnsafe())
    type = arrayType->getElementType().getTypePtr();
  return type;
}

inline const Type *Type::getPointeeOrArrayElementType() const {
  const Type *type = this;
  if (type->isAnyPointerType())
    return type->getPointeeType().getTypePtr();
  else if (type->isArrayType())
    return type->getBaseElementTypeUnsafe();
  return type;
}
/// Insertion operator for partial diagnostics. This allows sending adress
/// spaces into a diagnostic with <<.
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             LangAS AS) {
  PD.AddTaggedVal(llvm::to_underlying(AS),
                  DiagnosticsEngine::ArgumentKind::ak_addrspace);
  return PD;
}

/// Insertion operator for partial diagnostics. This allows sending Qualifiers
/// into a diagnostic with <<.
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             Qualifiers Q) {
  PD.AddTaggedVal(Q.getAsOpaqueValue(),
                  DiagnosticsEngine::ArgumentKind::ak_qual);
  return PD;
}

/// Insertion operator for partial diagnostics.  This allows sending QualType's
/// into a diagnostic with <<.
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             QualType T) {
  PD.AddTaggedVal(reinterpret_cast<uint64_t>(T.getAsOpaquePtr()),
                  DiagnosticsEngine::ak_qualtype);
  return PD;
}

// Helper class template that is used by Type::getAs to ensure that one does
// not try to look through a qualified type to get to an array type.
template <typename T>
using TypeIsArrayType =
    std::integral_constant<bool, std::is_same<T, ArrayType>::value ||
                                     std::is_base_of<ArrayType, T>::value>;

// Member-template getAs<specific type>'.
template <typename T> const T *Type::getAs() const {
  static_assert(!TypeIsArrayType<T>::value,
                "ArrayType cannot be used with getAs!");

  // If this is directly a T type, return it.
  if (const auto *Ty = dyn_cast<T>(this))
    return Ty;

  // If the canonical form of this type isn't the right kind, reject it.
  if (!isa<T>(CanonicalType))
    return nullptr;

  // If this is a typedef for the type, strip the typedef off without
  // losing all typedef information.
  return cast<T>(getUnqualifiedDesugaredType());
}

template <typename T> const T *Type::getAsAdjusted() const {
  static_assert(!TypeIsArrayType<T>::value, "ArrayType cannot be used with getAsAdjusted!");

  // If this is directly a T type, return it.
  if (const auto *Ty = dyn_cast<T>(this))
    return Ty;

  // If the canonical form of this type isn't the right kind, reject it.
  if (!isa<T>(CanonicalType))
    return nullptr;

  // Strip off type adjustments that do not modify the underlying nature of the
  // type.
  const Type *Ty = this;
  while (Ty) {
    if (const auto *A = dyn_cast<AttributedType>(Ty))
      Ty = A->getModifiedType().getTypePtr();
    else if (const auto *A = dyn_cast<BTFTagAttributedType>(Ty))
      Ty = A->getWrappedType().getTypePtr();
    else if (const auto *E = dyn_cast<ElaboratedType>(Ty))
      Ty = E->desugar().getTypePtr();
    else if (const auto *P = dyn_cast<ParenType>(Ty))
      Ty = P->desugar().getTypePtr();
    else if (const auto *A = dyn_cast<AdjustedType>(Ty))
      Ty = A->desugar().getTypePtr();
    else if (const auto *M = dyn_cast<MacroQualifiedType>(Ty))
      Ty = M->desugar().getTypePtr();
    else
      break;
  }

  // Just because the canonical type is correct does not mean we can use cast<>,
  // since we may not have stripped off all the sugar down to the base type.
  return dyn_cast<T>(Ty);
}

inline const ArrayType *Type::getAsArrayTypeUnsafe() const {
  // If this is directly an array type, return it.
  if (const auto *arr = dyn_cast<ArrayType>(this))
    return arr;

  // If the canonical form of this type isn't the right kind, reject it.
  if (!isa<ArrayType>(CanonicalType))
    return nullptr;

  // If this is a typedef for the type, strip the typedef off without
  // losing all typedef information.
  return cast<ArrayType>(getUnqualifiedDesugaredType());
}

template <typename T> const T *Type::castAs() const {
  static_assert(!TypeIsArrayType<T>::value,
                "ArrayType cannot be used with castAs!");

  if (const auto *ty = dyn_cast<T>(this)) return ty;
  assert(isa<T>(CanonicalType));
  return cast<T>(getUnqualifiedDesugaredType());
}

inline const ArrayType *Type::castAsArrayTypeUnsafe() const {
  assert(isa<ArrayType>(CanonicalType));
  if (const auto *arr = dyn_cast<ArrayType>(this)) return arr;
  return cast<ArrayType>(getUnqualifiedDesugaredType());
}

DecayedType::DecayedType(QualType OriginalType, QualType DecayedPtr,
                         QualType CanonicalPtr)
    : AdjustedType(Decayed, OriginalType, DecayedPtr, CanonicalPtr) {
#ifndef NDEBUG
  QualType Adjusted = getAdjustedType();
  (void)AttributedType::stripOuterNullability(Adjusted);
  assert(isa<PointerType>(Adjusted));
#endif
}

QualType DecayedType::getPointeeType() const {
  QualType Decayed = getDecayedType();
  (void)AttributedType::stripOuterNullability(Decayed);
  return cast<PointerType>(Decayed)->getPointeeType();
}

// Get the decimal string representation of a fixed point type, represented
// as a scaled integer.
// TODO: At some point, we should change the arguments to instead just accept an
// APFixedPoint instead of APSInt and scale.
void FixedPointValueToString(SmallVectorImpl<char> &Str, llvm::APSInt Val,
                             unsigned Scale);

inline FunctionEffectsRef FunctionEffectsRef::get(QualType QT) {
  while (true) {
    QualType Pointee = QT->getPointeeType();
    if (Pointee.isNull())
      break;
    QT = Pointee;
  }
  if (const auto *FPT = QT->getAs<FunctionProtoType>())
    return FPT->getFunctionEffects();
  return {};
}

} // namespace clang

#endif // LLVM_CLANG_AST_TYPE_H
