/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006-2007, Juniper Networks, Inc.
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
 * 3. Neither the name of Juniper Networks nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JUNIPER NETWORKS ``AS IS'' AND ANY EXPRESS OR
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

#include <sys/types.h>
#include <sys/procfs.h>
#include <ucontext.h>
#include <string.h>
#include <thread_db.h>
#include "libpthread_db.h"

void
pt_reg_to_ucontext(const struct reg *r, ucontext_t *uc)
{

	memcpy(uc->uc_mcontext.mc_regs, &r->r_regs[ZERO],
	    sizeof(uc->uc_mcontext.mc_regs));
	uc->uc_mcontext.mc_pc = r->r_regs[PC];
	uc->uc_mcontext.mullo = r->r_regs[MULLO];
	uc->uc_mcontext.mulhi = r->r_regs[MULHI];
}

void
pt_ucontext_to_reg(const ucontext_t *uc, struct reg *r)
{
	memcpy(&r->r_regs[ZERO], uc->uc_mcontext.mc_regs,
	    sizeof(uc->uc_mcontext.mc_regs));
	r->r_regs[PC] = uc->uc_mcontext.mc_pc;
	r->r_regs[MULLO] = uc->uc_mcontext.mullo;
	r->r_regs[MULHI] = uc->uc_mcontext.mulhi;
}

void
pt_fpreg_to_ucontext(const struct fpreg* r, ucontext_t *uc)
{

	memcpy(uc->uc_mcontext.mc_fpregs, r->r_regs,
	    sizeof(uc->uc_mcontext.mc_fpregs));
}

void
pt_ucontext_to_fpreg(const ucontext_t *uc, struct fpreg *r)
{

	memcpy(r->r_regs, uc->uc_mcontext.mc_fpregs,
	    sizeof(uc->uc_mcontext.mc_fpregs));
}

void
pt_md_init(void)
{
	/* Nothing to do */
}

int
pt_reg_sstep(struct reg *reg __unused, int step __unused)
{
	/*
	 * XXX: mips doesnt store single step info in any registers
	 */
	return (0);
}
