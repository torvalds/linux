//===- InstrumentationMap.h - XRay Instrumentation Map ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the interface for extracting the instrumentation map from an
// XRay-instrumented binary.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_XRAY_INSTRUMENTATIONMAP_H
#define LLVM_XRAY_INSTRUMENTATIONMAP_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace llvm {

namespace xray {

// Forward declare to make a friend.
class InstrumentationMap;

/// Loads the instrumentation map from |Filename|. This auto-deduces the type of
/// the instrumentation map.
Expected<InstrumentationMap> loadInstrumentationMap(StringRef Filename);

/// Represents an XRay instrumentation sled entry from an object file.
struct SledEntry {
  /// Each entry here represents the kinds of supported instrumentation map
  /// entries.
  enum class FunctionKinds { ENTRY, EXIT, TAIL, LOG_ARGS_ENTER, CUSTOM_EVENT };

  /// The address of the sled.
  uint64_t Address;

  /// The address of the function.
  uint64_t Function;

  /// The kind of sled.
  FunctionKinds Kind;

  /// Whether the sled was annotated to always be instrumented.
  bool AlwaysInstrument;

  unsigned char Version;
};

struct YAMLXRaySledEntry {
  int32_t FuncId;
  yaml::Hex64 Address;
  yaml::Hex64 Function;
  SledEntry::FunctionKinds Kind;
  bool AlwaysInstrument;
  std::string FunctionName;
  unsigned char Version;
};

/// The InstrumentationMap represents the computed function id's and indicated
/// function addresses from an object file (or a YAML file). This provides an
/// interface to just the mapping between the function id, and the function
/// address.
///
/// We also provide raw access to the actual instrumentation map entries we find
/// associated with a particular object file.
///
class InstrumentationMap {
public:
  using FunctionAddressMap = std::unordered_map<int32_t, uint64_t>;
  using FunctionAddressReverseMap = std::unordered_map<uint64_t, int32_t>;
  using SledContainer = std::vector<SledEntry>;

private:
  SledContainer Sleds;
  FunctionAddressMap FunctionAddresses;
  FunctionAddressReverseMap FunctionIds;

  friend Expected<InstrumentationMap> loadInstrumentationMap(StringRef);

public:
  /// Provides a raw accessor to the unordered map of function addresses.
  const FunctionAddressMap &getFunctionAddresses() { return FunctionAddresses; }

  /// Returns an XRay computed function id, provided a function address.
  std::optional<int32_t> getFunctionId(uint64_t Addr) const;

  /// Returns the function address for a function id.
  std::optional<uint64_t> getFunctionAddr(int32_t FuncId) const;

  /// Provide read-only access to the entries of the instrumentation map.
  const SledContainer &sleds() const { return Sleds; };
};

} // end namespace xray

namespace yaml {

template <> struct ScalarEnumerationTraits<xray::SledEntry::FunctionKinds> {
  static void enumeration(IO &IO, xray::SledEntry::FunctionKinds &Kind) {
    IO.enumCase(Kind, "function-enter", xray::SledEntry::FunctionKinds::ENTRY);
    IO.enumCase(Kind, "function-exit", xray::SledEntry::FunctionKinds::EXIT);
    IO.enumCase(Kind, "tail-exit", xray::SledEntry::FunctionKinds::TAIL);
    IO.enumCase(Kind, "log-args-enter",
                xray::SledEntry::FunctionKinds::LOG_ARGS_ENTER);
    IO.enumCase(Kind, "custom-event",
                xray::SledEntry::FunctionKinds::CUSTOM_EVENT);
  }
};

template <> struct MappingTraits<xray::YAMLXRaySledEntry> {
  static void mapping(IO &IO, xray::YAMLXRaySledEntry &Entry) {
    IO.mapRequired("id", Entry.FuncId);
    IO.mapRequired("address", Entry.Address);
    IO.mapRequired("function", Entry.Function);
    IO.mapRequired("kind", Entry.Kind);
    IO.mapRequired("always-instrument", Entry.AlwaysInstrument);
    IO.mapOptional("function-name", Entry.FunctionName);
    IO.mapOptional("version", Entry.Version, 0);
  }

  static constexpr bool flow = true;
};

} // end namespace yaml

} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(xray::YAMLXRaySledEntry)

#endif // LLVM_XRAY_INSTRUMENTATIONMAP_H
