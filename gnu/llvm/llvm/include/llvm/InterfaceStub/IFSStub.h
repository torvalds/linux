//===- IFSStub.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/
///
/// \file
/// This file defines an internal representation of an InterFace Stub.
///
//===-----------------------------------------------------------------------===/

#ifndef LLVM_INTERFACESTUB_IFSSTUB_H
#define LLVM_INTERFACESTUB_IFSSTUB_H

#include "llvm/Support/VersionTuple.h"
#include <optional>
#include <vector>

namespace llvm {
namespace ifs {

typedef uint16_t IFSArch;

enum class IFSSymbolType {
  NoType,
  Object,
  Func,
  TLS,

  // Type information is 4 bits, so 16 is safely out of range.
  Unknown = 16,
};

enum class IFSEndiannessType {
  Little,
  Big,

  // Endianness info is 1 bytes, 256 is safely out of range.
  Unknown = 256,
};

enum class IFSBitWidthType {
  IFS32,
  IFS64,

  // Bit width info is 1 bytes, 256 is safely out of range.
  Unknown = 256,
};

struct IFSSymbol {
  IFSSymbol() = default;
  explicit IFSSymbol(std::string SymbolName) : Name(std::move(SymbolName)) {}
  std::string Name;
  std::optional<uint64_t> Size;
  IFSSymbolType Type = IFSSymbolType::NoType;
  bool Undefined = false;
  bool Weak = false;
  std::optional<std::string> Warning;
  bool operator<(const IFSSymbol &RHS) const { return Name < RHS.Name; }
};

struct IFSTarget {
  std::optional<std::string> Triple;
  std::optional<std::string> ObjectFormat;
  std::optional<IFSArch> Arch;
  std::optional<std::string> ArchString;
  std::optional<IFSEndiannessType> Endianness;
  std::optional<IFSBitWidthType> BitWidth;

  bool empty();
};

inline bool operator==(const IFSTarget &Lhs, const IFSTarget &Rhs) {
  if (Lhs.Arch != Rhs.Arch || Lhs.BitWidth != Rhs.BitWidth ||
      Lhs.Endianness != Rhs.Endianness ||
      Lhs.ObjectFormat != Rhs.ObjectFormat || Lhs.Triple != Rhs.Triple)
    return false;
  return true;
}

inline bool operator!=(const IFSTarget &Lhs, const IFSTarget &Rhs) {
  return !(Lhs == Rhs);
}

// A cumulative representation of InterFace stubs.
// Both textual and binary stubs will read into and write from this object.
struct IFSStub {
  // TODO: Add support for symbol versioning.
  VersionTuple IfsVersion;
  std::optional<std::string> SoName;
  IFSTarget Target;
  std::vector<std::string> NeededLibs;
  std::vector<IFSSymbol> Symbols;

  IFSStub() = default;
  IFSStub(const IFSStub &Stub);
  IFSStub(IFSStub &&Stub);
  virtual ~IFSStub() = default;
};

// Create a alias class for IFSStub.
// LLVM's YAML library does not allow mapping a class with 2 traits,
// which prevents us using 'Target:' field with different definitions.
// This class makes it possible to map a second traits so the same data
// structure can be used for 2 different yaml schema.
struct IFSStubTriple : IFSStub {
  IFSStubTriple() = default;
  IFSStubTriple(const IFSStub &Stub);
  IFSStubTriple(const IFSStubTriple &Stub);
  IFSStubTriple(IFSStubTriple &&Stub);
};

/// This function convert bit width type from IFS enum to ELF format
/// Currently, ELFCLASS32 and ELFCLASS64 are supported.
///
/// @param BitWidth IFS bit width type.
uint8_t convertIFSBitWidthToELF(IFSBitWidthType BitWidth);

/// This function convert endianness type from IFS enum to ELF format
/// Currently, ELFDATA2LSB and ELFDATA2MSB are supported.
///
/// @param Endianness IFS endianness type.
uint8_t convertIFSEndiannessToELF(IFSEndiannessType Endianness);

/// This function convert symbol type from IFS enum to ELF format
/// Currently, STT_NOTYPE, STT_OBJECT, STT_FUNC, and STT_TLS are supported.
///
/// @param SymbolType IFS symbol type.
uint8_t convertIFSSymbolTypeToELF(IFSSymbolType SymbolType);

/// This function extracts ELF bit width from e_ident[EI_CLASS] of an ELF file
/// Currently, ELFCLASS32 and ELFCLASS64 are supported.
/// Other endianness types are mapped to IFSBitWidthType::Unknown.
///
/// @param BitWidth e_ident[EI_CLASS] value to extract bit width from.
IFSBitWidthType convertELFBitWidthToIFS(uint8_t BitWidth);

/// This function extracts ELF endianness from e_ident[EI_DATA] of an ELF file
/// Currently, ELFDATA2LSB and ELFDATA2MSB are supported.
/// Other endianness types are mapped to IFSEndiannessType::Unknown.
///
/// @param Endianness e_ident[EI_DATA] value to extract endianness type from.
IFSEndiannessType convertELFEndiannessToIFS(uint8_t Endianness);

/// This function extracts symbol type from a symbol's st_info member and
/// maps it to an IFSSymbolType enum.
/// Currently, STT_NOTYPE, STT_OBJECT, STT_FUNC, and STT_TLS are supported.
/// Other symbol types are mapped to IFSSymbolType::Unknown.
///
/// @param SymbolType Binary symbol st_info to extract symbol type from.
IFSSymbolType convertELFSymbolTypeToIFS(uint8_t SymbolType);
} // namespace ifs
} // end namespace llvm

#endif // LLVM_INTERFACESTUB_IFSSTUB_H
