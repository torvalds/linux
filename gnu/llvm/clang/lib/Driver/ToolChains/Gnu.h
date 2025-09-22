//===--- Gnu.h - Gnu Tool and ToolChain Implementations ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_GNU_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_GNU_H

#include "Cuda.h"
#include "LazyDetector.h"
#include "ROCm.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include <set>

namespace clang {
namespace driver {

struct DetectedMultilibs {
  /// The set of multilibs that the detected installation supports.
  MultilibSet Multilibs;

  /// The multilibs appropriate for the given flags.
  llvm::SmallVector<Multilib> SelectedMultilibs;

  /// On Biarch systems, this corresponds to the default multilib when
  /// targeting the non-default multilib. Otherwise, it is empty.
  std::optional<Multilib> BiarchSibling;
};

bool findMIPSMultilibs(const Driver &D, const llvm::Triple &TargetTriple,
                       StringRef Path, const llvm::opt::ArgList &Args,
                       DetectedMultilibs &Result);

namespace tools {

/// Directly call GNU Binutils' assembler and linker.
namespace gnutools {
class LLVM_LIBRARY_VISIBILITY Assembler : public Tool {
public:
  Assembler(const ToolChain &TC) : Tool("GNU::Assembler", "assembler", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("GNU::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY StaticLibTool : public Tool {
public:
  StaticLibTool(const ToolChain &TC)
      : Tool("GNU::StaticLibTool", "static-lib-linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace gnutools

/// gcc - Generic GCC tool implementations.
namespace gcc {
class LLVM_LIBRARY_VISIBILITY Common : public Tool {
public:
  Common(const char *Name, const char *ShortName, const ToolChain &TC)
      : Tool(Name, ShortName, TC) {}

  // A gcc tool has an "integrated" assembler that it will call to produce an
  // object. Let it use that assembler so that we don't have to deal with
  // assembly syntax incompatibilities.
  bool hasIntegratedAssembler() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;

  /// RenderExtraToolArgs - Render any arguments necessary to force
  /// the particular tool mode.
  virtual void RenderExtraToolArgs(const JobAction &JA,
                                   llvm::opt::ArgStringList &CmdArgs) const = 0;
};

class LLVM_LIBRARY_VISIBILITY Preprocessor : public Common {
public:
  Preprocessor(const ToolChain &TC)
      : Common("gcc::Preprocessor", "gcc preprocessor", TC) {}

  bool hasGoodDiagnostics() const override { return true; }
  bool hasIntegratedCPP() const override { return false; }

  void RenderExtraToolArgs(const JobAction &JA,
                           llvm::opt::ArgStringList &CmdArgs) const override;
};

class LLVM_LIBRARY_VISIBILITY Compiler : public Common {
public:
  Compiler(const ToolChain &TC) : Common("gcc::Compiler", "gcc frontend", TC) {}

  bool hasGoodDiagnostics() const override { return true; }
  bool hasIntegratedCPP() const override { return true; }

  void RenderExtraToolArgs(const JobAction &JA,
                           llvm::opt::ArgStringList &CmdArgs) const override;
};

class LLVM_LIBRARY_VISIBILITY Linker : public Common {
public:
  Linker(const ToolChain &TC) : Common("gcc::Linker", "linker (via gcc)", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void RenderExtraToolArgs(const JobAction &JA,
                           llvm::opt::ArgStringList &CmdArgs) const override;
};
} // end namespace gcc
} // end namespace tools

namespace toolchains {

/// Generic_GCC - A tool chain using the 'gcc' command to perform
/// all subcommands; this relies on gcc translating the majority of
/// command line options.
class LLVM_LIBRARY_VISIBILITY Generic_GCC : public ToolChain {
public:
  /// Struct to store and manipulate GCC versions.
  ///
  /// We rely on assumptions about the form and structure of GCC version
  /// numbers: they consist of at most three '.'-separated components, and each
  /// component is a non-negative integer except for the last component. For
  /// the last component we are very flexible in order to tolerate release
  /// candidates or 'x' wildcards.
  ///
  /// Note that the ordering established among GCCVersions is based on the
  /// preferred version string to use. For example we prefer versions without
  /// a hard-coded patch number to those with a hard coded patch number.
  ///
  /// Currently this doesn't provide any logic for textual suffixes to patches
  /// in the way that (for example) Debian's version format does. If that ever
  /// becomes necessary, it can be added.
  struct GCCVersion {
    /// The unparsed text of the version.
    std::string Text;

    /// The parsed major, minor, and patch numbers.
    int Major, Minor, Patch;

    /// The text of the parsed major, and major+minor versions.
    std::string MajorStr, MinorStr;

    /// Any textual suffix on the patch number.
    std::string PatchSuffix;

    static GCCVersion Parse(StringRef VersionText);
    bool isOlderThan(int RHSMajor, int RHSMinor, int RHSPatch,
                     StringRef RHSPatchSuffix = StringRef()) const;
    bool operator<(const GCCVersion &RHS) const {
      return isOlderThan(RHS.Major, RHS.Minor, RHS.Patch, RHS.PatchSuffix);
    }
    bool operator>(const GCCVersion &RHS) const { return RHS < *this; }
    bool operator<=(const GCCVersion &RHS) const { return !(*this > RHS); }
    bool operator>=(const GCCVersion &RHS) const { return !(*this < RHS); }
  };

  /// This is a class to find a viable GCC installation for Clang to
  /// use.
  ///
  /// This class tries to find a GCC installation on the system, and report
  /// information about it. It starts from the host information provided to the
  /// Driver, and has logic for fuzzing that where appropriate.
  class GCCInstallationDetector {
    bool IsValid;
    llvm::Triple GCCTriple;
    const Driver &D;

    // FIXME: These might be better as path objects.
    std::string GCCInstallPath;
    std::string GCCParentLibPath;

    /// The primary multilib appropriate for the given flags.
    Multilib SelectedMultilib;
    /// On Biarch systems, this corresponds to the default multilib when
    /// targeting the non-default multilib. Otherwise, it is empty.
    std::optional<Multilib> BiarchSibling;

    GCCVersion Version;

    // We retain the list of install paths that were considered and rejected in
    // order to print out detailed information in verbose mode.
    std::set<std::string> CandidateGCCInstallPaths;

    /// The set of multilibs that the detected installation supports.
    MultilibSet Multilibs;

    // Gentoo-specific toolchain configurations are stored here.
    const std::string GentooConfigDir = "/etc/env.d/gcc";

  public:
    explicit GCCInstallationDetector(const Driver &D) : IsValid(false), D(D) {}
    void init(const llvm::Triple &TargetTriple, const llvm::opt::ArgList &Args);

    /// Check whether we detected a valid GCC install.
    bool isValid() const { return IsValid; }

    /// Get the GCC triple for the detected install.
    const llvm::Triple &getTriple() const { return GCCTriple; }

    /// Get the detected GCC installation path.
    StringRef getInstallPath() const { return GCCInstallPath; }

    /// Get the detected GCC parent lib path.
    StringRef getParentLibPath() const { return GCCParentLibPath; }

    /// Get the detected Multilib
    const Multilib &getMultilib() const { return SelectedMultilib; }

    /// Get the whole MultilibSet
    const MultilibSet &getMultilibs() const { return Multilibs; }

    /// Get the biarch sibling multilib (if it exists).
    /// \return true iff such a sibling exists
    bool getBiarchSibling(Multilib &M) const;

    /// Get the detected GCC version string.
    const GCCVersion &getVersion() const { return Version; }

    /// Print information about the detected GCC installation.
    void print(raw_ostream &OS) const;

  private:
    static void
    CollectLibDirsAndTriples(const llvm::Triple &TargetTriple,
                             const llvm::Triple &BiarchTriple,
                             SmallVectorImpl<StringRef> &LibDirs,
                             SmallVectorImpl<StringRef> &TripleAliases,
                             SmallVectorImpl<StringRef> &BiarchLibDirs,
                             SmallVectorImpl<StringRef> &BiarchTripleAliases);

    void AddDefaultGCCPrefixes(const llvm::Triple &TargetTriple,
                               SmallVectorImpl<std::string> &Prefixes,
                               StringRef SysRoot);

    bool ScanGCCForMultilibs(const llvm::Triple &TargetTriple,
                             const llvm::opt::ArgList &Args,
                             StringRef Path,
                             bool NeedsBiarchSuffix = false);

    void ScanLibDirForGCCTriple(const llvm::Triple &TargetArch,
                                const llvm::opt::ArgList &Args,
                                const std::string &LibDir,
                                StringRef CandidateTriple,
                                bool NeedsBiarchSuffix, bool GCCDirExists,
                                bool GCCCrossDirExists);

    bool ScanGentooConfigs(const llvm::Triple &TargetTriple,
                           const llvm::opt::ArgList &Args,
                           const SmallVectorImpl<StringRef> &CandidateTriples,
                           const SmallVectorImpl<StringRef> &BiarchTriples);

    bool ScanGentooGccConfig(const llvm::Triple &TargetTriple,
                             const llvm::opt::ArgList &Args,
                             StringRef CandidateTriple,
                             bool NeedsBiarchSuffix = false);
  };

protected:
  GCCInstallationDetector GCCInstallation;
  LazyDetector<CudaInstallationDetector> CudaInstallation;
  LazyDetector<RocmInstallationDetector> RocmInstallation;

public:
  Generic_GCC(const Driver &D, const llvm::Triple &Triple,
              const llvm::opt::ArgList &Args);
  ~Generic_GCC() override;

  void printVerboseInfo(raw_ostream &OS) const override;

  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override;
  bool isPICDefault() const override;
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override;
  bool isPICDefaultForced() const override;
  bool IsIntegratedAssemblerDefault() const override;
  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const override;

protected:
  Tool *getTool(Action::ActionClass AC) const override;
  Tool *buildAssembler() const override;
  Tool *buildLinker() const override;

  /// \name ToolChain Implementation Helper Functions
  /// @{

  /// Check whether the target triple's architecture is 64-bits.
  bool isTarget64Bit() const { return getTriple().isArch64Bit(); }

  /// Check whether the target triple's architecture is 32-bits.
  bool isTarget32Bit() const { return getTriple().isArch32Bit(); }

  void PushPPaths(ToolChain::path_list &PPaths);
  void AddMultilibPaths(const Driver &D, const std::string &SysRoot,
                        const std::string &OSLibDir,
                        const std::string &MultiarchTriple,
                        path_list &Paths);
  void AddMultiarchPaths(const Driver &D, const std::string &SysRoot,
                         const std::string &OSLibDir, path_list &Paths);
  void AddMultilibIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                              llvm::opt::ArgStringList &CC1Args) const;

  // FIXME: This should be final, but the CrossWindows toolchain does weird
  // things that can't be easily generalized.
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &CC1Args) const override;

  virtual void
  addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                        llvm::opt::ArgStringList &CC1Args) const;
  virtual void
  addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                           llvm::opt::ArgStringList &CC1Args) const;

  bool addGCCLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                   llvm::opt::ArgStringList &CC1Args,
                                   StringRef DebianMultiarch) const;

  bool addLibStdCXXIncludePaths(Twine IncludeDir, StringRef Triple,
                                Twine IncludeSuffix,
                                const llvm::opt::ArgList &DriverArgs,
                                llvm::opt::ArgStringList &CC1Args,
                                bool DetectDebian = false) const;

  /// @}

private:
  mutable std::unique_ptr<tools::gcc::Preprocessor> Preprocess;
  mutable std::unique_ptr<tools::gcc::Compiler> Compile;
};

class LLVM_LIBRARY_VISIBILITY Generic_ELF : public Generic_GCC {
  virtual void anchor();

public:
  Generic_ELF(const Driver &D, const llvm::Triple &Triple,
              const llvm::opt::ArgList &Args)
      : Generic_GCC(D, Triple, Args) {}

  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args,
                             Action::OffloadKind DeviceOffloadKind) const override;

  virtual std::string getDynamicLinker(const llvm::opt::ArgList &Args) const {
    return {};
  }

  virtual void addExtraOpts(llvm::opt::ArgStringList &CmdArgs) const {}
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_GNU_H
