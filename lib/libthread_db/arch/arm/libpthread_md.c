/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Olivier Houchard
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>
#include <string.h>
#include <thread_db.h>

#include "libpthread_db.h"

void
pt_reg_to_ucontext(const struct reg *r, ucontext_t *uc)
{
	mcontext_t *mc = &uc->uc_mcontext;
	__greg_t *gr = mc->__gregs;

	gr[_REG_R0] = r->r[0];
	gr[_REG_R1] = r->r[1];
	gr[_REG_R2] = r->r[2];
	gr[_REG_R3] = r->r[3];
	gr[_REG_R4] = r->r[4];
	gr[_REG_R5] = r->r[5];
	gr[_REG_R6] = r->r[6];
	gr[_REG_R7] = r->r[7];
	gr[_REG_R8] = r->r[8];
	gr[_REG_R9] = r->r[9];
	gr[_REG_R10] = r->r[10];
	gr[_REG_R11] = r->r[11];
	gr[_REG_R12] = r->r[12];
	gr[_REG_SP] = r->r_sp;
	gr[_REG_LR] = r->r_lr;
	gr[_REG_PC] = r->r_pc;
	gr[_REG_CPSR] = r->r_cpsr;
}

void
pt_ucontext_to_reg(const ucontext_t *uc, struct reg *r)
{
	const mcontext_t *mc = &uc->uc_mcontext;

	const __greg_t *gr = mc->__gregs;

	r->r[0] = gr[_REG_R0];
	r->r[1] = gr[_REG_R1];
	r->r[2] = gr[_REG_R2];
	r->r[3] = gr[_REG_R3];
	r->r[4] = gr[_REG_R4];
	r->r[5] = gr[_REG_R5];
	r->r[6] = gr[_REG_R6];
	r->r[7] = gr[_REG_R7];
	r->r[8] = gr[_REG_R8];
	r->r[9] = gr[_REG_R9];
	r->r[10] = gr[_REG_R10];
	r->r[11] = gr[_REG_R11];
	r->r[12] = gr[_REG_R12];
	r->r_sp = gr[_REG_SP];
	r->r_lr = gr[_REG_LR];
	r->r_pc = gr[_REG_PC];
	r->r_cpsr = gr[_REG_CPSR];
}

void
pt_fpreg_to_ucontext(const struct fpreg *r __unused, ucontext_t *uc)
{
	mcontext_t *mc = &uc->uc_mcontext;

	/* XXX */
	mc->mc_vfp_size = 0;
	mc->mc_vfp_ptr = NULL;
	memset(mc->mc_spare, 0, sizeof(mc->mc_spare));
}

void
pt_ucontext_to_fpreg(const ucontext_t *uc __unused, struct fpreg *r)
{

	/* XXX */
	memset(r, 0, sizeof(*r));
}

void
pt_md_init(void)
{
}

int
pt_reg_sstep(struct reg *reg __unused, int step __unused)
{

	/* XXX */
	return (0);
}
