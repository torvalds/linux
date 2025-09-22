//===- DependencyScanningService.h - clang-scan-deps service ===-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGSERVICE_H
#define LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGSERVICE_H

#include "clang/Tooling/DependencyScanning/DependencyScanningFilesystem.h"
#include "llvm/ADT/BitmaskEnum.h"

namespace clang {
namespace tooling {
namespace dependencies {

/// The mode in which the dependency scanner will operate to find the
/// dependencies.
enum class ScanningMode {
  /// This mode is used to compute the dependencies by running the preprocessor
  /// over the source files.
  CanonicalPreprocessing,

  /// This mode is used to compute the dependencies by running the preprocessor
  /// with special kind of lexing after scanning header and source files to get
  /// the minimum necessary preprocessor directives for evaluating includes.
  DependencyDirectivesScan,
};

/// The format that is output by the dependency scanner.
enum class ScanningOutputFormat {
  /// This is the Makefile compatible dep format. This will include all of the
  /// deps necessary for an implicit modules build, but won't include any
  /// intermodule dependency information.
  Make,

  /// This outputs the full clang module dependency graph suitable for use for
  /// explicitly building modules.
  Full,

  /// This outputs the dependency graph for standard c++ modules in P1689R5
  /// format.
  P1689,
};

#define DSS_LAST_BITMASK_ENUM(Id)                                              \
  LLVM_MARK_AS_BITMASK_ENUM(Id), All = llvm::NextPowerOf2(Id) - 1

enum class ScanningOptimizations {
  None = 0,

  /// Remove unused header search paths including header maps.
  HeaderSearch = 1,

  /// Remove warnings from system modules.
  SystemWarnings = 2,

  /// Remove unused -ivfsoverlay arguments.
  VFS = 4,

  /// Canonicalize -D and -U options.
  Macros = 8,

  DSS_LAST_BITMASK_ENUM(Macros),
  Default = All
};

#undef DSS_LAST_BITMASK_ENUM

/// The dependency scanning service contains shared configuration and state that
/// is used by the individual dependency scanning workers.
class DependencyScanningService {
public:
  DependencyScanningService(
      ScanningMode Mode, ScanningOutputFormat Format,
      ScanningOptimizations OptimizeArgs = ScanningOptimizations::Default,
      bool EagerLoadModules = false);

  ScanningMode getMode() const { return Mode; }

  ScanningOutputFormat getFormat() const { return Format; }

  ScanningOptimizations getOptimizeArgs() const { return OptimizeArgs; }

  bool shouldEagerLoadModules() const { return EagerLoadModules; }

  DependencyScanningFilesystemSharedCache &getSharedCache() {
    return SharedCache;
  }

private:
  const ScanningMode Mode;
  const ScanningOutputFormat Format;
  /// Whether to optimize the modules' command-line arguments.
  const ScanningOptimizations OptimizeArgs;
  /// Whether to set up command-lines to load PCM files eagerly.
  const bool EagerLoadModules;
  /// The global file system cache.
  DependencyScanningFilesystemSharedCache SharedCache;
};

} // end namespace dependencies
} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGSERVICE_H
