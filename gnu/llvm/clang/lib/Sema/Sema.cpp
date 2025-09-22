//===--- Sema.cpp - AST Builder and Semantic Analysis Implementation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the actions class which performs semantic analysis and
// builds an AST out of a parse stream.
//
//===----------------------------------------------------------------------===//

#include "UsedDeclVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/DarwinSDKInfo.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Stack.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/CXXFieldCollector.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/ExternalSemaSource.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/RISCVIntrinsicManager.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaAMDGPU.h"
#include "clang/Sema/SemaARM.h"
#include "clang/Sema/SemaAVR.h"
#include "clang/Sema/SemaBPF.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Sema/SemaHLSL.h"
#include "clang/Sema/SemaHexagon.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaLoongArch.h"
#include "clang/Sema/SemaM68k.h"
#include "clang/Sema/SemaMIPS.h"
#include "clang/Sema/SemaMSP430.h"
#include "clang/Sema/SemaNVPTX.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenACC.h"
#include "clang/Sema/SemaOpenCL.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/SemaPPC.h"
#include "clang/Sema/SemaPseudoObject.h"
#include "clang/Sema/SemaRISCV.h"
#include "clang/Sema/SemaSYCL.h"
#include "clang/Sema/SemaSwift.h"
#include "clang/Sema/SemaSystemZ.h"
#include "clang/Sema/SemaWasm.h"
#include "clang/Sema/SemaX86.h"
#include "clang/Sema/TemplateDeduction.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "clang/Sema/TypoCorrection.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/TimeProfiler.h"
#include <optional>

using namespace clang;
using namespace sema;

SourceLocation Sema::getLocForEndOfToken(SourceLocation Loc, unsigned Offset) {
  return Lexer::getLocForEndOfToken(Loc, Offset, SourceMgr, LangOpts);
}

ModuleLoader &Sema::getModuleLoader() const { return PP.getModuleLoader(); }

DarwinSDKInfo *
Sema::getDarwinSDKInfoForAvailabilityChecking(SourceLocation Loc,
                                              StringRef Platform) {
  auto *SDKInfo = getDarwinSDKInfoForAvailabilityChecking();
  if (!SDKInfo && !WarnedDarwinSDKInfoMissing) {
    Diag(Loc, diag::warn_missing_sdksettings_for_availability_checking)
        << Platform;
    WarnedDarwinSDKInfoMissing = true;
  }
  return SDKInfo;
}

DarwinSDKInfo *Sema::getDarwinSDKInfoForAvailabilityChecking() {
  if (CachedDarwinSDKInfo)
    return CachedDarwinSDKInfo->get();
  auto SDKInfo = parseDarwinSDKInfo(
      PP.getFileManager().getVirtualFileSystem(),
      PP.getHeaderSearchInfo().getHeaderSearchOpts().Sysroot);
  if (SDKInfo && *SDKInfo) {
    CachedDarwinSDKInfo = std::make_unique<DarwinSDKInfo>(std::move(**SDKInfo));
    return CachedDarwinSDKInfo->get();
  }
  if (!SDKInfo)
    llvm::consumeError(SDKInfo.takeError());
  CachedDarwinSDKInfo = std::unique_ptr<DarwinSDKInfo>();
  return nullptr;
}

IdentifierInfo *Sema::InventAbbreviatedTemplateParameterTypeName(
    const IdentifierInfo *ParamName, unsigned int Index) {
  std::string InventedName;
  llvm::raw_string_ostream OS(InventedName);

  if (!ParamName)
    OS << "auto:" << Index + 1;
  else
    OS << ParamName->getName() << ":auto";

  OS.flush();
  return &Context.Idents.get(OS.str());
}

PrintingPolicy Sema::getPrintingPolicy(const ASTContext &Context,
                                       const Preprocessor &PP) {
  PrintingPolicy Policy = Context.getPrintingPolicy();
  // In diagnostics, we print _Bool as bool if the latter is defined as the
  // former.
  Policy.Bool = Context.getLangOpts().Bool;
  if (!Policy.Bool) {
    if (const MacroInfo *BoolMacro = PP.getMacroInfo(Context.getBoolName())) {
      Policy.Bool = BoolMacro->isObjectLike() &&
                    BoolMacro->getNumTokens() == 1 &&
                    BoolMacro->getReplacementToken(0).is(tok::kw__Bool);
    }
  }

  // Shorten the data output if needed
  Policy.EntireContentsOfLargeArray = false;

  return Policy;
}

void Sema::ActOnTranslationUnitScope(Scope *S) {
  TUScope = S;
  PushDeclContext(S, Context.getTranslationUnitDecl());
}

namespace clang {
namespace sema {

class SemaPPCallbacks : public PPCallbacks {
  Sema *S = nullptr;
  llvm::SmallVector<SourceLocation, 8> IncludeStack;
  llvm::SmallVector<llvm::TimeTraceProfilerEntry *, 8> ProfilerStack;

public:
  void set(Sema &S) { this->S = &S; }

  void reset() { S = nullptr; }

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID) override {
    if (!S)
      return;
    switch (Reason) {
    case EnterFile: {
      SourceManager &SM = S->getSourceManager();
      SourceLocation IncludeLoc = SM.getIncludeLoc(SM.getFileID(Loc));
      if (IncludeLoc.isValid()) {
        if (llvm::timeTraceProfilerEnabled()) {
          OptionalFileEntryRef FE = SM.getFileEntryRefForID(SM.getFileID(Loc));
          ProfilerStack.push_back(llvm::timeTraceAsyncProfilerBegin(
              "Source", FE ? FE->getName() : StringRef("<unknown>")));
        }

        IncludeStack.push_back(IncludeLoc);
        S->DiagnoseNonDefaultPragmaAlignPack(
            Sema::PragmaAlignPackDiagnoseKind::NonDefaultStateAtInclude,
            IncludeLoc);
      }
      break;
    }
    case ExitFile:
      if (!IncludeStack.empty()) {
        if (llvm::timeTraceProfilerEnabled())
          llvm::timeTraceProfilerEnd(ProfilerStack.pop_back_val());

        S->DiagnoseNonDefaultPragmaAlignPack(
            Sema::PragmaAlignPackDiagnoseKind::ChangedStateAtExit,
            IncludeStack.pop_back_val());
      }
      break;
    default:
      break;
    }
  }
};

} // end namespace sema
} // end namespace clang

const unsigned Sema::MaxAlignmentExponent;
const uint64_t Sema::MaximumAlignment;

Sema::Sema(Preprocessor &pp, ASTContext &ctxt, ASTConsumer &consumer,
           TranslationUnitKind TUKind, CodeCompleteConsumer *CodeCompleter)
    : SemaBase(*this), CollectStats(false), TUKind(TUKind),
      CurFPFeatures(pp.getLangOpts()), LangOpts(pp.getLangOpts()), PP(pp),
      Context(ctxt), Consumer(consumer), Diags(PP.getDiagnostics()),
      SourceMgr(PP.getSourceManager()), APINotes(SourceMgr, LangOpts),
      AnalysisWarnings(*this), ThreadSafetyDeclCache(nullptr),
      LateTemplateParser(nullptr), LateTemplateParserCleanup(nullptr),
      OpaqueParser(nullptr), CurContext(nullptr), ExternalSource(nullptr),
      CurScope(nullptr), Ident_super(nullptr),
      AMDGPUPtr(std::make_unique<SemaAMDGPU>(*this)),
      ARMPtr(std::make_unique<SemaARM>(*this)),
      AVRPtr(std::make_unique<SemaAVR>(*this)),
      BPFPtr(std::make_unique<SemaBPF>(*this)),
      CodeCompletionPtr(
          std::make_unique<SemaCodeCompletion>(*this, CodeCompleter)),
      CUDAPtr(std::make_unique<SemaCUDA>(*this)),
      HLSLPtr(std::make_unique<SemaHLSL>(*this)),
      HexagonPtr(std::make_unique<SemaHexagon>(*this)),
      LoongArchPtr(std::make_unique<SemaLoongArch>(*this)),
      M68kPtr(std::make_unique<SemaM68k>(*this)),
      MIPSPtr(std::make_unique<SemaMIPS>(*this)),
      MSP430Ptr(std::make_unique<SemaMSP430>(*this)),
      NVPTXPtr(std::make_unique<SemaNVPTX>(*this)),
      ObjCPtr(std::make_unique<SemaObjC>(*this)),
      OpenACCPtr(std::make_unique<SemaOpenACC>(*this)),
      OpenCLPtr(std::make_unique<SemaOpenCL>(*this)),
      OpenMPPtr(std::make_unique<SemaOpenMP>(*this)),
      PPCPtr(std::make_unique<SemaPPC>(*this)),
      PseudoObjectPtr(std::make_unique<SemaPseudoObject>(*this)),
      RISCVPtr(std::make_unique<SemaRISCV>(*this)),
      SYCLPtr(std::make_unique<SemaSYCL>(*this)),
      SwiftPtr(std::make_unique<SemaSwift>(*this)),
      SystemZPtr(std::make_unique<SemaSystemZ>(*this)),
      WasmPtr(std::make_unique<SemaWasm>(*this)),
      X86Ptr(std::make_unique<SemaX86>(*this)),
      MSPointerToMemberRepresentationMethod(
          LangOpts.getMSPointerToMemberRepresentationMethod()),
      MSStructPragmaOn(false), VtorDispStack(LangOpts.getVtorDispMode()),
      AlignPackStack(AlignPackInfo(getLangOpts().XLPragmaPack)),
      DataSegStack(nullptr), BSSSegStack(nullptr), ConstSegStack(nullptr),
      CodeSegStack(nullptr), StrictGuardStackCheckStack(false),
      FpPragmaStack(FPOptionsOverride()), CurInitSeg(nullptr),
      VisContext(nullptr), PragmaAttributeCurrentTargetDecl(nullptr),
      StdCoroutineTraitsCache(nullptr), IdResolver(pp),
      OriginalLexicalContext(nullptr), StdInitializerList(nullptr),
      FullyCheckedComparisonCategories(
          static_cast<unsigned>(ComparisonCategoryType::Last) + 1),
      StdSourceLocationImplDecl(nullptr), CXXTypeInfoDecl(nullptr),
      GlobalNewDeleteDeclared(false), DisableTypoCorrection(false),
      TyposCorrected(0), IsBuildingRecoveryCallExpr(false), NumSFINAEErrors(0),
      AccessCheckingSFINAE(false), CurrentInstantiationScope(nullptr),
      InNonInstantiationSFINAEContext(false), NonInstantiationEntries(0),
      ArgumentPackSubstitutionIndex(-1), SatisfactionCache(Context) {
  assert(pp.TUKind == TUKind);
  TUScope = nullptr;

  LoadedExternalKnownNamespaces = false;
  for (unsigned I = 0; I != NSAPI::NumNSNumberLiteralMethods; ++I)
    ObjC().NSNumberLiteralMethods[I] = nullptr;

  if (getLangOpts().ObjC)
    ObjC().NSAPIObj.reset(new NSAPI(Context));

  if (getLangOpts().CPlusPlus)
    FieldCollector.reset(new CXXFieldCollector());

  // Tell diagnostics how to render things from the AST library.
  Diags.SetArgToStringFn(&FormatASTNodeDiagnosticArgument, &Context);

  // This evaluation context exists to ensure that there's always at least one
  // valid evaluation context available. It is never removed from the
  // evaluation stack.
  ExprEvalContexts.emplace_back(
      ExpressionEvaluationContext::PotentiallyEvaluated, 0, CleanupInfo{},
      nullptr, ExpressionEvaluationContextRecord::EK_Other);

  // Initialization of data sharing attributes stack for OpenMP
  OpenMP().InitDataSharingAttributesStack();

  std::unique_ptr<sema::SemaPPCallbacks> Callbacks =
      std::make_unique<sema::SemaPPCallbacks>();
  SemaPPCallbackHandler = Callbacks.get();
  PP.addPPCallbacks(std::move(Callbacks));
  SemaPPCallbackHandler->set(*this);

  CurFPFeatures.setFPEvalMethod(PP.getCurrentFPEvalMethod());
}

// Anchor Sema's type info to this TU.
void Sema::anchor() {}

void Sema::addImplicitTypedef(StringRef Name, QualType T) {
  DeclarationName DN = &Context.Idents.get(Name);
  if (IdResolver.begin(DN) == IdResolver.end())
    PushOnScopeChains(Context.buildImplicitTypedef(T, Name), TUScope);
}

void Sema::Initialize() {
  if (SemaConsumer *SC = dyn_cast<SemaConsumer>(&Consumer))
    SC->InitializeSema(*this);

  // Tell the external Sema source about this Sema object.
  if (ExternalSemaSource *ExternalSema
      = dyn_cast_or_null<ExternalSemaSource>(Context.getExternalSource()))
    ExternalSema->InitializeSema(*this);

  // This needs to happen after ExternalSemaSource::InitializeSema(this) or we
  // will not be able to merge any duplicate __va_list_tag decls correctly.
  VAListTagName = PP.getIdentifierInfo("__va_list_tag");

  if (!TUScope)
    return;

  // Initialize predefined 128-bit integer types, if needed.
  if (Context.getTargetInfo().hasInt128Type() ||
      (Context.getAuxTargetInfo() &&
       Context.getAuxTargetInfo()->hasInt128Type())) {
    // If either of the 128-bit integer types are unavailable to name lookup,
    // define them now.
    DeclarationName Int128 = &Context.Idents.get("__int128_t");
    if (IdResolver.begin(Int128) == IdResolver.end())
      PushOnScopeChains(Context.getInt128Decl(), TUScope);

    DeclarationName UInt128 = &Context.Idents.get("__uint128_t");
    if (IdResolver.begin(UInt128) == IdResolver.end())
      PushOnScopeChains(Context.getUInt128Decl(), TUScope);
  }


  // Initialize predefined Objective-C types:
  if (getLangOpts().ObjC) {
    // If 'SEL' does not yet refer to any declarations, make it refer to the
    // predefined 'SEL'.
    DeclarationName SEL = &Context.Idents.get("SEL");
    if (IdResolver.begin(SEL) == IdResolver.end())
      PushOnScopeChains(Context.getObjCSelDecl(), TUScope);

    // If 'id' does not yet refer to any declarations, make it refer to the
    // predefined 'id'.
    DeclarationName Id = &Context.Idents.get("id");
    if (IdResolver.begin(Id) == IdResolver.end())
      PushOnScopeChains(Context.getObjCIdDecl(), TUScope);

    // Create the built-in typedef for 'Class'.
    DeclarationName Class = &Context.Idents.get("Class");
    if (IdResolver.begin(Class) == IdResolver.end())
      PushOnScopeChains(Context.getObjCClassDecl(), TUScope);

    // Create the built-in forward declaratino for 'Protocol'.
    DeclarationName Protocol = &Context.Idents.get("Protocol");
    if (IdResolver.begin(Protocol) == IdResolver.end())
      PushOnScopeChains(Context.getObjCProtocolDecl(), TUScope);
  }

  // Create the internal type for the *StringMakeConstantString builtins.
  DeclarationName ConstantString = &Context.Idents.get("__NSConstantString");
  if (IdResolver.begin(ConstantString) == IdResolver.end())
    PushOnScopeChains(Context.getCFConstantStringDecl(), TUScope);

  // Initialize Microsoft "predefined C++ types".
  if (getLangOpts().MSVCCompat) {
    if (getLangOpts().CPlusPlus &&
        IdResolver.begin(&Context.Idents.get("type_info")) == IdResolver.end())
      PushOnScopeChains(
          Context.buildImplicitRecord("type_info", TagTypeKind::Class),
          TUScope);

    addImplicitTypedef("size_t", Context.getSizeType());
  }

  // Initialize predefined OpenCL types and supported extensions and (optional)
  // core features.
  if (getLangOpts().OpenCL) {
    getOpenCLOptions().addSupport(
        Context.getTargetInfo().getSupportedOpenCLOpts(), getLangOpts());
    addImplicitTypedef("sampler_t", Context.OCLSamplerTy);
    addImplicitTypedef("event_t", Context.OCLEventTy);
    auto OCLCompatibleVersion = getLangOpts().getOpenCLCompatibleVersion();
    if (OCLCompatibleVersion >= 200) {
      if (getLangOpts().OpenCLCPlusPlus || getLangOpts().Blocks) {
        addImplicitTypedef("clk_event_t", Context.OCLClkEventTy);
        addImplicitTypedef("queue_t", Context.OCLQueueTy);
      }
      if (getLangOpts().OpenCLPipes)
        addImplicitTypedef("reserve_id_t", Context.OCLReserveIDTy);
      addImplicitTypedef("atomic_int", Context.getAtomicType(Context.IntTy));
      addImplicitTypedef("atomic_uint",
                         Context.getAtomicType(Context.UnsignedIntTy));
      addImplicitTypedef("atomic_float",
                         Context.getAtomicType(Context.FloatTy));
      // OpenCLC v2.0, s6.13.11.6 requires that atomic_flag is implemented as
      // 32-bit integer and OpenCLC v2.0, s6.1.1 int is always 32-bit wide.
      addImplicitTypedef("atomic_flag", Context.getAtomicType(Context.IntTy));


      // OpenCL v2.0 s6.13.11.6:
      // - The atomic_long and atomic_ulong types are supported if the
      //   cl_khr_int64_base_atomics and cl_khr_int64_extended_atomics
      //   extensions are supported.
      // - The atomic_double type is only supported if double precision
      //   is supported and the cl_khr_int64_base_atomics and
      //   cl_khr_int64_extended_atomics extensions are supported.
      // - If the device address space is 64-bits, the data types
      //   atomic_intptr_t, atomic_uintptr_t, atomic_size_t and
      //   atomic_ptrdiff_t are supported if the cl_khr_int64_base_atomics and
      //   cl_khr_int64_extended_atomics extensions are supported.

      auto AddPointerSizeDependentTypes = [&]() {
        auto AtomicSizeT = Context.getAtomicType(Context.getSizeType());
        auto AtomicIntPtrT = Context.getAtomicType(Context.getIntPtrType());
        auto AtomicUIntPtrT = Context.getAtomicType(Context.getUIntPtrType());
        auto AtomicPtrDiffT =
            Context.getAtomicType(Context.getPointerDiffType());
        addImplicitTypedef("atomic_size_t", AtomicSizeT);
        addImplicitTypedef("atomic_intptr_t", AtomicIntPtrT);
        addImplicitTypedef("atomic_uintptr_t", AtomicUIntPtrT);
        addImplicitTypedef("atomic_ptrdiff_t", AtomicPtrDiffT);
      };

      if (Context.getTypeSize(Context.getSizeType()) == 32) {
        AddPointerSizeDependentTypes();
      }

      if (getOpenCLOptions().isSupported("cl_khr_fp16", getLangOpts())) {
        auto AtomicHalfT = Context.getAtomicType(Context.HalfTy);
        addImplicitTypedef("atomic_half", AtomicHalfT);
      }

      std::vector<QualType> Atomic64BitTypes;
      if (getOpenCLOptions().isSupported("cl_khr_int64_base_atomics",
                                         getLangOpts()) &&
          getOpenCLOptions().isSupported("cl_khr_int64_extended_atomics",
                                         getLangOpts())) {
        if (getOpenCLOptions().isSupported("cl_khr_fp64", getLangOpts())) {
          auto AtomicDoubleT = Context.getAtomicType(Context.DoubleTy);
          addImplicitTypedef("atomic_double", AtomicDoubleT);
          Atomic64BitTypes.push_back(AtomicDoubleT);
        }
        auto AtomicLongT = Context.getAtomicType(Context.LongTy);
        auto AtomicULongT = Context.getAtomicType(Context.UnsignedLongTy);
        addImplicitTypedef("atomic_long", AtomicLongT);
        addImplicitTypedef("atomic_ulong", AtomicULongT);


        if (Context.getTypeSize(Context.getSizeType()) == 64) {
          AddPointerSizeDependentTypes();
        }
      }
    }

#define EXT_OPAQUE_TYPE(ExtType, Id, Ext)                                      \
  if (getOpenCLOptions().isSupported(#Ext, getLangOpts())) {                   \
    addImplicitTypedef(#ExtType, Context.Id##Ty);                              \
  }
#include "clang/Basic/OpenCLExtensionTypes.def"
  }

  if (Context.getTargetInfo().hasAArch64SVETypes() ||
      (Context.getAuxTargetInfo() &&
       Context.getAuxTargetInfo()->hasAArch64SVETypes())) {
#define SVE_TYPE(Name, Id, SingletonId) \
    addImplicitTypedef(Name, Context.SingletonId);
#include "clang/Basic/AArch64SVEACLETypes.def"
  }

  if (Context.getTargetInfo().getTriple().isPPC64()) {
#define PPC_VECTOR_MMA_TYPE(Name, Id, Size) \
      addImplicitTypedef(#Name, Context.Id##Ty);
#include "clang/Basic/PPCTypes.def"
#define PPC_VECTOR_VSX_TYPE(Name, Id, Size) \
    addImplicitTypedef(#Name, Context.Id##Ty);
#include "clang/Basic/PPCTypes.def"
  }

  if (Context.getTargetInfo().hasRISCVVTypes()) {
#define RVV_TYPE(Name, Id, SingletonId)                                        \
  addImplicitTypedef(Name, Context.SingletonId);
#include "clang/Basic/RISCVVTypes.def"
  }

  if (Context.getTargetInfo().getTriple().isWasm() &&
      Context.getTargetInfo().hasFeature("reference-types")) {
#define WASM_TYPE(Name, Id, SingletonId)                                       \
  addImplicitTypedef(Name, Context.SingletonId);
#include "clang/Basic/WebAssemblyReferenceTypes.def"
  }

  if (Context.getTargetInfo().getTriple().isAMDGPU() ||
      (Context.getAuxTargetInfo() &&
       Context.getAuxTargetInfo()->getTriple().isAMDGPU())) {
#define AMDGPU_TYPE(Name, Id, SingletonId)                                     \
  addImplicitTypedef(Name, Context.SingletonId);
#include "clang/Basic/AMDGPUTypes.def"
  }

  if (Context.getTargetInfo().hasBuiltinMSVaList()) {
    DeclarationName MSVaList = &Context.Idents.get("__builtin_ms_va_list");
    if (IdResolver.begin(MSVaList) == IdResolver.end())
      PushOnScopeChains(Context.getBuiltinMSVaListDecl(), TUScope);
  }

  DeclarationName BuiltinVaList = &Context.Idents.get("__builtin_va_list");
  if (IdResolver.begin(BuiltinVaList) == IdResolver.end())
    PushOnScopeChains(Context.getBuiltinVaListDecl(), TUScope);
}

Sema::~Sema() {
  assert(InstantiatingSpecializations.empty() &&
         "failed to clean up an InstantiatingTemplate?");

  if (VisContext) FreeVisContext();

  // Kill all the active scopes.
  for (sema::FunctionScopeInfo *FSI : FunctionScopes)
    delete FSI;

  // Tell the SemaConsumer to forget about us; we're going out of scope.
  if (SemaConsumer *SC = dyn_cast<SemaConsumer>(&Consumer))
    SC->ForgetSema();

  // Detach from the external Sema source.
  if (ExternalSemaSource *ExternalSema
        = dyn_cast_or_null<ExternalSemaSource>(Context.getExternalSource()))
    ExternalSema->ForgetSema();

  // Delete cached satisfactions.
  std::vector<ConstraintSatisfaction *> Satisfactions;
  Satisfactions.reserve(SatisfactionCache.size());
  for (auto &Node : SatisfactionCache)
    Satisfactions.push_back(&Node);
  for (auto *Node : Satisfactions)
    delete Node;

  threadSafety::threadSafetyCleanup(ThreadSafetyDeclCache);

  // Destroys data sharing attributes stack for OpenMP
  OpenMP().DestroyDataSharingAttributesStack();

  // Detach from the PP callback handler which outlives Sema since it's owned
  // by the preprocessor.
  SemaPPCallbackHandler->reset();
}

void Sema::warnStackExhausted(SourceLocation Loc) {
  // Only warn about this once.
  if (!WarnedStackExhausted) {
    Diag(Loc, diag::warn_stack_exhausted);
    WarnedStackExhausted = true;
  }
}

void Sema::runWithSufficientStackSpace(SourceLocation Loc,
                                       llvm::function_ref<void()> Fn) {
  clang::runWithSufficientStackSpace([&] { warnStackExhausted(Loc); }, Fn);
}

bool Sema::makeUnavailableInSystemHeader(SourceLocation loc,
                                      UnavailableAttr::ImplicitReason reason) {
  // If we're not in a function, it's an error.
  FunctionDecl *fn = dyn_cast<FunctionDecl>(CurContext);
  if (!fn) return false;

  // If we're in template instantiation, it's an error.
  if (inTemplateInstantiation())
    return false;

  // If that function's not in a system header, it's an error.
  if (!Context.getSourceManager().isInSystemHeader(loc))
    return false;

  // If the function is already unavailable, it's not an error.
  if (fn->hasAttr<UnavailableAttr>()) return true;

  fn->addAttr(UnavailableAttr::CreateImplicit(Context, "", reason, loc));
  return true;
}

ASTMutationListener *Sema::getASTMutationListener() const {
  return getASTConsumer().GetASTMutationListener();
}

void Sema::addExternalSource(ExternalSemaSource *E) {
  assert(E && "Cannot use with NULL ptr");

  if (!ExternalSource) {
    ExternalSource = E;
    return;
  }

  if (auto *Ex = dyn_cast<MultiplexExternalSemaSource>(ExternalSource))
    Ex->AddSource(E);
  else
    ExternalSource = new MultiplexExternalSemaSource(ExternalSource.get(), E);
}

void Sema::PrintStats() const {
  llvm::errs() << "\n*** Semantic Analysis Stats:\n";
  llvm::errs() << NumSFINAEErrors << " SFINAE diagnostics trapped.\n";

  BumpAlloc.PrintStats();
  AnalysisWarnings.PrintStats();
}

void Sema::diagnoseNullableToNonnullConversion(QualType DstType,
                                               QualType SrcType,
                                               SourceLocation Loc) {
  std::optional<NullabilityKind> ExprNullability = SrcType->getNullability();
  if (!ExprNullability || (*ExprNullability != NullabilityKind::Nullable &&
                           *ExprNullability != NullabilityKind::NullableResult))
    return;

  std::optional<NullabilityKind> TypeNullability = DstType->getNullability();
  if (!TypeNullability || *TypeNullability != NullabilityKind::NonNull)
    return;

  Diag(Loc, diag::warn_nullability_lost) << SrcType << DstType;
}

// Generate diagnostics when adding or removing effects in a type conversion.
void Sema::diagnoseFunctionEffectConversion(QualType DstType, QualType SrcType,
                                            SourceLocation Loc) {
  const auto SrcFX = FunctionEffectsRef::get(SrcType);
  const auto DstFX = FunctionEffectsRef::get(DstType);
  if (SrcFX != DstFX) {
    for (const auto &Diff : FunctionEffectDifferences(SrcFX, DstFX)) {
      if (Diff.shouldDiagnoseConversion(SrcType, SrcFX, DstType, DstFX))
        Diag(Loc, diag::warn_invalid_add_func_effects) << Diff.effectName();
    }
  }
}

void Sema::diagnoseZeroToNullptrConversion(CastKind Kind, const Expr *E) {
  // nullptr only exists from C++11 on, so don't warn on its absence earlier.
  if (!getLangOpts().CPlusPlus11)
    return;

  if (Kind != CK_NullToPointer && Kind != CK_NullToMemberPointer)
    return;

  const Expr *EStripped = E->IgnoreParenImpCasts();
  if (EStripped->getType()->isNullPtrType())
    return;
  if (isa<GNUNullExpr>(EStripped))
    return;

  if (Diags.isIgnored(diag::warn_zero_as_null_pointer_constant,
                      E->getBeginLoc()))
    return;

  // Don't diagnose the conversion from a 0 literal to a null pointer argument
  // in a synthesized call to operator<=>.
  if (!CodeSynthesisContexts.empty() &&
      CodeSynthesisContexts.back().Kind ==
          CodeSynthesisContext::RewritingOperatorAsSpaceship)
    return;

  // Ignore null pointers in defaulted comparison operators.
  FunctionDecl *FD = getCurFunctionDecl();
  if (FD && FD->isDefaulted()) {
    return;
  }

  // If it is a macro from system header, and if the macro name is not "NULL",
  // do not warn.
  // Note that uses of "NULL" will be ignored above on systems that define it
  // as __null.
  SourceLocation MaybeMacroLoc = E->getBeginLoc();
  if (Diags.getSuppressSystemWarnings() &&
      SourceMgr.isInSystemMacro(MaybeMacroLoc) &&
      !findMacroSpelling(MaybeMacroLoc, "NULL"))
    return;

  Diag(E->getBeginLoc(), diag::warn_zero_as_null_pointer_constant)
      << FixItHint::CreateReplacement(E->getSourceRange(), "nullptr");
}

/// ImpCastExprToType - If Expr is not of type 'Type', insert an implicit cast.
/// If there is already an implicit cast, merge into the existing one.
/// The result is of the given category.
ExprResult Sema::ImpCastExprToType(Expr *E, QualType Ty,
                                   CastKind Kind, ExprValueKind VK,
                                   const CXXCastPath *BasePath,
                                   CheckedConversionKind CCK) {
#ifndef NDEBUG
  if (VK == VK_PRValue && !E->isPRValue()) {
    switch (Kind) {
    default:
      llvm_unreachable(
          ("can't implicitly cast glvalue to prvalue with this cast "
           "kind: " +
           std::string(CastExpr::getCastKindName(Kind)))
              .c_str());
    case CK_Dependent:
    case CK_LValueToRValue:
    case CK_ArrayToPointerDecay:
    case CK_FunctionToPointerDecay:
    case CK_ToVoid:
    case CK_NonAtomicToAtomic:
    case CK_HLSLArrayRValue:
      break;
    }
  }
  assert((VK == VK_PRValue || Kind == CK_Dependent || !E->isPRValue()) &&
         "can't cast prvalue to glvalue");
#endif

  diagnoseNullableToNonnullConversion(Ty, E->getType(), E->getBeginLoc());
  diagnoseZeroToNullptrConversion(Kind, E);
  if (Context.hasAnyFunctionEffects() && !isCast(CCK) &&
      Kind != CK_NullToPointer && Kind != CK_NullToMemberPointer)
    diagnoseFunctionEffectConversion(Ty, E->getType(), E->getBeginLoc());

  QualType ExprTy = Context.getCanonicalType(E->getType());
  QualType TypeTy = Context.getCanonicalType(Ty);

  if (ExprTy == TypeTy)
    return E;

  if (Kind == CK_ArrayToPointerDecay) {
    // C++1z [conv.array]: The temporary materialization conversion is applied.
    // We also use this to fuel C++ DR1213, which applies to C++11 onwards.
    if (getLangOpts().CPlusPlus && E->isPRValue()) {
      // The temporary is an lvalue in C++98 and an xvalue otherwise.
      ExprResult Materialized = CreateMaterializeTemporaryExpr(
          E->getType(), E, !getLangOpts().CPlusPlus11);
      if (Materialized.isInvalid())
        return ExprError();
      E = Materialized.get();
    }
    // C17 6.7.1p6 footnote 124: The implementation can treat any register
    // declaration simply as an auto declaration. However, whether or not
    // addressable storage is actually used, the address of any part of an
    // object declared with storage-class specifier register cannot be
    // computed, either explicitly(by use of the unary & operator as discussed
    // in 6.5.3.2) or implicitly(by converting an array name to a pointer as
    // discussed in 6.3.2.1).Thus, the only operator that can be applied to an
    // array declared with storage-class specifier register is sizeof.
    if (VK == VK_PRValue && !getLangOpts().CPlusPlus && !E->isPRValue()) {
      if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->getStorageClass() == SC_Register) {
            Diag(E->getExprLoc(), diag::err_typecheck_address_of)
                << /*register variable*/ 3 << E->getSourceRange();
            return ExprError();
          }
        }
      }
    }
  }

  if (ImplicitCastExpr *ImpCast = dyn_cast<ImplicitCastExpr>(E)) {
    if (ImpCast->getCastKind() == Kind && (!BasePath || BasePath->empty())) {
      ImpCast->setType(Ty);
      ImpCast->setValueKind(VK);
      return E;
    }
  }

  return ImplicitCastExpr::Create(Context, Ty, Kind, E, BasePath, VK,
                                  CurFPFeatureOverrides());
}

CastKind Sema::ScalarTypeToBooleanCastKind(QualType ScalarTy) {
  switch (ScalarTy->getScalarTypeKind()) {
  case Type::STK_Bool: return CK_NoOp;
  case Type::STK_CPointer: return CK_PointerToBoolean;
  case Type::STK_BlockPointer: return CK_PointerToBoolean;
  case Type::STK_ObjCObjectPointer: return CK_PointerToBoolean;
  case Type::STK_MemberPointer: return CK_MemberPointerToBoolean;
  case Type::STK_Integral: return CK_IntegralToBoolean;
  case Type::STK_Floating: return CK_FloatingToBoolean;
  case Type::STK_IntegralComplex: return CK_IntegralComplexToBoolean;
  case Type::STK_FloatingComplex: return CK_FloatingComplexToBoolean;
  case Type::STK_FixedPoint: return CK_FixedPointToBoolean;
  }
  llvm_unreachable("unknown scalar type kind");
}

/// Used to prune the decls of Sema's UnusedFileScopedDecls vector.
static bool ShouldRemoveFromUnused(Sema *SemaRef, const DeclaratorDecl *D) {
  if (D->getMostRecentDecl()->isUsed())
    return true;

  if (D->isExternallyVisible())
    return true;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    // If this is a function template and none of its specializations is used,
    // we should warn.
    if (FunctionTemplateDecl *Template = FD->getDescribedFunctionTemplate())
      for (const auto *Spec : Template->specializations())
        if (ShouldRemoveFromUnused(SemaRef, Spec))
          return true;

    // UnusedFileScopedDecls stores the first declaration.
    // The declaration may have become definition so check again.
    const FunctionDecl *DeclToCheck;
    if (FD->hasBody(DeclToCheck))
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);

    // Later redecls may add new information resulting in not having to warn,
    // so check again.
    DeclToCheck = FD->getMostRecentDecl();
    if (DeclToCheck != FD)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    // If a variable usable in constant expressions is referenced,
    // don't warn if it isn't used: if the value of a variable is required
    // for the computation of a constant expression, it doesn't make sense to
    // warn even if the variable isn't odr-used.  (isReferenced doesn't
    // precisely reflect that, but it's a decent approximation.)
    if (VD->isReferenced() &&
        VD->mightBeUsableInConstantExpressions(SemaRef->Context))
      return true;

    if (VarTemplateDecl *Template = VD->getDescribedVarTemplate())
      // If this is a variable template and none of its specializations is used,
      // we should warn.
      for (const auto *Spec : Template->specializations())
        if (ShouldRemoveFromUnused(SemaRef, Spec))
          return true;

    // UnusedFileScopedDecls stores the first declaration.
    // The declaration may have become definition so check again.
    const VarDecl *DeclToCheck = VD->getDefinition();
    if (DeclToCheck)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);

    // Later redecls may add new information resulting in not having to warn,
    // so check again.
    DeclToCheck = VD->getMostRecentDecl();
    if (DeclToCheck != VD)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);
  }

  return false;
}

static bool isFunctionOrVarDeclExternC(const NamedDecl *ND) {
  if (const auto *FD = dyn_cast<FunctionDecl>(ND))
    return FD->isExternC();
  return cast<VarDecl>(ND)->isExternC();
}

/// Determine whether ND is an external-linkage function or variable whose
/// type has no linkage.
bool Sema::isExternalWithNoLinkageType(const ValueDecl *VD) const {
  // Note: it's not quite enough to check whether VD has UniqueExternalLinkage,
  // because we also want to catch the case where its type has VisibleNoLinkage,
  // which does not affect the linkage of VD.
  return getLangOpts().CPlusPlus && VD->hasExternalFormalLinkage() &&
         !isExternalFormalLinkage(VD->getType()->getLinkage()) &&
         !isFunctionOrVarDeclExternC(VD);
}

/// Obtains a sorted list of functions and variables that are undefined but
/// ODR-used.
void Sema::getUndefinedButUsed(
    SmallVectorImpl<std::pair<NamedDecl *, SourceLocation> > &Undefined) {
  for (const auto &UndefinedUse : UndefinedButUsed) {
    NamedDecl *ND = UndefinedUse.first;

    // Ignore attributes that have become invalid.
    if (ND->isInvalidDecl()) continue;

    // __attribute__((weakref)) is basically a definition.
    if (ND->hasAttr<WeakRefAttr>()) continue;

    if (isa<CXXDeductionGuideDecl>(ND))
      continue;

    if (ND->hasAttr<DLLImportAttr>() || ND->hasAttr<DLLExportAttr>()) {
      // An exported function will always be emitted when defined, so even if
      // the function is inline, it doesn't have to be emitted in this TU. An
      // imported function implies that it has been exported somewhere else.
      continue;
    }

    if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
      if (FD->isDefined())
        continue;
      if (FD->isExternallyVisible() &&
          !isExternalWithNoLinkageType(FD) &&
          !FD->getMostRecentDecl()->isInlined() &&
          !FD->hasAttr<ExcludeFromExplicitInstantiationAttr>())
        continue;
      if (FD->getBuiltinID())
        continue;
    } else {
      const auto *VD = cast<VarDecl>(ND);
      if (VD->hasDefinition() != VarDecl::DeclarationOnly)
        continue;
      if (VD->isExternallyVisible() &&
          !isExternalWithNoLinkageType(VD) &&
          !VD->getMostRecentDecl()->isInline() &&
          !VD->hasAttr<ExcludeFromExplicitInstantiationAttr>())
        continue;

      // Skip VarDecls that lack formal definitions but which we know are in
      // fact defined somewhere.
      if (VD->isKnownToBeDefined())
        continue;
    }

    Undefined.push_back(std::make_pair(ND, UndefinedUse.second));
  }
}

/// checkUndefinedButUsed - Check for undefined objects with internal linkage
/// or that are inline.
static void checkUndefinedButUsed(Sema &S) {
  if (S.UndefinedButUsed.empty()) return;

  // Collect all the still-undefined entities with internal linkage.
  SmallVector<std::pair<NamedDecl *, SourceLocation>, 16> Undefined;
  S.getUndefinedButUsed(Undefined);
  S.UndefinedButUsed.clear();
  if (Undefined.empty()) return;

  for (const auto &Undef : Undefined) {
    ValueDecl *VD = cast<ValueDecl>(Undef.first);
    SourceLocation UseLoc = Undef.second;

    if (S.isExternalWithNoLinkageType(VD)) {
      // C++ [basic.link]p8:
      //   A type without linkage shall not be used as the type of a variable
      //   or function with external linkage unless
      //    -- the entity has C language linkage
      //    -- the entity is not odr-used or is defined in the same TU
      //
      // As an extension, accept this in cases where the type is externally
      // visible, since the function or variable actually can be defined in
      // another translation unit in that case.
      S.Diag(VD->getLocation(), isExternallyVisible(VD->getType()->getLinkage())
                                    ? diag::ext_undefined_internal_type
                                    : diag::err_undefined_internal_type)
        << isa<VarDecl>(VD) << VD;
    } else if (!VD->isExternallyVisible()) {
      // FIXME: We can promote this to an error. The function or variable can't
      // be defined anywhere else, so the program must necessarily violate the
      // one definition rule.
      bool IsImplicitBase = false;
      if (const auto *BaseD = dyn_cast<FunctionDecl>(VD)) {
        auto *DVAttr = BaseD->getAttr<OMPDeclareVariantAttr>();
        if (DVAttr && !DVAttr->getTraitInfo().isExtensionActive(
                          llvm::omp::TraitProperty::
                              implementation_extension_disable_implicit_base)) {
          const auto *Func = cast<FunctionDecl>(
              cast<DeclRefExpr>(DVAttr->getVariantFuncRef())->getDecl());
          IsImplicitBase = BaseD->isImplicit() &&
                           Func->getIdentifier()->isMangledOpenMPVariantName();
        }
      }
      if (!S.getLangOpts().OpenMP || !IsImplicitBase)
        S.Diag(VD->getLocation(), diag::warn_undefined_internal)
            << isa<VarDecl>(VD) << VD;
    } else if (auto *FD = dyn_cast<FunctionDecl>(VD)) {
      (void)FD;
      assert(FD->getMostRecentDecl()->isInlined() &&
             "used object requires definition but isn't inline or internal?");
      // FIXME: This is ill-formed; we should reject.
      S.Diag(VD->getLocation(), diag::warn_undefined_inline) << VD;
    } else {
      assert(cast<VarDecl>(VD)->getMostRecentDecl()->isInline() &&
             "used var requires definition but isn't inline or internal?");
      S.Diag(VD->getLocation(), diag::err_undefined_inline_var) << VD;
    }
    if (UseLoc.isValid())
      S.Diag(UseLoc, diag::note_used_here);
  }
}

void Sema::LoadExternalWeakUndeclaredIdentifiers() {
  if (!ExternalSource)
    return;

  SmallVector<std::pair<IdentifierInfo *, WeakInfo>, 4> WeakIDs;
  ExternalSource->ReadWeakUndeclaredIdentifiers(WeakIDs);
  for (auto &WeakID : WeakIDs)
    (void)WeakUndeclaredIdentifiers[WeakID.first].insert(WeakID.second);
}


typedef llvm::DenseMap<const CXXRecordDecl*, bool> RecordCompleteMap;

/// Returns true, if all methods and nested classes of the given
/// CXXRecordDecl are defined in this translation unit.
///
/// Should only be called from ActOnEndOfTranslationUnit so that all
/// definitions are actually read.
static bool MethodsAndNestedClassesComplete(const CXXRecordDecl *RD,
                                            RecordCompleteMap &MNCComplete) {
  RecordCompleteMap::iterator Cache = MNCComplete.find(RD);
  if (Cache != MNCComplete.end())
    return Cache->second;
  if (!RD->isCompleteDefinition())
    return false;
  bool Complete = true;
  for (DeclContext::decl_iterator I = RD->decls_begin(),
                                  E = RD->decls_end();
       I != E && Complete; ++I) {
    if (const CXXMethodDecl *M = dyn_cast<CXXMethodDecl>(*I))
      Complete = M->isDefined() || M->isDefaulted() ||
                 (M->isPureVirtual() && !isa<CXXDestructorDecl>(M));
    else if (const FunctionTemplateDecl *F = dyn_cast<FunctionTemplateDecl>(*I))
      // If the template function is marked as late template parsed at this
      // point, it has not been instantiated and therefore we have not
      // performed semantic analysis on it yet, so we cannot know if the type
      // can be considered complete.
      Complete = !F->getTemplatedDecl()->isLateTemplateParsed() &&
                  F->getTemplatedDecl()->isDefined();
    else if (const CXXRecordDecl *R = dyn_cast<CXXRecordDecl>(*I)) {
      if (R->isInjectedClassName())
        continue;
      if (R->hasDefinition())
        Complete = MethodsAndNestedClassesComplete(R->getDefinition(),
                                                   MNCComplete);
      else
        Complete = false;
    }
  }
  MNCComplete[RD] = Complete;
  return Complete;
}

/// Returns true, if the given CXXRecordDecl is fully defined in this
/// translation unit, i.e. all methods are defined or pure virtual and all
/// friends, friend functions and nested classes are fully defined in this
/// translation unit.
///
/// Should only be called from ActOnEndOfTranslationUnit so that all
/// definitions are actually read.
static bool IsRecordFullyDefined(const CXXRecordDecl *RD,
                                 RecordCompleteMap &RecordsComplete,
                                 RecordCompleteMap &MNCComplete) {
  RecordCompleteMap::iterator Cache = RecordsComplete.find(RD);
  if (Cache != RecordsComplete.end())
    return Cache->second;
  bool Complete = MethodsAndNestedClassesComplete(RD, MNCComplete);
  for (CXXRecordDecl::friend_iterator I = RD->friend_begin(),
                                      E = RD->friend_end();
       I != E && Complete; ++I) {
    // Check if friend classes and methods are complete.
    if (TypeSourceInfo *TSI = (*I)->getFriendType()) {
      // Friend classes are available as the TypeSourceInfo of the FriendDecl.
      if (CXXRecordDecl *FriendD = TSI->getType()->getAsCXXRecordDecl())
        Complete = MethodsAndNestedClassesComplete(FriendD, MNCComplete);
      else
        Complete = false;
    } else {
      // Friend functions are available through the NamedDecl of FriendDecl.
      if (const FunctionDecl *FD =
          dyn_cast<FunctionDecl>((*I)->getFriendDecl()))
        Complete = FD->isDefined();
      else
        // This is a template friend, give up.
        Complete = false;
    }
  }
  RecordsComplete[RD] = Complete;
  return Complete;
}

void Sema::emitAndClearUnusedLocalTypedefWarnings() {
  if (ExternalSource)
    ExternalSource->ReadUnusedLocalTypedefNameCandidates(
        UnusedLocalTypedefNameCandidates);
  for (const TypedefNameDecl *TD : UnusedLocalTypedefNameCandidates) {
    if (TD->isReferenced())
      continue;
    Diag(TD->getLocation(), diag::warn_unused_local_typedef)
        << isa<TypeAliasDecl>(TD) << TD->getDeclName();
  }
  UnusedLocalTypedefNameCandidates.clear();
}

void Sema::ActOnStartOfTranslationUnit() {
  if (getLangOpts().CPlusPlusModules &&
      getLangOpts().getCompilingModule() == LangOptions::CMK_HeaderUnit)
    HandleStartOfHeaderUnit();
}

void Sema::ActOnEndOfTranslationUnitFragment(TUFragmentKind Kind) {
  // No explicit actions are required at the end of the global module fragment.
  if (Kind == TUFragmentKind::Global)
    return;

  // Transfer late parsed template instantiations over to the pending template
  // instantiation list. During normal compilation, the late template parser
  // will be installed and instantiating these templates will succeed.
  //
  // If we are building a TU prefix for serialization, it is also safe to
  // transfer these over, even though they are not parsed. The end of the TU
  // should be outside of any eager template instantiation scope, so when this
  // AST is deserialized, these templates will not be parsed until the end of
  // the combined TU.
  PendingInstantiations.insert(PendingInstantiations.end(),
                               LateParsedInstantiations.begin(),
                               LateParsedInstantiations.end());
  LateParsedInstantiations.clear();

  // If DefinedUsedVTables ends up marking any virtual member functions it
  // might lead to more pending template instantiations, which we then need
  // to instantiate.
  DefineUsedVTables();

  // C++: Perform implicit template instantiations.
  //
  // FIXME: When we perform these implicit instantiations, we do not
  // carefully keep track of the point of instantiation (C++ [temp.point]).
  // This means that name lookup that occurs within the template
  // instantiation will always happen at the end of the translation unit,
  // so it will find some names that are not required to be found. This is
  // valid, but we could do better by diagnosing if an instantiation uses a
  // name that was not visible at its first point of instantiation.
  if (ExternalSource) {
    // Load pending instantiations from the external source.
    SmallVector<PendingImplicitInstantiation, 4> Pending;
    ExternalSource->ReadPendingInstantiations(Pending);
    for (auto PII : Pending)
      if (auto Func = dyn_cast<FunctionDecl>(PII.first))
        Func->setInstantiationIsPending(true);
    PendingInstantiations.insert(PendingInstantiations.begin(),
                                 Pending.begin(), Pending.end());
  }

  {
    llvm::TimeTraceScope TimeScope("PerformPendingInstantiations");
    PerformPendingInstantiations();
  }

  emitDeferredDiags();

  assert(LateParsedInstantiations.empty() &&
         "end of TU template instantiation should not create more "
         "late-parsed templates");

  // Report diagnostics for uncorrected delayed typos. Ideally all of them
  // should have been corrected by that time, but it is very hard to cover all
  // cases in practice.
  for (const auto &Typo : DelayedTypos) {
    // We pass an empty TypoCorrection to indicate no correction was performed.
    Typo.second.DiagHandler(TypoCorrection());
  }
  DelayedTypos.clear();
}

void Sema::ActOnEndOfTranslationUnit() {
  assert(DelayedDiagnostics.getCurrentPool() == nullptr
         && "reached end of translation unit with a pool attached?");

  // If code completion is enabled, don't perform any end-of-translation-unit
  // work.
  if (PP.isCodeCompletionEnabled())
    return;

  // Complete translation units and modules define vtables and perform implicit
  // instantiations. PCH files do not.
  if (TUKind != TU_Prefix) {
    ObjC().DiagnoseUseOfUnimplementedSelectors();

    ActOnEndOfTranslationUnitFragment(
        !ModuleScopes.empty() && ModuleScopes.back().Module->Kind ==
                                     Module::PrivateModuleFragment
            ? TUFragmentKind::Private
            : TUFragmentKind::Normal);

    if (LateTemplateParserCleanup)
      LateTemplateParserCleanup(OpaqueParser);

    CheckDelayedMemberExceptionSpecs();
  } else {
    // If we are building a TU prefix for serialization, it is safe to transfer
    // these over, even though they are not parsed. The end of the TU should be
    // outside of any eager template instantiation scope, so when this AST is
    // deserialized, these templates will not be parsed until the end of the
    // combined TU.
    PendingInstantiations.insert(PendingInstantiations.end(),
                                 LateParsedInstantiations.begin(),
                                 LateParsedInstantiations.end());
    LateParsedInstantiations.clear();

    if (LangOpts.PCHInstantiateTemplates) {
      llvm::TimeTraceScope TimeScope("PerformPendingInstantiations");
      PerformPendingInstantiations();
    }
  }

  DiagnoseUnterminatedPragmaAlignPack();
  DiagnoseUnterminatedPragmaAttribute();
  OpenMP().DiagnoseUnterminatedOpenMPDeclareTarget();

  // All delayed member exception specs should be checked or we end up accepting
  // incompatible declarations.
  assert(DelayedOverridingExceptionSpecChecks.empty());
  assert(DelayedEquivalentExceptionSpecChecks.empty());

  // All dllexport classes should have been processed already.
  assert(DelayedDllExportClasses.empty());
  assert(DelayedDllExportMemberFunctions.empty());

  // Remove file scoped decls that turned out to be used.
  UnusedFileScopedDecls.erase(
      std::remove_if(UnusedFileScopedDecls.begin(nullptr, true),
                     UnusedFileScopedDecls.end(),
                     [this](const DeclaratorDecl *DD) {
                       return ShouldRemoveFromUnused(this, DD);
                     }),
      UnusedFileScopedDecls.end());

  if (TUKind == TU_Prefix) {
    // Translation unit prefixes don't need any of the checking below.
    if (!PP.isIncrementalProcessingEnabled())
      TUScope = nullptr;
    return;
  }

  // Check for #pragma weak identifiers that were never declared
  LoadExternalWeakUndeclaredIdentifiers();
  for (const auto &WeakIDs : WeakUndeclaredIdentifiers) {
    if (WeakIDs.second.empty())
      continue;

    Decl *PrevDecl = LookupSingleName(TUScope, WeakIDs.first, SourceLocation(),
                                      LookupOrdinaryName);
    if (PrevDecl != nullptr &&
        !(isa<FunctionDecl>(PrevDecl) || isa<VarDecl>(PrevDecl)))
      for (const auto &WI : WeakIDs.second)
        Diag(WI.getLocation(), diag::warn_attribute_wrong_decl_type)
            << "'weak'" << /*isRegularKeyword=*/0 << ExpectedVariableOrFunction;
    else
      for (const auto &WI : WeakIDs.second)
        Diag(WI.getLocation(), diag::warn_weak_identifier_undeclared)
            << WeakIDs.first;
  }

  if (LangOpts.CPlusPlus11 &&
      !Diags.isIgnored(diag::warn_delegating_ctor_cycle, SourceLocation()))
    CheckDelegatingCtorCycles();

  if (!Diags.hasErrorOccurred()) {
    if (ExternalSource)
      ExternalSource->ReadUndefinedButUsed(UndefinedButUsed);
    checkUndefinedButUsed(*this);
  }

  // A global-module-fragment is only permitted within a module unit.
  if (!ModuleScopes.empty() && ModuleScopes.back().Module->Kind ==
                                   Module::ExplicitGlobalModuleFragment) {
    Diag(ModuleScopes.back().BeginLoc,
         diag::err_module_declaration_missing_after_global_module_introducer);
  }

  // Now we can decide whether the modules we're building need an initializer.
  if (Module *CurrentModule = getCurrentModule();
      CurrentModule && CurrentModule->isInterfaceOrPartition()) {
    auto DoesModNeedInit = [this](Module *M) {
      if (!getASTContext().getModuleInitializers(M).empty())
        return true;
      for (auto [Exported, _] : M->Exports)
        if (Exported->isNamedModuleInterfaceHasInit())
          return true;
      for (Module *I : M->Imports)
        if (I->isNamedModuleInterfaceHasInit())
          return true;

      return false;
    };

    CurrentModule->NamedModuleHasInit =
        DoesModNeedInit(CurrentModule) ||
        llvm::any_of(CurrentModule->submodules(),
                     [&](auto *SubM) { return DoesModNeedInit(SubM); });
  }

  if (TUKind == TU_ClangModule) {
    // If we are building a module, resolve all of the exported declarations
    // now.
    if (Module *CurrentModule = PP.getCurrentModule()) {
      ModuleMap &ModMap = PP.getHeaderSearchInfo().getModuleMap();

      SmallVector<Module *, 2> Stack;
      Stack.push_back(CurrentModule);
      while (!Stack.empty()) {
        Module *Mod = Stack.pop_back_val();

        // Resolve the exported declarations and conflicts.
        // FIXME: Actually complain, once we figure out how to teach the
        // diagnostic client to deal with complaints in the module map at this
        // point.
        ModMap.resolveExports(Mod, /*Complain=*/false);
        ModMap.resolveUses(Mod, /*Complain=*/false);
        ModMap.resolveConflicts(Mod, /*Complain=*/false);

        // Queue the submodules, so their exports will also be resolved.
        auto SubmodulesRange = Mod->submodules();
        Stack.append(SubmodulesRange.begin(), SubmodulesRange.end());
      }
    }

    // Warnings emitted in ActOnEndOfTranslationUnit() should be emitted for
    // modules when they are built, not every time they are used.
    emitAndClearUnusedLocalTypedefWarnings();
  }

  // C++ standard modules. Diagnose cases where a function is declared inline
  // in the module purview but has no definition before the end of the TU or
  // the start of a Private Module Fragment (if one is present).
  if (!PendingInlineFuncDecls.empty()) {
    for (auto *D : PendingInlineFuncDecls) {
      if (auto *FD = dyn_cast<FunctionDecl>(D)) {
        bool DefInPMF = false;
        if (auto *FDD = FD->getDefinition()) {
          DefInPMF = FDD->getOwningModule()->isPrivateModule();
          if (!DefInPMF)
            continue;
        }
        Diag(FD->getLocation(), diag::err_export_inline_not_defined)
            << DefInPMF;
        // If we have a PMF it should be at the end of the ModuleScopes.
        if (DefInPMF &&
            ModuleScopes.back().Module->Kind == Module::PrivateModuleFragment) {
          Diag(ModuleScopes.back().BeginLoc,
               diag::note_private_module_fragment);
        }
      }
    }
    PendingInlineFuncDecls.clear();
  }

  // C99 6.9.2p2:
  //   A declaration of an identifier for an object that has file
  //   scope without an initializer, and without a storage-class
  //   specifier or with the storage-class specifier static,
  //   constitutes a tentative definition. If a translation unit
  //   contains one or more tentative definitions for an identifier,
  //   and the translation unit contains no external definition for
  //   that identifier, then the behavior is exactly as if the
  //   translation unit contains a file scope declaration of that
  //   identifier, with the composite type as of the end of the
  //   translation unit, with an initializer equal to 0.
  llvm::SmallSet<VarDecl *, 32> Seen;
  for (TentativeDefinitionsType::iterator
           T = TentativeDefinitions.begin(ExternalSource.get()),
           TEnd = TentativeDefinitions.end();
       T != TEnd; ++T) {
    VarDecl *VD = (*T)->getActingDefinition();

    // If the tentative definition was completed, getActingDefinition() returns
    // null. If we've already seen this variable before, insert()'s second
    // return value is false.
    if (!VD || VD->isInvalidDecl() || !Seen.insert(VD).second)
      continue;

    if (const IncompleteArrayType *ArrayT
        = Context.getAsIncompleteArrayType(VD->getType())) {
      // Set the length of the array to 1 (C99 6.9.2p5).
      Diag(VD->getLocation(), diag::warn_tentative_incomplete_array);
      llvm::APInt One(Context.getTypeSize(Context.getSizeType()), true);
      QualType T = Context.getConstantArrayType(
          ArrayT->getElementType(), One, nullptr, ArraySizeModifier::Normal, 0);
      VD->setType(T);
    } else if (RequireCompleteType(VD->getLocation(), VD->getType(),
                                   diag::err_tentative_def_incomplete_type))
      VD->setInvalidDecl();

    // No initialization is performed for a tentative definition.
    CheckCompleteVariableDeclaration(VD);

    // Notify the consumer that we've completed a tentative definition.
    if (!VD->isInvalidDecl())
      Consumer.CompleteTentativeDefinition(VD);
  }

  for (auto *D : ExternalDeclarations) {
    if (!D || D->isInvalidDecl() || D->getPreviousDecl() || !D->isUsed())
      continue;

    Consumer.CompleteExternalDeclaration(D);
  }

  if (LangOpts.HLSL)
    HLSL().DiagnoseAvailabilityViolations(
        getASTContext().getTranslationUnitDecl());

  // If there were errors, disable 'unused' warnings since they will mostly be
  // noise. Don't warn for a use from a module: either we should warn on all
  // file-scope declarations in modules or not at all, but whether the
  // declaration is used is immaterial.
  if (!Diags.hasErrorOccurred() && TUKind != TU_ClangModule) {
    // Output warning for unused file scoped decls.
    for (UnusedFileScopedDeclsType::iterator
             I = UnusedFileScopedDecls.begin(ExternalSource.get()),
             E = UnusedFileScopedDecls.end();
         I != E; ++I) {
      if (ShouldRemoveFromUnused(this, *I))
        continue;

      if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(*I)) {
        const FunctionDecl *DiagD;
        if (!FD->hasBody(DiagD))
          DiagD = FD;
        if (DiagD->isDeleted())
          continue; // Deleted functions are supposed to be unused.
        SourceRange DiagRange = DiagD->getLocation();
        if (const ASTTemplateArgumentListInfo *ASTTAL =
                DiagD->getTemplateSpecializationArgsAsWritten())
          DiagRange.setEnd(ASTTAL->RAngleLoc);
        if (DiagD->isReferenced()) {
          if (isa<CXXMethodDecl>(DiagD))
            Diag(DiagD->getLocation(), diag::warn_unneeded_member_function)
                << DiagD << DiagRange;
          else {
            if (FD->getStorageClass() == SC_Static &&
                !FD->isInlineSpecified() &&
                !SourceMgr.isInMainFile(
                   SourceMgr.getExpansionLoc(FD->getLocation())))
              Diag(DiagD->getLocation(),
                   diag::warn_unneeded_static_internal_decl)
                  << DiagD << DiagRange;
            else
              Diag(DiagD->getLocation(), diag::warn_unneeded_internal_decl)
                  << /*function=*/0 << DiagD << DiagRange;
          }
        } else if (!FD->isTargetMultiVersion() ||
                   FD->isTargetMultiVersionDefault()) {
          if (FD->getDescribedFunctionTemplate())
            Diag(DiagD->getLocation(), diag::warn_unused_template)
                << /*function=*/0 << DiagD << DiagRange;
          else
            Diag(DiagD->getLocation(), isa<CXXMethodDecl>(DiagD)
                                           ? diag::warn_unused_member_function
                                           : diag::warn_unused_function)
                << DiagD << DiagRange;
        }
      } else {
        const VarDecl *DiagD = cast<VarDecl>(*I)->getDefinition();
        if (!DiagD)
          DiagD = cast<VarDecl>(*I);
        SourceRange DiagRange = DiagD->getLocation();
        if (const auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(DiagD)) {
          if (const ASTTemplateArgumentListInfo *ASTTAL =
                  VTSD->getTemplateArgsAsWritten())
            DiagRange.setEnd(ASTTAL->RAngleLoc);
        }
        if (DiagD->isReferenced()) {
          Diag(DiagD->getLocation(), diag::warn_unneeded_internal_decl)
              << /*variable=*/1 << DiagD << DiagRange;
        } else if (DiagD->getDescribedVarTemplate()) {
          Diag(DiagD->getLocation(), diag::warn_unused_template)
              << /*variable=*/1 << DiagD << DiagRange;
        } else if (DiagD->getType().isConstQualified()) {
          const SourceManager &SM = SourceMgr;
          if (SM.getMainFileID() != SM.getFileID(DiagD->getLocation()) ||
              !PP.getLangOpts().IsHeaderFile)
            Diag(DiagD->getLocation(), diag::warn_unused_const_variable)
                << DiagD << DiagRange;
        } else {
          Diag(DiagD->getLocation(), diag::warn_unused_variable)
              << DiagD << DiagRange;
        }
      }
    }

    emitAndClearUnusedLocalTypedefWarnings();
  }

  if (!Diags.isIgnored(diag::warn_unused_private_field, SourceLocation())) {
    // FIXME: Load additional unused private field candidates from the external
    // source.
    RecordCompleteMap RecordsComplete;
    RecordCompleteMap MNCComplete;
    for (const NamedDecl *D : UnusedPrivateFields) {
      const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D->getDeclContext());
      if (RD && !RD->isUnion() &&
          IsRecordFullyDefined(RD, RecordsComplete, MNCComplete)) {
        Diag(D->getLocation(), diag::warn_unused_private_field)
              << D->getDeclName();
      }
    }
  }

  if (!Diags.isIgnored(diag::warn_mismatched_delete_new, SourceLocation())) {
    if (ExternalSource)
      ExternalSource->ReadMismatchingDeleteExpressions(DeleteExprs);
    for (const auto &DeletedFieldInfo : DeleteExprs) {
      for (const auto &DeleteExprLoc : DeletedFieldInfo.second) {
        AnalyzeDeleteExprMismatch(DeletedFieldInfo.first, DeleteExprLoc.first,
                                  DeleteExprLoc.second);
      }
    }
  }

  AnalysisWarnings.IssueWarnings(Context.getTranslationUnitDecl());

  // Check we've noticed that we're no longer parsing the initializer for every
  // variable. If we miss cases, then at best we have a performance issue and
  // at worst a rejects-valid bug.
  assert(ParsingInitForAutoVars.empty() &&
         "Didn't unmark var as having its initializer parsed");

  if (!PP.isIncrementalProcessingEnabled())
    TUScope = nullptr;
}


//===----------------------------------------------------------------------===//
// Helper functions.
//===----------------------------------------------------------------------===//

DeclContext *Sema::getFunctionLevelDeclContext(bool AllowLambda) const {
  DeclContext *DC = CurContext;

  while (true) {
    if (isa<BlockDecl>(DC) || isa<EnumDecl>(DC) || isa<CapturedDecl>(DC) ||
        isa<RequiresExprBodyDecl>(DC)) {
      DC = DC->getParent();
    } else if (!AllowLambda && isa<CXXMethodDecl>(DC) &&
               cast<CXXMethodDecl>(DC)->getOverloadedOperator() == OO_Call &&
               cast<CXXRecordDecl>(DC->getParent())->isLambda()) {
      DC = DC->getParent()->getParent();
    } else break;
  }

  return DC;
}

/// getCurFunctionDecl - If inside of a function body, this returns a pointer
/// to the function decl for the function being parsed.  If we're currently
/// in a 'block', this returns the containing context.
FunctionDecl *Sema::getCurFunctionDecl(bool AllowLambda) const {
  DeclContext *DC = getFunctionLevelDeclContext(AllowLambda);
  return dyn_cast<FunctionDecl>(DC);
}

ObjCMethodDecl *Sema::getCurMethodDecl() {
  DeclContext *DC = getFunctionLevelDeclContext();
  while (isa<RecordDecl>(DC))
    DC = DC->getParent();
  return dyn_cast<ObjCMethodDecl>(DC);
}

NamedDecl *Sema::getCurFunctionOrMethodDecl() const {
  DeclContext *DC = getFunctionLevelDeclContext();
  if (isa<ObjCMethodDecl>(DC) || isa<FunctionDecl>(DC))
    return cast<NamedDecl>(DC);
  return nullptr;
}

LangAS Sema::getDefaultCXXMethodAddrSpace() const {
  if (getLangOpts().OpenCL)
    return getASTContext().getDefaultOpenCLPointeeAddrSpace();
  return LangAS::Default;
}

void Sema::EmitCurrentDiagnostic(unsigned DiagID) {
  // FIXME: It doesn't make sense to me that DiagID is an incoming argument here
  // and yet we also use the current diag ID on the DiagnosticsEngine. This has
  // been made more painfully obvious by the refactor that introduced this
  // function, but it is possible that the incoming argument can be
  // eliminated. If it truly cannot be (for example, there is some reentrancy
  // issue I am not seeing yet), then there should at least be a clarifying
  // comment somewhere.
  if (std::optional<TemplateDeductionInfo *> Info = isSFINAEContext()) {
    switch (DiagnosticIDs::getDiagnosticSFINAEResponse(
              Diags.getCurrentDiagID())) {
    case DiagnosticIDs::SFINAE_Report:
      // We'll report the diagnostic below.
      break;

    case DiagnosticIDs::SFINAE_SubstitutionFailure:
      // Count this failure so that we know that template argument deduction
      // has failed.
      ++NumSFINAEErrors;

      // Make a copy of this suppressed diagnostic and store it with the
      // template-deduction information.
      if (*Info && !(*Info)->hasSFINAEDiagnostic()) {
        Diagnostic DiagInfo(&Diags);
        (*Info)->addSFINAEDiagnostic(DiagInfo.getLocation(),
                       PartialDiagnostic(DiagInfo, Context.getDiagAllocator()));
      }

      Diags.setLastDiagnosticIgnored(true);
      Diags.Clear();
      return;

    case DiagnosticIDs::SFINAE_AccessControl: {
      // Per C++ Core Issue 1170, access control is part of SFINAE.
      // Additionally, the AccessCheckingSFINAE flag can be used to temporarily
      // make access control a part of SFINAE for the purposes of checking
      // type traits.
      if (!AccessCheckingSFINAE && !getLangOpts().CPlusPlus11)
        break;

      SourceLocation Loc = Diags.getCurrentDiagLoc();

      // Suppress this diagnostic.
      ++NumSFINAEErrors;

      // Make a copy of this suppressed diagnostic and store it with the
      // template-deduction information.
      if (*Info && !(*Info)->hasSFINAEDiagnostic()) {
        Diagnostic DiagInfo(&Diags);
        (*Info)->addSFINAEDiagnostic(DiagInfo.getLocation(),
                       PartialDiagnostic(DiagInfo, Context.getDiagAllocator()));
      }

      Diags.setLastDiagnosticIgnored(true);
      Diags.Clear();

      // Now the diagnostic state is clear, produce a C++98 compatibility
      // warning.
      Diag(Loc, diag::warn_cxx98_compat_sfinae_access_control);

      // The last diagnostic which Sema produced was ignored. Suppress any
      // notes attached to it.
      Diags.setLastDiagnosticIgnored(true);
      return;
    }

    case DiagnosticIDs::SFINAE_Suppress:
      // Make a copy of this suppressed diagnostic and store it with the
      // template-deduction information;
      if (*Info) {
        Diagnostic DiagInfo(&Diags);
        (*Info)->addSuppressedDiagnostic(DiagInfo.getLocation(),
                       PartialDiagnostic(DiagInfo, Context.getDiagAllocator()));
      }

      // Suppress this diagnostic.
      Diags.setLastDiagnosticIgnored(true);
      Diags.Clear();
      return;
    }
  }

  // Copy the diagnostic printing policy over the ASTContext printing policy.
  // TODO: Stop doing that.  See: https://reviews.llvm.org/D45093#1090292
  Context.setPrintingPolicy(getPrintingPolicy());

  // Emit the diagnostic.
  if (!Diags.EmitCurrentDiagnostic())
    return;

  // If this is not a note, and we're in a template instantiation
  // that is different from the last template instantiation where
  // we emitted an error, print a template instantiation
  // backtrace.
  if (!DiagnosticIDs::isBuiltinNote(DiagID))
    PrintContextStack();
}

bool Sema::hasUncompilableErrorOccurred() const {
  if (getDiagnostics().hasUncompilableErrorOccurred())
    return true;
  auto *FD = dyn_cast<FunctionDecl>(CurContext);
  if (!FD)
    return false;
  auto Loc = DeviceDeferredDiags.find(FD);
  if (Loc == DeviceDeferredDiags.end())
    return false;
  for (auto PDAt : Loc->second) {
    if (DiagnosticIDs::isDefaultMappingAsError(PDAt.second.getDiagID()))
      return true;
  }
  return false;
}

// Print notes showing how we can reach FD starting from an a priori
// known-callable function.
static void emitCallStackNotes(Sema &S, const FunctionDecl *FD) {
  auto FnIt = S.CUDA().DeviceKnownEmittedFns.find(FD);
  while (FnIt != S.CUDA().DeviceKnownEmittedFns.end()) {
    // Respect error limit.
    if (S.Diags.hasFatalErrorOccurred())
      return;
    DiagnosticBuilder Builder(
        S.Diags.Report(FnIt->second.Loc, diag::note_called_by));
    Builder << FnIt->second.FD;
    FnIt = S.CUDA().DeviceKnownEmittedFns.find(FnIt->second.FD);
  }
}

namespace {

/// Helper class that emits deferred diagnostic messages if an entity directly
/// or indirectly using the function that causes the deferred diagnostic
/// messages is known to be emitted.
///
/// During parsing of AST, certain diagnostic messages are recorded as deferred
/// diagnostics since it is unknown whether the functions containing such
/// diagnostics will be emitted. A list of potentially emitted functions and
/// variables that may potentially trigger emission of functions are also
/// recorded. DeferredDiagnosticsEmitter recursively visits used functions
/// by each function to emit deferred diagnostics.
///
/// During the visit, certain OpenMP directives or initializer of variables
/// with certain OpenMP attributes will cause subsequent visiting of any
/// functions enter a state which is called OpenMP device context in this
/// implementation. The state is exited when the directive or initializer is
/// exited. This state can change the emission states of subsequent uses
/// of functions.
///
/// Conceptually the functions or variables to be visited form a use graph
/// where the parent node uses the child node. At any point of the visit,
/// the tree nodes traversed from the tree root to the current node form a use
/// stack. The emission state of the current node depends on two factors:
///    1. the emission state of the root node
///    2. whether the current node is in OpenMP device context
/// If the function is decided to be emitted, its contained deferred diagnostics
/// are emitted, together with the information about the use stack.
///
class DeferredDiagnosticsEmitter
    : public UsedDeclVisitor<DeferredDiagnosticsEmitter> {
public:
  typedef UsedDeclVisitor<DeferredDiagnosticsEmitter> Inherited;

  // Whether the function is already in the current use-path.
  llvm::SmallPtrSet<CanonicalDeclPtr<Decl>, 4> InUsePath;

  // The current use-path.
  llvm::SmallVector<CanonicalDeclPtr<FunctionDecl>, 4> UsePath;

  // Whether the visiting of the function has been done. Done[0] is for the
  // case not in OpenMP device context. Done[1] is for the case in OpenMP
  // device context. We need two sets because diagnostics emission may be
  // different depending on whether it is in OpenMP device context.
  llvm::SmallPtrSet<CanonicalDeclPtr<Decl>, 4> DoneMap[2];

  // Emission state of the root node of the current use graph.
  bool ShouldEmitRootNode;

  // Current OpenMP device context level. It is initialized to 0 and each
  // entering of device context increases it by 1 and each exit decreases
  // it by 1. Non-zero value indicates it is currently in device context.
  unsigned InOMPDeviceContext;

  DeferredDiagnosticsEmitter(Sema &S)
      : Inherited(S), ShouldEmitRootNode(false), InOMPDeviceContext(0) {}

  bool shouldVisitDiscardedStmt() const { return false; }

  void VisitOMPTargetDirective(OMPTargetDirective *Node) {
    ++InOMPDeviceContext;
    Inherited::VisitOMPTargetDirective(Node);
    --InOMPDeviceContext;
  }

  void visitUsedDecl(SourceLocation Loc, Decl *D) {
    if (isa<VarDecl>(D))
      return;
    if (auto *FD = dyn_cast<FunctionDecl>(D))
      checkFunc(Loc, FD);
    else
      Inherited::visitUsedDecl(Loc, D);
  }

  void checkVar(VarDecl *VD) {
    assert(VD->isFileVarDecl() &&
           "Should only check file-scope variables");
    if (auto *Init = VD->getInit()) {
      auto DevTy = OMPDeclareTargetDeclAttr::getDeviceType(VD);
      bool IsDev = DevTy && (*DevTy == OMPDeclareTargetDeclAttr::DT_NoHost ||
                             *DevTy == OMPDeclareTargetDeclAttr::DT_Any);
      if (IsDev)
        ++InOMPDeviceContext;
      this->Visit(Init);
      if (IsDev)
        --InOMPDeviceContext;
    }
  }

  void checkFunc(SourceLocation Loc, FunctionDecl *FD) {
    auto &Done = DoneMap[InOMPDeviceContext > 0 ? 1 : 0];
    FunctionDecl *Caller = UsePath.empty() ? nullptr : UsePath.back();
    if ((!ShouldEmitRootNode && !S.getLangOpts().OpenMP && !Caller) ||
        S.shouldIgnoreInHostDeviceCheck(FD) || InUsePath.count(FD))
      return;
    // Finalize analysis of OpenMP-specific constructs.
    if (Caller && S.LangOpts.OpenMP && UsePath.size() == 1 &&
        (ShouldEmitRootNode || InOMPDeviceContext))
      S.OpenMP().finalizeOpenMPDelayedAnalysis(Caller, FD, Loc);
    if (Caller)
      S.CUDA().DeviceKnownEmittedFns[FD] = {Caller, Loc};
    // Always emit deferred diagnostics for the direct users. This does not
    // lead to explosion of diagnostics since each user is visited at most
    // twice.
    if (ShouldEmitRootNode || InOMPDeviceContext)
      emitDeferredDiags(FD, Caller);
    // Do not revisit a function if the function body has been completely
    // visited before.
    if (!Done.insert(FD).second)
      return;
    InUsePath.insert(FD);
    UsePath.push_back(FD);
    if (auto *S = FD->getBody()) {
      this->Visit(S);
    }
    UsePath.pop_back();
    InUsePath.erase(FD);
  }

  void checkRecordedDecl(Decl *D) {
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      ShouldEmitRootNode = S.getEmissionStatus(FD, /*Final=*/true) ==
                           Sema::FunctionEmissionStatus::Emitted;
      checkFunc(SourceLocation(), FD);
    } else
      checkVar(cast<VarDecl>(D));
  }

  // Emit any deferred diagnostics for FD
  void emitDeferredDiags(FunctionDecl *FD, bool ShowCallStack) {
    auto It = S.DeviceDeferredDiags.find(FD);
    if (It == S.DeviceDeferredDiags.end())
      return;
    bool HasWarningOrError = false;
    bool FirstDiag = true;
    for (PartialDiagnosticAt &PDAt : It->second) {
      // Respect error limit.
      if (S.Diags.hasFatalErrorOccurred())
        return;
      const SourceLocation &Loc = PDAt.first;
      const PartialDiagnostic &PD = PDAt.second;
      HasWarningOrError |=
          S.getDiagnostics().getDiagnosticLevel(PD.getDiagID(), Loc) >=
          DiagnosticsEngine::Warning;
      {
        DiagnosticBuilder Builder(S.Diags.Report(Loc, PD.getDiagID()));
        PD.Emit(Builder);
      }
      // Emit the note on the first diagnostic in case too many diagnostics
      // cause the note not emitted.
      if (FirstDiag && HasWarningOrError && ShowCallStack) {
        emitCallStackNotes(S, FD);
        FirstDiag = false;
      }
    }
  }
};
} // namespace

void Sema::emitDeferredDiags() {
  if (ExternalSource)
    ExternalSource->ReadDeclsToCheckForDeferredDiags(
        DeclsToCheckForDeferredDiags);

  if ((DeviceDeferredDiags.empty() && !LangOpts.OpenMP) ||
      DeclsToCheckForDeferredDiags.empty())
    return;

  DeferredDiagnosticsEmitter DDE(*this);
  for (auto *D : DeclsToCheckForDeferredDiags)
    DDE.checkRecordedDecl(D);
}

// In CUDA, there are some constructs which may appear in semantically-valid
// code, but trigger errors if we ever generate code for the function in which
// they appear.  Essentially every construct you're not allowed to use on the
// device falls into this category, because you are allowed to use these
// constructs in a __host__ __device__ function, but only if that function is
// never codegen'ed on the device.
//
// To handle semantic checking for these constructs, we keep track of the set of
// functions we know will be emitted, either because we could tell a priori that
// they would be emitted, or because they were transitively called by a
// known-emitted function.
//
// We also keep a partial call graph of which not-known-emitted functions call
// which other not-known-emitted functions.
//
// When we see something which is illegal if the current function is emitted
// (usually by way of DiagIfDeviceCode, DiagIfHostCode, or
// CheckCall), we first check if the current function is known-emitted.  If
// so, we immediately output the diagnostic.
//
// Otherwise, we "defer" the diagnostic.  It sits in Sema::DeviceDeferredDiags
// until we discover that the function is known-emitted, at which point we take
// it out of this map and emit the diagnostic.

Sema::SemaDiagnosticBuilder::SemaDiagnosticBuilder(Kind K, SourceLocation Loc,
                                                   unsigned DiagID,
                                                   const FunctionDecl *Fn,
                                                   Sema &S)
    : S(S), Loc(Loc), DiagID(DiagID), Fn(Fn),
      ShowCallStack(K == K_ImmediateWithCallStack || K == K_Deferred) {
  switch (K) {
  case K_Nop:
    break;
  case K_Immediate:
  case K_ImmediateWithCallStack:
    ImmediateDiag.emplace(
        ImmediateDiagBuilder(S.Diags.Report(Loc, DiagID), S, DiagID));
    break;
  case K_Deferred:
    assert(Fn && "Must have a function to attach the deferred diag to.");
    auto &Diags = S.DeviceDeferredDiags[Fn];
    PartialDiagId.emplace(Diags.size());
    Diags.emplace_back(Loc, S.PDiag(DiagID));
    break;
  }
}

Sema::SemaDiagnosticBuilder::SemaDiagnosticBuilder(SemaDiagnosticBuilder &&D)
    : S(D.S), Loc(D.Loc), DiagID(D.DiagID), Fn(D.Fn),
      ShowCallStack(D.ShowCallStack), ImmediateDiag(D.ImmediateDiag),
      PartialDiagId(D.PartialDiagId) {
  // Clean the previous diagnostics.
  D.ShowCallStack = false;
  D.ImmediateDiag.reset();
  D.PartialDiagId.reset();
}

Sema::SemaDiagnosticBuilder::~SemaDiagnosticBuilder() {
  if (ImmediateDiag) {
    // Emit our diagnostic and, if it was a warning or error, output a callstack
    // if Fn isn't a priori known-emitted.
    bool IsWarningOrError = S.getDiagnostics().getDiagnosticLevel(
                                DiagID, Loc) >= DiagnosticsEngine::Warning;
    ImmediateDiag.reset(); // Emit the immediate diag.
    if (IsWarningOrError && ShowCallStack)
      emitCallStackNotes(S, Fn);
  } else {
    assert((!PartialDiagId || ShowCallStack) &&
           "Must always show call stack for deferred diags.");
  }
}

Sema::SemaDiagnosticBuilder
Sema::targetDiag(SourceLocation Loc, unsigned DiagID, const FunctionDecl *FD) {
  FD = FD ? FD : getCurFunctionDecl();
  if (LangOpts.OpenMP)
    return LangOpts.OpenMPIsTargetDevice
               ? OpenMP().diagIfOpenMPDeviceCode(Loc, DiagID, FD)
               : OpenMP().diagIfOpenMPHostCode(Loc, DiagID, FD);
  if (getLangOpts().CUDA)
    return getLangOpts().CUDAIsDevice ? CUDA().DiagIfDeviceCode(Loc, DiagID)
                                      : CUDA().DiagIfHostCode(Loc, DiagID);

  if (getLangOpts().SYCLIsDevice)
    return SYCL().DiagIfDeviceCode(Loc, DiagID);

  return SemaDiagnosticBuilder(SemaDiagnosticBuilder::K_Immediate, Loc, DiagID,
                               FD, *this);
}

void Sema::checkTypeSupport(QualType Ty, SourceLocation Loc, ValueDecl *D) {
  if (isUnevaluatedContext() || Ty.isNull())
    return;

  // The original idea behind checkTypeSupport function is that unused
  // declarations can be replaced with an array of bytes of the same size during
  // codegen, such replacement doesn't seem to be possible for types without
  // constant byte size like zero length arrays. So, do a deep check for SYCL.
  if (D && LangOpts.SYCLIsDevice) {
    llvm::DenseSet<QualType> Visited;
    SYCL().deepTypeCheckForDevice(Loc, Visited, D);
  }

  Decl *C = cast<Decl>(getCurLexicalContext());

  // Memcpy operations for structs containing a member with unsupported type
  // are ok, though.
  if (const auto *MD = dyn_cast<CXXMethodDecl>(C)) {
    if ((MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator()) &&
        MD->isTrivial())
      return;

    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(MD))
      if (Ctor->isCopyOrMoveConstructor() && Ctor->isTrivial())
        return;
  }

  // Try to associate errors with the lexical context, if that is a function, or
  // the value declaration otherwise.
  const FunctionDecl *FD = isa<FunctionDecl>(C)
                               ? cast<FunctionDecl>(C)
                               : dyn_cast_or_null<FunctionDecl>(D);

  auto CheckDeviceType = [&](QualType Ty) {
    if (Ty->isDependentType())
      return;

    if (Ty->isBitIntType()) {
      if (!Context.getTargetInfo().hasBitIntType()) {
        PartialDiagnostic PD = PDiag(diag::err_target_unsupported_type);
        if (D)
          PD << D;
        else
          PD << "expression";
        targetDiag(Loc, PD, FD)
            << false /*show bit size*/ << 0 /*bitsize*/ << false /*return*/
            << Ty << Context.getTargetInfo().getTriple().str();
      }
      return;
    }

    // Check if we are dealing with two 'long double' but with different
    // semantics.
    bool LongDoubleMismatched = false;
    if (Ty->isRealFloatingType() && Context.getTypeSize(Ty) == 128) {
      const llvm::fltSemantics &Sem = Context.getFloatTypeSemantics(Ty);
      if ((&Sem != &llvm::APFloat::PPCDoubleDouble() &&
           !Context.getTargetInfo().hasFloat128Type()) ||
          (&Sem == &llvm::APFloat::PPCDoubleDouble() &&
           !Context.getTargetInfo().hasIbm128Type()))
        LongDoubleMismatched = true;
    }

    if ((Ty->isFloat16Type() && !Context.getTargetInfo().hasFloat16Type()) ||
        (Ty->isFloat128Type() && !Context.getTargetInfo().hasFloat128Type()) ||
        (Ty->isIbm128Type() && !Context.getTargetInfo().hasIbm128Type()) ||
        (Ty->isIntegerType() && Context.getTypeSize(Ty) == 128 &&
         !Context.getTargetInfo().hasInt128Type()) ||
        (Ty->isBFloat16Type() && !Context.getTargetInfo().hasBFloat16Type() &&
         !LangOpts.CUDAIsDevice) ||
        LongDoubleMismatched) {
      PartialDiagnostic PD = PDiag(diag::err_target_unsupported_type);
      if (D)
        PD << D;
      else
        PD << "expression";

      if (targetDiag(Loc, PD, FD)
          << true /*show bit size*/
          << static_cast<unsigned>(Context.getTypeSize(Ty)) << Ty
          << false /*return*/ << Context.getTargetInfo().getTriple().str()) {
        if (D)
          D->setInvalidDecl();
      }
      if (D)
        targetDiag(D->getLocation(), diag::note_defined_here, FD) << D;
    }
  };

  auto CheckType = [&](QualType Ty, bool IsRetTy = false) {
    if (LangOpts.SYCLIsDevice ||
        (LangOpts.OpenMP && LangOpts.OpenMPIsTargetDevice) ||
        LangOpts.CUDAIsDevice)
      CheckDeviceType(Ty);

    QualType UnqualTy = Ty.getCanonicalType().getUnqualifiedType();
    const TargetInfo &TI = Context.getTargetInfo();
    if (!TI.hasLongDoubleType() && UnqualTy == Context.LongDoubleTy) {
      PartialDiagnostic PD = PDiag(diag::err_target_unsupported_type);
      if (D)
        PD << D;
      else
        PD << "expression";

      if (Diag(Loc, PD, FD)
          << false /*show bit size*/ << 0 << Ty << false /*return*/
          << TI.getTriple().str()) {
        if (D)
          D->setInvalidDecl();
      }
      if (D)
        targetDiag(D->getLocation(), diag::note_defined_here, FD) << D;
    }

    bool IsDouble = UnqualTy == Context.DoubleTy;
    bool IsFloat = UnqualTy == Context.FloatTy;
    if (IsRetTy && !TI.hasFPReturn() && (IsDouble || IsFloat)) {
      PartialDiagnostic PD = PDiag(diag::err_target_unsupported_type);
      if (D)
        PD << D;
      else
        PD << "expression";

      if (Diag(Loc, PD, FD)
          << false /*show bit size*/ << 0 << Ty << true /*return*/
          << TI.getTriple().str()) {
        if (D)
          D->setInvalidDecl();
      }
      if (D)
        targetDiag(D->getLocation(), diag::note_defined_here, FD) << D;
    }

    if (TI.hasRISCVVTypes() && Ty->isRVVSizelessBuiltinType() && FD) {
      llvm::StringMap<bool> CallerFeatureMap;
      Context.getFunctionFeatureMap(CallerFeatureMap, FD);
      RISCV().checkRVVTypeSupport(Ty, Loc, D, CallerFeatureMap);
    }

    // Don't allow SVE types in functions without a SVE target.
    if (Ty->isSVESizelessBuiltinType() && FD) {
      llvm::StringMap<bool> CallerFeatureMap;
      Context.getFunctionFeatureMap(CallerFeatureMap, FD);
      if (!Builtin::evaluateRequiredTargetFeatures("sve", CallerFeatureMap)) {
        if (!Builtin::evaluateRequiredTargetFeatures("sme", CallerFeatureMap))
          Diag(Loc, diag::err_sve_vector_in_non_sve_target) << Ty;
        else if (!IsArmStreamingFunction(FD,
                                         /*IncludeLocallyStreaming=*/true)) {
          Diag(Loc, diag::err_sve_vector_in_non_streaming_function) << Ty;
        }
      }
    }
  };

  CheckType(Ty);
  if (const auto *FPTy = dyn_cast<FunctionProtoType>(Ty)) {
    for (const auto &ParamTy : FPTy->param_types())
      CheckType(ParamTy);
    CheckType(FPTy->getReturnType(), /*IsRetTy=*/true);
  }
  if (const auto *FNPTy = dyn_cast<FunctionNoProtoType>(Ty))
    CheckType(FNPTy->getReturnType(), /*IsRetTy=*/true);
}

bool Sema::findMacroSpelling(SourceLocation &locref, StringRef name) {
  SourceLocation loc = locref;
  if (!loc.isMacroID()) return false;

  // There's no good way right now to look at the intermediate
  // expansions, so just jump to the expansion location.
  loc = getSourceManager().getExpansionLoc(loc);

  // If that's written with the name, stop here.
  SmallString<16> buffer;
  if (getPreprocessor().getSpelling(loc, buffer) == name) {
    locref = loc;
    return true;
  }
  return false;
}

Scope *Sema::getScopeForContext(DeclContext *Ctx) {

  if (!Ctx)
    return nullptr;

  Ctx = Ctx->getPrimaryContext();
  for (Scope *S = getCurScope(); S; S = S->getParent()) {
    // Ignore scopes that cannot have declarations. This is important for
    // out-of-line definitions of static class members.
    if (S->getFlags() & (Scope::DeclScope | Scope::TemplateParamScope))
      if (DeclContext *Entity = S->getEntity())
        if (Ctx == Entity->getPrimaryContext())
          return S;
  }

  return nullptr;
}

/// Enter a new function scope
void Sema::PushFunctionScope() {
  if (FunctionScopes.empty() && CachedFunctionScope) {
    // Use CachedFunctionScope to avoid allocating memory when possible.
    CachedFunctionScope->Clear();
    FunctionScopes.push_back(CachedFunctionScope.release());
  } else {
    FunctionScopes.push_back(new FunctionScopeInfo(getDiagnostics()));
  }
  if (LangOpts.OpenMP)
    OpenMP().pushOpenMPFunctionRegion();
}

void Sema::PushBlockScope(Scope *BlockScope, BlockDecl *Block) {
  FunctionScopes.push_back(new BlockScopeInfo(getDiagnostics(),
                                              BlockScope, Block));
  CapturingFunctionScopes++;
}

LambdaScopeInfo *Sema::PushLambdaScope() {
  LambdaScopeInfo *const LSI = new LambdaScopeInfo(getDiagnostics());
  FunctionScopes.push_back(LSI);
  CapturingFunctionScopes++;
  return LSI;
}

void Sema::RecordParsingTemplateParameterDepth(unsigned Depth) {
  if (LambdaScopeInfo *const LSI = getCurLambda()) {
    LSI->AutoTemplateParameterDepth = Depth;
    return;
  }
  llvm_unreachable(
      "Remove assertion if intentionally called in a non-lambda context.");
}

// Check that the type of the VarDecl has an accessible copy constructor and
// resolve its destructor's exception specification.
// This also performs initialization of block variables when they are moved
// to the heap. It uses the same rules as applicable for implicit moves
// according to the C++ standard in effect ([class.copy.elision]p3).
static void checkEscapingByref(VarDecl *VD, Sema &S) {
  QualType T = VD->getType();
  EnterExpressionEvaluationContext scope(
      S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
  SourceLocation Loc = VD->getLocation();
  Expr *VarRef =
      new (S.Context) DeclRefExpr(S.Context, VD, false, T, VK_LValue, Loc);
  ExprResult Result;
  auto IE = InitializedEntity::InitializeBlock(Loc, T);
  if (S.getLangOpts().CPlusPlus23) {
    auto *E = ImplicitCastExpr::Create(S.Context, T, CK_NoOp, VarRef, nullptr,
                                       VK_XValue, FPOptionsOverride());
    Result = S.PerformCopyInitialization(IE, SourceLocation(), E);
  } else {
    Result = S.PerformMoveOrCopyInitialization(
        IE, Sema::NamedReturnInfo{VD, Sema::NamedReturnInfo::MoveEligible},
        VarRef);
  }

  if (!Result.isInvalid()) {
    Result = S.MaybeCreateExprWithCleanups(Result);
    Expr *Init = Result.getAs<Expr>();
    S.Context.setBlockVarCopyInit(VD, Init, S.canThrow(Init));
  }

  // The destructor's exception specification is needed when IRGen generates
  // block copy/destroy functions. Resolve it here.
  if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
    if (CXXDestructorDecl *DD = RD->getDestructor()) {
      auto *FPT = DD->getType()->castAs<FunctionProtoType>();
      S.ResolveExceptionSpec(Loc, FPT);
    }
}

static void markEscapingByrefs(const FunctionScopeInfo &FSI, Sema &S) {
  // Set the EscapingByref flag of __block variables captured by
  // escaping blocks.
  for (const BlockDecl *BD : FSI.Blocks) {
    for (const BlockDecl::Capture &BC : BD->captures()) {
      VarDecl *VD = BC.getVariable();
      if (VD->hasAttr<BlocksAttr>()) {
        // Nothing to do if this is a __block variable captured by a
        // non-escaping block.
        if (BD->doesNotEscape())
          continue;
        VD->setEscapingByref();
      }
      // Check whether the captured variable is or contains an object of
      // non-trivial C union type.
      QualType CapType = BC.getVariable()->getType();
      if (CapType.hasNonTrivialToPrimitiveDestructCUnion() ||
          CapType.hasNonTrivialToPrimitiveCopyCUnion())
        S.checkNonTrivialCUnion(BC.getVariable()->getType(),
                                BD->getCaretLocation(),
                                Sema::NTCUC_BlockCapture,
                                Sema::NTCUK_Destruct|Sema::NTCUK_Copy);
    }
  }

  for (VarDecl *VD : FSI.ByrefBlockVars) {
    // __block variables might require us to capture a copy-initializer.
    if (!VD->isEscapingByref())
      continue;
    // It's currently invalid to ever have a __block variable with an
    // array type; should we diagnose that here?
    // Regardless, we don't want to ignore array nesting when
    // constructing this copy.
    if (VD->getType()->isStructureOrClassType())
      checkEscapingByref(VD, S);
  }
}

Sema::PoppedFunctionScopePtr
Sema::PopFunctionScopeInfo(const AnalysisBasedWarnings::Policy *WP,
                           const Decl *D, QualType BlockType) {
  assert(!FunctionScopes.empty() && "mismatched push/pop!");

  markEscapingByrefs(*FunctionScopes.back(), *this);

  PoppedFunctionScopePtr Scope(FunctionScopes.pop_back_val(),
                               PoppedFunctionScopeDeleter(this));

  if (LangOpts.OpenMP)
    OpenMP().popOpenMPFunctionRegion(Scope.get());

  // Issue any analysis-based warnings.
  if (WP && D)
    AnalysisWarnings.IssueWarnings(*WP, Scope.get(), D, BlockType);
  else
    for (const auto &PUD : Scope->PossiblyUnreachableDiags)
      Diag(PUD.Loc, PUD.PD);

  return Scope;
}

void Sema::PoppedFunctionScopeDeleter::
operator()(sema::FunctionScopeInfo *Scope) const {
  if (!Scope->isPlainFunction())
    Self->CapturingFunctionScopes--;
  // Stash the function scope for later reuse if it's for a normal function.
  if (Scope->isPlainFunction() && !Self->CachedFunctionScope)
    Self->CachedFunctionScope.reset(Scope);
  else
    delete Scope;
}

void Sema::PushCompoundScope(bool IsStmtExpr) {
  getCurFunction()->CompoundScopes.push_back(
      CompoundScopeInfo(IsStmtExpr, getCurFPFeatures()));
}

void Sema::PopCompoundScope() {
  FunctionScopeInfo *CurFunction = getCurFunction();
  assert(!CurFunction->CompoundScopes.empty() && "mismatched push/pop");

  CurFunction->CompoundScopes.pop_back();
}

bool Sema::hasAnyUnrecoverableErrorsInThisFunction() const {
  return getCurFunction()->hasUnrecoverableErrorOccurred();
}

void Sema::setFunctionHasBranchIntoScope() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasBranchIntoScope();
}

void Sema::setFunctionHasBranchProtectedScope() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasBranchProtectedScope();
}

void Sema::setFunctionHasIndirectGoto() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasIndirectGoto();
}

void Sema::setFunctionHasMustTail() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasMustTail();
}

BlockScopeInfo *Sema::getCurBlock() {
  if (FunctionScopes.empty())
    return nullptr;

  auto CurBSI = dyn_cast<BlockScopeInfo>(FunctionScopes.back());
  if (CurBSI && CurBSI->TheDecl &&
      !CurBSI->TheDecl->Encloses(CurContext)) {
    // We have switched contexts due to template instantiation.
    assert(!CodeSynthesisContexts.empty());
    return nullptr;
  }

  return CurBSI;
}

FunctionScopeInfo *Sema::getEnclosingFunction() const {
  if (FunctionScopes.empty())
    return nullptr;

  for (int e = FunctionScopes.size() - 1; e >= 0; --e) {
    if (isa<sema::BlockScopeInfo>(FunctionScopes[e]))
      continue;
    return FunctionScopes[e];
  }
  return nullptr;
}

LambdaScopeInfo *Sema::getEnclosingLambda() const {
  for (auto *Scope : llvm::reverse(FunctionScopes)) {
    if (auto *LSI = dyn_cast<sema::LambdaScopeInfo>(Scope)) {
      if (LSI->Lambda && !LSI->Lambda->Encloses(CurContext) &&
          LSI->AfterParameterList) {
        // We have switched contexts due to template instantiation.
        // FIXME: We should swap out the FunctionScopes during code synthesis
        // so that we don't need to check for this.
        assert(!CodeSynthesisContexts.empty());
        return nullptr;
      }
      return LSI;
    }
  }
  return nullptr;
}

LambdaScopeInfo *Sema::getCurLambda(bool IgnoreNonLambdaCapturingScope) {
  if (FunctionScopes.empty())
    return nullptr;

  auto I = FunctionScopes.rbegin();
  if (IgnoreNonLambdaCapturingScope) {
    auto E = FunctionScopes.rend();
    while (I != E && isa<CapturingScopeInfo>(*I) && !isa<LambdaScopeInfo>(*I))
      ++I;
    if (I == E)
      return nullptr;
  }
  auto *CurLSI = dyn_cast<LambdaScopeInfo>(*I);
  if (CurLSI && CurLSI->Lambda && CurLSI->CallOperator &&
      !CurLSI->Lambda->Encloses(CurContext) && CurLSI->AfterParameterList) {
    // We have switched contexts due to template instantiation.
    assert(!CodeSynthesisContexts.empty());
    return nullptr;
  }

  return CurLSI;
}

// We have a generic lambda if we parsed auto parameters, or we have
// an associated template parameter list.
LambdaScopeInfo *Sema::getCurGenericLambda() {
  if (LambdaScopeInfo *LSI =  getCurLambda()) {
    return (LSI->TemplateParams.size() ||
                    LSI->GLTemplateParameterList) ? LSI : nullptr;
  }
  return nullptr;
}


void Sema::ActOnComment(SourceRange Comment) {
  if (!LangOpts.RetainCommentsFromSystemHeaders &&
      SourceMgr.isInSystemHeader(Comment.getBegin()))
    return;
  RawComment RC(SourceMgr, Comment, LangOpts.CommentOpts, false);
  if (RC.isAlmostTrailingComment() || RC.hasUnsupportedSplice(SourceMgr)) {
    SourceRange MagicMarkerRange(Comment.getBegin(),
                                 Comment.getBegin().getLocWithOffset(3));
    StringRef MagicMarkerText;
    switch (RC.getKind()) {
    case RawComment::RCK_OrdinaryBCPL:
      MagicMarkerText = "///<";
      break;
    case RawComment::RCK_OrdinaryC:
      MagicMarkerText = "/**<";
      break;
    case RawComment::RCK_Invalid:
      // FIXME: are there other scenarios that could produce an invalid
      // raw comment here?
      Diag(Comment.getBegin(), diag::warn_splice_in_doxygen_comment);
      return;
    default:
      llvm_unreachable("if this is an almost Doxygen comment, "
                       "it should be ordinary");
    }
    Diag(Comment.getBegin(), diag::warn_not_a_doxygen_trailing_member_comment) <<
      FixItHint::CreateReplacement(MagicMarkerRange, MagicMarkerText);
  }
  Context.addComment(RC);
}

// Pin this vtable to this file.
ExternalSemaSource::~ExternalSemaSource() {}
char ExternalSemaSource::ID;

void ExternalSemaSource::ReadMethodPool(Selector Sel) { }
void ExternalSemaSource::updateOutOfDateSelector(Selector Sel) { }

void ExternalSemaSource::ReadKnownNamespaces(
                           SmallVectorImpl<NamespaceDecl *> &Namespaces) {
}

void ExternalSemaSource::ReadUndefinedButUsed(
    llvm::MapVector<NamedDecl *, SourceLocation> &Undefined) {}

void ExternalSemaSource::ReadMismatchingDeleteExpressions(llvm::MapVector<
    FieldDecl *, llvm::SmallVector<std::pair<SourceLocation, bool>, 4>> &) {}

bool Sema::tryExprAsCall(Expr &E, QualType &ZeroArgCallReturnTy,
                         UnresolvedSetImpl &OverloadSet) {
  ZeroArgCallReturnTy = QualType();
  OverloadSet.clear();

  const OverloadExpr *Overloads = nullptr;
  bool IsMemExpr = false;
  if (E.getType() == Context.OverloadTy) {
    OverloadExpr::FindResult FR = OverloadExpr::find(&E);

    // Ignore overloads that are pointer-to-member constants.
    if (FR.HasFormOfMemberPointer)
      return false;

    Overloads = FR.Expression;
  } else if (E.getType() == Context.BoundMemberTy) {
    Overloads = dyn_cast<UnresolvedMemberExpr>(E.IgnoreParens());
    IsMemExpr = true;
  }

  bool Ambiguous = false;
  bool IsMV = false;

  if (Overloads) {
    for (OverloadExpr::decls_iterator it = Overloads->decls_begin(),
         DeclsEnd = Overloads->decls_end(); it != DeclsEnd; ++it) {
      OverloadSet.addDecl(*it);

      // Check whether the function is a non-template, non-member which takes no
      // arguments.
      if (IsMemExpr)
        continue;
      if (const FunctionDecl *OverloadDecl
            = dyn_cast<FunctionDecl>((*it)->getUnderlyingDecl())) {
        if (OverloadDecl->getMinRequiredArguments() == 0) {
          if (!ZeroArgCallReturnTy.isNull() && !Ambiguous &&
              (!IsMV || !(OverloadDecl->isCPUDispatchMultiVersion() ||
                          OverloadDecl->isCPUSpecificMultiVersion()))) {
            ZeroArgCallReturnTy = QualType();
            Ambiguous = true;
          } else {
            ZeroArgCallReturnTy = OverloadDecl->getReturnType();
            IsMV = OverloadDecl->isCPUDispatchMultiVersion() ||
                   OverloadDecl->isCPUSpecificMultiVersion();
          }
        }
      }
    }

    // If it's not a member, use better machinery to try to resolve the call
    if (!IsMemExpr)
      return !ZeroArgCallReturnTy.isNull();
  }

  // Attempt to call the member with no arguments - this will correctly handle
  // member templates with defaults/deduction of template arguments, overloads
  // with default arguments, etc.
  if (IsMemExpr && !E.isTypeDependent()) {
    Sema::TentativeAnalysisScope Trap(*this);
    ExprResult R = BuildCallToMemberFunction(nullptr, &E, SourceLocation(),
                                             std::nullopt, SourceLocation());
    if (R.isUsable()) {
      ZeroArgCallReturnTy = R.get()->getType();
      return true;
    }
    return false;
  }

  if (const auto *DeclRef = dyn_cast<DeclRefExpr>(E.IgnoreParens())) {
    if (const auto *Fun = dyn_cast<FunctionDecl>(DeclRef->getDecl())) {
      if (Fun->getMinRequiredArguments() == 0)
        ZeroArgCallReturnTy = Fun->getReturnType();
      return true;
    }
  }

  // We don't have an expression that's convenient to get a FunctionDecl from,
  // but we can at least check if the type is "function of 0 arguments".
  QualType ExprTy = E.getType();
  const FunctionType *FunTy = nullptr;
  QualType PointeeTy = ExprTy->getPointeeType();
  if (!PointeeTy.isNull())
    FunTy = PointeeTy->getAs<FunctionType>();
  if (!FunTy)
    FunTy = ExprTy->getAs<FunctionType>();

  if (const auto *FPT = dyn_cast_if_present<FunctionProtoType>(FunTy)) {
    if (FPT->getNumParams() == 0)
      ZeroArgCallReturnTy = FunTy->getReturnType();
    return true;
  }
  return false;
}

/// Give notes for a set of overloads.
///
/// A companion to tryExprAsCall. In cases when the name that the programmer
/// wrote was an overloaded function, we may be able to make some guesses about
/// plausible overloads based on their return types; such guesses can be handed
/// off to this method to be emitted as notes.
///
/// \param Overloads - The overloads to note.
/// \param FinalNoteLoc - If we've suppressed printing some overloads due to
///  -fshow-overloads=best, this is the location to attach to the note about too
///  many candidates. Typically this will be the location of the original
///  ill-formed expression.
static void noteOverloads(Sema &S, const UnresolvedSetImpl &Overloads,
                          const SourceLocation FinalNoteLoc) {
  unsigned ShownOverloads = 0;
  unsigned SuppressedOverloads = 0;
  for (UnresolvedSetImpl::iterator It = Overloads.begin(),
       DeclsEnd = Overloads.end(); It != DeclsEnd; ++It) {
    if (ShownOverloads >= S.Diags.getNumOverloadCandidatesToShow()) {
      ++SuppressedOverloads;
      continue;
    }

    const NamedDecl *Fn = (*It)->getUnderlyingDecl();
    // Don't print overloads for non-default multiversioned functions.
    if (const auto *FD = Fn->getAsFunction()) {
      if (FD->isMultiVersion() && FD->hasAttr<TargetAttr>() &&
          !FD->getAttr<TargetAttr>()->isDefaultVersion())
        continue;
      if (FD->isMultiVersion() && FD->hasAttr<TargetVersionAttr>() &&
          !FD->getAttr<TargetVersionAttr>()->isDefaultVersion())
        continue;
    }
    S.Diag(Fn->getLocation(), diag::note_possible_target_of_call);
    ++ShownOverloads;
  }

  S.Diags.overloadCandidatesShown(ShownOverloads);

  if (SuppressedOverloads)
    S.Diag(FinalNoteLoc, diag::note_ovl_too_many_candidates)
      << SuppressedOverloads;
}

static void notePlausibleOverloads(Sema &S, SourceLocation Loc,
                                   const UnresolvedSetImpl &Overloads,
                                   bool (*IsPlausibleResult)(QualType)) {
  if (!IsPlausibleResult)
    return noteOverloads(S, Overloads, Loc);

  UnresolvedSet<2> PlausibleOverloads;
  for (OverloadExpr::decls_iterator It = Overloads.begin(),
         DeclsEnd = Overloads.end(); It != DeclsEnd; ++It) {
    const auto *OverloadDecl = cast<FunctionDecl>(*It);
    QualType OverloadResultTy = OverloadDecl->getReturnType();
    if (IsPlausibleResult(OverloadResultTy))
      PlausibleOverloads.addDecl(It.getDecl());
  }
  noteOverloads(S, PlausibleOverloads, Loc);
}

/// Determine whether the given expression can be called by just
/// putting parentheses after it.  Notably, expressions with unary
/// operators can't be because the unary operator will start parsing
/// outside the call.
static bool IsCallableWithAppend(const Expr *E) {
  E = E->IgnoreImplicit();
  return (!isa<CStyleCastExpr>(E) &&
          !isa<UnaryOperator>(E) &&
          !isa<BinaryOperator>(E) &&
          !isa<CXXOperatorCallExpr>(E));
}

static bool IsCPUDispatchCPUSpecificMultiVersion(const Expr *E) {
  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    E = UO->getSubExpr();

  if (const auto *ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
    if (ULE->getNumDecls() == 0)
      return false;

    const NamedDecl *ND = *ULE->decls_begin();
    if (const auto *FD = dyn_cast<FunctionDecl>(ND))
      return FD->isCPUDispatchMultiVersion() || FD->isCPUSpecificMultiVersion();
  }
  return false;
}

bool Sema::tryToRecoverWithCall(ExprResult &E, const PartialDiagnostic &PD,
                                bool ForceComplain,
                                bool (*IsPlausibleResult)(QualType)) {
  SourceLocation Loc = E.get()->getExprLoc();
  SourceRange Range = E.get()->getSourceRange();
  UnresolvedSet<4> Overloads;

  // If this is a SFINAE context, don't try anything that might trigger ADL
  // prematurely.
  if (!isSFINAEContext()) {
    QualType ZeroArgCallTy;
    if (tryExprAsCall(*E.get(), ZeroArgCallTy, Overloads) &&
        !ZeroArgCallTy.isNull() &&
        (!IsPlausibleResult || IsPlausibleResult(ZeroArgCallTy))) {
      // At this point, we know E is potentially callable with 0
      // arguments and that it returns something of a reasonable type,
      // so we can emit a fixit and carry on pretending that E was
      // actually a CallExpr.
      SourceLocation ParenInsertionLoc = getLocForEndOfToken(Range.getEnd());
      bool IsMV = IsCPUDispatchCPUSpecificMultiVersion(E.get());
      Diag(Loc, PD) << /*zero-arg*/ 1 << IsMV << Range
                    << (IsCallableWithAppend(E.get())
                            ? FixItHint::CreateInsertion(ParenInsertionLoc,
                                                         "()")
                            : FixItHint());
      if (!IsMV)
        notePlausibleOverloads(*this, Loc, Overloads, IsPlausibleResult);

      // FIXME: Try this before emitting the fixit, and suppress diagnostics
      // while doing so.
      E = BuildCallExpr(nullptr, E.get(), Range.getEnd(), std::nullopt,
                        Range.getEnd().getLocWithOffset(1));
      return true;
    }
  }
  if (!ForceComplain) return false;

  bool IsMV = IsCPUDispatchCPUSpecificMultiVersion(E.get());
  Diag(Loc, PD) << /*not zero-arg*/ 0 << IsMV << Range;
  if (!IsMV)
    notePlausibleOverloads(*this, Loc, Overloads, IsPlausibleResult);
  E = ExprError();
  return true;
}

IdentifierInfo *Sema::getSuperIdentifier() const {
  if (!Ident_super)
    Ident_super = &Context.Idents.get("super");
  return Ident_super;
}

void Sema::PushCapturedRegionScope(Scope *S, CapturedDecl *CD, RecordDecl *RD,
                                   CapturedRegionKind K,
                                   unsigned OpenMPCaptureLevel) {
  auto *CSI = new CapturedRegionScopeInfo(
      getDiagnostics(), S, CD, RD, CD->getContextParam(), K,
      (getLangOpts().OpenMP && K == CR_OpenMP)
          ? OpenMP().getOpenMPNestingLevel()
          : 0,
      OpenMPCaptureLevel);
  CSI->ReturnType = Context.VoidTy;
  FunctionScopes.push_back(CSI);
  CapturingFunctionScopes++;
}

CapturedRegionScopeInfo *Sema::getCurCapturedRegion() {
  if (FunctionScopes.empty())
    return nullptr;

  return dyn_cast<CapturedRegionScopeInfo>(FunctionScopes.back());
}

const llvm::MapVector<FieldDecl *, Sema::DeleteLocs> &
Sema::getMismatchingDeleteExpressions() const {
  return DeleteExprs;
}

Sema::FPFeaturesStateRAII::FPFeaturesStateRAII(Sema &S)
    : S(S), OldFPFeaturesState(S.CurFPFeatures),
      OldOverrides(S.FpPragmaStack.CurrentValue),
      OldEvalMethod(S.PP.getCurrentFPEvalMethod()),
      OldFPPragmaLocation(S.PP.getLastFPEvalPragmaLocation()) {}

Sema::FPFeaturesStateRAII::~FPFeaturesStateRAII() {
  S.CurFPFeatures = OldFPFeaturesState;
  S.FpPragmaStack.CurrentValue = OldOverrides;
  S.PP.setCurrentFPEvalMethod(OldFPPragmaLocation, OldEvalMethod);
}

bool Sema::isDeclaratorFunctionLike(Declarator &D) {
  assert(D.getCXXScopeSpec().isSet() &&
         "can only be called for qualified names");

  auto LR = LookupResult(*this, D.getIdentifier(), D.getBeginLoc(),
                         LookupOrdinaryName, forRedeclarationInCurContext());
  DeclContext *DC = computeDeclContext(D.getCXXScopeSpec(),
                                       !D.getDeclSpec().isFriendSpecified());
  if (!DC)
    return false;

  LookupQualifiedName(LR, DC);
  bool Result = llvm::all_of(LR, [](Decl *Dcl) {
    if (NamedDecl *ND = dyn_cast<NamedDecl>(Dcl)) {
      ND = ND->getUnderlyingDecl();
      return isa<FunctionDecl>(ND) || isa<FunctionTemplateDecl>(ND) ||
             isa<UsingDecl>(ND);
    }
    return false;
  });
  return Result;
}

FunctionEffectDifferences::FunctionEffectDifferences(
    const FunctionEffectsRef &Old, const FunctionEffectsRef &New) {

  FunctionEffectsRef::iterator POld = Old.begin();
  FunctionEffectsRef::iterator OldEnd = Old.end();
  FunctionEffectsRef::iterator PNew = New.begin();
  FunctionEffectsRef::iterator NewEnd = New.end();

  while (true) {
    int cmp = 0;
    if (POld == OldEnd) {
      if (PNew == NewEnd)
        break;
      cmp = 1;
    } else if (PNew == NewEnd)
      cmp = -1;
    else {
      FunctionEffectWithCondition Old = *POld;
      FunctionEffectWithCondition New = *PNew;
      if (Old.Effect.kind() < New.Effect.kind())
        cmp = -1;
      else if (New.Effect.kind() < Old.Effect.kind())
        cmp = 1;
      else {
        cmp = 0;
        if (Old.Cond.getCondition() != New.Cond.getCondition()) {
          // FIXME: Cases where the expressions are equivalent but
          // don't have the same identity.
          push_back(FunctionEffectDiff{
              Old.Effect.kind(), FunctionEffectDiff::Kind::ConditionMismatch,
              Old, New});
        }
      }
    }

    if (cmp < 0) {
      // removal
      FunctionEffectWithCondition Old = *POld;
      push_back(FunctionEffectDiff{
          Old.Effect.kind(), FunctionEffectDiff::Kind::Removed, Old, {}});
      ++POld;
    } else if (cmp > 0) {
      // addition
      FunctionEffectWithCondition New = *PNew;
      push_back(FunctionEffectDiff{
          New.Effect.kind(), FunctionEffectDiff::Kind::Added, {}, New});
      ++PNew;
    } else {
      ++POld;
      ++PNew;
    }
  }
}

bool FunctionEffectDiff::shouldDiagnoseConversion(
    QualType SrcType, const FunctionEffectsRef &SrcFX, QualType DstType,
    const FunctionEffectsRef &DstFX) const {

  switch (EffectKind) {
  case FunctionEffect::Kind::NonAllocating:
    // nonallocating can't be added (spoofed) during a conversion, unless we
    // have nonblocking.
    if (DiffKind == Kind::Added) {
      for (const auto &CFE : SrcFX) {
        if (CFE.Effect.kind() == FunctionEffect::Kind::NonBlocking)
          return false;
      }
    }
    [[fallthrough]];
  case FunctionEffect::Kind::NonBlocking:
    // nonblocking can't be added (spoofed) during a conversion.
    switch (DiffKind) {
    case Kind::Added:
      return true;
    case Kind::Removed:
      return false;
    case Kind::ConditionMismatch:
      // FIXME: Condition mismatches are too coarse right now -- expressions
      // which are equivalent but don't have the same identity are detected as
      // mismatches. We're going to diagnose those anyhow until expression
      // matching is better.
      return true;
    }
  case FunctionEffect::Kind::Blocking:
  case FunctionEffect::Kind::Allocating:
    return false;
  case FunctionEffect::Kind::None:
    break;
  }
  llvm_unreachable("unknown effect kind");
}

bool FunctionEffectDiff::shouldDiagnoseRedeclaration(
    const FunctionDecl &OldFunction, const FunctionEffectsRef &OldFX,
    const FunctionDecl &NewFunction, const FunctionEffectsRef &NewFX) const {
  switch (EffectKind) {
  case FunctionEffect::Kind::NonAllocating:
  case FunctionEffect::Kind::NonBlocking:
    // nonblocking/nonallocating can't be removed in a redeclaration.
    switch (DiffKind) {
    case Kind::Added:
      return false; // No diagnostic.
    case Kind::Removed:
      return true; // Issue diagnostic.
    case Kind::ConditionMismatch:
      // All these forms of mismatches are diagnosed.
      return true;
    }
  case FunctionEffect::Kind::Blocking:
  case FunctionEffect::Kind::Allocating:
    return false;
  case FunctionEffect::Kind::None:
    break;
  }
  llvm_unreachable("unknown effect kind");
}

FunctionEffectDiff::OverrideResult
FunctionEffectDiff::shouldDiagnoseMethodOverride(
    const CXXMethodDecl &OldMethod, const FunctionEffectsRef &OldFX,
    const CXXMethodDecl &NewMethod, const FunctionEffectsRef &NewFX) const {
  switch (EffectKind) {
  case FunctionEffect::Kind::NonAllocating:
  case FunctionEffect::Kind::NonBlocking:
    switch (DiffKind) {

    // If added on an override, that's fine and not diagnosed.
    case Kind::Added:
      return OverrideResult::NoAction;

    // If missing from an override (removed), propagate from base to derived.
    case Kind::Removed:
      return OverrideResult::Merge;

    // If there's a mismatch involving the effect's polarity or condition,
    // issue a warning.
    case Kind::ConditionMismatch:
      return OverrideResult::Warn;
    }

  case FunctionEffect::Kind::Blocking:
  case FunctionEffect::Kind::Allocating:
    return OverrideResult::NoAction;

  case FunctionEffect::Kind::None:
    break;
  }
  llvm_unreachable("unknown effect kind");
}
