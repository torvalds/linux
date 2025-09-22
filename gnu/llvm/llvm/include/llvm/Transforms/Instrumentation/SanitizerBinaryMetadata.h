//===------- Definition of the SanitizerBinaryMetadata class ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SanitizerBinaryMetadata pass.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERBINARYMETADATA_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERBINARYMETADATA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation.h"

namespace llvm {

struct SanitizerBinaryMetadataOptions {
  bool Covered = false;
  bool Atomics = false;
  bool UAR = false;
  SanitizerBinaryMetadataOptions() = default;
};

inline constexpr int kSanitizerBinaryMetadataAtomicsBit = 0;
inline constexpr int kSanitizerBinaryMetadataUARBit = 1;
inline constexpr int kSanitizerBinaryMetadataUARHasSizeBit = 2;

inline constexpr uint64_t kSanitizerBinaryMetadataAtomics =
    1 << kSanitizerBinaryMetadataAtomicsBit;
inline constexpr uint64_t kSanitizerBinaryMetadataUAR =
    1 << kSanitizerBinaryMetadataUARBit;
inline constexpr uint64_t kSanitizerBinaryMetadataUARHasSize =
    1 << kSanitizerBinaryMetadataUARHasSizeBit;

inline constexpr char kSanitizerBinaryMetadataCoveredSection[] =
    "sanmd_covered";
inline constexpr char kSanitizerBinaryMetadataAtomicsSection[] =
    "sanmd_atomics";

/// Public interface to the SanitizerBinaryMetadata module pass for emitting
/// metadata for binary analysis sanitizers.
//
/// The pass should be inserted after optimizations.
class SanitizerBinaryMetadataPass
    : public PassInfoMixin<SanitizerBinaryMetadataPass> {
public:
  explicit SanitizerBinaryMetadataPass(
      SanitizerBinaryMetadataOptions Opts = {},
      ArrayRef<std::string> IgnorelistFiles = {});
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  const SanitizerBinaryMetadataOptions Options;
  const ArrayRef<std::string> IgnorelistFiles;
};

} // namespace llvm

#endif
