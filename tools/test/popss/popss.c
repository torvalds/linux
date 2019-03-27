/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: popss.c,v 1.28 2018/05/09 21:35:29 kostik Exp kostik $
 * $FreeBSD$
 *
 * cc -m32 -Wall -Wextra -O2 -g -o popss popss.c
 * Use as "popss <instruction>", where instruction is one of
 * bound, into, int1, int3, int80, syscall, sysenter.
 */

#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <machine/reg.h>

static u_long *stk;

#define	ITERATIONS	4

static void
setup(pid_t child)
{
	struct reg r;
	struct dbreg dbr;
	int error, i, status;

	error = waitpid(child, &status, WTRAPPED | WEXITED);
	if (error == -1)
		err(1, "waitpid 1");
	error = ptrace(PT_GETREGS, child, (caddr_t)&r, 0);
	if (error == -1)
		err(1, "ptrace PT_GETREGS");
	printf("child %d stopped eip %#x esp %#x\n", child, r.r_eip, r.r_esp);

	error = ptrace(PT_GETDBREGS, child, (caddr_t)&dbr, 0);
	if (error != 0)
		err(1, "ptrace PT_GETDBREGS");
	dbr.dr[7] &= ~DBREG_DR7_MASK(0);
	dbr.dr[7] |= DBREG_DR7_SET(0, DBREG_DR7_LEN_4, DBREG_DR7_RDWR,
	    DBREG_DR7_LOCAL_ENABLE | DBREG_DR7_GLOBAL_ENABLE);
	dbr.dr[0] = (uintptr_t)stk;
	error = ptrace(PT_SETDBREGS, child, (caddr_t)&dbr, 0);
	if (error != 0)
		err(1, "ptrace PT_SETDBREGS");
	error = ptrace(PT_CONTINUE, child, (caddr_t)1, 0);
	if (error != 0)
		err(1, "ptrace PT_CONTINUE fire");

	for (i = 0; i < ITERATIONS; i++) {
		error = waitpid(child, &status, WTRAPPED | WEXITED);
		if (error == -1)
			err(1, "waitpid 2");
		if (WIFEXITED(status))
			break;
		error = ptrace(PT_GETREGS, child, (caddr_t)&r, 0);
		if (error == -1)
			err(1, "ptrace PT_GETREGS");
		error = ptrace(PT_GETDBREGS, child, (caddr_t)&dbr, 0);
		if (error != 0)
			err(1, "ptrace PT_GETDBREGS");
		printf("child %d stopped eip %#x esp %#x dr0 %#x "
		    "dr6 %#x dr7 %#x\n", child, r.r_eip, r.r_esp,
		    dbr.dr[0], dbr.dr[6], dbr.dr[7]);
		error = ptrace(PT_CONTINUE, child, (caddr_t)1, 0);
		if (error == -1)
			err(1, "ptrace PT_CONTINUE tail");
	}
	if (i == ITERATIONS) {
		kill(child, SIGKILL);
		ptrace(PT_DETACH, child, NULL, 0);
	}
}

static u_long tmpstk[1024 * 128];

static u_int
read_ss(void)
{
	u_int res;

	__asm volatile("movl\t%%ss,%0" : "=r" (res));
	return (res);
}

#define	PROLOGUE	"int3;movl\t%0,%%esp;popl\t%%ss;"

static void
act(const char *cmd)
{
	int error;
	static const int boundx[2] = {0, 1};

	printf("child pid %d, stk at %p\n", getpid(), stk);
	*stk = read_ss();

	error = ptrace(PT_TRACE_ME, 0, NULL, 0);
	if (error != 0)
		err(1, "ptrace PT_TRACE_ME");

	if (strcmp(cmd, "bound") == 0) {
		/* XXX BOUND args order clang ias bug */
		__asm volatile("int3;movl\t$11,%%eax;"
		    "movl\t%0,%%esp;popl\t%%ss;bound\t%1,%%eax"
		    : : "r" (stk), "m" (boundx) : "memory");
	} else if (strcmp(cmd, "int1") == 0) {
		__asm volatile(PROLOGUE ".byte 0xf1"
		    : : "r" (stk) : "memory");
	} else if (strcmp(cmd, "int3") == 0) {
		__asm volatile(PROLOGUE "int3"
		    : : "r" (stk) : "memory");
	} else if (strcmp(cmd, "into") == 0) {
		__asm volatile("int3;movl\t$0x80000000,%%eax;"
		    "addl\t%%eax,%%eax;movl\t%0,%%esp;popl\t%%ss;into"
		    : : "r" (stk) : "memory");
	} else if (strcmp(cmd, "int80") == 0) {
		__asm volatile(PROLOGUE "int\t$0x80"
		    : : "r" (stk) : "memory");
	} else if (strcmp(cmd, "syscall") == 0) {
		__asm volatile(PROLOGUE "syscall"
		    : : "r" (stk) : "memory");
	} else if (strcmp(cmd, "sysenter") == 0) {
		__asm volatile(PROLOGUE "sysenter"
		    : : "r" (stk) : "memory");
	} else {
		fprintf(stderr, "unknown instruction\n");
		exit(1);
	}
	printf("ho\n");
}

int
main(int argc, char *argv[])
{
	int child;

	if (argc != 2) {
		printf(
	    "Usage: popss [bound|int1|int3|into|int80|syscall|sysenter]\n");
		exit(1);
	}
	stk = &tmpstk[nitems(tmpstk) - 1];
	child = fork();
	if (child == -1)
		err(1, "fork");
	if (child == 0)
		act(argv[1]);
	else
		setup(child);
}
