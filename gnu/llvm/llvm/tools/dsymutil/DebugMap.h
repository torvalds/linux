//=- tools/dsymutil/DebugMap.h - Generic debug map representation -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// This file contains the class declaration of the DebugMap
/// entity. A DebugMap lists all the object files linked together to
/// produce an executable along with the linked address of all the
/// atoms used in these object files.
/// The DebugMap is an input to the DwarfLinker class that will
/// extract the Dwarf debug information from the referenced object
/// files and link their usefull debug info together.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_DSYMUTIL_DEBUGMAP_H
#define LLVM_TOOLS_DSYMUTIL_DEBUGMAP_H

#include "RelocationMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/TargetParser/Triple.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class raw_ostream;

namespace dsymutil {

class DebugMapObject;

/// The DebugMap object stores the list of object files to query for debug
/// information along with the mapping between the symbols' addresses in the
/// object file to their linked address in the linked binary.
///
/// A DebugMap producer could look like this:
/// DebugMap *DM = new DebugMap();
/// for (const auto &Obj: LinkedObjects) {
///     DebugMapObject &DMO = DM->addDebugMapObject(Obj.getPath());
///     for (const auto &Sym: Obj.getLinkedSymbols())
///         DMO.addSymbol(Sym.getName(), Sym.getObjectFileAddress(),
///                       Sym.getBinaryAddress());
/// }
///
/// A DebugMap consumer can then use the map to link the debug
/// information. For example something along the lines of:
/// for (const auto &DMO: DM->objects()) {
///     auto Obj = createBinary(DMO.getObjectFilename());
///     for (auto &DIE: Obj.getDwarfDIEs()) {
///         if (SymbolMapping *Sym = DMO.lookup(DIE.getName()))
///             DIE.relocate(Sym->ObjectAddress, Sym->BinaryAddress);
///         else
///             DIE.discardSubtree();
///     }
/// }
class DebugMap {
  Triple BinaryTriple;
  std::string BinaryPath;
  std::vector<uint8_t> BinaryUUID;
  using ObjectContainer = std::vector<std::unique_ptr<DebugMapObject>>;

  ObjectContainer Objects;

  /// For YAML IO support.
  ///@{
  friend yaml::MappingTraits<std::unique_ptr<DebugMap>>;
  friend yaml::MappingTraits<DebugMap>;

  DebugMap() = default;
  ///@}

public:
  DebugMap(const Triple &BinaryTriple, StringRef BinaryPath,
           ArrayRef<uint8_t> BinaryUUID = ArrayRef<uint8_t>())
      : BinaryTriple(BinaryTriple), BinaryPath(std::string(BinaryPath)),
        BinaryUUID(BinaryUUID.begin(), BinaryUUID.end()) {}

  using const_iterator = ObjectContainer::const_iterator;

  iterator_range<const_iterator> objects() const {
    return make_range(begin(), end());
  }

  const_iterator begin() const { return Objects.begin(); }

  const_iterator end() const { return Objects.end(); }

  unsigned getNumberOfObjects() const { return Objects.size(); }

  /// This function adds an DebugMapObject to the list owned by this
  /// debug map.
  DebugMapObject &
  addDebugMapObject(StringRef ObjectFilePath,
                    sys::TimePoint<std::chrono::seconds> Timestamp,
                    uint8_t Type = llvm::MachO::N_OSO);

  const Triple &getTriple() const { return BinaryTriple; }

  ArrayRef<uint8_t> getUUID() const { return ArrayRef<uint8_t>(BinaryUUID); }

  StringRef getBinaryPath() const { return BinaryPath; }

  void print(raw_ostream &OS) const;

#ifndef NDEBUG
  void dump() const;
#endif

  /// Read a debug map for \a InputFile.
  static ErrorOr<std::vector<std::unique_ptr<DebugMap>>>
  parseYAMLDebugMap(StringRef InputFile, StringRef PrependPath, bool Verbose);
};

/// The DebugMapObject represents one object file described by the DebugMap. It
/// contains a list of mappings between addresses in the object file and in the
/// linked binary for all the linked atoms in this object file.
class DebugMapObject {
public:
  using YAMLSymbolMapping = std::pair<std::string, SymbolMapping>;
  using DebugMapEntry = StringMapEntry<SymbolMapping>;

  /// Adds a symbol mapping to this DebugMapObject.
  /// \returns false if the symbol was already registered. The request
  /// is discarded in this case.
  bool addSymbol(StringRef SymName, std::optional<uint64_t> ObjectAddress,
                 uint64_t LinkedAddress, uint32_t Size);

  /// Lookup a symbol mapping.
  /// \returns null if the symbol isn't found.
  const DebugMapEntry *lookupSymbol(StringRef SymbolName) const;

  /// Lookup an object file address.
  /// \returns null if the address isn't found.
  const DebugMapEntry *lookupObjectAddress(uint64_t Address) const;

  StringRef getObjectFilename() const { return Filename; }

  sys::TimePoint<std::chrono::seconds> getTimestamp() const {
    return Timestamp;
  }

  uint8_t getType() const { return Type; }

  bool empty() const { return Symbols.empty(); }

  void addWarning(StringRef Warning) {
    Warnings.push_back(std::string(Warning));
  }
  const std::vector<std::string> &getWarnings() const { return Warnings; }

  const std::optional<RelocationMap> &getRelocationMap() const {
    return RelocMap;
  }
  void setRelocationMap(dsymutil::RelocationMap &RM);

  const std::optional<std::string> &getInstallName() const {
    return InstallName;
  }
  void setInstallName(StringRef IN);

  void print(raw_ostream &OS) const;
#ifndef NDEBUG
  void dump() const;
#endif

private:
  friend class DebugMap;

  /// DebugMapObjects can only be constructed by the owning DebugMap.
  DebugMapObject(StringRef ObjectFilename,
                 sys::TimePoint<std::chrono::seconds> Timestamp, uint8_t Type);

  std::string Filename;
  sys::TimePoint<std::chrono::seconds> Timestamp;
  StringMap<struct SymbolMapping> Symbols;
  DenseMap<uint64_t, DebugMapEntry *> AddressToMapping;
  uint8_t Type;

  std::optional<RelocationMap> RelocMap;
  std::optional<std::string> InstallName;

  std::vector<std::string> Warnings;

  /// For YAMLIO support.
  ///@{
  friend yaml::MappingTraits<dsymutil::DebugMapObject>;
  friend yaml::SequenceTraits<std::vector<std::unique_ptr<DebugMapObject>>>;

  DebugMapObject() = default;

public:
  DebugMapObject(DebugMapObject &&) = default;
  DebugMapObject &operator=(DebugMapObject &&) = default;
  ///@}
};

} // end namespace dsymutil
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::dsymutil::DebugMapObject::YAMLSymbolMapping)

namespace llvm {
namespace yaml {

using namespace llvm::dsymutil;

template <> struct MappingTraits<std::pair<std::string, SymbolMapping>> {
  static void mapping(IO &io, std::pair<std::string, SymbolMapping> &s);
  static const bool flow = true;
};

template <> struct MappingTraits<dsymutil::DebugMapObject> {
  struct YamlDMO;
  static void mapping(IO &io, dsymutil::DebugMapObject &DMO);
};

template <>
struct SequenceTraits<std::vector<std::unique_ptr<dsymutil::DebugMapObject>>> {
  static size_t
  size(IO &io, std::vector<std::unique_ptr<dsymutil::DebugMapObject>> &seq);
  static dsymutil::DebugMapObject &
  element(IO &, std::vector<std::unique_ptr<dsymutil::DebugMapObject>> &seq,
          size_t index);
};

template <> struct MappingTraits<dsymutil::DebugMap> {
  static void mapping(IO &io, dsymutil::DebugMap &DM);
};

template <> struct MappingTraits<std::unique_ptr<dsymutil::DebugMap>> {
  static void mapping(IO &io, std::unique_ptr<dsymutil::DebugMap> &DM);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_TOOLS_DSYMUTIL_DEBUGMAP_H
