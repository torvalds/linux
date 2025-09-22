//===---- llvm/Support/Discriminator.h -- Discriminator Utils ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the constants and utility functions for discriminators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DISCRIMINATOR_H
#define LLVM_SUPPORT_DISCRIMINATOR_H

#include "llvm/Support/Error.h"
#include <assert.h>

// Utility functions for encoding / decoding discriminators.
/// With a given unsigned int \p U, use up to 13 bits to represent it.
/// old_bit 1~5  --> new_bit 1~5
/// old_bit 6~12 --> new_bit 7~13
/// new_bit_6 is 0 if higher bits (7~13) are all 0
static inline unsigned getPrefixEncodingFromUnsigned(unsigned U) {
  U &= 0xfff;
  return U > 0x1f ? (((U & 0xfe0) << 1) | (U & 0x1f) | 0x20) : U;
}

/// Reverse transformation as getPrefixEncodingFromUnsigned.
static inline unsigned getUnsignedFromPrefixEncoding(unsigned U) {
  if (U & 1)
    return 0;
  U >>= 1;
  return (U & 0x20) ? (((U >> 1) & 0xfe0) | (U & 0x1f)) : (U & 0x1f);
}

/// Returns the next component stored in discriminator.
static inline unsigned getNextComponentInDiscriminator(unsigned D) {
  if ((D & 1) == 0)
    return D >> ((D & 0x40) ? 14 : 7);
  else
    return D >> 1;
}

static inline unsigned encodeComponent(unsigned C) {
  return (C == 0) ? 1U : (getPrefixEncodingFromUnsigned(C) << 1);
}

static inline unsigned encodingBits(unsigned C) {
  return (C == 0) ? 1 : (C > 0x1f ? 14 : 7);
}

// Some constants used in FS Discriminators.
//
namespace llvm {
namespace sampleprof {
enum FSDiscriminatorPass {
  Base = 0,
  Pass0 = 0,
  Pass1 = 1,
  Pass2 = 2,
  Pass3 = 3,
  Pass4 = 4,
  PassLast = 4,
};
} // namespace sampleprof

// The number of bits reserved for the base discrimininator. The base
// discriminaitor starts from bit 0.
static const unsigned BaseDiscriminatorBitWidth = 8;

// The number of bits reserved for each FS discriminator pass.
static const unsigned FSDiscriminatorBitWidth = 6;

// Return the number of FS passes, excluding the pass adding the base
// discriminators.
// The number of passes for FS discriminators. Note that the total
// number of discriminaitor bits, i.e.
// BaseDiscriminatorBitWidth
//  + FSDiscriminatorBitWidth * getNumFSPasses()
// needs to fit in an unsigned int type.
static inline unsigned getNumFSPasses() {
  return static_cast<unsigned>(sampleprof::FSDiscriminatorPass::PassLast);
}

// Return the ending bit for FSPass P.
static inline unsigned getFSPassBitEnd(sampleprof::FSDiscriminatorPass P) {
  unsigned I = static_cast<unsigned>(P);
  assert(I <= getNumFSPasses() && "Invalid FS discriminator pass number.");
  return BaseDiscriminatorBitWidth + I * FSDiscriminatorBitWidth - 1;
}

// Return the begining bit for FSPass P.
static inline unsigned getFSPassBitBegin(sampleprof::FSDiscriminatorPass P) {
  if (P == sampleprof::FSDiscriminatorPass::Base)
    return 0;
  unsigned I = static_cast<unsigned>(P);
  assert(I <= getNumFSPasses() && "Invalid FS discriminator pass number.");
  return getFSPassBitEnd(static_cast<sampleprof::FSDiscriminatorPass>(I - 1)) +
         1;
}

// Return the beginning bit for the last FSPass.
static inline int getLastFSPassBitBegin() {
  return getFSPassBitBegin(
      static_cast<sampleprof::FSDiscriminatorPass>(getNumFSPasses()));
}

// Return the ending bit for the last FSPass.
static inline unsigned getLastFSPassBitEnd() {
  return getFSPassBitEnd(
      static_cast<sampleprof::FSDiscriminatorPass>(getNumFSPasses()));
}

// Return the beginning bit for the base (first) FSPass.
static inline unsigned getBaseFSBitBegin() { return 0; }

// Return the ending bit for the base (first) FSPass.
static inline unsigned getBaseFSBitEnd() {
  return BaseDiscriminatorBitWidth - 1;
}

// Set bits in range of [0 .. n] to 1. Used in FS Discriminators.
static inline unsigned getN1Bits(int N) {
  // Work around the g++ bug that folding "(1U << (N + 1)) - 1" to 0.
  if (N == 31)
    return 0xFFFFFFFF;
  assert((N < 32) && "N is invalid");
  return (1U << (N + 1)) - 1;
}

} // namespace llvm

#endif /* LLVM_SUPPORT_DISCRIMINATOR_H */
