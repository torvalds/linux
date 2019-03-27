# $FreeBSD$

.include <src.opts.mk>
.include "../clang.pre.mk"

LIB=		clang

CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${OBJTOP}/lib/clang/libllvm
CFLAGS+=	-I${CLANG_SRCS}/lib/Basic
CFLAGS+=	-I${CLANG_SRCS}/lib/Driver

# Ensure FREEBSD_CC_VERSION is defined for Basic/Targets.cpp
CFLAGS.Targets.cpp+= -include ../freebsd_cc_version.h

# Work around GCC bug 67888
.if ${COMPILER_TYPE} == "gcc"
CXXFLAGS.Module.cpp+= -fpermissive
.endif

SRCDIR=		tools/clang/lib

SRCS_FUL+=	ARCMigrate/ARCMT.cpp
SRCS_FUL+=	ARCMigrate/ARCMTActions.cpp
SRCS_FUL+=	ARCMigrate/FileRemapper.cpp
SRCS_FUL+=	ARCMigrate/ObjCMT.cpp
SRCS_FUL+=	ARCMigrate/PlistReporter.cpp
SRCS_FUL+=	ARCMigrate/TransAPIUses.cpp
SRCS_FUL+=	ARCMigrate/TransARCAssign.cpp
SRCS_FUL+=	ARCMigrate/TransAutoreleasePool.cpp
SRCS_FUL+=	ARCMigrate/TransBlockObjCVariable.cpp
SRCS_FUL+=	ARCMigrate/TransEmptyStatementsAndDealloc.cpp
SRCS_FUL+=	ARCMigrate/TransGCAttrs.cpp
SRCS_FUL+=	ARCMigrate/TransGCCalls.cpp
SRCS_FUL+=	ARCMigrate/TransProperties.cpp
SRCS_FUL+=	ARCMigrate/TransProtectedScope.cpp
SRCS_FUL+=	ARCMigrate/TransRetainReleaseDealloc.cpp
SRCS_FUL+=	ARCMigrate/TransUnbridgedCasts.cpp
SRCS_FUL+=	ARCMigrate/TransUnusedInitDelegate.cpp
SRCS_FUL+=	ARCMigrate/TransZeroOutPropsInDealloc.cpp
SRCS_FUL+=	ARCMigrate/TransformActions.cpp
SRCS_FUL+=	ARCMigrate/Transforms.cpp
SRCS_MIN+=	AST/APValue.cpp
SRCS_MIN+=	AST/ASTConsumer.cpp
SRCS_MIN+=	AST/ASTContext.cpp
SRCS_MIN+=	AST/ASTDiagnostic.cpp
SRCS_MIN+=	AST/ASTDumper.cpp
SRCS_MIN+=	AST/ASTImporter.cpp
SRCS_MIN+=	AST/ASTImporterLookupTable.cpp
SRCS_MIN+=	AST/ASTStructuralEquivalence.cpp
SRCS_MIN+=	AST/ASTTypeTraits.cpp
SRCS_MIN+=	AST/AttrImpl.cpp
SRCS_MIN+=	AST/CXXInheritance.cpp
SRCS_MIN+=	AST/Comment.cpp
SRCS_MIN+=	AST/CommentBriefParser.cpp
SRCS_MIN+=	AST/CommentCommandTraits.cpp
SRCS_MIN+=	AST/CommentLexer.cpp
SRCS_MIN+=	AST/CommentParser.cpp
SRCS_MIN+=	AST/CommentSema.cpp
SRCS_MIN+=	AST/ComparisonCategories.cpp
SRCS_FUL+=	AST/DataCollection.cpp
SRCS_MIN+=	AST/Decl.cpp
SRCS_MIN+=	AST/DeclBase.cpp
SRCS_MIN+=	AST/DeclCXX.cpp
SRCS_MIN+=	AST/DeclFriend.cpp
SRCS_MIN+=	AST/DeclGroup.cpp
SRCS_MIN+=	AST/DeclObjC.cpp
SRCS_MIN+=	AST/DeclOpenMP.cpp
SRCS_MIN+=	AST/DeclPrinter.cpp
SRCS_MIN+=	AST/DeclTemplate.cpp
SRCS_MIN+=	AST/DeclarationName.cpp
SRCS_MIN+=	AST/Expr.cpp
SRCS_MIN+=	AST/ExprCXX.cpp
SRCS_MIN+=	AST/ExprClassification.cpp
SRCS_MIN+=	AST/ExprConstant.cpp
SRCS_MIN+=	AST/ExprObjC.cpp
SRCS_LDB+=	AST/ExternalASTMerger.cpp
SRCS_MIN+=	AST/ExternalASTSource.cpp
SRCS_MIN+=	AST/FormatString.cpp
SRCS_MIN+=	AST/InheritViz.cpp
SRCS_MIN+=	AST/ItaniumCXXABI.cpp
SRCS_MIN+=	AST/ItaniumMangle.cpp
SRCS_MIN+=	AST/Mangle.cpp
SRCS_MIN+=	AST/MicrosoftCXXABI.cpp
SRCS_MIN+=	AST/MicrosoftMangle.cpp
SRCS_MIN+=	AST/NSAPI.cpp
SRCS_MIN+=	AST/NestedNameSpecifier.cpp
SRCS_MIN+=	AST/ODRHash.cpp
SRCS_MIN+=	AST/OSLog.cpp
SRCS_MIN+=	AST/OpenMPClause.cpp
SRCS_MIN+=	AST/ParentMap.cpp
SRCS_MIN+=	AST/PrintfFormatString.cpp
SRCS_MIN+=	AST/QualTypeNames.cpp
SRCS_MIN+=	AST/RawCommentList.cpp
SRCS_MIN+=	AST/RecordLayout.cpp
SRCS_MIN+=	AST/RecordLayoutBuilder.cpp
SRCS_MIN+=	AST/ScanfFormatString.cpp
SRCS_MIN+=	AST/SelectorLocationsKind.cpp
SRCS_MIN+=	AST/Stmt.cpp
SRCS_MIN+=	AST/StmtCXX.cpp
SRCS_MIN+=	AST/StmtIterator.cpp
SRCS_MIN+=	AST/StmtObjC.cpp
SRCS_MIN+=	AST/StmtOpenMP.cpp
SRCS_MIN+=	AST/StmtPrinter.cpp
SRCS_MIN+=	AST/StmtProfile.cpp
SRCS_MIN+=	AST/StmtViz.cpp
SRCS_MIN+=	AST/TemplateBase.cpp
SRCS_MIN+=	AST/TemplateName.cpp
SRCS_MIN+=	AST/TextNodeDumper.cpp
SRCS_MIN+=	AST/Type.cpp
SRCS_MIN+=	AST/TypeLoc.cpp
SRCS_MIN+=	AST/TypePrinter.cpp
SRCS_MIN+=	AST/VTTBuilder.cpp
SRCS_MIN+=	AST/VTableBuilder.cpp
SRCS_MIN+=	ASTMatchers/ASTMatchFinder.cpp
SRCS_MIN+=	ASTMatchers/ASTMatchersInternal.cpp
SRCS_MIN+=	ASTMatchers/Dynamic/Diagnostics.cpp
SRCS_MIN+=	ASTMatchers/Dynamic/Parser.cpp
SRCS_MIN+=	ASTMatchers/Dynamic/Registry.cpp
SRCS_MIN+=	ASTMatchers/Dynamic/VariantValue.cpp
SRCS_MIN+=	Analysis/AnalysisDeclContext.cpp
SRCS_MIN+=	Analysis/BodyFarm.cpp
SRCS_MIN+=	Analysis/CFG.cpp
SRCS_MIN+=	Analysis/CFGReachabilityAnalysis.cpp
SRCS_MIN+=	Analysis/CFGStmtMap.cpp
SRCS_FUL+=	Analysis/CallGraph.cpp
SRCS_MIN+=	Analysis/CloneDetection.cpp
SRCS_MIN+=	Analysis/CocoaConventions.cpp
SRCS_FUL+=	Analysis/CodeInjector.cpp
SRCS_MIN+=	Analysis/Consumed.cpp
SRCS_MIN+=	Analysis/ConstructionContext.cpp
SRCS_FUL+=	Analysis/Dominators.cpp
SRCS_MIN+=	Analysis/LiveVariables.cpp
SRCS_MIN+=	Analysis/ObjCNoReturn.cpp
SRCS_MIN+=	Analysis/PostOrderCFGView.cpp
SRCS_FUL+=	Analysis/ProgramPoint.cpp
SRCS_MIN+=	Analysis/ReachableCode.cpp
SRCS_MIN+=	Analysis/ThreadSafety.cpp
SRCS_MIN+=	Analysis/ThreadSafetyCommon.cpp
SRCS_MIN+=	Analysis/ThreadSafetyLogical.cpp
SRCS_MIN+=	Analysis/ThreadSafetyTIL.cpp
SRCS_MIN+=	Analysis/UninitializedValues.cpp
SRCS_MIN+=	Basic/Attributes.cpp
SRCS_MIN+=	Basic/Builtins.cpp
SRCS_MIN+=	Basic/CharInfo.cpp
SRCS_MIN+=	Basic/CodeGenOptions.cpp
SRCS_MIN+=	Basic/Cuda.cpp
SRCS_MIN+=	Basic/Diagnostic.cpp
SRCS_MIN+=	Basic/DiagnosticIDs.cpp
SRCS_MIN+=	Basic/DiagnosticOptions.cpp
SRCS_MIN+=	Basic/FileManager.cpp
SRCS_MIN+=	Basic/FileSystemStatCache.cpp
SRCS_MIN+=	Basic/FixedPoint.cpp
SRCS_MIN+=	Basic/IdentifierTable.cpp
SRCS_MIN+=	Basic/LangOptions.cpp
SRCS_MIN+=	Basic/MemoryBufferCache.cpp
SRCS_MIN+=	Basic/Module.cpp
SRCS_MIN+=	Basic/ObjCRuntime.cpp
SRCS_MIN+=	Basic/OpenMPKinds.cpp
SRCS_MIN+=	Basic/OperatorPrecedence.cpp
SRCS_MIN+=	Basic/SanitizerBlacklist.cpp
SRCS_MIN+=	Basic/SanitizerSpecialCaseList.cpp
SRCS_MIN+=	Basic/Sanitizers.cpp
SRCS_MIN+=	Basic/SourceLocation.cpp
SRCS_MIN+=	Basic/SourceManager.cpp
SRCS_MIN+=	Basic/TargetInfo.cpp
SRCS_MIN+=	Basic/Targets.cpp
SRCS_MIN+=	Basic/Targets/AArch64.cpp
SRCS_MIN+=	Basic/Targets/AMDGPU.cpp
SRCS_MIN+=	Basic/Targets/ARC.cpp
SRCS_MIN+=	Basic/Targets/ARM.cpp
SRCS_MIN+=	Basic/Targets/AVR.cpp
SRCS_MIN+=	Basic/Targets/BPF.cpp
SRCS_MIN+=	Basic/Targets/Hexagon.cpp
SRCS_MIN+=	Basic/Targets/Lanai.cpp
SRCS_MIN+=	Basic/Targets/Le64.cpp
SRCS_MIN+=	Basic/Targets/MSP430.cpp
SRCS_MIN+=	Basic/Targets/Mips.cpp
SRCS_MIN+=	Basic/Targets/NVPTX.cpp
SRCS_MIN+=	Basic/Targets/OSTargets.cpp
SRCS_MIN+=	Basic/Targets/PNaCl.cpp
SRCS_MIN+=	Basic/Targets/PPC.cpp
SRCS_MIN+=	Basic/Targets/RISCV.cpp
SRCS_MIN+=	Basic/Targets/SPIR.cpp
SRCS_MIN+=	Basic/Targets/Sparc.cpp
SRCS_MIN+=	Basic/Targets/SystemZ.cpp
SRCS_MIN+=	Basic/Targets/TCE.cpp
SRCS_MIN+=	Basic/Targets/WebAssembly.cpp
SRCS_MIN+=	Basic/Targets/X86.cpp
SRCS_MIN+=	Basic/Targets/XCore.cpp
SRCS_MIN+=	Basic/TokenKinds.cpp
SRCS_MIN+=	Basic/Version.cpp
SRCS_MIN+=	Basic/Warnings.cpp
SRCS_MIN+=	Basic/XRayInstr.cpp
SRCS_MIN+=	Basic/XRayLists.cpp
SRCS_MIN+=	CodeGen/BackendUtil.cpp
SRCS_MIN+=	CodeGen/CGAtomic.cpp
SRCS_MIN+=	CodeGen/CGBlocks.cpp
SRCS_MIN+=	CodeGen/CGBuiltin.cpp
SRCS_MIN+=	CodeGen/CGCUDANV.cpp
SRCS_MIN+=	CodeGen/CGCUDARuntime.cpp
SRCS_MIN+=	CodeGen/CGCXX.cpp
SRCS_MIN+=	CodeGen/CGCXXABI.cpp
SRCS_MIN+=	CodeGen/CGCall.cpp
SRCS_MIN+=	CodeGen/CGClass.cpp
SRCS_MIN+=	CodeGen/CGCleanup.cpp
SRCS_MIN+=	CodeGen/CGCoroutine.cpp
SRCS_MIN+=	CodeGen/CGDebugInfo.cpp
SRCS_MIN+=	CodeGen/CGDecl.cpp
SRCS_MIN+=	CodeGen/CGDeclCXX.cpp
SRCS_MIN+=	CodeGen/CGException.cpp
SRCS_MIN+=	CodeGen/CGExpr.cpp
SRCS_MIN+=	CodeGen/CGExprAgg.cpp
SRCS_MIN+=	CodeGen/CGExprCXX.cpp
SRCS_MIN+=	CodeGen/CGExprComplex.cpp
SRCS_MIN+=	CodeGen/CGExprConstant.cpp
SRCS_MIN+=	CodeGen/CGExprScalar.cpp
SRCS_MIN+=	CodeGen/CGGPUBuiltin.cpp
SRCS_MIN+=	CodeGen/CGLoopInfo.cpp
SRCS_MIN+=	CodeGen/CGNonTrivialStruct.cpp
SRCS_MIN+=	CodeGen/CGObjC.cpp
SRCS_MIN+=	CodeGen/CGObjCGNU.cpp
SRCS_MIN+=	CodeGen/CGObjCMac.cpp
SRCS_MIN+=	CodeGen/CGObjCRuntime.cpp
SRCS_MIN+=	CodeGen/CGOpenCLRuntime.cpp
SRCS_MIN+=	CodeGen/CGOpenMPRuntime.cpp
SRCS_MIN+=	CodeGen/CGOpenMPRuntimeNVPTX.cpp
SRCS_MIN+=	CodeGen/CGRecordLayoutBuilder.cpp
SRCS_MIN+=	CodeGen/CGStmt.cpp
SRCS_MIN+=	CodeGen/CGStmtOpenMP.cpp
SRCS_MIN+=	CodeGen/CGVTT.cpp
SRCS_MIN+=	CodeGen/CGVTables.cpp
SRCS_MIN+=	CodeGen/CodeGenABITypes.cpp
SRCS_MIN+=	CodeGen/CodeGenAction.cpp
SRCS_MIN+=	CodeGen/CodeGenFunction.cpp
SRCS_MIN+=	CodeGen/CodeGenModule.cpp
SRCS_MIN+=	CodeGen/CodeGenPGO.cpp
SRCS_MIN+=	CodeGen/CodeGenTBAA.cpp
SRCS_MIN+=	CodeGen/CodeGenTypes.cpp
SRCS_MIN+=	CodeGen/ConstantInitBuilder.cpp
SRCS_MIN+=	CodeGen/CoverageMappingGen.cpp
SRCS_MIN+=	CodeGen/ItaniumCXXABI.cpp
SRCS_MIN+=	CodeGen/MacroPPCallbacks.cpp
SRCS_MIN+=	CodeGen/MicrosoftCXXABI.cpp
SRCS_MIN+=	CodeGen/ModuleBuilder.cpp
SRCS_MIN+=	CodeGen/ObjectFilePCHContainerOperations.cpp
SRCS_MIN+=	CodeGen/SanitizerMetadata.cpp
SRCS_MIN+=	CodeGen/SwiftCallingConv.cpp
SRCS_MIN+=	CodeGen/TargetInfo.cpp
SRCS_MIN+=	CodeGen/VarBypassDetector.cpp
SRCS_FUL+=	CrossTU/CrossTranslationUnit.cpp
SRCS_MIN+=	Driver/Action.cpp
SRCS_MIN+=	Driver/Compilation.cpp
SRCS_MIN+=	Driver/DarwinSDKInfo.cpp
SRCS_MIN+=	Driver/Distro.cpp
SRCS_MIN+=	Driver/Driver.cpp
SRCS_MIN+=	Driver/DriverOptions.cpp
SRCS_MIN+=	Driver/Job.cpp
SRCS_MIN+=	Driver/Multilib.cpp
SRCS_MIN+=	Driver/Phases.cpp
SRCS_MIN+=	Driver/SanitizerArgs.cpp
SRCS_MIN+=	Driver/Tool.cpp
SRCS_MIN+=	Driver/ToolChain.cpp
SRCS_MIN+=	Driver/ToolChains/AMDGPU.cpp
SRCS_MIN+=	Driver/ToolChains/AVR.cpp
SRCS_MIN+=	Driver/ToolChains/Ananas.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/AArch64.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/ARM.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/Mips.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/PPC.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/RISCV.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/Sparc.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/SystemZ.cpp
SRCS_MIN+=	Driver/ToolChains/Arch/X86.cpp
SRCS_MIN+=	Driver/ToolChains/BareMetal.cpp
SRCS_MIN+=	Driver/ToolChains/Clang.cpp
SRCS_MIN+=	Driver/ToolChains/CloudABI.cpp
SRCS_MIN+=	Driver/ToolChains/CommonArgs.cpp
SRCS_MIN+=	Driver/ToolChains/Contiki.cpp
SRCS_MIN+=	Driver/ToolChains/CrossWindows.cpp
SRCS_MIN+=	Driver/ToolChains/Cuda.cpp
SRCS_MIN+=	Driver/ToolChains/Darwin.cpp
SRCS_MIN+=	Driver/ToolChains/DragonFly.cpp
SRCS_MIN+=	Driver/ToolChains/FreeBSD.cpp
SRCS_MIN+=	Driver/ToolChains/Fuchsia.cpp
SRCS_MIN+=	Driver/ToolChains/Gnu.cpp
SRCS_MIN+=	Driver/ToolChains/HIP.cpp
SRCS_MIN+=	Driver/ToolChains/Haiku.cpp
SRCS_MIN+=	Driver/ToolChains/Hexagon.cpp
SRCS_MIN+=	Driver/ToolChains/Hurd.cpp
SRCS_MIN+=	Driver/ToolChains/Linux.cpp
SRCS_MIN+=	Driver/ToolChains/MSP430.cpp
SRCS_MIN+=	Driver/ToolChains/MSVC.cpp
SRCS_MIN+=	Driver/ToolChains/MinGW.cpp
SRCS_MIN+=	Driver/ToolChains/Minix.cpp
SRCS_MIN+=	Driver/ToolChains/MipsLinux.cpp
SRCS_MIN+=	Driver/ToolChains/Myriad.cpp
SRCS_MIN+=	Driver/ToolChains/NaCl.cpp
SRCS_MIN+=	Driver/ToolChains/NetBSD.cpp
SRCS_MIN+=	Driver/ToolChains/OpenBSD.cpp
SRCS_MIN+=	Driver/ToolChains/PS4CPU.cpp
SRCS_MIN+=	Driver/ToolChains/RISCVToolchain.cpp
SRCS_MIN+=	Driver/ToolChains/Solaris.cpp
SRCS_MIN+=	Driver/ToolChains/TCE.cpp
SRCS_MIN+=	Driver/ToolChains/WebAssembly.cpp
SRCS_MIN+=	Driver/ToolChains/XCore.cpp
SRCS_MIN+=	Driver/Types.cpp
SRCS_MIN+=	Driver/XRayArgs.cpp
SRCS_MIN+=	Edit/Commit.cpp
SRCS_MIN+=	Edit/EditedSource.cpp
SRCS_MIN+=	Edit/RewriteObjCFoundationAPI.cpp
SRCS_EXT+=	Format/AffectedRangeManager.cpp
SRCS_EXT+=	Format/BreakableToken.cpp
SRCS_EXT+=	Format/ContinuationIndenter.cpp
SRCS_EXT+=	Format/Format.cpp
SRCS_EXT+=	Format/FormatToken.cpp
SRCS_EXT+=	Format/FormatTokenLexer.cpp
SRCS_EXT+=	Format/NamespaceEndCommentsFixer.cpp
SRCS_EXT+=	Format/SortJavaScriptImports.cpp
SRCS_EXT+=	Format/TokenAnalyzer.cpp
SRCS_EXT+=	Format/TokenAnnotator.cpp
SRCS_EXT+=	Format/UnwrappedLineFormatter.cpp
SRCS_EXT+=	Format/UnwrappedLineParser.cpp
SRCS_EXT+=	Format/UsingDeclarationsSorter.cpp
SRCS_EXT+=	Format/WhitespaceManager.cpp
SRCS_MIN+=	Frontend/ASTConsumers.cpp
SRCS_MIN+=	Frontend/ASTMerge.cpp
SRCS_MIN+=	Frontend/ASTUnit.cpp
SRCS_MIN+=	Frontend/ChainedDiagnosticConsumer.cpp
SRCS_MIN+=	Frontend/ChainedIncludesSource.cpp
SRCS_MIN+=	Frontend/CompilerInstance.cpp
SRCS_MIN+=	Frontend/CompilerInvocation.cpp
SRCS_MIN+=	Frontend/CreateInvocationFromCommandLine.cpp
SRCS_MIN+=	Frontend/DependencyFile.cpp
SRCS_MIN+=	Frontend/DependencyGraph.cpp
SRCS_MIN+=	Frontend/DiagnosticRenderer.cpp
SRCS_MIN+=	Frontend/FrontendAction.cpp
SRCS_MIN+=	Frontend/FrontendActions.cpp
SRCS_MIN+=	Frontend/FrontendOptions.cpp
SRCS_MIN+=	Frontend/FrontendTiming.cpp
SRCS_MIN+=	Frontend/HeaderIncludeGen.cpp
SRCS_MIN+=	Frontend/InitHeaderSearch.cpp
SRCS_MIN+=	Frontend/InitPreprocessor.cpp
SRCS_MIN+=	Frontend/LangStandards.cpp
SRCS_MIN+=	Frontend/LayoutOverrideSource.cpp
SRCS_MIN+=	Frontend/LogDiagnosticPrinter.cpp
SRCS_MIN+=	Frontend/ModuleDependencyCollector.cpp
SRCS_MIN+=	Frontend/MultiplexConsumer.cpp
SRCS_MIN+=	Frontend/PrecompiledPreamble.cpp
SRCS_MIN+=	Frontend/PrintPreprocessedOutput.cpp
SRCS_MIN+=	Frontend/Rewrite/FixItRewriter.cpp
SRCS_MIN+=	Frontend/Rewrite/FrontendActions.cpp
SRCS_MIN+=	Frontend/Rewrite/HTMLPrint.cpp
SRCS_MIN+=	Frontend/Rewrite/InclusionRewriter.cpp
SRCS_MIN+=	Frontend/Rewrite/RewriteMacros.cpp
SRCS_MIN+=	Frontend/Rewrite/RewriteTest.cpp
SRCS_MIN+=	Frontend/SerializedDiagnosticPrinter.cpp
SRCS_MIN+=	Frontend/SerializedDiagnosticReader.cpp
SRCS_MIN+=	Frontend/TestModuleFileExtension.cpp
SRCS_MIN+=	Frontend/TextDiagnostic.cpp
SRCS_MIN+=	Frontend/TextDiagnosticBuffer.cpp
SRCS_MIN+=	Frontend/TextDiagnosticPrinter.cpp
SRCS_MIN+=	Frontend/VerifyDiagnosticConsumer.cpp
SRCS_MIN+=	FrontendTool/ExecuteCompilerInvocation.cpp
SRCS_MIN+=	Index/CodegenNameGenerator.cpp
SRCS_MIN+=	Index/CommentToXML.cpp
SRCS_MIN+=	Index/IndexBody.cpp
SRCS_MIN+=	Index/IndexDecl.cpp
SRCS_MIN+=	Index/IndexSymbol.cpp
SRCS_MIN+=	Index/IndexTypeSourceInfo.cpp
SRCS_MIN+=	Index/IndexingAction.cpp
SRCS_MIN+=	Index/IndexingContext.cpp
SRCS_MIN+=	Index/USRGeneration.cpp
SRCS_MIN+=	Lex/HeaderMap.cpp
SRCS_MIN+=	Lex/HeaderSearch.cpp
SRCS_MIN+=	Lex/Lexer.cpp
SRCS_MIN+=	Lex/LiteralSupport.cpp
SRCS_MIN+=	Lex/MacroArgs.cpp
SRCS_MIN+=	Lex/MacroInfo.cpp
SRCS_MIN+=	Lex/ModuleMap.cpp
SRCS_MIN+=	Lex/PPCaching.cpp
SRCS_MIN+=	Lex/PPCallbacks.cpp
SRCS_MIN+=	Lex/PPConditionalDirectiveRecord.cpp
SRCS_MIN+=	Lex/PPDirectives.cpp
SRCS_MIN+=	Lex/PPExpressions.cpp
SRCS_MIN+=	Lex/PPLexerChange.cpp
SRCS_MIN+=	Lex/PPMacroExpansion.cpp
SRCS_MIN+=	Lex/Pragma.cpp
SRCS_MIN+=	Lex/PreprocessingRecord.cpp
SRCS_MIN+=	Lex/Preprocessor.cpp
SRCS_MIN+=	Lex/PreprocessorLexer.cpp
SRCS_MIN+=	Lex/ScratchBuffer.cpp
SRCS_MIN+=	Lex/TokenConcatenation.cpp
SRCS_MIN+=	Lex/TokenLexer.cpp
SRCS_MIN+=	Parse/ParseAST.cpp
SRCS_MIN+=	Parse/ParseCXXInlineMethods.cpp
SRCS_MIN+=	Parse/ParseDecl.cpp
SRCS_MIN+=	Parse/ParseDeclCXX.cpp
SRCS_MIN+=	Parse/ParseExpr.cpp
SRCS_MIN+=	Parse/ParseExprCXX.cpp
SRCS_MIN+=	Parse/ParseInit.cpp
SRCS_MIN+=	Parse/ParseObjc.cpp
SRCS_MIN+=	Parse/ParseOpenMP.cpp
SRCS_MIN+=	Parse/ParsePragma.cpp
SRCS_MIN+=	Parse/ParseStmt.cpp
SRCS_MIN+=	Parse/ParseStmtAsm.cpp
SRCS_MIN+=	Parse/ParseTemplate.cpp
SRCS_MIN+=	Parse/ParseTentative.cpp
SRCS_MIN+=	Parse/Parser.cpp
SRCS_MIN+=	Rewrite/DeltaTree.cpp
SRCS_MIN+=	Rewrite/HTMLRewrite.cpp
SRCS_MIN+=	Rewrite/RewriteRope.cpp
SRCS_MIN+=	Rewrite/Rewriter.cpp
SRCS_MIN+=	Rewrite/TokenRewriter.cpp
SRCS_MIN+=	Sema/AnalysisBasedWarnings.cpp
SRCS_MIN+=	Sema/CodeCompleteConsumer.cpp
SRCS_MIN+=	Sema/DeclSpec.cpp
SRCS_MIN+=	Sema/DelayedDiagnostic.cpp
SRCS_MIN+=	Sema/IdentifierResolver.cpp
SRCS_MIN+=	Sema/JumpDiagnostics.cpp
SRCS_MIN+=	Sema/MultiplexExternalSemaSource.cpp
SRCS_MIN+=	Sema/ParsedAttr.cpp
SRCS_MIN+=	Sema/Scope.cpp
SRCS_MIN+=	Sema/ScopeInfo.cpp
SRCS_MIN+=	Sema/Sema.cpp
SRCS_MIN+=	Sema/SemaAccess.cpp
SRCS_MIN+=	Sema/SemaAttr.cpp
SRCS_MIN+=	Sema/SemaCUDA.cpp
SRCS_MIN+=	Sema/SemaCXXScopeSpec.cpp
SRCS_MIN+=	Sema/SemaCast.cpp
SRCS_MIN+=	Sema/SemaChecking.cpp
SRCS_MIN+=	Sema/SemaCodeComplete.cpp
SRCS_MIN+=	Sema/SemaConsumer.cpp
SRCS_MIN+=	Sema/SemaCoroutine.cpp
SRCS_MIN+=	Sema/SemaDecl.cpp
SRCS_MIN+=	Sema/SemaDeclAttr.cpp
SRCS_MIN+=	Sema/SemaDeclCXX.cpp
SRCS_MIN+=	Sema/SemaDeclObjC.cpp
SRCS_MIN+=	Sema/SemaExceptionSpec.cpp
SRCS_MIN+=	Sema/SemaExpr.cpp
SRCS_MIN+=	Sema/SemaExprCXX.cpp
SRCS_MIN+=	Sema/SemaExprMember.cpp
SRCS_MIN+=	Sema/SemaExprObjC.cpp
SRCS_MIN+=	Sema/SemaFixItUtils.cpp
SRCS_MIN+=	Sema/SemaInit.cpp
SRCS_MIN+=	Sema/SemaLambda.cpp
SRCS_MIN+=	Sema/SemaLookup.cpp
SRCS_MIN+=	Sema/SemaObjCProperty.cpp
SRCS_MIN+=	Sema/SemaOpenMP.cpp
SRCS_MIN+=	Sema/SemaOverload.cpp
SRCS_MIN+=	Sema/SemaPseudoObject.cpp
SRCS_MIN+=	Sema/SemaStmt.cpp
SRCS_MIN+=	Sema/SemaStmtAsm.cpp
SRCS_MIN+=	Sema/SemaStmtAttr.cpp
SRCS_MIN+=	Sema/SemaTemplate.cpp
SRCS_MIN+=	Sema/SemaTemplateDeduction.cpp
SRCS_MIN+=	Sema/SemaTemplateInstantiate.cpp
SRCS_MIN+=	Sema/SemaTemplateInstantiateDecl.cpp
SRCS_MIN+=	Sema/SemaTemplateVariadic.cpp
SRCS_MIN+=	Sema/SemaType.cpp
SRCS_MIN+=	Sema/TypeLocBuilder.cpp
SRCS_MIN+=	Serialization/ASTCommon.cpp
SRCS_MIN+=	Serialization/ASTReader.cpp
SRCS_MIN+=	Serialization/ASTReaderDecl.cpp
SRCS_MIN+=	Serialization/ASTReaderStmt.cpp
SRCS_MIN+=	Serialization/ASTWriter.cpp
SRCS_MIN+=	Serialization/ASTWriterDecl.cpp
SRCS_MIN+=	Serialization/ASTWriterStmt.cpp
SRCS_MIN+=	Serialization/GeneratePCH.cpp
SRCS_MIN+=	Serialization/GlobalModuleIndex.cpp
SRCS_MIN+=	Serialization/Module.cpp
SRCS_MIN+=	Serialization/ModuleFileExtension.cpp
SRCS_MIN+=	Serialization/ModuleManager.cpp
SRCS_MIN+=	Serialization/PCHContainerOperations.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/AnalysisOrderChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/AnalyzerStatsChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ArrayBoundChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ArrayBoundCheckerV2.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/BasicObjCFoundationChecks.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/BlockInCriticalSectionChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/BoolAssignmentChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/BuiltinFunctionChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/GCDAntipatternChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CStringChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CStringSyntaxChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CXXSelfAssignmentChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CallAndMessageChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CastSizeChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CastToStructChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CheckObjCDealloc.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CheckObjCInstMethSignature.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CheckSecuritySyntaxOnly.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CheckSizeofPointer.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ChrootChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/CloneChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ConversionChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DeadStoresChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DebugCheckers.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DeleteWithNonVirtualDtorChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DereferenceChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DirectIvarAssignment.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DivZeroChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DynamicTypeChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/DynamicTypePropagation.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/EnumCastOutOfRangeChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ExprInspectionChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/FixedAddressChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/GTestChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/GenericTaintChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/IdenticalExprChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/InnerPointerChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/IteratorChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/IvarInvalidationChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/LLVMConventionsChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/LocalizationChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MPI-Checker/MPIBugReporter.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MPI-Checker/MPIChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MPI-Checker/MPIFunctionClassifier.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MacOSKeychainAPIChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MacOSXAPIChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MallocChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MallocOverflowSecurityChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MallocSizeofChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MmapWriteExecChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/MoveChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/NSAutoreleasePoolChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/NSErrorChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/NoReturnFunctionChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/NonNullParamChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/NonnullGlobalConstantsChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/NullabilityChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/NumberObjectConversionChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCAtSyncChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCAutoreleaseWriteChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCContainersASTChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCContainersChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCMissingSuperCallChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCPropertyChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCSelfInitChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCSuperDeallocChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ObjCUnusedIVarsChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/PaddingChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/PointerArithChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/PointerSubChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/PthreadLockChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/RetainCountChecker/RetainCountChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/RetainCountChecker/RetainCountDiagnostics.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ReturnPointerRangeChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ReturnUndefChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/RunLoopAutoreleaseLeakChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/SimpleStreamChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/StackAddrEscapeChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/StdLibraryFunctionsChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/StreamChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/TaintTesterChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/TestAfterDivZeroChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/TraversalChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/TrustNonnullChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UndefBranchChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UndefCapturedBlockVarChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UndefResultChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UndefinedArraySubscriptChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UndefinedAssignmentChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UninitializedObject/UninitializedObjectChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UninitializedObject/UninitializedPointee.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UnixAPIChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/UnreachableCodeChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/VLASizeChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/ValistChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/VforkChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Checkers/VirtualCallChecker.cpp
SRCS_FUL+=	StaticAnalyzer/Core/APSIntType.cpp
SRCS_FUL+=	StaticAnalyzer/Core/AnalysisManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/AnalyzerOptions.cpp
SRCS_FUL+=	StaticAnalyzer/Core/BasicValueFactory.cpp
SRCS_FUL+=	StaticAnalyzer/Core/BlockCounter.cpp
SRCS_FUL+=	StaticAnalyzer/Core/BugReporter.cpp
SRCS_FUL+=	StaticAnalyzer/Core/BugReporterVisitors.cpp
SRCS_FUL+=	StaticAnalyzer/Core/CallEvent.cpp
SRCS_FUL+=	StaticAnalyzer/Core/Checker.cpp
SRCS_FUL+=	StaticAnalyzer/Core/CheckerContext.cpp
SRCS_FUL+=	StaticAnalyzer/Core/CheckerHelpers.cpp
SRCS_FUL+=	StaticAnalyzer/Core/CheckerManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/CommonBugCategories.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ConstraintManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/CoreEngine.cpp
SRCS_FUL+=	StaticAnalyzer/Core/DynamicTypeMap.cpp
SRCS_FUL+=	StaticAnalyzer/Core/Environment.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ExplodedGraph.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ExprEngine.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ExprEngineC.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ExprEngineCXX.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ExprEngineCallAndReturn.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ExprEngineObjC.cpp
SRCS_FUL+=	StaticAnalyzer/Core/FunctionSummary.cpp
SRCS_FUL+=	StaticAnalyzer/Core/HTMLDiagnostics.cpp
SRCS_FUL+=	StaticAnalyzer/Core/IssueHash.cpp
SRCS_FUL+=	StaticAnalyzer/Core/LoopUnrolling.cpp
SRCS_FUL+=	StaticAnalyzer/Core/LoopWidening.cpp
SRCS_FUL+=	StaticAnalyzer/Core/MemRegion.cpp
SRCS_FUL+=	StaticAnalyzer/Core/PathDiagnostic.cpp
SRCS_FUL+=	StaticAnalyzer/Core/PlistDiagnostics.cpp
SRCS_FUL+=	StaticAnalyzer/Core/ProgramState.cpp
SRCS_FUL+=	StaticAnalyzer/Core/RangeConstraintManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/RangedConstraintManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/RegionStore.cpp
SRCS_FUL+=	StaticAnalyzer/Core/RetainSummaryManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/SValBuilder.cpp
SRCS_FUL+=	StaticAnalyzer/Core/SVals.cpp
SRCS_FUL+=	StaticAnalyzer/Core/SarifDiagnostics.cpp
SRCS_FUL+=	StaticAnalyzer/Core/SimpleConstraintManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/SimpleSValBuilder.cpp
SRCS_FUL+=	StaticAnalyzer/Core/Store.cpp
SRCS_FUL+=	StaticAnalyzer/Core/SubEngine.cpp
SRCS_FUL+=	StaticAnalyzer/Core/SymbolManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/TaintManager.cpp
SRCS_FUL+=	StaticAnalyzer/Core/WorkList.cpp
SRCS_FUL+=	StaticAnalyzer/Core/Z3ConstraintManager.cpp
SRCS_FUL+=	StaticAnalyzer/Frontend/AnalysisConsumer.cpp
SRCS_FUL+=	StaticAnalyzer/Frontend/CheckerRegistration.cpp
SRCS_FUL+=	StaticAnalyzer/Frontend/CheckerRegistry.cpp
SRCS_FUL+=	StaticAnalyzer/Frontend/FrontendActions.cpp
SRCS_FUL+=	StaticAnalyzer/Frontend/ModelConsumer.cpp
SRCS_FUL+=	StaticAnalyzer/Frontend/ModelInjector.cpp
SRCS_MIN+=	Tooling/ArgumentsAdjusters.cpp
SRCS_MIN+=	Tooling/CommonOptionsParser.cpp
SRCS_MIN+=	Tooling/CompilationDatabase.cpp
SRCS_MIN+=	Tooling/Core/Lookup.cpp
SRCS_EXT+=	Tooling/Core/Replacement.cpp
SRCS_MIN+=	Tooling/FileMatchTrie.cpp
SRCS_MIN+=	Tooling/FixIt.cpp
SRCS_EXT+=	Tooling/Inclusions/HeaderIncludes.cpp
SRCS_EXT+=	Tooling/Inclusions/IncludeStyle.cpp
SRCS_MIN+=	Tooling/JSONCompilationDatabase.cpp
SRCS_MIN+=	Tooling/Refactoring.cpp
SRCS_MIN+=	Tooling/RefactoringCallbacks.cpp
SRCS_MIN+=	Tooling/Tooling.cpp

SRCS_ALL+=	${SRCS_MIN}
.if ${MK_CLANG_EXTRAS} != "no"
SRCS_ALL+=	${SRCS_EXT}
.endif
.if ${MK_CLANG_FULL} != "no"
SRCS_ALL+=	${SRCS_FUL}
.endif
.if ${MK_LLDB} != "no"
SRCS_ALL+=	${SRCS_LDB}
.endif
SRCS+=		${SRCS_ALL:O}

clang/AST/AttrImpl.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-impl \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/AST/AttrImpl.inc

clang/AST/AttrNodeTraverse.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-node-traverse \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/AST/AttrNodeTraverse.inc

clang/AST/AttrTextNodeDump.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-text-node-dump \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/AST/AttrTextNodeDump.inc

clang/AST/AttrVisitor.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-ast-visitor \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/AST/AttrVisitor.inc

clang/AST/Attrs.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-classes \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/AST/Attrs.inc

clang/AST/CommentCommandInfo.inc: \
	${CLANG_SRCS}/include/clang/AST/CommentCommands.td
	${CLANG_TBLGEN} -gen-clang-comment-command-info \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/AST/CommentCommands.td
TGHDRS+=	clang/AST/CommentCommandInfo.inc

clang/AST/CommentCommandList.inc: \
	${CLANG_SRCS}/include/clang/AST/CommentCommands.td
	${CLANG_TBLGEN} -gen-clang-comment-command-list \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/AST/CommentCommands.td
TGHDRS+=	clang/AST/CommentCommandList.inc

clang/AST/CommentHTMLNamedCharacterReferences.inc: \
	${CLANG_SRCS}/include/clang/AST/CommentHTMLNamedCharacterReferences.td
	${CLANG_TBLGEN} -gen-clang-comment-html-named-character-references \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/AST/CommentHTMLNamedCharacterReferences.td
TGHDRS+=	clang/AST/CommentHTMLNamedCharacterReferences.inc

clang/AST/CommentHTMLTags.inc: \
	${CLANG_SRCS}/include/clang/AST/CommentHTMLTags.td
	${CLANG_TBLGEN} -gen-clang-comment-html-tags \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/AST/CommentHTMLTags.td
TGHDRS+=	clang/AST/CommentHTMLTags.inc

clang/AST/CommentHTMLTagsProperties.inc: \
	${CLANG_SRCS}/include/clang/AST/CommentHTMLTags.td
	${CLANG_TBLGEN} -gen-clang-comment-html-tags-properties \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/AST/CommentHTMLTags.td
TGHDRS+=	clang/AST/CommentHTMLTagsProperties.inc

clang/AST/CommentNodes.inc: ${CLANG_SRCS}/include/clang/Basic/CommentNodes.td
	${CLANG_TBLGEN} -gen-clang-comment-nodes \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/CommentNodes.td
TGHDRS+=	clang/AST/CommentNodes.inc

clang/AST/DeclNodes.inc: ${CLANG_SRCS}/include/clang/Basic/DeclNodes.td
	${CLANG_TBLGEN} -gen-clang-decl-nodes \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/DeclNodes.td
TGHDRS+=	clang/AST/DeclNodes.inc

clang/AST/StmtDataCollectors.inc: ${CLANG_SRCS}/include/clang/AST/StmtDataCollectors.td
	${CLANG_TBLGEN} -gen-clang-data-collectors \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/AST/StmtDataCollectors.td
TGHDRS+=	clang/AST/StmtDataCollectors.inc

clang/AST/StmtNodes.inc: ${CLANG_SRCS}/include/clang/Basic/StmtNodes.td
	${CLANG_TBLGEN} -gen-clang-stmt-nodes \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/StmtNodes.td
TGHDRS+=	clang/AST/StmtNodes.inc

clang/Basic/AttrHasAttributeImpl.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-has-attribute-impl \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Basic/AttrHasAttributeImpl.inc

clang/Basic/AttrList.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-list \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Basic/AttrList.inc

clang/Basic/AttrSubMatchRulesList.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-subject-match-rule-list \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Basic/AttrSubMatchRulesList.inc

.for hdr in \
	AST Analysis Comment Common CrossTU Driver Frontend Lex Parse \
	Refactoring Sema Serialization SemaSerialization
clang/Basic/Diagnostic${hdr}Kinds.inc: \
	${CLANG_SRCS}/include/clang/Basic/Diagnostic.td
	${CLANG_TBLGEN} -gen-clang-diags-defs -clang-component=${hdr} \
	    -I ${CLANG_SRCS}/include/clang/Basic -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${CLANG_SRCS}/include/clang/Basic/Diagnostic.td
TGHDRS+=	clang/Basic/Diagnostic${hdr}Kinds.inc
.endfor

clang/Basic/DiagnosticGroups.inc: \
	${CLANG_SRCS}/include/clang/Basic/Diagnostic.td
	${CLANG_TBLGEN} -gen-clang-diag-groups \
	    -I ${CLANG_SRCS}/include/clang/Basic -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${CLANG_SRCS}/include/clang/Basic/Diagnostic.td
TGHDRS+=	clang/Basic/DiagnosticGroups.inc

clang/Basic/DiagnosticIndexName.inc: \
	${CLANG_SRCS}/include/clang/Basic/Diagnostic.td
	${CLANG_TBLGEN} -gen-clang-diags-index-name \
	    -I ${CLANG_SRCS}/include/clang/Basic -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${CLANG_SRCS}/include/clang/Basic/Diagnostic.td
TGHDRS+=	clang/Basic/DiagnosticIndexName.inc

clang/Basic/arm_fp16.inc: ${CLANG_SRCS}/include/clang/Basic/arm_fp16.td
	${CLANG_TBLGEN} -gen-arm-neon-sema \
	    -I ${CLANG_SRCS}/include/clang/Basic -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${CLANG_SRCS}/include/clang/Basic/arm_fp16.td
TGHDRS+=	clang/Basic/arm_fp16.inc

clang/Basic/arm_neon.inc: ${CLANG_SRCS}/include/clang/Basic/arm_neon.td
	${CLANG_TBLGEN} -gen-arm-neon-sema \
	    -I ${CLANG_SRCS}/include/clang/Basic -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${CLANG_SRCS}/include/clang/Basic/arm_neon.td
TGHDRS+=	clang/Basic/arm_neon.inc

clang/Driver/Options.inc: ${CLANG_SRCS}/include/clang/Driver/Options.td
	${LLVM_TBLGEN} -gen-opt-parser-defs \
	    -I ${LLVM_SRCS}/include -I ${CLANG_SRCS}/include/clang/Driver \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Driver/Options.td
TGHDRS+=	clang/Driver/Options.inc

clang/Parse/AttrParserStringSwitches.inc: \
	${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-parser-string-switches \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Parse/AttrParserStringSwitches.inc

clang/Parse/AttrSubMatchRulesParserStringSwitches.inc: \
	${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-subject-match-rules-parser-string-switches \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Parse/AttrSubMatchRulesParserStringSwitches.inc

clang/Sema/AttrParsedAttrImpl.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-parsed-attr-impl \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Sema/AttrParsedAttrImpl.inc

clang/Sema/AttrParsedAttrKinds.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-parsed-attr-kinds \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Sema/AttrParsedAttrKinds.inc

clang/Sema/AttrParsedAttrList.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-parsed-attr-list \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Sema/AttrParsedAttrList.inc

clang/Sema/AttrSpellingListIndex.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-spelling-index \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Sema/AttrSpellingListIndex.inc

clang/Sema/AttrTemplateInstantiate.inc: \
	${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-template-instantiate \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Sema/AttrTemplateInstantiate.inc

clang/Serialization/AttrPCHRead.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-pch-read \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Serialization/AttrPCHRead.inc

clang/Serialization/AttrPCHWrite.inc: ${CLANG_SRCS}/include/clang/Basic/Attr.td
	${CLANG_TBLGEN} -gen-clang-attr-pch-write \
	    -I ${CLANG_SRCS}/include -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/Basic/Attr.td
TGHDRS+=	clang/Serialization/AttrPCHWrite.inc

clang/StaticAnalyzer/Checkers/Checkers.inc: \
	${CLANG_SRCS}/include/clang/StaticAnalyzer/Checkers/Checkers.td
	${CLANG_TBLGEN} -gen-clang-sa-checkers \
	    -I ${CLANG_SRCS}/include/clang/StaticAnalyzer/Checkers \
	    -d ${.TARGET:C/$/.d/} -o ${.TARGET} \
	    ${CLANG_SRCS}/include/clang/StaticAnalyzer/Checkers/Checkers.td
TGHDRS+=	clang/StaticAnalyzer/Checkers/Checkers.inc

DEPENDFILES+=	${TGHDRS:C/$/.d/}
DPSRCS+=	${TGHDRS}
CLEANFILES+=	${TGHDRS} ${TGHDRS:C/$/.d/}

.include "../clang.build.mk"

INTERNALLIB=

.include <bsd.lib.mk>
