//===- DeclCXX.h - Classes for representing C++ declarations --*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the C++ Decl subclasses, other than those for templates
/// (found in DeclTemplate.h) and friends (in DeclFriend.h).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLCXX_H
#define LLVM_CLANG_AST_DECLCXX_H

#include "clang/AST/ASTUnresolvedSet.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Redeclarable.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <vector>

namespace clang {

class ASTContext;
class ClassTemplateDecl;
class ConstructorUsingShadowDecl;
class CXXBasePath;
class CXXBasePaths;
class CXXConstructorDecl;
class CXXDestructorDecl;
class CXXFinalOverriderMap;
class CXXIndirectPrimaryBaseSet;
class CXXMethodDecl;
class DecompositionDecl;
class FriendDecl;
class FunctionTemplateDecl;
class IdentifierInfo;
class MemberSpecializationInfo;
class BaseUsingDecl;
class TemplateDecl;
class TemplateParameterList;
class UsingDecl;

/// Represents an access specifier followed by colon ':'.
///
/// An objects of this class represents sugar for the syntactic occurrence
/// of an access specifier followed by a colon in the list of member
/// specifiers of a C++ class definition.
///
/// Note that they do not represent other uses of access specifiers,
/// such as those occurring in a list of base specifiers.
/// Also note that this class has nothing to do with so-called
/// "access declarations" (C++98 11.3 [class.access.dcl]).
class AccessSpecDecl : public Decl {
  /// The location of the ':'.
  SourceLocation ColonLoc;

  AccessSpecDecl(AccessSpecifier AS, DeclContext *DC,
                 SourceLocation ASLoc, SourceLocation ColonLoc)
    : Decl(AccessSpec, DC, ASLoc), ColonLoc(ColonLoc) {
    setAccess(AS);
  }

  AccessSpecDecl(EmptyShell Empty) : Decl(AccessSpec, Empty) {}

  virtual void anchor();

public:
  /// The location of the access specifier.
  SourceLocation getAccessSpecifierLoc() const { return getLocation(); }

  /// Sets the location of the access specifier.
  void setAccessSpecifierLoc(SourceLocation ASLoc) { setLocation(ASLoc); }

  /// The location of the colon following the access specifier.
  SourceLocation getColonLoc() const { return ColonLoc; }

  /// Sets the location of the colon.
  void setColonLoc(SourceLocation CLoc) { ColonLoc = CLoc; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getAccessSpecifierLoc(), getColonLoc());
  }

  static AccessSpecDecl *Create(ASTContext &C, AccessSpecifier AS,
                                DeclContext *DC, SourceLocation ASLoc,
                                SourceLocation ColonLoc) {
    return new (C, DC) AccessSpecDecl(AS, DC, ASLoc, ColonLoc);
  }

  static AccessSpecDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == AccessSpec; }
};

/// Represents a base class of a C++ class.
///
/// Each CXXBaseSpecifier represents a single, direct base class (or
/// struct) of a C++ class (or struct). It specifies the type of that
/// base class, whether it is a virtual or non-virtual base, and what
/// level of access (public, protected, private) is used for the
/// derivation. For example:
///
/// \code
///   class A { };
///   class B { };
///   class C : public virtual A, protected B { };
/// \endcode
///
/// In this code, C will have two CXXBaseSpecifiers, one for "public
/// virtual A" and the other for "protected B".
class CXXBaseSpecifier {
  /// The source code range that covers the full base
  /// specifier, including the "virtual" (if present) and access
  /// specifier (if present).
  SourceRange Range;

  /// The source location of the ellipsis, if this is a pack
  /// expansion.
  SourceLocation EllipsisLoc;

  /// Whether this is a virtual base class or not.
  LLVM_PREFERRED_TYPE(bool)
  unsigned Virtual : 1;

  /// Whether this is the base of a class (true) or of a struct (false).
  ///
  /// This determines the mapping from the access specifier as written in the
  /// source code to the access specifier used for semantic analysis.
  LLVM_PREFERRED_TYPE(bool)
  unsigned BaseOfClass : 1;

  /// Access specifier as written in the source code (may be AS_none).
  ///
  /// The actual type of data stored here is an AccessSpecifier, but we use
  /// "unsigned" here to work around Microsoft ABI.
  LLVM_PREFERRED_TYPE(AccessSpecifier)
  unsigned Access : 2;

  /// Whether the class contains a using declaration
  /// to inherit the named class's constructors.
  LLVM_PREFERRED_TYPE(bool)
  unsigned InheritConstructors : 1;

  /// The type of the base class.
  ///
  /// This will be a class or struct (or a typedef of such). The source code
  /// range does not include the \c virtual or the access specifier.
  TypeSourceInfo *BaseTypeInfo;

public:
  CXXBaseSpecifier() = default;
  CXXBaseSpecifier(SourceRange R, bool V, bool BC, AccessSpecifier A,
                   TypeSourceInfo *TInfo, SourceLocation EllipsisLoc)
    : Range(R), EllipsisLoc(EllipsisLoc), Virtual(V), BaseOfClass(BC),
      Access(A), InheritConstructors(false), BaseTypeInfo(TInfo) {}

  /// Retrieves the source range that contains the entire base specifier.
  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  /// Get the location at which the base class type was written.
  SourceLocation getBaseTypeLoc() const LLVM_READONLY {
    return BaseTypeInfo->getTypeLoc().getBeginLoc();
  }

  /// Determines whether the base class is a virtual base class (or not).
  bool isVirtual() const { return Virtual; }

  /// Determine whether this base class is a base of a class declared
  /// with the 'class' keyword (vs. one declared with the 'struct' keyword).
  bool isBaseOfClass() const { return BaseOfClass; }

  /// Determine whether this base specifier is a pack expansion.
  bool isPackExpansion() const { return EllipsisLoc.isValid(); }

  /// Determine whether this base class's constructors get inherited.
  bool getInheritConstructors() const { return InheritConstructors; }

  /// Set that this base class's constructors should be inherited.
  void setInheritConstructors(bool Inherit = true) {
    InheritConstructors = Inherit;
  }

  /// For a pack expansion, determine the location of the ellipsis.
  SourceLocation getEllipsisLoc() const {
    return EllipsisLoc;
  }

  /// Returns the access specifier for this base specifier.
  ///
  /// This is the actual base specifier as used for semantic analysis, so
  /// the result can never be AS_none. To retrieve the access specifier as
  /// written in the source code, use getAccessSpecifierAsWritten().
  AccessSpecifier getAccessSpecifier() const {
    if ((AccessSpecifier)Access == AS_none)
      return BaseOfClass? AS_private : AS_public;
    else
      return (AccessSpecifier)Access;
  }

  /// Retrieves the access specifier as written in the source code
  /// (which may mean that no access specifier was explicitly written).
  ///
  /// Use getAccessSpecifier() to retrieve the access specifier for use in
  /// semantic analysis.
  AccessSpecifier getAccessSpecifierAsWritten() const {
    return (AccessSpecifier)Access;
  }

  /// Retrieves the type of the base class.
  ///
  /// This type will always be an unqualified class type.
  QualType getType() const {
    return BaseTypeInfo->getType().getUnqualifiedType();
  }

  /// Retrieves the type and source location of the base class.
  TypeSourceInfo *getTypeSourceInfo() const { return BaseTypeInfo; }
};

/// Represents a C++ struct/union/class.
class CXXRecordDecl : public RecordDecl {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend class ASTNodeImporter;
  friend class ASTReader;
  friend class ASTRecordWriter;
  friend class ASTWriter;
  friend class DeclContext;
  friend class LambdaExpr;
  friend class ODRDiagsEmitter;

  friend void FunctionDecl::setIsPureVirtual(bool);
  friend void TagDecl::startDefinition();

  /// Values used in DefinitionData fields to represent special members.
  enum SpecialMemberFlags {
    SMF_DefaultConstructor = 0x1,
    SMF_CopyConstructor = 0x2,
    SMF_MoveConstructor = 0x4,
    SMF_CopyAssignment = 0x8,
    SMF_MoveAssignment = 0x10,
    SMF_Destructor = 0x20,
    SMF_All = 0x3f
  };

public:
  enum LambdaDependencyKind {
    LDK_Unknown = 0,
    LDK_AlwaysDependent,
    LDK_NeverDependent,
  };

private:
  struct DefinitionData {
    #define FIELD(Name, Width, Merge) \
    unsigned Name : Width;
    #include "CXXRecordDeclDefinitionBits.def"

    /// Whether this class describes a C++ lambda.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsLambda : 1;

    /// Whether we are currently parsing base specifiers.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsParsingBaseSpecifiers : 1;

    /// True when visible conversion functions are already computed
    /// and are available.
    LLVM_PREFERRED_TYPE(bool)
    unsigned ComputedVisibleConversions : 1;

    LLVM_PREFERRED_TYPE(bool)
    unsigned HasODRHash : 1;

    /// A hash of parts of the class to help in ODR checking.
    unsigned ODRHash = 0;

    /// The number of base class specifiers in Bases.
    unsigned NumBases = 0;

    /// The number of virtual base class specifiers in VBases.
    unsigned NumVBases = 0;

    /// Base classes of this class.
    ///
    /// FIXME: This is wasted space for a union.
    LazyCXXBaseSpecifiersPtr Bases;

    /// direct and indirect virtual base classes of this class.
    LazyCXXBaseSpecifiersPtr VBases;

    /// The conversion functions of this C++ class (but not its
    /// inherited conversion functions).
    ///
    /// Each of the entries in this overload set is a CXXConversionDecl.
    LazyASTUnresolvedSet Conversions;

    /// The conversion functions of this C++ class and all those
    /// inherited conversion functions that are visible in this class.
    ///
    /// Each of the entries in this overload set is a CXXConversionDecl or a
    /// FunctionTemplateDecl.
    LazyASTUnresolvedSet VisibleConversions;

    /// The declaration which defines this record.
    CXXRecordDecl *Definition;

    /// The first friend declaration in this class, or null if there
    /// aren't any.
    ///
    /// This is actually currently stored in reverse order.
    LazyDeclPtr FirstFriend;

    DefinitionData(CXXRecordDecl *D);

    /// Retrieve the set of direct base classes.
    CXXBaseSpecifier *getBases() const {
      if (!Bases.isOffset())
        return Bases.get(nullptr);
      return getBasesSlowCase();
    }

    /// Retrieve the set of virtual base classes.
    CXXBaseSpecifier *getVBases() const {
      if (!VBases.isOffset())
        return VBases.get(nullptr);
      return getVBasesSlowCase();
    }

    ArrayRef<CXXBaseSpecifier> bases() const {
      return llvm::ArrayRef(getBases(), NumBases);
    }

    ArrayRef<CXXBaseSpecifier> vbases() const {
      return llvm::ArrayRef(getVBases(), NumVBases);
    }

  private:
    CXXBaseSpecifier *getBasesSlowCase() const;
    CXXBaseSpecifier *getVBasesSlowCase() const;
  };

  struct DefinitionData *DefinitionData;

  /// Describes a C++ closure type (generated by a lambda expression).
  struct LambdaDefinitionData : public DefinitionData {
    using Capture = LambdaCapture;

    /// Whether this lambda is known to be dependent, even if its
    /// context isn't dependent.
    ///
    /// A lambda with a non-dependent context can be dependent if it occurs
    /// within the default argument of a function template, because the
    /// lambda will have been created with the enclosing context as its
    /// declaration context, rather than function. This is an unfortunate
    /// artifact of having to parse the default arguments before.
    LLVM_PREFERRED_TYPE(LambdaDependencyKind)
    unsigned DependencyKind : 2;

    /// Whether this lambda is a generic lambda.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsGenericLambda : 1;

    /// The Default Capture.
    LLVM_PREFERRED_TYPE(LambdaCaptureDefault)
    unsigned CaptureDefault : 2;

    /// The number of captures in this lambda is limited 2^NumCaptures.
    unsigned NumCaptures : 15;

    /// The number of explicit captures in this lambda.
    unsigned NumExplicitCaptures : 12;

    /// Has known `internal` linkage.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasKnownInternalLinkage : 1;

    /// The number used to indicate this lambda expression for name
    /// mangling in the Itanium C++ ABI.
    unsigned ManglingNumber : 31;

    /// The index of this lambda within its context declaration. This is not in
    /// general the same as the mangling number.
    unsigned IndexInContext;

    /// The declaration that provides context for this lambda, if the
    /// actual DeclContext does not suffice. This is used for lambdas that
    /// occur within default arguments of function parameters within the class
    /// or within a data member initializer.
    LazyDeclPtr ContextDecl;

    /// The lists of captures, both explicit and implicit, for this
    /// lambda. One list is provided for each merged copy of the lambda.
    /// The first list corresponds to the canonical definition.
    /// The destructor is registered by AddCaptureList when necessary.
    llvm::TinyPtrVector<Capture*> Captures;

    /// The type of the call method.
    TypeSourceInfo *MethodTyInfo;

    LambdaDefinitionData(CXXRecordDecl *D, TypeSourceInfo *Info, unsigned DK,
                         bool IsGeneric, LambdaCaptureDefault CaptureDefault)
        : DefinitionData(D), DependencyKind(DK), IsGenericLambda(IsGeneric),
          CaptureDefault(CaptureDefault), NumCaptures(0),
          NumExplicitCaptures(0), HasKnownInternalLinkage(0), ManglingNumber(0),
          IndexInContext(0), MethodTyInfo(Info) {
      IsLambda = true;

      // C++1z [expr.prim.lambda]p4:
      //   This class type is not an aggregate type.
      Aggregate = false;
      PlainOldData = false;
    }

    // Add a list of captures.
    void AddCaptureList(ASTContext &Ctx, Capture *CaptureList);
  };

  struct DefinitionData *dataPtr() const {
    // Complete the redecl chain (if necessary).
    getMostRecentDecl();
    return DefinitionData;
  }

  struct DefinitionData &data() const {
    auto *DD = dataPtr();
    assert(DD && "queried property of class with no definition");
    return *DD;
  }

  struct LambdaDefinitionData &getLambdaData() const {
    // No update required: a merged definition cannot change any lambda
    // properties.
    auto *DD = DefinitionData;
    assert(DD && DD->IsLambda && "queried lambda property of non-lambda class");
    return static_cast<LambdaDefinitionData&>(*DD);
  }

  /// The template or declaration that this declaration
  /// describes or was instantiated from, respectively.
  ///
  /// For non-templates, this value will be null. For record
  /// declarations that describe a class template, this will be a
  /// pointer to a ClassTemplateDecl. For member
  /// classes of class template specializations, this will be the
  /// MemberSpecializationInfo referring to the member class that was
  /// instantiated or specialized.
  llvm::PointerUnion<ClassTemplateDecl *, MemberSpecializationInfo *>
      TemplateOrInstantiation;

  /// Called from setBases and addedMember to notify the class that a
  /// direct or virtual base class or a member of class type has been added.
  void addedClassSubobject(CXXRecordDecl *Base);

  /// Notify the class that member has been added.
  ///
  /// This routine helps maintain information about the class based on which
  /// members have been added. It will be invoked by DeclContext::addDecl()
  /// whenever a member is added to this record.
  void addedMember(Decl *D);

  void markedVirtualFunctionPure();

  /// Get the head of our list of friend declarations, possibly
  /// deserializing the friends from an external AST source.
  FriendDecl *getFirstFriend() const;

  /// Determine whether this class has an empty base class subobject of type X
  /// or of one of the types that might be at offset 0 within X (per the C++
  /// "standard layout" rules).
  bool hasSubobjectAtOffsetZeroOfEmptyBaseType(ASTContext &Ctx,
                                               const CXXRecordDecl *X);

protected:
  CXXRecordDecl(Kind K, TagKind TK, const ASTContext &C, DeclContext *DC,
                SourceLocation StartLoc, SourceLocation IdLoc,
                IdentifierInfo *Id, CXXRecordDecl *PrevDecl);

public:
  /// Iterator that traverses the base classes of a class.
  using base_class_iterator = CXXBaseSpecifier *;

  /// Iterator that traverses the base classes of a class.
  using base_class_const_iterator = const CXXBaseSpecifier *;

  CXXRecordDecl *getCanonicalDecl() override {
    return cast<CXXRecordDecl>(RecordDecl::getCanonicalDecl());
  }

  const CXXRecordDecl *getCanonicalDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getCanonicalDecl();
  }

  CXXRecordDecl *getPreviousDecl() {
    return cast_or_null<CXXRecordDecl>(
            static_cast<RecordDecl *>(this)->getPreviousDecl());
  }

  const CXXRecordDecl *getPreviousDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getPreviousDecl();
  }

  CXXRecordDecl *getMostRecentDecl() {
    return cast<CXXRecordDecl>(
            static_cast<RecordDecl *>(this)->getMostRecentDecl());
  }

  const CXXRecordDecl *getMostRecentDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getMostRecentDecl();
  }

  CXXRecordDecl *getMostRecentNonInjectedDecl() {
    CXXRecordDecl *Recent =
        static_cast<CXXRecordDecl *>(this)->getMostRecentDecl();
    while (Recent->isInjectedClassName()) {
      // FIXME: Does injected class name need to be in the redeclarations chain?
      assert(Recent->getPreviousDecl());
      Recent = Recent->getPreviousDecl();
    }
    return Recent;
  }

  const CXXRecordDecl *getMostRecentNonInjectedDecl() const {
    return const_cast<CXXRecordDecl*>(this)->getMostRecentNonInjectedDecl();
  }

  CXXRecordDecl *getDefinition() const {
    // We only need an update if we don't already know which
    // declaration is the definition.
    auto *DD = DefinitionData ? DefinitionData : dataPtr();
    return DD ? DD->Definition : nullptr;
  }

  bool hasDefinition() const { return DefinitionData || dataPtr(); }

  static CXXRecordDecl *Create(const ASTContext &C, TagKind TK, DeclContext *DC,
                               SourceLocation StartLoc, SourceLocation IdLoc,
                               IdentifierInfo *Id,
                               CXXRecordDecl *PrevDecl = nullptr,
                               bool DelayTypeCreation = false);
  static CXXRecordDecl *CreateLambda(const ASTContext &C, DeclContext *DC,
                                     TypeSourceInfo *Info, SourceLocation Loc,
                                     unsigned DependencyKind, bool IsGeneric,
                                     LambdaCaptureDefault CaptureDefault);
  static CXXRecordDecl *CreateDeserialized(const ASTContext &C,
                                           GlobalDeclID ID);

  bool isDynamicClass() const {
    return data().Polymorphic || data().NumVBases != 0;
  }

  /// @returns true if class is dynamic or might be dynamic because the
  /// definition is incomplete of dependent.
  bool mayBeDynamicClass() const {
    return !hasDefinition() || isDynamicClass() || hasAnyDependentBases();
  }

  /// @returns true if class is non dynamic or might be non dynamic because the
  /// definition is incomplete of dependent.
  bool mayBeNonDynamicClass() const {
    return !hasDefinition() || !isDynamicClass() || hasAnyDependentBases();
  }

  void setIsParsingBaseSpecifiers() { data().IsParsingBaseSpecifiers = true; }

  bool isParsingBaseSpecifiers() const {
    return data().IsParsingBaseSpecifiers;
  }

  unsigned getODRHash() const;

  /// Sets the base classes of this struct or class.
  void setBases(CXXBaseSpecifier const * const *Bases, unsigned NumBases);

  /// Retrieves the number of base classes of this class.
  unsigned getNumBases() const { return data().NumBases; }

  using base_class_range = llvm::iterator_range<base_class_iterator>;
  using base_class_const_range =
      llvm::iterator_range<base_class_const_iterator>;

  base_class_range bases() {
    return base_class_range(bases_begin(), bases_end());
  }
  base_class_const_range bases() const {
    return base_class_const_range(bases_begin(), bases_end());
  }

  base_class_iterator bases_begin() { return data().getBases(); }
  base_class_const_iterator bases_begin() const { return data().getBases(); }
  base_class_iterator bases_end() { return bases_begin() + data().NumBases; }
  base_class_const_iterator bases_end() const {
    return bases_begin() + data().NumBases;
  }

  /// Retrieves the number of virtual base classes of this class.
  unsigned getNumVBases() const { return data().NumVBases; }

  base_class_range vbases() {
    return base_class_range(vbases_begin(), vbases_end());
  }
  base_class_const_range vbases() const {
    return base_class_const_range(vbases_begin(), vbases_end());
  }

  base_class_iterator vbases_begin() { return data().getVBases(); }
  base_class_const_iterator vbases_begin() const { return data().getVBases(); }
  base_class_iterator vbases_end() { return vbases_begin() + data().NumVBases; }
  base_class_const_iterator vbases_end() const {
    return vbases_begin() + data().NumVBases;
  }

  /// Determine whether this class has any dependent base classes which
  /// are not the current instantiation.
  bool hasAnyDependentBases() const;

  /// Iterator access to method members.  The method iterator visits
  /// all method members of the class, including non-instance methods,
  /// special methods, etc.
  using method_iterator = specific_decl_iterator<CXXMethodDecl>;
  using method_range =
      llvm::iterator_range<specific_decl_iterator<CXXMethodDecl>>;

  method_range methods() const {
    return method_range(method_begin(), method_end());
  }

  /// Method begin iterator.  Iterates in the order the methods
  /// were declared.
  method_iterator method_begin() const {
    return method_iterator(decls_begin());
  }

  /// Method past-the-end iterator.
  method_iterator method_end() const {
    return method_iterator(decls_end());
  }

  /// Iterator access to constructor members.
  using ctor_iterator = specific_decl_iterator<CXXConstructorDecl>;
  using ctor_range =
      llvm::iterator_range<specific_decl_iterator<CXXConstructorDecl>>;

  ctor_range ctors() const { return ctor_range(ctor_begin(), ctor_end()); }

  ctor_iterator ctor_begin() const {
    return ctor_iterator(decls_begin());
  }

  ctor_iterator ctor_end() const {
    return ctor_iterator(decls_end());
  }

  /// An iterator over friend declarations.  All of these are defined
  /// in DeclFriend.h.
  class friend_iterator;
  using friend_range = llvm::iterator_range<friend_iterator>;

  friend_range friends() const;
  friend_iterator friend_begin() const;
  friend_iterator friend_end() const;
  void pushFriendDecl(FriendDecl *FD);

  /// Determines whether this record has any friends.
  bool hasFriends() const {
    return data().FirstFriend.isValid();
  }

  /// \c true if a defaulted copy constructor for this class would be
  /// deleted.
  bool defaultedCopyConstructorIsDeleted() const {
    assert((!needsOverloadResolutionForCopyConstructor() ||
            (data().DeclaredSpecialMembers & SMF_CopyConstructor)) &&
           "this property has not yet been computed by Sema");
    return data().DefaultedCopyConstructorIsDeleted;
  }

  /// \c true if a defaulted move constructor for this class would be
  /// deleted.
  bool defaultedMoveConstructorIsDeleted() const {
    assert((!needsOverloadResolutionForMoveConstructor() ||
            (data().DeclaredSpecialMembers & SMF_MoveConstructor)) &&
           "this property has not yet been computed by Sema");
    return data().DefaultedMoveConstructorIsDeleted;
  }

  /// \c true if a defaulted destructor for this class would be deleted.
  bool defaultedDestructorIsDeleted() const {
    assert((!needsOverloadResolutionForDestructor() ||
            (data().DeclaredSpecialMembers & SMF_Destructor)) &&
           "this property has not yet been computed by Sema");
    return data().DefaultedDestructorIsDeleted;
  }

  /// \c true if we know for sure that this class has a single,
  /// accessible, unambiguous copy constructor that is not deleted.
  bool hasSimpleCopyConstructor() const {
    return !hasUserDeclaredCopyConstructor() &&
           !data().DefaultedCopyConstructorIsDeleted;
  }

  /// \c true if we know for sure that this class has a single,
  /// accessible, unambiguous move constructor that is not deleted.
  bool hasSimpleMoveConstructor() const {
    return !hasUserDeclaredMoveConstructor() && hasMoveConstructor() &&
           !data().DefaultedMoveConstructorIsDeleted;
  }

  /// \c true if we know for sure that this class has a single,
  /// accessible, unambiguous copy assignment operator that is not deleted.
  bool hasSimpleCopyAssignment() const {
    return !hasUserDeclaredCopyAssignment() &&
           !data().DefaultedCopyAssignmentIsDeleted;
  }

  /// \c true if we know for sure that this class has a single,
  /// accessible, unambiguous move assignment operator that is not deleted.
  bool hasSimpleMoveAssignment() const {
    return !hasUserDeclaredMoveAssignment() && hasMoveAssignment() &&
           !data().DefaultedMoveAssignmentIsDeleted;
  }

  /// \c true if we know for sure that this class has an accessible
  /// destructor that is not deleted.
  bool hasSimpleDestructor() const {
    return !hasUserDeclaredDestructor() &&
           !data().DefaultedDestructorIsDeleted;
  }

  /// Determine whether this class has any default constructors.
  bool hasDefaultConstructor() const {
    return (data().DeclaredSpecialMembers & SMF_DefaultConstructor) ||
           needsImplicitDefaultConstructor();
  }

  /// Determine if we need to declare a default constructor for
  /// this class.
  ///
  /// This value is used for lazy creation of default constructors.
  bool needsImplicitDefaultConstructor() const {
    return (!data().UserDeclaredConstructor &&
            !(data().DeclaredSpecialMembers & SMF_DefaultConstructor) &&
            (!isLambda() || lambdaIsDefaultConstructibleAndAssignable())) ||
           // FIXME: Proposed fix to core wording issue: if a class inherits
           // a default constructor and doesn't explicitly declare one, one
           // is declared implicitly.
           (data().HasInheritedDefaultConstructor &&
            !(data().DeclaredSpecialMembers & SMF_DefaultConstructor));
  }

  /// Determine whether this class has any user-declared constructors.
  ///
  /// When true, a default constructor will not be implicitly declared.
  bool hasUserDeclaredConstructor() const {
    return data().UserDeclaredConstructor;
  }

  /// Whether this class has a user-provided default constructor
  /// per C++11.
  bool hasUserProvidedDefaultConstructor() const {
    return data().UserProvidedDefaultConstructor;
  }

  /// Determine whether this class has a user-declared copy constructor.
  ///
  /// When false, a copy constructor will be implicitly declared.
  bool hasUserDeclaredCopyConstructor() const {
    return data().UserDeclaredSpecialMembers & SMF_CopyConstructor;
  }

  /// Determine whether this class needs an implicit copy
  /// constructor to be lazily declared.
  bool needsImplicitCopyConstructor() const {
    return !(data().DeclaredSpecialMembers & SMF_CopyConstructor);
  }

  /// Determine whether we need to eagerly declare a defaulted copy
  /// constructor for this class.
  bool needsOverloadResolutionForCopyConstructor() const {
    // C++17 [class.copy.ctor]p6:
    //   If the class definition declares a move constructor or move assignment
    //   operator, the implicitly declared copy constructor is defined as
    //   deleted.
    // In MSVC mode, sometimes a declared move assignment does not delete an
    // implicit copy constructor, so defer this choice to Sema.
    if (data().UserDeclaredSpecialMembers &
        (SMF_MoveConstructor | SMF_MoveAssignment))
      return true;
    return data().NeedOverloadResolutionForCopyConstructor;
  }

  /// Determine whether an implicit copy constructor for this type
  /// would have a parameter with a const-qualified reference type.
  bool implicitCopyConstructorHasConstParam() const {
    return data().ImplicitCopyConstructorCanHaveConstParamForNonVBase &&
           (isAbstract() ||
            data().ImplicitCopyConstructorCanHaveConstParamForVBase);
  }

  /// Determine whether this class has a copy constructor with
  /// a parameter type which is a reference to a const-qualified type.
  bool hasCopyConstructorWithConstParam() const {
    return data().HasDeclaredCopyConstructorWithConstParam ||
           (needsImplicitCopyConstructor() &&
            implicitCopyConstructorHasConstParam());
  }

  /// Whether this class has a user-declared move constructor or
  /// assignment operator.
  ///
  /// When false, a move constructor and assignment operator may be
  /// implicitly declared.
  bool hasUserDeclaredMoveOperation() const {
    return data().UserDeclaredSpecialMembers &
             (SMF_MoveConstructor | SMF_MoveAssignment);
  }

  /// Determine whether this class has had a move constructor
  /// declared by the user.
  bool hasUserDeclaredMoveConstructor() const {
    return data().UserDeclaredSpecialMembers & SMF_MoveConstructor;
  }

  /// Determine whether this class has a move constructor.
  bool hasMoveConstructor() const {
    return (data().DeclaredSpecialMembers & SMF_MoveConstructor) ||
           needsImplicitMoveConstructor();
  }

  /// Set that we attempted to declare an implicit copy
  /// constructor, but overload resolution failed so we deleted it.
  void setImplicitCopyConstructorIsDeleted() {
    assert((data().DefaultedCopyConstructorIsDeleted ||
            needsOverloadResolutionForCopyConstructor()) &&
           "Copy constructor should not be deleted");
    data().DefaultedCopyConstructorIsDeleted = true;
  }

  /// Set that we attempted to declare an implicit move
  /// constructor, but overload resolution failed so we deleted it.
  void setImplicitMoveConstructorIsDeleted() {
    assert((data().DefaultedMoveConstructorIsDeleted ||
            needsOverloadResolutionForMoveConstructor()) &&
           "move constructor should not be deleted");
    data().DefaultedMoveConstructorIsDeleted = true;
  }

  /// Set that we attempted to declare an implicit destructor,
  /// but overload resolution failed so we deleted it.
  void setImplicitDestructorIsDeleted() {
    assert((data().DefaultedDestructorIsDeleted ||
            needsOverloadResolutionForDestructor()) &&
           "destructor should not be deleted");
    data().DefaultedDestructorIsDeleted = true;
  }

  /// Determine whether this class should get an implicit move
  /// constructor or if any existing special member function inhibits this.
  bool needsImplicitMoveConstructor() const {
    return !(data().DeclaredSpecialMembers & SMF_MoveConstructor) &&
           !hasUserDeclaredCopyConstructor() &&
           !hasUserDeclaredCopyAssignment() &&
           !hasUserDeclaredMoveAssignment() &&
           !hasUserDeclaredDestructor();
  }

  /// Determine whether we need to eagerly declare a defaulted move
  /// constructor for this class.
  bool needsOverloadResolutionForMoveConstructor() const {
    return data().NeedOverloadResolutionForMoveConstructor;
  }

  /// Determine whether this class has a user-declared copy assignment
  /// operator.
  ///
  /// When false, a copy assignment operator will be implicitly declared.
  bool hasUserDeclaredCopyAssignment() const {
    return data().UserDeclaredSpecialMembers & SMF_CopyAssignment;
  }

  /// Set that we attempted to declare an implicit copy assignment
  /// operator, but overload resolution failed so we deleted it.
  void setImplicitCopyAssignmentIsDeleted() {
    assert((data().DefaultedCopyAssignmentIsDeleted ||
            needsOverloadResolutionForCopyAssignment()) &&
           "copy assignment should not be deleted");
    data().DefaultedCopyAssignmentIsDeleted = true;
  }

  /// Determine whether this class needs an implicit copy
  /// assignment operator to be lazily declared.
  bool needsImplicitCopyAssignment() const {
    return !(data().DeclaredSpecialMembers & SMF_CopyAssignment);
  }

  /// Determine whether we need to eagerly declare a defaulted copy
  /// assignment operator for this class.
  bool needsOverloadResolutionForCopyAssignment() const {
    // C++20 [class.copy.assign]p2:
    //   If the class definition declares a move constructor or move assignment
    //   operator, the implicitly declared copy assignment operator is defined
    //   as deleted.
    // In MSVC mode, sometimes a declared move constructor does not delete an
    // implicit copy assignment, so defer this choice to Sema.
    if (data().UserDeclaredSpecialMembers &
        (SMF_MoveConstructor | SMF_MoveAssignment))
      return true;
    return data().NeedOverloadResolutionForCopyAssignment;
  }

  /// Determine whether an implicit copy assignment operator for this
  /// type would have a parameter with a const-qualified reference type.
  bool implicitCopyAssignmentHasConstParam() const {
    return data().ImplicitCopyAssignmentHasConstParam;
  }

  /// Determine whether this class has a copy assignment operator with
  /// a parameter type which is a reference to a const-qualified type or is not
  /// a reference.
  bool hasCopyAssignmentWithConstParam() const {
    return data().HasDeclaredCopyAssignmentWithConstParam ||
           (needsImplicitCopyAssignment() &&
            implicitCopyAssignmentHasConstParam());
  }

  /// Determine whether this class has had a move assignment
  /// declared by the user.
  bool hasUserDeclaredMoveAssignment() const {
    return data().UserDeclaredSpecialMembers & SMF_MoveAssignment;
  }

  /// Determine whether this class has a move assignment operator.
  bool hasMoveAssignment() const {
    return (data().DeclaredSpecialMembers & SMF_MoveAssignment) ||
           needsImplicitMoveAssignment();
  }

  /// Set that we attempted to declare an implicit move assignment
  /// operator, but overload resolution failed so we deleted it.
  void setImplicitMoveAssignmentIsDeleted() {
    assert((data().DefaultedMoveAssignmentIsDeleted ||
            needsOverloadResolutionForMoveAssignment()) &&
           "move assignment should not be deleted");
    data().DefaultedMoveAssignmentIsDeleted = true;
  }

  /// Determine whether this class should get an implicit move
  /// assignment operator or if any existing special member function inhibits
  /// this.
  bool needsImplicitMoveAssignment() const {
    return !(data().DeclaredSpecialMembers & SMF_MoveAssignment) &&
           !hasUserDeclaredCopyConstructor() &&
           !hasUserDeclaredCopyAssignment() &&
           !hasUserDeclaredMoveConstructor() &&
           !hasUserDeclaredDestructor() &&
           (!isLambda() || lambdaIsDefaultConstructibleAndAssignable());
  }

  /// Determine whether we need to eagerly declare a move assignment
  /// operator for this class.
  bool needsOverloadResolutionForMoveAssignment() const {
    return data().NeedOverloadResolutionForMoveAssignment;
  }

  /// Determine whether this class has a user-declared destructor.
  ///
  /// When false, a destructor will be implicitly declared.
  bool hasUserDeclaredDestructor() const {
    return data().UserDeclaredSpecialMembers & SMF_Destructor;
  }

  /// Determine whether this class needs an implicit destructor to
  /// be lazily declared.
  bool needsImplicitDestructor() const {
    return !(data().DeclaredSpecialMembers & SMF_Destructor);
  }

  /// Determine whether we need to eagerly declare a destructor for this
  /// class.
  bool needsOverloadResolutionForDestructor() const {
    return data().NeedOverloadResolutionForDestructor;
  }

  /// Determine whether this class describes a lambda function object.
  bool isLambda() const {
    // An update record can't turn a non-lambda into a lambda.
    auto *DD = DefinitionData;
    return DD && DD->IsLambda;
  }

  /// Determine whether this class describes a generic
  /// lambda function object (i.e. function call operator is
  /// a template).
  bool isGenericLambda() const;

  /// Determine whether this lambda should have an implicit default constructor
  /// and copy and move assignment operators.
  bool lambdaIsDefaultConstructibleAndAssignable() const;

  /// Retrieve the lambda call operator of the closure type
  /// if this is a closure type.
  CXXMethodDecl *getLambdaCallOperator() const;

  /// Retrieve the dependent lambda call operator of the closure type
  /// if this is a templated closure type.
  FunctionTemplateDecl *getDependentLambdaCallOperator() const;

  /// Retrieve the lambda static invoker, the address of which
  /// is returned by the conversion operator, and the body of which
  /// is forwarded to the lambda call operator. The version that does not
  /// take a calling convention uses the 'default' calling convention for free
  /// functions if the Lambda's calling convention was not modified via
  /// attribute. Otherwise, it will return the calling convention specified for
  /// the lambda.
  CXXMethodDecl *getLambdaStaticInvoker() const;
  CXXMethodDecl *getLambdaStaticInvoker(CallingConv CC) const;

  /// Retrieve the generic lambda's template parameter list.
  /// Returns null if the class does not represent a lambda or a generic
  /// lambda.
  TemplateParameterList *getGenericLambdaTemplateParameterList() const;

  /// Retrieve the lambda template parameters that were specified explicitly.
  ArrayRef<NamedDecl *> getLambdaExplicitTemplateParameters() const;

  LambdaCaptureDefault getLambdaCaptureDefault() const {
    assert(isLambda());
    return static_cast<LambdaCaptureDefault>(getLambdaData().CaptureDefault);
  }

  bool isCapturelessLambda() const {
    if (!isLambda())
      return false;
    return getLambdaCaptureDefault() == LCD_None && capture_size() == 0;
  }

  /// Set the captures for this lambda closure type.
  void setCaptures(ASTContext &Context, ArrayRef<LambdaCapture> Captures);

  /// For a closure type, retrieve the mapping from captured
  /// variables and \c this to the non-static data members that store the
  /// values or references of the captures.
  ///
  /// \param Captures Will be populated with the mapping from captured
  /// variables to the corresponding fields.
  ///
  /// \param ThisCapture Will be set to the field declaration for the
  /// \c this capture.
  ///
  /// \note No entries will be added for init-captures, as they do not capture
  /// variables.
  ///
  /// \note If multiple versions of the lambda are merged together, they may
  /// have different variable declarations corresponding to the same capture.
  /// In that case, all of those variable declarations will be added to the
  /// Captures list, so it may have more than one variable listed per field.
  void
  getCaptureFields(llvm::DenseMap<const ValueDecl *, FieldDecl *> &Captures,
                   FieldDecl *&ThisCapture) const;

  using capture_const_iterator = const LambdaCapture *;
  using capture_const_range = llvm::iterator_range<capture_const_iterator>;

  capture_const_range captures() const {
    return capture_const_range(captures_begin(), captures_end());
  }

  capture_const_iterator captures_begin() const {
    if (!isLambda()) return nullptr;
    LambdaDefinitionData &LambdaData = getLambdaData();
    return LambdaData.Captures.empty() ? nullptr : LambdaData.Captures.front();
  }

  capture_const_iterator captures_end() const {
    return isLambda() ? captures_begin() + getLambdaData().NumCaptures
                      : nullptr;
  }

  unsigned capture_size() const { return getLambdaData().NumCaptures; }

  const LambdaCapture *getCapture(unsigned I) const {
    assert(isLambda() && I < capture_size() && "invalid index for capture");
    return captures_begin() + I;
  }

  using conversion_iterator = UnresolvedSetIterator;

  conversion_iterator conversion_begin() const {
    return data().Conversions.get(getASTContext()).begin();
  }

  conversion_iterator conversion_end() const {
    return data().Conversions.get(getASTContext()).end();
  }

  /// Removes a conversion function from this class.  The conversion
  /// function must currently be a member of this class.  Furthermore,
  /// this class must currently be in the process of being defined.
  void removeConversion(const NamedDecl *Old);

  /// Get all conversion functions visible in current class,
  /// including conversion function templates.
  llvm::iterator_range<conversion_iterator>
  getVisibleConversionFunctions() const;

  /// Determine whether this class is an aggregate (C++ [dcl.init.aggr]),
  /// which is a class with no user-declared constructors, no private
  /// or protected non-static data members, no base classes, and no virtual
  /// functions (C++ [dcl.init.aggr]p1).
  bool isAggregate() const { return data().Aggregate; }

  /// Whether this class has any in-class initializers
  /// for non-static data members (including those in anonymous unions or
  /// structs).
  bool hasInClassInitializer() const { return data().HasInClassInitializer; }

  /// Whether this class or any of its subobjects has any members of
  /// reference type which would make value-initialization ill-formed.
  ///
  /// Per C++03 [dcl.init]p5:
  ///  - if T is a non-union class type without a user-declared constructor,
  ///    then every non-static data member and base-class component of T is
  ///    value-initialized [...] A program that calls for [...]
  ///    value-initialization of an entity of reference type is ill-formed.
  bool hasUninitializedReferenceMember() const {
    return !isUnion() && !hasUserDeclaredConstructor() &&
           data().HasUninitializedReferenceMember;
  }

  /// Whether this class is a POD-type (C++ [class]p4)
  ///
  /// For purposes of this function a class is POD if it is an aggregate
  /// that has no non-static non-POD data members, no reference data
  /// members, no user-defined copy assignment operator and no
  /// user-defined destructor.
  ///
  /// Note that this is the C++ TR1 definition of POD.
  bool isPOD() const { return data().PlainOldData; }

  /// True if this class is C-like, without C++-specific features, e.g.
  /// it contains only public fields, no bases, tag kind is not 'class', etc.
  bool isCLike() const;

  /// Determine whether this is an empty class in the sense of
  /// (C++11 [meta.unary.prop]).
  ///
  /// The CXXRecordDecl is a class type, but not a union type,
  /// with no non-static data members other than bit-fields of length 0,
  /// no virtual member functions, no virtual base classes,
  /// and no base class B for which is_empty<B>::value is false.
  ///
  /// \note This does NOT include a check for union-ness.
  bool isEmpty() const { return data().Empty; }
  /// Marks this record as empty. This is used by DWARFASTParserClang
  /// when parsing records with empty fields having [[no_unique_address]]
  /// attribute
  void markEmpty() { data().Empty = true; }

  void setInitMethod(bool Val) { data().HasInitMethod = Val; }
  bool hasInitMethod() const { return data().HasInitMethod; }

  bool hasPrivateFields() const {
    return data().HasPrivateFields;
  }

  bool hasProtectedFields() const {
    return data().HasProtectedFields;
  }

  /// Determine whether this class has direct non-static data members.
  bool hasDirectFields() const {
    auto &D = data();
    return D.HasPublicFields || D.HasProtectedFields || D.HasPrivateFields;
  }

  /// If this is a standard-layout class or union, any and all data members will
  /// be declared in the same type.
  ///
  /// This retrieves the type where any fields are declared,
  /// or the current class if there is no class with fields.
  const CXXRecordDecl *getStandardLayoutBaseWithFields() const;

  /// Whether this class is polymorphic (C++ [class.virtual]),
  /// which means that the class contains or inherits a virtual function.
  bool isPolymorphic() const { return data().Polymorphic; }

  /// Determine whether this class has a pure virtual function.
  ///
  /// The class is abstract per (C++ [class.abstract]p2) if it declares
  /// a pure virtual function or inherits a pure virtual function that is
  /// not overridden.
  bool isAbstract() const { return data().Abstract; }

  /// Determine whether this class is standard-layout per
  /// C++ [class]p7.
  bool isStandardLayout() const { return data().IsStandardLayout; }

  /// Determine whether this class was standard-layout per
  /// C++11 [class]p7, specifically using the C++11 rules without any DRs.
  bool isCXX11StandardLayout() const { return data().IsCXX11StandardLayout; }

  /// Determine whether this class, or any of its class subobjects,
  /// contains a mutable field.
  bool hasMutableFields() const { return data().HasMutableFields; }

  /// Determine whether this class has any variant members.
  bool hasVariantMembers() const { return data().HasVariantMembers; }

  /// Determine whether this class has a trivial default constructor
  /// (C++11 [class.ctor]p5).
  bool hasTrivialDefaultConstructor() const {
    return hasDefaultConstructor() &&
           (data().HasTrivialSpecialMembers & SMF_DefaultConstructor);
  }

  /// Determine whether this class has a non-trivial default constructor
  /// (C++11 [class.ctor]p5).
  bool hasNonTrivialDefaultConstructor() const {
    return (data().DeclaredNonTrivialSpecialMembers & SMF_DefaultConstructor) ||
           (needsImplicitDefaultConstructor() &&
            !(data().HasTrivialSpecialMembers & SMF_DefaultConstructor));
  }

  /// Determine whether this class has at least one constexpr constructor
  /// other than the copy or move constructors.
  bool hasConstexprNonCopyMoveConstructor() const {
    return data().HasConstexprNonCopyMoveConstructor ||
           (needsImplicitDefaultConstructor() &&
            defaultedDefaultConstructorIsConstexpr());
  }

  /// Determine whether a defaulted default constructor for this class
  /// would be constexpr.
  bool defaultedDefaultConstructorIsConstexpr() const {
    return data().DefaultedDefaultConstructorIsConstexpr &&
           (!isUnion() || hasInClassInitializer() || !hasVariantMembers() ||
            getLangOpts().CPlusPlus20);
  }

  /// Determine whether this class has a constexpr default constructor.
  bool hasConstexprDefaultConstructor() const {
    return data().HasConstexprDefaultConstructor ||
           (needsImplicitDefaultConstructor() &&
            defaultedDefaultConstructorIsConstexpr());
  }

  /// Determine whether this class has a trivial copy constructor
  /// (C++ [class.copy]p6, C++11 [class.copy]p12)
  bool hasTrivialCopyConstructor() const {
    return data().HasTrivialSpecialMembers & SMF_CopyConstructor;
  }

  bool hasTrivialCopyConstructorForCall() const {
    return data().HasTrivialSpecialMembersForCall & SMF_CopyConstructor;
  }

  /// Determine whether this class has a non-trivial copy constructor
  /// (C++ [class.copy]p6, C++11 [class.copy]p12)
  bool hasNonTrivialCopyConstructor() const {
    return data().DeclaredNonTrivialSpecialMembers & SMF_CopyConstructor ||
           !hasTrivialCopyConstructor();
  }

  bool hasNonTrivialCopyConstructorForCall() const {
    return (data().DeclaredNonTrivialSpecialMembersForCall &
            SMF_CopyConstructor) ||
           !hasTrivialCopyConstructorForCall();
  }

  /// Determine whether this class has a trivial move constructor
  /// (C++11 [class.copy]p12)
  bool hasTrivialMoveConstructor() const {
    return hasMoveConstructor() &&
           (data().HasTrivialSpecialMembers & SMF_MoveConstructor);
  }

  bool hasTrivialMoveConstructorForCall() const {
    return hasMoveConstructor() &&
           (data().HasTrivialSpecialMembersForCall & SMF_MoveConstructor);
  }

  /// Determine whether this class has a non-trivial move constructor
  /// (C++11 [class.copy]p12)
  bool hasNonTrivialMoveConstructor() const {
    return (data().DeclaredNonTrivialSpecialMembers & SMF_MoveConstructor) ||
           (needsImplicitMoveConstructor() &&
            !(data().HasTrivialSpecialMembers & SMF_MoveConstructor));
  }

  bool hasNonTrivialMoveConstructorForCall() const {
    return (data().DeclaredNonTrivialSpecialMembersForCall &
            SMF_MoveConstructor) ||
           (needsImplicitMoveConstructor() &&
            !(data().HasTrivialSpecialMembersForCall & SMF_MoveConstructor));
  }

  /// Determine whether this class has a trivial copy assignment operator
  /// (C++ [class.copy]p11, C++11 [class.copy]p25)
  bool hasTrivialCopyAssignment() const {
    return data().HasTrivialSpecialMembers & SMF_CopyAssignment;
  }

  /// Determine whether this class has a non-trivial copy assignment
  /// operator (C++ [class.copy]p11, C++11 [class.copy]p25)
  bool hasNonTrivialCopyAssignment() const {
    return data().DeclaredNonTrivialSpecialMembers & SMF_CopyAssignment ||
           !hasTrivialCopyAssignment();
  }

  /// Determine whether this class has a trivial move assignment operator
  /// (C++11 [class.copy]p25)
  bool hasTrivialMoveAssignment() const {
    return hasMoveAssignment() &&
           (data().HasTrivialSpecialMembers & SMF_MoveAssignment);
  }

  /// Determine whether this class has a non-trivial move assignment
  /// operator (C++11 [class.copy]p25)
  bool hasNonTrivialMoveAssignment() const {
    return (data().DeclaredNonTrivialSpecialMembers & SMF_MoveAssignment) ||
           (needsImplicitMoveAssignment() &&
            !(data().HasTrivialSpecialMembers & SMF_MoveAssignment));
  }

  /// Determine whether a defaulted default constructor for this class
  /// would be constexpr.
  bool defaultedDestructorIsConstexpr() const {
    return data().DefaultedDestructorIsConstexpr &&
           getLangOpts().CPlusPlus20;
  }

  /// Determine whether this class has a constexpr destructor.
  bool hasConstexprDestructor() const;

  /// Determine whether this class has a trivial destructor
  /// (C++ [class.dtor]p3)
  bool hasTrivialDestructor() const {
    return data().HasTrivialSpecialMembers & SMF_Destructor;
  }

  bool hasTrivialDestructorForCall() const {
    return data().HasTrivialSpecialMembersForCall & SMF_Destructor;
  }

  /// Determine whether this class has a non-trivial destructor
  /// (C++ [class.dtor]p3)
  bool hasNonTrivialDestructor() const {
    return !(data().HasTrivialSpecialMembers & SMF_Destructor);
  }

  bool hasNonTrivialDestructorForCall() const {
    return !(data().HasTrivialSpecialMembersForCall & SMF_Destructor);
  }

  void setHasTrivialSpecialMemberForCall() {
    data().HasTrivialSpecialMembersForCall =
        (SMF_CopyConstructor | SMF_MoveConstructor | SMF_Destructor);
  }

  /// Determine whether declaring a const variable with this type is ok
  /// per core issue 253.
  bool allowConstDefaultInit() const {
    return !data().HasUninitializedFields ||
           !(data().HasDefaultedDefaultConstructor ||
             needsImplicitDefaultConstructor());
  }

  /// Determine whether this class has a destructor which has no
  /// semantic effect.
  ///
  /// Any such destructor will be trivial, public, defaulted and not deleted,
  /// and will call only irrelevant destructors.
  bool hasIrrelevantDestructor() const {
    return data().HasIrrelevantDestructor;
  }

  /// Determine whether this class has a non-literal or/ volatile type
  /// non-static data member or base class.
  bool hasNonLiteralTypeFieldsOrBases() const {
    return data().HasNonLiteralTypeFieldsOrBases;
  }

  /// Determine whether this class has a using-declaration that names
  /// a user-declared base class constructor.
  bool hasInheritedConstructor() const {
    return data().HasInheritedConstructor;
  }

  /// Determine whether this class has a using-declaration that names
  /// a base class assignment operator.
  bool hasInheritedAssignment() const {
    return data().HasInheritedAssignment;
  }

  /// Determine whether this class is considered trivially copyable per
  /// (C++11 [class]p6).
  bool isTriviallyCopyable() const;

  /// Determine whether this class is considered trivially copyable per
  bool isTriviallyCopyConstructible() const;

  /// Determine whether this class is considered trivial.
  ///
  /// C++11 [class]p6:
  ///    "A trivial class is a class that has a trivial default constructor and
  ///    is trivially copyable."
  bool isTrivial() const {
    return isTriviallyCopyable() && hasTrivialDefaultConstructor();
  }

  /// Determine whether this class is a literal type.
  ///
  /// C++20 [basic.types]p10:
  ///   A class type that has all the following properties:
  ///     - it has a constexpr destructor
  ///     - all of its non-static non-variant data members and base classes
  ///       are of non-volatile literal types, and it:
  ///        - is a closure type
  ///        - is an aggregate union type that has either no variant members
  ///          or at least one variant member of non-volatile literal type
  ///        - is a non-union aggregate type for which each of its anonymous
  ///          union members satisfies the above requirements for an aggregate
  ///          union type, or
  ///        - has at least one constexpr constructor or constructor template
  ///          that is not a copy or move constructor.
  bool isLiteral() const;

  /// Determine whether this is a structural type.
  bool isStructural() const {
    return isLiteral() && data().StructuralIfLiteral;
  }

  /// Notify the class that this destructor is now selected.
  ///
  /// Important properties of the class depend on destructor properties. Since
  /// C++20, it is possible to have multiple destructor declarations in a class
  /// out of which one will be selected at the end.
  /// This is called separately from addedMember because it has to be deferred
  /// to the completion of the class.
  void addedSelectedDestructor(CXXDestructorDecl *DD);

  /// Notify the class that an eligible SMF has been added.
  /// This updates triviality and destructor based properties of the class accordingly.
  void addedEligibleSpecialMemberFunction(const CXXMethodDecl *MD, unsigned SMKind);

  /// If this record is an instantiation of a member class,
  /// retrieves the member class from which it was instantiated.
  ///
  /// This routine will return non-null for (non-templated) member
  /// classes of class templates. For example, given:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   struct A { };
  /// };
  /// \endcode
  ///
  /// The declaration for X<int>::A is a (non-templated) CXXRecordDecl
  /// whose parent is the class template specialization X<int>. For
  /// this declaration, getInstantiatedFromMemberClass() will return
  /// the CXXRecordDecl X<T>::A. When a complete definition of
  /// X<int>::A is required, it will be instantiated from the
  /// declaration returned by getInstantiatedFromMemberClass().
  CXXRecordDecl *getInstantiatedFromMemberClass() const;

  /// If this class is an instantiation of a member class of a
  /// class template specialization, retrieves the member specialization
  /// information.
  MemberSpecializationInfo *getMemberSpecializationInfo() const;

  /// Specify that this record is an instantiation of the
  /// member class \p RD.
  void setInstantiationOfMemberClass(CXXRecordDecl *RD,
                                     TemplateSpecializationKind TSK);

  /// Retrieves the class template that is described by this
  /// class declaration.
  ///
  /// Every class template is represented as a ClassTemplateDecl and a
  /// CXXRecordDecl. The former contains template properties (such as
  /// the template parameter lists) while the latter contains the
  /// actual description of the template's
  /// contents. ClassTemplateDecl::getTemplatedDecl() retrieves the
  /// CXXRecordDecl that from a ClassTemplateDecl, while
  /// getDescribedClassTemplate() retrieves the ClassTemplateDecl from
  /// a CXXRecordDecl.
  ClassTemplateDecl *getDescribedClassTemplate() const;

  void setDescribedClassTemplate(ClassTemplateDecl *Template);

  /// Determine whether this particular class is a specialization or
  /// instantiation of a class template or member class of a class template,
  /// and how it was instantiated or specialized.
  TemplateSpecializationKind getTemplateSpecializationKind() const;

  /// Set the kind of specialization or template instantiation this is.
  void setTemplateSpecializationKind(TemplateSpecializationKind TSK);

  /// Retrieve the record declaration from which this record could be
  /// instantiated. Returns null if this class is not a template instantiation.
  const CXXRecordDecl *getTemplateInstantiationPattern() const;

  CXXRecordDecl *getTemplateInstantiationPattern() {
    return const_cast<CXXRecordDecl *>(const_cast<const CXXRecordDecl *>(this)
                                           ->getTemplateInstantiationPattern());
  }

  /// Returns the destructor decl for this class.
  CXXDestructorDecl *getDestructor() const;

  /// Returns true if the class destructor, or any implicitly invoked
  /// destructors are marked noreturn.
  bool isAnyDestructorNoReturn() const { return data().IsAnyDestructorNoReturn; }

  /// If the class is a local class [class.local], returns
  /// the enclosing function declaration.
  const FunctionDecl *isLocalClass() const {
    if (const auto *RD = dyn_cast<CXXRecordDecl>(getDeclContext()))
      return RD->isLocalClass();

    return dyn_cast<FunctionDecl>(getDeclContext());
  }

  FunctionDecl *isLocalClass() {
    return const_cast<FunctionDecl*>(
        const_cast<const CXXRecordDecl*>(this)->isLocalClass());
  }

  /// Determine whether this dependent class is a current instantiation,
  /// when viewed from within the given context.
  bool isCurrentInstantiation(const DeclContext *CurContext) const;

  /// Determine whether this class is derived from the class \p Base.
  ///
  /// This routine only determines whether this class is derived from \p Base,
  /// but does not account for factors that may make a Derived -> Base class
  /// ill-formed, such as private/protected inheritance or multiple, ambiguous
  /// base class subobjects.
  ///
  /// \param Base the base class we are searching for.
  ///
  /// \returns true if this class is derived from Base, false otherwise.
  bool isDerivedFrom(const CXXRecordDecl *Base) const;

  /// Determine whether this class is derived from the type \p Base.
  ///
  /// This routine only determines whether this class is derived from \p Base,
  /// but does not account for factors that may make a Derived -> Base class
  /// ill-formed, such as private/protected inheritance or multiple, ambiguous
  /// base class subobjects.
  ///
  /// \param Base the base class we are searching for.
  ///
  /// \param Paths will contain the paths taken from the current class to the
  /// given \p Base class.
  ///
  /// \returns true if this class is derived from \p Base, false otherwise.
  ///
  /// \todo add a separate parameter to configure IsDerivedFrom, rather than
  /// tangling input and output in \p Paths
  bool isDerivedFrom(const CXXRecordDecl *Base, CXXBasePaths &Paths) const;

  /// Determine whether this class is virtually derived from
  /// the class \p Base.
  ///
  /// This routine only determines whether this class is virtually
  /// derived from \p Base, but does not account for factors that may
  /// make a Derived -> Base class ill-formed, such as
  /// private/protected inheritance or multiple, ambiguous base class
  /// subobjects.
  ///
  /// \param Base the base class we are searching for.
  ///
  /// \returns true if this class is virtually derived from Base,
  /// false otherwise.
  bool isVirtuallyDerivedFrom(const CXXRecordDecl *Base) const;

  /// Determine whether this class is provably not derived from
  /// the type \p Base.
  bool isProvablyNotDerivedFrom(const CXXRecordDecl *Base) const;

  /// Function type used by forallBases() as a callback.
  ///
  /// \param BaseDefinition the definition of the base class
  ///
  /// \returns true if this base matched the search criteria
  using ForallBasesCallback =
      llvm::function_ref<bool(const CXXRecordDecl *BaseDefinition)>;

  /// Determines if the given callback holds for all the direct
  /// or indirect base classes of this type.
  ///
  /// The class itself does not count as a base class.  This routine
  /// returns false if the class has non-computable base classes.
  ///
  /// \param BaseMatches Callback invoked for each (direct or indirect) base
  /// class of this type until a call returns false.
  bool forallBases(ForallBasesCallback BaseMatches) const;

  /// Function type used by lookupInBases() to determine whether a
  /// specific base class subobject matches the lookup criteria.
  ///
  /// \param Specifier the base-class specifier that describes the inheritance
  /// from the base class we are trying to match.
  ///
  /// \param Path the current path, from the most-derived class down to the
  /// base named by the \p Specifier.
  ///
  /// \returns true if this base matched the search criteria, false otherwise.
  using BaseMatchesCallback =
      llvm::function_ref<bool(const CXXBaseSpecifier *Specifier,
                              CXXBasePath &Path)>;

  /// Look for entities within the base classes of this C++ class,
  /// transitively searching all base class subobjects.
  ///
  /// This routine uses the callback function \p BaseMatches to find base
  /// classes meeting some search criteria, walking all base class subobjects
  /// and populating the given \p Paths structure with the paths through the
  /// inheritance hierarchy that resulted in a match. On a successful search,
  /// the \p Paths structure can be queried to retrieve the matching paths and
  /// to determine if there were any ambiguities.
  ///
  /// \param BaseMatches callback function used to determine whether a given
  /// base matches the user-defined search criteria.
  ///
  /// \param Paths used to record the paths from this class to its base class
  /// subobjects that match the search criteria.
  ///
  /// \param LookupInDependent can be set to true to extend the search to
  /// dependent base classes.
  ///
  /// \returns true if there exists any path from this class to a base class
  /// subobject that matches the search criteria.
  bool lookupInBases(BaseMatchesCallback BaseMatches, CXXBasePaths &Paths,
                     bool LookupInDependent = false) const;

  /// Base-class lookup callback that determines whether the given
  /// base class specifier refers to a specific class declaration.
  ///
  /// This callback can be used with \c lookupInBases() to determine whether
  /// a given derived class has is a base class subobject of a particular type.
  /// The base record pointer should refer to the canonical CXXRecordDecl of the
  /// base class that we are searching for.
  static bool FindBaseClass(const CXXBaseSpecifier *Specifier,
                            CXXBasePath &Path, const CXXRecordDecl *BaseRecord);

  /// Base-class lookup callback that determines whether the
  /// given base class specifier refers to a specific class
  /// declaration and describes virtual derivation.
  ///
  /// This callback can be used with \c lookupInBases() to determine
  /// whether a given derived class has is a virtual base class
  /// subobject of a particular type.  The base record pointer should
  /// refer to the canonical CXXRecordDecl of the base class that we
  /// are searching for.
  static bool FindVirtualBaseClass(const CXXBaseSpecifier *Specifier,
                                   CXXBasePath &Path,
                                   const CXXRecordDecl *BaseRecord);

  /// Retrieve the final overriders for each virtual member
  /// function in the class hierarchy where this class is the
  /// most-derived class in the class hierarchy.
  void getFinalOverriders(CXXFinalOverriderMap &FinaOverriders) const;

  /// Get the indirect primary bases for this class.
  void getIndirectPrimaryBases(CXXIndirectPrimaryBaseSet& Bases) const;

  /// Determine whether this class has a member with the given name, possibly
  /// in a non-dependent base class.
  ///
  /// No check for ambiguity is performed, so this should never be used when
  /// implementing language semantics, but it may be appropriate for warnings,
  /// static analysis, or similar.
  bool hasMemberName(DeclarationName N) const;

  /// Performs an imprecise lookup of a dependent name in this class.
  ///
  /// This function does not follow strict semantic rules and should be used
  /// only when lookup rules can be relaxed, e.g. indexing.
  std::vector<const NamedDecl *>
  lookupDependentName(DeclarationName Name,
                      llvm::function_ref<bool(const NamedDecl *ND)> Filter);

  /// Renders and displays an inheritance diagram
  /// for this C++ class and all of its base classes (transitively) using
  /// GraphViz.
  void viewInheritance(ASTContext& Context) const;

  /// Calculates the access of a decl that is reached
  /// along a path.
  static AccessSpecifier MergeAccess(AccessSpecifier PathAccess,
                                     AccessSpecifier DeclAccess) {
    assert(DeclAccess != AS_none);
    if (DeclAccess == AS_private) return AS_none;
    return (PathAccess > DeclAccess ? PathAccess : DeclAccess);
  }

  /// Indicates that the declaration of a defaulted or deleted special
  /// member function is now complete.
  void finishedDefaultedOrDeletedMember(CXXMethodDecl *MD);

  void setTrivialForCallFlags(CXXMethodDecl *MD);

  /// Indicates that the definition of this class is now complete.
  void completeDefinition() override;

  /// Indicates that the definition of this class is now complete,
  /// and provides a final overrider map to help determine
  ///
  /// \param FinalOverriders The final overrider map for this class, which can
  /// be provided as an optimization for abstract-class checking. If NULL,
  /// final overriders will be computed if they are needed to complete the
  /// definition.
  void completeDefinition(CXXFinalOverriderMap *FinalOverriders);

  /// Determine whether this class may end up being abstract, even though
  /// it is not yet known to be abstract.
  ///
  /// \returns true if this class is not known to be abstract but has any
  /// base classes that are abstract. In this case, \c completeDefinition()
  /// will need to compute final overriders to determine whether the class is
  /// actually abstract.
  bool mayBeAbstract() const;

  /// Determine whether it's impossible for a class to be derived from this
  /// class. This is best-effort, and may conservatively return false.
  bool isEffectivelyFinal() const;

  /// If this is the closure type of a lambda expression, retrieve the
  /// number to be used for name mangling in the Itanium C++ ABI.
  ///
  /// Zero indicates that this closure type has internal linkage, so the
  /// mangling number does not matter, while a non-zero value indicates which
  /// lambda expression this is in this particular context.
  unsigned getLambdaManglingNumber() const {
    assert(isLambda() && "Not a lambda closure type!");
    return getLambdaData().ManglingNumber;
  }

  /// The lambda is known to has internal linkage no matter whether it has name
  /// mangling number.
  bool hasKnownLambdaInternalLinkage() const {
    assert(isLambda() && "Not a lambda closure type!");
    return getLambdaData().HasKnownInternalLinkage;
  }

  /// Retrieve the declaration that provides additional context for a
  /// lambda, when the normal declaration context is not specific enough.
  ///
  /// Certain contexts (default arguments of in-class function parameters and
  /// the initializers of data members) have separate name mangling rules for
  /// lambdas within the Itanium C++ ABI. For these cases, this routine provides
  /// the declaration in which the lambda occurs, e.g., the function parameter
  /// or the non-static data member. Otherwise, it returns NULL to imply that
  /// the declaration context suffices.
  Decl *getLambdaContextDecl() const;

  /// Retrieve the index of this lambda within the context declaration returned
  /// by getLambdaContextDecl().
  unsigned getLambdaIndexInContext() const {
    assert(isLambda() && "Not a lambda closure type!");
    return getLambdaData().IndexInContext;
  }

  /// Information about how a lambda is numbered within its context.
  struct LambdaNumbering {
    Decl *ContextDecl = nullptr;
    unsigned IndexInContext = 0;
    unsigned ManglingNumber = 0;
    unsigned DeviceManglingNumber = 0;
    bool HasKnownInternalLinkage = false;
  };

  /// Set the mangling numbers and context declaration for a lambda class.
  void setLambdaNumbering(LambdaNumbering Numbering);

  // Get the mangling numbers and context declaration for a lambda class.
  LambdaNumbering getLambdaNumbering() const {
    return {getLambdaContextDecl(), getLambdaIndexInContext(),
            getLambdaManglingNumber(), getDeviceLambdaManglingNumber(),
            hasKnownLambdaInternalLinkage()};
  }

  /// Retrieve the device side mangling number.
  unsigned getDeviceLambdaManglingNumber() const;

  /// Returns the inheritance model used for this record.
  MSInheritanceModel getMSInheritanceModel() const;

  /// Calculate what the inheritance model would be for this class.
  MSInheritanceModel calculateInheritanceModel() const;

  /// In the Microsoft C++ ABI, use zero for the field offset of a null data
  /// member pointer if we can guarantee that zero is not a valid field offset,
  /// or if the member pointer has multiple fields.  Polymorphic classes have a
  /// vfptr at offset zero, so we can use zero for null.  If there are multiple
  /// fields, we can use zero even if it is a valid field offset because
  /// null-ness testing will check the other fields.
  bool nullFieldOffsetIsZero() const;

  /// Controls when vtordisps will be emitted if this record is used as a
  /// virtual base.
  MSVtorDispMode getMSVtorDispMode() const;

  /// Determine whether this lambda expression was known to be dependent
  /// at the time it was created, even if its context does not appear to be
  /// dependent.
  ///
  /// This flag is a workaround for an issue with parsing, where default
  /// arguments are parsed before their enclosing function declarations have
  /// been created. This means that any lambda expressions within those
  /// default arguments will have as their DeclContext the context enclosing
  /// the function declaration, which may be non-dependent even when the
  /// function declaration itself is dependent. This flag indicates when we
  /// know that the lambda is dependent despite that.
  bool isDependentLambda() const {
    return isLambda() && getLambdaData().DependencyKind == LDK_AlwaysDependent;
  }

  bool isNeverDependentLambda() const {
    return isLambda() && getLambdaData().DependencyKind == LDK_NeverDependent;
  }

  unsigned getLambdaDependencyKind() const {
    if (!isLambda())
      return LDK_Unknown;
    return getLambdaData().DependencyKind;
  }

  TypeSourceInfo *getLambdaTypeInfo() const {
    return getLambdaData().MethodTyInfo;
  }

  void setLambdaTypeInfo(TypeSourceInfo *TS) {
    assert(DefinitionData && DefinitionData->IsLambda &&
           "setting lambda property of non-lambda class");
    auto &DL = static_cast<LambdaDefinitionData &>(*DefinitionData);
    DL.MethodTyInfo = TS;
  }

  void setLambdaDependencyKind(unsigned Kind) {
    getLambdaData().DependencyKind = Kind;
  }

  void setLambdaIsGeneric(bool IsGeneric) {
    assert(DefinitionData && DefinitionData->IsLambda &&
           "setting lambda property of non-lambda class");
    auto &DL = static_cast<LambdaDefinitionData &>(*DefinitionData);
    DL.IsGenericLambda = IsGeneric;
  }

  // Determine whether this type is an Interface Like type for
  // __interface inheritance purposes.
  bool isInterfaceLike() const;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstCXXRecord && K <= lastCXXRecord;
  }
  void markAbstract() { data().Abstract = true; }
};

/// Store information needed for an explicit specifier.
/// Used by CXXDeductionGuideDecl, CXXConstructorDecl and CXXConversionDecl.
class ExplicitSpecifier {
  llvm::PointerIntPair<Expr *, 2, ExplicitSpecKind> ExplicitSpec{
      nullptr, ExplicitSpecKind::ResolvedFalse};

public:
  ExplicitSpecifier() = default;
  ExplicitSpecifier(Expr *Expression, ExplicitSpecKind Kind)
      : ExplicitSpec(Expression, Kind) {}
  ExplicitSpecKind getKind() const { return ExplicitSpec.getInt(); }
  const Expr *getExpr() const { return ExplicitSpec.getPointer(); }
  Expr *getExpr() { return ExplicitSpec.getPointer(); }

  /// Determine if the declaration had an explicit specifier of any kind.
  bool isSpecified() const {
    return ExplicitSpec.getInt() != ExplicitSpecKind::ResolvedFalse ||
           ExplicitSpec.getPointer();
  }

  /// Check for equivalence of explicit specifiers.
  /// \return true if the explicit specifier are equivalent, false otherwise.
  bool isEquivalent(const ExplicitSpecifier Other) const;
  /// Determine whether this specifier is known to correspond to an explicit
  /// declaration. Returns false if the specifier is absent or has an
  /// expression that is value-dependent or evaluates to false.
  bool isExplicit() const {
    return ExplicitSpec.getInt() == ExplicitSpecKind::ResolvedTrue;
  }
  /// Determine if the explicit specifier is invalid.
  /// This state occurs after a substitution failures.
  bool isInvalid() const {
    return ExplicitSpec.getInt() == ExplicitSpecKind::Unresolved &&
           !ExplicitSpec.getPointer();
  }
  void setKind(ExplicitSpecKind Kind) { ExplicitSpec.setInt(Kind); }
  void setExpr(Expr *E) { ExplicitSpec.setPointer(E); }
  // Retrieve the explicit specifier in the given declaration, if any.
  static ExplicitSpecifier getFromDecl(FunctionDecl *Function);
  static const ExplicitSpecifier getFromDecl(const FunctionDecl *Function) {
    return getFromDecl(const_cast<FunctionDecl *>(Function));
  }
  static ExplicitSpecifier Invalid() {
    return ExplicitSpecifier(nullptr, ExplicitSpecKind::Unresolved);
  }
};

/// Represents a C++ deduction guide declaration.
///
/// \code
/// template<typename T> struct A { A(); A(T); };
/// A() -> A<int>;
/// \endcode
///
/// In this example, there will be an explicit deduction guide from the
/// second line, and implicit deduction guide templates synthesized from
/// the constructors of \c A.
class CXXDeductionGuideDecl : public FunctionDecl {
  void anchor() override;

private:
  CXXDeductionGuideDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
                        ExplicitSpecifier ES,
                        const DeclarationNameInfo &NameInfo, QualType T,
                        TypeSourceInfo *TInfo, SourceLocation EndLocation,
                        CXXConstructorDecl *Ctor, DeductionCandidate Kind)
      : FunctionDecl(CXXDeductionGuide, C, DC, StartLoc, NameInfo, T, TInfo,
                     SC_None, false, false, ConstexprSpecKind::Unspecified),
        Ctor(Ctor), ExplicitSpec(ES) {
    if (EndLocation.isValid())
      setRangeEnd(EndLocation);
    setDeductionCandidateKind(Kind);
  }

  CXXConstructorDecl *Ctor;
  ExplicitSpecifier ExplicitSpec;
  void setExplicitSpecifier(ExplicitSpecifier ES) { ExplicitSpec = ES; }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static CXXDeductionGuideDecl *
  Create(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
         ExplicitSpecifier ES, const DeclarationNameInfo &NameInfo, QualType T,
         TypeSourceInfo *TInfo, SourceLocation EndLocation,
         CXXConstructorDecl *Ctor = nullptr,
         DeductionCandidate Kind = DeductionCandidate::Normal);

  static CXXDeductionGuideDecl *CreateDeserialized(ASTContext &C,
                                                   GlobalDeclID ID);

  ExplicitSpecifier getExplicitSpecifier() { return ExplicitSpec; }
  const ExplicitSpecifier getExplicitSpecifier() const { return ExplicitSpec; }

  /// Return true if the declaration is already resolved to be explicit.
  bool isExplicit() const { return ExplicitSpec.isExplicit(); }

  /// Get the template for which this guide performs deduction.
  TemplateDecl *getDeducedTemplate() const {
    return getDeclName().getCXXDeductionGuideTemplate();
  }

  /// Get the constructor from which this deduction guide was generated, if
  /// this is an implicit deduction guide.
  CXXConstructorDecl *getCorrespondingConstructor() const { return Ctor; }

  void setDeductionCandidateKind(DeductionCandidate K) {
    FunctionDeclBits.DeductionCandidateKind = static_cast<unsigned char>(K);
  }

  DeductionCandidate getDeductionCandidateKind() const {
    return static_cast<DeductionCandidate>(
        FunctionDeclBits.DeductionCandidateKind);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXDeductionGuide; }
};

/// \brief Represents the body of a requires-expression.
///
/// This decl exists merely to serve as the DeclContext for the local
/// parameters of the requires expression as well as other declarations inside
/// it.
///
/// \code
/// template<typename T> requires requires (T t) { {t++} -> regular; }
/// \endcode
///
/// In this example, a RequiresExpr object will be generated for the expression,
/// and a RequiresExprBodyDecl will be created to hold the parameter t and the
/// template argument list imposed by the compound requirement.
class RequiresExprBodyDecl : public Decl, public DeclContext {
  RequiresExprBodyDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc)
      : Decl(RequiresExprBody, DC, StartLoc), DeclContext(RequiresExprBody) {}

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static RequiresExprBodyDecl *Create(ASTContext &C, DeclContext *DC,
                                      SourceLocation StartLoc);

  static RequiresExprBodyDecl *CreateDeserialized(ASTContext &C,
                                                  GlobalDeclID ID);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == RequiresExprBody; }

  static DeclContext *castToDeclContext(const RequiresExprBodyDecl *D) {
    return static_cast<DeclContext *>(const_cast<RequiresExprBodyDecl *>(D));
  }

  static RequiresExprBodyDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<RequiresExprBodyDecl *>(const_cast<DeclContext *>(DC));
  }
};

/// Represents a static or instance method of a struct/union/class.
///
/// In the terminology of the C++ Standard, these are the (static and
/// non-static) member functions, whether virtual or not.
class CXXMethodDecl : public FunctionDecl {
  void anchor() override;

protected:
  CXXMethodDecl(Kind DK, ASTContext &C, CXXRecordDecl *RD,
                SourceLocation StartLoc, const DeclarationNameInfo &NameInfo,
                QualType T, TypeSourceInfo *TInfo, StorageClass SC,
                bool UsesFPIntrin, bool isInline,
                ConstexprSpecKind ConstexprKind, SourceLocation EndLocation,
                Expr *TrailingRequiresClause = nullptr)
      : FunctionDecl(DK, C, RD, StartLoc, NameInfo, T, TInfo, SC, UsesFPIntrin,
                     isInline, ConstexprKind, TrailingRequiresClause) {
    if (EndLocation.isValid())
      setRangeEnd(EndLocation);
  }

public:
  static CXXMethodDecl *
  Create(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
         const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
         StorageClass SC, bool UsesFPIntrin, bool isInline,
         ConstexprSpecKind ConstexprKind, SourceLocation EndLocation,
         Expr *TrailingRequiresClause = nullptr);

  static CXXMethodDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  bool isStatic() const;
  bool isInstance() const { return !isStatic(); }

  /// [C++2b][dcl.fct]/p7
  /// An explicit object member function is a non-static
  /// member function with an explicit object parameter. e.g.,
  ///   void func(this SomeType);
  bool isExplicitObjectMemberFunction() const;

  /// [C++2b][dcl.fct]/p7
  /// An implicit object member function is a non-static
  /// member function without an explicit object parameter.
  bool isImplicitObjectMemberFunction() const;

  /// Returns true if the given operator is implicitly static in a record
  /// context.
  static bool isStaticOverloadedOperator(OverloadedOperatorKind OOK) {
    // [class.free]p1:
    // Any allocation function for a class T is a static member
    // (even if not explicitly declared static).
    // [class.free]p6 Any deallocation function for a class X is a static member
    // (even if not explicitly declared static).
    return OOK == OO_New || OOK == OO_Array_New || OOK == OO_Delete ||
           OOK == OO_Array_Delete;
  }

  bool isConst() const { return getType()->castAs<FunctionType>()->isConst(); }
  bool isVolatile() const { return getType()->castAs<FunctionType>()->isVolatile(); }

  bool isVirtual() const {
    CXXMethodDecl *CD = const_cast<CXXMethodDecl*>(this)->getCanonicalDecl();

    // Member function is virtual if it is marked explicitly so, or if it is
    // declared in __interface -- then it is automatically pure virtual.
    if (CD->isVirtualAsWritten() || CD->isPureVirtual())
      return true;

    return CD->size_overridden_methods() != 0;
  }

  /// If it's possible to devirtualize a call to this method, return the called
  /// function. Otherwise, return null.

  /// \param Base The object on which this virtual function is called.
  /// \param IsAppleKext True if we are compiling for Apple kext.
  CXXMethodDecl *getDevirtualizedMethod(const Expr *Base, bool IsAppleKext);

  const CXXMethodDecl *getDevirtualizedMethod(const Expr *Base,
                                              bool IsAppleKext) const {
    return const_cast<CXXMethodDecl *>(this)->getDevirtualizedMethod(
        Base, IsAppleKext);
  }

  /// Determine whether this is a usual deallocation function (C++
  /// [basic.stc.dynamic.deallocation]p2), which is an overloaded delete or
  /// delete[] operator with a particular signature. Populates \p PreventedBy
  /// with the declarations of the functions of the same kind if they were the
  /// reason for this function returning false. This is used by
  /// Sema::isUsualDeallocationFunction to reconsider the answer based on the
  /// context.
  bool isUsualDeallocationFunction(
      SmallVectorImpl<const FunctionDecl *> &PreventedBy) const;

  /// Determine whether this is a copy-assignment operator, regardless
  /// of whether it was declared implicitly or explicitly.
  bool isCopyAssignmentOperator() const;

  /// Determine whether this is a move assignment operator.
  bool isMoveAssignmentOperator() const;

  CXXMethodDecl *getCanonicalDecl() override {
    return cast<CXXMethodDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXMethodDecl *getCanonicalDecl() const {
    return const_cast<CXXMethodDecl*>(this)->getCanonicalDecl();
  }

  CXXMethodDecl *getMostRecentDecl() {
    return cast<CXXMethodDecl>(
            static_cast<FunctionDecl *>(this)->getMostRecentDecl());
  }
  const CXXMethodDecl *getMostRecentDecl() const {
    return const_cast<CXXMethodDecl*>(this)->getMostRecentDecl();
  }

  void addOverriddenMethod(const CXXMethodDecl *MD);

  using method_iterator = const CXXMethodDecl *const *;

  method_iterator begin_overridden_methods() const;
  method_iterator end_overridden_methods() const;
  unsigned size_overridden_methods() const;

  using overridden_method_range = llvm::iterator_range<
      llvm::TinyPtrVector<const CXXMethodDecl *>::const_iterator>;

  overridden_method_range overridden_methods() const;

  /// Return the parent of this method declaration, which
  /// is the class in which this method is defined.
  const CXXRecordDecl *getParent() const {
    return cast<CXXRecordDecl>(FunctionDecl::getParent());
  }

  /// Return the parent of this method declaration, which
  /// is the class in which this method is defined.
  CXXRecordDecl *getParent() {
    return const_cast<CXXRecordDecl *>(
             cast<CXXRecordDecl>(FunctionDecl::getParent()));
  }

  /// Return the type of the \c this pointer.
  ///
  /// Should only be called for instance (i.e., non-static) methods. Note
  /// that for the call operator of a lambda closure type, this returns the
  /// desugared 'this' type (a pointer to the closure type), not the captured
  /// 'this' type.
  QualType getThisType() const;

  /// Return the type of the object pointed by \c this.
  ///
  /// See getThisType() for usage restriction.

  QualType getFunctionObjectParameterReferenceType() const;
  QualType getFunctionObjectParameterType() const {
    return getFunctionObjectParameterReferenceType().getNonReferenceType();
  }

  unsigned getNumExplicitParams() const {
    return getNumParams() - (isExplicitObjectMemberFunction() ? 1 : 0);
  }

  static QualType getThisType(const FunctionProtoType *FPT,
                              const CXXRecordDecl *Decl);

  Qualifiers getMethodQualifiers() const {
    return getType()->castAs<FunctionProtoType>()->getMethodQuals();
  }

  /// Retrieve the ref-qualifier associated with this method.
  ///
  /// In the following example, \c f() has an lvalue ref-qualifier, \c g()
  /// has an rvalue ref-qualifier, and \c h() has no ref-qualifier.
  /// @code
  /// struct X {
  ///   void f() &;
  ///   void g() &&;
  ///   void h();
  /// };
  /// @endcode
  RefQualifierKind getRefQualifier() const {
    return getType()->castAs<FunctionProtoType>()->getRefQualifier();
  }

  bool hasInlineBody() const;

  /// Determine whether this is a lambda closure type's static member
  /// function that is used for the result of the lambda's conversion to
  /// function pointer (for a lambda with no captures).
  ///
  /// The function itself, if used, will have a placeholder body that will be
  /// supplied by IR generation to either forward to the function call operator
  /// or clone the function call operator.
  bool isLambdaStaticInvoker() const;

  /// Find the method in \p RD that corresponds to this one.
  ///
  /// Find if \p RD or one of the classes it inherits from override this method.
  /// If so, return it. \p RD is assumed to be a subclass of the class defining
  /// this method (or be the class itself), unless \p MayBeBase is set to true.
  CXXMethodDecl *
  getCorrespondingMethodInClass(const CXXRecordDecl *RD,
                                bool MayBeBase = false);

  const CXXMethodDecl *
  getCorrespondingMethodInClass(const CXXRecordDecl *RD,
                                bool MayBeBase = false) const {
    return const_cast<CXXMethodDecl *>(this)
              ->getCorrespondingMethodInClass(RD, MayBeBase);
  }

  /// Find if \p RD declares a function that overrides this function, and if so,
  /// return it. Does not search base classes.
  CXXMethodDecl *getCorrespondingMethodDeclaredInClass(const CXXRecordDecl *RD,
                                                       bool MayBeBase = false);
  const CXXMethodDecl *
  getCorrespondingMethodDeclaredInClass(const CXXRecordDecl *RD,
                                        bool MayBeBase = false) const {
    return const_cast<CXXMethodDecl *>(this)
        ->getCorrespondingMethodDeclaredInClass(RD, MayBeBase);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstCXXMethod && K <= lastCXXMethod;
  }
};

/// Represents a C++ base or member initializer.
///
/// This is part of a constructor initializer that
/// initializes one non-static member variable or one base class. For
/// example, in the following, both 'A(a)' and 'f(3.14159)' are member
/// initializers:
///
/// \code
/// class A { };
/// class B : public A {
///   float f;
/// public:
///   B(A& a) : A(a), f(3.14159) { }
/// };
/// \endcode
class CXXCtorInitializer final {
  /// Either the base class name/delegating constructor type (stored as
  /// a TypeSourceInfo*), an normal field (FieldDecl), or an anonymous field
  /// (IndirectFieldDecl*) being initialized.
  llvm::PointerUnion<TypeSourceInfo *, FieldDecl *, IndirectFieldDecl *>
      Initializee;

  /// The argument used to initialize the base or member, which may
  /// end up constructing an object (when multiple arguments are involved).
  Stmt *Init;

  /// The source location for the field name or, for a base initializer
  /// pack expansion, the location of the ellipsis.
  ///
  /// In the case of a delegating
  /// constructor, it will still include the type's source location as the
  /// Initializee points to the CXXConstructorDecl (to allow loop detection).
  SourceLocation MemberOrEllipsisLocation;

  /// Location of the left paren of the ctor-initializer.
  SourceLocation LParenLoc;

  /// Location of the right paren of the ctor-initializer.
  SourceLocation RParenLoc;

  /// If the initializee is a type, whether that type makes this
  /// a delegating initialization.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsDelegating : 1;

  /// If the initializer is a base initializer, this keeps track
  /// of whether the base is virtual or not.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsVirtual : 1;

  /// Whether or not the initializer is explicitly written
  /// in the sources.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsWritten : 1;

  /// If IsWritten is true, then this number keeps track of the textual order
  /// of this initializer in the original sources, counting from 0.
  unsigned SourceOrder : 13;

public:
  /// Creates a new base-class initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, TypeSourceInfo *TInfo, bool IsVirtual,
                     SourceLocation L, Expr *Init, SourceLocation R,
                     SourceLocation EllipsisLoc);

  /// Creates a new member initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, FieldDecl *Member,
                     SourceLocation MemberLoc, SourceLocation L, Expr *Init,
                     SourceLocation R);

  /// Creates a new anonymous field initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, IndirectFieldDecl *Member,
                     SourceLocation MemberLoc, SourceLocation L, Expr *Init,
                     SourceLocation R);

  /// Creates a new delegating initializer.
  explicit
  CXXCtorInitializer(ASTContext &Context, TypeSourceInfo *TInfo,
                     SourceLocation L, Expr *Init, SourceLocation R);

  /// \return Unique reproducible object identifier.
  int64_t getID(const ASTContext &Context) const;

  /// Determine whether this initializer is initializing a base class.
  bool isBaseInitializer() const {
    return Initializee.is<TypeSourceInfo*>() && !IsDelegating;
  }

  /// Determine whether this initializer is initializing a non-static
  /// data member.
  bool isMemberInitializer() const { return Initializee.is<FieldDecl*>(); }

  bool isAnyMemberInitializer() const {
    return isMemberInitializer() || isIndirectMemberInitializer();
  }

  bool isIndirectMemberInitializer() const {
    return Initializee.is<IndirectFieldDecl*>();
  }

  /// Determine whether this initializer is an implicit initializer
  /// generated for a field with an initializer defined on the member
  /// declaration.
  ///
  /// In-class member initializers (also known as "non-static data member
  /// initializations", NSDMIs) were introduced in C++11.
  bool isInClassMemberInitializer() const {
    return Init->getStmtClass() == Stmt::CXXDefaultInitExprClass;
  }

  /// Determine whether this initializer is creating a delegating
  /// constructor.
  bool isDelegatingInitializer() const {
    return Initializee.is<TypeSourceInfo*>() && IsDelegating;
  }

  /// Determine whether this initializer is a pack expansion.
  bool isPackExpansion() const {
    return isBaseInitializer() && MemberOrEllipsisLocation.isValid();
  }

  // For a pack expansion, returns the location of the ellipsis.
  SourceLocation getEllipsisLoc() const {
    if (!isPackExpansion())
      return {};
    return MemberOrEllipsisLocation;
  }

  /// If this is a base class initializer, returns the type of the
  /// base class with location information. Otherwise, returns an NULL
  /// type location.
  TypeLoc getBaseClassLoc() const;

  /// If this is a base class initializer, returns the type of the base class.
  /// Otherwise, returns null.
  const Type *getBaseClass() const;

  /// Returns whether the base is virtual or not.
  bool isBaseVirtual() const {
    assert(isBaseInitializer() && "Must call this on base initializer!");

    return IsVirtual;
  }

  /// Returns the declarator information for a base class or delegating
  /// initializer.
  TypeSourceInfo *getTypeSourceInfo() const {
    return Initializee.dyn_cast<TypeSourceInfo *>();
  }

  /// If this is a member initializer, returns the declaration of the
  /// non-static data member being initialized. Otherwise, returns null.
  FieldDecl *getMember() const {
    if (isMemberInitializer())
      return Initializee.get<FieldDecl*>();
    return nullptr;
  }

  FieldDecl *getAnyMember() const {
    if (isMemberInitializer())
      return Initializee.get<FieldDecl*>();
    if (isIndirectMemberInitializer())
      return Initializee.get<IndirectFieldDecl*>()->getAnonField();
    return nullptr;
  }

  IndirectFieldDecl *getIndirectMember() const {
    if (isIndirectMemberInitializer())
      return Initializee.get<IndirectFieldDecl*>();
    return nullptr;
  }

  SourceLocation getMemberLocation() const {
    return MemberOrEllipsisLocation;
  }

  /// Determine the source location of the initializer.
  SourceLocation getSourceLocation() const;

  /// Determine the source range covering the entire initializer.
  SourceRange getSourceRange() const LLVM_READONLY;

  /// Determine whether this initializer is explicitly written
  /// in the source code.
  bool isWritten() const { return IsWritten; }

  /// Return the source position of the initializer, counting from 0.
  /// If the initializer was implicit, -1 is returned.
  int getSourceOrder() const {
    return IsWritten ? static_cast<int>(SourceOrder) : -1;
  }

  /// Set the source order of this initializer.
  ///
  /// This can only be called once for each initializer; it cannot be called
  /// on an initializer having a positive number of (implicit) array indices.
  ///
  /// This assumes that the initializer was written in the source code, and
  /// ensures that isWritten() returns true.
  void setSourceOrder(int Pos) {
    assert(!IsWritten &&
           "setSourceOrder() used on implicit initializer");
    assert(SourceOrder == 0 &&
           "calling twice setSourceOrder() on the same initializer");
    assert(Pos >= 0 &&
           "setSourceOrder() used to make an initializer implicit");
    IsWritten = true;
    SourceOrder = static_cast<unsigned>(Pos);
  }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  /// Get the initializer.
  Expr *getInit() const { return static_cast<Expr *>(Init); }
};

/// Description of a constructor that was inherited from a base class.
class InheritedConstructor {
  ConstructorUsingShadowDecl *Shadow = nullptr;
  CXXConstructorDecl *BaseCtor = nullptr;

public:
  InheritedConstructor() = default;
  InheritedConstructor(ConstructorUsingShadowDecl *Shadow,
                       CXXConstructorDecl *BaseCtor)
      : Shadow(Shadow), BaseCtor(BaseCtor) {}

  explicit operator bool() const { return Shadow; }

  ConstructorUsingShadowDecl *getShadowDecl() const { return Shadow; }
  CXXConstructorDecl *getConstructor() const { return BaseCtor; }
};

/// Represents a C++ constructor within a class.
///
/// For example:
///
/// \code
/// class X {
/// public:
///   explicit X(int); // represented by a CXXConstructorDecl.
/// };
/// \endcode
class CXXConstructorDecl final
    : public CXXMethodDecl,
      private llvm::TrailingObjects<CXXConstructorDecl, InheritedConstructor,
                                    ExplicitSpecifier> {
  // This class stores some data in DeclContext::CXXConstructorDeclBits
  // to save some space. Use the provided accessors to access it.

  /// \name Support for base and member initializers.
  /// \{
  /// The arguments used to initialize the base or member.
  LazyCXXCtorInitializersPtr CtorInitializers;

  CXXConstructorDecl(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
                     const DeclarationNameInfo &NameInfo, QualType T,
                     TypeSourceInfo *TInfo, ExplicitSpecifier ES,
                     bool UsesFPIntrin, bool isInline,
                     bool isImplicitlyDeclared, ConstexprSpecKind ConstexprKind,
                     InheritedConstructor Inherited,
                     Expr *TrailingRequiresClause);

  void anchor() override;

  size_t numTrailingObjects(OverloadToken<InheritedConstructor>) const {
    return CXXConstructorDeclBits.IsInheritingConstructor;
  }
  size_t numTrailingObjects(OverloadToken<ExplicitSpecifier>) const {
    return CXXConstructorDeclBits.HasTrailingExplicitSpecifier;
  }

  ExplicitSpecifier getExplicitSpecifierInternal() const {
    if (CXXConstructorDeclBits.HasTrailingExplicitSpecifier)
      return *getTrailingObjects<ExplicitSpecifier>();
    return ExplicitSpecifier(
        nullptr, CXXConstructorDeclBits.IsSimpleExplicit
                     ? ExplicitSpecKind::ResolvedTrue
                     : ExplicitSpecKind::ResolvedFalse);
  }

  enum TrailingAllocKind {
    TAKInheritsConstructor = 1,
    TAKHasTailExplicit = 1 << 1,
  };

  uint64_t getTrailingAllocKind() const {
    return numTrailingObjects(OverloadToken<InheritedConstructor>()) |
           (numTrailingObjects(OverloadToken<ExplicitSpecifier>()) << 1);
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  static CXXConstructorDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                                uint64_t AllocKind);
  static CXXConstructorDecl *
  Create(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
         const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
         ExplicitSpecifier ES, bool UsesFPIntrin, bool isInline,
         bool isImplicitlyDeclared, ConstexprSpecKind ConstexprKind,
         InheritedConstructor Inherited = InheritedConstructor(),
         Expr *TrailingRequiresClause = nullptr);

  void setExplicitSpecifier(ExplicitSpecifier ES) {
    assert((!ES.getExpr() ||
            CXXConstructorDeclBits.HasTrailingExplicitSpecifier) &&
           "cannot set this explicit specifier. no trail-allocated space for "
           "explicit");
    if (ES.getExpr())
      *getCanonicalDecl()->getTrailingObjects<ExplicitSpecifier>() = ES;
    else
      CXXConstructorDeclBits.IsSimpleExplicit = ES.isExplicit();
  }

  ExplicitSpecifier getExplicitSpecifier() {
    return getCanonicalDecl()->getExplicitSpecifierInternal();
  }
  const ExplicitSpecifier getExplicitSpecifier() const {
    return getCanonicalDecl()->getExplicitSpecifierInternal();
  }

  /// Return true if the declaration is already resolved to be explicit.
  bool isExplicit() const { return getExplicitSpecifier().isExplicit(); }

  /// Iterates through the member/base initializer list.
  using init_iterator = CXXCtorInitializer **;

  /// Iterates through the member/base initializer list.
  using init_const_iterator = CXXCtorInitializer *const *;

  using init_range = llvm::iterator_range<init_iterator>;
  using init_const_range = llvm::iterator_range<init_const_iterator>;

  init_range inits() { return init_range(init_begin(), init_end()); }
  init_const_range inits() const {
    return init_const_range(init_begin(), init_end());
  }

  /// Retrieve an iterator to the first initializer.
  init_iterator init_begin() {
    const auto *ConstThis = this;
    return const_cast<init_iterator>(ConstThis->init_begin());
  }

  /// Retrieve an iterator to the first initializer.
  init_const_iterator init_begin() const;

  /// Retrieve an iterator past the last initializer.
  init_iterator       init_end()       {
    return init_begin() + getNumCtorInitializers();
  }

  /// Retrieve an iterator past the last initializer.
  init_const_iterator init_end() const {
    return init_begin() + getNumCtorInitializers();
  }

  using init_reverse_iterator = std::reverse_iterator<init_iterator>;
  using init_const_reverse_iterator =
      std::reverse_iterator<init_const_iterator>;

  init_reverse_iterator init_rbegin() {
    return init_reverse_iterator(init_end());
  }
  init_const_reverse_iterator init_rbegin() const {
    return init_const_reverse_iterator(init_end());
  }

  init_reverse_iterator init_rend() {
    return init_reverse_iterator(init_begin());
  }
  init_const_reverse_iterator init_rend() const {
    return init_const_reverse_iterator(init_begin());
  }

  /// Determine the number of arguments used to initialize the member
  /// or base.
  unsigned getNumCtorInitializers() const {
      return CXXConstructorDeclBits.NumCtorInitializers;
  }

  void setNumCtorInitializers(unsigned numCtorInitializers) {
    CXXConstructorDeclBits.NumCtorInitializers = numCtorInitializers;
    // This assert added because NumCtorInitializers is stored
    // in CXXConstructorDeclBits as a bitfield and its width has
    // been shrunk from 32 bits to fit into CXXConstructorDeclBitfields.
    assert(CXXConstructorDeclBits.NumCtorInitializers ==
           numCtorInitializers && "NumCtorInitializers overflow!");
  }

  void setCtorInitializers(CXXCtorInitializer **Initializers) {
    CtorInitializers = Initializers;
  }

  /// Determine whether this constructor is a delegating constructor.
  bool isDelegatingConstructor() const {
    return (getNumCtorInitializers() == 1) &&
           init_begin()[0]->isDelegatingInitializer();
  }

  /// When this constructor delegates to another, retrieve the target.
  CXXConstructorDecl *getTargetConstructor() const;

  /// Whether this constructor is a default
  /// constructor (C++ [class.ctor]p5), which can be used to
  /// default-initialize a class of this type.
  bool isDefaultConstructor() const;

  /// Whether this constructor is a copy constructor (C++ [class.copy]p2,
  /// which can be used to copy the class.
  ///
  /// \p TypeQuals will be set to the qualifiers on the
  /// argument type. For example, \p TypeQuals would be set to \c
  /// Qualifiers::Const for the following copy constructor:
  ///
  /// \code
  /// class X {
  /// public:
  ///   X(const X&);
  /// };
  /// \endcode
  bool isCopyConstructor(unsigned &TypeQuals) const;

  /// Whether this constructor is a copy
  /// constructor (C++ [class.copy]p2, which can be used to copy the
  /// class.
  bool isCopyConstructor() const {
    unsigned TypeQuals = 0;
    return isCopyConstructor(TypeQuals);
  }

  /// Determine whether this constructor is a move constructor
  /// (C++11 [class.copy]p3), which can be used to move values of the class.
  ///
  /// \param TypeQuals If this constructor is a move constructor, will be set
  /// to the type qualifiers on the referent of the first parameter's type.
  bool isMoveConstructor(unsigned &TypeQuals) const;

  /// Determine whether this constructor is a move constructor
  /// (C++11 [class.copy]p3), which can be used to move values of the class.
  bool isMoveConstructor() const {
    unsigned TypeQuals = 0;
    return isMoveConstructor(TypeQuals);
  }

  /// Determine whether this is a copy or move constructor.
  ///
  /// \param TypeQuals Will be set to the type qualifiers on the reference
  /// parameter, if in fact this is a copy or move constructor.
  bool isCopyOrMoveConstructor(unsigned &TypeQuals) const;

  /// Determine whether this a copy or move constructor.
  bool isCopyOrMoveConstructor() const {
    unsigned Quals;
    return isCopyOrMoveConstructor(Quals);
  }

  /// Whether this constructor is a
  /// converting constructor (C++ [class.conv.ctor]), which can be
  /// used for user-defined conversions.
  bool isConvertingConstructor(bool AllowExplicit) const;

  /// Determine whether this is a member template specialization that
  /// would copy the object to itself. Such constructors are never used to copy
  /// an object.
  bool isSpecializationCopyingObject() const;

  /// Determine whether this is an implicit constructor synthesized to
  /// model a call to a constructor inherited from a base class.
  bool isInheritingConstructor() const {
    return CXXConstructorDeclBits.IsInheritingConstructor;
  }

  /// State that this is an implicit constructor synthesized to
  /// model a call to a constructor inherited from a base class.
  void setInheritingConstructor(bool isIC = true) {
    CXXConstructorDeclBits.IsInheritingConstructor = isIC;
  }

  /// Get the constructor that this inheriting constructor is based on.
  InheritedConstructor getInheritedConstructor() const {
    return isInheritingConstructor() ?
      *getTrailingObjects<InheritedConstructor>() : InheritedConstructor();
  }

  CXXConstructorDecl *getCanonicalDecl() override {
    return cast<CXXConstructorDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXConstructorDecl *getCanonicalDecl() const {
    return const_cast<CXXConstructorDecl*>(this)->getCanonicalDecl();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXConstructor; }
};

/// Represents a C++ destructor within a class.
///
/// For example:
///
/// \code
/// class X {
/// public:
///   ~X(); // represented by a CXXDestructorDecl.
/// };
/// \endcode
class CXXDestructorDecl : public CXXMethodDecl {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  // FIXME: Don't allocate storage for these except in the first declaration
  // of a virtual destructor.
  FunctionDecl *OperatorDelete = nullptr;
  Expr *OperatorDeleteThisArg = nullptr;

  CXXDestructorDecl(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
                    const DeclarationNameInfo &NameInfo, QualType T,
                    TypeSourceInfo *TInfo, bool UsesFPIntrin, bool isInline,
                    bool isImplicitlyDeclared, ConstexprSpecKind ConstexprKind,
                    Expr *TrailingRequiresClause = nullptr)
      : CXXMethodDecl(CXXDestructor, C, RD, StartLoc, NameInfo, T, TInfo,
                      SC_None, UsesFPIntrin, isInline, ConstexprKind,
                      SourceLocation(), TrailingRequiresClause) {
    setImplicit(isImplicitlyDeclared);
  }

  void anchor() override;

public:
  static CXXDestructorDecl *
  Create(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
         const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
         bool UsesFPIntrin, bool isInline, bool isImplicitlyDeclared,
         ConstexprSpecKind ConstexprKind,
         Expr *TrailingRequiresClause = nullptr);
  static CXXDestructorDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  void setOperatorDelete(FunctionDecl *OD, Expr *ThisArg);

  const FunctionDecl *getOperatorDelete() const {
    return getCanonicalDecl()->OperatorDelete;
  }

  Expr *getOperatorDeleteThisArg() const {
    return getCanonicalDecl()->OperatorDeleteThisArg;
  }

  CXXDestructorDecl *getCanonicalDecl() override {
    return cast<CXXDestructorDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXDestructorDecl *getCanonicalDecl() const {
    return const_cast<CXXDestructorDecl*>(this)->getCanonicalDecl();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXDestructor; }
};

/// Represents a C++ conversion function within a class.
///
/// For example:
///
/// \code
/// class X {
/// public:
///   operator bool();
/// };
/// \endcode
class CXXConversionDecl : public CXXMethodDecl {
  CXXConversionDecl(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
                    const DeclarationNameInfo &NameInfo, QualType T,
                    TypeSourceInfo *TInfo, bool UsesFPIntrin, bool isInline,
                    ExplicitSpecifier ES, ConstexprSpecKind ConstexprKind,
                    SourceLocation EndLocation,
                    Expr *TrailingRequiresClause = nullptr)
      : CXXMethodDecl(CXXConversion, C, RD, StartLoc, NameInfo, T, TInfo,
                      SC_None, UsesFPIntrin, isInline, ConstexprKind,
                      EndLocation, TrailingRequiresClause),
        ExplicitSpec(ES) {}
  void anchor() override;

  ExplicitSpecifier ExplicitSpec;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static CXXConversionDecl *
  Create(ASTContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
         const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
         bool UsesFPIntrin, bool isInline, ExplicitSpecifier ES,
         ConstexprSpecKind ConstexprKind, SourceLocation EndLocation,
         Expr *TrailingRequiresClause = nullptr);
  static CXXConversionDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  ExplicitSpecifier getExplicitSpecifier() {
    return getCanonicalDecl()->ExplicitSpec;
  }

  const ExplicitSpecifier getExplicitSpecifier() const {
    return getCanonicalDecl()->ExplicitSpec;
  }

  /// Return true if the declaration is already resolved to be explicit.
  bool isExplicit() const { return getExplicitSpecifier().isExplicit(); }
  void setExplicitSpecifier(ExplicitSpecifier ES) { ExplicitSpec = ES; }

  /// Returns the type that this conversion function is converting to.
  QualType getConversionType() const {
    return getType()->castAs<FunctionType>()->getReturnType();
  }

  /// Determine whether this conversion function is a conversion from
  /// a lambda closure type to a block pointer.
  bool isLambdaToBlockPointerConversion() const;

  CXXConversionDecl *getCanonicalDecl() override {
    return cast<CXXConversionDecl>(FunctionDecl::getCanonicalDecl());
  }
  const CXXConversionDecl *getCanonicalDecl() const {
    return const_cast<CXXConversionDecl*>(this)->getCanonicalDecl();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == CXXConversion; }
};

/// Represents the language in a linkage specification.
///
/// The values are part of the serialization ABI for
/// ASTs and cannot be changed without altering that ABI.
enum class LinkageSpecLanguageIDs { C = 1, CXX = 2 };

/// Represents a linkage specification.
///
/// For example:
/// \code
///   extern "C" void foo();
/// \endcode
class LinkageSpecDecl : public Decl, public DeclContext {
  virtual void anchor();
  // This class stores some data in DeclContext::LinkageSpecDeclBits to save
  // some space. Use the provided accessors to access it.

  /// The source location for the extern keyword.
  SourceLocation ExternLoc;

  /// The source location for the right brace (if valid).
  SourceLocation RBraceLoc;

  LinkageSpecDecl(DeclContext *DC, SourceLocation ExternLoc,
                  SourceLocation LangLoc, LinkageSpecLanguageIDs lang,
                  bool HasBraces);

public:
  static LinkageSpecDecl *Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation ExternLoc,
                                 SourceLocation LangLoc,
                                 LinkageSpecLanguageIDs Lang, bool HasBraces);
  static LinkageSpecDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  /// Return the language specified by this linkage specification.
  LinkageSpecLanguageIDs getLanguage() const {
    return static_cast<LinkageSpecLanguageIDs>(LinkageSpecDeclBits.Language);
  }

  /// Set the language specified by this linkage specification.
  void setLanguage(LinkageSpecLanguageIDs L) {
    LinkageSpecDeclBits.Language = llvm::to_underlying(L);
  }

  /// Determines whether this linkage specification had braces in
  /// its syntactic form.
  bool hasBraces() const {
    assert(!RBraceLoc.isValid() || LinkageSpecDeclBits.HasBraces);
    return LinkageSpecDeclBits.HasBraces;
  }

  SourceLocation getExternLoc() const { return ExternLoc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setExternLoc(SourceLocation L) { ExternLoc = L; }
  void setRBraceLoc(SourceLocation L) {
    RBraceLoc = L;
    LinkageSpecDeclBits.HasBraces = RBraceLoc.isValid();
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    if (hasBraces())
      return getRBraceLoc();
    // No braces: get the end location of the (only) declaration in context
    // (if present).
    return decls_empty() ? getLocation() : decls_begin()->getEndLoc();
  }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(ExternLoc, getEndLoc());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == LinkageSpec; }

  static DeclContext *castToDeclContext(const LinkageSpecDecl *D) {
    return static_cast<DeclContext *>(const_cast<LinkageSpecDecl*>(D));
  }

  static LinkageSpecDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<LinkageSpecDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents C++ using-directive.
///
/// For example:
/// \code
///    using namespace std;
/// \endcode
///
/// \note UsingDirectiveDecl should be Decl not NamedDecl, but we provide
/// artificial names for all using-directives in order to store
/// them in DeclContext effectively.
class UsingDirectiveDecl : public NamedDecl {
  /// The location of the \c using keyword.
  SourceLocation UsingLoc;

  /// The location of the \c namespace keyword.
  SourceLocation NamespaceLoc;

  /// The nested-name-specifier that precedes the namespace.
  NestedNameSpecifierLoc QualifierLoc;

  /// The namespace nominated by this using-directive.
  NamedDecl *NominatedNamespace;

  /// Enclosing context containing both using-directive and nominated
  /// namespace.
  DeclContext *CommonAncestor;

  UsingDirectiveDecl(DeclContext *DC, SourceLocation UsingLoc,
                     SourceLocation NamespcLoc,
                     NestedNameSpecifierLoc QualifierLoc,
                     SourceLocation IdentLoc,
                     NamedDecl *Nominated,
                     DeclContext *CommonAncestor)
      : NamedDecl(UsingDirective, DC, IdentLoc, getName()), UsingLoc(UsingLoc),
        NamespaceLoc(NamespcLoc), QualifierLoc(QualifierLoc),
        NominatedNamespace(Nominated), CommonAncestor(CommonAncestor) {}

  /// Returns special DeclarationName used by using-directives.
  ///
  /// This is only used by DeclContext for storing UsingDirectiveDecls in
  /// its lookup structure.
  static DeclarationName getName() {
    return DeclarationName::getUsingDirectiveName();
  }

  void anchor() override;

public:
  friend class ASTDeclReader;

  // Friend for getUsingDirectiveName.
  friend class DeclContext;

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace, with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  NamedDecl *getNominatedNamespaceAsWritten() { return NominatedNamespace; }
  const NamedDecl *getNominatedNamespaceAsWritten() const {
    return NominatedNamespace;
  }

  /// Returns the namespace nominated by this using-directive.
  NamespaceDecl *getNominatedNamespace();

  const NamespaceDecl *getNominatedNamespace() const {
    return const_cast<UsingDirectiveDecl*>(this)->getNominatedNamespace();
  }

  /// Returns the common ancestor context of this using-directive and
  /// its nominated namespace.
  DeclContext *getCommonAncestor() { return CommonAncestor; }
  const DeclContext *getCommonAncestor() const { return CommonAncestor; }

  /// Return the location of the \c using keyword.
  SourceLocation getUsingLoc() const { return UsingLoc; }

  // FIXME: Could omit 'Key' in name.
  /// Returns the location of the \c namespace keyword.
  SourceLocation getNamespaceKeyLocation() const { return NamespaceLoc; }

  /// Returns the location of this using declaration's identifier.
  SourceLocation getIdentLocation() const { return getLocation(); }

  static UsingDirectiveDecl *Create(ASTContext &C, DeclContext *DC,
                                    SourceLocation UsingLoc,
                                    SourceLocation NamespaceLoc,
                                    NestedNameSpecifierLoc QualifierLoc,
                                    SourceLocation IdentLoc,
                                    NamedDecl *Nominated,
                                    DeclContext *CommonAncestor);
  static UsingDirectiveDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(UsingLoc, getLocation());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UsingDirective; }
};

/// Represents a C++ namespace alias.
///
/// For example:
///
/// \code
/// namespace Foo = Bar;
/// \endcode
class NamespaceAliasDecl : public NamedDecl,
                           public Redeclarable<NamespaceAliasDecl> {
  friend class ASTDeclReader;

  /// The location of the \c namespace keyword.
  SourceLocation NamespaceLoc;

  /// The location of the namespace's identifier.
  ///
  /// This is accessed by TargetNameLoc.
  SourceLocation IdentLoc;

  /// The nested-name-specifier that precedes the namespace.
  NestedNameSpecifierLoc QualifierLoc;

  /// The Decl that this alias points to, either a NamespaceDecl or
  /// a NamespaceAliasDecl.
  NamedDecl *Namespace;

  NamespaceAliasDecl(ASTContext &C, DeclContext *DC,
                     SourceLocation NamespaceLoc, SourceLocation AliasLoc,
                     IdentifierInfo *Alias, NestedNameSpecifierLoc QualifierLoc,
                     SourceLocation IdentLoc, NamedDecl *Namespace)
      : NamedDecl(NamespaceAlias, DC, AliasLoc, Alias), redeclarable_base(C),
        NamespaceLoc(NamespaceLoc), IdentLoc(IdentLoc),
        QualifierLoc(QualifierLoc), Namespace(Namespace) {}

  void anchor() override;

  using redeclarable_base = Redeclarable<NamespaceAliasDecl>;

  NamespaceAliasDecl *getNextRedeclarationImpl() override;
  NamespaceAliasDecl *getPreviousDeclImpl() override;
  NamespaceAliasDecl *getMostRecentDeclImpl() override;

public:
  static NamespaceAliasDecl *Create(ASTContext &C, DeclContext *DC,
                                    SourceLocation NamespaceLoc,
                                    SourceLocation AliasLoc,
                                    IdentifierInfo *Alias,
                                    NestedNameSpecifierLoc QualifierLoc,
                                    SourceLocation IdentLoc,
                                    NamedDecl *Namespace);

  static NamespaceAliasDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;

  NamespaceAliasDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const NamespaceAliasDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace, with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the
  /// name of the namespace.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  /// Retrieve the namespace declaration aliased by this directive.
  NamespaceDecl *getNamespace() {
    if (auto *AD = dyn_cast<NamespaceAliasDecl>(Namespace))
      return AD->getNamespace();

    return cast<NamespaceDecl>(Namespace);
  }

  const NamespaceDecl *getNamespace() const {
    return const_cast<NamespaceAliasDecl *>(this)->getNamespace();
  }

  /// Returns the location of the alias name, i.e. 'foo' in
  /// "namespace foo = ns::bar;".
  SourceLocation getAliasLoc() const { return getLocation(); }

  /// Returns the location of the \c namespace keyword.
  SourceLocation getNamespaceLoc() const { return NamespaceLoc; }

  /// Returns the location of the identifier in the named namespace.
  SourceLocation getTargetNameLoc() const { return IdentLoc; }

  /// Retrieve the namespace that this alias refers to, which
  /// may either be a NamespaceDecl or a NamespaceAliasDecl.
  NamedDecl *getAliasedNamespace() const { return Namespace; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(NamespaceLoc, IdentLoc);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == NamespaceAlias; }
};

/// Implicit declaration of a temporary that was materialized by
/// a MaterializeTemporaryExpr and lifetime-extended by a declaration
class LifetimeExtendedTemporaryDecl final
    : public Decl,
      public Mergeable<LifetimeExtendedTemporaryDecl> {
  friend class MaterializeTemporaryExpr;
  friend class ASTDeclReader;

  Stmt *ExprWithTemporary = nullptr;

  /// The declaration which lifetime-extended this reference, if any.
  /// Either a VarDecl, or (for a ctor-initializer) a FieldDecl.
  ValueDecl *ExtendingDecl = nullptr;
  unsigned ManglingNumber;

  mutable APValue *Value = nullptr;

  virtual void anchor();

  LifetimeExtendedTemporaryDecl(Expr *Temp, ValueDecl *EDecl, unsigned Mangling)
      : Decl(Decl::LifetimeExtendedTemporary, EDecl->getDeclContext(),
             EDecl->getLocation()),
        ExprWithTemporary(Temp), ExtendingDecl(EDecl),
        ManglingNumber(Mangling) {}

  LifetimeExtendedTemporaryDecl(EmptyShell)
      : Decl(Decl::LifetimeExtendedTemporary, EmptyShell{}) {}

public:
  static LifetimeExtendedTemporaryDecl *Create(Expr *Temp, ValueDecl *EDec,
                                               unsigned Mangling) {
    return new (EDec->getASTContext(), EDec->getDeclContext())
        LifetimeExtendedTemporaryDecl(Temp, EDec, Mangling);
  }
  static LifetimeExtendedTemporaryDecl *CreateDeserialized(ASTContext &C,
                                                           GlobalDeclID ID) {
    return new (C, ID) LifetimeExtendedTemporaryDecl(EmptyShell{});
  }

  ValueDecl *getExtendingDecl() { return ExtendingDecl; }
  const ValueDecl *getExtendingDecl() const { return ExtendingDecl; }

  /// Retrieve the storage duration for the materialized temporary.
  StorageDuration getStorageDuration() const;

  /// Retrieve the expression to which the temporary materialization conversion
  /// was applied. This isn't necessarily the initializer of the temporary due
  /// to the C++98 delayed materialization rules, but
  /// skipRValueSubobjectAdjustments can be used to find said initializer within
  /// the subexpression.
  Expr *getTemporaryExpr() { return cast<Expr>(ExprWithTemporary); }
  const Expr *getTemporaryExpr() const { return cast<Expr>(ExprWithTemporary); }

  unsigned getManglingNumber() const { return ManglingNumber; }

  /// Get the storage for the constant value of a materialized temporary
  /// of static storage duration.
  APValue *getOrCreateValue(bool MayCreate) const;

  APValue *getValue() const { return Value; }

  // Iterators
  Stmt::child_range childrenExpr() {
    return Stmt::child_range(&ExprWithTemporary, &ExprWithTemporary + 1);
  }

  Stmt::const_child_range childrenExpr() const {
    return Stmt::const_child_range(&ExprWithTemporary, &ExprWithTemporary + 1);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K == Decl::LifetimeExtendedTemporary;
  }
};

/// Represents a shadow declaration implicitly introduced into a scope by a
/// (resolved) using-declaration or using-enum-declaration to achieve
/// the desired lookup semantics.
///
/// For example:
/// \code
/// namespace A {
///   void foo();
///   void foo(int);
///   struct foo {};
///   enum bar { bar1, bar2 };
/// }
/// namespace B {
///   // add a UsingDecl and three UsingShadowDecls (named foo) to B.
///   using A::foo;
///   // adds UsingEnumDecl and two UsingShadowDecls (named bar1 and bar2) to B.
///   using enum A::bar;
/// }
/// \endcode
class UsingShadowDecl : public NamedDecl, public Redeclarable<UsingShadowDecl> {
  friend class BaseUsingDecl;

  /// The referenced declaration.
  NamedDecl *Underlying = nullptr;

  /// The using declaration which introduced this decl or the next using
  /// shadow declaration contained in the aforementioned using declaration.
  NamedDecl *UsingOrNextShadow = nullptr;

  void anchor() override;

  using redeclarable_base = Redeclarable<UsingShadowDecl>;

  UsingShadowDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  UsingShadowDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  UsingShadowDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

protected:
  UsingShadowDecl(Kind K, ASTContext &C, DeclContext *DC, SourceLocation Loc,
                  DeclarationName Name, BaseUsingDecl *Introducer,
                  NamedDecl *Target);
  UsingShadowDecl(Kind K, ASTContext &C, EmptyShell);

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static UsingShadowDecl *Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation Loc, DeclarationName Name,
                                 BaseUsingDecl *Introducer, NamedDecl *Target) {
    return new (C, DC)
        UsingShadowDecl(UsingShadow, C, DC, Loc, Name, Introducer, Target);
  }

  static UsingShadowDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  UsingShadowDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const UsingShadowDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  /// Gets the underlying declaration which has been brought into the
  /// local scope.
  NamedDecl *getTargetDecl() const { return Underlying; }

  /// Sets the underlying declaration which has been brought into the
  /// local scope.
  void setTargetDecl(NamedDecl *ND) {
    assert(ND && "Target decl is null!");
    Underlying = ND;
    // A UsingShadowDecl is never a friend or local extern declaration, even
    // if it is a shadow declaration for one.
    IdentifierNamespace =
        ND->getIdentifierNamespace() &
        ~(IDNS_OrdinaryFriend | IDNS_TagFriend | IDNS_LocalExtern);
  }

  /// Gets the (written or instantiated) using declaration that introduced this
  /// declaration.
  BaseUsingDecl *getIntroducer() const;

  /// The next using shadow declaration contained in the shadow decl
  /// chain of the using declaration which introduced this decl.
  UsingShadowDecl *getNextUsingShadowDecl() const {
    return dyn_cast_or_null<UsingShadowDecl>(UsingOrNextShadow);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K == Decl::UsingShadow || K == Decl::ConstructorUsingShadow;
  }
};

/// Represents a C++ declaration that introduces decls from somewhere else. It
/// provides a set of the shadow decls so introduced.

class BaseUsingDecl : public NamedDecl {
  /// The first shadow declaration of the shadow decl chain associated
  /// with this using declaration.
  ///
  /// The bool member of the pair is a bool flag a derived type may use
  /// (UsingDecl makes use of it).
  llvm::PointerIntPair<UsingShadowDecl *, 1, bool> FirstUsingShadow;

protected:
  BaseUsingDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName N)
      : NamedDecl(DK, DC, L, N), FirstUsingShadow(nullptr, false) {}

private:
  void anchor() override;

protected:
  /// A bool flag for use by a derived type
  bool getShadowFlag() const { return FirstUsingShadow.getInt(); }

  /// A bool flag a derived type may set
  void setShadowFlag(bool V) { FirstUsingShadow.setInt(V); }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Iterates through the using shadow declarations associated with
  /// this using declaration.
  class shadow_iterator {
    /// The current using shadow declaration.
    UsingShadowDecl *Current = nullptr;

  public:
    using value_type = UsingShadowDecl *;
    using reference = UsingShadowDecl *;
    using pointer = UsingShadowDecl *;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;

    shadow_iterator() = default;
    explicit shadow_iterator(UsingShadowDecl *C) : Current(C) {}

    reference operator*() const { return Current; }
    pointer operator->() const { return Current; }

    shadow_iterator &operator++() {
      Current = Current->getNextUsingShadowDecl();
      return *this;
    }

    shadow_iterator operator++(int) {
      shadow_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(shadow_iterator x, shadow_iterator y) {
      return x.Current == y.Current;
    }
    friend bool operator!=(shadow_iterator x, shadow_iterator y) {
      return x.Current != y.Current;
    }
  };

  using shadow_range = llvm::iterator_range<shadow_iterator>;

  shadow_range shadows() const {
    return shadow_range(shadow_begin(), shadow_end());
  }

  shadow_iterator shadow_begin() const {
    return shadow_iterator(FirstUsingShadow.getPointer());
  }

  shadow_iterator shadow_end() const { return shadow_iterator(); }

  /// Return the number of shadowed declarations associated with this
  /// using declaration.
  unsigned shadow_size() const {
    return std::distance(shadow_begin(), shadow_end());
  }

  void addShadowDecl(UsingShadowDecl *S);
  void removeShadowDecl(UsingShadowDecl *S);

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Using || K == UsingEnum; }
};

/// Represents a C++ using-declaration.
///
/// For example:
/// \code
///    using someNameSpace::someIdentifier;
/// \endcode
class UsingDecl : public BaseUsingDecl, public Mergeable<UsingDecl> {
  /// The source location of the 'using' keyword itself.
  SourceLocation UsingLocation;

  /// The nested-name-specifier that precedes the name.
  NestedNameSpecifierLoc QualifierLoc;

  /// Provides source/type location info for the declaration name
  /// embedded in the ValueDecl base class.
  DeclarationNameLoc DNLoc;

  UsingDecl(DeclContext *DC, SourceLocation UL,
            NestedNameSpecifierLoc QualifierLoc,
            const DeclarationNameInfo &NameInfo, bool HasTypenameKeyword)
      : BaseUsingDecl(Using, DC, NameInfo.getLoc(), NameInfo.getName()),
        UsingLocation(UL), QualifierLoc(QualifierLoc),
        DNLoc(NameInfo.getInfo()) {
    setShadowFlag(HasTypenameKeyword);
  }

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Return the source location of the 'using' keyword.
  SourceLocation getUsingLoc() const { return UsingLocation; }

  /// Set the source location of the 'using' keyword.
  void setUsingLoc(SourceLocation L) { UsingLocation = L; }

  /// Retrieve the nested-name-specifier that qualifies the name,
  /// with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the name.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation(), DNLoc);
  }

  /// Return true if it is a C++03 access declaration (no 'using').
  bool isAccessDeclaration() const { return UsingLocation.isInvalid(); }

  /// Return true if the using declaration has 'typename'.
  bool hasTypename() const { return getShadowFlag(); }

  /// Sets whether the using declaration has 'typename'.
  void setTypename(bool TN) { setShadowFlag(TN); }

  static UsingDecl *Create(ASTContext &C, DeclContext *DC,
                           SourceLocation UsingL,
                           NestedNameSpecifierLoc QualifierLoc,
                           const DeclarationNameInfo &NameInfo,
                           bool HasTypenameKeyword);

  static UsingDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Retrieves the canonical declaration of this declaration.
  UsingDecl *getCanonicalDecl() override {
    return cast<UsingDecl>(getFirstDecl());
  }
  const UsingDecl *getCanonicalDecl() const {
    return cast<UsingDecl>(getFirstDecl());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Using; }
};

/// Represents a shadow constructor declaration introduced into a
/// class by a C++11 using-declaration that names a constructor.
///
/// For example:
/// \code
/// struct Base { Base(int); };
/// struct Derived {
///    using Base::Base; // creates a UsingDecl and a ConstructorUsingShadowDecl
/// };
/// \endcode
class ConstructorUsingShadowDecl final : public UsingShadowDecl {
  /// If this constructor using declaration inherted the constructor
  /// from an indirect base class, this is the ConstructorUsingShadowDecl
  /// in the named direct base class from which the declaration was inherited.
  ConstructorUsingShadowDecl *NominatedBaseClassShadowDecl = nullptr;

  /// If this constructor using declaration inherted the constructor
  /// from an indirect base class, this is the ConstructorUsingShadowDecl
  /// that will be used to construct the unique direct or virtual base class
  /// that receives the constructor arguments.
  ConstructorUsingShadowDecl *ConstructedBaseClassShadowDecl = nullptr;

  /// \c true if the constructor ultimately named by this using shadow
  /// declaration is within a virtual base class subobject of the class that
  /// contains this declaration.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsVirtual : 1;

  ConstructorUsingShadowDecl(ASTContext &C, DeclContext *DC, SourceLocation Loc,
                             UsingDecl *Using, NamedDecl *Target,
                             bool TargetInVirtualBase)
      : UsingShadowDecl(ConstructorUsingShadow, C, DC, Loc,
                        Using->getDeclName(), Using,
                        Target->getUnderlyingDecl()),
        NominatedBaseClassShadowDecl(
            dyn_cast<ConstructorUsingShadowDecl>(Target)),
        ConstructedBaseClassShadowDecl(NominatedBaseClassShadowDecl),
        IsVirtual(TargetInVirtualBase) {
    // If we found a constructor that chains to a constructor for a virtual
    // base, we should directly call that virtual base constructor instead.
    // FIXME: This logic belongs in Sema.
    if (NominatedBaseClassShadowDecl &&
        NominatedBaseClassShadowDecl->constructsVirtualBase()) {
      ConstructedBaseClassShadowDecl =
          NominatedBaseClassShadowDecl->ConstructedBaseClassShadowDecl;
      IsVirtual = true;
    }
  }

  ConstructorUsingShadowDecl(ASTContext &C, EmptyShell Empty)
      : UsingShadowDecl(ConstructorUsingShadow, C, Empty), IsVirtual(false) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static ConstructorUsingShadowDecl *Create(ASTContext &C, DeclContext *DC,
                                            SourceLocation Loc,
                                            UsingDecl *Using, NamedDecl *Target,
                                            bool IsVirtual);
  static ConstructorUsingShadowDecl *CreateDeserialized(ASTContext &C,
                                                        GlobalDeclID ID);

  /// Override the UsingShadowDecl's getIntroducer, returning the UsingDecl that
  /// introduced this.
  UsingDecl *getIntroducer() const {
    return cast<UsingDecl>(UsingShadowDecl::getIntroducer());
  }

  /// Returns the parent of this using shadow declaration, which
  /// is the class in which this is declared.
  //@{
  const CXXRecordDecl *getParent() const {
    return cast<CXXRecordDecl>(getDeclContext());
  }
  CXXRecordDecl *getParent() {
    return cast<CXXRecordDecl>(getDeclContext());
  }
  //@}

  /// Get the inheriting constructor declaration for the direct base
  /// class from which this using shadow declaration was inherited, if there is
  /// one. This can be different for each redeclaration of the same shadow decl.
  ConstructorUsingShadowDecl *getNominatedBaseClassShadowDecl() const {
    return NominatedBaseClassShadowDecl;
  }

  /// Get the inheriting constructor declaration for the base class
  /// for which we don't have an explicit initializer, if there is one.
  ConstructorUsingShadowDecl *getConstructedBaseClassShadowDecl() const {
    return ConstructedBaseClassShadowDecl;
  }

  /// Get the base class that was named in the using declaration. This
  /// can be different for each redeclaration of this same shadow decl.
  CXXRecordDecl *getNominatedBaseClass() const;

  /// Get the base class whose constructor or constructor shadow
  /// declaration is passed the constructor arguments.
  CXXRecordDecl *getConstructedBaseClass() const {
    return cast<CXXRecordDecl>((ConstructedBaseClassShadowDecl
                                    ? ConstructedBaseClassShadowDecl
                                    : getTargetDecl())
                                   ->getDeclContext());
  }

  /// Returns \c true if the constructed base class is a virtual base
  /// class subobject of this declaration's class.
  bool constructsVirtualBase() const {
    return IsVirtual;
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ConstructorUsingShadow; }
};

/// Represents a C++ using-enum-declaration.
///
/// For example:
/// \code
///    using enum SomeEnumTag ;
/// \endcode

class UsingEnumDecl : public BaseUsingDecl, public Mergeable<UsingEnumDecl> {
  /// The source location of the 'using' keyword itself.
  SourceLocation UsingLocation;
  /// The source location of the 'enum' keyword.
  SourceLocation EnumLocation;
  /// 'qual::SomeEnum' as an EnumType, possibly with Elaborated/Typedef sugar.
  TypeSourceInfo *EnumType;

  UsingEnumDecl(DeclContext *DC, DeclarationName DN, SourceLocation UL,
                SourceLocation EL, SourceLocation NL, TypeSourceInfo *EnumType)
      : BaseUsingDecl(UsingEnum, DC, NL, DN), UsingLocation(UL), EnumLocation(EL),
        EnumType(EnumType){}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// The source location of the 'using' keyword.
  SourceLocation getUsingLoc() const { return UsingLocation; }
  void setUsingLoc(SourceLocation L) { UsingLocation = L; }

  /// The source location of the 'enum' keyword.
  SourceLocation getEnumLoc() const { return EnumLocation; }
  void setEnumLoc(SourceLocation L) { EnumLocation = L; }
  NestedNameSpecifier *getQualifier() const {
    return getQualifierLoc().getNestedNameSpecifier();
  }
  NestedNameSpecifierLoc getQualifierLoc() const {
    if (auto ETL = EnumType->getTypeLoc().getAs<ElaboratedTypeLoc>())
      return ETL.getQualifierLoc();
    return NestedNameSpecifierLoc();
  }
  // Returns the "qualifier::Name" part as a TypeLoc.
  TypeLoc getEnumTypeLoc() const {
    return EnumType->getTypeLoc();
  }
  TypeSourceInfo *getEnumType() const {
    return EnumType;
  }
  void setEnumType(TypeSourceInfo *TSI) { EnumType = TSI; }

public:
  EnumDecl *getEnumDecl() const { return cast<EnumDecl>(EnumType->getType()->getAsTagDecl()); }

  static UsingEnumDecl *Create(ASTContext &C, DeclContext *DC,
                               SourceLocation UsingL, SourceLocation EnumL,
                               SourceLocation NameL, TypeSourceInfo *EnumType);

  static UsingEnumDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Retrieves the canonical declaration of this declaration.
  UsingEnumDecl *getCanonicalDecl() override {
    return cast<UsingEnumDecl>(getFirstDecl());
  }
  const UsingEnumDecl *getCanonicalDecl() const {
    return cast<UsingEnumDecl>(getFirstDecl());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UsingEnum; }
};

/// Represents a pack of using declarations that a single
/// using-declarator pack-expanded into.
///
/// \code
/// template<typename ...T> struct X : T... {
///   using T::operator()...;
///   using T::operator T...;
/// };
/// \endcode
///
/// In the second case above, the UsingPackDecl will have the name
/// 'operator T' (which contains an unexpanded pack), but the individual
/// UsingDecls and UsingShadowDecls will have more reasonable names.
class UsingPackDecl final
    : public NamedDecl, public Mergeable<UsingPackDecl>,
      private llvm::TrailingObjects<UsingPackDecl, NamedDecl *> {
  /// The UnresolvedUsingValueDecl or UnresolvedUsingTypenameDecl from
  /// which this waas instantiated.
  NamedDecl *InstantiatedFrom;

  /// The number of using-declarations created by this pack expansion.
  unsigned NumExpansions;

  UsingPackDecl(DeclContext *DC, NamedDecl *InstantiatedFrom,
                ArrayRef<NamedDecl *> UsingDecls)
      : NamedDecl(UsingPack, DC,
                  InstantiatedFrom ? InstantiatedFrom->getLocation()
                                   : SourceLocation(),
                  InstantiatedFrom ? InstantiatedFrom->getDeclName()
                                   : DeclarationName()),
        InstantiatedFrom(InstantiatedFrom), NumExpansions(UsingDecls.size()) {
    std::uninitialized_copy(UsingDecls.begin(), UsingDecls.end(),
                            getTrailingObjects<NamedDecl *>());
  }

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  /// Get the using declaration from which this was instantiated. This will
  /// always be an UnresolvedUsingValueDecl or an UnresolvedUsingTypenameDecl
  /// that is a pack expansion.
  NamedDecl *getInstantiatedFromUsingDecl() const { return InstantiatedFrom; }

  /// Get the set of using declarations that this pack expanded into. Note that
  /// some of these may still be unresolved.
  ArrayRef<NamedDecl *> expansions() const {
    return llvm::ArrayRef(getTrailingObjects<NamedDecl *>(), NumExpansions);
  }

  static UsingPackDecl *Create(ASTContext &C, DeclContext *DC,
                               NamedDecl *InstantiatedFrom,
                               ArrayRef<NamedDecl *> UsingDecls);

  static UsingPackDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                           unsigned NumExpansions);

  SourceRange getSourceRange() const override LLVM_READONLY {
    return InstantiatedFrom->getSourceRange();
  }

  UsingPackDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const UsingPackDecl *getCanonicalDecl() const { return getFirstDecl(); }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UsingPack; }
};

/// Represents a dependent using declaration which was not marked with
/// \c typename.
///
/// Unlike non-dependent using declarations, these *only* bring through
/// non-types; otherwise they would break two-phase lookup.
///
/// \code
/// template \<class T> class A : public Base<T> {
///   using Base<T>::foo;
/// };
/// \endcode
class UnresolvedUsingValueDecl : public ValueDecl,
                                 public Mergeable<UnresolvedUsingValueDecl> {
  /// The source location of the 'using' keyword
  SourceLocation UsingLocation;

  /// If this is a pack expansion, the location of the '...'.
  SourceLocation EllipsisLoc;

  /// The nested-name-specifier that precedes the name.
  NestedNameSpecifierLoc QualifierLoc;

  /// Provides source/type location info for the declaration name
  /// embedded in the ValueDecl base class.
  DeclarationNameLoc DNLoc;

  UnresolvedUsingValueDecl(DeclContext *DC, QualType Ty,
                           SourceLocation UsingLoc,
                           NestedNameSpecifierLoc QualifierLoc,
                           const DeclarationNameInfo &NameInfo,
                           SourceLocation EllipsisLoc)
      : ValueDecl(UnresolvedUsingValue, DC,
                  NameInfo.getLoc(), NameInfo.getName(), Ty),
        UsingLocation(UsingLoc), EllipsisLoc(EllipsisLoc),
        QualifierLoc(QualifierLoc), DNLoc(NameInfo.getInfo()) {}

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Returns the source location of the 'using' keyword.
  SourceLocation getUsingLoc() const { return UsingLocation; }

  /// Set the source location of the 'using' keyword.
  void setUsingLoc(SourceLocation L) { UsingLocation = L; }

  /// Return true if it is a C++03 access declaration (no 'using').
  bool isAccessDeclaration() const { return UsingLocation.isInvalid(); }

  /// Retrieve the nested-name-specifier that qualifies the name,
  /// with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the name.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation(), DNLoc);
  }

  /// Determine whether this is a pack expansion.
  bool isPackExpansion() const {
    return EllipsisLoc.isValid();
  }

  /// Get the location of the ellipsis if this is a pack expansion.
  SourceLocation getEllipsisLoc() const {
    return EllipsisLoc;
  }

  static UnresolvedUsingValueDecl *
    Create(ASTContext &C, DeclContext *DC, SourceLocation UsingLoc,
           NestedNameSpecifierLoc QualifierLoc,
           const DeclarationNameInfo &NameInfo, SourceLocation EllipsisLoc);

  static UnresolvedUsingValueDecl *CreateDeserialized(ASTContext &C,
                                                      GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Retrieves the canonical declaration of this declaration.
  UnresolvedUsingValueDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const UnresolvedUsingValueDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UnresolvedUsingValue; }
};

/// Represents a dependent using declaration which was marked with
/// \c typename.
///
/// \code
/// template \<class T> class A : public Base<T> {
///   using typename Base<T>::foo;
/// };
/// \endcode
///
/// The type associated with an unresolved using typename decl is
/// currently always a typename type.
class UnresolvedUsingTypenameDecl
    : public TypeDecl,
      public Mergeable<UnresolvedUsingTypenameDecl> {
  friend class ASTDeclReader;

  /// The source location of the 'typename' keyword
  SourceLocation TypenameLocation;

  /// If this is a pack expansion, the location of the '...'.
  SourceLocation EllipsisLoc;

  /// The nested-name-specifier that precedes the name.
  NestedNameSpecifierLoc QualifierLoc;

  UnresolvedUsingTypenameDecl(DeclContext *DC, SourceLocation UsingLoc,
                              SourceLocation TypenameLoc,
                              NestedNameSpecifierLoc QualifierLoc,
                              SourceLocation TargetNameLoc,
                              IdentifierInfo *TargetName,
                              SourceLocation EllipsisLoc)
    : TypeDecl(UnresolvedUsingTypename, DC, TargetNameLoc, TargetName,
               UsingLoc),
      TypenameLocation(TypenameLoc), EllipsisLoc(EllipsisLoc),
      QualifierLoc(QualifierLoc) {}

  void anchor() override;

public:
  /// Returns the source location of the 'using' keyword.
  SourceLocation getUsingLoc() const { return getBeginLoc(); }

  /// Returns the source location of the 'typename' keyword.
  SourceLocation getTypenameLoc() const { return TypenameLocation; }

  /// Retrieve the nested-name-specifier that qualifies the name,
  /// with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const { return QualifierLoc; }

  /// Retrieve the nested-name-specifier that qualifies the name.
  NestedNameSpecifier *getQualifier() const {
    return QualifierLoc.getNestedNameSpecifier();
  }

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation());
  }

  /// Determine whether this is a pack expansion.
  bool isPackExpansion() const {
    return EllipsisLoc.isValid();
  }

  /// Get the location of the ellipsis if this is a pack expansion.
  SourceLocation getEllipsisLoc() const {
    return EllipsisLoc;
  }

  static UnresolvedUsingTypenameDecl *
    Create(ASTContext &C, DeclContext *DC, SourceLocation UsingLoc,
           SourceLocation TypenameLoc, NestedNameSpecifierLoc QualifierLoc,
           SourceLocation TargetNameLoc, DeclarationName TargetName,
           SourceLocation EllipsisLoc);

  static UnresolvedUsingTypenameDecl *CreateDeserialized(ASTContext &C,
                                                         GlobalDeclID ID);

  /// Retrieves the canonical declaration of this declaration.
  UnresolvedUsingTypenameDecl *getCanonicalDecl() override {
    return getFirstDecl();
  }
  const UnresolvedUsingTypenameDecl *getCanonicalDecl() const {
    return getFirstDecl();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == UnresolvedUsingTypename; }
};

/// This node is generated when a using-declaration that was annotated with
/// __attribute__((using_if_exists)) failed to resolve to a known declaration.
/// In that case, Sema builds a UsingShadowDecl whose target is an instance of
/// this declaration, adding it to the current scope. Referring to this
/// declaration in any way is an error.
class UnresolvedUsingIfExistsDecl final : public NamedDecl {
  UnresolvedUsingIfExistsDecl(DeclContext *DC, SourceLocation Loc,
                              DeclarationName Name);

  void anchor() override;

public:
  static UnresolvedUsingIfExistsDecl *Create(ASTContext &Ctx, DeclContext *DC,
                                             SourceLocation Loc,
                                             DeclarationName Name);
  static UnresolvedUsingIfExistsDecl *CreateDeserialized(ASTContext &Ctx,
                                                         GlobalDeclID ID);

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decl::UnresolvedUsingIfExists; }
};

/// Represents a C++11 static_assert declaration.
class StaticAssertDecl : public Decl {
  llvm::PointerIntPair<Expr *, 1, bool> AssertExprAndFailed;
  Expr *Message;
  SourceLocation RParenLoc;

  StaticAssertDecl(DeclContext *DC, SourceLocation StaticAssertLoc,
                   Expr *AssertExpr, Expr *Message, SourceLocation RParenLoc,
                   bool Failed)
      : Decl(StaticAssert, DC, StaticAssertLoc),
        AssertExprAndFailed(AssertExpr, Failed), Message(Message),
        RParenLoc(RParenLoc) {}

  virtual void anchor();

public:
  friend class ASTDeclReader;

  static StaticAssertDecl *Create(ASTContext &C, DeclContext *DC,
                                  SourceLocation StaticAssertLoc,
                                  Expr *AssertExpr, Expr *Message,
                                  SourceLocation RParenLoc, bool Failed);
  static StaticAssertDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  Expr *getAssertExpr() { return AssertExprAndFailed.getPointer(); }
  const Expr *getAssertExpr() const { return AssertExprAndFailed.getPointer(); }

  Expr *getMessage() { return Message; }
  const Expr *getMessage() const { return Message; }

  bool isFailed() const { return AssertExprAndFailed.getInt(); }

  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getLocation(), getRParenLoc());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == StaticAssert; }
};

/// A binding in a decomposition declaration. For instance, given:
///
///   int n[3];
///   auto &[a, b, c] = n;
///
/// a, b, and c are BindingDecls, whose bindings are the expressions
/// x[0], x[1], and x[2] respectively, where x is the implicit
/// DecompositionDecl of type 'int (&)[3]'.
class BindingDecl : public ValueDecl {
  /// The declaration that this binding binds to part of.
  ValueDecl *Decomp;
  /// The binding represented by this declaration. References to this
  /// declaration are effectively equivalent to this expression (except
  /// that it is only evaluated once at the point of declaration of the
  /// binding).
  Expr *Binding = nullptr;

  BindingDecl(DeclContext *DC, SourceLocation IdLoc, IdentifierInfo *Id)
      : ValueDecl(Decl::Binding, DC, IdLoc, Id, QualType()) {}

  void anchor() override;

public:
  friend class ASTDeclReader;

  static BindingDecl *Create(ASTContext &C, DeclContext *DC,
                             SourceLocation IdLoc, IdentifierInfo *Id);
  static BindingDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  /// Get the expression to which this declaration is bound. This may be null
  /// in two different cases: while parsing the initializer for the
  /// decomposition declaration, and when the initializer is type-dependent.
  Expr *getBinding() const { return Binding; }

  /// Get the decomposition declaration that this binding represents a
  /// decomposition of.
  ValueDecl *getDecomposedDecl() const { return Decomp; }

  /// Get the variable (if any) that holds the value of evaluating the binding.
  /// Only present for user-defined bindings for tuple-like types.
  VarDecl *getHoldingVar() const;

  /// Set the binding for this BindingDecl, along with its declared type (which
  /// should be a possibly-cv-qualified form of the type of the binding, or a
  /// reference to such a type).
  void setBinding(QualType DeclaredType, Expr *Binding) {
    setType(DeclaredType);
    this->Binding = Binding;
  }

  /// Set the decomposed variable for this BindingDecl.
  void setDecomposedDecl(ValueDecl *Decomposed) { Decomp = Decomposed; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decl::Binding; }
};

/// A decomposition declaration. For instance, given:
///
///   int n[3];
///   auto &[a, b, c] = n;
///
/// the second line declares a DecompositionDecl of type 'int (&)[3]', and
/// three BindingDecls (named a, b, and c). An instance of this class is always
/// unnamed, but behaves in almost all other respects like a VarDecl.
class DecompositionDecl final
    : public VarDecl,
      private llvm::TrailingObjects<DecompositionDecl, BindingDecl *> {
  /// The number of BindingDecl*s following this object.
  unsigned NumBindings;

  DecompositionDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
                    SourceLocation LSquareLoc, QualType T,
                    TypeSourceInfo *TInfo, StorageClass SC,
                    ArrayRef<BindingDecl *> Bindings)
      : VarDecl(Decomposition, C, DC, StartLoc, LSquareLoc, nullptr, T, TInfo,
                SC),
        NumBindings(Bindings.size()) {
    std::uninitialized_copy(Bindings.begin(), Bindings.end(),
                            getTrailingObjects<BindingDecl *>());
    for (auto *B : Bindings)
      B->setDecomposedDecl(this);
  }

  void anchor() override;

public:
  friend class ASTDeclReader;
  friend TrailingObjects;

  static DecompositionDecl *Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation StartLoc,
                                   SourceLocation LSquareLoc,
                                   QualType T, TypeSourceInfo *TInfo,
                                   StorageClass S,
                                   ArrayRef<BindingDecl *> Bindings);
  static DecompositionDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                               unsigned NumBindings);

  ArrayRef<BindingDecl *> bindings() const {
    return llvm::ArrayRef(getTrailingObjects<BindingDecl *>(), NumBindings);
  }

  void printName(raw_ostream &OS, const PrintingPolicy &Policy) const override;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decomposition; }
};

/// An instance of this class represents the declaration of a property
/// member.  This is a Microsoft extension to C++, first introduced in
/// Visual Studio .NET 2003 as a parallel to similar features in C#
/// and Managed C++.
///
/// A property must always be a non-static class member.
///
/// A property member superficially resembles a non-static data
/// member, except preceded by a property attribute:
///   __declspec(property(get=GetX, put=PutX)) int x;
/// Either (but not both) of the 'get' and 'put' names may be omitted.
///
/// A reference to a property is always an lvalue.  If the lvalue
/// undergoes lvalue-to-rvalue conversion, then a getter name is
/// required, and that member is called with no arguments.
/// If the lvalue is assigned into, then a setter name is required,
/// and that member is called with one argument, the value assigned.
/// Both operations are potentially overloaded.  Compound assignments
/// are permitted, as are the increment and decrement operators.
///
/// The getter and putter methods are permitted to be overloaded,
/// although their return and parameter types are subject to certain
/// restrictions according to the type of the property.
///
/// A property declared using an incomplete array type may
/// additionally be subscripted, adding extra parameters to the getter
/// and putter methods.
class MSPropertyDecl : public DeclaratorDecl {
  IdentifierInfo *GetterId, *SetterId;

  MSPropertyDecl(DeclContext *DC, SourceLocation L, DeclarationName N,
                 QualType T, TypeSourceInfo *TInfo, SourceLocation StartL,
                 IdentifierInfo *Getter, IdentifierInfo *Setter)
      : DeclaratorDecl(MSProperty, DC, L, N, T, TInfo, StartL),
        GetterId(Getter), SetterId(Setter) {}

  void anchor() override;
public:
  friend class ASTDeclReader;

  static MSPropertyDecl *Create(ASTContext &C, DeclContext *DC,
                                SourceLocation L, DeclarationName N, QualType T,
                                TypeSourceInfo *TInfo, SourceLocation StartL,
                                IdentifierInfo *Getter, IdentifierInfo *Setter);
  static MSPropertyDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  static bool classof(const Decl *D) { return D->getKind() == MSProperty; }

  bool hasGetter() const { return GetterId != nullptr; }
  IdentifierInfo* getGetterId() const { return GetterId; }
  bool hasSetter() const { return SetterId != nullptr; }
  IdentifierInfo* getSetterId() const { return SetterId; }
};

/// Parts of a decomposed MSGuidDecl. Factored out to avoid unnecessary
/// dependencies on DeclCXX.h.
struct MSGuidDeclParts {
  /// {01234567-...
  uint32_t Part1;
  /// ...-89ab-...
  uint16_t Part2;
  /// ...-cdef-...
  uint16_t Part3;
  /// ...-0123-456789abcdef}
  uint8_t Part4And5[8];

  uint64_t getPart4And5AsUint64() const {
    uint64_t Val;
    memcpy(&Val, &Part4And5, sizeof(Part4And5));
    return Val;
  }
};

/// A global _GUID constant. These are implicitly created by UuidAttrs.
///
///   struct _declspec(uuid("01234567-89ab-cdef-0123-456789abcdef")) X{};
///
/// X is a CXXRecordDecl that contains a UuidAttr that references the (unique)
/// MSGuidDecl for the specified UUID.
class MSGuidDecl : public ValueDecl,
                   public Mergeable<MSGuidDecl>,
                   public llvm::FoldingSetNode {
public:
  using Parts = MSGuidDeclParts;

private:
  /// The decomposed form of the UUID.
  Parts PartVal;

  /// The resolved value of the UUID as an APValue. Computed on demand and
  /// cached.
  mutable APValue APVal;

  void anchor() override;

  MSGuidDecl(DeclContext *DC, QualType T, Parts P);

  static MSGuidDecl *Create(const ASTContext &C, QualType T, Parts P);
  static MSGuidDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  // Only ASTContext::getMSGuidDecl and deserialization create these.
  friend class ASTContext;
  friend class ASTReader;
  friend class ASTDeclReader;

public:
  /// Print this UUID in a human-readable format.
  void printName(llvm::raw_ostream &OS,
                 const PrintingPolicy &Policy) const override;

  /// Get the decomposed parts of this declaration.
  Parts getParts() const { return PartVal; }

  /// Get the value of this MSGuidDecl as an APValue. This may fail and return
  /// an absent APValue if the type of the declaration is not of the expected
  /// shape.
  APValue &getAsAPValue() const;

  static void Profile(llvm::FoldingSetNodeID &ID, Parts P) {
    ID.AddInteger(P.Part1);
    ID.AddInteger(P.Part2);
    ID.AddInteger(P.Part3);
    ID.AddInteger(P.getPart4And5AsUint64());
  }
  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, PartVal); }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decl::MSGuid; }
};

/// An artificial decl, representing a global anonymous constant value which is
/// uniquified by value within a translation unit.
///
/// These is currently only used to back the LValue returned by
/// __builtin_source_location, but could potentially be used for other similar
/// situations in the future.
class UnnamedGlobalConstantDecl : public ValueDecl,
                                  public Mergeable<UnnamedGlobalConstantDecl>,
                                  public llvm::FoldingSetNode {

  // The constant value of this global.
  APValue Value;

  void anchor() override;

  UnnamedGlobalConstantDecl(const ASTContext &C, DeclContext *DC, QualType T,
                            const APValue &Val);

  static UnnamedGlobalConstantDecl *Create(const ASTContext &C, QualType T,
                                           const APValue &APVal);
  static UnnamedGlobalConstantDecl *CreateDeserialized(ASTContext &C,
                                                       GlobalDeclID ID);

  // Only ASTContext::getUnnamedGlobalConstantDecl and deserialization create
  // these.
  friend class ASTContext;
  friend class ASTReader;
  friend class ASTDeclReader;

public:
  /// Print this in a human-readable format.
  void printName(llvm::raw_ostream &OS,
                 const PrintingPolicy &Policy) const override;

  const APValue &getValue() const { return Value; }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Ty,
                      const APValue &APVal) {
    Ty.Profile(ID);
    APVal.Profile(ID);
  }
  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getType(), getValue());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Decl::UnnamedGlobalConstant; }
};

/// Insertion operator for diagnostics.  This allows sending an AccessSpecifier
/// into a diagnostic with <<.
const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                      AccessSpecifier AS);

} // namespace clang

#endif // LLVM_CLANG_AST_DECLCXX_H
