//===- TypeReferenceTracker.cpp ------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeReferenceTracker.h"

#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Object/COFF.h"

using namespace llvm;
using namespace llvm::pdb;
using namespace llvm::codeview;

// LazyRandomTypeCollection doesn't appear to expose the number of records, so
// just iterate up front to find out.
static uint32_t getNumRecordsInCollection(LazyRandomTypeCollection &Types) {
  uint32_t NumTypes = 0;
  for (std::optional<TypeIndex> TI = Types.getFirst(); TI;
       TI = Types.getNext(*TI))
    ++NumTypes;
  return NumTypes;
}

TypeReferenceTracker::TypeReferenceTracker(InputFile &File)
    : File(File), Types(File.types()),
      Ids(File.isPdb() ? &File.ids() : nullptr) {
  NumTypeRecords = getNumRecordsInCollection(Types);
  TypeReferenced.resize(NumTypeRecords, false);

  // If this is a PDB, ids are stored separately, so make a separate bit vector.
  if (Ids) {
    NumIdRecords = getNumRecordsInCollection(*Ids);
    IdReferenced.resize(NumIdRecords, false);
  }

  // Get the TpiStream pointer for forward decl resolution if this is a pdb.
  // Build the hash map to enable resolving forward decls.
  if (File.isPdb()) {
    Tpi = &cantFail(File.pdb().getPDBTpiStream());
    Tpi->buildHashMap();
  }
}

void TypeReferenceTracker::mark() {
  // Walk type roots:
  // - globals
  // - modi symbols
  // - LF_UDT_MOD_SRC_LINE? VC always links these in.
  for (const SymbolGroup &SG : File.symbol_groups()) {
    if (File.isObj()) {
      for (const auto &SS : SG.getDebugSubsections()) {
        // FIXME: Are there other type-referencing subsections? Inlinees?
        // Probably for IDs.
        if (SS.kind() != DebugSubsectionKind::Symbols)
          continue;

        CVSymbolArray Symbols;
        BinaryStreamReader Reader(SS.getRecordData());
        cantFail(Reader.readArray(Symbols, Reader.getLength()));
        for (const CVSymbol &S : Symbols)
          addTypeRefsFromSymbol(S);
      }
    } else if (SG.hasDebugStream()) {
      for (const CVSymbol &S : SG.getPdbModuleStream().getSymbolArray())
        addTypeRefsFromSymbol(S);
    }
  }

  // Walk globals and mark types referenced from globals.
  if (File.isPdb() && File.pdb().hasPDBGlobalsStream()) {
    SymbolStream &SymStream = cantFail(File.pdb().getPDBSymbolStream());
    GlobalsStream &GS = cantFail(File.pdb().getPDBGlobalsStream());
    for (uint32_t PubSymOff : GS.getGlobalsTable()) {
      CVSymbol Sym = SymStream.readRecord(PubSymOff);
      addTypeRefsFromSymbol(Sym);
    }
  }

  // FIXME: Should we walk Ids?
}

void TypeReferenceTracker::addOneTypeRef(TiRefKind RefKind, TypeIndex RefTI) {
  // If it's simple or already seen, no need to add to work list.
  BitVector &TypeOrIdReferenced =
      (Ids && RefKind == TiRefKind::IndexRef) ? IdReferenced : TypeReferenced;
  if (RefTI.isSimple() || TypeOrIdReferenced.test(RefTI.toArrayIndex()))
    return;

  // Otherwise, mark it seen and add it to the work list.
  TypeOrIdReferenced.set(RefTI.toArrayIndex());
  RefWorklist.push_back({RefKind, RefTI});
}

void TypeReferenceTracker::addTypeRefsFromSymbol(const CVSymbol &Sym) {
  SmallVector<TiReference, 4> DepList;
  // FIXME: Check for failure.
  discoverTypeIndicesInSymbol(Sym, DepList);
  addReferencedTypes(Sym.content(), DepList);
  markReferencedTypes();
}

void TypeReferenceTracker::addReferencedTypes(ArrayRef<uint8_t> RecData,
                                              ArrayRef<TiReference> DepList) {
  for (const auto &Ref : DepList) {
    // FIXME: Report OOB slice instead of truncating.
    ArrayRef<uint8_t> ByteSlice =
        RecData.drop_front(Ref.Offset).take_front(4 * Ref.Count);
    ArrayRef<TypeIndex> TIs(
        reinterpret_cast<const TypeIndex *>(ByteSlice.data()),
        ByteSlice.size() / 4);

    // If this is a PDB and this is an item reference, track it in the IPI
    // bitvector. Otherwise, it's a type ref, or there is only one stream.
    for (TypeIndex RefTI : TIs)
      addOneTypeRef(Ref.Kind, RefTI);
  }
}

void TypeReferenceTracker::markReferencedTypes() {
  while (!RefWorklist.empty()) {
    TiRefKind RefKind;
    TypeIndex RefTI;
    std::tie(RefKind, RefTI) = RefWorklist.pop_back_val();
    std::optional<CVType> Rec = (Ids && RefKind == TiRefKind::IndexRef)
                                    ? Ids->tryGetType(RefTI)
                                    : Types.tryGetType(RefTI);
    if (!Rec)
      continue; // FIXME: Report a reference to a non-existant type.

    SmallVector<TiReference, 4> DepList;
    // FIXME: Check for failure.
    discoverTypeIndices(*Rec, DepList);
    addReferencedTypes(Rec->content(), DepList);

    // If this is a tag kind and this is a PDB input, mark the complete type as
    // referenced.
    // FIXME: This limitation makes this feature somewhat useless on object file
    // inputs.
    if (Tpi) {
      switch (Rec->kind()) {
      default:
        break;
      case LF_CLASS:
      case LF_INTERFACE:
      case LF_STRUCTURE:
      case LF_UNION:
      case LF_ENUM:
        addOneTypeRef(TiRefKind::TypeRef,
                      cantFail(Tpi->findFullDeclForForwardRef(RefTI)));
        break;
      }
    }
  }
}
