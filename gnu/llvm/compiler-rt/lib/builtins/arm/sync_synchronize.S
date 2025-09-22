//===-- sync_synchronize - Implement memory barrier * ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

// When compiling a use of the gcc built-in __sync_synchronize() in thumb1 mode
// the compiler may emit a call to __sync_synchronize.
// On Darwin the implementation jumps to an OS supplied function named
// OSMemoryBarrier

	.text
	.syntax unified

#if __APPLE__

	.p2align 2
DEFINE_COMPILERRT_PRIVATE_FUNCTION(__sync_synchronize)
	stmfd	sp!, {r7, lr}
	add		r7, sp, #0
	bl		_OSMemoryBarrier
	ldmfd	sp!, {r7, pc}
END_COMPILERRT_FUNCTION(__sync_synchronize)

	// tell linker it can break up file at label boundaries
	.subsections_via_symbols

#endif

NO_EXEC_STACK_DIRECTIVE

