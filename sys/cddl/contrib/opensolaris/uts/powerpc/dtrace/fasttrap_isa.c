/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/* Portions Copyright 2013 Justin Hibbits */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/fasttrap_isa.h>
#include <sys/fasttrap_impl.h>
#include <sys/dtrace.h>
#include <sys/dtrace_impl.h>
#include <cddl/dev/dtrace/dtrace_cddl.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/rmlock.h>
#include <sys/sysent.h>

#define OP(x)	((x) >> 26)
#define OPX(x)	(((x) >> 2) & 0x3FF)
#define OP_BO(x) (((x) & 0x03E00000) >> 21)
#define OP_BI(x) (((x) & 0x001F0000) >> 16)
#define OP_RS(x) (((x) & 0x03E00000) >> 21)
#define OP_RA(x) (((x) & 0x001F0000) >> 16)
#define OP_RB(x) (((x) & 0x0000F100) >> 11)

int
fasttrap_tracepoint_install(proc_t *p, fasttrap_tracepoint_t *tp)
{
	fasttrap_instr_t instr = FASTTRAP_INSTR;

	if (uwrite(p, &instr, 4, tp->ftt_pc) != 0)
		return (-1);

	return (0);
}

int
fasttrap_tracepoint_remove(proc_t *p, fasttrap_tracepoint_t *tp)
{
	uint32_t instr;

	/*
	 * Distinguish between read or write failures and a changed
	 * instruction.
	 */
	if (uread(p, &instr, 4, tp->ftt_pc) != 0)
		return (0);
	if (instr != FASTTRAP_INSTR)
		return (0);
	if (uwrite(p, &tp->ftt_instr, 4, tp->ftt_pc) != 0)
		return (-1);

	return (0);
}

int
fasttrap_tracepoint_init(proc_t *p, fasttrap_tracepoint_t *tp, uintptr_t pc,
    fasttrap_probe_type_t type)
{
	uint32_t instr;
	//int32_t disp;

	/*
	 * Read the instruction at the given address out of the process's
	 * address space. We don't have to worry about a debugger
	 * changing this instruction before we overwrite it with our trap
	 * instruction since P_PR_LOCK is set.
	 */
	if (uread(p, &instr, 4, pc) != 0)
		return (-1);

	/*
	 * Decode the instruction to fill in the probe flags. We can have
	 * the process execute most instructions on its own using a pc/npc
	 * trick, but pc-relative control transfer present a problem since
	 * we're relocating the instruction. We emulate these instructions
	 * in the kernel. We assume a default type and over-write that as
	 * needed.
	 *
	 * pc-relative instructions must be emulated for correctness;
	 * other instructions (which represent a large set of commonly traced
	 * instructions) are emulated or otherwise optimized for performance.
	 */
	tp->ftt_type = FASTTRAP_T_COMMON;
	tp->ftt_instr = instr;

	switch (OP(instr)) {
	/* The following are invalid for trapping (invalid opcodes, tw/twi). */
	case 0:
	case 1:
	case 2:
	case 4:
	case 5:
	case 6:
	case 30:
	case 39:
	case 58:
	case 62:
	case 3:	/* twi */
		return (-1);
	case 31:	/* tw */
		if (OPX(instr) == 4)
			return (-1);
		else if (OPX(instr) == 444 && OP_RS(instr) == OP_RA(instr) &&
		    OP_RS(instr) == OP_RB(instr))
			tp->ftt_type = FASTTRAP_T_NOP;
		break;
	case 16:
		tp->ftt_type = FASTTRAP_T_BC;
		tp->ftt_dest = instr & 0x0000FFFC; /* Extract target address */
		if (instr & 0x00008000)
			tp->ftt_dest |= 0xFFFF0000;
		/* Use as offset if not absolute address. */
		if (!(instr & 0x02))
			tp->ftt_dest += pc;
		tp->ftt_bo = OP_BO(instr);
		tp->ftt_bi = OP_BI(instr);
		break;
	case 18:
		tp->ftt_type = FASTTRAP_T_B;
		tp->ftt_dest = instr & 0x03FFFFFC; /* Extract target address */
		if (instr & 0x02000000)
			tp->ftt_dest |= 0xFC000000;
		/* Use as offset if not absolute address. */
		if (!(instr & 0x02))
			tp->ftt_dest += pc;
		break;
	case 19:
		switch (OPX(instr)) {
		case 528:	/* bcctr */
			tp->ftt_type = FASTTRAP_T_BCTR;
			tp->ftt_bo = OP_BO(instr);
			tp->ftt_bi = OP_BI(instr);
			break;
		case 16:	/* bclr */
			tp->ftt_type = FASTTRAP_T_BCTR;
			tp->ftt_bo = OP_BO(instr);
			tp->ftt_bi = OP_BI(instr);
			break;
		};
		break;
	case 24:
		if (OP_RS(instr) == OP_RA(instr) &&
		    (instr & 0x0000FFFF) == 0)
			tp->ftt_type = FASTTRAP_T_NOP;
		break;
	};

	/*
	 * We don't know how this tracepoint is going to be used, but in case
	 * it's used as part of a function return probe, we need to indicate
	 * whether it's always a return site or only potentially a return
	 * site. If it's part of a return probe, it's always going to be a
	 * return from that function if it's a restore instruction or if
	 * the previous instruction was a return. If we could reliably
	 * distinguish jump tables from return sites, this wouldn't be
	 * necessary.
	 */
#if 0
	if (tp->ftt_type != FASTTRAP_T_RESTORE &&
	    (uread(p, &instr, 4, pc - sizeof (instr)) != 0 ||
	    !(OP(instr) == 2 && OP3(instr) == OP3_RETURN)))
		tp->ftt_flags |= FASTTRAP_F_RETMAYBE;
#endif

	return (0);
}

static uint64_t
fasttrap_anarg(struct reg *rp, int argno)
{
	uint64_t value;
	proc_t  *p = curproc;

	/* The first 8 arguments are in registers. */
	if (argno < 8)
		return rp->fixreg[argno + 3];

	/* Arguments on stack start after SP+LR (2 register slots). */
	if (SV_PROC_FLAG(p, SV_ILP32)) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		value = dtrace_fuword32((void *)(rp->fixreg[1] + 8 +
		    ((argno - 8) * sizeof(uint32_t))));
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT | CPU_DTRACE_BADADDR);
	} else {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		value = dtrace_fuword64((void *)(rp->fixreg[1] + 48 +
		    ((argno - 8) * sizeof(uint64_t))));
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT | CPU_DTRACE_BADADDR);
	}
	return value;
}

uint64_t
fasttrap_pid_getarg(void *arg, dtrace_id_t id, void *parg, int argno,
    int aframes)
{
	struct reg r;

	fill_regs(curthread, &r);

	return (fasttrap_anarg(&r, argno));
}

uint64_t
fasttrap_usdt_getarg(void *arg, dtrace_id_t id, void *parg, int argno,
    int aframes)
{
	struct reg r;

	fill_regs(curthread, &r);

	return (fasttrap_anarg(&r, argno));
}

static void
fasttrap_usdt_args(fasttrap_probe_t *probe, struct reg *rp, int argc,
    uintptr_t *argv)
{
	int i, x, cap = MIN(argc, probe->ftp_nargs);

	for (i = 0; i < cap; i++) {
		x = probe->ftp_argmap[i];

		if (x < 8)
			argv[i] = rp->fixreg[x];
		else
			if (SV_PROC_FLAG(curproc, SV_ILP32))
				argv[i] = fuword32((void *)(rp->fixreg[1] + 8 +
				    (x * sizeof(uint32_t))));
			else
				argv[i] = fuword64((void *)(rp->fixreg[1] + 48 +
				    (x * sizeof(uint64_t))));
	}

	for (; i < argc; i++) {
		argv[i] = 0;
	}
}

static void
fasttrap_return_common(struct reg *rp, uintptr_t pc, pid_t pid,
    uintptr_t new_pc)
{
	struct rm_priotracker tracker;
	fasttrap_tracepoint_t *tp;
	fasttrap_bucket_t *bucket;
	fasttrap_id_t *id;

	rm_rlock(&fasttrap_tp_lock, &tracker);
	bucket = &fasttrap_tpoints.fth_table[FASTTRAP_TPOINTS_INDEX(pid, pc)];

	for (tp = bucket->ftb_data; tp != NULL; tp = tp->ftt_next) {
		if (pid == tp->ftt_pid && pc == tp->ftt_pc &&
		    tp->ftt_proc->ftpc_acount != 0)
			break;
	}

	/*
	 * Don't sweat it if we can't find the tracepoint again; unlike
	 * when we're in fasttrap_pid_probe(), finding the tracepoint here
	 * is not essential to the correct execution of the process.
	 */
	if (tp == NULL) {
		rm_runlock(&fasttrap_tp_lock, &tracker);
		return;
	}

	for (id = tp->ftt_retids; id != NULL; id = id->fti_next) {
		/*
		 * If there's a branch that could act as a return site, we
		 * need to trace it, and check here if the program counter is
		 * external to the function.
		 */
		/* Skip function-local branches. */
		if ((new_pc - id->fti_probe->ftp_faddr) < id->fti_probe->ftp_fsize)
			continue;

		dtrace_probe(id->fti_probe->ftp_id,
		    pc - id->fti_probe->ftp_faddr,
		    rp->fixreg[3], rp->fixreg[4], 0, 0);
	}
	rm_runlock(&fasttrap_tp_lock, &tracker);
}


static int
fasttrap_branch_taken(int bo, int bi, struct reg *regs)
{
	int crzero = 0;

	/* Branch always? */
	if ((bo & 0x14) == 0x14)
		return 1;

	/* Handle decrementing ctr */
	if (!(bo & 0x04)) {
		--regs->ctr;
		crzero = (regs->ctr == 0);
		if (bo & 0x10) {
			return (!(crzero ^ (bo >> 1)));
		}
	}

	return (crzero | (((regs->cr >> (31 - bi)) ^ (bo >> 3)) ^ 1));
}


int
fasttrap_pid_probe(struct trapframe *frame)
{
	struct reg reg, *rp;
	struct rm_priotracker tracker;
	proc_t *p = curproc;
	uintptr_t pc;
	uintptr_t new_pc = 0;
	fasttrap_bucket_t *bucket;
	fasttrap_tracepoint_t *tp, tp_local;
	pid_t pid;
	dtrace_icookie_t cookie;
	uint_t is_enabled = 0;

	fill_regs(curthread, &reg);
	rp = &reg;
	pc = rp->pc;

	/*
	 * It's possible that a user (in a veritable orgy of bad planning)
	 * could redirect this thread's flow of control before it reached the
	 * return probe fasttrap. In this case we need to kill the process
	 * since it's in a unrecoverable state.
	 */
	if (curthread->t_dtrace_step) {
		ASSERT(curthread->t_dtrace_on);
		fasttrap_sigtrap(p, curthread, pc);
		return (0);
	}

	/*
	 * Clear all user tracing flags.
	 */
	curthread->t_dtrace_ft = 0;
	curthread->t_dtrace_pc = 0;
	curthread->t_dtrace_npc = 0;
	curthread->t_dtrace_scrpc = 0;
	curthread->t_dtrace_astpc = 0;

	rm_rlock(&fasttrap_tp_lock, &tracker);
	pid = p->p_pid;
	bucket = &fasttrap_tpoints.fth_table[FASTTRAP_TPOINTS_INDEX(pid, pc)];

	/*
	 * Lookup the tracepoint that the process just hit.
	 */
	for (tp = bucket->ftb_data; tp != NULL; tp = tp->ftt_next) {
		if (pid == tp->ftt_pid && pc == tp->ftt_pc &&
		    tp->ftt_proc->ftpc_acount != 0)
			break;
	}

	/*
	 * If we couldn't find a matching tracepoint, either a tracepoint has
	 * been inserted without using the pid<pid> ioctl interface (see
	 * fasttrap_ioctl), or somehow we have mislaid this tracepoint.
	 */
	if (tp == NULL) {
		rm_runlock(&fasttrap_tp_lock, &tracker);
		return (-1);
	}

	if (tp->ftt_ids != NULL) {
		fasttrap_id_t *id;

		for (id = tp->ftt_ids; id != NULL; id = id->fti_next) {
			fasttrap_probe_t *probe = id->fti_probe;

			if (id->fti_ptype == DTFTP_ENTRY) {
				/*
				 * We note that this was an entry
				 * probe to help ustack() find the
				 * first caller.
				 */
				cookie = dtrace_interrupt_disable();
				DTRACE_CPUFLAG_SET(CPU_DTRACE_ENTRY);
				dtrace_probe(probe->ftp_id, rp->fixreg[3],
						rp->fixreg[4], rp->fixreg[5], rp->fixreg[6],
						rp->fixreg[7]);
				DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_ENTRY);
				dtrace_interrupt_enable(cookie);
			} else if (id->fti_ptype == DTFTP_IS_ENABLED) {
				/*
				 * Note that in this case, we don't
				 * call dtrace_probe() since it's only
				 * an artificial probe meant to change
				 * the flow of control so that it
				 * encounters the true probe.
				 */
				is_enabled = 1;
			} else if (probe->ftp_argmap == NULL) {
				dtrace_probe(probe->ftp_id, rp->fixreg[3],
				    rp->fixreg[4], rp->fixreg[5], rp->fixreg[6],
				    rp->fixreg[7]);
			} else {
				uintptr_t t[5];

				fasttrap_usdt_args(probe, rp,
				    sizeof (t) / sizeof (t[0]), t);

				dtrace_probe(probe->ftp_id, t[0], t[1],
				    t[2], t[3], t[4]);
			}
		}
	}

	/*
	 * We're about to do a bunch of work so we cache a local copy of
	 * the tracepoint to emulate the instruction, and then find the
	 * tracepoint again later if we need to light up any return probes.
	 */
	tp_local = *tp;
	rm_runlock(&fasttrap_tp_lock, &tracker);
	tp = &tp_local;

	/*
	 * If there's an is-enabled probe connected to this tracepoint it
	 * means that there was a 'xor r3, r3, r3'
	 * instruction that was placed there by DTrace when the binary was
	 * linked. As this probe is, in fact, enabled, we need to stuff 1
	 * into R3. Accordingly, we can bypass all the instruction
	 * emulation logic since we know the inevitable result. It's possible
	 * that a user could construct a scenario where the 'is-enabled'
	 * probe was on some other instruction, but that would be a rather
	 * exotic way to shoot oneself in the foot.
	 */
	if (is_enabled) {
		rp->fixreg[3] = 1;
		new_pc = rp->pc + 4;
		goto done;
	}


	switch (tp->ftt_type) {
	case FASTTRAP_T_NOP:
		new_pc = rp->pc + 4;
		break;
	case FASTTRAP_T_BC:
		if (!fasttrap_branch_taken(tp->ftt_bo, tp->ftt_bi, rp))
			break;
		/* FALLTHROUGH */
	case FASTTRAP_T_B:
		if (tp->ftt_instr & 0x01)
			rp->lr = rp->pc + 4;
		new_pc = tp->ftt_dest;
		break;
	case FASTTRAP_T_BLR:
	case FASTTRAP_T_BCTR:
		if (!fasttrap_branch_taken(tp->ftt_bo, tp->ftt_bi, rp))
			break;
		/* FALLTHROUGH */
		if (tp->ftt_type == FASTTRAP_T_BCTR)
			new_pc = rp->ctr;
		else
			new_pc = rp->lr;
		if (tp->ftt_instr & 0x01)
			rp->lr = rp->pc + 4;
		break;
	case FASTTRAP_T_COMMON:
		break;
	};
done:
	/*
	 * If there were no return probes when we first found the tracepoint,
	 * we should feel no obligation to honor any return probes that were
	 * subsequently enabled -- they'll just have to wait until the next
	 * time around.
	 */
	if (tp->ftt_retids != NULL) {
		/*
		 * We need to wait until the results of the instruction are
		 * apparent before invoking any return probes. If this
		 * instruction was emulated we can just call
		 * fasttrap_return_common(); if it needs to be executed, we
		 * need to wait until the user thread returns to the kernel.
		 */
		if (tp->ftt_type != FASTTRAP_T_COMMON) {
			fasttrap_return_common(rp, pc, pid, new_pc);
		} else {
			ASSERT(curthread->t_dtrace_ret != 0);
			ASSERT(curthread->t_dtrace_pc == pc);
			ASSERT(curthread->t_dtrace_scrpc != 0);
			ASSERT(new_pc == curthread->t_dtrace_astpc);
		}
	}

	rp->pc = new_pc;
	set_regs(curthread, rp);

	return (0);
}

int
fasttrap_return_probe(struct trapframe *tf)
{
	struct reg reg, *rp;
	proc_t *p = curproc;
	uintptr_t pc = curthread->t_dtrace_pc;
	uintptr_t npc = curthread->t_dtrace_npc;

	curthread->t_dtrace_pc = 0;
	curthread->t_dtrace_npc = 0;
	curthread->t_dtrace_scrpc = 0;
	curthread->t_dtrace_astpc = 0;

	fill_regs(curthread, &reg);
	rp = &reg;

	/*
	 * We set rp->pc to the address of the traced instruction so
	 * that it appears to dtrace_probe() that we're on the original
	 * instruction.
	 */
	rp->pc = pc;

	fasttrap_return_common(rp, pc, p->p_pid, npc);

	return (0);
}
