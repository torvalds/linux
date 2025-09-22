//===- TypeStreamMerger.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPESTREAMMERGER_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPESTREAMMERGER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/Support/Error.h"

namespace llvm {
template <typename T> class SmallVectorImpl;
namespace codeview {

class TypeIndex;
struct GloballyHashedType;
class GlobalTypeTableBuilder;
class MergingTypeTableBuilder;

/// Used to forward information about PCH.OBJ (precompiled) files, when
/// applicable.
struct PCHMergerInfo {
  uint32_t PCHSignature{};
  uint32_t EndPrecompIndex = ~0U;
};

/// Merge one set of type records into another.  This method assumes
/// that all records are type records, and there are no Id records present.
///
/// \param Dest The table to store the re-written type records into.
///
/// \param SourceToDest A vector, indexed by the TypeIndex in the source
/// type stream, that contains the index of the corresponding type record
/// in the destination stream.
///
/// \param Types The collection of types to merge in.
///
/// \returns Error::success() if the operation succeeded, otherwise an
/// appropriate error code.
Error mergeTypeRecords(MergingTypeTableBuilder &Dest,
                       SmallVectorImpl<TypeIndex> &SourceToDest,
                       const CVTypeArray &Types);

/// Merge one set of id records into another.  This method assumes
/// that all records are id records, and there are no Type records present.
/// However, since Id records can refer back to Type records, this method
/// assumes that the referenced type records have also been merged into
/// another type stream (for example using the above method), and accepts
/// the mapping from source to dest for that stream so that it can re-write
/// the type record mappings accordingly.
///
/// \param Dest The table to store the re-written id records into.
///
/// \param Types The mapping to use for the type records that these id
/// records refer to.
///
/// \param SourceToDest A vector, indexed by the TypeIndex in the source
/// id stream, that contains the index of the corresponding id record
/// in the destination stream.
///
/// \param Ids The collection of id records to merge in.
///
/// \returns Error::success() if the operation succeeded, otherwise an
/// appropriate error code.
Error mergeIdRecords(MergingTypeTableBuilder &Dest, ArrayRef<TypeIndex> Types,
                     SmallVectorImpl<TypeIndex> &SourceToDest,
                     const CVTypeArray &Ids);

/// Merge a unified set of type and id records, splitting them into
/// separate output streams.
///
/// \param DestIds The table to store the re-written id records into.
///
/// \param DestTypes the table to store the re-written type records into.
///
/// \param SourceToDest A vector, indexed by the TypeIndex in the source
/// id stream, that contains the index of the corresponding id record
/// in the destination stream.
///
/// \param IdsAndTypes The collection of id records to merge in.
///
/// \returns Error::success() if the operation succeeded, otherwise an
/// appropriate error code.
Error mergeTypeAndIdRecords(MergingTypeTableBuilder &DestIds,
                            MergingTypeTableBuilder &DestTypes,
                            SmallVectorImpl<TypeIndex> &SourceToDest,
                            const CVTypeArray &IdsAndTypes,
                            std::optional<PCHMergerInfo> &PCHInfo);

Error mergeTypeAndIdRecords(GlobalTypeTableBuilder &DestIds,
                            GlobalTypeTableBuilder &DestTypes,
                            SmallVectorImpl<TypeIndex> &SourceToDest,
                            const CVTypeArray &IdsAndTypes,
                            ArrayRef<GloballyHashedType> Hashes,
                            std::optional<PCHMergerInfo> &PCHInfo);

Error mergeTypeRecords(GlobalTypeTableBuilder &Dest,
                       SmallVectorImpl<TypeIndex> &SourceToDest,
                       const CVTypeArray &Types,
                       ArrayRef<GloballyHashedType> Hashes,
                       std::optional<PCHMergerInfo> &PCHInfo);

Error mergeIdRecords(GlobalTypeTableBuilder &Dest, ArrayRef<TypeIndex> Types,
                     SmallVectorImpl<TypeIndex> &SourceToDest,
                     const CVTypeArray &Ids,
                     ArrayRef<GloballyHashedType> Hashes);

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_TYPESTREAMMERGER_H
