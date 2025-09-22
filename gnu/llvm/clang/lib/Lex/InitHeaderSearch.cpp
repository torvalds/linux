//===--- InitHeaderSearch.cpp - Initialize header search paths ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the InitHeaderSearch class.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Config/config.h" // C_INCLUDE_DIRS
#include "clang/Lex/HeaderMap.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

using namespace clang;
using namespace clang::frontend;

namespace {
/// Holds information about a single DirectoryLookup object.
struct DirectoryLookupInfo {
  IncludeDirGroup Group;
  DirectoryLookup Lookup;
  std::optional<unsigned> UserEntryIdx;

  DirectoryLookupInfo(IncludeDirGroup Group, DirectoryLookup Lookup,
                      std::optional<unsigned> UserEntryIdx)
      : Group(Group), Lookup(Lookup), UserEntryIdx(UserEntryIdx) {}
};

/// This class makes it easier to set the search paths of a HeaderSearch object.
/// InitHeaderSearch stores several search path lists internally, which can be
/// sent to a HeaderSearch object in one swoop.
class InitHeaderSearch {
  std::vector<DirectoryLookupInfo> IncludePath;
  std::vector<std::pair<std::string, bool> > SystemHeaderPrefixes;
  HeaderSearch &Headers;
  bool Verbose;
  std::string IncludeSysroot;
  bool HasSysroot;

public:
  InitHeaderSearch(HeaderSearch &HS, bool verbose, StringRef sysroot)
      : Headers(HS), Verbose(verbose), IncludeSysroot(std::string(sysroot)),
        HasSysroot(!(sysroot.empty() || sysroot == "/")) {}

  /// Add the specified path to the specified group list, prefixing the sysroot
  /// if used.
  /// Returns true if the path exists, false if it was ignored.
  bool AddPath(const Twine &Path, IncludeDirGroup Group, bool isFramework,
               std::optional<unsigned> UserEntryIdx = std::nullopt);

  /// Add the specified path to the specified group list, without performing any
  /// sysroot remapping.
  /// Returns true if the path exists, false if it was ignored.
  bool AddUnmappedPath(const Twine &Path, IncludeDirGroup Group,
                       bool isFramework,
                       std::optional<unsigned> UserEntryIdx = std::nullopt);

  /// Add the specified prefix to the system header prefix list.
  void AddSystemHeaderPrefix(StringRef Prefix, bool IsSystemHeader) {
    SystemHeaderPrefixes.emplace_back(std::string(Prefix), IsSystemHeader);
  }

  /// Add the necessary paths to support a MinGW libstdc++.
  void AddMinGWCPlusPlusIncludePaths(StringRef Base,
                                     StringRef Arch,
                                     StringRef Version);

  /// Add paths that should always be searched.
  void AddDefaultCIncludePaths(const llvm::Triple &triple,
                               const HeaderSearchOptions &HSOpts);

  /// Add paths that should be searched when compiling c++.
  void AddDefaultCPlusPlusIncludePaths(const LangOptions &LangOpts,
                                       const llvm::Triple &triple,
                                       const HeaderSearchOptions &HSOpts);

  /// Returns true iff AddDefaultIncludePaths should do anything.  If this
  /// returns false, include paths should instead be handled in the driver.
  bool ShouldAddDefaultIncludePaths(const llvm::Triple &triple);

  /// Adds the default system include paths so that e.g. stdio.h is found.
  void AddDefaultIncludePaths(const LangOptions &Lang,
                              const llvm::Triple &triple,
                              const HeaderSearchOptions &HSOpts);

  /// Merges all search path lists into one list and send it to HeaderSearch.
  void Realize(const LangOptions &Lang);
};

}  // end anonymous namespace.

static bool CanPrefixSysroot(StringRef Path) {
#if defined(_WIN32)
  return !Path.empty() && llvm::sys::path::is_separator(Path[0]);
#else
  return llvm::sys::path::is_absolute(Path);
#endif
}

bool InitHeaderSearch::AddPath(const Twine &Path, IncludeDirGroup Group,
                               bool isFramework,
                               std::optional<unsigned> UserEntryIdx) {
  // Add the path with sysroot prepended, if desired and this is a system header
  // group.
  if (HasSysroot) {
    SmallString<256> MappedPathStorage;
    StringRef MappedPathStr = Path.toStringRef(MappedPathStorage);
    if (CanPrefixSysroot(MappedPathStr)) {
      return AddUnmappedPath(IncludeSysroot + Path, Group, isFramework,
                             UserEntryIdx);
    }
  }

  return AddUnmappedPath(Path, Group, isFramework, UserEntryIdx);
}

bool InitHeaderSearch::AddUnmappedPath(const Twine &Path, IncludeDirGroup Group,
                                       bool isFramework,
                                       std::optional<unsigned> UserEntryIdx) {
  assert(!Path.isTriviallyEmpty() && "can't handle empty path here");

  FileManager &FM = Headers.getFileMgr();
  SmallString<256> MappedPathStorage;
  StringRef MappedPathStr = Path.toStringRef(MappedPathStorage);

  // If use system headers while cross-compiling, emit the warning.
  if (HasSysroot && (MappedPathStr.starts_with("/usr/include") ||
                     MappedPathStr.starts_with("/usr/local/include"))) {
    Headers.getDiags().Report(diag::warn_poison_system_directories)
        << MappedPathStr;
  }

  // Compute the DirectoryLookup type.
  SrcMgr::CharacteristicKind Type;
  if (Group == Quoted || Group == Angled || Group == IndexHeaderMap) {
    Type = SrcMgr::C_User;
  } else if (Group == ExternCSystem) {
    Type = SrcMgr::C_ExternCSystem;
  } else {
    Type = SrcMgr::C_System;
  }

  // If the directory exists, add it.
  if (auto DE = FM.getOptionalDirectoryRef(MappedPathStr)) {
    IncludePath.emplace_back(Group, DirectoryLookup(*DE, Type, isFramework),
                             UserEntryIdx);
    return true;
  }

  // Check to see if this is an apple-style headermap (which are not allowed to
  // be frameworks).
  if (!isFramework) {
    if (auto FE = FM.getOptionalFileRef(MappedPathStr)) {
      if (const HeaderMap *HM = Headers.CreateHeaderMap(*FE)) {
        // It is a headermap, add it to the search path.
        IncludePath.emplace_back(
            Group, DirectoryLookup(HM, Type, Group == IndexHeaderMap),
            UserEntryIdx);
        return true;
      }
    }
  }

  if (Verbose)
    llvm::errs() << "ignoring nonexistent directory \""
                 << MappedPathStr << "\"\n";
  return false;
}

void InitHeaderSearch::AddMinGWCPlusPlusIncludePaths(StringRef Base,
                                                     StringRef Arch,
                                                     StringRef Version) {
  AddPath(Base + "/" + Arch + "/" + Version + "/include/c++",
          CXXSystem, false);
  AddPath(Base + "/" + Arch + "/" + Version + "/include/c++/" + Arch,
          CXXSystem, false);
  AddPath(Base + "/" + Arch + "/" + Version + "/include/c++/backward",
          CXXSystem, false);
}

void InitHeaderSearch::AddDefaultCIncludePaths(const llvm::Triple &triple,
                                            const HeaderSearchOptions &HSOpts) {
  if (!ShouldAddDefaultIncludePaths(triple))
    llvm_unreachable("Include management is handled in the driver.");

  llvm::Triple::OSType os = triple.getOS();

  if (HSOpts.UseStandardSystemIncludes) {
    switch (os) {
    case llvm::Triple::Win32:
      if (triple.getEnvironment() != llvm::Triple::Cygnus)
        break;
      [[fallthrough]];
    default:
      // FIXME: temporary hack: hard-coded paths.
      AddPath("/usr/local/include", System, false);
      break;
    }
  }

  // Builtin includes use #include_next directives and should be positioned
  // just prior C include dirs.
  if (HSOpts.UseBuiltinIncludes) {
    // Ignore the sys root, we *always* look for clang headers relative to
    // supplied path.
    SmallString<128> P = StringRef(HSOpts.ResourceDir);
    llvm::sys::path::append(P, "include");
    AddUnmappedPath(P, ExternCSystem, false);
  }

  // All remaining additions are for system include directories, early exit if
  // we aren't using them.
  if (!HSOpts.UseStandardSystemIncludes)
    return;

  // Add dirs specified via 'configure --with-c-include-dirs'.
  StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (CIncludeDirs != "") {
    SmallVector<StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (StringRef dir : dirs)
      AddPath(dir, ExternCSystem, false);
    return;
  }

  switch (os) {
  case llvm::Triple::Win32:
    switch (triple.getEnvironment()) {
    default: llvm_unreachable("Include management is handled in the driver.");
    case llvm::Triple::Cygnus:
      AddPath("/usr/include/w32api", System, false);
      break;
    case llvm::Triple::GNU:
      break;
    }
    break;
  default:
    break;
  }

  AddPath("/usr/include", ExternCSystem, false);
}

void InitHeaderSearch::AddDefaultCPlusPlusIncludePaths(
    const LangOptions &LangOpts, const llvm::Triple &triple,
    const HeaderSearchOptions &HSOpts) {
  if (!ShouldAddDefaultIncludePaths(triple))
    llvm_unreachable("Include management is handled in the driver.");

  // FIXME: temporary hack: hard-coded paths.
  llvm::Triple::OSType os = triple.getOS();
  switch (os) {
  case llvm::Triple::Win32:
    switch (triple.getEnvironment()) {
    default: llvm_unreachable("Include management is handled in the driver.");
    case llvm::Triple::Cygnus:
      // Cygwin-1.7
      AddMinGWCPlusPlusIncludePaths("/usr/lib/gcc", "i686-pc-cygwin", "4.7.3");
      AddMinGWCPlusPlusIncludePaths("/usr/lib/gcc", "i686-pc-cygwin", "4.5.3");
      AddMinGWCPlusPlusIncludePaths("/usr/lib/gcc", "i686-pc-cygwin", "4.3.4");
      // g++-4 / Cygwin-1.5
      AddMinGWCPlusPlusIncludePaths("/usr/lib/gcc", "i686-pc-cygwin", "4.3.2");
      break;
    }
    break;
  default:
    break;
  }
}

bool InitHeaderSearch::ShouldAddDefaultIncludePaths(
    const llvm::Triple &triple) {
  switch (triple.getOS()) {
  case llvm::Triple::AIX:
  case llvm::Triple::DragonFly:
  case llvm::Triple::ELFIAMCU:
  case llvm::Triple::Emscripten:
  case llvm::Triple::FreeBSD:
  case llvm::Triple::Fuchsia:
  case llvm::Triple::Haiku:
  case llvm::Triple::Hurd:
  case llvm::Triple::Linux:
  case llvm::Triple::LiteOS:
  case llvm::Triple::NaCl:
  case llvm::Triple::NetBSD:
  case llvm::Triple::OpenBSD:
  case llvm::Triple::PS4:
  case llvm::Triple::PS5:
  case llvm::Triple::RTEMS:
  case llvm::Triple::Solaris:
  case llvm::Triple::WASI:
  case llvm::Triple::ZOS:
    return false;

  case llvm::Triple::Win32:
    if (triple.getEnvironment() != llvm::Triple::Cygnus ||
        triple.isOSBinFormatMachO())
      return false;
    break;

  case llvm::Triple::UnknownOS:
    if (triple.isWasm())
      return false;
    break;

  default:
    break;
  }

  return true; // Everything else uses AddDefaultIncludePaths().
}

void InitHeaderSearch::AddDefaultIncludePaths(
    const LangOptions &Lang, const llvm::Triple &triple,
    const HeaderSearchOptions &HSOpts) {
  // NB: This code path is going away. All of the logic is moving into the
  // driver which has the information necessary to do target-specific
  // selections of default include paths. Each target which moves there will be
  // exempted from this logic in ShouldAddDefaultIncludePaths() until we can
  // delete the entire pile of code.
  if (!ShouldAddDefaultIncludePaths(triple))
    return;

  // NOTE: some additional header search logic is handled in the driver for
  // Darwin.
  if (triple.isOSDarwin()) {
    if (HSOpts.UseStandardSystemIncludes) {
      // Add the default framework include paths on Darwin.
      if (triple.isDriverKit()) {
        AddPath("/System/DriverKit/System/Library/Frameworks", System, true);
      } else {
        AddPath("/System/Library/Frameworks", System, true);
        AddPath("/Library/Frameworks", System, true);
      }
    }
    return;
  }

  if (Lang.CPlusPlus && !Lang.AsmPreprocessor &&
      HSOpts.UseStandardCXXIncludes && HSOpts.UseStandardSystemIncludes) {
    if (HSOpts.UseLibcxx) {
      AddPath("/usr/include/c++/v1", CXXSystem, false);
    } else {
      AddDefaultCPlusPlusIncludePaths(Lang, triple, HSOpts);
    }
  }

  AddDefaultCIncludePaths(triple, HSOpts);
}

/// If there are duplicate directory entries in the specified search list,
/// remove the later (dead) ones.  Returns the number of non-system headers
/// removed, which is used to update NumAngled.
static unsigned RemoveDuplicates(std::vector<DirectoryLookupInfo> &SearchList,
                                 unsigned First, bool Verbose) {
  llvm::SmallPtrSet<const DirectoryEntry *, 8> SeenDirs;
  llvm::SmallPtrSet<const DirectoryEntry *, 8> SeenFrameworkDirs;
  llvm::SmallPtrSet<const HeaderMap *, 8> SeenHeaderMaps;
  unsigned NonSystemRemoved = 0;
  for (unsigned i = First; i != SearchList.size(); ++i) {
    unsigned DirToRemove = i;

    const DirectoryLookup &CurEntry = SearchList[i].Lookup;

    if (CurEntry.isNormalDir()) {
      // If this isn't the first time we've seen this dir, remove it.
      if (SeenDirs.insert(CurEntry.getDir()).second)
        continue;
    } else if (CurEntry.isFramework()) {
      // If this isn't the first time we've seen this framework dir, remove it.
      if (SeenFrameworkDirs.insert(CurEntry.getFrameworkDir()).second)
        continue;
    } else {
      assert(CurEntry.isHeaderMap() && "Not a headermap or normal dir?");
      // If this isn't the first time we've seen this headermap, remove it.
      if (SeenHeaderMaps.insert(CurEntry.getHeaderMap()).second)
        continue;
    }

    // If we have a normal #include dir/framework/headermap that is shadowed
    // later in the chain by a system include location, we actually want to
    // ignore the user's request and drop the user dir... keeping the system
    // dir.  This is weird, but required to emulate GCC's search path correctly.
    //
    // Since dupes of system dirs are rare, just rescan to find the original
    // that we're nuking instead of using a DenseMap.
    if (CurEntry.getDirCharacteristic() != SrcMgr::C_User) {
      // Find the dir that this is the same of.
      unsigned FirstDir;
      for (FirstDir = First;; ++FirstDir) {
        assert(FirstDir != i && "Didn't find dupe?");

        const DirectoryLookup &SearchEntry = SearchList[FirstDir].Lookup;

        // If these are different lookup types, then they can't be the dupe.
        if (SearchEntry.getLookupType() != CurEntry.getLookupType())
          continue;

        bool isSame;
        if (CurEntry.isNormalDir())
          isSame = SearchEntry.getDir() == CurEntry.getDir();
        else if (CurEntry.isFramework())
          isSame = SearchEntry.getFrameworkDir() == CurEntry.getFrameworkDir();
        else {
          assert(CurEntry.isHeaderMap() && "Not a headermap or normal dir?");
          isSame = SearchEntry.getHeaderMap() == CurEntry.getHeaderMap();
        }

        if (isSame)
          break;
      }

      // If the first dir in the search path is a non-system dir, zap it
      // instead of the system one.
      if (SearchList[FirstDir].Lookup.getDirCharacteristic() == SrcMgr::C_User)
        DirToRemove = FirstDir;
    }

    if (Verbose) {
      llvm::errs() << "ignoring duplicate directory \""
                   << CurEntry.getName() << "\"\n";
      if (DirToRemove != i)
        llvm::errs() << "  as it is a non-system directory that duplicates "
                     << "a system directory\n";
    }
    if (DirToRemove != i)
      ++NonSystemRemoved;

    // This is reached if the current entry is a duplicate.  Remove the
    // DirToRemove (usually the current dir).
    SearchList.erase(SearchList.begin()+DirToRemove);
    --i;
  }
  return NonSystemRemoved;
}

/// Extract DirectoryLookups from DirectoryLookupInfos.
static std::vector<DirectoryLookup>
extractLookups(const std::vector<DirectoryLookupInfo> &Infos) {
  std::vector<DirectoryLookup> Lookups;
  Lookups.reserve(Infos.size());
  llvm::transform(Infos, std::back_inserter(Lookups),
                  [](const DirectoryLookupInfo &Info) { return Info.Lookup; });
  return Lookups;
}

/// Collect the mapping between indices of DirectoryLookups and UserEntries.
static llvm::DenseMap<unsigned, unsigned>
mapToUserEntries(const std::vector<DirectoryLookupInfo> &Infos) {
  llvm::DenseMap<unsigned, unsigned> LookupsToUserEntries;
  for (unsigned I = 0, E = Infos.size(); I < E; ++I) {
    // Check whether this DirectoryLookup maps to a HeaderSearch::UserEntry.
    if (Infos[I].UserEntryIdx)
      LookupsToUserEntries.insert({I, *Infos[I].UserEntryIdx});
  }
  return LookupsToUserEntries;
}

void InitHeaderSearch::Realize(const LangOptions &Lang) {
  // Concatenate ANGLE+SYSTEM+AFTER chains together into SearchList.
  std::vector<DirectoryLookupInfo> SearchList;
  SearchList.reserve(IncludePath.size());

  // Quoted arguments go first.
  for (auto &Include : IncludePath)
    if (Include.Group == Quoted)
      SearchList.push_back(Include);

  // Deduplicate and remember index.
  RemoveDuplicates(SearchList, 0, Verbose);
  unsigned NumQuoted = SearchList.size();

  for (auto &Include : IncludePath)
    if (Include.Group == Angled || Include.Group == IndexHeaderMap)
      SearchList.push_back(Include);

  RemoveDuplicates(SearchList, NumQuoted, Verbose);
  unsigned NumAngled = SearchList.size();

  for (auto &Include : IncludePath)
    if (Include.Group == System || Include.Group == ExternCSystem ||
        (!Lang.ObjC && !Lang.CPlusPlus && Include.Group == CSystem) ||
        (/*FIXME !Lang.ObjC && */ Lang.CPlusPlus &&
         Include.Group == CXXSystem) ||
        (Lang.ObjC && !Lang.CPlusPlus && Include.Group == ObjCSystem) ||
        (Lang.ObjC && Lang.CPlusPlus && Include.Group == ObjCXXSystem))
      SearchList.push_back(Include);

  for (auto &Include : IncludePath)
    if (Include.Group == After)
      SearchList.push_back(Include);

  // Remove duplicates across both the Angled and System directories.  GCC does
  // this and failing to remove duplicates across these two groups breaks
  // #include_next.
  unsigned NonSystemRemoved = RemoveDuplicates(SearchList, NumQuoted, Verbose);
  NumAngled -= NonSystemRemoved;

  Headers.SetSearchPaths(extractLookups(SearchList), NumQuoted, NumAngled,
                         mapToUserEntries(SearchList));

  Headers.SetSystemHeaderPrefixes(SystemHeaderPrefixes);

  // If verbose, print the list of directories that will be searched.
  if (Verbose) {
    llvm::errs() << "#include \"...\" search starts here:\n";
    for (unsigned i = 0, e = SearchList.size(); i != e; ++i) {
      if (i == NumQuoted)
        llvm::errs() << "#include <...> search starts here:\n";
      StringRef Name = SearchList[i].Lookup.getName();
      const char *Suffix;
      if (SearchList[i].Lookup.isNormalDir())
        Suffix = "";
      else if (SearchList[i].Lookup.isFramework())
        Suffix = " (framework directory)";
      else {
        assert(SearchList[i].Lookup.isHeaderMap() && "Unknown DirectoryLookup");
        Suffix = " (headermap)";
      }
      llvm::errs() << " " << Name << Suffix << "\n";
    }
    llvm::errs() << "End of search list.\n";
  }
}

void clang::ApplyHeaderSearchOptions(HeaderSearch &HS,
                                     const HeaderSearchOptions &HSOpts,
                                     const LangOptions &Lang,
                                     const llvm::Triple &Triple) {
  InitHeaderSearch Init(HS, HSOpts.Verbose, HSOpts.Sysroot);

  // Add the user defined entries.
  for (unsigned i = 0, e = HSOpts.UserEntries.size(); i != e; ++i) {
    const HeaderSearchOptions::Entry &E = HSOpts.UserEntries[i];
    if (E.IgnoreSysRoot) {
      Init.AddUnmappedPath(E.Path, E.Group, E.IsFramework, i);
    } else {
      Init.AddPath(E.Path, E.Group, E.IsFramework, i);
    }
  }

  Init.AddDefaultIncludePaths(Lang, Triple, HSOpts);

  for (unsigned i = 0, e = HSOpts.SystemHeaderPrefixes.size(); i != e; ++i)
    Init.AddSystemHeaderPrefix(HSOpts.SystemHeaderPrefixes[i].Prefix,
                               HSOpts.SystemHeaderPrefixes[i].IsSystemHeader);

  if (HSOpts.UseBuiltinIncludes) {
    // Set up the builtin include directory in the module map.
    SmallString<128> P = StringRef(HSOpts.ResourceDir);
    llvm::sys::path::append(P, "include");
    if (auto Dir = HS.getFileMgr().getOptionalDirectoryRef(P))
      HS.getModuleMap().setBuiltinIncludeDir(*Dir);
  }

  Init.Realize(Lang);
}
