//===- MachOConfig.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_MACHO_MACHOCONFIG_H
#define LLVM_OBJCOPY_MACHO_MACHOCONFIG_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <vector>

namespace llvm {
namespace objcopy {

// Mach-O specific configuration for copying/stripping a single file.
struct MachOConfig {
  // Repeated options
  std::vector<StringRef> RPathToAdd;
  std::vector<StringRef> RPathToPrepend;
  DenseMap<StringRef, StringRef> RPathsToUpdate;
  DenseMap<StringRef, StringRef> InstallNamesToUpdate;
  DenseSet<StringRef> RPathsToRemove;

  // install-name-tool's id option
  std::optional<StringRef> SharedLibId;

  // Segments to remove if they are empty
  DenseSet<StringRef> EmptySegmentsToRemove;

  // Boolean options
  bool StripSwiftSymbols = false;
  bool KeepUndefined = false;

  // install-name-tool's --delete_all_rpaths
  bool RemoveAllRpaths = false;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_MACHO_MACHOCONFIG_H
