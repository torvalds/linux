//===-- DirectXSubtarget.cpp - DirectX Subtarget Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the DirectX-specific subclass of TargetSubtarget.
///
//===----------------------------------------------------------------------===//

#include "DirectXSubtarget.h"
#include "DirectXTargetLowering.h"

using namespace llvm;

#define DEBUG_TYPE "directx-subtarget"

#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_TARGET_DESC
#include "DirectXGenSubtargetInfo.inc"

DirectXSubtarget::DirectXSubtarget(const Triple &TT, StringRef CPU,
                                   StringRef FS, const DirectXTargetMachine &TM)
    : DirectXGenSubtargetInfo(TT, CPU, CPU, FS), FL(*this), TL(TM, *this) {}

void DirectXSubtarget::anchor() {}
