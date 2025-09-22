//===----- CGOpenMPRuntime.cpp - Interface to OpenMP Runtimes -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides a class for OpenMP runtime code generation.
//
//===----------------------------------------------------------------------===//

#include "CGOpenMPRuntime.h"
#include "ABIInfoImpl.h"
#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "TargetInfo.h"
#include "clang/AST/APValue.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/BitmaskEnum.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <numeric>
#include <optional>

using namespace clang;
using namespace CodeGen;
using namespace llvm::omp;

namespace {
/// Base class for handling code generation inside OpenMP regions.
class CGOpenMPRegionInfo : public CodeGenFunction::CGCapturedStmtInfo {
public:
  /// Kinds of OpenMP regions used in codegen.
  enum CGOpenMPRegionKind {
    /// Region with outlined function for standalone 'parallel'
    /// directive.
    ParallelOutlinedRegion,
    /// Region with outlined function for standalone 'task' directive.
    TaskOutlinedRegion,
    /// Region for constructs that do not require function outlining,
    /// like 'for', 'sections', 'atomic' etc. directives.
    InlinedRegion,
    /// Region with outlined function for standalone 'target' directive.
    TargetRegion,
  };

  CGOpenMPRegionInfo(const CapturedStmt &CS,
                     const CGOpenMPRegionKind RegionKind,
                     const RegionCodeGenTy &CodeGen, OpenMPDirectiveKind Kind,
                     bool HasCancel)
      : CGCapturedStmtInfo(CS, CR_OpenMP), RegionKind(RegionKind),
        CodeGen(CodeGen), Kind(Kind), HasCancel(HasCancel) {}

  CGOpenMPRegionInfo(const CGOpenMPRegionKind RegionKind,
                     const RegionCodeGenTy &CodeGen, OpenMPDirectiveKind Kind,
                     bool HasCancel)
      : CGCapturedStmtInfo(CR_OpenMP), RegionKind(RegionKind), CodeGen(CodeGen),
        Kind(Kind), HasCancel(HasCancel) {}

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  virtual const VarDecl *getThreadIDVariable() const = 0;

  /// Emit the captured statement body.
  void EmitBody(CodeGenFunction &CGF, const Stmt *S) override;

  /// Get an LValue for the current ThreadID variable.
  /// \return LValue for thread id variable. This LValue always has type int32*.
  virtual LValue getThreadIDVariableLValue(CodeGenFunction &CGF);

  virtual void emitUntiedSwitch(CodeGenFunction & /*CGF*/) {}

  CGOpenMPRegionKind getRegionKind() const { return RegionKind; }

  OpenMPDirectiveKind getDirectiveKind() const { return Kind; }

  bool hasCancel() const { return HasCancel; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return Info->getKind() == CR_OpenMP;
  }

  ~CGOpenMPRegionInfo() override = default;

protected:
  CGOpenMPRegionKind RegionKind;
  RegionCodeGenTy CodeGen;
  OpenMPDirectiveKind Kind;
  bool HasCancel;
};

/// API for captured statement code generation in OpenMP constructs.
class CGOpenMPOutlinedRegionInfo final : public CGOpenMPRegionInfo {
public:
  CGOpenMPOutlinedRegionInfo(const CapturedStmt &CS, const VarDecl *ThreadIDVar,
                             const RegionCodeGenTy &CodeGen,
                             OpenMPDirectiveKind Kind, bool HasCancel,
                             StringRef HelperName)
      : CGOpenMPRegionInfo(CS, ParallelOutlinedRegion, CodeGen, Kind,
                           HasCancel),
        ThreadIDVar(ThreadIDVar), HelperName(HelperName) {
    assert(ThreadIDVar != nullptr && "No ThreadID in OpenMP region.");
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override { return ThreadIDVar; }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override { return HelperName; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() ==
               ParallelOutlinedRegion;
  }

private:
  /// A variable or parameter storing global thread id for OpenMP
  /// constructs.
  const VarDecl *ThreadIDVar;
  StringRef HelperName;
};

/// API for captured statement code generation in OpenMP constructs.
class CGOpenMPTaskOutlinedRegionInfo final : public CGOpenMPRegionInfo {
public:
  class UntiedTaskActionTy final : public PrePostActionTy {
    bool Untied;
    const VarDecl *PartIDVar;
    const RegionCodeGenTy UntiedCodeGen;
    llvm::SwitchInst *UntiedSwitch = nullptr;

  public:
    UntiedTaskActionTy(bool Tied, const VarDecl *PartIDVar,
                       const RegionCodeGenTy &UntiedCodeGen)
        : Untied(!Tied), PartIDVar(PartIDVar), UntiedCodeGen(UntiedCodeGen) {}
    void Enter(CodeGenFunction &CGF) override {
      if (Untied) {
        // Emit task switching point.
        LValue PartIdLVal = CGF.EmitLoadOfPointerLValue(
            CGF.GetAddrOfLocalVar(PartIDVar),
            PartIDVar->getType()->castAs<PointerType>());
        llvm::Value *Res =
            CGF.EmitLoadOfScalar(PartIdLVal, PartIDVar->getLocation());
        llvm::BasicBlock *DoneBB = CGF.createBasicBlock(".untied.done.");
        UntiedSwitch = CGF.Builder.CreateSwitch(Res, DoneBB);
        CGF.EmitBlock(DoneBB);
        CGF.EmitBranchThroughCleanup(CGF.ReturnBlock);
        CGF.EmitBlock(CGF.createBasicBlock(".untied.jmp."));
        UntiedSwitch->addCase(CGF.Builder.getInt32(0),
                              CGF.Builder.GetInsertBlock());
        emitUntiedSwitch(CGF);
      }
    }
    void emitUntiedSwitch(CodeGenFunction &CGF) const {
      if (Untied) {
        LValue PartIdLVal = CGF.EmitLoadOfPointerLValue(
            CGF.GetAddrOfLocalVar(PartIDVar),
            PartIDVar->getType()->castAs<PointerType>());
        CGF.EmitStoreOfScalar(CGF.Builder.getInt32(UntiedSwitch->getNumCases()),
                              PartIdLVal);
        UntiedCodeGen(CGF);
        CodeGenFunction::JumpDest CurPoint =
            CGF.getJumpDestInCurrentScope(".untied.next.");
        CGF.EmitBranch(CGF.ReturnBlock.getBlock());
        CGF.EmitBlock(CGF.createBasicBlock(".untied.jmp."));
        UntiedSwitch->addCase(CGF.Builder.getInt32(UntiedSwitch->getNumCases()),
                              CGF.Builder.GetInsertBlock());
        CGF.EmitBranchThroughCleanup(CurPoint);
        CGF.EmitBlock(CurPoint.getBlock());
      }
    }
    unsigned getNumberOfParts() const { return UntiedSwitch->getNumCases(); }
  };
  CGOpenMPTaskOutlinedRegionInfo(const CapturedStmt &CS,
                                 const VarDecl *ThreadIDVar,
                                 const RegionCodeGenTy &CodeGen,
                                 OpenMPDirectiveKind Kind, bool HasCancel,
                                 const UntiedTaskActionTy &Action)
      : CGOpenMPRegionInfo(CS, TaskOutlinedRegion, CodeGen, Kind, HasCancel),
        ThreadIDVar(ThreadIDVar), Action(Action) {
    assert(ThreadIDVar != nullptr && "No ThreadID in OpenMP region.");
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override { return ThreadIDVar; }

  /// Get an LValue for the current ThreadID variable.
  LValue getThreadIDVariableLValue(CodeGenFunction &CGF) override;

  /// Get the name of the capture helper.
  StringRef getHelperName() const override { return ".omp_outlined."; }

  void emitUntiedSwitch(CodeGenFunction &CGF) override {
    Action.emitUntiedSwitch(CGF);
  }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() ==
               TaskOutlinedRegion;
  }

private:
  /// A variable or parameter storing global thread id for OpenMP
  /// constructs.
  const VarDecl *ThreadIDVar;
  /// Action for emitting code for untied tasks.
  const UntiedTaskActionTy &Action;
};

/// API for inlined captured statement code generation in OpenMP
/// constructs.
class CGOpenMPInlinedRegionInfo : public CGOpenMPRegionInfo {
public:
  CGOpenMPInlinedRegionInfo(CodeGenFunction::CGCapturedStmtInfo *OldCSI,
                            const RegionCodeGenTy &CodeGen,
                            OpenMPDirectiveKind Kind, bool HasCancel)
      : CGOpenMPRegionInfo(InlinedRegion, CodeGen, Kind, HasCancel),
        OldCSI(OldCSI),
        OuterRegionInfo(dyn_cast_or_null<CGOpenMPRegionInfo>(OldCSI)) {}

  // Retrieve the value of the context parameter.
  llvm::Value *getContextValue() const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getContextValue();
    llvm_unreachable("No context value for inlined OpenMP region");
  }

  void setContextValue(llvm::Value *V) override {
    if (OuterRegionInfo) {
      OuterRegionInfo->setContextValue(V);
      return;
    }
    llvm_unreachable("No context value for inlined OpenMP region");
  }

  /// Lookup the captured field decl for a variable.
  const FieldDecl *lookup(const VarDecl *VD) const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->lookup(VD);
    // If there is no outer outlined region,no need to lookup in a list of
    // captured variables, we can use the original one.
    return nullptr;
  }

  FieldDecl *getThisFieldDecl() const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getThisFieldDecl();
    return nullptr;
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getThreadIDVariable();
    return nullptr;
  }

  /// Get an LValue for the current ThreadID variable.
  LValue getThreadIDVariableLValue(CodeGenFunction &CGF) override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getThreadIDVariableLValue(CGF);
    llvm_unreachable("No LValue for inlined OpenMP construct");
  }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override {
    if (auto *OuterRegionInfo = getOldCSI())
      return OuterRegionInfo->getHelperName();
    llvm_unreachable("No helper name for inlined OpenMP construct");
  }

  void emitUntiedSwitch(CodeGenFunction &CGF) override {
    if (OuterRegionInfo)
      OuterRegionInfo->emitUntiedSwitch(CGF);
  }

  CodeGenFunction::CGCapturedStmtInfo *getOldCSI() const { return OldCSI; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() == InlinedRegion;
  }

  ~CGOpenMPInlinedRegionInfo() override = default;

private:
  /// CodeGen info about outer OpenMP region.
  CodeGenFunction::CGCapturedStmtInfo *OldCSI;
  CGOpenMPRegionInfo *OuterRegionInfo;
};

/// API for captured statement code generation in OpenMP target
/// constructs. For this captures, implicit parameters are used instead of the
/// captured fields. The name of the target region has to be unique in a given
/// application so it is provided by the client, because only the client has
/// the information to generate that.
class CGOpenMPTargetRegionInfo final : public CGOpenMPRegionInfo {
public:
  CGOpenMPTargetRegionInfo(const CapturedStmt &CS,
                           const RegionCodeGenTy &CodeGen, StringRef HelperName)
      : CGOpenMPRegionInfo(CS, TargetRegion, CodeGen, OMPD_target,
                           /*HasCancel=*/false),
        HelperName(HelperName) {}

  /// This is unused for target regions because each starts executing
  /// with a single thread.
  const VarDecl *getThreadIDVariable() const override { return nullptr; }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override { return HelperName; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() == TargetRegion;
  }

private:
  StringRef HelperName;
};

static void EmptyCodeGen(CodeGenFunction &, PrePostActionTy &) {
  llvm_unreachable("No codegen for expressions");
}
/// API for generation of expressions captured in a innermost OpenMP
/// region.
class CGOpenMPInnerExprInfo final : public CGOpenMPInlinedRegionInfo {
public:
  CGOpenMPInnerExprInfo(CodeGenFunction &CGF, const CapturedStmt &CS)
      : CGOpenMPInlinedRegionInfo(CGF.CapturedStmtInfo, EmptyCodeGen,
                                  OMPD_unknown,
                                  /*HasCancel=*/false),
        PrivScope(CGF) {
    // Make sure the globals captured in the provided statement are local by
    // using the privatization logic. We assume the same variable is not
    // captured more than once.
    for (const auto &C : CS.captures()) {
      if (!C.capturesVariable() && !C.capturesVariableByCopy())
        continue;

      const VarDecl *VD = C.getCapturedVar();
      if (VD->isLocalVarDeclOrParm())
        continue;

      DeclRefExpr DRE(CGF.getContext(), const_cast<VarDecl *>(VD),
                      /*RefersToEnclosingVariableOrCapture=*/false,
                      VD->getType().getNonReferenceType(), VK_LValue,
                      C.getLocation());
      PrivScope.addPrivate(VD, CGF.EmitLValue(&DRE).getAddress());
    }
    (void)PrivScope.Privatize();
  }

  /// Lookup the captured field decl for a variable.
  const FieldDecl *lookup(const VarDecl *VD) const override {
    if (const FieldDecl *FD = CGOpenMPInlinedRegionInfo::lookup(VD))
      return FD;
    return nullptr;
  }

  /// Emit the captured statement body.
  void EmitBody(CodeGenFunction &CGF, const Stmt *S) override {
    llvm_unreachable("No body for expressions");
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override {
    llvm_unreachable("No thread id for expressions");
  }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override {
    llvm_unreachable("No helper name for expressions");
  }

  static bool classof(const CGCapturedStmtInfo *Info) { return false; }

private:
  /// Private scope to capture global variables.
  CodeGenFunction::OMPPrivateScope PrivScope;
};

/// RAII for emitting code of OpenMP constructs.
class InlinedOpenMPRegionRAII {
  CodeGenFunction &CGF;
  llvm::DenseMap<const ValueDecl *, FieldDecl *> LambdaCaptureFields;
  FieldDecl *LambdaThisCaptureField = nullptr;
  const CodeGen::CGBlockInfo *BlockInfo = nullptr;
  bool NoInheritance = false;

public:
  /// Constructs region for combined constructs.
  /// \param CodeGen Code generation sequence for combined directives. Includes
  /// a list of functions used for code generation of implicitly inlined
  /// regions.
  InlinedOpenMPRegionRAII(CodeGenFunction &CGF, const RegionCodeGenTy &CodeGen,
                          OpenMPDirectiveKind Kind, bool HasCancel,
                          bool NoInheritance = true)
      : CGF(CGF), NoInheritance(NoInheritance) {
    // Start emission for the construct.
    CGF.CapturedStmtInfo = new CGOpenMPInlinedRegionInfo(
        CGF.CapturedStmtInfo, CodeGen, Kind, HasCancel);
    if (NoInheritance) {
      std::swap(CGF.LambdaCaptureFields, LambdaCaptureFields);
      LambdaThisCaptureField = CGF.LambdaThisCaptureField;
      CGF.LambdaThisCaptureField = nullptr;
      BlockInfo = CGF.BlockInfo;
      CGF.BlockInfo = nullptr;
    }
  }

  ~InlinedOpenMPRegionRAII() {
    // Restore original CapturedStmtInfo only if we're done with code emission.
    auto *OldCSI =
        cast<CGOpenMPInlinedRegionInfo>(CGF.CapturedStmtInfo)->getOldCSI();
    delete CGF.CapturedStmtInfo;
    CGF.CapturedStmtInfo = OldCSI;
    if (NoInheritance) {
      std::swap(CGF.LambdaCaptureFields, LambdaCaptureFields);
      CGF.LambdaThisCaptureField = LambdaThisCaptureField;
      CGF.BlockInfo = BlockInfo;
    }
  }
};

/// Values for bit flags used in the ident_t to describe the fields.
/// All enumeric elements are named and described in accordance with the code
/// from https://github.com/llvm/llvm-project/blob/main/openmp/runtime/src/kmp.h
enum OpenMPLocationFlags : unsigned {
  /// Use trampoline for internal microtask.
  OMP_IDENT_IMD = 0x01,
  /// Use c-style ident structure.
  OMP_IDENT_KMPC = 0x02,
  /// Atomic reduction option for kmpc_reduce.
  OMP_ATOMIC_REDUCE = 0x10,
  /// Explicit 'barrier' directive.
  OMP_IDENT_BARRIER_EXPL = 0x20,
  /// Implicit barrier in code.
  OMP_IDENT_BARRIER_IMPL = 0x40,
  /// Implicit barrier in 'for' directive.
  OMP_IDENT_BARRIER_IMPL_FOR = 0x40,
  /// Implicit barrier in 'sections' directive.
  OMP_IDENT_BARRIER_IMPL_SECTIONS = 0xC0,
  /// Implicit barrier in 'single' directive.
  OMP_IDENT_BARRIER_IMPL_SINGLE = 0x140,
  /// Call of __kmp_for_static_init for static loop.
  OMP_IDENT_WORK_LOOP = 0x200,
  /// Call of __kmp_for_static_init for sections.
  OMP_IDENT_WORK_SECTIONS = 0x400,
  /// Call of __kmp_for_static_init for distribute.
  OMP_IDENT_WORK_DISTRIBUTE = 0x800,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/OMP_IDENT_WORK_DISTRIBUTE)
};

/// Describes ident structure that describes a source location.
/// All descriptions are taken from
/// https://github.com/llvm/llvm-project/blob/main/openmp/runtime/src/kmp.h
/// Original structure:
/// typedef struct ident {
///    kmp_int32 reserved_1;   /**<  might be used in Fortran;
///                                  see above  */
///    kmp_int32 flags;        /**<  also f.flags; KMP_IDENT_xxx flags;
///                                  KMP_IDENT_KMPC identifies this union
///                                  member  */
///    kmp_int32 reserved_2;   /**<  not really used in Fortran any more;
///                                  see above */
///#if USE_ITT_BUILD
///                            /*  but currently used for storing
///                                region-specific ITT */
///                            /*  contextual information. */
///#endif /* USE_ITT_BUILD */
///    kmp_int32 reserved_3;   /**< source[4] in Fortran, do not use for
///                                 C++  */
///    char const *psource;    /**< String describing the source location.
///                            The string is composed of semi-colon separated
//                             fields which describe the source file,
///                            the function and a pair of line numbers that
///                            delimit the construct.
///                             */
/// } ident_t;
enum IdentFieldIndex {
  /// might be used in Fortran
  IdentField_Reserved_1,
  /// OMP_IDENT_xxx flags; OMP_IDENT_KMPC identifies this union member.
  IdentField_Flags,
  /// Not really used in Fortran any more
  IdentField_Reserved_2,
  /// Source[4] in Fortran, do not use for C++
  IdentField_Reserved_3,
  /// String describing the source location. The string is composed of
  /// semi-colon separated fields which describe the source file, the function
  /// and a pair of line numbers that delimit the construct.
  IdentField_PSource
};

/// Schedule types for 'omp for' loops (these enumerators are taken from
/// the enum sched_type in kmp.h).
enum OpenMPSchedType {
  /// Lower bound for default (unordered) versions.
  OMP_sch_lower = 32,
  OMP_sch_static_chunked = 33,
  OMP_sch_static = 34,
  OMP_sch_dynamic_chunked = 35,
  OMP_sch_guided_chunked = 36,
  OMP_sch_runtime = 37,
  OMP_sch_auto = 38,
  /// static with chunk adjustment (e.g., simd)
  OMP_sch_static_balanced_chunked = 45,
  /// Lower bound for 'ordered' versions.
  OMP_ord_lower = 64,
  OMP_ord_static_chunked = 65,
  OMP_ord_static = 66,
  OMP_ord_dynamic_chunked = 67,
  OMP_ord_guided_chunked = 68,
  OMP_ord_runtime = 69,
  OMP_ord_auto = 70,
  OMP_sch_default = OMP_sch_static,
  /// dist_schedule types
  OMP_dist_sch_static_chunked = 91,
  OMP_dist_sch_static = 92,
  /// Support for OpenMP 4.5 monotonic and nonmonotonic schedule modifiers.
  /// Set if the monotonic schedule modifier was present.
  OMP_sch_modifier_monotonic = (1 << 29),
  /// Set if the nonmonotonic schedule modifier was present.
  OMP_sch_modifier_nonmonotonic = (1 << 30),
};

/// A basic class for pre|post-action for advanced codegen sequence for OpenMP
/// region.
class CleanupTy final : public EHScopeStack::Cleanup {
  PrePostActionTy *Action;

public:
  explicit CleanupTy(PrePostActionTy *Action) : Action(Action) {}
  void Emit(CodeGenFunction &CGF, Flags /*flags*/) override {
    if (!CGF.HaveInsertPoint())
      return;
    Action->Exit(CGF);
  }
};

} // anonymous namespace

void RegionCodeGenTy::operator()(CodeGenFunction &CGF) const {
  CodeGenFunction::RunCleanupsScope Scope(CGF);
  if (PrePostAction) {
    CGF.EHStack.pushCleanup<CleanupTy>(NormalAndEHCleanup, PrePostAction);
    Callback(CodeGen, CGF, *PrePostAction);
  } else {
    PrePostActionTy Action;
    Callback(CodeGen, CGF, Action);
  }
}

/// Check if the combiner is a call to UDR combiner and if it is so return the
/// UDR decl used for reduction.
static const OMPDeclareReductionDecl *
getReductionInit(const Expr *ReductionOp) {
  if (const auto *CE = dyn_cast<CallExpr>(ReductionOp))
    if (const auto *OVE = dyn_cast<OpaqueValueExpr>(CE->getCallee()))
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(OVE->getSourceExpr()->IgnoreImpCasts()))
        if (const auto *DRD = dyn_cast<OMPDeclareReductionDecl>(DRE->getDecl()))
          return DRD;
  return nullptr;
}

static void emitInitWithReductionInitializer(CodeGenFunction &CGF,
                                             const OMPDeclareReductionDecl *DRD,
                                             const Expr *InitOp,
                                             Address Private, Address Original,
                                             QualType Ty) {
  if (DRD->getInitializer()) {
    std::pair<llvm::Function *, llvm::Function *> Reduction =
        CGF.CGM.getOpenMPRuntime().getUserDefinedReduction(DRD);
    const auto *CE = cast<CallExpr>(InitOp);
    const auto *OVE = cast<OpaqueValueExpr>(CE->getCallee());
    const Expr *LHS = CE->getArg(/*Arg=*/0)->IgnoreParenImpCasts();
    const Expr *RHS = CE->getArg(/*Arg=*/1)->IgnoreParenImpCasts();
    const auto *LHSDRE =
        cast<DeclRefExpr>(cast<UnaryOperator>(LHS)->getSubExpr());
    const auto *RHSDRE =
        cast<DeclRefExpr>(cast<UnaryOperator>(RHS)->getSubExpr());
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    PrivateScope.addPrivate(cast<VarDecl>(LHSDRE->getDecl()), Private);
    PrivateScope.addPrivate(cast<VarDecl>(RHSDRE->getDecl()), Original);
    (void)PrivateScope.Privatize();
    RValue Func = RValue::get(Reduction.second);
    CodeGenFunction::OpaqueValueMapping Map(CGF, OVE, Func);
    CGF.EmitIgnoredExpr(InitOp);
  } else {
    llvm::Constant *Init = CGF.CGM.EmitNullConstant(Ty);
    std::string Name = CGF.CGM.getOpenMPRuntime().getName({"init"});
    auto *GV = new llvm::GlobalVariable(
        CGF.CGM.getModule(), Init->getType(), /*isConstant=*/true,
        llvm::GlobalValue::PrivateLinkage, Init, Name);
    LValue LV = CGF.MakeNaturalAlignRawAddrLValue(GV, Ty);
    RValue InitRVal;
    switch (CGF.getEvaluationKind(Ty)) {
    case TEK_Scalar:
      InitRVal = CGF.EmitLoadOfLValue(LV, DRD->getLocation());
      break;
    case TEK_Complex:
      InitRVal =
          RValue::getComplex(CGF.EmitLoadOfComplex(LV, DRD->getLocation()));
      break;
    case TEK_Aggregate: {
      OpaqueValueExpr OVE(DRD->getLocation(), Ty, VK_LValue);
      CodeGenFunction::OpaqueValueMapping OpaqueMap(CGF, &OVE, LV);
      CGF.EmitAnyExprToMem(&OVE, Private, Ty.getQualifiers(),
                           /*IsInitializer=*/false);
      return;
    }
    }
    OpaqueValueExpr OVE(DRD->getLocation(), Ty, VK_PRValue);
    CodeGenFunction::OpaqueValueMapping OpaqueMap(CGF, &OVE, InitRVal);
    CGF.EmitAnyExprToMem(&OVE, Private, Ty.getQualifiers(),
                         /*IsInitializer=*/false);
  }
}

/// Emit initialization of arrays of complex types.
/// \param DestAddr Address of the array.
/// \param Type Type of array.
/// \param Init Initial expression of array.
/// \param SrcAddr Address of the original array.
static void EmitOMPAggregateInit(CodeGenFunction &CGF, Address DestAddr,
                                 QualType Type, bool EmitDeclareReductionInit,
                                 const Expr *Init,
                                 const OMPDeclareReductionDecl *DRD,
                                 Address SrcAddr = Address::invalid()) {
  // Perform element-by-element initialization.
  QualType ElementTy;

  // Drill down to the base element type on both arrays.
  const ArrayType *ArrayTy = Type->getAsArrayTypeUnsafe();
  llvm::Value *NumElements = CGF.emitArrayLength(ArrayTy, ElementTy, DestAddr);
  if (DRD)
    SrcAddr = SrcAddr.withElementType(DestAddr.getElementType());

  llvm::Value *SrcBegin = nullptr;
  if (DRD)
    SrcBegin = SrcAddr.emitRawPointer(CGF);
  llvm::Value *DestBegin = DestAddr.emitRawPointer(CGF);
  // Cast from pointer to array type to pointer to single element.
  llvm::Value *DestEnd =
      CGF.Builder.CreateGEP(DestAddr.getElementType(), DestBegin, NumElements);
  // The basic structure here is a while-do loop.
  llvm::BasicBlock *BodyBB = CGF.createBasicBlock("omp.arrayinit.body");
  llvm::BasicBlock *DoneBB = CGF.createBasicBlock("omp.arrayinit.done");
  llvm::Value *IsEmpty =
      CGF.Builder.CreateICmpEQ(DestBegin, DestEnd, "omp.arrayinit.isempty");
  CGF.Builder.CreateCondBr(IsEmpty, DoneBB, BodyBB);

  // Enter the loop body, making that address the current address.
  llvm::BasicBlock *EntryBB = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(BodyBB);

  CharUnits ElementSize = CGF.getContext().getTypeSizeInChars(ElementTy);

  llvm::PHINode *SrcElementPHI = nullptr;
  Address SrcElementCurrent = Address::invalid();
  if (DRD) {
    SrcElementPHI = CGF.Builder.CreatePHI(SrcBegin->getType(), 2,
                                          "omp.arraycpy.srcElementPast");
    SrcElementPHI->addIncoming(SrcBegin, EntryBB);
    SrcElementCurrent =
        Address(SrcElementPHI, SrcAddr.getElementType(),
                SrcAddr.getAlignment().alignmentOfArrayElement(ElementSize));
  }
  llvm::PHINode *DestElementPHI = CGF.Builder.CreatePHI(
      DestBegin->getType(), 2, "omp.arraycpy.destElementPast");
  DestElementPHI->addIncoming(DestBegin, EntryBB);
  Address DestElementCurrent =
      Address(DestElementPHI, DestAddr.getElementType(),
              DestAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  // Emit copy.
  {
    CodeGenFunction::RunCleanupsScope InitScope(CGF);
    if (EmitDeclareReductionInit) {
      emitInitWithReductionInitializer(CGF, DRD, Init, DestElementCurrent,
                                       SrcElementCurrent, ElementTy);
    } else
      CGF.EmitAnyExprToMem(Init, DestElementCurrent, ElementTy.getQualifiers(),
                           /*IsInitializer=*/false);
  }

  if (DRD) {
    // Shift the address forward by one element.
    llvm::Value *SrcElementNext = CGF.Builder.CreateConstGEP1_32(
        SrcAddr.getElementType(), SrcElementPHI, /*Idx0=*/1,
        "omp.arraycpy.dest.element");
    SrcElementPHI->addIncoming(SrcElementNext, CGF.Builder.GetInsertBlock());
  }

  // Shift the address forward by one element.
  llvm::Value *DestElementNext = CGF.Builder.CreateConstGEP1_32(
      DestAddr.getElementType(), DestElementPHI, /*Idx0=*/1,
      "omp.arraycpy.dest.element");
  // Check whether we've reached the end.
  llvm::Value *Done =
      CGF.Builder.CreateICmpEQ(DestElementNext, DestEnd, "omp.arraycpy.done");
  CGF.Builder.CreateCondBr(Done, DoneBB, BodyBB);
  DestElementPHI->addIncoming(DestElementNext, CGF.Builder.GetInsertBlock());

  // Done.
  CGF.EmitBlock(DoneBB, /*IsFinished=*/true);
}

LValue ReductionCodeGen::emitSharedLValue(CodeGenFunction &CGF, const Expr *E) {
  return CGF.EmitOMPSharedLValue(E);
}

LValue ReductionCodeGen::emitSharedLValueUB(CodeGenFunction &CGF,
                                            const Expr *E) {
  if (const auto *OASE = dyn_cast<ArraySectionExpr>(E))
    return CGF.EmitArraySectionExpr(OASE, /*IsLowerBound=*/false);
  return LValue();
}

void ReductionCodeGen::emitAggregateInitialization(
    CodeGenFunction &CGF, unsigned N, Address PrivateAddr, Address SharedAddr,
    const OMPDeclareReductionDecl *DRD) {
  // Emit VarDecl with copy init for arrays.
  // Get the address of the original variable captured in current
  // captured region.
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  bool EmitDeclareReductionInit =
      DRD && (DRD->getInitializer() || !PrivateVD->hasInit());
  EmitOMPAggregateInit(CGF, PrivateAddr, PrivateVD->getType(),
                       EmitDeclareReductionInit,
                       EmitDeclareReductionInit ? ClausesData[N].ReductionOp
                                                : PrivateVD->getInit(),
                       DRD, SharedAddr);
}

ReductionCodeGen::ReductionCodeGen(ArrayRef<const Expr *> Shareds,
                                   ArrayRef<const Expr *> Origs,
                                   ArrayRef<const Expr *> Privates,
                                   ArrayRef<const Expr *> ReductionOps) {
  ClausesData.reserve(Shareds.size());
  SharedAddresses.reserve(Shareds.size());
  Sizes.reserve(Shareds.size());
  BaseDecls.reserve(Shareds.size());
  const auto *IOrig = Origs.begin();
  const auto *IPriv = Privates.begin();
  const auto *IRed = ReductionOps.begin();
  for (const Expr *Ref : Shareds) {
    ClausesData.emplace_back(Ref, *IOrig, *IPriv, *IRed);
    std::advance(IOrig, 1);
    std::advance(IPriv, 1);
    std::advance(IRed, 1);
  }
}

void ReductionCodeGen::emitSharedOrigLValue(CodeGenFunction &CGF, unsigned N) {
  assert(SharedAddresses.size() == N && OrigAddresses.size() == N &&
         "Number of generated lvalues must be exactly N.");
  LValue First = emitSharedLValue(CGF, ClausesData[N].Shared);
  LValue Second = emitSharedLValueUB(CGF, ClausesData[N].Shared);
  SharedAddresses.emplace_back(First, Second);
  if (ClausesData[N].Shared == ClausesData[N].Ref) {
    OrigAddresses.emplace_back(First, Second);
  } else {
    LValue First = emitSharedLValue(CGF, ClausesData[N].Ref);
    LValue Second = emitSharedLValueUB(CGF, ClausesData[N].Ref);
    OrigAddresses.emplace_back(First, Second);
  }
}

void ReductionCodeGen::emitAggregateType(CodeGenFunction &CGF, unsigned N) {
  QualType PrivateType = getPrivateType(N);
  bool AsArraySection = isa<ArraySectionExpr>(ClausesData[N].Ref);
  if (!PrivateType->isVariablyModifiedType()) {
    Sizes.emplace_back(
        CGF.getTypeSize(OrigAddresses[N].first.getType().getNonReferenceType()),
        nullptr);
    return;
  }
  llvm::Value *Size;
  llvm::Value *SizeInChars;
  auto *ElemType = OrigAddresses[N].first.getAddress().getElementType();
  auto *ElemSizeOf = llvm::ConstantExpr::getSizeOf(ElemType);
  if (AsArraySection) {
    Size = CGF.Builder.CreatePtrDiff(ElemType,
                                     OrigAddresses[N].second.getPointer(CGF),
                                     OrigAddresses[N].first.getPointer(CGF));
    Size = CGF.Builder.CreateNUWAdd(
        Size, llvm::ConstantInt::get(Size->getType(), /*V=*/1));
    SizeInChars = CGF.Builder.CreateNUWMul(Size, ElemSizeOf);
  } else {
    SizeInChars =
        CGF.getTypeSize(OrigAddresses[N].first.getType().getNonReferenceType());
    Size = CGF.Builder.CreateExactUDiv(SizeInChars, ElemSizeOf);
  }
  Sizes.emplace_back(SizeInChars, Size);
  CodeGenFunction::OpaqueValueMapping OpaqueMap(
      CGF,
      cast<OpaqueValueExpr>(
          CGF.getContext().getAsVariableArrayType(PrivateType)->getSizeExpr()),
      RValue::get(Size));
  CGF.EmitVariablyModifiedType(PrivateType);
}

void ReductionCodeGen::emitAggregateType(CodeGenFunction &CGF, unsigned N,
                                         llvm::Value *Size) {
  QualType PrivateType = getPrivateType(N);
  if (!PrivateType->isVariablyModifiedType()) {
    assert(!Size && !Sizes[N].second &&
           "Size should be nullptr for non-variably modified reduction "
           "items.");
    return;
  }
  CodeGenFunction::OpaqueValueMapping OpaqueMap(
      CGF,
      cast<OpaqueValueExpr>(
          CGF.getContext().getAsVariableArrayType(PrivateType)->getSizeExpr()),
      RValue::get(Size));
  CGF.EmitVariablyModifiedType(PrivateType);
}

void ReductionCodeGen::emitInitialization(
    CodeGenFunction &CGF, unsigned N, Address PrivateAddr, Address SharedAddr,
    llvm::function_ref<bool(CodeGenFunction &)> DefaultInit) {
  assert(SharedAddresses.size() > N && "No variable was generated");
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  const OMPDeclareReductionDecl *DRD =
      getReductionInit(ClausesData[N].ReductionOp);
  if (CGF.getContext().getAsArrayType(PrivateVD->getType())) {
    if (DRD && DRD->getInitializer())
      (void)DefaultInit(CGF);
    emitAggregateInitialization(CGF, N, PrivateAddr, SharedAddr, DRD);
  } else if (DRD && (DRD->getInitializer() || !PrivateVD->hasInit())) {
    (void)DefaultInit(CGF);
    QualType SharedType = SharedAddresses[N].first.getType();
    emitInitWithReductionInitializer(CGF, DRD, ClausesData[N].ReductionOp,
                                     PrivateAddr, SharedAddr, SharedType);
  } else if (!DefaultInit(CGF) && PrivateVD->hasInit() &&
             !CGF.isTrivialInitializer(PrivateVD->getInit())) {
    CGF.EmitAnyExprToMem(PrivateVD->getInit(), PrivateAddr,
                         PrivateVD->getType().getQualifiers(),
                         /*IsInitializer=*/false);
  }
}

bool ReductionCodeGen::needCleanups(unsigned N) {
  QualType PrivateType = getPrivateType(N);
  QualType::DestructionKind DTorKind = PrivateType.isDestructedType();
  return DTorKind != QualType::DK_none;
}

void ReductionCodeGen::emitCleanups(CodeGenFunction &CGF, unsigned N,
                                    Address PrivateAddr) {
  QualType PrivateType = getPrivateType(N);
  QualType::DestructionKind DTorKind = PrivateType.isDestructedType();
  if (needCleanups(N)) {
    PrivateAddr =
        PrivateAddr.withElementType(CGF.ConvertTypeForMem(PrivateType));
    CGF.pushDestroy(DTorKind, PrivateAddr, PrivateType);
  }
}

static LValue loadToBegin(CodeGenFunction &CGF, QualType BaseTy, QualType ElTy,
                          LValue BaseLV) {
  BaseTy = BaseTy.getNonReferenceType();
  while ((BaseTy->isPointerType() || BaseTy->isReferenceType()) &&
         !CGF.getContext().hasSameType(BaseTy, ElTy)) {
    if (const auto *PtrTy = BaseTy->getAs<PointerType>()) {
      BaseLV = CGF.EmitLoadOfPointerLValue(BaseLV.getAddress(), PtrTy);
    } else {
      LValue RefLVal = CGF.MakeAddrLValue(BaseLV.getAddress(), BaseTy);
      BaseLV = CGF.EmitLoadOfReferenceLValue(RefLVal);
    }
    BaseTy = BaseTy->getPointeeType();
  }
  return CGF.MakeAddrLValue(
      BaseLV.getAddress().withElementType(CGF.ConvertTypeForMem(ElTy)),
      BaseLV.getType(), BaseLV.getBaseInfo(),
      CGF.CGM.getTBAAInfoForSubobject(BaseLV, BaseLV.getType()));
}

static Address castToBase(CodeGenFunction &CGF, QualType BaseTy, QualType ElTy,
                          Address OriginalBaseAddress, llvm::Value *Addr) {
  RawAddress Tmp = RawAddress::invalid();
  Address TopTmp = Address::invalid();
  Address MostTopTmp = Address::invalid();
  BaseTy = BaseTy.getNonReferenceType();
  while ((BaseTy->isPointerType() || BaseTy->isReferenceType()) &&
         !CGF.getContext().hasSameType(BaseTy, ElTy)) {
    Tmp = CGF.CreateMemTemp(BaseTy);
    if (TopTmp.isValid())
      CGF.Builder.CreateStore(Tmp.getPointer(), TopTmp);
    else
      MostTopTmp = Tmp;
    TopTmp = Tmp;
    BaseTy = BaseTy->getPointeeType();
  }

  if (Tmp.isValid()) {
    Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        Addr, Tmp.getElementType());
    CGF.Builder.CreateStore(Addr, Tmp);
    return MostTopTmp;
  }

  Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      Addr, OriginalBaseAddress.getType());
  return OriginalBaseAddress.withPointer(Addr, NotKnownNonNull);
}

static const VarDecl *getBaseDecl(const Expr *Ref, const DeclRefExpr *&DE) {
  const VarDecl *OrigVD = nullptr;
  if (const auto *OASE = dyn_cast<ArraySectionExpr>(Ref)) {
    const Expr *Base = OASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempOASE = dyn_cast<ArraySectionExpr>(Base))
      Base = TempOASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
      Base = TempASE->getBase()->IgnoreParenImpCasts();
    DE = cast<DeclRefExpr>(Base);
    OrigVD = cast<VarDecl>(DE->getDecl());
  } else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Ref)) {
    const Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
      Base = TempASE->getBase()->IgnoreParenImpCasts();
    DE = cast<DeclRefExpr>(Base);
    OrigVD = cast<VarDecl>(DE->getDecl());
  }
  return OrigVD;
}

Address ReductionCodeGen::adjustPrivateAddress(CodeGenFunction &CGF, unsigned N,
                                               Address PrivateAddr) {
  const DeclRefExpr *DE;
  if (const VarDecl *OrigVD = ::getBaseDecl(ClausesData[N].Ref, DE)) {
    BaseDecls.emplace_back(OrigVD);
    LValue OriginalBaseLValue = CGF.EmitLValue(DE);
    LValue BaseLValue =
        loadToBegin(CGF, OrigVD->getType(), SharedAddresses[N].first.getType(),
                    OriginalBaseLValue);
    Address SharedAddr = SharedAddresses[N].first.getAddress();
    llvm::Value *Adjustment = CGF.Builder.CreatePtrDiff(
        SharedAddr.getElementType(), BaseLValue.getPointer(CGF),
        SharedAddr.emitRawPointer(CGF));
    llvm::Value *PrivatePointer =
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            PrivateAddr.emitRawPointer(CGF), SharedAddr.getType());
    llvm::Value *Ptr = CGF.Builder.CreateGEP(
        SharedAddr.getElementType(), PrivatePointer, Adjustment);
    return castToBase(CGF, OrigVD->getType(),
                      SharedAddresses[N].first.getType(),
                      OriginalBaseLValue.getAddress(), Ptr);
  }
  BaseDecls.emplace_back(
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Ref)->getDecl()));
  return PrivateAddr;
}

bool ReductionCodeGen::usesReductionInitializer(unsigned N) const {
  const OMPDeclareReductionDecl *DRD =
      getReductionInit(ClausesData[N].ReductionOp);
  return DRD && DRD->getInitializer();
}

LValue CGOpenMPRegionInfo::getThreadIDVariableLValue(CodeGenFunction &CGF) {
  return CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(getThreadIDVariable()),
      getThreadIDVariable()->getType()->castAs<PointerType>());
}

void CGOpenMPRegionInfo::EmitBody(CodeGenFunction &CGF, const Stmt *S) {
  if (!CGF.HaveInsertPoint())
    return;
  // 1.2.2 OpenMP Language Terminology
  // Structured block - An executable statement with a single entry at the
  // top and a single exit at the bottom.
  // The point of exit cannot be a branch out of the structured block.
  // longjmp() and throw() must not violate the entry/exit criteria.
  CGF.EHStack.pushTerminate();
  if (S)
    CGF.incrementProfileCounter(S);
  CodeGen(CGF);
  CGF.EHStack.popTerminate();
}

LValue CGOpenMPTaskOutlinedRegionInfo::getThreadIDVariableLValue(
    CodeGenFunction &CGF) {
  return CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(getThreadIDVariable()),
                            getThreadIDVariable()->getType(),
                            AlignmentSource::Decl);
}

static FieldDecl *addFieldToRecordDecl(ASTContext &C, DeclContext *DC,
                                       QualType FieldTy) {
  auto *Field = FieldDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Id=*/nullptr, FieldTy,
      C.getTrivialTypeSourceInfo(FieldTy, SourceLocation()),
      /*BW=*/nullptr, /*Mutable=*/false, /*InitStyle=*/ICIS_NoInit);
  Field->setAccess(AS_public);
  DC->addDecl(Field);
  return Field;
}

CGOpenMPRuntime::CGOpenMPRuntime(CodeGenModule &CGM)
    : CGM(CGM), OMPBuilder(CGM.getModule()) {
  KmpCriticalNameTy = llvm::ArrayType::get(CGM.Int32Ty, /*NumElements*/ 8);
  llvm::OpenMPIRBuilderConfig Config(
      CGM.getLangOpts().OpenMPIsTargetDevice, isGPU(),
      CGM.getLangOpts().OpenMPOffloadMandatory,
      /*HasRequiresReverseOffload*/ false, /*HasRequiresUnifiedAddress*/ false,
      hasRequiresUnifiedSharedMemory(), /*HasRequiresDynamicAllocators*/ false);
  OMPBuilder.initialize();
  OMPBuilder.loadOffloadInfoMetadata(CGM.getLangOpts().OpenMPIsTargetDevice
                                         ? CGM.getLangOpts().OMPHostIRFile
                                         : StringRef{});
  OMPBuilder.setConfig(Config);

  // The user forces the compiler to behave as if omp requires
  // unified_shared_memory was given.
  if (CGM.getLangOpts().OpenMPForceUSM) {
    HasRequiresUnifiedSharedMemory = true;
    OMPBuilder.Config.setHasRequiresUnifiedSharedMemory(true);
  }
}

void CGOpenMPRuntime::clear() {
  InternalVars.clear();
  // Clean non-target variable declarations possibly used only in debug info.
  for (const auto &Data : EmittedNonTargetVariables) {
    if (!Data.getValue().pointsToAliveValue())
      continue;
    auto *GV = dyn_cast<llvm::GlobalVariable>(Data.getValue());
    if (!GV)
      continue;
    if (!GV->isDeclaration() || GV->getNumUses() > 0)
      continue;
    GV->eraseFromParent();
  }
}

std::string CGOpenMPRuntime::getName(ArrayRef<StringRef> Parts) const {
  return OMPBuilder.createPlatformSpecificName(Parts);
}

static llvm::Function *
emitCombinerOrInitializer(CodeGenModule &CGM, QualType Ty,
                          const Expr *CombinerInitializer, const VarDecl *In,
                          const VarDecl *Out, bool IsCombiner) {
  // void .omp_combiner.(Ty *in, Ty *out);
  ASTContext &C = CGM.getContext();
  QualType PtrTy = C.getPointerType(Ty).withRestrict();
  FunctionArgList Args;
  ImplicitParamDecl OmpOutParm(C, /*DC=*/nullptr, Out->getLocation(),
                               /*Id=*/nullptr, PtrTy, ImplicitParamKind::Other);
  ImplicitParamDecl OmpInParm(C, /*DC=*/nullptr, In->getLocation(),
                              /*Id=*/nullptr, PtrTy, ImplicitParamKind::Other);
  Args.push_back(&OmpOutParm);
  Args.push_back(&OmpInParm);
  const CGFunctionInfo &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName(
      {IsCombiner ? "omp_combiner" : "omp_initializer", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  if (CGM.getLangOpts().Optimize) {
    Fn->removeFnAttr(llvm::Attribute::NoInline);
    Fn->removeFnAttr(llvm::Attribute::OptimizeNone);
    Fn->addFnAttr(llvm::Attribute::AlwaysInline);
  }
  CodeGenFunction CGF(CGM);
  // Map "T omp_in;" variable to "*omp_in_parm" value in all expressions.
  // Map "T omp_out;" variable to "*omp_out_parm" value in all expressions.
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, In->getLocation(),
                    Out->getLocation());
  CodeGenFunction::OMPPrivateScope Scope(CGF);
  Address AddrIn = CGF.GetAddrOfLocalVar(&OmpInParm);
  Scope.addPrivate(
      In, CGF.EmitLoadOfPointerLValue(AddrIn, PtrTy->castAs<PointerType>())
              .getAddress());
  Address AddrOut = CGF.GetAddrOfLocalVar(&OmpOutParm);
  Scope.addPrivate(
      Out, CGF.EmitLoadOfPointerLValue(AddrOut, PtrTy->castAs<PointerType>())
               .getAddress());
  (void)Scope.Privatize();
  if (!IsCombiner && Out->hasInit() &&
      !CGF.isTrivialInitializer(Out->getInit())) {
    CGF.EmitAnyExprToMem(Out->getInit(), CGF.GetAddrOfLocalVar(Out),
                         Out->getType().getQualifiers(),
                         /*IsInitializer=*/true);
  }
  if (CombinerInitializer)
    CGF.EmitIgnoredExpr(CombinerInitializer);
  Scope.ForceCleanup();
  CGF.FinishFunction();
  return Fn;
}

void CGOpenMPRuntime::emitUserDefinedReduction(
    CodeGenFunction *CGF, const OMPDeclareReductionDecl *D) {
  if (UDRMap.count(D) > 0)
    return;
  llvm::Function *Combiner = emitCombinerOrInitializer(
      CGM, D->getType(), D->getCombiner(),
      cast<VarDecl>(cast<DeclRefExpr>(D->getCombinerIn())->getDecl()),
      cast<VarDecl>(cast<DeclRefExpr>(D->getCombinerOut())->getDecl()),
      /*IsCombiner=*/true);
  llvm::Function *Initializer = nullptr;
  if (const Expr *Init = D->getInitializer()) {
    Initializer = emitCombinerOrInitializer(
        CGM, D->getType(),
        D->getInitializerKind() == OMPDeclareReductionInitKind::Call ? Init
                                                                     : nullptr,
        cast<VarDecl>(cast<DeclRefExpr>(D->getInitOrig())->getDecl()),
        cast<VarDecl>(cast<DeclRefExpr>(D->getInitPriv())->getDecl()),
        /*IsCombiner=*/false);
  }
  UDRMap.try_emplace(D, Combiner, Initializer);
  if (CGF) {
    auto &Decls = FunctionUDRMap.FindAndConstruct(CGF->CurFn);
    Decls.second.push_back(D);
  }
}

std::pair<llvm::Function *, llvm::Function *>
CGOpenMPRuntime::getUserDefinedReduction(const OMPDeclareReductionDecl *D) {
  auto I = UDRMap.find(D);
  if (I != UDRMap.end())
    return I->second;
  emitUserDefinedReduction(/*CGF=*/nullptr, D);
  return UDRMap.lookup(D);
}

namespace {
// Temporary RAII solution to perform a push/pop stack event on the OpenMP IR
// Builder if one is present.
struct PushAndPopStackRAII {
  PushAndPopStackRAII(llvm::OpenMPIRBuilder *OMPBuilder, CodeGenFunction &CGF,
                      bool HasCancel, llvm::omp::Directive Kind)
      : OMPBuilder(OMPBuilder) {
    if (!OMPBuilder)
      return;

    // The following callback is the crucial part of clangs cleanup process.
    //
    // NOTE:
    // Once the OpenMPIRBuilder is used to create parallel regions (and
    // similar), the cancellation destination (Dest below) is determined via
    // IP. That means if we have variables to finalize we split the block at IP,
    // use the new block (=BB) as destination to build a JumpDest (via
    // getJumpDestInCurrentScope(BB)) which then is fed to
    // EmitBranchThroughCleanup. Furthermore, there will not be the need
    // to push & pop an FinalizationInfo object.
    // The FiniCB will still be needed but at the point where the
    // OpenMPIRBuilder is asked to construct a parallel (or similar) construct.
    auto FiniCB = [&CGF](llvm::OpenMPIRBuilder::InsertPointTy IP) {
      assert(IP.getBlock()->end() == IP.getPoint() &&
             "Clang CG should cause non-terminated block!");
      CGBuilderTy::InsertPointGuard IPG(CGF.Builder);
      CGF.Builder.restoreIP(IP);
      CodeGenFunction::JumpDest Dest =
          CGF.getOMPCancelDestination(OMPD_parallel);
      CGF.EmitBranchThroughCleanup(Dest);
    };

    // TODO: Remove this once we emit parallel regions through the
    //       OpenMPIRBuilder as it can do this setup internally.
    llvm::OpenMPIRBuilder::FinalizationInfo FI({FiniCB, Kind, HasCancel});
    OMPBuilder->pushFinalizationCB(std::move(FI));
  }
  ~PushAndPopStackRAII() {
    if (OMPBuilder)
      OMPBuilder->popFinalizationCB();
  }
  llvm::OpenMPIRBuilder *OMPBuilder;
};
} // namespace

static llvm::Function *emitParallelOrTeamsOutlinedFunction(
    CodeGenModule &CGM, const OMPExecutableDirective &D, const CapturedStmt *CS,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const StringRef OutlinedHelperName, const RegionCodeGenTy &CodeGen) {
  assert(ThreadIDVar->getType()->isPointerType() &&
         "thread id variable must be of type kmp_int32 *");
  CodeGenFunction CGF(CGM, true);
  bool HasCancel = false;
  if (const auto *OPD = dyn_cast<OMPParallelDirective>(&D))
    HasCancel = OPD->hasCancel();
  else if (const auto *OPD = dyn_cast<OMPTargetParallelDirective>(&D))
    HasCancel = OPD->hasCancel();
  else if (const auto *OPSD = dyn_cast<OMPParallelSectionsDirective>(&D))
    HasCancel = OPSD->hasCancel();
  else if (const auto *OPFD = dyn_cast<OMPParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD = dyn_cast<OMPTargetParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD = dyn_cast<OMPDistributeParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD =
               dyn_cast<OMPTeamsDistributeParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD =
               dyn_cast<OMPTargetTeamsDistributeParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();

  // TODO: Temporarily inform the OpenMPIRBuilder, if any, about the new
  //       parallel region to make cancellation barriers work properly.
  llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
  PushAndPopStackRAII PSR(&OMPBuilder, CGF, HasCancel, InnermostKind);
  CGOpenMPOutlinedRegionInfo CGInfo(*CS, ThreadIDVar, CodeGen, InnermostKind,
                                    HasCancel, OutlinedHelperName);
  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
  return CGF.GenerateOpenMPCapturedStmtFunction(*CS, D.getBeginLoc());
}

std::string CGOpenMPRuntime::getOutlinedHelperName(StringRef Name) const {
  std::string Suffix = getName({"omp_outlined"});
  return (Name + Suffix).str();
}

std::string CGOpenMPRuntime::getOutlinedHelperName(CodeGenFunction &CGF) const {
  return getOutlinedHelperName(CGF.CurFn->getName());
}

std::string CGOpenMPRuntime::getReductionFuncName(StringRef Name) const {
  std::string Suffix = getName({"omp", "reduction", "reduction_func"});
  return (Name + Suffix).str();
}

llvm::Function *CGOpenMPRuntime::emitParallelOutlinedFunction(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const RegionCodeGenTy &CodeGen) {
  const CapturedStmt *CS = D.getCapturedStmt(OMPD_parallel);
  return emitParallelOrTeamsOutlinedFunction(
      CGM, D, CS, ThreadIDVar, InnermostKind, getOutlinedHelperName(CGF),
      CodeGen);
}

llvm::Function *CGOpenMPRuntime::emitTeamsOutlinedFunction(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const RegionCodeGenTy &CodeGen) {
  const CapturedStmt *CS = D.getCapturedStmt(OMPD_teams);
  return emitParallelOrTeamsOutlinedFunction(
      CGM, D, CS, ThreadIDVar, InnermostKind, getOutlinedHelperName(CGF),
      CodeGen);
}

llvm::Function *CGOpenMPRuntime::emitTaskOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    const VarDecl *PartIDVar, const VarDecl *TaskTVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen,
    bool Tied, unsigned &NumberOfParts) {
  auto &&UntiedCodeGen = [this, &D, TaskTVar](CodeGenFunction &CGF,
                                              PrePostActionTy &) {
    llvm::Value *ThreadID = getThreadID(CGF, D.getBeginLoc());
    llvm::Value *UpLoc = emitUpdateLocation(CGF, D.getBeginLoc());
    llvm::Value *TaskArgs[] = {
        UpLoc, ThreadID,
        CGF.EmitLoadOfPointerLValue(CGF.GetAddrOfLocalVar(TaskTVar),
                                    TaskTVar->getType()->castAs<PointerType>())
            .getPointer(CGF)};
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_omp_task),
                        TaskArgs);
  };
  CGOpenMPTaskOutlinedRegionInfo::UntiedTaskActionTy Action(Tied, PartIDVar,
                                                            UntiedCodeGen);
  CodeGen.setAction(Action);
  assert(!ThreadIDVar->getType()->isPointerType() &&
         "thread id variable must be of type kmp_int32 for tasks");
  const OpenMPDirectiveKind Region =
      isOpenMPTaskLoopDirective(D.getDirectiveKind()) ? OMPD_taskloop
                                                      : OMPD_task;
  const CapturedStmt *CS = D.getCapturedStmt(Region);
  bool HasCancel = false;
  if (const auto *TD = dyn_cast<OMPTaskDirective>(&D))
    HasCancel = TD->hasCancel();
  else if (const auto *TD = dyn_cast<OMPTaskLoopDirective>(&D))
    HasCancel = TD->hasCancel();
  else if (const auto *TD = dyn_cast<OMPMasterTaskLoopDirective>(&D))
    HasCancel = TD->hasCancel();
  else if (const auto *TD = dyn_cast<OMPParallelMasterTaskLoopDirective>(&D))
    HasCancel = TD->hasCancel();

  CodeGenFunction CGF(CGM, true);
  CGOpenMPTaskOutlinedRegionInfo CGInfo(*CS, ThreadIDVar, CodeGen,
                                        InnermostKind, HasCancel, Action);
  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
  llvm::Function *Res = CGF.GenerateCapturedStmtFunction(*CS);
  if (!Tied)
    NumberOfParts = Action.getNumberOfParts();
  return Res;
}

void CGOpenMPRuntime::setLocThreadIdInsertPt(CodeGenFunction &CGF,
                                             bool AtCurrentPoint) {
  auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
  assert(!Elem.second.ServiceInsertPt && "Insert point is set already.");

  llvm::Value *Undef = llvm::UndefValue::get(CGF.Int32Ty);
  if (AtCurrentPoint) {
    Elem.second.ServiceInsertPt = new llvm::BitCastInst(
        Undef, CGF.Int32Ty, "svcpt", CGF.Builder.GetInsertBlock());
  } else {
    Elem.second.ServiceInsertPt =
        new llvm::BitCastInst(Undef, CGF.Int32Ty, "svcpt");
    Elem.second.ServiceInsertPt->insertAfter(CGF.AllocaInsertPt);
  }
}

void CGOpenMPRuntime::clearLocThreadIdInsertPt(CodeGenFunction &CGF) {
  auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
  if (Elem.second.ServiceInsertPt) {
    llvm::Instruction *Ptr = Elem.second.ServiceInsertPt;
    Elem.second.ServiceInsertPt = nullptr;
    Ptr->eraseFromParent();
  }
}

static StringRef getIdentStringFromSourceLocation(CodeGenFunction &CGF,
                                                  SourceLocation Loc,
                                                  SmallString<128> &Buffer) {
  llvm::raw_svector_ostream OS(Buffer);
  // Build debug location
  PresumedLoc PLoc = CGF.getContext().getSourceManager().getPresumedLoc(Loc);
  OS << ";" << PLoc.getFilename() << ";";
  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(CGF.CurFuncDecl))
    OS << FD->getQualifiedNameAsString();
  OS << ";" << PLoc.getLine() << ";" << PLoc.getColumn() << ";;";
  return OS.str();
}

llvm::Value *CGOpenMPRuntime::emitUpdateLocation(CodeGenFunction &CGF,
                                                 SourceLocation Loc,
                                                 unsigned Flags, bool EmitLoc) {
  uint32_t SrcLocStrSize;
  llvm::Constant *SrcLocStr;
  if ((!EmitLoc && CGM.getCodeGenOpts().getDebugInfo() ==
                       llvm::codegenoptions::NoDebugInfo) ||
      Loc.isInvalid()) {
    SrcLocStr = OMPBuilder.getOrCreateDefaultSrcLocStr(SrcLocStrSize);
  } else {
    std::string FunctionName;
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(CGF.CurFuncDecl))
      FunctionName = FD->getQualifiedNameAsString();
    PresumedLoc PLoc = CGF.getContext().getSourceManager().getPresumedLoc(Loc);
    const char *FileName = PLoc.getFilename();
    unsigned Line = PLoc.getLine();
    unsigned Column = PLoc.getColumn();
    SrcLocStr = OMPBuilder.getOrCreateSrcLocStr(FunctionName, FileName, Line,
                                                Column, SrcLocStrSize);
  }
  unsigned Reserved2Flags = getDefaultLocationReserved2Flags();
  return OMPBuilder.getOrCreateIdent(
      SrcLocStr, SrcLocStrSize, llvm::omp::IdentFlag(Flags), Reserved2Flags);
}

llvm::Value *CGOpenMPRuntime::getThreadID(CodeGenFunction &CGF,
                                          SourceLocation Loc) {
  assert(CGF.CurFn && "No function in current CodeGenFunction.");
  // If the OpenMPIRBuilder is used we need to use it for all thread id calls as
  // the clang invariants used below might be broken.
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    SmallString<128> Buffer;
    OMPBuilder.updateToLocation(CGF.Builder.saveIP());
    uint32_t SrcLocStrSize;
    auto *SrcLocStr = OMPBuilder.getOrCreateSrcLocStr(
        getIdentStringFromSourceLocation(CGF, Loc, Buffer), SrcLocStrSize);
    return OMPBuilder.getOrCreateThreadID(
        OMPBuilder.getOrCreateIdent(SrcLocStr, SrcLocStrSize));
  }

  llvm::Value *ThreadID = nullptr;
  // Check whether we've already cached a load of the thread id in this
  // function.
  auto I = OpenMPLocThreadIDMap.find(CGF.CurFn);
  if (I != OpenMPLocThreadIDMap.end()) {
    ThreadID = I->second.ThreadID;
    if (ThreadID != nullptr)
      return ThreadID;
  }
  // If exceptions are enabled, do not use parameter to avoid possible crash.
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo)) {
    if (OMPRegionInfo->getThreadIDVariable()) {
      // Check if this an outlined function with thread id passed as argument.
      LValue LVal = OMPRegionInfo->getThreadIDVariableLValue(CGF);
      llvm::BasicBlock *TopBlock = CGF.AllocaInsertPt->getParent();
      if (!CGF.EHStack.requiresLandingPad() || !CGF.getLangOpts().Exceptions ||
          !CGF.getLangOpts().CXXExceptions ||
          CGF.Builder.GetInsertBlock() == TopBlock ||
          !isa<llvm::Instruction>(LVal.getPointer(CGF)) ||
          cast<llvm::Instruction>(LVal.getPointer(CGF))->getParent() ==
              TopBlock ||
          cast<llvm::Instruction>(LVal.getPointer(CGF))->getParent() ==
              CGF.Builder.GetInsertBlock()) {
        ThreadID = CGF.EmitLoadOfScalar(LVal, Loc);
        // If value loaded in entry block, cache it and use it everywhere in
        // function.
        if (CGF.Builder.GetInsertBlock() == TopBlock) {
          auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
          Elem.second.ThreadID = ThreadID;
        }
        return ThreadID;
      }
    }
  }

  // This is not an outlined function region - need to call __kmpc_int32
  // kmpc_global_thread_num(ident_t *loc).
  // Generate thread id value and cache this value for use across the
  // function.
  auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
  if (!Elem.second.ServiceInsertPt)
    setLocThreadIdInsertPt(CGF);
  CGBuilderTy::InsertPointGuard IPG(CGF.Builder);
  CGF.Builder.SetInsertPoint(Elem.second.ServiceInsertPt);
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(CGF, Loc);
  llvm::CallInst *Call = CGF.Builder.CreateCall(
      OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                            OMPRTL___kmpc_global_thread_num),
      emitUpdateLocation(CGF, Loc));
  Call->setCallingConv(CGF.getRuntimeCC());
  Elem.second.ThreadID = Call;
  return Call;
}

void CGOpenMPRuntime::functionFinished(CodeGenFunction &CGF) {
  assert(CGF.CurFn && "No function in current CodeGenFunction.");
  if (OpenMPLocThreadIDMap.count(CGF.CurFn)) {
    clearLocThreadIdInsertPt(CGF);
    OpenMPLocThreadIDMap.erase(CGF.CurFn);
  }
  if (FunctionUDRMap.count(CGF.CurFn) > 0) {
    for(const auto *D : FunctionUDRMap[CGF.CurFn])
      UDRMap.erase(D);
    FunctionUDRMap.erase(CGF.CurFn);
  }
  auto I = FunctionUDMMap.find(CGF.CurFn);
  if (I != FunctionUDMMap.end()) {
    for(const auto *D : I->second)
      UDMMap.erase(D);
    FunctionUDMMap.erase(I);
  }
  LastprivateConditionalToTypes.erase(CGF.CurFn);
  FunctionToUntiedTaskStackMap.erase(CGF.CurFn);
}

llvm::Type *CGOpenMPRuntime::getIdentTyPointerTy() {
  return OMPBuilder.IdentPtr;
}

llvm::Type *CGOpenMPRuntime::getKmpc_MicroPointerTy() {
  if (!Kmpc_MicroTy) {
    // Build void (*kmpc_micro)(kmp_int32 *global_tid, kmp_int32 *bound_tid,...)
    llvm::Type *MicroParams[] = {llvm::PointerType::getUnqual(CGM.Int32Ty),
                                 llvm::PointerType::getUnqual(CGM.Int32Ty)};
    Kmpc_MicroTy = llvm::FunctionType::get(CGM.VoidTy, MicroParams, true);
  }
  return llvm::PointerType::getUnqual(Kmpc_MicroTy);
}

llvm::OffloadEntriesInfoManager::OMPTargetDeviceClauseKind
convertDeviceClause(const VarDecl *VD) {
  std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> DevTy =
      OMPDeclareTargetDeclAttr::getDeviceType(VD);
  if (!DevTy)
    return llvm::OffloadEntriesInfoManager::OMPTargetDeviceClauseNone;

  switch ((int)*DevTy) { // Avoid -Wcovered-switch-default
  case OMPDeclareTargetDeclAttr::DT_Host:
    return llvm::OffloadEntriesInfoManager::OMPTargetDeviceClauseHost;
    break;
  case OMPDeclareTargetDeclAttr::DT_NoHost:
    return llvm::OffloadEntriesInfoManager::OMPTargetDeviceClauseNoHost;
    break;
  case OMPDeclareTargetDeclAttr::DT_Any:
    return llvm::OffloadEntriesInfoManager::OMPTargetDeviceClauseAny;
    break;
  default:
    return llvm::OffloadEntriesInfoManager::OMPTargetDeviceClauseNone;
    break;
  }
}

llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryKind
convertCaptureClause(const VarDecl *VD) {
  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> MapType =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
  if (!MapType)
    return llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryNone;
  switch ((int)*MapType) { // Avoid -Wcovered-switch-default
  case OMPDeclareTargetDeclAttr::MapTypeTy::MT_To:
    return llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryTo;
    break;
  case OMPDeclareTargetDeclAttr::MapTypeTy::MT_Enter:
    return llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryEnter;
    break;
  case OMPDeclareTargetDeclAttr::MapTypeTy::MT_Link:
    return llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryLink;
    break;
  default:
    return llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryNone;
    break;
  }
}

static llvm::TargetRegionEntryInfo getEntryInfoFromPresumedLoc(
    CodeGenModule &CGM, llvm::OpenMPIRBuilder &OMPBuilder,
    SourceLocation BeginLoc, llvm::StringRef ParentName = "") {

  auto FileInfoCallBack = [&]() {
    SourceManager &SM = CGM.getContext().getSourceManager();
    PresumedLoc PLoc = SM.getPresumedLoc(BeginLoc);

    llvm::sys::fs::UniqueID ID;
    if (llvm::sys::fs::getUniqueID(PLoc.getFilename(), ID)) {
      PLoc = SM.getPresumedLoc(BeginLoc, /*UseLineDirectives=*/false);
    }

    return std::pair<std::string, uint64_t>(PLoc.getFilename(), PLoc.getLine());
  };

  return OMPBuilder.getTargetEntryUniqueInfo(FileInfoCallBack, ParentName);
}

ConstantAddress CGOpenMPRuntime::getAddrOfDeclareTargetVar(const VarDecl *VD) {
  auto AddrOfGlobal = [&VD, this]() { return CGM.GetAddrOfGlobal(VD); };

  auto LinkageForVariable = [&VD, this]() {
    return CGM.getLLVMLinkageVarDefinition(VD);
  };

  std::vector<llvm::GlobalVariable *> GeneratedRefs;

  llvm::Type *LlvmPtrTy = CGM.getTypes().ConvertTypeForMem(
      CGM.getContext().getPointerType(VD->getType()));
  llvm::Constant *addr = OMPBuilder.getAddrOfDeclareTargetVar(
      convertCaptureClause(VD), convertDeviceClause(VD),
      VD->hasDefinition(CGM.getContext()) == VarDecl::DeclarationOnly,
      VD->isExternallyVisible(),
      getEntryInfoFromPresumedLoc(CGM, OMPBuilder,
                                  VD->getCanonicalDecl()->getBeginLoc()),
      CGM.getMangledName(VD), GeneratedRefs, CGM.getLangOpts().OpenMPSimd,
      CGM.getLangOpts().OMPTargetTriples, LlvmPtrTy, AddrOfGlobal,
      LinkageForVariable);

  if (!addr)
    return ConstantAddress::invalid();
  return ConstantAddress(addr, LlvmPtrTy, CGM.getContext().getDeclAlign(VD));
}

llvm::Constant *
CGOpenMPRuntime::getOrCreateThreadPrivateCache(const VarDecl *VD) {
  assert(!CGM.getLangOpts().OpenMPUseTLS ||
         !CGM.getContext().getTargetInfo().isTLSSupported());
  // Lookup the entry, lazily creating it if necessary.
  std::string Suffix = getName({"cache", ""});
  return OMPBuilder.getOrCreateInternalVariable(
      CGM.Int8PtrPtrTy, Twine(CGM.getMangledName(VD)).concat(Suffix).str());
}

Address CGOpenMPRuntime::getAddrOfThreadPrivate(CodeGenFunction &CGF,
                                                const VarDecl *VD,
                                                Address VDAddr,
                                                SourceLocation Loc) {
  if (CGM.getLangOpts().OpenMPUseTLS &&
      CGM.getContext().getTargetInfo().isTLSSupported())
    return VDAddr;

  llvm::Type *VarTy = VDAddr.getElementType();
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      CGF.Builder.CreatePointerCast(VDAddr.emitRawPointer(CGF), CGM.Int8PtrTy),
      CGM.getSize(CGM.GetTargetTypeStoreSize(VarTy)),
      getOrCreateThreadPrivateCache(VD)};
  return Address(
      CGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(
              CGM.getModule(), OMPRTL___kmpc_threadprivate_cached),
          Args),
      CGF.Int8Ty, VDAddr.getAlignment());
}

void CGOpenMPRuntime::emitThreadPrivateVarInit(
    CodeGenFunction &CGF, Address VDAddr, llvm::Value *Ctor,
    llvm::Value *CopyCtor, llvm::Value *Dtor, SourceLocation Loc) {
  // Call kmp_int32 __kmpc_global_thread_num(&loc) to init OpenMP runtime
  // library.
  llvm::Value *OMPLoc = emitUpdateLocation(CGF, Loc);
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_global_thread_num),
                      OMPLoc);
  // Call __kmpc_threadprivate_register(&loc, &var, ctor, cctor/*NULL*/, dtor)
  // to register constructor/destructor for variable.
  llvm::Value *Args[] = {
      OMPLoc,
      CGF.Builder.CreatePointerCast(VDAddr.emitRawPointer(CGF), CGM.VoidPtrTy),
      Ctor, CopyCtor, Dtor};
  CGF.EmitRuntimeCall(
      OMPBuilder.getOrCreateRuntimeFunction(
          CGM.getModule(), OMPRTL___kmpc_threadprivate_register),
      Args);
}

llvm::Function *CGOpenMPRuntime::emitThreadPrivateVarDefinition(
    const VarDecl *VD, Address VDAddr, SourceLocation Loc,
    bool PerformInit, CodeGenFunction *CGF) {
  if (CGM.getLangOpts().OpenMPUseTLS &&
      CGM.getContext().getTargetInfo().isTLSSupported())
    return nullptr;

  VD = VD->getDefinition(CGM.getContext());
  if (VD && ThreadPrivateWithDefinition.insert(CGM.getMangledName(VD)).second) {
    QualType ASTTy = VD->getType();

    llvm::Value *Ctor = nullptr, *CopyCtor = nullptr, *Dtor = nullptr;
    const Expr *Init = VD->getAnyInitializer();
    if (CGM.getLangOpts().CPlusPlus && PerformInit) {
      // Generate function that re-emits the declaration's initializer into the
      // threadprivate copy of the variable VD
      CodeGenFunction CtorCGF(CGM);
      FunctionArgList Args;
      ImplicitParamDecl Dst(CGM.getContext(), /*DC=*/nullptr, Loc,
                            /*Id=*/nullptr, CGM.getContext().VoidPtrTy,
                            ImplicitParamKind::Other);
      Args.push_back(&Dst);

      const auto &FI = CGM.getTypes().arrangeBuiltinFunctionDeclaration(
          CGM.getContext().VoidPtrTy, Args);
      llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
      std::string Name = getName({"__kmpc_global_ctor_", ""});
      llvm::Function *Fn =
          CGM.CreateGlobalInitOrCleanUpFunction(FTy, Name, FI, Loc);
      CtorCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidPtrTy, Fn, FI,
                            Args, Loc, Loc);
      llvm::Value *ArgVal = CtorCGF.EmitLoadOfScalar(
          CtorCGF.GetAddrOfLocalVar(&Dst), /*Volatile=*/false,
          CGM.getContext().VoidPtrTy, Dst.getLocation());
      Address Arg(ArgVal, CtorCGF.ConvertTypeForMem(ASTTy),
                  VDAddr.getAlignment());
      CtorCGF.EmitAnyExprToMem(Init, Arg, Init->getType().getQualifiers(),
                               /*IsInitializer=*/true);
      ArgVal = CtorCGF.EmitLoadOfScalar(
          CtorCGF.GetAddrOfLocalVar(&Dst), /*Volatile=*/false,
          CGM.getContext().VoidPtrTy, Dst.getLocation());
      CtorCGF.Builder.CreateStore(ArgVal, CtorCGF.ReturnValue);
      CtorCGF.FinishFunction();
      Ctor = Fn;
    }
    if (VD->getType().isDestructedType() != QualType::DK_none) {
      // Generate function that emits destructor call for the threadprivate copy
      // of the variable VD
      CodeGenFunction DtorCGF(CGM);
      FunctionArgList Args;
      ImplicitParamDecl Dst(CGM.getContext(), /*DC=*/nullptr, Loc,
                            /*Id=*/nullptr, CGM.getContext().VoidPtrTy,
                            ImplicitParamKind::Other);
      Args.push_back(&Dst);

      const auto &FI = CGM.getTypes().arrangeBuiltinFunctionDeclaration(
          CGM.getContext().VoidTy, Args);
      llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
      std::string Name = getName({"__kmpc_global_dtor_", ""});
      llvm::Function *Fn =
          CGM.CreateGlobalInitOrCleanUpFunction(FTy, Name, FI, Loc);
      auto NL = ApplyDebugLocation::CreateEmpty(DtorCGF);
      DtorCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidTy, Fn, FI, Args,
                            Loc, Loc);
      // Create a scope with an artificial location for the body of this function.
      auto AL = ApplyDebugLocation::CreateArtificial(DtorCGF);
      llvm::Value *ArgVal = DtorCGF.EmitLoadOfScalar(
          DtorCGF.GetAddrOfLocalVar(&Dst),
          /*Volatile=*/false, CGM.getContext().VoidPtrTy, Dst.getLocation());
      DtorCGF.emitDestroy(
          Address(ArgVal, DtorCGF.Int8Ty, VDAddr.getAlignment()), ASTTy,
          DtorCGF.getDestroyer(ASTTy.isDestructedType()),
          DtorCGF.needsEHCleanup(ASTTy.isDestructedType()));
      DtorCGF.FinishFunction();
      Dtor = Fn;
    }
    // Do not emit init function if it is not required.
    if (!Ctor && !Dtor)
      return nullptr;

    llvm::Type *CopyCtorTyArgs[] = {CGM.VoidPtrTy, CGM.VoidPtrTy};
    auto *CopyCtorTy = llvm::FunctionType::get(CGM.VoidPtrTy, CopyCtorTyArgs,
                                               /*isVarArg=*/false)
                           ->getPointerTo();
    // Copying constructor for the threadprivate variable.
    // Must be NULL - reserved by runtime, but currently it requires that this
    // parameter is always NULL. Otherwise it fires assertion.
    CopyCtor = llvm::Constant::getNullValue(CopyCtorTy);
    if (Ctor == nullptr) {
      auto *CtorTy = llvm::FunctionType::get(CGM.VoidPtrTy, CGM.VoidPtrTy,
                                             /*isVarArg=*/false)
                         ->getPointerTo();
      Ctor = llvm::Constant::getNullValue(CtorTy);
    }
    if (Dtor == nullptr) {
      auto *DtorTy = llvm::FunctionType::get(CGM.VoidTy, CGM.VoidPtrTy,
                                             /*isVarArg=*/false)
                         ->getPointerTo();
      Dtor = llvm::Constant::getNullValue(DtorTy);
    }
    if (!CGF) {
      auto *InitFunctionTy =
          llvm::FunctionType::get(CGM.VoidTy, /*isVarArg*/ false);
      std::string Name = getName({"__omp_threadprivate_init_", ""});
      llvm::Function *InitFunction = CGM.CreateGlobalInitOrCleanUpFunction(
          InitFunctionTy, Name, CGM.getTypes().arrangeNullaryFunction());
      CodeGenFunction InitCGF(CGM);
      FunctionArgList ArgList;
      InitCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidTy, InitFunction,
                            CGM.getTypes().arrangeNullaryFunction(), ArgList,
                            Loc, Loc);
      emitThreadPrivateVarInit(InitCGF, VDAddr, Ctor, CopyCtor, Dtor, Loc);
      InitCGF.FinishFunction();
      return InitFunction;
    }
    emitThreadPrivateVarInit(*CGF, VDAddr, Ctor, CopyCtor, Dtor, Loc);
  }
  return nullptr;
}

void CGOpenMPRuntime::emitDeclareTargetFunction(const FunctionDecl *FD,
                                                llvm::GlobalValue *GV) {
  std::optional<OMPDeclareTargetDeclAttr *> ActiveAttr =
      OMPDeclareTargetDeclAttr::getActiveAttr(FD);

  // We only need to handle active 'indirect' declare target functions.
  if (!ActiveAttr || !(*ActiveAttr)->getIndirect())
    return;

  // Get a mangled name to store the new device global in.
  llvm::TargetRegionEntryInfo EntryInfo = getEntryInfoFromPresumedLoc(
      CGM, OMPBuilder, FD->getCanonicalDecl()->getBeginLoc(), FD->getName());
  SmallString<128> Name;
  OMPBuilder.OffloadInfoManager.getTargetRegionEntryFnName(Name, EntryInfo);

  // We need to generate a new global to hold the address of the indirectly
  // called device function. Doing this allows us to keep the visibility and
  // linkage of the associated function unchanged while allowing the runtime to
  // access its value.
  llvm::GlobalValue *Addr = GV;
  if (CGM.getLangOpts().OpenMPIsTargetDevice) {
    Addr = new llvm::GlobalVariable(
        CGM.getModule(), CGM.VoidPtrTy,
        /*isConstant=*/true, llvm::GlobalValue::ExternalLinkage, GV, Name,
        nullptr, llvm::GlobalValue::NotThreadLocal,
        CGM.getModule().getDataLayout().getDefaultGlobalsAddressSpace());
    Addr->setVisibility(llvm::GlobalValue::ProtectedVisibility);
  }

  OMPBuilder.OffloadInfoManager.registerDeviceGlobalVarEntryInfo(
      Name, Addr, CGM.GetTargetTypeStoreSize(CGM.VoidPtrTy).getQuantity(),
      llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryIndirect,
      llvm::GlobalValue::WeakODRLinkage);
}

Address CGOpenMPRuntime::getAddrOfArtificialThreadPrivate(CodeGenFunction &CGF,
                                                          QualType VarType,
                                                          StringRef Name) {
  std::string Suffix = getName({"artificial", ""});
  llvm::Type *VarLVType = CGF.ConvertTypeForMem(VarType);
  llvm::GlobalVariable *GAddr = OMPBuilder.getOrCreateInternalVariable(
      VarLVType, Twine(Name).concat(Suffix).str());
  if (CGM.getLangOpts().OpenMP && CGM.getLangOpts().OpenMPUseTLS &&
      CGM.getTarget().isTLSSupported()) {
    GAddr->setThreadLocal(/*Val=*/true);
    return Address(GAddr, GAddr->getValueType(),
                   CGM.getContext().getTypeAlignInChars(VarType));
  }
  std::string CacheSuffix = getName({"cache", ""});
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, SourceLocation()),
      getThreadID(CGF, SourceLocation()),
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(GAddr, CGM.VoidPtrTy),
      CGF.Builder.CreateIntCast(CGF.getTypeSize(VarType), CGM.SizeTy,
                                /*isSigned=*/false),
      OMPBuilder.getOrCreateInternalVariable(
          CGM.VoidPtrPtrTy,
          Twine(Name).concat(Suffix).concat(CacheSuffix).str())};
  return Address(
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          CGF.EmitRuntimeCall(
              OMPBuilder.getOrCreateRuntimeFunction(
                  CGM.getModule(), OMPRTL___kmpc_threadprivate_cached),
              Args),
          VarLVType->getPointerTo(/*AddrSpace=*/0)),
      VarLVType, CGM.getContext().getTypeAlignInChars(VarType));
}

void CGOpenMPRuntime::emitIfClause(CodeGenFunction &CGF, const Expr *Cond,
                                   const RegionCodeGenTy &ThenGen,
                                   const RegionCodeGenTy &ElseGen) {
  CodeGenFunction::LexicalScope ConditionScope(CGF, Cond->getSourceRange());

  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm of the if/else.
  bool CondConstant;
  if (CGF.ConstantFoldsToSimpleInteger(Cond, CondConstant)) {
    if (CondConstant)
      ThenGen(CGF);
    else
      ElseGen(CGF);
    return;
  }

  // Otherwise, the condition did not fold, or we couldn't elide it.  Just
  // emit the conditional branch.
  llvm::BasicBlock *ThenBlock = CGF.createBasicBlock("omp_if.then");
  llvm::BasicBlock *ElseBlock = CGF.createBasicBlock("omp_if.else");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("omp_if.end");
  CGF.EmitBranchOnBoolExpr(Cond, ThenBlock, ElseBlock, /*TrueCount=*/0);

  // Emit the 'then' code.
  CGF.EmitBlock(ThenBlock);
  ThenGen(CGF);
  CGF.EmitBranch(ContBlock);
  // Emit the 'else' code if present.
  // There is no need to emit line number for unconditional branch.
  (void)ApplyDebugLocation::CreateEmpty(CGF);
  CGF.EmitBlock(ElseBlock);
  ElseGen(CGF);
  // There is no need to emit line number for unconditional branch.
  (void)ApplyDebugLocation::CreateEmpty(CGF);
  CGF.EmitBranch(ContBlock);
  // Emit the continuation block for code after the if.
  CGF.EmitBlock(ContBlock, /*IsFinished=*/true);
}

void CGOpenMPRuntime::emitParallelCall(CodeGenFunction &CGF, SourceLocation Loc,
                                       llvm::Function *OutlinedFn,
                                       ArrayRef<llvm::Value *> CapturedVars,
                                       const Expr *IfCond,
                                       llvm::Value *NumThreads) {
  if (!CGF.HaveInsertPoint())
    return;
  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);
  auto &M = CGM.getModule();
  auto &&ThenGen = [&M, OutlinedFn, CapturedVars, RTLoc,
                    this](CodeGenFunction &CGF, PrePostActionTy &) {
    // Build call __kmpc_fork_call(loc, n, microtask, var1, .., varn);
    CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
    llvm::Value *Args[] = {
        RTLoc,
        CGF.Builder.getInt32(CapturedVars.size()), // Number of captured vars
        CGF.Builder.CreateBitCast(OutlinedFn, RT.getKmpc_MicroPointerTy())};
    llvm::SmallVector<llvm::Value *, 16> RealArgs;
    RealArgs.append(std::begin(Args), std::end(Args));
    RealArgs.append(CapturedVars.begin(), CapturedVars.end());

    llvm::FunctionCallee RTLFn =
        OMPBuilder.getOrCreateRuntimeFunction(M, OMPRTL___kmpc_fork_call);
    CGF.EmitRuntimeCall(RTLFn, RealArgs);
  };
  auto &&ElseGen = [&M, OutlinedFn, CapturedVars, RTLoc, Loc,
                    this](CodeGenFunction &CGF, PrePostActionTy &) {
    CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
    llvm::Value *ThreadID = RT.getThreadID(CGF, Loc);
    // Build calls:
    // __kmpc_serialized_parallel(&Loc, GTid);
    llvm::Value *Args[] = {RTLoc, ThreadID};
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            M, OMPRTL___kmpc_serialized_parallel),
                        Args);

    // OutlinedFn(&GTid, &zero_bound, CapturedStruct);
    Address ThreadIDAddr = RT.emitThreadIDAddress(CGF, Loc);
    RawAddress ZeroAddrBound =
        CGF.CreateDefaultAlignTempAlloca(CGF.Int32Ty,
                                         /*Name=*/".bound.zero.addr");
    CGF.Builder.CreateStore(CGF.Builder.getInt32(/*C*/ 0), ZeroAddrBound);
    llvm::SmallVector<llvm::Value *, 16> OutlinedFnArgs;
    // ThreadId for serialized parallels is 0.
    OutlinedFnArgs.push_back(ThreadIDAddr.emitRawPointer(CGF));
    OutlinedFnArgs.push_back(ZeroAddrBound.getPointer());
    OutlinedFnArgs.append(CapturedVars.begin(), CapturedVars.end());

    // Ensure we do not inline the function. This is trivially true for the ones
    // passed to __kmpc_fork_call but the ones called in serialized regions
    // could be inlined. This is not a perfect but it is closer to the invariant
    // we want, namely, every data environment starts with a new function.
    // TODO: We should pass the if condition to the runtime function and do the
    //       handling there. Much cleaner code.
    OutlinedFn->removeFnAttr(llvm::Attribute::AlwaysInline);
    OutlinedFn->addFnAttr(llvm::Attribute::NoInline);
    RT.emitOutlinedFunctionCall(CGF, Loc, OutlinedFn, OutlinedFnArgs);

    // __kmpc_end_serialized_parallel(&Loc, GTid);
    llvm::Value *EndArgs[] = {RT.emitUpdateLocation(CGF, Loc), ThreadID};
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            M, OMPRTL___kmpc_end_serialized_parallel),
                        EndArgs);
  };
  if (IfCond) {
    emitIfClause(CGF, IfCond, ThenGen, ElseGen);
  } else {
    RegionCodeGenTy ThenRCG(ThenGen);
    ThenRCG(CGF);
  }
}

// If we're inside an (outlined) parallel region, use the region info's
// thread-ID variable (it is passed in a first argument of the outlined function
// as "kmp_int32 *gtid"). Otherwise, if we're not inside parallel region, but in
// regular serial code region, get thread ID by calling kmp_int32
// kmpc_global_thread_num(ident_t *loc), stash this thread ID in a temporary and
// return the address of that temp.
Address CGOpenMPRuntime::emitThreadIDAddress(CodeGenFunction &CGF,
                                             SourceLocation Loc) {
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
    if (OMPRegionInfo->getThreadIDVariable())
      return OMPRegionInfo->getThreadIDVariableLValue(CGF).getAddress();

  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  QualType Int32Ty =
      CGF.getContext().getIntTypeForBitwidth(/*DestWidth*/ 32, /*Signed*/ true);
  Address ThreadIDTemp = CGF.CreateMemTemp(Int32Ty, /*Name*/ ".threadid_temp.");
  CGF.EmitStoreOfScalar(ThreadID,
                        CGF.MakeAddrLValue(ThreadIDTemp, Int32Ty));

  return ThreadIDTemp;
}

llvm::Value *CGOpenMPRuntime::getCriticalRegionLock(StringRef CriticalName) {
  std::string Prefix = Twine("gomp_critical_user_", CriticalName).str();
  std::string Name = getName({Prefix, "var"});
  return OMPBuilder.getOrCreateInternalVariable(KmpCriticalNameTy, Name);
}

namespace {
/// Common pre(post)-action for different OpenMP constructs.
class CommonActionTy final : public PrePostActionTy {
  llvm::FunctionCallee EnterCallee;
  ArrayRef<llvm::Value *> EnterArgs;
  llvm::FunctionCallee ExitCallee;
  ArrayRef<llvm::Value *> ExitArgs;
  bool Conditional;
  llvm::BasicBlock *ContBlock = nullptr;

public:
  CommonActionTy(llvm::FunctionCallee EnterCallee,
                 ArrayRef<llvm::Value *> EnterArgs,
                 llvm::FunctionCallee ExitCallee,
                 ArrayRef<llvm::Value *> ExitArgs, bool Conditional = false)
      : EnterCallee(EnterCallee), EnterArgs(EnterArgs), ExitCallee(ExitCallee),
        ExitArgs(ExitArgs), Conditional(Conditional) {}
  void Enter(CodeGenFunction &CGF) override {
    llvm::Value *EnterRes = CGF.EmitRuntimeCall(EnterCallee, EnterArgs);
    if (Conditional) {
      llvm::Value *CallBool = CGF.Builder.CreateIsNotNull(EnterRes);
      auto *ThenBlock = CGF.createBasicBlock("omp_if.then");
      ContBlock = CGF.createBasicBlock("omp_if.end");
      // Generate the branch (If-stmt)
      CGF.Builder.CreateCondBr(CallBool, ThenBlock, ContBlock);
      CGF.EmitBlock(ThenBlock);
    }
  }
  void Done(CodeGenFunction &CGF) {
    // Emit the rest of blocks/branches
    CGF.EmitBranch(ContBlock);
    CGF.EmitBlock(ContBlock, true);
  }
  void Exit(CodeGenFunction &CGF) override {
    CGF.EmitRuntimeCall(ExitCallee, ExitArgs);
  }
};
} // anonymous namespace

void CGOpenMPRuntime::emitCriticalRegion(CodeGenFunction &CGF,
                                         StringRef CriticalName,
                                         const RegionCodeGenTy &CriticalOpGen,
                                         SourceLocation Loc, const Expr *Hint) {
  // __kmpc_critical[_with_hint](ident_t *, gtid, Lock[, hint]);
  // CriticalOpGen();
  // __kmpc_end_critical(ident_t *, gtid, Lock);
  // Prepare arguments and build a call to __kmpc_critical
  if (!CGF.HaveInsertPoint())
    return;
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
                         getCriticalRegionLock(CriticalName)};
  llvm::SmallVector<llvm::Value *, 4> EnterArgs(std::begin(Args),
                                                std::end(Args));
  if (Hint) {
    EnterArgs.push_back(CGF.Builder.CreateIntCast(
        CGF.EmitScalarExpr(Hint), CGM.Int32Ty, /*isSigned=*/false));
  }
  CommonActionTy Action(
      OMPBuilder.getOrCreateRuntimeFunction(
          CGM.getModule(),
          Hint ? OMPRTL___kmpc_critical_with_hint : OMPRTL___kmpc_critical),
      EnterArgs,
      OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                            OMPRTL___kmpc_end_critical),
      Args);
  CriticalOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_critical, CriticalOpGen);
}

void CGOpenMPRuntime::emitMasterRegion(CodeGenFunction &CGF,
                                       const RegionCodeGenTy &MasterOpGen,
                                       SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // if(__kmpc_master(ident_t *, gtid)) {
  //   MasterOpGen();
  //   __kmpc_end_master(ident_t *, gtid);
  // }
  // Prepare arguments and build a call to __kmpc_master
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CommonActionTy Action(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_master),
                        Args,
                        OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_end_master),
                        Args,
                        /*Conditional=*/true);
  MasterOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_master, MasterOpGen);
  Action.Done(CGF);
}

void CGOpenMPRuntime::emitMaskedRegion(CodeGenFunction &CGF,
                                       const RegionCodeGenTy &MaskedOpGen,
                                       SourceLocation Loc, const Expr *Filter) {
  if (!CGF.HaveInsertPoint())
    return;
  // if(__kmpc_masked(ident_t *, gtid, filter)) {
  //   MaskedOpGen();
  //   __kmpc_end_masked(iden_t *, gtid);
  // }
  // Prepare arguments and build a call to __kmpc_masked
  llvm::Value *FilterVal = Filter
                               ? CGF.EmitScalarExpr(Filter, CGF.Int32Ty)
                               : llvm::ConstantInt::get(CGM.Int32Ty, /*V=*/0);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
                         FilterVal};
  llvm::Value *ArgsEnd[] = {emitUpdateLocation(CGF, Loc),
                            getThreadID(CGF, Loc)};
  CommonActionTy Action(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_masked),
                        Args,
                        OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_end_masked),
                        ArgsEnd,
                        /*Conditional=*/true);
  MaskedOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_masked, MaskedOpGen);
  Action.Done(CGF);
}

void CGOpenMPRuntime::emitTaskyieldCall(CodeGenFunction &CGF,
                                        SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  if (CGF.CGM.getLangOpts().OpenMPIRBuilder) {
    OMPBuilder.createTaskyield(CGF.Builder);
  } else {
    // Build call __kmpc_omp_taskyield(loc, thread_id, 0);
    llvm::Value *Args[] = {
        emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
        llvm::ConstantInt::get(CGM.IntTy, /*V=*/0, /*isSigned=*/true)};
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_omp_taskyield),
                        Args);
  }

  if (auto *Region = dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
    Region->emitUntiedSwitch(CGF);
}

void CGOpenMPRuntime::emitTaskgroupRegion(CodeGenFunction &CGF,
                                          const RegionCodeGenTy &TaskgroupOpGen,
                                          SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // __kmpc_taskgroup(ident_t *, gtid);
  // TaskgroupOpGen();
  // __kmpc_end_taskgroup(ident_t *, gtid);
  // Prepare arguments and build a call to __kmpc_taskgroup
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CommonActionTy Action(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_taskgroup),
                        Args,
                        OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_end_taskgroup),
                        Args);
  TaskgroupOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_taskgroup, TaskgroupOpGen);
}

/// Given an array of pointers to variables, project the address of a
/// given variable.
static Address emitAddrOfVarFromArray(CodeGenFunction &CGF, Address Array,
                                      unsigned Index, const VarDecl *Var) {
  // Pull out the pointer to the variable.
  Address PtrAddr = CGF.Builder.CreateConstArrayGEP(Array, Index);
  llvm::Value *Ptr = CGF.Builder.CreateLoad(PtrAddr);

  llvm::Type *ElemTy = CGF.ConvertTypeForMem(Var->getType());
  return Address(
      CGF.Builder.CreateBitCast(
          Ptr, ElemTy->getPointerTo(Ptr->getType()->getPointerAddressSpace())),
      ElemTy, CGF.getContext().getDeclAlign(Var));
}

static llvm::Value *emitCopyprivateCopyFunction(
    CodeGenModule &CGM, llvm::Type *ArgsElemType,
    ArrayRef<const Expr *> CopyprivateVars, ArrayRef<const Expr *> DestExprs,
    ArrayRef<const Expr *> SrcExprs, ArrayRef<const Expr *> AssignmentOps,
    SourceLocation Loc) {
  ASTContext &C = CGM.getContext();
  // void copy_func(void *LHSArg, void *RHSArg);
  FunctionArgList Args;
  ImplicitParamDecl LHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamKind::Other);
  ImplicitParamDecl RHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamKind::Other);
  Args.push_back(&LHSArg);
  Args.push_back(&RHSArg);
  const auto &CGFI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  std::string Name =
      CGM.getOpenMPRuntime().getName({"omp", "copyprivate", "copy_func"});
  auto *Fn = llvm::Function::Create(CGM.getTypes().GetFunctionType(CGFI),
                                    llvm::GlobalValue::InternalLinkage, Name,
                                    &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, CGFI);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, CGFI, Args, Loc, Loc);
  // Dest = (void*[n])(LHSArg);
  // Src = (void*[n])(RHSArg);
  Address LHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                  CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&LHSArg)),
                  ArgsElemType->getPointerTo()),
              ArgsElemType, CGF.getPointerAlign());
  Address RHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                  CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&RHSArg)),
                  ArgsElemType->getPointerTo()),
              ArgsElemType, CGF.getPointerAlign());
  // *(Type0*)Dst[0] = *(Type0*)Src[0];
  // *(Type1*)Dst[1] = *(Type1*)Src[1];
  // ...
  // *(Typen*)Dst[n] = *(Typen*)Src[n];
  for (unsigned I = 0, E = AssignmentOps.size(); I < E; ++I) {
    const auto *DestVar =
        cast<VarDecl>(cast<DeclRefExpr>(DestExprs[I])->getDecl());
    Address DestAddr = emitAddrOfVarFromArray(CGF, LHS, I, DestVar);

    const auto *SrcVar =
        cast<VarDecl>(cast<DeclRefExpr>(SrcExprs[I])->getDecl());
    Address SrcAddr = emitAddrOfVarFromArray(CGF, RHS, I, SrcVar);

    const auto *VD = cast<DeclRefExpr>(CopyprivateVars[I])->getDecl();
    QualType Type = VD->getType();
    CGF.EmitOMPCopy(Type, DestAddr, SrcAddr, DestVar, SrcVar, AssignmentOps[I]);
  }
  CGF.FinishFunction();
  return Fn;
}

void CGOpenMPRuntime::emitSingleRegion(CodeGenFunction &CGF,
                                       const RegionCodeGenTy &SingleOpGen,
                                       SourceLocation Loc,
                                       ArrayRef<const Expr *> CopyprivateVars,
                                       ArrayRef<const Expr *> SrcExprs,
                                       ArrayRef<const Expr *> DstExprs,
                                       ArrayRef<const Expr *> AssignmentOps) {
  if (!CGF.HaveInsertPoint())
    return;
  assert(CopyprivateVars.size() == SrcExprs.size() &&
         CopyprivateVars.size() == DstExprs.size() &&
         CopyprivateVars.size() == AssignmentOps.size());
  ASTContext &C = CGM.getContext();
  // int32 did_it = 0;
  // if(__kmpc_single(ident_t *, gtid)) {
  //   SingleOpGen();
  //   __kmpc_end_single(ident_t *, gtid);
  //   did_it = 1;
  // }
  // call __kmpc_copyprivate(ident_t *, gtid, <buf_size>, <copyprivate list>,
  // <copy_func>, did_it);

  Address DidIt = Address::invalid();
  if (!CopyprivateVars.empty()) {
    // int32 did_it = 0;
    QualType KmpInt32Ty =
        C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1);
    DidIt = CGF.CreateMemTemp(KmpInt32Ty, ".omp.copyprivate.did_it");
    CGF.Builder.CreateStore(CGF.Builder.getInt32(0), DidIt);
  }
  // Prepare arguments and build a call to __kmpc_single
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CommonActionTy Action(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_single),
                        Args,
                        OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_end_single),
                        Args,
                        /*Conditional=*/true);
  SingleOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_single, SingleOpGen);
  if (DidIt.isValid()) {
    // did_it = 1;
    CGF.Builder.CreateStore(CGF.Builder.getInt32(1), DidIt);
  }
  Action.Done(CGF);
  // call __kmpc_copyprivate(ident_t *, gtid, <buf_size>, <copyprivate list>,
  // <copy_func>, did_it);
  if (DidIt.isValid()) {
    llvm::APInt ArraySize(/*unsigned int numBits=*/32, CopyprivateVars.size());
    QualType CopyprivateArrayTy = C.getConstantArrayType(
        C.VoidPtrTy, ArraySize, nullptr, ArraySizeModifier::Normal,
        /*IndexTypeQuals=*/0);
    // Create a list of all private variables for copyprivate.
    Address CopyprivateList =
        CGF.CreateMemTemp(CopyprivateArrayTy, ".omp.copyprivate.cpr_list");
    for (unsigned I = 0, E = CopyprivateVars.size(); I < E; ++I) {
      Address Elem = CGF.Builder.CreateConstArrayGEP(CopyprivateList, I);
      CGF.Builder.CreateStore(
          CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
              CGF.EmitLValue(CopyprivateVars[I]).getPointer(CGF),
              CGF.VoidPtrTy),
          Elem);
    }
    // Build function that copies private values from single region to all other
    // threads in the corresponding parallel region.
    llvm::Value *CpyFn = emitCopyprivateCopyFunction(
        CGM, CGF.ConvertTypeForMem(CopyprivateArrayTy), CopyprivateVars,
        SrcExprs, DstExprs, AssignmentOps, Loc);
    llvm::Value *BufSize = CGF.getTypeSize(CopyprivateArrayTy);
    Address CL = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        CopyprivateList, CGF.VoidPtrTy, CGF.Int8Ty);
    llvm::Value *DidItVal = CGF.Builder.CreateLoad(DidIt);
    llvm::Value *Args[] = {
        emitUpdateLocation(CGF, Loc), // ident_t *<loc>
        getThreadID(CGF, Loc),        // i32 <gtid>
        BufSize,                      // size_t <buf_size>
        CL.emitRawPointer(CGF),       // void *<copyprivate list>
        CpyFn,                        // void (*) (void *, void *) <copy_func>
        DidItVal                      // i32 did_it
    };
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_copyprivate),
                        Args);
  }
}

void CGOpenMPRuntime::emitOrderedRegion(CodeGenFunction &CGF,
                                        const RegionCodeGenTy &OrderedOpGen,
                                        SourceLocation Loc, bool IsThreads) {
  if (!CGF.HaveInsertPoint())
    return;
  // __kmpc_ordered(ident_t *, gtid);
  // OrderedOpGen();
  // __kmpc_end_ordered(ident_t *, gtid);
  // Prepare arguments and build a call to __kmpc_ordered
  if (IsThreads) {
    llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
    CommonActionTy Action(OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_ordered),
                          Args,
                          OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_end_ordered),
                          Args);
    OrderedOpGen.setAction(Action);
    emitInlinedDirective(CGF, OMPD_ordered, OrderedOpGen);
    return;
  }
  emitInlinedDirective(CGF, OMPD_ordered, OrderedOpGen);
}

unsigned CGOpenMPRuntime::getDefaultFlagsForBarriers(OpenMPDirectiveKind Kind) {
  unsigned Flags;
  if (Kind == OMPD_for)
    Flags = OMP_IDENT_BARRIER_IMPL_FOR;
  else if (Kind == OMPD_sections)
    Flags = OMP_IDENT_BARRIER_IMPL_SECTIONS;
  else if (Kind == OMPD_single)
    Flags = OMP_IDENT_BARRIER_IMPL_SINGLE;
  else if (Kind == OMPD_barrier)
    Flags = OMP_IDENT_BARRIER_EXPL;
  else
    Flags = OMP_IDENT_BARRIER_IMPL;
  return Flags;
}

void CGOpenMPRuntime::getDefaultScheduleAndChunk(
    CodeGenFunction &CGF, const OMPLoopDirective &S,
    OpenMPScheduleClauseKind &ScheduleKind, const Expr *&ChunkExpr) const {
  // Check if the loop directive is actually a doacross loop directive. In this
  // case choose static, 1 schedule.
  if (llvm::any_of(
          S.getClausesOfKind<OMPOrderedClause>(),
          [](const OMPOrderedClause *C) { return C->getNumForLoops(); })) {
    ScheduleKind = OMPC_SCHEDULE_static;
    // Chunk size is 1 in this case.
    llvm::APInt ChunkSize(32, 1);
    ChunkExpr = IntegerLiteral::Create(
        CGF.getContext(), ChunkSize,
        CGF.getContext().getIntTypeForBitwidth(32, /*Signed=*/0),
        SourceLocation());
  }
}

void CGOpenMPRuntime::emitBarrierCall(CodeGenFunction &CGF, SourceLocation Loc,
                                      OpenMPDirectiveKind Kind, bool EmitChecks,
                                      bool ForceSimpleCall) {
  // Check if we should use the OMPBuilder
  auto *OMPRegionInfo =
      dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo);
  if (CGF.CGM.getLangOpts().OpenMPIRBuilder) {
    CGF.Builder.restoreIP(OMPBuilder.createBarrier(
        CGF.Builder, Kind, ForceSimpleCall, EmitChecks));
    return;
  }

  if (!CGF.HaveInsertPoint())
    return;
  // Build call __kmpc_cancel_barrier(loc, thread_id);
  // Build call __kmpc_barrier(loc, thread_id);
  unsigned Flags = getDefaultFlagsForBarriers(Kind);
  // Build call __kmpc_cancel_barrier(loc, thread_id) or __kmpc_barrier(loc,
  // thread_id);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc, Flags),
                         getThreadID(CGF, Loc)};
  if (OMPRegionInfo) {
    if (!ForceSimpleCall && OMPRegionInfo->hasCancel()) {
      llvm::Value *Result = CGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                                OMPRTL___kmpc_cancel_barrier),
          Args);
      if (EmitChecks) {
        // if (__kmpc_cancel_barrier()) {
        //   exit from construct;
        // }
        llvm::BasicBlock *ExitBB = CGF.createBasicBlock(".cancel.exit");
        llvm::BasicBlock *ContBB = CGF.createBasicBlock(".cancel.continue");
        llvm::Value *Cmp = CGF.Builder.CreateIsNotNull(Result);
        CGF.Builder.CreateCondBr(Cmp, ExitBB, ContBB);
        CGF.EmitBlock(ExitBB);
        //   exit from construct;
        CodeGenFunction::JumpDest CancelDestination =
            CGF.getOMPCancelDestination(OMPRegionInfo->getDirectiveKind());
        CGF.EmitBranchThroughCleanup(CancelDestination);
        CGF.EmitBlock(ContBB, /*IsFinished=*/true);
      }
      return;
    }
  }
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_barrier),
                      Args);
}

void CGOpenMPRuntime::emitErrorCall(CodeGenFunction &CGF, SourceLocation Loc,
                                    Expr *ME, bool IsFatal) {
  llvm::Value *MVL =
      ME ? CGF.EmitStringLiteralLValue(cast<StringLiteral>(ME)).getPointer(CGF)
         : llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  // Build call void __kmpc_error(ident_t *loc, int severity, const char
  // *message)
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc, /*Flags=*/0, /*GenLoc=*/true),
      llvm::ConstantInt::get(CGM.Int32Ty, IsFatal ? 2 : 1),
      CGF.Builder.CreatePointerCast(MVL, CGM.Int8PtrTy)};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_error),
                      Args);
}

/// Map the OpenMP loop schedule to the runtime enumeration.
static OpenMPSchedType getRuntimeSchedule(OpenMPScheduleClauseKind ScheduleKind,
                                          bool Chunked, bool Ordered) {
  switch (ScheduleKind) {
  case OMPC_SCHEDULE_static:
    return Chunked ? (Ordered ? OMP_ord_static_chunked : OMP_sch_static_chunked)
                   : (Ordered ? OMP_ord_static : OMP_sch_static);
  case OMPC_SCHEDULE_dynamic:
    return Ordered ? OMP_ord_dynamic_chunked : OMP_sch_dynamic_chunked;
  case OMPC_SCHEDULE_guided:
    return Ordered ? OMP_ord_guided_chunked : OMP_sch_guided_chunked;
  case OMPC_SCHEDULE_runtime:
    return Ordered ? OMP_ord_runtime : OMP_sch_runtime;
  case OMPC_SCHEDULE_auto:
    return Ordered ? OMP_ord_auto : OMP_sch_auto;
  case OMPC_SCHEDULE_unknown:
    assert(!Chunked && "chunk was specified but schedule kind not known");
    return Ordered ? OMP_ord_static : OMP_sch_static;
  }
  llvm_unreachable("Unexpected runtime schedule");
}

/// Map the OpenMP distribute schedule to the runtime enumeration.
static OpenMPSchedType
getRuntimeSchedule(OpenMPDistScheduleClauseKind ScheduleKind, bool Chunked) {
  // only static is allowed for dist_schedule
  return Chunked ? OMP_dist_sch_static_chunked : OMP_dist_sch_static;
}

bool CGOpenMPRuntime::isStaticNonchunked(OpenMPScheduleClauseKind ScheduleKind,
                                         bool Chunked) const {
  OpenMPSchedType Schedule =
      getRuntimeSchedule(ScheduleKind, Chunked, /*Ordered=*/false);
  return Schedule == OMP_sch_static;
}

bool CGOpenMPRuntime::isStaticNonchunked(
    OpenMPDistScheduleClauseKind ScheduleKind, bool Chunked) const {
  OpenMPSchedType Schedule = getRuntimeSchedule(ScheduleKind, Chunked);
  return Schedule == OMP_dist_sch_static;
}

bool CGOpenMPRuntime::isStaticChunked(OpenMPScheduleClauseKind ScheduleKind,
                                      bool Chunked) const {
  OpenMPSchedType Schedule =
      getRuntimeSchedule(ScheduleKind, Chunked, /*Ordered=*/false);
  return Schedule == OMP_sch_static_chunked;
}

bool CGOpenMPRuntime::isStaticChunked(
    OpenMPDistScheduleClauseKind ScheduleKind, bool Chunked) const {
  OpenMPSchedType Schedule = getRuntimeSchedule(ScheduleKind, Chunked);
  return Schedule == OMP_dist_sch_static_chunked;
}

bool CGOpenMPRuntime::isDynamic(OpenMPScheduleClauseKind ScheduleKind) const {
  OpenMPSchedType Schedule =
      getRuntimeSchedule(ScheduleKind, /*Chunked=*/false, /*Ordered=*/false);
  assert(Schedule != OMP_sch_static_chunked && "cannot be chunked here");
  return Schedule != OMP_sch_static;
}

static int addMonoNonMonoModifier(CodeGenModule &CGM, OpenMPSchedType Schedule,
                                  OpenMPScheduleClauseModifier M1,
                                  OpenMPScheduleClauseModifier M2) {
  int Modifier = 0;
  switch (M1) {
  case OMPC_SCHEDULE_MODIFIER_monotonic:
    Modifier = OMP_sch_modifier_monotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_nonmonotonic:
    Modifier = OMP_sch_modifier_nonmonotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_simd:
    if (Schedule == OMP_sch_static_chunked)
      Schedule = OMP_sch_static_balanced_chunked;
    break;
  case OMPC_SCHEDULE_MODIFIER_last:
  case OMPC_SCHEDULE_MODIFIER_unknown:
    break;
  }
  switch (M2) {
  case OMPC_SCHEDULE_MODIFIER_monotonic:
    Modifier = OMP_sch_modifier_monotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_nonmonotonic:
    Modifier = OMP_sch_modifier_nonmonotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_simd:
    if (Schedule == OMP_sch_static_chunked)
      Schedule = OMP_sch_static_balanced_chunked;
    break;
  case OMPC_SCHEDULE_MODIFIER_last:
  case OMPC_SCHEDULE_MODIFIER_unknown:
    break;
  }
  // OpenMP 5.0, 2.9.2 Worksharing-Loop Construct, Desription.
  // If the static schedule kind is specified or if the ordered clause is
  // specified, and if the nonmonotonic modifier is not specified, the effect is
  // as if the monotonic modifier is specified. Otherwise, unless the monotonic
  // modifier is specified, the effect is as if the nonmonotonic modifier is
  // specified.
  if (CGM.getLangOpts().OpenMP >= 50 && Modifier == 0) {
    if (!(Schedule == OMP_sch_static_chunked || Schedule == OMP_sch_static ||
          Schedule == OMP_sch_static_balanced_chunked ||
          Schedule == OMP_ord_static_chunked || Schedule == OMP_ord_static ||
          Schedule == OMP_dist_sch_static_chunked ||
          Schedule == OMP_dist_sch_static))
      Modifier = OMP_sch_modifier_nonmonotonic;
  }
  return Schedule | Modifier;
}

void CGOpenMPRuntime::emitForDispatchInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    const OpenMPScheduleTy &ScheduleKind, unsigned IVSize, bool IVSigned,
    bool Ordered, const DispatchRTInput &DispatchValues) {
  if (!CGF.HaveInsertPoint())
    return;
  OpenMPSchedType Schedule = getRuntimeSchedule(
      ScheduleKind.Schedule, DispatchValues.Chunk != nullptr, Ordered);
  assert(Ordered ||
         (Schedule != OMP_sch_static && Schedule != OMP_sch_static_chunked &&
          Schedule != OMP_ord_static && Schedule != OMP_ord_static_chunked &&
          Schedule != OMP_sch_static_balanced_chunked));
  // Call __kmpc_dispatch_init(
  //          ident_t *loc, kmp_int32 tid, kmp_int32 schedule,
  //          kmp_int[32|64] lower, kmp_int[32|64] upper,
  //          kmp_int[32|64] stride, kmp_int[32|64] chunk);

  // If the Chunk was not specified in the clause - use default value 1.
  llvm::Value *Chunk = DispatchValues.Chunk ? DispatchValues.Chunk
                                            : CGF.Builder.getIntN(IVSize, 1);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc),
      getThreadID(CGF, Loc),
      CGF.Builder.getInt32(addMonoNonMonoModifier(
          CGM, Schedule, ScheduleKind.M1, ScheduleKind.M2)), // Schedule type
      DispatchValues.LB,                                     // Lower
      DispatchValues.UB,                                     // Upper
      CGF.Builder.getIntN(IVSize, 1),                        // Stride
      Chunk                                                  // Chunk
  };
  CGF.EmitRuntimeCall(OMPBuilder.createDispatchInitFunction(IVSize, IVSigned),
                      Args);
}

void CGOpenMPRuntime::emitForDispatchDeinit(CodeGenFunction &CGF,
                                            SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // Call __kmpc_dispatch_deinit(ident_t *loc, kmp_int32 tid);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CGF.EmitRuntimeCall(OMPBuilder.createDispatchDeinitFunction(), Args);
}

static void emitForStaticInitCall(
    CodeGenFunction &CGF, llvm::Value *UpdateLocation, llvm::Value *ThreadId,
    llvm::FunctionCallee ForStaticInitFunction, OpenMPSchedType Schedule,
    OpenMPScheduleClauseModifier M1, OpenMPScheduleClauseModifier M2,
    const CGOpenMPRuntime::StaticRTInput &Values) {
  if (!CGF.HaveInsertPoint())
    return;

  assert(!Values.Ordered);
  assert(Schedule == OMP_sch_static || Schedule == OMP_sch_static_chunked ||
         Schedule == OMP_sch_static_balanced_chunked ||
         Schedule == OMP_ord_static || Schedule == OMP_ord_static_chunked ||
         Schedule == OMP_dist_sch_static ||
         Schedule == OMP_dist_sch_static_chunked);

  // Call __kmpc_for_static_init(
  //          ident_t *loc, kmp_int32 tid, kmp_int32 schedtype,
  //          kmp_int32 *p_lastiter, kmp_int[32|64] *p_lower,
  //          kmp_int[32|64] *p_upper, kmp_int[32|64] *p_stride,
  //          kmp_int[32|64] incr, kmp_int[32|64] chunk);
  llvm::Value *Chunk = Values.Chunk;
  if (Chunk == nullptr) {
    assert((Schedule == OMP_sch_static || Schedule == OMP_ord_static ||
            Schedule == OMP_dist_sch_static) &&
           "expected static non-chunked schedule");
    // If the Chunk was not specified in the clause - use default value 1.
    Chunk = CGF.Builder.getIntN(Values.IVSize, 1);
  } else {
    assert((Schedule == OMP_sch_static_chunked ||
            Schedule == OMP_sch_static_balanced_chunked ||
            Schedule == OMP_ord_static_chunked ||
            Schedule == OMP_dist_sch_static_chunked) &&
           "expected static chunked schedule");
  }
  llvm::Value *Args[] = {
      UpdateLocation,
      ThreadId,
      CGF.Builder.getInt32(addMonoNonMonoModifier(CGF.CGM, Schedule, M1,
                                                  M2)), // Schedule type
      Values.IL.emitRawPointer(CGF),                    // &isLastIter
      Values.LB.emitRawPointer(CGF),                    // &LB
      Values.UB.emitRawPointer(CGF),                    // &UB
      Values.ST.emitRawPointer(CGF),                    // &Stride
      CGF.Builder.getIntN(Values.IVSize, 1),            // Incr
      Chunk                                             // Chunk
  };
  CGF.EmitRuntimeCall(ForStaticInitFunction, Args);
}

void CGOpenMPRuntime::emitForStaticInit(CodeGenFunction &CGF,
                                        SourceLocation Loc,
                                        OpenMPDirectiveKind DKind,
                                        const OpenMPScheduleTy &ScheduleKind,
                                        const StaticRTInput &Values) {
  OpenMPSchedType ScheduleNum = getRuntimeSchedule(
      ScheduleKind.Schedule, Values.Chunk != nullptr, Values.Ordered);
  assert((isOpenMPWorksharingDirective(DKind) || (DKind == OMPD_loop)) &&
         "Expected loop-based or sections-based directive.");
  llvm::Value *UpdatedLocation = emitUpdateLocation(CGF, Loc,
                                             isOpenMPLoopDirective(DKind)
                                                 ? OMP_IDENT_WORK_LOOP
                                                 : OMP_IDENT_WORK_SECTIONS);
  llvm::Value *ThreadId = getThreadID(CGF, Loc);
  llvm::FunctionCallee StaticInitFunction =
      OMPBuilder.createForStaticInitFunction(Values.IVSize, Values.IVSigned,
                                             false);
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(CGF, Loc);
  emitForStaticInitCall(CGF, UpdatedLocation, ThreadId, StaticInitFunction,
                        ScheduleNum, ScheduleKind.M1, ScheduleKind.M2, Values);
}

void CGOpenMPRuntime::emitDistributeStaticInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDistScheduleClauseKind SchedKind,
    const CGOpenMPRuntime::StaticRTInput &Values) {
  OpenMPSchedType ScheduleNum =
      getRuntimeSchedule(SchedKind, Values.Chunk != nullptr);
  llvm::Value *UpdatedLocation =
      emitUpdateLocation(CGF, Loc, OMP_IDENT_WORK_DISTRIBUTE);
  llvm::Value *ThreadId = getThreadID(CGF, Loc);
  llvm::FunctionCallee StaticInitFunction;
  bool isGPUDistribute =
      CGM.getLangOpts().OpenMPIsTargetDevice &&
      (CGM.getTriple().isAMDGCN() || CGM.getTriple().isNVPTX());
  StaticInitFunction = OMPBuilder.createForStaticInitFunction(
      Values.IVSize, Values.IVSigned, isGPUDistribute);

  emitForStaticInitCall(CGF, UpdatedLocation, ThreadId, StaticInitFunction,
                        ScheduleNum, OMPC_SCHEDULE_MODIFIER_unknown,
                        OMPC_SCHEDULE_MODIFIER_unknown, Values);
}

void CGOpenMPRuntime::emitForStaticFinish(CodeGenFunction &CGF,
                                          SourceLocation Loc,
                                          OpenMPDirectiveKind DKind) {
  assert((DKind == OMPD_distribute || DKind == OMPD_for ||
          DKind == OMPD_sections) &&
         "Expected distribute, for, or sections directive kind");
  if (!CGF.HaveInsertPoint())
    return;
  // Call __kmpc_for_static_fini(ident_t *loc, kmp_int32 tid);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc,
                         isOpenMPDistributeDirective(DKind) ||
                                 (DKind == OMPD_target_teams_loop)
                             ? OMP_IDENT_WORK_DISTRIBUTE
                         : isOpenMPLoopDirective(DKind)
                             ? OMP_IDENT_WORK_LOOP
                             : OMP_IDENT_WORK_SECTIONS),
      getThreadID(CGF, Loc)};
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(CGF, Loc);
  if (isOpenMPDistributeDirective(DKind) &&
      CGM.getLangOpts().OpenMPIsTargetDevice &&
      (CGM.getTriple().isAMDGCN() || CGM.getTriple().isNVPTX()))
    CGF.EmitRuntimeCall(
        OMPBuilder.getOrCreateRuntimeFunction(
            CGM.getModule(), OMPRTL___kmpc_distribute_static_fini),
        Args);
  else
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_for_static_fini),
                        Args);
}

void CGOpenMPRuntime::emitForOrderedIterationEnd(CodeGenFunction &CGF,
                                                 SourceLocation Loc,
                                                 unsigned IVSize,
                                                 bool IVSigned) {
  if (!CGF.HaveInsertPoint())
    return;
  // Call __kmpc_for_dynamic_fini_(4|8)[u](ident_t *loc, kmp_int32 tid);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CGF.EmitRuntimeCall(OMPBuilder.createDispatchFiniFunction(IVSize, IVSigned),
                      Args);
}

llvm::Value *CGOpenMPRuntime::emitForNext(CodeGenFunction &CGF,
                                          SourceLocation Loc, unsigned IVSize,
                                          bool IVSigned, Address IL,
                                          Address LB, Address UB,
                                          Address ST) {
  // Call __kmpc_dispatch_next(
  //          ident_t *loc, kmp_int32 tid, kmp_int32 *p_lastiter,
  //          kmp_int[32|64] *p_lower, kmp_int[32|64] *p_upper,
  //          kmp_int[32|64] *p_stride);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      IL.emitRawPointer(CGF), // &isLastIter
      LB.emitRawPointer(CGF), // &Lower
      UB.emitRawPointer(CGF), // &Upper
      ST.emitRawPointer(CGF)  // &Stride
  };
  llvm::Value *Call = CGF.EmitRuntimeCall(
      OMPBuilder.createDispatchNextFunction(IVSize, IVSigned), Args);
  return CGF.EmitScalarConversion(
      Call, CGF.getContext().getIntTypeForBitwidth(32, /*Signed=*/1),
      CGF.getContext().BoolTy, Loc);
}

void CGOpenMPRuntime::emitNumThreadsClause(CodeGenFunction &CGF,
                                           llvm::Value *NumThreads,
                                           SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call __kmpc_push_num_threads(&loc, global_tid, num_threads)
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      CGF.Builder.CreateIntCast(NumThreads, CGF.Int32Ty, /*isSigned*/ true)};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_push_num_threads),
                      Args);
}

void CGOpenMPRuntime::emitProcBindClause(CodeGenFunction &CGF,
                                         ProcBindKind ProcBind,
                                         SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  assert(ProcBind != OMP_PROC_BIND_unknown && "Unsupported proc_bind value.");
  // Build call __kmpc_push_proc_bind(&loc, global_tid, proc_bind)
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      llvm::ConstantInt::get(CGM.IntTy, unsigned(ProcBind), /*isSigned=*/true)};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_push_proc_bind),
                      Args);
}

void CGOpenMPRuntime::emitFlush(CodeGenFunction &CGF, ArrayRef<const Expr *>,
                                SourceLocation Loc, llvm::AtomicOrdering AO) {
  if (CGF.CGM.getLangOpts().OpenMPIRBuilder) {
    OMPBuilder.createFlush(CGF.Builder);
  } else {
    if (!CGF.HaveInsertPoint())
      return;
    // Build call void __kmpc_flush(ident_t *loc)
    CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                            CGM.getModule(), OMPRTL___kmpc_flush),
                        emitUpdateLocation(CGF, Loc));
  }
}

namespace {
/// Indexes of fields for type kmp_task_t.
enum KmpTaskTFields {
  /// List of shared variables.
  KmpTaskTShareds,
  /// Task routine.
  KmpTaskTRoutine,
  /// Partition id for the untied tasks.
  KmpTaskTPartId,
  /// Function with call of destructors for private variables.
  Data1,
  /// Task priority.
  Data2,
  /// (Taskloops only) Lower bound.
  KmpTaskTLowerBound,
  /// (Taskloops only) Upper bound.
  KmpTaskTUpperBound,
  /// (Taskloops only) Stride.
  KmpTaskTStride,
  /// (Taskloops only) Is last iteration flag.
  KmpTaskTLastIter,
  /// (Taskloops only) Reduction data.
  KmpTaskTReductions,
};
} // anonymous namespace

void CGOpenMPRuntime::createOffloadEntriesAndInfoMetadata() {
  // If we are in simd mode or there are no entries, we don't need to do
  // anything.
  if (CGM.getLangOpts().OpenMPSimd || OMPBuilder.OffloadInfoManager.empty())
    return;

  llvm::OpenMPIRBuilder::EmitMetadataErrorReportFunctionTy &&ErrorReportFn =
      [this](llvm::OpenMPIRBuilder::EmitMetadataErrorKind Kind,
             const llvm::TargetRegionEntryInfo &EntryInfo) -> void {
    SourceLocation Loc;
    if (Kind != llvm::OpenMPIRBuilder::EMIT_MD_GLOBAL_VAR_LINK_ERROR) {
      for (auto I = CGM.getContext().getSourceManager().fileinfo_begin(),
                E = CGM.getContext().getSourceManager().fileinfo_end();
           I != E; ++I) {
        if (I->getFirst().getUniqueID().getDevice() == EntryInfo.DeviceID &&
            I->getFirst().getUniqueID().getFile() == EntryInfo.FileID) {
          Loc = CGM.getContext().getSourceManager().translateFileLineCol(
              I->getFirst(), EntryInfo.Line, 1);
          break;
        }
      }
    }
    switch (Kind) {
    case llvm::OpenMPIRBuilder::EMIT_MD_TARGET_REGION_ERROR: {
      unsigned DiagID = CGM.getDiags().getCustomDiagID(
          DiagnosticsEngine::Error, "Offloading entry for target region in "
                                    "%0 is incorrect: either the "
                                    "address or the ID is invalid.");
      CGM.getDiags().Report(Loc, DiagID) << EntryInfo.ParentName;
    } break;
    case llvm::OpenMPIRBuilder::EMIT_MD_DECLARE_TARGET_ERROR: {
      unsigned DiagID = CGM.getDiags().getCustomDiagID(
          DiagnosticsEngine::Error, "Offloading entry for declare target "
                                    "variable %0 is incorrect: the "
                                    "address is invalid.");
      CGM.getDiags().Report(Loc, DiagID) << EntryInfo.ParentName;
    } break;
    case llvm::OpenMPIRBuilder::EMIT_MD_GLOBAL_VAR_LINK_ERROR: {
      unsigned DiagID = CGM.getDiags().getCustomDiagID(
          DiagnosticsEngine::Error,
          "Offloading entry for declare target variable is incorrect: the "
          "address is invalid.");
      CGM.getDiags().Report(DiagID);
    } break;
    }
  };

  OMPBuilder.createOffloadEntriesAndInfoMetadata(ErrorReportFn);
}

void CGOpenMPRuntime::emitKmpRoutineEntryT(QualType KmpInt32Ty) {
  if (!KmpRoutineEntryPtrTy) {
    // Build typedef kmp_int32 (* kmp_routine_entry_t)(kmp_int32, void *); type.
    ASTContext &C = CGM.getContext();
    QualType KmpRoutineEntryTyArgs[] = {KmpInt32Ty, C.VoidPtrTy};
    FunctionProtoType::ExtProtoInfo EPI;
    KmpRoutineEntryPtrQTy = C.getPointerType(
        C.getFunctionType(KmpInt32Ty, KmpRoutineEntryTyArgs, EPI));
    KmpRoutineEntryPtrTy = CGM.getTypes().ConvertType(KmpRoutineEntryPtrQTy);
  }
}

namespace {
struct PrivateHelpersTy {
  PrivateHelpersTy(const Expr *OriginalRef, const VarDecl *Original,
                   const VarDecl *PrivateCopy, const VarDecl *PrivateElemInit)
      : OriginalRef(OriginalRef), Original(Original), PrivateCopy(PrivateCopy),
        PrivateElemInit(PrivateElemInit) {}
  PrivateHelpersTy(const VarDecl *Original) : Original(Original) {}
  const Expr *OriginalRef = nullptr;
  const VarDecl *Original = nullptr;
  const VarDecl *PrivateCopy = nullptr;
  const VarDecl *PrivateElemInit = nullptr;
  bool isLocalPrivate() const {
    return !OriginalRef && !PrivateCopy && !PrivateElemInit;
  }
};
typedef std::pair<CharUnits /*Align*/, PrivateHelpersTy> PrivateDataTy;
} // anonymous namespace

static bool isAllocatableDecl(const VarDecl *VD) {
  const VarDecl *CVD = VD->getCanonicalDecl();
  if (!CVD->hasAttr<OMPAllocateDeclAttr>())
    return false;
  const auto *AA = CVD->getAttr<OMPAllocateDeclAttr>();
  // Use the default allocation.
  return !(AA->getAllocatorType() == OMPAllocateDeclAttr::OMPDefaultMemAlloc &&
           !AA->getAllocator());
}

static RecordDecl *
createPrivatesRecordDecl(CodeGenModule &CGM, ArrayRef<PrivateDataTy> Privates) {
  if (!Privates.empty()) {
    ASTContext &C = CGM.getContext();
    // Build struct .kmp_privates_t. {
    //         /*  private vars  */
    //       };
    RecordDecl *RD = C.buildImplicitRecord(".kmp_privates.t");
    RD->startDefinition();
    for (const auto &Pair : Privates) {
      const VarDecl *VD = Pair.second.Original;
      QualType Type = VD->getType().getNonReferenceType();
      // If the private variable is a local variable with lvalue ref type,
      // allocate the pointer instead of the pointee type.
      if (Pair.second.isLocalPrivate()) {
        if (VD->getType()->isLValueReferenceType())
          Type = C.getPointerType(Type);
        if (isAllocatableDecl(VD))
          Type = C.getPointerType(Type);
      }
      FieldDecl *FD = addFieldToRecordDecl(C, RD, Type);
      if (VD->hasAttrs()) {
        for (specific_attr_iterator<AlignedAttr> I(VD->getAttrs().begin()),
             E(VD->getAttrs().end());
             I != E; ++I)
          FD->addAttr(*I);
      }
    }
    RD->completeDefinition();
    return RD;
  }
  return nullptr;
}

static RecordDecl *
createKmpTaskTRecordDecl(CodeGenModule &CGM, OpenMPDirectiveKind Kind,
                         QualType KmpInt32Ty,
                         QualType KmpRoutineEntryPointerQTy) {
  ASTContext &C = CGM.getContext();
  // Build struct kmp_task_t {
  //         void *              shareds;
  //         kmp_routine_entry_t routine;
  //         kmp_int32           part_id;
  //         kmp_cmplrdata_t data1;
  //         kmp_cmplrdata_t data2;
  // For taskloops additional fields:
  //         kmp_uint64          lb;
  //         kmp_uint64          ub;
  //         kmp_int64           st;
  //         kmp_int32           liter;
  //         void *              reductions;
  //       };
  RecordDecl *UD = C.buildImplicitRecord("kmp_cmplrdata_t", TagTypeKind::Union);
  UD->startDefinition();
  addFieldToRecordDecl(C, UD, KmpInt32Ty);
  addFieldToRecordDecl(C, UD, KmpRoutineEntryPointerQTy);
  UD->completeDefinition();
  QualType KmpCmplrdataTy = C.getRecordType(UD);
  RecordDecl *RD = C.buildImplicitRecord("kmp_task_t");
  RD->startDefinition();
  addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  addFieldToRecordDecl(C, RD, KmpRoutineEntryPointerQTy);
  addFieldToRecordDecl(C, RD, KmpInt32Ty);
  addFieldToRecordDecl(C, RD, KmpCmplrdataTy);
  addFieldToRecordDecl(C, RD, KmpCmplrdataTy);
  if (isOpenMPTaskLoopDirective(Kind)) {
    QualType KmpUInt64Ty =
        CGM.getContext().getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/0);
    QualType KmpInt64Ty =
        CGM.getContext().getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/1);
    addFieldToRecordDecl(C, RD, KmpUInt64Ty);
    addFieldToRecordDecl(C, RD, KmpUInt64Ty);
    addFieldToRecordDecl(C, RD, KmpInt64Ty);
    addFieldToRecordDecl(C, RD, KmpInt32Ty);
    addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  }
  RD->completeDefinition();
  return RD;
}

static RecordDecl *
createKmpTaskTWithPrivatesRecordDecl(CodeGenModule &CGM, QualType KmpTaskTQTy,
                                     ArrayRef<PrivateDataTy> Privates) {
  ASTContext &C = CGM.getContext();
  // Build struct kmp_task_t_with_privates {
  //         kmp_task_t task_data;
  //         .kmp_privates_t. privates;
  //       };
  RecordDecl *RD = C.buildImplicitRecord("kmp_task_t_with_privates");
  RD->startDefinition();
  addFieldToRecordDecl(C, RD, KmpTaskTQTy);
  if (const RecordDecl *PrivateRD = createPrivatesRecordDecl(CGM, Privates))
    addFieldToRecordDecl(C, RD, C.getRecordType(PrivateRD));
  RD->completeDefinition();
  return RD;
}

/// Emit a proxy function which accepts kmp_task_t as the second
/// argument.
/// \code
/// kmp_int32 .omp_task_entry.(kmp_int32 gtid, kmp_task_t *tt) {
///   TaskFunction(gtid, tt->part_id, &tt->privates, task_privates_map, tt,
///   For taskloops:
///   tt->task_data.lb, tt->task_data.ub, tt->task_data.st, tt->task_data.liter,
///   tt->reductions, tt->shareds);
///   return 0;
/// }
/// \endcode
static llvm::Function *
emitProxyTaskFunction(CodeGenModule &CGM, SourceLocation Loc,
                      OpenMPDirectiveKind Kind, QualType KmpInt32Ty,
                      QualType KmpTaskTWithPrivatesPtrQTy,
                      QualType KmpTaskTWithPrivatesQTy, QualType KmpTaskTQTy,
                      QualType SharedsPtrTy, llvm::Function *TaskFunction,
                      llvm::Value *TaskPrivatesMap) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl GtidArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, KmpInt32Ty,
                            ImplicitParamKind::Other);
  ImplicitParamDecl TaskTypeArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                                KmpTaskTWithPrivatesPtrQTy.withRestrict(),
                                ImplicitParamKind::Other);
  Args.push_back(&GtidArg);
  Args.push_back(&TaskTypeArg);
  const auto &TaskEntryFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(KmpInt32Ty, Args);
  llvm::FunctionType *TaskEntryTy =
      CGM.getTypes().GetFunctionType(TaskEntryFnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"omp_task_entry", ""});
  auto *TaskEntry = llvm::Function::Create(
      TaskEntryTy, llvm::GlobalValue::InternalLinkage, Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), TaskEntry, TaskEntryFnInfo);
  TaskEntry->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), KmpInt32Ty, TaskEntry, TaskEntryFnInfo, Args,
                    Loc, Loc);

  // TaskFunction(gtid, tt->task_data.part_id, &tt->privates, task_privates_map,
  // tt,
  // For taskloops:
  // tt->task_data.lb, tt->task_data.ub, tt->task_data.st, tt->task_data.liter,
  // tt->task_data.shareds);
  llvm::Value *GtidParam = CGF.EmitLoadOfScalar(
      CGF.GetAddrOfLocalVar(&GtidArg), /*Volatile=*/false, KmpInt32Ty, Loc);
  LValue TDBase = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&TaskTypeArg),
      KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
  const auto *KmpTaskTWithPrivatesQTyRD =
      cast<RecordDecl>(KmpTaskTWithPrivatesQTy->getAsTagDecl());
  LValue Base =
      CGF.EmitLValueForField(TDBase, *KmpTaskTWithPrivatesQTyRD->field_begin());
  const auto *KmpTaskTQTyRD = cast<RecordDecl>(KmpTaskTQTy->getAsTagDecl());
  auto PartIdFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTPartId);
  LValue PartIdLVal = CGF.EmitLValueForField(Base, *PartIdFI);
  llvm::Value *PartidParam = PartIdLVal.getPointer(CGF);

  auto SharedsFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTShareds);
  LValue SharedsLVal = CGF.EmitLValueForField(Base, *SharedsFI);
  llvm::Value *SharedsParam = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      CGF.EmitLoadOfScalar(SharedsLVal, Loc),
      CGF.ConvertTypeForMem(SharedsPtrTy));

  auto PrivatesFI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin(), 1);
  llvm::Value *PrivatesParam;
  if (PrivatesFI != KmpTaskTWithPrivatesQTyRD->field_end()) {
    LValue PrivatesLVal = CGF.EmitLValueForField(TDBase, *PrivatesFI);
    PrivatesParam = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        PrivatesLVal.getPointer(CGF), CGF.VoidPtrTy);
  } else {
    PrivatesParam = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  }

  llvm::Value *CommonArgs[] = {
      GtidParam, PartidParam, PrivatesParam, TaskPrivatesMap,
      CGF.Builder
          .CreatePointerBitCastOrAddrSpaceCast(TDBase.getAddress(),
                                               CGF.VoidPtrTy, CGF.Int8Ty)
          .emitRawPointer(CGF)};
  SmallVector<llvm::Value *, 16> CallArgs(std::begin(CommonArgs),
                                          std::end(CommonArgs));
  if (isOpenMPTaskLoopDirective(Kind)) {
    auto LBFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTLowerBound);
    LValue LBLVal = CGF.EmitLValueForField(Base, *LBFI);
    llvm::Value *LBParam = CGF.EmitLoadOfScalar(LBLVal, Loc);
    auto UBFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTUpperBound);
    LValue UBLVal = CGF.EmitLValueForField(Base, *UBFI);
    llvm::Value *UBParam = CGF.EmitLoadOfScalar(UBLVal, Loc);
    auto StFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTStride);
    LValue StLVal = CGF.EmitLValueForField(Base, *StFI);
    llvm::Value *StParam = CGF.EmitLoadOfScalar(StLVal, Loc);
    auto LIFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTLastIter);
    LValue LILVal = CGF.EmitLValueForField(Base, *LIFI);
    llvm::Value *LIParam = CGF.EmitLoadOfScalar(LILVal, Loc);
    auto RFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTReductions);
    LValue RLVal = CGF.EmitLValueForField(Base, *RFI);
    llvm::Value *RParam = CGF.EmitLoadOfScalar(RLVal, Loc);
    CallArgs.push_back(LBParam);
    CallArgs.push_back(UBParam);
    CallArgs.push_back(StParam);
    CallArgs.push_back(LIParam);
    CallArgs.push_back(RParam);
  }
  CallArgs.push_back(SharedsParam);

  CGM.getOpenMPRuntime().emitOutlinedFunctionCall(CGF, Loc, TaskFunction,
                                                  CallArgs);
  CGF.EmitStoreThroughLValue(RValue::get(CGF.Builder.getInt32(/*C=*/0)),
                             CGF.MakeAddrLValue(CGF.ReturnValue, KmpInt32Ty));
  CGF.FinishFunction();
  return TaskEntry;
}

static llvm::Value *emitDestructorsFunction(CodeGenModule &CGM,
                                            SourceLocation Loc,
                                            QualType KmpInt32Ty,
                                            QualType KmpTaskTWithPrivatesPtrQTy,
                                            QualType KmpTaskTWithPrivatesQTy) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl GtidArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, KmpInt32Ty,
                            ImplicitParamKind::Other);
  ImplicitParamDecl TaskTypeArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                                KmpTaskTWithPrivatesPtrQTy.withRestrict(),
                                ImplicitParamKind::Other);
  Args.push_back(&GtidArg);
  Args.push_back(&TaskTypeArg);
  const auto &DestructorFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(KmpInt32Ty, Args);
  llvm::FunctionType *DestructorFnTy =
      CGM.getTypes().GetFunctionType(DestructorFnInfo);
  std::string Name =
      CGM.getOpenMPRuntime().getName({"omp_task_destructor", ""});
  auto *DestructorFn =
      llvm::Function::Create(DestructorFnTy, llvm::GlobalValue::InternalLinkage,
                             Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), DestructorFn,
                                    DestructorFnInfo);
  DestructorFn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), KmpInt32Ty, DestructorFn, DestructorFnInfo,
                    Args, Loc, Loc);

  LValue Base = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&TaskTypeArg),
      KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
  const auto *KmpTaskTWithPrivatesQTyRD =
      cast<RecordDecl>(KmpTaskTWithPrivatesQTy->getAsTagDecl());
  auto FI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin());
  Base = CGF.EmitLValueForField(Base, *FI);
  for (const auto *Field :
       cast<RecordDecl>(FI->getType()->getAsTagDecl())->fields()) {
    if (QualType::DestructionKind DtorKind =
            Field->getType().isDestructedType()) {
      LValue FieldLValue = CGF.EmitLValueForField(Base, Field);
      CGF.pushDestroy(DtorKind, FieldLValue.getAddress(), Field->getType());
    }
  }
  CGF.FinishFunction();
  return DestructorFn;
}

/// Emit a privates mapping function for correct handling of private and
/// firstprivate variables.
/// \code
/// void .omp_task_privates_map.(const .privates. *noalias privs, <ty1>
/// **noalias priv1,...,  <tyn> **noalias privn) {
///   *priv1 = &.privates.priv1;
///   ...;
///   *privn = &.privates.privn;
/// }
/// \endcode
static llvm::Value *
emitTaskPrivateMappingFunction(CodeGenModule &CGM, SourceLocation Loc,
                               const OMPTaskDataTy &Data, QualType PrivatesQTy,
                               ArrayRef<PrivateDataTy> Privates) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl TaskPrivatesArg(
      C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
      C.getPointerType(PrivatesQTy).withConst().withRestrict(),
      ImplicitParamKind::Other);
  Args.push_back(&TaskPrivatesArg);
  llvm::DenseMap<CanonicalDeclPtr<const VarDecl>, unsigned> PrivateVarsPos;
  unsigned Counter = 1;
  for (const Expr *E : Data.PrivateVars) {
    Args.push_back(ImplicitParamDecl::Create(
        C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
        C.getPointerType(C.getPointerType(E->getType()))
            .withConst()
            .withRestrict(),
        ImplicitParamKind::Other));
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    PrivateVarsPos[VD] = Counter;
    ++Counter;
  }
  for (const Expr *E : Data.FirstprivateVars) {
    Args.push_back(ImplicitParamDecl::Create(
        C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
        C.getPointerType(C.getPointerType(E->getType()))
            .withConst()
            .withRestrict(),
        ImplicitParamKind::Other));
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    PrivateVarsPos[VD] = Counter;
    ++Counter;
  }
  for (const Expr *E : Data.LastprivateVars) {
    Args.push_back(ImplicitParamDecl::Create(
        C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
        C.getPointerType(C.getPointerType(E->getType()))
            .withConst()
            .withRestrict(),
        ImplicitParamKind::Other));
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    PrivateVarsPos[VD] = Counter;
    ++Counter;
  }
  for (const VarDecl *VD : Data.PrivateLocals) {
    QualType Ty = VD->getType().getNonReferenceType();
    if (VD->getType()->isLValueReferenceType())
      Ty = C.getPointerType(Ty);
    if (isAllocatableDecl(VD))
      Ty = C.getPointerType(Ty);
    Args.push_back(ImplicitParamDecl::Create(
        C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
        C.getPointerType(C.getPointerType(Ty)).withConst().withRestrict(),
        ImplicitParamKind::Other));
    PrivateVarsPos[VD] = Counter;
    ++Counter;
  }
  const auto &TaskPrivatesMapFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *TaskPrivatesMapTy =
      CGM.getTypes().GetFunctionType(TaskPrivatesMapFnInfo);
  std::string Name =
      CGM.getOpenMPRuntime().getName({"omp_task_privates_map", ""});
  auto *TaskPrivatesMap = llvm::Function::Create(
      TaskPrivatesMapTy, llvm::GlobalValue::InternalLinkage, Name,
      &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), TaskPrivatesMap,
                                    TaskPrivatesMapFnInfo);
  if (CGM.getLangOpts().Optimize) {
    TaskPrivatesMap->removeFnAttr(llvm::Attribute::NoInline);
    TaskPrivatesMap->removeFnAttr(llvm::Attribute::OptimizeNone);
    TaskPrivatesMap->addFnAttr(llvm::Attribute::AlwaysInline);
  }
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, TaskPrivatesMap,
                    TaskPrivatesMapFnInfo, Args, Loc, Loc);

  // *privi = &.privates.privi;
  LValue Base = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&TaskPrivatesArg),
      TaskPrivatesArg.getType()->castAs<PointerType>());
  const auto *PrivatesQTyRD = cast<RecordDecl>(PrivatesQTy->getAsTagDecl());
  Counter = 0;
  for (const FieldDecl *Field : PrivatesQTyRD->fields()) {
    LValue FieldLVal = CGF.EmitLValueForField(Base, Field);
    const VarDecl *VD = Args[PrivateVarsPos[Privates[Counter].second.Original]];
    LValue RefLVal =
        CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(VD), VD->getType());
    LValue RefLoadLVal = CGF.EmitLoadOfPointerLValue(
        RefLVal.getAddress(), RefLVal.getType()->castAs<PointerType>());
    CGF.EmitStoreOfScalar(FieldLVal.getPointer(CGF), RefLoadLVal);
    ++Counter;
  }
  CGF.FinishFunction();
  return TaskPrivatesMap;
}

/// Emit initialization for private variables in task-based directives.
static void emitPrivatesInit(CodeGenFunction &CGF,
                             const OMPExecutableDirective &D,
                             Address KmpTaskSharedsPtr, LValue TDBase,
                             const RecordDecl *KmpTaskTWithPrivatesQTyRD,
                             QualType SharedsTy, QualType SharedsPtrTy,
                             const OMPTaskDataTy &Data,
                             ArrayRef<PrivateDataTy> Privates, bool ForDup) {
  ASTContext &C = CGF.getContext();
  auto FI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin());
  LValue PrivatesBase = CGF.EmitLValueForField(TDBase, *FI);
  OpenMPDirectiveKind Kind = isOpenMPTaskLoopDirective(D.getDirectiveKind())
                                 ? OMPD_taskloop
                                 : OMPD_task;
  const CapturedStmt &CS = *D.getCapturedStmt(Kind);
  CodeGenFunction::CGCapturedStmtInfo CapturesInfo(CS);
  LValue SrcBase;
  bool IsTargetTask =
      isOpenMPTargetDataManagementDirective(D.getDirectiveKind()) ||
      isOpenMPTargetExecutionDirective(D.getDirectiveKind());
  // For target-based directives skip 4 firstprivate arrays BasePointersArray,
  // PointersArray, SizesArray, and MappersArray. The original variables for
  // these arrays are not captured and we get their addresses explicitly.
  if ((!IsTargetTask && !Data.FirstprivateVars.empty() && ForDup) ||
      (IsTargetTask && KmpTaskSharedsPtr.isValid())) {
    SrcBase = CGF.MakeAddrLValue(
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            KmpTaskSharedsPtr, CGF.ConvertTypeForMem(SharedsPtrTy),
            CGF.ConvertTypeForMem(SharedsTy)),
        SharedsTy);
  }
  FI = cast<RecordDecl>(FI->getType()->getAsTagDecl())->field_begin();
  for (const PrivateDataTy &Pair : Privates) {
    // Do not initialize private locals.
    if (Pair.second.isLocalPrivate()) {
      ++FI;
      continue;
    }
    const VarDecl *VD = Pair.second.PrivateCopy;
    const Expr *Init = VD->getAnyInitializer();
    if (Init && (!ForDup || (isa<CXXConstructExpr>(Init) &&
                             !CGF.isTrivialInitializer(Init)))) {
      LValue PrivateLValue = CGF.EmitLValueForField(PrivatesBase, *FI);
      if (const VarDecl *Elem = Pair.second.PrivateElemInit) {
        const VarDecl *OriginalVD = Pair.second.Original;
        // Check if the variable is the target-based BasePointersArray,
        // PointersArray, SizesArray, or MappersArray.
        LValue SharedRefLValue;
        QualType Type = PrivateLValue.getType();
        const FieldDecl *SharedField = CapturesInfo.lookup(OriginalVD);
        if (IsTargetTask && !SharedField) {
          assert(isa<ImplicitParamDecl>(OriginalVD) &&
                 isa<CapturedDecl>(OriginalVD->getDeclContext()) &&
                 cast<CapturedDecl>(OriginalVD->getDeclContext())
                         ->getNumParams() == 0 &&
                 isa<TranslationUnitDecl>(
                     cast<CapturedDecl>(OriginalVD->getDeclContext())
                         ->getDeclContext()) &&
                 "Expected artificial target data variable.");
          SharedRefLValue =
              CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(OriginalVD), Type);
        } else if (ForDup) {
          SharedRefLValue = CGF.EmitLValueForField(SrcBase, SharedField);
          SharedRefLValue = CGF.MakeAddrLValue(
              SharedRefLValue.getAddress().withAlignment(
                  C.getDeclAlign(OriginalVD)),
              SharedRefLValue.getType(), LValueBaseInfo(AlignmentSource::Decl),
              SharedRefLValue.getTBAAInfo());
        } else if (CGF.LambdaCaptureFields.count(
                       Pair.second.Original->getCanonicalDecl()) > 0 ||
                   isa_and_nonnull<BlockDecl>(CGF.CurCodeDecl)) {
          SharedRefLValue = CGF.EmitLValue(Pair.second.OriginalRef);
        } else {
          // Processing for implicitly captured variables.
          InlinedOpenMPRegionRAII Region(
              CGF, [](CodeGenFunction &, PrePostActionTy &) {}, OMPD_unknown,
              /*HasCancel=*/false, /*NoInheritance=*/true);
          SharedRefLValue = CGF.EmitLValue(Pair.second.OriginalRef);
        }
        if (Type->isArrayType()) {
          // Initialize firstprivate array.
          if (!isa<CXXConstructExpr>(Init) || CGF.isTrivialInitializer(Init)) {
            // Perform simple memcpy.
            CGF.EmitAggregateAssign(PrivateLValue, SharedRefLValue, Type);
          } else {
            // Initialize firstprivate array using element-by-element
            // initialization.
            CGF.EmitOMPAggregateAssign(
                PrivateLValue.getAddress(), SharedRefLValue.getAddress(), Type,
                [&CGF, Elem, Init, &CapturesInfo](Address DestElement,
                                                  Address SrcElement) {
                  // Clean up any temporaries needed by the initialization.
                  CodeGenFunction::OMPPrivateScope InitScope(CGF);
                  InitScope.addPrivate(Elem, SrcElement);
                  (void)InitScope.Privatize();
                  // Emit initialization for single element.
                  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(
                      CGF, &CapturesInfo);
                  CGF.EmitAnyExprToMem(Init, DestElement,
                                       Init->getType().getQualifiers(),
                                       /*IsInitializer=*/false);
                });
          }
        } else {
          CodeGenFunction::OMPPrivateScope InitScope(CGF);
          InitScope.addPrivate(Elem, SharedRefLValue.getAddress());
          (void)InitScope.Privatize();
          CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CapturesInfo);
          CGF.EmitExprAsInit(Init, VD, PrivateLValue,
                             /*capturedByInit=*/false);
        }
      } else {
        CGF.EmitExprAsInit(Init, VD, PrivateLValue, /*capturedByInit=*/false);
      }
    }
    ++FI;
  }
}

/// Check if duplication function is required for taskloops.
static bool checkInitIsRequired(CodeGenFunction &CGF,
                                ArrayRef<PrivateDataTy> Privates) {
  bool InitRequired = false;
  for (const PrivateDataTy &Pair : Privates) {
    if (Pair.second.isLocalPrivate())
      continue;
    const VarDecl *VD = Pair.second.PrivateCopy;
    const Expr *Init = VD->getAnyInitializer();
    InitRequired = InitRequired || (isa_and_nonnull<CXXConstructExpr>(Init) &&
                                    !CGF.isTrivialInitializer(Init));
    if (InitRequired)
      break;
  }
  return InitRequired;
}


/// Emit task_dup function (for initialization of
/// private/firstprivate/lastprivate vars and last_iter flag)
/// \code
/// void __task_dup_entry(kmp_task_t *task_dst, const kmp_task_t *task_src, int
/// lastpriv) {
/// // setup lastprivate flag
///    task_dst->last = lastpriv;
/// // could be constructor calls here...
/// }
/// \endcode
static llvm::Value *
emitTaskDupFunction(CodeGenModule &CGM, SourceLocation Loc,
                    const OMPExecutableDirective &D,
                    QualType KmpTaskTWithPrivatesPtrQTy,
                    const RecordDecl *KmpTaskTWithPrivatesQTyRD,
                    const RecordDecl *KmpTaskTQTyRD, QualType SharedsTy,
                    QualType SharedsPtrTy, const OMPTaskDataTy &Data,
                    ArrayRef<PrivateDataTy> Privates, bool WithLastIter) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl DstArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                           KmpTaskTWithPrivatesPtrQTy,
                           ImplicitParamKind::Other);
  ImplicitParamDecl SrcArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                           KmpTaskTWithPrivatesPtrQTy,
                           ImplicitParamKind::Other);
  ImplicitParamDecl LastprivArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.IntTy,
                                ImplicitParamKind::Other);
  Args.push_back(&DstArg);
  Args.push_back(&SrcArg);
  Args.push_back(&LastprivArg);
  const auto &TaskDupFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *TaskDupTy = CGM.getTypes().GetFunctionType(TaskDupFnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"omp_task_dup", ""});
  auto *TaskDup = llvm::Function::Create(
      TaskDupTy, llvm::GlobalValue::InternalLinkage, Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), TaskDup, TaskDupFnInfo);
  TaskDup->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, TaskDup, TaskDupFnInfo, Args, Loc,
                    Loc);

  LValue TDBase = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&DstArg),
      KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
  // task_dst->liter = lastpriv;
  if (WithLastIter) {
    auto LIFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTLastIter);
    LValue Base = CGF.EmitLValueForField(
        TDBase, *KmpTaskTWithPrivatesQTyRD->field_begin());
    LValue LILVal = CGF.EmitLValueForField(Base, *LIFI);
    llvm::Value *Lastpriv = CGF.EmitLoadOfScalar(
        CGF.GetAddrOfLocalVar(&LastprivArg), /*Volatile=*/false, C.IntTy, Loc);
    CGF.EmitStoreOfScalar(Lastpriv, LILVal);
  }

  // Emit initial values for private copies (if any).
  assert(!Privates.empty());
  Address KmpTaskSharedsPtr = Address::invalid();
  if (!Data.FirstprivateVars.empty()) {
    LValue TDBase = CGF.EmitLoadOfPointerLValue(
        CGF.GetAddrOfLocalVar(&SrcArg),
        KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
    LValue Base = CGF.EmitLValueForField(
        TDBase, *KmpTaskTWithPrivatesQTyRD->field_begin());
    KmpTaskSharedsPtr = Address(
        CGF.EmitLoadOfScalar(CGF.EmitLValueForField(
                                 Base, *std::next(KmpTaskTQTyRD->field_begin(),
                                                  KmpTaskTShareds)),
                             Loc),
        CGF.Int8Ty, CGM.getNaturalTypeAlignment(SharedsTy));
  }
  emitPrivatesInit(CGF, D, KmpTaskSharedsPtr, TDBase, KmpTaskTWithPrivatesQTyRD,
                   SharedsTy, SharedsPtrTy, Data, Privates, /*ForDup=*/true);
  CGF.FinishFunction();
  return TaskDup;
}

/// Checks if destructor function is required to be generated.
/// \return true if cleanups are required, false otherwise.
static bool
checkDestructorsRequired(const RecordDecl *KmpTaskTWithPrivatesQTyRD,
                         ArrayRef<PrivateDataTy> Privates) {
  for (const PrivateDataTy &P : Privates) {
    if (P.second.isLocalPrivate())
      continue;
    QualType Ty = P.second.Original->getType().getNonReferenceType();
    if (Ty.isDestructedType())
      return true;
  }
  return false;
}

namespace {
/// Loop generator for OpenMP iterator expression.
class OMPIteratorGeneratorScope final
    : public CodeGenFunction::OMPPrivateScope {
  CodeGenFunction &CGF;
  const OMPIteratorExpr *E = nullptr;
  SmallVector<CodeGenFunction::JumpDest, 4> ContDests;
  SmallVector<CodeGenFunction::JumpDest, 4> ExitDests;
  OMPIteratorGeneratorScope() = delete;
  OMPIteratorGeneratorScope(OMPIteratorGeneratorScope &) = delete;

public:
  OMPIteratorGeneratorScope(CodeGenFunction &CGF, const OMPIteratorExpr *E)
      : CodeGenFunction::OMPPrivateScope(CGF), CGF(CGF), E(E) {
    if (!E)
      return;
    SmallVector<llvm::Value *, 4> Uppers;
    for (unsigned I = 0, End = E->numOfIterators(); I < End; ++I) {
      Uppers.push_back(CGF.EmitScalarExpr(E->getHelper(I).Upper));
      const auto *VD = cast<VarDecl>(E->getIteratorDecl(I));
      addPrivate(VD, CGF.CreateMemTemp(VD->getType(), VD->getName()));
      const OMPIteratorHelperData &HelperData = E->getHelper(I);
      addPrivate(
          HelperData.CounterVD,
          CGF.CreateMemTemp(HelperData.CounterVD->getType(), "counter.addr"));
    }
    Privatize();

    for (unsigned I = 0, End = E->numOfIterators(); I < End; ++I) {
      const OMPIteratorHelperData &HelperData = E->getHelper(I);
      LValue CLVal =
          CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(HelperData.CounterVD),
                             HelperData.CounterVD->getType());
      // Counter = 0;
      CGF.EmitStoreOfScalar(
          llvm::ConstantInt::get(CLVal.getAddress().getElementType(), 0),
          CLVal);
      CodeGenFunction::JumpDest &ContDest =
          ContDests.emplace_back(CGF.getJumpDestInCurrentScope("iter.cont"));
      CodeGenFunction::JumpDest &ExitDest =
          ExitDests.emplace_back(CGF.getJumpDestInCurrentScope("iter.exit"));
      // N = <number-of_iterations>;
      llvm::Value *N = Uppers[I];
      // cont:
      // if (Counter < N) goto body; else goto exit;
      CGF.EmitBlock(ContDest.getBlock());
      auto *CVal =
          CGF.EmitLoadOfScalar(CLVal, HelperData.CounterVD->getLocation());
      llvm::Value *Cmp =
          HelperData.CounterVD->getType()->isSignedIntegerOrEnumerationType()
              ? CGF.Builder.CreateICmpSLT(CVal, N)
              : CGF.Builder.CreateICmpULT(CVal, N);
      llvm::BasicBlock *BodyBB = CGF.createBasicBlock("iter.body");
      CGF.Builder.CreateCondBr(Cmp, BodyBB, ExitDest.getBlock());
      // body:
      CGF.EmitBlock(BodyBB);
      // Iteri = Begini + Counter * Stepi;
      CGF.EmitIgnoredExpr(HelperData.Update);
    }
  }
  ~OMPIteratorGeneratorScope() {
    if (!E)
      return;
    for (unsigned I = E->numOfIterators(); I > 0; --I) {
      // Counter = Counter + 1;
      const OMPIteratorHelperData &HelperData = E->getHelper(I - 1);
      CGF.EmitIgnoredExpr(HelperData.CounterUpdate);
      // goto cont;
      CGF.EmitBranchThroughCleanup(ContDests[I - 1]);
      // exit:
      CGF.EmitBlock(ExitDests[I - 1].getBlock(), /*IsFinished=*/I == 1);
    }
  }
};
} // namespace

static std::pair<llvm::Value *, llvm::Value *>
getPointerAndSize(CodeGenFunction &CGF, const Expr *E) {
  const auto *OASE = dyn_cast<OMPArrayShapingExpr>(E);
  llvm::Value *Addr;
  if (OASE) {
    const Expr *Base = OASE->getBase();
    Addr = CGF.EmitScalarExpr(Base);
  } else {
    Addr = CGF.EmitLValue(E).getPointer(CGF);
  }
  llvm::Value *SizeVal;
  QualType Ty = E->getType();
  if (OASE) {
    SizeVal = CGF.getTypeSize(OASE->getBase()->getType()->getPointeeType());
    for (const Expr *SE : OASE->getDimensions()) {
      llvm::Value *Sz = CGF.EmitScalarExpr(SE);
      Sz = CGF.EmitScalarConversion(
          Sz, SE->getType(), CGF.getContext().getSizeType(), SE->getExprLoc());
      SizeVal = CGF.Builder.CreateNUWMul(SizeVal, Sz);
    }
  } else if (const auto *ASE =
                 dyn_cast<ArraySectionExpr>(E->IgnoreParenImpCasts())) {
    LValue UpAddrLVal = CGF.EmitArraySectionExpr(ASE, /*IsLowerBound=*/false);
    Address UpAddrAddress = UpAddrLVal.getAddress();
    llvm::Value *UpAddr = CGF.Builder.CreateConstGEP1_32(
        UpAddrAddress.getElementType(), UpAddrAddress.emitRawPointer(CGF),
        /*Idx0=*/1);
    llvm::Value *LowIntPtr = CGF.Builder.CreatePtrToInt(Addr, CGF.SizeTy);
    llvm::Value *UpIntPtr = CGF.Builder.CreatePtrToInt(UpAddr, CGF.SizeTy);
    SizeVal = CGF.Builder.CreateNUWSub(UpIntPtr, LowIntPtr);
  } else {
    SizeVal = CGF.getTypeSize(Ty);
  }
  return std::make_pair(Addr, SizeVal);
}

/// Builds kmp_depend_info, if it is not built yet, and builds flags type.
static void getKmpAffinityType(ASTContext &C, QualType &KmpTaskAffinityInfoTy) {
  QualType FlagsTy = C.getIntTypeForBitwidth(32, /*Signed=*/false);
  if (KmpTaskAffinityInfoTy.isNull()) {
    RecordDecl *KmpAffinityInfoRD =
        C.buildImplicitRecord("kmp_task_affinity_info_t");
    KmpAffinityInfoRD->startDefinition();
    addFieldToRecordDecl(C, KmpAffinityInfoRD, C.getIntPtrType());
    addFieldToRecordDecl(C, KmpAffinityInfoRD, C.getSizeType());
    addFieldToRecordDecl(C, KmpAffinityInfoRD, FlagsTy);
    KmpAffinityInfoRD->completeDefinition();
    KmpTaskAffinityInfoTy = C.getRecordType(KmpAffinityInfoRD);
  }
}

CGOpenMPRuntime::TaskResultTy
CGOpenMPRuntime::emitTaskInit(CodeGenFunction &CGF, SourceLocation Loc,
                              const OMPExecutableDirective &D,
                              llvm::Function *TaskFunction, QualType SharedsTy,
                              Address Shareds, const OMPTaskDataTy &Data) {
  ASTContext &C = CGM.getContext();
  llvm::SmallVector<PrivateDataTy, 4> Privates;
  // Aggregate privates and sort them by the alignment.
  const auto *I = Data.PrivateCopies.begin();
  for (const Expr *E : Data.PrivateVars) {
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    Privates.emplace_back(
        C.getDeclAlign(VD),
        PrivateHelpersTy(E, VD, cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl()),
                         /*PrivateElemInit=*/nullptr));
    ++I;
  }
  I = Data.FirstprivateCopies.begin();
  const auto *IElemInitRef = Data.FirstprivateInits.begin();
  for (const Expr *E : Data.FirstprivateVars) {
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    Privates.emplace_back(
        C.getDeclAlign(VD),
        PrivateHelpersTy(
            E, VD, cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl()),
            cast<VarDecl>(cast<DeclRefExpr>(*IElemInitRef)->getDecl())));
    ++I;
    ++IElemInitRef;
  }
  I = Data.LastprivateCopies.begin();
  for (const Expr *E : Data.LastprivateVars) {
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    Privates.emplace_back(
        C.getDeclAlign(VD),
        PrivateHelpersTy(E, VD, cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl()),
                         /*PrivateElemInit=*/nullptr));
    ++I;
  }
  for (const VarDecl *VD : Data.PrivateLocals) {
    if (isAllocatableDecl(VD))
      Privates.emplace_back(CGM.getPointerAlign(), PrivateHelpersTy(VD));
    else
      Privates.emplace_back(C.getDeclAlign(VD), PrivateHelpersTy(VD));
  }
  llvm::stable_sort(Privates,
                    [](const PrivateDataTy &L, const PrivateDataTy &R) {
                      return L.first > R.first;
                    });
  QualType KmpInt32Ty = C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1);
  // Build type kmp_routine_entry_t (if not built yet).
  emitKmpRoutineEntryT(KmpInt32Ty);
  // Build type kmp_task_t (if not built yet).
  if (isOpenMPTaskLoopDirective(D.getDirectiveKind())) {
    if (SavedKmpTaskloopTQTy.isNull()) {
      SavedKmpTaskloopTQTy = C.getRecordType(createKmpTaskTRecordDecl(
          CGM, D.getDirectiveKind(), KmpInt32Ty, KmpRoutineEntryPtrQTy));
    }
    KmpTaskTQTy = SavedKmpTaskloopTQTy;
  } else {
    assert((D.getDirectiveKind() == OMPD_task ||
            isOpenMPTargetExecutionDirective(D.getDirectiveKind()) ||
            isOpenMPTargetDataManagementDirective(D.getDirectiveKind())) &&
           "Expected taskloop, task or target directive");
    if (SavedKmpTaskTQTy.isNull()) {
      SavedKmpTaskTQTy = C.getRecordType(createKmpTaskTRecordDecl(
          CGM, D.getDirectiveKind(), KmpInt32Ty, KmpRoutineEntryPtrQTy));
    }
    KmpTaskTQTy = SavedKmpTaskTQTy;
  }
  const auto *KmpTaskTQTyRD = cast<RecordDecl>(KmpTaskTQTy->getAsTagDecl());
  // Build particular struct kmp_task_t for the given task.
  const RecordDecl *KmpTaskTWithPrivatesQTyRD =
      createKmpTaskTWithPrivatesRecordDecl(CGM, KmpTaskTQTy, Privates);
  QualType KmpTaskTWithPrivatesQTy = C.getRecordType(KmpTaskTWithPrivatesQTyRD);
  QualType KmpTaskTWithPrivatesPtrQTy =
      C.getPointerType(KmpTaskTWithPrivatesQTy);
  llvm::Type *KmpTaskTWithPrivatesTy = CGF.ConvertType(KmpTaskTWithPrivatesQTy);
  llvm::Type *KmpTaskTWithPrivatesPtrTy =
      KmpTaskTWithPrivatesTy->getPointerTo();
  llvm::Value *KmpTaskTWithPrivatesTySize =
      CGF.getTypeSize(KmpTaskTWithPrivatesQTy);
  QualType SharedsPtrTy = C.getPointerType(SharedsTy);

  // Emit initial values for private copies (if any).
  llvm::Value *TaskPrivatesMap = nullptr;
  llvm::Type *TaskPrivatesMapTy =
      std::next(TaskFunction->arg_begin(), 3)->getType();
  if (!Privates.empty()) {
    auto FI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin());
    TaskPrivatesMap =
        emitTaskPrivateMappingFunction(CGM, Loc, Data, FI->getType(), Privates);
    TaskPrivatesMap = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        TaskPrivatesMap, TaskPrivatesMapTy);
  } else {
    TaskPrivatesMap = llvm::ConstantPointerNull::get(
        cast<llvm::PointerType>(TaskPrivatesMapTy));
  }
  // Build a proxy function kmp_int32 .omp_task_entry.(kmp_int32 gtid,
  // kmp_task_t *tt);
  llvm::Function *TaskEntry = emitProxyTaskFunction(
      CGM, Loc, D.getDirectiveKind(), KmpInt32Ty, KmpTaskTWithPrivatesPtrQTy,
      KmpTaskTWithPrivatesQTy, KmpTaskTQTy, SharedsPtrTy, TaskFunction,
      TaskPrivatesMap);

  // Build call kmp_task_t * __kmpc_omp_task_alloc(ident_t *, kmp_int32 gtid,
  // kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  // kmp_routine_entry_t *task_entry);
  // Task flags. Format is taken from
  // https://github.com/llvm/llvm-project/blob/main/openmp/runtime/src/kmp.h,
  // description of kmp_tasking_flags struct.
  enum {
    TiedFlag = 0x1,
    FinalFlag = 0x2,
    DestructorsFlag = 0x8,
    PriorityFlag = 0x20,
    DetachableFlag = 0x40,
  };
  unsigned Flags = Data.Tied ? TiedFlag : 0;
  bool NeedsCleanup = false;
  if (!Privates.empty()) {
    NeedsCleanup =
        checkDestructorsRequired(KmpTaskTWithPrivatesQTyRD, Privates);
    if (NeedsCleanup)
      Flags = Flags | DestructorsFlag;
  }
  if (Data.Priority.getInt())
    Flags = Flags | PriorityFlag;
  if (D.hasClausesOfKind<OMPDetachClause>())
    Flags = Flags | DetachableFlag;
  llvm::Value *TaskFlags =
      Data.Final.getPointer()
          ? CGF.Builder.CreateSelect(Data.Final.getPointer(),
                                     CGF.Builder.getInt32(FinalFlag),
                                     CGF.Builder.getInt32(/*C=*/0))
          : CGF.Builder.getInt32(Data.Final.getInt() ? FinalFlag : 0);
  TaskFlags = CGF.Builder.CreateOr(TaskFlags, CGF.Builder.getInt32(Flags));
  llvm::Value *SharedsSize = CGM.getSize(C.getTypeSizeInChars(SharedsTy));
  SmallVector<llvm::Value *, 8> AllocArgs = {emitUpdateLocation(CGF, Loc),
      getThreadID(CGF, Loc), TaskFlags, KmpTaskTWithPrivatesTySize,
      SharedsSize, CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          TaskEntry, KmpRoutineEntryPtrTy)};
  llvm::Value *NewTask;
  if (D.hasClausesOfKind<OMPNowaitClause>()) {
    // Check if we have any device clause associated with the directive.
    const Expr *Device = nullptr;
    if (auto *C = D.getSingleClause<OMPDeviceClause>())
      Device = C->getDevice();
    // Emit device ID if any otherwise use default value.
    llvm::Value *DeviceID;
    if (Device)
      DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                           CGF.Int64Ty, /*isSigned=*/true);
    else
      DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
    AllocArgs.push_back(DeviceID);
    NewTask = CGF.EmitRuntimeCall(
        OMPBuilder.getOrCreateRuntimeFunction(
            CGM.getModule(), OMPRTL___kmpc_omp_target_task_alloc),
        AllocArgs);
  } else {
    NewTask =
        CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                                CGM.getModule(), OMPRTL___kmpc_omp_task_alloc),
                            AllocArgs);
  }
  // Emit detach clause initialization.
  // evt = (typeof(evt))__kmpc_task_allow_completion_event(loc, tid,
  // task_descriptor);
  if (const auto *DC = D.getSingleClause<OMPDetachClause>()) {
    const Expr *Evt = DC->getEventHandler()->IgnoreParenImpCasts();
    LValue EvtLVal = CGF.EmitLValue(Evt);

    // Build kmp_event_t *__kmpc_task_allow_completion_event(ident_t *loc_ref,
    // int gtid, kmp_task_t *task);
    llvm::Value *Loc = emitUpdateLocation(CGF, DC->getBeginLoc());
    llvm::Value *Tid = getThreadID(CGF, DC->getBeginLoc());
    Tid = CGF.Builder.CreateIntCast(Tid, CGF.IntTy, /*isSigned=*/false);
    llvm::Value *EvtVal = CGF.EmitRuntimeCall(
        OMPBuilder.getOrCreateRuntimeFunction(
            CGM.getModule(), OMPRTL___kmpc_task_allow_completion_event),
        {Loc, Tid, NewTask});
    EvtVal = CGF.EmitScalarConversion(EvtVal, C.VoidPtrTy, Evt->getType(),
                                      Evt->getExprLoc());
    CGF.EmitStoreOfScalar(EvtVal, EvtLVal);
  }
  // Process affinity clauses.
  if (D.hasClausesOfKind<OMPAffinityClause>()) {
    // Process list of affinity data.
    ASTContext &C = CGM.getContext();
    Address AffinitiesArray = Address::invalid();
    // Calculate number of elements to form the array of affinity data.
    llvm::Value *NumOfElements = nullptr;
    unsigned NumAffinities = 0;
    for (const auto *C : D.getClausesOfKind<OMPAffinityClause>()) {
      if (const Expr *Modifier = C->getModifier()) {
        const auto *IE = cast<OMPIteratorExpr>(Modifier->IgnoreParenImpCasts());
        for (unsigned I = 0, E = IE->numOfIterators(); I < E; ++I) {
          llvm::Value *Sz = CGF.EmitScalarExpr(IE->getHelper(I).Upper);
          Sz = CGF.Builder.CreateIntCast(Sz, CGF.SizeTy, /*isSigned=*/false);
          NumOfElements =
              NumOfElements ? CGF.Builder.CreateNUWMul(NumOfElements, Sz) : Sz;
        }
      } else {
        NumAffinities += C->varlist_size();
      }
    }
    getKmpAffinityType(CGM.getContext(), KmpTaskAffinityInfoTy);
    // Fields ids in kmp_task_affinity_info record.
    enum RTLAffinityInfoFieldsTy { BaseAddr, Len, Flags };

    QualType KmpTaskAffinityInfoArrayTy;
    if (NumOfElements) {
      NumOfElements = CGF.Builder.CreateNUWAdd(
          llvm::ConstantInt::get(CGF.SizeTy, NumAffinities), NumOfElements);
      auto *OVE = new (C) OpaqueValueExpr(
          Loc,
          C.getIntTypeForBitwidth(C.getTypeSize(C.getSizeType()), /*Signed=*/0),
          VK_PRValue);
      CodeGenFunction::OpaqueValueMapping OpaqueMap(CGF, OVE,
                                                    RValue::get(NumOfElements));
      KmpTaskAffinityInfoArrayTy = C.getVariableArrayType(
          KmpTaskAffinityInfoTy, OVE, ArraySizeModifier::Normal,
          /*IndexTypeQuals=*/0, SourceRange(Loc, Loc));
      // Properly emit variable-sized array.
      auto *PD = ImplicitParamDecl::Create(C, KmpTaskAffinityInfoArrayTy,
                                           ImplicitParamKind::Other);
      CGF.EmitVarDecl(*PD);
      AffinitiesArray = CGF.GetAddrOfLocalVar(PD);
      NumOfElements = CGF.Builder.CreateIntCast(NumOfElements, CGF.Int32Ty,
                                                /*isSigned=*/false);
    } else {
      KmpTaskAffinityInfoArrayTy = C.getConstantArrayType(
          KmpTaskAffinityInfoTy,
          llvm::APInt(C.getTypeSize(C.getSizeType()), NumAffinities), nullptr,
          ArraySizeModifier::Normal, /*IndexTypeQuals=*/0);
      AffinitiesArray =
          CGF.CreateMemTemp(KmpTaskAffinityInfoArrayTy, ".affs.arr.addr");
      AffinitiesArray = CGF.Builder.CreateConstArrayGEP(AffinitiesArray, 0);
      NumOfElements = llvm::ConstantInt::get(CGM.Int32Ty, NumAffinities,
                                             /*isSigned=*/false);
    }

    const auto *KmpAffinityInfoRD = KmpTaskAffinityInfoTy->getAsRecordDecl();
    // Fill array by elements without iterators.
    unsigned Pos = 0;
    bool HasIterator = false;
    for (const auto *C : D.getClausesOfKind<OMPAffinityClause>()) {
      if (C->getModifier()) {
        HasIterator = true;
        continue;
      }
      for (const Expr *E : C->varlists()) {
        llvm::Value *Addr;
        llvm::Value *Size;
        std::tie(Addr, Size) = getPointerAndSize(CGF, E);
        LValue Base =
            CGF.MakeAddrLValue(CGF.Builder.CreateConstGEP(AffinitiesArray, Pos),
                               KmpTaskAffinityInfoTy);
        // affs[i].base_addr = &<Affinities[i].second>;
        LValue BaseAddrLVal = CGF.EmitLValueForField(
            Base, *std::next(KmpAffinityInfoRD->field_begin(), BaseAddr));
        CGF.EmitStoreOfScalar(CGF.Builder.CreatePtrToInt(Addr, CGF.IntPtrTy),
                              BaseAddrLVal);
        // affs[i].len = sizeof(<Affinities[i].second>);
        LValue LenLVal = CGF.EmitLValueForField(
            Base, *std::next(KmpAffinityInfoRD->field_begin(), Len));
        CGF.EmitStoreOfScalar(Size, LenLVal);
        ++Pos;
      }
    }
    LValue PosLVal;
    if (HasIterator) {
      PosLVal = CGF.MakeAddrLValue(
          CGF.CreateMemTemp(C.getSizeType(), "affs.counter.addr"),
          C.getSizeType());
      CGF.EmitStoreOfScalar(llvm::ConstantInt::get(CGF.SizeTy, Pos), PosLVal);
    }
    // Process elements with iterators.
    for (const auto *C : D.getClausesOfKind<OMPAffinityClause>()) {
      const Expr *Modifier = C->getModifier();
      if (!Modifier)
        continue;
      OMPIteratorGeneratorScope IteratorScope(
          CGF, cast_or_null<OMPIteratorExpr>(Modifier->IgnoreParenImpCasts()));
      for (const Expr *E : C->varlists()) {
        llvm::Value *Addr;
        llvm::Value *Size;
        std::tie(Addr, Size) = getPointerAndSize(CGF, E);
        llvm::Value *Idx = CGF.EmitLoadOfScalar(PosLVal, E->getExprLoc());
        LValue Base =
            CGF.MakeAddrLValue(CGF.Builder.CreateGEP(CGF, AffinitiesArray, Idx),
                               KmpTaskAffinityInfoTy);
        // affs[i].base_addr = &<Affinities[i].second>;
        LValue BaseAddrLVal = CGF.EmitLValueForField(
            Base, *std::next(KmpAffinityInfoRD->field_begin(), BaseAddr));
        CGF.EmitStoreOfScalar(CGF.Builder.CreatePtrToInt(Addr, CGF.IntPtrTy),
                              BaseAddrLVal);
        // affs[i].len = sizeof(<Affinities[i].second>);
        LValue LenLVal = CGF.EmitLValueForField(
            Base, *std::next(KmpAffinityInfoRD->field_begin(), Len));
        CGF.EmitStoreOfScalar(Size, LenLVal);
        Idx = CGF.Builder.CreateNUWAdd(
            Idx, llvm::ConstantInt::get(Idx->getType(), 1));
        CGF.EmitStoreOfScalar(Idx, PosLVal);
      }
    }
    // Call to kmp_int32 __kmpc_omp_reg_task_with_affinity(ident_t *loc_ref,
    // kmp_int32 gtid, kmp_task_t *new_task, kmp_int32
    // naffins, kmp_task_affinity_info_t *affin_list);
    llvm::Value *LocRef = emitUpdateLocation(CGF, Loc);
    llvm::Value *GTid = getThreadID(CGF, Loc);
    llvm::Value *AffinListPtr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        AffinitiesArray.emitRawPointer(CGF), CGM.VoidPtrTy);
    // FIXME: Emit the function and ignore its result for now unless the
    // runtime function is properly implemented.
    (void)CGF.EmitRuntimeCall(
        OMPBuilder.getOrCreateRuntimeFunction(
            CGM.getModule(), OMPRTL___kmpc_omp_reg_task_with_affinity),
        {LocRef, GTid, NewTask, NumOfElements, AffinListPtr});
  }
  llvm::Value *NewTaskNewTaskTTy =
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          NewTask, KmpTaskTWithPrivatesPtrTy);
  LValue Base = CGF.MakeNaturalAlignRawAddrLValue(NewTaskNewTaskTTy,
                                                  KmpTaskTWithPrivatesQTy);
  LValue TDBase =
      CGF.EmitLValueForField(Base, *KmpTaskTWithPrivatesQTyRD->field_begin());
  // Fill the data in the resulting kmp_task_t record.
  // Copy shareds if there are any.
  Address KmpTaskSharedsPtr = Address::invalid();
  if (!SharedsTy->getAsStructureType()->getDecl()->field_empty()) {
    KmpTaskSharedsPtr = Address(
        CGF.EmitLoadOfScalar(
            CGF.EmitLValueForField(
                TDBase,
                *std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTShareds)),
            Loc),
        CGF.Int8Ty, CGM.getNaturalTypeAlignment(SharedsTy));
    LValue Dest = CGF.MakeAddrLValue(KmpTaskSharedsPtr, SharedsTy);
    LValue Src = CGF.MakeAddrLValue(Shareds, SharedsTy);
    CGF.EmitAggregateCopy(Dest, Src, SharedsTy, AggValueSlot::DoesNotOverlap);
  }
  // Emit initial values for private copies (if any).
  TaskResultTy Result;
  if (!Privates.empty()) {
    emitPrivatesInit(CGF, D, KmpTaskSharedsPtr, Base, KmpTaskTWithPrivatesQTyRD,
                     SharedsTy, SharedsPtrTy, Data, Privates,
                     /*ForDup=*/false);
    if (isOpenMPTaskLoopDirective(D.getDirectiveKind()) &&
        (!Data.LastprivateVars.empty() || checkInitIsRequired(CGF, Privates))) {
      Result.TaskDupFn = emitTaskDupFunction(
          CGM, Loc, D, KmpTaskTWithPrivatesPtrQTy, KmpTaskTWithPrivatesQTyRD,
          KmpTaskTQTyRD, SharedsTy, SharedsPtrTy, Data, Privates,
          /*WithLastIter=*/!Data.LastprivateVars.empty());
    }
  }
  // Fields of union "kmp_cmplrdata_t" for destructors and priority.
  enum { Priority = 0, Destructors = 1 };
  // Provide pointer to function with destructors for privates.
  auto FI = std::next(KmpTaskTQTyRD->field_begin(), Data1);
  const RecordDecl *KmpCmplrdataUD =
      (*FI)->getType()->getAsUnionType()->getDecl();
  if (NeedsCleanup) {
    llvm::Value *DestructorFn = emitDestructorsFunction(
        CGM, Loc, KmpInt32Ty, KmpTaskTWithPrivatesPtrQTy,
        KmpTaskTWithPrivatesQTy);
    LValue Data1LV = CGF.EmitLValueForField(TDBase, *FI);
    LValue DestructorsLV = CGF.EmitLValueForField(
        Data1LV, *std::next(KmpCmplrdataUD->field_begin(), Destructors));
    CGF.EmitStoreOfScalar(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                              DestructorFn, KmpRoutineEntryPtrTy),
                          DestructorsLV);
  }
  // Set priority.
  if (Data.Priority.getInt()) {
    LValue Data2LV = CGF.EmitLValueForField(
        TDBase, *std::next(KmpTaskTQTyRD->field_begin(), Data2));
    LValue PriorityLV = CGF.EmitLValueForField(
        Data2LV, *std::next(KmpCmplrdataUD->field_begin(), Priority));
    CGF.EmitStoreOfScalar(Data.Priority.getPointer(), PriorityLV);
  }
  Result.NewTask = NewTask;
  Result.TaskEntry = TaskEntry;
  Result.NewTaskNewTaskTTy = NewTaskNewTaskTTy;
  Result.TDBase = TDBase;
  Result.KmpTaskTQTyRD = KmpTaskTQTyRD;
  return Result;
}

/// Translates internal dependency kind into the runtime kind.
static RTLDependenceKindTy translateDependencyKind(OpenMPDependClauseKind K) {
  RTLDependenceKindTy DepKind;
  switch (K) {
  case OMPC_DEPEND_in:
    DepKind = RTLDependenceKindTy::DepIn;
    break;
  // Out and InOut dependencies must use the same code.
  case OMPC_DEPEND_out:
  case OMPC_DEPEND_inout:
    DepKind = RTLDependenceKindTy::DepInOut;
    break;
  case OMPC_DEPEND_mutexinoutset:
    DepKind = RTLDependenceKindTy::DepMutexInOutSet;
    break;
  case OMPC_DEPEND_inoutset:
    DepKind = RTLDependenceKindTy::DepInOutSet;
    break;
  case OMPC_DEPEND_outallmemory:
    DepKind = RTLDependenceKindTy::DepOmpAllMem;
    break;
  case OMPC_DEPEND_source:
  case OMPC_DEPEND_sink:
  case OMPC_DEPEND_depobj:
  case OMPC_DEPEND_inoutallmemory:
  case OMPC_DEPEND_unknown:
    llvm_unreachable("Unknown task dependence type");
  }
  return DepKind;
}

/// Builds kmp_depend_info, if it is not built yet, and builds flags type.
static void getDependTypes(ASTContext &C, QualType &KmpDependInfoTy,
                           QualType &FlagsTy) {
  FlagsTy = C.getIntTypeForBitwidth(C.getTypeSize(C.BoolTy), /*Signed=*/false);
  if (KmpDependInfoTy.isNull()) {
    RecordDecl *KmpDependInfoRD = C.buildImplicitRecord("kmp_depend_info");
    KmpDependInfoRD->startDefinition();
    addFieldToRecordDecl(C, KmpDependInfoRD, C.getIntPtrType());
    addFieldToRecordDecl(C, KmpDependInfoRD, C.getSizeType());
    addFieldToRecordDecl(C, KmpDependInfoRD, FlagsTy);
    KmpDependInfoRD->completeDefinition();
    KmpDependInfoTy = C.getRecordType(KmpDependInfoRD);
  }
}

std::pair<llvm::Value *, LValue>
CGOpenMPRuntime::getDepobjElements(CodeGenFunction &CGF, LValue DepobjLVal,
                                   SourceLocation Loc) {
  ASTContext &C = CGM.getContext();
  QualType FlagsTy;
  getDependTypes(C, KmpDependInfoTy, FlagsTy);
  RecordDecl *KmpDependInfoRD =
      cast<RecordDecl>(KmpDependInfoTy->getAsTagDecl());
  QualType KmpDependInfoPtrTy = C.getPointerType(KmpDependInfoTy);
  LValue Base = CGF.EmitLoadOfPointerLValue(
      DepobjLVal.getAddress().withElementType(
          CGF.ConvertTypeForMem(KmpDependInfoPtrTy)),
      KmpDependInfoPtrTy->castAs<PointerType>());
  Address DepObjAddr = CGF.Builder.CreateGEP(
      CGF, Base.getAddress(),
      llvm::ConstantInt::get(CGF.IntPtrTy, -1, /*isSigned=*/true));
  LValue NumDepsBase = CGF.MakeAddrLValue(
      DepObjAddr, KmpDependInfoTy, Base.getBaseInfo(), Base.getTBAAInfo());
  // NumDeps = deps[i].base_addr;
  LValue BaseAddrLVal = CGF.EmitLValueForField(
      NumDepsBase,
      *std::next(KmpDependInfoRD->field_begin(),
                 static_cast<unsigned int>(RTLDependInfoFields::BaseAddr)));
  llvm::Value *NumDeps = CGF.EmitLoadOfScalar(BaseAddrLVal, Loc);
  return std::make_pair(NumDeps, Base);
}

static void emitDependData(CodeGenFunction &CGF, QualType &KmpDependInfoTy,
                           llvm::PointerUnion<unsigned *, LValue *> Pos,
                           const OMPTaskDataTy::DependData &Data,
                           Address DependenciesArray) {
  CodeGenModule &CGM = CGF.CGM;
  ASTContext &C = CGM.getContext();
  QualType FlagsTy;
  getDependTypes(C, KmpDependInfoTy, FlagsTy);
  RecordDecl *KmpDependInfoRD =
      cast<RecordDecl>(KmpDependInfoTy->getAsTagDecl());
  llvm::Type *LLVMFlagsTy = CGF.ConvertTypeForMem(FlagsTy);

  OMPIteratorGeneratorScope IteratorScope(
      CGF, cast_or_null<OMPIteratorExpr>(
               Data.IteratorExpr ? Data.IteratorExpr->IgnoreParenImpCasts()
                                 : nullptr));
  for (const Expr *E : Data.DepExprs) {
    llvm::Value *Addr;
    llvm::Value *Size;

    // The expression will be a nullptr in the 'omp_all_memory' case.
    if (E) {
      std::tie(Addr, Size) = getPointerAndSize(CGF, E);
      Addr = CGF.Builder.CreatePtrToInt(Addr, CGF.IntPtrTy);
    } else {
      Addr = llvm::ConstantInt::get(CGF.IntPtrTy, 0);
      Size = llvm::ConstantInt::get(CGF.SizeTy, 0);
    }
    LValue Base;
    if (unsigned *P = Pos.dyn_cast<unsigned *>()) {
      Base = CGF.MakeAddrLValue(
          CGF.Builder.CreateConstGEP(DependenciesArray, *P), KmpDependInfoTy);
    } else {
      assert(E && "Expected a non-null expression");
      LValue &PosLVal = *Pos.get<LValue *>();
      llvm::Value *Idx = CGF.EmitLoadOfScalar(PosLVal, E->getExprLoc());
      Base = CGF.MakeAddrLValue(
          CGF.Builder.CreateGEP(CGF, DependenciesArray, Idx), KmpDependInfoTy);
    }
    // deps[i].base_addr = &<Dependencies[i].second>;
    LValue BaseAddrLVal = CGF.EmitLValueForField(
        Base,
        *std::next(KmpDependInfoRD->field_begin(),
                   static_cast<unsigned int>(RTLDependInfoFields::BaseAddr)));
    CGF.EmitStoreOfScalar(Addr, BaseAddrLVal);
    // deps[i].len = sizeof(<Dependencies[i].second>);
    LValue LenLVal = CGF.EmitLValueForField(
        Base, *std::next(KmpDependInfoRD->field_begin(),
                         static_cast<unsigned int>(RTLDependInfoFields::Len)));
    CGF.EmitStoreOfScalar(Size, LenLVal);
    // deps[i].flags = <Dependencies[i].first>;
    RTLDependenceKindTy DepKind = translateDependencyKind(Data.DepKind);
    LValue FlagsLVal = CGF.EmitLValueForField(
        Base,
        *std::next(KmpDependInfoRD->field_begin(),
                   static_cast<unsigned int>(RTLDependInfoFields::Flags)));
    CGF.EmitStoreOfScalar(
        llvm::ConstantInt::get(LLVMFlagsTy, static_cast<unsigned int>(DepKind)),
        FlagsLVal);
    if (unsigned *P = Pos.dyn_cast<unsigned *>()) {
      ++(*P);
    } else {
      LValue &PosLVal = *Pos.get<LValue *>();
      llvm::Value *Idx = CGF.EmitLoadOfScalar(PosLVal, E->getExprLoc());
      Idx = CGF.Builder.CreateNUWAdd(Idx,
                                     llvm::ConstantInt::get(Idx->getType(), 1));
      CGF.EmitStoreOfScalar(Idx, PosLVal);
    }
  }
}

SmallVector<llvm::Value *, 4> CGOpenMPRuntime::emitDepobjElementsSizes(
    CodeGenFunction &CGF, QualType &KmpDependInfoTy,
    const OMPTaskDataTy::DependData &Data) {
  assert(Data.DepKind == OMPC_DEPEND_depobj &&
         "Expected depobj dependency kind.");
  SmallVector<llvm::Value *, 4> Sizes;
  SmallVector<LValue, 4> SizeLVals;
  ASTContext &C = CGF.getContext();
  {
    OMPIteratorGeneratorScope IteratorScope(
        CGF, cast_or_null<OMPIteratorExpr>(
                 Data.IteratorExpr ? Data.IteratorExpr->IgnoreParenImpCasts()
                                   : nullptr));
    for (const Expr *E : Data.DepExprs) {
      llvm::Value *NumDeps;
      LValue Base;
      LValue DepobjLVal = CGF.EmitLValue(E->IgnoreParenImpCasts());
      std::tie(NumDeps, Base) =
          getDepobjElements(CGF, DepobjLVal, E->getExprLoc());
      LValue NumLVal = CGF.MakeAddrLValue(
          CGF.CreateMemTemp(C.getUIntPtrType(), "depobj.size.addr"),
          C.getUIntPtrType());
      CGF.Builder.CreateStore(llvm::ConstantInt::get(CGF.IntPtrTy, 0),
                              NumLVal.getAddress());
      llvm::Value *PrevVal = CGF.EmitLoadOfScalar(NumLVal, E->getExprLoc());
      llvm::Value *Add = CGF.Builder.CreateNUWAdd(PrevVal, NumDeps);
      CGF.EmitStoreOfScalar(Add, NumLVal);
      SizeLVals.push_back(NumLVal);
    }
  }
  for (unsigned I = 0, E = SizeLVals.size(); I < E; ++I) {
    llvm::Value *Size =
        CGF.EmitLoadOfScalar(SizeLVals[I], Data.DepExprs[I]->getExprLoc());
    Sizes.push_back(Size);
  }
  return Sizes;
}

void CGOpenMPRuntime::emitDepobjElements(CodeGenFunction &CGF,
                                         QualType &KmpDependInfoTy,
                                         LValue PosLVal,
                                         const OMPTaskDataTy::DependData &Data,
                                         Address DependenciesArray) {
  assert(Data.DepKind == OMPC_DEPEND_depobj &&
         "Expected depobj dependency kind.");
  llvm::Value *ElSize = CGF.getTypeSize(KmpDependInfoTy);
  {
    OMPIteratorGeneratorScope IteratorScope(
        CGF, cast_or_null<OMPIteratorExpr>(
                 Data.IteratorExpr ? Data.IteratorExpr->IgnoreParenImpCasts()
                                   : nullptr));
    for (unsigned I = 0, End = Data.DepExprs.size(); I < End; ++I) {
      const Expr *E = Data.DepExprs[I];
      llvm::Value *NumDeps;
      LValue Base;
      LValue DepobjLVal = CGF.EmitLValue(E->IgnoreParenImpCasts());
      std::tie(NumDeps, Base) =
          getDepobjElements(CGF, DepobjLVal, E->getExprLoc());

      // memcopy dependency data.
      llvm::Value *Size = CGF.Builder.CreateNUWMul(
          ElSize,
          CGF.Builder.CreateIntCast(NumDeps, CGF.SizeTy, /*isSigned=*/false));
      llvm::Value *Pos = CGF.EmitLoadOfScalar(PosLVal, E->getExprLoc());
      Address DepAddr = CGF.Builder.CreateGEP(CGF, DependenciesArray, Pos);
      CGF.Builder.CreateMemCpy(DepAddr, Base.getAddress(), Size);

      // Increase pos.
      // pos += size;
      llvm::Value *Add = CGF.Builder.CreateNUWAdd(Pos, NumDeps);
      CGF.EmitStoreOfScalar(Add, PosLVal);
    }
  }
}

std::pair<llvm::Value *, Address> CGOpenMPRuntime::emitDependClause(
    CodeGenFunction &CGF, ArrayRef<OMPTaskDataTy::DependData> Dependencies,
    SourceLocation Loc) {
  if (llvm::all_of(Dependencies, [](const OMPTaskDataTy::DependData &D) {
        return D.DepExprs.empty();
      }))
    return std::make_pair(nullptr, Address::invalid());
  // Process list of dependencies.
  ASTContext &C = CGM.getContext();
  Address DependenciesArray = Address::invalid();
  llvm::Value *NumOfElements = nullptr;
  unsigned NumDependencies = std::accumulate(
      Dependencies.begin(), Dependencies.end(), 0,
      [](unsigned V, const OMPTaskDataTy::DependData &D) {
        return D.DepKind == OMPC_DEPEND_depobj
                   ? V
                   : (V + (D.IteratorExpr ? 0 : D.DepExprs.size()));
      });
  QualType FlagsTy;
  getDependTypes(C, KmpDependInfoTy, FlagsTy);
  bool HasDepobjDeps = false;
  bool HasRegularWithIterators = false;
  llvm::Value *NumOfDepobjElements = llvm::ConstantInt::get(CGF.IntPtrTy, 0);
  llvm::Value *NumOfRegularWithIterators =
      llvm::ConstantInt::get(CGF.IntPtrTy, 0);
  // Calculate number of depobj dependencies and regular deps with the
  // iterators.
  for (const OMPTaskDataTy::DependData &D : Dependencies) {
    if (D.DepKind == OMPC_DEPEND_depobj) {
      SmallVector<llvm::Value *, 4> Sizes =
          emitDepobjElementsSizes(CGF, KmpDependInfoTy, D);
      for (llvm::Value *Size : Sizes) {
        NumOfDepobjElements =
            CGF.Builder.CreateNUWAdd(NumOfDepobjElements, Size);
      }
      HasDepobjDeps = true;
      continue;
    }
    // Include number of iterations, if any.

    if (const auto *IE = cast_or_null<OMPIteratorExpr>(D.IteratorExpr)) {
      llvm::Value *ClauseIteratorSpace =
          llvm::ConstantInt::get(CGF.IntPtrTy, 1);
      for (unsigned I = 0, E = IE->numOfIterators(); I < E; ++I) {
        llvm::Value *Sz = CGF.EmitScalarExpr(IE->getHelper(I).Upper);
        Sz = CGF.Builder.CreateIntCast(Sz, CGF.IntPtrTy, /*isSigned=*/false);
        ClauseIteratorSpace = CGF.Builder.CreateNUWMul(Sz, ClauseIteratorSpace);
      }
      llvm::Value *NumClauseDeps = CGF.Builder.CreateNUWMul(
          ClauseIteratorSpace,
          llvm::ConstantInt::get(CGF.IntPtrTy, D.DepExprs.size()));
      NumOfRegularWithIterators =
          CGF.Builder.CreateNUWAdd(NumOfRegularWithIterators, NumClauseDeps);
      HasRegularWithIterators = true;
      continue;
    }
  }

  QualType KmpDependInfoArrayTy;
  if (HasDepobjDeps || HasRegularWithIterators) {
    NumOfElements = llvm::ConstantInt::get(CGM.IntPtrTy, NumDependencies,
                                           /*isSigned=*/false);
    if (HasDepobjDeps) {
      NumOfElements =
          CGF.Builder.CreateNUWAdd(NumOfDepobjElements, NumOfElements);
    }
    if (HasRegularWithIterators) {
      NumOfElements =
          CGF.Builder.CreateNUWAdd(NumOfRegularWithIterators, NumOfElements);
    }
    auto *OVE = new (C) OpaqueValueExpr(
        Loc, C.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/0),
        VK_PRValue);
    CodeGenFunction::OpaqueValueMapping OpaqueMap(CGF, OVE,
                                                  RValue::get(NumOfElements));
    KmpDependInfoArrayTy =
        C.getVariableArrayType(KmpDependInfoTy, OVE, ArraySizeModifier::Normal,
                               /*IndexTypeQuals=*/0, SourceRange(Loc, Loc));
    // CGF.EmitVariablyModifiedType(KmpDependInfoArrayTy);
    // Properly emit variable-sized array.
    auto *PD = ImplicitParamDecl::Create(C, KmpDependInfoArrayTy,
                                         ImplicitParamKind::Other);
    CGF.EmitVarDecl(*PD);
    DependenciesArray = CGF.GetAddrOfLocalVar(PD);
    NumOfElements = CGF.Builder.CreateIntCast(NumOfElements, CGF.Int32Ty,
                                              /*isSigned=*/false);
  } else {
    KmpDependInfoArrayTy = C.getConstantArrayType(
        KmpDependInfoTy, llvm::APInt(/*numBits=*/64, NumDependencies), nullptr,
        ArraySizeModifier::Normal, /*IndexTypeQuals=*/0);
    DependenciesArray =
        CGF.CreateMemTemp(KmpDependInfoArrayTy, ".dep.arr.addr");
    DependenciesArray = CGF.Builder.CreateConstArrayGEP(DependenciesArray, 0);
    NumOfElements = llvm::ConstantInt::get(CGM.Int32Ty, NumDependencies,
                                           /*isSigned=*/false);
  }
  unsigned Pos = 0;
  for (unsigned I = 0, End = Dependencies.size(); I < End; ++I) {
    if (Dependencies[I].DepKind == OMPC_DEPEND_depobj ||
        Dependencies[I].IteratorExpr)
      continue;
    emitDependData(CGF, KmpDependInfoTy, &Pos, Dependencies[I],
                   DependenciesArray);
  }
  // Copy regular dependencies with iterators.
  LValue PosLVal = CGF.MakeAddrLValue(
      CGF.CreateMemTemp(C.getSizeType(), "dep.counter.addr"), C.getSizeType());
  CGF.EmitStoreOfScalar(llvm::ConstantInt::get(CGF.SizeTy, Pos), PosLVal);
  for (unsigned I = 0, End = Dependencies.size(); I < End; ++I) {
    if (Dependencies[I].DepKind == OMPC_DEPEND_depobj ||
        !Dependencies[I].IteratorExpr)
      continue;
    emitDependData(CGF, KmpDependInfoTy, &PosLVal, Dependencies[I],
                   DependenciesArray);
  }
  // Copy final depobj arrays without iterators.
  if (HasDepobjDeps) {
    for (unsigned I = 0, End = Dependencies.size(); I < End; ++I) {
      if (Dependencies[I].DepKind != OMPC_DEPEND_depobj)
        continue;
      emitDepobjElements(CGF, KmpDependInfoTy, PosLVal, Dependencies[I],
                         DependenciesArray);
    }
  }
  DependenciesArray = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      DependenciesArray, CGF.VoidPtrTy, CGF.Int8Ty);
  return std::make_pair(NumOfElements, DependenciesArray);
}

Address CGOpenMPRuntime::emitDepobjDependClause(
    CodeGenFunction &CGF, const OMPTaskDataTy::DependData &Dependencies,
    SourceLocation Loc) {
  if (Dependencies.DepExprs.empty())
    return Address::invalid();
  // Process list of dependencies.
  ASTContext &C = CGM.getContext();
  Address DependenciesArray = Address::invalid();
  unsigned NumDependencies = Dependencies.DepExprs.size();
  QualType FlagsTy;
  getDependTypes(C, KmpDependInfoTy, FlagsTy);
  RecordDecl *KmpDependInfoRD =
      cast<RecordDecl>(KmpDependInfoTy->getAsTagDecl());

  llvm::Value *Size;
  // Define type kmp_depend_info[<Dependencies.size()>];
  // For depobj reserve one extra element to store the number of elements.
  // It is required to handle depobj(x) update(in) construct.
  // kmp_depend_info[<Dependencies.size()>] deps;
  llvm::Value *NumDepsVal;
  CharUnits Align = C.getTypeAlignInChars(KmpDependInfoTy);
  if (const auto *IE =
          cast_or_null<OMPIteratorExpr>(Dependencies.IteratorExpr)) {
    NumDepsVal = llvm::ConstantInt::get(CGF.SizeTy, 1);
    for (unsigned I = 0, E = IE->numOfIterators(); I < E; ++I) {
      llvm::Value *Sz = CGF.EmitScalarExpr(IE->getHelper(I).Upper);
      Sz = CGF.Builder.CreateIntCast(Sz, CGF.SizeTy, /*isSigned=*/false);
      NumDepsVal = CGF.Builder.CreateNUWMul(NumDepsVal, Sz);
    }
    Size = CGF.Builder.CreateNUWAdd(llvm::ConstantInt::get(CGF.SizeTy, 1),
                                    NumDepsVal);
    CharUnits SizeInBytes =
        C.getTypeSizeInChars(KmpDependInfoTy).alignTo(Align);
    llvm::Value *RecSize = CGM.getSize(SizeInBytes);
    Size = CGF.Builder.CreateNUWMul(Size, RecSize);
    NumDepsVal =
        CGF.Builder.CreateIntCast(NumDepsVal, CGF.IntPtrTy, /*isSigned=*/false);
  } else {
    QualType KmpDependInfoArrayTy = C.getConstantArrayType(
        KmpDependInfoTy, llvm::APInt(/*numBits=*/64, NumDependencies + 1),
        nullptr, ArraySizeModifier::Normal, /*IndexTypeQuals=*/0);
    CharUnits Sz = C.getTypeSizeInChars(KmpDependInfoArrayTy);
    Size = CGM.getSize(Sz.alignTo(Align));
    NumDepsVal = llvm::ConstantInt::get(CGF.IntPtrTy, NumDependencies);
  }
  // Need to allocate on the dynamic memory.
  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  // Use default allocator.
  llvm::Value *Allocator = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  llvm::Value *Args[] = {ThreadID, Size, Allocator};

  llvm::Value *Addr =
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_alloc),
                          Args, ".dep.arr.addr");
  llvm::Type *KmpDependInfoLlvmTy = CGF.ConvertTypeForMem(KmpDependInfoTy);
  Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      Addr, KmpDependInfoLlvmTy->getPointerTo());
  DependenciesArray = Address(Addr, KmpDependInfoLlvmTy, Align);
  // Write number of elements in the first element of array for depobj.
  LValue Base = CGF.MakeAddrLValue(DependenciesArray, KmpDependInfoTy);
  // deps[i].base_addr = NumDependencies;
  LValue BaseAddrLVal = CGF.EmitLValueForField(
      Base,
      *std::next(KmpDependInfoRD->field_begin(),
                 static_cast<unsigned int>(RTLDependInfoFields::BaseAddr)));
  CGF.EmitStoreOfScalar(NumDepsVal, BaseAddrLVal);
  llvm::PointerUnion<unsigned *, LValue *> Pos;
  unsigned Idx = 1;
  LValue PosLVal;
  if (Dependencies.IteratorExpr) {
    PosLVal = CGF.MakeAddrLValue(
        CGF.CreateMemTemp(C.getSizeType(), "iterator.counter.addr"),
        C.getSizeType());
    CGF.EmitStoreOfScalar(llvm::ConstantInt::get(CGF.SizeTy, Idx), PosLVal,
                          /*IsInit=*/true);
    Pos = &PosLVal;
  } else {
    Pos = &Idx;
  }
  emitDependData(CGF, KmpDependInfoTy, Pos, Dependencies, DependenciesArray);
  DependenciesArray = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      CGF.Builder.CreateConstGEP(DependenciesArray, 1), CGF.VoidPtrTy,
      CGF.Int8Ty);
  return DependenciesArray;
}

void CGOpenMPRuntime::emitDestroyClause(CodeGenFunction &CGF, LValue DepobjLVal,
                                        SourceLocation Loc) {
  ASTContext &C = CGM.getContext();
  QualType FlagsTy;
  getDependTypes(C, KmpDependInfoTy, FlagsTy);
  LValue Base = CGF.EmitLoadOfPointerLValue(DepobjLVal.getAddress(),
                                            C.VoidPtrTy.castAs<PointerType>());
  QualType KmpDependInfoPtrTy = C.getPointerType(KmpDependInfoTy);
  Address Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      Base.getAddress(), CGF.ConvertTypeForMem(KmpDependInfoPtrTy),
      CGF.ConvertTypeForMem(KmpDependInfoTy));
  llvm::Value *DepObjAddr = CGF.Builder.CreateGEP(
      Addr.getElementType(), Addr.emitRawPointer(CGF),
      llvm::ConstantInt::get(CGF.IntPtrTy, -1, /*isSigned=*/true));
  DepObjAddr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(DepObjAddr,
                                                               CGF.VoidPtrTy);
  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  // Use default allocator.
  llvm::Value *Allocator = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  llvm::Value *Args[] = {ThreadID, DepObjAddr, Allocator};

  // _kmpc_free(gtid, addr, nullptr);
  (void)CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                                CGM.getModule(), OMPRTL___kmpc_free),
                            Args);
}

void CGOpenMPRuntime::emitUpdateClause(CodeGenFunction &CGF, LValue DepobjLVal,
                                       OpenMPDependClauseKind NewDepKind,
                                       SourceLocation Loc) {
  ASTContext &C = CGM.getContext();
  QualType FlagsTy;
  getDependTypes(C, KmpDependInfoTy, FlagsTy);
  RecordDecl *KmpDependInfoRD =
      cast<RecordDecl>(KmpDependInfoTy->getAsTagDecl());
  llvm::Type *LLVMFlagsTy = CGF.ConvertTypeForMem(FlagsTy);
  llvm::Value *NumDeps;
  LValue Base;
  std::tie(NumDeps, Base) = getDepobjElements(CGF, DepobjLVal, Loc);

  Address Begin = Base.getAddress();
  // Cast from pointer to array type to pointer to single element.
  llvm::Value *End = CGF.Builder.CreateGEP(Begin.getElementType(),
                                           Begin.emitRawPointer(CGF), NumDeps);
  // The basic structure here is a while-do loop.
  llvm::BasicBlock *BodyBB = CGF.createBasicBlock("omp.body");
  llvm::BasicBlock *DoneBB = CGF.createBasicBlock("omp.done");
  llvm::BasicBlock *EntryBB = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(BodyBB);
  llvm::PHINode *ElementPHI =
      CGF.Builder.CreatePHI(Begin.getType(), 2, "omp.elementPast");
  ElementPHI->addIncoming(Begin.emitRawPointer(CGF), EntryBB);
  Begin = Begin.withPointer(ElementPHI, KnownNonNull);
  Base = CGF.MakeAddrLValue(Begin, KmpDependInfoTy, Base.getBaseInfo(),
                            Base.getTBAAInfo());
  // deps[i].flags = NewDepKind;
  RTLDependenceKindTy DepKind = translateDependencyKind(NewDepKind);
  LValue FlagsLVal = CGF.EmitLValueForField(
      Base, *std::next(KmpDependInfoRD->field_begin(),
                       static_cast<unsigned int>(RTLDependInfoFields::Flags)));
  CGF.EmitStoreOfScalar(
      llvm::ConstantInt::get(LLVMFlagsTy, static_cast<unsigned int>(DepKind)),
      FlagsLVal);

  // Shift the address forward by one element.
  llvm::Value *ElementNext =
      CGF.Builder.CreateConstGEP(Begin, /*Index=*/1, "omp.elementNext")
          .emitRawPointer(CGF);
  ElementPHI->addIncoming(ElementNext, CGF.Builder.GetInsertBlock());
  llvm::Value *IsEmpty =
      CGF.Builder.CreateICmpEQ(ElementNext, End, "omp.isempty");
  CGF.Builder.CreateCondBr(IsEmpty, DoneBB, BodyBB);
  // Done.
  CGF.EmitBlock(DoneBB, /*IsFinished=*/true);
}

void CGOpenMPRuntime::emitTaskCall(CodeGenFunction &CGF, SourceLocation Loc,
                                   const OMPExecutableDirective &D,
                                   llvm::Function *TaskFunction,
                                   QualType SharedsTy, Address Shareds,
                                   const Expr *IfCond,
                                   const OMPTaskDataTy &Data) {
  if (!CGF.HaveInsertPoint())
    return;

  TaskResultTy Result =
      emitTaskInit(CGF, Loc, D, TaskFunction, SharedsTy, Shareds, Data);
  llvm::Value *NewTask = Result.NewTask;
  llvm::Function *TaskEntry = Result.TaskEntry;
  llvm::Value *NewTaskNewTaskTTy = Result.NewTaskNewTaskTTy;
  LValue TDBase = Result.TDBase;
  const RecordDecl *KmpTaskTQTyRD = Result.KmpTaskTQTyRD;
  // Process list of dependences.
  Address DependenciesArray = Address::invalid();
  llvm::Value *NumOfElements;
  std::tie(NumOfElements, DependenciesArray) =
      emitDependClause(CGF, Data.Dependences, Loc);

  // NOTE: routine and part_id fields are initialized by __kmpc_omp_task_alloc()
  // libcall.
  // Build kmp_int32 __kmpc_omp_task_with_deps(ident_t *, kmp_int32 gtid,
  // kmp_task_t *new_task, kmp_int32 ndeps, kmp_depend_info_t *dep_list,
  // kmp_int32 ndeps_noalias, kmp_depend_info_t *noalias_dep_list) if dependence
  // list is not empty
  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  llvm::Value *UpLoc = emitUpdateLocation(CGF, Loc);
  llvm::Value *TaskArgs[] = { UpLoc, ThreadID, NewTask };
  llvm::Value *DepTaskArgs[7];
  if (!Data.Dependences.empty()) {
    DepTaskArgs[0] = UpLoc;
    DepTaskArgs[1] = ThreadID;
    DepTaskArgs[2] = NewTask;
    DepTaskArgs[3] = NumOfElements;
    DepTaskArgs[4] = DependenciesArray.emitRawPointer(CGF);
    DepTaskArgs[5] = CGF.Builder.getInt32(0);
    DepTaskArgs[6] = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  }
  auto &&ThenCodeGen = [this, &Data, TDBase, KmpTaskTQTyRD, &TaskArgs,
                        &DepTaskArgs](CodeGenFunction &CGF, PrePostActionTy &) {
    if (!Data.Tied) {
      auto PartIdFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTPartId);
      LValue PartIdLVal = CGF.EmitLValueForField(TDBase, *PartIdFI);
      CGF.EmitStoreOfScalar(CGF.Builder.getInt32(0), PartIdLVal);
    }
    if (!Data.Dependences.empty()) {
      CGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(
              CGM.getModule(), OMPRTL___kmpc_omp_task_with_deps),
          DepTaskArgs);
    } else {
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_omp_task),
                          TaskArgs);
    }
    // Check if parent region is untied and build return for untied task;
    if (auto *Region =
            dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
      Region->emitUntiedSwitch(CGF);
  };

  llvm::Value *DepWaitTaskArgs[7];
  if (!Data.Dependences.empty()) {
    DepWaitTaskArgs[0] = UpLoc;
    DepWaitTaskArgs[1] = ThreadID;
    DepWaitTaskArgs[2] = NumOfElements;
    DepWaitTaskArgs[3] = DependenciesArray.emitRawPointer(CGF);
    DepWaitTaskArgs[4] = CGF.Builder.getInt32(0);
    DepWaitTaskArgs[5] = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
    DepWaitTaskArgs[6] =
        llvm::ConstantInt::get(CGF.Int32Ty, Data.HasNowaitClause);
  }
  auto &M = CGM.getModule();
  auto &&ElseCodeGen = [this, &M, &TaskArgs, ThreadID, NewTaskNewTaskTTy,
                        TaskEntry, &Data, &DepWaitTaskArgs,
                        Loc](CodeGenFunction &CGF, PrePostActionTy &) {
    CodeGenFunction::RunCleanupsScope LocalScope(CGF);
    // Build void __kmpc_omp_wait_deps(ident_t *, kmp_int32 gtid,
    // kmp_int32 ndeps, kmp_depend_info_t *dep_list, kmp_int32
    // ndeps_noalias, kmp_depend_info_t *noalias_dep_list); if dependence info
    // is specified.
    if (!Data.Dependences.empty())
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              M, OMPRTL___kmpc_omp_taskwait_deps_51),
                          DepWaitTaskArgs);
    // Call proxy_task_entry(gtid, new_task);
    auto &&CodeGen = [TaskEntry, ThreadID, NewTaskNewTaskTTy,
                      Loc](CodeGenFunction &CGF, PrePostActionTy &Action) {
      Action.Enter(CGF);
      llvm::Value *OutlinedFnArgs[] = {ThreadID, NewTaskNewTaskTTy};
      CGF.CGM.getOpenMPRuntime().emitOutlinedFunctionCall(CGF, Loc, TaskEntry,
                                                          OutlinedFnArgs);
    };

    // Build void __kmpc_omp_task_begin_if0(ident_t *, kmp_int32 gtid,
    // kmp_task_t *new_task);
    // Build void __kmpc_omp_task_complete_if0(ident_t *, kmp_int32 gtid,
    // kmp_task_t *new_task);
    RegionCodeGenTy RCG(CodeGen);
    CommonActionTy Action(OMPBuilder.getOrCreateRuntimeFunction(
                              M, OMPRTL___kmpc_omp_task_begin_if0),
                          TaskArgs,
                          OMPBuilder.getOrCreateRuntimeFunction(
                              M, OMPRTL___kmpc_omp_task_complete_if0),
                          TaskArgs);
    RCG.setAction(Action);
    RCG(CGF);
  };

  if (IfCond) {
    emitIfClause(CGF, IfCond, ThenCodeGen, ElseCodeGen);
  } else {
    RegionCodeGenTy ThenRCG(ThenCodeGen);
    ThenRCG(CGF);
  }
}

void CGOpenMPRuntime::emitTaskLoopCall(CodeGenFunction &CGF, SourceLocation Loc,
                                       const OMPLoopDirective &D,
                                       llvm::Function *TaskFunction,
                                       QualType SharedsTy, Address Shareds,
                                       const Expr *IfCond,
                                       const OMPTaskDataTy &Data) {
  if (!CGF.HaveInsertPoint())
    return;
  TaskResultTy Result =
      emitTaskInit(CGF, Loc, D, TaskFunction, SharedsTy, Shareds, Data);
  // NOTE: routine and part_id fields are initialized by __kmpc_omp_task_alloc()
  // libcall.
  // Call to void __kmpc_taskloop(ident_t *loc, int gtid, kmp_task_t *task, int
  // if_val, kmp_uint64 *lb, kmp_uint64 *ub, kmp_int64 st, int nogroup, int
  // sched, kmp_uint64 grainsize, void *task_dup);
  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  llvm::Value *UpLoc = emitUpdateLocation(CGF, Loc);
  llvm::Value *IfVal;
  if (IfCond) {
    IfVal = CGF.Builder.CreateIntCast(CGF.EvaluateExprAsBool(IfCond), CGF.IntTy,
                                      /*isSigned=*/true);
  } else {
    IfVal = llvm::ConstantInt::getSigned(CGF.IntTy, /*V=*/1);
  }

  LValue LBLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTLowerBound));
  const auto *LBVar =
      cast<VarDecl>(cast<DeclRefExpr>(D.getLowerBoundVariable())->getDecl());
  CGF.EmitAnyExprToMem(LBVar->getInit(), LBLVal.getAddress(), LBLVal.getQuals(),
                       /*IsInitializer=*/true);
  LValue UBLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTUpperBound));
  const auto *UBVar =
      cast<VarDecl>(cast<DeclRefExpr>(D.getUpperBoundVariable())->getDecl());
  CGF.EmitAnyExprToMem(UBVar->getInit(), UBLVal.getAddress(), UBLVal.getQuals(),
                       /*IsInitializer=*/true);
  LValue StLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTStride));
  const auto *StVar =
      cast<VarDecl>(cast<DeclRefExpr>(D.getStrideVariable())->getDecl());
  CGF.EmitAnyExprToMem(StVar->getInit(), StLVal.getAddress(), StLVal.getQuals(),
                       /*IsInitializer=*/true);
  // Store reductions address.
  LValue RedLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTReductions));
  if (Data.Reductions) {
    CGF.EmitStoreOfScalar(Data.Reductions, RedLVal);
  } else {
    CGF.EmitNullInitialization(RedLVal.getAddress(),
                               CGF.getContext().VoidPtrTy);
  }
  enum { NoSchedule = 0, Grainsize = 1, NumTasks = 2 };
  llvm::Value *TaskArgs[] = {
      UpLoc,
      ThreadID,
      Result.NewTask,
      IfVal,
      LBLVal.getPointer(CGF),
      UBLVal.getPointer(CGF),
      CGF.EmitLoadOfScalar(StLVal, Loc),
      llvm::ConstantInt::getSigned(
          CGF.IntTy, 1), // Always 1 because taskgroup emitted by the compiler
      llvm::ConstantInt::getSigned(
          CGF.IntTy, Data.Schedule.getPointer()
                         ? Data.Schedule.getInt() ? NumTasks : Grainsize
                         : NoSchedule),
      Data.Schedule.getPointer()
          ? CGF.Builder.CreateIntCast(Data.Schedule.getPointer(), CGF.Int64Ty,
                                      /*isSigned=*/false)
          : llvm::ConstantInt::get(CGF.Int64Ty, /*V=*/0),
      Result.TaskDupFn ? CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                             Result.TaskDupFn, CGF.VoidPtrTy)
                       : llvm::ConstantPointerNull::get(CGF.VoidPtrTy)};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_taskloop),
                      TaskArgs);
}

/// Emit reduction operation for each element of array (required for
/// array sections) LHS op = RHS.
/// \param Type Type of array.
/// \param LHSVar Variable on the left side of the reduction operation
/// (references element of array in original variable).
/// \param RHSVar Variable on the right side of the reduction operation
/// (references element of array in original variable).
/// \param RedOpGen Generator of reduction operation with use of LHSVar and
/// RHSVar.
static void EmitOMPAggregateReduction(
    CodeGenFunction &CGF, QualType Type, const VarDecl *LHSVar,
    const VarDecl *RHSVar,
    const llvm::function_ref<void(CodeGenFunction &CGF, const Expr *,
                                  const Expr *, const Expr *)> &RedOpGen,
    const Expr *XExpr = nullptr, const Expr *EExpr = nullptr,
    const Expr *UpExpr = nullptr) {
  // Perform element-by-element initialization.
  QualType ElementTy;
  Address LHSAddr = CGF.GetAddrOfLocalVar(LHSVar);
  Address RHSAddr = CGF.GetAddrOfLocalVar(RHSVar);

  // Drill down to the base element type on both arrays.
  const ArrayType *ArrayTy = Type->getAsArrayTypeUnsafe();
  llvm::Value *NumElements = CGF.emitArrayLength(ArrayTy, ElementTy, LHSAddr);

  llvm::Value *RHSBegin = RHSAddr.emitRawPointer(CGF);
  llvm::Value *LHSBegin = LHSAddr.emitRawPointer(CGF);
  // Cast from pointer to array type to pointer to single element.
  llvm::Value *LHSEnd =
      CGF.Builder.CreateGEP(LHSAddr.getElementType(), LHSBegin, NumElements);
  // The basic structure here is a while-do loop.
  llvm::BasicBlock *BodyBB = CGF.createBasicBlock("omp.arraycpy.body");
  llvm::BasicBlock *DoneBB = CGF.createBasicBlock("omp.arraycpy.done");
  llvm::Value *IsEmpty =
      CGF.Builder.CreateICmpEQ(LHSBegin, LHSEnd, "omp.arraycpy.isempty");
  CGF.Builder.CreateCondBr(IsEmpty, DoneBB, BodyBB);

  // Enter the loop body, making that address the current address.
  llvm::BasicBlock *EntryBB = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(BodyBB);

  CharUnits ElementSize = CGF.getContext().getTypeSizeInChars(ElementTy);

  llvm::PHINode *RHSElementPHI = CGF.Builder.CreatePHI(
      RHSBegin->getType(), 2, "omp.arraycpy.srcElementPast");
  RHSElementPHI->addIncoming(RHSBegin, EntryBB);
  Address RHSElementCurrent(
      RHSElementPHI, RHSAddr.getElementType(),
      RHSAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  llvm::PHINode *LHSElementPHI = CGF.Builder.CreatePHI(
      LHSBegin->getType(), 2, "omp.arraycpy.destElementPast");
  LHSElementPHI->addIncoming(LHSBegin, EntryBB);
  Address LHSElementCurrent(
      LHSElementPHI, LHSAddr.getElementType(),
      LHSAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  // Emit copy.
  CodeGenFunction::OMPPrivateScope Scope(CGF);
  Scope.addPrivate(LHSVar, LHSElementCurrent);
  Scope.addPrivate(RHSVar, RHSElementCurrent);
  Scope.Privatize();
  RedOpGen(CGF, XExpr, EExpr, UpExpr);
  Scope.ForceCleanup();

  // Shift the address forward by one element.
  llvm::Value *LHSElementNext = CGF.Builder.CreateConstGEP1_32(
      LHSAddr.getElementType(), LHSElementPHI, /*Idx0=*/1,
      "omp.arraycpy.dest.element");
  llvm::Value *RHSElementNext = CGF.Builder.CreateConstGEP1_32(
      RHSAddr.getElementType(), RHSElementPHI, /*Idx0=*/1,
      "omp.arraycpy.src.element");
  // Check whether we've reached the end.
  llvm::Value *Done =
      CGF.Builder.CreateICmpEQ(LHSElementNext, LHSEnd, "omp.arraycpy.done");
  CGF.Builder.CreateCondBr(Done, DoneBB, BodyBB);
  LHSElementPHI->addIncoming(LHSElementNext, CGF.Builder.GetInsertBlock());
  RHSElementPHI->addIncoming(RHSElementNext, CGF.Builder.GetInsertBlock());

  // Done.
  CGF.EmitBlock(DoneBB, /*IsFinished=*/true);
}

/// Emit reduction combiner. If the combiner is a simple expression emit it as
/// is, otherwise consider it as combiner of UDR decl and emit it as a call of
/// UDR combiner function.
static void emitReductionCombiner(CodeGenFunction &CGF,
                                  const Expr *ReductionOp) {
  if (const auto *CE = dyn_cast<CallExpr>(ReductionOp))
    if (const auto *OVE = dyn_cast<OpaqueValueExpr>(CE->getCallee()))
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(OVE->getSourceExpr()->IgnoreImpCasts()))
        if (const auto *DRD =
                dyn_cast<OMPDeclareReductionDecl>(DRE->getDecl())) {
          std::pair<llvm::Function *, llvm::Function *> Reduction =
              CGF.CGM.getOpenMPRuntime().getUserDefinedReduction(DRD);
          RValue Func = RValue::get(Reduction.first);
          CodeGenFunction::OpaqueValueMapping Map(CGF, OVE, Func);
          CGF.EmitIgnoredExpr(ReductionOp);
          return;
        }
  CGF.EmitIgnoredExpr(ReductionOp);
}

llvm::Function *CGOpenMPRuntime::emitReductionFunction(
    StringRef ReducerName, SourceLocation Loc, llvm::Type *ArgsElemType,
    ArrayRef<const Expr *> Privates, ArrayRef<const Expr *> LHSExprs,
    ArrayRef<const Expr *> RHSExprs, ArrayRef<const Expr *> ReductionOps) {
  ASTContext &C = CGM.getContext();

  // void reduction_func(void *LHSArg, void *RHSArg);
  FunctionArgList Args;
  ImplicitParamDecl LHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamKind::Other);
  ImplicitParamDecl RHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamKind::Other);
  Args.push_back(&LHSArg);
  Args.push_back(&RHSArg);
  const auto &CGFI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  std::string Name = getReductionFuncName(ReducerName);
  auto *Fn = llvm::Function::Create(CGM.getTypes().GetFunctionType(CGFI),
                                    llvm::GlobalValue::InternalLinkage, Name,
                                    &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, CGFI);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, CGFI, Args, Loc, Loc);

  // Dst = (void*[n])(LHSArg);
  // Src = (void*[n])(RHSArg);
  Address LHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                  CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&LHSArg)),
                  ArgsElemType->getPointerTo()),
              ArgsElemType, CGF.getPointerAlign());
  Address RHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                  CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&RHSArg)),
                  ArgsElemType->getPointerTo()),
              ArgsElemType, CGF.getPointerAlign());

  //  ...
  //  *(Type<i>*)lhs[i] = RedOp<i>(*(Type<i>*)lhs[i], *(Type<i>*)rhs[i]);
  //  ...
  CodeGenFunction::OMPPrivateScope Scope(CGF);
  const auto *IPriv = Privates.begin();
  unsigned Idx = 0;
  for (unsigned I = 0, E = ReductionOps.size(); I < E; ++I, ++IPriv, ++Idx) {
    const auto *RHSVar =
        cast<VarDecl>(cast<DeclRefExpr>(RHSExprs[I])->getDecl());
    Scope.addPrivate(RHSVar, emitAddrOfVarFromArray(CGF, RHS, Idx, RHSVar));
    const auto *LHSVar =
        cast<VarDecl>(cast<DeclRefExpr>(LHSExprs[I])->getDecl());
    Scope.addPrivate(LHSVar, emitAddrOfVarFromArray(CGF, LHS, Idx, LHSVar));
    QualType PrivTy = (*IPriv)->getType();
    if (PrivTy->isVariablyModifiedType()) {
      // Get array size and emit VLA type.
      ++Idx;
      Address Elem = CGF.Builder.CreateConstArrayGEP(LHS, Idx);
      llvm::Value *Ptr = CGF.Builder.CreateLoad(Elem);
      const VariableArrayType *VLA =
          CGF.getContext().getAsVariableArrayType(PrivTy);
      const auto *OVE = cast<OpaqueValueExpr>(VLA->getSizeExpr());
      CodeGenFunction::OpaqueValueMapping OpaqueMap(
          CGF, OVE, RValue::get(CGF.Builder.CreatePtrToInt(Ptr, CGF.SizeTy)));
      CGF.EmitVariablyModifiedType(PrivTy);
    }
  }
  Scope.Privatize();
  IPriv = Privates.begin();
  const auto *ILHS = LHSExprs.begin();
  const auto *IRHS = RHSExprs.begin();
  for (const Expr *E : ReductionOps) {
    if ((*IPriv)->getType()->isArrayType()) {
      // Emit reduction for array section.
      const auto *LHSVar = cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
      const auto *RHSVar = cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
      EmitOMPAggregateReduction(
          CGF, (*IPriv)->getType(), LHSVar, RHSVar,
          [=](CodeGenFunction &CGF, const Expr *, const Expr *, const Expr *) {
            emitReductionCombiner(CGF, E);
          });
    } else {
      // Emit reduction for array subscript or single variable.
      emitReductionCombiner(CGF, E);
    }
    ++IPriv;
    ++ILHS;
    ++IRHS;
  }
  Scope.ForceCleanup();
  CGF.FinishFunction();
  return Fn;
}

void CGOpenMPRuntime::emitSingleReductionCombiner(CodeGenFunction &CGF,
                                                  const Expr *ReductionOp,
                                                  const Expr *PrivateRef,
                                                  const DeclRefExpr *LHS,
                                                  const DeclRefExpr *RHS) {
  if (PrivateRef->getType()->isArrayType()) {
    // Emit reduction for array section.
    const auto *LHSVar = cast<VarDecl>(LHS->getDecl());
    const auto *RHSVar = cast<VarDecl>(RHS->getDecl());
    EmitOMPAggregateReduction(
        CGF, PrivateRef->getType(), LHSVar, RHSVar,
        [=](CodeGenFunction &CGF, const Expr *, const Expr *, const Expr *) {
          emitReductionCombiner(CGF, ReductionOp);
        });
  } else {
    // Emit reduction for array subscript or single variable.
    emitReductionCombiner(CGF, ReductionOp);
  }
}

void CGOpenMPRuntime::emitReduction(CodeGenFunction &CGF, SourceLocation Loc,
                                    ArrayRef<const Expr *> Privates,
                                    ArrayRef<const Expr *> LHSExprs,
                                    ArrayRef<const Expr *> RHSExprs,
                                    ArrayRef<const Expr *> ReductionOps,
                                    ReductionOptionsTy Options) {
  if (!CGF.HaveInsertPoint())
    return;

  bool WithNowait = Options.WithNowait;
  bool SimpleReduction = Options.SimpleReduction;

  // Next code should be emitted for reduction:
  //
  // static kmp_critical_name lock = { 0 };
  //
  // void reduce_func(void *lhs[<n>], void *rhs[<n>]) {
  //  *(Type0*)lhs[0] = ReductionOperation0(*(Type0*)lhs[0], *(Type0*)rhs[0]);
  //  ...
  //  *(Type<n>-1*)lhs[<n>-1] = ReductionOperation<n>-1(*(Type<n>-1*)lhs[<n>-1],
  //  *(Type<n>-1*)rhs[<n>-1]);
  // }
  //
  // ...
  // void *RedList[<n>] = {&<RHSExprs>[0], ..., &<RHSExprs>[<n>-1]};
  // switch (__kmpc_reduce{_nowait}(<loc>, <gtid>, <n>, sizeof(RedList),
  // RedList, reduce_func, &<lock>)) {
  // case 1:
  //  ...
  //  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  //  ...
  // __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  // break;
  // case 2:
  //  ...
  //  Atomic(<LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]));
  //  ...
  // [__kmpc_end_reduce(<loc>, <gtid>, &<lock>);]
  // break;
  // default:;
  // }
  //
  // if SimpleReduction is true, only the next code is generated:
  //  ...
  //  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  //  ...

  ASTContext &C = CGM.getContext();

  if (SimpleReduction) {
    CodeGenFunction::RunCleanupsScope Scope(CGF);
    const auto *IPriv = Privates.begin();
    const auto *ILHS = LHSExprs.begin();
    const auto *IRHS = RHSExprs.begin();
    for (const Expr *E : ReductionOps) {
      emitSingleReductionCombiner(CGF, E, *IPriv, cast<DeclRefExpr>(*ILHS),
                                  cast<DeclRefExpr>(*IRHS));
      ++IPriv;
      ++ILHS;
      ++IRHS;
    }
    return;
  }

  // 1. Build a list of reduction variables.
  // void *RedList[<n>] = {<ReductionVars>[0], ..., <ReductionVars>[<n>-1]};
  auto Size = RHSExprs.size();
  for (const Expr *E : Privates) {
    if (E->getType()->isVariablyModifiedType())
      // Reserve place for array size.
      ++Size;
  }
  llvm::APInt ArraySize(/*unsigned int numBits=*/32, Size);
  QualType ReductionArrayTy = C.getConstantArrayType(
      C.VoidPtrTy, ArraySize, nullptr, ArraySizeModifier::Normal,
      /*IndexTypeQuals=*/0);
  RawAddress ReductionList =
      CGF.CreateMemTemp(ReductionArrayTy, ".omp.reduction.red_list");
  const auto *IPriv = Privates.begin();
  unsigned Idx = 0;
  for (unsigned I = 0, E = RHSExprs.size(); I < E; ++I, ++IPriv, ++Idx) {
    Address Elem = CGF.Builder.CreateConstArrayGEP(ReductionList, Idx);
    CGF.Builder.CreateStore(
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            CGF.EmitLValue(RHSExprs[I]).getPointer(CGF), CGF.VoidPtrTy),
        Elem);
    if ((*IPriv)->getType()->isVariablyModifiedType()) {
      // Store array size.
      ++Idx;
      Elem = CGF.Builder.CreateConstArrayGEP(ReductionList, Idx);
      llvm::Value *Size = CGF.Builder.CreateIntCast(
          CGF.getVLASize(
                 CGF.getContext().getAsVariableArrayType((*IPriv)->getType()))
              .NumElts,
          CGF.SizeTy, /*isSigned=*/false);
      CGF.Builder.CreateStore(CGF.Builder.CreateIntToPtr(Size, CGF.VoidPtrTy),
                              Elem);
    }
  }

  // 2. Emit reduce_func().
  llvm::Function *ReductionFn = emitReductionFunction(
      CGF.CurFn->getName(), Loc, CGF.ConvertTypeForMem(ReductionArrayTy),
      Privates, LHSExprs, RHSExprs, ReductionOps);

  // 3. Create static kmp_critical_name lock = { 0 };
  std::string Name = getName({"reduction"});
  llvm::Value *Lock = getCriticalRegionLock(Name);

  // 4. Build res = __kmpc_reduce{_nowait}(<loc>, <gtid>, <n>, sizeof(RedList),
  // RedList, reduce_func, &<lock>);
  llvm::Value *IdentTLoc = emitUpdateLocation(CGF, Loc, OMP_ATOMIC_REDUCE);
  llvm::Value *ThreadId = getThreadID(CGF, Loc);
  llvm::Value *ReductionArrayTySize = CGF.getTypeSize(ReductionArrayTy);
  llvm::Value *RL = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReductionList.getPointer(), CGF.VoidPtrTy);
  llvm::Value *Args[] = {
      IdentTLoc,                             // ident_t *<loc>
      ThreadId,                              // i32 <gtid>
      CGF.Builder.getInt32(RHSExprs.size()), // i32 <n>
      ReductionArrayTySize,                  // size_type sizeof(RedList)
      RL,                                    // void *RedList
      ReductionFn, // void (*) (void *, void *) <reduce_func>
      Lock         // kmp_critical_name *&<lock>
  };
  llvm::Value *Res = CGF.EmitRuntimeCall(
      OMPBuilder.getOrCreateRuntimeFunction(
          CGM.getModule(),
          WithNowait ? OMPRTL___kmpc_reduce_nowait : OMPRTL___kmpc_reduce),
      Args);

  // 5. Build switch(res)
  llvm::BasicBlock *DefaultBB = CGF.createBasicBlock(".omp.reduction.default");
  llvm::SwitchInst *SwInst =
      CGF.Builder.CreateSwitch(Res, DefaultBB, /*NumCases=*/2);

  // 6. Build case 1:
  //  ...
  //  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  //  ...
  // __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  // break;
  llvm::BasicBlock *Case1BB = CGF.createBasicBlock(".omp.reduction.case1");
  SwInst->addCase(CGF.Builder.getInt32(1), Case1BB);
  CGF.EmitBlock(Case1BB);

  // Add emission of __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  llvm::Value *EndArgs[] = {
      IdentTLoc, // ident_t *<loc>
      ThreadId,  // i32 <gtid>
      Lock       // kmp_critical_name *&<lock>
  };
  auto &&CodeGen = [Privates, LHSExprs, RHSExprs, ReductionOps](
                       CodeGenFunction &CGF, PrePostActionTy &Action) {
    CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
    const auto *IPriv = Privates.begin();
    const auto *ILHS = LHSExprs.begin();
    const auto *IRHS = RHSExprs.begin();
    for (const Expr *E : ReductionOps) {
      RT.emitSingleReductionCombiner(CGF, E, *IPriv, cast<DeclRefExpr>(*ILHS),
                                     cast<DeclRefExpr>(*IRHS));
      ++IPriv;
      ++ILHS;
      ++IRHS;
    }
  };
  RegionCodeGenTy RCG(CodeGen);
  CommonActionTy Action(
      nullptr, std::nullopt,
      OMPBuilder.getOrCreateRuntimeFunction(
          CGM.getModule(), WithNowait ? OMPRTL___kmpc_end_reduce_nowait
                                      : OMPRTL___kmpc_end_reduce),
      EndArgs);
  RCG.setAction(Action);
  RCG(CGF);

  CGF.EmitBranch(DefaultBB);

  // 7. Build case 2:
  //  ...
  //  Atomic(<LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]));
  //  ...
  // break;
  llvm::BasicBlock *Case2BB = CGF.createBasicBlock(".omp.reduction.case2");
  SwInst->addCase(CGF.Builder.getInt32(2), Case2BB);
  CGF.EmitBlock(Case2BB);

  auto &&AtomicCodeGen = [Loc, Privates, LHSExprs, RHSExprs, ReductionOps](
                             CodeGenFunction &CGF, PrePostActionTy &Action) {
    const auto *ILHS = LHSExprs.begin();
    const auto *IRHS = RHSExprs.begin();
    const auto *IPriv = Privates.begin();
    for (const Expr *E : ReductionOps) {
      const Expr *XExpr = nullptr;
      const Expr *EExpr = nullptr;
      const Expr *UpExpr = nullptr;
      BinaryOperatorKind BO = BO_Comma;
      if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->getOpcode() == BO_Assign) {
          XExpr = BO->getLHS();
          UpExpr = BO->getRHS();
        }
      }
      // Try to emit update expression as a simple atomic.
      const Expr *RHSExpr = UpExpr;
      if (RHSExpr) {
        // Analyze RHS part of the whole expression.
        if (const auto *ACO = dyn_cast<AbstractConditionalOperator>(
                RHSExpr->IgnoreParenImpCasts())) {
          // If this is a conditional operator, analyze its condition for
          // min/max reduction operator.
          RHSExpr = ACO->getCond();
        }
        if (const auto *BORHS =
                dyn_cast<BinaryOperator>(RHSExpr->IgnoreParenImpCasts())) {
          EExpr = BORHS->getRHS();
          BO = BORHS->getOpcode();
        }
      }
      if (XExpr) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
        auto &&AtomicRedGen = [BO, VD,
                               Loc](CodeGenFunction &CGF, const Expr *XExpr,
                                    const Expr *EExpr, const Expr *UpExpr) {
          LValue X = CGF.EmitLValue(XExpr);
          RValue E;
          if (EExpr)
            E = CGF.EmitAnyExpr(EExpr);
          CGF.EmitOMPAtomicSimpleUpdateExpr(
              X, E, BO, /*IsXLHSInRHSPart=*/true,
              llvm::AtomicOrdering::Monotonic, Loc,
              [&CGF, UpExpr, VD, Loc](RValue XRValue) {
                CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
                Address LHSTemp = CGF.CreateMemTemp(VD->getType());
                CGF.emitOMPSimpleStore(
                    CGF.MakeAddrLValue(LHSTemp, VD->getType()), XRValue,
                    VD->getType().getNonReferenceType(), Loc);
                PrivateScope.addPrivate(VD, LHSTemp);
                (void)PrivateScope.Privatize();
                return CGF.EmitAnyExpr(UpExpr);
              });
        };
        if ((*IPriv)->getType()->isArrayType()) {
          // Emit atomic reduction for array section.
          const auto *RHSVar =
              cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
          EmitOMPAggregateReduction(CGF, (*IPriv)->getType(), VD, RHSVar,
                                    AtomicRedGen, XExpr, EExpr, UpExpr);
        } else {
          // Emit atomic reduction for array subscript or single variable.
          AtomicRedGen(CGF, XExpr, EExpr, UpExpr);
        }
      } else {
        // Emit as a critical region.
        auto &&CritRedGen = [E, Loc](CodeGenFunction &CGF, const Expr *,
                                           const Expr *, const Expr *) {
          CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
          std::string Name = RT.getName({"atomic_reduction"});
          RT.emitCriticalRegion(
              CGF, Name,
              [=](CodeGenFunction &CGF, PrePostActionTy &Action) {
                Action.Enter(CGF);
                emitReductionCombiner(CGF, E);
              },
              Loc);
        };
        if ((*IPriv)->getType()->isArrayType()) {
          const auto *LHSVar =
              cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
          const auto *RHSVar =
              cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
          EmitOMPAggregateReduction(CGF, (*IPriv)->getType(), LHSVar, RHSVar,
                                    CritRedGen);
        } else {
          CritRedGen(CGF, nullptr, nullptr, nullptr);
        }
      }
      ++ILHS;
      ++IRHS;
      ++IPriv;
    }
  };
  RegionCodeGenTy AtomicRCG(AtomicCodeGen);
  if (!WithNowait) {
    // Add emission of __kmpc_end_reduce(<loc>, <gtid>, &<lock>);
    llvm::Value *EndArgs[] = {
        IdentTLoc, // ident_t *<loc>
        ThreadId,  // i32 <gtid>
        Lock       // kmp_critical_name *&<lock>
    };
    CommonActionTy Action(nullptr, std::nullopt,
                          OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_end_reduce),
                          EndArgs);
    AtomicRCG.setAction(Action);
    AtomicRCG(CGF);
  } else {
    AtomicRCG(CGF);
  }

  CGF.EmitBranch(DefaultBB);
  CGF.EmitBlock(DefaultBB, /*IsFinished=*/true);
}

/// Generates unique name for artificial threadprivate variables.
/// Format is: <Prefix> "." <Decl_mangled_name> "_" "<Decl_start_loc_raw_enc>"
static std::string generateUniqueName(CodeGenModule &CGM, StringRef Prefix,
                                      const Expr *Ref) {
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  const clang::DeclRefExpr *DE;
  const VarDecl *D = ::getBaseDecl(Ref, DE);
  if (!D)
    D = cast<VarDecl>(cast<DeclRefExpr>(Ref)->getDecl());
  D = D->getCanonicalDecl();
  std::string Name = CGM.getOpenMPRuntime().getName(
      {D->isLocalVarDeclOrParm() ? D->getName() : CGM.getMangledName(D)});
  Out << Prefix << Name << "_"
      << D->getCanonicalDecl()->getBeginLoc().getRawEncoding();
  return std::string(Out.str());
}

/// Emits reduction initializer function:
/// \code
/// void @.red_init(void* %arg, void* %orig) {
/// %0 = bitcast void* %arg to <type>*
/// store <type> <init>, <type>* %0
/// ret void
/// }
/// \endcode
static llvm::Value *emitReduceInitFunction(CodeGenModule &CGM,
                                           SourceLocation Loc,
                                           ReductionCodeGen &RCG, unsigned N) {
  ASTContext &C = CGM.getContext();
  QualType VoidPtrTy = C.VoidPtrTy;
  VoidPtrTy.addRestrict();
  FunctionArgList Args;
  ImplicitParamDecl Param(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, VoidPtrTy,
                          ImplicitParamKind::Other);
  ImplicitParamDecl ParamOrig(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, VoidPtrTy,
                              ImplicitParamKind::Other);
  Args.emplace_back(&Param);
  Args.emplace_back(&ParamOrig);
  const auto &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"red_init", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, Loc, Loc);
  QualType PrivateType = RCG.getPrivateType(N);
  Address PrivateAddr = CGF.EmitLoadOfPointer(
      CGF.GetAddrOfLocalVar(&Param).withElementType(
          CGF.ConvertTypeForMem(PrivateType)->getPointerTo()),
      C.getPointerType(PrivateType)->castAs<PointerType>());
  llvm::Value *Size = nullptr;
  // If the size of the reduction item is non-constant, load it from global
  // threadprivate variable.
  if (RCG.getSizes(N).second) {
    Address SizeAddr = CGM.getOpenMPRuntime().getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    Size = CGF.EmitLoadOfScalar(SizeAddr, /*Volatile=*/false,
                                CGM.getContext().getSizeType(), Loc);
  }
  RCG.emitAggregateType(CGF, N, Size);
  Address OrigAddr = Address::invalid();
  // If initializer uses initializer from declare reduction construct, emit a
  // pointer to the address of the original reduction item (reuired by reduction
  // initializer)
  if (RCG.usesReductionInitializer(N)) {
    Address SharedAddr = CGF.GetAddrOfLocalVar(&ParamOrig);
    OrigAddr = CGF.EmitLoadOfPointer(
        SharedAddr,
        CGM.getContext().VoidPtrTy.castAs<PointerType>()->getTypePtr());
  }
  // Emit the initializer:
  // %0 = bitcast void* %arg to <type>*
  // store <type> <init>, <type>* %0
  RCG.emitInitialization(CGF, N, PrivateAddr, OrigAddr,
                         [](CodeGenFunction &) { return false; });
  CGF.FinishFunction();
  return Fn;
}

/// Emits reduction combiner function:
/// \code
/// void @.red_comb(void* %arg0, void* %arg1) {
/// %lhs = bitcast void* %arg0 to <type>*
/// %rhs = bitcast void* %arg1 to <type>*
/// %2 = <ReductionOp>(<type>* %lhs, <type>* %rhs)
/// store <type> %2, <type>* %lhs
/// ret void
/// }
/// \endcode
static llvm::Value *emitReduceCombFunction(CodeGenModule &CGM,
                                           SourceLocation Loc,
                                           ReductionCodeGen &RCG, unsigned N,
                                           const Expr *ReductionOp,
                                           const Expr *LHS, const Expr *RHS,
                                           const Expr *PrivateRef) {
  ASTContext &C = CGM.getContext();
  const auto *LHSVD = cast<VarDecl>(cast<DeclRefExpr>(LHS)->getDecl());
  const auto *RHSVD = cast<VarDecl>(cast<DeclRefExpr>(RHS)->getDecl());
  FunctionArgList Args;
  ImplicitParamDecl ParamInOut(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                               C.VoidPtrTy, ImplicitParamKind::Other);
  ImplicitParamDecl ParamIn(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                            ImplicitParamKind::Other);
  Args.emplace_back(&ParamInOut);
  Args.emplace_back(&ParamIn);
  const auto &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"red_comb", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, Loc, Loc);
  llvm::Value *Size = nullptr;
  // If the size of the reduction item is non-constant, load it from global
  // threadprivate variable.
  if (RCG.getSizes(N).second) {
    Address SizeAddr = CGM.getOpenMPRuntime().getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    Size = CGF.EmitLoadOfScalar(SizeAddr, /*Volatile=*/false,
                                CGM.getContext().getSizeType(), Loc);
  }
  RCG.emitAggregateType(CGF, N, Size);
  // Remap lhs and rhs variables to the addresses of the function arguments.
  // %lhs = bitcast void* %arg0 to <type>*
  // %rhs = bitcast void* %arg1 to <type>*
  CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
  PrivateScope.addPrivate(
      LHSVD,
      // Pull out the pointer to the variable.
      CGF.EmitLoadOfPointer(
          CGF.GetAddrOfLocalVar(&ParamInOut)
              .withElementType(
                  CGF.ConvertTypeForMem(LHSVD->getType())->getPointerTo()),
          C.getPointerType(LHSVD->getType())->castAs<PointerType>()));
  PrivateScope.addPrivate(
      RHSVD,
      // Pull out the pointer to the variable.
      CGF.EmitLoadOfPointer(
          CGF.GetAddrOfLocalVar(&ParamIn).withElementType(
              CGF.ConvertTypeForMem(RHSVD->getType())->getPointerTo()),
          C.getPointerType(RHSVD->getType())->castAs<PointerType>()));
  PrivateScope.Privatize();
  // Emit the combiner body:
  // %2 = <ReductionOp>(<type> *%lhs, <type> *%rhs)
  // store <type> %2, <type>* %lhs
  CGM.getOpenMPRuntime().emitSingleReductionCombiner(
      CGF, ReductionOp, PrivateRef, cast<DeclRefExpr>(LHS),
      cast<DeclRefExpr>(RHS));
  CGF.FinishFunction();
  return Fn;
}

/// Emits reduction finalizer function:
/// \code
/// void @.red_fini(void* %arg) {
/// %0 = bitcast void* %arg to <type>*
/// <destroy>(<type>* %0)
/// ret void
/// }
/// \endcode
static llvm::Value *emitReduceFiniFunction(CodeGenModule &CGM,
                                           SourceLocation Loc,
                                           ReductionCodeGen &RCG, unsigned N) {
  if (!RCG.needCleanups(N))
    return nullptr;
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl Param(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                          ImplicitParamKind::Other);
  Args.emplace_back(&Param);
  const auto &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"red_fini", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, Loc, Loc);
  Address PrivateAddr = CGF.EmitLoadOfPointer(
      CGF.GetAddrOfLocalVar(&Param), C.VoidPtrTy.castAs<PointerType>());
  llvm::Value *Size = nullptr;
  // If the size of the reduction item is non-constant, load it from global
  // threadprivate variable.
  if (RCG.getSizes(N).second) {
    Address SizeAddr = CGM.getOpenMPRuntime().getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    Size = CGF.EmitLoadOfScalar(SizeAddr, /*Volatile=*/false,
                                CGM.getContext().getSizeType(), Loc);
  }
  RCG.emitAggregateType(CGF, N, Size);
  // Emit the finalizer body:
  // <destroy>(<type>* %0)
  RCG.emitCleanups(CGF, N, PrivateAddr);
  CGF.FinishFunction(Loc);
  return Fn;
}

llvm::Value *CGOpenMPRuntime::emitTaskReductionInit(
    CodeGenFunction &CGF, SourceLocation Loc, ArrayRef<const Expr *> LHSExprs,
    ArrayRef<const Expr *> RHSExprs, const OMPTaskDataTy &Data) {
  if (!CGF.HaveInsertPoint() || Data.ReductionVars.empty())
    return nullptr;

  // Build typedef struct:
  // kmp_taskred_input {
  //   void *reduce_shar; // shared reduction item
  //   void *reduce_orig; // original reduction item used for initialization
  //   size_t reduce_size; // size of data item
  //   void *reduce_init; // data initialization routine
  //   void *reduce_fini; // data finalization routine
  //   void *reduce_comb; // data combiner routine
  //   kmp_task_red_flags_t flags; // flags for additional info from compiler
  // } kmp_taskred_input_t;
  ASTContext &C = CGM.getContext();
  RecordDecl *RD = C.buildImplicitRecord("kmp_taskred_input_t");
  RD->startDefinition();
  const FieldDecl *SharedFD = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *OrigFD = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *SizeFD = addFieldToRecordDecl(C, RD, C.getSizeType());
  const FieldDecl *InitFD  = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *FiniFD = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *CombFD = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *FlagsFD = addFieldToRecordDecl(
      C, RD, C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/false));
  RD->completeDefinition();
  QualType RDType = C.getRecordType(RD);
  unsigned Size = Data.ReductionVars.size();
  llvm::APInt ArraySize(/*numBits=*/64, Size);
  QualType ArrayRDType =
      C.getConstantArrayType(RDType, ArraySize, nullptr,
                             ArraySizeModifier::Normal, /*IndexTypeQuals=*/0);
  // kmp_task_red_input_t .rd_input.[Size];
  RawAddress TaskRedInput = CGF.CreateMemTemp(ArrayRDType, ".rd_input.");
  ReductionCodeGen RCG(Data.ReductionVars, Data.ReductionOrigs,
                       Data.ReductionCopies, Data.ReductionOps);
  for (unsigned Cnt = 0; Cnt < Size; ++Cnt) {
    // kmp_task_red_input_t &ElemLVal = .rd_input.[Cnt];
    llvm::Value *Idxs[] = {llvm::ConstantInt::get(CGM.SizeTy, /*V=*/0),
                           llvm::ConstantInt::get(CGM.SizeTy, Cnt)};
    llvm::Value *GEP = CGF.EmitCheckedInBoundsGEP(
        TaskRedInput.getElementType(), TaskRedInput.getPointer(), Idxs,
        /*SignedIndices=*/false, /*IsSubtraction=*/false, Loc,
        ".rd_input.gep.");
    LValue ElemLVal = CGF.MakeNaturalAlignRawAddrLValue(GEP, RDType);
    // ElemLVal.reduce_shar = &Shareds[Cnt];
    LValue SharedLVal = CGF.EmitLValueForField(ElemLVal, SharedFD);
    RCG.emitSharedOrigLValue(CGF, Cnt);
    llvm::Value *Shared = RCG.getSharedLValue(Cnt).getPointer(CGF);
    CGF.EmitStoreOfScalar(Shared, SharedLVal);
    // ElemLVal.reduce_orig = &Origs[Cnt];
    LValue OrigLVal = CGF.EmitLValueForField(ElemLVal, OrigFD);
    llvm::Value *Orig = RCG.getOrigLValue(Cnt).getPointer(CGF);
    CGF.EmitStoreOfScalar(Orig, OrigLVal);
    RCG.emitAggregateType(CGF, Cnt);
    llvm::Value *SizeValInChars;
    llvm::Value *SizeVal;
    std::tie(SizeValInChars, SizeVal) = RCG.getSizes(Cnt);
    // We use delayed creation/initialization for VLAs and array sections. It is
    // required because runtime does not provide the way to pass the sizes of
    // VLAs/array sections to initializer/combiner/finalizer functions. Instead
    // threadprivate global variables are used to store these values and use
    // them in the functions.
    bool DelayedCreation = !!SizeVal;
    SizeValInChars = CGF.Builder.CreateIntCast(SizeValInChars, CGM.SizeTy,
                                               /*isSigned=*/false);
    LValue SizeLVal = CGF.EmitLValueForField(ElemLVal, SizeFD);
    CGF.EmitStoreOfScalar(SizeValInChars, SizeLVal);
    // ElemLVal.reduce_init = init;
    LValue InitLVal = CGF.EmitLValueForField(ElemLVal, InitFD);
    llvm::Value *InitAddr = emitReduceInitFunction(CGM, Loc, RCG, Cnt);
    CGF.EmitStoreOfScalar(InitAddr, InitLVal);
    // ElemLVal.reduce_fini = fini;
    LValue FiniLVal = CGF.EmitLValueForField(ElemLVal, FiniFD);
    llvm::Value *Fini = emitReduceFiniFunction(CGM, Loc, RCG, Cnt);
    llvm::Value *FiniAddr =
        Fini ? Fini : llvm::ConstantPointerNull::get(CGM.VoidPtrTy);
    CGF.EmitStoreOfScalar(FiniAddr, FiniLVal);
    // ElemLVal.reduce_comb = comb;
    LValue CombLVal = CGF.EmitLValueForField(ElemLVal, CombFD);
    llvm::Value *CombAddr = emitReduceCombFunction(
        CGM, Loc, RCG, Cnt, Data.ReductionOps[Cnt], LHSExprs[Cnt],
        RHSExprs[Cnt], Data.ReductionCopies[Cnt]);
    CGF.EmitStoreOfScalar(CombAddr, CombLVal);
    // ElemLVal.flags = 0;
    LValue FlagsLVal = CGF.EmitLValueForField(ElemLVal, FlagsFD);
    if (DelayedCreation) {
      CGF.EmitStoreOfScalar(
          llvm::ConstantInt::get(CGM.Int32Ty, /*V=*/1, /*isSigned=*/true),
          FlagsLVal);
    } else
      CGF.EmitNullInitialization(FlagsLVal.getAddress(), FlagsLVal.getType());
  }
  if (Data.IsReductionWithTaskMod) {
    // Build call void *__kmpc_taskred_modifier_init(ident_t *loc, int gtid, int
    // is_ws, int num, void *data);
    llvm::Value *IdentTLoc = emitUpdateLocation(CGF, Loc);
    llvm::Value *GTid = CGF.Builder.CreateIntCast(getThreadID(CGF, Loc),
                                                  CGM.IntTy, /*isSigned=*/true);
    llvm::Value *Args[] = {
        IdentTLoc, GTid,
        llvm::ConstantInt::get(CGM.IntTy, Data.IsWorksharingReduction ? 1 : 0,
                               /*isSigned=*/true),
        llvm::ConstantInt::get(CGM.IntTy, Size, /*isSigned=*/true),
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            TaskRedInput.getPointer(), CGM.VoidPtrTy)};
    return CGF.EmitRuntimeCall(
        OMPBuilder.getOrCreateRuntimeFunction(
            CGM.getModule(), OMPRTL___kmpc_taskred_modifier_init),
        Args);
  }
  // Build call void *__kmpc_taskred_init(int gtid, int num_data, void *data);
  llvm::Value *Args[] = {
      CGF.Builder.CreateIntCast(getThreadID(CGF, Loc), CGM.IntTy,
                                /*isSigned=*/true),
      llvm::ConstantInt::get(CGM.IntTy, Size, /*isSigned=*/true),
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(TaskRedInput.getPointer(),
                                                      CGM.VoidPtrTy)};
  return CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                                 CGM.getModule(), OMPRTL___kmpc_taskred_init),
                             Args);
}

void CGOpenMPRuntime::emitTaskReductionFini(CodeGenFunction &CGF,
                                            SourceLocation Loc,
                                            bool IsWorksharingReduction) {
  // Build call void *__kmpc_taskred_modifier_init(ident_t *loc, int gtid, int
  // is_ws, int num, void *data);
  llvm::Value *IdentTLoc = emitUpdateLocation(CGF, Loc);
  llvm::Value *GTid = CGF.Builder.CreateIntCast(getThreadID(CGF, Loc),
                                                CGM.IntTy, /*isSigned=*/true);
  llvm::Value *Args[] = {IdentTLoc, GTid,
                         llvm::ConstantInt::get(CGM.IntTy,
                                                IsWorksharingReduction ? 1 : 0,
                                                /*isSigned=*/true)};
  (void)CGF.EmitRuntimeCall(
      OMPBuilder.getOrCreateRuntimeFunction(
          CGM.getModule(), OMPRTL___kmpc_task_reduction_modifier_fini),
      Args);
}

void CGOpenMPRuntime::emitTaskReductionFixups(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              ReductionCodeGen &RCG,
                                              unsigned N) {
  auto Sizes = RCG.getSizes(N);
  // Emit threadprivate global variable if the type is non-constant
  // (Sizes.second = nullptr).
  if (Sizes.second) {
    llvm::Value *SizeVal = CGF.Builder.CreateIntCast(Sizes.second, CGM.SizeTy,
                                                     /*isSigned=*/false);
    Address SizeAddr = getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    CGF.Builder.CreateStore(SizeVal, SizeAddr, /*IsVolatile=*/false);
  }
}

Address CGOpenMPRuntime::getTaskReductionItem(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              llvm::Value *ReductionsPtr,
                                              LValue SharedLVal) {
  // Build call void *__kmpc_task_reduction_get_th_data(int gtid, void *tg, void
  // *d);
  llvm::Value *Args[] = {CGF.Builder.CreateIntCast(getThreadID(CGF, Loc),
                                                   CGM.IntTy,
                                                   /*isSigned=*/true),
                         ReductionsPtr,
                         CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                             SharedLVal.getPointer(CGF), CGM.VoidPtrTy)};
  return Address(
      CGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(
              CGM.getModule(), OMPRTL___kmpc_task_reduction_get_th_data),
          Args),
      CGF.Int8Ty, SharedLVal.getAlignment());
}

void CGOpenMPRuntime::emitTaskwaitCall(CodeGenFunction &CGF, SourceLocation Loc,
                                       const OMPTaskDataTy &Data) {
  if (!CGF.HaveInsertPoint())
    return;

  if (CGF.CGM.getLangOpts().OpenMPIRBuilder && Data.Dependences.empty()) {
    // TODO: Need to support taskwait with dependences in the OpenMPIRBuilder.
    OMPBuilder.createTaskwait(CGF.Builder);
  } else {
    llvm::Value *ThreadID = getThreadID(CGF, Loc);
    llvm::Value *UpLoc = emitUpdateLocation(CGF, Loc);
    auto &M = CGM.getModule();
    Address DependenciesArray = Address::invalid();
    llvm::Value *NumOfElements;
    std::tie(NumOfElements, DependenciesArray) =
        emitDependClause(CGF, Data.Dependences, Loc);
    if (!Data.Dependences.empty()) {
      llvm::Value *DepWaitTaskArgs[7];
      DepWaitTaskArgs[0] = UpLoc;
      DepWaitTaskArgs[1] = ThreadID;
      DepWaitTaskArgs[2] = NumOfElements;
      DepWaitTaskArgs[3] = DependenciesArray.emitRawPointer(CGF);
      DepWaitTaskArgs[4] = CGF.Builder.getInt32(0);
      DepWaitTaskArgs[5] = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
      DepWaitTaskArgs[6] =
          llvm::ConstantInt::get(CGF.Int32Ty, Data.HasNowaitClause);

      CodeGenFunction::RunCleanupsScope LocalScope(CGF);

      // Build void __kmpc_omp_taskwait_deps_51(ident_t *, kmp_int32 gtid,
      // kmp_int32 ndeps, kmp_depend_info_t *dep_list, kmp_int32
      // ndeps_noalias, kmp_depend_info_t *noalias_dep_list,
      // kmp_int32 has_no_wait); if dependence info is specified.
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              M, OMPRTL___kmpc_omp_taskwait_deps_51),
                          DepWaitTaskArgs);

    } else {

      // Build call kmp_int32 __kmpc_omp_taskwait(ident_t *loc, kmp_int32
      // global_tid);
      llvm::Value *Args[] = {UpLoc, ThreadID};
      // Ignore return result until untied tasks are supported.
      CGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(M, OMPRTL___kmpc_omp_taskwait),
          Args);
    }
  }

  if (auto *Region = dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
    Region->emitUntiedSwitch(CGF);
}

void CGOpenMPRuntime::emitInlinedDirective(CodeGenFunction &CGF,
                                           OpenMPDirectiveKind InnerKind,
                                           const RegionCodeGenTy &CodeGen,
                                           bool HasCancel) {
  if (!CGF.HaveInsertPoint())
    return;
  InlinedOpenMPRegionRAII Region(CGF, CodeGen, InnerKind, HasCancel,
                                 InnerKind != OMPD_critical &&
                                     InnerKind != OMPD_master &&
                                     InnerKind != OMPD_masked);
  CGF.CapturedStmtInfo->EmitBody(CGF, /*S=*/nullptr);
}

namespace {
enum RTCancelKind {
  CancelNoreq = 0,
  CancelParallel = 1,
  CancelLoop = 2,
  CancelSections = 3,
  CancelTaskgroup = 4
};
} // anonymous namespace

static RTCancelKind getCancellationKind(OpenMPDirectiveKind CancelRegion) {
  RTCancelKind CancelKind = CancelNoreq;
  if (CancelRegion == OMPD_parallel)
    CancelKind = CancelParallel;
  else if (CancelRegion == OMPD_for)
    CancelKind = CancelLoop;
  else if (CancelRegion == OMPD_sections)
    CancelKind = CancelSections;
  else {
    assert(CancelRegion == OMPD_taskgroup);
    CancelKind = CancelTaskgroup;
  }
  return CancelKind;
}

void CGOpenMPRuntime::emitCancellationPointCall(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDirectiveKind CancelRegion) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call kmp_int32 __kmpc_cancellationpoint(ident_t *loc, kmp_int32
  // global_tid, kmp_int32 cncl_kind);
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo)) {
    // For 'cancellation point taskgroup', the task region info may not have a
    // cancel. This may instead happen in another adjacent task.
    if (CancelRegion == OMPD_taskgroup || OMPRegionInfo->hasCancel()) {
      llvm::Value *Args[] = {
          emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
          CGF.Builder.getInt32(getCancellationKind(CancelRegion))};
      // Ignore return result until untied tasks are supported.
      llvm::Value *Result = CGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(
              CGM.getModule(), OMPRTL___kmpc_cancellationpoint),
          Args);
      // if (__kmpc_cancellationpoint()) {
      //   call i32 @__kmpc_cancel_barrier( // for parallel cancellation only
      //   exit from construct;
      // }
      llvm::BasicBlock *ExitBB = CGF.createBasicBlock(".cancel.exit");
      llvm::BasicBlock *ContBB = CGF.createBasicBlock(".cancel.continue");
      llvm::Value *Cmp = CGF.Builder.CreateIsNotNull(Result);
      CGF.Builder.CreateCondBr(Cmp, ExitBB, ContBB);
      CGF.EmitBlock(ExitBB);
      if (CancelRegion == OMPD_parallel)
        emitBarrierCall(CGF, Loc, OMPD_unknown, /*EmitChecks=*/false);
      // exit from construct;
      CodeGenFunction::JumpDest CancelDest =
          CGF.getOMPCancelDestination(OMPRegionInfo->getDirectiveKind());
      CGF.EmitBranchThroughCleanup(CancelDest);
      CGF.EmitBlock(ContBB, /*IsFinished=*/true);
    }
  }
}

void CGOpenMPRuntime::emitCancelCall(CodeGenFunction &CGF, SourceLocation Loc,
                                     const Expr *IfCond,
                                     OpenMPDirectiveKind CancelRegion) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call kmp_int32 __kmpc_cancel(ident_t *loc, kmp_int32 global_tid,
  // kmp_int32 cncl_kind);
  auto &M = CGM.getModule();
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo)) {
    auto &&ThenGen = [this, &M, Loc, CancelRegion,
                      OMPRegionInfo](CodeGenFunction &CGF, PrePostActionTy &) {
      CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
      llvm::Value *Args[] = {
          RT.emitUpdateLocation(CGF, Loc), RT.getThreadID(CGF, Loc),
          CGF.Builder.getInt32(getCancellationKind(CancelRegion))};
      // Ignore return result until untied tasks are supported.
      llvm::Value *Result = CGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(M, OMPRTL___kmpc_cancel), Args);
      // if (__kmpc_cancel()) {
      //   call i32 @__kmpc_cancel_barrier( // for parallel cancellation only
      //   exit from construct;
      // }
      llvm::BasicBlock *ExitBB = CGF.createBasicBlock(".cancel.exit");
      llvm::BasicBlock *ContBB = CGF.createBasicBlock(".cancel.continue");
      llvm::Value *Cmp = CGF.Builder.CreateIsNotNull(Result);
      CGF.Builder.CreateCondBr(Cmp, ExitBB, ContBB);
      CGF.EmitBlock(ExitBB);
      if (CancelRegion == OMPD_parallel)
        RT.emitBarrierCall(CGF, Loc, OMPD_unknown, /*EmitChecks=*/false);
      // exit from construct;
      CodeGenFunction::JumpDest CancelDest =
          CGF.getOMPCancelDestination(OMPRegionInfo->getDirectiveKind());
      CGF.EmitBranchThroughCleanup(CancelDest);
      CGF.EmitBlock(ContBB, /*IsFinished=*/true);
    };
    if (IfCond) {
      emitIfClause(CGF, IfCond, ThenGen,
                   [](CodeGenFunction &, PrePostActionTy &) {});
    } else {
      RegionCodeGenTy ThenRCG(ThenGen);
      ThenRCG(CGF);
    }
  }
}

namespace {
/// Cleanup action for uses_allocators support.
class OMPUsesAllocatorsActionTy final : public PrePostActionTy {
  ArrayRef<std::pair<const Expr *, const Expr *>> Allocators;

public:
  OMPUsesAllocatorsActionTy(
      ArrayRef<std::pair<const Expr *, const Expr *>> Allocators)
      : Allocators(Allocators) {}
  void Enter(CodeGenFunction &CGF) override {
    if (!CGF.HaveInsertPoint())
      return;
    for (const auto &AllocatorData : Allocators) {
      CGF.CGM.getOpenMPRuntime().emitUsesAllocatorsInit(
          CGF, AllocatorData.first, AllocatorData.second);
    }
  }
  void Exit(CodeGenFunction &CGF) override {
    if (!CGF.HaveInsertPoint())
      return;
    for (const auto &AllocatorData : Allocators) {
      CGF.CGM.getOpenMPRuntime().emitUsesAllocatorsFini(CGF,
                                                        AllocatorData.first);
    }
  }
};
} // namespace

void CGOpenMPRuntime::emitTargetOutlinedFunction(
    const OMPExecutableDirective &D, StringRef ParentName,
    llvm::Function *&OutlinedFn, llvm::Constant *&OutlinedFnID,
    bool IsOffloadEntry, const RegionCodeGenTy &CodeGen) {
  assert(!ParentName.empty() && "Invalid target entry parent name!");
  HasEmittedTargetRegion = true;
  SmallVector<std::pair<const Expr *, const Expr *>, 4> Allocators;
  for (const auto *C : D.getClausesOfKind<OMPUsesAllocatorsClause>()) {
    for (unsigned I = 0, E = C->getNumberOfAllocators(); I < E; ++I) {
      const OMPUsesAllocatorsClause::Data D = C->getAllocatorData(I);
      if (!D.AllocatorTraits)
        continue;
      Allocators.emplace_back(D.Allocator, D.AllocatorTraits);
    }
  }
  OMPUsesAllocatorsActionTy UsesAllocatorAction(Allocators);
  CodeGen.setAction(UsesAllocatorAction);
  emitTargetOutlinedFunctionHelper(D, ParentName, OutlinedFn, OutlinedFnID,
                                   IsOffloadEntry, CodeGen);
}

void CGOpenMPRuntime::emitUsesAllocatorsInit(CodeGenFunction &CGF,
                                             const Expr *Allocator,
                                             const Expr *AllocatorTraits) {
  llvm::Value *ThreadId = getThreadID(CGF, Allocator->getExprLoc());
  ThreadId = CGF.Builder.CreateIntCast(ThreadId, CGF.IntTy, /*isSigned=*/true);
  // Use default memspace handle.
  llvm::Value *MemSpaceHandle = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  llvm::Value *NumTraits = llvm::ConstantInt::get(
      CGF.IntTy, cast<ConstantArrayType>(
                     AllocatorTraits->getType()->getAsArrayTypeUnsafe())
                     ->getSize()
                     .getLimitedValue());
  LValue AllocatorTraitsLVal = CGF.EmitLValue(AllocatorTraits);
  Address Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      AllocatorTraitsLVal.getAddress(), CGF.VoidPtrPtrTy, CGF.VoidPtrTy);
  AllocatorTraitsLVal = CGF.MakeAddrLValue(Addr, CGF.getContext().VoidPtrTy,
                                           AllocatorTraitsLVal.getBaseInfo(),
                                           AllocatorTraitsLVal.getTBAAInfo());
  llvm::Value *Traits = Addr.emitRawPointer(CGF);

  llvm::Value *AllocatorVal =
      CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                              CGM.getModule(), OMPRTL___kmpc_init_allocator),
                          {ThreadId, MemSpaceHandle, NumTraits, Traits});
  // Store to allocator.
  CGF.EmitAutoVarAlloca(*cast<VarDecl>(
      cast<DeclRefExpr>(Allocator->IgnoreParenImpCasts())->getDecl()));
  LValue AllocatorLVal = CGF.EmitLValue(Allocator->IgnoreParenImpCasts());
  AllocatorVal =
      CGF.EmitScalarConversion(AllocatorVal, CGF.getContext().VoidPtrTy,
                               Allocator->getType(), Allocator->getExprLoc());
  CGF.EmitStoreOfScalar(AllocatorVal, AllocatorLVal);
}

void CGOpenMPRuntime::emitUsesAllocatorsFini(CodeGenFunction &CGF,
                                             const Expr *Allocator) {
  llvm::Value *ThreadId = getThreadID(CGF, Allocator->getExprLoc());
  ThreadId = CGF.Builder.CreateIntCast(ThreadId, CGF.IntTy, /*isSigned=*/true);
  LValue AllocatorLVal = CGF.EmitLValue(Allocator->IgnoreParenImpCasts());
  llvm::Value *AllocatorVal =
      CGF.EmitLoadOfScalar(AllocatorLVal, Allocator->getExprLoc());
  AllocatorVal = CGF.EmitScalarConversion(AllocatorVal, Allocator->getType(),
                                          CGF.getContext().VoidPtrTy,
                                          Allocator->getExprLoc());
  (void)CGF.EmitRuntimeCall(
      OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                            OMPRTL___kmpc_destroy_allocator),
      {ThreadId, AllocatorVal});
}

void CGOpenMPRuntime::computeMinAndMaxThreadsAndTeams(
    const OMPExecutableDirective &D, CodeGenFunction &CGF,
    int32_t &MinThreadsVal, int32_t &MaxThreadsVal, int32_t &MinTeamsVal,
    int32_t &MaxTeamsVal) {

  getNumTeamsExprForTargetDirective(CGF, D, MinTeamsVal, MaxTeamsVal);
  getNumThreadsExprForTargetDirective(CGF, D, MaxThreadsVal,
                                      /*UpperBoundOnly=*/true);

  for (auto *C : D.getClausesOfKind<OMPXAttributeClause>()) {
    for (auto *A : C->getAttrs()) {
      int32_t AttrMinThreadsVal = 1, AttrMaxThreadsVal = -1;
      int32_t AttrMinBlocksVal = 1, AttrMaxBlocksVal = -1;
      if (auto *Attr = dyn_cast<CUDALaunchBoundsAttr>(A))
        CGM.handleCUDALaunchBoundsAttr(nullptr, Attr, &AttrMaxThreadsVal,
                                       &AttrMinBlocksVal, &AttrMaxBlocksVal);
      else if (auto *Attr = dyn_cast<AMDGPUFlatWorkGroupSizeAttr>(A))
        CGM.handleAMDGPUFlatWorkGroupSizeAttr(
            nullptr, Attr, /*ReqdWGS=*/nullptr, &AttrMinThreadsVal,
            &AttrMaxThreadsVal);
      else
        continue;

      MinThreadsVal = std::max(MinThreadsVal, AttrMinThreadsVal);
      if (AttrMaxThreadsVal > 0)
        MaxThreadsVal = MaxThreadsVal > 0
                            ? std::min(MaxThreadsVal, AttrMaxThreadsVal)
                            : AttrMaxThreadsVal;
      MinTeamsVal = std::max(MinTeamsVal, AttrMinBlocksVal);
      if (AttrMaxBlocksVal > 0)
        MaxTeamsVal = MaxTeamsVal > 0 ? std::min(MaxTeamsVal, AttrMaxBlocksVal)
                                      : AttrMaxBlocksVal;
    }
  }
}

void CGOpenMPRuntime::emitTargetOutlinedFunctionHelper(
    const OMPExecutableDirective &D, StringRef ParentName,
    llvm::Function *&OutlinedFn, llvm::Constant *&OutlinedFnID,
    bool IsOffloadEntry, const RegionCodeGenTy &CodeGen) {

  llvm::TargetRegionEntryInfo EntryInfo =
      getEntryInfoFromPresumedLoc(CGM, OMPBuilder, D.getBeginLoc(), ParentName);

  CodeGenFunction CGF(CGM, true);
  llvm::OpenMPIRBuilder::FunctionGenCallback &&GenerateOutlinedFunction =
      [&CGF, &D, &CodeGen](StringRef EntryFnName) {
        const CapturedStmt &CS = *D.getCapturedStmt(OMPD_target);

        CGOpenMPTargetRegionInfo CGInfo(CS, CodeGen, EntryFnName);
        CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
        return CGF.GenerateOpenMPCapturedStmtFunction(CS, D.getBeginLoc());
      };

  OMPBuilder.emitTargetRegionFunction(EntryInfo, GenerateOutlinedFunction,
                                      IsOffloadEntry, OutlinedFn, OutlinedFnID);

  if (!OutlinedFn)
    return;

  CGM.getTargetCodeGenInfo().setTargetAttributes(nullptr, OutlinedFn, CGM);

  for (auto *C : D.getClausesOfKind<OMPXAttributeClause>()) {
    for (auto *A : C->getAttrs()) {
      if (auto *Attr = dyn_cast<AMDGPUWavesPerEUAttr>(A))
        CGM.handleAMDGPUWavesPerEUAttr(OutlinedFn, Attr);
    }
  }
}

/// Checks if the expression is constant or does not have non-trivial function
/// calls.
static bool isTrivial(ASTContext &Ctx, const Expr * E) {
  // We can skip constant expressions.
  // We can skip expressions with trivial calls or simple expressions.
  return (E->isEvaluatable(Ctx, Expr::SE_AllowUndefinedBehavior) ||
          !E->hasNonTrivialCall(Ctx)) &&
         !E->HasSideEffects(Ctx, /*IncludePossibleEffects=*/true);
}

const Stmt *CGOpenMPRuntime::getSingleCompoundChild(ASTContext &Ctx,
                                                    const Stmt *Body) {
  const Stmt *Child = Body->IgnoreContainers();
  while (const auto *C = dyn_cast_or_null<CompoundStmt>(Child)) {
    Child = nullptr;
    for (const Stmt *S : C->body()) {
      if (const auto *E = dyn_cast<Expr>(S)) {
        if (isTrivial(Ctx, E))
          continue;
      }
      // Some of the statements can be ignored.
      if (isa<AsmStmt>(S) || isa<NullStmt>(S) || isa<OMPFlushDirective>(S) ||
          isa<OMPBarrierDirective>(S) || isa<OMPTaskyieldDirective>(S))
        continue;
      // Analyze declarations.
      if (const auto *DS = dyn_cast<DeclStmt>(S)) {
        if (llvm::all_of(DS->decls(), [](const Decl *D) {
              if (isa<EmptyDecl>(D) || isa<DeclContext>(D) ||
                  isa<TypeDecl>(D) || isa<PragmaCommentDecl>(D) ||
                  isa<PragmaDetectMismatchDecl>(D) || isa<UsingDecl>(D) ||
                  isa<UsingDirectiveDecl>(D) ||
                  isa<OMPDeclareReductionDecl>(D) ||
                  isa<OMPThreadPrivateDecl>(D) || isa<OMPAllocateDecl>(D))
                return true;
              const auto *VD = dyn_cast<VarDecl>(D);
              if (!VD)
                return false;
              return VD->hasGlobalStorage() || !VD->isUsed();
            }))
          continue;
      }
      // Found multiple children - cannot get the one child only.
      if (Child)
        return nullptr;
      Child = S;
    }
    if (Child)
      Child = Child->IgnoreContainers();
  }
  return Child;
}

const Expr *CGOpenMPRuntime::getNumTeamsExprForTargetDirective(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, int32_t &MinTeamsVal,
    int32_t &MaxTeamsVal) {

  OpenMPDirectiveKind DirectiveKind = D.getDirectiveKind();
  assert(isOpenMPTargetExecutionDirective(DirectiveKind) &&
         "Expected target-based executable directive.");
  switch (DirectiveKind) {
  case OMPD_target: {
    const auto *CS = D.getInnermostCapturedStmt();
    const auto *Body =
        CS->getCapturedStmt()->IgnoreContainers(/*IgnoreCaptured=*/true);
    const Stmt *ChildStmt =
        CGOpenMPRuntime::getSingleCompoundChild(CGF.getContext(), Body);
    if (const auto *NestedDir =
            dyn_cast_or_null<OMPExecutableDirective>(ChildStmt)) {
      if (isOpenMPTeamsDirective(NestedDir->getDirectiveKind())) {
        if (NestedDir->hasClausesOfKind<OMPNumTeamsClause>()) {
          const Expr *NumTeams =
              NestedDir->getSingleClause<OMPNumTeamsClause>()->getNumTeams();
          if (NumTeams->isIntegerConstantExpr(CGF.getContext()))
            if (auto Constant =
                    NumTeams->getIntegerConstantExpr(CGF.getContext()))
              MinTeamsVal = MaxTeamsVal = Constant->getExtValue();
          return NumTeams;
        }
        MinTeamsVal = MaxTeamsVal = 0;
        return nullptr;
      }
      if (isOpenMPParallelDirective(NestedDir->getDirectiveKind()) ||
          isOpenMPSimdDirective(NestedDir->getDirectiveKind())) {
        MinTeamsVal = MaxTeamsVal = 1;
        return nullptr;
      }
      MinTeamsVal = MaxTeamsVal = 1;
      return nullptr;
    }
    // A value of -1 is used to check if we need to emit no teams region
    MinTeamsVal = MaxTeamsVal = -1;
    return nullptr;
  }
  case OMPD_target_teams_loop:
  case OMPD_target_teams:
  case OMPD_target_teams_distribute:
  case OMPD_target_teams_distribute_simd:
  case OMPD_target_teams_distribute_parallel_for:
  case OMPD_target_teams_distribute_parallel_for_simd: {
    if (D.hasClausesOfKind<OMPNumTeamsClause>()) {
      const Expr *NumTeams =
          D.getSingleClause<OMPNumTeamsClause>()->getNumTeams();
      if (NumTeams->isIntegerConstantExpr(CGF.getContext()))
        if (auto Constant = NumTeams->getIntegerConstantExpr(CGF.getContext()))
          MinTeamsVal = MaxTeamsVal = Constant->getExtValue();
      return NumTeams;
    }
    MinTeamsVal = MaxTeamsVal = 0;
    return nullptr;
  }
  case OMPD_target_parallel:
  case OMPD_target_parallel_for:
  case OMPD_target_parallel_for_simd:
  case OMPD_target_parallel_loop:
  case OMPD_target_simd:
    MinTeamsVal = MaxTeamsVal = 1;
    return nullptr;
  case OMPD_parallel:
  case OMPD_for:
  case OMPD_parallel_for:
  case OMPD_parallel_loop:
  case OMPD_parallel_master:
  case OMPD_parallel_sections:
  case OMPD_for_simd:
  case OMPD_parallel_for_simd:
  case OMPD_cancel:
  case OMPD_cancellation_point:
  case OMPD_ordered:
  case OMPD_threadprivate:
  case OMPD_allocate:
  case OMPD_task:
  case OMPD_simd:
  case OMPD_tile:
  case OMPD_unroll:
  case OMPD_sections:
  case OMPD_section:
  case OMPD_single:
  case OMPD_master:
  case OMPD_critical:
  case OMPD_taskyield:
  case OMPD_barrier:
  case OMPD_taskwait:
  case OMPD_taskgroup:
  case OMPD_atomic:
  case OMPD_flush:
  case OMPD_depobj:
  case OMPD_scan:
  case OMPD_teams:
  case OMPD_target_data:
  case OMPD_target_exit_data:
  case OMPD_target_enter_data:
  case OMPD_distribute:
  case OMPD_distribute_simd:
  case OMPD_distribute_parallel_for:
  case OMPD_distribute_parallel_for_simd:
  case OMPD_teams_distribute:
  case OMPD_teams_distribute_simd:
  case OMPD_teams_distribute_parallel_for:
  case OMPD_teams_distribute_parallel_for_simd:
  case OMPD_target_update:
  case OMPD_declare_simd:
  case OMPD_declare_variant:
  case OMPD_begin_declare_variant:
  case OMPD_end_declare_variant:
  case OMPD_declare_target:
  case OMPD_end_declare_target:
  case OMPD_declare_reduction:
  case OMPD_declare_mapper:
  case OMPD_taskloop:
  case OMPD_taskloop_simd:
  case OMPD_master_taskloop:
  case OMPD_master_taskloop_simd:
  case OMPD_parallel_master_taskloop:
  case OMPD_parallel_master_taskloop_simd:
  case OMPD_requires:
  case OMPD_metadirective:
  case OMPD_unknown:
    break;
  default:
    break;
  }
  llvm_unreachable("Unexpected directive kind.");
}

llvm::Value *CGOpenMPRuntime::emitNumTeamsForTargetDirective(
    CodeGenFunction &CGF, const OMPExecutableDirective &D) {
  assert(!CGF.getLangOpts().OpenMPIsTargetDevice &&
         "Clauses associated with the teams directive expected to be emitted "
         "only for the host!");
  CGBuilderTy &Bld = CGF.Builder;
  int32_t MinNT = -1, MaxNT = -1;
  const Expr *NumTeams =
      getNumTeamsExprForTargetDirective(CGF, D, MinNT, MaxNT);
  if (NumTeams != nullptr) {
    OpenMPDirectiveKind DirectiveKind = D.getDirectiveKind();

    switch (DirectiveKind) {
    case OMPD_target: {
      const auto *CS = D.getInnermostCapturedStmt();
      CGOpenMPInnerExprInfo CGInfo(CGF, *CS);
      CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
      llvm::Value *NumTeamsVal = CGF.EmitScalarExpr(NumTeams,
                                                  /*IgnoreResultAssign*/ true);
      return Bld.CreateIntCast(NumTeamsVal, CGF.Int32Ty,
                             /*isSigned=*/true);
    }
    case OMPD_target_teams:
    case OMPD_target_teams_distribute:
    case OMPD_target_teams_distribute_simd:
    case OMPD_target_teams_distribute_parallel_for:
    case OMPD_target_teams_distribute_parallel_for_simd: {
      CodeGenFunction::RunCleanupsScope NumTeamsScope(CGF);
      llvm::Value *NumTeamsVal = CGF.EmitScalarExpr(NumTeams,
                                                  /*IgnoreResultAssign*/ true);
      return Bld.CreateIntCast(NumTeamsVal, CGF.Int32Ty,
                             /*isSigned=*/true);
    }
    default:
      break;
    }
  }

  assert(MinNT == MaxNT && "Num threads ranges require handling here.");
  return llvm::ConstantInt::get(CGF.Int32Ty, MinNT);
}

/// Check for a num threads constant value (stored in \p DefaultVal), or
/// expression (stored in \p E). If the value is conditional (via an if-clause),
/// store the condition in \p CondVal. If \p E, and \p CondVal respectively, are
/// nullptr, no expression evaluation is perfomed.
static void getNumThreads(CodeGenFunction &CGF, const CapturedStmt *CS,
                          const Expr **E, int32_t &UpperBound,
                          bool UpperBoundOnly, llvm::Value **CondVal) {
  const Stmt *Child = CGOpenMPRuntime::getSingleCompoundChild(
      CGF.getContext(), CS->getCapturedStmt());
  const auto *Dir = dyn_cast_or_null<OMPExecutableDirective>(Child);
  if (!Dir)
    return;

  if (isOpenMPParallelDirective(Dir->getDirectiveKind())) {
    // Handle if clause. If if clause present, the number of threads is
    // calculated as <cond> ? (<numthreads> ? <numthreads> : 0 ) : 1.
    if (CondVal && Dir->hasClausesOfKind<OMPIfClause>()) {
      CGOpenMPInnerExprInfo CGInfo(CGF, *CS);
      CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
      const OMPIfClause *IfClause = nullptr;
      for (const auto *C : Dir->getClausesOfKind<OMPIfClause>()) {
        if (C->getNameModifier() == OMPD_unknown ||
            C->getNameModifier() == OMPD_parallel) {
          IfClause = C;
          break;
        }
      }
      if (IfClause) {
        const Expr *CondExpr = IfClause->getCondition();
        bool Result;
        if (CondExpr->EvaluateAsBooleanCondition(Result, CGF.getContext())) {
          if (!Result) {
            UpperBound = 1;
            return;
          }
        } else {
          CodeGenFunction::LexicalScope Scope(CGF, CondExpr->getSourceRange());
          if (const auto *PreInit =
                  cast_or_null<DeclStmt>(IfClause->getPreInitStmt())) {
            for (const auto *I : PreInit->decls()) {
              if (!I->hasAttr<OMPCaptureNoInitAttr>()) {
                CGF.EmitVarDecl(cast<VarDecl>(*I));
              } else {
                CodeGenFunction::AutoVarEmission Emission =
                    CGF.EmitAutoVarAlloca(cast<VarDecl>(*I));
                CGF.EmitAutoVarCleanups(Emission);
              }
            }
            *CondVal = CGF.EvaluateExprAsBool(CondExpr);
          }
        }
      }
    }
    // Check the value of num_threads clause iff if clause was not specified
    // or is not evaluated to false.
    if (Dir->hasClausesOfKind<OMPNumThreadsClause>()) {
      CGOpenMPInnerExprInfo CGInfo(CGF, *CS);
      CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
      const auto *NumThreadsClause =
          Dir->getSingleClause<OMPNumThreadsClause>();
      const Expr *NTExpr = NumThreadsClause->getNumThreads();
      if (NTExpr->isIntegerConstantExpr(CGF.getContext()))
        if (auto Constant = NTExpr->getIntegerConstantExpr(CGF.getContext()))
          UpperBound =
              UpperBound
                  ? Constant->getZExtValue()
                  : std::min(UpperBound,
                             static_cast<int32_t>(Constant->getZExtValue()));
      // If we haven't found a upper bound, remember we saw a thread limiting
      // clause.
      if (UpperBound == -1)
        UpperBound = 0;
      if (!E)
        return;
      CodeGenFunction::LexicalScope Scope(CGF, NTExpr->getSourceRange());
      if (const auto *PreInit =
              cast_or_null<DeclStmt>(NumThreadsClause->getPreInitStmt())) {
        for (const auto *I : PreInit->decls()) {
          if (!I->hasAttr<OMPCaptureNoInitAttr>()) {
            CGF.EmitVarDecl(cast<VarDecl>(*I));
          } else {
            CodeGenFunction::AutoVarEmission Emission =
                CGF.EmitAutoVarAlloca(cast<VarDecl>(*I));
            CGF.EmitAutoVarCleanups(Emission);
          }
        }
      }
      *E = NTExpr;
    }
    return;
  }
  if (isOpenMPSimdDirective(Dir->getDirectiveKind()))
    UpperBound = 1;
}

const Expr *CGOpenMPRuntime::getNumThreadsExprForTargetDirective(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, int32_t &UpperBound,
    bool UpperBoundOnly, llvm::Value **CondVal, const Expr **ThreadLimitExpr) {
  assert((!CGF.getLangOpts().OpenMPIsTargetDevice || UpperBoundOnly) &&
         "Clauses associated with the teams directive expected to be emitted "
         "only for the host!");
  OpenMPDirectiveKind DirectiveKind = D.getDirectiveKind();
  assert(isOpenMPTargetExecutionDirective(DirectiveKind) &&
         "Expected target-based executable directive.");

  const Expr *NT = nullptr;
  const Expr **NTPtr = UpperBoundOnly ? nullptr : &NT;

  auto CheckForConstExpr = [&](const Expr *E, const Expr **EPtr) {
    if (E->isIntegerConstantExpr(CGF.getContext())) {
      if (auto Constant = E->getIntegerConstantExpr(CGF.getContext()))
        UpperBound = UpperBound ? Constant->getZExtValue()
                                : std::min(UpperBound,
                                           int32_t(Constant->getZExtValue()));
    }
    // If we haven't found a upper bound, remember we saw a thread limiting
    // clause.
    if (UpperBound == -1)
      UpperBound = 0;
    if (EPtr)
      *EPtr = E;
  };

  auto ReturnSequential = [&]() {
    UpperBound = 1;
    return NT;
  };

  switch (DirectiveKind) {
  case OMPD_target: {
    const CapturedStmt *CS = D.getInnermostCapturedStmt();
    getNumThreads(CGF, CS, NTPtr, UpperBound, UpperBoundOnly, CondVal);
    const Stmt *Child = CGOpenMPRuntime::getSingleCompoundChild(
        CGF.getContext(), CS->getCapturedStmt());
    // TODO: The standard is not clear how to resolve two thread limit clauses,
    //       let's pick the teams one if it's present, otherwise the target one.
    const auto *ThreadLimitClause = D.getSingleClause<OMPThreadLimitClause>();
    if (const auto *Dir = dyn_cast_or_null<OMPExecutableDirective>(Child)) {
      if (const auto *TLC = Dir->getSingleClause<OMPThreadLimitClause>()) {
        ThreadLimitClause = TLC;
        if (ThreadLimitExpr) {
          CGOpenMPInnerExprInfo CGInfo(CGF, *CS);
          CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
          CodeGenFunction::LexicalScope Scope(
              CGF, ThreadLimitClause->getThreadLimit()->getSourceRange());
          if (const auto *PreInit =
                  cast_or_null<DeclStmt>(ThreadLimitClause->getPreInitStmt())) {
            for (const auto *I : PreInit->decls()) {
              if (!I->hasAttr<OMPCaptureNoInitAttr>()) {
                CGF.EmitVarDecl(cast<VarDecl>(*I));
              } else {
                CodeGenFunction::AutoVarEmission Emission =
                    CGF.EmitAutoVarAlloca(cast<VarDecl>(*I));
                CGF.EmitAutoVarCleanups(Emission);
              }
            }
          }
        }
      }
    }
    if (ThreadLimitClause)
      CheckForConstExpr(ThreadLimitClause->getThreadLimit(), ThreadLimitExpr);
    if (const auto *Dir = dyn_cast_or_null<OMPExecutableDirective>(Child)) {
      if (isOpenMPTeamsDirective(Dir->getDirectiveKind()) &&
          !isOpenMPDistributeDirective(Dir->getDirectiveKind())) {
        CS = Dir->getInnermostCapturedStmt();
        const Stmt *Child = CGOpenMPRuntime::getSingleCompoundChild(
            CGF.getContext(), CS->getCapturedStmt());
        Dir = dyn_cast_or_null<OMPExecutableDirective>(Child);
      }
      if (Dir && isOpenMPParallelDirective(Dir->getDirectiveKind())) {
        CS = Dir->getInnermostCapturedStmt();
        getNumThreads(CGF, CS, NTPtr, UpperBound, UpperBoundOnly, CondVal);
      } else if (Dir && isOpenMPSimdDirective(Dir->getDirectiveKind()))
        return ReturnSequential();
    }
    return NT;
  }
  case OMPD_target_teams: {
    if (D.hasClausesOfKind<OMPThreadLimitClause>()) {
      CodeGenFunction::RunCleanupsScope ThreadLimitScope(CGF);
      const auto *ThreadLimitClause = D.getSingleClause<OMPThreadLimitClause>();
      CheckForConstExpr(ThreadLimitClause->getThreadLimit(), ThreadLimitExpr);
    }
    const CapturedStmt *CS = D.getInnermostCapturedStmt();
    getNumThreads(CGF, CS, NTPtr, UpperBound, UpperBoundOnly, CondVal);
    const Stmt *Child = CGOpenMPRuntime::getSingleCompoundChild(
        CGF.getContext(), CS->getCapturedStmt());
    if (const auto *Dir = dyn_cast_or_null<OMPExecutableDirective>(Child)) {
      if (Dir->getDirectiveKind() == OMPD_distribute) {
        CS = Dir->getInnermostCapturedStmt();
        getNumThreads(CGF, CS, NTPtr, UpperBound, UpperBoundOnly, CondVal);
      }
    }
    return NT;
  }
  case OMPD_target_teams_distribute:
    if (D.hasClausesOfKind<OMPThreadLimitClause>()) {
      CodeGenFunction::RunCleanupsScope ThreadLimitScope(CGF);
      const auto *ThreadLimitClause = D.getSingleClause<OMPThreadLimitClause>();
      CheckForConstExpr(ThreadLimitClause->getThreadLimit(), ThreadLimitExpr);
    }
    getNumThreads(CGF, D.getInnermostCapturedStmt(), NTPtr, UpperBound,
                  UpperBoundOnly, CondVal);
    return NT;
  case OMPD_target_teams_loop:
  case OMPD_target_parallel_loop:
  case OMPD_target_parallel:
  case OMPD_target_parallel_for:
  case OMPD_target_parallel_for_simd:
  case OMPD_target_teams_distribute_parallel_for:
  case OMPD_target_teams_distribute_parallel_for_simd: {
    if (CondVal && D.hasClausesOfKind<OMPIfClause>()) {
      const OMPIfClause *IfClause = nullptr;
      for (const auto *C : D.getClausesOfKind<OMPIfClause>()) {
        if (C->getNameModifier() == OMPD_unknown ||
            C->getNameModifier() == OMPD_parallel) {
          IfClause = C;
          break;
        }
      }
      if (IfClause) {
        const Expr *Cond = IfClause->getCondition();
        bool Result;
        if (Cond->EvaluateAsBooleanCondition(Result, CGF.getContext())) {
          if (!Result)
            return ReturnSequential();
        } else {
          CodeGenFunction::RunCleanupsScope Scope(CGF);
          *CondVal = CGF.EvaluateExprAsBool(Cond);
        }
      }
    }
    if (D.hasClausesOfKind<OMPThreadLimitClause>()) {
      CodeGenFunction::RunCleanupsScope ThreadLimitScope(CGF);
      const auto *ThreadLimitClause = D.getSingleClause<OMPThreadLimitClause>();
      CheckForConstExpr(ThreadLimitClause->getThreadLimit(), ThreadLimitExpr);
    }
    if (D.hasClausesOfKind<OMPNumThreadsClause>()) {
      CodeGenFunction::RunCleanupsScope NumThreadsScope(CGF);
      const auto *NumThreadsClause = D.getSingleClause<OMPNumThreadsClause>();
      CheckForConstExpr(NumThreadsClause->getNumThreads(), nullptr);
      return NumThreadsClause->getNumThreads();
    }
    return NT;
  }
  case OMPD_target_teams_distribute_simd:
  case OMPD_target_simd:
    return ReturnSequential();
  default:
    break;
  }
  llvm_unreachable("Unsupported directive kind.");
}

llvm::Value *CGOpenMPRuntime::emitNumThreadsForTargetDirective(
    CodeGenFunction &CGF, const OMPExecutableDirective &D) {
  llvm::Value *NumThreadsVal = nullptr;
  llvm::Value *CondVal = nullptr;
  llvm::Value *ThreadLimitVal = nullptr;
  const Expr *ThreadLimitExpr = nullptr;
  int32_t UpperBound = -1;

  const Expr *NT = getNumThreadsExprForTargetDirective(
      CGF, D, UpperBound, /* UpperBoundOnly */ false, &CondVal,
      &ThreadLimitExpr);

  // Thread limit expressions are used below, emit them.
  if (ThreadLimitExpr) {
    ThreadLimitVal =
        CGF.EmitScalarExpr(ThreadLimitExpr, /*IgnoreResultAssign=*/true);
    ThreadLimitVal = CGF.Builder.CreateIntCast(ThreadLimitVal, CGF.Int32Ty,
                                               /*isSigned=*/false);
  }

  // Generate the num teams expression.
  if (UpperBound == 1) {
    NumThreadsVal = CGF.Builder.getInt32(UpperBound);
  } else if (NT) {
    NumThreadsVal = CGF.EmitScalarExpr(NT, /*IgnoreResultAssign=*/true);
    NumThreadsVal = CGF.Builder.CreateIntCast(NumThreadsVal, CGF.Int32Ty,
                                              /*isSigned=*/false);
  } else if (ThreadLimitVal) {
    // If we do not have a num threads value but a thread limit, replace the
    // former with the latter. We know handled the thread limit expression.
    NumThreadsVal = ThreadLimitVal;
    ThreadLimitVal = nullptr;
  } else {
    // Default to "0" which means runtime choice.
    assert(!ThreadLimitVal && "Default not applicable with thread limit value");
    NumThreadsVal = CGF.Builder.getInt32(0);
  }

  // Handle if clause. If if clause present, the number of threads is
  // calculated as <cond> ? (<numthreads> ? <numthreads> : 0 ) : 1.
  if (CondVal) {
    CodeGenFunction::RunCleanupsScope Scope(CGF);
    NumThreadsVal = CGF.Builder.CreateSelect(CondVal, NumThreadsVal,
                                             CGF.Builder.getInt32(1));
  }

  // If the thread limit and num teams expression were present, take the
  // minimum.
  if (ThreadLimitVal) {
    NumThreadsVal = CGF.Builder.CreateSelect(
        CGF.Builder.CreateICmpULT(ThreadLimitVal, NumThreadsVal),
        ThreadLimitVal, NumThreadsVal);
  }

  return NumThreadsVal;
}

namespace {
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

// Utility to handle information from clauses associated with a given
// construct that use mappable expressions (e.g. 'map' clause, 'to' clause).
// It provides a convenient interface to obtain the information and generate
// code for that information.
class MappableExprsHandler {
public:
  /// Get the offset of the OMP_MAP_MEMBER_OF field.
  static unsigned getFlagMemberOffset() {
    unsigned Offset = 0;
    for (uint64_t Remain =
             static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                 OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF);
         !(Remain & 1); Remain = Remain >> 1)
      Offset++;
    return Offset;
  }

  /// Class that holds debugging information for a data mapping to be passed to
  /// the runtime library.
  class MappingExprInfo {
    /// The variable declaration used for the data mapping.
    const ValueDecl *MapDecl = nullptr;
    /// The original expression used in the map clause, or null if there is
    /// none.
    const Expr *MapExpr = nullptr;

  public:
    MappingExprInfo(const ValueDecl *MapDecl, const Expr *MapExpr = nullptr)
        : MapDecl(MapDecl), MapExpr(MapExpr) {}

    const ValueDecl *getMapDecl() const { return MapDecl; }
    const Expr *getMapExpr() const { return MapExpr; }
  };

  using DeviceInfoTy = llvm::OpenMPIRBuilder::DeviceInfoTy;
  using MapBaseValuesArrayTy = llvm::OpenMPIRBuilder::MapValuesArrayTy;
  using MapValuesArrayTy = llvm::OpenMPIRBuilder::MapValuesArrayTy;
  using MapFlagsArrayTy = llvm::OpenMPIRBuilder::MapFlagsArrayTy;
  using MapDimArrayTy = llvm::OpenMPIRBuilder::MapDimArrayTy;
  using MapNonContiguousArrayTy =
      llvm::OpenMPIRBuilder::MapNonContiguousArrayTy;
  using MapExprsArrayTy = SmallVector<MappingExprInfo, 4>;
  using MapValueDeclsArrayTy = SmallVector<const ValueDecl *, 4>;

  /// This structure contains combined information generated for mappable
  /// clauses, including base pointers, pointers, sizes, map types, user-defined
  /// mappers, and non-contiguous information.
  struct MapCombinedInfoTy : llvm::OpenMPIRBuilder::MapInfosTy {
    MapExprsArrayTy Exprs;
    MapValueDeclsArrayTy Mappers;
    MapValueDeclsArrayTy DevicePtrDecls;

    /// Append arrays in \a CurInfo.
    void append(MapCombinedInfoTy &CurInfo) {
      Exprs.append(CurInfo.Exprs.begin(), CurInfo.Exprs.end());
      DevicePtrDecls.append(CurInfo.DevicePtrDecls.begin(),
                            CurInfo.DevicePtrDecls.end());
      Mappers.append(CurInfo.Mappers.begin(), CurInfo.Mappers.end());
      llvm::OpenMPIRBuilder::MapInfosTy::append(CurInfo);
    }
  };

  /// Map between a struct and the its lowest & highest elements which have been
  /// mapped.
  /// [ValueDecl *] --> {LE(FieldIndex, Pointer),
  ///                    HE(FieldIndex, Pointer)}
  struct StructRangeInfoTy {
    MapCombinedInfoTy PreliminaryMapData;
    std::pair<unsigned /*FieldIndex*/, Address /*Pointer*/> LowestElem = {
        0, Address::invalid()};
    std::pair<unsigned /*FieldIndex*/, Address /*Pointer*/> HighestElem = {
        0, Address::invalid()};
    Address Base = Address::invalid();
    Address LB = Address::invalid();
    bool IsArraySection = false;
    bool HasCompleteRecord = false;
  };

private:
  /// Kind that defines how a device pointer has to be returned.
  struct MapInfo {
    OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
    OpenMPMapClauseKind MapType = OMPC_MAP_unknown;
    ArrayRef<OpenMPMapModifierKind> MapModifiers;
    ArrayRef<OpenMPMotionModifierKind> MotionModifiers;
    bool ReturnDevicePointer = false;
    bool IsImplicit = false;
    const ValueDecl *Mapper = nullptr;
    const Expr *VarRef = nullptr;
    bool ForDeviceAddr = false;

    MapInfo() = default;
    MapInfo(
        OMPClauseMappableExprCommon::MappableExprComponentListRef Components,
        OpenMPMapClauseKind MapType,
        ArrayRef<OpenMPMapModifierKind> MapModifiers,
        ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
        bool ReturnDevicePointer, bool IsImplicit,
        const ValueDecl *Mapper = nullptr, const Expr *VarRef = nullptr,
        bool ForDeviceAddr = false)
        : Components(Components), MapType(MapType), MapModifiers(MapModifiers),
          MotionModifiers(MotionModifiers),
          ReturnDevicePointer(ReturnDevicePointer), IsImplicit(IsImplicit),
          Mapper(Mapper), VarRef(VarRef), ForDeviceAddr(ForDeviceAddr) {}
  };

  /// If use_device_ptr or use_device_addr is used on a decl which is a struct
  /// member and there is no map information about it, then emission of that
  /// entry is deferred until the whole struct has been processed.
  struct DeferredDevicePtrEntryTy {
    const Expr *IE = nullptr;
    const ValueDecl *VD = nullptr;
    bool ForDeviceAddr = false;

    DeferredDevicePtrEntryTy(const Expr *IE, const ValueDecl *VD,
                             bool ForDeviceAddr)
        : IE(IE), VD(VD), ForDeviceAddr(ForDeviceAddr) {}
  };

  /// The target directive from where the mappable clauses were extracted. It
  /// is either a executable directive or a user-defined mapper directive.
  llvm::PointerUnion<const OMPExecutableDirective *,
                     const OMPDeclareMapperDecl *>
      CurDir;

  /// Function the directive is being generated for.
  CodeGenFunction &CGF;

  /// Set of all first private variables in the current directive.
  /// bool data is set to true if the variable is implicitly marked as
  /// firstprivate, false otherwise.
  llvm::DenseMap<CanonicalDeclPtr<const VarDecl>, bool> FirstPrivateDecls;

  /// Map between device pointer declarations and their expression components.
  /// The key value for declarations in 'this' is null.
  llvm::DenseMap<
      const ValueDecl *,
      SmallVector<OMPClauseMappableExprCommon::MappableExprComponentListRef, 4>>
      DevPointersMap;

  /// Map between device addr declarations and their expression components.
  /// The key value for declarations in 'this' is null.
  llvm::DenseMap<
      const ValueDecl *,
      SmallVector<OMPClauseMappableExprCommon::MappableExprComponentListRef, 4>>
      HasDevAddrsMap;

  /// Map between lambda declarations and their map type.
  llvm::DenseMap<const ValueDecl *, const OMPMapClause *> LambdasMap;

  llvm::Value *getExprTypeSize(const Expr *E) const {
    QualType ExprTy = E->getType().getCanonicalType();

    // Calculate the size for array shaping expression.
    if (const auto *OAE = dyn_cast<OMPArrayShapingExpr>(E)) {
      llvm::Value *Size =
          CGF.getTypeSize(OAE->getBase()->getType()->getPointeeType());
      for (const Expr *SE : OAE->getDimensions()) {
        llvm::Value *Sz = CGF.EmitScalarExpr(SE);
        Sz = CGF.EmitScalarConversion(Sz, SE->getType(),
                                      CGF.getContext().getSizeType(),
                                      SE->getExprLoc());
        Size = CGF.Builder.CreateNUWMul(Size, Sz);
      }
      return Size;
    }

    // Reference types are ignored for mapping purposes.
    if (const auto *RefTy = ExprTy->getAs<ReferenceType>())
      ExprTy = RefTy->getPointeeType().getCanonicalType();

    // Given that an array section is considered a built-in type, we need to
    // do the calculation based on the length of the section instead of relying
    // on CGF.getTypeSize(E->getType()).
    if (const auto *OAE = dyn_cast<ArraySectionExpr>(E)) {
      QualType BaseTy = ArraySectionExpr::getBaseOriginalType(
                            OAE->getBase()->IgnoreParenImpCasts())
                            .getCanonicalType();

      // If there is no length associated with the expression and lower bound is
      // not specified too, that means we are using the whole length of the
      // base.
      if (!OAE->getLength() && OAE->getColonLocFirst().isValid() &&
          !OAE->getLowerBound())
        return CGF.getTypeSize(BaseTy);

      llvm::Value *ElemSize;
      if (const auto *PTy = BaseTy->getAs<PointerType>()) {
        ElemSize = CGF.getTypeSize(PTy->getPointeeType().getCanonicalType());
      } else {
        const auto *ATy = cast<ArrayType>(BaseTy.getTypePtr());
        assert(ATy && "Expecting array type if not a pointer type.");
        ElemSize = CGF.getTypeSize(ATy->getElementType().getCanonicalType());
      }

      // If we don't have a length at this point, that is because we have an
      // array section with a single element.
      if (!OAE->getLength() && OAE->getColonLocFirst().isInvalid())
        return ElemSize;

      if (const Expr *LenExpr = OAE->getLength()) {
        llvm::Value *LengthVal = CGF.EmitScalarExpr(LenExpr);
        LengthVal = CGF.EmitScalarConversion(LengthVal, LenExpr->getType(),
                                             CGF.getContext().getSizeType(),
                                             LenExpr->getExprLoc());
        return CGF.Builder.CreateNUWMul(LengthVal, ElemSize);
      }
      assert(!OAE->getLength() && OAE->getColonLocFirst().isValid() &&
             OAE->getLowerBound() && "expected array_section[lb:].");
      // Size = sizetype - lb * elemtype;
      llvm::Value *LengthVal = CGF.getTypeSize(BaseTy);
      llvm::Value *LBVal = CGF.EmitScalarExpr(OAE->getLowerBound());
      LBVal = CGF.EmitScalarConversion(LBVal, OAE->getLowerBound()->getType(),
                                       CGF.getContext().getSizeType(),
                                       OAE->getLowerBound()->getExprLoc());
      LBVal = CGF.Builder.CreateNUWMul(LBVal, ElemSize);
      llvm::Value *Cmp = CGF.Builder.CreateICmpUGT(LengthVal, LBVal);
      llvm::Value *TrueVal = CGF.Builder.CreateNUWSub(LengthVal, LBVal);
      LengthVal = CGF.Builder.CreateSelect(
          Cmp, TrueVal, llvm::ConstantInt::get(CGF.SizeTy, 0));
      return LengthVal;
    }
    return CGF.getTypeSize(ExprTy);
  }

  /// Return the corresponding bits for a given map clause modifier. Add
  /// a flag marking the map as a pointer if requested. Add a flag marking the
  /// map as the first one of a series of maps that relate to the same map
  /// expression.
  OpenMPOffloadMappingFlags getMapTypeBits(
      OpenMPMapClauseKind MapType, ArrayRef<OpenMPMapModifierKind> MapModifiers,
      ArrayRef<OpenMPMotionModifierKind> MotionModifiers, bool IsImplicit,
      bool AddPtrFlag, bool AddIsTargetParamFlag, bool IsNonContiguous) const {
    OpenMPOffloadMappingFlags Bits =
        IsImplicit ? OpenMPOffloadMappingFlags::OMP_MAP_IMPLICIT
                   : OpenMPOffloadMappingFlags::OMP_MAP_NONE;
    switch (MapType) {
    case OMPC_MAP_alloc:
    case OMPC_MAP_release:
      // alloc and release is the default behavior in the runtime library,  i.e.
      // if we don't pass any bits alloc/release that is what the runtime is
      // going to do. Therefore, we don't need to signal anything for these two
      // type modifiers.
      break;
    case OMPC_MAP_to:
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_TO;
      break;
    case OMPC_MAP_from:
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_FROM;
      break;
    case OMPC_MAP_tofrom:
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_TO |
              OpenMPOffloadMappingFlags::OMP_MAP_FROM;
      break;
    case OMPC_MAP_delete:
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_DELETE;
      break;
    case OMPC_MAP_unknown:
      llvm_unreachable("Unexpected map type!");
    }
    if (AddPtrFlag)
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ;
    if (AddIsTargetParamFlag)
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_TARGET_PARAM;
    if (llvm::is_contained(MapModifiers, OMPC_MAP_MODIFIER_always))
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_ALWAYS;
    if (llvm::is_contained(MapModifiers, OMPC_MAP_MODIFIER_close))
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_CLOSE;
    if (llvm::is_contained(MapModifiers, OMPC_MAP_MODIFIER_present) ||
        llvm::is_contained(MotionModifiers, OMPC_MOTION_MODIFIER_present))
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_PRESENT;
    if (llvm::is_contained(MapModifiers, OMPC_MAP_MODIFIER_ompx_hold))
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_OMPX_HOLD;
    if (IsNonContiguous)
      Bits |= OpenMPOffloadMappingFlags::OMP_MAP_NON_CONTIG;
    return Bits;
  }

  /// Return true if the provided expression is a final array section. A
  /// final array section, is one whose length can't be proved to be one.
  bool isFinalArraySectionExpression(const Expr *E) const {
    const auto *OASE = dyn_cast<ArraySectionExpr>(E);

    // It is not an array section and therefore not a unity-size one.
    if (!OASE)
      return false;

    // An array section with no colon always refer to a single element.
    if (OASE->getColonLocFirst().isInvalid())
      return false;

    const Expr *Length = OASE->getLength();

    // If we don't have a length we have to check if the array has size 1
    // for this dimension. Also, we should always expect a length if the
    // base type is pointer.
    if (!Length) {
      QualType BaseQTy = ArraySectionExpr::getBaseOriginalType(
                             OASE->getBase()->IgnoreParenImpCasts())
                             .getCanonicalType();
      if (const auto *ATy = dyn_cast<ConstantArrayType>(BaseQTy.getTypePtr()))
        return ATy->getSExtSize() != 1;
      // If we don't have a constant dimension length, we have to consider
      // the current section as having any size, so it is not necessarily
      // unitary. If it happen to be unity size, that's user fault.
      return true;
    }

    // Check if the length evaluates to 1.
    Expr::EvalResult Result;
    if (!Length->EvaluateAsInt(Result, CGF.getContext()))
      return true; // Can have more that size 1.

    llvm::APSInt ConstLength = Result.Val.getInt();
    return ConstLength.getSExtValue() != 1;
  }

  /// Generate the base pointers, section pointers, sizes, map type bits, and
  /// user-defined mappers (all included in \a CombinedInfo) for the provided
  /// map type, map or motion modifiers, and expression components.
  /// \a IsFirstComponent should be set to true if the provided set of
  /// components is the first associated with a capture.
  void generateInfoForComponentList(
      OpenMPMapClauseKind MapType, ArrayRef<OpenMPMapModifierKind> MapModifiers,
      ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components,
      MapCombinedInfoTy &CombinedInfo,
      MapCombinedInfoTy &StructBaseCombinedInfo,
      StructRangeInfoTy &PartialStruct, bool IsFirstComponentList,
      bool IsImplicit, bool GenerateAllInfoForClauses,
      const ValueDecl *Mapper = nullptr, bool ForDeviceAddr = false,
      const ValueDecl *BaseDecl = nullptr, const Expr *MapExpr = nullptr,
      ArrayRef<OMPClauseMappableExprCommon::MappableExprComponentListRef>
          OverlappedElements = std::nullopt,
      bool AreBothBasePtrAndPteeMapped = false) const {
    // The following summarizes what has to be generated for each map and the
    // types below. The generated information is expressed in this order:
    // base pointer, section pointer, size, flags
    // (to add to the ones that come from the map type and modifier).
    //
    // double d;
    // int i[100];
    // float *p;
    // int **a = &i;
    //
    // struct S1 {
    //   int i;
    //   float f[50];
    // }
    // struct S2 {
    //   int i;
    //   float f[50];
    //   S1 s;
    //   double *p;
    //   struct S2 *ps;
    //   int &ref;
    // }
    // S2 s;
    // S2 *ps;
    //
    // map(d)
    // &d, &d, sizeof(double), TARGET_PARAM | TO | FROM
    //
    // map(i)
    // &i, &i, 100*sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(i[1:23])
    // &i(=&i[0]), &i[1], 23*sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(p)
    // &p, &p, sizeof(float*), TARGET_PARAM | TO | FROM
    //
    // map(p[1:24])
    // &p, &p[1], 24*sizeof(float), TARGET_PARAM | TO | FROM | PTR_AND_OBJ
    // in unified shared memory mode or for local pointers
    // p, &p[1], 24*sizeof(float), TARGET_PARAM | TO | FROM
    //
    // map((*a)[0:3])
    // &(*a), &(*a), sizeof(pointer), TARGET_PARAM | TO | FROM
    // &(*a), &(*a)[0], 3*sizeof(int), PTR_AND_OBJ | TO | FROM
    //
    // map(**a)
    // &(*a), &(*a), sizeof(pointer), TARGET_PARAM | TO | FROM
    // &(*a), &(**a), sizeof(int), PTR_AND_OBJ | TO | FROM
    //
    // map(s)
    // &s, &s, sizeof(S2), TARGET_PARAM | TO | FROM
    //
    // map(s.i)
    // &s, &(s.i), sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(s.s.f)
    // &s, &(s.s.f[0]), 50*sizeof(float), TARGET_PARAM | TO | FROM
    //
    // map(s.p)
    // &s, &(s.p), sizeof(double*), TARGET_PARAM | TO | FROM
    //
    // map(to: s.p[:22])
    // &s, &(s.p), sizeof(double*), TARGET_PARAM (*)
    // &s, &(s.p), sizeof(double*), MEMBER_OF(1) (**)
    // &(s.p), &(s.p[0]), 22*sizeof(double),
    //   MEMBER_OF(1) | PTR_AND_OBJ | TO (***)
    // (*) alloc space for struct members, only this is a target parameter
    // (**) map the pointer (nothing to be mapped in this example) (the compiler
    //      optimizes this entry out, same in the examples below)
    // (***) map the pointee (map: to)
    //
    // map(to: s.ref)
    // &s, &(s.ref), sizeof(int*), TARGET_PARAM (*)
    // &s, &(s.ref), sizeof(int), MEMBER_OF(1) | PTR_AND_OBJ | TO (***)
    // (*) alloc space for struct members, only this is a target parameter
    // (**) map the pointer (nothing to be mapped in this example) (the compiler
    //      optimizes this entry out, same in the examples below)
    // (***) map the pointee (map: to)
    //
    // map(s.ps)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM | TO | FROM
    //
    // map(from: s.ps->s.i)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->s.i), sizeof(int), MEMBER_OF(1) | PTR_AND_OBJ  | FROM
    //
    // map(to: s.ps->ps)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ  | TO
    //
    // map(s.ps->ps->ps)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(s.ps->ps), &(s.ps->ps->ps), sizeof(S2*), PTR_AND_OBJ | TO | FROM
    //
    // map(to: s.ps->ps->s.f[:22])
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(s.ps->ps), &(s.ps->ps->s.f[0]), 22*sizeof(float), PTR_AND_OBJ | TO
    //
    // map(ps)
    // &ps, &ps, sizeof(S2*), TARGET_PARAM | TO | FROM
    //
    // map(ps->i)
    // ps, &(ps->i), sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(ps->s.f)
    // ps, &(ps->s.f[0]), 50*sizeof(float), TARGET_PARAM | TO | FROM
    //
    // map(from: ps->p)
    // ps, &(ps->p), sizeof(double*), TARGET_PARAM | FROM
    //
    // map(to: ps->p[:22])
    // ps, &(ps->p), sizeof(double*), TARGET_PARAM
    // ps, &(ps->p), sizeof(double*), MEMBER_OF(1)
    // &(ps->p), &(ps->p[0]), 22*sizeof(double), MEMBER_OF(1) | PTR_AND_OBJ | TO
    //
    // map(ps->ps)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM | TO | FROM
    //
    // map(from: ps->ps->s.i)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->s.i), sizeof(int), MEMBER_OF(1) | PTR_AND_OBJ | FROM
    //
    // map(from: ps->ps->ps)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ | FROM
    //
    // map(ps->ps->ps->ps)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(ps->ps->ps), &(ps->ps->ps->ps), sizeof(S2*), PTR_AND_OBJ | TO | FROM
    //
    // map(to: ps->ps->ps->s.f[:22])
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(ps->ps->ps), &(ps->ps->ps->s.f[0]), 22*sizeof(float), PTR_AND_OBJ | TO
    //
    // map(to: s.f[:22]) map(from: s.p[:33])
    // &s, &(s.f[0]), 50*sizeof(float) + sizeof(struct S1) +
    //     sizeof(double*) (**), TARGET_PARAM
    // &s, &(s.f[0]), 22*sizeof(float), MEMBER_OF(1) | TO
    // &s, &(s.p), sizeof(double*), MEMBER_OF(1)
    // &(s.p), &(s.p[0]), 33*sizeof(double), MEMBER_OF(1) | PTR_AND_OBJ | FROM
    // (*) allocate contiguous space needed to fit all mapped members even if
    //     we allocate space for members not mapped (in this example,
    //     s.f[22..49] and s.s are not mapped, yet we must allocate space for
    //     them as well because they fall between &s.f[0] and &s.p)
    //
    // map(from: s.f[:22]) map(to: ps->p[:33])
    // &s, &(s.f[0]), 22*sizeof(float), TARGET_PARAM | FROM
    // ps, &(ps->p), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->p), sizeof(double*), MEMBER_OF(2) (*)
    // &(ps->p), &(ps->p[0]), 33*sizeof(double), MEMBER_OF(2) | PTR_AND_OBJ | TO
    // (*) the struct this entry pertains to is the 2nd element in the list of
    //     arguments, hence MEMBER_OF(2)
    //
    // map(from: s.f[:22], s.s) map(to: ps->p[:33])
    // &s, &(s.f[0]), 50*sizeof(float) + sizeof(struct S1), TARGET_PARAM
    // &s, &(s.f[0]), 22*sizeof(float), MEMBER_OF(1) | FROM
    // &s, &(s.s), sizeof(struct S1), MEMBER_OF(1) | FROM
    // ps, &(ps->p), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->p), sizeof(double*), MEMBER_OF(4) (*)
    // &(ps->p), &(ps->p[0]), 33*sizeof(double), MEMBER_OF(4) | PTR_AND_OBJ | TO
    // (*) the struct this entry pertains to is the 4th element in the list
    //     of arguments, hence MEMBER_OF(4)
    //
    // map(p, p[:100])
    // ===> map(p[:100])
    // &p, &p[0], 100*sizeof(float), TARGET_PARAM | PTR_AND_OBJ | TO | FROM

    // Track if the map information being generated is the first for a capture.
    bool IsCaptureFirstInfo = IsFirstComponentList;
    // When the variable is on a declare target link or in a to clause with
    // unified memory, a reference is needed to hold the host/device address
    // of the variable.
    bool RequiresReference = false;

    // Scan the components from the base to the complete expression.
    auto CI = Components.rbegin();
    auto CE = Components.rend();
    auto I = CI;

    // Track if the map information being generated is the first for a list of
    // components.
    bool IsExpressionFirstInfo = true;
    bool FirstPointerInComplexData = false;
    Address BP = Address::invalid();
    const Expr *AssocExpr = I->getAssociatedExpression();
    const auto *AE = dyn_cast<ArraySubscriptExpr>(AssocExpr);
    const auto *OASE = dyn_cast<ArraySectionExpr>(AssocExpr);
    const auto *OAShE = dyn_cast<OMPArrayShapingExpr>(AssocExpr);

    if (AreBothBasePtrAndPteeMapped && std::next(I) == CE)
      return;
    if (isa<MemberExpr>(AssocExpr)) {
      // The base is the 'this' pointer. The content of the pointer is going
      // to be the base of the field being mapped.
      BP = CGF.LoadCXXThisAddress();
    } else if ((AE && isa<CXXThisExpr>(AE->getBase()->IgnoreParenImpCasts())) ||
               (OASE &&
                isa<CXXThisExpr>(OASE->getBase()->IgnoreParenImpCasts()))) {
      BP = CGF.EmitOMPSharedLValue(AssocExpr).getAddress();
    } else if (OAShE &&
               isa<CXXThisExpr>(OAShE->getBase()->IgnoreParenCasts())) {
      BP = Address(
          CGF.EmitScalarExpr(OAShE->getBase()),
          CGF.ConvertTypeForMem(OAShE->getBase()->getType()->getPointeeType()),
          CGF.getContext().getTypeAlignInChars(OAShE->getBase()->getType()));
    } else {
      // The base is the reference to the variable.
      // BP = &Var.
      BP = CGF.EmitOMPSharedLValue(AssocExpr).getAddress();
      if (const auto *VD =
              dyn_cast_or_null<VarDecl>(I->getAssociatedDeclaration())) {
        if (std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
                OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD)) {
          if ((*Res == OMPDeclareTargetDeclAttr::MT_Link) ||
              ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
                *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
               CGF.CGM.getOpenMPRuntime().hasRequiresUnifiedSharedMemory())) {
            RequiresReference = true;
            BP = CGF.CGM.getOpenMPRuntime().getAddrOfDeclareTargetVar(VD);
          }
        }
      }

      // If the variable is a pointer and is being dereferenced (i.e. is not
      // the last component), the base has to be the pointer itself, not its
      // reference. References are ignored for mapping purposes.
      QualType Ty =
          I->getAssociatedDeclaration()->getType().getNonReferenceType();
      if (Ty->isAnyPointerType() && std::next(I) != CE) {
        // No need to generate individual map information for the pointer, it
        // can be associated with the combined storage if shared memory mode is
        // active or the base declaration is not global variable.
        const auto *VD = dyn_cast<VarDecl>(I->getAssociatedDeclaration());
        if (!AreBothBasePtrAndPteeMapped &&
            (CGF.CGM.getOpenMPRuntime().hasRequiresUnifiedSharedMemory() ||
             !VD || VD->hasLocalStorage()))
          BP = CGF.EmitLoadOfPointer(BP, Ty->castAs<PointerType>());
        else
          FirstPointerInComplexData = true;
        ++I;
      }
    }

    // Track whether a component of the list should be marked as MEMBER_OF some
    // combined entry (for partial structs). Only the first PTR_AND_OBJ entry
    // in a component list should be marked as MEMBER_OF, all subsequent entries
    // do not belong to the base struct. E.g.
    // struct S2 s;
    // s.ps->ps->ps->f[:]
    //   (1) (2) (3) (4)
    // ps(1) is a member pointer, ps(2) is a pointee of ps(1), so it is a
    // PTR_AND_OBJ entry; the PTR is ps(1), so MEMBER_OF the base struct. ps(3)
    // is the pointee of ps(2) which is not member of struct s, so it should not
    // be marked as such (it is still PTR_AND_OBJ).
    // The variable is initialized to false so that PTR_AND_OBJ entries which
    // are not struct members are not considered (e.g. array of pointers to
    // data).
    bool ShouldBeMemberOf = false;

    // Variable keeping track of whether or not we have encountered a component
    // in the component list which is a member expression. Useful when we have a
    // pointer or a final array section, in which case it is the previous
    // component in the list which tells us whether we have a member expression.
    // E.g. X.f[:]
    // While processing the final array section "[:]" it is "f" which tells us
    // whether we are dealing with a member of a declared struct.
    const MemberExpr *EncounteredME = nullptr;

    // Track for the total number of dimension. Start from one for the dummy
    // dimension.
    uint64_t DimSize = 1;

    bool IsNonContiguous = CombinedInfo.NonContigInfo.IsNonContiguous;
    bool IsPrevMemberReference = false;

    // We need to check if we will be encountering any MEs. If we do not
    // encounter any ME expression it means we will be mapping the whole struct.
    // In that case we need to skip adding an entry for the struct to the
    // CombinedInfo list and instead add an entry to the StructBaseCombinedInfo
    // list only when generating all info for clauses.
    bool IsMappingWholeStruct = true;
    if (!GenerateAllInfoForClauses) {
      IsMappingWholeStruct = false;
    } else {
      for (auto TempI = I; TempI != CE; ++TempI) {
        const MemberExpr *PossibleME =
            dyn_cast<MemberExpr>(TempI->getAssociatedExpression());
        if (PossibleME) {
          IsMappingWholeStruct = false;
          break;
        }
      }
    }

    for (; I != CE; ++I) {
      // If the current component is member of a struct (parent struct) mark it.
      if (!EncounteredME) {
        EncounteredME = dyn_cast<MemberExpr>(I->getAssociatedExpression());
        // If we encounter a PTR_AND_OBJ entry from now on it should be marked
        // as MEMBER_OF the parent struct.
        if (EncounteredME) {
          ShouldBeMemberOf = true;
          // Do not emit as complex pointer if this is actually not array-like
          // expression.
          if (FirstPointerInComplexData) {
            QualType Ty = std::prev(I)
                              ->getAssociatedDeclaration()
                              ->getType()
                              .getNonReferenceType();
            BP = CGF.EmitLoadOfPointer(BP, Ty->castAs<PointerType>());
            FirstPointerInComplexData = false;
          }
        }
      }

      auto Next = std::next(I);

      // We need to generate the addresses and sizes if this is the last
      // component, if the component is a pointer or if it is an array section
      // whose length can't be proved to be one. If this is a pointer, it
      // becomes the base address for the following components.

      // A final array section, is one whose length can't be proved to be one.
      // If the map item is non-contiguous then we don't treat any array section
      // as final array section.
      bool IsFinalArraySection =
          !IsNonContiguous &&
          isFinalArraySectionExpression(I->getAssociatedExpression());

      // If we have a declaration for the mapping use that, otherwise use
      // the base declaration of the map clause.
      const ValueDecl *MapDecl = (I->getAssociatedDeclaration())
                                     ? I->getAssociatedDeclaration()
                                     : BaseDecl;
      MapExpr = (I->getAssociatedExpression()) ? I->getAssociatedExpression()
                                               : MapExpr;

      // Get information on whether the element is a pointer. Have to do a
      // special treatment for array sections given that they are built-in
      // types.
      const auto *OASE =
          dyn_cast<ArraySectionExpr>(I->getAssociatedExpression());
      const auto *OAShE =
          dyn_cast<OMPArrayShapingExpr>(I->getAssociatedExpression());
      const auto *UO = dyn_cast<UnaryOperator>(I->getAssociatedExpression());
      const auto *BO = dyn_cast<BinaryOperator>(I->getAssociatedExpression());
      bool IsPointer =
          OAShE ||
          (OASE && ArraySectionExpr::getBaseOriginalType(OASE)
                       .getCanonicalType()
                       ->isAnyPointerType()) ||
          I->getAssociatedExpression()->getType()->isAnyPointerType();
      bool IsMemberReference = isa<MemberExpr>(I->getAssociatedExpression()) &&
                               MapDecl &&
                               MapDecl->getType()->isLValueReferenceType();
      bool IsNonDerefPointer = IsPointer &&
                               !(UO && UO->getOpcode() != UO_Deref) && !BO &&
                               !IsNonContiguous;

      if (OASE)
        ++DimSize;

      if (Next == CE || IsMemberReference || IsNonDerefPointer ||
          IsFinalArraySection) {
        // If this is not the last component, we expect the pointer to be
        // associated with an array expression or member expression.
        assert((Next == CE ||
                isa<MemberExpr>(Next->getAssociatedExpression()) ||
                isa<ArraySubscriptExpr>(Next->getAssociatedExpression()) ||
                isa<ArraySectionExpr>(Next->getAssociatedExpression()) ||
                isa<OMPArrayShapingExpr>(Next->getAssociatedExpression()) ||
                isa<UnaryOperator>(Next->getAssociatedExpression()) ||
                isa<BinaryOperator>(Next->getAssociatedExpression())) &&
               "Unexpected expression");

        Address LB = Address::invalid();
        Address LowestElem = Address::invalid();
        auto &&EmitMemberExprBase = [](CodeGenFunction &CGF,
                                       const MemberExpr *E) {
          const Expr *BaseExpr = E->getBase();
          // If this is s.x, emit s as an lvalue.  If it is s->x, emit s as a
          // scalar.
          LValue BaseLV;
          if (E->isArrow()) {
            LValueBaseInfo BaseInfo;
            TBAAAccessInfo TBAAInfo;
            Address Addr =
                CGF.EmitPointerWithAlignment(BaseExpr, &BaseInfo, &TBAAInfo);
            QualType PtrTy = BaseExpr->getType()->getPointeeType();
            BaseLV = CGF.MakeAddrLValue(Addr, PtrTy, BaseInfo, TBAAInfo);
          } else {
            BaseLV = CGF.EmitOMPSharedLValue(BaseExpr);
          }
          return BaseLV;
        };
        if (OAShE) {
          LowestElem = LB =
              Address(CGF.EmitScalarExpr(OAShE->getBase()),
                      CGF.ConvertTypeForMem(
                          OAShE->getBase()->getType()->getPointeeType()),
                      CGF.getContext().getTypeAlignInChars(
                          OAShE->getBase()->getType()));
        } else if (IsMemberReference) {
          const auto *ME = cast<MemberExpr>(I->getAssociatedExpression());
          LValue BaseLVal = EmitMemberExprBase(CGF, ME);
          LowestElem = CGF.EmitLValueForFieldInitialization(
                              BaseLVal, cast<FieldDecl>(MapDecl))
                           .getAddress();
          LB = CGF.EmitLoadOfReferenceLValue(LowestElem, MapDecl->getType())
                   .getAddress();
        } else {
          LowestElem = LB =
              CGF.EmitOMPSharedLValue(I->getAssociatedExpression())
                  .getAddress();
        }

        // If this component is a pointer inside the base struct then we don't
        // need to create any entry for it - it will be combined with the object
        // it is pointing to into a single PTR_AND_OBJ entry.
        bool IsMemberPointerOrAddr =
            EncounteredME &&
            (((IsPointer || ForDeviceAddr) &&
              I->getAssociatedExpression() == EncounteredME) ||
             (IsPrevMemberReference && !IsPointer) ||
             (IsMemberReference && Next != CE &&
              !Next->getAssociatedExpression()->getType()->isPointerType()));
        if (!OverlappedElements.empty() && Next == CE) {
          // Handle base element with the info for overlapped elements.
          assert(!PartialStruct.Base.isValid() && "The base element is set.");
          assert(!IsPointer &&
                 "Unexpected base element with the pointer type.");
          // Mark the whole struct as the struct that requires allocation on the
          // device.
          PartialStruct.LowestElem = {0, LowestElem};
          CharUnits TypeSize = CGF.getContext().getTypeSizeInChars(
              I->getAssociatedExpression()->getType());
          Address HB = CGF.Builder.CreateConstGEP(
              CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                  LowestElem, CGF.VoidPtrTy, CGF.Int8Ty),
              TypeSize.getQuantity() - 1);
          PartialStruct.HighestElem = {
              std::numeric_limits<decltype(
                  PartialStruct.HighestElem.first)>::max(),
              HB};
          PartialStruct.Base = BP;
          PartialStruct.LB = LB;
          assert(
              PartialStruct.PreliminaryMapData.BasePointers.empty() &&
              "Overlapped elements must be used only once for the variable.");
          std::swap(PartialStruct.PreliminaryMapData, CombinedInfo);
          // Emit data for non-overlapped data.
          OpenMPOffloadMappingFlags Flags =
              OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF |
              getMapTypeBits(MapType, MapModifiers, MotionModifiers, IsImplicit,
                             /*AddPtrFlag=*/false,
                             /*AddIsTargetParamFlag=*/false, IsNonContiguous);
          llvm::Value *Size = nullptr;
          // Do bitcopy of all non-overlapped structure elements.
          for (OMPClauseMappableExprCommon::MappableExprComponentListRef
                   Component : OverlappedElements) {
            Address ComponentLB = Address::invalid();
            for (const OMPClauseMappableExprCommon::MappableComponent &MC :
                 Component) {
              if (const ValueDecl *VD = MC.getAssociatedDeclaration()) {
                const auto *FD = dyn_cast<FieldDecl>(VD);
                if (FD && FD->getType()->isLValueReferenceType()) {
                  const auto *ME =
                      cast<MemberExpr>(MC.getAssociatedExpression());
                  LValue BaseLVal = EmitMemberExprBase(CGF, ME);
                  ComponentLB =
                      CGF.EmitLValueForFieldInitialization(BaseLVal, FD)
                          .getAddress();
                } else {
                  ComponentLB =
                      CGF.EmitOMPSharedLValue(MC.getAssociatedExpression())
                          .getAddress();
                }
                llvm::Value *ComponentLBPtr = ComponentLB.emitRawPointer(CGF);
                llvm::Value *LBPtr = LB.emitRawPointer(CGF);
                Size = CGF.Builder.CreatePtrDiff(CGF.Int8Ty, ComponentLBPtr,
                                                 LBPtr);
                break;
              }
            }
            assert(Size && "Failed to determine structure size");
            CombinedInfo.Exprs.emplace_back(MapDecl, MapExpr);
            CombinedInfo.BasePointers.push_back(BP.emitRawPointer(CGF));
            CombinedInfo.DevicePtrDecls.push_back(nullptr);
            CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
            CombinedInfo.Pointers.push_back(LB.emitRawPointer(CGF));
            CombinedInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
                Size, CGF.Int64Ty, /*isSigned=*/true));
            CombinedInfo.Types.push_back(Flags);
            CombinedInfo.Mappers.push_back(nullptr);
            CombinedInfo.NonContigInfo.Dims.push_back(IsNonContiguous ? DimSize
                                                                      : 1);
            LB = CGF.Builder.CreateConstGEP(ComponentLB, 1);
          }
          CombinedInfo.Exprs.emplace_back(MapDecl, MapExpr);
          CombinedInfo.BasePointers.push_back(BP.emitRawPointer(CGF));
          CombinedInfo.DevicePtrDecls.push_back(nullptr);
          CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
          CombinedInfo.Pointers.push_back(LB.emitRawPointer(CGF));
          llvm::Value *LBPtr = LB.emitRawPointer(CGF);
          Size = CGF.Builder.CreatePtrDiff(
              CGF.Int8Ty, CGF.Builder.CreateConstGEP(HB, 1).emitRawPointer(CGF),
              LBPtr);
          CombinedInfo.Sizes.push_back(
              CGF.Builder.CreateIntCast(Size, CGF.Int64Ty, /*isSigned=*/true));
          CombinedInfo.Types.push_back(Flags);
          CombinedInfo.Mappers.push_back(nullptr);
          CombinedInfo.NonContigInfo.Dims.push_back(IsNonContiguous ? DimSize
                                                                    : 1);
          break;
        }
        llvm::Value *Size = getExprTypeSize(I->getAssociatedExpression());
        // Skip adding an entry in the CurInfo of this combined entry if the
        // whole struct is currently being mapped. The struct needs to be added
        // in the first position before any data internal to the struct is being
        // mapped.
        if (!IsMemberPointerOrAddr ||
            (Next == CE && MapType != OMPC_MAP_unknown)) {
          if (!IsMappingWholeStruct) {
            CombinedInfo.Exprs.emplace_back(MapDecl, MapExpr);
            CombinedInfo.BasePointers.push_back(BP.emitRawPointer(CGF));
            CombinedInfo.DevicePtrDecls.push_back(nullptr);
            CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
            CombinedInfo.Pointers.push_back(LB.emitRawPointer(CGF));
            CombinedInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
                Size, CGF.Int64Ty, /*isSigned=*/true));
            CombinedInfo.NonContigInfo.Dims.push_back(IsNonContiguous ? DimSize
                                                                      : 1);
          } else {
            StructBaseCombinedInfo.Exprs.emplace_back(MapDecl, MapExpr);
            StructBaseCombinedInfo.BasePointers.push_back(
                BP.emitRawPointer(CGF));
            StructBaseCombinedInfo.DevicePtrDecls.push_back(nullptr);
            StructBaseCombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
            StructBaseCombinedInfo.Pointers.push_back(LB.emitRawPointer(CGF));
            StructBaseCombinedInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
                Size, CGF.Int64Ty, /*isSigned=*/true));
            StructBaseCombinedInfo.NonContigInfo.Dims.push_back(
                IsNonContiguous ? DimSize : 1);
          }

          // If Mapper is valid, the last component inherits the mapper.
          bool HasMapper = Mapper && Next == CE;
          if (!IsMappingWholeStruct)
            CombinedInfo.Mappers.push_back(HasMapper ? Mapper : nullptr);
          else
            StructBaseCombinedInfo.Mappers.push_back(HasMapper ? Mapper
                                                               : nullptr);

          // We need to add a pointer flag for each map that comes from the
          // same expression except for the first one. We also need to signal
          // this map is the first one that relates with the current capture
          // (there is a set of entries for each capture).
          OpenMPOffloadMappingFlags Flags =
              getMapTypeBits(MapType, MapModifiers, MotionModifiers, IsImplicit,
                             !IsExpressionFirstInfo || RequiresReference ||
                                 FirstPointerInComplexData || IsMemberReference,
                             AreBothBasePtrAndPteeMapped ||
                                 (IsCaptureFirstInfo && !RequiresReference),
                             IsNonContiguous);

          if (!IsExpressionFirstInfo || IsMemberReference) {
            // If we have a PTR_AND_OBJ pair where the OBJ is a pointer as well,
            // then we reset the TO/FROM/ALWAYS/DELETE/CLOSE flags.
            if (IsPointer || (IsMemberReference && Next != CE))
              Flags &= ~(OpenMPOffloadMappingFlags::OMP_MAP_TO |
                         OpenMPOffloadMappingFlags::OMP_MAP_FROM |
                         OpenMPOffloadMappingFlags::OMP_MAP_ALWAYS |
                         OpenMPOffloadMappingFlags::OMP_MAP_DELETE |
                         OpenMPOffloadMappingFlags::OMP_MAP_CLOSE);

            if (ShouldBeMemberOf) {
              // Set placeholder value MEMBER_OF=FFFF to indicate that the flag
              // should be later updated with the correct value of MEMBER_OF.
              Flags |= OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF;
              // From now on, all subsequent PTR_AND_OBJ entries should not be
              // marked as MEMBER_OF.
              ShouldBeMemberOf = false;
            }
          }

          if (!IsMappingWholeStruct)
            CombinedInfo.Types.push_back(Flags);
          else
            StructBaseCombinedInfo.Types.push_back(Flags);
        }

        // If we have encountered a member expression so far, keep track of the
        // mapped member. If the parent is "*this", then the value declaration
        // is nullptr.
        if (EncounteredME) {
          const auto *FD = cast<FieldDecl>(EncounteredME->getMemberDecl());
          unsigned FieldIndex = FD->getFieldIndex();

          // Update info about the lowest and highest elements for this struct
          if (!PartialStruct.Base.isValid()) {
            PartialStruct.LowestElem = {FieldIndex, LowestElem};
            if (IsFinalArraySection) {
              Address HB =
                  CGF.EmitArraySectionExpr(OASE, /*IsLowerBound=*/false)
                      .getAddress();
              PartialStruct.HighestElem = {FieldIndex, HB};
            } else {
              PartialStruct.HighestElem = {FieldIndex, LowestElem};
            }
            PartialStruct.Base = BP;
            PartialStruct.LB = BP;
          } else if (FieldIndex < PartialStruct.LowestElem.first) {
            PartialStruct.LowestElem = {FieldIndex, LowestElem};
          } else if (FieldIndex > PartialStruct.HighestElem.first) {
            if (IsFinalArraySection) {
              Address HB =
                  CGF.EmitArraySectionExpr(OASE, /*IsLowerBound=*/false)
                      .getAddress();
              PartialStruct.HighestElem = {FieldIndex, HB};
            } else {
              PartialStruct.HighestElem = {FieldIndex, LowestElem};
            }
          }
        }

        // Need to emit combined struct for array sections.
        if (IsFinalArraySection || IsNonContiguous)
          PartialStruct.IsArraySection = true;

        // If we have a final array section, we are done with this expression.
        if (IsFinalArraySection)
          break;

        // The pointer becomes the base for the next element.
        if (Next != CE)
          BP = IsMemberReference ? LowestElem : LB;

        IsExpressionFirstInfo = false;
        IsCaptureFirstInfo = false;
        FirstPointerInComplexData = false;
        IsPrevMemberReference = IsMemberReference;
      } else if (FirstPointerInComplexData) {
        QualType Ty = Components.rbegin()
                          ->getAssociatedDeclaration()
                          ->getType()
                          .getNonReferenceType();
        BP = CGF.EmitLoadOfPointer(BP, Ty->castAs<PointerType>());
        FirstPointerInComplexData = false;
      }
    }
    // If ran into the whole component - allocate the space for the whole
    // record.
    if (!EncounteredME)
      PartialStruct.HasCompleteRecord = true;

    if (!IsNonContiguous)
      return;

    const ASTContext &Context = CGF.getContext();

    // For supporting stride in array section, we need to initialize the first
    // dimension size as 1, first offset as 0, and first count as 1
    MapValuesArrayTy CurOffsets = {llvm::ConstantInt::get(CGF.CGM.Int64Ty, 0)};
    MapValuesArrayTy CurCounts = {llvm::ConstantInt::get(CGF.CGM.Int64Ty, 1)};
    MapValuesArrayTy CurStrides;
    MapValuesArrayTy DimSizes{llvm::ConstantInt::get(CGF.CGM.Int64Ty, 1)};
    uint64_t ElementTypeSize;

    // Collect Size information for each dimension and get the element size as
    // the first Stride. For example, for `int arr[10][10]`, the DimSizes
    // should be [10, 10] and the first stride is 4 btyes.
    for (const OMPClauseMappableExprCommon::MappableComponent &Component :
         Components) {
      const Expr *AssocExpr = Component.getAssociatedExpression();
      const auto *OASE = dyn_cast<ArraySectionExpr>(AssocExpr);

      if (!OASE)
        continue;

      QualType Ty = ArraySectionExpr::getBaseOriginalType(OASE->getBase());
      auto *CAT = Context.getAsConstantArrayType(Ty);
      auto *VAT = Context.getAsVariableArrayType(Ty);

      // We need all the dimension size except for the last dimension.
      assert((VAT || CAT || &Component == &*Components.begin()) &&
             "Should be either ConstantArray or VariableArray if not the "
             "first Component");

      // Get element size if CurStrides is empty.
      if (CurStrides.empty()) {
        const Type *ElementType = nullptr;
        if (CAT)
          ElementType = CAT->getElementType().getTypePtr();
        else if (VAT)
          ElementType = VAT->getElementType().getTypePtr();
        else
          assert(&Component == &*Components.begin() &&
                 "Only expect pointer (non CAT or VAT) when this is the "
                 "first Component");
        // If ElementType is null, then it means the base is a pointer
        // (neither CAT nor VAT) and we'll attempt to get ElementType again
        // for next iteration.
        if (ElementType) {
          // For the case that having pointer as base, we need to remove one
          // level of indirection.
          if (&Component != &*Components.begin())
            ElementType = ElementType->getPointeeOrArrayElementType();
          ElementTypeSize =
              Context.getTypeSizeInChars(ElementType).getQuantity();
          CurStrides.push_back(
              llvm::ConstantInt::get(CGF.Int64Ty, ElementTypeSize));
        }
      }
      // Get dimension value except for the last dimension since we don't need
      // it.
      if (DimSizes.size() < Components.size() - 1) {
        if (CAT)
          DimSizes.push_back(
              llvm::ConstantInt::get(CGF.Int64Ty, CAT->getZExtSize()));
        else if (VAT)
          DimSizes.push_back(CGF.Builder.CreateIntCast(
              CGF.EmitScalarExpr(VAT->getSizeExpr()), CGF.Int64Ty,
              /*IsSigned=*/false));
      }
    }

    // Skip the dummy dimension since we have already have its information.
    auto *DI = DimSizes.begin() + 1;
    // Product of dimension.
    llvm::Value *DimProd =
        llvm::ConstantInt::get(CGF.CGM.Int64Ty, ElementTypeSize);

    // Collect info for non-contiguous. Notice that offset, count, and stride
    // are only meaningful for array-section, so we insert a null for anything
    // other than array-section.
    // Also, the size of offset, count, and stride are not the same as
    // pointers, base_pointers, sizes, or dims. Instead, the size of offset,
    // count, and stride are the same as the number of non-contiguous
    // declaration in target update to/from clause.
    for (const OMPClauseMappableExprCommon::MappableComponent &Component :
         Components) {
      const Expr *AssocExpr = Component.getAssociatedExpression();

      if (const auto *AE = dyn_cast<ArraySubscriptExpr>(AssocExpr)) {
        llvm::Value *Offset = CGF.Builder.CreateIntCast(
            CGF.EmitScalarExpr(AE->getIdx()), CGF.Int64Ty,
            /*isSigned=*/false);
        CurOffsets.push_back(Offset);
        CurCounts.push_back(llvm::ConstantInt::get(CGF.Int64Ty, /*V=*/1));
        CurStrides.push_back(CurStrides.back());
        continue;
      }

      const auto *OASE = dyn_cast<ArraySectionExpr>(AssocExpr);

      if (!OASE)
        continue;

      // Offset
      const Expr *OffsetExpr = OASE->getLowerBound();
      llvm::Value *Offset = nullptr;
      if (!OffsetExpr) {
        // If offset is absent, then we just set it to zero.
        Offset = llvm::ConstantInt::get(CGF.Int64Ty, 0);
      } else {
        Offset = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(OffsetExpr),
                                           CGF.Int64Ty,
                                           /*isSigned=*/false);
      }
      CurOffsets.push_back(Offset);

      // Count
      const Expr *CountExpr = OASE->getLength();
      llvm::Value *Count = nullptr;
      if (!CountExpr) {
        // In Clang, once a high dimension is an array section, we construct all
        // the lower dimension as array section, however, for case like
        // arr[0:2][2], Clang construct the inner dimension as an array section
        // but it actually is not in an array section form according to spec.
        if (!OASE->getColonLocFirst().isValid() &&
            !OASE->getColonLocSecond().isValid()) {
          Count = llvm::ConstantInt::get(CGF.Int64Ty, 1);
        } else {
          // OpenMP 5.0, 2.1.5 Array Sections, Description.
          // When the length is absent it defaults to ⌈(size −
          // lower-bound)/stride⌉, where size is the size of the array
          // dimension.
          const Expr *StrideExpr = OASE->getStride();
          llvm::Value *Stride =
              StrideExpr
                  ? CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(StrideExpr),
                                              CGF.Int64Ty, /*isSigned=*/false)
                  : nullptr;
          if (Stride)
            Count = CGF.Builder.CreateUDiv(
                CGF.Builder.CreateNUWSub(*DI, Offset), Stride);
          else
            Count = CGF.Builder.CreateNUWSub(*DI, Offset);
        }
      } else {
        Count = CGF.EmitScalarExpr(CountExpr);
      }
      Count = CGF.Builder.CreateIntCast(Count, CGF.Int64Ty, /*isSigned=*/false);
      CurCounts.push_back(Count);

      // Stride_n' = Stride_n * (D_0 * D_1 ... * D_n-1) * Unit size
      // Take `int arr[5][5][5]` and `arr[0:2:2][1:2:1][0:2:2]` as an example:
      //              Offset      Count     Stride
      //    D0          0           1         4    (int)    <- dummy dimension
      //    D1          0           2         8    (2 * (1) * 4)
      //    D2          1           2         20   (1 * (1 * 5) * 4)
      //    D3          0           2         200  (2 * (1 * 5 * 4) * 4)
      const Expr *StrideExpr = OASE->getStride();
      llvm::Value *Stride =
          StrideExpr
              ? CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(StrideExpr),
                                          CGF.Int64Ty, /*isSigned=*/false)
              : nullptr;
      DimProd = CGF.Builder.CreateNUWMul(DimProd, *(DI - 1));
      if (Stride)
        CurStrides.push_back(CGF.Builder.CreateNUWMul(DimProd, Stride));
      else
        CurStrides.push_back(DimProd);
      if (DI != DimSizes.end())
        ++DI;
    }

    CombinedInfo.NonContigInfo.Offsets.push_back(CurOffsets);
    CombinedInfo.NonContigInfo.Counts.push_back(CurCounts);
    CombinedInfo.NonContigInfo.Strides.push_back(CurStrides);
  }

  /// Return the adjusted map modifiers if the declaration a capture refers to
  /// appears in a first-private clause. This is expected to be used only with
  /// directives that start with 'target'.
  OpenMPOffloadMappingFlags
  getMapModifiersForPrivateClauses(const CapturedStmt::Capture &Cap) const {
    assert(Cap.capturesVariable() && "Expected capture by reference only!");

    // A first private variable captured by reference will use only the
    // 'private ptr' and 'map to' flag. Return the right flags if the captured
    // declaration is known as first-private in this handler.
    if (FirstPrivateDecls.count(Cap.getCapturedVar())) {
      if (Cap.getCapturedVar()->getType()->isAnyPointerType())
        return OpenMPOffloadMappingFlags::OMP_MAP_TO |
               OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ;
      return OpenMPOffloadMappingFlags::OMP_MAP_PRIVATE |
             OpenMPOffloadMappingFlags::OMP_MAP_TO;
    }
    auto I = LambdasMap.find(Cap.getCapturedVar()->getCanonicalDecl());
    if (I != LambdasMap.end())
      // for map(to: lambda): using user specified map type.
      return getMapTypeBits(
          I->getSecond()->getMapType(), I->getSecond()->getMapTypeModifiers(),
          /*MotionModifiers=*/std::nullopt, I->getSecond()->isImplicit(),
          /*AddPtrFlag=*/false,
          /*AddIsTargetParamFlag=*/false,
          /*isNonContiguous=*/false);
    return OpenMPOffloadMappingFlags::OMP_MAP_TO |
           OpenMPOffloadMappingFlags::OMP_MAP_FROM;
  }

  void getPlainLayout(const CXXRecordDecl *RD,
                      llvm::SmallVectorImpl<const FieldDecl *> &Layout,
                      bool AsBase) const {
    const CGRecordLayout &RL = CGF.getTypes().getCGRecordLayout(RD);

    llvm::StructType *St =
        AsBase ? RL.getBaseSubobjectLLVMType() : RL.getLLVMType();

    unsigned NumElements = St->getNumElements();
    llvm::SmallVector<
        llvm::PointerUnion<const CXXRecordDecl *, const FieldDecl *>, 4>
        RecordLayout(NumElements);

    // Fill bases.
    for (const auto &I : RD->bases()) {
      if (I.isVirtual())
        continue;

      QualType BaseTy = I.getType();
      const auto *Base = BaseTy->getAsCXXRecordDecl();
      // Ignore empty bases.
      if (isEmptyRecordForLayout(CGF.getContext(), BaseTy) ||
          CGF.getContext()
              .getASTRecordLayout(Base)
              .getNonVirtualSize()
              .isZero())
        continue;

      unsigned FieldIndex = RL.getNonVirtualBaseLLVMFieldNo(Base);
      RecordLayout[FieldIndex] = Base;
    }
    // Fill in virtual bases.
    for (const auto &I : RD->vbases()) {
      QualType BaseTy = I.getType();
      // Ignore empty bases.
      if (isEmptyRecordForLayout(CGF.getContext(), BaseTy))
        continue;

      const auto *Base = BaseTy->getAsCXXRecordDecl();
      unsigned FieldIndex = RL.getVirtualBaseIndex(Base);
      if (RecordLayout[FieldIndex])
        continue;
      RecordLayout[FieldIndex] = Base;
    }
    // Fill in all the fields.
    assert(!RD->isUnion() && "Unexpected union.");
    for (const auto *Field : RD->fields()) {
      // Fill in non-bitfields. (Bitfields always use a zero pattern, which we
      // will fill in later.)
      if (!Field->isBitField() &&
          !isEmptyFieldForLayout(CGF.getContext(), Field)) {
        unsigned FieldIndex = RL.getLLVMFieldNo(Field);
        RecordLayout[FieldIndex] = Field;
      }
    }
    for (const llvm::PointerUnion<const CXXRecordDecl *, const FieldDecl *>
             &Data : RecordLayout) {
      if (Data.isNull())
        continue;
      if (const auto *Base = Data.dyn_cast<const CXXRecordDecl *>())
        getPlainLayout(Base, Layout, /*AsBase=*/true);
      else
        Layout.push_back(Data.get<const FieldDecl *>());
    }
  }

  /// Generate all the base pointers, section pointers, sizes, map types, and
  /// mappers for the extracted mappable expressions (all included in \a
  /// CombinedInfo). Also, for each item that relates with a device pointer, a
  /// pair of the relevant declaration and index where it occurs is appended to
  /// the device pointers info array.
  void generateAllInfoForClauses(
      ArrayRef<const OMPClause *> Clauses, MapCombinedInfoTy &CombinedInfo,
      llvm::OpenMPIRBuilder &OMPBuilder,
      const llvm::DenseSet<CanonicalDeclPtr<const Decl>> &SkipVarSet =
          llvm::DenseSet<CanonicalDeclPtr<const Decl>>()) const {
    // We have to process the component lists that relate with the same
    // declaration in a single chunk so that we can generate the map flags
    // correctly. Therefore, we organize all lists in a map.
    enum MapKind { Present, Allocs, Other, Total };
    llvm::MapVector<CanonicalDeclPtr<const Decl>,
                    SmallVector<SmallVector<MapInfo, 8>, 4>>
        Info;

    // Helper function to fill the information map for the different supported
    // clauses.
    auto &&InfoGen =
        [&Info, &SkipVarSet](
            const ValueDecl *D, MapKind Kind,
            OMPClauseMappableExprCommon::MappableExprComponentListRef L,
            OpenMPMapClauseKind MapType,
            ArrayRef<OpenMPMapModifierKind> MapModifiers,
            ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
            bool ReturnDevicePointer, bool IsImplicit, const ValueDecl *Mapper,
            const Expr *VarRef = nullptr, bool ForDeviceAddr = false) {
          if (SkipVarSet.contains(D))
            return;
          auto It = Info.find(D);
          if (It == Info.end())
            It = Info
                     .insert(std::make_pair(
                         D, SmallVector<SmallVector<MapInfo, 8>, 4>(Total)))
                     .first;
          It->second[Kind].emplace_back(
              L, MapType, MapModifiers, MotionModifiers, ReturnDevicePointer,
              IsImplicit, Mapper, VarRef, ForDeviceAddr);
        };

    for (const auto *Cl : Clauses) {
      const auto *C = dyn_cast<OMPMapClause>(Cl);
      if (!C)
        continue;
      MapKind Kind = Other;
      if (llvm::is_contained(C->getMapTypeModifiers(),
                             OMPC_MAP_MODIFIER_present))
        Kind = Present;
      else if (C->getMapType() == OMPC_MAP_alloc)
        Kind = Allocs;
      const auto *EI = C->getVarRefs().begin();
      for (const auto L : C->component_lists()) {
        const Expr *E = (C->getMapLoc().isValid()) ? *EI : nullptr;
        InfoGen(std::get<0>(L), Kind, std::get<1>(L), C->getMapType(),
                C->getMapTypeModifiers(), std::nullopt,
                /*ReturnDevicePointer=*/false, C->isImplicit(), std::get<2>(L),
                E);
        ++EI;
      }
    }
    for (const auto *Cl : Clauses) {
      const auto *C = dyn_cast<OMPToClause>(Cl);
      if (!C)
        continue;
      MapKind Kind = Other;
      if (llvm::is_contained(C->getMotionModifiers(),
                             OMPC_MOTION_MODIFIER_present))
        Kind = Present;
      const auto *EI = C->getVarRefs().begin();
      for (const auto L : C->component_lists()) {
        InfoGen(std::get<0>(L), Kind, std::get<1>(L), OMPC_MAP_to, std::nullopt,
                C->getMotionModifiers(), /*ReturnDevicePointer=*/false,
                C->isImplicit(), std::get<2>(L), *EI);
        ++EI;
      }
    }
    for (const auto *Cl : Clauses) {
      const auto *C = dyn_cast<OMPFromClause>(Cl);
      if (!C)
        continue;
      MapKind Kind = Other;
      if (llvm::is_contained(C->getMotionModifiers(),
                             OMPC_MOTION_MODIFIER_present))
        Kind = Present;
      const auto *EI = C->getVarRefs().begin();
      for (const auto L : C->component_lists()) {
        InfoGen(std::get<0>(L), Kind, std::get<1>(L), OMPC_MAP_from,
                std::nullopt, C->getMotionModifiers(),
                /*ReturnDevicePointer=*/false, C->isImplicit(), std::get<2>(L),
                *EI);
        ++EI;
      }
    }

    // Look at the use_device_ptr and use_device_addr clauses information and
    // mark the existing map entries as such. If there is no map information for
    // an entry in the use_device_ptr and use_device_addr list, we create one
    // with map type 'alloc' and zero size section. It is the user fault if that
    // was not mapped before. If there is no map information and the pointer is
    // a struct member, then we defer the emission of that entry until the whole
    // struct has been processed.
    llvm::MapVector<CanonicalDeclPtr<const Decl>,
                    SmallVector<DeferredDevicePtrEntryTy, 4>>
        DeferredInfo;
    MapCombinedInfoTy UseDeviceDataCombinedInfo;

    auto &&UseDeviceDataCombinedInfoGen =
        [&UseDeviceDataCombinedInfo](const ValueDecl *VD, llvm::Value *Ptr,
                                     CodeGenFunction &CGF, bool IsDevAddr) {
          UseDeviceDataCombinedInfo.Exprs.push_back(VD);
          UseDeviceDataCombinedInfo.BasePointers.emplace_back(Ptr);
          UseDeviceDataCombinedInfo.DevicePtrDecls.emplace_back(VD);
          UseDeviceDataCombinedInfo.DevicePointers.emplace_back(
              IsDevAddr ? DeviceInfoTy::Address : DeviceInfoTy::Pointer);
          UseDeviceDataCombinedInfo.Pointers.push_back(Ptr);
          UseDeviceDataCombinedInfo.Sizes.push_back(
              llvm::Constant::getNullValue(CGF.Int64Ty));
          UseDeviceDataCombinedInfo.Types.push_back(
              OpenMPOffloadMappingFlags::OMP_MAP_RETURN_PARAM);
          UseDeviceDataCombinedInfo.Mappers.push_back(nullptr);
        };

    auto &&MapInfoGen =
        [&DeferredInfo, &UseDeviceDataCombinedInfoGen,
         &InfoGen](CodeGenFunction &CGF, const Expr *IE, const ValueDecl *VD,
                   OMPClauseMappableExprCommon::MappableExprComponentListRef
                       Components,
                   bool IsImplicit, bool IsDevAddr) {
          // We didn't find any match in our map information - generate a zero
          // size array section - if the pointer is a struct member we defer
          // this action until the whole struct has been processed.
          if (isa<MemberExpr>(IE)) {
            // Insert the pointer into Info to be processed by
            // generateInfoForComponentList. Because it is a member pointer
            // without a pointee, no entry will be generated for it, therefore
            // we need to generate one after the whole struct has been
            // processed. Nonetheless, generateInfoForComponentList must be
            // called to take the pointer into account for the calculation of
            // the range of the partial struct.
            InfoGen(nullptr, Other, Components, OMPC_MAP_unknown, std::nullopt,
                    std::nullopt, /*ReturnDevicePointer=*/false, IsImplicit,
                    nullptr, nullptr, IsDevAddr);
            DeferredInfo[nullptr].emplace_back(IE, VD, IsDevAddr);
          } else {
            llvm::Value *Ptr;
            if (IsDevAddr) {
              if (IE->isGLValue())
                Ptr = CGF.EmitLValue(IE).getPointer(CGF);
              else
                Ptr = CGF.EmitScalarExpr(IE);
            } else {
              Ptr = CGF.EmitLoadOfScalar(CGF.EmitLValue(IE), IE->getExprLoc());
            }
            UseDeviceDataCombinedInfoGen(VD, Ptr, CGF, IsDevAddr);
          }
        };

    auto &&IsMapInfoExist = [&Info](CodeGenFunction &CGF, const ValueDecl *VD,
                                    const Expr *IE, bool IsDevAddr) -> bool {
      // We potentially have map information for this declaration already.
      // Look for the first set of components that refer to it. If found,
      // return true.
      // If the first component is a member expression, we have to look into
      // 'this', which maps to null in the map of map information. Otherwise
      // look directly for the information.
      auto It = Info.find(isa<MemberExpr>(IE) ? nullptr : VD);
      if (It != Info.end()) {
        bool Found = false;
        for (auto &Data : It->second) {
          auto *CI = llvm::find_if(Data, [VD](const MapInfo &MI) {
            return MI.Components.back().getAssociatedDeclaration() == VD;
          });
          // If we found a map entry, signal that the pointer has to be
          // returned and move on to the next declaration. Exclude cases where
          // the base pointer is mapped as array subscript, array section or
          // array shaping. The base address is passed as a pointer to base in
          // this case and cannot be used as a base for use_device_ptr list
          // item.
          if (CI != Data.end()) {
            if (IsDevAddr) {
              CI->ForDeviceAddr = IsDevAddr;
              CI->ReturnDevicePointer = true;
              Found = true;
              break;
            } else {
              auto PrevCI = std::next(CI->Components.rbegin());
              const auto *VarD = dyn_cast<VarDecl>(VD);
              if (CGF.CGM.getOpenMPRuntime().hasRequiresUnifiedSharedMemory() ||
                  isa<MemberExpr>(IE) ||
                  !VD->getType().getNonReferenceType()->isPointerType() ||
                  PrevCI == CI->Components.rend() ||
                  isa<MemberExpr>(PrevCI->getAssociatedExpression()) || !VarD ||
                  VarD->hasLocalStorage()) {
                CI->ForDeviceAddr = IsDevAddr;
                CI->ReturnDevicePointer = true;
                Found = true;
                break;
              }
            }
          }
        }
        return Found;
      }
      return false;
    };

    // Look at the use_device_ptr clause information and mark the existing map
    // entries as such. If there is no map information for an entry in the
    // use_device_ptr list, we create one with map type 'alloc' and zero size
    // section. It is the user fault if that was not mapped before. If there is
    // no map information and the pointer is a struct member, then we defer the
    // emission of that entry until the whole struct has been processed.
    for (const auto *Cl : Clauses) {
      const auto *C = dyn_cast<OMPUseDevicePtrClause>(Cl);
      if (!C)
        continue;
      for (const auto L : C->component_lists()) {
        OMPClauseMappableExprCommon::MappableExprComponentListRef Components =
            std::get<1>(L);
        assert(!Components.empty() &&
               "Not expecting empty list of components!");
        const ValueDecl *VD = Components.back().getAssociatedDeclaration();
        VD = cast<ValueDecl>(VD->getCanonicalDecl());
        const Expr *IE = Components.back().getAssociatedExpression();
        if (IsMapInfoExist(CGF, VD, IE, /*IsDevAddr=*/false))
          continue;
        MapInfoGen(CGF, IE, VD, Components, C->isImplicit(),
                   /*IsDevAddr=*/false);
      }
    }

    llvm::SmallDenseSet<CanonicalDeclPtr<const Decl>, 4> Processed;
    for (const auto *Cl : Clauses) {
      const auto *C = dyn_cast<OMPUseDeviceAddrClause>(Cl);
      if (!C)
        continue;
      for (const auto L : C->component_lists()) {
        OMPClauseMappableExprCommon::MappableExprComponentListRef Components =
            std::get<1>(L);
        assert(!std::get<1>(L).empty() &&
               "Not expecting empty list of components!");
        const ValueDecl *VD = std::get<1>(L).back().getAssociatedDeclaration();
        if (!Processed.insert(VD).second)
          continue;
        VD = cast<ValueDecl>(VD->getCanonicalDecl());
        const Expr *IE = std::get<1>(L).back().getAssociatedExpression();
        if (IsMapInfoExist(CGF, VD, IE, /*IsDevAddr=*/true))
          continue;
        MapInfoGen(CGF, IE, VD, Components, C->isImplicit(),
                   /*IsDevAddr=*/true);
      }
    }

    for (const auto &Data : Info) {
      StructRangeInfoTy PartialStruct;
      // Current struct information:
      MapCombinedInfoTy CurInfo;
      // Current struct base information:
      MapCombinedInfoTy StructBaseCurInfo;
      const Decl *D = Data.first;
      const ValueDecl *VD = cast_or_null<ValueDecl>(D);
      bool HasMapBasePtr = false;
      bool HasMapArraySec = false;
      if (VD && VD->getType()->isAnyPointerType()) {
        for (const auto &M : Data.second) {
          HasMapBasePtr = any_of(M, [](const MapInfo &L) {
            return isa_and_present<DeclRefExpr>(L.VarRef);
          });
          HasMapArraySec = any_of(M, [](const MapInfo &L) {
            return isa_and_present<ArraySectionExpr, ArraySubscriptExpr>(
                L.VarRef);
          });
          if (HasMapBasePtr && HasMapArraySec)
            break;
        }
      }
      for (const auto &M : Data.second) {
        for (const MapInfo &L : M) {
          assert(!L.Components.empty() &&
                 "Not expecting declaration with no component lists.");

          // Remember the current base pointer index.
          unsigned CurrentBasePointersIdx = CurInfo.BasePointers.size();
          unsigned StructBasePointersIdx =
              StructBaseCurInfo.BasePointers.size();
          CurInfo.NonContigInfo.IsNonContiguous =
              L.Components.back().isNonContiguous();
          generateInfoForComponentList(
              L.MapType, L.MapModifiers, L.MotionModifiers, L.Components,
              CurInfo, StructBaseCurInfo, PartialStruct,
              /*IsFirstComponentList=*/false, L.IsImplicit,
              /*GenerateAllInfoForClauses*/ true, L.Mapper, L.ForDeviceAddr, VD,
              L.VarRef, /*OverlappedElements*/ std::nullopt,
              HasMapBasePtr && HasMapArraySec);

          // If this entry relates to a device pointer, set the relevant
          // declaration and add the 'return pointer' flag.
          if (L.ReturnDevicePointer) {
            // Check whether a value was added to either CurInfo or
            // StructBaseCurInfo and error if no value was added to either of
            // them:
            assert((CurrentBasePointersIdx < CurInfo.BasePointers.size() ||
                    StructBasePointersIdx <
                        StructBaseCurInfo.BasePointers.size()) &&
                   "Unexpected number of mapped base pointers.");

            // Choose a base pointer index which is always valid:
            const ValueDecl *RelevantVD =
                L.Components.back().getAssociatedDeclaration();
            assert(RelevantVD &&
                   "No relevant declaration related with device pointer??");

            // If StructBaseCurInfo has been updated this iteration then work on
            // the first new entry added to it i.e. make sure that when multiple
            // values are added to any of the lists, the first value added is
            // being modified by the assignments below (not the last value
            // added).
            if (StructBasePointersIdx < StructBaseCurInfo.BasePointers.size()) {
              StructBaseCurInfo.DevicePtrDecls[StructBasePointersIdx] =
                  RelevantVD;
              StructBaseCurInfo.DevicePointers[StructBasePointersIdx] =
                  L.ForDeviceAddr ? DeviceInfoTy::Address
                                  : DeviceInfoTy::Pointer;
              StructBaseCurInfo.Types[StructBasePointersIdx] |=
                  OpenMPOffloadMappingFlags::OMP_MAP_RETURN_PARAM;
            } else {
              CurInfo.DevicePtrDecls[CurrentBasePointersIdx] = RelevantVD;
              CurInfo.DevicePointers[CurrentBasePointersIdx] =
                  L.ForDeviceAddr ? DeviceInfoTy::Address
                                  : DeviceInfoTy::Pointer;
              CurInfo.Types[CurrentBasePointersIdx] |=
                  OpenMPOffloadMappingFlags::OMP_MAP_RETURN_PARAM;
            }
          }
        }
      }

      // Append any pending zero-length pointers which are struct members and
      // used with use_device_ptr or use_device_addr.
      auto CI = DeferredInfo.find(Data.first);
      if (CI != DeferredInfo.end()) {
        for (const DeferredDevicePtrEntryTy &L : CI->second) {
          llvm::Value *BasePtr;
          llvm::Value *Ptr;
          if (L.ForDeviceAddr) {
            if (L.IE->isGLValue())
              Ptr = this->CGF.EmitLValue(L.IE).getPointer(CGF);
            else
              Ptr = this->CGF.EmitScalarExpr(L.IE);
            BasePtr = Ptr;
            // Entry is RETURN_PARAM. Also, set the placeholder value
            // MEMBER_OF=FFFF so that the entry is later updated with the
            // correct value of MEMBER_OF.
            CurInfo.Types.push_back(
                OpenMPOffloadMappingFlags::OMP_MAP_RETURN_PARAM |
                OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF);
          } else {
            BasePtr = this->CGF.EmitLValue(L.IE).getPointer(CGF);
            Ptr = this->CGF.EmitLoadOfScalar(this->CGF.EmitLValue(L.IE),
                                             L.IE->getExprLoc());
            // Entry is PTR_AND_OBJ and RETURN_PARAM. Also, set the
            // placeholder value MEMBER_OF=FFFF so that the entry is later
            // updated with the correct value of MEMBER_OF.
            CurInfo.Types.push_back(
                OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ |
                OpenMPOffloadMappingFlags::OMP_MAP_RETURN_PARAM |
                OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF);
          }
          CurInfo.Exprs.push_back(L.VD);
          CurInfo.BasePointers.emplace_back(BasePtr);
          CurInfo.DevicePtrDecls.emplace_back(L.VD);
          CurInfo.DevicePointers.emplace_back(
              L.ForDeviceAddr ? DeviceInfoTy::Address : DeviceInfoTy::Pointer);
          CurInfo.Pointers.push_back(Ptr);
          CurInfo.Sizes.push_back(
              llvm::Constant::getNullValue(this->CGF.Int64Ty));
          CurInfo.Mappers.push_back(nullptr);
        }
      }

      // Unify entries in one list making sure the struct mapping precedes the
      // individual fields:
      MapCombinedInfoTy UnionCurInfo;
      UnionCurInfo.append(StructBaseCurInfo);
      UnionCurInfo.append(CurInfo);

      // If there is an entry in PartialStruct it means we have a struct with
      // individual members mapped. Emit an extra combined entry.
      if (PartialStruct.Base.isValid()) {
        UnionCurInfo.NonContigInfo.Dims.push_back(0);
        // Emit a combined entry:
        emitCombinedEntry(CombinedInfo, UnionCurInfo.Types, PartialStruct,
                          /*IsMapThis*/ !VD, OMPBuilder, VD);
      }

      // We need to append the results of this capture to what we already have.
      CombinedInfo.append(UnionCurInfo);
    }
    // Append data for use_device_ptr clauses.
    CombinedInfo.append(UseDeviceDataCombinedInfo);
  }

public:
  MappableExprsHandler(const OMPExecutableDirective &Dir, CodeGenFunction &CGF)
      : CurDir(&Dir), CGF(CGF) {
    // Extract firstprivate clause information.
    for (const auto *C : Dir.getClausesOfKind<OMPFirstprivateClause>())
      for (const auto *D : C->varlists())
        FirstPrivateDecls.try_emplace(
            cast<VarDecl>(cast<DeclRefExpr>(D)->getDecl()), C->isImplicit());
    // Extract implicit firstprivates from uses_allocators clauses.
    for (const auto *C : Dir.getClausesOfKind<OMPUsesAllocatorsClause>()) {
      for (unsigned I = 0, E = C->getNumberOfAllocators(); I < E; ++I) {
        OMPUsesAllocatorsClause::Data D = C->getAllocatorData(I);
        if (const auto *DRE = dyn_cast_or_null<DeclRefExpr>(D.AllocatorTraits))
          FirstPrivateDecls.try_emplace(cast<VarDecl>(DRE->getDecl()),
                                        /*Implicit=*/true);
        else if (const auto *VD = dyn_cast<VarDecl>(
                     cast<DeclRefExpr>(D.Allocator->IgnoreParenImpCasts())
                         ->getDecl()))
          FirstPrivateDecls.try_emplace(VD, /*Implicit=*/true);
      }
    }
    // Extract device pointer clause information.
    for (const auto *C : Dir.getClausesOfKind<OMPIsDevicePtrClause>())
      for (auto L : C->component_lists())
        DevPointersMap[std::get<0>(L)].push_back(std::get<1>(L));
    // Extract device addr clause information.
    for (const auto *C : Dir.getClausesOfKind<OMPHasDeviceAddrClause>())
      for (auto L : C->component_lists())
        HasDevAddrsMap[std::get<0>(L)].push_back(std::get<1>(L));
    // Extract map information.
    for (const auto *C : Dir.getClausesOfKind<OMPMapClause>()) {
      if (C->getMapType() != OMPC_MAP_to)
        continue;
      for (auto L : C->component_lists()) {
        const ValueDecl *VD = std::get<0>(L);
        const auto *RD = VD ? VD->getType()
                                  .getCanonicalType()
                                  .getNonReferenceType()
                                  ->getAsCXXRecordDecl()
                            : nullptr;
        if (RD && RD->isLambda())
          LambdasMap.try_emplace(std::get<0>(L), C);
      }
    }
  }

  /// Constructor for the declare mapper directive.
  MappableExprsHandler(const OMPDeclareMapperDecl &Dir, CodeGenFunction &CGF)
      : CurDir(&Dir), CGF(CGF) {}

  /// Generate code for the combined entry if we have a partially mapped struct
  /// and take care of the mapping flags of the arguments corresponding to
  /// individual struct members.
  void emitCombinedEntry(MapCombinedInfoTy &CombinedInfo,
                         MapFlagsArrayTy &CurTypes,
                         const StructRangeInfoTy &PartialStruct, bool IsMapThis,
                         llvm::OpenMPIRBuilder &OMPBuilder,
                         const ValueDecl *VD = nullptr,
                         bool NotTargetParams = true) const {
    if (CurTypes.size() == 1 &&
        ((CurTypes.back() & OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF) !=
         OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF) &&
        !PartialStruct.IsArraySection)
      return;
    Address LBAddr = PartialStruct.LowestElem.second;
    Address HBAddr = PartialStruct.HighestElem.second;
    if (PartialStruct.HasCompleteRecord) {
      LBAddr = PartialStruct.LB;
      HBAddr = PartialStruct.LB;
    }
    CombinedInfo.Exprs.push_back(VD);
    // Base is the base of the struct
    CombinedInfo.BasePointers.push_back(PartialStruct.Base.emitRawPointer(CGF));
    CombinedInfo.DevicePtrDecls.push_back(nullptr);
    CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
    // Pointer is the address of the lowest element
    llvm::Value *LB = LBAddr.emitRawPointer(CGF);
    const CXXMethodDecl *MD =
        CGF.CurFuncDecl ? dyn_cast<CXXMethodDecl>(CGF.CurFuncDecl) : nullptr;
    const CXXRecordDecl *RD = MD ? MD->getParent() : nullptr;
    bool HasBaseClass = RD && IsMapThis ? RD->getNumBases() > 0 : false;
    // There should not be a mapper for a combined entry.
    if (HasBaseClass) {
      // OpenMP 5.2 148:21:
      // If the target construct is within a class non-static member function,
      // and a variable is an accessible data member of the object for which the
      // non-static data member function is invoked, the variable is treated as
      // if the this[:1] expression had appeared in a map clause with a map-type
      // of tofrom.
      // Emit this[:1]
      CombinedInfo.Pointers.push_back(PartialStruct.Base.emitRawPointer(CGF));
      QualType Ty = MD->getFunctionObjectParameterType();
      llvm::Value *Size =
          CGF.Builder.CreateIntCast(CGF.getTypeSize(Ty), CGF.Int64Ty,
                                    /*isSigned=*/true);
      CombinedInfo.Sizes.push_back(Size);
    } else {
      CombinedInfo.Pointers.push_back(LB);
      // Size is (addr of {highest+1} element) - (addr of lowest element)
      llvm::Value *HB = HBAddr.emitRawPointer(CGF);
      llvm::Value *HAddr = CGF.Builder.CreateConstGEP1_32(
          HBAddr.getElementType(), HB, /*Idx0=*/1);
      llvm::Value *CLAddr = CGF.Builder.CreatePointerCast(LB, CGF.VoidPtrTy);
      llvm::Value *CHAddr = CGF.Builder.CreatePointerCast(HAddr, CGF.VoidPtrTy);
      llvm::Value *Diff = CGF.Builder.CreatePtrDiff(CGF.Int8Ty, CHAddr, CLAddr);
      llvm::Value *Size = CGF.Builder.CreateIntCast(Diff, CGF.Int64Ty,
                                                    /*isSigned=*/false);
      CombinedInfo.Sizes.push_back(Size);
    }
    CombinedInfo.Mappers.push_back(nullptr);
    // Map type is always TARGET_PARAM, if generate info for captures.
    CombinedInfo.Types.push_back(
        NotTargetParams ? OpenMPOffloadMappingFlags::OMP_MAP_NONE
                        : OpenMPOffloadMappingFlags::OMP_MAP_TARGET_PARAM);
    // If any element has the present modifier, then make sure the runtime
    // doesn't attempt to allocate the struct.
    if (CurTypes.end() !=
        llvm::find_if(CurTypes, [](OpenMPOffloadMappingFlags Type) {
          return static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
              Type & OpenMPOffloadMappingFlags::OMP_MAP_PRESENT);
        }))
      CombinedInfo.Types.back() |= OpenMPOffloadMappingFlags::OMP_MAP_PRESENT;
    // Remove TARGET_PARAM flag from the first element
    (*CurTypes.begin()) &= ~OpenMPOffloadMappingFlags::OMP_MAP_TARGET_PARAM;
    // If any element has the ompx_hold modifier, then make sure the runtime
    // uses the hold reference count for the struct as a whole so that it won't
    // be unmapped by an extra dynamic reference count decrement.  Add it to all
    // elements as well so the runtime knows which reference count to check
    // when determining whether it's time for device-to-host transfers of
    // individual elements.
    if (CurTypes.end() !=
        llvm::find_if(CurTypes, [](OpenMPOffloadMappingFlags Type) {
          return static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
              Type & OpenMPOffloadMappingFlags::OMP_MAP_OMPX_HOLD);
        })) {
      CombinedInfo.Types.back() |= OpenMPOffloadMappingFlags::OMP_MAP_OMPX_HOLD;
      for (auto &M : CurTypes)
        M |= OpenMPOffloadMappingFlags::OMP_MAP_OMPX_HOLD;
    }

    // All other current entries will be MEMBER_OF the combined entry
    // (except for PTR_AND_OBJ entries which do not have a placeholder value
    // 0xFFFF in the MEMBER_OF field).
    OpenMPOffloadMappingFlags MemberOfFlag =
        OMPBuilder.getMemberOfFlag(CombinedInfo.BasePointers.size() - 1);
    for (auto &M : CurTypes)
      OMPBuilder.setCorrectMemberOfFlag(M, MemberOfFlag);
  }

  /// Generate all the base pointers, section pointers, sizes, map types, and
  /// mappers for the extracted mappable expressions (all included in \a
  /// CombinedInfo). Also, for each item that relates with a device pointer, a
  /// pair of the relevant declaration and index where it occurs is appended to
  /// the device pointers info array.
  void generateAllInfo(
      MapCombinedInfoTy &CombinedInfo, llvm::OpenMPIRBuilder &OMPBuilder,
      const llvm::DenseSet<CanonicalDeclPtr<const Decl>> &SkipVarSet =
          llvm::DenseSet<CanonicalDeclPtr<const Decl>>()) const {
    assert(CurDir.is<const OMPExecutableDirective *>() &&
           "Expect a executable directive");
    const auto *CurExecDir = CurDir.get<const OMPExecutableDirective *>();
    generateAllInfoForClauses(CurExecDir->clauses(), CombinedInfo, OMPBuilder,
                              SkipVarSet);
  }

  /// Generate all the base pointers, section pointers, sizes, map types, and
  /// mappers for the extracted map clauses of user-defined mapper (all included
  /// in \a CombinedInfo).
  void generateAllInfoForMapper(MapCombinedInfoTy &CombinedInfo,
                                llvm::OpenMPIRBuilder &OMPBuilder) const {
    assert(CurDir.is<const OMPDeclareMapperDecl *>() &&
           "Expect a declare mapper directive");
    const auto *CurMapperDir = CurDir.get<const OMPDeclareMapperDecl *>();
    generateAllInfoForClauses(CurMapperDir->clauses(), CombinedInfo,
                              OMPBuilder);
  }

  /// Emit capture info for lambdas for variables captured by reference.
  void generateInfoForLambdaCaptures(
      const ValueDecl *VD, llvm::Value *Arg, MapCombinedInfoTy &CombinedInfo,
      llvm::DenseMap<llvm::Value *, llvm::Value *> &LambdaPointers) const {
    QualType VDType = VD->getType().getCanonicalType().getNonReferenceType();
    const auto *RD = VDType->getAsCXXRecordDecl();
    if (!RD || !RD->isLambda())
      return;
    Address VDAddr(Arg, CGF.ConvertTypeForMem(VDType),
                   CGF.getContext().getDeclAlign(VD));
    LValue VDLVal = CGF.MakeAddrLValue(VDAddr, VDType);
    llvm::DenseMap<const ValueDecl *, FieldDecl *> Captures;
    FieldDecl *ThisCapture = nullptr;
    RD->getCaptureFields(Captures, ThisCapture);
    if (ThisCapture) {
      LValue ThisLVal =
          CGF.EmitLValueForFieldInitialization(VDLVal, ThisCapture);
      LValue ThisLValVal = CGF.EmitLValueForField(VDLVal, ThisCapture);
      LambdaPointers.try_emplace(ThisLVal.getPointer(CGF),
                                 VDLVal.getPointer(CGF));
      CombinedInfo.Exprs.push_back(VD);
      CombinedInfo.BasePointers.push_back(ThisLVal.getPointer(CGF));
      CombinedInfo.DevicePtrDecls.push_back(nullptr);
      CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
      CombinedInfo.Pointers.push_back(ThisLValVal.getPointer(CGF));
      CombinedInfo.Sizes.push_back(
          CGF.Builder.CreateIntCast(CGF.getTypeSize(CGF.getContext().VoidPtrTy),
                                    CGF.Int64Ty, /*isSigned=*/true));
      CombinedInfo.Types.push_back(
          OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ |
          OpenMPOffloadMappingFlags::OMP_MAP_LITERAL |
          OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF |
          OpenMPOffloadMappingFlags::OMP_MAP_IMPLICIT);
      CombinedInfo.Mappers.push_back(nullptr);
    }
    for (const LambdaCapture &LC : RD->captures()) {
      if (!LC.capturesVariable())
        continue;
      const VarDecl *VD = cast<VarDecl>(LC.getCapturedVar());
      if (LC.getCaptureKind() != LCK_ByRef && !VD->getType()->isPointerType())
        continue;
      auto It = Captures.find(VD);
      assert(It != Captures.end() && "Found lambda capture without field.");
      LValue VarLVal = CGF.EmitLValueForFieldInitialization(VDLVal, It->second);
      if (LC.getCaptureKind() == LCK_ByRef) {
        LValue VarLValVal = CGF.EmitLValueForField(VDLVal, It->second);
        LambdaPointers.try_emplace(VarLVal.getPointer(CGF),
                                   VDLVal.getPointer(CGF));
        CombinedInfo.Exprs.push_back(VD);
        CombinedInfo.BasePointers.push_back(VarLVal.getPointer(CGF));
        CombinedInfo.DevicePtrDecls.push_back(nullptr);
        CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
        CombinedInfo.Pointers.push_back(VarLValVal.getPointer(CGF));
        CombinedInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
            CGF.getTypeSize(
                VD->getType().getCanonicalType().getNonReferenceType()),
            CGF.Int64Ty, /*isSigned=*/true));
      } else {
        RValue VarRVal = CGF.EmitLoadOfLValue(VarLVal, RD->getLocation());
        LambdaPointers.try_emplace(VarLVal.getPointer(CGF),
                                   VDLVal.getPointer(CGF));
        CombinedInfo.Exprs.push_back(VD);
        CombinedInfo.BasePointers.push_back(VarLVal.getPointer(CGF));
        CombinedInfo.DevicePtrDecls.push_back(nullptr);
        CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
        CombinedInfo.Pointers.push_back(VarRVal.getScalarVal());
        CombinedInfo.Sizes.push_back(llvm::ConstantInt::get(CGF.Int64Ty, 0));
      }
      CombinedInfo.Types.push_back(
          OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ |
          OpenMPOffloadMappingFlags::OMP_MAP_LITERAL |
          OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF |
          OpenMPOffloadMappingFlags::OMP_MAP_IMPLICIT);
      CombinedInfo.Mappers.push_back(nullptr);
    }
  }

  /// Set correct indices for lambdas captures.
  void adjustMemberOfForLambdaCaptures(
      llvm::OpenMPIRBuilder &OMPBuilder,
      const llvm::DenseMap<llvm::Value *, llvm::Value *> &LambdaPointers,
      MapBaseValuesArrayTy &BasePointers, MapValuesArrayTy &Pointers,
      MapFlagsArrayTy &Types) const {
    for (unsigned I = 0, E = Types.size(); I < E; ++I) {
      // Set correct member_of idx for all implicit lambda captures.
      if (Types[I] != (OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ |
                       OpenMPOffloadMappingFlags::OMP_MAP_LITERAL |
                       OpenMPOffloadMappingFlags::OMP_MAP_MEMBER_OF |
                       OpenMPOffloadMappingFlags::OMP_MAP_IMPLICIT))
        continue;
      llvm::Value *BasePtr = LambdaPointers.lookup(BasePointers[I]);
      assert(BasePtr && "Unable to find base lambda address.");
      int TgtIdx = -1;
      for (unsigned J = I; J > 0; --J) {
        unsigned Idx = J - 1;
        if (Pointers[Idx] != BasePtr)
          continue;
        TgtIdx = Idx;
        break;
      }
      assert(TgtIdx != -1 && "Unable to find parent lambda.");
      // All other current entries will be MEMBER_OF the combined entry
      // (except for PTR_AND_OBJ entries which do not have a placeholder value
      // 0xFFFF in the MEMBER_OF field).
      OpenMPOffloadMappingFlags MemberOfFlag =
          OMPBuilder.getMemberOfFlag(TgtIdx);
      OMPBuilder.setCorrectMemberOfFlag(Types[I], MemberOfFlag);
    }
  }

  /// Generate the base pointers, section pointers, sizes, map types, and
  /// mappers associated to a given capture (all included in \a CombinedInfo).
  void generateInfoForCapture(const CapturedStmt::Capture *Cap,
                              llvm::Value *Arg, MapCombinedInfoTy &CombinedInfo,
                              StructRangeInfoTy &PartialStruct) const {
    assert(!Cap->capturesVariableArrayType() &&
           "Not expecting to generate map info for a variable array type!");

    // We need to know when we generating information for the first component
    const ValueDecl *VD = Cap->capturesThis()
                              ? nullptr
                              : Cap->getCapturedVar()->getCanonicalDecl();

    // for map(to: lambda): skip here, processing it in
    // generateDefaultMapInfo
    if (LambdasMap.count(VD))
      return;

    // If this declaration appears in a is_device_ptr clause we just have to
    // pass the pointer by value. If it is a reference to a declaration, we just
    // pass its value.
    if (VD && (DevPointersMap.count(VD) || HasDevAddrsMap.count(VD))) {
      CombinedInfo.Exprs.push_back(VD);
      CombinedInfo.BasePointers.emplace_back(Arg);
      CombinedInfo.DevicePtrDecls.emplace_back(VD);
      CombinedInfo.DevicePointers.emplace_back(DeviceInfoTy::Pointer);
      CombinedInfo.Pointers.push_back(Arg);
      CombinedInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
          CGF.getTypeSize(CGF.getContext().VoidPtrTy), CGF.Int64Ty,
          /*isSigned=*/true));
      CombinedInfo.Types.push_back(
          OpenMPOffloadMappingFlags::OMP_MAP_LITERAL |
          OpenMPOffloadMappingFlags::OMP_MAP_TARGET_PARAM);
      CombinedInfo.Mappers.push_back(nullptr);
      return;
    }

    using MapData =
        std::tuple<OMPClauseMappableExprCommon::MappableExprComponentListRef,
                   OpenMPMapClauseKind, ArrayRef<OpenMPMapModifierKind>, bool,
                   const ValueDecl *, const Expr *>;
    SmallVector<MapData, 4> DeclComponentLists;
    // For member fields list in is_device_ptr, store it in
    // DeclComponentLists for generating components info.
    static const OpenMPMapModifierKind Unknown = OMPC_MAP_MODIFIER_unknown;
    auto It = DevPointersMap.find(VD);
    if (It != DevPointersMap.end())
      for (const auto &MCL : It->second)
        DeclComponentLists.emplace_back(MCL, OMPC_MAP_to, Unknown,
                                        /*IsImpicit = */ true, nullptr,
                                        nullptr);
    auto I = HasDevAddrsMap.find(VD);
    if (I != HasDevAddrsMap.end())
      for (const auto &MCL : I->second)
        DeclComponentLists.emplace_back(MCL, OMPC_MAP_tofrom, Unknown,
                                        /*IsImpicit = */ true, nullptr,
                                        nullptr);
    assert(CurDir.is<const OMPExecutableDirective *>() &&
           "Expect a executable directive");
    const auto *CurExecDir = CurDir.get<const OMPExecutableDirective *>();
    bool HasMapBasePtr = false;
    bool HasMapArraySec = false;
    for (const auto *C : CurExecDir->getClausesOfKind<OMPMapClause>()) {
      const auto *EI = C->getVarRefs().begin();
      for (const auto L : C->decl_component_lists(VD)) {
        const ValueDecl *VDecl, *Mapper;
        // The Expression is not correct if the mapping is implicit
        const Expr *E = (C->getMapLoc().isValid()) ? *EI : nullptr;
        OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
        std::tie(VDecl, Components, Mapper) = L;
        assert(VDecl == VD && "We got information for the wrong declaration??");
        assert(!Components.empty() &&
               "Not expecting declaration with no component lists.");
        if (VD && E && VD->getType()->isAnyPointerType() && isa<DeclRefExpr>(E))
          HasMapBasePtr = true;
        if (VD && E && VD->getType()->isAnyPointerType() &&
            (isa<ArraySectionExpr>(E) || isa<ArraySubscriptExpr>(E)))
          HasMapArraySec = true;
        DeclComponentLists.emplace_back(Components, C->getMapType(),
                                        C->getMapTypeModifiers(),
                                        C->isImplicit(), Mapper, E);
        ++EI;
      }
    }
    llvm::stable_sort(DeclComponentLists, [](const MapData &LHS,
                                             const MapData &RHS) {
      ArrayRef<OpenMPMapModifierKind> MapModifiers = std::get<2>(LHS);
      OpenMPMapClauseKind MapType = std::get<1>(RHS);
      bool HasPresent =
          llvm::is_contained(MapModifiers, clang::OMPC_MAP_MODIFIER_present);
      bool HasAllocs = MapType == OMPC_MAP_alloc;
      MapModifiers = std::get<2>(RHS);
      MapType = std::get<1>(LHS);
      bool HasPresentR =
          llvm::is_contained(MapModifiers, clang::OMPC_MAP_MODIFIER_present);
      bool HasAllocsR = MapType == OMPC_MAP_alloc;
      return (HasPresent && !HasPresentR) || (HasAllocs && !HasAllocsR);
    });

    // Find overlapping elements (including the offset from the base element).
    llvm::SmallDenseMap<
        const MapData *,
        llvm::SmallVector<
            OMPClauseMappableExprCommon::MappableExprComponentListRef, 4>,
        4>
        OverlappedData;
    size_t Count = 0;
    for (const MapData &L : DeclComponentLists) {
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
      OpenMPMapClauseKind MapType;
      ArrayRef<OpenMPMapModifierKind> MapModifiers;
      bool IsImplicit;
      const ValueDecl *Mapper;
      const Expr *VarRef;
      std::tie(Components, MapType, MapModifiers, IsImplicit, Mapper, VarRef) =
          L;
      ++Count;
      for (const MapData &L1 : ArrayRef(DeclComponentLists).slice(Count)) {
        OMPClauseMappableExprCommon::MappableExprComponentListRef Components1;
        std::tie(Components1, MapType, MapModifiers, IsImplicit, Mapper,
                 VarRef) = L1;
        auto CI = Components.rbegin();
        auto CE = Components.rend();
        auto SI = Components1.rbegin();
        auto SE = Components1.rend();
        for (; CI != CE && SI != SE; ++CI, ++SI) {
          if (CI->getAssociatedExpression()->getStmtClass() !=
              SI->getAssociatedExpression()->getStmtClass())
            break;
          // Are we dealing with different variables/fields?
          if (CI->getAssociatedDeclaration() != SI->getAssociatedDeclaration())
            break;
        }
        // Found overlapping if, at least for one component, reached the head
        // of the components list.
        if (CI == CE || SI == SE) {
          // Ignore it if it is the same component.
          if (CI == CE && SI == SE)
            continue;
          const auto It = (SI == SE) ? CI : SI;
          // If one component is a pointer and another one is a kind of
          // dereference of this pointer (array subscript, section, dereference,
          // etc.), it is not an overlapping.
          // Same, if one component is a base and another component is a
          // dereferenced pointer memberexpr with the same base.
          if (!isa<MemberExpr>(It->getAssociatedExpression()) ||
              (std::prev(It)->getAssociatedDeclaration() &&
               std::prev(It)
                   ->getAssociatedDeclaration()
                   ->getType()
                   ->isPointerType()) ||
              (It->getAssociatedDeclaration() &&
               It->getAssociatedDeclaration()->getType()->isPointerType() &&
               std::next(It) != CE && std::next(It) != SE))
            continue;
          const MapData &BaseData = CI == CE ? L : L1;
          OMPClauseMappableExprCommon::MappableExprComponentListRef SubData =
              SI == SE ? Components : Components1;
          auto &OverlappedElements = OverlappedData.FindAndConstruct(&BaseData);
          OverlappedElements.getSecond().push_back(SubData);
        }
      }
    }
    // Sort the overlapped elements for each item.
    llvm::SmallVector<const FieldDecl *, 4> Layout;
    if (!OverlappedData.empty()) {
      const Type *BaseType = VD->getType().getCanonicalType().getTypePtr();
      const Type *OrigType = BaseType->getPointeeOrArrayElementType();
      while (BaseType != OrigType) {
        BaseType = OrigType->getCanonicalTypeInternal().getTypePtr();
        OrigType = BaseType->getPointeeOrArrayElementType();
      }

      if (const auto *CRD = BaseType->getAsCXXRecordDecl())
        getPlainLayout(CRD, Layout, /*AsBase=*/false);
      else {
        const auto *RD = BaseType->getAsRecordDecl();
        Layout.append(RD->field_begin(), RD->field_end());
      }
    }
    for (auto &Pair : OverlappedData) {
      llvm::stable_sort(
          Pair.getSecond(),
          [&Layout](
              OMPClauseMappableExprCommon::MappableExprComponentListRef First,
              OMPClauseMappableExprCommon::MappableExprComponentListRef
                  Second) {
            auto CI = First.rbegin();
            auto CE = First.rend();
            auto SI = Second.rbegin();
            auto SE = Second.rend();
            for (; CI != CE && SI != SE; ++CI, ++SI) {
              if (CI->getAssociatedExpression()->getStmtClass() !=
                  SI->getAssociatedExpression()->getStmtClass())
                break;
              // Are we dealing with different variables/fields?
              if (CI->getAssociatedDeclaration() !=
                  SI->getAssociatedDeclaration())
                break;
            }

            // Lists contain the same elements.
            if (CI == CE && SI == SE)
              return false;

            // List with less elements is less than list with more elements.
            if (CI == CE || SI == SE)
              return CI == CE;

            const auto *FD1 = cast<FieldDecl>(CI->getAssociatedDeclaration());
            const auto *FD2 = cast<FieldDecl>(SI->getAssociatedDeclaration());
            if (FD1->getParent() == FD2->getParent())
              return FD1->getFieldIndex() < FD2->getFieldIndex();
            const auto *It =
                llvm::find_if(Layout, [FD1, FD2](const FieldDecl *FD) {
                  return FD == FD1 || FD == FD2;
                });
            return *It == FD1;
          });
    }

    // Associated with a capture, because the mapping flags depend on it.
    // Go through all of the elements with the overlapped elements.
    bool IsFirstComponentList = true;
    MapCombinedInfoTy StructBaseCombinedInfo;
    for (const auto &Pair : OverlappedData) {
      const MapData &L = *Pair.getFirst();
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
      OpenMPMapClauseKind MapType;
      ArrayRef<OpenMPMapModifierKind> MapModifiers;
      bool IsImplicit;
      const ValueDecl *Mapper;
      const Expr *VarRef;
      std::tie(Components, MapType, MapModifiers, IsImplicit, Mapper, VarRef) =
          L;
      ArrayRef<OMPClauseMappableExprCommon::MappableExprComponentListRef>
          OverlappedComponents = Pair.getSecond();
      generateInfoForComponentList(
          MapType, MapModifiers, std::nullopt, Components, CombinedInfo,
          StructBaseCombinedInfo, PartialStruct, IsFirstComponentList,
          IsImplicit, /*GenerateAllInfoForClauses*/ false, Mapper,
          /*ForDeviceAddr=*/false, VD, VarRef, OverlappedComponents);
      IsFirstComponentList = false;
    }
    // Go through other elements without overlapped elements.
    for (const MapData &L : DeclComponentLists) {
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
      OpenMPMapClauseKind MapType;
      ArrayRef<OpenMPMapModifierKind> MapModifiers;
      bool IsImplicit;
      const ValueDecl *Mapper;
      const Expr *VarRef;
      std::tie(Components, MapType, MapModifiers, IsImplicit, Mapper, VarRef) =
          L;
      auto It = OverlappedData.find(&L);
      if (It == OverlappedData.end())
        generateInfoForComponentList(
            MapType, MapModifiers, std::nullopt, Components, CombinedInfo,
            StructBaseCombinedInfo, PartialStruct, IsFirstComponentList,
            IsImplicit, /*GenerateAllInfoForClauses*/ false, Mapper,
            /*ForDeviceAddr=*/false, VD, VarRef,
            /*OverlappedElements*/ std::nullopt,
            HasMapBasePtr && HasMapArraySec);
      IsFirstComponentList = false;
    }
  }

  /// Generate the default map information for a given capture \a CI,
  /// record field declaration \a RI and captured value \a CV.
  void generateDefaultMapInfo(const CapturedStmt::Capture &CI,
                              const FieldDecl &RI, llvm::Value *CV,
                              MapCombinedInfoTy &CombinedInfo) const {
    bool IsImplicit = true;
    // Do the default mapping.
    if (CI.capturesThis()) {
      CombinedInfo.Exprs.push_back(nullptr);
      CombinedInfo.BasePointers.push_back(CV);
      CombinedInfo.DevicePtrDecls.push_back(nullptr);
      CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
      CombinedInfo.Pointers.push_back(CV);
      const auto *PtrTy = cast<PointerType>(RI.getType().getTypePtr());
      CombinedInfo.Sizes.push_back(
          CGF.Builder.CreateIntCast(CGF.getTypeSize(PtrTy->getPointeeType()),
                                    CGF.Int64Ty, /*isSigned=*/true));
      // Default map type.
      CombinedInfo.Types.push_back(OpenMPOffloadMappingFlags::OMP_MAP_TO |
                                   OpenMPOffloadMappingFlags::OMP_MAP_FROM);
    } else if (CI.capturesVariableByCopy()) {
      const VarDecl *VD = CI.getCapturedVar();
      CombinedInfo.Exprs.push_back(VD->getCanonicalDecl());
      CombinedInfo.BasePointers.push_back(CV);
      CombinedInfo.DevicePtrDecls.push_back(nullptr);
      CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
      CombinedInfo.Pointers.push_back(CV);
      if (!RI.getType()->isAnyPointerType()) {
        // We have to signal to the runtime captures passed by value that are
        // not pointers.
        CombinedInfo.Types.push_back(
            OpenMPOffloadMappingFlags::OMP_MAP_LITERAL);
        CombinedInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
            CGF.getTypeSize(RI.getType()), CGF.Int64Ty, /*isSigned=*/true));
      } else {
        // Pointers are implicitly mapped with a zero size and no flags
        // (other than first map that is added for all implicit maps).
        CombinedInfo.Types.push_back(OpenMPOffloadMappingFlags::OMP_MAP_NONE);
        CombinedInfo.Sizes.push_back(llvm::Constant::getNullValue(CGF.Int64Ty));
      }
      auto I = FirstPrivateDecls.find(VD);
      if (I != FirstPrivateDecls.end())
        IsImplicit = I->getSecond();
    } else {
      assert(CI.capturesVariable() && "Expected captured reference.");
      const auto *PtrTy = cast<ReferenceType>(RI.getType().getTypePtr());
      QualType ElementType = PtrTy->getPointeeType();
      CombinedInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
          CGF.getTypeSize(ElementType), CGF.Int64Ty, /*isSigned=*/true));
      // The default map type for a scalar/complex type is 'to' because by
      // default the value doesn't have to be retrieved. For an aggregate
      // type, the default is 'tofrom'.
      CombinedInfo.Types.push_back(getMapModifiersForPrivateClauses(CI));
      const VarDecl *VD = CI.getCapturedVar();
      auto I = FirstPrivateDecls.find(VD);
      CombinedInfo.Exprs.push_back(VD->getCanonicalDecl());
      CombinedInfo.BasePointers.push_back(CV);
      CombinedInfo.DevicePtrDecls.push_back(nullptr);
      CombinedInfo.DevicePointers.push_back(DeviceInfoTy::None);
      if (I != FirstPrivateDecls.end() && ElementType->isAnyPointerType()) {
        Address PtrAddr = CGF.EmitLoadOfReference(CGF.MakeAddrLValue(
            CV, ElementType, CGF.getContext().getDeclAlign(VD),
            AlignmentSource::Decl));
        CombinedInfo.Pointers.push_back(PtrAddr.emitRawPointer(CGF));
      } else {
        CombinedInfo.Pointers.push_back(CV);
      }
      if (I != FirstPrivateDecls.end())
        IsImplicit = I->getSecond();
    }
    // Every default map produces a single argument which is a target parameter.
    CombinedInfo.Types.back() |=
        OpenMPOffloadMappingFlags::OMP_MAP_TARGET_PARAM;

    // Add flag stating this is an implicit map.
    if (IsImplicit)
      CombinedInfo.Types.back() |= OpenMPOffloadMappingFlags::OMP_MAP_IMPLICIT;

    // No user-defined mapper for default mapping.
    CombinedInfo.Mappers.push_back(nullptr);
  }
};
} // anonymous namespace

// Try to extract the base declaration from a `this->x` expression if possible.
static ValueDecl *getDeclFromThisExpr(const Expr *E) {
  if (!E)
    return nullptr;

  if (const auto *OASE = dyn_cast<ArraySectionExpr>(E->IgnoreParenCasts()))
    if (const MemberExpr *ME =
            dyn_cast<MemberExpr>(OASE->getBase()->IgnoreParenImpCasts()))
      return ME->getMemberDecl();
  return nullptr;
}

/// Emit a string constant containing the names of the values mapped to the
/// offloading runtime library.
llvm::Constant *
emitMappingInformation(CodeGenFunction &CGF, llvm::OpenMPIRBuilder &OMPBuilder,
                       MappableExprsHandler::MappingExprInfo &MapExprs) {

  uint32_t SrcLocStrSize;
  if (!MapExprs.getMapDecl() && !MapExprs.getMapExpr())
    return OMPBuilder.getOrCreateDefaultSrcLocStr(SrcLocStrSize);

  SourceLocation Loc;
  if (!MapExprs.getMapDecl() && MapExprs.getMapExpr()) {
    if (const ValueDecl *VD = getDeclFromThisExpr(MapExprs.getMapExpr()))
      Loc = VD->getLocation();
    else
      Loc = MapExprs.getMapExpr()->getExprLoc();
  } else {
    Loc = MapExprs.getMapDecl()->getLocation();
  }

  std::string ExprName;
  if (MapExprs.getMapExpr()) {
    PrintingPolicy P(CGF.getContext().getLangOpts());
    llvm::raw_string_ostream OS(ExprName);
    MapExprs.getMapExpr()->printPretty(OS, nullptr, P);
    OS.flush();
  } else {
    ExprName = MapExprs.getMapDecl()->getNameAsString();
  }

  PresumedLoc PLoc = CGF.getContext().getSourceManager().getPresumedLoc(Loc);
  return OMPBuilder.getOrCreateSrcLocStr(PLoc.getFilename(), ExprName,
                                         PLoc.getLine(), PLoc.getColumn(),
                                         SrcLocStrSize);
}

/// Emit the arrays used to pass the captures and map information to the
/// offloading runtime library. If there is no map or capture information,
/// return nullptr by reference.
static void emitOffloadingArrays(
    CodeGenFunction &CGF, MappableExprsHandler::MapCombinedInfoTy &CombinedInfo,
    CGOpenMPRuntime::TargetDataInfo &Info, llvm::OpenMPIRBuilder &OMPBuilder,
    bool IsNonContiguous = false) {
  CodeGenModule &CGM = CGF.CGM;

  // Reset the array information.
  Info.clearArrayInfo();
  Info.NumberOfPtrs = CombinedInfo.BasePointers.size();

  using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;
  InsertPointTy AllocaIP(CGF.AllocaInsertPt->getParent(),
                         CGF.AllocaInsertPt->getIterator());
  InsertPointTy CodeGenIP(CGF.Builder.GetInsertBlock(),
                          CGF.Builder.GetInsertPoint());

  auto FillInfoMap = [&](MappableExprsHandler::MappingExprInfo &MapExpr) {
    return emitMappingInformation(CGF, OMPBuilder, MapExpr);
  };
  if (CGM.getCodeGenOpts().getDebugInfo() !=
      llvm::codegenoptions::NoDebugInfo) {
    CombinedInfo.Names.resize(CombinedInfo.Exprs.size());
    llvm::transform(CombinedInfo.Exprs, CombinedInfo.Names.begin(),
                    FillInfoMap);
  }

  auto DeviceAddrCB = [&](unsigned int I, llvm::Value *NewDecl) {
    if (const ValueDecl *DevVD = CombinedInfo.DevicePtrDecls[I]) {
      Info.CaptureDeviceAddrMap.try_emplace(DevVD, NewDecl);
    }
  };

  auto CustomMapperCB = [&](unsigned int I) {
    llvm::Value *MFunc = nullptr;
    if (CombinedInfo.Mappers[I]) {
      Info.HasMapper = true;
      MFunc = CGF.CGM.getOpenMPRuntime().getOrCreateUserDefinedMapperFunc(
          cast<OMPDeclareMapperDecl>(CombinedInfo.Mappers[I]));
    }
    return MFunc;
  };
  OMPBuilder.emitOffloadingArrays(AllocaIP, CodeGenIP, CombinedInfo, Info,
                                  /*IsNonContiguous=*/true, DeviceAddrCB,
                                  CustomMapperCB);
}

/// Check for inner distribute directive.
static const OMPExecutableDirective *
getNestedDistributeDirective(ASTContext &Ctx, const OMPExecutableDirective &D) {
  const auto *CS = D.getInnermostCapturedStmt();
  const auto *Body =
      CS->getCapturedStmt()->IgnoreContainers(/*IgnoreCaptured=*/true);
  const Stmt *ChildStmt =
      CGOpenMPSIMDRuntime::getSingleCompoundChild(Ctx, Body);

  if (const auto *NestedDir =
          dyn_cast_or_null<OMPExecutableDirective>(ChildStmt)) {
    OpenMPDirectiveKind DKind = NestedDir->getDirectiveKind();
    switch (D.getDirectiveKind()) {
    case OMPD_target:
      // For now, treat 'target' with nested 'teams loop' as if it's
      // distributed (target teams distribute).
      if (isOpenMPDistributeDirective(DKind) || DKind == OMPD_teams_loop)
        return NestedDir;
      if (DKind == OMPD_teams) {
        Body = NestedDir->getInnermostCapturedStmt()->IgnoreContainers(
            /*IgnoreCaptured=*/true);
        if (!Body)
          return nullptr;
        ChildStmt = CGOpenMPSIMDRuntime::getSingleCompoundChild(Ctx, Body);
        if (const auto *NND =
                dyn_cast_or_null<OMPExecutableDirective>(ChildStmt)) {
          DKind = NND->getDirectiveKind();
          if (isOpenMPDistributeDirective(DKind))
            return NND;
        }
      }
      return nullptr;
    case OMPD_target_teams:
      if (isOpenMPDistributeDirective(DKind))
        return NestedDir;
      return nullptr;
    case OMPD_target_parallel:
    case OMPD_target_simd:
    case OMPD_target_parallel_for:
    case OMPD_target_parallel_for_simd:
      return nullptr;
    case OMPD_target_teams_distribute:
    case OMPD_target_teams_distribute_simd:
    case OMPD_target_teams_distribute_parallel_for:
    case OMPD_target_teams_distribute_parallel_for_simd:
    case OMPD_parallel:
    case OMPD_for:
    case OMPD_parallel_for:
    case OMPD_parallel_master:
    case OMPD_parallel_sections:
    case OMPD_for_simd:
    case OMPD_parallel_for_simd:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_ordered:
    case OMPD_threadprivate:
    case OMPD_allocate:
    case OMPD_task:
    case OMPD_simd:
    case OMPD_tile:
    case OMPD_unroll:
    case OMPD_sections:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_taskyield:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_atomic:
    case OMPD_flush:
    case OMPD_depobj:
    case OMPD_scan:
    case OMPD_teams:
    case OMPD_target_data:
    case OMPD_target_exit_data:
    case OMPD_target_enter_data:
    case OMPD_distribute:
    case OMPD_distribute_simd:
    case OMPD_distribute_parallel_for:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_target_update:
    case OMPD_declare_simd:
    case OMPD_declare_variant:
    case OMPD_begin_declare_variant:
    case OMPD_end_declare_variant:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_declare_reduction:
    case OMPD_declare_mapper:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_master_taskloop:
    case OMPD_master_taskloop_simd:
    case OMPD_parallel_master_taskloop:
    case OMPD_parallel_master_taskloop_simd:
    case OMPD_requires:
    case OMPD_metadirective:
    case OMPD_unknown:
    default:
      llvm_unreachable("Unexpected directive.");
    }
  }

  return nullptr;
}

/// Emit the user-defined mapper function. The code generation follows the
/// pattern in the example below.
/// \code
/// void .omp_mapper.<type_name>.<mapper_id>.(void *rt_mapper_handle,
///                                           void *base, void *begin,
///                                           int64_t size, int64_t type,
///                                           void *name = nullptr) {
///   // Allocate space for an array section first or add a base/begin for
///   // pointer dereference.
///   if ((size > 1 || (base != begin && maptype.IsPtrAndObj)) &&
///       !maptype.IsDelete)
///     __tgt_push_mapper_component(rt_mapper_handle, base, begin,
///                                 size*sizeof(Ty), clearToFromMember(type));
///   // Map members.
///   for (unsigned i = 0; i < size; i++) {
///     // For each component specified by this mapper:
///     for (auto c : begin[i]->all_components) {
///       if (c.hasMapper())
///         (*c.Mapper())(rt_mapper_handle, c.arg_base, c.arg_begin, c.arg_size,
///                       c.arg_type, c.arg_name);
///       else
///         __tgt_push_mapper_component(rt_mapper_handle, c.arg_base,
///                                     c.arg_begin, c.arg_size, c.arg_type,
///                                     c.arg_name);
///     }
///   }
///   // Delete the array section.
///   if (size > 1 && maptype.IsDelete)
///     __tgt_push_mapper_component(rt_mapper_handle, base, begin,
///                                 size*sizeof(Ty), clearToFromMember(type));
/// }
/// \endcode
void CGOpenMPRuntime::emitUserDefinedMapper(const OMPDeclareMapperDecl *D,
                                            CodeGenFunction *CGF) {
  if (UDMMap.count(D) > 0)
    return;
  ASTContext &C = CGM.getContext();
  QualType Ty = D->getType();
  QualType PtrTy = C.getPointerType(Ty).withRestrict();
  QualType Int64Ty = C.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/true);
  auto *MapperVarDecl =
      cast<VarDecl>(cast<DeclRefExpr>(D->getMapperVarRef())->getDecl());
  SourceLocation Loc = D->getLocation();
  CharUnits ElementSize = C.getTypeSizeInChars(Ty);
  llvm::Type *ElemTy = CGM.getTypes().ConvertTypeForMem(Ty);

  // Prepare mapper function arguments and attributes.
  ImplicitParamDecl HandleArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                              C.VoidPtrTy, ImplicitParamKind::Other);
  ImplicitParamDecl BaseArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                            ImplicitParamKind::Other);
  ImplicitParamDecl BeginArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                             C.VoidPtrTy, ImplicitParamKind::Other);
  ImplicitParamDecl SizeArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, Int64Ty,
                            ImplicitParamKind::Other);
  ImplicitParamDecl TypeArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, Int64Ty,
                            ImplicitParamKind::Other);
  ImplicitParamDecl NameArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                            ImplicitParamKind::Other);
  FunctionArgList Args;
  Args.push_back(&HandleArg);
  Args.push_back(&BaseArg);
  Args.push_back(&BeginArg);
  Args.push_back(&SizeArg);
  Args.push_back(&TypeArg);
  Args.push_back(&NameArg);
  const CGFunctionInfo &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  SmallString<64> TyStr;
  llvm::raw_svector_ostream Out(TyStr);
  CGM.getCXXABI().getMangleContext().mangleCanonicalTypeName(Ty, Out);
  std::string Name = getName({"omp_mapper", TyStr, D->getName()});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->removeFnAttr(llvm::Attribute::OptimizeNone);
  // Start the mapper function code generation.
  CodeGenFunction MapperCGF(CGM);
  MapperCGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, Loc, Loc);
  // Compute the starting and end addresses of array elements.
  llvm::Value *Size = MapperCGF.EmitLoadOfScalar(
      MapperCGF.GetAddrOfLocalVar(&SizeArg), /*Volatile=*/false,
      C.getPointerType(Int64Ty), Loc);
  // Prepare common arguments for array initiation and deletion.
  llvm::Value *Handle = MapperCGF.EmitLoadOfScalar(
      MapperCGF.GetAddrOfLocalVar(&HandleArg),
      /*Volatile=*/false, C.getPointerType(C.VoidPtrTy), Loc);
  llvm::Value *BaseIn = MapperCGF.EmitLoadOfScalar(
      MapperCGF.GetAddrOfLocalVar(&BaseArg),
      /*Volatile=*/false, C.getPointerType(C.VoidPtrTy), Loc);
  llvm::Value *BeginIn = MapperCGF.EmitLoadOfScalar(
      MapperCGF.GetAddrOfLocalVar(&BeginArg),
      /*Volatile=*/false, C.getPointerType(C.VoidPtrTy), Loc);
  // Convert the size in bytes into the number of array elements.
  Size = MapperCGF.Builder.CreateExactUDiv(
      Size, MapperCGF.Builder.getInt64(ElementSize.getQuantity()));
  llvm::Value *PtrBegin = MapperCGF.Builder.CreateBitCast(
      BeginIn, CGM.getTypes().ConvertTypeForMem(PtrTy));
  llvm::Value *PtrEnd = MapperCGF.Builder.CreateGEP(ElemTy, PtrBegin, Size);
  llvm::Value *MapType = MapperCGF.EmitLoadOfScalar(
      MapperCGF.GetAddrOfLocalVar(&TypeArg), /*Volatile=*/false,
      C.getPointerType(Int64Ty), Loc);
  llvm::Value *MapName = MapperCGF.EmitLoadOfScalar(
      MapperCGF.GetAddrOfLocalVar(&NameArg),
      /*Volatile=*/false, C.getPointerType(C.VoidPtrTy), Loc);

  // Emit array initiation if this is an array section and \p MapType indicates
  // that memory allocation is required.
  llvm::BasicBlock *HeadBB = MapperCGF.createBasicBlock("omp.arraymap.head");
  emitUDMapperArrayInitOrDel(MapperCGF, Handle, BaseIn, BeginIn, Size, MapType,
                             MapName, ElementSize, HeadBB, /*IsInit=*/true);

  // Emit a for loop to iterate through SizeArg of elements and map all of them.

  // Emit the loop header block.
  MapperCGF.EmitBlock(HeadBB);
  llvm::BasicBlock *BodyBB = MapperCGF.createBasicBlock("omp.arraymap.body");
  llvm::BasicBlock *DoneBB = MapperCGF.createBasicBlock("omp.done");
  // Evaluate whether the initial condition is satisfied.
  llvm::Value *IsEmpty =
      MapperCGF.Builder.CreateICmpEQ(PtrBegin, PtrEnd, "omp.arraymap.isempty");
  MapperCGF.Builder.CreateCondBr(IsEmpty, DoneBB, BodyBB);
  llvm::BasicBlock *EntryBB = MapperCGF.Builder.GetInsertBlock();

  // Emit the loop body block.
  MapperCGF.EmitBlock(BodyBB);
  llvm::BasicBlock *LastBB = BodyBB;
  llvm::PHINode *PtrPHI = MapperCGF.Builder.CreatePHI(
      PtrBegin->getType(), 2, "omp.arraymap.ptrcurrent");
  PtrPHI->addIncoming(PtrBegin, EntryBB);
  Address PtrCurrent(PtrPHI, ElemTy,
                     MapperCGF.GetAddrOfLocalVar(&BeginArg)
                         .getAlignment()
                         .alignmentOfArrayElement(ElementSize));
  // Privatize the declared variable of mapper to be the current array element.
  CodeGenFunction::OMPPrivateScope Scope(MapperCGF);
  Scope.addPrivate(MapperVarDecl, PtrCurrent);
  (void)Scope.Privatize();

  // Get map clause information. Fill up the arrays with all mapped variables.
  MappableExprsHandler::MapCombinedInfoTy Info;
  MappableExprsHandler MEHandler(*D, MapperCGF);
  MEHandler.generateAllInfoForMapper(Info, OMPBuilder);

  // Call the runtime API __tgt_mapper_num_components to get the number of
  // pre-existing components.
  llvm::Value *OffloadingArgs[] = {Handle};
  llvm::Value *PreviousSize = MapperCGF.EmitRuntimeCall(
      OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                            OMPRTL___tgt_mapper_num_components),
      OffloadingArgs);
  llvm::Value *ShiftedPreviousSize = MapperCGF.Builder.CreateShl(
      PreviousSize,
      MapperCGF.Builder.getInt64(MappableExprsHandler::getFlagMemberOffset()));

  // Fill up the runtime mapper handle for all components.
  for (unsigned I = 0; I < Info.BasePointers.size(); ++I) {
    llvm::Value *CurBaseArg = MapperCGF.Builder.CreateBitCast(
        Info.BasePointers[I], CGM.getTypes().ConvertTypeForMem(C.VoidPtrTy));
    llvm::Value *CurBeginArg = MapperCGF.Builder.CreateBitCast(
        Info.Pointers[I], CGM.getTypes().ConvertTypeForMem(C.VoidPtrTy));
    llvm::Value *CurSizeArg = Info.Sizes[I];
    llvm::Value *CurNameArg =
        (CGM.getCodeGenOpts().getDebugInfo() ==
         llvm::codegenoptions::NoDebugInfo)
            ? llvm::ConstantPointerNull::get(CGM.VoidPtrTy)
            : emitMappingInformation(MapperCGF, OMPBuilder, Info.Exprs[I]);

    // Extract the MEMBER_OF field from the map type.
    llvm::Value *OriMapType = MapperCGF.Builder.getInt64(
        static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
            Info.Types[I]));
    llvm::Value *MemberMapType =
        MapperCGF.Builder.CreateNUWAdd(OriMapType, ShiftedPreviousSize);

    // Combine the map type inherited from user-defined mapper with that
    // specified in the program. According to the OMP_MAP_TO and OMP_MAP_FROM
    // bits of the \a MapType, which is the input argument of the mapper
    // function, the following code will set the OMP_MAP_TO and OMP_MAP_FROM
    // bits of MemberMapType.
    // [OpenMP 5.0], 1.2.6. map-type decay.
    //        | alloc |  to   | from  | tofrom | release | delete
    // ----------------------------------------------------------
    // alloc  | alloc | alloc | alloc | alloc  | release | delete
    // to     | alloc |  to   | alloc |   to   | release | delete
    // from   | alloc | alloc | from  |  from  | release | delete
    // tofrom | alloc |  to   | from  | tofrom | release | delete
    llvm::Value *LeftToFrom = MapperCGF.Builder.CreateAnd(
        MapType,
        MapperCGF.Builder.getInt64(
            static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                OpenMPOffloadMappingFlags::OMP_MAP_TO |
                OpenMPOffloadMappingFlags::OMP_MAP_FROM)));
    llvm::BasicBlock *AllocBB = MapperCGF.createBasicBlock("omp.type.alloc");
    llvm::BasicBlock *AllocElseBB =
        MapperCGF.createBasicBlock("omp.type.alloc.else");
    llvm::BasicBlock *ToBB = MapperCGF.createBasicBlock("omp.type.to");
    llvm::BasicBlock *ToElseBB = MapperCGF.createBasicBlock("omp.type.to.else");
    llvm::BasicBlock *FromBB = MapperCGF.createBasicBlock("omp.type.from");
    llvm::BasicBlock *EndBB = MapperCGF.createBasicBlock("omp.type.end");
    llvm::Value *IsAlloc = MapperCGF.Builder.CreateIsNull(LeftToFrom);
    MapperCGF.Builder.CreateCondBr(IsAlloc, AllocBB, AllocElseBB);
    // In case of alloc, clear OMP_MAP_TO and OMP_MAP_FROM.
    MapperCGF.EmitBlock(AllocBB);
    llvm::Value *AllocMapType = MapperCGF.Builder.CreateAnd(
        MemberMapType,
        MapperCGF.Builder.getInt64(
            ~static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                OpenMPOffloadMappingFlags::OMP_MAP_TO |
                OpenMPOffloadMappingFlags::OMP_MAP_FROM)));
    MapperCGF.Builder.CreateBr(EndBB);
    MapperCGF.EmitBlock(AllocElseBB);
    llvm::Value *IsTo = MapperCGF.Builder.CreateICmpEQ(
        LeftToFrom,
        MapperCGF.Builder.getInt64(
            static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                OpenMPOffloadMappingFlags::OMP_MAP_TO)));
    MapperCGF.Builder.CreateCondBr(IsTo, ToBB, ToElseBB);
    // In case of to, clear OMP_MAP_FROM.
    MapperCGF.EmitBlock(ToBB);
    llvm::Value *ToMapType = MapperCGF.Builder.CreateAnd(
        MemberMapType,
        MapperCGF.Builder.getInt64(
            ~static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                OpenMPOffloadMappingFlags::OMP_MAP_FROM)));
    MapperCGF.Builder.CreateBr(EndBB);
    MapperCGF.EmitBlock(ToElseBB);
    llvm::Value *IsFrom = MapperCGF.Builder.CreateICmpEQ(
        LeftToFrom,
        MapperCGF.Builder.getInt64(
            static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                OpenMPOffloadMappingFlags::OMP_MAP_FROM)));
    MapperCGF.Builder.CreateCondBr(IsFrom, FromBB, EndBB);
    // In case of from, clear OMP_MAP_TO.
    MapperCGF.EmitBlock(FromBB);
    llvm::Value *FromMapType = MapperCGF.Builder.CreateAnd(
        MemberMapType,
        MapperCGF.Builder.getInt64(
            ~static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                OpenMPOffloadMappingFlags::OMP_MAP_TO)));
    // In case of tofrom, do nothing.
    MapperCGF.EmitBlock(EndBB);
    LastBB = EndBB;
    llvm::PHINode *CurMapType =
        MapperCGF.Builder.CreatePHI(CGM.Int64Ty, 4, "omp.maptype");
    CurMapType->addIncoming(AllocMapType, AllocBB);
    CurMapType->addIncoming(ToMapType, ToBB);
    CurMapType->addIncoming(FromMapType, FromBB);
    CurMapType->addIncoming(MemberMapType, ToElseBB);

    llvm::Value *OffloadingArgs[] = {Handle,     CurBaseArg, CurBeginArg,
                                     CurSizeArg, CurMapType, CurNameArg};
    if (Info.Mappers[I]) {
      // Call the corresponding mapper function.
      llvm::Function *MapperFunc = getOrCreateUserDefinedMapperFunc(
          cast<OMPDeclareMapperDecl>(Info.Mappers[I]));
      assert(MapperFunc && "Expect a valid mapper function is available.");
      MapperCGF.EmitNounwindRuntimeCall(MapperFunc, OffloadingArgs);
    } else {
      // Call the runtime API __tgt_push_mapper_component to fill up the runtime
      // data structure.
      MapperCGF.EmitRuntimeCall(
          OMPBuilder.getOrCreateRuntimeFunction(
              CGM.getModule(), OMPRTL___tgt_push_mapper_component),
          OffloadingArgs);
    }
  }

  // Update the pointer to point to the next element that needs to be mapped,
  // and check whether we have mapped all elements.
  llvm::Value *PtrNext = MapperCGF.Builder.CreateConstGEP1_32(
      ElemTy, PtrPHI, /*Idx0=*/1, "omp.arraymap.next");
  PtrPHI->addIncoming(PtrNext, LastBB);
  llvm::Value *IsDone =
      MapperCGF.Builder.CreateICmpEQ(PtrNext, PtrEnd, "omp.arraymap.isdone");
  llvm::BasicBlock *ExitBB = MapperCGF.createBasicBlock("omp.arraymap.exit");
  MapperCGF.Builder.CreateCondBr(IsDone, ExitBB, BodyBB);

  MapperCGF.EmitBlock(ExitBB);
  // Emit array deletion if this is an array section and \p MapType indicates
  // that deletion is required.
  emitUDMapperArrayInitOrDel(MapperCGF, Handle, BaseIn, BeginIn, Size, MapType,
                             MapName, ElementSize, DoneBB, /*IsInit=*/false);

  // Emit the function exit block.
  MapperCGF.EmitBlock(DoneBB, /*IsFinished=*/true);
  MapperCGF.FinishFunction();
  UDMMap.try_emplace(D, Fn);
  if (CGF) {
    auto &Decls = FunctionUDMMap.FindAndConstruct(CGF->CurFn);
    Decls.second.push_back(D);
  }
}

/// Emit the array initialization or deletion portion for user-defined mapper
/// code generation. First, it evaluates whether an array section is mapped and
/// whether the \a MapType instructs to delete this section. If \a IsInit is
/// true, and \a MapType indicates to not delete this array, array
/// initialization code is generated. If \a IsInit is false, and \a MapType
/// indicates to not this array, array deletion code is generated.
void CGOpenMPRuntime::emitUDMapperArrayInitOrDel(
    CodeGenFunction &MapperCGF, llvm::Value *Handle, llvm::Value *Base,
    llvm::Value *Begin, llvm::Value *Size, llvm::Value *MapType,
    llvm::Value *MapName, CharUnits ElementSize, llvm::BasicBlock *ExitBB,
    bool IsInit) {
  StringRef Prefix = IsInit ? ".init" : ".del";

  // Evaluate if this is an array section.
  llvm::BasicBlock *BodyBB =
      MapperCGF.createBasicBlock(getName({"omp.array", Prefix}));
  llvm::Value *IsArray = MapperCGF.Builder.CreateICmpSGT(
      Size, MapperCGF.Builder.getInt64(1), "omp.arrayinit.isarray");
  llvm::Value *DeleteBit = MapperCGF.Builder.CreateAnd(
      MapType,
      MapperCGF.Builder.getInt64(
          static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
              OpenMPOffloadMappingFlags::OMP_MAP_DELETE)));
  llvm::Value *DeleteCond;
  llvm::Value *Cond;
  if (IsInit) {
    // base != begin?
    llvm::Value *BaseIsBegin = MapperCGF.Builder.CreateICmpNE(Base, Begin);
    // IsPtrAndObj?
    llvm::Value *PtrAndObjBit = MapperCGF.Builder.CreateAnd(
        MapType,
        MapperCGF.Builder.getInt64(
            static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
                OpenMPOffloadMappingFlags::OMP_MAP_PTR_AND_OBJ)));
    PtrAndObjBit = MapperCGF.Builder.CreateIsNotNull(PtrAndObjBit);
    BaseIsBegin = MapperCGF.Builder.CreateAnd(BaseIsBegin, PtrAndObjBit);
    Cond = MapperCGF.Builder.CreateOr(IsArray, BaseIsBegin);
    DeleteCond = MapperCGF.Builder.CreateIsNull(
        DeleteBit, getName({"omp.array", Prefix, ".delete"}));
  } else {
    Cond = IsArray;
    DeleteCond = MapperCGF.Builder.CreateIsNotNull(
        DeleteBit, getName({"omp.array", Prefix, ".delete"}));
  }
  Cond = MapperCGF.Builder.CreateAnd(Cond, DeleteCond);
  MapperCGF.Builder.CreateCondBr(Cond, BodyBB, ExitBB);

  MapperCGF.EmitBlock(BodyBB);
  // Get the array size by multiplying element size and element number (i.e., \p
  // Size).
  llvm::Value *ArraySize = MapperCGF.Builder.CreateNUWMul(
      Size, MapperCGF.Builder.getInt64(ElementSize.getQuantity()));
  // Remove OMP_MAP_TO and OMP_MAP_FROM from the map type, so that it achieves
  // memory allocation/deletion purpose only.
  llvm::Value *MapTypeArg = MapperCGF.Builder.CreateAnd(
      MapType,
      MapperCGF.Builder.getInt64(
          ~static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
              OpenMPOffloadMappingFlags::OMP_MAP_TO |
              OpenMPOffloadMappingFlags::OMP_MAP_FROM)));
  MapTypeArg = MapperCGF.Builder.CreateOr(
      MapTypeArg,
      MapperCGF.Builder.getInt64(
          static_cast<std::underlying_type_t<OpenMPOffloadMappingFlags>>(
              OpenMPOffloadMappingFlags::OMP_MAP_IMPLICIT)));

  // Call the runtime API __tgt_push_mapper_component to fill up the runtime
  // data structure.
  llvm::Value *OffloadingArgs[] = {Handle,    Base,       Begin,
                                   ArraySize, MapTypeArg, MapName};
  MapperCGF.EmitRuntimeCall(
      OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                            OMPRTL___tgt_push_mapper_component),
      OffloadingArgs);
}

llvm::Function *CGOpenMPRuntime::getOrCreateUserDefinedMapperFunc(
    const OMPDeclareMapperDecl *D) {
  auto I = UDMMap.find(D);
  if (I != UDMMap.end())
    return I->second;
  emitUserDefinedMapper(D);
  return UDMMap.lookup(D);
}

llvm::Value *CGOpenMPRuntime::emitTargetNumIterationsCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    llvm::function_ref<llvm::Value *(CodeGenFunction &CGF,
                                     const OMPLoopDirective &D)>
        SizeEmitter) {
  OpenMPDirectiveKind Kind = D.getDirectiveKind();
  const OMPExecutableDirective *TD = &D;
  // Get nested teams distribute kind directive, if any. For now, treat
  // 'target_teams_loop' as if it's really a target_teams_distribute.
  if ((!isOpenMPDistributeDirective(Kind) || !isOpenMPTeamsDirective(Kind)) &&
      Kind != OMPD_target_teams_loop)
    TD = getNestedDistributeDirective(CGM.getContext(), D);
  if (!TD)
    return llvm::ConstantInt::get(CGF.Int64Ty, 0);

  const auto *LD = cast<OMPLoopDirective>(TD);
  if (llvm::Value *NumIterations = SizeEmitter(CGF, *LD))
    return NumIterations;
  return llvm::ConstantInt::get(CGF.Int64Ty, 0);
}

static void
emitTargetCallFallback(CGOpenMPRuntime *OMPRuntime, llvm::Function *OutlinedFn,
                       const OMPExecutableDirective &D,
                       llvm::SmallVectorImpl<llvm::Value *> &CapturedVars,
                       bool RequiresOuterTask, const CapturedStmt &CS,
                       bool OffloadingMandatory, CodeGenFunction &CGF) {
  if (OffloadingMandatory) {
    CGF.Builder.CreateUnreachable();
  } else {
    if (RequiresOuterTask) {
      CapturedVars.clear();
      CGF.GenerateOpenMPCapturedVars(CS, CapturedVars);
    }
    OMPRuntime->emitOutlinedFunctionCall(CGF, D.getBeginLoc(), OutlinedFn,
                                         CapturedVars);
  }
}

static llvm::Value *emitDeviceID(
    llvm::PointerIntPair<const Expr *, 2, OpenMPDeviceClauseModifier> Device,
    CodeGenFunction &CGF) {
  // Emit device ID if any.
  llvm::Value *DeviceID;
  if (Device.getPointer()) {
    assert((Device.getInt() == OMPC_DEVICE_unknown ||
            Device.getInt() == OMPC_DEVICE_device_num) &&
           "Expected device_num modifier.");
    llvm::Value *DevVal = CGF.EmitScalarExpr(Device.getPointer());
    DeviceID =
        CGF.Builder.CreateIntCast(DevVal, CGF.Int64Ty, /*isSigned=*/true);
  } else {
    DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
  }
  return DeviceID;
}

llvm::Value *emitDynCGGroupMem(const OMPExecutableDirective &D,
                               CodeGenFunction &CGF) {
  llvm::Value *DynCGroupMem = CGF.Builder.getInt32(0);

  if (auto *DynMemClause = D.getSingleClause<OMPXDynCGroupMemClause>()) {
    CodeGenFunction::RunCleanupsScope DynCGroupMemScope(CGF);
    llvm::Value *DynCGroupMemVal = CGF.EmitScalarExpr(
        DynMemClause->getSize(), /*IgnoreResultAssign=*/true);
    DynCGroupMem = CGF.Builder.CreateIntCast(DynCGroupMemVal, CGF.Int32Ty,
                                             /*isSigned=*/false);
  }
  return DynCGroupMem;
}

static void emitTargetCallKernelLaunch(
    CGOpenMPRuntime *OMPRuntime, llvm::Function *OutlinedFn,
    const OMPExecutableDirective &D,
    llvm::SmallVectorImpl<llvm::Value *> &CapturedVars, bool RequiresOuterTask,
    const CapturedStmt &CS, bool OffloadingMandatory,
    llvm::PointerIntPair<const Expr *, 2, OpenMPDeviceClauseModifier> Device,
    llvm::Value *OutlinedFnID, CodeGenFunction::OMPTargetDataInfo &InputInfo,
    llvm::Value *&MapTypesArray, llvm::Value *&MapNamesArray,
    llvm::function_ref<llvm::Value *(CodeGenFunction &CGF,
                                     const OMPLoopDirective &D)>
        SizeEmitter,
    CodeGenFunction &CGF, CodeGenModule &CGM) {
  llvm::OpenMPIRBuilder &OMPBuilder = OMPRuntime->getOMPBuilder();

  // Fill up the arrays with all the captured variables.
  MappableExprsHandler::MapCombinedInfoTy CombinedInfo;

  // Get mappable expression information.
  MappableExprsHandler MEHandler(D, CGF);
  llvm::DenseMap<llvm::Value *, llvm::Value *> LambdaPointers;
  llvm::DenseSet<CanonicalDeclPtr<const Decl>> MappedVarSet;

  auto RI = CS.getCapturedRecordDecl()->field_begin();
  auto *CV = CapturedVars.begin();
  for (CapturedStmt::const_capture_iterator CI = CS.capture_begin(),
                                            CE = CS.capture_end();
       CI != CE; ++CI, ++RI, ++CV) {
    MappableExprsHandler::MapCombinedInfoTy CurInfo;
    MappableExprsHandler::StructRangeInfoTy PartialStruct;

    // VLA sizes are passed to the outlined region by copy and do not have map
    // information associated.
    if (CI->capturesVariableArrayType()) {
      CurInfo.Exprs.push_back(nullptr);
      CurInfo.BasePointers.push_back(*CV);
      CurInfo.DevicePtrDecls.push_back(nullptr);
      CurInfo.DevicePointers.push_back(
          MappableExprsHandler::DeviceInfoTy::None);
      CurInfo.Pointers.push_back(*CV);
      CurInfo.Sizes.push_back(CGF.Builder.CreateIntCast(
          CGF.getTypeSize(RI->getType()), CGF.Int64Ty, /*isSigned=*/true));
      // Copy to the device as an argument. No need to retrieve it.
      CurInfo.Types.push_back(OpenMPOffloadMappingFlags::OMP_MAP_LITERAL |
                              OpenMPOffloadMappingFlags::OMP_MAP_TARGET_PARAM |
                              OpenMPOffloadMappingFlags::OMP_MAP_IMPLICIT);
      CurInfo.Mappers.push_back(nullptr);
    } else {
      // If we have any information in the map clause, we use it, otherwise we
      // just do a default mapping.
      MEHandler.generateInfoForCapture(CI, *CV, CurInfo, PartialStruct);
      if (!CI->capturesThis())
        MappedVarSet.insert(CI->getCapturedVar());
      else
        MappedVarSet.insert(nullptr);
      if (CurInfo.BasePointers.empty() && !PartialStruct.Base.isValid())
        MEHandler.generateDefaultMapInfo(*CI, **RI, *CV, CurInfo);
      // Generate correct mapping for variables captured by reference in
      // lambdas.
      if (CI->capturesVariable())
        MEHandler.generateInfoForLambdaCaptures(CI->getCapturedVar(), *CV,
                                                CurInfo, LambdaPointers);
    }
    // We expect to have at least an element of information for this capture.
    assert((!CurInfo.BasePointers.empty() || PartialStruct.Base.isValid()) &&
           "Non-existing map pointer for capture!");
    assert(CurInfo.BasePointers.size() == CurInfo.Pointers.size() &&
           CurInfo.BasePointers.size() == CurInfo.Sizes.size() &&
           CurInfo.BasePointers.size() == CurInfo.Types.size() &&
           CurInfo.BasePointers.size() == CurInfo.Mappers.size() &&
           "Inconsistent map information sizes!");

    // If there is an entry in PartialStruct it means we have a struct with
    // individual members mapped. Emit an extra combined entry.
    if (PartialStruct.Base.isValid()) {
      CombinedInfo.append(PartialStruct.PreliminaryMapData);
      MEHandler.emitCombinedEntry(
          CombinedInfo, CurInfo.Types, PartialStruct, CI->capturesThis(),
          OMPBuilder, nullptr,
          !PartialStruct.PreliminaryMapData.BasePointers.empty());
    }

    // We need to append the results of this capture to what we already have.
    CombinedInfo.append(CurInfo);
  }
  // Adjust MEMBER_OF flags for the lambdas captures.
  MEHandler.adjustMemberOfForLambdaCaptures(
      OMPBuilder, LambdaPointers, CombinedInfo.BasePointers,
      CombinedInfo.Pointers, CombinedInfo.Types);
  // Map any list items in a map clause that were not captures because they
  // weren't referenced within the construct.
  MEHandler.generateAllInfo(CombinedInfo, OMPBuilder, MappedVarSet);

  CGOpenMPRuntime::TargetDataInfo Info;
  // Fill up the arrays and create the arguments.
  emitOffloadingArrays(CGF, CombinedInfo, Info, OMPBuilder);
  bool EmitDebug = CGF.CGM.getCodeGenOpts().getDebugInfo() !=
                   llvm::codegenoptions::NoDebugInfo;
  OMPBuilder.emitOffloadingArraysArgument(CGF.Builder, Info.RTArgs, Info,
                                          EmitDebug,
                                          /*ForEndCall=*/false);

  InputInfo.NumberOfTargetItems = Info.NumberOfPtrs;
  InputInfo.BasePointersArray = Address(Info.RTArgs.BasePointersArray,
                                        CGF.VoidPtrTy, CGM.getPointerAlign());
  InputInfo.PointersArray =
      Address(Info.RTArgs.PointersArray, CGF.VoidPtrTy, CGM.getPointerAlign());
  InputInfo.SizesArray =
      Address(Info.RTArgs.SizesArray, CGF.Int64Ty, CGM.getPointerAlign());
  InputInfo.MappersArray =
      Address(Info.RTArgs.MappersArray, CGF.VoidPtrTy, CGM.getPointerAlign());
  MapTypesArray = Info.RTArgs.MapTypesArray;
  MapNamesArray = Info.RTArgs.MapNamesArray;

  auto &&ThenGen = [&OMPRuntime, OutlinedFn, &D, &CapturedVars,
                    RequiresOuterTask, &CS, OffloadingMandatory, Device,
                    OutlinedFnID, &InputInfo, &MapTypesArray, &MapNamesArray,
                    SizeEmitter](CodeGenFunction &CGF, PrePostActionTy &) {
    bool IsReverseOffloading = Device.getInt() == OMPC_DEVICE_ancestor;

    if (IsReverseOffloading) {
      // Reverse offloading is not supported, so just execute on the host.
      // FIXME: This fallback solution is incorrect since it ignores the
      // OMP_TARGET_OFFLOAD environment variable. Instead it would be better to
      // assert here and ensure SEMA emits an error.
      emitTargetCallFallback(OMPRuntime, OutlinedFn, D, CapturedVars,
                             RequiresOuterTask, CS, OffloadingMandatory, CGF);
      return;
    }

    bool HasNoWait = D.hasClausesOfKind<OMPNowaitClause>();
    unsigned NumTargetItems = InputInfo.NumberOfTargetItems;

    llvm::Value *BasePointersArray =
        InputInfo.BasePointersArray.emitRawPointer(CGF);
    llvm::Value *PointersArray = InputInfo.PointersArray.emitRawPointer(CGF);
    llvm::Value *SizesArray = InputInfo.SizesArray.emitRawPointer(CGF);
    llvm::Value *MappersArray = InputInfo.MappersArray.emitRawPointer(CGF);

    auto &&EmitTargetCallFallbackCB =
        [&OMPRuntime, OutlinedFn, &D, &CapturedVars, RequiresOuterTask, &CS,
         OffloadingMandatory, &CGF](llvm::OpenMPIRBuilder::InsertPointTy IP)
        -> llvm::OpenMPIRBuilder::InsertPointTy {
      CGF.Builder.restoreIP(IP);
      emitTargetCallFallback(OMPRuntime, OutlinedFn, D, CapturedVars,
                             RequiresOuterTask, CS, OffloadingMandatory, CGF);
      return CGF.Builder.saveIP();
    };

    llvm::Value *DeviceID = emitDeviceID(Device, CGF);
    llvm::Value *NumTeams = OMPRuntime->emitNumTeamsForTargetDirective(CGF, D);
    llvm::Value *NumThreads =
        OMPRuntime->emitNumThreadsForTargetDirective(CGF, D);
    llvm::Value *RTLoc = OMPRuntime->emitUpdateLocation(CGF, D.getBeginLoc());
    llvm::Value *NumIterations =
        OMPRuntime->emitTargetNumIterationsCall(CGF, D, SizeEmitter);
    llvm::Value *DynCGGroupMem = emitDynCGGroupMem(D, CGF);
    llvm::OpenMPIRBuilder::InsertPointTy AllocaIP(
        CGF.AllocaInsertPt->getParent(), CGF.AllocaInsertPt->getIterator());

    llvm::OpenMPIRBuilder::TargetDataRTArgs RTArgs(
        BasePointersArray, PointersArray, SizesArray, MapTypesArray,
        nullptr /* MapTypesArrayEnd */, MappersArray, MapNamesArray);

    llvm::OpenMPIRBuilder::TargetKernelArgs Args(
        NumTargetItems, RTArgs, NumIterations, NumTeams, NumThreads,
        DynCGGroupMem, HasNoWait);

    CGF.Builder.restoreIP(OMPRuntime->getOMPBuilder().emitKernelLaunch(
        CGF.Builder, OutlinedFn, OutlinedFnID, EmitTargetCallFallbackCB, Args,
        DeviceID, RTLoc, AllocaIP));
  };

  if (RequiresOuterTask)
    CGF.EmitOMPTargetTaskBasedDirective(D, ThenGen, InputInfo);
  else
    OMPRuntime->emitInlinedDirective(CGF, D.getDirectiveKind(), ThenGen);
}

static void
emitTargetCallElse(CGOpenMPRuntime *OMPRuntime, llvm::Function *OutlinedFn,
                   const OMPExecutableDirective &D,
                   llvm::SmallVectorImpl<llvm::Value *> &CapturedVars,
                   bool RequiresOuterTask, const CapturedStmt &CS,
                   bool OffloadingMandatory, CodeGenFunction &CGF) {

  // Notify that the host version must be executed.
  auto &&ElseGen =
      [&OMPRuntime, OutlinedFn, &D, &CapturedVars, RequiresOuterTask, &CS,
       OffloadingMandatory](CodeGenFunction &CGF, PrePostActionTy &) {
        emitTargetCallFallback(OMPRuntime, OutlinedFn, D, CapturedVars,
                               RequiresOuterTask, CS, OffloadingMandatory, CGF);
      };

  if (RequiresOuterTask) {
    CodeGenFunction::OMPTargetDataInfo InputInfo;
    CGF.EmitOMPTargetTaskBasedDirective(D, ElseGen, InputInfo);
  } else {
    OMPRuntime->emitInlinedDirective(CGF, D.getDirectiveKind(), ElseGen);
  }
}

void CGOpenMPRuntime::emitTargetCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    llvm::Function *OutlinedFn, llvm::Value *OutlinedFnID, const Expr *IfCond,
    llvm::PointerIntPair<const Expr *, 2, OpenMPDeviceClauseModifier> Device,
    llvm::function_ref<llvm::Value *(CodeGenFunction &CGF,
                                     const OMPLoopDirective &D)>
        SizeEmitter) {
  if (!CGF.HaveInsertPoint())
    return;

  const bool OffloadingMandatory = !CGM.getLangOpts().OpenMPIsTargetDevice &&
                                   CGM.getLangOpts().OpenMPOffloadMandatory;

  assert((OffloadingMandatory || OutlinedFn) && "Invalid outlined function!");

  const bool RequiresOuterTask =
      D.hasClausesOfKind<OMPDependClause>() ||
      D.hasClausesOfKind<OMPNowaitClause>() ||
      D.hasClausesOfKind<OMPInReductionClause>() ||
      (CGM.getLangOpts().OpenMP >= 51 &&
       needsTaskBasedThreadLimit(D.getDirectiveKind()) &&
       D.hasClausesOfKind<OMPThreadLimitClause>());
  llvm::SmallVector<llvm::Value *, 16> CapturedVars;
  const CapturedStmt &CS = *D.getCapturedStmt(OMPD_target);
  auto &&ArgsCodegen = [&CS, &CapturedVars](CodeGenFunction &CGF,
                                            PrePostActionTy &) {
    CGF.GenerateOpenMPCapturedVars(CS, CapturedVars);
  };
  emitInlinedDirective(CGF, OMPD_unknown, ArgsCodegen);

  CodeGenFunction::OMPTargetDataInfo InputInfo;
  llvm::Value *MapTypesArray = nullptr;
  llvm::Value *MapNamesArray = nullptr;

  auto &&TargetThenGen = [this, OutlinedFn, &D, &CapturedVars,
                          RequiresOuterTask, &CS, OffloadingMandatory, Device,
                          OutlinedFnID, &InputInfo, &MapTypesArray,
                          &MapNamesArray, SizeEmitter](CodeGenFunction &CGF,
                                                       PrePostActionTy &) {
    emitTargetCallKernelLaunch(this, OutlinedFn, D, CapturedVars,
                               RequiresOuterTask, CS, OffloadingMandatory,
                               Device, OutlinedFnID, InputInfo, MapTypesArray,
                               MapNamesArray, SizeEmitter, CGF, CGM);
  };

  auto &&TargetElseGen =
      [this, OutlinedFn, &D, &CapturedVars, RequiresOuterTask, &CS,
       OffloadingMandatory](CodeGenFunction &CGF, PrePostActionTy &) {
        emitTargetCallElse(this, OutlinedFn, D, CapturedVars, RequiresOuterTask,
                           CS, OffloadingMandatory, CGF);
      };

  // If we have a target function ID it means that we need to support
  // offloading, otherwise, just execute on the host. We need to execute on host
  // regardless of the conditional in the if clause if, e.g., the user do not
  // specify target triples.
  if (OutlinedFnID) {
    if (IfCond) {
      emitIfClause(CGF, IfCond, TargetThenGen, TargetElseGen);
    } else {
      RegionCodeGenTy ThenRCG(TargetThenGen);
      ThenRCG(CGF);
    }
  } else {
    RegionCodeGenTy ElseRCG(TargetElseGen);
    ElseRCG(CGF);
  }
}

void CGOpenMPRuntime::scanForTargetRegionsFunctions(const Stmt *S,
                                                    StringRef ParentName) {
  if (!S)
    return;

  // Codegen OMP target directives that offload compute to the device.
  bool RequiresDeviceCodegen =
      isa<OMPExecutableDirective>(S) &&
      isOpenMPTargetExecutionDirective(
          cast<OMPExecutableDirective>(S)->getDirectiveKind());

  if (RequiresDeviceCodegen) {
    const auto &E = *cast<OMPExecutableDirective>(S);

    llvm::TargetRegionEntryInfo EntryInfo = getEntryInfoFromPresumedLoc(
        CGM, OMPBuilder, E.getBeginLoc(), ParentName);

    // Is this a target region that should not be emitted as an entry point? If
    // so just signal we are done with this target region.
    if (!OMPBuilder.OffloadInfoManager.hasTargetRegionEntryInfo(EntryInfo))
      return;

    switch (E.getDirectiveKind()) {
    case OMPD_target:
      CodeGenFunction::EmitOMPTargetDeviceFunction(CGM, ParentName,
                                                   cast<OMPTargetDirective>(E));
      break;
    case OMPD_target_parallel:
      CodeGenFunction::EmitOMPTargetParallelDeviceFunction(
          CGM, ParentName, cast<OMPTargetParallelDirective>(E));
      break;
    case OMPD_target_teams:
      CodeGenFunction::EmitOMPTargetTeamsDeviceFunction(
          CGM, ParentName, cast<OMPTargetTeamsDirective>(E));
      break;
    case OMPD_target_teams_distribute:
      CodeGenFunction::EmitOMPTargetTeamsDistributeDeviceFunction(
          CGM, ParentName, cast<OMPTargetTeamsDistributeDirective>(E));
      break;
    case OMPD_target_teams_distribute_simd:
      CodeGenFunction::EmitOMPTargetTeamsDistributeSimdDeviceFunction(
          CGM, ParentName, cast<OMPTargetTeamsDistributeSimdDirective>(E));
      break;
    case OMPD_target_parallel_for:
      CodeGenFunction::EmitOMPTargetParallelForDeviceFunction(
          CGM, ParentName, cast<OMPTargetParallelForDirective>(E));
      break;
    case OMPD_target_parallel_for_simd:
      CodeGenFunction::EmitOMPTargetParallelForSimdDeviceFunction(
          CGM, ParentName, cast<OMPTargetParallelForSimdDirective>(E));
      break;
    case OMPD_target_simd:
      CodeGenFunction::EmitOMPTargetSimdDeviceFunction(
          CGM, ParentName, cast<OMPTargetSimdDirective>(E));
      break;
    case OMPD_target_teams_distribute_parallel_for:
      CodeGenFunction::EmitOMPTargetTeamsDistributeParallelForDeviceFunction(
          CGM, ParentName,
          cast<OMPTargetTeamsDistributeParallelForDirective>(E));
      break;
    case OMPD_target_teams_distribute_parallel_for_simd:
      CodeGenFunction::
          EmitOMPTargetTeamsDistributeParallelForSimdDeviceFunction(
              CGM, ParentName,
              cast<OMPTargetTeamsDistributeParallelForSimdDirective>(E));
      break;
    case OMPD_target_teams_loop:
      CodeGenFunction::EmitOMPTargetTeamsGenericLoopDeviceFunction(
          CGM, ParentName, cast<OMPTargetTeamsGenericLoopDirective>(E));
      break;
    case OMPD_target_parallel_loop:
      CodeGenFunction::EmitOMPTargetParallelGenericLoopDeviceFunction(
          CGM, ParentName, cast<OMPTargetParallelGenericLoopDirective>(E));
      break;
    case OMPD_parallel:
    case OMPD_for:
    case OMPD_parallel_for:
    case OMPD_parallel_master:
    case OMPD_parallel_sections:
    case OMPD_for_simd:
    case OMPD_parallel_for_simd:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_ordered:
    case OMPD_threadprivate:
    case OMPD_allocate:
    case OMPD_task:
    case OMPD_simd:
    case OMPD_tile:
    case OMPD_unroll:
    case OMPD_sections:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_taskyield:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_atomic:
    case OMPD_flush:
    case OMPD_depobj:
    case OMPD_scan:
    case OMPD_teams:
    case OMPD_target_data:
    case OMPD_target_exit_data:
    case OMPD_target_enter_data:
    case OMPD_distribute:
    case OMPD_distribute_simd:
    case OMPD_distribute_parallel_for:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_target_update:
    case OMPD_declare_simd:
    case OMPD_declare_variant:
    case OMPD_begin_declare_variant:
    case OMPD_end_declare_variant:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_declare_reduction:
    case OMPD_declare_mapper:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_master_taskloop:
    case OMPD_master_taskloop_simd:
    case OMPD_parallel_master_taskloop:
    case OMPD_parallel_master_taskloop_simd:
    case OMPD_requires:
    case OMPD_metadirective:
    case OMPD_unknown:
    default:
      llvm_unreachable("Unknown target directive for OpenMP device codegen.");
    }
    return;
  }

  if (const auto *E = dyn_cast<OMPExecutableDirective>(S)) {
    if (!E->hasAssociatedStmt() || !E->getAssociatedStmt())
      return;

    scanForTargetRegionsFunctions(E->getRawStmt(), ParentName);
    return;
  }

  // If this is a lambda function, look into its body.
  if (const auto *L = dyn_cast<LambdaExpr>(S))
    S = L->getBody();

  // Keep looking for target regions recursively.
  for (const Stmt *II : S->children())
    scanForTargetRegionsFunctions(II, ParentName);
}

static bool isAssumedToBeNotEmitted(const ValueDecl *VD, bool IsDevice) {
  std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> DevTy =
      OMPDeclareTargetDeclAttr::getDeviceType(VD);
  if (!DevTy)
    return false;
  // Do not emit device_type(nohost) functions for the host.
  if (!IsDevice && DevTy == OMPDeclareTargetDeclAttr::DT_NoHost)
    return true;
  // Do not emit device_type(host) functions for the device.
  if (IsDevice && DevTy == OMPDeclareTargetDeclAttr::DT_Host)
    return true;
  return false;
}

bool CGOpenMPRuntime::emitTargetFunctions(GlobalDecl GD) {
  // If emitting code for the host, we do not process FD here. Instead we do
  // the normal code generation.
  if (!CGM.getLangOpts().OpenMPIsTargetDevice) {
    if (const auto *FD = dyn_cast<FunctionDecl>(GD.getDecl()))
      if (isAssumedToBeNotEmitted(cast<ValueDecl>(FD),
                                  CGM.getLangOpts().OpenMPIsTargetDevice))
        return true;
    return false;
  }

  const ValueDecl *VD = cast<ValueDecl>(GD.getDecl());
  // Try to detect target regions in the function.
  if (const auto *FD = dyn_cast<FunctionDecl>(VD)) {
    StringRef Name = CGM.getMangledName(GD);
    scanForTargetRegionsFunctions(FD->getBody(), Name);
    if (isAssumedToBeNotEmitted(cast<ValueDecl>(FD),
                                CGM.getLangOpts().OpenMPIsTargetDevice))
      return true;
  }

  // Do not to emit function if it is not marked as declare target.
  return !OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD) &&
         AlreadyEmittedTargetDecls.count(VD) == 0;
}

bool CGOpenMPRuntime::emitTargetGlobalVariable(GlobalDecl GD) {
  if (isAssumedToBeNotEmitted(cast<ValueDecl>(GD.getDecl()),
                              CGM.getLangOpts().OpenMPIsTargetDevice))
    return true;

  if (!CGM.getLangOpts().OpenMPIsTargetDevice)
    return false;

  // Check if there are Ctors/Dtors in this declaration and look for target
  // regions in it. We use the complete variant to produce the kernel name
  // mangling.
  QualType RDTy = cast<VarDecl>(GD.getDecl())->getType();
  if (const auto *RD = RDTy->getBaseElementTypeUnsafe()->getAsCXXRecordDecl()) {
    for (const CXXConstructorDecl *Ctor : RD->ctors()) {
      StringRef ParentName =
          CGM.getMangledName(GlobalDecl(Ctor, Ctor_Complete));
      scanForTargetRegionsFunctions(Ctor->getBody(), ParentName);
    }
    if (const CXXDestructorDecl *Dtor = RD->getDestructor()) {
      StringRef ParentName =
          CGM.getMangledName(GlobalDecl(Dtor, Dtor_Complete));
      scanForTargetRegionsFunctions(Dtor->getBody(), ParentName);
    }
  }

  // Do not to emit variable if it is not marked as declare target.
  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(
          cast<VarDecl>(GD.getDecl()));
  if (!Res || *Res == OMPDeclareTargetDeclAttr::MT_Link ||
      ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
        *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
       HasRequiresUnifiedSharedMemory)) {
    DeferredGlobalVariables.insert(cast<VarDecl>(GD.getDecl()));
    return true;
  }
  return false;
}

void CGOpenMPRuntime::registerTargetGlobalVariable(const VarDecl *VD,
                                                   llvm::Constant *Addr) {
  if (CGM.getLangOpts().OMPTargetTriples.empty() &&
      !CGM.getLangOpts().OpenMPIsTargetDevice)
    return;

  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);

  // If this is an 'extern' declaration we defer to the canonical definition and
  // do not emit an offloading entry.
  if (Res && *Res != OMPDeclareTargetDeclAttr::MT_Link &&
      VD->hasExternalStorage())
    return;

  if (!Res) {
    if (CGM.getLangOpts().OpenMPIsTargetDevice) {
      // Register non-target variables being emitted in device code (debug info
      // may cause this).
      StringRef VarName = CGM.getMangledName(VD);
      EmittedNonTargetVariables.try_emplace(VarName, Addr);
    }
    return;
  }

  auto AddrOfGlobal = [&VD, this]() { return CGM.GetAddrOfGlobal(VD); };
  auto LinkageForVariable = [&VD, this]() {
    return CGM.getLLVMLinkageVarDefinition(VD);
  };

  std::vector<llvm::GlobalVariable *> GeneratedRefs;
  OMPBuilder.registerTargetGlobalVariable(
      convertCaptureClause(VD), convertDeviceClause(VD),
      VD->hasDefinition(CGM.getContext()) == VarDecl::DeclarationOnly,
      VD->isExternallyVisible(),
      getEntryInfoFromPresumedLoc(CGM, OMPBuilder,
                                  VD->getCanonicalDecl()->getBeginLoc()),
      CGM.getMangledName(VD), GeneratedRefs, CGM.getLangOpts().OpenMPSimd,
      CGM.getLangOpts().OMPTargetTriples, AddrOfGlobal, LinkageForVariable,
      CGM.getTypes().ConvertTypeForMem(
          CGM.getContext().getPointerType(VD->getType())),
      Addr);

  for (auto *ref : GeneratedRefs)
    CGM.addCompilerUsedGlobal(ref);
}

bool CGOpenMPRuntime::emitTargetGlobal(GlobalDecl GD) {
  if (isa<FunctionDecl>(GD.getDecl()) ||
      isa<OMPDeclareReductionDecl>(GD.getDecl()))
    return emitTargetFunctions(GD);

  return emitTargetGlobalVariable(GD);
}

void CGOpenMPRuntime::emitDeferredTargetDecls() const {
  for (const VarDecl *VD : DeferredGlobalVariables) {
    std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
        OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
    if (!Res)
      continue;
    if ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
         *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
        !HasRequiresUnifiedSharedMemory) {
      CGM.EmitGlobal(VD);
    } else {
      assert((*Res == OMPDeclareTargetDeclAttr::MT_Link ||
              ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
                *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
               HasRequiresUnifiedSharedMemory)) &&
             "Expected link clause or to clause with unified memory.");
      (void)CGM.getOpenMPRuntime().getAddrOfDeclareTargetVar(VD);
    }
  }
}

void CGOpenMPRuntime::adjustTargetSpecificDataForLambdas(
    CodeGenFunction &CGF, const OMPExecutableDirective &D) const {
  assert(isOpenMPTargetExecutionDirective(D.getDirectiveKind()) &&
         " Expected target-based directive.");
}

void CGOpenMPRuntime::processRequiresDirective(const OMPRequiresDecl *D) {
  for (const OMPClause *Clause : D->clauselists()) {
    if (Clause->getClauseKind() == OMPC_unified_shared_memory) {
      HasRequiresUnifiedSharedMemory = true;
      OMPBuilder.Config.setHasRequiresUnifiedSharedMemory(true);
    } else if (const auto *AC =
                   dyn_cast<OMPAtomicDefaultMemOrderClause>(Clause)) {
      switch (AC->getAtomicDefaultMemOrderKind()) {
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_acq_rel:
        RequiresAtomicOrdering = llvm::AtomicOrdering::AcquireRelease;
        break;
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_seq_cst:
        RequiresAtomicOrdering = llvm::AtomicOrdering::SequentiallyConsistent;
        break;
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_relaxed:
        RequiresAtomicOrdering = llvm::AtomicOrdering::Monotonic;
        break;
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown:
        break;
      }
    }
  }
}

llvm::AtomicOrdering CGOpenMPRuntime::getDefaultMemoryOrdering() const {
  return RequiresAtomicOrdering;
}

bool CGOpenMPRuntime::hasAllocateAttributeForGlobalVar(const VarDecl *VD,
                                                       LangAS &AS) {
  if (!VD || !VD->hasAttr<OMPAllocateDeclAttr>())
    return false;
  const auto *A = VD->getAttr<OMPAllocateDeclAttr>();
  switch(A->getAllocatorType()) {
  case OMPAllocateDeclAttr::OMPNullMemAlloc:
  case OMPAllocateDeclAttr::OMPDefaultMemAlloc:
  // Not supported, fallback to the default mem space.
  case OMPAllocateDeclAttr::OMPLargeCapMemAlloc:
  case OMPAllocateDeclAttr::OMPCGroupMemAlloc:
  case OMPAllocateDeclAttr::OMPHighBWMemAlloc:
  case OMPAllocateDeclAttr::OMPLowLatMemAlloc:
  case OMPAllocateDeclAttr::OMPThreadMemAlloc:
  case OMPAllocateDeclAttr::OMPConstMemAlloc:
  case OMPAllocateDeclAttr::OMPPTeamMemAlloc:
    AS = LangAS::Default;
    return true;
  case OMPAllocateDeclAttr::OMPUserDefinedMemAlloc:
    llvm_unreachable("Expected predefined allocator for the variables with the "
                     "static storage.");
  }
  return false;
}

bool CGOpenMPRuntime::hasRequiresUnifiedSharedMemory() const {
  return HasRequiresUnifiedSharedMemory;
}

CGOpenMPRuntime::DisableAutoDeclareTargetRAII::DisableAutoDeclareTargetRAII(
    CodeGenModule &CGM)
    : CGM(CGM) {
  if (CGM.getLangOpts().OpenMPIsTargetDevice) {
    SavedShouldMarkAsGlobal = CGM.getOpenMPRuntime().ShouldMarkAsGlobal;
    CGM.getOpenMPRuntime().ShouldMarkAsGlobal = false;
  }
}

CGOpenMPRuntime::DisableAutoDeclareTargetRAII::~DisableAutoDeclareTargetRAII() {
  if (CGM.getLangOpts().OpenMPIsTargetDevice)
    CGM.getOpenMPRuntime().ShouldMarkAsGlobal = SavedShouldMarkAsGlobal;
}

bool CGOpenMPRuntime::markAsGlobalTarget(GlobalDecl GD) {
  if (!CGM.getLangOpts().OpenMPIsTargetDevice || !ShouldMarkAsGlobal)
    return true;

  const auto *D = cast<FunctionDecl>(GD.getDecl());
  // Do not to emit function if it is marked as declare target as it was already
  // emitted.
  if (OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(D)) {
    if (D->hasBody() && AlreadyEmittedTargetDecls.count(D) == 0) {
      if (auto *F = dyn_cast_or_null<llvm::Function>(
              CGM.GetGlobalValue(CGM.getMangledName(GD))))
        return !F->isDeclaration();
      return false;
    }
    return true;
  }

  return !AlreadyEmittedTargetDecls.insert(D).second;
}

void CGOpenMPRuntime::emitTeamsCall(CodeGenFunction &CGF,
                                    const OMPExecutableDirective &D,
                                    SourceLocation Loc,
                                    llvm::Function *OutlinedFn,
                                    ArrayRef<llvm::Value *> CapturedVars) {
  if (!CGF.HaveInsertPoint())
    return;

  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);
  CodeGenFunction::RunCleanupsScope Scope(CGF);

  // Build call __kmpc_fork_teams(loc, n, microtask, var1, .., varn);
  llvm::Value *Args[] = {
      RTLoc,
      CGF.Builder.getInt32(CapturedVars.size()), // Number of captured vars
      CGF.Builder.CreateBitCast(OutlinedFn, getKmpc_MicroPointerTy())};
  llvm::SmallVector<llvm::Value *, 16> RealArgs;
  RealArgs.append(std::begin(Args), std::end(Args));
  RealArgs.append(CapturedVars.begin(), CapturedVars.end());

  llvm::FunctionCallee RTLFn = OMPBuilder.getOrCreateRuntimeFunction(
      CGM.getModule(), OMPRTL___kmpc_fork_teams);
  CGF.EmitRuntimeCall(RTLFn, RealArgs);
}

void CGOpenMPRuntime::emitNumTeamsClause(CodeGenFunction &CGF,
                                         const Expr *NumTeams,
                                         const Expr *ThreadLimit,
                                         SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;

  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);

  llvm::Value *NumTeamsVal =
      NumTeams
          ? CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(NumTeams),
                                      CGF.CGM.Int32Ty, /* isSigned = */ true)
          : CGF.Builder.getInt32(0);

  llvm::Value *ThreadLimitVal =
      ThreadLimit
          ? CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(ThreadLimit),
                                      CGF.CGM.Int32Ty, /* isSigned = */ true)
          : CGF.Builder.getInt32(0);

  // Build call __kmpc_push_num_teamss(&loc, global_tid, num_teams, thread_limit)
  llvm::Value *PushNumTeamsArgs[] = {RTLoc, getThreadID(CGF, Loc), NumTeamsVal,
                                     ThreadLimitVal};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_push_num_teams),
                      PushNumTeamsArgs);
}

void CGOpenMPRuntime::emitThreadLimitClause(CodeGenFunction &CGF,
                                            const Expr *ThreadLimit,
                                            SourceLocation Loc) {
  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);
  llvm::Value *ThreadLimitVal =
      ThreadLimit
          ? CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(ThreadLimit),
                                      CGF.CGM.Int32Ty, /* isSigned = */ true)
          : CGF.Builder.getInt32(0);

  // Build call __kmpc_set_thread_limit(&loc, global_tid, thread_limit)
  llvm::Value *ThreadLimitArgs[] = {RTLoc, getThreadID(CGF, Loc),
                                    ThreadLimitVal};
  CGF.EmitRuntimeCall(OMPBuilder.getOrCreateRuntimeFunction(
                          CGM.getModule(), OMPRTL___kmpc_set_thread_limit),
                      ThreadLimitArgs);
}

void CGOpenMPRuntime::emitTargetDataCalls(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device, const RegionCodeGenTy &CodeGen,
    CGOpenMPRuntime::TargetDataInfo &Info) {
  if (!CGF.HaveInsertPoint())
    return;

  // Action used to replace the default codegen action and turn privatization
  // off.
  PrePostActionTy NoPrivAction;

  using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

  llvm::Value *IfCondVal = nullptr;
  if (IfCond)
    IfCondVal = CGF.EvaluateExprAsBool(IfCond);

  // Emit device ID if any.
  llvm::Value *DeviceID = nullptr;
  if (Device) {
    DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                         CGF.Int64Ty, /*isSigned=*/true);
  } else {
    DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
  }

  // Fill up the arrays with all the mapped variables.
  MappableExprsHandler::MapCombinedInfoTy CombinedInfo;
  auto GenMapInfoCB =
      [&](InsertPointTy CodeGenIP) -> llvm::OpenMPIRBuilder::MapInfosTy & {
    CGF.Builder.restoreIP(CodeGenIP);
    // Get map clause information.
    MappableExprsHandler MEHandler(D, CGF);
    MEHandler.generateAllInfo(CombinedInfo, OMPBuilder);

    auto FillInfoMap = [&](MappableExprsHandler::MappingExprInfo &MapExpr) {
      return emitMappingInformation(CGF, OMPBuilder, MapExpr);
    };
    if (CGM.getCodeGenOpts().getDebugInfo() !=
        llvm::codegenoptions::NoDebugInfo) {
      CombinedInfo.Names.resize(CombinedInfo.Exprs.size());
      llvm::transform(CombinedInfo.Exprs, CombinedInfo.Names.begin(),
                      FillInfoMap);
    }

    return CombinedInfo;
  };
  using BodyGenTy = llvm::OpenMPIRBuilder::BodyGenTy;
  auto BodyCB = [&](InsertPointTy CodeGenIP, BodyGenTy BodyGenType) {
    CGF.Builder.restoreIP(CodeGenIP);
    switch (BodyGenType) {
    case BodyGenTy::Priv:
      if (!Info.CaptureDeviceAddrMap.empty())
        CodeGen(CGF);
      break;
    case BodyGenTy::DupNoPriv:
      if (!Info.CaptureDeviceAddrMap.empty()) {
        CodeGen.setAction(NoPrivAction);
        CodeGen(CGF);
      }
      break;
    case BodyGenTy::NoPriv:
      if (Info.CaptureDeviceAddrMap.empty()) {
        CodeGen.setAction(NoPrivAction);
        CodeGen(CGF);
      }
      break;
    }
    return InsertPointTy(CGF.Builder.GetInsertBlock(),
                         CGF.Builder.GetInsertPoint());
  };

  auto DeviceAddrCB = [&](unsigned int I, llvm::Value *NewDecl) {
    if (const ValueDecl *DevVD = CombinedInfo.DevicePtrDecls[I]) {
      Info.CaptureDeviceAddrMap.try_emplace(DevVD, NewDecl);
    }
  };

  auto CustomMapperCB = [&](unsigned int I) {
    llvm::Value *MFunc = nullptr;
    if (CombinedInfo.Mappers[I]) {
      Info.HasMapper = true;
      MFunc = CGF.CGM.getOpenMPRuntime().getOrCreateUserDefinedMapperFunc(
          cast<OMPDeclareMapperDecl>(CombinedInfo.Mappers[I]));
    }
    return MFunc;
  };

  // Source location for the ident struct
  llvm::Value *RTLoc = emitUpdateLocation(CGF, D.getBeginLoc());

  InsertPointTy AllocaIP(CGF.AllocaInsertPt->getParent(),
                         CGF.AllocaInsertPt->getIterator());
  InsertPointTy CodeGenIP(CGF.Builder.GetInsertBlock(),
                          CGF.Builder.GetInsertPoint());
  llvm::OpenMPIRBuilder::LocationDescription OmpLoc(CodeGenIP);
  CGF.Builder.restoreIP(OMPBuilder.createTargetData(
      OmpLoc, AllocaIP, CodeGenIP, DeviceID, IfCondVal, Info, GenMapInfoCB,
      /*MapperFunc=*/nullptr, BodyCB, DeviceAddrCB, CustomMapperCB, RTLoc));
}

void CGOpenMPRuntime::emitTargetDataStandAloneCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device) {
  if (!CGF.HaveInsertPoint())
    return;

  assert((isa<OMPTargetEnterDataDirective>(D) ||
          isa<OMPTargetExitDataDirective>(D) ||
          isa<OMPTargetUpdateDirective>(D)) &&
         "Expecting either target enter, exit data, or update directives.");

  CodeGenFunction::OMPTargetDataInfo InputInfo;
  llvm::Value *MapTypesArray = nullptr;
  llvm::Value *MapNamesArray = nullptr;
  // Generate the code for the opening of the data environment.
  auto &&ThenGen = [this, &D, Device, &InputInfo, &MapTypesArray,
                    &MapNamesArray](CodeGenFunction &CGF, PrePostActionTy &) {
    // Emit device ID if any.
    llvm::Value *DeviceID = nullptr;
    if (Device) {
      DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                           CGF.Int64Ty, /*isSigned=*/true);
    } else {
      DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
    }

    // Emit the number of elements in the offloading arrays.
    llvm::Constant *PointerNum =
        CGF.Builder.getInt32(InputInfo.NumberOfTargetItems);

    // Source location for the ident struct
    llvm::Value *RTLoc = emitUpdateLocation(CGF, D.getBeginLoc());

    SmallVector<llvm::Value *, 13> OffloadingArgs(
        {RTLoc, DeviceID, PointerNum,
         InputInfo.BasePointersArray.emitRawPointer(CGF),
         InputInfo.PointersArray.emitRawPointer(CGF),
         InputInfo.SizesArray.emitRawPointer(CGF), MapTypesArray, MapNamesArray,
         InputInfo.MappersArray.emitRawPointer(CGF)});

    // Select the right runtime function call for each standalone
    // directive.
    const bool HasNowait = D.hasClausesOfKind<OMPNowaitClause>();
    RuntimeFunction RTLFn;
    switch (D.getDirectiveKind()) {
    case OMPD_target_enter_data:
      RTLFn = HasNowait ? OMPRTL___tgt_target_data_begin_nowait_mapper
                        : OMPRTL___tgt_target_data_begin_mapper;
      break;
    case OMPD_target_exit_data:
      RTLFn = HasNowait ? OMPRTL___tgt_target_data_end_nowait_mapper
                        : OMPRTL___tgt_target_data_end_mapper;
      break;
    case OMPD_target_update:
      RTLFn = HasNowait ? OMPRTL___tgt_target_data_update_nowait_mapper
                        : OMPRTL___tgt_target_data_update_mapper;
      break;
    case OMPD_parallel:
    case OMPD_for:
    case OMPD_parallel_for:
    case OMPD_parallel_master:
    case OMPD_parallel_sections:
    case OMPD_for_simd:
    case OMPD_parallel_for_simd:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_ordered:
    case OMPD_threadprivate:
    case OMPD_allocate:
    case OMPD_task:
    case OMPD_simd:
    case OMPD_tile:
    case OMPD_unroll:
    case OMPD_sections:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_taskyield:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_atomic:
    case OMPD_flush:
    case OMPD_depobj:
    case OMPD_scan:
    case OMPD_teams:
    case OMPD_target_data:
    case OMPD_distribute:
    case OMPD_distribute_simd:
    case OMPD_distribute_parallel_for:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_declare_simd:
    case OMPD_declare_variant:
    case OMPD_begin_declare_variant:
    case OMPD_end_declare_variant:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_declare_reduction:
    case OMPD_declare_mapper:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_master_taskloop:
    case OMPD_master_taskloop_simd:
    case OMPD_parallel_master_taskloop:
    case OMPD_parallel_master_taskloop_simd:
    case OMPD_target:
    case OMPD_target_simd:
    case OMPD_target_teams_distribute:
    case OMPD_target_teams_distribute_simd:
    case OMPD_target_teams_distribute_parallel_for:
    case OMPD_target_teams_distribute_parallel_for_simd:
    case OMPD_target_teams:
    case OMPD_target_parallel:
    case OMPD_target_parallel_for:
    case OMPD_target_parallel_for_simd:
    case OMPD_requires:
    case OMPD_metadirective:
    case OMPD_unknown:
    default:
      llvm_unreachable("Unexpected standalone target data directive.");
      break;
    }
    if (HasNowait) {
      OffloadingArgs.push_back(llvm::Constant::getNullValue(CGF.Int32Ty));
      OffloadingArgs.push_back(llvm::Constant::getNullValue(CGF.VoidPtrTy));
      OffloadingArgs.push_back(llvm::Constant::getNullValue(CGF.Int32Ty));
      OffloadingArgs.push_back(llvm::Constant::getNullValue(CGF.VoidPtrTy));
    }
    CGF.EmitRuntimeCall(
        OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(), RTLFn),
        OffloadingArgs);
  };

  auto &&TargetThenGen = [this, &ThenGen, &D, &InputInfo, &MapTypesArray,
                          &MapNamesArray](CodeGenFunction &CGF,
                                          PrePostActionTy &) {
    // Fill up the arrays with all the mapped variables.
    MappableExprsHandler::MapCombinedInfoTy CombinedInfo;

    // Get map clause information.
    MappableExprsHandler MEHandler(D, CGF);
    MEHandler.generateAllInfo(CombinedInfo, OMPBuilder);

    CGOpenMPRuntime::TargetDataInfo Info;
    // Fill up the arrays and create the arguments.
    emitOffloadingArrays(CGF, CombinedInfo, Info, OMPBuilder,
                         /*IsNonContiguous=*/true);
    bool RequiresOuterTask = D.hasClausesOfKind<OMPDependClause>() ||
                             D.hasClausesOfKind<OMPNowaitClause>();
    bool EmitDebug = CGF.CGM.getCodeGenOpts().getDebugInfo() !=
                     llvm::codegenoptions::NoDebugInfo;
    OMPBuilder.emitOffloadingArraysArgument(CGF.Builder, Info.RTArgs, Info,
                                            EmitDebug,
                                            /*ForEndCall=*/false);
    InputInfo.NumberOfTargetItems = Info.NumberOfPtrs;
    InputInfo.BasePointersArray = Address(Info.RTArgs.BasePointersArray,
                                          CGF.VoidPtrTy, CGM.getPointerAlign());
    InputInfo.PointersArray = Address(Info.RTArgs.PointersArray, CGF.VoidPtrTy,
                                      CGM.getPointerAlign());
    InputInfo.SizesArray =
        Address(Info.RTArgs.SizesArray, CGF.Int64Ty, CGM.getPointerAlign());
    InputInfo.MappersArray =
        Address(Info.RTArgs.MappersArray, CGF.VoidPtrTy, CGM.getPointerAlign());
    MapTypesArray = Info.RTArgs.MapTypesArray;
    MapNamesArray = Info.RTArgs.MapNamesArray;
    if (RequiresOuterTask)
      CGF.EmitOMPTargetTaskBasedDirective(D, ThenGen, InputInfo);
    else
      emitInlinedDirective(CGF, D.getDirectiveKind(), ThenGen);
  };

  if (IfCond) {
    emitIfClause(CGF, IfCond, TargetThenGen,
                 [](CodeGenFunction &CGF, PrePostActionTy &) {});
  } else {
    RegionCodeGenTy ThenRCG(TargetThenGen);
    ThenRCG(CGF);
  }
}

namespace {
  /// Kind of parameter in a function with 'declare simd' directive.
enum ParamKindTy {
  Linear,
  LinearRef,
  LinearUVal,
  LinearVal,
  Uniform,
  Vector,
};
/// Attribute set of the parameter.
struct ParamAttrTy {
  ParamKindTy Kind = Vector;
  llvm::APSInt StrideOrArg;
  llvm::APSInt Alignment;
  bool HasVarStride = false;
};
} // namespace

static unsigned evaluateCDTSize(const FunctionDecl *FD,
                                ArrayRef<ParamAttrTy> ParamAttrs) {
  // Every vector variant of a SIMD-enabled function has a vector length (VLEN).
  // If OpenMP clause "simdlen" is used, the VLEN is the value of the argument
  // of that clause. The VLEN value must be power of 2.
  // In other case the notion of the function`s "characteristic data type" (CDT)
  // is used to compute the vector length.
  // CDT is defined in the following order:
  //   a) For non-void function, the CDT is the return type.
  //   b) If the function has any non-uniform, non-linear parameters, then the
  //   CDT is the type of the first such parameter.
  //   c) If the CDT determined by a) or b) above is struct, union, or class
  //   type which is pass-by-value (except for the type that maps to the
  //   built-in complex data type), the characteristic data type is int.
  //   d) If none of the above three cases is applicable, the CDT is int.
  // The VLEN is then determined based on the CDT and the size of vector
  // register of that ISA for which current vector version is generated. The
  // VLEN is computed using the formula below:
  //   VLEN  = sizeof(vector_register) / sizeof(CDT),
  // where vector register size specified in section 3.2.1 Registers and the
  // Stack Frame of original AMD64 ABI document.
  QualType RetType = FD->getReturnType();
  if (RetType.isNull())
    return 0;
  ASTContext &C = FD->getASTContext();
  QualType CDT;
  if (!RetType.isNull() && !RetType->isVoidType()) {
    CDT = RetType;
  } else {
    unsigned Offset = 0;
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (ParamAttrs[Offset].Kind == Vector)
        CDT = C.getPointerType(C.getRecordType(MD->getParent()));
      ++Offset;
    }
    if (CDT.isNull()) {
      for (unsigned I = 0, E = FD->getNumParams(); I < E; ++I) {
        if (ParamAttrs[I + Offset].Kind == Vector) {
          CDT = FD->getParamDecl(I)->getType();
          break;
        }
      }
    }
  }
  if (CDT.isNull())
    CDT = C.IntTy;
  CDT = CDT->getCanonicalTypeUnqualified();
  if (CDT->isRecordType() || CDT->isUnionType())
    CDT = C.IntTy;
  return C.getTypeSize(CDT);
}

/// Mangle the parameter part of the vector function name according to
/// their OpenMP classification. The mangling function is defined in
/// section 4.5 of the AAVFABI(2021Q1).
static std::string mangleVectorParameters(ArrayRef<ParamAttrTy> ParamAttrs) {
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  for (const auto &ParamAttr : ParamAttrs) {
    switch (ParamAttr.Kind) {
    case Linear:
      Out << 'l';
      break;
    case LinearRef:
      Out << 'R';
      break;
    case LinearUVal:
      Out << 'U';
      break;
    case LinearVal:
      Out << 'L';
      break;
    case Uniform:
      Out << 'u';
      break;
    case Vector:
      Out << 'v';
      break;
    }
    if (ParamAttr.HasVarStride)
      Out << "s" << ParamAttr.StrideOrArg;
    else if (ParamAttr.Kind == Linear || ParamAttr.Kind == LinearRef ||
             ParamAttr.Kind == LinearUVal || ParamAttr.Kind == LinearVal) {
      // Don't print the step value if it is not present or if it is
      // equal to 1.
      if (ParamAttr.StrideOrArg < 0)
        Out << 'n' << -ParamAttr.StrideOrArg;
      else if (ParamAttr.StrideOrArg != 1)
        Out << ParamAttr.StrideOrArg;
    }

    if (!!ParamAttr.Alignment)
      Out << 'a' << ParamAttr.Alignment;
  }

  return std::string(Out.str());
}

static void
emitX86DeclareSimdFunction(const FunctionDecl *FD, llvm::Function *Fn,
                           const llvm::APSInt &VLENVal,
                           ArrayRef<ParamAttrTy> ParamAttrs,
                           OMPDeclareSimdDeclAttr::BranchStateTy State) {
  struct ISADataTy {
    char ISA;
    unsigned VecRegSize;
  };
  ISADataTy ISAData[] = {
      {
          'b', 128
      }, // SSE
      {
          'c', 256
      }, // AVX
      {
          'd', 256
      }, // AVX2
      {
          'e', 512
      }, // AVX512
  };
  llvm::SmallVector<char, 2> Masked;
  switch (State) {
  case OMPDeclareSimdDeclAttr::BS_Undefined:
    Masked.push_back('N');
    Masked.push_back('M');
    break;
  case OMPDeclareSimdDeclAttr::BS_Notinbranch:
    Masked.push_back('N');
    break;
  case OMPDeclareSimdDeclAttr::BS_Inbranch:
    Masked.push_back('M');
    break;
  }
  for (char Mask : Masked) {
    for (const ISADataTy &Data : ISAData) {
      SmallString<256> Buffer;
      llvm::raw_svector_ostream Out(Buffer);
      Out << "_ZGV" << Data.ISA << Mask;
      if (!VLENVal) {
        unsigned NumElts = evaluateCDTSize(FD, ParamAttrs);
        assert(NumElts && "Non-zero simdlen/cdtsize expected");
        Out << llvm::APSInt::getUnsigned(Data.VecRegSize / NumElts);
      } else {
        Out << VLENVal;
      }
      Out << mangleVectorParameters(ParamAttrs);
      Out << '_' << Fn->getName();
      Fn->addFnAttr(Out.str());
    }
  }
}

// This are the Functions that are needed to mangle the name of the
// vector functions generated by the compiler, according to the rules
// defined in the "Vector Function ABI specifications for AArch64",
// available at
// https://developer.arm.com/products/software-development-tools/hpc/arm-compiler-for-hpc/vector-function-abi.

/// Maps To Vector (MTV), as defined in 4.1.1 of the AAVFABI (2021Q1).
static bool getAArch64MTV(QualType QT, ParamKindTy Kind) {
  QT = QT.getCanonicalType();

  if (QT->isVoidType())
    return false;

  if (Kind == ParamKindTy::Uniform)
    return false;

  if (Kind == ParamKindTy::LinearUVal || Kind == ParamKindTy::LinearRef)
    return false;

  if ((Kind == ParamKindTy::Linear || Kind == ParamKindTy::LinearVal) &&
      !QT->isReferenceType())
    return false;

  return true;
}

/// Pass By Value (PBV), as defined in 3.1.2 of the AAVFABI.
static bool getAArch64PBV(QualType QT, ASTContext &C) {
  QT = QT.getCanonicalType();
  unsigned Size = C.getTypeSize(QT);

  // Only scalars and complex within 16 bytes wide set PVB to true.
  if (Size != 8 && Size != 16 && Size != 32 && Size != 64 && Size != 128)
    return false;

  if (QT->isFloatingType())
    return true;

  if (QT->isIntegerType())
    return true;

  if (QT->isPointerType())
    return true;

  // TODO: Add support for complex types (section 3.1.2, item 2).

  return false;
}

/// Computes the lane size (LS) of a return type or of an input parameter,
/// as defined by `LS(P)` in 3.2.1 of the AAVFABI.
/// TODO: Add support for references, section 3.2.1, item 1.
static unsigned getAArch64LS(QualType QT, ParamKindTy Kind, ASTContext &C) {
  if (!getAArch64MTV(QT, Kind) && QT.getCanonicalType()->isPointerType()) {
    QualType PTy = QT.getCanonicalType()->getPointeeType();
    if (getAArch64PBV(PTy, C))
      return C.getTypeSize(PTy);
  }
  if (getAArch64PBV(QT, C))
    return C.getTypeSize(QT);

  return C.getTypeSize(C.getUIntPtrType());
}

// Get Narrowest Data Size (NDS) and Widest Data Size (WDS) from the
// signature of the scalar function, as defined in 3.2.2 of the
// AAVFABI.
static std::tuple<unsigned, unsigned, bool>
getNDSWDS(const FunctionDecl *FD, ArrayRef<ParamAttrTy> ParamAttrs) {
  QualType RetType = FD->getReturnType().getCanonicalType();

  ASTContext &C = FD->getASTContext();

  bool OutputBecomesInput = false;

  llvm::SmallVector<unsigned, 8> Sizes;
  if (!RetType->isVoidType()) {
    Sizes.push_back(getAArch64LS(RetType, ParamKindTy::Vector, C));
    if (!getAArch64PBV(RetType, C) && getAArch64MTV(RetType, {}))
      OutputBecomesInput = true;
  }
  for (unsigned I = 0, E = FD->getNumParams(); I < E; ++I) {
    QualType QT = FD->getParamDecl(I)->getType().getCanonicalType();
    Sizes.push_back(getAArch64LS(QT, ParamAttrs[I].Kind, C));
  }

  assert(!Sizes.empty() && "Unable to determine NDS and WDS.");
  // The LS of a function parameter / return value can only be a power
  // of 2, starting from 8 bits, up to 128.
  assert(llvm::all_of(Sizes,
                      [](unsigned Size) {
                        return Size == 8 || Size == 16 || Size == 32 ||
                               Size == 64 || Size == 128;
                      }) &&
         "Invalid size");

  return std::make_tuple(*std::min_element(std::begin(Sizes), std::end(Sizes)),
                         *std::max_element(std::begin(Sizes), std::end(Sizes)),
                         OutputBecomesInput);
}

// Function used to add the attribute. The parameter `VLEN` is
// templated to allow the use of "x" when targeting scalable functions
// for SVE.
template <typename T>
static void addAArch64VectorName(T VLEN, StringRef LMask, StringRef Prefix,
                                 char ISA, StringRef ParSeq,
                                 StringRef MangledName, bool OutputBecomesInput,
                                 llvm::Function *Fn) {
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  Out << Prefix << ISA << LMask << VLEN;
  if (OutputBecomesInput)
    Out << "v";
  Out << ParSeq << "_" << MangledName;
  Fn->addFnAttr(Out.str());
}

// Helper function to generate the Advanced SIMD names depending on
// the value of the NDS when simdlen is not present.
static void addAArch64AdvSIMDNDSNames(unsigned NDS, StringRef Mask,
                                      StringRef Prefix, char ISA,
                                      StringRef ParSeq, StringRef MangledName,
                                      bool OutputBecomesInput,
                                      llvm::Function *Fn) {
  switch (NDS) {
  case 8:
    addAArch64VectorName(8, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, Fn);
    addAArch64VectorName(16, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, Fn);
    break;
  case 16:
    addAArch64VectorName(4, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, Fn);
    addAArch64VectorName(8, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, Fn);
    break;
  case 32:
    addAArch64VectorName(2, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, Fn);
    addAArch64VectorName(4, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, Fn);
    break;
  case 64:
  case 128:
    addAArch64VectorName(2, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, Fn);
    break;
  default:
    llvm_unreachable("Scalar type is too wide.");
  }
}

/// Emit vector function attributes for AArch64, as defined in the AAVFABI.
static void emitAArch64DeclareSimdFunction(
    CodeGenModule &CGM, const FunctionDecl *FD, unsigned UserVLEN,
    ArrayRef<ParamAttrTy> ParamAttrs,
    OMPDeclareSimdDeclAttr::BranchStateTy State, StringRef MangledName,
    char ISA, unsigned VecRegSize, llvm::Function *Fn, SourceLocation SLoc) {

  // Get basic data for building the vector signature.
  const auto Data = getNDSWDS(FD, ParamAttrs);
  const unsigned NDS = std::get<0>(Data);
  const unsigned WDS = std::get<1>(Data);
  const bool OutputBecomesInput = std::get<2>(Data);

  // Check the values provided via `simdlen` by the user.
  // 1. A `simdlen(1)` doesn't produce vector signatures,
  if (UserVLEN == 1) {
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Warning,
        "The clause simdlen(1) has no effect when targeting aarch64.");
    CGM.getDiags().Report(SLoc, DiagID);
    return;
  }

  // 2. Section 3.3.1, item 1: user input must be a power of 2 for
  // Advanced SIMD output.
  if (ISA == 'n' && UserVLEN && !llvm::isPowerOf2_32(UserVLEN)) {
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Warning, "The value specified in simdlen must be a "
                                    "power of 2 when targeting Advanced SIMD.");
    CGM.getDiags().Report(SLoc, DiagID);
    return;
  }

  // 3. Section 3.4.1. SVE fixed lengh must obey the architectural
  // limits.
  if (ISA == 's' && UserVLEN != 0) {
    if ((UserVLEN * WDS > 2048) || (UserVLEN * WDS % 128 != 0)) {
      unsigned DiagID = CGM.getDiags().getCustomDiagID(
          DiagnosticsEngine::Warning, "The clause simdlen must fit the %0-bit "
                                      "lanes in the architectural constraints "
                                      "for SVE (min is 128-bit, max is "
                                      "2048-bit, by steps of 128-bit)");
      CGM.getDiags().Report(SLoc, DiagID) << WDS;
      return;
    }
  }

  // Sort out parameter sequence.
  const std::string ParSeq = mangleVectorParameters(ParamAttrs);
  StringRef Prefix = "_ZGV";
  // Generate simdlen from user input (if any).
  if (UserVLEN) {
    if (ISA == 's') {
      // SVE generates only a masked function.
      addAArch64VectorName(UserVLEN, "M", Prefix, ISA, ParSeq, MangledName,
                           OutputBecomesInput, Fn);
    } else {
      assert(ISA == 'n' && "Expected ISA either 's' or 'n'.");
      // Advanced SIMD generates one or two functions, depending on
      // the `[not]inbranch` clause.
      switch (State) {
      case OMPDeclareSimdDeclAttr::BS_Undefined:
        addAArch64VectorName(UserVLEN, "N", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, Fn);
        addAArch64VectorName(UserVLEN, "M", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, Fn);
        break;
      case OMPDeclareSimdDeclAttr::BS_Notinbranch:
        addAArch64VectorName(UserVLEN, "N", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, Fn);
        break;
      case OMPDeclareSimdDeclAttr::BS_Inbranch:
        addAArch64VectorName(UserVLEN, "M", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, Fn);
        break;
      }
    }
  } else {
    // If no user simdlen is provided, follow the AAVFABI rules for
    // generating the vector length.
    if (ISA == 's') {
      // SVE, section 3.4.1, item 1.
      addAArch64VectorName("x", "M", Prefix, ISA, ParSeq, MangledName,
                           OutputBecomesInput, Fn);
    } else {
      assert(ISA == 'n' && "Expected ISA either 's' or 'n'.");
      // Advanced SIMD, Section 3.3.1 of the AAVFABI, generates one or
      // two vector names depending on the use of the clause
      // `[not]inbranch`.
      switch (State) {
      case OMPDeclareSimdDeclAttr::BS_Undefined:
        addAArch64AdvSIMDNDSNames(NDS, "N", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, Fn);
        addAArch64AdvSIMDNDSNames(NDS, "M", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, Fn);
        break;
      case OMPDeclareSimdDeclAttr::BS_Notinbranch:
        addAArch64AdvSIMDNDSNames(NDS, "N", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, Fn);
        break;
      case OMPDeclareSimdDeclAttr::BS_Inbranch:
        addAArch64AdvSIMDNDSNames(NDS, "M", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, Fn);
        break;
      }
    }
  }
}

void CGOpenMPRuntime::emitDeclareSimdFunction(const FunctionDecl *FD,
                                              llvm::Function *Fn) {
  ASTContext &C = CGM.getContext();
  FD = FD->getMostRecentDecl();
  while (FD) {
    // Map params to their positions in function decl.
    llvm::DenseMap<const Decl *, unsigned> ParamPositions;
    if (isa<CXXMethodDecl>(FD))
      ParamPositions.try_emplace(FD, 0);
    unsigned ParamPos = ParamPositions.size();
    for (const ParmVarDecl *P : FD->parameters()) {
      ParamPositions.try_emplace(P->getCanonicalDecl(), ParamPos);
      ++ParamPos;
    }
    for (const auto *Attr : FD->specific_attrs<OMPDeclareSimdDeclAttr>()) {
      llvm::SmallVector<ParamAttrTy, 8> ParamAttrs(ParamPositions.size());
      // Mark uniform parameters.
      for (const Expr *E : Attr->uniforms()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[FD];
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          auto It = ParamPositions.find(PVD);
          assert(It != ParamPositions.end() && "Function parameter not found");
          Pos = It->second;
        }
        ParamAttrs[Pos].Kind = Uniform;
      }
      // Get alignment info.
      auto *NI = Attr->alignments_begin();
      for (const Expr *E : Attr->aligneds()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        QualType ParmTy;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[FD];
          ParmTy = E->getType();
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          auto It = ParamPositions.find(PVD);
          assert(It != ParamPositions.end() && "Function parameter not found");
          Pos = It->second;
          ParmTy = PVD->getType();
        }
        ParamAttrs[Pos].Alignment =
            (*NI)
                ? (*NI)->EvaluateKnownConstInt(C)
                : llvm::APSInt::getUnsigned(
                      C.toCharUnitsFromBits(C.getOpenMPDefaultSimdAlign(ParmTy))
                          .getQuantity());
        ++NI;
      }
      // Mark linear parameters.
      auto *SI = Attr->steps_begin();
      auto *MI = Attr->modifiers_begin();
      for (const Expr *E : Attr->linears()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        bool IsReferenceType = false;
        // Rescaling factor needed to compute the linear parameter
        // value in the mangled name.
        unsigned PtrRescalingFactor = 1;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[FD];
          auto *P = cast<PointerType>(E->getType());
          PtrRescalingFactor = CGM.getContext()
                                   .getTypeSizeInChars(P->getPointeeType())
                                   .getQuantity();
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          auto It = ParamPositions.find(PVD);
          assert(It != ParamPositions.end() && "Function parameter not found");
          Pos = It->second;
          if (auto *P = dyn_cast<PointerType>(PVD->getType()))
            PtrRescalingFactor = CGM.getContext()
                                     .getTypeSizeInChars(P->getPointeeType())
                                     .getQuantity();
          else if (PVD->getType()->isReferenceType()) {
            IsReferenceType = true;
            PtrRescalingFactor =
                CGM.getContext()
                    .getTypeSizeInChars(PVD->getType().getNonReferenceType())
                    .getQuantity();
          }
        }
        ParamAttrTy &ParamAttr = ParamAttrs[Pos];
        if (*MI == OMPC_LINEAR_ref)
          ParamAttr.Kind = LinearRef;
        else if (*MI == OMPC_LINEAR_uval)
          ParamAttr.Kind = LinearUVal;
        else if (IsReferenceType)
          ParamAttr.Kind = LinearVal;
        else
          ParamAttr.Kind = Linear;
        // Assuming a stride of 1, for `linear` without modifiers.
        ParamAttr.StrideOrArg = llvm::APSInt::getUnsigned(1);
        if (*SI) {
          Expr::EvalResult Result;
          if (!(*SI)->EvaluateAsInt(Result, C, Expr::SE_AllowSideEffects)) {
            if (const auto *DRE =
                    cast<DeclRefExpr>((*SI)->IgnoreParenImpCasts())) {
              if (const auto *StridePVD =
                      dyn_cast<ParmVarDecl>(DRE->getDecl())) {
                ParamAttr.HasVarStride = true;
                auto It = ParamPositions.find(StridePVD->getCanonicalDecl());
                assert(It != ParamPositions.end() &&
                       "Function parameter not found");
                ParamAttr.StrideOrArg = llvm::APSInt::getUnsigned(It->second);
              }
            }
          } else {
            ParamAttr.StrideOrArg = Result.Val.getInt();
          }
        }
        // If we are using a linear clause on a pointer, we need to
        // rescale the value of linear_step with the byte size of the
        // pointee type.
        if (!ParamAttr.HasVarStride &&
            (ParamAttr.Kind == Linear || ParamAttr.Kind == LinearRef))
          ParamAttr.StrideOrArg = ParamAttr.StrideOrArg * PtrRescalingFactor;
        ++SI;
        ++MI;
      }
      llvm::APSInt VLENVal;
      SourceLocation ExprLoc;
      const Expr *VLENExpr = Attr->getSimdlen();
      if (VLENExpr) {
        VLENVal = VLENExpr->EvaluateKnownConstInt(C);
        ExprLoc = VLENExpr->getExprLoc();
      }
      OMPDeclareSimdDeclAttr::BranchStateTy State = Attr->getBranchState();
      if (CGM.getTriple().isX86()) {
        emitX86DeclareSimdFunction(FD, Fn, VLENVal, ParamAttrs, State);
      } else if (CGM.getTriple().getArch() == llvm::Triple::aarch64) {
        unsigned VLEN = VLENVal.getExtValue();
        StringRef MangledName = Fn->getName();
        if (CGM.getTarget().hasFeature("sve"))
          emitAArch64DeclareSimdFunction(CGM, FD, VLEN, ParamAttrs, State,
                                         MangledName, 's', 128, Fn, ExprLoc);
        else if (CGM.getTarget().hasFeature("neon"))
          emitAArch64DeclareSimdFunction(CGM, FD, VLEN, ParamAttrs, State,
                                         MangledName, 'n', 128, Fn, ExprLoc);
      }
    }
    FD = FD->getPreviousDecl();
  }
}

namespace {
/// Cleanup action for doacross support.
class DoacrossCleanupTy final : public EHScopeStack::Cleanup {
public:
  static const int DoacrossFinArgs = 2;

private:
  llvm::FunctionCallee RTLFn;
  llvm::Value *Args[DoacrossFinArgs];

public:
  DoacrossCleanupTy(llvm::FunctionCallee RTLFn,
                    ArrayRef<llvm::Value *> CallArgs)
      : RTLFn(RTLFn) {
    assert(CallArgs.size() == DoacrossFinArgs);
    std::copy(CallArgs.begin(), CallArgs.end(), std::begin(Args));
  }
  void Emit(CodeGenFunction &CGF, Flags /*flags*/) override {
    if (!CGF.HaveInsertPoint())
      return;
    CGF.EmitRuntimeCall(RTLFn, Args);
  }
};
} // namespace

void CGOpenMPRuntime::emitDoacrossInit(CodeGenFunction &CGF,
                                       const OMPLoopDirective &D,
                                       ArrayRef<Expr *> NumIterations) {
  if (!CGF.HaveInsertPoint())
    return;

  ASTContext &C = CGM.getContext();
  QualType Int64Ty = C.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/true);
  RecordDecl *RD;
  if (KmpDimTy.isNull()) {
    // Build struct kmp_dim {  // loop bounds info casted to kmp_int64
    //  kmp_int64 lo; // lower
    //  kmp_int64 up; // upper
    //  kmp_int64 st; // stride
    // };
    RD = C.buildImplicitRecord("kmp_dim");
    RD->startDefinition();
    addFieldToRecordDecl(C, RD, Int64Ty);
    addFieldToRecordDecl(C, RD, Int64Ty);
    addFieldToRecordDecl(C, RD, Int64Ty);
    RD->completeDefinition();
    KmpDimTy = C.getRecordType(RD);
  } else {
    RD = cast<RecordDecl>(KmpDimTy->getAsTagDecl());
  }
  llvm::APInt Size(/*numBits=*/32, NumIterations.size());
  QualType ArrayTy = C.getConstantArrayType(KmpDimTy, Size, nullptr,
                                            ArraySizeModifier::Normal, 0);

  Address DimsAddr = CGF.CreateMemTemp(ArrayTy, "dims");
  CGF.EmitNullInitialization(DimsAddr, ArrayTy);
  enum { LowerFD = 0, UpperFD, StrideFD };
  // Fill dims with data.
  for (unsigned I = 0, E = NumIterations.size(); I < E; ++I) {
    LValue DimsLVal = CGF.MakeAddrLValue(
        CGF.Builder.CreateConstArrayGEP(DimsAddr, I), KmpDimTy);
    // dims.upper = num_iterations;
    LValue UpperLVal = CGF.EmitLValueForField(
        DimsLVal, *std::next(RD->field_begin(), UpperFD));
    llvm::Value *NumIterVal = CGF.EmitScalarConversion(
        CGF.EmitScalarExpr(NumIterations[I]), NumIterations[I]->getType(),
        Int64Ty, NumIterations[I]->getExprLoc());
    CGF.EmitStoreOfScalar(NumIterVal, UpperLVal);
    // dims.stride = 1;
    LValue StrideLVal = CGF.EmitLValueForField(
        DimsLVal, *std::next(RD->field_begin(), StrideFD));
    CGF.EmitStoreOfScalar(llvm::ConstantInt::getSigned(CGM.Int64Ty, /*V=*/1),
                          StrideLVal);
  }

  // Build call void __kmpc_doacross_init(ident_t *loc, kmp_int32 gtid,
  // kmp_int32 num_dims, struct kmp_dim * dims);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, D.getBeginLoc()),
      getThreadID(CGF, D.getBeginLoc()),
      llvm::ConstantInt::getSigned(CGM.Int32Ty, NumIterations.size()),
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          CGF.Builder.CreateConstArrayGEP(DimsAddr, 0).emitRawPointer(CGF),
          CGM.VoidPtrTy)};

  llvm::FunctionCallee RTLFn = OMPBuilder.getOrCreateRuntimeFunction(
      CGM.getModule(), OMPRTL___kmpc_doacross_init);
  CGF.EmitRuntimeCall(RTLFn, Args);
  llvm::Value *FiniArgs[DoacrossCleanupTy::DoacrossFinArgs] = {
      emitUpdateLocation(CGF, D.getEndLoc()), getThreadID(CGF, D.getEndLoc())};
  llvm::FunctionCallee FiniRTLFn = OMPBuilder.getOrCreateRuntimeFunction(
      CGM.getModule(), OMPRTL___kmpc_doacross_fini);
  CGF.EHStack.pushCleanup<DoacrossCleanupTy>(NormalAndEHCleanup, FiniRTLFn,
                                             llvm::ArrayRef(FiniArgs));
}

template <typename T>
static void EmitDoacrossOrdered(CodeGenFunction &CGF, CodeGenModule &CGM,
                                const T *C, llvm::Value *ULoc,
                                llvm::Value *ThreadID) {
  QualType Int64Ty =
      CGM.getContext().getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/1);
  llvm::APInt Size(/*numBits=*/32, C->getNumLoops());
  QualType ArrayTy = CGM.getContext().getConstantArrayType(
      Int64Ty, Size, nullptr, ArraySizeModifier::Normal, 0);
  Address CntAddr = CGF.CreateMemTemp(ArrayTy, ".cnt.addr");
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I) {
    const Expr *CounterVal = C->getLoopData(I);
    assert(CounterVal);
    llvm::Value *CntVal = CGF.EmitScalarConversion(
        CGF.EmitScalarExpr(CounterVal), CounterVal->getType(), Int64Ty,
        CounterVal->getExprLoc());
    CGF.EmitStoreOfScalar(CntVal, CGF.Builder.CreateConstArrayGEP(CntAddr, I),
                          /*Volatile=*/false, Int64Ty);
  }
  llvm::Value *Args[] = {
      ULoc, ThreadID,
      CGF.Builder.CreateConstArrayGEP(CntAddr, 0).emitRawPointer(CGF)};
  llvm::FunctionCallee RTLFn;
  llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
  OMPDoacrossKind<T> ODK;
  if (ODK.isSource(C)) {
    RTLFn = OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                                  OMPRTL___kmpc_doacross_post);
  } else {
    assert(ODK.isSink(C) && "Expect sink modifier.");
    RTLFn = OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(),
                                                  OMPRTL___kmpc_doacross_wait);
  }
  CGF.EmitRuntimeCall(RTLFn, Args);
}

void CGOpenMPRuntime::emitDoacrossOrdered(CodeGenFunction &CGF,
                                          const OMPDependClause *C) {
  return EmitDoacrossOrdered<OMPDependClause>(
      CGF, CGM, C, emitUpdateLocation(CGF, C->getBeginLoc()),
      getThreadID(CGF, C->getBeginLoc()));
}

void CGOpenMPRuntime::emitDoacrossOrdered(CodeGenFunction &CGF,
                                          const OMPDoacrossClause *C) {
  return EmitDoacrossOrdered<OMPDoacrossClause>(
      CGF, CGM, C, emitUpdateLocation(CGF, C->getBeginLoc()),
      getThreadID(CGF, C->getBeginLoc()));
}

void CGOpenMPRuntime::emitCall(CodeGenFunction &CGF, SourceLocation Loc,
                               llvm::FunctionCallee Callee,
                               ArrayRef<llvm::Value *> Args) const {
  assert(Loc.isValid() && "Outlined function call location must be valid.");
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(CGF, Loc);

  if (auto *Fn = dyn_cast<llvm::Function>(Callee.getCallee())) {
    if (Fn->doesNotThrow()) {
      CGF.EmitNounwindRuntimeCall(Fn, Args);
      return;
    }
  }
  CGF.EmitRuntimeCall(Callee, Args);
}

void CGOpenMPRuntime::emitOutlinedFunctionCall(
    CodeGenFunction &CGF, SourceLocation Loc, llvm::FunctionCallee OutlinedFn,
    ArrayRef<llvm::Value *> Args) const {
  emitCall(CGF, Loc, OutlinedFn, Args);
}

void CGOpenMPRuntime::emitFunctionProlog(CodeGenFunction &CGF, const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    if (OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(FD))
      HasEmittedDeclareTargetRegion = true;
}

Address CGOpenMPRuntime::getParameterAddress(CodeGenFunction &CGF,
                                             const VarDecl *NativeParam,
                                             const VarDecl *TargetParam) const {
  return CGF.GetAddrOfLocalVar(NativeParam);
}

/// Return allocator value from expression, or return a null allocator (default
/// when no allocator specified).
static llvm::Value *getAllocatorVal(CodeGenFunction &CGF,
                                    const Expr *Allocator) {
  llvm::Value *AllocVal;
  if (Allocator) {
    AllocVal = CGF.EmitScalarExpr(Allocator);
    // According to the standard, the original allocator type is a enum
    // (integer). Convert to pointer type, if required.
    AllocVal = CGF.EmitScalarConversion(AllocVal, Allocator->getType(),
                                        CGF.getContext().VoidPtrTy,
                                        Allocator->getExprLoc());
  } else {
    // If no allocator specified, it defaults to the null allocator.
    AllocVal = llvm::Constant::getNullValue(
        CGF.CGM.getTypes().ConvertType(CGF.getContext().VoidPtrTy));
  }
  return AllocVal;
}

/// Return the alignment from an allocate directive if present.
static llvm::Value *getAlignmentValue(CodeGenModule &CGM, const VarDecl *VD) {
  std::optional<CharUnits> AllocateAlignment = CGM.getOMPAllocateAlignment(VD);

  if (!AllocateAlignment)
    return nullptr;

  return llvm::ConstantInt::get(CGM.SizeTy, AllocateAlignment->getQuantity());
}

Address CGOpenMPRuntime::getAddressOfLocalVariable(CodeGenFunction &CGF,
                                                   const VarDecl *VD) {
  if (!VD)
    return Address::invalid();
  Address UntiedAddr = Address::invalid();
  Address UntiedRealAddr = Address::invalid();
  auto It = FunctionToUntiedTaskStackMap.find(CGF.CurFn);
  if (It != FunctionToUntiedTaskStackMap.end()) {
    const UntiedLocalVarsAddressesMap &UntiedData =
        UntiedLocalVarsStack[It->second];
    auto I = UntiedData.find(VD);
    if (I != UntiedData.end()) {
      UntiedAddr = I->second.first;
      UntiedRealAddr = I->second.second;
    }
  }
  const VarDecl *CVD = VD->getCanonicalDecl();
  if (CVD->hasAttr<OMPAllocateDeclAttr>()) {
    // Use the default allocation.
    if (!isAllocatableDecl(VD))
      return UntiedAddr;
    llvm::Value *Size;
    CharUnits Align = CGM.getContext().getDeclAlign(CVD);
    if (CVD->getType()->isVariablyModifiedType()) {
      Size = CGF.getTypeSize(CVD->getType());
      // Align the size: ((size + align - 1) / align) * align
      Size = CGF.Builder.CreateNUWAdd(
          Size, CGM.getSize(Align - CharUnits::fromQuantity(1)));
      Size = CGF.Builder.CreateUDiv(Size, CGM.getSize(Align));
      Size = CGF.Builder.CreateNUWMul(Size, CGM.getSize(Align));
    } else {
      CharUnits Sz = CGM.getContext().getTypeSizeInChars(CVD->getType());
      Size = CGM.getSize(Sz.alignTo(Align));
    }
    llvm::Value *ThreadID = getThreadID(CGF, CVD->getBeginLoc());
    const auto *AA = CVD->getAttr<OMPAllocateDeclAttr>();
    const Expr *Allocator = AA->getAllocator();
    llvm::Value *AllocVal = getAllocatorVal(CGF, Allocator);
    llvm::Value *Alignment = getAlignmentValue(CGM, CVD);
    SmallVector<llvm::Value *, 4> Args;
    Args.push_back(ThreadID);
    if (Alignment)
      Args.push_back(Alignment);
    Args.push_back(Size);
    Args.push_back(AllocVal);
    llvm::omp::RuntimeFunction FnID =
        Alignment ? OMPRTL___kmpc_aligned_alloc : OMPRTL___kmpc_alloc;
    llvm::Value *Addr = CGF.EmitRuntimeCall(
        OMPBuilder.getOrCreateRuntimeFunction(CGM.getModule(), FnID), Args,
        getName({CVD->getName(), ".void.addr"}));
    llvm::FunctionCallee FiniRTLFn = OMPBuilder.getOrCreateRuntimeFunction(
        CGM.getModule(), OMPRTL___kmpc_free);
    QualType Ty = CGM.getContext().getPointerType(CVD->getType());
    Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        Addr, CGF.ConvertTypeForMem(Ty), getName({CVD->getName(), ".addr"}));
    if (UntiedAddr.isValid())
      CGF.EmitStoreOfScalar(Addr, UntiedAddr, /*Volatile=*/false, Ty);

    // Cleanup action for allocate support.
    class OMPAllocateCleanupTy final : public EHScopeStack::Cleanup {
      llvm::FunctionCallee RTLFn;
      SourceLocation::UIntTy LocEncoding;
      Address Addr;
      const Expr *AllocExpr;

    public:
      OMPAllocateCleanupTy(llvm::FunctionCallee RTLFn,
                           SourceLocation::UIntTy LocEncoding, Address Addr,
                           const Expr *AllocExpr)
          : RTLFn(RTLFn), LocEncoding(LocEncoding), Addr(Addr),
            AllocExpr(AllocExpr) {}
      void Emit(CodeGenFunction &CGF, Flags /*flags*/) override {
        if (!CGF.HaveInsertPoint())
          return;
        llvm::Value *Args[3];
        Args[0] = CGF.CGM.getOpenMPRuntime().getThreadID(
            CGF, SourceLocation::getFromRawEncoding(LocEncoding));
        Args[1] = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            Addr.emitRawPointer(CGF), CGF.VoidPtrTy);
        llvm::Value *AllocVal = getAllocatorVal(CGF, AllocExpr);
        Args[2] = AllocVal;
        CGF.EmitRuntimeCall(RTLFn, Args);
      }
    };
    Address VDAddr =
        UntiedRealAddr.isValid()
            ? UntiedRealAddr
            : Address(Addr, CGF.ConvertTypeForMem(CVD->getType()), Align);
    CGF.EHStack.pushCleanup<OMPAllocateCleanupTy>(
        NormalAndEHCleanup, FiniRTLFn, CVD->getLocation().getRawEncoding(),
        VDAddr, Allocator);
    if (UntiedRealAddr.isValid())
      if (auto *Region =
              dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
        Region->emitUntiedSwitch(CGF);
    return VDAddr;
  }
  return UntiedAddr;
}

bool CGOpenMPRuntime::isLocalVarInUntiedTask(CodeGenFunction &CGF,
                                             const VarDecl *VD) const {
  auto It = FunctionToUntiedTaskStackMap.find(CGF.CurFn);
  if (It == FunctionToUntiedTaskStackMap.end())
    return false;
  return UntiedLocalVarsStack[It->second].count(VD) > 0;
}

CGOpenMPRuntime::NontemporalDeclsRAII::NontemporalDeclsRAII(
    CodeGenModule &CGM, const OMPLoopDirective &S)
    : CGM(CGM), NeedToPush(S.hasClausesOfKind<OMPNontemporalClause>()) {
  assert(CGM.getLangOpts().OpenMP && "Not in OpenMP mode.");
  if (!NeedToPush)
    return;
  NontemporalDeclsSet &DS =
      CGM.getOpenMPRuntime().NontemporalDeclsStack.emplace_back();
  for (const auto *C : S.getClausesOfKind<OMPNontemporalClause>()) {
    for (const Stmt *Ref : C->private_refs()) {
      const auto *SimpleRefExpr = cast<Expr>(Ref)->IgnoreParenImpCasts();
      const ValueDecl *VD;
      if (const auto *DRE = dyn_cast<DeclRefExpr>(SimpleRefExpr)) {
        VD = DRE->getDecl();
      } else {
        const auto *ME = cast<MemberExpr>(SimpleRefExpr);
        assert((ME->isImplicitCXXThis() ||
                isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts())) &&
               "Expected member of current class.");
        VD = ME->getMemberDecl();
      }
      DS.insert(VD);
    }
  }
}

CGOpenMPRuntime::NontemporalDeclsRAII::~NontemporalDeclsRAII() {
  if (!NeedToPush)
    return;
  CGM.getOpenMPRuntime().NontemporalDeclsStack.pop_back();
}

CGOpenMPRuntime::UntiedTaskLocalDeclsRAII::UntiedTaskLocalDeclsRAII(
    CodeGenFunction &CGF,
    const llvm::MapVector<CanonicalDeclPtr<const VarDecl>,
                          std::pair<Address, Address>> &LocalVars)
    : CGM(CGF.CGM), NeedToPush(!LocalVars.empty()) {
  if (!NeedToPush)
    return;
  CGM.getOpenMPRuntime().FunctionToUntiedTaskStackMap.try_emplace(
      CGF.CurFn, CGM.getOpenMPRuntime().UntiedLocalVarsStack.size());
  CGM.getOpenMPRuntime().UntiedLocalVarsStack.push_back(LocalVars);
}

CGOpenMPRuntime::UntiedTaskLocalDeclsRAII::~UntiedTaskLocalDeclsRAII() {
  if (!NeedToPush)
    return;
  CGM.getOpenMPRuntime().UntiedLocalVarsStack.pop_back();
}

bool CGOpenMPRuntime::isNontemporalDecl(const ValueDecl *VD) const {
  assert(CGM.getLangOpts().OpenMP && "Not in OpenMP mode.");

  return llvm::any_of(
      CGM.getOpenMPRuntime().NontemporalDeclsStack,
      [VD](const NontemporalDeclsSet &Set) { return Set.contains(VD); });
}

void CGOpenMPRuntime::LastprivateConditionalRAII::tryToDisableInnerAnalysis(
    const OMPExecutableDirective &S,
    llvm::DenseSet<CanonicalDeclPtr<const Decl>> &NeedToAddForLPCsAsDisabled)
    const {
  llvm::DenseSet<CanonicalDeclPtr<const Decl>> NeedToCheckForLPCs;
  // Vars in target/task regions must be excluded completely.
  if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()) ||
      isOpenMPTaskingDirective(S.getDirectiveKind())) {
    SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
    getOpenMPCaptureRegions(CaptureRegions, S.getDirectiveKind());
    const CapturedStmt *CS = S.getCapturedStmt(CaptureRegions.front());
    for (const CapturedStmt::Capture &Cap : CS->captures()) {
      if (Cap.capturesVariable() || Cap.capturesVariableByCopy())
        NeedToCheckForLPCs.insert(Cap.getCapturedVar());
    }
  }
  // Exclude vars in private clauses.
  for (const auto *C : S.getClausesOfKind<OMPPrivateClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      NeedToCheckForLPCs.insert(DRE->getDecl());
    }
  }
  for (const auto *C : S.getClausesOfKind<OMPFirstprivateClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      NeedToCheckForLPCs.insert(DRE->getDecl());
    }
  }
  for (const auto *C : S.getClausesOfKind<OMPLastprivateClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      NeedToCheckForLPCs.insert(DRE->getDecl());
    }
  }
  for (const auto *C : S.getClausesOfKind<OMPReductionClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      NeedToCheckForLPCs.insert(DRE->getDecl());
    }
  }
  for (const auto *C : S.getClausesOfKind<OMPLinearClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      NeedToCheckForLPCs.insert(DRE->getDecl());
    }
  }
  for (const Decl *VD : NeedToCheckForLPCs) {
    for (const LastprivateConditionalData &Data :
         llvm::reverse(CGM.getOpenMPRuntime().LastprivateConditionalStack)) {
      if (Data.DeclToUniqueName.count(VD) > 0) {
        if (!Data.Disabled)
          NeedToAddForLPCsAsDisabled.insert(VD);
        break;
      }
    }
  }
}

CGOpenMPRuntime::LastprivateConditionalRAII::LastprivateConditionalRAII(
    CodeGenFunction &CGF, const OMPExecutableDirective &S, LValue IVLVal)
    : CGM(CGF.CGM),
      Action((CGM.getLangOpts().OpenMP >= 50 &&
              llvm::any_of(S.getClausesOfKind<OMPLastprivateClause>(),
                           [](const OMPLastprivateClause *C) {
                             return C->getKind() ==
                                    OMPC_LASTPRIVATE_conditional;
                           }))
                 ? ActionToDo::PushAsLastprivateConditional
                 : ActionToDo::DoNotPush) {
  assert(CGM.getLangOpts().OpenMP && "Not in OpenMP mode.");
  if (CGM.getLangOpts().OpenMP < 50 || Action == ActionToDo::DoNotPush)
    return;
  assert(Action == ActionToDo::PushAsLastprivateConditional &&
         "Expected a push action.");
  LastprivateConditionalData &Data =
      CGM.getOpenMPRuntime().LastprivateConditionalStack.emplace_back();
  for (const auto *C : S.getClausesOfKind<OMPLastprivateClause>()) {
    if (C->getKind() != OMPC_LASTPRIVATE_conditional)
      continue;

    for (const Expr *Ref : C->varlists()) {
      Data.DeclToUniqueName.insert(std::make_pair(
          cast<DeclRefExpr>(Ref->IgnoreParenImpCasts())->getDecl(),
          SmallString<16>(generateUniqueName(CGM, "pl_cond", Ref))));
    }
  }
  Data.IVLVal = IVLVal;
  Data.Fn = CGF.CurFn;
}

CGOpenMPRuntime::LastprivateConditionalRAII::LastprivateConditionalRAII(
    CodeGenFunction &CGF, const OMPExecutableDirective &S)
    : CGM(CGF.CGM), Action(ActionToDo::DoNotPush) {
  assert(CGM.getLangOpts().OpenMP && "Not in OpenMP mode.");
  if (CGM.getLangOpts().OpenMP < 50)
    return;
  llvm::DenseSet<CanonicalDeclPtr<const Decl>> NeedToAddForLPCsAsDisabled;
  tryToDisableInnerAnalysis(S, NeedToAddForLPCsAsDisabled);
  if (!NeedToAddForLPCsAsDisabled.empty()) {
    Action = ActionToDo::DisableLastprivateConditional;
    LastprivateConditionalData &Data =
        CGM.getOpenMPRuntime().LastprivateConditionalStack.emplace_back();
    for (const Decl *VD : NeedToAddForLPCsAsDisabled)
      Data.DeclToUniqueName.insert(std::make_pair(VD, SmallString<16>()));
    Data.Fn = CGF.CurFn;
    Data.Disabled = true;
  }
}

CGOpenMPRuntime::LastprivateConditionalRAII
CGOpenMPRuntime::LastprivateConditionalRAII::disable(
    CodeGenFunction &CGF, const OMPExecutableDirective &S) {
  return LastprivateConditionalRAII(CGF, S);
}

CGOpenMPRuntime::LastprivateConditionalRAII::~LastprivateConditionalRAII() {
  if (CGM.getLangOpts().OpenMP < 50)
    return;
  if (Action == ActionToDo::DisableLastprivateConditional) {
    assert(CGM.getOpenMPRuntime().LastprivateConditionalStack.back().Disabled &&
           "Expected list of disabled private vars.");
    CGM.getOpenMPRuntime().LastprivateConditionalStack.pop_back();
  }
  if (Action == ActionToDo::PushAsLastprivateConditional) {
    assert(
        !CGM.getOpenMPRuntime().LastprivateConditionalStack.back().Disabled &&
        "Expected list of lastprivate conditional vars.");
    CGM.getOpenMPRuntime().LastprivateConditionalStack.pop_back();
  }
}

Address CGOpenMPRuntime::emitLastprivateConditionalInit(CodeGenFunction &CGF,
                                                        const VarDecl *VD) {
  ASTContext &C = CGM.getContext();
  auto I = LastprivateConditionalToTypes.find(CGF.CurFn);
  if (I == LastprivateConditionalToTypes.end())
    I = LastprivateConditionalToTypes.try_emplace(CGF.CurFn).first;
  QualType NewType;
  const FieldDecl *VDField;
  const FieldDecl *FiredField;
  LValue BaseLVal;
  auto VI = I->getSecond().find(VD);
  if (VI == I->getSecond().end()) {
    RecordDecl *RD = C.buildImplicitRecord("lasprivate.conditional");
    RD->startDefinition();
    VDField = addFieldToRecordDecl(C, RD, VD->getType().getNonReferenceType());
    FiredField = addFieldToRecordDecl(C, RD, C.CharTy);
    RD->completeDefinition();
    NewType = C.getRecordType(RD);
    Address Addr = CGF.CreateMemTemp(NewType, C.getDeclAlign(VD), VD->getName());
    BaseLVal = CGF.MakeAddrLValue(Addr, NewType, AlignmentSource::Decl);
    I->getSecond().try_emplace(VD, NewType, VDField, FiredField, BaseLVal);
  } else {
    NewType = std::get<0>(VI->getSecond());
    VDField = std::get<1>(VI->getSecond());
    FiredField = std::get<2>(VI->getSecond());
    BaseLVal = std::get<3>(VI->getSecond());
  }
  LValue FiredLVal =
      CGF.EmitLValueForField(BaseLVal, FiredField);
  CGF.EmitStoreOfScalar(
      llvm::ConstantInt::getNullValue(CGF.ConvertTypeForMem(C.CharTy)),
      FiredLVal);
  return CGF.EmitLValueForField(BaseLVal, VDField).getAddress();
}

namespace {
/// Checks if the lastprivate conditional variable is referenced in LHS.
class LastprivateConditionalRefChecker final
    : public ConstStmtVisitor<LastprivateConditionalRefChecker, bool> {
  ArrayRef<CGOpenMPRuntime::LastprivateConditionalData> LPM;
  const Expr *FoundE = nullptr;
  const Decl *FoundD = nullptr;
  StringRef UniqueDeclName;
  LValue IVLVal;
  llvm::Function *FoundFn = nullptr;
  SourceLocation Loc;

public:
  bool VisitDeclRefExpr(const DeclRefExpr *E) {
    for (const CGOpenMPRuntime::LastprivateConditionalData &D :
         llvm::reverse(LPM)) {
      auto It = D.DeclToUniqueName.find(E->getDecl());
      if (It == D.DeclToUniqueName.end())
        continue;
      if (D.Disabled)
        return false;
      FoundE = E;
      FoundD = E->getDecl()->getCanonicalDecl();
      UniqueDeclName = It->second;
      IVLVal = D.IVLVal;
      FoundFn = D.Fn;
      break;
    }
    return FoundE == E;
  }
  bool VisitMemberExpr(const MemberExpr *E) {
    if (!CodeGenFunction::IsWrappedCXXThis(E->getBase()))
      return false;
    for (const CGOpenMPRuntime::LastprivateConditionalData &D :
         llvm::reverse(LPM)) {
      auto It = D.DeclToUniqueName.find(E->getMemberDecl());
      if (It == D.DeclToUniqueName.end())
        continue;
      if (D.Disabled)
        return false;
      FoundE = E;
      FoundD = E->getMemberDecl()->getCanonicalDecl();
      UniqueDeclName = It->second;
      IVLVal = D.IVLVal;
      FoundFn = D.Fn;
      break;
    }
    return FoundE == E;
  }
  bool VisitStmt(const Stmt *S) {
    for (const Stmt *Child : S->children()) {
      if (!Child)
        continue;
      if (const auto *E = dyn_cast<Expr>(Child))
        if (!E->isGLValue())
          continue;
      if (Visit(Child))
        return true;
    }
    return false;
  }
  explicit LastprivateConditionalRefChecker(
      ArrayRef<CGOpenMPRuntime::LastprivateConditionalData> LPM)
      : LPM(LPM) {}
  std::tuple<const Expr *, const Decl *, StringRef, LValue, llvm::Function *>
  getFoundData() const {
    return std::make_tuple(FoundE, FoundD, UniqueDeclName, IVLVal, FoundFn);
  }
};
} // namespace

void CGOpenMPRuntime::emitLastprivateConditionalUpdate(CodeGenFunction &CGF,
                                                       LValue IVLVal,
                                                       StringRef UniqueDeclName,
                                                       LValue LVal,
                                                       SourceLocation Loc) {
  // Last updated loop counter for the lastprivate conditional var.
  // int<xx> last_iv = 0;
  llvm::Type *LLIVTy = CGF.ConvertTypeForMem(IVLVal.getType());
  llvm::Constant *LastIV = OMPBuilder.getOrCreateInternalVariable(
      LLIVTy, getName({UniqueDeclName, "iv"}));
  cast<llvm::GlobalVariable>(LastIV)->setAlignment(
      IVLVal.getAlignment().getAsAlign());
  LValue LastIVLVal =
      CGF.MakeNaturalAlignRawAddrLValue(LastIV, IVLVal.getType());

  // Last value of the lastprivate conditional.
  // decltype(priv_a) last_a;
  llvm::GlobalVariable *Last = OMPBuilder.getOrCreateInternalVariable(
      CGF.ConvertTypeForMem(LVal.getType()), UniqueDeclName);
  cast<llvm::GlobalVariable>(Last)->setAlignment(
      LVal.getAlignment().getAsAlign());
  LValue LastLVal =
      CGF.MakeRawAddrLValue(Last, LVal.getType(), LVal.getAlignment());

  // Global loop counter. Required to handle inner parallel-for regions.
  // iv
  llvm::Value *IVVal = CGF.EmitLoadOfScalar(IVLVal, Loc);

  // #pragma omp critical(a)
  // if (last_iv <= iv) {
  //   last_iv = iv;
  //   last_a = priv_a;
  // }
  auto &&CodeGen = [&LastIVLVal, &IVLVal, IVVal, &LVal, &LastLVal,
                    Loc](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    llvm::Value *LastIVVal = CGF.EmitLoadOfScalar(LastIVLVal, Loc);
    // (last_iv <= iv) ? Check if the variable is updated and store new
    // value in global var.
    llvm::Value *CmpRes;
    if (IVLVal.getType()->isSignedIntegerType()) {
      CmpRes = CGF.Builder.CreateICmpSLE(LastIVVal, IVVal);
    } else {
      assert(IVLVal.getType()->isUnsignedIntegerType() &&
             "Loop iteration variable must be integer.");
      CmpRes = CGF.Builder.CreateICmpULE(LastIVVal, IVVal);
    }
    llvm::BasicBlock *ThenBB = CGF.createBasicBlock("lp_cond_then");
    llvm::BasicBlock *ExitBB = CGF.createBasicBlock("lp_cond_exit");
    CGF.Builder.CreateCondBr(CmpRes, ThenBB, ExitBB);
    // {
    CGF.EmitBlock(ThenBB);

    //   last_iv = iv;
    CGF.EmitStoreOfScalar(IVVal, LastIVLVal);

    //   last_a = priv_a;
    switch (CGF.getEvaluationKind(LVal.getType())) {
    case TEK_Scalar: {
      llvm::Value *PrivVal = CGF.EmitLoadOfScalar(LVal, Loc);
      CGF.EmitStoreOfScalar(PrivVal, LastLVal);
      break;
    }
    case TEK_Complex: {
      CodeGenFunction::ComplexPairTy PrivVal = CGF.EmitLoadOfComplex(LVal, Loc);
      CGF.EmitStoreOfComplex(PrivVal, LastLVal, /*isInit=*/false);
      break;
    }
    case TEK_Aggregate:
      llvm_unreachable(
          "Aggregates are not supported in lastprivate conditional.");
    }
    // }
    CGF.EmitBranch(ExitBB);
    // There is no need to emit line number for unconditional branch.
    (void)ApplyDebugLocation::CreateEmpty(CGF);
    CGF.EmitBlock(ExitBB, /*IsFinished=*/true);
  };

  if (CGM.getLangOpts().OpenMPSimd) {
    // Do not emit as a critical region as no parallel region could be emitted.
    RegionCodeGenTy ThenRCG(CodeGen);
    ThenRCG(CGF);
  } else {
    emitCriticalRegion(CGF, UniqueDeclName, CodeGen, Loc);
  }
}

void CGOpenMPRuntime::checkAndEmitLastprivateConditional(CodeGenFunction &CGF,
                                                         const Expr *LHS) {
  if (CGF.getLangOpts().OpenMP < 50 || LastprivateConditionalStack.empty())
    return;
  LastprivateConditionalRefChecker Checker(LastprivateConditionalStack);
  if (!Checker.Visit(LHS))
    return;
  const Expr *FoundE;
  const Decl *FoundD;
  StringRef UniqueDeclName;
  LValue IVLVal;
  llvm::Function *FoundFn;
  std::tie(FoundE, FoundD, UniqueDeclName, IVLVal, FoundFn) =
      Checker.getFoundData();
  if (FoundFn != CGF.CurFn) {
    // Special codegen for inner parallel regions.
    // ((struct.lastprivate.conditional*)&priv_a)->Fired = 1;
    auto It = LastprivateConditionalToTypes[FoundFn].find(FoundD);
    assert(It != LastprivateConditionalToTypes[FoundFn].end() &&
           "Lastprivate conditional is not found in outer region.");
    QualType StructTy = std::get<0>(It->getSecond());
    const FieldDecl* FiredDecl = std::get<2>(It->getSecond());
    LValue PrivLVal = CGF.EmitLValue(FoundE);
    Address StructAddr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        PrivLVal.getAddress(),
        CGF.ConvertTypeForMem(CGF.getContext().getPointerType(StructTy)),
        CGF.ConvertTypeForMem(StructTy));
    LValue BaseLVal =
        CGF.MakeAddrLValue(StructAddr, StructTy, AlignmentSource::Decl);
    LValue FiredLVal = CGF.EmitLValueForField(BaseLVal, FiredDecl);
    CGF.EmitAtomicStore(RValue::get(llvm::ConstantInt::get(
                            CGF.ConvertTypeForMem(FiredDecl->getType()), 1)),
                        FiredLVal, llvm::AtomicOrdering::Unordered,
                        /*IsVolatile=*/true, /*isInit=*/false);
    return;
  }

  // Private address of the lastprivate conditional in the current context.
  // priv_a
  LValue LVal = CGF.EmitLValue(FoundE);
  emitLastprivateConditionalUpdate(CGF, IVLVal, UniqueDeclName, LVal,
                                   FoundE->getExprLoc());
}

void CGOpenMPRuntime::checkAndEmitSharedLastprivateConditional(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const llvm::DenseSet<CanonicalDeclPtr<const VarDecl>> &IgnoredDecls) {
  if (CGF.getLangOpts().OpenMP < 50 || LastprivateConditionalStack.empty())
    return;
  auto Range = llvm::reverse(LastprivateConditionalStack);
  auto It = llvm::find_if(
      Range, [](const LastprivateConditionalData &D) { return !D.Disabled; });
  if (It == Range.end() || It->Fn != CGF.CurFn)
    return;
  auto LPCI = LastprivateConditionalToTypes.find(It->Fn);
  assert(LPCI != LastprivateConditionalToTypes.end() &&
         "Lastprivates must be registered already.");
  SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
  getOpenMPCaptureRegions(CaptureRegions, D.getDirectiveKind());
  const CapturedStmt *CS = D.getCapturedStmt(CaptureRegions.back());
  for (const auto &Pair : It->DeclToUniqueName) {
    const auto *VD = cast<VarDecl>(Pair.first->getCanonicalDecl());
    if (!CS->capturesVariable(VD) || IgnoredDecls.contains(VD))
      continue;
    auto I = LPCI->getSecond().find(Pair.first);
    assert(I != LPCI->getSecond().end() &&
           "Lastprivate must be rehistered already.");
    // bool Cmp = priv_a.Fired != 0;
    LValue BaseLVal = std::get<3>(I->getSecond());
    LValue FiredLVal =
        CGF.EmitLValueForField(BaseLVal, std::get<2>(I->getSecond()));
    llvm::Value *Res = CGF.EmitLoadOfScalar(FiredLVal, D.getBeginLoc());
    llvm::Value *Cmp = CGF.Builder.CreateIsNotNull(Res);
    llvm::BasicBlock *ThenBB = CGF.createBasicBlock("lpc.then");
    llvm::BasicBlock *DoneBB = CGF.createBasicBlock("lpc.done");
    // if (Cmp) {
    CGF.Builder.CreateCondBr(Cmp, ThenBB, DoneBB);
    CGF.EmitBlock(ThenBB);
    Address Addr = CGF.GetAddrOfLocalVar(VD);
    LValue LVal;
    if (VD->getType()->isReferenceType())
      LVal = CGF.EmitLoadOfReferenceLValue(Addr, VD->getType(),
                                           AlignmentSource::Decl);
    else
      LVal = CGF.MakeAddrLValue(Addr, VD->getType().getNonReferenceType(),
                                AlignmentSource::Decl);
    emitLastprivateConditionalUpdate(CGF, It->IVLVal, Pair.second, LVal,
                                     D.getBeginLoc());
    auto AL = ApplyDebugLocation::CreateArtificial(CGF);
    CGF.EmitBlock(DoneBB, /*IsFinal=*/true);
    // }
  }
}

void CGOpenMPRuntime::emitLastprivateConditionalFinalUpdate(
    CodeGenFunction &CGF, LValue PrivLVal, const VarDecl *VD,
    SourceLocation Loc) {
  if (CGF.getLangOpts().OpenMP < 50)
    return;
  auto It = LastprivateConditionalStack.back().DeclToUniqueName.find(VD);
  assert(It != LastprivateConditionalStack.back().DeclToUniqueName.end() &&
         "Unknown lastprivate conditional variable.");
  StringRef UniqueName = It->second;
  llvm::GlobalVariable *GV = CGM.getModule().getNamedGlobal(UniqueName);
  // The variable was not updated in the region - exit.
  if (!GV)
    return;
  LValue LPLVal = CGF.MakeRawAddrLValue(
      GV, PrivLVal.getType().getNonReferenceType(), PrivLVal.getAlignment());
  llvm::Value *Res = CGF.EmitLoadOfScalar(LPLVal, Loc);
  CGF.EmitStoreOfScalar(Res, PrivLVal);
}

llvm::Function *CGOpenMPSIMDRuntime::emitParallelOutlinedFunction(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const RegionCodeGenTy &CodeGen) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Function *CGOpenMPSIMDRuntime::emitTeamsOutlinedFunction(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const RegionCodeGenTy &CodeGen) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Function *CGOpenMPSIMDRuntime::emitTaskOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    const VarDecl *PartIDVar, const VarDecl *TaskTVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen,
    bool Tied, unsigned &NumberOfParts) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitParallelCall(CodeGenFunction &CGF,
                                           SourceLocation Loc,
                                           llvm::Function *OutlinedFn,
                                           ArrayRef<llvm::Value *> CapturedVars,
                                           const Expr *IfCond,
                                           llvm::Value *NumThreads) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitCriticalRegion(
    CodeGenFunction &CGF, StringRef CriticalName,
    const RegionCodeGenTy &CriticalOpGen, SourceLocation Loc,
    const Expr *Hint) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitMasterRegion(CodeGenFunction &CGF,
                                           const RegionCodeGenTy &MasterOpGen,
                                           SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitMaskedRegion(CodeGenFunction &CGF,
                                           const RegionCodeGenTy &MasterOpGen,
                                           SourceLocation Loc,
                                           const Expr *Filter) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskyieldCall(CodeGenFunction &CGF,
                                            SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskgroupRegion(
    CodeGenFunction &CGF, const RegionCodeGenTy &TaskgroupOpGen,
    SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitSingleRegion(
    CodeGenFunction &CGF, const RegionCodeGenTy &SingleOpGen,
    SourceLocation Loc, ArrayRef<const Expr *> CopyprivateVars,
    ArrayRef<const Expr *> DestExprs, ArrayRef<const Expr *> SrcExprs,
    ArrayRef<const Expr *> AssignmentOps) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitOrderedRegion(CodeGenFunction &CGF,
                                            const RegionCodeGenTy &OrderedOpGen,
                                            SourceLocation Loc,
                                            bool IsThreads) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitBarrierCall(CodeGenFunction &CGF,
                                          SourceLocation Loc,
                                          OpenMPDirectiveKind Kind,
                                          bool EmitChecks,
                                          bool ForceSimpleCall) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForDispatchInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    const OpenMPScheduleTy &ScheduleKind, unsigned IVSize, bool IVSigned,
    bool Ordered, const DispatchRTInput &DispatchValues) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForDispatchDeinit(CodeGenFunction &CGF,
                                                SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForStaticInit(
    CodeGenFunction &CGF, SourceLocation Loc, OpenMPDirectiveKind DKind,
    const OpenMPScheduleTy &ScheduleKind, const StaticRTInput &Values) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitDistributeStaticInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDistScheduleClauseKind SchedKind, const StaticRTInput &Values) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForOrderedIterationEnd(CodeGenFunction &CGF,
                                                     SourceLocation Loc,
                                                     unsigned IVSize,
                                                     bool IVSigned) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForStaticFinish(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              OpenMPDirectiveKind DKind) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Value *CGOpenMPSIMDRuntime::emitForNext(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              unsigned IVSize, bool IVSigned,
                                              Address IL, Address LB,
                                              Address UB, Address ST) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitNumThreadsClause(CodeGenFunction &CGF,
                                               llvm::Value *NumThreads,
                                               SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitProcBindClause(CodeGenFunction &CGF,
                                             ProcBindKind ProcBind,
                                             SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address CGOpenMPSIMDRuntime::getAddrOfThreadPrivate(CodeGenFunction &CGF,
                                                    const VarDecl *VD,
                                                    Address VDAddr,
                                                    SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Function *CGOpenMPSIMDRuntime::emitThreadPrivateVarDefinition(
    const VarDecl *VD, Address VDAddr, SourceLocation Loc, bool PerformInit,
    CodeGenFunction *CGF) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address CGOpenMPSIMDRuntime::getAddrOfArtificialThreadPrivate(
    CodeGenFunction &CGF, QualType VarType, StringRef Name) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitFlush(CodeGenFunction &CGF,
                                    ArrayRef<const Expr *> Vars,
                                    SourceLocation Loc,
                                    llvm::AtomicOrdering AO) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskCall(CodeGenFunction &CGF, SourceLocation Loc,
                                       const OMPExecutableDirective &D,
                                       llvm::Function *TaskFunction,
                                       QualType SharedsTy, Address Shareds,
                                       const Expr *IfCond,
                                       const OMPTaskDataTy &Data) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskLoopCall(
    CodeGenFunction &CGF, SourceLocation Loc, const OMPLoopDirective &D,
    llvm::Function *TaskFunction, QualType SharedsTy, Address Shareds,
    const Expr *IfCond, const OMPTaskDataTy &Data) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitReduction(
    CodeGenFunction &CGF, SourceLocation Loc, ArrayRef<const Expr *> Privates,
    ArrayRef<const Expr *> LHSExprs, ArrayRef<const Expr *> RHSExprs,
    ArrayRef<const Expr *> ReductionOps, ReductionOptionsTy Options) {
  assert(Options.SimpleReduction && "Only simple reduction is expected.");
  CGOpenMPRuntime::emitReduction(CGF, Loc, Privates, LHSExprs, RHSExprs,
                                 ReductionOps, Options);
}

llvm::Value *CGOpenMPSIMDRuntime::emitTaskReductionInit(
    CodeGenFunction &CGF, SourceLocation Loc, ArrayRef<const Expr *> LHSExprs,
    ArrayRef<const Expr *> RHSExprs, const OMPTaskDataTy &Data) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskReductionFini(CodeGenFunction &CGF,
                                                SourceLocation Loc,
                                                bool IsWorksharingReduction) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskReductionFixups(CodeGenFunction &CGF,
                                                  SourceLocation Loc,
                                                  ReductionCodeGen &RCG,
                                                  unsigned N) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address CGOpenMPSIMDRuntime::getTaskReductionItem(CodeGenFunction &CGF,
                                                  SourceLocation Loc,
                                                  llvm::Value *ReductionsPtr,
                                                  LValue SharedLVal) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskwaitCall(CodeGenFunction &CGF,
                                           SourceLocation Loc,
                                           const OMPTaskDataTy &Data) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitCancellationPointCall(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDirectiveKind CancelRegion) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitCancelCall(CodeGenFunction &CGF,
                                         SourceLocation Loc, const Expr *IfCond,
                                         OpenMPDirectiveKind CancelRegion) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetOutlinedFunction(
    const OMPExecutableDirective &D, StringRef ParentName,
    llvm::Function *&OutlinedFn, llvm::Constant *&OutlinedFnID,
    bool IsOffloadEntry, const RegionCodeGenTy &CodeGen) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    llvm::Function *OutlinedFn, llvm::Value *OutlinedFnID, const Expr *IfCond,
    llvm::PointerIntPair<const Expr *, 2, OpenMPDeviceClauseModifier> Device,
    llvm::function_ref<llvm::Value *(CodeGenFunction &CGF,
                                     const OMPLoopDirective &D)>
        SizeEmitter) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

bool CGOpenMPSIMDRuntime::emitTargetFunctions(GlobalDecl GD) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

bool CGOpenMPSIMDRuntime::emitTargetGlobalVariable(GlobalDecl GD) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

bool CGOpenMPSIMDRuntime::emitTargetGlobal(GlobalDecl GD) {
  return false;
}

void CGOpenMPSIMDRuntime::emitTeamsCall(CodeGenFunction &CGF,
                                        const OMPExecutableDirective &D,
                                        SourceLocation Loc,
                                        llvm::Function *OutlinedFn,
                                        ArrayRef<llvm::Value *> CapturedVars) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitNumTeamsClause(CodeGenFunction &CGF,
                                             const Expr *NumTeams,
                                             const Expr *ThreadLimit,
                                             SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetDataCalls(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device, const RegionCodeGenTy &CodeGen,
    CGOpenMPRuntime::TargetDataInfo &Info) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetDataStandAloneCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitDoacrossInit(CodeGenFunction &CGF,
                                           const OMPLoopDirective &D,
                                           ArrayRef<Expr *> NumIterations) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitDoacrossOrdered(CodeGenFunction &CGF,
                                              const OMPDependClause *C) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitDoacrossOrdered(CodeGenFunction &CGF,
                                              const OMPDoacrossClause *C) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

const VarDecl *
CGOpenMPSIMDRuntime::translateParameter(const FieldDecl *FD,
                                        const VarDecl *NativeParam) const {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address
CGOpenMPSIMDRuntime::getParameterAddress(CodeGenFunction &CGF,
                                         const VarDecl *NativeParam,
                                         const VarDecl *TargetParam) const {
  llvm_unreachable("Not supported in SIMD-only mode");
}
