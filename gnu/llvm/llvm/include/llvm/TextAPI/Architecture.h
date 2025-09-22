//===- llvm/TextAPI/Architecture.h - Architecture ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the architecture enum and helper methods.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_ARCHITECTURE_H
#define LLVM_TEXTAPI_ARCHITECTURE_H

#include <cstdint>
#include <utility>

namespace llvm {
class raw_ostream;
class StringRef;
class Triple;

namespace MachO {

/// Defines the architecture slices that are supported by Text-based Stub files.
enum Architecture : uint8_t {
#define ARCHINFO(Arch, Type, SubType, NumBits) AK_##Arch,
#include "llvm/TextAPI/Architecture.def"
#undef ARCHINFO
  AK_unknown, // this has to go last.
};

/// Convert a CPU Type and Subtype pair to an architecture slice.
Architecture getArchitectureFromCpuType(uint32_t CPUType, uint32_t CPUSubType);

/// Convert a name to an architecture slice.
Architecture getArchitectureFromName(StringRef Name);

/// Convert an architecture slice to a string.
StringRef getArchitectureName(Architecture Arch);

/// Convert an architecture slice to a CPU Type and Subtype pair.
std::pair<uint32_t, uint32_t> getCPUTypeFromArchitecture(Architecture Arch);

/// Convert a target to an architecture slice.
Architecture mapToArchitecture(const llvm::Triple &Target);

/// Check if architecture is 64 bit.
bool is64Bit(Architecture);

raw_ostream &operator<<(raw_ostream &OS, Architecture Arch);

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_ARCHITECTURE_H
