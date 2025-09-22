//===- DriverUtils.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Config.h"
#include "Driver.h"
#include "InputFiles.h"
#include "ObjC.h"
#include "Target.h"

#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Reproduce.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/TextAPIReader.h"

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::opt;
using namespace llvm::sys;
using namespace lld;
using namespace lld::macho;

// Create prefix string literals used in Options.td
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Options.inc"
#undef PREFIX

// Create table mapping all options defined in Options.td
static constexpr OptTable::Info optInfo[] = {
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

MachOOptTable::MachOOptTable() : GenericOptTable(optInfo) {}

// Set color diagnostics according to --color-diagnostics={auto,always,never}
// or --no-color-diagnostics flags.
static void handleColorDiagnostics(InputArgList &args) {
  const Arg *arg =
      args.getLastArg(OPT_color_diagnostics, OPT_color_diagnostics_eq,
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

InputArgList MachOOptTable::parse(ArrayRef<const char *> argv) {
  // Make InputArgList from string vectors.
  unsigned missingIndex;
  unsigned missingCount;
  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());

  // Expand response files (arguments in the form of @<filename>)
  // and then parse the argument again.
  cl::ExpandResponseFiles(saver(), cl::TokenizeGNUCommandLine, vec);
  InputArgList args = ParseArgs(vec, missingIndex, missingCount);

  // Handle -fatal_warnings early since it converts missing argument warnings
  // to errors.
  errorHandler().fatalWarnings = args.hasArg(OPT_fatal_warnings);
  errorHandler().suppressWarnings = args.hasArg(OPT_w);

  if (missingCount)
    error(Twine(args.getArgString(missingIndex)) + ": missing argument");

  handleColorDiagnostics(args);

  for (const Arg *arg : args.filtered(OPT_UNKNOWN)) {
    std::string nearest;
    if (findNearest(arg->getAsString(args), nearest) > 1)
      error("unknown argument '" + arg->getAsString(args) + "'");
    else
      error("unknown argument '" + arg->getAsString(args) +
            "', did you mean '" + nearest + "'");
  }
  return args;
}

void MachOOptTable::printHelp(const char *argv0, bool showHidden) const {
  OptTable::printHelp(lld::outs(),
                      (std::string(argv0) + " [options] file...").c_str(),
                      "LLVM Linker", showHidden);
  lld::outs() << "\n";
}

static std::string rewritePath(StringRef s) {
  if (fs::exists(s))
    return relativeToRoot(s);
  return std::string(s);
}

static std::string rewriteInputPath(StringRef s) {
  // Don't bother rewriting "absolute" paths that are actually under the
  // syslibroot; simply rewriting the syslibroot is sufficient.
  if (rerootPath(s) == s && fs::exists(s))
    return relativeToRoot(s);
  return std::string(s);
}

// Reconstructs command line arguments so that so that you can re-run
// the same command with the same inputs. This is for --reproduce.
std::string macho::createResponseFile(const InputArgList &args) {
  SmallString<0> data;
  raw_svector_ostream os(data);

  // Copy the command line to the output while rewriting paths.
  for (const Arg *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_reproduce:
      break;
    case OPT_INPUT:
      os << quote(rewriteInputPath(arg->getValue())) << "\n";
      break;
    case OPT_o:
      os << "-o " << quote(path::filename(arg->getValue())) << "\n";
      break;
    case OPT_filelist:
      if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue()))
        for (StringRef path : args::getLines(*buffer))
          os << quote(rewriteInputPath(path)) << "\n";
      break;
    case OPT_force_load:
    case OPT_weak_library:
    case OPT_load_hidden:
      os << arg->getSpelling() << " "
         << quote(rewriteInputPath(arg->getValue())) << "\n";
      break;
    case OPT_F:
    case OPT_L:
    case OPT_bundle_loader:
    case OPT_exported_symbols_list:
    case OPT_order_file:
    case OPT_syslibroot:
    case OPT_unexported_symbols_list:
      os << arg->getSpelling() << " " << quote(rewritePath(arg->getValue()))
         << "\n";
      break;
    case OPT_sectcreate:
      os << arg->getSpelling() << " " << quote(arg->getValue(0)) << " "
         << quote(arg->getValue(1)) << " "
         << quote(rewritePath(arg->getValue(2))) << "\n";
      break;
    default:
      os << toString(*arg) << "\n";
    }
  }
  return std::string(data);
}

static void searchedDylib(const Twine &path, bool found) {
  if (config->printDylibSearch)
    message("searched " + path + (found ? ", found " : ", not found"));
  if (!found)
    depTracker->logFileNotFound(path);
}

std::optional<StringRef> macho::resolveDylibPath(StringRef dylibPath) {
  // TODO: if a tbd and dylib are both present, we should check to make sure
  // they are consistent.
  SmallString<261> tbdPath = dylibPath;
  path::replace_extension(tbdPath, ".tbd");
  bool tbdExists = fs::exists(tbdPath);
  searchedDylib(tbdPath, tbdExists);
  if (tbdExists)
    return saver().save(tbdPath.str());

  bool dylibExists = fs::exists(dylibPath);
  searchedDylib(dylibPath, dylibExists);
  if (dylibExists)
    return saver().save(dylibPath);
  return {};
}

// It's not uncommon to have multiple attempts to load a single dylib,
// especially if it's a commonly re-exported core library.
static DenseMap<CachedHashStringRef, DylibFile *> loadedDylibs;

DylibFile *macho::loadDylib(MemoryBufferRef mbref, DylibFile *umbrella,
                            bool isBundleLoader, bool explicitlyLinked) {
  CachedHashStringRef path(mbref.getBufferIdentifier());
  DylibFile *&file = loadedDylibs[path];
  if (file) {
    if (explicitlyLinked)
      file->setExplicitlyLinked();
    return file;
  }

  DylibFile *newFile;
  file_magic magic = identify_magic(mbref.getBuffer());
  if (magic == file_magic::tapi_file) {
    Expected<std::unique_ptr<InterfaceFile>> result = TextAPIReader::get(mbref);
    if (!result) {
      error("could not load TAPI file at " + mbref.getBufferIdentifier() +
            ": " + toString(result.takeError()));
      return nullptr;
    }
    file =
        make<DylibFile>(**result, umbrella, isBundleLoader, explicitlyLinked);

    // parseReexports() can recursively call loadDylib(). That's fine since
    // we wrote the DylibFile we just loaded to the loadDylib cache via the
    // `file` reference. But the recursive load can grow loadDylibs, so the
    // `file` reference might become invalid after parseReexports() -- so copy
    // the pointer it refers to before continuing.
    newFile = file;
    if (newFile->exportingFile)
      newFile->parseReexports(**result);
  } else {
    assert(magic == file_magic::macho_dynamically_linked_shared_lib ||
           magic == file_magic::macho_dynamically_linked_shared_lib_stub ||
           magic == file_magic::macho_executable ||
           magic == file_magic::macho_bundle);
    file = make<DylibFile>(mbref, umbrella, isBundleLoader, explicitlyLinked);

    // parseLoadCommands() can also recursively call loadDylib(). See comment
    // in previous block for why this means we must copy `file` here.
    newFile = file;
    if (newFile->exportingFile)
      newFile->parseLoadCommands(mbref);
  }
  return newFile;
}

void macho::resetLoadedDylibs() { loadedDylibs.clear(); }

std::optional<StringRef>
macho::findPathCombination(const Twine &name,
                           const std::vector<StringRef> &roots,
                           ArrayRef<StringRef> extensions) {
  SmallString<261> base;
  for (StringRef dir : roots) {
    base = dir;
    path::append(base, name);
    for (StringRef ext : extensions) {
      Twine location = base + ext;
      bool exists = fs::exists(location);
      searchedDylib(location, exists);
      if (exists)
        return saver().save(location.str());
    }
  }
  return {};
}

StringRef macho::rerootPath(StringRef path) {
  if (!path::is_absolute(path, path::Style::posix) || path.ends_with(".o"))
    return path;

  if (std::optional<StringRef> rerootedPath =
          findPathCombination(path, config->systemLibraryRoots))
    return *rerootedPath;

  return path;
}

uint32_t macho::getModTime(StringRef path) {
  if (config->zeroModTime)
    return 0;

  fs::file_status stat;
  if (!fs::status(path, stat))
    if (fs::exists(stat))
      return toTimeT(stat.getLastModificationTime());

  warn("failed to get modification time of " + path);
  return 0;
}

void macho::printArchiveMemberLoad(StringRef reason, const InputFile *f) {
  if (config->printEachFile)
    message(toString(f));
  if (config->printWhyLoad)
    message(reason + " forced load of " + toString(f));
}

macho::DependencyTracker::DependencyTracker(StringRef path)
    : path(path), active(!path.empty()) {
  if (active && fs::exists(path) && !fs::can_write(path)) {
    warn("Ignoring dependency_info option since specified path is not "
         "writeable.");
    active = false;
  }
}

void macho::DependencyTracker::write(StringRef version,
                                     const SetVector<InputFile *> &inputs,
                                     StringRef output) {
  if (!active)
    return;

  std::error_code ec;
  raw_fd_ostream os(path, ec, fs::OF_None);
  if (ec) {
    warn("Error writing dependency info to file");
    return;
  }

  auto addDep = [&os](DepOpCode opcode, const StringRef &path) {
    // XXX: Even though DepOpCode's underlying type is uint8_t,
    // this cast is still needed because Clang older than 10.x has a bug,
    // where it doesn't know to cast the enum to its underlying type.
    // Hence `<< DepOpCode` is ambiguous to it.
    os << static_cast<uint8_t>(opcode);
    os << path;
    os << '\0';
  };

  addDep(DepOpCode::Version, version);

  // Sort the input by its names.
  std::vector<StringRef> inputNames;
  inputNames.reserve(inputs.size());
  for (InputFile *f : inputs)
    inputNames.push_back(f->getName());
  llvm::sort(inputNames);

  for (const StringRef &in : inputNames)
    addDep(DepOpCode::Input, in);

  for (const std::string &f : notFounds)
    addDep(DepOpCode::NotFound, f);

  addDep(DepOpCode::Output, output);
}
