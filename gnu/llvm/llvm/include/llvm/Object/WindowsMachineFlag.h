//===- WindowsMachineFlag.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Functions for implementing the /machine: flag.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WINDOWSMACHINEFLAG_H
#define LLVM_OBJECT_WINDOWSMACHINEFLAG_H

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class StringRef;
namespace COFF {
enum MachineTypes : unsigned;
}

// Returns a user-readable string for ARMNT, ARM64, AMD64, I386.
// Other MachineTypes values must not be passed in.
StringRef machineToStr(COFF::MachineTypes MT);

// Maps /machine: arguments to a MachineTypes value.
// Only returns ARMNT, ARM64, AMD64, I386, or IMAGE_FILE_MACHINE_UNKNOWN.
COFF::MachineTypes getMachineType(StringRef S);

template <typename T> Triple::ArchType getMachineArchType(T machine) {
  switch (machine) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return llvm::Triple::ArchType::x86;
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return llvm::Triple::ArchType::x86_64;
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return llvm::Triple::ArchType::thumb;
  case COFF::IMAGE_FILE_MACHINE_ARM64:
  case COFF::IMAGE_FILE_MACHINE_ARM64EC:
  case COFF::IMAGE_FILE_MACHINE_ARM64X:
    return llvm::Triple::ArchType::aarch64;
  default:
    return llvm::Triple::ArchType::UnknownArch;
  }
}

} // namespace llvm

#endif
