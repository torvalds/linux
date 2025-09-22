//===-- HexagonTargetInfo.h - Hexagon Target Implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_TARGETINFO_HEXAGONTARGETINFO_H
#define LLVM_LIB_TARGET_HEXAGON_TARGETINFO_HEXAGONTARGETINFO_H

namespace llvm {

class Target;

Target &getTheHexagonTarget();

} // namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_TARGETINFO_HEXAGONTARGETINFO_H
