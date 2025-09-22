//===-- CppModuleConfiguration.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPMODULECONFIGURATION_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPMODULECONFIGURATION_H

#include <lldb/Utility/FileSpecList.h>
#include <llvm/Support/Regex.h>

namespace lldb_private {

/// A Clang configuration when importing C++ modules.
///
/// This class computes a list of include paths and module names that can be
/// imported given a list of source files. Currently only used when importing
/// the 'std' module and its dependencies.
class CppModuleConfiguration {
  /// Utility class for a path that can only be set once.
  class SetOncePath {
    std::string m_path;
    bool m_valid = false;
    /// True iff this path hasn't been set yet.
    bool m_first = true;

  public:
    /// Try setting the path. Returns true if the path was set and false if
    /// the path was already set.
    [[nodiscard]] bool TrySet(llvm::StringRef path);
    /// Return the path if there is one.
    llvm::StringRef Get() const {
      assert(m_valid && "Called Get() on an invalid SetOncePath?");
      return m_path;
    }
    /// Returns true iff this path was set exactly once so far.
    bool Valid() const { return m_valid; }
  };

  /// If valid, the include path used for the std module.
  SetOncePath m_std_inc;
  /// If valid, the per-target include path used for the std module.
  /// This is an optional path only required on some systems.
  SetOncePath m_std_target_inc;
  /// If valid, the include path to the C library (e.g. /usr/include).
  SetOncePath m_c_inc;
  /// If valid, the include path to target-specific C library files
  /// (e.g. /usr/include/x86_64-linux-gnu).
  /// This is an optional path only required on some systems.
  SetOncePath m_c_target_inc;
  /// The Clang resource include path for this configuration.
  std::string m_resource_inc;

  std::vector<std::string> m_include_dirs;
  std::vector<std::string> m_imported_modules;

  /// Analyze a given source file to build the current configuration.
  /// Returns false iff there was a fatal error that makes analyzing any
  /// further files pointless as the configuration is now invalid.
  bool analyzeFile(const FileSpec &f, const llvm::Triple &triple);

public:
  /// Creates a configuration by analyzing the given list of used source files.
  /// The triple (if valid) is used to search for target-specific include paths.
  explicit CppModuleConfiguration(const FileSpecList &support_files,
                                  const llvm::Triple &triple);
  /// Creates an empty and invalid configuration.
  CppModuleConfiguration() = default;

  /// Returns true iff this is a valid configuration that can be used to
  /// load and compile modules.
  bool hasValidConfig();

  /// Returns a list of include directories that should be used when using this
  /// configuration (e.g. {"/usr/include", "/usr/include/c++/v1"}).
  llvm::ArrayRef<std::string> GetIncludeDirs() const { return m_include_dirs; }

  /// Returns a list of (top level) modules that should be imported when using
  /// this configuration (e.g. {"std"}).
  llvm::ArrayRef<std::string> GetImportedModules() const {
    return m_imported_modules;
  }
};

} // namespace lldb_private

#endif
