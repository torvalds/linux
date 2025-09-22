//===- TpiHashing.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_TPIHASHING_H
#define LLVM_DEBUGINFO_PDB_NATIVE_TPIHASHING_H

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {

Expected<uint32_t> hashTypeRecord(const llvm::codeview::CVType &Type);

struct TagRecordHash {
  explicit TagRecordHash(codeview::ClassRecord CR, uint32_t Full,
                         uint32_t Forward)
      : FullRecordHash(Full), ForwardDeclHash(Forward), Class(std::move(CR)) {
    State = 0;
  }

  explicit TagRecordHash(codeview::EnumRecord ER, uint32_t Full,
                         uint32_t Forward)
      : FullRecordHash(Full), ForwardDeclHash(Forward), Enum(std::move(ER)) {
    State = 1;
  }

  explicit TagRecordHash(codeview::UnionRecord UR, uint32_t Full,
                         uint32_t Forward)
      : FullRecordHash(Full), ForwardDeclHash(Forward), Union(std::move(UR)) {
    State = 2;
  }

  uint32_t FullRecordHash;
  uint32_t ForwardDeclHash;

  codeview::TagRecord &getRecord() {
    switch (State) {
    case 0:
      return Class;
    case 1:
      return Enum;
    case 2:
      return Union;
    }
    llvm_unreachable("unreachable!");
  }

private:
  union {
    codeview::ClassRecord Class;
    codeview::EnumRecord Enum;
    codeview::UnionRecord Union;
  };

  uint8_t State = 0;
};

/// Given a CVType referring to a class, structure, union, or enum, compute
/// the hash of its forward decl and full decl.
Expected<TagRecordHash> hashTagRecord(const codeview::CVType &Type);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_TPIHASHING_H
