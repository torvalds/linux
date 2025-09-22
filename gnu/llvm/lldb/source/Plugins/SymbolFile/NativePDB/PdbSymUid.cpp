//===-- PdbSymUid.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PdbSymUid.h"

using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;

namespace {
struct GenericIdRepr {
  uint64_t tag : 4;
  uint64_t data : 60;
};

struct CompilandIdRepr {
  uint64_t tag : 4;
  uint64_t modi : 16;
  uint64_t unused : 44;
};

struct CompilandSymIdRepr {
  uint64_t tag : 4;
  uint64_t modi : 16;
  uint64_t offset : 32;
  uint64_t unused : 12;
};

struct GlobalSymIdRepr {
  uint64_t tag : 4;
  uint64_t offset : 32;
  uint64_t pub : 1;
  uint64_t unused : 27;
};

struct TypeSymIdRepr {
  uint64_t tag : 4;
  uint64_t index : 32;
  uint64_t ipi : 1;
  uint64_t unused : 27;
};

struct FieldListMemberIdRepr {
  uint64_t tag : 4;
  uint64_t index : 32;
  uint64_t offset : 16;
  uint64_t unused : 12;
};

static_assert(sizeof(CompilandIdRepr) == 8, "Invalid structure size!");
static_assert(sizeof(CompilandSymIdRepr) == 8, "Invalid structure size!");
static_assert(sizeof(GlobalSymIdRepr) == 8, "Invalid structure size!");
static_assert(sizeof(TypeSymIdRepr) == 8, "Invalid structure size!");
static_assert(sizeof(FieldListMemberIdRepr) == 8, "Invalid structure size!");
} // namespace

template <typename OutT, typename InT> static OutT repr_cast(const InT &value) {
  OutT result;
  ::memcpy(&result, &value, sizeof(value));
  return result;
}

PdbSymUid::PdbSymUid(const PdbCompilandId &cid) {
  CompilandIdRepr repr;
  ::memset(&repr, 0, sizeof(repr));
  repr.modi = cid.modi;
  repr.tag = static_cast<uint64_t>(PdbSymUidKind::Compiland);
  m_repr = repr_cast<uint64_t>(repr);
}

PdbSymUid::PdbSymUid(const PdbCompilandSymId &csid) {
  CompilandSymIdRepr repr;
  ::memset(&repr, 0, sizeof(repr));
  repr.modi = csid.modi;
  repr.offset = csid.offset;
  repr.tag = static_cast<uint64_t>(PdbSymUidKind::CompilandSym);
  m_repr = repr_cast<uint64_t>(repr);
}

PdbSymUid::PdbSymUid(const PdbGlobalSymId &gsid) {
  GlobalSymIdRepr repr;
  ::memset(&repr, 0, sizeof(repr));
  repr.pub = gsid.is_public;
  repr.offset = gsid.offset;
  repr.tag = static_cast<uint64_t>(PdbSymUidKind::GlobalSym);
  m_repr = repr_cast<uint64_t>(repr);
}

PdbSymUid::PdbSymUid(const PdbTypeSymId &tsid) {
  TypeSymIdRepr repr;
  ::memset(&repr, 0, sizeof(repr));
  repr.index = tsid.index.getIndex();
  repr.ipi = tsid.is_ipi;
  repr.tag = static_cast<uint64_t>(PdbSymUidKind::Type);
  m_repr = repr_cast<uint64_t>(repr);
}

PdbSymUid::PdbSymUid(const PdbFieldListMemberId &flmid) {
  FieldListMemberIdRepr repr;
  ::memset(&repr, 0, sizeof(repr));
  repr.index = flmid.index.getIndex();
  repr.offset = flmid.offset;
  repr.tag = static_cast<uint64_t>(PdbSymUidKind::FieldListMember);
  m_repr = repr_cast<uint64_t>(repr);
}

PdbSymUidKind PdbSymUid::kind() const {
  GenericIdRepr generic = repr_cast<GenericIdRepr>(m_repr);
  return static_cast<PdbSymUidKind>(generic.tag);
}

PdbCompilandId PdbSymUid::asCompiland() const {
  assert(kind() == PdbSymUidKind::Compiland);
  auto repr = repr_cast<CompilandIdRepr>(m_repr);
  PdbCompilandId result;
  result.modi = repr.modi;
  return result;
}

PdbCompilandSymId PdbSymUid::asCompilandSym() const {
  assert(kind() == PdbSymUidKind::CompilandSym);
  auto repr = repr_cast<CompilandSymIdRepr>(m_repr);
  PdbCompilandSymId result;
  result.modi = repr.modi;
  result.offset = repr.offset;
  return result;
}

PdbGlobalSymId PdbSymUid::asGlobalSym() const {
  assert(kind() == PdbSymUidKind::GlobalSym ||
         kind() == PdbSymUidKind::PublicSym);
  auto repr = repr_cast<GlobalSymIdRepr>(m_repr);
  PdbGlobalSymId result;
  result.is_public = repr.pub;
  result.offset = repr.offset;
  return result;
}

PdbTypeSymId PdbSymUid::asTypeSym() const {
  assert(kind() == PdbSymUidKind::Type);
  auto repr = repr_cast<TypeSymIdRepr>(m_repr);
  PdbTypeSymId result;
  result.index.setIndex(repr.index);
  result.is_ipi = repr.ipi;
  return result;
}

PdbFieldListMemberId PdbSymUid::asFieldListMember() const {
  assert(kind() == PdbSymUidKind::FieldListMember);
  auto repr = repr_cast<FieldListMemberIdRepr>(m_repr);
  PdbFieldListMemberId result;
  result.index.setIndex(repr.index);
  result.offset = repr.offset;
  return result;
}
