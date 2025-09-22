/*===- InstrProfiling.c - Support library for PGO instrumentation ---------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

// Note: This is linked into the Darwin kernel, and must remain compatible
// with freestanding compilation. See `darwin_add_builtin_libraries`.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"

#define INSTR_PROF_VALUE_PROF_DATA
#include "profile/InstrProfData.inc"

static uint32_t __llvm_profile_global_timestamp = 1;

COMPILER_RT_VISIBILITY
void INSTR_PROF_PROFILE_SET_TIMESTAMP(uint64_t *Probe) {
  if (*Probe == 0 || *Probe == (uint64_t)-1)
    *Probe = __llvm_profile_global_timestamp++;
}

COMPILER_RT_VISIBILITY uint64_t __llvm_profile_get_magic(void) {
  return sizeof(void *) == sizeof(uint64_t) ? (INSTR_PROF_RAW_MAGIC_64)
                                            : (INSTR_PROF_RAW_MAGIC_32);
}

COMPILER_RT_VISIBILITY void __llvm_profile_set_dumped(void) {
  lprofSetProfileDumped(1);
}

/* Return the number of bytes needed to add to SizeInBytes to make it
 *   the result a multiple of 8.
 */
COMPILER_RT_VISIBILITY uint8_t
__llvm_profile_get_num_padding_bytes(uint64_t SizeInBytes) {
  return 7 & (sizeof(uint64_t) - SizeInBytes % sizeof(uint64_t));
}

COMPILER_RT_VISIBILITY uint64_t __llvm_profile_get_version(void) {
  return INSTR_PROF_RAW_VERSION_VAR;
}

COMPILER_RT_VISIBILITY void __llvm_profile_reset_counters(void) {
  if (__llvm_profile_get_version() & VARIANT_MASK_TEMPORAL_PROF)
    __llvm_profile_global_timestamp = 1;

  char *I = __llvm_profile_begin_counters();
  char *E = __llvm_profile_end_counters();

  char ResetValue =
      (__llvm_profile_get_version() & VARIANT_MASK_BYTE_COVERAGE) ? 0xFF : 0;
  memset(I, ResetValue, E - I);

  I = __llvm_profile_begin_bitmap();
  E = __llvm_profile_end_bitmap();
  memset(I, 0x0, E - I);

  const __llvm_profile_data *DataBegin = __llvm_profile_begin_data();
  const __llvm_profile_data *DataEnd = __llvm_profile_end_data();
  const __llvm_profile_data *DI;
  for (DI = DataBegin; DI < DataEnd; ++DI) {
    uint64_t CurrentVSiteCount = 0;
    uint32_t VKI, i;
    if (!DI->Values)
      continue;

    ValueProfNode **ValueCounters = (ValueProfNode **)DI->Values;

    for (VKI = IPVK_First; VKI <= IPVK_Last; ++VKI)
      CurrentVSiteCount += DI->NumValueSites[VKI];

    for (i = 0; i < CurrentVSiteCount; ++i) {
      ValueProfNode *CurrVNode = ValueCounters[i];

      while (CurrVNode) {
        CurrVNode->Count = 0;
        CurrVNode = CurrVNode->Next;
      }
    }
  }
  lprofSetProfileDumped(0);
}
