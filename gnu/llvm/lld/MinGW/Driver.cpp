//===- MinGW/Driver.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MinGW is a GNU development environment for Windows. It consists of GNU
// tools such as GCC and GNU ld. Unlike Cygwin, there's no POSIX-compatible
// layer, as it aims to be a native development toolchain.
//
// lld/MinGW is a drop-in replacement for GNU ld/MinGW.
//
// Being a native development tool, a MinGW linker is not very different from
// Microsoft link.exe, so a MinGW linker can be implemented as a thin wrapper
// for lld/COFF. This driver takes Unix-ish command line options, translates
// them to Windows-ish ones, and then passes them to lld/COFF.
//
// When this driver calls the lld/COFF driver, it passes a hidden option
// "-lldmingw" along with other user-supplied options, to run the lld/COFF
// linker in "MinGW mode".
//
// There are subtle differences between MS link.exe and GNU ld/MinGW, and GNU
// ld/MinGW implements a few GNU-specific features. Such features are directly
// implemented in lld/COFF and enabled only when the linker is running in MinGW
// mode.
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Driver.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

using namespace lld;
using namespace llvm::opt;
using namespace llvm;

// Create OptTable
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

// Create prefix string literals used in Options.td
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "Options.inc"
#undef PREFIX

// Create table mapping all options defined in Options.td
static constexpr opt::OptTable::Info infoTable[] = {
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
class MinGWOptTable : public opt::GenericOptTable {
public:
  MinGWOptTable() : opt::GenericOptTable(infoTable, false) {}
  opt::InputArgList parse(ArrayRef<const char *> argv);
};
} // namespace

static void printHelp(const char *argv0) {
  MinGWOptTable().printHelp(
      lld::outs(), (std::string(argv0) + " [options] file...").c_str(), "lld",
      false /*ShowHidden*/, true /*ShowAllAliases*/);
  lld::outs() << "\n";
}

static cl::TokenizerCallback getQuotingStyle() {
  if (Triple(sys::getProcessTriple()).getOS() == Triple::Win32)
    return cl::TokenizeWindowsCommandLine;
  return cl::TokenizeGNUCommandLine;
}

opt::InputArgList MinGWOptTable::parse(ArrayRef<const char *> argv) {
  unsigned missingIndex;
  unsigned missingCount;

  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());
  cl::ExpandResponseFiles(saver(), getQuotingStyle(), vec);
  opt::InputArgList args = this->ParseArgs(vec, missingIndex, missingCount);

  if (missingCount)
    error(StringRef(args.getArgString(missingIndex)) + ": missing argument");
  for (auto *arg : args.filtered(OPT_UNKNOWN))
    error("unknown argument: " + arg->getAsString(args));
  return args;
}

// Find a file by concatenating given paths.
static std::optional<std::string> findFile(StringRef path1,
                                           const Twine &path2) {
  SmallString<128> s;
  sys::path::append(s, path1, path2);
  if (sys::fs::exists(s))
    return std::string(s);
  return std::nullopt;
}

// This is for -lfoo. We'll look for libfoo.dll.a or libfoo.a from search paths.
static std::string
searchLibrary(StringRef name, ArrayRef<StringRef> searchPaths, bool bStatic) {
  if (name.starts_with(":")) {
    for (StringRef dir : searchPaths)
      if (std::optional<std::string> s = findFile(dir, name.substr(1)))
        return *s;
    error("unable to find library -l" + name);
    return "";
  }

  for (StringRef dir : searchPaths) {
    if (!bStatic) {
      if (std::optional<std::string> s = findFile(dir, "lib" + name + ".dll.a"))
        return *s;
      if (std::optional<std::string> s = findFile(dir, name + ".dll.a"))
        return *s;
    }
    if (std::optional<std::string> s = findFile(dir, "lib" + name + ".a"))
      return *s;
    if (std::optional<std::string> s = findFile(dir, name + ".lib"))
      return *s;
    if (!bStatic) {
      if (std::optional<std::string> s = findFile(dir, "lib" + name + ".dll"))
        return *s;
      if (std::optional<std::string> s = findFile(dir, name + ".dll"))
        return *s;
    }
  }
  error("unable to find library -l" + name);
  return "";
}

namespace lld {
namespace coff {
bool link(ArrayRef<const char *> argsArr, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput);
}

namespace mingw {
// Convert Unix-ish command line arguments to Windows-ish ones and
// then call coff::link.
bool link(ArrayRef<const char *> argsArr, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  auto *ctx = new CommonLinkerContext;
  ctx->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);

  MinGWOptTable parser;
  opt::InputArgList args = parser.parse(argsArr.slice(1));

  if (errorCount())
    return false;

  if (args.hasArg(OPT_help)) {
    printHelp(argsArr[0]);
    return true;
  }

  // A note about "compatible with GNU linkers" message: this is a hack for
  // scripts generated by GNU Libtool 2.4.6 (released in February 2014 and
  // still the newest version in March 2017) or earlier to recognize LLD as
  // a GNU compatible linker. As long as an output for the -v option
  // contains "GNU" or "with BFD", they recognize us as GNU-compatible.
  if (args.hasArg(OPT_v) || args.hasArg(OPT_version))
    message(getLLDVersion() + " (compatible with GNU linkers)");

  // The behavior of -v or --version is a bit strange, but this is
  // needed for compatibility with GNU linkers.
  if (args.hasArg(OPT_v) && !args.hasArg(OPT_INPUT) && !args.hasArg(OPT_l))
    return true;
  if (args.hasArg(OPT_version))
    return true;

  if (!args.hasArg(OPT_INPUT) && !args.hasArg(OPT_l)) {
    error("no input files");
    return false;
  }

  std::vector<std::string> linkArgs;
  auto add = [&](const Twine &s) { linkArgs.push_back(s.str()); };

  add("lld-link");
  add("-lldmingw");

  if (auto *a = args.getLastArg(OPT_entry)) {
    StringRef s = a->getValue();
    if (args.getLastArgValue(OPT_m) == "i386pe" && s.starts_with("_"))
      add("-entry:" + s.substr(1));
    else if (!s.empty())
      add("-entry:" + s);
    else
      add("-noentry");
  }

  if (args.hasArg(OPT_major_os_version, OPT_minor_os_version,
                  OPT_major_subsystem_version, OPT_minor_subsystem_version)) {
    StringRef majOSVer = args.getLastArgValue(OPT_major_os_version, "6");
    StringRef minOSVer = args.getLastArgValue(OPT_minor_os_version, "0");
    StringRef majSubSysVer = "6";
    StringRef minSubSysVer = "0";
    StringRef subSysName = "default";
    StringRef subSysVer;
    // Iterate over --{major,minor}-subsystem-version and --subsystem, and pick
    // the version number components from the last one of them that specifies
    // a version.
    for (auto *a : args.filtered(OPT_major_subsystem_version,
                                 OPT_minor_subsystem_version, OPT_subs)) {
      switch (a->getOption().getID()) {
      case OPT_major_subsystem_version:
        majSubSysVer = a->getValue();
        break;
      case OPT_minor_subsystem_version:
        minSubSysVer = a->getValue();
        break;
      case OPT_subs:
        std::tie(subSysName, subSysVer) = StringRef(a->getValue()).split(':');
        if (!subSysVer.empty()) {
          if (subSysVer.contains('.'))
            std::tie(majSubSysVer, minSubSysVer) = subSysVer.split('.');
          else
            majSubSysVer = subSysVer;
        }
        break;
      }
    }
    add("-osversion:" + majOSVer + "." + minOSVer);
    add("-subsystem:" + subSysName + "," + majSubSysVer + "." + minSubSysVer);
  } else if (args.hasArg(OPT_subs)) {
    StringRef subSys = args.getLastArgValue(OPT_subs, "default");
    StringRef subSysName, subSysVer;
    std::tie(subSysName, subSysVer) = subSys.split(':');
    StringRef sep = subSysVer.empty() ? "" : ",";
    add("-subsystem:" + subSysName + sep + subSysVer);
  }

  if (auto *a = args.getLastArg(OPT_out_implib))
    add("-implib:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_stack))
    add("-stack:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_output_def))
    add("-output-def:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_image_base))
    add("-base:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_map))
    add("-lldmap:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_reproduce))
    add("-reproduce:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_file_alignment))
    add("-filealign:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_section_alignment))
    add("-align:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_heap))
    add("-heap:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_threads))
    add("-threads:" + StringRef(a->getValue()));

  if (auto *a = args.getLastArg(OPT_o))
    add("-out:" + StringRef(a->getValue()));
  else if (args.hasArg(OPT_shared))
    add("-out:a.dll");
  else
    add("-out:a.exe");

  if (auto *a = args.getLastArg(OPT_pdb)) {
    add("-debug");
    StringRef v = a->getValue();
    if (!v.empty())
      add("-pdb:" + v);
    if (args.hasArg(OPT_strip_all)) {
      add("-debug:nodwarf,nosymtab");
    } else if (args.hasArg(OPT_strip_debug)) {
      add("-debug:nodwarf,symtab");
    }
  } else if (args.hasArg(OPT_strip_debug)) {
    add("-debug:symtab");
  } else if (!args.hasArg(OPT_strip_all)) {
    add("-debug:dwarf");
  }
  if (auto *a = args.getLastArg(OPT_build_id)) {
    StringRef v = a->getValue();
    if (v == "none")
      add("-build-id:no");
    else {
      if (!v.empty())
        warn("unsupported build id hashing: " + v + ", using default hashing.");
      add("-build-id");
    }
  } else {
    if (args.hasArg(OPT_strip_debug) || args.hasArg(OPT_strip_all))
      add("-build-id:no");
    else
      add("-build-id");
  }

  if (args.hasFlag(OPT_fatal_warnings, OPT_no_fatal_warnings, false))
    add("-WX");
  else
    add("-WX:no");

  if (args.hasFlag(OPT_enable_stdcall_fixup, OPT_disable_stdcall_fixup, false))
    add("-stdcall-fixup");
  else if (args.hasArg(OPT_disable_stdcall_fixup))
    add("-stdcall-fixup:no");

  if (args.hasArg(OPT_shared))
    add("-dll");
  if (args.hasArg(OPT_verbose))
    add("-verbose");
  if (args.hasArg(OPT_exclude_all_symbols))
    add("-exclude-all-symbols");
  if (args.hasArg(OPT_export_all_symbols))
    add("-export-all-symbols");
  if (args.hasArg(OPT_large_address_aware))
    add("-largeaddressaware");
  if (args.hasArg(OPT_kill_at))
    add("-kill-at");
  if (args.hasArg(OPT_appcontainer))
    add("-appcontainer");
  if (args.hasFlag(OPT_no_seh, OPT_disable_no_seh, false))
    add("-noseh");

  if (args.getLastArgValue(OPT_m) != "thumb2pe" &&
      args.getLastArgValue(OPT_m) != "arm64pe" &&
      args.getLastArgValue(OPT_m) != "arm64ecpe" &&
      args.hasFlag(OPT_disable_dynamicbase, OPT_dynamicbase, false))
    add("-dynamicbase:no");
  if (args.hasFlag(OPT_disable_high_entropy_va, OPT_high_entropy_va, false))
    add("-highentropyva:no");
  if (args.hasFlag(OPT_disable_nxcompat, OPT_nxcompat, false))
    add("-nxcompat:no");
  if (args.hasFlag(OPT_disable_tsaware, OPT_tsaware, false))
    add("-tsaware:no");

  if (args.hasFlag(OPT_disable_reloc_section, OPT_enable_reloc_section, false))
    add("-fixed");

  if (args.hasFlag(OPT_no_insert_timestamp, OPT_insert_timestamp, false))
    add("-timestamp:0");

  if (args.hasFlag(OPT_gc_sections, OPT_no_gc_sections, false))
    add("-opt:ref");
  else
    add("-opt:noref");

  if (args.hasFlag(OPT_demangle, OPT_no_demangle, true))
    add("-demangle");
  else
    add("-demangle:no");

  if (args.hasFlag(OPT_enable_auto_import, OPT_disable_auto_import, true))
    add("-auto-import");
  else
    add("-auto-import:no");
  if (args.hasFlag(OPT_enable_runtime_pseudo_reloc,
                   OPT_disable_runtime_pseudo_reloc, true))
    add("-runtime-pseudo-reloc");
  else
    add("-runtime-pseudo-reloc:no");

  if (args.hasFlag(OPT_allow_multiple_definition,
                   OPT_no_allow_multiple_definition, false))
    add("-force:multiple");

  if (auto *a = args.getLastArg(OPT_icf)) {
    StringRef s = a->getValue();
    if (s == "all")
      add("-opt:icf");
    else if (s == "safe")
      add("-opt:safeicf");
    else if (s == "none")
      add("-opt:noicf");
    else
      error("unknown parameter: --icf=" + s);
  } else {
    add("-opt:noicf");
  }

  if (auto *a = args.getLastArg(OPT_m)) {
    StringRef s = a->getValue();
    if (s == "i386pe")
      add("-machine:x86");
    else if (s == "i386pep")
      add("-machine:x64");
    else if (s == "thumb2pe")
      add("-machine:arm");
    else if (s == "arm64pe")
      add("-machine:arm64");
    else if (s == "arm64ecpe")
      add("-machine:arm64ec");
    else
      error("unknown parameter: -m" + s);
  }

  if (args.hasFlag(OPT_guard_cf, OPT_no_guard_cf, false)) {
    if (args.hasFlag(OPT_guard_longjmp, OPT_no_guard_longjmp, true))
      add("-guard:cf,longjmp");
    else
      add("-guard:cf,nolongjmp");
  } else if (args.hasFlag(OPT_guard_longjmp, OPT_no_guard_longjmp, false)) {
    auto *a = args.getLastArg(OPT_guard_longjmp);
    warn("parameter " + a->getSpelling() +
         " only takes effect when used with --guard-cf");
  }

  if (auto *a = args.getLastArg(OPT_error_limit)) {
    int n;
    StringRef s = a->getValue();
    if (s.getAsInteger(10, n))
      error(a->getSpelling() + ": number expected, but got " + s);
    else
      add("-errorlimit:" + s);
  }

  if (auto *a = args.getLastArg(OPT_rpath))
    warn("parameter " + a->getSpelling() + " has no effect on PE/COFF targets");

  for (auto *a : args.filtered(OPT_mllvm))
    add("-mllvm:" + StringRef(a->getValue()));

  if (auto *arg = args.getLastArg(OPT_plugin_opt_mcpu_eq))
    add("-mllvm:-mcpu=" + StringRef(arg->getValue()));
  if (auto *arg = args.getLastArg(OPT_lto_O))
    add("-opt:lldlto=" + StringRef(arg->getValue()));
  if (auto *arg = args.getLastArg(OPT_lto_CGO))
    add("-opt:lldltocgo=" + StringRef(arg->getValue()));
  if (auto *arg = args.getLastArg(OPT_plugin_opt_dwo_dir_eq))
    add("-dwodir:" + StringRef(arg->getValue()));
  if (args.hasArg(OPT_lto_cs_profile_generate))
    add("-lto-cs-profile-generate");
  if (auto *arg = args.getLastArg(OPT_lto_cs_profile_file))
    add("-lto-cs-profile-file:" + StringRef(arg->getValue()));
  if (args.hasArg(OPT_plugin_opt_emit_llvm))
    add("-lldemit:llvm");
  if (args.hasArg(OPT_lto_emit_asm))
    add("-lldemit:asm");
  if (auto *arg = args.getLastArg(OPT_lto_sample_profile))
    add("-lto-sample-profile:" + StringRef(arg->getValue()));

  if (auto *a = args.getLastArg(OPT_thinlto_cache_dir))
    add("-lldltocache:" + StringRef(a->getValue()));
  if (auto *a = args.getLastArg(OPT_thinlto_cache_policy))
    add("-lldltocachepolicy:" + StringRef(a->getValue()));
  if (args.hasArg(OPT_thinlto_emit_imports_files))
    add("-thinlto-emit-imports-files");
  if (args.hasArg(OPT_thinlto_index_only))
    add("-thinlto-index-only");
  if (auto *arg = args.getLastArg(OPT_thinlto_index_only_eq))
    add("-thinlto-index-only:" + StringRef(arg->getValue()));
  if (auto *arg = args.getLastArg(OPT_thinlto_jobs_eq))
    add("-opt:lldltojobs=" + StringRef(arg->getValue()));
  if (auto *arg = args.getLastArg(OPT_thinlto_object_suffix_replace_eq))
    add("-thinlto-object-suffix-replace:" + StringRef(arg->getValue()));
  if (auto *arg = args.getLastArg(OPT_thinlto_prefix_replace_eq))
    add("-thinlto-prefix-replace:" + StringRef(arg->getValue()));

  for (auto *a : args.filtered(OPT_plugin_opt_eq_minus))
    add("-mllvm:-" + StringRef(a->getValue()));

  // GCC collect2 passes -plugin-opt=path/to/lto-wrapper with an absolute or
  // relative path. Just ignore. If not ended with "lto-wrapper" (or
  // "lto-wrapper.exe" for GCC cross-compiled for Windows), consider it an
  // unsupported LLVMgold.so option and error.
  for (opt::Arg *arg : args.filtered(OPT_plugin_opt_eq)) {
    StringRef v(arg->getValue());
    if (!v.ends_with("lto-wrapper") && !v.ends_with("lto-wrapper.exe"))
      error(arg->getSpelling() + ": unknown plugin option '" + arg->getValue() +
            "'");
  }

  for (auto *a : args.filtered(OPT_Xlink))
    add(a->getValue());

  if (args.getLastArgValue(OPT_m) == "i386pe")
    add("-alternatename:__image_base__=___ImageBase");
  else
    add("-alternatename:__image_base__=__ImageBase");

  for (auto *a : args.filtered(OPT_require_defined))
    add("-include:" + StringRef(a->getValue()));
  for (auto *a : args.filtered(OPT_undefined))
    add("-includeoptional:" + StringRef(a->getValue()));
  for (auto *a : args.filtered(OPT_delayload))
    add("-delayload:" + StringRef(a->getValue()));
  for (auto *a : args.filtered(OPT_wrap))
    add("-wrap:" + StringRef(a->getValue()));
  for (auto *a : args.filtered(OPT_exclude_symbols))
    add("-exclude-symbols:" + StringRef(a->getValue()));

  std::vector<StringRef> searchPaths;
  for (auto *a : args.filtered(OPT_L)) {
    searchPaths.push_back(a->getValue());
    add("-libpath:" + StringRef(a->getValue()));
  }

  StringRef prefix = "";
  bool isStatic = false;
  for (auto *a : args) {
    switch (a->getOption().getID()) {
    case OPT_INPUT:
      if (StringRef(a->getValue()).ends_with_insensitive(".def"))
        add("-def:" + StringRef(a->getValue()));
      else
        add(prefix + StringRef(a->getValue()));
      break;
    case OPT_l:
      add(prefix + searchLibrary(a->getValue(), searchPaths, isStatic));
      break;
    case OPT_whole_archive:
      prefix = "-wholearchive:";
      break;
    case OPT_no_whole_archive:
      prefix = "";
      break;
    case OPT_Bstatic:
      isStatic = true;
      break;
    case OPT_Bdynamic:
      isStatic = false;
      break;
    }
  }

  if (errorCount())
    return false;

  if (args.hasArg(OPT_verbose) || args.hasArg(OPT__HASH_HASH_HASH))
    lld::errs() << llvm::join(linkArgs, " ") << "\n";

  if (args.hasArg(OPT__HASH_HASH_HASH))
    return true;

  // Repack vector of strings to vector of const char pointers for coff::link.
  std::vector<const char *> vec;
  for (const std::string &s : linkArgs)
    vec.push_back(s.c_str());
  // Pass the actual binary name, to make error messages be printed with
  // the right prefix.
  vec[0] = argsArr[0];

  // The context will be re-created in the COFF driver.
  lld::CommonLinkerContext::destroy();

  return coff::link(vec, stdoutOS, stderrOS, exitEarly, disableOutput);
}
} // namespace mingw
} // namespace lld
