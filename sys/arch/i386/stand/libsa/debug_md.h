/*	$OpenBSD: debug_md.h,v 1.8 2022/12/08 01:25:45 guenther Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#define	REG_NAMES	\
	"eax", "ecx", "edx", "ebx", "esp", \
	"ebp", "esi", "edi", "eip", "eflags", \
	"cs",  "ss",  "ds",  "es",  "fs", \
	"gs"
#define REG_VALUES(r)	\
	&(r).r_eax, &(r).r_ecx, &(r).r_edx, &(r).r_ebx, &(r).r_esp, \
	&(r).r_ebp, &(r).r_esi, &(r).r_edi, &(r).r_eip, &(r).r_eflags, \
	&(r).r_cs , &(r).r_ss,  &(r).r_ds,  &(r).r_es,  &(r).r_fs, \
	&(r).r_gs
#define TRAP_NAMES	\
	"invalid opcode fault", "breakpoint trap", "arithmetic trap", \
	"asynchronous system trap", "protection fault", "trace trap", \
	"page fault", "alignment fault", "integer divide fault", \
	"non-maskable interrupt", "overflow trap", "bounds check fault", \
	"device not available fault", "double fault", \
	"fp coprocessor operand fetch fault (![P]Pro)", "invalid tss fault", \
	"segment not present fault", "stack fault", "machine check ([P]Pro)", \
	"reserved fault base"

#ifdef	_LOCORE
	.globl	reg
#define DUMP_REGS	int $2
#else
#define DUMP_REGS	__asm("int $2")
extern struct reg reg;
#endif
