//===- TextStubCommon.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements common Text Stub YAML mappings.
//
//===----------------------------------------------------------------------===//

#include "TextStubCommon.h"
#include "TextAPIContext.h"
#include "llvm/ADT/StringSwitch.h"

using namespace llvm::MachO;

namespace llvm {
namespace yaml {

void ScalarTraits<FlowStringRef>::output(const FlowStringRef &Value, void *Ctx,
                                         raw_ostream &OS) {
  ScalarTraits<StringRef>::output(Value, Ctx, OS);
}
StringRef ScalarTraits<FlowStringRef>::input(StringRef Value, void *Ctx,
                                             FlowStringRef &Out) {
  return ScalarTraits<StringRef>::input(Value, Ctx, Out.value);
}
QuotingType ScalarTraits<FlowStringRef>::mustQuote(StringRef Name) {
  return ScalarTraits<StringRef>::mustQuote(Name);
}

void ScalarEnumerationTraits<ObjCConstraintType>::enumeration(
    IO &IO, ObjCConstraintType &Constraint) {
  IO.enumCase(Constraint, "none", ObjCConstraintType::None);
  IO.enumCase(Constraint, "retain_release", ObjCConstraintType::Retain_Release);
  IO.enumCase(Constraint, "retain_release_for_simulator",
              ObjCConstraintType::Retain_Release_For_Simulator);
  IO.enumCase(Constraint, "retain_release_or_gc",
              ObjCConstraintType::Retain_Release_Or_GC);
  IO.enumCase(Constraint, "gc", ObjCConstraintType::GC);
}

void ScalarTraits<PlatformSet>::output(const PlatformSet &Values, void *IO,
                                       raw_ostream &OS) {

  const auto *Ctx = reinterpret_cast<TextAPIContext *>(IO);
  assert((!Ctx || Ctx->FileKind != FileType::Invalid) &&
         "File type is not set in context");

  if (Ctx && Ctx->FileKind == TBD_V3 && Values.count(PLATFORM_MACOS) &&
      Values.count(PLATFORM_MACCATALYST)) {
    OS << "zippered";
    return;
  }

  assert(Values.size() == 1U);
  switch (*Values.begin()) {
  default:
    llvm_unreachable("unexpected platform");
    break;
  case PLATFORM_MACOS:
    OS << "macosx";
    break;
  case PLATFORM_IOSSIMULATOR:
    [[fallthrough]];
  case PLATFORM_IOS:
    OS << "ios";
    break;
  case PLATFORM_WATCHOSSIMULATOR:
    [[fallthrough]];
  case PLATFORM_WATCHOS:
    OS << "watchos";
    break;
  case PLATFORM_TVOSSIMULATOR:
    [[fallthrough]];
  case PLATFORM_TVOS:
    OS << "tvos";
    break;
  case PLATFORM_BRIDGEOS:
    OS << "bridgeos";
    break;
  case PLATFORM_MACCATALYST:
    OS << "maccatalyst";
    break;
  case PLATFORM_DRIVERKIT:
    OS << "driverkit";
    break;
  }
}

StringRef ScalarTraits<PlatformSet>::input(StringRef Scalar, void *IO,
                                           PlatformSet &Values) {
  const auto *Ctx = reinterpret_cast<TextAPIContext *>(IO);
  assert((!Ctx || Ctx->FileKind != FileType::Invalid) &&
         "File type is not set in context");

  if (Scalar == "zippered") {
    if (Ctx && Ctx->FileKind == FileType::TBD_V3) {
      Values.insert(PLATFORM_MACOS);
      Values.insert(PLATFORM_MACCATALYST);
      return {};
    }
    return "invalid platform";
  }

  auto Platform = StringSwitch<PlatformType>(Scalar)
                      .Case("macosx", PLATFORM_MACOS)
                      .Case("ios", PLATFORM_IOS)
                      .Case("watchos", PLATFORM_WATCHOS)
                      .Case("tvos", PLATFORM_TVOS)
                      .Case("bridgeos", PLATFORM_BRIDGEOS)
                      .Case("iosmac", PLATFORM_MACCATALYST)
                      .Case("maccatalyst", PLATFORM_MACCATALYST)
                      .Case("driverkit", PLATFORM_DRIVERKIT)
                      .Default(PLATFORM_UNKNOWN);

  if (Platform == PLATFORM_MACCATALYST)
    if (Ctx && Ctx->FileKind != FileType::TBD_V3)
      return "invalid platform";

  if (Platform == PLATFORM_UNKNOWN)
    return "unknown platform";

  Values.insert(Platform);
  return {};
}

QuotingType ScalarTraits<PlatformSet>::mustQuote(StringRef) {
  return QuotingType::None;
}

void ScalarBitSetTraits<ArchitectureSet>::bitset(IO &IO,
                                                 ArchitectureSet &Archs) {
#define ARCHINFO(arch, type, subtype, numbits)                                 \
  IO.bitSetCase(Archs, #arch, 1U << static_cast<int>(AK_##arch));
#include "llvm/TextAPI/Architecture.def"
#undef ARCHINFO
}

void ScalarTraits<Architecture>::output(const Architecture &Value, void *,
                                        raw_ostream &OS) {
  OS << Value;
}
StringRef ScalarTraits<Architecture>::input(StringRef Scalar, void *,
                                            Architecture &Value) {
  Value = getArchitectureFromName(Scalar);
  return {};
}
QuotingType ScalarTraits<Architecture>::mustQuote(StringRef) {
  return QuotingType::None;
}

void ScalarTraits<PackedVersion>::output(const PackedVersion &Value, void *,
                                         raw_ostream &OS) {
  OS << Value;
}
StringRef ScalarTraits<PackedVersion>::input(StringRef Scalar, void *,
                                             PackedVersion &Value) {
  if (!Value.parse32(Scalar))
    return "invalid packed version string.";
  return {};
}
QuotingType ScalarTraits<PackedVersion>::mustQuote(StringRef) {
  return QuotingType::None;
}

void ScalarTraits<SwiftVersion>::output(const SwiftVersion &Value, void *,
                                        raw_ostream &OS) {
  switch (Value) {
  case 1:
    OS << "1.0";
    break;
  case 2:
    OS << "1.1";
    break;
  case 3:
    OS << "2.0";
    break;
  case 4:
    OS << "3.0";
    break;
  default:
    OS << (unsigned)Value;
    break;
  }
}
StringRef ScalarTraits<SwiftVersion>::input(StringRef Scalar, void *IO,
                                            SwiftVersion &Value) {
  const auto *Ctx = reinterpret_cast<TextAPIContext *>(IO);
  assert((!Ctx || Ctx->FileKind != FileType::Invalid) &&
         "File type is not set in context");

  if (Ctx->FileKind == FileType::TBD_V4) {
    if (Scalar.getAsInteger(10, Value))
      return "invalid Swift ABI version.";
    return {};
  } else {
    Value = StringSwitch<SwiftVersion>(Scalar)
                .Case("1.0", 1)
                .Case("1.1", 2)
                .Case("2.0", 3)
                .Case("3.0", 4)
                .Default(0);
  }

  if (Value != SwiftVersion(0))
    return {};

  if (Scalar.getAsInteger(10, Value))
    return "invalid Swift ABI version.";

  return StringRef();
}
QuotingType ScalarTraits<SwiftVersion>::mustQuote(StringRef) {
  return QuotingType::None;
}

void ScalarTraits<UUID>::output(const UUID &Value, void *, raw_ostream &OS) {}

StringRef ScalarTraits<UUID>::input(StringRef Scalar, void *, UUID &Value) {
  Value = {};
  return {};
}

QuotingType ScalarTraits<UUID>::mustQuote(StringRef) {
  return QuotingType::Single;
}

} // end namespace yaml.
} // end namespace llvm.
