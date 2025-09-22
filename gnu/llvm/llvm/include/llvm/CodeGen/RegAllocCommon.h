//===- RegAllocCommon.h - Utilities shared between allocators ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCCOMMON_H
#define LLVM_CODEGEN_REGALLOCCOMMON_H

#include "llvm/CodeGen/Register.h"
#include <functional>

namespace llvm {

class TargetRegisterClass;
class TargetRegisterInfo;
class MachineRegisterInfo;

/// Filter function for register classes during regalloc. Default register class
/// filter is nullptr, where all registers should be allocated.
typedef std::function<bool(const TargetRegisterInfo &TRI,
                           const MachineRegisterInfo &MRI, const Register Reg)>
    RegAllocFilterFunc;
}

#endif // LLVM_CODEGEN_REGALLOCCOMMON_H
