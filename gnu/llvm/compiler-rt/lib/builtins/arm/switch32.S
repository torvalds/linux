//===-- switch.S - Implement switch* --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

//
// When compiling switch statements in thumb mode, the compiler
// can use these __switch* helper functions  The compiler emits a blx to
// the __switch* function followed by a table of displacements for each
// case statement.  On entry, R0 is the index into the table. The __switch*
// function uses the return address in lr to find the start of the table.
// The first entry in the table is the count of the entries in the table.
// It then uses R0 to index into the table and get the displacement of the
// address to jump to.  If R0 is greater than the size of the table, it jumps
// to the last entry in the table. Each displacement in the table is actually
// the distance from lr to the label, thus making the tables PIC.


	.text
	.syntax unified

//
// The table contains signed 4-byte sized elements which are the distance
// from lr to the target label.
//
	.p2align 2
DEFINE_COMPILERRT_PRIVATE_FUNCTION(__switch32)
	ldr     ip, [lr, #-1]            // get first 32-bit word in table
	cmp     r0, ip                   // compare with index
	add     r0, lr, r0, lsl #2       // compute address of element in table
	add     ip, lr, ip, lsl #2       // compute address of last element in table
	ite lo
	ldrlo   r0, [r0, #3]             // load 32-bit element if r0 is in range
	ldrhs   r0, [ip, #3]             // load 32-bit element if r0 out of range
	add     ip, lr, r0               // compute label = lr + element
	bx      ip                       // jump to computed label
END_COMPILERRT_FUNCTION(__switch32)

NO_EXEC_STACK_DIRECTIVE

