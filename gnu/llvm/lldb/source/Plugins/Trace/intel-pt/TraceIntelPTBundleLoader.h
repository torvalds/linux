//===-- TraceIntelPTBundleLoader.h ----------------------------*- C++ //-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTBUNDLELOADER_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTBUNDLELOADER_H

#include "../common/ThreadPostMortemTrace.h"
#include "TraceIntelPTJSONStructs.h"

namespace lldb_private {
namespace trace_intel_pt {

class TraceIntelPT;

class TraceIntelPTBundleLoader {
public:
  /// Helper struct holding the objects created when parsing a process
  struct ParsedProcess {
    lldb::TargetSP target_sp;
    std::vector<lldb::ThreadPostMortemTraceSP> threads;
  };

  /// \param[in] debugger
  ///   The debugger that will own the targets to create.
  ///
  /// \param[in] bundle_description
  ///   The JSON description of a trace bundle that follows the schema of the
  ///   intel pt trace plug-in.
  ///
  /// \param[in] bundle_dir
  ///   The folder where the trace bundle is located.
  TraceIntelPTBundleLoader(Debugger &debugger,
                           const llvm::json::Value &bundle_description,
                           llvm::StringRef bundle_dir)
      : m_debugger(debugger), m_bundle_description(bundle_description),
        m_bundle_dir(bundle_dir) {}

  /// \return
  ///   The JSON schema for the bundle description.
  static llvm::StringRef GetSchema();

  /// Parse the trace bundle description and create the corresponding \a
  /// Target objects. In case of an error, no targets are created.
  ///
  /// \return
  ///   A \a lldb::TraceSP instance created according to the trace bundle
  ///   information. In case of errors, return a null pointer.
  llvm::Expected<lldb::TraceSP> Load();

private:
  /// Resolve non-absolute paths relative to the bundle folder.
  FileSpec NormalizePath(const std::string &path);

  /// Create a post-mortem thread associated with the given \p process
  /// using the definition from \p thread.
  lldb::ThreadPostMortemTraceSP ParseThread(Process &process,
                                            const JSONThread &thread);

  /// Given a bundle description and a list of fully parsed processes,
  /// create an actual Trace instance that "traces" these processes.
  llvm::Expected<lldb::TraceSP>
  CreateTraceIntelPTInstance(JSONTraceBundleDescription &bundle_description,
                             std::vector<ParsedProcess> &parsed_processes);

  /// Create an empty Process object with given pid and target.
  llvm::Expected<ParsedProcess> CreateEmptyProcess(lldb::pid_t pid,
                                                   llvm::StringRef triple);

  /// Create the corresponding Threads and Process objects given the JSON
  /// process definition.
  ///
  /// \param[in] process
  ///   The JSON process definition
  llvm::Expected<ParsedProcess> ParseProcess(const JSONProcess &process);

  /// Create a module associated with the given \p target using the definition
  /// from \p module.
  llvm::Error ParseModule(Target &target, const JSONModule &module);

  /// Create a kernel process and cpu threads given the JSON kernel definition.
  llvm::Expected<ParsedProcess>
  ParseKernel(const JSONTraceBundleDescription &bundle_description);

  /// Create a user-friendly error message upon a JSON-parsing failure using the
  /// \a json::ObjectMapper functionality.
  ///
  /// \param[in] root
  ///   The \a llvm::json::Path::Root used to parse the JSON \a value.
  ///
  /// \param[in] value
  ///   The json value that failed to parse.
  ///
  /// \return
  ///   An \a llvm::Error containing the user-friendly error message.
  llvm::Error CreateJSONError(llvm::json::Path::Root &root,
                              const llvm::json::Value &value);

  /// Create the corresponding Process, Thread and Module objects given this
  /// bundle description.
  llvm::Expected<std::vector<ParsedProcess>>
  LoadBundle(const JSONTraceBundleDescription &bundle_description);

  /// When applicable, augment the list of threads in the trace bundle by
  /// inspecting the context switch trace. This only applies for threads of
  /// processes already specified in this bundle description.
  ///
  /// \return
  ///   An \a llvm::Error in case if failures, or \a llvm::Error::success
  ///   otherwise.
  llvm::Error AugmentThreadsFromContextSwitches(
      JSONTraceBundleDescription &bundle_description);

  /// Modifiy the bundle description by normalizing all the paths relative to
  /// the session file directory.
  void NormalizeAllPaths(JSONTraceBundleDescription &bundle_description);

  Debugger &m_debugger;
  const llvm::json::Value &m_bundle_description;
  const std::string m_bundle_dir;
};

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTBUNDLELOADER_H
