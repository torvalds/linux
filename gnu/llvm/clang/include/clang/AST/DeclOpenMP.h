//===- DeclOpenMP.h - Classes for representing OpenMP directives -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines OpenMP nodes for declarative directives.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLOPENMP_H
#define LLVM_CLANG_AST_DECLOPENMP_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/TrailingObjects.h"

namespace clang {

/// This is a basic class for representing single OpenMP declarative directive.
///
template <typename U> class OMPDeclarativeDirective : public U {
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Get the clauses storage.
  MutableArrayRef<OMPClause *> getClauses() {
    if (!Data)
      return std::nullopt;
    return Data->getClauses();
  }

protected:
  /// Data, associated with the directive.
  OMPChildren *Data = nullptr;

  /// Build instance of directive.
  template <typename... Params>
  OMPDeclarativeDirective(Params &&... P) : U(std::forward<Params>(P)...) {}

  template <typename T, typename... Params>
  static T *createDirective(const ASTContext &C, DeclContext *DC,
                            ArrayRef<OMPClause *> Clauses, unsigned NumChildren,
                            Params &&... P) {
    auto *Inst = new (C, DC, size(Clauses.size(), NumChildren))
        T(DC, std::forward<Params>(P)...);
    Inst->Data = OMPChildren::Create(Inst + 1, Clauses,
                                     /*AssociatedStmt=*/nullptr, NumChildren);
    Inst->Data->setClauses(Clauses);
    return Inst;
  }

  template <typename T, typename... Params>
  static T *createEmptyDirective(const ASTContext &C, GlobalDeclID ID,
                                 unsigned NumClauses, unsigned NumChildren,
                                 Params &&... P) {
    auto *Inst = new (C, ID, size(NumClauses, NumChildren))
        T(nullptr, std::forward<Params>(P)...);
    Inst->Data = OMPChildren::CreateEmpty(
        Inst + 1, NumClauses, /*HasAssociatedStmt=*/false, NumChildren);
    return Inst;
  }

  static size_t size(unsigned NumClauses, unsigned NumChildren) {
    return OMPChildren::size(NumClauses, /*HasAssociatedStmt=*/false,
                             NumChildren);
  }

public:
  /// Get number of clauses.
  unsigned getNumClauses() const {
    if (!Data)
      return 0;
    return Data->getNumClauses();
  }

  /// Returns specified clause.
  ///
  /// \param I Number of clause.
  ///
  OMPClause *getClause(unsigned I) const { return clauses()[I]; }

  ArrayRef<OMPClause *> clauses() const {
    if (!Data)
      return std::nullopt;
    return Data->getClauses();
  }
};

/// This represents '#pragma omp threadprivate ...' directive.
/// For example, in the following, both 'a' and 'A::b' are threadprivate:
///
/// \code
/// int a;
/// #pragma omp threadprivate(a)
/// struct A {
///   static int b;
/// #pragma omp threadprivate(b)
/// };
/// \endcode
///
class OMPThreadPrivateDecl final : public OMPDeclarativeDirective<Decl> {
  friend class OMPDeclarativeDirective<Decl>;

  virtual void anchor();

  OMPThreadPrivateDecl(DeclContext *DC = nullptr,
                       SourceLocation L = SourceLocation())
      : OMPDeclarativeDirective<Decl>(OMPThreadPrivate, DC, L) {}

  ArrayRef<const Expr *> getVars() const {
    auto **Storage = reinterpret_cast<Expr **>(Data->getChildren().data());
    return llvm::ArrayRef(Storage, Data->getNumChildren());
  }

  MutableArrayRef<Expr *> getVars() {
    auto **Storage = reinterpret_cast<Expr **>(Data->getChildren().data());
    return llvm::MutableArrayRef(Storage, Data->getNumChildren());
  }

  void setVars(ArrayRef<Expr *> VL);

public:
  static OMPThreadPrivateDecl *Create(ASTContext &C, DeclContext *DC,
                                      SourceLocation L,
                                      ArrayRef<Expr *> VL);
  static OMPThreadPrivateDecl *CreateDeserialized(ASTContext &C,
                                                  GlobalDeclID ID, unsigned N);

  typedef MutableArrayRef<Expr *>::iterator varlist_iterator;
  typedef ArrayRef<const Expr *>::iterator varlist_const_iterator;
  typedef llvm::iterator_range<varlist_iterator> varlist_range;
  typedef llvm::iterator_range<varlist_const_iterator> varlist_const_range;

  unsigned varlist_size() const { return Data->getNumChildren(); }
  bool varlist_empty() const { return Data->getChildren().empty(); }

  varlist_range varlists() {
    return varlist_range(varlist_begin(), varlist_end());
  }
  varlist_const_range varlists() const {
    return varlist_const_range(varlist_begin(), varlist_end());
  }
  varlist_iterator varlist_begin() { return getVars().begin(); }
  varlist_iterator varlist_end() { return getVars().end(); }
  varlist_const_iterator varlist_begin() const { return getVars().begin(); }
  varlist_const_iterator varlist_end() const { return getVars().end(); }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == OMPThreadPrivate; }
};

enum class OMPDeclareReductionInitKind {
  Call,   // Initialized by function call.
  Direct, // omp_priv(<expr>)
  Copy    // omp_priv = <expr>
};

/// This represents '#pragma omp declare reduction ...' directive.
/// For example, in the following, declared reduction 'foo' for types 'int' and
/// 'float':
///
/// \code
/// #pragma omp declare reduction (foo : int,float : omp_out += omp_in)
///                     initializer (omp_priv = 0)
/// \endcode
///
/// Here 'omp_out += omp_in' is a combiner and 'omp_priv = 0' is an initializer.
class OMPDeclareReductionDecl final : public ValueDecl, public DeclContext {
  // This class stores some data in DeclContext::OMPDeclareReductionDeclBits
  // to save some space. Use the provided accessors to access it.

  friend class ASTDeclReader;
  /// Combiner for declare reduction construct.
  Expr *Combiner = nullptr;
  /// Initializer for declare reduction construct.
  Expr *Initializer = nullptr;
  /// In parameter of the combiner.
  Expr *In = nullptr;
  /// Out parameter of the combiner.
  Expr *Out = nullptr;
  /// Priv parameter of the initializer.
  Expr *Priv = nullptr;
  /// Orig parameter of the initializer.
  Expr *Orig = nullptr;

  /// Reference to the previous declare reduction construct in the same
  /// scope with the same name. Required for proper templates instantiation if
  /// the declare reduction construct is declared inside compound statement.
  LazyDeclPtr PrevDeclInScope;

  void anchor() override;

  OMPDeclareReductionDecl(Kind DK, DeclContext *DC, SourceLocation L,
                          DeclarationName Name, QualType Ty,
                          OMPDeclareReductionDecl *PrevDeclInScope);

  void setPrevDeclInScope(OMPDeclareReductionDecl *Prev) {
    PrevDeclInScope = Prev;
  }

public:
  /// Create declare reduction node.
  static OMPDeclareReductionDecl *
  Create(ASTContext &C, DeclContext *DC, SourceLocation L, DeclarationName Name,
         QualType T, OMPDeclareReductionDecl *PrevDeclInScope);
  /// Create deserialized declare reduction node.
  static OMPDeclareReductionDecl *CreateDeserialized(ASTContext &C,
                                                     GlobalDeclID ID);

  /// Get combiner expression of the declare reduction construct.
  Expr *getCombiner() { return Combiner; }
  const Expr *getCombiner() const { return Combiner; }
  /// Get In variable of the combiner.
  Expr *getCombinerIn() { return In; }
  const Expr *getCombinerIn() const { return In; }
  /// Get Out variable of the combiner.
  Expr *getCombinerOut() { return Out; }
  const Expr *getCombinerOut() const { return Out; }
  /// Set combiner expression for the declare reduction construct.
  void setCombiner(Expr *E) { Combiner = E; }
  /// Set combiner In and Out vars.
  void setCombinerData(Expr *InE, Expr *OutE) {
    In = InE;
    Out = OutE;
  }

  /// Get initializer expression (if specified) of the declare reduction
  /// construct.
  Expr *getInitializer() { return Initializer; }
  const Expr *getInitializer() const { return Initializer; }
  /// Get initializer kind.
  OMPDeclareReductionInitKind getInitializerKind() const {
    return static_cast<OMPDeclareReductionInitKind>(
        OMPDeclareReductionDeclBits.InitializerKind);
  }
  /// Get Orig variable of the initializer.
  Expr *getInitOrig() { return Orig; }
  const Expr *getInitOrig() const { return Orig; }
  /// Get Priv variable of the initializer.
  Expr *getInitPriv() { return Priv; }
  const Expr *getInitPriv() const { return Priv; }
  /// Set initializer expression for the declare reduction construct.
  void setInitializer(Expr *E, OMPDeclareReductionInitKind IK) {
    Initializer = E;
    OMPDeclareReductionDeclBits.InitializerKind = llvm::to_underlying(IK);
  }
  /// Set initializer Orig and Priv vars.
  void setInitializerData(Expr *OrigE, Expr *PrivE) {
    Orig = OrigE;
    Priv = PrivE;
  }

  /// Get reference to previous declare reduction construct in the same
  /// scope with the same name.
  OMPDeclareReductionDecl *getPrevDeclInScope();
  const OMPDeclareReductionDecl *getPrevDeclInScope() const;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == OMPDeclareReduction; }
  static DeclContext *castToDeclContext(const OMPDeclareReductionDecl *D) {
    return static_cast<DeclContext *>(const_cast<OMPDeclareReductionDecl *>(D));
  }
  static OMPDeclareReductionDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<OMPDeclareReductionDecl *>(
        const_cast<DeclContext *>(DC));
  }
};

/// This represents '#pragma omp declare mapper ...' directive. Map clauses are
/// allowed to use with this directive. The following example declares a user
/// defined mapper for the type 'struct vec'. This example instructs the fields
/// 'len' and 'data' should be mapped when mapping instances of 'struct vec'.
///
/// \code
/// #pragma omp declare mapper(mid: struct vec v) map(v.len, v.data[0:N])
/// \endcode
class OMPDeclareMapperDecl final : public OMPDeclarativeDirective<ValueDecl>,
                                   public DeclContext {
  friend class OMPDeclarativeDirective<ValueDecl>;
  friend class ASTDeclReader;
  friend class ASTDeclWriter;

  /// Mapper variable, which is 'v' in the example above
  Expr *MapperVarRef = nullptr;

  /// Name of the mapper variable
  DeclarationName VarName;

  LazyDeclPtr PrevDeclInScope;

  void anchor() override;

  OMPDeclareMapperDecl(DeclContext *DC, SourceLocation L, DeclarationName Name,
                       QualType Ty, DeclarationName VarName,
                       OMPDeclareMapperDecl *PrevDeclInScope)
      : OMPDeclarativeDirective<ValueDecl>(OMPDeclareMapper, DC, L, Name, Ty),
        DeclContext(OMPDeclareMapper), VarName(VarName),
        PrevDeclInScope(PrevDeclInScope) {}

  void setPrevDeclInScope(OMPDeclareMapperDecl *Prev) {
    PrevDeclInScope = Prev;
  }

public:
  /// Creates declare mapper node.
  static OMPDeclareMapperDecl *Create(ASTContext &C, DeclContext *DC,
                                      SourceLocation L, DeclarationName Name,
                                      QualType T, DeclarationName VarName,
                                      ArrayRef<OMPClause *> Clauses,
                                      OMPDeclareMapperDecl *PrevDeclInScope);
  /// Creates deserialized declare mapper node.
  static OMPDeclareMapperDecl *CreateDeserialized(ASTContext &C,
                                                  GlobalDeclID ID, unsigned N);

  using clauselist_iterator = MutableArrayRef<OMPClause *>::iterator;
  using clauselist_const_iterator = ArrayRef<const OMPClause *>::iterator;
  using clauselist_range = llvm::iterator_range<clauselist_iterator>;
  using clauselist_const_range =
      llvm::iterator_range<clauselist_const_iterator>;

  unsigned clauselist_size() const { return Data->getNumClauses(); }
  bool clauselist_empty() const { return Data->getClauses().empty(); }

  clauselist_range clauselists() {
    return clauselist_range(clauselist_begin(), clauselist_end());
  }
  clauselist_const_range clauselists() const {
    return clauselist_const_range(clauselist_begin(), clauselist_end());
  }
  clauselist_iterator clauselist_begin() { return Data->getClauses().begin(); }
  clauselist_iterator clauselist_end() { return Data->getClauses().end(); }
  clauselist_const_iterator clauselist_begin() const {
    return Data->getClauses().begin();
  }
  clauselist_const_iterator clauselist_end() const {
    return Data->getClauses().end();
  }

  /// Get the variable declared in the mapper
  Expr *getMapperVarRef() { return cast_or_null<Expr>(Data->getChildren()[0]); }
  const Expr *getMapperVarRef() const {
    return cast_or_null<Expr>(Data->getChildren()[0]);
  }
  /// Set the variable declared in the mapper
  void setMapperVarRef(Expr *MapperVarRefE) {
    Data->getChildren()[0] = MapperVarRefE;
  }

  /// Get the name of the variable declared in the mapper
  DeclarationName getVarName() { return VarName; }

  /// Get reference to previous declare mapper construct in the same
  /// scope with the same name.
  OMPDeclareMapperDecl *getPrevDeclInScope();
  const OMPDeclareMapperDecl *getPrevDeclInScope() const;

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == OMPDeclareMapper; }
  static DeclContext *castToDeclContext(const OMPDeclareMapperDecl *D) {
    return static_cast<DeclContext *>(const_cast<OMPDeclareMapperDecl *>(D));
  }
  static OMPDeclareMapperDecl *castFromDeclContext(const DeclContext *DC) {
    return static_cast<OMPDeclareMapperDecl *>(const_cast<DeclContext *>(DC));
  }
};

/// Pseudo declaration for capturing expressions. Also is used for capturing of
/// non-static data members in non-static member functions.
///
/// Clang supports capturing of variables only, but OpenMP 4.5 allows to
/// privatize non-static members of current class in non-static member
/// functions. This pseudo-declaration allows properly handle this kind of
/// capture by wrapping captured expression into a variable-like declaration.
class OMPCapturedExprDecl final : public VarDecl {
  friend class ASTDeclReader;
  void anchor() override;

  OMPCapturedExprDecl(ASTContext &C, DeclContext *DC, IdentifierInfo *Id,
                      QualType Type, TypeSourceInfo *TInfo,
                      SourceLocation StartLoc)
      : VarDecl(OMPCapturedExpr, C, DC, StartLoc, StartLoc, Id, Type, TInfo,
                SC_None) {
    setImplicit();
  }

public:
  static OMPCapturedExprDecl *Create(ASTContext &C, DeclContext *DC,
                                     IdentifierInfo *Id, QualType T,
                                     SourceLocation StartLoc);

  static OMPCapturedExprDecl *CreateDeserialized(ASTContext &C,
                                                 GlobalDeclID ID);

  SourceRange getSourceRange() const override LLVM_READONLY;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == OMPCapturedExpr; }
};

/// This represents '#pragma omp requires...' directive.
/// For example
///
/// \code
/// #pragma omp requires unified_address
/// \endcode
///
class OMPRequiresDecl final : public OMPDeclarativeDirective<Decl> {
  friend class OMPDeclarativeDirective<Decl>;
  friend class ASTDeclReader;

  virtual void anchor();

  OMPRequiresDecl(DeclContext *DC, SourceLocation L)
      : OMPDeclarativeDirective<Decl>(OMPRequires, DC, L) {}

public:
  /// Create requires node.
  static OMPRequiresDecl *Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L, ArrayRef<OMPClause *> CL);
  /// Create deserialized requires node.
  static OMPRequiresDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                             unsigned N);

  using clauselist_iterator = MutableArrayRef<OMPClause *>::iterator;
  using clauselist_const_iterator = ArrayRef<const OMPClause *>::iterator;
  using clauselist_range = llvm::iterator_range<clauselist_iterator>;
  using clauselist_const_range = llvm::iterator_range<clauselist_const_iterator>;

  unsigned clauselist_size() const { return Data->getNumClauses(); }
  bool clauselist_empty() const { return Data->getClauses().empty(); }

  clauselist_range clauselists() {
    return clauselist_range(clauselist_begin(), clauselist_end());
  }
  clauselist_const_range clauselists() const {
    return clauselist_const_range(clauselist_begin(), clauselist_end());
  }
  clauselist_iterator clauselist_begin() { return Data->getClauses().begin(); }
  clauselist_iterator clauselist_end() { return Data->getClauses().end(); }
  clauselist_const_iterator clauselist_begin() const {
    return Data->getClauses().begin();
  }
  clauselist_const_iterator clauselist_end() const {
    return Data->getClauses().end();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == OMPRequires; }
};

/// This represents '#pragma omp allocate ...' directive.
/// For example, in the following, the default allocator is used for both 'a'
/// and 'A::b':
///
/// \code
/// int a;
/// #pragma omp allocate(a)
/// struct A {
///   static int b;
/// #pragma omp allocate(b)
/// };
/// \endcode
///
class OMPAllocateDecl final : public OMPDeclarativeDirective<Decl> {
  friend class OMPDeclarativeDirective<Decl>;
  friend class ASTDeclReader;

  virtual void anchor();

  OMPAllocateDecl(DeclContext *DC, SourceLocation L)
      : OMPDeclarativeDirective<Decl>(OMPAllocate, DC, L) {}

  ArrayRef<const Expr *> getVars() const {
    auto **Storage = reinterpret_cast<Expr **>(Data->getChildren().data());
    return llvm::ArrayRef(Storage, Data->getNumChildren());
  }

  MutableArrayRef<Expr *> getVars() {
    auto **Storage = reinterpret_cast<Expr **>(Data->getChildren().data());
    return llvm::MutableArrayRef(Storage, Data->getNumChildren());
  }

  void setVars(ArrayRef<Expr *> VL);

public:
  static OMPAllocateDecl *Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L, ArrayRef<Expr *> VL,
                                 ArrayRef<OMPClause *> CL);
  static OMPAllocateDecl *CreateDeserialized(ASTContext &C, GlobalDeclID ID,
                                             unsigned NVars, unsigned NClauses);

  typedef MutableArrayRef<Expr *>::iterator varlist_iterator;
  typedef ArrayRef<const Expr *>::iterator varlist_const_iterator;
  typedef llvm::iterator_range<varlist_iterator> varlist_range;
  typedef llvm::iterator_range<varlist_const_iterator> varlist_const_range;
  using clauselist_iterator = MutableArrayRef<OMPClause *>::iterator;
  using clauselist_const_iterator = ArrayRef<const OMPClause *>::iterator;
  using clauselist_range = llvm::iterator_range<clauselist_iterator>;
  using clauselist_const_range = llvm::iterator_range<clauselist_const_iterator>;

  unsigned varlist_size() const { return Data->getNumChildren(); }
  bool varlist_empty() const { return Data->getChildren().empty(); }
  unsigned clauselist_size() const { return Data->getNumClauses(); }
  bool clauselist_empty() const { return Data->getClauses().empty(); }

  varlist_range varlists() {
    return varlist_range(varlist_begin(), varlist_end());
  }
  varlist_const_range varlists() const {
    return varlist_const_range(varlist_begin(), varlist_end());
  }
  varlist_iterator varlist_begin() { return getVars().begin(); }
  varlist_iterator varlist_end() { return getVars().end(); }
  varlist_const_iterator varlist_begin() const { return getVars().begin(); }
  varlist_const_iterator varlist_end() const { return getVars().end(); }

  clauselist_range clauselists() {
    return clauselist_range(clauselist_begin(), clauselist_end());
  }
  clauselist_const_range clauselists() const {
    return clauselist_const_range(clauselist_begin(), clauselist_end());
  }
  clauselist_iterator clauselist_begin() { return Data->getClauses().begin(); }
  clauselist_iterator clauselist_end() { return Data->getClauses().end(); }
  clauselist_const_iterator clauselist_begin() const {
    return Data->getClauses().begin();
  }
  clauselist_const_iterator clauselist_end() const {
    return Data->getClauses().end();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == OMPAllocate; }
};

} // end namespace clang

#endif
