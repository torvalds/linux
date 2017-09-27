/*
 * check_initial_reg_state.c - check that execve sets the correct state
 * Copyright (c) 2014-2016 Andrew Lutomirski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define _GNU_SOURCE

#include <stdio.h>

unsigned long ax, bx, cx, dx, si, di, bp, sp, flags;
unsigned long r8, r9, r10, r11, r12, r13, r14, r15;

asm (
	".pushsection .text\n\t"
	".type real_start, @function\n\t"
	".global real_start\n\t"
	"real_start:\n\t"
#ifdef __x86_64__
	"mov %rax, ax\n\t"
	"mov %rbx, bx\n\t"
	"mov %rcx, cx\n\t"
	"mov %rdx, dx\n\t"
	"mov %rsi, si\n\t"
	"mov %rdi, di\n\t"
	"mov %rbp, bp\n\t"
	"mov %rsp, sp\n\t"
	"mov %r8, r8\n\t"
	"mov %r9, r9\n\t"
	"mov %r10, r10\n\t"
	"mov %r11, r11\n\t"
	"mov %r12, r12\n\t"
	"mov %r13, r13\n\t"
	"mov %r14, r14\n\t"
	"mov %r15, r15\n\t"
	"pushfq\n\t"
	"popq flags\n\t"
#else
	"mov %eax, ax\n\t"
	"mov %ebx, bx\n\t"
	"mov %ecx, cx\n\t"
	"mov %edx, dx\n\t"
	"mov %esi, si\n\t"
	"mov %edi, di\n\t"
	"mov %ebp, bp\n\t"
	"mov %esp, sp\n\t"
	"pushfl\n\t"
	"popl flags\n\t"
#endif
	"jmp _start\n\t"
	".size real_start, . - real_start\n\t"
	".popsection");

int main()
{
	int nerrs = 0;

	if (sp == 0) {
		printf("[FAIL]\tTest was built incorrectly\n");
		return 1;
	}

	if (ax || bx || cx || dx || si || di || bp
#ifdef __x86_64__
	    || r8 || r9 || r10 || r11 || r12 || r13 || r14 || r15
#endif
		) {
		printf("[FAIL]\tAll GPRs except SP should be 0\n");
#define SHOW(x) printf("\t" #x " = 0x%lx\n", x);
		SHOW(ax);
		SHOW(bx);
		SHOW(cx);
		SHOW(dx);
		SHOW(si);
		SHOW(di);
		SHOW(bp);
		SHOW(sp);
#ifdef __x86_64__
		SHOW(r8);
		SHOW(r9);
		SHOW(r10);
		SHOW(r11);
		SHOW(r12);
		SHOW(r13);
		SHOW(r14);
		SHOW(r15);
#endif
		nerrs++;
	} else {
		printf("[OK]\tAll GPRs except SP are 0\n");
	}

	if (flags != 0x202) {
		printf("[FAIL]\tFLAGS is 0x%lx, but it should be 0x202\n", flags);
		nerrs++;
	} else {
		printf("[OK]\tFLAGS is 0x202\n");
	}

	return nerrs ? 1 : 0;
}
