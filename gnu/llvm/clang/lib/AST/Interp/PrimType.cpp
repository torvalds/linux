//===--- PrimType.cpp - Types for the constexpr VM --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PrimType.h"
#include "Boolean.h"
#include "Floating.h"
#include "FunctionPointer.h"
#include "IntegralAP.h"
#include "MemberPointer.h"
#include "Pointer.h"

using namespace clang;
using namespace clang::interp;

namespace clang {
namespace interp {

size_t primSize(PrimType Type) {
  TYPE_SWITCH(Type, return sizeof(T));
  llvm_unreachable("not a primitive type");
}

} // namespace interp
} // namespace clang
