//===--- Sema.h - Semantic Analysis & AST Building --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Sema class, which performs semantic analysis and
// builds ASTs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMA_H
#define LLVM_CLANG_SEMA_SEMA_H

#include "clang/APINotes/APINotesManager.h"
#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTFwd.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Availability.h"
#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/LocInfoType.h"
#include "clang/AST/MangleNumberingContext.h"
#include "clang/AST/NSAPI.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/BitmaskEnum.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Cuda.h"
#include "clang/Basic/DarwinSDKInfo.h"
#include "clang/Basic/ExpressionTraits.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/OpenCLOptions.h"
#include "clang/Basic/PragmaKinds.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TemplateKinds.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Sema/AnalysisBasedWarnings.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/CleanupInfo.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/ExternalSemaSource.h"
#include "clang/Sema/IdentifierResolver.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Redeclaration.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaBase.h"
#include "clang/Sema/SemaConcept.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Sema/TypoCorrection.h"
#include "clang/Sema/Weak.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/TinyPtrVector.h"
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace llvm {
class APSInt;
template <typename ValueT, typename ValueInfoT> class DenseSet;
class SmallBitVector;
struct InlineAsmIdentifierInfo;
} // namespace llvm

namespace clang {
class ADLResult;
class ASTConsumer;
class ASTContext;
class ASTMutationListener;
class ASTReader;
class ASTWriter;
class ArrayType;
class ParsedAttr;
class BindingDecl;
class BlockDecl;
class CapturedDecl;
class CXXBasePath;
class CXXBasePaths;
class CXXBindTemporaryExpr;
typedef SmallVector<CXXBaseSpecifier *, 4> CXXCastPath;
class CXXConstructorDecl;
class CXXConversionDecl;
class CXXDeleteExpr;
class CXXDestructorDecl;
class CXXFieldCollector;
class CXXMemberCallExpr;
class CXXMethodDecl;
class CXXScopeSpec;
class CXXTemporary;
class CXXTryStmt;
class CallExpr;
class ClassTemplateDecl;
class ClassTemplatePartialSpecializationDecl;
class ClassTemplateSpecializationDecl;
class VarTemplatePartialSpecializationDecl;
class CodeCompleteConsumer;
class CodeCompletionAllocator;
class CodeCompletionTUInfo;
class CodeCompletionResult;
class CoroutineBodyStmt;
class Decl;
class DeclAccessPair;
class DeclContext;
class DeclRefExpr;
class DeclaratorDecl;
class DeducedTemplateArgument;
class DependentDiagnostic;
class DesignatedInitExpr;
class Designation;
class EnableIfAttr;
class EnumConstantDecl;
class Expr;
class ExtVectorType;
class FormatAttr;
class FriendDecl;
class FunctionDecl;
class FunctionProtoType;
class FunctionTemplateDecl;
class ImplicitConversionSequence;
typedef MutableArrayRef<ImplicitConversionSequence> ConversionSequenceList;
class InitListExpr;
class InitializationKind;
class InitializationSequence;
class InitializedEntity;
class IntegerLiteral;
class LabelStmt;
class LambdaExpr;
class LangOptions;
class LocalInstantiationScope;
class LookupResult;
class MacroInfo;
typedef ArrayRef<std::pair<IdentifierInfo *, SourceLocation>> ModuleIdPath;
class ModuleLoader;
class MultiLevelTemplateArgumentList;
class NamedDecl;
class ObjCImplementationDecl;
class ObjCInterfaceDecl;
class ObjCMethodDecl;
class ObjCProtocolDecl;
struct OverloadCandidate;
enum class OverloadCandidateParamOrder : char;
enum OverloadCandidateRewriteKind : unsigned;
class OverloadCandidateSet;
class OverloadExpr;
class ParenListExpr;
class ParmVarDecl;
class Preprocessor;
class PseudoDestructorTypeStorage;
class PseudoObjectExpr;
class QualType;
class SemaAMDGPU;
class SemaARM;
class SemaAVR;
class SemaBPF;
class SemaCodeCompletion;
class SemaCUDA;
class SemaHLSL;
class SemaHexagon;
class SemaLoongArch;
class SemaM68k;
class SemaMIPS;
class SemaMSP430;
class SemaNVPTX;
class SemaObjC;
class SemaOpenACC;
class SemaOpenCL;
class SemaOpenMP;
class SemaPPC;
class SemaPseudoObject;
class SemaRISCV;
class SemaSYCL;
class SemaSwift;
class SemaSystemZ;
class SemaWasm;
class SemaX86;
class StandardConversionSequence;
class Stmt;
class StringLiteral;
class SwitchStmt;
class TemplateArgument;
class TemplateArgumentList;
class TemplateArgumentLoc;
class TemplateDecl;
class TemplateInstantiationCallback;
class TemplateParameterList;
class TemplatePartialOrderingContext;
class TemplateTemplateParmDecl;
class Token;
class TypeAliasDecl;
class TypedefDecl;
class TypedefNameDecl;
class TypeLoc;
class TypoCorrectionConsumer;
class UnqualifiedId;
class UnresolvedLookupExpr;
class UnresolvedMemberExpr;
class UnresolvedSetImpl;
class UnresolvedSetIterator;
class UsingDecl;
class UsingShadowDecl;
class ValueDecl;
class VarDecl;
class VarTemplateSpecializationDecl;
class VisibilityAttr;
class VisibleDeclConsumer;
class IndirectFieldDecl;
struct DeductionFailureInfo;
class TemplateSpecCandidateSet;

namespace sema {
class AccessedEntity;
class BlockScopeInfo;
class Capture;
class CapturedRegionScopeInfo;
class CapturingScopeInfo;
class CompoundScopeInfo;
class DelayedDiagnostic;
class DelayedDiagnosticPool;
class FunctionScopeInfo;
class LambdaScopeInfo;
class PossiblyUnreachableDiag;
class RISCVIntrinsicManager;
class SemaPPCallbacks;
class TemplateDeductionInfo;
} // namespace sema

namespace threadSafety {
class BeforeSet;
void threadSafetyCleanup(BeforeSet *Cache);
} // namespace threadSafety

// FIXME: No way to easily map from TemplateTypeParmTypes to
// TemplateTypeParmDecls, so we have this horrible PointerUnion.
typedef std::pair<llvm::PointerUnion<const TemplateTypeParmType *, NamedDecl *>,
                  SourceLocation>
    UnexpandedParameterPack;

/// Describes whether we've seen any nullability information for the given
/// file.
struct FileNullability {
  /// The first pointer declarator (of any pointer kind) in the file that does
  /// not have a corresponding nullability annotation.
  SourceLocation PointerLoc;

  /// The end location for the first pointer declarator in the file. Used for
  /// placing fix-its.
  SourceLocation PointerEndLoc;

  /// Which kind of pointer declarator we saw.
  uint8_t PointerKind;

  /// Whether we saw any type nullability annotations in the given file.
  bool SawTypeNullability = false;
};

/// A mapping from file IDs to a record of whether we've seen nullability
/// information in that file.
class FileNullabilityMap {
  /// A mapping from file IDs to the nullability information for each file ID.
  llvm::DenseMap<FileID, FileNullability> Map;

  /// A single-element cache based on the file ID.
  struct {
    FileID File;
    FileNullability Nullability;
  } Cache;

public:
  FileNullability &operator[](FileID file) {
    // Check the single-element cache.
    if (file == Cache.File)
      return Cache.Nullability;

    // It's not in the single-element cache; flush the cache if we have one.
    if (!Cache.File.isInvalid()) {
      Map[Cache.File] = Cache.Nullability;
    }

    // Pull this entry into the cache.
    Cache.File = file;
    Cache.Nullability = Map[file];
    return Cache.Nullability;
  }
};

/// Tracks expected type during expression parsing, for use in code completion.
/// The type is tied to a particular token, all functions that update or consume
/// the type take a start location of the token they are looking at as a
/// parameter. This avoids updating the type on hot paths in the parser.
class PreferredTypeBuilder {
public:
  PreferredTypeBuilder(bool Enabled) : Enabled(Enabled) {}

  void enterCondition(Sema &S, SourceLocation Tok);
  void enterReturn(Sema &S, SourceLocation Tok);
  void enterVariableInit(SourceLocation Tok, Decl *D);
  /// Handles e.g. BaseType{ .D = Tok...
  void enterDesignatedInitializer(SourceLocation Tok, QualType BaseType,
                                  const Designation &D);
  /// Computing a type for the function argument may require running
  /// overloading, so we postpone its computation until it is actually needed.
  ///
  /// Clients should be very careful when using this function, as it stores a
  /// function_ref, clients should make sure all calls to get() with the same
  /// location happen while function_ref is alive.
  ///
  /// The callback should also emit signature help as a side-effect, but only
  /// if the completion point has been reached.
  void enterFunctionArgument(SourceLocation Tok,
                             llvm::function_ref<QualType()> ComputeType);

  void enterParenExpr(SourceLocation Tok, SourceLocation LParLoc);
  void enterUnary(Sema &S, SourceLocation Tok, tok::TokenKind OpKind,
                  SourceLocation OpLoc);
  void enterBinary(Sema &S, SourceLocation Tok, Expr *LHS, tok::TokenKind Op);
  void enterMemAccess(Sema &S, SourceLocation Tok, Expr *Base);
  void enterSubscript(Sema &S, SourceLocation Tok, Expr *LHS);
  /// Handles all type casts, including C-style cast, C++ casts, etc.
  void enterTypeCast(SourceLocation Tok, QualType CastType);

  /// Get the expected type associated with this location, if any.
  ///
  /// If the location is a function argument, determining the expected type
  /// involves considering all function overloads and the arguments so far.
  /// In this case, signature help for these function overloads will be reported
  /// as a side-effect (only if the completion point has been reached).
  QualType get(SourceLocation Tok) const {
    if (!Enabled || Tok != ExpectedLoc)
      return QualType();
    if (!Type.isNull())
      return Type;
    if (ComputeType)
      return ComputeType();
    return QualType();
  }

private:
  bool Enabled;
  /// Start position of a token for which we store expected type.
  SourceLocation ExpectedLoc;
  /// Expected type for a token starting at ExpectedLoc.
  QualType Type;
  /// A function to compute expected type at ExpectedLoc. It is only considered
  /// if Type is null.
  llvm::function_ref<QualType()> ComputeType;
};

struct SkipBodyInfo {
  SkipBodyInfo() = default;
  bool ShouldSkip = false;
  bool CheckSameAsPrevious = false;
  NamedDecl *Previous = nullptr;
  NamedDecl *New = nullptr;
};

/// Describes the result of template argument deduction.
///
/// The TemplateDeductionResult enumeration describes the result of
/// template argument deduction, as returned from
/// DeduceTemplateArguments(). The separate TemplateDeductionInfo
/// structure provides additional information about the results of
/// template argument deduction, e.g., the deduced template argument
/// list (if successful) or the specific template parameters or
/// deduced arguments that were involved in the failure.
enum class TemplateDeductionResult {
  /// Template argument deduction was successful.
  Success = 0,
  /// The declaration was invalid; do nothing.
  Invalid,
  /// Template argument deduction exceeded the maximum template
  /// instantiation depth (which has already been diagnosed).
  InstantiationDepth,
  /// Template argument deduction did not deduce a value
  /// for every template parameter.
  Incomplete,
  /// Template argument deduction did not deduce a value for every
  /// expansion of an expanded template parameter pack.
  IncompletePack,
  /// Template argument deduction produced inconsistent
  /// deduced values for the given template parameter.
  Inconsistent,
  /// Template argument deduction failed due to inconsistent
  /// cv-qualifiers on a template parameter type that would
  /// otherwise be deduced, e.g., we tried to deduce T in "const T"
  /// but were given a non-const "X".
  Underqualified,
  /// Substitution of the deduced template argument values
  /// resulted in an error.
  SubstitutionFailure,
  /// After substituting deduced template arguments, a dependent
  /// parameter type did not match the corresponding argument.
  DeducedMismatch,
  /// After substituting deduced template arguments, an element of
  /// a dependent parameter type did not match the corresponding element
  /// of the corresponding argument (when deducing from an initializer list).
  DeducedMismatchNested,
  /// A non-depnedent component of the parameter did not match the
  /// corresponding component of the argument.
  NonDeducedMismatch,
  /// When performing template argument deduction for a function
  /// template, there were too many call arguments.
  TooManyArguments,
  /// When performing template argument deduction for a function
  /// template, there were too few call arguments.
  TooFewArguments,
  /// The explicitly-specified template arguments were not valid
  /// template arguments for the given template.
  InvalidExplicitArguments,
  /// Checking non-dependent argument conversions failed.
  NonDependentConversionFailure,
  /// The deduced arguments did not satisfy the constraints associated
  /// with the template.
  ConstraintsNotSatisfied,
  /// Deduction failed; that's all we know.
  MiscellaneousDeductionFailure,
  /// CUDA Target attributes do not match.
  CUDATargetMismatch,
  /// Some error which was already diagnosed.
  AlreadyDiagnosed
};

/// Kinds of C++ special members.
enum class CXXSpecialMemberKind {
  DefaultConstructor,
  CopyConstructor,
  MoveConstructor,
  CopyAssignment,
  MoveAssignment,
  Destructor,
  Invalid
};

/// The kind of conversion being performed.
enum class CheckedConversionKind {
  /// An implicit conversion.
  Implicit,
  /// A C-style cast.
  CStyleCast,
  /// A functional-style cast.
  FunctionalCast,
  /// A cast other than a C-style cast.
  OtherCast,
  /// A conversion for an operand of a builtin overloaded operator.
  ForBuiltinOverloadedOp
};

enum class TagUseKind {
  Reference,   // Reference to a tag:  'struct foo *X;'
  Declaration, // Fwd decl of a tag:   'struct foo;'
  Definition,  // Definition of a tag: 'struct foo { int X; } Y;'
  Friend       // Friend declaration:  'friend struct foo;'
};

/// Used with attributes/effects with a boolean condition, e.g. `nonblocking`.
enum class FunctionEffectMode : uint8_t {
  None,     // effect is not present.
  False,    // effect(false).
  True,     // effect(true).
  Dependent // effect(expr) where expr is dependent.
};

struct FunctionEffectDiff {
  enum class Kind { Added, Removed, ConditionMismatch };

  FunctionEffect::Kind EffectKind;
  Kind DiffKind;
  FunctionEffectWithCondition Old; // invalid when Added.
  FunctionEffectWithCondition New; // invalid when Removed.

  StringRef effectName() const {
    if (Old.Effect.kind() != FunctionEffect::Kind::None)
      return Old.Effect.name();
    return New.Effect.name();
  }

  /// Describes the result of effects differing between a base class's virtual
  /// method and an overriding method in a subclass.
  enum class OverrideResult {
    NoAction,
    Warn,
    Merge // Merge missing effect from base to derived.
  };

  /// Return true if adding or removing the effect as part of a type conversion
  /// should generate a diagnostic.
  bool shouldDiagnoseConversion(QualType SrcType,
                                const FunctionEffectsRef &SrcFX,
                                QualType DstType,
                                const FunctionEffectsRef &DstFX) const;

  /// Return true if adding or removing the effect in a redeclaration should
  /// generate a diagnostic.
  bool shouldDiagnoseRedeclaration(const FunctionDecl &OldFunction,
                                   const FunctionEffectsRef &OldFX,
                                   const FunctionDecl &NewFunction,
                                   const FunctionEffectsRef &NewFX) const;

  /// Return true if adding or removing the effect in a C++ virtual method
  /// override should generate a diagnostic.
  OverrideResult shouldDiagnoseMethodOverride(
      const CXXMethodDecl &OldMethod, const FunctionEffectsRef &OldFX,
      const CXXMethodDecl &NewMethod, const FunctionEffectsRef &NewFX) const;
};

struct FunctionEffectDifferences : public SmallVector<FunctionEffectDiff> {
  /// Caller should short-circuit by checking for equality first.
  FunctionEffectDifferences(const FunctionEffectsRef &Old,
                            const FunctionEffectsRef &New);
};

/// Sema - This implements semantic analysis and AST building for C.
/// \nosubgrouping
class Sema final : public SemaBase {
  // Table of Contents
  // -----------------
  // 1. Semantic Analysis (Sema.cpp)
  // 2. C++ Access Control (SemaAccess.cpp)
  // 3. Attributes (SemaAttr.cpp)
  // 4. Availability Attribute Handling (SemaAvailability.cpp)
  // 5. Casts (SemaCast.cpp)
  // 6. Extra Semantic Checking (SemaChecking.cpp)
  // 7. C++ Coroutines (SemaCoroutine.cpp)
  // 8. C++ Scope Specifiers (SemaCXXScopeSpec.cpp)
  // 9. Declarations (SemaDecl.cpp)
  // 10. Declaration Attribute Handling (SemaDeclAttr.cpp)
  // 11. C++ Declarations (SemaDeclCXX.cpp)
  // 12. C++ Exception Specifications (SemaExceptionSpec.cpp)
  // 13. Expressions (SemaExpr.cpp)
  // 14. C++ Expressions (SemaExprCXX.cpp)
  // 15. Member Access Expressions (SemaExprMember.cpp)
  // 16. Initializers (SemaInit.cpp)
  // 17. C++ Lambda Expressions (SemaLambda.cpp)
  // 18. Name Lookup (SemaLookup.cpp)
  // 19. Modules (SemaModule.cpp)
  // 20. C++ Overloading (SemaOverload.cpp)
  // 21. Statements (SemaStmt.cpp)
  // 22. `inline asm` Statement (SemaStmtAsm.cpp)
  // 23. Statement Attribute Handling (SemaStmtAttr.cpp)
  // 24. C++ Templates (SemaTemplate.cpp)
  // 25. C++ Template Argument Deduction (SemaTemplateDeduction.cpp)
  // 26. C++ Template Deduction Guide (SemaTemplateDeductionGuide.cpp)
  // 27. C++ Template Instantiation (SemaTemplateInstantiate.cpp)
  // 28. C++ Template Declaration Instantiation
  //     (SemaTemplateInstantiateDecl.cpp)
  // 29. C++ Variadic Templates (SemaTemplateVariadic.cpp)
  // 30. Constraints and Concepts (SemaConcept.cpp)
  // 31. Types (SemaType.cpp)
  // 32. FixIt Helpers (SemaFixItUtils.cpp)

  /// \name Semantic Analysis
  /// Implementations are in Sema.cpp
  ///@{

public:
  Sema(Preprocessor &pp, ASTContext &ctxt, ASTConsumer &consumer,
       TranslationUnitKind TUKind = TU_Complete,
       CodeCompleteConsumer *CompletionConsumer = nullptr);
  ~Sema();

  /// Perform initialization that occurs after the parser has been
  /// initialized but before it parses anything.
  void Initialize();

  /// This virtual key function only exists to limit the emission of debug info
  /// describing the Sema class. GCC and Clang only emit debug info for a class
  /// with a vtable when the vtable is emitted. Sema is final and not
  /// polymorphic, but the debug info size savings are so significant that it is
  /// worth adding a vtable just to take advantage of this optimization.
  virtual void anchor();

  const LangOptions &getLangOpts() const { return LangOpts; }
  OpenCLOptions &getOpenCLOptions() { return OpenCLFeatures; }
  FPOptions &getCurFPFeatures() { return CurFPFeatures; }

  DiagnosticsEngine &getDiagnostics() const { return Diags; }
  SourceManager &getSourceManager() const { return SourceMgr; }
  Preprocessor &getPreprocessor() const { return PP; }
  ASTContext &getASTContext() const { return Context; }
  ASTConsumer &getASTConsumer() const { return Consumer; }
  ASTMutationListener *getASTMutationListener() const;
  ExternalSemaSource *getExternalSource() const { return ExternalSource.get(); }

  DarwinSDKInfo *getDarwinSDKInfoForAvailabilityChecking(SourceLocation Loc,
                                                         StringRef Platform);
  DarwinSDKInfo *getDarwinSDKInfoForAvailabilityChecking();

  /// Registers an external source. If an external source already exists,
  ///  creates a multiplex external source and appends to it.
  ///
  ///\param[in] E - A non-null external sema source.
  ///
  void addExternalSource(ExternalSemaSource *E);

  /// Print out statistics about the semantic analysis.
  void PrintStats() const;

  /// Warn that the stack is nearly exhausted.
  void warnStackExhausted(SourceLocation Loc);

  /// Run some code with "sufficient" stack space. (Currently, at least 256K is
  /// guaranteed). Produces a warning if we're low on stack space and allocates
  /// more in that case. Use this in code that may recurse deeply (for example,
  /// in template instantiation) to avoid stack overflow.
  void runWithSufficientStackSpace(SourceLocation Loc,
                                   llvm::function_ref<void()> Fn);

  /// Returns default addr space for method qualifiers.
  LangAS getDefaultCXXMethodAddrSpace() const;

  /// Load weak undeclared identifiers from the external source.
  void LoadExternalWeakUndeclaredIdentifiers();

  /// Determine if VD, which must be a variable or function, is an external
  /// symbol that nonetheless can't be referenced from outside this translation
  /// unit because its type has no linkage and it's not extern "C".
  bool isExternalWithNoLinkageType(const ValueDecl *VD) const;

  /// Obtain a sorted list of functions that are undefined but ODR-used.
  void getUndefinedButUsed(
      SmallVectorImpl<std::pair<NamedDecl *, SourceLocation>> &Undefined);

  typedef std::pair<SourceLocation, bool> DeleteExprLoc;
  typedef llvm::SmallVector<DeleteExprLoc, 4> DeleteLocs;
  /// Retrieves list of suspicious delete-expressions that will be checked at
  /// the end of translation unit.
  const llvm::MapVector<FieldDecl *, DeleteLocs> &
  getMismatchingDeleteExpressions() const;

  /// Cause the active diagnostic on the DiagosticsEngine to be
  /// emitted. This is closely coupled to the SemaDiagnosticBuilder class and
  /// should not be used elsewhere.
  void EmitCurrentDiagnostic(unsigned DiagID);

  void addImplicitTypedef(StringRef Name, QualType T);

  /// Whether uncompilable error has occurred. This includes error happens
  /// in deferred diagnostics.
  bool hasUncompilableErrorOccurred() const;

  /// Looks through the macro-expansion chain for the given
  /// location, looking for a macro expansion with the given name.
  /// If one is found, returns true and sets the location to that
  /// expansion loc.
  bool findMacroSpelling(SourceLocation &loc, StringRef name);

  /// Calls \c Lexer::getLocForEndOfToken()
  SourceLocation getLocForEndOfToken(SourceLocation Loc, unsigned Offset = 0);

  /// Retrieve the module loader associated with the preprocessor.
  ModuleLoader &getModuleLoader() const;

  /// Invent a new identifier for parameters of abbreviated templates.
  IdentifierInfo *
  InventAbbreviatedTemplateParameterTypeName(const IdentifierInfo *ParamName,
                                             unsigned Index);

  void emitAndClearUnusedLocalTypedefWarnings();

  // Emit all deferred diagnostics.
  void emitDeferredDiags();

  enum TUFragmentKind {
    /// The global module fragment, between 'module;' and a module-declaration.
    Global,
    /// A normal translation unit fragment. For a non-module unit, this is the
    /// entire translation unit. Otherwise, it runs from the module-declaration
    /// to the private-module-fragment (if any) or the end of the TU (if not).
    Normal,
    /// The private module fragment, between 'module :private;' and the end of
    /// the translation unit.
    Private
  };

  /// This is called before the very first declaration in the translation unit
  /// is parsed. Note that the ASTContext may have already injected some
  /// declarations.
  void ActOnStartOfTranslationUnit();
  /// ActOnEndOfTranslationUnit - This is called at the very end of the
  /// translation unit when EOF is reached and all but the top-level scope is
  /// popped.
  void ActOnEndOfTranslationUnit();
  void ActOnEndOfTranslationUnitFragment(TUFragmentKind Kind);

  /// Determines the active Scope associated with the given declaration
  /// context.
  ///
  /// This routine maps a declaration context to the active Scope object that
  /// represents that declaration context in the parser. It is typically used
  /// from "scope-less" code (e.g., template instantiation, lazy creation of
  /// declarations) that injects a name for name-lookup purposes and, therefore,
  /// must update the Scope.
  ///
  /// \returns The scope corresponding to the given declaraion context, or NULL
  /// if no such scope is open.
  Scope *getScopeForContext(DeclContext *Ctx);

  void PushFunctionScope();
  void PushBlockScope(Scope *BlockScope, BlockDecl *Block);
  sema::LambdaScopeInfo *PushLambdaScope();

  /// This is used to inform Sema what the current TemplateParameterDepth
  /// is during Parsing.  Currently it is used to pass on the depth
  /// when parsing generic lambda 'auto' parameters.
  void RecordParsingTemplateParameterDepth(unsigned Depth);

  void PushCapturedRegionScope(Scope *RegionScope, CapturedDecl *CD,
                               RecordDecl *RD, CapturedRegionKind K,
                               unsigned OpenMPCaptureLevel = 0);

  /// Custom deleter to allow FunctionScopeInfos to be kept alive for a short
  /// time after they've been popped.
  class PoppedFunctionScopeDeleter {
    Sema *Self;

  public:
    explicit PoppedFunctionScopeDeleter(Sema *Self) : Self(Self) {}
    void operator()(sema::FunctionScopeInfo *Scope) const;
  };

  using PoppedFunctionScopePtr =
      std::unique_ptr<sema::FunctionScopeInfo, PoppedFunctionScopeDeleter>;

  /// Pop a function (or block or lambda or captured region) scope from the
  /// stack.
  ///
  /// \param WP The warning policy to use for CFG-based warnings, or null if
  ///        such warnings should not be produced.
  /// \param D The declaration corresponding to this function scope, if
  ///        producing CFG-based warnings.
  /// \param BlockType The type of the block expression, if D is a BlockDecl.
  PoppedFunctionScopePtr
  PopFunctionScopeInfo(const sema::AnalysisBasedWarnings::Policy *WP = nullptr,
                       const Decl *D = nullptr,
                       QualType BlockType = QualType());

  sema::FunctionScopeInfo *getEnclosingFunction() const;

  void setFunctionHasBranchIntoScope();
  void setFunctionHasBranchProtectedScope();
  void setFunctionHasIndirectGoto();
  void setFunctionHasMustTail();

  void PushCompoundScope(bool IsStmtExpr);
  void PopCompoundScope();

  /// Determine whether any errors occurred within this function/method/
  /// block.
  bool hasAnyUnrecoverableErrorsInThisFunction() const;

  /// Retrieve the current block, if any.
  sema::BlockScopeInfo *getCurBlock();

  /// Get the innermost lambda enclosing the current location, if any. This
  /// looks through intervening non-lambda scopes such as local functions and
  /// blocks.
  sema::LambdaScopeInfo *getEnclosingLambda() const;

  /// Retrieve the current lambda scope info, if any.
  /// \param IgnoreNonLambdaCapturingScope true if should find the top-most
  /// lambda scope info ignoring all inner capturing scopes that are not
  /// lambda scopes.
  sema::LambdaScopeInfo *
  getCurLambda(bool IgnoreNonLambdaCapturingScope = false);

  /// Retrieve the current generic lambda info, if any.
  sema::LambdaScopeInfo *getCurGenericLambda();

  /// Retrieve the current captured region, if any.
  sema::CapturedRegionScopeInfo *getCurCapturedRegion();

  void ActOnComment(SourceRange Comment);

  /// Retrieve the parser's current scope.
  ///
  /// This routine must only be used when it is certain that semantic analysis
  /// and the parser are in precisely the same context, which is not the case
  /// when, e.g., we are performing any kind of template instantiation.
  /// Therefore, the only safe places to use this scope are in the parser
  /// itself and in routines directly invoked from the parser and *never* from
  /// template substitution or instantiation.
  Scope *getCurScope() const { return CurScope; }

  IdentifierInfo *getSuperIdentifier() const;

  DeclContext *getCurLexicalContext() const {
    return OriginalLexicalContext ? OriginalLexicalContext : CurContext;
  }

  SemaDiagnosticBuilder targetDiag(SourceLocation Loc, unsigned DiagID,
                                   const FunctionDecl *FD = nullptr);
  SemaDiagnosticBuilder targetDiag(SourceLocation Loc,
                                   const PartialDiagnostic &PD,
                                   const FunctionDecl *FD = nullptr) {
    return targetDiag(Loc, PD.getDiagID(), FD) << PD;
  }

  /// Check if the type is allowed to be used for the current target.
  void checkTypeSupport(QualType Ty, SourceLocation Loc,
                        ValueDecl *D = nullptr);

  // /// The kind of conversion being performed.
  // enum CheckedConversionKind {
  //   /// An implicit conversion.
  //   CCK_ImplicitConversion,
  //   /// A C-style cast.
  //   CCK_CStyleCast,
  //   /// A functional-style cast.
  //   CCK_FunctionalCast,
  //   /// A cast other than a C-style cast.
  //   CCK_OtherCast,
  //   /// A conversion for an operand of a builtin overloaded operator.
  //   CCK_ForBuiltinOverloadedOp
  // };

  /// ImpCastExprToType - If Expr is not of type 'Type', insert an implicit
  /// cast.  If there is already an implicit cast, merge into the existing one.
  /// If isLvalue, the result of the cast is an lvalue.
  ExprResult ImpCastExprToType(
      Expr *E, QualType Type, CastKind CK, ExprValueKind VK = VK_PRValue,
      const CXXCastPath *BasePath = nullptr,
      CheckedConversionKind CCK = CheckedConversionKind::Implicit);

  /// ScalarTypeToBooleanCastKind - Returns the cast kind corresponding
  /// to the conversion from scalar type ScalarTy to the Boolean type.
  static CastKind ScalarTypeToBooleanCastKind(QualType ScalarTy);

  /// If \p AllowLambda is true, treat lambda as function.
  DeclContext *getFunctionLevelDeclContext(bool AllowLambda = false) const;

  /// Returns a pointer to the innermost enclosing function, or nullptr if the
  /// current context is not inside a function. If \p AllowLambda is true,
  /// this can return the call operator of an enclosing lambda, otherwise
  /// lambdas are skipped when looking for an enclosing function.
  FunctionDecl *getCurFunctionDecl(bool AllowLambda = false) const;

  /// getCurMethodDecl - If inside of a method body, this returns a pointer to
  /// the method decl for the method being parsed.  If we're currently
  /// in a 'block', this returns the containing context.
  ObjCMethodDecl *getCurMethodDecl();

  /// getCurFunctionOrMethodDecl - Return the Decl for the current ObjC method
  /// or C function we're in, otherwise return null.  If we're currently
  /// in a 'block', this returns the containing context.
  NamedDecl *getCurFunctionOrMethodDecl() const;

  /// Warn if we're implicitly casting from a _Nullable pointer type to a
  /// _Nonnull one.
  void diagnoseNullableToNonnullConversion(QualType DstType, QualType SrcType,
                                           SourceLocation Loc);

  /// Warn when implicitly casting 0 to nullptr.
  void diagnoseZeroToNullptrConversion(CastKind Kind, const Expr *E);

  // ----- function effects ---

  /// Warn when implicitly changing function effects.
  void diagnoseFunctionEffectConversion(QualType DstType, QualType SrcType,
                                        SourceLocation Loc);

  /// Warn and return true if adding an effect to a set would create a conflict.
  bool diagnoseConflictingFunctionEffect(const FunctionEffectsRef &FX,
                                         const FunctionEffectWithCondition &EC,
                                         SourceLocation NewAttrLoc);

  // Report a failure to merge function effects between declarations due to a
  // conflict.
  void
  diagnoseFunctionEffectMergeConflicts(const FunctionEffectSet::Conflicts &Errs,
                                       SourceLocation NewLoc,
                                       SourceLocation OldLoc);

  /// Try to parse the conditional expression attached to an effect attribute
  /// (e.g. 'nonblocking'). (c.f. Sema::ActOnNoexceptSpec). Return an empty
  /// optional on error.
  std::optional<FunctionEffectMode>
  ActOnEffectExpression(Expr *CondExpr, StringRef AttributeName);

  /// makeUnavailableInSystemHeader - There is an error in the current
  /// context.  If we're still in a system header, and we can plausibly
  /// make the relevant declaration unavailable instead of erroring, do
  /// so and return true.
  bool makeUnavailableInSystemHeader(SourceLocation loc,
                                     UnavailableAttr::ImplicitReason reason);

  /// Retrieve a suitable printing policy for diagnostics.
  PrintingPolicy getPrintingPolicy() const {
    return getPrintingPolicy(Context, PP);
  }

  /// Retrieve a suitable printing policy for diagnostics.
  static PrintingPolicy getPrintingPolicy(const ASTContext &Ctx,
                                          const Preprocessor &PP);

  /// Scope actions.
  void ActOnTranslationUnitScope(Scope *S);

  /// Determine whether \param D is function like (function or function
  /// template) for parsing.
  bool isDeclaratorFunctionLike(Declarator &D);

  /// The maximum alignment, same as in llvm::Value. We duplicate them here
  /// because that allows us not to duplicate the constants in clang code,
  /// which we must to since we can't directly use the llvm constants.
  /// The value is verified against llvm here: lib/CodeGen/CGDecl.cpp
  ///
  /// This is the greatest alignment value supported by load, store, and alloca
  /// instructions, and global values.
  static const unsigned MaxAlignmentExponent = 32;
  static const uint64_t MaximumAlignment = 1ull << MaxAlignmentExponent;

  /// Flag indicating whether or not to collect detailed statistics.
  bool CollectStats;

  std::unique_ptr<sema::FunctionScopeInfo> CachedFunctionScope;

  /// Stack containing information about each of the nested
  /// function, block, and method scopes that are currently active.
  SmallVector<sema::FunctionScopeInfo *, 4> FunctionScopes;

  /// The index of the first FunctionScope that corresponds to the current
  /// context.
  unsigned FunctionScopesStart = 0;

  /// Track the number of currently active capturing scopes.
  unsigned CapturingFunctionScopes = 0;

  llvm::BumpPtrAllocator BumpAlloc;

  /// The kind of translation unit we are processing.
  ///
  /// When we're processing a complete translation unit, Sema will perform
  /// end-of-translation-unit semantic tasks (such as creating
  /// initializers for tentative definitions in C) once parsing has
  /// completed. Modules and precompiled headers perform different kinds of
  /// checks.
  const TranslationUnitKind TUKind;

  /// Translation Unit Scope - useful to Objective-C actions that need
  /// to lookup file scope declarations in the "ordinary" C decl namespace.
  /// For example, user-defined classes, built-in "id" type, etc.
  Scope *TUScope;

  bool WarnedStackExhausted = false;

  void incrementMSManglingNumber() const {
    return CurScope->incrementMSManglingNumber();
  }

  /// Try to recover by turning the given expression into a
  /// call.  Returns true if recovery was attempted or an error was
  /// emitted; this may also leave the ExprResult invalid.
  bool tryToRecoverWithCall(ExprResult &E, const PartialDiagnostic &PD,
                            bool ForceComplain = false,
                            bool (*IsPlausibleResult)(QualType) = nullptr);

  /// Figure out if an expression could be turned into a call.
  ///
  /// Use this when trying to recover from an error where the programmer may
  /// have written just the name of a function instead of actually calling it.
  ///
  /// \param E - The expression to examine.
  /// \param ZeroArgCallReturnTy - If the expression can be turned into a call
  ///  with no arguments, this parameter is set to the type returned by such a
  ///  call; otherwise, it is set to an empty QualType.
  /// \param OverloadSet - If the expression is an overloaded function
  ///  name, this parameter is populated with the decls of the various
  ///  overloads.
  bool tryExprAsCall(Expr &E, QualType &ZeroArgCallReturnTy,
                     UnresolvedSetImpl &NonTemplateOverloads);

  typedef OpaquePtr<DeclGroupRef> DeclGroupPtrTy;
  typedef OpaquePtr<TemplateName> TemplateTy;
  typedef OpaquePtr<QualType> TypeTy;

  OpenCLOptions OpenCLFeatures;
  FPOptions CurFPFeatures;

  const LangOptions &LangOpts;
  Preprocessor &PP;
  ASTContext &Context;
  ASTConsumer &Consumer;
  DiagnosticsEngine &Diags;
  SourceManager &SourceMgr;
  api_notes::APINotesManager APINotes;

  /// A RAII object to enter scope of a compound statement.
  class CompoundScopeRAII {
  public:
    CompoundScopeRAII(Sema &S, bool IsStmtExpr = false) : S(S) {
      S.ActOnStartOfCompoundStmt(IsStmtExpr);
    }

    ~CompoundScopeRAII() { S.ActOnFinishOfCompoundStmt(); }

  private:
    Sema &S;
  };

  /// An RAII helper that pops function a function scope on exit.
  struct FunctionScopeRAII {
    Sema &S;
    bool Active;
    FunctionScopeRAII(Sema &S) : S(S), Active(true) {}
    ~FunctionScopeRAII() {
      if (Active)
        S.PopFunctionScopeInfo();
    }
    void disable() { Active = false; }
  };

  sema::FunctionScopeInfo *getCurFunction() const {
    return FunctionScopes.empty() ? nullptr : FunctionScopes.back();
  }

  /// Worker object for performing CFG-based warnings.
  sema::AnalysisBasedWarnings AnalysisWarnings;
  threadSafety::BeforeSet *ThreadSafetyDeclCache;

  /// Callback to the parser to parse templated functions when needed.
  typedef void LateTemplateParserCB(void *P, LateParsedTemplate &LPT);
  typedef void LateTemplateParserCleanupCB(void *P);
  LateTemplateParserCB *LateTemplateParser;
  LateTemplateParserCleanupCB *LateTemplateParserCleanup;
  void *OpaqueParser;

  void SetLateTemplateParser(LateTemplateParserCB *LTP,
                             LateTemplateParserCleanupCB *LTPCleanup, void *P) {
    LateTemplateParser = LTP;
    LateTemplateParserCleanup = LTPCleanup;
    OpaqueParser = P;
  }

  /// Callback to the parser to parse a type expressed as a string.
  std::function<TypeResult(StringRef, StringRef, SourceLocation)>
      ParseTypeFromStringCallback;

  /// VAListTagName - The declaration name corresponding to __va_list_tag.
  /// This is used as part of a hack to omit that class from ADL results.
  DeclarationName VAListTagName;

  /// Is the last error level diagnostic immediate. This is used to determined
  /// whether the next info diagnostic should be immediate.
  bool IsLastErrorImmediate = true;

  class DelayedDiagnostics;

  class DelayedDiagnosticsState {
    sema::DelayedDiagnosticPool *SavedPool = nullptr;
    friend class Sema::DelayedDiagnostics;
  };
  typedef DelayedDiagnosticsState ParsingDeclState;
  typedef DelayedDiagnosticsState ProcessingContextState;

  /// A class which encapsulates the logic for delaying diagnostics
  /// during parsing and other processing.
  class DelayedDiagnostics {
    /// The current pool of diagnostics into which delayed
    /// diagnostics should go.
    sema::DelayedDiagnosticPool *CurPool = nullptr;

  public:
    DelayedDiagnostics() = default;

    /// Adds a delayed diagnostic.
    void add(const sema::DelayedDiagnostic &diag); // in DelayedDiagnostic.h

    /// Determines whether diagnostics should be delayed.
    bool shouldDelayDiagnostics() { return CurPool != nullptr; }

    /// Returns the current delayed-diagnostics pool.
    sema::DelayedDiagnosticPool *getCurrentPool() const { return CurPool; }

    /// Enter a new scope.  Access and deprecation diagnostics will be
    /// collected in this pool.
    DelayedDiagnosticsState push(sema::DelayedDiagnosticPool &pool) {
      DelayedDiagnosticsState state;
      state.SavedPool = CurPool;
      CurPool = &pool;
      return state;
    }

    /// Leave a delayed-diagnostic state that was previously pushed.
    /// Do not emit any of the diagnostics.  This is performed as part
    /// of the bookkeeping of popping a pool "properly".
    void popWithoutEmitting(DelayedDiagnosticsState state) {
      CurPool = state.SavedPool;
    }

    /// Enter a new scope where access and deprecation diagnostics are
    /// not delayed.
    DelayedDiagnosticsState pushUndelayed() {
      DelayedDiagnosticsState state;
      state.SavedPool = CurPool;
      CurPool = nullptr;
      return state;
    }

    /// Undo a previous pushUndelayed().
    void popUndelayed(DelayedDiagnosticsState state) {
      assert(CurPool == nullptr);
      CurPool = state.SavedPool;
    }
  } DelayedDiagnostics;

  ParsingDeclState PushParsingDeclaration(sema::DelayedDiagnosticPool &pool) {
    return DelayedDiagnostics.push(pool);
  }

  /// Diagnostics that are emitted only if we discover that the given function
  /// must be codegen'ed.  Because handling these correctly adds overhead to
  /// compilation, this is currently only enabled for CUDA compilations.
  SemaDiagnosticBuilder::DeferredDiagnosticsType DeviceDeferredDiags;

  /// CurContext - This is the current declaration context of parsing.
  DeclContext *CurContext;

  SemaAMDGPU &AMDGPU() {
    assert(AMDGPUPtr);
    return *AMDGPUPtr;
  }

  SemaARM &ARM() {
    assert(ARMPtr);
    return *ARMPtr;
  }

  SemaAVR &AVR() {
    assert(AVRPtr);
    return *AVRPtr;
  }

  SemaBPF &BPF() {
    assert(BPFPtr);
    return *BPFPtr;
  }

  SemaCodeCompletion &CodeCompletion() {
    assert(CodeCompletionPtr);
    return *CodeCompletionPtr;
  }

  SemaCUDA &CUDA() {
    assert(CUDAPtr);
    return *CUDAPtr;
  }

  SemaHLSL &HLSL() {
    assert(HLSLPtr);
    return *HLSLPtr;
  }

  SemaHexagon &Hexagon() {
    assert(HexagonPtr);
    return *HexagonPtr;
  }

  SemaLoongArch &LoongArch() {
    assert(LoongArchPtr);
    return *LoongArchPtr;
  }

  SemaM68k &M68k() {
    assert(M68kPtr);
    return *M68kPtr;
  }

  SemaMIPS &MIPS() {
    assert(MIPSPtr);
    return *MIPSPtr;
  }

  SemaMSP430 &MSP430() {
    assert(MSP430Ptr);
    return *MSP430Ptr;
  }

  SemaNVPTX &NVPTX() {
    assert(NVPTXPtr);
    return *NVPTXPtr;
  }

  SemaObjC &ObjC() {
    assert(ObjCPtr);
    return *ObjCPtr;
  }

  SemaOpenACC &OpenACC() {
    assert(OpenACCPtr);
    return *OpenACCPtr;
  }

  SemaOpenCL &OpenCL() {
    assert(OpenCLPtr);
    return *OpenCLPtr;
  }

  SemaOpenMP &OpenMP() {
    assert(OpenMPPtr && "SemaOpenMP is dead");
    return *OpenMPPtr;
  }

  SemaPPC &PPC() {
    assert(PPCPtr);
    return *PPCPtr;
  }

  SemaPseudoObject &PseudoObject() {
    assert(PseudoObjectPtr);
    return *PseudoObjectPtr;
  }

  SemaRISCV &RISCV() {
    assert(RISCVPtr);
    return *RISCVPtr;
  }

  SemaSYCL &SYCL() {
    assert(SYCLPtr);
    return *SYCLPtr;
  }

  SemaSwift &Swift() {
    assert(SwiftPtr);
    return *SwiftPtr;
  }

  SemaSystemZ &SystemZ() {
    assert(SystemZPtr);
    return *SystemZPtr;
  }

  SemaWasm &Wasm() {
    assert(WasmPtr);
    return *WasmPtr;
  }

  SemaX86 &X86() {
    assert(X86Ptr);
    return *X86Ptr;
  }

  /// Source of additional semantic information.
  IntrusiveRefCntPtr<ExternalSemaSource> ExternalSource;

protected:
  friend class Parser;
  friend class InitializationSequence;
  friend class ASTReader;
  friend class ASTDeclReader;
  friend class ASTWriter;

private:
  std::optional<std::unique_ptr<DarwinSDKInfo>> CachedDarwinSDKInfo;
  bool WarnedDarwinSDKInfoMissing = false;

  Sema(const Sema &) = delete;
  void operator=(const Sema &) = delete;

  /// The handler for the FileChanged preprocessor events.
  ///
  /// Used for diagnostics that implement custom semantic analysis for #include
  /// directives, like -Wpragma-pack.
  sema::SemaPPCallbacks *SemaPPCallbackHandler;

  /// The parser's current scope.
  ///
  /// The parser maintains this state here.
  Scope *CurScope;

  mutable IdentifierInfo *Ident_super;

  std::unique_ptr<SemaAMDGPU> AMDGPUPtr;
  std::unique_ptr<SemaARM> ARMPtr;
  std::unique_ptr<SemaAVR> AVRPtr;
  std::unique_ptr<SemaBPF> BPFPtr;
  std::unique_ptr<SemaCodeCompletion> CodeCompletionPtr;
  std::unique_ptr<SemaCUDA> CUDAPtr;
  std::unique_ptr<SemaHLSL> HLSLPtr;
  std::unique_ptr<SemaHexagon> HexagonPtr;
  std::unique_ptr<SemaLoongArch> LoongArchPtr;
  std::unique_ptr<SemaM68k> M68kPtr;
  std::unique_ptr<SemaMIPS> MIPSPtr;
  std::unique_ptr<SemaMSP430> MSP430Ptr;
  std::unique_ptr<SemaNVPTX> NVPTXPtr;
  std::unique_ptr<SemaObjC> ObjCPtr;
  std::unique_ptr<SemaOpenACC> OpenACCPtr;
  std::unique_ptr<SemaOpenCL> OpenCLPtr;
  std::unique_ptr<SemaOpenMP> OpenMPPtr;
  std::unique_ptr<SemaPPC> PPCPtr;
  std::unique_ptr<SemaPseudoObject> PseudoObjectPtr;
  std::unique_ptr<SemaRISCV> RISCVPtr;
  std::unique_ptr<SemaSYCL> SYCLPtr;
  std::unique_ptr<SemaSwift> SwiftPtr;
  std::unique_ptr<SemaSystemZ> SystemZPtr;
  std::unique_ptr<SemaWasm> WasmPtr;
  std::unique_ptr<SemaX86> X86Ptr;

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Access Control
  /// Implementations are in SemaAccess.cpp
  ///@{

public:
  enum AccessResult {
    AR_accessible,
    AR_inaccessible,
    AR_dependent,
    AR_delayed
  };

  /// SetMemberAccessSpecifier - Set the access specifier of a member.
  /// Returns true on error (when the previous member decl access specifier
  /// is different from the new member decl access specifier).
  bool SetMemberAccessSpecifier(NamedDecl *MemberDecl,
                                NamedDecl *PrevMemberDecl,
                                AccessSpecifier LexicalAS);

  /// Perform access-control checking on a previously-unresolved member
  /// access which has now been resolved to a member.
  AccessResult CheckUnresolvedMemberAccess(UnresolvedMemberExpr *E,
                                           DeclAccessPair FoundDecl);
  AccessResult CheckUnresolvedLookupAccess(UnresolvedLookupExpr *E,
                                           DeclAccessPair FoundDecl);

  /// Checks access to an overloaded operator new or delete.
  AccessResult CheckAllocationAccess(SourceLocation OperatorLoc,
                                     SourceRange PlacementRange,
                                     CXXRecordDecl *NamingClass,
                                     DeclAccessPair FoundDecl,
                                     bool Diagnose = true);

  /// Checks access to a constructor.
  AccessResult CheckConstructorAccess(SourceLocation Loc, CXXConstructorDecl *D,
                                      DeclAccessPair FoundDecl,
                                      const InitializedEntity &Entity,
                                      bool IsCopyBindingRefToTemp = false);

  /// Checks access to a constructor.
  AccessResult CheckConstructorAccess(SourceLocation Loc, CXXConstructorDecl *D,
                                      DeclAccessPair FoundDecl,
                                      const InitializedEntity &Entity,
                                      const PartialDiagnostic &PDiag);
  AccessResult CheckDestructorAccess(SourceLocation Loc,
                                     CXXDestructorDecl *Dtor,
                                     const PartialDiagnostic &PDiag,
                                     QualType objectType = QualType());

  /// Checks access to the target of a friend declaration.
  AccessResult CheckFriendAccess(NamedDecl *D);

  /// Checks access to a member.
  AccessResult CheckMemberAccess(SourceLocation UseLoc,
                                 CXXRecordDecl *NamingClass,
                                 DeclAccessPair Found);

  /// Checks implicit access to a member in a structured binding.
  AccessResult
  CheckStructuredBindingMemberAccess(SourceLocation UseLoc,
                                     CXXRecordDecl *DecomposedClass,
                                     DeclAccessPair Field);
  AccessResult CheckMemberOperatorAccess(SourceLocation Loc, Expr *ObjectExpr,
                                         const SourceRange &,
                                         DeclAccessPair FoundDecl);

  /// Checks access to an overloaded member operator, including
  /// conversion operators.
  AccessResult CheckMemberOperatorAccess(SourceLocation Loc, Expr *ObjectExpr,
                                         Expr *ArgExpr,
                                         DeclAccessPair FoundDecl);
  AccessResult CheckMemberOperatorAccess(SourceLocation Loc, Expr *ObjectExpr,
                                         ArrayRef<Expr *> ArgExprs,
                                         DeclAccessPair FoundDecl);
  AccessResult CheckAddressOfMemberAccess(Expr *OvlExpr,
                                          DeclAccessPair FoundDecl);

  /// Checks access for a hierarchy conversion.
  ///
  /// \param ForceCheck true if this check should be performed even if access
  ///     control is disabled;  some things rely on this for semantics
  /// \param ForceUnprivileged true if this check should proceed as if the
  ///     context had no special privileges
  AccessResult CheckBaseClassAccess(SourceLocation AccessLoc, QualType Base,
                                    QualType Derived, const CXXBasePath &Path,
                                    unsigned DiagID, bool ForceCheck = false,
                                    bool ForceUnprivileged = false);

  /// Checks access to all the declarations in the given result set.
  void CheckLookupAccess(const LookupResult &R);

  /// Checks access to Target from the given class. The check will take access
  /// specifiers into account, but no member access expressions and such.
  ///
  /// \param Target the declaration to check if it can be accessed
  /// \param NamingClass the class in which the lookup was started.
  /// \param BaseType type of the left side of member access expression.
  ///        \p BaseType and \p NamingClass are used for C++ access control.
  ///        Depending on the lookup case, they should be set to the following:
  ///        - lhs.target (member access without a qualifier):
  ///          \p BaseType and \p NamingClass are both the type of 'lhs'.
  ///        - lhs.X::target (member access with a qualifier):
  ///          BaseType is the type of 'lhs', NamingClass is 'X'
  ///        - X::target (qualified lookup without member access):
  ///          BaseType is null, NamingClass is 'X'.
  ///        - target (unqualified lookup).
  ///          BaseType is null, NamingClass is the parent class of 'target'.
  /// \return true if the Target is accessible from the Class, false otherwise.
  bool IsSimplyAccessible(NamedDecl *Decl, CXXRecordDecl *NamingClass,
                          QualType BaseType);

  /// Is the given member accessible for the purposes of deciding whether to
  /// define a special member function as deleted?
  bool isMemberAccessibleForDeletion(CXXRecordDecl *NamingClass,
                                     DeclAccessPair Found, QualType ObjectType,
                                     SourceLocation Loc,
                                     const PartialDiagnostic &Diag);
  bool isMemberAccessibleForDeletion(CXXRecordDecl *NamingClass,
                                     DeclAccessPair Found,
                                     QualType ObjectType) {
    return isMemberAccessibleForDeletion(NamingClass, Found, ObjectType,
                                         SourceLocation(), PDiag());
  }

  void HandleDependentAccessCheck(
      const DependentDiagnostic &DD,
      const MultiLevelTemplateArgumentList &TemplateArgs);
  void HandleDelayedAccessCheck(sema::DelayedDiagnostic &DD, Decl *Ctx);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Attributes
  /// Implementations are in SemaAttr.cpp
  ///@{

public:
  /// Controls member pointer representation format under the MS ABI.
  LangOptions::PragmaMSPointersToMembersKind
      MSPointerToMemberRepresentationMethod;

  bool MSStructPragmaOn; // True when \#pragma ms_struct on

  /// Source location for newly created implicit MSInheritanceAttrs
  SourceLocation ImplicitMSInheritanceAttrLoc;

  /// pragma clang section kind
  enum PragmaClangSectionKind {
    PCSK_Invalid = 0,
    PCSK_BSS = 1,
    PCSK_Data = 2,
    PCSK_Rodata = 3,
    PCSK_Text = 4,
    PCSK_Relro = 5
  };

  enum PragmaClangSectionAction { PCSA_Set = 0, PCSA_Clear = 1 };

  struct PragmaClangSection {
    std::string SectionName;
    bool Valid = false;
    SourceLocation PragmaLocation;
  };

  PragmaClangSection PragmaClangBSSSection;
  PragmaClangSection PragmaClangDataSection;
  PragmaClangSection PragmaClangRodataSection;
  PragmaClangSection PragmaClangRelroSection;
  PragmaClangSection PragmaClangTextSection;

  enum PragmaMsStackAction {
    PSK_Reset = 0x0,                   // #pragma ()
    PSK_Set = 0x1,                     // #pragma (value)
    PSK_Push = 0x2,                    // #pragma (push[, id])
    PSK_Pop = 0x4,                     // #pragma (pop[, id])
    PSK_Show = 0x8,                    // #pragma (show) -- only for "pack"!
    PSK_Push_Set = PSK_Push | PSK_Set, // #pragma (push[, id], value)
    PSK_Pop_Set = PSK_Pop | PSK_Set,   // #pragma (pop[, id], value)
  };

  struct PragmaPackInfo {
    PragmaMsStackAction Action;
    StringRef SlotLabel;
    Token Alignment;
  };

  // #pragma pack and align.
  class AlignPackInfo {
  public:
    // `Native` represents default align mode, which may vary based on the
    // platform.
    enum Mode : unsigned char { Native, Natural, Packed, Mac68k };

    // #pragma pack info constructor
    AlignPackInfo(AlignPackInfo::Mode M, unsigned Num, bool IsXL)
        : PackAttr(true), AlignMode(M), PackNumber(Num), XLStack(IsXL) {
      assert(Num == PackNumber && "The pack number has been truncated.");
    }

    // #pragma align info constructor
    AlignPackInfo(AlignPackInfo::Mode M, bool IsXL)
        : PackAttr(false), AlignMode(M),
          PackNumber(M == Packed ? 1 : UninitPackVal), XLStack(IsXL) {}

    explicit AlignPackInfo(bool IsXL) : AlignPackInfo(Native, IsXL) {}

    AlignPackInfo() : AlignPackInfo(Native, false) {}

    // When a AlignPackInfo itself cannot be used, this returns an 32-bit
    // integer encoding for it. This should only be passed to
    // AlignPackInfo::getFromRawEncoding, it should not be inspected directly.
    static uint32_t getRawEncoding(const AlignPackInfo &Info) {
      std::uint32_t Encoding{};
      if (Info.IsXLStack())
        Encoding |= IsXLMask;

      Encoding |= static_cast<uint32_t>(Info.getAlignMode()) << 1;

      if (Info.IsPackAttr())
        Encoding |= PackAttrMask;

      Encoding |= static_cast<uint32_t>(Info.getPackNumber()) << 4;

      return Encoding;
    }

    static AlignPackInfo getFromRawEncoding(unsigned Encoding) {
      bool IsXL = static_cast<bool>(Encoding & IsXLMask);
      AlignPackInfo::Mode M =
          static_cast<AlignPackInfo::Mode>((Encoding & AlignModeMask) >> 1);
      int PackNumber = (Encoding & PackNumMask) >> 4;

      if (Encoding & PackAttrMask)
        return AlignPackInfo(M, PackNumber, IsXL);

      return AlignPackInfo(M, IsXL);
    }

    bool IsPackAttr() const { return PackAttr; }

    bool IsAlignAttr() const { return !PackAttr; }

    Mode getAlignMode() const { return AlignMode; }

    unsigned getPackNumber() const { return PackNumber; }

    bool IsPackSet() const {
      // #pragma align, #pragma pack(), and #pragma pack(0) do not set the pack
      // attriute on a decl.
      return PackNumber != UninitPackVal && PackNumber != 0;
    }

    bool IsXLStack() const { return XLStack; }

    bool operator==(const AlignPackInfo &Info) const {
      return std::tie(AlignMode, PackNumber, PackAttr, XLStack) ==
             std::tie(Info.AlignMode, Info.PackNumber, Info.PackAttr,
                      Info.XLStack);
    }

    bool operator!=(const AlignPackInfo &Info) const {
      return !(*this == Info);
    }

  private:
    /// \brief True if this is a pragma pack attribute,
    ///         not a pragma align attribute.
    bool PackAttr;

    /// \brief The alignment mode that is in effect.
    Mode AlignMode;

    /// \brief The pack number of the stack.
    unsigned char PackNumber;

    /// \brief True if it is a XL #pragma align/pack stack.
    bool XLStack;

    /// \brief Uninitialized pack value.
    static constexpr unsigned char UninitPackVal = -1;

    // Masks to encode and decode an AlignPackInfo.
    static constexpr uint32_t IsXLMask{0x0000'0001};
    static constexpr uint32_t AlignModeMask{0x0000'0006};
    static constexpr uint32_t PackAttrMask{0x00000'0008};
    static constexpr uint32_t PackNumMask{0x0000'01F0};
  };

  template <typename ValueType> struct PragmaStack {
    struct Slot {
      llvm::StringRef StackSlotLabel;
      ValueType Value;
      SourceLocation PragmaLocation;
      SourceLocation PragmaPushLocation;
      Slot(llvm::StringRef StackSlotLabel, ValueType Value,
           SourceLocation PragmaLocation, SourceLocation PragmaPushLocation)
          : StackSlotLabel(StackSlotLabel), Value(Value),
            PragmaLocation(PragmaLocation),
            PragmaPushLocation(PragmaPushLocation) {}
    };

    void Act(SourceLocation PragmaLocation, PragmaMsStackAction Action,
             llvm::StringRef StackSlotLabel, ValueType Value) {
      if (Action == PSK_Reset) {
        CurrentValue = DefaultValue;
        CurrentPragmaLocation = PragmaLocation;
        return;
      }
      if (Action & PSK_Push)
        Stack.emplace_back(StackSlotLabel, CurrentValue, CurrentPragmaLocation,
                           PragmaLocation);
      else if (Action & PSK_Pop) {
        if (!StackSlotLabel.empty()) {
          // If we've got a label, try to find it and jump there.
          auto I = llvm::find_if(llvm::reverse(Stack), [&](const Slot &x) {
            return x.StackSlotLabel == StackSlotLabel;
          });
          // If we found the label so pop from there.
          if (I != Stack.rend()) {
            CurrentValue = I->Value;
            CurrentPragmaLocation = I->PragmaLocation;
            Stack.erase(std::prev(I.base()), Stack.end());
          }
        } else if (!Stack.empty()) {
          // We do not have a label, just pop the last entry.
          CurrentValue = Stack.back().Value;
          CurrentPragmaLocation = Stack.back().PragmaLocation;
          Stack.pop_back();
        }
      }
      if (Action & PSK_Set) {
        CurrentValue = Value;
        CurrentPragmaLocation = PragmaLocation;
      }
    }

    // MSVC seems to add artificial slots to #pragma stacks on entering a C++
    // method body to restore the stacks on exit, so it works like this:
    //
    //   struct S {
    //     #pragma <name>(push, InternalPragmaSlot, <current_pragma_value>)
    //     void Method {}
    //     #pragma <name>(pop, InternalPragmaSlot)
    //   };
    //
    // It works even with #pragma vtordisp, although MSVC doesn't support
    //   #pragma vtordisp(push [, id], n)
    // syntax.
    //
    // Push / pop a named sentinel slot.
    void SentinelAction(PragmaMsStackAction Action, StringRef Label) {
      assert((Action == PSK_Push || Action == PSK_Pop) &&
             "Can only push / pop #pragma stack sentinels!");
      Act(CurrentPragmaLocation, Action, Label, CurrentValue);
    }

    // Constructors.
    explicit PragmaStack(const ValueType &Default)
        : DefaultValue(Default), CurrentValue(Default) {}

    bool hasValue() const { return CurrentValue != DefaultValue; }

    SmallVector<Slot, 2> Stack;
    ValueType DefaultValue; // Value used for PSK_Reset action.
    ValueType CurrentValue;
    SourceLocation CurrentPragmaLocation;
  };
  // FIXME: We should serialize / deserialize these if they occur in a PCH (but
  // we shouldn't do so if they're in a module).

  /// Whether to insert vtordisps prior to virtual bases in the Microsoft
  /// C++ ABI.  Possible values are 0, 1, and 2, which mean:
  ///
  /// 0: Suppress all vtordisps
  /// 1: Insert vtordisps in the presence of vbase overrides and non-trivial
  ///    structors
  /// 2: Always insert vtordisps to support RTTI on partially constructed
  ///    objects
  PragmaStack<MSVtorDispMode> VtorDispStack;
  PragmaStack<AlignPackInfo> AlignPackStack;
  // The current #pragma align/pack values and locations at each #include.
  struct AlignPackIncludeState {
    AlignPackInfo CurrentValue;
    SourceLocation CurrentPragmaLocation;
    bool HasNonDefaultValue, ShouldWarnOnInclude;
  };
  SmallVector<AlignPackIncludeState, 8> AlignPackIncludeStack;
  // Segment #pragmas.
  PragmaStack<StringLiteral *> DataSegStack;
  PragmaStack<StringLiteral *> BSSSegStack;
  PragmaStack<StringLiteral *> ConstSegStack;
  PragmaStack<StringLiteral *> CodeSegStack;

  // #pragma strict_gs_check.
  PragmaStack<bool> StrictGuardStackCheckStack;

  // This stack tracks the current state of Sema.CurFPFeatures.
  PragmaStack<FPOptionsOverride> FpPragmaStack;
  FPOptionsOverride CurFPFeatureOverrides() {
    FPOptionsOverride result;
    if (!FpPragmaStack.hasValue()) {
      result = FPOptionsOverride();
    } else {
      result = FpPragmaStack.CurrentValue;
    }
    return result;
  }

  enum PragmaSectionKind {
    PSK_DataSeg,
    PSK_BSSSeg,
    PSK_ConstSeg,
    PSK_CodeSeg,
  };

  // RAII object to push / pop sentinel slots for all MS #pragma stacks.
  // Actions should be performed only if we enter / exit a C++ method body.
  class PragmaStackSentinelRAII {
  public:
    PragmaStackSentinelRAII(Sema &S, StringRef SlotLabel, bool ShouldAct);
    ~PragmaStackSentinelRAII();

  private:
    Sema &S;
    StringRef SlotLabel;
    bool ShouldAct;
  };

  /// Last section used with #pragma init_seg.
  StringLiteral *CurInitSeg;
  SourceLocation CurInitSegLoc;

  /// Sections used with #pragma alloc_text.
  llvm::StringMap<std::tuple<StringRef, SourceLocation>> FunctionToSectionMap;

  /// VisContext - Manages the stack for \#pragma GCC visibility.
  void *VisContext; // Really a "PragmaVisStack*"

  /// This an attribute introduced by \#pragma clang attribute.
  struct PragmaAttributeEntry {
    SourceLocation Loc;
    ParsedAttr *Attribute;
    SmallVector<attr::SubjectMatchRule, 4> MatchRules;
    bool IsUsed;
  };

  /// A push'd group of PragmaAttributeEntries.
  struct PragmaAttributeGroup {
    /// The location of the push attribute.
    SourceLocation Loc;
    /// The namespace of this push group.
    const IdentifierInfo *Namespace;
    SmallVector<PragmaAttributeEntry, 2> Entries;
  };

  SmallVector<PragmaAttributeGroup, 2> PragmaAttributeStack;

  /// The declaration that is currently receiving an attribute from the
  /// #pragma attribute stack.
  const Decl *PragmaAttributeCurrentTargetDecl;

  /// This represents the last location of a "#pragma clang optimize off"
  /// directive if such a directive has not been closed by an "on" yet. If
  /// optimizations are currently "on", this is set to an invalid location.
  SourceLocation OptimizeOffPragmaLocation;

  /// Get the location for the currently active "\#pragma clang optimize
  /// off". If this location is invalid, then the state of the pragma is "on".
  SourceLocation getOptimizeOffPragmaLocation() const {
    return OptimizeOffPragmaLocation;
  }

  /// The "on" or "off" argument passed by \#pragma optimize, that denotes
  /// whether the optimizations in the list passed to the pragma should be
  /// turned off or on. This boolean is true by default because command line
  /// options are honored when `#pragma optimize("", on)`.
  /// (i.e. `ModifyFnAttributeMSPragmaOptimze()` does nothing)
  bool MSPragmaOptimizeIsOn = true;

  /// Set of no-builtin functions listed by \#pragma function.
  llvm::SmallSetVector<StringRef, 4> MSFunctionNoBuiltins;

  /// AddAlignmentAttributesForRecord - Adds any needed alignment attributes to
  /// a the record decl, to handle '\#pragma pack' and '\#pragma options align'.
  void AddAlignmentAttributesForRecord(RecordDecl *RD);

  /// AddMsStructLayoutForRecord - Adds ms_struct layout attribute to record.
  void AddMsStructLayoutForRecord(RecordDecl *RD);

  /// Add gsl::Pointer attribute to std::container::iterator
  /// \param ND The declaration that introduces the name
  /// std::container::iterator. \param UnderlyingRecord The record named by ND.
  void inferGslPointerAttribute(NamedDecl *ND, CXXRecordDecl *UnderlyingRecord);

  /// Add [[gsl::Owner]] and [[gsl::Pointer]] attributes for std:: types.
  void inferGslOwnerPointerAttribute(CXXRecordDecl *Record);

  /// Add [[gsl::Pointer]] attributes for std:: types.
  void inferGslPointerAttribute(TypedefNameDecl *TD);

  /// Add _Nullable attributes for std:: types.
  void inferNullableClassAttribute(CXXRecordDecl *CRD);

  enum PragmaOptionsAlignKind {
    POAK_Native,  // #pragma options align=native
    POAK_Natural, // #pragma options align=natural
    POAK_Packed,  // #pragma options align=packed
    POAK_Power,   // #pragma options align=power
    POAK_Mac68k,  // #pragma options align=mac68k
    POAK_Reset    // #pragma options align=reset
  };

  /// ActOnPragmaClangSection - Called on well formed \#pragma clang section
  void ActOnPragmaClangSection(SourceLocation PragmaLoc,
                               PragmaClangSectionAction Action,
                               PragmaClangSectionKind SecKind,
                               StringRef SecName);

  /// ActOnPragmaOptionsAlign - Called on well formed \#pragma options align.
  void ActOnPragmaOptionsAlign(PragmaOptionsAlignKind Kind,
                               SourceLocation PragmaLoc);

  /// ActOnPragmaPack - Called on well formed \#pragma pack(...).
  void ActOnPragmaPack(SourceLocation PragmaLoc, PragmaMsStackAction Action,
                       StringRef SlotLabel, Expr *Alignment);

  /// ConstantFoldAttrArgs - Folds attribute arguments into ConstantExprs
  /// (unless they are value dependent or type dependent). Returns false
  /// and emits a diagnostic if one or more of the arguments could not be
  /// folded into a constant.
  bool ConstantFoldAttrArgs(const AttributeCommonInfo &CI,
                            MutableArrayRef<Expr *> Args);

  enum class PragmaAlignPackDiagnoseKind {
    NonDefaultStateAtInclude,
    ChangedStateAtExit
  };

  void DiagnoseNonDefaultPragmaAlignPack(PragmaAlignPackDiagnoseKind Kind,
                                         SourceLocation IncludeLoc);
  void DiagnoseUnterminatedPragmaAlignPack();

  /// ActOnPragmaMSStruct - Called on well formed \#pragma ms_struct [on|off].
  void ActOnPragmaMSStruct(PragmaMSStructKind Kind);

  /// ActOnPragmaMSComment - Called on well formed
  /// \#pragma comment(kind, "arg").
  void ActOnPragmaMSComment(SourceLocation CommentLoc, PragmaMSCommentKind Kind,
                            StringRef Arg);

  /// ActOnPragmaDetectMismatch - Call on well-formed \#pragma detect_mismatch
  void ActOnPragmaDetectMismatch(SourceLocation Loc, StringRef Name,
                                 StringRef Value);

  /// Are precise floating point semantics currently enabled?
  bool isPreciseFPEnabled() {
    return !CurFPFeatures.getAllowFPReassociate() &&
           !CurFPFeatures.getNoSignedZero() &&
           !CurFPFeatures.getAllowReciprocal() &&
           !CurFPFeatures.getAllowApproxFunc();
  }

  void ActOnPragmaFPEvalMethod(SourceLocation Loc,
                               LangOptions::FPEvalMethodKind Value);

  /// ActOnPragmaFloatControl - Call on well-formed \#pragma float_control
  void ActOnPragmaFloatControl(SourceLocation Loc, PragmaMsStackAction Action,
                               PragmaFloatControlKind Value);

  /// ActOnPragmaMSPointersToMembers - called on well formed \#pragma
  /// pointers_to_members(representation method[, general purpose
  /// representation]).
  void ActOnPragmaMSPointersToMembers(
      LangOptions::PragmaMSPointersToMembersKind Kind,
      SourceLocation PragmaLoc);

  /// Called on well formed \#pragma vtordisp().
  void ActOnPragmaMSVtorDisp(PragmaMsStackAction Action,
                             SourceLocation PragmaLoc, MSVtorDispMode Value);

  bool UnifySection(StringRef SectionName, int SectionFlags,
                    NamedDecl *TheDecl);
  bool UnifySection(StringRef SectionName, int SectionFlags,
                    SourceLocation PragmaSectionLocation);

  /// Called on well formed \#pragma bss_seg/data_seg/const_seg/code_seg.
  void ActOnPragmaMSSeg(SourceLocation PragmaLocation,
                        PragmaMsStackAction Action,
                        llvm::StringRef StackSlotLabel,
                        StringLiteral *SegmentName, llvm::StringRef PragmaName);

  /// Called on well formed \#pragma section().
  void ActOnPragmaMSSection(SourceLocation PragmaLocation, int SectionFlags,
                            StringLiteral *SegmentName);

  /// Called on well-formed \#pragma init_seg().
  void ActOnPragmaMSInitSeg(SourceLocation PragmaLocation,
                            StringLiteral *SegmentName);

  /// Called on well-formed \#pragma alloc_text().
  void ActOnPragmaMSAllocText(
      SourceLocation PragmaLocation, StringRef Section,
      const SmallVector<std::tuple<IdentifierInfo *, SourceLocation>>
          &Functions);

  /// ActOnPragmaMSStrictGuardStackCheck - Called on well formed \#pragma
  /// strict_gs_check.
  void ActOnPragmaMSStrictGuardStackCheck(SourceLocation PragmaLocation,
                                          PragmaMsStackAction Action,
                                          bool Value);

  /// ActOnPragmaUnused - Called on well-formed '\#pragma unused'.
  void ActOnPragmaUnused(const Token &Identifier, Scope *curScope,
                         SourceLocation PragmaLoc);

  void ActOnPragmaAttributeAttribute(ParsedAttr &Attribute,
                                     SourceLocation PragmaLoc,
                                     attr::ParsedSubjectMatchRuleSet Rules);
  void ActOnPragmaAttributeEmptyPush(SourceLocation PragmaLoc,
                                     const IdentifierInfo *Namespace);

  /// Called on well-formed '\#pragma clang attribute pop'.
  void ActOnPragmaAttributePop(SourceLocation PragmaLoc,
                               const IdentifierInfo *Namespace);

  /// Adds the attributes that have been specified using the
  /// '\#pragma clang attribute push' directives to the given declaration.
  void AddPragmaAttributes(Scope *S, Decl *D);

  void PrintPragmaAttributeInstantiationPoint();

  void DiagnoseUnterminatedPragmaAttribute();

  /// Called on well formed \#pragma clang optimize.
  void ActOnPragmaOptimize(bool On, SourceLocation PragmaLoc);

  /// #pragma optimize("[optimization-list]", on | off).
  void ActOnPragmaMSOptimize(SourceLocation Loc, bool IsOn);

  /// Call on well formed \#pragma function.
  void
  ActOnPragmaMSFunction(SourceLocation Loc,
                        const llvm::SmallVectorImpl<StringRef> &NoBuiltins);

  /// Only called on function definitions; if there is a pragma in scope
  /// with the effect of a range-based optnone, consider marking the function
  /// with attribute optnone.
  void AddRangeBasedOptnone(FunctionDecl *FD);

  /// Only called on function definitions; if there is a `#pragma alloc_text`
  /// that decides which code section the function should be in, add
  /// attribute section to the function.
  void AddSectionMSAllocText(FunctionDecl *FD);

  /// Adds the 'optnone' attribute to the function declaration if there
  /// are no conflicts; Loc represents the location causing the 'optnone'
  /// attribute to be added (usually because of a pragma).
  void AddOptnoneAttributeIfNoConflicts(FunctionDecl *FD, SourceLocation Loc);

  /// Only called on function definitions; if there is a MSVC #pragma optimize
  /// in scope, consider changing the function's attributes based on the
  /// optimization list passed to the pragma.
  void ModifyFnAttributesMSPragmaOptimize(FunctionDecl *FD);

  /// Only called on function definitions; if there is a pragma in scope
  /// with the effect of a range-based no_builtin, consider marking the function
  /// with attribute no_builtin.
  void AddImplicitMSFunctionNoBuiltinAttr(FunctionDecl *FD);

  /// AddPushedVisibilityAttribute - If '\#pragma GCC visibility' was used,
  /// add an appropriate visibility attribute.
  void AddPushedVisibilityAttribute(Decl *RD);

  /// FreeVisContext - Deallocate and null out VisContext.
  void FreeVisContext();

  /// ActOnPragmaVisibility - Called on well formed \#pragma GCC visibility... .
  void ActOnPragmaVisibility(const IdentifierInfo *VisType,
                             SourceLocation PragmaLoc);

  /// ActOnPragmaFPContract - Called on well formed
  /// \#pragma {STDC,OPENCL} FP_CONTRACT and
  /// \#pragma clang fp contract
  void ActOnPragmaFPContract(SourceLocation Loc, LangOptions::FPModeKind FPC);

  /// Called on well formed
  /// \#pragma clang fp reassociate
  /// or
  /// \#pragma clang fp reciprocal
  void ActOnPragmaFPValueChangingOption(SourceLocation Loc, PragmaFPKind Kind,
                                        bool IsEnabled);

  /// ActOnPragmaFenvAccess - Called on well formed
  /// \#pragma STDC FENV_ACCESS
  void ActOnPragmaFEnvAccess(SourceLocation Loc, bool IsEnabled);

  /// ActOnPragmaCXLimitedRange - Called on well formed
  /// \#pragma STDC CX_LIMITED_RANGE
  void ActOnPragmaCXLimitedRange(SourceLocation Loc,
                                 LangOptions::ComplexRangeKind Range);

  /// Called on well formed '\#pragma clang fp' that has option 'exceptions'.
  void ActOnPragmaFPExceptions(SourceLocation Loc,
                               LangOptions::FPExceptionModeKind);

  /// Called to set constant rounding mode for floating point operations.
  void ActOnPragmaFEnvRound(SourceLocation Loc, llvm::RoundingMode);

  /// Called to set exception behavior for floating point operations.
  void setExceptionMode(SourceLocation Loc, LangOptions::FPExceptionModeKind);

  /// PushNamespaceVisibilityAttr - Note that we've entered a
  /// namespace with a visibility attribute.
  void PushNamespaceVisibilityAttr(const VisibilityAttr *Attr,
                                   SourceLocation Loc);

  /// PopPragmaVisibility - Pop the top element of the visibility stack; used
  /// for '\#pragma GCC visibility' and visibility attributes on namespaces.
  void PopPragmaVisibility(bool IsNamespaceEnd, SourceLocation EndLoc);

  /// Handles semantic checking for features that are common to all attributes,
  /// such as checking whether a parameter was properly specified, or the
  /// correct number of arguments were passed, etc. Returns true if the
  /// attribute has been diagnosed.
  bool checkCommonAttributeFeatures(const Decl *D, const ParsedAttr &A,
                                    bool SkipArgCountCheck = false);
  bool checkCommonAttributeFeatures(const Stmt *S, const ParsedAttr &A,
                                    bool SkipArgCountCheck = false);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Availability Attribute Handling
  /// Implementations are in SemaAvailability.cpp
  ///@{

public:
  /// Issue any -Wunguarded-availability warnings in \c FD
  void DiagnoseUnguardedAvailabilityViolations(Decl *FD);

  void handleDelayedAvailabilityCheck(sema::DelayedDiagnostic &DD, Decl *Ctx);

  /// Retrieve the current function, if any, that should be analyzed for
  /// potential availability violations.
  sema::FunctionScopeInfo *getCurFunctionAvailabilityContext();

  void DiagnoseAvailabilityOfDecl(NamedDecl *D, ArrayRef<SourceLocation> Locs,
                                  const ObjCInterfaceDecl *UnknownObjCClass,
                                  bool ObjCPropertyAccess,
                                  bool AvoidPartialAvailabilityChecks = false,
                                  ObjCInterfaceDecl *ClassReceiver = nullptr);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Casts
  /// Implementations are in SemaCast.cpp
  ///@{

public:
  static bool isCast(CheckedConversionKind CCK) {
    return CCK == CheckedConversionKind::CStyleCast ||
           CCK == CheckedConversionKind::FunctionalCast ||
           CCK == CheckedConversionKind::OtherCast;
  }

  /// ActOnCXXNamedCast - Parse
  /// {dynamic,static,reinterpret,const,addrspace}_cast's.
  ExprResult ActOnCXXNamedCast(SourceLocation OpLoc, tok::TokenKind Kind,
                               SourceLocation LAngleBracketLoc, Declarator &D,
                               SourceLocation RAngleBracketLoc,
                               SourceLocation LParenLoc, Expr *E,
                               SourceLocation RParenLoc);

  ExprResult BuildCXXNamedCast(SourceLocation OpLoc, tok::TokenKind Kind,
                               TypeSourceInfo *Ty, Expr *E,
                               SourceRange AngleBrackets, SourceRange Parens);

  ExprResult ActOnBuiltinBitCastExpr(SourceLocation KWLoc, Declarator &Dcl,
                                     ExprResult Operand,
                                     SourceLocation RParenLoc);

  ExprResult BuildBuiltinBitCastExpr(SourceLocation KWLoc, TypeSourceInfo *TSI,
                                     Expr *Operand, SourceLocation RParenLoc);

  // Checks that reinterpret casts don't have undefined behavior.
  void CheckCompatibleReinterpretCast(QualType SrcType, QualType DestType,
                                      bool IsDereference, SourceRange Range);

  // Checks that the vector type should be initialized from a scalar
  // by splatting the value rather than populating a single element.
  // This is the case for AltiVecVector types as well as with
  // AltiVecPixel and AltiVecBool when -faltivec-src-compat=xl is specified.
  bool ShouldSplatAltivecScalarInCast(const VectorType *VecTy);

  // Checks if the -faltivec-src-compat=gcc option is specified.
  // If so, AltiVecVector, AltiVecBool and AltiVecPixel types are
  // treated the same way as they are when trying to initialize
  // these vectors on gcc (an error is emitted).
  bool CheckAltivecInitFromScalar(SourceRange R, QualType VecTy,
                                  QualType SrcTy);

  ExprResult BuildCStyleCastExpr(SourceLocation LParenLoc, TypeSourceInfo *Ty,
                                 SourceLocation RParenLoc, Expr *Op);

  ExprResult BuildCXXFunctionalCastExpr(TypeSourceInfo *TInfo, QualType Type,
                                        SourceLocation LParenLoc,
                                        Expr *CastExpr,
                                        SourceLocation RParenLoc);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Extra Semantic Checking
  /// Implementations are in SemaChecking.cpp
  ///@{

public:
  /// Used to change context to isConstantEvaluated without pushing a heavy
  /// ExpressionEvaluationContextRecord object.
  bool isConstantEvaluatedOverride = false;

  bool isConstantEvaluatedContext() const {
    return currentEvaluationContext().isConstantEvaluated() ||
           isConstantEvaluatedOverride;
  }

  SourceLocation getLocationOfStringLiteralByte(const StringLiteral *SL,
                                                unsigned ByteNo) const;

  enum FormatArgumentPassingKind {
    FAPK_Fixed,    // values to format are fixed (no C-style variadic arguments)
    FAPK_Variadic, // values to format are passed as variadic arguments
    FAPK_VAList,   // values to format are passed in a va_list
  };

  // Used to grab the relevant information from a FormatAttr and a
  // FunctionDeclaration.
  struct FormatStringInfo {
    unsigned FormatIdx;
    unsigned FirstDataArg;
    FormatArgumentPassingKind ArgPassingKind;
  };

  /// Given a FunctionDecl's FormatAttr, attempts to populate the
  /// FomatStringInfo parameter with the FormatAttr's correct format_idx and
  /// firstDataArg. Returns true when the format fits the function and the
  /// FormatStringInfo has been populated.
  static bool getFormatStringInfo(const FormatAttr *Format, bool IsCXXMember,
                                  bool IsVariadic, FormatStringInfo *FSI);

  // Used by C++ template instantiation.
  ExprResult BuiltinShuffleVector(CallExpr *TheCall);

  /// ConvertVectorExpr - Handle __builtin_convertvector
  ExprResult ConvertVectorExpr(Expr *E, TypeSourceInfo *TInfo,
                               SourceLocation BuiltinLoc,
                               SourceLocation RParenLoc);

  enum FormatStringType {
    FST_Scanf,
    FST_Printf,
    FST_NSString,
    FST_Strftime,
    FST_Strfmon,
    FST_Kprintf,
    FST_FreeBSDKPrintf,
    FST_OSTrace,
    FST_OSLog,
    FST_Syslog,
    FST_Unknown
  };
  static FormatStringType GetFormatStringType(const FormatAttr *Format);

  bool FormatStringHasSArg(const StringLiteral *FExpr);

  /// Check for comparisons of floating-point values using == and !=. Issue a
  /// warning if the comparison is not likely to do what the programmer
  /// intended.
  void CheckFloatComparison(SourceLocation Loc, Expr *LHS, Expr *RHS,
                            BinaryOperatorKind Opcode);

  /// Register a magic integral constant to be used as a type tag.
  void RegisterTypeTagForDatatype(const IdentifierInfo *ArgumentKind,
                                  uint64_t MagicValue, QualType Type,
                                  bool LayoutCompatible, bool MustBeNull);

  struct TypeTagData {
    TypeTagData() {}

    TypeTagData(QualType Type, bool LayoutCompatible, bool MustBeNull)
        : Type(Type), LayoutCompatible(LayoutCompatible),
          MustBeNull(MustBeNull) {}

    QualType Type;

    /// If true, \c Type should be compared with other expression's types for
    /// layout-compatibility.
    LLVM_PREFERRED_TYPE(bool)
    unsigned LayoutCompatible : 1;
    LLVM_PREFERRED_TYPE(bool)
    unsigned MustBeNull : 1;
  };

  /// A pair of ArgumentKind identifier and magic value.  This uniquely
  /// identifies the magic value.
  typedef std::pair<const IdentifierInfo *, uint64_t> TypeTagMagicValue;

  /// Diagnoses the current set of gathered accesses. This typically
  /// happens at full expression level. The set is cleared after emitting the
  /// diagnostics.
  void DiagnoseMisalignedMembers();

  /// This function checks if the expression is in the sef of potentially
  /// misaligned members and it is converted to some pointer type T with lower
  /// or equal alignment requirements. If so it removes it. This is used when
  /// we do not want to diagnose such misaligned access (e.g. in conversions to
  /// void*).
  void DiscardMisalignedMemberAddress(const Type *T, Expr *E);

  /// This function calls Action when it determines that E designates a
  /// misaligned member due to the packed attribute. This is used to emit
  /// local diagnostics like in reference binding.
  void RefersToMemberWithReducedAlignment(
      Expr *E,
      llvm::function_ref<void(Expr *, RecordDecl *, FieldDecl *, CharUnits)>
          Action);

  enum class AtomicArgumentOrder { API, AST };
  ExprResult
  BuildAtomicExpr(SourceRange CallRange, SourceRange ExprRange,
                  SourceLocation RParenLoc, MultiExprArg Args,
                  AtomicExpr::AtomicOp Op,
                  AtomicArgumentOrder ArgOrder = AtomicArgumentOrder::API);

  /// Check to see if a given expression could have '.c_str()' called on it.
  bool hasCStrMethod(const Expr *E);

  /// Diagnose pointers that are always non-null.
  /// \param E the expression containing the pointer
  /// \param NullKind NPCK_NotNull if E is a cast to bool, otherwise, E is
  /// compared to a null pointer
  /// \param IsEqual True when the comparison is equal to a null pointer
  /// \param Range Extra SourceRange to highlight in the diagnostic
  void DiagnoseAlwaysNonNullPointer(Expr *E,
                                    Expr::NullPointerConstantKind NullType,
                                    bool IsEqual, SourceRange Range);

  /// CheckParmsForFunctionDef - Check that the parameters of the given
  /// function are appropriate for the definition of a function. This
  /// takes care of any checks that cannot be performed on the
  /// declaration itself, e.g., that the types of each of the function
  /// parameters are complete.
  bool CheckParmsForFunctionDef(ArrayRef<ParmVarDecl *> Parameters,
                                bool CheckParameterNames);

  /// CheckCastAlign - Implements -Wcast-align, which warns when a
  /// pointer cast increases the alignment requirements.
  void CheckCastAlign(Expr *Op, QualType T, SourceRange TRange);

  /// checkUnsafeAssigns - Check whether +1 expr is being assigned
  /// to weak/__unsafe_unretained type.
  bool checkUnsafeAssigns(SourceLocation Loc, QualType LHS, Expr *RHS);

  /// checkUnsafeExprAssigns - Check whether +1 expr is being assigned
  /// to weak/__unsafe_unretained expression.
  void checkUnsafeExprAssigns(SourceLocation Loc, Expr *LHS, Expr *RHS);

  /// Emit \p DiagID if statement located on \p StmtLoc has a suspicious null
  /// statement as a \p Body, and it is located on the same line.
  ///
  /// This helps prevent bugs due to typos, such as:
  ///     if (condition);
  ///       do_stuff();
  void DiagnoseEmptyStmtBody(SourceLocation StmtLoc, const Stmt *Body,
                             unsigned DiagID);

  /// Warn if a for/while loop statement \p S, which is followed by
  /// \p PossibleBody, has a suspicious null statement as a body.
  void DiagnoseEmptyLoopBody(const Stmt *S, const Stmt *PossibleBody);

  /// DiagnoseSelfMove - Emits a warning if a value is moved to itself.
  void DiagnoseSelfMove(const Expr *LHSExpr, const Expr *RHSExpr,
                        SourceLocation OpLoc);

  // Used for emitting the right warning by DefaultVariadicArgumentPromotion
  enum VariadicCallType {
    VariadicFunction,
    VariadicBlock,
    VariadicMethod,
    VariadicConstructor,
    VariadicDoesNotApply
  };

  bool IsLayoutCompatible(QualType T1, QualType T2) const;
  bool IsPointerInterconvertibleBaseOf(const TypeSourceInfo *Base,
                                       const TypeSourceInfo *Derived);

  /// CheckFunctionCall - Check a direct function call for various correctness
  /// and safety properties not strictly enforced by the C type system.
  bool CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall,
                         const FunctionProtoType *Proto);

  bool BuiltinVectorMath(CallExpr *TheCall, QualType &Res);
  bool BuiltinVectorToScalarMath(CallExpr *TheCall);

  /// Handles the checks for format strings, non-POD arguments to vararg
  /// functions, NULL arguments passed to non-NULL parameters, diagnose_if
  /// attributes and AArch64 SME attributes.
  void checkCall(NamedDecl *FDecl, const FunctionProtoType *Proto,
                 const Expr *ThisArg, ArrayRef<const Expr *> Args,
                 bool IsMemberFunction, SourceLocation Loc, SourceRange Range,
                 VariadicCallType CallType);

  /// \brief Enforce the bounds of a TCB
  /// CheckTCBEnforcement - Enforces that every function in a named TCB only
  /// directly calls other functions in the same TCB as marked by the
  /// enforce_tcb and enforce_tcb_leaf attributes.
  void CheckTCBEnforcement(const SourceLocation CallExprLoc,
                           const NamedDecl *Callee);

  void CheckConstrainedAuto(const AutoType *AutoT, SourceLocation Loc);

  /// BuiltinConstantArg - Handle a check if argument ArgNum of CallExpr
  /// TheCall is a constant expression.
  bool BuiltinConstantArg(CallExpr *TheCall, int ArgNum, llvm::APSInt &Result);

  /// BuiltinConstantArgRange - Handle a check if argument ArgNum of CallExpr
  /// TheCall is a constant expression in the range [Low, High].
  bool BuiltinConstantArgRange(CallExpr *TheCall, int ArgNum, int Low, int High,
                               bool RangeIsError = true);

  /// BuiltinConstantArgMultiple - Handle a check if argument ArgNum of CallExpr
  /// TheCall is a constant expression is a multiple of Num..
  bool BuiltinConstantArgMultiple(CallExpr *TheCall, int ArgNum,
                                  unsigned Multiple);

  /// BuiltinConstantArgPower2 - Check if argument ArgNum of TheCall is a
  /// constant expression representing a power of 2.
  bool BuiltinConstantArgPower2(CallExpr *TheCall, int ArgNum);

  /// BuiltinConstantArgShiftedByte - Check if argument ArgNum of TheCall is
  /// a constant expression representing an arbitrary byte value shifted left by
  /// a multiple of 8 bits.
  bool BuiltinConstantArgShiftedByte(CallExpr *TheCall, int ArgNum,
                                     unsigned ArgBits);

  /// BuiltinConstantArgShiftedByteOr0xFF - Check if argument ArgNum of
  /// TheCall is a constant expression representing either a shifted byte value,
  /// or a value of the form 0x??FF (i.e. a member of the arithmetic progression
  /// 0x00FF, 0x01FF, ..., 0xFFFF). This strange range check is needed for some
  /// Arm MVE intrinsics.
  bool BuiltinConstantArgShiftedByteOrXXFF(CallExpr *TheCall, int ArgNum,
                                           unsigned ArgBits);

  /// Checks that a call expression's argument count is at least the desired
  /// number. This is useful when doing custom type-checking on a variadic
  /// function. Returns true on error.
  bool checkArgCountAtLeast(CallExpr *Call, unsigned MinArgCount);

  /// Checks that a call expression's argument count is at most the desired
  /// number. This is useful when doing custom type-checking on a variadic
  /// function. Returns true on error.
  bool checkArgCountAtMost(CallExpr *Call, unsigned MaxArgCount);

  /// Checks that a call expression's argument count is in the desired range.
  /// This is useful when doing custom type-checking on a variadic function.
  /// Returns true on error.
  bool checkArgCountRange(CallExpr *Call, unsigned MinArgCount,
                          unsigned MaxArgCount);

  /// Checks that a call expression's argument count is the desired number.
  /// This is useful when doing custom type-checking.  Returns true on error.
  bool checkArgCount(CallExpr *Call, unsigned DesiredArgCount);

  /// Returns true if the argument consists of one contiguous run of 1s with any
  /// number of 0s on either side. The 1s are allowed to wrap from LSB to MSB,
  /// so 0x000FFF0, 0x0000FFFF, 0xFF0000FF, 0x0 are all runs. 0x0F0F0000 is not,
  /// since all 1s are not contiguous.
  bool ValueIsRunOfOnes(CallExpr *TheCall, unsigned ArgNum);

  void CheckImplicitConversion(Expr *E, QualType T, SourceLocation CC,
                               bool *ICContext = nullptr,
                               bool IsListInit = false);

  bool BuiltinElementwiseTernaryMath(CallExpr *TheCall,
                                     bool CheckForFloatArgs = true);
  bool PrepareBuiltinElementwiseMathOneArgCall(CallExpr *TheCall);

private:
  void CheckArrayAccess(const Expr *BaseExpr, const Expr *IndexExpr,
                        const ArraySubscriptExpr *ASE = nullptr,
                        bool AllowOnePastEnd = true, bool IndexNegated = false);
  void CheckArrayAccess(const Expr *E);

  bool CheckPointerCall(NamedDecl *NDecl, CallExpr *TheCall,
                        const FunctionProtoType *Proto);

  /// Checks function calls when a FunctionDecl or a NamedDecl is not available,
  /// such as function pointers returned from functions.
  bool CheckOtherCall(CallExpr *TheCall, const FunctionProtoType *Proto);

  /// CheckConstructorCall - Check a constructor call for correctness and safety
  /// properties not enforced by the C type system.
  void CheckConstructorCall(FunctionDecl *FDecl, QualType ThisType,
                            ArrayRef<const Expr *> Args,
                            const FunctionProtoType *Proto, SourceLocation Loc);

  /// Warn if a pointer or reference argument passed to a function points to an
  /// object that is less aligned than the parameter. This can happen when
  /// creating a typedef with a lower alignment than the original type and then
  /// calling functions defined in terms of the original type.
  void CheckArgAlignment(SourceLocation Loc, NamedDecl *FDecl,
                         StringRef ParamName, QualType ArgTy, QualType ParamTy);

  ExprResult CheckOSLogFormatStringArg(Expr *Arg);

  ExprResult CheckBuiltinFunctionCall(FunctionDecl *FDecl, unsigned BuiltinID,
                                      CallExpr *TheCall);

  bool CheckTSBuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                  CallExpr *TheCall);

  void checkFortifiedBuiltinMemoryFunction(FunctionDecl *FD, CallExpr *TheCall);

  /// Check the arguments to '__builtin_va_start' or '__builtin_ms_va_start'
  /// for validity.  Emit an error and return true on failure; return false
  /// on success.
  bool BuiltinVAStart(unsigned BuiltinID, CallExpr *TheCall);
  bool BuiltinVAStartARMMicrosoft(CallExpr *Call);

  /// BuiltinUnorderedCompare - Handle functions like __builtin_isgreater and
  /// friends.  This is declared to take (...), so we have to check everything.
  bool BuiltinUnorderedCompare(CallExpr *TheCall, unsigned BuiltinID);

  /// BuiltinSemaBuiltinFPClassification - Handle functions like
  /// __builtin_isnan and friends.  This is declared to take (...), so we have
  /// to check everything.
  bool BuiltinFPClassification(CallExpr *TheCall, unsigned NumArgs,
                               unsigned BuiltinID);

  /// Perform semantic analysis for a call to __builtin_complex.
  bool BuiltinComplex(CallExpr *TheCall);
  bool BuiltinOSLogFormat(CallExpr *TheCall);

  /// BuiltinPrefetch - Handle __builtin_prefetch.
  /// This is declared to take (const void*, ...) and can take two
  /// optional constant int args.
  bool BuiltinPrefetch(CallExpr *TheCall);

  /// Handle __builtin_alloca_with_align. This is declared
  /// as (size_t, size_t) where the second size_t must be a power of 2 greater
  /// than 8.
  bool BuiltinAllocaWithAlign(CallExpr *TheCall);

  /// BuiltinArithmeticFence - Handle __arithmetic_fence.
  bool BuiltinArithmeticFence(CallExpr *TheCall);

  /// BuiltinAssume - Handle __assume (MS Extension).
  /// __assume does not evaluate its arguments, and should warn if its argument
  /// has side effects.
  bool BuiltinAssume(CallExpr *TheCall);

  /// Handle __builtin_assume_aligned. This is declared
  /// as (const void*, size_t, ...) and can take one optional constant int arg.
  bool BuiltinAssumeAligned(CallExpr *TheCall);

  /// BuiltinLongjmp - Handle __builtin_longjmp(void *env[5], int val).
  /// This checks that the target supports __builtin_longjmp and
  /// that val is a constant 1.
  bool BuiltinLongjmp(CallExpr *TheCall);

  /// BuiltinSetjmp - Handle __builtin_setjmp(void *env[5]).
  /// This checks that the target supports __builtin_setjmp.
  bool BuiltinSetjmp(CallExpr *TheCall);

  /// We have a call to a function like __sync_fetch_and_add, which is an
  /// overloaded function based on the pointer type of its first argument.
  /// The main BuildCallExpr routines have already promoted the types of
  /// arguments because all of these calls are prototyped as void(...).
  ///
  /// This function goes through and does final semantic checking for these
  /// builtins, as well as generating any warnings.
  ExprResult BuiltinAtomicOverloaded(ExprResult TheCallResult);

  /// BuiltinNontemporalOverloaded - We have a call to
  /// __builtin_nontemporal_store or __builtin_nontemporal_load, which is an
  /// overloaded function based on the pointer type of its last argument.
  ///
  /// This function goes through and does final semantic checking for these
  /// builtins.
  ExprResult BuiltinNontemporalOverloaded(ExprResult TheCallResult);
  ExprResult AtomicOpsOverloaded(ExprResult TheCallResult,
                                 AtomicExpr::AtomicOp Op);

  bool BuiltinElementwiseMath(CallExpr *TheCall);
  bool PrepareBuiltinReduceMathOneArgCall(CallExpr *TheCall);

  bool BuiltinNonDeterministicValue(CallExpr *TheCall);

  // Matrix builtin handling.
  ExprResult BuiltinMatrixTranspose(CallExpr *TheCall, ExprResult CallResult);
  ExprResult BuiltinMatrixColumnMajorLoad(CallExpr *TheCall,
                                          ExprResult CallResult);
  ExprResult BuiltinMatrixColumnMajorStore(CallExpr *TheCall,
                                           ExprResult CallResult);

  /// CheckFormatArguments - Check calls to printf and scanf (and similar
  /// functions) for correct use of format strings.
  /// Returns true if a format string has been fully checked.
  bool CheckFormatArguments(const FormatAttr *Format,
                            ArrayRef<const Expr *> Args, bool IsCXXMember,
                            VariadicCallType CallType, SourceLocation Loc,
                            SourceRange Range,
                            llvm::SmallBitVector &CheckedVarArgs);
  bool CheckFormatArguments(ArrayRef<const Expr *> Args,
                            FormatArgumentPassingKind FAPK, unsigned format_idx,
                            unsigned firstDataArg, FormatStringType Type,
                            VariadicCallType CallType, SourceLocation Loc,
                            SourceRange range,
                            llvm::SmallBitVector &CheckedVarArgs);

  void CheckInfNaNFunction(const CallExpr *Call, const FunctionDecl *FDecl);

  /// Warn when using the wrong abs() function.
  void CheckAbsoluteValueFunction(const CallExpr *Call,
                                  const FunctionDecl *FDecl);

  void CheckMaxUnsignedZero(const CallExpr *Call, const FunctionDecl *FDecl);

  /// Check for dangerous or invalid arguments to memset().
  ///
  /// This issues warnings on known problematic, dangerous or unspecified
  /// arguments to the standard 'memset', 'memcpy', 'memmove', and 'memcmp'
  /// function calls.
  ///
  /// \param Call The call expression to diagnose.
  void CheckMemaccessArguments(const CallExpr *Call, unsigned BId,
                               IdentifierInfo *FnName);

  // Warn if the user has made the 'size' argument to strlcpy or strlcat
  // be the size of the source, instead of the destination.
  void CheckStrlcpycatArguments(const CallExpr *Call, IdentifierInfo *FnName);

  // Warn on anti-patterns as the 'size' argument to strncat.
  // The correct size argument should look like following:
  //   strncat(dst, src, sizeof(dst) - strlen(dest) - 1);
  void CheckStrncatArguments(const CallExpr *Call, IdentifierInfo *FnName);

  /// Alerts the user that they are attempting to free a non-malloc'd object.
  void CheckFreeArguments(const CallExpr *E);

  void CheckReturnValExpr(Expr *RetValExp, QualType lhsType,
                          SourceLocation ReturnLoc, bool isObjCMethod = false,
                          const AttrVec *Attrs = nullptr,
                          const FunctionDecl *FD = nullptr);

  /// Diagnoses "dangerous" implicit conversions within the given
  /// expression (which is a full expression).  Implements -Wconversion
  /// and -Wsign-compare.
  ///
  /// \param CC the "context" location of the implicit conversion, i.e.
  ///   the most location of the syntactic entity requiring the implicit
  ///   conversion
  void CheckImplicitConversions(Expr *E, SourceLocation CC = SourceLocation());

  /// CheckBoolLikeConversion - Check conversion of given expression to boolean.
  /// Input argument E is a logical expression.
  void CheckBoolLikeConversion(Expr *E, SourceLocation CC);

  /// Diagnose when expression is an integer constant expression and its
  /// evaluation results in integer overflow
  void CheckForIntOverflow(const Expr *E);
  void CheckUnsequencedOperations(const Expr *E);

  /// Perform semantic checks on a completed expression. This will either
  /// be a full-expression or a default argument expression.
  void CheckCompletedExpr(Expr *E, SourceLocation CheckLoc = SourceLocation(),
                          bool IsConstexpr = false);

  void CheckBitFieldInitialization(SourceLocation InitLoc, FieldDecl *Field,
                                   Expr *Init);

  /// A map from magic value to type information.
  std::unique_ptr<llvm::DenseMap<TypeTagMagicValue, TypeTagData>>
      TypeTagForDatatypeMagicValues;

  /// Peform checks on a call of a function with argument_with_type_tag
  /// or pointer_with_type_tag attributes.
  void CheckArgumentWithTypeTag(const ArgumentWithTypeTagAttr *Attr,
                                const ArrayRef<const Expr *> ExprArgs,
                                SourceLocation CallSiteLoc);

  /// Check if we are taking the address of a packed field
  /// as this may be a problem if the pointer value is dereferenced.
  void CheckAddressOfPackedMember(Expr *rhs);

  /// Helper class that collects misaligned member designations and
  /// their location info for delayed diagnostics.
  struct MisalignedMember {
    Expr *E;
    RecordDecl *RD;
    ValueDecl *MD;
    CharUnits Alignment;

    MisalignedMember() : E(), RD(), MD() {}
    MisalignedMember(Expr *E, RecordDecl *RD, ValueDecl *MD,
                     CharUnits Alignment)
        : E(E), RD(RD), MD(MD), Alignment(Alignment) {}
    explicit MisalignedMember(Expr *E)
        : MisalignedMember(E, nullptr, nullptr, CharUnits()) {}

    bool operator==(const MisalignedMember &m) { return this->E == m.E; }
  };
  /// Small set of gathered accesses to potentially misaligned members
  /// due to the packed attribute.
  SmallVector<MisalignedMember, 4> MisalignedMembers;

  /// Adds an expression to the set of gathered misaligned members.
  void AddPotentialMisalignedMembers(Expr *E, RecordDecl *RD, ValueDecl *MD,
                                     CharUnits Alignment);
  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Coroutines
  /// Implementations are in SemaCoroutine.cpp
  ///@{

public:
  /// The C++ "std::coroutine_traits" template, which is defined in
  /// \<coroutine_traits>
  ClassTemplateDecl *StdCoroutineTraitsCache;

  bool ActOnCoroutineBodyStart(Scope *S, SourceLocation KwLoc,
                               StringRef Keyword);
  ExprResult ActOnCoawaitExpr(Scope *S, SourceLocation KwLoc, Expr *E);
  ExprResult ActOnCoyieldExpr(Scope *S, SourceLocation KwLoc, Expr *E);
  StmtResult ActOnCoreturnStmt(Scope *S, SourceLocation KwLoc, Expr *E);

  ExprResult BuildOperatorCoawaitLookupExpr(Scope *S, SourceLocation Loc);
  ExprResult BuildOperatorCoawaitCall(SourceLocation Loc, Expr *E,
                                      UnresolvedLookupExpr *Lookup);
  ExprResult BuildResolvedCoawaitExpr(SourceLocation KwLoc, Expr *Operand,
                                      Expr *Awaiter, bool IsImplicit = false);
  ExprResult BuildUnresolvedCoawaitExpr(SourceLocation KwLoc, Expr *Operand,
                                        UnresolvedLookupExpr *Lookup);
  ExprResult BuildCoyieldExpr(SourceLocation KwLoc, Expr *E);
  StmtResult BuildCoreturnStmt(SourceLocation KwLoc, Expr *E,
                               bool IsImplicit = false);
  StmtResult BuildCoroutineBodyStmt(CoroutineBodyStmt::CtorArgs);
  bool buildCoroutineParameterMoves(SourceLocation Loc);
  VarDecl *buildCoroutinePromise(SourceLocation Loc);
  void CheckCompletedCoroutineBody(FunctionDecl *FD, Stmt *&Body);

  // As a clang extension, enforces that a non-coroutine function must be marked
  // with [[clang::coro_wrapper]] if it returns a type marked with
  // [[clang::coro_return_type]].
  // Expects that FD is not a coroutine.
  void CheckCoroutineWrapper(FunctionDecl *FD);
  /// Lookup 'coroutine_traits' in std namespace and std::experimental
  /// namespace. The namespace found is recorded in Namespace.
  ClassTemplateDecl *lookupCoroutineTraits(SourceLocation KwLoc,
                                           SourceLocation FuncLoc);
  /// Check that the expression co_await promise.final_suspend() shall not be
  /// potentially-throwing.
  bool checkFinalSuspendNoThrow(const Stmt *FinalSuspend);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Scope Specifiers
  /// Implementations are in SemaCXXScopeSpec.cpp
  ///@{

public:
  // Marks SS invalid if it represents an incomplete type.
  bool RequireCompleteDeclContext(CXXScopeSpec &SS, DeclContext *DC);
  // Complete an enum decl, maybe without a scope spec.
  bool RequireCompleteEnumDecl(EnumDecl *D, SourceLocation L,
                               CXXScopeSpec *SS = nullptr);

  /// Compute the DeclContext that is associated with the given type.
  ///
  /// \param T the type for which we are attempting to find a DeclContext.
  ///
  /// \returns the declaration context represented by the type T,
  /// or NULL if the declaration context cannot be computed (e.g., because it is
  /// dependent and not the current instantiation).
  DeclContext *computeDeclContext(QualType T);

  /// Compute the DeclContext that is associated with the given
  /// scope specifier.
  ///
  /// \param SS the C++ scope specifier as it appears in the source
  ///
  /// \param EnteringContext when true, we will be entering the context of
  /// this scope specifier, so we can retrieve the declaration context of a
  /// class template or class template partial specialization even if it is
  /// not the current instantiation.
  ///
  /// \returns the declaration context represented by the scope specifier @p SS,
  /// or NULL if the declaration context cannot be computed (e.g., because it is
  /// dependent and not the current instantiation).
  DeclContext *computeDeclContext(const CXXScopeSpec &SS,
                                  bool EnteringContext = false);
  bool isDependentScopeSpecifier(const CXXScopeSpec &SS);

  /// If the given nested name specifier refers to the current
  /// instantiation, return the declaration that corresponds to that
  /// current instantiation (C++0x [temp.dep.type]p1).
  ///
  /// \param NNS a dependent nested name specifier.
  CXXRecordDecl *getCurrentInstantiationOf(NestedNameSpecifier *NNS);

  /// The parser has parsed a global nested-name-specifier '::'.
  ///
  /// \param CCLoc The location of the '::'.
  ///
  /// \param SS The nested-name-specifier, which will be updated in-place
  /// to reflect the parsed nested-name-specifier.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool ActOnCXXGlobalScopeSpecifier(SourceLocation CCLoc, CXXScopeSpec &SS);

  /// The parser has parsed a '__super' nested-name-specifier.
  ///
  /// \param SuperLoc The location of the '__super' keyword.
  ///
  /// \param ColonColonLoc The location of the '::'.
  ///
  /// \param SS The nested-name-specifier, which will be updated in-place
  /// to reflect the parsed nested-name-specifier.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool ActOnSuperScopeSpecifier(SourceLocation SuperLoc,
                                SourceLocation ColonColonLoc, CXXScopeSpec &SS);

  /// Determines whether the given declaration is an valid acceptable
  /// result for name lookup of a nested-name-specifier.
  /// \param SD Declaration checked for nested-name-specifier.
  /// \param IsExtension If not null and the declaration is accepted as an
  /// extension, the pointed variable is assigned true.
  bool isAcceptableNestedNameSpecifier(const NamedDecl *SD,
                                       bool *CanCorrect = nullptr);

  /// If the given nested-name-specifier begins with a bare identifier
  /// (e.g., Base::), perform name lookup for that identifier as a
  /// nested-name-specifier within the given scope, and return the result of
  /// that name lookup.
  NamedDecl *FindFirstQualifierInScope(Scope *S, NestedNameSpecifier *NNS);

  /// Keeps information about an identifier in a nested-name-spec.
  ///
  struct NestedNameSpecInfo {
    /// The type of the object, if we're parsing nested-name-specifier in
    /// a member access expression.
    ParsedType ObjectType;

    /// The identifier preceding the '::'.
    IdentifierInfo *Identifier;

    /// The location of the identifier.
    SourceLocation IdentifierLoc;

    /// The location of the '::'.
    SourceLocation CCLoc;

    /// Creates info object for the most typical case.
    NestedNameSpecInfo(IdentifierInfo *II, SourceLocation IdLoc,
                       SourceLocation ColonColonLoc,
                       ParsedType ObjectType = ParsedType())
        : ObjectType(ObjectType), Identifier(II), IdentifierLoc(IdLoc),
          CCLoc(ColonColonLoc) {}

    NestedNameSpecInfo(IdentifierInfo *II, SourceLocation IdLoc,
                       SourceLocation ColonColonLoc, QualType ObjectType)
        : ObjectType(ParsedType::make(ObjectType)), Identifier(II),
          IdentifierLoc(IdLoc), CCLoc(ColonColonLoc) {}
  };

  /// Build a new nested-name-specifier for "identifier::", as described
  /// by ActOnCXXNestedNameSpecifier.
  ///
  /// \param S Scope in which the nested-name-specifier occurs.
  /// \param IdInfo Parser information about an identifier in the
  ///        nested-name-spec.
  /// \param EnteringContext If true, enter the context specified by the
  ///        nested-name-specifier.
  /// \param SS Optional nested name specifier preceding the identifier.
  /// \param ScopeLookupResult Provides the result of name lookup within the
  ///        scope of the nested-name-specifier that was computed at template
  ///        definition time.
  /// \param ErrorRecoveryLookup Specifies if the method is called to improve
  ///        error recovery and what kind of recovery is performed.
  /// \param IsCorrectedToColon If not null, suggestion of replace '::' -> ':'
  ///        are allowed.  The bool value pointed by this parameter is set to
  ///       'true' if the identifier is treated as if it was followed by ':',
  ///        not '::'.
  /// \param OnlyNamespace If true, only considers namespaces in lookup.
  ///
  /// This routine differs only slightly from ActOnCXXNestedNameSpecifier, in
  /// that it contains an extra parameter \p ScopeLookupResult, which provides
  /// the result of name lookup within the scope of the nested-name-specifier
  /// that was computed at template definition time.
  ///
  /// If ErrorRecoveryLookup is true, then this call is used to improve error
  /// recovery.  This means that it should not emit diagnostics, it should
  /// just return true on failure.  It also means it should only return a valid
  /// scope if it *knows* that the result is correct.  It should not return in a
  /// dependent context, for example. Nor will it extend \p SS with the scope
  /// specifier.
  bool BuildCXXNestedNameSpecifier(Scope *S, NestedNameSpecInfo &IdInfo,
                                   bool EnteringContext, CXXScopeSpec &SS,
                                   NamedDecl *ScopeLookupResult,
                                   bool ErrorRecoveryLookup,
                                   bool *IsCorrectedToColon = nullptr,
                                   bool OnlyNamespace = false);

  /// The parser has parsed a nested-name-specifier 'identifier::'.
  ///
  /// \param S The scope in which this nested-name-specifier occurs.
  ///
  /// \param IdInfo Parser information about an identifier in the
  /// nested-name-spec.
  ///
  /// \param EnteringContext Whether we're entering the context nominated by
  /// this nested-name-specifier.
  ///
  /// \param SS The nested-name-specifier, which is both an input
  /// parameter (the nested-name-specifier before this type) and an
  /// output parameter (containing the full nested-name-specifier,
  /// including this new type).
  ///
  /// \param IsCorrectedToColon If not null, suggestions to replace '::' -> ':'
  /// are allowed.  The bool value pointed by this parameter is set to 'true'
  /// if the identifier is treated as if it was followed by ':', not '::'.
  ///
  /// \param OnlyNamespace If true, only considers namespaces in lookup.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool ActOnCXXNestedNameSpecifier(Scope *S, NestedNameSpecInfo &IdInfo,
                                   bool EnteringContext, CXXScopeSpec &SS,
                                   bool *IsCorrectedToColon = nullptr,
                                   bool OnlyNamespace = false);

  /// The parser has parsed a nested-name-specifier
  /// 'template[opt] template-name < template-args >::'.
  ///
  /// \param S The scope in which this nested-name-specifier occurs.
  ///
  /// \param SS The nested-name-specifier, which is both an input
  /// parameter (the nested-name-specifier before this type) and an
  /// output parameter (containing the full nested-name-specifier,
  /// including this new type).
  ///
  /// \param TemplateKWLoc the location of the 'template' keyword, if any.
  /// \param TemplateName the template name.
  /// \param TemplateNameLoc The location of the template name.
  /// \param LAngleLoc The location of the opening angle bracket  ('<').
  /// \param TemplateArgs The template arguments.
  /// \param RAngleLoc The location of the closing angle bracket  ('>').
  /// \param CCLoc The location of the '::'.
  ///
  /// \param EnteringContext Whether we're entering the context of the
  /// nested-name-specifier.
  ///
  ///
  /// \returns true if an error occurred, false otherwise.
  bool ActOnCXXNestedNameSpecifier(
      Scope *S, CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
      TemplateTy TemplateName, SourceLocation TemplateNameLoc,
      SourceLocation LAngleLoc, ASTTemplateArgsPtr TemplateArgs,
      SourceLocation RAngleLoc, SourceLocation CCLoc, bool EnteringContext);

  bool ActOnCXXNestedNameSpecifierDecltype(CXXScopeSpec &SS, const DeclSpec &DS,
                                           SourceLocation ColonColonLoc);

  bool ActOnCXXNestedNameSpecifierIndexedPack(CXXScopeSpec &SS,
                                              const DeclSpec &DS,
                                              SourceLocation ColonColonLoc,
                                              QualType Type);

  /// IsInvalidUnlessNestedName - This method is used for error recovery
  /// purposes to determine whether the specified identifier is only valid as
  /// a nested name specifier, for example a namespace name.  It is
  /// conservatively correct to always return false from this method.
  ///
  /// The arguments are the same as those passed to ActOnCXXNestedNameSpecifier.
  bool IsInvalidUnlessNestedName(Scope *S, CXXScopeSpec &SS,
                                 NestedNameSpecInfo &IdInfo,
                                 bool EnteringContext);

  /// Given a C++ nested-name-specifier, produce an annotation value
  /// that the parser can use later to reconstruct the given
  /// nested-name-specifier.
  ///
  /// \param SS A nested-name-specifier.
  ///
  /// \returns A pointer containing all of the information in the
  /// nested-name-specifier \p SS.
  void *SaveNestedNameSpecifierAnnotation(CXXScopeSpec &SS);

  /// Given an annotation pointer for a nested-name-specifier, restore
  /// the nested-name-specifier structure.
  ///
  /// \param Annotation The annotation pointer, produced by
  /// \c SaveNestedNameSpecifierAnnotation().
  ///
  /// \param AnnotationRange The source range corresponding to the annotation.
  ///
  /// \param SS The nested-name-specifier that will be updated with the contents
  /// of the annotation pointer.
  void RestoreNestedNameSpecifierAnnotation(void *Annotation,
                                            SourceRange AnnotationRange,
                                            CXXScopeSpec &SS);

  bool ShouldEnterDeclaratorScope(Scope *S, const CXXScopeSpec &SS);

  /// ActOnCXXEnterDeclaratorScope - Called when a C++ scope specifier (global
  /// scope or nested-name-specifier) is parsed, part of a declarator-id.
  /// After this method is called, according to [C++ 3.4.3p3], names should be
  /// looked up in the declarator-id's scope, until the declarator is parsed and
  /// ActOnCXXExitDeclaratorScope is called.
  /// The 'SS' should be a non-empty valid CXXScopeSpec.
  bool ActOnCXXEnterDeclaratorScope(Scope *S, CXXScopeSpec &SS);

  /// ActOnCXXExitDeclaratorScope - Called when a declarator that previously
  /// invoked ActOnCXXEnterDeclaratorScope(), is finished. 'SS' is the same
  /// CXXScopeSpec that was passed to ActOnCXXEnterDeclaratorScope as well.
  /// Used to indicate that names should revert to being looked up in the
  /// defining scope.
  void ActOnCXXExitDeclaratorScope(Scope *S, const CXXScopeSpec &SS);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Declarations
  /// Implementations are in SemaDecl.cpp
  ///@{

public:
  IdentifierResolver IdResolver;

  /// The index of the first InventedParameterInfo that refers to the current
  /// context.
  unsigned InventedParameterInfosStart = 0;

  /// A RAII object to temporarily push a declaration context.
  class ContextRAII {
  private:
    Sema &S;
    DeclContext *SavedContext;
    ProcessingContextState SavedContextState;
    QualType SavedCXXThisTypeOverride;
    unsigned SavedFunctionScopesStart;
    unsigned SavedInventedParameterInfosStart;

  public:
    ContextRAII(Sema &S, DeclContext *ContextToPush, bool NewThisContext = true)
        : S(S), SavedContext(S.CurContext),
          SavedContextState(S.DelayedDiagnostics.pushUndelayed()),
          SavedCXXThisTypeOverride(S.CXXThisTypeOverride),
          SavedFunctionScopesStart(S.FunctionScopesStart),
          SavedInventedParameterInfosStart(S.InventedParameterInfosStart) {
      assert(ContextToPush && "pushing null context");
      S.CurContext = ContextToPush;
      if (NewThisContext)
        S.CXXThisTypeOverride = QualType();
      // Any saved FunctionScopes do not refer to this context.
      S.FunctionScopesStart = S.FunctionScopes.size();
      S.InventedParameterInfosStart = S.InventedParameterInfos.size();
    }

    void pop() {
      if (!SavedContext)
        return;
      S.CurContext = SavedContext;
      S.DelayedDiagnostics.popUndelayed(SavedContextState);
      S.CXXThisTypeOverride = SavedCXXThisTypeOverride;
      S.FunctionScopesStart = SavedFunctionScopesStart;
      S.InventedParameterInfosStart = SavedInventedParameterInfosStart;
      SavedContext = nullptr;
    }

    ~ContextRAII() { pop(); }
  };

  void DiagnoseInvalidJumps(Stmt *Body);

  /// The function definitions which were renamed as part of typo-correction
  /// to match their respective declarations. We want to keep track of them
  /// to ensure that we don't emit a "redefinition" error if we encounter a
  /// correctly named definition after the renamed definition.
  llvm::SmallPtrSet<const NamedDecl *, 4> TypoCorrectedFunctionDefinitions;

  /// A cache of the flags available in enumerations with the flag_bits
  /// attribute.
  mutable llvm::DenseMap<const EnumDecl *, llvm::APInt> FlagBitsCache;

  /// WeakUndeclaredIdentifiers - Identifiers contained in \#pragma weak before
  /// declared. Rare. May alias another identifier, declared or undeclared.
  ///
  /// For aliases, the target identifier is used as a key for eventual
  /// processing when the target is declared. For the single-identifier form,
  /// the sole identifier is used as the key. Each entry is a `SetVector`
  /// (ordered by parse order) of aliases (identified by the alias name) in case
  /// of multiple aliases to the same undeclared identifier.
  llvm::MapVector<
      IdentifierInfo *,
      llvm::SetVector<
          WeakInfo, llvm::SmallVector<WeakInfo, 1u>,
          llvm::SmallDenseSet<WeakInfo, 2u, WeakInfo::DenseMapInfoByAliasOnly>>>
      WeakUndeclaredIdentifiers;

  /// ExtnameUndeclaredIdentifiers - Identifiers contained in
  /// \#pragma redefine_extname before declared.  Used in Solaris system headers
  /// to define functions that occur in multiple standards to call the version
  /// in the currently selected standard.
  llvm::DenseMap<IdentifierInfo *, AsmLabelAttr *> ExtnameUndeclaredIdentifiers;

  /// Set containing all typedefs that are likely unused.
  llvm::SmallSetVector<const TypedefNameDecl *, 4>
      UnusedLocalTypedefNameCandidates;

  typedef LazyVector<const DeclaratorDecl *, ExternalSemaSource,
                     &ExternalSemaSource::ReadUnusedFileScopedDecls, 2, 2>
      UnusedFileScopedDeclsType;

  /// The set of file scoped decls seen so far that have not been used
  /// and must warn if not used. Only contains the first declaration.
  UnusedFileScopedDeclsType UnusedFileScopedDecls;

  typedef LazyVector<VarDecl *, ExternalSemaSource,
                     &ExternalSemaSource::ReadTentativeDefinitions, 2, 2>
      TentativeDefinitionsType;

  /// All the tentative definitions encountered in the TU.
  TentativeDefinitionsType TentativeDefinitions;

  /// All the external declarations encoutered and used in the TU.
  SmallVector<DeclaratorDecl *, 4> ExternalDeclarations;

  /// Generally null except when we temporarily switch decl contexts,
  /// like in \see SemaObjC::ActOnObjCTemporaryExitContainerContext.
  DeclContext *OriginalLexicalContext;

  /// Is the module scope we are in a C++ Header Unit?
  bool currentModuleIsHeaderUnit() const {
    return ModuleScopes.empty() ? false
                                : ModuleScopes.back().Module->isHeaderUnit();
  }

  /// Get the module owning an entity.
  Module *getOwningModule(const Decl *Entity) {
    return Entity->getOwningModule();
  }

  DeclGroupPtrTy ConvertDeclToDeclGroup(Decl *Ptr, Decl *OwnedType = nullptr);

  /// If the identifier refers to a type name within this scope,
  /// return the declaration of that type.
  ///
  /// This routine performs ordinary name lookup of the identifier II
  /// within the given scope, with optional C++ scope specifier SS, to
  /// determine whether the name refers to a type. If so, returns an
  /// opaque pointer (actually a QualType) corresponding to that
  /// type. Otherwise, returns NULL.
  ParsedType getTypeName(const IdentifierInfo &II, SourceLocation NameLoc,
                         Scope *S, CXXScopeSpec *SS = nullptr,
                         bool isClassName = false, bool HasTrailingDot = false,
                         ParsedType ObjectType = nullptr,
                         bool IsCtorOrDtorName = false,
                         bool WantNontrivialTypeSourceInfo = false,
                         bool IsClassTemplateDeductionContext = true,
                         ImplicitTypenameContext AllowImplicitTypename =
                             ImplicitTypenameContext::No,
                         IdentifierInfo **CorrectedII = nullptr);

  /// isTagName() - This method is called *for error recovery purposes only*
  /// to determine if the specified name is a valid tag name ("struct foo").  If
  /// so, this returns the TST for the tag corresponding to it (TST_enum,
  /// TST_union, TST_struct, TST_interface, TST_class).  This is used to
  /// diagnose cases in C where the user forgot to specify the tag.
  TypeSpecifierType isTagName(IdentifierInfo &II, Scope *S);

  /// isMicrosoftMissingTypename - In Microsoft mode, within class scope,
  /// if a CXXScopeSpec's type is equal to the type of one of the base classes
  /// then downgrade the missing typename error to a warning.
  /// This is needed for MSVC compatibility; Example:
  /// @code
  /// template<class T> class A {
  /// public:
  ///   typedef int TYPE;
  /// };
  /// template<class T> class B : public A<T> {
  /// public:
  ///   A<T>::TYPE a; // no typename required because A<T> is a base class.
  /// };
  /// @endcode
  bool isMicrosoftMissingTypename(const CXXScopeSpec *SS, Scope *S);
  void DiagnoseUnknownTypeName(IdentifierInfo *&II, SourceLocation IILoc,
                               Scope *S, CXXScopeSpec *SS,
                               ParsedType &SuggestedType,
                               bool IsTemplateName = false);

  /// Attempt to behave like MSVC in situations where lookup of an unqualified
  /// type name has failed in a dependent context. In these situations, we
  /// automatically form a DependentTypeName that will retry lookup in a related
  /// scope during instantiation.
  ParsedType ActOnMSVCUnknownTypeName(const IdentifierInfo &II,
                                      SourceLocation NameLoc,
                                      bool IsTemplateTypeArg);

  /// Describes the result of the name lookup and resolution performed
  /// by \c ClassifyName().
  enum NameClassificationKind {
    /// This name is not a type or template in this context, but might be
    /// something else.
    NC_Unknown,
    /// Classification failed; an error has been produced.
    NC_Error,
    /// The name has been typo-corrected to a keyword.
    NC_Keyword,
    /// The name was classified as a type.
    NC_Type,
    /// The name was classified as a specific non-type, non-template
    /// declaration. ActOnNameClassifiedAsNonType should be called to
    /// convert the declaration to an expression.
    NC_NonType,
    /// The name was classified as an ADL-only function name.
    /// ActOnNameClassifiedAsUndeclaredNonType should be called to convert the
    /// result to an expression.
    NC_UndeclaredNonType,
    /// The name denotes a member of a dependent type that could not be
    /// resolved. ActOnNameClassifiedAsDependentNonType should be called to
    /// convert the result to an expression.
    NC_DependentNonType,
    /// The name was classified as an overload set, and an expression
    /// representing that overload set has been formed.
    /// ActOnNameClassifiedAsOverloadSet should be called to form a suitable
    /// expression referencing the overload set.
    NC_OverloadSet,
    /// The name was classified as a template whose specializations are types.
    NC_TypeTemplate,
    /// The name was classified as a variable template name.
    NC_VarTemplate,
    /// The name was classified as a function template name.
    NC_FunctionTemplate,
    /// The name was classified as an ADL-only function template name.
    NC_UndeclaredTemplate,
    /// The name was classified as a concept name.
    NC_Concept,
  };

  class NameClassification {
    NameClassificationKind Kind;
    union {
      ExprResult Expr;
      NamedDecl *NonTypeDecl;
      TemplateName Template;
      ParsedType Type;
    };

    explicit NameClassification(NameClassificationKind Kind) : Kind(Kind) {}

  public:
    NameClassification(ParsedType Type) : Kind(NC_Type), Type(Type) {}

    NameClassification(const IdentifierInfo *Keyword) : Kind(NC_Keyword) {}

    static NameClassification Error() { return NameClassification(NC_Error); }

    static NameClassification Unknown() {
      return NameClassification(NC_Unknown);
    }

    static NameClassification OverloadSet(ExprResult E) {
      NameClassification Result(NC_OverloadSet);
      Result.Expr = E;
      return Result;
    }

    static NameClassification NonType(NamedDecl *D) {
      NameClassification Result(NC_NonType);
      Result.NonTypeDecl = D;
      return Result;
    }

    static NameClassification UndeclaredNonType() {
      return NameClassification(NC_UndeclaredNonType);
    }

    static NameClassification DependentNonType() {
      return NameClassification(NC_DependentNonType);
    }

    static NameClassification TypeTemplate(TemplateName Name) {
      NameClassification Result(NC_TypeTemplate);
      Result.Template = Name;
      return Result;
    }

    static NameClassification VarTemplate(TemplateName Name) {
      NameClassification Result(NC_VarTemplate);
      Result.Template = Name;
      return Result;
    }

    static NameClassification FunctionTemplate(TemplateName Name) {
      NameClassification Result(NC_FunctionTemplate);
      Result.Template = Name;
      return Result;
    }

    static NameClassification Concept(TemplateName Name) {
      NameClassification Result(NC_Concept);
      Result.Template = Name;
      return Result;
    }

    static NameClassification UndeclaredTemplate(TemplateName Name) {
      NameClassification Result(NC_UndeclaredTemplate);
      Result.Template = Name;
      return Result;
    }

    NameClassificationKind getKind() const { return Kind; }

    ExprResult getExpression() const {
      assert(Kind == NC_OverloadSet);
      return Expr;
    }

    ParsedType getType() const {
      assert(Kind == NC_Type);
      return Type;
    }

    NamedDecl *getNonTypeDecl() const {
      assert(Kind == NC_NonType);
      return NonTypeDecl;
    }

    TemplateName getTemplateName() const {
      assert(Kind == NC_TypeTemplate || Kind == NC_FunctionTemplate ||
             Kind == NC_VarTemplate || Kind == NC_Concept ||
             Kind == NC_UndeclaredTemplate);
      return Template;
    }

    TemplateNameKind getTemplateNameKind() const {
      switch (Kind) {
      case NC_TypeTemplate:
        return TNK_Type_template;
      case NC_FunctionTemplate:
        return TNK_Function_template;
      case NC_VarTemplate:
        return TNK_Var_template;
      case NC_Concept:
        return TNK_Concept_template;
      case NC_UndeclaredTemplate:
        return TNK_Undeclared_template;
      default:
        llvm_unreachable("unsupported name classification.");
      }
    }
  };

  /// Perform name lookup on the given name, classifying it based on
  /// the results of name lookup and the following token.
  ///
  /// This routine is used by the parser to resolve identifiers and help direct
  /// parsing. When the identifier cannot be found, this routine will attempt
  /// to correct the typo and classify based on the resulting name.
  ///
  /// \param S The scope in which we're performing name lookup.
  ///
  /// \param SS The nested-name-specifier that precedes the name.
  ///
  /// \param Name The identifier. If typo correction finds an alternative name,
  /// this pointer parameter will be updated accordingly.
  ///
  /// \param NameLoc The location of the identifier.
  ///
  /// \param NextToken The token following the identifier. Used to help
  /// disambiguate the name.
  ///
  /// \param CCC The correction callback, if typo correction is desired.
  NameClassification ClassifyName(Scope *S, CXXScopeSpec &SS,
                                  IdentifierInfo *&Name, SourceLocation NameLoc,
                                  const Token &NextToken,
                                  CorrectionCandidateCallback *CCC = nullptr);

  /// Act on the result of classifying a name as an undeclared (ADL-only)
  /// non-type declaration.
  ExprResult ActOnNameClassifiedAsUndeclaredNonType(IdentifierInfo *Name,
                                                    SourceLocation NameLoc);
  /// Act on the result of classifying a name as an undeclared member of a
  /// dependent base class.
  ExprResult ActOnNameClassifiedAsDependentNonType(const CXXScopeSpec &SS,
                                                   IdentifierInfo *Name,
                                                   SourceLocation NameLoc,
                                                   bool IsAddressOfOperand);
  /// Act on the result of classifying a name as a specific non-type
  /// declaration.
  ExprResult ActOnNameClassifiedAsNonType(Scope *S, const CXXScopeSpec &SS,
                                          NamedDecl *Found,
                                          SourceLocation NameLoc,
                                          const Token &NextToken);
  /// Act on the result of classifying a name as an overload set.
  ExprResult ActOnNameClassifiedAsOverloadSet(Scope *S, Expr *OverloadSet);

  /// Describes the detailed kind of a template name. Used in diagnostics.
  enum class TemplateNameKindForDiagnostics {
    ClassTemplate,
    FunctionTemplate,
    VarTemplate,
    AliasTemplate,
    TemplateTemplateParam,
    Concept,
    DependentTemplate
  };
  TemplateNameKindForDiagnostics
  getTemplateNameKindForDiagnostics(TemplateName Name);

  /// Determine whether it's plausible that E was intended to be a
  /// template-name.
  bool mightBeIntendedToBeTemplateName(ExprResult E, bool &Dependent) {
    if (!getLangOpts().CPlusPlus || E.isInvalid())
      return false;
    Dependent = false;
    if (auto *DRE = dyn_cast<DeclRefExpr>(E.get()))
      return !DRE->hasExplicitTemplateArgs();
    if (auto *ME = dyn_cast<MemberExpr>(E.get()))
      return !ME->hasExplicitTemplateArgs();
    Dependent = true;
    if (auto *DSDRE = dyn_cast<DependentScopeDeclRefExpr>(E.get()))
      return !DSDRE->hasExplicitTemplateArgs();
    if (auto *DSME = dyn_cast<CXXDependentScopeMemberExpr>(E.get()))
      return !DSME->hasExplicitTemplateArgs();
    // Any additional cases recognized here should also be handled by
    // diagnoseExprIntendedAsTemplateName.
    return false;
  }

  void warnOnReservedIdentifier(const NamedDecl *D);

  Decl *ActOnDeclarator(Scope *S, Declarator &D);

  NamedDecl *HandleDeclarator(Scope *S, Declarator &D,
                              MultiTemplateParamsArg TemplateParameterLists);

  /// Attempt to fold a variable-sized type to a constant-sized type, returning
  /// true if we were successful.
  bool tryToFixVariablyModifiedVarType(TypeSourceInfo *&TInfo, QualType &T,
                                       SourceLocation Loc,
                                       unsigned FailedFoldDiagID);

  /// Register the given locally-scoped extern "C" declaration so
  /// that it can be found later for redeclarations. We include any extern "C"
  /// declaration that is not visible in the translation unit here, not just
  /// function-scope declarations.
  void RegisterLocallyScopedExternCDecl(NamedDecl *ND, Scope *S);

  /// DiagnoseClassNameShadow - Implement C++ [class.mem]p13:
  ///   If T is the name of a class, then each of the following shall have a
  ///   name different from T:
  ///     - every static data member of class T;
  ///     - every member function of class T
  ///     - every member of class T that is itself a type;
  /// \returns true if the declaration name violates these rules.
  bool DiagnoseClassNameShadow(DeclContext *DC, DeclarationNameInfo Info);

  /// Diagnose a declaration whose declarator-id has the given
  /// nested-name-specifier.
  ///
  /// \param SS The nested-name-specifier of the declarator-id.
  ///
  /// \param DC The declaration context to which the nested-name-specifier
  /// resolves.
  ///
  /// \param Name The name of the entity being declared.
  ///
  /// \param Loc The location of the name of the entity being declared.
  ///
  /// \param IsMemberSpecialization Whether we are declaring a member
  /// specialization.
  ///
  /// \param TemplateId The template-id, if any.
  ///
  /// \returns true if we cannot safely recover from this error, false
  /// otherwise.
  bool diagnoseQualifiedDeclaration(CXXScopeSpec &SS, DeclContext *DC,
                                    DeclarationName Name, SourceLocation Loc,
                                    TemplateIdAnnotation *TemplateId,
                                    bool IsMemberSpecialization);

  bool checkPointerAuthEnabled(SourceLocation Loc, SourceRange Range);

  bool checkConstantPointerAuthKey(Expr *keyExpr, unsigned &key);

  /// Diagnose function specifiers on a declaration of an identifier that
  /// does not identify a function.
  void DiagnoseFunctionSpecifiers(const DeclSpec &DS);

  /// Return the declaration shadowed by the given typedef \p D, or null
  /// if it doesn't shadow any declaration or shadowing warnings are disabled.
  NamedDecl *getShadowedDeclaration(const TypedefNameDecl *D,
                                    const LookupResult &R);

  /// Return the declaration shadowed by the given variable \p D, or null
  /// if it doesn't shadow any declaration or shadowing warnings are disabled.
  NamedDecl *getShadowedDeclaration(const VarDecl *D, const LookupResult &R);

  /// Return the declaration shadowed by the given variable \p D, or null
  /// if it doesn't shadow any declaration or shadowing warnings are disabled.
  NamedDecl *getShadowedDeclaration(const BindingDecl *D,
                                    const LookupResult &R);
  /// Diagnose variable or built-in function shadowing.  Implements
  /// -Wshadow.
  ///
  /// This method is called whenever a VarDecl is added to a "useful"
  /// scope.
  ///
  /// \param ShadowedDecl the declaration that is shadowed by the given variable
  /// \param R the lookup of the name
  void CheckShadow(NamedDecl *D, NamedDecl *ShadowedDecl,
                   const LookupResult &R);

  /// Check -Wshadow without the advantage of a previous lookup.
  void CheckShadow(Scope *S, VarDecl *D);

  /// Warn if 'E', which is an expression that is about to be modified, refers
  /// to a shadowing declaration.
  void CheckShadowingDeclModification(Expr *E, SourceLocation Loc);

  /// Diagnose shadowing for variables shadowed in the lambda record \p LambdaRD
  /// when these variables are captured by the lambda.
  void DiagnoseShadowingLambdaDecls(const sema::LambdaScopeInfo *LSI);

  void handleTagNumbering(const TagDecl *Tag, Scope *TagScope);
  void setTagNameForLinkagePurposes(TagDecl *TagFromDeclSpec,
                                    TypedefNameDecl *NewTD);
  void CheckTypedefForVariablyModifiedType(Scope *S, TypedefNameDecl *D);
  NamedDecl *ActOnTypedefDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                    TypeSourceInfo *TInfo,
                                    LookupResult &Previous);

  /// ActOnTypedefNameDecl - Perform semantic checking for a declaration which
  /// declares a typedef-name, either using the 'typedef' type specifier or via
  /// a C++0x [dcl.typedef]p2 alias-declaration: 'using T = A;'.
  NamedDecl *ActOnTypedefNameDecl(Scope *S, DeclContext *DC, TypedefNameDecl *D,
                                  LookupResult &Previous, bool &Redeclaration);
  NamedDecl *ActOnVariableDeclarator(
      Scope *S, Declarator &D, DeclContext *DC, TypeSourceInfo *TInfo,
      LookupResult &Previous, MultiTemplateParamsArg TemplateParamLists,
      bool &AddToScope, ArrayRef<BindingDecl *> Bindings = std::nullopt);

  /// Perform semantic checking on a newly-created variable
  /// declaration.
  ///
  /// This routine performs all of the type-checking required for a
  /// variable declaration once it has been built. It is used both to
  /// check variables after they have been parsed and their declarators
  /// have been translated into a declaration, and to check variables
  /// that have been instantiated from a template.
  ///
  /// Sets NewVD->isInvalidDecl() if an error was encountered.
  ///
  /// Returns true if the variable declaration is a redeclaration.
  bool CheckVariableDeclaration(VarDecl *NewVD, LookupResult &Previous);
  void CheckVariableDeclarationType(VarDecl *NewVD);
  void CheckCompleteVariableDeclaration(VarDecl *VD);

  NamedDecl *ActOnFunctionDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                                     TypeSourceInfo *TInfo,
                                     LookupResult &Previous,
                                     MultiTemplateParamsArg TemplateParamLists,
                                     bool &AddToScope);

  /// AddOverriddenMethods - See if a method overrides any in the base classes,
  /// and if so, check that it's a valid override and remember it.
  bool AddOverriddenMethods(CXXRecordDecl *DC, CXXMethodDecl *MD);

  /// Perform semantic checking of a new function declaration.
  ///
  /// Performs semantic analysis of the new function declaration
  /// NewFD. This routine performs all semantic checking that does not
  /// require the actual declarator involved in the declaration, and is
  /// used both for the declaration of functions as they are parsed
  /// (called via ActOnDeclarator) and for the declaration of functions
  /// that have been instantiated via C++ template instantiation (called
  /// via InstantiateDecl).
  ///
  /// \param IsMemberSpecialization whether this new function declaration is
  /// a member specialization (that replaces any definition provided by the
  /// previous declaration).
  ///
  /// This sets NewFD->isInvalidDecl() to true if there was an error.
  ///
  /// \returns true if the function declaration is a redeclaration.
  bool CheckFunctionDeclaration(Scope *S, FunctionDecl *NewFD,
                                LookupResult &Previous,
                                bool IsMemberSpecialization, bool DeclIsDefn);

  /// Checks if the new declaration declared in dependent context must be
  /// put in the same redeclaration chain as the specified declaration.
  ///
  /// \param D Declaration that is checked.
  /// \param PrevDecl Previous declaration found with proper lookup method for
  ///                 the same declaration name.
  /// \returns True if D must be added to the redeclaration chain which PrevDecl
  ///          belongs to.
  bool shouldLinkDependentDeclWithPrevious(Decl *D, Decl *OldDecl);

  /// Determines if we can perform a correct type check for \p D as a
  /// redeclaration of \p PrevDecl. If not, we can generally still perform a
  /// best-effort check.
  ///
  /// \param NewD The new declaration.
  /// \param OldD The old declaration.
  /// \param NewT The portion of the type of the new declaration to check.
  /// \param OldT The portion of the type of the old declaration to check.
  bool canFullyTypeCheckRedeclaration(ValueDecl *NewD, ValueDecl *OldD,
                                      QualType NewT, QualType OldT);
  void CheckMain(FunctionDecl *FD, const DeclSpec &D);
  void CheckMSVCRTEntryPoint(FunctionDecl *FD);

  /// Returns an implicit CodeSegAttr if a __declspec(code_seg) is found on a
  /// containing class. Otherwise it will return implicit SectionAttr if the
  /// function is a definition and there is an active value on CodeSegStack
  /// (from the current #pragma code-seg value).
  ///
  /// \param FD Function being declared.
  /// \param IsDefinition Whether it is a definition or just a declaration.
  /// \returns A CodeSegAttr or SectionAttr to apply to the function or
  ///          nullptr if no attribute should be added.
  Attr *getImplicitCodeSegOrSectionAttrForFunction(const FunctionDecl *FD,
                                                   bool IsDefinition);

  /// Common checks for a parameter-declaration that should apply to both
  /// function parameters and non-type template parameters.
  void CheckFunctionOrTemplateParamDeclarator(Scope *S, Declarator &D);

  /// ActOnParamDeclarator - Called from Parser::ParseFunctionDeclarator()
  /// to introduce parameters into function prototype scope.
  Decl *ActOnParamDeclarator(Scope *S, Declarator &D,
                             SourceLocation ExplicitThisLoc = {});

  /// Synthesizes a variable for a parameter arising from a
  /// typedef.
  ParmVarDecl *BuildParmVarDeclForTypedef(DeclContext *DC, SourceLocation Loc,
                                          QualType T);
  ParmVarDecl *CheckParameter(DeclContext *DC, SourceLocation StartLoc,
                              SourceLocation NameLoc,
                              const IdentifierInfo *Name, QualType T,
                              TypeSourceInfo *TSInfo, StorageClass SC);

  // Contexts where using non-trivial C union types can be disallowed. This is
  // passed to err_non_trivial_c_union_in_invalid_context.
  enum NonTrivialCUnionContext {
    // Function parameter.
    NTCUC_FunctionParam,
    // Function return.
    NTCUC_FunctionReturn,
    // Default-initialized object.
    NTCUC_DefaultInitializedObject,
    // Variable with automatic storage duration.
    NTCUC_AutoVar,
    // Initializer expression that might copy from another object.
    NTCUC_CopyInit,
    // Assignment.
    NTCUC_Assignment,
    // Compound literal.
    NTCUC_CompoundLiteral,
    // Block capture.
    NTCUC_BlockCapture,
    // lvalue-to-rvalue conversion of volatile type.
    NTCUC_LValueToRValueVolatile,
  };

  /// Emit diagnostics if the initializer or any of its explicit or
  /// implicitly-generated subexpressions require copying or
  /// default-initializing a type that is or contains a C union type that is
  /// non-trivial to copy or default-initialize.
  void checkNonTrivialCUnionInInitializer(const Expr *Init, SourceLocation Loc);

  // These flags are passed to checkNonTrivialCUnion.
  enum NonTrivialCUnionKind {
    NTCUK_Init = 0x1,
    NTCUK_Destruct = 0x2,
    NTCUK_Copy = 0x4,
  };

  /// Emit diagnostics if a non-trivial C union type or a struct that contains
  /// a non-trivial C union is used in an invalid context.
  void checkNonTrivialCUnion(QualType QT, SourceLocation Loc,
                             NonTrivialCUnionContext UseContext,
                             unsigned NonTrivialKind);

  /// AddInitializerToDecl - Adds the initializer Init to the
  /// declaration dcl. If DirectInit is true, this is C++ direct
  /// initialization rather than copy initialization.
  void AddInitializerToDecl(Decl *dcl, Expr *init, bool DirectInit);
  void ActOnUninitializedDecl(Decl *dcl);

  /// ActOnInitializerError - Given that there was an error parsing an
  /// initializer for the given declaration, try to at least re-establish
  /// invariants such as whether a variable's type is either dependent or
  /// complete.
  void ActOnInitializerError(Decl *Dcl);

  void ActOnCXXForRangeDecl(Decl *D);
  StmtResult ActOnCXXForRangeIdentifier(Scope *S, SourceLocation IdentLoc,
                                        IdentifierInfo *Ident,
                                        ParsedAttributes &Attrs);

  /// Check if VD needs to be dllexport/dllimport due to being in a
  /// dllexport/import function.
  void CheckStaticLocalForDllExport(VarDecl *VD);
  void CheckThreadLocalForLargeAlignment(VarDecl *VD);

  /// FinalizeDeclaration - called by ParseDeclarationAfterDeclarator to perform
  /// any semantic actions necessary after any initializer has been attached.
  void FinalizeDeclaration(Decl *D);
  DeclGroupPtrTy FinalizeDeclaratorGroup(Scope *S, const DeclSpec &DS,
                                         ArrayRef<Decl *> Group);

  /// BuildDeclaratorGroup - convert a list of declarations into a declaration
  /// group, performing any necessary semantic checking.
  DeclGroupPtrTy BuildDeclaratorGroup(MutableArrayRef<Decl *> Group);

  /// Should be called on all declarations that might have attached
  /// documentation comments.
  void ActOnDocumentableDecl(Decl *D);
  void ActOnDocumentableDecls(ArrayRef<Decl *> Group);

  enum class FnBodyKind {
    /// C++26 [dcl.fct.def.general]p1
    /// function-body:
    ///   ctor-initializer[opt] compound-statement
    ///   function-try-block
    Other,
    ///   = default ;
    Default,
    ///   deleted-function-body
    ///
    /// deleted-function-body:
    ///   = delete ;
    ///   = delete ( unevaluated-string ) ;
    Delete
  };

  void ActOnFinishKNRParamDeclarations(Scope *S, Declarator &D,
                                       SourceLocation LocAfterDecls);
  void CheckForFunctionRedefinition(
      FunctionDecl *FD, const FunctionDecl *EffectiveDefinition = nullptr,
      SkipBodyInfo *SkipBody = nullptr);
  Decl *ActOnStartOfFunctionDef(Scope *S, Declarator &D,
                                MultiTemplateParamsArg TemplateParamLists,
                                SkipBodyInfo *SkipBody = nullptr,
                                FnBodyKind BodyKind = FnBodyKind::Other);
  Decl *ActOnStartOfFunctionDef(Scope *S, Decl *D,
                                SkipBodyInfo *SkipBody = nullptr,
                                FnBodyKind BodyKind = FnBodyKind::Other);
  void applyFunctionAttributesBeforeParsingBody(Decl *FD);

  /// Determine whether we can delay parsing the body of a function or
  /// function template until it is used, assuming we don't care about emitting
  /// code for that function.
  ///
  /// This will be \c false if we may need the body of the function in the
  /// middle of parsing an expression (where it's impractical to switch to
  /// parsing a different function), for instance, if it's constexpr in C++11
  /// or has an 'auto' return type in C++14. These cases are essentially bugs.
  bool canDelayFunctionBody(const Declarator &D);

  /// Determine whether we can skip parsing the body of a function
  /// definition, assuming we don't care about analyzing its body or emitting
  /// code for that function.
  ///
  /// This will be \c false only if we may need the body of the function in
  /// order to parse the rest of the program (for instance, if it is
  /// \c constexpr in C++11 or has an 'auto' return type in C++14).
  bool canSkipFunctionBody(Decl *D);

  /// Given the set of return statements within a function body,
  /// compute the variables that are subject to the named return value
  /// optimization.
  ///
  /// Each of the variables that is subject to the named return value
  /// optimization will be marked as NRVO variables in the AST, and any
  /// return statement that has a marked NRVO variable as its NRVO candidate can
  /// use the named return value optimization.
  ///
  /// This function applies a very simplistic algorithm for NRVO: if every
  /// return statement in the scope of a variable has the same NRVO candidate,
  /// that candidate is an NRVO variable.
  void computeNRVO(Stmt *Body, sema::FunctionScopeInfo *Scope);
  Decl *ActOnFinishFunctionBody(Decl *Decl, Stmt *Body);
  Decl *ActOnFinishFunctionBody(Decl *Decl, Stmt *Body, bool IsInstantiation);
  Decl *ActOnSkippedFunctionBody(Decl *Decl);
  void ActOnFinishInlineFunctionDef(FunctionDecl *D);

  /// ActOnFinishDelayedAttribute - Invoked when we have finished parsing an
  /// attribute for which parsing is delayed.
  void ActOnFinishDelayedAttribute(Scope *S, Decl *D, ParsedAttributes &Attrs);

  /// Diagnose any unused parameters in the given sequence of
  /// ParmVarDecl pointers.
  void DiagnoseUnusedParameters(ArrayRef<ParmVarDecl *> Parameters);

  /// Diagnose whether the size of parameters or return value of a
  /// function or obj-c method definition is pass-by-value and larger than a
  /// specified threshold.
  void
  DiagnoseSizeOfParametersAndReturnValue(ArrayRef<ParmVarDecl *> Parameters,
                                         QualType ReturnTy, NamedDecl *D);

  Decl *ActOnFileScopeAsmDecl(Expr *expr, SourceLocation AsmLoc,
                              SourceLocation RParenLoc);

  TopLevelStmtDecl *ActOnStartTopLevelStmtDecl(Scope *S);
  void ActOnFinishTopLevelStmtDecl(TopLevelStmtDecl *D, Stmt *Statement);

  void ActOnPopScope(SourceLocation Loc, Scope *S);

  /// ParsedFreeStandingDeclSpec - This method is invoked when a declspec with
  /// no declarator (e.g. "struct foo;") is parsed.
  Decl *ParsedFreeStandingDeclSpec(Scope *S, AccessSpecifier AS, DeclSpec &DS,
                                   const ParsedAttributesView &DeclAttrs,
                                   RecordDecl *&AnonRecord);

  /// ParsedFreeStandingDeclSpec - This method is invoked when a declspec with
  /// no declarator (e.g. "struct foo;") is parsed. It also accepts template
  /// parameters to cope with template friend declarations.
  Decl *ParsedFreeStandingDeclSpec(Scope *S, AccessSpecifier AS, DeclSpec &DS,
                                   const ParsedAttributesView &DeclAttrs,
                                   MultiTemplateParamsArg TemplateParams,
                                   bool IsExplicitInstantiation,
                                   RecordDecl *&AnonRecord);

  /// BuildAnonymousStructOrUnion - Handle the declaration of an
  /// anonymous structure or union. Anonymous unions are a C++ feature
  /// (C++ [class.union]) and a C11 feature; anonymous structures
  /// are a C11 feature and GNU C++ extension.
  Decl *BuildAnonymousStructOrUnion(Scope *S, DeclSpec &DS, AccessSpecifier AS,
                                    RecordDecl *Record,
                                    const PrintingPolicy &Policy);

  /// Called once it is known whether
  /// a tag declaration is an anonymous union or struct.
  void ActOnDefinedDeclarationSpecifier(Decl *D);

  /// Emit diagnostic warnings for placeholder members.
  /// We can only do that after the class is fully constructed,
  /// as anonymous union/structs can insert placeholders
  /// in their parent scope (which might be a Record).
  void DiagPlaceholderFieldDeclDefinitions(RecordDecl *Record);

  /// BuildMicrosoftCAnonymousStruct - Handle the declaration of an
  /// Microsoft C anonymous structure.
  /// Ref: http://msdn.microsoft.com/en-us/library/z2cx9y4f.aspx
  /// Example:
  ///
  /// struct A { int a; };
  /// struct B { struct A; int b; };
  ///
  /// void foo() {
  ///   B var;
  ///   var.a = 3;
  /// }
  Decl *BuildMicrosoftCAnonymousStruct(Scope *S, DeclSpec &DS,
                                       RecordDecl *Record);

  /// Common ways to introduce type names without a tag for use in diagnostics.
  /// Keep in sync with err_tag_reference_non_tag.
  enum NonTagKind {
    NTK_NonStruct,
    NTK_NonClass,
    NTK_NonUnion,
    NTK_NonEnum,
    NTK_Typedef,
    NTK_TypeAlias,
    NTK_Template,
    NTK_TypeAliasTemplate,
    NTK_TemplateTemplateArgument,
  };

  /// Given a non-tag type declaration, returns an enum useful for indicating
  /// what kind of non-tag type this is.
  NonTagKind getNonTagTypeDeclKind(const Decl *D, TagTypeKind TTK);

  /// Determine whether a tag with a given kind is acceptable
  /// as a redeclaration of the given tag declaration.
  ///
  /// \returns true if the new tag kind is acceptable, false otherwise.
  bool isAcceptableTagRedeclaration(const TagDecl *Previous, TagTypeKind NewTag,
                                    bool isDefinition, SourceLocation NewTagLoc,
                                    const IdentifierInfo *Name);

  enum OffsetOfKind {
    // Not parsing a type within __builtin_offsetof.
    OOK_Outside,
    // Parsing a type within __builtin_offsetof.
    OOK_Builtin,
    // Parsing a type within macro "offsetof", defined in __buitin_offsetof
    // To improve our diagnostic message.
    OOK_Macro,
  };

  /// This is invoked when we see 'struct foo' or 'struct {'.  In the
  /// former case, Name will be non-null.  In the later case, Name will be null.
  /// TagSpec indicates what kind of tag this is. TUK indicates whether this is
  /// a reference/declaration/definition of a tag.
  ///
  /// \param IsTypeSpecifier \c true if this is a type-specifier (or
  /// trailing-type-specifier) other than one in an alias-declaration.
  ///
  /// \param SkipBody If non-null, will be set to indicate if the caller should
  /// skip the definition of this tag and treat it as if it were a declaration.
  DeclResult ActOnTag(Scope *S, unsigned TagSpec, TagUseKind TUK,
                      SourceLocation KWLoc, CXXScopeSpec &SS,
                      IdentifierInfo *Name, SourceLocation NameLoc,
                      const ParsedAttributesView &Attr, AccessSpecifier AS,
                      SourceLocation ModulePrivateLoc,
                      MultiTemplateParamsArg TemplateParameterLists,
                      bool &OwnedDecl, bool &IsDependent,
                      SourceLocation ScopedEnumKWLoc,
                      bool ScopedEnumUsesClassTag, TypeResult UnderlyingType,
                      bool IsTypeSpecifier, bool IsTemplateParamOrArg,
                      OffsetOfKind OOK, SkipBodyInfo *SkipBody = nullptr);

  /// ActOnField - Each field of a C struct/union is passed into this in order
  /// to create a FieldDecl object for it.
  Decl *ActOnField(Scope *S, Decl *TagD, SourceLocation DeclStart,
                   Declarator &D, Expr *BitfieldWidth);

  /// HandleField - Analyze a field of a C struct or a C++ data member.
  FieldDecl *HandleField(Scope *S, RecordDecl *TagD, SourceLocation DeclStart,
                         Declarator &D, Expr *BitfieldWidth,
                         InClassInitStyle InitStyle, AccessSpecifier AS);

  /// Build a new FieldDecl and check its well-formedness.
  ///
  /// This routine builds a new FieldDecl given the fields name, type,
  /// record, etc. \p PrevDecl should refer to any previous declaration
  /// with the same name and in the same scope as the field to be
  /// created.
  ///
  /// \returns a new FieldDecl.
  ///
  /// \todo The Declarator argument is a hack. It will be removed once
  FieldDecl *CheckFieldDecl(DeclarationName Name, QualType T,
                            TypeSourceInfo *TInfo, RecordDecl *Record,
                            SourceLocation Loc, bool Mutable,
                            Expr *BitfieldWidth, InClassInitStyle InitStyle,
                            SourceLocation TSSL, AccessSpecifier AS,
                            NamedDecl *PrevDecl, Declarator *D = nullptr);

  bool CheckNontrivialField(FieldDecl *FD);

  /// ActOnLastBitfield - This routine handles synthesized bitfields rules for
  /// class and class extensions. For every class \@interface and class
  /// extension \@interface, if the last ivar is a bitfield of any type,
  /// then add an implicit `char :0` ivar to the end of that interface.
  void ActOnLastBitfield(SourceLocation DeclStart,
                         SmallVectorImpl<Decl *> &AllIvarDecls);

  // This is used for both record definitions and ObjC interface declarations.
  void ActOnFields(Scope *S, SourceLocation RecLoc, Decl *TagDecl,
                   ArrayRef<Decl *> Fields, SourceLocation LBrac,
                   SourceLocation RBrac, const ParsedAttributesView &AttrList);

  /// ActOnTagStartDefinition - Invoked when we have entered the
  /// scope of a tag's definition (e.g., for an enumeration, class,
  /// struct, or union).
  void ActOnTagStartDefinition(Scope *S, Decl *TagDecl);

  /// Perform ODR-like check for C/ObjC when merging tag types from modules.
  /// Differently from C++, actually parse the body and reject / error out
  /// in case of a structural mismatch.
  bool ActOnDuplicateDefinition(Decl *Prev, SkipBodyInfo &SkipBody);

  typedef void *SkippedDefinitionContext;

  /// Invoked when we enter a tag definition that we're skipping.
  SkippedDefinitionContext ActOnTagStartSkippedDefinition(Scope *S, Decl *TD);

  /// ActOnStartCXXMemberDeclarations - Invoked when we have parsed a
  /// C++ record definition's base-specifiers clause and are starting its
  /// member declarations.
  void ActOnStartCXXMemberDeclarations(Scope *S, Decl *TagDecl,
                                       SourceLocation FinalLoc,
                                       bool IsFinalSpelledSealed,
                                       bool IsAbstract,
                                       SourceLocation LBraceLoc);

  /// ActOnTagFinishDefinition - Invoked once we have finished parsing
  /// the definition of a tag (enumeration, class, struct, or union).
  void ActOnTagFinishDefinition(Scope *S, Decl *TagDecl,
                                SourceRange BraceRange);

  void ActOnTagFinishSkippedDefinition(SkippedDefinitionContext Context);

  /// ActOnTagDefinitionError - Invoked when there was an unrecoverable
  /// error parsing the definition of a tag.
  void ActOnTagDefinitionError(Scope *S, Decl *TagDecl);

  EnumConstantDecl *CheckEnumConstant(EnumDecl *Enum,
                                      EnumConstantDecl *LastEnumConst,
                                      SourceLocation IdLoc, IdentifierInfo *Id,
                                      Expr *val);

  /// Check that this is a valid underlying type for an enum declaration.
  bool CheckEnumUnderlyingType(TypeSourceInfo *TI);

  /// Check whether this is a valid redeclaration of a previous enumeration.
  /// \return true if the redeclaration was invalid.
  bool CheckEnumRedeclaration(SourceLocation EnumLoc, bool IsScoped,
                              QualType EnumUnderlyingTy, bool IsFixed,
                              const EnumDecl *Prev);

  /// Determine whether the body of an anonymous enumeration should be skipped.
  /// \param II The name of the first enumerator.
  SkipBodyInfo shouldSkipAnonEnumBody(Scope *S, IdentifierInfo *II,
                                      SourceLocation IILoc);

  Decl *ActOnEnumConstant(Scope *S, Decl *EnumDecl, Decl *LastEnumConstant,
                          SourceLocation IdLoc, IdentifierInfo *Id,
                          const ParsedAttributesView &Attrs,
                          SourceLocation EqualLoc, Expr *Val);
  void ActOnEnumBody(SourceLocation EnumLoc, SourceRange BraceRange,
                     Decl *EnumDecl, ArrayRef<Decl *> Elements, Scope *S,
                     const ParsedAttributesView &Attr);

  /// Set the current declaration context until it gets popped.
  void PushDeclContext(Scope *S, DeclContext *DC);
  void PopDeclContext();

  /// EnterDeclaratorContext - Used when we must lookup names in the context
  /// of a declarator's nested name specifier.
  void EnterDeclaratorContext(Scope *S, DeclContext *DC);
  void ExitDeclaratorContext(Scope *S);

  /// Enter a template parameter scope, after it's been associated with a
  /// particular DeclContext. Causes lookup within the scope to chain through
  /// enclosing contexts in the correct order.
  void EnterTemplatedContext(Scope *S, DeclContext *DC);

  /// Push the parameters of D, which must be a function, into scope.
  void ActOnReenterFunctionContext(Scope *S, Decl *D);
  void ActOnExitFunctionContext();

  /// Add this decl to the scope shadowed decl chains.
  void PushOnScopeChains(NamedDecl *D, Scope *S, bool AddToContext = true);

  /// isDeclInScope - If 'Ctx' is a function/method, isDeclInScope returns true
  /// if 'D' is in Scope 'S', otherwise 'S' is ignored and isDeclInScope returns
  /// true if 'D' belongs to the given declaration context.
  ///
  /// \param AllowInlineNamespace If \c true, allow the declaration to be in the
  ///        enclosing namespace set of the context, rather than contained
  ///        directly within it.
  bool isDeclInScope(NamedDecl *D, DeclContext *Ctx, Scope *S = nullptr,
                     bool AllowInlineNamespace = false) const;

  /// Finds the scope corresponding to the given decl context, if it
  /// happens to be an enclosing scope.  Otherwise return NULL.
  static Scope *getScopeForDeclContext(Scope *S, DeclContext *DC);

  /// Subroutines of ActOnDeclarator().
  TypedefDecl *ParseTypedefDecl(Scope *S, Declarator &D, QualType T,
                                TypeSourceInfo *TInfo);
  bool isIncompatibleTypedef(const TypeDecl *Old, TypedefNameDecl *New);

  /// Describes the kind of merge to perform for availability
  /// attributes (including "deprecated", "unavailable", and "availability").
  enum AvailabilityMergeKind {
    /// Don't merge availability attributes at all.
    AMK_None,
    /// Merge availability attributes for a redeclaration, which requires
    /// an exact match.
    AMK_Redeclaration,
    /// Merge availability attributes for an override, which requires
    /// an exact match or a weakening of constraints.
    AMK_Override,
    /// Merge availability attributes for an implementation of
    /// a protocol requirement.
    AMK_ProtocolImplementation,
    /// Merge availability attributes for an implementation of
    /// an optional protocol requirement.
    AMK_OptionalProtocolImplementation
  };

  /// mergeDeclAttributes - Copy attributes from the Old decl to the New one.
  void mergeDeclAttributes(NamedDecl *New, Decl *Old,
                           AvailabilityMergeKind AMK = AMK_Redeclaration);

  /// MergeTypedefNameDecl - We just parsed a typedef 'New' which has the
  /// same name and scope as a previous declaration 'Old'.  Figure out
  /// how to resolve this situation, merging decls or emitting
  /// diagnostics as appropriate. If there was an error, set New to be invalid.
  void MergeTypedefNameDecl(Scope *S, TypedefNameDecl *New,
                            LookupResult &OldDecls);

  /// MergeFunctionDecl - We just parsed a function 'New' from
  /// declarator D which has the same name and scope as a previous
  /// declaration 'Old'.  Figure out how to resolve this situation,
  /// merging decls or emitting diagnostics as appropriate.
  ///
  /// In C++, New and Old must be declarations that are not
  /// overloaded. Use IsOverload to determine whether New and Old are
  /// overloaded, and to select the Old declaration that New should be
  /// merged with.
  ///
  /// Returns true if there was an error, false otherwise.
  bool MergeFunctionDecl(FunctionDecl *New, NamedDecl *&Old, Scope *S,
                         bool MergeTypeWithOld, bool NewDeclIsDefn);

  /// Completes the merge of two function declarations that are
  /// known to be compatible.
  ///
  /// This routine handles the merging of attributes and other
  /// properties of function declarations from the old declaration to
  /// the new declaration, once we know that New is in fact a
  /// redeclaration of Old.
  ///
  /// \returns false
  bool MergeCompatibleFunctionDecls(FunctionDecl *New, FunctionDecl *Old,
                                    Scope *S, bool MergeTypeWithOld);
  void mergeObjCMethodDecls(ObjCMethodDecl *New, ObjCMethodDecl *Old);

  /// MergeVarDecl - We just parsed a variable 'New' which has the same name
  /// and scope as a previous declaration 'Old'.  Figure out how to resolve this
  /// situation, merging decls or emitting diagnostics as appropriate.
  ///
  /// Tentative definition rules (C99 6.9.2p2) are checked by
  /// FinalizeDeclaratorGroup. Unfortunately, we can't analyze tentative
  /// definitions here, since the initializer hasn't been attached.
  void MergeVarDecl(VarDecl *New, LookupResult &Previous);

  /// MergeVarDeclTypes - We parsed a variable 'New' which has the same name and
  /// scope as a previous declaration 'Old'.  Figure out how to merge their
  /// types, emitting diagnostics as appropriate.
  ///
  /// Declarations using the auto type specifier (C++ [decl.spec.auto]) call
  /// back to here in AddInitializerToDecl. We can't check them before the
  /// initializer is attached.
  void MergeVarDeclTypes(VarDecl *New, VarDecl *Old, bool MergeTypeWithOld);

  /// We've just determined that \p Old and \p New both appear to be definitions
  /// of the same variable. Either diagnose or fix the problem.
  bool checkVarDeclRedefinition(VarDecl *OldDefn, VarDecl *NewDefn);
  void notePreviousDefinition(const NamedDecl *Old, SourceLocation New);

  /// Filters out lookup results that don't fall within the given scope
  /// as determined by isDeclInScope.
  void FilterLookupForScope(LookupResult &R, DeclContext *Ctx, Scope *S,
                            bool ConsiderLinkage, bool AllowInlineNamespace);

  /// We've determined that \p New is a redeclaration of \p Old. Check that they
  /// have compatible owning modules.
  bool CheckRedeclarationModuleOwnership(NamedDecl *New, NamedDecl *Old);

  /// [module.interface]p6:
  /// A redeclaration of an entity X is implicitly exported if X was introduced
  /// by an exported declaration; otherwise it shall not be exported.
  bool CheckRedeclarationExported(NamedDecl *New, NamedDecl *Old);

  /// A wrapper function for checking the semantic restrictions of
  /// a redeclaration within a module.
  bool CheckRedeclarationInModule(NamedDecl *New, NamedDecl *Old);

  /// Check the redefinition in C++20 Modules.
  ///
  /// [basic.def.odr]p14:
  /// For any definable item D with definitions in multiple translation units,
  /// - if D is a non-inline non-templated function or variable, or
  /// - if the definitions in different translation units do not satisfy the
  /// following requirements,
  ///   the program is ill-formed; a diagnostic is required only if the
  ///   definable item is attached to a named module and a prior definition is
  ///   reachable at the point where a later definition occurs.
  /// - Each such definition shall not be attached to a named module
  /// ([module.unit]).
  /// - Each such definition shall consist of the same sequence of tokens, ...
  /// ...
  ///
  /// Return true if the redefinition is not allowed. Return false otherwise.
  bool IsRedefinitionInModule(const NamedDecl *New, const NamedDecl *Old) const;

  bool ShouldWarnIfUnusedFileScopedDecl(const DeclaratorDecl *D) const;

  /// If it's a file scoped decl that must warn if not used, keep track
  /// of it.
  void MarkUnusedFileScopedDecl(const DeclaratorDecl *D);

  typedef llvm::function_ref<void(SourceLocation Loc, PartialDiagnostic PD)>
      DiagReceiverTy;

  void DiagnoseUnusedNestedTypedefs(const RecordDecl *D);
  void DiagnoseUnusedNestedTypedefs(const RecordDecl *D,
                                    DiagReceiverTy DiagReceiver);
  void DiagnoseUnusedDecl(const NamedDecl *ND);

  /// DiagnoseUnusedDecl - Emit warnings about declarations that are not used
  /// unless they are marked attr(unused).
  void DiagnoseUnusedDecl(const NamedDecl *ND, DiagReceiverTy DiagReceiver);

  /// If VD is set but not otherwise used, diagnose, for a parameter or a
  /// variable.
  void DiagnoseUnusedButSetDecl(const VarDecl *VD, DiagReceiverTy DiagReceiver);

  /// getNonFieldDeclScope - Retrieves the innermost scope, starting
  /// from S, where a non-field would be declared. This routine copes
  /// with the difference between C and C++ scoping rules in structs and
  /// unions. For example, the following code is well-formed in C but
  /// ill-formed in C++:
  /// @code
  /// struct S6 {
  ///   enum { BAR } e;
  /// };
  ///
  /// void test_S6() {
  ///   struct S6 a;
  ///   a.e = BAR;
  /// }
  /// @endcode
  /// For the declaration of BAR, this routine will return a different
  /// scope. The scope S will be the scope of the unnamed enumeration
  /// within S6. In C++, this routine will return the scope associated
  /// with S6, because the enumeration's scope is a transparent
  /// context but structures can contain non-field names. In C, this
  /// routine will return the translation unit scope, since the
  /// enumeration's scope is a transparent context and structures cannot
  /// contain non-field names.
  Scope *getNonFieldDeclScope(Scope *S);

  FunctionDecl *CreateBuiltin(IdentifierInfo *II, QualType Type, unsigned ID,
                              SourceLocation Loc);

  /// LazilyCreateBuiltin - The specified Builtin-ID was first used at
  /// file scope.  lazily create a decl for it. ForRedeclaration is true
  /// if we're creating this built-in in anticipation of redeclaring the
  /// built-in.
  NamedDecl *LazilyCreateBuiltin(IdentifierInfo *II, unsigned ID, Scope *S,
                                 bool ForRedeclaration, SourceLocation Loc);

  /// Get the outermost AttributedType node that sets a calling convention.
  /// Valid types should not have multiple attributes with different CCs.
  const AttributedType *getCallingConvAttributedType(QualType T) const;

  /// GetNameForDeclarator - Determine the full declaration name for the
  /// given Declarator.
  DeclarationNameInfo GetNameForDeclarator(Declarator &D);

  /// Retrieves the declaration name from a parsed unqualified-id.
  DeclarationNameInfo GetNameFromUnqualifiedId(const UnqualifiedId &Name);

  /// ParsingInitForAutoVars - a set of declarations with auto types for which
  /// we are currently parsing the initializer.
  llvm::SmallPtrSet<const Decl *, 4> ParsingInitForAutoVars;

  /// Look for a locally scoped extern "C" declaration by the given name.
  NamedDecl *findLocallyScopedExternCDecl(DeclarationName Name);

  void deduceOpenCLAddressSpace(ValueDecl *decl);

  /// Adjust the \c DeclContext for a function or variable that might be a
  /// function-local external declaration.
  static bool adjustContextForLocalExternDecl(DeclContext *&DC);

  void MarkTypoCorrectedFunctionDefinition(const NamedDecl *F);

  /// Checks if the variant/multiversion functions are compatible.
  bool areMultiversionVariantFunctionsCompatible(
      const FunctionDecl *OldFD, const FunctionDecl *NewFD,
      const PartialDiagnostic &NoProtoDiagID,
      const PartialDiagnosticAt &NoteCausedDiagIDAt,
      const PartialDiagnosticAt &NoSupportDiagIDAt,
      const PartialDiagnosticAt &DiffDiagIDAt, bool TemplatesSupported,
      bool ConstexprSupported, bool CLinkageMayDiffer);

  /// type checking declaration initializers (C99 6.7.8)
  bool CheckForConstantInitializer(
      Expr *Init, unsigned DiagID = diag::err_init_element_not_constant);

  QualType deduceVarTypeFromInitializer(VarDecl *VDecl, DeclarationName Name,
                                        QualType Type, TypeSourceInfo *TSI,
                                        SourceRange Range, bool DirectInit,
                                        Expr *Init);

  bool DeduceVariableDeclarationType(VarDecl *VDecl, bool DirectInit,
                                     Expr *Init);

  sema::LambdaScopeInfo *RebuildLambdaScopeInfo(CXXMethodDecl *CallOperator);

  // Heuristically tells if the function is `get_return_object` member of a
  // coroutine promise_type by matching the function name.
  static bool CanBeGetReturnObject(const FunctionDecl *FD);
  static bool CanBeGetReturnTypeOnAllocFailure(const FunctionDecl *FD);

  /// ImplicitlyDefineFunction - An undeclared identifier was used in a function
  /// call, forming a call to an implicitly defined function (per C99 6.5.1p2).
  NamedDecl *ImplicitlyDefineFunction(SourceLocation Loc, IdentifierInfo &II,
                                      Scope *S);

  /// If this function is a C++ replaceable global allocation function
  /// (C++2a [basic.stc.dynamic.allocation], C++2a [new.delete]),
  /// adds any function attributes that we know a priori based on the standard.
  ///
  /// We need to check for duplicate attributes both here and where user-written
  /// attributes are applied to declarations.
  void AddKnownFunctionAttributesForReplaceableGlobalAllocationFunction(
      FunctionDecl *FD);

  /// Adds any function attributes that we know a priori based on
  /// the declaration of this function.
  ///
  /// These attributes can apply both to implicitly-declared builtins
  /// (like __builtin___printf_chk) or to library-declared functions
  /// like NSLog or printf.
  ///
  /// We need to check for duplicate attributes both here and where user-written
  /// attributes are applied to declarations.
  void AddKnownFunctionAttributes(FunctionDecl *FD);

  /// VerifyBitField - verifies that a bit field expression is an ICE and has
  /// the correct width, and that the field type is valid.
  /// Returns false on success.
  ExprResult VerifyBitField(SourceLocation FieldLoc,
                            const IdentifierInfo *FieldName, QualType FieldTy,
                            bool IsMsStruct, Expr *BitWidth);

  /// IsValueInFlagEnum - Determine if a value is allowed as part of a flag
  /// enum. If AllowMask is true, then we also allow the complement of a valid
  /// value, to be used as a mask.
  bool IsValueInFlagEnum(const EnumDecl *ED, const llvm::APInt &Val,
                         bool AllowMask) const;

  /// ActOnPragmaWeakID - Called on well formed \#pragma weak ident.
  void ActOnPragmaWeakID(IdentifierInfo *WeakName, SourceLocation PragmaLoc,
                         SourceLocation WeakNameLoc);

  /// ActOnPragmaRedefineExtname - Called on well formed
  /// \#pragma redefine_extname oldname newname.
  void ActOnPragmaRedefineExtname(IdentifierInfo *WeakName,
                                  IdentifierInfo *AliasName,
                                  SourceLocation PragmaLoc,
                                  SourceLocation WeakNameLoc,
                                  SourceLocation AliasNameLoc);

  /// ActOnPragmaWeakAlias - Called on well formed \#pragma weak ident = ident.
  void ActOnPragmaWeakAlias(IdentifierInfo *WeakName, IdentifierInfo *AliasName,
                            SourceLocation PragmaLoc,
                            SourceLocation WeakNameLoc,
                            SourceLocation AliasNameLoc);

  /// Status of the function emission on the CUDA/HIP/OpenMP host/device attrs.
  enum class FunctionEmissionStatus {
    Emitted,
    CUDADiscarded,     // Discarded due to CUDA/HIP hostness
    OMPDiscarded,      // Discarded due to OpenMP hostness
    TemplateDiscarded, // Discarded due to uninstantiated templates
    Unknown,
  };
  FunctionEmissionStatus getEmissionStatus(const FunctionDecl *Decl,
                                           bool Final = false);

  // Whether the callee should be ignored in CUDA/HIP/OpenMP host/device check.
  bool shouldIgnoreInHostDeviceCheck(FunctionDecl *Callee);

private:
  /// Function or variable declarations to be checked for whether the deferred
  /// diagnostics should be emitted.
  llvm::SmallSetVector<Decl *, 4> DeclsToCheckForDeferredDiags;

  /// Map of current shadowing declarations to shadowed declarations. Warn if
  /// it looks like the user is trying to modify the shadowing declaration.
  llvm::DenseMap<const NamedDecl *, const NamedDecl *> ShadowingDecls;

  // We need this to handle
  //
  // typedef struct {
  //   void *foo() { return 0; }
  // } A;
  //
  // When we see foo we don't know if after the typedef we will get 'A' or '*A'
  // for example. If 'A', foo will have external linkage. If we have '*A',
  // foo will have no linkage. Since we can't know until we get to the end
  // of the typedef, this function finds out if D might have non-external
  // linkage. Callers should verify at the end of the TU if it D has external
  // linkage or not.
  static bool mightHaveNonExternalLinkage(const DeclaratorDecl *FD);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Declaration Attribute Handling
  /// Implementations are in SemaDeclAttr.cpp
  ///@{

public:
  /// Describes the kind of priority given to an availability attribute.
  ///
  /// The sum of priorities deteremines the final priority of the attribute.
  /// The final priority determines how the attribute will be merged.
  /// An attribute with a lower priority will always remove higher priority
  /// attributes for the specified platform when it is being applied. An
  /// attribute with a higher priority will not be applied if the declaration
  /// already has an availability attribute with a lower priority for the
  /// specified platform. The final prirority values are not expected to match
  /// the values in this enumeration, but instead should be treated as a plain
  /// integer value. This enumeration just names the priority weights that are
  /// used to calculate that final vaue.
  enum AvailabilityPriority : int {
    /// The availability attribute was specified explicitly next to the
    /// declaration.
    AP_Explicit = 0,

    /// The availability attribute was applied using '#pragma clang attribute'.
    AP_PragmaClangAttribute = 1,

    /// The availability attribute for a specific platform was inferred from
    /// an availability attribute for another platform.
    AP_InferredFromOtherPlatform = 2
  };

  /// Describes the reason a calling convention specification was ignored, used
  /// for diagnostics.
  enum class CallingConventionIgnoredReason {
    ForThisTarget = 0,
    VariadicFunction,
    ConstructorDestructor,
    BuiltinFunction
  };

  /// A helper function to provide Attribute Location for the Attr types
  /// AND the ParsedAttr.
  template <typename AttrInfo>
  static std::enable_if_t<std::is_base_of_v<Attr, AttrInfo>, SourceLocation>
  getAttrLoc(const AttrInfo &AL) {
    return AL.getLocation();
  }
  SourceLocation getAttrLoc(const ParsedAttr &AL);

  /// If Expr is a valid integer constant, get the value of the integer
  /// expression and return success or failure. May output an error.
  ///
  /// Negative argument is implicitly converted to unsigned, unless
  /// \p StrictlyUnsigned is true.
  template <typename AttrInfo>
  bool checkUInt32Argument(const AttrInfo &AI, const Expr *Expr, uint32_t &Val,
                           unsigned Idx = UINT_MAX,
                           bool StrictlyUnsigned = false) {
    std::optional<llvm::APSInt> I = llvm::APSInt(32);
    if (Expr->isTypeDependent() ||
        !(I = Expr->getIntegerConstantExpr(Context))) {
      if (Idx != UINT_MAX)
        Diag(getAttrLoc(AI), diag::err_attribute_argument_n_type)
            << &AI << Idx << AANT_ArgumentIntegerConstant
            << Expr->getSourceRange();
      else
        Diag(getAttrLoc(AI), diag::err_attribute_argument_type)
            << &AI << AANT_ArgumentIntegerConstant << Expr->getSourceRange();
      return false;
    }

    if (!I->isIntN(32)) {
      Diag(Expr->getExprLoc(), diag::err_ice_too_large)
          << toString(*I, 10, false) << 32 << /* Unsigned */ 1;
      return false;
    }

    if (StrictlyUnsigned && I->isSigned() && I->isNegative()) {
      Diag(getAttrLoc(AI), diag::err_attribute_requires_positive_integer)
          << &AI << /*non-negative*/ 1;
      return false;
    }

    Val = (uint32_t)I->getZExtValue();
    return true;
  }

  /// WeakTopLevelDecl - Translation-unit scoped declarations generated by
  /// \#pragma weak during processing of other Decls.
  /// I couldn't figure out a clean way to generate these in-line, so
  /// we store them here and handle separately -- which is a hack.
  /// It would be best to refactor this.
  SmallVector<Decl *, 2> WeakTopLevelDecl;

  /// WeakTopLevelDeclDecls - access to \#pragma weak-generated Decls
  SmallVectorImpl<Decl *> &WeakTopLevelDecls() { return WeakTopLevelDecl; }

  typedef LazyVector<TypedefNameDecl *, ExternalSemaSource,
                     &ExternalSemaSource::ReadExtVectorDecls, 2, 2>
      ExtVectorDeclsType;

  /// ExtVectorDecls - This is a list all the extended vector types. This allows
  /// us to associate a raw vector type with one of the ext_vector type names.
  /// This is only necessary for issuing pretty diagnostics.
  ExtVectorDeclsType ExtVectorDecls;

  /// Check if the argument \p E is a ASCII string literal. If not emit an error
  /// and return false, otherwise set \p Str to the value of the string literal
  /// and return true.
  bool checkStringLiteralArgumentAttr(const AttributeCommonInfo &CI,
                                      const Expr *E, StringRef &Str,
                                      SourceLocation *ArgLocation = nullptr);

  /// Check if the argument \p ArgNum of \p Attr is a ASCII string literal.
  /// If not emit an error and return false. If the argument is an identifier it
  /// will emit an error with a fixit hint and treat it as if it was a string
  /// literal.
  bool checkStringLiteralArgumentAttr(const ParsedAttr &Attr, unsigned ArgNum,
                                      StringRef &Str,
                                      SourceLocation *ArgLocation = nullptr);

  /// Determine if type T is a valid subject for a nonnull and similar
  /// attributes. By default, we look through references (the behavior used by
  /// nonnull), but if the second parameter is true, then we treat a reference
  /// type as valid.
  bool isValidPointerAttrType(QualType T, bool RefOkay = false);

  /// AddAssumeAlignedAttr - Adds an assume_aligned attribute to a particular
  /// declaration.
  void AddAssumeAlignedAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E,
                            Expr *OE);

  /// AddAllocAlignAttr - Adds an alloc_align attribute to a particular
  /// declaration.
  void AddAllocAlignAttr(Decl *D, const AttributeCommonInfo &CI,
                         Expr *ParamExpr);

  bool CheckAttrTarget(const ParsedAttr &CurrAttr);
  bool CheckAttrNoArgs(const ParsedAttr &CurrAttr);

  AvailabilityAttr *mergeAvailabilityAttr(
      NamedDecl *D, const AttributeCommonInfo &CI, IdentifierInfo *Platform,
      bool Implicit, VersionTuple Introduced, VersionTuple Deprecated,
      VersionTuple Obsoleted, bool IsUnavailable, StringRef Message,
      bool IsStrict, StringRef Replacement, AvailabilityMergeKind AMK,
      int Priority, IdentifierInfo *IIEnvironment);

  TypeVisibilityAttr *
  mergeTypeVisibilityAttr(Decl *D, const AttributeCommonInfo &CI,
                          TypeVisibilityAttr::VisibilityType Vis);
  VisibilityAttr *mergeVisibilityAttr(Decl *D, const AttributeCommonInfo &CI,
                                      VisibilityAttr::VisibilityType Vis);
  SectionAttr *mergeSectionAttr(Decl *D, const AttributeCommonInfo &CI,
                                StringRef Name);

  /// Used to implement to perform semantic checking on
  /// attribute((section("foo"))) specifiers.
  ///
  /// In this case, "foo" is passed in to be checked.  If the section
  /// specifier is invalid, return an Error that indicates the problem.
  ///
  /// This is a simple quality of implementation feature to catch errors
  /// and give good diagnostics in cases when the assembler or code generator
  /// would otherwise reject the section specifier.
  llvm::Error isValidSectionSpecifier(StringRef Str);
  bool checkSectionName(SourceLocation LiteralLoc, StringRef Str);
  CodeSegAttr *mergeCodeSegAttr(Decl *D, const AttributeCommonInfo &CI,
                                StringRef Name);

  // Check for things we'd like to warn about. Multiversioning issues are
  // handled later in the process, once we know how many exist.
  bool checkTargetAttr(SourceLocation LiteralLoc, StringRef Str);

  /// Check Target Version attrs
  bool checkTargetVersionAttr(SourceLocation Loc, Decl *D, StringRef Str);
  bool checkTargetClonesAttrString(
      SourceLocation LiteralLoc, StringRef Str, const StringLiteral *Literal,
      Decl *D, bool &HasDefault, bool &HasCommas, bool &HasNotDefault,
      SmallVectorImpl<SmallString<64>> &StringsBuffer);

  ErrorAttr *mergeErrorAttr(Decl *D, const AttributeCommonInfo &CI,
                            StringRef NewUserDiagnostic);
  FormatAttr *mergeFormatAttr(Decl *D, const AttributeCommonInfo &CI,
                              IdentifierInfo *Format, int FormatIdx,
                              int FirstArg);

  /// AddAlignedAttr - Adds an aligned attribute to a particular declaration.
  void AddAlignedAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E,
                      bool IsPackExpansion);
  void AddAlignedAttr(Decl *D, const AttributeCommonInfo &CI, TypeSourceInfo *T,
                      bool IsPackExpansion);

  /// AddAlignValueAttr - Adds an align_value attribute to a particular
  /// declaration.
  void AddAlignValueAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E);

  /// AddAnnotationAttr - Adds an annotation Annot with Args arguments to D.
  void AddAnnotationAttr(Decl *D, const AttributeCommonInfo &CI,
                         StringRef Annot, MutableArrayRef<Expr *> Args);

  bool checkMSInheritanceAttrOnDefinition(CXXRecordDecl *RD, SourceRange Range,
                                          bool BestCase,
                                          MSInheritanceModel SemanticSpelling);

  void CheckAlignasUnderalignment(Decl *D);

  /// AddModeAttr - Adds a mode attribute to a particular declaration.
  void AddModeAttr(Decl *D, const AttributeCommonInfo &CI, IdentifierInfo *Name,
                   bool InInstantiation = false);
  AlwaysInlineAttr *mergeAlwaysInlineAttr(Decl *D,
                                          const AttributeCommonInfo &CI,
                                          const IdentifierInfo *Ident);
  MinSizeAttr *mergeMinSizeAttr(Decl *D, const AttributeCommonInfo &CI);
  OptimizeNoneAttr *mergeOptimizeNoneAttr(Decl *D,
                                          const AttributeCommonInfo &CI);
  InternalLinkageAttr *mergeInternalLinkageAttr(Decl *D, const ParsedAttr &AL);
  InternalLinkageAttr *mergeInternalLinkageAttr(Decl *D,
                                                const InternalLinkageAttr &AL);

  /// Check validaty of calling convention attribute \p attr. If \p FD
  /// is not null pointer, use \p FD to determine the CUDA/HIP host/device
  /// target. Otherwise, it is specified by \p CFT.
  bool CheckCallingConvAttr(
      const ParsedAttr &attr, CallingConv &CC, const FunctionDecl *FD = nullptr,
      CUDAFunctionTarget CFT = CUDAFunctionTarget::InvalidTarget);

  /// Checks a regparm attribute, returning true if it is ill-formed and
  /// otherwise setting numParams to the appropriate value.
  bool CheckRegparmAttr(const ParsedAttr &attr, unsigned &value);

  /// Create an CUDALaunchBoundsAttr attribute.
  CUDALaunchBoundsAttr *CreateLaunchBoundsAttr(const AttributeCommonInfo &CI,
                                               Expr *MaxThreads,
                                               Expr *MinBlocks,
                                               Expr *MaxBlocks);

  /// AddLaunchBoundsAttr - Adds a launch_bounds attribute to a particular
  /// declaration.
  void AddLaunchBoundsAttr(Decl *D, const AttributeCommonInfo &CI,
                           Expr *MaxThreads, Expr *MinBlocks, Expr *MaxBlocks);

  enum class RetainOwnershipKind { NS, CF, OS };

  UuidAttr *mergeUuidAttr(Decl *D, const AttributeCommonInfo &CI,
                          StringRef UuidAsWritten, MSGuidDecl *GuidDecl);

  BTFDeclTagAttr *mergeBTFDeclTagAttr(Decl *D, const BTFDeclTagAttr &AL);

  DLLImportAttr *mergeDLLImportAttr(Decl *D, const AttributeCommonInfo &CI);
  DLLExportAttr *mergeDLLExportAttr(Decl *D, const AttributeCommonInfo &CI);
  MSInheritanceAttr *mergeMSInheritanceAttr(Decl *D,
                                            const AttributeCommonInfo &CI,
                                            bool BestCase,
                                            MSInheritanceModel Model);

  EnforceTCBAttr *mergeEnforceTCBAttr(Decl *D, const EnforceTCBAttr &AL);
  EnforceTCBLeafAttr *mergeEnforceTCBLeafAttr(Decl *D,
                                              const EnforceTCBLeafAttr &AL);

  /// Helper for delayed processing TransparentUnion or
  /// BPFPreserveAccessIndexAttr attribute.
  void ProcessDeclAttributeDelayed(Decl *D,
                                   const ParsedAttributesView &AttrList);

  // Options for ProcessDeclAttributeList().
  struct ProcessDeclAttributeOptions {
    ProcessDeclAttributeOptions()
        : IncludeCXX11Attributes(true), IgnoreTypeAttributes(false) {}

    ProcessDeclAttributeOptions WithIncludeCXX11Attributes(bool Val) {
      ProcessDeclAttributeOptions Result = *this;
      Result.IncludeCXX11Attributes = Val;
      return Result;
    }

    ProcessDeclAttributeOptions WithIgnoreTypeAttributes(bool Val) {
      ProcessDeclAttributeOptions Result = *this;
      Result.IgnoreTypeAttributes = Val;
      return Result;
    }

    // Should C++11 attributes be processed?
    bool IncludeCXX11Attributes;

    // Should any type attributes encountered be ignored?
    // If this option is false, a diagnostic will be emitted for any type
    // attributes of a kind that does not "slide" from the declaration to
    // the decl-specifier-seq.
    bool IgnoreTypeAttributes;
  };

  /// ProcessDeclAttributeList - Apply all the decl attributes in the specified
  /// attribute list to the specified decl, ignoring any type attributes.
  void ProcessDeclAttributeList(Scope *S, Decl *D,
                                const ParsedAttributesView &AttrList,
                                const ProcessDeclAttributeOptions &Options =
                                    ProcessDeclAttributeOptions());

  /// Annotation attributes are the only attributes allowed after an access
  /// specifier.
  bool ProcessAccessDeclAttributeList(AccessSpecDecl *ASDecl,
                                      const ParsedAttributesView &AttrList);

  /// checkUnusedDeclAttributes - Given a declarator which is not being
  /// used to build a declaration, complain about any decl attributes
  /// which might be lying around on it.
  void checkUnusedDeclAttributes(Declarator &D);

  /// DeclClonePragmaWeak - clone existing decl (maybe definition),
  /// \#pragma weak needs a non-definition decl and source may not have one.
  NamedDecl *DeclClonePragmaWeak(NamedDecl *ND, const IdentifierInfo *II,
                                 SourceLocation Loc);

  /// DeclApplyPragmaWeak - A declaration (maybe definition) needs \#pragma weak
  /// applied to it, possibly with an alias.
  void DeclApplyPragmaWeak(Scope *S, NamedDecl *ND, const WeakInfo &W);

  void ProcessPragmaWeak(Scope *S, Decl *D);
  // Decl attributes - this routine is the top level dispatcher.
  void ProcessDeclAttributes(Scope *S, Decl *D, const Declarator &PD);

  void PopParsingDeclaration(ParsingDeclState state, Decl *decl);

  /// Given a set of delayed diagnostics, re-emit them as if they had
  /// been delayed in the current context instead of in the given pool.
  /// Essentially, this just moves them to the current pool.
  void redelayDiagnostics(sema::DelayedDiagnosticPool &pool);

  /// Check if IdxExpr is a valid parameter index for a function or
  /// instance method D.  May output an error.
  ///
  /// \returns true if IdxExpr is a valid index.
  template <typename AttrInfo>
  bool checkFunctionOrMethodParameterIndex(const Decl *D, const AttrInfo &AI,
                                           unsigned AttrArgNum,
                                           const Expr *IdxExpr, ParamIdx &Idx,
                                           bool CanIndexImplicitThis = false) {
    assert(isFunctionOrMethodOrBlockForAttrSubject(D));

    // In C++ the implicit 'this' function parameter also counts.
    // Parameters are counted from one.
    bool HP = hasFunctionProto(D);
    bool HasImplicitThisParam = isInstanceMethod(D);
    bool IV = HP && isFunctionOrMethodVariadic(D);
    unsigned NumParams =
        (HP ? getFunctionOrMethodNumParams(D) : 0) + HasImplicitThisParam;

    std::optional<llvm::APSInt> IdxInt;
    if (IdxExpr->isTypeDependent() ||
        !(IdxInt = IdxExpr->getIntegerConstantExpr(Context))) {
      Diag(getAttrLoc(AI), diag::err_attribute_argument_n_type)
          << &AI << AttrArgNum << AANT_ArgumentIntegerConstant
          << IdxExpr->getSourceRange();
      return false;
    }

    unsigned IdxSource = IdxInt->getLimitedValue(UINT_MAX);
    if (IdxSource < 1 || (!IV && IdxSource > NumParams)) {
      Diag(getAttrLoc(AI), diag::err_attribute_argument_out_of_bounds)
          << &AI << AttrArgNum << IdxExpr->getSourceRange();
      return false;
    }
    if (HasImplicitThisParam && !CanIndexImplicitThis) {
      if (IdxSource == 1) {
        Diag(getAttrLoc(AI), diag::err_attribute_invalid_implicit_this_argument)
            << &AI << IdxExpr->getSourceRange();
        return false;
      }
    }

    Idx = ParamIdx(IdxSource, D);
    return true;
  }

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Declarations
  /// Implementations are in SemaDeclCXX.cpp
  ///@{

public:
  void CheckDelegatingCtorCycles();

  /// Called before parsing a function declarator belonging to a function
  /// declaration.
  void ActOnStartFunctionDeclarationDeclarator(Declarator &D,
                                               unsigned TemplateParameterDepth);

  /// Called after parsing a function declarator belonging to a function
  /// declaration.
  void ActOnFinishFunctionDeclarationDeclarator(Declarator &D);

  // Act on C++ namespaces
  Decl *ActOnStartNamespaceDef(Scope *S, SourceLocation InlineLoc,
                               SourceLocation NamespaceLoc,
                               SourceLocation IdentLoc, IdentifierInfo *Ident,
                               SourceLocation LBrace,
                               const ParsedAttributesView &AttrList,
                               UsingDirectiveDecl *&UsingDecl, bool IsNested);

  /// ActOnFinishNamespaceDef - This callback is called after a namespace is
  /// exited. Decl is the DeclTy returned by ActOnStartNamespaceDef.
  void ActOnFinishNamespaceDef(Decl *Dcl, SourceLocation RBrace);

  NamespaceDecl *getStdNamespace() const;

  /// Retrieve the special "std" namespace, which may require us to
  /// implicitly define the namespace.
  NamespaceDecl *getOrCreateStdNamespace();

  CXXRecordDecl *getStdBadAlloc() const;
  EnumDecl *getStdAlignValT() const;

  ValueDecl *tryLookupUnambiguousFieldDecl(RecordDecl *ClassDecl,
                                           const IdentifierInfo *MemberOrBase);

  enum class ComparisonCategoryUsage {
    /// The '<=>' operator was used in an expression and a builtin operator
    /// was selected.
    OperatorInExpression,
    /// A defaulted 'operator<=>' needed the comparison category. This
    /// typically only applies to 'std::strong_ordering', due to the implicit
    /// fallback return value.
    DefaultedOperator,
  };

  /// Lookup the specified comparison category types in the standard
  ///   library, an check the VarDecls possibly returned by the operator<=>
  ///   builtins for that type.
  ///
  /// \return The type of the comparison category type corresponding to the
  ///   specified Kind, or a null type if an error occurs
  QualType CheckComparisonCategoryType(ComparisonCategoryType Kind,
                                       SourceLocation Loc,
                                       ComparisonCategoryUsage Usage);

  /// Tests whether Ty is an instance of std::initializer_list and, if
  /// it is and Element is not NULL, assigns the element type to Element.
  bool isStdInitializerList(QualType Ty, QualType *Element);

  /// Looks for the std::initializer_list template and instantiates it
  /// with Element, or emits an error if it's not found.
  ///
  /// \returns The instantiated template, or null on error.
  QualType BuildStdInitializerList(QualType Element, SourceLocation Loc);

  /// Determine whether Ctor is an initializer-list constructor, as
  /// defined in [dcl.init.list]p2.
  bool isInitListConstructor(const FunctionDecl *Ctor);

  Decl *ActOnUsingDirective(Scope *CurScope, SourceLocation UsingLoc,
                            SourceLocation NamespcLoc, CXXScopeSpec &SS,
                            SourceLocation IdentLoc,
                            IdentifierInfo *NamespcName,
                            const ParsedAttributesView &AttrList);

  void PushUsingDirective(Scope *S, UsingDirectiveDecl *UDir);

  Decl *ActOnNamespaceAliasDef(Scope *CurScope, SourceLocation NamespaceLoc,
                               SourceLocation AliasLoc, IdentifierInfo *Alias,
                               CXXScopeSpec &SS, SourceLocation IdentLoc,
                               IdentifierInfo *Ident);

  /// Remove decls we can't actually see from a lookup being used to declare
  /// shadow using decls.
  ///
  /// \param S - The scope of the potential shadow decl
  /// \param Previous - The lookup of a potential shadow decl's name.
  void FilterUsingLookup(Scope *S, LookupResult &lookup);

  /// Hides a using shadow declaration.  This is required by the current
  /// using-decl implementation when a resolvable using declaration in a
  /// class is followed by a declaration which would hide or override
  /// one or more of the using decl's targets; for example:
  ///
  ///   struct Base { void foo(int); };
  ///   struct Derived : Base {
  ///     using Base::foo;
  ///     void foo(int);
  ///   };
  ///
  /// The governing language is C++03 [namespace.udecl]p12:
  ///
  ///   When a using-declaration brings names from a base class into a
  ///   derived class scope, member functions in the derived class
  ///   override and/or hide member functions with the same name and
  ///   parameter types in a base class (rather than conflicting).
  ///
  /// There are two ways to implement this:
  ///   (1) optimistically create shadow decls when they're not hidden
  ///       by existing declarations, or
  ///   (2) don't create any shadow decls (or at least don't make them
  ///       visible) until we've fully parsed/instantiated the class.
  /// The problem with (1) is that we might have to retroactively remove
  /// a shadow decl, which requires several O(n) operations because the
  /// decl structures are (very reasonably) not designed for removal.
  /// (2) avoids this but is very fiddly and phase-dependent.
  void HideUsingShadowDecl(Scope *S, UsingShadowDecl *Shadow);

  /// Determines whether to create a using shadow decl for a particular
  /// decl, given the set of decls existing prior to this using lookup.
  bool CheckUsingShadowDecl(BaseUsingDecl *BUD, NamedDecl *Target,
                            const LookupResult &PreviousDecls,
                            UsingShadowDecl *&PrevShadow);

  /// Builds a shadow declaration corresponding to a 'using' declaration.
  UsingShadowDecl *BuildUsingShadowDecl(Scope *S, BaseUsingDecl *BUD,
                                        NamedDecl *Target,
                                        UsingShadowDecl *PrevDecl);

  /// Checks that the given using declaration is not an invalid
  /// redeclaration.  Note that this is checking only for the using decl
  /// itself, not for any ill-formedness among the UsingShadowDecls.
  bool CheckUsingDeclRedeclaration(SourceLocation UsingLoc,
                                   bool HasTypenameKeyword,
                                   const CXXScopeSpec &SS,
                                   SourceLocation NameLoc,
                                   const LookupResult &Previous);

  /// Checks that the given nested-name qualifier used in a using decl
  /// in the current context is appropriately related to the current
  /// scope.  If an error is found, diagnoses it and returns true.
  /// R is nullptr, if the caller has not (yet) done a lookup, otherwise it's
  /// the result of that lookup. UD is likewise nullptr, except when we have an
  /// already-populated UsingDecl whose shadow decls contain the same
  /// information (i.e. we're instantiating a UsingDecl with non-dependent
  /// scope).
  bool CheckUsingDeclQualifier(SourceLocation UsingLoc, bool HasTypename,
                               const CXXScopeSpec &SS,
                               const DeclarationNameInfo &NameInfo,
                               SourceLocation NameLoc,
                               const LookupResult *R = nullptr,
                               const UsingDecl *UD = nullptr);

  /// Builds a using declaration.
  ///
  /// \param IsInstantiation - Whether this call arises from an
  ///   instantiation of an unresolved using declaration.  We treat
  ///   the lookup differently for these declarations.
  NamedDecl *BuildUsingDeclaration(Scope *S, AccessSpecifier AS,
                                   SourceLocation UsingLoc,
                                   bool HasTypenameKeyword,
                                   SourceLocation TypenameLoc, CXXScopeSpec &SS,
                                   DeclarationNameInfo NameInfo,
                                   SourceLocation EllipsisLoc,
                                   const ParsedAttributesView &AttrList,
                                   bool IsInstantiation, bool IsUsingIfExists);
  NamedDecl *BuildUsingEnumDeclaration(Scope *S, AccessSpecifier AS,
                                       SourceLocation UsingLoc,
                                       SourceLocation EnumLoc,
                                       SourceLocation NameLoc,
                                       TypeSourceInfo *EnumType, EnumDecl *ED);
  NamedDecl *BuildUsingPackDecl(NamedDecl *InstantiatedFrom,
                                ArrayRef<NamedDecl *> Expansions);

  /// Additional checks for a using declaration referring to a constructor name.
  bool CheckInheritingConstructorUsingDecl(UsingDecl *UD);

  /// Given a derived-class using shadow declaration for a constructor and the
  /// correspnding base class constructor, find or create the implicit
  /// synthesized derived class constructor to use for this initialization.
  CXXConstructorDecl *
  findInheritingConstructor(SourceLocation Loc, CXXConstructorDecl *BaseCtor,
                            ConstructorUsingShadowDecl *DerivedShadow);

  Decl *ActOnUsingDeclaration(Scope *CurScope, AccessSpecifier AS,
                              SourceLocation UsingLoc,
                              SourceLocation TypenameLoc, CXXScopeSpec &SS,
                              UnqualifiedId &Name, SourceLocation EllipsisLoc,
                              const ParsedAttributesView &AttrList);
  Decl *ActOnUsingEnumDeclaration(Scope *CurScope, AccessSpecifier AS,
                                  SourceLocation UsingLoc,
                                  SourceLocation EnumLoc, SourceRange TyLoc,
                                  const IdentifierInfo &II, ParsedType Ty,
                                  CXXScopeSpec *SS = nullptr);
  Decl *ActOnAliasDeclaration(Scope *CurScope, AccessSpecifier AS,
                              MultiTemplateParamsArg TemplateParams,
                              SourceLocation UsingLoc, UnqualifiedId &Name,
                              const ParsedAttributesView &AttrList,
                              TypeResult Type, Decl *DeclFromDeclSpec);

  /// BuildCXXConstructExpr - Creates a complete call to a constructor,
  /// including handling of its default argument expressions.
  ///
  /// \param ConstructKind - a CXXConstructExpr::ConstructionKind
  ExprResult BuildCXXConstructExpr(
      SourceLocation ConstructLoc, QualType DeclInitType, NamedDecl *FoundDecl,
      CXXConstructorDecl *Constructor, MultiExprArg Exprs,
      bool HadMultipleCandidates, bool IsListInitialization,
      bool IsStdInitListInitialization, bool RequiresZeroInit,
      CXXConstructionKind ConstructKind, SourceRange ParenRange);

  /// Build a CXXConstructExpr whose constructor has already been resolved if
  /// it denotes an inherited constructor.
  ExprResult BuildCXXConstructExpr(
      SourceLocation ConstructLoc, QualType DeclInitType,
      CXXConstructorDecl *Constructor, bool Elidable, MultiExprArg Exprs,
      bool HadMultipleCandidates, bool IsListInitialization,
      bool IsStdInitListInitialization, bool RequiresZeroInit,
      CXXConstructionKind ConstructKind, SourceRange ParenRange);

  // FIXME: Can we remove this and have the above BuildCXXConstructExpr check if
  // the constructor can be elidable?
  ExprResult BuildCXXConstructExpr(
      SourceLocation ConstructLoc, QualType DeclInitType, NamedDecl *FoundDecl,
      CXXConstructorDecl *Constructor, bool Elidable, MultiExprArg Exprs,
      bool HadMultipleCandidates, bool IsListInitialization,
      bool IsStdInitListInitialization, bool RequiresZeroInit,
      CXXConstructionKind ConstructKind, SourceRange ParenRange);

  ExprResult ConvertMemberDefaultInitExpression(FieldDecl *FD, Expr *InitExpr,
                                                SourceLocation InitLoc);

  /// FinalizeVarWithDestructor - Prepare for calling destructor on the
  /// constructed variable.
  void FinalizeVarWithDestructor(VarDecl *VD, const RecordType *DeclInitType);

  /// Helper class that collects exception specifications for
  /// implicitly-declared special member functions.
  class ImplicitExceptionSpecification {
    // Pointer to allow copying
    Sema *Self;
    // We order exception specifications thus:
    // noexcept is the most restrictive, but is only used in C++11.
    // throw() comes next.
    // Then a throw(collected exceptions)
    // Finally no specification, which is expressed as noexcept(false).
    // throw(...) is used instead if any called function uses it.
    ExceptionSpecificationType ComputedEST;
    llvm::SmallPtrSet<CanQualType, 4> ExceptionsSeen;
    SmallVector<QualType, 4> Exceptions;

    void ClearExceptions() {
      ExceptionsSeen.clear();
      Exceptions.clear();
    }

  public:
    explicit ImplicitExceptionSpecification(Sema &Self)
        : Self(&Self), ComputedEST(EST_BasicNoexcept) {
      if (!Self.getLangOpts().CPlusPlus11)
        ComputedEST = EST_DynamicNone;
    }

    /// Get the computed exception specification type.
    ExceptionSpecificationType getExceptionSpecType() const {
      assert(!isComputedNoexcept(ComputedEST) &&
             "noexcept(expr) should not be a possible result");
      return ComputedEST;
    }

    /// The number of exceptions in the exception specification.
    unsigned size() const { return Exceptions.size(); }

    /// The set of exceptions in the exception specification.
    const QualType *data() const { return Exceptions.data(); }

    /// Integrate another called method into the collected data.
    void CalledDecl(SourceLocation CallLoc, const CXXMethodDecl *Method);

    /// Integrate an invoked expression into the collected data.
    void CalledExpr(Expr *E) { CalledStmt(E); }

    /// Integrate an invoked statement into the collected data.
    void CalledStmt(Stmt *S);

    /// Overwrite an EPI's exception specification with this
    /// computed exception specification.
    FunctionProtoType::ExceptionSpecInfo getExceptionSpec() const {
      FunctionProtoType::ExceptionSpecInfo ESI;
      ESI.Type = getExceptionSpecType();
      if (ESI.Type == EST_Dynamic) {
        ESI.Exceptions = Exceptions;
      } else if (ESI.Type == EST_None) {
        /// C++11 [except.spec]p14:
        ///   The exception-specification is noexcept(false) if the set of
        ///   potential exceptions of the special member function contains "any"
        ESI.Type = EST_NoexceptFalse;
        ESI.NoexceptExpr =
            Self->ActOnCXXBoolLiteral(SourceLocation(), tok::kw_false).get();
      }
      return ESI;
    }
  };

  /// Evaluate the implicit exception specification for a defaulted
  /// special member function.
  void EvaluateImplicitExceptionSpec(SourceLocation Loc, FunctionDecl *FD);

  /// Check the given exception-specification and update the
  /// exception specification information with the results.
  void checkExceptionSpecification(bool IsTopLevel,
                                   ExceptionSpecificationType EST,
                                   ArrayRef<ParsedType> DynamicExceptions,
                                   ArrayRef<SourceRange> DynamicExceptionRanges,
                                   Expr *NoexceptExpr,
                                   SmallVectorImpl<QualType> &Exceptions,
                                   FunctionProtoType::ExceptionSpecInfo &ESI);

  /// Add an exception-specification to the given member or friend function
  /// (or function template). The exception-specification was parsed
  /// after the function itself was declared.
  void actOnDelayedExceptionSpecification(
      Decl *D, ExceptionSpecificationType EST, SourceRange SpecificationRange,
      ArrayRef<ParsedType> DynamicExceptions,
      ArrayRef<SourceRange> DynamicExceptionRanges, Expr *NoexceptExpr);

  class InheritedConstructorInfo;

  /// Determine if a special member function should have a deleted
  /// definition when it is defaulted.
  bool ShouldDeleteSpecialMember(CXXMethodDecl *MD, CXXSpecialMemberKind CSM,
                                 InheritedConstructorInfo *ICI = nullptr,
                                 bool Diagnose = false);

  /// Produce notes explaining why a defaulted function was defined as deleted.
  void DiagnoseDeletedDefaultedFunction(FunctionDecl *FD);

  /// Declare the implicit default constructor for the given class.
  ///
  /// \param ClassDecl The class declaration into which the implicit
  /// default constructor will be added.
  ///
  /// \returns The implicitly-declared default constructor.
  CXXConstructorDecl *
  DeclareImplicitDefaultConstructor(CXXRecordDecl *ClassDecl);

  /// DefineImplicitDefaultConstructor - Checks for feasibility of
  /// defining this constructor as the default constructor.
  void DefineImplicitDefaultConstructor(SourceLocation CurrentLocation,
                                        CXXConstructorDecl *Constructor);

  /// Declare the implicit destructor for the given class.
  ///
  /// \param ClassDecl The class declaration into which the implicit
  /// destructor will be added.
  ///
  /// \returns The implicitly-declared destructor.
  CXXDestructorDecl *DeclareImplicitDestructor(CXXRecordDecl *ClassDecl);

  /// DefineImplicitDestructor - Checks for feasibility of
  /// defining this destructor as the default destructor.
  void DefineImplicitDestructor(SourceLocation CurrentLocation,
                                CXXDestructorDecl *Destructor);

  /// Build an exception spec for destructors that don't have one.
  ///
  /// C++11 says that user-defined destructors with no exception spec get one
  /// that looks as if the destructor was implicitly declared.
  void AdjustDestructorExceptionSpec(CXXDestructorDecl *Destructor);

  /// Define the specified inheriting constructor.
  void DefineInheritingConstructor(SourceLocation UseLoc,
                                   CXXConstructorDecl *Constructor);

  /// Declare the implicit copy constructor for the given class.
  ///
  /// \param ClassDecl The class declaration into which the implicit
  /// copy constructor will be added.
  ///
  /// \returns The implicitly-declared copy constructor.
  CXXConstructorDecl *DeclareImplicitCopyConstructor(CXXRecordDecl *ClassDecl);

  /// DefineImplicitCopyConstructor - Checks for feasibility of
  /// defining this constructor as the copy constructor.
  void DefineImplicitCopyConstructor(SourceLocation CurrentLocation,
                                     CXXConstructorDecl *Constructor);

  /// Declare the implicit move constructor for the given class.
  ///
  /// \param ClassDecl The Class declaration into which the implicit
  /// move constructor will be added.
  ///
  /// \returns The implicitly-declared move constructor, or NULL if it wasn't
  /// declared.
  CXXConstructorDecl *DeclareImplicitMoveConstructor(CXXRecordDecl *ClassDecl);

  /// DefineImplicitMoveConstructor - Checks for feasibility of
  /// defining this constructor as the move constructor.
  void DefineImplicitMoveConstructor(SourceLocation CurrentLocation,
                                     CXXConstructorDecl *Constructor);

  /// Declare the implicit copy assignment operator for the given class.
  ///
  /// \param ClassDecl The class declaration into which the implicit
  /// copy assignment operator will be added.
  ///
  /// \returns The implicitly-declared copy assignment operator.
  CXXMethodDecl *DeclareImplicitCopyAssignment(CXXRecordDecl *ClassDecl);

  /// Defines an implicitly-declared copy assignment operator.
  void DefineImplicitCopyAssignment(SourceLocation CurrentLocation,
                                    CXXMethodDecl *MethodDecl);

  /// Declare the implicit move assignment operator for the given class.
  ///
  /// \param ClassDecl The Class declaration into which the implicit
  /// move assignment operator will be added.
  ///
  /// \returns The implicitly-declared move assignment operator, or NULL if it
  /// wasn't declared.
  CXXMethodDecl *DeclareImplicitMoveAssignment(CXXRecordDecl *ClassDecl);

  /// Defines an implicitly-declared move assignment operator.
  void DefineImplicitMoveAssignment(SourceLocation CurrentLocation,
                                    CXXMethodDecl *MethodDecl);

  /// Check a completed declaration of an implicit special member.
  void CheckImplicitSpecialMemberDeclaration(Scope *S, FunctionDecl *FD);

  /// Determine whether the given function is an implicitly-deleted
  /// special member function.
  bool isImplicitlyDeleted(FunctionDecl *FD);

  /// Check whether 'this' shows up in the type of a static member
  /// function after the (naturally empty) cv-qualifier-seq would be.
  ///
  /// \returns true if an error occurred.
  bool checkThisInStaticMemberFunctionType(CXXMethodDecl *Method);

  /// Whether this' shows up in the exception specification of a static
  /// member function.
  bool checkThisInStaticMemberFunctionExceptionSpec(CXXMethodDecl *Method);

  /// Check whether 'this' shows up in the attributes of the given
  /// static member function.
  ///
  /// \returns true if an error occurred.
  bool checkThisInStaticMemberFunctionAttributes(CXXMethodDecl *Method);

  bool CheckImmediateEscalatingFunctionDefinition(
      FunctionDecl *FD, const sema::FunctionScopeInfo *FSI);

  void DiagnoseImmediateEscalatingReason(FunctionDecl *FD);

  /// Given a constructor and the set of arguments provided for the
  /// constructor, convert the arguments and add any required default arguments
  /// to form a proper call to this constructor.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool CompleteConstructorCall(CXXConstructorDecl *Constructor,
                               QualType DeclInitType, MultiExprArg ArgsPtr,
                               SourceLocation Loc,
                               SmallVectorImpl<Expr *> &ConvertedArgs,
                               bool AllowExplicit = false,
                               bool IsListInitialization = false);

  /// ActOnCXXEnterDeclInitializer - Invoked when we are about to parse an
  /// initializer for the declaration 'Dcl'.
  /// After this method is called, according to [C++ 3.4.1p13], if 'Dcl' is a
  /// static data member of class X, names should be looked up in the scope of
  /// class X.
  void ActOnCXXEnterDeclInitializer(Scope *S, Decl *Dcl);

  /// ActOnCXXExitDeclInitializer - Invoked after we are finished parsing an
  /// initializer for the declaration 'Dcl'.
  void ActOnCXXExitDeclInitializer(Scope *S, Decl *Dcl);

  /// Define the "body" of the conversion from a lambda object to a
  /// function pointer.
  ///
  /// This routine doesn't actually define a sensible body; rather, it fills
  /// in the initialization expression needed to copy the lambda object into
  /// the block, and IR generation actually generates the real body of the
  /// block pointer conversion.
  void
  DefineImplicitLambdaToFunctionPointerConversion(SourceLocation CurrentLoc,
                                                  CXXConversionDecl *Conv);

  /// Define the "body" of the conversion from a lambda object to a
  /// block pointer.
  ///
  /// This routine doesn't actually define a sensible body; rather, it fills
  /// in the initialization expression needed to copy the lambda object into
  /// the block, and IR generation actually generates the real body of the
  /// block pointer conversion.
  void DefineImplicitLambdaToBlockPointerConversion(SourceLocation CurrentLoc,
                                                    CXXConversionDecl *Conv);

  /// ActOnStartLinkageSpecification - Parsed the beginning of a C++
  /// linkage specification, including the language and (if present)
  /// the '{'. ExternLoc is the location of the 'extern', Lang is the
  /// language string literal. LBraceLoc, if valid, provides the location of
  /// the '{' brace. Otherwise, this linkage specification does not
  /// have any braces.
  Decl *ActOnStartLinkageSpecification(Scope *S, SourceLocation ExternLoc,
                                       Expr *LangStr, SourceLocation LBraceLoc);

  /// ActOnFinishLinkageSpecification - Complete the definition of
  /// the C++ linkage specification LinkageSpec. If RBraceLoc is
  /// valid, it's the position of the closing '}' brace in a linkage
  /// specification that uses braces.
  Decl *ActOnFinishLinkageSpecification(Scope *S, Decl *LinkageSpec,
                                        SourceLocation RBraceLoc);

  //===--------------------------------------------------------------------===//
  // C++ Classes
  //

  /// Get the class that is directly named by the current context. This is the
  /// class for which an unqualified-id in this scope could name a constructor
  /// or destructor.
  ///
  /// If the scope specifier denotes a class, this will be that class.
  /// If the scope specifier is empty, this will be the class whose
  /// member-specification we are currently within. Otherwise, there
  /// is no such class.
  CXXRecordDecl *getCurrentClass(Scope *S, const CXXScopeSpec *SS);

  /// isCurrentClassName - Determine whether the identifier II is the
  /// name of the class type currently being defined. In the case of
  /// nested classes, this will only return true if II is the name of
  /// the innermost class.
  bool isCurrentClassName(const IdentifierInfo &II, Scope *S,
                          const CXXScopeSpec *SS = nullptr);

  /// Determine whether the identifier II is a typo for the name of
  /// the class type currently being defined. If so, update it to the identifier
  /// that should have been used.
  bool isCurrentClassNameTypo(IdentifierInfo *&II, const CXXScopeSpec *SS);

  /// ActOnAccessSpecifier - Parsed an access specifier followed by a colon.
  bool ActOnAccessSpecifier(AccessSpecifier Access, SourceLocation ASLoc,
                            SourceLocation ColonLoc,
                            const ParsedAttributesView &Attrs);

  /// ActOnCXXMemberDeclarator - This is invoked when a C++ class member
  /// declarator is parsed. 'AS' is the access specifier, 'BW' specifies the
  /// bitfield width if there is one, 'InitExpr' specifies the initializer if
  /// one has been parsed, and 'InitStyle' is set if an in-class initializer is
  /// present (but parsing it has been deferred).
  NamedDecl *
  ActOnCXXMemberDeclarator(Scope *S, AccessSpecifier AS, Declarator &D,
                           MultiTemplateParamsArg TemplateParameterLists,
                           Expr *BitfieldWidth, const VirtSpecifiers &VS,
                           InClassInitStyle InitStyle);

  /// Enter a new C++ default initializer scope. After calling this, the
  /// caller must call \ref ActOnFinishCXXInClassMemberInitializer, even if
  /// parsing or instantiating the initializer failed.
  void ActOnStartCXXInClassMemberInitializer();

  /// This is invoked after parsing an in-class initializer for a
  /// non-static C++ class member, and after instantiating an in-class
  /// initializer in a class template. Such actions are deferred until the class
  /// is complete.
  void ActOnFinishCXXInClassMemberInitializer(Decl *VarDecl,
                                              SourceLocation EqualLoc,
                                              Expr *Init);

  /// Handle a C++ member initializer using parentheses syntax.
  MemInitResult
  ActOnMemInitializer(Decl *ConstructorD, Scope *S, CXXScopeSpec &SS,
                      IdentifierInfo *MemberOrBase, ParsedType TemplateTypeTy,
                      const DeclSpec &DS, SourceLocation IdLoc,
                      SourceLocation LParenLoc, ArrayRef<Expr *> Args,
                      SourceLocation RParenLoc, SourceLocation EllipsisLoc);

  /// Handle a C++ member initializer using braced-init-list syntax.
  MemInitResult ActOnMemInitializer(Decl *ConstructorD, Scope *S,
                                    CXXScopeSpec &SS,
                                    IdentifierInfo *MemberOrBase,
                                    ParsedType TemplateTypeTy,
                                    const DeclSpec &DS, SourceLocation IdLoc,
                                    Expr *InitList, SourceLocation EllipsisLoc);

  /// Handle a C++ member initializer.
  MemInitResult BuildMemInitializer(Decl *ConstructorD, Scope *S,
                                    CXXScopeSpec &SS,
                                    IdentifierInfo *MemberOrBase,
                                    ParsedType TemplateTypeTy,
                                    const DeclSpec &DS, SourceLocation IdLoc,
                                    Expr *Init, SourceLocation EllipsisLoc);

  MemInitResult BuildMemberInitializer(ValueDecl *Member, Expr *Init,
                                       SourceLocation IdLoc);

  MemInitResult BuildBaseInitializer(QualType BaseType,
                                     TypeSourceInfo *BaseTInfo, Expr *Init,
                                     CXXRecordDecl *ClassDecl,
                                     SourceLocation EllipsisLoc);

  MemInitResult BuildDelegatingInitializer(TypeSourceInfo *TInfo, Expr *Init,
                                           CXXRecordDecl *ClassDecl);

  bool SetDelegatingInitializer(CXXConstructorDecl *Constructor,
                                CXXCtorInitializer *Initializer);

  bool SetCtorInitializers(
      CXXConstructorDecl *Constructor, bool AnyErrors,
      ArrayRef<CXXCtorInitializer *> Initializers = std::nullopt);

  /// MarkBaseAndMemberDestructorsReferenced - Given a record decl,
  /// mark all the non-trivial destructors of its members and bases as
  /// referenced.
  void MarkBaseAndMemberDestructorsReferenced(SourceLocation Loc,
                                              CXXRecordDecl *Record);

  /// Mark destructors of virtual bases of this class referenced. In the Itanium
  /// C++ ABI, this is done when emitting a destructor for any non-abstract
  /// class. In the Microsoft C++ ABI, this is done any time a class's
  /// destructor is referenced.
  void MarkVirtualBaseDestructorsReferenced(
      SourceLocation Location, CXXRecordDecl *ClassDecl,
      llvm::SmallPtrSetImpl<const RecordType *> *DirectVirtualBases = nullptr);

  /// Do semantic checks to allow the complete destructor variant to be emitted
  /// when the destructor is defined in another translation unit. In the Itanium
  /// C++ ABI, destructor variants are emitted together. In the MS C++ ABI, they
  /// can be emitted in separate TUs. To emit the complete variant, run a subset
  /// of the checks performed when emitting a regular destructor.
  void CheckCompleteDestructorVariant(SourceLocation CurrentLocation,
                                      CXXDestructorDecl *Dtor);

  /// The list of classes whose vtables have been used within
  /// this translation unit, and the source locations at which the
  /// first use occurred.
  typedef std::pair<CXXRecordDecl *, SourceLocation> VTableUse;

  /// The list of vtables that are required but have not yet been
  /// materialized.
  SmallVector<VTableUse, 16> VTableUses;

  /// The set of classes whose vtables have been used within
  /// this translation unit, and a bit that will be true if the vtable is
  /// required to be emitted (otherwise, it should be emitted only if needed
  /// by code generation).
  llvm::DenseMap<CXXRecordDecl *, bool> VTablesUsed;

  /// Load any externally-stored vtable uses.
  void LoadExternalVTableUses();

  /// Note that the vtable for the given class was used at the
  /// given location.
  void MarkVTableUsed(SourceLocation Loc, CXXRecordDecl *Class,
                      bool DefinitionRequired = false);

  /// Mark the exception specifications of all virtual member functions
  /// in the given class as needed.
  void MarkVirtualMemberExceptionSpecsNeeded(SourceLocation Loc,
                                             const CXXRecordDecl *RD);

  /// MarkVirtualMembersReferenced - Will mark all members of the given
  /// CXXRecordDecl referenced.
  void MarkVirtualMembersReferenced(SourceLocation Loc, const CXXRecordDecl *RD,
                                    bool ConstexprOnly = false);

  /// Define all of the vtables that have been used in this
  /// translation unit and reference any virtual members used by those
  /// vtables.
  ///
  /// \returns true if any work was done, false otherwise.
  bool DefineUsedVTables();

  /// AddImplicitlyDeclaredMembersToClass - Adds any implicitly-declared
  /// special functions, such as the default constructor, copy
  /// constructor, or destructor, to the given C++ class (C++
  /// [special]p1).  This routine can only be executed just before the
  /// definition of the class is complete.
  void AddImplicitlyDeclaredMembersToClass(CXXRecordDecl *ClassDecl);

  /// ActOnMemInitializers - Handle the member initializers for a constructor.
  void ActOnMemInitializers(Decl *ConstructorDecl, SourceLocation ColonLoc,
                            ArrayRef<CXXCtorInitializer *> MemInits,
                            bool AnyErrors);

  /// Check class-level dllimport/dllexport attribute. The caller must
  /// ensure that referenceDLLExportedClassMethods is called some point later
  /// when all outer classes of Class are complete.
  void checkClassLevelDLLAttribute(CXXRecordDecl *Class);
  void checkClassLevelCodeSegAttribute(CXXRecordDecl *Class);

  void referenceDLLExportedClassMethods();

  /// Perform propagation of DLL attributes from a derived class to a
  /// templated base class for MS compatibility.
  void propagateDLLAttrToBaseClassTemplate(
      CXXRecordDecl *Class, Attr *ClassAttr,
      ClassTemplateSpecializationDecl *BaseTemplateSpec,
      SourceLocation BaseLoc);

  /// Perform semantic checks on a class definition that has been
  /// completing, introducing implicitly-declared members, checking for
  /// abstract types, etc.
  ///
  /// \param S The scope in which the class was parsed. Null if we didn't just
  ///        parse a class definition.
  /// \param Record The completed class.
  void CheckCompletedCXXClass(Scope *S, CXXRecordDecl *Record);

  /// Check that the C++ class annoated with "trivial_abi" satisfies all the
  /// conditions that are needed for the attribute to have an effect.
  void checkIllFormedTrivialABIStruct(CXXRecordDecl &RD);

  /// Check that VTable Pointer authentication is only being set on the first
  /// first instantiation of the vtable
  void checkIncorrectVTablePointerAuthenticationAttribute(CXXRecordDecl &RD);

  void ActOnFinishCXXMemberSpecification(Scope *S, SourceLocation RLoc,
                                         Decl *TagDecl, SourceLocation LBrac,
                                         SourceLocation RBrac,
                                         const ParsedAttributesView &AttrList);

  /// Perform any semantic analysis which needs to be delayed until all
  /// pending class member declarations have been parsed.
  void ActOnFinishCXXMemberDecls();
  void ActOnFinishCXXNonNestedClass();

  /// This is used to implement the constant expression evaluation part of the
  /// attribute enable_if extension. There is nothing in standard C++ which
  /// would require reentering parameters.
  void ActOnReenterCXXMethodParameter(Scope *S, ParmVarDecl *Param);
  unsigned ActOnReenterTemplateScope(Decl *Template,
                                     llvm::function_ref<Scope *()> EnterScope);
  void ActOnStartDelayedMemberDeclarations(Scope *S, Decl *Record);

  /// ActOnStartDelayedCXXMethodDeclaration - We have completed
  /// parsing a top-level (non-nested) C++ class, and we are now
  /// parsing those parts of the given Method declaration that could
  /// not be parsed earlier (C++ [class.mem]p2), such as default
  /// arguments. This action should enter the scope of the given
  /// Method declaration as if we had just parsed the qualified method
  /// name. However, it should not bring the parameters into scope;
  /// that will be performed by ActOnDelayedCXXMethodParameter.
  void ActOnStartDelayedCXXMethodDeclaration(Scope *S, Decl *Method);
  void ActOnDelayedCXXMethodParameter(Scope *S, Decl *Param);
  void ActOnFinishDelayedMemberDeclarations(Scope *S, Decl *Record);

  /// ActOnFinishDelayedCXXMethodDeclaration - We have finished
  /// processing the delayed method declaration for Method. The method
  /// declaration is now considered finished. There may be a separate
  /// ActOnStartOfFunctionDef action later (not necessarily
  /// immediately!) for this method, if it was also defined inside the
  /// class body.
  void ActOnFinishDelayedCXXMethodDeclaration(Scope *S, Decl *Method);
  void ActOnFinishDelayedMemberInitializers(Decl *Record);

  bool EvaluateStaticAssertMessageAsString(Expr *Message, std::string &Result,
                                           ASTContext &Ctx,
                                           bool ErrorOnInvalidMessage);
  Decl *ActOnStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                     Expr *AssertExpr, Expr *AssertMessageExpr,
                                     SourceLocation RParenLoc);
  Decl *BuildStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                     Expr *AssertExpr, Expr *AssertMessageExpr,
                                     SourceLocation RParenLoc, bool Failed);

  /// Try to print more useful information about a failed static_assert
  /// with expression \E
  void DiagnoseStaticAssertDetails(const Expr *E);

  /// Handle a friend type declaration.  This works in tandem with
  /// ActOnTag.
  ///
  /// Notes on friend class templates:
  ///
  /// We generally treat friend class declarations as if they were
  /// declaring a class.  So, for example, the elaborated type specifier
  /// in a friend declaration is required to obey the restrictions of a
  /// class-head (i.e. no typedefs in the scope chain), template
  /// parameters are required to match up with simple template-ids, &c.
  /// However, unlike when declaring a template specialization, it's
  /// okay to refer to a template specialization without an empty
  /// template parameter declaration, e.g.
  ///   friend class A<T>::B<unsigned>;
  /// We permit this as a special case; if there are any template
  /// parameters present at all, require proper matching, i.e.
  ///   template <> template \<class T> friend class A<int>::B;
  Decl *ActOnFriendTypeDecl(Scope *S, const DeclSpec &DS,
                            MultiTemplateParamsArg TemplateParams);
  NamedDecl *ActOnFriendFunctionDecl(Scope *S, Declarator &D,
                                     MultiTemplateParamsArg TemplateParams);

  /// CheckConstructorDeclarator - Called by ActOnDeclarator to check
  /// the well-formedness of the constructor declarator @p D with type @p
  /// R. If there are any errors in the declarator, this routine will
  /// emit diagnostics and set the invalid bit to true.  In any case, the type
  /// will be updated to reflect a well-formed type for the constructor and
  /// returned.
  QualType CheckConstructorDeclarator(Declarator &D, QualType R,
                                      StorageClass &SC);

  /// CheckConstructor - Checks a fully-formed constructor for
  /// well-formedness, issuing any diagnostics required. Returns true if
  /// the constructor declarator is invalid.
  void CheckConstructor(CXXConstructorDecl *Constructor);

  /// CheckDestructorDeclarator - Called by ActOnDeclarator to check
  /// the well-formednes of the destructor declarator @p D with type @p
  /// R. If there are any errors in the declarator, this routine will
  /// emit diagnostics and set the declarator to invalid.  Even if this happens,
  /// will be updated to reflect a well-formed type for the destructor and
  /// returned.
  QualType CheckDestructorDeclarator(Declarator &D, QualType R,
                                     StorageClass &SC);

  /// CheckDestructor - Checks a fully-formed destructor definition for
  /// well-formedness, issuing any diagnostics required.  Returns true
  /// on error.
  bool CheckDestructor(CXXDestructorDecl *Destructor);

  /// CheckConversionDeclarator - Called by ActOnDeclarator to check the
  /// well-formednes of the conversion function declarator @p D with
  /// type @p R. If there are any errors in the declarator, this routine
  /// will emit diagnostics and return true. Otherwise, it will return
  /// false. Either way, the type @p R will be updated to reflect a
  /// well-formed type for the conversion operator.
  void CheckConversionDeclarator(Declarator &D, QualType &R, StorageClass &SC);

  /// ActOnConversionDeclarator - Called by ActOnDeclarator to complete
  /// the declaration of the given C++ conversion function. This routine
  /// is responsible for recording the conversion function in the C++
  /// class, if possible.
  Decl *ActOnConversionDeclarator(CXXConversionDecl *Conversion);

  /// Check the validity of a declarator that we parsed for a deduction-guide.
  /// These aren't actually declarators in the grammar, so we need to check that
  /// the user didn't specify any pieces that are not part of the
  /// deduction-guide grammar. Return true on invalid deduction-guide.
  bool CheckDeductionGuideDeclarator(Declarator &D, QualType &R,
                                     StorageClass &SC);

  void CheckExplicitlyDefaultedFunction(Scope *S, FunctionDecl *MD);

  bool CheckExplicitlyDefaultedSpecialMember(CXXMethodDecl *MD,
                                             CXXSpecialMemberKind CSM,
                                             SourceLocation DefaultLoc);
  void CheckDelayedMemberExceptionSpecs();

  /// Kinds of defaulted comparison operator functions.
  enum class DefaultedComparisonKind : unsigned char {
    /// This is not a defaultable comparison operator.
    None,
    /// This is an operator== that should be implemented as a series of
    /// subobject comparisons.
    Equal,
    /// This is an operator<=> that should be implemented as a series of
    /// subobject comparisons.
    ThreeWay,
    /// This is an operator!= that should be implemented as a rewrite in terms
    /// of a == comparison.
    NotEqual,
    /// This is an <, <=, >, or >= that should be implemented as a rewrite in
    /// terms of a <=> comparison.
    Relational,
  };

  bool CheckExplicitlyDefaultedComparison(Scope *S, FunctionDecl *MD,
                                          DefaultedComparisonKind DCK);
  void DeclareImplicitEqualityComparison(CXXRecordDecl *RD,
                                         FunctionDecl *Spaceship);
  void DefineDefaultedComparison(SourceLocation Loc, FunctionDecl *FD,
                                 DefaultedComparisonKind DCK);

  void CheckExplicitObjectMemberFunction(Declarator &D, DeclarationName Name,
                                         QualType R, bool IsLambda,
                                         DeclContext *DC = nullptr);
  void CheckExplicitObjectMemberFunction(DeclContext *DC, Declarator &D,
                                         DeclarationName Name, QualType R);
  void CheckExplicitObjectLambda(Declarator &D);

  //===--------------------------------------------------------------------===//
  // C++ Derived Classes
  //

  /// Check the validity of a C++ base class specifier.
  ///
  /// \returns a new CXXBaseSpecifier if well-formed, emits diagnostics
  /// and returns NULL otherwise.
  CXXBaseSpecifier *CheckBaseSpecifier(CXXRecordDecl *Class,
                                       SourceRange SpecifierRange, bool Virtual,
                                       AccessSpecifier Access,
                                       TypeSourceInfo *TInfo,
                                       SourceLocation EllipsisLoc);

  /// ActOnBaseSpecifier - Parsed a base specifier. A base specifier is
  /// one entry in the base class list of a class specifier, for
  /// example:
  ///    class foo : public bar, virtual private baz {
  /// 'public bar' and 'virtual private baz' are each base-specifiers.
  BaseResult ActOnBaseSpecifier(Decl *classdecl, SourceRange SpecifierRange,
                                const ParsedAttributesView &Attrs, bool Virtual,
                                AccessSpecifier Access, ParsedType basetype,
                                SourceLocation BaseLoc,
                                SourceLocation EllipsisLoc);

  /// Performs the actual work of attaching the given base class
  /// specifiers to a C++ class.
  bool AttachBaseSpecifiers(CXXRecordDecl *Class,
                            MutableArrayRef<CXXBaseSpecifier *> Bases);

  /// ActOnBaseSpecifiers - Attach the given base specifiers to the
  /// class, after checking whether there are any duplicate base
  /// classes.
  void ActOnBaseSpecifiers(Decl *ClassDecl,
                           MutableArrayRef<CXXBaseSpecifier *> Bases);

  /// Determine whether the type \p Derived is a C++ class that is
  /// derived from the type \p Base.
  bool IsDerivedFrom(SourceLocation Loc, QualType Derived, QualType Base);

  /// Determine whether the type \p Derived is a C++ class that is
  /// derived from the type \p Base.
  bool IsDerivedFrom(SourceLocation Loc, QualType Derived, QualType Base,
                     CXXBasePaths &Paths);

  // FIXME: I don't like this name.
  void BuildBasePathArray(const CXXBasePaths &Paths, CXXCastPath &BasePath);

  bool CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                    SourceLocation Loc, SourceRange Range,
                                    CXXCastPath *BasePath = nullptr,
                                    bool IgnoreAccess = false);

  /// CheckDerivedToBaseConversion - Check whether the Derived-to-Base
  /// conversion (where Derived and Base are class types) is
  /// well-formed, meaning that the conversion is unambiguous (and
  /// that all of the base classes are accessible). Returns true
  /// and emits a diagnostic if the code is ill-formed, returns false
  /// otherwise. Loc is the location where this routine should point to
  /// if there is an error, and Range is the source range to highlight
  /// if there is an error.
  ///
  /// If either InaccessibleBaseID or AmbiguousBaseConvID are 0, then the
  /// diagnostic for the respective type of error will be suppressed, but the
  /// check for ill-formed code will still be performed.
  bool CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                    unsigned InaccessibleBaseID,
                                    unsigned AmbiguousBaseConvID,
                                    SourceLocation Loc, SourceRange Range,
                                    DeclarationName Name, CXXCastPath *BasePath,
                                    bool IgnoreAccess = false);

  /// Builds a string representing ambiguous paths from a
  /// specific derived class to different subobjects of the same base
  /// class.
  ///
  /// This function builds a string that can be used in error messages
  /// to show the different paths that one can take through the
  /// inheritance hierarchy to go from the derived class to different
  /// subobjects of a base class. The result looks something like this:
  /// @code
  /// struct D -> struct B -> struct A
  /// struct D -> struct C -> struct A
  /// @endcode
  std::string getAmbiguousPathsDisplayString(CXXBasePaths &Paths);

  bool CheckOverridingFunctionAttributes(CXXMethodDecl *New,
                                         const CXXMethodDecl *Old);

  /// CheckOverridingFunctionReturnType - Checks whether the return types are
  /// covariant, according to C++ [class.virtual]p5.
  bool CheckOverridingFunctionReturnType(const CXXMethodDecl *New,
                                         const CXXMethodDecl *Old);

  // Check that the overriding method has no explicit object parameter.
  bool CheckExplicitObjectOverride(CXXMethodDecl *New,
                                   const CXXMethodDecl *Old);

  /// Mark the given method pure.
  ///
  /// \param Method the method to be marked pure.
  ///
  /// \param InitRange the source range that covers the "0" initializer.
  bool CheckPureMethod(CXXMethodDecl *Method, SourceRange InitRange);

  /// CheckOverrideControl - Check C++11 override control semantics.
  void CheckOverrideControl(NamedDecl *D);

  /// DiagnoseAbsenceOfOverrideControl - Diagnose if 'override' keyword was
  /// not used in the declaration of an overriding method.
  void DiagnoseAbsenceOfOverrideControl(NamedDecl *D, bool Inconsistent);

  /// CheckIfOverriddenFunctionIsMarkedFinal - Checks whether a virtual member
  /// function overrides a virtual member function marked 'final', according to
  /// C++11 [class.virtual]p4.
  bool CheckIfOverriddenFunctionIsMarkedFinal(const CXXMethodDecl *New,
                                              const CXXMethodDecl *Old);

  enum AbstractDiagSelID {
    AbstractNone = -1,
    AbstractReturnType,
    AbstractParamType,
    AbstractVariableType,
    AbstractFieldType,
    AbstractIvarType,
    AbstractSynthesizedIvarType,
    AbstractArrayType
  };

  struct TypeDiagnoser;

  bool isAbstractType(SourceLocation Loc, QualType T);
  bool RequireNonAbstractType(SourceLocation Loc, QualType T,
                              TypeDiagnoser &Diagnoser);
  template <typename... Ts>
  bool RequireNonAbstractType(SourceLocation Loc, QualType T, unsigned DiagID,
                              const Ts &...Args) {
    BoundTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireNonAbstractType(Loc, T, Diagnoser);
  }

  void DiagnoseAbstractType(const CXXRecordDecl *RD);

  //===--------------------------------------------------------------------===//
  // C++ Overloaded Operators [C++ 13.5]
  //

  /// CheckOverloadedOperatorDeclaration - Check whether the declaration
  /// of this overloaded operator is well-formed. If so, returns false;
  /// otherwise, emits appropriate diagnostics and returns true.
  bool CheckOverloadedOperatorDeclaration(FunctionDecl *FnDecl);

  /// CheckLiteralOperatorDeclaration - Check whether the declaration
  /// of this literal operator function is well-formed. If so, returns
  /// false; otherwise, emits appropriate diagnostics and returns true.
  bool CheckLiteralOperatorDeclaration(FunctionDecl *FnDecl);

  /// ActOnExplicitBoolSpecifier - Build an ExplicitSpecifier from an expression
  /// found in an explicit(bool) specifier.
  ExplicitSpecifier ActOnExplicitBoolSpecifier(Expr *E);

  /// tryResolveExplicitSpecifier - Attempt to resolve the explict specifier.
  /// Returns true if the explicit specifier is now resolved.
  bool tryResolveExplicitSpecifier(ExplicitSpecifier &ExplicitSpec);

  /// ActOnCXXConditionDeclarationExpr - Parsed a condition declaration of a
  /// C++ if/switch/while/for statement.
  /// e.g: "if (int x = f()) {...}"
  DeclResult ActOnCXXConditionDeclaration(Scope *S, Declarator &D);

  // Emitting members of dllexported classes is delayed until the class
  // (including field initializers) is fully parsed.
  SmallVector<CXXRecordDecl *, 4> DelayedDllExportClasses;
  SmallVector<CXXMethodDecl *, 4> DelayedDllExportMemberFunctions;

  /// Merge the exception specifications of two variable declarations.
  ///
  /// This is called when there's a redeclaration of a VarDecl. The function
  /// checks if the redeclaration might have an exception specification and
  /// validates compatibility and merges the specs if necessary.
  void MergeVarDeclExceptionSpecs(VarDecl *New, VarDecl *Old);

  /// MergeCXXFunctionDecl - Merge two declarations of the same C++
  /// function, once we already know that they have the same
  /// type. Subroutine of MergeFunctionDecl. Returns true if there was an
  /// error, false otherwise.
  bool MergeCXXFunctionDecl(FunctionDecl *New, FunctionDecl *Old, Scope *S);

  /// Helpers for dealing with blocks and functions.
  void CheckCXXDefaultArguments(FunctionDecl *FD);

  /// CheckExtraCXXDefaultArguments - Check for any extra default
  /// arguments in the declarator, which is not a function declaration
  /// or definition and therefore is not permitted to have default
  /// arguments. This routine should be invoked for every declarator
  /// that is not a function declaration or definition.
  void CheckExtraCXXDefaultArguments(Declarator &D);

  CXXSpecialMemberKind getSpecialMember(const CXXMethodDecl *MD) {
    return getDefaultedFunctionKind(MD).asSpecialMember();
  }

  /// Perform semantic analysis for the variable declaration that
  /// occurs within a C++ catch clause, returning the newly-created
  /// variable.
  VarDecl *BuildExceptionDeclaration(Scope *S, TypeSourceInfo *TInfo,
                                     SourceLocation StartLoc,
                                     SourceLocation IdLoc,
                                     const IdentifierInfo *Id);

  /// ActOnExceptionDeclarator - Parsed the exception-declarator in a C++ catch
  /// handler.
  Decl *ActOnExceptionDeclarator(Scope *S, Declarator &D);

  void DiagnoseReturnInConstructorExceptionHandler(CXXTryStmt *TryBlock);

  /// Handle a friend tag declaration where the scope specifier was
  /// templated.
  DeclResult ActOnTemplatedFriendTag(Scope *S, SourceLocation FriendLoc,
                                     unsigned TagSpec, SourceLocation TagLoc,
                                     CXXScopeSpec &SS, IdentifierInfo *Name,
                                     SourceLocation NameLoc,
                                     const ParsedAttributesView &Attr,
                                     MultiTemplateParamsArg TempParamLists);

  MSPropertyDecl *HandleMSProperty(Scope *S, RecordDecl *TagD,
                                   SourceLocation DeclStart, Declarator &D,
                                   Expr *BitfieldWidth,
                                   InClassInitStyle InitStyle,
                                   AccessSpecifier AS,
                                   const ParsedAttr &MSPropertyAttr);

  /// Diagnose why the specified class does not have a trivial special member of
  /// the given kind.
  void DiagnoseNontrivial(const CXXRecordDecl *Record,
                          CXXSpecialMemberKind CSM);

  enum TrivialABIHandling {
    /// The triviality of a method unaffected by "trivial_abi".
    TAH_IgnoreTrivialABI,

    /// The triviality of a method affected by "trivial_abi".
    TAH_ConsiderTrivialABI
  };

  /// Determine whether a defaulted or deleted special member function is
  /// trivial, as specified in C++11 [class.ctor]p5, C++11 [class.copy]p12,
  /// C++11 [class.copy]p25, and C++11 [class.dtor]p5.
  bool SpecialMemberIsTrivial(CXXMethodDecl *MD, CXXSpecialMemberKind CSM,
                              TrivialABIHandling TAH = TAH_IgnoreTrivialABI,
                              bool Diagnose = false);

  /// For a defaulted function, the kind of defaulted function that it is.
  class DefaultedFunctionKind {
    LLVM_PREFERRED_TYPE(CXXSpecialMemberKind)
    unsigned SpecialMember : 8;
    unsigned Comparison : 8;

  public:
    DefaultedFunctionKind()
        : SpecialMember(llvm::to_underlying(CXXSpecialMemberKind::Invalid)),
          Comparison(llvm::to_underlying(DefaultedComparisonKind::None)) {}
    DefaultedFunctionKind(CXXSpecialMemberKind CSM)
        : SpecialMember(llvm::to_underlying(CSM)),
          Comparison(llvm::to_underlying(DefaultedComparisonKind::None)) {}
    DefaultedFunctionKind(DefaultedComparisonKind Comp)
        : SpecialMember(llvm::to_underlying(CXXSpecialMemberKind::Invalid)),
          Comparison(llvm::to_underlying(Comp)) {}

    bool isSpecialMember() const {
      return static_cast<CXXSpecialMemberKind>(SpecialMember) !=
             CXXSpecialMemberKind::Invalid;
    }
    bool isComparison() const {
      return static_cast<DefaultedComparisonKind>(Comparison) !=
             DefaultedComparisonKind::None;
    }

    explicit operator bool() const {
      return isSpecialMember() || isComparison();
    }

    CXXSpecialMemberKind asSpecialMember() const {
      return static_cast<CXXSpecialMemberKind>(SpecialMember);
    }
    DefaultedComparisonKind asComparison() const {
      return static_cast<DefaultedComparisonKind>(Comparison);
    }

    /// Get the index of this function kind for use in diagnostics.
    unsigned getDiagnosticIndex() const {
      static_assert(llvm::to_underlying(CXXSpecialMemberKind::Invalid) >
                        llvm::to_underlying(CXXSpecialMemberKind::Destructor),
                    "invalid should have highest index");
      static_assert((unsigned)DefaultedComparisonKind::None == 0,
                    "none should be equal to zero");
      return SpecialMember + Comparison;
    }
  };

  /// Determine the kind of defaulting that would be done for a given function.
  ///
  /// If the function is both a default constructor and a copy / move
  /// constructor (due to having a default argument for the first parameter),
  /// this picks CXXSpecialMemberKind::DefaultConstructor.
  ///
  /// FIXME: Check that case is properly handled by all callers.
  DefaultedFunctionKind getDefaultedFunctionKind(const FunctionDecl *FD);

  /// Handle a C++11 empty-declaration and attribute-declaration.
  Decl *ActOnEmptyDeclaration(Scope *S, const ParsedAttributesView &AttrList,
                              SourceLocation SemiLoc);

  enum class CheckConstexprKind {
    /// Diagnose issues that are non-constant or that are extensions.
    Diagnose,
    /// Identify whether this function satisfies the formal rules for constexpr
    /// functions in the current lanugage mode (with no extensions).
    CheckValid
  };

  // Check whether a function declaration satisfies the requirements of a
  // constexpr function definition or a constexpr constructor definition. If so,
  // return true. If not, produce appropriate diagnostics (unless asked not to
  // by Kind) and return false.
  //
  // This implements C++11 [dcl.constexpr]p3,4, as amended by DR1360.
  bool CheckConstexprFunctionDefinition(const FunctionDecl *FD,
                                        CheckConstexprKind Kind);

  /// Diagnose methods which overload virtual methods in a base class
  /// without overriding any.
  void DiagnoseHiddenVirtualMethods(CXXMethodDecl *MD);

  /// Check if a method overloads virtual methods in a base class without
  /// overriding any.
  void
  FindHiddenVirtualMethods(CXXMethodDecl *MD,
                           SmallVectorImpl<CXXMethodDecl *> &OverloadedMethods);
  void
  NoteHiddenVirtualMethods(CXXMethodDecl *MD,
                           SmallVectorImpl<CXXMethodDecl *> &OverloadedMethods);

  /// ActOnParamDefaultArgument - Check whether the default argument
  /// provided for a function parameter is well-formed. If so, attach it
  /// to the parameter declaration.
  void ActOnParamDefaultArgument(Decl *param, SourceLocation EqualLoc,
                                 Expr *defarg);

  /// ActOnParamUnparsedDefaultArgument - We've seen a default
  /// argument for a function parameter, but we can't parse it yet
  /// because we're inside a class definition. Note that this default
  /// argument will be parsed later.
  void ActOnParamUnparsedDefaultArgument(Decl *param, SourceLocation EqualLoc,
                                         SourceLocation ArgLoc);

  /// ActOnParamDefaultArgumentError - Parsing or semantic analysis of
  /// the default argument for the parameter param failed.
  void ActOnParamDefaultArgumentError(Decl *param, SourceLocation EqualLoc,
                                      Expr *DefaultArg);
  ExprResult ConvertParamDefaultArgument(ParmVarDecl *Param, Expr *DefaultArg,
                                         SourceLocation EqualLoc);
  void SetParamDefaultArgument(ParmVarDecl *Param, Expr *DefaultArg,
                               SourceLocation EqualLoc);

  void ActOnPureSpecifier(Decl *D, SourceLocation PureSpecLoc);
  void SetDeclDeleted(Decl *dcl, SourceLocation DelLoc,
                      StringLiteral *Message = nullptr);
  void SetDeclDefaulted(Decl *dcl, SourceLocation DefaultLoc);

  void SetFunctionBodyKind(Decl *D, SourceLocation Loc, FnBodyKind BodyKind,
                           StringLiteral *DeletedMessage = nullptr);
  void ActOnStartTrailingRequiresClause(Scope *S, Declarator &D);
  ExprResult ActOnFinishTrailingRequiresClause(ExprResult ConstraintExpr);
  ExprResult ActOnRequiresClause(ExprResult ConstraintExpr);

  NamedDecl *
  ActOnDecompositionDeclarator(Scope *S, Declarator &D,
                               MultiTemplateParamsArg TemplateParamLists);
  void DiagPlaceholderVariableDefinition(SourceLocation Loc);
  bool DiagRedefinedPlaceholderFieldDecl(SourceLocation Loc,
                                         RecordDecl *ClassDecl,
                                         const IdentifierInfo *Name);

  void CheckCompleteDecompositionDeclaration(DecompositionDecl *DD);

  /// Stack containing information needed when in C++2a an 'auto' is encountered
  /// in a function declaration parameter type specifier in order to invent a
  /// corresponding template parameter in the enclosing abbreviated function
  /// template. This information is also present in LambdaScopeInfo, stored in
  /// the FunctionScopes stack.
  SmallVector<InventedTemplateParameterInfo, 4> InventedParameterInfos;

  /// FieldCollector - Collects CXXFieldDecls during parsing of C++ classes.
  std::unique_ptr<CXXFieldCollector> FieldCollector;

  typedef llvm::SmallSetVector<const NamedDecl *, 16> NamedDeclSetType;
  /// Set containing all declared private fields that are not used.
  NamedDeclSetType UnusedPrivateFields;

  typedef llvm::SmallPtrSet<const CXXRecordDecl *, 8> RecordDeclSetTy;

  /// PureVirtualClassDiagSet - a set of class declarations which we have
  /// emitted a list of pure virtual functions. Used to prevent emitting the
  /// same list more than once.
  std::unique_ptr<RecordDeclSetTy> PureVirtualClassDiagSet;

  typedef LazyVector<CXXConstructorDecl *, ExternalSemaSource,
                     &ExternalSemaSource::ReadDelegatingConstructors, 2, 2>
      DelegatingCtorDeclsType;

  /// All the delegating constructors seen so far in the file, used for
  /// cycle detection at the end of the TU.
  DelegatingCtorDeclsType DelegatingCtorDecls;

  /// The C++ "std" namespace, where the standard library resides.
  LazyDeclPtr StdNamespace;

  /// The C++ "std::initializer_list" template, which is defined in
  /// \<initializer_list>.
  ClassTemplateDecl *StdInitializerList;

  // Contains the locations of the beginning of unparsed default
  // argument locations.
  llvm::DenseMap<ParmVarDecl *, SourceLocation> UnparsedDefaultArgLocs;

  /// UndefinedInternals - all the used, undefined objects which require a
  /// definition in this translation unit.
  llvm::MapVector<NamedDecl *, SourceLocation> UndefinedButUsed;

  typedef llvm::PointerIntPair<CXXRecordDecl *, 3, CXXSpecialMemberKind>
      SpecialMemberDecl;

  /// The C++ special members which we are currently in the process of
  /// declaring. If this process recursively triggers the declaration of the
  /// same special member, we should act as if it is not yet declared.
  llvm::SmallPtrSet<SpecialMemberDecl, 4> SpecialMembersBeingDeclared;

  void NoteDeletedInheritingConstructor(CXXConstructorDecl *CD);

  void ActOnDefaultCtorInitializers(Decl *CDtorDecl);

  typedef ProcessingContextState ParsingClassState;
  ParsingClassState PushParsingClass() {
    ParsingClassDepth++;
    return DelayedDiagnostics.pushUndelayed();
  }
  void PopParsingClass(ParsingClassState state) {
    ParsingClassDepth--;
    DelayedDiagnostics.popUndelayed(state);
  }

  ValueDecl *tryLookupCtorInitMemberDecl(CXXRecordDecl *ClassDecl,
                                         CXXScopeSpec &SS,
                                         ParsedType TemplateTypeTy,
                                         IdentifierInfo *MemberOrBase);

private:
  void setupImplicitSpecialMemberType(CXXMethodDecl *SpecialMem,
                                      QualType ResultTy,
                                      ArrayRef<QualType> Args);

  // A cache representing if we've fully checked the various comparison category
  // types stored in ASTContext. The bit-index corresponds to the integer value
  // of a ComparisonCategoryType enumerator.
  llvm::SmallBitVector FullyCheckedComparisonCategories;

  /// Check if there is a field shadowing.
  void CheckShadowInheritedFields(const SourceLocation &Loc,
                                  DeclarationName FieldName,
                                  const CXXRecordDecl *RD,
                                  bool DeclIsField = true);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Exception Specifications
  /// Implementations are in SemaExceptionSpec.cpp
  ///@{

public:
  /// All the overriding functions seen during a class definition
  /// that had their exception spec checks delayed, plus the overridden
  /// function.
  SmallVector<std::pair<const CXXMethodDecl *, const CXXMethodDecl *>, 2>
      DelayedOverridingExceptionSpecChecks;

  /// All the function redeclarations seen during a class definition that had
  /// their exception spec checks delayed, plus the prior declaration they
  /// should be checked against. Except during error recovery, the new decl
  /// should always be a friend declaration, as that's the only valid way to
  /// redeclare a special member before its class is complete.
  SmallVector<std::pair<FunctionDecl *, FunctionDecl *>, 2>
      DelayedEquivalentExceptionSpecChecks;

  /// Determine if we're in a case where we need to (incorrectly) eagerly
  /// parse an exception specification to work around a libstdc++ bug.
  bool isLibstdcxxEagerExceptionSpecHack(const Declarator &D);

  /// Check the given noexcept-specifier, convert its expression, and compute
  /// the appropriate ExceptionSpecificationType.
  ExprResult ActOnNoexceptSpec(Expr *NoexceptExpr,
                               ExceptionSpecificationType &EST);

  CanThrowResult canThrow(const Stmt *E);
  /// Determine whether the callee of a particular function call can throw.
  /// E, D and Loc are all optional.
  static CanThrowResult canCalleeThrow(Sema &S, const Expr *E, const Decl *D,
                                       SourceLocation Loc = SourceLocation());
  const FunctionProtoType *ResolveExceptionSpec(SourceLocation Loc,
                                                const FunctionProtoType *FPT);
  void UpdateExceptionSpec(FunctionDecl *FD,
                           const FunctionProtoType::ExceptionSpecInfo &ESI);

  /// CheckSpecifiedExceptionType - Check if the given type is valid in an
  /// exception specification. Incomplete types, or pointers to incomplete types
  /// other than void are not allowed.
  ///
  /// \param[in,out] T  The exception type. This will be decayed to a pointer
  /// type
  ///                   when the input is an array or a function type.
  bool CheckSpecifiedExceptionType(QualType &T, SourceRange Range);

  /// CheckDistantExceptionSpec - Check if the given type is a pointer or
  /// pointer to member to a function with an exception specification. This
  /// means that it is invalid to add another level of indirection.
  bool CheckDistantExceptionSpec(QualType T);
  bool CheckEquivalentExceptionSpec(FunctionDecl *Old, FunctionDecl *New);

  /// CheckEquivalentExceptionSpec - Check if the two types have equivalent
  /// exception specifications. Exception specifications are equivalent if
  /// they allow exactly the same set of exception types. It does not matter how
  /// that is achieved. See C++ [except.spec]p2.
  bool CheckEquivalentExceptionSpec(const FunctionProtoType *Old,
                                    SourceLocation OldLoc,
                                    const FunctionProtoType *New,
                                    SourceLocation NewLoc);
  bool CheckEquivalentExceptionSpec(const PartialDiagnostic &DiagID,
                                    const PartialDiagnostic &NoteID,
                                    const FunctionProtoType *Old,
                                    SourceLocation OldLoc,
                                    const FunctionProtoType *New,
                                    SourceLocation NewLoc);
  bool handlerCanCatch(QualType HandlerType, QualType ExceptionType);

  /// CheckExceptionSpecSubset - Check whether the second function type's
  /// exception specification is a subset (or equivalent) of the first function
  /// type. This is used by override and pointer assignment checks.
  bool CheckExceptionSpecSubset(
      const PartialDiagnostic &DiagID, const PartialDiagnostic &NestedDiagID,
      const PartialDiagnostic &NoteID, const PartialDiagnostic &NoThrowDiagID,
      const FunctionProtoType *Superset, bool SkipSupersetFirstParameter,
      SourceLocation SuperLoc, const FunctionProtoType *Subset,
      bool SkipSubsetFirstParameter, SourceLocation SubLoc);

  /// CheckParamExceptionSpec - Check if the parameter and return types of the
  /// two functions have equivalent exception specs. This is part of the
  /// assignment and override compatibility check. We do not check the
  /// parameters of parameter function pointers recursively, as no sane
  /// programmer would even be able to write such a function type.
  bool CheckParamExceptionSpec(
      const PartialDiagnostic &NestedDiagID, const PartialDiagnostic &NoteID,
      const FunctionProtoType *Target, bool SkipTargetFirstParameter,
      SourceLocation TargetLoc, const FunctionProtoType *Source,
      bool SkipSourceFirstParameter, SourceLocation SourceLoc);

  bool CheckExceptionSpecCompatibility(Expr *From, QualType ToType);

  /// CheckOverridingFunctionExceptionSpec - Checks whether the exception
  /// spec is a subset of base spec.
  bool CheckOverridingFunctionExceptionSpec(const CXXMethodDecl *New,
                                            const CXXMethodDecl *Old);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Expressions
  /// Implementations are in SemaExpr.cpp
  ///@{

public:
  /// Describes how the expressions currently being parsed are
  /// evaluated at run-time, if at all.
  enum class ExpressionEvaluationContext {
    /// The current expression and its subexpressions occur within an
    /// unevaluated operand (C++11 [expr]p7), such as the subexpression of
    /// \c sizeof, where the type of the expression may be significant but
    /// no code will be generated to evaluate the value of the expression at
    /// run time.
    Unevaluated,

    /// The current expression occurs within a braced-init-list within
    /// an unevaluated operand. This is mostly like a regular unevaluated
    /// context, except that we still instantiate constexpr functions that are
    /// referenced here so that we can perform narrowing checks correctly.
    UnevaluatedList,

    /// The current expression occurs within a discarded statement.
    /// This behaves largely similarly to an unevaluated operand in preventing
    /// definitions from being required, but not in other ways.
    DiscardedStatement,

    /// The current expression occurs within an unevaluated
    /// operand that unconditionally permits abstract references to
    /// fields, such as a SIZE operator in MS-style inline assembly.
    UnevaluatedAbstract,

    /// The current context is "potentially evaluated" in C++11 terms,
    /// but the expression is evaluated at compile-time (like the values of
    /// cases in a switch statement).
    ConstantEvaluated,

    /// In addition of being constant evaluated, the current expression
    /// occurs in an immediate function context - either a consteval function
    /// or a consteval if statement.
    ImmediateFunctionContext,

    /// The current expression is potentially evaluated at run time,
    /// which means that code may be generated to evaluate the value of the
    /// expression at run time.
    PotentiallyEvaluated,

    /// The current expression is potentially evaluated, but any
    /// declarations referenced inside that expression are only used if
    /// in fact the current expression is used.
    ///
    /// This value is used when parsing default function arguments, for which
    /// we would like to provide diagnostics (e.g., passing non-POD arguments
    /// through varargs) but do not want to mark declarations as "referenced"
    /// until the default argument is used.
    PotentiallyEvaluatedIfUsed
  };

  /// Store a set of either DeclRefExprs or MemberExprs that contain a reference
  /// to a variable (constant) that may or may not be odr-used in this Expr, and
  /// we won't know until all lvalue-to-rvalue and discarded value conversions
  /// have been applied to all subexpressions of the enclosing full expression.
  /// This is cleared at the end of each full expression.
  using MaybeODRUseExprSet = llvm::SmallSetVector<Expr *, 4>;
  MaybeODRUseExprSet MaybeODRUseExprs;

  using ImmediateInvocationCandidate = llvm::PointerIntPair<ConstantExpr *, 1>;

  /// Data structure used to record current or nested
  /// expression evaluation contexts.
  struct ExpressionEvaluationContextRecord {
    /// The expression evaluation context.
    ExpressionEvaluationContext Context;

    /// Whether the enclosing context needed a cleanup.
    CleanupInfo ParentCleanup;

    /// The number of active cleanup objects when we entered
    /// this expression evaluation context.
    unsigned NumCleanupObjects;

    /// The number of typos encountered during this expression evaluation
    /// context (i.e. the number of TypoExprs created).
    unsigned NumTypos;

    MaybeODRUseExprSet SavedMaybeODRUseExprs;

    /// The lambdas that are present within this context, if it
    /// is indeed an unevaluated context.
    SmallVector<LambdaExpr *, 2> Lambdas;

    /// The declaration that provides context for lambda expressions
    /// and block literals if the normal declaration context does not
    /// suffice, e.g., in a default function argument.
    Decl *ManglingContextDecl;

    /// If we are processing a decltype type, a set of call expressions
    /// for which we have deferred checking the completeness of the return type.
    SmallVector<CallExpr *, 8> DelayedDecltypeCalls;

    /// If we are processing a decltype type, a set of temporary binding
    /// expressions for which we have deferred checking the destructor.
    SmallVector<CXXBindTemporaryExpr *, 8> DelayedDecltypeBinds;

    llvm::SmallPtrSet<const Expr *, 8> PossibleDerefs;

    /// Expressions appearing as the LHS of a volatile assignment in this
    /// context. We produce a warning for these when popping the context if
    /// they are not discarded-value expressions nor unevaluated operands.
    SmallVector<Expr *, 2> VolatileAssignmentLHSs;

    /// Set of candidates for starting an immediate invocation.
    llvm::SmallVector<ImmediateInvocationCandidate, 4>
        ImmediateInvocationCandidates;

    /// Set of DeclRefExprs referencing a consteval function when used in a
    /// context not already known to be immediately invoked.
    llvm::SmallPtrSet<DeclRefExpr *, 4> ReferenceToConsteval;

    /// P2718R0 - Lifetime extension in range-based for loops.
    /// MaterializeTemporaryExprs in for-range-init expressions which need to
    /// extend lifetime. Add MaterializeTemporaryExpr* if the value of
    /// InLifetimeExtendingContext is true.
    SmallVector<MaterializeTemporaryExpr *, 8> ForRangeLifetimeExtendTemps;

    /// \brief Describes whether we are in an expression constext which we have
    /// to handle differently.
    enum ExpressionKind {
      EK_Decltype,
      EK_TemplateArgument,
      EK_AttrArgument,
      EK_Other
    } ExprContext;

    // A context can be nested in both a discarded statement context and
    // an immediate function context, so they need to be tracked independently.
    bool InDiscardedStatement;
    bool InImmediateFunctionContext;
    bool InImmediateEscalatingFunctionContext;

    bool IsCurrentlyCheckingDefaultArgumentOrInitializer = false;

    // We are in a constant context, but we also allow
    // non constant expressions, for example for array bounds (which may be
    // VLAs).
    bool InConditionallyConstantEvaluateContext = false;

    /// Whether we are currently in a context in which all temporaries must be
    /// lifetime-extended, even if they're not bound to a reference (for
    /// example, in a for-range initializer).
    bool InLifetimeExtendingContext = false;

    // When evaluating immediate functions in the initializer of a default
    // argument or default member initializer, this is the declaration whose
    // default initializer is being evaluated and the location of the call
    // or constructor definition.
    struct InitializationContext {
      InitializationContext(SourceLocation Loc, ValueDecl *Decl,
                            DeclContext *Context)
          : Loc(Loc), Decl(Decl), Context(Context) {
        assert(Decl && Context && "invalid initialization context");
      }

      SourceLocation Loc;
      ValueDecl *Decl = nullptr;
      DeclContext *Context = nullptr;
    };
    std::optional<InitializationContext> DelayedDefaultInitializationContext;

    ExpressionEvaluationContextRecord(ExpressionEvaluationContext Context,
                                      unsigned NumCleanupObjects,
                                      CleanupInfo ParentCleanup,
                                      Decl *ManglingContextDecl,
                                      ExpressionKind ExprContext)
        : Context(Context), ParentCleanup(ParentCleanup),
          NumCleanupObjects(NumCleanupObjects), NumTypos(0),
          ManglingContextDecl(ManglingContextDecl), ExprContext(ExprContext),
          InDiscardedStatement(false), InImmediateFunctionContext(false),
          InImmediateEscalatingFunctionContext(false) {}

    bool isUnevaluated() const {
      return Context == ExpressionEvaluationContext::Unevaluated ||
             Context == ExpressionEvaluationContext::UnevaluatedAbstract ||
             Context == ExpressionEvaluationContext::UnevaluatedList;
    }

    bool isPotentiallyEvaluated() const {
      return Context == ExpressionEvaluationContext::PotentiallyEvaluated ||
             Context ==
                 ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed ||
             Context == ExpressionEvaluationContext::ConstantEvaluated;
    }

    bool isConstantEvaluated() const {
      return Context == ExpressionEvaluationContext::ConstantEvaluated ||
             Context == ExpressionEvaluationContext::ImmediateFunctionContext;
    }

    bool isImmediateFunctionContext() const {
      return Context == ExpressionEvaluationContext::ImmediateFunctionContext ||
             (Context == ExpressionEvaluationContext::DiscardedStatement &&
              InImmediateFunctionContext) ||
             // C++23 [expr.const]p14:
             // An expression or conversion is in an immediate function
             // context if it is potentially evaluated and either:
             //   * its innermost enclosing non-block scope is a function
             //     parameter scope of an immediate function, or
             //   * its enclosing statement is enclosed by the compound-
             //     statement of a consteval if statement.
             (Context == ExpressionEvaluationContext::PotentiallyEvaluated &&
              InImmediateFunctionContext);
    }

    bool isDiscardedStatementContext() const {
      return Context == ExpressionEvaluationContext::DiscardedStatement ||
             (Context ==
                  ExpressionEvaluationContext::ImmediateFunctionContext &&
              InDiscardedStatement);
    }
  };

  const ExpressionEvaluationContextRecord &currentEvaluationContext() const {
    assert(!ExprEvalContexts.empty() &&
           "Must be in an expression evaluation context");
    return ExprEvalContexts.back();
  };

  ExpressionEvaluationContextRecord &currentEvaluationContext() {
    assert(!ExprEvalContexts.empty() &&
           "Must be in an expression evaluation context");
    return ExprEvalContexts.back();
  };

  ExpressionEvaluationContextRecord &parentEvaluationContext() {
    assert(ExprEvalContexts.size() >= 2 &&
           "Must be in an expression evaluation context");
    return ExprEvalContexts[ExprEvalContexts.size() - 2];
  };

  const ExpressionEvaluationContextRecord &parentEvaluationContext() const {
    return const_cast<Sema *>(this)->parentEvaluationContext();
  };

  bool isAttrContext() const {
    return ExprEvalContexts.back().ExprContext ==
           ExpressionEvaluationContextRecord::ExpressionKind::EK_AttrArgument;
  }

  /// Increment when we find a reference; decrement when we find an ignored
  /// assignment.  Ultimately the value is 0 if every reference is an ignored
  /// assignment.
  llvm::DenseMap<const VarDecl *, int> RefsMinusAssignments;

  /// Used to control the generation of ExprWithCleanups.
  CleanupInfo Cleanup;

  /// ExprCleanupObjects - This is the stack of objects requiring
  /// cleanup that are created by the current full expression.
  SmallVector<ExprWithCleanups::CleanupObject, 8> ExprCleanupObjects;

  // AssignmentAction - This is used by all the assignment diagnostic functions
  // to represent what is actually causing the operation
  enum AssignmentAction {
    AA_Assigning,
    AA_Passing,
    AA_Returning,
    AA_Converting,
    AA_Initializing,
    AA_Sending,
    AA_Casting,
    AA_Passing_CFAudited
  };

  /// Determine whether the use of this declaration is valid, without
  /// emitting diagnostics.
  bool CanUseDecl(NamedDecl *D, bool TreatUnavailableAsInvalid);
  // A version of DiagnoseUseOfDecl that should be used if overload resolution
  // has been used to find this declaration, which means we don't have to bother
  // checking the trailing requires clause.
  bool DiagnoseUseOfOverloadedDecl(NamedDecl *D, SourceLocation Loc) {
    return DiagnoseUseOfDecl(
        D, Loc, /*UnknownObjCClass=*/nullptr, /*ObjCPropertyAccess=*/false,
        /*AvoidPartialAvailabilityChecks=*/false, /*ClassReceiver=*/nullptr,
        /*SkipTrailingRequiresClause=*/true);
  }

  /// Determine whether the use of this declaration is valid, and
  /// emit any corresponding diagnostics.
  ///
  /// This routine diagnoses various problems with referencing
  /// declarations that can occur when using a declaration. For example,
  /// it might warn if a deprecated or unavailable declaration is being
  /// used, or produce an error (and return true) if a C++0x deleted
  /// function is being used.
  ///
  /// \returns true if there was an error (this declaration cannot be
  /// referenced), false otherwise.
  bool DiagnoseUseOfDecl(NamedDecl *D, ArrayRef<SourceLocation> Locs,
                         const ObjCInterfaceDecl *UnknownObjCClass = nullptr,
                         bool ObjCPropertyAccess = false,
                         bool AvoidPartialAvailabilityChecks = false,
                         ObjCInterfaceDecl *ClassReciever = nullptr,
                         bool SkipTrailingRequiresClause = false);

  /// Emit a note explaining that this function is deleted.
  void NoteDeletedFunction(FunctionDecl *FD);

  /// DiagnoseSentinelCalls - This routine checks whether a call or
  /// message-send is to a declaration with the sentinel attribute, and
  /// if so, it checks that the requirements of the sentinel are
  /// satisfied.
  void DiagnoseSentinelCalls(const NamedDecl *D, SourceLocation Loc,
                             ArrayRef<Expr *> Args);

  void PushExpressionEvaluationContext(
      ExpressionEvaluationContext NewContext, Decl *LambdaContextDecl = nullptr,
      ExpressionEvaluationContextRecord::ExpressionKind Type =
          ExpressionEvaluationContextRecord::EK_Other);
  enum ReuseLambdaContextDecl_t { ReuseLambdaContextDecl };
  void PushExpressionEvaluationContext(
      ExpressionEvaluationContext NewContext, ReuseLambdaContextDecl_t,
      ExpressionEvaluationContextRecord::ExpressionKind Type =
          ExpressionEvaluationContextRecord::EK_Other);
  void PopExpressionEvaluationContext();

  void DiscardCleanupsInEvaluationContext();

  ExprResult TransformToPotentiallyEvaluated(Expr *E);
  TypeSourceInfo *TransformToPotentiallyEvaluated(TypeSourceInfo *TInfo);
  ExprResult HandleExprEvaluationContextForTypeof(Expr *E);

  /// Check whether E, which is either a discarded-value expression or an
  /// unevaluated operand, is a simple-assignment to a volatlie-qualified
  /// lvalue, and if so, remove it from the list of volatile-qualified
  /// assignments that we are going to warn are deprecated.
  void CheckUnusedVolatileAssignment(Expr *E);

  ExprResult ActOnConstantExpression(ExprResult Res);

  // Functions for marking a declaration referenced.  These functions also
  // contain the relevant logic for marking if a reference to a function or
  // variable is an odr-use (in the C++11 sense).  There are separate variants
  // for expressions referring to a decl; these exist because odr-use marking
  // needs to be delayed for some constant variables when we build one of the
  // named expressions.
  //
  // MightBeOdrUse indicates whether the use could possibly be an odr-use, and
  // should usually be true. This only needs to be set to false if the lack of
  // odr-use cannot be determined from the current context (for instance,
  // because the name denotes a virtual function and was written without an
  // explicit nested-name-specifier).
  void MarkAnyDeclReferenced(SourceLocation Loc, Decl *D, bool MightBeOdrUse);

  /// Mark a function referenced, and check whether it is odr-used
  /// (C++ [basic.def.odr]p2, C99 6.9p3)
  void MarkFunctionReferenced(SourceLocation Loc, FunctionDecl *Func,
                              bool MightBeOdrUse = true);

  /// Mark a variable referenced, and check whether it is odr-used
  /// (C++ [basic.def.odr]p2, C99 6.9p3).  Note that this should not be
  /// used directly for normal expressions referring to VarDecl.
  void MarkVariableReferenced(SourceLocation Loc, VarDecl *Var);

  /// Perform reference-marking and odr-use handling for a DeclRefExpr.
  ///
  /// Note, this may change the dependence of the DeclRefExpr, and so needs to
  /// be handled with care if the DeclRefExpr is not newly-created.
  void MarkDeclRefReferenced(DeclRefExpr *E, const Expr *Base = nullptr);

  /// Perform reference-marking and odr-use handling for a MemberExpr.
  void MarkMemberReferenced(MemberExpr *E);

  /// Perform reference-marking and odr-use handling for a FunctionParmPackExpr.
  void MarkFunctionParmPackReferenced(FunctionParmPackExpr *E);
  void MarkCaptureUsedInEnclosingContext(ValueDecl *Capture, SourceLocation Loc,
                                         unsigned CapturingScopeIndex);

  ExprResult CheckLValueToRValueConversionOperand(Expr *E);
  void CleanupVarDeclMarking();

  enum TryCaptureKind {
    TryCapture_Implicit,
    TryCapture_ExplicitByVal,
    TryCapture_ExplicitByRef
  };

  /// Try to capture the given variable.
  ///
  /// \param Var The variable to capture.
  ///
  /// \param Loc The location at which the capture occurs.
  ///
  /// \param Kind The kind of capture, which may be implicit (for either a
  /// block or a lambda), or explicit by-value or by-reference (for a lambda).
  ///
  /// \param EllipsisLoc The location of the ellipsis, if one is provided in
  /// an explicit lambda capture.
  ///
  /// \param BuildAndDiagnose Whether we are actually supposed to add the
  /// captures or diagnose errors. If false, this routine merely check whether
  /// the capture can occur without performing the capture itself or complaining
  /// if the variable cannot be captured.
  ///
  /// \param CaptureType Will be set to the type of the field used to capture
  /// this variable in the innermost block or lambda. Only valid when the
  /// variable can be captured.
  ///
  /// \param DeclRefType Will be set to the type of a reference to the capture
  /// from within the current scope. Only valid when the variable can be
  /// captured.
  ///
  /// \param FunctionScopeIndexToStopAt If non-null, it points to the index
  /// of the FunctionScopeInfo stack beyond which we do not attempt to capture.
  /// This is useful when enclosing lambdas must speculatively capture
  /// variables that may or may not be used in certain specializations of
  /// a nested generic lambda.
  ///
  /// \returns true if an error occurred (i.e., the variable cannot be
  /// captured) and false if the capture succeeded.
  bool tryCaptureVariable(ValueDecl *Var, SourceLocation Loc,
                          TryCaptureKind Kind, SourceLocation EllipsisLoc,
                          bool BuildAndDiagnose, QualType &CaptureType,
                          QualType &DeclRefType,
                          const unsigned *const FunctionScopeIndexToStopAt);

  /// Try to capture the given variable.
  bool tryCaptureVariable(ValueDecl *Var, SourceLocation Loc,
                          TryCaptureKind Kind = TryCapture_Implicit,
                          SourceLocation EllipsisLoc = SourceLocation());

  /// Checks if the variable must be captured.
  bool NeedToCaptureVariable(ValueDecl *Var, SourceLocation Loc);

  /// Given a variable, determine the type that a reference to that
  /// variable will have in the given scope.
  QualType getCapturedDeclRefType(ValueDecl *Var, SourceLocation Loc);

  /// Mark all of the declarations referenced within a particular AST node as
  /// referenced. Used when template instantiation instantiates a non-dependent
  /// type -- entities referenced by the type are now referenced.
  void MarkDeclarationsReferencedInType(SourceLocation Loc, QualType T);

  /// Mark any declarations that appear within this expression or any
  /// potentially-evaluated subexpressions as "referenced".
  ///
  /// \param SkipLocalVariables If true, don't mark local variables as
  /// 'referenced'.
  /// \param StopAt Subexpressions that we shouldn't recurse into.
  void MarkDeclarationsReferencedInExpr(
      Expr *E, bool SkipLocalVariables = false,
      ArrayRef<const Expr *> StopAt = std::nullopt);

  /// Try to convert an expression \p E to type \p Ty. Returns the result of the
  /// conversion.
  ExprResult tryConvertExprToType(Expr *E, QualType Ty);

  /// Conditionally issue a diagnostic based on the statements's reachability
  /// analysis.
  ///
  /// \param Stmts If Stmts is non-empty, delay reporting the diagnostic until
  /// the function body is parsed, and then do a basic reachability analysis to
  /// determine if the statement is reachable. If it is unreachable, the
  /// diagnostic will not be emitted.
  bool DiagIfReachable(SourceLocation Loc, ArrayRef<const Stmt *> Stmts,
                       const PartialDiagnostic &PD);

  /// Conditionally issue a diagnostic based on the current
  /// evaluation context.
  ///
  /// \param Statement If Statement is non-null, delay reporting the
  /// diagnostic until the function body is parsed, and then do a basic
  /// reachability analysis to determine if the statement is reachable.
  /// If it is unreachable, the diagnostic will not be emitted.
  bool DiagRuntimeBehavior(SourceLocation Loc, const Stmt *Statement,
                           const PartialDiagnostic &PD);
  /// Similar, but diagnostic is only produced if all the specified statements
  /// are reachable.
  bool DiagRuntimeBehavior(SourceLocation Loc, ArrayRef<const Stmt *> Stmts,
                           const PartialDiagnostic &PD);

  // Primary Expressions.
  SourceRange getExprRange(Expr *E) const;

  ExprResult ActOnIdExpression(Scope *S, CXXScopeSpec &SS,
                               SourceLocation TemplateKWLoc, UnqualifiedId &Id,
                               bool HasTrailingLParen, bool IsAddressOfOperand,
                               CorrectionCandidateCallback *CCC = nullptr,
                               bool IsInlineAsmIdentifier = false,
                               Token *KeywordReplacement = nullptr);

  /// Decomposes the given name into a DeclarationNameInfo, its location, and
  /// possibly a list of template arguments.
  ///
  /// If this produces template arguments, it is permitted to call
  /// DecomposeTemplateName.
  ///
  /// This actually loses a lot of source location information for
  /// non-standard name kinds; we should consider preserving that in
  /// some way.
  void DecomposeUnqualifiedId(const UnqualifiedId &Id,
                              TemplateArgumentListInfo &Buffer,
                              DeclarationNameInfo &NameInfo,
                              const TemplateArgumentListInfo *&TemplateArgs);

  /// Diagnose a lookup that found results in an enclosing class during error
  /// recovery. This usually indicates that the results were found in a
  /// dependent base class that could not be searched as part of a template
  /// definition. Always issues a diagnostic (though this may be only a warning
  /// in MS compatibility mode).
  ///
  /// Return \c true if the error is unrecoverable, or \c false if the caller
  /// should attempt to recover using these lookup results.
  bool DiagnoseDependentMemberLookup(const LookupResult &R);

  /// Diagnose an empty lookup.
  ///
  /// \return false if new lookup candidates were found
  bool
  DiagnoseEmptyLookup(Scope *S, CXXScopeSpec &SS, LookupResult &R,
                      CorrectionCandidateCallback &CCC,
                      TemplateArgumentListInfo *ExplicitTemplateArgs = nullptr,
                      ArrayRef<Expr *> Args = std::nullopt,
                      DeclContext *LookupCtx = nullptr,
                      TypoExpr **Out = nullptr);

  /// If \p D cannot be odr-used in the current expression evaluation context,
  /// return a reason explaining why. Otherwise, return NOUR_None.
  NonOdrUseReason getNonOdrUseReasonInCurrentContext(ValueDecl *D);

  DeclRefExpr *BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                                SourceLocation Loc,
                                const CXXScopeSpec *SS = nullptr);
  DeclRefExpr *
  BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                   const DeclarationNameInfo &NameInfo,
                   const CXXScopeSpec *SS = nullptr,
                   NamedDecl *FoundD = nullptr,
                   SourceLocation TemplateKWLoc = SourceLocation(),
                   const TemplateArgumentListInfo *TemplateArgs = nullptr);

  /// BuildDeclRefExpr - Build an expression that references a
  /// declaration that does not require a closure capture.
  DeclRefExpr *
  BuildDeclRefExpr(ValueDecl *D, QualType Ty, ExprValueKind VK,
                   const DeclarationNameInfo &NameInfo,
                   NestedNameSpecifierLoc NNS, NamedDecl *FoundD = nullptr,
                   SourceLocation TemplateKWLoc = SourceLocation(),
                   const TemplateArgumentListInfo *TemplateArgs = nullptr);

  bool UseArgumentDependentLookup(const CXXScopeSpec &SS, const LookupResult &R,
                                  bool HasTrailingLParen);

  /// BuildQualifiedDeclarationNameExpr - Build a C++ qualified
  /// declaration name, generally during template instantiation.
  /// There's a large number of things which don't need to be done along
  /// this path.
  ExprResult BuildQualifiedDeclarationNameExpr(
      CXXScopeSpec &SS, const DeclarationNameInfo &NameInfo,
      bool IsAddressOfOperand, TypeSourceInfo **RecoveryTSI = nullptr);

  ExprResult BuildDeclarationNameExpr(const CXXScopeSpec &SS, LookupResult &R,
                                      bool NeedsADL,
                                      bool AcceptInvalidDecl = false);

  /// Complete semantic analysis for a reference to the given declaration.
  ExprResult BuildDeclarationNameExpr(
      const CXXScopeSpec &SS, const DeclarationNameInfo &NameInfo, NamedDecl *D,
      NamedDecl *FoundD = nullptr,
      const TemplateArgumentListInfo *TemplateArgs = nullptr,
      bool AcceptInvalidDecl = false);

  // ExpandFunctionLocalPredefinedMacros - Returns a new vector of Tokens,
  // where Tokens representing function local predefined macros (such as
  // __FUNCTION__) are replaced (expanded) with string-literal Tokens.
  std::vector<Token> ExpandFunctionLocalPredefinedMacros(ArrayRef<Token> Toks);

  ExprResult BuildPredefinedExpr(SourceLocation Loc, PredefinedIdentKind IK);
  ExprResult ActOnPredefinedExpr(SourceLocation Loc, tok::TokenKind Kind);
  ExprResult ActOnIntegerConstant(SourceLocation Loc, uint64_t Val);

  bool CheckLoopHintExpr(Expr *E, SourceLocation Loc, bool AllowZero);

  ExprResult ActOnNumericConstant(const Token &Tok, Scope *UDLScope = nullptr);
  ExprResult ActOnCharacterConstant(const Token &Tok,
                                    Scope *UDLScope = nullptr);
  ExprResult ActOnParenExpr(SourceLocation L, SourceLocation R, Expr *E);
  ExprResult ActOnParenListExpr(SourceLocation L, SourceLocation R,
                                MultiExprArg Val);

  /// ActOnStringLiteral - The specified tokens were lexed as pasted string
  /// fragments (e.g. "foo" "bar" L"baz").  The result string has to handle
  /// string concatenation ([C99 5.1.1.2, translation phase #6]), so it may come
  /// from multiple tokens.  However, the common case is that StringToks points
  /// to one string.
  ExprResult ActOnStringLiteral(ArrayRef<Token> StringToks,
                                Scope *UDLScope = nullptr);

  ExprResult ActOnUnevaluatedStringLiteral(ArrayRef<Token> StringToks);

  /// ControllingExprOrType is either an opaque pointer coming out of a
  /// ParsedType or an Expr *. FIXME: it'd be better to split this interface
  /// into two so we don't take a void *, but that's awkward because one of
  /// the operands is either a ParsedType or an Expr *, which doesn't lend
  /// itself to generic code very well.
  ExprResult ActOnGenericSelectionExpr(SourceLocation KeyLoc,
                                       SourceLocation DefaultLoc,
                                       SourceLocation RParenLoc,
                                       bool PredicateIsExpr,
                                       void *ControllingExprOrType,
                                       ArrayRef<ParsedType> ArgTypes,
                                       ArrayRef<Expr *> ArgExprs);
  /// ControllingExprOrType is either a TypeSourceInfo * or an Expr *. FIXME:
  /// it'd be better to split this interface into two so we don't take a
  /// void *, but see the FIXME on ActOnGenericSelectionExpr as to why that
  /// isn't a trivial change.
  ExprResult CreateGenericSelectionExpr(SourceLocation KeyLoc,
                                        SourceLocation DefaultLoc,
                                        SourceLocation RParenLoc,
                                        bool PredicateIsExpr,
                                        void *ControllingExprOrType,
                                        ArrayRef<TypeSourceInfo *> Types,
                                        ArrayRef<Expr *> Exprs);

  // Binary/Unary Operators.  'Tok' is the token for the operator.
  ExprResult CreateBuiltinUnaryOp(SourceLocation OpLoc, UnaryOperatorKind Opc,
                                  Expr *InputExpr, bool IsAfterAmp = false);
  ExprResult BuildUnaryOp(Scope *S, SourceLocation OpLoc, UnaryOperatorKind Opc,
                          Expr *Input, bool IsAfterAmp = false);

  /// Unary Operators.  'Tok' is the token for the operator.
  ExprResult ActOnUnaryOp(Scope *S, SourceLocation OpLoc, tok::TokenKind Op,
                          Expr *Input, bool IsAfterAmp = false);

  /// Determine whether the given expression is a qualified member
  /// access expression, of a form that could be turned into a pointer to member
  /// with the address-of operator.
  bool isQualifiedMemberAccess(Expr *E);
  bool CheckUseOfCXXMethodAsAddressOfOperand(SourceLocation OpLoc,
                                             const Expr *Op,
                                             const CXXMethodDecl *MD);

  /// CheckAddressOfOperand - The operand of & must be either a function
  /// designator or an lvalue designating an object. If it is an lvalue, the
  /// object cannot be declared with storage class register or be a bit field.
  /// Note: The usual conversions are *not* applied to the operand of the &
  /// operator (C99 6.3.2.1p[2-4]), and its result is never an lvalue.
  /// In C++, the operand might be an overloaded function name, in which case
  /// we allow the '&' but retain the overloaded-function type.
  QualType CheckAddressOfOperand(ExprResult &Operand, SourceLocation OpLoc);

  /// ActOnAlignasTypeArgument - Handle @c alignas(type-id) and @c
  /// _Alignas(type-name) .
  /// [dcl.align] An alignment-specifier of the form
  /// alignas(type-id) has the same effect as alignas(alignof(type-id)).
  ///
  /// [N1570 6.7.5] _Alignas(type-name) is equivalent to
  /// _Alignas(_Alignof(type-name)).
  bool ActOnAlignasTypeArgument(StringRef KWName, ParsedType Ty,
                                SourceLocation OpLoc, SourceRange R);
  bool CheckAlignasTypeArgument(StringRef KWName, TypeSourceInfo *TInfo,
                                SourceLocation OpLoc, SourceRange R);

  /// Build a sizeof or alignof expression given a type operand.
  ExprResult CreateUnaryExprOrTypeTraitExpr(TypeSourceInfo *TInfo,
                                            SourceLocation OpLoc,
                                            UnaryExprOrTypeTrait ExprKind,
                                            SourceRange R);

  /// Build a sizeof or alignof expression given an expression
  /// operand.
  ExprResult CreateUnaryExprOrTypeTraitExpr(Expr *E, SourceLocation OpLoc,
                                            UnaryExprOrTypeTrait ExprKind);

  /// ActOnUnaryExprOrTypeTraitExpr - Handle @c sizeof(type) and @c sizeof @c
  /// expr and the same for @c alignof and @c __alignof
  /// Note that the ArgRange is invalid if isType is false.
  ExprResult ActOnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc,
                                           UnaryExprOrTypeTrait ExprKind,
                                           bool IsType, void *TyOrEx,
                                           SourceRange ArgRange);

  /// Check for operands with placeholder types and complain if found.
  /// Returns ExprError() if there was an error and no recovery was possible.
  ExprResult CheckPlaceholderExpr(Expr *E);
  bool CheckVecStepExpr(Expr *E);

  /// Check the constraints on expression operands to unary type expression
  /// and type traits.
  ///
  /// Completes any types necessary and validates the constraints on the operand
  /// expression. The logic mostly mirrors the type-based overload, but may
  /// modify the expression as it completes the type for that expression through
  /// template instantiation, etc.
  bool CheckUnaryExprOrTypeTraitOperand(Expr *E, UnaryExprOrTypeTrait ExprKind);

  /// Check the constraints on operands to unary expression and type
  /// traits.
  ///
  /// This will complete any types necessary, and validate the various
  /// constraints on those operands.
  ///
  /// The UsualUnaryConversions() function is *not* called by this routine.
  /// C99 6.3.2.1p[2-4] all state:
  ///   Except when it is the operand of the sizeof operator ...
  ///
  /// C++ [expr.sizeof]p4
  ///   The lvalue-to-rvalue, array-to-pointer, and function-to-pointer
  ///   standard conversions are not applied to the operand of sizeof.
  ///
  /// This policy is followed for all of the unary trait expressions.
  bool CheckUnaryExprOrTypeTraitOperand(QualType ExprType, SourceLocation OpLoc,
                                        SourceRange ExprRange,
                                        UnaryExprOrTypeTrait ExprKind,
                                        StringRef KWName);

  ExprResult ActOnPostfixUnaryOp(Scope *S, SourceLocation OpLoc,
                                 tok::TokenKind Kind, Expr *Input);

  ExprResult ActOnArraySubscriptExpr(Scope *S, Expr *Base, SourceLocation LLoc,
                                     MultiExprArg ArgExprs,
                                     SourceLocation RLoc);
  ExprResult CreateBuiltinArraySubscriptExpr(Expr *Base, SourceLocation LLoc,
                                             Expr *Idx, SourceLocation RLoc);

  ExprResult CreateBuiltinMatrixSubscriptExpr(Expr *Base, Expr *RowIdx,
                                              Expr *ColumnIdx,
                                              SourceLocation RBLoc);

  /// ConvertArgumentsForCall - Converts the arguments specified in
  /// Args/NumArgs to the parameter types of the function FDecl with
  /// function prototype Proto. Call is the call expression itself, and
  /// Fn is the function expression. For a C++ member function, this
  /// routine does not attempt to convert the object argument. Returns
  /// true if the call is ill-formed.
  bool ConvertArgumentsForCall(CallExpr *Call, Expr *Fn, FunctionDecl *FDecl,
                               const FunctionProtoType *Proto,
                               ArrayRef<Expr *> Args, SourceLocation RParenLoc,
                               bool ExecConfig = false);

  /// CheckStaticArrayArgument - If the given argument corresponds to a static
  /// array parameter, check that it is non-null, and that if it is formed by
  /// array-to-pointer decay, the underlying array is sufficiently large.
  ///
  /// C99 6.7.5.3p7: If the keyword static also appears within the [ and ] of
  /// the array type derivation, then for each call to the function, the value
  /// of the corresponding actual argument shall provide access to the first
  /// element of an array with at least as many elements as specified by the
  /// size expression.
  void CheckStaticArrayArgument(SourceLocation CallLoc, ParmVarDecl *Param,
                                const Expr *ArgExpr);

  /// ActOnCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.
  ExprResult ActOnCallExpr(Scope *S, Expr *Fn, SourceLocation LParenLoc,
                           MultiExprArg ArgExprs, SourceLocation RParenLoc,
                           Expr *ExecConfig = nullptr);

  /// BuildCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.
  ExprResult BuildCallExpr(Scope *S, Expr *Fn, SourceLocation LParenLoc,
                           MultiExprArg ArgExprs, SourceLocation RParenLoc,
                           Expr *ExecConfig = nullptr,
                           bool IsExecConfig = false,
                           bool AllowRecovery = false);

  /// BuildBuiltinCallExpr - Create a call to a builtin function specified by Id
  //  with the specified CallArgs
  Expr *BuildBuiltinCallExpr(SourceLocation Loc, Builtin::ID Id,
                             MultiExprArg CallArgs);

  using ADLCallKind = CallExpr::ADLCallKind;

  /// BuildResolvedCallExpr - Build a call to a resolved expression,
  /// i.e. an expression not of \p OverloadTy.  The expression should
  /// unary-convert to an expression of function-pointer or
  /// block-pointer type.
  ///
  /// \param NDecl the declaration being called, if available
  ExprResult
  BuildResolvedCallExpr(Expr *Fn, NamedDecl *NDecl, SourceLocation LParenLoc,
                        ArrayRef<Expr *> Arg, SourceLocation RParenLoc,
                        Expr *Config = nullptr, bool IsExecConfig = false,
                        ADLCallKind UsesADL = ADLCallKind::NotADL);

  ExprResult ActOnCastExpr(Scope *S, SourceLocation LParenLoc, Declarator &D,
                           ParsedType &Ty, SourceLocation RParenLoc,
                           Expr *CastExpr);

  /// Prepares for a scalar cast, performing all the necessary stages
  /// except the final cast and returning the kind required.
  CastKind PrepareScalarCast(ExprResult &src, QualType destType);

  /// Build an altivec or OpenCL literal.
  ExprResult BuildVectorLiteral(SourceLocation LParenLoc,
                                SourceLocation RParenLoc, Expr *E,
                                TypeSourceInfo *TInfo);

  /// This is not an AltiVec-style cast or or C++ direct-initialization, so turn
  /// the ParenListExpr into a sequence of comma binary operators.
  ExprResult MaybeConvertParenListExprToParenExpr(Scope *S, Expr *ME);

  ExprResult ActOnCompoundLiteral(SourceLocation LParenLoc, ParsedType Ty,
                                  SourceLocation RParenLoc, Expr *InitExpr);

  ExprResult BuildCompoundLiteralExpr(SourceLocation LParenLoc,
                                      TypeSourceInfo *TInfo,
                                      SourceLocation RParenLoc,
                                      Expr *LiteralExpr);

  ExprResult ActOnInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                           SourceLocation RBraceLoc);

  ExprResult BuildInitList(SourceLocation LBraceLoc, MultiExprArg InitArgList,
                           SourceLocation RBraceLoc);

  /// Binary Operators.  'Tok' is the token for the operator.
  ExprResult ActOnBinOp(Scope *S, SourceLocation TokLoc, tok::TokenKind Kind,
                        Expr *LHSExpr, Expr *RHSExpr);
  ExprResult BuildBinOp(Scope *S, SourceLocation OpLoc, BinaryOperatorKind Opc,
                        Expr *LHSExpr, Expr *RHSExpr);

  /// CreateBuiltinBinOp - Creates a new built-in binary operation with
  /// operator @p Opc at location @c TokLoc. This routine only supports
  /// built-in operations; ActOnBinOp handles overloaded operators.
  ExprResult CreateBuiltinBinOp(SourceLocation OpLoc, BinaryOperatorKind Opc,
                                Expr *LHSExpr, Expr *RHSExpr);
  void LookupBinOp(Scope *S, SourceLocation OpLoc, BinaryOperatorKind Opc,
                   UnresolvedSetImpl &Functions);

  /// Look for instances where it is likely the comma operator is confused with
  /// another operator.  There is an explicit list of acceptable expressions for
  /// the left hand side of the comma operator, otherwise emit a warning.
  void DiagnoseCommaOperator(const Expr *LHS, SourceLocation Loc);

  /// ActOnConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  ExprResult ActOnConditionalOp(SourceLocation QuestionLoc,
                                SourceLocation ColonLoc, Expr *CondExpr,
                                Expr *LHSExpr, Expr *RHSExpr);

  /// ActOnAddrLabel - Parse the GNU address of label extension: "&&foo".
  ExprResult ActOnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                            LabelDecl *TheDecl);

  void ActOnStartStmtExpr();
  ExprResult ActOnStmtExpr(Scope *S, SourceLocation LPLoc, Stmt *SubStmt,
                           SourceLocation RPLoc);
  ExprResult BuildStmtExpr(SourceLocation LPLoc, Stmt *SubStmt,
                           SourceLocation RPLoc, unsigned TemplateDepth);
  // Handle the final expression in a statement expression.
  ExprResult ActOnStmtExprResult(ExprResult E);
  void ActOnStmtExprError();

  // __builtin_offsetof(type, identifier(.identifier|[expr])*)
  struct OffsetOfComponent {
    SourceLocation LocStart, LocEnd;
    bool isBrackets; // true if [expr], false if .ident
    union {
      IdentifierInfo *IdentInfo;
      Expr *E;
    } U;
  };

  /// __builtin_offsetof(type, a.b[123][456].c)
  ExprResult BuildBuiltinOffsetOf(SourceLocation BuiltinLoc,
                                  TypeSourceInfo *TInfo,
                                  ArrayRef<OffsetOfComponent> Components,
                                  SourceLocation RParenLoc);
  ExprResult ActOnBuiltinOffsetOf(Scope *S, SourceLocation BuiltinLoc,
                                  SourceLocation TypeLoc,
                                  ParsedType ParsedArgTy,
                                  ArrayRef<OffsetOfComponent> Components,
                                  SourceLocation RParenLoc);

  // __builtin_choose_expr(constExpr, expr1, expr2)
  ExprResult ActOnChooseExpr(SourceLocation BuiltinLoc, Expr *CondExpr,
                             Expr *LHSExpr, Expr *RHSExpr,
                             SourceLocation RPLoc);

  // __builtin_va_arg(expr, type)
  ExprResult ActOnVAArg(SourceLocation BuiltinLoc, Expr *E, ParsedType Ty,
                        SourceLocation RPLoc);
  ExprResult BuildVAArgExpr(SourceLocation BuiltinLoc, Expr *E,
                            TypeSourceInfo *TInfo, SourceLocation RPLoc);

  // __builtin_LINE(), __builtin_FUNCTION(), __builtin_FUNCSIG(),
  // __builtin_FILE(), __builtin_COLUMN(), __builtin_source_location()
  ExprResult ActOnSourceLocExpr(SourceLocIdentKind Kind,
                                SourceLocation BuiltinLoc,
                                SourceLocation RPLoc);

  // #embed
  ExprResult ActOnEmbedExpr(SourceLocation EmbedKeywordLoc,
                            StringLiteral *BinaryData);

  // Build a potentially resolved SourceLocExpr.
  ExprResult BuildSourceLocExpr(SourceLocIdentKind Kind, QualType ResultTy,
                                SourceLocation BuiltinLoc, SourceLocation RPLoc,
                                DeclContext *ParentContext);

  // __null
  ExprResult ActOnGNUNullExpr(SourceLocation TokenLoc);

  bool CheckCaseExpression(Expr *E);

  //===------------------------- "Block" Extension ------------------------===//

  /// ActOnBlockStart - This callback is invoked when a block literal is
  /// started.
  void ActOnBlockStart(SourceLocation CaretLoc, Scope *CurScope);

  /// ActOnBlockArguments - This callback allows processing of block arguments.
  /// If there are no arguments, this is still invoked.
  void ActOnBlockArguments(SourceLocation CaretLoc, Declarator &ParamInfo,
                           Scope *CurScope);

  /// ActOnBlockError - If there is an error parsing a block, this callback
  /// is invoked to pop the information about the block from the action impl.
  void ActOnBlockError(SourceLocation CaretLoc, Scope *CurScope);

  /// ActOnBlockStmtExpr - This is called when the body of a block statement
  /// literal was successfully completed.  ^(int x){...}
  ExprResult ActOnBlockStmtExpr(SourceLocation CaretLoc, Stmt *Body,
                                Scope *CurScope);

  //===---------------------------- Clang Extensions ----------------------===//

  /// ActOnConvertVectorExpr - create a new convert-vector expression from the
  /// provided arguments.
  ///
  /// __builtin_convertvector( value, dst type )
  ///
  ExprResult ActOnConvertVectorExpr(Expr *E, ParsedType ParsedDestTy,
                                    SourceLocation BuiltinLoc,
                                    SourceLocation RParenLoc);

  //===---------------------------- OpenCL Features -----------------------===//

  /// Parse a __builtin_astype expression.
  ///
  /// __builtin_astype( value, dst type )
  ///
  ExprResult ActOnAsTypeExpr(Expr *E, ParsedType ParsedDestTy,
                             SourceLocation BuiltinLoc,
                             SourceLocation RParenLoc);

  /// Create a new AsTypeExpr node (bitcast) from the arguments.
  ExprResult BuildAsTypeExpr(Expr *E, QualType DestTy,
                             SourceLocation BuiltinLoc,
                             SourceLocation RParenLoc);

  /// Attempts to produce a RecoveryExpr after some AST node cannot be created.
  ExprResult CreateRecoveryExpr(SourceLocation Begin, SourceLocation End,
                                ArrayRef<Expr *> SubExprs,
                                QualType T = QualType());

  /// Cast a base object to a member's actual type.
  ///
  /// There are two relevant checks:
  ///
  /// C++ [class.access.base]p7:
  ///
  ///   If a class member access operator [...] is used to access a non-static
  ///   data member or non-static member function, the reference is ill-formed
  ///   if the left operand [...] cannot be implicitly converted to a pointer to
  ///   the naming class of the right operand.
  ///
  /// C++ [expr.ref]p7:
  ///
  ///   If E2 is a non-static data member or a non-static member function, the
  ///   program is ill-formed if the class of which E2 is directly a member is
  ///   an ambiguous base (11.8) of the naming class (11.9.3) of E2.
  ///
  /// Note that the latter check does not consider access; the access of the
  /// "real" base class is checked as appropriate when checking the access of
  /// the member name.
  ExprResult PerformObjectMemberConversion(Expr *From,
                                           NestedNameSpecifier *Qualifier,
                                           NamedDecl *FoundDecl,
                                           NamedDecl *Member);

  /// CheckCallReturnType - Checks that a call expression's return type is
  /// complete. Returns true on failure. The location passed in is the location
  /// that best represents the call.
  bool CheckCallReturnType(QualType ReturnType, SourceLocation Loc,
                           CallExpr *CE, FunctionDecl *FD);

  /// Emit a warning for all pending noderef expressions that we recorded.
  void WarnOnPendingNoDerefs(ExpressionEvaluationContextRecord &Rec);

  ExprResult BuildCXXDefaultInitExpr(SourceLocation Loc, FieldDecl *Field);

  /// Instantiate or parse a C++ default argument expression as necessary.
  /// Return true on error.
  bool CheckCXXDefaultArgExpr(SourceLocation CallLoc, FunctionDecl *FD,
                              ParmVarDecl *Param, Expr *Init = nullptr,
                              bool SkipImmediateInvocations = true);

  /// BuildCXXDefaultArgExpr - Creates a CXXDefaultArgExpr, instantiating
  /// the default expr if needed.
  ExprResult BuildCXXDefaultArgExpr(SourceLocation CallLoc, FunctionDecl *FD,
                                    ParmVarDecl *Param, Expr *Init = nullptr);

  /// Wrap the expression in a ConstantExpr if it is a potential immediate
  /// invocation.
  ExprResult CheckForImmediateInvocation(ExprResult E, FunctionDecl *Decl);

  void MarkExpressionAsImmediateEscalating(Expr *E);

  // Check that the SME attributes for PSTATE.ZA and PSTATE.SM are compatible.
  bool IsInvalidSMECallConversion(QualType FromType, QualType ToType);

  /// Abstract base class used for diagnosing integer constant
  /// expression violations.
  class VerifyICEDiagnoser {
  public:
    bool Suppress;

    VerifyICEDiagnoser(bool Suppress = false) : Suppress(Suppress) {}

    virtual SemaDiagnosticBuilder
    diagnoseNotICEType(Sema &S, SourceLocation Loc, QualType T);
    virtual SemaDiagnosticBuilder diagnoseNotICE(Sema &S,
                                                 SourceLocation Loc) = 0;
    virtual SemaDiagnosticBuilder diagnoseFold(Sema &S, SourceLocation Loc);
    virtual ~VerifyICEDiagnoser() {}
  };

  enum AllowFoldKind {
    NoFold,
    AllowFold,
  };

  /// VerifyIntegerConstantExpression - Verifies that an expression is an ICE,
  /// and reports the appropriate diagnostics. Returns false on success.
  /// Can optionally return the value of the expression.
  ExprResult VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                             VerifyICEDiagnoser &Diagnoser,
                                             AllowFoldKind CanFold = NoFold);
  ExprResult VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                             unsigned DiagID,
                                             AllowFoldKind CanFold = NoFold);
  ExprResult VerifyIntegerConstantExpression(Expr *E,
                                             llvm::APSInt *Result = nullptr,
                                             AllowFoldKind CanFold = NoFold);
  ExprResult VerifyIntegerConstantExpression(Expr *E,
                                             AllowFoldKind CanFold = NoFold) {
    return VerifyIntegerConstantExpression(E, nullptr, CanFold);
  }

  /// DiagnoseAssignmentAsCondition - Given that an expression is
  /// being used as a boolean condition, warn if it's an assignment.
  void DiagnoseAssignmentAsCondition(Expr *E);

  /// Redundant parentheses over an equality comparison can indicate
  /// that the user intended an assignment used as condition.
  void DiagnoseEqualityWithExtraParens(ParenExpr *ParenE);

  class FullExprArg {
  public:
    FullExprArg() : E(nullptr) {}
    FullExprArg(Sema &actions) : E(nullptr) {}

    ExprResult release() { return E; }

    Expr *get() const { return E; }

    Expr *operator->() { return E; }

  private:
    // FIXME: No need to make the entire Sema class a friend when it's just
    // Sema::MakeFullExpr that needs access to the constructor below.
    friend class Sema;

    explicit FullExprArg(Expr *expr) : E(expr) {}

    Expr *E;
  };

  FullExprArg MakeFullExpr(Expr *Arg) {
    return MakeFullExpr(Arg, Arg ? Arg->getExprLoc() : SourceLocation());
  }
  FullExprArg MakeFullExpr(Expr *Arg, SourceLocation CC) {
    return FullExprArg(
        ActOnFinishFullExpr(Arg, CC, /*DiscardedValue*/ false).get());
  }
  FullExprArg MakeFullDiscardedValueExpr(Expr *Arg) {
    ExprResult FE =
        ActOnFinishFullExpr(Arg, Arg ? Arg->getExprLoc() : SourceLocation(),
                            /*DiscardedValue*/ true);
    return FullExprArg(FE.get());
  }

  class ConditionResult {
    Decl *ConditionVar;
    FullExprArg Condition;
    bool Invalid;
    std::optional<bool> KnownValue;

    friend class Sema;
    ConditionResult(Sema &S, Decl *ConditionVar, FullExprArg Condition,
                    bool IsConstexpr)
        : ConditionVar(ConditionVar), Condition(Condition), Invalid(false) {
      if (IsConstexpr && Condition.get()) {
        if (std::optional<llvm::APSInt> Val =
                Condition.get()->getIntegerConstantExpr(S.Context)) {
          KnownValue = !!(*Val);
        }
      }
    }
    explicit ConditionResult(bool Invalid)
        : ConditionVar(nullptr), Condition(nullptr), Invalid(Invalid),
          KnownValue(std::nullopt) {}

  public:
    ConditionResult() : ConditionResult(false) {}
    bool isInvalid() const { return Invalid; }
    std::pair<VarDecl *, Expr *> get() const {
      return std::make_pair(cast_or_null<VarDecl>(ConditionVar),
                            Condition.get());
    }
    std::optional<bool> getKnownValue() const { return KnownValue; }
  };
  static ConditionResult ConditionError() { return ConditionResult(true); }

  /// CheckBooleanCondition - Diagnose problems involving the use of
  /// the given expression as a boolean condition (e.g. in an if
  /// statement).  Also performs the standard function and array
  /// decays, possibly changing the input variable.
  ///
  /// \param Loc - A location associated with the condition, e.g. the
  /// 'if' keyword.
  /// \return true iff there were any errors
  ExprResult CheckBooleanCondition(SourceLocation Loc, Expr *E,
                                   bool IsConstexpr = false);

  enum class ConditionKind {
    Boolean,     ///< A boolean condition, from 'if', 'while', 'for', or 'do'.
    ConstexprIf, ///< A constant boolean condition from 'if constexpr'.
    Switch       ///< An integral condition for a 'switch' statement.
  };

  ConditionResult ActOnCondition(Scope *S, SourceLocation Loc, Expr *SubExpr,
                                 ConditionKind CK, bool MissingOK = false);

  QualType CheckConditionalOperands( // C99 6.5.15
      ExprResult &Cond, ExprResult &LHS, ExprResult &RHS, ExprValueKind &VK,
      ExprObjectKind &OK, SourceLocation QuestionLoc);

  /// Emit a specialized diagnostic when one expression is a null pointer
  /// constant and the other is not a pointer.  Returns true if a diagnostic is
  /// emitted.
  bool DiagnoseConditionalForNull(const Expr *LHSExpr, const Expr *RHSExpr,
                                  SourceLocation QuestionLoc);

  /// type checking for vector binary operators.
  QualType CheckVectorOperands(ExprResult &LHS, ExprResult &RHS,
                               SourceLocation Loc, bool IsCompAssign,
                               bool AllowBothBool, bool AllowBoolConversion,
                               bool AllowBoolOperation, bool ReportInvalid);

  /// Return a signed ext_vector_type that is of identical size and number of
  /// elements. For floating point vectors, return an integer type of identical
  /// size and number of elements. In the non ext_vector_type case, search from
  /// the largest type to the smallest type to avoid cases where long long ==
  /// long, where long gets picked over long long.
  QualType GetSignedVectorType(QualType V);
  QualType GetSignedSizelessVectorType(QualType V);

  /// CheckVectorCompareOperands - vector comparisons are a clang extension that
  /// operates on extended vector types.  Instead of producing an IntTy result,
  /// like a scalar comparison, a vector comparison produces a vector of integer
  /// types.
  QualType CheckVectorCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc,
                                      BinaryOperatorKind Opc);
  QualType CheckSizelessVectorCompareOperands(ExprResult &LHS, ExprResult &RHS,
                                              SourceLocation Loc,
                                              BinaryOperatorKind Opc);
  QualType CheckVectorLogicalOperands(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc);

  /// Context in which we're performing a usual arithmetic conversion.
  enum ArithConvKind {
    /// An arithmetic operation.
    ACK_Arithmetic,
    /// A bitwise operation.
    ACK_BitwiseOp,
    /// A comparison.
    ACK_Comparison,
    /// A conditional (?:) operator.
    ACK_Conditional,
    /// A compound assignment expression.
    ACK_CompAssign,
  };

  // type checking for sizeless vector binary operators.
  QualType CheckSizelessVectorOperands(ExprResult &LHS, ExprResult &RHS,
                                       SourceLocation Loc, bool IsCompAssign,
                                       ArithConvKind OperationKind);

  /// Type checking for matrix binary operators.
  QualType CheckMatrixElementwiseOperands(ExprResult &LHS, ExprResult &RHS,
                                          SourceLocation Loc,
                                          bool IsCompAssign);
  QualType CheckMatrixMultiplyOperands(ExprResult &LHS, ExprResult &RHS,
                                       SourceLocation Loc, bool IsCompAssign);

  /// Are the two types SVE-bitcast-compatible types? I.e. is bitcasting from
  /// the first SVE type (e.g. an SVE VLAT) to the second type (e.g. an SVE
  /// VLST) allowed?
  ///
  /// This will also return false if the two given types do not make sense from
  /// the perspective of SVE bitcasts.
  bool isValidSveBitcast(QualType srcType, QualType destType);

  /// Are the two types matrix types and do they have the same dimensions i.e.
  /// do they have the same number of rows and the same number of columns?
  bool areMatrixTypesOfTheSameDimension(QualType srcTy, QualType destTy);

  bool areVectorTypesSameSize(QualType srcType, QualType destType);

  /// Are the two types lax-compatible vector types?  That is, given
  /// that one of them is a vector, do they have equal storage sizes,
  /// where the storage size is the number of elements times the element
  /// size?
  ///
  /// This will also return false if either of the types is neither a
  /// vector nor a real type.
  bool areLaxCompatibleVectorTypes(QualType srcType, QualType destType);

  /// Is this a legal conversion between two types, one of which is
  /// known to be a vector type?
  bool isLaxVectorConversion(QualType srcType, QualType destType);

  // This returns true if at least one of the types is an altivec vector.
  bool anyAltivecTypes(QualType srcType, QualType destType);

  // type checking C++ declaration initializers (C++ [dcl.init]).

  /// Check a cast of an unknown-any type.  We intentionally only
  /// trigger this for C-style casts.
  ExprResult checkUnknownAnyCast(SourceRange TypeRange, QualType CastType,
                                 Expr *CastExpr, CastKind &CastKind,
                                 ExprValueKind &VK, CXXCastPath &Path);

  /// Force an expression with unknown-type to an expression of the
  /// given type.
  ExprResult forceUnknownAnyToType(Expr *E, QualType ToType);

  /// Type-check an expression that's being passed to an
  /// __unknown_anytype parameter.
  ExprResult checkUnknownAnyArg(SourceLocation callLoc, Expr *result,
                                QualType &paramType);

  // CheckMatrixCast - Check type constraints for matrix casts.
  // We allow casting between matrixes of the same dimensions i.e. when they
  // have the same number of rows and column. Returns true if the cast is
  // invalid.
  bool CheckMatrixCast(SourceRange R, QualType DestTy, QualType SrcTy,
                       CastKind &Kind);

  // CheckVectorCast - check type constraints for vectors.
  // Since vectors are an extension, there are no C standard reference for this.
  // We allow casting between vectors and integer datatypes of the same size.
  // returns true if the cast is invalid
  bool CheckVectorCast(SourceRange R, QualType VectorTy, QualType Ty,
                       CastKind &Kind);

  /// Prepare `SplattedExpr` for a vector splat operation, adding
  /// implicit casts if necessary.
  ExprResult prepareVectorSplat(QualType VectorTy, Expr *SplattedExpr);

  // CheckExtVectorCast - check type constraints for extended vectors.
  // Since vectors are an extension, there are no C standard reference for this.
  // We allow casting between vectors and integer datatypes of the same size,
  // or vectors and the element type of that vector.
  // returns the cast expr
  ExprResult CheckExtVectorCast(SourceRange R, QualType DestTy, Expr *CastExpr,
                                CastKind &Kind);

  QualType PreferredConditionType(ConditionKind K) const {
    return K == ConditionKind::Switch ? Context.IntTy : Context.BoolTy;
  }

  // UsualUnaryConversions - promotes integers (C99 6.3.1.1p2) and converts
  // functions and arrays to their respective pointers (C99 6.3.2.1).
  ExprResult UsualUnaryConversions(Expr *E);

  /// CallExprUnaryConversions - a special case of an unary conversion
  /// performed on a function designator of a call expression.
  ExprResult CallExprUnaryConversions(Expr *E);

  // DefaultFunctionArrayConversion - converts functions and arrays
  // to their respective pointers (C99 6.3.2.1).
  ExprResult DefaultFunctionArrayConversion(Expr *E, bool Diagnose = true);

  // DefaultFunctionArrayLvalueConversion - converts functions and
  // arrays to their respective pointers and performs the
  // lvalue-to-rvalue conversion.
  ExprResult DefaultFunctionArrayLvalueConversion(Expr *E,
                                                  bool Diagnose = true);

  // DefaultLvalueConversion - performs lvalue-to-rvalue conversion on
  // the operand. This function is a no-op if the operand has a function type
  // or an array type.
  ExprResult DefaultLvalueConversion(Expr *E);

  // DefaultArgumentPromotion (C99 6.5.2.2p6). Used for function calls that
  // do not have a prototype. Integer promotions are performed on each
  // argument, and arguments that have type float are promoted to double.
  ExprResult DefaultArgumentPromotion(Expr *E);

  VariadicCallType getVariadicCallType(FunctionDecl *FDecl,
                                       const FunctionProtoType *Proto,
                                       Expr *Fn);

  // Used for determining in which context a type is allowed to be passed to a
  // vararg function.
  enum VarArgKind {
    VAK_Valid,
    VAK_ValidInCXX11,
    VAK_Undefined,
    VAK_MSVCUndefined,
    VAK_Invalid
  };

  /// Determine the degree of POD-ness for an expression.
  /// Incomplete types are considered POD, since this check can be performed
  /// when we're in an unevaluated context.
  VarArgKind isValidVarArgType(const QualType &Ty);

  /// Check to see if the given expression is a valid argument to a variadic
  /// function, issuing a diagnostic if not.
  void checkVariadicArgument(const Expr *E, VariadicCallType CT);

  /// GatherArgumentsForCall - Collector argument expressions for various
  /// form of call prototypes.
  bool GatherArgumentsForCall(SourceLocation CallLoc, FunctionDecl *FDecl,
                              const FunctionProtoType *Proto,
                              unsigned FirstParam, ArrayRef<Expr *> Args,
                              SmallVectorImpl<Expr *> &AllArgs,
                              VariadicCallType CallType = VariadicDoesNotApply,
                              bool AllowExplicit = false,
                              bool IsListInitialization = false);

  // DefaultVariadicArgumentPromotion - Like DefaultArgumentPromotion, but
  // will create a runtime trap if the resulting type is not a POD type.
  ExprResult DefaultVariadicArgumentPromotion(Expr *E, VariadicCallType CT,
                                              FunctionDecl *FDecl);

  // UsualArithmeticConversions - performs the UsualUnaryConversions on it's
  // operands and then handles various conversions that are common to binary
  // operators (C99 6.3.1.8). If both operands aren't arithmetic, this
  // routine returns the first non-arithmetic type found. The client is
  // responsible for emitting appropriate error diagnostics.
  QualType UsualArithmeticConversions(ExprResult &LHS, ExprResult &RHS,
                                      SourceLocation Loc, ArithConvKind ACK);

  /// AssignConvertType - All of the 'assignment' semantic checks return this
  /// enum to indicate whether the assignment was allowed.  These checks are
  /// done for simple assignments, as well as initialization, return from
  /// function, argument passing, etc.  The query is phrased in terms of a
  /// source and destination type.
  enum AssignConvertType {
    /// Compatible - the types are compatible according to the standard.
    Compatible,

    /// PointerToInt - The assignment converts a pointer to an int, which we
    /// accept as an extension.
    PointerToInt,

    /// IntToPointer - The assignment converts an int to a pointer, which we
    /// accept as an extension.
    IntToPointer,

    /// FunctionVoidPointer - The assignment is between a function pointer and
    /// void*, which the standard doesn't allow, but we accept as an extension.
    FunctionVoidPointer,

    /// IncompatiblePointer - The assignment is between two pointers types that
    /// are not compatible, but we accept them as an extension.
    IncompatiblePointer,

    /// IncompatibleFunctionPointer - The assignment is between two function
    /// pointers types that are not compatible, but we accept them as an
    /// extension.
    IncompatibleFunctionPointer,

    /// IncompatibleFunctionPointerStrict - The assignment is between two
    /// function pointer types that are not identical, but are compatible,
    /// unless compiled with -fsanitize=cfi, in which case the type mismatch
    /// may trip an indirect call runtime check.
    IncompatibleFunctionPointerStrict,

    /// IncompatiblePointerSign - The assignment is between two pointers types
    /// which point to integers which have a different sign, but are otherwise
    /// identical. This is a subset of the above, but broken out because it's by
    /// far the most common case of incompatible pointers.
    IncompatiblePointerSign,

    /// CompatiblePointerDiscardsQualifiers - The assignment discards
    /// c/v/r qualifiers, which we accept as an extension.
    CompatiblePointerDiscardsQualifiers,

    /// IncompatiblePointerDiscardsQualifiers - The assignment
    /// discards qualifiers that we don't permit to be discarded,
    /// like address spaces.
    IncompatiblePointerDiscardsQualifiers,

    /// IncompatibleNestedPointerAddressSpaceMismatch - The assignment
    /// changes address spaces in nested pointer types which is not allowed.
    /// For instance, converting __private int ** to __generic int ** is
    /// illegal even though __private could be converted to __generic.
    IncompatibleNestedPointerAddressSpaceMismatch,

    /// IncompatibleNestedPointerQualifiers - The assignment is between two
    /// nested pointer types, and the qualifiers other than the first two
    /// levels differ e.g. char ** -> const char **, but we accept them as an
    /// extension.
    IncompatibleNestedPointerQualifiers,

    /// IncompatibleVectors - The assignment is between two vector types that
    /// have the same size, which we accept as an extension.
    IncompatibleVectors,

    /// IntToBlockPointer - The assignment converts an int to a block
    /// pointer. We disallow this.
    IntToBlockPointer,

    /// IncompatibleBlockPointer - The assignment is between two block
    /// pointers types that are not compatible.
    IncompatibleBlockPointer,

    /// IncompatibleObjCQualifiedId - The assignment is between a qualified
    /// id type and something else (that is incompatible with it). For example,
    /// "id <XXX>" = "Foo *", where "Foo *" doesn't implement the XXX protocol.
    IncompatibleObjCQualifiedId,

    /// IncompatibleObjCWeakRef - Assigning a weak-unavailable object to an
    /// object with __weak qualifier.
    IncompatibleObjCWeakRef,

    /// Incompatible - We reject this conversion outright, it is invalid to
    /// represent it in the AST.
    Incompatible
  };

  /// DiagnoseAssignmentResult - Emit a diagnostic, if required, for the
  /// assignment conversion type specified by ConvTy.  This returns true if the
  /// conversion was invalid or false if the conversion was accepted.
  bool DiagnoseAssignmentResult(AssignConvertType ConvTy, SourceLocation Loc,
                                QualType DstType, QualType SrcType,
                                Expr *SrcExpr, AssignmentAction Action,
                                bool *Complained = nullptr);

  /// CheckAssignmentConstraints - Perform type checking for assignment,
  /// argument passing, variable initialization, and function return values.
  /// C99 6.5.16.
  AssignConvertType CheckAssignmentConstraints(SourceLocation Loc,
                                               QualType LHSType,
                                               QualType RHSType);

  /// Check assignment constraints and optionally prepare for a conversion of
  /// the RHS to the LHS type. The conversion is prepared for if ConvertRHS
  /// is true.
  AssignConvertType CheckAssignmentConstraints(QualType LHSType,
                                               ExprResult &RHS, CastKind &Kind,
                                               bool ConvertRHS = true);

  /// Check assignment constraints for an assignment of RHS to LHSType.
  ///
  /// \param LHSType The destination type for the assignment.
  /// \param RHS The source expression for the assignment.
  /// \param Diagnose If \c true, diagnostics may be produced when checking
  ///        for assignability. If a diagnostic is produced, \p RHS will be
  ///        set to ExprError(). Note that this function may still return
  ///        without producing a diagnostic, even for an invalid assignment.
  /// \param DiagnoseCFAudited If \c true, the target is a function parameter
  ///        in an audited Core Foundation API and does not need to be checked
  ///        for ARC retain issues.
  /// \param ConvertRHS If \c true, \p RHS will be updated to model the
  ///        conversions necessary to perform the assignment. If \c false,
  ///        \p Diagnose must also be \c false.
  AssignConvertType CheckSingleAssignmentConstraints(
      QualType LHSType, ExprResult &RHS, bool Diagnose = true,
      bool DiagnoseCFAudited = false, bool ConvertRHS = true);

  // If the lhs type is a transparent union, check whether we
  // can initialize the transparent union with the given expression.
  AssignConvertType CheckTransparentUnionArgumentConstraints(QualType ArgType,
                                                             ExprResult &RHS);

  /// the following "Check" methods will return a valid/converted QualType
  /// or a null QualType (indicating an error diagnostic was issued).

  /// type checking binary operators (subroutines of CreateBuiltinBinOp).
  QualType InvalidOperands(SourceLocation Loc, ExprResult &LHS,
                           ExprResult &RHS);

  /// Diagnose cases where a scalar was implicitly converted to a vector and
  /// diagnose the underlying types. Otherwise, diagnose the error
  /// as invalid vector logical operands for non-C++ cases.
  QualType InvalidLogicalVectorOperands(SourceLocation Loc, ExprResult &LHS,
                                        ExprResult &RHS);

  QualType CheckMultiplyDivideOperands( // C99 6.5.5
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc, bool IsCompAssign,
      bool IsDivide);
  QualType CheckRemainderOperands( // C99 6.5.5
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      bool IsCompAssign = false);
  QualType CheckAdditionOperands( // C99 6.5.6
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc, QualType *CompLHSTy = nullptr);
  QualType CheckSubtractionOperands( // C99 6.5.6
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      QualType *CompLHSTy = nullptr);
  QualType CheckShiftOperands( // C99 6.5.7
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc, bool IsCompAssign = false);
  void CheckPtrComparisonWithNullChar(ExprResult &E, ExprResult &NullE);
  QualType CheckCompareOperands( // C99 6.5.8/9
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc);
  QualType CheckBitwiseOperands( // C99 6.5.[10...12]
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc);
  QualType CheckLogicalOperands( // C99 6.5.[13,14]
      ExprResult &LHS, ExprResult &RHS, SourceLocation Loc,
      BinaryOperatorKind Opc);
  // CheckAssignmentOperands is used for both simple and compound assignment.
  // For simple assignment, pass both expressions and a null converted type.
  // For compound assignment, pass both expressions and the converted type.
  QualType CheckAssignmentOperands( // C99 6.5.16.[1,2]
      Expr *LHSExpr, ExprResult &RHS, SourceLocation Loc, QualType CompoundType,
      BinaryOperatorKind Opc);

  /// To be used for checking whether the arguments being passed to
  /// function exceeds the number of parameters expected for it.
  static bool TooManyArguments(size_t NumParams, size_t NumArgs,
                               bool PartialOverloading = false) {
    // We check whether we're just after a comma in code-completion.
    if (NumArgs > 0 && PartialOverloading)
      return NumArgs + 1 > NumParams; // If so, we view as an extra argument.
    return NumArgs > NumParams;
  }

  /// Whether the AST is currently being rebuilt to correct immediate
  /// invocations. Immediate invocation candidates and references to consteval
  /// functions aren't tracked when this is set.
  bool RebuildingImmediateInvocation = false;

  bool isAlwaysConstantEvaluatedContext() const {
    const ExpressionEvaluationContextRecord &Ctx = currentEvaluationContext();
    return (Ctx.isConstantEvaluated() || isConstantEvaluatedOverride) &&
           !Ctx.InConditionallyConstantEvaluateContext;
  }

  /// Determines whether we are currently in a context that
  /// is not evaluated as per C++ [expr] p5.
  bool isUnevaluatedContext() const {
    return currentEvaluationContext().isUnevaluated();
  }

  bool isImmediateFunctionContext() const {
    return currentEvaluationContext().isImmediateFunctionContext();
  }

  bool isInLifetimeExtendingContext() const {
    assert(!ExprEvalContexts.empty() &&
           "Must be in an expression evaluation context");
    return ExprEvalContexts.back().InLifetimeExtendingContext;
  }

  bool isCheckingDefaultArgumentOrInitializer() const {
    const ExpressionEvaluationContextRecord &Ctx = currentEvaluationContext();
    return (Ctx.Context ==
            ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed) ||
           Ctx.IsCurrentlyCheckingDefaultArgumentOrInitializer;
  }

  std::optional<ExpressionEvaluationContextRecord::InitializationContext>
  InnermostDeclarationWithDelayedImmediateInvocations() const {
    assert(!ExprEvalContexts.empty() &&
           "Must be in an expression evaluation context");
    for (const auto &Ctx : llvm::reverse(ExprEvalContexts)) {
      if (Ctx.Context == ExpressionEvaluationContext::PotentiallyEvaluated &&
          Ctx.DelayedDefaultInitializationContext)
        return Ctx.DelayedDefaultInitializationContext;
      if (Ctx.isConstantEvaluated() || Ctx.isImmediateFunctionContext() ||
          Ctx.isUnevaluated())
        break;
    }
    return std::nullopt;
  }

  std::optional<ExpressionEvaluationContextRecord::InitializationContext>
  OutermostDeclarationWithDelayedImmediateInvocations() const {
    assert(!ExprEvalContexts.empty() &&
           "Must be in an expression evaluation context");
    std::optional<ExpressionEvaluationContextRecord::InitializationContext> Res;
    for (auto &Ctx : llvm::reverse(ExprEvalContexts)) {
      if (Ctx.Context == ExpressionEvaluationContext::PotentiallyEvaluated &&
          !Ctx.DelayedDefaultInitializationContext && Res)
        break;
      if (Ctx.isConstantEvaluated() || Ctx.isImmediateFunctionContext() ||
          Ctx.isUnevaluated())
        break;
      Res = Ctx.DelayedDefaultInitializationContext;
    }
    return Res;
  }

  /// keepInLifetimeExtendingContext - Pull down InLifetimeExtendingContext
  /// flag from previous context.
  void keepInLifetimeExtendingContext() {
    if (ExprEvalContexts.size() > 2 &&
        parentEvaluationContext().InLifetimeExtendingContext) {
      auto &LastRecord = ExprEvalContexts.back();
      auto &PrevRecord = parentEvaluationContext();
      LastRecord.InLifetimeExtendingContext =
          PrevRecord.InLifetimeExtendingContext;
    }
  }

  DefaultedComparisonKind getDefaultedComparisonKind(const FunctionDecl *FD) {
    return getDefaultedFunctionKind(FD).asComparison();
  }

  /// Returns a field in a CXXRecordDecl that has the same name as the decl \p
  /// SelfAssigned when inside a CXXMethodDecl.
  const FieldDecl *
  getSelfAssignmentClassMemberCandidate(const ValueDecl *SelfAssigned);

  void MaybeSuggestAddingStaticToDecl(const FunctionDecl *D);

  template <typename... Ts>
  bool RequireCompleteSizedType(SourceLocation Loc, QualType T, unsigned DiagID,
                                const Ts &...Args) {
    SizelessTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteType(Loc, T, CompleteTypeKind::Normal, Diagnoser);
  }

  template <typename... Ts>
  bool RequireCompleteSizedExprType(Expr *E, unsigned DiagID,
                                    const Ts &...Args) {
    SizelessTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteExprType(E, CompleteTypeKind::Normal, Diagnoser);
  }

  /// Abstract class used to diagnose incomplete types.
  struct TypeDiagnoser {
    TypeDiagnoser() {}

    virtual void diagnose(Sema &S, SourceLocation Loc, QualType T) = 0;
    virtual ~TypeDiagnoser() {}
  };

  template <typename... Ts> class BoundTypeDiagnoser : public TypeDiagnoser {
  protected:
    unsigned DiagID;
    std::tuple<const Ts &...> Args;

    template <std::size_t... Is>
    void emit(const SemaDiagnosticBuilder &DB,
              std::index_sequence<Is...>) const {
      // Apply all tuple elements to the builder in order.
      bool Dummy[] = {false, (DB << getPrintable(std::get<Is>(Args)))...};
      (void)Dummy;
    }

  public:
    BoundTypeDiagnoser(unsigned DiagID, const Ts &...Args)
        : TypeDiagnoser(), DiagID(DiagID), Args(Args...) {
      assert(DiagID != 0 && "no diagnostic for type diagnoser");
    }

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      const SemaDiagnosticBuilder &DB = S.Diag(Loc, DiagID);
      emit(DB, std::index_sequence_for<Ts...>());
      DB << T;
    }
  };

  /// A derivative of BoundTypeDiagnoser for which the diagnostic's type
  /// parameter is preceded by a 0/1 enum that is 1 if the type is sizeless.
  /// For example, a diagnostic with no other parameters would generally have
  /// the form "...%select{incomplete|sizeless}0 type %1...".
  template <typename... Ts>
  class SizelessTypeDiagnoser : public BoundTypeDiagnoser<Ts...> {
  public:
    SizelessTypeDiagnoser(unsigned DiagID, const Ts &...Args)
        : BoundTypeDiagnoser<Ts...>(DiagID, Args...) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      const SemaDiagnosticBuilder &DB = S.Diag(Loc, this->DiagID);
      this->emit(DB, std::index_sequence_for<Ts...>());
      DB << T->isSizelessType() << T;
    }
  };

  /// Check an argument list for placeholders that we won't try to
  /// handle later.
  bool CheckArgsForPlaceholders(MultiExprArg args);

  /// The C++ "std::source_location::__impl" struct, defined in
  /// \<source_location>.
  RecordDecl *StdSourceLocationImplDecl;

  /// A stack of expression evaluation contexts.
  SmallVector<ExpressionEvaluationContextRecord, 8> ExprEvalContexts;

  // Set of failed immediate invocations to avoid double diagnosing.
  llvm::SmallPtrSet<ConstantExpr *, 4> FailedImmediateInvocations;

  /// List of SourceLocations where 'self' is implicitly retained inside a
  /// block.
  llvm::SmallVector<std::pair<SourceLocation, const BlockDecl *>, 1>
      ImplicitlyRetainedSelfLocs;

  /// Do an explicit extend of the given block pointer if we're in ARC.
  void maybeExtendBlockObject(ExprResult &E);

private:
  static BinaryOperatorKind ConvertTokenKindToBinaryOpcode(tok::TokenKind Kind);

  /// Methods for marking which expressions involve dereferencing a pointer
  /// marked with the 'noderef' attribute. Expressions are checked bottom up as
  /// they are parsed, meaning that a noderef pointer may not be accessed. For
  /// example, in `&*p` where `p` is a noderef pointer, we will first parse the
  /// `*p`, but need to check that `address of` is called on it. This requires
  /// keeping a container of all pending expressions and checking if the address
  /// of them are eventually taken.
  void CheckSubscriptAccessOfNoDeref(const ArraySubscriptExpr *E);
  void CheckAddressOfNoDeref(const Expr *E);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Expressions
  /// Implementations are in SemaExprCXX.cpp
  ///@{

public:
  /// The C++ "std::bad_alloc" class, which is defined by the C++
  /// standard library.
  LazyDeclPtr StdBadAlloc;

  /// The C++ "std::align_val_t" enum class, which is defined by the C++
  /// standard library.
  LazyDeclPtr StdAlignValT;

  /// The C++ "type_info" declaration, which is defined in \<typeinfo>.
  RecordDecl *CXXTypeInfoDecl;

  /// A flag to remember whether the implicit forms of operator new and delete
  /// have been declared.
  bool GlobalNewDeleteDeclared;

  /// Delete-expressions to be analyzed at the end of translation unit
  ///
  /// This list contains class members, and locations of delete-expressions
  /// that could not be proven as to whether they mismatch with new-expression
  /// used in initializer of the field.
  llvm::MapVector<FieldDecl *, DeleteLocs> DeleteExprs;

  /// Handle the result of the special case name lookup for inheriting
  /// constructor declarations. 'NS::X::X' and 'NS::X<...>::X' are treated as
  /// constructor names in member using declarations, even if 'X' is not the
  /// name of the corresponding type.
  ParsedType getInheritingConstructorName(CXXScopeSpec &SS,
                                          SourceLocation NameLoc,
                                          const IdentifierInfo &Name);

  ParsedType getConstructorName(const IdentifierInfo &II,
                                SourceLocation NameLoc, Scope *S,
                                CXXScopeSpec &SS, bool EnteringContext);
  ParsedType getDestructorName(const IdentifierInfo &II, SourceLocation NameLoc,
                               Scope *S, CXXScopeSpec &SS,
                               ParsedType ObjectType, bool EnteringContext);

  ParsedType getDestructorTypeForDecltype(const DeclSpec &DS,
                                          ParsedType ObjectType);

  /// Build a C++ typeid expression with a type operand.
  ExprResult BuildCXXTypeId(QualType TypeInfoType, SourceLocation TypeidLoc,
                            TypeSourceInfo *Operand, SourceLocation RParenLoc);

  /// Build a C++ typeid expression with an expression operand.
  ExprResult BuildCXXTypeId(QualType TypeInfoType, SourceLocation TypeidLoc,
                            Expr *Operand, SourceLocation RParenLoc);

  /// ActOnCXXTypeid - Parse typeid( something ).
  ExprResult ActOnCXXTypeid(SourceLocation OpLoc, SourceLocation LParenLoc,
                            bool isType, void *TyOrExpr,
                            SourceLocation RParenLoc);

  /// Build a Microsoft __uuidof expression with a type operand.
  ExprResult BuildCXXUuidof(QualType TypeInfoType, SourceLocation TypeidLoc,
                            TypeSourceInfo *Operand, SourceLocation RParenLoc);

  /// Build a Microsoft __uuidof expression with an expression operand.
  ExprResult BuildCXXUuidof(QualType TypeInfoType, SourceLocation TypeidLoc,
                            Expr *Operand, SourceLocation RParenLoc);

  /// ActOnCXXUuidof - Parse __uuidof( something ).
  ExprResult ActOnCXXUuidof(SourceLocation OpLoc, SourceLocation LParenLoc,
                            bool isType, void *TyOrExpr,
                            SourceLocation RParenLoc);

  //// ActOnCXXThis -  Parse 'this' pointer.
  ExprResult ActOnCXXThis(SourceLocation Loc);

  /// Check whether the type of 'this' is valid in the current context.
  bool CheckCXXThisType(SourceLocation Loc, QualType Type);

  /// Build a CXXThisExpr and mark it referenced in the current context.
  Expr *BuildCXXThisExpr(SourceLocation Loc, QualType Type, bool IsImplicit);
  void MarkThisReferenced(CXXThisExpr *This);

  /// Try to retrieve the type of the 'this' pointer.
  ///
  /// \returns The type of 'this', if possible. Otherwise, returns a NULL type.
  QualType getCurrentThisType();

  /// When non-NULL, the C++ 'this' expression is allowed despite the
  /// current context not being a non-static member function. In such cases,
  /// this provides the type used for 'this'.
  QualType CXXThisTypeOverride;

  /// RAII object used to temporarily allow the C++ 'this' expression
  /// to be used, with the given qualifiers on the current class type.
  class CXXThisScopeRAII {
    Sema &S;
    QualType OldCXXThisTypeOverride;
    bool Enabled;

  public:
    /// Introduce a new scope where 'this' may be allowed (when enabled),
    /// using the given declaration (which is either a class template or a
    /// class) along with the given qualifiers.
    /// along with the qualifiers placed on '*this'.
    CXXThisScopeRAII(Sema &S, Decl *ContextDecl, Qualifiers CXXThisTypeQuals,
                     bool Enabled = true);

    ~CXXThisScopeRAII();
  };

  /// Make sure the value of 'this' is actually available in the current
  /// context, if it is a potentially evaluated context.
  ///
  /// \param Loc The location at which the capture of 'this' occurs.
  ///
  /// \param Explicit Whether 'this' is explicitly captured in a lambda
  /// capture list.
  ///
  /// \param FunctionScopeIndexToStopAt If non-null, it points to the index
  /// of the FunctionScopeInfo stack beyond which we do not attempt to capture.
  /// This is useful when enclosing lambdas must speculatively capture
  /// 'this' that may or may not be used in certain specializations of
  /// a nested generic lambda (depending on whether the name resolves to
  /// a non-static member function or a static function).
  /// \return returns 'true' if failed, 'false' if success.
  bool CheckCXXThisCapture(
      SourceLocation Loc, bool Explicit = false, bool BuildAndDiagnose = true,
      const unsigned *const FunctionScopeIndexToStopAt = nullptr,
      bool ByCopy = false);

  /// Determine whether the given type is the type of *this that is used
  /// outside of the body of a member function for a type that is currently
  /// being defined.
  bool isThisOutsideMemberFunctionBody(QualType BaseType);

  /// ActOnCXXBoolLiteral - Parse {true,false} literals.
  ExprResult ActOnCXXBoolLiteral(SourceLocation OpLoc, tok::TokenKind Kind);

  /// ActOnCXXNullPtrLiteral - Parse 'nullptr'.
  ExprResult ActOnCXXNullPtrLiteral(SourceLocation Loc);

  //// ActOnCXXThrow -  Parse throw expressions.
  ExprResult ActOnCXXThrow(Scope *S, SourceLocation OpLoc, Expr *expr);
  ExprResult BuildCXXThrow(SourceLocation OpLoc, Expr *Ex,
                           bool IsThrownVarInScope);

  /// CheckCXXThrowOperand - Validate the operand of a throw.
  bool CheckCXXThrowOperand(SourceLocation ThrowLoc, QualType ThrowTy, Expr *E);

  /// ActOnCXXTypeConstructExpr - Parse construction of a specified type.
  /// Can be interpreted either as function-style casting ("int(x)")
  /// or class type construction ("ClassType(x,y,z)")
  /// or creation of a value-initialized type ("int()").
  ExprResult ActOnCXXTypeConstructExpr(ParsedType TypeRep,
                                       SourceLocation LParenOrBraceLoc,
                                       MultiExprArg Exprs,
                                       SourceLocation RParenOrBraceLoc,
                                       bool ListInitialization);

  ExprResult BuildCXXTypeConstructExpr(TypeSourceInfo *Type,
                                       SourceLocation LParenLoc,
                                       MultiExprArg Exprs,
                                       SourceLocation RParenLoc,
                                       bool ListInitialization);

  /// Parsed a C++ 'new' expression (C++ 5.3.4).
  ///
  /// E.g.:
  /// @code new (memory) int[size][4] @endcode
  /// or
  /// @code ::new Foo(23, "hello") @endcode
  ///
  /// \param StartLoc The first location of the expression.
  /// \param UseGlobal True if 'new' was prefixed with '::'.
  /// \param PlacementLParen Opening paren of the placement arguments.
  /// \param PlacementArgs Placement new arguments.
  /// \param PlacementRParen Closing paren of the placement arguments.
  /// \param TypeIdParens If the type is in parens, the source range.
  /// \param D The type to be allocated, as well as array dimensions.
  /// \param Initializer The initializing expression or initializer-list, or
  ///   null if there is none.
  ExprResult ActOnCXXNew(SourceLocation StartLoc, bool UseGlobal,
                         SourceLocation PlacementLParen,
                         MultiExprArg PlacementArgs,
                         SourceLocation PlacementRParen,
                         SourceRange TypeIdParens, Declarator &D,
                         Expr *Initializer);
  ExprResult
  BuildCXXNew(SourceRange Range, bool UseGlobal, SourceLocation PlacementLParen,
              MultiExprArg PlacementArgs, SourceLocation PlacementRParen,
              SourceRange TypeIdParens, QualType AllocType,
              TypeSourceInfo *AllocTypeInfo, std::optional<Expr *> ArraySize,
              SourceRange DirectInitRange, Expr *Initializer);

  /// Determine whether \p FD is an aligned allocation or deallocation
  /// function that is unavailable.
  bool isUnavailableAlignedAllocationFunction(const FunctionDecl &FD) const;

  /// Produce diagnostics if \p FD is an aligned allocation or deallocation
  /// function that is unavailable.
  void diagnoseUnavailableAlignedAllocation(const FunctionDecl &FD,
                                            SourceLocation Loc);

  /// Checks that a type is suitable as the allocated type
  /// in a new-expression.
  bool CheckAllocatedType(QualType AllocType, SourceLocation Loc,
                          SourceRange R);

  /// The scope in which to find allocation functions.
  enum AllocationFunctionScope {
    /// Only look for allocation functions in the global scope.
    AFS_Global,
    /// Only look for allocation functions in the scope of the
    /// allocated class.
    AFS_Class,
    /// Look for allocation functions in both the global scope
    /// and in the scope of the allocated class.
    AFS_Both
  };

  /// Finds the overloads of operator new and delete that are appropriate
  /// for the allocation.
  bool FindAllocationFunctions(SourceLocation StartLoc, SourceRange Range,
                               AllocationFunctionScope NewScope,
                               AllocationFunctionScope DeleteScope,
                               QualType AllocType, bool IsArray,
                               bool &PassAlignment, MultiExprArg PlaceArgs,
                               FunctionDecl *&OperatorNew,
                               FunctionDecl *&OperatorDelete,
                               bool Diagnose = true);

  /// DeclareGlobalNewDelete - Declare the global forms of operator new and
  /// delete. These are:
  /// @code
  ///   // C++03:
  ///   void* operator new(std::size_t) throw(std::bad_alloc);
  ///   void* operator new[](std::size_t) throw(std::bad_alloc);
  ///   void operator delete(void *) throw();
  ///   void operator delete[](void *) throw();
  ///   // C++11:
  ///   void* operator new(std::size_t);
  ///   void* operator new[](std::size_t);
  ///   void operator delete(void *) noexcept;
  ///   void operator delete[](void *) noexcept;
  ///   // C++1y:
  ///   void* operator new(std::size_t);
  ///   void* operator new[](std::size_t);
  ///   void operator delete(void *) noexcept;
  ///   void operator delete[](void *) noexcept;
  ///   void operator delete(void *, std::size_t) noexcept;
  ///   void operator delete[](void *, std::size_t) noexcept;
  /// @endcode
  /// Note that the placement and nothrow forms of new are *not* implicitly
  /// declared. Their use requires including \<new\>.
  void DeclareGlobalNewDelete();
  void DeclareGlobalAllocationFunction(DeclarationName Name, QualType Return,
                                       ArrayRef<QualType> Params);

  bool FindDeallocationFunction(SourceLocation StartLoc, CXXRecordDecl *RD,
                                DeclarationName Name, FunctionDecl *&Operator,
                                bool Diagnose = true, bool WantSize = false,
                                bool WantAligned = false);
  FunctionDecl *FindUsualDeallocationFunction(SourceLocation StartLoc,
                                              bool CanProvideSize,
                                              bool Overaligned,
                                              DeclarationName Name);
  FunctionDecl *FindDeallocationFunctionForDestructor(SourceLocation StartLoc,
                                                      CXXRecordDecl *RD);

  /// ActOnCXXDelete - Parsed a C++ 'delete' expression (C++ 5.3.5), as in:
  /// @code ::delete ptr; @endcode
  /// or
  /// @code delete [] ptr; @endcode
  ExprResult ActOnCXXDelete(SourceLocation StartLoc, bool UseGlobal,
                            bool ArrayForm, Expr *Operand);
  void CheckVirtualDtorCall(CXXDestructorDecl *dtor, SourceLocation Loc,
                            bool IsDelete, bool CallCanBeVirtual,
                            bool WarnOnNonAbstractTypes,
                            SourceLocation DtorLoc);

  ExprResult ActOnNoexceptExpr(SourceLocation KeyLoc, SourceLocation LParen,
                               Expr *Operand, SourceLocation RParen);
  ExprResult BuildCXXNoexceptExpr(SourceLocation KeyLoc, Expr *Operand,
                                  SourceLocation RParen);

  ExprResult ActOnStartCXXMemberReference(Scope *S, Expr *Base,
                                          SourceLocation OpLoc,
                                          tok::TokenKind OpKind,
                                          ParsedType &ObjectType,
                                          bool &MayBePseudoDestructor);

  ExprResult BuildPseudoDestructorExpr(
      Expr *Base, SourceLocation OpLoc, tok::TokenKind OpKind,
      const CXXScopeSpec &SS, TypeSourceInfo *ScopeType, SourceLocation CCLoc,
      SourceLocation TildeLoc, PseudoDestructorTypeStorage DestroyedType);

  ExprResult ActOnPseudoDestructorExpr(
      Scope *S, Expr *Base, SourceLocation OpLoc, tok::TokenKind OpKind,
      CXXScopeSpec &SS, UnqualifiedId &FirstTypeName, SourceLocation CCLoc,
      SourceLocation TildeLoc, UnqualifiedId &SecondTypeName);

  ExprResult ActOnPseudoDestructorExpr(Scope *S, Expr *Base,
                                       SourceLocation OpLoc,
                                       tok::TokenKind OpKind,
                                       SourceLocation TildeLoc,
                                       const DeclSpec &DS);

  /// MaybeCreateExprWithCleanups - If the current full-expression
  /// requires any cleanups, surround it with a ExprWithCleanups node.
  /// Otherwise, just returns the passed-in expression.
  Expr *MaybeCreateExprWithCleanups(Expr *SubExpr);
  Stmt *MaybeCreateStmtWithCleanups(Stmt *SubStmt);
  ExprResult MaybeCreateExprWithCleanups(ExprResult SubExpr);

  ExprResult ActOnFinishFullExpr(Expr *Expr, bool DiscardedValue) {
    return ActOnFinishFullExpr(
        Expr, Expr ? Expr->getExprLoc() : SourceLocation(), DiscardedValue);
  }
  ExprResult ActOnFinishFullExpr(Expr *Expr, SourceLocation CC,
                                 bool DiscardedValue, bool IsConstexpr = false,
                                 bool IsTemplateArgument = false);
  StmtResult ActOnFinishFullStmt(Stmt *Stmt);

  /// Process the expression contained within a decltype. For such expressions,
  /// certain semantic checks on temporaries are delayed until this point, and
  /// are omitted for the 'topmost' call in the decltype expression. If the
  /// topmost call bound a temporary, strip that temporary off the expression.
  ExprResult ActOnDecltypeExpression(Expr *E);

  bool checkLiteralOperatorId(const CXXScopeSpec &SS, const UnqualifiedId &Id,
                              bool IsUDSuffix);

  bool isUsualDeallocationFunction(const CXXMethodDecl *FD);

  ConditionResult ActOnConditionVariable(Decl *ConditionVar,
                                         SourceLocation StmtLoc,
                                         ConditionKind CK);

  /// Check the use of the given variable as a C++ condition in an if,
  /// while, do-while, or switch statement.
  ExprResult CheckConditionVariable(VarDecl *ConditionVar,
                                    SourceLocation StmtLoc, ConditionKind CK);

  /// CheckCXXBooleanCondition - Returns true if conversion to bool is invalid.
  ExprResult CheckCXXBooleanCondition(Expr *CondExpr, bool IsConstexpr = false);

  /// Helper function to determine whether this is the (deprecated) C++
  /// conversion from a string literal to a pointer to non-const char or
  /// non-const wchar_t (for narrow and wide string literals,
  /// respectively).
  bool IsStringLiteralToNonConstPointerConversion(Expr *From, QualType ToType);

  /// PerformImplicitConversion - Perform an implicit conversion of the
  /// expression From to the type ToType using the pre-computed implicit
  /// conversion sequence ICS. Returns the converted
  /// expression. Action is the kind of conversion we're performing,
  /// used in the error message.
  ExprResult PerformImplicitConversion(
      Expr *From, QualType ToType, const ImplicitConversionSequence &ICS,
      AssignmentAction Action,
      CheckedConversionKind CCK = CheckedConversionKind::Implicit);

  /// PerformImplicitConversion - Perform an implicit conversion of the
  /// expression From to the type ToType by following the standard
  /// conversion sequence SCS. Returns the converted
  /// expression. Flavor is the context in which we're performing this
  /// conversion, for use in error messages.
  ExprResult PerformImplicitConversion(Expr *From, QualType ToType,
                                       const StandardConversionSequence &SCS,
                                       AssignmentAction Action,
                                       CheckedConversionKind CCK);

  bool CheckTypeTraitArity(unsigned Arity, SourceLocation Loc, size_t N);

  /// Parsed one of the type trait support pseudo-functions.
  ExprResult ActOnTypeTrait(TypeTrait Kind, SourceLocation KWLoc,
                            ArrayRef<ParsedType> Args,
                            SourceLocation RParenLoc);
  ExprResult BuildTypeTrait(TypeTrait Kind, SourceLocation KWLoc,
                            ArrayRef<TypeSourceInfo *> Args,
                            SourceLocation RParenLoc);

  /// ActOnArrayTypeTrait - Parsed one of the binary type trait support
  /// pseudo-functions.
  ExprResult ActOnArrayTypeTrait(ArrayTypeTrait ATT, SourceLocation KWLoc,
                                 ParsedType LhsTy, Expr *DimExpr,
                                 SourceLocation RParen);

  ExprResult BuildArrayTypeTrait(ArrayTypeTrait ATT, SourceLocation KWLoc,
                                 TypeSourceInfo *TSInfo, Expr *DimExpr,
                                 SourceLocation RParen);

  /// ActOnExpressionTrait - Parsed one of the unary type trait support
  /// pseudo-functions.
  ExprResult ActOnExpressionTrait(ExpressionTrait OET, SourceLocation KWLoc,
                                  Expr *Queried, SourceLocation RParen);

  ExprResult BuildExpressionTrait(ExpressionTrait OET, SourceLocation KWLoc,
                                  Expr *Queried, SourceLocation RParen);

  QualType CheckPointerToMemberOperands( // C++ 5.5
      ExprResult &LHS, ExprResult &RHS, ExprValueKind &VK, SourceLocation OpLoc,
      bool isIndirect);
  QualType CheckVectorConditionalTypes(ExprResult &Cond, ExprResult &LHS,
                                       ExprResult &RHS,
                                       SourceLocation QuestionLoc);

  QualType CheckSizelessVectorConditionalTypes(ExprResult &Cond,
                                               ExprResult &LHS, ExprResult &RHS,
                                               SourceLocation QuestionLoc);

  /// Check the operands of ?: under C++ semantics.
  ///
  /// See C++ [expr.cond]. Note that LHS is never null, even for the GNU x ?: y
  /// extension. In this case, LHS == Cond. (But they're not aliases.)
  ///
  /// This function also implements GCC's vector extension and the
  /// OpenCL/ext_vector_type extension for conditionals. The vector extensions
  /// permit the use of a?b:c where the type of a is that of a integer vector
  /// with the same number of elements and size as the vectors of b and c. If
  /// one of either b or c is a scalar it is implicitly converted to match the
  /// type of the vector. Otherwise the expression is ill-formed. If both b and
  /// c are scalars, then b and c are checked and converted to the type of a if
  /// possible.
  ///
  /// The expressions are evaluated differently for GCC's and OpenCL's
  /// extensions. For the GCC extension, the ?: operator is evaluated as
  ///   (a[0] != 0 ? b[0] : c[0], .. , a[n] != 0 ? b[n] : c[n]).
  /// For the OpenCL extensions, the ?: operator is evaluated as
  ///   (most-significant-bit-set(a[0])  ? b[0] : c[0], .. ,
  ///    most-significant-bit-set(a[n]) ? b[n] : c[n]).
  QualType CXXCheckConditionalOperands( // C++ 5.16
      ExprResult &cond, ExprResult &lhs, ExprResult &rhs, ExprValueKind &VK,
      ExprObjectKind &OK, SourceLocation questionLoc);

  /// Find a merged pointer type and convert the two expressions to it.
  ///
  /// This finds the composite pointer type for \p E1 and \p E2 according to
  /// C++2a [expr.type]p3. It converts both expressions to this type and returns
  /// it.  It does not emit diagnostics (FIXME: that's not true if \p
  /// ConvertArgs is \c true).
  ///
  /// \param Loc The location of the operator requiring these two expressions to
  /// be converted to the composite pointer type.
  ///
  /// \param ConvertArgs If \c false, do not convert E1 and E2 to the target
  /// type.
  QualType FindCompositePointerType(SourceLocation Loc, Expr *&E1, Expr *&E2,
                                    bool ConvertArgs = true);
  QualType FindCompositePointerType(SourceLocation Loc, ExprResult &E1,
                                    ExprResult &E2, bool ConvertArgs = true) {
    Expr *E1Tmp = E1.get(), *E2Tmp = E2.get();
    QualType Composite =
        FindCompositePointerType(Loc, E1Tmp, E2Tmp, ConvertArgs);
    E1 = E1Tmp;
    E2 = E2Tmp;
    return Composite;
  }

  /// MaybeBindToTemporary - If the passed in expression has a record type with
  /// a non-trivial destructor, this will return CXXBindTemporaryExpr. Otherwise
  /// it simply returns the passed in expression.
  ExprResult MaybeBindToTemporary(Expr *E);

  /// IgnoredValueConversions - Given that an expression's result is
  /// syntactically ignored, perform any conversions that are
  /// required.
  ExprResult IgnoredValueConversions(Expr *E);

  ExprResult CheckUnevaluatedOperand(Expr *E);

  /// Process any TypoExprs in the given Expr and its children,
  /// generating diagnostics as appropriate and returning a new Expr if there
  /// were typos that were all successfully corrected and ExprError if one or
  /// more typos could not be corrected.
  ///
  /// \param E The Expr to check for TypoExprs.
  ///
  /// \param InitDecl A VarDecl to avoid because the Expr being corrected is its
  /// initializer.
  ///
  /// \param RecoverUncorrectedTypos If true, when typo correction fails, it
  /// will rebuild the given Expr with all TypoExprs degraded to RecoveryExprs.
  ///
  /// \param Filter A function applied to a newly rebuilt Expr to determine if
  /// it is an acceptable/usable result from a single combination of typo
  /// corrections. As long as the filter returns ExprError, different
  /// combinations of corrections will be tried until all are exhausted.
  ExprResult CorrectDelayedTyposInExpr(
      Expr *E, VarDecl *InitDecl = nullptr,
      bool RecoverUncorrectedTypos = false,
      llvm::function_ref<ExprResult(Expr *)> Filter =
          [](Expr *E) -> ExprResult { return E; });

  ExprResult CorrectDelayedTyposInExpr(
      ExprResult ER, VarDecl *InitDecl = nullptr,
      bool RecoverUncorrectedTypos = false,
      llvm::function_ref<ExprResult(Expr *)> Filter =
          [](Expr *E) -> ExprResult { return E; }) {
    return ER.isInvalid()
               ? ER
               : CorrectDelayedTyposInExpr(ER.get(), InitDecl,
                                           RecoverUncorrectedTypos, Filter);
  }

  /// Describes the result of an "if-exists" condition check.
  enum IfExistsResult {
    /// The symbol exists.
    IER_Exists,

    /// The symbol does not exist.
    IER_DoesNotExist,

    /// The name is a dependent name, so the results will differ
    /// from one instantiation to the next.
    IER_Dependent,

    /// An error occurred.
    IER_Error
  };

  IfExistsResult
  CheckMicrosoftIfExistsSymbol(Scope *S, CXXScopeSpec &SS,
                               const DeclarationNameInfo &TargetNameInfo);

  IfExistsResult CheckMicrosoftIfExistsSymbol(Scope *S,
                                              SourceLocation KeywordLoc,
                                              bool IsIfExists, CXXScopeSpec &SS,
                                              UnqualifiedId &Name);

  RequiresExprBodyDecl *
  ActOnStartRequiresExpr(SourceLocation RequiresKWLoc,
                         ArrayRef<ParmVarDecl *> LocalParameters,
                         Scope *BodyScope);
  void ActOnFinishRequiresExpr();
  concepts::Requirement *ActOnSimpleRequirement(Expr *E);
  concepts::Requirement *ActOnTypeRequirement(SourceLocation TypenameKWLoc,
                                              CXXScopeSpec &SS,
                                              SourceLocation NameLoc,
                                              const IdentifierInfo *TypeName,
                                              TemplateIdAnnotation *TemplateId);
  concepts::Requirement *ActOnCompoundRequirement(Expr *E,
                                                  SourceLocation NoexceptLoc);
  concepts::Requirement *ActOnCompoundRequirement(
      Expr *E, SourceLocation NoexceptLoc, CXXScopeSpec &SS,
      TemplateIdAnnotation *TypeConstraint, unsigned Depth);
  concepts::Requirement *ActOnNestedRequirement(Expr *Constraint);
  concepts::ExprRequirement *BuildExprRequirement(
      Expr *E, bool IsSatisfied, SourceLocation NoexceptLoc,
      concepts::ExprRequirement::ReturnTypeRequirement ReturnTypeRequirement);
  concepts::ExprRequirement *BuildExprRequirement(
      concepts::Requirement::SubstitutionDiagnostic *ExprSubstDiag,
      bool IsSatisfied, SourceLocation NoexceptLoc,
      concepts::ExprRequirement::ReturnTypeRequirement ReturnTypeRequirement);
  concepts::TypeRequirement *BuildTypeRequirement(TypeSourceInfo *Type);
  concepts::TypeRequirement *BuildTypeRequirement(
      concepts::Requirement::SubstitutionDiagnostic *SubstDiag);
  concepts::NestedRequirement *BuildNestedRequirement(Expr *E);
  concepts::NestedRequirement *
  BuildNestedRequirement(StringRef InvalidConstraintEntity,
                         const ASTConstraintSatisfaction &Satisfaction);
  ExprResult ActOnRequiresExpr(SourceLocation RequiresKWLoc,
                               RequiresExprBodyDecl *Body,
                               SourceLocation LParenLoc,
                               ArrayRef<ParmVarDecl *> LocalParameters,
                               SourceLocation RParenLoc,
                               ArrayRef<concepts::Requirement *> Requirements,
                               SourceLocation ClosingBraceLoc);

private:
  ExprResult BuiltinOperatorNewDeleteOverloaded(ExprResult TheCallResult,
                                                bool IsDelete);

  void AnalyzeDeleteExprMismatch(const CXXDeleteExpr *DE);
  void AnalyzeDeleteExprMismatch(FieldDecl *Field, SourceLocation DeleteLoc,
                                 bool DeleteWasArrayForm);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Member Access Expressions
  /// Implementations are in SemaExprMember.cpp
  ///@{

public:
  /// Check whether an expression might be an implicit class member access.
  bool isPotentialImplicitMemberAccess(const CXXScopeSpec &SS, LookupResult &R,
                                       bool IsAddressOfOperand);

  /// Builds an expression which might be an implicit member expression.
  ExprResult BuildPossibleImplicitMemberExpr(
      const CXXScopeSpec &SS, SourceLocation TemplateKWLoc, LookupResult &R,
      const TemplateArgumentListInfo *TemplateArgs, const Scope *S);

  /// Builds an implicit member access expression.  The current context
  /// is known to be an instance method, and the given unqualified lookup
  /// set is known to contain only instance members, at least one of which
  /// is from an appropriate type.
  ExprResult
  BuildImplicitMemberExpr(const CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
                          LookupResult &R,
                          const TemplateArgumentListInfo *TemplateArgs,
                          bool IsDefiniteInstance, const Scope *S);

  ExprResult ActOnDependentMemberExpr(
      Expr *Base, QualType BaseType, bool IsArrow, SourceLocation OpLoc,
      const CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
      NamedDecl *FirstQualifierInScope, const DeclarationNameInfo &NameInfo,
      const TemplateArgumentListInfo *TemplateArgs);

  /// The main callback when the parser finds something like
  ///   expression . [nested-name-specifier] identifier
  ///   expression -> [nested-name-specifier] identifier
  /// where 'identifier' encompasses a fairly broad spectrum of
  /// possibilities, including destructor and operator references.
  ///
  /// \param OpKind either tok::arrow or tok::period
  /// \param ObjCImpDecl the current Objective-C \@implementation
  ///   decl; this is an ugly hack around the fact that Objective-C
  ///   \@implementations aren't properly put in the context chain
  ExprResult ActOnMemberAccessExpr(Scope *S, Expr *Base, SourceLocation OpLoc,
                                   tok::TokenKind OpKind, CXXScopeSpec &SS,
                                   SourceLocation TemplateKWLoc,
                                   UnqualifiedId &Member, Decl *ObjCImpDecl);

  MemberExpr *
  BuildMemberExpr(Expr *Base, bool IsArrow, SourceLocation OpLoc,
                  NestedNameSpecifierLoc NNS, SourceLocation TemplateKWLoc,
                  ValueDecl *Member, DeclAccessPair FoundDecl,
                  bool HadMultipleCandidates,
                  const DeclarationNameInfo &MemberNameInfo, QualType Ty,
                  ExprValueKind VK, ExprObjectKind OK,
                  const TemplateArgumentListInfo *TemplateArgs = nullptr);

  // Check whether the declarations we found through a nested-name
  // specifier in a member expression are actually members of the base
  // type.  The restriction here is:
  //
  //   C++ [expr.ref]p2:
  //     ... In these cases, the id-expression shall name a
  //     member of the class or of one of its base classes.
  //
  // So it's perfectly legitimate for the nested-name specifier to name
  // an unrelated class, and for us to find an overload set including
  // decls from classes which are not superclasses, as long as the decl
  // we actually pick through overload resolution is from a superclass.
  bool CheckQualifiedMemberReference(Expr *BaseExpr, QualType BaseType,
                                     const CXXScopeSpec &SS,
                                     const LookupResult &R);

  // This struct is for use by ActOnMemberAccess to allow
  // BuildMemberReferenceExpr to be able to reinvoke ActOnMemberAccess after
  // changing the access operator from a '.' to a '->' (to see if that is the
  // change needed to fix an error about an unknown member, e.g. when the class
  // defines a custom operator->).
  struct ActOnMemberAccessExtraArgs {
    Scope *S;
    UnqualifiedId &Id;
    Decl *ObjCImpDecl;
  };

  ExprResult BuildMemberReferenceExpr(
      Expr *Base, QualType BaseType, SourceLocation OpLoc, bool IsArrow,
      CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
      NamedDecl *FirstQualifierInScope, const DeclarationNameInfo &NameInfo,
      const TemplateArgumentListInfo *TemplateArgs, const Scope *S,
      ActOnMemberAccessExtraArgs *ExtraArgs = nullptr);

  ExprResult
  BuildMemberReferenceExpr(Expr *Base, QualType BaseType, SourceLocation OpLoc,
                           bool IsArrow, const CXXScopeSpec &SS,
                           SourceLocation TemplateKWLoc,
                           NamedDecl *FirstQualifierInScope, LookupResult &R,
                           const TemplateArgumentListInfo *TemplateArgs,
                           const Scope *S, bool SuppressQualifierCheck = false,
                           ActOnMemberAccessExtraArgs *ExtraArgs = nullptr);

  ExprResult BuildFieldReferenceExpr(Expr *BaseExpr, bool IsArrow,
                                     SourceLocation OpLoc,
                                     const CXXScopeSpec &SS, FieldDecl *Field,
                                     DeclAccessPair FoundDecl,
                                     const DeclarationNameInfo &MemberNameInfo);

  /// Perform conversions on the LHS of a member access expression.
  ExprResult PerformMemberExprBaseConversion(Expr *Base, bool IsArrow);

  ExprResult BuildAnonymousStructUnionMemberReference(
      const CXXScopeSpec &SS, SourceLocation nameLoc,
      IndirectFieldDecl *indirectField,
      DeclAccessPair FoundDecl = DeclAccessPair::make(nullptr, AS_none),
      Expr *baseObjectExpr = nullptr, SourceLocation opLoc = SourceLocation());

private:
  void CheckMemberAccessOfNoDeref(const MemberExpr *E);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Initializers
  /// Implementations are in SemaInit.cpp
  ///@{

public:
  /// Stack of types that correspond to the parameter entities that are
  /// currently being copy-initialized. Can be empty.
  llvm::SmallVector<QualType, 4> CurrentParameterCopyTypes;

  llvm::DenseMap<unsigned, CXXDeductionGuideDecl *>
      AggregateDeductionCandidates;

  bool IsStringInit(Expr *Init, const ArrayType *AT);

  /// Determine whether we can perform aggregate initialization for the purposes
  /// of overload resolution.
  bool CanPerformAggregateInitializationForOverloadResolution(
      const InitializedEntity &Entity, InitListExpr *From);

  ExprResult ActOnDesignatedInitializer(Designation &Desig,
                                        SourceLocation EqualOrColonLoc,
                                        bool GNUSyntax, ExprResult Init);

  /// Check that the lifetime of the initializer (and its subobjects) is
  /// sufficient for initializing the entity, and perform lifetime extension
  /// (when permitted) if not.
  void checkInitializerLifetime(const InitializedEntity &Entity, Expr *Init);

  MaterializeTemporaryExpr *
  CreateMaterializeTemporaryExpr(QualType T, Expr *Temporary,
                                 bool BoundToLvalueReference);

  /// If \p E is a prvalue denoting an unmaterialized temporary, materialize
  /// it as an xvalue. In C++98, the result will still be a prvalue, because
  /// we don't have xvalues there.
  ExprResult TemporaryMaterializationConversion(Expr *E);

  ExprResult PerformQualificationConversion(
      Expr *E, QualType Ty, ExprValueKind VK = VK_PRValue,
      CheckedConversionKind CCK = CheckedConversionKind::Implicit);

  bool CanPerformCopyInitialization(const InitializedEntity &Entity,
                                    ExprResult Init);
  ExprResult PerformCopyInitialization(const InitializedEntity &Entity,
                                       SourceLocation EqualLoc, ExprResult Init,
                                       bool TopLevelOfInitList = false,
                                       bool AllowExplicit = false);

  QualType DeduceTemplateSpecializationFromInitializer(
      TypeSourceInfo *TInfo, const InitializedEntity &Entity,
      const InitializationKind &Kind, MultiExprArg Init);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Lambda Expressions
  /// Implementations are in SemaLambda.cpp
  ///@{

public:
  /// Create a new lambda closure type.
  CXXRecordDecl *createLambdaClosureType(SourceRange IntroducerRange,
                                         TypeSourceInfo *Info,
                                         unsigned LambdaDependencyKind,
                                         LambdaCaptureDefault CaptureDefault);

  /// Number lambda for linkage purposes if necessary.
  void handleLambdaNumbering(CXXRecordDecl *Class, CXXMethodDecl *Method,
                             std::optional<CXXRecordDecl::LambdaNumbering>
                                 NumberingOverride = std::nullopt);

  /// Endow the lambda scope info with the relevant properties.
  void buildLambdaScope(sema::LambdaScopeInfo *LSI, CXXMethodDecl *CallOperator,
                        SourceRange IntroducerRange,
                        LambdaCaptureDefault CaptureDefault,
                        SourceLocation CaptureDefaultLoc, bool ExplicitParams,
                        bool Mutable);

  CXXMethodDecl *CreateLambdaCallOperator(SourceRange IntroducerRange,
                                          CXXRecordDecl *Class);

  void AddTemplateParametersToLambdaCallOperator(
      CXXMethodDecl *CallOperator, CXXRecordDecl *Class,
      TemplateParameterList *TemplateParams);

  void CompleteLambdaCallOperator(
      CXXMethodDecl *Method, SourceLocation LambdaLoc,
      SourceLocation CallOperatorLoc, Expr *TrailingRequiresClause,
      TypeSourceInfo *MethodTyInfo, ConstexprSpecKind ConstexprKind,
      StorageClass SC, ArrayRef<ParmVarDecl *> Params,
      bool HasExplicitResultType);

  /// Returns true if the explicit object parameter was invalid.
  bool DiagnoseInvalidExplicitObjectParameterInLambda(CXXMethodDecl *Method,
                                                      SourceLocation CallLoc);

  /// Perform initialization analysis of the init-capture and perform
  /// any implicit conversions such as an lvalue-to-rvalue conversion if
  /// not being used to initialize a reference.
  ParsedType actOnLambdaInitCaptureInitialization(
      SourceLocation Loc, bool ByRef, SourceLocation EllipsisLoc,
      IdentifierInfo *Id, LambdaCaptureInitKind InitKind, Expr *&Init) {
    return ParsedType::make(buildLambdaInitCaptureInitialization(
        Loc, ByRef, EllipsisLoc, std::nullopt, Id,
        InitKind != LambdaCaptureInitKind::CopyInit, Init));
  }
  QualType buildLambdaInitCaptureInitialization(
      SourceLocation Loc, bool ByRef, SourceLocation EllipsisLoc,
      std::optional<unsigned> NumExpansions, IdentifierInfo *Id,
      bool DirectInit, Expr *&Init);

  /// Create a dummy variable within the declcontext of the lambda's
  ///  call operator, for name lookup purposes for a lambda init capture.
  ///
  ///  CodeGen handles emission of lambda captures, ignoring these dummy
  ///  variables appropriately.
  VarDecl *createLambdaInitCaptureVarDecl(
      SourceLocation Loc, QualType InitCaptureType, SourceLocation EllipsisLoc,
      IdentifierInfo *Id, unsigned InitStyle, Expr *Init, DeclContext *DeclCtx);

  /// Add an init-capture to a lambda scope.
  void addInitCapture(sema::LambdaScopeInfo *LSI, VarDecl *Var, bool ByRef);

  /// Note that we have finished the explicit captures for the
  /// given lambda.
  void finishLambdaExplicitCaptures(sema::LambdaScopeInfo *LSI);

  /// Deduce a block or lambda's return type based on the return
  /// statements present in the body.
  void deduceClosureReturnType(sema::CapturingScopeInfo &CSI);

  /// Once the Lambdas capture are known, we can start to create the closure,
  /// call operator method, and keep track of the captures.
  /// We do the capture lookup here, but they are not actually captured until
  /// after we know what the qualifiers of the call operator are.
  void ActOnLambdaExpressionAfterIntroducer(LambdaIntroducer &Intro,
                                            Scope *CurContext);

  /// This is called after parsing the explicit template parameter list
  /// on a lambda (if it exists) in C++2a.
  void ActOnLambdaExplicitTemplateParameterList(LambdaIntroducer &Intro,
                                                SourceLocation LAngleLoc,
                                                ArrayRef<NamedDecl *> TParams,
                                                SourceLocation RAngleLoc,
                                                ExprResult RequiresClause);

  void ActOnLambdaClosureQualifiers(LambdaIntroducer &Intro,
                                    SourceLocation MutableLoc);

  void ActOnLambdaClosureParameters(
      Scope *LambdaScope,
      MutableArrayRef<DeclaratorChunk::ParamInfo> ParamInfo);

  /// ActOnStartOfLambdaDefinition - This is called just before we start
  /// parsing the body of a lambda; it analyzes the explicit captures and
  /// arguments, and sets up various data-structures for the body of the
  /// lambda.
  void ActOnStartOfLambdaDefinition(LambdaIntroducer &Intro,
                                    Declarator &ParamInfo, const DeclSpec &DS);

  /// ActOnLambdaError - If there is an error parsing a lambda, this callback
  /// is invoked to pop the information about the lambda.
  void ActOnLambdaError(SourceLocation StartLoc, Scope *CurScope,
                        bool IsInstantiation = false);

  /// ActOnLambdaExpr - This is called when the body of a lambda expression
  /// was successfully completed.
  ExprResult ActOnLambdaExpr(SourceLocation StartLoc, Stmt *Body);

  /// Does copying/destroying the captured variable have side effects?
  bool CaptureHasSideEffects(const sema::Capture &From);

  /// Diagnose if an explicit lambda capture is unused. Returns true if a
  /// diagnostic is emitted.
  bool DiagnoseUnusedLambdaCapture(SourceRange CaptureRange,
                                   const sema::Capture &From);

  /// Build a FieldDecl suitable to hold the given capture.
  FieldDecl *BuildCaptureField(RecordDecl *RD, const sema::Capture &Capture);

  /// Initialize the given capture with a suitable expression.
  ExprResult BuildCaptureInit(const sema::Capture &Capture,
                              SourceLocation ImplicitCaptureLoc,
                              bool IsOpenMPMapping = false);

  /// Complete a lambda-expression having processed and attached the
  /// lambda body.
  ExprResult BuildLambdaExpr(SourceLocation StartLoc, SourceLocation EndLoc,
                             sema::LambdaScopeInfo *LSI);

  /// Get the return type to use for a lambda's conversion function(s) to
  /// function pointer type, given the type of the call operator.
  QualType
  getLambdaConversionFunctionResultType(const FunctionProtoType *CallOpType,
                                        CallingConv CC);

  ExprResult BuildBlockForLambdaConversion(SourceLocation CurrentLocation,
                                           SourceLocation ConvLocation,
                                           CXXConversionDecl *Conv, Expr *Src);

  class LambdaScopeForCallOperatorInstantiationRAII
      : private FunctionScopeRAII {
  public:
    LambdaScopeForCallOperatorInstantiationRAII(
        Sema &SemasRef, FunctionDecl *FD, MultiLevelTemplateArgumentList MLTAL,
        LocalInstantiationScope &Scope,
        bool ShouldAddDeclsFromParentScope = true);
  };

  /// Compute the mangling number context for a lambda expression or
  /// block literal. Also return the extra mangling decl if any.
  ///
  /// \param DC - The DeclContext containing the lambda expression or
  /// block literal.
  std::tuple<MangleNumberingContext *, Decl *>
  getCurrentMangleNumberContext(const DeclContext *DC);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Name Lookup
  ///
  /// These routines provide name lookup that is used during semantic
  /// analysis to resolve the various kinds of names (identifiers,
  /// overloaded operator names, constructor names, etc.) into zero or
  /// more declarations within a particular scope. The major entry
  /// points are LookupName, which performs unqualified name lookup,
  /// and LookupQualifiedName, which performs qualified name lookup.
  ///
  /// All name lookup is performed based on some specific criteria,
  /// which specify what names will be visible to name lookup and how
  /// far name lookup should work. These criteria are important both
  /// for capturing language semantics (certain lookups will ignore
  /// certain names, for example) and for performance, since name
  /// lookup is often a bottleneck in the compilation of C++. Name
  /// lookup criteria is specified via the LookupCriteria enumeration.
  ///
  /// The results of name lookup can vary based on the kind of name
  /// lookup performed, the current language, and the translation
  /// unit. In C, for example, name lookup will either return nothing
  /// (no entity found) or a single declaration. In C++, name lookup
  /// can additionally refer to a set of overloaded functions or
  /// result in an ambiguity. All of the possible results of name
  /// lookup are captured by the LookupResult class, which provides
  /// the ability to distinguish among them.
  ///
  /// Implementations are in SemaLookup.cpp
  ///@{

public:
  /// Tracks whether we are in a context where typo correction is
  /// disabled.
  bool DisableTypoCorrection;

  /// The number of typos corrected by CorrectTypo.
  unsigned TyposCorrected;

  typedef llvm::SmallSet<SourceLocation, 2> SrcLocSet;
  typedef llvm::DenseMap<IdentifierInfo *, SrcLocSet> IdentifierSourceLocations;

  /// A cache containing identifiers for which typo correction failed and
  /// their locations, so that repeated attempts to correct an identifier in a
  /// given location are ignored if typo correction already failed for it.
  IdentifierSourceLocations TypoCorrectionFailures;

  /// SpecialMemberOverloadResult - The overloading result for a special member
  /// function.
  ///
  /// This is basically a wrapper around PointerIntPair. The lowest bits of the
  /// integer are used to determine whether overload resolution succeeded.
  class SpecialMemberOverloadResult {
  public:
    enum Kind { NoMemberOrDeleted, Ambiguous, Success };

  private:
    llvm::PointerIntPair<CXXMethodDecl *, 2> Pair;

  public:
    SpecialMemberOverloadResult() {}
    SpecialMemberOverloadResult(CXXMethodDecl *MD)
        : Pair(MD, MD->isDeleted() ? NoMemberOrDeleted : Success) {}

    CXXMethodDecl *getMethod() const { return Pair.getPointer(); }
    void setMethod(CXXMethodDecl *MD) { Pair.setPointer(MD); }

    Kind getKind() const { return static_cast<Kind>(Pair.getInt()); }
    void setKind(Kind K) { Pair.setInt(K); }
  };

  class SpecialMemberOverloadResultEntry : public llvm::FastFoldingSetNode,
                                           public SpecialMemberOverloadResult {
  public:
    SpecialMemberOverloadResultEntry(const llvm::FoldingSetNodeID &ID)
        : FastFoldingSetNode(ID) {}
  };

  /// A cache of special member function overload resolution results
  /// for C++ records.
  llvm::FoldingSet<SpecialMemberOverloadResultEntry> SpecialMemberCache;

  /// Holds TypoExprs that are created from `createDelayedTypo`. This is used by
  /// `TransformTypos` in order to keep track of any TypoExprs that are created
  /// recursively during typo correction and wipe them away if the correction
  /// fails.
  llvm::SmallVector<TypoExpr *, 2> TypoExprs;

  enum class AcceptableKind { Visible, Reachable };

  // Members have to be NamespaceDecl* or TranslationUnitDecl*.
  // TODO: make this is a typesafe union.
  typedef llvm::SmallSetVector<DeclContext *, 16> AssociatedNamespaceSet;
  typedef llvm::SmallSetVector<CXXRecordDecl *, 16> AssociatedClassSet;

  /// Describes the kind of name lookup to perform.
  enum LookupNameKind {
    /// Ordinary name lookup, which finds ordinary names (functions,
    /// variables, typedefs, etc.) in C and most kinds of names
    /// (functions, variables, members, types, etc.) in C++.
    LookupOrdinaryName = 0,
    /// Tag name lookup, which finds the names of enums, classes,
    /// structs, and unions.
    LookupTagName,
    /// Label name lookup.
    LookupLabel,
    /// Member name lookup, which finds the names of
    /// class/struct/union members.
    LookupMemberName,
    /// Look up of an operator name (e.g., operator+) for use with
    /// operator overloading. This lookup is similar to ordinary name
    /// lookup, but will ignore any declarations that are class members.
    LookupOperatorName,
    /// Look up a name following ~ in a destructor name. This is an ordinary
    /// lookup, but prefers tags to typedefs.
    LookupDestructorName,
    /// Look up of a name that precedes the '::' scope resolution
    /// operator in C++. This lookup completely ignores operator, object,
    /// function, and enumerator names (C++ [basic.lookup.qual]p1).
    LookupNestedNameSpecifierName,
    /// Look up a namespace name within a C++ using directive or
    /// namespace alias definition, ignoring non-namespace names (C++
    /// [basic.lookup.udir]p1).
    LookupNamespaceName,
    /// Look up all declarations in a scope with the given name,
    /// including resolved using declarations.  This is appropriate
    /// for checking redeclarations for a using declaration.
    LookupUsingDeclName,
    /// Look up an ordinary name that is going to be redeclared as a
    /// name with linkage. This lookup ignores any declarations that
    /// are outside of the current scope unless they have linkage. See
    /// C99 6.2.2p4-5 and C++ [basic.link]p6.
    LookupRedeclarationWithLinkage,
    /// Look up a friend of a local class. This lookup does not look
    /// outside the innermost non-class scope. See C++11 [class.friend]p11.
    LookupLocalFriendName,
    /// Look up the name of an Objective-C protocol.
    LookupObjCProtocolName,
    /// Look up implicit 'self' parameter of an objective-c method.
    LookupObjCImplicitSelfParam,
    /// Look up the name of an OpenMP user-defined reduction operation.
    LookupOMPReductionName,
    /// Look up the name of an OpenMP user-defined mapper.
    LookupOMPMapperName,
    /// Look up any declaration with any name.
    LookupAnyName
  };

  /// The possible outcomes of name lookup for a literal operator.
  enum LiteralOperatorLookupResult {
    /// The lookup resulted in an error.
    LOLR_Error,
    /// The lookup found no match but no diagnostic was issued.
    LOLR_ErrorNoDiagnostic,
    /// The lookup found a single 'cooked' literal operator, which
    /// expects a normal literal to be built and passed to it.
    LOLR_Cooked,
    /// The lookup found a single 'raw' literal operator, which expects
    /// a string literal containing the spelling of the literal token.
    LOLR_Raw,
    /// The lookup found an overload set of literal operator templates,
    /// which expect the characters of the spelling of the literal token to be
    /// passed as a non-type template argument pack.
    LOLR_Template,
    /// The lookup found an overload set of literal operator templates,
    /// which expect the character type and characters of the spelling of the
    /// string literal token to be passed as template arguments.
    LOLR_StringTemplatePack,
  };

  SpecialMemberOverloadResult
  LookupSpecialMember(CXXRecordDecl *D, CXXSpecialMemberKind SM, bool ConstArg,
                      bool VolatileArg, bool RValueThis, bool ConstThis,
                      bool VolatileThis);

  typedef std::function<void(const TypoCorrection &)> TypoDiagnosticGenerator;
  typedef std::function<ExprResult(Sema &, TypoExpr *, TypoCorrection)>
      TypoRecoveryCallback;

  RedeclarationKind forRedeclarationInCurContext() const;

  /// Look up a name, looking for a single declaration.  Return
  /// null if the results were absent, ambiguous, or overloaded.
  ///
  /// It is preferable to use the elaborated form and explicitly handle
  /// ambiguity and overloaded.
  NamedDecl *LookupSingleName(
      Scope *S, DeclarationName Name, SourceLocation Loc,
      LookupNameKind NameKind,
      RedeclarationKind Redecl = RedeclarationKind::NotForRedeclaration);

  /// Lookup a builtin function, when name lookup would otherwise
  /// fail.
  bool LookupBuiltin(LookupResult &R);
  void LookupNecessaryTypesForBuiltin(Scope *S, unsigned ID);

  /// Perform unqualified name lookup starting from a given
  /// scope.
  ///
  /// Unqualified name lookup (C++ [basic.lookup.unqual], C99 6.2.1) is
  /// used to find names within the current scope. For example, 'x' in
  /// @code
  /// int x;
  /// int f() {
  ///   return x; // unqualified name look finds 'x' in the global scope
  /// }
  /// @endcode
  ///
  /// Different lookup criteria can find different names. For example, a
  /// particular scope can have both a struct and a function of the same
  /// name, and each can be found by certain lookup criteria. For more
  /// information about lookup criteria, see the documentation for the
  /// class LookupCriteria.
  ///
  /// @param S        The scope from which unqualified name lookup will
  /// begin. If the lookup criteria permits, name lookup may also search
  /// in the parent scopes.
  ///
  /// @param [in,out] R Specifies the lookup to perform (e.g., the name to
  /// look up and the lookup kind), and is updated with the results of lookup
  /// including zero or more declarations and possibly additional information
  /// used to diagnose ambiguities.
  ///
  /// @returns \c true if lookup succeeded and false otherwise.
  bool LookupName(LookupResult &R, Scope *S, bool AllowBuiltinCreation = false,
                  bool ForceNoCPlusPlus = false);

  /// Perform qualified name lookup into a given context.
  ///
  /// Qualified name lookup (C++ [basic.lookup.qual]) is used to find
  /// names when the context of those names is explicit specified, e.g.,
  /// "std::vector" or "x->member", or as part of unqualified name lookup.
  ///
  /// Different lookup criteria can find different names. For example, a
  /// particular scope can have both a struct and a function of the same
  /// name, and each can be found by certain lookup criteria. For more
  /// information about lookup criteria, see the documentation for the
  /// class LookupCriteria.
  ///
  /// \param R captures both the lookup criteria and any lookup results found.
  ///
  /// \param LookupCtx The context in which qualified name lookup will
  /// search. If the lookup criteria permits, name lookup may also search
  /// in the parent contexts or (for C++ classes) base classes.
  ///
  /// \param InUnqualifiedLookup true if this is qualified name lookup that
  /// occurs as part of unqualified name lookup.
  ///
  /// \returns true if lookup succeeded, false if it failed.
  bool LookupQualifiedName(LookupResult &R, DeclContext *LookupCtx,
                           bool InUnqualifiedLookup = false);

  /// Performs qualified name lookup or special type of lookup for
  /// "__super::" scope specifier.
  ///
  /// This routine is a convenience overload meant to be called from contexts
  /// that need to perform a qualified name lookup with an optional C++ scope
  /// specifier that might require special kind of lookup.
  ///
  /// \param R captures both the lookup criteria and any lookup results found.
  ///
  /// \param LookupCtx The context in which qualified name lookup will
  /// search.
  ///
  /// \param SS An optional C++ scope-specifier.
  ///
  /// \returns true if lookup succeeded, false if it failed.
  bool LookupQualifiedName(LookupResult &R, DeclContext *LookupCtx,
                           CXXScopeSpec &SS);

  /// Performs name lookup for a name that was parsed in the
  /// source code, and may contain a C++ scope specifier.
  ///
  /// This routine is a convenience routine meant to be called from
  /// contexts that receive a name and an optional C++ scope specifier
  /// (e.g., "N::M::x"). It will then perform either qualified or
  /// unqualified name lookup (with LookupQualifiedName or LookupName,
  /// respectively) on the given name and return those results. It will
  /// perform a special type of lookup for "__super::" scope specifier.
  ///
  /// @param S        The scope from which unqualified name lookup will
  /// begin.
  ///
  /// @param SS       An optional C++ scope-specifier, e.g., "::N::M".
  ///
  /// @param EnteringContext Indicates whether we are going to enter the
  /// context of the scope-specifier SS (if present).
  ///
  /// @returns True if any decls were found (but possibly ambiguous)
  bool LookupParsedName(LookupResult &R, Scope *S, CXXScopeSpec *SS,
                        QualType ObjectType, bool AllowBuiltinCreation = false,
                        bool EnteringContext = false);

  /// Perform qualified name lookup into all base classes of the given
  /// class.
  ///
  /// \param R captures both the lookup criteria and any lookup results found.
  ///
  /// \param Class The context in which qualified name lookup will
  /// search. Name lookup will search in all base classes merging the results.
  ///
  /// @returns True if any decls were found (but possibly ambiguous)
  bool LookupInSuper(LookupResult &R, CXXRecordDecl *Class);

  void LookupOverloadedOperatorName(OverloadedOperatorKind Op, Scope *S,
                                    UnresolvedSetImpl &Functions);

  /// LookupOrCreateLabel - Do a name lookup of a label with the specified name.
  /// If GnuLabelLoc is a valid source location, then this is a definition
  /// of an __label__ label name, otherwise it is a normal label definition
  /// or use.
  LabelDecl *LookupOrCreateLabel(IdentifierInfo *II, SourceLocation IdentLoc,
                                 SourceLocation GnuLabelLoc = SourceLocation());

  /// Look up the constructors for the given class.
  DeclContextLookupResult LookupConstructors(CXXRecordDecl *Class);

  /// Look up the default constructor for the given class.
  CXXConstructorDecl *LookupDefaultConstructor(CXXRecordDecl *Class);

  /// Look up the copying constructor for the given class.
  CXXConstructorDecl *LookupCopyingConstructor(CXXRecordDecl *Class,
                                               unsigned Quals);

  /// Look up the copying assignment operator for the given class.
  CXXMethodDecl *LookupCopyingAssignment(CXXRecordDecl *Class, unsigned Quals,
                                         bool RValueThis, unsigned ThisQuals);

  /// Look up the moving constructor for the given class.
  CXXConstructorDecl *LookupMovingConstructor(CXXRecordDecl *Class,
                                              unsigned Quals);

  /// Look up the moving assignment operator for the given class.
  CXXMethodDecl *LookupMovingAssignment(CXXRecordDecl *Class, unsigned Quals,
                                        bool RValueThis, unsigned ThisQuals);

  /// Look for the destructor of the given class.
  ///
  /// During semantic analysis, this routine should be used in lieu of
  /// CXXRecordDecl::getDestructor().
  ///
  /// \returns The destructor for this class.
  CXXDestructorDecl *LookupDestructor(CXXRecordDecl *Class);

  /// Force the declaration of any implicitly-declared members of this
  /// class.
  void ForceDeclarationOfImplicitMembers(CXXRecordDecl *Class);

  /// Make a merged definition of an existing hidden definition \p ND
  /// visible at the specified location.
  void makeMergedDefinitionVisible(NamedDecl *ND);

  /// Check ODR hashes for C/ObjC when merging types from modules.
  /// Differently from C++, actually parse the body and reject in case
  /// of a mismatch.
  template <typename T,
            typename = std::enable_if_t<std::is_base_of<NamedDecl, T>::value>>
  bool ActOnDuplicateODRHashDefinition(T *Duplicate, T *Previous) {
    if (Duplicate->getODRHash() != Previous->getODRHash())
      return false;

    // Make the previous decl visible.
    makeMergedDefinitionVisible(Previous);
    return true;
  }

  /// Get the set of additional modules that should be checked during
  /// name lookup. A module and its imports become visible when instanting a
  /// template defined within it.
  llvm::DenseSet<Module *> &getLookupModules();

  bool hasVisibleMergedDefinition(const NamedDecl *Def);
  bool hasMergedDefinitionInCurrentModule(const NamedDecl *Def);

  /// Determine if the template parameter \p D has a visible default argument.
  bool
  hasVisibleDefaultArgument(const NamedDecl *D,
                            llvm::SmallVectorImpl<Module *> *Modules = nullptr);
  /// Determine if the template parameter \p D has a reachable default argument.
  bool hasReachableDefaultArgument(
      const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules = nullptr);
  /// Determine if the template parameter \p D has a reachable default argument.
  bool hasAcceptableDefaultArgument(const NamedDecl *D,
                                    llvm::SmallVectorImpl<Module *> *Modules,
                                    Sema::AcceptableKind Kind);

  /// Determine if there is a visible declaration of \p D that is an explicit
  /// specialization declaration for a specialization of a template. (For a
  /// member specialization, use hasVisibleMemberSpecialization.)
  bool hasVisibleExplicitSpecialization(
      const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules = nullptr);
  /// Determine if there is a reachable declaration of \p D that is an explicit
  /// specialization declaration for a specialization of a template. (For a
  /// member specialization, use hasReachableMemberSpecialization.)
  bool hasReachableExplicitSpecialization(
      const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules = nullptr);

  /// Determine if there is a visible declaration of \p D that is a member
  /// specialization declaration (as opposed to an instantiated declaration).
  bool hasVisibleMemberSpecialization(
      const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules = nullptr);
  /// Determine if there is a reachable declaration of \p D that is a member
  /// specialization declaration (as opposed to an instantiated declaration).
  bool hasReachableMemberSpecialization(
      const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules = nullptr);

  bool isModuleVisible(const Module *M, bool ModulePrivate = false);

  /// Determine whether any declaration of an entity is visible.
  bool
  hasVisibleDeclaration(const NamedDecl *D,
                        llvm::SmallVectorImpl<Module *> *Modules = nullptr) {
    return isVisible(D) || hasVisibleDeclarationSlow(D, Modules);
  }

  bool hasVisibleDeclarationSlow(const NamedDecl *D,
                                 llvm::SmallVectorImpl<Module *> *Modules);
  /// Determine whether any declaration of an entity is reachable.
  bool
  hasReachableDeclaration(const NamedDecl *D,
                          llvm::SmallVectorImpl<Module *> *Modules = nullptr) {
    return isReachable(D) || hasReachableDeclarationSlow(D, Modules);
  }
  bool hasReachableDeclarationSlow(
      const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules = nullptr);

  void diagnoseTypo(const TypoCorrection &Correction,
                    const PartialDiagnostic &TypoDiag,
                    bool ErrorRecovery = true);

  /// Diagnose a successfully-corrected typo. Separated from the correction
  /// itself to allow external validation of the result, etc.
  ///
  /// \param Correction The result of performing typo correction.
  /// \param TypoDiag The diagnostic to produce. This will have the corrected
  ///        string added to it (and usually also a fixit).
  /// \param PrevNote A note to use when indicating the location of the entity
  ///        to which we are correcting. Will have the correction string added
  ///        to it.
  /// \param ErrorRecovery If \c true (the default), the caller is going to
  ///        recover from the typo as if the corrected string had been typed.
  ///        In this case, \c PDiag must be an error, and we will attach a fixit
  ///        to it.
  void diagnoseTypo(const TypoCorrection &Correction,
                    const PartialDiagnostic &TypoDiag,
                    const PartialDiagnostic &PrevNote,
                    bool ErrorRecovery = true);

  /// Find the associated classes and namespaces for
  /// argument-dependent lookup for a call with the given set of
  /// arguments.
  ///
  /// This routine computes the sets of associated classes and associated
  /// namespaces searched by argument-dependent lookup
  /// (C++ [basic.lookup.argdep]) for a given set of arguments.
  void FindAssociatedClassesAndNamespaces(
      SourceLocation InstantiationLoc, ArrayRef<Expr *> Args,
      AssociatedNamespaceSet &AssociatedNamespaces,
      AssociatedClassSet &AssociatedClasses);

  /// Produce a diagnostic describing the ambiguity that resulted
  /// from name lookup.
  ///
  /// \param Result The result of the ambiguous lookup to be diagnosed.
  void DiagnoseAmbiguousLookup(LookupResult &Result);

  /// LookupLiteralOperator - Determine which literal operator should be used
  /// for a user-defined literal, per C++11 [lex.ext].
  ///
  /// Normal overload resolution is not used to select which literal operator to
  /// call for a user-defined literal. Look up the provided literal operator
  /// name, and filter the results to the appropriate set for the given argument
  /// types.
  LiteralOperatorLookupResult
  LookupLiteralOperator(Scope *S, LookupResult &R, ArrayRef<QualType> ArgTys,
                        bool AllowRaw, bool AllowTemplate,
                        bool AllowStringTemplate, bool DiagnoseMissing,
                        StringLiteral *StringLit = nullptr);

  void ArgumentDependentLookup(DeclarationName Name, SourceLocation Loc,
                               ArrayRef<Expr *> Args, ADLResult &Functions);

  void LookupVisibleDecls(Scope *S, LookupNameKind Kind,
                          VisibleDeclConsumer &Consumer,
                          bool IncludeGlobalScope = true,
                          bool LoadExternal = true);
  void LookupVisibleDecls(DeclContext *Ctx, LookupNameKind Kind,
                          VisibleDeclConsumer &Consumer,
                          bool IncludeGlobalScope = true,
                          bool IncludeDependentBases = false,
                          bool LoadExternal = true);

  enum CorrectTypoKind {
    CTK_NonError,     // CorrectTypo used in a non error recovery situation.
    CTK_ErrorRecovery // CorrectTypo used in normal error recovery.
  };

  /// Try to "correct" a typo in the source code by finding
  /// visible declarations whose names are similar to the name that was
  /// present in the source code.
  ///
  /// \param TypoName the \c DeclarationNameInfo structure that contains
  /// the name that was present in the source code along with its location.
  ///
  /// \param LookupKind the name-lookup criteria used to search for the name.
  ///
  /// \param S the scope in which name lookup occurs.
  ///
  /// \param SS the nested-name-specifier that precedes the name we're
  /// looking for, if present.
  ///
  /// \param CCC A CorrectionCandidateCallback object that provides further
  /// validation of typo correction candidates. It also provides flags for
  /// determining the set of keywords permitted.
  ///
  /// \param MemberContext if non-NULL, the context in which to look for
  /// a member access expression.
  ///
  /// \param EnteringContext whether we're entering the context described by
  /// the nested-name-specifier SS.
  ///
  /// \param OPT when non-NULL, the search for visible declarations will
  /// also walk the protocols in the qualified interfaces of \p OPT.
  ///
  /// \returns a \c TypoCorrection containing the corrected name if the typo
  /// along with information such as the \c NamedDecl where the corrected name
  /// was declared, and any additional \c NestedNameSpecifier needed to access
  /// it (C++ only). The \c TypoCorrection is empty if there is no correction.
  TypoCorrection CorrectTypo(const DeclarationNameInfo &Typo,
                             Sema::LookupNameKind LookupKind, Scope *S,
                             CXXScopeSpec *SS, CorrectionCandidateCallback &CCC,
                             CorrectTypoKind Mode,
                             DeclContext *MemberContext = nullptr,
                             bool EnteringContext = false,
                             const ObjCObjectPointerType *OPT = nullptr,
                             bool RecordFailure = true);

  /// Try to "correct" a typo in the source code by finding
  /// visible declarations whose names are similar to the name that was
  /// present in the source code.
  ///
  /// \param TypoName the \c DeclarationNameInfo structure that contains
  /// the name that was present in the source code along with its location.
  ///
  /// \param LookupKind the name-lookup criteria used to search for the name.
  ///
  /// \param S the scope in which name lookup occurs.
  ///
  /// \param SS the nested-name-specifier that precedes the name we're
  /// looking for, if present.
  ///
  /// \param CCC A CorrectionCandidateCallback object that provides further
  /// validation of typo correction candidates. It also provides flags for
  /// determining the set of keywords permitted.
  ///
  /// \param TDG A TypoDiagnosticGenerator functor that will be used to print
  /// diagnostics when the actual typo correction is attempted.
  ///
  /// \param TRC A TypoRecoveryCallback functor that will be used to build an
  /// Expr from a typo correction candidate.
  ///
  /// \param MemberContext if non-NULL, the context in which to look for
  /// a member access expression.
  ///
  /// \param EnteringContext whether we're entering the context described by
  /// the nested-name-specifier SS.
  ///
  /// \param OPT when non-NULL, the search for visible declarations will
  /// also walk the protocols in the qualified interfaces of \p OPT.
  ///
  /// \returns a new \c TypoExpr that will later be replaced in the AST with an
  /// Expr representing the result of performing typo correction, or nullptr if
  /// typo correction is not possible. If nullptr is returned, no diagnostics
  /// will be emitted and it is the responsibility of the caller to emit any
  /// that are needed.
  TypoExpr *CorrectTypoDelayed(
      const DeclarationNameInfo &Typo, Sema::LookupNameKind LookupKind,
      Scope *S, CXXScopeSpec *SS, CorrectionCandidateCallback &CCC,
      TypoDiagnosticGenerator TDG, TypoRecoveryCallback TRC,
      CorrectTypoKind Mode, DeclContext *MemberContext = nullptr,
      bool EnteringContext = false, const ObjCObjectPointerType *OPT = nullptr);

  /// Kinds of missing import. Note, the values of these enumerators correspond
  /// to %select values in diagnostics.
  enum class MissingImportKind {
    Declaration,
    Definition,
    DefaultArgument,
    ExplicitSpecialization,
    PartialSpecialization
  };

  /// Diagnose that the specified declaration needs to be visible but
  /// isn't, and suggest a module import that would resolve the problem.
  void diagnoseMissingImport(SourceLocation Loc, const NamedDecl *Decl,
                             MissingImportKind MIK, bool Recover = true);
  void diagnoseMissingImport(SourceLocation Loc, const NamedDecl *Decl,
                             SourceLocation DeclLoc, ArrayRef<Module *> Modules,
                             MissingImportKind MIK, bool Recover);

  struct TypoExprState {
    std::unique_ptr<TypoCorrectionConsumer> Consumer;
    TypoDiagnosticGenerator DiagHandler;
    TypoRecoveryCallback RecoveryHandler;
    TypoExprState();
    TypoExprState(TypoExprState &&other) noexcept;
    TypoExprState &operator=(TypoExprState &&other) noexcept;
  };

  const TypoExprState &getTypoExprState(TypoExpr *TE) const;

  /// Clears the state of the given TypoExpr.
  void clearDelayedTypo(TypoExpr *TE);

  /// Called on #pragma clang __debug dump II
  void ActOnPragmaDump(Scope *S, SourceLocation Loc, IdentifierInfo *II);

  /// Called on #pragma clang __debug dump E
  void ActOnPragmaDump(Expr *E);

private:
  // The set of known/encountered (unique, canonicalized) NamespaceDecls.
  //
  // The boolean value will be true to indicate that the namespace was loaded
  // from an AST/PCH file, or false otherwise.
  llvm::MapVector<NamespaceDecl *, bool> KnownNamespaces;

  /// Whether we have already loaded known namespaces from an extenal
  /// source.
  bool LoadedExternalKnownNamespaces;

  bool CppLookupName(LookupResult &R, Scope *S);

  /// Determine if we could use all the declarations in the module.
  bool isUsableModule(const Module *M);

  /// Helper for CorrectTypo and CorrectTypoDelayed used to create and
  /// populate a new TypoCorrectionConsumer. Returns nullptr if typo correction
  /// should be skipped entirely.
  std::unique_ptr<TypoCorrectionConsumer> makeTypoCorrectionConsumer(
      const DeclarationNameInfo &Typo, Sema::LookupNameKind LookupKind,
      Scope *S, CXXScopeSpec *SS, CorrectionCandidateCallback &CCC,
      DeclContext *MemberContext, bool EnteringContext,
      const ObjCObjectPointerType *OPT, bool ErrorRecovery);

  /// The set of unhandled TypoExprs and their associated state.
  llvm::MapVector<TypoExpr *, TypoExprState> DelayedTypos;

  /// Creates a new TypoExpr AST node.
  TypoExpr *createDelayedTypo(std::unique_ptr<TypoCorrectionConsumer> TCC,
                              TypoDiagnosticGenerator TDG,
                              TypoRecoveryCallback TRC, SourceLocation TypoLoc);

  /// Cache for module units which is usable for current module.
  llvm::DenseSet<const Module *> UsableModuleUnitsCache;

  /// Record the typo correction failure and return an empty correction.
  TypoCorrection FailedCorrection(IdentifierInfo *Typo, SourceLocation TypoLoc,
                                  bool RecordFailure = true) {
    if (RecordFailure)
      TypoCorrectionFailures[Typo].insert(TypoLoc);
    return TypoCorrection();
  }

  bool isAcceptableSlow(const NamedDecl *D, AcceptableKind Kind);

  /// Determine whether two declarations should be linked together, given that
  /// the old declaration might not be visible and the new declaration might
  /// not have external linkage.
  bool shouldLinkPossiblyHiddenDecl(const NamedDecl *Old,
                                    const NamedDecl *New) {
    if (isVisible(Old))
      return true;
    // See comment in below overload for why it's safe to compute the linkage
    // of the new declaration here.
    if (New->isExternallyDeclarable()) {
      assert(Old->isExternallyDeclarable() &&
             "should not have found a non-externally-declarable previous decl");
      return true;
    }
    return false;
  }
  bool shouldLinkPossiblyHiddenDecl(LookupResult &Old, const NamedDecl *New);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Modules
  /// Implementations are in SemaModule.cpp
  ///@{

public:
  /// Get the module unit whose scope we are currently within.
  Module *getCurrentModule() const {
    return ModuleScopes.empty() ? nullptr : ModuleScopes.back().Module;
  }

  /// Is the module scope we are an implementation unit?
  bool currentModuleIsImplementation() const {
    return ModuleScopes.empty()
               ? false
               : ModuleScopes.back().Module->isModuleImplementation();
  }

  // When loading a non-modular PCH files, this is used to restore module
  // visibility.
  void makeModuleVisible(Module *Mod, SourceLocation ImportLoc) {
    VisibleModules.setVisible(Mod, ImportLoc);
  }

  enum class ModuleDeclKind {
    Interface,               ///< 'export module X;'
    Implementation,          ///< 'module X;'
    PartitionInterface,      ///< 'export module X:Y;'
    PartitionImplementation, ///< 'module X:Y;'
  };

  /// An enumeration to represent the transition of states in parsing module
  /// fragments and imports.  If we are not parsing a C++20 TU, or we find
  /// an error in state transition, the state is set to NotACXX20Module.
  enum class ModuleImportState {
    FirstDecl,      ///< Parsing the first decl in a TU.
    GlobalFragment, ///< after 'module;' but before 'module X;'
    ImportAllowed,  ///< after 'module X;' but before any non-import decl.
    ImportFinished, ///< after any non-import decl.
    PrivateFragmentImportAllowed,  ///< after 'module :private;' but before any
                                   ///< non-import decl.
    PrivateFragmentImportFinished, ///< after 'module :private;' but a
                                   ///< non-import decl has already been seen.
    NotACXX20Module ///< Not a C++20 TU, or an invalid state was found.
  };

  /// The parser has processed a module-declaration that begins the definition
  /// of a module interface or implementation.
  DeclGroupPtrTy ActOnModuleDecl(SourceLocation StartLoc,
                                 SourceLocation ModuleLoc, ModuleDeclKind MDK,
                                 ModuleIdPath Path, ModuleIdPath Partition,
                                 ModuleImportState &ImportState);

  /// The parser has processed a global-module-fragment declaration that begins
  /// the definition of the global module fragment of the current module unit.
  /// \param ModuleLoc The location of the 'module' keyword.
  DeclGroupPtrTy ActOnGlobalModuleFragmentDecl(SourceLocation ModuleLoc);

  /// The parser has processed a private-module-fragment declaration that begins
  /// the definition of the private module fragment of the current module unit.
  /// \param ModuleLoc The location of the 'module' keyword.
  /// \param PrivateLoc The location of the 'private' keyword.
  DeclGroupPtrTy ActOnPrivateModuleFragmentDecl(SourceLocation ModuleLoc,
                                                SourceLocation PrivateLoc);

  /// The parser has processed a module import declaration.
  ///
  /// \param StartLoc The location of the first token in the declaration. This
  ///        could be the location of an '@', 'export', or 'import'.
  /// \param ExportLoc The location of the 'export' keyword, if any.
  /// \param ImportLoc The location of the 'import' keyword.
  /// \param Path The module toplevel name as an access path.
  /// \param IsPartition If the name is for a partition.
  DeclResult ActOnModuleImport(SourceLocation StartLoc,
                               SourceLocation ExportLoc,
                               SourceLocation ImportLoc, ModuleIdPath Path,
                               bool IsPartition = false);
  DeclResult ActOnModuleImport(SourceLocation StartLoc,
                               SourceLocation ExportLoc,
                               SourceLocation ImportLoc, Module *M,
                               ModuleIdPath Path = {});

  /// The parser has processed a module import translated from a
  /// #include or similar preprocessing directive.
  void ActOnAnnotModuleInclude(SourceLocation DirectiveLoc, Module *Mod);
  void BuildModuleInclude(SourceLocation DirectiveLoc, Module *Mod);

  /// The parsed has entered a submodule.
  void ActOnAnnotModuleBegin(SourceLocation DirectiveLoc, Module *Mod);
  /// The parser has left a submodule.
  void ActOnAnnotModuleEnd(SourceLocation DirectiveLoc, Module *Mod);

  /// Create an implicit import of the given module at the given
  /// source location, for error recovery, if possible.
  ///
  /// This routine is typically used when an entity found by name lookup
  /// is actually hidden within a module that we know about but the user
  /// has forgotten to import.
  void createImplicitModuleImportForErrorRecovery(SourceLocation Loc,
                                                  Module *Mod);

  /// We have parsed the start of an export declaration, including the '{'
  /// (if present).
  Decl *ActOnStartExportDecl(Scope *S, SourceLocation ExportLoc,
                             SourceLocation LBraceLoc);

  /// Complete the definition of an export declaration.
  Decl *ActOnFinishExportDecl(Scope *S, Decl *ExportDecl,
                              SourceLocation RBraceLoc);

private:
  /// The parser has begun a translation unit to be compiled as a C++20
  /// Header Unit, helper for ActOnStartOfTranslationUnit() only.
  void HandleStartOfHeaderUnit();

  struct ModuleScope {
    SourceLocation BeginLoc;
    clang::Module *Module = nullptr;
    VisibleModuleSet OuterVisibleModules;
  };
  /// The modules we're currently parsing.
  llvm::SmallVector<ModuleScope, 16> ModuleScopes;

  /// For an interface unit, this is the implicitly imported interface unit.
  clang::Module *ThePrimaryInterface = nullptr;

  /// The explicit global module fragment of the current translation unit.
  /// The explicit Global Module Fragment, as specified in C++
  /// [module.global.frag].
  clang::Module *TheGlobalModuleFragment = nullptr;

  /// The implicit global module fragments of the current translation unit.
  ///
  /// The contents in the implicit global module fragment can't be discarded.
  clang::Module *TheImplicitGlobalModuleFragment = nullptr;

  /// Namespace definitions that we will export when they finish.
  llvm::SmallPtrSet<const NamespaceDecl *, 8> DeferredExportedNamespaces;

  /// In a C++ standard module, inline declarations require a definition to be
  /// present at the end of a definition domain.  This set holds the decls to
  /// be checked at the end of the TU.
  llvm::SmallPtrSet<const FunctionDecl *, 8> PendingInlineFuncDecls;

  /// Helper function to judge if we are in module purview.
  /// Return false if we are not in a module.
  bool isCurrentModulePurview() const;

  /// Enter the scope of the explicit global module fragment.
  Module *PushGlobalModuleFragment(SourceLocation BeginLoc);
  /// Leave the scope of the explicit global module fragment.
  void PopGlobalModuleFragment();

  /// Enter the scope of an implicit global module fragment.
  Module *PushImplicitGlobalModuleFragment(SourceLocation BeginLoc);
  /// Leave the scope of an implicit global module fragment.
  void PopImplicitGlobalModuleFragment();

  VisibleModuleSet VisibleModules;

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Overloading
  /// Implementations are in SemaOverload.cpp
  ///@{

public:
  /// Whether deferrable diagnostics should be deferred.
  bool DeferDiags = false;

  /// RAII class to control scope of DeferDiags.
  class DeferDiagsRAII {
    Sema &S;
    bool SavedDeferDiags = false;

  public:
    DeferDiagsRAII(Sema &S, bool DeferDiags)
        : S(S), SavedDeferDiags(S.DeferDiags) {
      S.DeferDiags = DeferDiags;
    }
    ~DeferDiagsRAII() { S.DeferDiags = SavedDeferDiags; }
  };

  /// Flag indicating if Sema is building a recovery call expression.
  ///
  /// This flag is used to avoid building recovery call expressions
  /// if Sema is already doing so, which would cause infinite recursions.
  bool IsBuildingRecoveryCallExpr;

  enum OverloadKind {
    /// This is a legitimate overload: the existing declarations are
    /// functions or function templates with different signatures.
    Ovl_Overload,

    /// This is not an overload because the signature exactly matches
    /// an existing declaration.
    Ovl_Match,

    /// This is not an overload because the lookup results contain a
    /// non-function.
    Ovl_NonFunction
  };

  /// Determine whether the given New declaration is an overload of the
  /// declarations in Old. This routine returns Ovl_Match or Ovl_NonFunction if
  /// New and Old cannot be overloaded, e.g., if New has the same signature as
  /// some function in Old (C++ 1.3.10) or if the Old declarations aren't
  /// functions (or function templates) at all. When it does return Ovl_Match or
  /// Ovl_NonFunction, MatchedDecl will point to the decl that New cannot be
  /// overloaded with. This decl may be a UsingShadowDecl on top of the
  /// underlying declaration.
  ///
  /// Example: Given the following input:
  ///
  ///   void f(int, float); // #1
  ///   void f(int, int); // #2
  ///   int f(int, int); // #3
  ///
  /// When we process #1, there is no previous declaration of "f", so IsOverload
  /// will not be used.
  ///
  /// When we process #2, Old contains only the FunctionDecl for #1. By
  /// comparing the parameter types, we see that #1 and #2 are overloaded (since
  /// they have different signatures), so this routine returns Ovl_Overload;
  /// MatchedDecl is unchanged.
  ///
  /// When we process #3, Old is an overload set containing #1 and #2. We
  /// compare the signatures of #3 to #1 (they're overloaded, so we do nothing)
  /// and then #3 to #2. Since the signatures of #3 and #2 are identical (return
  /// types of functions are not part of the signature), IsOverload returns
  /// Ovl_Match and MatchedDecl will be set to point to the FunctionDecl for #2.
  ///
  /// 'NewIsUsingShadowDecl' indicates that 'New' is being introduced into a
  /// class by a using declaration. The rules for whether to hide shadow
  /// declarations ignore some properties which otherwise figure into a function
  /// template's signature.
  OverloadKind CheckOverload(Scope *S, FunctionDecl *New,
                             const LookupResult &OldDecls, NamedDecl *&OldDecl,
                             bool UseMemberUsingDeclRules);
  bool IsOverload(FunctionDecl *New, FunctionDecl *Old,
                  bool UseMemberUsingDeclRules, bool ConsiderCudaAttrs = true);

  // Checks whether MD constitutes an override the base class method BaseMD.
  // When checking for overrides, the object object members are ignored.
  bool IsOverride(FunctionDecl *MD, FunctionDecl *BaseMD,
                  bool UseMemberUsingDeclRules, bool ConsiderCudaAttrs = true);

  enum class AllowedExplicit {
    /// Allow no explicit functions to be used.
    None,
    /// Allow explicit conversion functions but not explicit constructors.
    Conversions,
    /// Allow both explicit conversion functions and explicit constructors.
    All
  };

  ImplicitConversionSequence TryImplicitConversion(
      Expr *From, QualType ToType, bool SuppressUserConversions,
      AllowedExplicit AllowExplicit, bool InOverloadResolution, bool CStyle,
      bool AllowObjCWritebackConversion);

  /// PerformImplicitConversion - Perform an implicit conversion of the
  /// expression From to the type ToType. Returns the
  /// converted expression. Flavor is the kind of conversion we're
  /// performing, used in the error message. If @p AllowExplicit,
  /// explicit user-defined conversions are permitted.
  ExprResult PerformImplicitConversion(Expr *From, QualType ToType,
                                       AssignmentAction Action,
                                       bool AllowExplicit = false);

  /// IsIntegralPromotion - Determines whether the conversion from the
  /// expression From (whose potentially-adjusted type is FromType) to
  /// ToType is an integral promotion (C++ 4.5). If so, returns true and
  /// sets PromotedType to the promoted type.
  bool IsIntegralPromotion(Expr *From, QualType FromType, QualType ToType);

  /// IsFloatingPointPromotion - Determines whether the conversion from
  /// FromType to ToType is a floating point promotion (C++ 4.6). If so,
  /// returns true and sets PromotedType to the promoted type.
  bool IsFloatingPointPromotion(QualType FromType, QualType ToType);

  /// Determine if a conversion is a complex promotion.
  ///
  /// A complex promotion is defined as a complex -> complex conversion
  /// where the conversion between the underlying real types is a
  /// floating-point or integral promotion.
  bool IsComplexPromotion(QualType FromType, QualType ToType);

  /// IsPointerConversion - Determines whether the conversion of the
  /// expression From, which has the (possibly adjusted) type FromType,
  /// can be converted to the type ToType via a pointer conversion (C++
  /// 4.10). If so, returns true and places the converted type (that
  /// might differ from ToType in its cv-qualifiers at some level) into
  /// ConvertedType.
  ///
  /// This routine also supports conversions to and from block pointers
  /// and conversions with Objective-C's 'id', 'id<protocols...>', and
  /// pointers to interfaces. FIXME: Once we've determined the
  /// appropriate overloading rules for Objective-C, we may want to
  /// split the Objective-C checks into a different routine; however,
  /// GCC seems to consider all of these conversions to be pointer
  /// conversions, so for now they live here. IncompatibleObjC will be
  /// set if the conversion is an allowed Objective-C conversion that
  /// should result in a warning.
  bool IsPointerConversion(Expr *From, QualType FromType, QualType ToType,
                           bool InOverloadResolution, QualType &ConvertedType,
                           bool &IncompatibleObjC);

  /// isObjCPointerConversion - Determines whether this is an
  /// Objective-C pointer conversion. Subroutine of IsPointerConversion,
  /// with the same arguments and return values.
  bool isObjCPointerConversion(QualType FromType, QualType ToType,
                               QualType &ConvertedType, bool &IncompatibleObjC);
  bool IsBlockPointerConversion(QualType FromType, QualType ToType,
                                QualType &ConvertedType);

  /// FunctionParamTypesAreEqual - This routine checks two function proto types
  /// for equality of their parameter types. Caller has already checked that
  /// they have same number of parameters.  If the parameters are different,
  /// ArgPos will have the parameter index of the first different parameter.
  /// If `Reversed` is true, the parameters of `NewType` will be compared in
  /// reverse order. That's useful if one of the functions is being used as a
  /// C++20 synthesized operator overload with a reversed parameter order.
  bool FunctionParamTypesAreEqual(ArrayRef<QualType> Old,
                                  ArrayRef<QualType> New,
                                  unsigned *ArgPos = nullptr,
                                  bool Reversed = false);

  bool FunctionParamTypesAreEqual(const FunctionProtoType *OldType,
                                  const FunctionProtoType *NewType,
                                  unsigned *ArgPos = nullptr,
                                  bool Reversed = false);

  bool FunctionNonObjectParamTypesAreEqual(const FunctionDecl *OldFunction,
                                           const FunctionDecl *NewFunction,
                                           unsigned *ArgPos = nullptr,
                                           bool Reversed = false);

  /// HandleFunctionTypeMismatch - Gives diagnostic information for differeing
  /// function types.  Catches different number of parameter, mismatch in
  /// parameter types, and different return types.
  void HandleFunctionTypeMismatch(PartialDiagnostic &PDiag, QualType FromType,
                                  QualType ToType);

  /// CheckPointerConversion - Check the pointer conversion from the
  /// expression From to the type ToType. This routine checks for
  /// ambiguous or inaccessible derived-to-base pointer
  /// conversions for which IsPointerConversion has already returned
  /// true. It returns true and produces a diagnostic if there was an
  /// error, or returns false otherwise.
  bool CheckPointerConversion(Expr *From, QualType ToType, CastKind &Kind,
                              CXXCastPath &BasePath, bool IgnoreBaseAccess,
                              bool Diagnose = true);

  /// IsMemberPointerConversion - Determines whether the conversion of the
  /// expression From, which has the (possibly adjusted) type FromType, can be
  /// converted to the type ToType via a member pointer conversion (C++ 4.11).
  /// If so, returns true and places the converted type (that might differ from
  /// ToType in its cv-qualifiers at some level) into ConvertedType.
  bool IsMemberPointerConversion(Expr *From, QualType FromType, QualType ToType,
                                 bool InOverloadResolution,
                                 QualType &ConvertedType);

  /// CheckMemberPointerConversion - Check the member pointer conversion from
  /// the expression From to the type ToType. This routine checks for ambiguous
  /// or virtual or inaccessible base-to-derived member pointer conversions for
  /// which IsMemberPointerConversion has already returned true. It returns true
  /// and produces a diagnostic if there was an error, or returns false
  /// otherwise.
  bool CheckMemberPointerConversion(Expr *From, QualType ToType, CastKind &Kind,
                                    CXXCastPath &BasePath,
                                    bool IgnoreBaseAccess);

  /// IsQualificationConversion - Determines whether the conversion from
  /// an rvalue of type FromType to ToType is a qualification conversion
  /// (C++ 4.4).
  ///
  /// \param ObjCLifetimeConversion Output parameter that will be set to
  /// indicate when the qualification conversion involves a change in the
  /// Objective-C object lifetime.
  bool IsQualificationConversion(QualType FromType, QualType ToType,
                                 bool CStyle, bool &ObjCLifetimeConversion);

  /// Determine whether the conversion from FromType to ToType is a valid
  /// conversion that strips "noexcept" or "noreturn" off the nested function
  /// type.
  bool IsFunctionConversion(QualType FromType, QualType ToType,
                            QualType &ResultTy);
  bool DiagnoseMultipleUserDefinedConversion(Expr *From, QualType ToType);
  void DiagnoseUseOfDeletedFunction(SourceLocation Loc, SourceRange Range,
                                    DeclarationName Name,
                                    OverloadCandidateSet &CandidateSet,
                                    FunctionDecl *Fn, MultiExprArg Args,
                                    bool IsMember = false);

  ExprResult InitializeExplicitObjectArgument(Sema &S, Expr *Obj,
                                              FunctionDecl *Fun);
  ExprResult PerformImplicitObjectArgumentInitialization(
      Expr *From, NestedNameSpecifier *Qualifier, NamedDecl *FoundDecl,
      CXXMethodDecl *Method);

  /// PerformContextuallyConvertToBool - Perform a contextual conversion
  /// of the expression From to bool (C++0x [conv]p3).
  ExprResult PerformContextuallyConvertToBool(Expr *From);

  /// PerformContextuallyConvertToObjCPointer - Perform a contextual
  /// conversion of the expression From to an Objective-C pointer type.
  /// Returns a valid but null ExprResult if no conversion sequence exists.
  ExprResult PerformContextuallyConvertToObjCPointer(Expr *From);

  /// Contexts in which a converted constant expression is required.
  enum CCEKind {
    CCEK_CaseValue,    ///< Expression in a case label.
    CCEK_Enumerator,   ///< Enumerator value with fixed underlying type.
    CCEK_TemplateArg,  ///< Value of a non-type template parameter.
    CCEK_ArrayBound,   ///< Array bound in array declarator or new-expression.
    CCEK_ExplicitBool, ///< Condition in an explicit(bool) specifier.
    CCEK_Noexcept,     ///< Condition in a noexcept(bool) specifier.
    CCEK_StaticAssertMessageSize, ///< Call to size() in a static assert
                                  ///< message.
    CCEK_StaticAssertMessageData, ///< Call to data() in a static assert
                                  ///< message.
  };

  ExprResult BuildConvertedConstantExpression(Expr *From, QualType T,
                                              CCEKind CCE,
                                              NamedDecl *Dest = nullptr);

  ExprResult CheckConvertedConstantExpression(Expr *From, QualType T,
                                              llvm::APSInt &Value, CCEKind CCE);
  ExprResult CheckConvertedConstantExpression(Expr *From, QualType T,
                                              APValue &Value, CCEKind CCE,
                                              NamedDecl *Dest = nullptr);

  /// EvaluateConvertedConstantExpression - Evaluate an Expression
  /// That is a converted constant expression
  /// (which was built with BuildConvertedConstantExpression)
  ExprResult
  EvaluateConvertedConstantExpression(Expr *E, QualType T, APValue &Value,
                                      CCEKind CCE, bool RequireInt,
                                      const APValue &PreNarrowingValue);

  /// Abstract base class used to perform a contextual implicit
  /// conversion from an expression to any type passing a filter.
  class ContextualImplicitConverter {
  public:
    bool Suppress;
    bool SuppressConversion;

    ContextualImplicitConverter(bool Suppress = false,
                                bool SuppressConversion = false)
        : Suppress(Suppress), SuppressConversion(SuppressConversion) {}

    /// Determine whether the specified type is a valid destination type
    /// for this conversion.
    virtual bool match(QualType T) = 0;

    /// Emits a diagnostic complaining that the expression does not have
    /// integral or enumeration type.
    virtual SemaDiagnosticBuilder diagnoseNoMatch(Sema &S, SourceLocation Loc,
                                                  QualType T) = 0;

    /// Emits a diagnostic when the expression has incomplete class type.
    virtual SemaDiagnosticBuilder
    diagnoseIncomplete(Sema &S, SourceLocation Loc, QualType T) = 0;

    /// Emits a diagnostic when the only matching conversion function
    /// is explicit.
    virtual SemaDiagnosticBuilder diagnoseExplicitConv(Sema &S,
                                                       SourceLocation Loc,
                                                       QualType T,
                                                       QualType ConvTy) = 0;

    /// Emits a note for the explicit conversion function.
    virtual SemaDiagnosticBuilder
    noteExplicitConv(Sema &S, CXXConversionDecl *Conv, QualType ConvTy) = 0;

    /// Emits a diagnostic when there are multiple possible conversion
    /// functions.
    virtual SemaDiagnosticBuilder diagnoseAmbiguous(Sema &S, SourceLocation Loc,
                                                    QualType T) = 0;

    /// Emits a note for one of the candidate conversions.
    virtual SemaDiagnosticBuilder
    noteAmbiguous(Sema &S, CXXConversionDecl *Conv, QualType ConvTy) = 0;

    /// Emits a diagnostic when we picked a conversion function
    /// (for cases when we are not allowed to pick a conversion function).
    virtual SemaDiagnosticBuilder diagnoseConversion(Sema &S,
                                                     SourceLocation Loc,
                                                     QualType T,
                                                     QualType ConvTy) = 0;

    virtual ~ContextualImplicitConverter() {}
  };

  class ICEConvertDiagnoser : public ContextualImplicitConverter {
    bool AllowScopedEnumerations;

  public:
    ICEConvertDiagnoser(bool AllowScopedEnumerations, bool Suppress,
                        bool SuppressConversion)
        : ContextualImplicitConverter(Suppress, SuppressConversion),
          AllowScopedEnumerations(AllowScopedEnumerations) {}

    /// Match an integral or (possibly scoped) enumeration type.
    bool match(QualType T) override;

    SemaDiagnosticBuilder diagnoseNoMatch(Sema &S, SourceLocation Loc,
                                          QualType T) override {
      return diagnoseNotInt(S, Loc, T);
    }

    /// Emits a diagnostic complaining that the expression does not have
    /// integral or enumeration type.
    virtual SemaDiagnosticBuilder diagnoseNotInt(Sema &S, SourceLocation Loc,
                                                 QualType T) = 0;
  };

  /// Perform a contextual implicit conversion.
  ExprResult
  PerformContextualImplicitConversion(SourceLocation Loc, Expr *FromE,
                                      ContextualImplicitConverter &Converter);

  /// ReferenceCompareResult - Expresses the result of comparing two
  /// types (cv1 T1 and cv2 T2) to determine their compatibility for the
  /// purposes of initialization by reference (C++ [dcl.init.ref]p4).
  enum ReferenceCompareResult {
    /// Ref_Incompatible - The two types are incompatible, so direct
    /// reference binding is not possible.
    Ref_Incompatible = 0,
    /// Ref_Related - The two types are reference-related, which means
    /// that their unqualified forms (T1 and T2) are either the same
    /// or T1 is a base class of T2.
    Ref_Related,
    /// Ref_Compatible - The two types are reference-compatible.
    Ref_Compatible
  };

  // Fake up a scoped enumeration that still contextually converts to bool.
  struct ReferenceConversionsScope {
    /// The conversions that would be performed on an lvalue of type T2 when
    /// binding a reference of type T1 to it, as determined when evaluating
    /// whether T1 is reference-compatible with T2.
    enum ReferenceConversions {
      Qualification = 0x1,
      NestedQualification = 0x2,
      Function = 0x4,
      DerivedToBase = 0x8,
      ObjC = 0x10,
      ObjCLifetime = 0x20,

      LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/ObjCLifetime)
    };
  };
  using ReferenceConversions = ReferenceConversionsScope::ReferenceConversions;

  /// CompareReferenceRelationship - Compare the two types T1 and T2 to
  /// determine whether they are reference-compatible,
  /// reference-related, or incompatible, for use in C++ initialization by
  /// reference (C++ [dcl.ref.init]p4). Neither type can be a reference
  /// type, and the first type (T1) is the pointee type of the reference
  /// type being initialized.
  ReferenceCompareResult
  CompareReferenceRelationship(SourceLocation Loc, QualType T1, QualType T2,
                               ReferenceConversions *Conv = nullptr);

  /// AddOverloadCandidate - Adds the given function to the set of
  /// candidate functions, using the given function call arguments.  If
  /// @p SuppressUserConversions, then don't allow user-defined
  /// conversions via constructors or conversion operators.
  ///
  /// \param PartialOverloading true if we are performing "partial" overloading
  /// based on an incomplete set of function arguments. This feature is used by
  /// code completion.
  void AddOverloadCandidate(
      FunctionDecl *Function, DeclAccessPair FoundDecl, ArrayRef<Expr *> Args,
      OverloadCandidateSet &CandidateSet, bool SuppressUserConversions = false,
      bool PartialOverloading = false, bool AllowExplicit = true,
      bool AllowExplicitConversion = false,
      ADLCallKind IsADLCandidate = ADLCallKind::NotADL,
      ConversionSequenceList EarlyConversions = std::nullopt,
      OverloadCandidateParamOrder PO = {},
      bool AggregateCandidateDeduction = false);

  /// Add all of the function declarations in the given function set to
  /// the overload candidate set.
  void AddFunctionCandidates(
      const UnresolvedSetImpl &Functions, ArrayRef<Expr *> Args,
      OverloadCandidateSet &CandidateSet,
      TemplateArgumentListInfo *ExplicitTemplateArgs = nullptr,
      bool SuppressUserConversions = false, bool PartialOverloading = false,
      bool FirstArgumentIsBase = false);

  /// AddMethodCandidate - Adds a named decl (which is some kind of
  /// method) as a method candidate to the given overload set.
  void AddMethodCandidate(DeclAccessPair FoundDecl, QualType ObjectType,
                          Expr::Classification ObjectClassification,
                          ArrayRef<Expr *> Args,
                          OverloadCandidateSet &CandidateSet,
                          bool SuppressUserConversion = false,
                          OverloadCandidateParamOrder PO = {});

  /// AddMethodCandidate - Adds the given C++ member function to the set
  /// of candidate functions, using the given function call arguments
  /// and the object argument (@c Object). For example, in a call
  /// @c o.f(a1,a2), @c Object will contain @c o and @c Args will contain
  /// both @c a1 and @c a2. If @p SuppressUserConversions, then don't
  /// allow user-defined conversions via constructors or conversion
  /// operators.
  void
  AddMethodCandidate(CXXMethodDecl *Method, DeclAccessPair FoundDecl,
                     CXXRecordDecl *ActingContext, QualType ObjectType,
                     Expr::Classification ObjectClassification,
                     ArrayRef<Expr *> Args, OverloadCandidateSet &CandidateSet,
                     bool SuppressUserConversions = false,
                     bool PartialOverloading = false,
                     ConversionSequenceList EarlyConversions = std::nullopt,
                     OverloadCandidateParamOrder PO = {});

  /// Add a C++ member function template as a candidate to the candidate
  /// set, using template argument deduction to produce an appropriate member
  /// function template specialization.
  void AddMethodTemplateCandidate(
      FunctionTemplateDecl *MethodTmpl, DeclAccessPair FoundDecl,
      CXXRecordDecl *ActingContext,
      TemplateArgumentListInfo *ExplicitTemplateArgs, QualType ObjectType,
      Expr::Classification ObjectClassification, ArrayRef<Expr *> Args,
      OverloadCandidateSet &CandidateSet, bool SuppressUserConversions = false,
      bool PartialOverloading = false, OverloadCandidateParamOrder PO = {});

  /// Add a C++ function template specialization as a candidate
  /// in the candidate set, using template argument deduction to produce
  /// an appropriate function template specialization.
  void AddTemplateOverloadCandidate(
      FunctionTemplateDecl *FunctionTemplate, DeclAccessPair FoundDecl,
      TemplateArgumentListInfo *ExplicitTemplateArgs, ArrayRef<Expr *> Args,
      OverloadCandidateSet &CandidateSet, bool SuppressUserConversions = false,
      bool PartialOverloading = false, bool AllowExplicit = true,
      ADLCallKind IsADLCandidate = ADLCallKind::NotADL,
      OverloadCandidateParamOrder PO = {},
      bool AggregateCandidateDeduction = false);

  /// Check that implicit conversion sequences can be formed for each argument
  /// whose corresponding parameter has a non-dependent type, per DR1391's
  /// [temp.deduct.call]p10.
  bool CheckNonDependentConversions(
      FunctionTemplateDecl *FunctionTemplate, ArrayRef<QualType> ParamTypes,
      ArrayRef<Expr *> Args, OverloadCandidateSet &CandidateSet,
      ConversionSequenceList &Conversions, bool SuppressUserConversions,
      CXXRecordDecl *ActingContext = nullptr, QualType ObjectType = QualType(),
      Expr::Classification ObjectClassification = {},
      OverloadCandidateParamOrder PO = {});

  /// AddConversionCandidate - Add a C++ conversion function as a
  /// candidate in the candidate set (C++ [over.match.conv],
  /// C++ [over.match.copy]). From is the expression we're converting from,
  /// and ToType is the type that we're eventually trying to convert to
  /// (which may or may not be the same type as the type that the
  /// conversion function produces).
  void AddConversionCandidate(
      CXXConversionDecl *Conversion, DeclAccessPair FoundDecl,
      CXXRecordDecl *ActingContext, Expr *From, QualType ToType,
      OverloadCandidateSet &CandidateSet, bool AllowObjCConversionOnExplicit,
      bool AllowExplicit, bool AllowResultConversion = true);

  /// Adds a conversion function template specialization
  /// candidate to the overload set, using template argument deduction
  /// to deduce the template arguments of the conversion function
  /// template from the type that we are converting to (C++
  /// [temp.deduct.conv]).
  void AddTemplateConversionCandidate(
      FunctionTemplateDecl *FunctionTemplate, DeclAccessPair FoundDecl,
      CXXRecordDecl *ActingContext, Expr *From, QualType ToType,
      OverloadCandidateSet &CandidateSet, bool AllowObjCConversionOnExplicit,
      bool AllowExplicit, bool AllowResultConversion = true);

  /// AddSurrogateCandidate - Adds a "surrogate" candidate function that
  /// converts the given @c Object to a function pointer via the
  /// conversion function @c Conversion, and then attempts to call it
  /// with the given arguments (C++ [over.call.object]p2-4). Proto is
  /// the type of function that we'll eventually be calling.
  void AddSurrogateCandidate(CXXConversionDecl *Conversion,
                             DeclAccessPair FoundDecl,
                             CXXRecordDecl *ActingContext,
                             const FunctionProtoType *Proto, Expr *Object,
                             ArrayRef<Expr *> Args,
                             OverloadCandidateSet &CandidateSet);

  /// Add all of the non-member operator function declarations in the given
  /// function set to the overload candidate set.
  void AddNonMemberOperatorCandidates(
      const UnresolvedSetImpl &Functions, ArrayRef<Expr *> Args,
      OverloadCandidateSet &CandidateSet,
      TemplateArgumentListInfo *ExplicitTemplateArgs = nullptr);

  /// Add overload candidates for overloaded operators that are
  /// member functions.
  ///
  /// Add the overloaded operator candidates that are member functions
  /// for the operator Op that was used in an operator expression such
  /// as "x Op y". , Args/NumArgs provides the operator arguments, and
  /// CandidateSet will store the added overload candidates. (C++
  /// [over.match.oper]).
  void AddMemberOperatorCandidates(OverloadedOperatorKind Op,
                                   SourceLocation OpLoc, ArrayRef<Expr *> Args,
                                   OverloadCandidateSet &CandidateSet,
                                   OverloadCandidateParamOrder PO = {});

  /// AddBuiltinCandidate - Add a candidate for a built-in
  /// operator. ResultTy and ParamTys are the result and parameter types
  /// of the built-in candidate, respectively. Args and NumArgs are the
  /// arguments being passed to the candidate. IsAssignmentOperator
  /// should be true when this built-in candidate is an assignment
  /// operator. NumContextualBoolArguments is the number of arguments
  /// (at the beginning of the argument list) that will be contextually
  /// converted to bool.
  void AddBuiltinCandidate(QualType *ParamTys, ArrayRef<Expr *> Args,
                           OverloadCandidateSet &CandidateSet,
                           bool IsAssignmentOperator = false,
                           unsigned NumContextualBoolArguments = 0);

  /// AddBuiltinOperatorCandidates - Add the appropriate built-in
  /// operator overloads to the candidate set (C++ [over.built]), based
  /// on the operator @p Op and the arguments given. For example, if the
  /// operator is a binary '+', this routine might add "int
  /// operator+(int, int)" to cover integer addition.
  void AddBuiltinOperatorCandidates(OverloadedOperatorKind Op,
                                    SourceLocation OpLoc, ArrayRef<Expr *> Args,
                                    OverloadCandidateSet &CandidateSet);

  /// Add function candidates found via argument-dependent lookup
  /// to the set of overloading candidates.
  ///
  /// This routine performs argument-dependent name lookup based on the
  /// given function name (which may also be an operator name) and adds
  /// all of the overload candidates found by ADL to the overload
  /// candidate set (C++ [basic.lookup.argdep]).
  void AddArgumentDependentLookupCandidates(
      DeclarationName Name, SourceLocation Loc, ArrayRef<Expr *> Args,
      TemplateArgumentListInfo *ExplicitTemplateArgs,
      OverloadCandidateSet &CandidateSet, bool PartialOverloading = false);

  /// Check the enable_if expressions on the given function. Returns the first
  /// failing attribute, or NULL if they were all successful.
  EnableIfAttr *CheckEnableIf(FunctionDecl *Function, SourceLocation CallLoc,
                              ArrayRef<Expr *> Args,
                              bool MissingImplicitThis = false);

  /// Emit diagnostics for the diagnose_if attributes on Function, ignoring any
  /// non-ArgDependent DiagnoseIfAttrs.
  ///
  /// Argument-dependent diagnose_if attributes should be checked each time a
  /// function is used as a direct callee of a function call.
  ///
  /// Returns true if any errors were emitted.
  bool diagnoseArgDependentDiagnoseIfAttrs(const FunctionDecl *Function,
                                           const Expr *ThisArg,
                                           ArrayRef<const Expr *> Args,
                                           SourceLocation Loc);

  /// Emit diagnostics for the diagnose_if attributes on Function, ignoring any
  /// ArgDependent DiagnoseIfAttrs.
  ///
  /// Argument-independent diagnose_if attributes should be checked on every use
  /// of a function.
  ///
  /// Returns true if any errors were emitted.
  bool diagnoseArgIndependentDiagnoseIfAttrs(const NamedDecl *ND,
                                             SourceLocation Loc);

  /// Determine if \p A and \p B are equivalent internal linkage declarations
  /// from different modules, and thus an ambiguity error can be downgraded to
  /// an extension warning.
  bool isEquivalentInternalLinkageDeclaration(const NamedDecl *A,
                                              const NamedDecl *B);
  void diagnoseEquivalentInternalLinkageDeclarations(
      SourceLocation Loc, const NamedDecl *D,
      ArrayRef<const NamedDecl *> Equiv);

  // Emit as a 'note' the specific overload candidate
  void NoteOverloadCandidate(
      const NamedDecl *Found, const FunctionDecl *Fn,
      OverloadCandidateRewriteKind RewriteKind = OverloadCandidateRewriteKind(),
      QualType DestType = QualType(), bool TakingAddress = false);

  // Emit as a series of 'note's all template and non-templates identified by
  // the expression Expr
  void NoteAllOverloadCandidates(Expr *E, QualType DestType = QualType(),
                                 bool TakingAddress = false);

  /// Returns whether the given function's address can be taken or not,
  /// optionally emitting a diagnostic if the address can't be taken.
  ///
  /// Returns false if taking the address of the function is illegal.
  bool checkAddressOfFunctionIsAvailable(const FunctionDecl *Function,
                                         bool Complain = false,
                                         SourceLocation Loc = SourceLocation());

  // [PossiblyAFunctionType]  -->   [Return]
  // NonFunctionType --> NonFunctionType
  // R (A) --> R(A)
  // R (*)(A) --> R (A)
  // R (&)(A) --> R (A)
  // R (S::*)(A) --> R (A)
  QualType ExtractUnqualifiedFunctionType(QualType PossiblyAFunctionType);

  /// ResolveAddressOfOverloadedFunction - Try to resolve the address of
  /// an overloaded function (C++ [over.over]), where @p From is an
  /// expression with overloaded function type and @p ToType is the type
  /// we're trying to resolve to. For example:
  ///
  /// @code
  /// int f(double);
  /// int f(int);
  ///
  /// int (*pfd)(double) = f; // selects f(double)
  /// @endcode
  ///
  /// This routine returns the resulting FunctionDecl if it could be
  /// resolved, and NULL otherwise. When @p Complain is true, this
  /// routine will emit diagnostics if there is an error.
  FunctionDecl *
  ResolveAddressOfOverloadedFunction(Expr *AddressOfExpr, QualType TargetType,
                                     bool Complain, DeclAccessPair &Found,
                                     bool *pHadMultipleCandidates = nullptr);

  /// Given an expression that refers to an overloaded function, try to
  /// resolve that function to a single function that can have its address
  /// taken. This will modify `Pair` iff it returns non-null.
  ///
  /// This routine can only succeed if from all of the candidates in the
  /// overload set for SrcExpr that can have their addresses taken, there is one
  /// candidate that is more constrained than the rest.
  FunctionDecl *
  resolveAddressOfSingleOverloadCandidate(Expr *E, DeclAccessPair &FoundResult);

  /// Given an overloaded function, tries to turn it into a non-overloaded
  /// function reference using resolveAddressOfSingleOverloadCandidate. This
  /// will perform access checks, diagnose the use of the resultant decl, and,
  /// if requested, potentially perform a function-to-pointer decay.
  ///
  /// Returns false if resolveAddressOfSingleOverloadCandidate fails.
  /// Otherwise, returns true. This may emit diagnostics and return true.
  bool resolveAndFixAddressOfSingleOverloadCandidate(
      ExprResult &SrcExpr, bool DoFunctionPointerConversion = false);

  /// Given an expression that refers to an overloaded function, try to
  /// resolve that overloaded function expression down to a single function.
  ///
  /// This routine can only resolve template-ids that refer to a single function
  /// template, where that template-id refers to a single template whose
  /// template arguments are either provided by the template-id or have
  /// defaults, as described in C++0x [temp.arg.explicit]p3.
  ///
  /// If no template-ids are found, no diagnostics are emitted and NULL is
  /// returned.
  FunctionDecl *ResolveSingleFunctionTemplateSpecialization(
      OverloadExpr *ovl, bool Complain = false, DeclAccessPair *Found = nullptr,
      TemplateSpecCandidateSet *FailedTSC = nullptr);

  // Resolve and fix an overloaded expression that can be resolved
  // because it identifies a single function template specialization.
  //
  // Last three arguments should only be supplied if Complain = true
  //
  // Return true if it was logically possible to so resolve the
  // expression, regardless of whether or not it succeeded.  Always
  // returns true if 'complain' is set.
  bool ResolveAndFixSingleFunctionTemplateSpecialization(
      ExprResult &SrcExpr, bool DoFunctionPointerConversion = false,
      bool Complain = false, SourceRange OpRangeForComplaining = SourceRange(),
      QualType DestTypeForComplaining = QualType(),
      unsigned DiagIDForComplaining = 0);

  /// Add the overload candidates named by callee and/or found by argument
  /// dependent lookup to the given overload set.
  void AddOverloadedCallCandidates(UnresolvedLookupExpr *ULE,
                                   ArrayRef<Expr *> Args,
                                   OverloadCandidateSet &CandidateSet,
                                   bool PartialOverloading = false);

  /// Add the call candidates from the given set of lookup results to the given
  /// overload set. Non-function lookup results are ignored.
  void AddOverloadedCallCandidates(
      LookupResult &R, TemplateArgumentListInfo *ExplicitTemplateArgs,
      ArrayRef<Expr *> Args, OverloadCandidateSet &CandidateSet);

  // An enum used to represent the different possible results of building a
  // range-based for loop.
  enum ForRangeStatus {
    FRS_Success,
    FRS_NoViableFunction,
    FRS_DiagnosticIssued
  };

  /// Build a call to 'begin' or 'end' for a C++11 for-range statement. If the
  /// given LookupResult is non-empty, it is assumed to describe a member which
  /// will be invoked. Otherwise, the function will be found via argument
  /// dependent lookup.
  /// CallExpr is set to a valid expression and FRS_Success returned on success,
  /// otherwise CallExpr is set to ExprError() and some non-success value
  /// is returned.
  ForRangeStatus BuildForRangeBeginEndCall(SourceLocation Loc,
                                           SourceLocation RangeLoc,
                                           const DeclarationNameInfo &NameInfo,
                                           LookupResult &MemberLookup,
                                           OverloadCandidateSet *CandidateSet,
                                           Expr *Range, ExprResult *CallExpr);

  /// BuildOverloadedCallExpr - Given the call expression that calls Fn
  /// (which eventually refers to the declaration Func) and the call
  /// arguments Args/NumArgs, attempt to resolve the function call down
  /// to a specific function. If overload resolution succeeds, returns
  /// the call expression produced by overload resolution.
  /// Otherwise, emits diagnostics and returns ExprError.
  ExprResult BuildOverloadedCallExpr(
      Scope *S, Expr *Fn, UnresolvedLookupExpr *ULE, SourceLocation LParenLoc,
      MultiExprArg Args, SourceLocation RParenLoc, Expr *ExecConfig,
      bool AllowTypoCorrection = true, bool CalleesAddressIsTaken = false);

  /// Constructs and populates an OverloadedCandidateSet from
  /// the given function.
  /// \returns true when an the ExprResult output parameter has been set.
  bool buildOverloadedCallSet(Scope *S, Expr *Fn, UnresolvedLookupExpr *ULE,
                              MultiExprArg Args, SourceLocation RParenLoc,
                              OverloadCandidateSet *CandidateSet,
                              ExprResult *Result);

  ExprResult CreateUnresolvedLookupExpr(CXXRecordDecl *NamingClass,
                                        NestedNameSpecifierLoc NNSLoc,
                                        DeclarationNameInfo DNI,
                                        const UnresolvedSetImpl &Fns,
                                        bool PerformADL = true);

  /// Create a unary operation that may resolve to an overloaded
  /// operator.
  ///
  /// \param OpLoc The location of the operator itself (e.g., '*').
  ///
  /// \param Opc The UnaryOperatorKind that describes this operator.
  ///
  /// \param Fns The set of non-member functions that will be
  /// considered by overload resolution. The caller needs to build this
  /// set based on the context using, e.g.,
  /// LookupOverloadedOperatorName() and ArgumentDependentLookup(). This
  /// set should not contain any member functions; those will be added
  /// by CreateOverloadedUnaryOp().
  ///
  /// \param Input The input argument.
  ExprResult CreateOverloadedUnaryOp(SourceLocation OpLoc,
                                     UnaryOperatorKind Opc,
                                     const UnresolvedSetImpl &Fns, Expr *input,
                                     bool RequiresADL = true);

  /// Perform lookup for an overloaded binary operator.
  void LookupOverloadedBinOp(OverloadCandidateSet &CandidateSet,
                             OverloadedOperatorKind Op,
                             const UnresolvedSetImpl &Fns,
                             ArrayRef<Expr *> Args, bool RequiresADL = true);

  /// Create a binary operation that may resolve to an overloaded
  /// operator.
  ///
  /// \param OpLoc The location of the operator itself (e.g., '+').
  ///
  /// \param Opc The BinaryOperatorKind that describes this operator.
  ///
  /// \param Fns The set of non-member functions that will be
  /// considered by overload resolution. The caller needs to build this
  /// set based on the context using, e.g.,
  /// LookupOverloadedOperatorName() and ArgumentDependentLookup(). This
  /// set should not contain any member functions; those will be added
  /// by CreateOverloadedBinOp().
  ///
  /// \param LHS Left-hand argument.
  /// \param RHS Right-hand argument.
  /// \param PerformADL Whether to consider operator candidates found by ADL.
  /// \param AllowRewrittenCandidates Whether to consider candidates found by
  ///        C++20 operator rewrites.
  /// \param DefaultedFn If we are synthesizing a defaulted operator function,
  ///        the function in question. Such a function is never a candidate in
  ///        our overload resolution. This also enables synthesizing a three-way
  ///        comparison from < and == as described in C++20 [class.spaceship]p1.
  ExprResult CreateOverloadedBinOp(SourceLocation OpLoc, BinaryOperatorKind Opc,
                                   const UnresolvedSetImpl &Fns, Expr *LHS,
                                   Expr *RHS, bool RequiresADL = true,
                                   bool AllowRewrittenCandidates = true,
                                   FunctionDecl *DefaultedFn = nullptr);
  ExprResult BuildSynthesizedThreeWayComparison(SourceLocation OpLoc,
                                                const UnresolvedSetImpl &Fns,
                                                Expr *LHS, Expr *RHS,
                                                FunctionDecl *DefaultedFn);

  ExprResult CreateOverloadedArraySubscriptExpr(SourceLocation LLoc,
                                                SourceLocation RLoc, Expr *Base,
                                                MultiExprArg Args);

  /// BuildCallToMemberFunction - Build a call to a member
  /// function. MemExpr is the expression that refers to the member
  /// function (and includes the object parameter), Args/NumArgs are the
  /// arguments to the function call (not including the object
  /// parameter). The caller needs to validate that the member
  /// expression refers to a non-static member function or an overloaded
  /// member function.
  ExprResult BuildCallToMemberFunction(
      Scope *S, Expr *MemExpr, SourceLocation LParenLoc, MultiExprArg Args,
      SourceLocation RParenLoc, Expr *ExecConfig = nullptr,
      bool IsExecConfig = false, bool AllowRecovery = false);

  /// BuildCallToObjectOfClassType - Build a call to an object of class
  /// type (C++ [over.call.object]), which can end up invoking an
  /// overloaded function call operator (@c operator()) or performing a
  /// user-defined conversion on the object argument.
  ExprResult BuildCallToObjectOfClassType(Scope *S, Expr *Object,
                                          SourceLocation LParenLoc,
                                          MultiExprArg Args,
                                          SourceLocation RParenLoc);

  /// BuildOverloadedArrowExpr - Build a call to an overloaded @c operator->
  ///  (if one exists), where @c Base is an expression of class type and
  /// @c Member is the name of the member we're trying to find.
  ExprResult BuildOverloadedArrowExpr(Scope *S, Expr *Base,
                                      SourceLocation OpLoc,
                                      bool *NoArrowOperatorFound = nullptr);

  ExprResult BuildCXXMemberCallExpr(Expr *Exp, NamedDecl *FoundDecl,
                                    CXXConversionDecl *Method,
                                    bool HadMultipleCandidates);

  /// BuildLiteralOperatorCall - Build a UserDefinedLiteral by creating a call
  /// to a literal operator described by the provided lookup results.
  ExprResult BuildLiteralOperatorCall(
      LookupResult &R, DeclarationNameInfo &SuffixInfo, ArrayRef<Expr *> Args,
      SourceLocation LitEndLoc,
      TemplateArgumentListInfo *ExplicitTemplateArgs = nullptr);

  /// FixOverloadedFunctionReference - E is an expression that refers to
  /// a C++ overloaded function (possibly with some parentheses and
  /// perhaps a '&' around it). We have resolved the overloaded function
  /// to the function declaration Fn, so patch up the expression E to
  /// refer (possibly indirectly) to Fn. Returns the new expr.
  ExprResult FixOverloadedFunctionReference(Expr *E, DeclAccessPair FoundDecl,
                                            FunctionDecl *Fn);
  ExprResult FixOverloadedFunctionReference(ExprResult,
                                            DeclAccessPair FoundDecl,
                                            FunctionDecl *Fn);

  /// - Returns a selector which best matches given argument list or
  /// nullptr if none could be found
  ObjCMethodDecl *SelectBestMethod(Selector Sel, MultiExprArg Args,
                                   bool IsInstance,
                                   SmallVectorImpl<ObjCMethodDecl *> &Methods);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Statements
  /// Implementations are in SemaStmt.cpp
  ///@{

public:
  /// Stack of active SEH __finally scopes.  Can be empty.
  SmallVector<Scope *, 2> CurrentSEHFinally;

  StmtResult ActOnExprStmt(ExprResult Arg, bool DiscardedValue = true);
  StmtResult ActOnExprStmtError();

  StmtResult ActOnNullStmt(SourceLocation SemiLoc,
                           bool HasLeadingEmptyMacro = false);

  StmtResult ActOnDeclStmt(DeclGroupPtrTy Decl, SourceLocation StartLoc,
                           SourceLocation EndLoc);
  void ActOnForEachDeclStmt(DeclGroupPtrTy Decl);

  /// DiagnoseUnusedExprResult - If the statement passed in is an expression
  /// whose result is unused, warn.
  void DiagnoseUnusedExprResult(const Stmt *S, unsigned DiagID);

  void ActOnStartOfCompoundStmt(bool IsStmtExpr);
  void ActOnAfterCompoundStatementLeadingPragmas();
  void ActOnFinishOfCompoundStmt();
  StmtResult ActOnCompoundStmt(SourceLocation L, SourceLocation R,
                               ArrayRef<Stmt *> Elts, bool isStmtExpr);

  sema::CompoundScopeInfo &getCurCompoundScope() const;

  ExprResult ActOnCaseExpr(SourceLocation CaseLoc, ExprResult Val);
  StmtResult ActOnCaseStmt(SourceLocation CaseLoc, ExprResult LHS,
                           SourceLocation DotDotDotLoc, ExprResult RHS,
                           SourceLocation ColonLoc);

  /// ActOnCaseStmtBody - This installs a statement as the body of a case.
  void ActOnCaseStmtBody(Stmt *CaseStmt, Stmt *SubStmt);

  StmtResult ActOnDefaultStmt(SourceLocation DefaultLoc,
                              SourceLocation ColonLoc, Stmt *SubStmt,
                              Scope *CurScope);
  StmtResult ActOnLabelStmt(SourceLocation IdentLoc, LabelDecl *TheDecl,
                            SourceLocation ColonLoc, Stmt *SubStmt);

  StmtResult BuildAttributedStmt(SourceLocation AttrsLoc,
                                 ArrayRef<const Attr *> Attrs, Stmt *SubStmt);
  StmtResult ActOnAttributedStmt(const ParsedAttributes &AttrList,
                                 Stmt *SubStmt);

  /// Check whether the given statement can have musttail applied to it,
  /// issuing a diagnostic and returning false if not. In the success case,
  /// the statement is rewritten to remove implicit nodes from the return
  /// value.
  bool checkAndRewriteMustTailAttr(Stmt *St, const Attr &MTA);

  StmtResult ActOnIfStmt(SourceLocation IfLoc, IfStatementKind StatementKind,
                         SourceLocation LParenLoc, Stmt *InitStmt,
                         ConditionResult Cond, SourceLocation RParenLoc,
                         Stmt *ThenVal, SourceLocation ElseLoc, Stmt *ElseVal);
  StmtResult BuildIfStmt(SourceLocation IfLoc, IfStatementKind StatementKind,
                         SourceLocation LParenLoc, Stmt *InitStmt,
                         ConditionResult Cond, SourceLocation RParenLoc,
                         Stmt *ThenVal, SourceLocation ElseLoc, Stmt *ElseVal);

  ExprResult CheckSwitchCondition(SourceLocation SwitchLoc, Expr *Cond);

  StmtResult ActOnStartOfSwitchStmt(SourceLocation SwitchLoc,
                                    SourceLocation LParenLoc, Stmt *InitStmt,
                                    ConditionResult Cond,
                                    SourceLocation RParenLoc);
  StmtResult ActOnFinishSwitchStmt(SourceLocation SwitchLoc, Stmt *Switch,
                                   Stmt *Body);

  /// DiagnoseAssignmentEnum - Warn if assignment to enum is a constant
  /// integer not in the range of enum values.
  void DiagnoseAssignmentEnum(QualType DstType, QualType SrcType,
                              Expr *SrcExpr);

  StmtResult ActOnWhileStmt(SourceLocation WhileLoc, SourceLocation LParenLoc,
                            ConditionResult Cond, SourceLocation RParenLoc,
                            Stmt *Body);
  StmtResult ActOnDoStmt(SourceLocation DoLoc, Stmt *Body,
                         SourceLocation WhileLoc, SourceLocation CondLParen,
                         Expr *Cond, SourceLocation CondRParen);

  StmtResult ActOnForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                          Stmt *First, ConditionResult Second,
                          FullExprArg Third, SourceLocation RParenLoc,
                          Stmt *Body);

  /// In an Objective C collection iteration statement:
  ///   for (x in y)
  /// x can be an arbitrary l-value expression.  Bind it up as a
  /// full-expression.
  StmtResult ActOnForEachLValueExpr(Expr *E);

  enum BuildForRangeKind {
    /// Initial building of a for-range statement.
    BFRK_Build,
    /// Instantiation or recovery rebuild of a for-range statement. Don't
    /// attempt any typo-correction.
    BFRK_Rebuild,
    /// Determining whether a for-range statement could be built. Avoid any
    /// unnecessary or irreversible actions.
    BFRK_Check
  };

  /// ActOnCXXForRangeStmt - Check and build a C++11 for-range statement.
  ///
  /// C++11 [stmt.ranged]:
  ///   A range-based for statement is equivalent to
  ///
  ///   {
  ///     auto && __range = range-init;
  ///     for ( auto __begin = begin-expr,
  ///           __end = end-expr;
  ///           __begin != __end;
  ///           ++__begin ) {
  ///       for-range-declaration = *__begin;
  ///       statement
  ///     }
  ///   }
  ///
  /// The body of the loop is not available yet, since it cannot be analysed
  /// until we have determined the type of the for-range-declaration.
  StmtResult ActOnCXXForRangeStmt(
      Scope *S, SourceLocation ForLoc, SourceLocation CoawaitLoc,
      Stmt *InitStmt, Stmt *LoopVar, SourceLocation ColonLoc, Expr *Collection,
      SourceLocation RParenLoc, BuildForRangeKind Kind,
      ArrayRef<MaterializeTemporaryExpr *> LifetimeExtendTemps = {});

  /// BuildCXXForRangeStmt - Build or instantiate a C++11 for-range statement.
  StmtResult BuildCXXForRangeStmt(
      SourceLocation ForLoc, SourceLocation CoawaitLoc, Stmt *InitStmt,
      SourceLocation ColonLoc, Stmt *RangeDecl, Stmt *Begin, Stmt *End,
      Expr *Cond, Expr *Inc, Stmt *LoopVarDecl, SourceLocation RParenLoc,
      BuildForRangeKind Kind,
      ArrayRef<MaterializeTemporaryExpr *> LifetimeExtendTemps = {});

  /// FinishCXXForRangeStmt - Attach the body to a C++0x for-range statement.
  /// This is a separate step from ActOnCXXForRangeStmt because analysis of the
  /// body cannot be performed until after the type of the range variable is
  /// determined.
  StmtResult FinishCXXForRangeStmt(Stmt *ForRange, Stmt *Body);

  StmtResult ActOnGotoStmt(SourceLocation GotoLoc, SourceLocation LabelLoc,
                           LabelDecl *TheDecl);
  StmtResult ActOnIndirectGotoStmt(SourceLocation GotoLoc,
                                   SourceLocation StarLoc, Expr *DestExp);
  StmtResult ActOnContinueStmt(SourceLocation ContinueLoc, Scope *CurScope);
  StmtResult ActOnBreakStmt(SourceLocation BreakLoc, Scope *CurScope);

  struct NamedReturnInfo {
    const VarDecl *Candidate;

    enum Status : uint8_t { None, MoveEligible, MoveEligibleAndCopyElidable };
    Status S;

    bool isMoveEligible() const { return S != None; };
    bool isCopyElidable() const { return S == MoveEligibleAndCopyElidable; }
  };
  enum class SimplerImplicitMoveMode { ForceOff, Normal, ForceOn };

  /// Determine whether the given expression might be move-eligible or
  /// copy-elidable in either a (co_)return statement or throw expression,
  /// without considering function return type, if applicable.
  ///
  /// \param E The expression being returned from the function or block,
  /// being thrown, or being co_returned from a coroutine. This expression
  /// might be modified by the implementation.
  ///
  /// \param Mode Overrides detection of current language mode
  /// and uses the rules for C++23.
  ///
  /// \returns An aggregate which contains the Candidate and isMoveEligible
  /// and isCopyElidable methods. If Candidate is non-null, it means
  /// isMoveEligible() would be true under the most permissive language
  /// standard.
  NamedReturnInfo getNamedReturnInfo(
      Expr *&E, SimplerImplicitMoveMode Mode = SimplerImplicitMoveMode::Normal);

  /// Determine whether the given NRVO candidate variable is move-eligible or
  /// copy-elidable, without considering function return type.
  ///
  /// \param VD The NRVO candidate variable.
  ///
  /// \returns An aggregate which contains the Candidate and isMoveEligible
  /// and isCopyElidable methods. If Candidate is non-null, it means
  /// isMoveEligible() would be true under the most permissive language
  /// standard.
  NamedReturnInfo getNamedReturnInfo(const VarDecl *VD);

  /// Updates given NamedReturnInfo's move-eligible and
  /// copy-elidable statuses, considering the function
  /// return type criteria as applicable to return statements.
  ///
  /// \param Info The NamedReturnInfo object to update.
  ///
  /// \param ReturnType This is the return type of the function.
  /// \returns The copy elision candidate, in case the initial return expression
  /// was copy elidable, or nullptr otherwise.
  const VarDecl *getCopyElisionCandidate(NamedReturnInfo &Info,
                                         QualType ReturnType);

  /// Perform the initialization of a potentially-movable value, which
  /// is the result of return value.
  ///
  /// This routine implements C++20 [class.copy.elision]p3, which attempts to
  /// treat returned lvalues as rvalues in certain cases (to prefer move
  /// construction), then falls back to treating them as lvalues if that failed.
  ExprResult
  PerformMoveOrCopyInitialization(const InitializedEntity &Entity,
                                  const NamedReturnInfo &NRInfo, Expr *Value,
                                  bool SupressSimplerImplicitMoves = false);

  TypeLoc getReturnTypeLoc(FunctionDecl *FD) const;

  /// Deduce the return type for a function from a returned expression, per
  /// C++1y [dcl.spec.auto]p6.
  bool DeduceFunctionTypeFromReturnExpr(FunctionDecl *FD,
                                        SourceLocation ReturnLoc, Expr *RetExpr,
                                        const AutoType *AT);

  StmtResult ActOnReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                             Scope *CurScope);
  StmtResult BuildReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                             bool AllowRecovery = false);

  /// ActOnCapScopeReturnStmt - Utility routine to type-check return statements
  /// for capturing scopes.
  StmtResult ActOnCapScopeReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp,
                                     NamedReturnInfo &NRInfo,
                                     bool SupressSimplerImplicitMoves);

  /// ActOnCXXCatchBlock - Takes an exception declaration and a handler block
  /// and creates a proper catch handler from them.
  StmtResult ActOnCXXCatchBlock(SourceLocation CatchLoc, Decl *ExDecl,
                                Stmt *HandlerBlock);

  /// ActOnCXXTryBlock - Takes a try compound-statement and a number of
  /// handlers and creates a try statement from them.
  StmtResult ActOnCXXTryBlock(SourceLocation TryLoc, Stmt *TryBlock,
                              ArrayRef<Stmt *> Handlers);

  StmtResult ActOnSEHTryBlock(bool IsCXXTry, // try (true) or __try (false) ?
                              SourceLocation TryLoc, Stmt *TryBlock,
                              Stmt *Handler);
  StmtResult ActOnSEHExceptBlock(SourceLocation Loc, Expr *FilterExpr,
                                 Stmt *Block);
  void ActOnStartSEHFinallyBlock();
  void ActOnAbortSEHFinallyBlock();
  StmtResult ActOnFinishSEHFinallyBlock(SourceLocation Loc, Stmt *Block);
  StmtResult ActOnSEHLeaveStmt(SourceLocation Loc, Scope *CurScope);

  StmtResult BuildMSDependentExistsStmt(SourceLocation KeywordLoc,
                                        bool IsIfExists,
                                        NestedNameSpecifierLoc QualifierLoc,
                                        DeclarationNameInfo NameInfo,
                                        Stmt *Nested);
  StmtResult ActOnMSDependentExistsStmt(SourceLocation KeywordLoc,
                                        bool IsIfExists, CXXScopeSpec &SS,
                                        UnqualifiedId &Name, Stmt *Nested);

  void ActOnCapturedRegionStart(SourceLocation Loc, Scope *CurScope,
                                CapturedRegionKind Kind, unsigned NumParams);
  typedef std::pair<StringRef, QualType> CapturedParamNameType;
  void ActOnCapturedRegionStart(SourceLocation Loc, Scope *CurScope,
                                CapturedRegionKind Kind,
                                ArrayRef<CapturedParamNameType> Params,
                                unsigned OpenMPCaptureLevel = 0);
  StmtResult ActOnCapturedRegionEnd(Stmt *S);
  void ActOnCapturedRegionError();
  RecordDecl *CreateCapturedStmtRecordDecl(CapturedDecl *&CD,
                                           SourceLocation Loc,
                                           unsigned NumParams);

private:
  /// Check whether the given statement can have musttail applied to it,
  /// issuing a diagnostic and returning false if not.
  bool checkMustTailAttr(const Stmt *St, const Attr &MTA);

  /// Check if the given expression contains 'break' or 'continue'
  /// statement that produces control flow different from GCC.
  void CheckBreakContinueBinding(Expr *E);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name `inline asm` Statement
  /// Implementations are in SemaStmtAsm.cpp
  ///@{

public:
  StmtResult ActOnGCCAsmStmt(SourceLocation AsmLoc, bool IsSimple,
                             bool IsVolatile, unsigned NumOutputs,
                             unsigned NumInputs, IdentifierInfo **Names,
                             MultiExprArg Constraints, MultiExprArg Exprs,
                             Expr *AsmString, MultiExprArg Clobbers,
                             unsigned NumLabels, SourceLocation RParenLoc);

  void FillInlineAsmIdentifierInfo(Expr *Res,
                                   llvm::InlineAsmIdentifierInfo &Info);
  ExprResult LookupInlineAsmIdentifier(CXXScopeSpec &SS,
                                       SourceLocation TemplateKWLoc,
                                       UnqualifiedId &Id,
                                       bool IsUnevaluatedContext);
  bool LookupInlineAsmField(StringRef Base, StringRef Member, unsigned &Offset,
                            SourceLocation AsmLoc);
  ExprResult LookupInlineAsmVarDeclField(Expr *RefExpr, StringRef Member,
                                         SourceLocation AsmLoc);
  StmtResult ActOnMSAsmStmt(SourceLocation AsmLoc, SourceLocation LBraceLoc,
                            ArrayRef<Token> AsmToks, StringRef AsmString,
                            unsigned NumOutputs, unsigned NumInputs,
                            ArrayRef<StringRef> Constraints,
                            ArrayRef<StringRef> Clobbers,
                            ArrayRef<Expr *> Exprs, SourceLocation EndLoc);
  LabelDecl *GetOrCreateMSAsmLabel(StringRef ExternalLabelName,
                                   SourceLocation Location, bool AlwaysCreate);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Statement Attribute Handling
  /// Implementations are in SemaStmtAttr.cpp
  ///@{

public:
  bool CheckNoInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                         const AttributeCommonInfo &A);
  bool CheckAlwaysInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                             const AttributeCommonInfo &A);

  CodeAlignAttr *BuildCodeAlignAttr(const AttributeCommonInfo &CI, Expr *E);
  bool CheckRebuiltStmtAttributes(ArrayRef<const Attr *> Attrs);

  /// Process the attributes before creating an attributed statement. Returns
  /// the semantic attributes that have been processed.
  void ProcessStmtAttributes(Stmt *Stmt, const ParsedAttributes &InAttrs,
                             SmallVectorImpl<const Attr *> &OutAttrs);

  ExprResult ActOnCXXAssumeAttr(Stmt *St, const ParsedAttr &A,
                                SourceRange Range);
  ExprResult BuildCXXAssumeExpr(Expr *Assumption,
                                const IdentifierInfo *AttrName,
                                SourceRange Range);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Templates
  /// Implementations are in SemaTemplate.cpp
  ///@{

public:
  // Saves the current floating-point pragma stack and clear it in this Sema.
  class FpPragmaStackSaveRAII {
  public:
    FpPragmaStackSaveRAII(Sema &S)
        : S(S), SavedStack(std::move(S.FpPragmaStack)) {
      S.FpPragmaStack.Stack.clear();
    }
    ~FpPragmaStackSaveRAII() { S.FpPragmaStack = std::move(SavedStack); }

  private:
    Sema &S;
    PragmaStack<FPOptionsOverride> SavedStack;
  };

  void resetFPOptions(FPOptions FPO) {
    CurFPFeatures = FPO;
    FpPragmaStack.CurrentValue = FPO.getChangesFrom(FPOptions(LangOpts));
  }

  ArrayRef<InventedTemplateParameterInfo> getInventedParameterInfos() const {
    return llvm::ArrayRef(InventedParameterInfos.begin() +
                              InventedParameterInfosStart,
                          InventedParameterInfos.end());
  }

  /// The number of SFINAE diagnostics that have been trapped.
  unsigned NumSFINAEErrors;

  ArrayRef<sema::FunctionScopeInfo *> getFunctionScopes() const {
    return llvm::ArrayRef(FunctionScopes.begin() + FunctionScopesStart,
                          FunctionScopes.end());
  }

  typedef llvm::MapVector<const FunctionDecl *,
                          std::unique_ptr<LateParsedTemplate>>
      LateParsedTemplateMapT;
  LateParsedTemplateMapT LateParsedTemplateMap;

  /// Determine the number of levels of enclosing template parameters. This is
  /// only usable while parsing. Note that this does not include dependent
  /// contexts in which no template parameters have yet been declared, such as
  /// in a terse function template or generic lambda before the first 'auto' is
  /// encountered.
  unsigned getTemplateDepth(Scope *S) const;

  void FilterAcceptableTemplateNames(LookupResult &R,
                                     bool AllowFunctionTemplates = true,
                                     bool AllowDependent = true);
  bool hasAnyAcceptableTemplateNames(LookupResult &R,
                                     bool AllowFunctionTemplates = true,
                                     bool AllowDependent = true,
                                     bool AllowNonTemplateFunctions = false);
  /// Try to interpret the lookup result D as a template-name.
  ///
  /// \param D A declaration found by name lookup.
  /// \param AllowFunctionTemplates Whether function templates should be
  ///        considered valid results.
  /// \param AllowDependent Whether unresolved using declarations (that might
  ///        name templates) should be considered valid results.
  static NamedDecl *getAsTemplateNameDecl(NamedDecl *D,
                                          bool AllowFunctionTemplates = true,
                                          bool AllowDependent = true);

  enum TemplateNameIsRequiredTag { TemplateNameIsRequired };
  /// Whether and why a template name is required in this lookup.
  class RequiredTemplateKind {
  public:
    /// Template name is required if TemplateKWLoc is valid.
    RequiredTemplateKind(SourceLocation TemplateKWLoc = SourceLocation())
        : TemplateKW(TemplateKWLoc) {}
    /// Template name is unconditionally required.
    RequiredTemplateKind(TemplateNameIsRequiredTag) {}

    SourceLocation getTemplateKeywordLoc() const {
      return TemplateKW.value_or(SourceLocation());
    }
    bool hasTemplateKeyword() const {
      return getTemplateKeywordLoc().isValid();
    }
    bool isRequired() const { return TemplateKW != SourceLocation(); }
    explicit operator bool() const { return isRequired(); }

  private:
    std::optional<SourceLocation> TemplateKW;
  };

  enum class AssumedTemplateKind {
    /// This is not assumed to be a template name.
    None,
    /// This is assumed to be a template name because lookup found nothing.
    FoundNothing,
    /// This is assumed to be a template name because lookup found one or more
    /// functions (but no function templates).
    FoundFunctions,
  };

  bool
  LookupTemplateName(LookupResult &R, Scope *S, CXXScopeSpec &SS,
                     QualType ObjectType, bool EnteringContext,
                     RequiredTemplateKind RequiredTemplate = SourceLocation(),
                     AssumedTemplateKind *ATK = nullptr,
                     bool AllowTypoCorrection = true);

  TemplateNameKind isTemplateName(Scope *S, CXXScopeSpec &SS,
                                  bool hasTemplateKeyword,
                                  const UnqualifiedId &Name,
                                  ParsedType ObjectType, bool EnteringContext,
                                  TemplateTy &Template,
                                  bool &MemberOfUnknownSpecialization,
                                  bool Disambiguation = false);

  /// Try to resolve an undeclared template name as a type template.
  ///
  /// Sets II to the identifier corresponding to the template name, and updates
  /// Name to a corresponding (typo-corrected) type template name and TNK to
  /// the corresponding kind, if possible.
  void ActOnUndeclaredTypeTemplateName(Scope *S, TemplateTy &Name,
                                       TemplateNameKind &TNK,
                                       SourceLocation NameLoc,
                                       IdentifierInfo *&II);

  bool resolveAssumedTemplateNameAsType(Scope *S, TemplateName &Name,
                                        SourceLocation NameLoc,
                                        bool Diagnose = true);

  /// Determine whether a particular identifier might be the name in a C++1z
  /// deduction-guide declaration.
  bool isDeductionGuideName(Scope *S, const IdentifierInfo &Name,
                            SourceLocation NameLoc, CXXScopeSpec &SS,
                            ParsedTemplateTy *Template = nullptr);

  bool DiagnoseUnknownTemplateName(const IdentifierInfo &II,
                                   SourceLocation IILoc, Scope *S,
                                   const CXXScopeSpec *SS,
                                   TemplateTy &SuggestedTemplate,
                                   TemplateNameKind &SuggestedKind);

  /// Determine whether we would be unable to instantiate this template (because
  /// it either has no definition, or is in the process of being instantiated).
  bool DiagnoseUninstantiableTemplate(SourceLocation PointOfInstantiation,
                                      NamedDecl *Instantiation,
                                      bool InstantiatedFromMember,
                                      const NamedDecl *Pattern,
                                      const NamedDecl *PatternDef,
                                      TemplateSpecializationKind TSK,
                                      bool Complain = true);

  /// DiagnoseTemplateParameterShadow - Produce a diagnostic complaining
  /// that the template parameter 'PrevDecl' is being shadowed by a new
  /// declaration at location Loc. Returns true to indicate that this is
  /// an error, and false otherwise.
  ///
  /// \param Loc The location of the declaration that shadows a template
  ///            parameter.
  ///
  /// \param PrevDecl The template parameter that the declaration shadows.
  ///
  /// \param SupportedForCompatibility Whether to issue the diagnostic as
  ///        a warning for compatibility with older versions of clang.
  ///        Ignored when MSVC compatibility is enabled.
  void DiagnoseTemplateParameterShadow(SourceLocation Loc, Decl *PrevDecl,
                                       bool SupportedForCompatibility = false);

  /// AdjustDeclIfTemplate - If the given decl happens to be a template, reset
  /// the parameter D to reference the templated declaration and return a
  /// pointer to the template declaration. Otherwise, do nothing to D and return
  /// null.
  TemplateDecl *AdjustDeclIfTemplate(Decl *&Decl);

  /// ActOnTypeParameter - Called when a C++ template type parameter
  /// (e.g., "typename T") has been parsed. Typename specifies whether
  /// the keyword "typename" was used to declare the type parameter
  /// (otherwise, "class" was used), and KeyLoc is the location of the
  /// "class" or "typename" keyword. ParamName is the name of the
  /// parameter (NULL indicates an unnamed template parameter) and
  /// ParamNameLoc is the location of the parameter name (if any).
  /// If the type parameter has a default argument, it will be added
  /// later via ActOnTypeParameterDefault.
  NamedDecl *ActOnTypeParameter(Scope *S, bool Typename,
                                SourceLocation EllipsisLoc,
                                SourceLocation KeyLoc,
                                IdentifierInfo *ParamName,
                                SourceLocation ParamNameLoc, unsigned Depth,
                                unsigned Position, SourceLocation EqualLoc,
                                ParsedType DefaultArg, bool HasTypeConstraint);

  bool CheckTypeConstraint(TemplateIdAnnotation *TypeConstraint);

  bool ActOnTypeConstraint(const CXXScopeSpec &SS,
                           TemplateIdAnnotation *TypeConstraint,
                           TemplateTypeParmDecl *ConstrainedParameter,
                           SourceLocation EllipsisLoc);
  bool BuildTypeConstraint(const CXXScopeSpec &SS,
                           TemplateIdAnnotation *TypeConstraint,
                           TemplateTypeParmDecl *ConstrainedParameter,
                           SourceLocation EllipsisLoc,
                           bool AllowUnexpandedPack);

  /// Attach a type-constraint to a template parameter.
  /// \returns true if an error occurred. This can happen if the
  /// immediately-declared constraint could not be formed (e.g. incorrect number
  /// of arguments for the named concept).
  bool AttachTypeConstraint(NestedNameSpecifierLoc NS,
                            DeclarationNameInfo NameInfo,
                            ConceptDecl *NamedConcept, NamedDecl *FoundDecl,
                            const TemplateArgumentListInfo *TemplateArgs,
                            TemplateTypeParmDecl *ConstrainedParameter,
                            SourceLocation EllipsisLoc);

  bool AttachTypeConstraint(AutoTypeLoc TL,
                            NonTypeTemplateParmDecl *NewConstrainedParm,
                            NonTypeTemplateParmDecl *OrigConstrainedParm,
                            SourceLocation EllipsisLoc);

  /// Require the given type to be a structural type, and diagnose if it is not.
  ///
  /// \return \c true if an error was produced.
  bool RequireStructuralType(QualType T, SourceLocation Loc);

  /// Check that the type of a non-type template parameter is
  /// well-formed.
  ///
  /// \returns the (possibly-promoted) parameter type if valid;
  /// otherwise, produces a diagnostic and returns a NULL type.
  QualType CheckNonTypeTemplateParameterType(TypeSourceInfo *&TSI,
                                             SourceLocation Loc);
  QualType CheckNonTypeTemplateParameterType(QualType T, SourceLocation Loc);

  NamedDecl *ActOnNonTypeTemplateParameter(Scope *S, Declarator &D,
                                           unsigned Depth, unsigned Position,
                                           SourceLocation EqualLoc,
                                           Expr *DefaultArg);

  /// ActOnTemplateTemplateParameter - Called when a C++ template template
  /// parameter (e.g. T in template <template \<typename> class T> class array)
  /// has been parsed. S is the current scope.
  NamedDecl *ActOnTemplateTemplateParameter(
      Scope *S, SourceLocation TmpLoc, TemplateParameterList *Params,
      bool Typename, SourceLocation EllipsisLoc, IdentifierInfo *ParamName,
      SourceLocation ParamNameLoc, unsigned Depth, unsigned Position,
      SourceLocation EqualLoc, ParsedTemplateArgument DefaultArg);

  /// ActOnTemplateParameterList - Builds a TemplateParameterList, optionally
  /// constrained by RequiresClause, that contains the template parameters in
  /// Params.
  TemplateParameterList *ActOnTemplateParameterList(
      unsigned Depth, SourceLocation ExportLoc, SourceLocation TemplateLoc,
      SourceLocation LAngleLoc, ArrayRef<NamedDecl *> Params,
      SourceLocation RAngleLoc, Expr *RequiresClause);

  /// The context in which we are checking a template parameter list.
  enum TemplateParamListContext {
    TPC_ClassTemplate,
    TPC_VarTemplate,
    TPC_FunctionTemplate,
    TPC_ClassTemplateMember,
    TPC_FriendClassTemplate,
    TPC_FriendFunctionTemplate,
    TPC_FriendFunctionTemplateDefinition,
    TPC_TypeAliasTemplate
  };

  /// Checks the validity of a template parameter list, possibly
  /// considering the template parameter list from a previous
  /// declaration.
  ///
  /// If an "old" template parameter list is provided, it must be
  /// equivalent (per TemplateParameterListsAreEqual) to the "new"
  /// template parameter list.
  ///
  /// \param NewParams Template parameter list for a new template
  /// declaration. This template parameter list will be updated with any
  /// default arguments that are carried through from the previous
  /// template parameter list.
  ///
  /// \param OldParams If provided, template parameter list from a
  /// previous declaration of the same template. Default template
  /// arguments will be merged from the old template parameter list to
  /// the new template parameter list.
  ///
  /// \param TPC Describes the context in which we are checking the given
  /// template parameter list.
  ///
  /// \param SkipBody If we might have already made a prior merged definition
  /// of this template visible, the corresponding body-skipping information.
  /// Default argument redefinition is not an error when skipping such a body,
  /// because (under the ODR) we can assume the default arguments are the same
  /// as the prior merged definition.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool CheckTemplateParameterList(TemplateParameterList *NewParams,
                                  TemplateParameterList *OldParams,
                                  TemplateParamListContext TPC,
                                  SkipBodyInfo *SkipBody = nullptr);

  /// Match the given template parameter lists to the given scope
  /// specifier, returning the template parameter list that applies to the
  /// name.
  ///
  /// \param DeclStartLoc the start of the declaration that has a scope
  /// specifier or a template parameter list.
  ///
  /// \param DeclLoc The location of the declaration itself.
  ///
  /// \param SS the scope specifier that will be matched to the given template
  /// parameter lists. This scope specifier precedes a qualified name that is
  /// being declared.
  ///
  /// \param TemplateId The template-id following the scope specifier, if there
  /// is one. Used to check for a missing 'template<>'.
  ///
  /// \param ParamLists the template parameter lists, from the outermost to the
  /// innermost template parameter lists.
  ///
  /// \param IsFriend Whether to apply the slightly different rules for
  /// matching template parameters to scope specifiers in friend
  /// declarations.
  ///
  /// \param IsMemberSpecialization will be set true if the scope specifier
  /// denotes a fully-specialized type, and therefore this is a declaration of
  /// a member specialization.
  ///
  /// \returns the template parameter list, if any, that corresponds to the
  /// name that is preceded by the scope specifier @p SS. This template
  /// parameter list may have template parameters (if we're declaring a
  /// template) or may have no template parameters (if we're declaring a
  /// template specialization), or may be NULL (if what we're declaring isn't
  /// itself a template).
  TemplateParameterList *MatchTemplateParametersToScopeSpecifier(
      SourceLocation DeclStartLoc, SourceLocation DeclLoc,
      const CXXScopeSpec &SS, TemplateIdAnnotation *TemplateId,
      ArrayRef<TemplateParameterList *> ParamLists, bool IsFriend,
      bool &IsMemberSpecialization, bool &Invalid,
      bool SuppressDiagnostic = false);

  /// Returns the template parameter list with all default template argument
  /// information.
  TemplateParameterList *GetTemplateParameterList(TemplateDecl *TD);

  DeclResult CheckClassTemplate(
      Scope *S, unsigned TagSpec, TagUseKind TUK, SourceLocation KWLoc,
      CXXScopeSpec &SS, IdentifierInfo *Name, SourceLocation NameLoc,
      const ParsedAttributesView &Attr, TemplateParameterList *TemplateParams,
      AccessSpecifier AS, SourceLocation ModulePrivateLoc,
      SourceLocation FriendLoc, unsigned NumOuterTemplateParamLists,
      TemplateParameterList **OuterTemplateParamLists,
      SkipBodyInfo *SkipBody = nullptr);

  /// Translates template arguments as provided by the parser
  /// into template arguments used by semantic analysis.
  void translateTemplateArguments(const ASTTemplateArgsPtr &In,
                                  TemplateArgumentListInfo &Out);

  /// Convert a parsed type into a parsed template argument. This is mostly
  /// trivial, except that we may have parsed a C++17 deduced class template
  /// specialization type, in which case we should form a template template
  /// argument instead of a type template argument.
  ParsedTemplateArgument ActOnTemplateTypeArgument(TypeResult ParsedType);

  void NoteAllFoundTemplates(TemplateName Name);

  QualType CheckTemplateIdType(TemplateName Template,
                               SourceLocation TemplateLoc,
                               TemplateArgumentListInfo &TemplateArgs);

  TypeResult
  ActOnTemplateIdType(Scope *S, CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
                      TemplateTy Template, const IdentifierInfo *TemplateII,
                      SourceLocation TemplateIILoc, SourceLocation LAngleLoc,
                      ASTTemplateArgsPtr TemplateArgs, SourceLocation RAngleLoc,
                      bool IsCtorOrDtorName = false, bool IsClassName = false,
                      ImplicitTypenameContext AllowImplicitTypename =
                          ImplicitTypenameContext::No);

  /// Parsed an elaborated-type-specifier that refers to a template-id,
  /// such as \c class T::template apply<U>.
  TypeResult ActOnTagTemplateIdType(
      TagUseKind TUK, TypeSpecifierType TagSpec, SourceLocation TagLoc,
      CXXScopeSpec &SS, SourceLocation TemplateKWLoc, TemplateTy TemplateD,
      SourceLocation TemplateLoc, SourceLocation LAngleLoc,
      ASTTemplateArgsPtr TemplateArgsIn, SourceLocation RAngleLoc);

  DeclResult ActOnVarTemplateSpecialization(
      Scope *S, Declarator &D, TypeSourceInfo *DI, LookupResult &Previous,
      SourceLocation TemplateKWLoc, TemplateParameterList *TemplateParams,
      StorageClass SC, bool IsPartialSpecialization);

  /// Get the specialization of the given variable template corresponding to
  /// the specified argument list, or a null-but-valid result if the arguments
  /// are dependent.
  DeclResult CheckVarTemplateId(VarTemplateDecl *Template,
                                SourceLocation TemplateLoc,
                                SourceLocation TemplateNameLoc,
                                const TemplateArgumentListInfo &TemplateArgs);

  /// Form a reference to the specialization of the given variable template
  /// corresponding to the specified argument list, or a null-but-valid result
  /// if the arguments are dependent.
  ExprResult CheckVarTemplateId(const CXXScopeSpec &SS,
                                const DeclarationNameInfo &NameInfo,
                                VarTemplateDecl *Template, NamedDecl *FoundD,
                                SourceLocation TemplateLoc,
                                const TemplateArgumentListInfo *TemplateArgs);

  ExprResult
  CheckConceptTemplateId(const CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
                         const DeclarationNameInfo &ConceptNameInfo,
                         NamedDecl *FoundDecl, ConceptDecl *NamedConcept,
                         const TemplateArgumentListInfo *TemplateArgs);

  void diagnoseMissingTemplateArguments(TemplateName Name, SourceLocation Loc);
  void diagnoseMissingTemplateArguments(const CXXScopeSpec &SS,
                                        bool TemplateKeyword, TemplateDecl *TD,
                                        SourceLocation Loc);

  ExprResult BuildTemplateIdExpr(const CXXScopeSpec &SS,
                                 SourceLocation TemplateKWLoc, LookupResult &R,
                                 bool RequiresADL,
                                 const TemplateArgumentListInfo *TemplateArgs);

  // We actually only call this from template instantiation.
  ExprResult
  BuildQualifiedTemplateIdExpr(CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
                               const DeclarationNameInfo &NameInfo,
                               const TemplateArgumentListInfo *TemplateArgs,
                               bool IsAddressOfOperand);

  /// Form a template name from a name that is syntactically required to name a
  /// template, either due to use of the 'template' keyword or because a name in
  /// this syntactic context is assumed to name a template (C++
  /// [temp.names]p2-4).
  ///
  /// This action forms a template name given the name of the template and its
  /// optional scope specifier. This is used when the 'template' keyword is used
  /// or when the parsing context unambiguously treats a following '<' as
  /// introducing a template argument list. Note that this may produce a
  /// non-dependent template name if we can perform the lookup now and identify
  /// the named template.
  ///
  /// For example, given "x.MetaFun::template apply", the scope specifier
  /// \p SS will be "MetaFun::", \p TemplateKWLoc contains the location
  /// of the "template" keyword, and "apply" is the \p Name.
  TemplateNameKind ActOnTemplateName(Scope *S, CXXScopeSpec &SS,
                                     SourceLocation TemplateKWLoc,
                                     const UnqualifiedId &Name,
                                     ParsedType ObjectType,
                                     bool EnteringContext, TemplateTy &Template,
                                     bool AllowInjectedClassName = false);

  DeclResult ActOnClassTemplateSpecialization(
      Scope *S, unsigned TagSpec, TagUseKind TUK, SourceLocation KWLoc,
      SourceLocation ModulePrivateLoc, CXXScopeSpec &SS,
      TemplateIdAnnotation &TemplateId, const ParsedAttributesView &Attr,
      MultiTemplateParamsArg TemplateParameterLists,
      SkipBodyInfo *SkipBody = nullptr);

  /// Check the non-type template arguments of a class template
  /// partial specialization according to C++ [temp.class.spec]p9.
  ///
  /// \param TemplateNameLoc the location of the template name.
  /// \param PrimaryTemplate the template parameters of the primary class
  ///        template.
  /// \param NumExplicit the number of explicitly-specified template arguments.
  /// \param TemplateArgs the template arguments of the class template
  ///        partial specialization.
  ///
  /// \returns \c true if there was an error, \c false otherwise.
  bool CheckTemplatePartialSpecializationArgs(SourceLocation Loc,
                                              TemplateDecl *PrimaryTemplate,
                                              unsigned NumExplicitArgs,
                                              ArrayRef<TemplateArgument> Args);
  void CheckTemplatePartialSpecialization(
      ClassTemplatePartialSpecializationDecl *Partial);
  void CheckTemplatePartialSpecialization(
      VarTemplatePartialSpecializationDecl *Partial);

  Decl *ActOnTemplateDeclarator(Scope *S,
                                MultiTemplateParamsArg TemplateParameterLists,
                                Declarator &D);

  /// Diagnose cases where we have an explicit template specialization
  /// before/after an explicit template instantiation, producing diagnostics
  /// for those cases where they are required and determining whether the
  /// new specialization/instantiation will have any effect.
  ///
  /// \param NewLoc the location of the new explicit specialization or
  /// instantiation.
  ///
  /// \param NewTSK the kind of the new explicit specialization or
  /// instantiation.
  ///
  /// \param PrevDecl the previous declaration of the entity.
  ///
  /// \param PrevTSK the kind of the old explicit specialization or
  /// instantiatin.
  ///
  /// \param PrevPointOfInstantiation if valid, indicates where the previous
  /// declaration was instantiated (either implicitly or explicitly).
  ///
  /// \param HasNoEffect will be set to true to indicate that the new
  /// specialization or instantiation has no effect and should be ignored.
  ///
  /// \returns true if there was an error that should prevent the introduction
  /// of the new declaration into the AST, false otherwise.
  bool CheckSpecializationInstantiationRedecl(
      SourceLocation NewLoc,
      TemplateSpecializationKind ActOnExplicitInstantiationNewTSK,
      NamedDecl *PrevDecl, TemplateSpecializationKind PrevTSK,
      SourceLocation PrevPtOfInstantiation, bool &SuppressNew);

  /// Perform semantic analysis for the given dependent function
  /// template specialization.
  ///
  /// The only possible way to get a dependent function template specialization
  /// is with a friend declaration, like so:
  ///
  /// \code
  ///   template \<class T> void foo(T);
  ///   template \<class T> class A {
  ///     friend void foo<>(T);
  ///   };
  /// \endcode
  ///
  /// There really isn't any useful analysis we can do here, so we
  /// just store the information.
  bool CheckDependentFunctionTemplateSpecialization(
      FunctionDecl *FD, const TemplateArgumentListInfo *ExplicitTemplateArgs,
      LookupResult &Previous);

  /// Perform semantic analysis for the given function template
  /// specialization.
  ///
  /// This routine performs all of the semantic analysis required for an
  /// explicit function template specialization. On successful completion,
  /// the function declaration \p FD will become a function template
  /// specialization.
  ///
  /// \param FD the function declaration, which will be updated to become a
  /// function template specialization.
  ///
  /// \param ExplicitTemplateArgs the explicitly-provided template arguments,
  /// if any. Note that this may be valid info even when 0 arguments are
  /// explicitly provided as in, e.g., \c void sort<>(char*, char*);
  /// as it anyway contains info on the angle brackets locations.
  ///
  /// \param Previous the set of declarations that may be specialized by
  /// this function specialization.
  ///
  /// \param QualifiedFriend whether this is a lookup for a qualified friend
  /// declaration with no explicit template argument list that might be
  /// befriending a function template specialization.
  bool CheckFunctionTemplateSpecialization(
      FunctionDecl *FD, TemplateArgumentListInfo *ExplicitTemplateArgs,
      LookupResult &Previous, bool QualifiedFriend = false);

  /// Perform semantic analysis for the given non-template member
  /// specialization.
  ///
  /// This routine performs all of the semantic analysis required for an
  /// explicit member function specialization. On successful completion,
  /// the function declaration \p FD will become a member function
  /// specialization.
  ///
  /// \param Member the member declaration, which will be updated to become a
  /// specialization.
  ///
  /// \param Previous the set of declarations, one of which may be specialized
  /// by this function specialization;  the set will be modified to contain the
  /// redeclared member.
  bool CheckMemberSpecialization(NamedDecl *Member, LookupResult &Previous);
  void CompleteMemberSpecialization(NamedDecl *Member, LookupResult &Previous);

  // Explicit instantiation of a class template specialization
  DeclResult ActOnExplicitInstantiation(
      Scope *S, SourceLocation ExternLoc, SourceLocation TemplateLoc,
      unsigned TagSpec, SourceLocation KWLoc, const CXXScopeSpec &SS,
      TemplateTy Template, SourceLocation TemplateNameLoc,
      SourceLocation LAngleLoc, ASTTemplateArgsPtr TemplateArgs,
      SourceLocation RAngleLoc, const ParsedAttributesView &Attr);

  // Explicit instantiation of a member class of a class template.
  DeclResult ActOnExplicitInstantiation(Scope *S, SourceLocation ExternLoc,
                                        SourceLocation TemplateLoc,
                                        unsigned TagSpec, SourceLocation KWLoc,
                                        CXXScopeSpec &SS, IdentifierInfo *Name,
                                        SourceLocation NameLoc,
                                        const ParsedAttributesView &Attr);

  DeclResult ActOnExplicitInstantiation(Scope *S, SourceLocation ExternLoc,
                                        SourceLocation TemplateLoc,
                                        Declarator &D);

  /// If the given template parameter has a default template
  /// argument, substitute into that default template argument and
  /// return the corresponding template argument.
  TemplateArgumentLoc SubstDefaultTemplateArgumentIfAvailable(
      TemplateDecl *Template, SourceLocation TemplateLoc,
      SourceLocation RAngleLoc, Decl *Param,
      ArrayRef<TemplateArgument> SugaredConverted,
      ArrayRef<TemplateArgument> CanonicalConverted, bool &HasDefaultArg);

  /// Returns the top most location responsible for the definition of \p N.
  /// If \p N is a a template specialization, this is the location
  /// of the top of the instantiation stack.
  /// Otherwise, the location of \p N is returned.
  SourceLocation getTopMostPointOfInstantiation(const NamedDecl *) const;

  /// Specifies the context in which a particular template
  /// argument is being checked.
  enum CheckTemplateArgumentKind {
    /// The template argument was specified in the code or was
    /// instantiated with some deduced template arguments.
    CTAK_Specified,

    /// The template argument was deduced via template argument
    /// deduction.
    CTAK_Deduced,

    /// The template argument was deduced from an array bound
    /// via template argument deduction.
    CTAK_DeducedFromArrayBound
  };

  /// Check that the given template argument corresponds to the given
  /// template parameter.
  ///
  /// \param Param The template parameter against which the argument will be
  /// checked.
  ///
  /// \param Arg The template argument, which may be updated due to conversions.
  ///
  /// \param Template The template in which the template argument resides.
  ///
  /// \param TemplateLoc The location of the template name for the template
  /// whose argument list we're matching.
  ///
  /// \param RAngleLoc The location of the right angle bracket ('>') that closes
  /// the template argument list.
  ///
  /// \param ArgumentPackIndex The index into the argument pack where this
  /// argument will be placed. Only valid if the parameter is a parameter pack.
  ///
  /// \param Converted The checked, converted argument will be added to the
  /// end of this small vector.
  ///
  /// \param CTAK Describes how we arrived at this particular template argument:
  /// explicitly written, deduced, etc.
  ///
  /// \returns true on error, false otherwise.
  bool
  CheckTemplateArgument(NamedDecl *Param, TemplateArgumentLoc &Arg,
                        NamedDecl *Template, SourceLocation TemplateLoc,
                        SourceLocation RAngleLoc, unsigned ArgumentPackIndex,
                        SmallVectorImpl<TemplateArgument> &SugaredConverted,
                        SmallVectorImpl<TemplateArgument> &CanonicalConverted,
                        CheckTemplateArgumentKind CTAK);

  /// Check that the given template arguments can be provided to
  /// the given template, converting the arguments along the way.
  ///
  /// \param Template The template to which the template arguments are being
  /// provided.
  ///
  /// \param TemplateLoc The location of the template name in the source.
  ///
  /// \param TemplateArgs The list of template arguments. If the template is
  /// a template template parameter, this function may extend the set of
  /// template arguments to also include substituted, defaulted template
  /// arguments.
  ///
  /// \param PartialTemplateArgs True if the list of template arguments is
  /// intentionally partial, e.g., because we're checking just the initial
  /// set of template arguments.
  ///
  /// \param Converted Will receive the converted, canonicalized template
  /// arguments.
  ///
  /// \param UpdateArgsWithConversions If \c true, update \p TemplateArgs to
  /// contain the converted forms of the template arguments as written.
  /// Otherwise, \p TemplateArgs will not be modified.
  ///
  /// \param ConstraintsNotSatisfied If provided, and an error occurred, will
  /// receive true if the cause for the error is the associated constraints of
  /// the template not being satisfied by the template arguments.
  ///
  /// \param PartialOrderingTTP If true, assume these template arguments are
  /// the injected template arguments for a template template parameter.
  /// This will relax the requirement that all its possible uses are valid:
  /// TTP checking is loose, and assumes that invalid uses will be diagnosed
  /// during instantiation.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool CheckTemplateArgumentList(
      TemplateDecl *Template, SourceLocation TemplateLoc,
      TemplateArgumentListInfo &TemplateArgs, bool PartialTemplateArgs,
      SmallVectorImpl<TemplateArgument> &SugaredConverted,
      SmallVectorImpl<TemplateArgument> &CanonicalConverted,
      bool UpdateArgsWithConversions = true,
      bool *ConstraintsNotSatisfied = nullptr, bool PartialOrderingTTP = false);

  bool CheckTemplateTypeArgument(
      TemplateTypeParmDecl *Param, TemplateArgumentLoc &Arg,
      SmallVectorImpl<TemplateArgument> &SugaredConverted,
      SmallVectorImpl<TemplateArgument> &CanonicalConverted);

  /// Check a template argument against its corresponding
  /// template type parameter.
  ///
  /// This routine implements the semantics of C++ [temp.arg.type]. It
  /// returns true if an error occurred, and false otherwise.
  bool CheckTemplateArgument(TypeSourceInfo *Arg);

  /// Check a template argument against its corresponding
  /// non-type template parameter.
  ///
  /// This routine implements the semantics of C++ [temp.arg.nontype].
  /// If an error occurred, it returns ExprError(); otherwise, it
  /// returns the converted template argument. \p ParamType is the
  /// type of the non-type template parameter after it has been instantiated.
  ExprResult CheckTemplateArgument(NonTypeTemplateParmDecl *Param,
                                   QualType InstantiatedParamType, Expr *Arg,
                                   TemplateArgument &SugaredConverted,
                                   TemplateArgument &CanonicalConverted,
                                   CheckTemplateArgumentKind CTAK);

  /// Check a template argument against its corresponding
  /// template template parameter.
  ///
  /// This routine implements the semantics of C++ [temp.arg.template].
  /// It returns true if an error occurred, and false otherwise.
  bool CheckTemplateTemplateArgument(TemplateTemplateParmDecl *Param,
                                     TemplateParameterList *Params,
                                     TemplateArgumentLoc &Arg, bool IsDeduced);

  void NoteTemplateLocation(const NamedDecl &Decl,
                            std::optional<SourceRange> ParamRange = {});
  void NoteTemplateParameterLocation(const NamedDecl &Decl);

  /// Given a non-type template argument that refers to a
  /// declaration and the type of its corresponding non-type template
  /// parameter, produce an expression that properly refers to that
  /// declaration.
  ExprResult BuildExpressionFromDeclTemplateArgument(
      const TemplateArgument &Arg, QualType ParamType, SourceLocation Loc,
      NamedDecl *TemplateParam = nullptr);
  ExprResult
  BuildExpressionFromNonTypeTemplateArgument(const TemplateArgument &Arg,
                                             SourceLocation Loc);

  /// Enumeration describing how template parameter lists are compared
  /// for equality.
  enum TemplateParameterListEqualKind {
    /// We are matching the template parameter lists of two templates
    /// that might be redeclarations.
    ///
    /// \code
    /// template<typename T> struct X;
    /// template<typename T> struct X;
    /// \endcode
    TPL_TemplateMatch,

    /// We are matching the template parameter lists of two template
    /// template parameters as part of matching the template parameter lists
    /// of two templates that might be redeclarations.
    ///
    /// \code
    /// template<template<int I> class TT> struct X;
    /// template<template<int Value> class Other> struct X;
    /// \endcode
    TPL_TemplateTemplateParmMatch,

    /// We are matching the template parameter lists of a template
    /// template argument against the template parameter lists of a template
    /// template parameter.
    ///
    /// \code
    /// template<template<int Value> class Metafun> struct X;
    /// template<int Value> struct integer_c;
    /// X<integer_c> xic;
    /// \endcode
    TPL_TemplateTemplateArgumentMatch,

    /// We are determining whether the template-parameters are equivalent
    /// according to C++ [temp.over.link]/6. This comparison does not consider
    /// constraints.
    ///
    /// \code
    /// template<C1 T> void f(T);
    /// template<C2 T> void f(T);
    /// \endcode
    TPL_TemplateParamsEquivalent,
  };

  // A struct to represent the 'new' declaration, which is either itself just
  // the named decl, or the important information we need about it in order to
  // do constraint comparisons.
  class TemplateCompareNewDeclInfo {
    const NamedDecl *ND = nullptr;
    const DeclContext *DC = nullptr;
    const DeclContext *LexicalDC = nullptr;
    SourceLocation Loc;

  public:
    TemplateCompareNewDeclInfo(const NamedDecl *ND) : ND(ND) {}
    TemplateCompareNewDeclInfo(const DeclContext *DeclCtx,
                               const DeclContext *LexicalDeclCtx,
                               SourceLocation Loc)

        : DC(DeclCtx), LexicalDC(LexicalDeclCtx), Loc(Loc) {
      assert(DC && LexicalDC &&
             "Constructor only for cases where we have the information to put "
             "in here");
    }

    // If this was constructed with no information, we cannot do substitution
    // for constraint comparison, so make sure we can check that.
    bool isInvalid() const { return !ND && !DC; }

    const NamedDecl *getDecl() const { return ND; }

    bool ContainsDecl(const NamedDecl *ND) const { return this->ND == ND; }

    const DeclContext *getLexicalDeclContext() const {
      return ND ? ND->getLexicalDeclContext() : LexicalDC;
    }

    const DeclContext *getDeclContext() const {
      return ND ? ND->getDeclContext() : DC;
    }

    SourceLocation getLocation() const { return ND ? ND->getLocation() : Loc; }
  };

  /// Determine whether the given template parameter lists are
  /// equivalent.
  ///
  /// \param New  The new template parameter list, typically written in the
  /// source code as part of a new template declaration.
  ///
  /// \param Old  The old template parameter list, typically found via
  /// name lookup of the template declared with this template parameter
  /// list.
  ///
  /// \param Complain  If true, this routine will produce a diagnostic if
  /// the template parameter lists are not equivalent.
  ///
  /// \param Kind describes how we are to match the template parameter lists.
  ///
  /// \param TemplateArgLoc If this source location is valid, then we
  /// are actually checking the template parameter list of a template
  /// argument (New) against the template parameter list of its
  /// corresponding template template parameter (Old). We produce
  /// slightly different diagnostics in this scenario.
  ///
  /// \returns True if the template parameter lists are equal, false
  /// otherwise.
  bool TemplateParameterListsAreEqual(
      const TemplateCompareNewDeclInfo &NewInstFrom, TemplateParameterList *New,
      const NamedDecl *OldInstFrom, TemplateParameterList *Old, bool Complain,
      TemplateParameterListEqualKind Kind,
      SourceLocation TemplateArgLoc = SourceLocation());

  bool TemplateParameterListsAreEqual(
      TemplateParameterList *New, TemplateParameterList *Old, bool Complain,
      TemplateParameterListEqualKind Kind,
      SourceLocation TemplateArgLoc = SourceLocation()) {
    return TemplateParameterListsAreEqual(nullptr, New, nullptr, Old, Complain,
                                          Kind, TemplateArgLoc);
  }

  /// Check whether a template can be declared within this scope.
  ///
  /// If the template declaration is valid in this scope, returns
  /// false. Otherwise, issues a diagnostic and returns true.
  bool CheckTemplateDeclScope(Scope *S, TemplateParameterList *TemplateParams);

  /// Called when the parser has parsed a C++ typename
  /// specifier, e.g., "typename T::type".
  ///
  /// \param S The scope in which this typename type occurs.
  /// \param TypenameLoc the location of the 'typename' keyword
  /// \param SS the nested-name-specifier following the typename (e.g., 'T::').
  /// \param II the identifier we're retrieving (e.g., 'type' in the example).
  /// \param IdLoc the location of the identifier.
  /// \param IsImplicitTypename context where T::type refers to a type.
  TypeResult ActOnTypenameType(
      Scope *S, SourceLocation TypenameLoc, const CXXScopeSpec &SS,
      const IdentifierInfo &II, SourceLocation IdLoc,
      ImplicitTypenameContext IsImplicitTypename = ImplicitTypenameContext::No);

  /// Called when the parser has parsed a C++ typename
  /// specifier that ends in a template-id, e.g.,
  /// "typename MetaFun::template apply<T1, T2>".
  ///
  /// \param S The scope in which this typename type occurs.
  /// \param TypenameLoc the location of the 'typename' keyword
  /// \param SS the nested-name-specifier following the typename (e.g., 'T::').
  /// \param TemplateLoc the location of the 'template' keyword, if any.
  /// \param TemplateName The template name.
  /// \param TemplateII The identifier used to name the template.
  /// \param TemplateIILoc The location of the template name.
  /// \param LAngleLoc The location of the opening angle bracket  ('<').
  /// \param TemplateArgs The template arguments.
  /// \param RAngleLoc The location of the closing angle bracket  ('>').
  TypeResult
  ActOnTypenameType(Scope *S, SourceLocation TypenameLoc,
                    const CXXScopeSpec &SS, SourceLocation TemplateLoc,
                    TemplateTy TemplateName, const IdentifierInfo *TemplateII,
                    SourceLocation TemplateIILoc, SourceLocation LAngleLoc,
                    ASTTemplateArgsPtr TemplateArgs, SourceLocation RAngleLoc);

  QualType CheckTypenameType(ElaboratedTypeKeyword Keyword,
                             SourceLocation KeywordLoc,
                             NestedNameSpecifierLoc QualifierLoc,
                             const IdentifierInfo &II, SourceLocation IILoc,
                             TypeSourceInfo **TSI, bool DeducedTSTContext);

  QualType CheckTypenameType(ElaboratedTypeKeyword Keyword,
                             SourceLocation KeywordLoc,
                             NestedNameSpecifierLoc QualifierLoc,
                             const IdentifierInfo &II, SourceLocation IILoc,
                             bool DeducedTSTContext = true);

  /// Rebuilds a type within the context of the current instantiation.
  ///
  /// The type \p T is part of the type of an out-of-line member definition of
  /// a class template (or class template partial specialization) that was
  /// parsed and constructed before we entered the scope of the class template
  /// (or partial specialization thereof). This routine will rebuild that type
  /// now that we have entered the declarator's scope, which may produce
  /// different canonical types, e.g.,
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   typedef T* pointer;
  ///   pointer data();
  /// };
  ///
  /// template<typename T>
  /// typename X<T>::pointer X<T>::data() { ... }
  /// \endcode
  ///
  /// Here, the type "typename X<T>::pointer" will be created as a
  /// DependentNameType, since we do not know that we can look into X<T> when we
  /// parsed the type. This function will rebuild the type, performing the
  /// lookup of "pointer" in X<T> and returning an ElaboratedType whose
  /// canonical type is the same as the canonical type of T*, allowing the
  /// return types of the out-of-line definition and the declaration to match.
  TypeSourceInfo *RebuildTypeInCurrentInstantiation(TypeSourceInfo *T,
                                                    SourceLocation Loc,
                                                    DeclarationName Name);
  bool RebuildNestedNameSpecifierInCurrentInstantiation(CXXScopeSpec &SS);

  ExprResult RebuildExprInCurrentInstantiation(Expr *E);

  /// Rebuild the template parameters now that we know we're in a current
  /// instantiation.
  bool
  RebuildTemplateParamsInCurrentInstantiation(TemplateParameterList *Params);

  /// Produces a formatted string that describes the binding of
  /// template parameters to template arguments.
  std::string
  getTemplateArgumentBindingsText(const TemplateParameterList *Params,
                                  const TemplateArgumentList &Args);

  std::string
  getTemplateArgumentBindingsText(const TemplateParameterList *Params,
                                  const TemplateArgument *Args,
                                  unsigned NumArgs);

  void diagnoseExprIntendedAsTemplateName(Scope *S, ExprResult TemplateName,
                                          SourceLocation Less,
                                          SourceLocation Greater);

  /// ActOnDependentIdExpression - Handle a dependent id-expression that
  /// was just parsed.  This is only possible with an explicit scope
  /// specifier naming a dependent type.
  ExprResult ActOnDependentIdExpression(
      const CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
      const DeclarationNameInfo &NameInfo, bool isAddressOfOperand,
      const TemplateArgumentListInfo *TemplateArgs);

  ExprResult
  BuildDependentDeclRefExpr(const CXXScopeSpec &SS,
                            SourceLocation TemplateKWLoc,
                            const DeclarationNameInfo &NameInfo,
                            const TemplateArgumentListInfo *TemplateArgs);

  // Calculates whether the expression Constraint depends on an enclosing
  // template, for the purposes of [temp.friend] p9.
  // TemplateDepth is the 'depth' of the friend function, which is used to
  // compare whether a declaration reference is referring to a containing
  // template, or just the current friend function. A 'lower' TemplateDepth in
  // the AST refers to a 'containing' template. As the constraint is
  // uninstantiated, this is relative to the 'top' of the TU.
  bool
  ConstraintExpressionDependsOnEnclosingTemplate(const FunctionDecl *Friend,
                                                 unsigned TemplateDepth,
                                                 const Expr *Constraint);

  /// Find the failed Boolean condition within a given Boolean
  /// constant expression, and describe it with a string.
  std::pair<Expr *, std::string> findFailedBooleanCondition(Expr *Cond);

  void CheckDeductionGuideTemplate(FunctionTemplateDecl *TD);

  Decl *ActOnConceptDefinition(Scope *S,
                               MultiTemplateParamsArg TemplateParameterLists,
                               const IdentifierInfo *Name,
                               SourceLocation NameLoc, Expr *ConstraintExpr,
                               const ParsedAttributesView &Attrs);

  void CheckConceptRedefinition(ConceptDecl *NewDecl, LookupResult &Previous,
                                bool &AddToScope);

  TypeResult ActOnDependentTag(Scope *S, unsigned TagSpec, TagUseKind TUK,
                               const CXXScopeSpec &SS,
                               const IdentifierInfo *Name,
                               SourceLocation TagLoc, SourceLocation NameLoc);

  void MarkAsLateParsedTemplate(FunctionDecl *FD, Decl *FnD,
                                CachedTokens &Toks);
  void UnmarkAsLateParsedTemplate(FunctionDecl *FD);
  bool IsInsideALocalClassWithinATemplateFunction();

  /// We've found a use of a templated declaration that would trigger an
  /// implicit instantiation. Check that any relevant explicit specializations
  /// and partial specializations are visible/reachable, and diagnose if not.
  void checkSpecializationVisibility(SourceLocation Loc, NamedDecl *Spec);
  void checkSpecializationReachability(SourceLocation Loc, NamedDecl *Spec);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Template Argument Deduction
  /// Implementations are in SemaTemplateDeduction.cpp
  ///@{

public:
  /// When true, access checking violations are treated as SFINAE
  /// failures rather than hard errors.
  bool AccessCheckingSFINAE;

  /// RAII class used to determine whether SFINAE has
  /// trapped any errors that occur during template argument
  /// deduction.
  class SFINAETrap {
    Sema &SemaRef;
    unsigned PrevSFINAEErrors;
    bool PrevInNonInstantiationSFINAEContext;
    bool PrevAccessCheckingSFINAE;
    bool PrevLastDiagnosticIgnored;

  public:
    explicit SFINAETrap(Sema &SemaRef, bool AccessCheckingSFINAE = false)
        : SemaRef(SemaRef), PrevSFINAEErrors(SemaRef.NumSFINAEErrors),
          PrevInNonInstantiationSFINAEContext(
              SemaRef.InNonInstantiationSFINAEContext),
          PrevAccessCheckingSFINAE(SemaRef.AccessCheckingSFINAE),
          PrevLastDiagnosticIgnored(
              SemaRef.getDiagnostics().isLastDiagnosticIgnored()) {
      if (!SemaRef.isSFINAEContext())
        SemaRef.InNonInstantiationSFINAEContext = true;
      SemaRef.AccessCheckingSFINAE = AccessCheckingSFINAE;
    }

    ~SFINAETrap() {
      SemaRef.NumSFINAEErrors = PrevSFINAEErrors;
      SemaRef.InNonInstantiationSFINAEContext =
          PrevInNonInstantiationSFINAEContext;
      SemaRef.AccessCheckingSFINAE = PrevAccessCheckingSFINAE;
      SemaRef.getDiagnostics().setLastDiagnosticIgnored(
          PrevLastDiagnosticIgnored);
    }

    /// Determine whether any SFINAE errors have been trapped.
    bool hasErrorOccurred() const {
      return SemaRef.NumSFINAEErrors > PrevSFINAEErrors;
    }
  };

  /// RAII class used to indicate that we are performing provisional
  /// semantic analysis to determine the validity of a construct, so
  /// typo-correction and diagnostics in the immediate context (not within
  /// implicitly-instantiated templates) should be suppressed.
  class TentativeAnalysisScope {
    Sema &SemaRef;
    // FIXME: Using a SFINAETrap for this is a hack.
    SFINAETrap Trap;
    bool PrevDisableTypoCorrection;

  public:
    explicit TentativeAnalysisScope(Sema &SemaRef)
        : SemaRef(SemaRef), Trap(SemaRef, true),
          PrevDisableTypoCorrection(SemaRef.DisableTypoCorrection) {
      SemaRef.DisableTypoCorrection = true;
    }
    ~TentativeAnalysisScope() {
      SemaRef.DisableTypoCorrection = PrevDisableTypoCorrection;
    }
  };

  /// For each declaration that involved template argument deduction, the
  /// set of diagnostics that were suppressed during that template argument
  /// deduction.
  ///
  /// FIXME: Serialize this structure to the AST file.
  typedef llvm::DenseMap<Decl *, SmallVector<PartialDiagnosticAt, 1>>
      SuppressedDiagnosticsMap;
  SuppressedDiagnosticsMap SuppressedDiagnostics;

  /// Compare types for equality with respect to possibly compatible
  /// function types (noreturn adjustment, implicit calling conventions). If any
  /// of parameter and argument is not a function, just perform type comparison.
  ///
  /// \param P the template parameter type.
  ///
  /// \param A the argument type.
  bool isSameOrCompatibleFunctionType(QualType Param, QualType Arg);

  /// Allocate a TemplateArgumentLoc where all locations have
  /// been initialized to the given location.
  ///
  /// \param Arg The template argument we are producing template argument
  /// location information for.
  ///
  /// \param NTTPType For a declaration template argument, the type of
  /// the non-type template parameter that corresponds to this template
  /// argument. Can be null if no type sugar is available to add to the
  /// type from the template argument.
  ///
  /// \param Loc The source location to use for the resulting template
  /// argument.
  TemplateArgumentLoc
  getTrivialTemplateArgumentLoc(const TemplateArgument &Arg, QualType NTTPType,
                                SourceLocation Loc,
                                NamedDecl *TemplateParam = nullptr);

  /// Get a template argument mapping the given template parameter to itself,
  /// e.g. for X in \c template<int X>, this would return an expression template
  /// argument referencing X.
  TemplateArgumentLoc getIdentityTemplateArgumentLoc(NamedDecl *Param,
                                                     SourceLocation Location);

  /// Adjust the type \p ArgFunctionType to match the calling convention,
  /// noreturn, and optionally the exception specification of \p FunctionType.
  /// Deduction often wants to ignore these properties when matching function
  /// types.
  QualType adjustCCAndNoReturn(QualType ArgFunctionType, QualType FunctionType,
                               bool AdjustExceptionSpec = false);

  TemplateDeductionResult
  DeduceTemplateArguments(ClassTemplatePartialSpecializationDecl *Partial,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          sema::TemplateDeductionInfo &Info);

  TemplateDeductionResult
  DeduceTemplateArguments(VarTemplatePartialSpecializationDecl *Partial,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          sema::TemplateDeductionInfo &Info);

  /// Deduce the template arguments of the given template from \p FromType.
  /// Used to implement the IsDeducible constraint for alias CTAD per C++
  /// [over.match.class.deduct]p4.
  ///
  /// It only supports class or type alias templates.
  TemplateDeductionResult
  DeduceTemplateArgumentsFromType(TemplateDecl *TD, QualType FromType,
                                  sema::TemplateDeductionInfo &Info);

  TemplateDeductionResult DeduceTemplateArguments(
      TemplateParameterList *TemplateParams, ArrayRef<TemplateArgument> Ps,
      ArrayRef<TemplateArgument> As, sema::TemplateDeductionInfo &Info,
      SmallVectorImpl<DeducedTemplateArgument> &Deduced,
      bool NumberOfArgumentsMustMatch);

  /// Substitute the explicitly-provided template arguments into the
  /// given function template according to C++ [temp.arg.explicit].
  ///
  /// \param FunctionTemplate the function template into which the explicit
  /// template arguments will be substituted.
  ///
  /// \param ExplicitTemplateArgs the explicitly-specified template
  /// arguments.
  ///
  /// \param Deduced the deduced template arguments, which will be populated
  /// with the converted and checked explicit template arguments.
  ///
  /// \param ParamTypes will be populated with the instantiated function
  /// parameters.
  ///
  /// \param FunctionType if non-NULL, the result type of the function template
  /// will also be instantiated and the pointed-to value will be updated with
  /// the instantiated function type.
  ///
  /// \param Info if substitution fails for any reason, this object will be
  /// populated with more information about the failure.
  ///
  /// \returns TemplateDeductionResult::Success if substitution was successful,
  /// or some failure condition.
  TemplateDeductionResult SubstituteExplicitTemplateArguments(
      FunctionTemplateDecl *FunctionTemplate,
      TemplateArgumentListInfo &ExplicitTemplateArgs,
      SmallVectorImpl<DeducedTemplateArgument> &Deduced,
      SmallVectorImpl<QualType> &ParamTypes, QualType *FunctionType,
      sema::TemplateDeductionInfo &Info);

  /// brief A function argument from which we performed template argument
  // deduction for a call.
  struct OriginalCallArg {
    OriginalCallArg(QualType OriginalParamType, bool DecomposedParam,
                    unsigned ArgIdx, QualType OriginalArgType)
        : OriginalParamType(OriginalParamType),
          DecomposedParam(DecomposedParam), ArgIdx(ArgIdx),
          OriginalArgType(OriginalArgType) {}

    QualType OriginalParamType;
    bool DecomposedParam;
    unsigned ArgIdx;
    QualType OriginalArgType;
  };

  /// Finish template argument deduction for a function template,
  /// checking the deduced template arguments for completeness and forming
  /// the function template specialization.
  ///
  /// \param OriginalCallArgs If non-NULL, the original call arguments against
  /// which the deduced argument types should be compared.
  TemplateDeductionResult FinishTemplateArgumentDeduction(
      FunctionTemplateDecl *FunctionTemplate,
      SmallVectorImpl<DeducedTemplateArgument> &Deduced,
      unsigned NumExplicitlySpecified, FunctionDecl *&Specialization,
      sema::TemplateDeductionInfo &Info,
      SmallVectorImpl<OriginalCallArg> const *OriginalCallArgs = nullptr,
      bool PartialOverloading = false,
      llvm::function_ref<bool()> CheckNonDependent = [] { return false; });

  /// Perform template argument deduction from a function call
  /// (C++ [temp.deduct.call]).
  ///
  /// \param FunctionTemplate the function template for which we are performing
  /// template argument deduction.
  ///
  /// \param ExplicitTemplateArgs the explicit template arguments provided
  /// for this call.
  ///
  /// \param Args the function call arguments
  ///
  /// \param Specialization if template argument deduction was successful,
  /// this will be set to the function template specialization produced by
  /// template argument deduction.
  ///
  /// \param Info the argument will be updated to provide additional information
  /// about template argument deduction.
  ///
  /// \param CheckNonDependent A callback to invoke to check conversions for
  /// non-dependent parameters, between deduction and substitution, per DR1391.
  /// If this returns true, substitution will be skipped and we return
  /// TemplateDeductionResult::NonDependentConversionFailure. The callback is
  /// passed the parameter types (after substituting explicit template
  /// arguments).
  ///
  /// \returns the result of template argument deduction.
  TemplateDeductionResult DeduceTemplateArguments(
      FunctionTemplateDecl *FunctionTemplate,
      TemplateArgumentListInfo *ExplicitTemplateArgs, ArrayRef<Expr *> Args,
      FunctionDecl *&Specialization, sema::TemplateDeductionInfo &Info,
      bool PartialOverloading, bool AggregateDeductionCandidate,
      QualType ObjectType, Expr::Classification ObjectClassification,
      llvm::function_ref<bool(ArrayRef<QualType>)> CheckNonDependent);

  /// Deduce template arguments when taking the address of a function
  /// template (C++ [temp.deduct.funcaddr]) or matching a specialization to
  /// a template.
  ///
  /// \param FunctionTemplate the function template for which we are performing
  /// template argument deduction.
  ///
  /// \param ExplicitTemplateArgs the explicitly-specified template
  /// arguments.
  ///
  /// \param ArgFunctionType the function type that will be used as the
  /// "argument" type (A) when performing template argument deduction from the
  /// function template's function type. This type may be NULL, if there is no
  /// argument type to compare against, in C++0x [temp.arg.explicit]p3.
  ///
  /// \param Specialization if template argument deduction was successful,
  /// this will be set to the function template specialization produced by
  /// template argument deduction.
  ///
  /// \param Info the argument will be updated to provide additional information
  /// about template argument deduction.
  ///
  /// \param IsAddressOfFunction If \c true, we are deducing as part of taking
  /// the address of a function template per [temp.deduct.funcaddr] and
  /// [over.over]. If \c false, we are looking up a function template
  /// specialization based on its signature, per [temp.deduct.decl].
  ///
  /// \returns the result of template argument deduction.
  TemplateDeductionResult DeduceTemplateArguments(
      FunctionTemplateDecl *FunctionTemplate,
      TemplateArgumentListInfo *ExplicitTemplateArgs, QualType ArgFunctionType,
      FunctionDecl *&Specialization, sema::TemplateDeductionInfo &Info,
      bool IsAddressOfFunction = false);

  /// Deduce template arguments for a templated conversion
  /// function (C++ [temp.deduct.conv]) and, if successful, produce a
  /// conversion function template specialization.
  TemplateDeductionResult DeduceTemplateArguments(
      FunctionTemplateDecl *FunctionTemplate, QualType ObjectType,
      Expr::Classification ObjectClassification, QualType ToType,
      CXXConversionDecl *&Specialization, sema::TemplateDeductionInfo &Info);

  /// Deduce template arguments for a function template when there is
  /// nothing to deduce against (C++0x [temp.arg.explicit]p3).
  ///
  /// \param FunctionTemplate the function template for which we are performing
  /// template argument deduction.
  ///
  /// \param ExplicitTemplateArgs the explicitly-specified template
  /// arguments.
  ///
  /// \param Specialization if template argument deduction was successful,
  /// this will be set to the function template specialization produced by
  /// template argument deduction.
  ///
  /// \param Info the argument will be updated to provide additional information
  /// about template argument deduction.
  ///
  /// \param IsAddressOfFunction If \c true, we are deducing as part of taking
  /// the address of a function template in a context where we do not have a
  /// target type, per [over.over]. If \c false, we are looking up a function
  /// template specialization based on its signature, which only happens when
  /// deducing a function parameter type from an argument that is a template-id
  /// naming a function template specialization.
  ///
  /// \returns the result of template argument deduction.
  TemplateDeductionResult
  DeduceTemplateArguments(FunctionTemplateDecl *FunctionTemplate,
                          TemplateArgumentListInfo *ExplicitTemplateArgs,
                          FunctionDecl *&Specialization,
                          sema::TemplateDeductionInfo &Info,
                          bool IsAddressOfFunction = false);

  /// Substitute Replacement for \p auto in \p TypeWithAuto
  QualType SubstAutoType(QualType TypeWithAuto, QualType Replacement);
  /// Substitute Replacement for auto in TypeWithAuto
  TypeSourceInfo *SubstAutoTypeSourceInfo(TypeSourceInfo *TypeWithAuto,
                                          QualType Replacement);

  // Substitute auto in TypeWithAuto for a Dependent auto type
  QualType SubstAutoTypeDependent(QualType TypeWithAuto);

  // Substitute auto in TypeWithAuto for a Dependent auto type
  TypeSourceInfo *
  SubstAutoTypeSourceInfoDependent(TypeSourceInfo *TypeWithAuto);

  /// Completely replace the \c auto in \p TypeWithAuto by
  /// \p Replacement. This does not retain any \c auto type sugar.
  QualType ReplaceAutoType(QualType TypeWithAuto, QualType Replacement);
  TypeSourceInfo *ReplaceAutoTypeSourceInfo(TypeSourceInfo *TypeWithAuto,
                                            QualType Replacement);

  /// Deduce the type for an auto type-specifier (C++11 [dcl.spec.auto]p6)
  ///
  /// Note that this is done even if the initializer is dependent. (This is
  /// necessary to support partial ordering of templates using 'auto'.)
  /// A dependent type will be produced when deducing from a dependent type.
  ///
  /// \param Type the type pattern using the auto type-specifier.
  /// \param Init the initializer for the variable whose type is to be deduced.
  /// \param Result if type deduction was successful, this will be set to the
  ///        deduced type.
  /// \param Info the argument will be updated to provide additional information
  ///        about template argument deduction.
  /// \param DependentDeduction Set if we should permit deduction in
  ///        dependent cases. This is necessary for template partial ordering
  ///        with 'auto' template parameters. The template parameter depth to be
  ///        used should be specified in the 'Info' parameter.
  /// \param IgnoreConstraints Set if we should not fail if the deduced type
  ///                          does not satisfy the type-constraint in the auto
  ///                          type.
  TemplateDeductionResult
  DeduceAutoType(TypeLoc AutoTypeLoc, Expr *Initializer, QualType &Result,
                 sema::TemplateDeductionInfo &Info,
                 bool DependentDeduction = false,
                 bool IgnoreConstraints = false,
                 TemplateSpecCandidateSet *FailedTSC = nullptr);
  void DiagnoseAutoDeductionFailure(const VarDecl *VDecl, const Expr *Init);
  bool DeduceReturnType(FunctionDecl *FD, SourceLocation Loc,
                        bool Diagnose = true);

  bool CheckIfFunctionSpecializationIsImmediate(FunctionDecl *FD,
                                                SourceLocation Loc);

  /// Returns the more specialized class template partial specialization
  /// according to the rules of partial ordering of class template partial
  /// specializations (C++ [temp.class.order]).
  ///
  /// \param PS1 the first class template partial specialization
  ///
  /// \param PS2 the second class template partial specialization
  ///
  /// \returns the more specialized class template partial specialization. If
  /// neither partial specialization is more specialized, returns NULL.
  ClassTemplatePartialSpecializationDecl *
  getMoreSpecializedPartialSpecialization(
      ClassTemplatePartialSpecializationDecl *PS1,
      ClassTemplatePartialSpecializationDecl *PS2, SourceLocation Loc);

  bool isMoreSpecializedThanPrimary(ClassTemplatePartialSpecializationDecl *T,
                                    sema::TemplateDeductionInfo &Info);

  VarTemplatePartialSpecializationDecl *getMoreSpecializedPartialSpecialization(
      VarTemplatePartialSpecializationDecl *PS1,
      VarTemplatePartialSpecializationDecl *PS2, SourceLocation Loc);

  bool isMoreSpecializedThanPrimary(VarTemplatePartialSpecializationDecl *T,
                                    sema::TemplateDeductionInfo &Info);

  bool isTemplateTemplateParameterAtLeastAsSpecializedAs(
      TemplateParameterList *PParam, TemplateDecl *AArg, SourceLocation Loc,
      bool IsDeduced);

  /// Mark which template parameters are used in a given expression.
  ///
  /// \param E the expression from which template parameters will be deduced.
  ///
  /// \param Used a bit vector whose elements will be set to \c true
  /// to indicate when the corresponding template parameter will be
  /// deduced.
  void MarkUsedTemplateParameters(const Expr *E, bool OnlyDeduced,
                                  unsigned Depth, llvm::SmallBitVector &Used);

  /// Mark which template parameters can be deduced from a given
  /// template argument list.
  ///
  /// \param TemplateArgs the template argument list from which template
  /// parameters will be deduced.
  ///
  /// \param Used a bit vector whose elements will be set to \c true
  /// to indicate when the corresponding template parameter will be
  /// deduced.
  void MarkUsedTemplateParameters(const TemplateArgumentList &TemplateArgs,
                                  bool OnlyDeduced, unsigned Depth,
                                  llvm::SmallBitVector &Used);
  void
  MarkDeducedTemplateParameters(const FunctionTemplateDecl *FunctionTemplate,
                                llvm::SmallBitVector &Deduced) {
    return MarkDeducedTemplateParameters(Context, FunctionTemplate, Deduced);
  }

  /// Marks all of the template parameters that will be deduced by a
  /// call to the given function template.
  static void
  MarkDeducedTemplateParameters(ASTContext &Ctx,
                                const FunctionTemplateDecl *FunctionTemplate,
                                llvm::SmallBitVector &Deduced);

  /// Returns the more specialized function template according
  /// to the rules of function template partial ordering (C++
  /// [temp.func.order]).
  ///
  /// \param FT1 the first function template
  ///
  /// \param FT2 the second function template
  ///
  /// \param TPOC the context in which we are performing partial ordering of
  /// function templates.
  ///
  /// \param NumCallArguments1 The number of arguments in the call to FT1, used
  /// only when \c TPOC is \c TPOC_Call. Does not include the object argument
  /// when calling a member function.
  ///
  /// \param RawObj1Ty The type of the object parameter of FT1 if a member
  /// function only used if \c TPOC is \c TPOC_Call and FT1 is a Function
  /// template from a member function
  ///
  /// \param RawObj2Ty The type of the object parameter of FT2 if a member
  /// function only used if \c TPOC is \c TPOC_Call and FT2 is a Function
  /// template from a member function
  ///
  /// \param Reversed If \c true, exactly one of FT1 and FT2 is an overload
  /// candidate with a reversed parameter order. In this case, the corresponding
  /// P/A pairs between FT1 and FT2 are reversed.
  ///
  /// \returns the more specialized function template. If neither
  /// template is more specialized, returns NULL.
  FunctionTemplateDecl *getMoreSpecializedTemplate(
      FunctionTemplateDecl *FT1, FunctionTemplateDecl *FT2, SourceLocation Loc,
      TemplatePartialOrderingContext TPOC, unsigned NumCallArguments1,
      QualType RawObj1Ty = {}, QualType RawObj2Ty = {}, bool Reversed = false);

  /// Retrieve the most specialized of the given function template
  /// specializations.
  ///
  /// \param SpecBegin the start iterator of the function template
  /// specializations that we will be comparing.
  ///
  /// \param SpecEnd the end iterator of the function template
  /// specializations, paired with \p SpecBegin.
  ///
  /// \param Loc the location where the ambiguity or no-specializations
  /// diagnostic should occur.
  ///
  /// \param NoneDiag partial diagnostic used to diagnose cases where there are
  /// no matching candidates.
  ///
  /// \param AmbigDiag partial diagnostic used to diagnose an ambiguity, if one
  /// occurs.
  ///
  /// \param CandidateDiag partial diagnostic used for each function template
  /// specialization that is a candidate in the ambiguous ordering. One
  /// parameter in this diagnostic should be unbound, which will correspond to
  /// the string describing the template arguments for the function template
  /// specialization.
  ///
  /// \returns the most specialized function template specialization, if
  /// found. Otherwise, returns SpecEnd.
  UnresolvedSetIterator
  getMostSpecialized(UnresolvedSetIterator SBegin, UnresolvedSetIterator SEnd,
                     TemplateSpecCandidateSet &FailedCandidates,
                     SourceLocation Loc, const PartialDiagnostic &NoneDiag,
                     const PartialDiagnostic &AmbigDiag,
                     const PartialDiagnostic &CandidateDiag,
                     bool Complain = true, QualType TargetType = QualType());

  /// Returns the more constrained function according to the rules of
  /// partial ordering by constraints (C++ [temp.constr.order]).
  ///
  /// \param FD1 the first function
  ///
  /// \param FD2 the second function
  ///
  /// \returns the more constrained function. If neither function is
  /// more constrained, returns NULL.
  FunctionDecl *getMoreConstrainedFunction(FunctionDecl *FD1,
                                           FunctionDecl *FD2);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Template Deduction Guide
  /// Implementations are in SemaTemplateDeductionGuide.cpp
  ///@{

  /// Declare implicit deduction guides for a class template if we've
  /// not already done so.
  void DeclareImplicitDeductionGuides(TemplateDecl *Template,
                                      SourceLocation Loc);

  FunctionTemplateDecl *DeclareAggregateDeductionGuideFromInitList(
      TemplateDecl *Template, MutableArrayRef<QualType> ParamTypes,
      SourceLocation Loc);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Template Instantiation
  /// Implementations are in SemaTemplateInstantiate.cpp
  ///@{

public:
  /// A helper class for building up ExtParameterInfos.
  class ExtParameterInfoBuilder {
    SmallVector<FunctionProtoType::ExtParameterInfo, 16> Infos;
    bool HasInteresting = false;

  public:
    /// Set the ExtParameterInfo for the parameter at the given index,
    ///
    void set(unsigned index, FunctionProtoType::ExtParameterInfo info) {
      assert(Infos.size() <= index);
      Infos.resize(index);
      Infos.push_back(info);

      if (!HasInteresting)
        HasInteresting = (info != FunctionProtoType::ExtParameterInfo());
    }

    /// Return a pointer (suitable for setting in an ExtProtoInfo) to the
    /// ExtParameterInfo array we've built up.
    const FunctionProtoType::ExtParameterInfo *
    getPointerOrNull(unsigned numParams) {
      if (!HasInteresting)
        return nullptr;
      Infos.resize(numParams);
      return Infos.data();
    }
  };

  /// The current instantiation scope used to store local
  /// variables.
  LocalInstantiationScope *CurrentInstantiationScope;

  typedef llvm::DenseMap<ParmVarDecl *, llvm::TinyPtrVector<ParmVarDecl *>>
      UnparsedDefaultArgInstantiationsMap;

  /// A mapping from parameters with unparsed default arguments to the
  /// set of instantiations of each parameter.
  ///
  /// This mapping is a temporary data structure used when parsing
  /// nested class templates or nested classes of class templates,
  /// where we might end up instantiating an inner class before the
  /// default arguments of its methods have been parsed.
  UnparsedDefaultArgInstantiationsMap UnparsedDefaultArgInstantiations;

  /// A context in which code is being synthesized (where a source location
  /// alone is not sufficient to identify the context). This covers template
  /// instantiation and various forms of implicitly-generated functions.
  struct CodeSynthesisContext {
    /// The kind of template instantiation we are performing
    enum SynthesisKind {
      /// We are instantiating a template declaration. The entity is
      /// the declaration we're instantiating (e.g., a CXXRecordDecl).
      TemplateInstantiation,

      /// We are instantiating a default argument for a template
      /// parameter. The Entity is the template parameter whose argument is
      /// being instantiated, the Template is the template, and the
      /// TemplateArgs/NumTemplateArguments provide the template arguments as
      /// specified.
      DefaultTemplateArgumentInstantiation,

      /// We are instantiating a default argument for a function.
      /// The Entity is the ParmVarDecl, and TemplateArgs/NumTemplateArgs
      /// provides the template arguments as specified.
      DefaultFunctionArgumentInstantiation,

      /// We are substituting explicit template arguments provided for
      /// a function template. The entity is a FunctionTemplateDecl.
      ExplicitTemplateArgumentSubstitution,

      /// We are substituting template argument determined as part of
      /// template argument deduction for either a class template
      /// partial specialization or a function template. The
      /// Entity is either a {Class|Var}TemplatePartialSpecializationDecl or
      /// a TemplateDecl.
      DeducedTemplateArgumentSubstitution,

      /// We are substituting into a lambda expression.
      LambdaExpressionSubstitution,

      /// We are substituting prior template arguments into a new
      /// template parameter. The template parameter itself is either a
      /// NonTypeTemplateParmDecl or a TemplateTemplateParmDecl.
      PriorTemplateArgumentSubstitution,

      /// We are checking the validity of a default template argument that
      /// has been used when naming a template-id.
      DefaultTemplateArgumentChecking,

      /// We are computing the exception specification for a defaulted special
      /// member function.
      ExceptionSpecEvaluation,

      /// We are instantiating the exception specification for a function
      /// template which was deferred until it was needed.
      ExceptionSpecInstantiation,

      /// We are instantiating a requirement of a requires expression.
      RequirementInstantiation,

      /// We are checking the satisfaction of a nested requirement of a requires
      /// expression.
      NestedRequirementConstraintsCheck,

      /// We are declaring an implicit special member function.
      DeclaringSpecialMember,

      /// We are declaring an implicit 'operator==' for a defaulted
      /// 'operator<=>'.
      DeclaringImplicitEqualityComparison,

      /// We are defining a synthesized function (such as a defaulted special
      /// member).
      DefiningSynthesizedFunction,

      // We are checking the constraints associated with a constrained entity or
      // the constraint expression of a concept. This includes the checks that
      // atomic constraints have the type 'bool' and that they can be constant
      // evaluated.
      ConstraintsCheck,

      // We are substituting template arguments into a constraint expression.
      ConstraintSubstitution,

      // We are normalizing a constraint expression.
      ConstraintNormalization,

      // Instantiating a Requires Expression parameter clause.
      RequirementParameterInstantiation,

      // We are substituting into the parameter mapping of an atomic constraint
      // during normalization.
      ParameterMappingSubstitution,

      /// We are rewriting a comparison operator in terms of an operator<=>.
      RewritingOperatorAsSpaceship,

      /// We are initializing a structured binding.
      InitializingStructuredBinding,

      /// We are marking a class as __dllexport.
      MarkingClassDllexported,

      /// We are building an implied call from __builtin_dump_struct. The
      /// arguments are in CallArgs.
      BuildingBuiltinDumpStructCall,

      /// Added for Template instantiation observation.
      /// Memoization means we are _not_ instantiating a template because
      /// it is already instantiated (but we entered a context where we
      /// would have had to if it was not already instantiated).
      Memoization,

      /// We are building deduction guides for a class.
      BuildingDeductionGuides,

      /// We are instantiating a type alias template declaration.
      TypeAliasTemplateInstantiation,
    } Kind;

    /// Was the enclosing context a non-instantiation SFINAE context?
    bool SavedInNonInstantiationSFINAEContext;

    /// The point of instantiation or synthesis within the source code.
    SourceLocation PointOfInstantiation;

    /// The entity that is being synthesized.
    Decl *Entity;

    /// The template (or partial specialization) in which we are
    /// performing the instantiation, for substitutions of prior template
    /// arguments.
    NamedDecl *Template;

    union {
      /// The list of template arguments we are substituting, if they
      /// are not part of the entity.
      const TemplateArgument *TemplateArgs;

      /// The list of argument expressions in a synthesized call.
      const Expr *const *CallArgs;
    };

    // FIXME: Wrap this union around more members, or perhaps store the
    // kind-specific members in the RAII object owning the context.
    union {
      /// The number of template arguments in TemplateArgs.
      unsigned NumTemplateArgs;

      /// The number of expressions in CallArgs.
      unsigned NumCallArgs;

      /// The special member being declared or defined.
      CXXSpecialMemberKind SpecialMember;
    };

    ArrayRef<TemplateArgument> template_arguments() const {
      assert(Kind != DeclaringSpecialMember);
      return {TemplateArgs, NumTemplateArgs};
    }

    /// The template deduction info object associated with the
    /// substitution or checking of explicit or deduced template arguments.
    sema::TemplateDeductionInfo *DeductionInfo;

    /// The source range that covers the construct that cause
    /// the instantiation, e.g., the template-id that causes a class
    /// template instantiation.
    SourceRange InstantiationRange;

    CodeSynthesisContext()
        : Kind(TemplateInstantiation),
          SavedInNonInstantiationSFINAEContext(false), Entity(nullptr),
          Template(nullptr), TemplateArgs(nullptr), NumTemplateArgs(0),
          DeductionInfo(nullptr) {}

    /// Determines whether this template is an actual instantiation
    /// that should be counted toward the maximum instantiation depth.
    bool isInstantiationRecord() const;
  };

  /// A stack object to be created when performing template
  /// instantiation.
  ///
  /// Construction of an object of type \c InstantiatingTemplate
  /// pushes the current instantiation onto the stack of active
  /// instantiations. If the size of this stack exceeds the maximum
  /// number of recursive template instantiations, construction
  /// produces an error and evaluates true.
  ///
  /// Destruction of this object will pop the named instantiation off
  /// the stack.
  struct InstantiatingTemplate {
    /// Note that we are instantiating a class template,
    /// function template, variable template, alias template,
    /// or a member thereof.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          Decl *Entity,
                          SourceRange InstantiationRange = SourceRange());

    struct ExceptionSpecification {};
    /// Note that we are instantiating an exception specification
    /// of a function template.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          FunctionDecl *Entity, ExceptionSpecification,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are instantiating a type alias template declaration.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          TypeAliasTemplateDecl *Entity,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are instantiating a default argument in a
    /// template-id.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          TemplateParameter Param, TemplateDecl *Template,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are substituting either explicitly-specified or
    /// deduced template arguments during function template argument deduction.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          FunctionTemplateDecl *FunctionTemplate,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          CodeSynthesisContext::SynthesisKind Kind,
                          sema::TemplateDeductionInfo &DeductionInfo,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are instantiating as part of template
    /// argument deduction for a class template declaration.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          TemplateDecl *Template,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          sema::TemplateDeductionInfo &DeductionInfo,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are instantiating as part of template
    /// argument deduction for a class template partial
    /// specialization.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          ClassTemplatePartialSpecializationDecl *PartialSpec,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          sema::TemplateDeductionInfo &DeductionInfo,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are instantiating as part of template
    /// argument deduction for a variable template partial
    /// specialization.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          VarTemplatePartialSpecializationDecl *PartialSpec,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          sema::TemplateDeductionInfo &DeductionInfo,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are instantiating a default argument for a function
    /// parameter.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          ParmVarDecl *Param,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we are substituting prior template arguments into a
    /// non-type parameter.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          NamedDecl *Template, NonTypeTemplateParmDecl *Param,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          SourceRange InstantiationRange);

    /// Note that we are substituting prior template arguments into a
    /// template template parameter.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          NamedDecl *Template, TemplateTemplateParmDecl *Param,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          SourceRange InstantiationRange);

    /// Note that we are checking the default template argument
    /// against the template parameter for a given template-id.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          TemplateDecl *Template, NamedDecl *Param,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          SourceRange InstantiationRange);

    struct ConstraintsCheck {};
    /// \brief Note that we are checking the constraints associated with some
    /// constrained entity (a concept declaration or a template with associated
    /// constraints).
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          ConstraintsCheck, NamedDecl *Template,
                          ArrayRef<TemplateArgument> TemplateArgs,
                          SourceRange InstantiationRange);

    struct ConstraintSubstitution {};
    /// \brief Note that we are checking a constraint expression associated
    /// with a template declaration or as part of the satisfaction check of a
    /// concept.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          ConstraintSubstitution, NamedDecl *Template,
                          sema::TemplateDeductionInfo &DeductionInfo,
                          SourceRange InstantiationRange);

    struct ConstraintNormalization {};
    /// \brief Note that we are normalizing a constraint expression.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          ConstraintNormalization, NamedDecl *Template,
                          SourceRange InstantiationRange);

    struct ParameterMappingSubstitution {};
    /// \brief Note that we are subtituting into the parameter mapping of an
    /// atomic constraint during constraint normalization.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          ParameterMappingSubstitution, NamedDecl *Template,
                          SourceRange InstantiationRange);

    /// \brief Note that we are substituting template arguments into a part of
    /// a requirement of a requires expression.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          concepts::Requirement *Req,
                          sema::TemplateDeductionInfo &DeductionInfo,
                          SourceRange InstantiationRange = SourceRange());

    /// \brief Note that we are checking the satisfaction of the constraint
    /// expression inside of a nested requirement.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          concepts::NestedRequirement *Req, ConstraintsCheck,
                          SourceRange InstantiationRange = SourceRange());

    /// \brief Note that we are checking a requires clause.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          const RequiresExpr *E,
                          sema::TemplateDeductionInfo &DeductionInfo,
                          SourceRange InstantiationRange);

    struct BuildingDeductionGuidesTag {};
    /// \brief Note that we are building deduction guides.
    InstantiatingTemplate(Sema &SemaRef, SourceLocation PointOfInstantiation,
                          TemplateDecl *Entity, BuildingDeductionGuidesTag,
                          SourceRange InstantiationRange = SourceRange());

    /// Note that we have finished instantiating this template.
    void Clear();

    ~InstantiatingTemplate() { Clear(); }

    /// Determines whether we have exceeded the maximum
    /// recursive template instantiations.
    bool isInvalid() const { return Invalid; }

    /// Determine whether we are already instantiating this
    /// specialization in some surrounding active instantiation.
    bool isAlreadyInstantiating() const { return AlreadyInstantiating; }

  private:
    Sema &SemaRef;
    bool Invalid;
    bool AlreadyInstantiating;
    bool CheckInstantiationDepth(SourceLocation PointOfInstantiation,
                                 SourceRange InstantiationRange);

    InstantiatingTemplate(
        Sema &SemaRef, CodeSynthesisContext::SynthesisKind Kind,
        SourceLocation PointOfInstantiation, SourceRange InstantiationRange,
        Decl *Entity, NamedDecl *Template = nullptr,
        ArrayRef<TemplateArgument> TemplateArgs = std::nullopt,
        sema::TemplateDeductionInfo *DeductionInfo = nullptr);

    InstantiatingTemplate(const InstantiatingTemplate &) = delete;

    InstantiatingTemplate &operator=(const InstantiatingTemplate &) = delete;
  };

  bool SubstTemplateArgument(const TemplateArgumentLoc &Input,
                             const MultiLevelTemplateArgumentList &TemplateArgs,
                             TemplateArgumentLoc &Output,
                             SourceLocation Loc = {},
                             const DeclarationName &Entity = {});
  bool
  SubstTemplateArguments(ArrayRef<TemplateArgumentLoc> Args,
                         const MultiLevelTemplateArgumentList &TemplateArgs,
                         TemplateArgumentListInfo &Outputs);

  /// Retrieve the template argument list(s) that should be used to
  /// instantiate the definition of the given declaration.
  ///
  /// \param ND the declaration for which we are computing template
  /// instantiation arguments.
  ///
  /// \param DC In the event we don't HAVE a declaration yet, we instead provide
  ///  the decl context where it will be created.  In this case, the `Innermost`
  ///  should likely be provided.  If ND is non-null, this is ignored.
  ///
  /// \param Innermost if non-NULL, specifies a template argument list for the
  /// template declaration passed as ND.
  ///
  /// \param RelativeToPrimary true if we should get the template
  /// arguments relative to the primary template, even when we're
  /// dealing with a specialization. This is only relevant for function
  /// template specializations.
  ///
  /// \param Pattern If non-NULL, indicates the pattern from which we will be
  /// instantiating the definition of the given declaration, \p ND. This is
  /// used to determine the proper set of template instantiation arguments for
  /// friend function template specializations.
  ///
  /// \param ForConstraintInstantiation when collecting arguments,
  /// ForConstraintInstantiation indicates we should continue looking when
  /// encountering a lambda generic call operator, and continue looking for
  /// arguments on an enclosing class template.
  MultiLevelTemplateArgumentList getTemplateInstantiationArgs(
      const NamedDecl *D, const DeclContext *DC = nullptr, bool Final = false,
      std::optional<ArrayRef<TemplateArgument>> Innermost = std::nullopt,
      bool RelativeToPrimary = false, const FunctionDecl *Pattern = nullptr,
      bool ForConstraintInstantiation = false,
      bool SkipForSpecialization = false);

  /// RAII object to handle the state changes required to synthesize
  /// a function body.
  class SynthesizedFunctionScope {
    Sema &S;
    Sema::ContextRAII SavedContext;
    bool PushedCodeSynthesisContext = false;

  public:
    SynthesizedFunctionScope(Sema &S, DeclContext *DC)
        : S(S), SavedContext(S, DC) {
      auto *FD = dyn_cast<FunctionDecl>(DC);
      S.PushFunctionScope();
      S.PushExpressionEvaluationContext(
          (FD && FD->isConsteval())
              ? ExpressionEvaluationContext::ImmediateFunctionContext
              : ExpressionEvaluationContext::PotentiallyEvaluated);
      if (FD) {
        FD->setWillHaveBody(true);
        S.ExprEvalContexts.back().InImmediateFunctionContext =
            FD->isImmediateFunction() ||
            S.ExprEvalContexts[S.ExprEvalContexts.size() - 2]
                .isConstantEvaluated() ||
            S.ExprEvalContexts[S.ExprEvalContexts.size() - 2]
                .isImmediateFunctionContext();
        S.ExprEvalContexts.back().InImmediateEscalatingFunctionContext =
            S.getLangOpts().CPlusPlus20 && FD->isImmediateEscalating();
      } else
        assert(isa<ObjCMethodDecl>(DC));
    }

    void addContextNote(SourceLocation UseLoc) {
      assert(!PushedCodeSynthesisContext);

      Sema::CodeSynthesisContext Ctx;
      Ctx.Kind = Sema::CodeSynthesisContext::DefiningSynthesizedFunction;
      Ctx.PointOfInstantiation = UseLoc;
      Ctx.Entity = cast<Decl>(S.CurContext);
      S.pushCodeSynthesisContext(Ctx);

      PushedCodeSynthesisContext = true;
    }

    ~SynthesizedFunctionScope() {
      if (PushedCodeSynthesisContext)
        S.popCodeSynthesisContext();
      if (auto *FD = dyn_cast<FunctionDecl>(S.CurContext)) {
        FD->setWillHaveBody(false);
        S.CheckImmediateEscalatingFunctionDefinition(FD, S.getCurFunction());
      }
      S.PopExpressionEvaluationContext();
      S.PopFunctionScopeInfo();
    }
  };

  /// List of active code synthesis contexts.
  ///
  /// This vector is treated as a stack. As synthesis of one entity requires
  /// synthesis of another, additional contexts are pushed onto the stack.
  SmallVector<CodeSynthesisContext, 16> CodeSynthesisContexts;

  /// Specializations whose definitions are currently being instantiated.
  llvm::DenseSet<std::pair<Decl *, unsigned>> InstantiatingSpecializations;

  /// Non-dependent types used in templates that have already been instantiated
  /// by some template instantiation.
  llvm::DenseSet<QualType> InstantiatedNonDependentTypes;

  /// Extra modules inspected when performing a lookup during a template
  /// instantiation. Computed lazily.
  SmallVector<Module *, 16> CodeSynthesisContextLookupModules;

  /// Cache of additional modules that should be used for name lookup
  /// within the current template instantiation. Computed lazily; use
  /// getLookupModules() to get a complete set.
  llvm::DenseSet<Module *> LookupModulesCache;

  /// Map from the most recent declaration of a namespace to the most
  /// recent visible declaration of that namespace.
  llvm::DenseMap<NamedDecl *, NamedDecl *> VisibleNamespaceCache;

  /// Whether we are in a SFINAE context that is not associated with
  /// template instantiation.
  ///
  /// This is used when setting up a SFINAE trap (\c see SFINAETrap) outside
  /// of a template instantiation or template argument deduction.
  bool InNonInstantiationSFINAEContext;

  /// The number of \p CodeSynthesisContexts that are not template
  /// instantiations and, therefore, should not be counted as part of the
  /// instantiation depth.
  ///
  /// When the instantiation depth reaches the user-configurable limit
  /// \p LangOptions::InstantiationDepth we will abort instantiation.
  // FIXME: Should we have a similar limit for other forms of synthesis?
  unsigned NonInstantiationEntries;

  /// The depth of the context stack at the point when the most recent
  /// error or warning was produced.
  ///
  /// This value is used to suppress printing of redundant context stacks
  /// when there are multiple errors or warnings in the same instantiation.
  // FIXME: Does this belong in Sema? It's tough to implement it anywhere else.
  unsigned LastEmittedCodeSynthesisContextDepth = 0;

  /// The template instantiation callbacks to trace or track
  /// instantiations (objects can be chained).
  ///
  /// This callbacks is used to print, trace or track template
  /// instantiations as they are being constructed.
  std::vector<std::unique_ptr<TemplateInstantiationCallback>>
      TemplateInstCallbacks;

  /// The current index into pack expansion arguments that will be
  /// used for substitution of parameter packs.
  ///
  /// The pack expansion index will be -1 to indicate that parameter packs
  /// should be instantiated as themselves. Otherwise, the index specifies
  /// which argument within the parameter pack will be used for substitution.
  int ArgumentPackSubstitutionIndex;

  /// RAII object used to change the argument pack substitution index
  /// within a \c Sema object.
  ///
  /// See \c ArgumentPackSubstitutionIndex for more information.
  class ArgumentPackSubstitutionIndexRAII {
    Sema &Self;
    int OldSubstitutionIndex;

  public:
    ArgumentPackSubstitutionIndexRAII(Sema &Self, int NewSubstitutionIndex)
        : Self(Self), OldSubstitutionIndex(Self.ArgumentPackSubstitutionIndex) {
      Self.ArgumentPackSubstitutionIndex = NewSubstitutionIndex;
    }

    ~ArgumentPackSubstitutionIndexRAII() {
      Self.ArgumentPackSubstitutionIndex = OldSubstitutionIndex;
    }
  };

  friend class ArgumentPackSubstitutionRAII;

  void pushCodeSynthesisContext(CodeSynthesisContext Ctx);
  void popCodeSynthesisContext();

  void PrintContextStack() {
    if (!CodeSynthesisContexts.empty() &&
        CodeSynthesisContexts.size() != LastEmittedCodeSynthesisContextDepth) {
      PrintInstantiationStack();
      LastEmittedCodeSynthesisContextDepth = CodeSynthesisContexts.size();
    }
    if (PragmaAttributeCurrentTargetDecl)
      PrintPragmaAttributeInstantiationPoint();
  }
  /// Prints the current instantiation stack through a series of
  /// notes.
  void PrintInstantiationStack();

  /// Determines whether we are currently in a context where
  /// template argument substitution failures are not considered
  /// errors.
  ///
  /// \returns An empty \c Optional if we're not in a SFINAE context.
  /// Otherwise, contains a pointer that, if non-NULL, contains the nearest
  /// template-deduction context object, which can be used to capture
  /// diagnostics that will be suppressed.
  std::optional<sema::TemplateDeductionInfo *> isSFINAEContext() const;

  /// Perform substitution on the type T with a given set of template
  /// arguments.
  ///
  /// This routine substitutes the given template arguments into the
  /// type T and produces the instantiated type.
  ///
  /// \param T the type into which the template arguments will be
  /// substituted. If this type is not dependent, it will be returned
  /// immediately.
  ///
  /// \param Args the template arguments that will be
  /// substituted for the top-level template parameters within T.
  ///
  /// \param Loc the location in the source code where this substitution
  /// is being performed. It will typically be the location of the
  /// declarator (if we're instantiating the type of some declaration)
  /// or the location of the type in the source code (if, e.g., we're
  /// instantiating the type of a cast expression).
  ///
  /// \param Entity the name of the entity associated with a declaration
  /// being instantiated (if any). May be empty to indicate that there
  /// is no such entity (if, e.g., this is a type that occurs as part of
  /// a cast expression) or that the entity has no name (e.g., an
  /// unnamed function parameter).
  ///
  /// \param AllowDeducedTST Whether a DeducedTemplateSpecializationType is
  /// acceptable as the top level type of the result.
  ///
  /// \returns If the instantiation succeeds, the instantiated
  /// type. Otherwise, produces diagnostics and returns a NULL type.
  TypeSourceInfo *SubstType(TypeSourceInfo *T,
                            const MultiLevelTemplateArgumentList &TemplateArgs,
                            SourceLocation Loc, DeclarationName Entity,
                            bool AllowDeducedTST = false);

  QualType SubstType(QualType T,
                     const MultiLevelTemplateArgumentList &TemplateArgs,
                     SourceLocation Loc, DeclarationName Entity);

  TypeSourceInfo *SubstType(TypeLoc TL,
                            const MultiLevelTemplateArgumentList &TemplateArgs,
                            SourceLocation Loc, DeclarationName Entity);

  /// A form of SubstType intended specifically for instantiating the
  /// type of a FunctionDecl.  Its purpose is solely to force the
  /// instantiation of default-argument expressions and to avoid
  /// instantiating an exception-specification.
  TypeSourceInfo *SubstFunctionDeclType(
      TypeSourceInfo *T, const MultiLevelTemplateArgumentList &TemplateArgs,
      SourceLocation Loc, DeclarationName Entity, CXXRecordDecl *ThisContext,
      Qualifiers ThisTypeQuals, bool EvaluateConstraints = true);
  void SubstExceptionSpec(FunctionDecl *New, const FunctionProtoType *Proto,
                          const MultiLevelTemplateArgumentList &Args);
  bool SubstExceptionSpec(SourceLocation Loc,
                          FunctionProtoType::ExceptionSpecInfo &ESI,
                          SmallVectorImpl<QualType> &ExceptionStorage,
                          const MultiLevelTemplateArgumentList &Args);
  ParmVarDecl *
  SubstParmVarDecl(ParmVarDecl *D,
                   const MultiLevelTemplateArgumentList &TemplateArgs,
                   int indexAdjustment, std::optional<unsigned> NumExpansions,
                   bool ExpectParameterPack, bool EvaluateConstraints = true);

  /// Substitute the given template arguments into the given set of
  /// parameters, producing the set of parameter types that would be generated
  /// from such a substitution.
  bool SubstParmTypes(SourceLocation Loc, ArrayRef<ParmVarDecl *> Params,
                      const FunctionProtoType::ExtParameterInfo *ExtParamInfos,
                      const MultiLevelTemplateArgumentList &TemplateArgs,
                      SmallVectorImpl<QualType> &ParamTypes,
                      SmallVectorImpl<ParmVarDecl *> *OutParams,
                      ExtParameterInfoBuilder &ParamInfos);

  /// Substitute the given template arguments into the default argument.
  bool SubstDefaultArgument(SourceLocation Loc, ParmVarDecl *Param,
                            const MultiLevelTemplateArgumentList &TemplateArgs,
                            bool ForCallExpr = false);
  ExprResult SubstExpr(Expr *E,
                       const MultiLevelTemplateArgumentList &TemplateArgs);

  // A RAII type used by the TemplateDeclInstantiator and TemplateInstantiator
  // to disable constraint evaluation, then restore the state.
  template <typename InstTy> struct ConstraintEvalRAII {
    InstTy &TI;
    bool OldValue;

    ConstraintEvalRAII(InstTy &TI)
        : TI(TI), OldValue(TI.getEvaluateConstraints()) {
      TI.setEvaluateConstraints(false);
    }
    ~ConstraintEvalRAII() { TI.setEvaluateConstraints(OldValue); }
  };

  // Must be used instead of SubstExpr at 'constraint checking' time.
  ExprResult
  SubstConstraintExpr(Expr *E,
                      const MultiLevelTemplateArgumentList &TemplateArgs);
  // Unlike the above, this does not evaluates constraints.
  ExprResult SubstConstraintExprWithoutSatisfaction(
      Expr *E, const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Substitute the given template arguments into a list of
  /// expressions, expanding pack expansions if required.
  ///
  /// \param Exprs The list of expressions to substitute into.
  ///
  /// \param IsCall Whether this is some form of call, in which case
  /// default arguments will be dropped.
  ///
  /// \param TemplateArgs The set of template arguments to substitute.
  ///
  /// \param Outputs Will receive all of the substituted arguments.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool SubstExprs(ArrayRef<Expr *> Exprs, bool IsCall,
                  const MultiLevelTemplateArgumentList &TemplateArgs,
                  SmallVectorImpl<Expr *> &Outputs);

  StmtResult SubstStmt(Stmt *S,
                       const MultiLevelTemplateArgumentList &TemplateArgs);

  ExprResult
  SubstInitializer(Expr *E, const MultiLevelTemplateArgumentList &TemplateArgs,
                   bool CXXDirectInit);

  /// Perform substitution on the base class specifiers of the
  /// given class template specialization.
  ///
  /// Produces a diagnostic and returns true on error, returns false and
  /// attaches the instantiated base classes to the class template
  /// specialization if successful.
  bool SubstBaseSpecifiers(CXXRecordDecl *Instantiation, CXXRecordDecl *Pattern,
                           const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Instantiate the definition of a class from a given pattern.
  ///
  /// \param PointOfInstantiation The point of instantiation within the
  /// source code.
  ///
  /// \param Instantiation is the declaration whose definition is being
  /// instantiated. This will be either a class template specialization
  /// or a member class of a class template specialization.
  ///
  /// \param Pattern is the pattern from which the instantiation
  /// occurs. This will be either the declaration of a class template or
  /// the declaration of a member class of a class template.
  ///
  /// \param TemplateArgs The template arguments to be substituted into
  /// the pattern.
  ///
  /// \param TSK the kind of implicit or explicit instantiation to perform.
  ///
  /// \param Complain whether to complain if the class cannot be instantiated
  /// due to the lack of a definition.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool InstantiateClass(SourceLocation PointOfInstantiation,
                        CXXRecordDecl *Instantiation, CXXRecordDecl *Pattern,
                        const MultiLevelTemplateArgumentList &TemplateArgs,
                        TemplateSpecializationKind TSK, bool Complain = true);

  /// Instantiate the definition of an enum from a given pattern.
  ///
  /// \param PointOfInstantiation The point of instantiation within the
  ///        source code.
  /// \param Instantiation is the declaration whose definition is being
  ///        instantiated. This will be a member enumeration of a class
  ///        temploid specialization, or a local enumeration within a
  ///        function temploid specialization.
  /// \param Pattern The templated declaration from which the instantiation
  ///        occurs.
  /// \param TemplateArgs The template arguments to be substituted into
  ///        the pattern.
  /// \param TSK The kind of implicit or explicit instantiation to perform.
  ///
  /// \return \c true if an error occurred, \c false otherwise.
  bool InstantiateEnum(SourceLocation PointOfInstantiation,
                       EnumDecl *Instantiation, EnumDecl *Pattern,
                       const MultiLevelTemplateArgumentList &TemplateArgs,
                       TemplateSpecializationKind TSK);

  /// Instantiate the definition of a field from the given pattern.
  ///
  /// \param PointOfInstantiation The point of instantiation within the
  ///        source code.
  /// \param Instantiation is the declaration whose definition is being
  ///        instantiated. This will be a class of a class temploid
  ///        specialization, or a local enumeration within a function temploid
  ///        specialization.
  /// \param Pattern The templated declaration from which the instantiation
  ///        occurs.
  /// \param TemplateArgs The template arguments to be substituted into
  ///        the pattern.
  ///
  /// \return \c true if an error occurred, \c false otherwise.
  bool InstantiateInClassInitializer(
      SourceLocation PointOfInstantiation, FieldDecl *Instantiation,
      FieldDecl *Pattern, const MultiLevelTemplateArgumentList &TemplateArgs);

  bool usesPartialOrExplicitSpecialization(
      SourceLocation Loc, ClassTemplateSpecializationDecl *ClassTemplateSpec);

  bool InstantiateClassTemplateSpecialization(
      SourceLocation PointOfInstantiation,
      ClassTemplateSpecializationDecl *ClassTemplateSpec,
      TemplateSpecializationKind TSK, bool Complain = true);

  /// Instantiates the definitions of all of the member
  /// of the given class, which is an instantiation of a class template
  /// or a member class of a template.
  void
  InstantiateClassMembers(SourceLocation PointOfInstantiation,
                          CXXRecordDecl *Instantiation,
                          const MultiLevelTemplateArgumentList &TemplateArgs,
                          TemplateSpecializationKind TSK);

  /// Instantiate the definitions of all of the members of the
  /// given class template specialization, which was named as part of an
  /// explicit instantiation.
  void InstantiateClassTemplateSpecializationMembers(
      SourceLocation PointOfInstantiation,
      ClassTemplateSpecializationDecl *ClassTemplateSpec,
      TemplateSpecializationKind TSK);

  NestedNameSpecifierLoc SubstNestedNameSpecifierLoc(
      NestedNameSpecifierLoc NNS,
      const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Do template substitution on declaration name info.
  DeclarationNameInfo
  SubstDeclarationNameInfo(const DeclarationNameInfo &NameInfo,
                           const MultiLevelTemplateArgumentList &TemplateArgs);
  TemplateName
  SubstTemplateName(NestedNameSpecifierLoc QualifierLoc, TemplateName Name,
                    SourceLocation Loc,
                    const MultiLevelTemplateArgumentList &TemplateArgs);

  bool SubstTypeConstraint(TemplateTypeParmDecl *Inst, const TypeConstraint *TC,
                           const MultiLevelTemplateArgumentList &TemplateArgs,
                           bool EvaluateConstraint);

  /// Determine whether we are currently performing template instantiation.
  bool inTemplateInstantiation() const {
    return CodeSynthesisContexts.size() > NonInstantiationEntries;
  }

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Template Declaration Instantiation
  /// Implementations are in SemaTemplateInstantiateDecl.cpp
  ///@{

public:
  /// An entity for which implicit template instantiation is required.
  ///
  /// The source location associated with the declaration is the first place in
  /// the source code where the declaration was "used". It is not necessarily
  /// the point of instantiation (which will be either before or after the
  /// namespace-scope declaration that triggered this implicit instantiation),
  /// However, it is the location that diagnostics should generally refer to,
  /// because users will need to know what code triggered the instantiation.
  typedef std::pair<ValueDecl *, SourceLocation> PendingImplicitInstantiation;

  /// The queue of implicit template instantiations that are required
  /// but have not yet been performed.
  std::deque<PendingImplicitInstantiation> PendingInstantiations;

  /// Queue of implicit template instantiations that cannot be performed
  /// eagerly.
  SmallVector<PendingImplicitInstantiation, 1> LateParsedInstantiations;

  SmallVector<SmallVector<VTableUse, 16>, 8> SavedVTableUses;
  SmallVector<std::deque<PendingImplicitInstantiation>, 8>
      SavedPendingInstantiations;

  /// The queue of implicit template instantiations that are required
  /// and must be performed within the current local scope.
  ///
  /// This queue is only used for member functions of local classes in
  /// templates, which must be instantiated in the same scope as their
  /// enclosing function, so that they can reference function-local
  /// types, static variables, enumerators, etc.
  std::deque<PendingImplicitInstantiation> PendingLocalImplicitInstantiations;

  class LocalEagerInstantiationScope {
  public:
    LocalEagerInstantiationScope(Sema &S) : S(S) {
      SavedPendingLocalImplicitInstantiations.swap(
          S.PendingLocalImplicitInstantiations);
    }

    void perform() { S.PerformPendingInstantiations(/*LocalOnly=*/true); }

    ~LocalEagerInstantiationScope() {
      assert(S.PendingLocalImplicitInstantiations.empty() &&
             "there shouldn't be any pending local implicit instantiations");
      SavedPendingLocalImplicitInstantiations.swap(
          S.PendingLocalImplicitInstantiations);
    }

  private:
    Sema &S;
    std::deque<PendingImplicitInstantiation>
        SavedPendingLocalImplicitInstantiations;
  };

  /// Records and restores the CurFPFeatures state on entry/exit of compound
  /// statements.
  class FPFeaturesStateRAII {
  public:
    FPFeaturesStateRAII(Sema &S);
    ~FPFeaturesStateRAII();
    FPOptionsOverride getOverrides() { return OldOverrides; }

  private:
    Sema &S;
    FPOptions OldFPFeaturesState;
    FPOptionsOverride OldOverrides;
    LangOptions::FPEvalMethodKind OldEvalMethod;
    SourceLocation OldFPPragmaLocation;
  };

  class GlobalEagerInstantiationScope {
  public:
    GlobalEagerInstantiationScope(Sema &S, bool Enabled)
        : S(S), Enabled(Enabled) {
      if (!Enabled)
        return;

      S.SavedPendingInstantiations.emplace_back();
      S.SavedPendingInstantiations.back().swap(S.PendingInstantiations);

      S.SavedVTableUses.emplace_back();
      S.SavedVTableUses.back().swap(S.VTableUses);
    }

    void perform() {
      if (Enabled) {
        S.DefineUsedVTables();
        S.PerformPendingInstantiations();
      }
    }

    ~GlobalEagerInstantiationScope() {
      if (!Enabled)
        return;

      // Restore the set of pending vtables.
      assert(S.VTableUses.empty() &&
             "VTableUses should be empty before it is discarded.");
      S.VTableUses.swap(S.SavedVTableUses.back());
      S.SavedVTableUses.pop_back();

      // Restore the set of pending implicit instantiations.
      if (S.TUKind != TU_Prefix || !S.LangOpts.PCHInstantiateTemplates) {
        assert(S.PendingInstantiations.empty() &&
               "PendingInstantiations should be empty before it is discarded.");
        S.PendingInstantiations.swap(S.SavedPendingInstantiations.back());
        S.SavedPendingInstantiations.pop_back();
      } else {
        // Template instantiations in the PCH may be delayed until the TU.
        S.PendingInstantiations.swap(S.SavedPendingInstantiations.back());
        S.PendingInstantiations.insert(
            S.PendingInstantiations.end(),
            S.SavedPendingInstantiations.back().begin(),
            S.SavedPendingInstantiations.back().end());
        S.SavedPendingInstantiations.pop_back();
      }
    }

  private:
    Sema &S;
    bool Enabled;
  };

  ExplicitSpecifier instantiateExplicitSpecifier(
      const MultiLevelTemplateArgumentList &TemplateArgs, ExplicitSpecifier ES);

  struct LateInstantiatedAttribute {
    const Attr *TmplAttr;
    LocalInstantiationScope *Scope;
    Decl *NewDecl;

    LateInstantiatedAttribute(const Attr *A, LocalInstantiationScope *S,
                              Decl *D)
        : TmplAttr(A), Scope(S), NewDecl(D) {}
  };
  typedef SmallVector<LateInstantiatedAttribute, 16> LateInstantiatedAttrVec;

  void InstantiateAttrs(const MultiLevelTemplateArgumentList &TemplateArgs,
                        const Decl *Pattern, Decl *Inst,
                        LateInstantiatedAttrVec *LateAttrs = nullptr,
                        LocalInstantiationScope *OuterMostScope = nullptr);

  /// Update instantiation attributes after template was late parsed.
  ///
  /// Some attributes are evaluated based on the body of template. If it is
  /// late parsed, such attributes cannot be evaluated when declaration is
  /// instantiated. This function is used to update instantiation attributes
  /// when template definition is ready.
  void updateAttrsForLateParsedTemplate(const Decl *Pattern, Decl *Inst);

  void
  InstantiateAttrsForDecl(const MultiLevelTemplateArgumentList &TemplateArgs,
                          const Decl *Pattern, Decl *Inst,
                          LateInstantiatedAttrVec *LateAttrs = nullptr,
                          LocalInstantiationScope *OuterMostScope = nullptr);

  /// In the MS ABI, we need to instantiate default arguments of dllexported
  /// default constructors along with the constructor definition. This allows IR
  /// gen to emit a constructor closure which calls the default constructor with
  /// its default arguments.
  void InstantiateDefaultCtorDefaultArgs(CXXConstructorDecl *Ctor);

  bool InstantiateDefaultArgument(SourceLocation CallLoc, FunctionDecl *FD,
                                  ParmVarDecl *Param);
  void InstantiateExceptionSpec(SourceLocation PointOfInstantiation,
                                FunctionDecl *Function);

  /// Instantiate (or find existing instantiation of) a function template with a
  /// given set of template arguments.
  ///
  /// Usually this should not be used, and template argument deduction should be
  /// used in its place.
  FunctionDecl *InstantiateFunctionDeclaration(
      FunctionTemplateDecl *FTD, const TemplateArgumentList *Args,
      SourceLocation Loc,
      CodeSynthesisContext::SynthesisKind CSC =
          CodeSynthesisContext::ExplicitTemplateArgumentSubstitution);

  /// Instantiate the definition of the given function from its
  /// template.
  ///
  /// \param PointOfInstantiation the point at which the instantiation was
  /// required. Note that this is not precisely a "point of instantiation"
  /// for the function, but it's close.
  ///
  /// \param Function the already-instantiated declaration of a
  /// function template specialization or member function of a class template
  /// specialization.
  ///
  /// \param Recursive if true, recursively instantiates any functions that
  /// are required by this instantiation.
  ///
  /// \param DefinitionRequired if true, then we are performing an explicit
  /// instantiation where the body of the function is required. Complain if
  /// there is no such body.
  void InstantiateFunctionDefinition(SourceLocation PointOfInstantiation,
                                     FunctionDecl *Function,
                                     bool Recursive = false,
                                     bool DefinitionRequired = false,
                                     bool AtEndOfTU = false);
  VarTemplateSpecializationDecl *BuildVarTemplateInstantiation(
      VarTemplateDecl *VarTemplate, VarDecl *FromVar,
      const TemplateArgumentList *PartialSpecArgs,
      const TemplateArgumentListInfo &TemplateArgsInfo,
      SmallVectorImpl<TemplateArgument> &Converted,
      SourceLocation PointOfInstantiation,
      LateInstantiatedAttrVec *LateAttrs = nullptr,
      LocalInstantiationScope *StartingScope = nullptr);

  /// Instantiates a variable template specialization by completing it
  /// with appropriate type information and initializer.
  VarTemplateSpecializationDecl *CompleteVarTemplateSpecializationDecl(
      VarTemplateSpecializationDecl *VarSpec, VarDecl *PatternDecl,
      const MultiLevelTemplateArgumentList &TemplateArgs);

  /// BuildVariableInstantiation - Used after a new variable has been created.
  /// Sets basic variable data and decides whether to postpone the
  /// variable instantiation.
  void
  BuildVariableInstantiation(VarDecl *NewVar, VarDecl *OldVar,
                             const MultiLevelTemplateArgumentList &TemplateArgs,
                             LateInstantiatedAttrVec *LateAttrs,
                             DeclContext *Owner,
                             LocalInstantiationScope *StartingScope,
                             bool InstantiatingVarTemplate = false,
                             VarTemplateSpecializationDecl *PrevVTSD = nullptr);

  /// Instantiate the initializer of a variable.
  void InstantiateVariableInitializer(
      VarDecl *Var, VarDecl *OldVar,
      const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Instantiate the definition of the given variable from its
  /// template.
  ///
  /// \param PointOfInstantiation the point at which the instantiation was
  /// required. Note that this is not precisely a "point of instantiation"
  /// for the variable, but it's close.
  ///
  /// \param Var the already-instantiated declaration of a templated variable.
  ///
  /// \param Recursive if true, recursively instantiates any functions that
  /// are required by this instantiation.
  ///
  /// \param DefinitionRequired if true, then we are performing an explicit
  /// instantiation where a definition of the variable is required. Complain
  /// if there is no such definition.
  void InstantiateVariableDefinition(SourceLocation PointOfInstantiation,
                                     VarDecl *Var, bool Recursive = false,
                                     bool DefinitionRequired = false,
                                     bool AtEndOfTU = false);

  void InstantiateMemInitializers(
      CXXConstructorDecl *New, const CXXConstructorDecl *Tmpl,
      const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Find the instantiation of the given declaration within the
  /// current instantiation.
  ///
  /// This routine is intended to be used when \p D is a declaration
  /// referenced from within a template, that needs to mapped into the
  /// corresponding declaration within an instantiation. For example,
  /// given:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   enum Kind {
  ///     KnownValue = sizeof(T)
  ///   };
  ///
  ///   bool getKind() const { return KnownValue; }
  /// };
  ///
  /// template struct X<int>;
  /// \endcode
  ///
  /// In the instantiation of X<int>::getKind(), we need to map the \p
  /// EnumConstantDecl for \p KnownValue (which refers to
  /// X<T>::<Kind>::KnownValue) to its instantiation
  /// (X<int>::<Kind>::KnownValue).
  /// \p FindInstantiatedDecl performs this mapping from within the
  /// instantiation of X<int>.
  NamedDecl *
  FindInstantiatedDecl(SourceLocation Loc, NamedDecl *D,
                       const MultiLevelTemplateArgumentList &TemplateArgs,
                       bool FindingInstantiatedContext = false);

  /// Finds the instantiation of the given declaration context
  /// within the current instantiation.
  ///
  /// \returns NULL if there was an error
  DeclContext *
  FindInstantiatedContext(SourceLocation Loc, DeclContext *DC,
                          const MultiLevelTemplateArgumentList &TemplateArgs);

  Decl *SubstDecl(Decl *D, DeclContext *Owner,
                  const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Substitute the name and return type of a defaulted 'operator<=>' to form
  /// an implicit 'operator=='.
  FunctionDecl *SubstSpaceshipAsEqualEqual(CXXRecordDecl *RD,
                                           FunctionDecl *Spaceship);

  /// Performs template instantiation for all implicit template
  /// instantiations we have seen until this point.
  void PerformPendingInstantiations(bool LocalOnly = false);

  TemplateParameterList *
  SubstTemplateParams(TemplateParameterList *Params, DeclContext *Owner,
                      const MultiLevelTemplateArgumentList &TemplateArgs,
                      bool EvaluateConstraints = true);

  void PerformDependentDiagnostics(
      const DeclContext *Pattern,
      const MultiLevelTemplateArgumentList &TemplateArgs);

private:
  /// Introduce the instantiated local variables into the local
  /// instantiation scope.
  void addInstantiatedLocalVarsToScope(FunctionDecl *Function,
                                       const FunctionDecl *PatternDecl,
                                       LocalInstantiationScope &Scope);
  /// Introduce the instantiated function parameters into the local
  /// instantiation scope, and set the parameter names to those used
  /// in the template.
  bool addInstantiatedParametersToScope(
      FunctionDecl *Function, const FunctionDecl *PatternDecl,
      LocalInstantiationScope &Scope,
      const MultiLevelTemplateArgumentList &TemplateArgs);

  int ParsingClassDepth = 0;

  class SavePendingParsedClassStateRAII {
  public:
    SavePendingParsedClassStateRAII(Sema &S) : S(S) { swapSavedState(); }

    ~SavePendingParsedClassStateRAII() {
      assert(S.DelayedOverridingExceptionSpecChecks.empty() &&
             "there shouldn't be any pending delayed exception spec checks");
      assert(S.DelayedEquivalentExceptionSpecChecks.empty() &&
             "there shouldn't be any pending delayed exception spec checks");
      swapSavedState();
    }

  private:
    Sema &S;
    decltype(DelayedOverridingExceptionSpecChecks)
        SavedOverridingExceptionSpecChecks;
    decltype(DelayedEquivalentExceptionSpecChecks)
        SavedEquivalentExceptionSpecChecks;

    void swapSavedState() {
      SavedOverridingExceptionSpecChecks.swap(
          S.DelayedOverridingExceptionSpecChecks);
      SavedEquivalentExceptionSpecChecks.swap(
          S.DelayedEquivalentExceptionSpecChecks);
    }
  };

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name C++ Variadic Templates
  /// Implementations are in SemaTemplateVariadic.cpp
  ///@{

public:
  /// Determine whether an unexpanded parameter pack might be permitted in this
  /// location. Useful for error recovery.
  bool isUnexpandedParameterPackPermitted();

  /// The context in which an unexpanded parameter pack is
  /// being diagnosed.
  ///
  /// Note that the values of this enumeration line up with the first
  /// argument to the \c err_unexpanded_parameter_pack diagnostic.
  enum UnexpandedParameterPackContext {
    /// An arbitrary expression.
    UPPC_Expression = 0,

    /// The base type of a class type.
    UPPC_BaseType,

    /// The type of an arbitrary declaration.
    UPPC_DeclarationType,

    /// The type of a data member.
    UPPC_DataMemberType,

    /// The size of a bit-field.
    UPPC_BitFieldWidth,

    /// The expression in a static assertion.
    UPPC_StaticAssertExpression,

    /// The fixed underlying type of an enumeration.
    UPPC_FixedUnderlyingType,

    /// The enumerator value.
    UPPC_EnumeratorValue,

    /// A using declaration.
    UPPC_UsingDeclaration,

    /// A friend declaration.
    UPPC_FriendDeclaration,

    /// A declaration qualifier.
    UPPC_DeclarationQualifier,

    /// An initializer.
    UPPC_Initializer,

    /// A default argument.
    UPPC_DefaultArgument,

    /// The type of a non-type template parameter.
    UPPC_NonTypeTemplateParameterType,

    /// The type of an exception.
    UPPC_ExceptionType,

    /// Explicit specialization.
    UPPC_ExplicitSpecialization,

    /// Partial specialization.
    UPPC_PartialSpecialization,

    /// Microsoft __if_exists.
    UPPC_IfExists,

    /// Microsoft __if_not_exists.
    UPPC_IfNotExists,

    /// Lambda expression.
    UPPC_Lambda,

    /// Block expression.
    UPPC_Block,

    /// A type constraint.
    UPPC_TypeConstraint,

    // A requirement in a requires-expression.
    UPPC_Requirement,

    // A requires-clause.
    UPPC_RequiresClause,
  };

  /// Diagnose unexpanded parameter packs.
  ///
  /// \param Loc The location at which we should emit the diagnostic.
  ///
  /// \param UPPC The context in which we are diagnosing unexpanded
  /// parameter packs.
  ///
  /// \param Unexpanded the set of unexpanded parameter packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPacks(
      SourceLocation Loc, UnexpandedParameterPackContext UPPC,
      ArrayRef<UnexpandedParameterPack> Unexpanded);

  /// If the given type contains an unexpanded parameter pack,
  /// diagnose the error.
  ///
  /// \param Loc The source location where a diagnostc should be emitted.
  ///
  /// \param T The type that is being checked for unexpanded parameter
  /// packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPack(SourceLocation Loc, TypeSourceInfo *T,
                                       UnexpandedParameterPackContext UPPC);

  /// If the given expression contains an unexpanded parameter
  /// pack, diagnose the error.
  ///
  /// \param E The expression that is being checked for unexpanded
  /// parameter packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPack(
      Expr *E, UnexpandedParameterPackContext UPPC = UPPC_Expression);

  /// If the given requirees-expression contains an unexpanded reference to one
  /// of its own parameter packs, diagnose the error.
  ///
  /// \param RE The requiress-expression that is being checked for unexpanded
  /// parameter packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPackInRequiresExpr(RequiresExpr *RE);

  /// If the given nested-name-specifier contains an unexpanded
  /// parameter pack, diagnose the error.
  ///
  /// \param SS The nested-name-specifier that is being checked for
  /// unexpanded parameter packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPack(const CXXScopeSpec &SS,
                                       UnexpandedParameterPackContext UPPC);

  /// If the given name contains an unexpanded parameter pack,
  /// diagnose the error.
  ///
  /// \param NameInfo The name (with source location information) that
  /// is being checked for unexpanded parameter packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPack(const DeclarationNameInfo &NameInfo,
                                       UnexpandedParameterPackContext UPPC);

  /// If the given template name contains an unexpanded parameter pack,
  /// diagnose the error.
  ///
  /// \param Loc The location of the template name.
  ///
  /// \param Template The template name that is being checked for unexpanded
  /// parameter packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPack(SourceLocation Loc,
                                       TemplateName Template,
                                       UnexpandedParameterPackContext UPPC);

  /// If the given template argument contains an unexpanded parameter
  /// pack, diagnose the error.
  ///
  /// \param Arg The template argument that is being checked for unexpanded
  /// parameter packs.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool DiagnoseUnexpandedParameterPack(TemplateArgumentLoc Arg,
                                       UnexpandedParameterPackContext UPPC);

  /// Collect the set of unexpanded parameter packs within the given
  /// template argument.
  ///
  /// \param Arg The template argument that will be traversed to find
  /// unexpanded parameter packs.
  void collectUnexpandedParameterPacks(
      TemplateArgument Arg,
      SmallVectorImpl<UnexpandedParameterPack> &Unexpanded);

  /// Collect the set of unexpanded parameter packs within the given
  /// template argument.
  ///
  /// \param Arg The template argument that will be traversed to find
  /// unexpanded parameter packs.
  void collectUnexpandedParameterPacks(
      TemplateArgumentLoc Arg,
      SmallVectorImpl<UnexpandedParameterPack> &Unexpanded);

  /// Collect the set of unexpanded parameter packs within the given
  /// type.
  ///
  /// \param T The type that will be traversed to find
  /// unexpanded parameter packs.
  void collectUnexpandedParameterPacks(
      QualType T, SmallVectorImpl<UnexpandedParameterPack> &Unexpanded);

  /// Collect the set of unexpanded parameter packs within the given
  /// type.
  ///
  /// \param TL The type that will be traversed to find
  /// unexpanded parameter packs.
  void collectUnexpandedParameterPacks(
      TypeLoc TL, SmallVectorImpl<UnexpandedParameterPack> &Unexpanded);

  /// Collect the set of unexpanded parameter packs within the given
  /// nested-name-specifier.
  ///
  /// \param NNS The nested-name-specifier that will be traversed to find
  /// unexpanded parameter packs.
  void collectUnexpandedParameterPacks(
      NestedNameSpecifierLoc NNS,
      SmallVectorImpl<UnexpandedParameterPack> &Unexpanded);

  /// Collect the set of unexpanded parameter packs within the given
  /// name.
  ///
  /// \param NameInfo The name that will be traversed to find
  /// unexpanded parameter packs.
  void collectUnexpandedParameterPacks(
      const DeclarationNameInfo &NameInfo,
      SmallVectorImpl<UnexpandedParameterPack> &Unexpanded);

  /// Collect the set of unexpanded parameter packs within the given
  /// expression.
  static void collectUnexpandedParameterPacks(
      Expr *E, SmallVectorImpl<UnexpandedParameterPack> &Unexpanded);

  /// Invoked when parsing a template argument followed by an
  /// ellipsis, which creates a pack expansion.
  ///
  /// \param Arg The template argument preceding the ellipsis, which
  /// may already be invalid.
  ///
  /// \param EllipsisLoc The location of the ellipsis.
  ParsedTemplateArgument ActOnPackExpansion(const ParsedTemplateArgument &Arg,
                                            SourceLocation EllipsisLoc);

  /// Invoked when parsing a type followed by an ellipsis, which
  /// creates a pack expansion.
  ///
  /// \param Type The type preceding the ellipsis, which will become
  /// the pattern of the pack expansion.
  ///
  /// \param EllipsisLoc The location of the ellipsis.
  TypeResult ActOnPackExpansion(ParsedType Type, SourceLocation EllipsisLoc);

  /// Construct a pack expansion type from the pattern of the pack
  /// expansion.
  TypeSourceInfo *CheckPackExpansion(TypeSourceInfo *Pattern,
                                     SourceLocation EllipsisLoc,
                                     std::optional<unsigned> NumExpansions);

  /// Construct a pack expansion type from the pattern of the pack
  /// expansion.
  QualType CheckPackExpansion(QualType Pattern, SourceRange PatternRange,
                              SourceLocation EllipsisLoc,
                              std::optional<unsigned> NumExpansions);

  /// Invoked when parsing an expression followed by an ellipsis, which
  /// creates a pack expansion.
  ///
  /// \param Pattern The expression preceding the ellipsis, which will become
  /// the pattern of the pack expansion.
  ///
  /// \param EllipsisLoc The location of the ellipsis.
  ExprResult ActOnPackExpansion(Expr *Pattern, SourceLocation EllipsisLoc);

  /// Invoked when parsing an expression followed by an ellipsis, which
  /// creates a pack expansion.
  ///
  /// \param Pattern The expression preceding the ellipsis, which will become
  /// the pattern of the pack expansion.
  ///
  /// \param EllipsisLoc The location of the ellipsis.
  ExprResult CheckPackExpansion(Expr *Pattern, SourceLocation EllipsisLoc,
                                std::optional<unsigned> NumExpansions);

  /// Determine whether we could expand a pack expansion with the
  /// given set of parameter packs into separate arguments by repeatedly
  /// transforming the pattern.
  ///
  /// \param EllipsisLoc The location of the ellipsis that identifies the
  /// pack expansion.
  ///
  /// \param PatternRange The source range that covers the entire pattern of
  /// the pack expansion.
  ///
  /// \param Unexpanded The set of unexpanded parameter packs within the
  /// pattern.
  ///
  /// \param ShouldExpand Will be set to \c true if the transformer should
  /// expand the corresponding pack expansions into separate arguments. When
  /// set, \c NumExpansions must also be set.
  ///
  /// \param RetainExpansion Whether the caller should add an unexpanded
  /// pack expansion after all of the expanded arguments. This is used
  /// when extending explicitly-specified template argument packs per
  /// C++0x [temp.arg.explicit]p9.
  ///
  /// \param NumExpansions The number of separate arguments that will be in
  /// the expanded form of the corresponding pack expansion. This is both an
  /// input and an output parameter, which can be set by the caller if the
  /// number of expansions is known a priori (e.g., due to a prior substitution)
  /// and will be set by the callee when the number of expansions is known.
  /// The callee must set this value when \c ShouldExpand is \c true; it may
  /// set this value in other cases.
  ///
  /// \returns true if an error occurred (e.g., because the parameter packs
  /// are to be instantiated with arguments of different lengths), false
  /// otherwise. If false, \c ShouldExpand (and possibly \c NumExpansions)
  /// must be set.
  bool CheckParameterPacksForExpansion(
      SourceLocation EllipsisLoc, SourceRange PatternRange,
      ArrayRef<UnexpandedParameterPack> Unexpanded,
      const MultiLevelTemplateArgumentList &TemplateArgs, bool &ShouldExpand,
      bool &RetainExpansion, std::optional<unsigned> &NumExpansions);

  /// Determine the number of arguments in the given pack expansion
  /// type.
  ///
  /// This routine assumes that the number of arguments in the expansion is
  /// consistent across all of the unexpanded parameter packs in its pattern.
  ///
  /// Returns an empty Optional if the type can't be expanded.
  std::optional<unsigned> getNumArgumentsInExpansion(
      QualType T, const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Determine whether the given declarator contains any unexpanded
  /// parameter packs.
  ///
  /// This routine is used by the parser to disambiguate function declarators
  /// with an ellipsis prior to the ')', e.g.,
  ///
  /// \code
  ///   void f(T...);
  /// \endcode
  ///
  /// To determine whether we have an (unnamed) function parameter pack or
  /// a variadic function.
  ///
  /// \returns true if the declarator contains any unexpanded parameter packs,
  /// false otherwise.
  bool containsUnexpandedParameterPacks(Declarator &D);

  /// Returns the pattern of the pack expansion for a template argument.
  ///
  /// \param OrigLoc The template argument to expand.
  ///
  /// \param Ellipsis Will be set to the location of the ellipsis.
  ///
  /// \param NumExpansions Will be set to the number of expansions that will
  /// be generated from this pack expansion, if known a priori.
  TemplateArgumentLoc getTemplateArgumentPackExpansionPattern(
      TemplateArgumentLoc OrigLoc, SourceLocation &Ellipsis,
      std::optional<unsigned> &NumExpansions) const;

  /// Given a template argument that contains an unexpanded parameter pack, but
  /// which has already been substituted, attempt to determine the number of
  /// elements that will be produced once this argument is fully-expanded.
  ///
  /// This is intended for use when transforming 'sizeof...(Arg)' in order to
  /// avoid actually expanding the pack where possible.
  std::optional<unsigned> getFullyPackExpandedSize(TemplateArgument Arg);

  /// Called when an expression computing the size of a parameter pack
  /// is parsed.
  ///
  /// \code
  /// template<typename ...Types> struct count {
  ///   static const unsigned value = sizeof...(Types);
  /// };
  /// \endcode
  ///
  //
  /// \param OpLoc The location of the "sizeof" keyword.
  /// \param Name The name of the parameter pack whose size will be determined.
  /// \param NameLoc The source location of the name of the parameter pack.
  /// \param RParenLoc The location of the closing parentheses.
  ExprResult ActOnSizeofParameterPackExpr(Scope *S, SourceLocation OpLoc,
                                          IdentifierInfo &Name,
                                          SourceLocation NameLoc,
                                          SourceLocation RParenLoc);

  ExprResult ActOnPackIndexingExpr(Scope *S, Expr *PackExpression,
                                   SourceLocation EllipsisLoc,
                                   SourceLocation LSquareLoc, Expr *IndexExpr,
                                   SourceLocation RSquareLoc);

  ExprResult BuildPackIndexingExpr(Expr *PackExpression,
                                   SourceLocation EllipsisLoc, Expr *IndexExpr,
                                   SourceLocation RSquareLoc,
                                   ArrayRef<Expr *> ExpandedExprs = {},
                                   bool EmptyPack = false);

  /// Handle a C++1z fold-expression: ( expr op ... op expr ).
  ExprResult ActOnCXXFoldExpr(Scope *S, SourceLocation LParenLoc, Expr *LHS,
                              tok::TokenKind Operator,
                              SourceLocation EllipsisLoc, Expr *RHS,
                              SourceLocation RParenLoc);
  ExprResult BuildCXXFoldExpr(UnresolvedLookupExpr *Callee,
                              SourceLocation LParenLoc, Expr *LHS,
                              BinaryOperatorKind Operator,
                              SourceLocation EllipsisLoc, Expr *RHS,
                              SourceLocation RParenLoc,
                              std::optional<unsigned> NumExpansions);
  ExprResult BuildEmptyCXXFoldExpr(SourceLocation EllipsisLoc,
                                   BinaryOperatorKind Operator);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Constraints and Concepts
  /// Implementations are in SemaConcept.cpp
  ///@{

public:
  void PushSatisfactionStackEntry(const NamedDecl *D,
                                  const llvm::FoldingSetNodeID &ID) {
    const NamedDecl *Can = cast<NamedDecl>(D->getCanonicalDecl());
    SatisfactionStack.emplace_back(Can, ID);
  }

  void PopSatisfactionStackEntry() { SatisfactionStack.pop_back(); }

  bool SatisfactionStackContains(const NamedDecl *D,
                                 const llvm::FoldingSetNodeID &ID) const {
    const NamedDecl *Can = cast<NamedDecl>(D->getCanonicalDecl());
    return llvm::find(SatisfactionStack, SatisfactionStackEntryTy{Can, ID}) !=
           SatisfactionStack.end();
  }

  using SatisfactionStackEntryTy =
      std::pair<const NamedDecl *, llvm::FoldingSetNodeID>;

  // Resets the current SatisfactionStack for cases where we are instantiating
  // constraints as a 'side effect' of normal instantiation in a way that is not
  // indicative of recursive definition.
  class SatisfactionStackResetRAII {
    llvm::SmallVector<SatisfactionStackEntryTy, 10> BackupSatisfactionStack;
    Sema &SemaRef;

  public:
    SatisfactionStackResetRAII(Sema &S) : SemaRef(S) {
      SemaRef.SwapSatisfactionStack(BackupSatisfactionStack);
    }

    ~SatisfactionStackResetRAII() {
      SemaRef.SwapSatisfactionStack(BackupSatisfactionStack);
    }
  };

  void SwapSatisfactionStack(
      llvm::SmallVectorImpl<SatisfactionStackEntryTy> &NewSS) {
    SatisfactionStack.swap(NewSS);
  }

  /// Check whether the given expression is a valid constraint expression.
  /// A diagnostic is emitted if it is not, false is returned, and
  /// PossibleNonPrimary will be set to true if the failure might be due to a
  /// non-primary expression being used as an atomic constraint.
  bool CheckConstraintExpression(const Expr *CE, Token NextToken = Token(),
                                 bool *PossibleNonPrimary = nullptr,
                                 bool IsTrailingRequiresClause = false);

  /// \brief Check whether the given list of constraint expressions are
  /// satisfied (as if in a 'conjunction') given template arguments.
  /// \param Template the template-like entity that triggered the constraints
  /// check (either a concept or a constrained entity).
  /// \param ConstraintExprs a list of constraint expressions, treated as if
  /// they were 'AND'ed together.
  /// \param TemplateArgLists the list of template arguments to substitute into
  /// the constraint expression.
  /// \param TemplateIDRange The source range of the template id that
  /// caused the constraints check.
  /// \param Satisfaction if true is returned, will contain details of the
  /// satisfaction, with enough information to diagnose an unsatisfied
  /// expression.
  /// \returns true if an error occurred and satisfaction could not be checked,
  /// false otherwise.
  bool CheckConstraintSatisfaction(
      const NamedDecl *Template, ArrayRef<const Expr *> ConstraintExprs,
      const MultiLevelTemplateArgumentList &TemplateArgLists,
      SourceRange TemplateIDRange, ConstraintSatisfaction &Satisfaction) {
    llvm::SmallVector<Expr *, 4> Converted;
    return CheckConstraintSatisfaction(Template, ConstraintExprs, Converted,
                                       TemplateArgLists, TemplateIDRange,
                                       Satisfaction);
  }

  /// \brief Check whether the given list of constraint expressions are
  /// satisfied (as if in a 'conjunction') given template arguments.
  /// Additionally, takes an empty list of Expressions which is populated with
  /// the instantiated versions of the ConstraintExprs.
  /// \param Template the template-like entity that triggered the constraints
  /// check (either a concept or a constrained entity).
  /// \param ConstraintExprs a list of constraint expressions, treated as if
  /// they were 'AND'ed together.
  /// \param ConvertedConstraints a out parameter that will get populated with
  /// the instantiated version of the ConstraintExprs if we successfully checked
  /// satisfaction.
  /// \param TemplateArgList the multi-level list of template arguments to
  /// substitute into the constraint expression. This should be relative to the
  /// top-level (hence multi-level), since we need to instantiate fully at the
  /// time of checking.
  /// \param TemplateIDRange The source range of the template id that
  /// caused the constraints check.
  /// \param Satisfaction if true is returned, will contain details of the
  /// satisfaction, with enough information to diagnose an unsatisfied
  /// expression.
  /// \returns true if an error occurred and satisfaction could not be checked,
  /// false otherwise.
  bool CheckConstraintSatisfaction(
      const NamedDecl *Template, ArrayRef<const Expr *> ConstraintExprs,
      llvm::SmallVectorImpl<Expr *> &ConvertedConstraints,
      const MultiLevelTemplateArgumentList &TemplateArgList,
      SourceRange TemplateIDRange, ConstraintSatisfaction &Satisfaction);

  /// \brief Check whether the given non-dependent constraint expression is
  /// satisfied. Returns false and updates Satisfaction with the satisfaction
  /// verdict if successful, emits a diagnostic and returns true if an error
  /// occurred and satisfaction could not be determined.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool CheckConstraintSatisfaction(const Expr *ConstraintExpr,
                                   ConstraintSatisfaction &Satisfaction);

  /// Check whether the given function decl's trailing requires clause is
  /// satisfied, if any. Returns false and updates Satisfaction with the
  /// satisfaction verdict if successful, emits a diagnostic and returns true if
  /// an error occurred and satisfaction could not be determined.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool CheckFunctionConstraints(const FunctionDecl *FD,
                                ConstraintSatisfaction &Satisfaction,
                                SourceLocation UsageLoc = SourceLocation(),
                                bool ForOverloadResolution = false);

  // Calculates whether two constraint expressions are equal irrespective of a
  // difference in 'depth'. This takes a pair of optional 'NamedDecl's 'Old' and
  // 'New', which are the "source" of the constraint, since this is necessary
  // for figuring out the relative 'depth' of the constraint. The depth of the
  // 'primary template' and the 'instantiated from' templates aren't necessarily
  // the same, such as a case when one is a 'friend' defined in a class.
  bool AreConstraintExpressionsEqual(const NamedDecl *Old,
                                     const Expr *OldConstr,
                                     const TemplateCompareNewDeclInfo &New,
                                     const Expr *NewConstr);

  // Calculates whether the friend function depends on an enclosing template for
  // the purposes of [temp.friend] p9.
  bool FriendConstraintsDependOnEnclosingTemplate(const FunctionDecl *FD);

  /// \brief Ensure that the given template arguments satisfy the constraints
  /// associated with the given template, emitting a diagnostic if they do not.
  ///
  /// \param Template The template to which the template arguments are being
  /// provided.
  ///
  /// \param TemplateArgs The converted, canonicalized template arguments.
  ///
  /// \param TemplateIDRange The source range of the template id that
  /// caused the constraints check.
  ///
  /// \returns true if the constrains are not satisfied or could not be checked
  /// for satisfaction, false if the constraints are satisfied.
  bool EnsureTemplateArgumentListConstraints(
      TemplateDecl *Template,
      const MultiLevelTemplateArgumentList &TemplateArgs,
      SourceRange TemplateIDRange);

  bool CheckInstantiatedFunctionTemplateConstraints(
      SourceLocation PointOfInstantiation, FunctionDecl *Decl,
      ArrayRef<TemplateArgument> TemplateArgs,
      ConstraintSatisfaction &Satisfaction);

  /// \brief Emit diagnostics explaining why a constraint expression was deemed
  /// unsatisfied.
  /// \param First whether this is the first time an unsatisfied constraint is
  /// diagnosed for this error.
  void DiagnoseUnsatisfiedConstraint(const ConstraintSatisfaction &Satisfaction,
                                     bool First = true);

  /// \brief Emit diagnostics explaining why a constraint expression was deemed
  /// unsatisfied.
  void
  DiagnoseUnsatisfiedConstraint(const ASTConstraintSatisfaction &Satisfaction,
                                bool First = true);

  const NormalizedConstraint *getNormalizedAssociatedConstraints(
      NamedDecl *ConstrainedDecl, ArrayRef<const Expr *> AssociatedConstraints);

  /// \brief Check whether the given declaration's associated constraints are
  /// at least as constrained than another declaration's according to the
  /// partial ordering of constraints.
  ///
  /// \param Result If no error occurred, receives the result of true if D1 is
  /// at least constrained than D2, and false otherwise.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool IsAtLeastAsConstrained(NamedDecl *D1, MutableArrayRef<const Expr *> AC1,
                              NamedDecl *D2, MutableArrayRef<const Expr *> AC2,
                              bool &Result);

  /// If D1 was not at least as constrained as D2, but would've been if a pair
  /// of atomic constraints involved had been declared in a concept and not
  /// repeated in two separate places in code.
  /// \returns true if such a diagnostic was emitted, false otherwise.
  bool MaybeEmitAmbiguousAtomicConstraintsDiagnostic(
      NamedDecl *D1, ArrayRef<const Expr *> AC1, NamedDecl *D2,
      ArrayRef<const Expr *> AC2);

private:
  /// Caches pairs of template-like decls whose associated constraints were
  /// checked for subsumption and whether or not the first's constraints did in
  /// fact subsume the second's.
  llvm::DenseMap<std::pair<NamedDecl *, NamedDecl *>, bool> SubsumptionCache;
  /// Caches the normalized associated constraints of declarations (concepts or
  /// constrained declarations). If an error occurred while normalizing the
  /// associated constraints of the template or concept, nullptr will be cached
  /// here.
  llvm::DenseMap<NamedDecl *, NormalizedConstraint *> NormalizationCache;

  llvm::ContextualFoldingSet<ConstraintSatisfaction, const ASTContext &>
      SatisfactionCache;

  // The current stack of constraint satisfactions, so we can exit-early.
  llvm::SmallVector<SatisfactionStackEntryTy, 10> SatisfactionStack;

  /// Introduce the instantiated captures of the lambda into the local
  /// instantiation scope.
  bool addInstantiatedCapturesToScope(
      FunctionDecl *Function, const FunctionDecl *PatternDecl,
      LocalInstantiationScope &Scope,
      const MultiLevelTemplateArgumentList &TemplateArgs);

  /// Used by SetupConstraintCheckingTemplateArgumentsAndScope to recursively(in
  /// the case of lambdas) set up the LocalInstantiationScope of the current
  /// function.
  bool
  SetupConstraintScope(FunctionDecl *FD,
                       std::optional<ArrayRef<TemplateArgument>> TemplateArgs,
                       const MultiLevelTemplateArgumentList &MLTAL,
                       LocalInstantiationScope &Scope);

  /// Used during constraint checking, sets up the constraint template argument
  /// lists, and calls SetupConstraintScope to set up the
  /// LocalInstantiationScope to have the proper set of ParVarDecls configured.
  std::optional<MultiLevelTemplateArgumentList>
  SetupConstraintCheckingTemplateArgumentsAndScope(
      FunctionDecl *FD, std::optional<ArrayRef<TemplateArgument>> TemplateArgs,
      LocalInstantiationScope &Scope);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Types
  /// Implementations are in SemaType.cpp
  ///@{

public:
  /// A mapping that describes the nullability we've seen in each header file.
  FileNullabilityMap NullabilityMap;

  static int getPrintable(int I) { return I; }
  static unsigned getPrintable(unsigned I) { return I; }
  static bool getPrintable(bool B) { return B; }
  static const char *getPrintable(const char *S) { return S; }
  static StringRef getPrintable(StringRef S) { return S; }
  static const std::string &getPrintable(const std::string &S) { return S; }
  static const IdentifierInfo *getPrintable(const IdentifierInfo *II) {
    return II;
  }
  static DeclarationName getPrintable(DeclarationName N) { return N; }
  static QualType getPrintable(QualType T) { return T; }
  static SourceRange getPrintable(SourceRange R) { return R; }
  static SourceRange getPrintable(SourceLocation L) { return L; }
  static SourceRange getPrintable(const Expr *E) { return E->getSourceRange(); }
  static SourceRange getPrintable(TypeLoc TL) { return TL.getSourceRange(); }

  enum class CompleteTypeKind {
    /// Apply the normal rules for complete types.  In particular,
    /// treat all sizeless types as incomplete.
    Normal,

    /// Relax the normal rules for complete types so that they include
    /// sizeless built-in types.
    AcceptSizeless,

    // FIXME: Eventually we should flip the default to Normal and opt in
    // to AcceptSizeless rather than opt out of it.
    Default = AcceptSizeless
  };

  QualType BuildQualifiedType(QualType T, SourceLocation Loc, Qualifiers Qs,
                              const DeclSpec *DS = nullptr);
  QualType BuildQualifiedType(QualType T, SourceLocation Loc, unsigned CVRA,
                              const DeclSpec *DS = nullptr);

  /// Build a pointer type.
  ///
  /// \param T The type to which we'll be building a pointer.
  ///
  /// \param Loc The location of the entity whose type involves this
  /// pointer type or, if there is no such entity, the location of the
  /// type that will have pointer type.
  ///
  /// \param Entity The name of the entity that involves the pointer
  /// type, if known.
  ///
  /// \returns A suitable pointer type, if there are no
  /// errors. Otherwise, returns a NULL type.
  QualType BuildPointerType(QualType T, SourceLocation Loc,
                            DeclarationName Entity);

  /// Build a reference type.
  ///
  /// \param T The type to which we'll be building a reference.
  ///
  /// \param Loc The location of the entity whose type involves this
  /// reference type or, if there is no such entity, the location of the
  /// type that will have reference type.
  ///
  /// \param Entity The name of the entity that involves the reference
  /// type, if known.
  ///
  /// \returns A suitable reference type, if there are no
  /// errors. Otherwise, returns a NULL type.
  QualType BuildReferenceType(QualType T, bool LValueRef, SourceLocation Loc,
                              DeclarationName Entity);

  /// Build an array type.
  ///
  /// \param T The type of each element in the array.
  ///
  /// \param ASM C99 array size modifier (e.g., '*', 'static').
  ///
  /// \param ArraySize Expression describing the size of the array.
  ///
  /// \param Brackets The range from the opening '[' to the closing ']'.
  ///
  /// \param Entity The name of the entity that involves the array
  /// type, if known.
  ///
  /// \returns A suitable array type, if there are no errors. Otherwise,
  /// returns a NULL type.
  QualType BuildArrayType(QualType T, ArraySizeModifier ASM, Expr *ArraySize,
                          unsigned Quals, SourceRange Brackets,
                          DeclarationName Entity);
  QualType BuildVectorType(QualType T, Expr *VecSize, SourceLocation AttrLoc);

  /// Build an ext-vector type.
  ///
  /// Run the required checks for the extended vector type.
  QualType BuildExtVectorType(QualType T, Expr *ArraySize,
                              SourceLocation AttrLoc);
  QualType BuildMatrixType(QualType T, Expr *NumRows, Expr *NumColumns,
                           SourceLocation AttrLoc);

  QualType BuildCountAttributedArrayOrPointerType(QualType WrappedTy,
                                                  Expr *CountExpr,
                                                  bool CountInBytes,
                                                  bool OrNull);

  /// BuildAddressSpaceAttr - Builds a DependentAddressSpaceType if an
  /// expression is uninstantiated. If instantiated it will apply the
  /// appropriate address space to the type. This function allows dependent
  /// template variables to be used in conjunction with the address_space
  /// attribute
  QualType BuildAddressSpaceAttr(QualType &T, LangAS ASIdx, Expr *AddrSpace,
                                 SourceLocation AttrLoc);

  /// Same as above, but constructs the AddressSpace index if not provided.
  QualType BuildAddressSpaceAttr(QualType &T, Expr *AddrSpace,
                                 SourceLocation AttrLoc);

  bool CheckQualifiedFunctionForTypeId(QualType T, SourceLocation Loc);

  bool CheckFunctionReturnType(QualType T, SourceLocation Loc);

  /// Build a function type.
  ///
  /// This routine checks the function type according to C++ rules and
  /// under the assumption that the result type and parameter types have
  /// just been instantiated from a template. It therefore duplicates
  /// some of the behavior of GetTypeForDeclarator, but in a much
  /// simpler form that is only suitable for this narrow use case.
  ///
  /// \param T The return type of the function.
  ///
  /// \param ParamTypes The parameter types of the function. This array
  /// will be modified to account for adjustments to the types of the
  /// function parameters.
  ///
  /// \param Loc The location of the entity whose type involves this
  /// function type or, if there is no such entity, the location of the
  /// type that will have function type.
  ///
  /// \param Entity The name of the entity that involves the function
  /// type, if known.
  ///
  /// \param EPI Extra information about the function type. Usually this will
  /// be taken from an existing function with the same prototype.
  ///
  /// \returns A suitable function type, if there are no errors. The
  /// unqualified type will always be a FunctionProtoType.
  /// Otherwise, returns a NULL type.
  QualType BuildFunctionType(QualType T, MutableArrayRef<QualType> ParamTypes,
                             SourceLocation Loc, DeclarationName Entity,
                             const FunctionProtoType::ExtProtoInfo &EPI);

  /// Build a member pointer type \c T Class::*.
  ///
  /// \param T the type to which the member pointer refers.
  /// \param Class the class type into which the member pointer points.
  /// \param Loc the location where this type begins
  /// \param Entity the name of the entity that will have this member pointer
  /// type
  ///
  /// \returns a member pointer type, if successful, or a NULL type if there was
  /// an error.
  QualType BuildMemberPointerType(QualType T, QualType Class,
                                  SourceLocation Loc, DeclarationName Entity);

  /// Build a block pointer type.
  ///
  /// \param T The type to which we'll be building a block pointer.
  ///
  /// \param Loc The source location, used for diagnostics.
  ///
  /// \param Entity The name of the entity that involves the block pointer
  /// type, if known.
  ///
  /// \returns A suitable block pointer type, if there are no
  /// errors. Otherwise, returns a NULL type.
  QualType BuildBlockPointerType(QualType T, SourceLocation Loc,
                                 DeclarationName Entity);

  /// Build a paren type including \p T.
  QualType BuildParenType(QualType T);
  QualType BuildAtomicType(QualType T, SourceLocation Loc);

  /// Build a Read-only Pipe type.
  ///
  /// \param T The type to which we'll be building a Pipe.
  ///
  /// \param Loc We do not use it for now.
  ///
  /// \returns A suitable pipe type, if there are no errors. Otherwise, returns
  /// a NULL type.
  QualType BuildReadPipeType(QualType T, SourceLocation Loc);

  /// Build a Write-only Pipe type.
  ///
  /// \param T The type to which we'll be building a Pipe.
  ///
  /// \param Loc We do not use it for now.
  ///
  /// \returns A suitable pipe type, if there are no errors. Otherwise, returns
  /// a NULL type.
  QualType BuildWritePipeType(QualType T, SourceLocation Loc);

  /// Build a bit-precise integer type.
  ///
  /// \param IsUnsigned Boolean representing the signedness of the type.
  ///
  /// \param BitWidth Size of this int type in bits, or an expression
  /// representing that.
  ///
  /// \param Loc Location of the keyword.
  QualType BuildBitIntType(bool IsUnsigned, Expr *BitWidth, SourceLocation Loc);

  /// GetTypeForDeclarator - Convert the type for the specified
  /// declarator to Type instances.
  ///
  /// The result of this call will never be null, but the associated
  /// type may be a null type if there's an unrecoverable error.
  TypeSourceInfo *GetTypeForDeclarator(Declarator &D);
  TypeSourceInfo *GetTypeForDeclaratorCast(Declarator &D, QualType FromTy);

  /// Package the given type and TSI into a ParsedType.
  ParsedType CreateParsedType(QualType T, TypeSourceInfo *TInfo);
  static QualType GetTypeFromParser(ParsedType Ty,
                                    TypeSourceInfo **TInfo = nullptr);

  TypeResult ActOnTypeName(Declarator &D);

  // Check whether the size of array element of type \p EltTy is a multiple of
  // its alignment and return false if it isn't.
  bool checkArrayElementAlignment(QualType EltTy, SourceLocation Loc);

  void
  diagnoseIgnoredQualifiers(unsigned DiagID, unsigned Quals,
                            SourceLocation FallbackLoc,
                            SourceLocation ConstQualLoc = SourceLocation(),
                            SourceLocation VolatileQualLoc = SourceLocation(),
                            SourceLocation RestrictQualLoc = SourceLocation(),
                            SourceLocation AtomicQualLoc = SourceLocation(),
                            SourceLocation UnalignedQualLoc = SourceLocation());

  /// Retrieve the keyword associated
  IdentifierInfo *getNullabilityKeyword(NullabilityKind nullability);

  /// Adjust the calling convention of a method to be the ABI default if it
  /// wasn't specified explicitly.  This handles method types formed from
  /// function type typedefs and typename template arguments.
  void adjustMemberFunctionCC(QualType &T, bool HasThisPointer,
                              bool IsCtorOrDtor, SourceLocation Loc);

  // Check if there is an explicit attribute, but only look through parens.
  // The intent is to look for an attribute on the current declarator, but not
  // one that came from a typedef.
  bool hasExplicitCallingConv(QualType T);

  /// Check whether a nullability type specifier can be added to the given
  /// type through some means not written in source (e.g. API notes).
  ///
  /// \param Type The type to which the nullability specifier will be
  /// added. On success, this type will be updated appropriately.
  ///
  /// \param Nullability The nullability specifier to add.
  ///
  /// \param DiagLoc The location to use for diagnostics.
  ///
  /// \param AllowArrayTypes Whether to accept nullability specifiers on an
  /// array type (e.g., because it will decay to a pointer).
  ///
  /// \param OverrideExisting Whether to override an existing, locally-specified
  /// nullability specifier rather than complaining about the conflict.
  ///
  /// \returns true if nullability cannot be applied, false otherwise.
  bool CheckImplicitNullabilityTypeSpecifier(QualType &Type,
                                             NullabilityKind Nullability,
                                             SourceLocation DiagLoc,
                                             bool AllowArrayTypes,
                                             bool OverrideExisting);

  /// Get the type of expression E, triggering instantiation to complete the
  /// type if necessary -- that is, if the expression refers to a templated
  /// static data member of incomplete array type.
  ///
  /// May still return an incomplete type if instantiation was not possible or
  /// if the type is incomplete for a different reason. Use
  /// RequireCompleteExprType instead if a diagnostic is expected for an
  /// incomplete expression type.
  QualType getCompletedType(Expr *E);

  void completeExprArrayBound(Expr *E);

  /// Ensure that the type of the given expression is complete.
  ///
  /// This routine checks whether the expression \p E has a complete type. If
  /// the expression refers to an instantiable construct, that instantiation is
  /// performed as needed to complete its type. Furthermore
  /// Sema::RequireCompleteType is called for the expression's type (or in the
  /// case of a reference type, the referred-to type).
  ///
  /// \param E The expression whose type is required to be complete.
  /// \param Kind Selects which completeness rules should be applied.
  /// \param Diagnoser The object that will emit a diagnostic if the type is
  /// incomplete.
  ///
  /// \returns \c true if the type of \p E is incomplete and diagnosed, \c false
  /// otherwise.
  bool RequireCompleteExprType(Expr *E, CompleteTypeKind Kind,
                               TypeDiagnoser &Diagnoser);
  bool RequireCompleteExprType(Expr *E, unsigned DiagID);

  template <typename... Ts>
  bool RequireCompleteExprType(Expr *E, unsigned DiagID, const Ts &...Args) {
    BoundTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteExprType(E, CompleteTypeKind::Default, Diagnoser);
  }

  /// Retrieve a version of the type 'T' that is elaborated by Keyword,
  /// qualified by the nested-name-specifier contained in SS, and that is
  /// (re)declared by OwnedTagDecl, which is nullptr if this is not a
  /// (re)declaration.
  QualType getElaboratedType(ElaboratedTypeKeyword Keyword,
                             const CXXScopeSpec &SS, QualType T,
                             TagDecl *OwnedTagDecl = nullptr);

  // Returns the underlying type of a decltype with the given expression.
  QualType getDecltypeForExpr(Expr *E);

  QualType BuildTypeofExprType(Expr *E, TypeOfKind Kind);
  /// If AsUnevaluated is false, E is treated as though it were an evaluated
  /// context, such as when building a type for decltype(auto).
  QualType BuildDecltypeType(Expr *E, bool AsUnevaluated = true);

  QualType ActOnPackIndexingType(QualType Pattern, Expr *IndexExpr,
                                 SourceLocation Loc,
                                 SourceLocation EllipsisLoc);
  QualType BuildPackIndexingType(QualType Pattern, Expr *IndexExpr,
                                 SourceLocation Loc, SourceLocation EllipsisLoc,
                                 bool FullySubstituted = false,
                                 ArrayRef<QualType> Expansions = {});

  using UTTKind = UnaryTransformType::UTTKind;
  QualType BuildUnaryTransformType(QualType BaseType, UTTKind UKind,
                                   SourceLocation Loc);
  QualType BuiltinEnumUnderlyingType(QualType BaseType, SourceLocation Loc);
  QualType BuiltinAddPointer(QualType BaseType, SourceLocation Loc);
  QualType BuiltinRemovePointer(QualType BaseType, SourceLocation Loc);
  QualType BuiltinDecay(QualType BaseType, SourceLocation Loc);
  QualType BuiltinAddReference(QualType BaseType, UTTKind UKind,
                               SourceLocation Loc);
  QualType BuiltinRemoveExtent(QualType BaseType, UTTKind UKind,
                               SourceLocation Loc);
  QualType BuiltinRemoveReference(QualType BaseType, UTTKind UKind,
                                  SourceLocation Loc);
  QualType BuiltinChangeCVRQualifiers(QualType BaseType, UTTKind UKind,
                                      SourceLocation Loc);
  QualType BuiltinChangeSignedness(QualType BaseType, UTTKind UKind,
                                   SourceLocation Loc);

  /// Ensure that the type T is a literal type.
  ///
  /// This routine checks whether the type @p T is a literal type. If @p T is an
  /// incomplete type, an attempt is made to complete it. If @p T is a literal
  /// type, or @p AllowIncompleteType is true and @p T is an incomplete type,
  /// returns false. Otherwise, this routine issues the diagnostic @p PD (giving
  /// it the type @p T), along with notes explaining why the type is not a
  /// literal type, and returns true.
  ///
  /// @param Loc  The location in the source that the non-literal type
  /// diagnostic should refer to.
  ///
  /// @param T  The type that this routine is examining for literalness.
  ///
  /// @param Diagnoser Emits a diagnostic if T is not a literal type.
  ///
  /// @returns @c true if @p T is not a literal type and a diagnostic was
  /// emitted, @c false otherwise.
  bool RequireLiteralType(SourceLocation Loc, QualType T,
                          TypeDiagnoser &Diagnoser);
  bool RequireLiteralType(SourceLocation Loc, QualType T, unsigned DiagID);

  template <typename... Ts>
  bool RequireLiteralType(SourceLocation Loc, QualType T, unsigned DiagID,
                          const Ts &...Args) {
    BoundTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireLiteralType(Loc, T, Diagnoser);
  }

  bool isCompleteType(SourceLocation Loc, QualType T,
                      CompleteTypeKind Kind = CompleteTypeKind::Default) {
    return !RequireCompleteTypeImpl(Loc, T, Kind, nullptr);
  }

  /// Ensure that the type T is a complete type.
  ///
  /// This routine checks whether the type @p T is complete in any
  /// context where a complete type is required. If @p T is a complete
  /// type, returns false. If @p T is a class template specialization,
  /// this routine then attempts to perform class template
  /// instantiation. If instantiation fails, or if @p T is incomplete
  /// and cannot be completed, issues the diagnostic @p diag (giving it
  /// the type @p T) and returns true.
  ///
  /// @param Loc  The location in the source that the incomplete type
  /// diagnostic should refer to.
  ///
  /// @param T  The type that this routine is examining for completeness.
  ///
  /// @param Kind Selects which completeness rules should be applied.
  ///
  /// @returns @c true if @p T is incomplete and a diagnostic was emitted,
  /// @c false otherwise.
  bool RequireCompleteType(SourceLocation Loc, QualType T,
                           CompleteTypeKind Kind, TypeDiagnoser &Diagnoser);
  bool RequireCompleteType(SourceLocation Loc, QualType T,
                           CompleteTypeKind Kind, unsigned DiagID);

  bool RequireCompleteType(SourceLocation Loc, QualType T,
                           TypeDiagnoser &Diagnoser) {
    return RequireCompleteType(Loc, T, CompleteTypeKind::Default, Diagnoser);
  }
  bool RequireCompleteType(SourceLocation Loc, QualType T, unsigned DiagID) {
    return RequireCompleteType(Loc, T, CompleteTypeKind::Default, DiagID);
  }

  template <typename... Ts>
  bool RequireCompleteType(SourceLocation Loc, QualType T, unsigned DiagID,
                           const Ts &...Args) {
    BoundTypeDiagnoser<Ts...> Diagnoser(DiagID, Args...);
    return RequireCompleteType(Loc, T, Diagnoser);
  }

  /// Determine whether a declaration is visible to name lookup.
  bool isVisible(const NamedDecl *D) {
    return D->isUnconditionallyVisible() ||
           isAcceptableSlow(D, AcceptableKind::Visible);
  }

  /// Determine whether a declaration is reachable.
  bool isReachable(const NamedDecl *D) {
    // All visible declarations are reachable.
    return D->isUnconditionallyVisible() ||
           isAcceptableSlow(D, AcceptableKind::Reachable);
  }

  /// Determine whether a declaration is acceptable (visible/reachable).
  bool isAcceptable(const NamedDecl *D, AcceptableKind Kind) {
    return Kind == AcceptableKind::Visible ? isVisible(D) : isReachable(D);
  }

  /// Determine if \p D and \p Suggested have a structurally compatible
  /// layout as described in C11 6.2.7/1.
  bool hasStructuralCompatLayout(Decl *D, Decl *Suggested);

  /// Determine if \p D has a visible definition. If not, suggest a declaration
  /// that should be made visible to expose the definition.
  bool hasVisibleDefinition(NamedDecl *D, NamedDecl **Suggested,
                            bool OnlyNeedComplete = false);
  bool hasVisibleDefinition(const NamedDecl *D) {
    NamedDecl *Hidden;
    return hasVisibleDefinition(const_cast<NamedDecl *>(D), &Hidden);
  }

  /// Determine if \p D has a reachable definition. If not, suggest a
  /// declaration that should be made reachable to expose the definition.
  bool hasReachableDefinition(NamedDecl *D, NamedDecl **Suggested,
                              bool OnlyNeedComplete = false);
  bool hasReachableDefinition(NamedDecl *D) {
    NamedDecl *Hidden;
    return hasReachableDefinition(D, &Hidden);
  }

  bool hasAcceptableDefinition(NamedDecl *D, NamedDecl **Suggested,
                               AcceptableKind Kind,
                               bool OnlyNeedComplete = false);
  bool hasAcceptableDefinition(NamedDecl *D, AcceptableKind Kind) {
    NamedDecl *Hidden;
    return hasAcceptableDefinition(D, &Hidden, Kind);
  }

private:
  /// The implementation of RequireCompleteType
  bool RequireCompleteTypeImpl(SourceLocation Loc, QualType T,
                               CompleteTypeKind Kind, TypeDiagnoser *Diagnoser);

  /// Nullability type specifiers.
  IdentifierInfo *Ident__Nonnull = nullptr;
  IdentifierInfo *Ident__Nullable = nullptr;
  IdentifierInfo *Ident__Nullable_result = nullptr;
  IdentifierInfo *Ident__Null_unspecified = nullptr;

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name FixIt Helpers
  /// Implementations are in SemaFixItUtils.cpp
  ///@{

public:
  /// Get a string to suggest for zero-initialization of a type.
  std::string getFixItZeroInitializerForType(QualType T,
                                             SourceLocation Loc) const;
  std::string getFixItZeroLiteralForType(QualType T, SourceLocation Loc) const;

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name API Notes
  /// Implementations are in SemaAPINotes.cpp
  ///@{

public:
  /// Map any API notes provided for this declaration to attributes on the
  /// declaration.
  ///
  /// Triggered by declaration-attribute processing.
  void ProcessAPINotes(Decl *D);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name Bounds Safety
  /// Implementations are in SemaBoundsSafety.cpp
  ///@{
public:
  /// Check if applying the specified attribute variant from the "counted by"
  /// family of attributes to FieldDecl \p FD is semantically valid. If
  /// semantically invalid diagnostics will be emitted explaining the problems.
  ///
  /// \param FD The FieldDecl to apply the attribute to
  /// \param E The count expression on the attribute
  /// \param[out] Decls If the attribute is semantically valid \p Decls
  ///             is populated with TypeCoupledDeclRefInfo objects, each
  ///             describing Decls referred to in \p E.
  /// \param CountInBytes If true the attribute is from the "sized_by" family of
  ///                     attributes. If the false the attribute is from
  ///                     "counted_by" family of attributes.
  /// \param OrNull If true the attribute is from the "_or_null" suffixed family
  ///               of attributes. If false the attribute does not have the
  ///               suffix.
  ///
  /// Together \p CountInBytes and \p OrNull decide the attribute variant. E.g.
  /// \p CountInBytes and \p OrNull both being true indicates the
  /// `counted_by_or_null` attribute.
  ///
  /// \returns false iff semantically valid.
  bool CheckCountedByAttrOnField(
      FieldDecl *FD, Expr *E,
      llvm::SmallVectorImpl<TypeCoupledDeclRefInfo> &Decls, bool CountInBytes,
      bool OrNull);

  ///@}
};

DeductionFailureInfo
MakeDeductionFailureInfo(ASTContext &Context, TemplateDeductionResult TDK,
                         sema::TemplateDeductionInfo &Info);

/// Contains a late templated function.
/// Will be parsed at the end of the translation unit, used by Sema & Parser.
struct LateParsedTemplate {
  CachedTokens Toks;
  /// The template function declaration to be late parsed.
  Decl *D;
  /// Floating-point options in the point of definition.
  FPOptions FPO;
};

template <>
void Sema::PragmaStack<Sema::AlignPackInfo>::Act(SourceLocation PragmaLocation,
                                                 PragmaMsStackAction Action,
                                                 llvm::StringRef StackSlotLabel,
                                                 AlignPackInfo Value);
} // end namespace clang

#endif
