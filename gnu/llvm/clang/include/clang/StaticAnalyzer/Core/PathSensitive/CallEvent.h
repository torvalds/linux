//===- CallEvent.h - Wrapper for all function and method calls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file defines CallEvent and its subclasses, which represent path-
/// sensitive instances of different kinds of function and method calls
/// (C, C++, and Objective-C).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CALLEVENT_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CALLEVENT_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <limits>
#include <optional>
#include <utility>

namespace clang {

class LocationContext;
class ProgramPoint;
class ProgramPointTag;
class StackFrameContext;

namespace ento {

enum CallEventKind {
  CE_Function,
  CE_CXXStaticOperator,
  CE_CXXMember,
  CE_CXXMemberOperator,
  CE_CXXDestructor,
  CE_BEG_CXX_INSTANCE_CALLS = CE_CXXMember,
  CE_END_CXX_INSTANCE_CALLS = CE_CXXDestructor,
  CE_CXXConstructor,
  CE_CXXInheritedConstructor,
  CE_BEG_CXX_CONSTRUCTOR_CALLS = CE_CXXConstructor,
  CE_END_CXX_CONSTRUCTOR_CALLS = CE_CXXInheritedConstructor,
  CE_CXXAllocator,
  CE_CXXDeallocator,
  CE_BEG_FUNCTION_CALLS = CE_Function,
  CE_END_FUNCTION_CALLS = CE_CXXDeallocator,
  CE_Block,
  CE_ObjCMessage
};

class CallEvent;

template <typename T = CallEvent>
class CallEventRef : public IntrusiveRefCntPtr<const T> {
public:
  CallEventRef(const T *Call) : IntrusiveRefCntPtr<const T>(Call) {}
  CallEventRef(const CallEventRef &Orig) : IntrusiveRefCntPtr<const T>(Orig) {}

  // The copy assignment operator is defined as deleted pending further
  // motivation.
  CallEventRef &operator=(const CallEventRef &) = delete;

  CallEventRef<T> cloneWithState(ProgramStateRef State) const {
    return this->get()->template cloneWithState<T>(State);
  }

  // Allow implicit conversions to a superclass type, since CallEventRef
  // behaves like a pointer-to-const.
  template <typename SuperT> operator CallEventRef<SuperT>() const {
    return this->get();
  }
};

/// \class RuntimeDefinition
/// Defines the runtime definition of the called function.
///
/// Encapsulates the information we have about which Decl will be used
/// when the call is executed on the given path. When dealing with dynamic
/// dispatch, the information is based on DynamicTypeInfo and might not be
/// precise.
class RuntimeDefinition {
  /// The Declaration of the function which could be called at runtime.
  /// NULL if not available.
  const Decl *D = nullptr;

  /// The region representing an object (ObjC/C++) on which the method is
  /// called. With dynamic dispatch, the method definition depends on the
  /// runtime type of this object. NULL when the DynamicTypeInfo is
  /// precise.
  const MemRegion *R = nullptr;

  /// A definition is foreign if it has been imported and newly created by the
  /// ASTImporter. This can be true only if CTU is enabled.
  const bool Foreign = false;

public:
  RuntimeDefinition() = default;
  RuntimeDefinition(const Decl *InD) : D(InD) {}
  RuntimeDefinition(const Decl *InD, bool Foreign) : D(InD), Foreign(Foreign) {}
  RuntimeDefinition(const Decl *InD, const MemRegion *InR) : D(InD), R(InR) {}

  const Decl *getDecl() { return D; }
  bool isForeign() const { return Foreign; }

  /// Check if the definition we have is precise.
  /// If not, it is possible that the call dispatches to another definition at
  /// execution time.
  bool mayHaveOtherDefinitions() { return R != nullptr; }

  /// When other definitions are possible, returns the region whose runtime type
  /// determines the method definition.
  const MemRegion *getDispatchRegion() { return R; }
};

/// Represents an abstract call to a function or method along a
/// particular path.
///
/// CallEvents are created through the factory methods of CallEventManager.
///
/// CallEvents should always be cheap to create and destroy. In order for
/// CallEventManager to be able to re-use CallEvent-sized memory blocks,
/// subclasses of CallEvent may not add any data members to the base class.
/// Use the "Data" and "Location" fields instead.
class CallEvent {
public:
  using Kind = CallEventKind;

private:
  ProgramStateRef State;
  const LocationContext *LCtx;
  llvm::PointerUnion<const Expr *, const Decl *> Origin;
  CFGBlock::ConstCFGElementRef ElemRef = {nullptr, 0};
  mutable std::optional<bool> Foreign; // Set by CTU analysis.

protected:
  // This is user data for subclasses.
  const void *Data;

  // This is user data for subclasses.
  // This should come right before RefCount, so that the two fields can be
  // packed together on LP64 platforms.
  SourceLocation Location;

private:
  template <typename T> friend struct llvm::IntrusiveRefCntPtrInfo;

  mutable unsigned RefCount = 0;

  void Retain() const { ++RefCount; }
  void Release() const;

protected:
  friend class CallEventManager;

  CallEvent(const Expr *E, ProgramStateRef state, const LocationContext *lctx,
            CFGBlock::ConstCFGElementRef ElemRef)
      : State(std::move(state)), LCtx(lctx), Origin(E), ElemRef(ElemRef) {}

  CallEvent(const Decl *D, ProgramStateRef state, const LocationContext *lctx,
            CFGBlock::ConstCFGElementRef ElemRef)
      : State(std::move(state)), LCtx(lctx), Origin(D), ElemRef(ElemRef) {}

  // DO NOT MAKE PUBLIC
  CallEvent(const CallEvent &Original)
      : State(Original.State), LCtx(Original.LCtx), Origin(Original.Origin),
        ElemRef(Original.ElemRef), Data(Original.Data),
        Location(Original.Location) {}

  /// Copies this CallEvent, with vtable intact, into a new block of memory.
  virtual void cloneTo(void *Dest) const = 0;

  /// Get the value of arbitrary expressions at this point in the path.
  SVal getSVal(const Stmt *S) const {
    return getState()->getSVal(S, getLocationContext());
  }

  using ValueList = SmallVectorImpl<SVal>;

  /// Used to specify non-argument regions that will be invalidated as a
  /// result of this call.
  virtual void
  getExtraInvalidatedValues(ValueList &Values,
                            RegionAndSymbolInvalidationTraits *ETraits) const {}

public:
  CallEvent &operator=(const CallEvent &) = delete;
  virtual ~CallEvent() = default;

  /// Returns the kind of call this is.
  virtual Kind getKind() const = 0;
  virtual StringRef getKindAsString() const = 0;

  /// Returns the declaration of the function or method that will be
  /// called. May be null.
  virtual const Decl *getDecl() const {
    return Origin.dyn_cast<const Decl *>();
  }

  bool isForeign() const {
    assert(Foreign && "Foreign must be set before querying");
    return *Foreign;
  }
  void setForeign(bool B) const { Foreign = B; }

  /// The state in which the call is being evaluated.
  const ProgramStateRef &getState() const { return State; }

  /// The context in which the call is being evaluated.
  const LocationContext *getLocationContext() const { return LCtx; }

  const CFGBlock::ConstCFGElementRef &getCFGElementRef() const {
    return ElemRef;
  }

  /// Returns the definition of the function or method that will be
  /// called.
  virtual RuntimeDefinition getRuntimeDefinition() const = 0;

  /// Returns the expression whose value will be the result of this call.
  /// May be null.
  virtual const Expr *getOriginExpr() const {
    return Origin.dyn_cast<const Expr *>();
  }

  /// Returns the number of arguments (explicit and implicit).
  ///
  /// Note that this may be greater than the number of parameters in the
  /// callee's declaration, and that it may include arguments not written in
  /// the source.
  virtual unsigned getNumArgs() const = 0;

  /// Returns true if the callee is known to be from a system header.
  bool isInSystemHeader() const {
    const Decl *D = getDecl();
    if (!D)
      return false;

    SourceLocation Loc = D->getLocation();
    if (Loc.isValid()) {
      const SourceManager &SM =
          getState()->getStateManager().getContext().getSourceManager();
      return SM.isInSystemHeader(D->getLocation());
    }

    // Special case for implicitly-declared global operator new/delete.
    // These should be considered system functions.
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      return FD->isOverloadedOperator() && FD->isImplicit() && FD->isGlobal();

    return false;
  }

  /// Returns a source range for the entire call, suitable for
  /// outputting in diagnostics.
  virtual SourceRange getSourceRange() const {
    return getOriginExpr()->getSourceRange();
  }

  /// Returns the value of a given argument at the time of the call.
  virtual SVal getArgSVal(unsigned Index) const;

  /// Returns the expression associated with a given argument.
  /// May be null if this expression does not appear in the source.
  virtual const Expr *getArgExpr(unsigned Index) const { return nullptr; }

  /// Returns the source range for errors associated with this argument.
  ///
  /// May be invalid if the argument is not written in the source.
  virtual SourceRange getArgSourceRange(unsigned Index) const;

  /// Returns the result type, adjusted for references.
  QualType getResultType() const;

  /// Returns the return value of the call.
  ///
  /// This should only be called if the CallEvent was created using a state in
  /// which the return value has already been bound to the origin expression.
  SVal getReturnValue() const;

  /// Returns true if the type of any of the non-null arguments satisfies
  /// the condition.
  bool hasNonNullArgumentsWithType(bool (*Condition)(QualType)) const;

  /// Returns true if any of the arguments appear to represent callbacks.
  bool hasNonZeroCallbackArg() const;

  /// Returns true if any of the arguments is void*.
  bool hasVoidPointerToNonConstArg() const;

  /// Returns true if any of the arguments are known to escape to long-
  /// term storage, even if this method will not modify them.
  // NOTE: The exact semantics of this are still being defined!
  // We don't really want a list of hardcoded exceptions in the long run,
  // but we don't want duplicated lists of known APIs in the short term either.
  virtual bool argumentsMayEscape() const { return hasNonZeroCallbackArg(); }

  /// Returns true if the callee is an externally-visible function in the
  /// top-level namespace, such as \c malloc.
  ///
  /// You can use this call to determine that a particular function really is
  /// a library function and not, say, a C++ member function with the same name.
  ///
  /// If a name is provided, the function must additionally match the given
  /// name.
  ///
  /// Note that this deliberately excludes C++ library functions in the \c std
  /// namespace, but will include C library functions accessed through the
  /// \c std namespace. This also does not check if the function is declared
  /// as 'extern "C"', or if it uses C++ name mangling.
  // FIXME: Add a helper for checking namespaces.
  // FIXME: Move this down to AnyFunctionCall once checkers have more
  // precise callbacks.
  bool isGlobalCFunction(StringRef SpecificName = StringRef()) const;

  /// Returns the name of the callee, if its name is a simple identifier.
  ///
  /// Note that this will fail for Objective-C methods, blocks, and C++
  /// overloaded operators. The former is named by a Selector rather than a
  /// simple identifier, and the latter two do not have names.
  // FIXME: Move this down to AnyFunctionCall once checkers have more
  // precise callbacks.
  const IdentifierInfo *getCalleeIdentifier() const {
    const auto *ND = dyn_cast_or_null<NamedDecl>(getDecl());
    if (!ND)
      return nullptr;
    return ND->getIdentifier();
  }

  /// Returns an appropriate ProgramPoint for this call.
  ProgramPoint getProgramPoint(bool IsPreVisit = false,
                               const ProgramPointTag *Tag = nullptr) const;

  /// Returns a new state with all argument regions invalidated.
  ///
  /// This accepts an alternate state in case some processing has already
  /// occurred.
  ProgramStateRef invalidateRegions(unsigned BlockCount,
                                    ProgramStateRef Orig = nullptr) const;

  using FrameBindingTy = std::pair<SVal, SVal>;
  using BindingsTy = SmallVectorImpl<FrameBindingTy>;

  /// Populates the given SmallVector with the bindings in the callee's stack
  /// frame at the start of this call.
  virtual void getInitialStackFrameContents(const StackFrameContext *CalleeCtx,
                                            BindingsTy &Bindings) const = 0;

  /// Returns a copy of this CallEvent, but using the given state.
  template <typename T>
  CallEventRef<T> cloneWithState(ProgramStateRef NewState) const;

  /// Returns a copy of this CallEvent, but using the given state.
  CallEventRef<> cloneWithState(ProgramStateRef NewState) const {
    return cloneWithState<CallEvent>(NewState);
  }

  /// Returns true if this is a statement is a function or method call
  /// of some kind.
  static bool isCallStmt(const Stmt *S);

  /// Returns the result type of a function or method declaration.
  ///
  /// This will return a null QualType if the result type cannot be determined.
  static QualType getDeclaredResultType(const Decl *D);

  /// Returns true if the given decl is known to be variadic.
  ///
  /// \p D must not be null.
  static bool isVariadic(const Decl *D);

  /// Returns AnalysisDeclContext for the callee stack frame.
  /// Currently may fail; returns null on failure.
  AnalysisDeclContext *getCalleeAnalysisDeclContext() const;

  /// Returns the callee stack frame. That stack frame will only be entered
  /// during analysis if the call is inlined, but it may still be useful
  /// in intermediate calculations even if the call isn't inlined.
  /// May fail; returns null on failure.
  const StackFrameContext *getCalleeStackFrame(unsigned BlockCount) const;

  /// Returns memory location for a parameter variable within the callee stack
  /// frame. The behavior is undefined if the block count is different from the
  /// one that is there when call happens. May fail; returns null on failure.
  const ParamVarRegion *getParameterLocation(unsigned Index,
                                             unsigned BlockCount) const;

  /// Returns true if on the current path, the argument was constructed by
  /// calling a C++ constructor over it. This is an internal detail of the
  /// analysis which doesn't necessarily represent the program semantics:
  /// if we are supposed to construct an argument directly, we may still
  /// not do that because we don't know how (i.e., construction context is
  /// unavailable in the CFG or not supported by the analyzer).
  bool isArgumentConstructedDirectly(unsigned Index) const {
    // This assumes that the object was not yet removed from the state.
    return ExprEngine::getObjectUnderConstruction(
               getState(), {getOriginExpr(), Index}, getLocationContext())
        .has_value();
  }

  /// Some calls have parameter numbering mismatched from argument numbering.
  /// This function converts an argument index to the corresponding
  /// parameter index. Returns std::nullopt is the argument doesn't correspond
  /// to any parameter variable.
  virtual std::optional<unsigned>
  getAdjustedParameterIndex(unsigned ASTArgumentIndex) const {
    return ASTArgumentIndex;
  }

  /// Some call event sub-classes conveniently adjust mismatching AST indices
  /// to match parameter indices. This function converts an argument index
  /// as understood by CallEvent to the argument index as understood by the AST.
  virtual unsigned getASTArgumentIndex(unsigned CallArgumentIndex) const {
    return CallArgumentIndex;
  }

  /// Returns the construction context of the call, if it is a C++ constructor
  /// call or a call of a function returning a C++ class instance. Otherwise
  /// return nullptr.
  const ConstructionContext *getConstructionContext() const;

  /// If the call returns a C++ record type then the region of its return value
  /// can be retrieved from its construction context.
  std::optional<SVal> getReturnValueUnderConstruction() const;

  // Returns the CallEvent representing the caller of this function
  const CallEventRef<> getCaller() const;

  // Returns true if the function was called from a standard library function.
  // If not or could not get the caller (it may be a top level function)
  // returns false.
  bool isCalledFromSystemHeader() const;

  // Iterator access to formal parameters and their types.
private:
  struct GetTypeFn {
    QualType operator()(ParmVarDecl *PD) const { return PD->getType(); }
  };

public:
  /// Return call's formal parameters.
  ///
  /// Remember that the number of formal parameters may not match the number
  /// of arguments for all calls. However, the first parameter will always
  /// correspond with the argument value returned by \c getArgSVal(0).
  virtual ArrayRef<ParmVarDecl *> parameters() const = 0;

  using param_type_iterator =
      llvm::mapped_iterator<ArrayRef<ParmVarDecl *>::iterator, GetTypeFn>;

  /// Returns an iterator over the types of the call's formal parameters.
  ///
  /// This uses the callee decl found by default name lookup rather than the
  /// definition because it represents a public interface, and probably has
  /// more annotations.
  param_type_iterator param_type_begin() const {
    return llvm::map_iterator(parameters().begin(), GetTypeFn());
  }
  /// \sa param_type_begin()
  param_type_iterator param_type_end() const {
    return llvm::map_iterator(parameters().end(), GetTypeFn());
  }

  // For debugging purposes only
  void dump(raw_ostream &Out) const;
  void dump() const;
};

/// Represents a call to any sort of function that might have a
/// FunctionDecl.
class AnyFunctionCall : public CallEvent {
protected:
  AnyFunctionCall(const Expr *E, ProgramStateRef St,
                  const LocationContext *LCtx,
                  CFGBlock::ConstCFGElementRef ElemRef)
      : CallEvent(E, St, LCtx, ElemRef) {}
  AnyFunctionCall(const Decl *D, ProgramStateRef St,
                  const LocationContext *LCtx,
                  CFGBlock::ConstCFGElementRef ElemRef)
      : CallEvent(D, St, LCtx, ElemRef) {}
  AnyFunctionCall(const AnyFunctionCall &Other) = default;

public:
  // This function is overridden by subclasses, but they must return
  // a FunctionDecl.
  const FunctionDecl *getDecl() const override {
    return cast<FunctionDecl>(CallEvent::getDecl());
  }

  RuntimeDefinition getRuntimeDefinition() const override;

  bool argumentsMayEscape() const override;

  void getInitialStackFrameContents(const StackFrameContext *CalleeCtx,
                                    BindingsTy &Bindings) const override;

  ArrayRef<ParmVarDecl *> parameters() const override;

  static bool classof(const CallEvent *CA) {
    return CA->getKind() >= CE_BEG_FUNCTION_CALLS &&
           CA->getKind() <= CE_END_FUNCTION_CALLS;
  }
};

/// Represents a C function or static C++ member function call.
///
/// Example: \c fun()
class SimpleFunctionCall : public AnyFunctionCall {
  friend class CallEventManager;

protected:
  SimpleFunctionCall(const CallExpr *CE, ProgramStateRef St,
                     const LocationContext *LCtx,
                     CFGBlock::ConstCFGElementRef ElemRef)
      : AnyFunctionCall(CE, St, LCtx, ElemRef) {}
  SimpleFunctionCall(const SimpleFunctionCall &Other) = default;

  void cloneTo(void *Dest) const override {
    new (Dest) SimpleFunctionCall(*this);
  }

public:
  const CallExpr *getOriginExpr() const override {
    return cast<CallExpr>(AnyFunctionCall::getOriginExpr());
  }

  const FunctionDecl *getDecl() const override;

  unsigned getNumArgs() const override { return getOriginExpr()->getNumArgs(); }

  const Expr *getArgExpr(unsigned Index) const override {
    return getOriginExpr()->getArg(Index);
  }

  Kind getKind() const override { return CE_Function; }
  StringRef getKindAsString() const override { return "SimpleFunctionCall"; }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_Function;
  }
};

/// Represents a call to a block.
///
/// Example: <tt>^{ statement-body }()</tt>
class BlockCall : public CallEvent {
  friend class CallEventManager;

protected:
  BlockCall(const CallExpr *CE, ProgramStateRef St, const LocationContext *LCtx,
            CFGBlock::ConstCFGElementRef ElemRef)
      : CallEvent(CE, St, LCtx, ElemRef) {}
  BlockCall(const BlockCall &Other) = default;

  void cloneTo(void *Dest) const override { new (Dest) BlockCall(*this); }

  void getExtraInvalidatedValues(
      ValueList &Values,
      RegionAndSymbolInvalidationTraits *ETraits) const override;

public:
  const CallExpr *getOriginExpr() const override {
    return cast<CallExpr>(CallEvent::getOriginExpr());
  }

  unsigned getNumArgs() const override { return getOriginExpr()->getNumArgs(); }

  const Expr *getArgExpr(unsigned Index) const override {
    return getOriginExpr()->getArg(Index);
  }

  /// Returns the region associated with this instance of the block.
  ///
  /// This may be NULL if the block's origin is unknown.
  const BlockDataRegion *getBlockRegion() const;

  const BlockDecl *getDecl() const override {
    const BlockDataRegion *BR = getBlockRegion();
    if (!BR)
      return nullptr;
    return BR->getDecl();
  }

  bool isConversionFromLambda() const {
    const BlockDecl *BD = getDecl();
    if (!BD)
      return false;

    return BD->isConversionFromLambda();
  }

  /// For a block converted from a C++ lambda, returns the block
  /// VarRegion for the variable holding the captured C++ lambda record.
  const VarRegion *getRegionStoringCapturedLambda() const {
    assert(isConversionFromLambda());
    const BlockDataRegion *BR = getBlockRegion();
    assert(BR && "Block converted from lambda must have a block region");

    auto ReferencedVars = BR->referenced_vars();
    assert(!ReferencedVars.empty());
    return ReferencedVars.begin().getCapturedRegion();
  }

  RuntimeDefinition getRuntimeDefinition() const override {
    if (!isConversionFromLambda())
      return RuntimeDefinition(getDecl());

    // Clang converts lambdas to blocks with an implicit user-defined
    // conversion operator method on the lambda record that looks (roughly)
    // like:
    //
    // typedef R(^block_type)(P1, P2, ...);
    // operator block_type() const {
    //   auto Lambda = *this;
    //   return ^(P1 p1, P2 p2, ...){
    //     /* return Lambda(p1, p2, ...); */
    //   };
    // }
    //
    // Here R is the return type of the lambda and P1, P2, ... are
    // its parameter types. 'Lambda' is a fake VarDecl captured by the block
    // that is initialized to a copy of the lambda.
    //
    // Sema leaves the body of a lambda-converted block empty (it is
    // produced by CodeGen), so we can't analyze it directly. Instead, we skip
    // the block body and analyze the operator() method on the captured lambda.
    const VarDecl *LambdaVD = getRegionStoringCapturedLambda()->getDecl();
    const CXXRecordDecl *LambdaDecl = LambdaVD->getType()->getAsCXXRecordDecl();
    CXXMethodDecl *LambdaCallOperator = LambdaDecl->getLambdaCallOperator();

    return RuntimeDefinition(LambdaCallOperator);
  }

  bool argumentsMayEscape() const override { return true; }

  void getInitialStackFrameContents(const StackFrameContext *CalleeCtx,
                                    BindingsTy &Bindings) const override;

  ArrayRef<ParmVarDecl *> parameters() const override;

  Kind getKind() const override { return CE_Block; }
  StringRef getKindAsString() const override { return "BlockCall"; }

  static bool classof(const CallEvent *CA) { return CA->getKind() == CE_Block; }
};

/// Represents a non-static C++ member function call, no matter how
/// it is written.
class CXXInstanceCall : public AnyFunctionCall {
protected:
  CXXInstanceCall(const CallExpr *CE, ProgramStateRef St,
                  const LocationContext *LCtx,
                  CFGBlock::ConstCFGElementRef ElemRef)
      : AnyFunctionCall(CE, St, LCtx, ElemRef) {}
  CXXInstanceCall(const FunctionDecl *D, ProgramStateRef St,
                  const LocationContext *LCtx,
                  CFGBlock::ConstCFGElementRef ElemRef)
      : AnyFunctionCall(D, St, LCtx, ElemRef) {}
  CXXInstanceCall(const CXXInstanceCall &Other) = default;

  void getExtraInvalidatedValues(
      ValueList &Values,
      RegionAndSymbolInvalidationTraits *ETraits) const override;

public:
  /// Returns the expression representing the implicit 'this' object.
  virtual const Expr *getCXXThisExpr() const { return nullptr; }

  /// Returns the value of the implicit 'this' object.
  virtual SVal getCXXThisVal() const;

  const FunctionDecl *getDecl() const override;

  RuntimeDefinition getRuntimeDefinition() const override;

  void getInitialStackFrameContents(const StackFrameContext *CalleeCtx,
                                    BindingsTy &Bindings) const override;

  static bool classof(const CallEvent *CA) {
    return CA->getKind() >= CE_BEG_CXX_INSTANCE_CALLS &&
           CA->getKind() <= CE_END_CXX_INSTANCE_CALLS;
  }
};

/// Represents a static C++ operator call.
///
/// "A" in this example.
/// However, "B" and "C" are represented by SimpleFunctionCall.
/// \code
///   struct S {
///     int pad;
///     static void operator()(int x, int y);
///   };
///   S s{10};
///   void (*fptr)(int, int) = &S::operator();
///
///   s(1, 2);  // A
///   S::operator()(1, 2);  // B
///   fptr(1, 2); // C
/// \endcode
class CXXStaticOperatorCall : public SimpleFunctionCall {
  friend class CallEventManager;

protected:
  CXXStaticOperatorCall(const CXXOperatorCallExpr *CE, ProgramStateRef St,
                        const LocationContext *LCtx,
                        CFGBlock::ConstCFGElementRef ElemRef)
      : SimpleFunctionCall(CE, St, LCtx, ElemRef) {}
  CXXStaticOperatorCall(const CXXStaticOperatorCall &Other) = default;

  void cloneTo(void *Dest) const override {
    new (Dest) CXXStaticOperatorCall(*this);
  }

public:
  const CXXOperatorCallExpr *getOriginExpr() const override {
    return cast<CXXOperatorCallExpr>(SimpleFunctionCall::getOriginExpr());
  }

  unsigned getNumArgs() const override {
    // Ignore the object parameter that is not used for static member functions.
    assert(getOriginExpr()->getNumArgs() > 0);
    return getOriginExpr()->getNumArgs() - 1;
  }

  const Expr *getArgExpr(unsigned Index) const override {
    // Ignore the object parameter that is not used for static member functions.
    return getOriginExpr()->getArg(Index + 1);
  }

  std::optional<unsigned>
  getAdjustedParameterIndex(unsigned ASTArgumentIndex) const override {
    // Ignore the object parameter that is not used for static member functions.
    if (ASTArgumentIndex == 0)
      return std::nullopt;
    return ASTArgumentIndex - 1;
  }

  unsigned getASTArgumentIndex(unsigned CallArgumentIndex) const override {
    // Account for the object parameter for the static member function.
    return CallArgumentIndex + 1;
  }

  OverloadedOperatorKind getOverloadedOperator() const {
    return getOriginExpr()->getOperator();
  }

  Kind getKind() const override { return CE_CXXStaticOperator; }
  StringRef getKindAsString() const override { return "CXXStaticOperatorCall"; }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_CXXStaticOperator;
  }
};

/// Represents a non-static C++ member function call.
///
/// Example: \c obj.fun()
class CXXMemberCall : public CXXInstanceCall {
  friend class CallEventManager;

protected:
  CXXMemberCall(const CXXMemberCallExpr *CE, ProgramStateRef St,
                const LocationContext *LCtx,
                CFGBlock::ConstCFGElementRef ElemRef)
      : CXXInstanceCall(CE, St, LCtx, ElemRef) {}
  CXXMemberCall(const CXXMemberCall &Other) = default;

  void cloneTo(void *Dest) const override { new (Dest) CXXMemberCall(*this); }

public:
  const CXXMemberCallExpr *getOriginExpr() const override {
    return cast<CXXMemberCallExpr>(CXXInstanceCall::getOriginExpr());
  }

  unsigned getNumArgs() const override {
    if (const CallExpr *CE = getOriginExpr())
      return CE->getNumArgs();
    return 0;
  }

  const Expr *getArgExpr(unsigned Index) const override {
    return getOriginExpr()->getArg(Index);
  }

  const Expr *getCXXThisExpr() const override;

  RuntimeDefinition getRuntimeDefinition() const override;

  Kind getKind() const override { return CE_CXXMember; }
  StringRef getKindAsString() const override { return "CXXMemberCall"; }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_CXXMember;
  }
};

/// Represents a C++ overloaded operator call where the operator is
/// implemented as a non-static member function.
///
/// Example: <tt>iter + 1</tt>
class CXXMemberOperatorCall : public CXXInstanceCall {
  friend class CallEventManager;

protected:
  CXXMemberOperatorCall(const CXXOperatorCallExpr *CE, ProgramStateRef St,
                        const LocationContext *LCtx,
                        CFGBlock::ConstCFGElementRef ElemRef)
      : CXXInstanceCall(CE, St, LCtx, ElemRef) {}
  CXXMemberOperatorCall(const CXXMemberOperatorCall &Other) = default;

  void cloneTo(void *Dest) const override {
    new (Dest) CXXMemberOperatorCall(*this);
  }

public:
  const CXXOperatorCallExpr *getOriginExpr() const override {
    return cast<CXXOperatorCallExpr>(CXXInstanceCall::getOriginExpr());
  }

  unsigned getNumArgs() const override {
    return getOriginExpr()->getNumArgs() - 1;
  }

  const Expr *getArgExpr(unsigned Index) const override {
    return getOriginExpr()->getArg(Index + 1);
  }

  const Expr *getCXXThisExpr() const override;

  Kind getKind() const override { return CE_CXXMemberOperator; }
  StringRef getKindAsString() const override { return "CXXMemberOperatorCall"; }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_CXXMemberOperator;
  }

  std::optional<unsigned>
  getAdjustedParameterIndex(unsigned ASTArgumentIndex) const override {
    // For member operator calls argument 0 on the expression corresponds
    // to implicit this-parameter on the declaration.
    return (ASTArgumentIndex > 0)
               ? std::optional<unsigned>(ASTArgumentIndex - 1)
               : std::nullopt;
  }

  unsigned getASTArgumentIndex(unsigned CallArgumentIndex) const override {
    // For member operator calls argument 0 on the expression corresponds
    // to implicit this-parameter on the declaration.
    return CallArgumentIndex + 1;
  }

  OverloadedOperatorKind getOverloadedOperator() const {
    return getOriginExpr()->getOperator();
  }
};

/// Represents an implicit call to a C++ destructor.
///
/// This can occur at the end of a scope (for automatic objects), at the end
/// of a full-expression (for temporaries), or as part of a delete.
class CXXDestructorCall : public CXXInstanceCall {
  friend class CallEventManager;

protected:
  using DtorDataTy = llvm::PointerIntPair<const MemRegion *, 1, bool>;

  /// Creates an implicit destructor.
  ///
  /// \param DD The destructor that will be called.
  /// \param Trigger The statement whose completion causes this destructor call.
  /// \param Target The object region to be destructed.
  /// \param St The path-sensitive state at this point in the program.
  /// \param LCtx The location context at this point in the program.
  /// \param ElemRef The reference to this destructor in the CFG.
  ///
  /// FIXME: Eventually we want to drop \param Target and deduce it from
  /// \param ElemRef. To do that we need to migrate the logic for target
  /// region lookup from ExprEngine::ProcessImplicitDtor() and make it
  /// independent from ExprEngine.
  CXXDestructorCall(const CXXDestructorDecl *DD, const Stmt *Trigger,
                    const MemRegion *Target, bool IsBaseDestructor,
                    ProgramStateRef St, const LocationContext *LCtx,
                    CFGBlock::ConstCFGElementRef ElemRef)
      : CXXInstanceCall(DD, St, LCtx, ElemRef) {
    Data = DtorDataTy(Target, IsBaseDestructor).getOpaqueValue();
    Location = Trigger->getEndLoc();
  }

  CXXDestructorCall(const CXXDestructorCall &Other) = default;

  void cloneTo(void *Dest) const override {
    new (Dest) CXXDestructorCall(*this);
  }

public:
  SourceRange getSourceRange() const override { return Location; }
  unsigned getNumArgs() const override { return 0; }

  RuntimeDefinition getRuntimeDefinition() const override;

  /// Returns the value of the implicit 'this' object.
  SVal getCXXThisVal() const override;

  /// Returns true if this is a call to a base class destructor.
  bool isBaseDestructor() const {
    return DtorDataTy::getFromOpaqueValue(Data).getInt();
  }

  Kind getKind() const override { return CE_CXXDestructor; }
  StringRef getKindAsString() const override { return "CXXDestructorCall"; }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_CXXDestructor;
  }
};

/// Represents any constructor invocation. This includes regular constructors
/// and inherited constructors.
class AnyCXXConstructorCall : public AnyFunctionCall {
protected:
  AnyCXXConstructorCall(const Expr *E, const MemRegion *Target,
                        ProgramStateRef St, const LocationContext *LCtx,
                        CFGBlock::ConstCFGElementRef ElemRef)
      : AnyFunctionCall(E, St, LCtx, ElemRef) {
    assert(E && (isa<CXXConstructExpr>(E) || isa<CXXInheritedCtorInitExpr>(E)));
    // Target may be null when the region is unknown.
    Data = Target;
  }

  void getExtraInvalidatedValues(
      ValueList &Values,
      RegionAndSymbolInvalidationTraits *ETraits) const override;

  void getInitialStackFrameContents(const StackFrameContext *CalleeCtx,
                                    BindingsTy &Bindings) const override;

public:
  /// Returns the value of the implicit 'this' object.
  SVal getCXXThisVal() const;

  static bool classof(const CallEvent *Call) {
    return Call->getKind() >= CE_BEG_CXX_CONSTRUCTOR_CALLS &&
           Call->getKind() <= CE_END_CXX_CONSTRUCTOR_CALLS;
  }
};

/// Represents a call to a C++ constructor.
///
/// Example: \c T(1)
class CXXConstructorCall : public AnyCXXConstructorCall {
  friend class CallEventManager;

protected:
  /// Creates a constructor call.
  ///
  /// \param CE The constructor expression as written in the source.
  /// \param Target The region where the object should be constructed. If NULL,
  ///               a new symbolic region will be used.
  /// \param St The path-sensitive state at this point in the program.
  /// \param LCtx The location context at this point in the program.
  /// \param ElemRef The reference to this constructor in the CFG.
  ///
  /// FIXME: Eventually we want to drop \param Target and deduce it from
  /// \param ElemRef.
  CXXConstructorCall(const CXXConstructExpr *CE, const MemRegion *Target,
                     ProgramStateRef St, const LocationContext *LCtx,
                     CFGBlock::ConstCFGElementRef ElemRef)
      : AnyCXXConstructorCall(CE, Target, St, LCtx, ElemRef) {}

  CXXConstructorCall(const CXXConstructorCall &Other) = default;

  void cloneTo(void *Dest) const override {
    new (Dest) CXXConstructorCall(*this);
  }

public:
  const CXXConstructExpr *getOriginExpr() const override {
    return cast<CXXConstructExpr>(AnyFunctionCall::getOriginExpr());
  }

  const CXXConstructorDecl *getDecl() const override {
    return getOriginExpr()->getConstructor();
  }

  unsigned getNumArgs() const override { return getOriginExpr()->getNumArgs(); }

  const Expr *getArgExpr(unsigned Index) const override {
    return getOriginExpr()->getArg(Index);
  }

  Kind getKind() const override { return CE_CXXConstructor; }
  StringRef getKindAsString() const override { return "CXXConstructorCall"; }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_CXXConstructor;
  }
};

/// Represents a call to a C++ inherited constructor.
///
/// Example: \c class T : public S { using S::S; }; T(1);
///
// Note, it is difficult to model the parameters. This is one of the reasons
// why we skip analysis of inheriting constructors as top-level functions.
// CXXInheritedCtorInitExpr doesn't take arguments and doesn't model parameter
// initialization because there is none: the arguments in the outer
// CXXConstructExpr directly initialize the parameters of the base class
// constructor, and no copies are made. (Making a copy of the parameter is
// incorrect, at least if it's done in an observable way.) The derived class
// constructor doesn't even exist in the formal model.
/// E.g., in:
///
/// struct X { X *p = this; ~X() {} };
/// struct A { A(X x) : b(x.p == &x) {} bool b; };
/// struct B : A { using A::A; };
/// B b = X{};
///
/// ... b.b is initialized to true.
class CXXInheritedConstructorCall : public AnyCXXConstructorCall {
  friend class CallEventManager;

protected:
  CXXInheritedConstructorCall(const CXXInheritedCtorInitExpr *CE,
                              const MemRegion *Target, ProgramStateRef St,
                              const LocationContext *LCtx,
                              CFGBlock::ConstCFGElementRef ElemRef)
      : AnyCXXConstructorCall(CE, Target, St, LCtx, ElemRef) {}

  CXXInheritedConstructorCall(const CXXInheritedConstructorCall &Other) =
      default;

  void cloneTo(void *Dest) const override {
    new (Dest) CXXInheritedConstructorCall(*this);
  }

public:
  const CXXInheritedCtorInitExpr *getOriginExpr() const override {
    return cast<CXXInheritedCtorInitExpr>(AnyFunctionCall::getOriginExpr());
  }

  const CXXConstructorDecl *getDecl() const override {
    return getOriginExpr()->getConstructor();
  }

  /// Obtain the stack frame of the inheriting constructor. Argument expressions
  /// can be found on the call site of that stack frame.
  const StackFrameContext *getInheritingStackFrame() const;

  /// Obtain the CXXConstructExpr for the sub-class that inherited the current
  /// constructor (possibly indirectly). It's the statement that contains
  /// argument expressions.
  const CXXConstructExpr *getInheritingConstructor() const {
    return cast<CXXConstructExpr>(getInheritingStackFrame()->getCallSite());
  }

  unsigned getNumArgs() const override {
    return getInheritingConstructor()->getNumArgs();
  }

  const Expr *getArgExpr(unsigned Index) const override {
    return getInheritingConstructor()->getArg(Index);
  }

  SVal getArgSVal(unsigned Index) const override {
    return getState()->getSVal(
        getArgExpr(Index),
        getInheritingStackFrame()->getParent()->getStackFrame());
  }

  Kind getKind() const override { return CE_CXXInheritedConstructor; }
  StringRef getKindAsString() const override {
    return "CXXInheritedConstructorCall";
  }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_CXXInheritedConstructor;
  }
};

/// Represents the memory allocation call in a C++ new-expression.
///
/// This is a call to "operator new".
class CXXAllocatorCall : public AnyFunctionCall {
  friend class CallEventManager;

protected:
  CXXAllocatorCall(const CXXNewExpr *E, ProgramStateRef St,
                   const LocationContext *LCtx,
                   CFGBlock::ConstCFGElementRef ElemRef)
      : AnyFunctionCall(E, St, LCtx, ElemRef) {}
  CXXAllocatorCall(const CXXAllocatorCall &Other) = default;

  void cloneTo(void *Dest) const override {
    new (Dest) CXXAllocatorCall(*this);
  }

public:
  const CXXNewExpr *getOriginExpr() const override {
    return cast<CXXNewExpr>(AnyFunctionCall::getOriginExpr());
  }

  const FunctionDecl *getDecl() const override {
    return getOriginExpr()->getOperatorNew();
  }

  SVal getObjectUnderConstruction() const {
    return *ExprEngine::getObjectUnderConstruction(getState(), getOriginExpr(),
                                                   getLocationContext());
  }

  /// Number of non-placement arguments to the call. It is equal to 2 for
  /// C++17 aligned operator new() calls that have alignment implicitly
  /// passed as the second argument, and to 1 for other operator new() calls.
  unsigned getNumImplicitArgs() const {
    return getOriginExpr()->passAlignment() ? 2 : 1;
  }

  unsigned getNumArgs() const override {
    return getOriginExpr()->getNumPlacementArgs() + getNumImplicitArgs();
  }

  bool isArray() const { return getOriginExpr()->isArray(); }

  std::optional<const clang::Expr *> getArraySizeExpr() const {
    return getOriginExpr()->getArraySize();
  }

  SVal getArraySizeVal() const {
    assert(isArray() && "The allocator call doesn't allocate and array!");

    return getState()->getSVal(*getArraySizeExpr(), getLocationContext());
  }

  const Expr *getArgExpr(unsigned Index) const override {
    // The first argument of an allocator call is the size of the allocation.
    if (Index < getNumImplicitArgs())
      return nullptr;
    return getOriginExpr()->getPlacementArg(Index - getNumImplicitArgs());
  }

  /// Number of placement arguments to the operator new() call. For example,
  /// standard std::nothrow operator new and standard placement new both have
  /// 1 implicit argument (size) and 1 placement argument, while regular
  /// operator new() has 1 implicit argument and 0 placement arguments.
  const Expr *getPlacementArgExpr(unsigned Index) const {
    return getOriginExpr()->getPlacementArg(Index);
  }

  Kind getKind() const override { return CE_CXXAllocator; }
  StringRef getKindAsString() const override { return "CXXAllocatorCall"; }

  static bool classof(const CallEvent *CE) {
    return CE->getKind() == CE_CXXAllocator;
  }
};

/// Represents the memory deallocation call in a C++ delete-expression.
///
/// This is a call to "operator delete".
// FIXME: CXXDeleteExpr isn't present for custom delete operators, or even for
// some those that are in the standard library, like the no-throw or align_val
// versions.
// Some pointers:
// http://lists.llvm.org/pipermail/cfe-dev/2020-April/065080.html
// clang/test/Analysis/cxx-dynamic-memory-analysis-order.cpp
// clang/unittests/StaticAnalyzer/CallEventTest.cpp
class CXXDeallocatorCall : public AnyFunctionCall {
  friend class CallEventManager;

protected:
  CXXDeallocatorCall(const CXXDeleteExpr *E, ProgramStateRef St,
                     const LocationContext *LCtx,
                     CFGBlock::ConstCFGElementRef ElemRef)
      : AnyFunctionCall(E, St, LCtx, ElemRef) {}
  CXXDeallocatorCall(const CXXDeallocatorCall &Other) = default;

  void cloneTo(void *Dest) const override {
    new (Dest) CXXDeallocatorCall(*this);
  }

public:
  const CXXDeleteExpr *getOriginExpr() const override {
    return cast<CXXDeleteExpr>(AnyFunctionCall::getOriginExpr());
  }

  const FunctionDecl *getDecl() const override {
    return getOriginExpr()->getOperatorDelete();
  }

  unsigned getNumArgs() const override { return getDecl()->getNumParams(); }

  const Expr *getArgExpr(unsigned Index) const override {
    // CXXDeleteExpr's only have a single argument.
    return getOriginExpr()->getArgument();
  }

  Kind getKind() const override { return CE_CXXDeallocator; }
  StringRef getKindAsString() const override { return "CXXDeallocatorCall"; }

  static bool classof(const CallEvent *CE) {
    return CE->getKind() == CE_CXXDeallocator;
  }
};

/// Represents the ways an Objective-C message send can occur.
//
// Note to maintainers: OCM_Message should always be last, since it does not
// need to fit in the Data field's low bits.
enum ObjCMessageKind { OCM_PropertyAccess, OCM_Subscript, OCM_Message };

/// Represents any expression that calls an Objective-C method.
///
/// This includes all of the kinds listed in ObjCMessageKind.
class ObjCMethodCall : public CallEvent {
  friend class CallEventManager;

  const PseudoObjectExpr *getContainingPseudoObjectExpr() const;

protected:
  ObjCMethodCall(const ObjCMessageExpr *Msg, ProgramStateRef St,
                 const LocationContext *LCtx,
                 CFGBlock::ConstCFGElementRef ElemRef)
      : CallEvent(Msg, St, LCtx, ElemRef) {
    Data = nullptr;
  }

  ObjCMethodCall(const ObjCMethodCall &Other) = default;

  void cloneTo(void *Dest) const override { new (Dest) ObjCMethodCall(*this); }

  void getExtraInvalidatedValues(
      ValueList &Values,
      RegionAndSymbolInvalidationTraits *ETraits) const override;

  /// Check if the selector may have multiple definitions (may have overrides).
  virtual bool canBeOverridenInSubclass(ObjCInterfaceDecl *IDecl,
                                        Selector Sel) const;

public:
  const ObjCMessageExpr *getOriginExpr() const override {
    return cast<ObjCMessageExpr>(CallEvent::getOriginExpr());
  }

  const ObjCMethodDecl *getDecl() const override {
    return getOriginExpr()->getMethodDecl();
  }

  unsigned getNumArgs() const override { return getOriginExpr()->getNumArgs(); }

  const Expr *getArgExpr(unsigned Index) const override {
    return getOriginExpr()->getArg(Index);
  }

  bool isInstanceMessage() const {
    return getOriginExpr()->isInstanceMessage();
  }

  ObjCMethodFamily getMethodFamily() const {
    return getOriginExpr()->getMethodFamily();
  }

  Selector getSelector() const { return getOriginExpr()->getSelector(); }

  SourceRange getSourceRange() const override;

  /// Returns the value of the receiver at the time of this call.
  SVal getReceiverSVal() const;

  /// Get the interface for the receiver.
  ///
  /// This works whether this is an instance message or a class message.
  /// However, it currently just uses the static type of the receiver.
  const ObjCInterfaceDecl *getReceiverInterface() const {
    return getOriginExpr()->getReceiverInterface();
  }

  /// Checks if the receiver refers to 'self' or 'super'.
  bool isReceiverSelfOrSuper() const;

  /// Returns how the message was written in the source (property access,
  /// subscript, or explicit message send).
  ObjCMessageKind getMessageKind() const;

  /// Returns true if this property access or subscript is a setter (has the
  /// form of an assignment).
  bool isSetter() const {
    switch (getMessageKind()) {
    case OCM_Message:
      llvm_unreachable("This is not a pseudo-object access!");
    case OCM_PropertyAccess:
      return getNumArgs() > 0;
    case OCM_Subscript:
      return getNumArgs() > 1;
    }
    llvm_unreachable("Unknown message kind");
  }

  // Returns the property accessed by this method, either explicitly via
  // property syntax or implicitly via a getter or setter method. Returns
  // nullptr if the call is not a prooperty access.
  const ObjCPropertyDecl *getAccessedProperty() const;

  RuntimeDefinition getRuntimeDefinition() const override;

  bool argumentsMayEscape() const override;

  void getInitialStackFrameContents(const StackFrameContext *CalleeCtx,
                                    BindingsTy &Bindings) const override;

  ArrayRef<ParmVarDecl *> parameters() const override;

  Kind getKind() const override { return CE_ObjCMessage; }
  StringRef getKindAsString() const override { return "ObjCMethodCall"; }

  static bool classof(const CallEvent *CA) {
    return CA->getKind() == CE_ObjCMessage;
  }
};

/// Manages the lifetime of CallEvent objects.
///
/// CallEventManager provides a way to create arbitrary CallEvents "on the
/// stack" as if they were value objects by keeping a cache of CallEvent-sized
/// memory blocks. The CallEvents created by CallEventManager are only valid
/// for the lifetime of the OwnedCallEvent that holds them; right now these
/// objects cannot be copied and ownership cannot be transferred.
class CallEventManager {
  friend class CallEvent;

  llvm::BumpPtrAllocator &Alloc;
  SmallVector<void *, 8> Cache;

  using CallEventTemplateTy = SimpleFunctionCall;

  void reclaim(const void *Memory) {
    Cache.push_back(const_cast<void *>(Memory));
  }

  /// Returns memory that can be initialized as a CallEvent.
  void *allocate() {
    if (Cache.empty())
      return Alloc.Allocate<CallEventTemplateTy>();
    else
      return Cache.pop_back_val();
  }

  template <typename T, typename Arg>
  T *create(Arg A, ProgramStateRef St, const LocationContext *LCtx,
            CFGBlock::ConstCFGElementRef ElemRef) {
    static_assert(sizeof(T) == sizeof(CallEventTemplateTy),
                  "CallEvent subclasses are not all the same size");
    return new (allocate()) T(A, St, LCtx, ElemRef);
  }

  template <typename T, typename Arg1, typename Arg2>
  T *create(Arg1 A1, Arg2 A2, ProgramStateRef St, const LocationContext *LCtx,
            CFGBlock::ConstCFGElementRef ElemRef) {
    static_assert(sizeof(T) == sizeof(CallEventTemplateTy),
                  "CallEvent subclasses are not all the same size");
    return new (allocate()) T(A1, A2, St, LCtx, ElemRef);
  }

  template <typename T, typename Arg1, typename Arg2, typename Arg3>
  T *create(Arg1 A1, Arg2 A2, Arg3 A3, ProgramStateRef St,
            const LocationContext *LCtx, CFGBlock::ConstCFGElementRef ElemRef) {
    static_assert(sizeof(T) == sizeof(CallEventTemplateTy),
                  "CallEvent subclasses are not all the same size");
    return new (allocate()) T(A1, A2, A3, St, LCtx, ElemRef);
  }

  template <typename T, typename Arg1, typename Arg2, typename Arg3,
            typename Arg4>
  T *create(Arg1 A1, Arg2 A2, Arg3 A3, Arg4 A4, ProgramStateRef St,
            const LocationContext *LCtx, CFGBlock::ConstCFGElementRef ElemRef) {
    static_assert(sizeof(T) == sizeof(CallEventTemplateTy),
                  "CallEvent subclasses are not all the same size");
    return new (allocate()) T(A1, A2, A3, A4, St, LCtx, ElemRef);
  }

public:
  CallEventManager(llvm::BumpPtrAllocator &alloc) : Alloc(alloc) {}

  /// Gets an outside caller given a callee context.
  CallEventRef<> getCaller(const StackFrameContext *CalleeCtx,
                           ProgramStateRef State);

  /// Gets a call event for a function call, Objective-C method call,
  /// a 'new', or a 'delete' call.
  CallEventRef<> getCall(const Stmt *S, ProgramStateRef State,
                         const LocationContext *LC,
                         CFGBlock::ConstCFGElementRef ElemRef);

  CallEventRef<> getSimpleCall(const CallExpr *E, ProgramStateRef State,
                               const LocationContext *LCtx,
                               CFGBlock::ConstCFGElementRef ElemRef);

  CallEventRef<ObjCMethodCall>
  getObjCMethodCall(const ObjCMessageExpr *E, ProgramStateRef State,
                    const LocationContext *LCtx,
                    CFGBlock::ConstCFGElementRef ElemRef) {
    return create<ObjCMethodCall>(E, State, LCtx, ElemRef);
  }

  CallEventRef<CXXConstructorCall>
  getCXXConstructorCall(const CXXConstructExpr *E, const MemRegion *Target,
                        ProgramStateRef State, const LocationContext *LCtx,
                        CFGBlock::ConstCFGElementRef ElemRef) {
    return create<CXXConstructorCall>(E, Target, State, LCtx, ElemRef);
  }

  CallEventRef<CXXInheritedConstructorCall>
  getCXXInheritedConstructorCall(const CXXInheritedCtorInitExpr *E,
                                 const MemRegion *Target, ProgramStateRef State,
                                 const LocationContext *LCtx,
                                 CFGBlock::ConstCFGElementRef ElemRef) {
    return create<CXXInheritedConstructorCall>(E, Target, State, LCtx, ElemRef);
  }

  CallEventRef<CXXDestructorCall>
  getCXXDestructorCall(const CXXDestructorDecl *DD, const Stmt *Trigger,
                       const MemRegion *Target, bool IsBase,
                       ProgramStateRef State, const LocationContext *LCtx,
                       CFGBlock::ConstCFGElementRef ElemRef) {
    return create<CXXDestructorCall>(DD, Trigger, Target, IsBase, State, LCtx,
                                     ElemRef);
  }

  CallEventRef<CXXAllocatorCall>
  getCXXAllocatorCall(const CXXNewExpr *E, ProgramStateRef State,
                      const LocationContext *LCtx,
                      CFGBlock::ConstCFGElementRef ElemRef) {
    return create<CXXAllocatorCall>(E, State, LCtx, ElemRef);
  }

  CallEventRef<CXXDeallocatorCall>
  getCXXDeallocatorCall(const CXXDeleteExpr *E, ProgramStateRef State,
                        const LocationContext *LCtx,
                        CFGBlock::ConstCFGElementRef ElemRef) {
    return create<CXXDeallocatorCall>(E, State, LCtx, ElemRef);
  }
};

template <typename T>
CallEventRef<T> CallEvent::cloneWithState(ProgramStateRef NewState) const {
  assert(isa<T>(*this) && "Cloning to unrelated type");
  static_assert(sizeof(T) == sizeof(CallEvent),
                "Subclasses may not add fields");

  if (NewState == State)
    return cast<T>(this);

  CallEventManager &Mgr = State->getStateManager().getCallEventManager();
  T *Copy = static_cast<T *>(Mgr.allocate());
  cloneTo(Copy);
  assert(Copy->getKind() == this->getKind() && "Bad copy");

  Copy->State = NewState;
  return Copy;
}

inline void CallEvent::Release() const {
  assert(RefCount > 0 && "Reference count is already zero.");
  --RefCount;

  if (RefCount > 0)
    return;

  CallEventManager &Mgr = State->getStateManager().getCallEventManager();
  Mgr.reclaim(this);

  this->~CallEvent();
}

} // namespace ento

} // namespace clang

namespace llvm {

// Support isa<>, cast<>, and dyn_cast<> for CallEventRef.
template <class T> struct simplify_type<clang::ento::CallEventRef<T>> {
  using SimpleType = const T *;

  static SimpleType getSimplifiedValue(clang::ento::CallEventRef<T> Val) {
    return Val.get();
  }
};

} // namespace llvm

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CALLEVENT_H
