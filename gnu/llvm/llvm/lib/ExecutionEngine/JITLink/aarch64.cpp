//===---- aarch64.cpp - Generic JITLink aarch64 edge kinds, utilities -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing aarch64 objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/aarch64.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {
namespace aarch64 {

const char NullPointerContent[8] = {0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00};

const char PointerJumpStubContent[12] = {
    0x10, 0x00, 0x00, (char)0x90u, // ADRP x16, <imm>@page21
    0x10, 0x02, 0x40, (char)0xf9u, // LDR x16, [x16, <imm>@pageoff12]
    0x00, 0x02, 0x1f, (char)0xd6u  // BR  x16
};

const char *getEdgeKindName(Edge::Kind R) {
  switch (R) {
  case Pointer64:
    return "Pointer64";
  case Pointer32:
    return "Pointer32";
  case Delta64:
    return "Delta64";
  case Delta32:
    return "Delta32";
  case NegDelta64:
    return "NegDelta64";
  case NegDelta32:
    return "NegDelta32";
  case Branch26PCRel:
    return "Branch26PCRel";
  case MoveWide16:
    return "MoveWide16";
  case LDRLiteral19:
    return "LDRLiteral19";
  case TestAndBranch14PCRel:
    return "TestAndBranch14PCRel";
  case CondBranch19PCRel:
    return "CondBranch19PCRel";
  case ADRLiteral21:
    return "ADRLiteral21";
  case Page21:
    return "Page21";
  case PageOffset12:
    return "PageOffset12";
  case RequestGOTAndTransformToPage21:
    return "RequestGOTAndTransformToPage21";
  case RequestGOTAndTransformToPageOffset12:
    return "RequestGOTAndTransformToPageOffset12";
  case RequestGOTAndTransformToDelta32:
    return "RequestGOTAndTransformToDelta32";
  case RequestTLVPAndTransformToPage21:
    return "RequestTLVPAndTransformToPage21";
  case RequestTLVPAndTransformToPageOffset12:
    return "RequestTLVPAndTransformToPageOffset12";
  case RequestTLSDescEntryAndTransformToPage21:
    return "RequestTLSDescEntryAndTransformToPage21";
  case RequestTLSDescEntryAndTransformToPageOffset12:
    return "RequestTLSDescEntryAndTransformToPageOffset12";
  default:
    return getGenericEdgeKindName(static_cast<Edge::Kind>(R));
  }
}

} // namespace aarch64
} // namespace jitlink
} // namespace llvm
