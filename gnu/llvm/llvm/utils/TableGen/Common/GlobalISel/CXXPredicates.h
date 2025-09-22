//===- CXXPredicates.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Contains utilities related to handling C++ code in MIR patterns for
///   GlobalISel. C++ predicates need to be expanded, and then stored in a
///   static pool until they can be emitted.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_MIRPATTERNS_CXXPREDICATES_H
#define LLVM_UTILS_MIRPATTERNS_CXXPREDICATES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace gi {

/// Entry into the static pool of all CXX Predicate code. This contains
/// fully expanded C++ code.
///
/// The static pool is hidden inside the object and can be accessed through
/// getAllMatchCode/getAllApplyCode
///
/// Note that CXXPattern trims C++ code, so the Code is already expected to be
/// free of leading/trailing whitespace.
class CXXPredicateCode {
  using CXXPredicateCodePool =
      DenseMap<hash_code, std::unique_ptr<CXXPredicateCode>>;
  static CXXPredicateCodePool AllCXXMatchCode;
  static CXXPredicateCodePool AllCXXCustomActionCode;

  /// Sorts a `CXXPredicateCodePool` by their IDs and returns it.
  static std::vector<const CXXPredicateCode *>
  getSorted(const CXXPredicateCodePool &Pool);

  /// Gets an instance of `CXXPredicateCode` for \p Code, or returns an already
  /// existing one.
  static const CXXPredicateCode &get(CXXPredicateCodePool &Pool,
                                     std::string Code);

  CXXPredicateCode(std::string Code, unsigned ID);

public:
  static const CXXPredicateCode &getMatchCode(std::string Code) {
    return get(AllCXXMatchCode, std::move(Code));
  }

  static const CXXPredicateCode &getCustomActionCode(std::string Code) {
    return get(AllCXXCustomActionCode, std::move(Code));
  }

  static std::vector<const CXXPredicateCode *> getAllMatchCode() {
    return getSorted(AllCXXMatchCode);
  }

  static std::vector<const CXXPredicateCode *> getAllCustomActionsCode() {
    return getSorted(AllCXXCustomActionCode);
  }

  const std::string Code;
  const unsigned ID;
  const std::string BaseEnumName;

  bool needsUnreachable() const {
    return !StringRef(Code).starts_with("return");
  }

  std::string getEnumNameWithPrefix(StringRef Prefix) const {
    return Prefix.str() + BaseEnumName;
  }
};

} // namespace gi
} // end namespace llvm

#endif // ifndef LLVM_UTILS_MIRPATTERNS_CXXPREDICATES_H
