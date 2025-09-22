//===------------ exit.S - global terminator for AVR ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

	.text
	.align 2

	.globl _exit
	.type  _exit, @function

_exit:
	cli                 ; Disable all interrupts.
__stop_program:
	rjmp __stop_program ; Fall into an infinite loop.
