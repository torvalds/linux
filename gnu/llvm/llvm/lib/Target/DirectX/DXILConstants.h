//===- DXILConstants.h - Essential DXIL constants -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains essential DXIL constants.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_DIRECTX_DXILCONSTANTS_H
#define LLVM_LIB_TARGET_DIRECTX_DXILCONSTANTS_H

namespace llvm {
namespace dxil {

#define DXIL_OP_ENUM
#include "DXILOperation.inc"
#undef DXIL_OP_ENUM

} // namespace dxil
} // namespace llvm

#endif
