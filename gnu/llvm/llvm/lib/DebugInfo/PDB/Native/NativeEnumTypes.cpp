//==- NativeEnumTypes.cpp - Native Type Enumerator impl ----------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumTypes.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeEnumTypes::NativeEnumTypes(NativeSession &PDBSession,
                                 LazyRandomTypeCollection &Types,
                                 std::vector<codeview::TypeLeafKind> Kinds)
    : Index(0), Session(PDBSession) {
  std::optional<TypeIndex> TI = Types.getFirst();
  while (TI) {
    CVType CVT = Types.getType(*TI);
    TypeLeafKind K = CVT.kind();
    if (llvm::is_contained(Kinds, K)) {
      // Don't add forward refs, we'll find those later while enumerating.
      if (!isUdtForwardRef(CVT))
        Matches.push_back(*TI);
    } else if (K == TypeLeafKind::LF_MODIFIER) {
      TypeIndex ModifiedTI = getModifiedType(CVT);
      if (!ModifiedTI.isSimple()) {
        CVType UnmodifiedCVT = Types.getType(ModifiedTI);
        // LF_MODIFIERs point to forward refs, but don't worry about that
        // here.  We're pushing the TypeIndex of the LF_MODIFIER itself,
        // so we'll worry about resolving forward refs later.
        if (llvm::is_contained(Kinds, UnmodifiedCVT.kind()))
          Matches.push_back(*TI);
      }
    }
    TI = Types.getNext(*TI);
  }
}

NativeEnumTypes::NativeEnumTypes(NativeSession &PDBSession,
                                 std::vector<codeview::TypeIndex> Indices)
    : Matches(std::move(Indices)), Index(0), Session(PDBSession) {}

uint32_t NativeEnumTypes::getChildCount() const {
  return static_cast<uint32_t>(Matches.size());
}

std::unique_ptr<PDBSymbol> NativeEnumTypes::getChildAtIndex(uint32_t N) const {
  if (N < Matches.size()) {
    SymIndexId Id = Session.getSymbolCache().findSymbolByTypeIndex(Matches[N]);
    return Session.getSymbolCache().getSymbolById(Id);
  }
  return nullptr;
}

std::unique_ptr<PDBSymbol> NativeEnumTypes::getNext() {
  return getChildAtIndex(Index++);
}

void NativeEnumTypes::reset() { Index = 0; }
