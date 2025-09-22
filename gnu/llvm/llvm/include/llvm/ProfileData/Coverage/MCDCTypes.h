//===- MCDCTypes.h - Types related to MC/DC Coverage ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Types related to MC/DC Coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_COVERAGE_MCDCTYPES_H
#define LLVM_PROFILEDATA_COVERAGE_MCDCTYPES_H

#include <array>
#include <cassert>
#include <type_traits>
#include <variant>

namespace llvm::coverage::mcdc {

/// The ID for MCDCBranch.
using ConditionID = int16_t;
using ConditionIDs = std::array<ConditionID, 2>;

struct DecisionParameters {
  /// Byte Index of Bitmap Coverage Object for a Decision Region.
  unsigned BitmapIdx;

  /// Number of Conditions used for a Decision Region.
  uint16_t NumConditions;

  DecisionParameters() = delete;
  DecisionParameters(unsigned BitmapIdx, unsigned NumConditions)
      : BitmapIdx(BitmapIdx), NumConditions(NumConditions) {
    assert(NumConditions > 0);
  }
};

struct BranchParameters {
  /// IDs used to represent a branch region and other branch regions
  /// evaluated based on True and False branches.
  ConditionID ID;
  ConditionIDs Conds;

  BranchParameters() = delete;
  BranchParameters(ConditionID ID, const ConditionIDs &Conds)
      : ID(ID), Conds(Conds) {
    assert(ID >= 0);
  }
};

/// The type of MC/DC-specific parameters.
using Parameters =
    std::variant<std::monostate, DecisionParameters, BranchParameters>;

/// Check and get underlying params in MCDCParams.
/// \tparam MaybeConstInnerParameters Type to get. May be const.
/// \tparam MaybeConstMCDCParameters Expected inferred. May be const.
/// \param MCDCParams May be const.
template <class MaybeConstInnerParameters, class MaybeConstMCDCParameters>
static auto &getParams(MaybeConstMCDCParameters &MCDCParams) {
  using InnerParameters =
      typename std::remove_const<MaybeConstInnerParameters>::type;
  MaybeConstInnerParameters *Params = std::get_if<InnerParameters>(&MCDCParams);
  assert(Params && "InnerParameters unavailable");
  return *Params;
}

} // namespace llvm::coverage::mcdc

#endif // LLVM_PROFILEDATA_COVERAGE_MCDCTYPES_H
