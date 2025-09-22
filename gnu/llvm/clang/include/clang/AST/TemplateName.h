//===- TemplateName.h - C++ Template Name Representation --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TemplateName interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TEMPLATENAME_H
#define LLVM_CLANG_AST_TEMPLATENAME_H

#include "clang/AST/DependenceFlags.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>
#include <optional>

namespace clang {

class ASTContext;
class Decl;
class DependentTemplateName;
class IdentifierInfo;
class NamedDecl;
class NestedNameSpecifier;
enum OverloadedOperatorKind : int;
class OverloadedTemplateStorage;
class AssumedTemplateStorage;
struct PrintingPolicy;
class QualifiedTemplateName;
class SubstTemplateTemplateParmPackStorage;
class SubstTemplateTemplateParmStorage;
class TemplateArgument;
class TemplateDecl;
class TemplateTemplateParmDecl;
class UsingShadowDecl;

/// Implementation class used to describe either a set of overloaded
/// template names or an already-substituted template template parameter pack.
class UncommonTemplateNameStorage {
protected:
  enum Kind {
    Overloaded,
    Assumed, // defined in DeclarationName.h
    SubstTemplateTemplateParm,
    SubstTemplateTemplateParmPack
  };

  struct BitsTag {
    LLVM_PREFERRED_TYPE(Kind)
    unsigned Kind : 2;

    // The template parameter index.
    unsigned Index : 15;

    /// The pack index, or the number of stored templates
    /// or template arguments, depending on which subclass we have.
    unsigned Data : 15;
  };

  union {
    struct BitsTag Bits;
    void *PointerAlignment;
  };

  UncommonTemplateNameStorage(Kind Kind, unsigned Index, unsigned Data) {
    Bits.Kind = Kind;
    Bits.Index = Index;
    Bits.Data = Data;
  }

public:
  OverloadedTemplateStorage *getAsOverloadedStorage()  {
    return Bits.Kind == Overloaded
             ? reinterpret_cast<OverloadedTemplateStorage *>(this)
             : nullptr;
  }

  AssumedTemplateStorage *getAsAssumedTemplateName()  {
    return Bits.Kind == Assumed
             ? reinterpret_cast<AssumedTemplateStorage *>(this)
             : nullptr;
  }

  SubstTemplateTemplateParmStorage *getAsSubstTemplateTemplateParm() {
    return Bits.Kind == SubstTemplateTemplateParm
             ? reinterpret_cast<SubstTemplateTemplateParmStorage *>(this)
             : nullptr;
  }

  SubstTemplateTemplateParmPackStorage *getAsSubstTemplateTemplateParmPack() {
    return Bits.Kind == SubstTemplateTemplateParmPack
             ? reinterpret_cast<SubstTemplateTemplateParmPackStorage *>(this)
             : nullptr;
  }
};

/// A structure for storing the information associated with an
/// overloaded template name.
class OverloadedTemplateStorage : public UncommonTemplateNameStorage {
  friend class ASTContext;

  OverloadedTemplateStorage(unsigned size)
      : UncommonTemplateNameStorage(Overloaded, 0, size) {}

  NamedDecl **getStorage() {
    return reinterpret_cast<NamedDecl **>(this + 1);
  }
  NamedDecl * const *getStorage() const {
    return reinterpret_cast<NamedDecl *const *>(this + 1);
  }

public:
  unsigned size() const { return Bits.Data; }

  using iterator = NamedDecl *const *;

  iterator begin() const { return getStorage(); }
  iterator end() const { return getStorage() + Bits.Data; }

  llvm::ArrayRef<NamedDecl*> decls() const {
    return llvm::ArrayRef(begin(), end());
  }
};

/// A structure for storing an already-substituted template template
/// parameter pack.
///
/// This kind of template names occurs when the parameter pack has been
/// provided with a template template argument pack in a context where its
/// enclosing pack expansion could not be fully expanded.
class SubstTemplateTemplateParmPackStorage : public UncommonTemplateNameStorage,
                                             public llvm::FoldingSetNode {
  const TemplateArgument *Arguments;
  llvm::PointerIntPair<Decl *, 1, bool> AssociatedDeclAndFinal;

public:
  SubstTemplateTemplateParmPackStorage(ArrayRef<TemplateArgument> ArgPack,
                                       Decl *AssociatedDecl, unsigned Index,
                                       bool Final);

  /// A template-like entity which owns the whole pattern being substituted.
  /// This will own a set of template parameters.
  Decl *getAssociatedDecl() const;

  /// Returns the index of the replaced parameter in the associated declaration.
  /// This should match the result of `getParameterPack()->getIndex()`.
  unsigned getIndex() const { return Bits.Index; }

  // When true the substitution will be 'Final' (subst node won't be placed).
  bool getFinal() const;

  /// Retrieve the template template parameter pack being substituted.
  TemplateTemplateParmDecl *getParameterPack() const;

  /// Retrieve the template template argument pack with which this
  /// parameter was substituted.
  TemplateArgument getArgumentPack() const;

  void Profile(llvm::FoldingSetNodeID &ID, ASTContext &Context);

  static void Profile(llvm::FoldingSetNodeID &ID, ASTContext &Context,
                      const TemplateArgument &ArgPack, Decl *AssociatedDecl,
                      unsigned Index, bool Final);
};

/// Represents a C++ template name within the type system.
///
/// A C++ template name refers to a template within the C++ type
/// system. In most cases, a template name is simply a reference to a
/// class template, e.g.
///
/// \code
/// template<typename T> class X { };
///
/// X<int> xi;
/// \endcode
///
/// Here, the 'X' in \c X<int> is a template name that refers to the
/// declaration of the class template X, above. Template names can
/// also refer to function templates, C++0x template aliases, etc.
///
/// Some template names are dependent. For example, consider:
///
/// \code
/// template<typename MetaFun, typename T1, typename T2> struct apply2 {
///   typedef typename MetaFun::template apply<T1, T2>::type type;
/// };
/// \endcode
///
/// Here, "apply" is treated as a template name within the typename
/// specifier in the typedef. "apply" is a nested template, and can
/// only be understood in the context of a template instantiation,
/// hence is represented as a dependent template name.
class TemplateName {
  // NameDecl is either a TemplateDecl or a UsingShadowDecl depending on the
  // NameKind.
  // !! There is no free low bits in 32-bit builds to discriminate more than 4
  // pointer types in PointerUnion.
  using StorageType =
      llvm::PointerUnion<Decl *, UncommonTemplateNameStorage *,
                         QualifiedTemplateName *, DependentTemplateName *>;

  StorageType Storage;

  explicit TemplateName(void *Ptr);

public:
  // Kind of name that is actually stored.
  enum NameKind {
    /// A single template declaration.
    Template,

    /// A set of overloaded template declarations.
    OverloadedTemplate,

    /// An unqualified-id that has been assumed to name a function template
    /// that will be found by ADL.
    AssumedTemplate,

    /// A qualified template name, where the qualification is kept
    /// to describe the source code as written.
    QualifiedTemplate,

    /// A dependent template name that has not been resolved to a
    /// template (or set of templates).
    DependentTemplate,

    /// A template template parameter that has been substituted
    /// for some other template name.
    SubstTemplateTemplateParm,

    /// A template template parameter pack that has been substituted for
    /// a template template argument pack, but has not yet been expanded into
    /// individual arguments.
    SubstTemplateTemplateParmPack,

    /// A template name that refers to a template declaration found through a
    /// specific using shadow declaration.
    UsingTemplate,
  };

  TemplateName() = default;
  explicit TemplateName(TemplateDecl *Template);
  explicit TemplateName(OverloadedTemplateStorage *Storage);
  explicit TemplateName(AssumedTemplateStorage *Storage);
  explicit TemplateName(SubstTemplateTemplateParmStorage *Storage);
  explicit TemplateName(SubstTemplateTemplateParmPackStorage *Storage);
  explicit TemplateName(QualifiedTemplateName *Qual);
  explicit TemplateName(DependentTemplateName *Dep);
  explicit TemplateName(UsingShadowDecl *Using);

  /// Determine whether this template name is NULL.
  bool isNull() const;

  // Get the kind of name that is actually stored.
  NameKind getKind() const;

  /// Retrieve the underlying template declaration that
  /// this template name refers to, if known.
  ///
  /// \returns The template declaration that this template name refers
  /// to, if any. If the template name does not refer to a specific
  /// declaration because it is a dependent name, or if it refers to a
  /// set of function templates, returns NULL.
  TemplateDecl *getAsTemplateDecl() const;

  /// Retrieve the underlying, overloaded function template
  /// declarations that this template name refers to, if known.
  ///
  /// \returns The set of overloaded function templates that this template
  /// name refers to, if known. If the template name does not refer to a
  /// specific set of function templates because it is a dependent name or
  /// refers to a single template, returns NULL.
  OverloadedTemplateStorage *getAsOverloadedTemplate() const;

  /// Retrieve information on a name that has been assumed to be a
  /// template-name in order to permit a call via ADL.
  AssumedTemplateStorage *getAsAssumedTemplateName() const;

  /// Retrieve the substituted template template parameter, if
  /// known.
  ///
  /// \returns The storage for the substituted template template parameter,
  /// if known. Otherwise, returns NULL.
  SubstTemplateTemplateParmStorage *getAsSubstTemplateTemplateParm() const;

  /// Retrieve the substituted template template parameter pack, if
  /// known.
  ///
  /// \returns The storage for the substituted template template parameter pack,
  /// if known. Otherwise, returns NULL.
  SubstTemplateTemplateParmPackStorage *
  getAsSubstTemplateTemplateParmPack() const;

  /// Retrieve the underlying qualified template name
  /// structure, if any.
  QualifiedTemplateName *getAsQualifiedTemplateName() const;

  /// Retrieve the underlying dependent template name
  /// structure, if any.
  DependentTemplateName *getAsDependentTemplateName() const;

  /// Retrieve the using shadow declaration through which the underlying
  /// template declaration is introduced, if any.
  UsingShadowDecl *getAsUsingShadowDecl() const;

  TemplateName getUnderlying() const;

  TemplateNameDependence getDependence() const;

  /// Determines whether this is a dependent template name.
  bool isDependent() const;

  /// Determines whether this is a template name that somehow
  /// depends on a template parameter.
  bool isInstantiationDependent() const;

  /// Determines whether this template name contains an
  /// unexpanded parameter pack (for C++0x variadic templates).
  bool containsUnexpandedParameterPack() const;

  enum class Qualified { None, AsWritten };
  /// Print the template name.
  ///
  /// \param OS the output stream to which the template name will be
  /// printed.
  ///
  /// \param Qual print the (Qualified::None) simple name,
  /// (Qualified::AsWritten) any written (possibly partial) qualifier, or
  /// (Qualified::Fully) the fully qualified name.
  void print(raw_ostream &OS, const PrintingPolicy &Policy,
             Qualified Qual = Qualified::AsWritten) const;

  /// Debugging aid that dumps the template name.
  void dump(raw_ostream &OS, const ASTContext &Context) const;

  /// Debugging aid that dumps the template name to standard
  /// error.
  void dump() const;

  void Profile(llvm::FoldingSetNodeID &ID);

  /// Retrieve the template name as a void pointer.
  void *getAsVoidPointer() const { return Storage.getOpaqueValue(); }

  /// Build a template name from a void pointer.
  static TemplateName getFromVoidPointer(void *Ptr) {
    return TemplateName(Ptr);
  }

  /// Structural equality.
  bool operator==(TemplateName Other) const { return Storage == Other.Storage; }
  bool operator!=(TemplateName Other) const { return !operator==(Other); }
};

/// Insertion operator for diagnostics.  This allows sending TemplateName's
/// into a diagnostic with <<.
const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                      TemplateName N);

/// A structure for storing the information associated with a
/// substituted template template parameter.
class SubstTemplateTemplateParmStorage
  : public UncommonTemplateNameStorage, public llvm::FoldingSetNode {
  friend class ASTContext;

  TemplateName Replacement;
  Decl *AssociatedDecl;

  SubstTemplateTemplateParmStorage(TemplateName Replacement,
                                   Decl *AssociatedDecl, unsigned Index,
                                   std::optional<unsigned> PackIndex)
      : UncommonTemplateNameStorage(SubstTemplateTemplateParm, Index,
                                    PackIndex ? *PackIndex + 1 : 0),
        Replacement(Replacement), AssociatedDecl(AssociatedDecl) {
    assert(AssociatedDecl != nullptr);
  }

public:
  /// A template-like entity which owns the whole pattern being substituted.
  /// This will own a set of template parameters.
  Decl *getAssociatedDecl() const { return AssociatedDecl; }

  /// Returns the index of the replaced parameter in the associated declaration.
  /// This should match the result of `getParameter()->getIndex()`.
  unsigned getIndex() const { return Bits.Index; }

  std::optional<unsigned> getPackIndex() const {
    if (Bits.Data == 0)
      return std::nullopt;
    return Bits.Data - 1;
  }

  TemplateTemplateParmDecl *getParameter() const;
  TemplateName getReplacement() const { return Replacement; }

  void Profile(llvm::FoldingSetNodeID &ID);

  static void Profile(llvm::FoldingSetNodeID &ID, TemplateName Replacement,
                      Decl *AssociatedDecl, unsigned Index,
                      std::optional<unsigned> PackIndex);
};

inline TemplateName TemplateName::getUnderlying() const {
  if (SubstTemplateTemplateParmStorage *subst
        = getAsSubstTemplateTemplateParm())
    return subst->getReplacement().getUnderlying();
  return *this;
}

/// Represents a template name as written in source code.
///
/// This kind of template name may refer to a template name that was
/// preceded by a nested name specifier, e.g., \c std::vector. Here,
/// the nested name specifier is "std::" and the template name is the
/// declaration for "vector". It may also have been written with the
/// 'template' keyword. The QualifiedTemplateName class is only
/// used to provide "sugar" for template names, so that they can
/// be differentiated from canonical template names. and has no
/// semantic meaning. In this manner, it is to TemplateName what
/// ElaboratedType is to Type, providing extra syntactic sugar
/// for downstream clients.
class QualifiedTemplateName : public llvm::FoldingSetNode {
  friend class ASTContext;

  /// The nested name specifier that qualifies the template name.
  ///
  /// The bit is used to indicate whether the "template" keyword was
  /// present before the template name itself. Note that the
  /// "template" keyword is always redundant in this case (otherwise,
  /// the template name would be a dependent name and we would express
  /// this name with DependentTemplateName).
  llvm::PointerIntPair<NestedNameSpecifier *, 1> Qualifier;

  /// The underlying template name, it is either
  ///  1) a Template -- a template declaration that this qualified name refers
  ///     to.
  ///  2) or a UsingTemplate -- a template declaration introduced by a
  ///     using-shadow declaration.
  TemplateName UnderlyingTemplate;

  QualifiedTemplateName(NestedNameSpecifier *NNS, bool TemplateKeyword,
                        TemplateName Template)
      : Qualifier(NNS, TemplateKeyword ? 1 : 0), UnderlyingTemplate(Template) {
    assert(UnderlyingTemplate.getKind() == TemplateName::Template ||
           UnderlyingTemplate.getKind() == TemplateName::UsingTemplate);
  }

public:
  /// Return the nested name specifier that qualifies this name.
  NestedNameSpecifier *getQualifier() const { return Qualifier.getPointer(); }

  /// Whether the template name was prefixed by the "template"
  /// keyword.
  bool hasTemplateKeyword() const { return Qualifier.getInt(); }

  /// Return the underlying template name.
  TemplateName getUnderlyingTemplate() const { return UnderlyingTemplate; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getQualifier(), hasTemplateKeyword(), UnderlyingTemplate);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, NestedNameSpecifier *NNS,
                      bool TemplateKeyword, TemplateName TN) {
    ID.AddPointer(NNS);
    ID.AddBoolean(TemplateKeyword);
    ID.AddPointer(TN.getAsVoidPointer());
  }
};

/// Represents a dependent template name that cannot be
/// resolved prior to template instantiation.
///
/// This kind of template name refers to a dependent template name,
/// including its nested name specifier (if any). For example,
/// DependentTemplateName can refer to "MetaFun::template apply",
/// where "MetaFun::" is the nested name specifier and "apply" is the
/// template name referenced. The "template" keyword is implied.
class DependentTemplateName : public llvm::FoldingSetNode {
  friend class ASTContext;

  /// The nested name specifier that qualifies the template
  /// name.
  ///
  /// The bit stored in this qualifier describes whether the \c Name field
  /// is interpreted as an IdentifierInfo pointer (when clear) or as an
  /// overloaded operator kind (when set).
  llvm::PointerIntPair<NestedNameSpecifier *, 1, bool> Qualifier;

  /// The dependent template name.
  union {
    /// The identifier template name.
    ///
    /// Only valid when the bit on \c Qualifier is clear.
    const IdentifierInfo *Identifier;

    /// The overloaded operator name.
    ///
    /// Only valid when the bit on \c Qualifier is set.
    OverloadedOperatorKind Operator;
  };

  /// The canonical template name to which this dependent
  /// template name refers.
  ///
  /// The canonical template name for a dependent template name is
  /// another dependent template name whose nested name specifier is
  /// canonical.
  TemplateName CanonicalTemplateName;

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        const IdentifierInfo *Identifier)
      : Qualifier(Qualifier, false), Identifier(Identifier),
        CanonicalTemplateName(this) {}

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        const IdentifierInfo *Identifier,
                        TemplateName Canon)
      : Qualifier(Qualifier, false), Identifier(Identifier),
        CanonicalTemplateName(Canon) {}

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        OverloadedOperatorKind Operator)
      : Qualifier(Qualifier, true), Operator(Operator),
        CanonicalTemplateName(this) {}

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        OverloadedOperatorKind Operator,
                        TemplateName Canon)
       : Qualifier(Qualifier, true), Operator(Operator),
         CanonicalTemplateName(Canon) {}

public:
  /// Return the nested name specifier that qualifies this name.
  NestedNameSpecifier *getQualifier() const { return Qualifier.getPointer(); }

  /// Determine whether this template name refers to an identifier.
  bool isIdentifier() const { return !Qualifier.getInt(); }

  /// Returns the identifier to which this template name refers.
  const IdentifierInfo *getIdentifier() const {
    assert(isIdentifier() && "Template name isn't an identifier?");
    return Identifier;
  }

  /// Determine whether this template name refers to an overloaded
  /// operator.
  bool isOverloadedOperator() const { return Qualifier.getInt(); }

  /// Return the overloaded operator to which this template name refers.
  OverloadedOperatorKind getOperator() const {
    assert(isOverloadedOperator() &&
           "Template name isn't an overloaded operator?");
    return Operator;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    if (isIdentifier())
      Profile(ID, getQualifier(), getIdentifier());
    else
      Profile(ID, getQualifier(), getOperator());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, NestedNameSpecifier *NNS,
                      const IdentifierInfo *Identifier) {
    ID.AddPointer(NNS);
    ID.AddBoolean(false);
    ID.AddPointer(Identifier);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, NestedNameSpecifier *NNS,
                      OverloadedOperatorKind Operator) {
    ID.AddPointer(NNS);
    ID.AddBoolean(true);
    ID.AddInteger(Operator);
  }
};

} // namespace clang.

namespace llvm {

/// The clang::TemplateName class is effectively a pointer.
template<>
struct PointerLikeTypeTraits<clang::TemplateName> {
  static inline void *getAsVoidPointer(clang::TemplateName TN) {
    return TN.getAsVoidPointer();
  }

  static inline clang::TemplateName getFromVoidPointer(void *Ptr) {
    return clang::TemplateName::getFromVoidPointer(Ptr);
  }

  // No bits are available!
  static constexpr int NumLowBitsAvailable = 0;
};

} // namespace llvm.

#endif // LLVM_CLANG_AST_TEMPLATENAME_H
