//===- Standard pass instrumentations handling ----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines IR-printing pass instrumentation callbacks as well as
/// StandardInstrumentations class that manages standard pass instrumentations.
///
//===----------------------------------------------------------------------===//

#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/ADT/Any.h"
#include "llvm/ADT/StableHashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineVerifier.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/IR/StructuralHash.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<bool> VerifyAnalysisInvalidation("verify-analysis-invalidation",
                                                cl::Hidden,
#ifdef EXPENSIVE_CHECKS
                                                cl::init(true)
#else
                                                cl::init(false)
#endif
);

// An option that supports the -print-changed option.  See
// the description for -print-changed for an explanation of the use
// of this option.  Note that this option has no effect without -print-changed.
static cl::opt<bool>
    PrintChangedBefore("print-before-changed",
                       cl::desc("Print before passes that change them"),
                       cl::init(false), cl::Hidden);

// An option for specifying the dot used by
// print-changed=[dot-cfg | dot-cfg-quiet]
static cl::opt<std::string>
    DotBinary("print-changed-dot-path", cl::Hidden, cl::init("dot"),
              cl::desc("system dot used by change reporters"));

// An option that determines the colour used for elements that are only
// in the before part.  Must be a colour named in appendix J of
// https://graphviz.org/pdf/dotguide.pdf
static cl::opt<std::string>
    BeforeColour("dot-cfg-before-color",
                 cl::desc("Color for dot-cfg before elements"), cl::Hidden,
                 cl::init("red"));
// An option that determines the colour used for elements that are only
// in the after part.  Must be a colour named in appendix J of
// https://graphviz.org/pdf/dotguide.pdf
static cl::opt<std::string>
    AfterColour("dot-cfg-after-color",
                cl::desc("Color for dot-cfg after elements"), cl::Hidden,
                cl::init("forestgreen"));
// An option that determines the colour used for elements that are in both
// the before and after parts.  Must be a colour named in appendix J of
// https://graphviz.org/pdf/dotguide.pdf
static cl::opt<std::string>
    CommonColour("dot-cfg-common-color",
                 cl::desc("Color for dot-cfg common elements"), cl::Hidden,
                 cl::init("black"));

// An option that determines where the generated website file (named
// passes.html) and the associated pdf files (named diff_*.pdf) are saved.
static cl::opt<std::string> DotCfgDir(
    "dot-cfg-dir",
    cl::desc("Generate dot files into specified directory for changed IRs"),
    cl::Hidden, cl::init("./"));

// Options to print the IR that was being processed when a pass crashes.
static cl::opt<std::string> PrintOnCrashPath(
    "print-on-crash-path",
    cl::desc("Print the last form of the IR before crash to a file"),
    cl::Hidden);

static cl::opt<bool> PrintOnCrash(
    "print-on-crash",
    cl::desc("Print the last form of the IR before crash (use -print-on-crash-path to dump to a file)"),
    cl::Hidden);

static cl::opt<std::string> OptBisectPrintIRPath(
    "opt-bisect-print-ir-path",
    cl::desc("Print IR to path when opt-bisect-limit is reached"), cl::Hidden);

static cl::opt<bool> PrintPassNumbers(
    "print-pass-numbers", cl::init(false), cl::Hidden,
    cl::desc("Print pass names and their ordinals"));

static cl::opt<unsigned> PrintBeforePassNumber(
    "print-before-pass-number", cl::init(0), cl::Hidden,
    cl::desc("Print IR before the pass with this number as "
             "reported by print-pass-numbers"));

static cl::opt<unsigned>
    PrintAfterPassNumber("print-after-pass-number", cl::init(0), cl::Hidden,
                         cl::desc("Print IR after the pass with this number as "
                                  "reported by print-pass-numbers"));

static cl::opt<std::string> IRDumpDirectory(
    "ir-dump-directory",
    cl::desc("If specified, IR printed using the "
             "-print-[before|after]{-all} options will be dumped into "
             "files in this directory rather than written to stderr"),
    cl::Hidden, cl::value_desc("filename"));

template <typename IRUnitT> static const IRUnitT *unwrapIR(Any IR) {
  const IRUnitT **IRPtr = llvm::any_cast<const IRUnitT *>(&IR);
  return IRPtr ? *IRPtr : nullptr;
}

namespace {

// An option for specifying an executable that will be called with the IR
// everytime it changes in the opt pipeline.  It will also be called on
// the initial IR as it enters the pipeline.  The executable will be passed
// the name of a temporary file containing the IR and the PassID.  This may
// be used, for example, to call llc on the IR and run a test to determine
// which pass makes a change that changes the functioning of the IR.
// The usual modifier options work as expected.
static cl::opt<std::string>
    TestChanged("exec-on-ir-change", cl::Hidden, cl::init(""),
                cl::desc("exe called with module IR after each pass that "
                         "changes it"));

/// Extract Module out of \p IR unit. May return nullptr if \p IR does not match
/// certain global filters. Will never return nullptr if \p Force is true.
const Module *unwrapModule(Any IR, bool Force = false) {
  if (const auto *M = unwrapIR<Module>(IR))
    return M;

  if (const auto *F = unwrapIR<Function>(IR)) {
    if (!Force && !isFunctionInPrintList(F->getName()))
      return nullptr;

    return F->getParent();
  }

  if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR)) {
    for (const LazyCallGraph::Node &N : *C) {
      const Function &F = N.getFunction();
      if (Force || (!F.isDeclaration() && isFunctionInPrintList(F.getName()))) {
        return F.getParent();
      }
    }
    assert(!Force && "Expected a module");
    return nullptr;
  }

  if (const auto *L = unwrapIR<Loop>(IR)) {
    const Function *F = L->getHeader()->getParent();
    if (!Force && !isFunctionInPrintList(F->getName()))
      return nullptr;
    return F->getParent();
  }

  if (const auto *MF = unwrapIR<MachineFunction>(IR)) {
    if (!Force && !isFunctionInPrintList(MF->getName()))
      return nullptr;
    return MF->getFunction().getParent();
  }

  llvm_unreachable("Unknown IR unit");
}

void printIR(raw_ostream &OS, const Function *F) {
  if (!isFunctionInPrintList(F->getName()))
    return;
  OS << *F;
}

void printIR(raw_ostream &OS, const Module *M) {
  if (isFunctionInPrintList("*") || forcePrintModuleIR()) {
    M->print(OS, nullptr);
  } else {
    for (const auto &F : M->functions()) {
      printIR(OS, &F);
    }
  }
}

void printIR(raw_ostream &OS, const LazyCallGraph::SCC *C) {
  for (const LazyCallGraph::Node &N : *C) {
    const Function &F = N.getFunction();
    if (!F.isDeclaration() && isFunctionInPrintList(F.getName())) {
      F.print(OS);
    }
  }
}

void printIR(raw_ostream &OS, const Loop *L) {
  const Function *F = L->getHeader()->getParent();
  if (!isFunctionInPrintList(F->getName()))
    return;
  printLoop(const_cast<Loop &>(*L), OS);
}

void printIR(raw_ostream &OS, const MachineFunction *MF) {
  if (!isFunctionInPrintList(MF->getName()))
    return;
  MF->print(OS);
}

std::string getIRName(Any IR) {
  if (unwrapIR<Module>(IR))
    return "[module]";

  if (const auto *F = unwrapIR<Function>(IR))
    return F->getName().str();

  if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR))
    return C->getName();

  if (const auto *L = unwrapIR<Loop>(IR))
    return "loop %" + L->getName().str() + " in function " +
           L->getHeader()->getParent()->getName().str();

  if (const auto *MF = unwrapIR<MachineFunction>(IR))
    return MF->getName().str();

  llvm_unreachable("Unknown wrapped IR type");
}

bool moduleContainsFilterPrintFunc(const Module &M) {
  return any_of(M.functions(),
                [](const Function &F) {
                  return isFunctionInPrintList(F.getName());
                }) ||
         isFunctionInPrintList("*");
}

bool sccContainsFilterPrintFunc(const LazyCallGraph::SCC &C) {
  return any_of(C,
                [](const LazyCallGraph::Node &N) {
                  return isFunctionInPrintList(N.getName());
                }) ||
         isFunctionInPrintList("*");
}

bool shouldPrintIR(Any IR) {
  if (const auto *M = unwrapIR<Module>(IR))
    return moduleContainsFilterPrintFunc(*M);

  if (const auto *F = unwrapIR<Function>(IR))
    return isFunctionInPrintList(F->getName());

  if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR))
    return sccContainsFilterPrintFunc(*C);

  if (const auto *L = unwrapIR<Loop>(IR))
    return isFunctionInPrintList(L->getHeader()->getParent()->getName());

  if (const auto *MF = unwrapIR<MachineFunction>(IR))
    return isFunctionInPrintList(MF->getName());
  llvm_unreachable("Unknown wrapped IR type");
}

/// Generic IR-printing helper that unpacks a pointer to IRUnit wrapped into
/// Any and does actual print job.
void unwrapAndPrint(raw_ostream &OS, Any IR) {
  if (!shouldPrintIR(IR))
    return;

  if (forcePrintModuleIR()) {
    auto *M = unwrapModule(IR);
    assert(M && "should have unwrapped module");
    printIR(OS, M);
    return;
  }

  if (const auto *M = unwrapIR<Module>(IR)) {
    printIR(OS, M);
    return;
  }

  if (const auto *F = unwrapIR<Function>(IR)) {
    printIR(OS, F);
    return;
  }

  if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR)) {
    printIR(OS, C);
    return;
  }

  if (const auto *L = unwrapIR<Loop>(IR)) {
    printIR(OS, L);
    return;
  }

  if (const auto *MF = unwrapIR<MachineFunction>(IR)) {
    printIR(OS, MF);
    return;
  }
  llvm_unreachable("Unknown wrapped IR type");
}

// Return true when this is a pass for which changes should be ignored
bool isIgnored(StringRef PassID) {
  return isSpecialPass(PassID,
                       {"PassManager", "PassAdaptor", "AnalysisManagerProxy",
                        "DevirtSCCRepeatedPass", "ModuleInlinerWrapperPass",
                        "VerifierPass", "PrintModulePass", "PrintMIRPass",
                        "PrintMIRPreparePass"});
}

std::string makeHTMLReady(StringRef SR) {
  std::string S;
  while (true) {
    StringRef Clean =
        SR.take_until([](char C) { return C == '<' || C == '>'; });
    S.append(Clean.str());
    SR = SR.drop_front(Clean.size());
    if (SR.size() == 0)
      return S;
    S.append(SR[0] == '<' ? "&lt;" : "&gt;");
    SR = SR.drop_front();
  }
  llvm_unreachable("problems converting string to HTML");
}

// Return the module when that is the appropriate level of comparison for \p IR.
const Module *getModuleForComparison(Any IR) {
  if (const auto *M = unwrapIR<Module>(IR))
    return M;
  if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR))
    return C->begin()->getFunction().getParent();
  return nullptr;
}

bool isInterestingFunction(const Function &F) {
  return isFunctionInPrintList(F.getName());
}

// Return true when this is a pass on IR for which printing
// of changes is desired.
bool isInteresting(Any IR, StringRef PassID, StringRef PassName) {
  if (isIgnored(PassID) || !isPassInPrintList(PassName))
    return false;
  if (const auto *F = unwrapIR<Function>(IR))
    return isInterestingFunction(*F);
  return true;
}

} // namespace

template <typename T> ChangeReporter<T>::~ChangeReporter() {
  assert(BeforeStack.empty() && "Problem with Change Printer stack.");
}

template <typename T>
void ChangeReporter<T>::saveIRBeforePass(Any IR, StringRef PassID,
                                         StringRef PassName) {
  // Is this the initial IR?
  if (InitialIR) {
    InitialIR = false;
    if (VerboseMode)
      handleInitialIR(IR);
  }

  // Always need to place something on the stack because invalidated passes
  // are not given the IR so it cannot be determined whether the pass was for
  // something that was filtered out.
  BeforeStack.emplace_back();

  if (!isInteresting(IR, PassID, PassName))
    return;

  // Save the IR representation on the stack.
  T &Data = BeforeStack.back();
  generateIRRepresentation(IR, PassID, Data);
}

template <typename T>
void ChangeReporter<T>::handleIRAfterPass(Any IR, StringRef PassID,
                                          StringRef PassName) {
  assert(!BeforeStack.empty() && "Unexpected empty stack encountered.");

  std::string Name = getIRName(IR);

  if (isIgnored(PassID)) {
    if (VerboseMode)
      handleIgnored(PassID, Name);
  } else if (!isInteresting(IR, PassID, PassName)) {
    if (VerboseMode)
      handleFiltered(PassID, Name);
  } else {
    // Get the before rep from the stack
    T &Before = BeforeStack.back();
    // Create the after rep
    T After;
    generateIRRepresentation(IR, PassID, After);

    // Was there a change in IR?
    if (Before == After) {
      if (VerboseMode)
        omitAfter(PassID, Name);
    } else
      handleAfter(PassID, Name, Before, After, IR);
  }
  BeforeStack.pop_back();
}

template <typename T>
void ChangeReporter<T>::handleInvalidatedPass(StringRef PassID) {
  assert(!BeforeStack.empty() && "Unexpected empty stack encountered.");

  // Always flag it as invalidated as we cannot determine when
  // a pass for a filtered function is invalidated since we do not
  // get the IR in the call.  Also, the output is just alternate
  // forms of the banner anyway.
  if (VerboseMode)
    handleInvalidated(PassID);
  BeforeStack.pop_back();
}

template <typename T>
void ChangeReporter<T>::registerRequiredCallbacks(
    PassInstrumentationCallbacks &PIC) {
  PIC.registerBeforeNonSkippedPassCallback([&PIC, this](StringRef P, Any IR) {
    saveIRBeforePass(IR, P, PIC.getPassNameForClassName(P));
  });

  PIC.registerAfterPassCallback(
      [&PIC, this](StringRef P, Any IR, const PreservedAnalyses &) {
        handleIRAfterPass(IR, P, PIC.getPassNameForClassName(P));
      });
  PIC.registerAfterPassInvalidatedCallback(
      [this](StringRef P, const PreservedAnalyses &) {
        handleInvalidatedPass(P);
      });
}

template <typename T>
TextChangeReporter<T>::TextChangeReporter(bool Verbose)
    : ChangeReporter<T>(Verbose), Out(dbgs()) {}

template <typename T> void TextChangeReporter<T>::handleInitialIR(Any IR) {
  // Always print the module.
  // Unwrap and print directly to avoid filtering problems in general routines.
  auto *M = unwrapModule(IR, /*Force=*/true);
  assert(M && "Expected module to be unwrapped when forced.");
  Out << "*** IR Dump At Start ***\n";
  M->print(Out, nullptr);
}

template <typename T>
void TextChangeReporter<T>::omitAfter(StringRef PassID, std::string &Name) {
  Out << formatv("*** IR Dump After {0} on {1} omitted because no change ***\n",
                 PassID, Name);
}

template <typename T>
void TextChangeReporter<T>::handleInvalidated(StringRef PassID) {
  Out << formatv("*** IR Pass {0} invalidated ***\n", PassID);
}

template <typename T>
void TextChangeReporter<T>::handleFiltered(StringRef PassID,
                                           std::string &Name) {
  SmallString<20> Banner =
      formatv("*** IR Dump After {0} on {1} filtered out ***\n", PassID, Name);
  Out << Banner;
}

template <typename T>
void TextChangeReporter<T>::handleIgnored(StringRef PassID, std::string &Name) {
  Out << formatv("*** IR Pass {0} on {1} ignored ***\n", PassID, Name);
}

IRChangedPrinter::~IRChangedPrinter() = default;

void IRChangedPrinter::registerCallbacks(PassInstrumentationCallbacks &PIC) {
  if (PrintChanged == ChangePrinter::Verbose ||
      PrintChanged == ChangePrinter::Quiet)
    TextChangeReporter<std::string>::registerRequiredCallbacks(PIC);
}

void IRChangedPrinter::generateIRRepresentation(Any IR, StringRef PassID,
                                                std::string &Output) {
  raw_string_ostream OS(Output);
  unwrapAndPrint(OS, IR);
  OS.str();
}

void IRChangedPrinter::handleAfter(StringRef PassID, std::string &Name,
                                   const std::string &Before,
                                   const std::string &After, Any) {
  // Report the IR before the changes when requested.
  if (PrintChangedBefore)
    Out << "*** IR Dump Before " << PassID << " on " << Name << " ***\n"
        << Before;

  // We might not get anything to print if we only want to print a specific
  // function but it gets deleted.
  if (After.empty()) {
    Out << "*** IR Deleted After " << PassID << " on " << Name << " ***\n";
    return;
  }

  Out << "*** IR Dump After " << PassID << " on " << Name << " ***\n" << After;
}

IRChangedTester::~IRChangedTester() {}

void IRChangedTester::registerCallbacks(PassInstrumentationCallbacks &PIC) {
  if (TestChanged != "")
    TextChangeReporter<std::string>::registerRequiredCallbacks(PIC);
}

void IRChangedTester::handleIR(const std::string &S, StringRef PassID) {
  // Store the body into a temporary file
  static SmallVector<int> FD{-1};
  SmallVector<StringRef> SR{S};
  static SmallVector<std::string> FileName{""};
  if (prepareTempFiles(FD, SR, FileName)) {
    dbgs() << "Unable to create temporary file.";
    return;
  }
  static ErrorOr<std::string> Exe = sys::findProgramByName(TestChanged);
  if (!Exe) {
    dbgs() << "Unable to find test-changed executable.";
    return;
  }

  StringRef Args[] = {TestChanged, FileName[0], PassID};
  int Result = sys::ExecuteAndWait(*Exe, Args);
  if (Result < 0) {
    dbgs() << "Error executing test-changed executable.";
    return;
  }

  if (cleanUpTempFiles(FileName))
    dbgs() << "Unable to remove temporary file.";
}

void IRChangedTester::handleInitialIR(Any IR) {
  // Always test the initial module.
  // Unwrap and print directly to avoid filtering problems in general routines.
  std::string S;
  generateIRRepresentation(IR, "Initial IR", S);
  handleIR(S, "Initial IR");
}

void IRChangedTester::omitAfter(StringRef PassID, std::string &Name) {}
void IRChangedTester::handleInvalidated(StringRef PassID) {}
void IRChangedTester::handleFiltered(StringRef PassID, std::string &Name) {}
void IRChangedTester::handleIgnored(StringRef PassID, std::string &Name) {}
void IRChangedTester::handleAfter(StringRef PassID, std::string &Name,
                                  const std::string &Before,
                                  const std::string &After, Any) {
  handleIR(After, PassID);
}

template <typename T>
void OrderedChangedData<T>::report(
    const OrderedChangedData &Before, const OrderedChangedData &After,
    function_ref<void(const T *, const T *)> HandlePair) {
  const auto &BFD = Before.getData();
  const auto &AFD = After.getData();
  std::vector<std::string>::const_iterator BI = Before.getOrder().begin();
  std::vector<std::string>::const_iterator BE = Before.getOrder().end();
  std::vector<std::string>::const_iterator AI = After.getOrder().begin();
  std::vector<std::string>::const_iterator AE = After.getOrder().end();

  auto HandlePotentiallyRemovedData = [&](std::string S) {
    // The order in LLVM may have changed so check if still exists.
    if (!AFD.count(S)) {
      // This has been removed.
      HandlePair(&BFD.find(*BI)->getValue(), nullptr);
    }
  };
  auto HandleNewData = [&](std::vector<const T *> &Q) {
    // Print out any queued up new sections
    for (const T *NBI : Q)
      HandlePair(nullptr, NBI);
    Q.clear();
  };

  // Print out the data in the after order, with before ones interspersed
  // appropriately (ie, somewhere near where they were in the before list).
  // Start at the beginning of both lists.  Loop through the
  // after list.  If an element is common, then advance in the before list
  // reporting the removed ones until the common one is reached.  Report any
  // queued up new ones and then report the common one.  If an element is not
  // common, then enqueue it for reporting.  When the after list is exhausted,
  // loop through the before list, reporting any removed ones.  Finally,
  // report the rest of the enqueued new ones.
  std::vector<const T *> NewDataQueue;
  while (AI != AE) {
    if (!BFD.count(*AI)) {
      // This section is new so place it in the queue.  This will cause it
      // to be reported after deleted sections.
      NewDataQueue.emplace_back(&AFD.find(*AI)->getValue());
      ++AI;
      continue;
    }
    // This section is in both; advance and print out any before-only
    // until we get to it.
    // It's possible that this section has moved to be later than before. This
    // will mess up printing most blocks side by side, but it's a rare case and
    // it's better than crashing.
    while (BI != BE && *BI != *AI) {
      HandlePotentiallyRemovedData(*BI);
      ++BI;
    }
    // Report any new sections that were queued up and waiting.
    HandleNewData(NewDataQueue);

    const T &AData = AFD.find(*AI)->getValue();
    const T &BData = BFD.find(*AI)->getValue();
    HandlePair(&BData, &AData);
    if (BI != BE)
      ++BI;
    ++AI;
  }

  // Check any remaining before sections to see if they have been removed
  while (BI != BE) {
    HandlePotentiallyRemovedData(*BI);
    ++BI;
  }

  HandleNewData(NewDataQueue);
}

template <typename T>
void IRComparer<T>::compare(
    bool CompareModule,
    std::function<void(bool InModule, unsigned Minor,
                       const FuncDataT<T> &Before, const FuncDataT<T> &After)>
        CompareFunc) {
  if (!CompareModule) {
    // Just handle the single function.
    assert(Before.getData().size() == 1 && After.getData().size() == 1 &&
           "Expected only one function.");
    CompareFunc(false, 0, Before.getData().begin()->getValue(),
                After.getData().begin()->getValue());
    return;
  }

  unsigned Minor = 0;
  FuncDataT<T> Missing("");
  IRDataT<T>::report(Before, After,
                     [&](const FuncDataT<T> *B, const FuncDataT<T> *A) {
                       assert((B || A) && "Both functions cannot be missing.");
                       if (!B)
                         B = &Missing;
                       else if (!A)
                         A = &Missing;
                       CompareFunc(true, Minor++, *B, *A);
                     });
}

template <typename T> void IRComparer<T>::analyzeIR(Any IR, IRDataT<T> &Data) {
  if (const Module *M = getModuleForComparison(IR)) {
    // Create data for each existing/interesting function in the module.
    for (const Function &F : *M)
      generateFunctionData(Data, F);
    return;
  }

  if (const auto *F = unwrapIR<Function>(IR)) {
    generateFunctionData(Data, *F);
    return;
  }

  if (const auto *L = unwrapIR<Loop>(IR)) {
    auto *F = L->getHeader()->getParent();
    generateFunctionData(Data, *F);
    return;
  }

  if (const auto *MF = unwrapIR<MachineFunction>(IR)) {
    generateFunctionData(Data, *MF);
    return;
  }

  llvm_unreachable("Unknown IR unit");
}

static bool shouldGenerateData(const Function &F) {
  return !F.isDeclaration() && isFunctionInPrintList(F.getName());
}

static bool shouldGenerateData(const MachineFunction &MF) {
  return isFunctionInPrintList(MF.getName());
}

template <typename T>
template <typename FunctionT>
bool IRComparer<T>::generateFunctionData(IRDataT<T> &Data, const FunctionT &F) {
  if (shouldGenerateData(F)) {
    FuncDataT<T> FD(F.front().getName().str());
    int I = 0;
    for (const auto &B : F) {
      std::string BBName = B.getName().str();
      if (BBName.empty()) {
        BBName = formatv("{0}", I);
        ++I;
      }
      FD.getOrder().emplace_back(BBName);
      FD.getData().insert({BBName, B});
    }
    Data.getOrder().emplace_back(F.getName());
    Data.getData().insert({F.getName(), FD});
    return true;
  }
  return false;
}

PrintIRInstrumentation::~PrintIRInstrumentation() {
  assert(PassRunDescriptorStack.empty() &&
         "PassRunDescriptorStack is not empty at exit");
}

static SmallString<32> getIRFileDisplayName(Any IR) {
  SmallString<32> Result;
  raw_svector_ostream ResultStream(Result);
  const Module *M = unwrapModule(IR);
  stable_hash NameHash = stable_hash_combine_string(M->getName());
  unsigned int MaxHashWidth = sizeof(stable_hash) * 8 / 4;
  write_hex(ResultStream, NameHash, HexPrintStyle::Lower, MaxHashWidth);
  if (unwrapIR<Module>(IR)) {
    ResultStream << "-module";
  } else if (const auto *F = unwrapIR<Function>(IR)) {
    ResultStream << "-function-";
    stable_hash FunctionNameHash = stable_hash_combine_string(F->getName());
    write_hex(ResultStream, FunctionNameHash, HexPrintStyle::Lower,
              MaxHashWidth);
  } else if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR)) {
    ResultStream << "-scc-";
    stable_hash SCCNameHash = stable_hash_combine_string(C->getName());
    write_hex(ResultStream, SCCNameHash, HexPrintStyle::Lower, MaxHashWidth);
  } else if (const auto *L = unwrapIR<Loop>(IR)) {
    ResultStream << "-loop-";
    stable_hash LoopNameHash = stable_hash_combine_string(L->getName());
    write_hex(ResultStream, LoopNameHash, HexPrintStyle::Lower, MaxHashWidth);
  } else if (const auto *MF = unwrapIR<MachineFunction>(IR)) {
    ResultStream << "-machine-function-";
    stable_hash MachineFunctionNameHash =
        stable_hash_combine_string(MF->getName());
    write_hex(ResultStream, MachineFunctionNameHash, HexPrintStyle::Lower,
              MaxHashWidth);
  } else {
    llvm_unreachable("Unknown wrapped IR type");
  }
  return Result;
}

std::string PrintIRInstrumentation::fetchDumpFilename(StringRef PassName,
                                                      Any IR) {
  const StringRef RootDirectory = IRDumpDirectory;
  assert(!RootDirectory.empty() &&
         "The flag -ir-dump-directory must be passed to dump IR to files");
  SmallString<128> ResultPath;
  ResultPath += RootDirectory;
  SmallString<64> Filename;
  raw_svector_ostream FilenameStream(Filename);
  FilenameStream << CurrentPassNumber;
  FilenameStream << "-";
  FilenameStream << getIRFileDisplayName(IR);
  FilenameStream << "-";
  FilenameStream << PassName;
  sys::path::append(ResultPath, Filename);
  return std::string(ResultPath);
}

enum class IRDumpFileSuffixType {
  Before,
  After,
  Invalidated,
};

static StringRef getFileSuffix(IRDumpFileSuffixType Type) {
  static constexpr std::array FileSuffixes = {"-before.ll", "-after.ll",
                                              "-invalidated.ll"};
  return FileSuffixes[static_cast<size_t>(Type)];
}

void PrintIRInstrumentation::pushPassRunDescriptor(
    StringRef PassID, Any IR, std::string &DumpIRFilename) {
  const Module *M = unwrapModule(IR);
  PassRunDescriptorStack.emplace_back(
      PassRunDescriptor(M, DumpIRFilename, getIRName(IR), PassID));
}

PrintIRInstrumentation::PassRunDescriptor
PrintIRInstrumentation::popPassRunDescriptor(StringRef PassID) {
  assert(!PassRunDescriptorStack.empty() && "empty PassRunDescriptorStack");
  PassRunDescriptor Descriptor = PassRunDescriptorStack.pop_back_val();
  assert(Descriptor.PassID == PassID && "malformed PassRunDescriptorStack");
  return Descriptor;
}

// Callers are responsible for closing the returned file descriptor
static int prepareDumpIRFileDescriptor(const StringRef DumpIRFilename) {
  std::error_code EC;
  auto ParentPath = llvm::sys::path::parent_path(DumpIRFilename);
  if (!ParentPath.empty()) {
    std::error_code EC = llvm::sys::fs::create_directories(ParentPath);
    if (EC)
      report_fatal_error(Twine("Failed to create directory ") + ParentPath +
                         " to support -ir-dump-directory: " + EC.message());
  }
  int Result = 0;
  EC = sys::fs::openFile(DumpIRFilename, Result, sys::fs::CD_OpenAlways,
                         sys::fs::FA_Write, sys::fs::OF_Text);
  if (EC)
    report_fatal_error(Twine("Failed to open ") + DumpIRFilename +
                       " to support -ir-dump-directory: " + EC.message());
  return Result;
}

void PrintIRInstrumentation::printBeforePass(StringRef PassID, Any IR) {
  if (isIgnored(PassID))
    return;

  std::string DumpIRFilename;
  if (!IRDumpDirectory.empty() &&
      (shouldPrintBeforePass(PassID) || shouldPrintAfterPass(PassID) ||
       shouldPrintBeforeCurrentPassNumber() ||
       shouldPrintAfterCurrentPassNumber()))
    DumpIRFilename = fetchDumpFilename(PassID, IR);

  // Saving Module for AfterPassInvalidated operations.
  // Note: here we rely on a fact that we do not change modules while
  // traversing the pipeline, so the latest captured module is good
  // for all print operations that has not happen yet.
  if (shouldPrintAfterPass(PassID))
    pushPassRunDescriptor(PassID, IR, DumpIRFilename);

  if (!shouldPrintIR(IR))
    return;

  ++CurrentPassNumber;

  if (shouldPrintPassNumbers())
    dbgs() << " Running pass " << CurrentPassNumber << " " << PassID
           << " on " << getIRName(IR) << "\n";

  if (shouldPrintAfterCurrentPassNumber())
    pushPassRunDescriptor(PassID, IR, DumpIRFilename);

  if (!shouldPrintBeforePass(PassID) && !shouldPrintBeforeCurrentPassNumber())
    return;

  auto WriteIRToStream = [&](raw_ostream &Stream) {
    Stream << "; *** IR Dump Before ";
    if (shouldPrintBeforeSomePassNumber())
      Stream << CurrentPassNumber << "-";
    Stream << PassID << " on " << getIRName(IR) << " ***\n";
    unwrapAndPrint(Stream, IR);
  };

  if (!DumpIRFilename.empty()) {
    DumpIRFilename += getFileSuffix(IRDumpFileSuffixType::Before);
    llvm::raw_fd_ostream DumpIRFileStream{
        prepareDumpIRFileDescriptor(DumpIRFilename), /* shouldClose */ true};
    WriteIRToStream(DumpIRFileStream);
  } else {
    WriteIRToStream(dbgs());
  }
}

void PrintIRInstrumentation::printAfterPass(StringRef PassID, Any IR) {
  if (isIgnored(PassID))
    return;

  if (!shouldPrintAfterPass(PassID) && !shouldPrintAfterCurrentPassNumber())
    return;

  auto [M, DumpIRFilename, IRName, StoredPassID] = popPassRunDescriptor(PassID);
  assert(StoredPassID == PassID && "mismatched PassID");

  if (!shouldPrintIR(IR) ||
      (!shouldPrintAfterPass(PassID) && !shouldPrintAfterCurrentPassNumber()))
    return;

  auto WriteIRToStream = [&](raw_ostream &Stream, const StringRef IRName) {
    Stream << "; *** IR Dump After ";
    if (shouldPrintAfterSomePassNumber())
      Stream << CurrentPassNumber << "-";
    Stream << StringRef(formatv("{0}", PassID)) << " on " << IRName << " ***\n";
    unwrapAndPrint(Stream, IR);
  };

  if (!IRDumpDirectory.empty()) {
    assert(!DumpIRFilename.empty() && "DumpIRFilename must not be empty and "
                                      "should be set in printBeforePass");
    const std::string DumpIRFilenameWithSuffix =
        DumpIRFilename + getFileSuffix(IRDumpFileSuffixType::After).str();
    llvm::raw_fd_ostream DumpIRFileStream{
        prepareDumpIRFileDescriptor(DumpIRFilenameWithSuffix),
        /* shouldClose */ true};
    WriteIRToStream(DumpIRFileStream, IRName);
  } else {
    WriteIRToStream(dbgs(), IRName);
  }
}

void PrintIRInstrumentation::printAfterPassInvalidated(StringRef PassID) {
  if (isIgnored(PassID))
    return;

  if (!shouldPrintAfterPass(PassID) && !shouldPrintAfterCurrentPassNumber())
    return;

  auto [M, DumpIRFilename, IRName, StoredPassID] = popPassRunDescriptor(PassID);
  assert(StoredPassID == PassID && "mismatched PassID");
  // Additional filtering (e.g. -filter-print-func) can lead to module
  // printing being skipped.
  if (!M ||
      (!shouldPrintAfterPass(PassID) && !shouldPrintAfterCurrentPassNumber()))
    return;

  auto WriteIRToStream = [&](raw_ostream &Stream, const Module *M,
                             const StringRef IRName) {
    SmallString<20> Banner;
    Banner = formatv("; *** IR Dump After {0} on {1} (invalidated) ***", PassID,
                     IRName);
    Stream << Banner << "\n";
    printIR(Stream, M);
  };

  if (!IRDumpDirectory.empty()) {
    assert(!DumpIRFilename.empty() && "DumpIRFilename must not be empty and "
                                      "should be set in printBeforePass");
    const std::string DumpIRFilenameWithSuffix =
        DumpIRFilename + getFileSuffix(IRDumpFileSuffixType::Invalidated).str();
    llvm::raw_fd_ostream DumpIRFileStream{
        prepareDumpIRFileDescriptor(DumpIRFilenameWithSuffix),
        /* shouldClose */ true};
    WriteIRToStream(DumpIRFileStream, M, IRName);
  } else {
    WriteIRToStream(dbgs(), M, IRName);
  }
}

bool PrintIRInstrumentation::shouldPrintBeforePass(StringRef PassID) {
  if (shouldPrintBeforeAll())
    return true;

  StringRef PassName = PIC->getPassNameForClassName(PassID);
  return is_contained(printBeforePasses(), PassName);
}

bool PrintIRInstrumentation::shouldPrintAfterPass(StringRef PassID) {
  if (shouldPrintAfterAll())
    return true;

  StringRef PassName = PIC->getPassNameForClassName(PassID);
  return is_contained(printAfterPasses(), PassName);
}

bool PrintIRInstrumentation::shouldPrintBeforeCurrentPassNumber() {
  return shouldPrintBeforeSomePassNumber() &&
         (CurrentPassNumber == PrintBeforePassNumber);
}

bool PrintIRInstrumentation::shouldPrintAfterCurrentPassNumber() {
  return shouldPrintAfterSomePassNumber() &&
         (CurrentPassNumber == PrintAfterPassNumber);
}

bool PrintIRInstrumentation::shouldPrintPassNumbers() {
  return PrintPassNumbers;
}

bool PrintIRInstrumentation::shouldPrintBeforeSomePassNumber() {
  return PrintBeforePassNumber > 0;
}

bool PrintIRInstrumentation::shouldPrintAfterSomePassNumber() {
  return PrintAfterPassNumber > 0;
}

void PrintIRInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  this->PIC = &PIC;

  // BeforePass callback is not just for printing, it also saves a Module
  // for later use in AfterPassInvalidated and keeps tracks of the
  // CurrentPassNumber.
  if (shouldPrintPassNumbers() || shouldPrintBeforeSomePassNumber() ||
      shouldPrintAfterSomePassNumber() || shouldPrintBeforeSomePass() ||
      shouldPrintAfterSomePass())
    PIC.registerBeforeNonSkippedPassCallback(
        [this](StringRef P, Any IR) { this->printBeforePass(P, IR); });

  if (shouldPrintAfterSomePass() || shouldPrintAfterSomePassNumber()) {
    PIC.registerAfterPassCallback(
        [this](StringRef P, Any IR, const PreservedAnalyses &) {
          this->printAfterPass(P, IR);
        });
    PIC.registerAfterPassInvalidatedCallback(
        [this](StringRef P, const PreservedAnalyses &) {
          this->printAfterPassInvalidated(P);
        });
  }
}

void OptNoneInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  PIC.registerShouldRunOptionalPassCallback(
      [this](StringRef P, Any IR) { return this->shouldRun(P, IR); });
}

bool OptNoneInstrumentation::shouldRun(StringRef PassID, Any IR) {
  const auto *F = unwrapIR<Function>(IR);
  if (!F) {
    if (const auto *L = unwrapIR<Loop>(IR))
      F = L->getHeader()->getParent();
  }
  bool ShouldRun = !(F && F->hasOptNone());
  if (!ShouldRun && DebugLogging) {
    errs() << "Skipping pass " << PassID << " on " << F->getName()
           << " due to optnone attribute\n";
  }
  return ShouldRun;
}

bool OptPassGateInstrumentation::shouldRun(StringRef PassName, Any IR) {
  if (isIgnored(PassName))
    return true;

  bool ShouldRun =
      Context.getOptPassGate().shouldRunPass(PassName, getIRName(IR));
  if (!ShouldRun && !this->HasWrittenIR && !OptBisectPrintIRPath.empty()) {
    // FIXME: print IR if limit is higher than number of opt-bisect
    // invocations
    this->HasWrittenIR = true;
    const Module *M = unwrapModule(IR, /*Force=*/true);
    assert((M && &M->getContext() == &Context) && "Missing/Mismatching Module");
    std::error_code EC;
    raw_fd_ostream OS(OptBisectPrintIRPath, EC);
    if (EC)
      report_fatal_error(errorCodeToError(EC));
    M->print(OS, nullptr);
  }
  return ShouldRun;
}

void OptPassGateInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  OptPassGate &PassGate = Context.getOptPassGate();
  if (!PassGate.isEnabled())
    return;

  PIC.registerShouldRunOptionalPassCallback([this](StringRef PassName, Any IR) {
    return this->shouldRun(PassName, IR);
  });
}

raw_ostream &PrintPassInstrumentation::print() {
  if (Opts.Indent) {
    assert(Indent >= 0);
    dbgs().indent(Indent);
  }
  return dbgs();
}

void PrintPassInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  if (!Enabled)
    return;

  std::vector<StringRef> SpecialPasses;
  if (!Opts.Verbose) {
    SpecialPasses.emplace_back("PassManager");
    SpecialPasses.emplace_back("PassAdaptor");
  }

  PIC.registerBeforeSkippedPassCallback([this, SpecialPasses](StringRef PassID,
                                                              Any IR) {
    assert(!isSpecialPass(PassID, SpecialPasses) &&
           "Unexpectedly skipping special pass");

    print() << "Skipping pass: " << PassID << " on " << getIRName(IR) << "\n";
  });
  PIC.registerBeforeNonSkippedPassCallback([this, SpecialPasses](
                                               StringRef PassID, Any IR) {
    if (isSpecialPass(PassID, SpecialPasses))
      return;

    auto &OS = print();
    OS << "Running pass: " << PassID << " on " << getIRName(IR);
    if (const auto *F = unwrapIR<Function>(IR)) {
      unsigned Count = F->getInstructionCount();
      OS << " (" << Count << " instruction";
      if (Count != 1)
        OS << 's';
      OS << ')';
    } else if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR)) {
      int Count = C->size();
      OS << " (" << Count << " node";
      if (Count != 1)
        OS << 's';
      OS << ')';
    }
    OS << "\n";
    Indent += 2;
  });
  PIC.registerAfterPassCallback(
      [this, SpecialPasses](StringRef PassID, Any IR,
                            const PreservedAnalyses &) {
        if (isSpecialPass(PassID, SpecialPasses))
          return;

        Indent -= 2;
      });
  PIC.registerAfterPassInvalidatedCallback(
      [this, SpecialPasses](StringRef PassID, Any IR) {
        if (isSpecialPass(PassID, SpecialPasses))
          return;

        Indent -= 2;
      });

  if (!Opts.SkipAnalyses) {
    PIC.registerBeforeAnalysisCallback([this](StringRef PassID, Any IR) {
      print() << "Running analysis: " << PassID << " on " << getIRName(IR)
              << "\n";
      Indent += 2;
    });
    PIC.registerAfterAnalysisCallback(
        [this](StringRef PassID, Any IR) { Indent -= 2; });
    PIC.registerAnalysisInvalidatedCallback([this](StringRef PassID, Any IR) {
      print() << "Invalidating analysis: " << PassID << " on " << getIRName(IR)
              << "\n";
    });
    PIC.registerAnalysesClearedCallback([this](StringRef IRName) {
      print() << "Clearing all analysis results for: " << IRName << "\n";
    });
  }
}

PreservedCFGCheckerInstrumentation::CFG::CFG(const Function *F,
                                             bool TrackBBLifetime) {
  if (TrackBBLifetime)
    BBGuards = DenseMap<intptr_t, BBGuard>(F->size());
  for (const auto &BB : *F) {
    if (BBGuards)
      BBGuards->try_emplace(intptr_t(&BB), &BB);
    for (const auto *Succ : successors(&BB)) {
      Graph[&BB][Succ]++;
      if (BBGuards)
        BBGuards->try_emplace(intptr_t(Succ), Succ);
    }
  }
}

static void printBBName(raw_ostream &out, const BasicBlock *BB) {
  if (BB->hasName()) {
    out << BB->getName() << "<" << BB << ">";
    return;
  }

  if (!BB->getParent()) {
    out << "unnamed_removed<" << BB << ">";
    return;
  }

  if (BB->isEntryBlock()) {
    out << "entry"
        << "<" << BB << ">";
    return;
  }

  unsigned FuncOrderBlockNum = 0;
  for (auto &FuncBB : *BB->getParent()) {
    if (&FuncBB == BB)
      break;
    FuncOrderBlockNum++;
  }
  out << "unnamed_" << FuncOrderBlockNum << "<" << BB << ">";
}

void PreservedCFGCheckerInstrumentation::CFG::printDiff(raw_ostream &out,
                                                        const CFG &Before,
                                                        const CFG &After) {
  assert(!After.isPoisoned());
  if (Before.isPoisoned()) {
    out << "Some blocks were deleted\n";
    return;
  }

  // Find and print graph differences.
  if (Before.Graph.size() != After.Graph.size())
    out << "Different number of non-leaf basic blocks: before="
        << Before.Graph.size() << ", after=" << After.Graph.size() << "\n";

  for (auto &BB : Before.Graph) {
    auto BA = After.Graph.find(BB.first);
    if (BA == After.Graph.end()) {
      out << "Non-leaf block ";
      printBBName(out, BB.first);
      out << " is removed (" << BB.second.size() << " successors)\n";
    }
  }

  for (auto &BA : After.Graph) {
    auto BB = Before.Graph.find(BA.first);
    if (BB == Before.Graph.end()) {
      out << "Non-leaf block ";
      printBBName(out, BA.first);
      out << " is added (" << BA.second.size() << " successors)\n";
      continue;
    }

    if (BB->second == BA.second)
      continue;

    out << "Different successors of block ";
    printBBName(out, BA.first);
    out << " (unordered):\n";
    out << "- before (" << BB->second.size() << "): ";
    for (auto &SuccB : BB->second) {
      printBBName(out, SuccB.first);
      if (SuccB.second != 1)
        out << "(" << SuccB.second << "), ";
      else
        out << ", ";
    }
    out << "\n";
    out << "- after (" << BA.second.size() << "): ";
    for (auto &SuccA : BA.second) {
      printBBName(out, SuccA.first);
      if (SuccA.second != 1)
        out << "(" << SuccA.second << "), ";
      else
        out << ", ";
    }
    out << "\n";
  }
}

// PreservedCFGCheckerInstrumentation uses PreservedCFGCheckerAnalysis to check
// passes, that reported they kept CFG analyses up-to-date, did not actually
// change CFG. This check is done as follows. Before every functional pass in
// BeforeNonSkippedPassCallback a CFG snapshot (an instance of
// PreservedCFGCheckerInstrumentation::CFG) is requested from
// FunctionAnalysisManager as a result of PreservedCFGCheckerAnalysis. When the
// functional pass finishes and reports that CFGAnalyses or AllAnalyses are
// up-to-date then the cached result of PreservedCFGCheckerAnalysis (if
// available) is checked to be equal to a freshly created CFG snapshot.
struct PreservedCFGCheckerAnalysis
    : public AnalysisInfoMixin<PreservedCFGCheckerAnalysis> {
  friend AnalysisInfoMixin<PreservedCFGCheckerAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = PreservedCFGCheckerInstrumentation::CFG;

  /// Run the analysis pass over a function and produce CFG.
  Result run(Function &F, FunctionAnalysisManager &FAM) {
    return Result(&F, /* TrackBBLifetime */ true);
  }
};

AnalysisKey PreservedCFGCheckerAnalysis::Key;

struct PreservedFunctionHashAnalysis
    : public AnalysisInfoMixin<PreservedFunctionHashAnalysis> {
  static AnalysisKey Key;

  struct FunctionHash {
    uint64_t Hash;
  };

  using Result = FunctionHash;

  Result run(Function &F, FunctionAnalysisManager &FAM) {
    return Result{StructuralHash(F)};
  }
};

AnalysisKey PreservedFunctionHashAnalysis::Key;

struct PreservedModuleHashAnalysis
    : public AnalysisInfoMixin<PreservedModuleHashAnalysis> {
  static AnalysisKey Key;

  struct ModuleHash {
    uint64_t Hash;
  };

  using Result = ModuleHash;

  Result run(Module &F, ModuleAnalysisManager &FAM) {
    return Result{StructuralHash(F)};
  }
};

AnalysisKey PreservedModuleHashAnalysis::Key;

bool PreservedCFGCheckerInstrumentation::CFG::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &) {
  auto PAC = PA.getChecker<PreservedCFGCheckerAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>() ||
           PAC.preservedSet<CFGAnalyses>());
}

static SmallVector<Function *, 1> GetFunctions(Any IR) {
  SmallVector<Function *, 1> Functions;

  if (const auto *MaybeF = unwrapIR<Function>(IR)) {
    Functions.push_back(const_cast<Function *>(MaybeF));
  } else if (const auto *MaybeM = unwrapIR<Module>(IR)) {
    for (Function &F : *const_cast<Module *>(MaybeM))
      Functions.push_back(&F);
  }
  return Functions;
}

void PreservedCFGCheckerInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC, ModuleAnalysisManager &MAM) {
  if (!VerifyAnalysisInvalidation)
    return;

  bool Registered = false;
  PIC.registerBeforeNonSkippedPassCallback([this, &MAM, Registered](
                                               StringRef P, Any IR) mutable {
#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(&PassStack.emplace_back(P));
#endif
    (void)this;

    auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(
                       *const_cast<Module *>(unwrapModule(IR, /*Force=*/true)))
                    .getManager();
    if (!Registered) {
      FAM.registerPass([&] { return PreservedCFGCheckerAnalysis(); });
      FAM.registerPass([&] { return PreservedFunctionHashAnalysis(); });
      MAM.registerPass([&] { return PreservedModuleHashAnalysis(); });
      Registered = true;
    }

    for (Function *F : GetFunctions(IR)) {
      // Make sure a fresh CFG snapshot is available before the pass.
      FAM.getResult<PreservedCFGCheckerAnalysis>(*F);
      FAM.getResult<PreservedFunctionHashAnalysis>(*F);
    }

    if (const auto *MPtr = unwrapIR<Module>(IR)) {
      auto &M = *const_cast<Module *>(MPtr);
      MAM.getResult<PreservedModuleHashAnalysis>(M);
    }
  });

  PIC.registerAfterPassInvalidatedCallback(
      [this](StringRef P, const PreservedAnalyses &PassPA) {
#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
        assert(PassStack.pop_back_val() == P &&
               "Before and After callbacks must correspond");
#endif
        (void)this;
      });

  PIC.registerAfterPassCallback([this, &MAM](StringRef P, Any IR,
                                             const PreservedAnalyses &PassPA) {
#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(PassStack.pop_back_val() == P &&
           "Before and After callbacks must correspond");
#endif
    (void)this;

    // We have to get the FAM via the MAM, rather than directly use a passed in
    // FAM because if MAM has not cached the FAM, it won't invalidate function
    // analyses in FAM.
    auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(
                       *const_cast<Module *>(unwrapModule(IR, /*Force=*/true)))
                    .getManager();

    for (Function *F : GetFunctions(IR)) {
      if (auto *HashBefore =
              FAM.getCachedResult<PreservedFunctionHashAnalysis>(*F)) {
        if (HashBefore->Hash != StructuralHash(*F)) {
          report_fatal_error(formatv(
              "Function @{0} changed by {1} without invalidating analyses",
              F->getName(), P));
        }
      }

      auto CheckCFG = [](StringRef Pass, StringRef FuncName,
                         const CFG &GraphBefore, const CFG &GraphAfter) {
        if (GraphAfter == GraphBefore)
          return;

        dbgs()
            << "Error: " << Pass
            << " does not invalidate CFG analyses but CFG changes detected in "
               "function @"
            << FuncName << ":\n";
        CFG::printDiff(dbgs(), GraphBefore, GraphAfter);
        report_fatal_error(Twine("CFG unexpectedly changed by ", Pass));
      };

      if (auto *GraphBefore =
              FAM.getCachedResult<PreservedCFGCheckerAnalysis>(*F))
        CheckCFG(P, F->getName(), *GraphBefore,
                 CFG(F, /* TrackBBLifetime */ false));
    }
    if (const auto *MPtr = unwrapIR<Module>(IR)) {
      auto &M = *const_cast<Module *>(MPtr);
      if (auto *HashBefore =
              MAM.getCachedResult<PreservedModuleHashAnalysis>(M)) {
        if (HashBefore->Hash != StructuralHash(M)) {
          report_fatal_error(formatv(
              "Module changed by {0} without invalidating analyses", P));
        }
      }
    }
  });
}

void VerifyInstrumentation::registerCallbacks(PassInstrumentationCallbacks &PIC,
                                              ModuleAnalysisManager *MAM) {
  PIC.registerAfterPassCallback(
      [this, MAM](StringRef P, Any IR, const PreservedAnalyses &PassPA) {
        if (isIgnored(P) || P == "VerifierPass")
          return;
        const auto *F = unwrapIR<Function>(IR);
        if (!F) {
          if (const auto *L = unwrapIR<Loop>(IR))
            F = L->getHeader()->getParent();
        }

        if (F) {
          if (DebugLogging)
            dbgs() << "Verifying function " << F->getName() << "\n";

          if (verifyFunction(*F, &errs()))
            report_fatal_error(formatv("Broken function found after pass "
                                       "\"{0}\", compilation aborted!",
                                       P));
        } else {
          const auto *M = unwrapIR<Module>(IR);
          if (!M) {
            if (const auto *C = unwrapIR<LazyCallGraph::SCC>(IR))
              M = C->begin()->getFunction().getParent();
          }

          if (M) {
            if (DebugLogging)
              dbgs() << "Verifying module " << M->getName() << "\n";

            if (verifyModule(*M, &errs()))
              report_fatal_error(formatv("Broken module found after pass "
                                         "\"{0}\", compilation aborted!",
                                         P));
          }

          if (auto *MF = unwrapIR<MachineFunction>(IR)) {
            if (DebugLogging)
              dbgs() << "Verifying machine function " << MF->getName() << '\n';
            std::string Banner =
                formatv("Broken machine function found after pass "
                        "\"{0}\", compilation aborted!",
                        P);
            if (MAM) {
              Module &M = const_cast<Module &>(*MF->getFunction().getParent());
              auto &MFAM =
                  MAM->getResult<MachineFunctionAnalysisManagerModuleProxy>(M)
                      .getManager();
              MachineVerifierPass Verifier(Banner);
              Verifier.run(const_cast<MachineFunction &>(*MF), MFAM);
            } else {
              verifyMachineFunction(Banner, *MF);
            }
          }
        }
      });
}

InLineChangePrinter::~InLineChangePrinter() = default;

void InLineChangePrinter::generateIRRepresentation(Any IR,
                                                   StringRef PassID,
                                                   IRDataT<EmptyData> &D) {
  IRComparer<EmptyData>::analyzeIR(IR, D);
}

void InLineChangePrinter::handleAfter(StringRef PassID, std::string &Name,
                                      const IRDataT<EmptyData> &Before,
                                      const IRDataT<EmptyData> &After,
                                      Any IR) {
  SmallString<20> Banner =
      formatv("*** IR Dump After {0} on {1} ***\n", PassID, Name);
  Out << Banner;
  IRComparer<EmptyData>(Before, After)
      .compare(getModuleForComparison(IR),
               [&](bool InModule, unsigned Minor,
                   const FuncDataT<EmptyData> &Before,
                   const FuncDataT<EmptyData> &After) -> void {
                 handleFunctionCompare(Name, "", PassID, " on ", InModule,
                                       Minor, Before, After);
               });
  Out << "\n";
}

void InLineChangePrinter::handleFunctionCompare(
    StringRef Name, StringRef Prefix, StringRef PassID, StringRef Divider,
    bool InModule, unsigned Minor, const FuncDataT<EmptyData> &Before,
    const FuncDataT<EmptyData> &After) {
  // Print a banner when this is being shown in the context of a module
  if (InModule)
    Out << "\n*** IR for function " << Name << " ***\n";

  FuncDataT<EmptyData>::report(
      Before, After,
      [&](const BlockDataT<EmptyData> *B, const BlockDataT<EmptyData> *A) {
        StringRef BStr = B ? B->getBody() : "\n";
        StringRef AStr = A ? A->getBody() : "\n";
        const std::string Removed =
            UseColour ? "\033[31m-%l\033[0m\n" : "-%l\n";
        const std::string Added = UseColour ? "\033[32m+%l\033[0m\n" : "+%l\n";
        const std::string NoChange = " %l\n";
        Out << doSystemDiff(BStr, AStr, Removed, Added, NoChange);
      });
}

void InLineChangePrinter::registerCallbacks(PassInstrumentationCallbacks &PIC) {
  if (PrintChanged == ChangePrinter::DiffVerbose ||
      PrintChanged == ChangePrinter::DiffQuiet ||
      PrintChanged == ChangePrinter::ColourDiffVerbose ||
      PrintChanged == ChangePrinter::ColourDiffQuiet)
    TextChangeReporter<IRDataT<EmptyData>>::registerRequiredCallbacks(PIC);
}

TimeProfilingPassesHandler::TimeProfilingPassesHandler() {}

void TimeProfilingPassesHandler::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  if (!getTimeTraceProfilerInstance())
    return;
  PIC.registerBeforeNonSkippedPassCallback(
      [this](StringRef P, Any IR) { this->runBeforePass(P, IR); });
  PIC.registerAfterPassCallback(
      [this](StringRef P, Any IR, const PreservedAnalyses &) {
        this->runAfterPass();
      },
      true);
  PIC.registerAfterPassInvalidatedCallback(
      [this](StringRef P, const PreservedAnalyses &) { this->runAfterPass(); },
      true);
  PIC.registerBeforeAnalysisCallback(
      [this](StringRef P, Any IR) { this->runBeforePass(P, IR); });
  PIC.registerAfterAnalysisCallback(
      [this](StringRef P, Any IR) { this->runAfterPass(); }, true);
}

void TimeProfilingPassesHandler::runBeforePass(StringRef PassID, Any IR) {
  timeTraceProfilerBegin(PassID, getIRName(IR));
}

void TimeProfilingPassesHandler::runAfterPass() { timeTraceProfilerEnd(); }

namespace {

class DisplayNode;
class DotCfgDiffDisplayGraph;

// Base class for a node or edge in the dot-cfg-changes graph.
class DisplayElement {
public:
  // Is this in before, after, or both?
  StringRef getColour() const { return Colour; }

protected:
  DisplayElement(StringRef Colour) : Colour(Colour) {}
  const StringRef Colour;
};

// An edge representing a transition between basic blocks in the
// dot-cfg-changes graph.
class DisplayEdge : public DisplayElement {
public:
  DisplayEdge(std::string Value, DisplayNode &Node, StringRef Colour)
      : DisplayElement(Colour), Value(Value), Node(Node) {}
  // The value on which the transition is made.
  std::string getValue() const { return Value; }
  // The node (representing a basic block) reached by this transition.
  const DisplayNode &getDestinationNode() const { return Node; }

protected:
  std::string Value;
  const DisplayNode &Node;
};

// A node in the dot-cfg-changes graph which represents a basic block.
class DisplayNode : public DisplayElement {
public:
  // \p C is the content for the node, \p T indicates the colour for the
  // outline of the node
  DisplayNode(std::string Content, StringRef Colour)
      : DisplayElement(Colour), Content(Content) {}

  // Iterator to the child nodes.  Required by GraphWriter.
  using ChildIterator = std::unordered_set<DisplayNode *>::const_iterator;
  ChildIterator children_begin() const { return Children.cbegin(); }
  ChildIterator children_end() const { return Children.cend(); }

  // Iterator for the edges.  Required by GraphWriter.
  using EdgeIterator = std::vector<DisplayEdge *>::const_iterator;
  EdgeIterator edges_begin() const { return EdgePtrs.cbegin(); }
  EdgeIterator edges_end() const { return EdgePtrs.cend(); }

  // Create an edge to \p Node on value \p Value, with colour \p Colour.
  void createEdge(StringRef Value, DisplayNode &Node, StringRef Colour);

  // Return the content of this node.
  std::string getContent() const { return Content; }

  // Return the edge to node \p S.
  const DisplayEdge &getEdge(const DisplayNode &To) const {
    assert(EdgeMap.find(&To) != EdgeMap.end() && "Expected to find edge.");
    return *EdgeMap.find(&To)->second;
  }

  // Return the value for the transition to basic block \p S.
  // Required by GraphWriter.
  std::string getEdgeSourceLabel(const DisplayNode &Sink) const {
    return getEdge(Sink).getValue();
  }

  void createEdgeMap();

protected:
  const std::string Content;

  // Place to collect all of the edges.  Once they are all in the vector,
  // the vector will not reallocate so then we can use pointers to them,
  // which are required by the graph writing routines.
  std::vector<DisplayEdge> Edges;

  std::vector<DisplayEdge *> EdgePtrs;
  std::unordered_set<DisplayNode *> Children;
  std::unordered_map<const DisplayNode *, const DisplayEdge *> EdgeMap;

  // Safeguard adding of edges.
  bool AllEdgesCreated = false;
};

// Class representing a difference display (corresponds to a pdf file).
class DotCfgDiffDisplayGraph {
public:
  DotCfgDiffDisplayGraph(std::string Name) : GraphName(Name) {}

  // Generate the file into \p DotFile.
  void generateDotFile(StringRef DotFile);

  // Iterator to the nodes.  Required by GraphWriter.
  using NodeIterator = std::vector<DisplayNode *>::const_iterator;
  NodeIterator nodes_begin() const {
    assert(NodeGenerationComplete && "Unexpected children iterator creation");
    return NodePtrs.cbegin();
  }
  NodeIterator nodes_end() const {
    assert(NodeGenerationComplete && "Unexpected children iterator creation");
    return NodePtrs.cend();
  }

  // Record the index of the entry node.  At this point, we can build up
  // vectors of pointers that are required by the graph routines.
  void setEntryNode(unsigned N) {
    // At this point, there will be no new nodes.
    assert(!NodeGenerationComplete && "Unexpected node creation");
    NodeGenerationComplete = true;
    for (auto &N : Nodes)
      NodePtrs.emplace_back(&N);

    EntryNode = NodePtrs[N];
  }

  // Create a node.
  void createNode(std::string C, StringRef Colour) {
    assert(!NodeGenerationComplete && "Unexpected node creation");
    Nodes.emplace_back(C, Colour);
  }
  // Return the node at index \p N to avoid problems with vectors reallocating.
  DisplayNode &getNode(unsigned N) {
    assert(N < Nodes.size() && "Node is out of bounds");
    return Nodes[N];
  }
  unsigned size() const {
    assert(NodeGenerationComplete && "Unexpected children iterator creation");
    return Nodes.size();
  }

  // Return the name of the graph.  Required by GraphWriter.
  std::string getGraphName() const { return GraphName; }

  // Return the string representing the differences for basic block \p Node.
  // Required by GraphWriter.
  std::string getNodeLabel(const DisplayNode &Node) const {
    return Node.getContent();
  }

  // Return a string with colour information for Dot.  Required by GraphWriter.
  std::string getNodeAttributes(const DisplayNode &Node) const {
    return attribute(Node.getColour());
  }

  // Return a string with colour information for Dot.  Required by GraphWriter.
  std::string getEdgeColorAttr(const DisplayNode &From,
                               const DisplayNode &To) const {
    return attribute(From.getEdge(To).getColour());
  }

  // Get the starting basic block.  Required by GraphWriter.
  DisplayNode *getEntryNode() const {
    assert(NodeGenerationComplete && "Unexpected children iterator creation");
    return EntryNode;
  }

protected:
  // Return the string containing the colour to use as a Dot attribute.
  std::string attribute(StringRef Colour) const {
    return "color=" + Colour.str();
  }

  bool NodeGenerationComplete = false;
  const std::string GraphName;
  std::vector<DisplayNode> Nodes;
  std::vector<DisplayNode *> NodePtrs;
  DisplayNode *EntryNode = nullptr;
};

void DisplayNode::createEdge(StringRef Value, DisplayNode &Node,
                             StringRef Colour) {
  assert(!AllEdgesCreated && "Expected to be able to still create edges.");
  Edges.emplace_back(Value.str(), Node, Colour);
  Children.insert(&Node);
}

void DisplayNode::createEdgeMap() {
  // No more edges will be added so we can now use pointers to the edges
  // as the vector will not grow and reallocate.
  AllEdgesCreated = true;
  for (auto &E : Edges)
    EdgeMap.insert({&E.getDestinationNode(), &E});
}

class DotCfgDiffNode;
class DotCfgDiff;

// A class representing a basic block in the Dot difference graph.
class DotCfgDiffNode {
public:
  DotCfgDiffNode() = delete;

  // Create a node in Dot difference graph \p G representing the basic block
  // represented by \p BD with colour \p Colour (where it exists).
  DotCfgDiffNode(DotCfgDiff &G, unsigned N, const BlockDataT<DCData> &BD,
                 StringRef Colour)
      : Graph(G), N(N), Data{&BD, nullptr}, Colour(Colour) {}
  DotCfgDiffNode(const DotCfgDiffNode &DN)
      : Graph(DN.Graph), N(DN.N), Data{DN.Data[0], DN.Data[1]},
        Colour(DN.Colour), EdgesMap(DN.EdgesMap), Children(DN.Children),
        Edges(DN.Edges) {}

  unsigned getIndex() const { return N; }

  // The label of the basic block
  StringRef getLabel() const {
    assert(Data[0] && "Expected Data[0] to be set.");
    return Data[0]->getLabel();
  }
  // Return the colour for this block
  StringRef getColour() const { return Colour; }
  // Change this basic block from being only in before to being common.
  // Save the pointer to \p Other.
  void setCommon(const BlockDataT<DCData> &Other) {
    assert(!Data[1] && "Expected only one block datum");
    Data[1] = &Other;
    Colour = CommonColour;
  }
  // Add an edge to \p E of colour {\p Value, \p Colour}.
  void addEdge(unsigned E, StringRef Value, StringRef Colour) {
    // This is a new edge or it is an edge being made common.
    assert((EdgesMap.count(E) == 0 || Colour == CommonColour) &&
           "Unexpected edge count and color.");
    EdgesMap[E] = {Value.str(), Colour};
  }
  // Record the children and create edges.
  void finalize(DotCfgDiff &G);

  // Return the colour of the edge to node \p S.
  StringRef getEdgeColour(const unsigned S) const {
    assert(EdgesMap.count(S) == 1 && "Expected to find edge.");
    return EdgesMap.at(S).second;
  }

  // Return the string representing the basic block.
  std::string getBodyContent() const;

  void createDisplayEdges(DotCfgDiffDisplayGraph &Graph, unsigned DisplayNode,
                          std::map<const unsigned, unsigned> &NodeMap) const;

protected:
  DotCfgDiff &Graph;
  const unsigned N;
  const BlockDataT<DCData> *Data[2];
  StringRef Colour;
  std::map<const unsigned, std::pair<std::string, StringRef>> EdgesMap;
  std::vector<unsigned> Children;
  std::vector<unsigned> Edges;
};

// Class representing the difference graph between two functions.
class DotCfgDiff {
public:
  // \p Title is the title given to the graph.  \p EntryNodeName is the
  // entry node for the function.  \p Before and \p After are the before
  // after versions of the function, respectively.  \p Dir is the directory
  // in which to store the results.
  DotCfgDiff(StringRef Title, const FuncDataT<DCData> &Before,
             const FuncDataT<DCData> &After);

  DotCfgDiff(const DotCfgDiff &) = delete;
  DotCfgDiff &operator=(const DotCfgDiff &) = delete;

  DotCfgDiffDisplayGraph createDisplayGraph(StringRef Title,
                                            StringRef EntryNodeName);

  // Return a string consisting of the labels for the \p Source and \p Sink.
  // The combination allows distinguishing changing transitions on the
  // same value (ie, a transition went to X before and goes to Y after).
  // Required by GraphWriter.
  StringRef getEdgeSourceLabel(const unsigned &Source,
                               const unsigned &Sink) const {
    std::string S =
        getNode(Source).getLabel().str() + " " + getNode(Sink).getLabel().str();
    assert(EdgeLabels.count(S) == 1 && "Expected to find edge label.");
    return EdgeLabels.find(S)->getValue();
  }

  // Return the number of basic blocks (nodes).  Required by GraphWriter.
  unsigned size() const { return Nodes.size(); }

  const DotCfgDiffNode &getNode(unsigned N) const {
    assert(N < Nodes.size() && "Unexpected index for node reference");
    return Nodes[N];
  }

protected:
  // Return the string surrounded by HTML to make it the appropriate colour.
  std::string colourize(std::string S, StringRef Colour) const;

  void createNode(StringRef Label, const BlockDataT<DCData> &BD, StringRef C) {
    unsigned Pos = Nodes.size();
    Nodes.emplace_back(*this, Pos, BD, C);
    NodePosition.insert({Label, Pos});
  }

  // TODO Nodes should probably be a StringMap<DotCfgDiffNode> after the
  // display graph is separated out, which would remove the need for
  // NodePosition.
  std::vector<DotCfgDiffNode> Nodes;
  StringMap<unsigned> NodePosition;
  const std::string GraphName;

  StringMap<std::string> EdgeLabels;
};

std::string DotCfgDiffNode::getBodyContent() const {
  if (Colour == CommonColour) {
    assert(Data[1] && "Expected Data[1] to be set.");

    StringRef SR[2];
    for (unsigned I = 0; I < 2; ++I) {
      SR[I] = Data[I]->getBody();
      // drop initial '\n' if present
      SR[I].consume_front("\n");
      // drop predecessors as they can be big and are redundant
      SR[I] = SR[I].drop_until([](char C) { return C == '\n'; }).drop_front();
    }

    SmallString<80> OldLineFormat = formatv(
        "<FONT COLOR=\"{0}\">%l</FONT><BR align=\"left\"/>", BeforeColour);
    SmallString<80> NewLineFormat = formatv(
        "<FONT COLOR=\"{0}\">%l</FONT><BR align=\"left\"/>", AfterColour);
    SmallString<80> UnchangedLineFormat = formatv(
        "<FONT COLOR=\"{0}\">%l</FONT><BR align=\"left\"/>", CommonColour);
    std::string Diff = Data[0]->getLabel().str();
    Diff += ":\n<BR align=\"left\"/>" +
            doSystemDiff(makeHTMLReady(SR[0]), makeHTMLReady(SR[1]),
                         OldLineFormat, NewLineFormat, UnchangedLineFormat);

    // Diff adds in some empty colour changes which are not valid HTML
    // so remove them.  Colours are all lowercase alpha characters (as
    // listed in https://graphviz.org/pdf/dotguide.pdf).
    Regex R("<FONT COLOR=\"\\w+\"></FONT>");
    while (true) {
      std::string Error;
      std::string S = R.sub("", Diff, &Error);
      if (Error != "")
        return Error;
      if (S == Diff)
        return Diff;
      Diff = S;
    }
    llvm_unreachable("Should not get here");
  }

  // Put node out in the appropriate colour.
  assert(!Data[1] && "Data[1] is set unexpectedly.");
  std::string Body = makeHTMLReady(Data[0]->getBody());
  const StringRef BS = Body;
  StringRef BS1 = BS;
  // Drop leading newline, if present.
  if (BS.front() == '\n')
    BS1 = BS1.drop_front(1);
  // Get label.
  StringRef Label = BS1.take_until([](char C) { return C == ':'; });
  // drop predecessors as they can be big and are redundant
  BS1 = BS1.drop_until([](char C) { return C == '\n'; }).drop_front();

  std::string S = "<FONT COLOR=\"" + Colour.str() + "\">" + Label.str() + ":";

  // align each line to the left.
  while (BS1.size()) {
    S.append("<BR align=\"left\"/>");
    StringRef Line = BS1.take_until([](char C) { return C == '\n'; });
    S.append(Line.str());
    BS1 = BS1.drop_front(Line.size() + 1);
  }
  S.append("<BR align=\"left\"/></FONT>");
  return S;
}

std::string DotCfgDiff::colourize(std::string S, StringRef Colour) const {
  if (S.length() == 0)
    return S;
  return "<FONT COLOR=\"" + Colour.str() + "\">" + S + "</FONT>";
}

DotCfgDiff::DotCfgDiff(StringRef Title, const FuncDataT<DCData> &Before,
                       const FuncDataT<DCData> &After)
    : GraphName(Title.str()) {
  StringMap<StringRef> EdgesMap;

  // Handle each basic block in the before IR.
  for (auto &B : Before.getData()) {
    StringRef Label = B.getKey();
    const BlockDataT<DCData> &BD = B.getValue();
    createNode(Label, BD, BeforeColour);

    // Create transitions with names made up of the from block label, the value
    // on which the transition is made and the to block label.
    for (StringMap<std::string>::const_iterator Sink = BD.getData().begin(),
                                                E = BD.getData().end();
         Sink != E; ++Sink) {
      std::string Key = (Label + " " + Sink->getKey().str()).str() + " " +
                        BD.getData().getSuccessorLabel(Sink->getKey()).str();
      EdgesMap.insert({Key, BeforeColour});
    }
  }

  // Handle each basic block in the after IR
  for (auto &A : After.getData()) {
    StringRef Label = A.getKey();
    const BlockDataT<DCData> &BD = A.getValue();
    unsigned C = NodePosition.count(Label);
    if (C == 0)
      // This only exists in the after IR.  Create the node.
      createNode(Label, BD, AfterColour);
    else {
      assert(C == 1 && "Unexpected multiple nodes.");
      Nodes[NodePosition[Label]].setCommon(BD);
    }
    // Add in the edges between the nodes (as common or only in after).
    for (StringMap<std::string>::const_iterator Sink = BD.getData().begin(),
                                                E = BD.getData().end();
         Sink != E; ++Sink) {
      std::string Key = (Label + " " + Sink->getKey().str()).str() + " " +
                        BD.getData().getSuccessorLabel(Sink->getKey()).str();
      unsigned C = EdgesMap.count(Key);
      if (C == 0)
        EdgesMap.insert({Key, AfterColour});
      else {
        EdgesMap[Key] = CommonColour;
      }
    }
  }

  // Now go through the map of edges and add them to the node.
  for (auto &E : EdgesMap) {
    // Extract the source, sink and value from the edge key.
    StringRef S = E.getKey();
    auto SP1 = S.rsplit(' ');
    auto &SourceSink = SP1.first;
    auto SP2 = SourceSink.split(' ');
    StringRef Source = SP2.first;
    StringRef Sink = SP2.second;
    StringRef Value = SP1.second;

    assert(NodePosition.count(Source) == 1 && "Expected to find node.");
    DotCfgDiffNode &SourceNode = Nodes[NodePosition[Source]];
    assert(NodePosition.count(Sink) == 1 && "Expected to find node.");
    unsigned SinkNode = NodePosition[Sink];
    StringRef Colour = E.second;

    // Look for an edge from Source to Sink
    if (EdgeLabels.count(SourceSink) == 0)
      EdgeLabels.insert({SourceSink, colourize(Value.str(), Colour)});
    else {
      StringRef V = EdgeLabels.find(SourceSink)->getValue();
      std::string NV = colourize(V.str() + " " + Value.str(), Colour);
      Colour = CommonColour;
      EdgeLabels[SourceSink] = NV;
    }
    SourceNode.addEdge(SinkNode, Value, Colour);
  }
  for (auto &I : Nodes)
    I.finalize(*this);
}

DotCfgDiffDisplayGraph DotCfgDiff::createDisplayGraph(StringRef Title,
                                                      StringRef EntryNodeName) {
  assert(NodePosition.count(EntryNodeName) == 1 &&
         "Expected to find entry block in map.");
  unsigned Entry = NodePosition[EntryNodeName];
  assert(Entry < Nodes.size() && "Expected to find entry node");
  DotCfgDiffDisplayGraph G(Title.str());

  std::map<const unsigned, unsigned> NodeMap;

  int EntryIndex = -1;
  unsigned Index = 0;
  for (auto &I : Nodes) {
    if (I.getIndex() == Entry)
      EntryIndex = Index;
    G.createNode(I.getBodyContent(), I.getColour());
    NodeMap.insert({I.getIndex(), Index++});
  }
  assert(EntryIndex >= 0 && "Expected entry node index to be set.");
  G.setEntryNode(EntryIndex);

  for (auto &I : NodeMap) {
    unsigned SourceNode = I.first;
    unsigned DisplayNode = I.second;
    getNode(SourceNode).createDisplayEdges(G, DisplayNode, NodeMap);
  }
  return G;
}

void DotCfgDiffNode::createDisplayEdges(
    DotCfgDiffDisplayGraph &DisplayGraph, unsigned DisplayNodeIndex,
    std::map<const unsigned, unsigned> &NodeMap) const {

  DisplayNode &SourceDisplayNode = DisplayGraph.getNode(DisplayNodeIndex);

  for (auto I : Edges) {
    unsigned SinkNodeIndex = I;
    StringRef Colour = getEdgeColour(SinkNodeIndex);
    const DotCfgDiffNode *SinkNode = &Graph.getNode(SinkNodeIndex);

    StringRef Label = Graph.getEdgeSourceLabel(getIndex(), SinkNodeIndex);
    DisplayNode &SinkDisplayNode = DisplayGraph.getNode(SinkNode->getIndex());
    SourceDisplayNode.createEdge(Label, SinkDisplayNode, Colour);
  }
  SourceDisplayNode.createEdgeMap();
}

void DotCfgDiffNode::finalize(DotCfgDiff &G) {
  for (auto E : EdgesMap) {
    Children.emplace_back(E.first);
    Edges.emplace_back(E.first);
  }
}

} // namespace

namespace llvm {

template <> struct GraphTraits<DotCfgDiffDisplayGraph *> {
  using NodeRef = const DisplayNode *;
  using ChildIteratorType = DisplayNode::ChildIterator;
  using nodes_iterator = DotCfgDiffDisplayGraph::NodeIterator;
  using EdgeRef = const DisplayEdge *;
  using ChildEdgeIterator = DisplayNode::EdgeIterator;

  static NodeRef getEntryNode(const DotCfgDiffDisplayGraph *G) {
    return G->getEntryNode();
  }
  static ChildIteratorType child_begin(NodeRef N) {
    return N->children_begin();
  }
  static ChildIteratorType child_end(NodeRef N) { return N->children_end(); }
  static nodes_iterator nodes_begin(const DotCfgDiffDisplayGraph *G) {
    return G->nodes_begin();
  }
  static nodes_iterator nodes_end(const DotCfgDiffDisplayGraph *G) {
    return G->nodes_end();
  }
  static ChildEdgeIterator child_edge_begin(NodeRef N) {
    return N->edges_begin();
  }
  static ChildEdgeIterator child_edge_end(NodeRef N) { return N->edges_end(); }
  static NodeRef edge_dest(EdgeRef E) { return &E->getDestinationNode(); }
  static unsigned size(const DotCfgDiffDisplayGraph *G) { return G->size(); }
};

template <>
struct DOTGraphTraits<DotCfgDiffDisplayGraph *> : public DefaultDOTGraphTraits {
  explicit DOTGraphTraits(bool Simple = false)
      : DefaultDOTGraphTraits(Simple) {}

  static bool renderNodesUsingHTML() { return true; }
  static std::string getGraphName(const DotCfgDiffDisplayGraph *DiffData) {
    return DiffData->getGraphName();
  }
  static std::string
  getGraphProperties(const DotCfgDiffDisplayGraph *DiffData) {
    return "\tsize=\"190, 190\";\n";
  }
  static std::string getNodeLabel(const DisplayNode *Node,
                                  const DotCfgDiffDisplayGraph *DiffData) {
    return DiffData->getNodeLabel(*Node);
  }
  static std::string getNodeAttributes(const DisplayNode *Node,
                                       const DotCfgDiffDisplayGraph *DiffData) {
    return DiffData->getNodeAttributes(*Node);
  }
  static std::string getEdgeSourceLabel(const DisplayNode *From,
                                        DisplayNode::ChildIterator &To) {
    return From->getEdgeSourceLabel(**To);
  }
  static std::string getEdgeAttributes(const DisplayNode *From,
                                       DisplayNode::ChildIterator &To,
                                       const DotCfgDiffDisplayGraph *DiffData) {
    return DiffData->getEdgeColorAttr(*From, **To);
  }
};

} // namespace llvm

namespace {

void DotCfgDiffDisplayGraph::generateDotFile(StringRef DotFile) {
  std::error_code EC;
  raw_fd_ostream OutStream(DotFile, EC);
  if (EC) {
    errs() << "Error: " << EC.message() << "\n";
    return;
  }
  WriteGraph(OutStream, this, false);
  OutStream.flush();
  OutStream.close();
}

} // namespace

namespace llvm {

DCData::DCData(const BasicBlock &B) {
  // Build up transition labels.
  const Instruction *Term = B.getTerminator();
  if (const BranchInst *Br = dyn_cast<const BranchInst>(Term))
    if (Br->isUnconditional())
      addSuccessorLabel(Br->getSuccessor(0)->getName().str(), "");
    else {
      addSuccessorLabel(Br->getSuccessor(0)->getName().str(), "true");
      addSuccessorLabel(Br->getSuccessor(1)->getName().str(), "false");
    }
  else if (const SwitchInst *Sw = dyn_cast<const SwitchInst>(Term)) {
    addSuccessorLabel(Sw->case_default()->getCaseSuccessor()->getName().str(),
                      "default");
    for (auto &C : Sw->cases()) {
      assert(C.getCaseValue() && "Expected to find case value.");
      SmallString<20> Value = formatv("{0}", C.getCaseValue()->getSExtValue());
      addSuccessorLabel(C.getCaseSuccessor()->getName().str(), Value);
    }
  } else
    for (const BasicBlock *Succ : successors(&B))
      addSuccessorLabel(Succ->getName().str(), "");
}

DCData::DCData(const MachineBasicBlock &B) {
  for (const MachineBasicBlock *Succ : successors(&B))
    addSuccessorLabel(Succ->getName().str(), "");
}

DotCfgChangeReporter::DotCfgChangeReporter(bool Verbose)
    : ChangeReporter<IRDataT<DCData>>(Verbose) {}

void DotCfgChangeReporter::handleFunctionCompare(
    StringRef Name, StringRef Prefix, StringRef PassID, StringRef Divider,
    bool InModule, unsigned Minor, const FuncDataT<DCData> &Before,
    const FuncDataT<DCData> &After) {
  assert(HTML && "Expected outstream to be set");
  SmallString<8> Extender;
  SmallString<8> Number;
  // Handle numbering and file names.
  if (InModule) {
    Extender = formatv("{0}_{1}", N, Minor);
    Number = formatv("{0}.{1}", N, Minor);
  } else {
    Extender = formatv("{0}", N);
    Number = formatv("{0}", N);
  }
  // Create a temporary file name for the dot file.
  SmallVector<char, 128> SV;
  sys::fs::createUniquePath("cfgdot-%%%%%%.dot", SV, true);
  std::string DotFile = Twine(SV).str();

  SmallString<20> PDFFileName = formatv("diff_{0}.pdf", Extender);
  SmallString<200> Text;

  Text = formatv("{0}.{1}{2}{3}{4}", Number, Prefix, makeHTMLReady(PassID),
                 Divider, Name);

  DotCfgDiff Diff(Text, Before, After);
  std::string EntryBlockName = After.getEntryBlockName();
  // Use the before entry block if the after entry block was removed.
  if (EntryBlockName == "")
    EntryBlockName = Before.getEntryBlockName();
  assert(EntryBlockName != "" && "Expected to find entry block");

  DotCfgDiffDisplayGraph DG = Diff.createDisplayGraph(Text, EntryBlockName);
  DG.generateDotFile(DotFile);

  *HTML << genHTML(Text, DotFile, PDFFileName);
  std::error_code EC = sys::fs::remove(DotFile);
  if (EC)
    errs() << "Error: " << EC.message() << "\n";
}

std::string DotCfgChangeReporter::genHTML(StringRef Text, StringRef DotFile,
                                          StringRef PDFFileName) {
  SmallString<20> PDFFile = formatv("{0}/{1}", DotCfgDir, PDFFileName);
  // Create the PDF file.
  static ErrorOr<std::string> DotExe = sys::findProgramByName(DotBinary);
  if (!DotExe)
    return "Unable to find dot executable.";

  StringRef Args[] = {DotBinary, "-Tpdf", "-o", PDFFile, DotFile};
  int Result = sys::ExecuteAndWait(*DotExe, Args, std::nullopt);
  if (Result < 0)
    return "Error executing system dot.";

  // Create the HTML tag refering to the PDF file.
  SmallString<200> S = formatv(
      "  <a href=\"{0}\" target=\"_blank\">{1}</a><br/>\n", PDFFileName, Text);
  return S.c_str();
}

void DotCfgChangeReporter::handleInitialIR(Any IR) {
  assert(HTML && "Expected outstream to be set");
  *HTML << "<button type=\"button\" class=\"collapsible\">0. "
        << "Initial IR (by function)</button>\n"
        << "<div class=\"content\">\n"
        << "  <p>\n";
  // Create representation of IR
  IRDataT<DCData> Data;
  IRComparer<DCData>::analyzeIR(IR, Data);
  // Now compare it against itself, which will have everything the
  // same and will generate the files.
  IRComparer<DCData>(Data, Data)
      .compare(getModuleForComparison(IR),
               [&](bool InModule, unsigned Minor,
                   const FuncDataT<DCData> &Before,
                   const FuncDataT<DCData> &After) -> void {
                 handleFunctionCompare("", " ", "Initial IR", "", InModule,
                                       Minor, Before, After);
               });
  *HTML << "  </p>\n"
        << "</div><br/>\n";
  ++N;
}

void DotCfgChangeReporter::generateIRRepresentation(Any IR, StringRef PassID,
                                                    IRDataT<DCData> &Data) {
  IRComparer<DCData>::analyzeIR(IR, Data);
}

void DotCfgChangeReporter::omitAfter(StringRef PassID, std::string &Name) {
  assert(HTML && "Expected outstream to be set");
  SmallString<20> Banner =
      formatv("  <a>{0}. Pass {1} on {2} omitted because no change</a><br/>\n",
              N, makeHTMLReady(PassID), Name);
  *HTML << Banner;
  ++N;
}

void DotCfgChangeReporter::handleAfter(StringRef PassID, std::string &Name,
                                       const IRDataT<DCData> &Before,
                                       const IRDataT<DCData> &After, Any IR) {
  assert(HTML && "Expected outstream to be set");
  IRComparer<DCData>(Before, After)
      .compare(getModuleForComparison(IR),
               [&](bool InModule, unsigned Minor,
                   const FuncDataT<DCData> &Before,
                   const FuncDataT<DCData> &After) -> void {
                 handleFunctionCompare(Name, " Pass ", PassID, " on ", InModule,
                                       Minor, Before, After);
               });
  *HTML << "    </p></div>\n";
  ++N;
}

void DotCfgChangeReporter::handleInvalidated(StringRef PassID) {
  assert(HTML && "Expected outstream to be set");
  SmallString<20> Banner =
      formatv("  <a>{0}. {1} invalidated</a><br/>\n", N, makeHTMLReady(PassID));
  *HTML << Banner;
  ++N;
}

void DotCfgChangeReporter::handleFiltered(StringRef PassID, std::string &Name) {
  assert(HTML && "Expected outstream to be set");
  SmallString<20> Banner =
      formatv("  <a>{0}. Pass {1} on {2} filtered out</a><br/>\n", N,
              makeHTMLReady(PassID), Name);
  *HTML << Banner;
  ++N;
}

void DotCfgChangeReporter::handleIgnored(StringRef PassID, std::string &Name) {
  assert(HTML && "Expected outstream to be set");
  SmallString<20> Banner = formatv("  <a>{0}. {1} on {2} ignored</a><br/>\n", N,
                                   makeHTMLReady(PassID), Name);
  *HTML << Banner;
  ++N;
}

bool DotCfgChangeReporter::initializeHTML() {
  std::error_code EC;
  HTML = std::make_unique<raw_fd_ostream>(DotCfgDir + "/passes.html", EC);
  if (EC) {
    HTML = nullptr;
    return false;
  }

  *HTML << "<!doctype html>"
        << "<html>"
        << "<head>"
        << "<style>.collapsible { "
        << "background-color: #777;"
        << " color: white;"
        << " cursor: pointer;"
        << " padding: 18px;"
        << " width: 100%;"
        << " border: none;"
        << " text-align: left;"
        << " outline: none;"
        << " font-size: 15px;"
        << "} .active, .collapsible:hover {"
        << " background-color: #555;"
        << "} .content {"
        << " padding: 0 18px;"
        << " display: none;"
        << " overflow: hidden;"
        << " background-color: #f1f1f1;"
        << "}"
        << "</style>"
        << "<title>passes.html</title>"
        << "</head>\n"
        << "<body>";
  return true;
}

DotCfgChangeReporter::~DotCfgChangeReporter() {
  if (!HTML)
    return;
  *HTML
      << "<script>var coll = document.getElementsByClassName(\"collapsible\");"
      << "var i;"
      << "for (i = 0; i < coll.length; i++) {"
      << "coll[i].addEventListener(\"click\", function() {"
      << " this.classList.toggle(\"active\");"
      << " var content = this.nextElementSibling;"
      << " if (content.style.display === \"block\"){"
      << " content.style.display = \"none\";"
      << " }"
      << " else {"
      << " content.style.display= \"block\";"
      << " }"
      << " });"
      << " }"
      << "</script>"
      << "</body>"
      << "</html>\n";
  HTML->flush();
  HTML->close();
}

void DotCfgChangeReporter::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  if (PrintChanged == ChangePrinter::DotCfgVerbose ||
       PrintChanged == ChangePrinter::DotCfgQuiet) {
    SmallString<128> OutputDir;
    sys::fs::expand_tilde(DotCfgDir, OutputDir);
    sys::fs::make_absolute(OutputDir);
    assert(!OutputDir.empty() && "expected output dir to be non-empty");
    DotCfgDir = OutputDir.c_str();
    if (initializeHTML()) {
      ChangeReporter<IRDataT<DCData>>::registerRequiredCallbacks(PIC);
      return;
    }
    dbgs() << "Unable to open output stream for -cfg-dot-changed\n";
  }
}

StandardInstrumentations::StandardInstrumentations(
    LLVMContext &Context, bool DebugLogging, bool VerifyEach,
    PrintPassOptions PrintPassOpts)
    : PrintPass(DebugLogging, PrintPassOpts),
      OptNone(DebugLogging),
      OptPassGate(Context),
      PrintChangedIR(PrintChanged == ChangePrinter::Verbose),
      PrintChangedDiff(PrintChanged == ChangePrinter::DiffVerbose ||
                           PrintChanged == ChangePrinter::ColourDiffVerbose,
                       PrintChanged == ChangePrinter::ColourDiffVerbose ||
                           PrintChanged == ChangePrinter::ColourDiffQuiet),
      WebsiteChangeReporter(PrintChanged == ChangePrinter::DotCfgVerbose),
      Verify(DebugLogging), VerifyEach(VerifyEach) {}

PrintCrashIRInstrumentation *PrintCrashIRInstrumentation::CrashReporter =
    nullptr;

void PrintCrashIRInstrumentation::reportCrashIR() {
  if (!PrintOnCrashPath.empty()) {
    std::error_code EC;
    raw_fd_ostream Out(PrintOnCrashPath, EC);
    if (EC)
      report_fatal_error(errorCodeToError(EC));
    Out << SavedIR;
  } else {
    dbgs() << SavedIR;
  }
}

void PrintCrashIRInstrumentation::SignalHandler(void *) {
  // Called by signal handlers so do not lock here
  // Is the PrintCrashIRInstrumentation still alive?
  if (!CrashReporter)
    return;

  assert((PrintOnCrash || !PrintOnCrashPath.empty()) &&
         "Did not expect to get here without option set.");
  CrashReporter->reportCrashIR();
}

PrintCrashIRInstrumentation::~PrintCrashIRInstrumentation() {
  if (!CrashReporter)
    return;

  assert((PrintOnCrash || !PrintOnCrashPath.empty()) &&
         "Did not expect to get here without option set.");
  CrashReporter = nullptr;
}

void PrintCrashIRInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  if ((!PrintOnCrash && PrintOnCrashPath.empty()) || CrashReporter)
    return;

  sys::AddSignalHandler(SignalHandler, nullptr);
  CrashReporter = this;

  PIC.registerBeforeNonSkippedPassCallback(
      [&PIC, this](StringRef PassID, Any IR) {
        SavedIR.clear();
        raw_string_ostream OS(SavedIR);
        OS << formatv("*** Dump of {0}IR Before Last Pass {1}",
                      llvm::forcePrintModuleIR() ? "Module " : "", PassID);
        if (!isInteresting(IR, PassID, PIC.getPassNameForClassName(PassID))) {
          OS << " Filtered Out ***\n";
          return;
        }
        OS << " Started ***\n";
        unwrapAndPrint(OS, IR);
      });
}

void StandardInstrumentations::registerCallbacks(
    PassInstrumentationCallbacks &PIC, ModuleAnalysisManager *MAM) {
  PrintIR.registerCallbacks(PIC);
  PrintPass.registerCallbacks(PIC);
  TimePasses.registerCallbacks(PIC);
  OptNone.registerCallbacks(PIC);
  OptPassGate.registerCallbacks(PIC);
  PrintChangedIR.registerCallbacks(PIC);
  PseudoProbeVerification.registerCallbacks(PIC);
  if (VerifyEach)
    Verify.registerCallbacks(PIC, MAM);
  PrintChangedDiff.registerCallbacks(PIC);
  WebsiteChangeReporter.registerCallbacks(PIC);
  ChangeTester.registerCallbacks(PIC);
  PrintCrashIR.registerCallbacks(PIC);
  if (MAM)
    PreservedCFGChecker.registerCallbacks(PIC, *MAM);

  // TimeProfiling records the pass running time cost.
  // Its 'BeforePassCallback' can be appended at the tail of all the
  // BeforeCallbacks by calling `registerCallbacks` in the end.
  // Its 'AfterPassCallback' is put at the front of all the
  // AfterCallbacks by its `registerCallbacks`. This is necessary
  // to ensure that other callbacks are not included in the timings.
  TimeProfilingPasses.registerCallbacks(PIC);
}

template class ChangeReporter<std::string>;
template class TextChangeReporter<std::string>;

template class BlockDataT<EmptyData>;
template class FuncDataT<EmptyData>;
template class IRDataT<EmptyData>;
template class ChangeReporter<IRDataT<EmptyData>>;
template class TextChangeReporter<IRDataT<EmptyData>>;
template class IRComparer<EmptyData>;

} // namespace llvm
