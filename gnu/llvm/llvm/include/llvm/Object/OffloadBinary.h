//===--- Offloading.h - Utilities for handling offloading code  -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the binary format used for budingling device metadata with
// an associated device image. The data can then be stored inside a host object
// file to create a fat binary and read by the linker. This is intended to be a
// thin wrapper around the image itself. If this format becomes sufficiently
// complex it should be moved to a standard binary format like msgpack or ELF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_OFFLOADBINARY_H
#define LLVM_OBJECT_OFFLOADBINARY_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {

namespace object {

/// The producer of the associated offloading image.
enum OffloadKind : uint16_t {
  OFK_None = 0,
  OFK_OpenMP,
  OFK_Cuda,
  OFK_HIP,
  OFK_LAST,
};

/// The type of contents the offloading image contains.
enum ImageKind : uint16_t {
  IMG_None = 0,
  IMG_Object,
  IMG_Bitcode,
  IMG_Cubin,
  IMG_Fatbinary,
  IMG_PTX,
  IMG_LAST,
};

/// A simple binary serialization of an offloading file. We use this format to
/// embed the offloading image into the host executable so it can be extracted
/// and used by the linker.
///
/// Many of these could be stored in the same section by the time the linker
/// sees it so we mark this information with a header. The version is used to
/// detect ABI stability and the size is used to find other offloading entries
/// that may exist in the same section. All offsets are given as absolute byte
/// offsets from the beginning of the file.
class OffloadBinary : public Binary {
public:
  using string_iterator = MapVector<StringRef, StringRef>::const_iterator;
  using string_iterator_range = iterator_range<string_iterator>;

  /// The current version of the binary used for backwards compatibility.
  static const uint32_t Version = 1;

  /// The offloading metadata that will be serialized to a memory buffer.
  struct OffloadingImage {
    ImageKind TheImageKind;
    OffloadKind TheOffloadKind;
    uint32_t Flags;
    MapVector<StringRef, StringRef> StringData;
    std::unique_ptr<MemoryBuffer> Image;
  };

  /// Attempt to parse the offloading binary stored in \p Data.
  static Expected<std::unique_ptr<OffloadBinary>> create(MemoryBufferRef);

  /// Serialize the contents of \p File to a binary buffer to be read later.
  static SmallString<0> write(const OffloadingImage &);

  static uint64_t getAlignment() { return 8; }

  ImageKind getImageKind() const { return TheEntry->TheImageKind; }
  OffloadKind getOffloadKind() const { return TheEntry->TheOffloadKind; }
  uint32_t getVersion() const { return TheHeader->Version; }
  uint32_t getFlags() const { return TheEntry->Flags; }
  uint64_t getSize() const { return TheHeader->Size; }

  StringRef getTriple() const { return getString("triple"); }
  StringRef getArch() const { return getString("arch"); }
  StringRef getImage() const {
    return StringRef(&Buffer[TheEntry->ImageOffset], TheEntry->ImageSize);
  }

  // Iterator over all the key and value pairs in the binary.
  string_iterator_range strings() const {
    return string_iterator_range(StringData.begin(), StringData.end());
  }

  StringRef getString(StringRef Key) const { return StringData.lookup(Key); }

  static bool classof(const Binary *V) { return V->isOffloadFile(); }

  struct Header {
    uint8_t Magic[4] = {0x10, 0xFF, 0x10, 0xAD}; // 0x10FF10AD magic bytes.
    uint32_t Version = OffloadBinary::Version;   // Version identifier.
    uint64_t Size;        // Size in bytes of this entire binary.
    uint64_t EntryOffset; // Offset of the metadata entry in bytes.
    uint64_t EntrySize;   // Size of the metadata entry in bytes.
  };

  struct Entry {
    ImageKind TheImageKind;     // The kind of the image stored.
    OffloadKind TheOffloadKind; // The producer of this image.
    uint32_t Flags;             // Additional flags associated with the image.
    uint64_t StringOffset;      // Offset in bytes to the string map.
    uint64_t NumStrings;        // Number of entries in the string map.
    uint64_t ImageOffset;       // Offset in bytes of the actual binary image.
    uint64_t ImageSize;         // Size in bytes of the binary image.
  };

  struct StringEntry {
    uint64_t KeyOffset;
    uint64_t ValueOffset;
  };

private:
  OffloadBinary(MemoryBufferRef Source, const Header *TheHeader,
                const Entry *TheEntry)
      : Binary(Binary::ID_Offload, Source), Buffer(Source.getBufferStart()),
        TheHeader(TheHeader), TheEntry(TheEntry) {
    const StringEntry *StringMapBegin =
        reinterpret_cast<const StringEntry *>(&Buffer[TheEntry->StringOffset]);
    for (uint64_t I = 0, E = TheEntry->NumStrings; I != E; ++I) {
      StringRef Key = &Buffer[StringMapBegin[I].KeyOffset];
      StringData[Key] = &Buffer[StringMapBegin[I].ValueOffset];
    }
  }

  OffloadBinary(const OffloadBinary &Other) = delete;

  /// Map from keys to offsets in the binary.
  MapVector<StringRef, StringRef> StringData;
  /// Raw pointer to the MemoryBufferRef for convenience.
  const char *Buffer;
  /// Location of the header within the binary.
  const Header *TheHeader;
  /// Location of the metadata entries within the binary.
  const Entry *TheEntry;
};

/// A class to contain the binary information for a single OffloadBinary that
/// owns its memory.
class OffloadFile : public OwningBinary<OffloadBinary> {
public:
  using TargetID = std::pair<StringRef, StringRef>;

  OffloadFile(std::unique_ptr<OffloadBinary> Binary,
              std::unique_ptr<MemoryBuffer> Buffer)
      : OwningBinary<OffloadBinary>(std::move(Binary), std::move(Buffer)) {}

  /// Make a deep copy of this offloading file.
  OffloadFile copy() const {
    std::unique_ptr<MemoryBuffer> Buffer = MemoryBuffer::getMemBufferCopy(
        getBinary()->getMemoryBufferRef().getBuffer());

    // This parsing should never fail because it has already been parsed.
    auto NewBinaryOrErr = OffloadBinary::create(*Buffer);
    assert(NewBinaryOrErr && "Failed to parse a copy of the binary?");
    if (!NewBinaryOrErr)
      llvm::consumeError(NewBinaryOrErr.takeError());
    return OffloadFile(std::move(*NewBinaryOrErr), std::move(Buffer));
  }

  /// We use the Triple and Architecture pair to group linker inputs together.
  /// This conversion function lets us use these inputs in a hash-map.
  operator TargetID() const {
    return std::make_pair(getBinary()->getTriple(), getBinary()->getArch());
  }
};

/// Extracts embedded device offloading code from a memory \p Buffer to a list
/// of \p Binaries.
Error extractOffloadBinaries(MemoryBufferRef Buffer,
                             SmallVectorImpl<OffloadFile> &Binaries);

/// Convert a string \p Name to an image kind.
ImageKind getImageKind(StringRef Name);

/// Convert an image kind to its string representation.
StringRef getImageKindName(ImageKind Name);

/// Convert a string \p Name to an offload kind.
OffloadKind getOffloadKind(StringRef Name);

/// Convert an offload kind to its string representation.
StringRef getOffloadKindName(OffloadKind Name);

/// If the target is AMD we check the target IDs for mutual compatibility. A
/// target id is a string conforming to the folowing BNF syntax:
///
///  target-id ::= '<arch> ( : <feature> ( '+' | '-' ) )*'
///
/// The features 'xnack' and 'sramecc' are currently supported. These can be in
/// the state of on, off, and any when unspecified. A target marked as any can
/// bind with either on or off. This is used to link mutually compatible
/// architectures together. Returns false in the case of an exact match.
bool areTargetsCompatible(const OffloadFile::TargetID &LHS,
                          const OffloadFile::TargetID &RHS);

} // namespace object

} // namespace llvm
#endif
