//===-- NVPTXTargetInfo.h - NVPTX Target Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_TARGETINFO_NVPTXTARGETINFO_H
#define LLVM_LIB_TARGET_NVPTX_TARGETINFO_NVPTXTARGETINFO_H

namespace llvm {

class Target;

Target &getTheNVPTXTarget32();
Target &getTheNVPTXTarget64();

} // namespace llvm

#endif // LLVM_LIB_TARGET_NVPTX_TARGETINFO_NVPTXTARGETINFO_H
