//===------------------------- MemberPointer.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_MEMBER_POINTER_H
#define LLVM_CLANG_AST_INTERP_MEMBER_POINTER_H

#include "Pointer.h"
#include <optional>

namespace clang {
class ASTContext;
namespace interp {

class Context;
class FunctionPointer;

class MemberPointer final {
private:
  Pointer Base;
  const Decl *Dcl = nullptr;
  int32_t PtrOffset = 0;

  MemberPointer(Pointer Base, const Decl *Dcl, int32_t PtrOffset)
      : Base(Base), Dcl(Dcl), PtrOffset(PtrOffset) {}

public:
  MemberPointer() = default;
  MemberPointer(Pointer Base, const Decl *Dcl) : Base(Base), Dcl(Dcl) {}
  MemberPointer(uint32_t Address, const Descriptor *D) {
    // We only reach this for Address == 0, when creating a null member pointer.
    assert(Address == 0);
  }

  MemberPointer(const Decl *D) : Dcl(D) {
    assert((isa<FieldDecl, IndirectFieldDecl, CXXMethodDecl>(D)));
  }

  uint64_t getIntegerRepresentation() const {
    assert(
        false &&
        "getIntegerRepresentation() shouldn't be reachable for MemberPointers");
    return 17;
  }

  std::optional<Pointer> toPointer(const Context &Ctx) const;

  FunctionPointer toFunctionPointer(const Context &Ctx) const;

  Pointer getBase() const {
    if (PtrOffset < 0)
      return Base.atField(-PtrOffset);
    return Base.atFieldSub(PtrOffset);
  }
  bool isMemberFunctionPointer() const {
    return isa_and_nonnull<CXXMethodDecl>(Dcl);
  }
  const CXXMethodDecl *getMemberFunction() const {
    return dyn_cast_if_present<CXXMethodDecl>(Dcl);
  }
  const FieldDecl *getField() const {
    return dyn_cast_if_present<FieldDecl>(Dcl);
  }

  bool hasDecl() const { return Dcl; }
  const Decl *getDecl() const { return Dcl; }

  MemberPointer atInstanceBase(unsigned Offset) const {
    if (Base.isZero())
      return MemberPointer(Base, Dcl, Offset);
    return MemberPointer(this->Base, Dcl, Offset + PtrOffset);
  }

  MemberPointer takeInstance(Pointer Instance) const {
    assert(this->Base.isZero());
    return MemberPointer(Instance, this->Dcl, this->PtrOffset);
  }

  APValue toAPValue(const ASTContext &) const;

  bool isZero() const { return Base.isZero() && !Dcl; }
  bool hasBase() const { return !Base.isZero(); }

  void print(llvm::raw_ostream &OS) const {
    OS << "MemberPtr(" << Base << " " << (const void *)Dcl << " + " << PtrOffset
       << ")";
  }

  std::string toDiagnosticString(const ASTContext &Ctx) const {
    return "FIXME";
  }

  ComparisonCategoryResult compare(const MemberPointer &RHS) const {
    if (this->Dcl == RHS.Dcl)
      return ComparisonCategoryResult::Equal;
    return ComparisonCategoryResult::Unordered;
  }
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, MemberPointer FP) {
  FP.print(OS);
  return OS;
}

} // namespace interp
} // namespace clang

#endif
