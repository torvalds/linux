//===- Driver.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Driver.h"
#include "Config.h"
#include "ICF.h"
#include "InputFiles.h"
#include "LTO.h"
#include "MarkLive.h"
#include "ObjC.h"
#include "OutputSection.h"
#include "OutputSegment.h"
#include "SectionPriorities.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "UnwindInfoSection.h"
#include "Writer.h"

#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Driver.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Reproduce.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/PackedVersion.h"

#include <algorithm>

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm::sys;
using namespace lld;
using namespace lld::macho;

std::unique_ptr<Configuration> macho::config;
std::unique_ptr<DependencyTracker> macho::depTracker;

static HeaderFileType getOutputType(const InputArgList &args) {
  // TODO: -r, -dylinker, -preload...
  Arg *outputArg = args.getLastArg(OPT_bundle, OPT_dylib, OPT_execute);
  if (outputArg == nullptr)
    return MH_EXECUTE;

  switch (outputArg->getOption().getID()) {
  case OPT_bundle:
    return MH_BUNDLE;
  case OPT_dylib:
    return MH_DYLIB;
  case OPT_execute:
    return MH_EXECUTE;
  default:
    llvm_unreachable("internal error");
  }
}

static DenseMap<CachedHashStringRef, StringRef> resolvedLibraries;
static std::optional<StringRef> findLibrary(StringRef name) {
  CachedHashStringRef key(name);
  auto entry = resolvedLibraries.find(key);
  if (entry != resolvedLibraries.end())
    return entry->second;

  auto doFind = [&] {
    // Special case for Csu support files required for Mac OS X 10.7 and older
    // (crt1.o)
    if (name.ends_with(".o"))
      return findPathCombination(name, config->librarySearchPaths, {""});
    if (config->searchDylibsFirst) {
      if (std::optional<StringRef> path =
              findPathCombination("lib" + name, config->librarySearchPaths,
                                  {".tbd", ".dylib", ".so"}))
        return path;
      return findPathCombination("lib" + name, config->librarySearchPaths,
                                 {".a"});
    }
    return findPathCombination("lib" + name, config->librarySearchPaths,
                               {".tbd", ".dylib", ".so", ".a"});
  };

  std::optional<StringRef> path = doFind();
  if (path)
    resolvedLibraries[key] = *path;

  return path;
}

static DenseMap<CachedHashStringRef, StringRef> resolvedFrameworks;
static std::optional<StringRef> findFramework(StringRef name) {
  CachedHashStringRef key(name);
  auto entry = resolvedFrameworks.find(key);
  if (entry != resolvedFrameworks.end())
    return entry->second;

  SmallString<260> symlink;
  StringRef suffix;
  std::tie(name, suffix) = name.split(",");
  for (StringRef dir : config->frameworkSearchPaths) {
    symlink = dir;
    path::append(symlink, name + ".framework", name);

    if (!suffix.empty()) {
      // NOTE: we must resolve the symlink before trying the suffixes, because
      // there are no symlinks for the suffixed paths.
      SmallString<260> location;
      if (!fs::real_path(symlink, location)) {
        // only append suffix if realpath() succeeds
        Twine suffixed = location + suffix;
        if (fs::exists(suffixed))
          return resolvedFrameworks[key] = saver().save(suffixed.str());
      }
      // Suffix lookup failed, fall through to the no-suffix case.
    }

    if (std::optional<StringRef> path = resolveDylibPath(symlink.str()))
      return resolvedFrameworks[key] = *path;
  }
  return {};
}

static bool warnIfNotDirectory(StringRef option, StringRef path) {
  if (!fs::exists(path)) {
    warn("directory not found for option -" + option + path);
    return false;
  } else if (!fs::is_directory(path)) {
    warn("option -" + option + path + " references a non-directory path");
    return false;
  }
  return true;
}

static std::vector<StringRef>
getSearchPaths(unsigned optionCode, InputArgList &args,
               const std::vector<StringRef> &roots,
               const SmallVector<StringRef, 2> &systemPaths) {
  std::vector<StringRef> paths;
  StringRef optionLetter{optionCode == OPT_F ? "F" : "L"};
  for (StringRef path : args::getStrings(args, optionCode)) {
    // NOTE: only absolute paths are re-rooted to syslibroot(s)
    bool found = false;
    if (path::is_absolute(path, path::Style::posix)) {
      for (StringRef root : roots) {
        SmallString<261> buffer(root);
        path::append(buffer, path);
        // Do not warn about paths that are computed via the syslib roots
        if (fs::is_directory(buffer)) {
          paths.push_back(saver().save(buffer.str()));
          found = true;
        }
      }
    }
    if (!found && warnIfNotDirectory(optionLetter, path))
      paths.push_back(path);
  }

  // `-Z` suppresses the standard "system" search paths.
  if (args.hasArg(OPT_Z))
    return paths;

  for (const StringRef &path : systemPaths) {
    for (const StringRef &root : roots) {
      SmallString<261> buffer(root);
      path::append(buffer, path);
      if (fs::is_directory(buffer))
        paths.push_back(saver().save(buffer.str()));
    }
  }
  return paths;
}

static std::vector<StringRef> getSystemLibraryRoots(InputArgList &args) {
  std::vector<StringRef> roots;
  for (const Arg *arg : args.filtered(OPT_syslibroot))
    roots.push_back(arg->getValue());
  // NOTE: the final `-syslibroot` being `/` will ignore all roots
  if (!roots.empty() && roots.back() == "/")
    roots.clear();
  // NOTE: roots can never be empty - add an empty root to simplify the library
  // and framework search path computation.
  if (roots.empty())
    roots.emplace_back("");
  return roots;
}

static std::vector<StringRef>
getLibrarySearchPaths(InputArgList &args, const std::vector<StringRef> &roots) {
  return getSearchPaths(OPT_L, args, roots, {"/usr/lib", "/usr/local/lib"});
}

static std::vector<StringRef>
getFrameworkSearchPaths(InputArgList &args,
                        const std::vector<StringRef> &roots) {
  return getSearchPaths(OPT_F, args, roots,
                        {"/Library/Frameworks", "/System/Library/Frameworks"});
}

static llvm::CachePruningPolicy getLTOCachePolicy(InputArgList &args) {
  SmallString<128> ltoPolicy;
  auto add = [&ltoPolicy](Twine val) {
    if (!ltoPolicy.empty())
      ltoPolicy += ":";
    val.toVector(ltoPolicy);
  };
  for (const Arg *arg :
       args.filtered(OPT_thinlto_cache_policy_eq, OPT_prune_interval_lto,
                     OPT_prune_after_lto, OPT_max_relative_cache_size_lto)) {
    switch (arg->getOption().getID()) {
    case OPT_thinlto_cache_policy_eq:
      add(arg->getValue());
      break;
    case OPT_prune_interval_lto:
      if (!strcmp("-1", arg->getValue()))
        add("prune_interval=87600h"); // 10 years
      else
        add(Twine("prune_interval=") + arg->getValue() + "s");
      break;
    case OPT_prune_after_lto:
      add(Twine("prune_after=") + arg->getValue() + "s");
      break;
    case OPT_max_relative_cache_size_lto:
      add(Twine("cache_size=") + arg->getValue() + "%");
      break;
    }
  }
  return CHECK(parseCachePruningPolicy(ltoPolicy), "invalid LTO cache policy");
}

// What caused a given library to be loaded. Only relevant for archives.
// Note that this does not tell us *how* we should load the library, i.e.
// whether we should do it lazily or eagerly (AKA force loading). The "how" is
// decided within addFile().
enum class LoadType {
  CommandLine,      // Library was passed as a regular CLI argument
  CommandLineForce, // Library was passed via `-force_load`
  LCLinkerOption,   // Library was passed via LC_LINKER_OPTIONS
};

struct ArchiveFileInfo {
  ArchiveFile *file;
  bool isCommandLineLoad;
};

static DenseMap<StringRef, ArchiveFileInfo> loadedArchives;

static void saveThinArchiveToRepro(ArchiveFile const *file) {
  assert(tar && file->getArchive().isThin());

  Error e = Error::success();
  for (const object::Archive::Child &c : file->getArchive().children(e)) {
    MemoryBufferRef mb = CHECK(c.getMemoryBufferRef(),
                               toString(file) + ": failed to get buffer");
    tar->append(relativeToRoot(CHECK(c.getFullName(), file)), mb.getBuffer());
  }
  if (e)
    error(toString(file) +
          ": Archive::children failed: " + toString(std::move(e)));
}

static InputFile *addFile(StringRef path, LoadType loadType,
                          bool isLazy = false, bool isExplicit = true,
                          bool isBundleLoader = false,
                          bool isForceHidden = false) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return nullptr;
  MemoryBufferRef mbref = *buffer;
  InputFile *newFile = nullptr;

  file_magic magic = identify_magic(mbref.getBuffer());
  switch (magic) {
  case file_magic::archive: {
    bool isCommandLineLoad = loadType != LoadType::LCLinkerOption;
    // Avoid loading archives twice. If the archives are being force-loaded,
    // loading them twice would create duplicate symbol errors. In the
    // non-force-loading case, this is just a minor performance optimization.
    // We don't take a reference to cachedFile here because the
    // loadArchiveMember() call below may recursively call addFile() and
    // invalidate this reference.
    auto entry = loadedArchives.find(path);

    ArchiveFile *file;
    if (entry == loadedArchives.end()) {
      // No cached archive, we need to create a new one
      std::unique_ptr<object::Archive> archive = CHECK(
          object::Archive::create(mbref), path + ": failed to parse archive");

      if (!archive->isEmpty() && !archive->hasSymbolTable())
        error(path + ": archive has no index; run ranlib to add one");
      file = make<ArchiveFile>(std::move(archive), isForceHidden);

      if (tar && file->getArchive().isThin())
        saveThinArchiveToRepro(file);
    } else {
      file = entry->second.file;
      // Command-line loads take precedence. If file is previously loaded via
      // command line, or is loaded via LC_LINKER_OPTION and being loaded via
      // LC_LINKER_OPTION again, using the cached archive is enough.
      if (entry->second.isCommandLineLoad || !isCommandLineLoad)
        return file;
    }

    bool isLCLinkerForceLoad = loadType == LoadType::LCLinkerOption &&
                               config->forceLoadSwift &&
                               path::filename(path).starts_with("libswift");
    if ((isCommandLineLoad && config->allLoad) ||
        loadType == LoadType::CommandLineForce || isLCLinkerForceLoad) {
      if (readFile(path)) {
        Error e = Error::success();
        for (const object::Archive::Child &c : file->getArchive().children(e)) {
          StringRef reason;
          switch (loadType) {
            case LoadType::LCLinkerOption:
              reason = "LC_LINKER_OPTION";
              break;
            case LoadType::CommandLineForce:
              reason = "-force_load";
              break;
            case LoadType::CommandLine:
              reason = "-all_load";
              break;
          }
          if (Error e = file->fetch(c, reason)) {
            if (config->warnThinArchiveMissingMembers)
              warn(toString(file) + ": " + reason +
                   " failed to load archive member: " + toString(std::move(e)));
            else
              llvm::consumeError(std::move(e));
          }
        }
        if (e)
          error(toString(file) +
                ": Archive::children failed: " + toString(std::move(e)));
      }
    } else if (isCommandLineLoad && config->forceLoadObjC) {
      for (const object::Archive::Symbol &sym : file->getArchive().symbols())
        if (sym.getName().starts_with(objc::symbol_names::klass))
          file->fetch(sym);

      // TODO: no need to look for ObjC sections for a given archive member if
      // we already found that it contains an ObjC symbol.
      if (readFile(path)) {
        Error e = Error::success();
        for (const object::Archive::Child &c : file->getArchive().children(e)) {
          Expected<MemoryBufferRef> mb = c.getMemoryBufferRef();
          if (!mb) {
            // We used to create broken repro tarballs that only included those
            // object files from thin archives that ended up being used.
            if (config->warnThinArchiveMissingMembers)
              warn(toString(file) + ": -ObjC failed to open archive member: " +
                   toString(mb.takeError()));
            else
              llvm::consumeError(mb.takeError());
            continue;
          }

          if (!hasObjCSection(*mb))
            continue;
          if (Error e = file->fetch(c, "-ObjC"))
            error(toString(file) + ": -ObjC failed to load archive member: " +
                  toString(std::move(e)));
        }
        if (e)
          error(toString(file) +
                ": Archive::children failed: " + toString(std::move(e)));
      }
    }

    file->addLazySymbols();
    loadedArchives[path] = ArchiveFileInfo{file, isCommandLineLoad};
    newFile = file;
    break;
  }
  case file_magic::macho_object:
    newFile = make<ObjFile>(mbref, getModTime(path), "", isLazy);
    break;
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::tapi_file:
    if (DylibFile *dylibFile =
            loadDylib(mbref, nullptr, /*isBundleLoader=*/false, isExplicit))
      newFile = dylibFile;
    break;
  case file_magic::bitcode:
    newFile = make<BitcodeFile>(mbref, "", 0, isLazy);
    break;
  case file_magic::macho_executable:
  case file_magic::macho_bundle:
    // We only allow executable and bundle type here if it is used
    // as a bundle loader.
    if (!isBundleLoader)
      error(path + ": unhandled file type");
    if (DylibFile *dylibFile = loadDylib(mbref, nullptr, isBundleLoader))
      newFile = dylibFile;
    break;
  default:
    error(path + ": unhandled file type");
  }
  if (newFile && !isa<DylibFile>(newFile)) {
    if ((isa<ObjFile>(newFile) || isa<BitcodeFile>(newFile)) && newFile->lazy &&
        config->forceLoadObjC) {
      for (Symbol *sym : newFile->symbols)
        if (sym && sym->getName().starts_with(objc::symbol_names::klass)) {
          extract(*newFile, "-ObjC");
          break;
        }
      if (newFile->lazy && hasObjCSection(mbref))
        extract(*newFile, "-ObjC");
    }

    // printArchiveMemberLoad() prints both .a and .o names, so no need to
    // print the .a name here. Similarly skip lazy files.
    if (config->printEachFile && magic != file_magic::archive && !isLazy)
      message(toString(newFile));
    inputFiles.insert(newFile);
  }
  return newFile;
}

static std::vector<StringRef> missingAutolinkWarnings;
static void addLibrary(StringRef name, bool isNeeded, bool isWeak,
                       bool isReexport, bool isHidden, bool isExplicit,
                       LoadType loadType) {
  if (std::optional<StringRef> path = findLibrary(name)) {
    if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
            addFile(*path, loadType, /*isLazy=*/false, isExplicit,
                    /*isBundleLoader=*/false, isHidden))) {
      if (isNeeded)
        dylibFile->forceNeeded = true;
      if (isWeak)
        dylibFile->forceWeakImport = true;
      if (isReexport) {
        config->hasReexports = true;
        dylibFile->reexport = true;
      }
    }
    return;
  }
  if (loadType == LoadType::LCLinkerOption) {
    missingAutolinkWarnings.push_back(
        saver().save("auto-linked library not found for -l" + name));
    return;
  }
  error("library not found for -l" + name);
}

static DenseSet<StringRef> loadedObjectFrameworks;
static void addFramework(StringRef name, bool isNeeded, bool isWeak,
                         bool isReexport, bool isExplicit, LoadType loadType) {
  if (std::optional<StringRef> path = findFramework(name)) {
    if (loadedObjectFrameworks.contains(*path))
      return;

    InputFile *file =
        addFile(*path, loadType, /*isLazy=*/false, isExplicit, false);
    if (auto *dylibFile = dyn_cast_or_null<DylibFile>(file)) {
      if (isNeeded)
        dylibFile->forceNeeded = true;
      if (isWeak)
        dylibFile->forceWeakImport = true;
      if (isReexport) {
        config->hasReexports = true;
        dylibFile->reexport = true;
      }
    } else if (isa_and_nonnull<ObjFile>(file) ||
               isa_and_nonnull<BitcodeFile>(file)) {
      // Cache frameworks containing object or bitcode files to avoid duplicate
      // symbols. Frameworks containing static archives are cached separately
      // in addFile() to share caching with libraries, and frameworks
      // containing dylibs should allow overwriting of attributes such as
      // forceNeeded by subsequent loads
      loadedObjectFrameworks.insert(*path);
    }
    return;
  }
  if (loadType == LoadType::LCLinkerOption) {
    missingAutolinkWarnings.push_back(
        saver().save("auto-linked framework not found for -framework " + name));
    return;
  }
  error("framework not found for -framework " + name);
}

// Parses LC_LINKER_OPTION contents, which can add additional command line
// flags. This directly parses the flags instead of using the standard argument
// parser to improve performance.
void macho::parseLCLinkerOption(
    llvm::SmallVectorImpl<StringRef> &LCLinkerOptions, InputFile *f,
    unsigned argc, StringRef data) {
  if (config->ignoreAutoLink)
    return;

  SmallVector<StringRef, 4> argv;
  size_t offset = 0;
  for (unsigned i = 0; i < argc && offset < data.size(); ++i) {
    argv.push_back(data.data() + offset);
    offset += strlen(data.data() + offset) + 1;
  }
  if (argv.size() != argc || offset > data.size())
    fatal(toString(f) + ": invalid LC_LINKER_OPTION");

  unsigned i = 0;
  StringRef arg = argv[i];
  if (arg.consume_front("-l")) {
    if (config->ignoreAutoLinkOptions.contains(arg))
      return;
  } else if (arg == "-framework") {
    StringRef name = argv[++i];
    if (config->ignoreAutoLinkOptions.contains(name))
      return;
  } else {
    error(arg + " is not allowed in LC_LINKER_OPTION");
  }

  LCLinkerOptions.append(argv);
}

void macho::resolveLCLinkerOptions() {
  while (!unprocessedLCLinkerOptions.empty()) {
    SmallVector<StringRef> LCLinkerOptions(unprocessedLCLinkerOptions);
    unprocessedLCLinkerOptions.clear();

    for (unsigned i = 0; i < LCLinkerOptions.size(); ++i) {
      StringRef arg = LCLinkerOptions[i];
      if (arg.consume_front("-l")) {
        assert(!config->ignoreAutoLinkOptions.contains(arg));
        addLibrary(arg, /*isNeeded=*/false, /*isWeak=*/false,
                   /*isReexport=*/false, /*isHidden=*/false,
                   /*isExplicit=*/false, LoadType::LCLinkerOption);
      } else if (arg == "-framework") {
        StringRef name = LCLinkerOptions[++i];
        assert(!config->ignoreAutoLinkOptions.contains(name));
        addFramework(name, /*isNeeded=*/false, /*isWeak=*/false,
                     /*isReexport=*/false, /*isExplicit=*/false,
                     LoadType::LCLinkerOption);
      } else {
        error(arg + " is not allowed in LC_LINKER_OPTION");
      }
    }
  }
}

static void addFileList(StringRef path, bool isLazy) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;
  for (StringRef path : args::getLines(mbref))
    addFile(rerootPath(path), LoadType::CommandLine, isLazy);
}

// We expect sub-library names of the form "libfoo", which will match a dylib
// with a path of .*/libfoo.{dylib, tbd}.
// XXX ld64 seems to ignore the extension entirely when matching sub-libraries;
// I'm not sure what the use case for that is.
static bool markReexport(StringRef searchName, ArrayRef<StringRef> extensions) {
  for (InputFile *file : inputFiles) {
    if (auto *dylibFile = dyn_cast<DylibFile>(file)) {
      StringRef filename = path::filename(dylibFile->getName());
      if (filename.consume_front(searchName) &&
          (filename.empty() || llvm::is_contained(extensions, filename))) {
        dylibFile->reexport = true;
        return true;
      }
    }
  }
  return false;
}

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

static bool compileBitcodeFiles() {
  TimeTraceScope timeScope("LTO");
  auto *lto = make<BitcodeCompiler>();
  for (InputFile *file : inputFiles)
    if (auto *bitcodeFile = dyn_cast<BitcodeFile>(file))
      if (!file->lazy)
        lto->add(*bitcodeFile);

  std::vector<ObjFile *> compiled = lto->compile();
  for (ObjFile *file : compiled)
    inputFiles.insert(file);

  return !compiled.empty();
}

// Replaces common symbols with defined symbols residing in __common sections.
// This function must be called after all symbol names are resolved (i.e. after
// all InputFiles have been loaded.) As a result, later operations won't see
// any CommonSymbols.
static void replaceCommonSymbols() {
  TimeTraceScope timeScope("Replace common symbols");
  ConcatOutputSection *osec = nullptr;
  for (Symbol *sym : symtab->getSymbols()) {
    auto *common = dyn_cast<CommonSymbol>(sym);
    if (common == nullptr)
      continue;

    // Casting to size_t will truncate large values on 32-bit architectures,
    // but it's not really worth supporting the linking of 64-bit programs on
    // 32-bit archs.
    ArrayRef<uint8_t> data = {nullptr, static_cast<size_t>(common->size)};
    // FIXME avoid creating one Section per symbol?
    auto *section =
        make<Section>(common->getFile(), segment_names::data,
                      section_names::common, S_ZEROFILL, /*addr=*/0);
    auto *isec = make<ConcatInputSection>(*section, data, common->align);
    if (!osec)
      osec = ConcatOutputSection::getOrCreateForInput(isec);
    isec->parent = osec;
    addInputSection(isec);

    // FIXME: CommonSymbol should store isReferencedDynamically, noDeadStrip
    // and pass them on here.
    replaceSymbol<Defined>(
        sym, sym->getName(), common->getFile(), isec, /*value=*/0, common->size,
        /*isWeakDef=*/false, /*isExternal=*/true, common->privateExtern,
        /*includeInSymtab=*/true, /*isReferencedDynamically=*/false,
        /*noDeadStrip=*/false);
  }
}

static void initializeSectionRenameMap() {
  if (config->dataConst) {
    SmallVector<StringRef> v{section_names::got,
                             section_names::authGot,
                             section_names::authPtr,
                             section_names::nonLazySymbolPtr,
                             section_names::const_,
                             section_names::cfString,
                             section_names::moduleInitFunc,
                             section_names::moduleTermFunc,
                             section_names::objcClassList,
                             section_names::objcNonLazyClassList,
                             section_names::objcCatList,
                             section_names::objcNonLazyCatList,
                             section_names::objcProtoList,
                             section_names::objCImageInfo};
    for (StringRef s : v)
      config->sectionRenameMap[{segment_names::data, s}] = {
          segment_names::dataConst, s};
  }
  config->sectionRenameMap[{segment_names::text, section_names::staticInit}] = {
      segment_names::text, section_names::text};
  config->sectionRenameMap[{segment_names::import, section_names::pointers}] = {
      config->dataConst ? segment_names::dataConst : segment_names::data,
      section_names::nonLazySymbolPtr};
}

static inline char toLowerDash(char x) {
  if (x >= 'A' && x <= 'Z')
    return x - 'A' + 'a';
  else if (x == ' ')
    return '-';
  return x;
}

static std::string lowerDash(StringRef s) {
  return std::string(map_iterator(s.begin(), toLowerDash),
                     map_iterator(s.end(), toLowerDash));
}

struct PlatformVersion {
  PlatformType platform = PLATFORM_UNKNOWN;
  llvm::VersionTuple minimum;
  llvm::VersionTuple sdk;
};

static PlatformVersion parsePlatformVersion(const Arg *arg) {
  assert(arg->getOption().getID() == OPT_platform_version);
  StringRef platformStr = arg->getValue(0);
  StringRef minVersionStr = arg->getValue(1);
  StringRef sdkVersionStr = arg->getValue(2);

  PlatformVersion platformVersion;

  // TODO(compnerd) see if we can generate this case list via XMACROS
  platformVersion.platform =
      StringSwitch<PlatformType>(lowerDash(platformStr))
          .Cases("macos", "1", PLATFORM_MACOS)
          .Cases("ios", "2", PLATFORM_IOS)
          .Cases("tvos", "3", PLATFORM_TVOS)
          .Cases("watchos", "4", PLATFORM_WATCHOS)
          .Cases("bridgeos", "5", PLATFORM_BRIDGEOS)
          .Cases("mac-catalyst", "6", PLATFORM_MACCATALYST)
          .Cases("ios-simulator", "7", PLATFORM_IOSSIMULATOR)
          .Cases("tvos-simulator", "8", PLATFORM_TVOSSIMULATOR)
          .Cases("watchos-simulator", "9", PLATFORM_WATCHOSSIMULATOR)
          .Cases("driverkit", "10", PLATFORM_DRIVERKIT)
          .Cases("xros", "11", PLATFORM_XROS)
          .Cases("xros-simulator", "12", PLATFORM_XROS_SIMULATOR)
          .Default(PLATFORM_UNKNOWN);
  if (platformVersion.platform == PLATFORM_UNKNOWN)
    error(Twine("malformed platform: ") + platformStr);
  // TODO: check validity of version strings, which varies by platform
  // NOTE: ld64 accepts version strings with 5 components
  // llvm::VersionTuple accepts no more than 4 components
  // Has Apple ever published version strings with 5 components?
  if (platformVersion.minimum.tryParse(minVersionStr))
    error(Twine("malformed minimum version: ") + minVersionStr);
  if (platformVersion.sdk.tryParse(sdkVersionStr))
    error(Twine("malformed sdk version: ") + sdkVersionStr);
  return platformVersion;
}

// Has the side-effect of setting Config::platformInfo and
// potentially Config::secondaryPlatformInfo.
static void setPlatformVersions(StringRef archName, const ArgList &args) {
  std::map<PlatformType, PlatformVersion> platformVersions;
  const PlatformVersion *lastVersionInfo = nullptr;
  for (const Arg *arg : args.filtered(OPT_platform_version)) {
    PlatformVersion version = parsePlatformVersion(arg);

    // For each platform, the last flag wins:
    // `-platform_version macos 2 3 -platform_version macos 4 5` has the same
    // effect as just passing `-platform_version macos 4 5`.
    // FIXME: ld64 warns on multiple flags for one platform. Should we?
    platformVersions[version.platform] = version;
    lastVersionInfo = &platformVersions[version.platform];
  }

  if (platformVersions.empty()) {
    error("must specify -platform_version");
    return;
  }
  if (platformVersions.size() > 2) {
    error("must specify -platform_version at most twice");
    return;
  }
  if (platformVersions.size() == 2) {
    bool isZipperedCatalyst = platformVersions.count(PLATFORM_MACOS) &&
                              platformVersions.count(PLATFORM_MACCATALYST);

    if (!isZipperedCatalyst) {
      error("lld supports writing zippered outputs only for "
            "macos and mac-catalyst");
    } else if (config->outputType != MH_DYLIB &&
               config->outputType != MH_BUNDLE) {
      error("writing zippered outputs only valid for -dylib and -bundle");
    }

    config->platformInfo = {
        MachO::Target(getArchitectureFromName(archName), PLATFORM_MACOS,
                      platformVersions[PLATFORM_MACOS].minimum),
        platformVersions[PLATFORM_MACOS].sdk};
    config->secondaryPlatformInfo = {
        MachO::Target(getArchitectureFromName(archName), PLATFORM_MACCATALYST,
                      platformVersions[PLATFORM_MACCATALYST].minimum),
        platformVersions[PLATFORM_MACCATALYST].sdk};
    return;
  }

  config->platformInfo = {MachO::Target(getArchitectureFromName(archName),
                                        lastVersionInfo->platform,
                                        lastVersionInfo->minimum),
                          lastVersionInfo->sdk};
}

// Has the side-effect of setting Config::target.
static TargetInfo *createTargetInfo(InputArgList &args) {
  StringRef archName = args.getLastArgValue(OPT_arch);
  if (archName.empty()) {
    error("must specify -arch");
    return nullptr;
  }

  setPlatformVersions(archName, args);
  auto [cpuType, cpuSubtype] = getCPUTypeFromArchitecture(config->arch());
  switch (cpuType) {
  case CPU_TYPE_X86_64:
    return createX86_64TargetInfo();
  case CPU_TYPE_ARM64:
    return createARM64TargetInfo();
  case CPU_TYPE_ARM64_32:
    return createARM64_32TargetInfo();
  default:
    error("missing or unsupported -arch " + archName);
    return nullptr;
  }
}

static UndefinedSymbolTreatment
getUndefinedSymbolTreatment(const ArgList &args) {
  StringRef treatmentStr = args.getLastArgValue(OPT_undefined);
  auto treatment =
      StringSwitch<UndefinedSymbolTreatment>(treatmentStr)
          .Cases("error", "", UndefinedSymbolTreatment::error)
          .Case("warning", UndefinedSymbolTreatment::warning)
          .Case("suppress", UndefinedSymbolTreatment::suppress)
          .Case("dynamic_lookup", UndefinedSymbolTreatment::dynamic_lookup)
          .Default(UndefinedSymbolTreatment::unknown);
  if (treatment == UndefinedSymbolTreatment::unknown) {
    warn(Twine("unknown -undefined TREATMENT '") + treatmentStr +
         "', defaulting to 'error'");
    treatment = UndefinedSymbolTreatment::error;
  } else if (config->namespaceKind == NamespaceKind::twolevel &&
             (treatment == UndefinedSymbolTreatment::warning ||
              treatment == UndefinedSymbolTreatment::suppress)) {
    if (treatment == UndefinedSymbolTreatment::warning)
      fatal("'-undefined warning' only valid with '-flat_namespace'");
    else
      fatal("'-undefined suppress' only valid with '-flat_namespace'");
    treatment = UndefinedSymbolTreatment::error;
  }
  return treatment;
}

static ICFLevel getICFLevel(const ArgList &args) {
  StringRef icfLevelStr = args.getLastArgValue(OPT_icf_eq);
  auto icfLevel = StringSwitch<ICFLevel>(icfLevelStr)
                      .Cases("none", "", ICFLevel::none)
                      .Case("safe", ICFLevel::safe)
                      .Case("all", ICFLevel::all)
                      .Default(ICFLevel::unknown);
  if (icfLevel == ICFLevel::unknown) {
    warn(Twine("unknown --icf=OPTION `") + icfLevelStr +
         "', defaulting to `none'");
    icfLevel = ICFLevel::none;
  }
  return icfLevel;
}

static ObjCStubsMode getObjCStubsMode(const ArgList &args) {
  const Arg *arg = args.getLastArg(OPT_objc_stubs_fast, OPT_objc_stubs_small);
  if (!arg)
    return ObjCStubsMode::fast;

  if (arg->getOption().getID() == OPT_objc_stubs_small) {
    if (is_contained({AK_arm64e, AK_arm64}, config->arch()))
      return ObjCStubsMode::small;
    else
      warn("-objc_stubs_small is not yet implemented, defaulting to "
           "-objc_stubs_fast");
  }
  return ObjCStubsMode::fast;
}

static void warnIfDeprecatedOption(const Option &opt) {
  if (!opt.getGroup().isValid())
    return;
  if (opt.getGroup().getID() == OPT_grp_deprecated) {
    warn("Option `" + opt.getPrefixedName() + "' is deprecated in ld64:");
    warn(opt.getHelpText());
  }
}

static void warnIfUnimplementedOption(const Option &opt) {
  if (!opt.getGroup().isValid() || !opt.hasFlag(DriverFlag::HelpHidden))
    return;
  switch (opt.getGroup().getID()) {
  case OPT_grp_deprecated:
    // warn about deprecated options elsewhere
    break;
  case OPT_grp_undocumented:
    warn("Option `" + opt.getPrefixedName() +
         "' is undocumented. Should lld implement it?");
    break;
  case OPT_grp_obsolete:
    warn("Option `" + opt.getPrefixedName() +
         "' is obsolete. Please modernize your usage.");
    break;
  case OPT_grp_ignored:
    warn("Option `" + opt.getPrefixedName() + "' is ignored.");
    break;
  case OPT_grp_ignored_silently:
    break;
  default:
    warn("Option `" + opt.getPrefixedName() +
         "' is not yet implemented. Stay tuned...");
    break;
  }
}

static const char *getReproduceOption(InputArgList &args) {
  if (const Arg *arg = args.getLastArg(OPT_reproduce))
    return arg->getValue();
  return getenv("LLD_REPRODUCE");
}

// Parse options of the form "old;new".
static std::pair<StringRef, StringRef> getOldNewOptions(opt::InputArgList &args,
                                                        unsigned id) {
  auto *arg = args.getLastArg(id);
  if (!arg)
    return {"", ""};

  StringRef s = arg->getValue();
  std::pair<StringRef, StringRef> ret = s.split(';');
  if (ret.second.empty())
    error(arg->getSpelling() + " expects 'old;new' format, but got " + s);
  return ret;
}

// Parse options of the form "old;new[;extra]".
static std::tuple<StringRef, StringRef, StringRef>
getOldNewOptionsExtra(opt::InputArgList &args, unsigned id) {
  auto [oldDir, second] = getOldNewOptions(args, id);
  auto [newDir, extraDir] = second.split(';');
  return {oldDir, newDir, extraDir};
}

static void parseClangOption(StringRef opt, const Twine &msg) {
  std::string err;
  raw_string_ostream os(err);

  const char *argv[] = {"lld", opt.data()};
  if (cl::ParseCommandLineOptions(2, argv, "", &os))
    return;
  os.flush();
  error(msg + ": " + StringRef(err).trim());
}

static uint32_t parseDylibVersion(const ArgList &args, unsigned id) {
  const Arg *arg = args.getLastArg(id);
  if (!arg)
    return 0;

  if (config->outputType != MH_DYLIB) {
    error(arg->getAsString(args) + ": only valid with -dylib");
    return 0;
  }

  PackedVersion version;
  if (!version.parse32(arg->getValue())) {
    error(arg->getAsString(args) + ": malformed version");
    return 0;
  }

  return version.rawValue();
}

static uint32_t parseProtection(StringRef protStr) {
  uint32_t prot = 0;
  for (char c : protStr) {
    switch (c) {
    case 'r':
      prot |= VM_PROT_READ;
      break;
    case 'w':
      prot |= VM_PROT_WRITE;
      break;
    case 'x':
      prot |= VM_PROT_EXECUTE;
      break;
    case '-':
      break;
    default:
      error("unknown -segprot letter '" + Twine(c) + "' in " + protStr);
      return 0;
    }
  }
  return prot;
}

static std::vector<SectionAlign> parseSectAlign(const opt::InputArgList &args) {
  std::vector<SectionAlign> sectAligns;
  for (const Arg *arg : args.filtered(OPT_sectalign)) {
    StringRef segName = arg->getValue(0);
    StringRef sectName = arg->getValue(1);
    StringRef alignStr = arg->getValue(2);
    alignStr.consume_front_insensitive("0x");
    uint32_t align;
    if (alignStr.getAsInteger(16, align)) {
      error("-sectalign: failed to parse '" + StringRef(arg->getValue(2)) +
            "' as number");
      continue;
    }
    if (!isPowerOf2_32(align)) {
      error("-sectalign: '" + StringRef(arg->getValue(2)) +
            "' (in base 16) not a power of two");
      continue;
    }
    sectAligns.push_back({segName, sectName, align});
  }
  return sectAligns;
}

PlatformType macho::removeSimulator(PlatformType platform) {
  switch (platform) {
  case PLATFORM_IOSSIMULATOR:
    return PLATFORM_IOS;
  case PLATFORM_TVOSSIMULATOR:
    return PLATFORM_TVOS;
  case PLATFORM_WATCHOSSIMULATOR:
    return PLATFORM_WATCHOS;
  case PLATFORM_XROS_SIMULATOR:
    return PLATFORM_XROS;
  default:
    return platform;
  }
}

static bool supportsNoPie() {
  return !(config->arch() == AK_arm64 || config->arch() == AK_arm64e ||
           config->arch() == AK_arm64_32);
}

static bool shouldAdhocSignByDefault(Architecture arch, PlatformType platform) {
  if (arch != AK_arm64 && arch != AK_arm64e)
    return false;

  return platform == PLATFORM_MACOS || platform == PLATFORM_IOSSIMULATOR ||
         platform == PLATFORM_TVOSSIMULATOR ||
         platform == PLATFORM_WATCHOSSIMULATOR ||
         platform == PLATFORM_XROS_SIMULATOR;
}

static bool dataConstDefault(const InputArgList &args) {
  static const std::array<std::pair<PlatformType, VersionTuple>, 6> minVersion =
      {{{PLATFORM_MACOS, VersionTuple(10, 15)},
        {PLATFORM_IOS, VersionTuple(13, 0)},
        {PLATFORM_TVOS, VersionTuple(13, 0)},
        {PLATFORM_WATCHOS, VersionTuple(6, 0)},
        {PLATFORM_XROS, VersionTuple(1, 0)},
        {PLATFORM_BRIDGEOS, VersionTuple(4, 0)}}};
  PlatformType platform = removeSimulator(config->platformInfo.target.Platform);
  auto it = llvm::find_if(minVersion,
                          [&](const auto &p) { return p.first == platform; });
  if (it != minVersion.end())
    if (config->platformInfo.target.MinDeployment < it->second)
      return false;

  switch (config->outputType) {
  case MH_EXECUTE:
    return !(args.hasArg(OPT_no_pie) && supportsNoPie());
  case MH_BUNDLE:
    // FIXME: return false when -final_name ...
    // has prefix "/System/Library/UserEventPlugins/"
    // or matches "/usr/libexec/locationd" "/usr/libexec/terminusd"
    return true;
  case MH_DYLIB:
    return true;
  case MH_OBJECT:
    return false;
  default:
    llvm_unreachable(
        "unsupported output type for determining data-const default");
  }
  return false;
}

static bool shouldEmitChainedFixups(const InputArgList &args) {
  const Arg *arg = args.getLastArg(OPT_fixup_chains, OPT_no_fixup_chains);
  if (arg && arg->getOption().matches(OPT_no_fixup_chains))
    return false;

  bool requested = arg && arg->getOption().matches(OPT_fixup_chains);
  if (!config->isPic) {
    if (requested)
      error("-fixup_chains is incompatible with -no_pie");

    return false;
  }

  if (!is_contained({AK_x86_64, AK_x86_64h, AK_arm64}, config->arch())) {
    if (requested)
      error("-fixup_chains is only supported on x86_64 and arm64 targets");

    return false;
  }

  if (args.hasArg(OPT_preload)) {
    if (requested)
      error("-fixup_chains is incompatible with -preload");

    return false;
  }

  if (requested)
    return true;

  static const std::array<std::pair<PlatformType, VersionTuple>, 9> minVersion =
      {{
          {PLATFORM_IOS, VersionTuple(13, 4)},
          {PLATFORM_IOSSIMULATOR, VersionTuple(16, 0)},
          {PLATFORM_MACOS, VersionTuple(13, 0)},
          {PLATFORM_TVOS, VersionTuple(14, 0)},
          {PLATFORM_TVOSSIMULATOR, VersionTuple(15, 0)},
          {PLATFORM_WATCHOS, VersionTuple(7, 0)},
          {PLATFORM_WATCHOSSIMULATOR, VersionTuple(8, 0)},
          {PLATFORM_XROS, VersionTuple(1, 0)},
          {PLATFORM_XROS_SIMULATOR, VersionTuple(1, 0)},
      }};
  PlatformType platform = config->platformInfo.target.Platform;
  auto it = llvm::find_if(minVersion,
                          [&](const auto &p) { return p.first == platform; });

  // We don't know the versions for other platforms, so default to disabled.
  if (it == minVersion.end())
    return false;

  if (it->second > config->platformInfo.target.MinDeployment)
    return false;

  return true;
}

static bool shouldEmitRelativeMethodLists(const InputArgList &args) {
  const Arg *arg = args.getLastArg(OPT_objc_relative_method_lists,
                                   OPT_no_objc_relative_method_lists);
  if (arg && arg->getOption().getID() == OPT_objc_relative_method_lists)
    return true;
  if (arg && arg->getOption().getID() == OPT_no_objc_relative_method_lists)
    return false;

  // TODO: If no flag is specified, don't default to false, but instead:
  //   - default false on   <   ios14
  //   - default true  on   >=  ios14
  // For now, until this feature is confirmed stable, default to false if no
  // flag is explicitly specified
  return false;
}

void SymbolPatterns::clear() {
  literals.clear();
  globs.clear();
}

void SymbolPatterns::insert(StringRef symbolName) {
  if (symbolName.find_first_of("*?[]") == StringRef::npos)
    literals.insert(CachedHashStringRef(symbolName));
  else if (Expected<GlobPattern> pattern = GlobPattern::create(symbolName))
    globs.emplace_back(*pattern);
  else
    error("invalid symbol-name pattern: " + symbolName);
}

bool SymbolPatterns::matchLiteral(StringRef symbolName) const {
  return literals.contains(CachedHashStringRef(symbolName));
}

bool SymbolPatterns::matchGlob(StringRef symbolName) const {
  for (const GlobPattern &glob : globs)
    if (glob.match(symbolName))
      return true;
  return false;
}

bool SymbolPatterns::match(StringRef symbolName) const {
  return matchLiteral(symbolName) || matchGlob(symbolName);
}

static void parseSymbolPatternsFile(const Arg *arg,
                                    SymbolPatterns &symbolPatterns) {
  StringRef path = arg->getValue();
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer) {
    error("Could not read symbol file: " + path);
    return;
  }
  MemoryBufferRef mbref = *buffer;
  for (StringRef line : args::getLines(mbref)) {
    line = line.take_until([](char c) { return c == '#'; }).trim();
    if (!line.empty())
      symbolPatterns.insert(line);
  }
}

static void handleSymbolPatterns(InputArgList &args,
                                 SymbolPatterns &symbolPatterns,
                                 unsigned singleOptionCode,
                                 unsigned listFileOptionCode) {
  for (const Arg *arg : args.filtered(singleOptionCode))
    symbolPatterns.insert(arg->getValue());
  for (const Arg *arg : args.filtered(listFileOptionCode))
    parseSymbolPatternsFile(arg, symbolPatterns);
}

static void createFiles(const InputArgList &args) {
  TimeTraceScope timeScope("Load input files");
  // This loop should be reserved for options whose exact ordering matters.
  // Other options should be handled via filtered() and/or getLastArg().
  bool isLazy = false;
  // If we've processed an opening --start-lib, without a matching --end-lib
  bool inLib = false;
  for (const Arg *arg : args) {
    const Option &opt = arg->getOption();
    warnIfDeprecatedOption(opt);
    warnIfUnimplementedOption(opt);

    switch (opt.getID()) {
    case OPT_INPUT:
      addFile(rerootPath(arg->getValue()), LoadType::CommandLine, isLazy);
      break;
    case OPT_needed_library:
      if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
              addFile(rerootPath(arg->getValue()), LoadType::CommandLine)))
        dylibFile->forceNeeded = true;
      break;
    case OPT_reexport_library:
      if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
              addFile(rerootPath(arg->getValue()), LoadType::CommandLine))) {
        config->hasReexports = true;
        dylibFile->reexport = true;
      }
      break;
    case OPT_weak_library:
      if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
              addFile(rerootPath(arg->getValue()), LoadType::CommandLine)))
        dylibFile->forceWeakImport = true;
      break;
    case OPT_filelist:
      addFileList(arg->getValue(), isLazy);
      break;
    case OPT_force_load:
      addFile(rerootPath(arg->getValue()), LoadType::CommandLineForce);
      break;
    case OPT_load_hidden:
      addFile(rerootPath(arg->getValue()), LoadType::CommandLine,
              /*isLazy=*/false, /*isExplicit=*/true, /*isBundleLoader=*/false,
              /*isForceHidden=*/true);
      break;
    case OPT_l:
    case OPT_needed_l:
    case OPT_reexport_l:
    case OPT_weak_l:
    case OPT_hidden_l:
      addLibrary(arg->getValue(), opt.getID() == OPT_needed_l,
                 opt.getID() == OPT_weak_l, opt.getID() == OPT_reexport_l,
                 opt.getID() == OPT_hidden_l,
                 /*isExplicit=*/true, LoadType::CommandLine);
      break;
    case OPT_framework:
    case OPT_needed_framework:
    case OPT_reexport_framework:
    case OPT_weak_framework:
      addFramework(arg->getValue(), opt.getID() == OPT_needed_framework,
                   opt.getID() == OPT_weak_framework,
                   opt.getID() == OPT_reexport_framework, /*isExplicit=*/true,
                   LoadType::CommandLine);
      break;
    case OPT_start_lib:
      if (inLib)
        error("nested --start-lib");
      inLib = true;
      if (!config->allLoad)
        isLazy = true;
      break;
    case OPT_end_lib:
      if (!inLib)
        error("stray --end-lib");
      inLib = false;
      isLazy = false;
      break;
    default:
      break;
    }
  }
}

static void gatherInputSections() {
  TimeTraceScope timeScope("Gathering input sections");
  for (const InputFile *file : inputFiles) {
    for (const Section *section : file->sections) {
      // Compact unwind entries require special handling elsewhere. (In
      // contrast, EH frames are handled like regular ConcatInputSections.)
      if (section->name == section_names::compactUnwind)
        continue;
      // Addrsig sections contain metadata only needed at link time.
      if (section->name == section_names::addrSig)
        continue;
      for (const Subsection &subsection : section->subsections)
        addInputSection(subsection.isec);
    }
    if (!file->objCImageInfo.empty())
      in.objCImageInfo->addFile(file);
  }
}

static void foldIdenticalLiterals() {
  TimeTraceScope timeScope("Fold identical literals");
  // We always create a cStringSection, regardless of whether dedupLiterals is
  // true. If it isn't, we simply create a non-deduplicating CStringSection.
  // Either way, we must unconditionally finalize it here.
  in.cStringSection->finalizeContents();
  in.objcMethnameSection->finalizeContents();
  in.wordLiteralSection->finalizeContents();
}

static void addSynthenticMethnames() {
  std::string &data = *make<std::string>();
  llvm::raw_string_ostream os(data);
  for (Symbol *sym : symtab->getSymbols())
    if (isa<Undefined>(sym))
      if (ObjCStubsSection::isObjCStubSymbol(sym))
        os << ObjCStubsSection::getMethname(sym) << '\0';

  if (data.empty())
    return;

  const auto *buf = reinterpret_cast<const uint8_t *>(data.c_str());
  Section &section = *make<Section>(/*file=*/nullptr, segment_names::text,
                                    section_names::objcMethname,
                                    S_CSTRING_LITERALS, /*addr=*/0);

  auto *isec =
      make<CStringInputSection>(section, ArrayRef<uint8_t>{buf, data.size()},
                                /*align=*/1, /*dedupLiterals=*/true);
  isec->splitIntoPieces();
  for (auto &piece : isec->pieces)
    piece.live = true;
  section.subsections.push_back({0, isec});
  in.objcMethnameSection->addInput(isec);
  in.objcMethnameSection->isec->markLive(0);
}

static void referenceStubBinder() {
  bool needsStubHelper = config->outputType == MH_DYLIB ||
                         config->outputType == MH_EXECUTE ||
                         config->outputType == MH_BUNDLE;
  if (!needsStubHelper || !symtab->find("dyld_stub_binder"))
    return;

  // dyld_stub_binder is used by dyld to resolve lazy bindings. This code here
  // adds a opportunistic reference to dyld_stub_binder if it happens to exist.
  // dyld_stub_binder is in libSystem.dylib, which is usually linked in. This
  // isn't needed for correctness, but the presence of that symbol suppresses
  // "no symbols" diagnostics from `nm`.
  // StubHelperSection::setUp() adds a reference and errors out if
  // dyld_stub_binder doesn't exist in case it is actually needed.
  symtab->addUndefined("dyld_stub_binder", /*file=*/nullptr, /*isWeak=*/false);
}

static void createAliases() {
  for (const auto &pair : config->aliasedSymbols) {
    if (const auto &sym = symtab->find(pair.first)) {
      if (const auto &defined = dyn_cast<Defined>(sym)) {
        symtab->aliasDefined(defined, pair.second, defined->getFile())
            ->noDeadStrip = true;
      } else {
        error("TODO: support aliasing to symbols of kind " +
              Twine(sym->kind()));
      }
    } else {
      warn("undefined base symbol '" + pair.first + "' for alias '" +
           pair.second + "'\n");
    }
  }

  for (const InputFile *file : inputFiles) {
    if (auto *objFile = dyn_cast<ObjFile>(file)) {
      for (const AliasSymbol *alias : objFile->aliases) {
        if (const auto &aliased = symtab->find(alias->getAliasedName())) {
          if (const auto &defined = dyn_cast<Defined>(aliased)) {
            symtab->aliasDefined(defined, alias->getName(), alias->getFile(),
                                 alias->privateExtern);
          } else {
            // Common, dylib, and undefined symbols are all valid alias
            // referents (undefineds can become valid Defined symbols later on
            // in the link.)
            error("TODO: support aliasing to symbols of kind " +
                  Twine(aliased->kind()));
          }
        } else {
          // This shouldn't happen since MC generates undefined symbols to
          // represent the alias referents. Thus we fatal() instead of just
          // warning here.
          fatal("unable to find alias referent " + alias->getAliasedName() +
                " for " + alias->getName());
        }
      }
    }
  }
}

static void handleExplicitExports() {
  static constexpr int kMaxWarnings = 3;
  if (config->hasExplicitExports) {
    std::atomic<uint64_t> warningsCount{0};
    parallelForEach(symtab->getSymbols(), [&warningsCount](Symbol *sym) {
      if (auto *defined = dyn_cast<Defined>(sym)) {
        if (config->exportedSymbols.match(sym->getName())) {
          if (defined->privateExtern) {
            if (defined->weakDefCanBeHidden) {
              // weak_def_can_be_hidden symbols behave similarly to
              // private_extern symbols in most cases, except for when
              // it is explicitly exported.
              // The former can be exported but the latter cannot.
              defined->privateExtern = false;
            } else {
              // Only print the first 3 warnings verbosely, and
              // shorten the rest to avoid crowding logs.
              if (warningsCount.fetch_add(1, std::memory_order_relaxed) <
                  kMaxWarnings)
                warn("cannot export hidden symbol " + toString(*defined) +
                     "\n>>> defined in " + toString(defined->getFile()));
            }
          }
        } else {
          defined->privateExtern = true;
        }
      } else if (auto *dysym = dyn_cast<DylibSymbol>(sym)) {
        dysym->shouldReexport = config->exportedSymbols.match(sym->getName());
      }
    });
    if (warningsCount > kMaxWarnings)
      warn("<... " + Twine(warningsCount - kMaxWarnings) +
           " more similar warnings...>");
  } else if (!config->unexportedSymbols.empty()) {
    parallelForEach(symtab->getSymbols(), [](Symbol *sym) {
      if (auto *defined = dyn_cast<Defined>(sym))
        if (config->unexportedSymbols.match(defined->getName()))
          defined->privateExtern = true;
    });
  }
}

static void eraseInitializerSymbols() {
  for (ConcatInputSection *isec : in.initOffsets->inputs())
    for (Defined *sym : isec->symbols)
      sym->used = false;
}

static SmallVector<StringRef, 0> getRuntimePaths(opt::InputArgList &args) {
  SmallVector<StringRef, 0> vals;
  DenseSet<StringRef> seen;
  for (const Arg *arg : args.filtered(OPT_rpath)) {
    StringRef val = arg->getValue();
    if (seen.insert(val).second)
      vals.push_back(val);
    else if (config->warnDuplicateRpath)
      warn("duplicate -rpath '" + val + "' ignored [--warn-duplicate-rpath]");
  }
  return vals;
}

namespace lld {
namespace macho {
bool link(ArrayRef<const char *> argsArr, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  // This driver-specific context will be freed later by lldMain().
  auto *ctx = new CommonLinkerContext;

  ctx->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  ctx->e.cleanupCallback = []() {
    resolvedFrameworks.clear();
    resolvedLibraries.clear();
    cachedReads.clear();
    concatOutputSections.clear();
    inputFiles.clear();
    inputSections.clear();
    inputSectionsOrder = 0;
    loadedArchives.clear();
    loadedObjectFrameworks.clear();
    missingAutolinkWarnings.clear();
    syntheticSections.clear();
    thunkMap.clear();
    unprocessedLCLinkerOptions.clear();
    ObjCSelRefsHelper::cleanup();

    firstTLVDataSection = nullptr;
    tar = nullptr;
    memset(&in, 0, sizeof(in));

    resetLoadedDylibs();
    resetOutputSegments();
    resetWriter();
    InputFile::resetIdCount();

    objc::doCleanup();
  };

  ctx->e.logName = args::getFilenameWithoutExe(argsArr[0]);

  MachOOptTable parser;
  InputArgList args = parser.parse(argsArr.slice(1));

  ctx->e.errorLimitExceededMsg = "too many errors emitted, stopping now "
                                 "(use --error-limit=0 to see all errors)";
  ctx->e.errorLimit = args::getInteger(args, OPT_error_limit_eq, 20);
  ctx->e.verbose = args.hasArg(OPT_verbose);

  if (args.hasArg(OPT_help_hidden)) {
    parser.printHelp(argsArr[0], /*showHidden=*/true);
    return true;
  }
  if (args.hasArg(OPT_help)) {
    parser.printHelp(argsArr[0], /*showHidden=*/false);
    return true;
  }
  if (args.hasArg(OPT_version)) {
    message(getLLDVersion());
    return true;
  }

  config = std::make_unique<Configuration>();
  symtab = std::make_unique<SymbolTable>();
  config->outputType = getOutputType(args);
  target = createTargetInfo(args);
  depTracker = std::make_unique<DependencyTracker>(
      args.getLastArgValue(OPT_dependency_info));

  config->ltoo = args::getInteger(args, OPT_lto_O, 2);
  if (config->ltoo > 3)
    error("--lto-O: invalid optimization level: " + Twine(config->ltoo));
  unsigned ltoCgo =
      args::getInteger(args, OPT_lto_CGO, args::getCGOptLevel(config->ltoo));
  if (auto level = CodeGenOpt::getLevel(ltoCgo))
    config->ltoCgo = *level;
  else
    error("--lto-CGO: invalid codegen optimization level: " + Twine(ltoCgo));

  if (errorCount())
    return false;

  if (args.hasArg(OPT_pagezero_size)) {
    uint64_t pagezeroSize = args::getHex(args, OPT_pagezero_size, 0);

    // ld64 does something really weird. It attempts to realign the value to the
    // page size, but assumes the page size is 4K. This doesn't work with most
    // of Apple's ARM64 devices, which use a page size of 16K. This means that
    // it will first 4K align it by rounding down, then round up to 16K.  This
    // probably only happened because no one using this arg with anything other
    // then 0, so no one checked if it did what is what it says it does.

    // So we are not copying this weird behavior and doing the it in a logical
    // way, by always rounding down to page size.
    if (!isAligned(Align(target->getPageSize()), pagezeroSize)) {
      pagezeroSize -= pagezeroSize % target->getPageSize();
      warn("__PAGEZERO size is not page aligned, rounding down to 0x" +
           Twine::utohexstr(pagezeroSize));
    }

    target->pageZeroSize = pagezeroSize;
  }

  config->osoPrefix = args.getLastArgValue(OPT_oso_prefix);
  if (!config->osoPrefix.empty()) {
    // Expand special characters, such as ".", "..", or  "~", if present.
    // Note: LD64 only expands "." and not other special characters.
    // That seems silly to imitate so we will not try to follow it, but rather
    // just use real_path() to do it.

    // The max path length is 4096, in theory. However that seems quite long
    // and seems unlikely that any one would want to strip everything from the
    // path. Hence we've picked a reasonably large number here.
    SmallString<1024> expanded;
    if (!fs::real_path(config->osoPrefix, expanded,
                       /*expand_tilde=*/true)) {
      // Note: LD64 expands "." to be `<current_dir>/`
      // (ie., it has a slash suffix) whereas real_path() doesn't.
      // So we have to append '/' to be consistent.
      StringRef sep = sys::path::get_separator();
      // real_path removes trailing slashes as part of the normalization, but
      // these are meaningful for our text based stripping
      if (config->osoPrefix == "." || config->osoPrefix.ends_with(sep))
        expanded += sep;
      config->osoPrefix = saver().save(expanded.str());
    }
  }

  bool pie = args.hasFlag(OPT_pie, OPT_no_pie, true);
  if (!supportsNoPie() && !pie) {
    warn("-no_pie ignored for arm64");
    pie = true;
  }

  config->isPic = config->outputType == MH_DYLIB ||
                  config->outputType == MH_BUNDLE ||
                  (config->outputType == MH_EXECUTE && pie);

  // Must be set before any InputSections and Symbols are created.
  config->deadStrip = args.hasArg(OPT_dead_strip);

  config->systemLibraryRoots = getSystemLibraryRoots(args);
  if (const char *path = getReproduceOption(args)) {
    // Note that --reproduce is a debug option so you can ignore it
    // if you are trying to understand the whole picture of the code.
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

  if (auto *arg = args.getLastArg(OPT_threads_eq)) {
    StringRef v(arg->getValue());
    unsigned threads = 0;
    if (!llvm::to_integer(v, threads, 0) || threads == 0)
      error(arg->getSpelling() + ": expected a positive integer, but got '" +
            arg->getValue() + "'");
    parallel::strategy = hardware_concurrency(threads);
    config->thinLTOJobs = v;
  }
  if (auto *arg = args.getLastArg(OPT_thinlto_jobs_eq))
    config->thinLTOJobs = arg->getValue();
  if (!get_threadpool_strategy(config->thinLTOJobs))
    error("--thinlto-jobs: invalid job count: " + config->thinLTOJobs);

  for (const Arg *arg : args.filtered(OPT_u)) {
    config->explicitUndefineds.push_back(symtab->addUndefined(
        arg->getValue(), /*file=*/nullptr, /*isWeakRef=*/false));
  }

  for (const Arg *arg : args.filtered(OPT_U))
    config->explicitDynamicLookups.insert(arg->getValue());

  config->mapFile = args.getLastArgValue(OPT_map);
  config->optimize = args::getInteger(args, OPT_O, 1);
  config->outputFile = args.getLastArgValue(OPT_o, "a.out");
  config->finalOutput =
      args.getLastArgValue(OPT_final_output, config->outputFile);
  config->astPaths = args.getAllArgValues(OPT_add_ast_path);
  config->headerPad = args::getHex(args, OPT_headerpad, /*Default=*/32);
  config->headerPadMaxInstallNames =
      args.hasArg(OPT_headerpad_max_install_names);
  config->printDylibSearch =
      args.hasArg(OPT_print_dylib_search) || getenv("RC_TRACE_DYLIB_SEARCHING");
  config->printEachFile = args.hasArg(OPT_t);
  config->printWhyLoad = args.hasArg(OPT_why_load);
  config->omitDebugInfo = args.hasArg(OPT_S);
  config->errorForArchMismatch = args.hasArg(OPT_arch_errors_fatal);
  if (const Arg *arg = args.getLastArg(OPT_bundle_loader)) {
    if (config->outputType != MH_BUNDLE)
      error("-bundle_loader can only be used with MachO bundle output");
    addFile(arg->getValue(), LoadType::CommandLine, /*isLazy=*/false,
            /*isExplicit=*/false, /*isBundleLoader=*/true);
  }
  for (auto *arg : args.filtered(OPT_dyld_env)) {
    StringRef envPair(arg->getValue());
    if (!envPair.contains('='))
      error("-dyld_env's argument is  malformed. Expected "
            "-dyld_env <ENV_VAR>=<VALUE>, got `" +
            envPair + "`");
    config->dyldEnvs.push_back(envPair);
  }
  if (!config->dyldEnvs.empty() && config->outputType != MH_EXECUTE)
    error("-dyld_env can only be used when creating executable output");

  if (const Arg *arg = args.getLastArg(OPT_umbrella)) {
    if (config->outputType != MH_DYLIB)
      warn("-umbrella used, but not creating dylib");
    config->umbrella = arg->getValue();
  }
  config->ltoObjPath = args.getLastArgValue(OPT_object_path_lto);
  config->thinLTOCacheDir = args.getLastArgValue(OPT_cache_path_lto);
  config->thinLTOCachePolicy = getLTOCachePolicy(args);
  config->thinLTOEmitImportsFiles = args.hasArg(OPT_thinlto_emit_imports_files);
  config->thinLTOEmitIndexFiles = args.hasArg(OPT_thinlto_emit_index_files) ||
                                  args.hasArg(OPT_thinlto_index_only) ||
                                  args.hasArg(OPT_thinlto_index_only_eq);
  config->thinLTOIndexOnly = args.hasArg(OPT_thinlto_index_only) ||
                             args.hasArg(OPT_thinlto_index_only_eq);
  config->thinLTOIndexOnlyArg = args.getLastArgValue(OPT_thinlto_index_only_eq);
  config->thinLTOObjectSuffixReplace =
      getOldNewOptions(args, OPT_thinlto_object_suffix_replace_eq);
  std::tie(config->thinLTOPrefixReplaceOld, config->thinLTOPrefixReplaceNew,
           config->thinLTOPrefixReplaceNativeObject) =
      getOldNewOptionsExtra(args, OPT_thinlto_prefix_replace_eq);
  if (config->thinLTOEmitIndexFiles && !config->thinLTOIndexOnly) {
    if (args.hasArg(OPT_thinlto_object_suffix_replace_eq))
      error("--thinlto-object-suffix-replace is not supported with "
            "--thinlto-emit-index-files");
    else if (args.hasArg(OPT_thinlto_prefix_replace_eq))
      error("--thinlto-prefix-replace is not supported with "
            "--thinlto-emit-index-files");
  }
  if (!config->thinLTOPrefixReplaceNativeObject.empty() &&
      config->thinLTOIndexOnlyArg.empty()) {
    error("--thinlto-prefix-replace=old_dir;new_dir;obj_dir must be used with "
          "--thinlto-index-only=");
  }
  config->warnDuplicateRpath =
      args.hasFlag(OPT_warn_duplicate_rpath, OPT_no_warn_duplicate_rpath, true);
  config->runtimePaths = getRuntimePaths(args);
  config->allLoad = args.hasFlag(OPT_all_load, OPT_noall_load, false);
  config->archMultiple = args.hasArg(OPT_arch_multiple);
  config->applicationExtension = args.hasFlag(
      OPT_application_extension, OPT_no_application_extension, false);
  config->exportDynamic = args.hasArg(OPT_export_dynamic);
  config->forceLoadObjC = args.hasArg(OPT_ObjC);
  config->forceLoadSwift = args.hasArg(OPT_force_load_swift_libs);
  config->deadStripDylibs = args.hasArg(OPT_dead_strip_dylibs);
  config->demangle = args.hasArg(OPT_demangle);
  config->implicitDylibs = !args.hasArg(OPT_no_implicit_dylibs);
  config->emitFunctionStarts =
      args.hasFlag(OPT_function_starts, OPT_no_function_starts, true);
  config->emitDataInCodeInfo =
      args.hasFlag(OPT_data_in_code_info, OPT_no_data_in_code_info, true);
  config->emitChainedFixups = shouldEmitChainedFixups(args);
  config->emitInitOffsets =
      config->emitChainedFixups || args.hasArg(OPT_init_offsets);
  config->emitRelativeMethodLists = shouldEmitRelativeMethodLists(args);
  config->icfLevel = getICFLevel(args);
  config->keepICFStabs = args.hasArg(OPT_keep_icf_stabs);
  config->dedupStrings =
      args.hasFlag(OPT_deduplicate_strings, OPT_no_deduplicate_strings, true);
  config->deadStripDuplicates = args.hasArg(OPT_dead_strip_duplicates);
  config->warnDylibInstallName = args.hasFlag(
      OPT_warn_dylib_install_name, OPT_no_warn_dylib_install_name, false);
  config->ignoreOptimizationHints = args.hasArg(OPT_ignore_optimization_hints);
  config->callGraphProfileSort = args.hasFlag(
      OPT_call_graph_profile_sort, OPT_no_call_graph_profile_sort, true);
  config->printSymbolOrder = args.getLastArgValue(OPT_print_symbol_order_eq);
  config->forceExactCpuSubtypeMatch =
      getenv("LD_DYLIB_CPU_SUBTYPES_MUST_MATCH");
  config->objcStubsMode = getObjCStubsMode(args);
  config->ignoreAutoLink = args.hasArg(OPT_ignore_auto_link);
  for (const Arg *arg : args.filtered(OPT_ignore_auto_link_option))
    config->ignoreAutoLinkOptions.insert(arg->getValue());
  config->strictAutoLink = args.hasArg(OPT_strict_auto_link);
  config->ltoDebugPassManager = args.hasArg(OPT_lto_debug_pass_manager);
  config->csProfileGenerate = args.hasArg(OPT_cs_profile_generate);
  config->csProfilePath = args.getLastArgValue(OPT_cs_profile_path);
  config->pgoWarnMismatch =
      args.hasFlag(OPT_pgo_warn_mismatch, OPT_no_pgo_warn_mismatch, true);
  config->warnThinArchiveMissingMembers =
      args.hasFlag(OPT_warn_thin_archive_missing_members,
                   OPT_no_warn_thin_archive_missing_members, true);
  config->generateUuid = !args.hasArg(OPT_no_uuid);

  for (const Arg *arg : args.filtered(OPT_alias)) {
    config->aliasedSymbols.push_back(
        std::make_pair(arg->getValue(0), arg->getValue(1)));
  }

  if (const char *zero = getenv("ZERO_AR_DATE"))
    config->zeroModTime = strcmp(zero, "0") != 0;
  if (args.getLastArg(OPT_reproducible))
    config->zeroModTime = true;

  std::array<PlatformType, 4> encryptablePlatforms{
      PLATFORM_IOS, PLATFORM_WATCHOS, PLATFORM_TVOS, PLATFORM_XROS};
  config->emitEncryptionInfo =
      args.hasFlag(OPT_encryptable, OPT_no_encryption,
                   is_contained(encryptablePlatforms, config->platform()));

  if (const Arg *arg = args.getLastArg(OPT_install_name)) {
    if (config->warnDylibInstallName && config->outputType != MH_DYLIB)
      warn(
          arg->getAsString(args) +
          ": ignored, only has effect with -dylib [--warn-dylib-install-name]");
    else
      config->installName = arg->getValue();
  } else if (config->outputType == MH_DYLIB) {
    config->installName = config->finalOutput;
  }

  if (args.hasArg(OPT_mark_dead_strippable_dylib)) {
    if (config->outputType != MH_DYLIB)
      warn("-mark_dead_strippable_dylib: ignored, only has effect with -dylib");
    else
      config->markDeadStrippableDylib = true;
  }

  if (const Arg *arg = args.getLastArg(OPT_static, OPT_dynamic))
    config->staticLink = (arg->getOption().getID() == OPT_static);

  if (const Arg *arg =
          args.getLastArg(OPT_flat_namespace, OPT_twolevel_namespace))
    config->namespaceKind = arg->getOption().getID() == OPT_twolevel_namespace
                                ? NamespaceKind::twolevel
                                : NamespaceKind::flat;

  config->undefinedSymbolTreatment = getUndefinedSymbolTreatment(args);

  if (config->outputType == MH_EXECUTE)
    config->entry = symtab->addUndefined(args.getLastArgValue(OPT_e, "_main"),
                                         /*file=*/nullptr,
                                         /*isWeakRef=*/false);

  config->librarySearchPaths =
      getLibrarySearchPaths(args, config->systemLibraryRoots);
  config->frameworkSearchPaths =
      getFrameworkSearchPaths(args, config->systemLibraryRoots);
  if (const Arg *arg =
          args.getLastArg(OPT_search_paths_first, OPT_search_dylibs_first))
    config->searchDylibsFirst =
        arg->getOption().getID() == OPT_search_dylibs_first;

  config->dylibCompatibilityVersion =
      parseDylibVersion(args, OPT_compatibility_version);
  config->dylibCurrentVersion = parseDylibVersion(args, OPT_current_version);

  config->dataConst =
      args.hasFlag(OPT_data_const, OPT_no_data_const, dataConstDefault(args));
  // Populate config->sectionRenameMap with builtin default renames.
  // Options -rename_section and -rename_segment are able to override.
  initializeSectionRenameMap();
  // Reject every special character except '.' and '$'
  // TODO(gkm): verify that this is the proper set of invalid chars
  StringRef invalidNameChars("!\"#%&'()*+,-/:;<=>?@[\\]^`{|}~");
  auto validName = [invalidNameChars](StringRef s) {
    if (s.find_first_of(invalidNameChars) != StringRef::npos)
      error("invalid name for segment or section: " + s);
    return s;
  };
  for (const Arg *arg : args.filtered(OPT_rename_section)) {
    config->sectionRenameMap[{validName(arg->getValue(0)),
                              validName(arg->getValue(1))}] = {
        validName(arg->getValue(2)), validName(arg->getValue(3))};
  }
  for (const Arg *arg : args.filtered(OPT_rename_segment)) {
    config->segmentRenameMap[validName(arg->getValue(0))] =
        validName(arg->getValue(1));
  }

  config->sectionAlignments = parseSectAlign(args);

  for (const Arg *arg : args.filtered(OPT_segprot)) {
    StringRef segName = arg->getValue(0);
    uint32_t maxProt = parseProtection(arg->getValue(1));
    uint32_t initProt = parseProtection(arg->getValue(2));
    if (maxProt != initProt && config->arch() != AK_i386)
      error("invalid argument '" + arg->getAsString(args) +
            "': max and init must be the same for non-i386 archs");
    if (segName == segment_names::linkEdit)
      error("-segprot cannot be used to change __LINKEDIT's protections");
    config->segmentProtections.push_back({segName, maxProt, initProt});
  }

  config->hasExplicitExports =
      args.hasArg(OPT_no_exported_symbols) ||
      args.hasArgNoClaim(OPT_exported_symbol, OPT_exported_symbols_list);
  handleSymbolPatterns(args, config->exportedSymbols, OPT_exported_symbol,
                       OPT_exported_symbols_list);
  handleSymbolPatterns(args, config->unexportedSymbols, OPT_unexported_symbol,
                       OPT_unexported_symbols_list);
  if (config->hasExplicitExports && !config->unexportedSymbols.empty())
    error("cannot use both -exported_symbol* and -unexported_symbol* options");

  if (args.hasArg(OPT_no_exported_symbols) && !config->exportedSymbols.empty())
    error("cannot use both -exported_symbol* and -no_exported_symbols options");

  // Imitating LD64's:
  // -non_global_symbols_no_strip_list and -non_global_symbols_strip_list can't
  // both be present.
  // But -x can be used with either of these two, in which case, the last arg
  // takes effect.
  // (TODO: This is kind of confusing - considering disallowing using them
  // together for a more straightforward behaviour)
  {
    bool includeLocal = false;
    bool excludeLocal = false;
    for (const Arg *arg :
         args.filtered(OPT_x, OPT_non_global_symbols_no_strip_list,
                       OPT_non_global_symbols_strip_list)) {
      switch (arg->getOption().getID()) {
      case OPT_x:
        config->localSymbolsPresence = SymtabPresence::None;
        break;
      case OPT_non_global_symbols_no_strip_list:
        if (excludeLocal) {
          error("cannot use both -non_global_symbols_no_strip_list and "
                "-non_global_symbols_strip_list");
        } else {
          includeLocal = true;
          config->localSymbolsPresence = SymtabPresence::SelectivelyIncluded;
          parseSymbolPatternsFile(arg, config->localSymbolPatterns);
        }
        break;
      case OPT_non_global_symbols_strip_list:
        if (includeLocal) {
          error("cannot use both -non_global_symbols_no_strip_list and "
                "-non_global_symbols_strip_list");
        } else {
          excludeLocal = true;
          config->localSymbolsPresence = SymtabPresence::SelectivelyExcluded;
          parseSymbolPatternsFile(arg, config->localSymbolPatterns);
        }
        break;
      default:
        llvm_unreachable("unexpected option");
      }
    }
  }
  // Explicitly-exported literal symbols must be defined, but might
  // languish in an archive if unreferenced elsewhere or if they are in the
  // non-global strip list. Light a fire under those lazy symbols!
  for (const CachedHashStringRef &cachedName : config->exportedSymbols.literals)
    symtab->addUndefined(cachedName.val(), /*file=*/nullptr,
                         /*isWeakRef=*/false);

  for (const Arg *arg : args.filtered(OPT_why_live))
    config->whyLive.insert(arg->getValue());
  if (!config->whyLive.empty() && !config->deadStrip)
    warn("-why_live has no effect without -dead_strip, ignoring");

  config->saveTemps = args.hasArg(OPT_save_temps);

  config->adhocCodesign = args.hasFlag(
      OPT_adhoc_codesign, OPT_no_adhoc_codesign,
      shouldAdhocSignByDefault(config->arch(), config->platform()));

  if (args.hasArg(OPT_v)) {
    message(getLLDVersion(), lld::errs());
    message(StringRef("Library search paths:") +
                (config->librarySearchPaths.empty()
                     ? ""
                     : "\n\t" + join(config->librarySearchPaths, "\n\t")),
            lld::errs());
    message(StringRef("Framework search paths:") +
                (config->frameworkSearchPaths.empty()
                     ? ""
                     : "\n\t" + join(config->frameworkSearchPaths, "\n\t")),
            lld::errs());
  }

  config->progName = argsArr[0];

  config->timeTraceEnabled = args.hasArg(OPT_time_trace_eq);
  config->timeTraceGranularity =
      args::getInteger(args, OPT_time_trace_granularity_eq, 500);

  // Initialize time trace profiler.
  if (config->timeTraceEnabled)
    timeTraceProfilerInitialize(config->timeTraceGranularity, config->progName);

  {
    TimeTraceScope timeScope("ExecuteLinker");

    initLLVM(); // must be run before any call to addFile()
    createFiles(args);

    // Now that all dylibs have been loaded, search for those that should be
    // re-exported.
    {
      auto reexportHandler = [](const Arg *arg,
                                const std::vector<StringRef> &extensions) {
        config->hasReexports = true;
        StringRef searchName = arg->getValue();
        if (!markReexport(searchName, extensions))
          error(arg->getSpelling() + " " + searchName +
                " does not match a supplied dylib");
      };
      std::vector<StringRef> extensions = {".tbd"};
      for (const Arg *arg : args.filtered(OPT_sub_umbrella))
        reexportHandler(arg, extensions);

      extensions.push_back(".dylib");
      for (const Arg *arg : args.filtered(OPT_sub_library))
        reexportHandler(arg, extensions);
    }

    cl::ResetAllOptionOccurrences();

    // Parse LTO options.
    if (const Arg *arg = args.getLastArg(OPT_mcpu))
      parseClangOption(saver().save("-mcpu=" + StringRef(arg->getValue())),
                       arg->getSpelling());

    for (const Arg *arg : args.filtered(OPT_mllvm)) {
      parseClangOption(arg->getValue(), arg->getSpelling());
      config->mllvmOpts.emplace_back(arg->getValue());
    }

    createSyntheticSections();
    createSyntheticSymbols();
    addSynthenticMethnames();

    createAliases();
    // If we are in "explicit exports" mode, hide everything that isn't
    // explicitly exported. Do this before running LTO so that LTO can better
    // optimize.
    handleExplicitExports();

    bool didCompileBitcodeFiles = compileBitcodeFiles();

    resolveLCLinkerOptions();

    // If --thinlto-index-only is given, we should create only "index
    // files" and not object files. Index file creation is already done
    // in compileBitcodeFiles, so we are done if that's the case.
    if (config->thinLTOIndexOnly)
      return errorCount() == 0;

    // LTO may emit a non-hidden (extern) object file symbol even if the
    // corresponding bitcode symbol is hidden. In particular, this happens for
    // cross-module references to hidden symbols under ThinLTO. Thus, if we
    // compiled any bitcode files, we must redo the symbol hiding.
    if (didCompileBitcodeFiles)
      handleExplicitExports();
    replaceCommonSymbols();

    StringRef orderFile = args.getLastArgValue(OPT_order_file);
    if (!orderFile.empty())
      priorityBuilder.parseOrderFile(orderFile);

    referenceStubBinder();

    // FIXME: should terminate the link early based on errors encountered so
    // far?

    for (const Arg *arg : args.filtered(OPT_sectcreate)) {
      StringRef segName = arg->getValue(0);
      StringRef sectName = arg->getValue(1);
      StringRef fileName = arg->getValue(2);
      std::optional<MemoryBufferRef> buffer = readFile(fileName);
      if (buffer)
        inputFiles.insert(make<OpaqueFile>(*buffer, segName, sectName));
    }

    for (const Arg *arg : args.filtered(OPT_add_empty_section)) {
      StringRef segName = arg->getValue(0);
      StringRef sectName = arg->getValue(1);
      inputFiles.insert(make<OpaqueFile>(MemoryBufferRef(), segName, sectName));
    }

    gatherInputSections();
    if (config->callGraphProfileSort)
      priorityBuilder.extractCallGraphProfile();

    if (config->deadStrip)
      markLive();

    // Ensure that no symbols point inside __mod_init_func sections if they are
    // removed due to -init_offsets. This must run after dead stripping.
    if (config->emitInitOffsets)
      eraseInitializerSymbols();

    // Categories are not subject to dead-strip. The __objc_catlist section is
    // marked as NO_DEAD_STRIP and that propagates into all category data.
    if (args.hasArg(OPT_check_category_conflicts))
      objc::checkCategories();

    // Category merging uses "->live = false" to erase old category data, so
    // it has to run after dead-stripping (markLive).
    if (args.hasFlag(OPT_objc_category_merging, OPT_no_objc_category_merging,
                     false))
      objc::mergeCategories();

    // ICF assumes that all literals have been folded already, so we must run
    // foldIdenticalLiterals before foldIdenticalSections.
    foldIdenticalLiterals();
    if (config->icfLevel != ICFLevel::none) {
      if (config->icfLevel == ICFLevel::safe)
        markAddrSigSymbols();
      foldIdenticalSections(/*onlyCfStrings=*/false);
    } else if (config->dedupStrings) {
      foldIdenticalSections(/*onlyCfStrings=*/true);
    }

    // Write to an output file.
    if (target->wordSize == 8)
      writeResult<LP64>();
    else
      writeResult<ILP32>();

    depTracker->write(getLLDVersion(), inputFiles, config->outputFile);
  }

  if (config->timeTraceEnabled) {
    checkError(timeTraceProfilerWrite(
        args.getLastArgValue(OPT_time_trace_eq).str(), config->outputFile));

    timeTraceProfilerCleanup();
  }

  if (errorCount() != 0 || config->strictAutoLink)
    for (const auto &warning : missingAutolinkWarnings)
      warn(warning);

  return errorCount() == 0;
}
} // namespace macho
} // namespace lld
