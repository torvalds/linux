/*===- InstrProfilingVersionVar.c - profile version variable setup  -------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#include "InstrProfiling.h"

/* uint64 __llvm_profile_raw_version
 *
 * The runtime should only provide its own definition of this symbol when the
 * user has not specified one. Set this up by moving the runtime's copy of this
 * symbol to an object file within the archive.
 */
COMPILER_RT_VISIBILITY COMPILER_RT_WEAK uint64_t INSTR_PROF_RAW_VERSION_VAR =
    INSTR_PROF_RAW_VERSION;
