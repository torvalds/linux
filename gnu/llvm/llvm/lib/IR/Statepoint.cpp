//===-- IR/Statepoint.cpp -- gc.statepoint utilities ---  -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some utility functions to help recognize gc.statepoint
// intrinsics.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Statepoint.h"

using namespace llvm;

bool llvm::isStatepointDirectiveAttr(Attribute Attr) {
  return Attr.hasAttribute("statepoint-id") ||
         Attr.hasAttribute("statepoint-num-patch-bytes");
}

StatepointDirectives
llvm::parseStatepointDirectivesFromAttrs(AttributeList AS) {
  StatepointDirectives Result;

  Attribute AttrID = AS.getFnAttr("statepoint-id");
  uint64_t StatepointID;
  if (AttrID.isStringAttribute())
    if (!AttrID.getValueAsString().getAsInteger(10, StatepointID))
      Result.StatepointID = StatepointID;

  uint32_t NumPatchBytes;
  Attribute AttrNumPatchBytes = AS.getFnAttr("statepoint-num-patch-bytes");
  if (AttrNumPatchBytes.isStringAttribute())
    if (!AttrNumPatchBytes.getValueAsString().getAsInteger(10, NumPatchBytes))
      Result.NumPatchBytes = NumPatchBytes;

  return Result;
}
