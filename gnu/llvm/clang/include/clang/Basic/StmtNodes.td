include "clang/Basic/ASTNode.td"

class StmtNode<StmtNode base, bit abstract = 0> : ASTNode, AttrSubject {
	StmtNode Base = base;
  bit Abstract = abstract;
}

// Statements
def Stmt : StmtNode<?, 1>;
def NullStmt : StmtNode<Stmt>;
def CompoundStmt : StmtNode<Stmt>;
def IfStmt : StmtNode<Stmt>;
def SwitchStmt : StmtNode<Stmt>;
def WhileStmt : StmtNode<Stmt>;
def DoStmt : StmtNode<Stmt>;
def ForStmt : StmtNode<Stmt>;
def GotoStmt : StmtNode<Stmt>;
def IndirectGotoStmt : StmtNode<Stmt>;
def ContinueStmt : StmtNode<Stmt>;
def BreakStmt : StmtNode<Stmt>;
def ReturnStmt : StmtNode<Stmt>;
def DeclStmt  : StmtNode<Stmt>;
def SwitchCase : StmtNode<Stmt, 1>;
def CaseStmt : StmtNode<SwitchCase>;
def DefaultStmt : StmtNode<SwitchCase>;
def CapturedStmt : StmtNode<Stmt>;

// Statements that might produce a value (for example, as the last non-null
// statement in a GNU statement-expression).
def ValueStmt : StmtNode<Stmt, 1>;
def LabelStmt : StmtNode<ValueStmt>;
def AttributedStmt : StmtNode<ValueStmt>;

// Asm statements
def AsmStmt : StmtNode<Stmt, 1>;
def GCCAsmStmt : StmtNode<AsmStmt>;
def MSAsmStmt : StmtNode<AsmStmt>;

// Obj-C statements
def ObjCAtTryStmt : StmtNode<Stmt>;
def ObjCAtCatchStmt : StmtNode<Stmt>;
def ObjCAtFinallyStmt : StmtNode<Stmt>;
def ObjCAtThrowStmt : StmtNode<Stmt>;
def ObjCAtSynchronizedStmt : StmtNode<Stmt>;
def ObjCForCollectionStmt : StmtNode<Stmt>;
def ObjCAutoreleasePoolStmt : StmtNode<Stmt>;

// C++ statements
def CXXCatchStmt : StmtNode<Stmt>;
def CXXTryStmt : StmtNode<Stmt>;
def CXXForRangeStmt : StmtNode<Stmt>;

// C++ Coroutines statements
def CoroutineBodyStmt : StmtNode<Stmt>;
def CoreturnStmt : StmtNode<Stmt>;

// Expressions
def Expr : StmtNode<ValueStmt, 1>;
def PredefinedExpr : StmtNode<Expr>;
def SYCLUniqueStableNameExpr : StmtNode<Expr>;
def DeclRefExpr : StmtNode<Expr>;
def IntegerLiteral : StmtNode<Expr>;
def FixedPointLiteral : StmtNode<Expr>;
def FloatingLiteral : StmtNode<Expr>;
def ImaginaryLiteral : StmtNode<Expr>;
def StringLiteral : StmtNode<Expr>;
def CharacterLiteral : StmtNode<Expr>;
def ParenExpr : StmtNode<Expr>;
def UnaryOperator : StmtNode<Expr>;
def OffsetOfExpr : StmtNode<Expr>;
def UnaryExprOrTypeTraitExpr : StmtNode<Expr>;
def ArraySubscriptExpr : StmtNode<Expr>;
def MatrixSubscriptExpr : StmtNode<Expr>;
def ArraySectionExpr : StmtNode<Expr>;
def OMPIteratorExpr : StmtNode<Expr>;
def CallExpr : StmtNode<Expr>;
def MemberExpr : StmtNode<Expr>;
def CastExpr : StmtNode<Expr, 1>;
def BinaryOperator : StmtNode<Expr>;
def CompoundAssignOperator : StmtNode<BinaryOperator>;
def AbstractConditionalOperator : StmtNode<Expr, 1>;
def ConditionalOperator : StmtNode<AbstractConditionalOperator>;
def BinaryConditionalOperator : StmtNode<AbstractConditionalOperator>;
def ImplicitCastExpr : StmtNode<CastExpr>;
def ExplicitCastExpr : StmtNode<CastExpr, 1>;
def CStyleCastExpr : StmtNode<ExplicitCastExpr>;
def OMPArrayShapingExpr : StmtNode<Expr>;
def CompoundLiteralExpr : StmtNode<Expr>;
def ExtVectorElementExpr : StmtNode<Expr>;
def InitListExpr : StmtNode<Expr>;
def DesignatedInitExpr : StmtNode<Expr>;
def DesignatedInitUpdateExpr : StmtNode<Expr>;
def ImplicitValueInitExpr : StmtNode<Expr>;
def NoInitExpr : StmtNode<Expr>;
def ArrayInitLoopExpr : StmtNode<Expr>;
def ArrayInitIndexExpr : StmtNode<Expr>;
def ParenListExpr : StmtNode<Expr>;
def VAArgExpr : StmtNode<Expr>;
def GenericSelectionExpr : StmtNode<Expr>;
def PseudoObjectExpr : StmtNode<Expr>;
def SourceLocExpr : StmtNode<Expr>;

// Wrapper expressions
def FullExpr : StmtNode<Expr, 1>;
def ConstantExpr : StmtNode<FullExpr>;

// Atomic expressions
def AtomicExpr : StmtNode<Expr>;

// GNU Extensions.
def AddrLabelExpr : StmtNode<Expr>;
def StmtExpr : StmtNode<Expr>;
def ChooseExpr : StmtNode<Expr>;
def GNUNullExpr : StmtNode<Expr>;

// C++ Expressions.
def CXXOperatorCallExpr : StmtNode<CallExpr>;
def CXXMemberCallExpr : StmtNode<CallExpr>;
def CXXRewrittenBinaryOperator : StmtNode<Expr>;
def CXXNamedCastExpr : StmtNode<ExplicitCastExpr, 1>;
def CXXStaticCastExpr : StmtNode<CXXNamedCastExpr>;
def CXXDynamicCastExpr : StmtNode<CXXNamedCastExpr>;
def CXXReinterpretCastExpr : StmtNode<CXXNamedCastExpr>;
def CXXConstCastExpr : StmtNode<CXXNamedCastExpr>;
def CXXAddrspaceCastExpr : StmtNode<CXXNamedCastExpr>;
def CXXFunctionalCastExpr : StmtNode<ExplicitCastExpr>;
def CXXTypeidExpr : StmtNode<Expr>;
def UserDefinedLiteral : StmtNode<CallExpr>;
def CXXBoolLiteralExpr : StmtNode<Expr>;
def CXXNullPtrLiteralExpr : StmtNode<Expr>;
def CXXThisExpr : StmtNode<Expr>;
def CXXThrowExpr : StmtNode<Expr>;
def CXXDefaultArgExpr : StmtNode<Expr>;
def CXXDefaultInitExpr : StmtNode<Expr>;
def CXXScalarValueInitExpr : StmtNode<Expr>;
def CXXStdInitializerListExpr : StmtNode<Expr>;
def CXXNewExpr : StmtNode<Expr>;
def CXXDeleteExpr : StmtNode<Expr>;
def CXXPseudoDestructorExpr : StmtNode<Expr>;
def TypeTraitExpr : StmtNode<Expr>;
def ArrayTypeTraitExpr : StmtNode<Expr>;
def ExpressionTraitExpr : StmtNode<Expr>;
def DependentScopeDeclRefExpr : StmtNode<Expr>;
def CXXConstructExpr : StmtNode<Expr>;
def CXXInheritedCtorInitExpr : StmtNode<Expr>;
def CXXBindTemporaryExpr : StmtNode<Expr>;
def ExprWithCleanups : StmtNode<FullExpr>;
def CXXTemporaryObjectExpr : StmtNode<CXXConstructExpr>;
def CXXUnresolvedConstructExpr : StmtNode<Expr>;
def CXXDependentScopeMemberExpr : StmtNode<Expr>;
def OverloadExpr : StmtNode<Expr, 1>;
def UnresolvedLookupExpr : StmtNode<OverloadExpr>;
def UnresolvedMemberExpr : StmtNode<OverloadExpr>;
def CXXNoexceptExpr : StmtNode<Expr>;
def PackExpansionExpr : StmtNode<Expr>;
def SizeOfPackExpr : StmtNode<Expr>;
def PackIndexingExpr : StmtNode<Expr>;
def SubstNonTypeTemplateParmExpr : StmtNode<Expr>;
def SubstNonTypeTemplateParmPackExpr : StmtNode<Expr>;
def FunctionParmPackExpr : StmtNode<Expr>;
def MaterializeTemporaryExpr : StmtNode<Expr>;
def LambdaExpr : StmtNode<Expr>;
def CXXFoldExpr : StmtNode<Expr>;
def CXXParenListInitExpr: StmtNode<Expr>;

// C++ Coroutines expressions
def CoroutineSuspendExpr : StmtNode<Expr, 1>;
def CoawaitExpr : StmtNode<CoroutineSuspendExpr>;
def DependentCoawaitExpr : StmtNode<Expr>;
def CoyieldExpr : StmtNode<CoroutineSuspendExpr>;

// C++20 Concepts expressions
def ConceptSpecializationExpr : StmtNode<Expr>;
def RequiresExpr : StmtNode<Expr>;

// Obj-C Expressions.
def ObjCStringLiteral : StmtNode<Expr>;
def ObjCBoxedExpr : StmtNode<Expr>;
def ObjCArrayLiteral : StmtNode<Expr>;
def ObjCDictionaryLiteral : StmtNode<Expr>;
def ObjCEncodeExpr : StmtNode<Expr>;
def ObjCMessageExpr : StmtNode<Expr>;
def ObjCSelectorExpr : StmtNode<Expr>;
def ObjCProtocolExpr : StmtNode<Expr>;
def ObjCIvarRefExpr : StmtNode<Expr>;
def ObjCPropertyRefExpr : StmtNode<Expr>;
def ObjCIsaExpr : StmtNode<Expr>;
def ObjCIndirectCopyRestoreExpr : StmtNode<Expr>;
def ObjCBoolLiteralExpr : StmtNode<Expr>;
def ObjCSubscriptRefExpr : StmtNode<Expr>;
def ObjCAvailabilityCheckExpr : StmtNode<Expr>;

// Obj-C ARC Expressions.
def ObjCBridgedCastExpr : StmtNode<ExplicitCastExpr>;

// CUDA Expressions.
def CUDAKernelCallExpr : StmtNode<CallExpr>;

// Clang Extensions.
def ShuffleVectorExpr : StmtNode<Expr>;
def ConvertVectorExpr : StmtNode<Expr>;
def BlockExpr : StmtNode<Expr>;
def OpaqueValueExpr : StmtNode<Expr>;
def TypoExpr : StmtNode<Expr>;
def RecoveryExpr : StmtNode<Expr>;
def BuiltinBitCastExpr : StmtNode<ExplicitCastExpr>;
def EmbedExpr : StmtNode<Expr>;

// Microsoft Extensions.
def MSPropertyRefExpr : StmtNode<Expr>;
def MSPropertySubscriptExpr : StmtNode<Expr>;
def CXXUuidofExpr : StmtNode<Expr>;
def SEHTryStmt : StmtNode<Stmt>;
def SEHExceptStmt : StmtNode<Stmt>;
def SEHFinallyStmt : StmtNode<Stmt>;
def SEHLeaveStmt : StmtNode<Stmt>;
def MSDependentExistsStmt : StmtNode<Stmt>;

// OpenCL Extensions.
def AsTypeExpr : StmtNode<Expr>;

// OpenMP Directives.
def OMPCanonicalLoop : StmtNode<Stmt>;
def OMPExecutableDirective : StmtNode<Stmt, 1>;
def OMPMetaDirective : StmtNode<OMPExecutableDirective>;
def OMPLoopBasedDirective : StmtNode<OMPExecutableDirective, 1>;
def OMPLoopDirective : StmtNode<OMPLoopBasedDirective, 1>;
def OMPParallelDirective : StmtNode<OMPExecutableDirective>;
def OMPSimdDirective : StmtNode<OMPLoopDirective>;
def OMPLoopTransformationDirective : StmtNode<OMPLoopBasedDirective, 1>;
def OMPTileDirective : StmtNode<OMPLoopTransformationDirective>;
def OMPUnrollDirective : StmtNode<OMPLoopTransformationDirective>;
def OMPReverseDirective : StmtNode<OMPLoopTransformationDirective>;
def OMPInterchangeDirective : StmtNode<OMPLoopTransformationDirective>;
def OMPForDirective : StmtNode<OMPLoopDirective>;
def OMPForSimdDirective : StmtNode<OMPLoopDirective>;
def OMPSectionsDirective : StmtNode<OMPExecutableDirective>;
def OMPSectionDirective : StmtNode<OMPExecutableDirective>;
def OMPSingleDirective : StmtNode<OMPExecutableDirective>;
def OMPMasterDirective : StmtNode<OMPExecutableDirective>;
def OMPCriticalDirective : StmtNode<OMPExecutableDirective>;
def OMPParallelForDirective : StmtNode<OMPLoopDirective>;
def OMPParallelForSimdDirective : StmtNode<OMPLoopDirective>;
def OMPParallelMasterDirective : StmtNode<OMPExecutableDirective>;
def OMPParallelSectionsDirective : StmtNode<OMPExecutableDirective>;
def OMPTaskDirective : StmtNode<OMPExecutableDirective>;
def OMPTaskyieldDirective : StmtNode<OMPExecutableDirective>;
def OMPBarrierDirective : StmtNode<OMPExecutableDirective>;
def OMPTaskwaitDirective : StmtNode<OMPExecutableDirective>;
def OMPTaskgroupDirective : StmtNode<OMPExecutableDirective>;
def OMPFlushDirective : StmtNode<OMPExecutableDirective>;
def OMPDepobjDirective : StmtNode<OMPExecutableDirective>;
def OMPScanDirective : StmtNode<OMPExecutableDirective>;
def OMPOrderedDirective : StmtNode<OMPExecutableDirective>;
def OMPAtomicDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetDataDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetEnterDataDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetExitDataDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetParallelDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetParallelForDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetUpdateDirective : StmtNode<OMPExecutableDirective>;
def OMPTeamsDirective : StmtNode<OMPExecutableDirective>;
def OMPCancellationPointDirective : StmtNode<OMPExecutableDirective>;
def OMPCancelDirective : StmtNode<OMPExecutableDirective>;
def OMPScopeDirective : StmtNode<OMPExecutableDirective>;
def OMPTaskLoopDirective : StmtNode<OMPLoopDirective>;
def OMPTaskLoopSimdDirective : StmtNode<OMPLoopDirective>;
def OMPMasterTaskLoopDirective : StmtNode<OMPLoopDirective>;
def OMPMasterTaskLoopSimdDirective : StmtNode<OMPLoopDirective>;
def OMPParallelMasterTaskLoopDirective : StmtNode<OMPLoopDirective>;
def OMPParallelMasterTaskLoopSimdDirective : StmtNode<OMPLoopDirective>;
def OMPMaskedTaskLoopDirective : StmtNode<OMPLoopDirective>;
def OMPMaskedTaskLoopSimdDirective : StmtNode<OMPLoopDirective>;
def OMPParallelMaskedTaskLoopDirective : StmtNode<OMPLoopDirective>;
def OMPParallelMaskedTaskLoopSimdDirective : StmtNode<OMPLoopDirective>;
def OMPDistributeDirective : StmtNode<OMPLoopDirective>;
def OMPDistributeParallelForDirective : StmtNode<OMPLoopDirective>;
def OMPDistributeParallelForSimdDirective : StmtNode<OMPLoopDirective>;
def OMPDistributeSimdDirective : StmtNode<OMPLoopDirective>;
def OMPTargetParallelForSimdDirective : StmtNode<OMPLoopDirective>;
def OMPTargetSimdDirective : StmtNode<OMPLoopDirective>;
def OMPTeamsDistributeDirective : StmtNode<OMPLoopDirective>;
def OMPTeamsDistributeSimdDirective : StmtNode<OMPLoopDirective>;
def OMPTeamsDistributeParallelForSimdDirective : StmtNode<OMPLoopDirective>;
def OMPTeamsDistributeParallelForDirective : StmtNode<OMPLoopDirective>;
def OMPTargetTeamsDirective : StmtNode<OMPExecutableDirective>;
def OMPTargetTeamsDistributeDirective : StmtNode<OMPLoopDirective>;
def OMPTargetTeamsDistributeParallelForDirective : StmtNode<OMPLoopDirective>;
def OMPTargetTeamsDistributeParallelForSimdDirective : StmtNode<OMPLoopDirective>;
def OMPTargetTeamsDistributeSimdDirective : StmtNode<OMPLoopDirective>;
def OMPInteropDirective : StmtNode<OMPExecutableDirective>;
def OMPDispatchDirective : StmtNode<OMPExecutableDirective>;
def OMPMaskedDirective : StmtNode<OMPExecutableDirective>;
def OMPParallelMaskedDirective : StmtNode<OMPExecutableDirective>;
def OMPGenericLoopDirective : StmtNode<OMPLoopDirective>;
def OMPTeamsGenericLoopDirective : StmtNode<OMPLoopDirective>;
def OMPTargetTeamsGenericLoopDirective : StmtNode<OMPLoopDirective>;
def OMPParallelGenericLoopDirective : StmtNode<OMPLoopDirective>;
def OMPTargetParallelGenericLoopDirective : StmtNode<OMPLoopDirective>;
def OMPErrorDirective : StmtNode<OMPExecutableDirective>;

// OpenACC Constructs.
def OpenACCConstructStmt : StmtNode<Stmt, /*abstract=*/1>;
def OpenACCAssociatedStmtConstruct
    : StmtNode<OpenACCConstructStmt, /*abstract=*/1>;
def OpenACCComputeConstruct : StmtNode<OpenACCAssociatedStmtConstruct>;
def OpenACCLoopConstruct : StmtNode<OpenACCAssociatedStmtConstruct>;
