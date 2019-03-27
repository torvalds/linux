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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
/* $Id: vm86_test.c,v 1.10 2018/05/12 11:35:58 kostik Exp kostik $ */

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/ucontext.h>

#include <machine/cpufunc.h>
#include <machine/psl.h>
#include <machine/sysarch.h>
#include <machine/vm86.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u_int gs;

static void
sig_handler(int signo, siginfo_t *si __unused, void *ucp)
{
	ucontext_t *uc;
	mcontext_t *mc;

	uc = ucp;
	mc = &uc->uc_mcontext;

	/*
	 * Reload pointer to the TLS base, so that malloc inside
	 * printf() works.
	 */
	load_gs(gs);

	printf("sig %d %%eax %#x %%ecx %#x %%eip %#x\n", signo,
	    mc->mc_eax, mc->mc_ecx, mc->mc_eip);
	exit(0);
}

extern char vm86_code_start[], vm86_code_end[];

int
main(void)
{
	ucontext_t uc;
	struct sigaction sa;
	struct vm86_init_args va;
	stack_t ssa;
	char *vm86_code;

	gs = rgs();

	memset(&ssa, 0, sizeof(ssa));
	ssa.ss_size = PAGE_SIZE * 128;
	ssa.ss_sp = mmap(NULL, ssa.ss_size, PROT_READ | PROT_WRITE |
	    PROT_EXEC, MAP_ANON, -1, 0);
	if (ssa.ss_sp == MAP_FAILED)
		err(1, "mmap sigstack");
	if (sigaltstack(&ssa, NULL) == -1)
		err(1, "sigaltstack");

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sig_handler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	if (sigaction(SIGBUS, &sa, NULL) == -1)
		err(1, "sigaction SIGBUS");
	if (sigaction(SIGSEGV, &sa, NULL) == -1)
		err(1, "sigaction SIGSEGV");
	if (sigaction(SIGILL, &sa, NULL) == -1)
		err(1, "sigaction SIGILL");

	vm86_code = mmap((void *)0x10000, PAGE_SIZE, PROT_READ | PROT_WRITE |
	    PROT_EXEC, MAP_ANON | MAP_FIXED, -1, 0);
	if (vm86_code == MAP_FAILED)
		err(1, "mmap");
	memcpy(vm86_code, vm86_code_start, vm86_code_end - vm86_code_start);

	memset(&va, 0, sizeof(va));
	if (i386_vm86(VM86_INIT, &va) == -1)
		err(1, "VM86_INIT");

	memset(&uc, 0, sizeof(uc));
	uc.uc_mcontext.mc_ecx = 0x2345;
	uc.uc_mcontext.mc_eflags = PSL_VM | PSL_USER;
	uc.uc_mcontext.mc_cs = uc.uc_mcontext.mc_ds = uc.uc_mcontext.mc_es =
	    uc.uc_mcontext.mc_ss = (uintptr_t)vm86_code >> 4;
	uc.uc_mcontext.mc_esp = 0xfffe;
	sigreturn(&uc);
}
