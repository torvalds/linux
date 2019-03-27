/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
	mcontext_t *mc;

	mc = &uc->uc_mcontext;

	memcpy(mc->mc_gpregs.gp_t, r->t, sizeof(mc->mc_gpregs.gp_t));
	memcpy(mc->mc_gpregs.gp_s, r->s, sizeof(mc->mc_gpregs.gp_s));
	memcpy(mc->mc_gpregs.gp_a, r->a, sizeof(mc->mc_gpregs.gp_a));
	mc->mc_gpregs.gp_ra = r->ra;
	mc->mc_gpregs.gp_sp = r->sp;
	mc->mc_gpregs.gp_gp = r->gp;
	mc->mc_gpregs.gp_tp = r->tp;
	mc->mc_gpregs.gp_sepc = r->sepc;
	mc->mc_gpregs.gp_sstatus = r->sstatus;
}

void
pt_ucontext_to_reg(const ucontext_t *uc, struct reg *r)
{
	const mcontext_t *mc;

	mc = &uc->uc_mcontext;

	memcpy(r->t, mc->mc_gpregs.gp_t, sizeof(mc->mc_gpregs.gp_t));
	memcpy(r->s, mc->mc_gpregs.gp_s, sizeof(mc->mc_gpregs.gp_s));
	memcpy(r->a, mc->mc_gpregs.gp_a, sizeof(mc->mc_gpregs.gp_a));
	r->ra = mc->mc_gpregs.gp_ra;
	r->sp = mc->mc_gpregs.gp_sp;
	r->gp = mc->mc_gpregs.gp_gp;
	r->tp = mc->mc_gpregs.gp_tp;
	r->sepc = mc->mc_gpregs.gp_sepc;
	r->sstatus = mc->mc_gpregs.gp_sstatus;
}

void
pt_fpreg_to_ucontext(const struct fpreg *r __unused, ucontext_t *uc __unused)
{
	mcontext_t *mc = &uc->uc_mcontext;

	memcpy(&mc->mc_fpregs, r, sizeof(*r));
	mc->mc_flags |= _MC_FP_VALID;
}

void
pt_ucontext_to_fpreg(const ucontext_t *uc __unused, struct fpreg *r __unused)
{
	const mcontext_t *mc = &uc->uc_mcontext;

	if (mc->mc_flags & _MC_FP_VALID)
		memcpy(r, &mc->mc_fpregs, sizeof(*r));
	else
		memset(r, 0, sizeof(*r));
}

void
pt_md_init(void)
{
}

int
pt_reg_sstep(struct reg *reg __unused, int step __unused)
{

	return (0);
}
