//===- SemaTemplate.h - C++ Templates ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// This file provides types used in the semantic analysis of C++ templates.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_TEMPLATE_H
#define LLVM_CLANG_SEMA_TEMPLATE_H

#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <optional>
#include <utility>

namespace clang {

class ASTContext;
class BindingDecl;
class CXXMethodDecl;
class Decl;
class DeclaratorDecl;
class DeclContext;
class EnumDecl;
class FunctionDecl;
class NamedDecl;
class ParmVarDecl;
class TagDecl;
class TypedefNameDecl;
class TypeSourceInfo;
class VarDecl;

/// The kind of template substitution being performed.
enum class TemplateSubstitutionKind : char {
  /// We are substituting template parameters for template arguments in order
  /// to form a template specialization.
  Specialization,
  /// We are substituting template parameters for (typically) other template
  /// parameters in order to rewrite a declaration as a different declaration
  /// (for example, when forming a deduction guide from a constructor).
  Rewrite,
};

  /// Data structure that captures multiple levels of template argument
  /// lists for use in template instantiation.
  ///
  /// Multiple levels of template arguments occur when instantiating the
  /// definitions of member templates. For example:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   template<T Value>
  ///   struct Y {
  ///     void f();
  ///   };
  /// };
  /// \endcode
  ///
  /// When instantiating X<int>::Y<17>::f, the multi-level template argument
  /// list will contain a template argument list (int) at depth 0 and a
  /// template argument list (17) at depth 1.
  class MultiLevelTemplateArgumentList {
    /// The template argument list at a certain template depth

    using ArgList = ArrayRef<TemplateArgument>;
    struct ArgumentListLevel {
      llvm::PointerIntPair<Decl *, 1, bool> AssociatedDeclAndFinal;
      ArgList Args;
    };
    using ContainerType = SmallVector<ArgumentListLevel, 4>;

    using ArgListsIterator = ContainerType::iterator;
    using ConstArgListsIterator = ContainerType::const_iterator;

    /// The template argument lists, stored from the innermost template
    /// argument list (first) to the outermost template argument list (last).
    ContainerType TemplateArgumentLists;

    /// The number of outer levels of template arguments that are not
    /// being substituted.
    unsigned NumRetainedOuterLevels = 0;

    /// The kind of substitution described by this argument list.
    TemplateSubstitutionKind Kind = TemplateSubstitutionKind::Specialization;

  public:
    /// Construct an empty set of template argument lists.
    MultiLevelTemplateArgumentList() = default;

    /// Construct a single-level template argument list.
    MultiLevelTemplateArgumentList(Decl *D, ArgList Args, bool Final) {
      addOuterTemplateArguments(D, Args, Final);
    }

    void setKind(TemplateSubstitutionKind K) { Kind = K; }

    /// Determine the kind of template substitution being performed.
    TemplateSubstitutionKind getKind() const { return Kind; }

    /// Determine whether we are rewriting template parameters rather than
    /// substituting for them. If so, we should not leave references to the
    /// original template parameters behind.
    bool isRewrite() const {
      return Kind == TemplateSubstitutionKind::Rewrite;
    }

    /// Determine the number of levels in this template argument
    /// list.
    unsigned getNumLevels() const {
      return TemplateArgumentLists.size() + NumRetainedOuterLevels;
    }

    /// Determine the number of substituted levels in this template
    /// argument list.
    unsigned getNumSubstitutedLevels() const {
      return TemplateArgumentLists.size();
    }

    // Determine the number of substituted args at 'Depth'.
    unsigned getNumSubsitutedArgs(unsigned Depth) const {
      assert(NumRetainedOuterLevels <= Depth && Depth < getNumLevels());
      return TemplateArgumentLists[getNumLevels() - Depth - 1].Args.size();
    }

    unsigned getNumRetainedOuterLevels() const {
      return NumRetainedOuterLevels;
    }

    /// Determine how many of the \p OldDepth outermost template parameter
    /// lists would be removed by substituting these arguments.
    unsigned getNewDepth(unsigned OldDepth) const {
      if (OldDepth < NumRetainedOuterLevels)
        return OldDepth;
      if (OldDepth < getNumLevels())
        return NumRetainedOuterLevels;
      return OldDepth - TemplateArgumentLists.size();
    }

    /// Retrieve the template argument at a given depth and index.
    const TemplateArgument &operator()(unsigned Depth, unsigned Index) const {
      assert(NumRetainedOuterLevels <= Depth && Depth < getNumLevels());
      assert(Index <
             TemplateArgumentLists[getNumLevels() - Depth - 1].Args.size());
      return TemplateArgumentLists[getNumLevels() - Depth - 1].Args[Index];
    }

    /// A template-like entity which owns the whole pattern being substituted.
    /// This will usually own a set of template parameters, or in some
    /// cases might even be a template parameter itself.
    std::pair<Decl *, bool> getAssociatedDecl(unsigned Depth) const {
      assert(NumRetainedOuterLevels <= Depth && Depth < getNumLevels());
      auto AD = TemplateArgumentLists[getNumLevels() - Depth - 1]
                    .AssociatedDeclAndFinal;
      return {AD.getPointer(), AD.getInt()};
    }

    /// Determine whether there is a non-NULL template argument at the
    /// given depth and index.
    ///
    /// There must exist a template argument list at the given depth.
    bool hasTemplateArgument(unsigned Depth, unsigned Index) const {
      assert(Depth < getNumLevels());

      if (Depth < NumRetainedOuterLevels)
        return false;

      if (Index >=
          TemplateArgumentLists[getNumLevels() - Depth - 1].Args.size())
        return false;

      return !(*this)(Depth, Index).isNull();
    }

    bool isAnyArgInstantiationDependent() const {
      for (ArgumentListLevel ListLevel : TemplateArgumentLists)
        for (const TemplateArgument &TA : ListLevel.Args)
          if (TA.isInstantiationDependent())
            return true;
      return false;
    }

    /// Clear out a specific template argument.
    void setArgument(unsigned Depth, unsigned Index,
                     TemplateArgument Arg) {
      assert(NumRetainedOuterLevels <= Depth && Depth < getNumLevels());
      assert(Index <
             TemplateArgumentLists[getNumLevels() - Depth - 1].Args.size());
      const_cast<TemplateArgument &>(
          TemplateArgumentLists[getNumLevels() - Depth - 1].Args[Index]) = Arg;
    }

    /// Add a new outmost level to the multi-level template argument
    /// list.
    /// A 'Final' substitution means that Subst* nodes won't be built
    /// for the replacements.
    void addOuterTemplateArguments(Decl *AssociatedDecl, ArgList Args,
                                   bool Final) {
      assert(!NumRetainedOuterLevels &&
             "substituted args outside retained args?");
      assert(getKind() == TemplateSubstitutionKind::Specialization);
      TemplateArgumentLists.push_back(
          {{AssociatedDecl ? AssociatedDecl->getCanonicalDecl() : nullptr,
            Final},
           Args});
    }

    void addOuterTemplateArguments(ArgList Args) {
      assert(!NumRetainedOuterLevels &&
             "substituted args outside retained args?");
      assert(getKind() == TemplateSubstitutionKind::Rewrite);
      TemplateArgumentLists.push_back({{}, Args});
    }

    void addOuterTemplateArguments(std::nullopt_t) {
      assert(!NumRetainedOuterLevels &&
             "substituted args outside retained args?");
      TemplateArgumentLists.push_back({});
    }

    /// Replaces the current 'innermost' level with the provided argument list.
    /// This is useful for type deduction cases where we need to get the entire
    /// list from the AST, but then add the deduced innermost list.
    void replaceInnermostTemplateArguments(Decl *AssociatedDecl, ArgList Args) {
      assert((!TemplateArgumentLists.empty() || NumRetainedOuterLevels) &&
             "Replacing in an empty list?");

      if (!TemplateArgumentLists.empty()) {
        assert((TemplateArgumentLists[0].AssociatedDeclAndFinal.getPointer() ||
                TemplateArgumentLists[0].AssociatedDeclAndFinal.getPointer() ==
                    AssociatedDecl) &&
               "Trying to change incorrect declaration?");
        TemplateArgumentLists[0].Args = Args;
      } else {
        --NumRetainedOuterLevels;
        TemplateArgumentLists.push_back(
            {{AssociatedDecl, /*Final=*/false}, Args});
      }
    }

    /// Add an outermost level that we are not substituting. We have no
    /// arguments at this level, and do not remove it from the depth of inner
    /// template parameters that we instantiate.
    void addOuterRetainedLevel() {
      ++NumRetainedOuterLevels;
    }
    void addOuterRetainedLevels(unsigned Num) {
      NumRetainedOuterLevels += Num;
    }

    /// Retrieve the innermost template argument list.
    const ArgList &getInnermost() const {
      return TemplateArgumentLists.front().Args;
    }
    /// Retrieve the outermost template argument list.
    const ArgList &getOutermost() const {
      return TemplateArgumentLists.back().Args;
    }
    ArgListsIterator begin() { return TemplateArgumentLists.begin(); }
    ConstArgListsIterator begin() const {
      return TemplateArgumentLists.begin();
    }
    ArgListsIterator end() { return TemplateArgumentLists.end(); }
    ConstArgListsIterator end() const { return TemplateArgumentLists.end(); }

    LLVM_DUMP_METHOD void dump() const {
      LangOptions LO;
      LO.CPlusPlus = true;
      LO.Bool = true;
      PrintingPolicy PP(LO);
      llvm::errs() << "NumRetainedOuterLevels: " << NumRetainedOuterLevels
                   << "\n";
      for (unsigned Depth = NumRetainedOuterLevels; Depth < getNumLevels();
           ++Depth) {
        llvm::errs() << Depth << ": ";
        printTemplateArgumentList(
            llvm::errs(),
            TemplateArgumentLists[getNumLevels() - Depth - 1].Args, PP);
        llvm::errs() << "\n";
      }
    }
  };

  /// The context in which partial ordering of function templates occurs.
  enum TPOC {
    /// Partial ordering of function templates for a function call.
    TPOC_Call,

    /// Partial ordering of function templates for a call to a
    /// conversion function.
    TPOC_Conversion,

    /// Partial ordering of function templates in other contexts, e.g.,
    /// taking the address of a function template or matching a function
    /// template specialization to a function template.
    TPOC_Other
  };

  // This is lame but unavoidable in a world without forward
  // declarations of enums.  The alternatives are to either pollute
  // Sema.h (by including this file) or sacrifice type safety (by
  // making Sema.h declare things as enums).
  class TemplatePartialOrderingContext {
    TPOC Value;

  public:
    TemplatePartialOrderingContext(TPOC Value) : Value(Value) {}

    operator TPOC() const { return Value; }
  };

  /// Captures a template argument whose value has been deduced
  /// via c++ template argument deduction.
  class DeducedTemplateArgument : public TemplateArgument {
    /// For a non-type template argument, whether the value was
    /// deduced from an array bound.
    bool DeducedFromArrayBound = false;

  public:
    DeducedTemplateArgument() = default;

    DeducedTemplateArgument(const TemplateArgument &Arg,
                            bool DeducedFromArrayBound = false)
        : TemplateArgument(Arg), DeducedFromArrayBound(DeducedFromArrayBound) {}

    /// Construct an integral non-type template argument that
    /// has been deduced, possibly from an array bound.
    DeducedTemplateArgument(ASTContext &Ctx,
                            const llvm::APSInt &Value,
                            QualType ValueType,
                            bool DeducedFromArrayBound)
        : TemplateArgument(Ctx, Value, ValueType),
          DeducedFromArrayBound(DeducedFromArrayBound) {}

    /// For a non-type template argument, determine whether the
    /// template argument was deduced from an array bound.
    bool wasDeducedFromArrayBound() const { return DeducedFromArrayBound; }

    /// Specify whether the given non-type template argument
    /// was deduced from an array bound.
    void setDeducedFromArrayBound(bool Deduced) {
      DeducedFromArrayBound = Deduced;
    }
  };

  /// A stack-allocated class that identifies which local
  /// variable declaration instantiations are present in this scope.
  ///
  /// A new instance of this class type will be created whenever we
  /// instantiate a new function declaration, which will have its own
  /// set of parameter declarations.
  class LocalInstantiationScope {
  public:
    /// A set of declarations.
    using DeclArgumentPack = SmallVector<VarDecl *, 4>;

  private:
    /// Reference to the semantic analysis that is performing
    /// this template instantiation.
    Sema &SemaRef;

    using LocalDeclsMap =
        llvm::SmallDenseMap<const Decl *,
                            llvm::PointerUnion<Decl *, DeclArgumentPack *>, 4>;

    /// A mapping from local declarations that occur
    /// within a template to their instantiations.
    ///
    /// This mapping is used during instantiation to keep track of,
    /// e.g., function parameter and variable declarations. For example,
    /// given:
    ///
    /// \code
    ///   template<typename T> T add(T x, T y) { return x + y; }
    /// \endcode
    ///
    /// when we instantiate add<int>, we will introduce a mapping from
    /// the ParmVarDecl for 'x' that occurs in the template to the
    /// instantiated ParmVarDecl for 'x'.
    ///
    /// For a parameter pack, the local instantiation scope may contain a
    /// set of instantiated parameters. This is stored as a DeclArgumentPack
    /// pointer.
    LocalDeclsMap LocalDecls;

    /// The set of argument packs we've allocated.
    SmallVector<DeclArgumentPack *, 1> ArgumentPacks;

    /// The outer scope, which contains local variable
    /// definitions from some other instantiation (that may not be
    /// relevant to this particular scope).
    LocalInstantiationScope *Outer;

    /// Whether we have already exited this scope.
    bool Exited = false;

    /// Whether to combine this scope with the outer scope, such that
    /// lookup will search our outer scope.
    bool CombineWithOuterScope;

    /// If non-NULL, the template parameter pack that has been
    /// partially substituted per C++0x [temp.arg.explicit]p9.
    NamedDecl *PartiallySubstitutedPack = nullptr;

    /// If \c PartiallySubstitutedPack is non-null, the set of
    /// explicitly-specified template arguments in that pack.
    const TemplateArgument *ArgsInPartiallySubstitutedPack;

    /// If \c PartiallySubstitutedPack, the number of
    /// explicitly-specified template arguments in
    /// ArgsInPartiallySubstitutedPack.
    unsigned NumArgsInPartiallySubstitutedPack;

  public:
    LocalInstantiationScope(Sema &SemaRef, bool CombineWithOuterScope = false)
        : SemaRef(SemaRef), Outer(SemaRef.CurrentInstantiationScope),
          CombineWithOuterScope(CombineWithOuterScope) {
      SemaRef.CurrentInstantiationScope = this;
    }

    LocalInstantiationScope(const LocalInstantiationScope &) = delete;
    LocalInstantiationScope &
    operator=(const LocalInstantiationScope &) = delete;

    ~LocalInstantiationScope() {
      Exit();
    }

    const Sema &getSema() const { return SemaRef; }

    /// Exit this local instantiation scope early.
    void Exit() {
      if (Exited)
        return;

      for (unsigned I = 0, N = ArgumentPacks.size(); I != N; ++I)
        delete ArgumentPacks[I];

      SemaRef.CurrentInstantiationScope = Outer;
      Exited = true;
    }

    /// Clone this scope, and all outer scopes, down to the given
    /// outermost scope.
    LocalInstantiationScope *cloneScopes(LocalInstantiationScope *Outermost) {
      if (this == Outermost) return this;

      // Save the current scope from SemaRef since the LocalInstantiationScope
      // will overwrite it on construction
      LocalInstantiationScope *oldScope = SemaRef.CurrentInstantiationScope;

      LocalInstantiationScope *newScope =
        new LocalInstantiationScope(SemaRef, CombineWithOuterScope);

      newScope->Outer = nullptr;
      if (Outer)
        newScope->Outer = Outer->cloneScopes(Outermost);

      newScope->PartiallySubstitutedPack = PartiallySubstitutedPack;
      newScope->ArgsInPartiallySubstitutedPack = ArgsInPartiallySubstitutedPack;
      newScope->NumArgsInPartiallySubstitutedPack =
        NumArgsInPartiallySubstitutedPack;

      for (LocalDeclsMap::iterator I = LocalDecls.begin(), E = LocalDecls.end();
           I != E; ++I) {
        const Decl *D = I->first;
        llvm::PointerUnion<Decl *, DeclArgumentPack *> &Stored =
          newScope->LocalDecls[D];
        if (I->second.is<Decl *>()) {
          Stored = I->second.get<Decl *>();
        } else {
          DeclArgumentPack *OldPack = I->second.get<DeclArgumentPack *>();
          DeclArgumentPack *NewPack = new DeclArgumentPack(*OldPack);
          Stored = NewPack;
          newScope->ArgumentPacks.push_back(NewPack);
        }
      }
      // Restore the saved scope to SemaRef
      SemaRef.CurrentInstantiationScope = oldScope;
      return newScope;
    }

    /// deletes the given scope, and all outer scopes, down to the
    /// given outermost scope.
    static void deleteScopes(LocalInstantiationScope *Scope,
                             LocalInstantiationScope *Outermost) {
      while (Scope && Scope != Outermost) {
        LocalInstantiationScope *Out = Scope->Outer;
        delete Scope;
        Scope = Out;
      }
    }

    /// Find the instantiation of the declaration D within the current
    /// instantiation scope.
    ///
    /// \param D The declaration whose instantiation we are searching for.
    ///
    /// \returns A pointer to the declaration or argument pack of declarations
    /// to which the declaration \c D is instantiated, if found. Otherwise,
    /// returns NULL.
    llvm::PointerUnion<Decl *, DeclArgumentPack *> *
    findInstantiationOf(const Decl *D);

    void InstantiatedLocal(const Decl *D, Decl *Inst);
    void InstantiatedLocalPackArg(const Decl *D, VarDecl *Inst);
    void MakeInstantiatedLocalArgPack(const Decl *D);

    /// Note that the given parameter pack has been partially substituted
    /// via explicit specification of template arguments
    /// (C++0x [temp.arg.explicit]p9).
    ///
    /// \param Pack The parameter pack, which will always be a template
    /// parameter pack.
    ///
    /// \param ExplicitArgs The explicitly-specified template arguments provided
    /// for this parameter pack.
    ///
    /// \param NumExplicitArgs The number of explicitly-specified template
    /// arguments provided for this parameter pack.
    void SetPartiallySubstitutedPack(NamedDecl *Pack,
                                     const TemplateArgument *ExplicitArgs,
                                     unsigned NumExplicitArgs);

    /// Reset the partially-substituted pack when it is no longer of
    /// interest.
    void ResetPartiallySubstitutedPack() {
      assert(PartiallySubstitutedPack && "No partially-substituted pack");
      PartiallySubstitutedPack = nullptr;
      ArgsInPartiallySubstitutedPack = nullptr;
      NumArgsInPartiallySubstitutedPack = 0;
    }

    /// Retrieve the partially-substitued template parameter pack.
    ///
    /// If there is no partially-substituted parameter pack, returns NULL.
    NamedDecl *
    getPartiallySubstitutedPack(const TemplateArgument **ExplicitArgs = nullptr,
                                unsigned *NumExplicitArgs = nullptr) const;

    /// Determine whether D is a pack expansion created in this scope.
    bool isLocalPackExpansion(const Decl *D);
  };

  class TemplateDeclInstantiator
    : public DeclVisitor<TemplateDeclInstantiator, Decl *>
  {
    Sema &SemaRef;
    Sema::ArgumentPackSubstitutionIndexRAII SubstIndex;
    DeclContext *Owner;
    const MultiLevelTemplateArgumentList &TemplateArgs;
    Sema::LateInstantiatedAttrVec* LateAttrs = nullptr;
    LocalInstantiationScope *StartingScope = nullptr;
    // Whether to evaluate the C++20 constraints or simply substitute into them.
    bool EvaluateConstraints = true;

    /// A list of out-of-line class template partial
    /// specializations that will need to be instantiated after the
    /// enclosing class's instantiation is complete.
    SmallVector<std::pair<ClassTemplateDecl *,
                                ClassTemplatePartialSpecializationDecl *>, 4>
      OutOfLinePartialSpecs;

    /// A list of out-of-line variable template partial
    /// specializations that will need to be instantiated after the
    /// enclosing variable's instantiation is complete.
    /// FIXME: Verify that this is needed.
    SmallVector<
        std::pair<VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>, 4>
    OutOfLineVarPartialSpecs;

  public:
    TemplateDeclInstantiator(Sema &SemaRef, DeclContext *Owner,
                             const MultiLevelTemplateArgumentList &TemplateArgs)
        : SemaRef(SemaRef),
          SubstIndex(SemaRef, SemaRef.ArgumentPackSubstitutionIndex),
          Owner(Owner), TemplateArgs(TemplateArgs) {}

    void setEvaluateConstraints(bool B) {
      EvaluateConstraints = B;
    }
    bool getEvaluateConstraints() {
      return EvaluateConstraints;
    }

// Define all the decl visitors using DeclNodes.inc
#define DECL(DERIVED, BASE) \
    Decl *Visit ## DERIVED ## Decl(DERIVED ## Decl *D);
#define ABSTRACT_DECL(DECL)

// Decls which never appear inside a class or function.
#define OBJCCONTAINER(DERIVED, BASE)
#define FILESCOPEASM(DERIVED, BASE)
#define TOPLEVELSTMT(DERIVED, BASE)
#define IMPORT(DERIVED, BASE)
#define EXPORT(DERIVED, BASE)
#define LINKAGESPEC(DERIVED, BASE)
#define OBJCCOMPATIBLEALIAS(DERIVED, BASE)
#define OBJCMETHOD(DERIVED, BASE)
#define OBJCTYPEPARAM(DERIVED, BASE)
#define OBJCIVAR(DERIVED, BASE)
#define OBJCPROPERTY(DERIVED, BASE)
#define OBJCPROPERTYIMPL(DERIVED, BASE)
#define EMPTY(DERIVED, BASE)
#define LIFETIMEEXTENDEDTEMPORARY(DERIVED, BASE)

    // Decls which use special-case instantiation code.
#define BLOCK(DERIVED, BASE)
#define CAPTURED(DERIVED, BASE)
#define IMPLICITPARAM(DERIVED, BASE)

#include "clang/AST/DeclNodes.inc"

    enum class RewriteKind { None, RewriteSpaceshipAsEqualEqual };

    void adjustForRewrite(RewriteKind RK, FunctionDecl *Orig, QualType &T,
                          TypeSourceInfo *&TInfo,
                          DeclarationNameInfo &NameInfo);

    // A few supplemental visitor functions.
    Decl *VisitCXXMethodDecl(CXXMethodDecl *D,
                             TemplateParameterList *TemplateParams,
                             RewriteKind RK = RewriteKind::None);
    Decl *VisitFunctionDecl(FunctionDecl *D,
                            TemplateParameterList *TemplateParams,
                            RewriteKind RK = RewriteKind::None);
    Decl *VisitDecl(Decl *D);
    Decl *VisitVarDecl(VarDecl *D, bool InstantiatingVarTemplate,
                       ArrayRef<BindingDecl *> *Bindings = nullptr);
    Decl *VisitBaseUsingDecls(BaseUsingDecl *D, BaseUsingDecl *Inst,
                              LookupResult *Lookup);

    // Enable late instantiation of attributes.  Late instantiated attributes
    // will be stored in LA.
    void enableLateAttributeInstantiation(Sema::LateInstantiatedAttrVec *LA) {
      LateAttrs = LA;
      StartingScope = SemaRef.CurrentInstantiationScope;
    }

    // Disable late instantiation of attributes.
    void disableLateAttributeInstantiation() {
      LateAttrs = nullptr;
      StartingScope = nullptr;
    }

    LocalInstantiationScope *getStartingScope() const { return StartingScope; }

    using delayed_partial_spec_iterator = SmallVectorImpl<std::pair<
      ClassTemplateDecl *, ClassTemplatePartialSpecializationDecl *>>::iterator;

    using delayed_var_partial_spec_iterator = SmallVectorImpl<std::pair<
        VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>>::iterator;

    /// Return an iterator to the beginning of the set of
    /// "delayed" partial specializations, which must be passed to
    /// InstantiateClassTemplatePartialSpecialization once the class
    /// definition has been completed.
    delayed_partial_spec_iterator delayed_partial_spec_begin() {
      return OutOfLinePartialSpecs.begin();
    }

    delayed_var_partial_spec_iterator delayed_var_partial_spec_begin() {
      return OutOfLineVarPartialSpecs.begin();
    }

    /// Return an iterator to the end of the set of
    /// "delayed" partial specializations, which must be passed to
    /// InstantiateClassTemplatePartialSpecialization once the class
    /// definition has been completed.
    delayed_partial_spec_iterator delayed_partial_spec_end() {
      return OutOfLinePartialSpecs.end();
    }

    delayed_var_partial_spec_iterator delayed_var_partial_spec_end() {
      return OutOfLineVarPartialSpecs.end();
    }

    // Helper functions for instantiating methods.
    TypeSourceInfo *SubstFunctionType(FunctionDecl *D,
                             SmallVectorImpl<ParmVarDecl *> &Params);
    bool InitFunctionInstantiation(FunctionDecl *New, FunctionDecl *Tmpl);
    bool InitMethodInstantiation(CXXMethodDecl *New, CXXMethodDecl *Tmpl);

    bool SubstDefaultedFunction(FunctionDecl *New, FunctionDecl *Tmpl);

    TemplateParameterList *
      SubstTemplateParams(TemplateParameterList *List);

    bool SubstQualifier(const DeclaratorDecl *OldDecl,
                        DeclaratorDecl *NewDecl);
    bool SubstQualifier(const TagDecl *OldDecl,
                        TagDecl *NewDecl);

    Decl *VisitVarTemplateSpecializationDecl(
        VarTemplateDecl *VarTemplate, VarDecl *FromVar,
        const TemplateArgumentListInfo &TemplateArgsInfo,
        ArrayRef<TemplateArgument> Converted,
        VarTemplateSpecializationDecl *PrevDecl = nullptr);

    Decl *InstantiateTypedefNameDecl(TypedefNameDecl *D, bool IsTypeAlias);
    Decl *InstantiateTypeAliasTemplateDecl(TypeAliasTemplateDecl *D);
    ClassTemplatePartialSpecializationDecl *
    InstantiateClassTemplatePartialSpecialization(
                                              ClassTemplateDecl *ClassTemplate,
                           ClassTemplatePartialSpecializationDecl *PartialSpec);
    VarTemplatePartialSpecializationDecl *
    InstantiateVarTemplatePartialSpecialization(
        VarTemplateDecl *VarTemplate,
        VarTemplatePartialSpecializationDecl *PartialSpec);
    void InstantiateEnumDefinition(EnumDecl *Enum, EnumDecl *Pattern);

  private:
    template<typename T>
    Decl *instantiateUnresolvedUsingDecl(T *D,
                                         bool InstantiatingPackElement = false);
  };

} // namespace clang

#endif // LLVM_CLANG_SEMA_TEMPLATE_H
