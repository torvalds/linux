//===-- RecordOps.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Operations on records (structs, classes, and unions).
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/FlowSensitive/RecordOps.h"

#define DEBUG_TYPE "dataflow"

namespace clang::dataflow {

static void copyField(const ValueDecl &Field, StorageLocation *SrcFieldLoc,
                      StorageLocation *DstFieldLoc, RecordStorageLocation &Dst,
                      Environment &Env) {
  assert(Field.getType()->isReferenceType() ||
         (SrcFieldLoc != nullptr && DstFieldLoc != nullptr));

  if (Field.getType()->isRecordType()) {
    copyRecord(cast<RecordStorageLocation>(*SrcFieldLoc),
               cast<RecordStorageLocation>(*DstFieldLoc), Env);
  } else if (Field.getType()->isReferenceType()) {
    Dst.setChild(Field, SrcFieldLoc);
  } else {
    if (Value *Val = Env.getValue(*SrcFieldLoc))
      Env.setValue(*DstFieldLoc, *Val);
    else
      Env.clearValue(*DstFieldLoc);
  }
}

static void copySyntheticField(QualType FieldType, StorageLocation &SrcFieldLoc,
                               StorageLocation &DstFieldLoc, Environment &Env) {
  if (FieldType->isRecordType()) {
    copyRecord(cast<RecordStorageLocation>(SrcFieldLoc),
               cast<RecordStorageLocation>(DstFieldLoc), Env);
  } else {
    if (Value *Val = Env.getValue(SrcFieldLoc))
      Env.setValue(DstFieldLoc, *Val);
    else
      Env.clearValue(DstFieldLoc);
  }
}

void copyRecord(RecordStorageLocation &Src, RecordStorageLocation &Dst,
                Environment &Env) {
  auto SrcType = Src.getType().getCanonicalType().getUnqualifiedType();
  auto DstType = Dst.getType().getCanonicalType().getUnqualifiedType();

  auto SrcDecl = SrcType->getAsCXXRecordDecl();
  auto DstDecl = DstType->getAsCXXRecordDecl();

  [[maybe_unused]] bool compatibleTypes =
      SrcType == DstType ||
      (SrcDecl != nullptr && DstDecl != nullptr &&
       (SrcDecl->isDerivedFrom(DstDecl) || DstDecl->isDerivedFrom(SrcDecl)));

  LLVM_DEBUG({
    if (!compatibleTypes) {
      llvm::dbgs() << "Source type " << Src.getType() << "\n";
      llvm::dbgs() << "Destination type " << Dst.getType() << "\n";
    }
  });
  assert(compatibleTypes);

  if (SrcType == DstType || (SrcDecl != nullptr && DstDecl != nullptr &&
                             SrcDecl->isDerivedFrom(DstDecl))) {
    for (auto [Field, DstFieldLoc] : Dst.children())
      copyField(*Field, Src.getChild(*Field), DstFieldLoc, Dst, Env);
    for (const auto &[Name, DstFieldLoc] : Dst.synthetic_fields())
      copySyntheticField(DstFieldLoc->getType(), Src.getSyntheticField(Name),
                         *DstFieldLoc, Env);
  } else {
    for (auto [Field, SrcFieldLoc] : Src.children())
      copyField(*Field, SrcFieldLoc, Dst.getChild(*Field), Dst, Env);
    for (const auto &[Name, SrcFieldLoc] : Src.synthetic_fields())
      copySyntheticField(SrcFieldLoc->getType(), *SrcFieldLoc,
                         Dst.getSyntheticField(Name), Env);
  }
}

bool recordsEqual(const RecordStorageLocation &Loc1, const Environment &Env1,
                  const RecordStorageLocation &Loc2, const Environment &Env2) {
  LLVM_DEBUG({
    if (Loc2.getType().getCanonicalType().getUnqualifiedType() !=
        Loc1.getType().getCanonicalType().getUnqualifiedType()) {
      llvm::dbgs() << "Loc1 type " << Loc1.getType() << "\n";
      llvm::dbgs() << "Loc2 type " << Loc2.getType() << "\n";
    }
  });
  assert(Loc2.getType().getCanonicalType().getUnqualifiedType() ==
         Loc1.getType().getCanonicalType().getUnqualifiedType());

  for (auto [Field, FieldLoc1] : Loc1.children()) {
    StorageLocation *FieldLoc2 = Loc2.getChild(*Field);

    assert(Field->getType()->isReferenceType() ||
           (FieldLoc1 != nullptr && FieldLoc2 != nullptr));

    if (Field->getType()->isRecordType()) {
      if (!recordsEqual(cast<RecordStorageLocation>(*FieldLoc1), Env1,
                        cast<RecordStorageLocation>(*FieldLoc2), Env2))
        return false;
    } else if (Field->getType()->isReferenceType()) {
      if (FieldLoc1 != FieldLoc2)
        return false;
    } else if (Env1.getValue(*FieldLoc1) != Env2.getValue(*FieldLoc2)) {
      return false;
    }
  }

  for (const auto &[Name, SynthFieldLoc1] : Loc1.synthetic_fields()) {
    if (SynthFieldLoc1->getType()->isRecordType()) {
      if (!recordsEqual(
              *cast<RecordStorageLocation>(SynthFieldLoc1), Env1,
              cast<RecordStorageLocation>(Loc2.getSyntheticField(Name)), Env2))
        return false;
    } else if (Env1.getValue(*SynthFieldLoc1) !=
               Env2.getValue(Loc2.getSyntheticField(Name))) {
      return false;
    }
  }

  return true;
}

} // namespace clang::dataflow
