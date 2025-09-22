//===- TextStubCommon.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines common Text Stub YAML mappings.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_TEXT_STUB_COMMON_H
#define LLVM_TEXTAPI_TEXT_STUB_COMMON_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/Target.h"

using UUID = std::pair<llvm::MachO::Target, std::string>;

// clang-format off
enum TBDFlags : unsigned {
  None                         = 0U,
  FlatNamespace                = 1U << 0,
  NotApplicationExtensionSafe  = 1U << 1,
  InstallAPI                   = 1U << 2,
  SimulatorSupport             = 1U << 3,
  OSLibNotForSharedCache       = 1U << 4,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/OSLibNotForSharedCache),
};
// clang-format on

LLVM_YAML_STRONG_TYPEDEF(llvm::StringRef, FlowStringRef)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, SwiftVersion)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(UUID)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(FlowStringRef)

namespace llvm {

namespace MachO {
class ArchitectureSet;
class PackedVersion;

Expected<std::unique_ptr<InterfaceFile>>
getInterfaceFileFromJSON(StringRef JSON);

Error serializeInterfaceFileToJSON(raw_ostream &OS, const InterfaceFile &File,
                                   const FileType FileKind, bool Compact);
} // namespace MachO

namespace yaml {

template <> struct ScalarTraits<FlowStringRef> {
  static void output(const FlowStringRef &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, FlowStringRef &);
  static QuotingType mustQuote(StringRef);
};

template <> struct ScalarEnumerationTraits<MachO::ObjCConstraintType> {
  static void enumeration(IO &, MachO::ObjCConstraintType &);
};

template <> struct ScalarTraits<MachO::PlatformSet> {
  static void output(const MachO::PlatformSet &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, MachO::PlatformSet &);
  static QuotingType mustQuote(StringRef);
};

template <> struct ScalarBitSetTraits<MachO::ArchitectureSet> {
  static void bitset(IO &, MachO::ArchitectureSet &);
};

template <> struct ScalarTraits<MachO::Architecture> {
  static void output(const MachO::Architecture &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, MachO::Architecture &);
  static QuotingType mustQuote(StringRef);
};

template <> struct ScalarTraits<MachO::PackedVersion> {
  static void output(const MachO::PackedVersion &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, MachO::PackedVersion &);
  static QuotingType mustQuote(StringRef);
};

template <> struct ScalarTraits<SwiftVersion> {
  static void output(const SwiftVersion &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, SwiftVersion &);
  static QuotingType mustQuote(StringRef);
};

// UUIDs are no longer respected but kept in the YAML parser
// to keep reading in older TBDs.
template <> struct ScalarTraits<UUID> {
  static void output(const UUID &, void *, raw_ostream &);
  static StringRef input(StringRef, void *, UUID &);
  static QuotingType mustQuote(StringRef);
};

} // end namespace yaml.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_TEXT_STUB_COMMON_H
