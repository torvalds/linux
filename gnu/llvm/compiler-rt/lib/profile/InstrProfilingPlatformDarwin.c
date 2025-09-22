/*===- InstrProfilingPlatformDarwin.c - Profile data on Darwin ------------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

// Note: This is linked into the Darwin kernel, and must remain compatible
// with freestanding compilation. See `darwin_add_builtin_libraries`.

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"

#if defined(__APPLE__)
/* Use linker magic to find the bounds of the Data section. */
COMPILER_RT_VISIBILITY
extern __llvm_profile_data
    DataStart __asm("section$start$__DATA$" INSTR_PROF_DATA_SECT_NAME);
COMPILER_RT_VISIBILITY
extern __llvm_profile_data
    DataEnd __asm("section$end$__DATA$" INSTR_PROF_DATA_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char
    NamesStart __asm("section$start$__DATA$" INSTR_PROF_NAME_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char NamesEnd __asm("section$end$__DATA$" INSTR_PROF_NAME_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char
    CountersStart __asm("section$start$__DATA$" INSTR_PROF_CNTS_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char CountersEnd __asm("section$end$__DATA$" INSTR_PROF_CNTS_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char
    BitmapStart __asm("section$start$__DATA$" INSTR_PROF_BITS_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char BitmapEnd __asm("section$end$__DATA$" INSTR_PROF_BITS_SECT_NAME);
COMPILER_RT_VISIBILITY
extern VTableProfData
    VTableProfStart __asm("section$start$__DATA$" INSTR_PROF_VTAB_SECT_NAME);
COMPILER_RT_VISIBILITY
extern VTableProfData
    VTableProfEnd __asm("section$end$__DATA$" INSTR_PROF_VTAB_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char
    VNameStart __asm("section$start$__DATA$" INSTR_PROF_VNAME_SECT_NAME);
COMPILER_RT_VISIBILITY
extern char VNameEnd __asm("section$end$__DATA$" INSTR_PROF_VNAME_SECT_NAME);
COMPILER_RT_VISIBILITY
extern uint32_t
    OrderFileStart __asm("section$start$__DATA$" INSTR_PROF_ORDERFILE_SECT_NAME);

COMPILER_RT_VISIBILITY
extern ValueProfNode
    VNodesStart __asm("section$start$__DATA$" INSTR_PROF_VNODES_SECT_NAME);
COMPILER_RT_VISIBILITY
extern ValueProfNode
    VNodesEnd __asm("section$end$__DATA$" INSTR_PROF_VNODES_SECT_NAME);

COMPILER_RT_VISIBILITY
const __llvm_profile_data *__llvm_profile_begin_data(void) {
  return &DataStart;
}
COMPILER_RT_VISIBILITY
const __llvm_profile_data *__llvm_profile_end_data(void) { return &DataEnd; }
COMPILER_RT_VISIBILITY
const char *__llvm_profile_begin_names(void) { return &NamesStart; }
COMPILER_RT_VISIBILITY
const char *__llvm_profile_end_names(void) { return &NamesEnd; }
COMPILER_RT_VISIBILITY
char *__llvm_profile_begin_counters(void) { return &CountersStart; }
COMPILER_RT_VISIBILITY
char *__llvm_profile_end_counters(void) { return &CountersEnd; }
COMPILER_RT_VISIBILITY
char *__llvm_profile_begin_bitmap(void) { return &BitmapStart; }
COMPILER_RT_VISIBILITY
char *__llvm_profile_end_bitmap(void) { return &BitmapEnd; }
COMPILER_RT_VISIBILITY
const VTableProfData *__llvm_profile_begin_vtables(void) {
  return &VTableProfStart;
}
COMPILER_RT_VISIBILITY
const VTableProfData *__llvm_profile_end_vtables(void) {
  return &VTableProfEnd;
}
COMPILER_RT_VISIBILITY
const char *__llvm_profile_begin_vtabnames(void) { return &VNameStart; }
COMPILER_RT_VISIBILITY
const char *__llvm_profile_end_vtabnames(void) { return &VNameEnd; }
COMPILER_RT_VISIBILITY
uint32_t *__llvm_profile_begin_orderfile(void) { return &OrderFileStart; }

COMPILER_RT_VISIBILITY
ValueProfNode *__llvm_profile_begin_vnodes(void) {
  return &VNodesStart;
}
COMPILER_RT_VISIBILITY
ValueProfNode *__llvm_profile_end_vnodes(void) { return &VNodesEnd; }

COMPILER_RT_VISIBILITY ValueProfNode *CurrentVNode = &VNodesStart;
COMPILER_RT_VISIBILITY ValueProfNode *EndVNode = &VNodesEnd;

COMPILER_RT_VISIBILITY int __llvm_write_binary_ids(ProfDataWriter *Writer) {
  return 0;
}

#endif
