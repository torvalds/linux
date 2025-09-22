//===------------------------- MemberPointer.cpp ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MemberPointer.h"
#include "Context.h"
#include "FunctionPointer.h"
#include "Program.h"
#include "Record.h"

namespace clang {
namespace interp {

std::optional<Pointer> MemberPointer::toPointer(const Context &Ctx) const {
  if (!Dcl || isa<FunctionDecl>(Dcl))
    return Base;
  const FieldDecl *FD = cast<FieldDecl>(Dcl);
  assert(FD);

  if (!Base.isBlockPointer())
    return std::nullopt;

  Pointer CastedBase =
      (PtrOffset < 0 ? Base.atField(-PtrOffset) : Base.atFieldSub(PtrOffset));

  const Record *BaseRecord = CastedBase.getRecord();
  if (!BaseRecord)
    return std::nullopt;

  assert(BaseRecord);
  if (FD->getParent() == BaseRecord->getDecl())
    return CastedBase.atField(BaseRecord->getField(FD)->Offset);

  const RecordDecl *FieldParent = FD->getParent();
  const Record *FieldRecord = Ctx.getRecord(FieldParent);

  unsigned Offset = 0;
  Offset += FieldRecord->getField(FD)->Offset;
  Offset += CastedBase.block()->getDescriptor()->getMetadataSize();

  if (Offset > CastedBase.block()->getSize())
    return std::nullopt;

  if (const RecordDecl *BaseDecl = Base.getDeclPtr().getRecord()->getDecl();
      BaseDecl != FieldParent)
    Offset += Ctx.collectBaseOffset(FieldParent, BaseDecl);

  if (Offset > CastedBase.block()->getSize())
    return std::nullopt;

  assert(Offset <= CastedBase.block()->getSize());
  return Pointer(const_cast<Block *>(Base.block()), Offset, Offset);
}

FunctionPointer MemberPointer::toFunctionPointer(const Context &Ctx) const {
  return FunctionPointer(Ctx.getProgram().getFunction(cast<FunctionDecl>(Dcl)));
}

APValue MemberPointer::toAPValue(const ASTContext &ASTCtx) const {
  if (isZero())
    return APValue(static_cast<ValueDecl *>(nullptr), /*IsDerivedMember=*/false,
                   /*Path=*/{});

  if (hasBase())
    return Base.toAPValue(ASTCtx);

  return APValue(cast<ValueDecl>(getDecl()), /*IsDerivedMember=*/false,
                 /*Path=*/{});
}

} // namespace interp
} // namespace clang
