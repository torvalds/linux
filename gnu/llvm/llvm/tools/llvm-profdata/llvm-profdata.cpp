//===- llvm-profdata.cpp - LLVM profile data tool -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-profdata merges .profdata files.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/ProfileData/InstrProfCorrelator.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/ProfileData/InstrProfWriter.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/ProfileData/MemProfReader.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/ProfileData/SampleProfReader.h"
#include "llvm/ProfileData/SampleProfWriter.h"
#include "llvm/Support/BalancedPartitioning.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Discriminator.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <queue>

using namespace llvm;
using ProfCorrelatorKind = InstrProfCorrelator::ProfCorrelatorKind;

// https://llvm.org/docs/CommandGuide/llvm-profdata.html has documentations
// on each subcommand.
cl::SubCommand ShowSubcommand(
    "show",
    "Takes a profile data file and displays the profiles. See detailed "
    "documentation in "
    "https://llvm.org/docs/CommandGuide/llvm-profdata.html#profdata-show");
cl::SubCommand OrderSubcommand(
    "order",
    "Reads temporal profiling traces from a profile and outputs a function "
    "order that reduces the number of page faults for those traces. See "
    "detailed documentation in "
    "https://llvm.org/docs/CommandGuide/llvm-profdata.html#profdata-order");
cl::SubCommand OverlapSubcommand(
    "overlap",
    "Computes and displays the overlap between two profiles. See detailed "
    "documentation in "
    "https://llvm.org/docs/CommandGuide/llvm-profdata.html#profdata-overlap");
cl::SubCommand MergeSubcommand(
    "merge",
    "Takes several profiles and merge them together. See detailed "
    "documentation in "
    "https://llvm.org/docs/CommandGuide/llvm-profdata.html#profdata-merge");

namespace {
enum ProfileKinds { instr, sample, memory };
enum FailureMode { warnOnly, failIfAnyAreInvalid, failIfAllAreInvalid };

enum ProfileFormat {
  PF_None = 0,
  PF_Text,
  PF_Compact_Binary, // Deprecated
  PF_Ext_Binary,
  PF_GCC,
  PF_Binary
};

enum class ShowFormat { Text, Json, Yaml };
} // namespace

// Common options.
cl::opt<std::string> OutputFilename("output", cl::value_desc("output"),
                                    cl::init("-"), cl::desc("Output file"),
                                    cl::sub(ShowSubcommand),
                                    cl::sub(OrderSubcommand),
                                    cl::sub(OverlapSubcommand),
                                    cl::sub(MergeSubcommand));
// NOTE: cl::alias must not have cl::sub(), since aliased option's cl::sub()
// will be used. llvm::cl::alias::done() method asserts this condition.
cl::alias OutputFilenameA("o", cl::desc("Alias for --output"),
                          cl::aliasopt(OutputFilename));

// Options common to at least two commands.
cl::opt<ProfileKinds> ProfileKind(
    cl::desc("Profile kind:"), cl::sub(MergeSubcommand),
    cl::sub(OverlapSubcommand), cl::init(instr),
    cl::values(clEnumVal(instr, "Instrumentation profile (default)"),
               clEnumVal(sample, "Sample profile")));
cl::opt<std::string> Filename(cl::Positional, cl::desc("<profdata-file>"),
                              cl::sub(ShowSubcommand),
                              cl::sub(OrderSubcommand));
cl::opt<unsigned> MaxDbgCorrelationWarnings(
    "max-debug-info-correlation-warnings",
    cl::desc("The maximum number of warnings to emit when correlating "
             "profile from debug info (0 = no limit)"),
    cl::sub(MergeSubcommand), cl::sub(ShowSubcommand), cl::init(5));
cl::opt<std::string> ProfiledBinary(
    "profiled-binary", cl::init(""),
    cl::desc("Path to binary from which the profile was collected."),
    cl::sub(ShowSubcommand), cl::sub(MergeSubcommand));
cl::opt<std::string> DebugInfoFilename(
    "debug-info", cl::init(""),
    cl::desc(
        "For show, read and extract profile metadata from debug info and show "
        "the functions it found. For merge, use the provided debug info to "
        "correlate the raw profile."),
    cl::sub(ShowSubcommand), cl::sub(MergeSubcommand));
cl::opt<std::string>
    BinaryFilename("binary-file", cl::init(""),
                   cl::desc("For merge, use the provided unstripped bianry to "
                            "correlate the raw profile."),
                   cl::sub(MergeSubcommand));
cl::opt<std::string> FuncNameFilter(
    "function",
    cl::desc("Only functions matching the filter are shown in the output. For "
             "overlapping CSSPGO, this takes a function name with calling "
             "context."),
    cl::sub(ShowSubcommand), cl::sub(OverlapSubcommand),
    cl::sub(MergeSubcommand));

// TODO: Consider creating a template class (e.g., MergeOption, ShowOption) to
// factor out the common cl::sub in cl::opt constructor for subcommand-specific
// options.

// Options specific to merge subcommand.
cl::list<std::string> InputFilenames(cl::Positional, cl::sub(MergeSubcommand),
                                     cl::desc("<filename...>"));
cl::list<std::string> WeightedInputFilenames("weighted-input",
                                             cl::sub(MergeSubcommand),
                                             cl::desc("<weight>,<filename>"));
cl::opt<ProfileFormat> OutputFormat(
    cl::desc("Format of output profile"), cl::sub(MergeSubcommand),
    cl::init(PF_Ext_Binary),
    cl::values(clEnumValN(PF_Binary, "binary", "Binary encoding"),
               clEnumValN(PF_Ext_Binary, "extbinary",
                          "Extensible binary encoding "
                          "(default)"),
               clEnumValN(PF_Text, "text", "Text encoding"),
               clEnumValN(PF_GCC, "gcc",
                          "GCC encoding (only meaningful for -sample)")));
cl::opt<std::string>
    InputFilenamesFile("input-files", cl::init(""), cl::sub(MergeSubcommand),
                       cl::desc("Path to file containing newline-separated "
                                "[<weight>,]<filename> entries"));
cl::alias InputFilenamesFileA("f", cl::desc("Alias for --input-files"),
                              cl::aliasopt(InputFilenamesFile));
cl::opt<bool> DumpInputFileList(
    "dump-input-file-list", cl::init(false), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("Dump the list of input files and their weights, then exit"));
cl::opt<std::string> RemappingFile("remapping-file", cl::value_desc("file"),
                                   cl::sub(MergeSubcommand),
                                   cl::desc("Symbol remapping file"));
cl::alias RemappingFileA("r", cl::desc("Alias for --remapping-file"),
                         cl::aliasopt(RemappingFile));
cl::opt<bool>
    UseMD5("use-md5", cl::init(false), cl::Hidden,
           cl::desc("Choose to use MD5 to represent string in name table (only "
                    "meaningful for -extbinary)"),
           cl::sub(MergeSubcommand));
cl::opt<bool> CompressAllSections(
    "compress-all-sections", cl::init(false), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("Compress all sections when writing the profile (only "
             "meaningful for -extbinary)"));
cl::opt<bool> SampleMergeColdContext(
    "sample-merge-cold-context", cl::init(false), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc(
        "Merge context sample profiles whose count is below cold threshold"));
cl::opt<bool> SampleTrimColdContext(
    "sample-trim-cold-context", cl::init(false), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc(
        "Trim context sample profiles whose count is below cold threshold"));
cl::opt<uint32_t> SampleColdContextFrameDepth(
    "sample-frame-depth-for-cold-context", cl::init(1),
    cl::sub(MergeSubcommand),
    cl::desc("Keep the last K frames while merging cold profile. 1 means the "
             "context-less base profile"));
cl::opt<size_t> OutputSizeLimit(
    "output-size-limit", cl::init(0), cl::Hidden, cl::sub(MergeSubcommand),
    cl::desc("Trim cold functions until profile size is below specified "
             "limit in bytes. This uses a heursitic and functions may be "
             "excessively trimmed"));
cl::opt<bool> GenPartialProfile(
    "gen-partial-profile", cl::init(false), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("Generate a partial profile (only meaningful for -extbinary)"));
cl::opt<std::string> SupplInstrWithSample(
    "supplement-instr-with-sample", cl::init(""), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("Supplement an instr profile with sample profile, to correct "
             "the profile unrepresentativeness issue. The sample "
             "profile is the input of the flag. Output will be in instr "
             "format (The flag only works with -instr)"));
cl::opt<float> ZeroCounterThreshold(
    "zero-counter-threshold", cl::init(0.7), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("For the function which is cold in instr profile but hot in "
             "sample profile, if the ratio of the number of zero counters "
             "divided by the total number of counters is above the "
             "threshold, the profile of the function will be regarded as "
             "being harmful for performance and will be dropped."));
cl::opt<unsigned> SupplMinSizeThreshold(
    "suppl-min-size-threshold", cl::init(10), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("If the size of a function is smaller than the threshold, "
             "assume it can be inlined by PGO early inliner and it won't "
             "be adjusted based on sample profile."));
cl::opt<unsigned> InstrProfColdThreshold(
    "instr-prof-cold-threshold", cl::init(0), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("User specified cold threshold for instr profile which will "
             "override the cold threshold got from profile summary. "));
// WARNING: This reservoir size value is propagated to any input indexed
// profiles for simplicity. Changing this value between invocations could
// result in sample bias.
cl::opt<uint64_t> TemporalProfTraceReservoirSize(
    "temporal-profile-trace-reservoir-size", cl::init(100),
    cl::sub(MergeSubcommand),
    cl::desc("The maximum number of stored temporal profile traces (default: "
             "100)"));
cl::opt<uint64_t> TemporalProfMaxTraceLength(
    "temporal-profile-max-trace-length", cl::init(10000),
    cl::sub(MergeSubcommand),
    cl::desc("The maximum length of a single temporal profile trace "
             "(default: 10000)"));
cl::opt<std::string> FuncNameNegativeFilter(
    "no-function", cl::init(""),
    cl::sub(MergeSubcommand),
    cl::desc("Exclude functions matching the filter from the output."));

cl::opt<FailureMode>
    FailMode("failure-mode", cl::init(failIfAnyAreInvalid),
             cl::desc("Failure mode:"), cl::sub(MergeSubcommand),
             cl::values(clEnumValN(warnOnly, "warn",
                                   "Do not fail and just print warnings."),
                        clEnumValN(failIfAnyAreInvalid, "any",
                                   "Fail if any profile is invalid."),
                        clEnumValN(failIfAllAreInvalid, "all",
                                   "Fail only if all profiles are invalid.")));

cl::opt<bool> OutputSparse(
    "sparse", cl::init(false), cl::sub(MergeSubcommand),
    cl::desc("Generate a sparse profile (only meaningful for -instr)"));
cl::opt<unsigned> NumThreads(
    "num-threads", cl::init(0), cl::sub(MergeSubcommand),
    cl::desc("Number of merge threads to use (default: autodetect)"));
cl::alias NumThreadsA("j", cl::desc("Alias for --num-threads"),
                      cl::aliasopt(NumThreads));

cl::opt<std::string> ProfileSymbolListFile(
    "prof-sym-list", cl::init(""), cl::sub(MergeSubcommand),
    cl::desc("Path to file containing the list of function symbols "
             "used to populate profile symbol list"));

cl::opt<SampleProfileLayout> ProfileLayout(
    "convert-sample-profile-layout",
    cl::desc("Convert the generated profile to a profile with a new layout"),
    cl::sub(MergeSubcommand), cl::init(SPL_None),
    cl::values(
        clEnumValN(SPL_Nest, "nest",
                   "Nested profile, the input should be CS flat profile"),
        clEnumValN(SPL_Flat, "flat",
                   "Profile with nested inlinee flatten out")));

cl::opt<bool> DropProfileSymbolList(
    "drop-profile-symbol-list", cl::init(false), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("Drop the profile symbol list when merging AutoFDO profiles "
             "(only meaningful for -sample)"));

cl::opt<bool> KeepVTableSymbols(
    "keep-vtable-symbols", cl::init(false), cl::Hidden,
    cl::sub(MergeSubcommand),
    cl::desc("If true, keep the vtable symbols in indexed profiles"));

// Temporary support for writing the previous version of the format, to enable
// some forward compatibility.
// TODO: Consider enabling this with future version changes as well, to ease
// deployment of newer versions of llvm-profdata.
cl::opt<bool> DoWritePrevVersion(
    "write-prev-version", cl::init(false), cl::Hidden,
    cl::desc("Write the previous version of indexed format, to enable "
             "some forward compatibility."));

cl::opt<memprof::IndexedVersion> MemProfVersionRequested(
    "memprof-version", cl::Hidden, cl::sub(MergeSubcommand),
    cl::desc("Specify the version of the memprof format to use"),
    cl::init(memprof::Version0),
    cl::values(clEnumValN(memprof::Version0, "0", "version 0"),
               clEnumValN(memprof::Version1, "1", "version 1"),
               clEnumValN(memprof::Version2, "2", "version 2"),
               clEnumValN(memprof::Version3, "3", "version 3")));

cl::opt<bool> MemProfFullSchema(
    "memprof-full-schema", cl::Hidden, cl::sub(MergeSubcommand),
    cl::desc("Use the full schema for serialization"), cl::init(false));

// Options specific to overlap subcommand.
cl::opt<std::string> BaseFilename(cl::Positional, cl::Required,
                                  cl::desc("<base profile file>"),
                                  cl::sub(OverlapSubcommand));
cl::opt<std::string> TestFilename(cl::Positional, cl::Required,
                                  cl::desc("<test profile file>"),
                                  cl::sub(OverlapSubcommand));

cl::opt<unsigned long long> SimilarityCutoff(
    "similarity-cutoff", cl::init(0),
    cl::desc("For sample profiles, list function names (with calling context "
             "for csspgo) for overlapped functions "
             "with similarities below the cutoff (percentage times 10000)."),
    cl::sub(OverlapSubcommand));

cl::opt<bool> IsCS(
    "cs", cl::init(false),
    cl::desc("For context sensitive PGO counts. Does not work with CSSPGO."),
    cl::sub(OverlapSubcommand));

cl::opt<unsigned long long> OverlapValueCutoff(
    "value-cutoff", cl::init(-1),
    cl::desc(
        "Function level overlap information for every function (with calling "
        "context for csspgo) in test "
        "profile with max count value greater then the parameter value"),
    cl::sub(OverlapSubcommand));

// Options specific to show subcommand.
cl::opt<bool> ShowCounts("counts", cl::init(false),
                         cl::desc("Show counter values for shown functions"),
                         cl::sub(ShowSubcommand));
cl::opt<ShowFormat>
    SFormat("show-format", cl::init(ShowFormat::Text),
            cl::desc("Emit output in the selected format if supported"),
            cl::sub(ShowSubcommand),
            cl::values(clEnumValN(ShowFormat::Text, "text",
                                  "emit normal text output (default)"),
                       clEnumValN(ShowFormat::Json, "json", "emit JSON"),
                       clEnumValN(ShowFormat::Yaml, "yaml", "emit YAML")));
// TODO: Consider replacing this with `--show-format=text-encoding`.
cl::opt<bool>
    TextFormat("text", cl::init(false),
               cl::desc("Show instr profile data in text dump format"),
               cl::sub(ShowSubcommand));
cl::opt<bool>
    JsonFormat("json",
               cl::desc("Show sample profile data in the JSON format "
                        "(deprecated, please use --show-format=json)"),
               cl::sub(ShowSubcommand));
cl::opt<bool> ShowIndirectCallTargets(
    "ic-targets", cl::init(false),
    cl::desc("Show indirect call site target values for shown functions"),
    cl::sub(ShowSubcommand));
cl::opt<bool> ShowVTables("show-vtables", cl::init(false),
                          cl::desc("Show vtable names for shown functions"),
                          cl::sub(ShowSubcommand));
cl::opt<bool> ShowMemOPSizes(
    "memop-sizes", cl::init(false),
    cl::desc("Show the profiled sizes of the memory intrinsic calls "
             "for shown functions"),
    cl::sub(ShowSubcommand));
cl::opt<bool> ShowDetailedSummary("detailed-summary", cl::init(false),
                                  cl::desc("Show detailed profile summary"),
                                  cl::sub(ShowSubcommand));
cl::list<uint32_t> DetailedSummaryCutoffs(
    cl::CommaSeparated, "detailed-summary-cutoffs",
    cl::desc(
        "Cutoff percentages (times 10000) for generating detailed summary"),
    cl::value_desc("800000,901000,999999"), cl::sub(ShowSubcommand));
cl::opt<bool>
    ShowHotFuncList("hot-func-list", cl::init(false),
                    cl::desc("Show profile summary of a list of hot functions"),
                    cl::sub(ShowSubcommand));
cl::opt<bool> ShowAllFunctions("all-functions", cl::init(false),
                               cl::desc("Details for each and every function"),
                               cl::sub(ShowSubcommand));
cl::opt<bool> ShowCS("showcs", cl::init(false),
                     cl::desc("Show context sensitive counts"),
                     cl::sub(ShowSubcommand));
cl::opt<ProfileKinds> ShowProfileKind(
    cl::desc("Profile kind supported by show:"), cl::sub(ShowSubcommand),
    cl::init(instr),
    cl::values(clEnumVal(instr, "Instrumentation profile (default)"),
               clEnumVal(sample, "Sample profile"),
               clEnumVal(memory, "MemProf memory access profile")));
cl::opt<uint32_t> TopNFunctions(
    "topn", cl::init(0),
    cl::desc("Show the list of functions with the largest internal counts"),
    cl::sub(ShowSubcommand));
cl::opt<uint32_t> ShowValueCutoff(
    "value-cutoff", cl::init(0),
    cl::desc("Set the count value cutoff. Functions with the maximum count "
             "less than this value will not be printed out. (Default is 0)"),
    cl::sub(ShowSubcommand));
cl::opt<bool> OnlyListBelow(
    "list-below-cutoff", cl::init(false),
    cl::desc("Only output names of functions whose max count values are "
             "below the cutoff value"),
    cl::sub(ShowSubcommand));
cl::opt<bool> ShowProfileSymbolList(
    "show-prof-sym-list", cl::init(false),
    cl::desc("Show profile symbol list if it exists in the profile. "),
    cl::sub(ShowSubcommand));
cl::opt<bool> ShowSectionInfoOnly(
    "show-sec-info-only", cl::init(false),
    cl::desc("Show the information of each section in the sample profile. "
             "The flag is only usable when the sample profile is in "
             "extbinary format"),
    cl::sub(ShowSubcommand));
cl::opt<bool> ShowBinaryIds("binary-ids", cl::init(false),
                            cl::desc("Show binary ids in the profile. "),
                            cl::sub(ShowSubcommand));
cl::opt<bool> ShowTemporalProfTraces(
    "temporal-profile-traces",
    cl::desc("Show temporal profile traces in the profile."),
    cl::sub(ShowSubcommand));

cl::opt<bool>
    ShowCovered("covered", cl::init(false),
                cl::desc("Show only the functions that have been executed."),
                cl::sub(ShowSubcommand));

cl::opt<bool> ShowProfileVersion("profile-version", cl::init(false),
                                 cl::desc("Show profile version. "),
                                 cl::sub(ShowSubcommand));

// Options specific to order subcommand.
cl::opt<unsigned>
    NumTestTraces("num-test-traces", cl::init(0),
                  cl::desc("Keep aside the last <num-test-traces> traces in "
                           "the profile when computing the function order and "
                           "instead use them to evaluate that order"),
                  cl::sub(OrderSubcommand));

// We use this string to indicate that there are
// multiple static functions map to the same name.
const std::string DuplicateNameStr = "----";

static void warn(Twine Message, StringRef Whence = "", StringRef Hint = "") {
  WithColor::warning();
  if (!Whence.empty())
    errs() << Whence << ": ";
  errs() << Message << "\n";
  if (!Hint.empty())
    WithColor::note() << Hint << "\n";
}

static void warn(Error E, StringRef Whence = "") {
  if (E.isA<InstrProfError>()) {
    handleAllErrors(std::move(E), [&](const InstrProfError &IPE) {
      warn(IPE.message(), Whence);
    });
  }
}

static void exitWithError(Twine Message, StringRef Whence = "",
                          StringRef Hint = "") {
  WithColor::error();
  if (!Whence.empty())
    errs() << Whence << ": ";
  errs() << Message << "\n";
  if (!Hint.empty())
    WithColor::note() << Hint << "\n";
  ::exit(1);
}

static void exitWithError(Error E, StringRef Whence = "") {
  if (E.isA<InstrProfError>()) {
    handleAllErrors(std::move(E), [&](const InstrProfError &IPE) {
      instrprof_error instrError = IPE.get();
      StringRef Hint = "";
      if (instrError == instrprof_error::unrecognized_format) {
        // Hint in case user missed specifying the profile type.
        Hint = "Perhaps you forgot to use the --sample or --memory option?";
      }
      exitWithError(IPE.message(), Whence, Hint);
    });
    return;
  }

  exitWithError(toString(std::move(E)), Whence);
}

static void exitWithErrorCode(std::error_code EC, StringRef Whence = "") {
  exitWithError(EC.message(), Whence);
}

static void warnOrExitGivenError(FailureMode FailMode, std::error_code EC,
                                 StringRef Whence = "") {
  if (FailMode == failIfAnyAreInvalid)
    exitWithErrorCode(EC, Whence);
  else
    warn(EC.message(), Whence);
}

static void handleMergeWriterError(Error E, StringRef WhenceFile = "",
                                   StringRef WhenceFunction = "",
                                   bool ShowHint = true) {
  if (!WhenceFile.empty())
    errs() << WhenceFile << ": ";
  if (!WhenceFunction.empty())
    errs() << WhenceFunction << ": ";

  auto IPE = instrprof_error::success;
  E = handleErrors(std::move(E),
                   [&IPE](std::unique_ptr<InstrProfError> E) -> Error {
                     IPE = E->get();
                     return Error(std::move(E));
                   });
  errs() << toString(std::move(E)) << "\n";

  if (ShowHint) {
    StringRef Hint = "";
    if (IPE != instrprof_error::success) {
      switch (IPE) {
      case instrprof_error::hash_mismatch:
      case instrprof_error::count_mismatch:
      case instrprof_error::value_site_count_mismatch:
        Hint = "Make sure that all profile data to be merged is generated "
               "from the same binary.";
        break;
      default:
        break;
      }
    }

    if (!Hint.empty())
      errs() << Hint << "\n";
  }
}

namespace {
/// A remapper from original symbol names to new symbol names based on a file
/// containing a list of mappings from old name to new name.
class SymbolRemapper {
  std::unique_ptr<MemoryBuffer> File;
  DenseMap<StringRef, StringRef> RemappingTable;

public:
  /// Build a SymbolRemapper from a file containing a list of old/new symbols.
  static std::unique_ptr<SymbolRemapper> create(StringRef InputFile) {
    auto BufOrError = MemoryBuffer::getFileOrSTDIN(InputFile);
    if (!BufOrError)
      exitWithErrorCode(BufOrError.getError(), InputFile);

    auto Remapper = std::make_unique<SymbolRemapper>();
    Remapper->File = std::move(BufOrError.get());

    for (line_iterator LineIt(*Remapper->File, /*SkipBlanks=*/true, '#');
         !LineIt.is_at_eof(); ++LineIt) {
      std::pair<StringRef, StringRef> Parts = LineIt->split(' ');
      if (Parts.first.empty() || Parts.second.empty() ||
          Parts.second.count(' ')) {
        exitWithError("unexpected line in remapping file",
                      (InputFile + ":" + Twine(LineIt.line_number())).str(),
                      "expected 'old_symbol new_symbol'");
      }
      Remapper->RemappingTable.insert(Parts);
    }
    return Remapper;
  }

  /// Attempt to map the given old symbol into a new symbol.
  ///
  /// \return The new symbol, or \p Name if no such symbol was found.
  StringRef operator()(StringRef Name) {
    StringRef New = RemappingTable.lookup(Name);
    return New.empty() ? Name : New;
  }

  FunctionId operator()(FunctionId Name) {
    // MD5 name cannot be remapped.
    if (!Name.isStringRef())
      return Name;
    StringRef New = RemappingTable.lookup(Name.stringRef());
    return New.empty() ? Name : FunctionId(New);
  }
};
}

struct WeightedFile {
  std::string Filename;
  uint64_t Weight;
};
typedef SmallVector<WeightedFile, 5> WeightedFileVector;

/// Keep track of merged data and reported errors.
struct WriterContext {
  std::mutex Lock;
  InstrProfWriter Writer;
  std::vector<std::pair<Error, std::string>> Errors;
  std::mutex &ErrLock;
  SmallSet<instrprof_error, 4> &WriterErrorCodes;

  WriterContext(bool IsSparse, std::mutex &ErrLock,
                SmallSet<instrprof_error, 4> &WriterErrorCodes,
                uint64_t ReservoirSize = 0, uint64_t MaxTraceLength = 0)
      : Writer(IsSparse, ReservoirSize, MaxTraceLength, DoWritePrevVersion,
               MemProfVersionRequested, MemProfFullSchema),
        ErrLock(ErrLock), WriterErrorCodes(WriterErrorCodes) {}
};

/// Computer the overlap b/w profile BaseFilename and TestFileName,
/// and store the program level result to Overlap.
static void overlapInput(const std::string &BaseFilename,
                         const std::string &TestFilename, WriterContext *WC,
                         OverlapStats &Overlap,
                         const OverlapFuncFilters &FuncFilter,
                         raw_fd_ostream &OS, bool IsCS) {
  auto FS = vfs::getRealFileSystem();
  auto ReaderOrErr = InstrProfReader::create(TestFilename, *FS);
  if (Error E = ReaderOrErr.takeError()) {
    // Skip the empty profiles by returning sliently.
    auto [ErrorCode, Msg] = InstrProfError::take(std::move(E));
    if (ErrorCode != instrprof_error::empty_raw_profile)
      WC->Errors.emplace_back(make_error<InstrProfError>(ErrorCode, Msg),
                              TestFilename);
    return;
  }

  auto Reader = std::move(ReaderOrErr.get());
  for (auto &I : *Reader) {
    OverlapStats FuncOverlap(OverlapStats::FunctionLevel);
    FuncOverlap.setFuncInfo(I.Name, I.Hash);

    WC->Writer.overlapRecord(std::move(I), Overlap, FuncOverlap, FuncFilter);
    FuncOverlap.dump(OS);
  }
}

/// Load an input into a writer context.
static void loadInput(const WeightedFile &Input, SymbolRemapper *Remapper,
                      const InstrProfCorrelator *Correlator,
                      const StringRef ProfiledBinary, WriterContext *WC) {
  std::unique_lock<std::mutex> CtxGuard{WC->Lock};

  // Copy the filename, because llvm::ThreadPool copied the input "const
  // WeightedFile &" by value, making a reference to the filename within it
  // invalid outside of this packaged task.
  std::string Filename = Input.Filename;

  using ::llvm::memprof::RawMemProfReader;
  if (RawMemProfReader::hasFormat(Input.Filename)) {
    auto ReaderOrErr = RawMemProfReader::create(Input.Filename, ProfiledBinary);
    if (!ReaderOrErr) {
      exitWithError(ReaderOrErr.takeError(), Input.Filename);
    }
    std::unique_ptr<RawMemProfReader> Reader = std::move(ReaderOrErr.get());
    // Check if the profile types can be merged, e.g. clang frontend profiles
    // should not be merged with memprof profiles.
    if (Error E = WC->Writer.mergeProfileKind(Reader->getProfileKind())) {
      consumeError(std::move(E));
      WC->Errors.emplace_back(
          make_error<StringError>(
              "Cannot merge MemProf profile with Clang generated profile.",
              std::error_code()),
          Filename);
      return;
    }

    auto MemProfError = [&](Error E) {
      auto [ErrorCode, Msg] = InstrProfError::take(std::move(E));
      WC->Errors.emplace_back(make_error<InstrProfError>(ErrorCode, Msg),
                              Filename);
    };

    // Add the frame mappings into the writer context.
    const auto &IdToFrame = Reader->getFrameMapping();
    for (const auto &I : IdToFrame) {
      bool Succeeded = WC->Writer.addMemProfFrame(
          /*Id=*/I.first, /*Frame=*/I.getSecond(), MemProfError);
      // If we weren't able to add the frame mappings then it doesn't make sense
      // to try to add the records from this profile.
      if (!Succeeded)
        return;
    }

    // Add the call stacks into the writer context.
    const auto &CSIdToCallStacks = Reader->getCallStacks();
    for (const auto &I : CSIdToCallStacks) {
      bool Succeeded = WC->Writer.addMemProfCallStack(
          /*Id=*/I.first, /*Frame=*/I.getSecond(), MemProfError);
      // If we weren't able to add the call stacks then it doesn't make sense
      // to try to add the records from this profile.
      if (!Succeeded)
        return;
    }

    const auto &FunctionProfileData = Reader->getProfileData();
    // Add the memprof records into the writer context.
    for (const auto &[GUID, Record] : FunctionProfileData) {
      WC->Writer.addMemProfRecord(GUID, Record);
    }
    return;
  }

  auto FS = vfs::getRealFileSystem();
  // TODO: This only saves the first non-fatal error from InstrProfReader, and
  // then added to WriterContext::Errors. However, this is not extensible, if
  // we have more non-fatal errors from InstrProfReader in the future. How
  // should this interact with different -failure-mode?
  std::optional<std::pair<Error, std::string>> ReaderWarning;
  auto Warn = [&](Error E) {
    if (ReaderWarning) {
      consumeError(std::move(E));
      return;
    }
    // Only show the first time an error occurs in this file.
    auto [ErrCode, Msg] = InstrProfError::take(std::move(E));
    ReaderWarning = {make_error<InstrProfError>(ErrCode, Msg), Filename};
  };
  auto ReaderOrErr =
      InstrProfReader::create(Input.Filename, *FS, Correlator, Warn);
  if (Error E = ReaderOrErr.takeError()) {
    // Skip the empty profiles by returning silently.
    auto [ErrCode, Msg] = InstrProfError::take(std::move(E));
    if (ErrCode != instrprof_error::empty_raw_profile)
      WC->Errors.emplace_back(make_error<InstrProfError>(ErrCode, Msg),
                              Filename);
    return;
  }

  auto Reader = std::move(ReaderOrErr.get());
  if (Error E = WC->Writer.mergeProfileKind(Reader->getProfileKind())) {
    consumeError(std::move(E));
    WC->Errors.emplace_back(
        make_error<StringError>(
            "Merge IR generated profile with Clang generated profile.",
            std::error_code()),
        Filename);
    return;
  }

  for (auto &I : *Reader) {
    if (Remapper)
      I.Name = (*Remapper)(I.Name);
    const StringRef FuncName = I.Name;
    bool Reported = false;
    WC->Writer.addRecord(std::move(I), Input.Weight, [&](Error E) {
      if (Reported) {
        consumeError(std::move(E));
        return;
      }
      Reported = true;
      // Only show hint the first time an error occurs.
      auto [ErrCode, Msg] = InstrProfError::take(std::move(E));
      std::unique_lock<std::mutex> ErrGuard{WC->ErrLock};
      bool firstTime = WC->WriterErrorCodes.insert(ErrCode).second;
      handleMergeWriterError(make_error<InstrProfError>(ErrCode, Msg),
                             Input.Filename, FuncName, firstTime);
    });
  }

  if (KeepVTableSymbols) {
    const InstrProfSymtab &symtab = Reader->getSymtab();
    const auto &VTableNames = symtab.getVTableNames();

    for (const auto &kv : VTableNames)
      WC->Writer.addVTableName(kv.getKey());
  }

  if (Reader->hasTemporalProfile()) {
    auto &Traces = Reader->getTemporalProfTraces(Input.Weight);
    if (!Traces.empty())
      WC->Writer.addTemporalProfileTraces(
          Traces, Reader->getTemporalProfTraceStreamSize());
  }
  if (Reader->hasError()) {
    if (Error E = Reader->getError()) {
      WC->Errors.emplace_back(std::move(E), Filename);
      return;
    }
  }

  std::vector<llvm::object::BuildID> BinaryIds;
  if (Error E = Reader->readBinaryIds(BinaryIds)) {
    WC->Errors.emplace_back(std::move(E), Filename);
    return;
  }
  WC->Writer.addBinaryIds(BinaryIds);

  if (ReaderWarning) {
    WC->Errors.emplace_back(std::move(ReaderWarning->first),
                            ReaderWarning->second);
  }
}

/// Merge the \p Src writer context into \p Dst.
static void mergeWriterContexts(WriterContext *Dst, WriterContext *Src) {
  for (auto &ErrorPair : Src->Errors)
    Dst->Errors.push_back(std::move(ErrorPair));
  Src->Errors.clear();

  if (Error E = Dst->Writer.mergeProfileKind(Src->Writer.getProfileKind()))
    exitWithError(std::move(E));

  Dst->Writer.mergeRecordsFromWriter(std::move(Src->Writer), [&](Error E) {
    auto [ErrorCode, Msg] = InstrProfError::take(std::move(E));
    std::unique_lock<std::mutex> ErrGuard{Dst->ErrLock};
    bool firstTime = Dst->WriterErrorCodes.insert(ErrorCode).second;
    if (firstTime)
      warn(toString(make_error<InstrProfError>(ErrorCode, Msg)));
  });
}

static StringRef
getFuncName(const StringMap<InstrProfWriter::ProfilingData>::value_type &Val) {
  return Val.first();
}

static std::string
getFuncName(const SampleProfileMap::value_type &Val) {
  return Val.second.getContext().toString();
}

template <typename T>
static void filterFunctions(T &ProfileMap) {
  bool hasFilter = !FuncNameFilter.empty();
  bool hasNegativeFilter = !FuncNameNegativeFilter.empty();
  if (!hasFilter && !hasNegativeFilter)
    return;

  // If filter starts with '?' it is MSVC mangled name, not a regex.
  llvm::Regex ProbablyMSVCMangledName("[?@$_0-9A-Za-z]+");
  if (hasFilter && FuncNameFilter[0] == '?' &&
      ProbablyMSVCMangledName.match(FuncNameFilter))
    FuncNameFilter = llvm::Regex::escape(FuncNameFilter);
  if (hasNegativeFilter && FuncNameNegativeFilter[0] == '?' &&
      ProbablyMSVCMangledName.match(FuncNameNegativeFilter))
    FuncNameNegativeFilter = llvm::Regex::escape(FuncNameNegativeFilter);

  size_t Count = ProfileMap.size();
  llvm::Regex Pattern(FuncNameFilter);
  llvm::Regex NegativePattern(FuncNameNegativeFilter);
  std::string Error;
  if (hasFilter && !Pattern.isValid(Error))
    exitWithError(Error);
  if (hasNegativeFilter && !NegativePattern.isValid(Error))
    exitWithError(Error);

  // Handle MD5 profile, so it is still able to match using the original name.
  std::string MD5Name = std::to_string(llvm::MD5Hash(FuncNameFilter));
  std::string NegativeMD5Name =
      std::to_string(llvm::MD5Hash(FuncNameNegativeFilter));

  for (auto I = ProfileMap.begin(); I != ProfileMap.end();) {
    auto Tmp = I++;
    const auto &FuncName = getFuncName(*Tmp);
    // Negative filter has higher precedence than positive filter.
    if ((hasNegativeFilter &&
         (NegativePattern.match(FuncName) ||
          (FunctionSamples::UseMD5 && NegativeMD5Name == FuncName))) ||
        (hasFilter && !(Pattern.match(FuncName) ||
                        (FunctionSamples::UseMD5 && MD5Name == FuncName))))
      ProfileMap.erase(Tmp);
  }

  llvm::dbgs() << Count - ProfileMap.size() << " of " << Count << " functions "
               << "in the original profile are filtered.\n";
}

static void writeInstrProfile(StringRef OutputFilename,
                              ProfileFormat OutputFormat,
                              InstrProfWriter &Writer) {
  std::error_code EC;
  raw_fd_ostream Output(OutputFilename.data(), EC,
                        OutputFormat == PF_Text ? sys::fs::OF_TextWithCRLF
                                                : sys::fs::OF_None);
  if (EC)
    exitWithErrorCode(EC, OutputFilename);

  if (OutputFormat == PF_Text) {
    if (Error E = Writer.writeText(Output))
      warn(std::move(E));
  } else {
    if (Output.is_displayed())
      exitWithError("cannot write a non-text format profile to the terminal");
    if (Error E = Writer.write(Output))
      warn(std::move(E));
  }
}

static void mergeInstrProfile(const WeightedFileVector &Inputs,
                              SymbolRemapper *Remapper,
                              int MaxDbgCorrelationWarnings,
                              const StringRef ProfiledBinary) {
  const uint64_t TraceReservoirSize = TemporalProfTraceReservoirSize.getValue();
  const uint64_t MaxTraceLength = TemporalProfMaxTraceLength.getValue();
  if (OutputFormat == PF_Compact_Binary)
    exitWithError("Compact Binary is deprecated");
  if (OutputFormat != PF_Binary && OutputFormat != PF_Ext_Binary &&
      OutputFormat != PF_Text)
    exitWithError("unknown format is specified");

  // TODO: Maybe we should support correlation with mixture of different
  // correlation modes(w/wo debug-info/object correlation).
  if (!DebugInfoFilename.empty() && !BinaryFilename.empty())
    exitWithError("Expected only one of -debug-info, -binary-file");
  std::string CorrelateFilename;
  ProfCorrelatorKind CorrelateKind = ProfCorrelatorKind::NONE;
  if (!DebugInfoFilename.empty()) {
    CorrelateFilename = DebugInfoFilename;
    CorrelateKind = ProfCorrelatorKind::DEBUG_INFO;
  } else if (!BinaryFilename.empty()) {
    CorrelateFilename = BinaryFilename;
    CorrelateKind = ProfCorrelatorKind::BINARY;
  }

  std::unique_ptr<InstrProfCorrelator> Correlator;
  if (CorrelateKind != InstrProfCorrelator::NONE) {
    if (auto Err = InstrProfCorrelator::get(CorrelateFilename, CorrelateKind)
                       .moveInto(Correlator))
      exitWithError(std::move(Err), CorrelateFilename);
    if (auto Err = Correlator->correlateProfileData(MaxDbgCorrelationWarnings))
      exitWithError(std::move(Err), CorrelateFilename);
  }

  std::mutex ErrorLock;
  SmallSet<instrprof_error, 4> WriterErrorCodes;

  // If NumThreads is not specified, auto-detect a good default.
  if (NumThreads == 0)
    NumThreads = std::min(hardware_concurrency().compute_thread_count(),
                          unsigned((Inputs.size() + 1) / 2));

  // Initialize the writer contexts.
  SmallVector<std::unique_ptr<WriterContext>, 4> Contexts;
  for (unsigned I = 0; I < NumThreads; ++I)
    Contexts.emplace_back(std::make_unique<WriterContext>(
        OutputSparse, ErrorLock, WriterErrorCodes, TraceReservoirSize,
        MaxTraceLength));

  if (NumThreads == 1) {
    for (const auto &Input : Inputs)
      loadInput(Input, Remapper, Correlator.get(), ProfiledBinary,
                Contexts[0].get());
  } else {
    DefaultThreadPool Pool(hardware_concurrency(NumThreads));

    // Load the inputs in parallel (N/NumThreads serial steps).
    unsigned Ctx = 0;
    for (const auto &Input : Inputs) {
      Pool.async(loadInput, Input, Remapper, Correlator.get(), ProfiledBinary,
                 Contexts[Ctx].get());
      Ctx = (Ctx + 1) % NumThreads;
    }
    Pool.wait();

    // Merge the writer contexts together (~ lg(NumThreads) serial steps).
    unsigned Mid = Contexts.size() / 2;
    unsigned End = Contexts.size();
    assert(Mid > 0 && "Expected more than one context");
    do {
      for (unsigned I = 0; I < Mid; ++I)
        Pool.async(mergeWriterContexts, Contexts[I].get(),
                   Contexts[I + Mid].get());
      Pool.wait();
      if (End & 1) {
        Pool.async(mergeWriterContexts, Contexts[0].get(),
                   Contexts[End - 1].get());
        Pool.wait();
      }
      End = Mid;
      Mid /= 2;
    } while (Mid > 0);
  }

  // Handle deferred errors encountered during merging. If the number of errors
  // is equal to the number of inputs the merge failed.
  unsigned NumErrors = 0;
  for (std::unique_ptr<WriterContext> &WC : Contexts) {
    for (auto &ErrorPair : WC->Errors) {
      ++NumErrors;
      warn(toString(std::move(ErrorPair.first)), ErrorPair.second);
    }
  }
  if ((NumErrors == Inputs.size() && FailMode == failIfAllAreInvalid) ||
      (NumErrors > 0 && FailMode == failIfAnyAreInvalid))
    exitWithError("no profile can be merged");

  filterFunctions(Contexts[0]->Writer.getProfileData());

  writeInstrProfile(OutputFilename, OutputFormat, Contexts[0]->Writer);
}

/// The profile entry for a function in instrumentation profile.
struct InstrProfileEntry {
  uint64_t MaxCount = 0;
  uint64_t NumEdgeCounters = 0;
  float ZeroCounterRatio = 0.0;
  InstrProfRecord *ProfRecord;
  InstrProfileEntry(InstrProfRecord *Record);
  InstrProfileEntry() = default;
};

InstrProfileEntry::InstrProfileEntry(InstrProfRecord *Record) {
  ProfRecord = Record;
  uint64_t CntNum = Record->Counts.size();
  uint64_t ZeroCntNum = 0;
  for (size_t I = 0; I < CntNum; ++I) {
    MaxCount = std::max(MaxCount, Record->Counts[I]);
    ZeroCntNum += !Record->Counts[I];
  }
  ZeroCounterRatio = (float)ZeroCntNum / CntNum;
  NumEdgeCounters = CntNum;
}

/// Either set all the counters in the instr profile entry \p IFE to
/// -1 / -2 /in order to drop the profile or scale up the
/// counters in \p IFP to be above hot / cold threshold. We use
/// the ratio of zero counters in the profile of a function to
/// decide the profile is helpful or harmful for performance,
/// and to choose whether to scale up or drop it.
static void updateInstrProfileEntry(InstrProfileEntry &IFE, bool SetToHot,
                                    uint64_t HotInstrThreshold,
                                    uint64_t ColdInstrThreshold,
                                    float ZeroCounterThreshold) {
  InstrProfRecord *ProfRecord = IFE.ProfRecord;
  if (!IFE.MaxCount || IFE.ZeroCounterRatio > ZeroCounterThreshold) {
    // If all or most of the counters of the function are zero, the
    // profile is unaccountable and should be dropped. Reset all the
    // counters to be -1 / -2 and PGO profile-use will drop the profile.
    // All counters being -1 also implies that the function is hot so
    // PGO profile-use will also set the entry count metadata to be
    // above hot threshold.
    // All counters being -2 implies that the function is warm so
    // PGO profile-use will also set the entry count metadata to be
    // above cold threshold.
    auto Kind =
        (SetToHot ? InstrProfRecord::PseudoHot : InstrProfRecord::PseudoWarm);
    ProfRecord->setPseudoCount(Kind);
    return;
  }

  // Scale up the MaxCount to be multiple times above hot / cold threshold.
  const unsigned MultiplyFactor = 3;
  uint64_t Threshold = (SetToHot ? HotInstrThreshold : ColdInstrThreshold);
  uint64_t Numerator = Threshold * MultiplyFactor;

  // Make sure Threshold for warm counters is below the HotInstrThreshold.
  if (!SetToHot && Threshold >= HotInstrThreshold) {
    Threshold = (HotInstrThreshold + ColdInstrThreshold) / 2;
  }

  uint64_t Denominator = IFE.MaxCount;
  if (Numerator <= Denominator)
    return;
  ProfRecord->scale(Numerator, Denominator, [&](instrprof_error E) {
    warn(toString(make_error<InstrProfError>(E)));
  });
}

const uint64_t ColdPercentileIdx = 15;
const uint64_t HotPercentileIdx = 11;

using sampleprof::FSDiscriminatorPass;

// Internal options to set FSDiscriminatorPass. Used in merge and show
// commands.
static cl::opt<FSDiscriminatorPass> FSDiscriminatorPassOption(
    "fs-discriminator-pass", cl::init(PassLast), cl::Hidden,
    cl::desc("Zero out the discriminator bits for the FS discrimiantor "
             "pass beyond this value. The enum values are defined in "
             "Support/Discriminator.h"),
    cl::values(clEnumVal(Base, "Use base discriminators only"),
               clEnumVal(Pass1, "Use base and pass 1 discriminators"),
               clEnumVal(Pass2, "Use base and pass 1-2 discriminators"),
               clEnumVal(Pass3, "Use base and pass 1-3 discriminators"),
               clEnumVal(PassLast, "Use all discriminator bits (default)")));

static unsigned getDiscriminatorMask() {
  return getN1Bits(getFSPassBitEnd(FSDiscriminatorPassOption.getValue()));
}

/// Adjust the instr profile in \p WC based on the sample profile in
/// \p Reader.
static void
adjustInstrProfile(std::unique_ptr<WriterContext> &WC,
                   std::unique_ptr<sampleprof::SampleProfileReader> &Reader,
                   unsigned SupplMinSizeThreshold, float ZeroCounterThreshold,
                   unsigned InstrProfColdThreshold) {
  // Function to its entry in instr profile.
  StringMap<InstrProfileEntry> InstrProfileMap;
  StringMap<StringRef> StaticFuncMap;
  InstrProfSummaryBuilder IPBuilder(ProfileSummaryBuilder::DefaultCutoffs);

  auto checkSampleProfileHasFUnique = [&Reader]() {
    for (const auto &PD : Reader->getProfiles()) {
      auto &FContext = PD.second.getContext();
      if (FContext.toString().find(FunctionSamples::UniqSuffix) !=
          std::string::npos) {
        return true;
      }
    }
    return false;
  };

  bool SampleProfileHasFUnique = checkSampleProfileHasFUnique();

  auto buildStaticFuncMap = [&StaticFuncMap,
                             SampleProfileHasFUnique](const StringRef Name) {
    std::string FilePrefixes[] = {".cpp", "cc", ".c", ".hpp", ".h"};
    size_t PrefixPos = StringRef::npos;
    for (auto &FilePrefix : FilePrefixes) {
      std::string NamePrefix = FilePrefix + GlobalIdentifierDelimiter;
      PrefixPos = Name.find_insensitive(NamePrefix);
      if (PrefixPos == StringRef::npos)
        continue;
      PrefixPos += NamePrefix.size();
      break;
    }

    if (PrefixPos == StringRef::npos) {
      return;
    }

    StringRef NewName = Name.drop_front(PrefixPos);
    StringRef FName = Name.substr(0, PrefixPos - 1);
    if (NewName.size() == 0) {
      return;
    }

    // This name should have a static linkage.
    size_t PostfixPos = NewName.find(FunctionSamples::UniqSuffix);
    bool ProfileHasFUnique = (PostfixPos != StringRef::npos);

    // If sample profile and instrumented profile do not agree on symbol
    // uniqification.
    if (SampleProfileHasFUnique != ProfileHasFUnique) {
      // If instrumented profile uses -funique-internal-linkage-symbols,
      // we need to trim the name.
      if (ProfileHasFUnique) {
        NewName = NewName.substr(0, PostfixPos);
      } else {
        // If sample profile uses -funique-internal-linkage-symbols,
        // we build the map.
        std::string NStr =
            NewName.str() + getUniqueInternalLinkagePostfix(FName);
        NewName = StringRef(NStr);
        StaticFuncMap[NewName] = Name;
        return;
      }
    }

    if (!StaticFuncMap.contains(NewName)) {
      StaticFuncMap[NewName] = Name;
    } else {
      StaticFuncMap[NewName] = DuplicateNameStr;
    }
  };

  // We need to flatten the SampleFDO profile as the InstrFDO
  // profile does not have inlined callsite profiles.
  // One caveat is the pre-inlined function -- their samples
  // should be collapsed into the caller function.
  // Here we do a DFS traversal to get the flatten profile
  // info: the sum of entrycount and the max of maxcount.
  // Here is the algorithm:
  //   recursive (FS, root_name) {
  //      name = FS->getName();
  //      get samples for FS;
  //      if (InstrProf.find(name) {
  //        root_name = name;
  //      } else {
  //        if (name is in static_func map) {
  //          root_name = static_name;
  //        }
  //      }
  //      update the Map entry for root_name;
  //      for (subfs: FS) {
  //        recursive(subfs, root_name);
  //      }
  //   }
  //
  // Here is an example.
  //
  // SampleProfile:
  // foo:12345:1000
  // 1: 1000
  // 2.1: 1000
  // 15: 5000
  // 4: bar:1000
  //  1: 1000
  //  2: goo:3000
  //   1: 3000
  // 8: bar:40000
  //  1: 10000
  //  2: goo:30000
  //   1: 30000
  //
  // InstrProfile has two entries:
  //  foo
  //  bar.cc;bar
  //
  // After BuildMaxSampleMap, we should have the following in FlattenSampleMap:
  // {"foo", {1000, 5000}}
  // {"bar.cc;bar", {11000, 30000}}
  //
  // foo's has an entry count of 1000, and max body count of 5000.
  // bar.cc;bar has an entry count of 11000 (sum two callsites of 1000 and
  // 10000), and max count of 30000 (from the callsite in line 8).
  //
  // Note that goo's count will remain in bar.cc;bar() as it does not have an
  // entry in InstrProfile.
  llvm::StringMap<std::pair<uint64_t, uint64_t>> FlattenSampleMap;
  auto BuildMaxSampleMap = [&FlattenSampleMap, &StaticFuncMap,
                            &InstrProfileMap](const FunctionSamples &FS,
                                              const StringRef &RootName) {
    auto BuildMaxSampleMapImpl = [&](const FunctionSamples &FS,
                                     const StringRef &RootName,
                                     auto &BuildImpl) -> void {
      std::string NameStr = FS.getFunction().str();
      const StringRef Name = NameStr;
      const StringRef *NewRootName = &RootName;
      uint64_t EntrySample = FS.getHeadSamplesEstimate();
      uint64_t MaxBodySample = FS.getMaxCountInside(/* SkipCallSite*/ true);

      auto It = InstrProfileMap.find(Name);
      if (It != InstrProfileMap.end()) {
        NewRootName = &Name;
      } else {
        auto NewName = StaticFuncMap.find(Name);
        if (NewName != StaticFuncMap.end()) {
          It = InstrProfileMap.find(NewName->second.str());
          if (NewName->second != DuplicateNameStr) {
            NewRootName = &NewName->second;
          }
        } else {
          // Here the EntrySample is of an inlined function, so we should not
          // update the EntrySample in the map.
          EntrySample = 0;
        }
      }
      EntrySample += FlattenSampleMap[*NewRootName].first;
      MaxBodySample =
          std::max(FlattenSampleMap[*NewRootName].second, MaxBodySample);
      FlattenSampleMap[*NewRootName] =
          std::make_pair(EntrySample, MaxBodySample);

      for (const auto &C : FS.getCallsiteSamples())
        for (const auto &F : C.second)
          BuildImpl(F.second, *NewRootName, BuildImpl);
    };
    BuildMaxSampleMapImpl(FS, RootName, BuildMaxSampleMapImpl);
  };

  for (auto &PD : WC->Writer.getProfileData()) {
    // Populate IPBuilder.
    for (const auto &PDV : PD.getValue()) {
      InstrProfRecord Record = PDV.second;
      IPBuilder.addRecord(Record);
    }

    // If a function has multiple entries in instr profile, skip it.
    if (PD.getValue().size() != 1)
      continue;

    // Initialize InstrProfileMap.
    InstrProfRecord *R = &PD.getValue().begin()->second;
    StringRef FullName = PD.getKey();
    InstrProfileMap[FullName] = InstrProfileEntry(R);
    buildStaticFuncMap(FullName);
  }

  for (auto &PD : Reader->getProfiles()) {
    sampleprof::FunctionSamples &FS = PD.second;
    std::string Name = FS.getFunction().str();
    BuildMaxSampleMap(FS, Name);
  }

  ProfileSummary InstrPS = *IPBuilder.getSummary();
  ProfileSummary SamplePS = Reader->getSummary();

  // Compute cold thresholds for instr profile and sample profile.
  uint64_t HotSampleThreshold =
      ProfileSummaryBuilder::getEntryForPercentile(
          SamplePS.getDetailedSummary(),
          ProfileSummaryBuilder::DefaultCutoffs[HotPercentileIdx])
          .MinCount;
  uint64_t ColdSampleThreshold =
      ProfileSummaryBuilder::getEntryForPercentile(
          SamplePS.getDetailedSummary(),
          ProfileSummaryBuilder::DefaultCutoffs[ColdPercentileIdx])
          .MinCount;
  uint64_t HotInstrThreshold =
      ProfileSummaryBuilder::getEntryForPercentile(
          InstrPS.getDetailedSummary(),
          ProfileSummaryBuilder::DefaultCutoffs[HotPercentileIdx])
          .MinCount;
  uint64_t ColdInstrThreshold =
      InstrProfColdThreshold
          ? InstrProfColdThreshold
          : ProfileSummaryBuilder::getEntryForPercentile(
                InstrPS.getDetailedSummary(),
                ProfileSummaryBuilder::DefaultCutoffs[ColdPercentileIdx])
                .MinCount;

  // Find hot/warm functions in sample profile which is cold in instr profile
  // and adjust the profiles of those functions in the instr profile.
  for (const auto &E : FlattenSampleMap) {
    uint64_t SampleMaxCount = std::max(E.second.first, E.second.second);
    if (SampleMaxCount < ColdSampleThreshold)
      continue;
    StringRef Name = E.first();
    auto It = InstrProfileMap.find(Name);
    if (It == InstrProfileMap.end()) {
      auto NewName = StaticFuncMap.find(Name);
      if (NewName != StaticFuncMap.end()) {
        It = InstrProfileMap.find(NewName->second.str());
        if (NewName->second == DuplicateNameStr) {
          WithColor::warning()
              << "Static function " << Name
              << " has multiple promoted names, cannot adjust profile.\n";
        }
      }
    }
    if (It == InstrProfileMap.end() ||
        It->second.MaxCount > ColdInstrThreshold ||
        It->second.NumEdgeCounters < SupplMinSizeThreshold)
      continue;
    bool SetToHot = SampleMaxCount >= HotSampleThreshold;
    updateInstrProfileEntry(It->second, SetToHot, HotInstrThreshold,
                            ColdInstrThreshold, ZeroCounterThreshold);
  }
}

/// The main function to supplement instr profile with sample profile.
/// \Inputs contains the instr profile. \p SampleFilename specifies the
/// sample profile. \p OutputFilename specifies the output profile name.
/// \p OutputFormat specifies the output profile format. \p OutputSparse
/// specifies whether to generate sparse profile. \p SupplMinSizeThreshold
/// specifies the minimal size for the functions whose profile will be
/// adjusted. \p ZeroCounterThreshold is the threshold to check whether
/// a function contains too many zero counters and whether its profile
/// should be dropped. \p InstrProfColdThreshold is the user specified
/// cold threshold which will override the cold threshold got from the
/// instr profile summary.
static void supplementInstrProfile(const WeightedFileVector &Inputs,
                                   StringRef SampleFilename, bool OutputSparse,
                                   unsigned SupplMinSizeThreshold,
                                   float ZeroCounterThreshold,
                                   unsigned InstrProfColdThreshold) {
  if (OutputFilename == "-")
    exitWithError("cannot write indexed profdata format to stdout");
  if (Inputs.size() != 1)
    exitWithError("expect one input to be an instr profile");
  if (Inputs[0].Weight != 1)
    exitWithError("expect instr profile doesn't have weight");

  StringRef InstrFilename = Inputs[0].Filename;

  // Read sample profile.
  LLVMContext Context;
  auto FS = vfs::getRealFileSystem();
  auto ReaderOrErr = sampleprof::SampleProfileReader::create(
      SampleFilename.str(), Context, *FS, FSDiscriminatorPassOption);
  if (std::error_code EC = ReaderOrErr.getError())
    exitWithErrorCode(EC, SampleFilename);
  auto Reader = std::move(ReaderOrErr.get());
  if (std::error_code EC = Reader->read())
    exitWithErrorCode(EC, SampleFilename);

  // Read instr profile.
  std::mutex ErrorLock;
  SmallSet<instrprof_error, 4> WriterErrorCodes;
  auto WC = std::make_unique<WriterContext>(OutputSparse, ErrorLock,
                                            WriterErrorCodes);
  loadInput(Inputs[0], nullptr, nullptr, /*ProfiledBinary=*/"", WC.get());
  if (WC->Errors.size() > 0)
    exitWithError(std::move(WC->Errors[0].first), InstrFilename);

  adjustInstrProfile(WC, Reader, SupplMinSizeThreshold, ZeroCounterThreshold,
                     InstrProfColdThreshold);
  writeInstrProfile(OutputFilename, OutputFormat, WC->Writer);
}

/// Make a copy of the given function samples with all symbol names remapped
/// by the provided symbol remapper.
static sampleprof::FunctionSamples
remapSamples(const sampleprof::FunctionSamples &Samples,
             SymbolRemapper &Remapper, sampleprof_error &Error) {
  sampleprof::FunctionSamples Result;
  Result.setFunction(Remapper(Samples.getFunction()));
  Result.addTotalSamples(Samples.getTotalSamples());
  Result.addHeadSamples(Samples.getHeadSamples());
  for (const auto &BodySample : Samples.getBodySamples()) {
    uint32_t MaskedDiscriminator =
        BodySample.first.Discriminator & getDiscriminatorMask();
    Result.addBodySamples(BodySample.first.LineOffset, MaskedDiscriminator,
                          BodySample.second.getSamples());
    for (const auto &Target : BodySample.second.getCallTargets()) {
      Result.addCalledTargetSamples(BodySample.first.LineOffset,
                                    MaskedDiscriminator,
                                    Remapper(Target.first), Target.second);
    }
  }
  for (const auto &CallsiteSamples : Samples.getCallsiteSamples()) {
    sampleprof::FunctionSamplesMap &Target =
        Result.functionSamplesAt(CallsiteSamples.first);
    for (const auto &Callsite : CallsiteSamples.second) {
      sampleprof::FunctionSamples Remapped =
          remapSamples(Callsite.second, Remapper, Error);
      mergeSampleProfErrors(Error,
                            Target[Remapped.getFunction()].merge(Remapped));
    }
  }
  return Result;
}

static sampleprof::SampleProfileFormat FormatMap[] = {
    sampleprof::SPF_None,
    sampleprof::SPF_Text,
    sampleprof::SPF_None,
    sampleprof::SPF_Ext_Binary,
    sampleprof::SPF_GCC,
    sampleprof::SPF_Binary};

static std::unique_ptr<MemoryBuffer>
getInputFileBuf(const StringRef &InputFile) {
  if (InputFile == "")
    return {};

  auto BufOrError = MemoryBuffer::getFileOrSTDIN(InputFile);
  if (!BufOrError)
    exitWithErrorCode(BufOrError.getError(), InputFile);

  return std::move(*BufOrError);
}

static void populateProfileSymbolList(MemoryBuffer *Buffer,
                                      sampleprof::ProfileSymbolList &PSL) {
  if (!Buffer)
    return;

  SmallVector<StringRef, 32> SymbolVec;
  StringRef Data = Buffer->getBuffer();
  Data.split(SymbolVec, '\n', /*MaxSplit=*/-1, /*KeepEmpty=*/false);

  for (StringRef SymbolStr : SymbolVec)
    PSL.add(SymbolStr.trim());
}

static void handleExtBinaryWriter(sampleprof::SampleProfileWriter &Writer,
                                  ProfileFormat OutputFormat,
                                  MemoryBuffer *Buffer,
                                  sampleprof::ProfileSymbolList &WriterList,
                                  bool CompressAllSections, bool UseMD5,
                                  bool GenPartialProfile) {
  populateProfileSymbolList(Buffer, WriterList);
  if (WriterList.size() > 0 && OutputFormat != PF_Ext_Binary)
    warn("Profile Symbol list is not empty but the output format is not "
         "ExtBinary format. The list will be lost in the output. ");

  Writer.setProfileSymbolList(&WriterList);

  if (CompressAllSections) {
    if (OutputFormat != PF_Ext_Binary)
      warn("-compress-all-section is ignored. Specify -extbinary to enable it");
    else
      Writer.setToCompressAllSections();
  }
  if (UseMD5) {
    if (OutputFormat != PF_Ext_Binary)
      warn("-use-md5 is ignored. Specify -extbinary to enable it");
    else
      Writer.setUseMD5();
  }
  if (GenPartialProfile) {
    if (OutputFormat != PF_Ext_Binary)
      warn("-gen-partial-profile is ignored. Specify -extbinary to enable it");
    else
      Writer.setPartialProfile();
  }
}

static void mergeSampleProfile(const WeightedFileVector &Inputs,
                               SymbolRemapper *Remapper,
                               StringRef ProfileSymbolListFile,
                               size_t OutputSizeLimit) {
  using namespace sampleprof;
  SampleProfileMap ProfileMap;
  SmallVector<std::unique_ptr<sampleprof::SampleProfileReader>, 5> Readers;
  LLVMContext Context;
  sampleprof::ProfileSymbolList WriterList;
  std::optional<bool> ProfileIsProbeBased;
  std::optional<bool> ProfileIsCS;
  for (const auto &Input : Inputs) {
    auto FS = vfs::getRealFileSystem();
    auto ReaderOrErr = SampleProfileReader::create(Input.Filename, Context, *FS,
                                                   FSDiscriminatorPassOption);
    if (std::error_code EC = ReaderOrErr.getError()) {
      warnOrExitGivenError(FailMode, EC, Input.Filename);
      continue;
    }

    // We need to keep the readers around until after all the files are
    // read so that we do not lose the function names stored in each
    // reader's memory. The function names are needed to write out the
    // merged profile map.
    Readers.push_back(std::move(ReaderOrErr.get()));
    const auto Reader = Readers.back().get();
    if (std::error_code EC = Reader->read()) {
      warnOrExitGivenError(FailMode, EC, Input.Filename);
      Readers.pop_back();
      continue;
    }

    SampleProfileMap &Profiles = Reader->getProfiles();
    if (ProfileIsProbeBased &&
        ProfileIsProbeBased != FunctionSamples::ProfileIsProbeBased)
      exitWithError(
          "cannot merge probe-based profile with non-probe-based profile");
    ProfileIsProbeBased = FunctionSamples::ProfileIsProbeBased;
    if (ProfileIsCS && ProfileIsCS != FunctionSamples::ProfileIsCS)
      exitWithError("cannot merge CS profile with non-CS profile");
    ProfileIsCS = FunctionSamples::ProfileIsCS;
    for (SampleProfileMap::iterator I = Profiles.begin(), E = Profiles.end();
         I != E; ++I) {
      sampleprof_error Result = sampleprof_error::success;
      FunctionSamples Remapped =
          Remapper ? remapSamples(I->second, *Remapper, Result)
                   : FunctionSamples();
      FunctionSamples &Samples = Remapper ? Remapped : I->second;
      SampleContext FContext = Samples.getContext();
      mergeSampleProfErrors(Result,
                            ProfileMap[FContext].merge(Samples, Input.Weight));
      if (Result != sampleprof_error::success) {
        std::error_code EC = make_error_code(Result);
        handleMergeWriterError(errorCodeToError(EC), Input.Filename,
                               FContext.toString());
      }
    }

    if (!DropProfileSymbolList) {
      std::unique_ptr<sampleprof::ProfileSymbolList> ReaderList =
          Reader->getProfileSymbolList();
      if (ReaderList)
        WriterList.merge(*ReaderList);
    }
  }

  if (ProfileIsCS && (SampleMergeColdContext || SampleTrimColdContext)) {
    // Use threshold calculated from profile summary unless specified.
    SampleProfileSummaryBuilder Builder(ProfileSummaryBuilder::DefaultCutoffs);
    auto Summary = Builder.computeSummaryForProfiles(ProfileMap);
    uint64_t SampleProfColdThreshold =
        ProfileSummaryBuilder::getColdCountThreshold(
            (Summary->getDetailedSummary()));

    // Trim and merge cold context profile using cold threshold above;
    SampleContextTrimmer(ProfileMap)
        .trimAndMergeColdContextProfiles(
            SampleProfColdThreshold, SampleTrimColdContext,
            SampleMergeColdContext, SampleColdContextFrameDepth, false);
  }

  if (ProfileLayout == llvm::sampleprof::SPL_Flat) {
    ProfileConverter::flattenProfile(ProfileMap, FunctionSamples::ProfileIsCS);
    ProfileIsCS = FunctionSamples::ProfileIsCS = false;
  } else if (ProfileIsCS && ProfileLayout == llvm::sampleprof::SPL_Nest) {
    ProfileConverter CSConverter(ProfileMap);
    CSConverter.convertCSProfiles();
    ProfileIsCS = FunctionSamples::ProfileIsCS = false;
  }

  filterFunctions(ProfileMap);

  auto WriterOrErr =
      SampleProfileWriter::create(OutputFilename, FormatMap[OutputFormat]);
  if (std::error_code EC = WriterOrErr.getError())
    exitWithErrorCode(EC, OutputFilename);

  auto Writer = std::move(WriterOrErr.get());
  // WriterList will have StringRef refering to string in Buffer.
  // Make sure Buffer lives as long as WriterList.
  auto Buffer = getInputFileBuf(ProfileSymbolListFile);
  handleExtBinaryWriter(*Writer, OutputFormat, Buffer.get(), WriterList,
                        CompressAllSections, UseMD5, GenPartialProfile);

  // If OutputSizeLimit is 0 (default), it is the same as write().
  if (std::error_code EC =
          Writer->writeWithSizeLimit(ProfileMap, OutputSizeLimit))
    exitWithErrorCode(EC);
}

static WeightedFile parseWeightedFile(const StringRef &WeightedFilename) {
  StringRef WeightStr, FileName;
  std::tie(WeightStr, FileName) = WeightedFilename.split(',');

  uint64_t Weight;
  if (WeightStr.getAsInteger(10, Weight) || Weight < 1)
    exitWithError("input weight must be a positive integer");

  return {std::string(FileName), Weight};
}

static void addWeightedInput(WeightedFileVector &WNI, const WeightedFile &WF) {
  StringRef Filename = WF.Filename;
  uint64_t Weight = WF.Weight;

  // If it's STDIN just pass it on.
  if (Filename == "-") {
    WNI.push_back({std::string(Filename), Weight});
    return;
  }

  llvm::sys::fs::file_status Status;
  llvm::sys::fs::status(Filename, Status);
  if (!llvm::sys::fs::exists(Status))
    exitWithErrorCode(make_error_code(errc::no_such_file_or_directory),
                      Filename);
  // If it's a source file, collect it.
  if (llvm::sys::fs::is_regular_file(Status)) {
    WNI.push_back({std::string(Filename), Weight});
    return;
  }

  if (llvm::sys::fs::is_directory(Status)) {
    std::error_code EC;
    for (llvm::sys::fs::recursive_directory_iterator F(Filename, EC), E;
         F != E && !EC; F.increment(EC)) {
      if (llvm::sys::fs::is_regular_file(F->path())) {
        addWeightedInput(WNI, {F->path(), Weight});
      }
    }
    if (EC)
      exitWithErrorCode(EC, Filename);
  }
}

static void parseInputFilenamesFile(MemoryBuffer *Buffer,
                                    WeightedFileVector &WFV) {
  if (!Buffer)
    return;

  SmallVector<StringRef, 8> Entries;
  StringRef Data = Buffer->getBuffer();
  Data.split(Entries, '\n', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (const StringRef &FileWeightEntry : Entries) {
    StringRef SanitizedEntry = FileWeightEntry.trim(" \t\v\f\r");
    // Skip comments.
    if (SanitizedEntry.starts_with("#"))
      continue;
    // If there's no comma, it's an unweighted profile.
    else if (!SanitizedEntry.contains(','))
      addWeightedInput(WFV, {std::string(SanitizedEntry), 1});
    else
      addWeightedInput(WFV, parseWeightedFile(SanitizedEntry));
  }
}

static int merge_main(StringRef ProgName) {
  WeightedFileVector WeightedInputs;
  for (StringRef Filename : InputFilenames)
    addWeightedInput(WeightedInputs, {std::string(Filename), 1});
  for (StringRef WeightedFilename : WeightedInputFilenames)
    addWeightedInput(WeightedInputs, parseWeightedFile(WeightedFilename));

  // Make sure that the file buffer stays alive for the duration of the
  // weighted input vector's lifetime.
  auto Buffer = getInputFileBuf(InputFilenamesFile);
  parseInputFilenamesFile(Buffer.get(), WeightedInputs);

  if (WeightedInputs.empty())
    exitWithError("no input files specified. See " + ProgName + " merge -help");

  if (DumpInputFileList) {
    for (auto &WF : WeightedInputs)
      outs() << WF.Weight << "," << WF.Filename << "\n";
    return 0;
  }

  std::unique_ptr<SymbolRemapper> Remapper;
  if (!RemappingFile.empty())
    Remapper = SymbolRemapper::create(RemappingFile);

  if (!SupplInstrWithSample.empty()) {
    if (ProfileKind != instr)
      exitWithError(
          "-supplement-instr-with-sample can only work with -instr. ");

    supplementInstrProfile(WeightedInputs, SupplInstrWithSample, OutputSparse,
                           SupplMinSizeThreshold, ZeroCounterThreshold,
                           InstrProfColdThreshold);
    return 0;
  }

  if (ProfileKind == instr)
    mergeInstrProfile(WeightedInputs, Remapper.get(), MaxDbgCorrelationWarnings,
                      ProfiledBinary);
  else
    mergeSampleProfile(WeightedInputs, Remapper.get(), ProfileSymbolListFile,
                       OutputSizeLimit);
  return 0;
}

/// Computer the overlap b/w profile BaseFilename and profile TestFilename.
static void overlapInstrProfile(const std::string &BaseFilename,
                                const std::string &TestFilename,
                                const OverlapFuncFilters &FuncFilter,
                                raw_fd_ostream &OS, bool IsCS) {
  std::mutex ErrorLock;
  SmallSet<instrprof_error, 4> WriterErrorCodes;
  WriterContext Context(false, ErrorLock, WriterErrorCodes);
  WeightedFile WeightedInput{BaseFilename, 1};
  OverlapStats Overlap;
  Error E = Overlap.accumulateCounts(BaseFilename, TestFilename, IsCS);
  if (E)
    exitWithError(std::move(E), "error in getting profile count sums");
  if (Overlap.Base.CountSum < 1.0f) {
    OS << "Sum of edge counts for profile " << BaseFilename << " is 0.\n";
    exit(0);
  }
  if (Overlap.Test.CountSum < 1.0f) {
    OS << "Sum of edge counts for profile " << TestFilename << " is 0.\n";
    exit(0);
  }
  loadInput(WeightedInput, nullptr, nullptr, /*ProfiledBinary=*/"", &Context);
  overlapInput(BaseFilename, TestFilename, &Context, Overlap, FuncFilter, OS,
               IsCS);
  Overlap.dump(OS);
}

namespace {
struct SampleOverlapStats {
  SampleContext BaseName;
  SampleContext TestName;
  // Number of overlap units
  uint64_t OverlapCount = 0;
  // Total samples of overlap units
  uint64_t OverlapSample = 0;
  // Number of and total samples of units that only present in base or test
  // profile
  uint64_t BaseUniqueCount = 0;
  uint64_t BaseUniqueSample = 0;
  uint64_t TestUniqueCount = 0;
  uint64_t TestUniqueSample = 0;
  // Number of units and total samples in base or test profile
  uint64_t BaseCount = 0;
  uint64_t BaseSample = 0;
  uint64_t TestCount = 0;
  uint64_t TestSample = 0;
  // Number of and total samples of units that present in at least one profile
  uint64_t UnionCount = 0;
  uint64_t UnionSample = 0;
  // Weighted similarity
  double Similarity = 0.0;
  // For SampleOverlapStats instances representing functions, weights of the
  // function in base and test profiles
  double BaseWeight = 0.0;
  double TestWeight = 0.0;

  SampleOverlapStats() = default;
};
} // end anonymous namespace

namespace {
struct FuncSampleStats {
  uint64_t SampleSum = 0;
  uint64_t MaxSample = 0;
  uint64_t HotBlockCount = 0;
  FuncSampleStats() = default;
  FuncSampleStats(uint64_t SampleSum, uint64_t MaxSample,
                  uint64_t HotBlockCount)
      : SampleSum(SampleSum), MaxSample(MaxSample),
        HotBlockCount(HotBlockCount) {}
};
} // end anonymous namespace

namespace {
enum MatchStatus { MS_Match, MS_FirstUnique, MS_SecondUnique, MS_None };

// Class for updating merging steps for two sorted maps. The class should be
// instantiated with a map iterator type.
template <class T> class MatchStep {
public:
  MatchStep() = delete;

  MatchStep(T FirstIter, T FirstEnd, T SecondIter, T SecondEnd)
      : FirstIter(FirstIter), FirstEnd(FirstEnd), SecondIter(SecondIter),
        SecondEnd(SecondEnd), Status(MS_None) {}

  bool areBothFinished() const {
    return (FirstIter == FirstEnd && SecondIter == SecondEnd);
  }

  bool isFirstFinished() const { return FirstIter == FirstEnd; }

  bool isSecondFinished() const { return SecondIter == SecondEnd; }

  /// Advance one step based on the previous match status unless the previous
  /// status is MS_None. Then update Status based on the comparison between two
  /// container iterators at the current step. If the previous status is
  /// MS_None, it means two iterators are at the beginning and no comparison has
  /// been made, so we simply update Status without advancing the iterators.
  void updateOneStep();

  T getFirstIter() const { return FirstIter; }

  T getSecondIter() const { return SecondIter; }

  MatchStatus getMatchStatus() const { return Status; }

private:
  // Current iterator and end iterator of the first container.
  T FirstIter;
  T FirstEnd;
  // Current iterator and end iterator of the second container.
  T SecondIter;
  T SecondEnd;
  // Match status of the current step.
  MatchStatus Status;
};
} // end anonymous namespace

template <class T> void MatchStep<T>::updateOneStep() {
  switch (Status) {
  case MS_Match:
    ++FirstIter;
    ++SecondIter;
    break;
  case MS_FirstUnique:
    ++FirstIter;
    break;
  case MS_SecondUnique:
    ++SecondIter;
    break;
  case MS_None:
    break;
  }

  // Update Status according to iterators at the current step.
  if (areBothFinished())
    return;
  if (FirstIter != FirstEnd &&
      (SecondIter == SecondEnd || FirstIter->first < SecondIter->first))
    Status = MS_FirstUnique;
  else if (SecondIter != SecondEnd &&
           (FirstIter == FirstEnd || SecondIter->first < FirstIter->first))
    Status = MS_SecondUnique;
  else
    Status = MS_Match;
}

// Return the sum of line/block samples, the max line/block sample, and the
// number of line/block samples above the given threshold in a function
// including its inlinees.
static void getFuncSampleStats(const sampleprof::FunctionSamples &Func,
                               FuncSampleStats &FuncStats,
                               uint64_t HotThreshold) {
  for (const auto &L : Func.getBodySamples()) {
    uint64_t Sample = L.second.getSamples();
    FuncStats.SampleSum += Sample;
    FuncStats.MaxSample = std::max(FuncStats.MaxSample, Sample);
    if (Sample >= HotThreshold)
      ++FuncStats.HotBlockCount;
  }

  for (const auto &C : Func.getCallsiteSamples()) {
    for (const auto &F : C.second)
      getFuncSampleStats(F.second, FuncStats, HotThreshold);
  }
}

/// Predicate that determines if a function is hot with a given threshold. We
/// keep it separate from its callsites for possible extension in the future.
static bool isFunctionHot(const FuncSampleStats &FuncStats,
                          uint64_t HotThreshold) {
  // We intentionally compare the maximum sample count in a function with the
  // HotThreshold to get an approximate determination on hot functions.
  return (FuncStats.MaxSample >= HotThreshold);
}

namespace {
class SampleOverlapAggregator {
public:
  SampleOverlapAggregator(const std::string &BaseFilename,
                          const std::string &TestFilename,
                          double LowSimilarityThreshold, double Epsilon,
                          const OverlapFuncFilters &FuncFilter)
      : BaseFilename(BaseFilename), TestFilename(TestFilename),
        LowSimilarityThreshold(LowSimilarityThreshold), Epsilon(Epsilon),
        FuncFilter(FuncFilter) {}

  /// Detect 0-sample input profile and report to output stream. This interface
  /// should be called after loadProfiles().
  bool detectZeroSampleProfile(raw_fd_ostream &OS) const;

  /// Write out function-level similarity statistics for functions specified by
  /// options --function, --value-cutoff, and --similarity-cutoff.
  void dumpFuncSimilarity(raw_fd_ostream &OS) const;

  /// Write out program-level similarity and overlap statistics.
  void dumpProgramSummary(raw_fd_ostream &OS) const;

  /// Write out hot-function and hot-block statistics for base_profile,
  /// test_profile, and their overlap. For both cases, the overlap HO is
  /// calculated as follows:
  ///    Given the number of functions (or blocks) that are hot in both profiles
  ///    HCommon and the number of functions (or blocks) that are hot in at
  ///    least one profile HUnion, HO = HCommon / HUnion.
  void dumpHotFuncAndBlockOverlap(raw_fd_ostream &OS) const;

  /// This function tries matching functions in base and test profiles. For each
  /// pair of matched functions, it aggregates the function-level
  /// similarity into a profile-level similarity. It also dump function-level
  /// similarity information of functions specified by --function,
  /// --value-cutoff, and --similarity-cutoff options. The program-level
  /// similarity PS is computed as follows:
  ///     Given function-level similarity FS(A) for all function A, the
  ///     weight of function A in base profile WB(A), and the weight of function
  ///     A in test profile WT(A), compute PS(base_profile, test_profile) =
  ///     sum_A(FS(A) * avg(WB(A), WT(A))) ranging in [0.0f to 1.0f] with 0.0
  ///     meaning no-overlap.
  void computeSampleProfileOverlap(raw_fd_ostream &OS);

  /// Initialize ProfOverlap with the sum of samples in base and test
  /// profiles. This function also computes and keeps the sum of samples and
  /// max sample counts of each function in BaseStats and TestStats for later
  /// use to avoid re-computations.
  void initializeSampleProfileOverlap();

  /// Load profiles specified by BaseFilename and TestFilename.
  std::error_code loadProfiles();

  using FuncSampleStatsMap =
      std::unordered_map<SampleContext, FuncSampleStats, SampleContext::Hash>;

private:
  SampleOverlapStats ProfOverlap;
  SampleOverlapStats HotFuncOverlap;
  SampleOverlapStats HotBlockOverlap;
  std::string BaseFilename;
  std::string TestFilename;
  std::unique_ptr<sampleprof::SampleProfileReader> BaseReader;
  std::unique_ptr<sampleprof::SampleProfileReader> TestReader;
  // BaseStats and TestStats hold FuncSampleStats for each function, with
  // function name as the key.
  FuncSampleStatsMap BaseStats;
  FuncSampleStatsMap TestStats;
  // Low similarity threshold in floating point number
  double LowSimilarityThreshold;
  // Block samples above BaseHotThreshold or TestHotThreshold are considered hot
  // for tracking hot blocks.
  uint64_t BaseHotThreshold;
  uint64_t TestHotThreshold;
  // A small threshold used to round the results of floating point accumulations
  // to resolve imprecision.
  const double Epsilon;
  std::multimap<double, SampleOverlapStats, std::greater<double>>
      FuncSimilarityDump;
  // FuncFilter carries specifications in options --value-cutoff and
  // --function.
  OverlapFuncFilters FuncFilter;
  // Column offsets for printing the function-level details table.
  static const unsigned int TestWeightCol = 15;
  static const unsigned int SimilarityCol = 30;
  static const unsigned int OverlapCol = 43;
  static const unsigned int BaseUniqueCol = 53;
  static const unsigned int TestUniqueCol = 67;
  static const unsigned int BaseSampleCol = 81;
  static const unsigned int TestSampleCol = 96;
  static const unsigned int FuncNameCol = 111;

  /// Return a similarity of two line/block sample counters in the same
  /// function in base and test profiles. The line/block-similarity BS(i) is
  /// computed as follows:
  ///    For an offsets i, given the sample count at i in base profile BB(i),
  ///    the sample count at i in test profile BT(i), the sum of sample counts
  ///    in this function in base profile SB, and the sum of sample counts in
  ///    this function in test profile ST, compute BS(i) = 1.0 - fabs(BB(i)/SB -
  ///    BT(i)/ST), ranging in [0.0f to 1.0f] with 0.0 meaning no-overlap.
  double computeBlockSimilarity(uint64_t BaseSample, uint64_t TestSample,
                                const SampleOverlapStats &FuncOverlap) const;

  void updateHotBlockOverlap(uint64_t BaseSample, uint64_t TestSample,
                             uint64_t HotBlockCount);

  void getHotFunctions(const FuncSampleStatsMap &ProfStats,
                       FuncSampleStatsMap &HotFunc,
                       uint64_t HotThreshold) const;

  void computeHotFuncOverlap();

  /// This function updates statistics in FuncOverlap, HotBlockOverlap, and
  /// Difference for two sample units in a matched function according to the
  /// given match status.
  void updateOverlapStatsForFunction(uint64_t BaseSample, uint64_t TestSample,
                                     uint64_t HotBlockCount,
                                     SampleOverlapStats &FuncOverlap,
                                     double &Difference, MatchStatus Status);

  /// This function updates statistics in FuncOverlap, HotBlockOverlap, and
  /// Difference for unmatched callees that only present in one profile in a
  /// matched caller function.
  void updateForUnmatchedCallee(const sampleprof::FunctionSamples &Func,
                                SampleOverlapStats &FuncOverlap,
                                double &Difference, MatchStatus Status);

  /// This function updates sample overlap statistics of an overlap function in
  /// base and test profile. It also calculates a function-internal similarity
  /// FIS as follows:
  ///    For offsets i that have samples in at least one profile in this
  ///    function A, given BS(i) returned by computeBlockSimilarity(), compute
  ///    FIS(A) = (2.0 - sum_i(1.0 - BS(i))) / 2, ranging in [0.0f to 1.0f] with
  ///    0.0 meaning no overlap.
  double computeSampleFunctionInternalOverlap(
      const sampleprof::FunctionSamples &BaseFunc,
      const sampleprof::FunctionSamples &TestFunc,
      SampleOverlapStats &FuncOverlap);

  /// Function-level similarity (FS) is a weighted value over function internal
  /// similarity (FIS). This function computes a function's FS from its FIS by
  /// applying the weight.
  double weightForFuncSimilarity(double FuncSimilarity, uint64_t BaseFuncSample,
                                 uint64_t TestFuncSample) const;

  /// The function-level similarity FS(A) for a function A is computed as
  /// follows:
  ///     Compute a function-internal similarity FIS(A) by
  ///     computeSampleFunctionInternalOverlap(). Then, with the weight of
  ///     function A in base profile WB(A), and the weight of function A in test
  ///     profile WT(A), compute FS(A) = FIS(A) * (1.0 - fabs(WB(A) - WT(A)))
  ///     ranging in [0.0f to 1.0f] with 0.0 meaning no overlap.
  double
  computeSampleFunctionOverlap(const sampleprof::FunctionSamples *BaseFunc,
                               const sampleprof::FunctionSamples *TestFunc,
                               SampleOverlapStats *FuncOverlap,
                               uint64_t BaseFuncSample,
                               uint64_t TestFuncSample);

  /// Profile-level similarity (PS) is a weighted aggregate over function-level
  /// similarities (FS). This method weights the FS value by the function
  /// weights in the base and test profiles for the aggregation.
  double weightByImportance(double FuncSimilarity, uint64_t BaseFuncSample,
                            uint64_t TestFuncSample) const;
};
} // end anonymous namespace

bool SampleOverlapAggregator::detectZeroSampleProfile(
    raw_fd_ostream &OS) const {
  bool HaveZeroSample = false;
  if (ProfOverlap.BaseSample == 0) {
    OS << "Sum of sample counts for profile " << BaseFilename << " is 0.\n";
    HaveZeroSample = true;
  }
  if (ProfOverlap.TestSample == 0) {
    OS << "Sum of sample counts for profile " << TestFilename << " is 0.\n";
    HaveZeroSample = true;
  }
  return HaveZeroSample;
}

double SampleOverlapAggregator::computeBlockSimilarity(
    uint64_t BaseSample, uint64_t TestSample,
    const SampleOverlapStats &FuncOverlap) const {
  double BaseFrac = 0.0;
  double TestFrac = 0.0;
  if (FuncOverlap.BaseSample > 0)
    BaseFrac = static_cast<double>(BaseSample) / FuncOverlap.BaseSample;
  if (FuncOverlap.TestSample > 0)
    TestFrac = static_cast<double>(TestSample) / FuncOverlap.TestSample;
  return 1.0 - std::fabs(BaseFrac - TestFrac);
}

void SampleOverlapAggregator::updateHotBlockOverlap(uint64_t BaseSample,
                                                    uint64_t TestSample,
                                                    uint64_t HotBlockCount) {
  bool IsBaseHot = (BaseSample >= BaseHotThreshold);
  bool IsTestHot = (TestSample >= TestHotThreshold);
  if (!IsBaseHot && !IsTestHot)
    return;

  HotBlockOverlap.UnionCount += HotBlockCount;
  if (IsBaseHot)
    HotBlockOverlap.BaseCount += HotBlockCount;
  if (IsTestHot)
    HotBlockOverlap.TestCount += HotBlockCount;
  if (IsBaseHot && IsTestHot)
    HotBlockOverlap.OverlapCount += HotBlockCount;
}

void SampleOverlapAggregator::getHotFunctions(
    const FuncSampleStatsMap &ProfStats, FuncSampleStatsMap &HotFunc,
    uint64_t HotThreshold) const {
  for (const auto &F : ProfStats) {
    if (isFunctionHot(F.second, HotThreshold))
      HotFunc.emplace(F.first, F.second);
  }
}

void SampleOverlapAggregator::computeHotFuncOverlap() {
  FuncSampleStatsMap BaseHotFunc;
  getHotFunctions(BaseStats, BaseHotFunc, BaseHotThreshold);
  HotFuncOverlap.BaseCount = BaseHotFunc.size();

  FuncSampleStatsMap TestHotFunc;
  getHotFunctions(TestStats, TestHotFunc, TestHotThreshold);
  HotFuncOverlap.TestCount = TestHotFunc.size();
  HotFuncOverlap.UnionCount = HotFuncOverlap.TestCount;

  for (const auto &F : BaseHotFunc) {
    if (TestHotFunc.count(F.first))
      ++HotFuncOverlap.OverlapCount;
    else
      ++HotFuncOverlap.UnionCount;
  }
}

void SampleOverlapAggregator::updateOverlapStatsForFunction(
    uint64_t BaseSample, uint64_t TestSample, uint64_t HotBlockCount,
    SampleOverlapStats &FuncOverlap, double &Difference, MatchStatus Status) {
  assert(Status != MS_None &&
         "Match status should be updated before updating overlap statistics");
  if (Status == MS_FirstUnique) {
    TestSample = 0;
    FuncOverlap.BaseUniqueSample += BaseSample;
  } else if (Status == MS_SecondUnique) {
    BaseSample = 0;
    FuncOverlap.TestUniqueSample += TestSample;
  } else {
    ++FuncOverlap.OverlapCount;
  }

  FuncOverlap.UnionSample += std::max(BaseSample, TestSample);
  FuncOverlap.OverlapSample += std::min(BaseSample, TestSample);
  Difference +=
      1.0 - computeBlockSimilarity(BaseSample, TestSample, FuncOverlap);
  updateHotBlockOverlap(BaseSample, TestSample, HotBlockCount);
}

void SampleOverlapAggregator::updateForUnmatchedCallee(
    const sampleprof::FunctionSamples &Func, SampleOverlapStats &FuncOverlap,
    double &Difference, MatchStatus Status) {
  assert((Status == MS_FirstUnique || Status == MS_SecondUnique) &&
         "Status must be either of the two unmatched cases");
  FuncSampleStats FuncStats;
  if (Status == MS_FirstUnique) {
    getFuncSampleStats(Func, FuncStats, BaseHotThreshold);
    updateOverlapStatsForFunction(FuncStats.SampleSum, 0,
                                  FuncStats.HotBlockCount, FuncOverlap,
                                  Difference, Status);
  } else {
    getFuncSampleStats(Func, FuncStats, TestHotThreshold);
    updateOverlapStatsForFunction(0, FuncStats.SampleSum,
                                  FuncStats.HotBlockCount, FuncOverlap,
                                  Difference, Status);
  }
}

double SampleOverlapAggregator::computeSampleFunctionInternalOverlap(
    const sampleprof::FunctionSamples &BaseFunc,
    const sampleprof::FunctionSamples &TestFunc,
    SampleOverlapStats &FuncOverlap) {

  using namespace sampleprof;

  double Difference = 0;

  // Accumulate Difference for regular line/block samples in the function.
  // We match them through sort-merge join algorithm because
  // FunctionSamples::getBodySamples() returns a map of sample counters ordered
  // by their offsets.
  MatchStep<BodySampleMap::const_iterator> BlockIterStep(
      BaseFunc.getBodySamples().cbegin(), BaseFunc.getBodySamples().cend(),
      TestFunc.getBodySamples().cbegin(), TestFunc.getBodySamples().cend());
  BlockIterStep.updateOneStep();
  while (!BlockIterStep.areBothFinished()) {
    uint64_t BaseSample =
        BlockIterStep.isFirstFinished()
            ? 0
            : BlockIterStep.getFirstIter()->second.getSamples();
    uint64_t TestSample =
        BlockIterStep.isSecondFinished()
            ? 0
            : BlockIterStep.getSecondIter()->second.getSamples();
    updateOverlapStatsForFunction(BaseSample, TestSample, 1, FuncOverlap,
                                  Difference, BlockIterStep.getMatchStatus());

    BlockIterStep.updateOneStep();
  }

  // Accumulate Difference for callsite lines in the function. We match
  // them through sort-merge algorithm because
  // FunctionSamples::getCallsiteSamples() returns a map of callsite records
  // ordered by their offsets.
  MatchStep<CallsiteSampleMap::const_iterator> CallsiteIterStep(
      BaseFunc.getCallsiteSamples().cbegin(),
      BaseFunc.getCallsiteSamples().cend(),
      TestFunc.getCallsiteSamples().cbegin(),
      TestFunc.getCallsiteSamples().cend());
  CallsiteIterStep.updateOneStep();
  while (!CallsiteIterStep.areBothFinished()) {
    MatchStatus CallsiteStepStatus = CallsiteIterStep.getMatchStatus();
    assert(CallsiteStepStatus != MS_None &&
           "Match status should be updated before entering loop body");

    if (CallsiteStepStatus != MS_Match) {
      auto Callsite = (CallsiteStepStatus == MS_FirstUnique)
                          ? CallsiteIterStep.getFirstIter()
                          : CallsiteIterStep.getSecondIter();
      for (const auto &F : Callsite->second)
        updateForUnmatchedCallee(F.second, FuncOverlap, Difference,
                                 CallsiteStepStatus);
    } else {
      // There may be multiple inlinees at the same offset, so we need to try
      // matching all of them. This match is implemented through sort-merge
      // algorithm because callsite records at the same offset are ordered by
      // function names.
      MatchStep<FunctionSamplesMap::const_iterator> CalleeIterStep(
          CallsiteIterStep.getFirstIter()->second.cbegin(),
          CallsiteIterStep.getFirstIter()->second.cend(),
          CallsiteIterStep.getSecondIter()->second.cbegin(),
          CallsiteIterStep.getSecondIter()->second.cend());
      CalleeIterStep.updateOneStep();
      while (!CalleeIterStep.areBothFinished()) {
        MatchStatus CalleeStepStatus = CalleeIterStep.getMatchStatus();
        if (CalleeStepStatus != MS_Match) {
          auto Callee = (CalleeStepStatus == MS_FirstUnique)
                            ? CalleeIterStep.getFirstIter()
                            : CalleeIterStep.getSecondIter();
          updateForUnmatchedCallee(Callee->second, FuncOverlap, Difference,
                                   CalleeStepStatus);
        } else {
          // An inlined function can contain other inlinees inside, so compute
          // the Difference recursively.
          Difference += 2.0 - 2 * computeSampleFunctionInternalOverlap(
                                      CalleeIterStep.getFirstIter()->second,
                                      CalleeIterStep.getSecondIter()->second,
                                      FuncOverlap);
        }
        CalleeIterStep.updateOneStep();
      }
    }
    CallsiteIterStep.updateOneStep();
  }

  // Difference reflects the total differences of line/block samples in this
  // function and ranges in [0.0f to 2.0f]. Take (2.0 - Difference) / 2 to
  // reflect the similarity between function profiles in [0.0f to 1.0f].
  return (2.0 - Difference) / 2;
}

double SampleOverlapAggregator::weightForFuncSimilarity(
    double FuncInternalSimilarity, uint64_t BaseFuncSample,
    uint64_t TestFuncSample) const {
  // Compute the weight as the distance between the function weights in two
  // profiles.
  double BaseFrac = 0.0;
  double TestFrac = 0.0;
  assert(ProfOverlap.BaseSample > 0 &&
         "Total samples in base profile should be greater than 0");
  BaseFrac = static_cast<double>(BaseFuncSample) / ProfOverlap.BaseSample;
  assert(ProfOverlap.TestSample > 0 &&
         "Total samples in test profile should be greater than 0");
  TestFrac = static_cast<double>(TestFuncSample) / ProfOverlap.TestSample;
  double WeightDistance = std::fabs(BaseFrac - TestFrac);

  // Take WeightDistance into the similarity.
  return FuncInternalSimilarity * (1 - WeightDistance);
}

double
SampleOverlapAggregator::weightByImportance(double FuncSimilarity,
                                            uint64_t BaseFuncSample,
                                            uint64_t TestFuncSample) const {

  double BaseFrac = 0.0;
  double TestFrac = 0.0;
  assert(ProfOverlap.BaseSample > 0 &&
         "Total samples in base profile should be greater than 0");
  BaseFrac = static_cast<double>(BaseFuncSample) / ProfOverlap.BaseSample / 2.0;
  assert(ProfOverlap.TestSample > 0 &&
         "Total samples in test profile should be greater than 0");
  TestFrac = static_cast<double>(TestFuncSample) / ProfOverlap.TestSample / 2.0;
  return FuncSimilarity * (BaseFrac + TestFrac);
}

double SampleOverlapAggregator::computeSampleFunctionOverlap(
    const sampleprof::FunctionSamples *BaseFunc,
    const sampleprof::FunctionSamples *TestFunc,
    SampleOverlapStats *FuncOverlap, uint64_t BaseFuncSample,
    uint64_t TestFuncSample) {
  // Default function internal similarity before weighted, meaning two functions
  // has no overlap.
  const double DefaultFuncInternalSimilarity = 0;
  double FuncSimilarity;
  double FuncInternalSimilarity;

  // If BaseFunc or TestFunc is nullptr, it means the functions do not overlap.
  // In this case, we use DefaultFuncInternalSimilarity as the function internal
  // similarity.
  if (!BaseFunc || !TestFunc) {
    FuncInternalSimilarity = DefaultFuncInternalSimilarity;
  } else {
    assert(FuncOverlap != nullptr &&
           "FuncOverlap should be provided in this case");
    FuncInternalSimilarity = computeSampleFunctionInternalOverlap(
        *BaseFunc, *TestFunc, *FuncOverlap);
    // Now, FuncInternalSimilarity may be a little less than 0 due to
    // imprecision of floating point accumulations. Make it zero if the
    // difference is below Epsilon.
    FuncInternalSimilarity = (std::fabs(FuncInternalSimilarity - 0) < Epsilon)
                                 ? 0
                                 : FuncInternalSimilarity;
  }
  FuncSimilarity = weightForFuncSimilarity(FuncInternalSimilarity,
                                           BaseFuncSample, TestFuncSample);
  return FuncSimilarity;
}

void SampleOverlapAggregator::computeSampleProfileOverlap(raw_fd_ostream &OS) {
  using namespace sampleprof;

  std::unordered_map<SampleContext, const FunctionSamples *,
                     SampleContext::Hash>
      BaseFuncProf;
  const auto &BaseProfiles = BaseReader->getProfiles();
  for (const auto &BaseFunc : BaseProfiles) {
    BaseFuncProf.emplace(BaseFunc.second.getContext(), &(BaseFunc.second));
  }
  ProfOverlap.UnionCount = BaseFuncProf.size();

  const auto &TestProfiles = TestReader->getProfiles();
  for (const auto &TestFunc : TestProfiles) {
    SampleOverlapStats FuncOverlap;
    FuncOverlap.TestName = TestFunc.second.getContext();
    assert(TestStats.count(FuncOverlap.TestName) &&
           "TestStats should have records for all functions in test profile "
           "except inlinees");
    FuncOverlap.TestSample = TestStats[FuncOverlap.TestName].SampleSum;

    bool Matched = false;
    const auto Match = BaseFuncProf.find(FuncOverlap.TestName);
    if (Match == BaseFuncProf.end()) {
      const FuncSampleStats &FuncStats = TestStats[FuncOverlap.TestName];
      ++ProfOverlap.TestUniqueCount;
      ProfOverlap.TestUniqueSample += FuncStats.SampleSum;
      FuncOverlap.TestUniqueSample = FuncStats.SampleSum;

      updateHotBlockOverlap(0, FuncStats.SampleSum, FuncStats.HotBlockCount);

      double FuncSimilarity = computeSampleFunctionOverlap(
          nullptr, nullptr, nullptr, 0, FuncStats.SampleSum);
      ProfOverlap.Similarity +=
          weightByImportance(FuncSimilarity, 0, FuncStats.SampleSum);

      ++ProfOverlap.UnionCount;
      ProfOverlap.UnionSample += FuncStats.SampleSum;
    } else {
      ++ProfOverlap.OverlapCount;

      // Two functions match with each other. Compute function-level overlap and
      // aggregate them into profile-level overlap.
      FuncOverlap.BaseName = Match->second->getContext();
      assert(BaseStats.count(FuncOverlap.BaseName) &&
             "BaseStats should have records for all functions in base profile "
             "except inlinees");
      FuncOverlap.BaseSample = BaseStats[FuncOverlap.BaseName].SampleSum;

      FuncOverlap.Similarity = computeSampleFunctionOverlap(
          Match->second, &TestFunc.second, &FuncOverlap, FuncOverlap.BaseSample,
          FuncOverlap.TestSample);
      ProfOverlap.Similarity +=
          weightByImportance(FuncOverlap.Similarity, FuncOverlap.BaseSample,
                             FuncOverlap.TestSample);
      ProfOverlap.OverlapSample += FuncOverlap.OverlapSample;
      ProfOverlap.UnionSample += FuncOverlap.UnionSample;

      // Accumulate the percentage of base unique and test unique samples into
      // ProfOverlap.
      ProfOverlap.BaseUniqueSample += FuncOverlap.BaseUniqueSample;
      ProfOverlap.TestUniqueSample += FuncOverlap.TestUniqueSample;

      // Remove matched base functions for later reporting functions not found
      // in test profile.
      BaseFuncProf.erase(Match);
      Matched = true;
    }

    // Print function-level similarity information if specified by options.
    assert(TestStats.count(FuncOverlap.TestName) &&
           "TestStats should have records for all functions in test profile "
           "except inlinees");
    if (TestStats[FuncOverlap.TestName].MaxSample >= FuncFilter.ValueCutoff ||
        (Matched && FuncOverlap.Similarity < LowSimilarityThreshold) ||
        (Matched && !FuncFilter.NameFilter.empty() &&
         FuncOverlap.BaseName.toString().find(FuncFilter.NameFilter) !=
             std::string::npos)) {
      assert(ProfOverlap.BaseSample > 0 &&
             "Total samples in base profile should be greater than 0");
      FuncOverlap.BaseWeight =
          static_cast<double>(FuncOverlap.BaseSample) / ProfOverlap.BaseSample;
      assert(ProfOverlap.TestSample > 0 &&
             "Total samples in test profile should be greater than 0");
      FuncOverlap.TestWeight =
          static_cast<double>(FuncOverlap.TestSample) / ProfOverlap.TestSample;
      FuncSimilarityDump.emplace(FuncOverlap.BaseWeight, FuncOverlap);
    }
  }

  // Traverse through functions in base profile but not in test profile.
  for (const auto &F : BaseFuncProf) {
    assert(BaseStats.count(F.second->getContext()) &&
           "BaseStats should have records for all functions in base profile "
           "except inlinees");
    const FuncSampleStats &FuncStats = BaseStats[F.second->getContext()];
    ++ProfOverlap.BaseUniqueCount;
    ProfOverlap.BaseUniqueSample += FuncStats.SampleSum;

    updateHotBlockOverlap(FuncStats.SampleSum, 0, FuncStats.HotBlockCount);

    double FuncSimilarity = computeSampleFunctionOverlap(
        nullptr, nullptr, nullptr, FuncStats.SampleSum, 0);
    ProfOverlap.Similarity +=
        weightByImportance(FuncSimilarity, FuncStats.SampleSum, 0);

    ProfOverlap.UnionSample += FuncStats.SampleSum;
  }

  // Now, ProfSimilarity may be a little greater than 1 due to imprecision
  // of floating point accumulations. Make it 1.0 if the difference is below
  // Epsilon.
  ProfOverlap.Similarity = (std::fabs(ProfOverlap.Similarity - 1) < Epsilon)
                               ? 1
                               : ProfOverlap.Similarity;

  computeHotFuncOverlap();
}

void SampleOverlapAggregator::initializeSampleProfileOverlap() {
  const auto &BaseProf = BaseReader->getProfiles();
  for (const auto &I : BaseProf) {
    ++ProfOverlap.BaseCount;
    FuncSampleStats FuncStats;
    getFuncSampleStats(I.second, FuncStats, BaseHotThreshold);
    ProfOverlap.BaseSample += FuncStats.SampleSum;
    BaseStats.emplace(I.second.getContext(), FuncStats);
  }

  const auto &TestProf = TestReader->getProfiles();
  for (const auto &I : TestProf) {
    ++ProfOverlap.TestCount;
    FuncSampleStats FuncStats;
    getFuncSampleStats(I.second, FuncStats, TestHotThreshold);
    ProfOverlap.TestSample += FuncStats.SampleSum;
    TestStats.emplace(I.second.getContext(), FuncStats);
  }

  ProfOverlap.BaseName = StringRef(BaseFilename);
  ProfOverlap.TestName = StringRef(TestFilename);
}

void SampleOverlapAggregator::dumpFuncSimilarity(raw_fd_ostream &OS) const {
  using namespace sampleprof;

  if (FuncSimilarityDump.empty())
    return;

  formatted_raw_ostream FOS(OS);
  FOS << "Function-level details:\n";
  FOS << "Base weight";
  FOS.PadToColumn(TestWeightCol);
  FOS << "Test weight";
  FOS.PadToColumn(SimilarityCol);
  FOS << "Similarity";
  FOS.PadToColumn(OverlapCol);
  FOS << "Overlap";
  FOS.PadToColumn(BaseUniqueCol);
  FOS << "Base unique";
  FOS.PadToColumn(TestUniqueCol);
  FOS << "Test unique";
  FOS.PadToColumn(BaseSampleCol);
  FOS << "Base samples";
  FOS.PadToColumn(TestSampleCol);
  FOS << "Test samples";
  FOS.PadToColumn(FuncNameCol);
  FOS << "Function name\n";
  for (const auto &F : FuncSimilarityDump) {
    double OverlapPercent =
        F.second.UnionSample > 0
            ? static_cast<double>(F.second.OverlapSample) / F.second.UnionSample
            : 0;
    double BaseUniquePercent =
        F.second.BaseSample > 0
            ? static_cast<double>(F.second.BaseUniqueSample) /
                  F.second.BaseSample
            : 0;
    double TestUniquePercent =
        F.second.TestSample > 0
            ? static_cast<double>(F.second.TestUniqueSample) /
                  F.second.TestSample
            : 0;

    FOS << format("%.2f%%", F.second.BaseWeight * 100);
    FOS.PadToColumn(TestWeightCol);
    FOS << format("%.2f%%", F.second.TestWeight * 100);
    FOS.PadToColumn(SimilarityCol);
    FOS << format("%.2f%%", F.second.Similarity * 100);
    FOS.PadToColumn(OverlapCol);
    FOS << format("%.2f%%", OverlapPercent * 100);
    FOS.PadToColumn(BaseUniqueCol);
    FOS << format("%.2f%%", BaseUniquePercent * 100);
    FOS.PadToColumn(TestUniqueCol);
    FOS << format("%.2f%%", TestUniquePercent * 100);
    FOS.PadToColumn(BaseSampleCol);
    FOS << F.second.BaseSample;
    FOS.PadToColumn(TestSampleCol);
    FOS << F.second.TestSample;
    FOS.PadToColumn(FuncNameCol);
    FOS << F.second.TestName.toString() << "\n";
  }
}

void SampleOverlapAggregator::dumpProgramSummary(raw_fd_ostream &OS) const {
  OS << "Profile overlap infomation for base_profile: "
     << ProfOverlap.BaseName.toString()
     << " and test_profile: " << ProfOverlap.TestName.toString()
     << "\nProgram level:\n";

  OS << "  Whole program profile similarity: "
     << format("%.3f%%", ProfOverlap.Similarity * 100) << "\n";

  assert(ProfOverlap.UnionSample > 0 &&
         "Total samples in two profile should be greater than 0");
  double OverlapPercent =
      static_cast<double>(ProfOverlap.OverlapSample) / ProfOverlap.UnionSample;
  assert(ProfOverlap.BaseSample > 0 &&
         "Total samples in base profile should be greater than 0");
  double BaseUniquePercent = static_cast<double>(ProfOverlap.BaseUniqueSample) /
                             ProfOverlap.BaseSample;
  assert(ProfOverlap.TestSample > 0 &&
         "Total samples in test profile should be greater than 0");
  double TestUniquePercent = static_cast<double>(ProfOverlap.TestUniqueSample) /
                             ProfOverlap.TestSample;

  OS << "  Whole program sample overlap: "
     << format("%.3f%%", OverlapPercent * 100) << "\n";
  OS << "    percentage of samples unique in base profile: "
     << format("%.3f%%", BaseUniquePercent * 100) << "\n";
  OS << "    percentage of samples unique in test profile: "
     << format("%.3f%%", TestUniquePercent * 100) << "\n";
  OS << "    total samples in base profile: " << ProfOverlap.BaseSample << "\n"
     << "    total samples in test profile: " << ProfOverlap.TestSample << "\n";

  assert(ProfOverlap.UnionCount > 0 &&
         "There should be at least one function in two input profiles");
  double FuncOverlapPercent =
      static_cast<double>(ProfOverlap.OverlapCount) / ProfOverlap.UnionCount;
  OS << "  Function overlap: " << format("%.3f%%", FuncOverlapPercent * 100)
     << "\n";
  OS << "    overlap functions: " << ProfOverlap.OverlapCount << "\n";
  OS << "    functions unique in base profile: " << ProfOverlap.BaseUniqueCount
     << "\n";
  OS << "    functions unique in test profile: " << ProfOverlap.TestUniqueCount
     << "\n";
}

void SampleOverlapAggregator::dumpHotFuncAndBlockOverlap(
    raw_fd_ostream &OS) const {
  assert(HotFuncOverlap.UnionCount > 0 &&
         "There should be at least one hot function in two input profiles");
  OS << "  Hot-function overlap: "
     << format("%.3f%%", static_cast<double>(HotFuncOverlap.OverlapCount) /
                             HotFuncOverlap.UnionCount * 100)
     << "\n";
  OS << "    overlap hot functions: " << HotFuncOverlap.OverlapCount << "\n";
  OS << "    hot functions unique in base profile: "
     << HotFuncOverlap.BaseCount - HotFuncOverlap.OverlapCount << "\n";
  OS << "    hot functions unique in test profile: "
     << HotFuncOverlap.TestCount - HotFuncOverlap.OverlapCount << "\n";

  assert(HotBlockOverlap.UnionCount > 0 &&
         "There should be at least one hot block in two input profiles");
  OS << "  Hot-block overlap: "
     << format("%.3f%%", static_cast<double>(HotBlockOverlap.OverlapCount) /
                             HotBlockOverlap.UnionCount * 100)
     << "\n";
  OS << "    overlap hot blocks: " << HotBlockOverlap.OverlapCount << "\n";
  OS << "    hot blocks unique in base profile: "
     << HotBlockOverlap.BaseCount - HotBlockOverlap.OverlapCount << "\n";
  OS << "    hot blocks unique in test profile: "
     << HotBlockOverlap.TestCount - HotBlockOverlap.OverlapCount << "\n";
}

std::error_code SampleOverlapAggregator::loadProfiles() {
  using namespace sampleprof;

  LLVMContext Context;
  auto FS = vfs::getRealFileSystem();
  auto BaseReaderOrErr = SampleProfileReader::create(BaseFilename, Context, *FS,
                                                     FSDiscriminatorPassOption);
  if (std::error_code EC = BaseReaderOrErr.getError())
    exitWithErrorCode(EC, BaseFilename);

  auto TestReaderOrErr = SampleProfileReader::create(TestFilename, Context, *FS,
                                                     FSDiscriminatorPassOption);
  if (std::error_code EC = TestReaderOrErr.getError())
    exitWithErrorCode(EC, TestFilename);

  BaseReader = std::move(BaseReaderOrErr.get());
  TestReader = std::move(TestReaderOrErr.get());

  if (std::error_code EC = BaseReader->read())
    exitWithErrorCode(EC, BaseFilename);
  if (std::error_code EC = TestReader->read())
    exitWithErrorCode(EC, TestFilename);
  if (BaseReader->profileIsProbeBased() != TestReader->profileIsProbeBased())
    exitWithError(
        "cannot compare probe-based profile with non-probe-based profile");
  if (BaseReader->profileIsCS() != TestReader->profileIsCS())
    exitWithError("cannot compare CS profile with non-CS profile");

  // Load BaseHotThreshold and TestHotThreshold as 99-percentile threshold in
  // profile summary.
  ProfileSummary &BasePS = BaseReader->getSummary();
  ProfileSummary &TestPS = TestReader->getSummary();
  BaseHotThreshold =
      ProfileSummaryBuilder::getHotCountThreshold(BasePS.getDetailedSummary());
  TestHotThreshold =
      ProfileSummaryBuilder::getHotCountThreshold(TestPS.getDetailedSummary());

  return std::error_code();
}

void overlapSampleProfile(const std::string &BaseFilename,
                          const std::string &TestFilename,
                          const OverlapFuncFilters &FuncFilter,
                          uint64_t SimilarityCutoff, raw_fd_ostream &OS) {
  using namespace sampleprof;

  // We use 0.000005 to initialize OverlapAggr.Epsilon because the final metrics
  // report 2--3 places after decimal point in percentage numbers.
  SampleOverlapAggregator OverlapAggr(
      BaseFilename, TestFilename,
      static_cast<double>(SimilarityCutoff) / 1000000, 0.000005, FuncFilter);
  if (std::error_code EC = OverlapAggr.loadProfiles())
    exitWithErrorCode(EC);

  OverlapAggr.initializeSampleProfileOverlap();
  if (OverlapAggr.detectZeroSampleProfile(OS))
    return;

  OverlapAggr.computeSampleProfileOverlap(OS);

  OverlapAggr.dumpProgramSummary(OS);
  OverlapAggr.dumpHotFuncAndBlockOverlap(OS);
  OverlapAggr.dumpFuncSimilarity(OS);
}

static int overlap_main() {
  std::error_code EC;
  raw_fd_ostream OS(OutputFilename.data(), EC, sys::fs::OF_TextWithCRLF);
  if (EC)
    exitWithErrorCode(EC, OutputFilename);

  if (ProfileKind == instr)
    overlapInstrProfile(BaseFilename, TestFilename,
                        OverlapFuncFilters{OverlapValueCutoff, FuncNameFilter},
                        OS, IsCS);
  else
    overlapSampleProfile(BaseFilename, TestFilename,
                         OverlapFuncFilters{OverlapValueCutoff, FuncNameFilter},
                         SimilarityCutoff, OS);

  return 0;
}

namespace {
struct ValueSitesStats {
  ValueSitesStats() = default;
  uint64_t TotalNumValueSites = 0;
  uint64_t TotalNumValueSitesWithValueProfile = 0;
  uint64_t TotalNumValues = 0;
  std::vector<unsigned> ValueSitesHistogram;
};
} // namespace

static void traverseAllValueSites(const InstrProfRecord &Func, uint32_t VK,
                                  ValueSitesStats &Stats, raw_fd_ostream &OS,
                                  InstrProfSymtab *Symtab) {
  uint32_t NS = Func.getNumValueSites(VK);
  Stats.TotalNumValueSites += NS;
  for (size_t I = 0; I < NS; ++I) {
    auto VD = Func.getValueArrayForSite(VK, I);
    uint32_t NV = VD.size();
    if (NV == 0)
      continue;
    Stats.TotalNumValues += NV;
    Stats.TotalNumValueSitesWithValueProfile++;
    if (NV > Stats.ValueSitesHistogram.size())
      Stats.ValueSitesHistogram.resize(NV, 0);
    Stats.ValueSitesHistogram[NV - 1]++;

    uint64_t SiteSum = 0;
    for (const auto &V : VD)
      SiteSum += V.Count;
    if (SiteSum == 0)
      SiteSum = 1;

    for (const auto &V : VD) {
      OS << "\t[ " << format("%2u", I) << ", ";
      if (Symtab == nullptr)
        OS << format("%4" PRIu64, V.Value);
      else
        OS << Symtab->getFuncOrVarName(V.Value);
      OS << ", " << format("%10" PRId64, V.Count) << " ] ("
         << format("%.2f%%", (V.Count * 100.0 / SiteSum)) << ")\n";
    }
  }
}

static void showValueSitesStats(raw_fd_ostream &OS, uint32_t VK,
                                ValueSitesStats &Stats) {
  OS << "  Total number of sites: " << Stats.TotalNumValueSites << "\n";
  OS << "  Total number of sites with values: "
     << Stats.TotalNumValueSitesWithValueProfile << "\n";
  OS << "  Total number of profiled values: " << Stats.TotalNumValues << "\n";

  OS << "  Value sites histogram:\n\tNumTargets, SiteCount\n";
  for (unsigned I = 0; I < Stats.ValueSitesHistogram.size(); I++) {
    if (Stats.ValueSitesHistogram[I] > 0)
      OS << "\t" << I + 1 << ", " << Stats.ValueSitesHistogram[I] << "\n";
  }
}

static int showInstrProfile(ShowFormat SFormat, raw_fd_ostream &OS) {
  if (SFormat == ShowFormat::Json)
    exitWithError("JSON output is not supported for instr profiles");
  if (SFormat == ShowFormat::Yaml)
    exitWithError("YAML output is not supported for instr profiles");
  auto FS = vfs::getRealFileSystem();
  auto ReaderOrErr = InstrProfReader::create(Filename, *FS);
  std::vector<uint32_t> Cutoffs = std::move(DetailedSummaryCutoffs);
  if (ShowDetailedSummary && Cutoffs.empty()) {
    Cutoffs = ProfileSummaryBuilder::DefaultCutoffs;
  }
  InstrProfSummaryBuilder Builder(std::move(Cutoffs));
  if (Error E = ReaderOrErr.takeError())
    exitWithError(std::move(E), Filename);

  auto Reader = std::move(ReaderOrErr.get());
  bool IsIRInstr = Reader->isIRLevelProfile();
  size_t ShownFunctions = 0;
  size_t BelowCutoffFunctions = 0;
  int NumVPKind = IPVK_Last - IPVK_First + 1;
  std::vector<ValueSitesStats> VPStats(NumVPKind);

  auto MinCmp = [](const std::pair<std::string, uint64_t> &v1,
                   const std::pair<std::string, uint64_t> &v2) {
    return v1.second > v2.second;
  };

  std::priority_queue<std::pair<std::string, uint64_t>,
                      std::vector<std::pair<std::string, uint64_t>>,
                      decltype(MinCmp)>
      HottestFuncs(MinCmp);

  if (!TextFormat && OnlyListBelow) {
    OS << "The list of functions with the maximum counter less than "
       << ShowValueCutoff << ":\n";
  }

  // Add marker so that IR-level instrumentation round-trips properly.
  if (TextFormat && IsIRInstr)
    OS << ":ir\n";

  for (const auto &Func : *Reader) {
    if (Reader->isIRLevelProfile()) {
      bool FuncIsCS = NamedInstrProfRecord::hasCSFlagInHash(Func.Hash);
      if (FuncIsCS != ShowCS)
        continue;
    }
    bool Show = ShowAllFunctions ||
                (!FuncNameFilter.empty() && Func.Name.contains(FuncNameFilter));

    bool doTextFormatDump = (Show && TextFormat);

    if (doTextFormatDump) {
      InstrProfSymtab &Symtab = Reader->getSymtab();
      InstrProfWriter::writeRecordInText(Func.Name, Func.Hash, Func, Symtab,
                                         OS);
      continue;
    }

    assert(Func.Counts.size() > 0 && "function missing entry counter");
    Builder.addRecord(Func);

    if (ShowCovered) {
      if (llvm::any_of(Func.Counts, [](uint64_t C) { return C; }))
        OS << Func.Name << "\n";
      continue;
    }

    uint64_t FuncMax = 0;
    uint64_t FuncSum = 0;

    auto PseudoKind = Func.getCountPseudoKind();
    if (PseudoKind != InstrProfRecord::NotPseudo) {
      if (Show) {
        if (!ShownFunctions)
          OS << "Counters:\n";
        ++ShownFunctions;
        OS << "  " << Func.Name << ":\n"
           << "    Hash: " << format("0x%016" PRIx64, Func.Hash) << "\n"
           << "    Counters: " << Func.Counts.size();
        if (PseudoKind == InstrProfRecord::PseudoHot)
          OS << "    <PseudoHot>\n";
        else if (PseudoKind == InstrProfRecord::PseudoWarm)
          OS << "    <PseudoWarm>\n";
        else
          llvm_unreachable("Unknown PseudoKind");
      }
      continue;
    }

    for (size_t I = 0, E = Func.Counts.size(); I < E; ++I) {
      FuncMax = std::max(FuncMax, Func.Counts[I]);
      FuncSum += Func.Counts[I];
    }

    if (FuncMax < ShowValueCutoff) {
      ++BelowCutoffFunctions;
      if (OnlyListBelow) {
        OS << "  " << Func.Name << ": (Max = " << FuncMax
           << " Sum = " << FuncSum << ")\n";
      }
      continue;
    } else if (OnlyListBelow)
      continue;

    if (TopNFunctions) {
      if (HottestFuncs.size() == TopNFunctions) {
        if (HottestFuncs.top().second < FuncMax) {
          HottestFuncs.pop();
          HottestFuncs.emplace(std::make_pair(std::string(Func.Name), FuncMax));
        }
      } else
        HottestFuncs.emplace(std::make_pair(std::string(Func.Name), FuncMax));
    }

    if (Show) {
      if (!ShownFunctions)
        OS << "Counters:\n";

      ++ShownFunctions;

      OS << "  " << Func.Name << ":\n"
         << "    Hash: " << format("0x%016" PRIx64, Func.Hash) << "\n"
         << "    Counters: " << Func.Counts.size() << "\n";
      if (!IsIRInstr)
        OS << "    Function count: " << Func.Counts[0] << "\n";

      if (ShowIndirectCallTargets)
        OS << "    Indirect Call Site Count: "
           << Func.getNumValueSites(IPVK_IndirectCallTarget) << "\n";

      if (ShowVTables)
        OS << "    Number of instrumented vtables: "
           << Func.getNumValueSites(IPVK_VTableTarget) << "\n";

      uint32_t NumMemOPCalls = Func.getNumValueSites(IPVK_MemOPSize);
      if (ShowMemOPSizes && NumMemOPCalls > 0)
        OS << "    Number of Memory Intrinsics Calls: " << NumMemOPCalls
           << "\n";

      if (ShowCounts) {
        OS << "    Block counts: [";
        size_t Start = (IsIRInstr ? 0 : 1);
        for (size_t I = Start, E = Func.Counts.size(); I < E; ++I) {
          OS << (I == Start ? "" : ", ") << Func.Counts[I];
        }
        OS << "]\n";
      }

      if (ShowIndirectCallTargets) {
        OS << "    Indirect Target Results:\n";
        traverseAllValueSites(Func, IPVK_IndirectCallTarget,
                              VPStats[IPVK_IndirectCallTarget], OS,
                              &(Reader->getSymtab()));
      }

      if (ShowVTables) {
        OS << "    VTable Results:\n";
        traverseAllValueSites(Func, IPVK_VTableTarget,
                              VPStats[IPVK_VTableTarget], OS,
                              &(Reader->getSymtab()));
      }

      if (ShowMemOPSizes && NumMemOPCalls > 0) {
        OS << "    Memory Intrinsic Size Results:\n";
        traverseAllValueSites(Func, IPVK_MemOPSize, VPStats[IPVK_MemOPSize], OS,
                              nullptr);
      }
    }
  }
  if (Reader->hasError())
    exitWithError(Reader->getError(), Filename);

  if (TextFormat || ShowCovered)
    return 0;
  std::unique_ptr<ProfileSummary> PS(Builder.getSummary());
  bool IsIR = Reader->isIRLevelProfile();
  OS << "Instrumentation level: " << (IsIR ? "IR" : "Front-end");
  if (IsIR)
    OS << "  entry_first = " << Reader->instrEntryBBEnabled();
  OS << "\n";
  if (ShowAllFunctions || !FuncNameFilter.empty())
    OS << "Functions shown: " << ShownFunctions << "\n";
  OS << "Total functions: " << PS->getNumFunctions() << "\n";
  if (ShowValueCutoff > 0) {
    OS << "Number of functions with maximum count (< " << ShowValueCutoff
       << "): " << BelowCutoffFunctions << "\n";
    OS << "Number of functions with maximum count (>= " << ShowValueCutoff
       << "): " << PS->getNumFunctions() - BelowCutoffFunctions << "\n";
  }
  OS << "Maximum function count: " << PS->getMaxFunctionCount() << "\n";
  OS << "Maximum internal block count: " << PS->getMaxInternalCount() << "\n";

  if (TopNFunctions) {
    std::vector<std::pair<std::string, uint64_t>> SortedHottestFuncs;
    while (!HottestFuncs.empty()) {
      SortedHottestFuncs.emplace_back(HottestFuncs.top());
      HottestFuncs.pop();
    }
    OS << "Top " << TopNFunctions
       << " functions with the largest internal block counts: \n";
    for (auto &hotfunc : llvm::reverse(SortedHottestFuncs))
      OS << "  " << hotfunc.first << ", max count = " << hotfunc.second << "\n";
  }

  if (ShownFunctions && ShowIndirectCallTargets) {
    OS << "Statistics for indirect call sites profile:\n";
    showValueSitesStats(OS, IPVK_IndirectCallTarget,
                        VPStats[IPVK_IndirectCallTarget]);
  }

  if (ShownFunctions && ShowVTables) {
    OS << "Statistics for vtable profile:\n";
    showValueSitesStats(OS, IPVK_VTableTarget, VPStats[IPVK_VTableTarget]);
  }

  if (ShownFunctions && ShowMemOPSizes) {
    OS << "Statistics for memory intrinsic calls sizes profile:\n";
    showValueSitesStats(OS, IPVK_MemOPSize, VPStats[IPVK_MemOPSize]);
  }

  if (ShowDetailedSummary) {
    OS << "Total number of blocks: " << PS->getNumCounts() << "\n";
    OS << "Total count: " << PS->getTotalCount() << "\n";
    PS->printDetailedSummary(OS);
  }

  if (ShowBinaryIds)
    if (Error E = Reader->printBinaryIds(OS))
      exitWithError(std::move(E), Filename);

  if (ShowProfileVersion)
    OS << "Profile version: " << Reader->getVersion() << "\n";

  if (ShowTemporalProfTraces) {
    auto &Traces = Reader->getTemporalProfTraces();
    OS << "Temporal Profile Traces (samples=" << Traces.size()
       << " seen=" << Reader->getTemporalProfTraceStreamSize() << "):\n";
    for (unsigned i = 0; i < Traces.size(); i++) {
      OS << "  Temporal Profile Trace " << i << " (weight=" << Traces[i].Weight
         << " count=" << Traces[i].FunctionNameRefs.size() << "):\n";
      for (auto &NameRef : Traces[i].FunctionNameRefs)
        OS << "    " << Reader->getSymtab().getFuncOrVarName(NameRef) << "\n";
    }
  }

  return 0;
}

static void showSectionInfo(sampleprof::SampleProfileReader *Reader,
                            raw_fd_ostream &OS) {
  if (!Reader->dumpSectionInfo(OS)) {
    WithColor::warning() << "-show-sec-info-only is only supported for "
                         << "sample profile in extbinary format and is "
                         << "ignored for other formats.\n";
    return;
  }
}

namespace {
struct HotFuncInfo {
  std::string FuncName;
  uint64_t TotalCount = 0;
  double TotalCountPercent = 0.0f;
  uint64_t MaxCount = 0;
  uint64_t EntryCount = 0;

  HotFuncInfo() = default;

  HotFuncInfo(StringRef FN, uint64_t TS, double TSP, uint64_t MS, uint64_t ES)
      : FuncName(FN.begin(), FN.end()), TotalCount(TS), TotalCountPercent(TSP),
        MaxCount(MS), EntryCount(ES) {}
};
} // namespace

// Print out detailed information about hot functions in PrintValues vector.
// Users specify titles and offset of every columns through ColumnTitle and
// ColumnOffset. The size of ColumnTitle and ColumnOffset need to be the same
// and at least 4. Besides, users can optionally give a HotFuncMetric string to
// print out or let it be an empty string.
static void dumpHotFunctionList(const std::vector<std::string> &ColumnTitle,
                                const std::vector<int> &ColumnOffset,
                                const std::vector<HotFuncInfo> &PrintValues,
                                uint64_t HotFuncCount, uint64_t TotalFuncCount,
                                uint64_t HotProfCount, uint64_t TotalProfCount,
                                const std::string &HotFuncMetric,
                                uint32_t TopNFunctions, raw_fd_ostream &OS) {
  assert(ColumnOffset.size() == ColumnTitle.size() &&
         "ColumnOffset and ColumnTitle should have the same size");
  assert(ColumnTitle.size() >= 4 &&
         "ColumnTitle should have at least 4 elements");
  assert(TotalFuncCount > 0 &&
         "There should be at least one function in the profile");
  double TotalProfPercent = 0;
  if (TotalProfCount > 0)
    TotalProfPercent = static_cast<double>(HotProfCount) / TotalProfCount * 100;

  formatted_raw_ostream FOS(OS);
  FOS << HotFuncCount << " out of " << TotalFuncCount
      << " functions with profile ("
      << format("%.2f%%",
                (static_cast<double>(HotFuncCount) / TotalFuncCount * 100))
      << ") are considered hot functions";
  if (!HotFuncMetric.empty())
    FOS << " (" << HotFuncMetric << ")";
  FOS << ".\n";
  FOS << HotProfCount << " out of " << TotalProfCount << " profile counts ("
      << format("%.2f%%", TotalProfPercent) << ") are from hot functions.\n";

  for (size_t I = 0; I < ColumnTitle.size(); ++I) {
    FOS.PadToColumn(ColumnOffset[I]);
    FOS << ColumnTitle[I];
  }
  FOS << "\n";

  uint32_t Count = 0;
  for (const auto &R : PrintValues) {
    if (TopNFunctions && (Count++ == TopNFunctions))
      break;
    FOS.PadToColumn(ColumnOffset[0]);
    FOS << R.TotalCount << " (" << format("%.2f%%", R.TotalCountPercent) << ")";
    FOS.PadToColumn(ColumnOffset[1]);
    FOS << R.MaxCount;
    FOS.PadToColumn(ColumnOffset[2]);
    FOS << R.EntryCount;
    FOS.PadToColumn(ColumnOffset[3]);
    FOS << R.FuncName << "\n";
  }
}

static int showHotFunctionList(const sampleprof::SampleProfileMap &Profiles,
                               ProfileSummary &PS, uint32_t TopN,
                               raw_fd_ostream &OS) {
  using namespace sampleprof;

  const uint32_t HotFuncCutoff = 990000;
  auto &SummaryVector = PS.getDetailedSummary();
  uint64_t MinCountThreshold = 0;
  for (const ProfileSummaryEntry &SummaryEntry : SummaryVector) {
    if (SummaryEntry.Cutoff == HotFuncCutoff) {
      MinCountThreshold = SummaryEntry.MinCount;
      break;
    }
  }

  // Traverse all functions in the profile and keep only hot functions.
  // The following loop also calculates the sum of total samples of all
  // functions.
  std::multimap<uint64_t, std::pair<const FunctionSamples *, const uint64_t>,
                std::greater<uint64_t>>
      HotFunc;
  uint64_t ProfileTotalSample = 0;
  uint64_t HotFuncSample = 0;
  uint64_t HotFuncCount = 0;

  for (const auto &I : Profiles) {
    FuncSampleStats FuncStats;
    const FunctionSamples &FuncProf = I.second;
    ProfileTotalSample += FuncProf.getTotalSamples();
    getFuncSampleStats(FuncProf, FuncStats, MinCountThreshold);

    if (isFunctionHot(FuncStats, MinCountThreshold)) {
      HotFunc.emplace(FuncProf.getTotalSamples(),
                      std::make_pair(&(I.second), FuncStats.MaxSample));
      HotFuncSample += FuncProf.getTotalSamples();
      ++HotFuncCount;
    }
  }

  std::vector<std::string> ColumnTitle{"Total sample (%)", "Max sample",
                                       "Entry sample", "Function name"};
  std::vector<int> ColumnOffset{0, 24, 42, 58};
  std::string Metric =
      std::string("max sample >= ") + std::to_string(MinCountThreshold);
  std::vector<HotFuncInfo> PrintValues;
  for (const auto &FuncPair : HotFunc) {
    const FunctionSamples &Func = *FuncPair.second.first;
    double TotalSamplePercent =
        (ProfileTotalSample > 0)
            ? (Func.getTotalSamples() * 100.0) / ProfileTotalSample
            : 0;
    PrintValues.emplace_back(
        HotFuncInfo(Func.getContext().toString(), Func.getTotalSamples(),
                    TotalSamplePercent, FuncPair.second.second,
                    Func.getHeadSamplesEstimate()));
  }
  dumpHotFunctionList(ColumnTitle, ColumnOffset, PrintValues, HotFuncCount,
                      Profiles.size(), HotFuncSample, ProfileTotalSample,
                      Metric, TopN, OS);

  return 0;
}

static int showSampleProfile(ShowFormat SFormat, raw_fd_ostream &OS) {
  if (SFormat == ShowFormat::Yaml)
    exitWithError("YAML output is not supported for sample profiles");
  using namespace sampleprof;
  LLVMContext Context;
  auto FS = vfs::getRealFileSystem();
  auto ReaderOrErr = SampleProfileReader::create(Filename, Context, *FS,
                                                 FSDiscriminatorPassOption);
  if (std::error_code EC = ReaderOrErr.getError())
    exitWithErrorCode(EC, Filename);

  auto Reader = std::move(ReaderOrErr.get());
  if (ShowSectionInfoOnly) {
    showSectionInfo(Reader.get(), OS);
    return 0;
  }

  if (std::error_code EC = Reader->read())
    exitWithErrorCode(EC, Filename);

  if (ShowAllFunctions || FuncNameFilter.empty()) {
    if (SFormat == ShowFormat::Json)
      Reader->dumpJson(OS);
    else
      Reader->dump(OS);
  } else {
    if (SFormat == ShowFormat::Json)
      exitWithError(
          "the JSON format is supported only when all functions are to "
          "be printed");

    // TODO: parse context string to support filtering by contexts.
    FunctionSamples *FS = Reader->getSamplesFor(StringRef(FuncNameFilter));
    Reader->dumpFunctionProfile(FS ? *FS : FunctionSamples(), OS);
  }

  if (ShowProfileSymbolList) {
    std::unique_ptr<sampleprof::ProfileSymbolList> ReaderList =
        Reader->getProfileSymbolList();
    ReaderList->dump(OS);
  }

  if (ShowDetailedSummary) {
    auto &PS = Reader->getSummary();
    PS.printSummary(OS);
    PS.printDetailedSummary(OS);
  }

  if (ShowHotFuncList || TopNFunctions)
    showHotFunctionList(Reader->getProfiles(), Reader->getSummary(),
                        TopNFunctions, OS);

  return 0;
}

static int showMemProfProfile(ShowFormat SFormat, raw_fd_ostream &OS) {
  if (SFormat == ShowFormat::Json)
    exitWithError("JSON output is not supported for MemProf");
  auto ReaderOr = llvm::memprof::RawMemProfReader::create(
      Filename, ProfiledBinary, /*KeepNames=*/true);
  if (Error E = ReaderOr.takeError())
    // Since the error can be related to the profile or the binary we do not
    // pass whence. Instead additional context is provided where necessary in
    // the error message.
    exitWithError(std::move(E), /*Whence*/ "");

  std::unique_ptr<llvm::memprof::RawMemProfReader> Reader(
      ReaderOr.get().release());

  Reader->printYAML(OS);
  return 0;
}

static int showDebugInfoCorrelation(const std::string &Filename,
                                    ShowFormat SFormat, raw_fd_ostream &OS) {
  if (SFormat == ShowFormat::Json)
    exitWithError("JSON output is not supported for debug info correlation");
  std::unique_ptr<InstrProfCorrelator> Correlator;
  if (auto Err =
          InstrProfCorrelator::get(Filename, InstrProfCorrelator::DEBUG_INFO)
              .moveInto(Correlator))
    exitWithError(std::move(Err), Filename);
  if (SFormat == ShowFormat::Yaml) {
    if (auto Err = Correlator->dumpYaml(MaxDbgCorrelationWarnings, OS))
      exitWithError(std::move(Err), Filename);
    return 0;
  }

  if (auto Err = Correlator->correlateProfileData(MaxDbgCorrelationWarnings))
    exitWithError(std::move(Err), Filename);

  InstrProfSymtab Symtab;
  if (auto Err = Symtab.create(
          StringRef(Correlator->getNamesPointer(), Correlator->getNamesSize())))
    exitWithError(std::move(Err), Filename);

  if (ShowProfileSymbolList)
    Symtab.dumpNames(OS);
  // TODO: Read "Profile Data Type" from debug info to compute and show how many
  // counters the section holds.
  if (ShowDetailedSummary)
    OS << "Counters section size: 0x"
       << Twine::utohexstr(Correlator->getCountersSectionSize()) << " bytes\n";
  OS << "Found " << Correlator->getDataSize() << " functions\n";

  return 0;
}

static int show_main(StringRef ProgName) {
  if (Filename.empty() && DebugInfoFilename.empty())
    exitWithError(
        "the positional argument '<profdata-file>' is required unless '--" +
        DebugInfoFilename.ArgStr + "' is provided");

  if (Filename == OutputFilename) {
    errs() << ProgName
           << " show: Input file name cannot be the same as the output file "
              "name!\n";
    return 1;
  }
  if (JsonFormat)
    SFormat = ShowFormat::Json;

  std::error_code EC;
  raw_fd_ostream OS(OutputFilename.data(), EC, sys::fs::OF_TextWithCRLF);
  if (EC)
    exitWithErrorCode(EC, OutputFilename);

  if (ShowAllFunctions && !FuncNameFilter.empty())
    WithColor::warning() << "-function argument ignored: showing all functions\n";

  if (!DebugInfoFilename.empty())
    return showDebugInfoCorrelation(DebugInfoFilename, SFormat, OS);

  if (ShowProfileKind == instr)
    return showInstrProfile(SFormat, OS);
  if (ShowProfileKind == sample)
    return showSampleProfile(SFormat, OS);
  return showMemProfProfile(SFormat, OS);
}

static int order_main() {
  std::error_code EC;
  raw_fd_ostream OS(OutputFilename.data(), EC, sys::fs::OF_TextWithCRLF);
  if (EC)
    exitWithErrorCode(EC, OutputFilename);
  auto FS = vfs::getRealFileSystem();
  auto ReaderOrErr = InstrProfReader::create(Filename, *FS);
  if (Error E = ReaderOrErr.takeError())
    exitWithError(std::move(E), Filename);

  auto Reader = std::move(ReaderOrErr.get());
  for (auto &I : *Reader) {
    // Read all entries
    (void)I;
  }
  ArrayRef Traces = Reader->getTemporalProfTraces();
  if (NumTestTraces && NumTestTraces >= Traces.size())
    exitWithError(
        "--" + NumTestTraces.ArgStr +
        " must be smaller than the total number of traces: expected: < " +
        Twine(Traces.size()) + ", actual: " + Twine(NumTestTraces));
  ArrayRef TestTraces = Traces.take_back(NumTestTraces);
  Traces = Traces.drop_back(NumTestTraces);

  std::vector<BPFunctionNode> Nodes;
  TemporalProfTraceTy::createBPFunctionNodes(Traces, Nodes);
  BalancedPartitioningConfig Config;
  BalancedPartitioning BP(Config);
  BP.run(Nodes);

  OS << "# Ordered " << Nodes.size() << " functions\n";
  if (!TestTraces.empty()) {
    // Since we don't know the symbol sizes, we assume 32 functions per page.
    DenseMap<BPFunctionNode::IDT, unsigned> IdToPageNumber;
    for (auto &Node : Nodes)
      IdToPageNumber[Node.Id] = IdToPageNumber.size() / 32;

    SmallSet<unsigned, 0> TouchedPages;
    unsigned Area = 0;
    for (auto &Trace : TestTraces) {
      for (auto Id : Trace.FunctionNameRefs) {
        auto It = IdToPageNumber.find(Id);
        if (It == IdToPageNumber.end())
          continue;
        TouchedPages.insert(It->getSecond());
        Area += TouchedPages.size();
      }
      TouchedPages.clear();
    }
    OS << "# Total area under the page fault curve: " << (float)Area << "\n";
  }
  OS << "# Warning: Mach-O may prefix symbols with \"_\" depending on the "
        "linkage and this output does not take that into account. Some "
        "post-processing may be required before passing to the linker via "
        "-order_file.\n";
  for (auto &N : Nodes) {
    auto [Filename, ParsedFuncName] =
        getParsedIRPGOName(Reader->getSymtab().getFuncOrVarName(N.Id));
    if (!Filename.empty())
      OS << "# " << Filename << "\n";
    OS << ParsedFuncName << "\n";
  }
  return 0;
}

int llvm_profdata_main(int argc, char **argvNonConst,
                       const llvm::ToolContext &) {
  const char **argv = const_cast<const char **>(argvNonConst);

  StringRef ProgName(sys::path::filename(argv[0]));

  if (argc < 2) {
    errs() << ProgName
           << ": No subcommand specified! Run llvm-profata --help for usage.\n";
    return 1;
  }

  cl::ParseCommandLineOptions(argc, argv, "LLVM profile data\n");

  if (ShowSubcommand)
    return show_main(ProgName);

  if (OrderSubcommand)
    return order_main();

  if (OverlapSubcommand)
    return overlap_main();

  if (MergeSubcommand)
    return merge_main(ProgName);

  errs() << ProgName
         << ": Unknown command. Run llvm-profdata --help for usage.\n";
  return 1;
}
