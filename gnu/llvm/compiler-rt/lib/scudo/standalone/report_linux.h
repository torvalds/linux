//===-- report_linux.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_REPORT_LINUX_H_
#define SCUDO_REPORT_LINUX_H_

#include "platform.h"

#if SCUDO_LINUX || SCUDO_TRUSTY

#include "internal_defs.h"

namespace scudo {

// Report a fatal error when a map call fails. SizeIfOOM shall
// hold the requested size on an out-of-memory error, 0 otherwise.
void NORETURN reportMapError(uptr SizeIfOOM = 0);

// Report a fatal error when an unmap call fails.
void NORETURN reportUnmapError(uptr Addr, uptr Size);

// Report a fatal error when a mprotect call fails.
void NORETURN reportProtectError(uptr Addr, uptr Size, int Prot);

} // namespace scudo

#endif // SCUDO_LINUX || SCUDO_TRUSTY

#endif // SCUDO_REPORT_LINUX_H_
