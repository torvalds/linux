//===- StandardInstrumentations.h ------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header defines a class that provides bookkeeping for all standard
/// (i.e in-tree) pass instrumentations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_STANDARDINSTRUMENTATIONS_H
#define LLVM_PASSES_STANDARDINSTRUMENTATIONS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Transforms/IPO/SampleProfileProbe.h"

#include <string>
#include <utility>

namespace llvm {

class Module;
class Function;
class MachineFunction;
class PassInstrumentationCallbacks;

/// Instrumentation to print IR before/after passes.
///
/// Needs state to be able to print module after pass that invalidates IR unit
/// (typically Loop or SCC).
class PrintIRInstrumentation {
public:
  ~PrintIRInstrumentation();

  void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  struct PassRunDescriptor {
    const Module *M;
    const std::string DumpIRFilename;
    const std::string IRName;
    const StringRef PassID;

    PassRunDescriptor(const Module *M, std::string DumpIRFilename,
                      std::string IRName, const StringRef PassID)
        : M{M}, DumpIRFilename{DumpIRFilename}, IRName{IRName}, PassID(PassID) {
    }
  };

  void printBeforePass(StringRef PassID, Any IR);
  void printAfterPass(StringRef PassID, Any IR);
  void printAfterPassInvalidated(StringRef PassID);

  bool shouldPrintBeforePass(StringRef PassID);
  bool shouldPrintAfterPass(StringRef PassID);
  bool shouldPrintBeforeCurrentPassNumber();
  bool shouldPrintAfterCurrentPassNumber();
  bool shouldPrintPassNumbers();
  bool shouldPrintBeforeSomePassNumber();
  bool shouldPrintAfterSomePassNumber();

  void pushPassRunDescriptor(StringRef PassID, Any IR,
                             std::string &DumpIRFilename);
  PassRunDescriptor popPassRunDescriptor(StringRef PassID);
  std::string fetchDumpFilename(StringRef PassId, Any IR);

  PassInstrumentationCallbacks *PIC;
  /// Stack of Pass Run descriptions, enough to print the IR unit after a given
  /// pass.
  SmallVector<PassRunDescriptor, 2> PassRunDescriptorStack;

  /// Used for print-at-pass-number
  unsigned CurrentPassNumber = 0;
};

class OptNoneInstrumentation {
public:
  OptNoneInstrumentation(bool DebugLogging) : DebugLogging(DebugLogging) {}
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  bool DebugLogging;
  bool shouldRun(StringRef PassID, Any IR);
};

class OptPassGateInstrumentation {
  LLVMContext &Context;
  bool HasWrittenIR = false;
public:
  OptPassGateInstrumentation(LLVMContext &Context) : Context(Context) {}
  bool shouldRun(StringRef PassName, Any IR);
  void registerCallbacks(PassInstrumentationCallbacks &PIC);
};

struct PrintPassOptions {
  /// Print adaptors and pass managers.
  bool Verbose = false;
  /// Don't print information for analyses.
  bool SkipAnalyses = false;
  /// Indent based on hierarchy.
  bool Indent = false;
};

// Debug logging for transformation and analysis passes.
class PrintPassInstrumentation {
  raw_ostream &print();

public:
  PrintPassInstrumentation(bool Enabled, PrintPassOptions Opts)
      : Enabled(Enabled), Opts(Opts) {}
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  bool Enabled;
  PrintPassOptions Opts;
  int Indent = 0;
};

class PreservedCFGCheckerInstrumentation {
public:
  // Keeps sticky poisoned flag for the given basic block once it has been
  // deleted or RAUWed.
  struct BBGuard final : public CallbackVH {
    BBGuard(const BasicBlock *BB) : CallbackVH(BB) {}
    void deleted() override { CallbackVH::deleted(); }
    void allUsesReplacedWith(Value *) override { CallbackVH::deleted(); }
    bool isPoisoned() const { return !getValPtr(); }
  };

  // CFG is a map BB -> {(Succ, Multiplicity)}, where BB is a non-leaf basic
  // block, {(Succ, Multiplicity)} set of all pairs of the block's successors
  // and the multiplicity of the edge (BB->Succ). As the mapped sets are
  // unordered the order of successors is not tracked by the CFG. In other words
  // this allows basic block successors to be swapped by a pass without
  // reporting a CFG change. CFG can be guarded by basic block tracking pointers
  // in the Graph (BBGuard). That is if any of the block is deleted or RAUWed
  // then the CFG is treated poisoned and no block pointer of the Graph is used.
  struct CFG {
    std::optional<DenseMap<intptr_t, BBGuard>> BBGuards;
    DenseMap<const BasicBlock *, DenseMap<const BasicBlock *, unsigned>> Graph;

    CFG(const Function *F, bool TrackBBLifetime);

    bool operator==(const CFG &G) const {
      return !isPoisoned() && !G.isPoisoned() && Graph == G.Graph;
    }

    bool isPoisoned() const {
      return BBGuards && llvm::any_of(*BBGuards, [](const auto &BB) {
               return BB.second.isPoisoned();
             });
    }

    static void printDiff(raw_ostream &out, const CFG &Before,
                          const CFG &After);
    bool invalidate(Function &F, const PreservedAnalyses &PA,
                    FunctionAnalysisManager::Invalidator &);
  };

#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
  SmallVector<StringRef, 8> PassStack;
#endif

  void registerCallbacks(PassInstrumentationCallbacks &PIC,
                         ModuleAnalysisManager &MAM);
};

// Base class for classes that report changes to the IR.
// It presents an interface for such classes and provides calls
// on various events as the new pass manager transforms the IR.
// It also provides filtering of information based on hidden options
// specifying which functions are interesting.
// Calls are made for the following events/queries:
// 1.  The initial IR processed.
// 2.  To get the representation of the IR (of type \p T).
// 3.  When a pass does not change the IR.
// 4.  When a pass changes the IR (given both before and after representations
//         of type \p T).
// 5.  When an IR is invalidated.
// 6.  When a pass is run on an IR that is not interesting (based on options).
// 7.  When a pass is ignored (pass manager or adapter pass).
// 8.  To compare two IR representations (of type \p T).
template <typename IRUnitT> class ChangeReporter {
protected:
  ChangeReporter(bool RunInVerboseMode) : VerboseMode(RunInVerboseMode) {}

public:
  virtual ~ChangeReporter();

  // Determine if this pass/IR is interesting and if so, save the IR
  // otherwise it is left on the stack without data.
  void saveIRBeforePass(Any IR, StringRef PassID, StringRef PassName);
  // Compare the IR from before the pass after the pass.
  void handleIRAfterPass(Any IR, StringRef PassID, StringRef PassName);
  // Handle the situation where a pass is invalidated.
  void handleInvalidatedPass(StringRef PassID);

protected:
  // Register required callbacks.
  void registerRequiredCallbacks(PassInstrumentationCallbacks &PIC);

  // Called on the first IR processed.
  virtual void handleInitialIR(Any IR) = 0;
  // Called before and after a pass to get the representation of the IR.
  virtual void generateIRRepresentation(Any IR, StringRef PassID,
                                        IRUnitT &Output) = 0;
  // Called when the pass is not iteresting.
  virtual void omitAfter(StringRef PassID, std::string &Name) = 0;
  // Called when an interesting IR has changed.
  virtual void handleAfter(StringRef PassID, std::string &Name,
                           const IRUnitT &Before, const IRUnitT &After,
                           Any) = 0;
  // Called when an interesting pass is invalidated.
  virtual void handleInvalidated(StringRef PassID) = 0;
  // Called when the IR or pass is not interesting.
  virtual void handleFiltered(StringRef PassID, std::string &Name) = 0;
  // Called when an ignored pass is encountered.
  virtual void handleIgnored(StringRef PassID, std::string &Name) = 0;

  // Stack of IRs before passes.
  std::vector<IRUnitT> BeforeStack;
  // Is this the first IR seen?
  bool InitialIR = true;

  // Run in verbose mode, printing everything?
  const bool VerboseMode;
};

// An abstract template base class that handles printing banners and
// reporting when things have not changed or are filtered out.
template <typename IRUnitT>
class TextChangeReporter : public ChangeReporter<IRUnitT> {
protected:
  TextChangeReporter(bool Verbose);

  // Print a module dump of the first IR that is changed.
  void handleInitialIR(Any IR) override;
  // Report that the IR was omitted because it did not change.
  void omitAfter(StringRef PassID, std::string &Name) override;
  // Report that the pass was invalidated.
  void handleInvalidated(StringRef PassID) override;
  // Report that the IR was filtered out.
  void handleFiltered(StringRef PassID, std::string &Name) override;
  // Report that the pass was ignored.
  void handleIgnored(StringRef PassID, std::string &Name) override;
  // Make substitutions in \p S suitable for reporting changes
  // after the pass and then print it.

  raw_ostream &Out;
};

// A change printer based on the string representation of the IR as created
// by unwrapAndPrint.  The string representation is stored in a std::string
// to preserve it as the IR changes in each pass.  Note that the banner is
// included in this representation but it is massaged before reporting.
class IRChangedPrinter : public TextChangeReporter<std::string> {
public:
  IRChangedPrinter(bool VerboseMode)
      : TextChangeReporter<std::string>(VerboseMode) {}
  ~IRChangedPrinter() override;
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  // Called before and after a pass to get the representation of the IR.
  void generateIRRepresentation(Any IR, StringRef PassID,
                                std::string &Output) override;
  // Called when an interesting IR has changed.
  void handleAfter(StringRef PassID, std::string &Name,
                   const std::string &Before, const std::string &After,
                   Any) override;
};

class IRChangedTester : public IRChangedPrinter {
public:
  IRChangedTester() : IRChangedPrinter(true) {}
  ~IRChangedTester() override;
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  void handleIR(const std::string &IR, StringRef PassID);

  // Check initial IR
  void handleInitialIR(Any IR) override;
  // Do nothing.
  void omitAfter(StringRef PassID, std::string &Name) override;
  // Do nothing.
  void handleInvalidated(StringRef PassID) override;
  // Do nothing.
  void handleFiltered(StringRef PassID, std::string &Name) override;
  // Do nothing.
  void handleIgnored(StringRef PassID, std::string &Name) override;

  // Call test as interesting IR has changed.
  void handleAfter(StringRef PassID, std::string &Name,
                   const std::string &Before, const std::string &After,
                   Any) override;
};

// Information that needs to be saved for a basic block in order to compare
// before and after the pass to determine if it was changed by a pass.
template <typename T> class BlockDataT {
public:
  BlockDataT(const BasicBlock &B) : Label(B.getName().str()), Data(B) {
    raw_string_ostream SS(Body);
    B.print(SS, nullptr, true, true);
  }

  BlockDataT(const MachineBasicBlock &B) : Label(B.getName().str()), Data(B) {
    raw_string_ostream SS(Body);
    B.print(SS);
  }

  bool operator==(const BlockDataT &That) const { return Body == That.Body; }
  bool operator!=(const BlockDataT &That) const { return Body != That.Body; }

  // Return the label of the represented basic block.
  StringRef getLabel() const { return Label; }
  // Return the string representation of the basic block.
  StringRef getBody() const { return Body; }

  // Return the associated data
  const T &getData() const { return Data; }

protected:
  std::string Label;
  std::string Body;

  // Extra data associated with a basic block
  T Data;
};

template <typename T> class OrderedChangedData {
public:
  // Return the names in the order they were saved
  std::vector<std::string> &getOrder() { return Order; }
  const std::vector<std::string> &getOrder() const { return Order; }

  // Return a map of names to saved representations
  StringMap<T> &getData() { return Data; }
  const StringMap<T> &getData() const { return Data; }

  bool operator==(const OrderedChangedData<T> &That) const {
    return Data == That.getData();
  }

  // Call the lambda \p HandlePair on each corresponding pair of data from
  // \p Before and \p After.  The order is based on the order in \p After
  // with ones that are only in \p Before interspersed based on where they
  // occur in \p Before.  This is used to present the output in an order
  // based on how the data is ordered in LLVM.
  static void report(const OrderedChangedData &Before,
                     const OrderedChangedData &After,
                     function_ref<void(const T *, const T *)> HandlePair);

protected:
  std::vector<std::string> Order;
  StringMap<T> Data;
};

// Do not need extra information for patch-style change reporter.
class EmptyData {
public:
  EmptyData(const BasicBlock &) {}
  EmptyData(const MachineBasicBlock &) {}
};

// The data saved for comparing functions.
template <typename T>
class FuncDataT : public OrderedChangedData<BlockDataT<T>> {
public:
  FuncDataT(std::string S) : EntryBlockName(S) {}

  // Return the name of the entry block
  std::string getEntryBlockName() const { return EntryBlockName; }

protected:
  std::string EntryBlockName;
};

// The data saved for comparing IRs.
template <typename T>
class IRDataT : public OrderedChangedData<FuncDataT<T>> {};

// Abstract template base class for a class that compares two IRs.  The
// class is created with the 2 IRs to compare and then compare is called.
// The static function analyzeIR is used to build up the IR representation.
template <typename T> class IRComparer {
public:
  IRComparer(const IRDataT<T> &Before, const IRDataT<T> &After)
      : Before(Before), After(After) {}

  // Compare the 2 IRs. \p handleFunctionCompare is called to handle the
  // compare of a function. When \p InModule is set,
  // this function is being handled as part of comparing a module.
  void compare(
      bool CompareModule,
      std::function<void(bool InModule, unsigned Minor,
                         const FuncDataT<T> &Before, const FuncDataT<T> &After)>
          CompareFunc);

  // Analyze \p IR and build the IR representation in \p Data.
  static void analyzeIR(Any IR, IRDataT<T> &Data);

protected:
  // Generate the data for \p F into \p Data.
  template <typename FunctionT>
  static bool generateFunctionData(IRDataT<T> &Data, const FunctionT &F);

  const IRDataT<T> &Before;
  const IRDataT<T> &After;
};

// A change printer that prints out in-line differences in the basic
// blocks.  It uses an InlineComparer to do the comparison so it shows
// the differences prefixed with '-' and '+' for code that is removed
// and added, respectively.  Changes to the IR that do not affect basic
// blocks are not reported as having changed the IR.  The option
// -print-module-scope does not affect this change reporter.
class InLineChangePrinter : public TextChangeReporter<IRDataT<EmptyData>> {
public:
  InLineChangePrinter(bool VerboseMode, bool ColourMode)
      : TextChangeReporter<IRDataT<EmptyData>>(VerboseMode),
        UseColour(ColourMode) {}
  ~InLineChangePrinter() override;
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  // Create a representation of the IR.
  void generateIRRepresentation(Any IR, StringRef PassID,
                                IRDataT<EmptyData> &Output) override;

  // Called when an interesting IR has changed.
  void handleAfter(StringRef PassID, std::string &Name,
                   const IRDataT<EmptyData> &Before,
                   const IRDataT<EmptyData> &After, Any) override;

  void handleFunctionCompare(StringRef Name, StringRef Prefix, StringRef PassID,
                             StringRef Divider, bool InModule, unsigned Minor,
                             const FuncDataT<EmptyData> &Before,
                             const FuncDataT<EmptyData> &After);

  bool UseColour;
};

class VerifyInstrumentation {
  bool DebugLogging;

public:
  VerifyInstrumentation(bool DebugLogging) : DebugLogging(DebugLogging) {}
  void registerCallbacks(PassInstrumentationCallbacks &PIC,
                         ModuleAnalysisManager *MAM);
};

/// This class implements --time-trace functionality for new pass manager.
/// It provides the pass-instrumentation callbacks that measure the pass
/// execution time. They collect time tracing info by TimeProfiler.
class TimeProfilingPassesHandler {
public:
  TimeProfilingPassesHandler();
  // We intend this to be unique per-compilation, thus no copies.
  TimeProfilingPassesHandler(const TimeProfilingPassesHandler &) = delete;
  void operator=(const TimeProfilingPassesHandler &) = delete;

  void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  // Implementation of pass instrumentation callbacks.
  void runBeforePass(StringRef PassID, Any IR);
  void runAfterPass();
};

// Class that holds transitions between basic blocks.  The transitions
// are contained in a map of values to names of basic blocks.
class DCData {
public:
  // Fill the map with the transitions from basic block \p B.
  DCData(const BasicBlock &B);
  DCData(const MachineBasicBlock &B);

  // Return an iterator to the names of the successor blocks.
  StringMap<std::string>::const_iterator begin() const {
    return Successors.begin();
  }
  StringMap<std::string>::const_iterator end() const {
    return Successors.end();
  }

  // Return the label of the basic block reached on a transition on \p S.
  StringRef getSuccessorLabel(StringRef S) const {
    assert(Successors.count(S) == 1 && "Expected to find successor.");
    return Successors.find(S)->getValue();
  }

protected:
  // Add a transition to \p Succ on \p Label
  void addSuccessorLabel(StringRef Succ, StringRef Label) {
    std::pair<std::string, std::string> SS{Succ.str(), Label.str()};
    Successors.insert(SS);
  }

  StringMap<std::string> Successors;
};

// A change reporter that builds a website with links to pdf files showing
// dot control flow graphs with changed instructions shown in colour.
class DotCfgChangeReporter : public ChangeReporter<IRDataT<DCData>> {
public:
  DotCfgChangeReporter(bool Verbose);
  ~DotCfgChangeReporter() override;
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  // Initialize the HTML file and output the header.
  bool initializeHTML();

  // Called on the first IR processed.
  void handleInitialIR(Any IR) override;
  // Called before and after a pass to get the representation of the IR.
  void generateIRRepresentation(Any IR, StringRef PassID,
                                IRDataT<DCData> &Output) override;
  // Called when the pass is not iteresting.
  void omitAfter(StringRef PassID, std::string &Name) override;
  // Called when an interesting IR has changed.
  void handleAfter(StringRef PassID, std::string &Name,
                   const IRDataT<DCData> &Before, const IRDataT<DCData> &After,
                   Any) override;
  // Called when an interesting pass is invalidated.
  void handleInvalidated(StringRef PassID) override;
  // Called when the IR or pass is not interesting.
  void handleFiltered(StringRef PassID, std::string &Name) override;
  // Called when an ignored pass is encountered.
  void handleIgnored(StringRef PassID, std::string &Name) override;

  // Generate the pdf file into \p Dir / \p PDFFileName using \p DotFile as
  // input and return the html <a> tag with \Text as the content.
  static std::string genHTML(StringRef Text, StringRef DotFile,
                             StringRef PDFFileName);

  void handleFunctionCompare(StringRef Name, StringRef Prefix, StringRef PassID,
                             StringRef Divider, bool InModule, unsigned Minor,
                             const FuncDataT<DCData> &Before,
                             const FuncDataT<DCData> &After);

  unsigned N = 0;
  std::unique_ptr<raw_fd_ostream> HTML;
};

// Print IR on crash.
class PrintCrashIRInstrumentation {
public:
  PrintCrashIRInstrumentation()
      : SavedIR("*** Dump of IR Before Last Pass Unknown ***") {}
  ~PrintCrashIRInstrumentation();
  void registerCallbacks(PassInstrumentationCallbacks &PIC);
  void reportCrashIR();

protected:
  std::string SavedIR;

private:
  // The crash reporter that will report on a crash.
  static PrintCrashIRInstrumentation *CrashReporter;
  // Crash handler registered when print-on-crash is specified.
  static void SignalHandler(void *);
};

/// This class provides an interface to register all the standard pass
/// instrumentations and manages their state (if any).
class StandardInstrumentations {
  PrintIRInstrumentation PrintIR;
  PrintPassInstrumentation PrintPass;
  TimePassesHandler TimePasses;
  TimeProfilingPassesHandler TimeProfilingPasses;
  OptNoneInstrumentation OptNone;
  OptPassGateInstrumentation OptPassGate;
  PreservedCFGCheckerInstrumentation PreservedCFGChecker;
  IRChangedPrinter PrintChangedIR;
  PseudoProbeVerifier PseudoProbeVerification;
  InLineChangePrinter PrintChangedDiff;
  DotCfgChangeReporter WebsiteChangeReporter;
  PrintCrashIRInstrumentation PrintCrashIR;
  IRChangedTester ChangeTester;
  VerifyInstrumentation Verify;

  bool VerifyEach;

public:
  StandardInstrumentations(LLVMContext &Context, bool DebugLogging,
                           bool VerifyEach = false,
                           PrintPassOptions PrintPassOpts = PrintPassOptions());

  // Register all the standard instrumentation callbacks. If \p FAM is nullptr
  // then PreservedCFGChecker is not enabled.
  void registerCallbacks(PassInstrumentationCallbacks &PIC,
                         ModuleAnalysisManager *MAM = nullptr);

  TimePassesHandler &getTimePasses() { return TimePasses; }
};

extern template class ChangeReporter<std::string>;
extern template class TextChangeReporter<std::string>;

extern template class BlockDataT<EmptyData>;
extern template class FuncDataT<EmptyData>;
extern template class IRDataT<EmptyData>;
extern template class ChangeReporter<IRDataT<EmptyData>>;
extern template class TextChangeReporter<IRDataT<EmptyData>>;
extern template class IRComparer<EmptyData>;

} // namespace llvm

#endif
