//===- llvm/TargetParser/Host.h - Host machine detection  -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Methods for querying the nature of the host machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_HOST_H
#define LLVM_TARGETPARSER_HOST_H

#include <string>

namespace llvm {
class MallocAllocator;
class StringRef;
template <typename ValueTy, typename AllocatorTy> class StringMap;
class raw_ostream;

namespace sys {

  /// getDefaultTargetTriple() - Return the default target triple the compiler
  /// has been configured to produce code for.
  ///
  /// The target triple is a string in the format of:
  ///   CPU_TYPE-VENDOR-OPERATING_SYSTEM
  /// or
  ///   CPU_TYPE-VENDOR-KERNEL-OPERATING_SYSTEM
  std::string getDefaultTargetTriple();

  /// getProcessTriple() - Return an appropriate target triple for generating
  /// code to be loaded into the current process, e.g. when using the JIT.
  std::string getProcessTriple();

  /// getHostCPUName - Get the LLVM name for the host CPU. The particular format
  /// of the name is target dependent, and suitable for passing as -mcpu to the
  /// target which matches the host.
  ///
  /// \return - The host CPU name, or empty if the CPU could not be determined.
  StringRef getHostCPUName();

  /// getHostCPUFeatures - Get the LLVM names for the host CPU features.
  /// The particular format of the names are target dependent, and suitable for
  /// passing as -mattr to the target which matches the host.
  ///
  /// \return - A string map mapping feature names to either true (if enabled)
  /// or false (if disabled). This routine makes no guarantees about exactly
  /// which features may appear in this map, except that they are all valid LLVM
  /// feature names. The map can be empty, for example if feature detection
  /// fails.
  const StringMap<bool, MallocAllocator> getHostCPUFeatures();

  /// This is a function compatible with cl::AddExtraVersionPrinter, which adds
  /// info about the current target triple and detected CPU.
  void printDefaultTargetAndDetectedCPU(raw_ostream &OS);

  namespace detail {
  /// Helper functions to extract HostCPUName from /proc/cpuinfo on linux.
  StringRef getHostCPUNameForPowerPC(StringRef ProcCpuinfoContent);
  StringRef getHostCPUNameForARM(StringRef ProcCpuinfoContent);
  StringRef getHostCPUNameForS390x(StringRef ProcCpuinfoContent);
  StringRef getHostCPUNameForRISCV(StringRef ProcCpuinfoContent);
  StringRef getHostCPUNameForSPARC(StringRef ProcCpuinfoContent);
  StringRef getHostCPUNameForBPF();

  /// Helper functions to extract CPU details from CPUID on x86.
  namespace x86 {
  enum class VendorSignatures {
    UNKNOWN,
    GENUINE_INTEL,
    AUTHENTIC_AMD,
  };

  /// Returns the host CPU's vendor.
  /// MaxLeaf: if a non-nullptr pointer is specified, the EAX value will be
  /// assigned to its pointee.
  VendorSignatures getVendorSignature(unsigned *MaxLeaf = nullptr);
  } // namespace x86
  }
}
}

#endif
