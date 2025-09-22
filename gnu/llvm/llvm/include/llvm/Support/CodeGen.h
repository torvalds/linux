//===-- llvm/Support/CodeGen.h - CodeGen Concepts ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file define some types which define code generation concepts. For
// example, relocation model.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CODEGEN_H
#define LLVM_SUPPORT_CODEGEN_H

#include <cstdint>
#include <optional>

namespace llvm {

  // Relocation model types.
  namespace Reloc {
    // Cannot be named PIC due to collision with -DPIC
    enum Model { Static, PIC_, DynamicNoPIC, ROPI, RWPI, ROPI_RWPI };
  }

  // Code model types.
  namespace CodeModel {
    // Sync changes with CodeGenCWrappers.h.
    enum Model { Tiny, Small, Kernel, Medium, Large };
  }

  namespace PICLevel {
    // This is used to map -fpic/-fPIC.
    enum Level { NotPIC=0, SmallPIC=1, BigPIC=2 };
  }

  namespace PIELevel {
    enum Level { Default=0, Small=1, Large=2 };
  }

  // TLS models.
  namespace TLSModel {
    enum Model {
      GeneralDynamic,
      LocalDynamic,
      InitialExec,
      LocalExec
    };
  }

  /// Code generation optimization level.
  enum class CodeGenOptLevel {
    None = 0,      ///< -O0
    Less = 1,      ///< -O1
    Default = 2,   ///< -O2, -Os
    Aggressive = 3 ///< -O3
  };

  namespace CodeGenOpt {
  /// Get the \c Level identified by the integer \p OL.
  ///
  /// Returns std::nullopt if \p OL is invalid.
  inline std::optional<CodeGenOptLevel> getLevel(int OL) {
    if (OL < 0 || OL > 3)
      return std::nullopt;
    return static_cast<CodeGenOptLevel>(OL);
  }
  /// Parse \p C as a single digit integer and get matching \c CodeGenLevel.
  ///
  /// Returns std::nullopt if the input is not a valid optimization level.
  inline std::optional<CodeGenOptLevel> parseLevel(char C) {
    if (C < '0')
      return std::nullopt;
    return getLevel(static_cast<int>(C - '0'));
  }
  } // namespace CodeGenOpt

  /// These enums are meant to be passed into addPassesToEmitFile to indicate
  /// what type of file to emit, and returned by it to indicate what type of
  /// file could actually be made.
  enum class CodeGenFileType {
    AssemblyFile,
    ObjectFile,
    Null // Do not emit any output.
  };

  // Specify what functions should keep the frame pointer.
  enum class FramePointerKind { None, NonLeaf, All, Reserved };

  // Specify what type of zeroing callee-used registers.
  namespace ZeroCallUsedRegs {
  const unsigned ONLY_USED = 1U << 1;
  const unsigned ONLY_GPR = 1U << 2;
  const unsigned ONLY_ARG = 1U << 3;

  enum class ZeroCallUsedRegsKind : unsigned int {
    // Don't zero any call-used regs.
    Skip = 1U << 0,
    // Only zeros call-used GPRs used in the fn and pass args.
    UsedGPRArg = ONLY_USED | ONLY_GPR | ONLY_ARG,
    // Only zeros call-used GPRs used in the fn.
    UsedGPR = ONLY_USED | ONLY_GPR,
    // Only zeros call-used regs used in the fn and pass args.
    UsedArg = ONLY_USED | ONLY_ARG,
    // Only zeros call-used regs used in the fn.
    Used = ONLY_USED,
    // Zeros all call-used GPRs that pass args.
    AllGPRArg = ONLY_GPR | ONLY_ARG,
    // Zeros all call-used GPRs.
    AllGPR = ONLY_GPR,
    // Zeros all call-used regs that pass args.
    AllArg = ONLY_ARG,
    // Zeros all call-used regs.
    All = 0,
  };
  } // namespace ZeroCallUsedRegs

  enum class UWTableKind {
    None = 0,  ///< No unwind table requested
    Sync = 1,  ///< "Synchronous" unwind tables
    Async = 2, ///< "Asynchronous" unwind tables (instr precise)
    Default = 2,
  };

  enum class FunctionReturnThunksKind : unsigned int {
    Keep = 0,    ///< No function return thunk.
    Extern = 1,  ///< Replace returns with jump to thunk, don't emit thunk.
    Invalid = 2, ///< Not used.
  };

  } // namespace llvm

#endif
