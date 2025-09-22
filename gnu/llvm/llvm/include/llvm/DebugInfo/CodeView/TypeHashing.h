//===- TypeHashing.h ---------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEHASHING_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEHASHING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"

#include "llvm/Support/FormatProviders.h"

#include <type_traits>

namespace llvm {
class raw_ostream;
namespace codeview {

/// A locally hashed type represents a straightforward hash code of a serialized
/// record.  The record is simply serialized, and then the bytes are hashed by
/// a standard algorithm.  This is sufficient for the case of de-duplicating
/// records within a single sequence of types, because if two records both have
/// a back-reference to the same type in the same stream, they will both have
/// the same numeric value for the TypeIndex of the back reference.
struct LocallyHashedType {
  hash_code Hash;
  ArrayRef<uint8_t> RecordData;

  /// Given a type, compute its local hash.
  static LocallyHashedType hashType(ArrayRef<uint8_t> RecordData);

  /// Given a sequence of types, compute all of the local hashes.
  template <typename Range>
  static std::vector<LocallyHashedType> hashTypes(Range &&Records) {
    std::vector<LocallyHashedType> Hashes;
    Hashes.reserve(std::distance(std::begin(Records), std::end(Records)));
    for (const auto &R : Records)
      Hashes.push_back(hashType(R));

    return Hashes;
  }

  static std::vector<LocallyHashedType>
  hashTypeCollection(TypeCollection &Types) {
    std::vector<LocallyHashedType> Hashes;
    Types.ForEachRecord([&Hashes](TypeIndex TI, const CVType &Type) {
      Hashes.push_back(hashType(Type.RecordData));
    });
    return Hashes;
  }
};

enum class GlobalTypeHashAlg : uint16_t {
  SHA1 = 0, // standard 20-byte SHA1 hash
  SHA1_8,   // last 8-bytes of standard SHA1 hash
  BLAKE3,   // truncated 8-bytes BLAKE3
};

/// A globally hashed type represents a hash value that is sufficient to
/// uniquely identify a record across multiple type streams or type sequences.
/// This works by, for any given record A which references B, replacing the
/// TypeIndex that refers to B with a previously-computed global hash for B.  As
/// this is a recursive algorithm (e.g. the global hash of B also depends on the
/// global hashes of the types that B refers to), a global hash can uniquely
/// identify that A occurs in another stream that has a completely
/// different graph structure.  Although the hash itself is slower to compute,
/// probing is much faster with a globally hashed type, because the hash itself
/// is considered "as good as" the original type.  Since type records can be
/// quite large, this makes the equality comparison of the hash much faster than
/// equality comparison of a full record.
struct GloballyHashedType {
  GloballyHashedType() = default;
  GloballyHashedType(StringRef H)
      : GloballyHashedType(ArrayRef<uint8_t>(H.bytes_begin(), H.bytes_end())) {}
  GloballyHashedType(ArrayRef<uint8_t> H) {
    assert(H.size() == 8);
    ::memcpy(Hash.data(), H.data(), 8);
  }
  std::array<uint8_t, 8> Hash;

  bool empty() const { return *(const uint64_t*)Hash.data() == 0; }

  friend inline bool operator==(const GloballyHashedType &L,
                                const GloballyHashedType &R) {
    return L.Hash == R.Hash;
  }

  friend inline bool operator!=(const GloballyHashedType &L,
                                const GloballyHashedType &R) {
    return !(L.Hash == R.Hash);
  }

  /// Given a sequence of bytes representing a record, compute a global hash for
  /// this record.  Due to the nature of global hashes incorporating the hashes
  /// of referenced records, this function requires a list of types and ids
  /// that RecordData might reference, indexable by TypeIndex.
  static GloballyHashedType hashType(ArrayRef<uint8_t> RecordData,
                                     ArrayRef<GloballyHashedType> PreviousTypes,
                                     ArrayRef<GloballyHashedType> PreviousIds);

  /// Given a sequence of bytes representing a record, compute a global hash for
  /// this record.  Due to the nature of global hashes incorporating the hashes
  /// of referenced records, this function requires a list of types and ids
  /// that RecordData might reference, indexable by TypeIndex.
  static GloballyHashedType hashType(CVType Type,
                                     ArrayRef<GloballyHashedType> PreviousTypes,
                                     ArrayRef<GloballyHashedType> PreviousIds) {
    return hashType(Type.RecordData, PreviousTypes, PreviousIds);
  }

  /// Given a sequence of combined type and ID records, compute global hashes
  /// for each of them, returning the results in a vector of hashed types.
  template <typename Range>
  static std::vector<GloballyHashedType> hashTypes(Range &&Records) {
    std::vector<GloballyHashedType> Hashes;
    bool UnresolvedRecords = false;
    for (const auto &R : Records) {
      GloballyHashedType H = hashType(R, Hashes, Hashes);
      if (H.empty())
        UnresolvedRecords = true;
      Hashes.push_back(H);
    }

    // In some rare cases, there might be records with forward references in the
    // stream. Several passes might be needed to fully hash each record in the
    // Type stream. However this occurs on very small OBJs generated by MASM,
    // with a dozen records at most. Therefore this codepath isn't
    // time-critical, as it isn't taken in 99% of cases.
    while (UnresolvedRecords) {
      UnresolvedRecords = false;
      auto HashIt = Hashes.begin();
      for (const auto &R : Records) {
        if (HashIt->empty()) {
          GloballyHashedType H = hashType(R, Hashes, Hashes);
          if (H.empty())
            UnresolvedRecords = true;
          else
            *HashIt = H;
        }
        ++HashIt;
      }
    }

    return Hashes;
  }

  /// Given a sequence of combined type and ID records, compute global hashes
  /// for each of them, returning the results in a vector of hashed types.
  template <typename Range>
  static std::vector<GloballyHashedType>
  hashIds(Range &&Records, ArrayRef<GloballyHashedType> TypeHashes) {
    std::vector<GloballyHashedType> IdHashes;
    for (const auto &R : Records)
      IdHashes.push_back(hashType(R, TypeHashes, IdHashes));

    return IdHashes;
  }

  static std::vector<GloballyHashedType>
  hashTypeCollection(TypeCollection &Types) {
    std::vector<GloballyHashedType> Hashes;
    Types.ForEachRecord([&Hashes](TypeIndex TI, const CVType &Type) {
      Hashes.push_back(hashType(Type.RecordData, Hashes, Hashes));
    });
    return Hashes;
  }
};
static_assert(std::is_trivially_copyable<GloballyHashedType>::value,
              "GloballyHashedType must be trivially copyable so that we can "
              "reinterpret_cast arrays of hash data to arrays of "
              "GloballyHashedType");
} // namespace codeview

template <> struct DenseMapInfo<codeview::LocallyHashedType> {
  static codeview::LocallyHashedType Empty;
  static codeview::LocallyHashedType Tombstone;

  static codeview::LocallyHashedType getEmptyKey() { return Empty; }

  static codeview::LocallyHashedType getTombstoneKey() { return Tombstone; }

  static unsigned getHashValue(codeview::LocallyHashedType Val) {
    return Val.Hash;
  }

  static bool isEqual(codeview::LocallyHashedType LHS,
                      codeview::LocallyHashedType RHS) {
    if (LHS.Hash != RHS.Hash)
      return false;
    return LHS.RecordData == RHS.RecordData;
  }
};

template <> struct DenseMapInfo<codeview::GloballyHashedType> {
  static codeview::GloballyHashedType Empty;
  static codeview::GloballyHashedType Tombstone;

  static codeview::GloballyHashedType getEmptyKey() { return Empty; }

  static codeview::GloballyHashedType getTombstoneKey() { return Tombstone; }

  static unsigned getHashValue(codeview::GloballyHashedType Val) {
    return *reinterpret_cast<const unsigned *>(Val.Hash.data());
  }

  static bool isEqual(codeview::GloballyHashedType LHS,
                      codeview::GloballyHashedType RHS) {
    return LHS == RHS;
  }
};

template <> struct format_provider<codeview::LocallyHashedType> {
public:
  static void format(const codeview::LocallyHashedType &V,
                     llvm::raw_ostream &Stream, StringRef Style) {
    write_hex(Stream, V.Hash, HexPrintStyle::Upper, 8);
  }
};

template <> struct format_provider<codeview::GloballyHashedType> {
public:
  static void format(const codeview::GloballyHashedType &V,
                     llvm::raw_ostream &Stream, StringRef Style) {
    for (uint8_t B : V.Hash) {
      write_hex(Stream, B, HexPrintStyle::Upper, 2);
    }
  }
};

} // namespace llvm

#endif
