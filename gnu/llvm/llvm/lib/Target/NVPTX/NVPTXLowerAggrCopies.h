//===-- llvm/lib/Target/NVPTX/NVPTXLowerAggrCopies.h ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the NVIDIA specific lowering of
// aggregate copies
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXLOWERAGGRCOPIES_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXLOWERAGGRCOPIES_H

namespace llvm {
class FunctionPass;

FunctionPass *createLowerAggrCopies();
}

#endif
