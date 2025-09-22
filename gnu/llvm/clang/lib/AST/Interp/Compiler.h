//===--- Compiler.h - Code generator for expressions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the constexpr bytecode compiler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_BYTECODEEXPRGEN_H
#define LLVM_CLANG_AST_INTERP_BYTECODEEXPRGEN_H

#include "ByteCodeEmitter.h"
#include "EvalEmitter.h"
#include "Pointer.h"
#include "PrimType.h"
#include "Record.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/TargetInfo.h"

namespace clang {
class QualType;

namespace interp {

template <class Emitter> class LocalScope;
template <class Emitter> class DestructorScope;
template <class Emitter> class VariableScope;
template <class Emitter> class DeclScope;
template <class Emitter> class InitLinkScope;
template <class Emitter> class InitStackScope;
template <class Emitter> class OptionScope;
template <class Emitter> class ArrayIndexScope;
template <class Emitter> class SourceLocScope;
template <class Emitter> class LoopScope;
template <class Emitter> class LabelScope;
template <class Emitter> class SwitchScope;
template <class Emitter> class StmtExprScope;

template <class Emitter> class Compiler;
struct InitLink {
public:
  enum {
    K_This = 0,
    K_Field = 1,
    K_Temp = 2,
    K_Decl = 3,
  };

  static InitLink This() { return InitLink{K_This}; }
  static InitLink Field(unsigned Offset) {
    InitLink IL{K_Field};
    IL.Offset = Offset;
    return IL;
  }
  static InitLink Temp(unsigned Offset) {
    InitLink IL{K_Temp};
    IL.Offset = Offset;
    return IL;
  }
  static InitLink Decl(const ValueDecl *D) {
    InitLink IL{K_Decl};
    IL.D = D;
    return IL;
  }

  InitLink(uint8_t Kind) : Kind(Kind) {}
  template <class Emitter>
  bool emit(Compiler<Emitter> *Ctx, const Expr *E) const;

  uint32_t Kind;
  union {
    unsigned Offset;
    const ValueDecl *D;
  };
};

/// State encapsulating if a the variable creation has been successful,
/// unsuccessful, or no variable has been created at all.
struct VarCreationState {
  std::optional<bool> S = std::nullopt;
  VarCreationState() = default;
  VarCreationState(bool b) : S(b) {}
  static VarCreationState NotCreated() { return VarCreationState(); }

  operator bool() const { return S && *S; }
  bool notCreated() const { return !S; }
};

/// Compilation context for expressions.
template <class Emitter>
class Compiler : public ConstStmtVisitor<Compiler<Emitter>, bool>,
                 public Emitter {
protected:
  // Aliases for types defined in the emitter.
  using LabelTy = typename Emitter::LabelTy;
  using AddrTy = typename Emitter::AddrTy;
  using OptLabelTy = std::optional<LabelTy>;
  using CaseMap = llvm::DenseMap<const SwitchCase *, LabelTy>;

  /// Current compilation context.
  Context &Ctx;
  /// Program to link to.
  Program &P;

public:
  /// Initializes the compiler and the backend emitter.
  template <typename... Tys>
  Compiler(Context &Ctx, Program &P, Tys &&...Args)
      : Emitter(Ctx, P, Args...), Ctx(Ctx), P(P) {}

  // Expressions.
  bool VisitCastExpr(const CastExpr *E);
  bool VisitIntegerLiteral(const IntegerLiteral *E);
  bool VisitFloatingLiteral(const FloatingLiteral *E);
  bool VisitImaginaryLiteral(const ImaginaryLiteral *E);
  bool VisitParenExpr(const ParenExpr *E);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitLogicalBinOp(const BinaryOperator *E);
  bool VisitPointerArithBinOp(const BinaryOperator *E);
  bool VisitComplexBinOp(const BinaryOperator *E);
  bool VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *E);
  bool VisitCallExpr(const CallExpr *E);
  bool VisitBuiltinCallExpr(const CallExpr *E);
  bool VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *E);
  bool VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *E);
  bool VisitCXXNullPtrLiteralExpr(const CXXNullPtrLiteralExpr *E);
  bool VisitGNUNullExpr(const GNUNullExpr *E);
  bool VisitCXXThisExpr(const CXXThisExpr *E);
  bool VisitUnaryOperator(const UnaryOperator *E);
  bool VisitComplexUnaryOperator(const UnaryOperator *E);
  bool VisitDeclRefExpr(const DeclRefExpr *E);
  bool VisitImplicitValueInitExpr(const ImplicitValueInitExpr *E);
  bool VisitSubstNonTypeTemplateParmExpr(const SubstNonTypeTemplateParmExpr *E);
  bool VisitArraySubscriptExpr(const ArraySubscriptExpr *E);
  bool VisitInitListExpr(const InitListExpr *E);
  bool VisitCXXParenListInitExpr(const CXXParenListInitExpr *E);
  bool VisitConstantExpr(const ConstantExpr *E);
  bool VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E);
  bool VisitMemberExpr(const MemberExpr *E);
  bool VisitArrayInitIndexExpr(const ArrayInitIndexExpr *E);
  bool VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E);
  bool VisitOpaqueValueExpr(const OpaqueValueExpr *E);
  bool VisitAbstractConditionalOperator(const AbstractConditionalOperator *E);
  bool VisitStringLiteral(const StringLiteral *E);
  bool VisitObjCStringLiteral(const ObjCStringLiteral *E);
  bool VisitObjCEncodeExpr(const ObjCEncodeExpr *E);
  bool VisitSYCLUniqueStableNameExpr(const SYCLUniqueStableNameExpr *E);
  bool VisitCharacterLiteral(const CharacterLiteral *E);
  bool VisitCompoundAssignOperator(const CompoundAssignOperator *E);
  bool VisitFloatCompoundAssignOperator(const CompoundAssignOperator *E);
  bool VisitPointerCompoundAssignOperator(const CompoundAssignOperator *E);
  bool VisitExprWithCleanups(const ExprWithCleanups *E);
  bool VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *E);
  bool VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *E);
  bool VisitCompoundLiteralExpr(const CompoundLiteralExpr *E);
  bool VisitTypeTraitExpr(const TypeTraitExpr *E);
  bool VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *E);
  bool VisitLambdaExpr(const LambdaExpr *E);
  bool VisitPredefinedExpr(const PredefinedExpr *E);
  bool VisitCXXThrowExpr(const CXXThrowExpr *E);
  bool VisitCXXReinterpretCastExpr(const CXXReinterpretCastExpr *E);
  bool VisitCXXNoexceptExpr(const CXXNoexceptExpr *E);
  bool VisitCXXConstructExpr(const CXXConstructExpr *E);
  bool VisitSourceLocExpr(const SourceLocExpr *E);
  bool VisitOffsetOfExpr(const OffsetOfExpr *E);
  bool VisitCXXScalarValueInitExpr(const CXXScalarValueInitExpr *E);
  bool VisitSizeOfPackExpr(const SizeOfPackExpr *E);
  bool VisitGenericSelectionExpr(const GenericSelectionExpr *E);
  bool VisitChooseExpr(const ChooseExpr *E);
  bool VisitEmbedExpr(const EmbedExpr *E);
  bool VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *E);
  bool VisitCXXInheritedCtorInitExpr(const CXXInheritedCtorInitExpr *E);
  bool VisitExpressionTraitExpr(const ExpressionTraitExpr *E);
  bool VisitCXXUuidofExpr(const CXXUuidofExpr *E);
  bool VisitRequiresExpr(const RequiresExpr *E);
  bool VisitConceptSpecializationExpr(const ConceptSpecializationExpr *E);
  bool VisitCXXRewrittenBinaryOperator(const CXXRewrittenBinaryOperator *E);
  bool VisitPseudoObjectExpr(const PseudoObjectExpr *E);
  bool VisitPackIndexingExpr(const PackIndexingExpr *E);
  bool VisitRecoveryExpr(const RecoveryExpr *E);
  bool VisitAddrLabelExpr(const AddrLabelExpr *E);
  bool VisitConvertVectorExpr(const ConvertVectorExpr *E);
  bool VisitShuffleVectorExpr(const ShuffleVectorExpr *E);
  bool VisitExtVectorElementExpr(const ExtVectorElementExpr *E);
  bool VisitObjCBoxedExpr(const ObjCBoxedExpr *E);
  bool VisitCXXStdInitializerListExpr(const CXXStdInitializerListExpr *E);
  bool VisitStmtExpr(const StmtExpr *E);
  bool VisitCXXNewExpr(const CXXNewExpr *E);
  bool VisitCXXDeleteExpr(const CXXDeleteExpr *E);

  // Statements.
  bool visitCompoundStmt(const CompoundStmt *S);
  bool visitLoopBody(const Stmt *S);
  bool visitDeclStmt(const DeclStmt *DS);
  bool visitReturnStmt(const ReturnStmt *RS);
  bool visitIfStmt(const IfStmt *IS);
  bool visitWhileStmt(const WhileStmt *S);
  bool visitDoStmt(const DoStmt *S);
  bool visitForStmt(const ForStmt *S);
  bool visitCXXForRangeStmt(const CXXForRangeStmt *S);
  bool visitBreakStmt(const BreakStmt *S);
  bool visitContinueStmt(const ContinueStmt *S);
  bool visitSwitchStmt(const SwitchStmt *S);
  bool visitCaseStmt(const CaseStmt *S);
  bool visitDefaultStmt(const DefaultStmt *S);
  bool visitAttributedStmt(const AttributedStmt *S);
  bool visitCXXTryStmt(const CXXTryStmt *S);

protected:
  bool visitStmt(const Stmt *S);
  bool visitExpr(const Expr *E) override;
  bool visitFunc(const FunctionDecl *F) override;

  bool visitDeclAndReturn(const VarDecl *VD, bool ConstantContext) override;

protected:
  /// Emits scope cleanup instructions.
  void emitCleanup();

  /// Returns a record type from a record or pointer type.
  const RecordType *getRecordTy(QualType Ty);

  /// Returns a record from a record or pointer type.
  Record *getRecord(QualType Ty);
  Record *getRecord(const RecordDecl *RD);

  /// Returns a function for the given FunctionDecl.
  /// If the function does not exist yet, it is compiled.
  const Function *getFunction(const FunctionDecl *FD);

  std::optional<PrimType> classify(const Expr *E) const {
    return Ctx.classify(E);
  }
  std::optional<PrimType> classify(QualType Ty) const {
    return Ctx.classify(Ty);
  }

  /// Classifies a known primitive type.
  PrimType classifyPrim(QualType Ty) const {
    if (auto T = classify(Ty)) {
      return *T;
    }
    llvm_unreachable("not a primitive type");
  }
  /// Classifies a known primitive expression.
  PrimType classifyPrim(const Expr *E) const {
    if (auto T = classify(E))
      return *T;
    llvm_unreachable("not a primitive type");
  }

  /// Evaluates an expression and places the result on the stack. If the
  /// expression is of composite type, a local variable will be created
  /// and a pointer to said variable will be placed on the stack.
  bool visit(const Expr *E);
  /// Compiles an initializer. This is like visit() but it will never
  /// create a variable and instead rely on a variable already having
  /// been created. visitInitializer() then relies on a pointer to this
  /// variable being on top of the stack.
  bool visitInitializer(const Expr *E);
  /// Evaluates an expression for side effects and discards the result.
  bool discard(const Expr *E);
  /// Just pass evaluation on to \p E. This leaves all the parsing flags
  /// intact.
  bool delegate(const Expr *E);
  /// Creates and initializes a variable from the given decl.
  VarCreationState visitVarDecl(const VarDecl *VD, bool Toplevel = false);
  VarCreationState visitDecl(const VarDecl *VD);
  /// Visit an APValue.
  bool visitAPValue(const APValue &Val, PrimType ValType, const Expr *E);
  bool visitAPValueInitializer(const APValue &Val, const Expr *E);
  /// Visit the given decl as if we have a reference to it.
  bool visitDeclRef(const ValueDecl *D, const Expr *E);

  /// Visits an expression and converts it to a boolean.
  bool visitBool(const Expr *E);

  bool visitInitList(ArrayRef<const Expr *> Inits, const Expr *ArrayFiller,
                     const Expr *E);
  bool visitArrayElemInit(unsigned ElemIndex, const Expr *Init);

  /// Creates a local primitive value.
  unsigned allocateLocalPrimitive(DeclTy &&Decl, PrimType Ty, bool IsConst,
                                  bool IsExtended = false);

  /// Allocates a space storing a local given its type.
  std::optional<unsigned>
  allocateLocal(DeclTy &&Decl, const ValueDecl *ExtendingDecl = nullptr);

private:
  friend class VariableScope<Emitter>;
  friend class LocalScope<Emitter>;
  friend class DestructorScope<Emitter>;
  friend class DeclScope<Emitter>;
  friend class InitLinkScope<Emitter>;
  friend class InitStackScope<Emitter>;
  friend class OptionScope<Emitter>;
  friend class ArrayIndexScope<Emitter>;
  friend class SourceLocScope<Emitter>;
  friend struct InitLink;
  friend class LoopScope<Emitter>;
  friend class LabelScope<Emitter>;
  friend class SwitchScope<Emitter>;
  friend class StmtExprScope<Emitter>;

  /// Emits a zero initializer.
  bool visitZeroInitializer(PrimType T, QualType QT, const Expr *E);
  bool visitZeroRecordInitializer(const Record *R, const Expr *E);

  /// Emits an APSInt constant.
  bool emitConst(const llvm::APSInt &Value, PrimType Ty, const Expr *E);
  bool emitConst(const llvm::APSInt &Value, const Expr *E);
  bool emitConst(const llvm::APInt &Value, const Expr *E) {
    return emitConst(static_cast<llvm::APSInt>(Value), E);
  }

  /// Emits an integer constant.
  template <typename T> bool emitConst(T Value, PrimType Ty, const Expr *E);
  template <typename T> bool emitConst(T Value, const Expr *E);

  llvm::RoundingMode getRoundingMode(const Expr *E) const {
    FPOptions FPO = E->getFPFeaturesInEffect(Ctx.getLangOpts());

    if (FPO.getRoundingMode() == llvm::RoundingMode::Dynamic)
      return llvm::RoundingMode::NearestTiesToEven;

    return FPO.getRoundingMode();
  }

  bool emitPrimCast(PrimType FromT, PrimType ToT, QualType ToQT, const Expr *E);
  PrimType classifyComplexElementType(QualType T) const {
    assert(T->isAnyComplexType());

    QualType ElemType = T->getAs<ComplexType>()->getElementType();

    return *this->classify(ElemType);
  }

  bool emitComplexReal(const Expr *SubExpr);
  bool emitComplexBoolCast(const Expr *E);
  bool emitComplexComparison(const Expr *LHS, const Expr *RHS,
                             const BinaryOperator *E);

  bool emitRecordDestruction(const Record *R);
  bool emitDestruction(const Descriptor *Desc);
  unsigned collectBaseOffset(const QualType BaseType,
                             const QualType DerivedType);
  bool emitLambdaStaticInvokerBody(const CXXMethodDecl *MD);

protected:
  /// Variable to storage mapping.
  llvm::DenseMap<const ValueDecl *, Scope::Local> Locals;

  /// OpaqueValueExpr to location mapping.
  llvm::DenseMap<const OpaqueValueExpr *, unsigned> OpaqueExprs;

  /// Current scope.
  VariableScope<Emitter> *VarScope = nullptr;

  /// Current argument index. Needed to emit ArrayInitIndexExpr.
  std::optional<uint64_t> ArrayIndex;

  /// DefaultInit- or DefaultArgExpr, needed for SourceLocExpr.
  const Expr *SourceLocDefaultExpr = nullptr;

  /// Flag indicating if return value is to be discarded.
  bool DiscardResult = false;

  bool InStmtExpr = false;

  /// Flag inidicating if we're initializing an already created
  /// variable. This is set in visitInitializer().
  bool Initializing = false;
  const ValueDecl *InitializingDecl = nullptr;

  llvm::SmallVector<InitLink> InitStack;
  bool InitStackActive = false;

  /// Flag indicating if we're initializing a global variable.
  bool GlobalDecl = false;

  /// Type of the expression returned by the function.
  std::optional<PrimType> ReturnType;

  /// Switch case mapping.
  CaseMap CaseLabels;

  /// Point to break to.
  OptLabelTy BreakLabel;
  /// Point to continue to.
  OptLabelTy ContinueLabel;
  /// Default case label.
  OptLabelTy DefaultLabel;
};

extern template class Compiler<ByteCodeEmitter>;
extern template class Compiler<EvalEmitter>;

/// Scope chain managing the variable lifetimes.
template <class Emitter> class VariableScope {
public:
  VariableScope(Compiler<Emitter> *Ctx, const ValueDecl *VD)
      : Ctx(Ctx), Parent(Ctx->VarScope), ValDecl(VD) {
    Ctx->VarScope = this;
  }

  virtual ~VariableScope() { Ctx->VarScope = this->Parent; }

  void add(const Scope::Local &Local, bool IsExtended) {
    if (IsExtended)
      this->addExtended(Local);
    else
      this->addLocal(Local);
  }

  virtual void addLocal(const Scope::Local &Local) {
    if (this->Parent)
      this->Parent->addLocal(Local);
  }

  virtual void addExtended(const Scope::Local &Local) {
    if (this->Parent)
      this->Parent->addExtended(Local);
  }

  void addExtended(const Scope::Local &Local, const ValueDecl *ExtendingDecl) {
    // Walk up the chain of scopes until we find the one for ExtendingDecl.
    // If there is no such scope, attach it to the parent one.
    VariableScope *P = this;
    while (P) {
      if (P->ValDecl == ExtendingDecl) {
        P->addLocal(Local);
        return;
      }
      P = P->Parent;
      if (!P)
        break;
    }

    // Use the parent scope.
    addExtended(Local);
  }

  virtual void emitDestruction() {}
  virtual bool emitDestructors() { return true; }
  VariableScope *getParent() const { return Parent; }

protected:
  /// Compiler instance.
  Compiler<Emitter> *Ctx;
  /// Link to the parent scope.
  VariableScope *Parent;
  const ValueDecl *ValDecl = nullptr;
};

/// Generic scope for local variables.
template <class Emitter> class LocalScope : public VariableScope<Emitter> {
public:
  LocalScope(Compiler<Emitter> *Ctx) : VariableScope<Emitter>(Ctx, nullptr) {}
  LocalScope(Compiler<Emitter> *Ctx, const ValueDecl *VD)
      : VariableScope<Emitter>(Ctx, VD) {}

  /// Emit a Destroy op for this scope.
  ~LocalScope() override {
    if (!Idx)
      return;
    this->Ctx->emitDestroy(*Idx, SourceInfo{});
    removeStoredOpaqueValues();
  }

  /// Overriden to support explicit destruction.
  void emitDestruction() override { destroyLocals(); }

  /// Explicit destruction of local variables.
  bool destroyLocals() {
    if (!Idx)
      return true;

    bool Success = this->emitDestructors();
    this->Ctx->emitDestroy(*Idx, SourceInfo{});
    removeStoredOpaqueValues();
    this->Idx = std::nullopt;
    return Success;
  }

  void addLocal(const Scope::Local &Local) override {
    if (!Idx) {
      Idx = this->Ctx->Descriptors.size();
      this->Ctx->Descriptors.emplace_back();
    }

    this->Ctx->Descriptors[*Idx].emplace_back(Local);
  }

  bool emitDestructors() override {
    if (!Idx)
      return true;
    // Emit destructor calls for local variables of record
    // type with a destructor.
    for (Scope::Local &Local : this->Ctx->Descriptors[*Idx]) {
      if (!Local.Desc->isPrimitive() && !Local.Desc->isPrimitiveArray()) {
        if (!this->Ctx->emitGetPtrLocal(Local.Offset, SourceInfo{}))
          return false;

        if (!this->Ctx->emitDestruction(Local.Desc))
          return false;

        if (!this->Ctx->emitPopPtr(SourceInfo{}))
          return false;
        removeIfStoredOpaqueValue(Local);
      }
    }
    return true;
  }

  void removeStoredOpaqueValues() {
    if (!Idx)
      return;

    for (const Scope::Local &Local : this->Ctx->Descriptors[*Idx]) {
      removeIfStoredOpaqueValue(Local);
    }
  }

  void removeIfStoredOpaqueValue(const Scope::Local &Local) {
    if (const auto *OVE =
            llvm::dyn_cast_if_present<OpaqueValueExpr>(Local.Desc->asExpr())) {
      if (auto It = this->Ctx->OpaqueExprs.find(OVE);
          It != this->Ctx->OpaqueExprs.end())
        this->Ctx->OpaqueExprs.erase(It);
    };
  }

  /// Index of the scope in the chain.
  std::optional<unsigned> Idx;
};

/// Emits the destructors of the variables of \param OtherScope
/// when this scope is destroyed. Does not create a Scope in the bytecode at
/// all, this is just a RAII object to emit destructors.
template <class Emitter> class DestructorScope final {
public:
  DestructorScope(LocalScope<Emitter> &OtherScope) : OtherScope(OtherScope) {}

  ~DestructorScope() { OtherScope.emitDestructors(); }

private:
  LocalScope<Emitter> &OtherScope;
};

/// Scope for storage declared in a compound statement.
template <class Emitter> class BlockScope final : public LocalScope<Emitter> {
public:
  BlockScope(Compiler<Emitter> *Ctx) : LocalScope<Emitter>(Ctx) {}

  void addExtended(const Scope::Local &Local) override {
    // If we to this point, just add the variable as a normal local
    // variable. It will be destroyed at the end of the block just
    // like all others.
    this->addLocal(Local);
  }
};

template <class Emitter> class ArrayIndexScope final {
public:
  ArrayIndexScope(Compiler<Emitter> *Ctx, uint64_t Index) : Ctx(Ctx) {
    OldArrayIndex = Ctx->ArrayIndex;
    Ctx->ArrayIndex = Index;
  }

  ~ArrayIndexScope() { Ctx->ArrayIndex = OldArrayIndex; }

private:
  Compiler<Emitter> *Ctx;
  std::optional<uint64_t> OldArrayIndex;
};

template <class Emitter> class SourceLocScope final {
public:
  SourceLocScope(Compiler<Emitter> *Ctx, const Expr *DefaultExpr) : Ctx(Ctx) {
    assert(DefaultExpr);
    // We only switch if the current SourceLocDefaultExpr is null.
    if (!Ctx->SourceLocDefaultExpr) {
      Enabled = true;
      Ctx->SourceLocDefaultExpr = DefaultExpr;
    }
  }

  ~SourceLocScope() {
    if (Enabled)
      Ctx->SourceLocDefaultExpr = nullptr;
  }

private:
  Compiler<Emitter> *Ctx;
  bool Enabled = false;
};

template <class Emitter> class InitLinkScope final {
public:
  InitLinkScope(Compiler<Emitter> *Ctx, InitLink &&Link) : Ctx(Ctx) {
    Ctx->InitStack.push_back(std::move(Link));
  }

  ~InitLinkScope() { this->Ctx->InitStack.pop_back(); }

private:
  Compiler<Emitter> *Ctx;
};

template <class Emitter> class InitStackScope final {
public:
  InitStackScope(Compiler<Emitter> *Ctx, bool Active)
      : Ctx(Ctx), OldValue(Ctx->InitStackActive) {
    Ctx->InitStackActive = Active;
  }

  ~InitStackScope() { this->Ctx->InitStackActive = OldValue; }

private:
  Compiler<Emitter> *Ctx;
  bool OldValue;
};

} // namespace interp
} // namespace clang

#endif
