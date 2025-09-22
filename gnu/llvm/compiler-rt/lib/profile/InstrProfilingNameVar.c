/*===- InstrProfilingNameVar.c - profile name variable setup  -------------===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

#include "InstrProfiling.h"

/* char __llvm_profile_filename[1]
 *
 * The runtime should only provide its own definition of this symbol when the
 * user has not specified one. Set this up by moving the runtime's copy of this
 * symbol to an object file within the archive.
 */
COMPILER_RT_WEAK COMPILER_RT_VISIBILITY char INSTR_PROF_PROFILE_NAME_VAR[1] = {0};
