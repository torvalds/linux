/*===- InstrProfilingPlatformWindows.c - Profile data on Windows ----------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#include <stddef.h>

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"

#if defined(_WIN32)

#if defined(_MSC_VER)
/* Merge read-write sections into .data. */
#pragma comment(linker, "/MERGE:.lprfb=.data")
#pragma comment(linker, "/MERGE:.lprfd=.data")
#pragma comment(linker, "/MERGE:.lprfv=.data")
#pragma comment(linker, "/MERGE:.lprfnd=.data")
/* Do *NOT* merge .lprfn and .lcovmap into .rdata. llvm-cov must be able to find
 * after the fact.
 * Do *NOT* merge .lprfc .rdata. When binary profile correlation is enabled,
 * llvm-cov must be able to find after the fact.
 */

/* Allocate read-only section bounds. */
#pragma section(".lprfn$A", read)
#pragma section(".lprfn$Z", read)

/* Allocate read-write section bounds. */
#pragma section(".lprfd$A", read, write)
#pragma section(".lprfd$Z", read, write)
#pragma section(".lprfc$A", read, write)
#pragma section(".lprfc$Z", read, write)
#pragma section(".lprfb$A", read, write)
#pragma section(".lprfb$Z", read, write)
#pragma section(".lorderfile$A", read, write)
#pragma section(".lprfnd$A", read, write)
#pragma section(".lprfnd$Z", read, write)
#endif

__llvm_profile_data COMPILER_RT_SECTION(".lprfd$A") DataStart = {0};
__llvm_profile_data COMPILER_RT_SECTION(".lprfd$Z") DataEnd = {0};

const char COMPILER_RT_SECTION(".lprfn$A") NamesStart = '\0';
const char COMPILER_RT_SECTION(".lprfn$Z") NamesEnd = '\0';

char COMPILER_RT_SECTION(".lprfc$A") CountersStart;
char COMPILER_RT_SECTION(".lprfc$Z") CountersEnd;
char COMPILER_RT_SECTION(".lprfb$A") BitmapStart;
char COMPILER_RT_SECTION(".lprfb$Z") BitmapEnd;
uint32_t COMPILER_RT_SECTION(".lorderfile$A") OrderFileStart;

ValueProfNode COMPILER_RT_SECTION(".lprfnd$A") VNodesStart;
ValueProfNode COMPILER_RT_SECTION(".lprfnd$Z") VNodesEnd;

const __llvm_profile_data *__llvm_profile_begin_data(void) {
  return &DataStart + 1;
}
const __llvm_profile_data *__llvm_profile_end_data(void) { return &DataEnd; }

// Type profiling isn't implemented under MSVC ABI, so return NULL (rather than
// implementing linker magic on Windows) to make it more explicit. To elaborate,
// the current type profiling implementation maps a profiled vtable address to a
// vtable variable through vtables mangled name. Under MSVC ABI, the variable
// name for vtables might not be the mangled name (see
// MicrosoftCXXABI::getAddrOfVTable in MicrosoftCXXABI.cpp for more details on
// how a vtable name is computed). Note the mangled name is still in the vtable
// IR (just not variable name) for mapping purpose, but more implementation work
// is required.
const VTableProfData *__llvm_profile_begin_vtables(void) { return NULL; }
const VTableProfData *__llvm_profile_end_vtables(void) { return NULL; }

const char *__llvm_profile_begin_names(void) { return &NamesStart + 1; }
const char *__llvm_profile_end_names(void) { return &NamesEnd; }

// Type profiling isn't supported on Windows, so return NULl to make it more
// explicit.
const char *__llvm_profile_begin_vtabnames(void) { return NULL; }
const char *__llvm_profile_end_vtabnames(void) { return NULL; }

char *__llvm_profile_begin_counters(void) { return &CountersStart + 1; }
char *__llvm_profile_end_counters(void) { return &CountersEnd; }
char *__llvm_profile_begin_bitmap(void) { return &BitmapStart + 1; }
char *__llvm_profile_end_bitmap(void) { return &BitmapEnd; }
uint32_t *__llvm_profile_begin_orderfile(void) { return &OrderFileStart; }

ValueProfNode *__llvm_profile_begin_vnodes(void) { return &VNodesStart + 1; }
ValueProfNode *__llvm_profile_end_vnodes(void) { return &VNodesEnd; }

ValueProfNode *CurrentVNode = &VNodesStart + 1;
ValueProfNode *EndVNode = &VNodesEnd;

/* lld-link provides __buildid symbol which points to the 16 bytes build id when
 * using /build-id flag. https://lld.llvm.org/windows_support.html#lld-flags */
#define BUILD_ID_LEN 16
COMPILER_RT_WEAK uint8_t __buildid[BUILD_ID_LEN] = {0};
COMPILER_RT_VISIBILITY int __llvm_write_binary_ids(ProfDataWriter *Writer) {
  static const uint8_t zeros[BUILD_ID_LEN] = {0};
  if (memcmp(__buildid, zeros, BUILD_ID_LEN) != 0) {
    if (Writer &&
        lprofWriteOneBinaryId(Writer, BUILD_ID_LEN, __buildid, 0) == -1)
      return -1;
    return sizeof(uint64_t) + BUILD_ID_LEN;
  }
  return 0;
}

#endif
