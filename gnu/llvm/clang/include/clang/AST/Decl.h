//===- Decl.h - Classes for representing declarations -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Decl subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECL_H
#define LLVM_CLANG_AST_DECL_H

#include "clang/AST/APNumericStorage.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContextAllocate.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Redeclarable.h"
#include "clang/AST/Type.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/PragmaKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/Visibility.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace clang {

class ASTContext;
struct ASTTemplateArgumentListInfo;
class CompoundStmt;
class DependentFunctionTemplateSpecializationInfo;
class EnumDecl;
class Expr;
class FunctionTemplateDecl;
class FunctionTemplateSpecializationInfo;
class FunctionTypeLoc;
class LabelStmt;
class MemberSpecializationInfo;
class Module;
class NamespaceDecl;
class ParmVarDecl;
class RecordDecl;
class Stmt;
class StringLiteral;
class TagDecl;
class TemplateArgumentList;
class TemplateArgumentListInfo;
class TemplateParameterList;
class TypeAliasTemplateDecl;
class UnresolvedSetImpl;
class VarTemplateDecl;
enum class ImplicitParamKind;

/// The top declaration context.
class TranslationUnitDecl : public Decl,
                            public DeclContext,
                            public Redeclarable<TranslationUnitDecl> {
  using redeclarable_base = Redeclarable<TranslationUnitDecl>;

  TranslationUnitDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  TranslationUnitDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  TranslationUnitDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

  ASTContext &Ctx;

  /// The (most recently entered) anonymous namespace for this
  /// translation unit, if one has been created.
  NamespaceDecl *AnonymousNamespace = nullptr;

  explicit TranslationUnitDecl(ASTContext &ctx);

  virtual void anchor();

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::isFirstDecl;
  using redeclarable_base::redecls;
  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;

  ASTContext &getASTContext() const { return Ctx; }

  NamespaceDecl *getAnonymousNamespace() const { return AnonymousNamespace; }
  void setAnonymousNamespace(NamespaceDecl *D);

  static TranslationUnitDecl *Create(ASTContext &C);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TranslationUnit; }
  static DeclContext *castToDeclContext(const TranslationUnitDecl *D) {
    return static_cast<DeclContext *>(const_cast<TranslationUnitDecl*>(D));
  }
  static TranslationUnitDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<TranslationUnitDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents a `#pragma comment` line. Always a child of
/// TranslationUnitDecl.
class PragmaCommentDecl final
    : public Decl,
      private llvm::TrailingObjects<PragmaCommentDecl, char> {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  PragmaMSCommentKind CommentKind;

  PragmaCommentDecl(TranslationUnitDecl *TU, SourceLocation CommentLoc,
                    PragmaMSCommentKind CommentKind)
      : Decl(PragmaComment, TU, CommentLoc), CommentKind(CommentKind) {}

  virtual void anchor();

public:
  static PragmaCommentDecl *Create(const ASTContext &C, TranslationUnitDecl *DC,
                                   SourceLocation CommentLoc,
                                   PragmaMSCommentKind CommentKind,
                                   StringRef Arg);
  static PragmaCommentDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                               unsigned ArgSize);

  PragmaMSCommentKind getCommentKind() const { return CommentKind; }

  StringRef getArg() const { return getTrailingObjects<char>(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == PragmaComment; }
};

/// Represents a `#pragma detect_mismatch` line. Always a child of
/// TranslationUnitDecl.
class PragmaDetectMismatchDecl final
    : public Decl,
      private llvm::TrailingObjects<PragmaDetectMismatchDecl, char> {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  size_t ValueStart;

  PragmaDetectMismatchDecl(TranslationUnitDecl *TU, SourceLocation Loc,
                           size_t ValueStart)
      : Decl(PragmaDetectMismatch, TU, Loc), ValueStart(ValueStart) {}

  virtual void anchor();

public:
  static PragmaDetectMismatchDecl *Create(const ASTContext &C,
                                          TranslationUnitDecl *DC,
                                          SourceLocation Loc, StringRef Name,
                                          StringRef Value);
  static PragmaDetectMismatchDecl *
  CreateDeserialized(ASTContext &C, GlobalDeclID ID, unsigned NameValueSize);

  StringRef getName() const { return getTrailingObjects<char>(); }
  StringRef getValue() const { return getTrailingObjects<char>() + ValueStart; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == PragmaDetectMismatch; }
};

/// Declaration context for names declared as extern "C" in C++. This
/// is neither the semantic nor lexical context for such declarations, but is
/// used to check for conflicts with other extern "C" declarations. Example:
///
/// \code
///   namespace N { extern "C" void f(); } // #1
///   void N::f() {}                       // #2
///   namespace M { extern "C" void f(); } // #3
/// \endcode
///
/// The semantic context of #1 is namespace N and its lexical context is the
/// LinkageSpecDecl; the semantic context of #2 is namespace N and its lexical
/// context is the TU. However, both declarations are also visible in the
/// extern "C" context.
///
/// The declaration at #3 finds it is a redeclaration of \c N::f through
/// lookup in the extern "C" context.
class ExternCContextDecl : public Decl, public DeclContext {
  explicit ExternCContextDecl(TranslationUnitDecl *TU)
    : Decl(ExternCContext, TU, SourceLocation()),
      DeclContext(ExternCContext) {}

  virtual void anchor();

public:
  static ExternCContextDecl *Create(const ASTContext &C,
                                    TranslationUnitDecl *TU);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ExternCContext; }
  static DeclContext *castToDeclContext(const ExternCContextDecl *D) {
    return static_cast<DeclContext *>(const_cast<ExternCContextDecl*>(D));
  }
  static ExternCContextDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<ExternCContextDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// This represents a decl that may have a name.  Many decls have names such
/// as ObjCMethodDecl, but not \@class, etc.
///
/// Note that not every NamedDecl is actually named (e.g., a struct might
/// be anonymous), and not every name is an identifier.
class NamedDecl : public Decl {
  /// The name of this declaration, which is typically a normal
  /// identifier but may also be a special kind of name (C++
  /// constructor, Objective-C selector, etc.)
  DeclarationName Name;

  virtual void anchor();

private:
  NamedDecl *getUnderlyingDeclImpl() LLVM_READONLY;

protected:
  NamedDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName N)
      : Decl(DK, DC, L), Name(N) {}

public:
  /// Get the identifier that names this declaration, if there is one.
  ///
  /// This will return NULL if this declaration has no name (e.g., for
  /// an unnamed class) or if the name is a special name (C++ constructor,
  /// Objective-C selector, etc.).
  IdentifierInfo *getIdentifier() const { return Name.getAsIdentifierInfo(); }

  /// Get the name of identifier for this declaration as a StringRef.
  ///
  /// This requires that the declaration have a name and that it be a simple
  /// identifier.
  StringRef getName() const {
    assert(Name.isIdentifier() && "Name is not a simple identifier");
    return getIdentifier() ? getIdentifier()->getName() : "";
  }

  /// Get a human-readable name for the declaration, even if it is one of the
  /// special kinds of names (C++ constructor, Objective-C selector, etc).
  ///
  /// Creating this name requires expensive string manipulation, so it should
  /// be called only when performance doesn't matter. For simple declarations,
  /// getNameAsCString() should suffice.
  //
  // FIXME: This function should be renamed to indicate that it is not just an
  // alternate form of getName(), and clients should move as appropriate.
  //
  // FIXME: Deprecated, move clients to getName().
  std::string getNameAsString() const { return Name.getAsString(); }

  /// Pretty-print the unqualified name of this declaration. Can be overloaded
  /// by derived classes to provide a more user-friendly name when appropriate.
  virtual void printName(raw_ostream &OS, const PrintingPolicy &Policy) const;
  /// Calls printName() with the ASTContext printing policy from the decl.
  void printName(raw_ostream &OS) const;

  /// Get the actual, stored name of the declaration, which may be a special
  /// name.
  ///
  /// Note that generally in diagnostics, the non-null \p NamedDecl* itself
  /// should be sent into the diagnostic instead of using the result of
  /// \p getDeclName().
  ///
  /// A \p DeclarationName in a diagnostic will just be streamed to the output,
  /// which will directly result in a call to \p DeclarationName::print.
  ///
  /// A \p NamedDecl* in a diagnostic will also ultimately result in a call to
  /// \p DeclarationName::print, but with two customisation points along the
  /// way (\p getNameForDiagnostic and \p printName). These are used to print
  /// the template arguments if any, and to provide a user-friendly name for
  /// some entities (such as unnamed variables and anonymous records).
  DeclarationName getDeclName() const { return Name; }

  /// Set the name of this declaration.
  void setDeclName(DeclarationName N) { Name = N; }

  /// Returns a human-readable qualified name for this declaration, like
  /// A::B::i, for i being member of namespace A::B.
  ///
  /// If the declaration is not a member of context which can be named (record,
  /// namespace), it will return the same result as printName().
  ///
  /// Creating this name is expensive, so it should be called only when
  /// performance doesn't matter.
  void printQualifiedName(raw_ostream &OS) const;
  void printQualifiedName(raw_ostream &OS, const PrintingPolicy &Policy) const;

  /// Print only the nested name specifier part of a fully-qualified name,
  /// including the '::' at the end. E.g.
  ///    when `printQualifiedName(D)` prints "A::B::i",
  ///    this function prints "A::B::".
  void printNestedNameSpecifier(raw_ostream &OS) const;
  void printNestedNameSpecifier(raw_ostream &OS,
                                const PrintingPolicy &Policy) const;

  // FIXME: Remove string version.
  std::string getQualifiedNameAsString() const;

  /// Appends a human-readable name for this declaration into the given stream.
  ///
  /// This is the method invoked by Sema when displaying a NamedDecl
  /// in a diagnostic.  It does not necessarily produce the same
  /// result as printName(); for example, class template
  /// specializations are printed with their template arguments.
  virtual void getNameForDiagnostic(raw_ostream &OS,
                                    const PrintingPolicy &Policy,
                                    bool Qualified) const;

  /// Determine whether this declaration, if known to be well-formed within
  /// its context, will replace the declaration OldD if introduced into scope.
  ///
  /// A declaration will replace another declaration if, for example, it is
  /// a redeclaration of the same variable or function, but not if it is a
  /// declaration of a different kind (function vs. class) or an overloaded
  /// function.
  ///
  /// \param IsKnownNewer \c true if this declaration is known to be newer
  /// than \p OldD (for instance, if this declaration is newly-created).
  bool declarationReplaces(const NamedDecl *OldD,
                           bool IsKnownNewer = true) const;

  /// Determine whether this declaration has linkage.
  bool hasLinkage() const;

  using Decl::isModulePrivate;
  using Decl::setModulePrivate;

  /// Determine whether this declaration is a C++ class member.
  bool isCXXClassMember() const {
    const DeclContext *DC = getDeclContext();

    // C++0x [class.mem]p1:
    //   The enumerators of an unscoped enumeration defined in
    //   the class are members of the class.
    if (isa<EnumDecl>(DC))
      DC = DC->getRedeclContext();

    return DC->isRecord();
  }

  /// Determine whether the given declaration is an instance member of
  /// a C++ class.
  bool isCXXInstanceMember() const;

  /// Determine if the declaration obeys the reserved identifier rules of the
  /// given language.
  ReservedIdentifierStatus isReserved(const LangOptions &LangOpts) const;

  /// Determine what kind of linkage this entity has.
  ///
  /// This is not the linkage as defined by the standard or the codegen notion
  /// of linkage. It is just an implementation detail that is used to compute
  /// those.
  Linkage getLinkageInternal() const;

  /// Get the linkage from a semantic point of view. Entities in
  /// anonymous namespaces are external (in c++98).
  Linkage getFormalLinkage() const;

  /// True if this decl has external linkage.
  bool hasExternalFormalLinkage() const {
    return isExternalFormalLinkage(getLinkageInternal());
  }

  bool isExternallyVisible() const {
    return clang::isExternallyVisible(getLinkageInternal());
  }

  /// Determine whether this declaration can be redeclared in a
  /// different translation unit.
  bool isExternallyDeclarable() const {
    return isExternallyVisible() && !getOwningModuleForLinkage();
  }

  /// Determines the visibility of this entity.
  Visibility getVisibility() const {
    return getLinkageAndVisibility().getVisibility();
  }

  /// Determines the linkage and visibility of this entity.
  LinkageInfo getLinkageAndVisibility() const;

  /// Kinds of explicit visibility.
  enum ExplicitVisibilityKind {
    /// Do an LV computation for, ultimately, a type.
    /// Visibility may be restricted by type visibility settings and
    /// the visibility of template arguments.
    VisibilityForType,

    /// Do an LV computation for, ultimately, a non-type declaration.
    /// Visibility may be restricted by value visibility settings and
    /// the visibility of template arguments.
    VisibilityForValue
  };

  /// If visibility was explicitly specified for this
  /// declaration, return that visibility.
  std::optional<Visibility>
  getExplicitVisibility(ExplicitVisibilityKind kind) const;

  /// True if the computed linkage is valid. Used for consistency
  /// checking. Should always return true.
  bool isLinkageValid() const;

  /// True if something has required us to compute the linkage
  /// of this declaration.
  ///
  /// Language features which can retroactively change linkage (like a
  /// typedef name for linkage purposes) may need to consider this,
  /// but hopefully only in transitory ways during parsing.
  bool hasLinkageBeenComputed() const {
    return hasCachedLinkage();
  }

  bool isPlaceholderVar(const LangOptions &LangOpts) const;

  /// Looks through UsingDecls and ObjCCompatibleAliasDecls for
  /// the underlying named decl.
  NamedDecl *getUnderlyingDecl() {
    // Fast-path the common case.
    if (this->getKind() != UsingShadow &&
        this->getKind() != ConstructorUsingShadow &&
        this->getKind() != ObjCCompatibleAlias &&
        this->getKind() != NamespaceAlias)
      return this;

    return getUnderlyingDeclImpl();
  }
  const NamedDecl *getUnderlyingDecl() const {
    return const_cast<NamedDecl*>(this)->getUnderlyingDecl();
  }

  NamedDecl *getMostRecentDecl() {
    return cast<NamedDecl>(static_cast<Decl *>(this)->getMostRecentDecl());
  }
  const NamedDecl *getMostRecentDecl() const {
    return const_cast<NamedDecl*>(this)->getMostRecentDecl();
  }

  ObjCStringFormatFamily getObjCFStringFormattingFamily() const;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstNamed && K <= lastNamed; }
};

inline raw_ostream &operator<<(raw_ostream &OS, const NamedDecl &ND) {
  ND.printName(OS);
  return OS;
}

/// Represents the declaration of a label.  Labels also have a
/// corresponding LabelStmt, which indicates the position that the label was
/// defined at.  For normal labels, the location of the decl is the same as the
/// location of the statement.  For GNU local labels (__label__), the decl
/// location is where the __label__ is.
class LabelDecl : public NamedDecl {
  LabelStmt *TheStmt;
  StringRef MSAsmName;
  bool MSAsmNameResolved = false;

  /// For normal labels, this is the same as the main declaration
  /// label, i.e., the location of the identifier; for GNU local labels,
  /// this is the location of the __label__ keyword.
  SourceLocation LocStart;

  LabelDecl(DeclContext *DC, SourceLocation IdentL, IdentifierInfo *II,
            LabelStmt *S, SourceLocation StartL)
      : NamedDecl(Label, DC, IdentL, II), TheStmt(S), LocStart(StartL) {}

  void anchor() override;

public:
  static LabelDecl *Create(ASTContext &C, DeclContext *DC,
                           SourceLocation IdentL, IdentifierInfo *II);
  static LabelDecl *Create(ASTContext &C, DeclContext *DC,
                           SourceLocation IdentL, IdentifierInfo *II,
                           SourceLocation GnuLabelL);
  static LabelDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  LabelStmt *getStmt() const { return TheStmt; }
  void setStmt(LabelStmt *T) { TheStmt = T; }

  bool isGnuLocal() const { return LocStart != getLocation(); }
  void setLocStart(SourceLocation L) { LocStart = L; }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(LocStart, getLocation());
  }

  bool isMSAsmLabel() const { return !MSAsmName.empty(); }
  bool isResolvedMSAsmLabel() const { return isMSAsmLabel() && MSAsmNameResolved; }
  void setMSAsmLabel(StringRef Name);
  StringRef getMSAsmLabel() const { return MSAsmName; }
  void setMSAsmLabelResolved() { MSAsmNameResolved = true; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Label; }
};

/// Represent a C++ namespace.
class NamespaceDecl : public NamedDecl,
                      public DeclContext,
                      public Redeclarable<NamespaceDecl> {
  /// The starting location of the source range, pointing
  /// to either the namespace or the inline keyword.
  SourceLocation LocStart;

  /// The ending location of the source range.
  SourceLocation RBraceLoc;

  /// The unnamed namespace that inhabits this namespace, if any.
  NamespaceDecl *AnonymousNamespace = nullptr;

  NamespaceDecl(ASTContext &C, DeclContext *DC, bool Inline,
                SourceLocation StartLoc, SourceLocation IdLoc,
                IdentifierInfo *Id, NamespaceDecl *PrevDecl, bool Nested);

  using redeclarable_base = Redeclarable<NamespaceDecl>;

  NamespaceDecl *getNextRedeclarationImpl() override;
  NamespaceDecl *getPreviousDeclImpl() override;
  NamespaceDecl *getMostRecentDeclImpl() override;

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static NamespaceDecl *Create(ASTContext &C, DeclContext *DC, bool Inline,
                               SourceLocation StartLoc, SourceLocation IdLoc,
                               IdentifierInfo *Id, NamespaceDecl *PrevDecl,
                               bool Nested);

  static NamespaceDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  /// Returns true if this is an anonymous namespace declaration.
  ///
  /// For example:
  /// \code
  ///   namespace {
  ///     ...
  ///   };
  /// \endcode
  /// q.v. C++ [namespace.unnamed]
  bool isAnonymousNamespace() const {
    return !getIdentifier();
  }

  /// Returns true if this is an inline namespace declaration.
  bool isInline() const { return NamespaceDeclBits.IsInline; }

  /// Set whether this is an inline namespace declaration.
  void setInline(bool Inline) { NamespaceDeclBits.IsInline = Inline; }

  /// Returns true if this is a nested namespace declaration.
  /// \code
  /// namespace outer::nested { }
  /// \endcode
  bool isNested() const { return NamespaceDeclBits.IsNested; }

  /// Set whether this is a nested namespace declaration.
  void setNested(bool Nested) { NamespaceDeclBits.IsNested = Nested; }

  /// Returns true if the inline qualifier for \c Name is redundant.
  bool isRedundantInlineQualifierFor(DeclarationName Name) const {
    if (!isInline())
      return false;
    auto X = lookup(Name);
    // We should not perform a lookup within a transparent context, so find a
    // non-transparent parent context.
    auto Y = getParent()->getNonTransparentContext()->lookup(Name);
    return std::distance(X.begin(), X.end()) ==
      std::distance(Y.begin(), Y.end());
  }

  /// Retrieve the anonymous namespace that inhabits this namespace, if any.
  NamespaceDecl *getAnonymousNamespace() const {
    return getFirstDecl()->AnonymousNamespace;
  }

  void setAnonymousNamespace(NamespaceDecl *D) {
    getFirstDecl()->AnonymousNamespace = D;
  }

  /// Retrieves the canonical declaration of this namespace.
  NamespaceDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const NamespaceDecl *getCanonicalDecl() const { return getFirstDecl(); }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(LocStart, RBraceLoc);
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LocStart; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setLocStart(SourceLocation L) { LocStart = L; }
  void setRBraceLoc(SourceLocation L) { RBraceLoc = L; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Namespace; }
  static DeclContext *castToDeclContext(const NamespaceDecl *D) {
    return static_cast<DeclContext *>(const_cast<NamespaceDecl*>(D));
  }
  static NamespaceDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<NamespaceDecl *>(const_cast<DeclContext*>(DC));
  }
};

class VarDecl;

/// Represent the declaration of a variable (in which case it is
/// an lvalue) a function (in which case it is a function designator) or
/// an enum constant.
class ValueDecl : public NamedDecl {
  QualType DeclType;

  void anchor() override;

protected:
  ValueDecl(Kind DK, DeclContext *DC, SourceLocation L,
            DeclarationName N, QualType T)
    : NamedDecl(DK, DC, L, N), DeclType(T) {}

public:
  QualType getType() const { return DeclType; }
  void setType(QualType newType) { DeclType = newType; }

  /// Determine whether this symbol is weakly-imported,
  ///        or declared with the weak or weak-ref attr.
  bool isWeak() const;

  /// Whether this variable is the implicit variable for a lambda init-capture.
  /// Only VarDecl can be init captures, but both VarDecl and BindingDecl
  /// can be captured.
  bool isInitCapture() const;

  // If this is a VarDecl, or a BindindDecl with an
  // associated decomposed VarDecl, return that VarDecl.
  VarDecl *getPotentiallyDecomposedVarDecl();
  const VarDecl *getPotentiallyDecomposedVarDecl() const {
    return const_cast<ValueDecl *>(this)->getPotentiallyDecomposedVarDecl();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstValue && K <= lastValue; }
};

/// A struct with extended info about a syntactic
/// name qualifier, to be used for the case of out-of-line declarations.
struct QualifierInfo {
  NestedNameSpecifierLoc QualifierLoc;

  /// The number of "outer" template parameter lists.
  /// The count includes all of the template parameter lists that were matched
  /// against the template-ids occurring into the NNS and possibly (in the
  /// case of an explicit specialization) a final "template <>".
  unsigned NumTemplParamLists = 0;

  /// A new-allocated array of size NumTemplParamLists,
  /// containing pointers to the "outer" template parameter lists.
  /// It includes all of the template parameter lists that were matched
  /// against the template-ids occurring into the NNS and possibly (in the
  /// case of an explicit specialization) a final "template <>".
  TemplateParameterList** TemplParamLists = nullptr;

  QualifierInfo() = default;
  QualifierInfo(const QualifierInfo &) = delete;
  QualifierInfo& operator=(const QualifierInfo &) = delete;

  /// Sets info about "outer" template parameter lists.
  void setTemplateParameterListsInfo(ASTContext &Context,
                                     ArrayRef<TemplateParameterList *> TPLists);
};

/// Represents a ValueDecl that came out of a declarator.
/// Contains type source information through TypeSourceInfo.
class DeclaratorDecl : public ValueDecl {
  // A struct representing a TInfo, a trailing requires-clause and a syntactic
  // qualifier, to be used for the (uncommon) case of out-of-line declarations
  // and constrained function decls.
  struct ExtInfo : public QualifierInfo {
    TypeSourceInfo *TInfo;
    Expr *TrailingRequiresClause = nullptr;
  };

  llvm::PointerUnion<TypeSourceInfo *, ExtInfo *> DeclInfo;

  /// The start of the source range for this declaration,
  /// ignoring outer template declarations.
  SourceLocation InnerLocStart;

  bool hasExtInfo() const { return DeclInfo.is<ExtInfo*>(); }
  ExtInfo *getExtInfo() { return DeclInfo.get<ExtInfo*>(); }
  const ExtInfo *getExtInfo() const { return DeclInfo.get<ExtInfo*>(); }

protected:
  DeclaratorDecl(Kind DK, DeclContext *DC, SourceLocation L,
                 DeclarationName N, QualType T, TypeSourceInfo *TInfo,
                 SourceLocation StartL)
      : ValueDecl(DK, DC, L, N, T), DeclInfo(TInfo), InnerLocStart(StartL) {}

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  TypeSourceInfo *getTypeSourceInfo() const {
    return hasExtInfo()
      ? getExtInfo()->TInfo
      : DeclInfo.get<TypeSourceInfo*>();
  }

  void setTypeSourceInfo(TypeSourceInfo *TI) {
    if (hasExtInfo())
      getExtInfo()->TInfo = TI;
    else
      DeclInfo = TI;
  }

  /// Return start of source range ignoring outer template declarations.
  SourceLocation getInnerLocStart() const { return InnerLocStart; }
  void setInnerLocStart(SourceLocation L) { InnerLocStart = L; }

  /// Return start of source range taking into account any outer template
  /// declarations.
  SourceLocation getOuterLocStart() const;

  SourceRange getSourceRange() const override LLVM_READONLY;

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getOuterLocStart();
  }

  /// Retrieve the nested-name-specifier that qualifies the name of this
  /// declaration, if it was present in the source.
  NestedNameSpecifier *getQualifier() const {
    return hasExtInfo() ? getExtInfo()->QualifierLoc.getNestedNameSpecifier()
                        : nullptr;
  }

  /// Retrieve the nested-name-specifier (with source-location
  /// information) that qualifies the name of this declaration, if it was
  /// present in the source.
  NestedNameSpecifierLoc getQualifierLoc() const {
    return hasExtInfo() ? getExtInfo()->QualifierLoc
                        : NestedNameSpecifierLoc();
  }

  void setQualifierInfo(NestedNameSpecifierLoc QualifierLoc);

  /// \brief Get the constraint-expression introduced by the trailing
  /// requires-clause in the function/member declaration, or null if no
  /// requires-clause was provided.
  Expr *getTrailingRequiresClause() {
    return hasExtInfo() ? getExtInfo()->TrailingRequiresClause
                        : nullptr;
  }

  const Expr *getTrailingRequiresClause() const {
    return hasExtInfo() ? getExtInfo()->TrailingRequiresClause
                        : nullptr;
  }

  void setTrailingRequiresClause(Expr *TrailingRequiresClause);

  unsigned getNumTemplateParameterLists() const {
    return hasExtInfo() ? getExtInfo()->NumTemplParamLists : 0;
  }

  TemplateParameterList *getTemplateParameterList(unsigned index) const {
    assert(index < getNumTemplateParameterLists());
    return getExtInfo()->TemplParamLists[index];
  }

  void setTemplateParameterListsInfo(ASTContext &Context,
                                     ArrayRef<TemplateParameterList *> TPLists);

  SourceLocation getTypeSpecStartLoc() const;
  SourceLocation getTypeSpecEndLoc() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstDeclarator && K <= lastDeclarator;
  }
};

/// Structure used to store a statement, the constant value to
/// which it was evaluated (if any), and whether or not the statement
/// is an integral constant expression (if known).
struct EvaluatedStmt {
  /// Whether this statement was already evaluated.
  bool WasEvaluated : 1;

  /// Whether this statement is being evaluated.
  bool IsEvaluating : 1;

  /// Whether this variable is known to have constant initialization. This is
  /// currently only computed in C++, for static / thread storage duration
  /// variables that might have constant initialization and for variables that
  /// are usable in constant expressions.
  bool HasConstantInitialization : 1;

  /// Whether this variable is known to have constant destruction. That is,
  /// whether running the destructor on the initial value is a side-effect
  /// (and doesn't inspect any state that might have changed during program
  /// execution). This is currently only computed if the destructor is
  /// non-trivial.
  bool HasConstantDestruction : 1;

  /// In C++98, whether the initializer is an ICE. This affects whether the
  /// variable is usable in constant expressions.
  bool HasICEInit : 1;
  bool CheckedForICEInit : 1;

  LazyDeclStmtPtr Value;
  APValue Evaluated;

  EvaluatedStmt()
      : WasEvaluated(false), IsEvaluating(false),
        HasConstantInitialization(false), HasConstantDestruction(false),
        HasICEInit(false), CheckedForICEInit(false) {}
};

/// Represents a variable declaration or definition.
class VarDecl : public DeclaratorDecl, public Redeclarable<VarDecl> {
public:
  /// Initialization styles.
  enum InitializationStyle {
    /// C-style initialization with assignment
    CInit,

    /// Call-style initialization (C++98)
    CallInit,

    /// Direct list-initialization (C++11)
    ListInit,

    /// Parenthesized list-initialization (C++20)
    ParenListInit
  };

  /// Kinds of thread-local storage.
  enum TLSKind {
    /// Not a TLS variable.
    TLS_None,

    /// TLS with a known-constant initializer.
    TLS_Static,

    /// TLS with a dynamic initializer.
    TLS_Dynamic
  };

  /// Return the string used to specify the storage class \p SC.
  ///
  /// It is illegal to call this function with SC == None.
  static const char *getStorageClassSpecifierString(StorageClass SC);

protected:
  // A pointer union of Stmt * and EvaluatedStmt *. When an EvaluatedStmt, we
  // have allocated the auxiliary struct of information there.
  //
  // TODO: It is a bit unfortunate to use a PointerUnion inside the VarDecl for
  // this as *many* VarDecls are ParmVarDecls that don't have default
  // arguments. We could save some space by moving this pointer union to be
  // allocated in trailing space when necessary.
  using InitType = llvm::PointerUnion<Stmt *, EvaluatedStmt *>;

  /// The initializer for this variable or, for a ParmVarDecl, the
  /// C++ default argument.
  mutable InitType Init;

private:
  friend class ASTDeclReader;
  friend class ASTNodeImporter;
  friend class StmtIteratorBase;

  class VarDeclBitfields {
    friend class ASTDeclReader;
    friend class VarDecl;

    LLVM_PREFERRED_TYPE(StorageClass)
    unsigned SClass : 3;
    LLVM_PREFERRED_TYPE(ThreadStorageClassSpecifier)
    unsigned TSCSpec : 2;
    LLVM_PREFERRED_TYPE(InitializationStyle)
    unsigned InitStyle : 2;

    /// Whether this variable is an ARC pseudo-__strong variable; see
    /// isARCPseudoStrong() for details.
    LLVM_PREFERRED_TYPE(bool)
    unsigned ARCPseudoStrong : 1;
  };
  enum { NumVarDeclBits = 8 };

protected:
  enum { NumParameterIndexBits = 8 };

  enum DefaultArgKind {
    DAK_None,
    DAK_Unparsed,
    DAK_Uninstantiated,
    DAK_Normal
  };

  enum { NumScopeDepthOrObjCQualsBits = 7 };

  class ParmVarDeclBitfields {
    friend class ASTDeclReader;
    friend class ParmVarDecl;

    LLVM_PREFERRED_TYPE(VarDeclBitfields)
    unsigned : NumVarDeclBits;

    /// Whether this parameter inherits a default argument from a
    /// prior declaration.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasInheritedDefaultArg : 1;

    /// Describes the kind of default argument for this parameter. By default
    /// this is none. If this is normal, then the default argument is stored in
    /// the \c VarDecl initializer expression unless we were unable to parse
    /// (even an invalid) expression for the default argument.
    LLVM_PREFERRED_TYPE(DefaultArgKind)
    unsigned DefaultArgKind : 2;

    /// Whether this parameter undergoes K&R argument promotion.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsKNRPromoted : 1;

    /// Whether this parameter is an ObjC method parameter or not.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsObjCMethodParam : 1;

    /// If IsObjCMethodParam, a Decl::ObjCDeclQualifier.
    /// Otherwise, the number of function parameter scopes enclosing
    /// the function parameter scope in which this parameter was
    /// declared.
    unsigned ScopeDepthOrObjCQuals : NumScopeDepthOrObjCQualsBits;

    /// The number of parameters preceding this parameter in the
    /// function parameter scope in which it was declared.
    unsigned ParameterIndex : NumParameterIndexBits;
  };

  class NonParmVarDeclBitfields {
    friend class ASTDeclReader;
    friend class ImplicitParamDecl;
    friend class VarDecl;

    LLVM_PREFERRED_TYPE(VarDeclBitfields)
    unsigned : NumVarDeclBits;

    // FIXME: We need something similar to CXXRecordDecl::DefinitionData.
    /// Whether this variable is a definition which was demoted due to
    /// module merge.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsThisDeclarationADemotedDefinition : 1;

    /// Whether this variable is the exception variable in a C++ catch
    /// or an Objective-C @catch statement.
    LLVM_PREFERRED_TYPE(bool)
    unsigned ExceptionVar : 1;

    /// Whether this local variable could be allocated in the return
    /// slot of its function, enabling the named return value optimization
    /// (NRVO).
    LLVM_PREFERRED_TYPE(bool)
    unsigned NRVOVariable : 1;

    /// Whether this variable is the for-range-declaration in a C++0x
    /// for-range statement.
    LLVM_PREFERRED_TYPE(bool)
    unsigned CXXForRangeDecl : 1;

    /// Whether this variable is the for-in loop declaration in Objective-C.
    LLVM_PREFERRED_TYPE(bool)
    unsigned ObjCForDecl : 1;

    /// Whether this variable is (C++1z) inline.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsInline : 1;

    /// Whether this variable has (C++1z) inline explicitly specified.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsInlineSpecified : 1;

    /// Whether this variable is (C++0x) constexpr.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsConstexpr : 1;

    /// Whether this variable is the implicit variable for a lambda
    /// init-capture.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsInitCapture : 1;

    /// Whether this local extern variable's previous declaration was
    /// declared in the same block scope. This controls whether we should merge
    /// the type of this declaration with its previous declaration.
    LLVM_PREFERRED_TYPE(bool)
    unsigned PreviousDeclInSameBlockScope : 1;

    /// Defines kind of the ImplicitParamDecl: 'this', 'self', 'vtt', '_cmd' or
    /// something else.
    LLVM_PREFERRED_TYPE(ImplicitParamKind)
    unsigned ImplicitParamKind : 3;

    LLVM_PREFERRED_TYPE(bool)
    unsigned EscapingByref : 1;

    LLVM_PREFERRED_TYPE(bool)
    unsigned IsCXXCondDecl : 1;
  };

  union {
    unsigned AllBits;
    VarDeclBitfields VarDeclBits;
    ParmVarDeclBitfields ParmVarDeclBits;
    NonParmVarDeclBitfields NonParmVarDeclBits;
  };

  VarDecl(Kind DK, ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
          SourceLocation IdLoc, const IdentifierInfo *Id, QualType T,
          TypeSourceInfo *TInfo, StorageClass SC);

  using redeclarable_base = Redeclarable<VarDecl>;

  VarDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  VarDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  VarDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  static VarDecl *Create(ASTContext &C, DeclContext *DC,
                         SourceLocation StartLoc, SourceLocation IdLoc,
                         const IdentifierInfo *Id, QualType T,
                         TypeSourceInfo *TInfo, StorageClass S);

  static VarDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Returns the storage class as written in the source. For the
  /// computed linkage of symbol, see getLinkage.
  StorageClass getStorageClass() const {
    return (StorageClass) VarDeclBits.SClass;
  }
  void setStorageClass(StorageClass SC);

  void setTSCSpec(ThreadStorageClassSpecifier TSC) {
    VarDeclBits.TSCSpec = TSC;
    assert(VarDeclBits.TSCSpec == TSC && "truncation");
  }
  ThreadStorageClassSpecifier getTSCSpec() const {
    return static_cast<ThreadStorageClassSpecifier>(VarDeclBits.TSCSpec);
  }
  TLSKind getTLSKind() const;

  /// Returns true if a variable with function scope is a non-static local
  /// variable.
  bool hasLocalStorage() const {
    if (getStorageClass() == SC_None) {
      // OpenCL v1.2 s6.5.3: The __constant or constant address space name is
      // used to describe variables allocated in global memory and which are
      // accessed inside a kernel(s) as read-only variables. As such, variables
      // in constant address space cannot have local storage.
      if (getType().getAddressSpace() == LangAS::opencl_constant)
        return false;
      // Second check is for C++11 [dcl.stc]p4.
      return !isFileVarDecl() && getTSCSpec() == TSCS_unspecified;
    }

    // Global Named Register (GNU extension)
    if (getStorageClass() == SC_Register && !isLocalVarDeclOrParm())
      return false;

    // Return true for:  Auto, Register.
    // Return false for: Extern, Static, PrivateExtern, OpenCLWorkGroupLocal.

    return getStorageClass() >= SC_Auto;
  }

  /// Returns true if a variable with function scope is a static local
  /// variable.
  bool isStaticLocal() const {
    return (getStorageClass() == SC_Static ||
            // C++11 [dcl.stc]p4
            (getStorageClass() == SC_None && getTSCSpec() == TSCS_thread_local))
      && !isFileVarDecl();
  }

  /// Returns true if a variable has extern or __private_extern__
  /// storage.
  bool hasExternalStorage() const {
    return getStorageClass() == SC_Extern ||
           getStorageClass() == SC_PrivateExtern;
  }

  /// Returns true for all variables that do not have local storage.
  ///
  /// This includes all global variables as well as static variables declared
  /// within a function.
  bool hasGlobalStorage() const { return !hasLocalStorage(); }

  /// Get the storage duration of this variable, per C++ [basic.stc].
  StorageDuration getStorageDuration() const {
    return hasLocalStorage() ? SD_Automatic :
           getTSCSpec() ? SD_Thread : SD_Static;
  }

  /// Compute the language linkage.
  LanguageLinkage getLanguageLinkage() const;

  /// Determines whether this variable is a variable with external, C linkage.
  bool isExternC() const;

  /// Determines whether this variable's context is, or is nested within,
  /// a C++ extern "C" linkage spec.
  bool isInExternCContext() const;

  /// Determines whether this variable's context is, or is nested within,
  /// a C++ extern "C++" linkage spec.
  bool isInExternCXXContext() const;

  /// Returns true for local variable declarations other than parameters.
  /// Note that this includes static variables inside of functions. It also
  /// includes variables inside blocks.
  ///
  ///   void foo() { int x; static int y; extern int z; }
  bool isLocalVarDecl() const {
    if (getKind() != Decl::Var && getKind() != Decl::Decomposition)
      return false;
    if (const DeclContext *DC = getLexicalDeclContext())
      return DC->getRedeclContext()->isFunctionOrMethod();
    return false;
  }

  /// Similar to isLocalVarDecl but also includes parameters.
  bool isLocalVarDeclOrParm() const {
    return isLocalVarDecl() || getKind() == Decl::ParmVar;
  }

  /// Similar to isLocalVarDecl, but excludes variables declared in blocks.
  bool isFunctionOrMethodVarDecl() const {
    if (getKind() != Decl::Var && getKind() != Decl::Decomposition)
      return false;
    const DeclContext *DC = getLexicalDeclContext()->getRedeclContext();
    return DC->isFunctionOrMethod() && DC->getDeclKind() != Decl::Block;
  }

  /// Determines whether this is a static data member.
  ///
  /// This will only be true in C++, and applies to, e.g., the
  /// variable 'x' in:
  /// \code
  /// struct S {
  ///   static int x;
  /// };
  /// \endcode
  bool isStaticDataMember() const {
    // If it wasn't static, it would be a FieldDecl.
    return getKind() != Decl::ParmVar && getDeclContext()->isRecord();
  }

  VarDecl *getCanonicalDecl() override;
  const VarDecl *getCanonicalDecl() const {
    return const_cast<VarDecl*>(this)->getCanonicalDecl();
  }

  enum DefinitionKind {
    /// This declaration is only a declaration.
    DeclarationOnly,

    /// This declaration is a tentative definition.
    TentativeDefinition,

    /// This declaration is definitely a definition.
    Definition
  };

  /// Check whether this declaration is a definition. If this could be
  /// a tentative definition (in C), don't check whether there's an overriding
  /// definition.
  DefinitionKind isThisDeclarationADefinition(ASTContext &) const;
  DefinitionKind isThisDeclarationADefinition() const {
    return isThisDeclarationADefinition(getASTContext());
  }

  /// Check whether this variable is defined in this translation unit.
  DefinitionKind hasDefinition(ASTContext &) const;
  DefinitionKind hasDefinition() const {
    return hasDefinition(getASTContext());
  }

  /// Get the tentative definition that acts as the real definition in a TU.
  /// Returns null if there is a proper definition available.
  VarDecl *getActingDefinition();
  const VarDecl *getActingDefinition() const {
    return const_cast<VarDecl*>(this)->getActingDefinition();
  }

  /// Get the real (not just tentative) definition for this declaration.
  VarDecl *getDefinition(ASTContext &);
  const VarDecl *getDefinition(ASTContext &C) const {
    return const_cast<VarDecl*>(this)->getDefinition(C);
  }
  VarDecl *getDefinition() {
    return getDefinition(getASTContext());
  }
  const VarDecl *getDefinition() const {
    return const_cast<VarDecl*>(this)->getDefinition();
  }

  /// Determine whether this is or was instantiated from an out-of-line
  /// definition of a static data member.
  bool isOutOfLine() const override;

  /// Returns true for file scoped variable declaration.
  bool isFileVarDecl() const {
    Kind K = getKind();
    if (K == ParmVar || K == ImplicitParam)
      return false;

    if (getLexicalDeclContext()->getRedeclContext()->isFileContext())
      return true;

    if (isStaticDataMember())
      return true;

    return false;
  }

  /// Get the initializer for this variable, no matter which
  /// declaration it is attached to.
  const Expr *getAnyInitializer() const {
    const VarDecl *D;
    return getAnyInitializer(D);
  }

  /// Get the initializer for this variable, no matter which
  /// declaration it is attached to. Also get that declaration.
  const Expr *getAnyInitializer(const VarDecl *&D) const;

  bool hasInit() const;
  const Expr *getInit() const {
    return const_cast<VarDecl *>(this)->getInit();
  }
  Expr *getInit();

  /// Retrieve the address of the initializer expression.
  Stmt **getInitAddress();

  void setInit(Expr *I);

  /// Get the initializing declaration of this variable, if any. This is
  /// usually the definition, except that for a static data member it can be
  /// the in-class declaration.
  VarDecl *getInitializingDeclaration();
  const VarDecl *getInitializingDeclaration() const {
    return const_cast<VarDecl *>(this)->getInitializingDeclaration();
  }

  /// Determine whether this variable's value might be usable in a
  /// constant expression, according to the relevant language standard.
  /// This only checks properties of the declaration, and does not check
  /// whether the initializer is in fact a constant expression.
  ///
  /// This corresponds to C++20 [expr.const]p3's notion of a
  /// "potentially-constant" variable.
  bool mightBeUsableInConstantExpressions(const ASTContext &C) const;

  /// Determine whether this variable's value can be used in a
  /// constant expression, according to the relevant language standard,
  /// including checking whether it was initialized by a constant expression.
  bool isUsableInConstantExpressions(const ASTContext &C) const;

  EvaluatedStmt *ensureEvaluatedStmt() const;
  EvaluatedStmt *getEvaluatedStmt() const;

  /// Attempt to evaluate the value of the initializer attached to this
  /// declaration, and produce notes explaining why it cannot be evaluated.
  /// Returns a pointer to the value if evaluation succeeded, 0 otherwise.
  APValue *evaluateValue() const;

private:
  APValue *evaluateValueImpl(SmallVectorImpl<PartialDiagnosticAt> &Notes,
                             bool IsConstantInitialization) const;

public:
  /// Return the already-evaluated value of this variable's
  /// initializer, or NULL if the value is not yet known. Returns pointer
  /// to untyped APValue if the value could not be evaluated.
  APValue *getEvaluatedValue() const;

  /// Evaluate the destruction of this variable to determine if it constitutes
  /// constant destruction.
  ///
  /// \pre hasConstantInitialization()
  /// \return \c true if this variable has constant destruction, \c false if
  ///         not.
  bool evaluateDestruction(SmallVectorImpl<PartialDiagnosticAt> &Notes) const;

  /// Determine whether this variable has constant initialization.
  ///
  /// This is only set in two cases: when the language semantics require
  /// constant initialization (globals in C and some globals in C++), and when
  /// the variable is usable in constant expressions (constexpr, const int, and
  /// reference variables in C++).
  bool hasConstantInitialization() const;

  /// Determine whether the initializer of this variable is an integer constant
  /// expression. For use in C++98, where this affects whether the variable is
  /// usable in constant expressions.
  bool hasICEInitializer(const ASTContext &Context) const;

  /// Evaluate the initializer of this variable to determine whether it's a
  /// constant initializer. Should only be called once, after completing the
  /// definition of the variable.
  bool checkForConstantInitialization(
      SmallVectorImpl<PartialDiagnosticAt> &Notes) const;

  void setInitStyle(InitializationStyle Style) {
    VarDeclBits.InitStyle = Style;
  }

  /// The style of initialization for this declaration.
  ///
  /// C-style initialization is "int x = 1;". Call-style initialization is
  /// a C++98 direct-initializer, e.g. "int x(1);". The Init expression will be
  /// the expression inside the parens or a "ClassType(a,b,c)" class constructor
  /// expression for class types. List-style initialization is C++11 syntax,
  /// e.g. "int x{1};". Clients can distinguish between different forms of
  /// initialization by checking this value. In particular, "int x = {1};" is
  /// C-style, "int x({1})" is call-style, and "int x{1};" is list-style; the
  /// Init expression in all three cases is an InitListExpr.
  InitializationStyle getInitStyle() const {
    return static_cast<InitializationStyle>(VarDeclBits.InitStyle);
  }

  /// Whether the initializer is a direct-initializer (list or call).
  bool isDirectInit() const {
    return getInitStyle() != CInit;
  }

  /// If this definition should pretend to be a declaration.
  bool isThisDeclarationADemotedDefinition() const {
    return isa<ParmVarDecl>(this) ? false :
      NonParmVarDeclBits.IsThisDeclarationADemotedDefinition;
  }

  /// This is a definition which should be demoted to a declaration.
  ///
  /// In some cases (mostly module merging) we can end up with two visible
  /// definitions one of which needs to be demoted to a declaration to keep
  /// the AST invariants.
  void demoteThisDefinitionToDeclaration() {
    assert(isThisDeclarationADefinition() && "Not a definition!");
    assert(!isa<ParmVarDecl>(this) && "Cannot demote ParmVarDecls!");
    NonParmVarDeclBits.IsThisDeclarationADemotedDefinition = 1;
  }

  /// Determine whether this variable is the exception variable in a
  /// C++ catch statememt or an Objective-C \@catch statement.
  bool isExceptionVariable() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.ExceptionVar;
  }
  void setExceptionVariable(bool EV) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.ExceptionVar = EV;
  }

  /// Determine whether this local variable can be used with the named
  /// return value optimization (NRVO).
  ///
  /// The named return value optimization (NRVO) works by marking certain
  /// non-volatile local variables of class type as NRVO objects. These
  /// locals can be allocated within the return slot of their containing
  /// function, in which case there is no need to copy the object to the
  /// return slot when returning from the function. Within the function body,
  /// each return that returns the NRVO object will have this variable as its
  /// NRVO candidate.
  bool isNRVOVariable() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.NRVOVariable;
  }
  void setNRVOVariable(bool NRVO) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.NRVOVariable = NRVO;
  }

  /// Determine whether this variable is the for-range-declaration in
  /// a C++0x for-range statement.
  bool isCXXForRangeDecl() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.CXXForRangeDecl;
  }
  void setCXXForRangeDecl(bool FRD) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.CXXForRangeDecl = FRD;
  }

  /// Determine whether this variable is a for-loop declaration for a
  /// for-in statement in Objective-C.
  bool isObjCForDecl() const {
    return NonParmVarDeclBits.ObjCForDecl;
  }

  void setObjCForDecl(bool FRD) {
    NonParmVarDeclBits.ObjCForDecl = FRD;
  }

  /// Determine whether this variable is an ARC pseudo-__strong variable. A
  /// pseudo-__strong variable has a __strong-qualified type but does not
  /// actually retain the object written into it. Generally such variables are
  /// also 'const' for safety. There are 3 cases where this will be set, 1) if
  /// the variable is annotated with the objc_externally_retained attribute, 2)
  /// if its 'self' in a non-init method, or 3) if its the variable in an for-in
  /// loop.
  bool isARCPseudoStrong() const { return VarDeclBits.ARCPseudoStrong; }
  void setARCPseudoStrong(bool PS) { VarDeclBits.ARCPseudoStrong = PS; }

  /// Whether this variable is (C++1z) inline.
  bool isInline() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.IsInline;
  }
  bool isInlineSpecified() const {
    return isa<ParmVarDecl>(this) ? false
                                  : NonParmVarDeclBits.IsInlineSpecified;
  }
  void setInlineSpecified() {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsInline = true;
    NonParmVarDeclBits.IsInlineSpecified = true;
  }
  void setImplicitlyInline() {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsInline = true;
  }

  /// Whether this variable is (C++11) constexpr.
  bool isConstexpr() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.IsConstexpr;
  }
  void setConstexpr(bool IC) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsConstexpr = IC;
  }

  /// Whether this variable is the implicit variable for a lambda init-capture.
  bool isInitCapture() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.IsInitCapture;
  }
  void setInitCapture(bool IC) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsInitCapture = IC;
  }

  /// Determine whether this variable is actually a function parameter pack or
  /// init-capture pack.
  bool isParameterPack() const;

  /// Whether this local extern variable declaration's previous declaration
  /// was declared in the same block scope. Only correct in C++.
  bool isPreviousDeclInSameBlockScope() const {
    return isa<ParmVarDecl>(this)
               ? false
               : NonParmVarDeclBits.PreviousDeclInSameBlockScope;
  }
  void setPreviousDeclInSameBlockScope(bool Same) {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.PreviousDeclInSameBlockScope = Same;
  }

  /// Indicates the capture is a __block variable that is captured by a block
  /// that can potentially escape (a block for which BlockDecl::doesNotEscape
  /// returns false).
  bool isEscapingByref() const;

  /// Indicates the capture is a __block variable that is never captured by an
  /// escaping block.
  bool isNonEscapingByref() const;

  void setEscapingByref() {
    NonParmVarDeclBits.EscapingByref = true;
  }

  bool isCXXCondDecl() const {
    return isa<ParmVarDecl>(this) ? false : NonParmVarDeclBits.IsCXXCondDecl;
  }

  void setCXXCondDecl() {
    assert(!isa<ParmVarDecl>(this));
    NonParmVarDeclBits.IsCXXCondDecl = true;
  }

  /// Determines if this variable's alignment is dependent.
  bool hasDependentAlignment() const;

  /// Retrieve the variable declaration from which this variable could
  /// be instantiated, if it is an instantiation (rather than a non-template).
  VarDecl *getTemplateInstantiationPattern() const;

  /// If this variable is an instantiated static data member of a
  /// class template specialization, returns the templated static data member
  /// from which it was instantiated.
  VarDecl *getInstantiatedFromStaticDataMember() const;

  /// If this variable is an instantiation of a variable template or a
  /// static data member of a class template, determine what kind of
  /// template specialization or instantiation this is.
  TemplateSpecializationKind getTemplateSpecializationKind() const;

  /// Get the template specialization kind of this variable for the purposes of
  /// template instantiation. This differs from getTemplateSpecializationKind()
  /// for an instantiation of a class-scope explicit specialization.
  TemplateSpecializationKind
  getTemplateSpecializationKindForInstantiation() const;

  /// If this variable is an instantiation of a variable template or a
  /// static data member of a class template, determine its point of
  /// instantiation.
  SourceLocation getPointOfInstantiation() const;

  /// If this variable is an instantiation of a static data member of a
  /// class template specialization, retrieves the member specialization
  /// information.
  MemberSpecializationInfo *getMemberSpecializationInfo() const;

  /// For a static data member that was instantiated from a static
  /// data member of a class template, set the template specialiation kind.
  void setTemplateSpecializationKind(TemplateSpecializationKind TSK,
                        SourceLocation PointOfInstantiation = SourceLocation());

  /// Specify that this variable is an instantiation of the
  /// static data member VD.
  void setInstantiationOfStaticDataMember(VarDecl *VD,
                                          TemplateSpecializationKind TSK);

  /// Retrieves the variable template that is described by this
  /// variable declaration.
  ///
  /// Every variable template is represented as a VarTemplateDecl and a
  /// VarDecl. The former contains template properties (such as
  /// the template parameter lists) while the latter contains the
  /// actual description of the template's
  /// contents. VarTemplateDecl::getTemplatedDecl() retrieves the
  /// VarDecl that from a VarTemplateDecl, while
  /// getDescribedVarTemplate() retrieves the VarTemplateDecl from
  /// a VarDecl.
  VarTemplateDecl *getDescribedVarTemplate() const;

  void setDescribedVarTemplate(VarTemplateDecl *Template);

  // Is this variable known to have a definition somewhere in the complete
  // program? This may be true even if the declaration has internal linkage and
  // has no definition within this source file.
  bool isKnownToBeDefined() const;

  /// Is destruction of this variable entirely suppressed? If so, the variable
  /// need not have a usable destructor at all.
  bool isNoDestroy(const ASTContext &) const;

  /// Would the destruction of this variable have any effect, and if so, what
  /// kind?
  QualType::DestructionKind needsDestruction(const ASTContext &Ctx) const;

  /// Whether this variable has a flexible array member initialized with one
  /// or more elements. This can only be called for declarations where
  /// hasInit() is true.
  ///
  /// (The standard doesn't allow initializing flexible array members; this is
  /// a gcc/msvc extension.)
  bool hasFlexibleArrayInit(const ASTContext &Ctx) const;

  /// If hasFlexibleArrayInit is true, compute the number of additional bytes
  /// necessary to store those elements. Otherwise, returns zero.
  ///
  /// This can only be called for declarations where hasInit() is true.
  CharUnits getFlexibleArrayInitChars(const ASTContext &Ctx) const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstVar && K <= lastVar; }
};

/// Defines the kind of the implicit parameter: is this an implicit parameter
/// with pointer to 'this', 'self', '_cmd', virtual table pointers, captured
/// context or something else.
enum class ImplicitParamKind {
  /// Parameter for Objective-C 'self' argument
  ObjCSelf,

  /// Parameter for Objective-C '_cmd' argument
  ObjCCmd,

  /// Parameter for C++ 'this' argument
  CXXThis,

  /// Parameter for C++ virtual table pointers
  CXXVTT,

  /// Parameter for captured context
  CapturedContext,

  /// Parameter for Thread private variable
  ThreadPrivateVar,

  /// Other implicit parameter
  Other,
};

class ImplicitParamDecl : public VarDecl {
  void anchor() override;

public:
  /// Create implicit parameter.
  static ImplicitParamDecl *Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation IdLoc, IdentifierInfo *Id,
                                   QualType T, ImplicitParamKind ParamKind);
  static ImplicitParamDecl *Create(ASTContext &C, QualType T,
                                   ImplicitParamKind ParamKind);

  static ImplicitParamDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  ImplicitParamDecl(ASTContext &C, DeclContext *DC, SourceLocation IdLoc,
                    const IdentifierInfo *Id, QualType Type,
                    ImplicitParamKind ParamKind)
      : VarDecl(ImplicitParam, C, DC, IdLoc, IdLoc, Id, Type,
                /*TInfo=*/nullptr, SC_None) {
    NonParmVarDeclBits.ImplicitParamKind = llvm::to_underlying(ParamKind);
    setImplicit();
  }

  ImplicitParamDecl(ASTContext &C, QualType Type, ImplicitParamKind ParamKind)
      : VarDecl(ImplicitParam, C, /*DC=*/nullptr, SourceLocation(),
                SourceLocation(), /*Id=*/nullptr, Type,
                /*TInfo=*/nullptr, SC_None) {
    NonParmVarDeclBits.ImplicitParamKind = llvm::to_underlying(ParamKind);
    setImplicit();
  }

  /// Returns the implicit parameter kind.
  ImplicitParamKind getParameterKind() const {
    return static_cast<ImplicitParamKind>(NonParmVarDeclBits.ImplicitParamKind);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ImplicitParam; }
};

/// Represents a parameter to a function.
class ParmVarDecl : public VarDecl {
public:
  enum { MaxFunctionScopeDepth = 255 };
  enum { MaxFunctionScopeIndex = 255 };

protected:
  ParmVarDecl(Kind DK, ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
              SourceLocation IdLoc, const IdentifierInfo *Id, QualType T,
              TypeSourceInfo *TInfo, StorageClass S, Expr *DefArg)
      : VarDecl(DK, C, DC, StartLoc, IdLoc, Id, T, TInfo, S) {
    assert(ParmVarDeclBits.HasInheritedDefaultArg == false);
    assert(ParmVarDeclBits.DefaultArgKind == DAK_None);
    assert(ParmVarDeclBits.IsKNRPromoted == false);
    assert(ParmVarDeclBits.IsObjCMethodParam == false);
    setDefaultArg(DefArg);
  }

public:
  static ParmVarDecl *Create(ASTContext &C, DeclContext *DC,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             const IdentifierInfo *Id, QualType T,
                             TypeSourceInfo *TInfo, StorageClass S,
                             Expr *DefArg);

  static ParmVarDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  void setObjCMethodScopeInfo(unsigned parameterIndex) {
    ParmVarDeclBits.IsObjCMethodParam = true;
    setParameterIndex(parameterIndex);
  }

  void setScopeInfo(unsigned scopeDepth, unsigned parameterIndex) {
    assert(!ParmVarDeclBits.IsObjCMethodParam);

    ParmVarDeclBits.ScopeDepthOrObjCQuals = scopeDepth;
    assert(ParmVarDeclBits.ScopeDepthOrObjCQuals == scopeDepth
           && "truncation!");

    setParameterIndex(parameterIndex);
  }

  bool isObjCMethodParameter() const {
    return ParmVarDeclBits.IsObjCMethodParam;
  }

  /// Determines whether this parameter is destroyed in the callee function.
  bool isDestroyedInCallee() const;

  unsigned getFunctionScopeDepth() const {
    if (ParmVarDeclBits.IsObjCMethodParam) return 0;
    return ParmVarDeclBits.ScopeDepthOrObjCQuals;
  }

  static constexpr unsigned getMaxFunctionScopeDepth() {
    return (1u << NumScopeDepthOrObjCQualsBits) - 1;
  }

  /// Returns the index of this parameter in its prototype or method scope.
  unsigned getFunctionScopeIndex() const {
    return getParameterIndex();
  }

  ObjCDeclQualifier getObjCDeclQualifier() const {
    if (!ParmVarDeclBits.IsObjCMethodParam) return OBJC_TQ_None;
    return ObjCDeclQualifier(ParmVarDeclBits.ScopeDepthOrObjCQuals);
  }
  void setObjCDeclQualifier(ObjCDeclQualifier QTVal) {
    assert(ParmVarDeclBits.IsObjCMethodParam);
    ParmVarDeclBits.ScopeDepthOrObjCQuals = QTVal;
  }

  /// True if the value passed to this parameter must undergo
  /// K&R-style default argument promotion:
  ///
  /// C99 6.5.2.2.
  ///   If the expression that denotes the called function has a type
  ///   that does not include a prototype, the integer promotions are
  ///   performed on each argument, and arguments that have type float
  ///   are promoted to double.
  bool isKNRPromoted() const {
    return ParmVarDeclBits.IsKNRPromoted;
  }
  void setKNRPromoted(bool promoted) {
    ParmVarDeclBits.IsKNRPromoted = promoted;
  }

  bool isExplicitObjectParameter() const {
    return ExplicitObjectParameterIntroducerLoc.isValid();
  }

  void setExplicitObjectParameterLoc(SourceLocation Loc) {
    ExplicitObjectParameterIntroducerLoc = Loc;
  }

  SourceLocation getExplicitObjectParamThisLoc() const {
    return ExplicitObjectParameterIntroducerLoc;
  }

  Expr *getDefaultArg();
  const Expr *getDefaultArg() const {
    return const_cast<ParmVarDecl *>(this)->getDefaultArg();
  }

  void setDefaultArg(Expr *defarg);

  /// Retrieve the source range that covers the entire default
  /// argument.
  SourceRange getDefaultArgRange() const;
  void setUninstantiatedDefaultArg(Expr *arg);
  Expr *getUninstantiatedDefaultArg();
  const Expr *getUninstantiatedDefaultArg() const {
    return const_cast<ParmVarDecl *>(this)->getUninstantiatedDefaultArg();
  }

  /// Determines whether this parameter has a default argument,
  /// either parsed or not.
  bool hasDefaultArg() const;

  /// Determines whether this parameter has a default argument that has not
  /// yet been parsed. This will occur during the processing of a C++ class
  /// whose member functions have default arguments, e.g.,
  /// @code
  ///   class X {
  ///   public:
  ///     void f(int x = 17); // x has an unparsed default argument now
  ///   }; // x has a regular default argument now
  /// @endcode
  bool hasUnparsedDefaultArg() const {
    return ParmVarDeclBits.DefaultArgKind == DAK_Unparsed;
  }

  bool hasUninstantiatedDefaultArg() const {
    return ParmVarDeclBits.DefaultArgKind == DAK_Uninstantiated;
  }

  /// Specify that this parameter has an unparsed default argument.
  /// The argument will be replaced with a real default argument via
  /// setDefaultArg when the class definition enclosing the function
  /// declaration that owns this default argument is completed.
  void setUnparsedDefaultArg() {
    ParmVarDeclBits.DefaultArgKind = DAK_Unparsed;
  }

  bool hasInheritedDefaultArg() const {
    return ParmVarDeclBits.HasInheritedDefaultArg;
  }

  void setHasInheritedDefaultArg(bool I = true) {
    ParmVarDeclBits.HasInheritedDefaultArg = I;
  }

  QualType getOriginalType() const;

  /// Sets the function declaration that owns this
  /// ParmVarDecl. Since ParmVarDecls are often created before the
  /// FunctionDecls that own them, this routine is required to update
  /// the DeclContext appropriately.
  void setOwningFunction(DeclContext *FD) { setDeclContext(FD); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ParmVar; }

private:
  friend class ASTDeclReader;

  enum { ParameterIndexSentinel = (1 << NumParameterIndexBits) - 1 };
  SourceLocation ExplicitObjectParameterIntroducerLoc;

  void setParameterIndex(unsigned parameterIndex) {
    if (parameterIndex >= ParameterIndexSentinel) {
      setParameterIndexLarge(parameterIndex);
      return;
    }

    ParmVarDeclBits.ParameterIndex = parameterIndex;
    assert(ParmVarDeclBits.ParameterIndex == parameterIndex && "truncation!");
  }
  unsigned getParameterIndex() const {
    unsigned d = ParmVarDeclBits.ParameterIndex;
    return d == ParameterIndexSentinel ? getParameterIndexLarge() : d;
  }

  void setParameterIndexLarge(unsigned parameterIndex);
  unsigned getParameterIndexLarge() const;
};

enum class MultiVersionKind {
  None,
  Target,
  CPUSpecific,
  CPUDispatch,
  TargetClones,
  TargetVersion
};

/// Represents a function declaration or definition.
///
/// Since a given function can be declared several times in a program,
/// there may be several FunctionDecls that correspond to that
/// function. Only one of those FunctionDecls will be found when
/// traversing the list of declarations in the context of the
/// FunctionDecl (e.g., the translation unit); this FunctionDecl
/// contains all of the information known about the function. Other,
/// previous declarations of the function are available via the
/// getPreviousDecl() chain.
class FunctionDecl : public DeclaratorDecl,
                     public DeclContext,
                     public Redeclarable<FunctionDecl> {
  // This class stores some data in DeclContext::FunctionDeclBits
  // to save some space. Use the provided accessors to access it.
public:
  /// The kind of templated function a FunctionDecl can be.
  enum TemplatedKind {
    // Not templated.
    TK_NonTemplate,
    // The pattern in a function template declaration.
    TK_FunctionTemplate,
    // A non-template function that is an instantiation or explicit
    // specialization of a member of a templated class.
    TK_MemberSpecialization,
    // An instantiation or explicit specialization of a function template.
    // Note: this might have been instantiated from a templated class if it
    // is a class-scope explicit specialization.
    TK_FunctionTemplateSpecialization,
    // A function template specialization that hasn't yet been resolved to a
    // particular specialized function template.
    TK_DependentFunctionTemplateSpecialization,
    // A non-template function which is in a dependent scope.
    TK_DependentNonTemplate

  };

  /// Stashed information about a defaulted/deleted function body.
  class DefaultedOrDeletedFunctionInfo final
      : llvm::TrailingObjects<DefaultedOrDeletedFunctionInfo, DeclAccessPair,
                              StringLiteral *> {
    friend TrailingObjects;
    unsigned NumLookups;
    bool HasDeletedMessage;

    size_t numTrailingObjects(OverloadToken<DeclAccessPair>) const {
      return NumLookups;
    }

  public:
    static DefaultedOrDeletedFunctionInfo *
    Create(ASTContext &Context, ArrayRef<DeclAccessPair> Lookups,
           StringLiteral *DeletedMessage = nullptr);

    /// Get the unqualified lookup results that should be used in this
    /// defaulted function definition.
    ArrayRef<DeclAccessPair> getUnqualifiedLookups() const {
      return {getTrailingObjects<DeclAccessPair>(), NumLookups};
    }

    StringLiteral *getDeletedMessage() const {
      return HasDeletedMessage ? *getTrailingObjects<StringLiteral *>()
                               : nullptr;
    }

    void setDeletedMessage(StringLiteral *Message);
  };

private:
  /// A new[]'d array of pointers to VarDecls for the formal
  /// parameters of this function.  This is null if a prototype or if there are
  /// no formals.
  ParmVarDecl **ParamInfo = nullptr;

  /// The active member of this union is determined by
  /// FunctionDeclBits.HasDefaultedOrDeletedInfo.
  union {
    /// The body of the function.
    LazyDeclStmtPtr Body;
    /// Information about a future defaulted function definition.
    DefaultedOrDeletedFunctionInfo *DefaultedOrDeletedInfo;
  };

  unsigned ODRHash;

  /// End part of this FunctionDecl's source range.
  ///
  /// We could compute the full range in getSourceRange(). However, when we're
  /// dealing with a function definition deserialized from a PCH/AST file,
  /// we can only compute the full range once the function body has been
  /// de-serialized, so it's far better to have the (sometimes-redundant)
  /// EndRangeLoc.
  SourceLocation EndRangeLoc;

  SourceLocation DefaultKWLoc;

  /// The template or declaration that this declaration
  /// describes or was instantiated from, respectively.
  ///
  /// For non-templates this value will be NULL, unless this declaration was
  /// declared directly inside of a function template, in which case it will
  /// have a pointer to a FunctionDecl, stored in the NamedDecl. For function
  /// declarations that describe a function template, this will be a pointer to
  /// a FunctionTemplateDecl, stored in the NamedDecl. For member functions of
  /// class template specializations, this will be a MemberSpecializationInfo
  /// pointer containing information about the specialization.
  /// For function template specializations, this will be a
  /// FunctionTemplateSpecializationInfo, which contains information about
  /// the template being specialized and the template arguments involved in
  /// that specialization.
  llvm::PointerUnion<NamedDecl *, MemberSpecializationInfo *,
                     FunctionTemplateSpecializationInfo *,
                     DependentFunctionTemplateSpecializationInfo *>
      TemplateOrSpecialization;

  /// Provides source/type location info for the declaration name embedded in
  /// the DeclaratorDecl base class.
  DeclarationNameLoc DNLoc;

  /// Specify that this function declaration is actually a function
  /// template specialization.
  ///
  /// \param C the ASTContext.
  ///
  /// \param Template the function template that this function template
  /// specialization specializes.
  ///
  /// \param TemplateArgs the template arguments that produced this
  /// function template specialization from the template.
  ///
  /// \param InsertPos If non-NULL, the position in the function template
  /// specialization set where the function template specialization data will
  /// be inserted.
  ///
  /// \param TSK the kind of template specialization this is.
  ///
  /// \param TemplateArgsAsWritten location info of template arguments.
  ///
  /// \param PointOfInstantiation point at which the function template
  /// specialization was first instantiated.
  void setFunctionTemplateSpecialization(
      ASTContext &C, FunctionTemplateDecl *Template,
      TemplateArgumentList *TemplateArgs, void *InsertPos,
      TemplateSpecializationKind TSK,
      const TemplateArgumentListInfo *TemplateArgsAsWritten,
      SourceLocation PointOfInstantiation);

  /// Specify that this record is an instantiation of the
  /// member function FD.
  void setInstantiationOfMemberFunction(ASTContext &C, FunctionDecl *FD,
                                        TemplateSpecializationKind TSK);

  void setParams(ASTContext &C, ArrayRef<ParmVarDecl *> NewParamInfo);

  // This is unfortunately needed because ASTDeclWriter::VisitFunctionDecl
  // need to access this bit but we want to avoid making ASTDeclWriter
  // a friend of FunctionDeclBitfields just for this.
  bool isDeletedBit() const { return FunctionDeclBits.IsDeleted; }

  /// Whether an ODRHash has been stored.
  bool hasODRHash() const { return FunctionDeclBits.HasODRHash; }

  /// State that an ODRHash has been stored.
  void setHasODRHash(bool B = true) { FunctionDeclBits.HasODRHash = B; }

protected:
  FunctionDecl(Kind DK, ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
               const DeclarationNameInfo &NameInfo, QualType T,
               TypeSourceInfo *TInfo, StorageClass S, bool UsesFPIntrin,
               bool isInlineSpecified, ConstexprSpecKind ConstexprKind,
               Expr *TrailingRequiresClause = nullptr);

  using redeclarable_base = Redeclarable<FunctionDecl>;

  FunctionDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  FunctionDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  FunctionDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  static FunctionDecl *
  Create(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
         SourceLocation NLoc, DeclarationName N, QualType T,
         TypeSourceInfo *TInfo, StorageClass SC, bool UsesFPIntrin = false,
         bool isInlineSpecified = false, bool hasWrittenPrototype = true,
         ConstexprSpecKind ConstexprKind = ConstexprSpecKind::Unspecified,
         Expr *TrailingRequiresClause = nullptr) {
    DeclarationNameInfo NameInfo(N, NLoc);
    return FunctionDecl::Create(C, DC, StartLoc, NameInfo, T, TInfo, SC,
                                UsesFPIntrin, isInlineSpecified,
                                hasWrittenPrototype, ConstexprKind,
                                TrailingRequiresClause);
  }

  static FunctionDecl *
  Create(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
         const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
         StorageClass SC, bool UsesFPIntrin, bool isInlineSpecified,
         bool hasWrittenPrototype, ConstexprSpecKind ConstexprKind,
         Expr *TrailingRequiresClause);

  static FunctionDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDeclName(), getLocation(), DNLoc);
  }

  void getNameForDiagnostic(raw_ostream &OS, const PrintingPolicy &Policy,
                            bool Qualified) const override;

  void setRangeEnd(SourceLocation E) { EndRangeLoc = E; }

  void setDeclarationNameLoc(DeclarationNameLoc L) { DNLoc = L; }

  /// Returns the location of the ellipsis of a variadic function.
  SourceLocation getEllipsisLoc() const {
    const auto *FPT = getType()->getAs<FunctionProtoType>();
    if (FPT && FPT->isVariadic())
      return FPT->getEllipsisLoc();
    return SourceLocation();
  }

  SourceRange getSourceRange() const override LLVM_READONLY;

  // Function definitions.
  //
  // A function declaration may be:
  // - a non defining declaration,
  // - a definition. A function may be defined because:
  //   - it has a body, or will have it in the case of late parsing.
  //   - it has an uninstantiated body. The body does not exist because the
  //     function is not used yet, but the declaration is considered a
  //     definition and does not allow other definition of this function.
  //   - it does not have a user specified body, but it does not allow
  //     redefinition, because it is deleted/defaulted or is defined through
  //     some other mechanism (alias, ifunc).

  /// Returns true if the function has a body.
  ///
  /// The function body might be in any of the (re-)declarations of this
  /// function. The variant that accepts a FunctionDecl pointer will set that
  /// function declaration to the actual declaration containing the body (if
  /// there is one).
  bool hasBody(const FunctionDecl *&Definition) const;

  bool hasBody() const override {
    const FunctionDecl* Definition;
    return hasBody(Definition);
  }

  /// Returns whether the function has a trivial body that does not require any
  /// specific codegen.
  bool hasTrivialBody() const;

  /// Returns true if the function has a definition that does not need to be
  /// instantiated.
  ///
  /// The variant that accepts a FunctionDecl pointer will set that function
  /// declaration to the declaration that is a definition (if there is one).
  ///
  /// \param CheckForPendingFriendDefinition If \c true, also check for friend
  ///        declarations that were instantiated from function definitions.
  ///        Such a declaration behaves as if it is a definition for the
  ///        purpose of redefinition checking, but isn't actually a "real"
  ///        definition until its body is instantiated.
  bool isDefined(const FunctionDecl *&Definition,
                 bool CheckForPendingFriendDefinition = false) const;

  bool isDefined() const {
    const FunctionDecl* Definition;
    return isDefined(Definition);
  }

  /// Get the definition for this declaration.
  FunctionDecl *getDefinition() {
    const FunctionDecl *Definition;
    if (isDefined(Definition))
      return const_cast<FunctionDecl *>(Definition);
    return nullptr;
  }
  const FunctionDecl *getDefinition() const {
    return const_cast<FunctionDecl *>(this)->getDefinition();
  }

  /// Retrieve the body (definition) of the function. The function body might be
  /// in any of the (re-)declarations of this function. The variant that accepts
  /// a FunctionDecl pointer will set that function declaration to the actual
  /// declaration containing the body (if there is one).
  /// NOTE: For checking if there is a body, use hasBody() instead, to avoid
  /// unnecessary AST de-serialization of the body.
  Stmt *getBody(const FunctionDecl *&Definition) const;

  Stmt *getBody() const override {
    const FunctionDecl* Definition;
    return getBody(Definition);
  }

  /// Returns whether this specific declaration of the function is also a
  /// definition that does not contain uninstantiated body.
  ///
  /// This does not determine whether the function has been defined (e.g., in a
  /// previous definition); for that information, use isDefined.
  ///
  /// Note: the function declaration does not become a definition until the
  /// parser reaches the definition, if called before, this function will return
  /// `false`.
  bool isThisDeclarationADefinition() const {
    return isDeletedAsWritten() || isDefaulted() ||
           doesThisDeclarationHaveABody() || hasSkippedBody() ||
           willHaveBody() || hasDefiningAttr();
  }

  /// Determine whether this specific declaration of the function is a friend
  /// declaration that was instantiated from a function definition. Such
  /// declarations behave like definitions in some contexts.
  bool isThisDeclarationInstantiatedFromAFriendDefinition() const;

  /// Returns whether this specific declaration of the function has a body.
  bool doesThisDeclarationHaveABody() const {
    return (!FunctionDeclBits.HasDefaultedOrDeletedInfo && Body) ||
           isLateTemplateParsed();
  }

  void setBody(Stmt *B);
  void setLazyBody(uint64_t Offset) {
    FunctionDeclBits.HasDefaultedOrDeletedInfo = false;
    Body = LazyDeclStmtPtr(Offset);
  }

  void setDefaultedOrDeletedInfo(DefaultedOrDeletedFunctionInfo *Info);
  DefaultedOrDeletedFunctionInfo *getDefalutedOrDeletedInfo() const;

  /// Whether this function is variadic.
  bool isVariadic() const;

  /// Whether this function is marked as virtual explicitly.
  bool isVirtualAsWritten() const {
    return FunctionDeclBits.IsVirtualAsWritten;
  }

  /// State that this function is marked as virtual explicitly.
  void setVirtualAsWritten(bool V) { FunctionDeclBits.IsVirtualAsWritten = V; }

  /// Whether this virtual function is pure, i.e. makes the containing class
  /// abstract.
  bool isPureVirtual() const { return FunctionDeclBits.IsPureVirtual; }
  void setIsPureVirtual(bool P = true);

  /// Whether this templated function will be late parsed.
  bool isLateTemplateParsed() const {
    return FunctionDeclBits.IsLateTemplateParsed;
  }

  /// State that this templated function will be late parsed.
  void setLateTemplateParsed(bool ILT = true) {
    FunctionDeclBits.IsLateTemplateParsed = ILT;
  }

  /// Whether this function is "trivial" in some specialized C++ senses.
  /// Can only be true for default constructors, copy constructors,
  /// copy assignment operators, and destructors.  Not meaningful until
  /// the class has been fully built by Sema.
  bool isTrivial() const { return FunctionDeclBits.IsTrivial; }
  void setTrivial(bool IT) { FunctionDeclBits.IsTrivial = IT; }

  bool isTrivialForCall() const { return FunctionDeclBits.IsTrivialForCall; }
  void setTrivialForCall(bool IT) { FunctionDeclBits.IsTrivialForCall = IT; }

  /// Whether this function is defaulted. Valid for e.g.
  /// special member functions, defaulted comparisions (not methods!).
  bool isDefaulted() const { return FunctionDeclBits.IsDefaulted; }
  void setDefaulted(bool D = true) { FunctionDeclBits.IsDefaulted = D; }

  /// Whether this function is explicitly defaulted.
  bool isExplicitlyDefaulted() const {
    return FunctionDeclBits.IsExplicitlyDefaulted;
  }

  /// State that this function is explicitly defaulted.
  void setExplicitlyDefaulted(bool ED = true) {
    FunctionDeclBits.IsExplicitlyDefaulted = ED;
  }

  SourceLocation getDefaultLoc() const {
    return isExplicitlyDefaulted() ? DefaultKWLoc : SourceLocation();
  }

  void setDefaultLoc(SourceLocation NewLoc) {
    assert((NewLoc.isInvalid() || isExplicitlyDefaulted()) &&
           "Can't set default loc is function isn't explicitly defaulted");
    DefaultKWLoc = NewLoc;
  }

  /// True if this method is user-declared and was not
  /// deleted or defaulted on its first declaration.
  bool isUserProvided() const {
    auto *DeclAsWritten = this;
    if (FunctionDecl *Pattern = getTemplateInstantiationPattern())
      DeclAsWritten = Pattern;
    return !(DeclAsWritten->isDeleted() ||
             DeclAsWritten->getCanonicalDecl()->isDefaulted());
  }

  bool isIneligibleOrNotSelected() const {
    return FunctionDeclBits.IsIneligibleOrNotSelected;
  }
  void setIneligibleOrNotSelected(bool II) {
    FunctionDeclBits.IsIneligibleOrNotSelected = II;
  }

  /// Whether falling off this function implicitly returns null/zero.
  /// If a more specific implicit return value is required, front-ends
  /// should synthesize the appropriate return statements.
  bool hasImplicitReturnZero() const {
    return FunctionDeclBits.HasImplicitReturnZero;
  }

  /// State that falling off this function implicitly returns null/zero.
  /// If a more specific implicit return value is required, front-ends
  /// should synthesize the appropriate return statements.
  void setHasImplicitReturnZero(bool IRZ) {
    FunctionDeclBits.HasImplicitReturnZero = IRZ;
  }

  /// Whether this function has a prototype, either because one
  /// was explicitly written or because it was "inherited" by merging
  /// a declaration without a prototype with a declaration that has a
  /// prototype.
  bool hasPrototype() const {
    return hasWrittenPrototype() || hasInheritedPrototype();
  }

  /// Whether this function has a written prototype.
  bool hasWrittenPrototype() const {
    return FunctionDeclBits.HasWrittenPrototype;
  }

  /// State that this function has a written prototype.
  void setHasWrittenPrototype(bool P = true) {
    FunctionDeclBits.HasWrittenPrototype = P;
  }

  /// Whether this function inherited its prototype from a
  /// previous declaration.
  bool hasInheritedPrototype() const {
    return FunctionDeclBits.HasInheritedPrototype;
  }

  /// State that this function inherited its prototype from a
  /// previous declaration.
  void setHasInheritedPrototype(bool P = true) {
    FunctionDeclBits.HasInheritedPrototype = P;
  }

  /// Whether this is a (C++11) constexpr function or constexpr constructor.
  bool isConstexpr() const {
    return getConstexprKind() != ConstexprSpecKind::Unspecified;
  }
  void setConstexprKind(ConstexprSpecKind CSK) {
    FunctionDeclBits.ConstexprKind = static_cast<uint64_t>(CSK);
  }
  ConstexprSpecKind getConstexprKind() const {
    return static_cast<ConstexprSpecKind>(FunctionDeclBits.ConstexprKind);
  }
  bool isConstexprSpecified() const {
    return getConstexprKind() == ConstexprSpecKind::Constexpr;
  }
  bool isConsteval() const {
    return getConstexprKind() == ConstexprSpecKind::Consteval;
  }

  void setBodyContainsImmediateEscalatingExpressions(bool Set) {
    FunctionDeclBits.BodyContainsImmediateEscalatingExpression = Set;
  }

  bool BodyContainsImmediateEscalatingExpressions() const {
    return FunctionDeclBits.BodyContainsImmediateEscalatingExpression;
  }

  bool isImmediateEscalating() const;

  // The function is a C++ immediate function.
  // This can be either a consteval function, or an immediate escalating
  // function containing an immediate escalating expression.
  bool isImmediateFunction() const;

  /// Whether the instantiation of this function is pending.
  /// This bit is set when the decision to instantiate this function is made
  /// and unset if and when the function body is created. That leaves out
  /// cases where instantiation did not happen because the template definition
  /// was not seen in this TU. This bit remains set in those cases, under the
  /// assumption that the instantiation will happen in some other TU.
  bool instantiationIsPending() const {
    return FunctionDeclBits.InstantiationIsPending;
  }

  /// State that the instantiation of this function is pending.
  /// (see instantiationIsPending)
  void setInstantiationIsPending(bool IC) {
    FunctionDeclBits.InstantiationIsPending = IC;
  }

  /// Indicates the function uses __try.
  bool usesSEHTry() const { return FunctionDeclBits.UsesSEHTry; }
  void setUsesSEHTry(bool UST) { FunctionDeclBits.UsesSEHTry = UST; }

  /// Whether this function has been deleted.
  ///
  /// A function that is "deleted" (via the C++0x "= delete" syntax)
  /// acts like a normal function, except that it cannot actually be
  /// called or have its address taken. Deleted functions are
  /// typically used in C++ overload resolution to attract arguments
  /// whose type or lvalue/rvalue-ness would permit the use of a
  /// different overload that would behave incorrectly. For example,
  /// one might use deleted functions to ban implicit conversion from
  /// a floating-point number to an Integer type:
  ///
  /// @code
  /// struct Integer {
  ///   Integer(long); // construct from a long
  ///   Integer(double) = delete; // no construction from float or double
  ///   Integer(long double) = delete; // no construction from long double
  /// };
  /// @endcode
  // If a function is deleted, its first declaration must be.
  bool isDeleted() const {
    return getCanonicalDecl()->FunctionDeclBits.IsDeleted;
  }

  bool isDeletedAsWritten() const {
    return FunctionDeclBits.IsDeleted && !isDefaulted();
  }

  void setDeletedAsWritten(bool D = true, StringLiteral *Message = nullptr);

  /// Determines whether this function is "main", which is the
  /// entry point into an executable program.
  bool isMain() const;

  /// Determines whether this function is a MSVCRT user defined entry
  /// point.
  bool isMSVCRTEntryPoint() const;

  /// Determines whether this operator new or delete is one
  /// of the reserved global placement operators:
  ///    void *operator new(size_t, void *);
  ///    void *operator new[](size_t, void *);
  ///    void operator delete(void *, void *);
  ///    void operator delete[](void *, void *);
  /// These functions have special behavior under [new.delete.placement]:
  ///    These functions are reserved, a C++ program may not define
  ///    functions that displace the versions in the Standard C++ library.
  ///    The provisions of [basic.stc.dynamic] do not apply to these
  ///    reserved placement forms of operator new and operator delete.
  ///
  /// This function must be an allocation or deallocation function.
  bool isReservedGlobalPlacementOperator() const;

  /// Determines whether this function is one of the replaceable
  /// global allocation functions:
  ///    void *operator new(size_t);
  ///    void *operator new(size_t, const std::nothrow_t &) noexcept;
  ///    void *operator new[](size_t);
  ///    void *operator new[](size_t, const std::nothrow_t &) noexcept;
  ///    void operator delete(void *) noexcept;
  ///    void operator delete(void *, std::size_t) noexcept;      [C++1y]
  ///    void operator delete(void *, const std::nothrow_t &) noexcept;
  ///    void operator delete[](void *) noexcept;
  ///    void operator delete[](void *, std::size_t) noexcept;    [C++1y]
  ///    void operator delete[](void *, const std::nothrow_t &) noexcept;
  /// These functions have special behavior under C++1y [expr.new]:
  ///    An implementation is allowed to omit a call to a replaceable global
  ///    allocation function. [...]
  ///
  /// If this function is an aligned allocation/deallocation function, return
  /// the parameter number of the requested alignment through AlignmentParam.
  ///
  /// If this function is an allocation/deallocation function that takes
  /// the `std::nothrow_t` tag, return true through IsNothrow,
  bool isReplaceableGlobalAllocationFunction(
      std::optional<unsigned> *AlignmentParam = nullptr,
      bool *IsNothrow = nullptr) const;

  /// Determine if this function provides an inline implementation of a builtin.
  bool isInlineBuiltinDeclaration() const;

  /// Determine whether this is a destroying operator delete.
  bool isDestroyingOperatorDelete() const;

  /// Compute the language linkage.
  LanguageLinkage getLanguageLinkage() const;

  /// Determines whether this function is a function with
  /// external, C linkage.
  bool isExternC() const;

  /// Determines whether this function's context is, or is nested within,
  /// a C++ extern "C" linkage spec.
  bool isInExternCContext() const;

  /// Determines whether this function's context is, or is nested within,
  /// a C++ extern "C++" linkage spec.
  bool isInExternCXXContext() const;

  /// Determines whether this is a global function.
  bool isGlobal() const;

  /// Determines whether this function is known to be 'noreturn', through
  /// an attribute on its declaration or its type.
  bool isNoReturn() const;

  /// True if the function was a definition but its body was skipped.
  bool hasSkippedBody() const { return FunctionDeclBits.HasSkippedBody; }
  void setHasSkippedBody(bool Skipped = true) {
    FunctionDeclBits.HasSkippedBody = Skipped;
  }

  /// True if this function will eventually have a body, once it's fully parsed.
  bool willHaveBody() const { return FunctionDeclBits.WillHaveBody; }
  void setWillHaveBody(bool V = true) { FunctionDeclBits.WillHaveBody = V; }

  /// True if this function is considered a multiversioned function.
  bool isMultiVersion() const {
    return getCanonicalDecl()->FunctionDeclBits.IsMultiVersion;
  }

  /// Sets the multiversion state for this declaration and all of its
  /// redeclarations.
  void setIsMultiVersion(bool V = true) {
    getCanonicalDecl()->FunctionDeclBits.IsMultiVersion = V;
  }

  // Sets that this is a constrained friend where the constraint refers to an
  // enclosing template.
  void setFriendConstraintRefersToEnclosingTemplate(bool V = true) {
    getCanonicalDecl()
        ->FunctionDeclBits.FriendConstraintRefersToEnclosingTemplate = V;
  }
  // Indicates this function is a constrained friend, where the constraint
  // refers to an enclosing template for hte purposes of [temp.friend]p9.
  bool FriendConstraintRefersToEnclosingTemplate() const {
    return getCanonicalDecl()
        ->FunctionDeclBits.FriendConstraintRefersToEnclosingTemplate;
  }

  /// Determine whether a function is a friend function that cannot be
  /// redeclared outside of its class, per C++ [temp.friend]p9.
  bool isMemberLikeConstrainedFriend() const;

  /// Gets the kind of multiversioning attribute this declaration has. Note that
  /// this can return a value even if the function is not multiversion, such as
  /// the case of 'target'.
  MultiVersionKind getMultiVersionKind() const;


  /// True if this function is a multiversioned dispatch function as a part of
  /// the cpu_specific/cpu_dispatch functionality.
  bool isCPUDispatchMultiVersion() const;
  /// True if this function is a multiversioned processor specific function as a
  /// part of the cpu_specific/cpu_dispatch functionality.
  bool isCPUSpecificMultiVersion() const;

  /// True if this function is a multiversioned dispatch function as a part of
  /// the target functionality.
  bool isTargetMultiVersion() const;

  /// True if this function is the default version of a multiversioned dispatch
  /// function as a part of the target functionality.
  bool isTargetMultiVersionDefault() const;

  /// True if this function is a multiversioned dispatch function as a part of
  /// the target-clones functionality.
  bool isTargetClonesMultiVersion() const;

  /// True if this function is a multiversioned dispatch function as a part of
  /// the target-version functionality.
  bool isTargetVersionMultiVersion() const;

  /// \brief Get the associated-constraints of this function declaration.
  /// Currently, this will either be a vector of size 1 containing the
  /// trailing-requires-clause or an empty vector.
  ///
  /// Use this instead of getTrailingRequiresClause for concepts APIs that
  /// accept an ArrayRef of constraint expressions.
  void getAssociatedConstraints(SmallVectorImpl<const Expr *> &AC) const {
    if (auto *TRC = getTrailingRequiresClause())
      AC.push_back(TRC);
  }

  /// Get the message that indicates why this function was deleted.
  StringLiteral *getDeletedMessage() const {
    return FunctionDeclBits.HasDefaultedOrDeletedInfo
               ? DefaultedOrDeletedInfo->getDeletedMessage()
               : nullptr;
  }

  void setPreviousDeclaration(FunctionDecl * PrevDecl);

  FunctionDecl *getCanonicalDecl() override;
  const FunctionDecl *getCanonicalDecl() const {
    return const_cast<FunctionDecl*>(this)->getCanonicalDecl();
  }

  unsigned getBuiltinID(bool ConsiderWrapperFunctions = false) const;

  // ArrayRef interface to parameters.
  ArrayRef<ParmVarDecl *> parameters() const {
    return {ParamInfo, getNumParams()};
  }
  MutableArrayRef<ParmVarDecl *> parameters() {
    return {ParamInfo, getNumParams()};
  }

  // Iterator access to formal parameters.
  using param_iterator = MutableArrayRef<ParmVarDecl *>::iterator;
  using param_const_iterator = ArrayRef<ParmVarDecl *>::const_iterator;

  bool param_empty() const { return parameters().empty(); }
  param_iterator param_begin() { return parameters().begin(); }
  param_iterator param_end() { return parameters().end(); }
  param_const_iterator param_begin() const { return parameters().begin(); }
  param_const_iterator param_end() const { return parameters().end(); }
  size_t param_size() const { return parameters().size(); }

  /// Return the number of parameters this function must have based on its
  /// FunctionType.  This is the length of the ParamInfo array after it has been
  /// created.
  unsigned getNumParams() const;

  const ParmVarDecl *getParamDecl(unsigned i) const {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }
  ParmVarDecl *getParamDecl(unsigned i) {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }
  void setParams(ArrayRef<ParmVarDecl *> NewParamInfo) {
    setParams(getASTContext(), NewParamInfo);
  }

  /// Returns the minimum number of arguments needed to call this function. This
  /// may be fewer than the number of function parameters, if some of the
  /// parameters have default arguments (in C++).
  unsigned getMinRequiredArguments() const;

  /// Returns the minimum number of non-object arguments needed to call this
  /// function. This produces the same value as getMinRequiredArguments except
  /// it does not count the explicit object argument, if any.
  unsigned getMinRequiredExplicitArguments() const;

  bool hasCXXExplicitFunctionObjectParameter() const;

  unsigned getNumNonObjectParams() const;

  const ParmVarDecl *getNonObjectParameter(unsigned I) const {
    return getParamDecl(hasCXXExplicitFunctionObjectParameter() ? I + 1 : I);
  }

  ParmVarDecl *getNonObjectParameter(unsigned I) {
    return getParamDecl(hasCXXExplicitFunctionObjectParameter() ? I + 1 : I);
  }

  /// Determine whether this function has a single parameter, or multiple
  /// parameters where all but the first have default arguments.
  ///
  /// This notion is used in the definition of copy/move constructors and
  /// initializer list constructors. Note that, unlike getMinRequiredArguments,
  /// parameter packs are not treated specially here.
  bool hasOneParamOrDefaultArgs() const;

  /// Find the source location information for how the type of this function
  /// was written. May be absent (for example if the function was declared via
  /// a typedef) and may contain a different type from that of the function
  /// (for example if the function type was adjusted by an attribute).
  FunctionTypeLoc getFunctionTypeLoc() const;

  QualType getReturnType() const {
    return getType()->castAs<FunctionType>()->getReturnType();
  }

  /// Attempt to compute an informative source range covering the
  /// function return type. This may omit qualifiers and other information with
  /// limited representation in the AST.
  SourceRange getReturnTypeSourceRange() const;

  /// Attempt to compute an informative source range covering the
  /// function parameters, including the ellipsis of a variadic function.
  /// The source range excludes the parentheses, and is invalid if there are
  /// no parameters and no ellipsis.
  SourceRange getParametersSourceRange() const;

  /// Get the declared return type, which may differ from the actual return
  /// type if the return type is deduced.
  QualType getDeclaredReturnType() const {
    auto *TSI = getTypeSourceInfo();
    QualType T = TSI ? TSI->getType() : getType();
    return T->castAs<FunctionType>()->getReturnType();
  }

  /// Gets the ExceptionSpecificationType as declared.
  ExceptionSpecificationType getExceptionSpecType() const {
    auto *TSI = getTypeSourceInfo();
    QualType T = TSI ? TSI->getType() : getType();
    const auto *FPT = T->getAs<FunctionProtoType>();
    return FPT ? FPT->getExceptionSpecType() : EST_None;
  }

  /// Attempt to compute an informative source range covering the
  /// function exception specification, if any.
  SourceRange getExceptionSpecSourceRange() const;

  /// Determine the type of an expression that calls this function.
  QualType getCallResultType() const {
    return getType()->castAs<FunctionType>()->getCallResultType(
        getASTContext());
  }

  /// Returns the storage class as written in the source. For the
  /// computed linkage of symbol, see getLinkage.
  StorageClass getStorageClass() const {
    return static_cast<StorageClass>(FunctionDeclBits.SClass);
  }

  /// Sets the storage class as written in the source.
  void setStorageClass(StorageClass SClass) {
    FunctionDeclBits.SClass = SClass;
  }

  /// Determine whether the "inline" keyword was specified for this
  /// function.
  bool isInlineSpecified() const { return FunctionDeclBits.IsInlineSpecified; }

  /// Set whether the "inline" keyword was specified for this function.
  void setInlineSpecified(bool I) {
    FunctionDeclBits.IsInlineSpecified = I;
    FunctionDeclBits.IsInline = I;
  }

  /// Determine whether the function was declared in source context
  /// that requires constrained FP intrinsics
  bool UsesFPIntrin() const { return FunctionDeclBits.UsesFPIntrin; }

  /// Set whether the function was declared in source context
  /// that requires constrained FP intrinsics
  void setUsesFPIntrin(bool I) { FunctionDeclBits.UsesFPIntrin = I; }

  /// Flag that this function is implicitly inline.
  void setImplicitlyInline(bool I = true) { FunctionDeclBits.IsInline = I; }

  /// Determine whether this function should be inlined, because it is
  /// either marked "inline" or "constexpr" or is a member function of a class
  /// that was defined in the class body.
  bool isInlined() const { return FunctionDeclBits.IsInline; }

  bool isInlineDefinitionExternallyVisible() const;

  bool isMSExternInline() const;

  bool doesDeclarationForceExternallyVisibleDefinition() const;

  bool isStatic() const { return getStorageClass() == SC_Static; }

  /// Whether this function declaration represents an C++ overloaded
  /// operator, e.g., "operator+".
  bool isOverloadedOperator() const {
    return getOverloadedOperator() != OO_None;
  }

  OverloadedOperatorKind getOverloadedOperator() const;

  const IdentifierInfo *getLiteralIdentifier() const;

  /// If this function is an instantiation of a member function
  /// of a class template specialization, retrieves the function from
  /// which it was instantiated.
  ///
  /// This routine will return non-NULL for (non-templated) member
  /// functions of class templates and for instantiations of function
  /// templates. For example, given:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   void f(T);
  /// };
  /// \endcode
  ///
  /// The declaration for X<int>::f is a (non-templated) FunctionDecl
  /// whose parent is the class template specialization X<int>. For
  /// this declaration, getInstantiatedFromFunction() will return
  /// the FunctionDecl X<T>::A. When a complete definition of
  /// X<int>::A is required, it will be instantiated from the
  /// declaration returned by getInstantiatedFromMemberFunction().
  FunctionDecl *getInstantiatedFromMemberFunction() const;

  /// What kind of templated function this is.
  TemplatedKind getTemplatedKind() const;

  /// If this function is an instantiation of a member function of a
  /// class template specialization, retrieves the member specialization
  /// information.
  MemberSpecializationInfo *getMemberSpecializationInfo() const;

  /// Specify that this record is an instantiation of the
  /// member function FD.
  void setInstantiationOfMemberFunction(FunctionDecl *FD,
                                        TemplateSpecializationKind TSK) {
    setInstantiationOfMemberFunction(getASTContext(), FD, TSK);
  }

  /// Specify that this function declaration was instantiated from a
  /// FunctionDecl FD. This is only used if this is a function declaration
  /// declared locally inside of a function template.
  void setInstantiatedFromDecl(FunctionDecl *FD);

  FunctionDecl *getInstantiatedFromDecl() const;

  /// Retrieves the function template that is described by this
  /// function declaration.
  ///
  /// Every function template is represented as a FunctionTemplateDecl
  /// and a FunctionDecl (or something derived from FunctionDecl). The
  /// former contains template properties (such as the template
  /// parameter lists) while the latter contains the actual
  /// description of the template's
  /// contents. FunctionTemplateDecl::getTemplatedDecl() retrieves the
  /// FunctionDecl that describes the function template,
  /// getDescribedFunctionTemplate() retrieves the
  /// FunctionTemplateDecl from a FunctionDecl.
  FunctionTemplateDecl *getDescribedFunctionTemplate() const;

  void setDescribedFunctionTemplate(FunctionTemplateDecl *Template);

  /// Determine whether this function is a function template
  /// specialization.
  bool isFunctionTemplateSpecialization() const;

  /// If this function is actually a function template specialization,
  /// retrieve information about this function template specialization.
  /// Otherwise, returns NULL.
  FunctionTemplateSpecializationInfo *getTemplateSpecializationInfo() const;

  /// Determines whether this function is a function template
  /// specialization or a member of a class template specialization that can
  /// be implicitly instantiated.
  bool isImplicitlyInstantiable() const;

  /// Determines if the given function was instantiated from a
  /// function template.
  bool isTemplateInstantiation() const;

  /// Retrieve the function declaration from which this function could
  /// be instantiated, if it is an instantiation (rather than a non-template
  /// or a specialization, for example).
  ///
  /// If \p ForDefinition is \c false, explicit specializations will be treated
  /// as if they were implicit instantiations. This will then find the pattern
  /// corresponding to non-definition portions of the declaration, such as
  /// default arguments and the exception specification.
  FunctionDecl *
  getTemplateInstantiationPattern(bool ForDefinition = true) const;

  /// Retrieve the primary template that this function template
  /// specialization either specializes or was instantiated from.
  ///
  /// If this function declaration is not a function template specialization,
  /// returns NULL.
  FunctionTemplateDecl *getPrimaryTemplate() const;

  /// Retrieve the template arguments used to produce this function
  /// template specialization from the primary template.
  ///
  /// If this function declaration is not a function template specialization,
  /// returns NULL.
  const TemplateArgumentList *getTemplateSpecializationArgs() const;

  /// Retrieve the template argument list as written in the sources,
  /// if any.
  ///
  /// If this function declaration is not a function template specialization
  /// or if it had no explicit template argument list, returns NULL.
  /// Note that it an explicit template argument list may be written empty,
  /// e.g., template<> void foo<>(char* s);
  const ASTTemplateArgumentListInfo*
  getTemplateSpecializationArgsAsWritten() const;

  /// Specify that this function declaration is actually a function
  /// template specialization.
  ///
  /// \param Template the function template that this function template
  /// specialization specializes.
  ///
  /// \param TemplateArgs the template arguments that produced this
  /// function template specialization from the template.
  ///
  /// \param InsertPos If non-NULL, the position in the function template
  /// specialization set where the function template specialization data will
  /// be inserted.
  ///
  /// \param TSK the kind of template specialization this is.
  ///
  /// \param TemplateArgsAsWritten location info of template arguments.
  ///
  /// \param PointOfInstantiation point at which the function template
  /// specialization was first instantiated.
  void setFunctionTemplateSpecialization(
      FunctionTemplateDecl *Template, TemplateArgumentList *TemplateArgs,
      void *InsertPos,
      TemplateSpecializationKind TSK = TSK_ImplicitInstantiation,
      TemplateArgumentListInfo *TemplateArgsAsWritten = nullptr,
      SourceLocation PointOfInstantiation = SourceLocation()) {
    setFunctionTemplateSpecialization(getASTContext(), Template, TemplateArgs,
                                      InsertPos, TSK, TemplateArgsAsWritten,
                                      PointOfInstantiation);
  }

  /// Specifies that this function declaration is actually a
  /// dependent function template specialization.
  void setDependentTemplateSpecialization(
      ASTContext &Context, const UnresolvedSetImpl &Templates,
      const TemplateArgumentListInfo *TemplateArgs);

  DependentFunctionTemplateSpecializationInfo *
  getDependentSpecializationInfo() const;

  /// Determine what kind of template instantiation this function
  /// represents.
  TemplateSpecializationKind getTemplateSpecializationKind() const;

  /// Determine the kind of template specialization this function represents
  /// for the purpose of template instantiation.
  TemplateSpecializationKind
  getTemplateSpecializationKindForInstantiation() const;

  /// Determine what kind of template instantiation this function
  /// represents.
  void setTemplateSpecializationKind(TemplateSpecializationKind TSK,
                        SourceLocation PointOfInstantiation = SourceLocation());

  /// Retrieve the (first) point of instantiation of a function template
  /// specialization or a member of a class template specialization.
  ///
  /// \returns the first point of instantiation, if this function was
  /// instantiated from a template; otherwise, returns an invalid source
  /// location.
  SourceLocation getPointOfInstantiation() const;

  /// Determine whether this is or was instantiated from an out-of-line
  /// definition of a member function.
  bool isOutOfLine() const override;

  /// Identify a memory copying or setting function.
  /// If the given function is a memory copy or setting function, returns
  /// the corresponding Builtin ID. If the function is not a memory function,
  /// returns 0.
  unsigned getMemoryFunctionKind() const;

  /// Returns ODRHash of the function.  This value is calculated and
  /// stored on first call, then the stored value returned on the other calls.
  unsigned getODRHash();

  /// Returns cached ODRHash of the function.  This must have been previously
  /// computed and stored.
  unsigned getODRHash() const;

  FunctionEffectsRef getFunctionEffects() const {
    // Effects may differ between declarations, but they should be propagated
    // from old to new on any redeclaration, so it suffices to look at
    // getMostRecentDecl().
    if (const auto *FPT =
            getMostRecentDecl()->getType()->getAs<FunctionProtoType>())
      return FPT->getFunctionEffects();
    return {};
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstFunction && K <= lastFunction;
  }
  static DeclContext *castToDeclContext(const FunctionDecl *D) {
    return static_cast<DeclContext *>(const_cast<FunctionDecl*>(D));
  }
  static FunctionDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<FunctionDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents a member of a struct/union/class.
class FieldDecl : public DeclaratorDecl, public Mergeable<FieldDecl> {
  /// The kinds of value we can store in StorageKind.
  ///
  /// Note that this is compatible with InClassInitStyle except for
  /// ISK_CapturedVLAType.
  enum InitStorageKind {
    /// If the pointer is null, there's nothing special.  Otherwise,
    /// this is a bitfield and the pointer is the Expr* storing the
    /// bit-width.
    ISK_NoInit = (unsigned) ICIS_NoInit,

    /// The pointer is an (optional due to delayed parsing) Expr*
    /// holding the copy-initializer.
    ISK_InClassCopyInit = (unsigned) ICIS_CopyInit,

    /// The pointer is an (optional due to delayed parsing) Expr*
    /// holding the list-initializer.
    ISK_InClassListInit = (unsigned) ICIS_ListInit,

    /// The pointer is a VariableArrayType* that's been captured;
    /// the enclosing context is a lambda or captured statement.
    ISK_CapturedVLAType,
  };

  LLVM_PREFERRED_TYPE(bool)
  unsigned BitField : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Mutable : 1;
  LLVM_PREFERRED_TYPE(InitStorageKind)
  unsigned StorageKind : 2;
  mutable unsigned CachedFieldIndex : 28;

  /// If this is a bitfield with a default member initializer, this
  /// structure is used to represent the two expressions.
  struct InitAndBitWidthStorage {
    LazyDeclStmtPtr Init;
    Expr *BitWidth;
  };

  /// Storage for either the bit-width, the in-class initializer, or
  /// both (via InitAndBitWidth), or the captured variable length array bound.
  ///
  /// If the storage kind is ISK_InClassCopyInit or
  /// ISK_InClassListInit, but the initializer is null, then this
  /// field has an in-class initializer that has not yet been parsed
  /// and attached.
  // FIXME: Tail-allocate this to reduce the size of FieldDecl in the
  // overwhelmingly common case that we have none of these things.
  union {
    // Active member if ISK is not ISK_CapturedVLAType and BitField is false.
    LazyDeclStmtPtr Init;
    // Active member if ISK is ISK_NoInit and BitField is true.
    Expr *BitWidth;
    // Active member if ISK is ISK_InClass*Init and BitField is true.
    InitAndBitWidthStorage *InitAndBitWidth;
    // Active member if ISK is ISK_CapturedVLAType.
    const VariableArrayType *CapturedVLAType;
  };

protected:
  FieldDecl(Kind DK, DeclContext *DC, SourceLocation StartLoc,
            SourceLocation IdLoc, const IdentifierInfo *Id, QualType T,
            TypeSourceInfo *TInfo, Expr *BW, bool Mutable,
            InClassInitStyle InitStyle)
      : DeclaratorDecl(DK, DC, IdLoc, Id, T, TInfo, StartLoc), BitField(false),
        Mutable(Mutable), StorageKind((InitStorageKind)InitStyle),
        CachedFieldIndex(0), Init() {
    if (BW)
      setBitWidth(BW);
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  static FieldDecl *Create(const ASTContext &C, DeclContext *DC,
                           SourceLocation StartLoc, SourceLocation IdLoc,
                           const IdentifierInfo *Id, QualType T,
                           TypeSourceInfo *TInfo, Expr *BW, bool Mutable,
                           InClassInitStyle InitStyle);

  static FieldDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  /// Returns the index of this field within its record,
  /// as appropriate for passing to ASTRecordLayout::getFieldOffset.
  unsigned getFieldIndex() const;

  /// Determines whether this field is mutable (C++ only).
  bool isMutable() const { return Mutable; }

  /// Determines whether this field is a bitfield.
  bool isBitField() const { return BitField; }

  /// Determines whether this is an unnamed bitfield.
  bool isUnnamedBitField() const { return isBitField() && !getDeclName(); }

  /// Determines whether this field is a
  /// representative for an anonymous struct or union. Such fields are
  /// unnamed and are implicitly generated by the implementation to
  /// store the data for the anonymous union or struct.
  bool isAnonymousStructOrUnion() const;

  /// Returns the expression that represents the bit width, if this field
  /// is a bit field. For non-bitfields, this returns \c nullptr.
  Expr *getBitWidth() const {
    if (!BitField)
      return nullptr;
    return hasInClassInitializer() ? InitAndBitWidth->BitWidth : BitWidth;
  }

  /// Computes the bit width of this field, if this is a bit field.
  /// May not be called on non-bitfields.
  unsigned getBitWidthValue(const ASTContext &Ctx) const;

  /// Set the bit-field width for this member.
  // Note: used by some clients (i.e., do not remove it).
  void setBitWidth(Expr *Width) {
    assert(!hasCapturedVLAType() && !BitField &&
           "bit width or captured type already set");
    assert(Width && "no bit width specified");
    if (hasInClassInitializer())
      InitAndBitWidth =
          new (getASTContext()) InitAndBitWidthStorage{Init, Width};
    else
      BitWidth = Width;
    BitField = true;
  }

  /// Remove the bit-field width from this member.
  // Note: used by some clients (i.e., do not remove it).
  void removeBitWidth() {
    assert(isBitField() && "no bitfield width to remove");
    if (hasInClassInitializer()) {
      // Read the old initializer before we change the active union member.
      auto ExistingInit = InitAndBitWidth->Init;
      Init = ExistingInit;
    }
    BitField = false;
  }

  /// Is this a zero-length bit-field? Such bit-fields aren't really bit-fields
  /// at all and instead act as a separator between contiguous runs of other
  /// bit-fields.
  bool isZeroLengthBitField(const ASTContext &Ctx) const;

  /// Determine if this field is a subobject of zero size, that is, either a
  /// zero-length bit-field or a field of empty class type with the
  /// [[no_unique_address]] attribute.
  bool isZeroSize(const ASTContext &Ctx) const;

  /// Determine if this field is of potentially-overlapping class type, that
  /// is, subobject with the [[no_unique_address]] attribute
  bool isPotentiallyOverlapping() const;

  /// Get the kind of (C++11) default member initializer that this field has.
  InClassInitStyle getInClassInitStyle() const {
    return (StorageKind == ISK_CapturedVLAType ? ICIS_NoInit
                                               : (InClassInitStyle)StorageKind);
  }

  /// Determine whether this member has a C++11 default member initializer.
  bool hasInClassInitializer() const {
    return getInClassInitStyle() != ICIS_NoInit;
  }

  /// Determine whether getInClassInitializer() would return a non-null pointer
  /// without deserializing the initializer.
  bool hasNonNullInClassInitializer() const {
    return hasInClassInitializer() && (BitField ? InitAndBitWidth->Init : Init);
  }

  /// Get the C++11 default member initializer for this member, or null if one
  /// has not been set. If a valid declaration has a default member initializer,
  /// but this returns null, then we have not parsed and attached it yet.
  Expr *getInClassInitializer() const;

  /// Set the C++11 in-class initializer for this member.
  void setInClassInitializer(Expr *NewInit);

private:
  void setLazyInClassInitializer(LazyDeclStmtPtr NewInit);

public:
  /// Remove the C++11 in-class initializer from this member.
  void removeInClassInitializer() {
    assert(hasInClassInitializer() && "no initializer to remove");
    StorageKind = ISK_NoInit;
    if (BitField) {
      // Read the bit width before we change the active union member.
      Expr *ExistingBitWidth = InitAndBitWidth->BitWidth;
      BitWidth = ExistingBitWidth;
    }
  }

  /// Determine whether this member captures the variable length array
  /// type.
  bool hasCapturedVLAType() const {
    return StorageKind == ISK_CapturedVLAType;
  }

  /// Get the captured variable length array type.
  const VariableArrayType *getCapturedVLAType() const {
    return hasCapturedVLAType() ? CapturedVLAType : nullptr;
  }

  /// Set the captured variable length array type for this field.
  void setCapturedVLAType(const VariableArrayType *VLAType);

  /// Returns the parent of this field declaration, which
  /// is the struct in which this field is defined.
  ///
  /// Returns null if this is not a normal class/struct field declaration, e.g.
  /// ObjCAtDefsFieldDecl, ObjCIvarDecl.
  const RecordDecl *getParent() const {
    return dyn_cast<RecordDecl>(getDeclContext());
  }

  RecordDecl *getParent() {
    return dyn_cast<RecordDecl>(getDeclContext());
  }

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Retrieves the canonical declaration of this field.
  FieldDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const FieldDecl *getCanonicalDecl() const { return getFirstDecl(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstField && K <= lastField; }

  void printName(raw_ostream &OS, const PrintingPolicy &Policy) const override;
};

/// An instance of this object exists for each enum constant
/// that is defined.  For example, in "enum X {a,b}", each of a/b are
/// EnumConstantDecl's, X is an instance of EnumDecl, and the type of a/b is a
/// TagType for the X EnumDecl.
class EnumConstantDecl : public ValueDecl,
                         public Mergeable<EnumConstantDecl>,
                         public APIntStorage {
  Stmt *Init; // an integer constant expression
  bool IsUnsigned;

protected:
  EnumConstantDecl(const ASTContext &C, DeclContext *DC, SourceLocation L,
                   IdentifierInfo *Id, QualType T, Expr *E,
                   const llvm::APSInt &V);

public:
  friend class StmtIteratorBase;

  static EnumConstantDecl *Create(ASTContext &C, EnumDecl *DC,
                                  SourceLocation L, IdentifierInfo *Id,
                                  QualType T, Expr *E,
                                  const llvm::APSInt &V);
  static EnumConstantDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  const Expr *getInitExpr() const { return (const Expr*) Init; }
  Expr *getInitExpr() { return (Expr*) Init; }
  llvm::APSInt getInitVal() const {
    return llvm::APSInt(getValue(), IsUnsigned);
  }

  void setInitExpr(Expr *E) { Init = (Stmt*) E; }
  void setInitVal(const ASTContext &C, const llvm::APSInt &V) {
    setValue(C, V);
    IsUnsigned = V.isUnsigned();
  }

  SourceRange getSourceRange() const override LLVM_READONLY;

  /// Retrieves the canonical declaration of this enumerator.
  EnumConstantDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const EnumConstantDecl *getCanonicalDecl() const { return getFirstDecl(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == EnumConstant; }
};

/// Represents a field injected from an anonymous union/struct into the parent
/// scope. These are always implicit.
class IndirectFieldDecl : public ValueDecl,
                          public Mergeable<IndirectFieldDecl> {
  NamedDecl **Chaining;
  unsigned ChainingSize;

  IndirectFieldDecl(ASTContext &C, DeclContext *DC, SourceLocation L,
                    DeclarationName N, QualType T,
                    MutableArrayRef<NamedDecl *> CH);

  void anchor() override;

public:
  friend class ASTDeclReader;

  static IndirectFieldDecl *Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation L, const IdentifierInfo *Id,
                                   QualType T,
                                   llvm::MutableArrayRef<NamedDecl *> CH);

  static IndirectFieldDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  using chain_iterator = ArrayRef<NamedDecl *>::const_iterator;

  ArrayRef<NamedDecl *> chain() const {
    return llvm::ArrayRef(Chaining, ChainingSize);
  }
  chain_iterator chain_begin() const { return chain().begin(); }
  chain_iterator chain_end() const { return chain().end(); }

  unsigned getChainingSize() const { return ChainingSize; }

  FieldDecl *getAnonField() const {
    assert(chain().size() >= 2);
    return cast<FieldDecl>(chain().back());
  }

  VarDecl *getVarDecl() const {
    assert(chain().size() >= 2);
    return dyn_cast<VarDecl>(chain().front());
  }

  IndirectFieldDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const IndirectFieldDecl *getCanonicalDecl() const { return getFirstDecl(); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == IndirectField; }
};

/// Represents a declaration of a type.
class TypeDecl : public NamedDecl {
  friend class ASTContext;

  /// This indicates the Type object that represents
  /// this TypeDecl.  It is a cache maintained by
  /// ASTContext::getTypedefType, ASTContext::getTagDeclType, and
  /// ASTContext::getTemplateTypeParmType, and TemplateTypeParmDecl.
  mutable const Type *TypeForDecl = nullptr;

  /// The start of the source range for this declaration.
  SourceLocation LocStart;

  void anchor() override;

protected:
  TypeDecl(Kind DK, DeclContext *DC, SourceLocation L, const IdentifierInfo *Id,
           SourceLocation StartL = SourceLocation())
      : NamedDecl(DK, DC, L, Id), LocStart(StartL) {}

public:
  // Low-level accessor. If you just want the type defined by this node,
  // check out ASTContext::getTypeDeclType or one of
  // ASTContext::getTypedefType, ASTContext::getRecordType, etc. if you
  // already know the specific kind of node this is.
  const Type *getTypeForDecl() const { return TypeForDecl; }
  void setTypeForDecl(const Type *TD) { TypeForDecl = TD; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LocStart; }
  void setLocStart(SourceLocation L) { LocStart = L; }
  SourceRange getSourceRange() const override LLVM_READONLY {
    if (LocStart.isValid())
      return SourceRange(LocStart, getLocation());
    else
      return SourceRange(getLocation());
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstType && K <= lastType; }
};

/// Base class for declarations which introduce a typedef-name.
class TypedefNameDecl : public TypeDecl, public Redeclarable<TypedefNameDecl> {
  struct alignas(8) ModedTInfo {
    TypeSourceInfo *first;
    QualType second;
  };

  /// If int part is 0, we have not computed IsTransparentTag.
  /// Otherwise, IsTransparentTag is (getInt() >> 1).
  mutable llvm::PointerIntPair<
      llvm::PointerUnion<TypeSourceInfo *, ModedTInfo *>, 2>
      MaybeModedTInfo;

  void anchor() override;

protected:
  TypedefNameDecl(Kind DK, ASTContext &C, DeclContext *DC,
                  SourceLocation StartLoc, SourceLocation IdLoc,
                  const IdentifierInfo *Id, TypeSourceInfo *TInfo)
      : TypeDecl(DK, DC, IdLoc, Id, StartLoc), redeclarable_base(C),
        MaybeModedTInfo(TInfo, 0) {}

  using redeclarable_base = Redeclarable<TypedefNameDecl>;

  TypedefNameDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  TypedefNameDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  TypedefNameDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

public:
  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  bool isModed() const {
    return MaybeModedTInfo.getPointer().is<ModedTInfo *>();
  }

  TypeSourceInfo *getTypeSourceInfo() const {
    return isModed() ? MaybeModedTInfo.getPointer().get<ModedTInfo *>()->first
                     : MaybeModedTInfo.getPointer().get<TypeSourceInfo *>();
  }

  QualType getUnderlyingType() const {
    return isModed() ? MaybeModedTInfo.getPointer().get<ModedTInfo *>()->second
                     : MaybeModedTInfo.getPointer()
                           .get<TypeSourceInfo *>()
                           ->getType();
  }

  void setTypeSourceInfo(TypeSourceInfo *newType) {
    MaybeModedTInfo.setPointer(newType);
  }

  void setModedTypeSourceInfo(TypeSourceInfo *unmodedTSI, QualType modedTy) {
    MaybeModedTInfo.setPointer(new (getASTContext(), 8)
                                   ModedTInfo({unmodedTSI, modedTy}));
  }

  /// Retrieves the canonical declaration of this typedef-name.
  TypedefNameDecl *getCanonicalDecl() override { return getFirstDecl(); }
  const TypedefNameDecl *getCanonicalDecl() const { return getFirstDecl(); }

  /// Retrieves the tag declaration for which this is the typedef name for
  /// linkage purposes, if any.
  ///
  /// \param AnyRedecl Look for the tag declaration in any redeclaration of
  /// this typedef declaration.
  TagDecl *getAnonDeclWithTypedefName(bool AnyRedecl = false) const;

  /// Determines if this typedef shares a name and spelling location with its
  /// underlying tag type, as is the case with the NS_ENUM macro.
  bool isTransparentTag() const {
    if (MaybeModedTInfo.getInt())
      return MaybeModedTInfo.getInt() & 0x2;
    return isTransparentTagSlow();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstTypedefName && K <= lastTypedefName;
  }

private:
  bool isTransparentTagSlow() const;
};

/// Represents the declaration of a typedef-name via the 'typedef'
/// type specifier.
class TypedefDecl : public TypedefNameDecl {
  TypedefDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
              SourceLocation IdLoc, const IdentifierInfo *Id,
              TypeSourceInfo *TInfo)
      : TypedefNameDecl(Typedef, C, DC, StartLoc, IdLoc, Id, TInfo) {}

public:
  static TypedefDecl *Create(ASTContext &C, DeclContext *DC,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             const IdentifierInfo *Id, TypeSourceInfo *TInfo);
  static TypedefDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Typedef; }
};

/// Represents the declaration of a typedef-name via a C++11
/// alias-declaration.
class TypeAliasDecl : public TypedefNameDecl {
  /// The template for which this is the pattern, if any.
  TypeAliasTemplateDecl *Template;

  TypeAliasDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
                SourceLocation IdLoc, const IdentifierInfo *Id,
                TypeSourceInfo *TInfo)
      : TypedefNameDecl(TypeAlias, C, DC, StartLoc, IdLoc, Id, TInfo),
        Template(nullptr) {}

public:
  static TypeAliasDecl *Create(ASTContext &C, DeclContext *DC,
                               SourceLocation StartLoc, SourceLocation IdLoc,
                               const IdentifierInfo *Id, TypeSourceInfo *TInfo);
  static TypeAliasDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  TypeAliasTemplateDecl *getDescribedAliasTemplate() const { return Template; }
  void setDescribedAliasTemplate(TypeAliasTemplateDecl *TAT) { Template = TAT; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TypeAlias; }
};

/// Represents the declaration of a struct/union/class/enum.
class TagDecl : public TypeDecl,
                public DeclContext,
                public Redeclarable<TagDecl> {
  // This class stores some data in DeclContext::TagDeclBits
  // to save some space. Use the provided accessors to access it.
public:
  // This is really ugly.
  using TagKind = TagTypeKind;

private:
  SourceRange BraceRange;

  // A struct representing syntactic qualifier info,
  // to be used for the (uncommon) case of out-of-line declarations.
  using ExtInfo = QualifierInfo;

  /// If the (out-of-line) tag declaration name
  /// is qualified, it points to the qualifier info (nns and range);
  /// otherwise, if the tag declaration is anonymous and it is part of
  /// a typedef or alias, it points to the TypedefNameDecl (used for mangling);
  /// otherwise, if the tag declaration is anonymous and it is used as a
  /// declaration specifier for variables, it points to the first VarDecl (used
  /// for mangling);
  /// otherwise, it is a null (TypedefNameDecl) pointer.
  llvm::PointerUnion<TypedefNameDecl *, ExtInfo *> TypedefNameDeclOrQualifier;

  bool hasExtInfo() const { return TypedefNameDeclOrQualifier.is<ExtInfo *>(); }
  ExtInfo *getExtInfo() { return TypedefNameDeclOrQualifier.get<ExtInfo *>(); }
  const ExtInfo *getExtInfo() const {
    return TypedefNameDeclOrQualifier.get<ExtInfo *>();
  }

protected:
  TagDecl(Kind DK, TagKind TK, const ASTContext &C, DeclContext *DC,
          SourceLocation L, IdentifierInfo *Id, TagDecl *PrevDecl,
          SourceLocation StartL);

  using redeclarable_base = Redeclarable<TagDecl>;

  TagDecl *getNextRedeclarationImpl() override {
    return getNextRedeclaration();
  }

  TagDecl *getPreviousDeclImpl() override {
    return getPreviousDecl();
  }

  TagDecl *getMostRecentDeclImpl() override {
    return getMostRecentDecl();
  }

  /// Completes the definition of this tag declaration.
  ///
  /// This is a helper function for derived classes.
  void completeDefinition();

  /// True if this decl is currently being defined.
  void setBeingDefined(bool V = true) { TagDeclBits.IsBeingDefined = V; }

  /// Indicates whether it is possible for declarations of this kind
  /// to have an out-of-date definition.
  ///
  /// This option is only enabled when modules are enabled.
  void setMayHaveOutOfDateDef(bool V = true) {
    TagDeclBits.MayHaveOutOfDateDef = V;
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  using redecl_range = redeclarable_base::redecl_range;
  using redecl_iterator = redeclarable_base::redecl_iterator;

  using redeclarable_base::redecls_begin;
  using redeclarable_base::redecls_end;
  using redeclarable_base::redecls;
  using redeclarable_base::getPreviousDecl;
  using redeclarable_base::getMostRecentDecl;
  using redeclarable_base::isFirstDecl;

  SourceRange getBraceRange() const { return BraceRange; }
  void setBraceRange(SourceRange R) { BraceRange = R; }

  /// Return SourceLocation representing start of source
  /// range ignoring outer template declarations.
  SourceLocation getInnerLocStart() const { return getBeginLoc(); }

  /// Return SourceLocation representing start of source
  /// range taking into account any outer template declarations.
  SourceLocation getOuterLocStart() const;
  SourceRange getSourceRange() const override LLVM_READONLY;

  TagDecl *getCanonicalDecl() override;
  const TagDecl *getCanonicalDecl() const {
    return const_cast<TagDecl*>(this)->getCanonicalDecl();
  }

  /// Return true if this declaration is a completion definition of the type.
  /// Provided for consistency.
  bool isThisDeclarationADefinition() const {
    return isCompleteDefinition();
  }

  /// Return true if this decl has its body fully specified.
  bool isCompleteDefinition() const { return TagDeclBits.IsCompleteDefinition; }

  /// True if this decl has its body fully specified.
  void setCompleteDefinition(bool V = true) {
    TagDeclBits.IsCompleteDefinition = V;
  }

  /// Return true if this complete decl is
  /// required to be complete for some existing use.
  bool isCompleteDefinitionRequired() const {
    return TagDeclBits.IsCompleteDefinitionRequired;
  }

  /// True if this complete decl is
  /// required to be complete for some existing use.
  void setCompleteDefinitionRequired(bool V = true) {
    TagDeclBits.IsCompleteDefinitionRequired = V;
  }

  /// Return true if this decl is currently being defined.
  bool isBeingDefined() const { return TagDeclBits.IsBeingDefined; }

  /// True if this tag declaration is "embedded" (i.e., defined or declared
  /// for the very first time) in the syntax of a declarator.
  bool isEmbeddedInDeclarator() const {
    return TagDeclBits.IsEmbeddedInDeclarator;
  }

  /// True if this tag declaration is "embedded" (i.e., defined or declared
  /// for the very first time) in the syntax of a declarator.
  void setEmbeddedInDeclarator(bool isInDeclarator) {
    TagDeclBits.IsEmbeddedInDeclarator = isInDeclarator;
  }

  /// True if this tag is free standing, e.g. "struct foo;".
  bool isFreeStanding() const { return TagDeclBits.IsFreeStanding; }

  /// True if this tag is free standing, e.g. "struct foo;".
  void setFreeStanding(bool isFreeStanding = true) {
    TagDeclBits.IsFreeStanding = isFreeStanding;
  }

  /// Indicates whether it is possible for declarations of this kind
  /// to have an out-of-date definition.
  ///
  /// This option is only enabled when modules are enabled.
  bool mayHaveOutOfDateDef() const { return TagDeclBits.MayHaveOutOfDateDef; }

  /// Whether this declaration declares a type that is
  /// dependent, i.e., a type that somehow depends on template
  /// parameters.
  bool isDependentType() const { return isDependentContext(); }

  /// Whether this declaration was a definition in some module but was forced
  /// to be a declaration.
  ///
  /// Useful for clients checking if a module has a definition of a specific
  /// symbol and not interested in the final AST with deduplicated definitions.
  bool isThisDeclarationADemotedDefinition() const {
    return TagDeclBits.IsThisDeclarationADemotedDefinition;
  }

  /// Mark a definition as a declaration and maintain information it _was_
  /// a definition.
  void demoteThisDefinitionToDeclaration() {
    assert(isCompleteDefinition() &&
           "Should demote definitions only, not forward declarations");
    setCompleteDefinition(false);
    TagDeclBits.IsThisDeclarationADemotedDefinition = true;
  }

  /// Starts the definition of this tag declaration.
  ///
  /// This method should be invoked at the beginning of the definition
  /// of this tag declaration. It will set the tag type into a state
  /// where it is in the process of being defined.
  void startDefinition();

  /// Returns the TagDecl that actually defines this
  ///  struct/union/class/enum.  When determining whether or not a
  ///  struct/union/class/enum has a definition, one should use this
  ///  method as opposed to 'isDefinition'.  'isDefinition' indicates
  ///  whether or not a specific TagDecl is defining declaration, not
  ///  whether or not the struct/union/class/enum type is defined.
  ///  This method returns NULL if there is no TagDecl that defines
  ///  the struct/union/class/enum.
  TagDecl *getDefinition() const;

  StringRef getKindName() const {
    return TypeWithKeyword::getTagTypeKindName(getTagKind());
  }

  TagKind getTagKind() const {
    return static_cast<TagKind>(TagDeclBits.TagDeclKind);
  }

  void setTagKind(TagKind TK) {
    TagDeclBits.TagDeclKind = llvm::to_underlying(TK);
  }

  bool isStruct() const { return getTagKind() == TagTypeKind::Struct; }
  bool isInterface() const { return getTagKind() == TagTypeKind::Interface; }
  bool isClass() const { return getTagKind() == TagTypeKind::Class; }
  bool isUnion() const { return getTagKind() == TagTypeKind::Union; }
  bool isEnum() const { return getTagKind() == TagTypeKind::Enum; }

  /// Is this tag type named, either directly or via being defined in
  /// a typedef of this type?
  ///
  /// C++11 [basic.link]p8:
  ///   A type is said to have linkage if and only if:
  ///     - it is a class or enumeration type that is named (or has a
  ///       name for linkage purposes) and the name has linkage; ...
  /// C++11 [dcl.typedef]p9:
  ///   If the typedef declaration defines an unnamed class (or enum),
  ///   the first typedef-name declared by the declaration to be that
  ///   class type (or enum type) is used to denote the class type (or
  ///   enum type) for linkage purposes only.
  ///
  /// C does not have an analogous rule, but the same concept is
  /// nonetheless useful in some places.
  bool hasNameForLinkage() const {
    return (getDeclName() || getTypedefNameForAnonDecl());
  }

  TypedefNameDecl *getTypedefNameForAnonDecl() const {
    return hasExtInfo() ? nullptr
                        : TypedefNameDeclOrQualifier.get<TypedefNameDecl *>();
  }

  void setTypedefNameForAnonDecl(TypedefNameDecl *TDD);

  /// Retrieve the nested-name-specifier that qualifies the name of this
  /// declaration, if it was present in the source.
  NestedNameSpecifier *getQualifier() const {
    return hasExtInfo() ? getExtInfo()->QualifierLoc.getNestedNameSpecifier()
                        : nullptr;
  }

  /// Retrieve the nested-name-specifier (with source-location
  /// information) that qualifies the name of this declaration, if it was
  /// present in the source.
  NestedNameSpecifierLoc getQualifierLoc() const {
    return hasExtInfo() ? getExtInfo()->QualifierLoc
                        : NestedNameSpecifierLoc();
  }

  void setQualifierInfo(NestedNameSpecifierLoc QualifierLoc);

  unsigned getNumTemplateParameterLists() const {
    return hasExtInfo() ? getExtInfo()->NumTemplParamLists : 0;
  }

  TemplateParameterList *getTemplateParameterList(unsigned i) const {
    assert(i < getNumTemplateParameterLists());
    return getExtInfo()->TemplParamLists[i];
  }

  using TypeDecl::printName;
  void printName(raw_ostream &OS, const PrintingPolicy &Policy) const override;

  void setTemplateParameterListsInfo(ASTContext &Context,
                                     ArrayRef<TemplateParameterList *> TPLists);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K >= firstTag && K <= lastTag; }

  static DeclContext *castToDeclContext(const TagDecl *D) {
    return static_cast<DeclContext *>(const_cast<TagDecl*>(D));
  }

  static TagDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<TagDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents an enum.  In C++11, enums can be forward-declared
/// with a fixed underlying type, and in C we allow them to be forward-declared
/// with no underlying type as an extension.
class EnumDecl : public TagDecl {
  // This class stores some data in DeclContext::EnumDeclBits
  // to save some space. Use the provided accessors to access it.

  /// This represent the integer type that the enum corresponds
  /// to for code generation purposes.  Note that the enumerator constants may
  /// have a different type than this does.
  ///
  /// If the underlying integer type was explicitly stated in the source
  /// code, this is a TypeSourceInfo* for that type. Otherwise this type
  /// was automatically deduced somehow, and this is a Type*.
  ///
  /// Normally if IsFixed(), this would contain a TypeSourceInfo*, but in
  /// some cases it won't.
  ///
  /// The underlying type of an enumeration never has any qualifiers, so
  /// we can get away with just storing a raw Type*, and thus save an
  /// extra pointer when TypeSourceInfo is needed.
  llvm::PointerUnion<const Type *, TypeSourceInfo *> IntegerType;

  /// The integer type that values of this type should
  /// promote to.  In C, enumerators are generally of an integer type
  /// directly, but gcc-style large enumerators (and all enumerators
  /// in C++) are of the enum type instead.
  QualType PromotionType;

  /// If this enumeration is an instantiation of a member enumeration
  /// of a class template specialization, this is the member specialization
  /// information.
  MemberSpecializationInfo *SpecializationInfo = nullptr;

  /// Store the ODRHash after first calculation.
  /// The corresponding flag HasODRHash is in EnumDeclBits
  /// and can be accessed with the provided accessors.
  unsigned ODRHash;

  EnumDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
           SourceLocation IdLoc, IdentifierInfo *Id, EnumDecl *PrevDecl,
           bool Scoped, bool ScopedUsingClassTag, bool Fixed);

  void anchor() override;

  void setInstantiationOfMemberEnum(ASTContext &C, EnumDecl *ED,
                                    TemplateSpecializationKind TSK);

  /// Sets the width in bits required to store all the
  /// non-negative enumerators of this enum.
  void setNumPositiveBits(unsigned Num) {
    EnumDeclBits.NumPositiveBits = Num;
    assert(EnumDeclBits.NumPositiveBits == Num && "can't store this bitcount");
  }

  /// Returns the width in bits required to store all the
  /// negative enumerators of this enum. (see getNumNegativeBits)
  void setNumNegativeBits(unsigned Num) { EnumDeclBits.NumNegativeBits = Num; }

public:
  /// True if this tag declaration is a scoped enumeration. Only
  /// possible in C++11 mode.
  void setScoped(bool Scoped = true) { EnumDeclBits.IsScoped = Scoped; }

  /// If this tag declaration is a scoped enum,
  /// then this is true if the scoped enum was declared using the class
  /// tag, false if it was declared with the struct tag. No meaning is
  /// associated if this tag declaration is not a scoped enum.
  void setScopedUsingClassTag(bool ScopedUCT = true) {
    EnumDeclBits.IsScopedUsingClassTag = ScopedUCT;
  }

  /// True if this is an Objective-C, C++11, or
  /// Microsoft-style enumeration with a fixed underlying type.
  void setFixed(bool Fixed = true) { EnumDeclBits.IsFixed = Fixed; }

private:
  /// True if a valid hash is stored in ODRHash.
  bool hasODRHash() const { return EnumDeclBits.HasODRHash; }
  void setHasODRHash(bool Hash = true) { EnumDeclBits.HasODRHash = Hash; }

public:
  friend class ASTDeclReader;

  EnumDecl *getCanonicalDecl() override {
    return cast<EnumDecl>(TagDecl::getCanonicalDecl());
  }
  const EnumDecl *getCanonicalDecl() const {
    return const_cast<EnumDecl*>(this)->getCanonicalDecl();
  }

  EnumDecl *getPreviousDecl() {
    return cast_or_null<EnumDecl>(
            static_cast<TagDecl *>(this)->getPreviousDecl());
  }
  const EnumDecl *getPreviousDecl() const {
    return const_cast<EnumDecl*>(this)->getPreviousDecl();
  }

  EnumDecl *getMostRecentDecl() {
    return cast<EnumDecl>(static_cast<TagDecl *>(this)->getMostRecentDecl());
  }
  const EnumDecl *getMostRecentDecl() const {
    return const_cast<EnumDecl*>(this)->getMostRecentDecl();
  }

  EnumDecl *getDefinition() const {
    return cast_or_null<EnumDecl>(TagDecl::getDefinition());
  }

  static EnumDecl *Create(ASTContext &C, DeclContext *DC,
                          SourceLocation StartLoc, SourceLocation IdLoc,
                          IdentifierInfo *Id, EnumDecl *PrevDecl,
                          bool IsScoped, bool IsScopedUsingClassTag,
                          bool IsFixed);
  static EnumDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  /// Overrides to provide correct range when there's an enum-base specifier
  /// with forward declarations.
  SourceRange getSourceRange() const override LLVM_READONLY;

  /// When created, the EnumDecl corresponds to a
  /// forward-declared enum. This method is used to mark the
  /// declaration as being defined; its enumerators have already been
  /// added (via DeclContext::addDecl). NewType is the new underlying
  /// type of the enumeration type.
  void completeDefinition(QualType NewType,
                          QualType PromotionType,
                          unsigned NumPositiveBits,
                          unsigned NumNegativeBits);

  // Iterates through the enumerators of this enumeration.
  using enumerator_iterator = specific_decl_iterator<EnumConstantDecl>;
  using enumerator_range =
      llvm::iterator_range<specific_decl_iterator<EnumConstantDecl>>;

  enumerator_range enumerators() const {
    return enumerator_range(enumerator_begin(), enumerator_end());
  }

  enumerator_iterator enumerator_begin() const {
    const EnumDecl *E = getDefinition();
    if (!E)
      E = this;
    return enumerator_iterator(E->decls_begin());
  }

  enumerator_iterator enumerator_end() const {
    const EnumDecl *E = getDefinition();
    if (!E)
      E = this;
    return enumerator_iterator(E->decls_end());
  }

  /// Return the integer type that enumerators should promote to.
  QualType getPromotionType() const { return PromotionType; }

  /// Set the promotion type.
  void setPromotionType(QualType T) { PromotionType = T; }

  /// Return the integer type this enum decl corresponds to.
  /// This returns a null QualType for an enum forward definition with no fixed
  /// underlying type.
  QualType getIntegerType() const {
    if (!IntegerType)
      return QualType();
    if (const Type *T = IntegerType.dyn_cast<const Type*>())
      return QualType(T, 0);
    return IntegerType.get<TypeSourceInfo*>()->getType().getUnqualifiedType();
  }

  /// Set the underlying integer type.
  void setIntegerType(QualType T) { IntegerType = T.getTypePtrOrNull(); }

  /// Set the underlying integer type source info.
  void setIntegerTypeSourceInfo(TypeSourceInfo *TInfo) { IntegerType = TInfo; }

  /// Return the type source info for the underlying integer type,
  /// if no type source info exists, return 0.
  TypeSourceInfo *getIntegerTypeSourceInfo() const {
    return IntegerType.dyn_cast<TypeSourceInfo*>();
  }

  /// Retrieve the source range that covers the underlying type if
  /// specified.
  SourceRange getIntegerTypeRange() const LLVM_READONLY;

  /// Returns the width in bits required to store all the
  /// non-negative enumerators of this enum.
  unsigned getNumPositiveBits() const { return EnumDeclBits.NumPositiveBits; }

  /// Returns the width in bits required to store all the
  /// negative enumerators of this enum.  These widths include
  /// the rightmost leading 1;  that is:
  ///
  /// MOST NEGATIVE ENUMERATOR     PATTERN     NUM NEGATIVE BITS
  /// ------------------------     -------     -----------------
  ///                       -1     1111111                     1
  ///                      -10     1110110                     5
  ///                     -101     1001011                     8
  unsigned getNumNegativeBits() const { return EnumDeclBits.NumNegativeBits; }

  /// Calculates the [Min,Max) values the enum can store based on the
  /// NumPositiveBits and NumNegativeBits. This matters for enums that do not
  /// have a fixed underlying type.
  void getValueRange(llvm::APInt &Max, llvm::APInt &Min) const;

  /// Returns true if this is a C++11 scoped enumeration.
  bool isScoped() const { return EnumDeclBits.IsScoped; }

  /// Returns true if this is a C++11 scoped enumeration.
  bool isScopedUsingClassTag() const {
    return EnumDeclBits.IsScopedUsingClassTag;
  }

  /// Returns true if this is an Objective-C, C++11, or
  /// Microsoft-style enumeration with a fixed underlying type.
  bool isFixed() const { return EnumDeclBits.IsFixed; }

  unsigned getODRHash();

  /// Returns true if this can be considered a complete type.
  bool isComplete() const {
    // IntegerType is set for fixed type enums and non-fixed but implicitly
    // int-sized Microsoft enums.
    return isCompleteDefinition() || IntegerType;
  }

  /// Returns true if this enum is either annotated with
  /// enum_extensibility(closed) or isn't annotated with enum_extensibility.
  bool isClosed() const;

  /// Returns true if this enum is annotated with flag_enum and isn't annotated
  /// with enum_extensibility(open).
  bool isClosedFlag() const;

  /// Returns true if this enum is annotated with neither flag_enum nor
  /// enum_extensibility(open).
  bool isClosedNonFlag() const;

  /// Retrieve the enum definition from which this enumeration could
  /// be instantiated, if it is an instantiation (rather than a non-template).
  EnumDecl *getTemplateInstantiationPattern() const;

  /// Returns the enumeration (declared within the template)
  /// from which this enumeration type was instantiated, or NULL if
  /// this enumeration was not instantiated from any template.
  EnumDecl *getInstantiatedFromMemberEnum() const;

  /// If this enumeration is a member of a specialization of a
  /// templated class, determine what kind of template specialization
  /// or instantiation this is.
  TemplateSpecializationKind getTemplateSpecializationKind() const;

  /// For an enumeration member that was instantiated from a member
  /// enumeration of a templated class, set the template specialiation kind.
  void setTemplateSpecializationKind(TemplateSpecializationKind TSK,
                        SourceLocation PointOfInstantiation = SourceLocation());

  /// If this enumeration is an instantiation of a member enumeration of
  /// a class template specialization, retrieves the member specialization
  /// information.
  MemberSpecializationInfo *getMemberSpecializationInfo() const {
    return SpecializationInfo;
  }

  /// Specify that this enumeration is an instantiation of the
  /// member enumeration ED.
  void setInstantiationOfMemberEnum(EnumDecl *ED,
                                    TemplateSpecializationKind TSK) {
    setInstantiationOfMemberEnum(getASTContext(), ED, TSK);
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Enum; }
};

/// Enum that represents the different ways arguments are passed to and
/// returned from function calls. This takes into account the target-specific
/// and version-specific rules along with the rules determined by the
/// language.
enum class RecordArgPassingKind {
  /// The argument of this type can be passed directly in registers.
  CanPassInRegs,

  /// The argument of this type cannot be passed directly in registers.
  /// Records containing this type as a subobject are not forced to be passed
  /// indirectly. This value is used only in C++. This value is required by
  /// C++ because, in uncommon situations, it is possible for a class to have
  /// only trivial copy/move constructors even when one of its subobjects has
  /// a non-trivial copy/move constructor (if e.g. the corresponding copy/move
  /// constructor in the derived class is deleted).
  CannotPassInRegs,

  /// The argument of this type cannot be passed directly in registers.
  /// Records containing this type as a subobject are forced to be passed
  /// indirectly.
  CanNeverPassInRegs
};

/// Represents a struct/union/class.  For example:
///   struct X;                  // Forward declaration, no "body".
///   union Y { int A, B; };     // Has body with members A and B (FieldDecls).
/// This decl will be marked invalid if *any* members are invalid.
class RecordDecl : public TagDecl {
  // This class stores some data in DeclContext::RecordDeclBits
  // to save some space. Use the provided accessors to access it.
public:
  friend class DeclContext;
  friend class ASTDeclReader;

protected:
  RecordDecl(Kind DK, TagKind TK, const ASTContext &C, DeclContext *DC,
             SourceLocation StartLoc, SourceLocation IdLoc,
             IdentifierInfo *Id, RecordDecl *PrevDecl);

public:
  static RecordDecl *Create(const ASTContext &C, TagKind TK, DeclContext *DC,
                            SourceLocation StartLoc, SourceLocation IdLoc,
                            IdentifierInfo *Id, RecordDecl* PrevDecl = nullptr);
  static RecordDecl *CreateDeserialized(const ASTContext &C, GlobalDeclID ID);

  RecordDecl *getPreviousDecl() {
    return cast_or_null<RecordDecl>(
            static_cast<TagDecl *>(this)->getPreviousDecl());
  }
  const RecordDecl *getPreviousDecl() const {
    return const_cast<RecordDecl*>(this)->getPreviousDecl();
  }

  RecordDecl *getMostRecentDecl() {
    return cast<RecordDecl>(static_cast<TagDecl *>(this)->getMostRecentDecl());
  }
  const RecordDecl *getMostRecentDecl() const {
    return const_cast<RecordDecl*>(this)->getMostRecentDecl();
  }

  bool hasFlexibleArrayMember() const {
    return RecordDeclBits.HasFlexibleArrayMember;
  }

  void setHasFlexibleArrayMember(bool V) {
    RecordDeclBits.HasFlexibleArrayMember = V;
  }

  /// Whether this is an anonymous struct or union. To be an anonymous
  /// struct or union, it must have been declared without a name and
  /// there must be no objects of this type declared, e.g.,
  /// @code
  ///   union { int i; float f; };
  /// @endcode
  /// is an anonymous union but neither of the following are:
  /// @code
  ///  union X { int i; float f; };
  ///  union { int i; float f; } obj;
  /// @endcode
  bool isAnonymousStructOrUnion() const {
    return RecordDeclBits.AnonymousStructOrUnion;
  }

  void setAnonymousStructOrUnion(bool Anon) {
    RecordDeclBits.AnonymousStructOrUnion = Anon;
  }

  bool hasObjectMember() const { return RecordDeclBits.HasObjectMember; }
  void setHasObjectMember(bool val) { RecordDeclBits.HasObjectMember = val; }

  bool hasVolatileMember() const { return RecordDeclBits.HasVolatileMember; }

  void setHasVolatileMember(bool val) {
    RecordDeclBits.HasVolatileMember = val;
  }

  bool hasLoadedFieldsFromExternalStorage() const {
    return RecordDeclBits.LoadedFieldsFromExternalStorage;
  }

  void setHasLoadedFieldsFromExternalStorage(bool val) const {
    RecordDeclBits.LoadedFieldsFromExternalStorage = val;
  }

  /// Functions to query basic properties of non-trivial C structs.
  bool isNonTrivialToPrimitiveDefaultInitialize() const {
    return RecordDeclBits.NonTrivialToPrimitiveDefaultInitialize;
  }

  void setNonTrivialToPrimitiveDefaultInitialize(bool V) {
    RecordDeclBits.NonTrivialToPrimitiveDefaultInitialize = V;
  }

  bool isNonTrivialToPrimitiveCopy() const {
    return RecordDeclBits.NonTrivialToPrimitiveCopy;
  }

  void setNonTrivialToPrimitiveCopy(bool V) {
    RecordDeclBits.NonTrivialToPrimitiveCopy = V;
  }

  bool isNonTrivialToPrimitiveDestroy() const {
    return RecordDeclBits.NonTrivialToPrimitiveDestroy;
  }

  void setNonTrivialToPrimitiveDestroy(bool V) {
    RecordDeclBits.NonTrivialToPrimitiveDestroy = V;
  }

  bool hasNonTrivialToPrimitiveDefaultInitializeCUnion() const {
    return RecordDeclBits.HasNonTrivialToPrimitiveDefaultInitializeCUnion;
  }

  void setHasNonTrivialToPrimitiveDefaultInitializeCUnion(bool V) {
    RecordDeclBits.HasNonTrivialToPrimitiveDefaultInitializeCUnion = V;
  }

  bool hasNonTrivialToPrimitiveDestructCUnion() const {
    return RecordDeclBits.HasNonTrivialToPrimitiveDestructCUnion;
  }

  void setHasNonTrivialToPrimitiveDestructCUnion(bool V) {
    RecordDeclBits.HasNonTrivialToPrimitiveDestructCUnion = V;
  }

  bool hasNonTrivialToPrimitiveCopyCUnion() const {
    return RecordDeclBits.HasNonTrivialToPrimitiveCopyCUnion;
  }

  void setHasNonTrivialToPrimitiveCopyCUnion(bool V) {
    RecordDeclBits.HasNonTrivialToPrimitiveCopyCUnion = V;
  }

  /// Determine whether this class can be passed in registers. In C++ mode,
  /// it must have at least one trivial, non-deleted copy or move constructor.
  /// FIXME: This should be set as part of completeDefinition.
  bool canPassInRegisters() const {
    return getArgPassingRestrictions() == RecordArgPassingKind::CanPassInRegs;
  }

  RecordArgPassingKind getArgPassingRestrictions() const {
    return static_cast<RecordArgPassingKind>(
        RecordDeclBits.ArgPassingRestrictions);
  }

  void setArgPassingRestrictions(RecordArgPassingKind Kind) {
    RecordDeclBits.ArgPassingRestrictions = llvm::to_underlying(Kind);
  }

  bool isParamDestroyedInCallee() const {
    return RecordDeclBits.ParamDestroyedInCallee;
  }

  void setParamDestroyedInCallee(bool V) {
    RecordDeclBits.ParamDestroyedInCallee = V;
  }

  bool isRandomized() const { return RecordDeclBits.IsRandomized; }

  void setIsRandomized(bool V) { RecordDeclBits.IsRandomized = V; }

  void reorderDecls(const SmallVectorImpl<Decl *> &Decls);

  /// Determines whether this declaration represents the
  /// injected class name.
  ///
  /// The injected class name in C++ is the name of the class that
  /// appears inside the class itself. For example:
  ///
  /// \code
  /// struct C {
  ///   // C is implicitly declared here as a synonym for the class name.
  /// };
  ///
  /// C::C c; // same as "C c;"
  /// \endcode
  bool isInjectedClassName() const;

  /// Determine whether this record is a class describing a lambda
  /// function object.
  bool isLambda() const;

  /// Determine whether this record is a record for captured variables in
  /// CapturedStmt construct.
  bool isCapturedRecord() const;

  /// Mark the record as a record for captured variables in CapturedStmt
  /// construct.
  void setCapturedRecord();

  /// Returns the RecordDecl that actually defines
  ///  this struct/union/class.  When determining whether or not a
  ///  struct/union/class is completely defined, one should use this
  ///  method as opposed to 'isCompleteDefinition'.
  ///  'isCompleteDefinition' indicates whether or not a specific
  ///  RecordDecl is a completed definition, not whether or not the
  ///  record type is defined.  This method returns NULL if there is
  ///  no RecordDecl that defines the struct/union/tag.
  RecordDecl *getDefinition() const {
    return cast_or_null<RecordDecl>(TagDecl::getDefinition());
  }

  /// Returns whether this record is a union, or contains (at any nesting level)
  /// a union member. This is used by CMSE to warn about possible information
  /// leaks.
  bool isOrContainsUnion() const;

  // Iterator access to field members. The field iterator only visits
  // the non-static data members of this class, ignoring any static
  // data members, functions, constructors, destructors, etc.
  using field_iterator = specific_decl_iterator<FieldDecl>;
  using field_range = llvm::iterator_range<specific_decl_iterator<FieldDecl>>;

  field_range fields() const { return field_range(field_begin(), field_end()); }
  field_iterator field_begin() const;

  field_iterator field_end() const {
    return field_iterator(decl_iterator());
  }

  // Whether there are any fields (non-static data members) in this record.
  bool field_empty() const {
    return field_begin() == field_end();
  }

  /// Note that the definition of this type is now complete.
  virtual void completeDefinition();

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K >= firstRecord && K <= lastRecord;
  }

  /// Get whether or not this is an ms_struct which can
  /// be turned on with an attribute, pragma, or -mms-bitfields
  /// commandline option.
  bool isMsStruct(const ASTContext &C) const;

  /// Whether we are allowed to insert extra padding between fields.
  /// These padding are added to help AddressSanitizer detect
  /// intra-object-overflow bugs.
  bool mayInsertExtraPadding(bool EmitRemark = false) const;

  /// Finds the first data member which has a name.
  /// nullptr is returned if no named data member exists.
  const FieldDecl *findFirstNamedDataMember() const;

  /// Get precomputed ODRHash or add a new one.
  unsigned getODRHash();

private:
  /// Deserialize just the fields.
  void LoadFieldsFromExternalStorage() const;

  /// True if a valid hash is stored in ODRHash.
  bool hasODRHash() const { return RecordDeclBits.ODRHash; }
  void setODRHash(unsigned Hash) { RecordDeclBits.ODRHash = Hash; }
};

class FileScopeAsmDecl : public Decl {
  StringLiteral *AsmString;
  SourceLocation RParenLoc;

  FileScopeAsmDecl(DeclContext *DC, StringLiteral *asmstring,
                   SourceLocation StartL, SourceLocation EndL)
    : Decl(FileScopeAsm, DC, StartL), AsmString(asmstring), RParenLoc(EndL) {}

  virtual void anchor();

public:
  static FileScopeAsmDecl *Create(ASTContext &C, DeclContext *DC,
                                  StringLiteral *Str, SourceLocation AsmLoc,
                                  SourceLocation RParenLoc);

  static FileScopeAsmDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceLocation getAsmLoc() const { return getLocation(); }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }
  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getAsmLoc(), getRParenLoc());
  }

  const StringLiteral *getAsmString() const { return AsmString; }
  StringLiteral *getAsmString() { return AsmString; }
  void setAsmString(StringLiteral *Asm) { AsmString = Asm; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == FileScopeAsm; }
};

/// A declaration that models statements at global scope. This declaration
/// supports incremental and interactive C/C++.
///
/// \note This is used in libInterpreter, clang -cc1 -fincremental-extensions
/// and in tools such as clang-repl.
class TopLevelStmtDecl : public Decl, public DeclContext {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  Stmt *Statement = nullptr;
  bool IsSemiMissing = false;

  TopLevelStmtDecl(DeclContext *DC, SourceLocation L, Stmt *S)
      : Decl(TopLevelStmt, DC, L), DeclContext(TopLevelStmt), Statement(S) {}

  virtual void anchor();

public:
  static TopLevelStmtDecl *Create(ASTContext &C, Stmt *Statement);
  static TopLevelStmtDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;
  Stmt *getStmt() { return Statement; }
  const Stmt *getStmt() const { return Statement; }
  void setStmt(Stmt *S);
  bool isSemiMissing() const { return IsSemiMissing; }
  void setSemiMissing(bool Missing = true) { IsSemiMissing = Missing; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TopLevelStmt; }

  static DeclContext *castToDeclContext(const TopLevelStmtDecl *D) {
    return static_cast<DeclContext *>(const_cast<TopLevelStmtDecl *>(D));
  }
  static TopLevelStmtDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<TopLevelStmtDecl *>(const_cast<DeclContext *>(DC));
  }
};

/// Represents a block literal declaration, which is like an
/// unnamed FunctionDecl.  For example:
/// ^{ statement-body }   or   ^(int arg1, float arg2){ statement-body }
class BlockDecl : public Decl, public DeclContext {
  // This class stores some data in DeclContext::BlockDeclBits
  // to save some space. Use the provided accessors to access it.
public:
  /// A class which contains all the information about a particular
  /// captured value.
  class Capture {
    enum {
      flag_isByRef = 0x1,
      flag_isNested = 0x2
    };

    /// The variable being captured.
    llvm::PointerIntPair<VarDecl*, 2> VariableAndFlags;

    /// The copy expression, expressed in terms of a DeclRef (or
    /// BlockDeclRef) to the captured variable.  Only required if the
    /// variable has a C++ class type.
    Expr *CopyExpr;

  public:
    Capture(VarDecl *variable, bool byRef, bool nested, Expr *copy)
      : VariableAndFlags(variable,
                  (byRef ? flag_isByRef : 0) | (nested ? flag_isNested : 0)),
        CopyExpr(copy) {}

    /// The variable being captured.
    VarDecl *getVariable() const { return VariableAndFlags.getPointer(); }

    /// Whether this is a "by ref" capture, i.e. a capture of a __block
    /// variable.
    bool isByRef() const { return VariableAndFlags.getInt() & flag_isByRef; }

    bool isEscapingByref() const {
      return getVariable()->isEscapingByref();
    }

    bool isNonEscapingByref() const {
      return getVariable()->isNonEscapingByref();
    }

    /// Whether this is a nested capture, i.e. the variable captured
    /// is not from outside the immediately enclosing function/block.
    bool isNested() const { return VariableAndFlags.getInt() & flag_isNested; }

    bool hasCopyExpr() const { return CopyExpr != nullptr; }
    Expr *getCopyExpr() const { return CopyExpr; }
    void setCopyExpr(Expr *e) { CopyExpr = e; }
  };

private:
  /// A new[]'d array of pointers to ParmVarDecls for the formal
  /// parameters of this function.  This is null if a prototype or if there are
  /// no formals.
  ParmVarDecl **ParamInfo = nullptr;
  unsigned NumParams = 0;

  Stmt *Body = nullptr;
  TypeSourceInfo *SignatureAsWritten = nullptr;

  const Capture *Captures = nullptr;
  unsigned NumCaptures = 0;

  unsigned ManglingNumber = 0;
  Decl *ManglingContextDecl = nullptr;

protected:
  BlockDecl(DeclContext *DC, SourceLocation CaretLoc);

public:
  static BlockDecl *Create(ASTContext &C, DeclContext *DC, SourceLocation L);
  static BlockDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceLocation getCaretLocation() const { return getLocation(); }

  bool isVariadic() const { return BlockDeclBits.IsVariadic; }
  void setIsVariadic(bool value) { BlockDeclBits.IsVariadic = value; }

  CompoundStmt *getCompoundBody() const { return (CompoundStmt*) Body; }
  Stmt *getBody() const override { return (Stmt*) Body; }
  void setBody(CompoundStmt *B) { Body = (Stmt*) B; }

  void setSignatureAsWritten(TypeSourceInfo *Sig) { SignatureAsWritten = Sig; }
  TypeSourceInfo *getSignatureAsWritten() const { return SignatureAsWritten; }

  // ArrayRef access to formal parameters.
  ArrayRef<ParmVarDecl *> parameters() const {
    return {ParamInfo, getNumParams()};
  }
  MutableArrayRef<ParmVarDecl *> parameters() {
    return {ParamInfo, getNumParams()};
  }

  // Iterator access to formal parameters.
  using param_iterator = MutableArrayRef<ParmVarDecl *>::iterator;
  using param_const_iterator = ArrayRef<ParmVarDecl *>::const_iterator;

  bool param_empty() const { return parameters().empty(); }
  param_iterator param_begin() { return parameters().begin(); }
  param_iterator param_end() { return parameters().end(); }
  param_const_iterator param_begin() const { return parameters().begin(); }
  param_const_iterator param_end() const { return parameters().end(); }
  size_t param_size() const { return parameters().size(); }

  unsigned getNumParams() const { return NumParams; }

  const ParmVarDecl *getParamDecl(unsigned i) const {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }
  ParmVarDecl *getParamDecl(unsigned i) {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }

  void setParams(ArrayRef<ParmVarDecl *> NewParamInfo);

  /// True if this block (or its nested blocks) captures
  /// anything of local storage from its enclosing scopes.
  bool hasCaptures() const { return NumCaptures || capturesCXXThis(); }

  /// Returns the number of captured variables.
  /// Does not include an entry for 'this'.
  unsigned getNumCaptures() const { return NumCaptures; }

  using capture_const_iterator = ArrayRef<Capture>::const_iterator;

  ArrayRef<Capture> captures() const { return {Captures, NumCaptures}; }

  capture_const_iterator capture_begin() const { return captures().begin(); }
  capture_const_iterator capture_end() const { return captures().end(); }

  bool capturesCXXThis() const { return BlockDeclBits.CapturesCXXThis; }
  void setCapturesCXXThis(bool B = true) { BlockDeclBits.CapturesCXXThis = B; }

  bool blockMissingReturnType() const {
    return BlockDeclBits.BlockMissingReturnType;
  }

  void setBlockMissingReturnType(bool val = true) {
    BlockDeclBits.BlockMissingReturnType = val;
  }

  bool isConversionFromLambda() const {
    return BlockDeclBits.IsConversionFromLambda;
  }

  void setIsConversionFromLambda(bool val = true) {
    BlockDeclBits.IsConversionFromLambda = val;
  }

  bool doesNotEscape() const { return BlockDeclBits.DoesNotEscape; }
  void setDoesNotEscape(bool B = true) { BlockDeclBits.DoesNotEscape = B; }

  bool canAvoidCopyToHeap() const {
    return BlockDeclBits.CanAvoidCopyToHeap;
  }
  void setCanAvoidCopyToHeap(bool B = true) {
    BlockDeclBits.CanAvoidCopyToHeap = B;
  }

  bool capturesVariable(const VarDecl *var) const;

  void setCaptures(ASTContext &Context, ArrayRef<Capture> Captures,
                   bool CapturesCXXThis);

  unsigned getBlockManglingNumber() const { return ManglingNumber; }

  Decl *getBlockManglingContextDecl() const { return ManglingContextDecl; }

  void setBlockMangling(unsigned Number, Decl *Ctx) {
    ManglingNumber = Number;
    ManglingContextDecl = Ctx;
  }

  SourceRange getSourceRange() const override LLVM_READONLY;

  FunctionEffectsRef getFunctionEffects() const {
    if (const TypeSourceInfo *TSI = getSignatureAsWritten())
      if (const auto *FPT = TSI->getType()->getAs<FunctionProtoType>())
        return FPT->getFunctionEffects();
    return {};
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Block; }
  static DeclContext *castToDeclContext(const BlockDecl *D) {
    return static_cast<DeclContext *>(const_cast<BlockDecl*>(D));
  }
  static BlockDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<BlockDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents the body of a CapturedStmt, and serves as its DeclContext.
class CapturedDecl final
    : public Decl,
      public DeclContext,
      private llvm::TrailingObjects<CapturedDecl, ImplicitParamDecl *> {
protected:
  size_t numTrailingObjects(OverloadToken<ImplicitParamDecl>) {
    return NumParams;
  }

private:
  /// The number of parameters to the outlined function.
  unsigned NumParams;

  /// The position of context parameter in list of parameters.
  unsigned ContextParam;

  /// The body of the outlined function.
  llvm::PointerIntPair<Stmt *, 1, bool> BodyAndNothrow;

  explicit CapturedDecl(DeclContext *DC, unsigned NumParams);

  ImplicitParamDecl *const *getParams() const {
    return getTrailingObjects<ImplicitParamDecl *>();
  }

  ImplicitParamDecl **getParams() {
    return getTrailingObjects<ImplicitParamDecl *>();
  }

public:
  friend class ASTDeclReader;
  friend class ASTDeclWriter;
  friend TrailingObjects;

  static CapturedDecl *Create(ASTContext &C, DeclContext *DC,
                              unsigned NumParams);
  static CapturedDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                          unsigned NumParams);

  Stmt *getBody() const override;
  void setBody(Stmt *B);

  bool isNothrow() const;
  void setNothrow(bool Nothrow = true);

  unsigned getNumParams() const { return NumParams; }

  ImplicitParamDecl *getParam(unsigned i) const {
    assert(i < NumParams);
    return getParams()[i];
  }
  void setParam(unsigned i, ImplicitParamDecl *P) {
    assert(i < NumParams);
    getParams()[i] = P;
  }

  // ArrayRef interface to parameters.
  ArrayRef<ImplicitParamDecl *> parameters() const {
    return {getParams(), getNumParams()};
  }
  MutableArrayRef<ImplicitParamDecl *> parameters() {
    return {getParams(), getNumParams()};
  }

  /// Retrieve the parameter containing captured variables.
  ImplicitParamDecl *getContextParam() const {
    assert(ContextParam < NumParams);
    return getParam(ContextParam);
  }
  void setContextParam(unsigned i, ImplicitParamDecl *P) {
    assert(i < NumParams);
    ContextParam = i;
    setParam(i, P);
  }
  unsigned getContextParamPosition() const { return ContextParam; }

  using param_iterator = ImplicitParamDecl *const *;
  using param_range = llvm::iterator_range<param_iterator>;

  /// Retrieve an iterator pointing to the first parameter decl.
  param_iterator param_begin() const { return getParams(); }
  /// Retrieve an iterator one past the last parameter decl.
  param_iterator param_end() const { return getParams() + NumParams; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Captured; }
  static DeclContext *castToDeclContext(const CapturedDecl *D) {
    return static_cast<DeclContext *>(const_cast<CapturedDecl *>(D));
  }
  static CapturedDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<CapturedDecl *>(const_cast<DeclContext *>(DC));
  }
};

/// Describes a module import declaration, which makes the contents
/// of the named module visible in the current translation unit.
///
/// An import declaration imports the named module (or submodule). For example:
/// \code
///   @import std.vector;
/// \endcode
///
/// A C++20 module import declaration imports the named module or partition.
/// Periods are permitted in C++20 module names, but have no semantic meaning.
/// For example:
/// \code
///   import NamedModule;
///   import :SomePartition; // Must be a partition of the current module.
///   import Names.Like.this; // Allowed.
///   import :and.Also.Partition.names;
/// \endcode
///
/// Import declarations can also be implicitly generated from
/// \#include/\#import directives.
class ImportDecl final : public Decl,
                         llvm::TrailingObjects<ImportDecl, SourceLocation> {
  friend class ASTContext;
  friend class ASTDeclReader;
  friend class ASTReader;
  friend TrailingObjects;

  /// The imported module.
  Module *ImportedModule = nullptr;

  /// The next import in the list of imports local to the translation
  /// unit being parsed (not loaded from an AST file).
  ///
  /// Includes a bit that indicates whether we have source-location information
  /// for each identifier in the module name.
  ///
  /// When the bit is false, we only have a single source location for the
  /// end of the import declaration.
  llvm::PointerIntPair<ImportDecl *, 1, bool> NextLocalImportAndComplete;

  ImportDecl(DeclContext *DC, SourceLocation StartLoc, Module *Imported,
             ArrayRef<SourceLocation> IdentifierLocs);

  ImportDecl(DeclContext *DC, SourceLocation StartLoc, Module *Imported,
             SourceLocation EndLoc);

  ImportDecl(EmptyShell Empty) : Decl(Import, Empty) {}

  bool isImportComplete() const { return NextLocalImportAndComplete.getInt(); }

  void setImportComplete(bool C) { NextLocalImportAndComplete.setInt(C); }

  /// The next import in the list of imports local to the translation
  /// unit being parsed (not loaded from an AST file).
  ImportDecl *getNextLocalImport() const {
    return NextLocalImportAndComplete.getPointer();
  }

  void setNextLocalImport(ImportDecl *Import) {
    NextLocalImportAndComplete.setPointer(Import);
  }

public:
  /// Create a new module import declaration.
  static ImportDecl *Create(ASTContext &C, DeclContext *DC,
                            SourceLocation StartLoc, Module *Imported,
                            ArrayRef<SourceLocation> IdentifierLocs);

  /// Create a new module import declaration for an implicitly-generated
  /// import.
  static ImportDecl *CreateImplicit(ASTContext &C, DeclContext *DC,
                                    SourceLocation StartLoc, Module *Imported,
                                    SourceLocation EndLoc);

  /// Create a new, deserialized module import declaration.
  static ImportDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                        unsigned NumLocations);

  /// Retrieve the module that was imported by the import declaration.
  Module *getImportedModule() const { return ImportedModule; }

  /// Retrieves the locations of each of the identifiers that make up
  /// the complete module name in the import declaration.
  ///
  /// This will return an empty array if the locations of the individual
  /// identifiers aren't available.
  ArrayRef<SourceLocation> getIdentifierLocs() const;

  SourceRange getSourceRange() const override LLVM_READONLY;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Import; }
};

/// Represents a standard C++ module export declaration.
///
/// For example:
/// \code
///   export void foo();
/// \endcode
class ExportDecl final : public Decl, public DeclContext {
  virtual void anchor();

private:
  friend class ASTDeclReader;

  /// The source location for the right brace (if valid).
  SourceLocation RBraceLoc;

  ExportDecl(DeclContext *DC, SourceLocation ExportLoc)
      : Decl(Export, DC, ExportLoc), DeclContext(Export),
        RBraceLoc(SourceLocation()) {}

public:
  static ExportDecl *Create(ASTContext &C, DeclContext *DC,
                            SourceLocation ExportLoc);
  static ExportDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceLocation getExportLoc() const { return getLocation(); }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setRBraceLoc(SourceLocation L) { RBraceLoc = L; }

  bool hasBraces() const { return RBraceLoc.isValid(); }

  SourceLocation getEndLoc() const LLVM_READONLY {
    if (hasBraces())
      return RBraceLoc;
    // No braces: get the end location of the (only) declaration in context
    // (if present).
    return decls_empty() ? getLocation() : decls_begin()->getEndLoc();
  }

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getLocation(), getEndLoc());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Export; }
  static DeclContext *castToDeclContext(const ExportDecl *D) {
    return static_cast<DeclContext *>(const_cast<ExportDecl*>(D));
  }
  static ExportDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<ExportDecl *>(const_cast<DeclContext*>(DC));
  }
};

/// Represents an empty-declaration.
class EmptyDecl : public Decl {
  EmptyDecl(DeclContext *DC, SourceLocation L) : Decl(Empty, DC, L) {}

  virtual void anchor();

public:
  static EmptyDecl *Create(ASTContext &C, DeclContext *DC,
                           SourceLocation L);
  static EmptyDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Empty; }
};

/// HLSLBufferDecl - Represent a cbuffer or tbuffer declaration.
class HLSLBufferDecl final : public NamedDecl, public DeclContext {
  /// LBraceLoc - The ending location of the source range.
  SourceLocation LBraceLoc;
  /// RBraceLoc - The ending location of the source range.
  SourceLocation RBraceLoc;
  /// KwLoc - The location of the cbuffer or tbuffer keyword.
  SourceLocation KwLoc;
  /// IsCBuffer - Whether the buffer is a cbuffer (and not a tbuffer).
  bool IsCBuffer;

  HLSLBufferDecl(DeclContext *DC, bool CBuffer, SourceLocation KwLoc,
                 IdentifierInfo *ID, SourceLocation IDLoc,
                 SourceLocation LBrace);

public:
  static HLSLBufferDecl *Create(ASTContext &C, DeclContext *LexicalParent,
                                bool CBuffer, SourceLocation KwLoc,
                                IdentifierInfo *ID, SourceLocation IDLoc,
                                SourceLocation LBrace);
  static HLSLBufferDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY {
    return SourceRange(getLocStart(), RBraceLoc);
  }
  SourceLocation getLocStart() const LLVM_READONLY { return KwLoc; }
  SourceLocation getLBraceLoc() const { return LBraceLoc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setRBraceLoc(SourceLocation L) { RBraceLoc = L; }
  bool isCBuffer() const { return IsCBuffer; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == HLSLBuffer; }
  static DeclContext *castToDeclContext(const HLSLBufferDecl *D) {
    return static_cast<DeclContext *>(const_cast<HLSLBufferDecl *>(D));
  }
  static HLSLBufferDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<HLSLBufferDecl *>(const_cast<DeclContext *>(DC));
  }

  friend class ASTDeclReader;
  friend class ASTDeclWriter;
};

/// Insertion operator for diagnostics.  This allows sending NamedDecl's
/// into a diagnostic with <<.
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             const NamedDecl *ND) {
  PD.AddTaggedVal(reinterpret_cast<uint64_t>(ND),
                  DiagnosticsEngine::ak_nameddecl);
  return PD;
}

template<typename decl_type>
void Redeclarable<decl_type>::setPreviousDecl(decl_type *PrevDecl) {
  // Note: This routine is implemented here because we need both NamedDecl
  // and Redeclarable to be defined.
  assert(RedeclLink.isFirst() &&
         "setPreviousDecl on a decl already in a redeclaration chain");

  if (PrevDecl) {
    // Point to previous. Make sure that this is actually the most recent
    // redeclaration, or we can build invalid chains. If the most recent
    // redeclaration is invalid, it won't be PrevDecl, but we want it anyway.
    First = PrevDecl->getFirstDecl();
    assert(First->RedeclLink.isFirst() && "Expected first");
    decl_type *MostRecent = First->getNextRedeclaration();
    RedeclLink = PreviousDeclLink(cast<decl_type>(MostRecent));

    // If the declaration was previously visible, a redeclaration of it remains
    // visible even if it wouldn't be visible by itself.
    static_cast<decl_type*>(this)->IdentifierNamespace |=
      MostRecent->getIdentifierNamespace() &
      (Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Type);
  } else {
    // Make this first.
    First = static_cast<decl_type*>(this);
  }

  // First one will point to this one as latest.
  First->RedeclLink.setLatest(static_cast<decl_type*>(this));

  assert(!isa<NamedDecl>(static_cast<decl_type*>(this)) ||
         cast<NamedDecl>(static_cast<decl_type*>(this))->isLinkageValid());
}

// Inline function definitions.

/// Check if the given decl is complete.
///
/// We use this function to break a cycle between the inline definitions in
/// Type.h and Decl.h.
inline bool IsEnumDeclComplete(EnumDecl *ED) {
  return ED->isComplete();
}

/// Check if the given decl is scoped.
///
/// We use this function to break a cycle between the inline definitions in
/// Type.h and Decl.h.
inline bool IsEnumDeclScoped(EnumDecl *ED) {
  return ED->isScoped();
}

/// OpenMP variants are mangled early based on their OpenMP context selector.
/// The new name looks likes this:
///  <name> + OpenMPVariantManglingSeparatorStr + <mangled OpenMP context>
static constexpr StringRef getOpenMPVariantManglingSeparatorStr() {
  return "$ompvariant";
}

/// Returns whether the given FunctionDecl has an __arm[_locally]_streaming
/// attribute.
bool IsArmStreamingFunction(const FunctionDecl *FD,
                            bool IncludeLocallyStreaming);

} // namespace clang

#endif // LLVM_CLANG_AST_DECL_H
