//===----- CGCall.h - Encapsulate calling convention details ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function
// definition used to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGCALL_H
#define LLVM_CLANG_LIB_CODEGEN_CGCALL_H

#include "CGPointerAuthInfo.h"
#include "CGValue.h"
#include "EHScopeStack.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/CanonicalType.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/IR/Value.h"

namespace llvm {
class Type;
class Value;
} // namespace llvm

namespace clang {
class Decl;
class FunctionDecl;
class TargetOptions;
class VarDecl;

namespace CodeGen {

/// Abstract information about a function or function prototype.
class CGCalleeInfo {
  /// The function prototype of the callee.
  const FunctionProtoType *CalleeProtoTy;
  /// The function declaration of the callee.
  GlobalDecl CalleeDecl;

public:
  explicit CGCalleeInfo() : CalleeProtoTy(nullptr) {}
  CGCalleeInfo(const FunctionProtoType *calleeProtoTy, GlobalDecl calleeDecl)
      : CalleeProtoTy(calleeProtoTy), CalleeDecl(calleeDecl) {}
  CGCalleeInfo(const FunctionProtoType *calleeProtoTy)
      : CalleeProtoTy(calleeProtoTy) {}
  CGCalleeInfo(GlobalDecl calleeDecl)
      : CalleeProtoTy(nullptr), CalleeDecl(calleeDecl) {}

  const FunctionProtoType *getCalleeFunctionProtoType() const {
    return CalleeProtoTy;
  }
  const GlobalDecl getCalleeDecl() const { return CalleeDecl; }
};

/// All available information about a concrete callee.
class CGCallee {
  enum class SpecialKind : uintptr_t {
    Invalid,
    Builtin,
    PseudoDestructor,
    Virtual,

    Last = Virtual
  };

  struct OrdinaryInfoStorage {
    CGCalleeInfo AbstractInfo;
    CGPointerAuthInfo PointerAuthInfo;
  };
  struct BuiltinInfoStorage {
    const FunctionDecl *Decl;
    unsigned ID;
  };
  struct PseudoDestructorInfoStorage {
    const CXXPseudoDestructorExpr *Expr;
  };
  struct VirtualInfoStorage {
    const CallExpr *CE;
    GlobalDecl MD;
    Address Addr;
    llvm::FunctionType *FTy;
  };

  SpecialKind KindOrFunctionPointer;
  union {
    OrdinaryInfoStorage OrdinaryInfo;
    BuiltinInfoStorage BuiltinInfo;
    PseudoDestructorInfoStorage PseudoDestructorInfo;
    VirtualInfoStorage VirtualInfo;
  };

  explicit CGCallee(SpecialKind kind) : KindOrFunctionPointer(kind) {}

  CGCallee(const FunctionDecl *builtinDecl, unsigned builtinID)
      : KindOrFunctionPointer(SpecialKind::Builtin) {
    BuiltinInfo.Decl = builtinDecl;
    BuiltinInfo.ID = builtinID;
  }

public:
  CGCallee() : KindOrFunctionPointer(SpecialKind::Invalid) {}

  /// Construct a callee.  Call this constructor directly when this
  /// isn't a direct call.
  CGCallee(const CGCalleeInfo &abstractInfo, llvm::Value *functionPtr,
           /* FIXME: make parameter pointerAuthInfo mandatory */
           const CGPointerAuthInfo &pointerAuthInfo = CGPointerAuthInfo())
      : KindOrFunctionPointer(
            SpecialKind(reinterpret_cast<uintptr_t>(functionPtr))) {
    OrdinaryInfo.AbstractInfo = abstractInfo;
    OrdinaryInfo.PointerAuthInfo = pointerAuthInfo;
    assert(functionPtr && "configuring callee without function pointer");
    assert(functionPtr->getType()->isPointerTy());
  }

  static CGCallee forBuiltin(unsigned builtinID,
                             const FunctionDecl *builtinDecl) {
    CGCallee result(SpecialKind::Builtin);
    result.BuiltinInfo.Decl = builtinDecl;
    result.BuiltinInfo.ID = builtinID;
    return result;
  }

  static CGCallee forPseudoDestructor(const CXXPseudoDestructorExpr *E) {
    CGCallee result(SpecialKind::PseudoDestructor);
    result.PseudoDestructorInfo.Expr = E;
    return result;
  }

  static CGCallee forDirect(llvm::Constant *functionPtr,
                            const CGCalleeInfo &abstractInfo = CGCalleeInfo()) {
    return CGCallee(abstractInfo, functionPtr);
  }

  static CGCallee forDirect(llvm::FunctionCallee functionPtr,
                            const CGCalleeInfo &abstractInfo = CGCalleeInfo()) {
    return CGCallee(abstractInfo, functionPtr.getCallee());
  }

  static CGCallee forVirtual(const CallExpr *CE, GlobalDecl MD, Address Addr,
                             llvm::FunctionType *FTy) {
    CGCallee result(SpecialKind::Virtual);
    result.VirtualInfo.CE = CE;
    result.VirtualInfo.MD = MD;
    result.VirtualInfo.Addr = Addr;
    result.VirtualInfo.FTy = FTy;
    return result;
  }

  bool isBuiltin() const {
    return KindOrFunctionPointer == SpecialKind::Builtin;
  }
  const FunctionDecl *getBuiltinDecl() const {
    assert(isBuiltin());
    return BuiltinInfo.Decl;
  }
  unsigned getBuiltinID() const {
    assert(isBuiltin());
    return BuiltinInfo.ID;
  }

  bool isPseudoDestructor() const {
    return KindOrFunctionPointer == SpecialKind::PseudoDestructor;
  }
  const CXXPseudoDestructorExpr *getPseudoDestructorExpr() const {
    assert(isPseudoDestructor());
    return PseudoDestructorInfo.Expr;
  }

  bool isOrdinary() const {
    return uintptr_t(KindOrFunctionPointer) > uintptr_t(SpecialKind::Last);
  }
  CGCalleeInfo getAbstractInfo() const {
    if (isVirtual())
      return VirtualInfo.MD;
    assert(isOrdinary());
    return OrdinaryInfo.AbstractInfo;
  }
  const CGPointerAuthInfo &getPointerAuthInfo() const {
    assert(isOrdinary());
    return OrdinaryInfo.PointerAuthInfo;
  }
  llvm::Value *getFunctionPointer() const {
    assert(isOrdinary());
    return reinterpret_cast<llvm::Value *>(uintptr_t(KindOrFunctionPointer));
  }
  void setFunctionPointer(llvm::Value *functionPtr) {
    assert(isOrdinary());
    KindOrFunctionPointer =
        SpecialKind(reinterpret_cast<uintptr_t>(functionPtr));
  }
  void setPointerAuthInfo(CGPointerAuthInfo PointerAuth) {
    assert(isOrdinary());
    OrdinaryInfo.PointerAuthInfo = PointerAuth;
  }

  bool isVirtual() const {
    return KindOrFunctionPointer == SpecialKind::Virtual;
  }
  const CallExpr *getVirtualCallExpr() const {
    assert(isVirtual());
    return VirtualInfo.CE;
  }
  GlobalDecl getVirtualMethodDecl() const {
    assert(isVirtual());
    return VirtualInfo.MD;
  }
  Address getThisAddress() const {
    assert(isVirtual());
    return VirtualInfo.Addr;
  }
  llvm::FunctionType *getVirtualFunctionType() const {
    assert(isVirtual());
    return VirtualInfo.FTy;
  }

  /// If this is a delayed callee computation of some sort, prepare
  /// a concrete callee.
  CGCallee prepareConcreteCallee(CodeGenFunction &CGF) const;
};

struct CallArg {
private:
  union {
    RValue RV;
    LValue LV; /// The argument is semantically a load from this l-value.
  };
  bool HasLV;

  /// A data-flow flag to make sure getRValue and/or copyInto are not
  /// called twice for duplicated IR emission.
  mutable bool IsUsed;

public:
  QualType Ty;
  CallArg(RValue rv, QualType ty)
      : RV(rv), HasLV(false), IsUsed(false), Ty(ty) {}
  CallArg(LValue lv, QualType ty)
      : LV(lv), HasLV(true), IsUsed(false), Ty(ty) {}
  bool hasLValue() const { return HasLV; }
  QualType getType() const { return Ty; }

  /// \returns an independent RValue. If the CallArg contains an LValue,
  /// a temporary copy is returned.
  RValue getRValue(CodeGenFunction &CGF) const;

  LValue getKnownLValue() const {
    assert(HasLV && !IsUsed);
    return LV;
  }
  RValue getKnownRValue() const {
    assert(!HasLV && !IsUsed);
    return RV;
  }
  void setRValue(RValue _RV) {
    assert(!HasLV);
    RV = _RV;
  }

  bool isAggregate() const { return HasLV || RV.isAggregate(); }

  void copyInto(CodeGenFunction &CGF, Address A) const;
};

/// CallArgList - Type for representing both the value and type of
/// arguments in a call.
class CallArgList : public SmallVector<CallArg, 8> {
public:
  CallArgList() = default;

  struct Writeback {
    /// The original argument.  Note that the argument l-value
    /// is potentially null.
    LValue Source;

    /// The temporary alloca.
    Address Temporary;

    /// A value to "use" after the writeback, or null.
    llvm::Value *ToUse;
  };

  struct CallArgCleanup {
    EHScopeStack::stable_iterator Cleanup;

    /// The "is active" insertion point.  This instruction is temporary and
    /// will be removed after insertion.
    llvm::Instruction *IsActiveIP;
  };

  void add(RValue rvalue, QualType type) { push_back(CallArg(rvalue, type)); }

  void addUncopiedAggregate(LValue LV, QualType type) {
    push_back(CallArg(LV, type));
  }

  /// Add all the arguments from another CallArgList to this one. After doing
  /// this, the old CallArgList retains its list of arguments, but must not
  /// be used to emit a call.
  void addFrom(const CallArgList &other) {
    insert(end(), other.begin(), other.end());
    Writebacks.insert(Writebacks.end(), other.Writebacks.begin(),
                      other.Writebacks.end());
    CleanupsToDeactivate.insert(CleanupsToDeactivate.end(),
                                other.CleanupsToDeactivate.begin(),
                                other.CleanupsToDeactivate.end());
    assert(!(StackBase && other.StackBase) && "can't merge stackbases");
    if (!StackBase)
      StackBase = other.StackBase;
  }

  void addWriteback(LValue srcLV, Address temporary, llvm::Value *toUse) {
    Writeback writeback = {srcLV, temporary, toUse};
    Writebacks.push_back(writeback);
  }

  bool hasWritebacks() const { return !Writebacks.empty(); }

  typedef llvm::iterator_range<SmallVectorImpl<Writeback>::const_iterator>
      writeback_const_range;

  writeback_const_range writebacks() const {
    return writeback_const_range(Writebacks.begin(), Writebacks.end());
  }

  void addArgCleanupDeactivation(EHScopeStack::stable_iterator Cleanup,
                                 llvm::Instruction *IsActiveIP) {
    CallArgCleanup ArgCleanup;
    ArgCleanup.Cleanup = Cleanup;
    ArgCleanup.IsActiveIP = IsActiveIP;
    CleanupsToDeactivate.push_back(ArgCleanup);
  }

  ArrayRef<CallArgCleanup> getCleanupsToDeactivate() const {
    return CleanupsToDeactivate;
  }

  void allocateArgumentMemory(CodeGenFunction &CGF);
  llvm::Instruction *getStackBase() const { return StackBase; }
  void freeArgumentMemory(CodeGenFunction &CGF) const;

  /// Returns if we're using an inalloca struct to pass arguments in
  /// memory.
  bool isUsingInAlloca() const { return StackBase; }

private:
  SmallVector<Writeback, 1> Writebacks;

  /// Deactivate these cleanups immediately before making the call.  This
  /// is used to cleanup objects that are owned by the callee once the call
  /// occurs.
  SmallVector<CallArgCleanup, 1> CleanupsToDeactivate;

  /// The stacksave call.  It dominates all of the argument evaluation.
  llvm::CallInst *StackBase = nullptr;
};

/// FunctionArgList - Type for representing both the decl and type
/// of parameters to a function. The decl must be either a
/// ParmVarDecl or ImplicitParamDecl.
class FunctionArgList : public SmallVector<const VarDecl *, 16> {};

/// ReturnValueSlot - Contains the address where the return value of a
/// function can be stored, and whether the address is volatile or not.
class ReturnValueSlot {
  Address Addr = Address::invalid();

  // Return value slot flags
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsVolatile : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsUnused : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsExternallyDestructed : 1;

public:
  ReturnValueSlot()
      : IsVolatile(false), IsUnused(false), IsExternallyDestructed(false) {}
  ReturnValueSlot(Address Addr, bool IsVolatile, bool IsUnused = false,
                  bool IsExternallyDestructed = false)
      : Addr(Addr), IsVolatile(IsVolatile), IsUnused(IsUnused),
        IsExternallyDestructed(IsExternallyDestructed) {}

  bool isNull() const { return !Addr.isValid(); }
  bool isVolatile() const { return IsVolatile; }
  Address getValue() const { return Addr; }
  bool isUnused() const { return IsUnused; }
  bool isExternallyDestructed() const { return IsExternallyDestructed; }
  Address getAddress() const { return Addr; }
};

/// Adds attributes to \p F according to our \p CodeGenOpts and \p LangOpts, as
/// though we had emitted it ourselves. We remove any attributes on F that
/// conflict with the attributes we add here.
///
/// This is useful for adding attrs to bitcode modules that you want to link
/// with but don't control, such as CUDA's libdevice.  When linking with such
/// a bitcode library, you might want to set e.g. its functions'
/// "unsafe-fp-math" attribute to match the attr of the functions you're
/// codegen'ing.  Otherwise, LLVM will interpret the bitcode module's lack of
/// unsafe-fp-math attrs as tantamount to unsafe-fp-math=false, and then LLVM
/// will propagate unsafe-fp-math=false up to every transitive caller of a
/// function in the bitcode library!
///
/// With the exception of fast-math attrs, this will only make the attributes
/// on the function more conservative.  But it's unsafe to call this on a
/// function which relies on particular fast-math attributes for correctness.
/// It's up to you to ensure that this is safe.
void mergeDefaultFunctionDefinitionAttributes(llvm::Function &F,
                                              const CodeGenOptions &CodeGenOpts,
                                              const LangOptions &LangOpts,
                                              const TargetOptions &TargetOpts,
                                              bool WillInternalize);

enum class FnInfoOpts {
  None = 0,
  IsInstanceMethod = 1 << 0,
  IsChainCall = 1 << 1,
  IsDelegateCall = 1 << 2,
};

inline FnInfoOpts operator|(FnInfoOpts A, FnInfoOpts B) {
  return static_cast<FnInfoOpts>(llvm::to_underlying(A) |
                                 llvm::to_underlying(B));
}

inline FnInfoOpts operator&(FnInfoOpts A, FnInfoOpts B) {
  return static_cast<FnInfoOpts>(llvm::to_underlying(A) &
                                 llvm::to_underlying(B));
}

inline FnInfoOpts operator|=(FnInfoOpts A, FnInfoOpts B) {
  A = A | B;
  return A;
}

inline FnInfoOpts operator&=(FnInfoOpts A, FnInfoOpts B) {
  A = A & B;
  return A;
}

} // end namespace CodeGen
} // end namespace clang

#endif
