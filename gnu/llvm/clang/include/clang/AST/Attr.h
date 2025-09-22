//===--- Attr.h - Classes for representing attributes ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Attr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ATTR_H
#define LLVM_CLANG_AST_ATTR_H

#include "clang/AST/ASTFwd.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/Frontend/HLSL/HLSLResource.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>

namespace clang {
class ASTContext;
class AttributeCommonInfo;
class FunctionDecl;
class OMPTraitInfo;

/// Attr - This represents one attribute.
class Attr : public AttributeCommonInfo {
private:
  LLVM_PREFERRED_TYPE(attr::Kind)
  unsigned AttrKind : 16;

protected:
  /// An index into the spelling list of an
  /// attribute defined in Attr.td file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned Inherited : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsPackExpansion : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Implicit : 1;
  // FIXME: These are properties of the attribute kind, not state for this
  // instance of the attribute.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsLateParsed : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned InheritEvenIfAlreadyPresent : 1;

  void *operator new(size_t bytes) noexcept {
    llvm_unreachable("Attrs cannot be allocated with regular 'new'.");
  }
  void operator delete(void *data) noexcept {
    llvm_unreachable("Attrs cannot be released with regular 'delete'.");
  }

public:
  // Forward so that the regular new and delete do not hide global ones.
  void *operator new(size_t Bytes, ASTContext &C,
                     size_t Alignment = 8) noexcept {
    return ::operator new(Bytes, C, Alignment);
  }
  void operator delete(void *Ptr, ASTContext &C, size_t Alignment) noexcept {
    return ::operator delete(Ptr, C, Alignment);
  }

protected:
  Attr(ASTContext &Context, const AttributeCommonInfo &CommonInfo,
       attr::Kind AK, bool IsLateParsed)
      : AttributeCommonInfo(CommonInfo), AttrKind(AK), Inherited(false),
        IsPackExpansion(false), Implicit(false), IsLateParsed(IsLateParsed),
        InheritEvenIfAlreadyPresent(false) {}

public:
  attr::Kind getKind() const { return static_cast<attr::Kind>(AttrKind); }

  unsigned getSpellingListIndex() const {
    return getAttributeSpellingListIndex();
  }
  const char *getSpelling() const;

  SourceLocation getLocation() const { return getRange().getBegin(); }

  bool isInherited() const { return Inherited; }

  /// Returns true if the attribute has been implicitly created instead
  /// of explicitly written by the user.
  bool isImplicit() const { return Implicit; }
  void setImplicit(bool I) { Implicit = I; }

  void setPackExpansion(bool PE) { IsPackExpansion = PE; }
  bool isPackExpansion() const { return IsPackExpansion; }

  // Clone this attribute.
  Attr *clone(ASTContext &C) const;

  bool isLateParsed() const { return IsLateParsed; }

  // Pretty print this attribute.
  void printPretty(raw_ostream &OS, const PrintingPolicy &Policy) const;

  static StringRef getDocumentation(attr::Kind);
};

class TypeAttr : public Attr {
protected:
  TypeAttr(ASTContext &Context, const AttributeCommonInfo &CommonInfo,
           attr::Kind AK, bool IsLateParsed)
      : Attr(Context, CommonInfo, AK, IsLateParsed) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstTypeAttr &&
           A->getKind() <= attr::LastTypeAttr;
  }
};

class StmtAttr : public Attr {
protected:
  StmtAttr(ASTContext &Context, const AttributeCommonInfo &CommonInfo,
           attr::Kind AK, bool IsLateParsed)
      : Attr(Context, CommonInfo, AK, IsLateParsed) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstStmtAttr &&
           A->getKind() <= attr::LastStmtAttr;
  }
};

class InheritableAttr : public Attr {
protected:
  InheritableAttr(ASTContext &Context, const AttributeCommonInfo &CommonInfo,
                  attr::Kind AK, bool IsLateParsed,
                  bool InheritEvenIfAlreadyPresent)
      : Attr(Context, CommonInfo, AK, IsLateParsed) {
    this->InheritEvenIfAlreadyPresent = InheritEvenIfAlreadyPresent;
  }

public:
  void setInherited(bool I) { Inherited = I; }

  /// Should this attribute be inherited from a prior declaration even if it's
  /// explicitly provided in the current declaration?
  bool shouldInheritEvenIfAlreadyPresent() const {
    return InheritEvenIfAlreadyPresent;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstInheritableAttr &&
           A->getKind() <= attr::LastInheritableAttr;
  }
};

class DeclOrStmtAttr : public InheritableAttr {
protected:
  DeclOrStmtAttr(ASTContext &Context, const AttributeCommonInfo &CommonInfo,
                 attr::Kind AK, bool IsLateParsed,
                 bool InheritEvenIfAlreadyPresent)
      : InheritableAttr(Context, CommonInfo, AK, IsLateParsed,
                        InheritEvenIfAlreadyPresent) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstDeclOrStmtAttr &&
           A->getKind() <= attr::LastDeclOrStmtAttr;
  }
};

class InheritableParamAttr : public InheritableAttr {
protected:
  InheritableParamAttr(ASTContext &Context,
                       const AttributeCommonInfo &CommonInfo, attr::Kind AK,
                       bool IsLateParsed, bool InheritEvenIfAlreadyPresent)
      : InheritableAttr(Context, CommonInfo, AK, IsLateParsed,
                        InheritEvenIfAlreadyPresent) {}

public:
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstInheritableParamAttr &&
           A->getKind() <= attr::LastInheritableParamAttr;
  }
};

class HLSLAnnotationAttr : public InheritableAttr {
protected:
  HLSLAnnotationAttr(ASTContext &Context, const AttributeCommonInfo &CommonInfo,
                     attr::Kind AK, bool IsLateParsed,
                     bool InheritEvenIfAlreadyPresent)
      : InheritableAttr(Context, CommonInfo, AK, IsLateParsed,
                        InheritEvenIfAlreadyPresent) {}

public:
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstHLSLAnnotationAttr &&
           A->getKind() <= attr::LastHLSLAnnotationAttr;
  }
};

/// A parameter attribute which changes the argument-passing ABI rule
/// for the parameter.
class ParameterABIAttr : public InheritableParamAttr {
protected:
  ParameterABIAttr(ASTContext &Context, const AttributeCommonInfo &CommonInfo,
                   attr::Kind AK, bool IsLateParsed,
                   bool InheritEvenIfAlreadyPresent)
      : InheritableParamAttr(Context, CommonInfo, AK, IsLateParsed,
                             InheritEvenIfAlreadyPresent) {}

public:
  ParameterABI getABI() const {
    switch (getKind()) {
    case attr::SwiftContext:
      return ParameterABI::SwiftContext;
    case attr::SwiftAsyncContext:
      return ParameterABI::SwiftAsyncContext;
    case attr::SwiftErrorResult:
      return ParameterABI::SwiftErrorResult;
    case attr::SwiftIndirectResult:
      return ParameterABI::SwiftIndirectResult;
    default:
      llvm_unreachable("bad parameter ABI attribute kind");
    }
  }

  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstParameterABIAttr &&
           A->getKind() <= attr::LastParameterABIAttr;
   }
};

/// A single parameter index whose accessors require each use to make explicit
/// the parameter index encoding needed.
class ParamIdx {
  // Idx is exposed only via accessors that specify specific encodings.
  unsigned Idx : 30;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasThis : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsValid : 1;

  void assertComparable(const ParamIdx &I) const {
    assert(isValid() && I.isValid() &&
           "ParamIdx must be valid to be compared");
    // It's possible to compare indices from separate functions, but so far
    // it's not proven useful.  Moreover, it might be confusing because a
    // comparison on the results of getASTIndex might be inconsistent with a
    // comparison on the ParamIdx objects themselves.
    assert(HasThis == I.HasThis &&
           "ParamIdx must be for the same function to be compared");
  }

public:
  /// Construct an invalid parameter index (\c isValid returns false and
  /// accessors fail an assert).
  ParamIdx() : Idx(0), HasThis(false), IsValid(false) {}

  /// \param Idx is the parameter index as it is normally specified in
  /// attributes in the source: one-origin including any C++ implicit this
  /// parameter.
  ///
  /// \param D is the declaration containing the parameters.  It is used to
  /// determine if there is a C++ implicit this parameter.
  ParamIdx(unsigned Idx, const Decl *D)
      : Idx(Idx), HasThis(false), IsValid(true) {
    assert(Idx >= 1 && "Idx must be one-origin");
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      HasThis = FD->isCXXInstanceMember();
  }

  /// A type into which \c ParamIdx can be serialized.
  ///
  /// A static assertion that it's of the correct size follows the \c ParamIdx
  /// class definition.
  typedef uint32_t SerialType;

  /// Produce a representation that can later be passed to \c deserialize to
  /// construct an equivalent \c ParamIdx.
  SerialType serialize() const {
    return *reinterpret_cast<const SerialType *>(this);
  }

  /// Construct from a result from \c serialize.
  static ParamIdx deserialize(SerialType S) {
    // Using this two-step static_cast via void * instead of reinterpret_cast
    // silences a -Wstrict-aliasing false positive from GCC7 and earlier.
    void *ParamIdxPtr = static_cast<void *>(&S);
    ParamIdx P(*static_cast<ParamIdx *>(ParamIdxPtr));
    assert((!P.IsValid || P.Idx >= 1) && "valid Idx must be one-origin");
    return P;
  }

  /// Is this parameter index valid?
  bool isValid() const { return IsValid; }

  /// Get the parameter index as it would normally be encoded for attributes at
  /// the source level of representation: one-origin including any C++ implicit
  /// this parameter.
  ///
  /// This encoding thus makes sense for diagnostics, pretty printing, and
  /// constructing new attributes from a source-like specification.
  unsigned getSourceIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    return Idx;
  }

  /// Get the parameter index as it would normally be encoded at the AST level
  /// of representation: zero-origin not including any C++ implicit this
  /// parameter.
  ///
  /// This is the encoding primarily used in Sema.  However, in diagnostics,
  /// Sema uses \c getSourceIndex instead.
  unsigned getASTIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    assert(Idx >= 1 + HasThis &&
           "stored index must be base-1 and not specify C++ implicit this");
    return Idx - 1 - HasThis;
  }

  /// Get the parameter index as it would normally be encoded at the LLVM level
  /// of representation: zero-origin including any C++ implicit this parameter.
  ///
  /// This is the encoding primarily used in CodeGen.
  unsigned getLLVMIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    assert(Idx >= 1 && "stored index must be base-1");
    return Idx - 1;
  }

  bool operator==(const ParamIdx &I) const {
    assertComparable(I);
    return Idx == I.Idx;
  }
  bool operator!=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx != I.Idx;
  }
  bool operator<(const ParamIdx &I) const {
    assertComparable(I);
    return Idx < I.Idx;
  }
  bool operator>(const ParamIdx &I) const {
    assertComparable(I);
    return Idx > I.Idx;
  }
  bool operator<=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx <= I.Idx;
  }
  bool operator>=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx >= I.Idx;
  }
};

static_assert(sizeof(ParamIdx) == sizeof(ParamIdx::SerialType),
              "ParamIdx does not fit its serialization type");

#include "clang/AST/Attrs.inc"

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const Attr *At) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(At), DiagnosticsEngine::ak_attr);
  return DB;
}
}  // end namespace clang

#endif
