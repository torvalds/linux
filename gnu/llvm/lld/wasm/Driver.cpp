//===- Driver.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Driver.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputElement.h"
#include "MarkLive.h"
#include "SymbolTable.h"
#include "Writer.h"
#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Reproduce.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include <optional>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm::sys;
using namespace llvm::wasm;

namespace lld::wasm {
Configuration *config;
Ctx ctx;

void Ctx::reset() {
  objectFiles.clear();
  stubFiles.clear();
  sharedFiles.clear();
  bitcodeFiles.clear();
  syntheticFunctions.clear();
  syntheticGlobals.clear();
  syntheticTables.clear();
  whyExtractRecords.clear();
  isPic = false;
  legacyFunctionTable = false;
  emitBssSegments = false;
}

namespace {

// Create enum with OPT_xxx values for each option in Options.td
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

// This function is called on startup. We need this for LTO since
// LTO calls LLVM functions to compile bitcode files to native code.
// Technically this can be delayed until we read bitcode files, but
// we don't bother to do lazily because the initialization is fast.
static void initLLVM() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}

class LinkerDriver {
public:
  void linkerMain(ArrayRef<const char *> argsArr);

private:
  void createFiles(opt::InputArgList &args);
  void addFile(StringRef path);
  void addLibrary(StringRef name);

  // True if we are in --whole-archive and --no-whole-archive.
  bool inWholeArchive = false;

  // True if we are in --start-lib and --end-lib.
  bool inLib = false;

  std::vector<InputFile *> files;
};
} // anonymous namespace

bool link(ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  // This driver-specific context will be freed later by unsafeLldMain().
  auto *ctx = new CommonLinkerContext;

  ctx->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  ctx->e.cleanupCallback = []() { wasm::ctx.reset(); };
  ctx->e.logName = args::getFilenameWithoutExe(args[0]);
  ctx->e.errorLimitExceededMsg = "too many errors emitted, stopping now (use "
                                 "-error-limit=0 to see all errors)";

  config = make<Configuration>();
  symtab = make<SymbolTable>();

  initLLVM();
  LinkerDriver().linkerMain(args);

  return errorCount() == 0;
}

// Create prefix string literals used in Options.td
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Options.inc"
#undef PREFIX

// Create table mapping all options defined in Options.td
static constexpr opt::OptTable::Info optInfo[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS,         \
               VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS, METAVAR,     \
               VALUES)                                                         \
  {PREFIX,                                                                     \
   NAME,                                                                       \
   HELPTEXT,                                                                   \
   HELPTEXTSFORVARIANTS,                                                       \
   METAVAR,                                                                    \
   OPT_##ID,                                                                   \
   opt::Option::KIND##Class,                                                   \
   PARAM,                                                                      \
   FLAGS,                                                                      \
   VISIBILITY,                                                                 \
   OPT_##GROUP,                                                                \
   OPT_##ALIAS,                                                                \
   ALIASARGS,                                                                  \
   VALUES},
#include "Options.inc"
#undef OPTION
};

namespace {
class WasmOptTable : public opt::GenericOptTable {
public:
  WasmOptTable() : opt::GenericOptTable(optInfo) {}
  opt::InputArgList parse(ArrayRef<const char *> argv);
};
} // namespace

// Set color diagnostics according to -color-diagnostics={auto,always,never}
// or -no-color-diagnostics flags.
static void handleColorDiagnostics(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_color_diagnostics, OPT_color_diagnostics_eq,
                              OPT_no_color_diagnostics);
  if (!arg)
    return;
  if (arg->getOption().getID() == OPT_color_diagnostics) {
    lld::errs().enable_colors(true);
  } else if (arg->getOption().getID() == OPT_no_color_diagnostics) {
    lld::errs().enable_colors(false);
  } else {
    StringRef s = arg->getValue();
    if (s == "always")
      lld::errs().enable_colors(true);
    else if (s == "never")
      lld::errs().enable_colors(false);
    else if (s != "auto")
      error("unknown option: --color-diagnostics=" + s);
  }
}

static cl::TokenizerCallback getQuotingStyle(opt::InputArgList &args) {
  if (auto *arg = args.getLastArg(OPT_rsp_quoting)) {
    StringRef s = arg->getValue();
    if (s != "windows" && s != "posix")
      error("invalid response file quoting: " + s);
    if (s == "windows")
      return cl::TokenizeWindowsCommandLine;
    return cl::TokenizeGNUCommandLine;
  }
  if (Triple(sys::getProcessTriple()).isOSWindows())
    return cl::TokenizeWindowsCommandLine;
  return cl::TokenizeGNUCommandLine;
}

// Find a file by concatenating given paths.
static std::optional<std::string> findFile(StringRef path1,
                                           const Twine &path2) {
  SmallString<128> s;
  path::append(s, path1, path2);
  if (fs::exists(s))
    return std::string(s);
  return std::nullopt;
}

opt::InputArgList WasmOptTable::parse(ArrayRef<const char *> argv) {
  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());

  unsigned missingIndex;
  unsigned missingCount;

  // We need to get the quoting style for response files before parsing all
  // options so we parse here before and ignore all the options but
  // --rsp-quoting.
  opt::InputArgList args = this->ParseArgs(vec, missingIndex, missingCount);

  // Expand response files (arguments in the form of @<filename>)
  // and then parse the argument again.
  cl::ExpandResponseFiles(saver(), getQuotingStyle(args), vec);
  args = this->ParseArgs(vec, missingIndex, missingCount);

  handleColorDiagnostics(args);
  if (missingCount)
    error(Twine(args.getArgString(missingIndex)) + ": missing argument");

  for (auto *arg : args.filtered(OPT_UNKNOWN))
    error("unknown argument: " + arg->getAsString(args));
  return args;
}

// Currently we allow a ".imports" to live alongside a library. This can
// be used to specify a list of symbols which can be undefined at link
// time (imported from the environment.  For example libc.a include an
// import file that lists the syscall functions it relies on at runtime.
// In the long run this information would be better stored as a symbol
// attribute/flag in the object file itself.
// See: https://github.com/WebAssembly/tool-conventions/issues/35
static void readImportFile(StringRef filename) {
  if (std::optional<MemoryBufferRef> buf = readFile(filename))
    for (StringRef sym : args::getLines(*buf))
      config->allowUndefinedSymbols.insert(sym);
}

// Returns slices of MB by parsing MB as an archive file.
// Each slice consists of a member file in the archive.
std::vector<std::pair<MemoryBufferRef, uint64_t>> static getArchiveMembers(
    MemoryBufferRef mb) {
  std::unique_ptr<Archive> file =
      CHECK(Archive::create(mb),
            mb.getBufferIdentifier() + ": failed to parse archive");

  std::vector<std::pair<MemoryBufferRef, uint64_t>> v;
  Error err = Error::success();
  for (const Archive::Child &c : file->children(err)) {
    MemoryBufferRef mbref =
        CHECK(c.getMemoryBufferRef(),
              mb.getBufferIdentifier() +
                  ": could not get the buffer for a child of the archive");
    v.push_back(std::make_pair(mbref, c.getChildOffset()));
  }
  if (err)
    fatal(mb.getBufferIdentifier() +
          ": Archive::children failed: " + toString(std::move(err)));

  // Take ownership of memory buffers created for members of thin archives.
  for (std::unique_ptr<MemoryBuffer> &mb : file->takeThinBuffers())
    make<std::unique_ptr<MemoryBuffer>>(std::move(mb));

  return v;
}

void LinkerDriver::addFile(StringRef path) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;

  switch (identify_magic(mbref.getBuffer())) {
  case file_magic::archive: {
    SmallString<128> importFile = path;
    path::replace_extension(importFile, ".imports");
    if (fs::exists(importFile))
      readImportFile(importFile.str());

    auto members = getArchiveMembers(mbref);

    // Handle -whole-archive.
    if (inWholeArchive) {
      for (const auto &[m, offset] : members) {
        auto *object = createObjectFile(m, path, offset);
        // Mark object as live; object members are normally not
        // live by default but -whole-archive is designed to treat
        // them as such.
        object->markLive();
        files.push_back(object);
      }

      return;
    }

    std::unique_ptr<Archive> file =
        CHECK(Archive::create(mbref), path + ": failed to parse archive");

    for (const auto &[m, offset] : members) {
      auto magic = identify_magic(m.getBuffer());
      if (magic == file_magic::wasm_object || magic == file_magic::bitcode)
        files.push_back(createObjectFile(m, path, offset, true));
      else
        warn(path + ": archive member '" + m.getBufferIdentifier() +
             "' is neither Wasm object file nor LLVM bitcode");
    }

    return;
  }
  case file_magic::bitcode:
  case file_magic::wasm_object:
    files.push_back(createObjectFile(mbref, "", 0, inLib));
    break;
  case file_magic::unknown:
    if (mbref.getBuffer().starts_with("#STUB")) {
      files.push_back(make<StubFile>(mbref));
      break;
    }
    [[fallthrough]];
  default:
    error("unknown file type: " + mbref.getBufferIdentifier());
  }
}

static std::optional<std::string> findFromSearchPaths(StringRef path) {
  for (StringRef dir : config->searchPaths)
    if (std::optional<std::string> s = findFile(dir, path))
      return s;
  return std::nullopt;
}

// This is for -l<basename>. We'll look for lib<basename>.a from
// search paths.
static std::optional<std::string> searchLibraryBaseName(StringRef name) {
  for (StringRef dir : config->searchPaths) {
    if (!config->isStatic)
      if (std::optional<std::string> s = findFile(dir, "lib" + name + ".so"))
        return s;
    if (std::optional<std::string> s = findFile(dir, "lib" + name + ".a"))
      return s;
  }
  return std::nullopt;
}

// This is for -l<namespec>.
static std::optional<std::string> searchLibrary(StringRef name) {
  if (name.starts_with(":"))
    return findFromSearchPaths(name.substr(1));
  return searchLibraryBaseName(name);
}

// Add a given library by searching it from input search paths.
void LinkerDriver::addLibrary(StringRef name) {
  if (std::optional<std::string> path = searchLibrary(name))
    addFile(saver().save(*path));
  else
    error("unable to find library -l" + name, ErrorTag::LibNotFound, {name});
}

void LinkerDriver::createFiles(opt::InputArgList &args) {
  for (auto *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_library:
      addLibrary(arg->getValue());
      break;
    case OPT_INPUT:
      addFile(arg->getValue());
      break;
    case OPT_Bstatic:
      config->isStatic = true;
      break;
    case OPT_Bdynamic:
      config->isStatic = false;
      break;
    case OPT_whole_archive:
      inWholeArchive = true;
      break;
    case OPT_no_whole_archive:
      inWholeArchive = false;
      break;
    case OPT_start_lib:
      if (inLib)
        error("nested --start-lib");
      inLib = true;
      break;
    case OPT_end_lib:
      if (!inLib)
        error("stray --end-lib");
      inLib = false;
      break;
    }
  }
  if (files.empty() && errorCount() == 0)
    error("no input files");
}

static StringRef getEntry(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_entry, OPT_no_entry);
  if (!arg) {
    if (args.hasArg(OPT_relocatable))
      return "";
    if (args.hasArg(OPT_shared))
      return "__wasm_call_ctors";
    return "_start";
  }
  if (arg->getOption().getID() == OPT_no_entry)
    return "";
  return arg->getValue();
}

// Determines what we should do if there are remaining unresolved
// symbols after the name resolution.
static UnresolvedPolicy getUnresolvedSymbolPolicy(opt::InputArgList &args) {
  UnresolvedPolicy errorOrWarn = args.hasFlag(OPT_error_unresolved_symbols,
                                              OPT_warn_unresolved_symbols, true)
                                     ? UnresolvedPolicy::ReportError
                                     : UnresolvedPolicy::Warn;

  if (auto *arg = args.getLastArg(OPT_unresolved_symbols)) {
    StringRef s = arg->getValue();
    if (s == "ignore-all")
      return UnresolvedPolicy::Ignore;
    if (s == "import-dynamic")
      return UnresolvedPolicy::ImportDynamic;
    if (s == "report-all")
      return errorOrWarn;
    error("unknown --unresolved-symbols value: " + s);
  }

  return errorOrWarn;
}

// Parse --build-id or --build-id=<style>. We handle "tree" as a
// synonym for "sha1" because all our hash functions including
// -build-id=sha1 are actually tree hashes for performance reasons.
static std::pair<BuildIdKind, SmallVector<uint8_t, 0>>
getBuildId(opt::InputArgList &args) {
  auto *arg = args.getLastArg(OPT_build_id, OPT_build_id_eq);
  if (!arg)
    return {BuildIdKind::None, {}};

  if (arg->getOption().getID() == OPT_build_id)
    return {BuildIdKind::Fast, {}};

  StringRef s = arg->getValue();
  if (s == "fast")
    return {BuildIdKind::Fast, {}};
  if (s == "sha1" || s == "tree")
    return {BuildIdKind::Sha1, {}};
  if (s == "uuid")
    return {BuildIdKind::Uuid, {}};
  if (s.starts_with("0x"))
    return {BuildIdKind::Hexstring, parseHex(s.substr(2))};

  if (s != "none")
    error("unknown --build-id style: " + s);
  return {BuildIdKind::None, {}};
}

// Initializes Config members by the command line options.
static void readConfigs(opt::InputArgList &args) {
  config->bsymbolic = args.hasArg(OPT_Bsymbolic);
  config->checkFeatures =
      args.hasFlag(OPT_check_features, OPT_no_check_features, true);
  config->compressRelocations = args.hasArg(OPT_compress_relocations);
  config->demangle = args.hasFlag(OPT_demangle, OPT_no_demangle, true);
  config->disableVerify = args.hasArg(OPT_disable_verify);
  config->emitRelocs = args.hasArg(OPT_emit_relocs);
  config->experimentalPic = args.hasArg(OPT_experimental_pic);
  config->entry = getEntry(args);
  config->exportAll = args.hasArg(OPT_export_all);
  config->exportTable = args.hasArg(OPT_export_table);
  config->growableTable = args.hasArg(OPT_growable_table);

  if (args.hasArg(OPT_import_memory_with_name)) {
    config->memoryImport =
        args.getLastArgValue(OPT_import_memory_with_name).split(",");
  } else if (args.hasArg(OPT_import_memory)) {
    config->memoryImport =
        std::pair<llvm::StringRef, llvm::StringRef>(defaultModule, memoryName);
  } else {
    config->memoryImport =
        std::optional<std::pair<llvm::StringRef, llvm::StringRef>>();
  }

  if (args.hasArg(OPT_export_memory_with_name)) {
    config->memoryExport =
        args.getLastArgValue(OPT_export_memory_with_name);
  } else if (args.hasArg(OPT_export_memory)) {
    config->memoryExport = memoryName;
  } else {
    config->memoryExport = std::optional<llvm::StringRef>();
  }

  config->sharedMemory = args.hasArg(OPT_shared_memory);
  config->soName = args.getLastArgValue(OPT_soname);
  config->importTable = args.hasArg(OPT_import_table);
  config->importUndefined = args.hasArg(OPT_import_undefined);
  config->ltoo = args::getInteger(args, OPT_lto_O, 2);
  if (config->ltoo > 3)
    error("invalid optimization level for LTO: " + Twine(config->ltoo));
  unsigned ltoCgo =
      args::getInteger(args, OPT_lto_CGO, args::getCGOptLevel(config->ltoo));
  if (auto level = CodeGenOpt::getLevel(ltoCgo))
    config->ltoCgo = *level;
  else
    error("invalid codegen optimization level for LTO: " + Twine(ltoCgo));
  config->ltoPartitions = args::getInteger(args, OPT_lto_partitions, 1);
  config->ltoDebugPassManager = args.hasArg(OPT_lto_debug_pass_manager);
  config->mapFile = args.getLastArgValue(OPT_Map);
  config->optimize = args::getInteger(args, OPT_O, 1);
  config->outputFile = args.getLastArgValue(OPT_o);
  config->relocatable = args.hasArg(OPT_relocatable);
  config->gcSections =
      args.hasFlag(OPT_gc_sections, OPT_no_gc_sections, !config->relocatable);
  for (auto *arg : args.filtered(OPT_keep_section))
    config->keepSections.insert(arg->getValue());
  config->mergeDataSegments =
      args.hasFlag(OPT_merge_data_segments, OPT_no_merge_data_segments,
                   !config->relocatable);
  config->pie = args.hasFlag(OPT_pie, OPT_no_pie, false);
  config->printGcSections =
      args.hasFlag(OPT_print_gc_sections, OPT_no_print_gc_sections, false);
  config->saveTemps = args.hasArg(OPT_save_temps);
  config->searchPaths = args::getStrings(args, OPT_library_path);
  config->shared = args.hasArg(OPT_shared);
  config->shlibSigCheck = !args.hasArg(OPT_no_shlib_sigcheck);
  config->stripAll = args.hasArg(OPT_strip_all);
  config->stripDebug = args.hasArg(OPT_strip_debug);
  config->stackFirst = args.hasArg(OPT_stack_first);
  config->trace = args.hasArg(OPT_trace);
  config->thinLTOCacheDir = args.getLastArgValue(OPT_thinlto_cache_dir);
  config->thinLTOCachePolicy = CHECK(
      parseCachePruningPolicy(args.getLastArgValue(OPT_thinlto_cache_policy)),
      "--thinlto-cache-policy: invalid cache policy");
  config->unresolvedSymbols = getUnresolvedSymbolPolicy(args);
  config->whyExtract = args.getLastArgValue(OPT_why_extract);
  errorHandler().verbose = args.hasArg(OPT_verbose);
  LLVM_DEBUG(errorHandler().verbose = true);

  config->tableBase = args::getInteger(args, OPT_table_base, 0);
  config->globalBase = args::getInteger(args, OPT_global_base, 0);
  config->initialHeap = args::getInteger(args, OPT_initial_heap, 0);
  config->initialMemory = args::getInteger(args, OPT_initial_memory, 0);
  config->maxMemory = args::getInteger(args, OPT_max_memory, 0);
  config->noGrowableMemory = args.hasArg(OPT_no_growable_memory);
  config->zStackSize =
      args::getZOptionValue(args, OPT_z, "stack-size", WasmPageSize);

  // -Bdynamic by default if -pie or -shared is specified.
  if (config->pie || config->shared)
    config->isStatic = false;

  if (config->maxMemory != 0 && config->noGrowableMemory) {
    // Erroring out here is simpler than defining precedence rules.
    error("--max-memory is incompatible with --no-growable-memory");
  }

  // Default value of exportDynamic depends on `-shared`
  config->exportDynamic =
      args.hasFlag(OPT_export_dynamic, OPT_no_export_dynamic, config->shared);

  // Parse wasm32/64.
  if (auto *arg = args.getLastArg(OPT_m)) {
    StringRef s = arg->getValue();
    if (s == "wasm32")
      config->is64 = false;
    else if (s == "wasm64")
      config->is64 = true;
    else
      error("invalid target architecture: " + s);
  }

  // --threads= takes a positive integer and provides the default value for
  // --thinlto-jobs=.
  if (auto *arg = args.getLastArg(OPT_threads)) {
    StringRef v(arg->getValue());
    unsigned threads = 0;
    if (!llvm::to_integer(v, threads, 0) || threads == 0)
      error(arg->getSpelling() + ": expected a positive integer, but got '" +
            arg->getValue() + "'");
    parallel::strategy = hardware_concurrency(threads);
    config->thinLTOJobs = v;
  }
  if (auto *arg = args.getLastArg(OPT_thinlto_jobs))
    config->thinLTOJobs = arg->getValue();

  if (auto *arg = args.getLastArg(OPT_features)) {
    config->features =
        std::optional<std::vector<std::string>>(std::vector<std::string>());
    for (StringRef s : arg->getValues())
      config->features->push_back(std::string(s));
  }

  if (auto *arg = args.getLastArg(OPT_extra_features)) {
    config->extraFeatures =
        std::optional<std::vector<std::string>>(std::vector<std::string>());
    for (StringRef s : arg->getValues())
      config->extraFeatures->push_back(std::string(s));
  }

  // Legacy --allow-undefined flag which is equivalent to
  // --unresolve-symbols=ignore + --import-undefined
  if (args.hasArg(OPT_allow_undefined)) {
    config->importUndefined = true;
    config->unresolvedSymbols = UnresolvedPolicy::Ignore;
  }

  if (args.hasArg(OPT_print_map))
    config->mapFile = "-";

  std::tie(config->buildId, config->buildIdVector) = getBuildId(args);
}

// Some Config members do not directly correspond to any particular
// command line options, but computed based on other Config values.
// This function initialize such members. See Config.h for the details
// of these values.
static void setConfigs() {
  ctx.isPic = config->pie || config->shared;

  if (ctx.isPic) {
    if (config->exportTable)
      error("-shared/-pie is incompatible with --export-table");
    config->importTable = true;
  } else {
    // Default table base.  Defaults to 1, reserving 0 for the NULL function
    // pointer.
    if (!config->tableBase)
      config->tableBase = 1;
    // The default offset for static/global data, for when --global-base is
    // not specified on the command line.  The precise value of 1024 is
    // somewhat arbitrary, and pre-dates wasm-ld (Its the value that
    // emscripten used prior to wasm-ld).
    if (!config->globalBase && !config->relocatable && !config->stackFirst)
      config->globalBase = 1024;
  }

  if (config->relocatable) {
    if (config->exportTable)
      error("--relocatable is incompatible with --export-table");
    if (config->growableTable)
      error("--relocatable is incompatible with --growable-table");
    // Ignore any --import-table, as it's redundant.
    config->importTable = true;
  }

  if (config->shared) {
    if (config->memoryExport.has_value()) {
      error("--export-memory is incompatible with --shared");
    }
    if (!config->memoryImport.has_value()) {
      config->memoryImport =
          std::pair<llvm::StringRef, llvm::StringRef>(defaultModule, memoryName);
    }
  }

  // If neither export-memory nor import-memory is specified, default to
  // exporting memory under its default name.
  if (!config->memoryExport.has_value() && !config->memoryImport.has_value()) {
    config->memoryExport = memoryName;
  }
}

// Some command line options or some combinations of them are not allowed.
// This function checks for such errors.
static void checkOptions(opt::InputArgList &args) {
  if (!config->stripDebug && !config->stripAll && config->compressRelocations)
    error("--compress-relocations is incompatible with output debug"
          " information. Please pass --strip-debug or --strip-all");

  if (config->ltoPartitions == 0)
    error("--lto-partitions: number of threads must be > 0");
  if (!get_threadpool_strategy(config->thinLTOJobs))
    error("--thinlto-jobs: invalid job count: " + config->thinLTOJobs);

  if (config->pie && config->shared)
    error("-shared and -pie may not be used together");

  if (config->outputFile.empty())
    error("no output file specified");

  if (config->importTable && config->exportTable)
    error("--import-table and --export-table may not be used together");

  if (config->relocatable) {
    if (!config->entry.empty())
      error("entry point specified for relocatable output file");
    if (config->gcSections)
      error("-r and --gc-sections may not be used together");
    if (config->compressRelocations)
      error("-r -and --compress-relocations may not be used together");
    if (args.hasArg(OPT_undefined))
      error("-r -and --undefined may not be used together");
    if (config->pie)
      error("-r and -pie may not be used together");
    if (config->sharedMemory)
      error("-r and --shared-memory may not be used together");
    if (config->globalBase)
      error("-r and --global-base may not by used together");
  }

  // To begin to prepare for Module Linking-style shared libraries, start
  // warning about uses of `-shared` and related flags outside of Experimental
  // mode, to give anyone using them a heads-up that they will be changing.
  //
  // Also, warn about flags which request explicit exports.
  if (!config->experimentalPic) {
    // -shared will change meaning when Module Linking is implemented.
    if (config->shared) {
      warn("creating shared libraries, with -shared, is not yet stable");
    }

    // -pie will change meaning when Module Linking is implemented.
    if (config->pie) {
      warn("creating PIEs, with -pie, is not yet stable");
    }

    if (config->unresolvedSymbols == UnresolvedPolicy::ImportDynamic) {
      warn("dynamic imports are not yet stable "
           "(--unresolved-symbols=import-dynamic)");
    }
  }

  if (config->bsymbolic && !config->shared) {
    warn("-Bsymbolic is only meaningful when combined with -shared");
  }

  if (ctx.isPic) {
    if (config->globalBase)
      error("--global-base may not be used with -shared/-pie");
    if (config->tableBase)
      error("--table-base may not be used with -shared/-pie");
  }
}

static const char *getReproduceOption(opt::InputArgList &args) {
  if (auto *arg = args.getLastArg(OPT_reproduce))
    return arg->getValue();
  return getenv("LLD_REPRODUCE");
}

// Force Sym to be entered in the output. Used for -u or equivalent.
static Symbol *handleUndefined(StringRef name, const char *option) {
  Symbol *sym = symtab->find(name);
  if (!sym)
    return nullptr;

  // Since symbol S may not be used inside the program, LTO may
  // eliminate it. Mark the symbol as "used" to prevent it.
  sym->isUsedInRegularObj = true;

  if (auto *lazySym = dyn_cast<LazySymbol>(sym)) {
    lazySym->extract();
    if (!config->whyExtract.empty())
      ctx.whyExtractRecords.emplace_back(option, sym->getFile(), *sym);
  }

  return sym;
}

static void handleLibcall(StringRef name) {
  Symbol *sym = symtab->find(name);
  if (sym && sym->isLazy() && isa<BitcodeFile>(sym->getFile())) {
    if (!config->whyExtract.empty())
      ctx.whyExtractRecords.emplace_back("<libcall>", sym->getFile(), *sym);
    cast<LazySymbol>(sym)->extract();
  }
}

static void writeWhyExtract() {
  if (config->whyExtract.empty())
    return;

  std::error_code ec;
  raw_fd_ostream os(config->whyExtract, ec, sys::fs::OF_None);
  if (ec) {
    error("cannot open --why-extract= file " + config->whyExtract + ": " +
          ec.message());
    return;
  }

  os << "reference\textracted\tsymbol\n";
  for (auto &entry : ctx.whyExtractRecords) {
    os << std::get<0>(entry) << '\t' << toString(std::get<1>(entry)) << '\t'
       << toString(std::get<2>(entry)) << '\n';
  }
}

// Equivalent of demote demoteSharedAndLazySymbols() in the ELF linker
static void demoteLazySymbols() {
  for (Symbol *sym : symtab->symbols()) {
    if (auto* s = dyn_cast<LazySymbol>(sym)) {
      if (s->signature) {
        LLVM_DEBUG(llvm::dbgs()
                   << "demoting lazy func: " << s->getName() << "\n");
        replaceSymbol<UndefinedFunction>(s, s->getName(), std::nullopt,
                                         std::nullopt, WASM_SYMBOL_BINDING_WEAK,
                                         s->getFile(), s->signature);
      }
    }
  }
}

static UndefinedGlobal *
createUndefinedGlobal(StringRef name, llvm::wasm::WasmGlobalType *type) {
  auto *sym = cast<UndefinedGlobal>(symtab->addUndefinedGlobal(
      name, std::nullopt, std::nullopt, WASM_SYMBOL_UNDEFINED, nullptr, type));
  config->allowUndefinedSymbols.insert(sym->getName());
  sym->isUsedInRegularObj = true;
  return sym;
}

static InputGlobal *createGlobal(StringRef name, bool isMutable) {
  llvm::wasm::WasmGlobal wasmGlobal;
  bool is64 = config->is64.value_or(false);
  wasmGlobal.Type = {uint8_t(is64 ? WASM_TYPE_I64 : WASM_TYPE_I32), isMutable};
  wasmGlobal.InitExpr = intConst(0, is64);
  wasmGlobal.SymbolName = name;
  return make<InputGlobal>(wasmGlobal, nullptr);
}

static GlobalSymbol *createGlobalVariable(StringRef name, bool isMutable) {
  InputGlobal *g = createGlobal(name, isMutable);
  return symtab->addSyntheticGlobal(name, WASM_SYMBOL_VISIBILITY_HIDDEN, g);
}

static GlobalSymbol *createOptionalGlobal(StringRef name, bool isMutable) {
  InputGlobal *g = createGlobal(name, isMutable);
  return symtab->addOptionalGlobalSymbol(name, g);
}

// Create ABI-defined synthetic symbols
static void createSyntheticSymbols() {
  if (config->relocatable)
    return;

  static WasmSignature nullSignature = {{}, {}};
  static WasmSignature i32ArgSignature = {{}, {ValType::I32}};
  static WasmSignature i64ArgSignature = {{}, {ValType::I64}};
  static llvm::wasm::WasmGlobalType globalTypeI32 = {WASM_TYPE_I32, false};
  static llvm::wasm::WasmGlobalType globalTypeI64 = {WASM_TYPE_I64, false};
  static llvm::wasm::WasmGlobalType mutableGlobalTypeI32 = {WASM_TYPE_I32,
                                                            true};
  static llvm::wasm::WasmGlobalType mutableGlobalTypeI64 = {WASM_TYPE_I64,
                                                            true};
  WasmSym::callCtors = symtab->addSyntheticFunction(
      "__wasm_call_ctors", WASM_SYMBOL_VISIBILITY_HIDDEN,
      make<SyntheticFunction>(nullSignature, "__wasm_call_ctors"));

  bool is64 = config->is64.value_or(false);

  if (ctx.isPic) {
    WasmSym::stackPointer =
        createUndefinedGlobal("__stack_pointer", config->is64.value_or(false)
                                                     ? &mutableGlobalTypeI64
                                                     : &mutableGlobalTypeI32);
    // For PIC code, we import two global variables (__memory_base and
    // __table_base) from the environment and use these as the offset at
    // which to load our static data and function table.
    // See:
    // https://github.com/WebAssembly/tool-conventions/blob/main/DynamicLinking.md
    auto *globalType = is64 ? &globalTypeI64 : &globalTypeI32;
    WasmSym::memoryBase = createUndefinedGlobal("__memory_base", globalType);
    WasmSym::tableBase = createUndefinedGlobal("__table_base", globalType);
    WasmSym::memoryBase->markLive();
    WasmSym::tableBase->markLive();
  } else {
    // For non-PIC code
    WasmSym::stackPointer = createGlobalVariable("__stack_pointer", true);
    WasmSym::stackPointer->markLive();
  }

  if (config->sharedMemory) {
    WasmSym::tlsBase = createGlobalVariable("__tls_base", true);
    WasmSym::tlsSize = createGlobalVariable("__tls_size", false);
    WasmSym::tlsAlign = createGlobalVariable("__tls_align", false);
    WasmSym::initTLS = symtab->addSyntheticFunction(
        "__wasm_init_tls", WASM_SYMBOL_VISIBILITY_HIDDEN,
        make<SyntheticFunction>(
            is64 ? i64ArgSignature : i32ArgSignature,
            "__wasm_init_tls"));
  }

  if (ctx.isPic ||
      config->unresolvedSymbols == UnresolvedPolicy::ImportDynamic) {
    // For PIC code, or when dynamically importing addresses, we create
    // synthetic functions that apply relocations.  These get called from
    // __wasm_call_ctors before the user-level constructors.
    WasmSym::applyDataRelocs = symtab->addSyntheticFunction(
        "__wasm_apply_data_relocs",
        WASM_SYMBOL_VISIBILITY_DEFAULT | WASM_SYMBOL_EXPORTED,
        make<SyntheticFunction>(nullSignature, "__wasm_apply_data_relocs"));
  }
}

static void createOptionalSymbols() {
  if (config->relocatable)
    return;

  WasmSym::dsoHandle = symtab->addOptionalDataSymbol("__dso_handle");

  if (!config->shared)
    WasmSym::dataEnd = symtab->addOptionalDataSymbol("__data_end");

  if (!ctx.isPic) {
    WasmSym::stackLow = symtab->addOptionalDataSymbol("__stack_low");
    WasmSym::stackHigh = symtab->addOptionalDataSymbol("__stack_high");
    WasmSym::globalBase = symtab->addOptionalDataSymbol("__global_base");
    WasmSym::heapBase = symtab->addOptionalDataSymbol("__heap_base");
    WasmSym::heapEnd = symtab->addOptionalDataSymbol("__heap_end");
    WasmSym::definedMemoryBase = symtab->addOptionalDataSymbol("__memory_base");
    WasmSym::definedTableBase = symtab->addOptionalDataSymbol("__table_base");
  }

  // For non-shared memory programs we still need to define __tls_base since we
  // allow object files built with TLS to be linked into single threaded
  // programs, and such object files can contain references to this symbol.
  //
  // However, in this case __tls_base is immutable and points directly to the
  // start of the `.tdata` static segment.
  //
  // __tls_size and __tls_align are not needed in this case since they are only
  // needed for __wasm_init_tls (which we do not create in this case).
  if (!config->sharedMemory)
    WasmSym::tlsBase = createOptionalGlobal("__tls_base", false);
}

static void processStubLibrariesPreLTO() {
  log("-- processStubLibrariesPreLTO");
  for (auto &stub_file : ctx.stubFiles) {
    LLVM_DEBUG(llvm::dbgs()
               << "processing stub file: " << stub_file->getName() << "\n");
    for (auto [name, deps]: stub_file->symbolDependencies) {
      auto* sym = symtab->find(name);
      // If the symbol is not present at all (yet), or if it is present but
      // undefined, then mark the dependent symbols as used by a regular
      // object so they will be preserved and exported by the LTO process.
      if (!sym || sym->isUndefined()) {
        for (const auto dep : deps) {
          auto* needed = symtab->find(dep);
          if (needed ) {
            needed->isUsedInRegularObj = true;
          }
        }
      }
    }
  }
}

static bool addStubSymbolDeps(const StubFile *stub_file, Symbol *sym,
                              ArrayRef<StringRef> deps) {
  // The first stub library to define a given symbol sets this and
  // definitions in later stub libraries are ignored.
  if (sym->forceImport)
    return false; // Already handled
  sym->forceImport = true;
  if (sym->traced)
    message(toString(stub_file) + ": importing " + sym->getName());
  else
    LLVM_DEBUG(llvm::dbgs() << toString(stub_file) << ": importing "
                            << sym->getName() << "\n");
  bool depsAdded = false;
  for (const auto dep : deps) {
    auto *needed = symtab->find(dep);
    if (!needed) {
      error(toString(stub_file) + ": undefined symbol: " + dep +
            ". Required by " + toString(*sym));
    } else if (needed->isUndefined()) {
      error(toString(stub_file) + ": undefined symbol: " + toString(*needed) +
            ". Required by " + toString(*sym));
    } else {
      if (needed->traced)
        message(toString(stub_file) + ": exported " + toString(*needed) +
                " due to import of " + sym->getName());
      else
        LLVM_DEBUG(llvm::dbgs()
                   << "force export: " << toString(*needed) << "\n");
      needed->forceExport = true;
      if (auto *lazy = dyn_cast<LazySymbol>(needed)) {
        depsAdded = true;
        lazy->extract();
        if (!config->whyExtract.empty())
          ctx.whyExtractRecords.emplace_back(toString(stub_file),
                                             sym->getFile(), *sym);
      }
    }
  }
  return depsAdded;
}

static void processStubLibraries() {
  log("-- processStubLibraries");
  bool depsAdded = false;
  do {
    depsAdded = false;
    for (auto &stub_file : ctx.stubFiles) {
      LLVM_DEBUG(llvm::dbgs()
                 << "processing stub file: " << stub_file->getName() << "\n");

      // First look for any imported symbols that directly match
      // the names of the stub imports
      for (auto [name, deps]: stub_file->symbolDependencies) {
        auto* sym = symtab->find(name);
        if (sym && sym->isUndefined()) {
          depsAdded |= addStubSymbolDeps(stub_file, sym, deps);
        } else {
          if (sym && sym->traced)
            message(toString(stub_file) + ": stub symbol not needed: " + name);
          else
            LLVM_DEBUG(llvm::dbgs()
                       << "stub symbol not needed: `" << name << "`\n");
        }
      }

      // Secondly looks for any symbols with an `importName` that matches
      for (Symbol *sym : symtab->symbols()) {
        if (sym->isUndefined() && sym->importName.has_value()) {
          auto it = stub_file->symbolDependencies.find(sym->importName.value());
          if (it != stub_file->symbolDependencies.end()) {
            depsAdded |= addStubSymbolDeps(stub_file, sym, it->second);
          }
        }
      }
    }
  } while (depsAdded);

  log("-- done processStubLibraries");
}

// Reconstructs command line arguments so that so that you can re-run
// the same command with the same inputs. This is for --reproduce.
static std::string createResponseFile(const opt::InputArgList &args) {
  SmallString<0> data;
  raw_svector_ostream os(data);

  // Copy the command line to the output while rewriting paths.
  for (auto *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_reproduce:
      break;
    case OPT_INPUT:
      os << quote(relativeToRoot(arg->getValue())) << "\n";
      break;
    case OPT_o:
      // If -o path contains directories, "lld @response.txt" will likely
      // fail because the archive we are creating doesn't contain empty
      // directories for the output path (-o doesn't create directories).
      // Strip directories to prevent the issue.
      os << "-o " << quote(sys::path::filename(arg->getValue())) << "\n";
      break;
    default:
      os << toString(*arg) << "\n";
    }
  }
  return std::string(data);
}

// The --wrap option is a feature to rename symbols so that you can write
// wrappers for existing functions. If you pass `-wrap=foo`, all
// occurrences of symbol `foo` are resolved to `wrap_foo` (so, you are
// expected to write `wrap_foo` function as a wrapper). The original
// symbol becomes accessible as `real_foo`, so you can call that from your
// wrapper.
//
// This data structure is instantiated for each -wrap option.
struct WrappedSymbol {
  Symbol *sym;
  Symbol *real;
  Symbol *wrap;
};

static Symbol *addUndefined(StringRef name) {
  return symtab->addUndefinedFunction(name, std::nullopt, std::nullopt,
                                      WASM_SYMBOL_UNDEFINED, nullptr, nullptr,
                                      false);
}

// Handles -wrap option.
//
// This function instantiates wrapper symbols. At this point, they seem
// like they are not being used at all, so we explicitly set some flags so
// that LTO won't eliminate them.
static std::vector<WrappedSymbol> addWrappedSymbols(opt::InputArgList &args) {
  std::vector<WrappedSymbol> v;
  DenseSet<StringRef> seen;

  for (auto *arg : args.filtered(OPT_wrap)) {
    StringRef name = arg->getValue();
    if (!seen.insert(name).second)
      continue;

    Symbol *sym = symtab->find(name);
    if (!sym)
      continue;

    Symbol *real = addUndefined(saver().save("__real_" + name));
    Symbol *wrap = addUndefined(saver().save("__wrap_" + name));
    v.push_back({sym, real, wrap});

    // We want to tell LTO not to inline symbols to be overwritten
    // because LTO doesn't know the final symbol contents after renaming.
    real->canInline = false;
    sym->canInline = false;

    // Tell LTO not to eliminate these symbols.
    sym->isUsedInRegularObj = true;
    wrap->isUsedInRegularObj = true;
    real->isUsedInRegularObj = false;
  }
  return v;
}

// Do renaming for -wrap by updating pointers to symbols.
//
// When this function is executed, only InputFiles and symbol table
// contain pointers to symbol objects. We visit them to replace pointers,
// so that wrapped symbols are swapped as instructed by the command line.
static void wrapSymbols(ArrayRef<WrappedSymbol> wrapped) {
  DenseMap<Symbol *, Symbol *> map;
  for (const WrappedSymbol &w : wrapped) {
    map[w.sym] = w.wrap;
    map[w.real] = w.sym;
  }

  // Update pointers in input files.
  parallelForEach(ctx.objectFiles, [&](InputFile *file) {
    MutableArrayRef<Symbol *> syms = file->getMutableSymbols();
    for (size_t i = 0, e = syms.size(); i != e; ++i)
      if (Symbol *s = map.lookup(syms[i]))
        syms[i] = s;
  });

  // Update pointers in the symbol table.
  for (const WrappedSymbol &w : wrapped)
    symtab->wrap(w.sym, w.real, w.wrap);
}

static void splitSections() {
  // splitIntoPieces needs to be called on each MergeInputChunk
  // before calling finalizeContents().
  LLVM_DEBUG(llvm::dbgs() << "splitSections\n");
  parallelForEach(ctx.objectFiles, [](ObjFile *file) {
    for (InputChunk *seg : file->segments) {
      if (auto *s = dyn_cast<MergeInputChunk>(seg))
        s->splitIntoPieces();
    }
    for (InputChunk *sec : file->customSections) {
      if (auto *s = dyn_cast<MergeInputChunk>(sec))
        s->splitIntoPieces();
    }
  });
}

static bool isKnownZFlag(StringRef s) {
  // For now, we only support a very limited set of -z flags
  return s.starts_with("stack-size=");
}

// Report a warning for an unknown -z option.
static void checkZOptions(opt::InputArgList &args) {
  for (auto *arg : args.filtered(OPT_z))
    if (!isKnownZFlag(arg->getValue()))
      warn("unknown -z value: " + StringRef(arg->getValue()));
}

void LinkerDriver::linkerMain(ArrayRef<const char *> argsArr) {
  WasmOptTable parser;
  opt::InputArgList args = parser.parse(argsArr.slice(1));

  // Interpret these flags early because error()/warn() depend on them.
  errorHandler().errorLimit = args::getInteger(args, OPT_error_limit, 20);
  errorHandler().fatalWarnings =
      args.hasFlag(OPT_fatal_warnings, OPT_no_fatal_warnings, false);
  checkZOptions(args);

  // Handle --help
  if (args.hasArg(OPT_help)) {
    parser.printHelp(lld::outs(),
                     (std::string(argsArr[0]) + " [options] file...").c_str(),
                     "LLVM Linker", false);
    return;
  }

  // Handle --version
  if (args.hasArg(OPT_version) || args.hasArg(OPT_v)) {
    lld::outs() << getLLDVersion() << "\n";
    return;
  }

  // Handle --reproduce
  if (const char *path = getReproduceOption(args)) {
    Expected<std::unique_ptr<TarWriter>> errOrWriter =
        TarWriter::create(path, path::stem(path));
    if (errOrWriter) {
      tar = std::move(*errOrWriter);
      tar->append("response.txt", createResponseFile(args));
      tar->append("version.txt", getLLDVersion() + "\n");
    } else {
      error("--reproduce: " + toString(errOrWriter.takeError()));
    }
  }

  // Parse and evaluate -mllvm options.
  std::vector<const char *> v;
  v.push_back("wasm-ld (LLVM option parsing)");
  for (auto *arg : args.filtered(OPT_mllvm))
    v.push_back(arg->getValue());
  cl::ResetAllOptionOccurrences();
  cl::ParseCommandLineOptions(v.size(), v.data());

  readConfigs(args);
  setConfigs();

  createFiles(args);
  if (errorCount())
    return;

  checkOptions(args);
  if (errorCount())
    return;

  if (auto *arg = args.getLastArg(OPT_allow_undefined_file))
    readImportFile(arg->getValue());

  // Fail early if the output file or map file is not writable. If a user has a
  // long link, e.g. due to a large LTO link, they do not wish to run it and
  // find that it failed because there was a mistake in their command-line.
  if (auto e = tryCreateFile(config->outputFile))
    error("cannot open output file " + config->outputFile + ": " + e.message());
  if (auto e = tryCreateFile(config->mapFile))
    error("cannot open map file " + config->mapFile + ": " + e.message());
  if (errorCount())
    return;

  // Handle --trace-symbol.
  for (auto *arg : args.filtered(OPT_trace_symbol))
    symtab->trace(arg->getValue());

  for (auto *arg : args.filtered(OPT_export_if_defined))
    config->exportedSymbols.insert(arg->getValue());

  for (auto *arg : args.filtered(OPT_export)) {
    config->exportedSymbols.insert(arg->getValue());
    config->requiredExports.push_back(arg->getValue());
  }

  createSyntheticSymbols();

  // Add all files to the symbol table. This will add almost all
  // symbols that we need to the symbol table.
  for (InputFile *f : files)
    symtab->addFile(f);
  if (errorCount())
    return;

  // Handle the `--undefined <sym>` options.
  for (auto *arg : args.filtered(OPT_undefined))
    handleUndefined(arg->getValue(), "<internal>");

  // Handle the `--export <sym>` options
  // This works like --undefined but also exports the symbol if its found
  for (auto &iter : config->exportedSymbols)
    handleUndefined(iter.first(), "--export");

  Symbol *entrySym = nullptr;
  if (!config->relocatable && !config->entry.empty()) {
    entrySym = handleUndefined(config->entry, "--entry");
    if (entrySym && entrySym->isDefined())
      entrySym->forceExport = true;
    else
      error("entry symbol not defined (pass --no-entry to suppress): " +
            config->entry);
  }

  // If the user code defines a `__wasm_call_dtors` function, remember it so
  // that we can call it from the command export wrappers. Unlike
  // `__wasm_call_ctors` which we synthesize, `__wasm_call_dtors` is defined
  // by libc/etc., because destructors are registered dynamically with
  // `__cxa_atexit` and friends.
  if (!config->relocatable && !config->shared &&
      !WasmSym::callCtors->isUsedInRegularObj &&
      WasmSym::callCtors->getName() != config->entry &&
      !config->exportedSymbols.count(WasmSym::callCtors->getName())) {
    if (Symbol *callDtors =
            handleUndefined("__wasm_call_dtors", "<internal>")) {
      if (auto *callDtorsFunc = dyn_cast<DefinedFunction>(callDtors)) {
        if (callDtorsFunc->signature &&
            (!callDtorsFunc->signature->Params.empty() ||
             !callDtorsFunc->signature->Returns.empty())) {
          error("__wasm_call_dtors must have no argument or return values");
        }
        WasmSym::callDtors = callDtorsFunc;
      } else {
        error("__wasm_call_dtors must be a function");
      }
    }
  }

  if (errorCount())
    return;

  // Create wrapped symbols for -wrap option.
  std::vector<WrappedSymbol> wrapped = addWrappedSymbols(args);

  // If any of our inputs are bitcode files, the LTO code generator may create
  // references to certain library functions that might not be explicit in the
  // bitcode file's symbol table. If any of those library functions are defined
  // in a bitcode file in an archive member, we need to arrange to use LTO to
  // compile those archive members by adding them to the link beforehand.
  //
  // We only need to add libcall symbols to the link before LTO if the symbol's
  // definition is in bitcode. Any other required libcall symbols will be added
  // to the link after LTO when we add the LTO object file to the link.
  if (!ctx.bitcodeFiles.empty()) {
    llvm::Triple TT(ctx.bitcodeFiles.front()->obj->getTargetTriple());
    for (auto *s : lto::LTO::getRuntimeLibcallSymbols(TT))
      handleLibcall(s);
  }
  if (errorCount())
    return;

  // We process the stub libraries once beofore LTO to ensure that any possible
  // required exports are preserved by the LTO process.
  processStubLibrariesPreLTO();

  // Do link-time optimization if given files are LLVM bitcode files.
  // This compiles bitcode files into real object files.
  symtab->compileBitcodeFiles();
  if (errorCount())
    return;

  // The LTO process can generate new undefined symbols, specifically libcall
  // functions.  Because those symbols might be declared in a stub library we
  // need the process the stub libraries once again after LTO to handle all
  // undefined symbols, including ones that didn't exist prior to LTO.
  processStubLibraries();

  writeWhyExtract();

  createOptionalSymbols();

  // Resolve any variant symbols that were created due to signature
  // mismatchs.
  symtab->handleSymbolVariants();
  if (errorCount())
    return;

  // Apply symbol renames for -wrap.
  if (!wrapped.empty())
    wrapSymbols(wrapped);

  for (auto &iter : config->exportedSymbols) {
    Symbol *sym = symtab->find(iter.first());
    if (sym && sym->isDefined())
      sym->forceExport = true;
  }

  if (!config->relocatable && !ctx.isPic) {
    // Add synthetic dummies for weak undefined functions.  Must happen
    // after LTO otherwise functions may not yet have signatures.
    symtab->handleWeakUndefines();
  }

  if (entrySym)
    entrySym->setHidden(false);

  if (errorCount())
    return;

  // Split WASM_SEG_FLAG_STRINGS sections into pieces in preparation for garbage
  // collection.
  splitSections();

  // Any remaining lazy symbols should be demoted to Undefined
  demoteLazySymbols();

  // Do size optimizations: garbage collection
  markLive();

  // Provide the indirect function table if needed.
  WasmSym::indirectFunctionTable =
      symtab->resolveIndirectFunctionTable(/*required =*/false);

  if (errorCount())
    return;

  // Write the result to the file.
  writeResult();
}

} // namespace lld::wasm
