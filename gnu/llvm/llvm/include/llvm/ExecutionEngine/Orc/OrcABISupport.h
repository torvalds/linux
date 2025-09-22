//===- OrcABISupport.h - ABI support code -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ABI specific code for Orc, e.g. callback assembly.
//
// ABI classes should be part of the JIT *target* process, not the host
// process (except where you're doing hosted JITing and the two are one and the
// same).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_ORCABISUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_ORCABISUPPORT_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>

namespace llvm {
namespace orc {

struct IndirectStubsAllocationSizes {
  uint64_t StubBytes = 0;
  uint64_t PointerBytes = 0;
  unsigned NumStubs = 0;
};

template <typename ORCABI>
IndirectStubsAllocationSizes
getIndirectStubsBlockSizes(unsigned MinStubs, unsigned RoundToMultipleOf = 0) {
  assert(
      (RoundToMultipleOf == 0 || (RoundToMultipleOf % ORCABI::StubSize == 0)) &&
      "RoundToMultipleOf is not a multiple of stub size");
  uint64_t StubBytes = MinStubs * ORCABI::StubSize;
  if (RoundToMultipleOf)
    StubBytes = alignTo(StubBytes, RoundToMultipleOf);
  unsigned NumStubs = StubBytes / ORCABI::StubSize;
  uint64_t PointerBytes = NumStubs * ORCABI::PointerSize;
  return {StubBytes, PointerBytes, NumStubs};
}

/// Generic ORC ABI support.
///
/// This class can be substituted as the target architecture support class for
/// ORC templates that require one (e.g. IndirectStubsManagers). It does not
/// support lazy JITing however, and any attempt to use that functionality
/// will result in execution of an llvm_unreachable.
class OrcGenericABI {
public:
  static constexpr unsigned PointerSize = sizeof(uintptr_t);
  static constexpr unsigned TrampolineSize = 1;
  static constexpr unsigned StubSize = 1;
  static constexpr unsigned StubToPointerMaxDisplacement = 1;
  static constexpr unsigned ResolverCodeSize = 1;

  static void writeResolverCode(char *ResolveWorkingMem,
                                ExecutorAddr ResolverTargetAddr,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr) {
    llvm_unreachable("writeResolverCode is not supported by the generic host "
                     "support class");
  }

  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddr,
                               ExecutorAddr ResolverAddr,
                               unsigned NumTrampolines) {
    llvm_unreachable("writeTrampolines is not supported by the generic host "
                     "support class");
  }

  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs) {
    llvm_unreachable(
        "writeIndirectStubsBlock is not supported by the generic host "
        "support class");
  }
};

class OrcAArch64 {
public:
  static constexpr unsigned PointerSize = 8;
  static constexpr unsigned TrampolineSize = 12;
  static constexpr unsigned StubSize = 8;
  static constexpr unsigned StubToPointerMaxDisplacement = 1U << 27;
  static constexpr unsigned ResolverCodeSize = 0x120;

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr RentryCtxAddr);

  /// Write the requested number of trampolines into the given memory,
  /// which must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddress,
                               ExecutorAddr ResolverAddr,
                               unsigned NumTrampolines);

  /// Write NumStubs indirect stubs to working memory at StubsBlockWorkingMem.
  /// Stubs will be written as if linked at StubsBlockTargetAddress, with the
  /// Nth stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned MinStubs);
};

/// X86_64 code that's common to all ABIs.
///
/// X86_64 supports lazy JITing.
class OrcX86_64_Base {
public:
  static constexpr unsigned PointerSize = 8;
  static constexpr unsigned TrampolineSize = 8;
  static constexpr unsigned StubSize = 8;
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;

  /// Write the requested number of trampolines into the given memory,
  /// which must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddress,
                               ExecutorAddr ResolverAddr,
                               unsigned NumTrampolines);

  /// Write NumStubs indirect stubs to working memory at StubsBlockWorkingMem.
  /// Stubs will be written as if linked at StubsBlockTargetAddress, with the
  /// Nth stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs);
};

/// X86_64 support for SysV ABI (Linux, MacOSX).
///
/// X86_64_SysV supports lazy JITing.
class OrcX86_64_SysV : public OrcX86_64_Base {
public:
  static constexpr unsigned ResolverCodeSize = 0x6C;

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr);
};

/// X86_64 support for Win32.
///
/// X86_64_Win32 supports lazy JITing.
class OrcX86_64_Win32 : public OrcX86_64_Base {
public:
  static constexpr unsigned ResolverCodeSize = 0x74;

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr);
};

/// I386 support.
///
/// I386 supports lazy JITing.
class OrcI386 {
public:
  static constexpr unsigned PointerSize = 4;
  static constexpr unsigned TrampolineSize = 8;
  static constexpr unsigned StubSize = 8;
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  static constexpr unsigned ResolverCodeSize = 0x4a;

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory,
  /// which must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddress,
                               ExecutorAddr ResolverAddr,
                               unsigned NumTrampolines);

  /// Write NumStubs indirect stubs to working memory at StubsBlockWorkingMem.
  /// Stubs will be written as if linked at StubsBlockTargetAddress, with the
  /// Nth stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs);
};

// @brief Mips32 support.
//
// Mips32 supports lazy JITing.
class OrcMips32_Base {
public:
  static constexpr unsigned PointerSize = 4;
  static constexpr unsigned TrampolineSize = 20;
  static constexpr unsigned StubSize = 8;
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  static constexpr unsigned ResolverCodeSize = 0xfc;

  /// Write the requested number of trampolines into the given memory,
  /// which must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddress,
                               ExecutorAddr ResolverAddr,
                               unsigned NumTrampolines);

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverBlockWorkingMem,
                                ExecutorAddr ResolverBlockTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr, bool isBigEndian);
  /// Write NumStubs indirect stubs to working memory at StubsBlockWorkingMem.
  /// Stubs will be written as if linked at StubsBlockTargetAddress, with the
  /// Nth stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs);
};

class OrcMips32Le : public OrcMips32_Base {
public:
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr) {
    OrcMips32_Base::writeResolverCode(ResolverWorkingMem, ResolverTargetAddress,
                                      ReentryFnAddr, ReentryCtxAddr, false);
  }
};

class OrcMips32Be : public OrcMips32_Base {
public:
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr) {
    OrcMips32_Base::writeResolverCode(ResolverWorkingMem, ResolverTargetAddress,
                                      ReentryFnAddr, ReentryCtxAddr, true);
  }
};

// @brief Mips64 support.
//
// Mips64 supports lazy JITing.
class OrcMips64 {
public:
  static constexpr unsigned PointerSize = 8;
  static constexpr unsigned TrampolineSize = 40;
  static constexpr unsigned StubSize = 32;
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  static constexpr unsigned ResolverCodeSize = 0x120;

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory,
  /// which must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddress,
                               ExecutorAddr ResolverFnAddr,
                               unsigned NumTrampolines);
  /// Write NumStubs indirect stubs to working memory at StubsBlockWorkingMem.
  /// Stubs will be written as if linked at StubsBlockTargetAddress, with the
  /// Nth stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs);
};

// @brief riscv64 support.
//
// RISC-V 64 supports lazy JITing.
class OrcRiscv64 {
public:
  static constexpr unsigned PointerSize = 8;
  static constexpr unsigned TrampolineSize = 16;
  static constexpr unsigned StubSize = 16;
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  static constexpr unsigned ResolverCodeSize = 0x148;

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory,
  /// which must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddress,
                               ExecutorAddr ResolverFnAddr,
                               unsigned NumTrampolines);
  /// Write NumStubs indirect stubs to working memory at StubsBlockWorkingMem.
  /// Stubs will be written as if linked at StubsBlockTargetAddress, with the
  /// Nth stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs);
};

// @brief loongarch64 support.
//
// LoongArch 64 supports lazy JITing.
class OrcLoongArch64 {
public:
  static constexpr unsigned PointerSize = 8;
  static constexpr unsigned TrampolineSize = 16;
  static constexpr unsigned StubSize = 16;
  static constexpr unsigned StubToPointerMaxDisplacement = 1 << 31;
  static constexpr unsigned ResolverCodeSize = 0xc8;

  /// Write the resolver code into the given memory. The user is
  /// responsible for allocating the memory and setting permissions.
  ///
  /// ReentryFnAddr should be the address of a function whose signature matches
  /// void* (*)(void *TrampolineAddr, void *ReentryCtxAddr). The ReentryCtxAddr
  /// argument of writeResolverCode will be passed as the second argument to
  /// the function at ReentryFnAddr.
  static void writeResolverCode(char *ResolverWorkingMem,
                                ExecutorAddr ResolverTargetAddress,
                                ExecutorAddr ReentryFnAddr,
                                ExecutorAddr ReentryCtxAddr);

  /// Write the requested number of trampolines into the given memory,
  /// which must be big enough to hold 1 pointer, plus NumTrampolines
  /// trampolines.
  static void writeTrampolines(char *TrampolineBlockWorkingMem,
                               ExecutorAddr TrampolineBlockTargetAddress,
                               ExecutorAddr ResolverFnAddr,
                               unsigned NumTrampolines);

  /// Write NumStubs indirect stubs to working memory at StubsBlockWorkingMem.
  /// Stubs will be written as if linked at StubsBlockTargetAddress, with the
  /// Nth stub using the Nth pointer in memory starting at
  /// PointersBlockTargetAddress.
  static void writeIndirectStubsBlock(char *StubsBlockWorkingMem,
                                      ExecutorAddr StubsBlockTargetAddress,
                                      ExecutorAddr PointersBlockTargetAddress,
                                      unsigned NumStubs);
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_ORCABISUPPORT_H
