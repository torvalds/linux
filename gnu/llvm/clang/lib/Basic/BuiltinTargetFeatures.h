//===-- CodeGenFunction.h - Target features for builtin ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the internal required target features for builtin.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_BUILTINTARGETFEATURES_H
#define LLVM_CLANG_LIB_BASIC_BUILTINTARGETFEATURES_H
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

using llvm::StringRef;

namespace clang {
namespace Builtin {
/// TargetFeatures - This class is used to check whether the builtin function
/// has the required tagert specific features. It is able to support the
/// combination of ','(and), '|'(or), and '()'. By default, the priority of
/// ',' is higher than that of '|' .
/// E.g:
/// A,B|C means the builtin function requires both A and B, or C.
/// If we want the builtin function requires both A and B, or both A and C,
/// there are two ways: A,B|A,C or A,(B|C).
/// The FeaturesList should not contain spaces, and brackets must appear in
/// pairs.
class TargetFeatures {
  struct FeatureListStatus {
    bool HasFeatures;
    StringRef CurFeaturesList;
  };

  const llvm::StringMap<bool> &CallerFeatureMap;

  FeatureListStatus getAndFeatures(StringRef FeatureList) {
    int InParentheses = 0;
    bool HasFeatures = true;
    size_t SubexpressionStart = 0;
    for (size_t i = 0, e = FeatureList.size(); i < e; ++i) {
      char CurrentToken = FeatureList[i];
      switch (CurrentToken) {
      default:
        break;
      case '(':
        if (InParentheses == 0)
          SubexpressionStart = i + 1;
        ++InParentheses;
        break;
      case ')':
        --InParentheses;
        assert(InParentheses >= 0 && "Parentheses are not in pair");
        [[fallthrough]];
      case '|':
      case ',':
        if (InParentheses == 0) {
          if (HasFeatures && i != SubexpressionStart) {
            StringRef F = FeatureList.slice(SubexpressionStart, i);
            HasFeatures = CurrentToken == ')' ? hasRequiredFeatures(F)
                                              : CallerFeatureMap.lookup(F);
          }
          SubexpressionStart = i + 1;
          if (CurrentToken == '|') {
            return {HasFeatures, FeatureList.substr(SubexpressionStart)};
          }
        }
        break;
      }
    }
    assert(InParentheses == 0 && "Parentheses are not in pair");
    if (HasFeatures && SubexpressionStart != FeatureList.size())
      HasFeatures =
          CallerFeatureMap.lookup(FeatureList.substr(SubexpressionStart));
    return {HasFeatures, StringRef()};
  }

public:
  bool hasRequiredFeatures(StringRef FeatureList) {
    FeatureListStatus FS = {false, FeatureList};
    while (!FS.HasFeatures && !FS.CurFeaturesList.empty())
      FS = getAndFeatures(FS.CurFeaturesList);
    return FS.HasFeatures;
  }

  TargetFeatures(const llvm::StringMap<bool> &CallerFeatureMap)
      : CallerFeatureMap(CallerFeatureMap) {}
};

} // namespace Builtin
} // namespace clang
#endif /* CLANG_LIB_BASIC_BUILTINTARGETFEATURES_H */
