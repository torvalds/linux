/*	$NetBSD: fpu_emu.c,v 1.14 2005/12/11 12:18:42 christos Exp $ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Eduardo Horvath and Simon Burge for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)fpu.c	8.1 (Berkeley) 6/11/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/signal.h>
#include <sys/syslog.h>
#include <sys/signalvar.h>

#include <machine/fpu.h>
#include <machine/reg.h>

#include <powerpc/fpu/fpu_emu.h>
#include <powerpc/fpu/fpu_extern.h>
#include <powerpc/fpu/fpu_instr.h>

static SYSCTL_NODE(_hw, OID_AUTO, fpu_emu, CTLFLAG_RW, 0, "FPU emulator");

#define	FPU_EMU_EVCNT_DECL(name)					\
static u_int fpu_emu_evcnt_##name;					\
SYSCTL_INT(_hw_fpu_emu, OID_AUTO, evcnt_##name, CTLFLAG_RD,		\
    &fpu_emu_evcnt_##name, 0, "")

#define	FPU_EMU_EVCNT_INCR(name)	fpu_emu_evcnt_##name++

FPU_EMU_EVCNT_DECL(stfiwx);
FPU_EMU_EVCNT_DECL(fpstore);
FPU_EMU_EVCNT_DECL(fpload);
FPU_EMU_EVCNT_DECL(fcmpu);
FPU_EMU_EVCNT_DECL(frsp);
FPU_EMU_EVCNT_DECL(fctiw);
FPU_EMU_EVCNT_DECL(fcmpo);
FPU_EMU_EVCNT_DECL(mtfsb1);
FPU_EMU_EVCNT_DECL(fnegabs);
FPU_EMU_EVCNT_DECL(mcrfs);
FPU_EMU_EVCNT_DECL(mtfsb0);
FPU_EMU_EVCNT_DECL(fmr);
FPU_EMU_EVCNT_DECL(mtfsfi);
FPU_EMU_EVCNT_DECL(fnabs);
FPU_EMU_EVCNT_DECL(fabs);
FPU_EMU_EVCNT_DECL(mffs);
FPU_EMU_EVCNT_DECL(mtfsf);
FPU_EMU_EVCNT_DECL(fctid);
FPU_EMU_EVCNT_DECL(fcfid);
FPU_EMU_EVCNT_DECL(fdiv);
FPU_EMU_EVCNT_DECL(fsub);
FPU_EMU_EVCNT_DECL(fadd);
FPU_EMU_EVCNT_DECL(fsqrt);
FPU_EMU_EVCNT_DECL(fsel);
FPU_EMU_EVCNT_DECL(fpres);
FPU_EMU_EVCNT_DECL(fmul);
FPU_EMU_EVCNT_DECL(frsqrte);
FPU_EMU_EVCNT_DECL(fmulsub);
FPU_EMU_EVCNT_DECL(fmuladd);
FPU_EMU_EVCNT_DECL(fnmsub);
FPU_EMU_EVCNT_DECL(fnmadd);

/* FPSR exception masks */
#define FPSR_EX_MSK	(FPSCR_VX|FPSCR_OX|FPSCR_UX|FPSCR_ZX|		\
			FPSCR_XX|FPSCR_VXSNAN|FPSCR_VXISI|FPSCR_VXIDI|	\
			FPSCR_VXZDZ|FPSCR_VXIMZ|FPSCR_VXVC|FPSCR_VXSOFT|\
			FPSCR_VXSQRT|FPSCR_VXCVI)
#define	FPSR_EX		(FPSCR_VE|FPSCR_OE|FPSCR_UE|FPSCR_ZE|FPSCR_XE)
#define	FPSR_EXOP	(FPSR_EX_MSK&(~FPSR_EX))

int fpe_debug = 0;

#ifdef DEBUG
vm_offset_t opc_disasm(vm_offset_t, int);

/*
 * Dump a `fpn' structure.
 */
void
fpu_dumpfpn(struct fpn *fp)
{
	static const char *class[] = {
		"SNAN", "QNAN", "ZERO", "NUM", "INF"
	};

	printf("%s %c.%x %x %x %xE%d", class[fp->fp_class + 2],
		fp->fp_sign ? '-' : ' ',
		fp->fp_mant[0],	fp->fp_mant[1],
		fp->fp_mant[2], fp->fp_mant[3], 
		fp->fp_exp);
}
#endif

/*
 * fpu_execute returns the following error numbers (0 = no error):
 */
#define	FPE		1	/* take a floating point exception */
#define	NOTFPU		2	/* not an FPU instruction */
#define	FAULT		3


/*
 * Emulate a floating-point instruction.
 * Return zero for success, else signal number.
 * (Typically: zero, SIGFPE, SIGILL, SIGSEGV)
 */
int
fpu_emulate(struct trapframe *frame, struct fpu *fpf)
{
	union instr insn;
	struct fpemu fe;
	int sig;

	/* initialize insn.is_datasize to tell it is *not* initialized */
	fe.fe_fpstate = fpf;
	fe.fe_cx = 0;

	/* always set this (to avoid a warning) */

	if (copyin((void *) (frame->srr0), &insn.i_int, sizeof (insn.i_int))) {
#ifdef DEBUG
		printf("fpu_emulate: fault reading opcode\n");
#endif
		return SIGSEGV;
	}

	DPRINTF(FPE_EX, ("fpu_emulate: emulating insn %x at %p\n",
	    insn.i_int, (void *)frame->srr0));


	if ((insn.i_any.i_opcd == OPC_TWI) ||
	    ((insn.i_any.i_opcd == OPC_integer_31) &&
	    (insn.i_x.i_xo == OPC31_TW))) {
		/* Check for the two trap insns. */
		DPRINTF(FPE_EX, ("fpu_emulate: SIGTRAP\n"));
		return (SIGTRAP);
	}
	sig = 0;
	switch (fpu_execute(frame, &fe, &insn)) {
	case 0:
		DPRINTF(FPE_EX, ("fpu_emulate: success\n"));
		frame->srr0 += 4;
		break;

	case FPE:
		DPRINTF(FPE_EX, ("fpu_emulate: SIGFPE\n"));
		sig = SIGFPE;
		break;

	case FAULT:
		DPRINTF(FPE_EX, ("fpu_emulate: SIGSEGV\n"));
		sig = SIGSEGV;
		break;

	case NOTFPU:
	default:
		DPRINTF(FPE_EX, ("fpu_emulate: SIGILL\n"));
#ifdef DEBUG
		if (fpe_debug & FPE_EX) {
			printf("fpu_emulate:  illegal insn %x at %p:",
			insn.i_int, (void *) (frame->srr0));
			opc_disasm(frame->srr0, insn.i_int);
		}
#endif
		sig = SIGILL;
#ifdef DEBUG
		if (fpe_debug & FPE_EX)
			kdb_enter(KDB_WHY_UNSET, "illegal instruction");
#endif
		break;
	}

	return (sig);
}

/*
 * Execute an FPU instruction (one that runs entirely in the FPU; not
 * FBfcc or STF, for instance).  On return, fe->fe_fs->fs_fsr will be
 * modified to reflect the setting the hardware would have left.
 *
 * Note that we do not catch all illegal opcodes, so you can, for instance,
 * multiply two integers this way.
 */
int
fpu_execute(struct trapframe *tf, struct fpemu *fe, union instr *insn)
{
	struct fpn *fp;
	union instr instr = *insn;
	int *a;
	vm_offset_t addr;
	int ra, rb, rc, rt, type, mask, fsr, cx, bf, setcr;
	unsigned int cond;
	struct fpu *fs;

	/* Setup work. */
	fp = NULL;
	fs = fe->fe_fpstate;
	fe->fe_fpscr = ((int *)&fs->fpscr)[1];

	/*
	 * On PowerPC all floating point values are stored in registers
	 * as doubles, even when used for single precision operations.
	 */
	type = FTYPE_DBL;
	cond = instr.i_any.i_rc;
	setcr = 0;
	bf = 0;	/* XXX gcc */

#if defined(DDB) && defined(DEBUG)
	if (fpe_debug & FPE_EX) {
		vm_offset_t loc = tf->srr0;

		printf("Trying to emulate: %p ", (void *)loc);
		opc_disasm(loc, instr.i_int);
	}
#endif

	/*
	 * `Decode' and execute instruction.
	 */

	if ((instr.i_any.i_opcd >= OPC_LFS && instr.i_any.i_opcd <= OPC_STFDU) ||
	    instr.i_any.i_opcd == OPC_integer_31) {
		/*
		 * Handle load/store insns:
		 *
		 * Convert to/from single if needed, calculate addr,
		 * and update index reg if needed.
		 */
		double buf;
		size_t size = sizeof(float);
		int store, update;

		cond = 0; /* ld/st never set condition codes */


		if (instr.i_any.i_opcd == OPC_integer_31) {
			if (instr.i_x.i_xo == OPC31_STFIWX) {
				FPU_EMU_EVCNT_INCR(stfiwx);

				/* Store as integer */
				ra = instr.i_x.i_ra;
				rb = instr.i_x.i_rb;
				DPRINTF(FPE_INSN,
					("reg %d has %jx reg %d has %jx\n",
					ra, (uintmax_t)tf->fixreg[ra], rb,
					(uintmax_t)tf->fixreg[rb]));

				addr = tf->fixreg[rb];
				if (ra != 0)
					addr += tf->fixreg[ra];
				rt = instr.i_x.i_rt;
				a = (int *)&fs->fpr[rt].fpr;
				DPRINTF(FPE_INSN,
					("fpu_execute: Store INT %x at %p\n",
						a[1], (void *)addr));
				if (copyout(&a[1], (void *)addr, sizeof(int)))
					return (FAULT);
				return (0);
			}

			if ((instr.i_x.i_xo & OPC31_FPMASK) != OPC31_FPOP)
				/* Not an indexed FP load/store op */
				return (NOTFPU);

			store = (instr.i_x.i_xo & 0x80);
			if (instr.i_x.i_xo & 0x40)
				size = sizeof(double);
			else
				type = FTYPE_SNG;
			update = (instr.i_x.i_xo & 0x20);
			
			/* calculate EA of load/store */
			ra = instr.i_x.i_ra;
			rb = instr.i_x.i_rb;
			DPRINTF(FPE_INSN, ("reg %d has %jx reg %d has %jx\n",
				ra, (uintmax_t)tf->fixreg[ra], rb,
				(uintmax_t)tf->fixreg[rb]));
			addr = tf->fixreg[rb];
			if (ra != 0)
				addr += tf->fixreg[ra];
			rt = instr.i_x.i_rt;
		} else {
			store = instr.i_d.i_opcd & 0x4;
			if (instr.i_d.i_opcd & 0x2)
				size = sizeof(double);
			else
				type = FTYPE_SNG;
			update = instr.i_d.i_opcd & 0x1;

			/* calculate EA of load/store */
			ra = instr.i_d.i_ra;
			addr = instr.i_d.i_d;
			DPRINTF(FPE_INSN, ("reg %d has %jx displ %jx\n",
				ra, (uintmax_t)tf->fixreg[ra],
				(uintmax_t)addr));
			if (ra != 0)
				addr += tf->fixreg[ra];
			rt = instr.i_d.i_rt;
		}

		if (update && ra == 0)
			return (NOTFPU);

		if (store) {
			/* Store */
			FPU_EMU_EVCNT_INCR(fpstore);
			if (type != FTYPE_DBL) {
				DPRINTF(FPE_INSN,
					("fpu_execute: Store SNG at %p\n",
						(void *)addr));
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_DBL, rt);
				fpu_implode(fe, fp, type, (void *)&buf);
				if (copyout(&buf, (void *)addr, size))
					return (FAULT);
			} else {
				DPRINTF(FPE_INSN, 
					("fpu_execute: Store DBL at %p\n",
						(void *)addr));
				if (copyout(&fs->fpr[rt].fpr, (void *)addr,
				    size))
					return (FAULT);
			}
		} else {
			/* Load */
			FPU_EMU_EVCNT_INCR(fpload);
			DPRINTF(FPE_INSN, ("fpu_execute: Load from %p\n",
				(void *)addr));
			if (copyin((const void *)addr, &fs->fpr[rt].fpr,
			    size))
				return (FAULT);
			if (type != FTYPE_DBL) {
				fpu_explode(fe, fp = &fe->fe_f1, type, rt);
				fpu_implode(fe, fp, FTYPE_DBL, 
					(u_int *)&fs->fpr[rt].fpr);
			}
		}
		if (update) 
			tf->fixreg[ra] = addr;
		/* Complete. */
		return (0);
#ifdef notyet
	} else if (instr.i_any.i_opcd == OPC_load_st_62) {
		/* These are 64-bit extensions */
		return (NOTFPU);
#endif
	} else if (instr.i_any.i_opcd == OPC_sp_fp_59 ||
		instr.i_any.i_opcd == OPC_dp_fp_63) {


		if (instr.i_any.i_opcd == OPC_dp_fp_63 &&
		    !(instr.i_a.i_xo & OPC63M_MASK)) {
			/* Format X */
			rt = instr.i_x.i_rt;
			ra = instr.i_x.i_ra;
			rb = instr.i_x.i_rb;


			/* One of the special opcodes.... */
			switch (instr.i_x.i_xo) {
			case	OPC63_FCMPU:
				FPU_EMU_EVCNT_INCR(fcmpu);
				DPRINTF(FPE_INSN, ("fpu_execute: FCMPU\n"));
				rt >>= 2;
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fpu_compare(fe, 0);
				/* Make sure we do the condition regs. */
				cond = 0;
				/* N.B.: i_rs is already left shifted by two. */
				bf = instr.i_x.i_rs & 0xfc;
				setcr = 1;
				break;

			case	OPC63_FRSP:
				/*
				 * Convert to single: 
				 *
				 * PowerPC uses this to round a double
				 * precision value to single precision,
				 * but values in registers are always 
				 * stored in double precision format.
				 */
				FPU_EMU_EVCNT_INCR(frsp);
				DPRINTF(FPE_INSN, ("fpu_execute: FRSP\n"));
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_DBL, rb);
				fpu_implode(fe, fp, FTYPE_SNG, 
					(u_int *)&fs->fpr[rt].fpr);
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_SNG, rt);
				type = FTYPE_DBL;
				break;
			case	OPC63_FCTIW:
			case	OPC63_FCTIWZ:
				FPU_EMU_EVCNT_INCR(fctiw);
				DPRINTF(FPE_INSN, ("fpu_execute: FCTIW\n"));
				fpu_explode(fe, fp = &fe->fe_f1, type, rb);
				type = FTYPE_INT;
				break;
			case	OPC63_FCMPO:
				FPU_EMU_EVCNT_INCR(fcmpo);
				DPRINTF(FPE_INSN, ("fpu_execute: FCMPO\n"));
				rt >>= 2;
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fpu_compare(fe, 1);
				/* Make sure we do the condition regs. */
				cond = 0;
				/* N.B.: i_rs is already left shifted by two. */
				bf = instr.i_x.i_rs & 0xfc;
				setcr = 1;
				break;
			case	OPC63_MTFSB1:
				FPU_EMU_EVCNT_INCR(mtfsb1);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSB1\n"));
				fe->fe_fpscr |= 
					(~(FPSCR_VX|FPSR_EX) & (1<<(31-rt)));
				break;
			case	OPC63_FNEG:
				FPU_EMU_EVCNT_INCR(fnegabs);
				DPRINTF(FPE_INSN, ("fpu_execute: FNEGABS\n"));
				memcpy(&fs->fpr[rt].fpr, &fs->fpr[rb].fpr,
					sizeof(double));
				a = (int *)&fs->fpr[rt].fpr;
				*a ^= (1U << 31);
				break;
			case	OPC63_MCRFS:
				FPU_EMU_EVCNT_INCR(mcrfs);
				DPRINTF(FPE_INSN, ("fpu_execute: MCRFS\n"));
				cond = 0;
				rt &= 0x1c;
				ra &= 0x1c;
				/* Extract the bits we want */
				mask = (fe->fe_fpscr >> (28 - ra)) & 0xf;
				/* Clear the bits we copied. */
				fe->fe_cx =
					(FPSR_EX_MSK | (0xf << (28 - ra)));
				fe->fe_fpscr &= fe->fe_cx;
				/* Now shove them in the right part of cr */
				tf->cr &= ~(0xf << (28 - rt));
				tf->cr |= (mask << (28 - rt));
				break;
			case	OPC63_MTFSB0:
				FPU_EMU_EVCNT_INCR(mtfsb0);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSB0\n"));
				fe->fe_fpscr &=
					((FPSCR_VX|FPSR_EX) & ~(1<<(31-rt)));
				break;
			case	OPC63_FMR:
				FPU_EMU_EVCNT_INCR(fmr);
				DPRINTF(FPE_INSN, ("fpu_execute: FMR\n"));
				memcpy(&fs->fpr[rt].fpr, &fs->fpr[rb].fpr,
					sizeof(double));
				break;
			case	OPC63_MTFSFI:
				FPU_EMU_EVCNT_INCR(mtfsfi);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSFI\n"));
				rb >>= 1;
				rt &= 0x1c; /* Already left-shifted 4 */
				fe->fe_cx = rb << (28 - rt);
				mask = 0xf<<(28 - rt);
				fe->fe_fpscr = (fe->fe_fpscr & ~mask) | 
					fe->fe_cx;
/* XXX weird stuff about OX, FX, FEX, and VX should be handled */
				break;
			case	OPC63_FNABS:
				FPU_EMU_EVCNT_INCR(fnabs);
				DPRINTF(FPE_INSN, ("fpu_execute: FABS\n"));
				memcpy(&fs->fpr[rt].fpr, &fs->fpr[rb].fpr,
					sizeof(double));
				a = (int *)&fs->fpr[rt].fpr;
				*a |= (1U << 31);
				break;
			case	OPC63_FABS:
				FPU_EMU_EVCNT_INCR(fabs);
				DPRINTF(FPE_INSN, ("fpu_execute: FABS\n"));
				memcpy(&fs->fpr[rt].fpr, &fs->fpr[rb].fpr,
					sizeof(double));
				a = (int *)&fs->fpr[rt].fpr;
				*a &= ~(1U << 31);
				break;
			case	OPC63_MFFS:
				FPU_EMU_EVCNT_INCR(mffs);
				DPRINTF(FPE_INSN, ("fpu_execute: MFFS\n"));
				memcpy(&fs->fpr[rt].fpr, &fs->fpscr,
					sizeof(fs->fpscr));
				break;
			case	OPC63_MTFSF:
				FPU_EMU_EVCNT_INCR(mtfsf);
				DPRINTF(FPE_INSN, ("fpu_execute: MTFSF\n"));
				if ((rt = instr.i_xfl.i_flm) == -1)
					mask = -1;
				else {
					mask = 0;
					/* Convert 1 bit -> 4 bits */
					for (ra = 0; ra < 8; ra ++)
						if (rt & (1<<ra))
							mask |= (0xf<<(4*ra));
				}
				a = (int *)&fs->fpr[rt].fpr;
				fe->fe_cx = mask & a[1];
				fe->fe_fpscr = (fe->fe_fpscr&~mask) | 
					(fe->fe_cx);
/* XXX weird stuff about OX, FX, FEX, and VX should be handled */
				break;
			case	OPC63_FCTID:
			case	OPC63_FCTIDZ:
				FPU_EMU_EVCNT_INCR(fctid);
				DPRINTF(FPE_INSN, ("fpu_execute: FCTID\n"));
				fpu_explode(fe, fp = &fe->fe_f1, type, rb);
				type = FTYPE_LNG;
				break;
			case	OPC63_FCFID:
				FPU_EMU_EVCNT_INCR(fcfid);
				DPRINTF(FPE_INSN, ("fpu_execute: FCFID\n"));
				type = FTYPE_LNG;
				fpu_explode(fe, fp = &fe->fe_f1, type, rb);
				type = FTYPE_DBL;
				break;
			default:
				return (NOTFPU);
				break;
			}
		} else {
			/* Format A */
			rt = instr.i_a.i_frt;
			ra = instr.i_a.i_fra;
			rb = instr.i_a.i_frb;
			rc = instr.i_a.i_frc;

			/*
			 * All arithmetic operations work on registers, which
			 * are stored as doubles.
			 */
			type = FTYPE_DBL;
			switch ((unsigned int)instr.i_a.i_xo) {
			case	OPC59_FDIVS:
				FPU_EMU_EVCNT_INCR(fdiv);
				DPRINTF(FPE_INSN, ("fpu_execute: FDIV\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fp = fpu_div(fe);
				break;
			case	OPC59_FSUBS:
				FPU_EMU_EVCNT_INCR(fsub);
				DPRINTF(FPE_INSN, ("fpu_execute: FSUB\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fp = fpu_sub(fe);
				break;
			case	OPC59_FADDS:
				FPU_EMU_EVCNT_INCR(fadd);
				DPRINTF(FPE_INSN, ("fpu_execute: FADD\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fp = fpu_add(fe);
				break;
			case	OPC59_FSQRTS:
				FPU_EMU_EVCNT_INCR(fsqrt);
				DPRINTF(FPE_INSN, ("fpu_execute: FSQRT\n"));
				fpu_explode(fe, &fe->fe_f1, type, rb);
				fp = fpu_sqrt(fe);
				break;
			case	OPC63M_FSEL:
				FPU_EMU_EVCNT_INCR(fsel);
				DPRINTF(FPE_INSN, ("fpu_execute: FSEL\n"));
				a = (int *)&fe->fe_fpstate->fpr[ra].fpr;
				if ((*a & 0x80000000) && (*a & 0x7fffffff)) 
					/* fra < 0 */
					rc = rb;
				DPRINTF(FPE_INSN, ("f%d => f%d\n", rc, rt));
				memcpy(&fs->fpr[rt].fpr, &fs->fpr[rc].fpr,
					sizeof(double));
				break;
			case	OPC59_FRES:
				FPU_EMU_EVCNT_INCR(fpres);
				DPRINTF(FPE_INSN, ("fpu_execute: FPRES\n"));
				fpu_explode(fe, &fe->fe_f1, type, rb);
				fp = fpu_sqrt(fe);
				/* now we've gotta overwrite the dest reg */
				*((int *)&fe->fe_fpstate->fpr[rt].fpr) = 1;
				fpu_explode(fe, &fe->fe_f1, FTYPE_INT, rt);
				fpu_div(fe);
				break;
			case	OPC59_FMULS:
				FPU_EMU_EVCNT_INCR(fmul);
				DPRINTF(FPE_INSN, ("fpu_execute: FMUL\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rc);
				fp = fpu_mul(fe);
				break;
			case	OPC63M_FRSQRTE:
				/* Reciprocal sqrt() estimate */
				FPU_EMU_EVCNT_INCR(frsqrte);
				DPRINTF(FPE_INSN, ("fpu_execute: FRSQRTE\n"));
				fpu_explode(fe, &fe->fe_f1, type, rb);
				fp = fpu_sqrt(fe);
				fe->fe_f2 = *fp;
				/* now we've gotta overwrite the dest reg */
				*((int *)&fe->fe_fpstate->fpr[rt].fpr) = 1;
				fpu_explode(fe, &fe->fe_f1, FTYPE_INT, rt);
				fpu_div(fe);
				break;
			case	OPC59_FMSUBS:
				FPU_EMU_EVCNT_INCR(fmulsub);
				DPRINTF(FPE_INSN, ("fpu_execute: FMULSUB\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rc);
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fp = fpu_sub(fe);
				break;
			case	OPC59_FMADDS:
				FPU_EMU_EVCNT_INCR(fmuladd);
				DPRINTF(FPE_INSN, ("fpu_execute: FMULADD\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rc);
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fp = fpu_add(fe);
				break;
			case	OPC59_FNMSUBS:
				FPU_EMU_EVCNT_INCR(fnmsub);
				DPRINTF(FPE_INSN, ("fpu_execute: FNMSUB\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rc);
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fp = fpu_sub(fe);
				/* Negate */
				fp->fp_sign ^= 1;
				break;
			case	OPC59_FNMADDS:
				FPU_EMU_EVCNT_INCR(fnmadd);
				DPRINTF(FPE_INSN, ("fpu_execute: FNMADD\n"));
				fpu_explode(fe, &fe->fe_f1, type, ra);
				fpu_explode(fe, &fe->fe_f2, type, rc);
				fp = fpu_mul(fe);
				fe->fe_f1 = *fp;
				fpu_explode(fe, &fe->fe_f2, type, rb);
				fp = fpu_add(fe);
				/* Negate */
				fp->fp_sign ^= 1;
				break;
			default:
				return (NOTFPU);
				break;
			}

			/* If the instruction was single precision, round */
			if (!(instr.i_any.i_opcd & 0x4)) {
				fpu_implode(fe, fp, FTYPE_SNG, 
					(u_int *)&fs->fpr[rt].fpr);
				fpu_explode(fe, fp = &fe->fe_f1, FTYPE_SNG, rt);
			}
		}
	} else {
		return (NOTFPU);
	}

	/*
	 * ALU operation is complete.  Collapse the result and then check
	 * for exceptions.  If we got any, and they are enabled, do not
	 * alter the destination register, just stop with an exception.
	 * Otherwise set new current exceptions and accrue.
	 */
	if (fp)
		fpu_implode(fe, fp, type, (u_int *)&fs->fpr[rt].fpr);
	cx = fe->fe_cx;
	fsr = fe->fe_fpscr;
	if (cx != 0) {
		fsr &= ~FPSCR_FX;
		if ((cx^fsr)&FPSR_EX_MSK)
			fsr |= FPSCR_FX;
		mask = fsr & FPSR_EX;
		mask <<= (25-3);
		if (cx & mask) 
			fsr |= FPSCR_FEX;
		if (cx & FPSCR_FPRF) {
			/* Need to replace CC */
			fsr &= ~FPSCR_FPRF;
		}
		if (cx & (FPSR_EXOP))
			fsr |= FPSCR_VX;
		fsr |= cx;
		DPRINTF(FPE_INSN, ("fpu_execute: cx %x, fsr %x\n", cx, fsr));
	}

	if (cond) {
		cond = fsr & 0xf0000000;
		/* Isolate condition codes */
		cond >>= 28;
		/* Move fpu condition codes to cr[1] */
		tf->cr &= (0x0f000000);
		tf->cr |= (cond<<24);
		DPRINTF(FPE_INSN, ("fpu_execute: cr[1] <= %x\n", cond));
	}

	if (setcr) {
		cond = fsr & FPSCR_FPCC;
		/* Isolate condition codes */
		cond <<= 16;
		/* Move fpu condition codes to cr[1] */
		tf->cr &= ~(0xf0000000>>bf);
		tf->cr |= (cond>>bf);
		DPRINTF(FPE_INSN, ("fpu_execute: cr[%d] (cr=%jx) <= %x\n",
			bf/4, (uintmax_t)tf->cr, cond));
	}

	((int *)&fs->fpscr)[1] = fsr;
	if (fsr & FPSCR_FEX)
		return(FPE);
	return (0);	/* success */
}
