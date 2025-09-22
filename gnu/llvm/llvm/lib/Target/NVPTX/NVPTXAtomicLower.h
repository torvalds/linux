//===-- NVPTXAtomicLower.h - Lower atomics of local memory ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Lower atomics of local memory to simple load/stores
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXATOMICLOWER_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXATOMICLOWER_H

namespace llvm {
class FunctionPass;

extern FunctionPass *createNVPTXAtomicLowerPass();
} // end namespace llvm

#endif
