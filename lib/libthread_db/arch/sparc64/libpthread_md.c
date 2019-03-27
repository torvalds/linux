/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
 * Copyright (c) 2011 Marius Strobl <marius@FreeBSD.org>
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

#include <sys/types.h>
#include <string.h>
#include <thread_db.h>
#include <ucontext.h>
#include <machine/fsr.h>

#include "libpthread_db.h"

void
pt_reg_to_ucontext(const struct reg *r, ucontext_t *uc)
{

	memcpy(&uc->uc_mcontext, r, MIN(sizeof(uc->uc_mcontext), sizeof(*r)));
}

void
pt_ucontext_to_reg(const ucontext_t *uc, struct reg *r)
{

	memcpy(r, &uc->uc_mcontext, MIN(sizeof(uc->uc_mcontext), sizeof(*r)));
}

void
pt_fpreg_to_ucontext(const struct fpreg* r, ucontext_t *uc)
{
	mcontext_t *mc = &uc->uc_mcontext;

	memcpy(mc->mc_fp, r->fr_regs, MIN(sizeof(mc->mc_fp),
	    sizeof(r->fr_regs)));
	mc->_mc_fsr = r->fr_fsr;
	mc->_mc_gsr = r->fr_gsr;
	mc->_mc_fprs |= FPRS_FEF;
}

void
pt_ucontext_to_fpreg(const ucontext_t *uc, struct fpreg *r)
{
	const mcontext_t *mc = &uc->uc_mcontext;

	if ((mc->_mc_fprs & FPRS_FEF) != 0) {
		memcpy(r->fr_regs, mc->mc_fp, MIN(sizeof(mc->mc_fp),
		    sizeof(r->fr_regs)));
		r->fr_fsr = mc->_mc_fsr;
		r->fr_gsr = mc->_mc_gsr;
	} else
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
