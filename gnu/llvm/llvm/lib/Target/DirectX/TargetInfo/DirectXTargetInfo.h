//===-- DirectXTargetInfo.h - DircetX Target Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DIRECTX_TARGETINFO_DIRECTXTARGETINFO_H
#define LLVM_DIRECTX_TARGETINFO_DIRECTXTARGETINFO_H

namespace llvm {
class Target;

Target &getTheDirectXTarget();
} // namespace llvm

#endif // LLVM_DIRECTX_TARGETINFO_DIRECTXTARGETINFO_H
