//===- DebugTypes.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_DEBUGTYPES_H
#define LLD_COFF_DEBUGTYPES_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm::codeview {
struct GloballyHashedType;
}
namespace llvm::pdb {
class NativeSession;
class TpiStream;
}

namespace lld::coff {

using llvm::codeview::GloballyHashedType;
using llvm::codeview::TypeIndex;

class ObjFile;
class PDBInputFile;
class TypeMerger;
struct GHashState;
class COFFLinkerContext;

class TpiSource {
public:
  enum TpiKind : uint8_t { Regular, PCH, UsingPCH, PDB, PDBIpi, UsingPDB };

  TpiSource(COFFLinkerContext &ctx, TpiKind k, ObjFile *f);
  virtual ~TpiSource();

  /// Produce a mapping from the type and item indices used in the object
  /// file to those in the destination PDB.
  ///
  /// If the object file uses a type server PDB (compiled with /Zi), merge TPI
  /// and IPI from the type server PDB and return a map for it. Each unique type
  /// server PDB is merged at most once, so this may return an existing index
  /// mapping.
  ///
  /// If the object does not use a type server PDB (compiled with /Z7), we merge
  /// all the type and item records from the .debug$S stream and fill in the
  /// caller-provided ObjectIndexMap.
  virtual Error mergeDebugT(TypeMerger *m);

  /// Load global hashes, either by hashing types directly, or by loading them
  /// from LLVM's .debug$H section.
  virtual void loadGHashes();

  /// Use global hashes to merge type information.
  virtual void remapTpiWithGHashes(GHashState *g);

  // Remap a type index in place.
  bool remapTypeIndex(TypeIndex &ti, llvm::codeview::TiRefKind refKind) const;

protected:
  void remapRecord(MutableArrayRef<uint8_t> rec,
                   ArrayRef<llvm::codeview::TiReference> typeRefs);

  void mergeTypeRecord(TypeIndex curIndex, llvm::codeview::CVType ty);

  // Merge the type records listed in uniqueTypes. beginIndex is the TypeIndex
  // of the first record in this source, typically 0x1000. When PCHs are
  // involved, it may start higher.
  void mergeUniqueTypeRecords(
      ArrayRef<uint8_t> debugTypes,
      TypeIndex beginIndex = TypeIndex(TypeIndex::FirstNonSimpleIndex));

  // Use the ghash table to construct a map from source type index to
  // destination PDB type index. Usable for either TPI or IPI.
  void fillMapFromGHashes(GHashState *m);

  // Copies ghashes from a vector into an array. These are long lived, so it's
  // worth the time to copy these into an appropriately sized vector to reduce
  // memory usage.
  void assignGHashesFromVector(std::vector<GloballyHashedType> &&hashVec);

  // Walk over file->debugTypes and fill in the isItemIndex bit vector.
  void fillIsItemIndexFromDebugT();

  COFFLinkerContext &ctx;

public:
  bool remapTypesInSymbolRecord(MutableArrayRef<uint8_t> rec);

  void remapTypesInTypeRecord(MutableArrayRef<uint8_t> rec);

  /// Is this a dependent file that needs to be processed first, before other
  /// OBJs?
  virtual bool isDependency() const { return false; }

  /// Returns true if this type record should be omitted from the PDB, even if
  /// it is unique. This prevents a record from being added to the input ghash
  /// table.
  bool shouldOmitFromPdb(uint32_t ghashIdx) {
    return ghashIdx == endPrecompIdx;
  }

  const TpiKind kind;
  bool ownedGHashes = true;
  uint32_t tpiSrcIdx = 0;

  /// The index (zero based, not 0x1000-based) of the LF_ENDPRECOMP record in
  /// this object, if one exists. This is the all ones value otherwise. It is
  /// recorded here for validation, and so that it can be omitted from the final
  /// ghash table.
  uint32_t endPrecompIdx = ~0U;

public:
  ObjFile *file;

  /// An error encountered during type merging, if any.
  Error typeMergingError = Error::success();

  // Storage for tpiMap or ipiMap, depending on the kind of source.
  llvm::SmallVector<TypeIndex, 0> indexMapStorage;

  // Source type index to PDB type index mapping for type and item records.
  // These mappings will be the same for /Z7 objects, and distinct for /Zi
  // objects.
  llvm::ArrayRef<TypeIndex> tpiMap;
  llvm::ArrayRef<TypeIndex> ipiMap;

  /// Array of global type hashes, indexed by TypeIndex. May be calculated on
  /// demand, or present in input object files.
  llvm::ArrayRef<llvm::codeview::GloballyHashedType> ghashes;

  /// When ghashing is used, record the mapping from LF_[M]FUNC_ID to function
  /// type index here. Both indices are PDB indices, not object type indexes.
  std::vector<std::pair<TypeIndex, TypeIndex>> funcIdToType;

  /// Indicates if a type record is an item index or a type index.
  llvm::BitVector isItemIndex;

  /// A list of all "unique" type indices which must be merged into the final
  /// PDB. GHash type deduplication produces this list, and it should be
  /// considerably smaller than the input.
  std::vector<uint32_t> uniqueTypes;

  struct MergedInfo {
    std::vector<uint8_t> recs;
    std::vector<uint16_t> recSizes;
    std::vector<uint32_t> recHashes;
  };

  MergedInfo mergedTpi;
  MergedInfo mergedIpi;

  uint64_t nbTypeRecords = 0;
  uint64_t nbTypeRecordsBytes = 0;
};

TpiSource *makeTpiSource(COFFLinkerContext &ctx, ObjFile *f);
TpiSource *makeTypeServerSource(COFFLinkerContext &ctx,
                                PDBInputFile *pdbInputFile);
TpiSource *makeUseTypeServerSource(COFFLinkerContext &ctx, ObjFile *file,
                                   llvm::codeview::TypeServer2Record ts);
TpiSource *makePrecompSource(COFFLinkerContext &ctx, ObjFile *file);
TpiSource *makeUsePrecompSource(COFFLinkerContext &ctx, ObjFile *file,
                                llvm::codeview::PrecompRecord ts);

} // namespace lld::coff

#endif
