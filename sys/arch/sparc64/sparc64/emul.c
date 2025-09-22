/*	$OpenBSD: emul.c,v 1.28 2024/03/29 21:14:31 miod Exp $	*/
/*	$NetBSD: emul.c,v 1.8 2001/06/29 23:58:40 eeh Exp $	*/

/*-
 * Copyright (c) 1997, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <machine/reg.h>
#include <machine/instr.h>
#include <machine/cpu.h>
#include <machine/psl.h>
#include <uvm/uvm_extern.h>

#ifdef DEBUG_EMUL
# define DPRINTF(a) printf a
#else
# define DPRINTF(a)
#endif

void swap_quad(int64_t *);

#define	SIGN_EXT13(v)	(((int64_t)(v) << 51) >> 51)

void
swap_quad(int64_t *p)
{
	int64_t t;

	t = htole64(p[0]);
	p[0] = htole64(p[1]);
	p[1] = t;
}

/*
 * emulate STQF, STQFA, LDQF, and LDQFA
 */
int
emul_qf(int32_t insv, struct proc *p, union sigval sv, struct trapframe *tf)
{
	extern const struct fpstate initfpstate;
	struct fpstate *fs = p->p_md.md_fpstate;
	int64_t addr, buf[2];
	union instr ins;
	int freg, isload, err;
	u_int8_t asi;

	ins.i_int = insv;
	freg = ins.i_op3.i_rd & ~1;
	freg |= (ins.i_op3.i_rd & 1) << 5;

	if (ins.i_op3.i_op3 == IOP3_LDQF || ins.i_op3.i_op3 == IOP3_LDQFA)
		isload = 1;
	else
		isload = 0;

	if (ins.i_op3.i_op3 == IOP3_STQF || ins.i_op3.i_op3 == IOP3_LDQF)
		asi = ASI_PRIMARY;
	else if (ins.i_loadstore.i_i)
		asi = (tf->tf_tstate & TSTATE_ASI) >> TSTATE_ASI_SHIFT;
	else
		asi = ins.i_asi.i_asi;

	addr = tf->tf_global[ins.i_asi.i_rs1];
	if (ins.i_loadstore.i_i)
		addr += SIGN_EXT13(ins.i_simm13.i_simm13);
	else
		addr += tf->tf_global[ins.i_asi.i_rs2];

	if (asi < ASI_PRIMARY) {
		/* privileged asi */
		trapsignal(p, SIGILL, 0, ILL_PRVOPC, sv);
		return (0);
	}
	if (asi > ASI_SECONDARY_NOFAULT_LITTLE ||
	    (asi > ASI_SECONDARY_NOFAULT && asi < ASI_PRIMARY_LITTLE)) {
		/* architecturally undefined user ASI's */
		goto segv;
	}

	if ((freg & 3) != 0) {
		/* only valid for %fN where N % 4 = 0 */
		trapsignal(p, SIGILL, 0, ILL_ILLOPN, sv);
		return (0);
	}

	if ((addr & 3) != 0) {
		/* request is not aligned */
		trapsignal(p, SIGBUS, 0, BUS_ADRALN, sv);
		return (0);
	}

	fs = p->p_md.md_fpstate;
	if (fs == NULL) {
		KERNEL_LOCK();
		/* don't currently have an fpu context, get one */
		fs = malloc(sizeof(*fs), M_SUBPROC, M_WAITOK);
		*fs = initfpstate;
		p->p_md.md_fpstate = fs;
		KERNEL_UNLOCK();
	} else
		fpusave_proc(p, 1);

	/* Ok, try to do the actual operation (finally) */
	if (isload) {
		err = copyin((caddr_t)addr, buf, sizeof(buf));
		if (err != 0 && (asi & 2) == 0)
			goto segv;
		if (err == 0) {
			if (asi & 8)
				swap_quad(buf);
			bcopy(buf, &fs->fs_regs[freg], sizeof(buf));
		}
	} else {
		bcopy(&fs->fs_regs[freg], buf, sizeof(buf));
		if (asi & 8)
			swap_quad(buf);
		if (copyout(buf, (caddr_t)addr, sizeof(buf)) && (asi & 2) == 0)
			goto segv;
	}

	return (1);

segv:
	trapsignal(p, SIGSEGV, isload ? PROT_READ : PROT_WRITE,
	    SEGV_MAPERR, sv);
	return (0);
}

int
emul_popc(int32_t insv, struct proc *p, union sigval sv, struct trapframe *tf)
{
	u_int64_t val, ret = 0;
	union instr ins;

	ins.i_int = insv;
	if (ins.i_simm13.i_i == 0)
		val = tf->tf_global[ins.i_asi.i_rs2];
	else
		val = SIGN_EXT13(ins.i_simm13.i_simm13);

	for (; val != 0; val >>= 1)
		ret += val & 1;

	tf->tf_global[ins.i_asi.i_rd] = ret;
	return (1);
}
