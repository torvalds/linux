//===-- save_restore_regs.S - Implement save/restore* ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// When compiling C++ functions that need to handle thrown exceptions the
// compiler is required to save all registers and call __Unwind_SjLj_Register
// in the function prolog.  But when compiling for thumb1, there are
// no instructions to access the floating point registers, so the
// compiler needs to add a call to the helper function _save_vfp_d8_d15_regs
// written in ARM to save the float registers.  In the epilog, the compiler
// must also add a call to __restore_vfp_d8_d15_regs to restore those registers.
//

	.text
	.syntax unified

//
// Restore registers d8-d15 from stack
//
	.p2align 2
DEFINE_COMPILERRT_PRIVATE_FUNCTION(__restore_vfp_d8_d15_regs)
	vldmia	sp!, {d8-d15}           // pop registers d8-d15 off stack
	bx      lr                      // return to prolog
END_COMPILERRT_FUNCTION(__restore_vfp_d8_d15_regs)

NO_EXEC_STACK_DIRECTIVE

