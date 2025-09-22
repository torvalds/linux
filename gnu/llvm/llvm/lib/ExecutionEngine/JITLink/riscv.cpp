//===------ riscv.cpp - Generic JITLink riscv edge kinds, utilities -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing riscv objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/riscv.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {
namespace riscv {

const char *getEdgeKindName(Edge::Kind K) {
  switch (K) {
  case R_RISCV_32:
    return "R_RISCV_32";
  case R_RISCV_64:
    return "R_RISCV_64";
  case R_RISCV_BRANCH:
    return "R_RISCV_BRANCH";
  case R_RISCV_JAL:
    return "R_RISCV_JAL";
  case R_RISCV_CALL:
    return "R_RISCV_CALL";
  case R_RISCV_CALL_PLT:
    return "R_RISCV_CALL_PLT";
  case R_RISCV_GOT_HI20:
    return "R_RISCV_GOT_HI20";
  case R_RISCV_PCREL_HI20:
    return "R_RISCV_PCREL_HI20";
  case R_RISCV_PCREL_LO12_I:
    return "R_RISCV_PCREL_LO12_I";
  case R_RISCV_PCREL_LO12_S:
    return "R_RISCV_PCREL_LO12_S";
  case R_RISCV_HI20:
    return "R_RISCV_HI20";
  case R_RISCV_LO12_I:
    return "R_RISCV_LO12_I";
  case R_RISCV_LO12_S:
    return "R_RISCV_LO12_S";
  case R_RISCV_ADD8:
    return "R_RISCV_ADD8";
  case R_RISCV_ADD16:
    return "R_RISCV_ADD16";
  case R_RISCV_ADD32:
    return "R_RISCV_ADD32";
  case R_RISCV_ADD64:
    return "R_RISCV_ADD64";
  case R_RISCV_SUB8:
    return "R_RISCV_SUB8";
  case R_RISCV_SUB16:
    return "R_RISCV_SUB16";
  case R_RISCV_SUB32:
    return "R_RISCV_SUB32";
  case R_RISCV_SUB64:
    return "R_RISCV_SUB64";
  case R_RISCV_RVC_BRANCH:
    return "R_RISCV_RVC_BRANCH";
  case R_RISCV_RVC_JUMP:
    return "R_RISCV_RVC_JUMP";
  case R_RISCV_SUB6:
    return "R_RISCV_SUB6";
  case R_RISCV_SET6:
    return "R_RISCV_SET6";
  case R_RISCV_SET8:
    return "R_RISCV_SET8";
  case R_RISCV_SET16:
    return "R_RISCV_SET16";
  case R_RISCV_SET32:
    return "R_RISCV_SET32";
  case R_RISCV_32_PCREL:
    return "R_RISCV_32_PCREL";
  case CallRelaxable:
    return "CallRelaxable";
  case AlignRelaxable:
    return "AlignRelaxable";
  case NegDelta32:
    return "NegDelta32";
  }
  return getGenericEdgeKindName(K);
}
} // namespace riscv
} // namespace jitlink
} // namespace llvm
