//===-- llvm/BinaryFormat/MachO.cpp - The MachO file format -----*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/TargetParser/ARMTargetParser.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

static MachO::CPUSubTypeX86 getX86SubType(const Triple &T) {
  assert(T.isX86());
  if (T.isArch32Bit())
    return MachO::CPU_SUBTYPE_I386_ALL;

  assert(T.isArch64Bit());
  if (T.getArchName() == "x86_64h")
    return MachO::CPU_SUBTYPE_X86_64_H;
  return MachO::CPU_SUBTYPE_X86_64_ALL;
}

static MachO::CPUSubTypeARM getARMSubType(const Triple &T) {
  assert(T.isARM() || T.isThumb());
  StringRef Arch = T.getArchName();
  ARM::ArchKind AK = ARM::parseArch(Arch);
  switch (AK) {
  default:
    return MachO::CPU_SUBTYPE_ARM_V7;
  case ARM::ArchKind::ARMV4T:
    return MachO::CPU_SUBTYPE_ARM_V4T;
  case ARM::ArchKind::ARMV5T:
  case ARM::ArchKind::ARMV5TE:
  case ARM::ArchKind::ARMV5TEJ:
    return MachO::CPU_SUBTYPE_ARM_V5;
  case ARM::ArchKind::ARMV6:
  case ARM::ArchKind::ARMV6K:
    return MachO::CPU_SUBTYPE_ARM_V6;
  case ARM::ArchKind::ARMV7A:
    return MachO::CPU_SUBTYPE_ARM_V7;
  case ARM::ArchKind::ARMV7S:
    return MachO::CPU_SUBTYPE_ARM_V7S;
  case ARM::ArchKind::ARMV7K:
    return MachO::CPU_SUBTYPE_ARM_V7K;
  case ARM::ArchKind::ARMV6M:
    return MachO::CPU_SUBTYPE_ARM_V6M;
  case ARM::ArchKind::ARMV7M:
    return MachO::CPU_SUBTYPE_ARM_V7M;
  case ARM::ArchKind::ARMV7EM:
    return MachO::CPU_SUBTYPE_ARM_V7EM;
  }
}

static MachO::CPUSubTypeARM64 getARM64SubType(const Triple &T) {
  assert(T.isAArch64());
  if (T.isArch32Bit())
    return (MachO::CPUSubTypeARM64)MachO::CPU_SUBTYPE_ARM64_32_V8;
  if (T.isArm64e())
    return MachO::CPU_SUBTYPE_ARM64E;

  return MachO::CPU_SUBTYPE_ARM64_ALL;
}

static MachO::CPUSubTypePowerPC getPowerPCSubType(const Triple &T) {
  return MachO::CPU_SUBTYPE_POWERPC_ALL;
}

static Error unsupported(const char *Str, const Triple &T) {
  return createStringError(std::errc::invalid_argument,
                           "Unsupported triple for mach-o cpu %s: %s", Str,
                           T.str().c_str());
}

Expected<uint32_t> MachO::getCPUType(const Triple &T) {
  if (!T.isOSBinFormatMachO())
    return unsupported("type", T);
  if (T.isX86() && T.isArch32Bit())
    return MachO::CPU_TYPE_X86;
  if (T.isX86() && T.isArch64Bit())
    return MachO::CPU_TYPE_X86_64;
  if (T.isARM() || T.isThumb())
    return MachO::CPU_TYPE_ARM;
  if (T.isAArch64())
    return T.isArch32Bit() ? MachO::CPU_TYPE_ARM64_32 : MachO::CPU_TYPE_ARM64;
  if (T.getArch() == Triple::ppc)
    return MachO::CPU_TYPE_POWERPC;
  if (T.getArch() == Triple::ppc64)
    return MachO::CPU_TYPE_POWERPC64;
  return unsupported("type", T);
}

Expected<uint32_t> MachO::getCPUSubType(const Triple &T) {
  if (!T.isOSBinFormatMachO())
    return unsupported("subtype", T);
  if (T.isX86())
    return getX86SubType(T);
  if (T.isARM() || T.isThumb())
    return getARMSubType(T);
  if (T.isAArch64() || T.getArch() == Triple::aarch64_32)
    return getARM64SubType(T);
  if (T.getArch() == Triple::ppc || T.getArch() == Triple::ppc64)
    return getPowerPCSubType(T);
  return unsupported("subtype", T);
}
