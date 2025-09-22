//===- TypeReferenceTracker.h --------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_TYPEREFERENCETRACKER_H
#define LLVM_TOOLS_LLVMPDBDUMP_TYPEREFERENCETRACKER_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {

class TpiStream;

/// Maintains bitvector to track whether a type was referenced by a symbol
/// record.
class TypeReferenceTracker {
public:
  TypeReferenceTracker(InputFile &File);

  // Do the work of marking referenced types.
  void mark();

  // Return true if a symbol record transitively references this type.
  bool isTypeReferenced(codeview::TypeIndex TI) {
    return TI.toArrayIndex() <= NumTypeRecords &&
           TypeReferenced.test(TI.toArrayIndex());
  }

private:
  void addTypeRefsFromSymbol(const codeview::CVSymbol &Sym);

  // Mark types on this list as referenced.
  void addReferencedTypes(ArrayRef<uint8_t> RecData,
                          ArrayRef<codeview::TiReference> Refs);

  // Consume all types on the worklist.
  void markReferencedTypes();

  void addOneTypeRef(codeview::TiRefKind RefKind, codeview::TypeIndex RefTI);

  InputFile &File;
  codeview::LazyRandomTypeCollection &Types;
  codeview::LazyRandomTypeCollection *Ids = nullptr;
  TpiStream *Tpi = nullptr;
  BitVector TypeReferenced;
  BitVector IdReferenced;
  SmallVector<std::pair<codeview::TiRefKind, codeview::TypeIndex>, 10>
      RefWorklist;
  uint32_t NumTypeRecords = 0;
  uint32_t NumIdRecords = 0;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_TOOLS_LLVMPDBDUMP_TYPEREFERENCETRACKER_H
