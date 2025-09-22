//===----- ppc64.cpp - Generic JITLink ppc64 edge kinds, utilities ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing 64-bit PowerPC objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ppc64.h"

#define DEBUG_TYPE "jitlink"

namespace llvm::jitlink::ppc64 {

const char NullPointerContent[8] = {0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00};

const char PointerJumpStubContent_little[20] = {
    0x18,       0x00, 0x41,       (char)0xf8, // std r2, 24(r1)
    0x00,       0x00, (char)0x82, 0x3d,       // addis r12, r2, OffHa
    0x00,       0x00, (char)0x8c, (char)0xe9, // ld r12, OffLo(r12)
    (char)0xa6, 0x03, (char)0x89, 0x7d,       // mtctr r12
    0x20,       0x04, (char)0x80, 0x4e,       // bctr
};

const char PointerJumpStubContent_big[20] = {
    (char)0xf8, 0x41,       0x00, 0x18,       // std r2, 24(r1)
    0x3d,       (char)0x82, 0x00, 0x00,       // addis r12, r2, OffHa
    (char)0xe9, (char)0x8c, 0x00, 0x00,       // ld r12, OffLo(r12)
    0x7d,       (char)0x89, 0x03, (char)0xa6, // mtctr r12
    0x4e,       (char)0x80, 0x04, 0x20,       // bctr
};

// TODO: We can use prefixed instructions if LLJIT is running on power10.
const char PointerJumpStubNoTOCContent_little[32] = {
    (char)0xa6, 0x02,       (char)0x88, 0x7d,       // mflr 12
    0x05,       (char)0x00, (char)0x9f, 0x42,       // bcl 20,31,.+4
    (char)0xa6, 0x02,       0x68,       0x7d,       // mflr 11
    (char)0xa6, 0x03,       (char)0x88, 0x7d,       // mtlr 12
    0x00,       0x00,       (char)0x8b, 0x3d,       // addis 12,11,OffHa
    0x00,       0x00,       (char)0x8c, (char)0xe9, // ld 12, OffLo(12)
    (char)0xa6, 0x03,       (char)0x89, 0x7d,       // mtctr 12
    0x20,       0x04,       (char)0x80, 0x4e,       // bctr
};

const char PointerJumpStubNoTOCContent_big[32] = {
    0x7d,       (char)0x88, 0x02, (char)0xa6, // mflr 12
    0x42,       (char)0x9f, 0x00, 0x05,       // bcl 20,31,.+4
    0x7d,       0x68,       0x02, (char)0xa6, // mflr 11
    0x7d,       (char)0x88, 0x03, (char)0xa6, // mtlr 12
    0x3d,       (char)0x8b, 0x00, 0x00,       // addis 12,11,OffHa
    (char)0xe9, (char)0x8c, 0x00, 0x00,       // ld 12, OffLo(12)
    0x7d,       (char)0x89, 0x03, (char)0xa6, // mtctr 12
    0x4e,       (char)0x80, 0x04, 0x20,       // bctr
};

const char *getEdgeKindName(Edge::Kind K) {
  switch (K) {
  case Pointer64:
    return "Pointer64";
  case Pointer32:
    return "Pointer32";
  case Pointer16:
    return "Pointer16";
  case Pointer16DS:
    return "Pointer16DS";
  case Pointer16HA:
    return "Pointer16HA";
  case Pointer16HI:
    return "Pointer16HI";
  case Pointer16HIGH:
    return "Pointer16HIGH";
  case Pointer16HIGHA:
    return "Pointer16HIGHA";
  case Pointer16HIGHER:
    return "Pointer16HIGHER";
  case Pointer16HIGHERA:
    return "Pointer16HIGHERA";
  case Pointer16HIGHEST:
    return "Pointer16HIGHEST";
  case Pointer16HIGHESTA:
    return "Pointer16HIGHESTA";
  case Pointer16LO:
    return "Pointer16LO";
  case Pointer16LODS:
    return "Pointer16LODS";
  case Pointer14:
    return "Pointer14";
  case Delta64:
    return "Delta64";
  case Delta34:
    return "Delta34";
  case Delta32:
    return "Delta32";
  case NegDelta32:
    return "NegDelta32";
  case Delta16:
    return "Delta16";
  case Delta16HA:
    return "Delta16HA";
  case Delta16HI:
    return "Delta16HI";
  case Delta16LO:
    return "Delta16LO";
  case TOC:
    return "TOC";
  case TOCDelta16:
    return "TOCDelta16";
  case TOCDelta16DS:
    return "TOCDelta16DS";
  case TOCDelta16HA:
    return "TOCDelta16HA";
  case TOCDelta16HI:
    return "TOCDelta16HI";
  case TOCDelta16LO:
    return "TOCDelta16LO";
  case TOCDelta16LODS:
    return "TOCDelta16LODS";
  case RequestGOTAndTransformToDelta34:
    return "RequestGOTAndTransformToDelta34";
  case CallBranchDelta:
    return "CallBranchDelta";
  case CallBranchDeltaRestoreTOC:
    return "CallBranchDeltaRestoreTOC";
  case RequestCall:
    return "RequestCall";
  case RequestCallNoTOC:
    return "RequestCallNoTOC";
  case RequestTLSDescInGOTAndTransformToTOCDelta16HA:
    return "RequestTLSDescInGOTAndTransformToTOCDelta16HA";
  case RequestTLSDescInGOTAndTransformToTOCDelta16LO:
    return "RequestTLSDescInGOTAndTransformToTOCDelta16LO";
  case RequestTLSDescInGOTAndTransformToDelta34:
    return "RequestTLSDescInGOTAndTransformToDelta34";
  default:
    return getGenericEdgeKindName(static_cast<Edge::Kind>(K));
  }
}

} // end namespace llvm::jitlink::ppc64
