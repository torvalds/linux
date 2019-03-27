/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/procfs.h>
#include <string.h>
#include <thread_db.h>
#include <ucontext.h>

#include "libpthread_db.h"

void
pt_reg_to_ucontext(const struct reg *r, ucontext_t *uc)
{
	mcontext_t *mc = &uc->uc_mcontext;

	mc->mc_rdi = r->r_rdi;
	mc->mc_rsi = r->r_rsi;
	mc->mc_rdx = r->r_rdx;
	mc->mc_rcx = r->r_rcx;
	mc->mc_r8 = r->r_r8;
	mc->mc_r9 = r->r_r9;
	mc->mc_rax = r->r_rax;
	mc->mc_rbx = r->r_rbx;
	mc->mc_rbp = r->r_rbp;
	mc->mc_r10 = r->r_r10;
	mc->mc_r11 = r->r_r11;
	mc->mc_r12 = r->r_r12;
	mc->mc_r13 = r->r_r13;
	mc->mc_r14 = r->r_r14;
	mc->mc_r15 = r->r_r15;
	mc->mc_rip = r->r_rip;
	mc->mc_cs = r->r_cs;
	mc->mc_rflags = r->r_rflags;
	mc->mc_rsp = r->r_rsp;
	mc->mc_ss = r->r_ss;
}

void
pt_ucontext_to_reg(const ucontext_t *uc, struct reg *r)
{
	const mcontext_t *mc = &uc->uc_mcontext;

	r->r_rdi = mc->mc_rdi;
	r->r_rsi = mc->mc_rsi;
	r->r_rdx = mc->mc_rdx;
	r->r_rcx = mc->mc_rcx;
	r->r_r8 = mc->mc_r8;
	r->r_r9 = mc->mc_r9;
	r->r_rax = mc->mc_rax;
	r->r_rbx = mc->mc_rbx;
	r->r_rbp = mc->mc_rbp;
	r->r_r10 = mc->mc_r10;
	r->r_r11 = mc->mc_r11;
	r->r_r12 = mc->mc_r12;
	r->r_r13 = mc->mc_r13;
	r->r_r14 = mc->mc_r14;
	r->r_r15 = mc->mc_r15;
	r->r_rip = mc->mc_rip;
	r->r_cs = mc->mc_cs;
	r->r_rflags = mc->mc_rflags;
	r->r_rsp = mc->mc_rsp;
	r->r_ss = mc->mc_ss;
}

void
pt_fpreg_to_ucontext(const struct fpreg* r, ucontext_t *uc)
{

	memcpy(&uc->uc_mcontext.mc_fpstate, r, sizeof(*r));
}

void
pt_ucontext_to_fpreg(const ucontext_t *uc, struct fpreg *r)
{

	memcpy(r, &uc->uc_mcontext.mc_fpstate, sizeof(*r));
}

void
pt_md_init(void)
{

	/* Nothing to do */
}

int
pt_reg_sstep(struct reg *reg, int step)
{
	register_t old;

	old = reg->r_rflags;
	if (step)
		reg->r_rflags |= 0x0100;
	else
		reg->r_rflags &= ~0x0100;
	return (old != reg->r_rflags); /* changed ? */
}
