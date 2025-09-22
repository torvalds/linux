//===-- SystemInitializerLLGS.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemInitializerLLGS.h"

#if defined(__APPLE__)
#include "Plugins/ObjectFile/Mach-O/ObjectFileMachO.h"
using HostObjectFile = ObjectFileMachO;
#elif defined(_WIN32)
#include "Plugins/ObjectFile/PECOFF/ObjectFilePECOFF.h"
using HostObjectFile = ObjectFilePECOFF;
#else
#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
using HostObjectFile = ObjectFileELF;
#endif

#if defined(__arm64__) || defined(__aarch64__) || defined(_M_ARM64)
#define LLDB_TARGET_ARM64
#endif

#if defined(__arm__) || defined(__arm) || defined(_ARM) || defined(_M_ARM) ||  \
    defined(LLDB_TARGET_ARM64)
#define LLDB_TARGET_ARM
#include "Plugins/Instruction/ARM/EmulateInstructionARM.h"
#endif

#if defined(__loongarch__)
#define LLDB_TARGET_LoongArch
#include "Plugins/Instruction/LoongArch/EmulateInstructionLoongArch.h"
#endif

#if defined(__mips64__) || defined(mips64) || defined(__mips64) ||             \
    defined(__MIPS64__) || defined(_M_MIPS64)
#define LLDB_TARGET_MIPS64
#include "Plugins/Instruction/MIPS64/EmulateInstructionMIPS64.h"
#endif

#if defined(__mips__) || defined(mips) || defined(__mips) ||                   \
    defined(__MIPS__) || defined(_M_MIPS) || defined(LLDB_TARGET_MIPS64)
#define LLDB_TARGET_MIPS
#include "Plugins/Instruction/MIPS/EmulateInstructionMIPS.h"
#endif

#if defined(__riscv)
#define LLDB_TARGET_RISCV
#include "Plugins/Instruction/RISCV/EmulateInstructionRISCV.h"
#endif

using namespace lldb_private;

llvm::Error SystemInitializerLLGS::Initialize() {
  if (auto e = SystemInitializerCommon::Initialize())
    return e;

  HostObjectFile::Initialize();

#if defined(LLDB_TARGET_ARM) || defined(LLDB_TARGET_ARM64)
  EmulateInstructionARM::Initialize();
#endif
#if defined(LLDB_TARGET_LoongArch)
  EmulateInstructionLoongArch::Initialize();
#endif
#if defined(LLDB_TARGET_MIPS) || defined(LLDB_TARGET_MIPS64)
  EmulateInstructionMIPS::Initialize();
#endif
#if defined(LLDB_TARGET_MIPS64)
  EmulateInstructionMIPS64::Initialize();
#endif
#if defined(LLDB_TARGET_RISCV)
  EmulateInstructionRISCV::Initialize();
#endif

  return llvm::Error::success();
}

void SystemInitializerLLGS::Terminate() {
  HostObjectFile::Terminate();

#if defined(LLDB_TARGET_ARM) || defined(LLDB_TARGET_ARM64)
  EmulateInstructionARM::Terminate();
#endif
#if defined(LLDB_TARGET_LoongArch)
  EmulateInstructionLoongArch::Terminate();
#endif
#if defined(LLDB_TARGET_MIPS) || defined(LLDB_TARGET_MIPS64)
  EmulateInstructionMIPS::Terminate();
#endif
#if defined(LLDB_TARGET_MIPS64)
  EmulateInstructionMIPS64::Terminate();
#endif
#if defined(LLDB_TARGET_RISCV)
  EmulateInstructionRISCV::Terminate();
#endif

  SystemInitializerCommon::Terminate();
}
