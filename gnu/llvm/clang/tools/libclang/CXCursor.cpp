//===- CXCursor.cpp - Routines for manipulating CXCursors -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines routines for manipulating CXCursors. It should be the
// only file that has internal knowledge of the encoding of the data in
// CXCursor.
//
//===----------------------------------------------------------------------===//

#include "CXCursor.h"
#include "CXString.h"
#include "CXTranslationUnit.h"
#include "CXType.h"
#include "clang-c/Index.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Frontend/ASTUnit.h"
#include "llvm/Support/ErrorHandling.h"

using namespace clang;
using namespace cxcursor;

CXCursor cxcursor::MakeCXCursorInvalid(CXCursorKind K, CXTranslationUnit TU) {
  assert(K >= CXCursor_FirstInvalid && K <= CXCursor_LastInvalid);
  CXCursor C = {K, 0, {nullptr, nullptr, TU}};
  return C;
}

static CXCursorKind GetCursorKind(const Attr *A) {
  assert(A && "Invalid arguments!");
  switch (A->getKind()) {
  default:
    break;
  case attr::IBAction:
    return CXCursor_IBActionAttr;
  case attr::IBOutlet:
    return CXCursor_IBOutletAttr;
  case attr::IBOutletCollection:
    return CXCursor_IBOutletCollectionAttr;
  case attr::Final:
    return CXCursor_CXXFinalAttr;
  case attr::Override:
    return CXCursor_CXXOverrideAttr;
  case attr::Annotate:
    return CXCursor_AnnotateAttr;
  case attr::AsmLabel:
    return CXCursor_AsmLabelAttr;
  case attr::Packed:
    return CXCursor_PackedAttr;
  case attr::Pure:
    return CXCursor_PureAttr;
  case attr::Const:
    return CXCursor_ConstAttr;
  case attr::NoDuplicate:
    return CXCursor_NoDuplicateAttr;
  case attr::CUDAConstant:
    return CXCursor_CUDAConstantAttr;
  case attr::CUDADevice:
    return CXCursor_CUDADeviceAttr;
  case attr::CUDAGlobal:
    return CXCursor_CUDAGlobalAttr;
  case attr::CUDAHost:
    return CXCursor_CUDAHostAttr;
  case attr::CUDAShared:
    return CXCursor_CUDASharedAttr;
  case attr::Visibility:
    return CXCursor_VisibilityAttr;
  case attr::DLLExport:
    return CXCursor_DLLExport;
  case attr::DLLImport:
    return CXCursor_DLLImport;
  case attr::NSReturnsRetained:
    return CXCursor_NSReturnsRetained;
  case attr::NSReturnsNotRetained:
    return CXCursor_NSReturnsNotRetained;
  case attr::NSReturnsAutoreleased:
    return CXCursor_NSReturnsAutoreleased;
  case attr::NSConsumesSelf:
    return CXCursor_NSConsumesSelf;
  case attr::NSConsumed:
    return CXCursor_NSConsumed;
  case attr::ObjCException:
    return CXCursor_ObjCException;
  case attr::ObjCNSObject:
    return CXCursor_ObjCNSObject;
  case attr::ObjCIndependentClass:
    return CXCursor_ObjCIndependentClass;
  case attr::ObjCPreciseLifetime:
    return CXCursor_ObjCPreciseLifetime;
  case attr::ObjCReturnsInnerPointer:
    return CXCursor_ObjCReturnsInnerPointer;
  case attr::ObjCRequiresSuper:
    return CXCursor_ObjCRequiresSuper;
  case attr::ObjCRootClass:
    return CXCursor_ObjCRootClass;
  case attr::ObjCSubclassingRestricted:
    return CXCursor_ObjCSubclassingRestricted;
  case attr::ObjCExplicitProtocolImpl:
    return CXCursor_ObjCExplicitProtocolImpl;
  case attr::ObjCDesignatedInitializer:
    return CXCursor_ObjCDesignatedInitializer;
  case attr::ObjCRuntimeVisible:
    return CXCursor_ObjCRuntimeVisible;
  case attr::ObjCBoxable:
    return CXCursor_ObjCBoxable;
  case attr::FlagEnum:
    return CXCursor_FlagEnum;
  case attr::Convergent:
    return CXCursor_ConvergentAttr;
  case attr::WarnUnused:
    return CXCursor_WarnUnusedAttr;
  case attr::WarnUnusedResult:
    return CXCursor_WarnUnusedResultAttr;
  case attr::Aligned:
    return CXCursor_AlignedAttr;
  }

  return CXCursor_UnexposedAttr;
}

CXCursor cxcursor::MakeCXCursor(const Attr *A, const Decl *Parent,
                                CXTranslationUnit TU) {
  assert(A && Parent && TU && "Invalid arguments!");
  CXCursor C = {GetCursorKind(A), 0, {Parent, A, TU}};
  return C;
}

CXCursor cxcursor::MakeCXCursor(const Decl *D, CXTranslationUnit TU,
                                SourceRange RegionOfInterest,
                                bool FirstInDeclGroup) {
  assert(D && TU && "Invalid arguments!");

  CXCursorKind K = getCursorKindForDecl(D);

  if (K == CXCursor_ObjCClassMethodDecl ||
      K == CXCursor_ObjCInstanceMethodDecl) {
    int SelectorIdIndex = -1;
    // Check if cursor points to a selector id.
    if (RegionOfInterest.isValid() &&
        RegionOfInterest.getBegin() == RegionOfInterest.getEnd()) {
      SmallVector<SourceLocation, 16> SelLocs;
      cast<ObjCMethodDecl>(D)->getSelectorLocs(SelLocs);
      SmallVectorImpl<SourceLocation>::iterator I =
          llvm::find(SelLocs, RegionOfInterest.getBegin());
      if (I != SelLocs.end())
        SelectorIdIndex = I - SelLocs.begin();
    }
    CXCursor C = {K,
                  SelectorIdIndex,
                  {D, (void *)(intptr_t)(FirstInDeclGroup ? 1 : 0), TU}};
    return C;
  }

  CXCursor C = {K, 0, {D, (void *)(intptr_t)(FirstInDeclGroup ? 1 : 0), TU}};
  return C;
}

CXCursor cxcursor::MakeCXCursor(const Stmt *S, const Decl *Parent,
                                CXTranslationUnit TU,
                                SourceRange RegionOfInterest) {
  assert(S && TU && "Invalid arguments!");
  CXCursorKind K = CXCursor_NotImplemented;

  switch (S->getStmtClass()) {
  case Stmt::NoStmtClass:
    break;

  case Stmt::CaseStmtClass:
    K = CXCursor_CaseStmt;
    break;

  case Stmt::DefaultStmtClass:
    K = CXCursor_DefaultStmt;
    break;

  case Stmt::IfStmtClass:
    K = CXCursor_IfStmt;
    break;

  case Stmt::SwitchStmtClass:
    K = CXCursor_SwitchStmt;
    break;

  case Stmt::WhileStmtClass:
    K = CXCursor_WhileStmt;
    break;

  case Stmt::DoStmtClass:
    K = CXCursor_DoStmt;
    break;

  case Stmt::ForStmtClass:
    K = CXCursor_ForStmt;
    break;

  case Stmt::GotoStmtClass:
    K = CXCursor_GotoStmt;
    break;

  case Stmt::IndirectGotoStmtClass:
    K = CXCursor_IndirectGotoStmt;
    break;

  case Stmt::ContinueStmtClass:
    K = CXCursor_ContinueStmt;
    break;

  case Stmt::BreakStmtClass:
    K = CXCursor_BreakStmt;
    break;

  case Stmt::ReturnStmtClass:
    K = CXCursor_ReturnStmt;
    break;

  case Stmt::GCCAsmStmtClass:
    K = CXCursor_GCCAsmStmt;
    break;

  case Stmt::MSAsmStmtClass:
    K = CXCursor_MSAsmStmt;
    break;

  case Stmt::ObjCAtTryStmtClass:
    K = CXCursor_ObjCAtTryStmt;
    break;

  case Stmt::ObjCAtCatchStmtClass:
    K = CXCursor_ObjCAtCatchStmt;
    break;

  case Stmt::ObjCAtFinallyStmtClass:
    K = CXCursor_ObjCAtFinallyStmt;
    break;

  case Stmt::ObjCAtThrowStmtClass:
    K = CXCursor_ObjCAtThrowStmt;
    break;

  case Stmt::ObjCAtSynchronizedStmtClass:
    K = CXCursor_ObjCAtSynchronizedStmt;
    break;

  case Stmt::ObjCAutoreleasePoolStmtClass:
    K = CXCursor_ObjCAutoreleasePoolStmt;
    break;

  case Stmt::ObjCForCollectionStmtClass:
    K = CXCursor_ObjCForCollectionStmt;
    break;

  case Stmt::CXXCatchStmtClass:
    K = CXCursor_CXXCatchStmt;
    break;

  case Stmt::CXXTryStmtClass:
    K = CXCursor_CXXTryStmt;
    break;

  case Stmt::CXXForRangeStmtClass:
    K = CXCursor_CXXForRangeStmt;
    break;

  case Stmt::SEHTryStmtClass:
    K = CXCursor_SEHTryStmt;
    break;

  case Stmt::SEHExceptStmtClass:
    K = CXCursor_SEHExceptStmt;
    break;

  case Stmt::SEHFinallyStmtClass:
    K = CXCursor_SEHFinallyStmt;
    break;

  case Stmt::SEHLeaveStmtClass:
    K = CXCursor_SEHLeaveStmt;
    break;

  case Stmt::CoroutineBodyStmtClass:
  case Stmt::CoreturnStmtClass:
    K = CXCursor_UnexposedStmt;
    break;

  case Stmt::ArrayTypeTraitExprClass:
  case Stmt::AsTypeExprClass:
  case Stmt::AtomicExprClass:
  case Stmt::BinaryConditionalOperatorClass:
  case Stmt::TypeTraitExprClass:
  case Stmt::CoawaitExprClass:
  case Stmt::DependentCoawaitExprClass:
  case Stmt::CoyieldExprClass:
  case Stmt::CXXBindTemporaryExprClass:
  case Stmt::CXXDefaultArgExprClass:
  case Stmt::CXXDefaultInitExprClass:
  case Stmt::CXXFoldExprClass:
  case Stmt::CXXRewrittenBinaryOperatorClass:
  case Stmt::CXXStdInitializerListExprClass:
  case Stmt::CXXScalarValueInitExprClass:
  case Stmt::CXXUuidofExprClass:
  case Stmt::ChooseExprClass:
  case Stmt::DesignatedInitExprClass:
  case Stmt::DesignatedInitUpdateExprClass:
  case Stmt::ArrayInitLoopExprClass:
  case Stmt::ArrayInitIndexExprClass:
  case Stmt::ExprWithCleanupsClass:
  case Stmt::ExpressionTraitExprClass:
  case Stmt::ExtVectorElementExprClass:
  case Stmt::ImplicitCastExprClass:
  case Stmt::ImplicitValueInitExprClass:
  case Stmt::NoInitExprClass:
  case Stmt::MaterializeTemporaryExprClass:
  case Stmt::ObjCIndirectCopyRestoreExprClass:
  case Stmt::OffsetOfExprClass:
  case Stmt::ParenListExprClass:
  case Stmt::PredefinedExprClass:
  case Stmt::ShuffleVectorExprClass:
  case Stmt::SourceLocExprClass:
  case Stmt::ConvertVectorExprClass:
  case Stmt::VAArgExprClass:
  case Stmt::ObjCArrayLiteralClass:
  case Stmt::ObjCDictionaryLiteralClass:
  case Stmt::ObjCBoxedExprClass:
  case Stmt::ObjCSubscriptRefExprClass:
  case Stmt::RecoveryExprClass:
  case Stmt::SYCLUniqueStableNameExprClass:
  case Stmt::EmbedExprClass:
    K = CXCursor_UnexposedExpr;
    break;

  case Stmt::OpaqueValueExprClass:
    if (Expr *Src = cast<OpaqueValueExpr>(S)->getSourceExpr())
      return MakeCXCursor(Src, Parent, TU, RegionOfInterest);
    K = CXCursor_UnexposedExpr;
    break;

  case Stmt::PseudoObjectExprClass:
    return MakeCXCursor(cast<PseudoObjectExpr>(S)->getSyntacticForm(), Parent,
                        TU, RegionOfInterest);

  case Stmt::CompoundStmtClass:
    K = CXCursor_CompoundStmt;
    break;

  case Stmt::NullStmtClass:
    K = CXCursor_NullStmt;
    break;

  case Stmt::LabelStmtClass:
    K = CXCursor_LabelStmt;
    break;

  case Stmt::AttributedStmtClass:
    K = CXCursor_UnexposedStmt;
    break;

  case Stmt::DeclStmtClass:
    K = CXCursor_DeclStmt;
    break;

  case Stmt::CapturedStmtClass:
    K = CXCursor_UnexposedStmt;
    break;

  case Stmt::IntegerLiteralClass:
    K = CXCursor_IntegerLiteral;
    break;

  case Stmt::FixedPointLiteralClass:
    K = CXCursor_FixedPointLiteral;
    break;

  case Stmt::FloatingLiteralClass:
    K = CXCursor_FloatingLiteral;
    break;

  case Stmt::ImaginaryLiteralClass:
    K = CXCursor_ImaginaryLiteral;
    break;

  case Stmt::StringLiteralClass:
    K = CXCursor_StringLiteral;
    break;

  case Stmt::CharacterLiteralClass:
    K = CXCursor_CharacterLiteral;
    break;

  case Stmt::ConstantExprClass:
    return MakeCXCursor(cast<ConstantExpr>(S)->getSubExpr(), Parent, TU,
                        RegionOfInterest);

  case Stmt::ParenExprClass:
    K = CXCursor_ParenExpr;
    break;

  case Stmt::UnaryOperatorClass:
    K = CXCursor_UnaryOperator;
    break;

  case Stmt::UnaryExprOrTypeTraitExprClass:
  case Stmt::CXXNoexceptExprClass:
    K = CXCursor_UnaryExpr;
    break;

  case Stmt::MSPropertySubscriptExprClass:
  case Stmt::ArraySubscriptExprClass:
    K = CXCursor_ArraySubscriptExpr;
    break;

  case Stmt::MatrixSubscriptExprClass:
    // TODO: add support for MatrixSubscriptExpr.
    K = CXCursor_UnexposedExpr;
    break;

  case Stmt::ArraySectionExprClass:
    K = CXCursor_ArraySectionExpr;
    break;

  case Stmt::OMPArrayShapingExprClass:
    K = CXCursor_OMPArrayShapingExpr;
    break;

  case Stmt::OMPIteratorExprClass:
    K = CXCursor_OMPIteratorExpr;
    break;

  case Stmt::BinaryOperatorClass:
    K = CXCursor_BinaryOperator;
    break;

  case Stmt::CompoundAssignOperatorClass:
    K = CXCursor_CompoundAssignOperator;
    break;

  case Stmt::ConditionalOperatorClass:
    K = CXCursor_ConditionalOperator;
    break;

  case Stmt::CStyleCastExprClass:
    K = CXCursor_CStyleCastExpr;
    break;

  case Stmt::CompoundLiteralExprClass:
    K = CXCursor_CompoundLiteralExpr;
    break;

  case Stmt::InitListExprClass:
    K = CXCursor_InitListExpr;
    break;

  case Stmt::AddrLabelExprClass:
    K = CXCursor_AddrLabelExpr;
    break;

  case Stmt::StmtExprClass:
    K = CXCursor_StmtExpr;
    break;

  case Stmt::GenericSelectionExprClass:
    K = CXCursor_GenericSelectionExpr;
    break;

  case Stmt::GNUNullExprClass:
    K = CXCursor_GNUNullExpr;
    break;

  case Stmt::CXXStaticCastExprClass:
    K = CXCursor_CXXStaticCastExpr;
    break;

  case Stmt::CXXDynamicCastExprClass:
    K = CXCursor_CXXDynamicCastExpr;
    break;

  case Stmt::CXXReinterpretCastExprClass:
    K = CXCursor_CXXReinterpretCastExpr;
    break;

  case Stmt::CXXConstCastExprClass:
    K = CXCursor_CXXConstCastExpr;
    break;

  case Stmt::CXXFunctionalCastExprClass:
    K = CXCursor_CXXFunctionalCastExpr;
    break;

  case Stmt::CXXAddrspaceCastExprClass:
    K = CXCursor_CXXAddrspaceCastExpr;
    break;

  case Stmt::CXXTypeidExprClass:
    K = CXCursor_CXXTypeidExpr;
    break;

  case Stmt::CXXBoolLiteralExprClass:
    K = CXCursor_CXXBoolLiteralExpr;
    break;

  case Stmt::CXXNullPtrLiteralExprClass:
    K = CXCursor_CXXNullPtrLiteralExpr;
    break;

  case Stmt::CXXThisExprClass:
    K = CXCursor_CXXThisExpr;
    break;

  case Stmt::CXXThrowExprClass:
    K = CXCursor_CXXThrowExpr;
    break;

  case Stmt::CXXNewExprClass:
    K = CXCursor_CXXNewExpr;
    break;

  case Stmt::CXXDeleteExprClass:
    K = CXCursor_CXXDeleteExpr;
    break;

  case Stmt::ObjCStringLiteralClass:
    K = CXCursor_ObjCStringLiteral;
    break;

  case Stmt::ObjCEncodeExprClass:
    K = CXCursor_ObjCEncodeExpr;
    break;

  case Stmt::ObjCSelectorExprClass:
    K = CXCursor_ObjCSelectorExpr;
    break;

  case Stmt::ObjCProtocolExprClass:
    K = CXCursor_ObjCProtocolExpr;
    break;

  case Stmt::ObjCBoolLiteralExprClass:
    K = CXCursor_ObjCBoolLiteralExpr;
    break;

  case Stmt::ObjCAvailabilityCheckExprClass:
    K = CXCursor_ObjCAvailabilityCheckExpr;
    break;

  case Stmt::ObjCBridgedCastExprClass:
    K = CXCursor_ObjCBridgedCastExpr;
    break;

  case Stmt::BlockExprClass:
    K = CXCursor_BlockExpr;
    break;

  case Stmt::PackExpansionExprClass:
    K = CXCursor_PackExpansionExpr;
    break;

  case Stmt::SizeOfPackExprClass:
    K = CXCursor_SizeOfPackExpr;
    break;

  case Stmt::PackIndexingExprClass:
    K = CXCursor_PackIndexingExpr;
    break;

  case Stmt::DeclRefExprClass:
    if (const ImplicitParamDecl *IPD = dyn_cast_or_null<ImplicitParamDecl>(
            cast<DeclRefExpr>(S)->getDecl())) {
      if (const ObjCMethodDecl *MD =
              dyn_cast<ObjCMethodDecl>(IPD->getDeclContext())) {
        if (MD->getSelfDecl() == IPD) {
          K = CXCursor_ObjCSelfExpr;
          break;
        }
      }
    }

    K = CXCursor_DeclRefExpr;
    break;

  case Stmt::DependentScopeDeclRefExprClass:
  case Stmt::SubstNonTypeTemplateParmExprClass:
  case Stmt::SubstNonTypeTemplateParmPackExprClass:
  case Stmt::FunctionParmPackExprClass:
  case Stmt::UnresolvedLookupExprClass:
  case Stmt::TypoExprClass: // A typo could actually be a DeclRef or a MemberRef
    K = CXCursor_DeclRefExpr;
    break;

  case Stmt::CXXDependentScopeMemberExprClass:
  case Stmt::CXXPseudoDestructorExprClass:
  case Stmt::MemberExprClass:
  case Stmt::MSPropertyRefExprClass:
  case Stmt::ObjCIsaExprClass:
  case Stmt::ObjCIvarRefExprClass:
  case Stmt::ObjCPropertyRefExprClass:
  case Stmt::UnresolvedMemberExprClass:
    K = CXCursor_MemberRefExpr;
    break;

  case Stmt::CallExprClass:
  case Stmt::CXXOperatorCallExprClass:
  case Stmt::CXXMemberCallExprClass:
  case Stmt::CUDAKernelCallExprClass:
  case Stmt::CXXConstructExprClass:
  case Stmt::CXXInheritedCtorInitExprClass:
  case Stmt::CXXTemporaryObjectExprClass:
  case Stmt::CXXUnresolvedConstructExprClass:
  case Stmt::UserDefinedLiteralClass:
    K = CXCursor_CallExpr;
    break;

  case Stmt::LambdaExprClass:
    K = CXCursor_LambdaExpr;
    break;

  case Stmt::ObjCMessageExprClass: {
    K = CXCursor_ObjCMessageExpr;
    int SelectorIdIndex = -1;
    // Check if cursor points to a selector id.
    if (RegionOfInterest.isValid() &&
        RegionOfInterest.getBegin() == RegionOfInterest.getEnd()) {
      SmallVector<SourceLocation, 16> SelLocs;
      cast<ObjCMessageExpr>(S)->getSelectorLocs(SelLocs);
      SmallVectorImpl<SourceLocation>::iterator I =
          llvm::find(SelLocs, RegionOfInterest.getBegin());
      if (I != SelLocs.end())
        SelectorIdIndex = I - SelLocs.begin();
    }
    CXCursor C = {K, 0, {Parent, S, TU}};
    return getSelectorIdentifierCursor(SelectorIdIndex, C);
  }

  case Stmt::ConceptSpecializationExprClass:
    K = CXCursor_ConceptSpecializationExpr;
    break;

  case Stmt::RequiresExprClass:
    K = CXCursor_RequiresExpr;
    break;

  case Stmt::CXXParenListInitExprClass:
    K = CXCursor_CXXParenListInitExpr;
    break;

  case Stmt::MSDependentExistsStmtClass:
    K = CXCursor_UnexposedStmt;
    break;
  case Stmt::OMPCanonicalLoopClass:
    K = CXCursor_OMPCanonicalLoop;
    break;
  case Stmt::OMPMetaDirectiveClass:
    K = CXCursor_OMPMetaDirective;
    break;
  case Stmt::OMPParallelDirectiveClass:
    K = CXCursor_OMPParallelDirective;
    break;
  case Stmt::OMPSimdDirectiveClass:
    K = CXCursor_OMPSimdDirective;
    break;
  case Stmt::OMPTileDirectiveClass:
    K = CXCursor_OMPTileDirective;
    break;
  case Stmt::OMPUnrollDirectiveClass:
    K = CXCursor_OMPUnrollDirective;
    break;
  case Stmt::OMPReverseDirectiveClass:
    K = CXCursor_OMPReverseDirective;
    break;
  case Stmt::OMPInterchangeDirectiveClass:
    K = CXCursor_OMPTileDirective;
    break;
  case Stmt::OMPForDirectiveClass:
    K = CXCursor_OMPForDirective;
    break;
  case Stmt::OMPForSimdDirectiveClass:
    K = CXCursor_OMPForSimdDirective;
    break;
  case Stmt::OMPSectionsDirectiveClass:
    K = CXCursor_OMPSectionsDirective;
    break;
  case Stmt::OMPSectionDirectiveClass:
    K = CXCursor_OMPSectionDirective;
    break;
  case Stmt::OMPScopeDirectiveClass:
    K = CXCursor_OMPScopeDirective;
    break;
  case Stmt::OMPSingleDirectiveClass:
    K = CXCursor_OMPSingleDirective;
    break;
  case Stmt::OMPMasterDirectiveClass:
    K = CXCursor_OMPMasterDirective;
    break;
  case Stmt::OMPCriticalDirectiveClass:
    K = CXCursor_OMPCriticalDirective;
    break;
  case Stmt::OMPParallelForDirectiveClass:
    K = CXCursor_OMPParallelForDirective;
    break;
  case Stmt::OMPParallelForSimdDirectiveClass:
    K = CXCursor_OMPParallelForSimdDirective;
    break;
  case Stmt::OMPParallelMasterDirectiveClass:
    K = CXCursor_OMPParallelMasterDirective;
    break;
  case Stmt::OMPParallelMaskedDirectiveClass:
    K = CXCursor_OMPParallelMaskedDirective;
    break;
  case Stmt::OMPParallelSectionsDirectiveClass:
    K = CXCursor_OMPParallelSectionsDirective;
    break;
  case Stmt::OMPTaskDirectiveClass:
    K = CXCursor_OMPTaskDirective;
    break;
  case Stmt::OMPTaskyieldDirectiveClass:
    K = CXCursor_OMPTaskyieldDirective;
    break;
  case Stmt::OMPBarrierDirectiveClass:
    K = CXCursor_OMPBarrierDirective;
    break;
  case Stmt::OMPTaskwaitDirectiveClass:
    K = CXCursor_OMPTaskwaitDirective;
    break;
  case Stmt::OMPErrorDirectiveClass:
    K = CXCursor_OMPErrorDirective;
    break;
  case Stmt::OMPTaskgroupDirectiveClass:
    K = CXCursor_OMPTaskgroupDirective;
    break;
  case Stmt::OMPFlushDirectiveClass:
    K = CXCursor_OMPFlushDirective;
    break;
  case Stmt::OMPDepobjDirectiveClass:
    K = CXCursor_OMPDepobjDirective;
    break;
  case Stmt::OMPScanDirectiveClass:
    K = CXCursor_OMPScanDirective;
    break;
  case Stmt::OMPOrderedDirectiveClass:
    K = CXCursor_OMPOrderedDirective;
    break;
  case Stmt::OMPAtomicDirectiveClass:
    K = CXCursor_OMPAtomicDirective;
    break;
  case Stmt::OMPTargetDirectiveClass:
    K = CXCursor_OMPTargetDirective;
    break;
  case Stmt::OMPTargetDataDirectiveClass:
    K = CXCursor_OMPTargetDataDirective;
    break;
  case Stmt::OMPTargetEnterDataDirectiveClass:
    K = CXCursor_OMPTargetEnterDataDirective;
    break;
  case Stmt::OMPTargetExitDataDirectiveClass:
    K = CXCursor_OMPTargetExitDataDirective;
    break;
  case Stmt::OMPTargetParallelDirectiveClass:
    K = CXCursor_OMPTargetParallelDirective;
    break;
  case Stmt::OMPTargetParallelForDirectiveClass:
    K = CXCursor_OMPTargetParallelForDirective;
    break;
  case Stmt::OMPTargetUpdateDirectiveClass:
    K = CXCursor_OMPTargetUpdateDirective;
    break;
  case Stmt::OMPTeamsDirectiveClass:
    K = CXCursor_OMPTeamsDirective;
    break;
  case Stmt::OMPCancellationPointDirectiveClass:
    K = CXCursor_OMPCancellationPointDirective;
    break;
  case Stmt::OMPCancelDirectiveClass:
    K = CXCursor_OMPCancelDirective;
    break;
  case Stmt::OMPTaskLoopDirectiveClass:
    K = CXCursor_OMPTaskLoopDirective;
    break;
  case Stmt::OMPTaskLoopSimdDirectiveClass:
    K = CXCursor_OMPTaskLoopSimdDirective;
    break;
  case Stmt::OMPMasterTaskLoopDirectiveClass:
    K = CXCursor_OMPMasterTaskLoopDirective;
    break;
  case Stmt::OMPMaskedTaskLoopDirectiveClass:
    K = CXCursor_OMPMaskedTaskLoopDirective;
    break;
  case Stmt::OMPMasterTaskLoopSimdDirectiveClass:
    K = CXCursor_OMPMasterTaskLoopSimdDirective;
    break;
  case Stmt::OMPMaskedTaskLoopSimdDirectiveClass:
    K = CXCursor_OMPMaskedTaskLoopSimdDirective;
    break;
  case Stmt::OMPParallelMasterTaskLoopDirectiveClass:
    K = CXCursor_OMPParallelMasterTaskLoopDirective;
    break;
  case Stmt::OMPParallelMaskedTaskLoopDirectiveClass:
    K = CXCursor_OMPParallelMaskedTaskLoopDirective;
    break;
  case Stmt::OMPParallelMasterTaskLoopSimdDirectiveClass:
    K = CXCursor_OMPParallelMasterTaskLoopSimdDirective;
    break;
  case Stmt::OMPParallelMaskedTaskLoopSimdDirectiveClass:
    K = CXCursor_OMPParallelMaskedTaskLoopSimdDirective;
    break;
  case Stmt::OMPDistributeDirectiveClass:
    K = CXCursor_OMPDistributeDirective;
    break;
  case Stmt::OMPDistributeParallelForDirectiveClass:
    K = CXCursor_OMPDistributeParallelForDirective;
    break;
  case Stmt::OMPDistributeParallelForSimdDirectiveClass:
    K = CXCursor_OMPDistributeParallelForSimdDirective;
    break;
  case Stmt::OMPDistributeSimdDirectiveClass:
    K = CXCursor_OMPDistributeSimdDirective;
    break;
  case Stmt::OMPTargetParallelForSimdDirectiveClass:
    K = CXCursor_OMPTargetParallelForSimdDirective;
    break;
  case Stmt::OMPTargetSimdDirectiveClass:
    K = CXCursor_OMPTargetSimdDirective;
    break;
  case Stmt::OMPTeamsDistributeDirectiveClass:
    K = CXCursor_OMPTeamsDistributeDirective;
    break;
  case Stmt::OMPTeamsDistributeSimdDirectiveClass:
    K = CXCursor_OMPTeamsDistributeSimdDirective;
    break;
  case Stmt::OMPTeamsDistributeParallelForSimdDirectiveClass:
    K = CXCursor_OMPTeamsDistributeParallelForSimdDirective;
    break;
  case Stmt::OMPTeamsDistributeParallelForDirectiveClass:
    K = CXCursor_OMPTeamsDistributeParallelForDirective;
    break;
  case Stmt::OMPTargetTeamsDirectiveClass:
    K = CXCursor_OMPTargetTeamsDirective;
    break;
  case Stmt::OMPTargetTeamsDistributeDirectiveClass:
    K = CXCursor_OMPTargetTeamsDistributeDirective;
    break;
  case Stmt::OMPTargetTeamsDistributeParallelForDirectiveClass:
    K = CXCursor_OMPTargetTeamsDistributeParallelForDirective;
    break;
  case Stmt::OMPTargetTeamsDistributeParallelForSimdDirectiveClass:
    K = CXCursor_OMPTargetTeamsDistributeParallelForSimdDirective;
    break;
  case Stmt::OMPTargetTeamsDistributeSimdDirectiveClass:
    K = CXCursor_OMPTargetTeamsDistributeSimdDirective;
    break;
  case Stmt::OMPInteropDirectiveClass:
    K = CXCursor_OMPInteropDirective;
    break;
  case Stmt::OMPDispatchDirectiveClass:
    K = CXCursor_OMPDispatchDirective;
    break;
  case Stmt::OMPMaskedDirectiveClass:
    K = CXCursor_OMPMaskedDirective;
    break;
  case Stmt::OMPGenericLoopDirectiveClass:
    K = CXCursor_OMPGenericLoopDirective;
    break;
  case Stmt::OMPTeamsGenericLoopDirectiveClass:
    K = CXCursor_OMPTeamsGenericLoopDirective;
    break;
  case Stmt::OMPTargetTeamsGenericLoopDirectiveClass:
    K = CXCursor_OMPTargetTeamsGenericLoopDirective;
    break;
  case Stmt::OMPParallelGenericLoopDirectiveClass:
    K = CXCursor_OMPParallelGenericLoopDirective;
    break;
  case Stmt::OpenACCComputeConstructClass:
    K = CXCursor_OpenACCComputeConstruct;
    break;
  case Stmt::OpenACCLoopConstructClass:
    K = CXCursor_OpenACCLoopConstruct;
    break;
  case Stmt::OMPTargetParallelGenericLoopDirectiveClass:
    K = CXCursor_OMPTargetParallelGenericLoopDirective;
    break;
  case Stmt::BuiltinBitCastExprClass:
    K = CXCursor_BuiltinBitCastExpr;
  }

  CXCursor C = {K, 0, {Parent, S, TU}};
  return C;
}

CXCursor cxcursor::MakeCursorObjCSuperClassRef(ObjCInterfaceDecl *Super,
                                               SourceLocation Loc,
                                               CXTranslationUnit TU) {
  assert(Super && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_ObjCSuperClassRef, 0, {Super, RawLoc, TU}};
  return C;
}

std::pair<const ObjCInterfaceDecl *, SourceLocation>
cxcursor::getCursorObjCSuperClassRef(CXCursor C) {
  assert(C.kind == CXCursor_ObjCSuperClassRef);
  return std::make_pair(static_cast<const ObjCInterfaceDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorObjCProtocolRef(const ObjCProtocolDecl *Proto,
                                             SourceLocation Loc,
                                             CXTranslationUnit TU) {
  assert(Proto && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_ObjCProtocolRef, 0, {Proto, RawLoc, TU}};
  return C;
}

std::pair<const ObjCProtocolDecl *, SourceLocation>
cxcursor::getCursorObjCProtocolRef(CXCursor C) {
  assert(C.kind == CXCursor_ObjCProtocolRef);
  return std::make_pair(static_cast<const ObjCProtocolDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorObjCClassRef(const ObjCInterfaceDecl *Class,
                                          SourceLocation Loc,
                                          CXTranslationUnit TU) {
  // 'Class' can be null for invalid code.
  if (!Class)
    return MakeCXCursorInvalid(CXCursor_InvalidCode);
  assert(TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_ObjCClassRef, 0, {Class, RawLoc, TU}};
  return C;
}

std::pair<const ObjCInterfaceDecl *, SourceLocation>
cxcursor::getCursorObjCClassRef(CXCursor C) {
  assert(C.kind == CXCursor_ObjCClassRef);
  return std::make_pair(static_cast<const ObjCInterfaceDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorTypeRef(const TypeDecl *Type, SourceLocation Loc,
                                     CXTranslationUnit TU) {
  assert(Type && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_TypeRef, 0, {Type, RawLoc, TU}};
  return C;
}

std::pair<const TypeDecl *, SourceLocation>
cxcursor::getCursorTypeRef(CXCursor C) {
  assert(C.kind == CXCursor_TypeRef);
  return std::make_pair(static_cast<const TypeDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorTemplateRef(const TemplateDecl *Template,
                                         SourceLocation Loc,
                                         CXTranslationUnit TU) {
  assert(Template && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_TemplateRef, 0, {Template, RawLoc, TU}};
  return C;
}

std::pair<const TemplateDecl *, SourceLocation>
cxcursor::getCursorTemplateRef(CXCursor C) {
  assert(C.kind == CXCursor_TemplateRef);
  return std::make_pair(static_cast<const TemplateDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorNamespaceRef(const NamedDecl *NS,
                                          SourceLocation Loc,
                                          CXTranslationUnit TU) {

  assert(NS && (isa<NamespaceDecl>(NS) || isa<NamespaceAliasDecl>(NS)) && TU &&
         "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_NamespaceRef, 0, {NS, RawLoc, TU}};
  return C;
}

std::pair<const NamedDecl *, SourceLocation>
cxcursor::getCursorNamespaceRef(CXCursor C) {
  assert(C.kind == CXCursor_NamespaceRef);
  return std::make_pair(static_cast<const NamedDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorVariableRef(const VarDecl *Var, SourceLocation Loc,
                                         CXTranslationUnit TU) {

  assert(Var && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_VariableRef, 0, {Var, RawLoc, TU}};
  return C;
}

std::pair<const VarDecl *, SourceLocation>
cxcursor::getCursorVariableRef(CXCursor C) {
  assert(C.kind == CXCursor_VariableRef);
  return std::make_pair(static_cast<const VarDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorMemberRef(const FieldDecl *Field,
                                       SourceLocation Loc,
                                       CXTranslationUnit TU) {

  assert(Field && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_MemberRef, 0, {Field, RawLoc, TU}};
  return C;
}

std::pair<const FieldDecl *, SourceLocation>
cxcursor::getCursorMemberRef(CXCursor C) {
  assert(C.kind == CXCursor_MemberRef);
  return std::make_pair(static_cast<const FieldDecl *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorCXXBaseSpecifier(const CXXBaseSpecifier *B,
                                              CXTranslationUnit TU) {
  CXCursor C = {CXCursor_CXXBaseSpecifier, 0, {B, nullptr, TU}};
  return C;
}

const CXXBaseSpecifier *cxcursor::getCursorCXXBaseSpecifier(CXCursor C) {
  assert(C.kind == CXCursor_CXXBaseSpecifier);
  return static_cast<const CXXBaseSpecifier *>(C.data[0]);
}

CXCursor cxcursor::MakePreprocessingDirectiveCursor(SourceRange Range,
                                                    CXTranslationUnit TU) {
  CXCursor C = {
      CXCursor_PreprocessingDirective,
      0,
      {Range.getBegin().getPtrEncoding(), Range.getEnd().getPtrEncoding(), TU}};
  return C;
}

SourceRange cxcursor::getCursorPreprocessingDirective(CXCursor C) {
  assert(C.kind == CXCursor_PreprocessingDirective);
  SourceRange Range(SourceLocation::getFromPtrEncoding(C.data[0]),
                    SourceLocation::getFromPtrEncoding(C.data[1]));
  ASTUnit *TU = getCursorASTUnit(C);
  return TU->mapRangeFromPreamble(Range);
}

CXCursor cxcursor::MakeMacroDefinitionCursor(const MacroDefinitionRecord *MI,
                                             CXTranslationUnit TU) {
  CXCursor C = {CXCursor_MacroDefinition, 0, {MI, nullptr, TU}};
  return C;
}

const MacroDefinitionRecord *cxcursor::getCursorMacroDefinition(CXCursor C) {
  assert(C.kind == CXCursor_MacroDefinition);
  return static_cast<const MacroDefinitionRecord *>(C.data[0]);
}

CXCursor cxcursor::MakeMacroExpansionCursor(MacroExpansion *MI,
                                            CXTranslationUnit TU) {
  CXCursor C = {CXCursor_MacroExpansion, 0, {MI, nullptr, TU}};
  return C;
}

CXCursor cxcursor::MakeMacroExpansionCursor(MacroDefinitionRecord *MI,
                                            SourceLocation Loc,
                                            CXTranslationUnit TU) {
  assert(Loc.isValid());
  CXCursor C = {CXCursor_MacroExpansion, 0, {MI, Loc.getPtrEncoding(), TU}};
  return C;
}

const IdentifierInfo *cxcursor::MacroExpansionCursor::getName() const {
  if (isPseudo())
    return getAsMacroDefinition()->getName();
  return getAsMacroExpansion()->getName();
}
const MacroDefinitionRecord *
cxcursor::MacroExpansionCursor::getDefinition() const {
  if (isPseudo())
    return getAsMacroDefinition();
  return getAsMacroExpansion()->getDefinition();
}
SourceRange cxcursor::MacroExpansionCursor::getSourceRange() const {
  if (isPseudo())
    return getPseudoLoc();
  return getAsMacroExpansion()->getSourceRange();
}

CXCursor cxcursor::MakeInclusionDirectiveCursor(InclusionDirective *ID,
                                                CXTranslationUnit TU) {
  CXCursor C = {CXCursor_InclusionDirective, 0, {ID, nullptr, TU}};
  return C;
}

const InclusionDirective *cxcursor::getCursorInclusionDirective(CXCursor C) {
  assert(C.kind == CXCursor_InclusionDirective);
  return static_cast<const InclusionDirective *>(C.data[0]);
}

CXCursor cxcursor::MakeCursorLabelRef(LabelStmt *Label, SourceLocation Loc,
                                      CXTranslationUnit TU) {

  assert(Label && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  CXCursor C = {CXCursor_LabelRef, 0, {Label, RawLoc, TU}};
  return C;
}

std::pair<const LabelStmt *, SourceLocation>
cxcursor::getCursorLabelRef(CXCursor C) {
  assert(C.kind == CXCursor_LabelRef);
  return std::make_pair(static_cast<const LabelStmt *>(C.data[0]),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

CXCursor cxcursor::MakeCursorOverloadedDeclRef(const OverloadExpr *E,
                                               CXTranslationUnit TU) {
  assert(E && TU && "Invalid arguments!");
  OverloadedDeclRefStorage Storage(E);
  void *RawLoc = E->getNameLoc().getPtrEncoding();
  CXCursor C = {
      CXCursor_OverloadedDeclRef, 0, {Storage.getOpaqueValue(), RawLoc, TU}};
  return C;
}

CXCursor cxcursor::MakeCursorOverloadedDeclRef(const Decl *D,
                                               SourceLocation Loc,
                                               CXTranslationUnit TU) {
  assert(D && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  OverloadedDeclRefStorage Storage(D);
  CXCursor C = {
      CXCursor_OverloadedDeclRef, 0, {Storage.getOpaqueValue(), RawLoc, TU}};
  return C;
}

CXCursor cxcursor::MakeCursorOverloadedDeclRef(TemplateName Name,
                                               SourceLocation Loc,
                                               CXTranslationUnit TU) {
  assert(Name.getAsOverloadedTemplate() && TU && "Invalid arguments!");
  void *RawLoc = Loc.getPtrEncoding();
  OverloadedDeclRefStorage Storage(Name.getAsOverloadedTemplate());
  CXCursor C = {
      CXCursor_OverloadedDeclRef, 0, {Storage.getOpaqueValue(), RawLoc, TU}};
  return C;
}

std::pair<cxcursor::OverloadedDeclRefStorage, SourceLocation>
cxcursor::getCursorOverloadedDeclRef(CXCursor C) {
  assert(C.kind == CXCursor_OverloadedDeclRef);
  return std::make_pair(OverloadedDeclRefStorage::getFromOpaqueValue(
                            const_cast<void *>(C.data[0])),
                        SourceLocation::getFromPtrEncoding(C.data[1]));
}

const Decl *cxcursor::getCursorDecl(CXCursor Cursor) {
  return static_cast<const Decl *>(Cursor.data[0]);
}

const Expr *cxcursor::getCursorExpr(CXCursor Cursor) {
  return dyn_cast_or_null<Expr>(getCursorStmt(Cursor));
}

const Stmt *cxcursor::getCursorStmt(CXCursor Cursor) {
  if (Cursor.kind == CXCursor_ObjCSuperClassRef ||
      Cursor.kind == CXCursor_ObjCProtocolRef ||
      Cursor.kind == CXCursor_ObjCClassRef)
    return nullptr;

  return static_cast<const Stmt *>(Cursor.data[1]);
}

const Attr *cxcursor::getCursorAttr(CXCursor Cursor) {
  return static_cast<const Attr *>(Cursor.data[1]);
}

ASTContext &cxcursor::getCursorContext(CXCursor Cursor) {
  return getCursorASTUnit(Cursor)->getASTContext();
}

ASTUnit *cxcursor::getCursorASTUnit(CXCursor Cursor) {
  CXTranslationUnit TU = getCursorTU(Cursor);
  if (!TU)
    return nullptr;
  return cxtu::getASTUnit(TU);
}

CXTranslationUnit cxcursor::getCursorTU(CXCursor Cursor) {
  return static_cast<CXTranslationUnit>(const_cast<void *>(Cursor.data[2]));
}

void cxcursor::getOverriddenCursors(CXCursor cursor,
                                    SmallVectorImpl<CXCursor> &overridden) {
  assert(clang_isDeclaration(cursor.kind));
  const NamedDecl *D = dyn_cast_or_null<NamedDecl>(getCursorDecl(cursor));
  if (!D)
    return;

  CXTranslationUnit TU = getCursorTU(cursor);
  SmallVector<const NamedDecl *, 8> OverDecls;
  D->getASTContext().getOverriddenMethods(D, OverDecls);

  for (SmallVectorImpl<const NamedDecl *>::iterator I = OverDecls.begin(),
                                                    E = OverDecls.end();
       I != E; ++I) {
    overridden.push_back(MakeCXCursor(*I, TU));
  }
}

std::pair<int, SourceLocation>
cxcursor::getSelectorIdentifierIndexAndLoc(CXCursor cursor) {
  if (cursor.kind == CXCursor_ObjCMessageExpr) {
    if (cursor.xdata != -1)
      return std::make_pair(cursor.xdata,
                            cast<ObjCMessageExpr>(getCursorExpr(cursor))
                                ->getSelectorLoc(cursor.xdata));
  } else if (cursor.kind == CXCursor_ObjCClassMethodDecl ||
             cursor.kind == CXCursor_ObjCInstanceMethodDecl) {
    if (cursor.xdata != -1)
      return std::make_pair(cursor.xdata,
                            cast<ObjCMethodDecl>(getCursorDecl(cursor))
                                ->getSelectorLoc(cursor.xdata));
  }

  return std::make_pair(-1, SourceLocation());
}

CXCursor cxcursor::getSelectorIdentifierCursor(int SelIdx, CXCursor cursor) {
  CXCursor newCursor = cursor;

  if (cursor.kind == CXCursor_ObjCMessageExpr) {
    if (SelIdx == -1 ||
        unsigned(SelIdx) >=
            cast<ObjCMessageExpr>(getCursorExpr(cursor))->getNumSelectorLocs())
      newCursor.xdata = -1;
    else
      newCursor.xdata = SelIdx;
  } else if (cursor.kind == CXCursor_ObjCClassMethodDecl ||
             cursor.kind == CXCursor_ObjCInstanceMethodDecl) {
    if (SelIdx == -1 ||
        unsigned(SelIdx) >=
            cast<ObjCMethodDecl>(getCursorDecl(cursor))->getNumSelectorLocs())
      newCursor.xdata = -1;
    else
      newCursor.xdata = SelIdx;
  }

  return newCursor;
}

CXCursor cxcursor::getTypeRefCursor(CXCursor cursor) {
  if (cursor.kind != CXCursor_CallExpr)
    return cursor;

  if (cursor.xdata == 0)
    return cursor;

  const Expr *E = getCursorExpr(cursor);
  TypeSourceInfo *Type = nullptr;
  if (const CXXUnresolvedConstructExpr *UnCtor =
          dyn_cast<CXXUnresolvedConstructExpr>(E)) {
    Type = UnCtor->getTypeSourceInfo();
  } else if (const CXXTemporaryObjectExpr *Tmp =
                 dyn_cast<CXXTemporaryObjectExpr>(E)) {
    Type = Tmp->getTypeSourceInfo();
  }

  if (!Type)
    return cursor;

  CXTranslationUnit TU = getCursorTU(cursor);
  QualType Ty = Type->getType();
  TypeLoc TL = Type->getTypeLoc();
  SourceLocation Loc = TL.getBeginLoc();

  if (const ElaboratedType *ElabT = Ty->getAs<ElaboratedType>()) {
    Ty = ElabT->getNamedType();
    ElaboratedTypeLoc ElabTL = TL.castAs<ElaboratedTypeLoc>();
    Loc = ElabTL.getNamedTypeLoc().getBeginLoc();
  }

  if (const TypedefType *Typedef = Ty->getAs<TypedefType>())
    return MakeCursorTypeRef(Typedef->getDecl(), Loc, TU);
  if (const TagType *Tag = Ty->getAs<TagType>())
    return MakeCursorTypeRef(Tag->getDecl(), Loc, TU);
  if (const TemplateTypeParmType *TemplP = Ty->getAs<TemplateTypeParmType>())
    return MakeCursorTypeRef(TemplP->getDecl(), Loc, TU);

  return cursor;
}

bool cxcursor::operator==(CXCursor X, CXCursor Y) {
  return X.kind == Y.kind && X.data[0] == Y.data[0] && X.data[1] == Y.data[1] &&
         X.data[2] == Y.data[2];
}

// FIXME: Remove once we can model DeclGroups and their appropriate ranges
// properly in the ASTs.
bool cxcursor::isFirstInDeclGroup(CXCursor C) {
  assert(clang_isDeclaration(C.kind));
  return ((uintptr_t)(C.data[1])) != 0;
}

//===----------------------------------------------------------------------===//
// libclang CXCursor APIs
//===----------------------------------------------------------------------===//

int clang_Cursor_isNull(CXCursor cursor) {
  return clang_equalCursors(cursor, clang_getNullCursor());
}

CXTranslationUnit clang_Cursor_getTranslationUnit(CXCursor cursor) {
  return getCursorTU(cursor);
}

int clang_Cursor_getNumArguments(CXCursor C) {
  if (clang_isDeclaration(C.kind)) {
    const Decl *D = cxcursor::getCursorDecl(C);
    if (const ObjCMethodDecl *MD = dyn_cast_or_null<ObjCMethodDecl>(D))
      return MD->param_size();
    if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D))
      return FD->param_size();
  }

  if (clang_isExpression(C.kind)) {
    const Expr *E = cxcursor::getCursorExpr(C);
    if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
      return CE->getNumArgs();
    }
    if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(E)) {
      return CE->getNumArgs();
    }
  }

  return -1;
}

CXCursor clang_Cursor_getArgument(CXCursor C, unsigned i) {
  if (clang_isDeclaration(C.kind)) {
    const Decl *D = cxcursor::getCursorDecl(C);
    if (const ObjCMethodDecl *MD = dyn_cast_or_null<ObjCMethodDecl>(D)) {
      if (i < MD->param_size())
        return cxcursor::MakeCXCursor(MD->parameters()[i],
                                      cxcursor::getCursorTU(C));
    } else if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
      if (i < FD->param_size())
        return cxcursor::MakeCXCursor(FD->parameters()[i],
                                      cxcursor::getCursorTU(C));
    }
  }

  if (clang_isExpression(C.kind)) {
    const Expr *E = cxcursor::getCursorExpr(C);
    if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
      if (i < CE->getNumArgs()) {
        return cxcursor::MakeCXCursor(CE->getArg(i), getCursorDecl(C),
                                      cxcursor::getCursorTU(C));
      }
    }
    if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(E)) {
      if (i < CE->getNumArgs()) {
        return cxcursor::MakeCXCursor(CE->getArg(i), getCursorDecl(C),
                                      cxcursor::getCursorTU(C));
      }
    }
  }

  return clang_getNullCursor();
}

int clang_Cursor_getNumTemplateArguments(CXCursor C) {
  CXCursorKind kind = clang_getCursorKind(C);
  if (kind != CXCursor_FunctionDecl && kind != CXCursor_StructDecl &&
      kind != CXCursor_ClassDecl &&
      kind != CXCursor_ClassTemplatePartialSpecialization) {
    return -1;
  }

  if (const auto *FD =
          llvm::dyn_cast_if_present<clang::FunctionDecl>(getCursorDecl(C))) {
    const FunctionTemplateSpecializationInfo *SpecInfo =
        FD->getTemplateSpecializationInfo();
    if (!SpecInfo) {
      return -1;
    }
    return SpecInfo->TemplateArguments->size();
  }

  if (const auto *SD =
          llvm::dyn_cast_if_present<clang::ClassTemplateSpecializationDecl>(
              getCursorDecl(C))) {
    return SD->getTemplateArgs().size();
  }

  return -1;
}

enum CXGetTemplateArgumentStatus {
  /** The operation completed successfully */
  CXGetTemplateArgumentStatus_Success = 0,

  /** The specified cursor did not represent a FunctionDecl or
      ClassTemplateSpecializationDecl.                         */
  CXGetTemplateArgumentStatus_CursorNotCompatibleDecl = -1,

  /** The specified cursor was not castable to a FunctionDecl or
      ClassTemplateSpecializationDecl.                         */
  CXGetTemplateArgumentStatus_BadDeclCast = -2,

  /** A NULL FunctionTemplateSpecializationInfo was retrieved. */
  CXGetTemplateArgumentStatus_NullTemplSpecInfo = -3,

  /** An invalid (OOB) argument index was specified */
  CXGetTemplateArgumentStatus_InvalidIndex = -4
};

static int clang_Cursor_getTemplateArgument(CXCursor C, unsigned I,
                                            TemplateArgument *TA) {
  CXCursorKind kind = clang_getCursorKind(C);
  if (kind != CXCursor_FunctionDecl && kind != CXCursor_StructDecl &&
      kind != CXCursor_ClassDecl &&
      kind != CXCursor_ClassTemplatePartialSpecialization) {
    return -1;
  }

  if (const auto *FD =
          llvm::dyn_cast_if_present<clang::FunctionDecl>(getCursorDecl(C))) {

    const FunctionTemplateSpecializationInfo *SpecInfo =
        FD->getTemplateSpecializationInfo();
    if (!SpecInfo) {
      return CXGetTemplateArgumentStatus_NullTemplSpecInfo;
    }

    if (I >= SpecInfo->TemplateArguments->size()) {
      return CXGetTemplateArgumentStatus_InvalidIndex;
    }

    *TA = SpecInfo->TemplateArguments->get(I);
    return 0;
  }

  if (const auto *SD =
          llvm::dyn_cast_if_present<clang::ClassTemplateSpecializationDecl>(
              getCursorDecl(C))) {
    if (I >= SD->getTemplateArgs().size()) {
      return CXGetTemplateArgumentStatus_InvalidIndex;
    }

    *TA = SD->getTemplateArgs()[I];
    return 0;
  }

  return CXGetTemplateArgumentStatus_BadDeclCast;
}

enum CXTemplateArgumentKind clang_Cursor_getTemplateArgumentKind(CXCursor C,
                                                                 unsigned I) {
  TemplateArgument TA;
  if (clang_Cursor_getTemplateArgument(C, I, &TA)) {
    return CXTemplateArgumentKind_Invalid;
  }

  switch (TA.getKind()) {
  case TemplateArgument::Null:
    return CXTemplateArgumentKind_Null;
  case TemplateArgument::Type:
    return CXTemplateArgumentKind_Type;
  case TemplateArgument::Declaration:
    return CXTemplateArgumentKind_Declaration;
  case TemplateArgument::NullPtr:
    return CXTemplateArgumentKind_NullPtr;
  case TemplateArgument::Integral:
    return CXTemplateArgumentKind_Integral;
  case TemplateArgument::StructuralValue:
    // FIXME: Expose these values.
    return CXTemplateArgumentKind_Invalid;
  case TemplateArgument::Template:
    return CXTemplateArgumentKind_Template;
  case TemplateArgument::TemplateExpansion:
    return CXTemplateArgumentKind_TemplateExpansion;
  case TemplateArgument::Expression:
    return CXTemplateArgumentKind_Expression;
  case TemplateArgument::Pack:
    return CXTemplateArgumentKind_Pack;
  }

  return CXTemplateArgumentKind_Invalid;
}

CXType clang_Cursor_getTemplateArgumentType(CXCursor C, unsigned I) {
  TemplateArgument TA;
  if (clang_Cursor_getTemplateArgument(C, I, &TA) !=
      CXGetTemplateArgumentStatus_Success) {
    return cxtype::MakeCXType(QualType(), getCursorTU(C));
  }

  if (TA.getKind() != TemplateArgument::Type) {
    return cxtype::MakeCXType(QualType(), getCursorTU(C));
  }

  return cxtype::MakeCXType(TA.getAsType(), getCursorTU(C));
}

long long clang_Cursor_getTemplateArgumentValue(CXCursor C, unsigned I) {
  TemplateArgument TA;
  if (clang_Cursor_getTemplateArgument(C, I, &TA) !=
      CXGetTemplateArgumentStatus_Success) {
    assert(0 && "Unable to retrieve TemplateArgument");
    return 0;
  }

  if (TA.getKind() != TemplateArgument::Integral) {
    assert(0 && "Passed template argument is not Integral");
    return 0;
  }

  return TA.getAsIntegral().getSExtValue();
}

unsigned long long clang_Cursor_getTemplateArgumentUnsignedValue(CXCursor C,
                                                                 unsigned I) {
  TemplateArgument TA;
  if (clang_Cursor_getTemplateArgument(C, I, &TA) !=
      CXGetTemplateArgumentStatus_Success) {
    assert(0 && "Unable to retrieve TemplateArgument");
    return 0;
  }

  if (TA.getKind() != TemplateArgument::Integral) {
    assert(0 && "Passed template argument is not Integral");
    return 0;
  }

  return TA.getAsIntegral().getZExtValue();
}

//===----------------------------------------------------------------------===//
// CXCursorSet.
//===----------------------------------------------------------------------===//

typedef llvm::DenseMap<CXCursor, unsigned> CXCursorSet_Impl;

static inline CXCursorSet packCXCursorSet(CXCursorSet_Impl *setImpl) {
  return (CXCursorSet)setImpl;
}
static inline CXCursorSet_Impl *unpackCXCursorSet(CXCursorSet set) {
  return (CXCursorSet_Impl *)set;
}
namespace llvm {
template <> struct DenseMapInfo<CXCursor> {
public:
  static inline CXCursor getEmptyKey() {
    return MakeCXCursorInvalid(CXCursor_InvalidFile);
  }
  static inline CXCursor getTombstoneKey() {
    return MakeCXCursorInvalid(CXCursor_NoDeclFound);
  }
  static inline unsigned getHashValue(const CXCursor &cursor) {
    return llvm::DenseMapInfo<std::pair<const void *, const void *>>::
        getHashValue(std::make_pair(cursor.data[0], cursor.data[1]));
  }
  static inline bool isEqual(const CXCursor &x, const CXCursor &y) {
    return x.kind == y.kind && x.data[0] == y.data[0] && x.data[1] == y.data[1];
  }
};
} // namespace llvm

CXCursorSet clang_createCXCursorSet() {
  return packCXCursorSet(new CXCursorSet_Impl());
}

void clang_disposeCXCursorSet(CXCursorSet set) {
  delete unpackCXCursorSet(set);
}

unsigned clang_CXCursorSet_contains(CXCursorSet set, CXCursor cursor) {
  CXCursorSet_Impl *setImpl = unpackCXCursorSet(set);
  if (!setImpl)
    return 0;
  return setImpl->find(cursor) != setImpl->end();
}

unsigned clang_CXCursorSet_insert(CXCursorSet set, CXCursor cursor) {
  // Do not insert invalid cursors into the set.
  if (cursor.kind >= CXCursor_FirstInvalid &&
      cursor.kind <= CXCursor_LastInvalid)
    return 1;

  CXCursorSet_Impl *setImpl = unpackCXCursorSet(set);
  if (!setImpl)
    return 1;
  unsigned &entry = (*setImpl)[cursor];
  unsigned flag = entry == 0 ? 1 : 0;
  entry = 1;
  return flag;
}

CXCompletionString clang_getCursorCompletionString(CXCursor cursor) {
  enum CXCursorKind kind = clang_getCursorKind(cursor);
  if (clang_isDeclaration(kind)) {
    const Decl *decl = getCursorDecl(cursor);
    if (const NamedDecl *namedDecl = dyn_cast_or_null<NamedDecl>(decl)) {
      ASTUnit *unit = getCursorASTUnit(cursor);
      CodeCompletionResult Result(namedDecl, CCP_Declaration);
      CodeCompletionString *String = Result.CreateCodeCompletionString(
          unit->getASTContext(), unit->getPreprocessor(),
          CodeCompletionContext::CCC_Other,
          unit->getCodeCompletionTUInfo().getAllocator(),
          unit->getCodeCompletionTUInfo(), true);
      return String;
    }
  } else if (kind == CXCursor_MacroDefinition) {
    const MacroDefinitionRecord *definition = getCursorMacroDefinition(cursor);
    const IdentifierInfo *Macro = definition->getName();
    ASTUnit *unit = getCursorASTUnit(cursor);
    CodeCompletionResult Result(
        Macro,
        unit->getPreprocessor().getMacroDefinition(Macro).getMacroInfo());
    CodeCompletionString *String = Result.CreateCodeCompletionString(
        unit->getASTContext(), unit->getPreprocessor(),
        CodeCompletionContext::CCC_Other,
        unit->getCodeCompletionTUInfo().getAllocator(),
        unit->getCodeCompletionTUInfo(), false);
    return String;
  }
  return nullptr;
}

namespace {
struct OverridenCursorsPool {
  typedef SmallVector<CXCursor, 2> CursorVec;
  std::vector<CursorVec *> AllCursors;
  std::vector<CursorVec *> AvailableCursors;

  ~OverridenCursorsPool() {
    for (std::vector<CursorVec *>::iterator I = AllCursors.begin(),
                                            E = AllCursors.end();
         I != E; ++I) {
      delete *I;
    }
  }
};
} // namespace

void *cxcursor::createOverridenCXCursorsPool() {
  return new OverridenCursorsPool();
}

void cxcursor::disposeOverridenCXCursorsPool(void *pool) {
  delete static_cast<OverridenCursorsPool *>(pool);
}

void clang_getOverriddenCursors(CXCursor cursor, CXCursor **overridden,
                                unsigned *num_overridden) {
  if (overridden)
    *overridden = nullptr;
  if (num_overridden)
    *num_overridden = 0;

  CXTranslationUnit TU = cxcursor::getCursorTU(cursor);

  if (!overridden || !num_overridden || !TU)
    return;

  if (!clang_isDeclaration(cursor.kind))
    return;

  OverridenCursorsPool &pool =
      *static_cast<OverridenCursorsPool *>(TU->OverridenCursorsPool);

  OverridenCursorsPool::CursorVec *Vec = nullptr;

  if (!pool.AvailableCursors.empty()) {
    Vec = pool.AvailableCursors.back();
    pool.AvailableCursors.pop_back();
  } else {
    Vec = new OverridenCursorsPool::CursorVec();
    pool.AllCursors.push_back(Vec);
  }

  // Clear out the vector, but don't free the memory contents.  This
  // reduces malloc() traffic.
  Vec->clear();

  // Use the first entry to contain a back reference to the vector.
  // This is a complete hack.
  CXCursor backRefCursor = MakeCXCursorInvalid(CXCursor_InvalidFile, TU);
  backRefCursor.data[0] = Vec;
  assert(cxcursor::getCursorTU(backRefCursor) == TU);
  Vec->push_back(backRefCursor);

  // Get the overridden cursors.
  cxcursor::getOverriddenCursors(cursor, *Vec);

  // Did we get any overridden cursors?  If not, return Vec to the pool
  // of available cursor vectors.
  if (Vec->size() == 1) {
    pool.AvailableCursors.push_back(Vec);
    return;
  }

  // Now tell the caller about the overridden cursors.
  assert(Vec->size() > 1);
  *overridden = &((*Vec)[1]);
  *num_overridden = Vec->size() - 1;
}

void clang_disposeOverriddenCursors(CXCursor *overridden) {
  if (!overridden)
    return;

  // Use pointer arithmetic to get back the first faux entry
  // which has a back-reference to the TU and the vector.
  --overridden;
  OverridenCursorsPool::CursorVec *Vec =
      static_cast<OverridenCursorsPool::CursorVec *>(
          const_cast<void *>(overridden->data[0]));
  CXTranslationUnit TU = getCursorTU(*overridden);

  assert(Vec && TU);

  OverridenCursorsPool &pool =
      *static_cast<OverridenCursorsPool *>(TU->OverridenCursorsPool);

  pool.AvailableCursors.push_back(Vec);
}

int clang_Cursor_isDynamicCall(CXCursor C) {
  const Expr *E = nullptr;
  if (clang_isExpression(C.kind))
    E = getCursorExpr(C);
  if (!E)
    return 0;

  if (const ObjCMessageExpr *MsgE = dyn_cast<ObjCMessageExpr>(E)) {
    if (MsgE->getReceiverKind() != ObjCMessageExpr::Instance)
      return false;
    if (auto *RecE = dyn_cast<ObjCMessageExpr>(
            MsgE->getInstanceReceiver()->IgnoreParenCasts())) {
      if (RecE->getMethodFamily() == OMF_alloc)
        return false;
    }
    return true;
  }

  if (auto *PropRefE = dyn_cast<ObjCPropertyRefExpr>(E)) {
    return !PropRefE->isSuperReceiver();
  }

  const MemberExpr *ME = nullptr;
  if (isa<MemberExpr>(E))
    ME = cast<MemberExpr>(E);
  else if (const CallExpr *CE = dyn_cast<CallExpr>(E))
    ME = dyn_cast_or_null<MemberExpr>(CE->getCallee());

  if (ME) {
    if (const CXXMethodDecl *MD =
            dyn_cast_or_null<CXXMethodDecl>(ME->getMemberDecl()))
      return MD->isVirtual() &&
             ME->performsVirtualDispatch(
                 cxcursor::getCursorContext(C).getLangOpts());
  }

  return 0;
}

CXType clang_Cursor_getReceiverType(CXCursor C) {
  CXTranslationUnit TU = cxcursor::getCursorTU(C);
  const Expr *E = nullptr;
  if (clang_isExpression(C.kind))
    E = getCursorExpr(C);

  if (const ObjCMessageExpr *MsgE = dyn_cast_or_null<ObjCMessageExpr>(E))
    return cxtype::MakeCXType(MsgE->getReceiverType(), TU);

  if (auto *PropRefE = dyn_cast<ObjCPropertyRefExpr>(E)) {
    return cxtype::MakeCXType(
        PropRefE->getReceiverType(cxcursor::getCursorContext(C)), TU);
  }

  const MemberExpr *ME = nullptr;
  if (isa<MemberExpr>(E))
    ME = cast<MemberExpr>(E);
  else if (const CallExpr *CE = dyn_cast<CallExpr>(E))
    ME = dyn_cast_or_null<MemberExpr>(CE->getCallee());

  if (ME) {
    if (isa_and_nonnull<CXXMethodDecl>(ME->getMemberDecl())) {
      auto receiverTy = ME->getBase()->IgnoreImpCasts()->getType();
      return cxtype::MakeCXType(receiverTy, TU);
    }
  }

  return cxtype::MakeCXType(QualType(), TU);
}
