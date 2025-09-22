//===- tools/dsymutil/RelocationMap.h -------------------------- *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// This file contains the class declaration of the RelocationMap
/// entity. RelocationMap lists all the relocations of all the
/// atoms used in the object files linked together to
/// produce an executable.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_DSYMUTIL_RELOCATIONMAP_H
#define LLVM_TOOLS_DSYMUTIL_RELOCATIONMAP_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/TargetParser/Triple.h"

#include <optional>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;

namespace dsymutil {

struct SymbolMapping {
  std::optional<yaml::Hex64> ObjectAddress;
  yaml::Hex64 BinaryAddress;
  yaml::Hex32 Size;

  SymbolMapping(std::optional<uint64_t> ObjectAddr, uint64_t BinaryAddress,
                uint32_t Size)
      : BinaryAddress(BinaryAddress), Size(Size) {
    if (ObjectAddr)
      ObjectAddress = *ObjectAddr;
  }

  /// For YAML IO support
  SymbolMapping() = default;
};

/// ValidReloc represents one relocation entry described by the RelocationMap.
/// It contains a list of DWARF relocations to apply to a linked binary.
class ValidReloc {
public:
  yaml::Hex64 Offset;
  yaml::Hex32 Size;
  yaml::Hex64 Addend;
  std::string SymbolName;
  struct SymbolMapping SymbolMapping;

  struct SymbolMapping getSymbolMapping() const { return SymbolMapping; }

  ValidReloc(uint64_t Offset, uint32_t Size, uint64_t Addend,
             StringRef SymbolName, struct SymbolMapping SymbolMapping)
      : Offset(Offset), Size(Size), Addend(Addend), SymbolName(SymbolName),
        SymbolMapping(SymbolMapping) {}

  bool operator<(const ValidReloc &RHS) const { return Offset < RHS.Offset; }

  /// For YAMLIO support.
  ValidReloc() = default;
};

/// The RelocationMap object stores the list of relocation entries for a binary
class RelocationMap {
  Triple BinaryTriple;
  std::string BinaryPath;
  using RelocContainer = std::vector<ValidReloc>;

  RelocContainer Relocations;

  /// For YAML IO support.
  ///@{
  friend yaml::MappingTraits<std::unique_ptr<RelocationMap>>;
  friend yaml::MappingTraits<RelocationMap>;

  RelocationMap() = default;
  ///@}

public:
  RelocationMap(const Triple &BinaryTriple, StringRef BinaryPath)
      : BinaryTriple(BinaryTriple), BinaryPath(std::string(BinaryPath)) {}

  using const_iterator = RelocContainer::const_iterator;

  iterator_range<const_iterator> relocations() const {
    return make_range(begin(), end());
  }

  const_iterator begin() const { return Relocations.begin(); }

  const_iterator end() const { return Relocations.end(); }

  size_t getNumberOfEntries() const { return Relocations.size(); }

  /// This function adds a ValidReloc to the list owned by this
  /// relocation map.
  void addRelocationMapEntry(const ValidReloc &Relocation);

  const Triple &getTriple() const { return BinaryTriple; }

  StringRef getBinaryPath() const { return BinaryPath; }

  void print(raw_ostream &OS) const;

#ifndef NDEBUG
  void dump() const;
#endif

  /// Read a relocation map from \a InputFile.
  static ErrorOr<std::unique_ptr<RelocationMap>>
  parseYAMLRelocationMap(StringRef InputFile, StringRef PrependPath);
};

} // end namespace dsymutil
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(dsymutil::ValidReloc)

namespace llvm {
namespace yaml {

using namespace llvm::dsymutil;

template <> struct MappingTraits<dsymutil::ValidReloc> {
  static void mapping(IO &io, dsymutil::ValidReloc &VR);
  static const bool flow = true;
};

template <> struct MappingTraits<dsymutil::RelocationMap> {
  struct YamlRM;
  static void mapping(IO &io, dsymutil::RelocationMap &RM);
};

template <> struct MappingTraits<std::unique_ptr<dsymutil::RelocationMap>> {
  struct YamlRM;
  static void mapping(IO &io, std::unique_ptr<dsymutil::RelocationMap> &RM);
};

template <> struct ScalarTraits<Triple> {
  static void output(const Triple &val, void *, raw_ostream &out);
  static StringRef input(StringRef scalar, void *, Triple &value);
  static QuotingType mustQuote(StringRef) { return QuotingType::Single; }
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_TOOLS_DSYMUTIL_RELOCATIONMAP_H
