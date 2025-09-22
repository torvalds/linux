//===- RecordLayout.cpp - Layout information for a struct/union -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the RecordLayout interface.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/RecordLayout.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/TargetCXXABI.h"
#include "clang/Basic/TargetInfo.h"
#include <cassert>

using namespace clang;

void ASTRecordLayout::Destroy(ASTContext &Ctx) {
  if (CXXInfo) {
    CXXInfo->~CXXRecordLayoutInfo();
    Ctx.Deallocate(CXXInfo);
  }
  this->~ASTRecordLayout();
  Ctx.Deallocate(this);
}

ASTRecordLayout::ASTRecordLayout(const ASTContext &Ctx, CharUnits size,
                                 CharUnits alignment,
                                 CharUnits preferredAlignment,
                                 CharUnits unadjustedAlignment,
                                 CharUnits requiredAlignment,
                                 CharUnits datasize,
                                 ArrayRef<uint64_t> fieldoffsets)
    : Size(size), DataSize(datasize), Alignment(alignment),
      PreferredAlignment(preferredAlignment),
      UnadjustedAlignment(unadjustedAlignment),
      RequiredAlignment(requiredAlignment) {
  FieldOffsets.append(Ctx, fieldoffsets.begin(), fieldoffsets.end());
}

// Constructor for C++ records.
ASTRecordLayout::ASTRecordLayout(
    const ASTContext &Ctx, CharUnits size, CharUnits alignment,
    CharUnits preferredAlignment, CharUnits unadjustedAlignment,
    CharUnits requiredAlignment, bool hasOwnVFPtr, bool hasExtendableVFPtr,
    CharUnits vbptroffset, CharUnits datasize, ArrayRef<uint64_t> fieldoffsets,
    CharUnits nonvirtualsize, CharUnits nonvirtualalignment,
    CharUnits preferrednvalignment, CharUnits SizeOfLargestEmptySubobject,
    const CXXRecordDecl *PrimaryBase, bool IsPrimaryBaseVirtual,
    const CXXRecordDecl *BaseSharingVBPtr, bool EndsWithZeroSizedObject,
    bool LeadsWithZeroSizedBase, const BaseOffsetsMapTy &BaseOffsets,
    const VBaseOffsetsMapTy &VBaseOffsets)
    : Size(size), DataSize(datasize), Alignment(alignment),
      PreferredAlignment(preferredAlignment),
      UnadjustedAlignment(unadjustedAlignment),
      RequiredAlignment(requiredAlignment),
      CXXInfo(new (Ctx) CXXRecordLayoutInfo) {
  FieldOffsets.append(Ctx, fieldoffsets.begin(), fieldoffsets.end());

  CXXInfo->PrimaryBase.setPointer(PrimaryBase);
  CXXInfo->PrimaryBase.setInt(IsPrimaryBaseVirtual);
  CXXInfo->NonVirtualSize = nonvirtualsize;
  CXXInfo->NonVirtualAlignment = nonvirtualalignment;
  CXXInfo->PreferredNVAlignment = preferrednvalignment;
  CXXInfo->SizeOfLargestEmptySubobject = SizeOfLargestEmptySubobject;
  CXXInfo->BaseOffsets = BaseOffsets;
  CXXInfo->VBaseOffsets = VBaseOffsets;
  CXXInfo->HasOwnVFPtr = hasOwnVFPtr;
  CXXInfo->VBPtrOffset = vbptroffset;
  CXXInfo->HasExtendableVFPtr = hasExtendableVFPtr;
  CXXInfo->BaseSharingVBPtr = BaseSharingVBPtr;
  CXXInfo->EndsWithZeroSizedObject = EndsWithZeroSizedObject;
  CXXInfo->LeadsWithZeroSizedBase = LeadsWithZeroSizedBase;

#ifndef NDEBUG
    if (const CXXRecordDecl *PrimaryBase = getPrimaryBase()) {
      if (isPrimaryBaseVirtual()) {
        if (Ctx.getTargetInfo().getCXXABI().hasPrimaryVBases()) {
          assert(getVBaseClassOffset(PrimaryBase).isZero() &&
                 "Primary virtual base must be at offset 0!");
        }
      } else {
        assert(getBaseClassOffset(PrimaryBase).isZero() &&
               "Primary base must be at offset 0!");
      }
    }
#endif
}
