//===-- TraceIntelPTBundleSaver.h ----------------------------*- C++ //-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTBUNDLESAVER_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTBUNDLESAVER_H

#include "TraceIntelPT.h"
#include "TraceIntelPTJSONStructs.h"

namespace lldb_private {
namespace trace_intel_pt {

class TraceIntelPTBundleSaver {
public:
  /// Save the Intel PT trace of a live process to the specified directory,
  /// which will be created if needed. This will also create a file
  /// \a <directory>/trace.json with the description of the trace
  /// bundle, along with others files which contain the actual trace data.
  /// The trace.json file can be used later as input for the "trace load"
  /// command to load the trace in LLDB.
  ///
  /// \param[in] trace_ipt
  ///     The Intel PT trace to be saved to disk.
  ///
  /// \param[in] directory
  ///     The directory where the trace bundle will be created.
  ///
  /// \param[in] compact
  ///     Filter out information irrelevant to the traced processes in the
  ///     context switch and intel pt traces when using per-cpu mode. This
  ///     effectively reduces the size of those traces.
  ///
  /// \return
  ///   A \a FileSpec pointing to the bundle description file, or an \a
  ///   llvm::Error otherwise.
  llvm::Expected<FileSpec> SaveToDisk(TraceIntelPT &trace_ipt,
                                      FileSpec directory, bool compact);
};

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTBUNDLESAVER_H
