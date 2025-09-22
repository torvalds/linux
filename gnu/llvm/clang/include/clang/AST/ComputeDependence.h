//===--- ComputeDependence.h -------------------------------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Calculate various template dependency flags for the AST.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_COMPUTEDEPENDENCE_H
#define LLVM_CLANG_AST_COMPUTEDEPENDENCE_H

#include "clang/AST/DependenceFlags.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "llvm/ADT/ArrayRef.h"

namespace clang {

class ASTContext;

class Expr;
class FullExpr;
class OpaqueValueExpr;
class ParenExpr;
class UnaryOperator;
class UnaryExprOrTypeTraitExpr;
class ArraySubscriptExpr;
class MatrixSubscriptExpr;
class CompoundLiteralExpr;
class ImplicitCastExpr;
class ExplicitCastExpr;
class BinaryOperator;
class ConditionalOperator;
class BinaryConditionalOperator;
class StmtExpr;
class ConvertVectorExpr;
class VAArgExpr;
class ChooseExpr;
class NoInitExpr;
class ArrayInitLoopExpr;
class ImplicitValueInitExpr;
class InitListExpr;
class ExtVectorElementExpr;
class BlockExpr;
class AsTypeExpr;
class DeclRefExpr;
class RecoveryExpr;
class CXXRewrittenBinaryOperator;
class CXXStdInitializerListExpr;
class CXXTypeidExpr;
class MSPropertyRefExpr;
class MSPropertySubscriptExpr;
class CXXUuidofExpr;
class CXXThisExpr;
class CXXThrowExpr;
class CXXBindTemporaryExpr;
class CXXScalarValueInitExpr;
class CXXDeleteExpr;
class ArrayTypeTraitExpr;
class ExpressionTraitExpr;
class CXXNoexceptExpr;
class PackExpansionExpr;
class PackIndexingExpr;
class SubstNonTypeTemplateParmExpr;
class CoroutineSuspendExpr;
class DependentCoawaitExpr;
class CXXNewExpr;
class CXXPseudoDestructorExpr;
class OverloadExpr;
class DependentScopeDeclRefExpr;
class CXXConstructExpr;
class CXXTemporaryObjectExpr;
class CXXDefaultInitExpr;
class CXXDefaultArgExpr;
class LambdaExpr;
class CXXUnresolvedConstructExpr;
class CXXDependentScopeMemberExpr;
class MaterializeTemporaryExpr;
class CXXFoldExpr;
class CXXParenListInitExpr;
class TypeTraitExpr;
class ConceptSpecializationExpr;
class SYCLUniqueStableNameExpr;
class PredefinedExpr;
class CallExpr;
class OffsetOfExpr;
class MemberExpr;
class ShuffleVectorExpr;
class GenericSelectionExpr;
class DesignatedInitExpr;
class ParenListExpr;
class PseudoObjectExpr;
class AtomicExpr;
class ArraySectionExpr;
class OMPArrayShapingExpr;
class OMPIteratorExpr;
class ObjCArrayLiteral;
class ObjCDictionaryLiteral;
class ObjCBoxedExpr;
class ObjCEncodeExpr;
class ObjCIvarRefExpr;
class ObjCPropertyRefExpr;
class ObjCSubscriptRefExpr;
class ObjCIsaExpr;
class ObjCIndirectCopyRestoreExpr;
class ObjCMessageExpr;

// The following functions are called from constructors of `Expr`, so they
// should not access anything beyond basic
ExprDependence computeDependence(FullExpr *E);
ExprDependence computeDependence(OpaqueValueExpr *E);
ExprDependence computeDependence(ParenExpr *E);
ExprDependence computeDependence(UnaryOperator *E, const ASTContext &Ctx);
ExprDependence computeDependence(UnaryExprOrTypeTraitExpr *E);
ExprDependence computeDependence(ArraySubscriptExpr *E);
ExprDependence computeDependence(MatrixSubscriptExpr *E);
ExprDependence computeDependence(CompoundLiteralExpr *E);
ExprDependence computeDependence(ImplicitCastExpr *E);
ExprDependence computeDependence(ExplicitCastExpr *E);
ExprDependence computeDependence(BinaryOperator *E);
ExprDependence computeDependence(ConditionalOperator *E);
ExprDependence computeDependence(BinaryConditionalOperator *E);
ExprDependence computeDependence(StmtExpr *E, unsigned TemplateDepth);
ExprDependence computeDependence(ConvertVectorExpr *E);
ExprDependence computeDependence(VAArgExpr *E);
ExprDependence computeDependence(ChooseExpr *E);
ExprDependence computeDependence(NoInitExpr *E);
ExprDependence computeDependence(ArrayInitLoopExpr *E);
ExprDependence computeDependence(ImplicitValueInitExpr *E);
ExprDependence computeDependence(InitListExpr *E);
ExprDependence computeDependence(ExtVectorElementExpr *E);
ExprDependence computeDependence(BlockExpr *E);
ExprDependence computeDependence(AsTypeExpr *E);
ExprDependence computeDependence(DeclRefExpr *E, const ASTContext &Ctx);
ExprDependence computeDependence(RecoveryExpr *E);
ExprDependence computeDependence(CXXRewrittenBinaryOperator *E);
ExprDependence computeDependence(CXXStdInitializerListExpr *E);
ExprDependence computeDependence(CXXTypeidExpr *E);
ExprDependence computeDependence(MSPropertyRefExpr *E);
ExprDependence computeDependence(MSPropertySubscriptExpr *E);
ExprDependence computeDependence(CXXUuidofExpr *E);
ExprDependence computeDependence(CXXThisExpr *E);
ExprDependence computeDependence(CXXThrowExpr *E);
ExprDependence computeDependence(CXXBindTemporaryExpr *E);
ExprDependence computeDependence(CXXScalarValueInitExpr *E);
ExprDependence computeDependence(CXXDeleteExpr *E);
ExprDependence computeDependence(ArrayTypeTraitExpr *E);
ExprDependence computeDependence(ExpressionTraitExpr *E);
ExprDependence computeDependence(CXXNoexceptExpr *E, CanThrowResult CT);
ExprDependence computeDependence(PackExpansionExpr *E);
ExprDependence computeDependence(PackIndexingExpr *E);
ExprDependence computeDependence(SubstNonTypeTemplateParmExpr *E);
ExprDependence computeDependence(CoroutineSuspendExpr *E);
ExprDependence computeDependence(DependentCoawaitExpr *E);
ExprDependence computeDependence(CXXNewExpr *E);
ExprDependence computeDependence(CXXPseudoDestructorExpr *E);
ExprDependence computeDependence(OverloadExpr *E, bool KnownDependent,
                                 bool KnownInstantiationDependent,
                                 bool KnownContainsUnexpandedParameterPack);
ExprDependence computeDependence(DependentScopeDeclRefExpr *E);
ExprDependence computeDependence(CXXConstructExpr *E);
ExprDependence computeDependence(CXXTemporaryObjectExpr *E);
ExprDependence computeDependence(CXXDefaultInitExpr *E);
ExprDependence computeDependence(CXXDefaultArgExpr *E);
ExprDependence computeDependence(LambdaExpr *E,
                                 bool ContainsUnexpandedParameterPack);
ExprDependence computeDependence(CXXUnresolvedConstructExpr *E);
ExprDependence computeDependence(CXXDependentScopeMemberExpr *E);
ExprDependence computeDependence(MaterializeTemporaryExpr *E);
ExprDependence computeDependence(CXXFoldExpr *E);
ExprDependence computeDependence(CXXParenListInitExpr *E);
ExprDependence computeDependence(TypeTraitExpr *E);
ExprDependence computeDependence(ConceptSpecializationExpr *E,
                                 bool ValueDependent);

ExprDependence computeDependence(SYCLUniqueStableNameExpr *E);
ExprDependence computeDependence(PredefinedExpr *E);
ExprDependence computeDependence(CallExpr *E, llvm::ArrayRef<Expr *> PreArgs);
ExprDependence computeDependence(OffsetOfExpr *E);
ExprDependence computeDependence(MemberExpr *E);
ExprDependence computeDependence(ShuffleVectorExpr *E);
ExprDependence computeDependence(GenericSelectionExpr *E,
                                 bool ContainsUnexpandedPack);
ExprDependence computeDependence(DesignatedInitExpr *E);
ExprDependence computeDependence(ParenListExpr *E);
ExprDependence computeDependence(PseudoObjectExpr *E);
ExprDependence computeDependence(AtomicExpr *E);

ExprDependence computeDependence(ArraySectionExpr *E);
ExprDependence computeDependence(OMPArrayShapingExpr *E);
ExprDependence computeDependence(OMPIteratorExpr *E);

ExprDependence computeDependence(ObjCArrayLiteral *E);
ExprDependence computeDependence(ObjCDictionaryLiteral *E);
ExprDependence computeDependence(ObjCBoxedExpr *E);
ExprDependence computeDependence(ObjCEncodeExpr *E);
ExprDependence computeDependence(ObjCIvarRefExpr *E);
ExprDependence computeDependence(ObjCPropertyRefExpr *E);
ExprDependence computeDependence(ObjCSubscriptRefExpr *E);
ExprDependence computeDependence(ObjCIsaExpr *E);
ExprDependence computeDependence(ObjCIndirectCopyRestoreExpr *E);
ExprDependence computeDependence(ObjCMessageExpr *E);

} // namespace clang
#endif
