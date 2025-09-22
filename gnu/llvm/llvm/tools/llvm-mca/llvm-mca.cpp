//===-- llvm-mca.cpp - Machine Code Analyzer -------------------*- C++ -* -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility is a simple driver that allows static performance analysis on
// machine code similarly to how IACA (Intel Architecture Code Analyzer) works.
//
//   llvm-mca [options] <file-name>
//      -march <type>
//      -mcpu <cpu>
//      -o <file>
//
// The target defaults to the host target.
// The cpu defaults to the 'native' host cpu.
// The output defaults to standard output.
//
//===----------------------------------------------------------------------===//

#include "CodeRegion.h"
#include "CodeRegionGenerator.h"
#include "PipelinePrinter.h"
#include "Views/BottleneckAnalysis.h"
#include "Views/DispatchStatistics.h"
#include "Views/InstructionInfoView.h"
#include "Views/RegisterFileStatistics.h"
#include "Views/ResourcePressureView.h"
#include "Views/RetireControlUnitStatistics.h"
#include "Views/SchedulerStatistics.h"
#include "Views/SummaryView.h"
#include "Views/TimelineView.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/MCA/CodeEmitter.h"
#include "llvm/MCA/Context.h"
#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/MCA/InstrBuilder.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/MCA/Stages/EntryStage.h"
#include "llvm/MCA/Stages/InstructionTables.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;

static mc::RegisterMCTargetOptionsFlags MOF;

static cl::OptionCategory ToolOptions("Tool Options");
static cl::OptionCategory ViewOptions("View Options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input file>"),
                                          cl::cat(ToolOptions), cl::init("-"));

static cl::opt<std::string> OutputFilename("o", cl::desc("Output filename"),
                                           cl::init("-"), cl::cat(ToolOptions),
                                           cl::value_desc("filename"));

static cl::opt<std::string>
    ArchName("march",
             cl::desc("Target architecture. "
                      "See -version for available targets"),
             cl::cat(ToolOptions));

static cl::opt<std::string>
    TripleName("mtriple",
               cl::desc("Target triple. See -version for available targets"),
               cl::cat(ToolOptions));

static cl::opt<std::string>
    MCPU("mcpu",
         cl::desc("Target a specific cpu type (-mcpu=help for details)"),
         cl::value_desc("cpu-name"), cl::cat(ToolOptions), cl::init("native"));

static cl::list<std::string>
    MATTRS("mattr", cl::CommaSeparated,
           cl::desc("Target specific attributes (-mattr=help for details)"),
           cl::value_desc("a1,+a2,-a3,..."), cl::cat(ToolOptions));

static cl::opt<bool> PrintJson("json",
                               cl::desc("Print the output in json format"),
                               cl::cat(ToolOptions), cl::init(false));

static cl::opt<int>
    OutputAsmVariant("output-asm-variant",
                     cl::desc("Syntax variant to use for output printing"),
                     cl::cat(ToolOptions), cl::init(-1));

static cl::opt<bool>
    PrintImmHex("print-imm-hex", cl::cat(ToolOptions), cl::init(false),
                cl::desc("Prefer hex format when printing immediate values"));

static cl::opt<unsigned> Iterations("iterations",
                                    cl::desc("Number of iterations to run"),
                                    cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned>
    DispatchWidth("dispatch", cl::desc("Override the processor dispatch width"),
                  cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned>
    RegisterFileSize("register-file-size",
                     cl::desc("Maximum number of physical registers which can "
                              "be used for register mappings"),
                     cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned>
    MicroOpQueue("micro-op-queue-size", cl::Hidden,
                 cl::desc("Number of entries in the micro-op queue"),
                 cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned>
    DecoderThroughput("decoder-throughput", cl::Hidden,
                      cl::desc("Maximum throughput from the decoders "
                               "(instructions per cycle)"),
                      cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned>
    CallLatency("call-latency", cl::Hidden,
                cl::desc("Number of cycles to assume for a call instruction"),
                cl::cat(ToolOptions), cl::init(100U));

enum class SkipType { NONE, LACK_SCHED, PARSE_FAILURE, ANY_FAILURE };

static cl::opt<enum SkipType> SkipUnsupportedInstructions(
    "skip-unsupported-instructions",
    cl::desc("Force analysis to continue in the presence of unsupported "
             "instructions"),
    cl::values(
        clEnumValN(SkipType::NONE, "none",
                   "Exit with an error when an instruction is unsupported for "
                   "any reason (default)"),
        clEnumValN(
            SkipType::LACK_SCHED, "lack-sched",
            "Skip instructions on input which lack scheduling information"),
        clEnumValN(
            SkipType::PARSE_FAILURE, "parse-failure",
            "Skip lines on the input which fail to parse for any reason"),
        clEnumValN(SkipType::ANY_FAILURE, "any",
                   "Skip instructions or lines on input which are unsupported "
                   "for any reason")),
    cl::init(SkipType::NONE), cl::cat(ViewOptions));

bool shouldSkip(enum SkipType skipType) {
  if (SkipUnsupportedInstructions == SkipType::NONE)
    return false;
  if (SkipUnsupportedInstructions == SkipType::ANY_FAILURE)
    return true;
  return skipType == SkipUnsupportedInstructions;
}

static cl::opt<bool>
    PrintRegisterFileStats("register-file-stats",
                           cl::desc("Print register file statistics"),
                           cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> PrintDispatchStats("dispatch-stats",
                                        cl::desc("Print dispatch statistics"),
                                        cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool>
    PrintSummaryView("summary-view", cl::Hidden,
                     cl::desc("Print summary view (enabled by default)"),
                     cl::cat(ViewOptions), cl::init(true));

static cl::opt<bool> PrintSchedulerStats("scheduler-stats",
                                         cl::desc("Print scheduler statistics"),
                                         cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool>
    PrintRetireStats("retire-stats",
                     cl::desc("Print retire control unit statistics"),
                     cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> PrintResourcePressureView(
    "resource-pressure",
    cl::desc("Print the resource pressure view (enabled by default)"),
    cl::cat(ViewOptions), cl::init(true));

static cl::opt<bool> PrintTimelineView("timeline",
                                       cl::desc("Print the timeline view"),
                                       cl::cat(ViewOptions), cl::init(false));

static cl::opt<unsigned> TimelineMaxIterations(
    "timeline-max-iterations",
    cl::desc("Maximum number of iterations to print in timeline view"),
    cl::cat(ViewOptions), cl::init(0));

static cl::opt<unsigned>
    TimelineMaxCycles("timeline-max-cycles",
                      cl::desc("Maximum number of cycles in the timeline view, "
                               "or 0 for unlimited. Defaults to 80 cycles"),
                      cl::cat(ViewOptions), cl::init(80));

static cl::opt<bool>
    AssumeNoAlias("noalias",
                  cl::desc("If set, assume that loads and stores do not alias"),
                  cl::cat(ToolOptions), cl::init(true));

static cl::opt<unsigned> LoadQueueSize("lqueue",
                                       cl::desc("Size of the load queue"),
                                       cl::cat(ToolOptions), cl::init(0));

static cl::opt<unsigned> StoreQueueSize("squeue",
                                        cl::desc("Size of the store queue"),
                                        cl::cat(ToolOptions), cl::init(0));

static cl::opt<bool>
    PrintInstructionTables("instruction-tables",
                           cl::desc("Print instruction tables"),
                           cl::cat(ToolOptions), cl::init(false));

static cl::opt<bool> PrintInstructionInfoView(
    "instruction-info",
    cl::desc("Print the instruction info view (enabled by default)"),
    cl::cat(ViewOptions), cl::init(true));

static cl::opt<bool> EnableAllStats("all-stats",
                                    cl::desc("Print all hardware statistics"),
                                    cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool>
    EnableAllViews("all-views",
                   cl::desc("Print all views including hardware statistics"),
                   cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> EnableBottleneckAnalysis(
    "bottleneck-analysis",
    cl::desc("Enable bottleneck analysis (disabled by default)"),
    cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> ShowEncoding(
    "show-encoding",
    cl::desc("Print encoding information in the instruction info view"),
    cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> ShowBarriers(
    "show-barriers",
    cl::desc("Print memory barrier information in the instruction info view"),
    cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> DisableCustomBehaviour(
    "disable-cb",
    cl::desc(
        "Disable custom behaviour (use the default class which does nothing)."),
    cl::cat(ViewOptions), cl::init(false));

static cl::opt<bool> DisableInstrumentManager(
    "disable-im",
    cl::desc("Disable instrumentation manager (use the default class which "
             "ignores instruments.)."),
    cl::cat(ViewOptions), cl::init(false));

namespace {

const Target *getTarget(const char *ProgName) {
  if (TripleName.empty())
    TripleName = Triple::normalize(sys::getDefaultTargetTriple());
  Triple TheTriple(TripleName);

  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(ArchName, TheTriple, Error);
  if (!TheTarget) {
    errs() << ProgName << ": " << Error;
    return nullptr;
  }

  // Update TripleName with the updated triple from the target lookup.
  TripleName = TheTriple.str();

  // Return the found target.
  return TheTarget;
}

ErrorOr<std::unique_ptr<ToolOutputFile>> getOutputStream() {
  if (OutputFilename == "")
    OutputFilename = "-";
  std::error_code EC;
  auto Out = std::make_unique<ToolOutputFile>(OutputFilename, EC,
                                              sys::fs::OF_TextWithCRLF);
  if (!EC)
    return std::move(Out);
  return EC;
}
} // end of anonymous namespace

static void processOptionImpl(cl::opt<bool> &O, const cl::opt<bool> &Default) {
  if (!O.getNumOccurrences() || O.getPosition() < Default.getPosition())
    O = Default.getValue();
}

static void processViewOptions(bool IsOutOfOrder) {
  if (!EnableAllViews.getNumOccurrences() &&
      !EnableAllStats.getNumOccurrences())
    return;

  if (EnableAllViews.getNumOccurrences()) {
    processOptionImpl(PrintSummaryView, EnableAllViews);
    if (IsOutOfOrder)
      processOptionImpl(EnableBottleneckAnalysis, EnableAllViews);
    processOptionImpl(PrintResourcePressureView, EnableAllViews);
    processOptionImpl(PrintTimelineView, EnableAllViews);
    processOptionImpl(PrintInstructionInfoView, EnableAllViews);
  }

  const cl::opt<bool> &Default =
      EnableAllViews.getPosition() < EnableAllStats.getPosition()
          ? EnableAllStats
          : EnableAllViews;
  processOptionImpl(PrintRegisterFileStats, Default);
  processOptionImpl(PrintDispatchStats, Default);
  processOptionImpl(PrintSchedulerStats, Default);
  if (IsOutOfOrder)
    processOptionImpl(PrintRetireStats, Default);
}

// Returns true on success.
static bool runPipeline(mca::Pipeline &P) {
  // Handle pipeline errors here.
  Expected<unsigned> Cycles = P.run();
  if (!Cycles) {
    WithColor::error() << toString(Cycles.takeError());
    return false;
  }
  return true;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Initialize targets and assembly parsers.
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllTargetMCAs();

  // Register the Target and CPU printer for --version.
  cl::AddExtraVersionPrinter(sys::printDefaultTargetAndDetectedCPU);

  // Enable printing of available targets when flag --version is specified.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  cl::HideUnrelatedOptions({&ToolOptions, &ViewOptions});

  // Parse flags and initialize target options.
  cl::ParseCommandLineOptions(argc, argv,
                              "llvm machine code performance analyzer.\n");

  // Get the target from the triple. If a triple is not specified, then select
  // the default triple for the host. If the triple doesn't correspond to any
  // registered target, then exit with an error message.
  const char *ProgName = argv[0];
  const Target *TheTarget = getTarget(ProgName);
  if (!TheTarget)
    return 1;

  // GetTarget() may replaced TripleName with a default triple.
  // For safety, reconstruct the Triple object.
  Triple TheTriple(TripleName);

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferPtr =
      MemoryBuffer::getFileOrSTDIN(InputFilename);
  if (std::error_code EC = BufferPtr.getError()) {
    WithColor::error() << InputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  if (MCPU == "native")
    MCPU = std::string(llvm::sys::getHostCPUName());

  // Package up features to be passed to target/subtarget
  std::string FeaturesStr;
  if (MATTRS.size()) {
    SubtargetFeatures Features;
    for (std::string &MAttr : MATTRS)
      Features.AddFeature(MAttr);
    FeaturesStr = Features.getString();
  }

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, MCPU, FeaturesStr));
  assert(STI && "Unable to create subtarget info!");
  if (!STI->isCPUStringValid(MCPU))
    return 1;

  if (!STI->getSchedModel().hasInstrSchedModel()) {
    WithColor::error()
        << "unable to find instruction-level scheduling information for"
        << " target triple '" << TheTriple.normalize() << "' and cpu '" << MCPU
        << "'.\n";

    if (STI->getSchedModel().InstrItineraries)
      WithColor::note()
          << "cpu '" << MCPU << "' provides itineraries. However, "
          << "instruction itineraries are currently unsupported.\n";
    return 1;
  }

  // Apply overrides to llvm-mca specific options.
  bool IsOutOfOrder = STI->getSchedModel().isOutOfOrder();
  processViewOptions(IsOutOfOrder);

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  assert(MRI && "Unable to create target register info!");

  MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags();
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  assert(MAI && "Unable to create target asm info!");

  SourceMgr SrcMgr;

  // Tell SrcMgr about this buffer, which is what the parser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(*BufferPtr), SMLoc());

  std::unique_ptr<buffer_ostream> BOS;

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  assert(MCII && "Unable to create instruction info!");

  std::unique_ptr<MCInstrAnalysis> MCIA(
      TheTarget->createMCInstrAnalysis(MCII.get()));

  // Need to initialize an MCInstPrinter as it is
  // required for initializing the MCTargetStreamer
  // which needs to happen within the CRG.parseAnalysisRegions() call below.
  // Without an MCTargetStreamer, certain assembly directives can trigger a
  // segfault. (For example, the .cv_fpo_proc directive on x86 will segfault if
  // we don't initialize the MCTargetStreamer.)
  unsigned IPtempOutputAsmVariant =
      OutputAsmVariant == -1 ? 0 : OutputAsmVariant;
  std::unique_ptr<MCInstPrinter> IPtemp(TheTarget->createMCInstPrinter(
      Triple(TripleName), IPtempOutputAsmVariant, *MAI, *MCII, *MRI));
  if (!IPtemp) {
    WithColor::error()
        << "unable to create instruction printer for target triple '"
        << TheTriple.normalize() << "' with assembly variant "
        << IPtempOutputAsmVariant << ".\n";
    return 1;
  }

  // Parse the input and create CodeRegions that llvm-mca can analyze.
  MCContext ACtx(TheTriple, MAI.get(), MRI.get(), STI.get(), &SrcMgr);
  std::unique_ptr<MCObjectFileInfo> AMOFI(
      TheTarget->createMCObjectFileInfo(ACtx, /*PIC=*/false));
  ACtx.setObjectFileInfo(AMOFI.get());
  mca::AsmAnalysisRegionGenerator CRG(*TheTarget, SrcMgr, ACtx, *MAI, *STI,
                                      *MCII);
  Expected<const mca::AnalysisRegions &> RegionsOrErr =
      CRG.parseAnalysisRegions(std::move(IPtemp),
                               shouldSkip(SkipType::PARSE_FAILURE));
  if (!RegionsOrErr) {
    if (auto Err =
            handleErrors(RegionsOrErr.takeError(), [](const StringError &E) {
              WithColor::error() << E.getMessage() << '\n';
            })) {
      // Default case.
      WithColor::error() << toString(std::move(Err)) << '\n';
    }
    return 1;
  }
  const mca::AnalysisRegions &Regions = *RegionsOrErr;

  // Early exit if errors were found by the code region parsing logic.
  if (!Regions.isValid())
    return 1;

  if (Regions.empty()) {
    WithColor::error() << "no assembly instructions found.\n";
    return 1;
  }

  std::unique_ptr<mca::InstrumentManager> IM;
  if (!DisableInstrumentManager) {
    IM = std::unique_ptr<mca::InstrumentManager>(
        TheTarget->createInstrumentManager(*STI, *MCII));
  }
  if (!IM) {
    // If the target doesn't have its own IM implemented (or the -disable-cb
    // flag is set) then we use the base class (which does nothing).
    IM = std::make_unique<mca::InstrumentManager>(*STI, *MCII);
  }

  // Parse the input and create InstrumentRegion that llvm-mca
  // can use to improve analysis.
  MCContext ICtx(TheTriple, MAI.get(), MRI.get(), STI.get(), &SrcMgr);
  std::unique_ptr<MCObjectFileInfo> IMOFI(
      TheTarget->createMCObjectFileInfo(ICtx, /*PIC=*/false));
  ICtx.setObjectFileInfo(IMOFI.get());
  mca::AsmInstrumentRegionGenerator IRG(*TheTarget, SrcMgr, ICtx, *MAI, *STI,
                                        *MCII, *IM);
  Expected<const mca::InstrumentRegions &> InstrumentRegionsOrErr =
      IRG.parseInstrumentRegions(std::move(IPtemp),
                                 shouldSkip(SkipType::PARSE_FAILURE));
  if (!InstrumentRegionsOrErr) {
    if (auto Err = handleErrors(InstrumentRegionsOrErr.takeError(),
                                [](const StringError &E) {
                                  WithColor::error() << E.getMessage() << '\n';
                                })) {
      // Default case.
      WithColor::error() << toString(std::move(Err)) << '\n';
    }
    return 1;
  }
  const mca::InstrumentRegions &InstrumentRegions = *InstrumentRegionsOrErr;

  // Early exit if errors were found by the instrumentation parsing logic.
  if (!InstrumentRegions.isValid())
    return 1;

  // Now initialize the output file.
  auto OF = getOutputStream();
  if (std::error_code EC = OF.getError()) {
    WithColor::error() << EC.message() << '\n';
    return 1;
  }

  unsigned AssemblerDialect = CRG.getAssemblerDialect();
  if (OutputAsmVariant >= 0)
    AssemblerDialect = static_cast<unsigned>(OutputAsmVariant);
  std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
      Triple(TripleName), AssemblerDialect, *MAI, *MCII, *MRI));
  if (!IP) {
    WithColor::error()
        << "unable to create instruction printer for target triple '"
        << TheTriple.normalize() << "' with assembly variant "
        << AssemblerDialect << ".\n";
    return 1;
  }

  // Set the display preference for hex vs. decimal immediates.
  IP->setPrintImmHex(PrintImmHex);

  std::unique_ptr<ToolOutputFile> TOF = std::move(*OF);

  const MCSchedModel &SM = STI->getSchedModel();

  std::unique_ptr<mca::InstrPostProcess> IPP;
  if (!DisableCustomBehaviour) {
    // TODO: It may be a good idea to separate CB and IPP so that they can
    // be used independently of each other. What I mean by this is to add
    // an extra command-line arg --disable-ipp so that CB and IPP can be
    // toggled without needing to toggle both of them together.
    IPP = std::unique_ptr<mca::InstrPostProcess>(
        TheTarget->createInstrPostProcess(*STI, *MCII));
  }
  if (!IPP) {
    // If the target doesn't have its own IPP implemented (or the -disable-cb
    // flag is set) then we use the base class (which does nothing).
    IPP = std::make_unique<mca::InstrPostProcess>(*STI, *MCII);
  }

  // Create an instruction builder.
  mca::InstrBuilder IB(*STI, *MCII, *MRI, MCIA.get(), *IM, CallLatency);

  // Create a context to control ownership of the pipeline hardware.
  mca::Context MCA(*MRI, *STI);

  mca::PipelineOptions PO(MicroOpQueue, DecoderThroughput, DispatchWidth,
                          RegisterFileSize, LoadQueueSize, StoreQueueSize,
                          AssumeNoAlias, EnableBottleneckAnalysis);

  // Number each region in the sequence.
  unsigned RegionIdx = 0;

  std::unique_ptr<MCCodeEmitter> MCE(
      TheTarget->createMCCodeEmitter(*MCII, ACtx));
  assert(MCE && "Unable to create code emitter!");

  std::unique_ptr<MCAsmBackend> MAB(TheTarget->createMCAsmBackend(
      *STI, *MRI, mc::InitMCTargetOptionsFromFlags()));
  assert(MAB && "Unable to create asm backend!");

  json::Object JSONOutput;
  int NonEmptyRegions = 0;
  for (const std::unique_ptr<mca::AnalysisRegion> &Region : Regions) {
    // Skip empty code regions.
    if (Region->empty())
      continue;

    IB.clear();

    // Lower the MCInst sequence into an mca::Instruction sequence.
    ArrayRef<MCInst> Insts = Region->getInstructions();
    mca::CodeEmitter CE(*STI, *MAB, *MCE, Insts);

    IPP->resetState();

    DenseMap<const MCInst *, SmallVector<mca::Instrument *>> InstToInstruments;
    SmallVector<std::unique_ptr<mca::Instruction>> LoweredSequence;
    SmallPtrSet<const MCInst *, 16> DroppedInsts;
    for (const MCInst &MCI : Insts) {
      SMLoc Loc = MCI.getLoc();
      const SmallVector<mca::Instrument *> Instruments =
          InstrumentRegions.getActiveInstruments(Loc);

      Expected<std::unique_ptr<mca::Instruction>> Inst =
          IB.createInstruction(MCI, Instruments);
      if (!Inst) {
        if (auto NewE = handleErrors(
                Inst.takeError(),
                [&IP, &STI](const mca::InstructionError<MCInst> &IE) {
                  std::string InstructionStr;
                  raw_string_ostream SS(InstructionStr);
                  if (shouldSkip(SkipType::LACK_SCHED))
                    WithColor::warning()
                        << IE.Message
                        << ", skipping with -skip-unsupported-instructions, "
                           "note accuracy will be impacted:\n";
                  else
                    WithColor::error()
                        << IE.Message
                        << ", use -skip-unsupported-instructions=lack-sched to "
                           "ignore these on the input.\n";
                  IP->printInst(&IE.Inst, 0, "", *STI, SS);
                  SS.flush();
                  WithColor::note()
                      << "instruction: " << InstructionStr << '\n';
                })) {
          // Default case.
          WithColor::error() << toString(std::move(NewE));
        }
        if (shouldSkip(SkipType::LACK_SCHED)) {
          DroppedInsts.insert(&MCI);
          continue;
        }
        return 1;
      }

      IPP->postProcessInstruction(Inst.get(), MCI);
      InstToInstruments.insert({&MCI, Instruments});
      LoweredSequence.emplace_back(std::move(Inst.get()));
    }

    Insts = Region->dropInstructions(DroppedInsts);

    // Skip empty regions.
    if (Insts.empty())
      continue;
    NonEmptyRegions++;

    mca::CircularSourceMgr S(LoweredSequence,
                             PrintInstructionTables ? 1 : Iterations);

    if (PrintInstructionTables) {
      //  Create a pipeline, stages, and a printer.
      auto P = std::make_unique<mca::Pipeline>();
      P->appendStage(std::make_unique<mca::EntryStage>(S));
      P->appendStage(std::make_unique<mca::InstructionTables>(SM));

      mca::PipelinePrinter Printer(*P, *Region, RegionIdx, *STI, PO);
      if (PrintJson) {
        Printer.addView(
            std::make_unique<mca::InstructionView>(*STI, *IP, Insts));
      }

      // Create the views for this pipeline, execute, and emit a report.
      if (PrintInstructionInfoView) {
        Printer.addView(std::make_unique<mca::InstructionInfoView>(
            *STI, *MCII, CE, ShowEncoding, Insts, *IP, LoweredSequence,
            ShowBarriers, *IM, InstToInstruments));
      }
      Printer.addView(
          std::make_unique<mca::ResourcePressureView>(*STI, *IP, Insts));

      if (!runPipeline(*P))
        return 1;

      if (PrintJson) {
        Printer.printReport(JSONOutput);
      } else {
        Printer.printReport(TOF->os());
      }

      ++RegionIdx;
      continue;
    }

    // Create the CustomBehaviour object for enforcing Target Specific
    // behaviours and dependencies that aren't expressed well enough
    // in the tablegen. CB cannot depend on the list of MCInst or
    // the source code (but it can depend on the list of
    // mca::Instruction or any objects that can be reconstructed
    // from the target information).
    std::unique_ptr<mca::CustomBehaviour> CB;
    if (!DisableCustomBehaviour)
      CB = std::unique_ptr<mca::CustomBehaviour>(
          TheTarget->createCustomBehaviour(*STI, S, *MCII));
    if (!CB)
      // If the target doesn't have its own CB implemented (or the -disable-cb
      // flag is set) then we use the base class (which does nothing).
      CB = std::make_unique<mca::CustomBehaviour>(*STI, S, *MCII);

    // Create a basic pipeline simulating an out-of-order backend.
    auto P = MCA.createDefaultPipeline(PO, S, *CB);

    mca::PipelinePrinter Printer(*P, *Region, RegionIdx, *STI, PO);

    // Targets can define their own custom Views that exist within their
    // /lib/Target/ directory so that the View can utilize their CustomBehaviour
    // or other backend symbols / functionality that are not already exposed
    // through one of the MC-layer classes. These Views will be initialized
    // using the CustomBehaviour::getViews() variants.
    // If a target makes a custom View that does not depend on their target
    // CB or their backend, they should put the View within
    // /tools/llvm-mca/Views/ instead.
    if (!DisableCustomBehaviour) {
      std::vector<std::unique_ptr<mca::View>> CBViews =
          CB->getStartViews(*IP, Insts);
      for (auto &CBView : CBViews)
        Printer.addView(std::move(CBView));
    }

    // When we output JSON, we add a view that contains the instructions
    // and CPU resource information.
    if (PrintJson) {
      auto IV = std::make_unique<mca::InstructionView>(*STI, *IP, Insts);
      Printer.addView(std::move(IV));
    }

    if (PrintSummaryView)
      Printer.addView(
          std::make_unique<mca::SummaryView>(SM, Insts, DispatchWidth));

    if (EnableBottleneckAnalysis) {
      if (!IsOutOfOrder) {
        WithColor::warning()
            << "bottleneck analysis is not supported for in-order CPU '" << MCPU
            << "'.\n";
      }
      Printer.addView(std::make_unique<mca::BottleneckAnalysis>(
          *STI, *IP, Insts, S.getNumIterations()));
    }

    if (PrintInstructionInfoView)
      Printer.addView(std::make_unique<mca::InstructionInfoView>(
          *STI, *MCII, CE, ShowEncoding, Insts, *IP, LoweredSequence,
          ShowBarriers, *IM, InstToInstruments));

    // Fetch custom Views that are to be placed after the InstructionInfoView.
    // Refer to the comment paired with the CB->getStartViews(*IP, Insts); line
    // for more info.
    if (!DisableCustomBehaviour) {
      std::vector<std::unique_ptr<mca::View>> CBViews =
          CB->getPostInstrInfoViews(*IP, Insts);
      for (auto &CBView : CBViews)
        Printer.addView(std::move(CBView));
    }

    if (PrintDispatchStats)
      Printer.addView(std::make_unique<mca::DispatchStatistics>());

    if (PrintSchedulerStats)
      Printer.addView(std::make_unique<mca::SchedulerStatistics>(*STI));

    if (PrintRetireStats)
      Printer.addView(std::make_unique<mca::RetireControlUnitStatistics>(SM));

    if (PrintRegisterFileStats)
      Printer.addView(std::make_unique<mca::RegisterFileStatistics>(*STI));

    if (PrintResourcePressureView)
      Printer.addView(
          std::make_unique<mca::ResourcePressureView>(*STI, *IP, Insts));

    if (PrintTimelineView) {
      unsigned TimelineIterations =
          TimelineMaxIterations ? TimelineMaxIterations : 10;
      Printer.addView(std::make_unique<mca::TimelineView>(
          *STI, *IP, Insts, std::min(TimelineIterations, S.getNumIterations()),
          TimelineMaxCycles));
    }

    // Fetch custom Views that are to be placed after all other Views.
    // Refer to the comment paired with the CB->getStartViews(*IP, Insts); line
    // for more info.
    if (!DisableCustomBehaviour) {
      std::vector<std::unique_ptr<mca::View>> CBViews =
          CB->getEndViews(*IP, Insts);
      for (auto &CBView : CBViews)
        Printer.addView(std::move(CBView));
    }

    if (!runPipeline(*P))
      return 1;

    if (PrintJson) {
      Printer.printReport(JSONOutput);
    } else {
      Printer.printReport(TOF->os());
    }

    ++RegionIdx;
  }

  if (NonEmptyRegions == 0) {
    WithColor::error() << "no assembly instructions found.\n";
    return 1;
  }

  if (PrintJson)
    TOF->os() << formatv("{0:2}", json::Value(std::move(JSONOutput))) << "\n";

  TOF->keep();
  return 0;
}
