//===-- X86ShuffleDecodeConstantPool.h - X86 shuffle decode -----*-C++-*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Define several functions to decode x86 specific shuffle semantics using
// constants from the constant pool.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86SHUFFLEDECODECONSTANTPOOL_H
#define LLVM_LIB_TARGET_X86_X86SHUFFLEDECODECONSTANTPOOL_H

//===----------------------------------------------------------------------===//
//  Vector Mask Decoding
//===----------------------------------------------------------------------===//

namespace llvm {
class Constant;
template <typename T> class SmallVectorImpl;

/// Decode a PSHUFB mask from an IR-level vector constant.
void DecodePSHUFBMask(const Constant *C, unsigned Width,
                      SmallVectorImpl<int> &ShuffleMask);

/// Decode a VPERMILP variable mask from an IR-level vector constant.
void DecodeVPERMILPMask(const Constant *C, unsigned ElSize, unsigned Width,
                        SmallVectorImpl<int> &ShuffleMask);

/// Decode a VPERMILP2 variable mask from an IR-level vector constant.
void DecodeVPERMIL2PMask(const Constant *C, unsigned M2Z, unsigned ElSize,
                         unsigned Width, SmallVectorImpl<int> &ShuffleMask);

/// Decode a VPPERM variable mask from an IR-level vector constant.
void DecodeVPPERMMask(const Constant *C, unsigned Width,
                      SmallVectorImpl<int> &ShuffleMask);

} // llvm namespace

#endif
