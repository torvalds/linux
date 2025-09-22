//===--------- Definition of the AddressSanitizer options -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines data types used to set Address Sanitizer options.
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZEROPTIONS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZEROPTIONS_H

namespace llvm {

/// Types of ASan module destructors supported
enum class AsanDtorKind {
  None,    ///< Do not emit any destructors for ASan
  Global,  ///< Append to llvm.global_dtors
  Invalid, ///< Not a valid destructor Kind.
};

/// Types of ASan module constructors supported
enum class AsanCtorKind {
  None,
  Global
};

/// Mode of ASan detect stack use after return
enum class AsanDetectStackUseAfterReturnMode {
  Never,   ///< Never detect stack use after return.
  Runtime, ///< Detect stack use after return if not disabled runtime with
           ///< (ASAN_OPTIONS=detect_stack_use_after_return=0).
  Always,  ///< Always detect stack use after return.
  Invalid, ///< Not a valid detect mode.
};

} // namespace llvm

#endif
