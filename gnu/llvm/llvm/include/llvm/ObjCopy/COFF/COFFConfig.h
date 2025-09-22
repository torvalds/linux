//===- COFFConfig.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_COFF_COFFCONFIG_H
#define LLVM_OBJCOPY_COFF_COFFCONFIG_H

#include <optional>

namespace llvm {
namespace objcopy {

// Coff specific configuration for copying/stripping a single file.
struct COFFConfig {
  std::optional<unsigned> Subsystem;
  std::optional<unsigned> MajorSubsystemVersion;
  std::optional<unsigned> MinorSubsystemVersion;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_COFF_COFFCONFIG_H
