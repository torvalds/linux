//===-- XCoreMCTargetDesc.h - XCore Target Descriptions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides XCore specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_MCTARGETDESC_XCOREMCTARGETDESC_H
#define LLVM_LIB_TARGET_XCORE_MCTARGETDESC_XCOREMCTARGETDESC_H

// Defines symbolic names for XCore registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "XCoreGenRegisterInfo.inc"

// Defines symbolic names for the XCore instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "XCoreGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "XCoreGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_XCORE_MCTARGETDESC_XCOREMCTARGETDESC_H
