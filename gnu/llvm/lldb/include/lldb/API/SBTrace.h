//===-- SBTrace.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTRACE_H
#define LLDB_API_SBTRACE_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBTraceCursor.h"

namespace lldb {

class LLDB_API SBTrace {
public:
  /// Default constructor for an invalid Trace object.
  SBTrace();

  /// See SBDebugger::LoadTraceFromFile.
  static SBTrace LoadTraceFromFile(SBError &error, SBDebugger &debugger,
                                   const SBFileSpec &trace_description_file);

  /// Get a \a TraceCursor for the given thread's trace.
  ///
  /// \param[out] error
  ///   This will be set with an error in case of failures.
  //
  /// \param[in] thread
  ///   The thread to get a \a TraceCursor for.
  //
  /// \return
  ///     A \a SBTraceCursor. If the thread is not traced or its trace
  ///     information failed to load, an invalid \a SBTraceCursor is returned
  ///     and the \p error parameter is set.
  SBTraceCursor CreateNewCursor(SBError &error, SBThread &thread);

  /// Save the trace to the specified directory, which will be created if
  /// needed. This will also create a file \a <directory>/trace.json with the
  /// main properties of the trace session, along with others files which
  /// contain the actual trace data. The trace.json file can be used later as
  /// input for the "trace load" command to load the trace in LLDB, or for the
  /// method \a SBDebugger.LoadTraceFromFile().
  ///
  /// \param[out] error
  ///   This will be set with an error in case of failures.
  ///
  /// \param[in] bundle_dir
  ///   The directory where the trace files will be saved.
  ///
  /// \param[in] compact
  ///   Try not to save to disk information irrelevant to the traced processes.
  ///   Each trace plug-in implements this in a different fashion.
  ///
  /// \return
  ///   A \a SBFileSpec pointing to the bundle description file.
  SBFileSpec SaveToDisk(SBError &error, const SBFileSpec &bundle_dir,
                        bool compact = false);

  /// \return
  ///     A description of the parameters to use for the \a SBTrace::Start
  ///     method, or \b null if the object is invalid.
  const char *GetStartConfigurationHelp();

  /// Start tracing all current and future threads in a live process using a
  /// provided configuration. This is referred as "process tracing" in the
  /// documentation.
  ///
  /// This is equivalent to the command "process trace start".
  ///
  /// This operation fails if it is invoked twice in a row without
  /// first stopping the process trace with \a SBTrace::Stop().
  ///
  /// If a thread is already being traced explicitly, e.g. with \a
  /// SBTrace::Start(const SBThread &thread, const SBStructuredData
  /// &configuration), it is left unaffected by this operation.
  ///
  /// \param[in] configuration
  ///     Dictionary object with custom fields for the corresponding trace
  ///     technology.
  ///
  ///     Full details for the trace start parameters that can be set can be
  ///     retrieved by calling \a SBTrace::GetStartConfigurationHelp().
  ///
  /// \return
  ///     An error explaining any failures.
  SBError Start(const SBStructuredData &configuration);

  /// Start tracing a specific thread in a live process using a provided
  /// configuration. This is referred as "thread tracing" in the documentation.
  ///
  /// This is equivalent to the command "thread trace start".
  ///
  /// If the thread is already being traced by a "process tracing" operation,
  /// e.g. with \a SBTrace::Start(const SBStructuredData &configuration), this
  /// operation fails.
  ///
  /// \param[in] configuration
  ///     Dictionary object with custom fields for the corresponding trace
  ///     technology.
  ///
  ///     Full details for the trace start parameters that can be set can be
  ///     retrieved by calling \a SBTrace::GetStartConfigurationHelp().
  ///
  /// \return
  ///     An error explaining any failures.
  SBError Start(const SBThread &thread, const SBStructuredData &configuration);

  /// Stop tracing all threads in a live process.
  ///
  /// If a "process tracing" operation is active, e.g. \a SBTrace::Start(const
  /// SBStructuredData &configuration), this effectively prevents future threads
  /// from being traced.
  ///
  /// This is equivalent to the command "process trace stop".
  ///
  /// \return
  ///     An error explaining any failures.
  SBError Stop();

  /// Stop tracing a specific thread in a live process regardless of whether the
  /// thread was traced explicitly or as part of a "process tracing" operation.
  ///
  /// This is equivalent to the command "thread trace stop".
  ///
  /// \return
  ///     An error explaining any failures.
  SBError Stop(const SBThread &thread);

  explicit operator bool() const;

  bool IsValid();

protected:
  friend class SBTarget;

  SBTrace(const lldb::TraceSP &trace_sp);

  lldb::TraceSP m_opaque_sp;
  /// deprecated
  lldb::ProcessWP m_opaque_wp;
};
} // namespace lldb

#endif // LLDB_API_SBTRACE_H
