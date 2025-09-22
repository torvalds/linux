//===-- CodeGenFunction.h - Per-Function state for LLVM CodeGen -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the internal per-function state used for llvm translation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENFUNCTION_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENFUNCTION_H

#include "CGBuilder.h"
#include "CGDebugInfo.h"
#include "CGLoopInfo.h"
#include "CGValue.h"
#include "CodeGenModule.h"
#include "CodeGenPGO.h"
#include "EHScopeStack.h"
#include "VarBypassDetector.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/CurrentSourceLocExprScope.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/Type.h"
#include "clang/Basic/ABI.h"
#include "clang/Basic/CapturedStmt.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/SanitizerStats.h"
#include <optional>

namespace llvm {
class BasicBlock;
class LLVMContext;
class MDNode;
class SwitchInst;
class Twine;
class Value;
class CanonicalLoopInfo;
}

namespace clang {
class ASTContext;
class CXXDestructorDecl;
class CXXForRangeStmt;
class CXXTryStmt;
class Decl;
class LabelDecl;
class FunctionDecl;
class FunctionProtoType;
class LabelStmt;
class ObjCContainerDecl;
class ObjCInterfaceDecl;
class ObjCIvarDecl;
class ObjCMethodDecl;
class ObjCImplementationDecl;
class ObjCPropertyImplDecl;
class TargetInfo;
class VarDecl;
class ObjCForCollectionStmt;
class ObjCAtTryStmt;
class ObjCAtThrowStmt;
class ObjCAtSynchronizedStmt;
class ObjCAutoreleasePoolStmt;
class OMPUseDevicePtrClause;
class OMPUseDeviceAddrClause;
class SVETypeFlags;
class OMPExecutableDirective;

namespace analyze_os_log {
class OSLogBufferLayout;
}

namespace CodeGen {
class CodeGenTypes;
class CGCallee;
class CGFunctionInfo;
class CGBlockInfo;
class CGCXXABI;
class BlockByrefHelpers;
class BlockByrefInfo;
class BlockFieldFlags;
class RegionCodeGenTy;
class TargetCodeGenInfo;
struct OMPTaskDataTy;
struct CGCoroData;

/// The kind of evaluation to perform on values of a particular
/// type.  Basically, is the code in CGExprScalar, CGExprComplex, or
/// CGExprAgg?
///
/// TODO: should vectors maybe be split out into their own thing?
enum TypeEvaluationKind {
  TEK_Scalar,
  TEK_Complex,
  TEK_Aggregate
};

#define LIST_SANITIZER_CHECKS                                                  \
  SANITIZER_CHECK(AddOverflow, add_overflow, 0)                                \
  SANITIZER_CHECK(BuiltinUnreachable, builtin_unreachable, 0)                  \
  SANITIZER_CHECK(CFICheckFail, cfi_check_fail, 0)                             \
  SANITIZER_CHECK(DivremOverflow, divrem_overflow, 0)                          \
  SANITIZER_CHECK(DynamicTypeCacheMiss, dynamic_type_cache_miss, 0)            \
  SANITIZER_CHECK(FloatCastOverflow, float_cast_overflow, 0)                   \
  SANITIZER_CHECK(FunctionTypeMismatch, function_type_mismatch, 0)             \
  SANITIZER_CHECK(ImplicitConversion, implicit_conversion, 0)                  \
  SANITIZER_CHECK(InvalidBuiltin, invalid_builtin, 0)                          \
  SANITIZER_CHECK(InvalidObjCCast, invalid_objc_cast, 0)                       \
  SANITIZER_CHECK(LoadInvalidValue, load_invalid_value, 0)                     \
  SANITIZER_CHECK(MissingReturn, missing_return, 0)                            \
  SANITIZER_CHECK(MulOverflow, mul_overflow, 0)                                \
  SANITIZER_CHECK(NegateOverflow, negate_overflow, 0)                          \
  SANITIZER_CHECK(NullabilityArg, nullability_arg, 0)                          \
  SANITIZER_CHECK(NullabilityReturn, nullability_return, 1)                    \
  SANITIZER_CHECK(NonnullArg, nonnull_arg, 0)                                  \
  SANITIZER_CHECK(NonnullReturn, nonnull_return, 1)                            \
  SANITIZER_CHECK(OutOfBounds, out_of_bounds, 0)                               \
  SANITIZER_CHECK(PointerOverflow, pointer_overflow, 0)                        \
  SANITIZER_CHECK(ShiftOutOfBounds, shift_out_of_bounds, 0)                    \
  SANITIZER_CHECK(SubOverflow, sub_overflow, 0)                                \
  SANITIZER_CHECK(TypeMismatch, type_mismatch, 1)                              \
  SANITIZER_CHECK(AlignmentAssumption, alignment_assumption, 0)                \
  SANITIZER_CHECK(VLABoundNotPositive, vla_bound_not_positive, 0)              \
  SANITIZER_CHECK(BoundsSafety, bounds_safety, 0)

enum SanitizerHandler {
#define SANITIZER_CHECK(Enum, Name, Version) Enum,
  LIST_SANITIZER_CHECKS
#undef SANITIZER_CHECK
};

/// Helper class with most of the code for saving a value for a
/// conditional expression cleanup.
struct DominatingLLVMValue {
  typedef llvm::PointerIntPair<llvm::Value*, 1, bool> saved_type;

  /// Answer whether the given value needs extra work to be saved.
  static bool needsSaving(llvm::Value *value) {
    if (!value)
      return false;

    // If it's not an instruction, we don't need to save.
    if (!isa<llvm::Instruction>(value)) return false;

    // If it's an instruction in the entry block, we don't need to save.
    llvm::BasicBlock *block = cast<llvm::Instruction>(value)->getParent();
    return (block != &block->getParent()->getEntryBlock());
  }

  static saved_type save(CodeGenFunction &CGF, llvm::Value *value);
  static llvm::Value *restore(CodeGenFunction &CGF, saved_type value);
};

/// A partial specialization of DominatingValue for llvm::Values that
/// might be llvm::Instructions.
template <class T> struct DominatingPointer<T,true> : DominatingLLVMValue {
  typedef T *type;
  static type restore(CodeGenFunction &CGF, saved_type value) {
    return static_cast<T*>(DominatingLLVMValue::restore(CGF, value));
  }
};

/// A specialization of DominatingValue for Address.
template <> struct DominatingValue<Address> {
  typedef Address type;

  struct saved_type {
    DominatingLLVMValue::saved_type BasePtr;
    llvm::Type *ElementType;
    CharUnits Alignment;
    DominatingLLVMValue::saved_type Offset;
    llvm::PointerType *EffectiveType;
  };

  static bool needsSaving(type value) {
    if (DominatingLLVMValue::needsSaving(value.getBasePointer()) ||
        DominatingLLVMValue::needsSaving(value.getOffset()))
      return true;
    return false;
  }
  static saved_type save(CodeGenFunction &CGF, type value) {
    return {DominatingLLVMValue::save(CGF, value.getBasePointer()),
            value.getElementType(), value.getAlignment(),
            DominatingLLVMValue::save(CGF, value.getOffset()), value.getType()};
  }
  static type restore(CodeGenFunction &CGF, saved_type value) {
    return Address(DominatingLLVMValue::restore(CGF, value.BasePtr),
                   value.ElementType, value.Alignment, CGPointerAuthInfo(),
                   DominatingLLVMValue::restore(CGF, value.Offset));
  }
};

/// A specialization of DominatingValue for RValue.
template <> struct DominatingValue<RValue> {
  typedef RValue type;
  class saved_type {
    enum Kind { ScalarLiteral, ScalarAddress, AggregateLiteral,
                AggregateAddress, ComplexAddress };
    union {
      struct {
        DominatingLLVMValue::saved_type first, second;
      } Vals;
      DominatingValue<Address>::saved_type AggregateAddr;
    };
    LLVM_PREFERRED_TYPE(Kind)
    unsigned K : 3;

    saved_type(DominatingLLVMValue::saved_type Val1, unsigned K)
        : Vals{Val1, DominatingLLVMValue::saved_type()}, K(K) {}

    saved_type(DominatingLLVMValue::saved_type Val1,
               DominatingLLVMValue::saved_type Val2)
        : Vals{Val1, Val2}, K(ComplexAddress) {}

    saved_type(DominatingValue<Address>::saved_type AggregateAddr, unsigned K)
        : AggregateAddr(AggregateAddr), K(K) {}

  public:
    static bool needsSaving(RValue value);
    static saved_type save(CodeGenFunction &CGF, RValue value);
    RValue restore(CodeGenFunction &CGF);

    // implementations in CGCleanup.cpp
  };

  static bool needsSaving(type value) {
    return saved_type::needsSaving(value);
  }
  static saved_type save(CodeGenFunction &CGF, type value) {
    return saved_type::save(CGF, value);
  }
  static type restore(CodeGenFunction &CGF, saved_type value) {
    return value.restore(CGF);
  }
};

/// CodeGenFunction - This class organizes the per-function state that is used
/// while generating LLVM code.
class CodeGenFunction : public CodeGenTypeCache {
  CodeGenFunction(const CodeGenFunction &) = delete;
  void operator=(const CodeGenFunction &) = delete;

  friend class CGCXXABI;
public:
  /// A jump destination is an abstract label, branching to which may
  /// require a jump out through normal cleanups.
  struct JumpDest {
    JumpDest() : Block(nullptr), Index(0) {}
    JumpDest(llvm::BasicBlock *Block, EHScopeStack::stable_iterator Depth,
             unsigned Index)
        : Block(Block), ScopeDepth(Depth), Index(Index) {}

    bool isValid() const { return Block != nullptr; }
    llvm::BasicBlock *getBlock() const { return Block; }
    EHScopeStack::stable_iterator getScopeDepth() const { return ScopeDepth; }
    unsigned getDestIndex() const { return Index; }

    // This should be used cautiously.
    void setScopeDepth(EHScopeStack::stable_iterator depth) {
      ScopeDepth = depth;
    }

  private:
    llvm::BasicBlock *Block;
    EHScopeStack::stable_iterator ScopeDepth;
    unsigned Index;
  };

  CodeGenModule &CGM;  // Per-module state.
  const TargetInfo &Target;

  // For EH/SEH outlined funclets, this field points to parent's CGF
  CodeGenFunction *ParentCGF = nullptr;

  typedef std::pair<llvm::Value *, llvm::Value *> ComplexPairTy;
  LoopInfoStack LoopStack;
  CGBuilderTy Builder;

  // Stores variables for which we can't generate correct lifetime markers
  // because of jumps.
  VarBypassDetector Bypasses;

  /// List of recently emitted OMPCanonicalLoops.
  ///
  /// Since OMPCanonicalLoops are nested inside other statements (in particular
  /// CapturedStmt generated by OMPExecutableDirective and non-perfectly nested
  /// loops), we cannot directly call OMPEmitOMPCanonicalLoop and receive its
  /// llvm::CanonicalLoopInfo. Instead, we call EmitStmt and any
  /// OMPEmitOMPCanonicalLoop called by it will add its CanonicalLoopInfo to
  /// this stack when done. Entering a new loop requires clearing this list; it
  /// either means we start parsing a new loop nest (in which case the previous
  /// loop nest goes out of scope) or a second loop in the same level in which
  /// case it would be ambiguous into which of the two (or more) loops the loop
  /// nest would extend.
  SmallVector<llvm::CanonicalLoopInfo *, 4> OMPLoopNestStack;

  /// Stack to track the Logical Operator recursion nest for MC/DC.
  SmallVector<const BinaryOperator *, 16> MCDCLogOpStack;

  /// Stack to track the controlled convergence tokens.
  SmallVector<llvm::IntrinsicInst *, 4> ConvergenceTokenStack;

  /// Number of nested loop to be consumed by the last surrounding
  /// loop-associated directive.
  int ExpectedOMPLoopDepth = 0;

  // CodeGen lambda for loops and support for ordered clause
  typedef llvm::function_ref<void(CodeGenFunction &, const OMPLoopDirective &,
                                  JumpDest)>
      CodeGenLoopTy;
  typedef llvm::function_ref<void(CodeGenFunction &, SourceLocation,
                                  const unsigned, const bool)>
      CodeGenOrderedTy;

  // Codegen lambda for loop bounds in worksharing loop constructs
  typedef llvm::function_ref<std::pair<LValue, LValue>(
      CodeGenFunction &, const OMPExecutableDirective &S)>
      CodeGenLoopBoundsTy;

  // Codegen lambda for loop bounds in dispatch-based loop implementation
  typedef llvm::function_ref<std::pair<llvm::Value *, llvm::Value *>(
      CodeGenFunction &, const OMPExecutableDirective &S, Address LB,
      Address UB)>
      CodeGenDispatchBoundsTy;

  /// CGBuilder insert helper. This function is called after an
  /// instruction is created using Builder.
  void InsertHelper(llvm::Instruction *I, const llvm::Twine &Name,
                    llvm::BasicBlock::iterator InsertPt) const;

  /// CurFuncDecl - Holds the Decl for the current outermost
  /// non-closure context.
  const Decl *CurFuncDecl = nullptr;
  /// CurCodeDecl - This is the inner-most code context, which includes blocks.
  const Decl *CurCodeDecl = nullptr;
  const CGFunctionInfo *CurFnInfo = nullptr;
  QualType FnRetTy;
  llvm::Function *CurFn = nullptr;

  /// Save Parameter Decl for coroutine.
  llvm::SmallVector<const ParmVarDecl *, 4> FnArgs;

  // Holds coroutine data if the current function is a coroutine. We use a
  // wrapper to manage its lifetime, so that we don't have to define CGCoroData
  // in this header.
  struct CGCoroInfo {
    std::unique_ptr<CGCoroData> Data;
    bool InSuspendBlock = false;
    CGCoroInfo();
    ~CGCoroInfo();
  };
  CGCoroInfo CurCoro;

  bool isCoroutine() const {
    return CurCoro.Data != nullptr;
  }

  bool inSuspendBlock() const {
    return isCoroutine() && CurCoro.InSuspendBlock;
  }

  // Holds FramePtr for await_suspend wrapper generation,
  // so that __builtin_coro_frame call can be lowered
  // directly to value of its second argument
  struct AwaitSuspendWrapperInfo {
    llvm::Value *FramePtr = nullptr;
  };
  AwaitSuspendWrapperInfo CurAwaitSuspendWrapper;

  // Generates wrapper function for `llvm.coro.await.suspend.*` intrinisics.
  // It encapsulates SuspendExpr in a function, to separate it's body
  // from the main coroutine to avoid miscompilations. Intrinisic
  // is lowered to this function call in CoroSplit pass
  // Function signature is:
  // <type> __await_suspend_wrapper_<name>(ptr %awaiter, ptr %hdl)
  // where type is one of (void, i1, ptr)
  llvm::Function *generateAwaitSuspendWrapper(Twine const &CoroName,
                                              Twine const &SuspendPointName,
                                              CoroutineSuspendExpr const &S);

  /// CurGD - The GlobalDecl for the current function being compiled.
  GlobalDecl CurGD;

  /// PrologueCleanupDepth - The cleanup depth enclosing all the
  /// cleanups associated with the parameters.
  EHScopeStack::stable_iterator PrologueCleanupDepth;

  /// ReturnBlock - Unified return block.
  JumpDest ReturnBlock;

  /// ReturnValue - The temporary alloca to hold the return
  /// value. This is invalid iff the function has no return value.
  Address ReturnValue = Address::invalid();

  /// ReturnValuePointer - The temporary alloca to hold a pointer to sret.
  /// This is invalid if sret is not in use.
  Address ReturnValuePointer = Address::invalid();

  /// If a return statement is being visited, this holds the return statment's
  /// result expression.
  const Expr *RetExpr = nullptr;

  /// Return true if a label was seen in the current scope.
  bool hasLabelBeenSeenInCurrentScope() const {
    if (CurLexicalScope)
      return CurLexicalScope->hasLabels();
    return !LabelMap.empty();
  }

  /// AllocaInsertPoint - This is an instruction in the entry block before which
  /// we prefer to insert allocas.
  llvm::AssertingVH<llvm::Instruction> AllocaInsertPt;

private:
  /// PostAllocaInsertPt - This is a place in the prologue where code can be
  /// inserted that will be dominated by all the static allocas. This helps
  /// achieve two things:
  ///   1. Contiguity of all static allocas (within the prologue) is maintained.
  ///   2. All other prologue code (which are dominated by static allocas) do
  ///      appear in the source order immediately after all static allocas.
  ///
  /// PostAllocaInsertPt will be lazily created when it is *really* required.
  llvm::AssertingVH<llvm::Instruction> PostAllocaInsertPt = nullptr;

public:
  /// Return PostAllocaInsertPt. If it is not yet created, then insert it
  /// immediately after AllocaInsertPt.
  llvm::Instruction *getPostAllocaInsertPoint() {
    if (!PostAllocaInsertPt) {
      assert(AllocaInsertPt &&
             "Expected static alloca insertion point at function prologue");
      assert(AllocaInsertPt->getParent()->isEntryBlock() &&
             "EBB should be entry block of the current code gen function");
      PostAllocaInsertPt = AllocaInsertPt->clone();
      PostAllocaInsertPt->setName("postallocapt");
      PostAllocaInsertPt->insertAfter(AllocaInsertPt);
    }

    return PostAllocaInsertPt;
  }

  /// API for captured statement code generation.
  class CGCapturedStmtInfo {
  public:
    explicit CGCapturedStmtInfo(CapturedRegionKind K = CR_Default)
        : Kind(K), ThisValue(nullptr), CXXThisFieldDecl(nullptr) {}
    explicit CGCapturedStmtInfo(const CapturedStmt &S,
                                CapturedRegionKind K = CR_Default)
      : Kind(K), ThisValue(nullptr), CXXThisFieldDecl(nullptr) {

      RecordDecl::field_iterator Field =
        S.getCapturedRecordDecl()->field_begin();
      for (CapturedStmt::const_capture_iterator I = S.capture_begin(),
                                                E = S.capture_end();
           I != E; ++I, ++Field) {
        if (I->capturesThis())
          CXXThisFieldDecl = *Field;
        else if (I->capturesVariable())
          CaptureFields[I->getCapturedVar()->getCanonicalDecl()] = *Field;
        else if (I->capturesVariableByCopy())
          CaptureFields[I->getCapturedVar()->getCanonicalDecl()] = *Field;
      }
    }

    virtual ~CGCapturedStmtInfo();

    CapturedRegionKind getKind() const { return Kind; }

    virtual void setContextValue(llvm::Value *V) { ThisValue = V; }
    // Retrieve the value of the context parameter.
    virtual llvm::Value *getContextValue() const { return ThisValue; }

    /// Lookup the captured field decl for a variable.
    virtual const FieldDecl *lookup(const VarDecl *VD) const {
      return CaptureFields.lookup(VD->getCanonicalDecl());
    }

    bool isCXXThisExprCaptured() const { return getThisFieldDecl() != nullptr; }
    virtual FieldDecl *getThisFieldDecl() const { return CXXThisFieldDecl; }

    static bool classof(const CGCapturedStmtInfo *) {
      return true;
    }

    /// Emit the captured statement body.
    virtual void EmitBody(CodeGenFunction &CGF, const Stmt *S) {
      CGF.incrementProfileCounter(S);
      CGF.EmitStmt(S);
    }

    /// Get the name of the capture helper.
    virtual StringRef getHelperName() const { return "__captured_stmt"; }

    /// Get the CaptureFields
    llvm::SmallDenseMap<const VarDecl *, FieldDecl *> getCaptureFields() {
      return CaptureFields;
    }

  private:
    /// The kind of captured statement being generated.
    CapturedRegionKind Kind;

    /// Keep the map between VarDecl and FieldDecl.
    llvm::SmallDenseMap<const VarDecl *, FieldDecl *> CaptureFields;

    /// The base address of the captured record, passed in as the first
    /// argument of the parallel region function.
    llvm::Value *ThisValue;

    /// Captured 'this' type.
    FieldDecl *CXXThisFieldDecl;
  };
  CGCapturedStmtInfo *CapturedStmtInfo = nullptr;

  /// RAII for correct setting/restoring of CapturedStmtInfo.
  class CGCapturedStmtRAII {
  private:
    CodeGenFunction &CGF;
    CGCapturedStmtInfo *PrevCapturedStmtInfo;
  public:
    CGCapturedStmtRAII(CodeGenFunction &CGF,
                       CGCapturedStmtInfo *NewCapturedStmtInfo)
        : CGF(CGF), PrevCapturedStmtInfo(CGF.CapturedStmtInfo) {
      CGF.CapturedStmtInfo = NewCapturedStmtInfo;
    }
    ~CGCapturedStmtRAII() { CGF.CapturedStmtInfo = PrevCapturedStmtInfo; }
  };

  /// An abstract representation of regular/ObjC call/message targets.
  class AbstractCallee {
    /// The function declaration of the callee.
    const Decl *CalleeDecl;

  public:
    AbstractCallee() : CalleeDecl(nullptr) {}
    AbstractCallee(const FunctionDecl *FD) : CalleeDecl(FD) {}
    AbstractCallee(const ObjCMethodDecl *OMD) : CalleeDecl(OMD) {}
    bool hasFunctionDecl() const {
      return isa_and_nonnull<FunctionDecl>(CalleeDecl);
    }
    const Decl *getDecl() const { return CalleeDecl; }
    unsigned getNumParams() const {
      if (const auto *FD = dyn_cast<FunctionDecl>(CalleeDecl))
        return FD->getNumParams();
      return cast<ObjCMethodDecl>(CalleeDecl)->param_size();
    }
    const ParmVarDecl *getParamDecl(unsigned I) const {
      if (const auto *FD = dyn_cast<FunctionDecl>(CalleeDecl))
        return FD->getParamDecl(I);
      return *(cast<ObjCMethodDecl>(CalleeDecl)->param_begin() + I);
    }
  };

  /// Sanitizers enabled for this function.
  SanitizerSet SanOpts;

  /// True if CodeGen currently emits code implementing sanitizer checks.
  bool IsSanitizerScope = false;

  /// RAII object to set/unset CodeGenFunction::IsSanitizerScope.
  class SanitizerScope {
    CodeGenFunction *CGF;
  public:
    SanitizerScope(CodeGenFunction *CGF);
    ~SanitizerScope();
  };

  /// In C++, whether we are code generating a thunk.  This controls whether we
  /// should emit cleanups.
  bool CurFuncIsThunk = false;

  /// In ARC, whether we should autorelease the return value.
  bool AutoreleaseResult = false;

  /// Whether we processed a Microsoft-style asm block during CodeGen. These can
  /// potentially set the return value.
  bool SawAsmBlock = false;

  GlobalDecl CurSEHParent;

  /// True if the current function is an outlined SEH helper. This can be a
  /// finally block or filter expression.
  bool IsOutlinedSEHHelper = false;

  /// True if CodeGen currently emits code inside presereved access index
  /// region.
  bool IsInPreservedAIRegion = false;

  /// True if the current statement has nomerge attribute.
  bool InNoMergeAttributedStmt = false;

  /// True if the current statement has noinline attribute.
  bool InNoInlineAttributedStmt = false;

  /// True if the current statement has always_inline attribute.
  bool InAlwaysInlineAttributedStmt = false;

  // The CallExpr within the current statement that the musttail attribute
  // applies to.  nullptr if there is no 'musttail' on the current statement.
  const CallExpr *MustTailCall = nullptr;

  /// Returns true if a function must make progress, which means the
  /// mustprogress attribute can be added.
  bool checkIfFunctionMustProgress() {
    if (CGM.getCodeGenOpts().getFiniteLoops() ==
        CodeGenOptions::FiniteLoopsKind::Never)
      return false;

    // C++11 and later guarantees that a thread eventually will do one of the
    // following (C++11 [intro.multithread]p24 and C++17 [intro.progress]p1):
    // - terminate,
    //  - make a call to a library I/O function,
    //  - perform an access through a volatile glvalue, or
    //  - perform a synchronization operation or an atomic operation.
    //
    // Hence each function is 'mustprogress' in C++11 or later.
    return getLangOpts().CPlusPlus11;
  }

  /// Returns true if a loop must make progress, which means the mustprogress
  /// attribute can be added. \p HasConstantCond indicates whether the branch
  /// condition is a known constant.
  bool checkIfLoopMustProgress(const Expr *, bool HasEmptyBody);

  const CodeGen::CGBlockInfo *BlockInfo = nullptr;
  llvm::Value *BlockPointer = nullptr;

  llvm::DenseMap<const ValueDecl *, FieldDecl *> LambdaCaptureFields;
  FieldDecl *LambdaThisCaptureField = nullptr;

  /// A mapping from NRVO variables to the flags used to indicate
  /// when the NRVO has been applied to this variable.
  llvm::DenseMap<const VarDecl *, llvm::Value *> NRVOFlags;

  EHScopeStack EHStack;
  llvm::SmallVector<char, 256> LifetimeExtendedCleanupStack;

  // A stack of cleanups which were added to EHStack but have to be deactivated
  // later before being popped or emitted. These are usually deactivated on
  // exiting a `CleanupDeactivationScope` scope. For instance, after a
  // full-expr.
  //
  // These are specially useful for correctly emitting cleanups while
  // encountering branches out of expression (through stmt-expr or coroutine
  // suspensions).
  struct DeferredDeactivateCleanup {
    EHScopeStack::stable_iterator Cleanup;
    llvm::Instruction *DominatingIP;
  };
  llvm::SmallVector<DeferredDeactivateCleanup> DeferredDeactivationCleanupStack;

  // Enters a new scope for capturing cleanups which are deferred to be
  // deactivated, all of which will be deactivated once the scope is exited.
  struct CleanupDeactivationScope {
    CodeGenFunction &CGF;
    size_t OldDeactivateCleanupStackSize;
    bool Deactivated;
    CleanupDeactivationScope(CodeGenFunction &CGF)
        : CGF(CGF), OldDeactivateCleanupStackSize(
                        CGF.DeferredDeactivationCleanupStack.size()),
          Deactivated(false) {}

    void ForceDeactivate() {
      assert(!Deactivated && "Deactivating already deactivated scope");
      auto &Stack = CGF.DeferredDeactivationCleanupStack;
      for (size_t I = Stack.size(); I > OldDeactivateCleanupStackSize; I--) {
        CGF.DeactivateCleanupBlock(Stack[I - 1].Cleanup,
                                   Stack[I - 1].DominatingIP);
        Stack[I - 1].DominatingIP->eraseFromParent();
      }
      Stack.resize(OldDeactivateCleanupStackSize);
      Deactivated = true;
    }

    ~CleanupDeactivationScope() {
      if (Deactivated)
        return;
      ForceDeactivate();
    }
  };

  llvm::SmallVector<const JumpDest *, 2> SEHTryEpilogueStack;

  llvm::Instruction *CurrentFuncletPad = nullptr;

  class CallLifetimeEnd final : public EHScopeStack::Cleanup {
    bool isRedundantBeforeReturn() override { return true; }

    llvm::Value *Addr;
    llvm::Value *Size;

  public:
    CallLifetimeEnd(RawAddress addr, llvm::Value *size)
        : Addr(addr.getPointer()), Size(size) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.EmitLifetimeEnd(Size, Addr);
    }
  };

  /// Header for data within LifetimeExtendedCleanupStack.
  struct LifetimeExtendedCleanupHeader {
    /// The size of the following cleanup object.
    unsigned Size;
    /// The kind of cleanup to push.
    LLVM_PREFERRED_TYPE(CleanupKind)
    unsigned Kind : 31;
    /// Whether this is a conditional cleanup.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsConditional : 1;

    size_t getSize() const { return Size; }
    CleanupKind getKind() const { return (CleanupKind)Kind; }
    bool isConditional() const { return IsConditional; }
  };

  /// i32s containing the indexes of the cleanup destinations.
  RawAddress NormalCleanupDest = RawAddress::invalid();

  unsigned NextCleanupDestIndex = 1;

  /// EHResumeBlock - Unified block containing a call to llvm.eh.resume.
  llvm::BasicBlock *EHResumeBlock = nullptr;

  /// The exception slot.  All landing pads write the current exception pointer
  /// into this alloca.
  llvm::Value *ExceptionSlot = nullptr;

  /// The selector slot.  Under the MandatoryCleanup model, all landing pads
  /// write the current selector value into this alloca.
  llvm::AllocaInst *EHSelectorSlot = nullptr;

  /// A stack of exception code slots. Entering an __except block pushes a slot
  /// on the stack and leaving pops one. The __exception_code() intrinsic loads
  /// a value from the top of the stack.
  SmallVector<Address, 1> SEHCodeSlotStack;

  /// Value returned by __exception_info intrinsic.
  llvm::Value *SEHInfo = nullptr;

  /// Emits a landing pad for the current EH stack.
  llvm::BasicBlock *EmitLandingPad();

  llvm::BasicBlock *getInvokeDestImpl();

  /// Parent loop-based directive for scan directive.
  const OMPExecutableDirective *OMPParentLoopDirectiveForScan = nullptr;
  llvm::BasicBlock *OMPBeforeScanBlock = nullptr;
  llvm::BasicBlock *OMPAfterScanBlock = nullptr;
  llvm::BasicBlock *OMPScanExitBlock = nullptr;
  llvm::BasicBlock *OMPScanDispatch = nullptr;
  bool OMPFirstScanLoop = false;

  /// Manages parent directive for scan directives.
  class ParentLoopDirectiveForScanRegion {
    CodeGenFunction &CGF;
    const OMPExecutableDirective *ParentLoopDirectiveForScan;

  public:
    ParentLoopDirectiveForScanRegion(
        CodeGenFunction &CGF,
        const OMPExecutableDirective &ParentLoopDirectiveForScan)
        : CGF(CGF),
          ParentLoopDirectiveForScan(CGF.OMPParentLoopDirectiveForScan) {
      CGF.OMPParentLoopDirectiveForScan = &ParentLoopDirectiveForScan;
    }
    ~ParentLoopDirectiveForScanRegion() {
      CGF.OMPParentLoopDirectiveForScan = ParentLoopDirectiveForScan;
    }
  };

  template <class T>
  typename DominatingValue<T>::saved_type saveValueInCond(T value) {
    return DominatingValue<T>::save(*this, value);
  }

  class CGFPOptionsRAII {
  public:
    CGFPOptionsRAII(CodeGenFunction &CGF, FPOptions FPFeatures);
    CGFPOptionsRAII(CodeGenFunction &CGF, const Expr *E);
    ~CGFPOptionsRAII();

  private:
    void ConstructorHelper(FPOptions FPFeatures);
    CodeGenFunction &CGF;
    FPOptions OldFPFeatures;
    llvm::fp::ExceptionBehavior OldExcept;
    llvm::RoundingMode OldRounding;
    std::optional<CGBuilderTy::FastMathFlagGuard> FMFGuard;
  };
  FPOptions CurFPFeatures;

public:
  /// ObjCEHValueStack - Stack of Objective-C exception values, used for
  /// rethrows.
  SmallVector<llvm::Value*, 8> ObjCEHValueStack;

  /// A class controlling the emission of a finally block.
  class FinallyInfo {
    /// Where the catchall's edge through the cleanup should go.
    JumpDest RethrowDest;

    /// A function to call to enter the catch.
    llvm::FunctionCallee BeginCatchFn;

    /// An i1 variable indicating whether or not the @finally is
    /// running for an exception.
    llvm::AllocaInst *ForEHVar = nullptr;

    /// An i8* variable into which the exception pointer to rethrow
    /// has been saved.
    llvm::AllocaInst *SavedExnVar = nullptr;

  public:
    void enter(CodeGenFunction &CGF, const Stmt *Finally,
               llvm::FunctionCallee beginCatchFn,
               llvm::FunctionCallee endCatchFn, llvm::FunctionCallee rethrowFn);
    void exit(CodeGenFunction &CGF);
  };

  /// Returns true inside SEH __try blocks.
  bool isSEHTryScope() const { return !SEHTryEpilogueStack.empty(); }

  /// Returns true while emitting a cleanuppad.
  bool isCleanupPadScope() const {
    return CurrentFuncletPad && isa<llvm::CleanupPadInst>(CurrentFuncletPad);
  }

  /// pushFullExprCleanup - Push a cleanup to be run at the end of the
  /// current full-expression.  Safe against the possibility that
  /// we're currently inside a conditionally-evaluated expression.
  template <class T, class... As>
  void pushFullExprCleanup(CleanupKind kind, As... A) {
    // If we're not in a conditional branch, or if none of the
    // arguments requires saving, then use the unconditional cleanup.
    if (!isInConditionalBranch())
      return EHStack.pushCleanup<T>(kind, A...);

    // Stash values in a tuple so we can guarantee the order of saves.
    typedef std::tuple<typename DominatingValue<As>::saved_type...> SavedTuple;
    SavedTuple Saved{saveValueInCond(A)...};

    typedef EHScopeStack::ConditionalCleanup<T, As...> CleanupType;
    EHStack.pushCleanupTuple<CleanupType>(kind, Saved);
    initFullExprCleanup();
  }

  /// Queue a cleanup to be pushed after finishing the current full-expression,
  /// potentially with an active flag.
  template <class T, class... As>
  void pushCleanupAfterFullExpr(CleanupKind Kind, As... A) {
    if (!isInConditionalBranch())
      return pushCleanupAfterFullExprWithActiveFlag<T>(
          Kind, RawAddress::invalid(), A...);

    RawAddress ActiveFlag = createCleanupActiveFlag();
    assert(!DominatingValue<Address>::needsSaving(ActiveFlag) &&
           "cleanup active flag should never need saving");

    typedef std::tuple<typename DominatingValue<As>::saved_type...> SavedTuple;
    SavedTuple Saved{saveValueInCond(A)...};

    typedef EHScopeStack::ConditionalCleanup<T, As...> CleanupType;
    pushCleanupAfterFullExprWithActiveFlag<CleanupType>(Kind, ActiveFlag, Saved);
  }

  template <class T, class... As>
  void pushCleanupAfterFullExprWithActiveFlag(CleanupKind Kind,
                                              RawAddress ActiveFlag, As... A) {
    LifetimeExtendedCleanupHeader Header = {sizeof(T), Kind,
                                            ActiveFlag.isValid()};

    size_t OldSize = LifetimeExtendedCleanupStack.size();
    LifetimeExtendedCleanupStack.resize(
        LifetimeExtendedCleanupStack.size() + sizeof(Header) + Header.Size +
        (Header.IsConditional ? sizeof(ActiveFlag) : 0));

    static_assert(sizeof(Header) % alignof(T) == 0,
                  "Cleanup will be allocated on misaligned address");
    char *Buffer = &LifetimeExtendedCleanupStack[OldSize];
    new (Buffer) LifetimeExtendedCleanupHeader(Header);
    new (Buffer + sizeof(Header)) T(A...);
    if (Header.IsConditional)
      new (Buffer + sizeof(Header) + sizeof(T)) RawAddress(ActiveFlag);
  }

  // Push a cleanup onto EHStack and deactivate it later. It is usually
  // deactivated when exiting a `CleanupDeactivationScope` (for example: after a
  // full expression).
  template <class T, class... As>
  void pushCleanupAndDeferDeactivation(CleanupKind Kind, As... A) {
    // Placeholder dominating IP for this cleanup.
    llvm::Instruction *DominatingIP =
        Builder.CreateFlagLoad(llvm::Constant::getNullValue(Int8PtrTy));
    EHStack.pushCleanup<T>(Kind, A...);
    DeferredDeactivationCleanupStack.push_back(
        {EHStack.stable_begin(), DominatingIP});
  }

  /// Set up the last cleanup that was pushed as a conditional
  /// full-expression cleanup.
  void initFullExprCleanup() {
    initFullExprCleanupWithFlag(createCleanupActiveFlag());
  }

  void initFullExprCleanupWithFlag(RawAddress ActiveFlag);
  RawAddress createCleanupActiveFlag();

  /// PushDestructorCleanup - Push a cleanup to call the
  /// complete-object destructor of an object of the given type at the
  /// given address.  Does nothing if T is not a C++ class type with a
  /// non-trivial destructor.
  void PushDestructorCleanup(QualType T, Address Addr);

  /// PushDestructorCleanup - Push a cleanup to call the
  /// complete-object variant of the given destructor on the object at
  /// the given address.
  void PushDestructorCleanup(const CXXDestructorDecl *Dtor, QualType T,
                             Address Addr);

  /// PopCleanupBlock - Will pop the cleanup entry on the stack and
  /// process all branch fixups.
  void PopCleanupBlock(bool FallThroughIsBranchThrough = false,
                       bool ForDeactivation = false);

  /// DeactivateCleanupBlock - Deactivates the given cleanup block.
  /// The block cannot be reactivated.  Pops it if it's the top of the
  /// stack.
  ///
  /// \param DominatingIP - An instruction which is known to
  ///   dominate the current IP (if set) and which lies along
  ///   all paths of execution between the current IP and the
  ///   the point at which the cleanup comes into scope.
  void DeactivateCleanupBlock(EHScopeStack::stable_iterator Cleanup,
                              llvm::Instruction *DominatingIP);

  /// ActivateCleanupBlock - Activates an initially-inactive cleanup.
  /// Cannot be used to resurrect a deactivated cleanup.
  ///
  /// \param DominatingIP - An instruction which is known to
  ///   dominate the current IP (if set) and which lies along
  ///   all paths of execution between the current IP and the
  ///   the point at which the cleanup comes into scope.
  void ActivateCleanupBlock(EHScopeStack::stable_iterator Cleanup,
                            llvm::Instruction *DominatingIP);

  /// Enters a new scope for capturing cleanups, all of which
  /// will be executed once the scope is exited.
  class RunCleanupsScope {
    EHScopeStack::stable_iterator CleanupStackDepth, OldCleanupScopeDepth;
    size_t LifetimeExtendedCleanupStackSize;
    CleanupDeactivationScope DeactivateCleanups;
    bool OldDidCallStackSave;
  protected:
    bool PerformCleanup;
  private:

    RunCleanupsScope(const RunCleanupsScope &) = delete;
    void operator=(const RunCleanupsScope &) = delete;

  protected:
    CodeGenFunction& CGF;

  public:
    /// Enter a new cleanup scope.
    explicit RunCleanupsScope(CodeGenFunction &CGF)
        : DeactivateCleanups(CGF), PerformCleanup(true), CGF(CGF) {
      CleanupStackDepth = CGF.EHStack.stable_begin();
      LifetimeExtendedCleanupStackSize =
          CGF.LifetimeExtendedCleanupStack.size();
      OldDidCallStackSave = CGF.DidCallStackSave;
      CGF.DidCallStackSave = false;
      OldCleanupScopeDepth = CGF.CurrentCleanupScopeDepth;
      CGF.CurrentCleanupScopeDepth = CleanupStackDepth;
    }

    /// Exit this cleanup scope, emitting any accumulated cleanups.
    ~RunCleanupsScope() {
      if (PerformCleanup)
        ForceCleanup();
    }

    /// Determine whether this scope requires any cleanups.
    bool requiresCleanups() const {
      return CGF.EHStack.stable_begin() != CleanupStackDepth;
    }

    /// Force the emission of cleanups now, instead of waiting
    /// until this object is destroyed.
    /// \param ValuesToReload - A list of values that need to be available at
    /// the insertion point after cleanup emission. If cleanup emission created
    /// a shared cleanup block, these value pointers will be rewritten.
    /// Otherwise, they not will be modified.
    void ForceCleanup(std::initializer_list<llvm::Value**> ValuesToReload = {}) {
      assert(PerformCleanup && "Already forced cleanup");
      CGF.DidCallStackSave = OldDidCallStackSave;
      DeactivateCleanups.ForceDeactivate();
      CGF.PopCleanupBlocks(CleanupStackDepth, LifetimeExtendedCleanupStackSize,
                           ValuesToReload);
      PerformCleanup = false;
      CGF.CurrentCleanupScopeDepth = OldCleanupScopeDepth;
    }
  };

  // Cleanup stack depth of the RunCleanupsScope that was pushed most recently.
  EHScopeStack::stable_iterator CurrentCleanupScopeDepth =
      EHScopeStack::stable_end();

  class LexicalScope : public RunCleanupsScope {
    SourceRange Range;
    SmallVector<const LabelDecl*, 4> Labels;
    LexicalScope *ParentScope;

    LexicalScope(const LexicalScope &) = delete;
    void operator=(const LexicalScope &) = delete;

  public:
    /// Enter a new cleanup scope.
    explicit LexicalScope(CodeGenFunction &CGF, SourceRange Range)
      : RunCleanupsScope(CGF), Range(Range), ParentScope(CGF.CurLexicalScope) {
      CGF.CurLexicalScope = this;
      if (CGDebugInfo *DI = CGF.getDebugInfo())
        DI->EmitLexicalBlockStart(CGF.Builder, Range.getBegin());
    }

    void addLabel(const LabelDecl *label) {
      assert(PerformCleanup && "adding label to dead scope?");
      Labels.push_back(label);
    }

    /// Exit this cleanup scope, emitting any accumulated
    /// cleanups.
    ~LexicalScope() {
      if (CGDebugInfo *DI = CGF.getDebugInfo())
        DI->EmitLexicalBlockEnd(CGF.Builder, Range.getEnd());

      // If we should perform a cleanup, force them now.  Note that
      // this ends the cleanup scope before rescoping any labels.
      if (PerformCleanup) {
        ApplyDebugLocation DL(CGF, Range.getEnd());
        ForceCleanup();
      }
    }

    /// Force the emission of cleanups now, instead of waiting
    /// until this object is destroyed.
    void ForceCleanup() {
      CGF.CurLexicalScope = ParentScope;
      RunCleanupsScope::ForceCleanup();

      if (!Labels.empty())
        rescopeLabels();
    }

    bool hasLabels() const {
      return !Labels.empty();
    }

    void rescopeLabels();
  };

  typedef llvm::DenseMap<const Decl *, Address> DeclMapTy;

  /// The class used to assign some variables some temporarily addresses.
  class OMPMapVars {
    DeclMapTy SavedLocals;
    DeclMapTy SavedTempAddresses;
    OMPMapVars(const OMPMapVars &) = delete;
    void operator=(const OMPMapVars &) = delete;

  public:
    explicit OMPMapVars() = default;
    ~OMPMapVars() {
      assert(SavedLocals.empty() && "Did not restored original addresses.");
    };

    /// Sets the address of the variable \p LocalVD to be \p TempAddr in
    /// function \p CGF.
    /// \return true if at least one variable was set already, false otherwise.
    bool setVarAddr(CodeGenFunction &CGF, const VarDecl *LocalVD,
                    Address TempAddr) {
      LocalVD = LocalVD->getCanonicalDecl();
      // Only save it once.
      if (SavedLocals.count(LocalVD)) return false;

      // Copy the existing local entry to SavedLocals.
      auto it = CGF.LocalDeclMap.find(LocalVD);
      if (it != CGF.LocalDeclMap.end())
        SavedLocals.try_emplace(LocalVD, it->second);
      else
        SavedLocals.try_emplace(LocalVD, Address::invalid());

      // Generate the private entry.
      QualType VarTy = LocalVD->getType();
      if (VarTy->isReferenceType()) {
        Address Temp = CGF.CreateMemTemp(VarTy);
        CGF.Builder.CreateStore(TempAddr.emitRawPointer(CGF), Temp);
        TempAddr = Temp;
      }
      SavedTempAddresses.try_emplace(LocalVD, TempAddr);

      return true;
    }

    /// Applies new addresses to the list of the variables.
    /// \return true if at least one variable is using new address, false
    /// otherwise.
    bool apply(CodeGenFunction &CGF) {
      copyInto(SavedTempAddresses, CGF.LocalDeclMap);
      SavedTempAddresses.clear();
      return !SavedLocals.empty();
    }

    /// Restores original addresses of the variables.
    void restore(CodeGenFunction &CGF) {
      if (!SavedLocals.empty()) {
        copyInto(SavedLocals, CGF.LocalDeclMap);
        SavedLocals.clear();
      }
    }

  private:
    /// Copy all the entries in the source map over the corresponding
    /// entries in the destination, which must exist.
    static void copyInto(const DeclMapTy &Src, DeclMapTy &Dest) {
      for (auto &Pair : Src) {
        if (!Pair.second.isValid()) {
          Dest.erase(Pair.first);
          continue;
        }

        auto I = Dest.find(Pair.first);
        if (I != Dest.end())
          I->second = Pair.second;
        else
          Dest.insert(Pair);
      }
    }
  };

  /// The scope used to remap some variables as private in the OpenMP loop body
  /// (or other captured region emitted without outlining), and to restore old
  /// vars back on exit.
  class OMPPrivateScope : public RunCleanupsScope {
    OMPMapVars MappedVars;
    OMPPrivateScope(const OMPPrivateScope &) = delete;
    void operator=(const OMPPrivateScope &) = delete;

  public:
    /// Enter a new OpenMP private scope.
    explicit OMPPrivateScope(CodeGenFunction &CGF) : RunCleanupsScope(CGF) {}

    /// Registers \p LocalVD variable as a private with \p Addr as the address
    /// of the corresponding private variable. \p
    /// PrivateGen is the address of the generated private variable.
    /// \return true if the variable is registered as private, false if it has
    /// been privatized already.
    bool addPrivate(const VarDecl *LocalVD, Address Addr) {
      assert(PerformCleanup && "adding private to dead scope");
      return MappedVars.setVarAddr(CGF, LocalVD, Addr);
    }

    /// Privatizes local variables previously registered as private.
    /// Registration is separate from the actual privatization to allow
    /// initializers use values of the original variables, not the private one.
    /// This is important, for example, if the private variable is a class
    /// variable initialized by a constructor that references other private
    /// variables. But at initialization original variables must be used, not
    /// private copies.
    /// \return true if at least one variable was privatized, false otherwise.
    bool Privatize() { return MappedVars.apply(CGF); }

    void ForceCleanup() {
      RunCleanupsScope::ForceCleanup();
      restoreMap();
    }

    /// Exit scope - all the mapped variables are restored.
    ~OMPPrivateScope() {
      if (PerformCleanup)
        ForceCleanup();
    }

    /// Checks if the global variable is captured in current function.
    bool isGlobalVarCaptured(const VarDecl *VD) const {
      VD = VD->getCanonicalDecl();
      return !VD->isLocalVarDeclOrParm() && CGF.LocalDeclMap.count(VD) > 0;
    }

    /// Restore all mapped variables w/o clean up. This is usefully when we want
    /// to reference the original variables but don't want the clean up because
    /// that could emit lifetime end too early, causing backend issue #56913.
    void restoreMap() { MappedVars.restore(CGF); }
  };

  /// Save/restore original map of previously emitted local vars in case when we
  /// need to duplicate emission of the same code several times in the same
  /// function for OpenMP code.
  class OMPLocalDeclMapRAII {
    CodeGenFunction &CGF;
    DeclMapTy SavedMap;

  public:
    OMPLocalDeclMapRAII(CodeGenFunction &CGF)
        : CGF(CGF), SavedMap(CGF.LocalDeclMap) {}
    ~OMPLocalDeclMapRAII() { SavedMap.swap(CGF.LocalDeclMap); }
  };

  /// Takes the old cleanup stack size and emits the cleanup blocks
  /// that have been added.
  void
  PopCleanupBlocks(EHScopeStack::stable_iterator OldCleanupStackSize,
                   std::initializer_list<llvm::Value **> ValuesToReload = {});

  /// Takes the old cleanup stack size and emits the cleanup blocks
  /// that have been added, then adds all lifetime-extended cleanups from
  /// the given position to the stack.
  void
  PopCleanupBlocks(EHScopeStack::stable_iterator OldCleanupStackSize,
                   size_t OldLifetimeExtendedStackSize,
                   std::initializer_list<llvm::Value **> ValuesToReload = {});

  void ResolveBranchFixups(llvm::BasicBlock *Target);

  /// The given basic block lies in the current EH scope, but may be a
  /// target of a potentially scope-crossing jump; get a stable handle
  /// to which we can perform this jump later.
  JumpDest getJumpDestInCurrentScope(llvm::BasicBlock *Target) {
    return JumpDest(Target,
                    EHStack.getInnermostNormalCleanup(),
                    NextCleanupDestIndex++);
  }

  /// The given basic block lies in the current EH scope, but may be a
  /// target of a potentially scope-crossing jump; get a stable handle
  /// to which we can perform this jump later.
  JumpDest getJumpDestInCurrentScope(StringRef Name = StringRef()) {
    return getJumpDestInCurrentScope(createBasicBlock(Name));
  }

  /// EmitBranchThroughCleanup - Emit a branch from the current insert
  /// block through the normal cleanup handling code (if any) and then
  /// on to \arg Dest.
  void EmitBranchThroughCleanup(JumpDest Dest);

  /// isObviouslyBranchWithoutCleanups - Return true if a branch to the
  /// specified destination obviously has no cleanups to run.  'false' is always
  /// a conservatively correct answer for this method.
  bool isObviouslyBranchWithoutCleanups(JumpDest Dest) const;

  /// popCatchScope - Pops the catch scope at the top of the EHScope
  /// stack, emitting any required code (other than the catch handlers
  /// themselves).
  void popCatchScope();

  llvm::BasicBlock *getEHResumeBlock(bool isCleanup);
  llvm::BasicBlock *getEHDispatchBlock(EHScopeStack::stable_iterator scope);
  llvm::BasicBlock *
  getFuncletEHDispatchBlock(EHScopeStack::stable_iterator scope);

  /// An object to manage conditionally-evaluated expressions.
  class ConditionalEvaluation {
    llvm::BasicBlock *StartBB;

  public:
    ConditionalEvaluation(CodeGenFunction &CGF)
      : StartBB(CGF.Builder.GetInsertBlock()) {}

    void begin(CodeGenFunction &CGF) {
      assert(CGF.OutermostConditional != this);
      if (!CGF.OutermostConditional)
        CGF.OutermostConditional = this;
    }

    void end(CodeGenFunction &CGF) {
      assert(CGF.OutermostConditional != nullptr);
      if (CGF.OutermostConditional == this)
        CGF.OutermostConditional = nullptr;
    }

    /// Returns a block which will be executed prior to each
    /// evaluation of the conditional code.
    llvm::BasicBlock *getStartingBlock() const {
      return StartBB;
    }
  };

  /// isInConditionalBranch - Return true if we're currently emitting
  /// one branch or the other of a conditional expression.
  bool isInConditionalBranch() const { return OutermostConditional != nullptr; }

  void setBeforeOutermostConditional(llvm::Value *value, Address addr,
                                     CodeGenFunction &CGF) {
    assert(isInConditionalBranch());
    llvm::BasicBlock *block = OutermostConditional->getStartingBlock();
    auto store =
        new llvm::StoreInst(value, addr.emitRawPointer(CGF), &block->back());
    store->setAlignment(addr.getAlignment().getAsAlign());
  }

  /// An RAII object to record that we're evaluating a statement
  /// expression.
  class StmtExprEvaluation {
    CodeGenFunction &CGF;

    /// We have to save the outermost conditional: cleanups in a
    /// statement expression aren't conditional just because the
    /// StmtExpr is.
    ConditionalEvaluation *SavedOutermostConditional;

  public:
    StmtExprEvaluation(CodeGenFunction &CGF)
      : CGF(CGF), SavedOutermostConditional(CGF.OutermostConditional) {
      CGF.OutermostConditional = nullptr;
    }

    ~StmtExprEvaluation() {
      CGF.OutermostConditional = SavedOutermostConditional;
      CGF.EnsureInsertPoint();
    }
  };

  /// An object which temporarily prevents a value from being
  /// destroyed by aggressive peephole optimizations that assume that
  /// all uses of a value have been realized in the IR.
  class PeepholeProtection {
    llvm::Instruction *Inst = nullptr;
    friend class CodeGenFunction;

  public:
    PeepholeProtection() = default;
  };

  /// A non-RAII class containing all the information about a bound
  /// opaque value.  OpaqueValueMapping, below, is a RAII wrapper for
  /// this which makes individual mappings very simple; using this
  /// class directly is useful when you have a variable number of
  /// opaque values or don't want the RAII functionality for some
  /// reason.
  class OpaqueValueMappingData {
    const OpaqueValueExpr *OpaqueValue;
    bool BoundLValue;
    CodeGenFunction::PeepholeProtection Protection;

    OpaqueValueMappingData(const OpaqueValueExpr *ov,
                           bool boundLValue)
      : OpaqueValue(ov), BoundLValue(boundLValue) {}
  public:
    OpaqueValueMappingData() : OpaqueValue(nullptr) {}

    static bool shouldBindAsLValue(const Expr *expr) {
      // gl-values should be bound as l-values for obvious reasons.
      // Records should be bound as l-values because IR generation
      // always keeps them in memory.  Expressions of function type
      // act exactly like l-values but are formally required to be
      // r-values in C.
      return expr->isGLValue() ||
             expr->getType()->isFunctionType() ||
             hasAggregateEvaluationKind(expr->getType());
    }

    static OpaqueValueMappingData bind(CodeGenFunction &CGF,
                                       const OpaqueValueExpr *ov,
                                       const Expr *e) {
      if (shouldBindAsLValue(ov))
        return bind(CGF, ov, CGF.EmitLValue(e));
      return bind(CGF, ov, CGF.EmitAnyExpr(e));
    }

    static OpaqueValueMappingData bind(CodeGenFunction &CGF,
                                       const OpaqueValueExpr *ov,
                                       const LValue &lv) {
      assert(shouldBindAsLValue(ov));
      CGF.OpaqueLValues.insert(std::make_pair(ov, lv));
      return OpaqueValueMappingData(ov, true);
    }

    static OpaqueValueMappingData bind(CodeGenFunction &CGF,
                                       const OpaqueValueExpr *ov,
                                       const RValue &rv) {
      assert(!shouldBindAsLValue(ov));
      CGF.OpaqueRValues.insert(std::make_pair(ov, rv));

      OpaqueValueMappingData data(ov, false);

      // Work around an extremely aggressive peephole optimization in
      // EmitScalarConversion which assumes that all other uses of a
      // value are extant.
      data.Protection = CGF.protectFromPeepholes(rv);

      return data;
    }

    bool isValid() const { return OpaqueValue != nullptr; }
    void clear() { OpaqueValue = nullptr; }

    void unbind(CodeGenFunction &CGF) {
      assert(OpaqueValue && "no data to unbind!");

      if (BoundLValue) {
        CGF.OpaqueLValues.erase(OpaqueValue);
      } else {
        CGF.OpaqueRValues.erase(OpaqueValue);
        CGF.unprotectFromPeepholes(Protection);
      }
    }
  };

  /// An RAII object to set (and then clear) a mapping for an OpaqueValueExpr.
  class OpaqueValueMapping {
    CodeGenFunction &CGF;
    OpaqueValueMappingData Data;

  public:
    static bool shouldBindAsLValue(const Expr *expr) {
      return OpaqueValueMappingData::shouldBindAsLValue(expr);
    }

    /// Build the opaque value mapping for the given conditional
    /// operator if it's the GNU ?: extension.  This is a common
    /// enough pattern that the convenience operator is really
    /// helpful.
    ///
    OpaqueValueMapping(CodeGenFunction &CGF,
                       const AbstractConditionalOperator *op) : CGF(CGF) {
      if (isa<ConditionalOperator>(op))
        // Leave Data empty.
        return;

      const BinaryConditionalOperator *e = cast<BinaryConditionalOperator>(op);
      Data = OpaqueValueMappingData::bind(CGF, e->getOpaqueValue(),
                                          e->getCommon());
    }

    /// Build the opaque value mapping for an OpaqueValueExpr whose source
    /// expression is set to the expression the OVE represents.
    OpaqueValueMapping(CodeGenFunction &CGF, const OpaqueValueExpr *OV)
        : CGF(CGF) {
      if (OV) {
        assert(OV->getSourceExpr() && "wrong form of OpaqueValueMapping used "
                                      "for OVE with no source expression");
        Data = OpaqueValueMappingData::bind(CGF, OV, OV->getSourceExpr());
      }
    }

    OpaqueValueMapping(CodeGenFunction &CGF,
                       const OpaqueValueExpr *opaqueValue,
                       LValue lvalue)
      : CGF(CGF), Data(OpaqueValueMappingData::bind(CGF, opaqueValue, lvalue)) {
    }

    OpaqueValueMapping(CodeGenFunction &CGF,
                       const OpaqueValueExpr *opaqueValue,
                       RValue rvalue)
      : CGF(CGF), Data(OpaqueValueMappingData::bind(CGF, opaqueValue, rvalue)) {
    }

    void pop() {
      Data.unbind(CGF);
      Data.clear();
    }

    ~OpaqueValueMapping() {
      if (Data.isValid()) Data.unbind(CGF);
    }
  };

private:
  CGDebugInfo *DebugInfo;
  /// Used to create unique names for artificial VLA size debug info variables.
  unsigned VLAExprCounter = 0;
  bool DisableDebugInfo = false;

  /// DidCallStackSave - Whether llvm.stacksave has been called. Used to avoid
  /// calling llvm.stacksave for multiple VLAs in the same scope.
  bool DidCallStackSave = false;

  /// IndirectBranch - The first time an indirect goto is seen we create a block
  /// with an indirect branch.  Every time we see the address of a label taken,
  /// we add the label to the indirect goto.  Every subsequent indirect goto is
  /// codegen'd as a jump to the IndirectBranch's basic block.
  llvm::IndirectBrInst *IndirectBranch = nullptr;

  /// LocalDeclMap - This keeps track of the LLVM allocas or globals for local C
  /// decls.
  DeclMapTy LocalDeclMap;

  // Keep track of the cleanups for callee-destructed parameters pushed to the
  // cleanup stack so that they can be deactivated later.
  llvm::DenseMap<const ParmVarDecl *, EHScopeStack::stable_iterator>
      CalleeDestructedParamCleanups;

  /// SizeArguments - If a ParmVarDecl had the pass_object_size attribute, this
  /// will contain a mapping from said ParmVarDecl to its implicit "object_size"
  /// parameter.
  llvm::SmallDenseMap<const ParmVarDecl *, const ImplicitParamDecl *, 2>
      SizeArguments;

  /// Track escaped local variables with auto storage. Used during SEH
  /// outlining to produce a call to llvm.localescape.
  llvm::DenseMap<llvm::AllocaInst *, int> EscapedLocals;

  /// LabelMap - This keeps track of the LLVM basic block for each C label.
  llvm::DenseMap<const LabelDecl*, JumpDest> LabelMap;

  // BreakContinueStack - This keeps track of where break and continue
  // statements should jump to.
  struct BreakContinue {
    BreakContinue(JumpDest Break, JumpDest Continue)
      : BreakBlock(Break), ContinueBlock(Continue) {}

    JumpDest BreakBlock;
    JumpDest ContinueBlock;
  };
  SmallVector<BreakContinue, 8> BreakContinueStack;

  /// Handles cancellation exit points in OpenMP-related constructs.
  class OpenMPCancelExitStack {
    /// Tracks cancellation exit point and join point for cancel-related exit
    /// and normal exit.
    struct CancelExit {
      CancelExit() = default;
      CancelExit(OpenMPDirectiveKind Kind, JumpDest ExitBlock,
                 JumpDest ContBlock)
          : Kind(Kind), ExitBlock(ExitBlock), ContBlock(ContBlock) {}
      OpenMPDirectiveKind Kind = llvm::omp::OMPD_unknown;
      /// true if the exit block has been emitted already by the special
      /// emitExit() call, false if the default codegen is used.
      bool HasBeenEmitted = false;
      JumpDest ExitBlock;
      JumpDest ContBlock;
    };

    SmallVector<CancelExit, 8> Stack;

  public:
    OpenMPCancelExitStack() : Stack(1) {}
    ~OpenMPCancelExitStack() = default;
    /// Fetches the exit block for the current OpenMP construct.
    JumpDest getExitBlock() const { return Stack.back().ExitBlock; }
    /// Emits exit block with special codegen procedure specific for the related
    /// OpenMP construct + emits code for normal construct cleanup.
    void emitExit(CodeGenFunction &CGF, OpenMPDirectiveKind Kind,
                  const llvm::function_ref<void(CodeGenFunction &)> CodeGen) {
      if (Stack.back().Kind == Kind && getExitBlock().isValid()) {
        assert(CGF.getOMPCancelDestination(Kind).isValid());
        assert(CGF.HaveInsertPoint());
        assert(!Stack.back().HasBeenEmitted);
        auto IP = CGF.Builder.saveAndClearIP();
        CGF.EmitBlock(Stack.back().ExitBlock.getBlock());
        CodeGen(CGF);
        CGF.EmitBranch(Stack.back().ContBlock.getBlock());
        CGF.Builder.restoreIP(IP);
        Stack.back().HasBeenEmitted = true;
      }
      CodeGen(CGF);
    }
    /// Enter the cancel supporting \a Kind construct.
    /// \param Kind OpenMP directive that supports cancel constructs.
    /// \param HasCancel true, if the construct has inner cancel directive,
    /// false otherwise.
    void enter(CodeGenFunction &CGF, OpenMPDirectiveKind Kind, bool HasCancel) {
      Stack.push_back({Kind,
                       HasCancel ? CGF.getJumpDestInCurrentScope("cancel.exit")
                                 : JumpDest(),
                       HasCancel ? CGF.getJumpDestInCurrentScope("cancel.cont")
                                 : JumpDest()});
    }
    /// Emits default exit point for the cancel construct (if the special one
    /// has not be used) + join point for cancel/normal exits.
    void exit(CodeGenFunction &CGF) {
      if (getExitBlock().isValid()) {
        assert(CGF.getOMPCancelDestination(Stack.back().Kind).isValid());
        bool HaveIP = CGF.HaveInsertPoint();
        if (!Stack.back().HasBeenEmitted) {
          if (HaveIP)
            CGF.EmitBranchThroughCleanup(Stack.back().ContBlock);
          CGF.EmitBlock(Stack.back().ExitBlock.getBlock());
          CGF.EmitBranchThroughCleanup(Stack.back().ContBlock);
        }
        CGF.EmitBlock(Stack.back().ContBlock.getBlock());
        if (!HaveIP) {
          CGF.Builder.CreateUnreachable();
          CGF.Builder.ClearInsertionPoint();
        }
      }
      Stack.pop_back();
    }
  };
  OpenMPCancelExitStack OMPCancelStack;

  /// Lower the Likelihood knowledge about the \p Cond via llvm.expect intrin.
  llvm::Value *emitCondLikelihoodViaExpectIntrinsic(llvm::Value *Cond,
                                                    Stmt::Likelihood LH);

  CodeGenPGO PGO;

  /// Bitmap used by MC/DC to track condition outcomes of a boolean expression.
  Address MCDCCondBitmapAddr = Address::invalid();

  /// Calculate branch weights appropriate for PGO data
  llvm::MDNode *createProfileWeights(uint64_t TrueCount,
                                     uint64_t FalseCount) const;
  llvm::MDNode *createProfileWeights(ArrayRef<uint64_t> Weights) const;
  llvm::MDNode *createProfileWeightsForLoop(const Stmt *Cond,
                                            uint64_t LoopCount) const;

public:
  /// Increment the profiler's counter for the given statement by \p StepV.
  /// If \p StepV is null, the default increment is 1.
  void incrementProfileCounter(const Stmt *S, llvm::Value *StepV = nullptr) {
    if (CGM.getCodeGenOpts().hasProfileClangInstr() &&
        !CurFn->hasFnAttribute(llvm::Attribute::NoProfile) &&
        !CurFn->hasFnAttribute(llvm::Attribute::SkipProfile)) {
      auto AL = ApplyDebugLocation::CreateArtificial(*this);
      PGO.emitCounterSetOrIncrement(Builder, S, StepV);
    }
    PGO.setCurrentStmt(S);
  }

  bool isMCDCCoverageEnabled() const {
    return (CGM.getCodeGenOpts().hasProfileClangInstr() &&
            CGM.getCodeGenOpts().MCDCCoverage &&
            !CurFn->hasFnAttribute(llvm::Attribute::NoProfile));
  }

  /// Allocate a temp value on the stack that MCDC can use to track condition
  /// results.
  void maybeCreateMCDCCondBitmap() {
    if (isMCDCCoverageEnabled()) {
      PGO.emitMCDCParameters(Builder);
      MCDCCondBitmapAddr =
          CreateIRTemp(getContext().UnsignedIntTy, "mcdc.addr");
    }
  }

  bool isBinaryLogicalOp(const Expr *E) const {
    const BinaryOperator *BOp = dyn_cast<BinaryOperator>(E->IgnoreParens());
    return (BOp && BOp->isLogicalOp());
  }

  /// Zero-init the MCDC temp value.
  void maybeResetMCDCCondBitmap(const Expr *E) {
    if (isMCDCCoverageEnabled() && isBinaryLogicalOp(E)) {
      PGO.emitMCDCCondBitmapReset(Builder, E, MCDCCondBitmapAddr);
      PGO.setCurrentStmt(E);
    }
  }

  /// Increment the profiler's counter for the given expression by \p StepV.
  /// If \p StepV is null, the default increment is 1.
  void maybeUpdateMCDCTestVectorBitmap(const Expr *E) {
    if (isMCDCCoverageEnabled() && isBinaryLogicalOp(E)) {
      PGO.emitMCDCTestVectorBitmapUpdate(Builder, E, MCDCCondBitmapAddr, *this);
      PGO.setCurrentStmt(E);
    }
  }

  /// Update the MCDC temp value with the condition's evaluated result.
  void maybeUpdateMCDCCondBitmap(const Expr *E, llvm::Value *Val) {
    if (isMCDCCoverageEnabled()) {
      PGO.emitMCDCCondBitmapUpdate(Builder, E, MCDCCondBitmapAddr, Val, *this);
      PGO.setCurrentStmt(E);
    }
  }

  /// Get the profiler's count for the given statement.
  uint64_t getProfileCount(const Stmt *S) {
    return PGO.getStmtCount(S).value_or(0);
  }

  /// Set the profiler's current count.
  void setCurrentProfileCount(uint64_t Count) {
    PGO.setCurrentRegionCount(Count);
  }

  /// Get the profiler's current count. This is generally the count for the most
  /// recently incremented counter.
  uint64_t getCurrentProfileCount() {
    return PGO.getCurrentRegionCount();
  }

private:

  /// SwitchInsn - This is nearest current switch instruction. It is null if
  /// current context is not in a switch.
  llvm::SwitchInst *SwitchInsn = nullptr;
  /// The branch weights of SwitchInsn when doing instrumentation based PGO.
  SmallVector<uint64_t, 16> *SwitchWeights = nullptr;

  /// The likelihood attributes of the SwitchCase.
  SmallVector<Stmt::Likelihood, 16> *SwitchLikelihood = nullptr;

  /// CaseRangeBlock - This block holds if condition check for last case
  /// statement range in current switch instruction.
  llvm::BasicBlock *CaseRangeBlock = nullptr;

  /// OpaqueLValues - Keeps track of the current set of opaque value
  /// expressions.
  llvm::DenseMap<const OpaqueValueExpr *, LValue> OpaqueLValues;
  llvm::DenseMap<const OpaqueValueExpr *, RValue> OpaqueRValues;

  // VLASizeMap - This keeps track of the associated size for each VLA type.
  // We track this by the size expression rather than the type itself because
  // in certain situations, like a const qualifier applied to an VLA typedef,
  // multiple VLA types can share the same size expression.
  // FIXME: Maybe this could be a stack of maps that is pushed/popped as we
  // enter/leave scopes.
  llvm::DenseMap<const Expr*, llvm::Value*> VLASizeMap;

  /// A block containing a single 'unreachable' instruction.  Created
  /// lazily by getUnreachableBlock().
  llvm::BasicBlock *UnreachableBlock = nullptr;

  /// Counts of the number return expressions in the function.
  unsigned NumReturnExprs = 0;

  /// Count the number of simple (constant) return expressions in the function.
  unsigned NumSimpleReturnExprs = 0;

  /// The last regular (non-return) debug location (breakpoint) in the function.
  SourceLocation LastStopPoint;

public:
  /// Source location information about the default argument or member
  /// initializer expression we're evaluating, if any.
  CurrentSourceLocExprScope CurSourceLocExprScope;
  using SourceLocExprScopeGuard =
      CurrentSourceLocExprScope::SourceLocExprScopeGuard;

  /// A scope within which we are constructing the fields of an object which
  /// might use a CXXDefaultInitExpr. This stashes away a 'this' value to use
  /// if we need to evaluate a CXXDefaultInitExpr within the evaluation.
  class FieldConstructionScope {
  public:
    FieldConstructionScope(CodeGenFunction &CGF, Address This)
        : CGF(CGF), OldCXXDefaultInitExprThis(CGF.CXXDefaultInitExprThis) {
      CGF.CXXDefaultInitExprThis = This;
    }
    ~FieldConstructionScope() {
      CGF.CXXDefaultInitExprThis = OldCXXDefaultInitExprThis;
    }

  private:
    CodeGenFunction &CGF;
    Address OldCXXDefaultInitExprThis;
  };

  /// The scope of a CXXDefaultInitExpr. Within this scope, the value of 'this'
  /// is overridden to be the object under construction.
  class CXXDefaultInitExprScope  {
  public:
    CXXDefaultInitExprScope(CodeGenFunction &CGF, const CXXDefaultInitExpr *E)
        : CGF(CGF), OldCXXThisValue(CGF.CXXThisValue),
          OldCXXThisAlignment(CGF.CXXThisAlignment),
          SourceLocScope(E, CGF.CurSourceLocExprScope) {
      CGF.CXXThisValue = CGF.CXXDefaultInitExprThis.getBasePointer();
      CGF.CXXThisAlignment = CGF.CXXDefaultInitExprThis.getAlignment();
    }
    ~CXXDefaultInitExprScope() {
      CGF.CXXThisValue = OldCXXThisValue;
      CGF.CXXThisAlignment = OldCXXThisAlignment;
    }

  public:
    CodeGenFunction &CGF;
    llvm::Value *OldCXXThisValue;
    CharUnits OldCXXThisAlignment;
    SourceLocExprScopeGuard SourceLocScope;
  };

  struct CXXDefaultArgExprScope : SourceLocExprScopeGuard {
    CXXDefaultArgExprScope(CodeGenFunction &CGF, const CXXDefaultArgExpr *E)
        : SourceLocExprScopeGuard(E, CGF.CurSourceLocExprScope) {}
  };

  /// The scope of an ArrayInitLoopExpr. Within this scope, the value of the
  /// current loop index is overridden.
  class ArrayInitLoopExprScope {
  public:
    ArrayInitLoopExprScope(CodeGenFunction &CGF, llvm::Value *Index)
      : CGF(CGF), OldArrayInitIndex(CGF.ArrayInitIndex) {
      CGF.ArrayInitIndex = Index;
    }
    ~ArrayInitLoopExprScope() {
      CGF.ArrayInitIndex = OldArrayInitIndex;
    }

  private:
    CodeGenFunction &CGF;
    llvm::Value *OldArrayInitIndex;
  };

  class InlinedInheritingConstructorScope {
  public:
    InlinedInheritingConstructorScope(CodeGenFunction &CGF, GlobalDecl GD)
        : CGF(CGF), OldCurGD(CGF.CurGD), OldCurFuncDecl(CGF.CurFuncDecl),
          OldCurCodeDecl(CGF.CurCodeDecl),
          OldCXXABIThisDecl(CGF.CXXABIThisDecl),
          OldCXXABIThisValue(CGF.CXXABIThisValue),
          OldCXXThisValue(CGF.CXXThisValue),
          OldCXXABIThisAlignment(CGF.CXXABIThisAlignment),
          OldCXXThisAlignment(CGF.CXXThisAlignment),
          OldReturnValue(CGF.ReturnValue), OldFnRetTy(CGF.FnRetTy),
          OldCXXInheritedCtorInitExprArgs(
              std::move(CGF.CXXInheritedCtorInitExprArgs)) {
      CGF.CurGD = GD;
      CGF.CurFuncDecl = CGF.CurCodeDecl =
          cast<CXXConstructorDecl>(GD.getDecl());
      CGF.CXXABIThisDecl = nullptr;
      CGF.CXXABIThisValue = nullptr;
      CGF.CXXThisValue = nullptr;
      CGF.CXXABIThisAlignment = CharUnits();
      CGF.CXXThisAlignment = CharUnits();
      CGF.ReturnValue = Address::invalid();
      CGF.FnRetTy = QualType();
      CGF.CXXInheritedCtorInitExprArgs.clear();
    }
    ~InlinedInheritingConstructorScope() {
      CGF.CurGD = OldCurGD;
      CGF.CurFuncDecl = OldCurFuncDecl;
      CGF.CurCodeDecl = OldCurCodeDecl;
      CGF.CXXABIThisDecl = OldCXXABIThisDecl;
      CGF.CXXABIThisValue = OldCXXABIThisValue;
      CGF.CXXThisValue = OldCXXThisValue;
      CGF.CXXABIThisAlignment = OldCXXABIThisAlignment;
      CGF.CXXThisAlignment = OldCXXThisAlignment;
      CGF.ReturnValue = OldReturnValue;
      CGF.FnRetTy = OldFnRetTy;
      CGF.CXXInheritedCtorInitExprArgs =
          std::move(OldCXXInheritedCtorInitExprArgs);
    }

  private:
    CodeGenFunction &CGF;
    GlobalDecl OldCurGD;
    const Decl *OldCurFuncDecl;
    const Decl *OldCurCodeDecl;
    ImplicitParamDecl *OldCXXABIThisDecl;
    llvm::Value *OldCXXABIThisValue;
    llvm::Value *OldCXXThisValue;
    CharUnits OldCXXABIThisAlignment;
    CharUnits OldCXXThisAlignment;
    Address OldReturnValue;
    QualType OldFnRetTy;
    CallArgList OldCXXInheritedCtorInitExprArgs;
  };

  // Helper class for the OpenMP IR Builder. Allows reusability of code used for
  // region body, and finalization codegen callbacks. This will class will also
  // contain privatization functions used by the privatization call backs
  //
  // TODO: this is temporary class for things that are being moved out of
  // CGOpenMPRuntime, new versions of current CodeGenFunction methods, or
  // utility function for use with the OMPBuilder. Once that move to use the
  // OMPBuilder is done, everything here will either become part of CodeGenFunc.
  // directly, or a new helper class that will contain functions used by both
  // this and the OMPBuilder

  struct OMPBuilderCBHelpers {

    OMPBuilderCBHelpers() = delete;
    OMPBuilderCBHelpers(const OMPBuilderCBHelpers &) = delete;
    OMPBuilderCBHelpers &operator=(const OMPBuilderCBHelpers &) = delete;

    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

    /// Cleanup action for allocate support.
    class OMPAllocateCleanupTy final : public EHScopeStack::Cleanup {

    private:
      llvm::CallInst *RTLFnCI;

    public:
      OMPAllocateCleanupTy(llvm::CallInst *RLFnCI) : RTLFnCI(RLFnCI) {
        RLFnCI->removeFromParent();
      }

      void Emit(CodeGenFunction &CGF, Flags /*flags*/) override {
        if (!CGF.HaveInsertPoint())
          return;
        CGF.Builder.Insert(RTLFnCI);
      }
    };

    /// Returns address of the threadprivate variable for the current
    /// thread. This Also create any necessary OMP runtime calls.
    ///
    /// \param VD VarDecl for Threadprivate variable.
    /// \param VDAddr Address of the Vardecl
    /// \param Loc  The location where the barrier directive was encountered
    static Address getAddrOfThreadPrivate(CodeGenFunction &CGF,
                                          const VarDecl *VD, Address VDAddr,
                                          SourceLocation Loc);

    /// Gets the OpenMP-specific address of the local variable /p VD.
    static Address getAddressOfLocalVariable(CodeGenFunction &CGF,
                                             const VarDecl *VD);
    /// Get the platform-specific name separator.
    /// \param Parts different parts of the final name that needs separation
    /// \param FirstSeparator First separator used between the initial two
    ///        parts of the name.
    /// \param Separator separator used between all of the rest consecutinve
    ///        parts of the name
    static std::string getNameWithSeparators(ArrayRef<StringRef> Parts,
                                             StringRef FirstSeparator = ".",
                                             StringRef Separator = ".");
    /// Emit the Finalization for an OMP region
    /// \param CGF	The Codegen function this belongs to
    /// \param IP	Insertion point for generating the finalization code.
    static void FinalizeOMPRegion(CodeGenFunction &CGF, InsertPointTy IP) {
      CGBuilderTy::InsertPointGuard IPG(CGF.Builder);
      assert(IP.getBlock()->end() != IP.getPoint() &&
             "OpenMP IR Builder should cause terminated block!");

      llvm::BasicBlock *IPBB = IP.getBlock();
      llvm::BasicBlock *DestBB = IPBB->getUniqueSuccessor();
      assert(DestBB && "Finalization block should have one successor!");

      // erase and replace with cleanup branch.
      IPBB->getTerminator()->eraseFromParent();
      CGF.Builder.SetInsertPoint(IPBB);
      CodeGenFunction::JumpDest Dest = CGF.getJumpDestInCurrentScope(DestBB);
      CGF.EmitBranchThroughCleanup(Dest);
    }

    /// Emit the body of an OMP region
    /// \param CGF	          The Codegen function this belongs to
    /// \param RegionBodyStmt The body statement for the OpenMP region being
    ///                       generated
    /// \param AllocaIP       Where to insert alloca instructions
    /// \param CodeGenIP      Where to insert the region code
    /// \param RegionName     Name to be used for new blocks
    static void EmitOMPInlinedRegionBody(CodeGenFunction &CGF,
                                         const Stmt *RegionBodyStmt,
                                         InsertPointTy AllocaIP,
                                         InsertPointTy CodeGenIP,
                                         Twine RegionName);

    static void EmitCaptureStmt(CodeGenFunction &CGF, InsertPointTy CodeGenIP,
                                llvm::BasicBlock &FiniBB, llvm::Function *Fn,
                                ArrayRef<llvm::Value *> Args) {
      llvm::BasicBlock *CodeGenIPBB = CodeGenIP.getBlock();
      if (llvm::Instruction *CodeGenIPBBTI = CodeGenIPBB->getTerminator())
        CodeGenIPBBTI->eraseFromParent();

      CGF.Builder.SetInsertPoint(CodeGenIPBB);

      if (Fn->doesNotThrow())
        CGF.EmitNounwindRuntimeCall(Fn, Args);
      else
        CGF.EmitRuntimeCall(Fn, Args);

      if (CGF.Builder.saveIP().isSet())
        CGF.Builder.CreateBr(&FiniBB);
    }

    /// Emit the body of an OMP region that will be outlined in
    /// OpenMPIRBuilder::finalize().
    /// \param CGF	          The Codegen function this belongs to
    /// \param RegionBodyStmt The body statement for the OpenMP region being
    ///                       generated
    /// \param AllocaIP       Where to insert alloca instructions
    /// \param CodeGenIP      Where to insert the region code
    /// \param RegionName     Name to be used for new blocks
    static void EmitOMPOutlinedRegionBody(CodeGenFunction &CGF,
                                          const Stmt *RegionBodyStmt,
                                          InsertPointTy AllocaIP,
                                          InsertPointTy CodeGenIP,
                                          Twine RegionName);

    /// RAII for preserving necessary info during Outlined region body codegen.
    class OutlinedRegionBodyRAII {

      llvm::AssertingVH<llvm::Instruction> OldAllocaIP;
      CodeGenFunction::JumpDest OldReturnBlock;
      CodeGenFunction &CGF;

    public:
      OutlinedRegionBodyRAII(CodeGenFunction &cgf, InsertPointTy &AllocaIP,
                             llvm::BasicBlock &RetBB)
          : CGF(cgf) {
        assert(AllocaIP.isSet() &&
               "Must specify Insertion point for allocas of outlined function");
        OldAllocaIP = CGF.AllocaInsertPt;
        CGF.AllocaInsertPt = &*AllocaIP.getPoint();

        OldReturnBlock = CGF.ReturnBlock;
        CGF.ReturnBlock = CGF.getJumpDestInCurrentScope(&RetBB);
      }

      ~OutlinedRegionBodyRAII() {
        CGF.AllocaInsertPt = OldAllocaIP;
        CGF.ReturnBlock = OldReturnBlock;
      }
    };

    /// RAII for preserving necessary info during inlined region body codegen.
    class InlinedRegionBodyRAII {

      llvm::AssertingVH<llvm::Instruction> OldAllocaIP;
      CodeGenFunction &CGF;

    public:
      InlinedRegionBodyRAII(CodeGenFunction &cgf, InsertPointTy &AllocaIP,
                            llvm::BasicBlock &FiniBB)
          : CGF(cgf) {
        // Alloca insertion block should be in the entry block of the containing
        // function so it expects an empty AllocaIP in which case will reuse the
        // old alloca insertion point, or a new AllocaIP in the same block as
        // the old one
        assert((!AllocaIP.isSet() ||
                CGF.AllocaInsertPt->getParent() == AllocaIP.getBlock()) &&
               "Insertion point should be in the entry block of containing "
               "function!");
        OldAllocaIP = CGF.AllocaInsertPt;
        if (AllocaIP.isSet())
          CGF.AllocaInsertPt = &*AllocaIP.getPoint();

        // TODO: Remove the call, after making sure the counter is not used by
        //       the EHStack.
        // Since this is an inlined region, it should not modify the
        // ReturnBlock, and should reuse the one for the enclosing outlined
        // region. So, the JumpDest being return by the function is discarded
        (void)CGF.getJumpDestInCurrentScope(&FiniBB);
      }

      ~InlinedRegionBodyRAII() { CGF.AllocaInsertPt = OldAllocaIP; }
    };
  };

private:
  /// CXXThisDecl - When generating code for a C++ member function,
  /// this will hold the implicit 'this' declaration.
  ImplicitParamDecl *CXXABIThisDecl = nullptr;
  llvm::Value *CXXABIThisValue = nullptr;
  llvm::Value *CXXThisValue = nullptr;
  CharUnits CXXABIThisAlignment;
  CharUnits CXXThisAlignment;

  /// The value of 'this' to use when evaluating CXXDefaultInitExprs within
  /// this expression.
  Address CXXDefaultInitExprThis = Address::invalid();

  /// The current array initialization index when evaluating an
  /// ArrayInitIndexExpr within an ArrayInitLoopExpr.
  llvm::Value *ArrayInitIndex = nullptr;

  /// The values of function arguments to use when evaluating
  /// CXXInheritedCtorInitExprs within this context.
  CallArgList CXXInheritedCtorInitExprArgs;

  /// CXXStructorImplicitParamDecl - When generating code for a constructor or
  /// destructor, this will hold the implicit argument (e.g. VTT).
  ImplicitParamDecl *CXXStructorImplicitParamDecl = nullptr;
  llvm::Value *CXXStructorImplicitParamValue = nullptr;

  /// OutermostConditional - Points to the outermost active
  /// conditional control.  This is used so that we know if a
  /// temporary should be destroyed conditionally.
  ConditionalEvaluation *OutermostConditional = nullptr;

  /// The current lexical scope.
  LexicalScope *CurLexicalScope = nullptr;

  /// The current source location that should be used for exception
  /// handling code.
  SourceLocation CurEHLocation;

  /// BlockByrefInfos - For each __block variable, contains
  /// information about the layout of the variable.
  llvm::DenseMap<const ValueDecl *, BlockByrefInfo> BlockByrefInfos;

  /// Used by -fsanitize=nullability-return to determine whether the return
  /// value can be checked.
  llvm::Value *RetValNullabilityPrecondition = nullptr;

  /// Check if -fsanitize=nullability-return instrumentation is required for
  /// this function.
  bool requiresReturnValueNullabilityCheck() const {
    return RetValNullabilityPrecondition;
  }

  /// Used to store precise source locations for return statements by the
  /// runtime return value checks.
  Address ReturnLocation = Address::invalid();

  /// Check if the return value of this function requires sanitization.
  bool requiresReturnValueCheck() const;

  bool isInAllocaArgument(CGCXXABI &ABI, QualType Ty);
  bool hasInAllocaArg(const CXXMethodDecl *MD);

  llvm::BasicBlock *TerminateLandingPad = nullptr;
  llvm::BasicBlock *TerminateHandler = nullptr;
  llvm::SmallVector<llvm::BasicBlock *, 2> TrapBBs;

  /// Terminate funclets keyed by parent funclet pad.
  llvm::MapVector<llvm::Value *, llvm::BasicBlock *> TerminateFunclets;

  /// Largest vector width used in ths function. Will be used to create a
  /// function attribute.
  unsigned LargestVectorWidth = 0;

  /// True if we need emit the life-time markers. This is initially set in
  /// the constructor, but could be overwritten to true if this is a coroutine.
  bool ShouldEmitLifetimeMarkers;

  /// Add OpenCL kernel arg metadata and the kernel attribute metadata to
  /// the function metadata.
  void EmitKernelMetadata(const FunctionDecl *FD, llvm::Function *Fn);

public:
  CodeGenFunction(CodeGenModule &cgm, bool suppressNewContext=false);
  ~CodeGenFunction();

  CodeGenTypes &getTypes() const { return CGM.getTypes(); }
  ASTContext &getContext() const { return CGM.getContext(); }
  CGDebugInfo *getDebugInfo() {
    if (DisableDebugInfo)
      return nullptr;
    return DebugInfo;
  }
  void disableDebugInfo() { DisableDebugInfo = true; }
  void enableDebugInfo() { DisableDebugInfo = false; }

  bool shouldUseFusedARCCalls() {
    return CGM.getCodeGenOpts().OptimizationLevel == 0;
  }

  const LangOptions &getLangOpts() const { return CGM.getLangOpts(); }

  /// Returns a pointer to the function's exception object and selector slot,
  /// which is assigned in every landing pad.
  Address getExceptionSlot();
  Address getEHSelectorSlot();

  /// Returns the contents of the function's exception object and selector
  /// slots.
  llvm::Value *getExceptionFromSlot();
  llvm::Value *getSelectorFromSlot();

  RawAddress getNormalCleanupDestSlot();

  llvm::BasicBlock *getUnreachableBlock() {
    if (!UnreachableBlock) {
      UnreachableBlock = createBasicBlock("unreachable");
      new llvm::UnreachableInst(getLLVMContext(), UnreachableBlock);
    }
    return UnreachableBlock;
  }

  llvm::BasicBlock *getInvokeDest() {
    if (!EHStack.requiresLandingPad()) return nullptr;
    return getInvokeDestImpl();
  }

  bool currentFunctionUsesSEHTry() const { return !!CurSEHParent; }

  const TargetInfo &getTarget() const { return Target; }
  llvm::LLVMContext &getLLVMContext() { return CGM.getLLVMContext(); }
  const TargetCodeGenInfo &getTargetHooks() const {
    return CGM.getTargetCodeGenInfo();
  }

  //===--------------------------------------------------------------------===//
  //                                  Cleanups
  //===--------------------------------------------------------------------===//

  typedef void Destroyer(CodeGenFunction &CGF, Address addr, QualType ty);

  void pushIrregularPartialArrayCleanup(llvm::Value *arrayBegin,
                                        Address arrayEndPointer,
                                        QualType elementType,
                                        CharUnits elementAlignment,
                                        Destroyer *destroyer);
  void pushRegularPartialArrayCleanup(llvm::Value *arrayBegin,
                                      llvm::Value *arrayEnd,
                                      QualType elementType,
                                      CharUnits elementAlignment,
                                      Destroyer *destroyer);

  void pushDestroy(QualType::DestructionKind dtorKind,
                   Address addr, QualType type);
  void pushEHDestroy(QualType::DestructionKind dtorKind,
                     Address addr, QualType type);
  void pushDestroy(CleanupKind kind, Address addr, QualType type,
                   Destroyer *destroyer, bool useEHCleanupForArray);
  void pushDestroyAndDeferDeactivation(QualType::DestructionKind dtorKind,
                                       Address addr, QualType type);
  void pushDestroyAndDeferDeactivation(CleanupKind cleanupKind, Address addr,
                                       QualType type, Destroyer *destroyer,
                                       bool useEHCleanupForArray);
  void pushLifetimeExtendedDestroy(CleanupKind kind, Address addr,
                                   QualType type, Destroyer *destroyer,
                                   bool useEHCleanupForArray);
  void pushCallObjectDeleteCleanup(const FunctionDecl *OperatorDelete,
                                   llvm::Value *CompletePtr,
                                   QualType ElementType);
  void pushStackRestore(CleanupKind kind, Address SPMem);
  void pushKmpcAllocFree(CleanupKind Kind,
                         std::pair<llvm::Value *, llvm::Value *> AddrSizePair);
  void emitDestroy(Address addr, QualType type, Destroyer *destroyer,
                   bool useEHCleanupForArray);
  llvm::Function *generateDestroyHelper(Address addr, QualType type,
                                        Destroyer *destroyer,
                                        bool useEHCleanupForArray,
                                        const VarDecl *VD);
  void emitArrayDestroy(llvm::Value *begin, llvm::Value *end,
                        QualType elementType, CharUnits elementAlign,
                        Destroyer *destroyer,
                        bool checkZeroLength, bool useEHCleanup);

  Destroyer *getDestroyer(QualType::DestructionKind destructionKind);

  /// Determines whether an EH cleanup is required to destroy a type
  /// with the given destruction kind.
  bool needsEHCleanup(QualType::DestructionKind kind) {
    switch (kind) {
    case QualType::DK_none:
      return false;
    case QualType::DK_cxx_destructor:
    case QualType::DK_objc_weak_lifetime:
    case QualType::DK_nontrivial_c_struct:
      return getLangOpts().Exceptions;
    case QualType::DK_objc_strong_lifetime:
      return getLangOpts().Exceptions &&
             CGM.getCodeGenOpts().ObjCAutoRefCountExceptions;
    }
    llvm_unreachable("bad destruction kind");
  }

  CleanupKind getCleanupKind(QualType::DestructionKind kind) {
    return (needsEHCleanup(kind) ? NormalAndEHCleanup : NormalCleanup);
  }

  //===--------------------------------------------------------------------===//
  //                                  Objective-C
  //===--------------------------------------------------------------------===//

  void GenerateObjCMethod(const ObjCMethodDecl *OMD);

  void StartObjCMethod(const ObjCMethodDecl *MD, const ObjCContainerDecl *CD);

  /// GenerateObjCGetter - Synthesize an Objective-C property getter function.
  void GenerateObjCGetter(ObjCImplementationDecl *IMP,
                          const ObjCPropertyImplDecl *PID);
  void generateObjCGetterBody(const ObjCImplementationDecl *classImpl,
                              const ObjCPropertyImplDecl *propImpl,
                              const ObjCMethodDecl *GetterMothodDecl,
                              llvm::Constant *AtomicHelperFn);

  void GenerateObjCCtorDtorMethod(ObjCImplementationDecl *IMP,
                                  ObjCMethodDecl *MD, bool ctor);

  /// GenerateObjCSetter - Synthesize an Objective-C property setter function
  /// for the given property.
  void GenerateObjCSetter(ObjCImplementationDecl *IMP,
                          const ObjCPropertyImplDecl *PID);
  void generateObjCSetterBody(const ObjCImplementationDecl *classImpl,
                              const ObjCPropertyImplDecl *propImpl,
                              llvm::Constant *AtomicHelperFn);

  //===--------------------------------------------------------------------===//
  //                                  Block Bits
  //===--------------------------------------------------------------------===//

  /// Emit block literal.
  /// \return an LLVM value which is a pointer to a struct which contains
  /// information about the block, including the block invoke function, the
  /// captured variables, etc.
  llvm::Value *EmitBlockLiteral(const BlockExpr *);

  llvm::Function *GenerateBlockFunction(GlobalDecl GD,
                                        const CGBlockInfo &Info,
                                        const DeclMapTy &ldm,
                                        bool IsLambdaConversionToBlock,
                                        bool BuildGlobalBlock);

  /// Check if \p T is a C++ class that has a destructor that can throw.
  static bool cxxDestructorCanThrow(QualType T);

  llvm::Constant *GenerateCopyHelperFunction(const CGBlockInfo &blockInfo);
  llvm::Constant *GenerateDestroyHelperFunction(const CGBlockInfo &blockInfo);
  llvm::Constant *GenerateObjCAtomicSetterCopyHelperFunction(
                                             const ObjCPropertyImplDecl *PID);
  llvm::Constant *GenerateObjCAtomicGetterCopyHelperFunction(
                                             const ObjCPropertyImplDecl *PID);
  llvm::Value *EmitBlockCopyAndAutorelease(llvm::Value *Block, QualType Ty);

  void BuildBlockRelease(llvm::Value *DeclPtr, BlockFieldFlags flags,
                         bool CanThrow);

  class AutoVarEmission;

  void emitByrefStructureInit(const AutoVarEmission &emission);

  /// Enter a cleanup to destroy a __block variable.  Note that this
  /// cleanup should be a no-op if the variable hasn't left the stack
  /// yet; if a cleanup is required for the variable itself, that needs
  /// to be done externally.
  ///
  /// \param Kind Cleanup kind.
  ///
  /// \param Addr When \p LoadBlockVarAddr is false, the address of the __block
  /// structure that will be passed to _Block_object_dispose. When
  /// \p LoadBlockVarAddr is true, the address of the field of the block
  /// structure that holds the address of the __block structure.
  ///
  /// \param Flags The flag that will be passed to _Block_object_dispose.
  ///
  /// \param LoadBlockVarAddr Indicates whether we need to emit a load from
  /// \p Addr to get the address of the __block structure.
  void enterByrefCleanup(CleanupKind Kind, Address Addr, BlockFieldFlags Flags,
                         bool LoadBlockVarAddr, bool CanThrow);

  void setBlockContextParameter(const ImplicitParamDecl *D, unsigned argNum,
                                llvm::Value *ptr);

  Address LoadBlockStruct();
  Address GetAddrOfBlockDecl(const VarDecl *var);

  /// BuildBlockByrefAddress - Computes the location of the
  /// data in a variable which is declared as __block.
  Address emitBlockByrefAddress(Address baseAddr, const VarDecl *V,
                                bool followForward = true);
  Address emitBlockByrefAddress(Address baseAddr,
                                const BlockByrefInfo &info,
                                bool followForward,
                                const llvm::Twine &name);

  const BlockByrefInfo &getBlockByrefInfo(const VarDecl *var);

  QualType BuildFunctionArgList(GlobalDecl GD, FunctionArgList &Args);

  void GenerateCode(GlobalDecl GD, llvm::Function *Fn,
                    const CGFunctionInfo &FnInfo);

  /// Annotate the function with an attribute that disables TSan checking at
  /// runtime.
  void markAsIgnoreThreadCheckingAtRuntime(llvm::Function *Fn);

  /// Emit code for the start of a function.
  /// \param Loc       The location to be associated with the function.
  /// \param StartLoc  The location of the function body.
  void StartFunction(GlobalDecl GD,
                     QualType RetTy,
                     llvm::Function *Fn,
                     const CGFunctionInfo &FnInfo,
                     const FunctionArgList &Args,
                     SourceLocation Loc = SourceLocation(),
                     SourceLocation StartLoc = SourceLocation());

  static bool IsConstructorDelegationValid(const CXXConstructorDecl *Ctor);

  void EmitConstructorBody(FunctionArgList &Args);
  void EmitDestructorBody(FunctionArgList &Args);
  void emitImplicitAssignmentOperatorBody(FunctionArgList &Args);
  void EmitFunctionBody(const Stmt *Body);
  void EmitBlockWithFallThrough(llvm::BasicBlock *BB, const Stmt *S);

  void EmitForwardingCallToLambda(const CXXMethodDecl *LambdaCallOperator,
                                  CallArgList &CallArgs,
                                  const CGFunctionInfo *CallOpFnInfo = nullptr,
                                  llvm::Constant *CallOpFn = nullptr);
  void EmitLambdaBlockInvokeBody();
  void EmitLambdaStaticInvokeBody(const CXXMethodDecl *MD);
  void EmitLambdaDelegatingInvokeBody(const CXXMethodDecl *MD,
                                      CallArgList &CallArgs);
  void EmitLambdaInAllocaImplFn(const CXXMethodDecl *CallOp,
                                const CGFunctionInfo **ImplFnInfo,
                                llvm::Function **ImplFn);
  void EmitLambdaInAllocaCallOpBody(const CXXMethodDecl *MD);
  void EmitLambdaVLACapture(const VariableArrayType *VAT, LValue LV) {
    EmitStoreThroughLValue(RValue::get(VLASizeMap[VAT->getSizeExpr()]), LV);
  }
  void EmitAsanPrologueOrEpilogue(bool Prologue);

  /// Emit the unified return block, trying to avoid its emission when
  /// possible.
  /// \return The debug location of the user written return statement if the
  /// return block is avoided.
  llvm::DebugLoc EmitReturnBlock();

  /// FinishFunction - Complete IR generation of the current function. It is
  /// legal to call this function even if there is no current insertion point.
  void FinishFunction(SourceLocation EndLoc=SourceLocation());

  void StartThunk(llvm::Function *Fn, GlobalDecl GD,
                  const CGFunctionInfo &FnInfo, bool IsUnprototyped);

  void EmitCallAndReturnForThunk(llvm::FunctionCallee Callee,
                                 const ThunkInfo *Thunk, bool IsUnprototyped);

  void FinishThunk();

  /// Emit a musttail call for a thunk with a potentially adjusted this pointer.
  void EmitMustTailThunk(GlobalDecl GD, llvm::Value *AdjustedThisPtr,
                         llvm::FunctionCallee Callee);

  /// Generate a thunk for the given method.
  void generateThunk(llvm::Function *Fn, const CGFunctionInfo &FnInfo,
                     GlobalDecl GD, const ThunkInfo &Thunk,
                     bool IsUnprototyped);

  llvm::Function *GenerateVarArgsThunk(llvm::Function *Fn,
                                       const CGFunctionInfo &FnInfo,
                                       GlobalDecl GD, const ThunkInfo &Thunk);

  void EmitCtorPrologue(const CXXConstructorDecl *CD, CXXCtorType Type,
                        FunctionArgList &Args);

  void EmitInitializerForField(FieldDecl *Field, LValue LHS, Expr *Init);

  /// Struct with all information about dynamic [sub]class needed to set vptr.
  struct VPtr {
    BaseSubobject Base;
    const CXXRecordDecl *NearestVBase;
    CharUnits OffsetFromNearestVBase;
    const CXXRecordDecl *VTableClass;
  };

  /// Initialize the vtable pointer of the given subobject.
  void InitializeVTablePointer(const VPtr &vptr);

  typedef llvm::SmallVector<VPtr, 4> VPtrsVector;

  typedef llvm::SmallPtrSet<const CXXRecordDecl *, 4> VisitedVirtualBasesSetTy;
  VPtrsVector getVTablePointers(const CXXRecordDecl *VTableClass);

  void getVTablePointers(BaseSubobject Base, const CXXRecordDecl *NearestVBase,
                         CharUnits OffsetFromNearestVBase,
                         bool BaseIsNonVirtualPrimaryBase,
                         const CXXRecordDecl *VTableClass,
                         VisitedVirtualBasesSetTy &VBases, VPtrsVector &vptrs);

  void InitializeVTablePointers(const CXXRecordDecl *ClassDecl);

  // VTableTrapMode - whether we guarantee that loading the
  // vtable is guaranteed to trap on authentication failure,
  // even if the resulting vtable pointer is unused.
  enum class VTableAuthMode {
    Authenticate,
    MustTrap,
    UnsafeUbsanStrip // Should only be used for Vptr UBSan check
  };
  /// GetVTablePtr - Return the Value of the vtable pointer member pointed
  /// to by This.
  llvm::Value *
  GetVTablePtr(Address This, llvm::Type *VTableTy,
               const CXXRecordDecl *VTableClass,
               VTableAuthMode AuthMode = VTableAuthMode::Authenticate);

  enum CFITypeCheckKind {
    CFITCK_VCall,
    CFITCK_NVCall,
    CFITCK_DerivedCast,
    CFITCK_UnrelatedCast,
    CFITCK_ICall,
    CFITCK_NVMFCall,
    CFITCK_VMFCall,
  };

  /// Derived is the presumed address of an object of type T after a
  /// cast. If T is a polymorphic class type, emit a check that the virtual
  /// table for Derived belongs to a class derived from T.
  void EmitVTablePtrCheckForCast(QualType T, Address Derived, bool MayBeNull,
                                 CFITypeCheckKind TCK, SourceLocation Loc);

  /// EmitVTablePtrCheckForCall - Virtual method MD is being called via VTable.
  /// If vptr CFI is enabled, emit a check that VTable is valid.
  void EmitVTablePtrCheckForCall(const CXXRecordDecl *RD, llvm::Value *VTable,
                                 CFITypeCheckKind TCK, SourceLocation Loc);

  /// EmitVTablePtrCheck - Emit a check that VTable is a valid virtual table for
  /// RD using llvm.type.test.
  void EmitVTablePtrCheck(const CXXRecordDecl *RD, llvm::Value *VTable,
                          CFITypeCheckKind TCK, SourceLocation Loc);

  /// If whole-program virtual table optimization is enabled, emit an assumption
  /// that VTable is a member of RD's type identifier. Or, if vptr CFI is
  /// enabled, emit a check that VTable is a member of RD's type identifier.
  void EmitTypeMetadataCodeForVCall(const CXXRecordDecl *RD,
                                    llvm::Value *VTable, SourceLocation Loc);

  /// Returns whether we should perform a type checked load when loading a
  /// virtual function for virtual calls to members of RD. This is generally
  /// true when both vcall CFI and whole-program-vtables are enabled.
  bool ShouldEmitVTableTypeCheckedLoad(const CXXRecordDecl *RD);

  /// Emit a type checked load from the given vtable.
  llvm::Value *EmitVTableTypeCheckedLoad(const CXXRecordDecl *RD,
                                         llvm::Value *VTable,
                                         llvm::Type *VTableTy,
                                         uint64_t VTableByteOffset);

  /// EnterDtorCleanups - Enter the cleanups necessary to complete the
  /// given phase of destruction for a destructor.  The end result
  /// should call destructors on members and base classes in reverse
  /// order of their construction.
  void EnterDtorCleanups(const CXXDestructorDecl *Dtor, CXXDtorType Type);

  /// ShouldInstrumentFunction - Return true if the current function should be
  /// instrumented with __cyg_profile_func_* calls
  bool ShouldInstrumentFunction();

  /// ShouldSkipSanitizerInstrumentation - Return true if the current function
  /// should not be instrumented with sanitizers.
  bool ShouldSkipSanitizerInstrumentation();

  /// ShouldXRayInstrument - Return true if the current function should be
  /// instrumented with XRay nop sleds.
  bool ShouldXRayInstrumentFunction() const;

  /// AlwaysEmitXRayCustomEvents - Return true if we must unconditionally emit
  /// XRay custom event handling calls.
  bool AlwaysEmitXRayCustomEvents() const;

  /// AlwaysEmitXRayTypedEvents - Return true if clang must unconditionally emit
  /// XRay typed event handling calls.
  bool AlwaysEmitXRayTypedEvents() const;

  /// Return a type hash constant for a function instrumented by
  /// -fsanitize=function.
  llvm::ConstantInt *getUBSanFunctionTypeHash(QualType T) const;

  /// EmitFunctionProlog - Emit the target specific LLVM code to load the
  /// arguments for the given function. This is also responsible for naming the
  /// LLVM function arguments.
  void EmitFunctionProlog(const CGFunctionInfo &FI,
                          llvm::Function *Fn,
                          const FunctionArgList &Args);

  /// EmitFunctionEpilog - Emit the target specific LLVM code to return the
  /// given temporary.
  void EmitFunctionEpilog(const CGFunctionInfo &FI, bool EmitRetDbgLoc,
                          SourceLocation EndLoc);

  /// Emit a test that checks if the return value \p RV is nonnull.
  void EmitReturnValueCheck(llvm::Value *RV);

  /// EmitStartEHSpec - Emit the start of the exception spec.
  void EmitStartEHSpec(const Decl *D);

  /// EmitEndEHSpec - Emit the end of the exception spec.
  void EmitEndEHSpec(const Decl *D);

  /// getTerminateLandingPad - Return a landing pad that just calls terminate.
  llvm::BasicBlock *getTerminateLandingPad();

  /// getTerminateLandingPad - Return a cleanup funclet that just calls
  /// terminate.
  llvm::BasicBlock *getTerminateFunclet();

  /// getTerminateHandler - Return a handler (not a landing pad, just
  /// a catch handler) that just calls terminate.  This is used when
  /// a terminate scope encloses a try.
  llvm::BasicBlock *getTerminateHandler();

  llvm::Type *ConvertTypeForMem(QualType T);
  llvm::Type *ConvertType(QualType T);
  llvm::Type *convertTypeForLoadStore(QualType ASTTy,
                                      llvm::Type *LLVMTy = nullptr);
  llvm::Type *ConvertType(const TypeDecl *T) {
    return ConvertType(getContext().getTypeDeclType(T));
  }

  /// LoadObjCSelf - Load the value of self. This function is only valid while
  /// generating code for an Objective-C method.
  llvm::Value *LoadObjCSelf();

  /// TypeOfSelfObject - Return type of object that this self represents.
  QualType TypeOfSelfObject();

  /// getEvaluationKind - Return the TypeEvaluationKind of QualType \c T.
  static TypeEvaluationKind getEvaluationKind(QualType T);

  static bool hasScalarEvaluationKind(QualType T) {
    return getEvaluationKind(T) == TEK_Scalar;
  }

  static bool hasAggregateEvaluationKind(QualType T) {
    return getEvaluationKind(T) == TEK_Aggregate;
  }

  /// createBasicBlock - Create an LLVM basic block.
  llvm::BasicBlock *createBasicBlock(const Twine &name = "",
                                     llvm::Function *parent = nullptr,
                                     llvm::BasicBlock *before = nullptr) {
    return llvm::BasicBlock::Create(getLLVMContext(), name, parent, before);
  }

  /// getBasicBlockForLabel - Return the LLVM basicblock that the specified
  /// label maps to.
  JumpDest getJumpDestForLabel(const LabelDecl *S);

  /// SimplifyForwardingBlocks - If the given basic block is only a branch to
  /// another basic block, simplify it. This assumes that no other code could
  /// potentially reference the basic block.
  void SimplifyForwardingBlocks(llvm::BasicBlock *BB);

  /// EmitBlock - Emit the given block \arg BB and set it as the insert point,
  /// adding a fall-through branch from the current insert block if
  /// necessary. It is legal to call this function even if there is no current
  /// insertion point.
  ///
  /// IsFinished - If true, indicates that the caller has finished emitting
  /// branches to the given block and does not expect to emit code into it. This
  /// means the block can be ignored if it is unreachable.
  void EmitBlock(llvm::BasicBlock *BB, bool IsFinished=false);

  /// EmitBlockAfterUses - Emit the given block somewhere hopefully
  /// near its uses, and leave the insertion point in it.
  void EmitBlockAfterUses(llvm::BasicBlock *BB);

  /// EmitBranch - Emit a branch to the specified basic block from the current
  /// insert block, taking care to avoid creation of branches from dummy
  /// blocks. It is legal to call this function even if there is no current
  /// insertion point.
  ///
  /// This function clears the current insertion point. The caller should follow
  /// calls to this function with calls to Emit*Block prior to generation new
  /// code.
  void EmitBranch(llvm::BasicBlock *Block);

  /// HaveInsertPoint - True if an insertion point is defined. If not, this
  /// indicates that the current code being emitted is unreachable.
  bool HaveInsertPoint() const {
    return Builder.GetInsertBlock() != nullptr;
  }

  /// EnsureInsertPoint - Ensure that an insertion point is defined so that
  /// emitted IR has a place to go. Note that by definition, if this function
  /// creates a block then that block is unreachable; callers may do better to
  /// detect when no insertion point is defined and simply skip IR generation.
  void EnsureInsertPoint() {
    if (!HaveInsertPoint())
      EmitBlock(createBasicBlock());
  }

  /// ErrorUnsupported - Print out an error that codegen doesn't support the
  /// specified stmt yet.
  void ErrorUnsupported(const Stmt *S, const char *Type);

  //===--------------------------------------------------------------------===//
  //                                  Helpers
  //===--------------------------------------------------------------------===//

  Address mergeAddressesInConditionalExpr(Address LHS, Address RHS,
                                          llvm::BasicBlock *LHSBlock,
                                          llvm::BasicBlock *RHSBlock,
                                          llvm::BasicBlock *MergeBlock,
                                          QualType MergedType) {
    Builder.SetInsertPoint(MergeBlock);
    llvm::PHINode *PtrPhi = Builder.CreatePHI(LHS.getType(), 2, "cond");
    PtrPhi->addIncoming(LHS.getBasePointer(), LHSBlock);
    PtrPhi->addIncoming(RHS.getBasePointer(), RHSBlock);
    LHS.replaceBasePointer(PtrPhi);
    LHS.setAlignment(std::min(LHS.getAlignment(), RHS.getAlignment()));
    return LHS;
  }

  /// Construct an address with the natural alignment of T. If a pointer to T
  /// is expected to be signed, the pointer passed to this function must have
  /// been signed, and the returned Address will have the pointer authentication
  /// information needed to authenticate the signed pointer.
  Address makeNaturalAddressForPointer(
      llvm::Value *Ptr, QualType T, CharUnits Alignment = CharUnits::Zero(),
      bool ForPointeeType = false, LValueBaseInfo *BaseInfo = nullptr,
      TBAAAccessInfo *TBAAInfo = nullptr,
      KnownNonNull_t IsKnownNonNull = NotKnownNonNull) {
    if (Alignment.isZero())
      Alignment =
          CGM.getNaturalTypeAlignment(T, BaseInfo, TBAAInfo, ForPointeeType);
    return Address(Ptr, ConvertTypeForMem(T), Alignment,
                   CGM.getPointerAuthInfoForPointeeType(T), /*Offset=*/nullptr,
                   IsKnownNonNull);
  }

  LValue MakeAddrLValue(Address Addr, QualType T,
                        AlignmentSource Source = AlignmentSource::Type) {
    return MakeAddrLValue(Addr, T, LValueBaseInfo(Source),
                          CGM.getTBAAAccessInfo(T));
  }

  LValue MakeAddrLValue(Address Addr, QualType T, LValueBaseInfo BaseInfo,
                        TBAAAccessInfo TBAAInfo) {
    return LValue::MakeAddr(Addr, T, getContext(), BaseInfo, TBAAInfo);
  }

  LValue MakeAddrLValue(llvm::Value *V, QualType T, CharUnits Alignment,
                        AlignmentSource Source = AlignmentSource::Type) {
    return MakeAddrLValue(makeNaturalAddressForPointer(V, T, Alignment), T,
                          LValueBaseInfo(Source), CGM.getTBAAAccessInfo(T));
  }

  /// Same as MakeAddrLValue above except that the pointer is known to be
  /// unsigned.
  LValue MakeRawAddrLValue(llvm::Value *V, QualType T, CharUnits Alignment,
                           AlignmentSource Source = AlignmentSource::Type) {
    Address Addr(V, ConvertTypeForMem(T), Alignment);
    return LValue::MakeAddr(Addr, T, getContext(), LValueBaseInfo(Source),
                            CGM.getTBAAAccessInfo(T));
  }

  LValue
  MakeAddrLValueWithoutTBAA(Address Addr, QualType T,
                            AlignmentSource Source = AlignmentSource::Type) {
    return LValue::MakeAddr(Addr, T, getContext(), LValueBaseInfo(Source),
                            TBAAAccessInfo());
  }

  /// Given a value of type T* that may not be to a complete object, construct
  /// an l-value with the natural pointee alignment of T.
  LValue MakeNaturalAlignPointeeAddrLValue(llvm::Value *V, QualType T);

  LValue
  MakeNaturalAlignAddrLValue(llvm::Value *V, QualType T,
                             KnownNonNull_t IsKnownNonNull = NotKnownNonNull);

  /// Same as MakeNaturalAlignPointeeAddrLValue except that the pointer is known
  /// to be unsigned.
  LValue MakeNaturalAlignPointeeRawAddrLValue(llvm::Value *V, QualType T);

  LValue MakeNaturalAlignRawAddrLValue(llvm::Value *V, QualType T);

  Address EmitLoadOfReference(LValue RefLVal,
                              LValueBaseInfo *PointeeBaseInfo = nullptr,
                              TBAAAccessInfo *PointeeTBAAInfo = nullptr);
  LValue EmitLoadOfReferenceLValue(LValue RefLVal);
  LValue EmitLoadOfReferenceLValue(Address RefAddr, QualType RefTy,
                                   AlignmentSource Source =
                                       AlignmentSource::Type) {
    LValue RefLVal = MakeAddrLValue(RefAddr, RefTy, LValueBaseInfo(Source),
                                    CGM.getTBAAAccessInfo(RefTy));
    return EmitLoadOfReferenceLValue(RefLVal);
  }

  /// Load a pointer with type \p PtrTy stored at address \p Ptr.
  /// Note that \p PtrTy is the type of the loaded pointer, not the addresses
  /// it is loaded from.
  Address EmitLoadOfPointer(Address Ptr, const PointerType *PtrTy,
                            LValueBaseInfo *BaseInfo = nullptr,
                            TBAAAccessInfo *TBAAInfo = nullptr);
  LValue EmitLoadOfPointerLValue(Address Ptr, const PointerType *PtrTy);

private:
  struct AllocaTracker {
    void Add(llvm::AllocaInst *I) { Allocas.push_back(I); }
    llvm::SmallVector<llvm::AllocaInst *> Take() { return std::move(Allocas); }

  private:
    llvm::SmallVector<llvm::AllocaInst *> Allocas;
  };
  AllocaTracker *Allocas = nullptr;

public:
  // Captures all the allocas created during the scope of its RAII object.
  struct AllocaTrackerRAII {
    AllocaTrackerRAII(CodeGenFunction &CGF)
        : CGF(CGF), OldTracker(CGF.Allocas) {
      CGF.Allocas = &Tracker;
    }
    ~AllocaTrackerRAII() { CGF.Allocas = OldTracker; }

    llvm::SmallVector<llvm::AllocaInst *> Take() { return Tracker.Take(); }

  private:
    CodeGenFunction &CGF;
    AllocaTracker *OldTracker;
    AllocaTracker Tracker;
  };

  /// CreateTempAlloca - This creates an alloca and inserts it into the entry
  /// block if \p ArraySize is nullptr, otherwise inserts it at the current
  /// insertion point of the builder. The caller is responsible for setting an
  /// appropriate alignment on
  /// the alloca.
  ///
  /// \p ArraySize is the number of array elements to be allocated if it
  ///    is not nullptr.
  ///
  /// LangAS::Default is the address space of pointers to local variables and
  /// temporaries, as exposed in the source language. In certain
  /// configurations, this is not the same as the alloca address space, and a
  /// cast is needed to lift the pointer from the alloca AS into
  /// LangAS::Default. This can happen when the target uses a restricted
  /// address space for the stack but the source language requires
  /// LangAS::Default to be a generic address space. The latter condition is
  /// common for most programming languages; OpenCL is an exception in that
  /// LangAS::Default is the private address space, which naturally maps
  /// to the stack.
  ///
  /// Because the address of a temporary is often exposed to the program in
  /// various ways, this function will perform the cast. The original alloca
  /// instruction is returned through \p Alloca if it is not nullptr.
  ///
  /// The cast is not performaed in CreateTempAllocaWithoutCast. This is
  /// more efficient if the caller knows that the address will not be exposed.
  llvm::AllocaInst *CreateTempAlloca(llvm::Type *Ty, const Twine &Name = "tmp",
                                     llvm::Value *ArraySize = nullptr);
  RawAddress CreateTempAlloca(llvm::Type *Ty, CharUnits align,
                              const Twine &Name = "tmp",
                              llvm::Value *ArraySize = nullptr,
                              RawAddress *Alloca = nullptr);
  RawAddress CreateTempAllocaWithoutCast(llvm::Type *Ty, CharUnits align,
                                         const Twine &Name = "tmp",
                                         llvm::Value *ArraySize = nullptr);

  /// CreateDefaultAlignedTempAlloca - This creates an alloca with the
  /// default ABI alignment of the given LLVM type.
  ///
  /// IMPORTANT NOTE: This is *not* generally the right alignment for
  /// any given AST type that happens to have been lowered to the
  /// given IR type.  This should only ever be used for function-local,
  /// IR-driven manipulations like saving and restoring a value.  Do
  /// not hand this address off to arbitrary IRGen routines, and especially
  /// do not pass it as an argument to a function that might expect a
  /// properly ABI-aligned value.
  RawAddress CreateDefaultAlignTempAlloca(llvm::Type *Ty,
                                          const Twine &Name = "tmp");

  /// CreateIRTemp - Create a temporary IR object of the given type, with
  /// appropriate alignment. This routine should only be used when an temporary
  /// value needs to be stored into an alloca (for example, to avoid explicit
  /// PHI construction), but the type is the IR type, not the type appropriate
  /// for storing in memory.
  ///
  /// That is, this is exactly equivalent to CreateMemTemp, but calling
  /// ConvertType instead of ConvertTypeForMem.
  RawAddress CreateIRTemp(QualType T, const Twine &Name = "tmp");

  /// CreateMemTemp - Create a temporary memory object of the given type, with
  /// appropriate alignmen and cast it to the default address space. Returns
  /// the original alloca instruction by \p Alloca if it is not nullptr.
  RawAddress CreateMemTemp(QualType T, const Twine &Name = "tmp",
                           RawAddress *Alloca = nullptr);
  RawAddress CreateMemTemp(QualType T, CharUnits Align,
                           const Twine &Name = "tmp",
                           RawAddress *Alloca = nullptr);

  /// CreateMemTemp - Create a temporary memory object of the given type, with
  /// appropriate alignmen without casting it to the default address space.
  RawAddress CreateMemTempWithoutCast(QualType T, const Twine &Name = "tmp");
  RawAddress CreateMemTempWithoutCast(QualType T, CharUnits Align,
                                      const Twine &Name = "tmp");

  /// CreateAggTemp - Create a temporary memory object for the given
  /// aggregate type.
  AggValueSlot CreateAggTemp(QualType T, const Twine &Name = "tmp",
                             RawAddress *Alloca = nullptr) {
    return AggValueSlot::forAddr(
        CreateMemTemp(T, Name, Alloca), T.getQualifiers(),
        AggValueSlot::IsNotDestructed, AggValueSlot::DoesNotNeedGCBarriers,
        AggValueSlot::IsNotAliased, AggValueSlot::DoesNotOverlap);
  }

  /// EvaluateExprAsBool - Perform the usual unary conversions on the specified
  /// expression and compare the result against zero, returning an Int1Ty value.
  llvm::Value *EvaluateExprAsBool(const Expr *E);

  /// Retrieve the implicit cast expression of the rhs in a binary operator
  /// expression by passing pointers to Value and QualType
  /// This is used for implicit bitfield conversion checks, which
  /// must compare with the value before potential truncation.
  llvm::Value *EmitWithOriginalRHSBitfieldAssignment(const BinaryOperator *E,
                                                     llvm::Value **Previous,
                                                     QualType *SrcType);

  /// Emit a check that an [implicit] conversion of a bitfield. It is not UB,
  /// so we use the value after conversion.
  void EmitBitfieldConversionCheck(llvm::Value *Src, QualType SrcType,
                                   llvm::Value *Dst, QualType DstType,
                                   const CGBitFieldInfo &Info,
                                   SourceLocation Loc);

  /// EmitIgnoredExpr - Emit an expression in a context which ignores the result.
  void EmitIgnoredExpr(const Expr *E);

  /// EmitAnyExpr - Emit code to compute the specified expression which can have
  /// any type.  The result is returned as an RValue struct.  If this is an
  /// aggregate expression, the aggloc/agglocvolatile arguments indicate where
  /// the result should be returned.
  ///
  /// \param ignoreResult True if the resulting value isn't used.
  RValue EmitAnyExpr(const Expr *E,
                     AggValueSlot aggSlot = AggValueSlot::ignored(),
                     bool ignoreResult = false);

  // EmitVAListRef - Emit a "reference" to a va_list; this is either the address
  // or the value of the expression, depending on how va_list is defined.
  Address EmitVAListRef(const Expr *E);

  /// Emit a "reference" to a __builtin_ms_va_list; this is
  /// always the value of the expression, because a __builtin_ms_va_list is a
  /// pointer to a char.
  Address EmitMSVAListRef(const Expr *E);

  /// EmitAnyExprToTemp - Similarly to EmitAnyExpr(), however, the result will
  /// always be accessible even if no aggregate location is provided.
  RValue EmitAnyExprToTemp(const Expr *E);

  /// EmitAnyExprToMem - Emits the code necessary to evaluate an
  /// arbitrary expression into the given memory location.
  void EmitAnyExprToMem(const Expr *E, Address Location,
                        Qualifiers Quals, bool IsInitializer);

  void EmitAnyExprToExn(const Expr *E, Address Addr);

  /// EmitExprAsInit - Emits the code necessary to initialize a
  /// location in memory with the given initializer.
  void EmitExprAsInit(const Expr *init, const ValueDecl *D, LValue lvalue,
                      bool capturedByInit);

  /// hasVolatileMember - returns true if aggregate type has a volatile
  /// member.
  bool hasVolatileMember(QualType T) {
    if (const RecordType *RT = T->getAs<RecordType>()) {
      const RecordDecl *RD = cast<RecordDecl>(RT->getDecl());
      return RD->hasVolatileMember();
    }
    return false;
  }

  /// Determine whether a return value slot may overlap some other object.
  AggValueSlot::Overlap_t getOverlapForReturnValue() {
    // FIXME: Assuming no overlap here breaks guaranteed copy elision for base
    // class subobjects. These cases may need to be revisited depending on the
    // resolution of the relevant core issue.
    return AggValueSlot::DoesNotOverlap;
  }

  /// Determine whether a field initialization may overlap some other object.
  AggValueSlot::Overlap_t getOverlapForFieldInit(const FieldDecl *FD);

  /// Determine whether a base class initialization may overlap some other
  /// object.
  AggValueSlot::Overlap_t getOverlapForBaseInit(const CXXRecordDecl *RD,
                                                const CXXRecordDecl *BaseRD,
                                                bool IsVirtual);

  /// Emit an aggregate assignment.
  void EmitAggregateAssign(LValue Dest, LValue Src, QualType EltTy) {
    bool IsVolatile = hasVolatileMember(EltTy);
    EmitAggregateCopy(Dest, Src, EltTy, AggValueSlot::MayOverlap, IsVolatile);
  }

  void EmitAggregateCopyCtor(LValue Dest, LValue Src,
                             AggValueSlot::Overlap_t MayOverlap) {
    EmitAggregateCopy(Dest, Src, Src.getType(), MayOverlap);
  }

  /// EmitAggregateCopy - Emit an aggregate copy.
  ///
  /// \param isVolatile \c true iff either the source or the destination is
  ///        volatile.
  /// \param MayOverlap Whether the tail padding of the destination might be
  ///        occupied by some other object. More efficient code can often be
  ///        generated if not.
  void EmitAggregateCopy(LValue Dest, LValue Src, QualType EltTy,
                         AggValueSlot::Overlap_t MayOverlap,
                         bool isVolatile = false);

  /// GetAddrOfLocalVar - Return the address of a local variable.
  Address GetAddrOfLocalVar(const VarDecl *VD) {
    auto it = LocalDeclMap.find(VD);
    assert(it != LocalDeclMap.end() &&
           "Invalid argument to GetAddrOfLocalVar(), no decl!");
    return it->second;
  }

  /// Given an opaque value expression, return its LValue mapping if it exists,
  /// otherwise create one.
  LValue getOrCreateOpaqueLValueMapping(const OpaqueValueExpr *e);

  /// Given an opaque value expression, return its RValue mapping if it exists,
  /// otherwise create one.
  RValue getOrCreateOpaqueRValueMapping(const OpaqueValueExpr *e);

  /// Get the index of the current ArrayInitLoopExpr, if any.
  llvm::Value *getArrayInitIndex() { return ArrayInitIndex; }

  /// getAccessedFieldNo - Given an encoded value and a result number, return
  /// the input field number being accessed.
  static unsigned getAccessedFieldNo(unsigned Idx, const llvm::Constant *Elts);

  llvm::BlockAddress *GetAddrOfLabel(const LabelDecl *L);
  llvm::BasicBlock *GetIndirectGotoBlock();

  /// Check if \p E is a C++ "this" pointer wrapped in value-preserving casts.
  static bool IsWrappedCXXThis(const Expr *E);

  /// EmitNullInitialization - Generate code to set a value of the given type to
  /// null, If the type contains data member pointers, they will be initialized
  /// to -1 in accordance with the Itanium C++ ABI.
  void EmitNullInitialization(Address DestPtr, QualType Ty);

  /// Emits a call to an LLVM variable-argument intrinsic, either
  /// \c llvm.va_start or \c llvm.va_end.
  /// \param ArgValue A reference to the \c va_list as emitted by either
  /// \c EmitVAListRef or \c EmitMSVAListRef.
  /// \param IsStart If \c true, emits a call to \c llvm.va_start; otherwise,
  /// calls \c llvm.va_end.
  llvm::Value *EmitVAStartEnd(llvm::Value *ArgValue, bool IsStart);

  /// Generate code to get an argument from the passed in pointer
  /// and update it accordingly.
  /// \param VE The \c VAArgExpr for which to generate code.
  /// \param VAListAddr Receives a reference to the \c va_list as emitted by
  /// either \c EmitVAListRef or \c EmitMSVAListRef.
  /// \returns A pointer to the argument.
  // FIXME: We should be able to get rid of this method and use the va_arg
  // instruction in LLVM instead once it works well enough.
  RValue EmitVAArg(VAArgExpr *VE, Address &VAListAddr,
                   AggValueSlot Slot = AggValueSlot::ignored());

  /// emitArrayLength - Compute the length of an array, even if it's a
  /// VLA, and drill down to the base element type.
  llvm::Value *emitArrayLength(const ArrayType *arrayType,
                               QualType &baseType,
                               Address &addr);

  /// EmitVLASize - Capture all the sizes for the VLA expressions in
  /// the given variably-modified type and store them in the VLASizeMap.
  ///
  /// This function can be called with a null (unreachable) insert point.
  void EmitVariablyModifiedType(QualType Ty);

  struct VlaSizePair {
    llvm::Value *NumElts;
    QualType Type;

    VlaSizePair(llvm::Value *NE, QualType T) : NumElts(NE), Type(T) {}
  };

  /// Return the number of elements for a single dimension
  /// for the given array type.
  VlaSizePair getVLAElements1D(const VariableArrayType *vla);
  VlaSizePair getVLAElements1D(QualType vla);

  /// Returns an LLVM value that corresponds to the size,
  /// in non-variably-sized elements, of a variable length array type,
  /// plus that largest non-variably-sized element type.  Assumes that
  /// the type has already been emitted with EmitVariablyModifiedType.
  VlaSizePair getVLASize(const VariableArrayType *vla);
  VlaSizePair getVLASize(QualType vla);

  /// LoadCXXThis - Load the value of 'this'. This function is only valid while
  /// generating code for an C++ member function.
  llvm::Value *LoadCXXThis() {
    assert(CXXThisValue && "no 'this' value for this function");
    return CXXThisValue;
  }
  Address LoadCXXThisAddress();

  /// LoadCXXVTT - Load the VTT parameter to base constructors/destructors have
  /// virtual bases.
  // FIXME: Every place that calls LoadCXXVTT is something
  // that needs to be abstracted properly.
  llvm::Value *LoadCXXVTT() {
    assert(CXXStructorImplicitParamValue && "no VTT value for this function");
    return CXXStructorImplicitParamValue;
  }

  /// GetAddressOfBaseOfCompleteClass - Convert the given pointer to a
  /// complete class to the given direct base.
  Address
  GetAddressOfDirectBaseInCompleteClass(Address Value,
                                        const CXXRecordDecl *Derived,
                                        const CXXRecordDecl *Base,
                                        bool BaseIsVirtual);

  static bool ShouldNullCheckClassCastValue(const CastExpr *Cast);

  /// GetAddressOfBaseClass - This function will add the necessary delta to the
  /// load of 'this' and returns address of the base class.
  Address GetAddressOfBaseClass(Address Value,
                                const CXXRecordDecl *Derived,
                                CastExpr::path_const_iterator PathBegin,
                                CastExpr::path_const_iterator PathEnd,
                                bool NullCheckValue, SourceLocation Loc);

  Address GetAddressOfDerivedClass(Address Value,
                                   const CXXRecordDecl *Derived,
                                   CastExpr::path_const_iterator PathBegin,
                                   CastExpr::path_const_iterator PathEnd,
                                   bool NullCheckValue);

  /// GetVTTParameter - Return the VTT parameter that should be passed to a
  /// base constructor/destructor with virtual bases.
  /// FIXME: VTTs are Itanium ABI-specific, so the definition should move
  /// to ItaniumCXXABI.cpp together with all the references to VTT.
  llvm::Value *GetVTTParameter(GlobalDecl GD, bool ForVirtualBase,
                               bool Delegating);

  void EmitDelegateCXXConstructorCall(const CXXConstructorDecl *Ctor,
                                      CXXCtorType CtorType,
                                      const FunctionArgList &Args,
                                      SourceLocation Loc);
  // It's important not to confuse this and the previous function. Delegating
  // constructors are the C++0x feature. The constructor delegate optimization
  // is used to reduce duplication in the base and complete consturctors where
  // they are substantially the same.
  void EmitDelegatingCXXConstructorCall(const CXXConstructorDecl *Ctor,
                                        const FunctionArgList &Args);

  /// Emit a call to an inheriting constructor (that is, one that invokes a
  /// constructor inherited from a base class) by inlining its definition. This
  /// is necessary if the ABI does not support forwarding the arguments to the
  /// base class constructor (because they're variadic or similar).
  void EmitInlinedInheritingCXXConstructorCall(const CXXConstructorDecl *Ctor,
                                               CXXCtorType CtorType,
                                               bool ForVirtualBase,
                                               bool Delegating,
                                               CallArgList &Args);

  /// Emit a call to a constructor inherited from a base class, passing the
  /// current constructor's arguments along unmodified (without even making
  /// a copy).
  void EmitInheritedCXXConstructorCall(const CXXConstructorDecl *D,
                                       bool ForVirtualBase, Address This,
                                       bool InheritedFromVBase,
                                       const CXXInheritedCtorInitExpr *E);

  void EmitCXXConstructorCall(const CXXConstructorDecl *D, CXXCtorType Type,
                              bool ForVirtualBase, bool Delegating,
                              AggValueSlot ThisAVS, const CXXConstructExpr *E);

  void EmitCXXConstructorCall(const CXXConstructorDecl *D, CXXCtorType Type,
                              bool ForVirtualBase, bool Delegating,
                              Address This, CallArgList &Args,
                              AggValueSlot::Overlap_t Overlap,
                              SourceLocation Loc, bool NewPointerIsChecked);

  /// Emit assumption load for all bases. Requires to be called only on
  /// most-derived class and not under construction of the object.
  void EmitVTableAssumptionLoads(const CXXRecordDecl *ClassDecl, Address This);

  /// Emit assumption that vptr load == global vtable.
  void EmitVTableAssumptionLoad(const VPtr &vptr, Address This);

  void EmitSynthesizedCXXCopyCtorCall(const CXXConstructorDecl *D,
                                      Address This, Address Src,
                                      const CXXConstructExpr *E);

  void EmitCXXAggrConstructorCall(const CXXConstructorDecl *D,
                                  const ArrayType *ArrayTy,
                                  Address ArrayPtr,
                                  const CXXConstructExpr *E,
                                  bool NewPointerIsChecked,
                                  bool ZeroInitialization = false);

  void EmitCXXAggrConstructorCall(const CXXConstructorDecl *D,
                                  llvm::Value *NumElements,
                                  Address ArrayPtr,
                                  const CXXConstructExpr *E,
                                  bool NewPointerIsChecked,
                                  bool ZeroInitialization = false);

  static Destroyer destroyCXXObject;

  void EmitCXXDestructorCall(const CXXDestructorDecl *D, CXXDtorType Type,
                             bool ForVirtualBase, bool Delegating, Address This,
                             QualType ThisTy);

  void EmitNewArrayInitializer(const CXXNewExpr *E, QualType elementType,
                               llvm::Type *ElementTy, Address NewPtr,
                               llvm::Value *NumElements,
                               llvm::Value *AllocSizeWithoutCookie);

  void EmitCXXTemporary(const CXXTemporary *Temporary, QualType TempType,
                        Address Ptr);

  void EmitSehCppScopeBegin();
  void EmitSehCppScopeEnd();
  void EmitSehTryScopeBegin();
  void EmitSehTryScopeEnd();

  llvm::Value *EmitLifetimeStart(llvm::TypeSize Size, llvm::Value *Addr);
  void EmitLifetimeEnd(llvm::Value *Size, llvm::Value *Addr);

  llvm::Value *EmitCXXNewExpr(const CXXNewExpr *E);
  void EmitCXXDeleteExpr(const CXXDeleteExpr *E);

  void EmitDeleteCall(const FunctionDecl *DeleteFD, llvm::Value *Ptr,
                      QualType DeleteTy, llvm::Value *NumElements = nullptr,
                      CharUnits CookieSize = CharUnits());

  RValue EmitBuiltinNewDeleteCall(const FunctionProtoType *Type,
                                  const CallExpr *TheCallExpr, bool IsDelete);

  llvm::Value *EmitCXXTypeidExpr(const CXXTypeidExpr *E);
  llvm::Value *EmitDynamicCast(Address V, const CXXDynamicCastExpr *DCE);
  Address EmitCXXUuidofExpr(const CXXUuidofExpr *E);

  /// Situations in which we might emit a check for the suitability of a
  /// pointer or glvalue. Needs to be kept in sync with ubsan_handlers.cpp in
  /// compiler-rt.
  enum TypeCheckKind {
    /// Checking the operand of a load. Must be suitably sized and aligned.
    TCK_Load,
    /// Checking the destination of a store. Must be suitably sized and aligned.
    TCK_Store,
    /// Checking the bound value in a reference binding. Must be suitably sized
    /// and aligned, but is not required to refer to an object (until the
    /// reference is used), per core issue 453.
    TCK_ReferenceBinding,
    /// Checking the object expression in a non-static data member access. Must
    /// be an object within its lifetime.
    TCK_MemberAccess,
    /// Checking the 'this' pointer for a call to a non-static member function.
    /// Must be an object within its lifetime.
    TCK_MemberCall,
    /// Checking the 'this' pointer for a constructor call.
    TCK_ConstructorCall,
    /// Checking the operand of a static_cast to a derived pointer type. Must be
    /// null or an object within its lifetime.
    TCK_DowncastPointer,
    /// Checking the operand of a static_cast to a derived reference type. Must
    /// be an object within its lifetime.
    TCK_DowncastReference,
    /// Checking the operand of a cast to a base object. Must be suitably sized
    /// and aligned.
    TCK_Upcast,
    /// Checking the operand of a cast to a virtual base object. Must be an
    /// object within its lifetime.
    TCK_UpcastToVirtualBase,
    /// Checking the value assigned to a _Nonnull pointer. Must not be null.
    TCK_NonnullAssign,
    /// Checking the operand of a dynamic_cast or a typeid expression.  Must be
    /// null or an object within its lifetime.
    TCK_DynamicOperation
  };

  /// Determine whether the pointer type check \p TCK permits null pointers.
  static bool isNullPointerAllowed(TypeCheckKind TCK);

  /// Determine whether the pointer type check \p TCK requires a vptr check.
  static bool isVptrCheckRequired(TypeCheckKind TCK, QualType Ty);

  /// Whether any type-checking sanitizers are enabled. If \c false,
  /// calls to EmitTypeCheck can be skipped.
  bool sanitizePerformTypeCheck() const;

  void EmitTypeCheck(TypeCheckKind TCK, SourceLocation Loc, LValue LV,
                     QualType Type, SanitizerSet SkippedChecks = SanitizerSet(),
                     llvm::Value *ArraySize = nullptr) {
    if (!sanitizePerformTypeCheck())
      return;
    EmitTypeCheck(TCK, Loc, LV.emitRawPointer(*this), Type, LV.getAlignment(),
                  SkippedChecks, ArraySize);
  }

  void EmitTypeCheck(TypeCheckKind TCK, SourceLocation Loc, Address Addr,
                     QualType Type, CharUnits Alignment = CharUnits::Zero(),
                     SanitizerSet SkippedChecks = SanitizerSet(),
                     llvm::Value *ArraySize = nullptr) {
    if (!sanitizePerformTypeCheck())
      return;
    EmitTypeCheck(TCK, Loc, Addr.emitRawPointer(*this), Type, Alignment,
                  SkippedChecks, ArraySize);
  }

  /// Emit a check that \p V is the address of storage of the
  /// appropriate size and alignment for an object of type \p Type
  /// (or if ArraySize is provided, for an array of that bound).
  void EmitTypeCheck(TypeCheckKind TCK, SourceLocation Loc, llvm::Value *V,
                     QualType Type, CharUnits Alignment = CharUnits::Zero(),
                     SanitizerSet SkippedChecks = SanitizerSet(),
                     llvm::Value *ArraySize = nullptr);

  /// Emit a check that \p Base points into an array object, which
  /// we can access at index \p Index. \p Accessed should be \c false if we
  /// this expression is used as an lvalue, for instance in "&Arr[Idx]".
  void EmitBoundsCheck(const Expr *E, const Expr *Base, llvm::Value *Index,
                       QualType IndexType, bool Accessed);
  void EmitBoundsCheckImpl(const Expr *E, llvm::Value *Bound,
                           llvm::Value *Index, QualType IndexType,
                           QualType IndexedType, bool Accessed);

  // Find a struct's flexible array member and get its offset. It may be
  // embedded inside multiple sub-structs, but must still be the last field.
  const FieldDecl *
  FindFlexibleArrayMemberFieldAndOffset(ASTContext &Ctx, const RecordDecl *RD,
                                        const FieldDecl *FAMDecl,
                                        uint64_t &Offset);

  /// Find the FieldDecl specified in a FAM's "counted_by" attribute. Returns
  /// \p nullptr if either the attribute or the field doesn't exist.
  const FieldDecl *FindCountedByField(const FieldDecl *FD);

  /// Build an expression accessing the "counted_by" field.
  llvm::Value *EmitCountedByFieldExpr(const Expr *Base,
                                      const FieldDecl *FAMDecl,
                                      const FieldDecl *CountDecl);

  llvm::Value *EmitScalarPrePostIncDec(const UnaryOperator *E, LValue LV,
                                       bool isInc, bool isPre);
  ComplexPairTy EmitComplexPrePostIncDec(const UnaryOperator *E, LValue LV,
                                         bool isInc, bool isPre);

  /// Converts Location to a DebugLoc, if debug information is enabled.
  llvm::DebugLoc SourceLocToDebugLoc(SourceLocation Location);

  /// Get the record field index as represented in debug info.
  unsigned getDebugInfoFIndex(const RecordDecl *Rec, unsigned FieldIndex);


  //===--------------------------------------------------------------------===//
  //                            Declaration Emission
  //===--------------------------------------------------------------------===//

  /// EmitDecl - Emit a declaration.
  ///
  /// This function can be called with a null (unreachable) insert point.
  void EmitDecl(const Decl &D);

  /// EmitVarDecl - Emit a local variable declaration.
  ///
  /// This function can be called with a null (unreachable) insert point.
  void EmitVarDecl(const VarDecl &D);

  void EmitScalarInit(const Expr *init, const ValueDecl *D, LValue lvalue,
                      bool capturedByInit);

  typedef void SpecialInitFn(CodeGenFunction &Init, const VarDecl &D,
                             llvm::Value *Address);

  /// Determine whether the given initializer is trivial in the sense
  /// that it requires no code to be generated.
  bool isTrivialInitializer(const Expr *Init);

  /// EmitAutoVarDecl - Emit an auto variable declaration.
  ///
  /// This function can be called with a null (unreachable) insert point.
  void EmitAutoVarDecl(const VarDecl &D);

  class AutoVarEmission {
    friend class CodeGenFunction;

    const VarDecl *Variable;

    /// The address of the alloca for languages with explicit address space
    /// (e.g. OpenCL) or alloca casted to generic pointer for address space
    /// agnostic languages (e.g. C++). Invalid if the variable was emitted
    /// as a global constant.
    Address Addr;

    llvm::Value *NRVOFlag;

    /// True if the variable is a __block variable that is captured by an
    /// escaping block.
    bool IsEscapingByRef;

    /// True if the variable is of aggregate type and has a constant
    /// initializer.
    bool IsConstantAggregate;

    /// Non-null if we should use lifetime annotations.
    llvm::Value *SizeForLifetimeMarkers;

    /// Address with original alloca instruction. Invalid if the variable was
    /// emitted as a global constant.
    RawAddress AllocaAddr;

    struct Invalid {};
    AutoVarEmission(Invalid)
        : Variable(nullptr), Addr(Address::invalid()),
          AllocaAddr(RawAddress::invalid()) {}

    AutoVarEmission(const VarDecl &variable)
        : Variable(&variable), Addr(Address::invalid()), NRVOFlag(nullptr),
          IsEscapingByRef(false), IsConstantAggregate(false),
          SizeForLifetimeMarkers(nullptr), AllocaAddr(RawAddress::invalid()) {}

    bool wasEmittedAsGlobal() const { return !Addr.isValid(); }

  public:
    static AutoVarEmission invalid() { return AutoVarEmission(Invalid()); }

    bool useLifetimeMarkers() const {
      return SizeForLifetimeMarkers != nullptr;
    }
    llvm::Value *getSizeForLifetimeMarkers() const {
      assert(useLifetimeMarkers());
      return SizeForLifetimeMarkers;
    }

    /// Returns the raw, allocated address, which is not necessarily
    /// the address of the object itself. It is casted to default
    /// address space for address space agnostic languages.
    Address getAllocatedAddress() const {
      return Addr;
    }

    /// Returns the address for the original alloca instruction.
    RawAddress getOriginalAllocatedAddress() const { return AllocaAddr; }

    /// Returns the address of the object within this declaration.
    /// Note that this does not chase the forwarding pointer for
    /// __block decls.
    Address getObjectAddress(CodeGenFunction &CGF) const {
      if (!IsEscapingByRef) return Addr;

      return CGF.emitBlockByrefAddress(Addr, Variable, /*forward*/ false);
    }
  };
  AutoVarEmission EmitAutoVarAlloca(const VarDecl &var);
  void EmitAutoVarInit(const AutoVarEmission &emission);
  void EmitAutoVarCleanups(const AutoVarEmission &emission);
  void emitAutoVarTypeCleanup(const AutoVarEmission &emission,
                              QualType::DestructionKind dtorKind);

  /// Emits the alloca and debug information for the size expressions for each
  /// dimension of an array. It registers the association of its (1-dimensional)
  /// QualTypes and size expression's debug node, so that CGDebugInfo can
  /// reference this node when creating the DISubrange object to describe the
  /// array types.
  void EmitAndRegisterVariableArrayDimensions(CGDebugInfo *DI,
                                              const VarDecl &D,
                                              bool EmitDebugInfo);

  void EmitStaticVarDecl(const VarDecl &D,
                         llvm::GlobalValue::LinkageTypes Linkage);

  class ParamValue {
    union {
      Address Addr;
      llvm::Value *Value;
    };

    bool IsIndirect;

    ParamValue(llvm::Value *V) : Value(V), IsIndirect(false) {}
    ParamValue(Address A) : Addr(A), IsIndirect(true) {}

  public:
    static ParamValue forDirect(llvm::Value *value) {
      return ParamValue(value);
    }
    static ParamValue forIndirect(Address addr) {
      assert(!addr.getAlignment().isZero());
      return ParamValue(addr);
    }

    bool isIndirect() const { return IsIndirect; }
    llvm::Value *getAnyValue() const {
      if (!isIndirect())
        return Value;
      assert(!Addr.hasOffset() && "unexpected offset");
      return Addr.getBasePointer();
    }

    llvm::Value *getDirectValue() const {
      assert(!isIndirect());
      return Value;
    }

    Address getIndirectAddress() const {
      assert(isIndirect());
      return Addr;
    }
  };

  /// EmitParmDecl - Emit a ParmVarDecl or an ImplicitParamDecl.
  void EmitParmDecl(const VarDecl &D, ParamValue Arg, unsigned ArgNo);

  /// protectFromPeepholes - Protect a value that we're intending to
  /// store to the side, but which will probably be used later, from
  /// aggressive peepholing optimizations that might delete it.
  ///
  /// Pass the result to unprotectFromPeepholes to declare that
  /// protection is no longer required.
  ///
  /// There's no particular reason why this shouldn't apply to
  /// l-values, it's just that no existing peepholes work on pointers.
  PeepholeProtection protectFromPeepholes(RValue rvalue);
  void unprotectFromPeepholes(PeepholeProtection protection);

  void emitAlignmentAssumptionCheck(llvm::Value *Ptr, QualType Ty,
                                    SourceLocation Loc,
                                    SourceLocation AssumptionLoc,
                                    llvm::Value *Alignment,
                                    llvm::Value *OffsetValue,
                                    llvm::Value *TheCheck,
                                    llvm::Instruction *Assumption);

  void emitAlignmentAssumption(llvm::Value *PtrValue, QualType Ty,
                               SourceLocation Loc, SourceLocation AssumptionLoc,
                               llvm::Value *Alignment,
                               llvm::Value *OffsetValue = nullptr);

  void emitAlignmentAssumption(llvm::Value *PtrValue, const Expr *E,
                               SourceLocation AssumptionLoc,
                               llvm::Value *Alignment,
                               llvm::Value *OffsetValue = nullptr);

  //===--------------------------------------------------------------------===//
  //                             Statement Emission
  //===--------------------------------------------------------------------===//

  /// EmitStopPoint - Emit a debug stoppoint if we are emitting debug info.
  void EmitStopPoint(const Stmt *S);

  /// EmitStmt - Emit the code for the statement \arg S. It is legal to call
  /// this function even if there is no current insertion point.
  ///
  /// This function may clear the current insertion point; callers should use
  /// EnsureInsertPoint if they wish to subsequently generate code without first
  /// calling EmitBlock, EmitBranch, or EmitStmt.
  void EmitStmt(const Stmt *S, ArrayRef<const Attr *> Attrs = std::nullopt);

  /// EmitSimpleStmt - Try to emit a "simple" statement which does not
  /// necessarily require an insertion point or debug information; typically
  /// because the statement amounts to a jump or a container of other
  /// statements.
  ///
  /// \return True if the statement was handled.
  bool EmitSimpleStmt(const Stmt *S, ArrayRef<const Attr *> Attrs);

  Address EmitCompoundStmt(const CompoundStmt &S, bool GetLast = false,
                           AggValueSlot AVS = AggValueSlot::ignored());
  Address EmitCompoundStmtWithoutScope(const CompoundStmt &S,
                                       bool GetLast = false,
                                       AggValueSlot AVS =
                                                AggValueSlot::ignored());

  /// EmitLabel - Emit the block for the given label. It is legal to call this
  /// function even if there is no current insertion point.
  void EmitLabel(const LabelDecl *D); // helper for EmitLabelStmt.

  void EmitLabelStmt(const LabelStmt &S);
  void EmitAttributedStmt(const AttributedStmt &S);
  void EmitGotoStmt(const GotoStmt &S);
  void EmitIndirectGotoStmt(const IndirectGotoStmt &S);
  void EmitIfStmt(const IfStmt &S);

  void EmitWhileStmt(const WhileStmt &S,
                     ArrayRef<const Attr *> Attrs = std::nullopt);
  void EmitDoStmt(const DoStmt &S, ArrayRef<const Attr *> Attrs = std::nullopt);
  void EmitForStmt(const ForStmt &S,
                   ArrayRef<const Attr *> Attrs = std::nullopt);
  void EmitReturnStmt(const ReturnStmt &S);
  void EmitDeclStmt(const DeclStmt &S);
  void EmitBreakStmt(const BreakStmt &S);
  void EmitContinueStmt(const ContinueStmt &S);
  void EmitSwitchStmt(const SwitchStmt &S);
  void EmitDefaultStmt(const DefaultStmt &S, ArrayRef<const Attr *> Attrs);
  void EmitCaseStmt(const CaseStmt &S, ArrayRef<const Attr *> Attrs);
  void EmitCaseStmtRange(const CaseStmt &S, ArrayRef<const Attr *> Attrs);
  void EmitAsmStmt(const AsmStmt &S);

  void EmitObjCForCollectionStmt(const ObjCForCollectionStmt &S);
  void EmitObjCAtTryStmt(const ObjCAtTryStmt &S);
  void EmitObjCAtThrowStmt(const ObjCAtThrowStmt &S);
  void EmitObjCAtSynchronizedStmt(const ObjCAtSynchronizedStmt &S);
  void EmitObjCAutoreleasePoolStmt(const ObjCAutoreleasePoolStmt &S);

  void EmitCoroutineBody(const CoroutineBodyStmt &S);
  void EmitCoreturnStmt(const CoreturnStmt &S);
  RValue EmitCoawaitExpr(const CoawaitExpr &E,
                         AggValueSlot aggSlot = AggValueSlot::ignored(),
                         bool ignoreResult = false);
  LValue EmitCoawaitLValue(const CoawaitExpr *E);
  RValue EmitCoyieldExpr(const CoyieldExpr &E,
                         AggValueSlot aggSlot = AggValueSlot::ignored(),
                         bool ignoreResult = false);
  LValue EmitCoyieldLValue(const CoyieldExpr *E);
  RValue EmitCoroutineIntrinsic(const CallExpr *E, unsigned int IID);

  void EnterCXXTryStmt(const CXXTryStmt &S, bool IsFnTryBlock = false);
  void ExitCXXTryStmt(const CXXTryStmt &S, bool IsFnTryBlock = false);

  void EmitCXXTryStmt(const CXXTryStmt &S);
  void EmitSEHTryStmt(const SEHTryStmt &S);
  void EmitSEHLeaveStmt(const SEHLeaveStmt &S);
  void EnterSEHTryStmt(const SEHTryStmt &S);
  void ExitSEHTryStmt(const SEHTryStmt &S);
  void VolatilizeTryBlocks(llvm::BasicBlock *BB,
                           llvm::SmallPtrSet<llvm::BasicBlock *, 10> &V);

  void pushSEHCleanup(CleanupKind kind,
                      llvm::Function *FinallyFunc);
  void startOutlinedSEHHelper(CodeGenFunction &ParentCGF, bool IsFilter,
                              const Stmt *OutlinedStmt);

  llvm::Function *GenerateSEHFilterFunction(CodeGenFunction &ParentCGF,
                                            const SEHExceptStmt &Except);

  llvm::Function *GenerateSEHFinallyFunction(CodeGenFunction &ParentCGF,
                                             const SEHFinallyStmt &Finally);

  void EmitSEHExceptionCodeSave(CodeGenFunction &ParentCGF,
                                llvm::Value *ParentFP,
                                llvm::Value *EntryEBP);
  llvm::Value *EmitSEHExceptionCode();
  llvm::Value *EmitSEHExceptionInfo();
  llvm::Value *EmitSEHAbnormalTermination();

  /// Emit simple code for OpenMP directives in Simd-only mode.
  void EmitSimpleOMPExecutableDirective(const OMPExecutableDirective &D);

  /// Scan the outlined statement for captures from the parent function. For
  /// each capture, mark the capture as escaped and emit a call to
  /// llvm.localrecover. Insert the localrecover result into the LocalDeclMap.
  void EmitCapturedLocals(CodeGenFunction &ParentCGF, const Stmt *OutlinedStmt,
                          bool IsFilter);

  /// Recovers the address of a local in a parent function. ParentVar is the
  /// address of the variable used in the immediate parent function. It can
  /// either be an alloca or a call to llvm.localrecover if there are nested
  /// outlined functions. ParentFP is the frame pointer of the outermost parent
  /// frame.
  Address recoverAddrOfEscapedLocal(CodeGenFunction &ParentCGF,
                                    Address ParentVar,
                                    llvm::Value *ParentFP);

  void EmitCXXForRangeStmt(const CXXForRangeStmt &S,
                           ArrayRef<const Attr *> Attrs = std::nullopt);

  /// Controls insertion of cancellation exit blocks in worksharing constructs.
  class OMPCancelStackRAII {
    CodeGenFunction &CGF;

  public:
    OMPCancelStackRAII(CodeGenFunction &CGF, OpenMPDirectiveKind Kind,
                       bool HasCancel)
        : CGF(CGF) {
      CGF.OMPCancelStack.enter(CGF, Kind, HasCancel);
    }
    ~OMPCancelStackRAII() { CGF.OMPCancelStack.exit(CGF); }
  };

  /// Returns calculated size of the specified type.
  llvm::Value *getTypeSize(QualType Ty);
  LValue InitCapturedStruct(const CapturedStmt &S);
  llvm::Function *EmitCapturedStmt(const CapturedStmt &S, CapturedRegionKind K);
  llvm::Function *GenerateCapturedStmtFunction(const CapturedStmt &S);
  Address GenerateCapturedStmtArgument(const CapturedStmt &S);
  llvm::Function *GenerateOpenMPCapturedStmtFunction(const CapturedStmt &S,
                                                     SourceLocation Loc);
  void GenerateOpenMPCapturedVars(const CapturedStmt &S,
                                  SmallVectorImpl<llvm::Value *> &CapturedVars);
  void emitOMPSimpleStore(LValue LVal, RValue RVal, QualType RValTy,
                          SourceLocation Loc);
  /// Perform element by element copying of arrays with type \a
  /// OriginalType from \a SrcAddr to \a DestAddr using copying procedure
  /// generated by \a CopyGen.
  ///
  /// \param DestAddr Address of the destination array.
  /// \param SrcAddr Address of the source array.
  /// \param OriginalType Type of destination and source arrays.
  /// \param CopyGen Copying procedure that copies value of single array element
  /// to another single array element.
  void EmitOMPAggregateAssign(
      Address DestAddr, Address SrcAddr, QualType OriginalType,
      const llvm::function_ref<void(Address, Address)> CopyGen);
  /// Emit proper copying of data from one variable to another.
  ///
  /// \param OriginalType Original type of the copied variables.
  /// \param DestAddr Destination address.
  /// \param SrcAddr Source address.
  /// \param DestVD Destination variable used in \a CopyExpr (for arrays, has
  /// type of the base array element).
  /// \param SrcVD Source variable used in \a CopyExpr (for arrays, has type of
  /// the base array element).
  /// \param Copy Actual copygin expression for copying data from \a SrcVD to \a
  /// DestVD.
  void EmitOMPCopy(QualType OriginalType,
                   Address DestAddr, Address SrcAddr,
                   const VarDecl *DestVD, const VarDecl *SrcVD,
                   const Expr *Copy);
  /// Emit atomic update code for constructs: \a X = \a X \a BO \a E or
  /// \a X = \a E \a BO \a E.
  ///
  /// \param X Value to be updated.
  /// \param E Update value.
  /// \param BO Binary operation for update operation.
  /// \param IsXLHSInRHSPart true if \a X is LHS in RHS part of the update
  /// expression, false otherwise.
  /// \param AO Atomic ordering of the generated atomic instructions.
  /// \param CommonGen Code generator for complex expressions that cannot be
  /// expressed through atomicrmw instruction.
  /// \returns <true, OldAtomicValue> if simple 'atomicrmw' instruction was
  /// generated, <false, RValue::get(nullptr)> otherwise.
  std::pair<bool, RValue> EmitOMPAtomicSimpleUpdateExpr(
      LValue X, RValue E, BinaryOperatorKind BO, bool IsXLHSInRHSPart,
      llvm::AtomicOrdering AO, SourceLocation Loc,
      const llvm::function_ref<RValue(RValue)> CommonGen);
  bool EmitOMPFirstprivateClause(const OMPExecutableDirective &D,
                                 OMPPrivateScope &PrivateScope);
  void EmitOMPPrivateClause(const OMPExecutableDirective &D,
                            OMPPrivateScope &PrivateScope);
  void EmitOMPUseDevicePtrClause(
      const OMPUseDevicePtrClause &C, OMPPrivateScope &PrivateScope,
      const llvm::DenseMap<const ValueDecl *, llvm::Value *>
          CaptureDeviceAddrMap);
  void EmitOMPUseDeviceAddrClause(
      const OMPUseDeviceAddrClause &C, OMPPrivateScope &PrivateScope,
      const llvm::DenseMap<const ValueDecl *, llvm::Value *>
          CaptureDeviceAddrMap);
  /// Emit code for copyin clause in \a D directive. The next code is
  /// generated at the start of outlined functions for directives:
  /// \code
  /// threadprivate_var1 = master_threadprivate_var1;
  /// operator=(threadprivate_var2, master_threadprivate_var2);
  /// ...
  /// __kmpc_barrier(&loc, global_tid);
  /// \endcode
  ///
  /// \param D OpenMP directive possibly with 'copyin' clause(s).
  /// \returns true if at least one copyin variable is found, false otherwise.
  bool EmitOMPCopyinClause(const OMPExecutableDirective &D);
  /// Emit initial code for lastprivate variables. If some variable is
  /// not also firstprivate, then the default initialization is used. Otherwise
  /// initialization of this variable is performed by EmitOMPFirstprivateClause
  /// method.
  ///
  /// \param D Directive that may have 'lastprivate' directives.
  /// \param PrivateScope Private scope for capturing lastprivate variables for
  /// proper codegen in internal captured statement.
  ///
  /// \returns true if there is at least one lastprivate variable, false
  /// otherwise.
  bool EmitOMPLastprivateClauseInit(const OMPExecutableDirective &D,
                                    OMPPrivateScope &PrivateScope);
  /// Emit final copying of lastprivate values to original variables at
  /// the end of the worksharing or simd directive.
  ///
  /// \param D Directive that has at least one 'lastprivate' directives.
  /// \param IsLastIterCond Boolean condition that must be set to 'i1 true' if
  /// it is the last iteration of the loop code in associated directive, or to
  /// 'i1 false' otherwise. If this item is nullptr, no final check is required.
  void EmitOMPLastprivateClauseFinal(const OMPExecutableDirective &D,
                                     bool NoFinals,
                                     llvm::Value *IsLastIterCond = nullptr);
  /// Emit initial code for linear clauses.
  void EmitOMPLinearClause(const OMPLoopDirective &D,
                           CodeGenFunction::OMPPrivateScope &PrivateScope);
  /// Emit final code for linear clauses.
  /// \param CondGen Optional conditional code for final part of codegen for
  /// linear clause.
  void EmitOMPLinearClauseFinal(
      const OMPLoopDirective &D,
      const llvm::function_ref<llvm::Value *(CodeGenFunction &)> CondGen);
  /// Emit initial code for reduction variables. Creates reduction copies
  /// and initializes them with the values according to OpenMP standard.
  ///
  /// \param D Directive (possibly) with the 'reduction' clause.
  /// \param PrivateScope Private scope for capturing reduction variables for
  /// proper codegen in internal captured statement.
  ///
  void EmitOMPReductionClauseInit(const OMPExecutableDirective &D,
                                  OMPPrivateScope &PrivateScope,
                                  bool ForInscan = false);
  /// Emit final update of reduction values to original variables at
  /// the end of the directive.
  ///
  /// \param D Directive that has at least one 'reduction' directives.
  /// \param ReductionKind The kind of reduction to perform.
  void EmitOMPReductionClauseFinal(const OMPExecutableDirective &D,
                                   const OpenMPDirectiveKind ReductionKind);
  /// Emit initial code for linear variables. Creates private copies
  /// and initializes them with the values according to OpenMP standard.
  ///
  /// \param D Directive (possibly) with the 'linear' clause.
  /// \return true if at least one linear variable is found that should be
  /// initialized with the value of the original variable, false otherwise.
  bool EmitOMPLinearClauseInit(const OMPLoopDirective &D);

  typedef const llvm::function_ref<void(CodeGenFunction & /*CGF*/,
                                        llvm::Function * /*OutlinedFn*/,
                                        const OMPTaskDataTy & /*Data*/)>
      TaskGenTy;
  void EmitOMPTaskBasedDirective(const OMPExecutableDirective &S,
                                 const OpenMPDirectiveKind CapturedRegion,
                                 const RegionCodeGenTy &BodyGen,
                                 const TaskGenTy &TaskGen, OMPTaskDataTy &Data);
  struct OMPTargetDataInfo {
    Address BasePointersArray = Address::invalid();
    Address PointersArray = Address::invalid();
    Address SizesArray = Address::invalid();
    Address MappersArray = Address::invalid();
    unsigned NumberOfTargetItems = 0;
    explicit OMPTargetDataInfo() = default;
    OMPTargetDataInfo(Address BasePointersArray, Address PointersArray,
                      Address SizesArray, Address MappersArray,
                      unsigned NumberOfTargetItems)
        : BasePointersArray(BasePointersArray), PointersArray(PointersArray),
          SizesArray(SizesArray), MappersArray(MappersArray),
          NumberOfTargetItems(NumberOfTargetItems) {}
  };
  void EmitOMPTargetTaskBasedDirective(const OMPExecutableDirective &S,
                                       const RegionCodeGenTy &BodyGen,
                                       OMPTargetDataInfo &InputInfo);
  void processInReduction(const OMPExecutableDirective &S,
                          OMPTaskDataTy &Data,
                          CodeGenFunction &CGF,
                          const CapturedStmt *CS,
                          OMPPrivateScope &Scope);
  void EmitOMPMetaDirective(const OMPMetaDirective &S);
  void EmitOMPParallelDirective(const OMPParallelDirective &S);
  void EmitOMPSimdDirective(const OMPSimdDirective &S);
  void EmitOMPTileDirective(const OMPTileDirective &S);
  void EmitOMPUnrollDirective(const OMPUnrollDirective &S);
  void EmitOMPReverseDirective(const OMPReverseDirective &S);
  void EmitOMPInterchangeDirective(const OMPInterchangeDirective &S);
  void EmitOMPForDirective(const OMPForDirective &S);
  void EmitOMPForSimdDirective(const OMPForSimdDirective &S);
  void EmitOMPSectionsDirective(const OMPSectionsDirective &S);
  void EmitOMPSectionDirective(const OMPSectionDirective &S);
  void EmitOMPSingleDirective(const OMPSingleDirective &S);
  void EmitOMPMasterDirective(const OMPMasterDirective &S);
  void EmitOMPMaskedDirective(const OMPMaskedDirective &S);
  void EmitOMPCriticalDirective(const OMPCriticalDirective &S);
  void EmitOMPParallelForDirective(const OMPParallelForDirective &S);
  void EmitOMPParallelForSimdDirective(const OMPParallelForSimdDirective &S);
  void EmitOMPParallelSectionsDirective(const OMPParallelSectionsDirective &S);
  void EmitOMPParallelMasterDirective(const OMPParallelMasterDirective &S);
  void EmitOMPTaskDirective(const OMPTaskDirective &S);
  void EmitOMPTaskyieldDirective(const OMPTaskyieldDirective &S);
  void EmitOMPErrorDirective(const OMPErrorDirective &S);
  void EmitOMPBarrierDirective(const OMPBarrierDirective &S);
  void EmitOMPTaskwaitDirective(const OMPTaskwaitDirective &S);
  void EmitOMPTaskgroupDirective(const OMPTaskgroupDirective &S);
  void EmitOMPFlushDirective(const OMPFlushDirective &S);
  void EmitOMPDepobjDirective(const OMPDepobjDirective &S);
  void EmitOMPScanDirective(const OMPScanDirective &S);
  void EmitOMPOrderedDirective(const OMPOrderedDirective &S);
  void EmitOMPAtomicDirective(const OMPAtomicDirective &S);
  void EmitOMPTargetDirective(const OMPTargetDirective &S);
  void EmitOMPTargetDataDirective(const OMPTargetDataDirective &S);
  void EmitOMPTargetEnterDataDirective(const OMPTargetEnterDataDirective &S);
  void EmitOMPTargetExitDataDirective(const OMPTargetExitDataDirective &S);
  void EmitOMPTargetUpdateDirective(const OMPTargetUpdateDirective &S);
  void EmitOMPTargetParallelDirective(const OMPTargetParallelDirective &S);
  void
  EmitOMPTargetParallelForDirective(const OMPTargetParallelForDirective &S);
  void EmitOMPTeamsDirective(const OMPTeamsDirective &S);
  void
  EmitOMPCancellationPointDirective(const OMPCancellationPointDirective &S);
  void EmitOMPCancelDirective(const OMPCancelDirective &S);
  void EmitOMPTaskLoopBasedDirective(const OMPLoopDirective &S);
  void EmitOMPTaskLoopDirective(const OMPTaskLoopDirective &S);
  void EmitOMPTaskLoopSimdDirective(const OMPTaskLoopSimdDirective &S);
  void EmitOMPMasterTaskLoopDirective(const OMPMasterTaskLoopDirective &S);
  void
  EmitOMPMasterTaskLoopSimdDirective(const OMPMasterTaskLoopSimdDirective &S);
  void EmitOMPParallelMasterTaskLoopDirective(
      const OMPParallelMasterTaskLoopDirective &S);
  void EmitOMPParallelMasterTaskLoopSimdDirective(
      const OMPParallelMasterTaskLoopSimdDirective &S);
  void EmitOMPDistributeDirective(const OMPDistributeDirective &S);
  void EmitOMPDistributeParallelForDirective(
      const OMPDistributeParallelForDirective &S);
  void EmitOMPDistributeParallelForSimdDirective(
      const OMPDistributeParallelForSimdDirective &S);
  void EmitOMPDistributeSimdDirective(const OMPDistributeSimdDirective &S);
  void EmitOMPTargetParallelForSimdDirective(
      const OMPTargetParallelForSimdDirective &S);
  void EmitOMPTargetSimdDirective(const OMPTargetSimdDirective &S);
  void EmitOMPTeamsDistributeDirective(const OMPTeamsDistributeDirective &S);
  void
  EmitOMPTeamsDistributeSimdDirective(const OMPTeamsDistributeSimdDirective &S);
  void EmitOMPTeamsDistributeParallelForSimdDirective(
      const OMPTeamsDistributeParallelForSimdDirective &S);
  void EmitOMPTeamsDistributeParallelForDirective(
      const OMPTeamsDistributeParallelForDirective &S);
  void EmitOMPTargetTeamsDirective(const OMPTargetTeamsDirective &S);
  void EmitOMPTargetTeamsDistributeDirective(
      const OMPTargetTeamsDistributeDirective &S);
  void EmitOMPTargetTeamsDistributeParallelForDirective(
      const OMPTargetTeamsDistributeParallelForDirective &S);
  void EmitOMPTargetTeamsDistributeParallelForSimdDirective(
      const OMPTargetTeamsDistributeParallelForSimdDirective &S);
  void EmitOMPTargetTeamsDistributeSimdDirective(
      const OMPTargetTeamsDistributeSimdDirective &S);
  void EmitOMPGenericLoopDirective(const OMPGenericLoopDirective &S);
  void EmitOMPParallelGenericLoopDirective(const OMPLoopDirective &S);
  void EmitOMPTargetParallelGenericLoopDirective(
      const OMPTargetParallelGenericLoopDirective &S);
  void EmitOMPTargetTeamsGenericLoopDirective(
      const OMPTargetTeamsGenericLoopDirective &S);
  void EmitOMPTeamsGenericLoopDirective(const OMPTeamsGenericLoopDirective &S);
  void EmitOMPInteropDirective(const OMPInteropDirective &S);
  void EmitOMPParallelMaskedDirective(const OMPParallelMaskedDirective &S);

  /// Emit device code for the target directive.
  static void EmitOMPTargetDeviceFunction(CodeGenModule &CGM,
                                          StringRef ParentName,
                                          const OMPTargetDirective &S);
  static void
  EmitOMPTargetParallelDeviceFunction(CodeGenModule &CGM, StringRef ParentName,
                                      const OMPTargetParallelDirective &S);
  /// Emit device code for the target parallel for directive.
  static void EmitOMPTargetParallelForDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetParallelForDirective &S);
  /// Emit device code for the target parallel for simd directive.
  static void EmitOMPTargetParallelForSimdDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetParallelForSimdDirective &S);
  /// Emit device code for the target teams directive.
  static void
  EmitOMPTargetTeamsDeviceFunction(CodeGenModule &CGM, StringRef ParentName,
                                   const OMPTargetTeamsDirective &S);
  /// Emit device code for the target teams distribute directive.
  static void EmitOMPTargetTeamsDistributeDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetTeamsDistributeDirective &S);
  /// Emit device code for the target teams distribute simd directive.
  static void EmitOMPTargetTeamsDistributeSimdDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetTeamsDistributeSimdDirective &S);
  /// Emit device code for the target simd directive.
  static void EmitOMPTargetSimdDeviceFunction(CodeGenModule &CGM,
                                              StringRef ParentName,
                                              const OMPTargetSimdDirective &S);
  /// Emit device code for the target teams distribute parallel for simd
  /// directive.
  static void EmitOMPTargetTeamsDistributeParallelForSimdDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetTeamsDistributeParallelForSimdDirective &S);

  /// Emit device code for the target teams loop directive.
  static void EmitOMPTargetTeamsGenericLoopDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetTeamsGenericLoopDirective &S);

  /// Emit device code for the target parallel loop directive.
  static void EmitOMPTargetParallelGenericLoopDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetParallelGenericLoopDirective &S);

  static void EmitOMPTargetTeamsDistributeParallelForDeviceFunction(
      CodeGenModule &CGM, StringRef ParentName,
      const OMPTargetTeamsDistributeParallelForDirective &S);

  /// Emit the Stmt \p S and return its topmost canonical loop, if any.
  /// TODO: The \p Depth paramter is not yet implemented and must be 1. In the
  /// future it is meant to be the number of loops expected in the loop nests
  /// (usually specified by the "collapse" clause) that are collapsed to a
  /// single loop by this function.
  llvm::CanonicalLoopInfo *EmitOMPCollapsedCanonicalLoopNest(const Stmt *S,
                                                             int Depth);

  /// Emit an OMPCanonicalLoop using the OpenMPIRBuilder.
  void EmitOMPCanonicalLoop(const OMPCanonicalLoop *S);

  /// Emit inner loop of the worksharing/simd construct.
  ///
  /// \param S Directive, for which the inner loop must be emitted.
  /// \param RequiresCleanup true, if directive has some associated private
  /// variables.
  /// \param LoopCond Bollean condition for loop continuation.
  /// \param IncExpr Increment expression for loop control variable.
  /// \param BodyGen Generator for the inner body of the inner loop.
  /// \param PostIncGen Genrator for post-increment code (required for ordered
  /// loop directvies).
  void EmitOMPInnerLoop(
      const OMPExecutableDirective &S, bool RequiresCleanup,
      const Expr *LoopCond, const Expr *IncExpr,
      const llvm::function_ref<void(CodeGenFunction &)> BodyGen,
      const llvm::function_ref<void(CodeGenFunction &)> PostIncGen);

  JumpDest getOMPCancelDestination(OpenMPDirectiveKind Kind);
  /// Emit initial code for loop counters of loop-based directives.
  void EmitOMPPrivateLoopCounters(const OMPLoopDirective &S,
                                  OMPPrivateScope &LoopScope);

  /// Helper for the OpenMP loop directives.
  void EmitOMPLoopBody(const OMPLoopDirective &D, JumpDest LoopExit);

  /// Emit code for the worksharing loop-based directive.
  /// \return true, if this construct has any lastprivate clause, false -
  /// otherwise.
  bool EmitOMPWorksharingLoop(const OMPLoopDirective &S, Expr *EUB,
                              const CodeGenLoopBoundsTy &CodeGenLoopBounds,
                              const CodeGenDispatchBoundsTy &CGDispatchBounds);

  /// Emit code for the distribute loop-based directive.
  void EmitOMPDistributeLoop(const OMPLoopDirective &S,
                             const CodeGenLoopTy &CodeGenLoop, Expr *IncExpr);

  /// Helpers for the OpenMP loop directives.
  void EmitOMPSimdInit(const OMPLoopDirective &D);
  void EmitOMPSimdFinal(
      const OMPLoopDirective &D,
      const llvm::function_ref<llvm::Value *(CodeGenFunction &)> CondGen);

  /// Emits the lvalue for the expression with possibly captured variable.
  LValue EmitOMPSharedLValue(const Expr *E);

private:
  /// Helpers for blocks.
  llvm::Value *EmitBlockLiteral(const CGBlockInfo &Info);

  /// struct with the values to be passed to the OpenMP loop-related functions
  struct OMPLoopArguments {
    /// loop lower bound
    Address LB = Address::invalid();
    /// loop upper bound
    Address UB = Address::invalid();
    /// loop stride
    Address ST = Address::invalid();
    /// isLastIteration argument for runtime functions
    Address IL = Address::invalid();
    /// Chunk value generated by sema
    llvm::Value *Chunk = nullptr;
    /// EnsureUpperBound
    Expr *EUB = nullptr;
    /// IncrementExpression
    Expr *IncExpr = nullptr;
    /// Loop initialization
    Expr *Init = nullptr;
    /// Loop exit condition
    Expr *Cond = nullptr;
    /// Update of LB after a whole chunk has been executed
    Expr *NextLB = nullptr;
    /// Update of UB after a whole chunk has been executed
    Expr *NextUB = nullptr;
    /// Distinguish between the for distribute and sections
    OpenMPDirectiveKind DKind = llvm::omp::OMPD_unknown;
    OMPLoopArguments() = default;
    OMPLoopArguments(Address LB, Address UB, Address ST, Address IL,
                     llvm::Value *Chunk = nullptr, Expr *EUB = nullptr,
                     Expr *IncExpr = nullptr, Expr *Init = nullptr,
                     Expr *Cond = nullptr, Expr *NextLB = nullptr,
                     Expr *NextUB = nullptr)
        : LB(LB), UB(UB), ST(ST), IL(IL), Chunk(Chunk), EUB(EUB),
          IncExpr(IncExpr), Init(Init), Cond(Cond), NextLB(NextLB),
          NextUB(NextUB) {}
  };
  void EmitOMPOuterLoop(bool DynamicOrOrdered, bool IsMonotonic,
                        const OMPLoopDirective &S, OMPPrivateScope &LoopScope,
                        const OMPLoopArguments &LoopArgs,
                        const CodeGenLoopTy &CodeGenLoop,
                        const CodeGenOrderedTy &CodeGenOrdered);
  void EmitOMPForOuterLoop(const OpenMPScheduleTy &ScheduleKind,
                           bool IsMonotonic, const OMPLoopDirective &S,
                           OMPPrivateScope &LoopScope, bool Ordered,
                           const OMPLoopArguments &LoopArgs,
                           const CodeGenDispatchBoundsTy &CGDispatchBounds);
  void EmitOMPDistributeOuterLoop(OpenMPDistScheduleClauseKind ScheduleKind,
                                  const OMPLoopDirective &S,
                                  OMPPrivateScope &LoopScope,
                                  const OMPLoopArguments &LoopArgs,
                                  const CodeGenLoopTy &CodeGenLoopContent);
  /// Emit code for sections directive.
  void EmitSections(const OMPExecutableDirective &S);

public:
  //===--------------------------------------------------------------------===//
  //                         OpenACC Emission
  //===--------------------------------------------------------------------===//
  void EmitOpenACCComputeConstruct(const OpenACCComputeConstruct &S) {
    // TODO OpenACC: Implement this.  It is currently implemented as a 'no-op',
    // simply emitting its structured block, but in the future we will implement
    // some sort of IR.
    EmitStmt(S.getStructuredBlock());
  }

  void EmitOpenACCLoopConstruct(const OpenACCLoopConstruct &S) {
    // TODO OpenACC: Implement this.  It is currently implemented as a 'no-op',
    // simply emitting its loop, but in the future we will implement
    // some sort of IR.
    EmitStmt(S.getLoop());
  }

  //===--------------------------------------------------------------------===//
  //                         LValue Expression Emission
  //===--------------------------------------------------------------------===//

  /// Create a check that a scalar RValue is non-null.
  llvm::Value *EmitNonNullRValueCheck(RValue RV, QualType T);

  /// GetUndefRValue - Get an appropriate 'undef' rvalue for the given type.
  RValue GetUndefRValue(QualType Ty);

  /// EmitUnsupportedRValue - Emit a dummy r-value using the type of E
  /// and issue an ErrorUnsupported style diagnostic (using the
  /// provided Name).
  RValue EmitUnsupportedRValue(const Expr *E,
                               const char *Name);

  /// EmitUnsupportedLValue - Emit a dummy l-value using the type of E and issue
  /// an ErrorUnsupported style diagnostic (using the provided Name).
  LValue EmitUnsupportedLValue(const Expr *E,
                               const char *Name);

  /// EmitLValue - Emit code to compute a designator that specifies the location
  /// of the expression.
  ///
  /// This can return one of two things: a simple address or a bitfield
  /// reference.  In either case, the LLVM Value* in the LValue structure is
  /// guaranteed to be an LLVM pointer type.
  ///
  /// If this returns a bitfield reference, nothing about the pointee type of
  /// the LLVM value is known: For example, it may not be a pointer to an
  /// integer.
  ///
  /// If this returns a normal address, and if the lvalue's C type is fixed
  /// size, this method guarantees that the returned pointer type will point to
  /// an LLVM type of the same size of the lvalue's type.  If the lvalue has a
  /// variable length type, this is not possible.
  ///
  LValue EmitLValue(const Expr *E,
                    KnownNonNull_t IsKnownNonNull = NotKnownNonNull);

private:
  LValue EmitLValueHelper(const Expr *E, KnownNonNull_t IsKnownNonNull);

public:
  /// Same as EmitLValue but additionally we generate checking code to
  /// guard against undefined behavior.  This is only suitable when we know
  /// that the address will be used to access the object.
  LValue EmitCheckedLValue(const Expr *E, TypeCheckKind TCK);

  RValue convertTempToRValue(Address addr, QualType type,
                             SourceLocation Loc);

  void EmitAtomicInit(Expr *E, LValue lvalue);

  bool LValueIsSuitableForInlineAtomic(LValue Src);

  RValue EmitAtomicLoad(LValue LV, SourceLocation SL,
                        AggValueSlot Slot = AggValueSlot::ignored());

  RValue EmitAtomicLoad(LValue lvalue, SourceLocation loc,
                        llvm::AtomicOrdering AO, bool IsVolatile = false,
                        AggValueSlot slot = AggValueSlot::ignored());

  void EmitAtomicStore(RValue rvalue, LValue lvalue, bool isInit);

  void EmitAtomicStore(RValue rvalue, LValue lvalue, llvm::AtomicOrdering AO,
                       bool IsVolatile, bool isInit);

  std::pair<RValue, llvm::Value *> EmitAtomicCompareExchange(
      LValue Obj, RValue Expected, RValue Desired, SourceLocation Loc,
      llvm::AtomicOrdering Success =
          llvm::AtomicOrdering::SequentiallyConsistent,
      llvm::AtomicOrdering Failure =
          llvm::AtomicOrdering::SequentiallyConsistent,
      bool IsWeak = false, AggValueSlot Slot = AggValueSlot::ignored());

  void EmitAtomicUpdate(LValue LVal, llvm::AtomicOrdering AO,
                        const llvm::function_ref<RValue(RValue)> &UpdateOp,
                        bool IsVolatile);

  /// EmitToMemory - Change a scalar value from its value
  /// representation to its in-memory representation.
  llvm::Value *EmitToMemory(llvm::Value *Value, QualType Ty);

  /// EmitFromMemory - Change a scalar value from its memory
  /// representation to its value representation.
  llvm::Value *EmitFromMemory(llvm::Value *Value, QualType Ty);

  /// Check if the scalar \p Value is within the valid range for the given
  /// type \p Ty.
  ///
  /// Returns true if a check is needed (even if the range is unknown).
  bool EmitScalarRangeCheck(llvm::Value *Value, QualType Ty,
                            SourceLocation Loc);

  /// EmitLoadOfScalar - Load a scalar value from an address, taking
  /// care to appropriately convert from the memory representation to
  /// the LLVM value representation.
  llvm::Value *EmitLoadOfScalar(Address Addr, bool Volatile, QualType Ty,
                                SourceLocation Loc,
                                AlignmentSource Source = AlignmentSource::Type,
                                bool isNontemporal = false) {
    return EmitLoadOfScalar(Addr, Volatile, Ty, Loc, LValueBaseInfo(Source),
                            CGM.getTBAAAccessInfo(Ty), isNontemporal);
  }

  llvm::Value *EmitLoadOfScalar(Address Addr, bool Volatile, QualType Ty,
                                SourceLocation Loc, LValueBaseInfo BaseInfo,
                                TBAAAccessInfo TBAAInfo,
                                bool isNontemporal = false);

  /// EmitLoadOfScalar - Load a scalar value from an address, taking
  /// care to appropriately convert from the memory representation to
  /// the LLVM value representation.  The l-value must be a simple
  /// l-value.
  llvm::Value *EmitLoadOfScalar(LValue lvalue, SourceLocation Loc);

  /// EmitStoreOfScalar - Store a scalar value to an address, taking
  /// care to appropriately convert from the memory representation to
  /// the LLVM value representation.
  void EmitStoreOfScalar(llvm::Value *Value, Address Addr,
                         bool Volatile, QualType Ty,
                         AlignmentSource Source = AlignmentSource::Type,
                         bool isInit = false, bool isNontemporal = false) {
    EmitStoreOfScalar(Value, Addr, Volatile, Ty, LValueBaseInfo(Source),
                      CGM.getTBAAAccessInfo(Ty), isInit, isNontemporal);
  }

  void EmitStoreOfScalar(llvm::Value *Value, Address Addr,
                         bool Volatile, QualType Ty,
                         LValueBaseInfo BaseInfo, TBAAAccessInfo TBAAInfo,
                         bool isInit = false, bool isNontemporal = false);

  /// EmitStoreOfScalar - Store a scalar value to an address, taking
  /// care to appropriately convert from the memory representation to
  /// the LLVM value representation.  The l-value must be a simple
  /// l-value.  The isInit flag indicates whether this is an initialization.
  /// If so, atomic qualifiers are ignored and the store is always non-atomic.
  void EmitStoreOfScalar(llvm::Value *value, LValue lvalue, bool isInit=false);

  /// EmitLoadOfLValue - Given an expression that represents a value lvalue,
  /// this method emits the address of the lvalue, then loads the result as an
  /// rvalue, returning the rvalue.
  RValue EmitLoadOfLValue(LValue V, SourceLocation Loc);
  RValue EmitLoadOfExtVectorElementLValue(LValue V);
  RValue EmitLoadOfBitfieldLValue(LValue LV, SourceLocation Loc);
  RValue EmitLoadOfGlobalRegLValue(LValue LV);

  /// Like EmitLoadOfLValue but also handles complex and aggregate types.
  RValue EmitLoadOfAnyValue(LValue V,
                            AggValueSlot Slot = AggValueSlot::ignored(),
                            SourceLocation Loc = {});

  /// EmitStoreThroughLValue - Store the specified rvalue into the specified
  /// lvalue, where both are guaranteed to the have the same type, and that type
  /// is 'Ty'.
  void EmitStoreThroughLValue(RValue Src, LValue Dst, bool isInit = false);
  void EmitStoreThroughExtVectorComponentLValue(RValue Src, LValue Dst);
  void EmitStoreThroughGlobalRegLValue(RValue Src, LValue Dst);

  /// EmitStoreThroughBitfieldLValue - Store Src into Dst with same constraints
  /// as EmitStoreThroughLValue.
  ///
  /// \param Result [out] - If non-null, this will be set to a Value* for the
  /// bit-field contents after the store, appropriate for use as the result of
  /// an assignment to the bit-field.
  void EmitStoreThroughBitfieldLValue(RValue Src, LValue Dst,
                                      llvm::Value **Result=nullptr);

  /// Emit an l-value for an assignment (simple or compound) of complex type.
  LValue EmitComplexAssignmentLValue(const BinaryOperator *E);
  LValue EmitComplexCompoundAssignmentLValue(const CompoundAssignOperator *E);
  LValue EmitScalarCompoundAssignWithComplex(const CompoundAssignOperator *E,
                                             llvm::Value *&Result);

  // Note: only available for agg return types
  LValue EmitBinaryOperatorLValue(const BinaryOperator *E);
  LValue EmitCompoundAssignmentLValue(const CompoundAssignOperator *E);
  // Note: only available for agg return types
  LValue EmitCallExprLValue(const CallExpr *E);
  // Note: only available for agg return types
  LValue EmitVAArgExprLValue(const VAArgExpr *E);
  LValue EmitDeclRefLValue(const DeclRefExpr *E);
  LValue EmitStringLiteralLValue(const StringLiteral *E);
  LValue EmitObjCEncodeExprLValue(const ObjCEncodeExpr *E);
  LValue EmitPredefinedLValue(const PredefinedExpr *E);
  LValue EmitUnaryOpLValue(const UnaryOperator *E);
  LValue EmitArraySubscriptExpr(const ArraySubscriptExpr *E,
                                bool Accessed = false);
  LValue EmitMatrixSubscriptExpr(const MatrixSubscriptExpr *E);
  LValue EmitArraySectionExpr(const ArraySectionExpr *E,
                              bool IsLowerBound = true);
  LValue EmitExtVectorElementExpr(const ExtVectorElementExpr *E);
  LValue EmitMemberExpr(const MemberExpr *E);
  LValue EmitObjCIsaExpr(const ObjCIsaExpr *E);
  LValue EmitCompoundLiteralLValue(const CompoundLiteralExpr *E);
  LValue EmitInitListLValue(const InitListExpr *E);
  void EmitIgnoredConditionalOperator(const AbstractConditionalOperator *E);
  LValue EmitConditionalOperatorLValue(const AbstractConditionalOperator *E);
  LValue EmitCastLValue(const CastExpr *E);
  LValue EmitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *E);
  LValue EmitOpaqueValueLValue(const OpaqueValueExpr *e);

  Address EmitExtVectorElementLValue(LValue V);

  RValue EmitRValueForField(LValue LV, const FieldDecl *FD, SourceLocation Loc);

  Address EmitArrayToPointerDecay(const Expr *Array,
                                  LValueBaseInfo *BaseInfo = nullptr,
                                  TBAAAccessInfo *TBAAInfo = nullptr);

  class ConstantEmission {
    llvm::PointerIntPair<llvm::Constant*, 1, bool> ValueAndIsReference;
    ConstantEmission(llvm::Constant *C, bool isReference)
      : ValueAndIsReference(C, isReference) {}
  public:
    ConstantEmission() {}
    static ConstantEmission forReference(llvm::Constant *C) {
      return ConstantEmission(C, true);
    }
    static ConstantEmission forValue(llvm::Constant *C) {
      return ConstantEmission(C, false);
    }

    explicit operator bool() const {
      return ValueAndIsReference.getOpaqueValue() != nullptr;
    }

    bool isReference() const { return ValueAndIsReference.getInt(); }
    LValue getReferenceLValue(CodeGenFunction &CGF, Expr *refExpr) const {
      assert(isReference());
      return CGF.MakeNaturalAlignAddrLValue(ValueAndIsReference.getPointer(),
                                            refExpr->getType());
    }

    llvm::Constant *getValue() const {
      assert(!isReference());
      return ValueAndIsReference.getPointer();
    }
  };

  ConstantEmission tryEmitAsConstant(DeclRefExpr *refExpr);
  ConstantEmission tryEmitAsConstant(const MemberExpr *ME);
  llvm::Value *emitScalarConstant(const ConstantEmission &Constant, Expr *E);

  RValue EmitPseudoObjectRValue(const PseudoObjectExpr *e,
                                AggValueSlot slot = AggValueSlot::ignored());
  LValue EmitPseudoObjectLValue(const PseudoObjectExpr *e);

  llvm::Value *EmitIvarOffset(const ObjCInterfaceDecl *Interface,
                              const ObjCIvarDecl *Ivar);
  llvm::Value *EmitIvarOffsetAsPointerDiff(const ObjCInterfaceDecl *Interface,
                                           const ObjCIvarDecl *Ivar);
  LValue EmitLValueForField(LValue Base, const FieldDecl* Field);
  LValue EmitLValueForLambdaField(const FieldDecl *Field);
  LValue EmitLValueForLambdaField(const FieldDecl *Field,
                                  llvm::Value *ThisValue);

  /// EmitLValueForFieldInitialization - Like EmitLValueForField, except that
  /// if the Field is a reference, this will return the address of the reference
  /// and not the address of the value stored in the reference.
  LValue EmitLValueForFieldInitialization(LValue Base,
                                          const FieldDecl* Field);

  LValue EmitLValueForIvar(QualType ObjectTy,
                           llvm::Value* Base, const ObjCIvarDecl *Ivar,
                           unsigned CVRQualifiers);

  LValue EmitCXXConstructLValue(const CXXConstructExpr *E);
  LValue EmitCXXBindTemporaryLValue(const CXXBindTemporaryExpr *E);
  LValue EmitCXXTypeidLValue(const CXXTypeidExpr *E);
  LValue EmitCXXUuidofLValue(const CXXUuidofExpr *E);

  LValue EmitObjCMessageExprLValue(const ObjCMessageExpr *E);
  LValue EmitObjCIvarRefLValue(const ObjCIvarRefExpr *E);
  LValue EmitStmtExprLValue(const StmtExpr *E);
  LValue EmitPointerToDataMemberBinaryExpr(const BinaryOperator *E);
  LValue EmitObjCSelectorLValue(const ObjCSelectorExpr *E);
  void   EmitDeclRefExprDbgValue(const DeclRefExpr *E, const APValue &Init);

  //===--------------------------------------------------------------------===//
  //                         Scalar Expression Emission
  //===--------------------------------------------------------------------===//

  /// EmitCall - Generate a call of the given function, expecting the given
  /// result type, and using the given argument list which specifies both the
  /// LLVM arguments and the types they were derived from.
  RValue EmitCall(const CGFunctionInfo &CallInfo, const CGCallee &Callee,
                  ReturnValueSlot ReturnValue, const CallArgList &Args,
                  llvm::CallBase **callOrInvoke, bool IsMustTail,
                  SourceLocation Loc,
                  bool IsVirtualFunctionPointerThunk = false);
  RValue EmitCall(const CGFunctionInfo &CallInfo, const CGCallee &Callee,
                  ReturnValueSlot ReturnValue, const CallArgList &Args,
                  llvm::CallBase **callOrInvoke = nullptr,
                  bool IsMustTail = false) {
    return EmitCall(CallInfo, Callee, ReturnValue, Args, callOrInvoke,
                    IsMustTail, SourceLocation());
  }
  RValue EmitCall(QualType FnType, const CGCallee &Callee, const CallExpr *E,
                  ReturnValueSlot ReturnValue, llvm::Value *Chain = nullptr);
  RValue EmitCallExpr(const CallExpr *E,
                      ReturnValueSlot ReturnValue = ReturnValueSlot());
  RValue EmitSimpleCallExpr(const CallExpr *E, ReturnValueSlot ReturnValue);
  CGCallee EmitCallee(const Expr *E);

  void checkTargetFeatures(const CallExpr *E, const FunctionDecl *TargetDecl);
  void checkTargetFeatures(SourceLocation Loc, const FunctionDecl *TargetDecl);

  llvm::CallInst *EmitRuntimeCall(llvm::FunctionCallee callee,
                                  const Twine &name = "");
  llvm::CallInst *EmitRuntimeCall(llvm::FunctionCallee callee,
                                  ArrayRef<llvm::Value *> args,
                                  const Twine &name = "");
  llvm::CallInst *EmitNounwindRuntimeCall(llvm::FunctionCallee callee,
                                          const Twine &name = "");
  llvm::CallInst *EmitNounwindRuntimeCall(llvm::FunctionCallee callee,
                                          ArrayRef<Address> args,
                                          const Twine &name = "");
  llvm::CallInst *EmitNounwindRuntimeCall(llvm::FunctionCallee callee,
                                          ArrayRef<llvm::Value *> args,
                                          const Twine &name = "");

  SmallVector<llvm::OperandBundleDef, 1>
  getBundlesForFunclet(llvm::Value *Callee);

  llvm::CallBase *EmitCallOrInvoke(llvm::FunctionCallee Callee,
                                   ArrayRef<llvm::Value *> Args,
                                   const Twine &Name = "");
  llvm::CallBase *EmitRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                          ArrayRef<llvm::Value *> args,
                                          const Twine &name = "");
  llvm::CallBase *EmitRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                          const Twine &name = "");
  void EmitNoreturnRuntimeCallOrInvoke(llvm::FunctionCallee callee,
                                       ArrayRef<llvm::Value *> args);

  CGCallee BuildAppleKextVirtualCall(const CXXMethodDecl *MD,
                                     NestedNameSpecifier *Qual,
                                     llvm::Type *Ty);

  CGCallee BuildAppleKextVirtualDestructorCall(const CXXDestructorDecl *DD,
                                               CXXDtorType Type,
                                               const CXXRecordDecl *RD);

  bool isPointerKnownNonNull(const Expr *E);

  /// Create the discriminator from the storage address and the entity hash.
  llvm::Value *EmitPointerAuthBlendDiscriminator(llvm::Value *StorageAddress,
                                                 llvm::Value *Discriminator);
  CGPointerAuthInfo EmitPointerAuthInfo(const PointerAuthSchema &Schema,
                                        llvm::Value *StorageAddress,
                                        GlobalDecl SchemaDecl,
                                        QualType SchemaType);

  llvm::Value *EmitPointerAuthSign(const CGPointerAuthInfo &Info,
                                   llvm::Value *Pointer);

  llvm::Value *EmitPointerAuthAuth(const CGPointerAuthInfo &Info,
                                   llvm::Value *Pointer);

  llvm::Value *emitPointerAuthResign(llvm::Value *Pointer, QualType PointerType,
                                     const CGPointerAuthInfo &CurAuthInfo,
                                     const CGPointerAuthInfo &NewAuthInfo,
                                     bool IsKnownNonNull);
  llvm::Value *emitPointerAuthResignCall(llvm::Value *Pointer,
                                         const CGPointerAuthInfo &CurInfo,
                                         const CGPointerAuthInfo &NewInfo);

  void EmitPointerAuthOperandBundle(
      const CGPointerAuthInfo &Info,
      SmallVectorImpl<llvm::OperandBundleDef> &Bundles);

  llvm::Value *authPointerToPointerCast(llvm::Value *ResultPtr,
                                        QualType SourceType, QualType DestType);
  Address authPointerToPointerCast(Address Ptr, QualType SourceType,
                                   QualType DestType);

  Address getAsNaturalAddressOf(Address Addr, QualType PointeeTy);

  llvm::Value *getAsNaturalPointerTo(Address Addr, QualType PointeeType) {
    return getAsNaturalAddressOf(Addr, PointeeType).getBasePointer();
  }

  // Return the copy constructor name with the prefix "__copy_constructor_"
  // removed.
  static std::string getNonTrivialCopyConstructorStr(QualType QT,
                                                     CharUnits Alignment,
                                                     bool IsVolatile,
                                                     ASTContext &Ctx);

  // Return the destructor name with the prefix "__destructor_" removed.
  static std::string getNonTrivialDestructorStr(QualType QT,
                                                CharUnits Alignment,
                                                bool IsVolatile,
                                                ASTContext &Ctx);

  // These functions emit calls to the special functions of non-trivial C
  // structs.
  void defaultInitNonTrivialCStructVar(LValue Dst);
  void callCStructDefaultConstructor(LValue Dst);
  void callCStructDestructor(LValue Dst);
  void callCStructCopyConstructor(LValue Dst, LValue Src);
  void callCStructMoveConstructor(LValue Dst, LValue Src);
  void callCStructCopyAssignmentOperator(LValue Dst, LValue Src);
  void callCStructMoveAssignmentOperator(LValue Dst, LValue Src);

  RValue
  EmitCXXMemberOrOperatorCall(const CXXMethodDecl *Method,
                              const CGCallee &Callee,
                              ReturnValueSlot ReturnValue, llvm::Value *This,
                              llvm::Value *ImplicitParam,
                              QualType ImplicitParamTy, const CallExpr *E,
                              CallArgList *RtlArgs);
  RValue EmitCXXDestructorCall(GlobalDecl Dtor, const CGCallee &Callee,
                               llvm::Value *This, QualType ThisTy,
                               llvm::Value *ImplicitParam,
                               QualType ImplicitParamTy, const CallExpr *E);
  RValue EmitCXXMemberCallExpr(const CXXMemberCallExpr *E,
                               ReturnValueSlot ReturnValue);
  RValue EmitCXXMemberOrOperatorMemberCallExpr(const CallExpr *CE,
                                               const CXXMethodDecl *MD,
                                               ReturnValueSlot ReturnValue,
                                               bool HasQualifier,
                                               NestedNameSpecifier *Qualifier,
                                               bool IsArrow, const Expr *Base);
  // Compute the object pointer.
  Address EmitCXXMemberDataPointerAddress(const Expr *E, Address base,
                                          llvm::Value *memberPtr,
                                          const MemberPointerType *memberPtrType,
                                          LValueBaseInfo *BaseInfo = nullptr,
                                          TBAAAccessInfo *TBAAInfo = nullptr);
  RValue EmitCXXMemberPointerCallExpr(const CXXMemberCallExpr *E,
                                      ReturnValueSlot ReturnValue);

  RValue EmitCXXOperatorMemberCallExpr(const CXXOperatorCallExpr *E,
                                       const CXXMethodDecl *MD,
                                       ReturnValueSlot ReturnValue);
  RValue EmitCXXPseudoDestructorExpr(const CXXPseudoDestructorExpr *E);

  RValue EmitCUDAKernelCallExpr(const CUDAKernelCallExpr *E,
                                ReturnValueSlot ReturnValue);

  RValue EmitNVPTXDevicePrintfCallExpr(const CallExpr *E);
  RValue EmitAMDGPUDevicePrintfCallExpr(const CallExpr *E);
  RValue EmitOpenMPDevicePrintfCallExpr(const CallExpr *E);

  RValue EmitBuiltinExpr(const GlobalDecl GD, unsigned BuiltinID,
                         const CallExpr *E, ReturnValueSlot ReturnValue);

  RValue emitRotate(const CallExpr *E, bool IsRotateRight);

  /// Emit IR for __builtin_os_log_format.
  RValue emitBuiltinOSLogFormat(const CallExpr &E);

  /// Emit IR for __builtin_is_aligned.
  RValue EmitBuiltinIsAligned(const CallExpr *E);
  /// Emit IR for __builtin_align_up/__builtin_align_down.
  RValue EmitBuiltinAlignTo(const CallExpr *E, bool AlignUp);

  llvm::Function *generateBuiltinOSLogHelperFunction(
      const analyze_os_log::OSLogBufferLayout &Layout,
      CharUnits BufferAlignment);

  RValue EmitBlockCallExpr(const CallExpr *E, ReturnValueSlot ReturnValue);

  /// EmitTargetBuiltinExpr - Emit the given builtin call. Returns 0 if the call
  /// is unhandled by the current target.
  llvm::Value *EmitTargetBuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                     ReturnValueSlot ReturnValue);

  llvm::Value *EmitAArch64CompareBuiltinExpr(llvm::Value *Op, llvm::Type *Ty,
                                             const llvm::CmpInst::Predicate Fp,
                                             const llvm::CmpInst::Predicate Ip,
                                             const llvm::Twine &Name = "");
  llvm::Value *EmitARMBuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                  ReturnValueSlot ReturnValue,
                                  llvm::Triple::ArchType Arch);
  llvm::Value *EmitARMMVEBuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                     ReturnValueSlot ReturnValue,
                                     llvm::Triple::ArchType Arch);
  llvm::Value *EmitARMCDEBuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                     ReturnValueSlot ReturnValue,
                                     llvm::Triple::ArchType Arch);
  llvm::Value *EmitCMSEClearRecord(llvm::Value *V, llvm::IntegerType *ITy,
                                   QualType RTy);
  llvm::Value *EmitCMSEClearRecord(llvm::Value *V, llvm::ArrayType *ATy,
                                   QualType RTy);

  llvm::Value *EmitCommonNeonBuiltinExpr(unsigned BuiltinID,
                                         unsigned LLVMIntrinsic,
                                         unsigned AltLLVMIntrinsic,
                                         const char *NameHint,
                                         unsigned Modifier,
                                         const CallExpr *E,
                                         SmallVectorImpl<llvm::Value *> &Ops,
                                         Address PtrOp0, Address PtrOp1,
                                         llvm::Triple::ArchType Arch);

  llvm::Function *LookupNeonLLVMIntrinsic(unsigned IntrinsicID,
                                          unsigned Modifier, llvm::Type *ArgTy,
                                          const CallExpr *E);
  llvm::Value *EmitNeonCall(llvm::Function *F,
                            SmallVectorImpl<llvm::Value*> &O,
                            const char *name,
                            unsigned shift = 0, bool rightshift = false);
  llvm::Value *EmitNeonSplat(llvm::Value *V, llvm::Constant *Idx,
                             const llvm::ElementCount &Count);
  llvm::Value *EmitNeonSplat(llvm::Value *V, llvm::Constant *Idx);
  llvm::Value *EmitNeonShiftVector(llvm::Value *V, llvm::Type *Ty,
                                   bool negateForRightShift);
  llvm::Value *EmitNeonRShiftImm(llvm::Value *Vec, llvm::Value *Amt,
                                 llvm::Type *Ty, bool usgn, const char *name);
  llvm::Value *vectorWrapScalar16(llvm::Value *Op);
  /// SVEBuiltinMemEltTy - Returns the memory element type for this memory
  /// access builtin.  Only required if it can't be inferred from the base
  /// pointer operand.
  llvm::Type *SVEBuiltinMemEltTy(const SVETypeFlags &TypeFlags);

  SmallVector<llvm::Type *, 2>
  getSVEOverloadTypes(const SVETypeFlags &TypeFlags, llvm::Type *ReturnType,
                      ArrayRef<llvm::Value *> Ops);
  llvm::Type *getEltType(const SVETypeFlags &TypeFlags);
  llvm::ScalableVectorType *getSVEType(const SVETypeFlags &TypeFlags);
  llvm::ScalableVectorType *getSVEPredType(const SVETypeFlags &TypeFlags);
  llvm::Value *EmitSVETupleSetOrGet(const SVETypeFlags &TypeFlags,
                                    llvm::Type *ReturnType,
                                    ArrayRef<llvm::Value *> Ops);
  llvm::Value *EmitSVETupleCreate(const SVETypeFlags &TypeFlags,
                                  llvm::Type *ReturnType,
                                  ArrayRef<llvm::Value *> Ops);
  llvm::Value *EmitSVEAllTruePred(const SVETypeFlags &TypeFlags);
  llvm::Value *EmitSVEDupX(llvm::Value *Scalar);
  llvm::Value *EmitSVEDupX(llvm::Value *Scalar, llvm::Type *Ty);
  llvm::Value *EmitSVEReinterpret(llvm::Value *Val, llvm::Type *Ty);
  llvm::Value *EmitSVEPMull(const SVETypeFlags &TypeFlags,
                            llvm::SmallVectorImpl<llvm::Value *> &Ops,
                            unsigned BuiltinID);
  llvm::Value *EmitSVEMovl(const SVETypeFlags &TypeFlags,
                           llvm::ArrayRef<llvm::Value *> Ops,
                           unsigned BuiltinID);
  llvm::Value *EmitSVEPredicateCast(llvm::Value *Pred,
                                    llvm::ScalableVectorType *VTy);
  llvm::Value *EmitSVEGatherLoad(const SVETypeFlags &TypeFlags,
                                 llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                 unsigned IntID);
  llvm::Value *EmitSVEScatterStore(const SVETypeFlags &TypeFlags,
                                   llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                   unsigned IntID);
  llvm::Value *EmitSVEMaskedLoad(const CallExpr *, llvm::Type *ReturnTy,
                                 SmallVectorImpl<llvm::Value *> &Ops,
                                 unsigned BuiltinID, bool IsZExtReturn);
  llvm::Value *EmitSVEMaskedStore(const CallExpr *,
                                  SmallVectorImpl<llvm::Value *> &Ops,
                                  unsigned BuiltinID);
  llvm::Value *EmitSVEPrefetchLoad(const SVETypeFlags &TypeFlags,
                                   SmallVectorImpl<llvm::Value *> &Ops,
                                   unsigned BuiltinID);
  llvm::Value *EmitSVEGatherPrefetch(const SVETypeFlags &TypeFlags,
                                     SmallVectorImpl<llvm::Value *> &Ops,
                                     unsigned IntID);
  llvm::Value *EmitSVEStructLoad(const SVETypeFlags &TypeFlags,
                                 SmallVectorImpl<llvm::Value *> &Ops,
                                 unsigned IntID);
  llvm::Value *EmitSVEStructStore(const SVETypeFlags &TypeFlags,
                                  SmallVectorImpl<llvm::Value *> &Ops,
                                  unsigned IntID);
  /// FormSVEBuiltinResult - Returns the struct of scalable vectors as a wider
  /// vector. It extracts the scalable vector from the struct and inserts into
  /// the wider vector. This avoids the error when allocating space in llvm
  /// for struct of scalable vectors if a function returns struct.
  llvm::Value *FormSVEBuiltinResult(llvm::Value *Call);

  llvm::Value *EmitAArch64SVEBuiltinExpr(unsigned BuiltinID, const CallExpr *E);

  llvm::Value *EmitSMELd1St1(const SVETypeFlags &TypeFlags,
                             llvm::SmallVectorImpl<llvm::Value *> &Ops,
                             unsigned IntID);
  llvm::Value *EmitSMEReadWrite(const SVETypeFlags &TypeFlags,
                                llvm::SmallVectorImpl<llvm::Value *> &Ops,
                                unsigned IntID);
  llvm::Value *EmitSMEZero(const SVETypeFlags &TypeFlags,
                           llvm::SmallVectorImpl<llvm::Value *> &Ops,
                           unsigned IntID);
  llvm::Value *EmitSMELdrStr(const SVETypeFlags &TypeFlags,
                             llvm::SmallVectorImpl<llvm::Value *> &Ops,
                             unsigned IntID);

  void GetAArch64SVEProcessedOperands(unsigned BuiltinID, const CallExpr *E,
                                      SmallVectorImpl<llvm::Value *> &Ops,
                                      SVETypeFlags TypeFlags);

  llvm::Value *EmitAArch64SMEBuiltinExpr(unsigned BuiltinID, const CallExpr *E);

  llvm::Value *EmitAArch64BuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                      llvm::Triple::ArchType Arch);
  llvm::Value *EmitBPFBuiltinExpr(unsigned BuiltinID, const CallExpr *E);

  llvm::Value *BuildVector(ArrayRef<llvm::Value*> Ops);
  llvm::Value *EmitX86BuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitPPCBuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitAMDGPUBuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitHLSLBuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitScalarOrConstFoldImmArg(unsigned ICEArguments, unsigned Idx,
                                           const CallExpr *E);
  llvm::Value *EmitSystemZBuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitNVPTXBuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitWebAssemblyBuiltinExpr(unsigned BuiltinID,
                                          const CallExpr *E);
  llvm::Value *EmitHexagonBuiltinExpr(unsigned BuiltinID, const CallExpr *E);
  llvm::Value *EmitRISCVBuiltinExpr(unsigned BuiltinID, const CallExpr *E,
                                    ReturnValueSlot ReturnValue);

  void AddAMDGPUFenceAddressSpaceMMRA(llvm::Instruction *Inst,
                                      const CallExpr *E);
  void ProcessOrderScopeAMDGCN(llvm::Value *Order, llvm::Value *Scope,
                               llvm::AtomicOrdering &AO,
                               llvm::SyncScope::ID &SSID);

  enum class MSVCIntrin;
  llvm::Value *EmitMSVCBuiltinExpr(MSVCIntrin BuiltinID, const CallExpr *E);

  llvm::Value *EmitBuiltinAvailable(const VersionTuple &Version);

  llvm::Value *EmitObjCProtocolExpr(const ObjCProtocolExpr *E);
  llvm::Value *EmitObjCStringLiteral(const ObjCStringLiteral *E);
  llvm::Value *EmitObjCBoxedExpr(const ObjCBoxedExpr *E);
  llvm::Value *EmitObjCArrayLiteral(const ObjCArrayLiteral *E);
  llvm::Value *EmitObjCDictionaryLiteral(const ObjCDictionaryLiteral *E);
  llvm::Value *EmitObjCCollectionLiteral(const Expr *E,
                                const ObjCMethodDecl *MethodWithObjects);
  llvm::Value *EmitObjCSelectorExpr(const ObjCSelectorExpr *E);
  RValue EmitObjCMessageExpr(const ObjCMessageExpr *E,
                             ReturnValueSlot Return = ReturnValueSlot());

  /// Retrieves the default cleanup kind for an ARC cleanup.
  /// Except under -fobjc-arc-eh, ARC cleanups are normal-only.
  CleanupKind getARCCleanupKind() {
    return CGM.getCodeGenOpts().ObjCAutoRefCountExceptions
             ? NormalAndEHCleanup : NormalCleanup;
  }

  // ARC primitives.
  void EmitARCInitWeak(Address addr, llvm::Value *value);
  void EmitARCDestroyWeak(Address addr);
  llvm::Value *EmitARCLoadWeak(Address addr);
  llvm::Value *EmitARCLoadWeakRetained(Address addr);
  llvm::Value *EmitARCStoreWeak(Address addr, llvm::Value *value, bool ignored);
  void emitARCCopyAssignWeak(QualType Ty, Address DstAddr, Address SrcAddr);
  void emitARCMoveAssignWeak(QualType Ty, Address DstAddr, Address SrcAddr);
  void EmitARCCopyWeak(Address dst, Address src);
  void EmitARCMoveWeak(Address dst, Address src);
  llvm::Value *EmitARCRetainAutorelease(QualType type, llvm::Value *value);
  llvm::Value *EmitARCRetainAutoreleaseNonBlock(llvm::Value *value);
  llvm::Value *EmitARCStoreStrong(LValue lvalue, llvm::Value *value,
                                  bool resultIgnored);
  llvm::Value *EmitARCStoreStrongCall(Address addr, llvm::Value *value,
                                      bool resultIgnored);
  llvm::Value *EmitARCRetain(QualType type, llvm::Value *value);
  llvm::Value *EmitARCRetainNonBlock(llvm::Value *value);
  llvm::Value *EmitARCRetainBlock(llvm::Value *value, bool mandatory);
  void EmitARCDestroyStrong(Address addr, ARCPreciseLifetime_t precise);
  void EmitARCRelease(llvm::Value *value, ARCPreciseLifetime_t precise);
  llvm::Value *EmitARCAutorelease(llvm::Value *value);
  llvm::Value *EmitARCAutoreleaseReturnValue(llvm::Value *value);
  llvm::Value *EmitARCRetainAutoreleaseReturnValue(llvm::Value *value);
  llvm::Value *EmitARCRetainAutoreleasedReturnValue(llvm::Value *value);
  llvm::Value *EmitARCUnsafeClaimAutoreleasedReturnValue(llvm::Value *value);

  llvm::Value *EmitObjCAutorelease(llvm::Value *value, llvm::Type *returnType);
  llvm::Value *EmitObjCRetainNonBlock(llvm::Value *value,
                                      llvm::Type *returnType);
  void EmitObjCRelease(llvm::Value *value, ARCPreciseLifetime_t precise);

  std::pair<LValue,llvm::Value*>
  EmitARCStoreAutoreleasing(const BinaryOperator *e);
  std::pair<LValue,llvm::Value*>
  EmitARCStoreStrong(const BinaryOperator *e, bool ignored);
  std::pair<LValue,llvm::Value*>
  EmitARCStoreUnsafeUnretained(const BinaryOperator *e, bool ignored);

  llvm::Value *EmitObjCAlloc(llvm::Value *value,
                             llvm::Type *returnType);
  llvm::Value *EmitObjCAllocWithZone(llvm::Value *value,
                                     llvm::Type *returnType);
  llvm::Value *EmitObjCAllocInit(llvm::Value *value, llvm::Type *resultType);

  llvm::Value *EmitObjCThrowOperand(const Expr *expr);
  llvm::Value *EmitObjCConsumeObject(QualType T, llvm::Value *Ptr);
  llvm::Value *EmitObjCExtendObjectLifetime(QualType T, llvm::Value *Ptr);

  llvm::Value *EmitARCExtendBlockObject(const Expr *expr);
  llvm::Value *EmitARCReclaimReturnedObject(const Expr *e,
                                            bool allowUnsafeClaim);
  llvm::Value *EmitARCRetainScalarExpr(const Expr *expr);
  llvm::Value *EmitARCRetainAutoreleaseScalarExpr(const Expr *expr);
  llvm::Value *EmitARCUnsafeUnretainedScalarExpr(const Expr *expr);

  void EmitARCIntrinsicUse(ArrayRef<llvm::Value*> values);

  void EmitARCNoopIntrinsicUse(ArrayRef<llvm::Value *> values);

  static Destroyer destroyARCStrongImprecise;
  static Destroyer destroyARCStrongPrecise;
  static Destroyer destroyARCWeak;
  static Destroyer emitARCIntrinsicUse;
  static Destroyer destroyNonTrivialCStruct;

  void EmitObjCAutoreleasePoolPop(llvm::Value *Ptr);
  llvm::Value *EmitObjCAutoreleasePoolPush();
  llvm::Value *EmitObjCMRRAutoreleasePoolPush();
  void EmitObjCAutoreleasePoolCleanup(llvm::Value *Ptr);
  void EmitObjCMRRAutoreleasePoolPop(llvm::Value *Ptr);

  /// Emits a reference binding to the passed in expression.
  RValue EmitReferenceBindingToExpr(const Expr *E);

  //===--------------------------------------------------------------------===//
  //                           Expression Emission
  //===--------------------------------------------------------------------===//

  // Expressions are broken into three classes: scalar, complex, aggregate.

  /// EmitScalarExpr - Emit the computation of the specified expression of LLVM
  /// scalar type, returning the result.
  llvm::Value *EmitScalarExpr(const Expr *E , bool IgnoreResultAssign = false);

  /// Emit a conversion from the specified type to the specified destination
  /// type, both of which are LLVM scalar types.
  llvm::Value *EmitScalarConversion(llvm::Value *Src, QualType SrcTy,
                                    QualType DstTy, SourceLocation Loc);

  /// Emit a conversion from the specified complex type to the specified
  /// destination type, where the destination type is an LLVM scalar type.
  llvm::Value *EmitComplexToScalarConversion(ComplexPairTy Src, QualType SrcTy,
                                             QualType DstTy,
                                             SourceLocation Loc);

  /// EmitAggExpr - Emit the computation of the specified expression
  /// of aggregate type.  The result is computed into the given slot,
  /// which may be null to indicate that the value is not needed.
  void EmitAggExpr(const Expr *E, AggValueSlot AS);

  /// EmitAggExprToLValue - Emit the computation of the specified expression of
  /// aggregate type into a temporary LValue.
  LValue EmitAggExprToLValue(const Expr *E);

  enum ExprValueKind { EVK_RValue, EVK_NonRValue };

  /// EmitAggFinalDestCopy - Emit copy of the specified aggregate into
  /// destination address.
  void EmitAggFinalDestCopy(QualType Type, AggValueSlot Dest, const LValue &Src,
                            ExprValueKind SrcKind);

  /// Create a store to \arg DstPtr from \arg Src, truncating the stored value
  /// to at most \arg DstSize bytes.
  void CreateCoercedStore(llvm::Value *Src, Address Dst, llvm::TypeSize DstSize,
                          bool DstIsVolatile);

  /// EmitExtendGCLifetime - Given a pointer to an Objective-C object,
  /// make sure it survives garbage collection until this point.
  void EmitExtendGCLifetime(llvm::Value *object);

  /// EmitComplexExpr - Emit the computation of the specified expression of
  /// complex type, returning the result.
  ComplexPairTy EmitComplexExpr(const Expr *E,
                                bool IgnoreReal = false,
                                bool IgnoreImag = false);

  /// EmitComplexExprIntoLValue - Emit the given expression of complex
  /// type and place its result into the specified l-value.
  void EmitComplexExprIntoLValue(const Expr *E, LValue dest, bool isInit);

  /// EmitStoreOfComplex - Store a complex number into the specified l-value.
  void EmitStoreOfComplex(ComplexPairTy V, LValue dest, bool isInit);

  /// EmitLoadOfComplex - Load a complex number from the specified l-value.
  ComplexPairTy EmitLoadOfComplex(LValue src, SourceLocation loc);

  ComplexPairTy EmitPromotedComplexExpr(const Expr *E, QualType PromotionType);
  llvm::Value *EmitPromotedScalarExpr(const Expr *E, QualType PromotionType);
  ComplexPairTy EmitPromotedValue(ComplexPairTy result, QualType PromotionType);
  ComplexPairTy EmitUnPromotedValue(ComplexPairTy result, QualType PromotionType);

  Address emitAddrOfRealComponent(Address complex, QualType complexType);
  Address emitAddrOfImagComponent(Address complex, QualType complexType);

  /// AddInitializerToStaticVarDecl - Add the initializer for 'D' to the
  /// global variable that has already been created for it.  If the initializer
  /// has a different type than GV does, this may free GV and return a different
  /// one.  Otherwise it just returns GV.
  llvm::GlobalVariable *
  AddInitializerToStaticVarDecl(const VarDecl &D,
                                llvm::GlobalVariable *GV);

  // Emit an @llvm.invariant.start call for the given memory region.
  void EmitInvariantStart(llvm::Constant *Addr, CharUnits Size);

  /// EmitCXXGlobalVarDeclInit - Create the initializer for a C++
  /// variable with global storage.
  void EmitCXXGlobalVarDeclInit(const VarDecl &D, llvm::GlobalVariable *GV,
                                bool PerformInit);

  llvm::Constant *createAtExitStub(const VarDecl &VD, llvm::FunctionCallee Dtor,
                                   llvm::Constant *Addr);

  llvm::Function *createTLSAtExitStub(const VarDecl &VD,
                                      llvm::FunctionCallee Dtor,
                                      llvm::Constant *Addr,
                                      llvm::FunctionCallee &AtExit);

  /// Call atexit() with a function that passes the given argument to
  /// the given function.
  void registerGlobalDtorWithAtExit(const VarDecl &D, llvm::FunctionCallee fn,
                                    llvm::Constant *addr);

  /// Registers the dtor using 'llvm.global_dtors' for platforms that do not
  /// support an 'atexit()' function.
  void registerGlobalDtorWithLLVM(const VarDecl &D, llvm::FunctionCallee fn,
                                  llvm::Constant *addr);

  /// Call atexit() with function dtorStub.
  void registerGlobalDtorWithAtExit(llvm::Constant *dtorStub);

  /// Call unatexit() with function dtorStub.
  llvm::Value *unregisterGlobalDtorWithUnAtExit(llvm::Constant *dtorStub);

  /// Emit code in this function to perform a guarded variable
  /// initialization.  Guarded initializations are used when it's not
  /// possible to prove that an initialization will be done exactly
  /// once, e.g. with a static local variable or a static data member
  /// of a class template.
  void EmitCXXGuardedInit(const VarDecl &D, llvm::GlobalVariable *DeclPtr,
                          bool PerformInit);

  enum class GuardKind { VariableGuard, TlsGuard };

  /// Emit a branch to select whether or not to perform guarded initialization.
  void EmitCXXGuardedInitBranch(llvm::Value *NeedsInit,
                                llvm::BasicBlock *InitBlock,
                                llvm::BasicBlock *NoInitBlock,
                                GuardKind Kind, const VarDecl *D);

  /// GenerateCXXGlobalInitFunc - Generates code for initializing global
  /// variables.
  void
  GenerateCXXGlobalInitFunc(llvm::Function *Fn,
                            ArrayRef<llvm::Function *> CXXThreadLocals,
                            ConstantAddress Guard = ConstantAddress::invalid());

  /// GenerateCXXGlobalCleanUpFunc - Generates code for cleaning up global
  /// variables.
  void GenerateCXXGlobalCleanUpFunc(
      llvm::Function *Fn,
      ArrayRef<std::tuple<llvm::FunctionType *, llvm::WeakTrackingVH,
                          llvm::Constant *>>
          DtorsOrStermFinalizers);

  void GenerateCXXGlobalVarDeclInitFunc(llvm::Function *Fn,
                                        const VarDecl *D,
                                        llvm::GlobalVariable *Addr,
                                        bool PerformInit);

  void EmitCXXConstructExpr(const CXXConstructExpr *E, AggValueSlot Dest);

  void EmitSynthesizedCXXCopyCtor(Address Dest, Address Src, const Expr *Exp);

  void EmitCXXThrowExpr(const CXXThrowExpr *E, bool KeepInsertionPoint = true);

  RValue EmitAtomicExpr(AtomicExpr *E);

  //===--------------------------------------------------------------------===//
  //                         Annotations Emission
  //===--------------------------------------------------------------------===//

  /// Emit an annotation call (intrinsic).
  llvm::Value *EmitAnnotationCall(llvm::Function *AnnotationFn,
                                  llvm::Value *AnnotatedVal,
                                  StringRef AnnotationStr,
                                  SourceLocation Location,
                                  const AnnotateAttr *Attr);

  /// Emit local annotations for the local variable V, declared by D.
  void EmitVarAnnotations(const VarDecl *D, llvm::Value *V);

  /// Emit field annotations for the given field & value. Returns the
  /// annotation result.
  Address EmitFieldAnnotations(const FieldDecl *D, Address V);

  //===--------------------------------------------------------------------===//
  //                             Internal Helpers
  //===--------------------------------------------------------------------===//

  /// ContainsLabel - Return true if the statement contains a label in it.  If
  /// this statement is not executed normally, it not containing a label means
  /// that we can just remove the code.
  static bool ContainsLabel(const Stmt *S, bool IgnoreCaseStmts = false);

  /// containsBreak - Return true if the statement contains a break out of it.
  /// If the statement (recursively) contains a switch or loop with a break
  /// inside of it, this is fine.
  static bool containsBreak(const Stmt *S);

  /// Determine if the given statement might introduce a declaration into the
  /// current scope, by being a (possibly-labelled) DeclStmt.
  static bool mightAddDeclToScope(const Stmt *S);

  /// ConstantFoldsToSimpleInteger - If the specified expression does not fold
  /// to a constant, or if it does but contains a label, return false.  If it
  /// constant folds return true and set the boolean result in Result.
  bool ConstantFoldsToSimpleInteger(const Expr *Cond, bool &Result,
                                    bool AllowLabels = false);

  /// ConstantFoldsToSimpleInteger - If the specified expression does not fold
  /// to a constant, or if it does but contains a label, return false.  If it
  /// constant folds return true and set the folded value.
  bool ConstantFoldsToSimpleInteger(const Expr *Cond, llvm::APSInt &Result,
                                    bool AllowLabels = false);

  /// Ignore parentheses and logical-NOT to track conditions consistently.
  static const Expr *stripCond(const Expr *C);

  /// isInstrumentedCondition - Determine whether the given condition is an
  /// instrumentable condition (i.e. no "&&" or "||").
  static bool isInstrumentedCondition(const Expr *C);

  /// EmitBranchToCounterBlock - Emit a conditional branch to a new block that
  /// increments a profile counter based on the semantics of the given logical
  /// operator opcode.  This is used to instrument branch condition coverage
  /// for logical operators.
  void EmitBranchToCounterBlock(const Expr *Cond, BinaryOperator::Opcode LOp,
                                llvm::BasicBlock *TrueBlock,
                                llvm::BasicBlock *FalseBlock,
                                uint64_t TrueCount = 0,
                                Stmt::Likelihood LH = Stmt::LH_None,
                                const Expr *CntrIdx = nullptr);

  /// EmitBranchOnBoolExpr - Emit a branch on a boolean condition (e.g. for an
  /// if statement) to the specified blocks.  Based on the condition, this might
  /// try to simplify the codegen of the conditional based on the branch.
  /// TrueCount should be the number of times we expect the condition to
  /// evaluate to true based on PGO data.
  void EmitBranchOnBoolExpr(const Expr *Cond, llvm::BasicBlock *TrueBlock,
                            llvm::BasicBlock *FalseBlock, uint64_t TrueCount,
                            Stmt::Likelihood LH = Stmt::LH_None,
                            const Expr *ConditionalOp = nullptr);

  /// Given an assignment `*LHS = RHS`, emit a test that checks if \p RHS is
  /// nonnull, if \p LHS is marked _Nonnull.
  void EmitNullabilityCheck(LValue LHS, llvm::Value *RHS, SourceLocation Loc);

  /// An enumeration which makes it easier to specify whether or not an
  /// operation is a subtraction.
  enum { NotSubtraction = false, IsSubtraction = true };

  /// Same as IRBuilder::CreateInBoundsGEP, but additionally emits a check to
  /// detect undefined behavior when the pointer overflow sanitizer is enabled.
  /// \p SignedIndices indicates whether any of the GEP indices are signed.
  /// \p IsSubtraction indicates whether the expression used to form the GEP
  /// is a subtraction.
  llvm::Value *EmitCheckedInBoundsGEP(llvm::Type *ElemTy, llvm::Value *Ptr,
                                      ArrayRef<llvm::Value *> IdxList,
                                      bool SignedIndices,
                                      bool IsSubtraction,
                                      SourceLocation Loc,
                                      const Twine &Name = "");

  Address EmitCheckedInBoundsGEP(Address Addr, ArrayRef<llvm::Value *> IdxList,
                                 llvm::Type *elementType, bool SignedIndices,
                                 bool IsSubtraction, SourceLocation Loc,
                                 CharUnits Align, const Twine &Name = "");

  /// Specifies which type of sanitizer check to apply when handling a
  /// particular builtin.
  enum BuiltinCheckKind {
    BCK_CTZPassedZero,
    BCK_CLZPassedZero,
  };

  /// Emits an argument for a call to a builtin. If the builtin sanitizer is
  /// enabled, a runtime check specified by \p Kind is also emitted.
  llvm::Value *EmitCheckedArgForBuiltin(const Expr *E, BuiltinCheckKind Kind);

  /// Emit a description of a type in a format suitable for passing to
  /// a runtime sanitizer handler.
  llvm::Constant *EmitCheckTypeDescriptor(QualType T);

  /// Convert a value into a format suitable for passing to a runtime
  /// sanitizer handler.
  llvm::Value *EmitCheckValue(llvm::Value *V);

  /// Emit a description of a source location in a format suitable for
  /// passing to a runtime sanitizer handler.
  llvm::Constant *EmitCheckSourceLocation(SourceLocation Loc);

  void EmitKCFIOperandBundle(const CGCallee &Callee,
                             SmallVectorImpl<llvm::OperandBundleDef> &Bundles);

  /// Create a basic block that will either trap or call a handler function in
  /// the UBSan runtime with the provided arguments, and create a conditional
  /// branch to it.
  void EmitCheck(ArrayRef<std::pair<llvm::Value *, SanitizerMask>> Checked,
                 SanitizerHandler Check, ArrayRef<llvm::Constant *> StaticArgs,
                 ArrayRef<llvm::Value *> DynamicArgs);

  /// Emit a slow path cross-DSO CFI check which calls __cfi_slowpath
  /// if Cond if false.
  void EmitCfiSlowPathCheck(SanitizerMask Kind, llvm::Value *Cond,
                            llvm::ConstantInt *TypeId, llvm::Value *Ptr,
                            ArrayRef<llvm::Constant *> StaticArgs);

  /// Emit a reached-unreachable diagnostic if \p Loc is valid and runtime
  /// checking is enabled. Otherwise, just emit an unreachable instruction.
  void EmitUnreachable(SourceLocation Loc);

  /// Create a basic block that will call the trap intrinsic, and emit a
  /// conditional branch to it, for the -ftrapv checks.
  void EmitTrapCheck(llvm::Value *Checked, SanitizerHandler CheckHandlerID);

  /// Emit a call to trap or debugtrap and attach function attribute
  /// "trap-func-name" if specified.
  llvm::CallInst *EmitTrapCall(llvm::Intrinsic::ID IntrID);

  /// Emit a stub for the cross-DSO CFI check function.
  void EmitCfiCheckStub();

  /// Emit a cross-DSO CFI failure handling function.
  void EmitCfiCheckFail();

  /// Create a check for a function parameter that may potentially be
  /// declared as non-null.
  void EmitNonNullArgCheck(RValue RV, QualType ArgType, SourceLocation ArgLoc,
                           AbstractCallee AC, unsigned ParmNum);

  void EmitNonNullArgCheck(Address Addr, QualType ArgType,
                           SourceLocation ArgLoc, AbstractCallee AC,
                           unsigned ParmNum);

  /// EmitCallArg - Emit a single call argument.
  void EmitCallArg(CallArgList &args, const Expr *E, QualType ArgType);

  /// EmitDelegateCallArg - We are performing a delegate call; that
  /// is, the current function is delegating to another one.  Produce
  /// a r-value suitable for passing the given parameter.
  void EmitDelegateCallArg(CallArgList &args, const VarDecl *param,
                           SourceLocation loc);

  /// SetFPAccuracy - Set the minimum required accuracy of the given floating
  /// point operation, expressed as the maximum relative error in ulp.
  void SetFPAccuracy(llvm::Value *Val, float Accuracy);

  /// Set the minimum required accuracy of the given sqrt operation
  /// based on CodeGenOpts.
  void SetSqrtFPAccuracy(llvm::Value *Val);

  /// Set the minimum required accuracy of the given sqrt operation based on
  /// CodeGenOpts.
  void SetDivFPAccuracy(llvm::Value *Val);

  /// Set the codegen fast-math flags.
  void SetFastMathFlags(FPOptions FPFeatures);

  // Truncate or extend a boolean vector to the requested number of elements.
  llvm::Value *emitBoolVecConversion(llvm::Value *SrcVec,
                                     unsigned NumElementsDst,
                                     const llvm::Twine &Name = "");
  // Adds a convergence_ctrl token to |Input| and emits the required parent
  // convergence instructions.
  template <typename CallType>
  CallType *addControlledConvergenceToken(CallType *Input) {
    return cast<CallType>(
        addConvergenceControlToken(Input, ConvergenceTokenStack.back()));
  }

private:
  // Emits a convergence_loop instruction for the given |BB|, with |ParentToken|
  // as it's parent convergence instr.
  llvm::IntrinsicInst *emitConvergenceLoopToken(llvm::BasicBlock *BB,
                                                llvm::Value *ParentToken);
  // Adds a convergence_ctrl token with |ParentToken| as parent convergence
  // instr to the call |Input|.
  llvm::CallBase *addConvergenceControlToken(llvm::CallBase *Input,
                                             llvm::Value *ParentToken);
  // Find the convergence_entry instruction |F|, or emits ones if none exists.
  // Returns the convergence instruction.
  llvm::IntrinsicInst *getOrEmitConvergenceEntryToken(llvm::Function *F);
  // Find the convergence_loop instruction for the loop defined by |LI|, or
  // emits one if none exists. Returns the convergence instruction.
  llvm::IntrinsicInst *getOrEmitConvergenceLoopToken(const LoopInfo *LI);

private:
  llvm::MDNode *getRangeForLoadFromType(QualType Ty);
  void EmitReturnOfRValue(RValue RV, QualType Ty);

  void deferPlaceholderReplacement(llvm::Instruction *Old, llvm::Value *New);

  llvm::SmallVector<std::pair<llvm::WeakTrackingVH, llvm::Value *>, 4>
      DeferredReplacements;

  /// Set the address of a local variable.
  void setAddrOfLocalVar(const VarDecl *VD, Address Addr) {
    assert(!LocalDeclMap.count(VD) && "Decl already exists in LocalDeclMap!");
    LocalDeclMap.insert({VD, Addr});
  }

  /// ExpandTypeFromArgs - Reconstruct a structure of type \arg Ty
  /// from function arguments into \arg Dst. See ABIArgInfo::Expand.
  ///
  /// \param AI - The first function argument of the expansion.
  void ExpandTypeFromArgs(QualType Ty, LValue Dst,
                          llvm::Function::arg_iterator &AI);

  /// ExpandTypeToArgs - Expand an CallArg \arg Arg, with the LLVM type for \arg
  /// Ty, into individual arguments on the provided vector \arg IRCallArgs,
  /// starting at index \arg IRCallArgPos. See ABIArgInfo::Expand.
  void ExpandTypeToArgs(QualType Ty, CallArg Arg, llvm::FunctionType *IRFuncTy,
                        SmallVectorImpl<llvm::Value *> &IRCallArgs,
                        unsigned &IRCallArgPos);

  std::pair<llvm::Value *, llvm::Type *>
  EmitAsmInput(const TargetInfo::ConstraintInfo &Info, const Expr *InputExpr,
               std::string &ConstraintStr);

  std::pair<llvm::Value *, llvm::Type *>
  EmitAsmInputLValue(const TargetInfo::ConstraintInfo &Info, LValue InputValue,
                     QualType InputType, std::string &ConstraintStr,
                     SourceLocation Loc);

  /// Attempts to statically evaluate the object size of E. If that
  /// fails, emits code to figure the size of E out for us. This is
  /// pass_object_size aware.
  ///
  /// If EmittedExpr is non-null, this will use that instead of re-emitting E.
  llvm::Value *evaluateOrEmitBuiltinObjectSize(const Expr *E, unsigned Type,
                                               llvm::IntegerType *ResType,
                                               llvm::Value *EmittedE,
                                               bool IsDynamic);

  /// Emits the size of E, as required by __builtin_object_size. This
  /// function is aware of pass_object_size parameters, and will act accordingly
  /// if E is a parameter with the pass_object_size attribute.
  llvm::Value *emitBuiltinObjectSize(const Expr *E, unsigned Type,
                                     llvm::IntegerType *ResType,
                                     llvm::Value *EmittedE,
                                     bool IsDynamic);

  llvm::Value *emitFlexibleArrayMemberSize(const Expr *E, unsigned Type,
                                           llvm::IntegerType *ResType);

  void emitZeroOrPatternForAutoVarInit(QualType type, const VarDecl &D,
                                       Address Loc);

public:
  enum class EvaluationOrder {
    ///! No language constraints on evaluation order.
    Default,
    ///! Language semantics require left-to-right evaluation.
    ForceLeftToRight,
    ///! Language semantics require right-to-left evaluation.
    ForceRightToLeft
  };

  // Wrapper for function prototype sources. Wraps either a FunctionProtoType or
  // an ObjCMethodDecl.
  struct PrototypeWrapper {
    llvm::PointerUnion<const FunctionProtoType *, const ObjCMethodDecl *> P;

    PrototypeWrapper(const FunctionProtoType *FT) : P(FT) {}
    PrototypeWrapper(const ObjCMethodDecl *MD) : P(MD) {}
  };

  void EmitCallArgs(CallArgList &Args, PrototypeWrapper Prototype,
                    llvm::iterator_range<CallExpr::const_arg_iterator> ArgRange,
                    AbstractCallee AC = AbstractCallee(),
                    unsigned ParamsToSkip = 0,
                    EvaluationOrder Order = EvaluationOrder::Default);

  /// EmitPointerWithAlignment - Given an expression with a pointer type,
  /// emit the value and compute our best estimate of the alignment of the
  /// pointee.
  ///
  /// \param BaseInfo - If non-null, this will be initialized with
  /// information about the source of the alignment and the may-alias
  /// attribute.  Note that this function will conservatively fall back on
  /// the type when it doesn't recognize the expression and may-alias will
  /// be set to false.
  ///
  /// One reasonable way to use this information is when there's a language
  /// guarantee that the pointer must be aligned to some stricter value, and
  /// we're simply trying to ensure that sufficiently obvious uses of under-
  /// aligned objects don't get miscompiled; for example, a placement new
  /// into the address of a local variable.  In such a case, it's quite
  /// reasonable to just ignore the returned alignment when it isn't from an
  /// explicit source.
  Address
  EmitPointerWithAlignment(const Expr *Addr, LValueBaseInfo *BaseInfo = nullptr,
                           TBAAAccessInfo *TBAAInfo = nullptr,
                           KnownNonNull_t IsKnownNonNull = NotKnownNonNull);

  /// If \p E references a parameter with pass_object_size info or a constant
  /// array size modifier, emit the object size divided by the size of \p EltTy.
  /// Otherwise return null.
  llvm::Value *LoadPassedObjectSize(const Expr *E, QualType EltTy);

  void EmitSanitizerStatReport(llvm::SanitizerStatKind SSK);

  struct MultiVersionResolverOption {
    llvm::Function *Function;
    struct Conds {
      StringRef Architecture;
      llvm::SmallVector<StringRef, 8> Features;

      Conds(StringRef Arch, ArrayRef<StringRef> Feats)
          : Architecture(Arch), Features(Feats.begin(), Feats.end()) {}
    } Conditions;

    MultiVersionResolverOption(llvm::Function *F, StringRef Arch,
                               ArrayRef<StringRef> Feats)
        : Function(F), Conditions(Arch, Feats) {}
  };

  // Emits the body of a multiversion function's resolver. Assumes that the
  // options are already sorted in the proper order, with the 'default' option
  // last (if it exists).
  void EmitMultiVersionResolver(llvm::Function *Resolver,
                                ArrayRef<MultiVersionResolverOption> Options);
  void
  EmitX86MultiVersionResolver(llvm::Function *Resolver,
                              ArrayRef<MultiVersionResolverOption> Options);
  void
  EmitAArch64MultiVersionResolver(llvm::Function *Resolver,
                                  ArrayRef<MultiVersionResolverOption> Options);

private:
  QualType getVarArgType(const Expr *Arg);

  void EmitDeclMetadata();

  BlockByrefHelpers *buildByrefHelpers(llvm::StructType &byrefType,
                                  const AutoVarEmission &emission);

  void AddObjCARCExceptionMetadata(llvm::Instruction *Inst);

  llvm::Value *GetValueForARMHint(unsigned BuiltinID);
  llvm::Value *EmitX86CpuIs(const CallExpr *E);
  llvm::Value *EmitX86CpuIs(StringRef CPUStr);
  llvm::Value *EmitX86CpuSupports(const CallExpr *E);
  llvm::Value *EmitX86CpuSupports(ArrayRef<StringRef> FeatureStrs);
  llvm::Value *EmitX86CpuSupports(std::array<uint32_t, 4> FeatureMask);
  llvm::Value *EmitX86CpuInit();
  llvm::Value *FormX86ResolverCondition(const MultiVersionResolverOption &RO);
  llvm::Value *EmitAArch64CpuInit();
  llvm::Value *
  FormAArch64ResolverCondition(const MultiVersionResolverOption &RO);
  llvm::Value *EmitAArch64CpuSupports(const CallExpr *E);
  llvm::Value *EmitAArch64CpuSupports(ArrayRef<StringRef> FeatureStrs);
};

inline DominatingLLVMValue::saved_type
DominatingLLVMValue::save(CodeGenFunction &CGF, llvm::Value *value) {
  if (!needsSaving(value)) return saved_type(value, false);

  // Otherwise, we need an alloca.
  auto align = CharUnits::fromQuantity(
      CGF.CGM.getDataLayout().getPrefTypeAlign(value->getType()));
  Address alloca =
      CGF.CreateTempAlloca(value->getType(), align, "cond-cleanup.save");
  CGF.Builder.CreateStore(value, alloca);

  return saved_type(alloca.emitRawPointer(CGF), true);
}

inline llvm::Value *DominatingLLVMValue::restore(CodeGenFunction &CGF,
                                                 saved_type value) {
  // If the value says it wasn't saved, trust that it's still dominating.
  if (!value.getInt()) return value.getPointer();

  // Otherwise, it should be an alloca instruction, as set up in save().
  auto alloca = cast<llvm::AllocaInst>(value.getPointer());
  return CGF.Builder.CreateAlignedLoad(alloca->getAllocatedType(), alloca,
                                       alloca->getAlign());
}

}  // end namespace CodeGen

// Map the LangOption for floating point exception behavior into
// the corresponding enum in the IR.
llvm::fp::ExceptionBehavior
ToConstrainedExceptMD(LangOptions::FPExceptionModeKind Kind);
}  // end namespace clang

#endif
