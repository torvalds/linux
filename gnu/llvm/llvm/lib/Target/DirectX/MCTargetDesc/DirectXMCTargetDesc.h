//===- DirectXMCTargetDesc.h - DirectX Target Interface ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains DirectX target interface.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DIRECTX_DIRECTXMCTARGETDESC_H
#define LLVM_DIRECTX_DIRECTXMCTARGETDESC_H

// Include DirectX stub register info
#define GET_REGINFO_ENUM
#include "DirectXGenRegisterInfo.inc"

// Include DirectX stub instruction info
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "DirectXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "DirectXGenSubtargetInfo.inc"

#endif // LLVM_DIRECTX_DIRECTXMCTARGETDESC_H
