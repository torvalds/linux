//===-- llvm-exegesis.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Measures execution properties (latencies/uops) of an instruction.
///
//===----------------------------------------------------------------------===//

#include "lib/Analysis.h"
#include "lib/BenchmarkResult.h"
#include "lib/BenchmarkRunner.h"
#include "lib/Clustering.h"
#include "lib/CodeTemplate.h"
#include "lib/Error.h"
#include "lib/LlvmState.h"
#include "lib/PerfHelper.h"
#include "lib/ProgressMeter.h"
#include "lib/ResultAggregator.h"
#include "lib/SnippetFile.h"
#include "lib/SnippetRepetitor.h"
#include "lib/Target.h"
#include "lib/TargetSelect.h"
#include "lib/ValidationEvent.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <string>

namespace llvm {
namespace exegesis {

static cl::opt<int> OpcodeIndex(
    "opcode-index",
    cl::desc("opcode to measure, by index, or -1 to measure all opcodes"),
    cl::cat(BenchmarkOptions), cl::init(0));

static cl::opt<std::string>
    OpcodeNames("opcode-name",
                cl::desc("comma-separated list of opcodes to measure, by name"),
                cl::cat(BenchmarkOptions), cl::init(""));

static cl::opt<std::string> SnippetsFile("snippets-file",
                                         cl::desc("code snippets to measure"),
                                         cl::cat(BenchmarkOptions),
                                         cl::init(""));

static cl::opt<std::string>
    BenchmarkFile("benchmarks-file",
                  cl::desc("File to read (analysis mode) or write "
                           "(latency/uops/inverse_throughput modes) benchmark "
                           "results. “-” uses stdin/stdout."),
                  cl::cat(Options), cl::init(""));

static cl::opt<Benchmark::ModeE> BenchmarkMode(
    "mode", cl::desc("the mode to run"), cl::cat(Options),
    cl::values(clEnumValN(Benchmark::Latency, "latency", "Instruction Latency"),
               clEnumValN(Benchmark::InverseThroughput, "inverse_throughput",
                          "Instruction Inverse Throughput"),
               clEnumValN(Benchmark::Uops, "uops", "Uop Decomposition"),
               // When not asking for a specific benchmark mode,
               // we'll analyse the results.
               clEnumValN(Benchmark::Unknown, "analysis", "Analysis")));

static cl::opt<Benchmark::ResultAggregationModeE> ResultAggMode(
    "result-aggregation-mode", cl::desc("How to aggregate multi-values result"),
    cl::cat(BenchmarkOptions),
    cl::values(clEnumValN(Benchmark::Min, "min", "Keep min reading"),
               clEnumValN(Benchmark::Max, "max", "Keep max reading"),
               clEnumValN(Benchmark::Mean, "mean",
                          "Compute mean of all readings"),
               clEnumValN(Benchmark::MinVariance, "min-variance",
                          "Keep readings set with min-variance")),
    cl::init(Benchmark::Min));

static cl::opt<Benchmark::RepetitionModeE> RepetitionMode(
    "repetition-mode", cl::desc("how to repeat the instruction snippet"),
    cl::cat(BenchmarkOptions),
    cl::values(
        clEnumValN(Benchmark::Duplicate, "duplicate", "Duplicate the snippet"),
        clEnumValN(Benchmark::Loop, "loop", "Loop over the snippet"),
        clEnumValN(Benchmark::AggregateMin, "min",
                   "All of the above and take the minimum of measurements"),
        clEnumValN(Benchmark::MiddleHalfDuplicate, "middle-half-duplicate",
                   "Middle half duplicate mode"),
        clEnumValN(Benchmark::MiddleHalfLoop, "middle-half-loop",
                   "Middle half loop mode")),
    cl::init(Benchmark::Duplicate));

static cl::opt<bool> BenchmarkMeasurementsPrintProgress(
    "measurements-print-progress",
    cl::desc("Produce progress indicator when performing measurements"),
    cl::cat(BenchmarkOptions), cl::init(false));

static cl::opt<BenchmarkPhaseSelectorE> BenchmarkPhaseSelector(
    "benchmark-phase",
    cl::desc(
        "it is possible to stop the benchmarking process after some phase"),
    cl::cat(BenchmarkOptions),
    cl::values(
        clEnumValN(BenchmarkPhaseSelectorE::PrepareSnippet, "prepare-snippet",
                   "Only generate the minimal instruction sequence"),
        clEnumValN(BenchmarkPhaseSelectorE::PrepareAndAssembleSnippet,
                   "prepare-and-assemble-snippet",
                   "Same as prepare-snippet, but also dumps an excerpt of the "
                   "sequence (hex encoded)"),
        clEnumValN(BenchmarkPhaseSelectorE::AssembleMeasuredCode,
                   "assemble-measured-code",
                   "Same as prepare-and-assemble-snippet, but also creates the "
                   "full sequence "
                   "that can be dumped to a file using --dump-object-to-disk"),
        clEnumValN(
            BenchmarkPhaseSelectorE::Measure, "measure",
            "Same as prepare-measured-code, but also runs the measurement "
            "(default)")),
    cl::init(BenchmarkPhaseSelectorE::Measure));

static cl::opt<bool>
    UseDummyPerfCounters("use-dummy-perf-counters",
                         cl::desc("Do not read real performance counters, use "
                                  "dummy values (for testing)"),
                         cl::cat(BenchmarkOptions), cl::init(false));

static cl::opt<unsigned>
    MinInstructions("min-instructions",
                    cl::desc("The minimum number of instructions that should "
                             "be included in the snippet"),
                    cl::cat(BenchmarkOptions), cl::init(10000));

static cl::opt<unsigned>
    LoopBodySize("loop-body-size",
                 cl::desc("when repeating the instruction snippet by looping "
                          "over it, duplicate the snippet until the loop body "
                          "contains at least this many instruction"),
                 cl::cat(BenchmarkOptions), cl::init(0));

static cl::opt<unsigned> MaxConfigsPerOpcode(
    "max-configs-per-opcode",
    cl::desc(
        "allow to snippet generator to generate at most that many configs"),
    cl::cat(BenchmarkOptions), cl::init(1));

static cl::opt<bool> IgnoreInvalidSchedClass(
    "ignore-invalid-sched-class",
    cl::desc("ignore instructions that do not define a sched class"),
    cl::cat(BenchmarkOptions), cl::init(false));

static cl::opt<BenchmarkFilter> AnalysisSnippetFilter(
    "analysis-filter", cl::desc("Filter the benchmarks before analysing them"),
    cl::cat(BenchmarkOptions),
    cl::values(
        clEnumValN(BenchmarkFilter::All, "all",
                   "Keep all benchmarks (default)"),
        clEnumValN(BenchmarkFilter::RegOnly, "reg-only",
                   "Keep only those benchmarks that do *NOT* involve memory"),
        clEnumValN(BenchmarkFilter::WithMem, "mem-only",
                   "Keep only the benchmarks that *DO* involve memory")),
    cl::init(BenchmarkFilter::All));

static cl::opt<BenchmarkClustering::ModeE> AnalysisClusteringAlgorithm(
    "analysis-clustering", cl::desc("the clustering algorithm to use"),
    cl::cat(AnalysisOptions),
    cl::values(clEnumValN(BenchmarkClustering::Dbscan, "dbscan",
                          "use DBSCAN/OPTICS algorithm"),
               clEnumValN(BenchmarkClustering::Naive, "naive",
                          "one cluster per opcode")),
    cl::init(BenchmarkClustering::Dbscan));

static cl::opt<unsigned> AnalysisDbscanNumPoints(
    "analysis-numpoints",
    cl::desc("minimum number of points in an analysis cluster (dbscan only)"),
    cl::cat(AnalysisOptions), cl::init(3));

static cl::opt<float> AnalysisClusteringEpsilon(
    "analysis-clustering-epsilon",
    cl::desc("epsilon for benchmark point clustering"),
    cl::cat(AnalysisOptions), cl::init(0.1));

static cl::opt<float> AnalysisInconsistencyEpsilon(
    "analysis-inconsistency-epsilon",
    cl::desc("epsilon for detection of when the cluster is different from the "
             "LLVM schedule profile values"),
    cl::cat(AnalysisOptions), cl::init(0.1));

static cl::opt<std::string>
    AnalysisClustersOutputFile("analysis-clusters-output-file", cl::desc(""),
                               cl::cat(AnalysisOptions), cl::init(""));
static cl::opt<std::string>
    AnalysisInconsistenciesOutputFile("analysis-inconsistencies-output-file",
                                      cl::desc(""), cl::cat(AnalysisOptions),
                                      cl::init(""));

static cl::opt<bool> AnalysisDisplayUnstableOpcodes(
    "analysis-display-unstable-clusters",
    cl::desc("if there is more than one benchmark for an opcode, said "
             "benchmarks may end up not being clustered into the same cluster "
             "if the measured performance characteristics are different. by "
             "default all such opcodes are filtered out. this flag will "
             "instead show only such unstable opcodes"),
    cl::cat(AnalysisOptions), cl::init(false));

static cl::opt<bool> AnalysisOverrideBenchmarksTripleAndCpu(
    "analysis-override-benchmark-triple-and-cpu",
    cl::desc("By default, we analyze the benchmarks for the triple/CPU they "
             "were measured for, but if you want to analyze them for some "
             "other combination (specified via -mtriple/-mcpu), you can "
             "pass this flag."),
    cl::cat(AnalysisOptions), cl::init(false));

static cl::opt<std::string>
    TripleName("mtriple",
               cl::desc("Target triple. See -version for available targets"),
               cl::cat(Options));

static cl::opt<std::string>
    MCPU("mcpu",
         cl::desc("Target a specific cpu type (-mcpu=help for details)"),
         cl::value_desc("cpu-name"), cl::cat(Options), cl::init("native"));

static cl::opt<std::string>
    DumpObjectToDisk("dump-object-to-disk",
                     cl::desc("dumps the generated benchmark object to disk "
                              "and prints a message to access it"),
                     cl::ValueOptional, cl::cat(BenchmarkOptions));

static cl::opt<BenchmarkRunner::ExecutionModeE> ExecutionMode(
    "execution-mode",
    cl::desc("Selects the execution mode to use for running snippets"),
    cl::cat(BenchmarkOptions),
    cl::values(clEnumValN(BenchmarkRunner::ExecutionModeE::InProcess,
                          "inprocess",
                          "Executes the snippets within the same process"),
               clEnumValN(BenchmarkRunner::ExecutionModeE::SubProcess,
                          "subprocess",
                          "Spawns a subprocess for each snippet execution, "
                          "allows for the use of memory annotations")),
    cl::init(BenchmarkRunner::ExecutionModeE::InProcess));

static cl::opt<unsigned> BenchmarkRepeatCount(
    "benchmark-repeat-count",
    cl::desc("The number of times to repeat measurements on the benchmark k "
             "before aggregating the results"),
    cl::cat(BenchmarkOptions), cl::init(30));

static cl::list<ValidationEvent> ValidationCounters(
    "validation-counter",
    cl::desc(
        "The name of a validation counter to run concurrently with the main "
        "counter to validate benchmarking assumptions"),
    cl::CommaSeparated, cl::cat(BenchmarkOptions), ValidationEventOptions());

static ExitOnError ExitOnErr("llvm-exegesis error: ");

// Helper function that logs the error(s) and exits.
template <typename... ArgTs> static void ExitWithError(ArgTs &&... Args) {
  ExitOnErr(make_error<Failure>(std::forward<ArgTs>(Args)...));
}

// Check Err. If it's in a failure state log the file error(s) and exit.
static void ExitOnFileError(const Twine &FileName, Error Err) {
  if (Err) {
    ExitOnErr(createFileError(FileName, std::move(Err)));
  }
}

// Check E. If it's in a success state then return the contained value.
// If it's in a failure state log the file error(s) and exit.
template <typename T>
T ExitOnFileError(const Twine &FileName, Expected<T> &&E) {
  ExitOnFileError(FileName, E.takeError());
  return std::move(*E);
}

// Checks that only one of OpcodeNames, OpcodeIndex or SnippetsFile is provided,
// and returns the opcode indices or {} if snippets should be read from
// `SnippetsFile`.
static std::vector<unsigned> getOpcodesOrDie(const LLVMState &State) {
  const size_t NumSetFlags = (OpcodeNames.empty() ? 0 : 1) +
                             (OpcodeIndex == 0 ? 0 : 1) +
                             (SnippetsFile.empty() ? 0 : 1);
  const auto &ET = State.getExegesisTarget();
  const auto AvailableFeatures = State.getSubtargetInfo().getFeatureBits();

  if (NumSetFlags != 1) {
    ExitOnErr.setBanner("llvm-exegesis: ");
    ExitWithError("please provide one and only one of 'opcode-index', "
                  "'opcode-name' or 'snippets-file'");
  }
  if (!SnippetsFile.empty())
    return {};
  if (OpcodeIndex > 0)
    return {static_cast<unsigned>(OpcodeIndex)};
  if (OpcodeIndex < 0) {
    std::vector<unsigned> Result;
    unsigned NumOpcodes = State.getInstrInfo().getNumOpcodes();
    Result.reserve(NumOpcodes);
    for (unsigned I = 0, E = NumOpcodes; I < E; ++I) {
      if (!ET.isOpcodeAvailable(I, AvailableFeatures))
        continue;
      Result.push_back(I);
    }
    return Result;
  }
  // Resolve opcode name -> opcode.
  const auto ResolveName = [&State](StringRef OpcodeName) -> unsigned {
    const auto &Map = State.getOpcodeNameToOpcodeIdxMapping();
    auto I = Map.find(OpcodeName);
    if (I != Map.end())
      return I->getSecond();
    return 0u;
  };
  SmallVector<StringRef, 2> Pieces;
  StringRef(OpcodeNames.getValue())
      .split(Pieces, ",", /* MaxSplit */ -1, /* KeepEmpty */ false);
  std::vector<unsigned> Result;
  Result.reserve(Pieces.size());
  for (const StringRef &OpcodeName : Pieces) {
    if (unsigned Opcode = ResolveName(OpcodeName))
      Result.push_back(Opcode);
    else
      ExitWithError(Twine("unknown opcode ").concat(OpcodeName));
  }
  return Result;
}

// Generates code snippets for opcode `Opcode`.
static Expected<std::vector<BenchmarkCode>>
generateSnippets(const LLVMState &State, unsigned Opcode,
                 const BitVector &ForbiddenRegs) {
  const Instruction &Instr = State.getIC().getInstr(Opcode);
  const MCInstrDesc &InstrDesc = Instr.Description;
  // Ignore instructions that we cannot run.
  if (InstrDesc.isPseudo() || InstrDesc.usesCustomInsertionHook())
    return make_error<Failure>(
        "Unsupported opcode: isPseudo/usesCustomInserter");
  if (InstrDesc.isBranch() || InstrDesc.isIndirectBranch())
    return make_error<Failure>("Unsupported opcode: isBranch/isIndirectBranch");
  if (InstrDesc.isCall() || InstrDesc.isReturn())
    return make_error<Failure>("Unsupported opcode: isCall/isReturn");

  const std::vector<InstructionTemplate> InstructionVariants =
      State.getExegesisTarget().generateInstructionVariants(
          Instr, MaxConfigsPerOpcode);

  SnippetGenerator::Options SnippetOptions;
  SnippetOptions.MaxConfigsPerOpcode = MaxConfigsPerOpcode;
  const std::unique_ptr<SnippetGenerator> Generator =
      State.getExegesisTarget().createSnippetGenerator(BenchmarkMode, State,
                                                       SnippetOptions);
  if (!Generator)
    ExitWithError("cannot create snippet generator");

  std::vector<BenchmarkCode> Benchmarks;
  for (const InstructionTemplate &Variant : InstructionVariants) {
    if (Benchmarks.size() >= MaxConfigsPerOpcode)
      break;
    if (auto Err = Generator->generateConfigurations(Variant, Benchmarks,
                                                     ForbiddenRegs))
      return std::move(Err);
  }
  return Benchmarks;
}

static void runBenchmarkConfigurations(
    const LLVMState &State, ArrayRef<BenchmarkCode> Configurations,
    ArrayRef<std::unique_ptr<const SnippetRepetitor>> Repetitors,
    const BenchmarkRunner &Runner) {
  assert(!Configurations.empty() && "Don't have any configurations to run.");
  std::optional<raw_fd_ostream> FileOstr;
  if (BenchmarkFile != "-") {
    int ResultFD = 0;
    // Create output file or open existing file and truncate it, once.
    ExitOnErr(errorCodeToError(openFileForWrite(BenchmarkFile, ResultFD,
                                                sys::fs::CD_CreateAlways,
                                                sys::fs::OF_TextWithCRLF)));
    FileOstr.emplace(ResultFD, true /*shouldClose*/);
  }
  raw_ostream &Ostr = FileOstr ? *FileOstr : outs();

  std::optional<ProgressMeter<>> Meter;
  if (BenchmarkMeasurementsPrintProgress)
    Meter.emplace(Configurations.size());

  SmallVector<unsigned, 2> MinInstructionCounts = {MinInstructions};
  if (RepetitionMode == Benchmark::MiddleHalfDuplicate ||
      RepetitionMode == Benchmark::MiddleHalfLoop)
    MinInstructionCounts.push_back(MinInstructions * 2);

  for (const BenchmarkCode &Conf : Configurations) {
    ProgressMeter<>::ProgressMeterStep MeterStep(Meter ? &*Meter : nullptr);
    SmallVector<Benchmark, 2> AllResults;

    for (const std::unique_ptr<const SnippetRepetitor> &Repetitor :
         Repetitors) {
      for (unsigned IterationRepetitions : MinInstructionCounts) {
        auto RC = ExitOnErr(Runner.getRunnableConfiguration(
            Conf, IterationRepetitions, LoopBodySize, *Repetitor));
        std::optional<StringRef> DumpFile;
        if (DumpObjectToDisk.getNumOccurrences())
          DumpFile = DumpObjectToDisk;
        auto [Err, BenchmarkResult] =
            Runner.runConfiguration(std::move(RC), DumpFile);
        if (Err) {
          // Errors from executing the snippets are fine.
          // All other errors are a framework issue and should fail.
          if (!Err.isA<SnippetExecutionFailure>())
            ExitOnErr(std::move(Err));

          BenchmarkResult.Error = toString(std::move(Err));
        }
        AllResults.push_back(std::move(BenchmarkResult));
      }
    }

    Benchmark &Result = AllResults.front();

    // If any of our measurements failed, pretend they all have failed.
    if (AllResults.size() > 1 &&
        any_of(AllResults, [](const Benchmark &R) {
          return R.Measurements.empty();
        }))
      Result.Measurements.clear();

    std::unique_ptr<ResultAggregator> ResultAgg =
        ResultAggregator::CreateAggregator(RepetitionMode);
    ResultAgg->AggregateResults(Result,
                                ArrayRef<Benchmark>(AllResults).drop_front());

    // With dummy counters, measurements are rather meaningless,
    // so drop them altogether.
    if (UseDummyPerfCounters)
      Result.Measurements.clear();

    ExitOnFileError(BenchmarkFile, Result.writeYamlTo(State, Ostr));
  }
}

void benchmarkMain() {
  if (BenchmarkPhaseSelector == BenchmarkPhaseSelectorE::Measure &&
      !UseDummyPerfCounters) {
#ifndef HAVE_LIBPFM
    ExitWithError(
        "benchmarking unavailable, LLVM was built without libpfm. You can "
        "pass --benchmark-phase=... to skip the actual benchmarking or "
        "--use-dummy-perf-counters to not query the kernel for real event "
        "counts.");
#else
    if (pfm::pfmInitialize())
      ExitWithError("cannot initialize libpfm");
#endif
  }

  InitializeAllExegesisTargets();
#define LLVM_EXEGESIS(TargetName)                                              \
  LLVMInitialize##TargetName##AsmPrinter();                                    \
  LLVMInitialize##TargetName##AsmParser();
#include "llvm/Config/TargetExegesis.def"

  const LLVMState State =
      ExitOnErr(LLVMState::Create(TripleName, MCPU, "", UseDummyPerfCounters));

  // Preliminary check to ensure features needed for requested
  // benchmark mode are present on target CPU and/or OS.
  if (BenchmarkPhaseSelector == BenchmarkPhaseSelectorE::Measure)
    ExitOnErr(State.getExegesisTarget().checkFeatureSupport());

  if (ExecutionMode == BenchmarkRunner::ExecutionModeE::SubProcess &&
      UseDummyPerfCounters)
    ExitWithError("Dummy perf counters are not supported in the subprocess "
                  "execution mode.");

  const std::unique_ptr<BenchmarkRunner> Runner =
      ExitOnErr(State.getExegesisTarget().createBenchmarkRunner(
          BenchmarkMode, State, BenchmarkPhaseSelector, ExecutionMode,
          BenchmarkRepeatCount, ValidationCounters, ResultAggMode));
  if (!Runner) {
    ExitWithError("cannot create benchmark runner");
  }

  const auto Opcodes = getOpcodesOrDie(State);
  std::vector<BenchmarkCode> Configurations;

  unsigned LoopRegister =
      State.getExegesisTarget().getDefaultLoopCounterRegister(
          State.getTargetMachine().getTargetTriple());

  if (Opcodes.empty()) {
    Configurations = ExitOnErr(readSnippets(State, SnippetsFile));
    for (const auto &Configuration : Configurations) {
      if (ExecutionMode != BenchmarkRunner::ExecutionModeE::SubProcess &&
          (Configuration.Key.MemoryMappings.size() != 0 ||
           Configuration.Key.MemoryValues.size() != 0 ||
           Configuration.Key.SnippetAddress != 0))
        ExitWithError("Memory and snippet address annotations are only "
                      "supported in subprocess "
                      "execution mode");
    }
    LoopRegister = Configurations[0].Key.LoopRegister;
  }

  SmallVector<std::unique_ptr<const SnippetRepetitor>, 2> Repetitors;
  if (RepetitionMode != Benchmark::RepetitionModeE::AggregateMin)
    Repetitors.emplace_back(
        SnippetRepetitor::Create(RepetitionMode, State, LoopRegister));
  else {
    for (Benchmark::RepetitionModeE RepMode :
         {Benchmark::RepetitionModeE::Duplicate,
          Benchmark::RepetitionModeE::Loop})
      Repetitors.emplace_back(
          SnippetRepetitor::Create(RepMode, State, LoopRegister));
  }

  BitVector AllReservedRegs;
  for (const std::unique_ptr<const SnippetRepetitor> &Repetitor : Repetitors)
    AllReservedRegs |= Repetitor->getReservedRegs();

  if (!Opcodes.empty()) {
    for (const unsigned Opcode : Opcodes) {
      // Ignore instructions without a sched class if
      // -ignore-invalid-sched-class is passed.
      if (IgnoreInvalidSchedClass &&
          State.getInstrInfo().get(Opcode).getSchedClass() == 0) {
        errs() << State.getInstrInfo().getName(Opcode)
               << ": ignoring instruction without sched class\n";
        continue;
      }

      auto ConfigsForInstr = generateSnippets(State, Opcode, AllReservedRegs);
      if (!ConfigsForInstr) {
        logAllUnhandledErrors(
            ConfigsForInstr.takeError(), errs(),
            Twine(State.getInstrInfo().getName(Opcode)).concat(": "));
        continue;
      }
      std::move(ConfigsForInstr->begin(), ConfigsForInstr->end(),
                std::back_inserter(Configurations));
    }
  }

  if (MinInstructions == 0) {
    ExitOnErr.setBanner("llvm-exegesis: ");
    ExitWithError("--min-instructions must be greater than zero");
  }

  // Write to standard output if file is not set.
  if (BenchmarkFile.empty())
    BenchmarkFile = "-";

  if (!Configurations.empty())
    runBenchmarkConfigurations(State, Configurations, Repetitors, *Runner);

  pfm::pfmTerminate();
}

// Prints the results of running analysis pass `Pass` to file `OutputFilename`
// if OutputFilename is non-empty.
template <typename Pass>
static void maybeRunAnalysis(const Analysis &Analyzer, const std::string &Name,
                             const std::string &OutputFilename) {
  if (OutputFilename.empty())
    return;
  if (OutputFilename != "-") {
    errs() << "Printing " << Name << " results to file '" << OutputFilename
           << "'\n";
  }
  std::error_code ErrorCode;
  raw_fd_ostream ClustersOS(OutputFilename, ErrorCode,
                            sys::fs::FA_Read | sys::fs::FA_Write);
  if (ErrorCode)
    ExitOnFileError(OutputFilename, errorCodeToError(ErrorCode));
  if (auto Err = Analyzer.run<Pass>(ClustersOS))
    ExitOnFileError(OutputFilename, std::move(Err));
}

static void filterPoints(MutableArrayRef<Benchmark> Points,
                         const MCInstrInfo &MCII) {
  if (AnalysisSnippetFilter == BenchmarkFilter::All)
    return;

  bool WantPointsWithMemOps = AnalysisSnippetFilter == BenchmarkFilter::WithMem;
  for (Benchmark &Point : Points) {
    if (!Point.Error.empty())
      continue;
    if (WantPointsWithMemOps ==
        any_of(Point.Key.Instructions, [&MCII](const MCInst &Inst) {
          const MCInstrDesc &MCDesc = MCII.get(Inst.getOpcode());
          return MCDesc.mayLoad() || MCDesc.mayStore();
        }))
      continue;
    Point.Error = "filtered out by user";
  }
}

static void analysisMain() {
  ExitOnErr.setBanner("llvm-exegesis: ");
  if (BenchmarkFile.empty())
    ExitWithError("--benchmarks-file must be set");

  if (AnalysisClustersOutputFile.empty() &&
      AnalysisInconsistenciesOutputFile.empty()) {
    ExitWithError(
        "for --mode=analysis: At least one of --analysis-clusters-output-file "
        "and --analysis-inconsistencies-output-file must be specified");
  }

  InitializeAllExegesisTargets();
#define LLVM_EXEGESIS(TargetName)                                              \
  LLVMInitialize##TargetName##AsmPrinter();                                    \
  LLVMInitialize##TargetName##Disassembler();
#include "llvm/Config/TargetExegesis.def"

  auto MemoryBuffer = ExitOnFileError(
      BenchmarkFile,
      errorOrToExpected(MemoryBuffer::getFile(BenchmarkFile, /*IsText=*/true)));

  const auto TriplesAndCpus = ExitOnFileError(
      BenchmarkFile,
      Benchmark::readTriplesAndCpusFromYamls(*MemoryBuffer));
  if (TriplesAndCpus.empty()) {
    errs() << "no benchmarks to analyze\n";
    return;
  }
  if (TriplesAndCpus.size() > 1) {
    ExitWithError("analysis file contains benchmarks from several CPUs. This "
                  "is unsupported.");
  }
  auto TripleAndCpu = *TriplesAndCpus.begin();
  if (AnalysisOverrideBenchmarksTripleAndCpu) {
    errs() << "overridding file CPU name (" << TripleAndCpu.CpuName
           << ") with provided tripled (" << TripleName << ") and CPU name ("
           << MCPU << ")\n";
    TripleAndCpu.LLVMTriple = TripleName;
    TripleAndCpu.CpuName = MCPU;
  }
  errs() << "using Triple '" << TripleAndCpu.LLVMTriple << "' and CPU '"
         << TripleAndCpu.CpuName << "'\n";

  // Read benchmarks.
  const LLVMState State = ExitOnErr(
      LLVMState::Create(TripleAndCpu.LLVMTriple, TripleAndCpu.CpuName));
  std::vector<Benchmark> Points = ExitOnFileError(
      BenchmarkFile, Benchmark::readYamls(State, *MemoryBuffer));

  outs() << "Parsed " << Points.size() << " benchmark points\n";
  if (Points.empty()) {
    errs() << "no benchmarks to analyze\n";
    return;
  }
  // FIXME: Merge points from several runs (latency and uops).

  filterPoints(Points, State.getInstrInfo());

  const auto Clustering = ExitOnErr(BenchmarkClustering::create(
      Points, AnalysisClusteringAlgorithm, AnalysisDbscanNumPoints,
      AnalysisClusteringEpsilon, &State.getSubtargetInfo(),
      &State.getInstrInfo()));

  const Analysis Analyzer(State, Clustering, AnalysisInconsistencyEpsilon,
                          AnalysisDisplayUnstableOpcodes);

  maybeRunAnalysis<Analysis::PrintClusters>(Analyzer, "analysis clusters",
                                            AnalysisClustersOutputFile);
  maybeRunAnalysis<Analysis::PrintSchedClassInconsistencies>(
      Analyzer, "sched class consistency analysis",
      AnalysisInconsistenciesOutputFile);
}

} // namespace exegesis
} // namespace llvm

int main(int Argc, char **Argv) {
  using namespace llvm;

  InitLLVM X(Argc, Argv);

  // Initialize targets so we can print them when flag --version is specified.
#define LLVM_EXEGESIS(TargetName)                                              \
  LLVMInitialize##TargetName##Target();                                        \
  LLVMInitialize##TargetName##TargetInfo();                                    \
  LLVMInitialize##TargetName##TargetMC();
#include "llvm/Config/TargetExegesis.def"

  // Register the Target and CPU printer for --version.
  cl::AddExtraVersionPrinter(sys::printDefaultTargetAndDetectedCPU);

  // Enable printing of available targets when flag --version is specified.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  cl::HideUnrelatedOptions({&exegesis::Options, &exegesis::BenchmarkOptions,
                            &exegesis::AnalysisOptions});

  cl::ParseCommandLineOptions(Argc, Argv,
                              "llvm host machine instruction characteristics "
                              "measurment and analysis.\n");

  exegesis::ExitOnErr.setExitCodeMapper([](const Error &Err) {
    if (Err.isA<exegesis::ClusteringError>())
      return EXIT_SUCCESS;
    return EXIT_FAILURE;
  });

  if (exegesis::BenchmarkMode == exegesis::Benchmark::Unknown) {
    exegesis::analysisMain();
  } else {
    exegesis::benchmarkMain();
  }
  return EXIT_SUCCESS;
}
