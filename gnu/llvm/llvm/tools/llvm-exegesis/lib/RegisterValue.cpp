//===-- RegisterValue.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterValue.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace exegesis {

static APFloat getFloatValue(const fltSemantics &FltSemantics,
                             PredefinedValues Value) {
  switch (Value) {
  case PredefinedValues::POS_ZERO:
    return APFloat::getZero(FltSemantics);
  case PredefinedValues::NEG_ZERO:
    return APFloat::getZero(FltSemantics, true);
  case PredefinedValues::ONE:
    return APFloat(FltSemantics, "1");
  case PredefinedValues::TWO:
    return APFloat(FltSemantics, "2");
  case PredefinedValues::INF:
    return APFloat::getInf(FltSemantics);
  case PredefinedValues::QNAN:
    return APFloat::getQNaN(FltSemantics);
  case PredefinedValues::SMALLEST_NORM:
    return APFloat::getSmallestNormalized(FltSemantics);
  case PredefinedValues::LARGEST:
    return APFloat::getLargest(FltSemantics);
  case PredefinedValues::ULP:
    return APFloat::getSmallest(FltSemantics);
  case PredefinedValues::ONE_PLUS_ULP:
    auto Output = getFloatValue(FltSemantics, PredefinedValues::ONE);
    Output.next(false);
    return Output;
  }
  llvm_unreachable("Unhandled exegesis::PredefinedValues");
}

APInt bitcastFloatValue(const fltSemantics &FltSemantics,
                        PredefinedValues Value) {
  return getFloatValue(FltSemantics, Value).bitcastToAPInt();
}

} // namespace exegesis
} // namespace llvm
