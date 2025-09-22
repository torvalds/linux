//===-- llvm/Support/ExitCodes.h - Exit codes for exit()  -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains definitions of exit codes for exit() function. They are
/// either defined by sysexits.h if it is supported, or defined here if
/// sysexits.h is not supported.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_EXITCODES_H
#define LLVM_SUPPORT_EXITCODES_H

#include "llvm/Config/llvm-config.h"

#if HAVE_SYSEXITS_H
#include <sysexits.h>
#elif __MVS__ || defined(_WIN32)
// <sysexits.h> does not exist on z/OS and Windows. The only value used in LLVM
// is EX_IOERR, which is used to signal a special error condition (broken pipe).
// Define the macro with its usual value from BSD systems, which is chosen to
// not clash with more standard exit codes like 1.
#define EX_IOERR 74
#elif LLVM_ON_UNIX
#error Exit code EX_IOERR not available
#endif

#endif
