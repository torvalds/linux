//===- Compilation.h - Compilation Task Data Structure ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_COMPILATION_H
#define LLVM_CLANG_DRIVER_COMPILATION_H

#include "clang/Basic/LLVM.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Util.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include <cassert>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace llvm {
namespace opt {

class DerivedArgList;
class InputArgList;

} // namespace opt
} // namespace llvm

namespace clang {
namespace driver {

class Driver;
class ToolChain;

/// Compilation - A set of tasks to perform for a single driver
/// invocation.
class Compilation {
  /// The driver we were created by.
  const Driver &TheDriver;

  /// The default tool chain.
  const ToolChain &DefaultToolChain;

  /// A mask of all the programming models the host has to support in the
  /// current compilation.
  unsigned ActiveOffloadMask = 0;

  /// Array with the toolchains of offloading host and devices in the order they
  /// were requested by the user. We are preserving that order in case the code
  /// generation needs to derive a programming-model-specific semantic out of
  /// it.
  std::multimap<Action::OffloadKind, const ToolChain *>
      OrderedOffloadingToolchains;

  /// The original (untranslated) input argument list.
  llvm::opt::InputArgList *Args;

  /// The driver translated arguments. Note that toolchains may perform their
  /// own argument translation.
  llvm::opt::DerivedArgList *TranslatedArgs;

  /// The list of actions we've created via MakeAction.  This is not accessible
  /// to consumers; it's here just to manage ownership.
  std::vector<std::unique_ptr<Action>> AllActions;

  /// The list of actions.  This is maintained and modified by consumers, via
  /// getActions().
  ActionList Actions;

  /// The root list of jobs.
  JobList Jobs;

  /// Cache of translated arguments for a particular tool chain, bound
  /// architecture, and device offload kind.
  struct TCArgsKey final {
    const ToolChain *TC = nullptr;
    StringRef BoundArch;
    Action::OffloadKind DeviceOffloadKind = Action::OFK_None;

    TCArgsKey(const ToolChain *TC, StringRef BoundArch,
              Action::OffloadKind DeviceOffloadKind)
        : TC(TC), BoundArch(BoundArch), DeviceOffloadKind(DeviceOffloadKind) {}

    bool operator<(const TCArgsKey &K) const {
      if (TC < K.TC)
        return true;
      else if (TC == K.TC && BoundArch < K.BoundArch)
        return true;
      else if (TC == K.TC && BoundArch == K.BoundArch &&
               DeviceOffloadKind < K.DeviceOffloadKind)
        return true;
      return false;
    }
  };
  std::map<TCArgsKey, llvm::opt::DerivedArgList *> TCArgs;

  /// Temporary files which should be removed on exit.
  llvm::opt::ArgStringList TempFiles;

  /// Result files which should be removed on failure.
  ArgStringMap ResultFiles;

  /// Result files which are generated correctly on failure, and which should
  /// only be removed if we crash.
  ArgStringMap FailureResultFiles;

  /// -ftime-trace result files.
  ArgStringMap TimeTraceFiles;

  /// Optional redirection for stdin, stdout, stderr.
  std::vector<std::optional<StringRef>> Redirects;

  /// Callback called after compilation job has been finished.
  /// Arguments of the callback are the compilation job as an instance of
  /// class Command and the exit status of the corresponding child process.
  std::function<void(const Command &, int)> PostCallback;

  /// Whether we're compiling for diagnostic purposes.
  bool ForDiagnostics = false;

  /// Whether an error during the parsing of the input args.
  bool ContainsError;

  /// Whether to keep temporary files regardless of -save-temps.
  bool ForceKeepTempFiles = false;

public:
  Compilation(const Driver &D, const ToolChain &DefaultToolChain,
              llvm::opt::InputArgList *Args,
              llvm::opt::DerivedArgList *TranslatedArgs, bool ContainsError);
  ~Compilation();

  const Driver &getDriver() const { return TheDriver; }

  const ToolChain &getDefaultToolChain() const { return DefaultToolChain; }

  unsigned isOffloadingHostKind(Action::OffloadKind Kind) const {
    return ActiveOffloadMask & Kind;
  }

  unsigned getActiveOffloadKinds() const { return ActiveOffloadMask; }

  /// Iterator that visits device toolchains of a given kind.
  using const_offload_toolchains_iterator =
      const std::multimap<Action::OffloadKind,
                          const ToolChain *>::const_iterator;
  using const_offload_toolchains_range =
      std::pair<const_offload_toolchains_iterator,
                const_offload_toolchains_iterator>;

  template <Action::OffloadKind Kind>
  const_offload_toolchains_range getOffloadToolChains() const {
    return OrderedOffloadingToolchains.equal_range(Kind);
  }

  const_offload_toolchains_range
  getOffloadToolChains(Action::OffloadKind Kind) const {
    return OrderedOffloadingToolchains.equal_range(Kind);
  }

  /// Return true if an offloading tool chain of a given kind exists.
  template <Action::OffloadKind Kind> bool hasOffloadToolChain() const {
    return OrderedOffloadingToolchains.find(Kind) !=
           OrderedOffloadingToolchains.end();
  }

  /// Return an offload toolchain of the provided kind. Only one is expected to
  /// exist.
  template <Action::OffloadKind Kind>
  const ToolChain *getSingleOffloadToolChain() const {
    auto TCs = getOffloadToolChains<Kind>();

    assert(TCs.first != TCs.second &&
           "No tool chains of the selected kind exist!");
    assert(std::next(TCs.first) == TCs.second &&
           "More than one tool chain of the this kind exist.");
    return TCs.first->second;
  }

  void addOffloadDeviceToolChain(const ToolChain *DeviceToolChain,
                                 Action::OffloadKind OffloadKind) {
    assert(OffloadKind != Action::OFK_Host && OffloadKind != Action::OFK_None &&
           "This is not a device tool chain!");

    // Update the host offload kind to also contain this kind.
    ActiveOffloadMask |= OffloadKind;
    OrderedOffloadingToolchains.insert(
        std::make_pair(OffloadKind, DeviceToolChain));
  }

  const llvm::opt::InputArgList &getInputArgs() const { return *Args; }

  const llvm::opt::DerivedArgList &getArgs() const { return *TranslatedArgs; }

  llvm::opt::DerivedArgList &getArgs() { return *TranslatedArgs; }

  ActionList &getActions() { return Actions; }
  const ActionList &getActions() const { return Actions; }

  /// Creates a new Action owned by this Compilation.
  ///
  /// The new Action is *not* added to the list returned by getActions().
  template <typename T, typename... Args> T *MakeAction(Args &&... Arg) {
    T *RawPtr = new T(std::forward<Args>(Arg)...);
    AllActions.push_back(std::unique_ptr<Action>(RawPtr));
    return RawPtr;
  }

  JobList &getJobs() { return Jobs; }
  const JobList &getJobs() const { return Jobs; }

  void addCommand(std::unique_ptr<Command> C) { Jobs.addJob(std::move(C)); }

  llvm::opt::ArgStringList &getTempFiles() { return TempFiles; }
  const llvm::opt::ArgStringList &getTempFiles() const { return TempFiles; }

  const ArgStringMap &getResultFiles() const { return ResultFiles; }

  const ArgStringMap &getFailureResultFiles() const {
    return FailureResultFiles;
  }

  /// Installs a handler that is executed when a compilation job is finished.
  /// The arguments of the callback specify the compilation job as an instance
  /// of class Command and the exit status of the child process executed that
  /// job.
  void setPostCallback(const std::function<void(const Command &, int)> &CB) {
    PostCallback = CB;
  }

  /// Returns the sysroot path.
  StringRef getSysRoot() const;

  /// getArgsForToolChain - Return the derived argument list for the
  /// tool chain \p TC (or the default tool chain, if TC is not specified).
  /// If a device offloading kind is specified, a translation specific for that
  /// kind is performed, if any.
  ///
  /// \param BoundArch - The bound architecture name, or 0.
  /// \param DeviceOffloadKind - The offload device kind that should be used in
  /// the translation, if any.
  const llvm::opt::DerivedArgList &
  getArgsForToolChain(const ToolChain *TC, StringRef BoundArch,
                      Action::OffloadKind DeviceOffloadKind);

  /// addTempFile - Add a file to remove on exit, and returns its
  /// argument.
  const char *addTempFile(const char *Name) {
    TempFiles.push_back(Name);
    return Name;
  }

  /// addResultFile - Add a file to remove on failure, and returns its
  /// argument.
  const char *addResultFile(const char *Name, const JobAction *JA) {
    ResultFiles[JA] = Name;
    return Name;
  }

  /// addFailureResultFile - Add a file to remove if we crash, and returns its
  /// argument.
  const char *addFailureResultFile(const char *Name, const JobAction *JA) {
    FailureResultFiles[JA] = Name;
    return Name;
  }

  const char *getTimeTraceFile(const JobAction *JA) const {
    return TimeTraceFiles.lookup(JA);
  }
  void addTimeTraceFile(const char *Name, const JobAction *JA) {
    assert(!TimeTraceFiles.contains(JA));
    TimeTraceFiles[JA] = Name;
  }

  /// CleanupFile - Delete a given file.
  ///
  /// \param IssueErrors - Report failures as errors.
  /// \return Whether the file was removed successfully.
  bool CleanupFile(const char *File, bool IssueErrors = false) const;

  /// CleanupFileList - Remove the files in the given list.
  ///
  /// \param IssueErrors - Report failures as errors.
  /// \return Whether all files were removed successfully.
  bool CleanupFileList(const llvm::opt::ArgStringList &Files,
                       bool IssueErrors = false) const;

  /// CleanupFileMap - Remove the files in the given map.
  ///
  /// \param JA - If specified, only delete the files associated with this
  /// JobAction.  Otherwise, delete all files in the map.
  /// \param IssueErrors - Report failures as errors.
  /// \return Whether all files were removed successfully.
  bool CleanupFileMap(const ArgStringMap &Files,
                      const JobAction *JA,
                      bool IssueErrors = false) const;

  /// ExecuteCommand - Execute an actual command.
  ///
  /// \param FailingCommand - For non-zero results, this will be set to the
  /// Command which failed, if any.
  /// \param LogOnly - When true, only tries to log the command, not actually
  /// execute it.
  /// \return The result code of the subprocess.
  int ExecuteCommand(const Command &C, const Command *&FailingCommand,
                     bool LogOnly = false) const;

  /// ExecuteJob - Execute a single job.
  ///
  /// \param FailingCommands - For non-zero results, this will be a vector of
  /// failing commands and their associated result code.
  /// \param LogOnly - When true, only tries to log the command, not actually
  /// execute it.
  void
  ExecuteJobs(const JobList &Jobs,
              SmallVectorImpl<std::pair<int, const Command *>> &FailingCommands,
              bool LogOnly = false) const;

  /// initCompilationForDiagnostics - Remove stale state and suppress output
  /// so compilation can be reexecuted to generate additional diagnostic
  /// information (e.g., preprocessed source(s)).
  void initCompilationForDiagnostics();

  /// Return true if we're compiling for diagnostics.
  bool isForDiagnostics() const { return ForDiagnostics; }

  /// Return whether an error during the parsing of the input args.
  bool containsError() const { return ContainsError; }

  /// Force driver to fail before toolchain is created. This is necessary when
  /// error happens in action builder.
  void setContainsError() { ContainsError = true; }

  /// Redirect - Redirect output of this compilation. Can only be done once.
  ///
  /// \param Redirects - array of optional paths. The array should have a size
  /// of three. The inferior process's stdin(0), stdout(1), and stderr(2) will
  /// be redirected to the corresponding paths, if provided (not std::nullopt).
  void Redirect(ArrayRef<std::optional<StringRef>> Redirects);
};

} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_DRIVER_COMPILATION_H
