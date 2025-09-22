//===-- Host.cpp - Implement OS Host Detection ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the operating system Host detection.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/Host.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <string.h>

// Include the platform-specific parts of this class.
#ifdef LLVM_ON_UNIX
#include "Unix/Host.inc"
#include <sched.h>
#endif
#ifdef _WIN32
#include "Windows/Host.inc"
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifdef __MVS__
#include "llvm/Support/BCD.h"
#endif
#if defined(__APPLE__)
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/machine.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#ifdef _AIX
#include <sys/systemcfg.h>
#endif
#if defined(__sun__) && defined(__svr4__)
#include <kstat.h>
#endif

#define DEBUG_TYPE "host-detection"

//===----------------------------------------------------------------------===//
//
//  Implementations of the CPU detection routines
//
//===----------------------------------------------------------------------===//

using namespace llvm;

static std::unique_ptr<llvm::MemoryBuffer>
    LLVM_ATTRIBUTE_UNUSED getProcCpuinfoContent() {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
      llvm::MemoryBuffer::getFileAsStream("/proc/cpuinfo");
  if (std::error_code EC = Text.getError()) {
    llvm::errs() << "Can't read "
                 << "/proc/cpuinfo: " << EC.message() << "\n";
    return nullptr;
  }
  return std::move(*Text);
}

StringRef sys::detail::getHostCPUNameForPowerPC(StringRef ProcCpuinfoContent) {
  // Access to the Processor Version Register (PVR) on PowerPC is privileged,
  // and so we must use an operating-system interface to determine the current
  // processor type. On Linux, this is exposed through the /proc/cpuinfo file.
  const char *generic = "generic";

  // The cpu line is second (after the 'processor: 0' line), so if this
  // buffer is too small then something has changed (or is wrong).
  StringRef::const_iterator CPUInfoStart = ProcCpuinfoContent.begin();
  StringRef::const_iterator CPUInfoEnd = ProcCpuinfoContent.end();

  StringRef::const_iterator CIP = CPUInfoStart;

  StringRef::const_iterator CPUStart = nullptr;
  size_t CPULen = 0;

  // We need to find the first line which starts with cpu, spaces, and a colon.
  // After the colon, there may be some additional spaces and then the cpu type.
  while (CIP < CPUInfoEnd && CPUStart == nullptr) {
    if (CIP < CPUInfoEnd && *CIP == '\n')
      ++CIP;

    if (CIP < CPUInfoEnd && *CIP == 'c') {
      ++CIP;
      if (CIP < CPUInfoEnd && *CIP == 'p') {
        ++CIP;
        if (CIP < CPUInfoEnd && *CIP == 'u') {
          ++CIP;
          while (CIP < CPUInfoEnd && (*CIP == ' ' || *CIP == '\t'))
            ++CIP;

          if (CIP < CPUInfoEnd && *CIP == ':') {
            ++CIP;
            while (CIP < CPUInfoEnd && (*CIP == ' ' || *CIP == '\t'))
              ++CIP;

            if (CIP < CPUInfoEnd) {
              CPUStart = CIP;
              while (CIP < CPUInfoEnd && (*CIP != ' ' && *CIP != '\t' &&
                                          *CIP != ',' && *CIP != '\n'))
                ++CIP;
              CPULen = CIP - CPUStart;
            }
          }
        }
      }
    }

    if (CPUStart == nullptr)
      while (CIP < CPUInfoEnd && *CIP != '\n')
        ++CIP;
  }

  if (CPUStart == nullptr)
    return generic;

  return StringSwitch<const char *>(StringRef(CPUStart, CPULen))
      .Case("604e", "604e")
      .Case("604", "604")
      .Case("7400", "7400")
      .Case("7410", "7400")
      .Case("7447", "7400")
      .Case("7455", "7450")
      .Case("G4", "g4")
      .Case("POWER4", "970")
      .Case("PPC970FX", "970")
      .Case("PPC970MP", "970")
      .Case("G5", "g5")
      .Case("POWER5", "g5")
      .Case("A2", "a2")
      .Case("POWER6", "pwr6")
      .Case("POWER7", "pwr7")
      .Case("POWER8", "pwr8")
      .Case("POWER8E", "pwr8")
      .Case("POWER8NVL", "pwr8")
      .Case("POWER9", "pwr9")
      .Case("POWER10", "pwr10")
      .Case("POWER11", "pwr11")
      // FIXME: If we get a simulator or machine with the capabilities of
      // mcpu=future, we should revisit this and add the name reported by the
      // simulator/machine.
      .Default(generic);
}

StringRef sys::detail::getHostCPUNameForARM(StringRef ProcCpuinfoContent) {
  // The cpuid register on arm is not accessible from user space. On Linux,
  // it is exposed through the /proc/cpuinfo file.

  // Read 32 lines from /proc/cpuinfo, which should contain the CPU part line
  // in all cases.
  SmallVector<StringRef, 32> Lines;
  ProcCpuinfoContent.split(Lines, "\n");

  // Look for the CPU implementer line.
  StringRef Implementer;
  StringRef Hardware;
  StringRef Part;
  for (unsigned I = 0, E = Lines.size(); I != E; ++I) {
    if (Lines[I].starts_with("CPU implementer"))
      Implementer = Lines[I].substr(15).ltrim("\t :");
    if (Lines[I].starts_with("Hardware"))
      Hardware = Lines[I].substr(8).ltrim("\t :");
    if (Lines[I].starts_with("CPU part"))
      Part = Lines[I].substr(8).ltrim("\t :");
  }

  if (Implementer == "0x41") { // ARM Ltd.
    // MSM8992/8994 may give cpu part for the core that the kernel is running on,
    // which is undeterministic and wrong. Always return cortex-a53 for these SoC.
    if (Hardware.ends_with("MSM8994") || Hardware.ends_with("MSM8996"))
      return "cortex-a53";


    // The CPU part is a 3 digit hexadecimal number with a 0x prefix. The
    // values correspond to the "Part number" in the CP15/c0 register. The
    // contents are specified in the various processor manuals.
    // This corresponds to the Main ID Register in Technical Reference Manuals.
    // and is used in programs like sys-utils
    return StringSwitch<const char *>(Part)
        .Case("0x926", "arm926ej-s")
        .Case("0xb02", "mpcore")
        .Case("0xb36", "arm1136j-s")
        .Case("0xb56", "arm1156t2-s")
        .Case("0xb76", "arm1176jz-s")
        .Case("0xc05", "cortex-a5")
        .Case("0xc07", "cortex-a7")
        .Case("0xc08", "cortex-a8")
        .Case("0xc09", "cortex-a9")
        .Case("0xc0f", "cortex-a15")
        .Case("0xc0e", "cortex-a17")
        .Case("0xc20", "cortex-m0")
        .Case("0xc23", "cortex-m3")
        .Case("0xc24", "cortex-m4")
        .Case("0xc27", "cortex-m7")
        .Case("0xd20", "cortex-m23")
        .Case("0xd21", "cortex-m33")
        .Case("0xd24", "cortex-m52")
        .Case("0xd22", "cortex-m55")
        .Case("0xd23", "cortex-m85")
        .Case("0xc18", "cortex-r8")
        .Case("0xd13", "cortex-r52")
        .Case("0xd16", "cortex-r52plus")
        .Case("0xd15", "cortex-r82")
        .Case("0xd14", "cortex-r82ae")
        .Case("0xd02", "cortex-a34")
        .Case("0xd04", "cortex-a35")
        .Case("0xd03", "cortex-a53")
        .Case("0xd05", "cortex-a55")
        .Case("0xd46", "cortex-a510")
        .Case("0xd80", "cortex-a520")
        .Case("0xd88", "cortex-a520ae")
        .Case("0xd07", "cortex-a57")
        .Case("0xd06", "cortex-a65")
        .Case("0xd43", "cortex-a65ae")
        .Case("0xd08", "cortex-a72")
        .Case("0xd09", "cortex-a73")
        .Case("0xd0a", "cortex-a75")
        .Case("0xd0b", "cortex-a76")
        .Case("0xd0e", "cortex-a76ae")
        .Case("0xd0d", "cortex-a77")
        .Case("0xd41", "cortex-a78")
        .Case("0xd42", "cortex-a78ae")
        .Case("0xd4b", "cortex-a78c")
        .Case("0xd47", "cortex-a710")
        .Case("0xd4d", "cortex-a715")
        .Case("0xd81", "cortex-a720")
        .Case("0xd89", "cortex-a720ae")
        .Case("0xd87", "cortex-a725")
        .Case("0xd44", "cortex-x1")
        .Case("0xd4c", "cortex-x1c")
        .Case("0xd48", "cortex-x2")
        .Case("0xd4e", "cortex-x3")
        .Case("0xd82", "cortex-x4")
        .Case("0xd85", "cortex-x925")
        .Case("0xd4a", "neoverse-e1")
        .Case("0xd0c", "neoverse-n1")
        .Case("0xd49", "neoverse-n2")
        .Case("0xd8e", "neoverse-n3")
        .Case("0xd40", "neoverse-v1")
        .Case("0xd4f", "neoverse-v2")
        .Case("0xd84", "neoverse-v3")
        .Case("0xd83", "neoverse-v3ae")
        .Default("generic");
  }

  if (Implementer == "0x42" || Implementer == "0x43") { // Broadcom | Cavium.
    return StringSwitch<const char *>(Part)
      .Case("0x516", "thunderx2t99")
      .Case("0x0516", "thunderx2t99")
      .Case("0xaf", "thunderx2t99")
      .Case("0x0af", "thunderx2t99")
      .Case("0xa1", "thunderxt88")
      .Case("0x0a1", "thunderxt88")
      .Default("generic");
  }

  if (Implementer == "0x46") { // Fujitsu Ltd.
    return StringSwitch<const char *>(Part)
      .Case("0x001", "a64fx")
      .Default("generic");
  }

  if (Implementer == "0x4e") { // NVIDIA Corporation
    return StringSwitch<const char *>(Part)
        .Case("0x004", "carmel")
        .Default("generic");
  }

  if (Implementer == "0x48") // HiSilicon Technologies, Inc.
    // The CPU part is a 3 digit hexadecimal number with a 0x prefix. The
    // values correspond to the "Part number" in the CP15/c0 register. The
    // contents are specified in the various processor manuals.
    return StringSwitch<const char *>(Part)
      .Case("0xd01", "tsv110")
      .Default("generic");

  if (Implementer == "0x51") // Qualcomm Technologies, Inc.
    // The CPU part is a 3 digit hexadecimal number with a 0x prefix. The
    // values correspond to the "Part number" in the CP15/c0 register. The
    // contents are specified in the various processor manuals.
    return StringSwitch<const char *>(Part)
        .Case("0x06f", "krait") // APQ8064
        .Case("0x201", "kryo")
        .Case("0x205", "kryo")
        .Case("0x211", "kryo")
        .Case("0x800", "cortex-a73") // Kryo 2xx Gold
        .Case("0x801", "cortex-a73") // Kryo 2xx Silver
        .Case("0x802", "cortex-a75") // Kryo 3xx Gold
        .Case("0x803", "cortex-a75") // Kryo 3xx Silver
        .Case("0x804", "cortex-a76") // Kryo 4xx Gold
        .Case("0x805", "cortex-a76") // Kryo 4xx/5xx Silver
        .Case("0xc00", "falkor")
        .Case("0xc01", "saphira")
        .Case("0x001", "oryon-1")
        .Default("generic");
  if (Implementer == "0x53") { // Samsung Electronics Co., Ltd.
    // The Exynos chips have a convoluted ID scheme that doesn't seem to follow
    // any predictive pattern across variants and parts.
    unsigned Variant = 0, Part = 0;

    // Look for the CPU variant line, whose value is a 1 digit hexadecimal
    // number, corresponding to the Variant bits in the CP15/C0 register.
    for (auto I : Lines)
      if (I.consume_front("CPU variant"))
        I.ltrim("\t :").getAsInteger(0, Variant);

    // Look for the CPU part line, whose value is a 3 digit hexadecimal
    // number, corresponding to the PartNum bits in the CP15/C0 register.
    for (auto I : Lines)
      if (I.consume_front("CPU part"))
        I.ltrim("\t :").getAsInteger(0, Part);

    unsigned Exynos = (Variant << 12) | Part;
    switch (Exynos) {
    default:
      // Default by falling through to Exynos M3.
      [[fallthrough]];
    case 0x1002:
      return "exynos-m3";
    case 0x1003:
      return "exynos-m4";
    }
  }

  if (Implementer == "0x6d") { // Microsoft Corporation.
    // The Microsoft Azure Cobalt 100 CPU is handled as a Neoverse N2.
    return StringSwitch<const char *>(Part)
        .Case("0xd49", "neoverse-n2")
        .Default("generic");
  }

  if (Implementer == "0xc0") { // Ampere Computing
    return StringSwitch<const char *>(Part)
        .Case("0xac3", "ampere1")
        .Case("0xac4", "ampere1a")
        .Case("0xac5", "ampere1b")
        .Default("generic");
  }

  return "generic";
}

namespace {
StringRef getCPUNameFromS390Model(unsigned int Id, bool HaveVectorSupport) {
  switch (Id) {
    case 2064:  // z900 not supported by LLVM
    case 2066:
    case 2084:  // z990 not supported by LLVM
    case 2086:
    case 2094:  // z9-109 not supported by LLVM
    case 2096:
      return "generic";
    case 2097:
    case 2098:
      return "z10";
    case 2817:
    case 2818:
      return "z196";
    case 2827:
    case 2828:
      return "zEC12";
    case 2964:
    case 2965:
      return HaveVectorSupport? "z13" : "zEC12";
    case 3906:
    case 3907:
      return HaveVectorSupport? "z14" : "zEC12";
    case 8561:
    case 8562:
      return HaveVectorSupport? "z15" : "zEC12";
    case 3931:
    case 3932:
    default:
      return HaveVectorSupport? "z16" : "zEC12";
  }
}
} // end anonymous namespace

StringRef sys::detail::getHostCPUNameForS390x(StringRef ProcCpuinfoContent) {
  // STIDP is a privileged operation, so use /proc/cpuinfo instead.

  // The "processor 0:" line comes after a fair amount of other information,
  // including a cache breakdown, but this should be plenty.
  SmallVector<StringRef, 32> Lines;
  ProcCpuinfoContent.split(Lines, "\n");

  // Look for the CPU features.
  SmallVector<StringRef, 32> CPUFeatures;
  for (unsigned I = 0, E = Lines.size(); I != E; ++I)
    if (Lines[I].starts_with("features")) {
      size_t Pos = Lines[I].find(':');
      if (Pos != StringRef::npos) {
        Lines[I].drop_front(Pos + 1).split(CPUFeatures, ' ');
        break;
      }
    }

  // We need to check for the presence of vector support independently of
  // the machine type, since we may only use the vector register set when
  // supported by the kernel (and hypervisor).
  bool HaveVectorSupport = false;
  for (unsigned I = 0, E = CPUFeatures.size(); I != E; ++I) {
    if (CPUFeatures[I] == "vx")
      HaveVectorSupport = true;
  }

  // Now check the processor machine type.
  for (unsigned I = 0, E = Lines.size(); I != E; ++I) {
    if (Lines[I].starts_with("processor ")) {
      size_t Pos = Lines[I].find("machine = ");
      if (Pos != StringRef::npos) {
        Pos += sizeof("machine = ") - 1;
        unsigned int Id;
        if (!Lines[I].drop_front(Pos).getAsInteger(10, Id))
          return getCPUNameFromS390Model(Id, HaveVectorSupport);
      }
      break;
    }
  }

  return "generic";
}

StringRef sys::detail::getHostCPUNameForRISCV(StringRef ProcCpuinfoContent) {
  // There are 24 lines in /proc/cpuinfo
  SmallVector<StringRef> Lines;
  ProcCpuinfoContent.split(Lines, "\n");

  // Look for uarch line to determine cpu name
  StringRef UArch;
  for (unsigned I = 0, E = Lines.size(); I != E; ++I) {
    if (Lines[I].starts_with("uarch")) {
      UArch = Lines[I].substr(5).ltrim("\t :");
      break;
    }
  }

  return StringSwitch<const char *>(UArch)
      .Case("sifive,u74-mc", "sifive-u74")
      .Case("sifive,bullet0", "sifive-u74")
      .Default("");
}

StringRef sys::detail::getHostCPUNameForBPF() {
#if !defined(__linux__) || !defined(__x86_64__)
  return "generic";
#else
  uint8_t v3_insns[40] __attribute__ ((aligned (8))) =
      /* BPF_MOV64_IMM(BPF_REG_0, 0) */
    { 0xb7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_2, 1) */
      0xb7, 0x2, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_JMP32_REG(BPF_JLT, BPF_REG_0, BPF_REG_2, 1) */
      0xae, 0x20, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_0, 1) */
      0xb7, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_EXIT_INSN() */
      0x95, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

  uint8_t v2_insns[40] __attribute__ ((aligned (8))) =
      /* BPF_MOV64_IMM(BPF_REG_0, 0) */
    { 0xb7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_2, 1) */
      0xb7, 0x2, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_JMP_REG(BPF_JLT, BPF_REG_0, BPF_REG_2, 1) */
      0xad, 0x20, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0,
      /* BPF_MOV64_IMM(BPF_REG_0, 1) */
      0xb7, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
      /* BPF_EXIT_INSN() */
      0x95, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

  struct bpf_prog_load_attr {
    uint32_t prog_type;
    uint32_t insn_cnt;
    uint64_t insns;
    uint64_t license;
    uint32_t log_level;
    uint32_t log_size;
    uint64_t log_buf;
    uint32_t kern_version;
    uint32_t prog_flags;
  } attr = {};
  attr.prog_type = 1; /* BPF_PROG_TYPE_SOCKET_FILTER */
  attr.insn_cnt = 5;
  attr.insns = (uint64_t)v3_insns;
  attr.license = (uint64_t)"DUMMY";

  int fd = syscall(321 /* __NR_bpf */, 5 /* BPF_PROG_LOAD */, &attr,
                   sizeof(attr));
  if (fd >= 0) {
    close(fd);
    return "v3";
  }

  /* Clear the whole attr in case its content changed by syscall. */
  memset(&attr, 0, sizeof(attr));
  attr.prog_type = 1; /* BPF_PROG_TYPE_SOCKET_FILTER */
  attr.insn_cnt = 5;
  attr.insns = (uint64_t)v2_insns;
  attr.license = (uint64_t)"DUMMY";
  fd = syscall(321 /* __NR_bpf */, 5 /* BPF_PROG_LOAD */, &attr, sizeof(attr));
  if (fd >= 0) {
    close(fd);
    return "v2";
  }
  return "v1";
#endif
}

#if defined(__i386__) || defined(_M_IX86) || \
    defined(__x86_64__) || defined(_M_X64)

// The check below for i386 was copied from clang's cpuid.h (__get_cpuid_max).
// Check motivated by bug reports for OpenSSL crashing on CPUs without CPUID
// support. Consequently, for i386, the presence of CPUID is checked first
// via the corresponding eflags bit.
// Removal of cpuid.h header motivated by PR30384
// Header cpuid.h and method __get_cpuid_max are not used in llvm, clang, openmp
// or test-suite, but are used in external projects e.g. libstdcxx
static bool isCpuIdSupported() {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__i386__)
  int __cpuid_supported;
  __asm__("  pushfl\n"
          "  popl   %%eax\n"
          "  movl   %%eax,%%ecx\n"
          "  xorl   $0x00200000,%%eax\n"
          "  pushl  %%eax\n"
          "  popfl\n"
          "  pushfl\n"
          "  popl   %%eax\n"
          "  movl   $0,%0\n"
          "  cmpl   %%eax,%%ecx\n"
          "  je     1f\n"
          "  movl   $1,%0\n"
          "1:"
          : "=r"(__cpuid_supported)
          :
          : "eax", "ecx");
  if (!__cpuid_supported)
    return false;
#endif
  return true;
#endif
  return true;
}

/// getX86CpuIDAndInfo - Execute the specified cpuid and return the 4 values in
/// the specified arguments.  If we can't run cpuid on the host, return true.
static bool getX86CpuIDAndInfo(unsigned value, unsigned *rEAX, unsigned *rEBX,
                               unsigned *rECX, unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
  // gcc doesn't know cpuid would clobber ebx/rbx. Preserve it manually.
  // FIXME: should we save this for Clang?
  __asm__("movq\t%%rbx, %%rsi\n\t"
          "cpuid\n\t"
          "xchgq\t%%rbx, %%rsi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value));
  return false;
#elif defined(__i386__)
  __asm__("movl\t%%ebx, %%esi\n\t"
          "cpuid\n\t"
          "xchgl\t%%ebx, %%esi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value));
  return false;
#else
  return true;
#endif
#elif defined(_MSC_VER)
  // The MSVC intrinsic is portable across x86 and x64.
  int registers[4];
  __cpuid(registers, value);
  *rEAX = registers[0];
  *rEBX = registers[1];
  *rECX = registers[2];
  *rEDX = registers[3];
  return false;
#else
  return true;
#endif
}

namespace llvm {
namespace sys {
namespace detail {
namespace x86 {

VendorSignatures getVendorSignature(unsigned *MaxLeaf) {
  unsigned EAX = 0, EBX = 0, ECX = 0, EDX = 0;
  if (MaxLeaf == nullptr)
    MaxLeaf = &EAX;
  else
    *MaxLeaf = 0;

  if (!isCpuIdSupported())
    return VendorSignatures::UNKNOWN;

  if (getX86CpuIDAndInfo(0, MaxLeaf, &EBX, &ECX, &EDX) || *MaxLeaf < 1)
    return VendorSignatures::UNKNOWN;

  // "Genu ineI ntel"
  if (EBX == 0x756e6547 && EDX == 0x49656e69 && ECX == 0x6c65746e)
    return VendorSignatures::GENUINE_INTEL;

  // "Auth enti cAMD"
  if (EBX == 0x68747541 && EDX == 0x69746e65 && ECX == 0x444d4163)
    return VendorSignatures::AUTHENTIC_AMD;

  return VendorSignatures::UNKNOWN;
}

} // namespace x86
} // namespace detail
} // namespace sys
} // namespace llvm

using namespace llvm::sys::detail::x86;

/// getX86CpuIDAndInfoEx - Execute the specified cpuid with subleaf and return
/// the 4 values in the specified arguments.  If we can't run cpuid on the host,
/// return true.
static bool getX86CpuIDAndInfoEx(unsigned value, unsigned subleaf,
                                 unsigned *rEAX, unsigned *rEBX, unsigned *rECX,
                                 unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
  // gcc doesn't know cpuid would clobber ebx/rbx. Preserve it manually.
  // FIXME: should we save this for Clang?
  __asm__("movq\t%%rbx, %%rsi\n\t"
          "cpuid\n\t"
          "xchgq\t%%rbx, %%rsi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value), "c"(subleaf));
  return false;
#elif defined(__i386__)
  __asm__("movl\t%%ebx, %%esi\n\t"
          "cpuid\n\t"
          "xchgl\t%%ebx, %%esi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value), "c"(subleaf));
  return false;
#else
  return true;
#endif
#elif defined(_MSC_VER)
  int registers[4];
  __cpuidex(registers, value, subleaf);
  *rEAX = registers[0];
  *rEBX = registers[1];
  *rECX = registers[2];
  *rEDX = registers[3];
  return false;
#else
  return true;
#endif
}

// Read control register 0 (XCR0). Used to detect features such as AVX.
static bool getX86XCR0(unsigned *rEAX, unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
  // Check xgetbv; this uses a .byte sequence instead of the instruction
  // directly because older assemblers do not include support for xgetbv and
  // there is no easy way to conditionally compile based on the assembler used.
  __asm__(".byte 0x0f, 0x01, 0xd0" : "=a"(*rEAX), "=d"(*rEDX) : "c"(0));
  return false;
#elif defined(_MSC_FULL_VER) && defined(_XCR_XFEATURE_ENABLED_MASK)
  unsigned long long Result = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
  *rEAX = Result;
  *rEDX = Result >> 32;
  return false;
#else
  return true;
#endif
}

static void detectX86FamilyModel(unsigned EAX, unsigned *Family,
                                 unsigned *Model) {
  *Family = (EAX >> 8) & 0xf; // Bits 8 - 11
  *Model = (EAX >> 4) & 0xf;  // Bits 4 - 7
  if (*Family == 6 || *Family == 0xf) {
    if (*Family == 0xf)
      // Examine extended family ID if family ID is F.
      *Family += (EAX >> 20) & 0xff; // Bits 20 - 27
    // Examine extended model ID if family ID is 6 or F.
    *Model += ((EAX >> 16) & 0xf) << 4; // Bits 16 - 19
  }
}

#define testFeature(F) (Features[F / 32] & (1 << (F % 32))) != 0

static StringRef getIntelProcessorTypeAndSubtype(unsigned Family,
                                                 unsigned Model,
                                                 const unsigned *Features,
                                                 unsigned *Type,
                                                 unsigned *Subtype) {
  StringRef CPU;

  switch (Family) {
  case 3:
    CPU = "i386";
    break;
  case 4:
    CPU = "i486";
    break;
  case 5:
    if (testFeature(X86::FEATURE_MMX)) {
      CPU = "pentium-mmx";
      break;
    }
    CPU = "pentium";
    break;
  case 6:
    switch (Model) {
    case 0x0f: // Intel Core 2 Duo processor, Intel Core 2 Duo mobile
               // processor, Intel Core 2 Quad processor, Intel Core 2 Quad
               // mobile processor, Intel Core 2 Extreme processor, Intel
               // Pentium Dual-Core processor, Intel Xeon processor, model
               // 0Fh. All processors are manufactured using the 65 nm process.
    case 0x16: // Intel Celeron processor model 16h. All processors are
               // manufactured using the 65 nm process
      CPU = "core2";
      *Type = X86::INTEL_CORE2;
      break;
    case 0x17: // Intel Core 2 Extreme processor, Intel Xeon processor, model
               // 17h. All processors are manufactured using the 45 nm process.
               //
               // 45nm: Penryn , Wolfdale, Yorkfield (XE)
    case 0x1d: // Intel Xeon processor MP. All processors are manufactured using
               // the 45 nm process.
      CPU = "penryn";
      *Type = X86::INTEL_CORE2;
      break;
    case 0x1a: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 45 nm process.
    case 0x1e: // Intel(R) Core(TM) i7 CPU         870  @ 2.93GHz.
               // As found in a Summer 2010 model iMac.
    case 0x1f:
    case 0x2e:              // Nehalem EX
      CPU = "nehalem";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_NEHALEM;
      break;
    case 0x25: // Intel Core i7, laptop version.
    case 0x2c: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 32 nm process.
    case 0x2f: // Westmere EX
      CPU = "westmere";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_WESTMERE;
      break;
    case 0x2a: // Intel Core i7 processor. All processors are manufactured
               // using the 32 nm process.
    case 0x2d:
      CPU = "sandybridge";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_SANDYBRIDGE;
      break;
    case 0x3a:
    case 0x3e:              // Ivy Bridge EP
      CPU = "ivybridge";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_IVYBRIDGE;
      break;

    // Haswell:
    case 0x3c:
    case 0x3f:
    case 0x45:
    case 0x46:
      CPU = "haswell";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_HASWELL;
      break;

    // Broadwell:
    case 0x3d:
    case 0x47:
    case 0x4f:
    case 0x56:
      CPU = "broadwell";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_BROADWELL;
      break;

    // Skylake:
    case 0x4e:              // Skylake mobile
    case 0x5e:              // Skylake desktop
    case 0x8e:              // Kaby Lake mobile
    case 0x9e:              // Kaby Lake desktop
    case 0xa5:              // Comet Lake-H/S
    case 0xa6:              // Comet Lake-U
      CPU = "skylake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_SKYLAKE;
      break;

    // Rocketlake:
    case 0xa7:
      CPU = "rocketlake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ROCKETLAKE;
      break;

    // Skylake Xeon:
    case 0x55:
      *Type = X86::INTEL_COREI7;
      if (testFeature(X86::FEATURE_AVX512BF16)) {
        CPU = "cooperlake";
        *Subtype = X86::INTEL_COREI7_COOPERLAKE;
      } else if (testFeature(X86::FEATURE_AVX512VNNI)) {
        CPU = "cascadelake";
        *Subtype = X86::INTEL_COREI7_CASCADELAKE;
      } else {
        CPU = "skylake-avx512";
        *Subtype = X86::INTEL_COREI7_SKYLAKE_AVX512;
      }
      break;

    // Cannonlake:
    case 0x66:
      CPU = "cannonlake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_CANNONLAKE;
      break;

    // Icelake:
    case 0x7d:
    case 0x7e:
      CPU = "icelake-client";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ICELAKE_CLIENT;
      break;

    // Tigerlake:
    case 0x8c:
    case 0x8d:
      CPU = "tigerlake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_TIGERLAKE;
      break;

    // Alderlake:
    case 0x97:
    case 0x9a:
    // Gracemont
    case 0xbe:
    // Raptorlake:
    case 0xb7:
    case 0xba:
    case 0xbf:
    // Meteorlake:
    case 0xaa:
    case 0xac:
      CPU = "alderlake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ALDERLAKE;
      break;

    // Arrowlake:
    case 0xc5:
      CPU = "arrowlake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ARROWLAKE;
      break;

    // Arrowlake S:
    case 0xc6:
    // Lunarlake:
    case 0xbd:
      CPU = "arrowlake-s";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ARROWLAKE_S;
      break;

    // Pantherlake:
    case 0xcc:
      CPU = "pantherlake";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_PANTHERLAKE;
      break;

    // Graniterapids:
    case 0xad:
      CPU = "graniterapids";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_GRANITERAPIDS;
      break;

    // Granite Rapids D:
    case 0xae:
      CPU = "graniterapids-d";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_GRANITERAPIDS_D;
      break;

    // Icelake Xeon:
    case 0x6a:
    case 0x6c:
      CPU = "icelake-server";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_ICELAKE_SERVER;
      break;

    // Emerald Rapids:
    case 0xcf:
    // Sapphire Rapids:
    case 0x8f:
      CPU = "sapphirerapids";
      *Type = X86::INTEL_COREI7;
      *Subtype = X86::INTEL_COREI7_SAPPHIRERAPIDS;
      break;

    case 0x1c: // Most 45 nm Intel Atom processors
    case 0x26: // 45 nm Atom Lincroft
    case 0x27: // 32 nm Atom Medfield
    case 0x35: // 32 nm Atom Midview
    case 0x36: // 32 nm Atom Midview
      CPU = "bonnell";
      *Type = X86::INTEL_BONNELL;
      break;

    // Atom Silvermont codes from the Intel software optimization guide.
    case 0x37:
    case 0x4a:
    case 0x4d:
    case 0x5a:
    case 0x5d:
    case 0x4c: // really airmont
      CPU = "silvermont";
      *Type = X86::INTEL_SILVERMONT;
      break;
    // Goldmont:
    case 0x5c: // Apollo Lake
    case 0x5f: // Denverton
      CPU = "goldmont";
      *Type = X86::INTEL_GOLDMONT;
      break;
    case 0x7a:
      CPU = "goldmont-plus";
      *Type = X86::INTEL_GOLDMONT_PLUS;
      break;
    case 0x86:
    case 0x8a: // Lakefield
    case 0x96: // Elkhart Lake
    case 0x9c: // Jasper Lake
      CPU = "tremont";
      *Type = X86::INTEL_TREMONT;
      break;

    // Sierraforest:
    case 0xaf:
      CPU = "sierraforest";
      *Type = X86::INTEL_SIERRAFOREST;
      break;

    // Grandridge:
    case 0xb6:
      CPU = "grandridge";
      *Type = X86::INTEL_GRANDRIDGE;
      break;

    // Clearwaterforest:
    case 0xdd:
      CPU = "clearwaterforest";
      *Type = X86::INTEL_CLEARWATERFOREST;
      break;

    // Xeon Phi (Knights Landing + Knights Mill):
    case 0x57:
      CPU = "knl";
      *Type = X86::INTEL_KNL;
      break;
    case 0x85:
      CPU = "knm";
      *Type = X86::INTEL_KNM;
      break;

    default: // Unknown family 6 CPU, try to guess.
      // Don't both with Type/Subtype here, they aren't used by the caller.
      // They're used above to keep the code in sync with compiler-rt.
      // TODO detect tigerlake host from model
      if (testFeature(X86::FEATURE_AVX512VP2INTERSECT)) {
        CPU = "tigerlake";
      } else if (testFeature(X86::FEATURE_AVX512VBMI2)) {
        CPU = "icelake-client";
      } else if (testFeature(X86::FEATURE_AVX512VBMI)) {
        CPU = "cannonlake";
      } else if (testFeature(X86::FEATURE_AVX512BF16)) {
        CPU = "cooperlake";
      } else if (testFeature(X86::FEATURE_AVX512VNNI)) {
        CPU = "cascadelake";
      } else if (testFeature(X86::FEATURE_AVX512VL)) {
        CPU = "skylake-avx512";
      } else if (testFeature(X86::FEATURE_CLFLUSHOPT)) {
        if (testFeature(X86::FEATURE_SHA))
          CPU = "goldmont";
        else
          CPU = "skylake";
      } else if (testFeature(X86::FEATURE_ADX)) {
        CPU = "broadwell";
      } else if (testFeature(X86::FEATURE_AVX2)) {
        CPU = "haswell";
      } else if (testFeature(X86::FEATURE_AVX)) {
        CPU = "sandybridge";
      } else if (testFeature(X86::FEATURE_SSE4_2)) {
        if (testFeature(X86::FEATURE_MOVBE))
          CPU = "silvermont";
        else
          CPU = "nehalem";
      } else if (testFeature(X86::FEATURE_SSE4_1)) {
        CPU = "penryn";
      } else if (testFeature(X86::FEATURE_SSSE3)) {
        if (testFeature(X86::FEATURE_MOVBE))
          CPU = "bonnell";
        else
          CPU = "core2";
      } else if (testFeature(X86::FEATURE_64BIT)) {
        CPU = "core2";
      } else if (testFeature(X86::FEATURE_SSE3)) {
        CPU = "yonah";
      } else if (testFeature(X86::FEATURE_SSE2)) {
        CPU = "pentium-m";
      } else if (testFeature(X86::FEATURE_SSE)) {
        CPU = "pentium3";
      } else if (testFeature(X86::FEATURE_MMX)) {
        CPU = "pentium2";
      } else {
        CPU = "pentiumpro";
      }
      break;
    }
    break;
  case 15: {
    if (testFeature(X86::FEATURE_64BIT)) {
      CPU = "nocona";
      break;
    }
    if (testFeature(X86::FEATURE_SSE3)) {
      CPU = "prescott";
      break;
    }
    CPU = "pentium4";
    break;
  }
  default:
    break; // Unknown.
  }

  return CPU;
}

static const char *getAMDProcessorTypeAndSubtype(unsigned Family,
                                                 unsigned Model,
                                                 const unsigned *Features,
                                                 unsigned *Type,
                                                 unsigned *Subtype) {
  const char *CPU = 0;

  switch (Family) {
  case 4:
    CPU = "i486";
    break;
  case 5:
    CPU = "pentium";
    switch (Model) {
    case 6:
    case 7:
      CPU = "k6";
      break;
    case 8:
      CPU = "k6-2";
      break;
    case 9:
    case 13:
      CPU = "k6-3";
      break;
    case 10:
      CPU = "geode";
      break;
    }
    break;
  case 6:
    if (testFeature(X86::FEATURE_SSE)) {
      CPU = "athlon-xp";
      break;
    }
    CPU = "athlon";
    break;
  case 15:
    if (testFeature(X86::FEATURE_SSE3)) {
      CPU = "k8-sse3";
      break;
    }
    CPU = "k8";
    break;
  case 16:
    CPU = "amdfam10";
    *Type = X86::AMDFAM10H; // "amdfam10"
    switch (Model) {
    case 2:
      *Subtype = X86::AMDFAM10H_BARCELONA;
      break;
    case 4:
      *Subtype = X86::AMDFAM10H_SHANGHAI;
      break;
    case 8:
      *Subtype = X86::AMDFAM10H_ISTANBUL;
      break;
    }
    break;
  case 20:
    CPU = "btver1";
    *Type = X86::AMD_BTVER1;
    break;
  case 21:
    CPU = "bdver1";
    *Type = X86::AMDFAM15H;
    if (Model >= 0x60 && Model <= 0x7f) {
      CPU = "bdver4";
      *Subtype = X86::AMDFAM15H_BDVER4;
      break; // 60h-7Fh: Excavator
    }
    if (Model >= 0x30 && Model <= 0x3f) {
      CPU = "bdver3";
      *Subtype = X86::AMDFAM15H_BDVER3;
      break; // 30h-3Fh: Steamroller
    }
    if ((Model >= 0x10 && Model <= 0x1f) || Model == 0x02) {
      CPU = "bdver2";
      *Subtype = X86::AMDFAM15H_BDVER2;
      break; // 02h, 10h-1Fh: Piledriver
    }
    if (Model <= 0x0f) {
      *Subtype = X86::AMDFAM15H_BDVER1;
      break; // 00h-0Fh: Bulldozer
    }
    break;
  case 22:
    CPU = "btver2";
    *Type = X86::AMD_BTVER2;
    break;
  case 23:
    CPU = "znver1";
    *Type = X86::AMDFAM17H;
    if ((Model >= 0x30 && Model <= 0x3f) || (Model == 0x47) ||
        (Model >= 0x60 && Model <= 0x67) || (Model >= 0x68 && Model <= 0x6f) ||
        (Model >= 0x70 && Model <= 0x7f) || (Model >= 0x84 && Model <= 0x87) ||
        (Model >= 0x90 && Model <= 0x97) || (Model >= 0x98 && Model <= 0x9f) ||
        (Model >= 0xa0 && Model <= 0xaf)) {
      // Family 17h Models 30h-3Fh (Starship) Zen 2
      // Family 17h Models 47h (Cardinal) Zen 2
      // Family 17h Models 60h-67h (Renoir) Zen 2
      // Family 17h Models 68h-6Fh (Lucienne) Zen 2
      // Family 17h Models 70h-7Fh (Matisse) Zen 2
      // Family 17h Models 84h-87h (ProjectX) Zen 2
      // Family 17h Models 90h-97h (VanGogh) Zen 2
      // Family 17h Models 98h-9Fh (Mero) Zen 2
      // Family 17h Models A0h-AFh (Mendocino) Zen 2
      CPU = "znver2";
      *Subtype = X86::AMDFAM17H_ZNVER2;
      break;
    }
    if ((Model >= 0x10 && Model <= 0x1f) || (Model >= 0x20 && Model <= 0x2f)) {
      // Family 17h Models 10h-1Fh (Raven1) Zen
      // Family 17h Models 10h-1Fh (Picasso) Zen+
      // Family 17h Models 20h-2Fh (Raven2 x86) Zen
      *Subtype = X86::AMDFAM17H_ZNVER1;
      break;
    }
    break;
  case 25:
    CPU = "znver3";
    *Type = X86::AMDFAM19H;
    if (Model <= 0x0f || (Model >= 0x20 && Model <= 0x2f) ||
        (Model >= 0x30 && Model <= 0x3f) || (Model >= 0x40 && Model <= 0x4f) ||
        (Model >= 0x50 && Model <= 0x5f)) {
      // Family 19h Models 00h-0Fh (Genesis, Chagall) Zen 3
      // Family 19h Models 20h-2Fh (Vermeer) Zen 3
      // Family 19h Models 30h-3Fh (Badami) Zen 3
      // Family 19h Models 40h-4Fh (Rembrandt) Zen 3+
      // Family 19h Models 50h-5Fh (Cezanne) Zen 3
      *Subtype = X86::AMDFAM19H_ZNVER3;
      break;
    }
    if ((Model >= 0x10 && Model <= 0x1f) || (Model >= 0x60 && Model <= 0x6f) ||
        (Model >= 0x70 && Model <= 0x77) || (Model >= 0x78 && Model <= 0x7f) ||
        (Model >= 0xa0 && Model <= 0xaf)) {
      // Family 19h Models 10h-1Fh (Stones; Storm Peak) Zen 4
      // Family 19h Models 60h-6Fh (Raphael) Zen 4
      // Family 19h Models 70h-77h (Phoenix, Hawkpoint1) Zen 4
      // Family 19h Models 78h-7Fh (Phoenix 2, Hawkpoint2) Zen 4
      // Family 19h Models A0h-AFh (Stones-Dense) Zen 4
      CPU = "znver4";
      *Subtype = X86::AMDFAM19H_ZNVER4;
      break; //  "znver4"
    }
    break; // family 19h
  case 26:
    CPU = "znver5";
    *Type = X86::AMDFAM1AH;
    if (Model <= 0x77) {
      // Models 00h-0Fh (Breithorn).
      // Models 10h-1Fh (Breithorn-Dense).
      // Models 20h-2Fh (Strix 1).
      // Models 30h-37h (Strix 2).
      // Models 38h-3Fh (Strix 3).
      // Models 40h-4Fh (Granite Ridge).
      // Models 50h-5Fh (Weisshorn).
      // Models 60h-6Fh (Krackan1).
      // Models 70h-77h (Sarlak).
      CPU = "znver5";
      *Subtype = X86::AMDFAM1AH_ZNVER5;
      break; //  "znver5"
    }
    break;

  default:
    break; // Unknown AMD CPU.
  }

  return CPU;
}

#undef testFeature

static void getAvailableFeatures(unsigned ECX, unsigned EDX, unsigned MaxLeaf,
                                 unsigned *Features) {
  unsigned EAX, EBX;

  auto setFeature = [&](unsigned F) {
    Features[F / 32] |= 1U << (F % 32);
  };

  if ((EDX >> 15) & 1)
    setFeature(X86::FEATURE_CMOV);
  if ((EDX >> 23) & 1)
    setFeature(X86::FEATURE_MMX);
  if ((EDX >> 25) & 1)
    setFeature(X86::FEATURE_SSE);
  if ((EDX >> 26) & 1)
    setFeature(X86::FEATURE_SSE2);

  if ((ECX >> 0) & 1)
    setFeature(X86::FEATURE_SSE3);
  if ((ECX >> 1) & 1)
    setFeature(X86::FEATURE_PCLMUL);
  if ((ECX >> 9) & 1)
    setFeature(X86::FEATURE_SSSE3);
  if ((ECX >> 12) & 1)
    setFeature(X86::FEATURE_FMA);
  if ((ECX >> 19) & 1)
    setFeature(X86::FEATURE_SSE4_1);
  if ((ECX >> 20) & 1) {
    setFeature(X86::FEATURE_SSE4_2);
    setFeature(X86::FEATURE_CRC32);
  }
  if ((ECX >> 23) & 1)
    setFeature(X86::FEATURE_POPCNT);
  if ((ECX >> 25) & 1)
    setFeature(X86::FEATURE_AES);

  if ((ECX >> 22) & 1)
    setFeature(X86::FEATURE_MOVBE);

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  const unsigned AVXBits = (1 << 27) | (1 << 28);
  bool HasAVX = ((ECX & AVXBits) == AVXBits) && !getX86XCR0(&EAX, &EDX) &&
                ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVX && ((EAX & 0xe0) == 0xe0);
#endif

  if (HasAVX)
    setFeature(X86::FEATURE_AVX);

  bool HasLeaf7 =
      MaxLeaf >= 0x7 && !getX86CpuIDAndInfoEx(0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  if (HasLeaf7 && ((EBX >> 3) & 1))
    setFeature(X86::FEATURE_BMI);
  if (HasLeaf7 && ((EBX >> 5) & 1) && HasAVX)
    setFeature(X86::FEATURE_AVX2);
  if (HasLeaf7 && ((EBX >> 8) & 1))
    setFeature(X86::FEATURE_BMI2);
  if (HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save) {
    setFeature(X86::FEATURE_AVX512F);
    setFeature(X86::FEATURE_EVEX512);
  }
  if (HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512DQ);
  if (HasLeaf7 && ((EBX >> 19) & 1))
    setFeature(X86::FEATURE_ADX);
  if (HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512IFMA);
  if (HasLeaf7 && ((EBX >> 23) & 1))
    setFeature(X86::FEATURE_CLFLUSHOPT);
  if (HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512CD);
  if (HasLeaf7 && ((EBX >> 29) & 1))
    setFeature(X86::FEATURE_SHA);
  if (HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512BW);
  if (HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VL);

  if (HasLeaf7 && ((ECX >> 1) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VBMI);
  if (HasLeaf7 && ((ECX >> 6) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VBMI2);
  if (HasLeaf7 && ((ECX >> 8) & 1))
    setFeature(X86::FEATURE_GFNI);
  if (HasLeaf7 && ((ECX >> 10) & 1) && HasAVX)
    setFeature(X86::FEATURE_VPCLMULQDQ);
  if (HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VNNI);
  if (HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512BITALG);
  if (HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VPOPCNTDQ);

  if (HasLeaf7 && ((EDX >> 2) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX5124VNNIW);
  if (HasLeaf7 && ((EDX >> 3) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX5124FMAPS);
  if (HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512VP2INTERSECT);

  // EAX from subleaf 0 is the maximum subleaf supported. Some CPUs don't
  // return all 0s for invalid subleaves so check the limit.
  bool HasLeaf7Subleaf1 =
      HasLeaf7 && EAX >= 1 &&
      !getX86CpuIDAndInfoEx(0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save)
    setFeature(X86::FEATURE_AVX512BF16);

  unsigned MaxExtLevel;
  getX86CpuIDAndInfo(0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !getX86CpuIDAndInfo(0x80000001, &EAX, &EBX, &ECX, &EDX);
  if (HasExtLeaf1 && ((ECX >> 6) & 1))
    setFeature(X86::FEATURE_SSE4_A);
  if (HasExtLeaf1 && ((ECX >> 11) & 1))
    setFeature(X86::FEATURE_XOP);
  if (HasExtLeaf1 && ((ECX >> 16) & 1))
    setFeature(X86::FEATURE_FMA4);

  if (HasExtLeaf1 && ((EDX >> 29) & 1))
    setFeature(X86::FEATURE_64BIT);
}

StringRef sys::getHostCPUName() {
  unsigned MaxLeaf = 0;
  const VendorSignatures Vendor = getVendorSignature(&MaxLeaf);
  if (Vendor == VendorSignatures::UNKNOWN)
    return "generic";

  unsigned EAX = 0, EBX = 0, ECX = 0, EDX = 0;
  getX86CpuIDAndInfo(0x1, &EAX, &EBX, &ECX, &EDX);

  unsigned Family = 0, Model = 0;
  unsigned Features[(X86::CPU_FEATURE_MAX + 31) / 32] = {0};
  detectX86FamilyModel(EAX, &Family, &Model);
  getAvailableFeatures(ECX, EDX, MaxLeaf, Features);

  // These aren't consumed in this file, but we try to keep some source code the
  // same or similar to compiler-rt.
  unsigned Type = 0;
  unsigned Subtype = 0;

  StringRef CPU;

  if (Vendor == VendorSignatures::GENUINE_INTEL) {
    CPU = getIntelProcessorTypeAndSubtype(Family, Model, Features, &Type,
                                          &Subtype);
  } else if (Vendor == VendorSignatures::AUTHENTIC_AMD) {
    CPU = getAMDProcessorTypeAndSubtype(Family, Model, Features, &Type,
                                        &Subtype);
  }

  if (!CPU.empty())
    return CPU;

  return "generic";
}

#elif defined(__APPLE__) && defined(__powerpc__)
StringRef sys::getHostCPUName() {
  host_basic_info_data_t hostInfo;
  mach_msg_type_number_t infoCount;

  infoCount = HOST_BASIC_INFO_COUNT;
  mach_port_t hostPort = mach_host_self();
  host_info(hostPort, HOST_BASIC_INFO, (host_info_t)&hostInfo,
            &infoCount);
  mach_port_deallocate(mach_task_self(), hostPort);

  if (hostInfo.cpu_type != CPU_TYPE_POWERPC)
    return "generic";

  switch (hostInfo.cpu_subtype) {
  case CPU_SUBTYPE_POWERPC_601:
    return "601";
  case CPU_SUBTYPE_POWERPC_602:
    return "602";
  case CPU_SUBTYPE_POWERPC_603:
    return "603";
  case CPU_SUBTYPE_POWERPC_603e:
    return "603e";
  case CPU_SUBTYPE_POWERPC_603ev:
    return "603ev";
  case CPU_SUBTYPE_POWERPC_604:
    return "604";
  case CPU_SUBTYPE_POWERPC_604e:
    return "604e";
  case CPU_SUBTYPE_POWERPC_620:
    return "620";
  case CPU_SUBTYPE_POWERPC_750:
    return "750";
  case CPU_SUBTYPE_POWERPC_7400:
    return "7400";
  case CPU_SUBTYPE_POWERPC_7450:
    return "7450";
  case CPU_SUBTYPE_POWERPC_970:
    return "970";
  default:;
  }

  return "generic";
}
#elif defined(__linux__) && defined(__powerpc__)
StringRef sys::getHostCPUName() {
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  return detail::getHostCPUNameForPowerPC(Content);
}
#elif defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
StringRef sys::getHostCPUName() {
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  return detail::getHostCPUNameForARM(Content);
}
#elif defined(__linux__) && defined(__s390x__)
StringRef sys::getHostCPUName() {
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  return detail::getHostCPUNameForS390x(Content);
}
#elif defined(__MVS__)
StringRef sys::getHostCPUName() {
  // Get pointer to Communications Vector Table (CVT).
  // The pointer is located at offset 16 of the Prefixed Save Area (PSA).
  // It is stored as 31 bit pointer and will be zero-extended to 64 bit.
  int *StartToCVTOffset = reinterpret_cast<int *>(0x10);
  // Since its stored as a 31-bit pointer, get the 4 bytes from the start
  // of address.
  int ReadValue = *StartToCVTOffset;
  // Explicitly clear the high order bit.
  ReadValue = (ReadValue & 0x7FFFFFFF);
  char *CVT = reinterpret_cast<char *>(ReadValue);
  // The model number is located in the CVT prefix at offset -6 and stored as
  // signless packed decimal.
  uint16_t Id = *(uint16_t *)&CVT[-6];
  // Convert number to integer.
  Id = decodePackedBCD<uint16_t>(Id, false);
  // Check for vector support. It's stored in field CVTFLAG5 (offset 244),
  // bit CVTVEF (X'80'). The facilities list is part of the PSA but the vector
  // extension can only be used if bit CVTVEF is on.
  bool HaveVectorSupport = CVT[244] & 0x80;
  return getCPUNameFromS390Model(Id, HaveVectorSupport);
}
#elif defined(__APPLE__) && (defined(__arm__) || defined(__aarch64__))
#define CPUFAMILY_ARM_SWIFT 0x1e2d6381
#define CPUFAMILY_ARM_CYCLONE 0x37a09642
#define CPUFAMILY_ARM_TYPHOON 0x2c91a47e
#define CPUFAMILY_ARM_TWISTER 0x92fb37c8
#define CPUFAMILY_ARM_HURRICANE 0x67ceee93
#define CPUFAMILY_ARM_MONSOON_MISTRAL 0xe81e7ef6
#define CPUFAMILY_ARM_VORTEX_TEMPEST 0x07d34b9f
#define CPUFAMILY_ARM_LIGHTNING_THUNDER 0x462504d2
#define CPUFAMILY_ARM_FIRESTORM_ICESTORM 0x1b588bb3
#define CPUFAMILY_ARM_BLIZZARD_AVALANCHE 0xda33d83d
#define CPUFAMILY_ARM_EVEREST_SAWTOOTH 0x8765edea

StringRef sys::getHostCPUName() {
  uint32_t Family;
  size_t Length = sizeof(Family);
  sysctlbyname("hw.cpufamily", &Family, &Length, NULL, 0);

  switch (Family) {
  case CPUFAMILY_ARM_SWIFT:
    return "swift";
  case CPUFAMILY_ARM_CYCLONE:
    return "apple-a7";
  case CPUFAMILY_ARM_TYPHOON:
    return "apple-a8";
  case CPUFAMILY_ARM_TWISTER:
    return "apple-a9";
  case CPUFAMILY_ARM_HURRICANE:
    return "apple-a10";
  case CPUFAMILY_ARM_MONSOON_MISTRAL:
    return "apple-a11";
  case CPUFAMILY_ARM_VORTEX_TEMPEST:
    return "apple-a12";
  case CPUFAMILY_ARM_LIGHTNING_THUNDER:
    return "apple-a13";
  case CPUFAMILY_ARM_FIRESTORM_ICESTORM:
    return "apple-m1";
  case CPUFAMILY_ARM_BLIZZARD_AVALANCHE:
    return "apple-m2";
  case CPUFAMILY_ARM_EVEREST_SAWTOOTH:
    return "apple-m3";
  default:
    // Default to the newest CPU we know about.
    return "apple-m3";
  }
}
#elif defined(_AIX)
StringRef sys::getHostCPUName() {
  switch (_system_configuration.implementation) {
  case POWER_4:
    if (_system_configuration.version == PV_4_3)
      return "970";
    return "pwr4";
  case POWER_5:
    if (_system_configuration.version == PV_5)
      return "pwr5";
    return "pwr5x";
  case POWER_6:
    if (_system_configuration.version == PV_6_Compat)
      return "pwr6";
    return "pwr6x";
  case POWER_7:
    return "pwr7";
  case POWER_8:
    return "pwr8";
  case POWER_9:
    return "pwr9";
// TODO: simplify this once the macro is available in all OS levels.
#ifdef POWER_10
  case POWER_10:
#else
  case 0x40000:
#endif
    return "pwr10";
#ifdef POWER_11
  case POWER_11:
#else
  case 0x80000:
#endif
    return "pwr11";
  default:
    return "generic";
  }
}
#elif defined(__loongarch__)
StringRef sys::getHostCPUName() {
  // Use processor id to detect cpu name.
  uint32_t processor_id;
  __asm__("cpucfg %[prid], $zero\n\t" : [prid] "=r"(processor_id));
  // Refer PRID_SERIES_MASK in linux kernel: arch/loongarch/include/asm/cpu.h.
  switch (processor_id & 0xf000) {
  case 0xc000: // Loongson 64bit, 4-issue
    return "la464";
  case 0xd000: // Loongson 64bit, 6-issue
    return "la664";
  // TODO: Others.
  default:
    break;
  }
  return "generic";
}
#elif defined(__riscv)
StringRef sys::getHostCPUName() {
#if defined(__linux__)
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  StringRef Name = detail::getHostCPUNameForRISCV(Content);
  if (!Name.empty())
    return Name;
#endif
#if __riscv_xlen == 64
  return "generic-rv64";
#elif __riscv_xlen == 32
  return "generic-rv32";
#else
#error "Unhandled value of __riscv_xlen"
#endif
}
#elif defined(__sparc__)
#if defined(__linux__)
StringRef sys::detail::getHostCPUNameForSPARC(StringRef ProcCpuinfoContent) {
  SmallVector<StringRef> Lines;
  ProcCpuinfoContent.split(Lines, "\n");

  // Look for cpu line to determine cpu name
  StringRef Cpu;
  for (unsigned I = 0, E = Lines.size(); I != E; ++I) {
    if (Lines[I].starts_with("cpu")) {
      Cpu = Lines[I].substr(5).ltrim("\t :");
      break;
    }
  }

  return StringSwitch<const char *>(Cpu)
      .StartsWith("SuperSparc", "supersparc")
      .StartsWith("HyperSparc", "hypersparc")
      .StartsWith("SpitFire", "ultrasparc")
      .StartsWith("BlackBird", "ultrasparc")
      .StartsWith("Sabre", " ultrasparc")
      .StartsWith("Hummingbird", "ultrasparc")
      .StartsWith("Cheetah", "ultrasparc3")
      .StartsWith("Jalapeno", "ultrasparc3")
      .StartsWith("Jaguar", "ultrasparc3")
      .StartsWith("Panther", "ultrasparc3")
      .StartsWith("Serrano", "ultrasparc3")
      .StartsWith("UltraSparc T1", "niagara")
      .StartsWith("UltraSparc T2", "niagara2")
      .StartsWith("UltraSparc T3", "niagara3")
      .StartsWith("UltraSparc T4", "niagara4")
      .StartsWith("UltraSparc T5", "niagara4")
      .StartsWith("LEON", "leon3")
      // niagara7/m8 not supported by LLVM yet.
      .StartsWith("SPARC-M7", "niagara4" /* "niagara7" */)
      .StartsWith("SPARC-S7", "niagara4" /* "niagara7" */)
      .StartsWith("SPARC-M8", "niagara4" /* "m8" */)
      .Default("generic");
}
#endif

StringRef sys::getHostCPUName() {
#if defined(__linux__)
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  StringRef Content = P ? P->getBuffer() : "";
  return detail::getHostCPUNameForSPARC(Content);
#elif defined(__sun__) && defined(__svr4__)
  char *buf = NULL;
  kstat_ctl_t *kc;
  kstat_t *ksp;
  kstat_named_t *brand = NULL;

  kc = kstat_open();
  if (kc != NULL) {
    ksp = kstat_lookup(kc, const_cast<char *>("cpu_info"), -1, NULL);
    if (ksp != NULL && kstat_read(kc, ksp, NULL) != -1 &&
        ksp->ks_type == KSTAT_TYPE_NAMED)
      brand =
          (kstat_named_t *)kstat_data_lookup(ksp, const_cast<char *>("brand"));
    if (brand != NULL && brand->data_type == KSTAT_DATA_STRING)
      buf = KSTAT_NAMED_STR_PTR(brand);
  }
  kstat_close(kc);

  return StringSwitch<const char *>(buf)
      .Case("TMS390S10", "supersparc") // Texas Instruments microSPARC I
      .Case("TMS390Z50", "supersparc") // Texas Instruments SuperSPARC I
      .Case("TMS390Z55",
            "supersparc") // Texas Instruments SuperSPARC I with SuperCache
      .Case("MB86904", "supersparc") // Fujitsu microSPARC II
      .Case("MB86907", "supersparc") // Fujitsu TurboSPARC
      .Case("RT623", "hypersparc")   // Ross hyperSPARC
      .Case("RT625", "hypersparc")
      .Case("RT626", "hypersparc")
      .Case("UltraSPARC-I", "ultrasparc")
      .Case("UltraSPARC-II", "ultrasparc")
      .Case("UltraSPARC-IIe", "ultrasparc")
      .Case("UltraSPARC-IIi", "ultrasparc")
      .Case("SPARC64-III", "ultrasparc")
      .Case("SPARC64-IV", "ultrasparc")
      .Case("UltraSPARC-III", "ultrasparc3")
      .Case("UltraSPARC-III+", "ultrasparc3")
      .Case("UltraSPARC-IIIi", "ultrasparc3")
      .Case("UltraSPARC-IIIi+", "ultrasparc3")
      .Case("UltraSPARC-IV", "ultrasparc3")
      .Case("UltraSPARC-IV+", "ultrasparc3")
      .Case("SPARC64-V", "ultrasparc3")
      .Case("SPARC64-VI", "ultrasparc3")
      .Case("SPARC64-VII", "ultrasparc3")
      .Case("UltraSPARC-T1", "niagara")
      .Case("UltraSPARC-T2", "niagara2")
      .Case("UltraSPARC-T2", "niagara2")
      .Case("UltraSPARC-T2+", "niagara2")
      .Case("SPARC-T3", "niagara3")
      .Case("SPARC-T4", "niagara4")
      .Case("SPARC-T5", "niagara4")
      // niagara7/m8 not supported by LLVM yet.
      .Case("SPARC-M7", "niagara4" /* "niagara7" */)
      .Case("SPARC-S7", "niagara4" /* "niagara7" */)
      .Case("SPARC-M8", "niagara4" /* "m8" */)
      .Default("generic");
#else
  return "generic";
#endif
}
#else
StringRef sys::getHostCPUName() { return "generic"; }
namespace llvm {
namespace sys {
namespace detail {
namespace x86 {

VendorSignatures getVendorSignature(unsigned *MaxLeaf) {
  return VendorSignatures::UNKNOWN;
}

} // namespace x86
} // namespace detail
} // namespace sys
} // namespace llvm
#endif

#if defined(__i386__) || defined(_M_IX86) || \
    defined(__x86_64__) || defined(_M_X64)
const StringMap<bool> sys::getHostCPUFeatures() {
  unsigned EAX = 0, EBX = 0, ECX = 0, EDX = 0;
  unsigned MaxLevel;
  StringMap<bool> Features;

  if (getX86CpuIDAndInfo(0, &MaxLevel, &EBX, &ECX, &EDX) || MaxLevel < 1)
    return Features;

  getX86CpuIDAndInfo(1, &EAX, &EBX, &ECX, &EDX);

  Features["cx8"]    = (EDX >>  8) & 1;
  Features["cmov"]   = (EDX >> 15) & 1;
  Features["mmx"]    = (EDX >> 23) & 1;
  Features["fxsr"]   = (EDX >> 24) & 1;
  Features["sse"]    = (EDX >> 25) & 1;
  Features["sse2"]   = (EDX >> 26) & 1;

  Features["sse3"]   = (ECX >>  0) & 1;
  Features["pclmul"] = (ECX >>  1) & 1;
  Features["ssse3"]  = (ECX >>  9) & 1;
  Features["cx16"]   = (ECX >> 13) & 1;
  Features["sse4.1"] = (ECX >> 19) & 1;
  Features["sse4.2"] = (ECX >> 20) & 1;
  Features["crc32"]  = Features["sse4.2"];
  Features["movbe"]  = (ECX >> 22) & 1;
  Features["popcnt"] = (ECX >> 23) & 1;
  Features["aes"]    = (ECX >> 25) & 1;
  Features["rdrnd"]  = (ECX >> 30) & 1;

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  bool HasXSave = ((ECX >> 27) & 1) && !getX86XCR0(&EAX, &EDX);
  bool HasAVXSave = HasXSave && ((ECX >> 28) & 1) && ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVXSave && ((EAX & 0xe0) == 0xe0);
#endif
  // AMX requires additional context to be saved by the OS.
  const unsigned AMXBits = (1 << 17) | (1 << 18);
  bool HasAMXSave = HasXSave && ((EAX & AMXBits) == AMXBits);

  Features["avx"]   = HasAVXSave;
  Features["fma"]   = ((ECX >> 12) & 1) && HasAVXSave;
  // Only enable XSAVE if OS has enabled support for saving YMM state.
  Features["xsave"] = ((ECX >> 26) & 1) && HasAVXSave;
  Features["f16c"]  = ((ECX >> 29) & 1) && HasAVXSave;

  unsigned MaxExtLevel;
  getX86CpuIDAndInfo(0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !getX86CpuIDAndInfo(0x80000001, &EAX, &EBX, &ECX, &EDX);
  Features["sahf"]   = HasExtLeaf1 && ((ECX >>  0) & 1);
  Features["lzcnt"]  = HasExtLeaf1 && ((ECX >>  5) & 1);
  Features["sse4a"]  = HasExtLeaf1 && ((ECX >>  6) & 1);
  Features["prfchw"] = HasExtLeaf1 && ((ECX >>  8) & 1);
  Features["xop"]    = HasExtLeaf1 && ((ECX >> 11) & 1) && HasAVXSave;
  Features["lwp"]    = HasExtLeaf1 && ((ECX >> 15) & 1);
  Features["fma4"]   = HasExtLeaf1 && ((ECX >> 16) & 1) && HasAVXSave;
  Features["tbm"]    = HasExtLeaf1 && ((ECX >> 21) & 1);
  Features["mwaitx"] = HasExtLeaf1 && ((ECX >> 29) & 1);

  Features["64bit"]  = HasExtLeaf1 && ((EDX >> 29) & 1);

  // Miscellaneous memory related features, detected by
  // using the 0x80000008 leaf of the CPUID instruction
  bool HasExtLeaf8 = MaxExtLevel >= 0x80000008 &&
                     !getX86CpuIDAndInfo(0x80000008, &EAX, &EBX, &ECX, &EDX);
  Features["clzero"]   = HasExtLeaf8 && ((EBX >> 0) & 1);
  Features["rdpru"]    = HasExtLeaf8 && ((EBX >> 4) & 1);
  Features["wbnoinvd"] = HasExtLeaf8 && ((EBX >> 9) & 1);

  bool HasLeaf7 =
      MaxLevel >= 7 && !getX86CpuIDAndInfoEx(0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  Features["fsgsbase"]   = HasLeaf7 && ((EBX >>  0) & 1);
  Features["sgx"]        = HasLeaf7 && ((EBX >>  2) & 1);
  Features["bmi"]        = HasLeaf7 && ((EBX >>  3) & 1);
  // AVX2 is only supported if we have the OS save support from AVX.
  Features["avx2"]       = HasLeaf7 && ((EBX >>  5) & 1) && HasAVXSave;
  Features["bmi2"]       = HasLeaf7 && ((EBX >>  8) & 1);
  Features["invpcid"]    = HasLeaf7 && ((EBX >> 10) & 1);
  Features["rtm"]        = HasLeaf7 && ((EBX >> 11) & 1);
  // AVX512 is only supported if the OS supports the context save for it.
  Features["avx512f"]    = HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save;
  if (Features["avx512f"])
    Features["evex512"]  = true;
  Features["avx512dq"]   = HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save;
  Features["rdseed"]     = HasLeaf7 && ((EBX >> 18) & 1);
  Features["adx"]        = HasLeaf7 && ((EBX >> 19) & 1);
  Features["avx512ifma"] = HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save;
  Features["clflushopt"] = HasLeaf7 && ((EBX >> 23) & 1);
  Features["clwb"]       = HasLeaf7 && ((EBX >> 24) & 1);
  Features["avx512cd"]   = HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save;
  Features["sha"]        = HasLeaf7 && ((EBX >> 29) & 1);
  Features["avx512bw"]   = HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save;
  Features["avx512vl"]   = HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save;

  Features["avx512vbmi"]      = HasLeaf7 && ((ECX >>  1) & 1) && HasAVX512Save;
  Features["pku"]             = HasLeaf7 && ((ECX >>  4) & 1);
  Features["waitpkg"]         = HasLeaf7 && ((ECX >>  5) & 1);
  Features["avx512vbmi2"]     = HasLeaf7 && ((ECX >>  6) & 1) && HasAVX512Save;
  Features["shstk"]           = HasLeaf7 && ((ECX >>  7) & 1);
  Features["gfni"]            = HasLeaf7 && ((ECX >>  8) & 1);
  Features["vaes"]            = HasLeaf7 && ((ECX >>  9) & 1) && HasAVXSave;
  Features["vpclmulqdq"]      = HasLeaf7 && ((ECX >> 10) & 1) && HasAVXSave;
  Features["avx512vnni"]      = HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save;
  Features["avx512bitalg"]    = HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save;
  Features["avx512vpopcntdq"] = HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save;
  Features["rdpid"]           = HasLeaf7 && ((ECX >> 22) & 1);
  Features["kl"]              = HasLeaf7 && ((ECX >> 23) & 1); // key locker
  Features["cldemote"]        = HasLeaf7 && ((ECX >> 25) & 1);
  Features["movdiri"]         = HasLeaf7 && ((ECX >> 27) & 1);
  Features["movdir64b"]       = HasLeaf7 && ((ECX >> 28) & 1);
  Features["enqcmd"]          = HasLeaf7 && ((ECX >> 29) & 1);

  Features["uintr"]           = HasLeaf7 && ((EDX >> 5) & 1);
  Features["avx512vp2intersect"] =
      HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save;
  Features["serialize"]       = HasLeaf7 && ((EDX >> 14) & 1);
  Features["tsxldtrk"]        = HasLeaf7 && ((EDX >> 16) & 1);
  // There are two CPUID leafs which information associated with the pconfig
  // instruction:
  // EAX=0x7, ECX=0x0 indicates the availability of the instruction (via the 18th
  // bit of EDX), while the EAX=0x1b leaf returns information on the
  // availability of specific pconfig leafs.
  // The target feature here only refers to the the first of these two.
  // Users might need to check for the availability of specific pconfig
  // leaves using cpuid, since that information is ignored while
  // detecting features using the "-march=native" flag.
  // For more info, see X86 ISA docs.
  Features["pconfig"] = HasLeaf7 && ((EDX >> 18) & 1);
  Features["amx-bf16"]   = HasLeaf7 && ((EDX >> 22) & 1) && HasAMXSave;
  Features["avx512fp16"] = HasLeaf7 && ((EDX >> 23) & 1) && HasAVX512Save;
  Features["amx-tile"]   = HasLeaf7 && ((EDX >> 24) & 1) && HasAMXSave;
  Features["amx-int8"]   = HasLeaf7 && ((EDX >> 25) & 1) && HasAMXSave;
  // EAX from subleaf 0 is the maximum subleaf supported. Some CPUs don't
  // return all 0s for invalid subleaves so check the limit.
  bool HasLeaf7Subleaf1 =
      HasLeaf7 && EAX >= 1 &&
      !getX86CpuIDAndInfoEx(0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  Features["sha512"]     = HasLeaf7Subleaf1 && ((EAX >> 0) & 1);
  Features["sm3"]        = HasLeaf7Subleaf1 && ((EAX >> 1) & 1);
  Features["sm4"]        = HasLeaf7Subleaf1 && ((EAX >> 2) & 1);
  Features["raoint"]     = HasLeaf7Subleaf1 && ((EAX >> 3) & 1);
  Features["avxvnni"]    = HasLeaf7Subleaf1 && ((EAX >> 4) & 1) && HasAVXSave;
  Features["avx512bf16"] = HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save;
  Features["amx-fp16"]   = HasLeaf7Subleaf1 && ((EAX >> 21) & 1) && HasAMXSave;
  Features["cmpccxadd"]  = HasLeaf7Subleaf1 && ((EAX >> 7) & 1);
  Features["hreset"]     = HasLeaf7Subleaf1 && ((EAX >> 22) & 1);
  Features["avxifma"]    = HasLeaf7Subleaf1 && ((EAX >> 23) & 1) && HasAVXSave;
  Features["avxvnniint8"] = HasLeaf7Subleaf1 && ((EDX >> 4) & 1) && HasAVXSave;
  Features["avxneconvert"] = HasLeaf7Subleaf1 && ((EDX >> 5) & 1) && HasAVXSave;
  Features["amx-complex"] = HasLeaf7Subleaf1 && ((EDX >> 8) & 1) && HasAMXSave;
  Features["avxvnniint16"] = HasLeaf7Subleaf1 && ((EDX >> 10) & 1) && HasAVXSave;
  Features["prefetchi"]  = HasLeaf7Subleaf1 && ((EDX >> 14) & 1);
  Features["usermsr"]  = HasLeaf7Subleaf1 && ((EDX >> 15) & 1);
  Features["avx10.1-256"] = HasLeaf7Subleaf1 && ((EDX >> 19) & 1);
  bool HasAPXF = HasLeaf7Subleaf1 && ((EDX >> 21) & 1);
  Features["egpr"] = HasAPXF;
  Features["push2pop2"] = HasAPXF;
  Features["ppx"] = HasAPXF;
  Features["ndd"] = HasAPXF;
  Features["ccmp"] = HasAPXF;
  Features["cf"] = HasAPXF;

  bool HasLeafD = MaxLevel >= 0xd &&
                  !getX86CpuIDAndInfoEx(0xd, 0x1, &EAX, &EBX, &ECX, &EDX);

  // Only enable XSAVE if OS has enabled support for saving YMM state.
  Features["xsaveopt"] = HasLeafD && ((EAX >> 0) & 1) && HasAVXSave;
  Features["xsavec"]   = HasLeafD && ((EAX >> 1) & 1) && HasAVXSave;
  Features["xsaves"]   = HasLeafD && ((EAX >> 3) & 1) && HasAVXSave;

  bool HasLeaf14 = MaxLevel >= 0x14 &&
                  !getX86CpuIDAndInfoEx(0x14, 0x0, &EAX, &EBX, &ECX, &EDX);

  Features["ptwrite"] = HasLeaf14 && ((EBX >> 4) & 1);

  bool HasLeaf19 =
      MaxLevel >= 0x19 && !getX86CpuIDAndInfo(0x19, &EAX, &EBX, &ECX, &EDX);
  Features["widekl"] = HasLeaf7 && HasLeaf19 && ((EBX >> 2) & 1);

  bool HasLeaf24 =
      MaxLevel >= 0x24 && !getX86CpuIDAndInfo(0x24, &EAX, &EBX, &ECX, &EDX);
  Features["avx10.1-512"] =
      Features["avx10.1-256"] && HasLeaf24 && ((EBX >> 18) & 1);

  return Features;
}
#elif defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
const StringMap<bool> sys::getHostCPUFeatures() {
  StringMap<bool> Features;
  std::unique_ptr<llvm::MemoryBuffer> P = getProcCpuinfoContent();
  if (!P)
    return Features;

  SmallVector<StringRef, 32> Lines;
  P->getBuffer().split(Lines, "\n");

  SmallVector<StringRef, 32> CPUFeatures;

  // Look for the CPU features.
  for (unsigned I = 0, E = Lines.size(); I != E; ++I)
    if (Lines[I].starts_with("Features")) {
      Lines[I].split(CPUFeatures, ' ');
      break;
    }

#if defined(__aarch64__)
  // Keep track of which crypto features we have seen
  enum { CAP_AES = 0x1, CAP_PMULL = 0x2, CAP_SHA1 = 0x4, CAP_SHA2 = 0x8 };
  uint32_t crypto = 0;
#endif

  for (unsigned I = 0, E = CPUFeatures.size(); I != E; ++I) {
    StringRef LLVMFeatureStr = StringSwitch<StringRef>(CPUFeatures[I])
#if defined(__aarch64__)
                                   .Case("asimd", "neon")
                                   .Case("fp", "fp-armv8")
                                   .Case("crc32", "crc")
                                   .Case("atomics", "lse")
                                   .Case("sve", "sve")
                                   .Case("sve2", "sve2")
#else
                                   .Case("half", "fp16")
                                   .Case("neon", "neon")
                                   .Case("vfpv3", "vfp3")
                                   .Case("vfpv3d16", "vfp3d16")
                                   .Case("vfpv4", "vfp4")
                                   .Case("idiva", "hwdiv-arm")
                                   .Case("idivt", "hwdiv")
#endif
                                   .Default("");

#if defined(__aarch64__)
    // We need to check crypto separately since we need all of the crypto
    // extensions to enable the subtarget feature
    if (CPUFeatures[I] == "aes")
      crypto |= CAP_AES;
    else if (CPUFeatures[I] == "pmull")
      crypto |= CAP_PMULL;
    else if (CPUFeatures[I] == "sha1")
      crypto |= CAP_SHA1;
    else if (CPUFeatures[I] == "sha2")
      crypto |= CAP_SHA2;
#endif

    if (LLVMFeatureStr != "")
      Features[LLVMFeatureStr] = true;
  }

#if defined(__aarch64__)
  // If we have all crypto bits we can add the feature
  if (crypto == (CAP_AES | CAP_PMULL | CAP_SHA1 | CAP_SHA2))
    Features["crypto"] = true;
#endif

  return Features;
}
#elif defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
const StringMap<bool> sys::getHostCPUFeatures() {
  StringMap<bool> Features;

  if (IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE))
    Features["neon"] = true;
  if (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE))
    Features["crc"] = true;
  if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE))
    Features["crypto"] = true;

  return Features;
}
#elif defined(__linux__) && defined(__loongarch__)
#include <sys/auxv.h>
const StringMap<bool> sys::getHostCPUFeatures() {
  unsigned long hwcap = getauxval(AT_HWCAP);
  bool HasFPU = hwcap & (1UL << 3); // HWCAP_LOONGARCH_FPU
  uint32_t cpucfg2 = 0x2;
  __asm__("cpucfg %[cpucfg2], %[cpucfg2]\n\t" : [cpucfg2] "+r"(cpucfg2));

  StringMap<bool> Features;

  Features["f"] = HasFPU && (cpucfg2 & (1U << 1)); // CPUCFG.2.FP_SP
  Features["d"] = HasFPU && (cpucfg2 & (1U << 2)); // CPUCFG.2.FP_DP

  Features["lsx"] = hwcap & (1UL << 4);  // HWCAP_LOONGARCH_LSX
  Features["lasx"] = hwcap & (1UL << 5); // HWCAP_LOONGARCH_LASX
  Features["lvz"] = hwcap & (1UL << 9);  // HWCAP_LOONGARCH_LVZ

  return Features;
}
#elif defined(__linux__) && defined(__riscv)
// struct riscv_hwprobe
struct RISCVHwProbe {
  int64_t Key;
  uint64_t Value;
};
const StringMap<bool> sys::getHostCPUFeatures() {
  RISCVHwProbe Query[]{{/*RISCV_HWPROBE_KEY_BASE_BEHAVIOR=*/3, 0},
                       {/*RISCV_HWPROBE_KEY_IMA_EXT_0=*/4, 0}};
  int Ret = syscall(/*__NR_riscv_hwprobe=*/258, /*pairs=*/Query,
                    /*pair_count=*/std::size(Query), /*cpu_count=*/0,
                    /*cpus=*/0, /*flags=*/0);
  if (Ret != 0)
    return {};

  StringMap<bool> Features;
  uint64_t BaseMask = Query[0].Value;
  // Check whether RISCV_HWPROBE_BASE_BEHAVIOR_IMA is set.
  if (BaseMask & 1) {
    Features["i"] = true;
    Features["m"] = true;
    Features["a"] = true;
  }

  uint64_t ExtMask = Query[1].Value;
  Features["f"] = ExtMask & (1 << 0);           // RISCV_HWPROBE_IMA_FD
  Features["d"] = ExtMask & (1 << 0);           // RISCV_HWPROBE_IMA_FD
  Features["c"] = ExtMask & (1 << 1);           // RISCV_HWPROBE_IMA_C
  Features["v"] = ExtMask & (1 << 2);           // RISCV_HWPROBE_IMA_V
  Features["zba"] = ExtMask & (1 << 3);         // RISCV_HWPROBE_EXT_ZBA
  Features["zbb"] = ExtMask & (1 << 4);         // RISCV_HWPROBE_EXT_ZBB
  Features["zbs"] = ExtMask & (1 << 5);         // RISCV_HWPROBE_EXT_ZBS
  Features["zicboz"] = ExtMask & (1 << 6);      // RISCV_HWPROBE_EXT_ZICBOZ
  Features["zbc"] = ExtMask & (1 << 7);         // RISCV_HWPROBE_EXT_ZBC
  Features["zbkb"] = ExtMask & (1 << 8);        // RISCV_HWPROBE_EXT_ZBKB
  Features["zbkc"] = ExtMask & (1 << 9);        // RISCV_HWPROBE_EXT_ZBKC
  Features["zbkx"] = ExtMask & (1 << 10);       // RISCV_HWPROBE_EXT_ZBKX
  Features["zknd"] = ExtMask & (1 << 11);       // RISCV_HWPROBE_EXT_ZKND
  Features["zkne"] = ExtMask & (1 << 12);       // RISCV_HWPROBE_EXT_ZKNE
  Features["zknh"] = ExtMask & (1 << 13);       // RISCV_HWPROBE_EXT_ZKNH
  Features["zksed"] = ExtMask & (1 << 14);      // RISCV_HWPROBE_EXT_ZKSED
  Features["zksh"] = ExtMask & (1 << 15);       // RISCV_HWPROBE_EXT_ZKSH
  Features["zkt"] = ExtMask & (1 << 16);        // RISCV_HWPROBE_EXT_ZKT
  Features["zvbb"] = ExtMask & (1 << 17);       // RISCV_HWPROBE_EXT_ZVBB
  Features["zvbc"] = ExtMask & (1 << 18);       // RISCV_HWPROBE_EXT_ZVBC
  Features["zvkb"] = ExtMask & (1 << 19);       // RISCV_HWPROBE_EXT_ZVKB
  Features["zvkg"] = ExtMask & (1 << 20);       // RISCV_HWPROBE_EXT_ZVKG
  Features["zvkned"] = ExtMask & (1 << 21);     // RISCV_HWPROBE_EXT_ZVKNED
  Features["zvknha"] = ExtMask & (1 << 22);     // RISCV_HWPROBE_EXT_ZVKNHA
  Features["zvknhb"] = ExtMask & (1 << 23);     // RISCV_HWPROBE_EXT_ZVKNHB
  Features["zvksed"] = ExtMask & (1 << 24);     // RISCV_HWPROBE_EXT_ZVKSED
  Features["zvksh"] = ExtMask & (1 << 25);      // RISCV_HWPROBE_EXT_ZVKSH
  Features["zvkt"] = ExtMask & (1 << 26);       // RISCV_HWPROBE_EXT_ZVKT
  Features["zfh"] = ExtMask & (1 << 27);        // RISCV_HWPROBE_EXT_ZFH
  Features["zfhmin"] = ExtMask & (1 << 28);     // RISCV_HWPROBE_EXT_ZFHMIN
  Features["zihintntl"] = ExtMask & (1 << 29);  // RISCV_HWPROBE_EXT_ZIHINTNTL
  Features["zvfh"] = ExtMask & (1 << 30);       // RISCV_HWPROBE_EXT_ZVFH
  Features["zvfhmin"] = ExtMask & (1ULL << 31); // RISCV_HWPROBE_EXT_ZVFHMIN
  Features["zfa"] = ExtMask & (1ULL << 32);     // RISCV_HWPROBE_EXT_ZFA
  Features["ztso"] = ExtMask & (1ULL << 33);    // RISCV_HWPROBE_EXT_ZTSO
  // TODO: Re-enable zacas when it is marked non-experimental again.
  // Features["zacas"] = ExtMask & (1ULL << 34);   // RISCV_HWPROBE_EXT_ZACAS
  Features["zicond"] = ExtMask & (1ULL << 35);  // RISCV_HWPROBE_EXT_ZICOND
  Features["zihintpause"] =
      ExtMask & (1ULL << 36); // RISCV_HWPROBE_EXT_ZIHINTPAUSE

  // TODO: set unaligned-scalar-mem if RISCV_HWPROBE_KEY_MISALIGNED_PERF returns
  // RISCV_HWPROBE_MISALIGNED_FAST.

  return Features;
}
#else
const StringMap<bool> sys::getHostCPUFeatures() { return {}; }
#endif

#if __APPLE__
/// \returns the \p triple, but with the Host's arch spliced in.
static Triple withHostArch(Triple T) {
#if defined(__arm__)
  T.setArch(Triple::arm);
  T.setArchName("arm");
#elif defined(__arm64e__)
  T.setArch(Triple::aarch64, Triple::AArch64SubArch_arm64e);
  T.setArchName("arm64e");
#elif defined(__aarch64__)
  T.setArch(Triple::aarch64);
  T.setArchName("arm64");
#elif defined(__x86_64h__)
  T.setArch(Triple::x86_64);
  T.setArchName("x86_64h");
#elif defined(__x86_64__)
  T.setArch(Triple::x86_64);
  T.setArchName("x86_64");
#elif defined(__i386__)
  T.setArch(Triple::x86);
  T.setArchName("i386");
#elif defined(__powerpc__)
  T.setArch(Triple::ppc);
  T.setArchName("powerpc");
#else
#  error "Unimplemented host arch fixup"
#endif
  return T;
}
#endif

std::string sys::getProcessTriple() {
  std::string TargetTripleString = updateTripleOSVersion(LLVM_HOST_TRIPLE);
  Triple PT(Triple::normalize(TargetTripleString));

#if __APPLE__
  /// In Universal builds, LLVM_HOST_TRIPLE will have the wrong arch in one of
  /// the slices. This fixes that up.
  PT = withHostArch(PT);
#endif

  if (sizeof(void *) == 8 && PT.isArch32Bit())
    PT = PT.get64BitArchVariant();
  if (sizeof(void *) == 4 && PT.isArch64Bit())
    PT = PT.get32BitArchVariant();

  return PT.str();
}

void sys::printDefaultTargetAndDetectedCPU(raw_ostream &OS) {
#if LLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO
  std::string CPU = std::string(sys::getHostCPUName());
  if (CPU == "generic")
    CPU = "(unknown)";
  OS << "  Default target: " << sys::getDefaultTargetTriple() << '\n'
     << "  Host CPU: " << CPU << '\n';
#endif
}
