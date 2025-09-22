//===-- TraceExporter.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_TRACE_EXPORTER_H
#define LLDB_TARGET_TRACE_EXPORTER_H

#include "lldb/Core/PluginInterface.h"
#include "lldb/lldb-forward.h"
#include "llvm/Support/Error.h"

namespace lldb_private {

/// \class TraceExporter TraceExporter.h "lldb/Target/TraceExporter.h"
/// A plug-in interface definition class for trace exporters.
///
/// Trace exporter plug-ins operate on traces, converting the trace data
/// provided by an \a lldb_private::TraceCursor into a different format that can
/// be digested by other tools, e.g. Chrome Trace Event Profiler.
///
/// Trace exporters are supposed to operate on an architecture-agnostic fashion,
/// as a TraceCursor, which feeds the data, hides the actual trace technology
/// being used.
class TraceExporter : public PluginInterface {
public:
  /// Create an instance of a trace exporter plugin given its name.
  ///
  /// \param[in] plugin_Name
  ///     Plug-in name to search.
  ///
  /// \return
  ///     A \a TraceExporterUP instance, or an \a llvm::Error if the plug-in
  ///     name doesn't match any registered plug-ins.
  static llvm::Expected<lldb::TraceExporterUP>
  FindPlugin(llvm::StringRef plugin_name);
};

} // namespace lldb_private

#endif // LLDB_TARGET_TRACE_EXPORTER_H
