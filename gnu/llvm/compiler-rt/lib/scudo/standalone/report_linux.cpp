//===-- report_linux.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

#if SCUDO_LINUX || SCUDO_TRUSTY

#include "common.h"
#include "internal_defs.h"
#include "report.h"
#include "report_linux.h"
#include "string_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace scudo {

// Fatal internal map() error (potentially OOM related).
void NORETURN reportMapError(uptr SizeIfOOM) {
  ScopedString Error;
  Error.append("Scudo ERROR: internal map failure (error desc=%s)",
               strerror(errno));
  if (SizeIfOOM)
    Error.append(" requesting %zuKB", SizeIfOOM >> 10);
  Error.append("\n");
  reportRawError(Error.data());
}

void NORETURN reportUnmapError(uptr Addr, uptr Size) {
  ScopedString Error;
  Error.append("Scudo ERROR: internal unmap failure (error desc=%s) Addr 0x%zx "
               "Size %zu\n",
               strerror(errno), Addr, Size);
  reportRawError(Error.data());
}

void NORETURN reportProtectError(uptr Addr, uptr Size, int Prot) {
  ScopedString Error;
  Error.append(
      "Scudo ERROR: internal protect failure (error desc=%s) Addr 0x%zx "
      "Size %zu Prot %x\n",
      strerror(errno), Addr, Size, Prot);
  reportRawError(Error.data());
}

} // namespace scudo

#endif // SCUDO_LINUX || SCUDO_TRUSTY
