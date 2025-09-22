//===-- llvm-config.cpp - LLVM project configuration utility --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tool encapsulates information about an LLVM project configuration for
// use by other project's build environments (to determine installed path,
// available features, required libraries, etc.).
//
// Note that although this tool *may* be used by some parts of LLVM's build
// itself (i.e., the Makefiles use it to compute required libraries when linking
// tools), this tool is primarily designed to support external projects.
//
//===----------------------------------------------------------------------===//

#include "llvm/Config/llvm-config.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdlib>
#include <set>
#include <unordered_set>
#include <vector>

using namespace llvm;

// Include the build time variables we can report to the user. This is generated
// at build time from the BuildVariables.inc.in file by the build system.
#include "BuildVariables.inc"

// Include the component table. This creates an array of struct
// AvailableComponent entries, which record the component name, library name,
// and required components for all of the available libraries.
//
// Not all components define a library, we also use "library groups" as a way to
// create entries for pseudo groups like x86 or all-targets.
#include "LibraryDependencies.inc"

// Built-in extensions also register their dependencies, but in a separate file,
// later in the process.
#include "ExtensionDependencies.inc"

// LinkMode determines what libraries and flags are returned by llvm-config.
enum LinkMode {
  // LinkModeAuto will link with the default link mode for the installation,
  // which is dependent on the value of LLVM_LINK_LLVM_DYLIB, and fall back
  // to the alternative if the required libraries are not available.
  LinkModeAuto = 0,

  // LinkModeShared will link with the dynamic component libraries if they
  // exist, and return an error otherwise.
  LinkModeShared = 1,

  // LinkModeStatic will link with the static component libraries if they
  // exist, and return an error otherwise.
  LinkModeStatic = 2,
};

/// Traverse a single component adding to the topological ordering in
/// \arg RequiredLibs.
///
/// \param Name - The component to traverse.
/// \param ComponentMap - A prebuilt map of component names to descriptors.
/// \param VisitedComponents [in] [out] - The set of already visited components.
/// \param RequiredLibs [out] - The ordered list of required
/// libraries.
/// \param GetComponentNames - Get the component names instead of the
/// library name.
static void VisitComponent(const std::string &Name,
                           const StringMap<AvailableComponent *> &ComponentMap,
                           std::set<AvailableComponent *> &VisitedComponents,
                           std::vector<std::string> &RequiredLibs,
                           bool IncludeNonInstalled, bool GetComponentNames,
                           const std::function<std::string(const StringRef &)>
                               *GetComponentLibraryPath,
                           std::vector<std::string> *Missing,
                           const std::string &DirSep) {
  // Lookup the component.
  AvailableComponent *AC = ComponentMap.lookup(Name);
  if (!AC) {
    errs() << "Can't find component: '" << Name << "' in the map. Available components are: ";
    for (const auto &Component : ComponentMap) {
      errs() << "'" << Component.first() << "' ";
    }
    errs() << "\n";
    report_fatal_error("abort");
  }
  assert(AC && "Invalid component name!");

  // Add to the visited table.
  if (!VisitedComponents.insert(AC).second) {
    // We are done if the component has already been visited.
    return;
  }

  // Only include non-installed components if requested.
  if (!AC->IsInstalled && !IncludeNonInstalled)
    return;

  // Otherwise, visit all the dependencies.
  for (unsigned i = 0; AC->RequiredLibraries[i]; ++i) {
    VisitComponent(AC->RequiredLibraries[i], ComponentMap, VisitedComponents,
                   RequiredLibs, IncludeNonInstalled, GetComponentNames,
                   GetComponentLibraryPath, Missing, DirSep);
  }

  // Special handling for the special 'extensions' component. Its content is
  // not populated by llvm-build, but later in the process and loaded from
  // ExtensionDependencies.inc.
  if (Name == "extensions") {
    for (auto const &AvailableExtension : AvailableExtensions) {
      for (const char *const *Iter = &AvailableExtension.RequiredLibraries[0];
           *Iter; ++Iter) {
        AvailableComponent *AC = ComponentMap.lookup(*Iter);
        if (!AC) {
          RequiredLibs.push_back(*Iter);
        } else {
          VisitComponent(*Iter, ComponentMap, VisitedComponents, RequiredLibs,
                         IncludeNonInstalled, GetComponentNames,
                         GetComponentLibraryPath, Missing, DirSep);
        }
      }
    }
  }

  if (GetComponentNames) {
    RequiredLibs.push_back(Name);
    return;
  }

  // Add to the required library list.
  if (AC->Library) {
    if (Missing && GetComponentLibraryPath) {
      std::string path = (*GetComponentLibraryPath)(AC->Library);
      if (DirSep == "\\") {
        std::replace(path.begin(), path.end(), '/', '\\');
      }
      if (!sys::fs::exists(path))
        Missing->push_back(path);
    }
    RequiredLibs.push_back(AC->Library);
  }
}

/// Compute the list of required libraries for a given list of
/// components, in an order suitable for passing to a linker (that is, libraries
/// appear prior to their dependencies).
///
/// \param Components - The names of the components to find libraries for.
/// \param IncludeNonInstalled - Whether non-installed components should be
/// reported.
/// \param GetComponentNames - True if one would prefer the component names.
static std::vector<std::string> ComputeLibsForComponents(
    const std::vector<StringRef> &Components, bool IncludeNonInstalled,
    bool GetComponentNames, const std::function<std::string(const StringRef &)>
                                *GetComponentLibraryPath,
    std::vector<std::string> *Missing, const std::string &DirSep) {
  std::vector<std::string> RequiredLibs;
  std::set<AvailableComponent *> VisitedComponents;

  // Build a map of component names to information.
  StringMap<AvailableComponent *> ComponentMap;
  for (auto &AC : AvailableComponents)
    ComponentMap[AC.Name] = &AC;

  // Visit the components.
  for (unsigned i = 0, e = Components.size(); i != e; ++i) {
    // Users are allowed to provide mixed case component names.
    std::string ComponentLower = Components[i].lower();

    // Validate that the user supplied a valid component name.
    if (!ComponentMap.count(ComponentLower)) {
      llvm::errs() << "llvm-config: unknown component name: " << Components[i]
                   << "\n";
      exit(1);
    }

    VisitComponent(ComponentLower, ComponentMap, VisitedComponents,
                   RequiredLibs, IncludeNonInstalled, GetComponentNames,
                   GetComponentLibraryPath, Missing, DirSep);
  }

  // The list is now ordered with leafs first, we want the libraries to printed
  // in the reverse order of dependency.
  std::reverse(RequiredLibs.begin(), RequiredLibs.end());

  return RequiredLibs;
}

/* *** */

static void usage(bool ExitWithFailure = true) {
  errs() << "\
usage: llvm-config <OPTION>... [<COMPONENT>...]\n\
\n\
Get various configuration information needed to compile programs which use\n\
LLVM.  Typically called from 'configure' scripts.  Examples:\n\
  llvm-config --cxxflags\n\
  llvm-config --ldflags\n\
  llvm-config --libs engine bcreader scalaropts\n\
\n\
Options:\n\
  --assertion-mode  Print assertion mode of LLVM tree (ON or OFF).\n\
  --bindir          Directory containing LLVM executables.\n\
  --build-mode      Print build mode of LLVM tree (e.g. Debug or Release).\n\
  --build-system    Print the build system used to build LLVM (e.g. `cmake` or `gn`).\n\
  --cflags          C compiler flags for files that include LLVM headers.\n\
  --cmakedir        Directory containing LLVM CMake modules.\n\
  --components      List of all possible components.\n\
  --cppflags        C preprocessor flags for files that include LLVM headers.\n\
  --cxxflags        C++ compiler flags for files that include LLVM headers.\n\
  --has-rtti        Print whether or not LLVM was built with rtti (YES or NO).\n\
  --help            Print a summary of llvm-config arguments.\n\
  --host-target     Target triple used to configure LLVM.\n\
  --ignore-libllvm  Ignore libLLVM and link component libraries instead.\n\
  --includedir      Directory containing LLVM headers.\n\
  --ldflags         Print Linker flags.\n\
  --libdir          Directory containing LLVM libraries.\n\
  --libfiles        Fully qualified library filenames for makefile depends.\n\
  --libnames        Bare library names for in-tree builds.\n\
  --libs            Libraries needed to link against LLVM components.\n\
  --link-shared     Link the components as shared libraries.\n\
  --link-static     Link the component libraries statically.\n\
  --obj-root        Print the object root used to build LLVM.\n\
  --prefix          Print the installation prefix.\n\
  --shared-mode     Print how the provided components can be collectively linked (`shared` or `static`).\n\
  --system-libs     System Libraries needed to link against LLVM components.\n\
  --targets-built   List of all targets currently built.\n\
  --version         Print LLVM version.\n\
Typical components:\n\
  all               All LLVM libraries (default).\n\
  engine            Either a native JIT or a bitcode interpreter.\n";
  if (ExitWithFailure)
    exit(1);
}

/// Compute the path to the main executable.
std::string GetExecutablePath(const char *Argv0) {
  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *P = (void *)(intptr_t)GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, P);
}

/// Expand the semi-colon delimited LLVM_DYLIB_COMPONENTS into
/// the full list of components.
std::vector<std::string> GetAllDyLibComponents(const bool IsInDevelopmentTree,
                                               const bool GetComponentNames,
                                               const std::string &DirSep) {
  std::vector<StringRef> DyLibComponents;

  StringRef DyLibComponentsStr(LLVM_DYLIB_COMPONENTS);
  size_t Offset = 0;
  while (true) {
    const size_t NextOffset = DyLibComponentsStr.find(';', Offset);
    DyLibComponents.push_back(DyLibComponentsStr.substr(Offset, NextOffset-Offset));
    if (NextOffset == std::string::npos) {
      break;
    }
    Offset = NextOffset + 1;
  }

  assert(!DyLibComponents.empty());

  return ComputeLibsForComponents(DyLibComponents,
                                  /*IncludeNonInstalled=*/IsInDevelopmentTree,
                                  GetComponentNames, nullptr, nullptr, DirSep);
}

int main(int argc, char **argv) {
  std::vector<StringRef> Components;
  bool PrintLibs = false, PrintLibNames = false, PrintLibFiles = false;
  bool PrintSystemLibs = false, PrintSharedMode = false;
  bool HasAnyOption = false;

  // llvm-config is designed to support being run both from a development tree
  // and from an installed path. We try and auto-detect which case we are in so
  // that we can report the correct information when run from a development
  // tree.
  bool IsInDevelopmentTree;
  enum { CMakeStyle, CMakeBuildModeStyle } DevelopmentTreeLayout;
  llvm::SmallString<256> CurrentPath(GetExecutablePath(argv[0]));
  std::string CurrentExecPrefix;
  std::string ActiveObjRoot;

  // If CMAKE_CFG_INTDIR is given, honor it as build mode.
  char const *build_mode = LLVM_BUILDMODE;
#if defined(CMAKE_CFG_INTDIR)
  if (!(CMAKE_CFG_INTDIR[0] == '.' && CMAKE_CFG_INTDIR[1] == '\0'))
    build_mode = CMAKE_CFG_INTDIR;
#endif

  // Create an absolute path, and pop up one directory (we expect to be inside a
  // bin dir).
  sys::fs::make_absolute(CurrentPath);
  CurrentExecPrefix =
      sys::path::parent_path(sys::path::parent_path(CurrentPath)).str();

  // Check to see if we are inside a development tree by comparing to possible
  // locations (prefix style or CMake style).
  if (sys::fs::equivalent(CurrentExecPrefix, LLVM_OBJ_ROOT)) {
    IsInDevelopmentTree = true;
    DevelopmentTreeLayout = CMakeStyle;
    ActiveObjRoot = LLVM_OBJ_ROOT;
  } else if (sys::fs::equivalent(sys::path::parent_path(CurrentExecPrefix),
                                 LLVM_OBJ_ROOT)) {
    IsInDevelopmentTree = true;
    DevelopmentTreeLayout = CMakeBuildModeStyle;
    ActiveObjRoot = LLVM_OBJ_ROOT;
  } else {
    IsInDevelopmentTree = false;
    DevelopmentTreeLayout = CMakeStyle; // Initialized to avoid warnings.
  }

  // Compute various directory locations based on the derived location
  // information.
  std::string ActivePrefix, ActiveBinDir, ActiveIncludeDir, ActiveLibDir,
              ActiveCMakeDir;
  std::string ActiveIncludeOption;
  if (IsInDevelopmentTree) {
    ActiveIncludeDir = std::string(LLVM_SRC_ROOT) + "/include";
    ActivePrefix = CurrentExecPrefix;

    // CMake organizes the products differently than a normal prefix style
    // layout.
    switch (DevelopmentTreeLayout) {
    case CMakeStyle:
      ActiveBinDir = ActiveObjRoot + "/bin";
      ActiveLibDir = ActiveObjRoot + "/lib" + LLVM_LIBDIR_SUFFIX;
      ActiveCMakeDir = ActiveLibDir + "/cmake/llvm";
      break;
    case CMakeBuildModeStyle:
      // FIXME: Should we consider the build-mode-specific path as the prefix?
      ActivePrefix = ActiveObjRoot;
      ActiveBinDir = ActiveObjRoot + "/" + build_mode + "/bin";
      ActiveLibDir =
          ActiveObjRoot + "/" + build_mode + "/lib" + LLVM_LIBDIR_SUFFIX;
      // The CMake directory isn't separated by build mode.
      ActiveCMakeDir =
          ActivePrefix + "/lib" + LLVM_LIBDIR_SUFFIX + "/cmake/llvm";
      break;
    }

    // We need to include files from both the source and object trees.
    ActiveIncludeOption =
        ("-I" + ActiveIncludeDir + " " + "-I" + ActiveObjRoot + "/include");
  } else {
    ActivePrefix = CurrentExecPrefix;
    {
      SmallString<256> Path(LLVM_INSTALL_INCLUDEDIR);
      sys::fs::make_absolute(ActivePrefix, Path);
      ActiveIncludeDir = std::string(Path);
    }
    {
      SmallString<256> Path(LLVM_TOOLS_INSTALL_DIR);
      sys::fs::make_absolute(ActivePrefix, Path);
      ActiveBinDir = std::string(Path);
    }
    ActiveLibDir = ActivePrefix + "/lib" + LLVM_LIBDIR_SUFFIX;
    {
      SmallString<256> Path(LLVM_INSTALL_PACKAGE_DIR);
      sys::fs::make_absolute(ActivePrefix, Path);
      ActiveCMakeDir = std::string(Path);
    }
    ActiveIncludeOption = "-I" + ActiveIncludeDir;
  }

  /// We only use `shared library` mode in cases where the static library form
  /// of the components provided are not available; note however that this is
  /// skipped if we're run from within the build dir. However, once installed,
  /// we still need to provide correct output when the static archives are
  /// removed or, as in the case of CMake's `BUILD_SHARED_LIBS`, never present
  /// in the first place. This can't be done at configure/build time.

  StringRef SharedExt, SharedVersionedExt, SharedDir, SharedPrefix, StaticExt,
      StaticPrefix, StaticDir = "lib";
  std::string DirSep = "/";
  const Triple HostTriple(Triple::normalize(LLVM_HOST_TRIPLE));
  if (HostTriple.isOSWindows()) {
    SharedExt = "dll";
    SharedVersionedExt = LLVM_DYLIB_VERSION ".dll";
    if (HostTriple.isOSCygMing()) {
      SharedPrefix = "lib";
      StaticExt = "a";
      StaticPrefix = "lib";
    } else {
      StaticExt = "lib";
      DirSep = "\\";
      std::replace(ActiveObjRoot.begin(), ActiveObjRoot.end(), '/', '\\');
      std::replace(ActivePrefix.begin(), ActivePrefix.end(), '/', '\\');
      std::replace(ActiveBinDir.begin(), ActiveBinDir.end(), '/', '\\');
      std::replace(ActiveLibDir.begin(), ActiveLibDir.end(), '/', '\\');
      std::replace(ActiveCMakeDir.begin(), ActiveCMakeDir.end(), '/', '\\');
      std::replace(ActiveIncludeOption.begin(), ActiveIncludeOption.end(), '/',
                   '\\');
    }
    SharedDir = ActiveBinDir;
    StaticDir = ActiveLibDir;
  } else if (HostTriple.isOSDarwin()) {
    SharedExt = "dylib";
    SharedVersionedExt = LLVM_DYLIB_VERSION ".dylib";
    StaticExt = "a";
    StaticDir = SharedDir = ActiveLibDir;
    StaticPrefix = SharedPrefix = "lib";
  } else if (HostTriple.isOSOpenBSD()) {
    SharedExt = "so";
    SharedVersionedExt = ".so" ;
    StaticExt = "a";
    StaticDir = SharedDir = ActiveLibDir;
    StaticPrefix = SharedPrefix = "lib";
  } else {
    // default to the unix values:
    SharedExt = "so";
    SharedVersionedExt = LLVM_DYLIB_VERSION ".so";
    StaticExt = "a";
    StaticDir = SharedDir = ActiveLibDir;
    StaticPrefix = SharedPrefix = "lib";
  }

  const bool BuiltDyLib = !!LLVM_ENABLE_DYLIB;

  /// CMake style shared libs, ie each component is in a shared library.
  const bool BuiltSharedLibs = !!LLVM_ENABLE_SHARED;

  bool DyLibExists = false;
  const std::string DyLibName =
      (SharedPrefix + "LLVM" + SharedVersionedExt).str();

  // If LLVM_LINK_DYLIB is ON, the single shared library will be returned
  // for "--libs", etc, if they exist. This behaviour can be overridden with
  // --link-static or --link-shared.
  bool LinkDyLib = !!LLVM_LINK_DYLIB;

  if (BuiltDyLib) {
    std::string path((SharedDir + DirSep + DyLibName).str());
    if (DirSep == "\\") {
      std::replace(path.begin(), path.end(), '/', '\\');
    }
    // path does not include major.minor
    if (HostTriple.isOSOpenBSD()) {
      DyLibExists = true;
    } else {
      DyLibExists = sys::fs::exists(path);
    }
    if (!DyLibExists) {
      // The shared library does not exist: don't error unless the user
      // explicitly passes --link-shared.
      LinkDyLib = false;
    }
  }
  LinkMode LinkMode =
      (LinkDyLib || BuiltSharedLibs) ? LinkModeShared : LinkModeAuto;

  /// Get the component's library name without the lib prefix and the
  /// extension. Returns true if Lib is in a recognized format.
  auto GetComponentLibraryNameSlice = [&](const StringRef &Lib,
                                          StringRef &Out) {
    if (Lib.starts_with("lib")) {
      unsigned FromEnd;
      if (Lib.ends_with(StaticExt)) {
        FromEnd = StaticExt.size() + 1;
      } else if (Lib.ends_with(SharedExt)) {
        FromEnd = SharedExt.size() + 1;
      } else {
        FromEnd = 0;
      }

      if (FromEnd != 0) {
        Out = Lib.slice(3, Lib.size() - FromEnd);
        return true;
      }
    }

    return false;
  };
  /// Maps Unixizms to the host platform.
  auto GetComponentLibraryFileName = [&](const StringRef &Lib,
                                         const bool Shared) {
    std::string LibFileName;
    if (Shared) {
      if (Lib == DyLibName) {
        // Treat the DyLibName specially. It is not a component library and
        // already has the necessary prefix and suffix (e.g. `.so`) added so
        // just return it unmodified.
        assert(Lib.ends_with(SharedExt) && "DyLib is missing suffix");
        LibFileName = std::string(Lib);
      } else {
        LibFileName = (SharedPrefix + Lib + "." + SharedExt).str();
      }
    } else {
      // default to static
      LibFileName = (StaticPrefix + Lib + "." + StaticExt).str();
    }

    return LibFileName;
  };
  /// Get the full path for a possibly shared component library.
  auto GetComponentLibraryPath = [&](const StringRef &Name, const bool Shared) {
    auto LibFileName = GetComponentLibraryFileName(Name, Shared);
    if (Shared) {
      return (SharedDir + DirSep + LibFileName).str();
    } else {
      return (StaticDir + DirSep + LibFileName).str();
    }
  };

  raw_ostream &OS = outs();
  for (int i = 1; i != argc; ++i) {
    StringRef Arg = argv[i];

    if (Arg.starts_with("-")) {
      HasAnyOption = true;
      if (Arg == "--version") {
        OS << PACKAGE_VERSION << '\n';
      } else if (Arg == "--prefix") {
        OS << ActivePrefix << '\n';
      } else if (Arg == "--bindir") {
        OS << ActiveBinDir << '\n';
      } else if (Arg == "--includedir") {
        OS << ActiveIncludeDir << '\n';
      } else if (Arg == "--libdir") {
        OS << ActiveLibDir << '\n';
      } else if (Arg == "--cmakedir") {
        OS << ActiveCMakeDir << '\n';
      } else if (Arg == "--cppflags") {
        OS << ActiveIncludeOption << ' ' << LLVM_CPPFLAGS << '\n';
      } else if (Arg == "--cflags") {
        OS << ActiveIncludeOption << ' ' << LLVM_CFLAGS << '\n';
      } else if (Arg == "--cxxflags") {
        OS << ActiveIncludeOption << ' ' << LLVM_CXXFLAGS << '\n';
      } else if (Arg == "--ldflags") {
        OS << ((HostTriple.isWindowsMSVCEnvironment()) ? "-LIBPATH:" : "-L")
           << ActiveLibDir << ' ' << LLVM_LDFLAGS << '\n';
      } else if (Arg == "--system-libs") {
        PrintSystemLibs = true;
      } else if (Arg == "--libs") {
        PrintLibs = true;
      } else if (Arg == "--libnames") {
        PrintLibNames = true;
      } else if (Arg == "--libfiles") {
        PrintLibFiles = true;
      } else if (Arg == "--components") {
        /// If there are missing static archives and a dylib was
        /// built, print LLVM_DYLIB_COMPONENTS instead of everything
        /// in the manifest.
        std::vector<std::string> Components;
        for (const auto &AC : AvailableComponents) {
          // Only include non-installed components when in a development tree.
          if (!AC.IsInstalled && !IsInDevelopmentTree)
            continue;

          Components.push_back(AC.Name);
          if (AC.Library && !IsInDevelopmentTree) {
            std::string path(GetComponentLibraryPath(AC.Library, false));
            if (DirSep == "\\") {
              std::replace(path.begin(), path.end(), '/', '\\');
            }
            if (DyLibExists && !sys::fs::exists(path)) {
              Components =
                  GetAllDyLibComponents(IsInDevelopmentTree, true, DirSep);
              llvm::sort(Components);
              break;
            }
          }
        }

        for (unsigned I = 0; I < Components.size(); ++I) {
          if (I) {
            OS << ' ';
          }

          OS << Components[I];
        }
        OS << '\n';
      } else if (Arg == "--targets-built") {
        OS << LLVM_TARGETS_BUILT << '\n';
      } else if (Arg == "--host-target") {
        OS << Triple::normalize(LLVM_DEFAULT_TARGET_TRIPLE) << '\n';
      } else if (Arg == "--build-mode") {
        OS << build_mode << '\n';
      } else if (Arg == "--assertion-mode") {
#if defined(NDEBUG)
        OS << "OFF\n";
#else
        OS << "ON\n";
#endif
      } else if (Arg == "--build-system") {
        OS << LLVM_BUILD_SYSTEM << '\n';
      } else if (Arg == "--has-rtti") {
        OS << (LLVM_HAS_RTTI ? "YES" : "NO") << '\n';
      } else if (Arg == "--shared-mode") {
        PrintSharedMode = true;
      } else if (Arg == "--obj-root") {
        OS << ActivePrefix << '\n';
      } else if (Arg == "--ignore-libllvm") {
        LinkDyLib = false;
        LinkMode = BuiltSharedLibs ? LinkModeShared : LinkModeAuto;
      } else if (Arg == "--link-shared") {
        LinkMode = LinkModeShared;
      } else if (Arg == "--link-static") {
        LinkMode = LinkModeStatic;
      } else if (Arg == "--help") {
        usage(false);
      } else {
        usage();
      }
    } else {
      Components.push_back(Arg);
    }
  }

  if (!HasAnyOption)
    usage();

  if (LinkMode == LinkModeShared && !DyLibExists && !BuiltSharedLibs) {
    WithColor::error(errs(), "llvm-config") << DyLibName << " is missing\n";
    return 1;
  }

  if (PrintLibs || PrintLibNames || PrintLibFiles || PrintSystemLibs ||
      PrintSharedMode) {

    if (PrintSharedMode && BuiltSharedLibs) {
      OS << "shared\n";
      return 0;
    }

    // If no components were specified, default to "all".
    if (Components.empty())
      Components.push_back("all");

    // Construct the list of all the required libraries.
    std::function<std::string(const StringRef &)>
        GetComponentLibraryPathFunction = [&](const StringRef &Name) {
          return GetComponentLibraryPath(Name, LinkMode == LinkModeShared);
        };
    std::vector<std::string> MissingLibs;
    std::vector<std::string> RequiredLibs = ComputeLibsForComponents(
        Components,
        /*IncludeNonInstalled=*/IsInDevelopmentTree, false,
        &GetComponentLibraryPathFunction, &MissingLibs, DirSep);
    if (!MissingLibs.empty()) {
      switch (LinkMode) {
      case LinkModeShared:
        if (LinkDyLib && !BuiltSharedLibs)
          break;
        // Using component shared libraries.
        for (auto &Lib : MissingLibs)
          WithColor::error(errs(), "llvm-config") << "missing: " << Lib << "\n";
        return 1;
      case LinkModeAuto:
        if (DyLibExists) {
          LinkMode = LinkModeShared;
          break;
        }
        WithColor::error(errs(), "llvm-config")
            << "component libraries and shared library\n\n";
        [[fallthrough]];
      case LinkModeStatic:
        for (auto &Lib : MissingLibs)
          WithColor::error(errs(), "llvm-config") << "missing: " << Lib << "\n";
        return 1;
      }
    } else if (LinkMode == LinkModeAuto) {
      LinkMode = LinkModeStatic;
    }

    if (PrintSharedMode) {
      std::unordered_set<std::string> FullDyLibComponents;
      std::vector<std::string> DyLibComponents =
          GetAllDyLibComponents(IsInDevelopmentTree, false, DirSep);

      for (auto &Component : DyLibComponents) {
        FullDyLibComponents.insert(Component);
      }
      DyLibComponents.clear();

      for (auto &Lib : RequiredLibs) {
        if (!FullDyLibComponents.count(Lib)) {
          OS << "static\n";
          return 0;
        }
      }
      FullDyLibComponents.clear();

      if (LinkMode == LinkModeShared) {
        OS << "shared\n";
        return 0;
      } else {
        OS << "static\n";
        return 0;
      }
    }

    if (PrintLibs || PrintLibNames || PrintLibFiles) {

      auto PrintForLib = [&](const StringRef &Lib) {
        const bool Shared = LinkMode == LinkModeShared;
        if (PrintLibNames) {
          OS << GetComponentLibraryFileName(Lib, Shared);
        } else if (PrintLibFiles) {
          OS << GetComponentLibraryPath(Lib, Shared);
        } else if (PrintLibs) {
          // On Windows, output full path to library without parameters.
          // Elsewhere, if this is a typical library name, include it using -l.
          if (HostTriple.isWindowsMSVCEnvironment()) {
            OS << GetComponentLibraryPath(Lib, Shared);
          } else {
            StringRef LibName;
            if (GetComponentLibraryNameSlice(Lib, LibName)) {
              // Extract library name (remove prefix and suffix).
              OS << "-l" << LibName;
            } else {
              // Lib is already a library name without prefix and suffix.
              OS << "-l" << Lib;
            }
          }
        }
      };

      if (LinkMode == LinkModeShared && LinkDyLib) {
        PrintForLib(DyLibName);
      } else {
        for (unsigned i = 0, e = RequiredLibs.size(); i != e; ++i) {
          auto Lib = RequiredLibs[i];
          if (i)
            OS << ' ';

          PrintForLib(Lib);
        }
      }
      OS << '\n';
    }

    // Print SYSTEM_LIBS after --libs.
    // FIXME: Each LLVM component may have its dependent system libs.
    if (PrintSystemLibs) {
      // Output system libraries only if linking against a static
      // library (since the shared library links to all system libs
      // already)
      OS << (LinkMode == LinkModeStatic ? LLVM_SYSTEM_LIBS : "") << '\n';
    }
  } else if (!Components.empty()) {
    WithColor::error(errs(), "llvm-config")
        << "components given, but unused\n\n";
    usage();
  }

  return 0;
}
