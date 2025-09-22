//===- FunctionImport.cpp - ThinLTO Summary-based Function Import ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Function import based on summaries.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/IRMover.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "function-import"

STATISTIC(NumImportedFunctionsThinLink,
          "Number of functions thin link decided to import");
STATISTIC(NumImportedHotFunctionsThinLink,
          "Number of hot functions thin link decided to import");
STATISTIC(NumImportedCriticalFunctionsThinLink,
          "Number of critical functions thin link decided to import");
STATISTIC(NumImportedGlobalVarsThinLink,
          "Number of global variables thin link decided to import");
STATISTIC(NumImportedFunctions, "Number of functions imported in backend");
STATISTIC(NumImportedGlobalVars,
          "Number of global variables imported in backend");
STATISTIC(NumImportedModules, "Number of modules imported from");
STATISTIC(NumDeadSymbols, "Number of dead stripped symbols in index");
STATISTIC(NumLiveSymbols, "Number of live symbols in index");

/// Limit on instruction count of imported functions.
static cl::opt<unsigned> ImportInstrLimit(
    "import-instr-limit", cl::init(100), cl::Hidden, cl::value_desc("N"),
    cl::desc("Only import functions with less than N instructions"));

static cl::opt<int> ImportCutoff(
    "import-cutoff", cl::init(-1), cl::Hidden, cl::value_desc("N"),
    cl::desc("Only import first N functions if N>=0 (default -1)"));

static cl::opt<bool>
    ForceImportAll("force-import-all", cl::init(false), cl::Hidden,
                   cl::desc("Import functions with noinline attribute"));

static cl::opt<float>
    ImportInstrFactor("import-instr-evolution-factor", cl::init(0.7),
                      cl::Hidden, cl::value_desc("x"),
                      cl::desc("As we import functions, multiply the "
                               "`import-instr-limit` threshold by this factor "
                               "before processing newly imported functions"));

static cl::opt<float> ImportHotInstrFactor(
    "import-hot-evolution-factor", cl::init(1.0), cl::Hidden,
    cl::value_desc("x"),
    cl::desc("As we import functions called from hot callsite, multiply the "
             "`import-instr-limit` threshold by this factor "
             "before processing newly imported functions"));

static cl::opt<float> ImportHotMultiplier(
    "import-hot-multiplier", cl::init(10.0), cl::Hidden, cl::value_desc("x"),
    cl::desc("Multiply the `import-instr-limit` threshold for hot callsites"));

static cl::opt<float> ImportCriticalMultiplier(
    "import-critical-multiplier", cl::init(100.0), cl::Hidden,
    cl::value_desc("x"),
    cl::desc(
        "Multiply the `import-instr-limit` threshold for critical callsites"));

// FIXME: This multiplier was not really tuned up.
static cl::opt<float> ImportColdMultiplier(
    "import-cold-multiplier", cl::init(0), cl::Hidden, cl::value_desc("N"),
    cl::desc("Multiply the `import-instr-limit` threshold for cold callsites"));

static cl::opt<bool> PrintImports("print-imports", cl::init(false), cl::Hidden,
                                  cl::desc("Print imported functions"));

static cl::opt<bool> PrintImportFailures(
    "print-import-failures", cl::init(false), cl::Hidden,
    cl::desc("Print information for functions rejected for importing"));

static cl::opt<bool> ComputeDead("compute-dead", cl::init(true), cl::Hidden,
                                 cl::desc("Compute dead symbols"));

static cl::opt<bool> EnableImportMetadata(
    "enable-import-metadata", cl::init(false), cl::Hidden,
    cl::desc("Enable import metadata like 'thinlto_src_module' and "
             "'thinlto_src_file'"));

/// Summary file to use for function importing when using -function-import from
/// the command line.
static cl::opt<std::string>
    SummaryFile("summary-file",
                cl::desc("The summary file to use for function importing."));

/// Used when testing importing from distributed indexes via opt
// -function-import.
static cl::opt<bool>
    ImportAllIndex("import-all-index",
                   cl::desc("Import all external functions in index."));

/// This is a test-only option.
/// If this option is enabled, the ThinLTO indexing step will import each
/// function declaration as a fallback. In a real build this may increase ram
/// usage of the indexing step unnecessarily.
/// TODO: Implement selective import (based on combined summary analysis) to
/// ensure the imported function has a use case in the postlink pipeline.
static cl::opt<bool> ImportDeclaration(
    "import-declaration", cl::init(false), cl::Hidden,
    cl::desc("If true, import function declaration as fallback if the function "
             "definition is not imported."));

/// Pass a workload description file - an example of workload would be the
/// functions executed to satisfy a RPC request. A workload is defined by a root
/// function and the list of functions that are (frequently) needed to satisfy
/// it. The module that defines the root will have all those functions imported.
/// The file contains a JSON dictionary. The keys are root functions, the values
/// are lists of functions to import in the module defining the root. It is
/// assumed -funique-internal-linkage-names was used, thus ensuring function
/// names are unique even for local linkage ones.
static cl::opt<std::string> WorkloadDefinitions(
    "thinlto-workload-def",
    cl::desc("Pass a workload definition. This is a file containing a JSON "
             "dictionary. The keys are root functions, the values are lists of "
             "functions to import in the module defining the root. It is "
             "assumed -funique-internal-linkage-names was used, to ensure "
             "local linkage functions have unique names. For example: \n"
             "{\n"
             "  \"rootFunction_1\": [\"function_to_import_1\", "
             "\"function_to_import_2\"], \n"
             "  \"rootFunction_2\": [\"function_to_import_3\", "
             "\"function_to_import_4\"] \n"
             "}"),
    cl::Hidden);

namespace llvm {
extern cl::opt<bool> EnableMemProfContextDisambiguation;
}

// Load lazily a module from \p FileName in \p Context.
static std::unique_ptr<Module> loadFile(const std::string &FileName,
                                        LLVMContext &Context) {
  SMDiagnostic Err;
  LLVM_DEBUG(dbgs() << "Loading '" << FileName << "'\n");
  // Metadata isn't loaded until functions are imported, to minimize
  // the memory overhead.
  std::unique_ptr<Module> Result =
      getLazyIRFileModule(FileName, Err, Context,
                          /* ShouldLazyLoadMetadata = */ true);
  if (!Result) {
    Err.print("function-import", errs());
    report_fatal_error("Abort");
  }

  return Result;
}

/// Given a list of possible callee implementation for a call site, qualify the
/// legality of importing each. The return is a range of pairs. Each pair
/// corresponds to a candidate. The first value is the ImportFailureReason for
/// that candidate, the second is the candidate.
static auto qualifyCalleeCandidates(
    const ModuleSummaryIndex &Index,
    ArrayRef<std::unique_ptr<GlobalValueSummary>> CalleeSummaryList,
    StringRef CallerModulePath) {
  return llvm::map_range(
      CalleeSummaryList,
      [&Index, CalleeSummaryList,
       CallerModulePath](const std::unique_ptr<GlobalValueSummary> &SummaryPtr)
          -> std::pair<FunctionImporter::ImportFailureReason,
                       const GlobalValueSummary *> {
        auto *GVSummary = SummaryPtr.get();
        if (!Index.isGlobalValueLive(GVSummary))
          return {FunctionImporter::ImportFailureReason::NotLive, GVSummary};

        if (GlobalValue::isInterposableLinkage(GVSummary->linkage()))
          return {FunctionImporter::ImportFailureReason::InterposableLinkage,
                  GVSummary};

        auto *Summary = dyn_cast<FunctionSummary>(GVSummary->getBaseObject());

        // Ignore any callees that aren't actually functions. This could happen
        // in the case of GUID hash collisions. It could also happen in theory
        // for SamplePGO profiles collected on old versions of the code after
        // renaming, since we synthesize edges to any inlined callees appearing
        // in the profile.
        if (!Summary)
          return {FunctionImporter::ImportFailureReason::GlobalVar, GVSummary};

        // If this is a local function, make sure we import the copy
        // in the caller's module. The only time a local function can
        // share an entry in the index is if there is a local with the same name
        // in another module that had the same source file name (in a different
        // directory), where each was compiled in their own directory so there
        // was not distinguishing path.
        // However, do the import from another module if there is only one
        // entry in the list - in that case this must be a reference due
        // to indirect call profile data, since a function pointer can point to
        // a local in another module.
        if (GlobalValue::isLocalLinkage(Summary->linkage()) &&
            CalleeSummaryList.size() > 1 &&
            Summary->modulePath() != CallerModulePath)
          return {
              FunctionImporter::ImportFailureReason::LocalLinkageNotInModule,
              GVSummary};

        // Skip if it isn't legal to import (e.g. may reference unpromotable
        // locals).
        if (Summary->notEligibleToImport())
          return {FunctionImporter::ImportFailureReason::NotEligible,
                  GVSummary};

        return {FunctionImporter::ImportFailureReason::None, GVSummary};
      });
}

/// Given a list of possible callee implementation for a call site, select one
/// that fits the \p Threshold for function definition import. If none are
/// found, the Reason will give the last reason for the failure (last, in the
/// order of CalleeSummaryList entries). While looking for a callee definition,
/// sets \p TooLargeOrNoInlineSummary to the last seen too-large or noinline
/// candidate; other modules may want to know the function summary or
/// declaration even if a definition is not needed.
///
/// FIXME: select "best" instead of first that fits. But what is "best"?
/// - The smallest: more likely to be inlined.
/// - The one with the least outgoing edges (already well optimized).
/// - One from a module already being imported from in order to reduce the
///   number of source modules parsed/linked.
/// - One that has PGO data attached.
/// - [insert you fancy metric here]
static const GlobalValueSummary *
selectCallee(const ModuleSummaryIndex &Index,
             ArrayRef<std::unique_ptr<GlobalValueSummary>> CalleeSummaryList,
             unsigned Threshold, StringRef CallerModulePath,
             const GlobalValueSummary *&TooLargeOrNoInlineSummary,
             FunctionImporter::ImportFailureReason &Reason) {
  // Records the last summary with reason noinline or too-large.
  TooLargeOrNoInlineSummary = nullptr;
  auto QualifiedCandidates =
      qualifyCalleeCandidates(Index, CalleeSummaryList, CallerModulePath);
  for (auto QualifiedValue : QualifiedCandidates) {
    Reason = QualifiedValue.first;
    // Skip a summary if its import is not (proved to be) legal.
    if (Reason != FunctionImporter::ImportFailureReason::None)
      continue;
    auto *Summary =
        cast<FunctionSummary>(QualifiedValue.second->getBaseObject());

    // Don't bother importing the definition if the chance of inlining it is
    // not high enough (except under `--force-import-all`).
    if ((Summary->instCount() > Threshold) && !Summary->fflags().AlwaysInline &&
        !ForceImportAll) {
      TooLargeOrNoInlineSummary = Summary;
      Reason = FunctionImporter::ImportFailureReason::TooLarge;
      continue;
    }

    // Don't bother importing the definition if we can't inline it anyway.
    if (Summary->fflags().NoInline && !ForceImportAll) {
      TooLargeOrNoInlineSummary = Summary;
      Reason = FunctionImporter::ImportFailureReason::NoInline;
      continue;
    }

    return Summary;
  }
  return nullptr;
}

namespace {

using EdgeInfo = std::tuple<const FunctionSummary *, unsigned /* Threshold */>;

} // anonymous namespace

/// Import globals referenced by a function or other globals that are being
/// imported, if importing such global is possible.
class GlobalsImporter final {
  const ModuleSummaryIndex &Index;
  const GVSummaryMapTy &DefinedGVSummaries;
  function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
      IsPrevailing;
  FunctionImporter::ImportMapTy &ImportList;
  DenseMap<StringRef, FunctionImporter::ExportSetTy> *const ExportLists;

  bool shouldImportGlobal(const ValueInfo &VI) {
    const auto &GVS = DefinedGVSummaries.find(VI.getGUID());
    if (GVS == DefinedGVSummaries.end())
      return true;
    // We should not skip import if the module contains a non-prevailing
    // definition with interposable linkage type. This is required for
    // correctness in the situation where there is a prevailing def available
    // for import and marked read-only. In this case, the non-prevailing def
    // will be converted to a declaration, while the prevailing one becomes
    // internal, thus no definitions will be available for linking. In order to
    // prevent undefined symbol link error, the prevailing definition must be
    // imported.
    // FIXME: Consider adding a check that the suitable prevailing definition
    // exists and marked read-only.
    if (VI.getSummaryList().size() > 1 &&
        GlobalValue::isInterposableLinkage(GVS->second->linkage()) &&
        !IsPrevailing(VI.getGUID(), GVS->second))
      return true;

    return false;
  }

  void
  onImportingSummaryImpl(const GlobalValueSummary &Summary,
                         SmallVectorImpl<const GlobalVarSummary *> &Worklist) {
    for (const auto &VI : Summary.refs()) {
      if (!shouldImportGlobal(VI)) {
        LLVM_DEBUG(
            dbgs() << "Ref ignored! Target already in destination module.\n");
        continue;
      }

      LLVM_DEBUG(dbgs() << " ref -> " << VI << "\n");

      // If this is a local variable, make sure we import the copy
      // in the caller's module. The only time a local variable can
      // share an entry in the index is if there is a local with the same name
      // in another module that had the same source file name (in a different
      // directory), where each was compiled in their own directory so there
      // was not distinguishing path.
      auto LocalNotInModule =
          [&](const GlobalValueSummary *RefSummary) -> bool {
        return GlobalValue::isLocalLinkage(RefSummary->linkage()) &&
               RefSummary->modulePath() != Summary.modulePath();
      };

      for (const auto &RefSummary : VI.getSummaryList()) {
        const auto *GVS = dyn_cast<GlobalVarSummary>(RefSummary.get());
        // Functions could be referenced by global vars - e.g. a vtable; but we
        // don't currently imagine a reason those would be imported here, rather
        // than as part of the logic deciding which functions to import (i.e.
        // based on profile information). Should we decide to handle them here,
        // we can refactor accordingly at that time.
        if (!GVS || !Index.canImportGlobalVar(GVS, /* AnalyzeRefs */ true) ||
            LocalNotInModule(GVS))
          continue;

        // If there isn't an entry for GUID, insert <GUID, Definition> pair.
        // Otherwise, definition should take precedence over declaration.
        auto [Iter, Inserted] =
            ImportList[RefSummary->modulePath()].try_emplace(
                VI.getGUID(), GlobalValueSummary::Definition);
        // Only update stat and exports if we haven't already imported this
        // variable.
        if (!Inserted) {
          // Set the value to 'std::min(existing-value, new-value)' to make
          // sure a definition takes precedence over a declaration.
          Iter->second = std::min(GlobalValueSummary::Definition, Iter->second);
          break;
        }
        NumImportedGlobalVarsThinLink++;
        // Any references made by this variable will be marked exported
        // later, in ComputeCrossModuleImport, after import decisions are
        // complete, which is more efficient than adding them here.
        if (ExportLists)
          (*ExportLists)[RefSummary->modulePath()].insert(VI);

        // If variable is not writeonly we attempt to recursively analyze
        // its references in order to import referenced constants.
        if (!Index.isWriteOnly(GVS))
          Worklist.emplace_back(GVS);
        break;
      }
    }
  }

public:
  GlobalsImporter(
      const ModuleSummaryIndex &Index, const GVSummaryMapTy &DefinedGVSummaries,
      function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
          IsPrevailing,
      FunctionImporter::ImportMapTy &ImportList,
      DenseMap<StringRef, FunctionImporter::ExportSetTy> *ExportLists)
      : Index(Index), DefinedGVSummaries(DefinedGVSummaries),
        IsPrevailing(IsPrevailing), ImportList(ImportList),
        ExportLists(ExportLists) {}

  void onImportingSummary(const GlobalValueSummary &Summary) {
    SmallVector<const GlobalVarSummary *, 128> Worklist;
    onImportingSummaryImpl(Summary, Worklist);
    while (!Worklist.empty())
      onImportingSummaryImpl(*Worklist.pop_back_val(), Worklist);
  }
};

static const char *getFailureName(FunctionImporter::ImportFailureReason Reason);

/// Determine the list of imports and exports for each module.
class ModuleImportsManager {
protected:
  function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
      IsPrevailing;
  const ModuleSummaryIndex &Index;
  DenseMap<StringRef, FunctionImporter::ExportSetTy> *const ExportLists;

  ModuleImportsManager(
      function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
          IsPrevailing,
      const ModuleSummaryIndex &Index,
      DenseMap<StringRef, FunctionImporter::ExportSetTy> *ExportLists = nullptr)
      : IsPrevailing(IsPrevailing), Index(Index), ExportLists(ExportLists) {}

public:
  virtual ~ModuleImportsManager() = default;

  /// Given the list of globals defined in a module, compute the list of imports
  /// as well as the list of "exports", i.e. the list of symbols referenced from
  /// another module (that may require promotion).
  virtual void
  computeImportForModule(const GVSummaryMapTy &DefinedGVSummaries,
                         StringRef ModName,
                         FunctionImporter::ImportMapTy &ImportList);

  static std::unique_ptr<ModuleImportsManager>
  create(function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
             IsPrevailing,
         const ModuleSummaryIndex &Index,
         DenseMap<StringRef, FunctionImporter::ExportSetTy> *ExportLists =
             nullptr);
};

/// A ModuleImportsManager that operates based on a workload definition (see
/// -thinlto-workload-def). For modules that do not define workload roots, it
/// applies the base ModuleImportsManager import policy.
class WorkloadImportsManager : public ModuleImportsManager {
  // Keep a module name -> value infos to import association. We use it to
  // determine if a module's import list should be done by the base
  // ModuleImportsManager or by us.
  StringMap<DenseSet<ValueInfo>> Workloads;

  void
  computeImportForModule(const GVSummaryMapTy &DefinedGVSummaries,
                         StringRef ModName,
                         FunctionImporter::ImportMapTy &ImportList) override {
    auto SetIter = Workloads.find(ModName);
    if (SetIter == Workloads.end()) {
      LLVM_DEBUG(dbgs() << "[Workload] " << ModName
                        << " does not contain the root of any context.\n");
      return ModuleImportsManager::computeImportForModule(DefinedGVSummaries,
                                                          ModName, ImportList);
    }
    LLVM_DEBUG(dbgs() << "[Workload] " << ModName
                      << " contains the root(s) of context(s).\n");

    GlobalsImporter GVI(Index, DefinedGVSummaries, IsPrevailing, ImportList,
                        ExportLists);
    auto &ValueInfos = SetIter->second;
    SmallVector<EdgeInfo, 128> GlobWorklist;
    for (auto &VI : llvm::make_early_inc_range(ValueInfos)) {
      auto It = DefinedGVSummaries.find(VI.getGUID());
      if (It != DefinedGVSummaries.end() &&
          IsPrevailing(VI.getGUID(), It->second)) {
        LLVM_DEBUG(
            dbgs() << "[Workload] " << VI.name()
                   << " has the prevailing variant already in the module "
                   << ModName << ". No need to import\n");
        continue;
      }
      auto Candidates =
          qualifyCalleeCandidates(Index, VI.getSummaryList(), ModName);

      const GlobalValueSummary *GVS = nullptr;
      auto PotentialCandidates = llvm::map_range(
          llvm::make_filter_range(
              Candidates,
              [&](const auto &Candidate) {
                LLVM_DEBUG(dbgs() << "[Workflow] Candidate for " << VI.name()
                                  << " from " << Candidate.second->modulePath()
                                  << " ImportFailureReason: "
                                  << getFailureName(Candidate.first) << "\n");
                return Candidate.first ==
                        FunctionImporter::ImportFailureReason::None;
              }),
          [](const auto &Candidate) { return Candidate.second; });
      if (PotentialCandidates.empty()) {
        LLVM_DEBUG(dbgs() << "[Workload] Not importing " << VI.name()
                          << " because can't find eligible Callee. Guid is: "
                          << Function::getGUID(VI.name()) << "\n");
        continue;
      }
      /// We will prefer importing the prevailing candidate, if not, we'll
      /// still pick the first available candidate. The reason we want to make
      /// sure we do import the prevailing candidate is because the goal of
      /// workload-awareness is to enable optimizations specializing the call
      /// graph of that workload. Suppose a function is already defined in the
      /// module, but it's not the prevailing variant. Suppose also we do not
      /// inline it (in fact, if it were interposable, we can't inline it),
      /// but we could specialize it to the workload in other ways. However,
      /// the linker would drop it in the favor of the prevailing copy.
      /// Instead, by importing the prevailing variant (assuming also the use
      /// of `-avail-extern-to-local`), we keep the specialization. We could
      /// alteranatively make the non-prevailing variant local, but the
      /// prevailing one is also the one for which we would have previously
      /// collected profiles, making it preferrable.
      auto PrevailingCandidates = llvm::make_filter_range(
          PotentialCandidates, [&](const auto *Candidate) {
            return IsPrevailing(VI.getGUID(), Candidate);
          });
      if (PrevailingCandidates.empty()) {
        GVS = *PotentialCandidates.begin();
        if (!llvm::hasSingleElement(PotentialCandidates) &&
            GlobalValue::isLocalLinkage(GVS->linkage()))
          LLVM_DEBUG(
              dbgs()
              << "[Workload] Found multiple non-prevailing candidates for "
              << VI.name()
              << ". This is unexpected. Are module paths passed to the "
                 "compiler unique for the modules passed to the linker?");
        // We could in theory have multiple (interposable) copies of a symbol
        // when there is no prevailing candidate, if say the prevailing copy was
        // in a native object being linked in. However, we should in theory be
        // marking all of these non-prevailing IR copies dead in that case, in
        // which case they won't be candidates.
        assert(GVS->isLive());
      } else {
        assert(llvm::hasSingleElement(PrevailingCandidates));
        GVS = *PrevailingCandidates.begin();
      }

      auto ExportingModule = GVS->modulePath();
      // We checked that for the prevailing case, but if we happen to have for
      // example an internal that's defined in this module, it'd have no
      // PrevailingCandidates.
      if (ExportingModule == ModName) {
        LLVM_DEBUG(dbgs() << "[Workload] Not importing " << VI.name()
                          << " because its defining module is the same as the "
                             "current module\n");
        continue;
      }
      LLVM_DEBUG(dbgs() << "[Workload][Including]" << VI.name() << " from "
                        << ExportingModule << " : "
                        << Function::getGUID(VI.name()) << "\n");
      ImportList[ExportingModule][VI.getGUID()] =
          GlobalValueSummary::Definition;
      GVI.onImportingSummary(*GVS);
      if (ExportLists)
        (*ExportLists)[ExportingModule].insert(VI);
    }
    LLVM_DEBUG(dbgs() << "[Workload] Done\n");
  }

public:
  WorkloadImportsManager(
      function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
          IsPrevailing,
      const ModuleSummaryIndex &Index,
      DenseMap<StringRef, FunctionImporter::ExportSetTy> *ExportLists)
      : ModuleImportsManager(IsPrevailing, Index, ExportLists) {
    // Since the workload def uses names, we need a quick lookup
    // name->ValueInfo.
    StringMap<ValueInfo> NameToValueInfo;
    StringSet<> AmbiguousNames;
    for (auto &I : Index) {
      ValueInfo VI = Index.getValueInfo(I);
      if (!NameToValueInfo.insert(std::make_pair(VI.name(), VI)).second)
        LLVM_DEBUG(AmbiguousNames.insert(VI.name()));
    }
    auto DbgReportIfAmbiguous = [&](StringRef Name) {
      LLVM_DEBUG(if (AmbiguousNames.count(Name) > 0) {
        dbgs() << "[Workload] Function name " << Name
               << " present in the workload definition is ambiguous. Consider "
                  "compiling with -funique-internal-linkage-names.";
      });
    };
    std::error_code EC;
    auto BufferOrErr = MemoryBuffer::getFileOrSTDIN(WorkloadDefinitions);
    if (std::error_code EC = BufferOrErr.getError()) {
      report_fatal_error("Failed to open context file");
      return;
    }
    auto Buffer = std::move(BufferOrErr.get());
    std::map<std::string, std::vector<std::string>> WorkloadDefs;
    json::Path::Root NullRoot;
    // The JSON is supposed to contain a dictionary matching the type of
    // WorkloadDefs. For example:
    // {
    //   "rootFunction_1": ["function_to_import_1", "function_to_import_2"],
    //   "rootFunction_2": ["function_to_import_3", "function_to_import_4"]
    // }
    auto Parsed = json::parse(Buffer->getBuffer());
    if (!Parsed)
      report_fatal_error(Parsed.takeError());
    if (!json::fromJSON(*Parsed, WorkloadDefs, NullRoot))
      report_fatal_error("Invalid thinlto contextual profile format.");
    for (const auto &Workload : WorkloadDefs) {
      const auto &Root = Workload.first;
      DbgReportIfAmbiguous(Root);
      LLVM_DEBUG(dbgs() << "[Workload] Root: " << Root << "\n");
      const auto &AllCallees = Workload.second;
      auto RootIt = NameToValueInfo.find(Root);
      if (RootIt == NameToValueInfo.end()) {
        LLVM_DEBUG(dbgs() << "[Workload] Root " << Root
                          << " not found in this linkage unit.\n");
        continue;
      }
      auto RootVI = RootIt->second;
      if (RootVI.getSummaryList().size() != 1) {
        LLVM_DEBUG(dbgs() << "[Workload] Root " << Root
                          << " should have exactly one summary, but has "
                          << RootVI.getSummaryList().size() << ". Skipping.\n");
        continue;
      }
      StringRef RootDefiningModule =
          RootVI.getSummaryList().front()->modulePath();
      LLVM_DEBUG(dbgs() << "[Workload] Root defining module for " << Root
                        << " is : " << RootDefiningModule << "\n");
      auto &Set = Workloads[RootDefiningModule];
      for (const auto &Callee : AllCallees) {
        LLVM_DEBUG(dbgs() << "[Workload] " << Callee << "\n");
        DbgReportIfAmbiguous(Callee);
        auto ElemIt = NameToValueInfo.find(Callee);
        if (ElemIt == NameToValueInfo.end()) {
          LLVM_DEBUG(dbgs() << "[Workload] " << Callee << " not found\n");
          continue;
        }
        Set.insert(ElemIt->second);
      }
      LLVM_DEBUG({
        dbgs() << "[Workload] Root: " << Root << " we have " << Set.size()
               << " distinct callees.\n";
        for (const auto &VI : Set) {
          dbgs() << "[Workload] Root: " << Root
                 << " Would include: " << VI.getGUID() << "\n";
        }
      });
    }
  }
};

std::unique_ptr<ModuleImportsManager> ModuleImportsManager::create(
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        IsPrevailing,
    const ModuleSummaryIndex &Index,
    DenseMap<StringRef, FunctionImporter::ExportSetTy> *ExportLists) {
  if (WorkloadDefinitions.empty()) {
    LLVM_DEBUG(dbgs() << "[Workload] Using the regular imports manager.\n");
    return std::unique_ptr<ModuleImportsManager>(
        new ModuleImportsManager(IsPrevailing, Index, ExportLists));
  }
  LLVM_DEBUG(dbgs() << "[Workload] Using the contextual imports manager.\n");
  return std::make_unique<WorkloadImportsManager>(IsPrevailing, Index,
                                                  ExportLists);
}

static const char *
getFailureName(FunctionImporter::ImportFailureReason Reason) {
  switch (Reason) {
  case FunctionImporter::ImportFailureReason::None:
    return "None";
  case FunctionImporter::ImportFailureReason::GlobalVar:
    return "GlobalVar";
  case FunctionImporter::ImportFailureReason::NotLive:
    return "NotLive";
  case FunctionImporter::ImportFailureReason::TooLarge:
    return "TooLarge";
  case FunctionImporter::ImportFailureReason::InterposableLinkage:
    return "InterposableLinkage";
  case FunctionImporter::ImportFailureReason::LocalLinkageNotInModule:
    return "LocalLinkageNotInModule";
  case FunctionImporter::ImportFailureReason::NotEligible:
    return "NotEligible";
  case FunctionImporter::ImportFailureReason::NoInline:
    return "NoInline";
  }
  llvm_unreachable("invalid reason");
}

/// Compute the list of functions to import for a given caller. Mark these
/// imported functions and the symbols they reference in their source module as
/// exported from their source module.
static void computeImportForFunction(
    const FunctionSummary &Summary, const ModuleSummaryIndex &Index,
    const unsigned Threshold, const GVSummaryMapTy &DefinedGVSummaries,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        isPrevailing,
    SmallVectorImpl<EdgeInfo> &Worklist, GlobalsImporter &GVImporter,
    FunctionImporter::ImportMapTy &ImportList,
    DenseMap<StringRef, FunctionImporter::ExportSetTy> *ExportLists,
    FunctionImporter::ImportThresholdsTy &ImportThresholds) {
  GVImporter.onImportingSummary(Summary);
  static int ImportCount = 0;
  for (const auto &Edge : Summary.calls()) {
    ValueInfo VI = Edge.first;
    LLVM_DEBUG(dbgs() << " edge -> " << VI << " Threshold:" << Threshold
                      << "\n");

    if (ImportCutoff >= 0 && ImportCount >= ImportCutoff) {
      LLVM_DEBUG(dbgs() << "ignored! import-cutoff value of " << ImportCutoff
                        << " reached.\n");
      continue;
    }

    if (DefinedGVSummaries.count(VI.getGUID())) {
      // FIXME: Consider not skipping import if the module contains
      // a non-prevailing def with interposable linkage. The prevailing copy
      // can safely be imported (see shouldImportGlobal()).
      LLVM_DEBUG(dbgs() << "ignored! Target already in destination module.\n");
      continue;
    }

    auto GetBonusMultiplier = [](CalleeInfo::HotnessType Hotness) -> float {
      if (Hotness == CalleeInfo::HotnessType::Hot)
        return ImportHotMultiplier;
      if (Hotness == CalleeInfo::HotnessType::Cold)
        return ImportColdMultiplier;
      if (Hotness == CalleeInfo::HotnessType::Critical)
        return ImportCriticalMultiplier;
      return 1.0;
    };

    const auto NewThreshold =
        Threshold * GetBonusMultiplier(Edge.second.getHotness());

    auto IT = ImportThresholds.insert(std::make_pair(
        VI.getGUID(), std::make_tuple(NewThreshold, nullptr, nullptr)));
    bool PreviouslyVisited = !IT.second;
    auto &ProcessedThreshold = std::get<0>(IT.first->second);
    auto &CalleeSummary = std::get<1>(IT.first->second);
    auto &FailureInfo = std::get<2>(IT.first->second);

    bool IsHotCallsite =
        Edge.second.getHotness() == CalleeInfo::HotnessType::Hot;
    bool IsCriticalCallsite =
        Edge.second.getHotness() == CalleeInfo::HotnessType::Critical;

    const FunctionSummary *ResolvedCalleeSummary = nullptr;
    if (CalleeSummary) {
      assert(PreviouslyVisited);
      // Since the traversal of the call graph is DFS, we can revisit a function
      // a second time with a higher threshold. In this case, it is added back
      // to the worklist with the new threshold (so that its own callee chains
      // can be considered with the higher threshold).
      if (NewThreshold <= ProcessedThreshold) {
        LLVM_DEBUG(
            dbgs() << "ignored! Target was already imported with Threshold "
                   << ProcessedThreshold << "\n");
        continue;
      }
      // Update with new larger threshold.
      ProcessedThreshold = NewThreshold;
      ResolvedCalleeSummary = cast<FunctionSummary>(CalleeSummary);
    } else {
      // If we already rejected importing a callee at the same or higher
      // threshold, don't waste time calling selectCallee.
      if (PreviouslyVisited && NewThreshold <= ProcessedThreshold) {
        LLVM_DEBUG(
            dbgs() << "ignored! Target was already rejected with Threshold "
            << ProcessedThreshold << "\n");
        if (PrintImportFailures) {
          assert(FailureInfo &&
                 "Expected FailureInfo for previously rejected candidate");
          FailureInfo->Attempts++;
        }
        continue;
      }

      FunctionImporter::ImportFailureReason Reason{};

      // `SummaryForDeclImport` is an summary eligible for declaration import.
      const GlobalValueSummary *SummaryForDeclImport = nullptr;
      CalleeSummary =
          selectCallee(Index, VI.getSummaryList(), NewThreshold,
                       Summary.modulePath(), SummaryForDeclImport, Reason);
      if (!CalleeSummary) {
        // There isn't a callee for definition import but one for declaration
        // import.
        if (ImportDeclaration && SummaryForDeclImport) {
          StringRef DeclSourceModule = SummaryForDeclImport->modulePath();

          // Since definition takes precedence over declaration for the same VI,
          // try emplace <VI, declaration> pair without checking insert result.
          // If insert doesn't happen, there must be an existing entry keyed by
          // VI. Note `ExportLists` only keeps track of exports due to imported
          // definitions.
          ImportList[DeclSourceModule].try_emplace(
              VI.getGUID(), GlobalValueSummary::Declaration);
        }
        // Update with new larger threshold if this was a retry (otherwise
        // we would have already inserted with NewThreshold above). Also
        // update failure info if requested.
        if (PreviouslyVisited) {
          ProcessedThreshold = NewThreshold;
          if (PrintImportFailures) {
            assert(FailureInfo &&
                   "Expected FailureInfo for previously rejected candidate");
            FailureInfo->Reason = Reason;
            FailureInfo->Attempts++;
            FailureInfo->MaxHotness =
                std::max(FailureInfo->MaxHotness, Edge.second.getHotness());
          }
        } else if (PrintImportFailures) {
          assert(!FailureInfo &&
                 "Expected no FailureInfo for newly rejected candidate");
          FailureInfo = std::make_unique<FunctionImporter::ImportFailureInfo>(
              VI, Edge.second.getHotness(), Reason, 1);
        }
        if (ForceImportAll) {
          std::string Msg = std::string("Failed to import function ") +
                            VI.name().str() + " due to " +
                            getFailureName(Reason);
          auto Error = make_error<StringError>(
              Msg, make_error_code(errc::not_supported));
          logAllUnhandledErrors(std::move(Error), errs(),
                                "Error importing module: ");
          break;
        } else {
          LLVM_DEBUG(dbgs()
                     << "ignored! No qualifying callee with summary found.\n");
          continue;
        }
      }

      // "Resolve" the summary
      CalleeSummary = CalleeSummary->getBaseObject();
      ResolvedCalleeSummary = cast<FunctionSummary>(CalleeSummary);

      assert((ResolvedCalleeSummary->fflags().AlwaysInline || ForceImportAll ||
              (ResolvedCalleeSummary->instCount() <= NewThreshold)) &&
             "selectCallee() didn't honor the threshold");

      auto ExportModulePath = ResolvedCalleeSummary->modulePath();

      // Try emplace the definition entry, and update stats based on insertion
      // status.
      auto [Iter, Inserted] = ImportList[ExportModulePath].try_emplace(
          VI.getGUID(), GlobalValueSummary::Definition);

      // We previously decided to import this GUID definition if it was already
      // inserted in the set of imports from the exporting module.
      if (Inserted || Iter->second == GlobalValueSummary::Declaration) {
        NumImportedFunctionsThinLink++;
        if (IsHotCallsite)
          NumImportedHotFunctionsThinLink++;
        if (IsCriticalCallsite)
          NumImportedCriticalFunctionsThinLink++;
      }

      if (Iter->second == GlobalValueSummary::Declaration)
        Iter->second = GlobalValueSummary::Definition;

      // Any calls/references made by this function will be marked exported
      // later, in ComputeCrossModuleImport, after import decisions are
      // complete, which is more efficient than adding them here.
      if (ExportLists)
        (*ExportLists)[ExportModulePath].insert(VI);
    }

    auto GetAdjustedThreshold = [](unsigned Threshold, bool IsHotCallsite) {
      // Adjust the threshold for next level of imported functions.
      // The threshold is different for hot callsites because we can then
      // inline chains of hot calls.
      if (IsHotCallsite)
        return Threshold * ImportHotInstrFactor;
      return Threshold * ImportInstrFactor;
    };

    const auto AdjThreshold = GetAdjustedThreshold(Threshold, IsHotCallsite);

    ImportCount++;

    // Insert the newly imported function to the worklist.
    Worklist.emplace_back(ResolvedCalleeSummary, AdjThreshold);
  }
}

void ModuleImportsManager::computeImportForModule(
    const GVSummaryMapTy &DefinedGVSummaries, StringRef ModName,
    FunctionImporter::ImportMapTy &ImportList) {
  // Worklist contains the list of function imported in this module, for which
  // we will analyse the callees and may import further down the callgraph.
  SmallVector<EdgeInfo, 128> Worklist;
  GlobalsImporter GVI(Index, DefinedGVSummaries, IsPrevailing, ImportList,
                      ExportLists);
  FunctionImporter::ImportThresholdsTy ImportThresholds;

  // Populate the worklist with the import for the functions in the current
  // module
  for (const auto &GVSummary : DefinedGVSummaries) {
#ifndef NDEBUG
    // FIXME: Change the GVSummaryMapTy to hold ValueInfo instead of GUID
    // so this map look up (and possibly others) can be avoided.
    auto VI = Index.getValueInfo(GVSummary.first);
#endif
    if (!Index.isGlobalValueLive(GVSummary.second)) {
      LLVM_DEBUG(dbgs() << "Ignores Dead GUID: " << VI << "\n");
      continue;
    }
    auto *FuncSummary =
        dyn_cast<FunctionSummary>(GVSummary.second->getBaseObject());
    if (!FuncSummary)
      // Skip import for global variables
      continue;
    LLVM_DEBUG(dbgs() << "Initialize import for " << VI << "\n");
    computeImportForFunction(*FuncSummary, Index, ImportInstrLimit,
                             DefinedGVSummaries, IsPrevailing, Worklist, GVI,
                             ImportList, ExportLists, ImportThresholds);
  }

  // Process the newly imported functions and add callees to the worklist.
  while (!Worklist.empty()) {
    auto GVInfo = Worklist.pop_back_val();
    auto *Summary = std::get<0>(GVInfo);
    auto Threshold = std::get<1>(GVInfo);

    if (auto *FS = dyn_cast<FunctionSummary>(Summary))
      computeImportForFunction(*FS, Index, Threshold, DefinedGVSummaries,
                               IsPrevailing, Worklist, GVI, ImportList,
                               ExportLists, ImportThresholds);
  }

  // Print stats about functions considered but rejected for importing
  // when requested.
  if (PrintImportFailures) {
    dbgs() << "Missed imports into module " << ModName << "\n";
    for (auto &I : ImportThresholds) {
      auto &ProcessedThreshold = std::get<0>(I.second);
      auto &CalleeSummary = std::get<1>(I.second);
      auto &FailureInfo = std::get<2>(I.second);
      if (CalleeSummary)
        continue; // We are going to import.
      assert(FailureInfo);
      FunctionSummary *FS = nullptr;
      if (!FailureInfo->VI.getSummaryList().empty())
        FS = dyn_cast<FunctionSummary>(
            FailureInfo->VI.getSummaryList()[0]->getBaseObject());
      dbgs() << FailureInfo->VI
             << ": Reason = " << getFailureName(FailureInfo->Reason)
             << ", Threshold = " << ProcessedThreshold
             << ", Size = " << (FS ? (int)FS->instCount() : -1)
             << ", MaxHotness = " << getHotnessName(FailureInfo->MaxHotness)
             << ", Attempts = " << FailureInfo->Attempts << "\n";
    }
  }
}

#ifndef NDEBUG
static bool isGlobalVarSummary(const ModuleSummaryIndex &Index, ValueInfo VI) {
  auto SL = VI.getSummaryList();
  return SL.empty()
             ? false
             : SL[0]->getSummaryKind() == GlobalValueSummary::GlobalVarKind;
}

static bool isGlobalVarSummary(const ModuleSummaryIndex &Index,
                               GlobalValue::GUID G) {
  if (const auto &VI = Index.getValueInfo(G))
    return isGlobalVarSummary(Index, VI);
  return false;
}

// Return the number of global variable summaries in ExportSet.
static unsigned
numGlobalVarSummaries(const ModuleSummaryIndex &Index,
                      FunctionImporter::ExportSetTy &ExportSet) {
  unsigned NumGVS = 0;
  for (auto &VI : ExportSet)
    if (isGlobalVarSummary(Index, VI.getGUID()))
      ++NumGVS;
  return NumGVS;
}

// Given ImportMap, return the number of global variable summaries and record
// the number of defined function summaries as output parameter.
static unsigned
numGlobalVarSummaries(const ModuleSummaryIndex &Index,
                      FunctionImporter::FunctionsToImportTy &ImportMap,
                      unsigned &DefinedFS) {
  unsigned NumGVS = 0;
  DefinedFS = 0;
  for (auto &[GUID, Type] : ImportMap) {
    if (isGlobalVarSummary(Index, GUID))
      ++NumGVS;
    else if (Type == GlobalValueSummary::Definition)
      ++DefinedFS;
  }
  return NumGVS;
}
#endif

#ifndef NDEBUG
static bool checkVariableImport(
    const ModuleSummaryIndex &Index,
    DenseMap<StringRef, FunctionImporter::ImportMapTy> &ImportLists,
    DenseMap<StringRef, FunctionImporter::ExportSetTy> &ExportLists) {
  DenseSet<GlobalValue::GUID> FlattenedImports;

  for (auto &ImportPerModule : ImportLists)
    for (auto &ExportPerModule : ImportPerModule.second)
      for (auto &[GUID, Type] : ExportPerModule.second)
        FlattenedImports.insert(GUID);

  // Checks that all GUIDs of read/writeonly vars we see in export lists
  // are also in the import lists. Otherwise we my face linker undefs,
  // because readonly and writeonly vars are internalized in their
  // source modules. The exception would be if it has a linkage type indicating
  // that there may have been a copy existing in the importing module (e.g.
  // linkonce_odr). In that case we cannot accurately do this checking.
  auto IsReadOrWriteOnlyVarNeedingImporting = [&](StringRef ModulePath,
                                                  const ValueInfo &VI) {
    auto *GVS = dyn_cast_or_null<GlobalVarSummary>(
        Index.findSummaryInModule(VI, ModulePath));
    return GVS && (Index.isReadOnly(GVS) || Index.isWriteOnly(GVS)) &&
           !(GVS->linkage() == GlobalValue::AvailableExternallyLinkage ||
             GVS->linkage() == GlobalValue::WeakODRLinkage ||
             GVS->linkage() == GlobalValue::LinkOnceODRLinkage);
  };

  for (auto &ExportPerModule : ExportLists)
    for (auto &VI : ExportPerModule.second)
      if (!FlattenedImports.count(VI.getGUID()) &&
          IsReadOrWriteOnlyVarNeedingImporting(ExportPerModule.first, VI))
        return false;

  return true;
}
#endif

/// Compute all the import and export for every module using the Index.
void llvm::ComputeCrossModuleImport(
    const ModuleSummaryIndex &Index,
    const DenseMap<StringRef, GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        isPrevailing,
    DenseMap<StringRef, FunctionImporter::ImportMapTy> &ImportLists,
    DenseMap<StringRef, FunctionImporter::ExportSetTy> &ExportLists) {
  auto MIS = ModuleImportsManager::create(isPrevailing, Index, &ExportLists);
  // For each module that has function defined, compute the import/export lists.
  for (const auto &DefinedGVSummaries : ModuleToDefinedGVSummaries) {
    auto &ImportList = ImportLists[DefinedGVSummaries.first];
    LLVM_DEBUG(dbgs() << "Computing import for Module '"
                      << DefinedGVSummaries.first << "'\n");
    MIS->computeImportForModule(DefinedGVSummaries.second,
                                DefinedGVSummaries.first, ImportList);
  }

  // When computing imports we only added the variables and functions being
  // imported to the export list. We also need to mark any references and calls
  // they make as exported as well. We do this here, as it is more efficient
  // since we may import the same values multiple times into different modules
  // during the import computation.
  for (auto &ELI : ExportLists) {
    // `NewExports` tracks the VI that gets exported because the full definition
    // of its user/referencer gets exported.
    FunctionImporter::ExportSetTy NewExports;
    const auto &DefinedGVSummaries =
        ModuleToDefinedGVSummaries.lookup(ELI.first);
    for (auto &EI : ELI.second) {
      // Find the copy defined in the exporting module so that we can mark the
      // values it references in that specific definition as exported.
      // Below we will add all references and called values, without regard to
      // whether they are also defined in this module. We subsequently prune the
      // list to only include those defined in the exporting module, see comment
      // there as to why.
      auto DS = DefinedGVSummaries.find(EI.getGUID());
      // Anything marked exported during the import computation must have been
      // defined in the exporting module.
      assert(DS != DefinedGVSummaries.end());
      auto *S = DS->getSecond();
      S = S->getBaseObject();
      if (auto *GVS = dyn_cast<GlobalVarSummary>(S)) {
        // Export referenced functions and variables. We don't export/promote
        // objects referenced by writeonly variable initializer, because
        // we convert such variables initializers to "zeroinitializer".
        // See processGlobalForThinLTO.
        if (!Index.isWriteOnly(GVS))
          for (const auto &VI : GVS->refs())
            NewExports.insert(VI);
      } else {
        auto *FS = cast<FunctionSummary>(S);
        for (const auto &Edge : FS->calls())
          NewExports.insert(Edge.first);
        for (const auto &Ref : FS->refs())
          NewExports.insert(Ref);
      }
    }
    // Prune list computed above to only include values defined in the
    // exporting module. We do this after the above insertion since we may hit
    // the same ref/call target multiple times in above loop, and it is more
    // efficient to avoid a set lookup each time.
    for (auto EI = NewExports.begin(); EI != NewExports.end();) {
      if (!DefinedGVSummaries.count(EI->getGUID()))
        NewExports.erase(EI++);
      else
        ++EI;
    }
    ELI.second.insert(NewExports.begin(), NewExports.end());
  }

  assert(checkVariableImport(Index, ImportLists, ExportLists));
#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "Import/Export lists for " << ImportLists.size()
                    << " modules:\n");
  for (auto &ModuleImports : ImportLists) {
    auto ModName = ModuleImports.first;
    auto &Exports = ExportLists[ModName];
    unsigned NumGVS = numGlobalVarSummaries(Index, Exports);
    LLVM_DEBUG(dbgs() << "* Module " << ModName << " exports "
                      << Exports.size() - NumGVS << " functions and " << NumGVS
                      << " vars. Imports from " << ModuleImports.second.size()
                      << " modules.\n");
    for (auto &Src : ModuleImports.second) {
      auto SrcModName = Src.first;
      unsigned DefinedFS = 0;
      unsigned NumGVSPerMod =
          numGlobalVarSummaries(Index, Src.second, DefinedFS);
      LLVM_DEBUG(dbgs() << " - " << DefinedFS << " function definitions and "
                        << Src.second.size() - NumGVSPerMod - DefinedFS
                        << " function declarations imported from " << SrcModName
                        << "\n");
      LLVM_DEBUG(dbgs() << " - " << NumGVSPerMod
                        << " global vars imported from " << SrcModName << "\n");
    }
  }
#endif
}

#ifndef NDEBUG
static void dumpImportListForModule(const ModuleSummaryIndex &Index,
                                    StringRef ModulePath,
                                    FunctionImporter::ImportMapTy &ImportList) {
  LLVM_DEBUG(dbgs() << "* Module " << ModulePath << " imports from "
                    << ImportList.size() << " modules.\n");
  for (auto &Src : ImportList) {
    auto SrcModName = Src.first;
    unsigned DefinedFS = 0;
    unsigned NumGVSPerMod = numGlobalVarSummaries(Index, Src.second, DefinedFS);
    LLVM_DEBUG(dbgs() << " - " << DefinedFS << " function definitions and "
                      << Src.second.size() - DefinedFS - NumGVSPerMod
                      << " function declarations imported from " << SrcModName
                      << "\n");
    LLVM_DEBUG(dbgs() << " - " << NumGVSPerMod << " vars imported from "
                      << SrcModName << "\n");
  }
}
#endif

/// Compute all the imports for the given module using the Index.
///
/// \p isPrevailing is a callback that will be called with a global value's GUID
/// and summary and should return whether the module corresponding to the
/// summary contains the linker-prevailing copy of that value.
///
/// \p ImportList will be populated with a map that can be passed to
/// FunctionImporter::importFunctions() above (see description there).
static void ComputeCrossModuleImportForModuleForTest(
    StringRef ModulePath,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        isPrevailing,
    const ModuleSummaryIndex &Index,
    FunctionImporter::ImportMapTy &ImportList) {
  // Collect the list of functions this module defines.
  // GUID -> Summary
  GVSummaryMapTy FunctionSummaryMap;
  Index.collectDefinedFunctionsForModule(ModulePath, FunctionSummaryMap);

  // Compute the import list for this module.
  LLVM_DEBUG(dbgs() << "Computing import for Module '" << ModulePath << "'\n");
  auto MIS = ModuleImportsManager::create(isPrevailing, Index);
  MIS->computeImportForModule(FunctionSummaryMap, ModulePath, ImportList);

#ifndef NDEBUG
  dumpImportListForModule(Index, ModulePath, ImportList);
#endif
}

/// Mark all external summaries in \p Index for import into the given module.
/// Used for testing the case of distributed builds using a distributed index.
///
/// \p ImportList will be populated with a map that can be passed to
/// FunctionImporter::importFunctions() above (see description there).
static void ComputeCrossModuleImportForModuleFromIndexForTest(
    StringRef ModulePath, const ModuleSummaryIndex &Index,
    FunctionImporter::ImportMapTy &ImportList) {
  for (const auto &GlobalList : Index) {
    // Ignore entries for undefined references.
    if (GlobalList.second.SummaryList.empty())
      continue;

    auto GUID = GlobalList.first;
    assert(GlobalList.second.SummaryList.size() == 1 &&
           "Expected individual combined index to have one summary per GUID");
    auto &Summary = GlobalList.second.SummaryList[0];
    // Skip the summaries for the importing module. These are included to
    // e.g. record required linkage changes.
    if (Summary->modulePath() == ModulePath)
      continue;
    // Add an entry to provoke importing by thinBackend.
    auto [Iter, Inserted] = ImportList[Summary->modulePath()].try_emplace(
        GUID, Summary->importType());
    if (!Inserted) {
      // Use 'std::min' to make sure definition (with enum value 0) takes
      // precedence over declaration (with enum value 1).
      Iter->second = std::min(Iter->second, Summary->importType());
    }
  }
#ifndef NDEBUG
  dumpImportListForModule(Index, ModulePath, ImportList);
#endif
}

// For SamplePGO, the indirect call targets for local functions will
// have its original name annotated in profile. We try to find the
// corresponding PGOFuncName as the GUID, and fix up the edges
// accordingly.
void updateValueInfoForIndirectCalls(ModuleSummaryIndex &Index,
                                     FunctionSummary *FS) {
  for (auto &EI : FS->mutableCalls()) {
    if (!EI.first.getSummaryList().empty())
      continue;
    auto GUID = Index.getGUIDFromOriginalID(EI.first.getGUID());
    if (GUID == 0)
      continue;
    // Update the edge to point directly to the correct GUID.
    auto VI = Index.getValueInfo(GUID);
    if (llvm::any_of(
            VI.getSummaryList(),
            [&](const std::unique_ptr<GlobalValueSummary> &SummaryPtr) {
              // The mapping from OriginalId to GUID may return a GUID
              // that corresponds to a static variable. Filter it out here.
              // This can happen when
              // 1) There is a call to a library function which is not defined
              // in the index.
              // 2) There is a static variable with the  OriginalGUID identical
              // to the GUID of the library function in 1);
              // When this happens the static variable in 2) will be found,
              // which needs to be filtered out.
              return SummaryPtr->getSummaryKind() ==
                     GlobalValueSummary::GlobalVarKind;
            }))
      continue;
    EI.first = VI;
  }
}

void llvm::updateIndirectCalls(ModuleSummaryIndex &Index) {
  for (const auto &Entry : Index) {
    for (const auto &S : Entry.second.SummaryList) {
      if (auto *FS = dyn_cast<FunctionSummary>(S.get()))
        updateValueInfoForIndirectCalls(Index, FS);
    }
  }
}

void llvm::computeDeadSymbolsAndUpdateIndirectCalls(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    function_ref<PrevailingType(GlobalValue::GUID)> isPrevailing) {
  assert(!Index.withGlobalValueDeadStripping());
  if (!ComputeDead ||
      // Don't do anything when nothing is live, this is friendly with tests.
      GUIDPreservedSymbols.empty()) {
    // Still need to update indirect calls.
    updateIndirectCalls(Index);
    return;
  }
  unsigned LiveSymbols = 0;
  SmallVector<ValueInfo, 128> Worklist;
  Worklist.reserve(GUIDPreservedSymbols.size() * 2);
  for (auto GUID : GUIDPreservedSymbols) {
    ValueInfo VI = Index.getValueInfo(GUID);
    if (!VI)
      continue;
    for (const auto &S : VI.getSummaryList())
      S->setLive(true);
  }

  // Add values flagged in the index as live roots to the worklist.
  for (const auto &Entry : Index) {
    auto VI = Index.getValueInfo(Entry);
    for (const auto &S : Entry.second.SummaryList) {
      if (auto *FS = dyn_cast<FunctionSummary>(S.get()))
        updateValueInfoForIndirectCalls(Index, FS);
      if (S->isLive()) {
        LLVM_DEBUG(dbgs() << "Live root: " << VI << "\n");
        Worklist.push_back(VI);
        ++LiveSymbols;
        break;
      }
    }
  }

  // Make value live and add it to the worklist if it was not live before.
  auto visit = [&](ValueInfo VI, bool IsAliasee) {
    // FIXME: If we knew which edges were created for indirect call profiles,
    // we could skip them here. Any that are live should be reached via
    // other edges, e.g. reference edges. Otherwise, using a profile collected
    // on a slightly different binary might provoke preserving, importing
    // and ultimately promoting calls to functions not linked into this
    // binary, which increases the binary size unnecessarily. Note that
    // if this code changes, the importer needs to change so that edges
    // to functions marked dead are skipped.

    if (llvm::any_of(VI.getSummaryList(),
                     [](const std::unique_ptr<llvm::GlobalValueSummary> &S) {
                       return S->isLive();
                     }))
      return;

    // We only keep live symbols that are known to be non-prevailing if any are
    // available_externally, linkonceodr, weakodr. Those symbols are discarded
    // later in the EliminateAvailableExternally pass and setting them to
    // not-live could break downstreams users of liveness information (PR36483)
    // or limit optimization opportunities.
    if (isPrevailing(VI.getGUID()) == PrevailingType::No) {
      bool KeepAliveLinkage = false;
      bool Interposable = false;
      for (const auto &S : VI.getSummaryList()) {
        if (S->linkage() == GlobalValue::AvailableExternallyLinkage ||
            S->linkage() == GlobalValue::WeakODRLinkage ||
            S->linkage() == GlobalValue::LinkOnceODRLinkage)
          KeepAliveLinkage = true;
        else if (GlobalValue::isInterposableLinkage(S->linkage()))
          Interposable = true;
      }

      if (!IsAliasee) {
        if (!KeepAliveLinkage)
          return;

        if (Interposable)
          report_fatal_error(
              "Interposable and available_externally/linkonce_odr/weak_odr "
              "symbol");
      }
    }

    for (const auto &S : VI.getSummaryList())
      S->setLive(true);
    ++LiveSymbols;
    Worklist.push_back(VI);
  };

  while (!Worklist.empty()) {
    auto VI = Worklist.pop_back_val();
    for (const auto &Summary : VI.getSummaryList()) {
      if (auto *AS = dyn_cast<AliasSummary>(Summary.get())) {
        // If this is an alias, visit the aliasee VI to ensure that all copies
        // are marked live and it is added to the worklist for further
        // processing of its references.
        visit(AS->getAliaseeVI(), true);
        continue;
      }
      for (auto Ref : Summary->refs())
        visit(Ref, false);
      if (auto *FS = dyn_cast<FunctionSummary>(Summary.get()))
        for (auto Call : FS->calls())
          visit(Call.first, false);
    }
  }
  Index.setWithGlobalValueDeadStripping();

  unsigned DeadSymbols = Index.size() - LiveSymbols;
  LLVM_DEBUG(dbgs() << LiveSymbols << " symbols Live, and " << DeadSymbols
                    << " symbols Dead \n");
  NumDeadSymbols += DeadSymbols;
  NumLiveSymbols += LiveSymbols;
}

// Compute dead symbols and propagate constants in combined index.
void llvm::computeDeadSymbolsWithConstProp(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    function_ref<PrevailingType(GlobalValue::GUID)> isPrevailing,
    bool ImportEnabled) {
  computeDeadSymbolsAndUpdateIndirectCalls(Index, GUIDPreservedSymbols,
                                           isPrevailing);
  if (ImportEnabled)
    Index.propagateAttributes(GUIDPreservedSymbols);
}

/// Compute the set of summaries needed for a ThinLTO backend compilation of
/// \p ModulePath.
void llvm::gatherImportedSummariesForModule(
    StringRef ModulePath,
    const DenseMap<StringRef, GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    const FunctionImporter::ImportMapTy &ImportList,
    std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex,
    GVSummaryPtrSet &DecSummaries) {
  // Include all summaries from the importing module.
  ModuleToSummariesForIndex[std::string(ModulePath)] =
      ModuleToDefinedGVSummaries.lookup(ModulePath);
  // Include summaries for imports.
  for (const auto &ILI : ImportList) {
    auto &SummariesForIndex = ModuleToSummariesForIndex[std::string(ILI.first)];

    const auto &DefinedGVSummaries =
        ModuleToDefinedGVSummaries.lookup(ILI.first);
    for (const auto &[GUID, Type] : ILI.second) {
      const auto &DS = DefinedGVSummaries.find(GUID);
      assert(DS != DefinedGVSummaries.end() &&
             "Expected a defined summary for imported global value");
      if (Type == GlobalValueSummary::Declaration)
        DecSummaries.insert(DS->second);

      SummariesForIndex[GUID] = DS->second;
    }
  }
}

/// Emit the files \p ModulePath will import from into \p OutputFilename.
std::error_code llvm::EmitImportsFiles(
    StringRef ModulePath, StringRef OutputFilename,
    const std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex) {
  std::error_code EC;
  raw_fd_ostream ImportsOS(OutputFilename, EC, sys::fs::OpenFlags::OF_Text);
  if (EC)
    return EC;
  for (const auto &ILI : ModuleToSummariesForIndex)
    // The ModuleToSummariesForIndex map includes an entry for the current
    // Module (needed for writing out the index files). We don't want to
    // include it in the imports file, however, so filter it out.
    if (ILI.first != ModulePath)
      ImportsOS << ILI.first << "\n";
  return std::error_code();
}

bool llvm::convertToDeclaration(GlobalValue &GV) {
  LLVM_DEBUG(dbgs() << "Converting to a declaration: `" << GV.getName()
                    << "\n");
  if (Function *F = dyn_cast<Function>(&GV)) {
    F->deleteBody();
    F->clearMetadata();
    F->setComdat(nullptr);
  } else if (GlobalVariable *V = dyn_cast<GlobalVariable>(&GV)) {
    V->setInitializer(nullptr);
    V->setLinkage(GlobalValue::ExternalLinkage);
    V->clearMetadata();
    V->setComdat(nullptr);
  } else {
    GlobalValue *NewGV;
    if (GV.getValueType()->isFunctionTy())
      NewGV =
          Function::Create(cast<FunctionType>(GV.getValueType()),
                           GlobalValue::ExternalLinkage, GV.getAddressSpace(),
                           "", GV.getParent());
    else
      NewGV =
          new GlobalVariable(*GV.getParent(), GV.getValueType(),
                             /*isConstant*/ false, GlobalValue::ExternalLinkage,
                             /*init*/ nullptr, "",
                             /*insertbefore*/ nullptr, GV.getThreadLocalMode(),
                             GV.getType()->getAddressSpace());
    NewGV->takeName(&GV);
    GV.replaceAllUsesWith(NewGV);
    return false;
  }
  if (!GV.isImplicitDSOLocal())
    GV.setDSOLocal(false);
  return true;
}

void llvm::thinLTOFinalizeInModule(Module &TheModule,
                                   const GVSummaryMapTy &DefinedGlobals,
                                   bool PropagateAttrs) {
  DenseSet<Comdat *> NonPrevailingComdats;
  auto FinalizeInModule = [&](GlobalValue &GV, bool Propagate = false) {
    // See if the global summary analysis computed a new resolved linkage.
    const auto &GS = DefinedGlobals.find(GV.getGUID());
    if (GS == DefinedGlobals.end())
      return;

    if (Propagate)
      if (FunctionSummary *FS = dyn_cast<FunctionSummary>(GS->second)) {
        if (Function *F = dyn_cast<Function>(&GV)) {
          // TODO: propagate ReadNone and ReadOnly.
          if (FS->fflags().ReadNone && !F->doesNotAccessMemory())
            F->setDoesNotAccessMemory();

          if (FS->fflags().ReadOnly && !F->onlyReadsMemory())
            F->setOnlyReadsMemory();

          if (FS->fflags().NoRecurse && !F->doesNotRecurse())
            F->setDoesNotRecurse();

          if (FS->fflags().NoUnwind && !F->doesNotThrow())
            F->setDoesNotThrow();
        }
      }

    auto NewLinkage = GS->second->linkage();
    if (GlobalValue::isLocalLinkage(GV.getLinkage()) ||
        // Don't internalize anything here, because the code below
        // lacks necessary correctness checks. Leave this job to
        // LLVM 'internalize' pass.
        GlobalValue::isLocalLinkage(NewLinkage) ||
        // In case it was dead and already converted to declaration.
        GV.isDeclaration())
      return;

    // Set the potentially more constraining visibility computed from summaries.
    // The DefaultVisibility condition is because older GlobalValueSummary does
    // not record DefaultVisibility and we don't want to change protected/hidden
    // to default.
    if (GS->second->getVisibility() != GlobalValue::DefaultVisibility)
      GV.setVisibility(GS->second->getVisibility());

    if (NewLinkage == GV.getLinkage())
      return;

    // Check for a non-prevailing def that has interposable linkage
    // (e.g. non-odr weak or linkonce). In that case we can't simply
    // convert to available_externally, since it would lose the
    // interposable property and possibly get inlined. Simply drop
    // the definition in that case.
    if (GlobalValue::isAvailableExternallyLinkage(NewLinkage) &&
        GlobalValue::isInterposableLinkage(GV.getLinkage())) {
      if (!convertToDeclaration(GV))
        // FIXME: Change this to collect replaced GVs and later erase
        // them from the parent module once thinLTOResolvePrevailingGUID is
        // changed to enable this for aliases.
        llvm_unreachable("Expected GV to be converted");
    } else {
      // If all copies of the original symbol had global unnamed addr and
      // linkonce_odr linkage, or if all of them had local unnamed addr linkage
      // and are constants, then it should be an auto hide symbol. In that case
      // the thin link would have marked it as CanAutoHide. Add hidden
      // visibility to the symbol to preserve the property.
      if (NewLinkage == GlobalValue::WeakODRLinkage &&
          GS->second->canAutoHide()) {
        assert(GV.canBeOmittedFromSymbolTable());
        GV.setVisibility(GlobalValue::HiddenVisibility);
      }

      LLVM_DEBUG(dbgs() << "ODR fixing up linkage for `" << GV.getName()
                        << "` from " << GV.getLinkage() << " to " << NewLinkage
                        << "\n");
      GV.setLinkage(NewLinkage);
    }
    // Remove declarations from comdats, including available_externally
    // as this is a declaration for the linker, and will be dropped eventually.
    // It is illegal for comdats to contain declarations.
    auto *GO = dyn_cast_or_null<GlobalObject>(&GV);
    if (GO && GO->isDeclarationForLinker() && GO->hasComdat()) {
      if (GO->getComdat()->getName() == GO->getName())
        NonPrevailingComdats.insert(GO->getComdat());
      GO->setComdat(nullptr);
    }
  };

  // Process functions and global now
  for (auto &GV : TheModule)
    FinalizeInModule(GV, PropagateAttrs);
  for (auto &GV : TheModule.globals())
    FinalizeInModule(GV);
  for (auto &GV : TheModule.aliases())
    FinalizeInModule(GV);

  // For a non-prevailing comdat, all its members must be available_externally.
  // FinalizeInModule has handled non-local-linkage GlobalValues. Here we handle
  // local linkage GlobalValues.
  if (NonPrevailingComdats.empty())
    return;
  for (auto &GO : TheModule.global_objects()) {
    if (auto *C = GO.getComdat(); C && NonPrevailingComdats.count(C)) {
      GO.setComdat(nullptr);
      GO.setLinkage(GlobalValue::AvailableExternallyLinkage);
    }
  }
  bool Changed;
  do {
    Changed = false;
    // If an alias references a GlobalValue in a non-prevailing comdat, change
    // it to available_externally. For simplicity we only handle GlobalValue and
    // ConstantExpr with a base object. ConstantExpr without a base object is
    // unlikely used in a COMDAT.
    for (auto &GA : TheModule.aliases()) {
      if (GA.hasAvailableExternallyLinkage())
        continue;
      GlobalObject *Obj = GA.getAliaseeObject();
      assert(Obj && "aliasee without an base object is unimplemented");
      if (Obj->hasAvailableExternallyLinkage()) {
        GA.setLinkage(GlobalValue::AvailableExternallyLinkage);
        Changed = true;
      }
    }
  } while (Changed);
}

/// Run internalization on \p TheModule based on symmary analysis.
void llvm::thinLTOInternalizeModule(Module &TheModule,
                                    const GVSummaryMapTy &DefinedGlobals) {
  // Declare a callback for the internalize pass that will ask for every
  // candidate GlobalValue if it can be internalized or not.
  auto MustPreserveGV = [&](const GlobalValue &GV) -> bool {
    // It may be the case that GV is on a chain of an ifunc, its alias and
    // subsequent aliases. In this case, the summary for the value is not
    // available.
    if (isa<GlobalIFunc>(&GV) ||
        (isa<GlobalAlias>(&GV) &&
         isa<GlobalIFunc>(cast<GlobalAlias>(&GV)->getAliaseeObject())))
      return true;

    // Lookup the linkage recorded in the summaries during global analysis.
    auto GS = DefinedGlobals.find(GV.getGUID());
    if (GS == DefinedGlobals.end()) {
      // Must have been promoted (possibly conservatively). Find original
      // name so that we can access the correct summary and see if it can
      // be internalized again.
      // FIXME: Eventually we should control promotion instead of promoting
      // and internalizing again.
      StringRef OrigName =
          ModuleSummaryIndex::getOriginalNameBeforePromote(GV.getName());
      std::string OrigId = GlobalValue::getGlobalIdentifier(
          OrigName, GlobalValue::InternalLinkage,
          TheModule.getSourceFileName());
      GS = DefinedGlobals.find(GlobalValue::getGUID(OrigId));
      if (GS == DefinedGlobals.end()) {
        // Also check the original non-promoted non-globalized name. In some
        // cases a preempted weak value is linked in as a local copy because
        // it is referenced by an alias (IRLinker::linkGlobalValueProto).
        // In that case, since it was originally not a local value, it was
        // recorded in the index using the original name.
        // FIXME: This may not be needed once PR27866 is fixed.
        GS = DefinedGlobals.find(GlobalValue::getGUID(OrigName));
        assert(GS != DefinedGlobals.end());
      }
    }
    return !GlobalValue::isLocalLinkage(GS->second->linkage());
  };

  // FIXME: See if we can just internalize directly here via linkage changes
  // based on the index, rather than invoking internalizeModule.
  internalizeModule(TheModule, MustPreserveGV);
}

/// Make alias a clone of its aliasee.
static Function *replaceAliasWithAliasee(Module *SrcModule, GlobalAlias *GA) {
  Function *Fn = cast<Function>(GA->getAliaseeObject());

  ValueToValueMapTy VMap;
  Function *NewFn = CloneFunction(Fn, VMap);
  // Clone should use the original alias's linkage, visibility and name, and we
  // ensure all uses of alias instead use the new clone (casted if necessary).
  NewFn->setLinkage(GA->getLinkage());
  NewFn->setVisibility(GA->getVisibility());
  GA->replaceAllUsesWith(NewFn);
  NewFn->takeName(GA);
  return NewFn;
}

// Internalize values that we marked with specific attribute
// in processGlobalForThinLTO.
static void internalizeGVsAfterImport(Module &M) {
  for (auto &GV : M.globals())
    // Skip GVs which have been converted to declarations
    // by dropDeadSymbols.
    if (!GV.isDeclaration() && GV.hasAttribute("thinlto-internalize")) {
      GV.setLinkage(GlobalValue::InternalLinkage);
      GV.setVisibility(GlobalValue::DefaultVisibility);
    }
}

// Automatically import functions in Module \p DestModule based on the summaries
// index.
Expected<bool> FunctionImporter::importFunctions(
    Module &DestModule, const FunctionImporter::ImportMapTy &ImportList) {
  LLVM_DEBUG(dbgs() << "Starting import for Module "
                    << DestModule.getModuleIdentifier() << "\n");
  unsigned ImportedCount = 0, ImportedGVCount = 0;

  IRMover Mover(DestModule);
  // Do the actual import of functions now, one Module at a time
  std::set<StringRef> ModuleNameOrderedList;
  for (const auto &FunctionsToImportPerModule : ImportList) {
    ModuleNameOrderedList.insert(FunctionsToImportPerModule.first);
  }

  auto getImportType = [&](const FunctionsToImportTy &GUIDToImportType,
                           GlobalValue::GUID GUID)
      -> std::optional<GlobalValueSummary::ImportKind> {
    auto Iter = GUIDToImportType.find(GUID);
    if (Iter == GUIDToImportType.end())
      return std::nullopt;
    return Iter->second;
  };

  for (const auto &Name : ModuleNameOrderedList) {
    // Get the module for the import
    const auto &FunctionsToImportPerModule = ImportList.find(Name);
    assert(FunctionsToImportPerModule != ImportList.end());
    Expected<std::unique_ptr<Module>> SrcModuleOrErr = ModuleLoader(Name);
    if (!SrcModuleOrErr)
      return SrcModuleOrErr.takeError();
    std::unique_ptr<Module> SrcModule = std::move(*SrcModuleOrErr);
    assert(&DestModule.getContext() == &SrcModule->getContext() &&
           "Context mismatch");

    // If modules were created with lazy metadata loading, materialize it
    // now, before linking it (otherwise this will be a noop).
    if (Error Err = SrcModule->materializeMetadata())
      return std::move(Err);

    auto &ImportGUIDs = FunctionsToImportPerModule->second;

    // Find the globals to import
    SetVector<GlobalValue *> GlobalsToImport;
    for (Function &F : *SrcModule) {
      if (!F.hasName())
        continue;
      auto GUID = F.getGUID();
      auto MaybeImportType = getImportType(ImportGUIDs, GUID);

      bool ImportDefinition =
          (MaybeImportType &&
           (*MaybeImportType == GlobalValueSummary::Definition));

      LLVM_DEBUG(dbgs() << (MaybeImportType ? "Is" : "Not")
                        << " importing function"
                        << (ImportDefinition
                                ? " definition "
                                : (MaybeImportType ? " declaration " : " "))
                        << GUID << " " << F.getName() << " from "
                        << SrcModule->getSourceFileName() << "\n");
      if (ImportDefinition) {
        if (Error Err = F.materialize())
          return std::move(Err);
        // MemProf should match function's definition and summary,
        // 'thinlto_src_module' is needed.
        if (EnableImportMetadata || EnableMemProfContextDisambiguation) {
          // Add 'thinlto_src_module' and 'thinlto_src_file' metadata for
          // statistics and debugging.
          F.setMetadata(
              "thinlto_src_module",
              MDNode::get(DestModule.getContext(),
                          {MDString::get(DestModule.getContext(),
                                         SrcModule->getModuleIdentifier())}));
          F.setMetadata(
              "thinlto_src_file",
              MDNode::get(DestModule.getContext(),
                          {MDString::get(DestModule.getContext(),
                                         SrcModule->getSourceFileName())}));
        }
        GlobalsToImport.insert(&F);
      }
    }
    for (GlobalVariable &GV : SrcModule->globals()) {
      if (!GV.hasName())
        continue;
      auto GUID = GV.getGUID();
      auto MaybeImportType = getImportType(ImportGUIDs, GUID);

      bool ImportDefinition =
          (MaybeImportType &&
           (*MaybeImportType == GlobalValueSummary::Definition));

      LLVM_DEBUG(dbgs() << (MaybeImportType ? "Is" : "Not")
                        << " importing global"
                        << (ImportDefinition
                                ? " definition "
                                : (MaybeImportType ? " declaration " : " "))
                        << GUID << " " << GV.getName() << " from "
                        << SrcModule->getSourceFileName() << "\n");
      if (ImportDefinition) {
        if (Error Err = GV.materialize())
          return std::move(Err);
        ImportedGVCount += GlobalsToImport.insert(&GV);
      }
    }
    for (GlobalAlias &GA : SrcModule->aliases()) {
      if (!GA.hasName() || isa<GlobalIFunc>(GA.getAliaseeObject()))
        continue;
      auto GUID = GA.getGUID();
      auto MaybeImportType = getImportType(ImportGUIDs, GUID);

      bool ImportDefinition =
          (MaybeImportType &&
           (*MaybeImportType == GlobalValueSummary::Definition));

      LLVM_DEBUG(dbgs() << (MaybeImportType ? "Is" : "Not")
                        << " importing alias"
                        << (ImportDefinition
                                ? " definition "
                                : (MaybeImportType ? " declaration " : " "))
                        << GUID << " " << GA.getName() << " from "
                        << SrcModule->getSourceFileName() << "\n");
      if (ImportDefinition) {
        if (Error Err = GA.materialize())
          return std::move(Err);
        // Import alias as a copy of its aliasee.
        GlobalObject *GO = GA.getAliaseeObject();
        if (Error Err = GO->materialize())
          return std::move(Err);
        auto *Fn = replaceAliasWithAliasee(SrcModule.get(), &GA);
        LLVM_DEBUG(dbgs() << "Is importing aliasee fn " << GO->getGUID() << " "
                          << GO->getName() << " from "
                          << SrcModule->getSourceFileName() << "\n");
        if (EnableImportMetadata || EnableMemProfContextDisambiguation) {
          // Add 'thinlto_src_module' and 'thinlto_src_file' metadata for
          // statistics and debugging.
          Fn->setMetadata(
              "thinlto_src_module",
              MDNode::get(DestModule.getContext(),
                          {MDString::get(DestModule.getContext(),
                                         SrcModule->getModuleIdentifier())}));
          Fn->setMetadata(
              "thinlto_src_file",
              MDNode::get(DestModule.getContext(),
                          {MDString::get(DestModule.getContext(),
                                         SrcModule->getSourceFileName())}));
        }
        GlobalsToImport.insert(Fn);
      }
    }

    // Upgrade debug info after we're done materializing all the globals and we
    // have loaded all the required metadata!
    UpgradeDebugInfo(*SrcModule);

    // Set the partial sample profile ratio in the profile summary module flag
    // of the imported source module, if applicable, so that the profile summary
    // module flag will match with that of the destination module when it's
    // imported.
    SrcModule->setPartialSampleProfileRatio(Index);

    // Link in the specified functions.
    if (renameModuleForThinLTO(*SrcModule, Index, ClearDSOLocalOnDeclarations,
                               &GlobalsToImport))
      return true;

    if (PrintImports) {
      for (const auto *GV : GlobalsToImport)
        dbgs() << DestModule.getSourceFileName() << ": Import " << GV->getName()
               << " from " << SrcModule->getSourceFileName() << "\n";
    }

    if (Error Err = Mover.move(std::move(SrcModule),
                               GlobalsToImport.getArrayRef(), nullptr,
                               /*IsPerformingImport=*/true))
      return createStringError(errc::invalid_argument,
                               Twine("Function Import: link error: ") +
                                   toString(std::move(Err)));

    ImportedCount += GlobalsToImport.size();
    NumImportedModules++;
  }

  internalizeGVsAfterImport(DestModule);

  NumImportedFunctions += (ImportedCount - ImportedGVCount);
  NumImportedGlobalVars += ImportedGVCount;

  // TODO: Print counters for definitions and declarations in the debugging log.
  LLVM_DEBUG(dbgs() << "Imported " << ImportedCount - ImportedGVCount
                    << " functions for Module "
                    << DestModule.getModuleIdentifier() << "\n");
  LLVM_DEBUG(dbgs() << "Imported " << ImportedGVCount
                    << " global variables for Module "
                    << DestModule.getModuleIdentifier() << "\n");
  return ImportedCount;
}

static bool doImportingForModuleForTest(
    Module &M, function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
                   isPrevailing) {
  if (SummaryFile.empty())
    report_fatal_error("error: -function-import requires -summary-file\n");
  Expected<std::unique_ptr<ModuleSummaryIndex>> IndexPtrOrErr =
      getModuleSummaryIndexForFile(SummaryFile);
  if (!IndexPtrOrErr) {
    logAllUnhandledErrors(IndexPtrOrErr.takeError(), errs(),
                          "Error loading file '" + SummaryFile + "': ");
    return false;
  }
  std::unique_ptr<ModuleSummaryIndex> Index = std::move(*IndexPtrOrErr);

  // First step is collecting the import list.
  FunctionImporter::ImportMapTy ImportList;
  // If requested, simply import all functions in the index. This is used
  // when testing distributed backend handling via the opt tool, when
  // we have distributed indexes containing exactly the summaries to import.
  if (ImportAllIndex)
    ComputeCrossModuleImportForModuleFromIndexForTest(M.getModuleIdentifier(),
                                                      *Index, ImportList);
  else
    ComputeCrossModuleImportForModuleForTest(M.getModuleIdentifier(),
                                             isPrevailing, *Index, ImportList);

  // Conservatively mark all internal values as promoted. This interface is
  // only used when doing importing via the function importing pass. The pass
  // is only enabled when testing importing via the 'opt' tool, which does
  // not do the ThinLink that would normally determine what values to promote.
  for (auto &I : *Index) {
    for (auto &S : I.second.SummaryList) {
      if (GlobalValue::isLocalLinkage(S->linkage()))
        S->setLinkage(GlobalValue::ExternalLinkage);
    }
  }

  // Next we need to promote to global scope and rename any local values that
  // are potentially exported to other modules.
  if (renameModuleForThinLTO(M, *Index, /*ClearDSOLocalOnDeclarations=*/false,
                             /*GlobalsToImport=*/nullptr)) {
    errs() << "Error renaming module\n";
    return true;
  }

  // Perform the import now.
  auto ModuleLoader = [&M](StringRef Identifier) {
    return loadFile(std::string(Identifier), M.getContext());
  };
  FunctionImporter Importer(*Index, ModuleLoader,
                            /*ClearDSOLocalOnDeclarations=*/false);
  Expected<bool> Result = Importer.importFunctions(M, ImportList);

  // FIXME: Probably need to propagate Errors through the pass manager.
  if (!Result) {
    logAllUnhandledErrors(Result.takeError(), errs(),
                          "Error importing module: ");
    return true;
  }

  return true;
}

PreservedAnalyses FunctionImportPass::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  // This is only used for testing the function import pass via opt, where we
  // don't have prevailing information from the LTO context available, so just
  // conservatively assume everything is prevailing (which is fine for the very
  // limited use of prevailing checking in this pass).
  auto isPrevailing = [](GlobalValue::GUID, const GlobalValueSummary *) {
    return true;
  };
  if (!doImportingForModuleForTest(M, isPrevailing))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}
