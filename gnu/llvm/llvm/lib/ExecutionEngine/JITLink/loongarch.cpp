//===--- loongarch.cpp - Generic JITLink loongarch edge kinds, utilities --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing loongarch objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/loongarch.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {
namespace loongarch {

const char NullPointerContent[8] = {0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00};

const uint8_t LA64StubContent[StubEntrySize] = {
    0x14, 0x00, 0x00, 0x1a, // pcalau12i $t8, %page20(imm)
    0x94, 0x02, 0xc0, 0x28, // ld.d $t8, $t8, %pageoff12(imm)
    0x80, 0x02, 0x00, 0x4c  // jr $t8
};

const uint8_t LA32StubContent[StubEntrySize] = {
    0x14, 0x00, 0x00, 0x1a, // pcalau12i $t8, %page20(imm)
    0x94, 0x02, 0x80, 0x28, // ld.w $t8, $t8, %pageoff12(imm)
    0x80, 0x02, 0x00, 0x4c  // jr $t8
};

const char *getEdgeKindName(Edge::Kind K) {
#define KIND_NAME_CASE(K)                                                      \
  case K:                                                                      \
    return #K;

  switch (K) {
    KIND_NAME_CASE(Pointer64)
    KIND_NAME_CASE(Pointer32)
    KIND_NAME_CASE(Delta32)
    KIND_NAME_CASE(NegDelta32)
    KIND_NAME_CASE(Delta64)
    KIND_NAME_CASE(Branch26PCRel)
    KIND_NAME_CASE(Page20)
    KIND_NAME_CASE(PageOffset12)
    KIND_NAME_CASE(RequestGOTAndTransformToPage20)
    KIND_NAME_CASE(RequestGOTAndTransformToPageOffset12)
  default:
    return getGenericEdgeKindName(K);
  }
#undef KIND_NAME_CASE
}

} // namespace loongarch
} // namespace jitlink
} // namespace llvm
