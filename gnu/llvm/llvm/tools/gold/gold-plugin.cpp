//===-- gold-plugin.cpp - Plugin to gold for Link Time Optimization  ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a gold plugin for LLVM. It provides an LLVM implementation of the
// interface described in http://gcc.gnu.org/wiki/whopr/driver .
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Config/config.h" // plugin-api.h requires HAVE_STDINT_H
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Error.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <list>
#include <map>
#include <plugin-api.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// FIXME: remove this declaration when we stop maintaining Ubuntu Quantal and
// Precise and Debian Wheezy (binutils 2.23 is required)
#define LDPO_PIE 3

#define LDPT_GET_SYMBOLS_V3 28

// FIXME: Remove when binutils 2.31 (containing gold 1.16) is the minimum
// required version.
#define LDPT_GET_WRAP_SYMBOLS 32

using namespace llvm;
using namespace lto;

static codegen::RegisterCodeGenFlags CodeGenFlags;

// FIXME: Remove when binutils 2.31 (containing gold 1.16) is the minimum
// required version.
typedef enum ld_plugin_status (*ld_plugin_get_wrap_symbols)(
    uint64_t *num_symbols, const char ***wrap_symbol_list);

static ld_plugin_status discard_message(int level, const char *format, ...) {
  // Die loudly. Recent versions of Gold pass ld_plugin_message as the first
  // callback in the transfer vector. This should never be called.
  abort();
}

static ld_plugin_release_input_file release_input_file = nullptr;
static ld_plugin_get_input_file get_input_file = nullptr;
static ld_plugin_message message = discard_message;
static ld_plugin_get_wrap_symbols get_wrap_symbols = nullptr;

namespace {
struct claimed_file {
  void *handle;
  void *leader_handle;
  std::vector<ld_plugin_symbol> syms;
  off_t filesize;
  std::string name;
};

/// RAII wrapper to manage opening and releasing of a ld_plugin_input_file.
struct PluginInputFile {
  void *Handle;
  std::unique_ptr<ld_plugin_input_file> File;

  PluginInputFile(void *Handle) : Handle(Handle) {
    File = std::make_unique<ld_plugin_input_file>();
    if (get_input_file(Handle, File.get()) != LDPS_OK)
      message(LDPL_FATAL, "Failed to get file information");
  }
  ~PluginInputFile() {
    // File would have been reset to nullptr if we moved this object
    // to a new owner.
    if (File)
      if (release_input_file(Handle) != LDPS_OK)
        message(LDPL_FATAL, "Failed to release file information");
  }

  ld_plugin_input_file &file() { return *File; }

  PluginInputFile(PluginInputFile &&RHS) = default;
  PluginInputFile &operator=(PluginInputFile &&RHS) = default;
};

struct ResolutionInfo {
  bool CanOmitFromDynSym = true;
  bool DefaultVisibility = true;
  bool CanInline = true;
  bool IsUsedInRegularObj = false;
};

}

static ld_plugin_add_symbols add_symbols = nullptr;
static ld_plugin_get_symbols get_symbols = nullptr;
static ld_plugin_add_input_file add_input_file = nullptr;
static ld_plugin_set_extra_library_path set_extra_library_path = nullptr;
static ld_plugin_get_view get_view = nullptr;
static bool IsExecutable = false;
static bool SplitSections = true;
static std::optional<Reloc::Model> RelocationModel;
static std::string output_name = "";
static std::list<claimed_file> Modules;
static DenseMap<int, void *> FDToLeaderHandle;
static StringMap<ResolutionInfo> ResInfo;
static std::vector<std::string> Cleanup;

namespace options {
  enum OutputType {
    OT_NORMAL,
    OT_DISABLE,
    OT_BC_ONLY,
    OT_ASM_ONLY,
    OT_SAVE_TEMPS
  };
  static OutputType TheOutputType = OT_NORMAL;
  static unsigned OptLevel = 2;
  // Currently only affects ThinLTO, where the default is the max cores in the
  // system. See llvm::get_threadpool_strategy() for acceptable values.
  static std::string Parallelism;
  // Default regular LTO codegen parallelism (number of partitions).
  static unsigned ParallelCodeGenParallelismLevel = 1;
#ifdef NDEBUG
  static bool DisableVerify = true;
#else
  static bool DisableVerify = false;
#endif
  static std::string obj_path;
  static std::string extra_library_path;
  static std::string triple;
  static std::string mcpu;
  // When the thinlto plugin option is specified, only read the function
  // the information from intermediate files and write a combined
  // global index for the ThinLTO backends.
  static bool thinlto = false;
  // If false, all ThinLTO backend compilations through code gen are performed
  // using multiple threads in the gold-plugin, before handing control back to
  // gold. If true, write individual backend index files which reflect
  // the import decisions, and exit afterwards. The assumption is
  // that the build system will launch the backend processes.
  static bool thinlto_index_only = false;
  // If non-empty, holds the name of a file in which to write the list of
  // oject files gold selected for inclusion in the link after symbol
  // resolution (i.e. they had selected symbols). This will only be non-empty
  // in the thinlto_index_only case. It is used to identify files, which may
  // have originally been within archive libraries specified via
  // --start-lib/--end-lib pairs, that should be included in the final
  // native link process (since intervening function importing and inlining
  // may change the symbol resolution detected in the final link and which
  // files to include out of --start-lib/--end-lib libraries as a result).
  static std::string thinlto_linked_objects_file;
  // If true, when generating individual index files for distributed backends,
  // also generate a "${bitcodefile}.imports" file at the same location for each
  // bitcode file, listing the files it imports from in plain text. This is to
  // support distributed build file staging.
  static bool thinlto_emit_imports_files = false;
  // Option to control where files for a distributed backend (the individual
  // index files and optional imports files) are created.
  // If specified, expects a string of the form "oldprefix:newprefix", and
  // instead of generating these files in the same directory path as the
  // corresponding bitcode file, will use a path formed by replacing the
  // bitcode file's path prefix matching oldprefix with newprefix.
  static std::string thinlto_prefix_replace;
  // Option to control the name of modules encoded in the individual index
  // files for a distributed backend. This enables the use of minimized
  // bitcode files for the thin link, assuming the name of the full bitcode
  // file used in the backend differs just in some part of the file suffix.
  // If specified, expects a string of the form "oldsuffix:newsuffix".
  static std::string thinlto_object_suffix_replace;
  // Optional path to a directory for caching ThinLTO objects.
  static std::string cache_dir;
  // Optional pruning policy for ThinLTO caches.
  static std::string cache_policy;
  // Additional options to pass into the code generator.
  // Note: This array will contain all plugin options which are not claimed
  // as plugin exclusive to pass to the code generator.
  static std::vector<const char *> extra;
  // Sample profile file path
  static std::string sample_profile;
  // Debug new pass manager
  static bool debug_pass_manager = false;
  // Directory to store the .dwo files.
  static std::string dwo_dir;
  /// Statistics output filename.
  static std::string stats_file;
  // Asserts that LTO link has whole program visibility
  static bool whole_program_visibility = false;

  // Optimization remarks filename, accepted passes and hotness options
  static std::string RemarksFilename;
  static std::string RemarksPasses;
  static bool RemarksWithHotness = false;
  static std::optional<uint64_t> RemarksHotnessThreshold = 0;
  static std::string RemarksFormat;

  // Context sensitive PGO options.
  static std::string cs_profile_path;
  static bool cs_pgo_gen = false;

  // Time trace options.
  static std::string time_trace_file;
  static unsigned time_trace_granularity = 500;

  static void process_plugin_option(const char *opt_)
  {
    if (opt_ == nullptr)
      return;
    llvm::StringRef opt = opt_;

    if (opt.consume_front("mcpu=")) {
      mcpu = std::string(opt);
    } else if (opt.consume_front("extra-library-path=")) {
      extra_library_path = std::string(opt);
    } else if (opt.consume_front("mtriple=")) {
      triple = std::string(opt);
    } else if (opt.consume_front("obj-path=")) {
      obj_path = std::string(opt);
    } else if (opt == "emit-llvm") {
      TheOutputType = OT_BC_ONLY;
    } else if (opt == "save-temps") {
      TheOutputType = OT_SAVE_TEMPS;
    } else if (opt == "disable-output") {
      TheOutputType = OT_DISABLE;
    } else if (opt == "emit-asm") {
      TheOutputType = OT_ASM_ONLY;
    } else if (opt == "thinlto") {
      thinlto = true;
    } else if (opt == "thinlto-index-only") {
      thinlto_index_only = true;
    } else if (opt.consume_front("thinlto-index-only=")) {
      thinlto_index_only = true;
      thinlto_linked_objects_file = std::string(opt);
    } else if (opt == "thinlto-emit-imports-files") {
      thinlto_emit_imports_files = true;
    } else if (opt.consume_front("thinlto-prefix-replace=")) {
      thinlto_prefix_replace = std::string(opt);
      if (thinlto_prefix_replace.find(';') == std::string::npos)
        message(LDPL_FATAL, "thinlto-prefix-replace expects 'old;new' format");
    } else if (opt.consume_front("thinlto-object-suffix-replace=")) {
      thinlto_object_suffix_replace = std::string(opt);
      if (thinlto_object_suffix_replace.find(';') == std::string::npos)
        message(LDPL_FATAL,
                "thinlto-object-suffix-replace expects 'old;new' format");
    } else if (opt.consume_front("cache-dir=")) {
      cache_dir = std::string(opt);
    } else if (opt.consume_front("cache-policy=")) {
      cache_policy = std::string(opt);
    } else if (opt.size() == 2 && opt[0] == 'O') {
      if (opt[1] < '0' || opt[1] > '3')
        message(LDPL_FATAL, "Optimization level must be between 0 and 3");
      OptLevel = opt[1] - '0';
    } else if (opt.consume_front("jobs=")) {
      Parallelism = std::string(opt);
      if (!get_threadpool_strategy(opt))
        message(LDPL_FATAL, "Invalid parallelism level: %s",
                Parallelism.c_str());
    } else if (opt.consume_front("lto-partitions=")) {
      if (opt.getAsInteger(10, ParallelCodeGenParallelismLevel))
        message(LDPL_FATAL, "Invalid codegen partition level: %s", opt_ + 5);
    } else if (opt == "disable-verify") {
      DisableVerify = true;
    } else if (opt.consume_front("sample-profile=")) {
      sample_profile = std::string(opt);
    } else if (opt == "cs-profile-generate") {
      cs_pgo_gen = true;
    } else if (opt.consume_front("cs-profile-path=")) {
      cs_profile_path = std::string(opt);
    } else if (opt == "new-pass-manager") {
      // We always use the new pass manager.
    } else if (opt == "debug-pass-manager") {
      debug_pass_manager = true;
    } else if (opt == "whole-program-visibility") {
      whole_program_visibility = true;
    } else if (opt.consume_front("dwo_dir=")) {
      dwo_dir = std::string(opt);
    } else if (opt.consume_front("opt-remarks-filename=")) {
      RemarksFilename = std::string(opt);
    } else if (opt.consume_front("opt-remarks-passes=")) {
      RemarksPasses = std::string(opt);
    } else if (opt == "opt-remarks-with-hotness") {
      RemarksWithHotness = true;
    } else if (opt.consume_front("opt-remarks-hotness-threshold=")) {
      auto ResultOrErr = remarks::parseHotnessThresholdOption(opt);
      if (!ResultOrErr)
        message(LDPL_FATAL, "Invalid remarks hotness threshold: %s",
                opt.data());
      else
        RemarksHotnessThreshold = *ResultOrErr;
    } else if (opt.consume_front("opt-remarks-format=")) {
      RemarksFormat = std::string(opt);
    } else if (opt.consume_front("stats-file=")) {
      stats_file = std::string(opt);
    } else if (opt.consume_front("time-trace=")) {
      time_trace_file = std::string(opt);
    } else if (opt.consume_front("time-trace-granularity=")) {
      unsigned Granularity;
      if (opt.getAsInteger(10, Granularity))
        message(LDPL_FATAL, "Invalid time trace granularity: %s", opt.data());
      else
        time_trace_granularity = Granularity;
    } else {
      // Save this option to pass to the code generator.
      // ParseCommandLineOptions() expects argv[0] to be program name. Lazily
      // add that.
      if (extra.empty())
        extra.push_back("LLVMgold");

      extra.push_back(opt_);
    }
  }
}

static ld_plugin_status claim_file_hook(const ld_plugin_input_file *file,
                                        int *claimed);
static ld_plugin_status all_symbols_read_hook(void);
static ld_plugin_status cleanup_hook(void);

extern "C" ld_plugin_status onload(ld_plugin_tv *tv);
ld_plugin_status onload(ld_plugin_tv *tv) {
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  // We're given a pointer to the first transfer vector. We read through them
  // until we find one where tv_tag == LDPT_NULL. The REGISTER_* tagged values
  // contain pointers to functions that we need to call to register our own
  // hooks. The others are addresses of functions we can use to call into gold
  // for services.

  bool registeredClaimFile = false;
  bool RegisteredAllSymbolsRead = false;

  for (; tv->tv_tag != LDPT_NULL; ++tv) {
    // Cast tv_tag to int to allow values not in "enum ld_plugin_tag", like, for
    // example, LDPT_GET_SYMBOLS_V3 when building against an older plugin-api.h
    // header.
    switch (static_cast<int>(tv->tv_tag)) {
    case LDPT_OUTPUT_NAME:
      output_name = tv->tv_u.tv_string;
      break;
    case LDPT_LINKER_OUTPUT:
      switch (tv->tv_u.tv_val) {
      case LDPO_REL: // .o
        IsExecutable = false;
        SplitSections = false;
        break;
      case LDPO_DYN: // .so
        IsExecutable = false;
        RelocationModel = Reloc::PIC_;
        break;
      case LDPO_PIE: // position independent executable
        IsExecutable = true;
        RelocationModel = Reloc::PIC_;
        break;
      case LDPO_EXEC: // .exe
        IsExecutable = true;
        RelocationModel = Reloc::Static;
        break;
      default:
        message(LDPL_ERROR, "Unknown output file type %d", tv->tv_u.tv_val);
        return LDPS_ERR;
      }
      break;
    case LDPT_OPTION:
      options::process_plugin_option(tv->tv_u.tv_string);
      break;
    case LDPT_REGISTER_CLAIM_FILE_HOOK: {
      ld_plugin_register_claim_file callback;
      callback = tv->tv_u.tv_register_claim_file;

      if (callback(claim_file_hook) != LDPS_OK)
        return LDPS_ERR;

      registeredClaimFile = true;
    } break;
    case LDPT_REGISTER_ALL_SYMBOLS_READ_HOOK: {
      ld_plugin_register_all_symbols_read callback;
      callback = tv->tv_u.tv_register_all_symbols_read;

      if (callback(all_symbols_read_hook) != LDPS_OK)
        return LDPS_ERR;

      RegisteredAllSymbolsRead = true;
    } break;
    case LDPT_REGISTER_CLEANUP_HOOK: {
      ld_plugin_register_cleanup callback;
      callback = tv->tv_u.tv_register_cleanup;

      if (callback(cleanup_hook) != LDPS_OK)
        return LDPS_ERR;
    } break;
    case LDPT_GET_INPUT_FILE:
      get_input_file = tv->tv_u.tv_get_input_file;
      break;
    case LDPT_RELEASE_INPUT_FILE:
      release_input_file = tv->tv_u.tv_release_input_file;
      break;
    case LDPT_ADD_SYMBOLS:
      add_symbols = tv->tv_u.tv_add_symbols;
      break;
    case LDPT_GET_SYMBOLS_V2:
      // Do not override get_symbols_v3 with get_symbols_v2.
      if (!get_symbols)
        get_symbols = tv->tv_u.tv_get_symbols;
      break;
    case LDPT_GET_SYMBOLS_V3:
      get_symbols = tv->tv_u.tv_get_symbols;
      break;
    case LDPT_ADD_INPUT_FILE:
      add_input_file = tv->tv_u.tv_add_input_file;
      break;
    case LDPT_SET_EXTRA_LIBRARY_PATH:
      set_extra_library_path = tv->tv_u.tv_set_extra_library_path;
      break;
    case LDPT_GET_VIEW:
      get_view = tv->tv_u.tv_get_view;
      break;
    case LDPT_MESSAGE:
      message = tv->tv_u.tv_message;
      break;
    case LDPT_GET_WRAP_SYMBOLS:
      // FIXME: When binutils 2.31 (containing gold 1.16) is the minimum
      // required version, this should be changed to:
      // get_wrap_symbols = tv->tv_u.tv_get_wrap_symbols;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
      get_wrap_symbols = (ld_plugin_get_wrap_symbols)tv->tv_u.tv_message;
#pragma GCC diagnostic pop
      break;
    default:
      break;
    }
  }

  if (!registeredClaimFile) {
    message(LDPL_ERROR, "register_claim_file not passed to LLVMgold.");
    return LDPS_ERR;
  }
  if (!add_symbols) {
    message(LDPL_ERROR, "add_symbols not passed to LLVMgold.");
    return LDPS_ERR;
  }

  if (!RegisteredAllSymbolsRead)
    return LDPS_OK;

  if (!get_input_file) {
    message(LDPL_ERROR, "get_input_file not passed to LLVMgold.");
    return LDPS_ERR;
  }
  if (!release_input_file) {
    message(LDPL_ERROR, "release_input_file not passed to LLVMgold.");
    return LDPS_ERR;
  }

  return LDPS_OK;
}

static void diagnosticHandler(const DiagnosticInfo &DI) {
  std::string ErrStorage;
  {
    raw_string_ostream OS(ErrStorage);
    DiagnosticPrinterRawOStream DP(OS);
    DI.print(DP);
  }
  ld_plugin_level Level;
  switch (DI.getSeverity()) {
  case DS_Error:
    Level = LDPL_FATAL;
    break;
  case DS_Warning:
    Level = LDPL_WARNING;
    break;
  case DS_Note:
  case DS_Remark:
    Level = LDPL_INFO;
    break;
  }
  message(Level, "LLVM gold plugin: %s",  ErrStorage.c_str());
}

static void check(Error E, std::string Msg = "LLVM gold plugin") {
  handleAllErrors(std::move(E), [&](ErrorInfoBase &EIB) -> Error {
    message(LDPL_FATAL, "%s: %s", Msg.c_str(), EIB.message().c_str());
    return Error::success();
  });
}

template <typename T> static T check(Expected<T> E) {
  if (E)
    return std::move(*E);
  check(E.takeError());
  return T();
}

/// Called by gold to see whether this file is one that our plugin can handle.
/// We'll try to open it and register all the symbols with add_symbol if
/// possible.
static ld_plugin_status claim_file_hook(const ld_plugin_input_file *file,
                                        int *claimed) {
  MemoryBufferRef BufferRef;
  std::unique_ptr<MemoryBuffer> Buffer;
  if (get_view) {
    const void *view;
    if (get_view(file->handle, &view) != LDPS_OK) {
      message(LDPL_ERROR, "Failed to get a view of %s", file->name);
      return LDPS_ERR;
    }
    BufferRef =
        MemoryBufferRef(StringRef((const char *)view, file->filesize), "");
  } else {
    int64_t offset = 0;
    // Gold has found what might be IR part-way inside of a file, such as
    // an .a archive.
    if (file->offset) {
      offset = file->offset;
    }
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getOpenFileSlice(sys::fs::convertFDToNativeFile(file->fd),
                                       file->name, file->filesize, offset);
    if (std::error_code EC = BufferOrErr.getError()) {
      message(LDPL_ERROR, EC.message().c_str());
      return LDPS_ERR;
    }
    Buffer = std::move(BufferOrErr.get());
    BufferRef = Buffer->getMemBufferRef();
  }

  *claimed = 1;

  Expected<std::unique_ptr<InputFile>> ObjOrErr = InputFile::create(BufferRef);
  if (!ObjOrErr) {
    handleAllErrors(ObjOrErr.takeError(), [&](const ErrorInfoBase &EI) {
      std::error_code EC = EI.convertToErrorCode();
      if (EC == object::object_error::invalid_file_type ||
          EC == object::object_error::bitcode_section_not_found)
        *claimed = 0;
      else
        message(LDPL_FATAL,
                "LLVM gold plugin has failed to create LTO module: %s",
                EI.message().c_str());
    });

    return *claimed ? LDPS_ERR : LDPS_OK;
  }

  std::unique_ptr<InputFile> Obj = std::move(*ObjOrErr);

  Modules.emplace_back();
  claimed_file &cf = Modules.back();

  cf.handle = file->handle;
  // Keep track of the first handle for each file descriptor, since there are
  // multiple in the case of an archive. This is used later in the case of
  // ThinLTO parallel backends to ensure that each file is only opened and
  // released once.
  auto LeaderHandle =
      FDToLeaderHandle.insert(std::make_pair(file->fd, file->handle)).first;
  cf.leader_handle = LeaderHandle->second;
  // Save the filesize since for parallel ThinLTO backends we can only
  // invoke get_input_file once per archive (only for the leader handle).
  cf.filesize = file->filesize;
  // In the case of an archive library, all but the first member must have a
  // non-zero offset, which we can append to the file name to obtain a
  // unique name.
  cf.name = file->name;
  if (file->offset)
    cf.name += ".llvm." + std::to_string(file->offset) + "." +
               sys::path::filename(Obj->getSourceFileName()).str();

  for (auto &Sym : Obj->symbols()) {
    cf.syms.push_back(ld_plugin_symbol());
    ld_plugin_symbol &sym = cf.syms.back();
    sym.version = nullptr;
    StringRef Name = Sym.getName();
    sym.name = strdup(Name.str().c_str());

    ResolutionInfo &Res = ResInfo[Name];

    Res.CanOmitFromDynSym &= Sym.canBeOmittedFromSymbolTable();

    sym.visibility = LDPV_DEFAULT;
    GlobalValue::VisibilityTypes Vis = Sym.getVisibility();
    if (Vis != GlobalValue::DefaultVisibility)
      Res.DefaultVisibility = false;
    switch (Vis) {
    case GlobalValue::DefaultVisibility:
      break;
    case GlobalValue::HiddenVisibility:
      sym.visibility = LDPV_HIDDEN;
      break;
    case GlobalValue::ProtectedVisibility:
      sym.visibility = LDPV_PROTECTED;
      break;
    }

    if (Sym.isUndefined()) {
      sym.def = LDPK_UNDEF;
      if (Sym.isWeak())
        sym.def = LDPK_WEAKUNDEF;
    } else if (Sym.isCommon())
      sym.def = LDPK_COMMON;
    else if (Sym.isWeak())
      sym.def = LDPK_WEAKDEF;
    else
      sym.def = LDPK_DEF;

    sym.size = 0;
    sym.comdat_key = nullptr;
    int CI = Sym.getComdatIndex();
    if (CI != -1) {
      // Not setting comdat_key for nodeduplicate ensuress we don't deduplicate.
      std::pair<StringRef, Comdat::SelectionKind> C = Obj->getComdatTable()[CI];
      if (C.second != Comdat::NoDeduplicate)
        sym.comdat_key = strdup(C.first.str().c_str());
    }

    sym.resolution = LDPR_UNKNOWN;
  }

  if (!cf.syms.empty()) {
    if (add_symbols(cf.handle, cf.syms.size(), cf.syms.data()) != LDPS_OK) {
      message(LDPL_ERROR, "Unable to add symbols!");
      return LDPS_ERR;
    }
  }

  // Handle any --wrap options passed to gold, which are than passed
  // along to the plugin.
  if (get_wrap_symbols) {
    const char **wrap_symbols;
    uint64_t count = 0;
    if (get_wrap_symbols(&count, &wrap_symbols) != LDPS_OK) {
      message(LDPL_ERROR, "Unable to get wrap symbols!");
      return LDPS_ERR;
    }
    for (uint64_t i = 0; i < count; i++) {
      StringRef Name = wrap_symbols[i];
      ResolutionInfo &Res = ResInfo[Name];
      ResolutionInfo &WrapRes = ResInfo["__wrap_" + Name.str()];
      ResolutionInfo &RealRes = ResInfo["__real_" + Name.str()];
      // Tell LTO not to inline symbols that will be overwritten.
      Res.CanInline = false;
      RealRes.CanInline = false;
      // Tell LTO not to eliminate symbols that will be used after renaming.
      Res.IsUsedInRegularObj = true;
      WrapRes.IsUsedInRegularObj = true;
    }
  }

  return LDPS_OK;
}

static void freeSymName(ld_plugin_symbol &Sym) {
  free(Sym.name);
  free(Sym.comdat_key);
  Sym.name = nullptr;
  Sym.comdat_key = nullptr;
}

/// Helper to get a file's symbols and a view into it via gold callbacks.
static const void *getSymbolsAndView(claimed_file &F) {
  ld_plugin_status status = get_symbols(F.handle, F.syms.size(), F.syms.data());
  if (status == LDPS_NO_SYMS)
    return nullptr;

  if (status != LDPS_OK)
    message(LDPL_FATAL, "Failed to get symbol information");

  const void *View;
  if (get_view(F.handle, &View) != LDPS_OK)
    message(LDPL_FATAL, "Failed to get a view of file");

  return View;
}

/// Parse the thinlto-object-suffix-replace option into the \p OldSuffix and
/// \p NewSuffix strings, if it was specified.
static void getThinLTOOldAndNewSuffix(std::string &OldSuffix,
                                      std::string &NewSuffix) {
  assert(options::thinlto_object_suffix_replace.empty() ||
         options::thinlto_object_suffix_replace.find(';') != StringRef::npos);
  StringRef SuffixReplace = options::thinlto_object_suffix_replace;
  auto Split = SuffixReplace.split(';');
  OldSuffix = std::string(Split.first);
  NewSuffix = std::string(Split.second);
}

/// Given the original \p Path to an output file, replace any filename
/// suffix matching \p OldSuffix with \p NewSuffix.
static std::string getThinLTOObjectFileName(StringRef Path, StringRef OldSuffix,
                                            StringRef NewSuffix) {
  if (Path.consume_back(OldSuffix))
    return (Path + NewSuffix).str();
  return std::string(Path);
}

// Returns true if S is valid as a C language identifier.
static bool isValidCIdentifier(StringRef S) {
  return !S.empty() && (isAlpha(S[0]) || S[0] == '_') &&
         llvm::all_of(llvm::drop_begin(S),
                      [](char C) { return C == '_' || isAlnum(C); });
}

static bool isUndefined(ld_plugin_symbol &Sym) {
  return Sym.def == LDPK_UNDEF || Sym.def == LDPK_WEAKUNDEF;
}

static void addModule(LTO &Lto, claimed_file &F, const void *View,
                      StringRef Filename) {
  MemoryBufferRef BufferRef(StringRef((const char *)View, F.filesize),
                            Filename);
  Expected<std::unique_ptr<InputFile>> ObjOrErr = InputFile::create(BufferRef);

  if (!ObjOrErr)
    message(LDPL_FATAL, "Could not read bitcode from file : %s",
            toString(ObjOrErr.takeError()).c_str());

  unsigned SymNum = 0;
  std::unique_ptr<InputFile> Input = std::move(ObjOrErr.get());
  auto InputFileSyms = Input->symbols();
  assert(InputFileSyms.size() == F.syms.size());
  std::vector<SymbolResolution> Resols(F.syms.size());
  for (ld_plugin_symbol &Sym : F.syms) {
    const InputFile::Symbol &InpSym = InputFileSyms[SymNum];
    SymbolResolution &R = Resols[SymNum++];

    ld_plugin_symbol_resolution Resolution =
        (ld_plugin_symbol_resolution)Sym.resolution;

    ResolutionInfo &Res = ResInfo[Sym.name];

    switch (Resolution) {
    case LDPR_UNKNOWN:
      llvm_unreachable("Unexpected resolution");

    case LDPR_RESOLVED_IR:
    case LDPR_RESOLVED_EXEC:
    case LDPR_PREEMPTED_IR:
    case LDPR_PREEMPTED_REG:
    case LDPR_UNDEF:
      break;

    case LDPR_RESOLVED_DYN:
      R.ExportDynamic = true;
      break;

    case LDPR_PREVAILING_DEF_IRONLY:
      R.Prevailing = !isUndefined(Sym);
      break;

    case LDPR_PREVAILING_DEF:
      R.Prevailing = !isUndefined(Sym);
      R.VisibleToRegularObj = true;
      break;

    case LDPR_PREVAILING_DEF_IRONLY_EXP:
      R.Prevailing = !isUndefined(Sym);
      // Identify symbols exported dynamically, and that therefore could be
      // referenced by a shared library not visible to the linker.
      R.ExportDynamic = true;
      if (!Res.CanOmitFromDynSym)
        R.VisibleToRegularObj = true;
      break;
    }

    // If the symbol has a C identifier section name, we need to mark
    // it as visible to a regular object so that LTO will keep it around
    // to ensure the linker generates special __start_<secname> and
    // __stop_<secname> symbols which may be used elsewhere.
    if (isValidCIdentifier(InpSym.getSectionName()))
      R.VisibleToRegularObj = true;

    if (Resolution != LDPR_RESOLVED_DYN && Resolution != LDPR_UNDEF &&
        (IsExecutable || !Res.DefaultVisibility))
      R.FinalDefinitionInLinkageUnit = true;

    if (!Res.CanInline)
      R.LinkerRedefined = true;

    if (Res.IsUsedInRegularObj)
      R.VisibleToRegularObj = true;

    freeSymName(Sym);
  }

  check(Lto.add(std::move(Input), Resols),
        std::string("Failed to link module ") + F.name);
}

static void recordFile(const std::string &Filename, bool TempOutFile) {
  if (add_input_file(Filename.c_str()) != LDPS_OK)
    message(LDPL_FATAL,
            "Unable to add .o file to the link. File left behind in: %s",
            Filename.c_str());
  if (TempOutFile)
    Cleanup.push_back(Filename);
}

/// Return the desired output filename given a base input name, a flag
/// indicating whether a temp file should be generated, and an optional task id.
/// The new filename generated is returned in \p NewFilename.
static int getOutputFileName(StringRef InFilename, bool TempOutFile,
                             SmallString<128> &NewFilename, int TaskID) {
  int FD = -1;
  if (TempOutFile) {
    std::error_code EC =
        sys::fs::createTemporaryFile("lto-llvm", "o", FD, NewFilename);
    if (EC)
      message(LDPL_FATAL, "Could not create temporary file: %s",
              EC.message().c_str());
  } else {
    NewFilename = InFilename;
    if (TaskID > 0)
      NewFilename += utostr(TaskID);
    std::error_code EC =
        sys::fs::openFileForWrite(NewFilename, FD, sys::fs::CD_CreateAlways);
    if (EC)
      message(LDPL_FATAL, "Could not open file %s: %s", NewFilename.c_str(),
              EC.message().c_str());
  }
  return FD;
}

/// Parse the thinlto_prefix_replace option into the \p OldPrefix and
/// \p NewPrefix strings, if it was specified.
static void getThinLTOOldAndNewPrefix(std::string &OldPrefix,
                                      std::string &NewPrefix) {
  StringRef PrefixReplace = options::thinlto_prefix_replace;
  assert(PrefixReplace.empty() || PrefixReplace.find(';') != StringRef::npos);
  auto Split = PrefixReplace.split(';');
  OldPrefix = std::string(Split.first);
  NewPrefix = std::string(Split.second);
}

/// Creates instance of LTO.
/// OnIndexWrite is callback to let caller know when LTO writes index files.
/// LinkedObjectsFile is an output stream to write the list of object files for
/// the final ThinLTO linking. Can be nullptr.
static std::unique_ptr<LTO> createLTO(IndexWriteCallback OnIndexWrite,
                                      raw_fd_ostream *LinkedObjectsFile) {
  Config Conf;
  ThinBackend Backend;

  Conf.CPU = options::mcpu;
  Conf.Options = codegen::InitTargetOptionsFromCodeGenFlags(Triple());

  // Disable the new X86 relax relocations since gold might not support them.
  // FIXME: Check the gold version or add a new option to enable them.
  Conf.Options.MCOptions.X86RelaxRelocations = false;

  // Toggle function/data sections.
  if (!codegen::getExplicitFunctionSections())
    Conf.Options.FunctionSections = SplitSections;
  if (!codegen::getExplicitDataSections())
    Conf.Options.DataSections = SplitSections;

  Conf.MAttrs = codegen::getMAttrs();
  Conf.RelocModel = RelocationModel;
  Conf.CodeModel = codegen::getExplicitCodeModel();
  std::optional<CodeGenOptLevel> CGOptLevelOrNone =
      CodeGenOpt::getLevel(options::OptLevel);
  assert(CGOptLevelOrNone && "Invalid optimization level");
  Conf.CGOptLevel = *CGOptLevelOrNone;
  Conf.DisableVerify = options::DisableVerify;
  Conf.OptLevel = options::OptLevel;
  Conf.PTO.LoopVectorization = options::OptLevel > 1;
  Conf.PTO.SLPVectorization = options::OptLevel > 1;
  Conf.AlwaysEmitRegularLTOObj = !options::obj_path.empty();

  if (options::thinlto_index_only) {
    std::string OldPrefix, NewPrefix;
    getThinLTOOldAndNewPrefix(OldPrefix, NewPrefix);
    Backend = createWriteIndexesThinBackend(
        OldPrefix, NewPrefix,
        // TODO: Add support for optional native object path in
        // thinlto_prefix_replace option to match lld.
        /*NativeObjectPrefix=*/"", options::thinlto_emit_imports_files,
        LinkedObjectsFile, OnIndexWrite);
  } else {
    Backend = createInProcessThinBackend(
        llvm::heavyweight_hardware_concurrency(options::Parallelism));
  }

  Conf.OverrideTriple = options::triple;
  Conf.DefaultTriple = sys::getDefaultTargetTriple();

  Conf.DiagHandler = diagnosticHandler;

  switch (options::TheOutputType) {
  case options::OT_NORMAL:
    break;

  case options::OT_DISABLE:
    Conf.PreOptModuleHook = [](size_t Task, const Module &M) { return false; };
    break;

  case options::OT_BC_ONLY:
    Conf.PostInternalizeModuleHook = [](size_t Task, const Module &M) {
      std::error_code EC;
      SmallString<128> TaskFilename;
      getOutputFileName(output_name, /* TempOutFile */ false, TaskFilename,
                        Task);
      raw_fd_ostream OS(TaskFilename, EC, sys::fs::OpenFlags::OF_None);
      if (EC)
        message(LDPL_FATAL, "Failed to write the output file.");
      WriteBitcodeToFile(M, OS, /* ShouldPreserveUseListOrder */ false);
      return false;
    };
    break;

  case options::OT_SAVE_TEMPS:
    check(Conf.addSaveTemps(output_name + ".",
                            /* UseInputModulePath */ true));
    break;
  case options::OT_ASM_ONLY:
    Conf.CGFileType = CodeGenFileType::AssemblyFile;
    Conf.Options.MCOptions.AsmVerbose = true;
    break;
  }

  if (!options::sample_profile.empty())
    Conf.SampleProfile = options::sample_profile;

  if (!options::cs_profile_path.empty())
    Conf.CSIRProfile = options::cs_profile_path;
  Conf.RunCSIRInstr = options::cs_pgo_gen;

  Conf.DwoDir = options::dwo_dir;

  // Set up optimization remarks handling.
  Conf.RemarksFilename = options::RemarksFilename;
  Conf.RemarksPasses = options::RemarksPasses;
  Conf.RemarksWithHotness = options::RemarksWithHotness;
  Conf.RemarksHotnessThreshold = options::RemarksHotnessThreshold;
  Conf.RemarksFormat = options::RemarksFormat;

  // Debug new pass manager if requested
  Conf.DebugPassManager = options::debug_pass_manager;

  Conf.HasWholeProgramVisibility = options::whole_program_visibility;

  Conf.StatsFile = options::stats_file;

  Conf.TimeTraceEnabled = !options::time_trace_file.empty();
  Conf.TimeTraceGranularity = options::time_trace_granularity;

  return std::make_unique<LTO>(std::move(Conf), Backend,
                                options::ParallelCodeGenParallelismLevel);
}

// Write empty files that may be expected by a distributed build
// system when invoked with thinlto_index_only. This is invoked when
// the linker has decided not to include the given module in the
// final link. Frequently the distributed build system will want to
// confirm that all expected outputs are created based on all of the
// modules provided to the linker.
// If SkipModule is true then .thinlto.bc should contain just
// SkipModuleByDistributedBackend flag which requests distributed backend
// to skip the compilation of the corresponding module and produce an empty
// object file.
static void writeEmptyDistributedBuildOutputs(const std::string &ModulePath,
                                              const std::string &OldPrefix,
                                              const std::string &NewPrefix,
                                              bool SkipModule) {
  std::string NewModulePath =
      getThinLTOOutputFile(ModulePath, OldPrefix, NewPrefix);
  std::error_code EC;
  {
    raw_fd_ostream OS(NewModulePath + ".thinlto.bc", EC,
                      sys::fs::OpenFlags::OF_None);
    if (EC)
      message(LDPL_FATAL, "Failed to write '%s': %s",
              (NewModulePath + ".thinlto.bc").c_str(), EC.message().c_str());

    if (SkipModule) {
      ModuleSummaryIndex Index(/*HaveGVs*/ false);
      Index.setSkipModuleByDistributedBackend();
      writeIndexToFile(Index, OS, nullptr);
    }
  }
  if (options::thinlto_emit_imports_files) {
    raw_fd_ostream OS(NewModulePath + ".imports", EC,
                      sys::fs::OpenFlags::OF_None);
    if (EC)
      message(LDPL_FATAL, "Failed to write '%s': %s",
              (NewModulePath + ".imports").c_str(), EC.message().c_str());
  }
}

// Creates and returns output stream with a list of object files for final
// linking of distributed ThinLTO.
static std::unique_ptr<raw_fd_ostream> CreateLinkedObjectsFile() {
  if (options::thinlto_linked_objects_file.empty())
    return nullptr;
  assert(options::thinlto_index_only);
  std::error_code EC;
  auto LinkedObjectsFile = std::make_unique<raw_fd_ostream>(
      options::thinlto_linked_objects_file, EC, sys::fs::OpenFlags::OF_None);
  if (EC)
    message(LDPL_FATAL, "Failed to create '%s': %s",
            options::thinlto_linked_objects_file.c_str(), EC.message().c_str());
  return LinkedObjectsFile;
}

/// Runs LTO and return a list of pairs <FileName, IsTemporary>.
static std::vector<std::pair<SmallString<128>, bool>> runLTO() {
  // Map to own RAII objects that manage the file opening and releasing
  // interfaces with gold. This is needed only for ThinLTO mode, since
  // unlike regular LTO, where addModule will result in the opened file
  // being merged into a new combined module, we need to keep these files open
  // through Lto->run().
  DenseMap<void *, std::unique_ptr<PluginInputFile>> HandleToInputFile;

  // Owns string objects and tells if index file was already created.
  StringMap<bool> ObjectToIndexFileState;

  std::unique_ptr<raw_fd_ostream> LinkedObjects = CreateLinkedObjectsFile();
  std::unique_ptr<LTO> Lto = createLTO(
      [&ObjectToIndexFileState](const std::string &Identifier) {
        ObjectToIndexFileState[Identifier] = true;
      },
      LinkedObjects.get());

  std::string OldPrefix, NewPrefix;
  if (options::thinlto_index_only)
    getThinLTOOldAndNewPrefix(OldPrefix, NewPrefix);

  std::string OldSuffix, NewSuffix;
  getThinLTOOldAndNewSuffix(OldSuffix, NewSuffix);

  for (claimed_file &F : Modules) {
    if (options::thinlto && !HandleToInputFile.count(F.leader_handle))
      HandleToInputFile.insert(std::make_pair(
          F.leader_handle, std::make_unique<PluginInputFile>(F.handle)));
    // In case we are thin linking with a minimized bitcode file, ensure
    // the module paths encoded in the index reflect where the backends
    // will locate the full bitcode files for compiling/importing.
    std::string Identifier =
        getThinLTOObjectFileName(F.name, OldSuffix, NewSuffix);
    auto ObjFilename = ObjectToIndexFileState.insert({Identifier, false});
    assert(ObjFilename.second);
    if (const void *View = getSymbolsAndView(F))
      addModule(*Lto, F, View, ObjFilename.first->first());
    else if (options::thinlto_index_only) {
      ObjFilename.first->second = true;
      writeEmptyDistributedBuildOutputs(Identifier, OldPrefix, NewPrefix,
                                        /* SkipModule */ true);
    }
  }

  SmallString<128> Filename;
  // Note that getOutputFileName will append a unique ID for each task
  if (!options::obj_path.empty())
    Filename = options::obj_path;
  else if (options::TheOutputType == options::OT_SAVE_TEMPS)
    Filename = output_name + ".lto.o";
  else if (options::TheOutputType == options::OT_ASM_ONLY)
    Filename = output_name;
  bool SaveTemps = !Filename.empty();

  size_t MaxTasks = Lto->getMaxTasks();
  std::vector<std::pair<SmallString<128>, bool>> Files(MaxTasks);

  auto AddStream =
      [&](size_t Task,
          const Twine &ModuleName) -> std::unique_ptr<CachedFileStream> {
    Files[Task].second = !SaveTemps;
    int FD = getOutputFileName(Filename, /* TempOutFile */ !SaveTemps,
                               Files[Task].first, Task);
    return std::make_unique<CachedFileStream>(
        std::make_unique<llvm::raw_fd_ostream>(FD, true));
  };

  auto AddBuffer = [&](size_t Task, const Twine &moduleName,
                       std::unique_ptr<MemoryBuffer> MB) {
    *AddStream(Task, moduleName)->OS << MB->getBuffer();
  };

  FileCache Cache;
  if (!options::cache_dir.empty())
    Cache = check(localCache("ThinLTO", "Thin", options::cache_dir, AddBuffer));

  check(Lto->run(AddStream, Cache));

  // Write empty output files that may be expected by the distributed build
  // system.
  if (options::thinlto_index_only)
    for (auto &Identifier : ObjectToIndexFileState)
      if (!Identifier.getValue())
        writeEmptyDistributedBuildOutputs(std::string(Identifier.getKey()),
                                          OldPrefix, NewPrefix,
                                          /* SkipModule */ false);

  return Files;
}

/// gold informs us that all symbols have been read. At this point, we use
/// get_symbols to see if any of our definitions have been overridden by a
/// native object file. Then, perform optimization and codegen.
static ld_plugin_status allSymbolsReadHook() {
  if (Modules.empty())
    return LDPS_OK;

  if (unsigned NumOpts = options::extra.size())
    cl::ParseCommandLineOptions(NumOpts, &options::extra[0]);

  // Initialize time trace profiler
  if (!options::time_trace_file.empty())
    llvm::timeTraceProfilerInitialize(options::time_trace_granularity,
                                      options::extra.size() ? options::extra[0]
                                                            : "LLVMgold");
  auto FinalizeTimeTrace = llvm::make_scope_exit([&]() {
    if (!llvm::timeTraceProfilerEnabled())
      return;
    assert(!options::time_trace_file.empty());
    check(llvm::timeTraceProfilerWrite(options::time_trace_file, output_name));
    llvm::timeTraceProfilerCleanup();
  });

  std::vector<std::pair<SmallString<128>, bool>> Files = runLTO();

  if (options::TheOutputType == options::OT_DISABLE ||
      options::TheOutputType == options::OT_BC_ONLY ||
      options::TheOutputType == options::OT_ASM_ONLY)
    return LDPS_OK;

  if (options::thinlto_index_only) {
    llvm_shutdown();
    cleanup_hook();
    exit(0);
  }

  for (const auto &F : Files)
    if (!F.first.empty())
      recordFile(std::string(F.first.str()), F.second);

  if (!options::extra_library_path.empty() &&
      set_extra_library_path(options::extra_library_path.c_str()) != LDPS_OK)
    message(LDPL_FATAL, "Unable to set the extra library path.");

  return LDPS_OK;
}

static ld_plugin_status all_symbols_read_hook(void) {
  ld_plugin_status Ret = allSymbolsReadHook();
  llvm_shutdown();

  if (options::TheOutputType == options::OT_BC_ONLY ||
      options::TheOutputType == options::OT_ASM_ONLY ||
      options::TheOutputType == options::OT_DISABLE) {
    if (options::TheOutputType == options::OT_DISABLE) {
      // Remove the output file here since ld.bfd creates the output file
      // early.
      std::error_code EC = sys::fs::remove(output_name);
      if (EC)
        message(LDPL_ERROR, "Failed to delete '%s': %s", output_name.c_str(),
                EC.message().c_str());
    }
    exit(0);
  }

  return Ret;
}

static ld_plugin_status cleanup_hook(void) {
  for (std::string &Name : Cleanup) {
    std::error_code EC = sys::fs::remove(Name);
    if (EC)
      message(LDPL_ERROR, "Failed to delete '%s': %s", Name.c_str(),
              EC.message().c_str());
  }

  // Prune cache
  if (!options::cache_dir.empty()) {
    CachePruningPolicy policy = check(parseCachePruningPolicy(options::cache_policy));
    pruneCache(options::cache_dir, policy);
  }

  return LDPS_OK;
}
