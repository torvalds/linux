//===- CXXPredicates.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "CXXPredicates.h"
#include "llvm/ADT/STLExtras.h"

namespace llvm {
namespace gi {

std::vector<const CXXPredicateCode *>
CXXPredicateCode::getSorted(const CXXPredicateCodePool &Pool) {
  std::vector<const CXXPredicateCode *> Out;
  std::transform(Pool.begin(), Pool.end(), std::back_inserter(Out),
                 [&](auto &Elt) { return Elt.second.get(); });
  sort(Out, [](const auto *A, const auto *B) { return A->ID < B->ID; });
  return Out;
}

const CXXPredicateCode &CXXPredicateCode::get(CXXPredicateCodePool &Pool,
                                              std::string Code) {
  // Check if we already have an identical piece of code, if not, create an
  // entry in the pool.
  const auto CodeHash = hash_value(Code);
  if (auto It = Pool.find(CodeHash); It != Pool.end())
    return *It->second;

  const auto ID = Pool.size();
  auto OwnedData = std::unique_ptr<CXXPredicateCode>(
      new CXXPredicateCode(std::move(Code), ID));
  const auto &DataRef = *OwnedData;
  Pool[CodeHash] = std::move(OwnedData);
  return DataRef;
}

// TODO: Make BaseEnumName prefix configurable.
CXXPredicateCode::CXXPredicateCode(std::string Code, unsigned ID)
    : Code(Code), ID(ID), BaseEnumName("GICombiner" + std::to_string(ID)) {}

CXXPredicateCode::CXXPredicateCodePool CXXPredicateCode::AllCXXMatchCode;
CXXPredicateCode::CXXPredicateCodePool CXXPredicateCode::AllCXXCustomActionCode;

} // namespace gi
} // namespace llvm
