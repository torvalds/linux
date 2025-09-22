//===-- SBSaveCoreOptions.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBSAVECOREOPTIONS_H
#define LLDB_API_SBSAVECOREOPTIONS_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBSaveCoreOptions {
public:
  SBSaveCoreOptions();
  SBSaveCoreOptions(const lldb::SBSaveCoreOptions &rhs);
  ~SBSaveCoreOptions();

  const SBSaveCoreOptions &operator=(const lldb::SBSaveCoreOptions &rhs);

  /// Set the plugin name. Supplying null or empty string will reset
  /// the option.
  ///
  /// \param plugin Name of the object file plugin.
  SBError SetPluginName(const char *plugin);

  /// Get the Core dump plugin name, if set.
  ///
  /// \return The name of the plugin, or null if not set.
  const char *GetPluginName() const;

  /// Set the Core dump style.
  ///
  /// \param style The style of the core dump.
  void SetStyle(lldb::SaveCoreStyle style);

  /// Get the Core dump style, if set.
  ///
  /// \return The core dump style, or undefined if not set.
  lldb::SaveCoreStyle GetStyle() const;

  /// Set the output file path
  ///
  /// \param output_file a
  /// \class SBFileSpec object that describes the output file.
  void SetOutputFile(SBFileSpec output_file);

  /// Get the output file spec
  ///
  /// \return The output file spec.
  SBFileSpec GetOutputFile() const;

  /// Reset all options.
  void Clear();

protected:
  friend class SBProcess;
  lldb_private::SaveCoreOptions &ref() const;

private:
  std::unique_ptr<lldb_private::SaveCoreOptions> m_opaque_up;
}; // SBSaveCoreOptions
} // namespace lldb

#endif // LLDB_API_SBSAVECOREOPTIONS_H
