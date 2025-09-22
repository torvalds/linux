//===- llvm/TargetParser/Win32/Host.inc -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Win32 Host support.
//
//===----------------------------------------------------------------------===//

// We need to include config.h here because LLVM_DEFAULT_TARGET_TRIPLE is not
// defined in llvm-config.h if it is unset.
#include "llvm/Config/config.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#include <cstdio>
#include <string>

using namespace llvm;

static std::string updateTripleOSVersion(std::string Triple) { return Triple; }

std::string sys::getDefaultTargetTriple() {
  const char *Triple = LLVM_DEFAULT_TARGET_TRIPLE;

  // Override the default target with an environment variable named by
  // LLVM_TARGET_TRIPLE_ENV.
#if defined(LLVM_TARGET_TRIPLE_ENV)
  if (const char *EnvTriple = std::getenv(LLVM_TARGET_TRIPLE_ENV))
    Triple = EnvTriple;
#endif

  return Triple;
}
