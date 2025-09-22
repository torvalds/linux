//===--------------------- Unwind_AIXExtras.cpp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//===----------------------------------------------------------------------===//

// This file is only used for AIX.
#if defined(_AIX)

#include "config.h"
#include "libunwind_ext.h"
#include <sys/debug.h>

namespace libunwind {
// getFuncNameFromTBTable
// Get the function name from its traceback table.
char *getFuncNameFromTBTable(uintptr_t Pc, uint16_t &NameLen,
                             unw_word_t *Offset) {
  uint32_t *p = reinterpret_cast<uint32_t *>(Pc);
  *Offset = 0;

  // Keep looking forward until a word of 0 is found. The traceback
  // table starts at the following word.
  while (*p)
    p++;
  tbtable *TBTable = reinterpret_cast<tbtable *>(p + 1);

  if (!TBTable->tb.name_present)
    return NULL;

  // Get to the name of the function.
  p = reinterpret_cast<uint32_t *>(&TBTable->tb_ext);

  // Skip field parminfo if it exists.
  if (TBTable->tb.fixedparms || TBTable->tb.floatparms)
    p++;

  // If the tb_offset field exists, get the offset from the start of
  // the function to pc. Skip the field.
  if (TBTable->tb.has_tboff) {
    unw_word_t StartIp =
        reinterpret_cast<uintptr_t>(TBTable) - *p - sizeof(uint32_t);
    *Offset = Pc - StartIp;
    p++;
  }

  // Skip field hand_mask if it exists.
  if (TBTable->tb.int_hndl)
    p++;

  // Skip fields ctl_info and ctl_info_disp if they exist.
  if (TBTable->tb.has_ctl) {
    p += 1 + *p;
  }

  NameLen = *(reinterpret_cast<uint16_t *>(p));
  return reinterpret_cast<char *>(p) + sizeof(uint16_t);
}
} // namespace libunwind
#endif // defined(_AIX)
