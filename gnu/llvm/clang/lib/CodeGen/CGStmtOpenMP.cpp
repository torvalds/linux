//===--- CGStmtOpenMP.cpp - Emit LLVM Code from Statements ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit OpenMP nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CGCleanup.h"
#include "CGOpenMPRuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Debug.h"
#include <optional>
using namespace clang;
using namespace CodeGen;
using namespace llvm::omp;

#define TTL_CODEGEN_TYPE "target-teams-loop-codegen"

static const VarDecl *getBaseDecl(const Expr *Ref);

namespace {
/// Lexical scope for OpenMP executable constructs, that handles correct codegen
/// for captured expressions.
class OMPLexicalScope : public CodeGenFunction::LexicalScope {
  void emitPreInitStmt(CodeGenFunction &CGF, const OMPExecutableDirective &S) {
    for (const auto *C : S.clauses()) {
      if (const auto *CPI = OMPClauseWithPreInit::get(C)) {
        if (const auto *PreInit =
                cast_or_null<DeclStmt>(CPI->getPreInitStmt())) {
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
  CodeGenFunction::OMPPrivateScope InlinedShareds;

  static bool isCapturedVar(CodeGenFunction &CGF, const VarDecl *VD) {
    return CGF.LambdaCaptureFields.lookup(VD) ||
           (CGF.CapturedStmtInfo && CGF.CapturedStmtInfo->lookup(VD)) ||
           (isa_and_nonnull<BlockDecl>(CGF.CurCodeDecl) &&
            cast<BlockDecl>(CGF.CurCodeDecl)->capturesVariable(VD));
  }

public:
  OMPLexicalScope(
      CodeGenFunction &CGF, const OMPExecutableDirective &S,
      const std::optional<OpenMPDirectiveKind> CapturedRegion = std::nullopt,
      const bool EmitPreInitStmt = true)
      : CodeGenFunction::LexicalScope(CGF, S.getSourceRange()),
        InlinedShareds(CGF) {
    if (EmitPreInitStmt)
      emitPreInitStmt(CGF, S);
    if (!CapturedRegion)
      return;
    assert(S.hasAssociatedStmt() &&
           "Expected associated statement for inlined directive.");
    const CapturedStmt *CS = S.getCapturedStmt(*CapturedRegion);
    for (const auto &C : CS->captures()) {
      if (C.capturesVariable() || C.capturesVariableByCopy()) {
        auto *VD = C.getCapturedVar();
        assert(VD == VD->getCanonicalDecl() &&
               "Canonical decl must be captured.");
        DeclRefExpr DRE(
            CGF.getContext(), const_cast<VarDecl *>(VD),
            isCapturedVar(CGF, VD) || (CGF.CapturedStmtInfo &&
                                       InlinedShareds.isGlobalVarCaptured(VD)),
            VD->getType().getNonReferenceType(), VK_LValue, C.getLocation());
        InlinedShareds.addPrivate(VD, CGF.EmitLValue(&DRE).getAddress());
      }
    }
    (void)InlinedShareds.Privatize();
  }
};

/// Lexical scope for OpenMP parallel construct, that handles correct codegen
/// for captured expressions.
class OMPParallelScope final : public OMPLexicalScope {
  bool EmitPreInitStmt(const OMPExecutableDirective &S) {
    OpenMPDirectiveKind Kind = S.getDirectiveKind();
    return !(isOpenMPTargetExecutionDirective(Kind) ||
             isOpenMPLoopBoundSharingDirective(Kind)) &&
           isOpenMPParallelDirective(Kind);
  }

public:
  OMPParallelScope(CodeGenFunction &CGF, const OMPExecutableDirective &S)
      : OMPLexicalScope(CGF, S, /*CapturedRegion=*/std::nullopt,
                        EmitPreInitStmt(S)) {}
};

/// Lexical scope for OpenMP teams construct, that handles correct codegen
/// for captured expressions.
class OMPTeamsScope final : public OMPLexicalScope {
  bool EmitPreInitStmt(const OMPExecutableDirective &S) {
    OpenMPDirectiveKind Kind = S.getDirectiveKind();
    return !isOpenMPTargetExecutionDirective(Kind) &&
           isOpenMPTeamsDirective(Kind);
  }

public:
  OMPTeamsScope(CodeGenFunction &CGF, const OMPExecutableDirective &S)
      : OMPLexicalScope(CGF, S, /*CapturedRegion=*/std::nullopt,
                        EmitPreInitStmt(S)) {}
};

/// Private scope for OpenMP loop-based directives, that supports capturing
/// of used expression from loop statement.
class OMPLoopScope : public CodeGenFunction::RunCleanupsScope {
  void emitPreInitStmt(CodeGenFunction &CGF, const OMPLoopBasedDirective &S) {
    const Stmt *PreInits;
    CodeGenFunction::OMPMapVars PreCondVars;
    if (auto *LD = dyn_cast<OMPLoopDirective>(&S)) {
      llvm::DenseSet<const VarDecl *> EmittedAsPrivate;
      for (const auto *E : LD->counters()) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
        EmittedAsPrivate.insert(VD->getCanonicalDecl());
        (void)PreCondVars.setVarAddr(
            CGF, VD, CGF.CreateMemTemp(VD->getType().getNonReferenceType()));
      }
      // Mark private vars as undefs.
      for (const auto *C : LD->getClausesOfKind<OMPPrivateClause>()) {
        for (const Expr *IRef : C->varlists()) {
          const auto *OrigVD =
              cast<VarDecl>(cast<DeclRefExpr>(IRef)->getDecl());
          if (EmittedAsPrivate.insert(OrigVD->getCanonicalDecl()).second) {
            QualType OrigVDTy = OrigVD->getType().getNonReferenceType();
            (void)PreCondVars.setVarAddr(
                CGF, OrigVD,
                Address(llvm::UndefValue::get(CGF.ConvertTypeForMem(
                            CGF.getContext().getPointerType(OrigVDTy))),
                        CGF.ConvertTypeForMem(OrigVDTy),
                        CGF.getContext().getDeclAlign(OrigVD)));
          }
        }
      }
      (void)PreCondVars.apply(CGF);
      // Emit init, __range and __end variables for C++ range loops.
      (void)OMPLoopBasedDirective::doForAllLoops(
          LD->getInnermostCapturedStmt()->getCapturedStmt(),
          /*TryImperfectlyNestedLoops=*/true, LD->getLoopsNumber(),
          [&CGF](unsigned Cnt, const Stmt *CurStmt) {
            if (const auto *CXXFor = dyn_cast<CXXForRangeStmt>(CurStmt)) {
              if (const Stmt *Init = CXXFor->getInit())
                CGF.EmitStmt(Init);
              CGF.EmitStmt(CXXFor->getRangeStmt());
              CGF.EmitStmt(CXXFor->getEndStmt());
            }
            return false;
          });
      PreInits = LD->getPreInits();
    } else if (const auto *Tile = dyn_cast<OMPTileDirective>(&S)) {
      PreInits = Tile->getPreInits();
    } else if (const auto *Unroll = dyn_cast<OMPUnrollDirective>(&S)) {
      PreInits = Unroll->getPreInits();
    } else if (const auto *Reverse = dyn_cast<OMPReverseDirective>(&S)) {
      PreInits = Reverse->getPreInits();
    } else if (const auto *Interchange =
                   dyn_cast<OMPInterchangeDirective>(&S)) {
      PreInits = Interchange->getPreInits();
    } else {
      llvm_unreachable("Unknown loop-based directive kind.");
    }
    if (PreInits) {
      // CompoundStmts and DeclStmts are used as lists of PreInit statements and
      // declarations. Since declarations must be visible in the the following
      // that they initialize, unpack the CompoundStmt they are nested in.
      SmallVector<const Stmt *> PreInitStmts;
      if (auto *PreInitCompound = dyn_cast<CompoundStmt>(PreInits))
        llvm::append_range(PreInitStmts, PreInitCompound->body());
      else
        PreInitStmts.push_back(PreInits);

      for (const Stmt *S : PreInitStmts) {
        // EmitStmt skips any OMPCapturedExprDecls, but needs to be emitted
        // here.
        if (auto *PreInitDecl = dyn_cast<DeclStmt>(S)) {
          for (Decl *I : PreInitDecl->decls())
            CGF.EmitVarDecl(cast<VarDecl>(*I));
          continue;
        }
        CGF.EmitStmt(S);
      }
    }
    PreCondVars.restore(CGF);
  }

public:
  OMPLoopScope(CodeGenFunction &CGF, const OMPLoopBasedDirective &S)
      : CodeGenFunction::RunCleanupsScope(CGF) {
    emitPreInitStmt(CGF, S);
  }
};

class OMPSimdLexicalScope : public CodeGenFunction::LexicalScope {
  CodeGenFunction::OMPPrivateScope InlinedShareds;

  static bool isCapturedVar(CodeGenFunction &CGF, const VarDecl *VD) {
    return CGF.LambdaCaptureFields.lookup(VD) ||
           (CGF.CapturedStmtInfo && CGF.CapturedStmtInfo->lookup(VD)) ||
           (isa_and_nonnull<BlockDecl>(CGF.CurCodeDecl) &&
            cast<BlockDecl>(CGF.CurCodeDecl)->capturesVariable(VD));
  }

public:
  OMPSimdLexicalScope(CodeGenFunction &CGF, const OMPExecutableDirective &S)
      : CodeGenFunction::LexicalScope(CGF, S.getSourceRange()),
        InlinedShareds(CGF) {
    for (const auto *C : S.clauses()) {
      if (const auto *CPI = OMPClauseWithPreInit::get(C)) {
        if (const auto *PreInit =
                cast_or_null<DeclStmt>(CPI->getPreInitStmt())) {
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
      } else if (const auto *UDP = dyn_cast<OMPUseDevicePtrClause>(C)) {
        for (const Expr *E : UDP->varlists()) {
          const Decl *D = cast<DeclRefExpr>(E)->getDecl();
          if (const auto *OED = dyn_cast<OMPCapturedExprDecl>(D))
            CGF.EmitVarDecl(*OED);
        }
      } else if (const auto *UDP = dyn_cast<OMPUseDeviceAddrClause>(C)) {
        for (const Expr *E : UDP->varlists()) {
          const Decl *D = getBaseDecl(E);
          if (const auto *OED = dyn_cast<OMPCapturedExprDecl>(D))
            CGF.EmitVarDecl(*OED);
        }
      }
    }
    if (!isOpenMPSimdDirective(S.getDirectiveKind()))
      CGF.EmitOMPPrivateClause(S, InlinedShareds);
    if (const auto *TG = dyn_cast<OMPTaskgroupDirective>(&S)) {
      if (const Expr *E = TG->getReductionRef())
        CGF.EmitVarDecl(*cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl()));
    }
    // Temp copy arrays for inscan reductions should not be emitted as they are
    // not used in simd only mode.
    llvm::DenseSet<CanonicalDeclPtr<const Decl>> CopyArrayTemps;
    for (const auto *C : S.getClausesOfKind<OMPReductionClause>()) {
      if (C->getModifier() != OMPC_REDUCTION_inscan)
        continue;
      for (const Expr *E : C->copy_array_temps())
        CopyArrayTemps.insert(cast<DeclRefExpr>(E)->getDecl());
    }
    const auto *CS = cast_or_null<CapturedStmt>(S.getAssociatedStmt());
    while (CS) {
      for (auto &C : CS->captures()) {
        if (C.capturesVariable() || C.capturesVariableByCopy()) {
          auto *VD = C.getCapturedVar();
          if (CopyArrayTemps.contains(VD))
            continue;
          assert(VD == VD->getCanonicalDecl() &&
                 "Canonical decl must be captured.");
          DeclRefExpr DRE(CGF.getContext(), const_cast<VarDecl *>(VD),
                          isCapturedVar(CGF, VD) ||
                              (CGF.CapturedStmtInfo &&
                               InlinedShareds.isGlobalVarCaptured(VD)),
                          VD->getType().getNonReferenceType(), VK_LValue,
                          C.getLocation());
          InlinedShareds.addPrivate(VD, CGF.EmitLValue(&DRE).getAddress());
        }
      }
      CS = dyn_cast<CapturedStmt>(CS->getCapturedStmt());
    }
    (void)InlinedShareds.Privatize();
  }
};

} // namespace

static void emitCommonOMPTargetDirective(CodeGenFunction &CGF,
                                         const OMPExecutableDirective &S,
                                         const RegionCodeGenTy &CodeGen);

LValue CodeGenFunction::EmitOMPSharedLValue(const Expr *E) {
  if (const auto *OrigDRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *OrigVD = dyn_cast<VarDecl>(OrigDRE->getDecl())) {
      OrigVD = OrigVD->getCanonicalDecl();
      bool IsCaptured =
          LambdaCaptureFields.lookup(OrigVD) ||
          (CapturedStmtInfo && CapturedStmtInfo->lookup(OrigVD)) ||
          (isa_and_nonnull<BlockDecl>(CurCodeDecl));
      DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(OrigVD), IsCaptured,
                      OrigDRE->getType(), VK_LValue, OrigDRE->getExprLoc());
      return EmitLValue(&DRE);
    }
  }
  return EmitLValue(E);
}

llvm::Value *CodeGenFunction::getTypeSize(QualType Ty) {
  ASTContext &C = getContext();
  llvm::Value *Size = nullptr;
  auto SizeInChars = C.getTypeSizeInChars(Ty);
  if (SizeInChars.isZero()) {
    // getTypeSizeInChars() returns 0 for a VLA.
    while (const VariableArrayType *VAT = C.getAsVariableArrayType(Ty)) {
      VlaSizePair VlaSize = getVLASize(VAT);
      Ty = VlaSize.Type;
      Size =
          Size ? Builder.CreateNUWMul(Size, VlaSize.NumElts) : VlaSize.NumElts;
    }
    SizeInChars = C.getTypeSizeInChars(Ty);
    if (SizeInChars.isZero())
      return llvm::ConstantInt::get(SizeTy, /*V=*/0);
    return Builder.CreateNUWMul(Size, CGM.getSize(SizeInChars));
  }
  return CGM.getSize(SizeInChars);
}

void CodeGenFunction::GenerateOpenMPCapturedVars(
    const CapturedStmt &S, SmallVectorImpl<llvm::Value *> &CapturedVars) {
  const RecordDecl *RD = S.getCapturedRecordDecl();
  auto CurField = RD->field_begin();
  auto CurCap = S.captures().begin();
  for (CapturedStmt::const_capture_init_iterator I = S.capture_init_begin(),
                                                 E = S.capture_init_end();
       I != E; ++I, ++CurField, ++CurCap) {
    if (CurField->hasCapturedVLAType()) {
      const VariableArrayType *VAT = CurField->getCapturedVLAType();
      llvm::Value *Val = VLASizeMap[VAT->getSizeExpr()];
      CapturedVars.push_back(Val);
    } else if (CurCap->capturesThis()) {
      CapturedVars.push_back(CXXThisValue);
    } else if (CurCap->capturesVariableByCopy()) {
      llvm::Value *CV = EmitLoadOfScalar(EmitLValue(*I), CurCap->getLocation());

      // If the field is not a pointer, we need to save the actual value
      // and load it as a void pointer.
      if (!CurField->getType()->isAnyPointerType()) {
        ASTContext &Ctx = getContext();
        Address DstAddr = CreateMemTemp(
            Ctx.getUIntPtrType(),
            Twine(CurCap->getCapturedVar()->getName(), ".casted"));
        LValue DstLV = MakeAddrLValue(DstAddr, Ctx.getUIntPtrType());

        llvm::Value *SrcAddrVal = EmitScalarConversion(
            DstAddr.emitRawPointer(*this),
            Ctx.getPointerType(Ctx.getUIntPtrType()),
            Ctx.getPointerType(CurField->getType()), CurCap->getLocation());
        LValue SrcLV =
            MakeNaturalAlignAddrLValue(SrcAddrVal, CurField->getType());

        // Store the value using the source type pointer.
        EmitStoreThroughLValue(RValue::get(CV), SrcLV);

        // Load the value using the destination type pointer.
        CV = EmitLoadOfScalar(DstLV, CurCap->getLocation());
      }
      CapturedVars.push_back(CV);
    } else {
      assert(CurCap->capturesVariable() && "Expected capture by reference.");
      CapturedVars.push_back(EmitLValue(*I).getAddress().emitRawPointer(*this));
    }
  }
}

static Address castValueFromUintptr(CodeGenFunction &CGF, SourceLocation Loc,
                                    QualType DstType, StringRef Name,
                                    LValue AddrLV) {
  ASTContext &Ctx = CGF.getContext();

  llvm::Value *CastedPtr = CGF.EmitScalarConversion(
      AddrLV.getAddress().emitRawPointer(CGF), Ctx.getUIntPtrType(),
      Ctx.getPointerType(DstType), Loc);
  // FIXME: should the pointee type (DstType) be passed?
  Address TmpAddr =
      CGF.MakeNaturalAlignAddrLValue(CastedPtr, DstType).getAddress();
  return TmpAddr;
}

static QualType getCanonicalParamType(ASTContext &C, QualType T) {
  if (T->isLValueReferenceType())
    return C.getLValueReferenceType(
        getCanonicalParamType(C, T.getNonReferenceType()),
        /*SpelledAsLValue=*/false);
  if (T->isPointerType())
    return C.getPointerType(getCanonicalParamType(C, T->getPointeeType()));
  if (const ArrayType *A = T->getAsArrayTypeUnsafe()) {
    if (const auto *VLA = dyn_cast<VariableArrayType>(A))
      return getCanonicalParamType(C, VLA->getElementType());
    if (!A->isVariablyModifiedType())
      return C.getCanonicalType(T);
  }
  return C.getCanonicalParamType(T);
}

namespace {
/// Contains required data for proper outlined function codegen.
struct FunctionOptions {
  /// Captured statement for which the function is generated.
  const CapturedStmt *S = nullptr;
  /// true if cast to/from  UIntPtr is required for variables captured by
  /// value.
  const bool UIntPtrCastRequired = true;
  /// true if only casted arguments must be registered as local args or VLA
  /// sizes.
  const bool RegisterCastedArgsOnly = false;
  /// Name of the generated function.
  const StringRef FunctionName;
  /// Location of the non-debug version of the outlined function.
  SourceLocation Loc;
  explicit FunctionOptions(const CapturedStmt *S, bool UIntPtrCastRequired,
                           bool RegisterCastedArgsOnly, StringRef FunctionName,
                           SourceLocation Loc)
      : S(S), UIntPtrCastRequired(UIntPtrCastRequired),
        RegisterCastedArgsOnly(UIntPtrCastRequired && RegisterCastedArgsOnly),
        FunctionName(FunctionName), Loc(Loc) {}
};
} // namespace

static llvm::Function *emitOutlinedFunctionPrologue(
    CodeGenFunction &CGF, FunctionArgList &Args,
    llvm::MapVector<const Decl *, std::pair<const VarDecl *, Address>>
        &LocalAddrs,
    llvm::DenseMap<const Decl *, std::pair<const Expr *, llvm::Value *>>
        &VLASizes,
    llvm::Value *&CXXThisValue, const FunctionOptions &FO) {
  const CapturedDecl *CD = FO.S->getCapturedDecl();
  const RecordDecl *RD = FO.S->getCapturedRecordDecl();
  assert(CD->hasBody() && "missing CapturedDecl body");

  CXXThisValue = nullptr;
  // Build the argument list.
  CodeGenModule &CGM = CGF.CGM;
  ASTContext &Ctx = CGM.getContext();
  FunctionArgList TargetArgs;
  Args.append(CD->param_begin(),
              std::next(CD->param_begin(), CD->getContextParamPosition()));
  TargetArgs.append(
      CD->param_begin(),
      std::next(CD->param_begin(), CD->getContextParamPosition()));
  auto I = FO.S->captures().begin();
  FunctionDecl *DebugFunctionDecl = nullptr;
  if (!FO.UIntPtrCastRequired) {
    FunctionProtoType::ExtProtoInfo EPI;
    QualType FunctionTy = Ctx.getFunctionType(Ctx.VoidTy, std::nullopt, EPI);
    DebugFunctionDecl = FunctionDecl::Create(
        Ctx, Ctx.getTranslationUnitDecl(), FO.S->getBeginLoc(),
        SourceLocation(), DeclarationName(), FunctionTy,
        Ctx.getTrivialTypeSourceInfo(FunctionTy), SC_Static,
        /*UsesFPIntrin=*/false, /*isInlineSpecified=*/false,
        /*hasWrittenPrototype=*/false);
  }
  for (const FieldDecl *FD : RD->fields()) {
    QualType ArgType = FD->getType();
    IdentifierInfo *II = nullptr;
    VarDecl *CapVar = nullptr;

    // If this is a capture by copy and the type is not a pointer, the outlined
    // function argument type should be uintptr and the value properly casted to
    // uintptr. This is necessary given that the runtime library is only able to
    // deal with pointers. We can pass in the same way the VLA type sizes to the
    // outlined function.
    if (FO.UIntPtrCastRequired &&
        ((I->capturesVariableByCopy() && !ArgType->isAnyPointerType()) ||
         I->capturesVariableArrayType()))
      ArgType = Ctx.getUIntPtrType();

    if (I->capturesVariable() || I->capturesVariableByCopy()) {
      CapVar = I->getCapturedVar();
      II = CapVar->getIdentifier();
    } else if (I->capturesThis()) {
      II = &Ctx.Idents.get("this");
    } else {
      assert(I->capturesVariableArrayType());
      II = &Ctx.Idents.get("vla");
    }
    if (ArgType->isVariablyModifiedType())
      ArgType = getCanonicalParamType(Ctx, ArgType);
    VarDecl *Arg;
    if (CapVar && (CapVar->getTLSKind() != clang::VarDecl::TLS_None)) {
      Arg = ImplicitParamDecl::Create(Ctx, /*DC=*/nullptr, FD->getLocation(),
                                      II, ArgType,
                                      ImplicitParamKind::ThreadPrivateVar);
    } else if (DebugFunctionDecl && (CapVar || I->capturesThis())) {
      Arg = ParmVarDecl::Create(
          Ctx, DebugFunctionDecl,
          CapVar ? CapVar->getBeginLoc() : FD->getBeginLoc(),
          CapVar ? CapVar->getLocation() : FD->getLocation(), II, ArgType,
          /*TInfo=*/nullptr, SC_None, /*DefArg=*/nullptr);
    } else {
      Arg = ImplicitParamDecl::Create(Ctx, /*DC=*/nullptr, FD->getLocation(),
                                      II, ArgType, ImplicitParamKind::Other);
    }
    Args.emplace_back(Arg);
    // Do not cast arguments if we emit function with non-original types.
    TargetArgs.emplace_back(
        FO.UIntPtrCastRequired
            ? Arg
            : CGM.getOpenMPRuntime().translateParameter(FD, Arg));
    ++I;
  }
  Args.append(std::next(CD->param_begin(), CD->getContextParamPosition() + 1),
              CD->param_end());
  TargetArgs.append(
      std::next(CD->param_begin(), CD->getContextParamPosition() + 1),
      CD->param_end());

  // Create the function declaration.
  const CGFunctionInfo &FuncInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, TargetArgs);
  llvm::FunctionType *FuncLLVMTy = CGM.getTypes().GetFunctionType(FuncInfo);

  auto *F =
      llvm::Function::Create(FuncLLVMTy, llvm::GlobalValue::InternalLinkage,
                             FO.FunctionName, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(CD, F, FuncInfo);
  if (CD->isNothrow())
    F->setDoesNotThrow();
  F->setDoesNotRecurse();

  // Always inline the outlined function if optimizations are enabled.
  if (CGM.getCodeGenOpts().OptimizationLevel != 0) {
    F->removeFnAttr(llvm::Attribute::NoInline);
    F->addFnAttr(llvm::Attribute::AlwaysInline);
  }

  // Generate the function.
  CGF.StartFunction(CD, Ctx.VoidTy, F, FuncInfo, TargetArgs,
                    FO.UIntPtrCastRequired ? FO.Loc : FO.S->getBeginLoc(),
                    FO.UIntPtrCastRequired ? FO.Loc
                                           : CD->getBody()->getBeginLoc());
  unsigned Cnt = CD->getContextParamPosition();
  I = FO.S->captures().begin();
  for (const FieldDecl *FD : RD->fields()) {
    // Do not map arguments if we emit function with non-original types.
    Address LocalAddr(Address::invalid());
    if (!FO.UIntPtrCastRequired && Args[Cnt] != TargetArgs[Cnt]) {
      LocalAddr = CGM.getOpenMPRuntime().getParameterAddress(CGF, Args[Cnt],
                                                             TargetArgs[Cnt]);
    } else {
      LocalAddr = CGF.GetAddrOfLocalVar(Args[Cnt]);
    }
    // If we are capturing a pointer by copy we don't need to do anything, just
    // use the value that we get from the arguments.
    if (I->capturesVariableByCopy() && FD->getType()->isAnyPointerType()) {
      const VarDecl *CurVD = I->getCapturedVar();
      if (!FO.RegisterCastedArgsOnly)
        LocalAddrs.insert({Args[Cnt], {CurVD, LocalAddr}});
      ++Cnt;
      ++I;
      continue;
    }

    LValue ArgLVal = CGF.MakeAddrLValue(LocalAddr, Args[Cnt]->getType(),
                                        AlignmentSource::Decl);
    if (FD->hasCapturedVLAType()) {
      if (FO.UIntPtrCastRequired) {
        ArgLVal = CGF.MakeAddrLValue(
            castValueFromUintptr(CGF, I->getLocation(), FD->getType(),
                                 Args[Cnt]->getName(), ArgLVal),
            FD->getType(), AlignmentSource::Decl);
      }
      llvm::Value *ExprArg = CGF.EmitLoadOfScalar(ArgLVal, I->getLocation());
      const VariableArrayType *VAT = FD->getCapturedVLAType();
      VLASizes.try_emplace(Args[Cnt], VAT->getSizeExpr(), ExprArg);
    } else if (I->capturesVariable()) {
      const VarDecl *Var = I->getCapturedVar();
      QualType VarTy = Var->getType();
      Address ArgAddr = ArgLVal.getAddress();
      if (ArgLVal.getType()->isLValueReferenceType()) {
        ArgAddr = CGF.EmitLoadOfReference(ArgLVal);
      } else if (!VarTy->isVariablyModifiedType() || !VarTy->isPointerType()) {
        assert(ArgLVal.getType()->isPointerType());
        ArgAddr = CGF.EmitLoadOfPointer(
            ArgAddr, ArgLVal.getType()->castAs<PointerType>());
      }
      if (!FO.RegisterCastedArgsOnly) {
        LocalAddrs.insert(
            {Args[Cnt], {Var, ArgAddr.withAlignment(Ctx.getDeclAlign(Var))}});
      }
    } else if (I->capturesVariableByCopy()) {
      assert(!FD->getType()->isAnyPointerType() &&
             "Not expecting a captured pointer.");
      const VarDecl *Var = I->getCapturedVar();
      LocalAddrs.insert({Args[Cnt],
                         {Var, FO.UIntPtrCastRequired
                                   ? castValueFromUintptr(
                                         CGF, I->getLocation(), FD->getType(),
                                         Args[Cnt]->getName(), ArgLVal)
                                   : ArgLVal.getAddress()}});
    } else {
      // If 'this' is captured, load it into CXXThisValue.
      assert(I->capturesThis());
      CXXThisValue = CGF.EmitLoadOfScalar(ArgLVal, I->getLocation());
      LocalAddrs.insert({Args[Cnt], {nullptr, ArgLVal.getAddress()}});
    }
    ++Cnt;
    ++I;
  }

  return F;
}

llvm::Function *
CodeGenFunction::GenerateOpenMPCapturedStmtFunction(const CapturedStmt &S,
                                                    SourceLocation Loc) {
  assert(
      CapturedStmtInfo &&
      "CapturedStmtInfo should be set when generating the captured function");
  const CapturedDecl *CD = S.getCapturedDecl();
  // Build the argument list.
  bool NeedWrapperFunction =
      getDebugInfo() && CGM.getCodeGenOpts().hasReducedDebugInfo();
  FunctionArgList Args;
  llvm::MapVector<const Decl *, std::pair<const VarDecl *, Address>> LocalAddrs;
  llvm::DenseMap<const Decl *, std::pair<const Expr *, llvm::Value *>> VLASizes;
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  Out << CapturedStmtInfo->getHelperName();
  if (NeedWrapperFunction)
    Out << "_debug__";
  FunctionOptions FO(&S, !NeedWrapperFunction, /*RegisterCastedArgsOnly=*/false,
                     Out.str(), Loc);
  llvm::Function *F = emitOutlinedFunctionPrologue(*this, Args, LocalAddrs,
                                                   VLASizes, CXXThisValue, FO);
  CodeGenFunction::OMPPrivateScope LocalScope(*this);
  for (const auto &LocalAddrPair : LocalAddrs) {
    if (LocalAddrPair.second.first) {
      LocalScope.addPrivate(LocalAddrPair.second.first,
                            LocalAddrPair.second.second);
    }
  }
  (void)LocalScope.Privatize();
  for (const auto &VLASizePair : VLASizes)
    VLASizeMap[VLASizePair.second.first] = VLASizePair.second.second;
  PGO.assignRegionCounters(GlobalDecl(CD), F);
  CapturedStmtInfo->EmitBody(*this, CD->getBody());
  (void)LocalScope.ForceCleanup();
  FinishFunction(CD->getBodyRBrace());
  if (!NeedWrapperFunction)
    return F;

  FunctionOptions WrapperFO(&S, /*UIntPtrCastRequired=*/true,
                            /*RegisterCastedArgsOnly=*/true,
                            CapturedStmtInfo->getHelperName(), Loc);
  CodeGenFunction WrapperCGF(CGM, /*suppressNewContext=*/true);
  WrapperCGF.CapturedStmtInfo = CapturedStmtInfo;
  Args.clear();
  LocalAddrs.clear();
  VLASizes.clear();
  llvm::Function *WrapperF =
      emitOutlinedFunctionPrologue(WrapperCGF, Args, LocalAddrs, VLASizes,
                                   WrapperCGF.CXXThisValue, WrapperFO);
  llvm::SmallVector<llvm::Value *, 4> CallArgs;
  auto *PI = F->arg_begin();
  for (const auto *Arg : Args) {
    llvm::Value *CallArg;
    auto I = LocalAddrs.find(Arg);
    if (I != LocalAddrs.end()) {
      LValue LV = WrapperCGF.MakeAddrLValue(
          I->second.second,
          I->second.first ? I->second.first->getType() : Arg->getType(),
          AlignmentSource::Decl);
      if (LV.getType()->isAnyComplexType())
        LV.setAddress(LV.getAddress().withElementType(PI->getType()));
      CallArg = WrapperCGF.EmitLoadOfScalar(LV, S.getBeginLoc());
    } else {
      auto EI = VLASizes.find(Arg);
      if (EI != VLASizes.end()) {
        CallArg = EI->second.second;
      } else {
        LValue LV =
            WrapperCGF.MakeAddrLValue(WrapperCGF.GetAddrOfLocalVar(Arg),
                                      Arg->getType(), AlignmentSource::Decl);
        CallArg = WrapperCGF.EmitLoadOfScalar(LV, S.getBeginLoc());
      }
    }
    CallArgs.emplace_back(WrapperCGF.EmitFromMemory(CallArg, Arg->getType()));
    ++PI;
  }
  CGM.getOpenMPRuntime().emitOutlinedFunctionCall(WrapperCGF, Loc, F, CallArgs);
  WrapperCGF.FinishFunction();
  return WrapperF;
}

//===----------------------------------------------------------------------===//
//                              OpenMP Directive Emission
//===----------------------------------------------------------------------===//
void CodeGenFunction::EmitOMPAggregateAssign(
    Address DestAddr, Address SrcAddr, QualType OriginalType,
    const llvm::function_ref<void(Address, Address)> CopyGen) {
  // Perform element-by-element initialization.
  QualType ElementTy;

  // Drill down to the base element type on both arrays.
  const ArrayType *ArrayTy = OriginalType->getAsArrayTypeUnsafe();
  llvm::Value *NumElements = emitArrayLength(ArrayTy, ElementTy, DestAddr);
  SrcAddr = SrcAddr.withElementType(DestAddr.getElementType());

  llvm::Value *SrcBegin = SrcAddr.emitRawPointer(*this);
  llvm::Value *DestBegin = DestAddr.emitRawPointer(*this);
  // Cast from pointer to array type to pointer to single element.
  llvm::Value *DestEnd = Builder.CreateInBoundsGEP(DestAddr.getElementType(),
                                                   DestBegin, NumElements);

  // The basic structure here is a while-do loop.
  llvm::BasicBlock *BodyBB = createBasicBlock("omp.arraycpy.body");
  llvm::BasicBlock *DoneBB = createBasicBlock("omp.arraycpy.done");
  llvm::Value *IsEmpty =
      Builder.CreateICmpEQ(DestBegin, DestEnd, "omp.arraycpy.isempty");
  Builder.CreateCondBr(IsEmpty, DoneBB, BodyBB);

  // Enter the loop body, making that address the current address.
  llvm::BasicBlock *EntryBB = Builder.GetInsertBlock();
  EmitBlock(BodyBB);

  CharUnits ElementSize = getContext().getTypeSizeInChars(ElementTy);

  llvm::PHINode *SrcElementPHI =
      Builder.CreatePHI(SrcBegin->getType(), 2, "omp.arraycpy.srcElementPast");
  SrcElementPHI->addIncoming(SrcBegin, EntryBB);
  Address SrcElementCurrent =
      Address(SrcElementPHI, SrcAddr.getElementType(),
              SrcAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  llvm::PHINode *DestElementPHI = Builder.CreatePHI(
      DestBegin->getType(), 2, "omp.arraycpy.destElementPast");
  DestElementPHI->addIncoming(DestBegin, EntryBB);
  Address DestElementCurrent =
      Address(DestElementPHI, DestAddr.getElementType(),
              DestAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  // Emit copy.
  CopyGen(DestElementCurrent, SrcElementCurrent);

  // Shift the address forward by one element.
  llvm::Value *DestElementNext =
      Builder.CreateConstGEP1_32(DestAddr.getElementType(), DestElementPHI,
                                 /*Idx0=*/1, "omp.arraycpy.dest.element");
  llvm::Value *SrcElementNext =
      Builder.CreateConstGEP1_32(SrcAddr.getElementType(), SrcElementPHI,
                                 /*Idx0=*/1, "omp.arraycpy.src.element");
  // Check whether we've reached the end.
  llvm::Value *Done =
      Builder.CreateICmpEQ(DestElementNext, DestEnd, "omp.arraycpy.done");
  Builder.CreateCondBr(Done, DoneBB, BodyBB);
  DestElementPHI->addIncoming(DestElementNext, Builder.GetInsertBlock());
  SrcElementPHI->addIncoming(SrcElementNext, Builder.GetInsertBlock());

  // Done.
  EmitBlock(DoneBB, /*IsFinished=*/true);
}

void CodeGenFunction::EmitOMPCopy(QualType OriginalType, Address DestAddr,
                                  Address SrcAddr, const VarDecl *DestVD,
                                  const VarDecl *SrcVD, const Expr *Copy) {
  if (OriginalType->isArrayType()) {
    const auto *BO = dyn_cast<BinaryOperator>(Copy);
    if (BO && BO->getOpcode() == BO_Assign) {
      // Perform simple memcpy for simple copying.
      LValue Dest = MakeAddrLValue(DestAddr, OriginalType);
      LValue Src = MakeAddrLValue(SrcAddr, OriginalType);
      EmitAggregateAssign(Dest, Src, OriginalType);
    } else {
      // For arrays with complex element types perform element by element
      // copying.
      EmitOMPAggregateAssign(
          DestAddr, SrcAddr, OriginalType,
          [this, Copy, SrcVD, DestVD](Address DestElement, Address SrcElement) {
            // Working with the single array element, so have to remap
            // destination and source variables to corresponding array
            // elements.
            CodeGenFunction::OMPPrivateScope Remap(*this);
            Remap.addPrivate(DestVD, DestElement);
            Remap.addPrivate(SrcVD, SrcElement);
            (void)Remap.Privatize();
            EmitIgnoredExpr(Copy);
          });
    }
  } else {
    // Remap pseudo source variable to private copy.
    CodeGenFunction::OMPPrivateScope Remap(*this);
    Remap.addPrivate(SrcVD, SrcAddr);
    Remap.addPrivate(DestVD, DestAddr);
    (void)Remap.Privatize();
    // Emit copying of the whole variable.
    EmitIgnoredExpr(Copy);
  }
}

bool CodeGenFunction::EmitOMPFirstprivateClause(const OMPExecutableDirective &D,
                                                OMPPrivateScope &PrivateScope) {
  if (!HaveInsertPoint())
    return false;
  bool DeviceConstTarget =
      getLangOpts().OpenMPIsTargetDevice &&
      isOpenMPTargetExecutionDirective(D.getDirectiveKind());
  bool FirstprivateIsLastprivate = false;
  llvm::DenseMap<const VarDecl *, OpenMPLastprivateModifier> Lastprivates;
  for (const auto *C : D.getClausesOfKind<OMPLastprivateClause>()) {
    for (const auto *D : C->varlists())
      Lastprivates.try_emplace(
          cast<VarDecl>(cast<DeclRefExpr>(D)->getDecl())->getCanonicalDecl(),
          C->getKind());
  }
  llvm::DenseSet<const VarDecl *> EmittedAsFirstprivate;
  llvm::SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
  getOpenMPCaptureRegions(CaptureRegions, D.getDirectiveKind());
  // Force emission of the firstprivate copy if the directive does not emit
  // outlined function, like omp for, omp simd, omp distribute etc.
  bool MustEmitFirstprivateCopy =
      CaptureRegions.size() == 1 && CaptureRegions.back() == OMPD_unknown;
  for (const auto *C : D.getClausesOfKind<OMPFirstprivateClause>()) {
    const auto *IRef = C->varlist_begin();
    const auto *InitsRef = C->inits().begin();
    for (const Expr *IInit : C->private_copies()) {
      const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      bool ThisFirstprivateIsLastprivate =
          Lastprivates.count(OrigVD->getCanonicalDecl()) > 0;
      const FieldDecl *FD = CapturedStmtInfo->lookup(OrigVD);
      const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(IInit)->getDecl());
      if (!MustEmitFirstprivateCopy && !ThisFirstprivateIsLastprivate && FD &&
          !FD->getType()->isReferenceType() &&
          (!VD || !VD->hasAttr<OMPAllocateDeclAttr>())) {
        EmittedAsFirstprivate.insert(OrigVD->getCanonicalDecl());
        ++IRef;
        ++InitsRef;
        continue;
      }
      // Do not emit copy for firstprivate constant variables in target regions,
      // captured by reference.
      if (DeviceConstTarget && OrigVD->getType().isConstant(getContext()) &&
          FD && FD->getType()->isReferenceType() &&
          (!VD || !VD->hasAttr<OMPAllocateDeclAttr>())) {
        EmittedAsFirstprivate.insert(OrigVD->getCanonicalDecl());
        ++IRef;
        ++InitsRef;
        continue;
      }
      FirstprivateIsLastprivate =
          FirstprivateIsLastprivate || ThisFirstprivateIsLastprivate;
      if (EmittedAsFirstprivate.insert(OrigVD->getCanonicalDecl()).second) {
        const auto *VDInit =
            cast<VarDecl>(cast<DeclRefExpr>(*InitsRef)->getDecl());
        bool IsRegistered;
        DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(OrigVD),
                        /*RefersToEnclosingVariableOrCapture=*/FD != nullptr,
                        (*IRef)->getType(), VK_LValue, (*IRef)->getExprLoc());
        LValue OriginalLVal;
        if (!FD) {
          // Check if the firstprivate variable is just a constant value.
          ConstantEmission CE = tryEmitAsConstant(&DRE);
          if (CE && !CE.isReference()) {
            // Constant value, no need to create a copy.
            ++IRef;
            ++InitsRef;
            continue;
          }
          if (CE && CE.isReference()) {
            OriginalLVal = CE.getReferenceLValue(*this, &DRE);
          } else {
            assert(!CE && "Expected non-constant firstprivate.");
            OriginalLVal = EmitLValue(&DRE);
          }
        } else {
          OriginalLVal = EmitLValue(&DRE);
        }
        QualType Type = VD->getType();
        if (Type->isArrayType()) {
          // Emit VarDecl with copy init for arrays.
          // Get the address of the original variable captured in current
          // captured region.
          AutoVarEmission Emission = EmitAutoVarAlloca(*VD);
          const Expr *Init = VD->getInit();
          if (!isa<CXXConstructExpr>(Init) || isTrivialInitializer(Init)) {
            // Perform simple memcpy.
            LValue Dest = MakeAddrLValue(Emission.getAllocatedAddress(), Type);
            EmitAggregateAssign(Dest, OriginalLVal, Type);
          } else {
            EmitOMPAggregateAssign(
                Emission.getAllocatedAddress(), OriginalLVal.getAddress(), Type,
                [this, VDInit, Init](Address DestElement, Address SrcElement) {
                  // Clean up any temporaries needed by the
                  // initialization.
                  RunCleanupsScope InitScope(*this);
                  // Emit initialization for single element.
                  setAddrOfLocalVar(VDInit, SrcElement);
                  EmitAnyExprToMem(Init, DestElement,
                                   Init->getType().getQualifiers(),
                                   /*IsInitializer*/ false);
                  LocalDeclMap.erase(VDInit);
                });
          }
          EmitAutoVarCleanups(Emission);
          IsRegistered =
              PrivateScope.addPrivate(OrigVD, Emission.getAllocatedAddress());
        } else {
          Address OriginalAddr = OriginalLVal.getAddress();
          // Emit private VarDecl with copy init.
          // Remap temp VDInit variable to the address of the original
          // variable (for proper handling of captured global variables).
          setAddrOfLocalVar(VDInit, OriginalAddr);
          EmitDecl(*VD);
          LocalDeclMap.erase(VDInit);
          Address VDAddr = GetAddrOfLocalVar(VD);
          if (ThisFirstprivateIsLastprivate &&
              Lastprivates[OrigVD->getCanonicalDecl()] ==
                  OMPC_LASTPRIVATE_conditional) {
            // Create/init special variable for lastprivate conditionals.
            llvm::Value *V =
                EmitLoadOfScalar(MakeAddrLValue(VDAddr, (*IRef)->getType(),
                                                AlignmentSource::Decl),
                                 (*IRef)->getExprLoc());
            VDAddr = CGM.getOpenMPRuntime().emitLastprivateConditionalInit(
                *this, OrigVD);
            EmitStoreOfScalar(V, MakeAddrLValue(VDAddr, (*IRef)->getType(),
                                                AlignmentSource::Decl));
            LocalDeclMap.erase(VD);
            setAddrOfLocalVar(VD, VDAddr);
          }
          IsRegistered = PrivateScope.addPrivate(OrigVD, VDAddr);
        }
        assert(IsRegistered &&
               "firstprivate var already registered as private");
        // Silence the warning about unused variable.
        (void)IsRegistered;
      }
      ++IRef;
      ++InitsRef;
    }
  }
  return FirstprivateIsLastprivate && !EmittedAsFirstprivate.empty();
}

void CodeGenFunction::EmitOMPPrivateClause(
    const OMPExecutableDirective &D,
    CodeGenFunction::OMPPrivateScope &PrivateScope) {
  if (!HaveInsertPoint())
    return;
  llvm::DenseSet<const VarDecl *> EmittedAsPrivate;
  for (const auto *C : D.getClausesOfKind<OMPPrivateClause>()) {
    auto IRef = C->varlist_begin();
    for (const Expr *IInit : C->private_copies()) {
      const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      if (EmittedAsPrivate.insert(OrigVD->getCanonicalDecl()).second) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(IInit)->getDecl());
        EmitDecl(*VD);
        // Emit private VarDecl with copy init.
        bool IsRegistered =
            PrivateScope.addPrivate(OrigVD, GetAddrOfLocalVar(VD));
        assert(IsRegistered && "private var already registered as private");
        // Silence the warning about unused variable.
        (void)IsRegistered;
      }
      ++IRef;
    }
  }
}

bool CodeGenFunction::EmitOMPCopyinClause(const OMPExecutableDirective &D) {
  if (!HaveInsertPoint())
    return false;
  // threadprivate_var1 = master_threadprivate_var1;
  // operator=(threadprivate_var2, master_threadprivate_var2);
  // ...
  // __kmpc_barrier(&loc, global_tid);
  llvm::DenseSet<const VarDecl *> CopiedVars;
  llvm::BasicBlock *CopyBegin = nullptr, *CopyEnd = nullptr;
  for (const auto *C : D.getClausesOfKind<OMPCopyinClause>()) {
    auto IRef = C->varlist_begin();
    auto ISrcRef = C->source_exprs().begin();
    auto IDestRef = C->destination_exprs().begin();
    for (const Expr *AssignOp : C->assignment_ops()) {
      const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      QualType Type = VD->getType();
      if (CopiedVars.insert(VD->getCanonicalDecl()).second) {
        // Get the address of the master variable. If we are emitting code with
        // TLS support, the address is passed from the master as field in the
        // captured declaration.
        Address MasterAddr = Address::invalid();
        if (getLangOpts().OpenMPUseTLS &&
            getContext().getTargetInfo().isTLSSupported()) {
          assert(CapturedStmtInfo->lookup(VD) &&
                 "Copyin threadprivates should have been captured!");
          DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(VD), true,
                          (*IRef)->getType(), VK_LValue, (*IRef)->getExprLoc());
          MasterAddr = EmitLValue(&DRE).getAddress();
          LocalDeclMap.erase(VD);
        } else {
          MasterAddr =
              Address(VD->isStaticLocal() ? CGM.getStaticLocalDeclAddress(VD)
                                          : CGM.GetAddrOfGlobal(VD),
                      CGM.getTypes().ConvertTypeForMem(VD->getType()),
                      getContext().getDeclAlign(VD));
        }
        // Get the address of the threadprivate variable.
        Address PrivateAddr = EmitLValue(*IRef).getAddress();
        if (CopiedVars.size() == 1) {
          // At first check if current thread is a master thread. If it is, no
          // need to copy data.
          CopyBegin = createBasicBlock("copyin.not.master");
          CopyEnd = createBasicBlock("copyin.not.master.end");
          // TODO: Avoid ptrtoint conversion.
          auto *MasterAddrInt = Builder.CreatePtrToInt(
              MasterAddr.emitRawPointer(*this), CGM.IntPtrTy);
          auto *PrivateAddrInt = Builder.CreatePtrToInt(
              PrivateAddr.emitRawPointer(*this), CGM.IntPtrTy);
          Builder.CreateCondBr(
              Builder.CreateICmpNE(MasterAddrInt, PrivateAddrInt), CopyBegin,
              CopyEnd);
          EmitBlock(CopyBegin);
        }
        const auto *SrcVD =
            cast<VarDecl>(cast<DeclRefExpr>(*ISrcRef)->getDecl());
        const auto *DestVD =
            cast<VarDecl>(cast<DeclRefExpr>(*IDestRef)->getDecl());
        EmitOMPCopy(Type, PrivateAddr, MasterAddr, DestVD, SrcVD, AssignOp);
      }
      ++IRef;
      ++ISrcRef;
      ++IDestRef;
    }
  }
  if (CopyEnd) {
    // Exit out of copying procedure for non-master thread.
    EmitBlock(CopyEnd, /*IsFinished=*/true);
    return true;
  }
  return false;
}

bool CodeGenFunction::EmitOMPLastprivateClauseInit(
    const OMPExecutableDirective &D, OMPPrivateScope &PrivateScope) {
  if (!HaveInsertPoint())
    return false;
  bool HasAtLeastOneLastprivate = false;
  llvm::DenseSet<const VarDecl *> SIMDLCVs;
  if (isOpenMPSimdDirective(D.getDirectiveKind())) {
    const auto *LoopDirective = cast<OMPLoopDirective>(&D);
    for (const Expr *C : LoopDirective->counters()) {
      SIMDLCVs.insert(
          cast<VarDecl>(cast<DeclRefExpr>(C)->getDecl())->getCanonicalDecl());
    }
  }
  llvm::DenseSet<const VarDecl *> AlreadyEmittedVars;
  for (const auto *C : D.getClausesOfKind<OMPLastprivateClause>()) {
    HasAtLeastOneLastprivate = true;
    if (isOpenMPTaskLoopDirective(D.getDirectiveKind()) &&
        !getLangOpts().OpenMPSimd)
      break;
    const auto *IRef = C->varlist_begin();
    const auto *IDestRef = C->destination_exprs().begin();
    for (const Expr *IInit : C->private_copies()) {
      // Keep the address of the original variable for future update at the end
      // of the loop.
      const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      // Taskloops do not require additional initialization, it is done in
      // runtime support library.
      if (AlreadyEmittedVars.insert(OrigVD->getCanonicalDecl()).second) {
        const auto *DestVD =
            cast<VarDecl>(cast<DeclRefExpr>(*IDestRef)->getDecl());
        DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(OrigVD),
                        /*RefersToEnclosingVariableOrCapture=*/
                        CapturedStmtInfo->lookup(OrigVD) != nullptr,
                        (*IRef)->getType(), VK_LValue, (*IRef)->getExprLoc());
        PrivateScope.addPrivate(DestVD, EmitLValue(&DRE).getAddress());
        // Check if the variable is also a firstprivate: in this case IInit is
        // not generated. Initialization of this variable will happen in codegen
        // for 'firstprivate' clause.
        if (IInit && !SIMDLCVs.count(OrigVD->getCanonicalDecl())) {
          const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(IInit)->getDecl());
          Address VDAddr = Address::invalid();
          if (C->getKind() == OMPC_LASTPRIVATE_conditional) {
            VDAddr = CGM.getOpenMPRuntime().emitLastprivateConditionalInit(
                *this, OrigVD);
            setAddrOfLocalVar(VD, VDAddr);
          } else {
            // Emit private VarDecl with copy init.
            EmitDecl(*VD);
            VDAddr = GetAddrOfLocalVar(VD);
          }
          bool IsRegistered = PrivateScope.addPrivate(OrigVD, VDAddr);
          assert(IsRegistered &&
                 "lastprivate var already registered as private");
          (void)IsRegistered;
        }
      }
      ++IRef;
      ++IDestRef;
    }
  }
  return HasAtLeastOneLastprivate;
}

void CodeGenFunction::EmitOMPLastprivateClauseFinal(
    const OMPExecutableDirective &D, bool NoFinals,
    llvm::Value *IsLastIterCond) {
  if (!HaveInsertPoint())
    return;
  // Emit following code:
  // if (<IsLastIterCond>) {
  //   orig_var1 = private_orig_var1;
  //   ...
  //   orig_varn = private_orig_varn;
  // }
  llvm::BasicBlock *ThenBB = nullptr;
  llvm::BasicBlock *DoneBB = nullptr;
  if (IsLastIterCond) {
    // Emit implicit barrier if at least one lastprivate conditional is found
    // and this is not a simd mode.
    if (!getLangOpts().OpenMPSimd &&
        llvm::any_of(D.getClausesOfKind<OMPLastprivateClause>(),
                     [](const OMPLastprivateClause *C) {
                       return C->getKind() == OMPC_LASTPRIVATE_conditional;
                     })) {
      CGM.getOpenMPRuntime().emitBarrierCall(*this, D.getBeginLoc(),
                                             OMPD_unknown,
                                             /*EmitChecks=*/false,
                                             /*ForceSimpleCall=*/true);
    }
    ThenBB = createBasicBlock(".omp.lastprivate.then");
    DoneBB = createBasicBlock(".omp.lastprivate.done");
    Builder.CreateCondBr(IsLastIterCond, ThenBB, DoneBB);
    EmitBlock(ThenBB);
  }
  llvm::DenseSet<const VarDecl *> AlreadyEmittedVars;
  llvm::DenseMap<const VarDecl *, const Expr *> LoopCountersAndUpdates;
  if (const auto *LoopDirective = dyn_cast<OMPLoopDirective>(&D)) {
    auto IC = LoopDirective->counters().begin();
    for (const Expr *F : LoopDirective->finals()) {
      const auto *D =
          cast<VarDecl>(cast<DeclRefExpr>(*IC)->getDecl())->getCanonicalDecl();
      if (NoFinals)
        AlreadyEmittedVars.insert(D);
      else
        LoopCountersAndUpdates[D] = F;
      ++IC;
    }
  }
  for (const auto *C : D.getClausesOfKind<OMPLastprivateClause>()) {
    auto IRef = C->varlist_begin();
    auto ISrcRef = C->source_exprs().begin();
    auto IDestRef = C->destination_exprs().begin();
    for (const Expr *AssignOp : C->assignment_ops()) {
      const auto *PrivateVD =
          cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      QualType Type = PrivateVD->getType();
      const auto *CanonicalVD = PrivateVD->getCanonicalDecl();
      if (AlreadyEmittedVars.insert(CanonicalVD).second) {
        // If lastprivate variable is a loop control variable for loop-based
        // directive, update its value before copyin back to original
        // variable.
        if (const Expr *FinalExpr = LoopCountersAndUpdates.lookup(CanonicalVD))
          EmitIgnoredExpr(FinalExpr);
        const auto *SrcVD =
            cast<VarDecl>(cast<DeclRefExpr>(*ISrcRef)->getDecl());
        const auto *DestVD =
            cast<VarDecl>(cast<DeclRefExpr>(*IDestRef)->getDecl());
        // Get the address of the private variable.
        Address PrivateAddr = GetAddrOfLocalVar(PrivateVD);
        if (const auto *RefTy = PrivateVD->getType()->getAs<ReferenceType>())
          PrivateAddr = Address(
              Builder.CreateLoad(PrivateAddr),
              CGM.getTypes().ConvertTypeForMem(RefTy->getPointeeType()),
              CGM.getNaturalTypeAlignment(RefTy->getPointeeType()));
        // Store the last value to the private copy in the last iteration.
        if (C->getKind() == OMPC_LASTPRIVATE_conditional)
          CGM.getOpenMPRuntime().emitLastprivateConditionalFinalUpdate(
              *this, MakeAddrLValue(PrivateAddr, (*IRef)->getType()), PrivateVD,
              (*IRef)->getExprLoc());
        // Get the address of the original variable.
        Address OriginalAddr = GetAddrOfLocalVar(DestVD);
        EmitOMPCopy(Type, OriginalAddr, PrivateAddr, DestVD, SrcVD, AssignOp);
      }
      ++IRef;
      ++ISrcRef;
      ++IDestRef;
    }
    if (const Expr *PostUpdate = C->getPostUpdateExpr())
      EmitIgnoredExpr(PostUpdate);
  }
  if (IsLastIterCond)
    EmitBlock(DoneBB, /*IsFinished=*/true);
}

void CodeGenFunction::EmitOMPReductionClauseInit(
    const OMPExecutableDirective &D,
    CodeGenFunction::OMPPrivateScope &PrivateScope, bool ForInscan) {
  if (!HaveInsertPoint())
    return;
  SmallVector<const Expr *, 4> Shareds;
  SmallVector<const Expr *, 4> Privates;
  SmallVector<const Expr *, 4> ReductionOps;
  SmallVector<const Expr *, 4> LHSs;
  SmallVector<const Expr *, 4> RHSs;
  OMPTaskDataTy Data;
  SmallVector<const Expr *, 4> TaskLHSs;
  SmallVector<const Expr *, 4> TaskRHSs;
  for (const auto *C : D.getClausesOfKind<OMPReductionClause>()) {
    if (ForInscan != (C->getModifier() == OMPC_REDUCTION_inscan))
      continue;
    Shareds.append(C->varlist_begin(), C->varlist_end());
    Privates.append(C->privates().begin(), C->privates().end());
    ReductionOps.append(C->reduction_ops().begin(), C->reduction_ops().end());
    LHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
    RHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
    if (C->getModifier() == OMPC_REDUCTION_task) {
      Data.ReductionVars.append(C->privates().begin(), C->privates().end());
      Data.ReductionOrigs.append(C->varlist_begin(), C->varlist_end());
      Data.ReductionCopies.append(C->privates().begin(), C->privates().end());
      Data.ReductionOps.append(C->reduction_ops().begin(),
                               C->reduction_ops().end());
      TaskLHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
      TaskRHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
    }
  }
  ReductionCodeGen RedCG(Shareds, Shareds, Privates, ReductionOps);
  unsigned Count = 0;
  auto *ILHS = LHSs.begin();
  auto *IRHS = RHSs.begin();
  auto *IPriv = Privates.begin();
  for (const Expr *IRef : Shareds) {
    const auto *PrivateVD = cast<VarDecl>(cast<DeclRefExpr>(*IPriv)->getDecl());
    // Emit private VarDecl with reduction init.
    RedCG.emitSharedOrigLValue(*this, Count);
    RedCG.emitAggregateType(*this, Count);
    AutoVarEmission Emission = EmitAutoVarAlloca(*PrivateVD);
    RedCG.emitInitialization(*this, Count, Emission.getAllocatedAddress(),
                             RedCG.getSharedLValue(Count).getAddress(),
                             [&Emission](CodeGenFunction &CGF) {
                               CGF.EmitAutoVarInit(Emission);
                               return true;
                             });
    EmitAutoVarCleanups(Emission);
    Address BaseAddr = RedCG.adjustPrivateAddress(
        *this, Count, Emission.getAllocatedAddress());
    bool IsRegistered =
        PrivateScope.addPrivate(RedCG.getBaseDecl(Count), BaseAddr);
    assert(IsRegistered && "private var already registered as private");
    // Silence the warning about unused variable.
    (void)IsRegistered;

    const auto *LHSVD = cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
    const auto *RHSVD = cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
    QualType Type = PrivateVD->getType();
    bool isaOMPArraySectionExpr = isa<ArraySectionExpr>(IRef);
    if (isaOMPArraySectionExpr && Type->isVariablyModifiedType()) {
      // Store the address of the original variable associated with the LHS
      // implicit variable.
      PrivateScope.addPrivate(LHSVD, RedCG.getSharedLValue(Count).getAddress());
      PrivateScope.addPrivate(RHSVD, GetAddrOfLocalVar(PrivateVD));
    } else if ((isaOMPArraySectionExpr && Type->isScalarType()) ||
               isa<ArraySubscriptExpr>(IRef)) {
      // Store the address of the original variable associated with the LHS
      // implicit variable.
      PrivateScope.addPrivate(LHSVD, RedCG.getSharedLValue(Count).getAddress());
      PrivateScope.addPrivate(RHSVD,
                              GetAddrOfLocalVar(PrivateVD).withElementType(
                                  ConvertTypeForMem(RHSVD->getType())));
    } else {
      QualType Type = PrivateVD->getType();
      bool IsArray = getContext().getAsArrayType(Type) != nullptr;
      Address OriginalAddr = RedCG.getSharedLValue(Count).getAddress();
      // Store the address of the original variable associated with the LHS
      // implicit variable.
      if (IsArray) {
        OriginalAddr =
            OriginalAddr.withElementType(ConvertTypeForMem(LHSVD->getType()));
      }
      PrivateScope.addPrivate(LHSVD, OriginalAddr);
      PrivateScope.addPrivate(
          RHSVD, IsArray ? GetAddrOfLocalVar(PrivateVD).withElementType(
                               ConvertTypeForMem(RHSVD->getType()))
                         : GetAddrOfLocalVar(PrivateVD));
    }
    ++ILHS;
    ++IRHS;
    ++IPriv;
    ++Count;
  }
  if (!Data.ReductionVars.empty()) {
    Data.IsReductionWithTaskMod = true;
    Data.IsWorksharingReduction =
        isOpenMPWorksharingDirective(D.getDirectiveKind());
    llvm::Value *ReductionDesc = CGM.getOpenMPRuntime().emitTaskReductionInit(
        *this, D.getBeginLoc(), TaskLHSs, TaskRHSs, Data);
    const Expr *TaskRedRef = nullptr;
    switch (D.getDirectiveKind()) {
    case OMPD_parallel:
      TaskRedRef = cast<OMPParallelDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_for:
      TaskRedRef = cast<OMPForDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_sections:
      TaskRedRef = cast<OMPSectionsDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_parallel_for:
      TaskRedRef = cast<OMPParallelForDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_parallel_master:
      TaskRedRef =
          cast<OMPParallelMasterDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_parallel_sections:
      TaskRedRef =
          cast<OMPParallelSectionsDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_target_parallel:
      TaskRedRef =
          cast<OMPTargetParallelDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_target_parallel_for:
      TaskRedRef =
          cast<OMPTargetParallelForDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_distribute_parallel_for:
      TaskRedRef =
          cast<OMPDistributeParallelForDirective>(D).getTaskReductionRefExpr();
      break;
    case OMPD_teams_distribute_parallel_for:
      TaskRedRef = cast<OMPTeamsDistributeParallelForDirective>(D)
                       .getTaskReductionRefExpr();
      break;
    case OMPD_target_teams_distribute_parallel_for:
      TaskRedRef = cast<OMPTargetTeamsDistributeParallelForDirective>(D)
                       .getTaskReductionRefExpr();
      break;
    case OMPD_simd:
    case OMPD_for_simd:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_parallel_for_simd:
    case OMPD_task:
    case OMPD_taskyield:
    case OMPD_error:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_flush:
    case OMPD_depobj:
    case OMPD_scan:
    case OMPD_ordered:
    case OMPD_atomic:
    case OMPD_teams:
    case OMPD_target:
    case OMPD_cancellation_point:
    case OMPD_cancel:
    case OMPD_target_data:
    case OMPD_target_enter_data:
    case OMPD_target_exit_data:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_master_taskloop:
    case OMPD_master_taskloop_simd:
    case OMPD_parallel_master_taskloop:
    case OMPD_parallel_master_taskloop_simd:
    case OMPD_distribute:
    case OMPD_target_update:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_distribute_simd:
    case OMPD_target_parallel_for_simd:
    case OMPD_target_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_target_teams:
    case OMPD_target_teams_distribute:
    case OMPD_target_teams_distribute_parallel_for_simd:
    case OMPD_target_teams_distribute_simd:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_threadprivate:
    case OMPD_allocate:
    case OMPD_declare_reduction:
    case OMPD_declare_mapper:
    case OMPD_declare_simd:
    case OMPD_requires:
    case OMPD_declare_variant:
    case OMPD_begin_declare_variant:
    case OMPD_end_declare_variant:
    case OMPD_unknown:
    default:
      llvm_unreachable("Unexpected directive with task reductions.");
    }

    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(TaskRedRef)->getDecl());
    EmitVarDecl(*VD);
    EmitStoreOfScalar(ReductionDesc, GetAddrOfLocalVar(VD),
                      /*Volatile=*/false, TaskRedRef->getType());
  }
}

void CodeGenFunction::EmitOMPReductionClauseFinal(
    const OMPExecutableDirective &D, const OpenMPDirectiveKind ReductionKind) {
  if (!HaveInsertPoint())
    return;
  llvm::SmallVector<const Expr *, 8> Privates;
  llvm::SmallVector<const Expr *, 8> LHSExprs;
  llvm::SmallVector<const Expr *, 8> RHSExprs;
  llvm::SmallVector<const Expr *, 8> ReductionOps;
  bool HasAtLeastOneReduction = false;
  bool IsReductionWithTaskMod = false;
  for (const auto *C : D.getClausesOfKind<OMPReductionClause>()) {
    // Do not emit for inscan reductions.
    if (C->getModifier() == OMPC_REDUCTION_inscan)
      continue;
    HasAtLeastOneReduction = true;
    Privates.append(C->privates().begin(), C->privates().end());
    LHSExprs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
    RHSExprs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
    ReductionOps.append(C->reduction_ops().begin(), C->reduction_ops().end());
    IsReductionWithTaskMod =
        IsReductionWithTaskMod || C->getModifier() == OMPC_REDUCTION_task;
  }
  if (HasAtLeastOneReduction) {
    if (IsReductionWithTaskMod) {
      CGM.getOpenMPRuntime().emitTaskReductionFini(
          *this, D.getBeginLoc(),
          isOpenMPWorksharingDirective(D.getDirectiveKind()));
    }
    bool TeamsLoopCanBeParallel = false;
    if (auto *TTLD = dyn_cast<OMPTargetTeamsGenericLoopDirective>(&D))
      TeamsLoopCanBeParallel = TTLD->canBeParallelFor();
    bool WithNowait = D.getSingleClause<OMPNowaitClause>() ||
                      isOpenMPParallelDirective(D.getDirectiveKind()) ||
                      TeamsLoopCanBeParallel || ReductionKind == OMPD_simd;
    bool SimpleReduction = ReductionKind == OMPD_simd;
    // Emit nowait reduction if nowait clause is present or directive is a
    // parallel directive (it always has implicit barrier).
    CGM.getOpenMPRuntime().emitReduction(
        *this, D.getEndLoc(), Privates, LHSExprs, RHSExprs, ReductionOps,
        {WithNowait, SimpleReduction, ReductionKind});
  }
}

static void emitPostUpdateForReductionClause(
    CodeGenFunction &CGF, const OMPExecutableDirective &D,
    const llvm::function_ref<llvm::Value *(CodeGenFunction &)> CondGen) {
  if (!CGF.HaveInsertPoint())
    return;
  llvm::BasicBlock *DoneBB = nullptr;
  for (const auto *C : D.getClausesOfKind<OMPReductionClause>()) {
    if (const Expr *PostUpdate = C->getPostUpdateExpr()) {
      if (!DoneBB) {
        if (llvm::Value *Cond = CondGen(CGF)) {
          // If the first post-update expression is found, emit conditional
          // block if it was requested.
          llvm::BasicBlock *ThenBB = CGF.createBasicBlock(".omp.reduction.pu");
          DoneBB = CGF.createBasicBlock(".omp.reduction.pu.done");
          CGF.Builder.CreateCondBr(Cond, ThenBB, DoneBB);
          CGF.EmitBlock(ThenBB);
        }
      }
      CGF.EmitIgnoredExpr(PostUpdate);
    }
  }
  if (DoneBB)
    CGF.EmitBlock(DoneBB, /*IsFinished=*/true);
}

namespace {
/// Codegen lambda for appending distribute lower and upper bounds to outlined
/// parallel function. This is necessary for combined constructs such as
/// 'distribute parallel for'
typedef llvm::function_ref<void(CodeGenFunction &,
                                const OMPExecutableDirective &,
                                llvm::SmallVectorImpl<llvm::Value *> &)>
    CodeGenBoundParametersTy;
} // anonymous namespace

static void
checkForLastprivateConditionalUpdate(CodeGenFunction &CGF,
                                     const OMPExecutableDirective &S) {
  if (CGF.getLangOpts().OpenMP < 50)
    return;
  llvm::DenseSet<CanonicalDeclPtr<const VarDecl>> PrivateDecls;
  for (const auto *C : S.getClausesOfKind<OMPReductionClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      PrivateDecls.insert(cast<VarDecl>(DRE->getDecl()));
      CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, Ref);
    }
  }
  for (const auto *C : S.getClausesOfKind<OMPLastprivateClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      PrivateDecls.insert(cast<VarDecl>(DRE->getDecl()));
      CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, Ref);
    }
  }
  for (const auto *C : S.getClausesOfKind<OMPLinearClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      PrivateDecls.insert(cast<VarDecl>(DRE->getDecl()));
      CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, Ref);
    }
  }
  // Privates should ne analyzed since they are not captured at all.
  // Task reductions may be skipped - tasks are ignored.
  // Firstprivates do not return value but may be passed by reference - no need
  // to check for updated lastprivate conditional.
  for (const auto *C : S.getClausesOfKind<OMPFirstprivateClause>()) {
    for (const Expr *Ref : C->varlists()) {
      if (!Ref->getType()->isScalarType())
        continue;
      const auto *DRE = dyn_cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
      if (!DRE)
        continue;
      PrivateDecls.insert(cast<VarDecl>(DRE->getDecl()));
    }
  }
  CGF.CGM.getOpenMPRuntime().checkAndEmitSharedLastprivateConditional(
      CGF, S, PrivateDecls);
}

static void emitCommonOMPParallelDirective(
    CodeGenFunction &CGF, const OMPExecutableDirective &S,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen,
    const CodeGenBoundParametersTy &CodeGenBoundParameters) {
  const CapturedStmt *CS = S.getCapturedStmt(OMPD_parallel);
  llvm::Value *NumThreads = nullptr;
  llvm::Function *OutlinedFn =
      CGF.CGM.getOpenMPRuntime().emitParallelOutlinedFunction(
          CGF, S, *CS->getCapturedDecl()->param_begin(), InnermostKind,
          CodeGen);
  if (const auto *NumThreadsClause = S.getSingleClause<OMPNumThreadsClause>()) {
    CodeGenFunction::RunCleanupsScope NumThreadsScope(CGF);
    NumThreads = CGF.EmitScalarExpr(NumThreadsClause->getNumThreads(),
                                    /*IgnoreResultAssign=*/true);
    CGF.CGM.getOpenMPRuntime().emitNumThreadsClause(
        CGF, NumThreads, NumThreadsClause->getBeginLoc());
  }
  if (const auto *ProcBindClause = S.getSingleClause<OMPProcBindClause>()) {
    CodeGenFunction::RunCleanupsScope ProcBindScope(CGF);
    CGF.CGM.getOpenMPRuntime().emitProcBindClause(
        CGF, ProcBindClause->getProcBindKind(), ProcBindClause->getBeginLoc());
  }
  const Expr *IfCond = nullptr;
  for (const auto *C : S.getClausesOfKind<OMPIfClause>()) {
    if (C->getNameModifier() == OMPD_unknown ||
        C->getNameModifier() == OMPD_parallel) {
      IfCond = C->getCondition();
      break;
    }
  }

  OMPParallelScope Scope(CGF, S);
  llvm::SmallVector<llvm::Value *, 16> CapturedVars;
  // Combining 'distribute' with 'for' requires sharing each 'distribute' chunk
  // lower and upper bounds with the pragma 'for' chunking mechanism.
  // The following lambda takes care of appending the lower and upper bound
  // parameters when necessary
  CodeGenBoundParameters(CGF, S, CapturedVars);
  CGF.GenerateOpenMPCapturedVars(*CS, CapturedVars);
  CGF.CGM.getOpenMPRuntime().emitParallelCall(CGF, S.getBeginLoc(), OutlinedFn,
                                              CapturedVars, IfCond, NumThreads);
}

static bool isAllocatableDecl(const VarDecl *VD) {
  const VarDecl *CVD = VD->getCanonicalDecl();
  if (!CVD->hasAttr<OMPAllocateDeclAttr>())
    return false;
  const auto *AA = CVD->getAttr<OMPAllocateDeclAttr>();
  // Use the default allocation.
  return !((AA->getAllocatorType() == OMPAllocateDeclAttr::OMPDefaultMemAlloc ||
            AA->getAllocatorType() == OMPAllocateDeclAttr::OMPNullMemAlloc) &&
           !AA->getAllocator());
}

static void emitEmptyBoundParameters(CodeGenFunction &,
                                     const OMPExecutableDirective &,
                                     llvm::SmallVectorImpl<llvm::Value *> &) {}

static void emitOMPCopyinClause(CodeGenFunction &CGF,
                                const OMPExecutableDirective &S) {
  bool Copyins = CGF.EmitOMPCopyinClause(S);
  if (Copyins) {
    // Emit implicit barrier to synchronize threads and avoid data races on
    // propagation master's thread values of threadprivate variables to local
    // instances of that variables of all other implicit threads.
    CGF.CGM.getOpenMPRuntime().emitBarrierCall(
        CGF, S.getBeginLoc(), OMPD_unknown, /*EmitChecks=*/false,
        /*ForceSimpleCall=*/true);
  }
}

Address CodeGenFunction::OMPBuilderCBHelpers::getAddressOfLocalVariable(
    CodeGenFunction &CGF, const VarDecl *VD) {
  CodeGenModule &CGM = CGF.CGM;
  auto &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();

  if (!VD)
    return Address::invalid();
  const VarDecl *CVD = VD->getCanonicalDecl();
  if (!isAllocatableDecl(CVD))
    return Address::invalid();
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

  const auto *AA = CVD->getAttr<OMPAllocateDeclAttr>();
  assert(AA->getAllocator() &&
         "Expected allocator expression for non-default allocator.");
  llvm::Value *Allocator = CGF.EmitScalarExpr(AA->getAllocator());
  // According to the standard, the original allocator type is a enum (integer).
  // Convert to pointer type, if required.
  if (Allocator->getType()->isIntegerTy())
    Allocator = CGF.Builder.CreateIntToPtr(Allocator, CGM.VoidPtrTy);
  else if (Allocator->getType()->isPointerTy())
    Allocator = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(Allocator,
                                                                CGM.VoidPtrTy);

  llvm::Value *Addr = OMPBuilder.createOMPAlloc(
      CGF.Builder, Size, Allocator,
      getNameWithSeparators({CVD->getName(), ".void.addr"}, ".", "."));
  llvm::CallInst *FreeCI =
      OMPBuilder.createOMPFree(CGF.Builder, Addr, Allocator);

  CGF.EHStack.pushCleanup<OMPAllocateCleanupTy>(NormalAndEHCleanup, FreeCI);
  Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      Addr,
      CGF.ConvertTypeForMem(CGM.getContext().getPointerType(CVD->getType())),
      getNameWithSeparators({CVD->getName(), ".addr"}, ".", "."));
  return Address(Addr, CGF.ConvertTypeForMem(CVD->getType()), Align);
}

Address CodeGenFunction::OMPBuilderCBHelpers::getAddrOfThreadPrivate(
    CodeGenFunction &CGF, const VarDecl *VD, Address VDAddr,
    SourceLocation Loc) {
  CodeGenModule &CGM = CGF.CGM;
  if (CGM.getLangOpts().OpenMPUseTLS &&
      CGM.getContext().getTargetInfo().isTLSSupported())
    return VDAddr;

  llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();

  llvm::Type *VarTy = VDAddr.getElementType();
  llvm::Value *Data =
      CGF.Builder.CreatePointerCast(VDAddr.emitRawPointer(CGF), CGM.Int8PtrTy);
  llvm::ConstantInt *Size = CGM.getSize(CGM.GetTargetTypeStoreSize(VarTy));
  std::string Suffix = getNameWithSeparators({"cache", ""});
  llvm::Twine CacheName = Twine(CGM.getMangledName(VD)).concat(Suffix);

  llvm::CallInst *ThreadPrivateCacheCall =
      OMPBuilder.createCachedThreadPrivate(CGF.Builder, Data, Size, CacheName);

  return Address(ThreadPrivateCacheCall, CGM.Int8Ty, VDAddr.getAlignment());
}

std::string CodeGenFunction::OMPBuilderCBHelpers::getNameWithSeparators(
    ArrayRef<StringRef> Parts, StringRef FirstSeparator, StringRef Separator) {
  SmallString<128> Buffer;
  llvm::raw_svector_ostream OS(Buffer);
  StringRef Sep = FirstSeparator;
  for (StringRef Part : Parts) {
    OS << Sep << Part;
    Sep = Separator;
  }
  return OS.str().str();
}

void CodeGenFunction::OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
    CodeGenFunction &CGF, const Stmt *RegionBodyStmt, InsertPointTy AllocaIP,
    InsertPointTy CodeGenIP, Twine RegionName) {
  CGBuilderTy &Builder = CGF.Builder;
  Builder.restoreIP(CodeGenIP);
  llvm::BasicBlock *FiniBB = splitBBWithSuffix(Builder, /*CreateBranch=*/false,
                                               "." + RegionName + ".after");

  {
    OMPBuilderCBHelpers::InlinedRegionBodyRAII IRB(CGF, AllocaIP, *FiniBB);
    CGF.EmitStmt(RegionBodyStmt);
  }

  if (Builder.saveIP().isSet())
    Builder.CreateBr(FiniBB);
}

void CodeGenFunction::OMPBuilderCBHelpers::EmitOMPOutlinedRegionBody(
    CodeGenFunction &CGF, const Stmt *RegionBodyStmt, InsertPointTy AllocaIP,
    InsertPointTy CodeGenIP, Twine RegionName) {
  CGBuilderTy &Builder = CGF.Builder;
  Builder.restoreIP(CodeGenIP);
  llvm::BasicBlock *FiniBB = splitBBWithSuffix(Builder, /*CreateBranch=*/false,
                                               "." + RegionName + ".after");

  {
    OMPBuilderCBHelpers::OutlinedRegionBodyRAII IRB(CGF, AllocaIP, *FiniBB);
    CGF.EmitStmt(RegionBodyStmt);
  }

  if (Builder.saveIP().isSet())
    Builder.CreateBr(FiniBB);
}

void CodeGenFunction::EmitOMPParallelDirective(const OMPParallelDirective &S) {
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    // Check if we have any if clause associated with the directive.
    llvm::Value *IfCond = nullptr;
    if (const auto *C = S.getSingleClause<OMPIfClause>())
      IfCond = EmitScalarExpr(C->getCondition(),
                              /*IgnoreResultAssign=*/true);

    llvm::Value *NumThreads = nullptr;
    if (const auto *NumThreadsClause = S.getSingleClause<OMPNumThreadsClause>())
      NumThreads = EmitScalarExpr(NumThreadsClause->getNumThreads(),
                                  /*IgnoreResultAssign=*/true);

    ProcBindKind ProcBind = OMP_PROC_BIND_default;
    if (const auto *ProcBindClause = S.getSingleClause<OMPProcBindClause>())
      ProcBind = ProcBindClause->getProcBindKind();

    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

    // The cleanup callback that finalizes all variables at the given location,
    // thus calls destructors etc.
    auto FiniCB = [this](InsertPointTy IP) {
      OMPBuilderCBHelpers::FinalizeOMPRegion(*this, IP);
    };

    // Privatization callback that performs appropriate action for
    // shared/private/firstprivate/lastprivate/copyin/... variables.
    //
    // TODO: This defaults to shared right now.
    auto PrivCB = [](InsertPointTy AllocaIP, InsertPointTy CodeGenIP,
                     llvm::Value &, llvm::Value &Val, llvm::Value *&ReplVal) {
      // The next line is appropriate only for variables (Val) with the
      // data-sharing attribute "shared".
      ReplVal = &Val;

      return CodeGenIP;
    };

    const CapturedStmt *CS = S.getCapturedStmt(OMPD_parallel);
    const Stmt *ParallelRegionBodyStmt = CS->getCapturedStmt();

    auto BodyGenCB = [&, this](InsertPointTy AllocaIP,
                               InsertPointTy CodeGenIP) {
      OMPBuilderCBHelpers::EmitOMPOutlinedRegionBody(
          *this, ParallelRegionBodyStmt, AllocaIP, CodeGenIP, "parallel");
    };

    CGCapturedStmtInfo CGSI(*CS, CR_OpenMP);
    CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(*this, &CGSI);
    llvm::OpenMPIRBuilder::InsertPointTy AllocaIP(
        AllocaInsertPt->getParent(), AllocaInsertPt->getIterator());
    Builder.restoreIP(
        OMPBuilder.createParallel(Builder, AllocaIP, BodyGenCB, PrivCB, FiniCB,
                                  IfCond, NumThreads, ProcBind, S.hasCancel()));
    return;
  }

  // Emit parallel region as a standalone region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    emitOMPCopyinClause(CGF, S);
    (void)CGF.EmitOMPFirstprivateClause(S, PrivateScope);
    CGF.EmitOMPPrivateClause(S, PrivateScope);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.EmitStmt(S.getCapturedStmt(OMPD_parallel)->getCapturedStmt());
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_parallel);
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    emitCommonOMPParallelDirective(*this, S, OMPD_parallel, CodeGen,
                                   emitEmptyBoundParameters);
    emitPostUpdateForReductionClause(*this, S,
                                     [](CodeGenFunction &) { return nullptr; });
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPMetaDirective(const OMPMetaDirective &S) {
  EmitStmt(S.getIfStmt());
}

namespace {
/// RAII to handle scopes for loop transformation directives.
class OMPTransformDirectiveScopeRAII {
  OMPLoopScope *Scope = nullptr;
  CodeGenFunction::CGCapturedStmtInfo *CGSI = nullptr;
  CodeGenFunction::CGCapturedStmtRAII *CapInfoRAII = nullptr;

  OMPTransformDirectiveScopeRAII(const OMPTransformDirectiveScopeRAII &) =
      delete;
  OMPTransformDirectiveScopeRAII &
  operator=(const OMPTransformDirectiveScopeRAII &) = delete;

public:
  OMPTransformDirectiveScopeRAII(CodeGenFunction &CGF, const Stmt *S) {
    if (const auto *Dir = dyn_cast<OMPLoopBasedDirective>(S)) {
      Scope = new OMPLoopScope(CGF, *Dir);
      CGSI = new CodeGenFunction::CGCapturedStmtInfo(CR_OpenMP);
      CapInfoRAII = new CodeGenFunction::CGCapturedStmtRAII(CGF, CGSI);
    }
  }
  ~OMPTransformDirectiveScopeRAII() {
    if (!Scope)
      return;
    delete CapInfoRAII;
    delete CGSI;
    delete Scope;
  }
};
} // namespace

static void emitBody(CodeGenFunction &CGF, const Stmt *S, const Stmt *NextLoop,
                     int MaxLevel, int Level = 0) {
  assert(Level < MaxLevel && "Too deep lookup during loop body codegen.");
  const Stmt *SimplifiedS = S->IgnoreContainers();
  if (const auto *CS = dyn_cast<CompoundStmt>(SimplifiedS)) {
    PrettyStackTraceLoc CrashInfo(
        CGF.getContext().getSourceManager(), CS->getLBracLoc(),
        "LLVM IR generation of compound statement ('{}')");

    // Keep track of the current cleanup stack depth, including debug scopes.
    CodeGenFunction::LexicalScope Scope(CGF, S->getSourceRange());
    for (const Stmt *CurStmt : CS->body())
      emitBody(CGF, CurStmt, NextLoop, MaxLevel, Level);
    return;
  }
  if (SimplifiedS == NextLoop) {
    if (auto *Dir = dyn_cast<OMPLoopTransformationDirective>(SimplifiedS))
      SimplifiedS = Dir->getTransformedStmt();
    if (const auto *CanonLoop = dyn_cast<OMPCanonicalLoop>(SimplifiedS))
      SimplifiedS = CanonLoop->getLoopStmt();
    if (const auto *For = dyn_cast<ForStmt>(SimplifiedS)) {
      S = For->getBody();
    } else {
      assert(isa<CXXForRangeStmt>(SimplifiedS) &&
             "Expected canonical for loop or range-based for loop.");
      const auto *CXXFor = cast<CXXForRangeStmt>(SimplifiedS);
      CGF.EmitStmt(CXXFor->getLoopVarStmt());
      S = CXXFor->getBody();
    }
    if (Level + 1 < MaxLevel) {
      NextLoop = OMPLoopDirective::tryToFindNextInnerLoop(
          S, /*TryImperfectlyNestedLoops=*/true);
      emitBody(CGF, S, NextLoop, MaxLevel, Level + 1);
      return;
    }
  }
  CGF.EmitStmt(S);
}

void CodeGenFunction::EmitOMPLoopBody(const OMPLoopDirective &D,
                                      JumpDest LoopExit) {
  RunCleanupsScope BodyScope(*this);
  // Update counters values on current iteration.
  for (const Expr *UE : D.updates())
    EmitIgnoredExpr(UE);
  // Update the linear variables.
  // In distribute directives only loop counters may be marked as linear, no
  // need to generate the code for them.
  if (!isOpenMPDistributeDirective(D.getDirectiveKind())) {
    for (const auto *C : D.getClausesOfKind<OMPLinearClause>()) {
      for (const Expr *UE : C->updates())
        EmitIgnoredExpr(UE);
    }
  }

  // On a continue in the body, jump to the end.
  JumpDest Continue = getJumpDestInCurrentScope("omp.body.continue");
  BreakContinueStack.push_back(BreakContinue(LoopExit, Continue));
  for (const Expr *E : D.finals_conditions()) {
    if (!E)
      continue;
    // Check that loop counter in non-rectangular nest fits into the iteration
    // space.
    llvm::BasicBlock *NextBB = createBasicBlock("omp.body.next");
    EmitBranchOnBoolExpr(E, NextBB, Continue.getBlock(),
                         getProfileCount(D.getBody()));
    EmitBlock(NextBB);
  }

  OMPPrivateScope InscanScope(*this);
  EmitOMPReductionClauseInit(D, InscanScope, /*ForInscan=*/true);
  bool IsInscanRegion = InscanScope.Privatize();
  if (IsInscanRegion) {
    // Need to remember the block before and after scan directive
    // to dispatch them correctly depending on the clause used in
    // this directive, inclusive or exclusive. For inclusive scan the natural
    // order of the blocks is used, for exclusive clause the blocks must be
    // executed in reverse order.
    OMPBeforeScanBlock = createBasicBlock("omp.before.scan.bb");
    OMPAfterScanBlock = createBasicBlock("omp.after.scan.bb");
    // No need to allocate inscan exit block, in simd mode it is selected in the
    // codegen for the scan directive.
    if (D.getDirectiveKind() != OMPD_simd && !getLangOpts().OpenMPSimd)
      OMPScanExitBlock = createBasicBlock("omp.exit.inscan.bb");
    OMPScanDispatch = createBasicBlock("omp.inscan.dispatch");
    EmitBranch(OMPScanDispatch);
    EmitBlock(OMPBeforeScanBlock);
  }

  // Emit loop variables for C++ range loops.
  const Stmt *Body =
      D.getInnermostCapturedStmt()->getCapturedStmt()->IgnoreContainers();
  // Emit loop body.
  emitBody(*this, Body,
           OMPLoopBasedDirective::tryToFindNextInnerLoop(
               Body, /*TryImperfectlyNestedLoops=*/true),
           D.getLoopsNumber());

  // Jump to the dispatcher at the end of the loop body.
  if (IsInscanRegion)
    EmitBranch(OMPScanExitBlock);

  // The end (updates/cleanups).
  EmitBlock(Continue.getBlock());
  BreakContinueStack.pop_back();
}

using EmittedClosureTy = std::pair<llvm::Function *, llvm::Value *>;

/// Emit a captured statement and return the function as well as its captured
/// closure context.
static EmittedClosureTy emitCapturedStmtFunc(CodeGenFunction &ParentCGF,
                                             const CapturedStmt *S) {
  LValue CapStruct = ParentCGF.InitCapturedStruct(*S);
  CodeGenFunction CGF(ParentCGF.CGM, /*suppressNewContext=*/true);
  std::unique_ptr<CodeGenFunction::CGCapturedStmtInfo> CSI =
      std::make_unique<CodeGenFunction::CGCapturedStmtInfo>(*S);
  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, CSI.get());
  llvm::Function *F = CGF.GenerateCapturedStmtFunction(*S);

  return {F, CapStruct.getPointer(ParentCGF)};
}

/// Emit a call to a previously captured closure.
static llvm::CallInst *
emitCapturedStmtCall(CodeGenFunction &ParentCGF, EmittedClosureTy Cap,
                     llvm::ArrayRef<llvm::Value *> Args) {
  // Append the closure context to the argument.
  SmallVector<llvm::Value *> EffectiveArgs;
  EffectiveArgs.reserve(Args.size() + 1);
  llvm::append_range(EffectiveArgs, Args);
  EffectiveArgs.push_back(Cap.second);

  return ParentCGF.Builder.CreateCall(Cap.first, EffectiveArgs);
}

llvm::CanonicalLoopInfo *
CodeGenFunction::EmitOMPCollapsedCanonicalLoopNest(const Stmt *S, int Depth) {
  assert(Depth == 1 && "Nested loops with OpenMPIRBuilder not yet implemented");

  // The caller is processing the loop-associated directive processing the \p
  // Depth loops nested in \p S. Put the previous pending loop-associated
  // directive to the stack. If the current loop-associated directive is a loop
  // transformation directive, it will push its generated loops onto the stack
  // such that together with the loops left here they form the combined loop
  // nest for the parent loop-associated directive.
  int ParentExpectedOMPLoopDepth = ExpectedOMPLoopDepth;
  ExpectedOMPLoopDepth = Depth;

  EmitStmt(S);
  assert(OMPLoopNestStack.size() >= (size_t)Depth && "Found too few loops");

  // The last added loop is the outermost one.
  llvm::CanonicalLoopInfo *Result = OMPLoopNestStack.back();

  // Pop the \p Depth loops requested by the call from that stack and restore
  // the previous context.
  OMPLoopNestStack.pop_back_n(Depth);
  ExpectedOMPLoopDepth = ParentExpectedOMPLoopDepth;

  return Result;
}

void CodeGenFunction::EmitOMPCanonicalLoop(const OMPCanonicalLoop *S) {
  const Stmt *SyntacticalLoop = S->getLoopStmt();
  if (!getLangOpts().OpenMPIRBuilder) {
    // Ignore if OpenMPIRBuilder is not enabled.
    EmitStmt(SyntacticalLoop);
    return;
  }

  LexicalScope ForScope(*this, S->getSourceRange());

  // Emit init statements. The Distance/LoopVar funcs may reference variable
  // declarations they contain.
  const Stmt *BodyStmt;
  if (const auto *For = dyn_cast<ForStmt>(SyntacticalLoop)) {
    if (const Stmt *InitStmt = For->getInit())
      EmitStmt(InitStmt);
    BodyStmt = For->getBody();
  } else if (const auto *RangeFor =
                 dyn_cast<CXXForRangeStmt>(SyntacticalLoop)) {
    if (const DeclStmt *RangeStmt = RangeFor->getRangeStmt())
      EmitStmt(RangeStmt);
    if (const DeclStmt *BeginStmt = RangeFor->getBeginStmt())
      EmitStmt(BeginStmt);
    if (const DeclStmt *EndStmt = RangeFor->getEndStmt())
      EmitStmt(EndStmt);
    if (const DeclStmt *LoopVarStmt = RangeFor->getLoopVarStmt())
      EmitStmt(LoopVarStmt);
    BodyStmt = RangeFor->getBody();
  } else
    llvm_unreachable("Expected for-stmt or range-based for-stmt");

  // Emit closure for later use. By-value captures will be captured here.
  const CapturedStmt *DistanceFunc = S->getDistanceFunc();
  EmittedClosureTy DistanceClosure = emitCapturedStmtFunc(*this, DistanceFunc);
  const CapturedStmt *LoopVarFunc = S->getLoopVarFunc();
  EmittedClosureTy LoopVarClosure = emitCapturedStmtFunc(*this, LoopVarFunc);

  // Call the distance function to get the number of iterations of the loop to
  // come.
  QualType LogicalTy = DistanceFunc->getCapturedDecl()
                           ->getParam(0)
                           ->getType()
                           .getNonReferenceType();
  RawAddress CountAddr = CreateMemTemp(LogicalTy, ".count.addr");
  emitCapturedStmtCall(*this, DistanceClosure, {CountAddr.getPointer()});
  llvm::Value *DistVal = Builder.CreateLoad(CountAddr, ".count");

  // Emit the loop structure.
  llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
  auto BodyGen = [&, this](llvm::OpenMPIRBuilder::InsertPointTy CodeGenIP,
                           llvm::Value *IndVar) {
    Builder.restoreIP(CodeGenIP);

    // Emit the loop body: Convert the logical iteration number to the loop
    // variable and emit the body.
    const DeclRefExpr *LoopVarRef = S->getLoopVarRef();
    LValue LCVal = EmitLValue(LoopVarRef);
    Address LoopVarAddress = LCVal.getAddress();
    emitCapturedStmtCall(*this, LoopVarClosure,
                         {LoopVarAddress.emitRawPointer(*this), IndVar});

    RunCleanupsScope BodyScope(*this);
    EmitStmt(BodyStmt);
  };
  llvm::CanonicalLoopInfo *CL =
      OMPBuilder.createCanonicalLoop(Builder, BodyGen, DistVal);

  // Finish up the loop.
  Builder.restoreIP(CL->getAfterIP());
  ForScope.ForceCleanup();

  // Remember the CanonicalLoopInfo for parent AST nodes consuming it.
  OMPLoopNestStack.push_back(CL);
}

void CodeGenFunction::EmitOMPInnerLoop(
    const OMPExecutableDirective &S, bool RequiresCleanup, const Expr *LoopCond,
    const Expr *IncExpr,
    const llvm::function_ref<void(CodeGenFunction &)> BodyGen,
    const llvm::function_ref<void(CodeGenFunction &)> PostIncGen) {
  auto LoopExit = getJumpDestInCurrentScope("omp.inner.for.end");

  // Start the loop with a block that tests the condition.
  auto CondBlock = createBasicBlock("omp.inner.for.cond");
  EmitBlock(CondBlock);
  const SourceRange R = S.getSourceRange();

  // If attributes are attached, push to the basic block with them.
  const auto &OMPED = cast<OMPExecutableDirective>(S);
  const CapturedStmt *ICS = OMPED.getInnermostCapturedStmt();
  const Stmt *SS = ICS->getCapturedStmt();
  const AttributedStmt *AS = dyn_cast_or_null<AttributedStmt>(SS);
  OMPLoopNestStack.clear();
  if (AS)
    LoopStack.push(CondBlock, CGM.getContext(), CGM.getCodeGenOpts(),
                   AS->getAttrs(), SourceLocToDebugLoc(R.getBegin()),
                   SourceLocToDebugLoc(R.getEnd()));
  else
    LoopStack.push(CondBlock, SourceLocToDebugLoc(R.getBegin()),
                   SourceLocToDebugLoc(R.getEnd()));

  // If there are any cleanups between here and the loop-exit scope,
  // create a block to stage a loop exit along.
  llvm::BasicBlock *ExitBlock = LoopExit.getBlock();
  if (RequiresCleanup)
    ExitBlock = createBasicBlock("omp.inner.for.cond.cleanup");

  llvm::BasicBlock *LoopBody = createBasicBlock("omp.inner.for.body");

  // Emit condition.
  EmitBranchOnBoolExpr(LoopCond, LoopBody, ExitBlock, getProfileCount(&S));
  if (ExitBlock != LoopExit.getBlock()) {
    EmitBlock(ExitBlock);
    EmitBranchThroughCleanup(LoopExit);
  }

  EmitBlock(LoopBody);
  incrementProfileCounter(&S);

  // Create a block for the increment.
  JumpDest Continue = getJumpDestInCurrentScope("omp.inner.for.inc");
  BreakContinueStack.push_back(BreakContinue(LoopExit, Continue));

  BodyGen(*this);

  // Emit "IV = IV + 1" and a back-edge to the condition block.
  EmitBlock(Continue.getBlock());
  EmitIgnoredExpr(IncExpr);
  PostIncGen(*this);
  BreakContinueStack.pop_back();
  EmitBranch(CondBlock);
  LoopStack.pop();
  // Emit the fall-through block.
  EmitBlock(LoopExit.getBlock());
}

bool CodeGenFunction::EmitOMPLinearClauseInit(const OMPLoopDirective &D) {
  if (!HaveInsertPoint())
    return false;
  // Emit inits for the linear variables.
  bool HasLinears = false;
  for (const auto *C : D.getClausesOfKind<OMPLinearClause>()) {
    for (const Expr *Init : C->inits()) {
      HasLinears = true;
      const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(Init)->getDecl());
      if (const auto *Ref =
              dyn_cast<DeclRefExpr>(VD->getInit()->IgnoreImpCasts())) {
        AutoVarEmission Emission = EmitAutoVarAlloca(*VD);
        const auto *OrigVD = cast<VarDecl>(Ref->getDecl());
        DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(OrigVD),
                        CapturedStmtInfo->lookup(OrigVD) != nullptr,
                        VD->getInit()->getType(), VK_LValue,
                        VD->getInit()->getExprLoc());
        EmitExprAsInit(
            &DRE, VD,
            MakeAddrLValue(Emission.getAllocatedAddress(), VD->getType()),
            /*capturedByInit=*/false);
        EmitAutoVarCleanups(Emission);
      } else {
        EmitVarDecl(*VD);
      }
    }
    // Emit the linear steps for the linear clauses.
    // If a step is not constant, it is pre-calculated before the loop.
    if (const auto *CS = cast_or_null<BinaryOperator>(C->getCalcStep()))
      if (const auto *SaveRef = cast<DeclRefExpr>(CS->getLHS())) {
        EmitVarDecl(*cast<VarDecl>(SaveRef->getDecl()));
        // Emit calculation of the linear step.
        EmitIgnoredExpr(CS);
      }
  }
  return HasLinears;
}

void CodeGenFunction::EmitOMPLinearClauseFinal(
    const OMPLoopDirective &D,
    const llvm::function_ref<llvm::Value *(CodeGenFunction &)> CondGen) {
  if (!HaveInsertPoint())
    return;
  llvm::BasicBlock *DoneBB = nullptr;
  // Emit the final values of the linear variables.
  for (const auto *C : D.getClausesOfKind<OMPLinearClause>()) {
    auto IC = C->varlist_begin();
    for (const Expr *F : C->finals()) {
      if (!DoneBB) {
        if (llvm::Value *Cond = CondGen(*this)) {
          // If the first post-update expression is found, emit conditional
          // block if it was requested.
          llvm::BasicBlock *ThenBB = createBasicBlock(".omp.linear.pu");
          DoneBB = createBasicBlock(".omp.linear.pu.done");
          Builder.CreateCondBr(Cond, ThenBB, DoneBB);
          EmitBlock(ThenBB);
        }
      }
      const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(*IC)->getDecl());
      DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(OrigVD),
                      CapturedStmtInfo->lookup(OrigVD) != nullptr,
                      (*IC)->getType(), VK_LValue, (*IC)->getExprLoc());
      Address OrigAddr = EmitLValue(&DRE).getAddress();
      CodeGenFunction::OMPPrivateScope VarScope(*this);
      VarScope.addPrivate(OrigVD, OrigAddr);
      (void)VarScope.Privatize();
      EmitIgnoredExpr(F);
      ++IC;
    }
    if (const Expr *PostUpdate = C->getPostUpdateExpr())
      EmitIgnoredExpr(PostUpdate);
  }
  if (DoneBB)
    EmitBlock(DoneBB, /*IsFinished=*/true);
}

static void emitAlignedClause(CodeGenFunction &CGF,
                              const OMPExecutableDirective &D) {
  if (!CGF.HaveInsertPoint())
    return;
  for (const auto *Clause : D.getClausesOfKind<OMPAlignedClause>()) {
    llvm::APInt ClauseAlignment(64, 0);
    if (const Expr *AlignmentExpr = Clause->getAlignment()) {
      auto *AlignmentCI =
          cast<llvm::ConstantInt>(CGF.EmitScalarExpr(AlignmentExpr));
      ClauseAlignment = AlignmentCI->getValue();
    }
    for (const Expr *E : Clause->varlists()) {
      llvm::APInt Alignment(ClauseAlignment);
      if (Alignment == 0) {
        // OpenMP [2.8.1, Description]
        // If no optional parameter is specified, implementation-defined default
        // alignments for SIMD instructions on the target platforms are assumed.
        Alignment =
            CGF.getContext()
                .toCharUnitsFromBits(CGF.getContext().getOpenMPDefaultSimdAlign(
                    E->getType()->getPointeeType()))
                .getQuantity();
      }
      assert((Alignment == 0 || Alignment.isPowerOf2()) &&
             "alignment is not power of 2");
      if (Alignment != 0) {
        llvm::Value *PtrValue = CGF.EmitScalarExpr(E);
        CGF.emitAlignmentAssumption(
            PtrValue, E, /*No second loc needed*/ SourceLocation(),
            llvm::ConstantInt::get(CGF.getLLVMContext(), Alignment));
      }
    }
  }
}

void CodeGenFunction::EmitOMPPrivateLoopCounters(
    const OMPLoopDirective &S, CodeGenFunction::OMPPrivateScope &LoopScope) {
  if (!HaveInsertPoint())
    return;
  auto I = S.private_counters().begin();
  for (const Expr *E : S.counters()) {
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    const auto *PrivateVD = cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl());
    // Emit var without initialization.
    AutoVarEmission VarEmission = EmitAutoVarAlloca(*PrivateVD);
    EmitAutoVarCleanups(VarEmission);
    LocalDeclMap.erase(PrivateVD);
    (void)LoopScope.addPrivate(VD, VarEmission.getAllocatedAddress());
    if (LocalDeclMap.count(VD) || CapturedStmtInfo->lookup(VD) ||
        VD->hasGlobalStorage()) {
      DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(VD),
                      LocalDeclMap.count(VD) || CapturedStmtInfo->lookup(VD),
                      E->getType(), VK_LValue, E->getExprLoc());
      (void)LoopScope.addPrivate(PrivateVD, EmitLValue(&DRE).getAddress());
    } else {
      (void)LoopScope.addPrivate(PrivateVD, VarEmission.getAllocatedAddress());
    }
    ++I;
  }
  // Privatize extra loop counters used in loops for ordered(n) clauses.
  for (const auto *C : S.getClausesOfKind<OMPOrderedClause>()) {
    if (!C->getNumForLoops())
      continue;
    for (unsigned I = S.getLoopsNumber(), E = C->getLoopNumIterations().size();
         I < E; ++I) {
      const auto *DRE = cast<DeclRefExpr>(C->getLoopCounter(I));
      const auto *VD = cast<VarDecl>(DRE->getDecl());
      // Override only those variables that can be captured to avoid re-emission
      // of the variables declared within the loops.
      if (DRE->refersToEnclosingVariableOrCapture()) {
        (void)LoopScope.addPrivate(
            VD, CreateMemTemp(DRE->getType(), VD->getName()));
      }
    }
  }
}

static void emitPreCond(CodeGenFunction &CGF, const OMPLoopDirective &S,
                        const Expr *Cond, llvm::BasicBlock *TrueBlock,
                        llvm::BasicBlock *FalseBlock, uint64_t TrueCount) {
  if (!CGF.HaveInsertPoint())
    return;
  {
    CodeGenFunction::OMPPrivateScope PreCondScope(CGF);
    CGF.EmitOMPPrivateLoopCounters(S, PreCondScope);
    (void)PreCondScope.Privatize();
    // Get initial values of real counters.
    for (const Expr *I : S.inits()) {
      CGF.EmitIgnoredExpr(I);
    }
  }
  // Create temp loop control variables with their init values to support
  // non-rectangular loops.
  CodeGenFunction::OMPMapVars PreCondVars;
  for (const Expr *E : S.dependent_counters()) {
    if (!E)
      continue;
    assert(!E->getType().getNonReferenceType()->isRecordType() &&
           "dependent counter must not be an iterator.");
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    Address CounterAddr =
        CGF.CreateMemTemp(VD->getType().getNonReferenceType());
    (void)PreCondVars.setVarAddr(CGF, VD, CounterAddr);
  }
  (void)PreCondVars.apply(CGF);
  for (const Expr *E : S.dependent_inits()) {
    if (!E)
      continue;
    CGF.EmitIgnoredExpr(E);
  }
  // Check that loop is executed at least one time.
  CGF.EmitBranchOnBoolExpr(Cond, TrueBlock, FalseBlock, TrueCount);
  PreCondVars.restore(CGF);
}

void CodeGenFunction::EmitOMPLinearClause(
    const OMPLoopDirective &D, CodeGenFunction::OMPPrivateScope &PrivateScope) {
  if (!HaveInsertPoint())
    return;
  llvm::DenseSet<const VarDecl *> SIMDLCVs;
  if (isOpenMPSimdDirective(D.getDirectiveKind())) {
    const auto *LoopDirective = cast<OMPLoopDirective>(&D);
    for (const Expr *C : LoopDirective->counters()) {
      SIMDLCVs.insert(
          cast<VarDecl>(cast<DeclRefExpr>(C)->getDecl())->getCanonicalDecl());
    }
  }
  for (const auto *C : D.getClausesOfKind<OMPLinearClause>()) {
    auto CurPrivate = C->privates().begin();
    for (const Expr *E : C->varlists()) {
      const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
      const auto *PrivateVD =
          cast<VarDecl>(cast<DeclRefExpr>(*CurPrivate)->getDecl());
      if (!SIMDLCVs.count(VD->getCanonicalDecl())) {
        // Emit private VarDecl with copy init.
        EmitVarDecl(*PrivateVD);
        bool IsRegistered =
            PrivateScope.addPrivate(VD, GetAddrOfLocalVar(PrivateVD));
        assert(IsRegistered && "linear var already registered as private");
        // Silence the warning about unused variable.
        (void)IsRegistered;
      } else {
        EmitVarDecl(*PrivateVD);
      }
      ++CurPrivate;
    }
  }
}

static void emitSimdlenSafelenClause(CodeGenFunction &CGF,
                                     const OMPExecutableDirective &D) {
  if (!CGF.HaveInsertPoint())
    return;
  if (const auto *C = D.getSingleClause<OMPSimdlenClause>()) {
    RValue Len = CGF.EmitAnyExpr(C->getSimdlen(), AggValueSlot::ignored(),
                                 /*ignoreResult=*/true);
    auto *Val = cast<llvm::ConstantInt>(Len.getScalarVal());
    CGF.LoopStack.setVectorizeWidth(Val->getZExtValue());
    // In presence of finite 'safelen', it may be unsafe to mark all
    // the memory instructions parallel, because loop-carried
    // dependences of 'safelen' iterations are possible.
    CGF.LoopStack.setParallel(!D.getSingleClause<OMPSafelenClause>());
  } else if (const auto *C = D.getSingleClause<OMPSafelenClause>()) {
    RValue Len = CGF.EmitAnyExpr(C->getSafelen(), AggValueSlot::ignored(),
                                 /*ignoreResult=*/true);
    auto *Val = cast<llvm::ConstantInt>(Len.getScalarVal());
    CGF.LoopStack.setVectorizeWidth(Val->getZExtValue());
    // In presence of finite 'safelen', it may be unsafe to mark all
    // the memory instructions parallel, because loop-carried
    // dependences of 'safelen' iterations are possible.
    CGF.LoopStack.setParallel(/*Enable=*/false);
  }
}

void CodeGenFunction::EmitOMPSimdInit(const OMPLoopDirective &D) {
  // Walk clauses and process safelen/lastprivate.
  LoopStack.setParallel(/*Enable=*/true);
  LoopStack.setVectorizeEnable();
  emitSimdlenSafelenClause(*this, D);
  if (const auto *C = D.getSingleClause<OMPOrderClause>())
    if (C->getKind() == OMPC_ORDER_concurrent)
      LoopStack.setParallel(/*Enable=*/true);
  if ((D.getDirectiveKind() == OMPD_simd ||
       (getLangOpts().OpenMPSimd &&
        isOpenMPSimdDirective(D.getDirectiveKind()))) &&
      llvm::any_of(D.getClausesOfKind<OMPReductionClause>(),
                   [](const OMPReductionClause *C) {
                     return C->getModifier() == OMPC_REDUCTION_inscan;
                   }))
    // Disable parallel access in case of prefix sum.
    LoopStack.setParallel(/*Enable=*/false);
}

void CodeGenFunction::EmitOMPSimdFinal(
    const OMPLoopDirective &D,
    const llvm::function_ref<llvm::Value *(CodeGenFunction &)> CondGen) {
  if (!HaveInsertPoint())
    return;
  llvm::BasicBlock *DoneBB = nullptr;
  auto IC = D.counters().begin();
  auto IPC = D.private_counters().begin();
  for (const Expr *F : D.finals()) {
    const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>((*IC))->getDecl());
    const auto *PrivateVD = cast<VarDecl>(cast<DeclRefExpr>((*IPC))->getDecl());
    const auto *CED = dyn_cast<OMPCapturedExprDecl>(OrigVD);
    if (LocalDeclMap.count(OrigVD) || CapturedStmtInfo->lookup(OrigVD) ||
        OrigVD->hasGlobalStorage() || CED) {
      if (!DoneBB) {
        if (llvm::Value *Cond = CondGen(*this)) {
          // If the first post-update expression is found, emit conditional
          // block if it was requested.
          llvm::BasicBlock *ThenBB = createBasicBlock(".omp.final.then");
          DoneBB = createBasicBlock(".omp.final.done");
          Builder.CreateCondBr(Cond, ThenBB, DoneBB);
          EmitBlock(ThenBB);
        }
      }
      Address OrigAddr = Address::invalid();
      if (CED) {
        OrigAddr = EmitLValue(CED->getInit()->IgnoreImpCasts()).getAddress();
      } else {
        DeclRefExpr DRE(getContext(), const_cast<VarDecl *>(PrivateVD),
                        /*RefersToEnclosingVariableOrCapture=*/false,
                        (*IPC)->getType(), VK_LValue, (*IPC)->getExprLoc());
        OrigAddr = EmitLValue(&DRE).getAddress();
      }
      OMPPrivateScope VarScope(*this);
      VarScope.addPrivate(OrigVD, OrigAddr);
      (void)VarScope.Privatize();
      EmitIgnoredExpr(F);
    }
    ++IC;
    ++IPC;
  }
  if (DoneBB)
    EmitBlock(DoneBB, /*IsFinished=*/true);
}

static void emitOMPLoopBodyWithStopPoint(CodeGenFunction &CGF,
                                         const OMPLoopDirective &S,
                                         CodeGenFunction::JumpDest LoopExit) {
  CGF.EmitOMPLoopBody(S, LoopExit);
  CGF.EmitStopPoint(&S);
}

/// Emit a helper variable and return corresponding lvalue.
static LValue EmitOMPHelperVar(CodeGenFunction &CGF,
                               const DeclRefExpr *Helper) {
  auto VDecl = cast<VarDecl>(Helper->getDecl());
  CGF.EmitVarDecl(*VDecl);
  return CGF.EmitLValue(Helper);
}

static void emitCommonSimdLoop(CodeGenFunction &CGF, const OMPLoopDirective &S,
                               const RegionCodeGenTy &SimdInitGen,
                               const RegionCodeGenTy &BodyCodeGen) {
  auto &&ThenGen = [&S, &SimdInitGen, &BodyCodeGen](CodeGenFunction &CGF,
                                                    PrePostActionTy &) {
    CGOpenMPRuntime::NontemporalDeclsRAII NontemporalsRegion(CGF.CGM, S);
    CodeGenFunction::OMPLocalDeclMapRAII Scope(CGF);
    SimdInitGen(CGF);

    BodyCodeGen(CGF);
  };
  auto &&ElseGen = [&BodyCodeGen](CodeGenFunction &CGF, PrePostActionTy &) {
    CodeGenFunction::OMPLocalDeclMapRAII Scope(CGF);
    CGF.LoopStack.setVectorizeEnable(/*Enable=*/false);

    BodyCodeGen(CGF);
  };
  const Expr *IfCond = nullptr;
  if (isOpenMPSimdDirective(S.getDirectiveKind())) {
    for (const auto *C : S.getClausesOfKind<OMPIfClause>()) {
      if (CGF.getLangOpts().OpenMP >= 50 &&
          (C->getNameModifier() == OMPD_unknown ||
           C->getNameModifier() == OMPD_simd)) {
        IfCond = C->getCondition();
        break;
      }
    }
  }
  if (IfCond) {
    CGF.CGM.getOpenMPRuntime().emitIfClause(CGF, IfCond, ThenGen, ElseGen);
  } else {
    RegionCodeGenTy ThenRCG(ThenGen);
    ThenRCG(CGF);
  }
}

static void emitOMPSimdRegion(CodeGenFunction &CGF, const OMPLoopDirective &S,
                              PrePostActionTy &Action) {
  Action.Enter(CGF);
  assert(isOpenMPSimdDirective(S.getDirectiveKind()) &&
         "Expected simd directive");
  OMPLoopScope PreInitScope(CGF, S);
  // if (PreCond) {
  //   for (IV in 0..LastIteration) BODY;
  //   <Final counter/linear vars updates>;
  // }
  //
  if (isOpenMPDistributeDirective(S.getDirectiveKind()) ||
      isOpenMPWorksharingDirective(S.getDirectiveKind()) ||
      isOpenMPTaskLoopDirective(S.getDirectiveKind())) {
    (void)EmitOMPHelperVar(CGF, cast<DeclRefExpr>(S.getLowerBoundVariable()));
    (void)EmitOMPHelperVar(CGF, cast<DeclRefExpr>(S.getUpperBoundVariable()));
  }

  // Emit: if (PreCond) - begin.
  // If the condition constant folds and can be elided, avoid emitting the
  // whole loop.
  bool CondConstant;
  llvm::BasicBlock *ContBlock = nullptr;
  if (CGF.ConstantFoldsToSimpleInteger(S.getPreCond(), CondConstant)) {
    if (!CondConstant)
      return;
  } else {
    llvm::BasicBlock *ThenBlock = CGF.createBasicBlock("simd.if.then");
    ContBlock = CGF.createBasicBlock("simd.if.end");
    emitPreCond(CGF, S, S.getPreCond(), ThenBlock, ContBlock,
                CGF.getProfileCount(&S));
    CGF.EmitBlock(ThenBlock);
    CGF.incrementProfileCounter(&S);
  }

  // Emit the loop iteration variable.
  const Expr *IVExpr = S.getIterationVariable();
  const auto *IVDecl = cast<VarDecl>(cast<DeclRefExpr>(IVExpr)->getDecl());
  CGF.EmitVarDecl(*IVDecl);
  CGF.EmitIgnoredExpr(S.getInit());

  // Emit the iterations count variable.
  // If it is not a variable, Sema decided to calculate iterations count on
  // each iteration (e.g., it is foldable into a constant).
  if (const auto *LIExpr = dyn_cast<DeclRefExpr>(S.getLastIteration())) {
    CGF.EmitVarDecl(*cast<VarDecl>(LIExpr->getDecl()));
    // Emit calculation of the iterations count.
    CGF.EmitIgnoredExpr(S.getCalcLastIteration());
  }

  emitAlignedClause(CGF, S);
  (void)CGF.EmitOMPLinearClauseInit(S);
  {
    CodeGenFunction::OMPPrivateScope LoopScope(CGF);
    CGF.EmitOMPPrivateClause(S, LoopScope);
    CGF.EmitOMPPrivateLoopCounters(S, LoopScope);
    CGF.EmitOMPLinearClause(S, LoopScope);
    CGF.EmitOMPReductionClauseInit(S, LoopScope);
    CGOpenMPRuntime::LastprivateConditionalRAII LPCRegion(
        CGF, S, CGF.EmitLValue(S.getIterationVariable()));
    bool HasLastprivateClause = CGF.EmitOMPLastprivateClauseInit(S, LoopScope);
    (void)LoopScope.Privatize();
    if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()))
      CGF.CGM.getOpenMPRuntime().adjustTargetSpecificDataForLambdas(CGF, S);

    emitCommonSimdLoop(
        CGF, S,
        [&S](CodeGenFunction &CGF, PrePostActionTy &) {
          CGF.EmitOMPSimdInit(S);
        },
        [&S, &LoopScope](CodeGenFunction &CGF, PrePostActionTy &) {
          CGF.EmitOMPInnerLoop(
              S, LoopScope.requiresCleanups(), S.getCond(), S.getInc(),
              [&S](CodeGenFunction &CGF) {
                emitOMPLoopBodyWithStopPoint(CGF, S,
                                             CodeGenFunction::JumpDest());
              },
              [](CodeGenFunction &) {});
        });
    CGF.EmitOMPSimdFinal(S, [](CodeGenFunction &) { return nullptr; });
    // Emit final copy of the lastprivate variables at the end of loops.
    if (HasLastprivateClause)
      CGF.EmitOMPLastprivateClauseFinal(S, /*NoFinals=*/true);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_simd);
    emitPostUpdateForReductionClause(CGF, S,
                                     [](CodeGenFunction &) { return nullptr; });
    LoopScope.restoreMap();
    CGF.EmitOMPLinearClauseFinal(S, [](CodeGenFunction &) { return nullptr; });
  }
  // Emit: if (PreCond) - end.
  if (ContBlock) {
    CGF.EmitBranch(ContBlock);
    CGF.EmitBlock(ContBlock, true);
  }
}

static bool isSupportedByOpenMPIRBuilder(const OMPSimdDirective &S) {
  // Check for unsupported clauses
  for (OMPClause *C : S.clauses()) {
    // Currently only order, simdlen and safelen clauses are supported
    if (!(isa<OMPSimdlenClause>(C) || isa<OMPSafelenClause>(C) ||
          isa<OMPOrderClause>(C) || isa<OMPAlignedClause>(C)))
      return false;
  }

  // Check if we have a statement with the ordered directive.
  // Visit the statement hierarchy to find a compound statement
  // with a ordered directive in it.
  if (const auto *CanonLoop = dyn_cast<OMPCanonicalLoop>(S.getRawStmt())) {
    if (const Stmt *SyntacticalLoop = CanonLoop->getLoopStmt()) {
      for (const Stmt *SubStmt : SyntacticalLoop->children()) {
        if (!SubStmt)
          continue;
        if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(SubStmt)) {
          for (const Stmt *CSSubStmt : CS->children()) {
            if (!CSSubStmt)
              continue;
            if (isa<OMPOrderedDirective>(CSSubStmt)) {
              return false;
            }
          }
        }
      }
    }
  }
  return true;
}
static llvm::MapVector<llvm::Value *, llvm::Value *>
GetAlignedMapping(const OMPSimdDirective &S, CodeGenFunction &CGF) {
  llvm::MapVector<llvm::Value *, llvm::Value *> AlignedVars;
  for (const auto *Clause : S.getClausesOfKind<OMPAlignedClause>()) {
    llvm::APInt ClauseAlignment(64, 0);
    if (const Expr *AlignmentExpr = Clause->getAlignment()) {
      auto *AlignmentCI =
          cast<llvm::ConstantInt>(CGF.EmitScalarExpr(AlignmentExpr));
      ClauseAlignment = AlignmentCI->getValue();
    }
    for (const Expr *E : Clause->varlists()) {
      llvm::APInt Alignment(ClauseAlignment);
      if (Alignment == 0) {
        // OpenMP [2.8.1, Description]
        // If no optional parameter is specified, implementation-defined default
        // alignments for SIMD instructions on the target platforms are assumed.
        Alignment =
            CGF.getContext()
                .toCharUnitsFromBits(CGF.getContext().getOpenMPDefaultSimdAlign(
                    E->getType()->getPointeeType()))
                .getQuantity();
      }
      assert((Alignment == 0 || Alignment.isPowerOf2()) &&
             "alignment is not power of 2");
      llvm::Value *PtrValue = CGF.EmitScalarExpr(E);
      AlignedVars[PtrValue] = CGF.Builder.getInt64(Alignment.getSExtValue());
    }
  }
  return AlignedVars;
}

void CodeGenFunction::EmitOMPSimdDirective(const OMPSimdDirective &S) {
  bool UseOMPIRBuilder =
      CGM.getLangOpts().OpenMPIRBuilder && isSupportedByOpenMPIRBuilder(S);
  if (UseOMPIRBuilder) {
    auto &&CodeGenIRBuilder = [this, &S, UseOMPIRBuilder](CodeGenFunction &CGF,
                                                          PrePostActionTy &) {
      // Use the OpenMPIRBuilder if enabled.
      if (UseOMPIRBuilder) {
        llvm::MapVector<llvm::Value *, llvm::Value *> AlignedVars =
            GetAlignedMapping(S, CGF);
        // Emit the associated statement and get its loop representation.
        const Stmt *Inner = S.getRawStmt();
        llvm::CanonicalLoopInfo *CLI =
            EmitOMPCollapsedCanonicalLoopNest(Inner, 1);

        llvm::OpenMPIRBuilder &OMPBuilder =
            CGM.getOpenMPRuntime().getOMPBuilder();
        // Add SIMD specific metadata
        llvm::ConstantInt *Simdlen = nullptr;
        if (const auto *C = S.getSingleClause<OMPSimdlenClause>()) {
          RValue Len =
              this->EmitAnyExpr(C->getSimdlen(), AggValueSlot::ignored(),
                                /*ignoreResult=*/true);
          auto *Val = cast<llvm::ConstantInt>(Len.getScalarVal());
          Simdlen = Val;
        }
        llvm::ConstantInt *Safelen = nullptr;
        if (const auto *C = S.getSingleClause<OMPSafelenClause>()) {
          RValue Len =
              this->EmitAnyExpr(C->getSafelen(), AggValueSlot::ignored(),
                                /*ignoreResult=*/true);
          auto *Val = cast<llvm::ConstantInt>(Len.getScalarVal());
          Safelen = Val;
        }
        llvm::omp::OrderKind Order = llvm::omp::OrderKind::OMP_ORDER_unknown;
        if (const auto *C = S.getSingleClause<OMPOrderClause>()) {
          if (C->getKind() == OpenMPOrderClauseKind ::OMPC_ORDER_concurrent) {
            Order = llvm::omp::OrderKind::OMP_ORDER_concurrent;
          }
        }
        // Add simd metadata to the collapsed loop. Do not generate
        // another loop for if clause. Support for if clause is done earlier.
        OMPBuilder.applySimd(CLI, AlignedVars,
                             /*IfCond*/ nullptr, Order, Simdlen, Safelen);
        return;
      }
    };
    {
      auto LPCRegion =
          CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
      OMPLexicalScope Scope(*this, S, OMPD_unknown);
      CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_simd,
                                                  CodeGenIRBuilder);
    }
    return;
  }

  ParentLoopDirectiveForScanRegion ScanRegion(*this, S);
  OMPFirstScanLoop = true;
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitOMPSimdRegion(CGF, S, Action);
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    OMPLexicalScope Scope(*this, S, OMPD_unknown);
    CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_simd, CodeGen);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPTileDirective(const OMPTileDirective &S) {
  // Emit the de-sugared statement.
  OMPTransformDirectiveScopeRAII TileScope(*this, &S);
  EmitStmt(S.getTransformedStmt());
}

void CodeGenFunction::EmitOMPReverseDirective(const OMPReverseDirective &S) {
  // Emit the de-sugared statement.
  OMPTransformDirectiveScopeRAII ReverseScope(*this, &S);
  EmitStmt(S.getTransformedStmt());
}

void CodeGenFunction::EmitOMPInterchangeDirective(
    const OMPInterchangeDirective &S) {
  // Emit the de-sugared statement.
  OMPTransformDirectiveScopeRAII InterchangeScope(*this, &S);
  EmitStmt(S.getTransformedStmt());
}

void CodeGenFunction::EmitOMPUnrollDirective(const OMPUnrollDirective &S) {
  bool UseOMPIRBuilder = CGM.getLangOpts().OpenMPIRBuilder;

  if (UseOMPIRBuilder) {
    auto DL = SourceLocToDebugLoc(S.getBeginLoc());
    const Stmt *Inner = S.getRawStmt();

    // Consume nested loop. Clear the entire remaining loop stack because a
    // fully unrolled loop is non-transformable. For partial unrolling the
    // generated outer loop is pushed back to the stack.
    llvm::CanonicalLoopInfo *CLI = EmitOMPCollapsedCanonicalLoopNest(Inner, 1);
    OMPLoopNestStack.clear();

    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();

    bool NeedsUnrolledCLI = ExpectedOMPLoopDepth >= 1;
    llvm::CanonicalLoopInfo *UnrolledCLI = nullptr;

    if (S.hasClausesOfKind<OMPFullClause>()) {
      assert(ExpectedOMPLoopDepth == 0);
      OMPBuilder.unrollLoopFull(DL, CLI);
    } else if (auto *PartialClause = S.getSingleClause<OMPPartialClause>()) {
      uint64_t Factor = 0;
      if (Expr *FactorExpr = PartialClause->getFactor()) {
        Factor = FactorExpr->EvaluateKnownConstInt(getContext()).getZExtValue();
        assert(Factor >= 1 && "Only positive factors are valid");
      }
      OMPBuilder.unrollLoopPartial(DL, CLI, Factor,
                                   NeedsUnrolledCLI ? &UnrolledCLI : nullptr);
    } else {
      OMPBuilder.unrollLoopHeuristic(DL, CLI);
    }

    assert((!NeedsUnrolledCLI || UnrolledCLI) &&
           "NeedsUnrolledCLI implies UnrolledCLI to be set");
    if (UnrolledCLI)
      OMPLoopNestStack.push_back(UnrolledCLI);

    return;
  }

  // This function is only called if the unrolled loop is not consumed by any
  // other loop-associated construct. Such a loop-associated construct will have
  // used the transformed AST.

  // Set the unroll metadata for the next emitted loop.
  LoopStack.setUnrollState(LoopAttributes::Enable);

  if (S.hasClausesOfKind<OMPFullClause>()) {
    LoopStack.setUnrollState(LoopAttributes::Full);
  } else if (auto *PartialClause = S.getSingleClause<OMPPartialClause>()) {
    if (Expr *FactorExpr = PartialClause->getFactor()) {
      uint64_t Factor =
          FactorExpr->EvaluateKnownConstInt(getContext()).getZExtValue();
      assert(Factor >= 1 && "Only positive factors are valid");
      LoopStack.setUnrollCount(Factor);
    }
  }

  EmitStmt(S.getAssociatedStmt());
}

void CodeGenFunction::EmitOMPOuterLoop(
    bool DynamicOrOrdered, bool IsMonotonic, const OMPLoopDirective &S,
    CodeGenFunction::OMPPrivateScope &LoopScope,
    const CodeGenFunction::OMPLoopArguments &LoopArgs,
    const CodeGenFunction::CodeGenLoopTy &CodeGenLoop,
    const CodeGenFunction::CodeGenOrderedTy &CodeGenOrdered) {
  CGOpenMPRuntime &RT = CGM.getOpenMPRuntime();

  const Expr *IVExpr = S.getIterationVariable();
  const unsigned IVSize = getContext().getTypeSize(IVExpr->getType());
  const bool IVSigned = IVExpr->getType()->hasSignedIntegerRepresentation();

  JumpDest LoopExit = getJumpDestInCurrentScope("omp.dispatch.end");

  // Start the loop with a block that tests the condition.
  llvm::BasicBlock *CondBlock = createBasicBlock("omp.dispatch.cond");
  EmitBlock(CondBlock);
  const SourceRange R = S.getSourceRange();
  OMPLoopNestStack.clear();
  LoopStack.push(CondBlock, SourceLocToDebugLoc(R.getBegin()),
                 SourceLocToDebugLoc(R.getEnd()));

  llvm::Value *BoolCondVal = nullptr;
  if (!DynamicOrOrdered) {
    // UB = min(UB, GlobalUB) or
    // UB = min(UB, PrevUB) for combined loop sharing constructs (e.g.
    // 'distribute parallel for')
    EmitIgnoredExpr(LoopArgs.EUB);
    // IV = LB
    EmitIgnoredExpr(LoopArgs.Init);
    // IV < UB
    BoolCondVal = EvaluateExprAsBool(LoopArgs.Cond);
  } else {
    BoolCondVal =
        RT.emitForNext(*this, S.getBeginLoc(), IVSize, IVSigned, LoopArgs.IL,
                       LoopArgs.LB, LoopArgs.UB, LoopArgs.ST);
  }

  // If there are any cleanups between here and the loop-exit scope,
  // create a block to stage a loop exit along.
  llvm::BasicBlock *ExitBlock = LoopExit.getBlock();
  if (LoopScope.requiresCleanups())
    ExitBlock = createBasicBlock("omp.dispatch.cleanup");

  llvm::BasicBlock *LoopBody = createBasicBlock("omp.dispatch.body");
  Builder.CreateCondBr(BoolCondVal, LoopBody, ExitBlock);
  if (ExitBlock != LoopExit.getBlock()) {
    EmitBlock(ExitBlock);
    EmitBranchThroughCleanup(LoopExit);
  }
  EmitBlock(LoopBody);

  // Emit "IV = LB" (in case of static schedule, we have already calculated new
  // LB for loop condition and emitted it above).
  if (DynamicOrOrdered)
    EmitIgnoredExpr(LoopArgs.Init);

  // Create a block for the increment.
  JumpDest Continue = getJumpDestInCurrentScope("omp.dispatch.inc");
  BreakContinueStack.push_back(BreakContinue(LoopExit, Continue));

  emitCommonSimdLoop(
      *this, S,
      [&S, IsMonotonic](CodeGenFunction &CGF, PrePostActionTy &) {
        // Generate !llvm.loop.parallel metadata for loads and stores for loops
        // with dynamic/guided scheduling and without ordered clause.
        if (!isOpenMPSimdDirective(S.getDirectiveKind())) {
          CGF.LoopStack.setParallel(!IsMonotonic);
          if (const auto *C = S.getSingleClause<OMPOrderClause>())
            if (C->getKind() == OMPC_ORDER_concurrent)
              CGF.LoopStack.setParallel(/*Enable=*/true);
        } else {
          CGF.EmitOMPSimdInit(S);
        }
      },
      [&S, &LoopArgs, LoopExit, &CodeGenLoop, IVSize, IVSigned, &CodeGenOrdered,
       &LoopScope](CodeGenFunction &CGF, PrePostActionTy &) {
        SourceLocation Loc = S.getBeginLoc();
        // when 'distribute' is not combined with a 'for':
        // while (idx <= UB) { BODY; ++idx; }
        // when 'distribute' is combined with a 'for'
        // (e.g. 'distribute parallel for')
        // while (idx <= UB) { <CodeGen rest of pragma>; idx += ST; }
        CGF.EmitOMPInnerLoop(
            S, LoopScope.requiresCleanups(), LoopArgs.Cond, LoopArgs.IncExpr,
            [&S, LoopExit, &CodeGenLoop](CodeGenFunction &CGF) {
              CodeGenLoop(CGF, S, LoopExit);
            },
            [IVSize, IVSigned, Loc, &CodeGenOrdered](CodeGenFunction &CGF) {
              CodeGenOrdered(CGF, Loc, IVSize, IVSigned);
            });
      });

  EmitBlock(Continue.getBlock());
  BreakContinueStack.pop_back();
  if (!DynamicOrOrdered) {
    // Emit "LB = LB + Stride", "UB = UB + Stride".
    EmitIgnoredExpr(LoopArgs.NextLB);
    EmitIgnoredExpr(LoopArgs.NextUB);
  }

  EmitBranch(CondBlock);
  OMPLoopNestStack.clear();
  LoopStack.pop();
  // Emit the fall-through block.
  EmitBlock(LoopExit.getBlock());

  // Tell the runtime we are done.
  auto &&CodeGen = [DynamicOrOrdered, &S, &LoopArgs](CodeGenFunction &CGF) {
    if (!DynamicOrOrdered)
      CGF.CGM.getOpenMPRuntime().emitForStaticFinish(CGF, S.getEndLoc(),
                                                     LoopArgs.DKind);
  };
  OMPCancelStack.emitExit(*this, S.getDirectiveKind(), CodeGen);
}

void CodeGenFunction::EmitOMPForOuterLoop(
    const OpenMPScheduleTy &ScheduleKind, bool IsMonotonic,
    const OMPLoopDirective &S, OMPPrivateScope &LoopScope, bool Ordered,
    const OMPLoopArguments &LoopArgs,
    const CodeGenDispatchBoundsTy &CGDispatchBounds) {
  CGOpenMPRuntime &RT = CGM.getOpenMPRuntime();

  // Dynamic scheduling of the outer loop (dynamic, guided, auto, runtime).
  const bool DynamicOrOrdered = Ordered || RT.isDynamic(ScheduleKind.Schedule);

  assert((Ordered || !RT.isStaticNonchunked(ScheduleKind.Schedule,
                                            LoopArgs.Chunk != nullptr)) &&
         "static non-chunked schedule does not need outer loop");

  // Emit outer loop.
  //
  // OpenMP [2.7.1, Loop Construct, Description, table 2-1]
  // When schedule(dynamic,chunk_size) is specified, the iterations are
  // distributed to threads in the team in chunks as the threads request them.
  // Each thread executes a chunk of iterations, then requests another chunk,
  // until no chunks remain to be distributed. Each chunk contains chunk_size
  // iterations, except for the last chunk to be distributed, which may have
  // fewer iterations. When no chunk_size is specified, it defaults to 1.
  //
  // When schedule(guided,chunk_size) is specified, the iterations are assigned
  // to threads in the team in chunks as the executing threads request them.
  // Each thread executes a chunk of iterations, then requests another chunk,
  // until no chunks remain to be assigned. For a chunk_size of 1, the size of
  // each chunk is proportional to the number of unassigned iterations divided
  // by the number of threads in the team, decreasing to 1. For a chunk_size
  // with value k (greater than 1), the size of each chunk is determined in the
  // same way, with the restriction that the chunks do not contain fewer than k
  // iterations (except for the last chunk to be assigned, which may have fewer
  // than k iterations).
  //
  // When schedule(auto) is specified, the decision regarding scheduling is
  // delegated to the compiler and/or runtime system. The programmer gives the
  // implementation the freedom to choose any possible mapping of iterations to
  // threads in the team.
  //
  // When schedule(runtime) is specified, the decision regarding scheduling is
  // deferred until run time, and the schedule and chunk size are taken from the
  // run-sched-var ICV. If the ICV is set to auto, the schedule is
  // implementation defined
  //
  // __kmpc_dispatch_init();
  // while(__kmpc_dispatch_next(&LB, &UB)) {
  //   idx = LB;
  //   while (idx <= UB) { BODY; ++idx;
  //   __kmpc_dispatch_fini_(4|8)[u](); // For ordered loops only.
  //   } // inner loop
  // }
  // __kmpc_dispatch_deinit();
  //
  // OpenMP [2.7.1, Loop Construct, Description, table 2-1]
  // When schedule(static, chunk_size) is specified, iterations are divided into
  // chunks of size chunk_size, and the chunks are assigned to the threads in
  // the team in a round-robin fashion in the order of the thread number.
  //
  // while(UB = min(UB, GlobalUB), idx = LB, idx < UB) {
  //   while (idx <= UB) { BODY; ++idx; } // inner loop
  //   LB = LB + ST;
  //   UB = UB + ST;
  // }
  //

  const Expr *IVExpr = S.getIterationVariable();
  const unsigned IVSize = getContext().getTypeSize(IVExpr->getType());
  const bool IVSigned = IVExpr->getType()->hasSignedIntegerRepresentation();

  if (DynamicOrOrdered) {
    const std::pair<llvm::Value *, llvm::Value *> DispatchBounds =
        CGDispatchBounds(*this, S, LoopArgs.LB, LoopArgs.UB);
    llvm::Value *LBVal = DispatchBounds.first;
    llvm::Value *UBVal = DispatchBounds.second;
    CGOpenMPRuntime::DispatchRTInput DipatchRTInputValues = {LBVal, UBVal,
                                                             LoopArgs.Chunk};
    RT.emitForDispatchInit(*this, S.getBeginLoc(), ScheduleKind, IVSize,
                           IVSigned, Ordered, DipatchRTInputValues);
  } else {
    CGOpenMPRuntime::StaticRTInput StaticInit(
        IVSize, IVSigned, Ordered, LoopArgs.IL, LoopArgs.LB, LoopArgs.UB,
        LoopArgs.ST, LoopArgs.Chunk);
    RT.emitForStaticInit(*this, S.getBeginLoc(), S.getDirectiveKind(),
                         ScheduleKind, StaticInit);
  }

  auto &&CodeGenOrdered = [Ordered](CodeGenFunction &CGF, SourceLocation Loc,
                                    const unsigned IVSize,
                                    const bool IVSigned) {
    if (Ordered) {
      CGF.CGM.getOpenMPRuntime().emitForOrderedIterationEnd(CGF, Loc, IVSize,
                                                            IVSigned);
    }
  };

  OMPLoopArguments OuterLoopArgs(LoopArgs.LB, LoopArgs.UB, LoopArgs.ST,
                                 LoopArgs.IL, LoopArgs.Chunk, LoopArgs.EUB);
  OuterLoopArgs.IncExpr = S.getInc();
  OuterLoopArgs.Init = S.getInit();
  OuterLoopArgs.Cond = S.getCond();
  OuterLoopArgs.NextLB = S.getNextLowerBound();
  OuterLoopArgs.NextUB = S.getNextUpperBound();
  OuterLoopArgs.DKind = LoopArgs.DKind;
  EmitOMPOuterLoop(DynamicOrOrdered, IsMonotonic, S, LoopScope, OuterLoopArgs,
                   emitOMPLoopBodyWithStopPoint, CodeGenOrdered);
  if (DynamicOrOrdered) {
    RT.emitForDispatchDeinit(*this, S.getBeginLoc());
  }
}

static void emitEmptyOrdered(CodeGenFunction &, SourceLocation Loc,
                             const unsigned IVSize, const bool IVSigned) {}

void CodeGenFunction::EmitOMPDistributeOuterLoop(
    OpenMPDistScheduleClauseKind ScheduleKind, const OMPLoopDirective &S,
    OMPPrivateScope &LoopScope, const OMPLoopArguments &LoopArgs,
    const CodeGenLoopTy &CodeGenLoopContent) {

  CGOpenMPRuntime &RT = CGM.getOpenMPRuntime();

  // Emit outer loop.
  // Same behavior as a OMPForOuterLoop, except that schedule cannot be
  // dynamic
  //

  const Expr *IVExpr = S.getIterationVariable();
  const unsigned IVSize = getContext().getTypeSize(IVExpr->getType());
  const bool IVSigned = IVExpr->getType()->hasSignedIntegerRepresentation();

  CGOpenMPRuntime::StaticRTInput StaticInit(
      IVSize, IVSigned, /* Ordered = */ false, LoopArgs.IL, LoopArgs.LB,
      LoopArgs.UB, LoopArgs.ST, LoopArgs.Chunk);
  RT.emitDistributeStaticInit(*this, S.getBeginLoc(), ScheduleKind, StaticInit);

  // for combined 'distribute' and 'for' the increment expression of distribute
  // is stored in DistInc. For 'distribute' alone, it is in Inc.
  Expr *IncExpr;
  if (isOpenMPLoopBoundSharingDirective(S.getDirectiveKind()))
    IncExpr = S.getDistInc();
  else
    IncExpr = S.getInc();

  // this routine is shared by 'omp distribute parallel for' and
  // 'omp distribute': select the right EUB expression depending on the
  // directive
  OMPLoopArguments OuterLoopArgs;
  OuterLoopArgs.LB = LoopArgs.LB;
  OuterLoopArgs.UB = LoopArgs.UB;
  OuterLoopArgs.ST = LoopArgs.ST;
  OuterLoopArgs.IL = LoopArgs.IL;
  OuterLoopArgs.Chunk = LoopArgs.Chunk;
  OuterLoopArgs.EUB = isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                          ? S.getCombinedEnsureUpperBound()
                          : S.getEnsureUpperBound();
  OuterLoopArgs.IncExpr = IncExpr;
  OuterLoopArgs.Init = isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                           ? S.getCombinedInit()
                           : S.getInit();
  OuterLoopArgs.Cond = isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                           ? S.getCombinedCond()
                           : S.getCond();
  OuterLoopArgs.NextLB = isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                             ? S.getCombinedNextLowerBound()
                             : S.getNextLowerBound();
  OuterLoopArgs.NextUB = isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                             ? S.getCombinedNextUpperBound()
                             : S.getNextUpperBound();
  OuterLoopArgs.DKind = OMPD_distribute;

  EmitOMPOuterLoop(/* DynamicOrOrdered = */ false, /* IsMonotonic = */ false, S,
                   LoopScope, OuterLoopArgs, CodeGenLoopContent,
                   emitEmptyOrdered);
}

static std::pair<LValue, LValue>
emitDistributeParallelForInnerBounds(CodeGenFunction &CGF,
                                     const OMPExecutableDirective &S) {
  const OMPLoopDirective &LS = cast<OMPLoopDirective>(S);
  LValue LB =
      EmitOMPHelperVar(CGF, cast<DeclRefExpr>(LS.getLowerBoundVariable()));
  LValue UB =
      EmitOMPHelperVar(CGF, cast<DeclRefExpr>(LS.getUpperBoundVariable()));

  // When composing 'distribute' with 'for' (e.g. as in 'distribute
  // parallel for') we need to use the 'distribute'
  // chunk lower and upper bounds rather than the whole loop iteration
  // space. These are parameters to the outlined function for 'parallel'
  // and we copy the bounds of the previous schedule into the
  // the current ones.
  LValue PrevLB = CGF.EmitLValue(LS.getPrevLowerBoundVariable());
  LValue PrevUB = CGF.EmitLValue(LS.getPrevUpperBoundVariable());
  llvm::Value *PrevLBVal = CGF.EmitLoadOfScalar(
      PrevLB, LS.getPrevLowerBoundVariable()->getExprLoc());
  PrevLBVal = CGF.EmitScalarConversion(
      PrevLBVal, LS.getPrevLowerBoundVariable()->getType(),
      LS.getIterationVariable()->getType(),
      LS.getPrevLowerBoundVariable()->getExprLoc());
  llvm::Value *PrevUBVal = CGF.EmitLoadOfScalar(
      PrevUB, LS.getPrevUpperBoundVariable()->getExprLoc());
  PrevUBVal = CGF.EmitScalarConversion(
      PrevUBVal, LS.getPrevUpperBoundVariable()->getType(),
      LS.getIterationVariable()->getType(),
      LS.getPrevUpperBoundVariable()->getExprLoc());

  CGF.EmitStoreOfScalar(PrevLBVal, LB);
  CGF.EmitStoreOfScalar(PrevUBVal, UB);

  return {LB, UB};
}

/// if the 'for' loop has a dispatch schedule (e.g. dynamic, guided) then
/// we need to use the LB and UB expressions generated by the worksharing
/// code generation support, whereas in non combined situations we would
/// just emit 0 and the LastIteration expression
/// This function is necessary due to the difference of the LB and UB
/// types for the RT emission routines for 'for_static_init' and
/// 'for_dispatch_init'
static std::pair<llvm::Value *, llvm::Value *>
emitDistributeParallelForDispatchBounds(CodeGenFunction &CGF,
                                        const OMPExecutableDirective &S,
                                        Address LB, Address UB) {
  const OMPLoopDirective &LS = cast<OMPLoopDirective>(S);
  const Expr *IVExpr = LS.getIterationVariable();
  // when implementing a dynamic schedule for a 'for' combined with a
  // 'distribute' (e.g. 'distribute parallel for'), the 'for' loop
  // is not normalized as each team only executes its own assigned
  // distribute chunk
  QualType IteratorTy = IVExpr->getType();
  llvm::Value *LBVal =
      CGF.EmitLoadOfScalar(LB, /*Volatile=*/false, IteratorTy, S.getBeginLoc());
  llvm::Value *UBVal =
      CGF.EmitLoadOfScalar(UB, /*Volatile=*/false, IteratorTy, S.getBeginLoc());
  return {LBVal, UBVal};
}

static void emitDistributeParallelForDistributeInnerBoundParams(
    CodeGenFunction &CGF, const OMPExecutableDirective &S,
    llvm::SmallVectorImpl<llvm::Value *> &CapturedVars) {
  const auto &Dir = cast<OMPLoopDirective>(S);
  LValue LB =
      CGF.EmitLValue(cast<DeclRefExpr>(Dir.getCombinedLowerBoundVariable()));
  llvm::Value *LBCast = CGF.Builder.CreateIntCast(
      CGF.Builder.CreateLoad(LB.getAddress()), CGF.SizeTy, /*isSigned=*/false);
  CapturedVars.push_back(LBCast);
  LValue UB =
      CGF.EmitLValue(cast<DeclRefExpr>(Dir.getCombinedUpperBoundVariable()));

  llvm::Value *UBCast = CGF.Builder.CreateIntCast(
      CGF.Builder.CreateLoad(UB.getAddress()), CGF.SizeTy, /*isSigned=*/false);
  CapturedVars.push_back(UBCast);
}

static void
emitInnerParallelForWhenCombined(CodeGenFunction &CGF,
                                 const OMPLoopDirective &S,
                                 CodeGenFunction::JumpDest LoopExit) {
  auto &&CGInlinedWorksharingLoop = [&S](CodeGenFunction &CGF,
                                         PrePostActionTy &Action) {
    Action.Enter(CGF);
    bool HasCancel = false;
    if (!isOpenMPSimdDirective(S.getDirectiveKind())) {
      if (const auto *D = dyn_cast<OMPTeamsDistributeParallelForDirective>(&S))
        HasCancel = D->hasCancel();
      else if (const auto *D = dyn_cast<OMPDistributeParallelForDirective>(&S))
        HasCancel = D->hasCancel();
      else if (const auto *D =
                   dyn_cast<OMPTargetTeamsDistributeParallelForDirective>(&S))
        HasCancel = D->hasCancel();
    }
    CodeGenFunction::OMPCancelStackRAII CancelRegion(CGF, S.getDirectiveKind(),
                                                     HasCancel);
    CGF.EmitOMPWorksharingLoop(S, S.getPrevEnsureUpperBound(),
                               emitDistributeParallelForInnerBounds,
                               emitDistributeParallelForDispatchBounds);
  };

  emitCommonOMPParallelDirective(
      CGF, S,
      isOpenMPSimdDirective(S.getDirectiveKind()) ? OMPD_for_simd : OMPD_for,
      CGInlinedWorksharingLoop,
      emitDistributeParallelForDistributeInnerBoundParams);
}

void CodeGenFunction::EmitOMPDistributeParallelForDirective(
    const OMPDistributeParallelForDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitInnerParallelForWhenCombined,
                              S.getDistInc());
  };
  OMPLexicalScope Scope(*this, S, OMPD_parallel);
  CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_distribute, CodeGen);
}

void CodeGenFunction::EmitOMPDistributeParallelForSimdDirective(
    const OMPDistributeParallelForSimdDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitInnerParallelForWhenCombined,
                              S.getDistInc());
  };
  OMPLexicalScope Scope(*this, S, OMPD_parallel);
  CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_distribute, CodeGen);
}

void CodeGenFunction::EmitOMPDistributeSimdDirective(
    const OMPDistributeSimdDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };
  OMPLexicalScope Scope(*this, S, OMPD_unknown);
  CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_simd, CodeGen);
}

void CodeGenFunction::EmitOMPTargetSimdDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName, const OMPTargetSimdDirective &S) {
  // Emit SPMD target parallel for region as a standalone region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitOMPSimdRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetSimdDirective(
    const OMPTargetSimdDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitOMPSimdRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

namespace {
struct ScheduleKindModifiersTy {
  OpenMPScheduleClauseKind Kind;
  OpenMPScheduleClauseModifier M1;
  OpenMPScheduleClauseModifier M2;
  ScheduleKindModifiersTy(OpenMPScheduleClauseKind Kind,
                          OpenMPScheduleClauseModifier M1,
                          OpenMPScheduleClauseModifier M2)
      : Kind(Kind), M1(M1), M2(M2) {}
};
} // namespace

bool CodeGenFunction::EmitOMPWorksharingLoop(
    const OMPLoopDirective &S, Expr *EUB,
    const CodeGenLoopBoundsTy &CodeGenLoopBounds,
    const CodeGenDispatchBoundsTy &CGDispatchBounds) {
  // Emit the loop iteration variable.
  const auto *IVExpr = cast<DeclRefExpr>(S.getIterationVariable());
  const auto *IVDecl = cast<VarDecl>(IVExpr->getDecl());
  EmitVarDecl(*IVDecl);

  // Emit the iterations count variable.
  // If it is not a variable, Sema decided to calculate iterations count on each
  // iteration (e.g., it is foldable into a constant).
  if (const auto *LIExpr = dyn_cast<DeclRefExpr>(S.getLastIteration())) {
    EmitVarDecl(*cast<VarDecl>(LIExpr->getDecl()));
    // Emit calculation of the iterations count.
    EmitIgnoredExpr(S.getCalcLastIteration());
  }

  CGOpenMPRuntime &RT = CGM.getOpenMPRuntime();

  bool HasLastprivateClause;
  // Check pre-condition.
  {
    OMPLoopScope PreInitScope(*this, S);
    // Skip the entire loop if we don't meet the precondition.
    // If the condition constant folds and can be elided, avoid emitting the
    // whole loop.
    bool CondConstant;
    llvm::BasicBlock *ContBlock = nullptr;
    if (ConstantFoldsToSimpleInteger(S.getPreCond(), CondConstant)) {
      if (!CondConstant)
        return false;
    } else {
      llvm::BasicBlock *ThenBlock = createBasicBlock("omp.precond.then");
      ContBlock = createBasicBlock("omp.precond.end");
      emitPreCond(*this, S, S.getPreCond(), ThenBlock, ContBlock,
                  getProfileCount(&S));
      EmitBlock(ThenBlock);
      incrementProfileCounter(&S);
    }

    RunCleanupsScope DoacrossCleanupScope(*this);
    bool Ordered = false;
    if (const auto *OrderedClause = S.getSingleClause<OMPOrderedClause>()) {
      if (OrderedClause->getNumForLoops())
        RT.emitDoacrossInit(*this, S, OrderedClause->getLoopNumIterations());
      else
        Ordered = true;
    }

    llvm::DenseSet<const Expr *> EmittedFinals;
    emitAlignedClause(*this, S);
    bool HasLinears = EmitOMPLinearClauseInit(S);
    // Emit helper vars inits.

    std::pair<LValue, LValue> Bounds = CodeGenLoopBounds(*this, S);
    LValue LB = Bounds.first;
    LValue UB = Bounds.second;
    LValue ST =
        EmitOMPHelperVar(*this, cast<DeclRefExpr>(S.getStrideVariable()));
    LValue IL =
        EmitOMPHelperVar(*this, cast<DeclRefExpr>(S.getIsLastIterVariable()));

    // Emit 'then' code.
    {
      OMPPrivateScope LoopScope(*this);
      if (EmitOMPFirstprivateClause(S, LoopScope) || HasLinears) {
        // Emit implicit barrier to synchronize threads and avoid data races on
        // initialization of firstprivate variables and post-update of
        // lastprivate variables.
        CGM.getOpenMPRuntime().emitBarrierCall(
            *this, S.getBeginLoc(), OMPD_unknown, /*EmitChecks=*/false,
            /*ForceSimpleCall=*/true);
      }
      EmitOMPPrivateClause(S, LoopScope);
      CGOpenMPRuntime::LastprivateConditionalRAII LPCRegion(
          *this, S, EmitLValue(S.getIterationVariable()));
      HasLastprivateClause = EmitOMPLastprivateClauseInit(S, LoopScope);
      EmitOMPReductionClauseInit(S, LoopScope);
      EmitOMPPrivateLoopCounters(S, LoopScope);
      EmitOMPLinearClause(S, LoopScope);
      (void)LoopScope.Privatize();
      if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()))
        CGM.getOpenMPRuntime().adjustTargetSpecificDataForLambdas(*this, S);

      // Detect the loop schedule kind and chunk.
      const Expr *ChunkExpr = nullptr;
      OpenMPScheduleTy ScheduleKind;
      if (const auto *C = S.getSingleClause<OMPScheduleClause>()) {
        ScheduleKind.Schedule = C->getScheduleKind();
        ScheduleKind.M1 = C->getFirstScheduleModifier();
        ScheduleKind.M2 = C->getSecondScheduleModifier();
        ChunkExpr = C->getChunkSize();
      } else {
        // Default behaviour for schedule clause.
        CGM.getOpenMPRuntime().getDefaultScheduleAndChunk(
            *this, S, ScheduleKind.Schedule, ChunkExpr);
      }
      bool HasChunkSizeOne = false;
      llvm::Value *Chunk = nullptr;
      if (ChunkExpr) {
        Chunk = EmitScalarExpr(ChunkExpr);
        Chunk = EmitScalarConversion(Chunk, ChunkExpr->getType(),
                                     S.getIterationVariable()->getType(),
                                     S.getBeginLoc());
        Expr::EvalResult Result;
        if (ChunkExpr->EvaluateAsInt(Result, getContext())) {
          llvm::APSInt EvaluatedChunk = Result.Val.getInt();
          HasChunkSizeOne = (EvaluatedChunk.getLimitedValue() == 1);
        }
      }
      const unsigned IVSize = getContext().getTypeSize(IVExpr->getType());
      const bool IVSigned = IVExpr->getType()->hasSignedIntegerRepresentation();
      // OpenMP 4.5, 2.7.1 Loop Construct, Description.
      // If the static schedule kind is specified or if the ordered clause is
      // specified, and if no monotonic modifier is specified, the effect will
      // be as if the monotonic modifier was specified.
      bool StaticChunkedOne =
          RT.isStaticChunked(ScheduleKind.Schedule,
                             /* Chunked */ Chunk != nullptr) &&
          HasChunkSizeOne &&
          isOpenMPLoopBoundSharingDirective(S.getDirectiveKind());
      bool IsMonotonic =
          Ordered ||
          (ScheduleKind.Schedule == OMPC_SCHEDULE_static &&
           !(ScheduleKind.M1 == OMPC_SCHEDULE_MODIFIER_nonmonotonic ||
             ScheduleKind.M2 == OMPC_SCHEDULE_MODIFIER_nonmonotonic)) ||
          ScheduleKind.M1 == OMPC_SCHEDULE_MODIFIER_monotonic ||
          ScheduleKind.M2 == OMPC_SCHEDULE_MODIFIER_monotonic;
      if ((RT.isStaticNonchunked(ScheduleKind.Schedule,
                                 /* Chunked */ Chunk != nullptr) ||
           StaticChunkedOne) &&
          !Ordered) {
        JumpDest LoopExit =
            getJumpDestInCurrentScope(createBasicBlock("omp.loop.exit"));
        emitCommonSimdLoop(
            *this, S,
            [&S](CodeGenFunction &CGF, PrePostActionTy &) {
              if (isOpenMPSimdDirective(S.getDirectiveKind())) {
                CGF.EmitOMPSimdInit(S);
              } else if (const auto *C = S.getSingleClause<OMPOrderClause>()) {
                if (C->getKind() == OMPC_ORDER_concurrent)
                  CGF.LoopStack.setParallel(/*Enable=*/true);
              }
            },
            [IVSize, IVSigned, Ordered, IL, LB, UB, ST, StaticChunkedOne, Chunk,
             &S, ScheduleKind, LoopExit,
             &LoopScope](CodeGenFunction &CGF, PrePostActionTy &) {
              // OpenMP [2.7.1, Loop Construct, Description, table 2-1]
              // When no chunk_size is specified, the iteration space is divided
              // into chunks that are approximately equal in size, and at most
              // one chunk is distributed to each thread. Note that the size of
              // the chunks is unspecified in this case.
              CGOpenMPRuntime::StaticRTInput StaticInit(
                  IVSize, IVSigned, Ordered, IL.getAddress(), LB.getAddress(),
                  UB.getAddress(), ST.getAddress(),
                  StaticChunkedOne ? Chunk : nullptr);
              CGF.CGM.getOpenMPRuntime().emitForStaticInit(
                  CGF, S.getBeginLoc(), S.getDirectiveKind(), ScheduleKind,
                  StaticInit);
              // UB = min(UB, GlobalUB);
              if (!StaticChunkedOne)
                CGF.EmitIgnoredExpr(S.getEnsureUpperBound());
              // IV = LB;
              CGF.EmitIgnoredExpr(S.getInit());
              // For unchunked static schedule generate:
              //
              // while (idx <= UB) {
              //   BODY;
              //   ++idx;
              // }
              //
              // For static schedule with chunk one:
              //
              // while (IV <= PrevUB) {
              //   BODY;
              //   IV += ST;
              // }
              CGF.EmitOMPInnerLoop(
                  S, LoopScope.requiresCleanups(),
                  StaticChunkedOne ? S.getCombinedParForInDistCond()
                                   : S.getCond(),
                  StaticChunkedOne ? S.getDistInc() : S.getInc(),
                  [&S, LoopExit](CodeGenFunction &CGF) {
                    emitOMPLoopBodyWithStopPoint(CGF, S, LoopExit);
                  },
                  [](CodeGenFunction &) {});
            });
        EmitBlock(LoopExit.getBlock());
        // Tell the runtime we are done.
        auto &&CodeGen = [&S](CodeGenFunction &CGF) {
          CGF.CGM.getOpenMPRuntime().emitForStaticFinish(CGF, S.getEndLoc(),
                                                         OMPD_for);
        };
        OMPCancelStack.emitExit(*this, S.getDirectiveKind(), CodeGen);
      } else {
        // Emit the outer loop, which requests its work chunk [LB..UB] from
        // runtime and runs the inner loop to process it.
        OMPLoopArguments LoopArguments(LB.getAddress(), UB.getAddress(),
                                       ST.getAddress(), IL.getAddress(), Chunk,
                                       EUB);
        LoopArguments.DKind = OMPD_for;
        EmitOMPForOuterLoop(ScheduleKind, IsMonotonic, S, LoopScope, Ordered,
                            LoopArguments, CGDispatchBounds);
      }
      if (isOpenMPSimdDirective(S.getDirectiveKind())) {
        EmitOMPSimdFinal(S, [IL, &S](CodeGenFunction &CGF) {
          return CGF.Builder.CreateIsNotNull(
              CGF.EmitLoadOfScalar(IL, S.getBeginLoc()));
        });
      }
      EmitOMPReductionClauseFinal(
          S, /*ReductionKind=*/isOpenMPSimdDirective(S.getDirectiveKind())
                 ? /*Parallel and Simd*/ OMPD_parallel_for_simd
                 : /*Parallel only*/ OMPD_parallel);
      // Emit post-update of the reduction variables if IsLastIter != 0.
      emitPostUpdateForReductionClause(
          *this, S, [IL, &S](CodeGenFunction &CGF) {
            return CGF.Builder.CreateIsNotNull(
                CGF.EmitLoadOfScalar(IL, S.getBeginLoc()));
          });
      // Emit final copy of the lastprivate variables if IsLastIter != 0.
      if (HasLastprivateClause)
        EmitOMPLastprivateClauseFinal(
            S, isOpenMPSimdDirective(S.getDirectiveKind()),
            Builder.CreateIsNotNull(EmitLoadOfScalar(IL, S.getBeginLoc())));
      LoopScope.restoreMap();
      EmitOMPLinearClauseFinal(S, [IL, &S](CodeGenFunction &CGF) {
        return CGF.Builder.CreateIsNotNull(
            CGF.EmitLoadOfScalar(IL, S.getBeginLoc()));
      });
    }
    DoacrossCleanupScope.ForceCleanup();
    // We're now done with the loop, so jump to the continuation block.
    if (ContBlock) {
      EmitBranch(ContBlock);
      EmitBlock(ContBlock, /*IsFinished=*/true);
    }
  }
  return HasLastprivateClause;
}

/// The following two functions generate expressions for the loop lower
/// and upper bounds in case of static and dynamic (dispatch) schedule
/// of the associated 'for' or 'distribute' loop.
static std::pair<LValue, LValue>
emitForLoopBounds(CodeGenFunction &CGF, const OMPExecutableDirective &S) {
  const auto &LS = cast<OMPLoopDirective>(S);
  LValue LB =
      EmitOMPHelperVar(CGF, cast<DeclRefExpr>(LS.getLowerBoundVariable()));
  LValue UB =
      EmitOMPHelperVar(CGF, cast<DeclRefExpr>(LS.getUpperBoundVariable()));
  return {LB, UB};
}

/// When dealing with dispatch schedules (e.g. dynamic, guided) we do not
/// consider the lower and upper bound expressions generated by the
/// worksharing loop support, but we use 0 and the iteration space size as
/// constants
static std::pair<llvm::Value *, llvm::Value *>
emitDispatchForLoopBounds(CodeGenFunction &CGF, const OMPExecutableDirective &S,
                          Address LB, Address UB) {
  const auto &LS = cast<OMPLoopDirective>(S);
  const Expr *IVExpr = LS.getIterationVariable();
  const unsigned IVSize = CGF.getContext().getTypeSize(IVExpr->getType());
  llvm::Value *LBVal = CGF.Builder.getIntN(IVSize, 0);
  llvm::Value *UBVal = CGF.EmitScalarExpr(LS.getLastIteration());
  return {LBVal, UBVal};
}

/// Emits internal temp array declarations for the directive with inscan
/// reductions.
/// The code is the following:
/// \code
/// size num_iters = <num_iters>;
/// <type> buffer[num_iters];
/// \endcode
static void emitScanBasedDirectiveDecls(
    CodeGenFunction &CGF, const OMPLoopDirective &S,
    llvm::function_ref<llvm::Value *(CodeGenFunction &)> NumIteratorsGen) {
  llvm::Value *OMPScanNumIterations = CGF.Builder.CreateIntCast(
      NumIteratorsGen(CGF), CGF.SizeTy, /*isSigned=*/false);
  SmallVector<const Expr *, 4> Shareds;
  SmallVector<const Expr *, 4> Privates;
  SmallVector<const Expr *, 4> ReductionOps;
  SmallVector<const Expr *, 4> CopyArrayTemps;
  for (const auto *C : S.getClausesOfKind<OMPReductionClause>()) {
    assert(C->getModifier() == OMPC_REDUCTION_inscan &&
           "Only inscan reductions are expected.");
    Shareds.append(C->varlist_begin(), C->varlist_end());
    Privates.append(C->privates().begin(), C->privates().end());
    ReductionOps.append(C->reduction_ops().begin(), C->reduction_ops().end());
    CopyArrayTemps.append(C->copy_array_temps().begin(),
                          C->copy_array_temps().end());
  }
  {
    // Emit buffers for each reduction variables.
    // ReductionCodeGen is required to emit correctly the code for array
    // reductions.
    ReductionCodeGen RedCG(Shareds, Shareds, Privates, ReductionOps);
    unsigned Count = 0;
    auto *ITA = CopyArrayTemps.begin();
    for (const Expr *IRef : Privates) {
      const auto *PrivateVD = cast<VarDecl>(cast<DeclRefExpr>(IRef)->getDecl());
      // Emit variably modified arrays, used for arrays/array sections
      // reductions.
      if (PrivateVD->getType()->isVariablyModifiedType()) {
        RedCG.emitSharedOrigLValue(CGF, Count);
        RedCG.emitAggregateType(CGF, Count);
      }
      CodeGenFunction::OpaqueValueMapping DimMapping(
          CGF,
          cast<OpaqueValueExpr>(
              cast<VariableArrayType>((*ITA)->getType()->getAsArrayTypeUnsafe())
                  ->getSizeExpr()),
          RValue::get(OMPScanNumIterations));
      // Emit temp buffer.
      CGF.EmitVarDecl(*cast<VarDecl>(cast<DeclRefExpr>(*ITA)->getDecl()));
      ++ITA;
      ++Count;
    }
  }
}

/// Copies final inscan reductions values to the original variables.
/// The code is the following:
/// \code
/// <orig_var> = buffer[num_iters-1];
/// \endcode
static void emitScanBasedDirectiveFinals(
    CodeGenFunction &CGF, const OMPLoopDirective &S,
    llvm::function_ref<llvm::Value *(CodeGenFunction &)> NumIteratorsGen) {
  llvm::Value *OMPScanNumIterations = CGF.Builder.CreateIntCast(
      NumIteratorsGen(CGF), CGF.SizeTy, /*isSigned=*/false);
  SmallVector<const Expr *, 4> Shareds;
  SmallVector<const Expr *, 4> LHSs;
  SmallVector<const Expr *, 4> RHSs;
  SmallVector<const Expr *, 4> Privates;
  SmallVector<const Expr *, 4> CopyOps;
  SmallVector<const Expr *, 4> CopyArrayElems;
  for (const auto *C : S.getClausesOfKind<OMPReductionClause>()) {
    assert(C->getModifier() == OMPC_REDUCTION_inscan &&
           "Only inscan reductions are expected.");
    Shareds.append(C->varlist_begin(), C->varlist_end());
    LHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
    RHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
    Privates.append(C->privates().begin(), C->privates().end());
    CopyOps.append(C->copy_ops().begin(), C->copy_ops().end());
    CopyArrayElems.append(C->copy_array_elems().begin(),
                          C->copy_array_elems().end());
  }
  // Create temp var and copy LHS value to this temp value.
  // LHS = TMP[LastIter];
  llvm::Value *OMPLast = CGF.Builder.CreateNSWSub(
      OMPScanNumIterations,
      llvm::ConstantInt::get(CGF.SizeTy, 1, /*isSigned=*/false));
  for (unsigned I = 0, E = CopyArrayElems.size(); I < E; ++I) {
    const Expr *PrivateExpr = Privates[I];
    const Expr *OrigExpr = Shareds[I];
    const Expr *CopyArrayElem = CopyArrayElems[I];
    CodeGenFunction::OpaqueValueMapping IdxMapping(
        CGF,
        cast<OpaqueValueExpr>(
            cast<ArraySubscriptExpr>(CopyArrayElem)->getIdx()),
        RValue::get(OMPLast));
    LValue DestLVal = CGF.EmitLValue(OrigExpr);
    LValue SrcLVal = CGF.EmitLValue(CopyArrayElem);
    CGF.EmitOMPCopy(
        PrivateExpr->getType(), DestLVal.getAddress(), SrcLVal.getAddress(),
        cast<VarDecl>(cast<DeclRefExpr>(LHSs[I])->getDecl()),
        cast<VarDecl>(cast<DeclRefExpr>(RHSs[I])->getDecl()), CopyOps[I]);
  }
}

/// Emits the code for the directive with inscan reductions.
/// The code is the following:
/// \code
/// #pragma omp ...
/// for (i: 0..<num_iters>) {
///   <input phase>;
///   buffer[i] = red;
/// }
/// #pragma omp master // in parallel region
/// for (int k = 0; k != ceil(log2(num_iters)); ++k)
/// for (size cnt = last_iter; cnt >= pow(2, k); --k)
///   buffer[i] op= buffer[i-pow(2,k)];
/// #pragma omp barrier // in parallel region
/// #pragma omp ...
/// for (0..<num_iters>) {
///   red = InclusiveScan ? buffer[i] : buffer[i-1];
///   <scan phase>;
/// }
/// \endcode
static void emitScanBasedDirective(
    CodeGenFunction &CGF, const OMPLoopDirective &S,
    llvm::function_ref<llvm::Value *(CodeGenFunction &)> NumIteratorsGen,
    llvm::function_ref<void(CodeGenFunction &)> FirstGen,
    llvm::function_ref<void(CodeGenFunction &)> SecondGen) {
  llvm::Value *OMPScanNumIterations = CGF.Builder.CreateIntCast(
      NumIteratorsGen(CGF), CGF.SizeTy, /*isSigned=*/false);
  SmallVector<const Expr *, 4> Privates;
  SmallVector<const Expr *, 4> ReductionOps;
  SmallVector<const Expr *, 4> LHSs;
  SmallVector<const Expr *, 4> RHSs;
  SmallVector<const Expr *, 4> CopyArrayElems;
  for (const auto *C : S.getClausesOfKind<OMPReductionClause>()) {
    assert(C->getModifier() == OMPC_REDUCTION_inscan &&
           "Only inscan reductions are expected.");
    Privates.append(C->privates().begin(), C->privates().end());
    ReductionOps.append(C->reduction_ops().begin(), C->reduction_ops().end());
    LHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
    RHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
    CopyArrayElems.append(C->copy_array_elems().begin(),
                          C->copy_array_elems().end());
  }
  CodeGenFunction::ParentLoopDirectiveForScanRegion ScanRegion(CGF, S);
  {
    // Emit loop with input phase:
    // #pragma omp ...
    // for (i: 0..<num_iters>) {
    //   <input phase>;
    //   buffer[i] = red;
    // }
    CGF.OMPFirstScanLoop = true;
    CodeGenFunction::OMPLocalDeclMapRAII Scope(CGF);
    FirstGen(CGF);
  }
  // #pragma omp barrier // in parallel region
  auto &&CodeGen = [&S, OMPScanNumIterations, &LHSs, &RHSs, &CopyArrayElems,
                    &ReductionOps,
                    &Privates](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    // Emit prefix reduction:
    // #pragma omp master // in parallel region
    // for (int k = 0; k <= ceil(log2(n)); ++k)
    llvm::BasicBlock *InputBB = CGF.Builder.GetInsertBlock();
    llvm::BasicBlock *LoopBB = CGF.createBasicBlock("omp.outer.log.scan.body");
    llvm::BasicBlock *ExitBB = CGF.createBasicBlock("omp.outer.log.scan.exit");
    llvm::Function *F =
        CGF.CGM.getIntrinsic(llvm::Intrinsic::log2, CGF.DoubleTy);
    llvm::Value *Arg =
        CGF.Builder.CreateUIToFP(OMPScanNumIterations, CGF.DoubleTy);
    llvm::Value *LogVal = CGF.EmitNounwindRuntimeCall(F, Arg);
    F = CGF.CGM.getIntrinsic(llvm::Intrinsic::ceil, CGF.DoubleTy);
    LogVal = CGF.EmitNounwindRuntimeCall(F, LogVal);
    LogVal = CGF.Builder.CreateFPToUI(LogVal, CGF.IntTy);
    llvm::Value *NMin1 = CGF.Builder.CreateNUWSub(
        OMPScanNumIterations, llvm::ConstantInt::get(CGF.SizeTy, 1));
    auto DL = ApplyDebugLocation::CreateDefaultArtificial(CGF, S.getBeginLoc());
    CGF.EmitBlock(LoopBB);
    auto *Counter = CGF.Builder.CreatePHI(CGF.IntTy, 2);
    // size pow2k = 1;
    auto *Pow2K = CGF.Builder.CreatePHI(CGF.SizeTy, 2);
    Counter->addIncoming(llvm::ConstantInt::get(CGF.IntTy, 0), InputBB);
    Pow2K->addIncoming(llvm::ConstantInt::get(CGF.SizeTy, 1), InputBB);
    // for (size i = n - 1; i >= 2 ^ k; --i)
    //   tmp[i] op= tmp[i-pow2k];
    llvm::BasicBlock *InnerLoopBB =
        CGF.createBasicBlock("omp.inner.log.scan.body");
    llvm::BasicBlock *InnerExitBB =
        CGF.createBasicBlock("omp.inner.log.scan.exit");
    llvm::Value *CmpI = CGF.Builder.CreateICmpUGE(NMin1, Pow2K);
    CGF.Builder.CreateCondBr(CmpI, InnerLoopBB, InnerExitBB);
    CGF.EmitBlock(InnerLoopBB);
    auto *IVal = CGF.Builder.CreatePHI(CGF.SizeTy, 2);
    IVal->addIncoming(NMin1, LoopBB);
    {
      CodeGenFunction::OMPPrivateScope PrivScope(CGF);
      auto *ILHS = LHSs.begin();
      auto *IRHS = RHSs.begin();
      for (const Expr *CopyArrayElem : CopyArrayElems) {
        const auto *LHSVD = cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
        const auto *RHSVD = cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
        Address LHSAddr = Address::invalid();
        {
          CodeGenFunction::OpaqueValueMapping IdxMapping(
              CGF,
              cast<OpaqueValueExpr>(
                  cast<ArraySubscriptExpr>(CopyArrayElem)->getIdx()),
              RValue::get(IVal));
          LHSAddr = CGF.EmitLValue(CopyArrayElem).getAddress();
        }
        PrivScope.addPrivate(LHSVD, LHSAddr);
        Address RHSAddr = Address::invalid();
        {
          llvm::Value *OffsetIVal = CGF.Builder.CreateNUWSub(IVal, Pow2K);
          CodeGenFunction::OpaqueValueMapping IdxMapping(
              CGF,
              cast<OpaqueValueExpr>(
                  cast<ArraySubscriptExpr>(CopyArrayElem)->getIdx()),
              RValue::get(OffsetIVal));
          RHSAddr = CGF.EmitLValue(CopyArrayElem).getAddress();
        }
        PrivScope.addPrivate(RHSVD, RHSAddr);
        ++ILHS;
        ++IRHS;
      }
      PrivScope.Privatize();
      CGF.CGM.getOpenMPRuntime().emitReduction(
          CGF, S.getEndLoc(), Privates, LHSs, RHSs, ReductionOps,
          {/*WithNowait=*/true, /*SimpleReduction=*/true, OMPD_unknown});
    }
    llvm::Value *NextIVal =
        CGF.Builder.CreateNUWSub(IVal, llvm::ConstantInt::get(CGF.SizeTy, 1));
    IVal->addIncoming(NextIVal, CGF.Builder.GetInsertBlock());
    CmpI = CGF.Builder.CreateICmpUGE(NextIVal, Pow2K);
    CGF.Builder.CreateCondBr(CmpI, InnerLoopBB, InnerExitBB);
    CGF.EmitBlock(InnerExitBB);
    llvm::Value *Next =
        CGF.Builder.CreateNUWAdd(Counter, llvm::ConstantInt::get(CGF.IntTy, 1));
    Counter->addIncoming(Next, CGF.Builder.GetInsertBlock());
    // pow2k <<= 1;
    llvm::Value *NextPow2K =
        CGF.Builder.CreateShl(Pow2K, 1, "", /*HasNUW=*/true);
    Pow2K->addIncoming(NextPow2K, CGF.Builder.GetInsertBlock());
    llvm::Value *Cmp = CGF.Builder.CreateICmpNE(Next, LogVal);
    CGF.Builder.CreateCondBr(Cmp, LoopBB, ExitBB);
    auto DL1 = ApplyDebugLocation::CreateDefaultArtificial(CGF, S.getEndLoc());
    CGF.EmitBlock(ExitBB);
  };
  if (isOpenMPParallelDirective(S.getDirectiveKind())) {
    CGF.CGM.getOpenMPRuntime().emitMasterRegion(CGF, CodeGen, S.getBeginLoc());
    CGF.CGM.getOpenMPRuntime().emitBarrierCall(
        CGF, S.getBeginLoc(), OMPD_unknown, /*EmitChecks=*/false,
        /*ForceSimpleCall=*/true);
  } else {
    RegionCodeGenTy RCG(CodeGen);
    RCG(CGF);
  }

  CGF.OMPFirstScanLoop = false;
  SecondGen(CGF);
}

static bool emitWorksharingDirective(CodeGenFunction &CGF,
                                     const OMPLoopDirective &S,
                                     bool HasCancel) {
  bool HasLastprivates;
  if (llvm::any_of(S.getClausesOfKind<OMPReductionClause>(),
                   [](const OMPReductionClause *C) {
                     return C->getModifier() == OMPC_REDUCTION_inscan;
                   })) {
    const auto &&NumIteratorsGen = [&S](CodeGenFunction &CGF) {
      CodeGenFunction::OMPLocalDeclMapRAII Scope(CGF);
      OMPLoopScope LoopScope(CGF, S);
      return CGF.EmitScalarExpr(S.getNumIterations());
    };
    const auto &&FirstGen = [&S, HasCancel](CodeGenFunction &CGF) {
      CodeGenFunction::OMPCancelStackRAII CancelRegion(
          CGF, S.getDirectiveKind(), HasCancel);
      (void)CGF.EmitOMPWorksharingLoop(S, S.getEnsureUpperBound(),
                                       emitForLoopBounds,
                                       emitDispatchForLoopBounds);
      // Emit an implicit barrier at the end.
      CGF.CGM.getOpenMPRuntime().emitBarrierCall(CGF, S.getBeginLoc(),
                                                 OMPD_for);
    };
    const auto &&SecondGen = [&S, HasCancel,
                              &HasLastprivates](CodeGenFunction &CGF) {
      CodeGenFunction::OMPCancelStackRAII CancelRegion(
          CGF, S.getDirectiveKind(), HasCancel);
      HasLastprivates = CGF.EmitOMPWorksharingLoop(S, S.getEnsureUpperBound(),
                                                   emitForLoopBounds,
                                                   emitDispatchForLoopBounds);
    };
    if (!isOpenMPParallelDirective(S.getDirectiveKind()))
      emitScanBasedDirectiveDecls(CGF, S, NumIteratorsGen);
    emitScanBasedDirective(CGF, S, NumIteratorsGen, FirstGen, SecondGen);
    if (!isOpenMPParallelDirective(S.getDirectiveKind()))
      emitScanBasedDirectiveFinals(CGF, S, NumIteratorsGen);
  } else {
    CodeGenFunction::OMPCancelStackRAII CancelRegion(CGF, S.getDirectiveKind(),
                                                     HasCancel);
    HasLastprivates = CGF.EmitOMPWorksharingLoop(S, S.getEnsureUpperBound(),
                                                 emitForLoopBounds,
                                                 emitDispatchForLoopBounds);
  }
  return HasLastprivates;
}

static bool isSupportedByOpenMPIRBuilder(const OMPForDirective &S) {
  if (S.hasCancel())
    return false;
  for (OMPClause *C : S.clauses()) {
    if (isa<OMPNowaitClause>(C))
      continue;

    if (auto *SC = dyn_cast<OMPScheduleClause>(C)) {
      if (SC->getFirstScheduleModifier() != OMPC_SCHEDULE_MODIFIER_unknown)
        return false;
      if (SC->getSecondScheduleModifier() != OMPC_SCHEDULE_MODIFIER_unknown)
        return false;
      switch (SC->getScheduleKind()) {
      case OMPC_SCHEDULE_auto:
      case OMPC_SCHEDULE_dynamic:
      case OMPC_SCHEDULE_runtime:
      case OMPC_SCHEDULE_guided:
      case OMPC_SCHEDULE_static:
        continue;
      case OMPC_SCHEDULE_unknown:
        return false;
      }
    }

    return false;
  }

  return true;
}

static llvm::omp::ScheduleKind
convertClauseKindToSchedKind(OpenMPScheduleClauseKind ScheduleClauseKind) {
  switch (ScheduleClauseKind) {
  case OMPC_SCHEDULE_unknown:
    return llvm::omp::OMP_SCHEDULE_Default;
  case OMPC_SCHEDULE_auto:
    return llvm::omp::OMP_SCHEDULE_Auto;
  case OMPC_SCHEDULE_dynamic:
    return llvm::omp::OMP_SCHEDULE_Dynamic;
  case OMPC_SCHEDULE_guided:
    return llvm::omp::OMP_SCHEDULE_Guided;
  case OMPC_SCHEDULE_runtime:
    return llvm::omp::OMP_SCHEDULE_Runtime;
  case OMPC_SCHEDULE_static:
    return llvm::omp::OMP_SCHEDULE_Static;
  }
  llvm_unreachable("Unhandled schedule kind");
}

void CodeGenFunction::EmitOMPForDirective(const OMPForDirective &S) {
  bool HasLastprivates = false;
  bool UseOMPIRBuilder =
      CGM.getLangOpts().OpenMPIRBuilder && isSupportedByOpenMPIRBuilder(S);
  auto &&CodeGen = [this, &S, &HasLastprivates,
                    UseOMPIRBuilder](CodeGenFunction &CGF, PrePostActionTy &) {
    // Use the OpenMPIRBuilder if enabled.
    if (UseOMPIRBuilder) {
      bool NeedsBarrier = !S.getSingleClause<OMPNowaitClause>();

      llvm::omp::ScheduleKind SchedKind = llvm::omp::OMP_SCHEDULE_Default;
      llvm::Value *ChunkSize = nullptr;
      if (auto *SchedClause = S.getSingleClause<OMPScheduleClause>()) {
        SchedKind =
            convertClauseKindToSchedKind(SchedClause->getScheduleKind());
        if (const Expr *ChunkSizeExpr = SchedClause->getChunkSize())
          ChunkSize = EmitScalarExpr(ChunkSizeExpr);
      }

      // Emit the associated statement and get its loop representation.
      const Stmt *Inner = S.getRawStmt();
      llvm::CanonicalLoopInfo *CLI =
          EmitOMPCollapsedCanonicalLoopNest(Inner, 1);

      llvm::OpenMPIRBuilder &OMPBuilder =
          CGM.getOpenMPRuntime().getOMPBuilder();
      llvm::OpenMPIRBuilder::InsertPointTy AllocaIP(
          AllocaInsertPt->getParent(), AllocaInsertPt->getIterator());
      OMPBuilder.applyWorkshareLoop(
          Builder.getCurrentDebugLocation(), CLI, AllocaIP, NeedsBarrier,
          SchedKind, ChunkSize, /*HasSimdModifier=*/false,
          /*HasMonotonicModifier=*/false, /*HasNonmonotonicModifier=*/false,
          /*HasOrderedClause=*/false);
      return;
    }

    HasLastprivates = emitWorksharingDirective(CGF, S, S.hasCancel());
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    OMPLexicalScope Scope(*this, S, OMPD_unknown);
    CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_for, CodeGen,
                                                S.hasCancel());
  }

  if (!UseOMPIRBuilder) {
    // Emit an implicit barrier at the end.
    if (!S.getSingleClause<OMPNowaitClause>() || HasLastprivates)
      CGM.getOpenMPRuntime().emitBarrierCall(*this, S.getBeginLoc(), OMPD_for);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPForSimdDirective(const OMPForSimdDirective &S) {
  bool HasLastprivates = false;
  auto &&CodeGen = [&S, &HasLastprivates](CodeGenFunction &CGF,
                                          PrePostActionTy &) {
    HasLastprivates = emitWorksharingDirective(CGF, S, /*HasCancel=*/false);
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    OMPLexicalScope Scope(*this, S, OMPD_unknown);
    CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_simd, CodeGen);
  }

  // Emit an implicit barrier at the end.
  if (!S.getSingleClause<OMPNowaitClause>() || HasLastprivates)
    CGM.getOpenMPRuntime().emitBarrierCall(*this, S.getBeginLoc(), OMPD_for);
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

static LValue createSectionLVal(CodeGenFunction &CGF, QualType Ty,
                                const Twine &Name,
                                llvm::Value *Init = nullptr) {
  LValue LVal = CGF.MakeAddrLValue(CGF.CreateMemTemp(Ty, Name), Ty);
  if (Init)
    CGF.EmitStoreThroughLValue(RValue::get(Init), LVal, /*isInit*/ true);
  return LVal;
}

void CodeGenFunction::EmitSections(const OMPExecutableDirective &S) {
  const Stmt *CapturedStmt = S.getInnermostCapturedStmt()->getCapturedStmt();
  const auto *CS = dyn_cast<CompoundStmt>(CapturedStmt);
  bool HasLastprivates = false;
  auto &&CodeGen = [&S, CapturedStmt, CS,
                    &HasLastprivates](CodeGenFunction &CGF, PrePostActionTy &) {
    const ASTContext &C = CGF.getContext();
    QualType KmpInt32Ty =
        C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1);
    // Emit helper vars inits.
    LValue LB = createSectionLVal(CGF, KmpInt32Ty, ".omp.sections.lb.",
                                  CGF.Builder.getInt32(0));
    llvm::ConstantInt *GlobalUBVal = CS != nullptr
                                         ? CGF.Builder.getInt32(CS->size() - 1)
                                         : CGF.Builder.getInt32(0);
    LValue UB =
        createSectionLVal(CGF, KmpInt32Ty, ".omp.sections.ub.", GlobalUBVal);
    LValue ST = createSectionLVal(CGF, KmpInt32Ty, ".omp.sections.st.",
                                  CGF.Builder.getInt32(1));
    LValue IL = createSectionLVal(CGF, KmpInt32Ty, ".omp.sections.il.",
                                  CGF.Builder.getInt32(0));
    // Loop counter.
    LValue IV = createSectionLVal(CGF, KmpInt32Ty, ".omp.sections.iv.");
    OpaqueValueExpr IVRefExpr(S.getBeginLoc(), KmpInt32Ty, VK_LValue);
    CodeGenFunction::OpaqueValueMapping OpaqueIV(CGF, &IVRefExpr, IV);
    OpaqueValueExpr UBRefExpr(S.getBeginLoc(), KmpInt32Ty, VK_LValue);
    CodeGenFunction::OpaqueValueMapping OpaqueUB(CGF, &UBRefExpr, UB);
    // Generate condition for loop.
    BinaryOperator *Cond = BinaryOperator::Create(
        C, &IVRefExpr, &UBRefExpr, BO_LE, C.BoolTy, VK_PRValue, OK_Ordinary,
        S.getBeginLoc(), FPOptionsOverride());
    // Increment for loop counter.
    UnaryOperator *Inc = UnaryOperator::Create(
        C, &IVRefExpr, UO_PreInc, KmpInt32Ty, VK_PRValue, OK_Ordinary,
        S.getBeginLoc(), true, FPOptionsOverride());
    auto &&BodyGen = [CapturedStmt, CS, &S, &IV](CodeGenFunction &CGF) {
      // Iterate through all sections and emit a switch construct:
      // switch (IV) {
      //   case 0:
      //     <SectionStmt[0]>;
      //     break;
      // ...
      //   case <NumSection> - 1:
      //     <SectionStmt[<NumSection> - 1]>;
      //     break;
      // }
      // .omp.sections.exit:
      llvm::BasicBlock *ExitBB = CGF.createBasicBlock(".omp.sections.exit");
      llvm::SwitchInst *SwitchStmt =
          CGF.Builder.CreateSwitch(CGF.EmitLoadOfScalar(IV, S.getBeginLoc()),
                                   ExitBB, CS == nullptr ? 1 : CS->size());
      if (CS) {
        unsigned CaseNumber = 0;
        for (const Stmt *SubStmt : CS->children()) {
          auto CaseBB = CGF.createBasicBlock(".omp.sections.case");
          CGF.EmitBlock(CaseBB);
          SwitchStmt->addCase(CGF.Builder.getInt32(CaseNumber), CaseBB);
          CGF.EmitStmt(SubStmt);
          CGF.EmitBranch(ExitBB);
          ++CaseNumber;
        }
      } else {
        llvm::BasicBlock *CaseBB = CGF.createBasicBlock(".omp.sections.case");
        CGF.EmitBlock(CaseBB);
        SwitchStmt->addCase(CGF.Builder.getInt32(0), CaseBB);
        CGF.EmitStmt(CapturedStmt);
        CGF.EmitBranch(ExitBB);
      }
      CGF.EmitBlock(ExitBB, /*IsFinished=*/true);
    };

    CodeGenFunction::OMPPrivateScope LoopScope(CGF);
    if (CGF.EmitOMPFirstprivateClause(S, LoopScope)) {
      // Emit implicit barrier to synchronize threads and avoid data races on
      // initialization of firstprivate variables and post-update of lastprivate
      // variables.
      CGF.CGM.getOpenMPRuntime().emitBarrierCall(
          CGF, S.getBeginLoc(), OMPD_unknown, /*EmitChecks=*/false,
          /*ForceSimpleCall=*/true);
    }
    CGF.EmitOMPPrivateClause(S, LoopScope);
    CGOpenMPRuntime::LastprivateConditionalRAII LPCRegion(CGF, S, IV);
    HasLastprivates = CGF.EmitOMPLastprivateClauseInit(S, LoopScope);
    CGF.EmitOMPReductionClauseInit(S, LoopScope);
    (void)LoopScope.Privatize();
    if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()))
      CGF.CGM.getOpenMPRuntime().adjustTargetSpecificDataForLambdas(CGF, S);

    // Emit static non-chunked loop.
    OpenMPScheduleTy ScheduleKind;
    ScheduleKind.Schedule = OMPC_SCHEDULE_static;
    CGOpenMPRuntime::StaticRTInput StaticInit(
        /*IVSize=*/32, /*IVSigned=*/true, /*Ordered=*/false, IL.getAddress(),
        LB.getAddress(), UB.getAddress(), ST.getAddress());
    CGF.CGM.getOpenMPRuntime().emitForStaticInit(
        CGF, S.getBeginLoc(), S.getDirectiveKind(), ScheduleKind, StaticInit);
    // UB = min(UB, GlobalUB);
    llvm::Value *UBVal = CGF.EmitLoadOfScalar(UB, S.getBeginLoc());
    llvm::Value *MinUBGlobalUB = CGF.Builder.CreateSelect(
        CGF.Builder.CreateICmpSLT(UBVal, GlobalUBVal), UBVal, GlobalUBVal);
    CGF.EmitStoreOfScalar(MinUBGlobalUB, UB);
    // IV = LB;
    CGF.EmitStoreOfScalar(CGF.EmitLoadOfScalar(LB, S.getBeginLoc()), IV);
    // while (idx <= UB) { BODY; ++idx; }
    CGF.EmitOMPInnerLoop(S, /*RequiresCleanup=*/false, Cond, Inc, BodyGen,
                         [](CodeGenFunction &) {});
    // Tell the runtime we are done.
    auto &&CodeGen = [&S](CodeGenFunction &CGF) {
      CGF.CGM.getOpenMPRuntime().emitForStaticFinish(CGF, S.getEndLoc(),
                                                     OMPD_sections);
    };
    CGF.OMPCancelStack.emitExit(CGF, S.getDirectiveKind(), CodeGen);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_parallel);
    // Emit post-update of the reduction variables if IsLastIter != 0.
    emitPostUpdateForReductionClause(CGF, S, [IL, &S](CodeGenFunction &CGF) {
      return CGF.Builder.CreateIsNotNull(
          CGF.EmitLoadOfScalar(IL, S.getBeginLoc()));
    });

    // Emit final copy of the lastprivate variables if IsLastIter != 0.
    if (HasLastprivates)
      CGF.EmitOMPLastprivateClauseFinal(
          S, /*NoFinals=*/false,
          CGF.Builder.CreateIsNotNull(
              CGF.EmitLoadOfScalar(IL, S.getBeginLoc())));
  };

  bool HasCancel = false;
  if (auto *OSD = dyn_cast<OMPSectionsDirective>(&S))
    HasCancel = OSD->hasCancel();
  else if (auto *OPSD = dyn_cast<OMPParallelSectionsDirective>(&S))
    HasCancel = OPSD->hasCancel();
  OMPCancelStackRAII CancelRegion(*this, S.getDirectiveKind(), HasCancel);
  CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_sections, CodeGen,
                                              HasCancel);
  // Emit barrier for lastprivates only if 'sections' directive has 'nowait'
  // clause. Otherwise the barrier will be generated by the codegen for the
  // directive.
  if (HasLastprivates && S.getSingleClause<OMPNowaitClause>()) {
    // Emit implicit barrier to synchronize threads and avoid data races on
    // initialization of firstprivate variables.
    CGM.getOpenMPRuntime().emitBarrierCall(*this, S.getBeginLoc(),
                                           OMPD_unknown);
  }
}

void CodeGenFunction::EmitOMPSectionsDirective(const OMPSectionsDirective &S) {
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;
    using BodyGenCallbackTy = llvm::OpenMPIRBuilder::StorableBodyGenCallbackTy;

    auto FiniCB = [this](InsertPointTy IP) {
      OMPBuilderCBHelpers::FinalizeOMPRegion(*this, IP);
    };

    const CapturedStmt *ICS = S.getInnermostCapturedStmt();
    const Stmt *CapturedStmt = S.getInnermostCapturedStmt()->getCapturedStmt();
    const auto *CS = dyn_cast<CompoundStmt>(CapturedStmt);
    llvm::SmallVector<BodyGenCallbackTy, 4> SectionCBVector;
    if (CS) {
      for (const Stmt *SubStmt : CS->children()) {
        auto SectionCB = [this, SubStmt](InsertPointTy AllocaIP,
                                         InsertPointTy CodeGenIP) {
          OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
              *this, SubStmt, AllocaIP, CodeGenIP, "section");
        };
        SectionCBVector.push_back(SectionCB);
      }
    } else {
      auto SectionCB = [this, CapturedStmt](InsertPointTy AllocaIP,
                                            InsertPointTy CodeGenIP) {
        OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
            *this, CapturedStmt, AllocaIP, CodeGenIP, "section");
      };
      SectionCBVector.push_back(SectionCB);
    }

    // Privatization callback that performs appropriate action for
    // shared/private/firstprivate/lastprivate/copyin/... variables.
    //
    // TODO: This defaults to shared right now.
    auto PrivCB = [](InsertPointTy AllocaIP, InsertPointTy CodeGenIP,
                     llvm::Value &, llvm::Value &Val, llvm::Value *&ReplVal) {
      // The next line is appropriate only for variables (Val) with the
      // data-sharing attribute "shared".
      ReplVal = &Val;

      return CodeGenIP;
    };

    CGCapturedStmtInfo CGSI(*ICS, CR_OpenMP);
    CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(*this, &CGSI);
    llvm::OpenMPIRBuilder::InsertPointTy AllocaIP(
        AllocaInsertPt->getParent(), AllocaInsertPt->getIterator());
    Builder.restoreIP(OMPBuilder.createSections(
        Builder, AllocaIP, SectionCBVector, PrivCB, FiniCB, S.hasCancel(),
        S.getSingleClause<OMPNowaitClause>()));
    return;
  }
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    OMPLexicalScope Scope(*this, S, OMPD_unknown);
    EmitSections(S);
  }
  // Emit an implicit barrier at the end.
  if (!S.getSingleClause<OMPNowaitClause>()) {
    CGM.getOpenMPRuntime().emitBarrierCall(*this, S.getBeginLoc(),
                                           OMPD_sections);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPSectionDirective(const OMPSectionDirective &S) {
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

    const Stmt *SectionRegionBodyStmt = S.getAssociatedStmt();
    auto FiniCB = [this](InsertPointTy IP) {
      OMPBuilderCBHelpers::FinalizeOMPRegion(*this, IP);
    };

    auto BodyGenCB = [SectionRegionBodyStmt, this](InsertPointTy AllocaIP,
                                                   InsertPointTy CodeGenIP) {
      OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
          *this, SectionRegionBodyStmt, AllocaIP, CodeGenIP, "section");
    };

    LexicalScope Scope(*this, S.getSourceRange());
    EmitStopPoint(&S);
    Builder.restoreIP(OMPBuilder.createSection(Builder, BodyGenCB, FiniCB));

    return;
  }
  LexicalScope Scope(*this, S.getSourceRange());
  EmitStopPoint(&S);
  EmitStmt(S.getAssociatedStmt());
}

void CodeGenFunction::EmitOMPSingleDirective(const OMPSingleDirective &S) {
  llvm::SmallVector<const Expr *, 8> CopyprivateVars;
  llvm::SmallVector<const Expr *, 8> DestExprs;
  llvm::SmallVector<const Expr *, 8> SrcExprs;
  llvm::SmallVector<const Expr *, 8> AssignmentOps;
  // Check if there are any 'copyprivate' clauses associated with this
  // 'single' construct.
  // Build a list of copyprivate variables along with helper expressions
  // (<source>, <destination>, <destination>=<source> expressions)
  for (const auto *C : S.getClausesOfKind<OMPCopyprivateClause>()) {
    CopyprivateVars.append(C->varlists().begin(), C->varlists().end());
    DestExprs.append(C->destination_exprs().begin(),
                     C->destination_exprs().end());
    SrcExprs.append(C->source_exprs().begin(), C->source_exprs().end());
    AssignmentOps.append(C->assignment_ops().begin(),
                         C->assignment_ops().end());
  }
  // Emit code for 'single' region along with 'copyprivate' clauses
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope SingleScope(CGF);
    (void)CGF.EmitOMPFirstprivateClause(S, SingleScope);
    CGF.EmitOMPPrivateClause(S, SingleScope);
    (void)SingleScope.Privatize();
    CGF.EmitStmt(S.getInnermostCapturedStmt()->getCapturedStmt());
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    OMPLexicalScope Scope(*this, S, OMPD_unknown);
    CGM.getOpenMPRuntime().emitSingleRegion(*this, CodeGen, S.getBeginLoc(),
                                            CopyprivateVars, DestExprs,
                                            SrcExprs, AssignmentOps);
  }
  // Emit an implicit barrier at the end (to avoid data race on firstprivate
  // init or if no 'nowait' clause was specified and no 'copyprivate' clause).
  if (!S.getSingleClause<OMPNowaitClause>() && CopyprivateVars.empty()) {
    CGM.getOpenMPRuntime().emitBarrierCall(
        *this, S.getBeginLoc(),
        S.getSingleClause<OMPNowaitClause>() ? OMPD_unknown : OMPD_single);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

static void emitMaster(CodeGenFunction &CGF, const OMPExecutableDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CGF.EmitStmt(S.getRawStmt());
  };
  CGF.CGM.getOpenMPRuntime().emitMasterRegion(CGF, CodeGen, S.getBeginLoc());
}

void CodeGenFunction::EmitOMPMasterDirective(const OMPMasterDirective &S) {
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

    const Stmt *MasterRegionBodyStmt = S.getAssociatedStmt();

    auto FiniCB = [this](InsertPointTy IP) {
      OMPBuilderCBHelpers::FinalizeOMPRegion(*this, IP);
    };

    auto BodyGenCB = [MasterRegionBodyStmt, this](InsertPointTy AllocaIP,
                                                  InsertPointTy CodeGenIP) {
      OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
          *this, MasterRegionBodyStmt, AllocaIP, CodeGenIP, "master");
    };

    LexicalScope Scope(*this, S.getSourceRange());
    EmitStopPoint(&S);
    Builder.restoreIP(OMPBuilder.createMaster(Builder, BodyGenCB, FiniCB));

    return;
  }
  LexicalScope Scope(*this, S.getSourceRange());
  EmitStopPoint(&S);
  emitMaster(*this, S);
}

static void emitMasked(CodeGenFunction &CGF, const OMPExecutableDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CGF.EmitStmt(S.getRawStmt());
  };
  Expr *Filter = nullptr;
  if (const auto *FilterClause = S.getSingleClause<OMPFilterClause>())
    Filter = FilterClause->getThreadID();
  CGF.CGM.getOpenMPRuntime().emitMaskedRegion(CGF, CodeGen, S.getBeginLoc(),
                                              Filter);
}

void CodeGenFunction::EmitOMPMaskedDirective(const OMPMaskedDirective &S) {
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

    const Stmt *MaskedRegionBodyStmt = S.getAssociatedStmt();
    const Expr *Filter = nullptr;
    if (const auto *FilterClause = S.getSingleClause<OMPFilterClause>())
      Filter = FilterClause->getThreadID();
    llvm::Value *FilterVal = Filter
                                 ? EmitScalarExpr(Filter, CGM.Int32Ty)
                                 : llvm::ConstantInt::get(CGM.Int32Ty, /*V=*/0);

    auto FiniCB = [this](InsertPointTy IP) {
      OMPBuilderCBHelpers::FinalizeOMPRegion(*this, IP);
    };

    auto BodyGenCB = [MaskedRegionBodyStmt, this](InsertPointTy AllocaIP,
                                                  InsertPointTy CodeGenIP) {
      OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
          *this, MaskedRegionBodyStmt, AllocaIP, CodeGenIP, "masked");
    };

    LexicalScope Scope(*this, S.getSourceRange());
    EmitStopPoint(&S);
    Builder.restoreIP(
        OMPBuilder.createMasked(Builder, BodyGenCB, FiniCB, FilterVal));

    return;
  }
  LexicalScope Scope(*this, S.getSourceRange());
  EmitStopPoint(&S);
  emitMasked(*this, S);
}

void CodeGenFunction::EmitOMPCriticalDirective(const OMPCriticalDirective &S) {
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

    const Stmt *CriticalRegionBodyStmt = S.getAssociatedStmt();
    const Expr *Hint = nullptr;
    if (const auto *HintClause = S.getSingleClause<OMPHintClause>())
      Hint = HintClause->getHint();

    // TODO: This is slightly different from what's currently being done in
    // clang. Fix the Int32Ty to IntPtrTy (pointer width size) when everything
    // about typing is final.
    llvm::Value *HintInst = nullptr;
    if (Hint)
      HintInst =
          Builder.CreateIntCast(EmitScalarExpr(Hint), CGM.Int32Ty, false);

    auto FiniCB = [this](InsertPointTy IP) {
      OMPBuilderCBHelpers::FinalizeOMPRegion(*this, IP);
    };

    auto BodyGenCB = [CriticalRegionBodyStmt, this](InsertPointTy AllocaIP,
                                                    InsertPointTy CodeGenIP) {
      OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
          *this, CriticalRegionBodyStmt, AllocaIP, CodeGenIP, "critical");
    };

    LexicalScope Scope(*this, S.getSourceRange());
    EmitStopPoint(&S);
    Builder.restoreIP(OMPBuilder.createCritical(
        Builder, BodyGenCB, FiniCB, S.getDirectiveName().getAsString(),
        HintInst));

    return;
  }

  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CGF.EmitStmt(S.getAssociatedStmt());
  };
  const Expr *Hint = nullptr;
  if (const auto *HintClause = S.getSingleClause<OMPHintClause>())
    Hint = HintClause->getHint();
  LexicalScope Scope(*this, S.getSourceRange());
  EmitStopPoint(&S);
  CGM.getOpenMPRuntime().emitCriticalRegion(*this,
                                            S.getDirectiveName().getAsString(),
                                            CodeGen, S.getBeginLoc(), Hint);
}

void CodeGenFunction::EmitOMPParallelForDirective(
    const OMPParallelForDirective &S) {
  // Emit directive as a combined directive that consists of two implicit
  // directives: 'parallel' with 'for' directive.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    emitOMPCopyinClause(CGF, S);
    (void)emitWorksharingDirective(CGF, S, S.hasCancel());
  };
  {
    const auto &&NumIteratorsGen = [&S](CodeGenFunction &CGF) {
      CodeGenFunction::OMPLocalDeclMapRAII Scope(CGF);
      CGCapturedStmtInfo CGSI(CR_OpenMP);
      CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGSI);
      OMPLoopScope LoopScope(CGF, S);
      return CGF.EmitScalarExpr(S.getNumIterations());
    };
    bool IsInscan = llvm::any_of(S.getClausesOfKind<OMPReductionClause>(),
                     [](const OMPReductionClause *C) {
                       return C->getModifier() == OMPC_REDUCTION_inscan;
                     });
    if (IsInscan)
      emitScanBasedDirectiveDecls(*this, S, NumIteratorsGen);
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    emitCommonOMPParallelDirective(*this, S, OMPD_for, CodeGen,
                                   emitEmptyBoundParameters);
    if (IsInscan)
      emitScanBasedDirectiveFinals(*this, S, NumIteratorsGen);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPParallelForSimdDirective(
    const OMPParallelForSimdDirective &S) {
  // Emit directive as a combined directive that consists of two implicit
  // directives: 'parallel' with 'for' directive.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    emitOMPCopyinClause(CGF, S);
    (void)emitWorksharingDirective(CGF, S, /*HasCancel=*/false);
  };
  {
    const auto &&NumIteratorsGen = [&S](CodeGenFunction &CGF) {
      CodeGenFunction::OMPLocalDeclMapRAII Scope(CGF);
      CGCapturedStmtInfo CGSI(CR_OpenMP);
      CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGSI);
      OMPLoopScope LoopScope(CGF, S);
      return CGF.EmitScalarExpr(S.getNumIterations());
    };
    bool IsInscan = llvm::any_of(S.getClausesOfKind<OMPReductionClause>(),
                     [](const OMPReductionClause *C) {
                       return C->getModifier() == OMPC_REDUCTION_inscan;
                     });
    if (IsInscan)
      emitScanBasedDirectiveDecls(*this, S, NumIteratorsGen);
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    emitCommonOMPParallelDirective(*this, S, OMPD_for_simd, CodeGen,
                                   emitEmptyBoundParameters);
    if (IsInscan)
      emitScanBasedDirectiveFinals(*this, S, NumIteratorsGen);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPParallelMasterDirective(
    const OMPParallelMasterDirective &S) {
  // Emit directive as a combined directive that consists of two implicit
  // directives: 'parallel' with 'master' directive.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    emitOMPCopyinClause(CGF, S);
    (void)CGF.EmitOMPFirstprivateClause(S, PrivateScope);
    CGF.EmitOMPPrivateClause(S, PrivateScope);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    emitMaster(CGF, S);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_parallel);
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    emitCommonOMPParallelDirective(*this, S, OMPD_master, CodeGen,
                                   emitEmptyBoundParameters);
    emitPostUpdateForReductionClause(*this, S,
                                     [](CodeGenFunction &) { return nullptr; });
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPParallelMaskedDirective(
    const OMPParallelMaskedDirective &S) {
  // Emit directive as a combined directive that consists of two implicit
  // directives: 'parallel' with 'masked' directive.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    emitOMPCopyinClause(CGF, S);
    (void)CGF.EmitOMPFirstprivateClause(S, PrivateScope);
    CGF.EmitOMPPrivateClause(S, PrivateScope);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    emitMasked(CGF, S);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_parallel);
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    emitCommonOMPParallelDirective(*this, S, OMPD_masked, CodeGen,
                                   emitEmptyBoundParameters);
    emitPostUpdateForReductionClause(*this, S,
                                     [](CodeGenFunction &) { return nullptr; });
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPParallelSectionsDirective(
    const OMPParallelSectionsDirective &S) {
  // Emit directive as a combined directive that consists of two implicit
  // directives: 'parallel' with 'sections' directive.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    emitOMPCopyinClause(CGF, S);
    CGF.EmitSections(S);
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    emitCommonOMPParallelDirective(*this, S, OMPD_sections, CodeGen,
                                   emitEmptyBoundParameters);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

namespace {
/// Get the list of variables declared in the context of the untied tasks.
class CheckVarsEscapingUntiedTaskDeclContext final
    : public ConstStmtVisitor<CheckVarsEscapingUntiedTaskDeclContext> {
  llvm::SmallVector<const VarDecl *, 4> PrivateDecls;

public:
  explicit CheckVarsEscapingUntiedTaskDeclContext() = default;
  virtual ~CheckVarsEscapingUntiedTaskDeclContext() = default;
  void VisitDeclStmt(const DeclStmt *S) {
    if (!S)
      return;
    // Need to privatize only local vars, static locals can be processed as is.
    for (const Decl *D : S->decls()) {
      if (const auto *VD = dyn_cast_or_null<VarDecl>(D))
        if (VD->hasLocalStorage())
          PrivateDecls.push_back(VD);
    }
  }
  void VisitOMPExecutableDirective(const OMPExecutableDirective *) {}
  void VisitCapturedStmt(const CapturedStmt *) {}
  void VisitLambdaExpr(const LambdaExpr *) {}
  void VisitBlockExpr(const BlockExpr *) {}
  void VisitStmt(const Stmt *S) {
    if (!S)
      return;
    for (const Stmt *Child : S->children())
      if (Child)
        Visit(Child);
  }

  /// Swaps list of vars with the provided one.
  ArrayRef<const VarDecl *> getPrivateDecls() const { return PrivateDecls; }
};
} // anonymous namespace

static void buildDependences(const OMPExecutableDirective &S,
                             OMPTaskDataTy &Data) {

  // First look for 'omp_all_memory' and add this first.
  bool OmpAllMemory = false;
  if (llvm::any_of(
          S.getClausesOfKind<OMPDependClause>(), [](const OMPDependClause *C) {
            return C->getDependencyKind() == OMPC_DEPEND_outallmemory ||
                   C->getDependencyKind() == OMPC_DEPEND_inoutallmemory;
          })) {
    OmpAllMemory = true;
    // Since both OMPC_DEPEND_outallmemory and OMPC_DEPEND_inoutallmemory are
    // equivalent to the runtime, always use OMPC_DEPEND_outallmemory to
    // simplify.
    OMPTaskDataTy::DependData &DD =
        Data.Dependences.emplace_back(OMPC_DEPEND_outallmemory,
                                      /*IteratorExpr=*/nullptr);
    // Add a nullptr Expr to simplify the codegen in emitDependData.
    DD.DepExprs.push_back(nullptr);
  }
  // Add remaining dependences skipping any 'out' or 'inout' if they are
  // overridden by 'omp_all_memory'.
  for (const auto *C : S.getClausesOfKind<OMPDependClause>()) {
    OpenMPDependClauseKind Kind = C->getDependencyKind();
    if (Kind == OMPC_DEPEND_outallmemory || Kind == OMPC_DEPEND_inoutallmemory)
      continue;
    if (OmpAllMemory && (Kind == OMPC_DEPEND_out || Kind == OMPC_DEPEND_inout))
      continue;
    OMPTaskDataTy::DependData &DD =
        Data.Dependences.emplace_back(C->getDependencyKind(), C->getModifier());
    DD.DepExprs.append(C->varlist_begin(), C->varlist_end());
  }
}

void CodeGenFunction::EmitOMPTaskBasedDirective(
    const OMPExecutableDirective &S, const OpenMPDirectiveKind CapturedRegion,
    const RegionCodeGenTy &BodyGen, const TaskGenTy &TaskGen,
    OMPTaskDataTy &Data) {
  // Emit outlined function for task construct.
  const CapturedStmt *CS = S.getCapturedStmt(CapturedRegion);
  auto I = CS->getCapturedDecl()->param_begin();
  auto PartId = std::next(I);
  auto TaskT = std::next(I, 4);
  // Check if the task is final
  if (const auto *Clause = S.getSingleClause<OMPFinalClause>()) {
    // If the condition constant folds and can be elided, try to avoid emitting
    // the condition and the dead arm of the if/else.
    const Expr *Cond = Clause->getCondition();
    bool CondConstant;
    if (ConstantFoldsToSimpleInteger(Cond, CondConstant))
      Data.Final.setInt(CondConstant);
    else
      Data.Final.setPointer(EvaluateExprAsBool(Cond));
  } else {
    // By default the task is not final.
    Data.Final.setInt(/*IntVal=*/false);
  }
  // Check if the task has 'priority' clause.
  if (const auto *Clause = S.getSingleClause<OMPPriorityClause>()) {
    const Expr *Prio = Clause->getPriority();
    Data.Priority.setInt(/*IntVal=*/true);
    Data.Priority.setPointer(EmitScalarConversion(
        EmitScalarExpr(Prio), Prio->getType(),
        getContext().getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1),
        Prio->getExprLoc()));
  }
  // The first function argument for tasks is a thread id, the second one is a
  // part id (0 for tied tasks, >=0 for untied task).
  llvm::DenseSet<const VarDecl *> EmittedAsPrivate;
  // Get list of private variables.
  for (const auto *C : S.getClausesOfKind<OMPPrivateClause>()) {
    auto IRef = C->varlist_begin();
    for (const Expr *IInit : C->private_copies()) {
      const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      if (EmittedAsPrivate.insert(OrigVD->getCanonicalDecl()).second) {
        Data.PrivateVars.push_back(*IRef);
        Data.PrivateCopies.push_back(IInit);
      }
      ++IRef;
    }
  }
  EmittedAsPrivate.clear();
  // Get list of firstprivate variables.
  for (const auto *C : S.getClausesOfKind<OMPFirstprivateClause>()) {
    auto IRef = C->varlist_begin();
    auto IElemInitRef = C->inits().begin();
    for (const Expr *IInit : C->private_copies()) {
      const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      if (EmittedAsPrivate.insert(OrigVD->getCanonicalDecl()).second) {
        Data.FirstprivateVars.push_back(*IRef);
        Data.FirstprivateCopies.push_back(IInit);
        Data.FirstprivateInits.push_back(*IElemInitRef);
      }
      ++IRef;
      ++IElemInitRef;
    }
  }
  // Get list of lastprivate variables (for taskloops).
  llvm::MapVector<const VarDecl *, const DeclRefExpr *> LastprivateDstsOrigs;
  for (const auto *C : S.getClausesOfKind<OMPLastprivateClause>()) {
    auto IRef = C->varlist_begin();
    auto ID = C->destination_exprs().begin();
    for (const Expr *IInit : C->private_copies()) {
      const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(*IRef)->getDecl());
      if (EmittedAsPrivate.insert(OrigVD->getCanonicalDecl()).second) {
        Data.LastprivateVars.push_back(*IRef);
        Data.LastprivateCopies.push_back(IInit);
      }
      LastprivateDstsOrigs.insert(
          std::make_pair(cast<VarDecl>(cast<DeclRefExpr>(*ID)->getDecl()),
                         cast<DeclRefExpr>(*IRef)));
      ++IRef;
      ++ID;
    }
  }
  SmallVector<const Expr *, 4> LHSs;
  SmallVector<const Expr *, 4> RHSs;
  for (const auto *C : S.getClausesOfKind<OMPReductionClause>()) {
    Data.ReductionVars.append(C->varlist_begin(), C->varlist_end());
    Data.ReductionOrigs.append(C->varlist_begin(), C->varlist_end());
    Data.ReductionCopies.append(C->privates().begin(), C->privates().end());
    Data.ReductionOps.append(C->reduction_ops().begin(),
                             C->reduction_ops().end());
    LHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
    RHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
  }
  Data.Reductions = CGM.getOpenMPRuntime().emitTaskReductionInit(
      *this, S.getBeginLoc(), LHSs, RHSs, Data);
  // Build list of dependences.
  buildDependences(S, Data);
  // Get list of local vars for untied tasks.
  if (!Data.Tied) {
    CheckVarsEscapingUntiedTaskDeclContext Checker;
    Checker.Visit(S.getInnermostCapturedStmt()->getCapturedStmt());
    Data.PrivateLocals.append(Checker.getPrivateDecls().begin(),
                              Checker.getPrivateDecls().end());
  }
  auto &&CodeGen = [&Data, &S, CS, &BodyGen, &LastprivateDstsOrigs,
                    CapturedRegion](CodeGenFunction &CGF,
                                    PrePostActionTy &Action) {
    llvm::MapVector<CanonicalDeclPtr<const VarDecl>,
                    std::pair<Address, Address>>
        UntiedLocalVars;
    // Set proper addresses for generated private copies.
    OMPPrivateScope Scope(CGF);
    // Generate debug info for variables present in shared clause.
    if (auto *DI = CGF.getDebugInfo()) {
      llvm::SmallDenseMap<const VarDecl *, FieldDecl *> CaptureFields =
          CGF.CapturedStmtInfo->getCaptureFields();
      llvm::Value *ContextValue = CGF.CapturedStmtInfo->getContextValue();
      if (CaptureFields.size() && ContextValue) {
        unsigned CharWidth = CGF.getContext().getCharWidth();
        // The shared variables are packed together as members of structure.
        // So the address of each shared variable can be computed by adding
        // offset of it (within record) to the base address of record. For each
        // shared variable, debug intrinsic llvm.dbg.declare is generated with
        // appropriate expressions (DIExpression).
        // Ex:
        //  %12 = load %struct.anon*, %struct.anon** %__context.addr.i
        //  call void @llvm.dbg.declare(metadata %struct.anon* %12,
        //            metadata !svar1,
        //            metadata !DIExpression(DW_OP_deref))
        //  call void @llvm.dbg.declare(metadata %struct.anon* %12,
        //            metadata !svar2,
        //            metadata !DIExpression(DW_OP_plus_uconst, 8, DW_OP_deref))
        for (auto It = CaptureFields.begin(); It != CaptureFields.end(); ++It) {
          const VarDecl *SharedVar = It->first;
          RecordDecl *CaptureRecord = It->second->getParent();
          const ASTRecordLayout &Layout =
              CGF.getContext().getASTRecordLayout(CaptureRecord);
          unsigned Offset =
              Layout.getFieldOffset(It->second->getFieldIndex()) / CharWidth;
          if (CGF.CGM.getCodeGenOpts().hasReducedDebugInfo())
            (void)DI->EmitDeclareOfAutoVariable(SharedVar, ContextValue,
                                                CGF.Builder, false);
          // Get the call dbg.declare instruction we just created and update
          // its DIExpression to add offset to base address.
          auto UpdateExpr = [](llvm::LLVMContext &Ctx, auto *Declare,
                               unsigned Offset) {
            SmallVector<uint64_t, 8> Ops;
            // Add offset to the base address if non zero.
            if (Offset) {
              Ops.push_back(llvm::dwarf::DW_OP_plus_uconst);
              Ops.push_back(Offset);
            }
            Ops.push_back(llvm::dwarf::DW_OP_deref);
            Declare->setExpression(llvm::DIExpression::get(Ctx, Ops));
          };
          llvm::Instruction &Last = CGF.Builder.GetInsertBlock()->back();
          if (auto DDI = dyn_cast<llvm::DbgVariableIntrinsic>(&Last))
            UpdateExpr(DDI->getContext(), DDI, Offset);
          // If we're emitting using the new debug info format into a block
          // without a terminator, the record will be "trailing".
          assert(!Last.isTerminator() && "unexpected terminator");
          if (auto *Marker =
                  CGF.Builder.GetInsertBlock()->getTrailingDbgRecords()) {
            for (llvm::DbgVariableRecord &DVR : llvm::reverse(
                     llvm::filterDbgVars(Marker->getDbgRecordRange()))) {
              UpdateExpr(Last.getContext(), &DVR, Offset);
              break;
            }
          }
        }
      }
    }
    llvm::SmallVector<std::pair<const VarDecl *, Address>, 16> FirstprivatePtrs;
    if (!Data.PrivateVars.empty() || !Data.FirstprivateVars.empty() ||
        !Data.LastprivateVars.empty() || !Data.PrivateLocals.empty()) {
      enum { PrivatesParam = 2, CopyFnParam = 3 };
      llvm::Value *CopyFn = CGF.Builder.CreateLoad(
          CGF.GetAddrOfLocalVar(CS->getCapturedDecl()->getParam(CopyFnParam)));
      llvm::Value *PrivatesPtr = CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(
          CS->getCapturedDecl()->getParam(PrivatesParam)));
      // Map privates.
      llvm::SmallVector<std::pair<const VarDecl *, Address>, 16> PrivatePtrs;
      llvm::SmallVector<llvm::Value *, 16> CallArgs;
      llvm::SmallVector<llvm::Type *, 4> ParamTypes;
      CallArgs.push_back(PrivatesPtr);
      ParamTypes.push_back(PrivatesPtr->getType());
      for (const Expr *E : Data.PrivateVars) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
        RawAddress PrivatePtr = CGF.CreateMemTemp(
            CGF.getContext().getPointerType(E->getType()), ".priv.ptr.addr");
        PrivatePtrs.emplace_back(VD, PrivatePtr);
        CallArgs.push_back(PrivatePtr.getPointer());
        ParamTypes.push_back(PrivatePtr.getType());
      }
      for (const Expr *E : Data.FirstprivateVars) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
        RawAddress PrivatePtr =
            CGF.CreateMemTemp(CGF.getContext().getPointerType(E->getType()),
                              ".firstpriv.ptr.addr");
        PrivatePtrs.emplace_back(VD, PrivatePtr);
        FirstprivatePtrs.emplace_back(VD, PrivatePtr);
        CallArgs.push_back(PrivatePtr.getPointer());
        ParamTypes.push_back(PrivatePtr.getType());
      }
      for (const Expr *E : Data.LastprivateVars) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
        RawAddress PrivatePtr =
            CGF.CreateMemTemp(CGF.getContext().getPointerType(E->getType()),
                              ".lastpriv.ptr.addr");
        PrivatePtrs.emplace_back(VD, PrivatePtr);
        CallArgs.push_back(PrivatePtr.getPointer());
        ParamTypes.push_back(PrivatePtr.getType());
      }
      for (const VarDecl *VD : Data.PrivateLocals) {
        QualType Ty = VD->getType().getNonReferenceType();
        if (VD->getType()->isLValueReferenceType())
          Ty = CGF.getContext().getPointerType(Ty);
        if (isAllocatableDecl(VD))
          Ty = CGF.getContext().getPointerType(Ty);
        RawAddress PrivatePtr = CGF.CreateMemTemp(
            CGF.getContext().getPointerType(Ty), ".local.ptr.addr");
        auto Result = UntiedLocalVars.insert(
            std::make_pair(VD, std::make_pair(PrivatePtr, Address::invalid())));
        // If key exists update in place.
        if (Result.second == false)
          *Result.first = std::make_pair(
              VD, std::make_pair(PrivatePtr, Address::invalid()));
        CallArgs.push_back(PrivatePtr.getPointer());
        ParamTypes.push_back(PrivatePtr.getType());
      }
      auto *CopyFnTy = llvm::FunctionType::get(CGF.Builder.getVoidTy(),
                                               ParamTypes, /*isVarArg=*/false);
      CGF.CGM.getOpenMPRuntime().emitOutlinedFunctionCall(
          CGF, S.getBeginLoc(), {CopyFnTy, CopyFn}, CallArgs);
      for (const auto &Pair : LastprivateDstsOrigs) {
        const auto *OrigVD = cast<VarDecl>(Pair.second->getDecl());
        DeclRefExpr DRE(CGF.getContext(), const_cast<VarDecl *>(OrigVD),
                        /*RefersToEnclosingVariableOrCapture=*/
                        CGF.CapturedStmtInfo->lookup(OrigVD) != nullptr,
                        Pair.second->getType(), VK_LValue,
                        Pair.second->getExprLoc());
        Scope.addPrivate(Pair.first, CGF.EmitLValue(&DRE).getAddress());
      }
      for (const auto &Pair : PrivatePtrs) {
        Address Replacement = Address(
            CGF.Builder.CreateLoad(Pair.second),
            CGF.ConvertTypeForMem(Pair.first->getType().getNonReferenceType()),
            CGF.getContext().getDeclAlign(Pair.first));
        Scope.addPrivate(Pair.first, Replacement);
        if (auto *DI = CGF.getDebugInfo())
          if (CGF.CGM.getCodeGenOpts().hasReducedDebugInfo())
            (void)DI->EmitDeclareOfAutoVariable(
                Pair.first, Pair.second.getBasePointer(), CGF.Builder,
                /*UsePointerValue*/ true);
      }
      // Adjust mapping for internal locals by mapping actual memory instead of
      // a pointer to this memory.
      for (auto &Pair : UntiedLocalVars) {
        QualType VDType = Pair.first->getType().getNonReferenceType();
        if (Pair.first->getType()->isLValueReferenceType())
          VDType = CGF.getContext().getPointerType(VDType);
        if (isAllocatableDecl(Pair.first)) {
          llvm::Value *Ptr = CGF.Builder.CreateLoad(Pair.second.first);
          Address Replacement(
              Ptr,
              CGF.ConvertTypeForMem(CGF.getContext().getPointerType(VDType)),
              CGF.getPointerAlign());
          Pair.second.first = Replacement;
          Ptr = CGF.Builder.CreateLoad(Replacement);
          Replacement = Address(Ptr, CGF.ConvertTypeForMem(VDType),
                                CGF.getContext().getDeclAlign(Pair.first));
          Pair.second.second = Replacement;
        } else {
          llvm::Value *Ptr = CGF.Builder.CreateLoad(Pair.second.first);
          Address Replacement(Ptr, CGF.ConvertTypeForMem(VDType),
                              CGF.getContext().getDeclAlign(Pair.first));
          Pair.second.first = Replacement;
        }
      }
    }
    if (Data.Reductions) {
      OMPPrivateScope FirstprivateScope(CGF);
      for (const auto &Pair : FirstprivatePtrs) {
        Address Replacement(
            CGF.Builder.CreateLoad(Pair.second),
            CGF.ConvertTypeForMem(Pair.first->getType().getNonReferenceType()),
            CGF.getContext().getDeclAlign(Pair.first));
        FirstprivateScope.addPrivate(Pair.first, Replacement);
      }
      (void)FirstprivateScope.Privatize();
      OMPLexicalScope LexScope(CGF, S, CapturedRegion);
      ReductionCodeGen RedCG(Data.ReductionVars, Data.ReductionVars,
                             Data.ReductionCopies, Data.ReductionOps);
      llvm::Value *ReductionsPtr = CGF.Builder.CreateLoad(
          CGF.GetAddrOfLocalVar(CS->getCapturedDecl()->getParam(9)));
      for (unsigned Cnt = 0, E = Data.ReductionVars.size(); Cnt < E; ++Cnt) {
        RedCG.emitSharedOrigLValue(CGF, Cnt);
        RedCG.emitAggregateType(CGF, Cnt);
        // FIXME: This must removed once the runtime library is fixed.
        // Emit required threadprivate variables for
        // initializer/combiner/finalizer.
        CGF.CGM.getOpenMPRuntime().emitTaskReductionFixups(CGF, S.getBeginLoc(),
                                                           RedCG, Cnt);
        Address Replacement = CGF.CGM.getOpenMPRuntime().getTaskReductionItem(
            CGF, S.getBeginLoc(), ReductionsPtr, RedCG.getSharedLValue(Cnt));
        Replacement = Address(
            CGF.EmitScalarConversion(Replacement.emitRawPointer(CGF),
                                     CGF.getContext().VoidPtrTy,
                                     CGF.getContext().getPointerType(
                                         Data.ReductionCopies[Cnt]->getType()),
                                     Data.ReductionCopies[Cnt]->getExprLoc()),
            CGF.ConvertTypeForMem(Data.ReductionCopies[Cnt]->getType()),
            Replacement.getAlignment());
        Replacement = RedCG.adjustPrivateAddress(CGF, Cnt, Replacement);
        Scope.addPrivate(RedCG.getBaseDecl(Cnt), Replacement);
      }
    }
    // Privatize all private variables except for in_reduction items.
    (void)Scope.Privatize();
    SmallVector<const Expr *, 4> InRedVars;
    SmallVector<const Expr *, 4> InRedPrivs;
    SmallVector<const Expr *, 4> InRedOps;
    SmallVector<const Expr *, 4> TaskgroupDescriptors;
    for (const auto *C : S.getClausesOfKind<OMPInReductionClause>()) {
      auto IPriv = C->privates().begin();
      auto IRed = C->reduction_ops().begin();
      auto ITD = C->taskgroup_descriptors().begin();
      for (const Expr *Ref : C->varlists()) {
        InRedVars.emplace_back(Ref);
        InRedPrivs.emplace_back(*IPriv);
        InRedOps.emplace_back(*IRed);
        TaskgroupDescriptors.emplace_back(*ITD);
        std::advance(IPriv, 1);
        std::advance(IRed, 1);
        std::advance(ITD, 1);
      }
    }
    // Privatize in_reduction items here, because taskgroup descriptors must be
    // privatized earlier.
    OMPPrivateScope InRedScope(CGF);
    if (!InRedVars.empty()) {
      ReductionCodeGen RedCG(InRedVars, InRedVars, InRedPrivs, InRedOps);
      for (unsigned Cnt = 0, E = InRedVars.size(); Cnt < E; ++Cnt) {
        RedCG.emitSharedOrigLValue(CGF, Cnt);
        RedCG.emitAggregateType(CGF, Cnt);
        // The taskgroup descriptor variable is always implicit firstprivate and
        // privatized already during processing of the firstprivates.
        // FIXME: This must removed once the runtime library is fixed.
        // Emit required threadprivate variables for
        // initializer/combiner/finalizer.
        CGF.CGM.getOpenMPRuntime().emitTaskReductionFixups(CGF, S.getBeginLoc(),
                                                           RedCG, Cnt);
        llvm::Value *ReductionsPtr;
        if (const Expr *TRExpr = TaskgroupDescriptors[Cnt]) {
          ReductionsPtr = CGF.EmitLoadOfScalar(CGF.EmitLValue(TRExpr),
                                               TRExpr->getExprLoc());
        } else {
          ReductionsPtr = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
        }
        Address Replacement = CGF.CGM.getOpenMPRuntime().getTaskReductionItem(
            CGF, S.getBeginLoc(), ReductionsPtr, RedCG.getSharedLValue(Cnt));
        Replacement = Address(
            CGF.EmitScalarConversion(
                Replacement.emitRawPointer(CGF), CGF.getContext().VoidPtrTy,
                CGF.getContext().getPointerType(InRedPrivs[Cnt]->getType()),
                InRedPrivs[Cnt]->getExprLoc()),
            CGF.ConvertTypeForMem(InRedPrivs[Cnt]->getType()),
            Replacement.getAlignment());
        Replacement = RedCG.adjustPrivateAddress(CGF, Cnt, Replacement);
        InRedScope.addPrivate(RedCG.getBaseDecl(Cnt), Replacement);
      }
    }
    (void)InRedScope.Privatize();

    CGOpenMPRuntime::UntiedTaskLocalDeclsRAII LocalVarsScope(CGF,
                                                             UntiedLocalVars);
    Action.Enter(CGF);
    BodyGen(CGF);
  };
  llvm::Function *OutlinedFn = CGM.getOpenMPRuntime().emitTaskOutlinedFunction(
      S, *I, *PartId, *TaskT, S.getDirectiveKind(), CodeGen, Data.Tied,
      Data.NumberOfParts);
  OMPLexicalScope Scope(*this, S, std::nullopt,
                        !isOpenMPParallelDirective(S.getDirectiveKind()) &&
                            !isOpenMPSimdDirective(S.getDirectiveKind()));
  TaskGen(*this, OutlinedFn, Data);
}

static ImplicitParamDecl *
createImplicitFirstprivateForType(ASTContext &C, OMPTaskDataTy &Data,
                                  QualType Ty, CapturedDecl *CD,
                                  SourceLocation Loc) {
  auto *OrigVD = ImplicitParamDecl::Create(C, CD, Loc, /*Id=*/nullptr, Ty,
                                           ImplicitParamKind::Other);
  auto *OrigRef = DeclRefExpr::Create(
      C, NestedNameSpecifierLoc(), SourceLocation(), OrigVD,
      /*RefersToEnclosingVariableOrCapture=*/false, Loc, Ty, VK_LValue);
  auto *PrivateVD = ImplicitParamDecl::Create(C, CD, Loc, /*Id=*/nullptr, Ty,
                                              ImplicitParamKind::Other);
  auto *PrivateRef = DeclRefExpr::Create(
      C, NestedNameSpecifierLoc(), SourceLocation(), PrivateVD,
      /*RefersToEnclosingVariableOrCapture=*/false, Loc, Ty, VK_LValue);
  QualType ElemType = C.getBaseElementType(Ty);
  auto *InitVD = ImplicitParamDecl::Create(C, CD, Loc, /*Id=*/nullptr, ElemType,
                                           ImplicitParamKind::Other);
  auto *InitRef = DeclRefExpr::Create(
      C, NestedNameSpecifierLoc(), SourceLocation(), InitVD,
      /*RefersToEnclosingVariableOrCapture=*/false, Loc, ElemType, VK_LValue);
  PrivateVD->setInitStyle(VarDecl::CInit);
  PrivateVD->setInit(ImplicitCastExpr::Create(C, ElemType, CK_LValueToRValue,
                                              InitRef, /*BasePath=*/nullptr,
                                              VK_PRValue, FPOptionsOverride()));
  Data.FirstprivateVars.emplace_back(OrigRef);
  Data.FirstprivateCopies.emplace_back(PrivateRef);
  Data.FirstprivateInits.emplace_back(InitRef);
  return OrigVD;
}

void CodeGenFunction::EmitOMPTargetTaskBasedDirective(
    const OMPExecutableDirective &S, const RegionCodeGenTy &BodyGen,
    OMPTargetDataInfo &InputInfo) {
  // Emit outlined function for task construct.
  const CapturedStmt *CS = S.getCapturedStmt(OMPD_task);
  Address CapturedStruct = GenerateCapturedStmtArgument(*CS);
  QualType SharedsTy = getContext().getRecordType(CS->getCapturedRecordDecl());
  auto I = CS->getCapturedDecl()->param_begin();
  auto PartId = std::next(I);
  auto TaskT = std::next(I, 4);
  OMPTaskDataTy Data;
  // The task is not final.
  Data.Final.setInt(/*IntVal=*/false);
  // Get list of firstprivate variables.
  for (const auto *C : S.getClausesOfKind<OMPFirstprivateClause>()) {
    auto IRef = C->varlist_begin();
    auto IElemInitRef = C->inits().begin();
    for (auto *IInit : C->private_copies()) {
      Data.FirstprivateVars.push_back(*IRef);
      Data.FirstprivateCopies.push_back(IInit);
      Data.FirstprivateInits.push_back(*IElemInitRef);
      ++IRef;
      ++IElemInitRef;
    }
  }
  SmallVector<const Expr *, 4> LHSs;
  SmallVector<const Expr *, 4> RHSs;
  for (const auto *C : S.getClausesOfKind<OMPInReductionClause>()) {
    Data.ReductionVars.append(C->varlist_begin(), C->varlist_end());
    Data.ReductionOrigs.append(C->varlist_begin(), C->varlist_end());
    Data.ReductionCopies.append(C->privates().begin(), C->privates().end());
    Data.ReductionOps.append(C->reduction_ops().begin(),
                             C->reduction_ops().end());
    LHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
    RHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
  }
  OMPPrivateScope TargetScope(*this);
  VarDecl *BPVD = nullptr;
  VarDecl *PVD = nullptr;
  VarDecl *SVD = nullptr;
  VarDecl *MVD = nullptr;
  if (InputInfo.NumberOfTargetItems > 0) {
    auto *CD = CapturedDecl::Create(
        getContext(), getContext().getTranslationUnitDecl(), /*NumParams=*/0);
    llvm::APInt ArrSize(/*numBits=*/32, InputInfo.NumberOfTargetItems);
    QualType BaseAndPointerAndMapperType = getContext().getConstantArrayType(
        getContext().VoidPtrTy, ArrSize, nullptr, ArraySizeModifier::Normal,
        /*IndexTypeQuals=*/0);
    BPVD = createImplicitFirstprivateForType(
        getContext(), Data, BaseAndPointerAndMapperType, CD, S.getBeginLoc());
    PVD = createImplicitFirstprivateForType(
        getContext(), Data, BaseAndPointerAndMapperType, CD, S.getBeginLoc());
    QualType SizesType = getContext().getConstantArrayType(
        getContext().getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/1),
        ArrSize, nullptr, ArraySizeModifier::Normal,
        /*IndexTypeQuals=*/0);
    SVD = createImplicitFirstprivateForType(getContext(), Data, SizesType, CD,
                                            S.getBeginLoc());
    TargetScope.addPrivate(BPVD, InputInfo.BasePointersArray);
    TargetScope.addPrivate(PVD, InputInfo.PointersArray);
    TargetScope.addPrivate(SVD, InputInfo.SizesArray);
    // If there is no user-defined mapper, the mapper array will be nullptr. In
    // this case, we don't need to privatize it.
    if (!isa_and_nonnull<llvm::ConstantPointerNull>(
            InputInfo.MappersArray.emitRawPointer(*this))) {
      MVD = createImplicitFirstprivateForType(
          getContext(), Data, BaseAndPointerAndMapperType, CD, S.getBeginLoc());
      TargetScope.addPrivate(MVD, InputInfo.MappersArray);
    }
  }
  (void)TargetScope.Privatize();
  buildDependences(S, Data);
  auto &&CodeGen = [&Data, &S, CS, &BodyGen, BPVD, PVD, SVD, MVD,
                    &InputInfo](CodeGenFunction &CGF, PrePostActionTy &Action) {
    // Set proper addresses for generated private copies.
    OMPPrivateScope Scope(CGF);
    if (!Data.FirstprivateVars.empty()) {
      enum { PrivatesParam = 2, CopyFnParam = 3 };
      llvm::Value *CopyFn = CGF.Builder.CreateLoad(
          CGF.GetAddrOfLocalVar(CS->getCapturedDecl()->getParam(CopyFnParam)));
      llvm::Value *PrivatesPtr = CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(
          CS->getCapturedDecl()->getParam(PrivatesParam)));
      // Map privates.
      llvm::SmallVector<std::pair<const VarDecl *, Address>, 16> PrivatePtrs;
      llvm::SmallVector<llvm::Value *, 16> CallArgs;
      llvm::SmallVector<llvm::Type *, 4> ParamTypes;
      CallArgs.push_back(PrivatesPtr);
      ParamTypes.push_back(PrivatesPtr->getType());
      for (const Expr *E : Data.FirstprivateVars) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
        RawAddress PrivatePtr =
            CGF.CreateMemTemp(CGF.getContext().getPointerType(E->getType()),
                              ".firstpriv.ptr.addr");
        PrivatePtrs.emplace_back(VD, PrivatePtr);
        CallArgs.push_back(PrivatePtr.getPointer());
        ParamTypes.push_back(PrivatePtr.getType());
      }
      auto *CopyFnTy = llvm::FunctionType::get(CGF.Builder.getVoidTy(),
                                               ParamTypes, /*isVarArg=*/false);
      CGF.CGM.getOpenMPRuntime().emitOutlinedFunctionCall(
          CGF, S.getBeginLoc(), {CopyFnTy, CopyFn}, CallArgs);
      for (const auto &Pair : PrivatePtrs) {
        Address Replacement(
            CGF.Builder.CreateLoad(Pair.second),
            CGF.ConvertTypeForMem(Pair.first->getType().getNonReferenceType()),
            CGF.getContext().getDeclAlign(Pair.first));
        Scope.addPrivate(Pair.first, Replacement);
      }
    }
    CGF.processInReduction(S, Data, CGF, CS, Scope);
    if (InputInfo.NumberOfTargetItems > 0) {
      InputInfo.BasePointersArray = CGF.Builder.CreateConstArrayGEP(
          CGF.GetAddrOfLocalVar(BPVD), /*Index=*/0);
      InputInfo.PointersArray = CGF.Builder.CreateConstArrayGEP(
          CGF.GetAddrOfLocalVar(PVD), /*Index=*/0);
      InputInfo.SizesArray = CGF.Builder.CreateConstArrayGEP(
          CGF.GetAddrOfLocalVar(SVD), /*Index=*/0);
      // If MVD is nullptr, the mapper array is not privatized
      if (MVD)
        InputInfo.MappersArray = CGF.Builder.CreateConstArrayGEP(
            CGF.GetAddrOfLocalVar(MVD), /*Index=*/0);
    }

    Action.Enter(CGF);
    OMPLexicalScope LexScope(CGF, S, OMPD_task, /*EmitPreInitStmt=*/false);
    auto *TL = S.getSingleClause<OMPThreadLimitClause>();
    if (CGF.CGM.getLangOpts().OpenMP >= 51 &&
        needsTaskBasedThreadLimit(S.getDirectiveKind()) && TL) {
      // Emit __kmpc_set_thread_limit() to set the thread_limit for the task
      // enclosing this target region. This will indirectly set the thread_limit
      // for every applicable construct within target region.
      CGF.CGM.getOpenMPRuntime().emitThreadLimitClause(
          CGF, TL->getThreadLimit(), S.getBeginLoc());
    }
    BodyGen(CGF);
  };
  llvm::Function *OutlinedFn = CGM.getOpenMPRuntime().emitTaskOutlinedFunction(
      S, *I, *PartId, *TaskT, S.getDirectiveKind(), CodeGen, /*Tied=*/true,
      Data.NumberOfParts);
  llvm::APInt TrueOrFalse(32, S.hasClausesOfKind<OMPNowaitClause>() ? 1 : 0);
  IntegerLiteral IfCond(getContext(), TrueOrFalse,
                        getContext().getIntTypeForBitwidth(32, /*Signed=*/0),
                        SourceLocation());
  CGM.getOpenMPRuntime().emitTaskCall(*this, S.getBeginLoc(), S, OutlinedFn,
                                      SharedsTy, CapturedStruct, &IfCond, Data);
}

void CodeGenFunction::processInReduction(const OMPExecutableDirective &S,
                                         OMPTaskDataTy &Data,
                                         CodeGenFunction &CGF,
                                         const CapturedStmt *CS,
                                         OMPPrivateScope &Scope) {
  if (Data.Reductions) {
    OpenMPDirectiveKind CapturedRegion = S.getDirectiveKind();
    OMPLexicalScope LexScope(CGF, S, CapturedRegion);
    ReductionCodeGen RedCG(Data.ReductionVars, Data.ReductionVars,
                           Data.ReductionCopies, Data.ReductionOps);
    llvm::Value *ReductionsPtr = CGF.Builder.CreateLoad(
        CGF.GetAddrOfLocalVar(CS->getCapturedDecl()->getParam(4)));
    for (unsigned Cnt = 0, E = Data.ReductionVars.size(); Cnt < E; ++Cnt) {
      RedCG.emitSharedOrigLValue(CGF, Cnt);
      RedCG.emitAggregateType(CGF, Cnt);
      // FIXME: This must removed once the runtime library is fixed.
      // Emit required threadprivate variables for
      // initializer/combiner/finalizer.
      CGF.CGM.getOpenMPRuntime().emitTaskReductionFixups(CGF, S.getBeginLoc(),
                                                         RedCG, Cnt);
      Address Replacement = CGF.CGM.getOpenMPRuntime().getTaskReductionItem(
          CGF, S.getBeginLoc(), ReductionsPtr, RedCG.getSharedLValue(Cnt));
      Replacement = Address(
          CGF.EmitScalarConversion(Replacement.emitRawPointer(CGF),
                                   CGF.getContext().VoidPtrTy,
                                   CGF.getContext().getPointerType(
                                       Data.ReductionCopies[Cnt]->getType()),
                                   Data.ReductionCopies[Cnt]->getExprLoc()),
          CGF.ConvertTypeForMem(Data.ReductionCopies[Cnt]->getType()),
          Replacement.getAlignment());
      Replacement = RedCG.adjustPrivateAddress(CGF, Cnt, Replacement);
      Scope.addPrivate(RedCG.getBaseDecl(Cnt), Replacement);
    }
  }
  (void)Scope.Privatize();
  SmallVector<const Expr *, 4> InRedVars;
  SmallVector<const Expr *, 4> InRedPrivs;
  SmallVector<const Expr *, 4> InRedOps;
  SmallVector<const Expr *, 4> TaskgroupDescriptors;
  for (const auto *C : S.getClausesOfKind<OMPInReductionClause>()) {
    auto IPriv = C->privates().begin();
    auto IRed = C->reduction_ops().begin();
    auto ITD = C->taskgroup_descriptors().begin();
    for (const Expr *Ref : C->varlists()) {
      InRedVars.emplace_back(Ref);
      InRedPrivs.emplace_back(*IPriv);
      InRedOps.emplace_back(*IRed);
      TaskgroupDescriptors.emplace_back(*ITD);
      std::advance(IPriv, 1);
      std::advance(IRed, 1);
      std::advance(ITD, 1);
    }
  }
  OMPPrivateScope InRedScope(CGF);
  if (!InRedVars.empty()) {
    ReductionCodeGen RedCG(InRedVars, InRedVars, InRedPrivs, InRedOps);
    for (unsigned Cnt = 0, E = InRedVars.size(); Cnt < E; ++Cnt) {
      RedCG.emitSharedOrigLValue(CGF, Cnt);
      RedCG.emitAggregateType(CGF, Cnt);
      // FIXME: This must removed once the runtime library is fixed.
      // Emit required threadprivate variables for
      // initializer/combiner/finalizer.
      CGF.CGM.getOpenMPRuntime().emitTaskReductionFixups(CGF, S.getBeginLoc(),
                                                         RedCG, Cnt);
      llvm::Value *ReductionsPtr;
      if (const Expr *TRExpr = TaskgroupDescriptors[Cnt]) {
        ReductionsPtr =
            CGF.EmitLoadOfScalar(CGF.EmitLValue(TRExpr), TRExpr->getExprLoc());
      } else {
        ReductionsPtr = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
      }
      Address Replacement = CGF.CGM.getOpenMPRuntime().getTaskReductionItem(
          CGF, S.getBeginLoc(), ReductionsPtr, RedCG.getSharedLValue(Cnt));
      Replacement = Address(
          CGF.EmitScalarConversion(
              Replacement.emitRawPointer(CGF), CGF.getContext().VoidPtrTy,
              CGF.getContext().getPointerType(InRedPrivs[Cnt]->getType()),
              InRedPrivs[Cnt]->getExprLoc()),
          CGF.ConvertTypeForMem(InRedPrivs[Cnt]->getType()),
          Replacement.getAlignment());
      Replacement = RedCG.adjustPrivateAddress(CGF, Cnt, Replacement);
      InRedScope.addPrivate(RedCG.getBaseDecl(Cnt), Replacement);
    }
  }
  (void)InRedScope.Privatize();
}

void CodeGenFunction::EmitOMPTaskDirective(const OMPTaskDirective &S) {
  // Emit outlined function for task construct.
  const CapturedStmt *CS = S.getCapturedStmt(OMPD_task);
  Address CapturedStruct = GenerateCapturedStmtArgument(*CS);
  QualType SharedsTy = getContext().getRecordType(CS->getCapturedRecordDecl());
  const Expr *IfCond = nullptr;
  for (const auto *C : S.getClausesOfKind<OMPIfClause>()) {
    if (C->getNameModifier() == OMPD_unknown ||
        C->getNameModifier() == OMPD_task) {
      IfCond = C->getCondition();
      break;
    }
  }

  OMPTaskDataTy Data;
  // Check if we should emit tied or untied task.
  Data.Tied = !S.getSingleClause<OMPUntiedClause>();
  auto &&BodyGen = [CS](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitStmt(CS->getCapturedStmt());
  };
  auto &&TaskGen = [&S, SharedsTy, CapturedStruct,
                    IfCond](CodeGenFunction &CGF, llvm::Function *OutlinedFn,
                            const OMPTaskDataTy &Data) {
    CGF.CGM.getOpenMPRuntime().emitTaskCall(CGF, S.getBeginLoc(), S, OutlinedFn,
                                            SharedsTy, CapturedStruct, IfCond,
                                            Data);
  };
  auto LPCRegion =
      CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
  EmitOMPTaskBasedDirective(S, OMPD_task, BodyGen, TaskGen, Data);
}

void CodeGenFunction::EmitOMPTaskyieldDirective(
    const OMPTaskyieldDirective &S) {
  CGM.getOpenMPRuntime().emitTaskyieldCall(*this, S.getBeginLoc());
}

void CodeGenFunction::EmitOMPErrorDirective(const OMPErrorDirective &S) {
  const OMPMessageClause *MC = S.getSingleClause<OMPMessageClause>();
  Expr *ME = MC ? MC->getMessageString() : nullptr;
  const OMPSeverityClause *SC = S.getSingleClause<OMPSeverityClause>();
  bool IsFatal = false;
  if (!SC || SC->getSeverityKind() == OMPC_SEVERITY_fatal)
    IsFatal = true;
  CGM.getOpenMPRuntime().emitErrorCall(*this, S.getBeginLoc(), ME, IsFatal);
}

void CodeGenFunction::EmitOMPBarrierDirective(const OMPBarrierDirective &S) {
  CGM.getOpenMPRuntime().emitBarrierCall(*this, S.getBeginLoc(), OMPD_barrier);
}

void CodeGenFunction::EmitOMPTaskwaitDirective(const OMPTaskwaitDirective &S) {
  OMPTaskDataTy Data;
  // Build list of dependences
  buildDependences(S, Data);
  Data.HasNowaitClause = S.hasClausesOfKind<OMPNowaitClause>();
  CGM.getOpenMPRuntime().emitTaskwaitCall(*this, S.getBeginLoc(), Data);
}

bool isSupportedByOpenMPIRBuilder(const OMPTaskgroupDirective &T) {
  return T.clauses().empty();
}

void CodeGenFunction::EmitOMPTaskgroupDirective(
    const OMPTaskgroupDirective &S) {
  OMPLexicalScope Scope(*this, S, OMPD_unknown);
  if (CGM.getLangOpts().OpenMPIRBuilder && isSupportedByOpenMPIRBuilder(S)) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;
    InsertPointTy AllocaIP(AllocaInsertPt->getParent(),
                           AllocaInsertPt->getIterator());

    auto BodyGenCB = [&, this](InsertPointTy AllocaIP,
                               InsertPointTy CodeGenIP) {
      Builder.restoreIP(CodeGenIP);
      EmitStmt(S.getInnermostCapturedStmt()->getCapturedStmt());
    };
    CodeGenFunction::CGCapturedStmtInfo CapStmtInfo;
    if (!CapturedStmtInfo)
      CapturedStmtInfo = &CapStmtInfo;
    Builder.restoreIP(OMPBuilder.createTaskgroup(Builder, AllocaIP, BodyGenCB));
    return;
  }
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    if (const Expr *E = S.getReductionRef()) {
      SmallVector<const Expr *, 4> LHSs;
      SmallVector<const Expr *, 4> RHSs;
      OMPTaskDataTy Data;
      for (const auto *C : S.getClausesOfKind<OMPTaskReductionClause>()) {
        Data.ReductionVars.append(C->varlist_begin(), C->varlist_end());
        Data.ReductionOrigs.append(C->varlist_begin(), C->varlist_end());
        Data.ReductionCopies.append(C->privates().begin(), C->privates().end());
        Data.ReductionOps.append(C->reduction_ops().begin(),
                                 C->reduction_ops().end());
        LHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
        RHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
      }
      llvm::Value *ReductionDesc =
          CGF.CGM.getOpenMPRuntime().emitTaskReductionInit(CGF, S.getBeginLoc(),
                                                           LHSs, RHSs, Data);
      const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
      CGF.EmitVarDecl(*VD);
      CGF.EmitStoreOfScalar(ReductionDesc, CGF.GetAddrOfLocalVar(VD),
                            /*Volatile=*/false, E->getType());
    }
    CGF.EmitStmt(S.getInnermostCapturedStmt()->getCapturedStmt());
  };
  CGM.getOpenMPRuntime().emitTaskgroupRegion(*this, CodeGen, S.getBeginLoc());
}

void CodeGenFunction::EmitOMPFlushDirective(const OMPFlushDirective &S) {
  llvm::AtomicOrdering AO = S.getSingleClause<OMPFlushClause>()
                                ? llvm::AtomicOrdering::NotAtomic
                                : llvm::AtomicOrdering::AcquireRelease;
  CGM.getOpenMPRuntime().emitFlush(
      *this,
      [&S]() -> ArrayRef<const Expr *> {
        if (const auto *FlushClause = S.getSingleClause<OMPFlushClause>())
          return llvm::ArrayRef(FlushClause->varlist_begin(),
                                FlushClause->varlist_end());
        return std::nullopt;
      }(),
      S.getBeginLoc(), AO);
}

void CodeGenFunction::EmitOMPDepobjDirective(const OMPDepobjDirective &S) {
  const auto *DO = S.getSingleClause<OMPDepobjClause>();
  LValue DOLVal = EmitLValue(DO->getDepobj());
  if (const auto *DC = S.getSingleClause<OMPDependClause>()) {
    OMPTaskDataTy::DependData Dependencies(DC->getDependencyKind(),
                                           DC->getModifier());
    Dependencies.DepExprs.append(DC->varlist_begin(), DC->varlist_end());
    Address DepAddr = CGM.getOpenMPRuntime().emitDepobjDependClause(
        *this, Dependencies, DC->getBeginLoc());
    EmitStoreOfScalar(DepAddr.emitRawPointer(*this), DOLVal);
    return;
  }
  if (const auto *DC = S.getSingleClause<OMPDestroyClause>()) {
    CGM.getOpenMPRuntime().emitDestroyClause(*this, DOLVal, DC->getBeginLoc());
    return;
  }
  if (const auto *UC = S.getSingleClause<OMPUpdateClause>()) {
    CGM.getOpenMPRuntime().emitUpdateClause(
        *this, DOLVal, UC->getDependencyKind(), UC->getBeginLoc());
    return;
  }
}

void CodeGenFunction::EmitOMPScanDirective(const OMPScanDirective &S) {
  if (!OMPParentLoopDirectiveForScan)
    return;
  const OMPExecutableDirective &ParentDir = *OMPParentLoopDirectiveForScan;
  bool IsInclusive = S.hasClausesOfKind<OMPInclusiveClause>();
  SmallVector<const Expr *, 4> Shareds;
  SmallVector<const Expr *, 4> Privates;
  SmallVector<const Expr *, 4> LHSs;
  SmallVector<const Expr *, 4> RHSs;
  SmallVector<const Expr *, 4> ReductionOps;
  SmallVector<const Expr *, 4> CopyOps;
  SmallVector<const Expr *, 4> CopyArrayTemps;
  SmallVector<const Expr *, 4> CopyArrayElems;
  for (const auto *C : ParentDir.getClausesOfKind<OMPReductionClause>()) {
    if (C->getModifier() != OMPC_REDUCTION_inscan)
      continue;
    Shareds.append(C->varlist_begin(), C->varlist_end());
    Privates.append(C->privates().begin(), C->privates().end());
    LHSs.append(C->lhs_exprs().begin(), C->lhs_exprs().end());
    RHSs.append(C->rhs_exprs().begin(), C->rhs_exprs().end());
    ReductionOps.append(C->reduction_ops().begin(), C->reduction_ops().end());
    CopyOps.append(C->copy_ops().begin(), C->copy_ops().end());
    CopyArrayTemps.append(C->copy_array_temps().begin(),
                          C->copy_array_temps().end());
    CopyArrayElems.append(C->copy_array_elems().begin(),
                          C->copy_array_elems().end());
  }
  if (ParentDir.getDirectiveKind() == OMPD_simd ||
      (getLangOpts().OpenMPSimd &&
       isOpenMPSimdDirective(ParentDir.getDirectiveKind()))) {
    // For simd directive and simd-based directives in simd only mode, use the
    // following codegen:
    // int x = 0;
    // #pragma omp simd reduction(inscan, +: x)
    // for (..) {
    //   <first part>
    //   #pragma omp scan inclusive(x)
    //   <second part>
    //  }
    // is transformed to:
    // int x = 0;
    // for (..) {
    //   int x_priv = 0;
    //   <first part>
    //   x = x_priv + x;
    //   x_priv = x;
    //   <second part>
    // }
    // and
    // int x = 0;
    // #pragma omp simd reduction(inscan, +: x)
    // for (..) {
    //   <first part>
    //   #pragma omp scan exclusive(x)
    //   <second part>
    // }
    // to
    // int x = 0;
    // for (..) {
    //   int x_priv = 0;
    //   <second part>
    //   int temp = x;
    //   x = x_priv + x;
    //   x_priv = temp;
    //   <first part>
    // }
    llvm::BasicBlock *OMPScanReduce = createBasicBlock("omp.inscan.reduce");
    EmitBranch(IsInclusive
                   ? OMPScanReduce
                   : BreakContinueStack.back().ContinueBlock.getBlock());
    EmitBlock(OMPScanDispatch);
    {
      // New scope for correct construction/destruction of temp variables for
      // exclusive scan.
      LexicalScope Scope(*this, S.getSourceRange());
      EmitBranch(IsInclusive ? OMPBeforeScanBlock : OMPAfterScanBlock);
      EmitBlock(OMPScanReduce);
      if (!IsInclusive) {
        // Create temp var and copy LHS value to this temp value.
        // TMP = LHS;
        for (unsigned I = 0, E = CopyArrayElems.size(); I < E; ++I) {
          const Expr *PrivateExpr = Privates[I];
          const Expr *TempExpr = CopyArrayTemps[I];
          EmitAutoVarDecl(
              *cast<VarDecl>(cast<DeclRefExpr>(TempExpr)->getDecl()));
          LValue DestLVal = EmitLValue(TempExpr);
          LValue SrcLVal = EmitLValue(LHSs[I]);
          EmitOMPCopy(PrivateExpr->getType(), DestLVal.getAddress(),
                      SrcLVal.getAddress(),
                      cast<VarDecl>(cast<DeclRefExpr>(LHSs[I])->getDecl()),
                      cast<VarDecl>(cast<DeclRefExpr>(RHSs[I])->getDecl()),
                      CopyOps[I]);
        }
      }
      CGM.getOpenMPRuntime().emitReduction(
          *this, ParentDir.getEndLoc(), Privates, LHSs, RHSs, ReductionOps,
          {/*WithNowait=*/true, /*SimpleReduction=*/true, OMPD_simd});
      for (unsigned I = 0, E = CopyArrayElems.size(); I < E; ++I) {
        const Expr *PrivateExpr = Privates[I];
        LValue DestLVal;
        LValue SrcLVal;
        if (IsInclusive) {
          DestLVal = EmitLValue(RHSs[I]);
          SrcLVal = EmitLValue(LHSs[I]);
        } else {
          const Expr *TempExpr = CopyArrayTemps[I];
          DestLVal = EmitLValue(RHSs[I]);
          SrcLVal = EmitLValue(TempExpr);
        }
        EmitOMPCopy(
            PrivateExpr->getType(), DestLVal.getAddress(), SrcLVal.getAddress(),
            cast<VarDecl>(cast<DeclRefExpr>(LHSs[I])->getDecl()),
            cast<VarDecl>(cast<DeclRefExpr>(RHSs[I])->getDecl()), CopyOps[I]);
      }
    }
    EmitBranch(IsInclusive ? OMPAfterScanBlock : OMPBeforeScanBlock);
    OMPScanExitBlock = IsInclusive
                           ? BreakContinueStack.back().ContinueBlock.getBlock()
                           : OMPScanReduce;
    EmitBlock(OMPAfterScanBlock);
    return;
  }
  if (!IsInclusive) {
    EmitBranch(BreakContinueStack.back().ContinueBlock.getBlock());
    EmitBlock(OMPScanExitBlock);
  }
  if (OMPFirstScanLoop) {
    // Emit buffer[i] = red; at the end of the input phase.
    const auto *IVExpr = cast<OMPLoopDirective>(ParentDir)
                             .getIterationVariable()
                             ->IgnoreParenImpCasts();
    LValue IdxLVal = EmitLValue(IVExpr);
    llvm::Value *IdxVal = EmitLoadOfScalar(IdxLVal, IVExpr->getExprLoc());
    IdxVal = Builder.CreateIntCast(IdxVal, SizeTy, /*isSigned=*/false);
    for (unsigned I = 0, E = CopyArrayElems.size(); I < E; ++I) {
      const Expr *PrivateExpr = Privates[I];
      const Expr *OrigExpr = Shareds[I];
      const Expr *CopyArrayElem = CopyArrayElems[I];
      OpaqueValueMapping IdxMapping(
          *this,
          cast<OpaqueValueExpr>(
              cast<ArraySubscriptExpr>(CopyArrayElem)->getIdx()),
          RValue::get(IdxVal));
      LValue DestLVal = EmitLValue(CopyArrayElem);
      LValue SrcLVal = EmitLValue(OrigExpr);
      EmitOMPCopy(
          PrivateExpr->getType(), DestLVal.getAddress(), SrcLVal.getAddress(),
          cast<VarDecl>(cast<DeclRefExpr>(LHSs[I])->getDecl()),
          cast<VarDecl>(cast<DeclRefExpr>(RHSs[I])->getDecl()), CopyOps[I]);
    }
  }
  EmitBranch(BreakContinueStack.back().ContinueBlock.getBlock());
  if (IsInclusive) {
    EmitBlock(OMPScanExitBlock);
    EmitBranch(BreakContinueStack.back().ContinueBlock.getBlock());
  }
  EmitBlock(OMPScanDispatch);
  if (!OMPFirstScanLoop) {
    // Emit red = buffer[i]; at the entrance to the scan phase.
    const auto *IVExpr = cast<OMPLoopDirective>(ParentDir)
                             .getIterationVariable()
                             ->IgnoreParenImpCasts();
    LValue IdxLVal = EmitLValue(IVExpr);
    llvm::Value *IdxVal = EmitLoadOfScalar(IdxLVal, IVExpr->getExprLoc());
    IdxVal = Builder.CreateIntCast(IdxVal, SizeTy, /*isSigned=*/false);
    llvm::BasicBlock *ExclusiveExitBB = nullptr;
    if (!IsInclusive) {
      llvm::BasicBlock *ContBB = createBasicBlock("omp.exclusive.dec");
      ExclusiveExitBB = createBasicBlock("omp.exclusive.copy.exit");
      llvm::Value *Cmp = Builder.CreateIsNull(IdxVal);
      Builder.CreateCondBr(Cmp, ExclusiveExitBB, ContBB);
      EmitBlock(ContBB);
      // Use idx - 1 iteration for exclusive scan.
      IdxVal = Builder.CreateNUWSub(IdxVal, llvm::ConstantInt::get(SizeTy, 1));
    }
    for (unsigned I = 0, E = CopyArrayElems.size(); I < E; ++I) {
      const Expr *PrivateExpr = Privates[I];
      const Expr *OrigExpr = Shareds[I];
      const Expr *CopyArrayElem = CopyArrayElems[I];
      OpaqueValueMapping IdxMapping(
          *this,
          cast<OpaqueValueExpr>(
              cast<ArraySubscriptExpr>(CopyArrayElem)->getIdx()),
          RValue::get(IdxVal));
      LValue SrcLVal = EmitLValue(CopyArrayElem);
      LValue DestLVal = EmitLValue(OrigExpr);
      EmitOMPCopy(
          PrivateExpr->getType(), DestLVal.getAddress(), SrcLVal.getAddress(),
          cast<VarDecl>(cast<DeclRefExpr>(LHSs[I])->getDecl()),
          cast<VarDecl>(cast<DeclRefExpr>(RHSs[I])->getDecl()), CopyOps[I]);
    }
    if (!IsInclusive) {
      EmitBlock(ExclusiveExitBB);
    }
  }
  EmitBranch((OMPFirstScanLoop == IsInclusive) ? OMPBeforeScanBlock
                                               : OMPAfterScanBlock);
  EmitBlock(OMPAfterScanBlock);
}

void CodeGenFunction::EmitOMPDistributeLoop(const OMPLoopDirective &S,
                                            const CodeGenLoopTy &CodeGenLoop,
                                            Expr *IncExpr) {
  // Emit the loop iteration variable.
  const auto *IVExpr = cast<DeclRefExpr>(S.getIterationVariable());
  const auto *IVDecl = cast<VarDecl>(IVExpr->getDecl());
  EmitVarDecl(*IVDecl);

  // Emit the iterations count variable.
  // If it is not a variable, Sema decided to calculate iterations count on each
  // iteration (e.g., it is foldable into a constant).
  if (const auto *LIExpr = dyn_cast<DeclRefExpr>(S.getLastIteration())) {
    EmitVarDecl(*cast<VarDecl>(LIExpr->getDecl()));
    // Emit calculation of the iterations count.
    EmitIgnoredExpr(S.getCalcLastIteration());
  }

  CGOpenMPRuntime &RT = CGM.getOpenMPRuntime();

  bool HasLastprivateClause = false;
  // Check pre-condition.
  {
    OMPLoopScope PreInitScope(*this, S);
    // Skip the entire loop if we don't meet the precondition.
    // If the condition constant folds and can be elided, avoid emitting the
    // whole loop.
    bool CondConstant;
    llvm::BasicBlock *ContBlock = nullptr;
    if (ConstantFoldsToSimpleInteger(S.getPreCond(), CondConstant)) {
      if (!CondConstant)
        return;
    } else {
      llvm::BasicBlock *ThenBlock = createBasicBlock("omp.precond.then");
      ContBlock = createBasicBlock("omp.precond.end");
      emitPreCond(*this, S, S.getPreCond(), ThenBlock, ContBlock,
                  getProfileCount(&S));
      EmitBlock(ThenBlock);
      incrementProfileCounter(&S);
    }

    emitAlignedClause(*this, S);
    // Emit 'then' code.
    {
      // Emit helper vars inits.

      LValue LB = EmitOMPHelperVar(
          *this, cast<DeclRefExpr>(
                     (isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                          ? S.getCombinedLowerBoundVariable()
                          : S.getLowerBoundVariable())));
      LValue UB = EmitOMPHelperVar(
          *this, cast<DeclRefExpr>(
                     (isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                          ? S.getCombinedUpperBoundVariable()
                          : S.getUpperBoundVariable())));
      LValue ST =
          EmitOMPHelperVar(*this, cast<DeclRefExpr>(S.getStrideVariable()));
      LValue IL =
          EmitOMPHelperVar(*this, cast<DeclRefExpr>(S.getIsLastIterVariable()));

      OMPPrivateScope LoopScope(*this);
      if (EmitOMPFirstprivateClause(S, LoopScope)) {
        // Emit implicit barrier to synchronize threads and avoid data races
        // on initialization of firstprivate variables and post-update of
        // lastprivate variables.
        CGM.getOpenMPRuntime().emitBarrierCall(
            *this, S.getBeginLoc(), OMPD_unknown, /*EmitChecks=*/false,
            /*ForceSimpleCall=*/true);
      }
      EmitOMPPrivateClause(S, LoopScope);
      if (isOpenMPSimdDirective(S.getDirectiveKind()) &&
          !isOpenMPParallelDirective(S.getDirectiveKind()) &&
          !isOpenMPTeamsDirective(S.getDirectiveKind()))
        EmitOMPReductionClauseInit(S, LoopScope);
      HasLastprivateClause = EmitOMPLastprivateClauseInit(S, LoopScope);
      EmitOMPPrivateLoopCounters(S, LoopScope);
      (void)LoopScope.Privatize();
      if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()))
        CGM.getOpenMPRuntime().adjustTargetSpecificDataForLambdas(*this, S);

      // Detect the distribute schedule kind and chunk.
      llvm::Value *Chunk = nullptr;
      OpenMPDistScheduleClauseKind ScheduleKind = OMPC_DIST_SCHEDULE_unknown;
      if (const auto *C = S.getSingleClause<OMPDistScheduleClause>()) {
        ScheduleKind = C->getDistScheduleKind();
        if (const Expr *Ch = C->getChunkSize()) {
          Chunk = EmitScalarExpr(Ch);
          Chunk = EmitScalarConversion(Chunk, Ch->getType(),
                                       S.getIterationVariable()->getType(),
                                       S.getBeginLoc());
        }
      } else {
        // Default behaviour for dist_schedule clause.
        CGM.getOpenMPRuntime().getDefaultDistScheduleAndChunk(
            *this, S, ScheduleKind, Chunk);
      }
      const unsigned IVSize = getContext().getTypeSize(IVExpr->getType());
      const bool IVSigned = IVExpr->getType()->hasSignedIntegerRepresentation();

      // OpenMP [2.10.8, distribute Construct, Description]
      // If dist_schedule is specified, kind must be static. If specified,
      // iterations are divided into chunks of size chunk_size, chunks are
      // assigned to the teams of the league in a round-robin fashion in the
      // order of the team number. When no chunk_size is specified, the
      // iteration space is divided into chunks that are approximately equal
      // in size, and at most one chunk is distributed to each team of the
      // league. The size of the chunks is unspecified in this case.
      bool StaticChunked =
          RT.isStaticChunked(ScheduleKind, /* Chunked */ Chunk != nullptr) &&
          isOpenMPLoopBoundSharingDirective(S.getDirectiveKind());
      if (RT.isStaticNonchunked(ScheduleKind,
                                /* Chunked */ Chunk != nullptr) ||
          StaticChunked) {
        CGOpenMPRuntime::StaticRTInput StaticInit(
            IVSize, IVSigned, /* Ordered = */ false, IL.getAddress(),
            LB.getAddress(), UB.getAddress(), ST.getAddress(),
            StaticChunked ? Chunk : nullptr);
        RT.emitDistributeStaticInit(*this, S.getBeginLoc(), ScheduleKind,
                                    StaticInit);
        JumpDest LoopExit =
            getJumpDestInCurrentScope(createBasicBlock("omp.loop.exit"));
        // UB = min(UB, GlobalUB);
        EmitIgnoredExpr(isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                            ? S.getCombinedEnsureUpperBound()
                            : S.getEnsureUpperBound());
        // IV = LB;
        EmitIgnoredExpr(isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                            ? S.getCombinedInit()
                            : S.getInit());

        const Expr *Cond =
            isOpenMPLoopBoundSharingDirective(S.getDirectiveKind())
                ? S.getCombinedCond()
                : S.getCond();

        if (StaticChunked)
          Cond = S.getCombinedDistCond();

        // For static unchunked schedules generate:
        //
        //  1. For distribute alone, codegen
        //    while (idx <= UB) {
        //      BODY;
        //      ++idx;
        //    }
        //
        //  2. When combined with 'for' (e.g. as in 'distribute parallel for')
        //    while (idx <= UB) {
        //      <CodeGen rest of pragma>(LB, UB);
        //      idx += ST;
        //    }
        //
        // For static chunk one schedule generate:
        //
        // while (IV <= GlobalUB) {
        //   <CodeGen rest of pragma>(LB, UB);
        //   LB += ST;
        //   UB += ST;
        //   UB = min(UB, GlobalUB);
        //   IV = LB;
        // }
        //
        emitCommonSimdLoop(
            *this, S,
            [&S](CodeGenFunction &CGF, PrePostActionTy &) {
              if (isOpenMPSimdDirective(S.getDirectiveKind()))
                CGF.EmitOMPSimdInit(S);
            },
            [&S, &LoopScope, Cond, IncExpr, LoopExit, &CodeGenLoop,
             StaticChunked](CodeGenFunction &CGF, PrePostActionTy &) {
              CGF.EmitOMPInnerLoop(
                  S, LoopScope.requiresCleanups(), Cond, IncExpr,
                  [&S, LoopExit, &CodeGenLoop](CodeGenFunction &CGF) {
                    CodeGenLoop(CGF, S, LoopExit);
                  },
                  [&S, StaticChunked](CodeGenFunction &CGF) {
                    if (StaticChunked) {
                      CGF.EmitIgnoredExpr(S.getCombinedNextLowerBound());
                      CGF.EmitIgnoredExpr(S.getCombinedNextUpperBound());
                      CGF.EmitIgnoredExpr(S.getCombinedEnsureUpperBound());
                      CGF.EmitIgnoredExpr(S.getCombinedInit());
                    }
                  });
            });
        EmitBlock(LoopExit.getBlock());
        // Tell the runtime we are done.
        RT.emitForStaticFinish(*this, S.getEndLoc(), OMPD_distribute);
      } else {
        // Emit the outer loop, which requests its work chunk [LB..UB] from
        // runtime and runs the inner loop to process it.
        const OMPLoopArguments LoopArguments = {
            LB.getAddress(), UB.getAddress(), ST.getAddress(), IL.getAddress(),
            Chunk};
        EmitOMPDistributeOuterLoop(ScheduleKind, S, LoopScope, LoopArguments,
                                   CodeGenLoop);
      }
      if (isOpenMPSimdDirective(S.getDirectiveKind())) {
        EmitOMPSimdFinal(S, [IL, &S](CodeGenFunction &CGF) {
          return CGF.Builder.CreateIsNotNull(
              CGF.EmitLoadOfScalar(IL, S.getBeginLoc()));
        });
      }
      if (isOpenMPSimdDirective(S.getDirectiveKind()) &&
          !isOpenMPParallelDirective(S.getDirectiveKind()) &&
          !isOpenMPTeamsDirective(S.getDirectiveKind())) {
        EmitOMPReductionClauseFinal(S, OMPD_simd);
        // Emit post-update of the reduction variables if IsLastIter != 0.
        emitPostUpdateForReductionClause(
            *this, S, [IL, &S](CodeGenFunction &CGF) {
              return CGF.Builder.CreateIsNotNull(
                  CGF.EmitLoadOfScalar(IL, S.getBeginLoc()));
            });
      }
      // Emit final copy of the lastprivate variables if IsLastIter != 0.
      if (HasLastprivateClause) {
        EmitOMPLastprivateClauseFinal(
            S, /*NoFinals=*/false,
            Builder.CreateIsNotNull(EmitLoadOfScalar(IL, S.getBeginLoc())));
      }
    }

    // We're now done with the loop, so jump to the continuation block.
    if (ContBlock) {
      EmitBranch(ContBlock);
      EmitBlock(ContBlock, true);
    }
  }
}

void CodeGenFunction::EmitOMPDistributeDirective(
    const OMPDistributeDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };
  OMPLexicalScope Scope(*this, S, OMPD_unknown);
  CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_distribute, CodeGen);
}

static llvm::Function *emitOutlinedOrderedFunction(CodeGenModule &CGM,
                                                   const CapturedStmt *S,
                                                   SourceLocation Loc) {
  CodeGenFunction CGF(CGM, /*suppressNewContext=*/true);
  CodeGenFunction::CGCapturedStmtInfo CapStmtInfo;
  CGF.CapturedStmtInfo = &CapStmtInfo;
  llvm::Function *Fn = CGF.GenerateOpenMPCapturedStmtFunction(*S, Loc);
  Fn->setDoesNotRecurse();
  return Fn;
}

template <typename T>
static void emitRestoreIP(CodeGenFunction &CGF, const T *C,
                          llvm::OpenMPIRBuilder::InsertPointTy AllocaIP,
                          llvm::OpenMPIRBuilder &OMPBuilder) {

  unsigned NumLoops = C->getNumLoops();
  QualType Int64Ty = CGF.CGM.getContext().getIntTypeForBitwidth(
      /*DestWidth=*/64, /*Signed=*/1);
  llvm::SmallVector<llvm::Value *> StoreValues;
  for (unsigned I = 0; I < NumLoops; I++) {
    const Expr *CounterVal = C->getLoopData(I);
    assert(CounterVal);
    llvm::Value *StoreValue = CGF.EmitScalarConversion(
        CGF.EmitScalarExpr(CounterVal), CounterVal->getType(), Int64Ty,
        CounterVal->getExprLoc());
    StoreValues.emplace_back(StoreValue);
  }
  OMPDoacrossKind<T> ODK;
  bool IsDependSource = ODK.isSource(C);
  CGF.Builder.restoreIP(
      OMPBuilder.createOrderedDepend(CGF.Builder, AllocaIP, NumLoops,
                                     StoreValues, ".cnt.addr", IsDependSource));
}

void CodeGenFunction::EmitOMPOrderedDirective(const OMPOrderedDirective &S) {
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    using InsertPointTy = llvm::OpenMPIRBuilder::InsertPointTy;

    if (S.hasClausesOfKind<OMPDependClause>() ||
        S.hasClausesOfKind<OMPDoacrossClause>()) {
      // The ordered directive with depend clause.
      assert(!S.hasAssociatedStmt() && "No associated statement must be in "
                                       "ordered depend|doacross construct.");
      InsertPointTy AllocaIP(AllocaInsertPt->getParent(),
                             AllocaInsertPt->getIterator());
      for (const auto *DC : S.getClausesOfKind<OMPDependClause>())
        emitRestoreIP(*this, DC, AllocaIP, OMPBuilder);
      for (const auto *DC : S.getClausesOfKind<OMPDoacrossClause>())
        emitRestoreIP(*this, DC, AllocaIP, OMPBuilder);
    } else {
      // The ordered directive with threads or simd clause, or without clause.
      // Without clause, it behaves as if the threads clause is specified.
      const auto *C = S.getSingleClause<OMPSIMDClause>();

      auto FiniCB = [this](InsertPointTy IP) {
        OMPBuilderCBHelpers::FinalizeOMPRegion(*this, IP);
      };

      auto BodyGenCB = [&S, C, this](InsertPointTy AllocaIP,
                                     InsertPointTy CodeGenIP) {
        Builder.restoreIP(CodeGenIP);

        const CapturedStmt *CS = S.getInnermostCapturedStmt();
        if (C) {
          llvm::BasicBlock *FiniBB = splitBBWithSuffix(
              Builder, /*CreateBranch=*/false, ".ordered.after");
          llvm::SmallVector<llvm::Value *, 16> CapturedVars;
          GenerateOpenMPCapturedVars(*CS, CapturedVars);
          llvm::Function *OutlinedFn =
              emitOutlinedOrderedFunction(CGM, CS, S.getBeginLoc());
          assert(S.getBeginLoc().isValid() &&
                 "Outlined function call location must be valid.");
          ApplyDebugLocation::CreateDefaultArtificial(*this, S.getBeginLoc());
          OMPBuilderCBHelpers::EmitCaptureStmt(*this, CodeGenIP, *FiniBB,
                                               OutlinedFn, CapturedVars);
        } else {
          OMPBuilderCBHelpers::EmitOMPInlinedRegionBody(
              *this, CS->getCapturedStmt(), AllocaIP, CodeGenIP, "ordered");
        }
      };

      OMPLexicalScope Scope(*this, S, OMPD_unknown);
      Builder.restoreIP(
          OMPBuilder.createOrderedThreadsSimd(Builder, BodyGenCB, FiniCB, !C));
    }
    return;
  }

  if (S.hasClausesOfKind<OMPDependClause>()) {
    assert(!S.hasAssociatedStmt() &&
           "No associated statement must be in ordered depend construct.");
    for (const auto *DC : S.getClausesOfKind<OMPDependClause>())
      CGM.getOpenMPRuntime().emitDoacrossOrdered(*this, DC);
    return;
  }
  if (S.hasClausesOfKind<OMPDoacrossClause>()) {
    assert(!S.hasAssociatedStmt() &&
           "No associated statement must be in ordered doacross construct.");
    for (const auto *DC : S.getClausesOfKind<OMPDoacrossClause>())
      CGM.getOpenMPRuntime().emitDoacrossOrdered(*this, DC);
    return;
  }
  const auto *C = S.getSingleClause<OMPSIMDClause>();
  auto &&CodeGen = [&S, C, this](CodeGenFunction &CGF,
                                 PrePostActionTy &Action) {
    const CapturedStmt *CS = S.getInnermostCapturedStmt();
    if (C) {
      llvm::SmallVector<llvm::Value *, 16> CapturedVars;
      CGF.GenerateOpenMPCapturedVars(*CS, CapturedVars);
      llvm::Function *OutlinedFn =
          emitOutlinedOrderedFunction(CGM, CS, S.getBeginLoc());
      CGM.getOpenMPRuntime().emitOutlinedFunctionCall(CGF, S.getBeginLoc(),
                                                      OutlinedFn, CapturedVars);
    } else {
      Action.Enter(CGF);
      CGF.EmitStmt(CS->getCapturedStmt());
    }
  };
  OMPLexicalScope Scope(*this, S, OMPD_unknown);
  CGM.getOpenMPRuntime().emitOrderedRegion(*this, CodeGen, S.getBeginLoc(), !C);
}

static llvm::Value *convertToScalarValue(CodeGenFunction &CGF, RValue Val,
                                         QualType SrcType, QualType DestType,
                                         SourceLocation Loc) {
  assert(CGF.hasScalarEvaluationKind(DestType) &&
         "DestType must have scalar evaluation kind.");
  assert(!Val.isAggregate() && "Must be a scalar or complex.");
  return Val.isScalar() ? CGF.EmitScalarConversion(Val.getScalarVal(), SrcType,
                                                   DestType, Loc)
                        : CGF.EmitComplexToScalarConversion(
                              Val.getComplexVal(), SrcType, DestType, Loc);
}

static CodeGenFunction::ComplexPairTy
convertToComplexValue(CodeGenFunction &CGF, RValue Val, QualType SrcType,
                      QualType DestType, SourceLocation Loc) {
  assert(CGF.getEvaluationKind(DestType) == TEK_Complex &&
         "DestType must have complex evaluation kind.");
  CodeGenFunction::ComplexPairTy ComplexVal;
  if (Val.isScalar()) {
    // Convert the input element to the element type of the complex.
    QualType DestElementType =
        DestType->castAs<ComplexType>()->getElementType();
    llvm::Value *ScalarVal = CGF.EmitScalarConversion(
        Val.getScalarVal(), SrcType, DestElementType, Loc);
    ComplexVal = CodeGenFunction::ComplexPairTy(
        ScalarVal, llvm::Constant::getNullValue(ScalarVal->getType()));
  } else {
    assert(Val.isComplex() && "Must be a scalar or complex.");
    QualType SrcElementType = SrcType->castAs<ComplexType>()->getElementType();
    QualType DestElementType =
        DestType->castAs<ComplexType>()->getElementType();
    ComplexVal.first = CGF.EmitScalarConversion(
        Val.getComplexVal().first, SrcElementType, DestElementType, Loc);
    ComplexVal.second = CGF.EmitScalarConversion(
        Val.getComplexVal().second, SrcElementType, DestElementType, Loc);
  }
  return ComplexVal;
}

static void emitSimpleAtomicStore(CodeGenFunction &CGF, llvm::AtomicOrdering AO,
                                  LValue LVal, RValue RVal) {
  if (LVal.isGlobalReg())
    CGF.EmitStoreThroughGlobalRegLValue(RVal, LVal);
  else
    CGF.EmitAtomicStore(RVal, LVal, AO, LVal.isVolatile(), /*isInit=*/false);
}

static RValue emitSimpleAtomicLoad(CodeGenFunction &CGF,
                                   llvm::AtomicOrdering AO, LValue LVal,
                                   SourceLocation Loc) {
  if (LVal.isGlobalReg())
    return CGF.EmitLoadOfLValue(LVal, Loc);
  return CGF.EmitAtomicLoad(
      LVal, Loc, llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO),
      LVal.isVolatile());
}

void CodeGenFunction::emitOMPSimpleStore(LValue LVal, RValue RVal,
                                         QualType RValTy, SourceLocation Loc) {
  switch (getEvaluationKind(LVal.getType())) {
  case TEK_Scalar:
    EmitStoreThroughLValue(RValue::get(convertToScalarValue(
                               *this, RVal, RValTy, LVal.getType(), Loc)),
                           LVal);
    break;
  case TEK_Complex:
    EmitStoreOfComplex(
        convertToComplexValue(*this, RVal, RValTy, LVal.getType(), Loc), LVal,
        /*isInit=*/false);
    break;
  case TEK_Aggregate:
    llvm_unreachable("Must be a scalar or complex.");
  }
}

static void emitOMPAtomicReadExpr(CodeGenFunction &CGF, llvm::AtomicOrdering AO,
                                  const Expr *X, const Expr *V,
                                  SourceLocation Loc) {
  // v = x;
  assert(V->isLValue() && "V of 'omp atomic read' is not lvalue");
  assert(X->isLValue() && "X of 'omp atomic read' is not lvalue");
  LValue XLValue = CGF.EmitLValue(X);
  LValue VLValue = CGF.EmitLValue(V);
  RValue Res = emitSimpleAtomicLoad(CGF, AO, XLValue, Loc);
  // OpenMP, 2.17.7, atomic Construct
  // If the read or capture clause is specified and the acquire, acq_rel, or
  // seq_cst clause is specified then the strong flush on exit from the atomic
  // operation is also an acquire flush.
  switch (AO) {
  case llvm::AtomicOrdering::Acquire:
  case llvm::AtomicOrdering::AcquireRelease:
  case llvm::AtomicOrdering::SequentiallyConsistent:
    CGF.CGM.getOpenMPRuntime().emitFlush(CGF, std::nullopt, Loc,
                                         llvm::AtomicOrdering::Acquire);
    break;
  case llvm::AtomicOrdering::Monotonic:
  case llvm::AtomicOrdering::Release:
    break;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    llvm_unreachable("Unexpected ordering.");
  }
  CGF.emitOMPSimpleStore(VLValue, Res, X->getType().getNonReferenceType(), Loc);
  CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, V);
}

static void emitOMPAtomicWriteExpr(CodeGenFunction &CGF,
                                   llvm::AtomicOrdering AO, const Expr *X,
                                   const Expr *E, SourceLocation Loc) {
  // x = expr;
  assert(X->isLValue() && "X of 'omp atomic write' is not lvalue");
  emitSimpleAtomicStore(CGF, AO, CGF.EmitLValue(X), CGF.EmitAnyExpr(E));
  CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, X);
  // OpenMP, 2.17.7, atomic Construct
  // If the write, update, or capture clause is specified and the release,
  // acq_rel, or seq_cst clause is specified then the strong flush on entry to
  // the atomic operation is also a release flush.
  switch (AO) {
  case llvm::AtomicOrdering::Release:
  case llvm::AtomicOrdering::AcquireRelease:
  case llvm::AtomicOrdering::SequentiallyConsistent:
    CGF.CGM.getOpenMPRuntime().emitFlush(CGF, std::nullopt, Loc,
                                         llvm::AtomicOrdering::Release);
    break;
  case llvm::AtomicOrdering::Acquire:
  case llvm::AtomicOrdering::Monotonic:
    break;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    llvm_unreachable("Unexpected ordering.");
  }
}

static std::pair<bool, RValue> emitOMPAtomicRMW(CodeGenFunction &CGF, LValue X,
                                                RValue Update,
                                                BinaryOperatorKind BO,
                                                llvm::AtomicOrdering AO,
                                                bool IsXLHSInRHSPart) {
  ASTContext &Context = CGF.getContext();
  // Allow atomicrmw only if 'x' and 'update' are integer values, lvalue for 'x'
  // expression is simple and atomic is allowed for the given type for the
  // target platform.
  if (BO == BO_Comma || !Update.isScalar() || !X.isSimple() ||
      (!isa<llvm::ConstantInt>(Update.getScalarVal()) &&
       (Update.getScalarVal()->getType() != X.getAddress().getElementType())) ||
      !Context.getTargetInfo().hasBuiltinAtomic(
          Context.getTypeSize(X.getType()), Context.toBits(X.getAlignment())))
    return std::make_pair(false, RValue::get(nullptr));

  auto &&CheckAtomicSupport = [&CGF](llvm::Type *T, BinaryOperatorKind BO) {
    if (T->isIntegerTy())
      return true;

    if (T->isFloatingPointTy() && (BO == BO_Add || BO == BO_Sub))
      return llvm::isPowerOf2_64(CGF.CGM.getDataLayout().getTypeStoreSize(T));

    return false;
  };

  if (!CheckAtomicSupport(Update.getScalarVal()->getType(), BO) ||
      !CheckAtomicSupport(X.getAddress().getElementType(), BO))
    return std::make_pair(false, RValue::get(nullptr));

  bool IsInteger = X.getAddress().getElementType()->isIntegerTy();
  llvm::AtomicRMWInst::BinOp RMWOp;
  switch (BO) {
  case BO_Add:
    RMWOp = IsInteger ? llvm::AtomicRMWInst::Add : llvm::AtomicRMWInst::FAdd;
    break;
  case BO_Sub:
    if (!IsXLHSInRHSPart)
      return std::make_pair(false, RValue::get(nullptr));
    RMWOp = IsInteger ? llvm::AtomicRMWInst::Sub : llvm::AtomicRMWInst::FSub;
    break;
  case BO_And:
    RMWOp = llvm::AtomicRMWInst::And;
    break;
  case BO_Or:
    RMWOp = llvm::AtomicRMWInst::Or;
    break;
  case BO_Xor:
    RMWOp = llvm::AtomicRMWInst::Xor;
    break;
  case BO_LT:
    if (IsInteger)
      RMWOp = X.getType()->hasSignedIntegerRepresentation()
                  ? (IsXLHSInRHSPart ? llvm::AtomicRMWInst::Min
                                     : llvm::AtomicRMWInst::Max)
                  : (IsXLHSInRHSPart ? llvm::AtomicRMWInst::UMin
                                     : llvm::AtomicRMWInst::UMax);
    else
      RMWOp = IsXLHSInRHSPart ? llvm::AtomicRMWInst::FMin
                              : llvm::AtomicRMWInst::FMax;
    break;
  case BO_GT:
    if (IsInteger)
      RMWOp = X.getType()->hasSignedIntegerRepresentation()
                  ? (IsXLHSInRHSPart ? llvm::AtomicRMWInst::Max
                                     : llvm::AtomicRMWInst::Min)
                  : (IsXLHSInRHSPart ? llvm::AtomicRMWInst::UMax
                                     : llvm::AtomicRMWInst::UMin);
    else
      RMWOp = IsXLHSInRHSPart ? llvm::AtomicRMWInst::FMax
                              : llvm::AtomicRMWInst::FMin;
    break;
  case BO_Assign:
    RMWOp = llvm::AtomicRMWInst::Xchg;
    break;
  case BO_Mul:
  case BO_Div:
  case BO_Rem:
  case BO_Shl:
  case BO_Shr:
  case BO_LAnd:
  case BO_LOr:
    return std::make_pair(false, RValue::get(nullptr));
  case BO_PtrMemD:
  case BO_PtrMemI:
  case BO_LE:
  case BO_GE:
  case BO_EQ:
  case BO_NE:
  case BO_Cmp:
  case BO_AddAssign:
  case BO_SubAssign:
  case BO_AndAssign:
  case BO_OrAssign:
  case BO_XorAssign:
  case BO_MulAssign:
  case BO_DivAssign:
  case BO_RemAssign:
  case BO_ShlAssign:
  case BO_ShrAssign:
  case BO_Comma:
    llvm_unreachable("Unsupported atomic update operation");
  }
  llvm::Value *UpdateVal = Update.getScalarVal();
  if (auto *IC = dyn_cast<llvm::ConstantInt>(UpdateVal)) {
    if (IsInteger)
      UpdateVal = CGF.Builder.CreateIntCast(
          IC, X.getAddress().getElementType(),
          X.getType()->hasSignedIntegerRepresentation());
    else
      UpdateVal = CGF.Builder.CreateCast(llvm::Instruction::CastOps::UIToFP, IC,
                                         X.getAddress().getElementType());
  }
  llvm::Value *Res =
      CGF.Builder.CreateAtomicRMW(RMWOp, X.getAddress(), UpdateVal, AO);
  return std::make_pair(true, RValue::get(Res));
}

std::pair<bool, RValue> CodeGenFunction::EmitOMPAtomicSimpleUpdateExpr(
    LValue X, RValue E, BinaryOperatorKind BO, bool IsXLHSInRHSPart,
    llvm::AtomicOrdering AO, SourceLocation Loc,
    const llvm::function_ref<RValue(RValue)> CommonGen) {
  // Update expressions are allowed to have the following forms:
  // x binop= expr; -> xrval + expr;
  // x++, ++x -> xrval + 1;
  // x--, --x -> xrval - 1;
  // x = x binop expr; -> xrval binop expr
  // x = expr Op x; - > expr binop xrval;
  auto Res = emitOMPAtomicRMW(*this, X, E, BO, AO, IsXLHSInRHSPart);
  if (!Res.first) {
    if (X.isGlobalReg()) {
      // Emit an update expression: 'xrval' binop 'expr' or 'expr' binop
      // 'xrval'.
      EmitStoreThroughLValue(CommonGen(EmitLoadOfLValue(X, Loc)), X);
    } else {
      // Perform compare-and-swap procedure.
      EmitAtomicUpdate(X, AO, CommonGen, X.getType().isVolatileQualified());
    }
  }
  return Res;
}

static void emitOMPAtomicUpdateExpr(CodeGenFunction &CGF,
                                    llvm::AtomicOrdering AO, const Expr *X,
                                    const Expr *E, const Expr *UE,
                                    bool IsXLHSInRHSPart, SourceLocation Loc) {
  assert(isa<BinaryOperator>(UE->IgnoreImpCasts()) &&
         "Update expr in 'atomic update' must be a binary operator.");
  const auto *BOUE = cast<BinaryOperator>(UE->IgnoreImpCasts());
  // Update expressions are allowed to have the following forms:
  // x binop= expr; -> xrval + expr;
  // x++, ++x -> xrval + 1;
  // x--, --x -> xrval - 1;
  // x = x binop expr; -> xrval binop expr
  // x = expr Op x; - > expr binop xrval;
  assert(X->isLValue() && "X of 'omp atomic update' is not lvalue");
  LValue XLValue = CGF.EmitLValue(X);
  RValue ExprRValue = CGF.EmitAnyExpr(E);
  const auto *LHS = cast<OpaqueValueExpr>(BOUE->getLHS()->IgnoreImpCasts());
  const auto *RHS = cast<OpaqueValueExpr>(BOUE->getRHS()->IgnoreImpCasts());
  const OpaqueValueExpr *XRValExpr = IsXLHSInRHSPart ? LHS : RHS;
  const OpaqueValueExpr *ERValExpr = IsXLHSInRHSPart ? RHS : LHS;
  auto &&Gen = [&CGF, UE, ExprRValue, XRValExpr, ERValExpr](RValue XRValue) {
    CodeGenFunction::OpaqueValueMapping MapExpr(CGF, ERValExpr, ExprRValue);
    CodeGenFunction::OpaqueValueMapping MapX(CGF, XRValExpr, XRValue);
    return CGF.EmitAnyExpr(UE);
  };
  (void)CGF.EmitOMPAtomicSimpleUpdateExpr(
      XLValue, ExprRValue, BOUE->getOpcode(), IsXLHSInRHSPart, AO, Loc, Gen);
  CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, X);
  // OpenMP, 2.17.7, atomic Construct
  // If the write, update, or capture clause is specified and the release,
  // acq_rel, or seq_cst clause is specified then the strong flush on entry to
  // the atomic operation is also a release flush.
  switch (AO) {
  case llvm::AtomicOrdering::Release:
  case llvm::AtomicOrdering::AcquireRelease:
  case llvm::AtomicOrdering::SequentiallyConsistent:
    CGF.CGM.getOpenMPRuntime().emitFlush(CGF, std::nullopt, Loc,
                                         llvm::AtomicOrdering::Release);
    break;
  case llvm::AtomicOrdering::Acquire:
  case llvm::AtomicOrdering::Monotonic:
    break;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    llvm_unreachable("Unexpected ordering.");
  }
}

static RValue convertToType(CodeGenFunction &CGF, RValue Value,
                            QualType SourceType, QualType ResType,
                            SourceLocation Loc) {
  switch (CGF.getEvaluationKind(ResType)) {
  case TEK_Scalar:
    return RValue::get(
        convertToScalarValue(CGF, Value, SourceType, ResType, Loc));
  case TEK_Complex: {
    auto Res = convertToComplexValue(CGF, Value, SourceType, ResType, Loc);
    return RValue::getComplex(Res.first, Res.second);
  }
  case TEK_Aggregate:
    break;
  }
  llvm_unreachable("Must be a scalar or complex.");
}

static void emitOMPAtomicCaptureExpr(CodeGenFunction &CGF,
                                     llvm::AtomicOrdering AO,
                                     bool IsPostfixUpdate, const Expr *V,
                                     const Expr *X, const Expr *E,
                                     const Expr *UE, bool IsXLHSInRHSPart,
                                     SourceLocation Loc) {
  assert(X->isLValue() && "X of 'omp atomic capture' is not lvalue");
  assert(V->isLValue() && "V of 'omp atomic capture' is not lvalue");
  RValue NewVVal;
  LValue VLValue = CGF.EmitLValue(V);
  LValue XLValue = CGF.EmitLValue(X);
  RValue ExprRValue = CGF.EmitAnyExpr(E);
  QualType NewVValType;
  if (UE) {
    // 'x' is updated with some additional value.
    assert(isa<BinaryOperator>(UE->IgnoreImpCasts()) &&
           "Update expr in 'atomic capture' must be a binary operator.");
    const auto *BOUE = cast<BinaryOperator>(UE->IgnoreImpCasts());
    // Update expressions are allowed to have the following forms:
    // x binop= expr; -> xrval + expr;
    // x++, ++x -> xrval + 1;
    // x--, --x -> xrval - 1;
    // x = x binop expr; -> xrval binop expr
    // x = expr Op x; - > expr binop xrval;
    const auto *LHS = cast<OpaqueValueExpr>(BOUE->getLHS()->IgnoreImpCasts());
    const auto *RHS = cast<OpaqueValueExpr>(BOUE->getRHS()->IgnoreImpCasts());
    const OpaqueValueExpr *XRValExpr = IsXLHSInRHSPart ? LHS : RHS;
    NewVValType = XRValExpr->getType();
    const OpaqueValueExpr *ERValExpr = IsXLHSInRHSPart ? RHS : LHS;
    auto &&Gen = [&CGF, &NewVVal, UE, ExprRValue, XRValExpr, ERValExpr,
                  IsPostfixUpdate](RValue XRValue) {
      CodeGenFunction::OpaqueValueMapping MapExpr(CGF, ERValExpr, ExprRValue);
      CodeGenFunction::OpaqueValueMapping MapX(CGF, XRValExpr, XRValue);
      RValue Res = CGF.EmitAnyExpr(UE);
      NewVVal = IsPostfixUpdate ? XRValue : Res;
      return Res;
    };
    auto Res = CGF.EmitOMPAtomicSimpleUpdateExpr(
        XLValue, ExprRValue, BOUE->getOpcode(), IsXLHSInRHSPart, AO, Loc, Gen);
    CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, X);
    if (Res.first) {
      // 'atomicrmw' instruction was generated.
      if (IsPostfixUpdate) {
        // Use old value from 'atomicrmw'.
        NewVVal = Res.second;
      } else {
        // 'atomicrmw' does not provide new value, so evaluate it using old
        // value of 'x'.
        CodeGenFunction::OpaqueValueMapping MapExpr(CGF, ERValExpr, ExprRValue);
        CodeGenFunction::OpaqueValueMapping MapX(CGF, XRValExpr, Res.second);
        NewVVal = CGF.EmitAnyExpr(UE);
      }
    }
  } else {
    // 'x' is simply rewritten with some 'expr'.
    NewVValType = X->getType().getNonReferenceType();
    ExprRValue = convertToType(CGF, ExprRValue, E->getType(),
                               X->getType().getNonReferenceType(), Loc);
    auto &&Gen = [&NewVVal, ExprRValue](RValue XRValue) {
      NewVVal = XRValue;
      return ExprRValue;
    };
    // Try to perform atomicrmw xchg, otherwise simple exchange.
    auto Res = CGF.EmitOMPAtomicSimpleUpdateExpr(
        XLValue, ExprRValue, /*BO=*/BO_Assign, /*IsXLHSInRHSPart=*/false, AO,
        Loc, Gen);
    CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, X);
    if (Res.first) {
      // 'atomicrmw' instruction was generated.
      NewVVal = IsPostfixUpdate ? Res.second : ExprRValue;
    }
  }
  // Emit post-update store to 'v' of old/new 'x' value.
  CGF.emitOMPSimpleStore(VLValue, NewVVal, NewVValType, Loc);
  CGF.CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(CGF, V);
  // OpenMP 5.1 removes the required flush for capture clause.
  if (CGF.CGM.getLangOpts().OpenMP < 51) {
    // OpenMP, 2.17.7, atomic Construct
    // If the write, update, or capture clause is specified and the release,
    // acq_rel, or seq_cst clause is specified then the strong flush on entry to
    // the atomic operation is also a release flush.
    // If the read or capture clause is specified and the acquire, acq_rel, or
    // seq_cst clause is specified then the strong flush on exit from the atomic
    // operation is also an acquire flush.
    switch (AO) {
    case llvm::AtomicOrdering::Release:
      CGF.CGM.getOpenMPRuntime().emitFlush(CGF, std::nullopt, Loc,
                                           llvm::AtomicOrdering::Release);
      break;
    case llvm::AtomicOrdering::Acquire:
      CGF.CGM.getOpenMPRuntime().emitFlush(CGF, std::nullopt, Loc,
                                           llvm::AtomicOrdering::Acquire);
      break;
    case llvm::AtomicOrdering::AcquireRelease:
    case llvm::AtomicOrdering::SequentiallyConsistent:
      CGF.CGM.getOpenMPRuntime().emitFlush(
          CGF, std::nullopt, Loc, llvm::AtomicOrdering::AcquireRelease);
      break;
    case llvm::AtomicOrdering::Monotonic:
      break;
    case llvm::AtomicOrdering::NotAtomic:
    case llvm::AtomicOrdering::Unordered:
      llvm_unreachable("Unexpected ordering.");
    }
  }
}

static void emitOMPAtomicCompareExpr(
    CodeGenFunction &CGF, llvm::AtomicOrdering AO, llvm::AtomicOrdering FailAO,
    const Expr *X, const Expr *V, const Expr *R, const Expr *E, const Expr *D,
    const Expr *CE, bool IsXBinopExpr, bool IsPostfixUpdate, bool IsFailOnly,
    SourceLocation Loc) {
  llvm::OpenMPIRBuilder &OMPBuilder =
      CGF.CGM.getOpenMPRuntime().getOMPBuilder();

  OMPAtomicCompareOp Op;
  assert(isa<BinaryOperator>(CE) && "CE is not a BinaryOperator");
  switch (cast<BinaryOperator>(CE)->getOpcode()) {
  case BO_EQ:
    Op = OMPAtomicCompareOp::EQ;
    break;
  case BO_LT:
    Op = OMPAtomicCompareOp::MIN;
    break;
  case BO_GT:
    Op = OMPAtomicCompareOp::MAX;
    break;
  default:
    llvm_unreachable("unsupported atomic compare binary operator");
  }

  LValue XLVal = CGF.EmitLValue(X);
  Address XAddr = XLVal.getAddress();

  auto EmitRValueWithCastIfNeeded = [&CGF, Loc](const Expr *X, const Expr *E) {
    if (X->getType() == E->getType())
      return CGF.EmitScalarExpr(E);
    const Expr *NewE = E->IgnoreImplicitAsWritten();
    llvm::Value *V = CGF.EmitScalarExpr(NewE);
    if (NewE->getType() == X->getType())
      return V;
    return CGF.EmitScalarConversion(V, NewE->getType(), X->getType(), Loc);
  };

  llvm::Value *EVal = EmitRValueWithCastIfNeeded(X, E);
  llvm::Value *DVal = D ? EmitRValueWithCastIfNeeded(X, D) : nullptr;
  if (auto *CI = dyn_cast<llvm::ConstantInt>(EVal))
    EVal = CGF.Builder.CreateIntCast(
        CI, XLVal.getAddress().getElementType(),
        E->getType()->hasSignedIntegerRepresentation());
  if (DVal)
    if (auto *CI = dyn_cast<llvm::ConstantInt>(DVal))
      DVal = CGF.Builder.CreateIntCast(
          CI, XLVal.getAddress().getElementType(),
          D->getType()->hasSignedIntegerRepresentation());

  llvm::OpenMPIRBuilder::AtomicOpValue XOpVal{
      XAddr.emitRawPointer(CGF), XAddr.getElementType(),
      X->getType()->hasSignedIntegerRepresentation(),
      X->getType().isVolatileQualified()};
  llvm::OpenMPIRBuilder::AtomicOpValue VOpVal, ROpVal;
  if (V) {
    LValue LV = CGF.EmitLValue(V);
    Address Addr = LV.getAddress();
    VOpVal = {Addr.emitRawPointer(CGF), Addr.getElementType(),
              V->getType()->hasSignedIntegerRepresentation(),
              V->getType().isVolatileQualified()};
  }
  if (R) {
    LValue LV = CGF.EmitLValue(R);
    Address Addr = LV.getAddress();
    ROpVal = {Addr.emitRawPointer(CGF), Addr.getElementType(),
              R->getType()->hasSignedIntegerRepresentation(),
              R->getType().isVolatileQualified()};
  }

  if (FailAO == llvm::AtomicOrdering::NotAtomic) {
    // fail clause was not mentioned on the
    // "#pragma omp atomic compare" construct.
    CGF.Builder.restoreIP(OMPBuilder.createAtomicCompare(
        CGF.Builder, XOpVal, VOpVal, ROpVal, EVal, DVal, AO, Op, IsXBinopExpr,
        IsPostfixUpdate, IsFailOnly));
  } else
    CGF.Builder.restoreIP(OMPBuilder.createAtomicCompare(
        CGF.Builder, XOpVal, VOpVal, ROpVal, EVal, DVal, AO, Op, IsXBinopExpr,
        IsPostfixUpdate, IsFailOnly, FailAO));
}

static void emitOMPAtomicExpr(CodeGenFunction &CGF, OpenMPClauseKind Kind,
                              llvm::AtomicOrdering AO,
                              llvm::AtomicOrdering FailAO, bool IsPostfixUpdate,
                              const Expr *X, const Expr *V, const Expr *R,
                              const Expr *E, const Expr *UE, const Expr *D,
                              const Expr *CE, bool IsXLHSInRHSPart,
                              bool IsFailOnly, SourceLocation Loc) {
  switch (Kind) {
  case OMPC_read:
    emitOMPAtomicReadExpr(CGF, AO, X, V, Loc);
    break;
  case OMPC_write:
    emitOMPAtomicWriteExpr(CGF, AO, X, E, Loc);
    break;
  case OMPC_unknown:
  case OMPC_update:
    emitOMPAtomicUpdateExpr(CGF, AO, X, E, UE, IsXLHSInRHSPart, Loc);
    break;
  case OMPC_capture:
    emitOMPAtomicCaptureExpr(CGF, AO, IsPostfixUpdate, V, X, E, UE,
                             IsXLHSInRHSPart, Loc);
    break;
  case OMPC_compare: {
    emitOMPAtomicCompareExpr(CGF, AO, FailAO, X, V, R, E, D, CE,
                             IsXLHSInRHSPart, IsPostfixUpdate, IsFailOnly, Loc);
    break;
  }
  default:
    llvm_unreachable("Clause is not allowed in 'omp atomic'.");
  }
}

void CodeGenFunction::EmitOMPAtomicDirective(const OMPAtomicDirective &S) {
  llvm::AtomicOrdering AO = CGM.getOpenMPRuntime().getDefaultMemoryOrdering();
  // Fail Memory Clause Ordering.
  llvm::AtomicOrdering FailAO = llvm::AtomicOrdering::NotAtomic;
  bool MemOrderingSpecified = false;
  if (S.getSingleClause<OMPSeqCstClause>()) {
    AO = llvm::AtomicOrdering::SequentiallyConsistent;
    MemOrderingSpecified = true;
  } else if (S.getSingleClause<OMPAcqRelClause>()) {
    AO = llvm::AtomicOrdering::AcquireRelease;
    MemOrderingSpecified = true;
  } else if (S.getSingleClause<OMPAcquireClause>()) {
    AO = llvm::AtomicOrdering::Acquire;
    MemOrderingSpecified = true;
  } else if (S.getSingleClause<OMPReleaseClause>()) {
    AO = llvm::AtomicOrdering::Release;
    MemOrderingSpecified = true;
  } else if (S.getSingleClause<OMPRelaxedClause>()) {
    AO = llvm::AtomicOrdering::Monotonic;
    MemOrderingSpecified = true;
  }
  llvm::SmallSet<OpenMPClauseKind, 2> KindsEncountered;
  OpenMPClauseKind Kind = OMPC_unknown;
  for (const OMPClause *C : S.clauses()) {
    // Find first clause (skip seq_cst|acq_rel|aqcuire|release|relaxed clause,
    // if it is first).
    OpenMPClauseKind K = C->getClauseKind();
    // TBD
    if (K == OMPC_weak)
      return;
    if (K == OMPC_seq_cst || K == OMPC_acq_rel || K == OMPC_acquire ||
        K == OMPC_release || K == OMPC_relaxed || K == OMPC_hint)
      continue;
    Kind = K;
    KindsEncountered.insert(K);
  }
  // We just need to correct Kind here. No need to set a bool saying it is
  // actually compare capture because we can tell from whether V and R are
  // nullptr.
  if (KindsEncountered.contains(OMPC_compare) &&
      KindsEncountered.contains(OMPC_capture))
    Kind = OMPC_compare;
  if (!MemOrderingSpecified) {
    llvm::AtomicOrdering DefaultOrder =
        CGM.getOpenMPRuntime().getDefaultMemoryOrdering();
    if (DefaultOrder == llvm::AtomicOrdering::Monotonic ||
        DefaultOrder == llvm::AtomicOrdering::SequentiallyConsistent ||
        (DefaultOrder == llvm::AtomicOrdering::AcquireRelease &&
         Kind == OMPC_capture)) {
      AO = DefaultOrder;
    } else if (DefaultOrder == llvm::AtomicOrdering::AcquireRelease) {
      if (Kind == OMPC_unknown || Kind == OMPC_update || Kind == OMPC_write) {
        AO = llvm::AtomicOrdering::Release;
      } else if (Kind == OMPC_read) {
        assert(Kind == OMPC_read && "Unexpected atomic kind.");
        AO = llvm::AtomicOrdering::Acquire;
      }
    }
  }

  if (KindsEncountered.contains(OMPC_compare) &&
      KindsEncountered.contains(OMPC_fail)) {
    Kind = OMPC_compare;
    const auto *FailClause = S.getSingleClause<OMPFailClause>();
    if (FailClause) {
      OpenMPClauseKind FailParameter = FailClause->getFailParameter();
      if (FailParameter == llvm::omp::OMPC_relaxed)
        FailAO = llvm::AtomicOrdering::Monotonic;
      else if (FailParameter == llvm::omp::OMPC_acquire)
        FailAO = llvm::AtomicOrdering::Acquire;
      else if (FailParameter == llvm::omp::OMPC_seq_cst)
        FailAO = llvm::AtomicOrdering::SequentiallyConsistent;
    }
  }

  LexicalScope Scope(*this, S.getSourceRange());
  EmitStopPoint(S.getAssociatedStmt());
  emitOMPAtomicExpr(*this, Kind, AO, FailAO, S.isPostfixUpdate(), S.getX(),
                    S.getV(), S.getR(), S.getExpr(), S.getUpdateExpr(),
                    S.getD(), S.getCondExpr(), S.isXLHSInRHSPart(),
                    S.isFailOnly(), S.getBeginLoc());
}

static void emitCommonOMPTargetDirective(CodeGenFunction &CGF,
                                         const OMPExecutableDirective &S,
                                         const RegionCodeGenTy &CodeGen) {
  assert(isOpenMPTargetExecutionDirective(S.getDirectiveKind()));
  CodeGenModule &CGM = CGF.CGM;

  // On device emit this construct as inlined code.
  if (CGM.getLangOpts().OpenMPIsTargetDevice) {
    OMPLexicalScope Scope(CGF, S, OMPD_target);
    CGM.getOpenMPRuntime().emitInlinedDirective(
        CGF, OMPD_target, [&S](CodeGenFunction &CGF, PrePostActionTy &) {
          CGF.EmitStmt(S.getInnermostCapturedStmt()->getCapturedStmt());
        });
    return;
  }

  auto LPCRegion = CGOpenMPRuntime::LastprivateConditionalRAII::disable(CGF, S);
  llvm::Function *Fn = nullptr;
  llvm::Constant *FnID = nullptr;

  const Expr *IfCond = nullptr;
  // Check for the at most one if clause associated with the target region.
  for (const auto *C : S.getClausesOfKind<OMPIfClause>()) {
    if (C->getNameModifier() == OMPD_unknown ||
        C->getNameModifier() == OMPD_target) {
      IfCond = C->getCondition();
      break;
    }
  }

  // Check if we have any device clause associated with the directive.
  llvm::PointerIntPair<const Expr *, 2, OpenMPDeviceClauseModifier> Device(
      nullptr, OMPC_DEVICE_unknown);
  if (auto *C = S.getSingleClause<OMPDeviceClause>())
    Device.setPointerAndInt(C->getDevice(), C->getModifier());

  // Check if we have an if clause whose conditional always evaluates to false
  // or if we do not have any targets specified. If so the target region is not
  // an offload entry point.
  bool IsOffloadEntry = true;
  if (IfCond) {
    bool Val;
    if (CGF.ConstantFoldsToSimpleInteger(IfCond, Val) && !Val)
      IsOffloadEntry = false;
  }
  if (CGM.getLangOpts().OMPTargetTriples.empty())
    IsOffloadEntry = false;

  if (CGM.getLangOpts().OpenMPOffloadMandatory && !IsOffloadEntry) {
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error,
        "No offloading entry generated while offloading is mandatory.");
    CGM.getDiags().Report(DiagID);
  }

  assert(CGF.CurFuncDecl && "No parent declaration for target region!");
  StringRef ParentName;
  // In case we have Ctors/Dtors we use the complete type variant to produce
  // the mangling of the device outlined kernel.
  if (const auto *D = dyn_cast<CXXConstructorDecl>(CGF.CurFuncDecl))
    ParentName = CGM.getMangledName(GlobalDecl(D, Ctor_Complete));
  else if (const auto *D = dyn_cast<CXXDestructorDecl>(CGF.CurFuncDecl))
    ParentName = CGM.getMangledName(GlobalDecl(D, Dtor_Complete));
  else
    ParentName =
        CGM.getMangledName(GlobalDecl(cast<FunctionDecl>(CGF.CurFuncDecl)));

  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(S, ParentName, Fn, FnID,
                                                    IsOffloadEntry, CodeGen);
  OMPLexicalScope Scope(CGF, S, OMPD_task);
  auto &&SizeEmitter =
      [IsOffloadEntry](CodeGenFunction &CGF,
                       const OMPLoopDirective &D) -> llvm::Value * {
    if (IsOffloadEntry) {
      OMPLoopScope(CGF, D);
      // Emit calculation of the iterations count.
      llvm::Value *NumIterations = CGF.EmitScalarExpr(D.getNumIterations());
      NumIterations = CGF.Builder.CreateIntCast(NumIterations, CGF.Int64Ty,
                                                /*isSigned=*/false);
      return NumIterations;
    }
    return nullptr;
  };
  CGM.getOpenMPRuntime().emitTargetCall(CGF, S, Fn, FnID, IfCond, Device,
                                        SizeEmitter);
}

static void emitTargetRegion(CodeGenFunction &CGF, const OMPTargetDirective &S,
                             PrePostActionTy &Action) {
  Action.Enter(CGF);
  CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
  (void)CGF.EmitOMPFirstprivateClause(S, PrivateScope);
  CGF.EmitOMPPrivateClause(S, PrivateScope);
  (void)PrivateScope.Privatize();
  if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()))
    CGF.CGM.getOpenMPRuntime().adjustTargetSpecificDataForLambdas(CGF, S);

  CGF.EmitStmt(S.getCapturedStmt(OMPD_target)->getCapturedStmt());
  CGF.EnsureInsertPoint();
}

void CodeGenFunction::EmitOMPTargetDeviceFunction(CodeGenModule &CGM,
                                                  StringRef ParentName,
                                                  const OMPTargetDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetDirective(const OMPTargetDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

static void emitCommonOMPTeamsDirective(CodeGenFunction &CGF,
                                        const OMPExecutableDirective &S,
                                        OpenMPDirectiveKind InnermostKind,
                                        const RegionCodeGenTy &CodeGen) {
  const CapturedStmt *CS = S.getCapturedStmt(OMPD_teams);
  llvm::Function *OutlinedFn =
      CGF.CGM.getOpenMPRuntime().emitTeamsOutlinedFunction(
          CGF, S, *CS->getCapturedDecl()->param_begin(), InnermostKind,
          CodeGen);

  const auto *NT = S.getSingleClause<OMPNumTeamsClause>();
  const auto *TL = S.getSingleClause<OMPThreadLimitClause>();
  if (NT || TL) {
    const Expr *NumTeams = NT ? NT->getNumTeams() : nullptr;
    const Expr *ThreadLimit = TL ? TL->getThreadLimit() : nullptr;

    CGF.CGM.getOpenMPRuntime().emitNumTeamsClause(CGF, NumTeams, ThreadLimit,
                                                  S.getBeginLoc());
  }

  OMPTeamsScope Scope(CGF, S);
  llvm::SmallVector<llvm::Value *, 16> CapturedVars;
  CGF.GenerateOpenMPCapturedVars(*CS, CapturedVars);
  CGF.CGM.getOpenMPRuntime().emitTeamsCall(CGF, S, S.getBeginLoc(), OutlinedFn,
                                           CapturedVars);
}

void CodeGenFunction::EmitOMPTeamsDirective(const OMPTeamsDirective &S) {
  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    (void)CGF.EmitOMPFirstprivateClause(S, PrivateScope);
    CGF.EmitOMPPrivateClause(S, PrivateScope);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.EmitStmt(S.getCapturedStmt(OMPD_teams)->getCapturedStmt());
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(*this, S, OMPD_distribute, CodeGen);
  emitPostUpdateForReductionClause(*this, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

static void emitTargetTeamsRegion(CodeGenFunction &CGF, PrePostActionTy &Action,
                                  const OMPTargetTeamsDirective &S) {
  auto *CS = S.getCapturedStmt(OMPD_teams);
  Action.Enter(CGF);
  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, CS](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    (void)CGF.EmitOMPFirstprivateClause(S, PrivateScope);
    CGF.EmitOMPPrivateClause(S, PrivateScope);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()))
      CGF.CGM.getOpenMPRuntime().adjustTargetSpecificDataForLambdas(CGF, S);
    CGF.EmitStmt(CS->getCapturedStmt());
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(CGF, S, OMPD_teams, CodeGen);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTargetTeamsDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetTeamsDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsRegion(CGF, Action, S);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetTeamsDirective(
    const OMPTargetTeamsDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsRegion(CGF, Action, S);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

static void
emitTargetTeamsDistributeRegion(CodeGenFunction &CGF, PrePostActionTy &Action,
                                const OMPTargetTeamsDistributeDirective &S) {
  Action.Enter(CGF);
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_distribute,
                                                    CodeGenDistribute);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(CGF, S, OMPD_distribute, CodeGen);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetTeamsDistributeDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeRegion(CGF, Action, S);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeDirective(
    const OMPTargetTeamsDistributeDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeRegion(CGF, Action, S);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

static void emitTargetTeamsDistributeSimdRegion(
    CodeGenFunction &CGF, PrePostActionTy &Action,
    const OMPTargetTeamsDistributeSimdDirective &S) {
  Action.Enter(CGF);
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_distribute,
                                                    CodeGenDistribute);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(CGF, S, OMPD_distribute_simd, CodeGen);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeSimdDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetTeamsDistributeSimdDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeSimdRegion(CGF, Action, S);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeSimdDirective(
    const OMPTargetTeamsDistributeSimdDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeSimdRegion(CGF, Action, S);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

void CodeGenFunction::EmitOMPTeamsDistributeDirective(
    const OMPTeamsDistributeDirective &S) {

  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_distribute,
                                                    CodeGenDistribute);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(*this, S, OMPD_distribute, CodeGen);
  emitPostUpdateForReductionClause(*this, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTeamsDistributeSimdDirective(
    const OMPTeamsDistributeSimdDirective &S) {
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_simd,
                                                    CodeGenDistribute);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(*this, S, OMPD_distribute_simd, CodeGen);
  emitPostUpdateForReductionClause(*this, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTeamsDistributeParallelForDirective(
    const OMPTeamsDistributeParallelForDirective &S) {
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitInnerParallelForWhenCombined,
                              S.getDistInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_distribute,
                                                    CodeGenDistribute);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(*this, S, OMPD_distribute_parallel_for, CodeGen);
  emitPostUpdateForReductionClause(*this, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTeamsDistributeParallelForSimdDirective(
    const OMPTeamsDistributeParallelForSimdDirective &S) {
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitInnerParallelForWhenCombined,
                              S.getDistInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(
        CGF, OMPD_distribute, CodeGenDistribute, /*HasCancel=*/false);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(*this, S, OMPD_distribute_parallel_for_simd,
                              CodeGen);
  emitPostUpdateForReductionClause(*this, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPInteropDirective(const OMPInteropDirective &S) {
  llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
  llvm::Value *Device = nullptr;
  llvm::Value *NumDependences = nullptr;
  llvm::Value *DependenceList = nullptr;

  if (const auto *C = S.getSingleClause<OMPDeviceClause>())
    Device = EmitScalarExpr(C->getDevice());

  // Build list and emit dependences
  OMPTaskDataTy Data;
  buildDependences(S, Data);
  if (!Data.Dependences.empty()) {
    Address DependenciesArray = Address::invalid();
    std::tie(NumDependences, DependenciesArray) =
        CGM.getOpenMPRuntime().emitDependClause(*this, Data.Dependences,
                                                S.getBeginLoc());
    DependenceList = DependenciesArray.emitRawPointer(*this);
  }
  Data.HasNowaitClause = S.hasClausesOfKind<OMPNowaitClause>();

  assert(!(Data.HasNowaitClause && !(S.getSingleClause<OMPInitClause>() ||
                                     S.getSingleClause<OMPDestroyClause>() ||
                                     S.getSingleClause<OMPUseClause>())) &&
         "OMPNowaitClause clause is used separately in OMPInteropDirective.");

  auto ItOMPInitClause = S.getClausesOfKind<OMPInitClause>();
  if (!ItOMPInitClause.empty()) {
    // Look at the multiple init clauses
    for (const OMPInitClause *C : ItOMPInitClause) {
      llvm::Value *InteropvarPtr =
          EmitLValue(C->getInteropVar()).getPointer(*this);
      llvm::omp::OMPInteropType InteropType =
          llvm::omp::OMPInteropType::Unknown;
      if (C->getIsTarget()) {
        InteropType = llvm::omp::OMPInteropType::Target;
      } else {
        assert(C->getIsTargetSync() &&
               "Expected interop-type target/targetsync");
        InteropType = llvm::omp::OMPInteropType::TargetSync;
      }
      OMPBuilder.createOMPInteropInit(Builder, InteropvarPtr, InteropType,
                                      Device, NumDependences, DependenceList,
                                      Data.HasNowaitClause);
    }
  }
  auto ItOMPDestroyClause = S.getClausesOfKind<OMPDestroyClause>();
  if (!ItOMPDestroyClause.empty()) {
    // Look at the multiple destroy clauses
    for (const OMPDestroyClause *C : ItOMPDestroyClause) {
      llvm::Value *InteropvarPtr =
          EmitLValue(C->getInteropVar()).getPointer(*this);
      OMPBuilder.createOMPInteropDestroy(Builder, InteropvarPtr, Device,
                                         NumDependences, DependenceList,
                                         Data.HasNowaitClause);
    }
  }
  auto ItOMPUseClause = S.getClausesOfKind<OMPUseClause>();
  if (!ItOMPUseClause.empty()) {
    // Look at the multiple use clauses
    for (const OMPUseClause *C : ItOMPUseClause) {
      llvm::Value *InteropvarPtr =
          EmitLValue(C->getInteropVar()).getPointer(*this);
      OMPBuilder.createOMPInteropUse(Builder, InteropvarPtr, Device,
                                     NumDependences, DependenceList,
                                     Data.HasNowaitClause);
    }
  }
}

static void emitTargetTeamsDistributeParallelForRegion(
    CodeGenFunction &CGF, const OMPTargetTeamsDistributeParallelForDirective &S,
    PrePostActionTy &Action) {
  Action.Enter(CGF);
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitInnerParallelForWhenCombined,
                              S.getDistInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGenTeams = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                                 PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(
        CGF, OMPD_distribute, CodeGenDistribute, /*HasCancel=*/false);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };

  emitCommonOMPTeamsDirective(CGF, S, OMPD_distribute_parallel_for,
                              CodeGenTeams);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeParallelForDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetTeamsDistributeParallelForDirective &S) {
  // Emit SPMD target teams distribute parallel for region as a standalone
  // region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeParallelForRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeParallelForDirective(
    const OMPTargetTeamsDistributeParallelForDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeParallelForRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

static void emitTargetTeamsDistributeParallelForSimdRegion(
    CodeGenFunction &CGF,
    const OMPTargetTeamsDistributeParallelForSimdDirective &S,
    PrePostActionTy &Action) {
  Action.Enter(CGF);
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitInnerParallelForWhenCombined,
                              S.getDistInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGenTeams = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                                 PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(
        CGF, OMPD_distribute, CodeGenDistribute, /*HasCancel=*/false);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };

  emitCommonOMPTeamsDirective(CGF, S, OMPD_distribute_parallel_for_simd,
                              CodeGenTeams);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeParallelForSimdDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetTeamsDistributeParallelForSimdDirective &S) {
  // Emit SPMD target teams distribute parallel for simd region as a standalone
  // region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeParallelForSimdRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetTeamsDistributeParallelForSimdDirective(
    const OMPTargetTeamsDistributeParallelForSimdDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetTeamsDistributeParallelForSimdRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

void CodeGenFunction::EmitOMPCancellationPointDirective(
    const OMPCancellationPointDirective &S) {
  CGM.getOpenMPRuntime().emitCancellationPointCall(*this, S.getBeginLoc(),
                                                   S.getCancelRegion());
}

void CodeGenFunction::EmitOMPCancelDirective(const OMPCancelDirective &S) {
  const Expr *IfCond = nullptr;
  for (const auto *C : S.getClausesOfKind<OMPIfClause>()) {
    if (C->getNameModifier() == OMPD_unknown ||
        C->getNameModifier() == OMPD_cancel) {
      IfCond = C->getCondition();
      break;
    }
  }
  if (CGM.getLangOpts().OpenMPIRBuilder) {
    llvm::OpenMPIRBuilder &OMPBuilder = CGM.getOpenMPRuntime().getOMPBuilder();
    // TODO: This check is necessary as we only generate `omp parallel` through
    // the OpenMPIRBuilder for now.
    if (S.getCancelRegion() == OMPD_parallel ||
        S.getCancelRegion() == OMPD_sections ||
        S.getCancelRegion() == OMPD_section) {
      llvm::Value *IfCondition = nullptr;
      if (IfCond)
        IfCondition = EmitScalarExpr(IfCond,
                                     /*IgnoreResultAssign=*/true);
      return Builder.restoreIP(
          OMPBuilder.createCancel(Builder, IfCondition, S.getCancelRegion()));
    }
  }

  CGM.getOpenMPRuntime().emitCancelCall(*this, S.getBeginLoc(), IfCond,
                                        S.getCancelRegion());
}

CodeGenFunction::JumpDest
CodeGenFunction::getOMPCancelDestination(OpenMPDirectiveKind Kind) {
  if (Kind == OMPD_parallel || Kind == OMPD_task ||
      Kind == OMPD_target_parallel || Kind == OMPD_taskloop ||
      Kind == OMPD_master_taskloop || Kind == OMPD_parallel_master_taskloop)
    return ReturnBlock;
  assert(Kind == OMPD_for || Kind == OMPD_section || Kind == OMPD_sections ||
         Kind == OMPD_parallel_sections || Kind == OMPD_parallel_for ||
         Kind == OMPD_distribute_parallel_for ||
         Kind == OMPD_target_parallel_for ||
         Kind == OMPD_teams_distribute_parallel_for ||
         Kind == OMPD_target_teams_distribute_parallel_for);
  return OMPCancelStack.getExitBlock();
}

void CodeGenFunction::EmitOMPUseDevicePtrClause(
    const OMPUseDevicePtrClause &C, OMPPrivateScope &PrivateScope,
    const llvm::DenseMap<const ValueDecl *, llvm::Value *>
        CaptureDeviceAddrMap) {
  llvm::SmallDenseSet<CanonicalDeclPtr<const Decl>, 4> Processed;
  for (const Expr *OrigVarIt : C.varlists()) {
    const auto *OrigVD = cast<VarDecl>(cast<DeclRefExpr>(OrigVarIt)->getDecl());
    if (!Processed.insert(OrigVD).second)
      continue;

    // In order to identify the right initializer we need to match the
    // declaration used by the mapping logic. In some cases we may get
    // OMPCapturedExprDecl that refers to the original declaration.
    const ValueDecl *MatchingVD = OrigVD;
    if (const auto *OED = dyn_cast<OMPCapturedExprDecl>(MatchingVD)) {
      // OMPCapturedExprDecl are used to privative fields of the current
      // structure.
      const auto *ME = cast<MemberExpr>(OED->getInit());
      assert(isa<CXXThisExpr>(ME->getBase()->IgnoreImpCasts()) &&
             "Base should be the current struct!");
      MatchingVD = ME->getMemberDecl();
    }

    // If we don't have information about the current list item, move on to
    // the next one.
    auto InitAddrIt = CaptureDeviceAddrMap.find(MatchingVD);
    if (InitAddrIt == CaptureDeviceAddrMap.end())
      continue;

    llvm::Type *Ty = ConvertTypeForMem(OrigVD->getType().getNonReferenceType());

    // Return the address of the private variable.
    bool IsRegistered = PrivateScope.addPrivate(
        OrigVD,
        Address(InitAddrIt->second, Ty,
                getContext().getTypeAlignInChars(getContext().VoidPtrTy)));
    assert(IsRegistered && "firstprivate var already registered as private");
    // Silence the warning about unused variable.
    (void)IsRegistered;
  }
}

static const VarDecl *getBaseDecl(const Expr *Ref) {
  const Expr *Base = Ref->IgnoreParenImpCasts();
  while (const auto *OASE = dyn_cast<ArraySectionExpr>(Base))
    Base = OASE->getBase()->IgnoreParenImpCasts();
  while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Base))
    Base = ASE->getBase()->IgnoreParenImpCasts();
  return cast<VarDecl>(cast<DeclRefExpr>(Base)->getDecl());
}

void CodeGenFunction::EmitOMPUseDeviceAddrClause(
    const OMPUseDeviceAddrClause &C, OMPPrivateScope &PrivateScope,
    const llvm::DenseMap<const ValueDecl *, llvm::Value *>
        CaptureDeviceAddrMap) {
  llvm::SmallDenseSet<CanonicalDeclPtr<const Decl>, 4> Processed;
  for (const Expr *Ref : C.varlists()) {
    const VarDecl *OrigVD = getBaseDecl(Ref);
    if (!Processed.insert(OrigVD).second)
      continue;
    // In order to identify the right initializer we need to match the
    // declaration used by the mapping logic. In some cases we may get
    // OMPCapturedExprDecl that refers to the original declaration.
    const ValueDecl *MatchingVD = OrigVD;
    if (const auto *OED = dyn_cast<OMPCapturedExprDecl>(MatchingVD)) {
      // OMPCapturedExprDecl are used to privative fields of the current
      // structure.
      const auto *ME = cast<MemberExpr>(OED->getInit());
      assert(isa<CXXThisExpr>(ME->getBase()) &&
             "Base should be the current struct!");
      MatchingVD = ME->getMemberDecl();
    }

    // If we don't have information about the current list item, move on to
    // the next one.
    auto InitAddrIt = CaptureDeviceAddrMap.find(MatchingVD);
    if (InitAddrIt == CaptureDeviceAddrMap.end())
      continue;

    llvm::Type *Ty = ConvertTypeForMem(OrigVD->getType().getNonReferenceType());

    Address PrivAddr =
        Address(InitAddrIt->second, Ty,
                getContext().getTypeAlignInChars(getContext().VoidPtrTy));
    // For declrefs and variable length array need to load the pointer for
    // correct mapping, since the pointer to the data was passed to the runtime.
    if (isa<DeclRefExpr>(Ref->IgnoreParenImpCasts()) ||
        MatchingVD->getType()->isArrayType()) {
      QualType PtrTy = getContext().getPointerType(
          OrigVD->getType().getNonReferenceType());
      PrivAddr =
          EmitLoadOfPointer(PrivAddr.withElementType(ConvertTypeForMem(PtrTy)),
                            PtrTy->castAs<PointerType>());
    }

    (void)PrivateScope.addPrivate(OrigVD, PrivAddr);
  }
}

// Generate the instructions for '#pragma omp target data' directive.
void CodeGenFunction::EmitOMPTargetDataDirective(
    const OMPTargetDataDirective &S) {
  CGOpenMPRuntime::TargetDataInfo Info(/*RequiresDevicePointerInfo=*/true,
                                       /*SeparateBeginEndCalls=*/true);

  // Create a pre/post action to signal the privatization of the device pointer.
  // This action can be replaced by the OpenMP runtime code generation to
  // deactivate privatization.
  bool PrivatizeDevicePointers = false;
  class DevicePointerPrivActionTy : public PrePostActionTy {
    bool &PrivatizeDevicePointers;

  public:
    explicit DevicePointerPrivActionTy(bool &PrivatizeDevicePointers)
        : PrivatizeDevicePointers(PrivatizeDevicePointers) {}
    void Enter(CodeGenFunction &CGF) override {
      PrivatizeDevicePointers = true;
    }
  };
  DevicePointerPrivActionTy PrivAction(PrivatizeDevicePointers);

  auto &&CodeGen = [&](CodeGenFunction &CGF, PrePostActionTy &Action) {
    auto &&InnermostCodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
      CGF.EmitStmt(S.getInnermostCapturedStmt()->getCapturedStmt());
    };

    // Codegen that selects whether to generate the privatization code or not.
    auto &&PrivCodeGen = [&](CodeGenFunction &CGF, PrePostActionTy &Action) {
      RegionCodeGenTy RCG(InnermostCodeGen);
      PrivatizeDevicePointers = false;

      // Call the pre-action to change the status of PrivatizeDevicePointers if
      // needed.
      Action.Enter(CGF);

      if (PrivatizeDevicePointers) {
        OMPPrivateScope PrivateScope(CGF);
        // Emit all instances of the use_device_ptr clause.
        for (const auto *C : S.getClausesOfKind<OMPUseDevicePtrClause>())
          CGF.EmitOMPUseDevicePtrClause(*C, PrivateScope,
                                        Info.CaptureDeviceAddrMap);
        for (const auto *C : S.getClausesOfKind<OMPUseDeviceAddrClause>())
          CGF.EmitOMPUseDeviceAddrClause(*C, PrivateScope,
                                         Info.CaptureDeviceAddrMap);
        (void)PrivateScope.Privatize();
        RCG(CGF);
      } else {
        // If we don't have target devices, don't bother emitting the data
        // mapping code.
        std::optional<OpenMPDirectiveKind> CaptureRegion;
        if (CGM.getLangOpts().OMPTargetTriples.empty()) {
          // Emit helper decls of the use_device_ptr/use_device_addr clauses.
          for (const auto *C : S.getClausesOfKind<OMPUseDevicePtrClause>())
            for (const Expr *E : C->varlists()) {
              const Decl *D = cast<DeclRefExpr>(E)->getDecl();
              if (const auto *OED = dyn_cast<OMPCapturedExprDecl>(D))
                CGF.EmitVarDecl(*OED);
            }
          for (const auto *C : S.getClausesOfKind<OMPUseDeviceAddrClause>())
            for (const Expr *E : C->varlists()) {
              const Decl *D = getBaseDecl(E);
              if (const auto *OED = dyn_cast<OMPCapturedExprDecl>(D))
                CGF.EmitVarDecl(*OED);
            }
        } else {
          CaptureRegion = OMPD_unknown;
        }

        OMPLexicalScope Scope(CGF, S, CaptureRegion);
        RCG(CGF);
      }
    };

    // Forward the provided action to the privatization codegen.
    RegionCodeGenTy PrivRCG(PrivCodeGen);
    PrivRCG.setAction(Action);

    // Notwithstanding the body of the region is emitted as inlined directive,
    // we don't use an inline scope as changes in the references inside the
    // region are expected to be visible outside, so we do not privative them.
    OMPLexicalScope Scope(CGF, S);
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_target_data,
                                                    PrivRCG);
  };

  RegionCodeGenTy RCG(CodeGen);

  // If we don't have target devices, don't bother emitting the data mapping
  // code.
  if (CGM.getLangOpts().OMPTargetTriples.empty()) {
    RCG(*this);
    return;
  }

  // Check if we have any if clause associated with the directive.
  const Expr *IfCond = nullptr;
  if (const auto *C = S.getSingleClause<OMPIfClause>())
    IfCond = C->getCondition();

  // Check if we have any device clause associated with the directive.
  const Expr *Device = nullptr;
  if (const auto *C = S.getSingleClause<OMPDeviceClause>())
    Device = C->getDevice();

  // Set the action to signal privatization of device pointers.
  RCG.setAction(PrivAction);

  // Emit region code.
  CGM.getOpenMPRuntime().emitTargetDataCalls(*this, S, IfCond, Device, RCG,
                                             Info);
}

void CodeGenFunction::EmitOMPTargetEnterDataDirective(
    const OMPTargetEnterDataDirective &S) {
  // If we don't have target devices, don't bother emitting the data mapping
  // code.
  if (CGM.getLangOpts().OMPTargetTriples.empty())
    return;

  // Check if we have any if clause associated with the directive.
  const Expr *IfCond = nullptr;
  if (const auto *C = S.getSingleClause<OMPIfClause>())
    IfCond = C->getCondition();

  // Check if we have any device clause associated with the directive.
  const Expr *Device = nullptr;
  if (const auto *C = S.getSingleClause<OMPDeviceClause>())
    Device = C->getDevice();

  OMPLexicalScope Scope(*this, S, OMPD_task);
  CGM.getOpenMPRuntime().emitTargetDataStandAloneCall(*this, S, IfCond, Device);
}

void CodeGenFunction::EmitOMPTargetExitDataDirective(
    const OMPTargetExitDataDirective &S) {
  // If we don't have target devices, don't bother emitting the data mapping
  // code.
  if (CGM.getLangOpts().OMPTargetTriples.empty())
    return;

  // Check if we have any if clause associated with the directive.
  const Expr *IfCond = nullptr;
  if (const auto *C = S.getSingleClause<OMPIfClause>())
    IfCond = C->getCondition();

  // Check if we have any device clause associated with the directive.
  const Expr *Device = nullptr;
  if (const auto *C = S.getSingleClause<OMPDeviceClause>())
    Device = C->getDevice();

  OMPLexicalScope Scope(*this, S, OMPD_task);
  CGM.getOpenMPRuntime().emitTargetDataStandAloneCall(*this, S, IfCond, Device);
}

static void emitTargetParallelRegion(CodeGenFunction &CGF,
                                     const OMPTargetParallelDirective &S,
                                     PrePostActionTy &Action) {
  // Get the captured statement associated with the 'parallel' region.
  const CapturedStmt *CS = S.getCapturedStmt(OMPD_parallel);
  Action.Enter(CGF);
  auto &&CodeGen = [&S, CS](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    (void)CGF.EmitOMPFirstprivateClause(S, PrivateScope);
    CGF.EmitOMPPrivateClause(S, PrivateScope);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    if (isOpenMPTargetExecutionDirective(S.getDirectiveKind()))
      CGF.CGM.getOpenMPRuntime().adjustTargetSpecificDataForLambdas(CGF, S);
    // TODO: Add support for clauses.
    CGF.EmitStmt(CS->getCapturedStmt());
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_parallel);
  };
  emitCommonOMPParallelDirective(CGF, S, OMPD_parallel, CodeGen,
                                 emitEmptyBoundParameters);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTargetParallelDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetParallelDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetParallelDirective(
    const OMPTargetParallelDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

static void emitTargetParallelForRegion(CodeGenFunction &CGF,
                                        const OMPTargetParallelForDirective &S,
                                        PrePostActionTy &Action) {
  Action.Enter(CGF);
  // Emit directive as a combined directive that consists of two implicit
  // directives: 'parallel' with 'for' directive.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPCancelStackRAII CancelRegion(
        CGF, OMPD_target_parallel_for, S.hasCancel());
    CGF.EmitOMPWorksharingLoop(S, S.getEnsureUpperBound(), emitForLoopBounds,
                               emitDispatchForLoopBounds);
  };
  emitCommonOMPParallelDirective(CGF, S, OMPD_for, CodeGen,
                                 emitEmptyBoundParameters);
}

void CodeGenFunction::EmitOMPTargetParallelForDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetParallelForDirective &S) {
  // Emit SPMD target parallel for region as a standalone region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelForRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetParallelForDirective(
    const OMPTargetParallelForDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelForRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

static void
emitTargetParallelForSimdRegion(CodeGenFunction &CGF,
                                const OMPTargetParallelForSimdDirective &S,
                                PrePostActionTy &Action) {
  Action.Enter(CGF);
  // Emit directive as a combined directive that consists of two implicit
  // directives: 'parallel' with 'for' directive.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CGF.EmitOMPWorksharingLoop(S, S.getEnsureUpperBound(), emitForLoopBounds,
                               emitDispatchForLoopBounds);
  };
  emitCommonOMPParallelDirective(CGF, S, OMPD_simd, CodeGen,
                                 emitEmptyBoundParameters);
}

void CodeGenFunction::EmitOMPTargetParallelForSimdDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetParallelForSimdDirective &S) {
  // Emit SPMD target parallel for region as a standalone region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelForSimdRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

void CodeGenFunction::EmitOMPTargetParallelForSimdDirective(
    const OMPTargetParallelForSimdDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelForSimdRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

/// Emit a helper variable and return corresponding lvalue.
static void mapParam(CodeGenFunction &CGF, const DeclRefExpr *Helper,
                     const ImplicitParamDecl *PVD,
                     CodeGenFunction::OMPPrivateScope &Privates) {
  const auto *VDecl = cast<VarDecl>(Helper->getDecl());
  Privates.addPrivate(VDecl, CGF.GetAddrOfLocalVar(PVD));
}

void CodeGenFunction::EmitOMPTaskLoopBasedDirective(const OMPLoopDirective &S) {
  assert(isOpenMPTaskLoopDirective(S.getDirectiveKind()));
  // Emit outlined function for task construct.
  const CapturedStmt *CS = S.getCapturedStmt(OMPD_taskloop);
  Address CapturedStruct = Address::invalid();
  {
    OMPLexicalScope Scope(*this, S, OMPD_taskloop, /*EmitPreInitStmt=*/false);
    CapturedStruct = GenerateCapturedStmtArgument(*CS);
  }
  QualType SharedsTy = getContext().getRecordType(CS->getCapturedRecordDecl());
  const Expr *IfCond = nullptr;
  for (const auto *C : S.getClausesOfKind<OMPIfClause>()) {
    if (C->getNameModifier() == OMPD_unknown ||
        C->getNameModifier() == OMPD_taskloop) {
      IfCond = C->getCondition();
      break;
    }
  }

  OMPTaskDataTy Data;
  // Check if taskloop must be emitted without taskgroup.
  Data.Nogroup = S.getSingleClause<OMPNogroupClause>();
  // TODO: Check if we should emit tied or untied task.
  Data.Tied = true;
  // Set scheduling for taskloop
  if (const auto *Clause = S.getSingleClause<OMPGrainsizeClause>()) {
    // grainsize clause
    Data.Schedule.setInt(/*IntVal=*/false);
    Data.Schedule.setPointer(EmitScalarExpr(Clause->getGrainsize()));
  } else if (const auto *Clause = S.getSingleClause<OMPNumTasksClause>()) {
    // num_tasks clause
    Data.Schedule.setInt(/*IntVal=*/true);
    Data.Schedule.setPointer(EmitScalarExpr(Clause->getNumTasks()));
  }

  auto &&BodyGen = [CS, &S](CodeGenFunction &CGF, PrePostActionTy &) {
    // if (PreCond) {
    //   for (IV in 0..LastIteration) BODY;
    //   <Final counter/linear vars updates>;
    // }
    //

    // Emit: if (PreCond) - begin.
    // If the condition constant folds and can be elided, avoid emitting the
    // whole loop.
    bool CondConstant;
    llvm::BasicBlock *ContBlock = nullptr;
    OMPLoopScope PreInitScope(CGF, S);
    if (CGF.ConstantFoldsToSimpleInteger(S.getPreCond(), CondConstant)) {
      if (!CondConstant)
        return;
    } else {
      llvm::BasicBlock *ThenBlock = CGF.createBasicBlock("taskloop.if.then");
      ContBlock = CGF.createBasicBlock("taskloop.if.end");
      emitPreCond(CGF, S, S.getPreCond(), ThenBlock, ContBlock,
                  CGF.getProfileCount(&S));
      CGF.EmitBlock(ThenBlock);
      CGF.incrementProfileCounter(&S);
    }

    (void)CGF.EmitOMPLinearClauseInit(S);

    OMPPrivateScope LoopScope(CGF);
    // Emit helper vars inits.
    enum { LowerBound = 5, UpperBound, Stride, LastIter };
    auto *I = CS->getCapturedDecl()->param_begin();
    auto *LBP = std::next(I, LowerBound);
    auto *UBP = std::next(I, UpperBound);
    auto *STP = std::next(I, Stride);
    auto *LIP = std::next(I, LastIter);
    mapParam(CGF, cast<DeclRefExpr>(S.getLowerBoundVariable()), *LBP,
             LoopScope);
    mapParam(CGF, cast<DeclRefExpr>(S.getUpperBoundVariable()), *UBP,
             LoopScope);
    mapParam(CGF, cast<DeclRefExpr>(S.getStrideVariable()), *STP, LoopScope);
    mapParam(CGF, cast<DeclRefExpr>(S.getIsLastIterVariable()), *LIP,
             LoopScope);
    CGF.EmitOMPPrivateLoopCounters(S, LoopScope);
    CGF.EmitOMPLinearClause(S, LoopScope);
    bool HasLastprivateClause = CGF.EmitOMPLastprivateClauseInit(S, LoopScope);
    (void)LoopScope.Privatize();
    // Emit the loop iteration variable.
    const Expr *IVExpr = S.getIterationVariable();
    const auto *IVDecl = cast<VarDecl>(cast<DeclRefExpr>(IVExpr)->getDecl());
    CGF.EmitVarDecl(*IVDecl);
    CGF.EmitIgnoredExpr(S.getInit());

    // Emit the iterations count variable.
    // If it is not a variable, Sema decided to calculate iterations count on
    // each iteration (e.g., it is foldable into a constant).
    if (const auto *LIExpr = dyn_cast<DeclRefExpr>(S.getLastIteration())) {
      CGF.EmitVarDecl(*cast<VarDecl>(LIExpr->getDecl()));
      // Emit calculation of the iterations count.
      CGF.EmitIgnoredExpr(S.getCalcLastIteration());
    }

    {
      OMPLexicalScope Scope(CGF, S, OMPD_taskloop, /*EmitPreInitStmt=*/false);
      emitCommonSimdLoop(
          CGF, S,
          [&S](CodeGenFunction &CGF, PrePostActionTy &) {
            if (isOpenMPSimdDirective(S.getDirectiveKind()))
              CGF.EmitOMPSimdInit(S);
          },
          [&S, &LoopScope](CodeGenFunction &CGF, PrePostActionTy &) {
            CGF.EmitOMPInnerLoop(
                S, LoopScope.requiresCleanups(), S.getCond(), S.getInc(),
                [&S](CodeGenFunction &CGF) {
                  emitOMPLoopBodyWithStopPoint(CGF, S,
                                               CodeGenFunction::JumpDest());
                },
                [](CodeGenFunction &) {});
          });
    }
    // Emit: if (PreCond) - end.
    if (ContBlock) {
      CGF.EmitBranch(ContBlock);
      CGF.EmitBlock(ContBlock, true);
    }
    // Emit final copy of the lastprivate variables if IsLastIter != 0.
    if (HasLastprivateClause) {
      CGF.EmitOMPLastprivateClauseFinal(
          S, isOpenMPSimdDirective(S.getDirectiveKind()),
          CGF.Builder.CreateIsNotNull(CGF.EmitLoadOfScalar(
              CGF.GetAddrOfLocalVar(*LIP), /*Volatile=*/false,
              (*LIP)->getType(), S.getBeginLoc())));
    }
    LoopScope.restoreMap();
    CGF.EmitOMPLinearClauseFinal(S, [LIP, &S](CodeGenFunction &CGF) {
      return CGF.Builder.CreateIsNotNull(
          CGF.EmitLoadOfScalar(CGF.GetAddrOfLocalVar(*LIP), /*Volatile=*/false,
                               (*LIP)->getType(), S.getBeginLoc()));
    });
  };
  auto &&TaskGen = [&S, SharedsTy, CapturedStruct,
                    IfCond](CodeGenFunction &CGF, llvm::Function *OutlinedFn,
                            const OMPTaskDataTy &Data) {
    auto &&CodeGen = [&S, OutlinedFn, SharedsTy, CapturedStruct, IfCond,
                      &Data](CodeGenFunction &CGF, PrePostActionTy &) {
      OMPLoopScope PreInitScope(CGF, S);
      CGF.CGM.getOpenMPRuntime().emitTaskLoopCall(CGF, S.getBeginLoc(), S,
                                                  OutlinedFn, SharedsTy,
                                                  CapturedStruct, IfCond, Data);
    };
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_taskloop,
                                                    CodeGen);
  };
  if (Data.Nogroup) {
    EmitOMPTaskBasedDirective(S, OMPD_taskloop, BodyGen, TaskGen, Data);
  } else {
    CGM.getOpenMPRuntime().emitTaskgroupRegion(
        *this,
        [&S, &BodyGen, &TaskGen, &Data](CodeGenFunction &CGF,
                                        PrePostActionTy &Action) {
          Action.Enter(CGF);
          CGF.EmitOMPTaskBasedDirective(S, OMPD_taskloop, BodyGen, TaskGen,
                                        Data);
        },
        S.getBeginLoc());
  }
}

void CodeGenFunction::EmitOMPTaskLoopDirective(const OMPTaskLoopDirective &S) {
  auto LPCRegion =
      CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
  EmitOMPTaskLoopBasedDirective(S);
}

void CodeGenFunction::EmitOMPTaskLoopSimdDirective(
    const OMPTaskLoopSimdDirective &S) {
  auto LPCRegion =
      CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
  OMPLexicalScope Scope(*this, S);
  EmitOMPTaskLoopBasedDirective(S);
}

void CodeGenFunction::EmitOMPMasterTaskLoopDirective(
    const OMPMasterTaskLoopDirective &S) {
  auto &&CodeGen = [this, &S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    EmitOMPTaskLoopBasedDirective(S);
  };
  auto LPCRegion =
      CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
  OMPLexicalScope Scope(*this, S, std::nullopt, /*EmitPreInitStmt=*/false);
  CGM.getOpenMPRuntime().emitMasterRegion(*this, CodeGen, S.getBeginLoc());
}

void CodeGenFunction::EmitOMPMasterTaskLoopSimdDirective(
    const OMPMasterTaskLoopSimdDirective &S) {
  auto &&CodeGen = [this, &S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    EmitOMPTaskLoopBasedDirective(S);
  };
  auto LPCRegion =
      CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
  OMPLexicalScope Scope(*this, S);
  CGM.getOpenMPRuntime().emitMasterRegion(*this, CodeGen, S.getBeginLoc());
}

void CodeGenFunction::EmitOMPParallelMasterTaskLoopDirective(
    const OMPParallelMasterTaskLoopDirective &S) {
  auto &&CodeGen = [this, &S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    auto &&TaskLoopCodeGen = [&S](CodeGenFunction &CGF,
                                  PrePostActionTy &Action) {
      Action.Enter(CGF);
      CGF.EmitOMPTaskLoopBasedDirective(S);
    };
    OMPLexicalScope Scope(CGF, S, OMPD_parallel, /*EmitPreInitStmt=*/false);
    CGM.getOpenMPRuntime().emitMasterRegion(CGF, TaskLoopCodeGen,
                                            S.getBeginLoc());
  };
  auto LPCRegion =
      CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
  emitCommonOMPParallelDirective(*this, S, OMPD_master_taskloop, CodeGen,
                                 emitEmptyBoundParameters);
}

void CodeGenFunction::EmitOMPParallelMasterTaskLoopSimdDirective(
    const OMPParallelMasterTaskLoopSimdDirective &S) {
  auto &&CodeGen = [this, &S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    auto &&TaskLoopCodeGen = [&S](CodeGenFunction &CGF,
                                  PrePostActionTy &Action) {
      Action.Enter(CGF);
      CGF.EmitOMPTaskLoopBasedDirective(S);
    };
    OMPLexicalScope Scope(CGF, S, OMPD_parallel, /*EmitPreInitStmt=*/false);
    CGM.getOpenMPRuntime().emitMasterRegion(CGF, TaskLoopCodeGen,
                                            S.getBeginLoc());
  };
  auto LPCRegion =
      CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
  emitCommonOMPParallelDirective(*this, S, OMPD_master_taskloop_simd, CodeGen,
                                 emitEmptyBoundParameters);
}

// Generate the instructions for '#pragma omp target update' directive.
void CodeGenFunction::EmitOMPTargetUpdateDirective(
    const OMPTargetUpdateDirective &S) {
  // If we don't have target devices, don't bother emitting the data mapping
  // code.
  if (CGM.getLangOpts().OMPTargetTriples.empty())
    return;

  // Check if we have any if clause associated with the directive.
  const Expr *IfCond = nullptr;
  if (const auto *C = S.getSingleClause<OMPIfClause>())
    IfCond = C->getCondition();

  // Check if we have any device clause associated with the directive.
  const Expr *Device = nullptr;
  if (const auto *C = S.getSingleClause<OMPDeviceClause>())
    Device = C->getDevice();

  OMPLexicalScope Scope(*this, S, OMPD_task);
  CGM.getOpenMPRuntime().emitTargetDataStandAloneCall(*this, S, IfCond, Device);
}

void CodeGenFunction::EmitOMPGenericLoopDirective(
    const OMPGenericLoopDirective &S) {
  // Unimplemented, just inline the underlying statement for now.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    // Emit the loop iteration variable.
    const Stmt *CS =
        cast<CapturedStmt>(S.getAssociatedStmt())->getCapturedStmt();
    const auto *ForS = dyn_cast<ForStmt>(CS);
    if (ForS && !isa<DeclStmt>(ForS->getInit())) {
      OMPPrivateScope LoopScope(CGF);
      CGF.EmitOMPPrivateLoopCounters(S, LoopScope);
      (void)LoopScope.Privatize();
      CGF.EmitStmt(CS);
      LoopScope.restoreMap();
    } else {
      CGF.EmitStmt(CS);
    }
  };
  OMPLexicalScope Scope(*this, S, OMPD_unknown);
  CGM.getOpenMPRuntime().emitInlinedDirective(*this, OMPD_loop, CodeGen);
}

void CodeGenFunction::EmitOMPParallelGenericLoopDirective(
    const OMPLoopDirective &S) {
  // Emit combined directive as if its constituent constructs are 'parallel'
  // and 'for'.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    emitOMPCopyinClause(CGF, S);
    (void)emitWorksharingDirective(CGF, S, /*HasCancel=*/false);
  };
  {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, S);
    emitCommonOMPParallelDirective(*this, S, OMPD_for, CodeGen,
                                   emitEmptyBoundParameters);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, S);
}

void CodeGenFunction::EmitOMPTeamsGenericLoopDirective(
    const OMPTeamsGenericLoopDirective &S) {
  // To be consistent with current behavior of 'target teams loop', emit
  // 'teams loop' as if its constituent constructs are 'teams' and 'distribute'.
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(CGF, OMPD_distribute,
                                                    CodeGenDistribute);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  emitCommonOMPTeamsDirective(*this, S, OMPD_distribute, CodeGen);
  emitPostUpdateForReductionClause(*this, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

#ifndef NDEBUG
static void emitTargetTeamsLoopCodegenStatus(CodeGenFunction &CGF,
                                             std::string StatusMsg,
                                             const OMPExecutableDirective &D) {
  bool IsDevice = CGF.CGM.getLangOpts().OpenMPIsTargetDevice;
  if (IsDevice)
    StatusMsg += ": DEVICE";
  else
    StatusMsg += ": HOST";
  SourceLocation L = D.getBeginLoc();
  auto &SM = CGF.getContext().getSourceManager();
  PresumedLoc PLoc = SM.getPresumedLoc(L);
  const char *FileName = PLoc.isValid() ? PLoc.getFilename() : nullptr;
  unsigned LineNo =
      PLoc.isValid() ? PLoc.getLine() : SM.getExpansionLineNumber(L);
  llvm::dbgs() << StatusMsg << ": " << FileName << ": " << LineNo << "\n";
}
#endif

static void emitTargetTeamsGenericLoopRegionAsParallel(
    CodeGenFunction &CGF, PrePostActionTy &Action,
    const OMPTargetTeamsGenericLoopDirective &S) {
  Action.Enter(CGF);
  // Emit 'teams loop' as if its constituent constructs are 'distribute,
  // 'parallel, and 'for'.
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitInnerParallelForWhenCombined,
                              S.getDistInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGenTeams = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                                 PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(
        CGF, OMPD_distribute, CodeGenDistribute, /*HasCancel=*/false);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  DEBUG_WITH_TYPE(TTL_CODEGEN_TYPE,
                  emitTargetTeamsLoopCodegenStatus(
                      CGF, TTL_CODEGEN_TYPE " as parallel for", S));
  emitCommonOMPTeamsDirective(CGF, S, OMPD_distribute_parallel_for,
                              CodeGenTeams);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

static void emitTargetTeamsGenericLoopRegionAsDistribute(
    CodeGenFunction &CGF, PrePostActionTy &Action,
    const OMPTargetTeamsGenericLoopDirective &S) {
  Action.Enter(CGF);
  // Emit 'teams loop' as if its constituent construct is 'distribute'.
  auto &&CodeGenDistribute = [&S](CodeGenFunction &CGF, PrePostActionTy &) {
    CGF.EmitOMPDistributeLoop(S, emitOMPLoopBodyWithStopPoint, S.getInc());
  };

  // Emit teams region as a standalone region.
  auto &&CodeGen = [&S, &CodeGenDistribute](CodeGenFunction &CGF,
                                            PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    CGF.EmitOMPReductionClauseInit(S, PrivateScope);
    (void)PrivateScope.Privatize();
    CGF.CGM.getOpenMPRuntime().emitInlinedDirective(
        CGF, OMPD_distribute, CodeGenDistribute, /*HasCancel=*/false);
    CGF.EmitOMPReductionClauseFinal(S, /*ReductionKind=*/OMPD_teams);
  };
  DEBUG_WITH_TYPE(TTL_CODEGEN_TYPE,
                  emitTargetTeamsLoopCodegenStatus(
                      CGF, TTL_CODEGEN_TYPE " as distribute", S));
  emitCommonOMPTeamsDirective(CGF, S, OMPD_distribute, CodeGen);
  emitPostUpdateForReductionClause(CGF, S,
                                   [](CodeGenFunction &) { return nullptr; });
}

void CodeGenFunction::EmitOMPTargetTeamsGenericLoopDirective(
    const OMPTargetTeamsGenericLoopDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    if (S.canBeParallelFor())
      emitTargetTeamsGenericLoopRegionAsParallel(CGF, Action, S);
    else
      emitTargetTeamsGenericLoopRegionAsDistribute(CGF, Action, S);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

void CodeGenFunction::EmitOMPTargetTeamsGenericLoopDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetTeamsGenericLoopDirective &S) {
  // Emit SPMD target parallel loop region as a standalone region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    if (S.canBeParallelFor())
      emitTargetTeamsGenericLoopRegionAsParallel(CGF, Action, S);
    else
      emitTargetTeamsGenericLoopRegionAsDistribute(CGF, Action, S);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr &&
         "Target device function emission failed for 'target teams loop'.");
}

static void emitTargetParallelGenericLoopRegion(
    CodeGenFunction &CGF, const OMPTargetParallelGenericLoopDirective &S,
    PrePostActionTy &Action) {
  Action.Enter(CGF);
  // Emit as 'parallel for'.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    Action.Enter(CGF);
    CodeGenFunction::OMPCancelStackRAII CancelRegion(
        CGF, OMPD_target_parallel_loop, /*hasCancel=*/false);
    CGF.EmitOMPWorksharingLoop(S, S.getEnsureUpperBound(), emitForLoopBounds,
                               emitDispatchForLoopBounds);
  };
  emitCommonOMPParallelDirective(CGF, S, OMPD_for, CodeGen,
                                 emitEmptyBoundParameters);
}

void CodeGenFunction::EmitOMPTargetParallelGenericLoopDeviceFunction(
    CodeGenModule &CGM, StringRef ParentName,
    const OMPTargetParallelGenericLoopDirective &S) {
  // Emit target parallel loop region as a standalone region.
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelGenericLoopRegion(CGF, S, Action);
  };
  llvm::Function *Fn;
  llvm::Constant *Addr;
  // Emit target region as a standalone region.
  CGM.getOpenMPRuntime().emitTargetOutlinedFunction(
      S, ParentName, Fn, Addr, /*IsOffloadEntry=*/true, CodeGen);
  assert(Fn && Addr && "Target device function emission failed.");
}

/// Emit combined directive 'target parallel loop' as if its constituent
/// constructs are 'target', 'parallel', and 'for'.
void CodeGenFunction::EmitOMPTargetParallelGenericLoopDirective(
    const OMPTargetParallelGenericLoopDirective &S) {
  auto &&CodeGen = [&S](CodeGenFunction &CGF, PrePostActionTy &Action) {
    emitTargetParallelGenericLoopRegion(CGF, S, Action);
  };
  emitCommonOMPTargetDirective(*this, S, CodeGen);
}

void CodeGenFunction::EmitSimpleOMPExecutableDirective(
    const OMPExecutableDirective &D) {
  if (const auto *SD = dyn_cast<OMPScanDirective>(&D)) {
    EmitOMPScanDirective(*SD);
    return;
  }
  if (!D.hasAssociatedStmt() || !D.getAssociatedStmt())
    return;
  auto &&CodeGen = [&D](CodeGenFunction &CGF, PrePostActionTy &Action) {
    OMPPrivateScope GlobalsScope(CGF);
    if (isOpenMPTaskingDirective(D.getDirectiveKind())) {
      // Capture global firstprivates to avoid crash.
      for (const auto *C : D.getClausesOfKind<OMPFirstprivateClause>()) {
        for (const Expr *Ref : C->varlists()) {
          const auto *DRE = cast<DeclRefExpr>(Ref->IgnoreParenImpCasts());
          if (!DRE)
            continue;
          const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
          if (!VD || VD->hasLocalStorage())
            continue;
          if (!CGF.LocalDeclMap.count(VD)) {
            LValue GlobLVal = CGF.EmitLValue(Ref);
            GlobalsScope.addPrivate(VD, GlobLVal.getAddress());
          }
        }
      }
    }
    if (isOpenMPSimdDirective(D.getDirectiveKind())) {
      (void)GlobalsScope.Privatize();
      ParentLoopDirectiveForScanRegion ScanRegion(CGF, D);
      emitOMPSimdRegion(CGF, cast<OMPLoopDirective>(D), Action);
    } else {
      if (const auto *LD = dyn_cast<OMPLoopDirective>(&D)) {
        for (const Expr *E : LD->counters()) {
          const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
          if (!VD->hasLocalStorage() && !CGF.LocalDeclMap.count(VD)) {
            LValue GlobLVal = CGF.EmitLValue(E);
            GlobalsScope.addPrivate(VD, GlobLVal.getAddress());
          }
          if (isa<OMPCapturedExprDecl>(VD)) {
            // Emit only those that were not explicitly referenced in clauses.
            if (!CGF.LocalDeclMap.count(VD))
              CGF.EmitVarDecl(*VD);
          }
        }
        for (const auto *C : D.getClausesOfKind<OMPOrderedClause>()) {
          if (!C->getNumForLoops())
            continue;
          for (unsigned I = LD->getLoopsNumber(),
                        E = C->getLoopNumIterations().size();
               I < E; ++I) {
            if (const auto *VD = dyn_cast<OMPCapturedExprDecl>(
                    cast<DeclRefExpr>(C->getLoopCounter(I))->getDecl())) {
              // Emit only those that were not explicitly referenced in clauses.
              if (!CGF.LocalDeclMap.count(VD))
                CGF.EmitVarDecl(*VD);
            }
          }
        }
      }
      (void)GlobalsScope.Privatize();
      CGF.EmitStmt(D.getInnermostCapturedStmt()->getCapturedStmt());
    }
  };
  if (D.getDirectiveKind() == OMPD_atomic ||
      D.getDirectiveKind() == OMPD_critical ||
      D.getDirectiveKind() == OMPD_section ||
      D.getDirectiveKind() == OMPD_master ||
      D.getDirectiveKind() == OMPD_masked ||
      D.getDirectiveKind() == OMPD_unroll) {
    EmitStmt(D.getAssociatedStmt());
  } else {
    auto LPCRegion =
        CGOpenMPRuntime::LastprivateConditionalRAII::disable(*this, D);
    OMPSimdLexicalScope Scope(*this, D);
    CGM.getOpenMPRuntime().emitInlinedDirective(
        *this,
        isOpenMPSimdDirective(D.getDirectiveKind()) ? OMPD_simd
                                                    : D.getDirectiveKind(),
        CodeGen);
  }
  // Check for outer lastprivate conditional update.
  checkForLastprivateConditionalUpdate(*this, D);
}
