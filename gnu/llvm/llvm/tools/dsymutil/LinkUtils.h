//===- tools/dsymutil/LinkUtils.h - Dwarf linker utilities ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_DSYMUTIL_LINKOPTIONS_H
#define LLVM_TOOLS_DSYMUTIL_LINKOPTIONS_H

#include "llvm/ADT/Twine.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/WithColor.h"

#include "llvm/DWARFLinker/Classic/DWARFLinker.h"
#include "llvm/DWARFLinker/Classic/DWARFStreamer.h"
#include <string>

namespace llvm {
namespace dsymutil {

enum class DsymutilAccelTableKind : uint8_t {
  None,
  Apple,   ///< .apple_names, .apple_namespaces, .apple_types, .apple_objc.
  Dwarf,   ///< DWARF v5 .debug_names.
  Default, ///< Dwarf for DWARF5 or later, Apple otherwise.
  Pub,     ///< .debug_pubnames, .debug_pubtypes
};

enum class DsymutilDWARFLinkerType : uint8_t {
  Classic, /// Classic implementation of DWARFLinker.
  Parallel /// Implementation of DWARFLinker heavily using parallel execution.
};

struct LinkOptions {
  /// Verbosity
  bool Verbose = false;

  /// Quiet
  bool Quiet = false;

  /// Statistics
  bool Statistics = false;

  /// Verify the input DWARF.
  bool VerifyInputDWARF = false;

  /// Skip emitting output
  bool NoOutput = false;

  /// Do not unique types according to ODR
  bool NoODR = false;

  /// Update
  bool Update = false;

  /// Do not check swiftmodule timestamp
  bool NoTimestamp = false;

  /// Whether we want a static variable to force us to keep its enclosing
  /// function.
  bool KeepFunctionForStatic = false;

  /// Type of DWARFLinker to use.
  DsymutilDWARFLinkerType DWARFLinkerType = DsymutilDWARFLinkerType::Classic;

  /// Use a 64-bit header when emitting universal binaries.
  bool Fat64 = false;

  /// Number of threads.
  unsigned Threads = 1;

  // Output file type.
  dwarf_linker::DWARFLinkerBase::OutputFileType FileType =
      dwarf_linker::DWARFLinkerBase::OutputFileType::Object;

  /// The accelerator table kind
  DsymutilAccelTableKind TheAccelTableKind;

  /// -oso-prepend-path
  std::string PrependPath;

  /// The -object-prefix-map.
  std::map<std::string, std::string> ObjectPrefixMap;

  /// The Resources directory in the .dSYM bundle.
  std::optional<std::string> ResourceDir;

  /// Virtual File System.
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS =
      vfs::getRealFileSystem();

  /// -build-variant-suffix.
  std::string BuildVariantSuffix;

  /// Paths where to search for the .dSYM files of merged libraries.
  std::vector<std::string> DSYMSearchPaths;

  /// Fields used for linking and placing remarks into the .dSYM bundle.
  /// @{

  /// Number of debug maps processed in total.
  unsigned NumDebugMaps = 0;

  /// -remarks-prepend-path: prepend a path to all the external remark file
  /// paths found in remark metadata.
  std::string RemarksPrependPath;

  /// The output format of the remarks.
  remarks::Format RemarksFormat = remarks::Format::Bitstream;

  /// Whether all remarks should be kept or only remarks with valid debug
  /// locations.
  bool RemarksKeepAll = true;
  /// @}

  LinkOptions() = default;
};

inline void warn(Twine Warning, Twine Context = {}) {
  WithColor::warning() << Warning + "\n";
  if (!Context.isTriviallyEmpty())
    WithColor::note() << Twine("while processing ") + Context + "\n";
}

inline bool error(Twine Error, Twine Context = {}) {
  WithColor::error() << Error + "\n";
  if (!Context.isTriviallyEmpty())
    WithColor::note() << Twine("while processing ") + Context + "\n";
  return false;
}

} // end namespace dsymutil
} // end namespace llvm

#endif // LLVM_TOOLS_DSYMUTIL_LINKOPTIONS_H
