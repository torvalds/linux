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
 *
 * Portions Copyright 2010 The FreeBSD Foundation
 *
 * $FreeBSD$
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/fasttrap_isa.h>
#include <sys/fasttrap_impl.h>
#include <sys/dtrace.h>
#include <sys/dtrace_impl.h>
#include <sys/cmn_err.h>
#include <sys/types.h>
#include <sys/dtrace_bsd.h>
#include <sys/proc.h>
#include <sys/rmlock.h>
#include <cddl/dev/dtrace/dtrace_cddl.h>
#include <cddl/dev/dtrace/x86/regset.h>
#include <machine/segments.h>
#include <machine/reg.h>
#include <machine/pcb.h>
#include <machine/trap.h>
#include <sys/sysmacros.h>
#include <sys/ptrace.h>

#ifdef __i386__
#define	r_rax	r_eax
#define	r_rbx	r_ebx
#define	r_rip	r_eip
#define	r_rflags r_eflags
#define	r_rsp	r_esp
#define	r_rbp	r_ebp
#endif

/*
 * Lossless User-Land Tracing on x86
 * ---------------------------------
 *
 * The execution of most instructions is not dependent on the address; for
 * these instructions it is sufficient to copy them into the user process's
 * address space and execute them. To effectively single-step an instruction
 * in user-land, we copy out the following sequence of instructions to scratch
 * space in the user thread's ulwp_t structure.
 *
 * We then set the program counter (%eip or %rip) to point to this scratch
 * space. Once execution resumes, the original instruction is executed and
 * then control flow is redirected to what was originally the subsequent
 * instruction. If the kernel attemps to deliver a signal while single-
 * stepping, the signal is deferred and the program counter is moved into the
 * second sequence of instructions. The second sequence ends in a trap into
 * the kernel where the deferred signal is then properly handled and delivered.
 *
 * For instructions whose execute is position dependent, we perform simple
 * emulation. These instructions are limited to control transfer
 * instructions in 32-bit mode, but in 64-bit mode there's the added wrinkle
 * of %rip-relative addressing that means that almost any instruction can be
 * position dependent. For all the details on how we emulate generic
 * instructions included %rip-relative instructions, see the code in
 * fasttrap_pid_probe() below where we handle instructions of type
 * FASTTRAP_T_COMMON (under the header: Generic Instruction Tracing).
 */

#define	FASTTRAP_MODRM_MOD(modrm)	(((modrm) >> 6) & 0x3)
#define	FASTTRAP_MODRM_REG(modrm)	(((modrm) >> 3) & 0x7)
#define	FASTTRAP_MODRM_RM(modrm)	((modrm) & 0x7)
#define	FASTTRAP_MODRM(mod, reg, rm)	(((mod) << 6) | ((reg) << 3) | (rm))

#define	FASTTRAP_SIB_SCALE(sib)		(((sib) >> 6) & 0x3)
#define	FASTTRAP_SIB_INDEX(sib)		(((sib) >> 3) & 0x7)
#define	FASTTRAP_SIB_BASE(sib)		((sib) & 0x7)

#define	FASTTRAP_REX_W(rex)		(((rex) >> 3) & 1)
#define	FASTTRAP_REX_R(rex)		(((rex) >> 2) & 1)
#define	FASTTRAP_REX_X(rex)		(((rex) >> 1) & 1)
#define	FASTTRAP_REX_B(rex)		((rex) & 1)
#define	FASTTRAP_REX(w, r, x, b)	\
	(0x40 | ((w) << 3) | ((r) << 2) | ((x) << 1) | (b))

/*
 * Single-byte op-codes.
 */
#define	FASTTRAP_PUSHL_EBP	0x55

#define	FASTTRAP_JO		0x70
#define	FASTTRAP_JNO		0x71
#define	FASTTRAP_JB		0x72
#define	FASTTRAP_JAE		0x73
#define	FASTTRAP_JE		0x74
#define	FASTTRAP_JNE		0x75
#define	FASTTRAP_JBE		0x76
#define	FASTTRAP_JA		0x77
#define	FASTTRAP_JS		0x78
#define	FASTTRAP_JNS		0x79
#define	FASTTRAP_JP		0x7a
#define	FASTTRAP_JNP		0x7b
#define	FASTTRAP_JL		0x7c
#define	FASTTRAP_JGE		0x7d
#define	FASTTRAP_JLE		0x7e
#define	FASTTRAP_JG		0x7f

#define	FASTTRAP_NOP		0x90

#define	FASTTRAP_MOV_EAX	0xb8
#define	FASTTRAP_MOV_ECX	0xb9

#define	FASTTRAP_RET16		0xc2
#define	FASTTRAP_RET		0xc3

#define	FASTTRAP_LOOPNZ		0xe0
#define	FASTTRAP_LOOPZ		0xe1
#define	FASTTRAP_LOOP		0xe2
#define	FASTTRAP_JCXZ		0xe3

#define	FASTTRAP_CALL		0xe8
#define	FASTTRAP_JMP32		0xe9
#define	FASTTRAP_JMP8		0xeb

#define	FASTTRAP_INT3		0xcc
#define	FASTTRAP_INT		0xcd

#define	FASTTRAP_2_BYTE_OP	0x0f
#define	FASTTRAP_GROUP5_OP	0xff

/*
 * Two-byte op-codes (second byte only).
 */
#define	FASTTRAP_0F_JO		0x80
#define	FASTTRAP_0F_JNO		0x81
#define	FASTTRAP_0F_JB		0x82
#define	FASTTRAP_0F_JAE		0x83
#define	FASTTRAP_0F_JE		0x84
#define	FASTTRAP_0F_JNE		0x85
#define	FASTTRAP_0F_JBE		0x86
#define	FASTTRAP_0F_JA		0x87
#define	FASTTRAP_0F_JS		0x88
#define	FASTTRAP_0F_JNS		0x89
#define	FASTTRAP_0F_JP		0x8a
#define	FASTTRAP_0F_JNP		0x8b
#define	FASTTRAP_0F_JL		0x8c
#define	FASTTRAP_0F_JGE		0x8d
#define	FASTTRAP_0F_JLE		0x8e
#define	FASTTRAP_0F_JG		0x8f

#define	FASTTRAP_EFLAGS_OF	0x800
#define	FASTTRAP_EFLAGS_DF	0x400
#define	FASTTRAP_EFLAGS_SF	0x080
#define	FASTTRAP_EFLAGS_ZF	0x040
#define	FASTTRAP_EFLAGS_AF	0x010
#define	FASTTRAP_EFLAGS_PF	0x004
#define	FASTTRAP_EFLAGS_CF	0x001

/*
 * Instruction prefixes.
 */
#define	FASTTRAP_PREFIX_OPERAND	0x66
#define	FASTTRAP_PREFIX_ADDRESS	0x67
#define	FASTTRAP_PREFIX_CS	0x2E
#define	FASTTRAP_PREFIX_DS	0x3E
#define	FASTTRAP_PREFIX_ES	0x26
#define	FASTTRAP_PREFIX_FS	0x64
#define	FASTTRAP_PREFIX_GS	0x65
#define	FASTTRAP_PREFIX_SS	0x36
#define	FASTTRAP_PREFIX_LOCK	0xF0
#define	FASTTRAP_PREFIX_REP	0xF3
#define	FASTTRAP_PREFIX_REPNE	0xF2

#define	FASTTRAP_NOREG	0xff

/*
 * Map between instruction register encodings and the kernel constants which
 * correspond to indicies into struct regs.
 */
#ifdef __amd64
static const uint8_t regmap[16] = {
	REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
	REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
};
#else
static const uint8_t regmap[8] = {
	EAX, ECX, EDX, EBX, UESP, EBP, ESI, EDI
};
#endif

static ulong_t fasttrap_getreg(struct reg *, uint_t);

static uint64_t
fasttrap_anarg(struct reg *rp, int function_entry, int argno)
{
	uint64_t value = 0;
	int shift = function_entry ? 1 : 0;

#ifdef __amd64
	if (curproc->p_model == DATAMODEL_LP64) {
		uintptr_t *stack;

		/*
		 * In 64-bit mode, the first six arguments are stored in
		 * registers.
		 */
		if (argno < 6)
			switch (argno) {
			case 0:
				return (rp->r_rdi);
			case 1:
				return (rp->r_rsi);
			case 2:
				return (rp->r_rdx);
			case 3:
				return (rp->r_rcx);
			case 4:
				return (rp->r_r8);
			case 5:
				return (rp->r_r9);
			}

		stack = (uintptr_t *)rp->r_rsp;
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		value = dtrace_fulword(&stack[argno - 6 + shift]);
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT | CPU_DTRACE_BADADDR);
	} else {
#endif
		uint32_t *stack = (uint32_t *)rp->r_rsp;
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		value = dtrace_fuword32(&stack[argno + shift]);
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT | CPU_DTRACE_BADADDR);
#ifdef __amd64
	}
#endif

	return (value);
}

/*ARGSUSED*/
int
fasttrap_tracepoint_init(proc_t *p, fasttrap_tracepoint_t *tp, uintptr_t pc,
    fasttrap_probe_type_t type)
{
	uint8_t instr[FASTTRAP_MAX_INSTR_SIZE + 10];
	size_t len = FASTTRAP_MAX_INSTR_SIZE;
	size_t first = MIN(len, PAGESIZE - (pc & PAGEOFFSET));
	uint_t start = 0;
	int rmindex, size;
	uint8_t seg, rex = 0;

	/*
	 * Read the instruction at the given address out of the process's
	 * address space. We don't have to worry about a debugger
	 * changing this instruction before we overwrite it with our trap
	 * instruction since P_PR_LOCK is set. Since instructions can span
	 * pages, we potentially read the instruction in two parts. If the
	 * second part fails, we just zero out that part of the instruction.
	 */
	if (uread(p, &instr[0], first, pc) != 0)
		return (-1);
	if (len > first &&
	    uread(p, &instr[first], len - first, pc + first) != 0) {
		bzero(&instr[first], len - first);
		len = first;
	}

	/*
	 * If the disassembly fails, then we have a malformed instruction.
	 */
	if ((size = dtrace_instr_size_isa(instr, p->p_model, &rmindex)) <= 0)
		return (-1);

	/*
	 * Make sure the disassembler isn't completely broken.
	 */
	ASSERT(-1 <= rmindex && rmindex < size);

	/*
	 * If the computed size is greater than the number of bytes read,
	 * then it was a malformed instruction possibly because it fell on a
	 * page boundary and the subsequent page was missing or because of
	 * some malicious user.
	 */
	if (size > len)
		return (-1);

	tp->ftt_size = (uint8_t)size;
	tp->ftt_segment = FASTTRAP_SEG_NONE;

	/*
	 * Find the start of the instruction's opcode by processing any
	 * legacy prefixes.
	 */
	for (;;) {
		seg = 0;
		switch (instr[start]) {
		case FASTTRAP_PREFIX_SS:
			seg++;
			/*FALLTHRU*/
		case FASTTRAP_PREFIX_GS:
			seg++;
			/*FALLTHRU*/
		case FASTTRAP_PREFIX_FS:
			seg++;
			/*FALLTHRU*/
		case FASTTRAP_PREFIX_ES:
			seg++;
			/*FALLTHRU*/
		case FASTTRAP_PREFIX_DS:
			seg++;
			/*FALLTHRU*/
		case FASTTRAP_PREFIX_CS:
			seg++;
			/*FALLTHRU*/
		case FASTTRAP_PREFIX_OPERAND:
		case FASTTRAP_PREFIX_ADDRESS:
		case FASTTRAP_PREFIX_LOCK:
		case FASTTRAP_PREFIX_REP:
		case FASTTRAP_PREFIX_REPNE:
			if (seg != 0) {
				/*
				 * It's illegal for an instruction to specify
				 * two segment prefixes -- give up on this
				 * illegal instruction.
				 */
				if (tp->ftt_segment != FASTTRAP_SEG_NONE)
					return (-1);

				tp->ftt_segment = seg;
			}
			start++;
			continue;
		}
		break;
	}

#ifdef __amd64
	/*
	 * Identify the REX prefix on 64-bit processes.
	 */
	if (p->p_model == DATAMODEL_LP64 && (instr[start] & 0xf0) == 0x40)
		rex = instr[start++];
#endif

	/*
	 * Now that we're pretty sure that the instruction is okay, copy the
	 * valid part to the tracepoint.
	 */
	bcopy(instr, tp->ftt_instr, FASTTRAP_MAX_INSTR_SIZE);

	tp->ftt_type = FASTTRAP_T_COMMON;
	if (instr[start] == FASTTRAP_2_BYTE_OP) {
		switch (instr[start + 1]) {
		case FASTTRAP_0F_JO:
		case FASTTRAP_0F_JNO:
		case FASTTRAP_0F_JB:
		case FASTTRAP_0F_JAE:
		case FASTTRAP_0F_JE:
		case FASTTRAP_0F_JNE:
		case FASTTRAP_0F_JBE:
		case FASTTRAP_0F_JA:
		case FASTTRAP_0F_JS:
		case FASTTRAP_0F_JNS:
		case FASTTRAP_0F_JP:
		case FASTTRAP_0F_JNP:
		case FASTTRAP_0F_JL:
		case FASTTRAP_0F_JGE:
		case FASTTRAP_0F_JLE:
		case FASTTRAP_0F_JG:
			tp->ftt_type = FASTTRAP_T_JCC;
			tp->ftt_code = (instr[start + 1] & 0x0f) | FASTTRAP_JO;
			tp->ftt_dest = pc + tp->ftt_size +
			    /* LINTED - alignment */
			    *(int32_t *)&instr[start + 2];
			break;
		}
	} else if (instr[start] == FASTTRAP_GROUP5_OP) {
		uint_t mod = FASTTRAP_MODRM_MOD(instr[start + 1]);
		uint_t reg = FASTTRAP_MODRM_REG(instr[start + 1]);
		uint_t rm = FASTTRAP_MODRM_RM(instr[start + 1]);

		if (reg == 2 || reg == 4) {
			uint_t i, sz;

			if (reg == 2)
				tp->ftt_type = FASTTRAP_T_CALL;
			else
				tp->ftt_type = FASTTRAP_T_JMP;

			if (mod == 3)
				tp->ftt_code = 2;
			else
				tp->ftt_code = 1;

			ASSERT(p->p_model == DATAMODEL_LP64 || rex == 0);

			/*
			 * See AMD x86-64 Architecture Programmer's Manual
			 * Volume 3, Section 1.2.7, Table 1-12, and
			 * Appendix A.3.1, Table A-15.
			 */
			if (mod != 3 && rm == 4) {
				uint8_t sib = instr[start + 2];
				uint_t index = FASTTRAP_SIB_INDEX(sib);
				uint_t base = FASTTRAP_SIB_BASE(sib);

				tp->ftt_scale = FASTTRAP_SIB_SCALE(sib);

				tp->ftt_index = (index == 4) ?
				    FASTTRAP_NOREG :
				    regmap[index | (FASTTRAP_REX_X(rex) << 3)];
				tp->ftt_base = (mod == 0 && base == 5) ?
				    FASTTRAP_NOREG :
				    regmap[base | (FASTTRAP_REX_B(rex) << 3)];

				i = 3;
				sz = mod == 1 ? 1 : 4;
			} else {
				/*
				 * In 64-bit mode, mod == 0 and r/m == 5
				 * denotes %rip-relative addressing; in 32-bit
				 * mode, the base register isn't used. In both
				 * modes, there is a 32-bit operand.
				 */
				if (mod == 0 && rm == 5) {
#ifdef __amd64
					if (p->p_model == DATAMODEL_LP64)
						tp->ftt_base = REG_RIP;
					else
#endif
						tp->ftt_base = FASTTRAP_NOREG;
					sz = 4;
				} else  {
					uint8_t base = rm |
					    (FASTTRAP_REX_B(rex) << 3);

					tp->ftt_base = regmap[base];
					sz = mod == 1 ? 1 : mod == 2 ? 4 : 0;
				}
				tp->ftt_index = FASTTRAP_NOREG;
				i = 2;
			}

			if (sz == 1) {
				tp->ftt_dest = *(int8_t *)&instr[start + i];
			} else if (sz == 4) {
				/* LINTED - alignment */
				tp->ftt_dest = *(int32_t *)&instr[start + i];
			} else {
				tp->ftt_dest = 0;
			}
		}
	} else {
		switch (instr[start]) {
		case FASTTRAP_RET:
			tp->ftt_type = FASTTRAP_T_RET;
			break;

		case FASTTRAP_RET16:
			tp->ftt_type = FASTTRAP_T_RET16;
			/* LINTED - alignment */
			tp->ftt_dest = *(uint16_t *)&instr[start + 1];
			break;

		case FASTTRAP_JO:
		case FASTTRAP_JNO:
		case FASTTRAP_JB:
		case FASTTRAP_JAE:
		case FASTTRAP_JE:
		case FASTTRAP_JNE:
		case FASTTRAP_JBE:
		case FASTTRAP_JA:
		case FASTTRAP_JS:
		case FASTTRAP_JNS:
		case FASTTRAP_JP:
		case FASTTRAP_JNP:
		case FASTTRAP_JL:
		case FASTTRAP_JGE:
		case FASTTRAP_JLE:
		case FASTTRAP_JG:
			tp->ftt_type = FASTTRAP_T_JCC;
			tp->ftt_code = instr[start];
			tp->ftt_dest = pc + tp->ftt_size +
			    (int8_t)instr[start + 1];
			break;

		case FASTTRAP_LOOPNZ:
		case FASTTRAP_LOOPZ:
		case FASTTRAP_LOOP:
			tp->ftt_type = FASTTRAP_T_LOOP;
			tp->ftt_code = instr[start];
			tp->ftt_dest = pc + tp->ftt_size +
			    (int8_t)instr[start + 1];
			break;

		case FASTTRAP_JCXZ:
			tp->ftt_type = FASTTRAP_T_JCXZ;
			tp->ftt_dest = pc + tp->ftt_size +
			    (int8_t)instr[start + 1];
			break;

		case FASTTRAP_CALL:
			tp->ftt_type = FASTTRAP_T_CALL;
			tp->ftt_dest = pc + tp->ftt_size +
			    /* LINTED - alignment */
			    *(int32_t *)&instr[start + 1];
			tp->ftt_code = 0;
			break;

		case FASTTRAP_JMP32:
			tp->ftt_type = FASTTRAP_T_JMP;
			tp->ftt_dest = pc + tp->ftt_size +
			    /* LINTED - alignment */
			    *(int32_t *)&instr[start + 1];
			break;
		case FASTTRAP_JMP8:
			tp->ftt_type = FASTTRAP_T_JMP;
			tp->ftt_dest = pc + tp->ftt_size +
			    (int8_t)instr[start + 1];
			break;

		case FASTTRAP_PUSHL_EBP:
			if (start == 0)
				tp->ftt_type = FASTTRAP_T_PUSHL_EBP;
			break;

		case FASTTRAP_NOP:
#ifdef __amd64
			ASSERT(p->p_model == DATAMODEL_LP64 || rex == 0);

			/*
			 * On amd64 we have to be careful not to confuse a nop
			 * (actually xchgl %eax, %eax) with an instruction using
			 * the same opcode, but that does something different
			 * (e.g. xchgl %r8d, %eax or xcghq %r8, %rax).
			 */
			if (FASTTRAP_REX_B(rex) == 0)
#endif
				tp->ftt_type = FASTTRAP_T_NOP;
			break;

		case FASTTRAP_INT3:
			/*
			 * The pid provider shares the int3 trap with debugger
			 * breakpoints so we can't instrument them.
			 */
			ASSERT(instr[start] == FASTTRAP_INSTR);
			return (-1);

		case FASTTRAP_INT:
			/*
			 * Interrupts seem like they could be traced with
			 * no negative implications, but it's possible that
			 * a thread could be redirected by the trap handling
			 * code which would eventually return to the
			 * instruction after the interrupt. If the interrupt
			 * were in our scratch space, the subsequent
			 * instruction might be overwritten before we return.
			 * Accordingly we refuse to instrument any interrupt.
			 */
			return (-1);
		}
	}

#ifdef __amd64
	if (p->p_model == DATAMODEL_LP64 && tp->ftt_type == FASTTRAP_T_COMMON) {
		/*
		 * If the process is 64-bit and the instruction type is still
		 * FASTTRAP_T_COMMON -- meaning we're going to copy it out an
		 * execute it -- we need to watch for %rip-relative
		 * addressing mode. See the portion of fasttrap_pid_probe()
		 * below where we handle tracepoints with type
		 * FASTTRAP_T_COMMON for how we emulate instructions that
		 * employ %rip-relative addressing.
		 */
		if (rmindex != -1) {
			uint_t mod = FASTTRAP_MODRM_MOD(instr[rmindex]);
			uint_t reg = FASTTRAP_MODRM_REG(instr[rmindex]);
			uint_t rm = FASTTRAP_MODRM_RM(instr[rmindex]);

			ASSERT(rmindex > start);

			if (mod == 0 && rm == 5) {
				/*
				 * We need to be sure to avoid other
				 * registers used by this instruction. While
				 * the reg field may determine the op code
				 * rather than denoting a register, assuming
				 * that it denotes a register is always safe.
				 * We leave the REX field intact and use
				 * whatever value's there for simplicity.
				 */
				if (reg != 0) {
					tp->ftt_ripmode = FASTTRAP_RIP_1 |
					    (FASTTRAP_RIP_X *
					    FASTTRAP_REX_B(rex));
					rm = 0;
				} else {
					tp->ftt_ripmode = FASTTRAP_RIP_2 |
					    (FASTTRAP_RIP_X *
					    FASTTRAP_REX_B(rex));
					rm = 1;
				}

				tp->ftt_modrm = tp->ftt_instr[rmindex];
				tp->ftt_instr[rmindex] =
				    FASTTRAP_MODRM(2, reg, rm);
			}
		}
	}
#endif

	return (0);
}

int
fasttrap_tracepoint_install(proc_t *p, fasttrap_tracepoint_t *tp)
{
	fasttrap_instr_t instr = FASTTRAP_INSTR;

	if (uwrite(p, &instr, 1, tp->ftt_pc) != 0)
		return (-1);

	return (0);
}

int
fasttrap_tracepoint_remove(proc_t *p, fasttrap_tracepoint_t *tp)
{
	uint8_t instr;

	/*
	 * Distinguish between read or write failures and a changed
	 * instruction.
	 */
	if (uread(p, &instr, 1, tp->ftt_pc) != 0)
		return (0);
	if (instr != FASTTRAP_INSTR)
		return (0);
	if (uwrite(p, &tp->ftt_instr[0], 1, tp->ftt_pc) != 0)
		return (-1);

	return (0);
}

#ifdef __amd64
static uintptr_t
fasttrap_fulword_noerr(const void *uaddr)
{
	uintptr_t ret;

	if ((ret = fasttrap_fulword(uaddr)) != -1)
		return (ret);

	return (0);
}
#endif

static uint32_t
fasttrap_fuword32_noerr(const void *uaddr)
{
	uint32_t ret;

	if ((ret = fasttrap_fuword32(uaddr)) != -1)
		return (ret);

	return (0);
}

static void
fasttrap_return_common(struct reg *rp, uintptr_t pc, pid_t pid,
    uintptr_t new_pc)
{
	fasttrap_tracepoint_t *tp;
	fasttrap_bucket_t *bucket;
	fasttrap_id_t *id;
	struct rm_priotracker tracker;

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
		if (tp->ftt_type != FASTTRAP_T_RET &&
		    tp->ftt_type != FASTTRAP_T_RET16 &&
		    new_pc - id->fti_probe->ftp_faddr <
		    id->fti_probe->ftp_fsize)
			continue;

		dtrace_probe(id->fti_probe->ftp_id,
		    pc - id->fti_probe->ftp_faddr,
		    rp->r_rax, rp->r_rbx, 0, 0);
	}

	rm_runlock(&fasttrap_tp_lock, &tracker);
}

static void
fasttrap_sigsegv(proc_t *p, kthread_t *t, uintptr_t addr)
{
	ksiginfo_t ksi;

	ksiginfo_init(&ksi);
	ksi.ksi_signo = SIGSEGV;
	ksi.ksi_code = SEGV_MAPERR;
	ksi.ksi_addr = (caddr_t)addr;
	PROC_LOCK(p);
	(void)tdksignal(t, SIGSEGV, &ksi);
	PROC_UNLOCK(p);
}

#ifdef __amd64
static void
fasttrap_usdt_args64(fasttrap_probe_t *probe, struct reg *rp, int argc,
    uintptr_t *argv)
{
	int i, x, cap = MIN(argc, probe->ftp_nargs);
	uintptr_t *stack = (uintptr_t *)rp->r_rsp;

	for (i = 0; i < cap; i++) {
		x = probe->ftp_argmap[i];

		if (x < 6)
			argv[i] = (&rp->r_rdi)[x];
		else
			argv[i] = fasttrap_fulword_noerr(&stack[x]);
	}

	for (; i < argc; i++) {
		argv[i] = 0;
	}
}
#endif

static void
fasttrap_usdt_args32(fasttrap_probe_t *probe, struct reg *rp, int argc,
    uint32_t *argv)
{
	int i, x, cap = MIN(argc, probe->ftp_nargs);
	uint32_t *stack = (uint32_t *)rp->r_rsp;

	for (i = 0; i < cap; i++) {
		x = probe->ftp_argmap[i];

		argv[i] = fasttrap_fuword32_noerr(&stack[x]);
	}

	for (; i < argc; i++) {
		argv[i] = 0;
	}
}

static int
fasttrap_do_seg(fasttrap_tracepoint_t *tp, struct reg *rp, uintptr_t *addr)
{
	proc_t *p = curproc;
#ifdef __i386__
	struct segment_descriptor *desc;
#else
	struct user_segment_descriptor *desc;
#endif
	uint16_t sel = 0, ndx, type;
	uintptr_t limit;

	switch (tp->ftt_segment) {
	case FASTTRAP_SEG_CS:
		sel = rp->r_cs;
		break;
	case FASTTRAP_SEG_DS:
		sel = rp->r_ds;
		break;
	case FASTTRAP_SEG_ES:
		sel = rp->r_es;
		break;
	case FASTTRAP_SEG_FS:
		sel = rp->r_fs;
		break;
	case FASTTRAP_SEG_GS:
		sel = rp->r_gs;
		break;
	case FASTTRAP_SEG_SS:
		sel = rp->r_ss;
		break;
	}

	/*
	 * Make sure the given segment register specifies a user priority
	 * selector rather than a kernel selector.
	 */
	if (ISPL(sel) != SEL_UPL)
		return (-1);

	ndx = IDXSEL(sel);

	/*
	 * Check the bounds and grab the descriptor out of the specified
	 * descriptor table.
	 */
	if (ISLDT(sel)) {
#ifdef __i386__
		if (ndx > p->p_md.md_ldt->ldt_len)
			return (-1);

		desc = (struct segment_descriptor *)
		    p->p_md.md_ldt[ndx].ldt_base;
#else
		if (ndx > max_ldt_segment)
			return (-1);

		desc = (struct user_segment_descriptor *)
		    p->p_md.md_ldt[ndx].ldt_base;
#endif

	} else {
		if (ndx >= NGDT)
			return (-1);

#ifdef __i386__
		desc = &gdt[ndx].sd;
#else
		desc = &gdt[ndx];
#endif
	}

	/*
	 * The descriptor must have user privilege level and it must be
	 * present in memory.
	 */
	if (desc->sd_dpl != SEL_UPL || desc->sd_p != 1)
		return (-1);

	type = desc->sd_type;

	/*
	 * If the S bit in the type field is not set, this descriptor can
	 * only be used in system context.
	 */
	if ((type & 0x10) != 0x10)
		return (-1);

	limit = USD_GETLIMIT(desc) * (desc->sd_gran ? PAGESIZE : 1);

	if (tp->ftt_segment == FASTTRAP_SEG_CS) {
		/*
		 * The code/data bit and readable bit must both be set.
		 */
		if ((type & 0xa) != 0xa)
			return (-1);

		if (*addr > limit)
			return (-1);
	} else {
		/*
		 * The code/data bit must be clear.
		 */
		if ((type & 0x8) != 0)
			return (-1);

		/*
		 * If the expand-down bit is clear, we just check the limit as
		 * it would naturally be applied. Otherwise, we need to check
		 * that the address is the range [limit + 1 .. 0xffff] or
		 * [limit + 1 ... 0xffffffff] depending on if the default
		 * operand size bit is set.
		 */
		if ((type & 0x4) == 0) {
			if (*addr > limit)
				return (-1);
		} else if (desc->sd_def32) {
			if (*addr < limit + 1 || 0xffff < *addr)
				return (-1);
		} else {
			if (*addr < limit + 1 || 0xffffffff < *addr)
				return (-1);
		}
	}

	*addr += USD_GETBASE(desc);

	return (0);
}

int
fasttrap_pid_probe(struct trapframe *tf)
{
	struct reg reg, *rp;
	proc_t *p = curproc, *pp;
	struct rm_priotracker tracker;
	uint64_t gen;
	uintptr_t pc;
	uintptr_t new_pc = 0;
	fasttrap_bucket_t *bucket;
	fasttrap_tracepoint_t *tp, tp_local;
	pid_t pid;
	dtrace_icookie_t cookie;
	uint_t is_enabled = 0;

	fill_frame_regs(tf, &reg);
	rp = &reg;

	pc = rp->r_rip - 1;

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
#ifdef __amd64
	curthread->t_dtrace_regv = 0;
#endif

	/*
	 * Treat a child created by a call to vfork(2) as if it were its
	 * parent. We know that there's only one thread of control in such a
	 * process: this one.
	 */
	pp = p;
	sx_slock(&proctree_lock);
	while (pp->p_vmspace == pp->p_pptr->p_vmspace)
		pp = pp->p_pptr;
	pid = pp->p_pid;
	if (pp != p) {
		PROC_LOCK(pp);
		if ((pp->p_flag & P_WEXIT) != 0) {
			/*
			 * This can happen if the child was created with
			 * rfork(2).  Userspace tracing cannot work reliably in
			 * such a scenario, but we can at least try.
			 */
			PROC_UNLOCK(pp);
			sx_sunlock(&proctree_lock);
			return (-1);
		}
		_PHOLD_LITE(pp);
		PROC_UNLOCK(pp);
	}
	sx_sunlock(&proctree_lock);

	rm_rlock(&fasttrap_tp_lock, &tracker);

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
		gen = atomic_load_acq_64(&pp->p_fasttrap_tp_gen);
		if (pp != p)
			PRELE(pp);
		if (curthread->t_fasttrap_tp_gen != gen) {
			/*
			 * At least one tracepoint associated with this PID has
			 * been removed from the table since #BP was raised.
			 * Speculate that we hit a tracepoint that has since
			 * been removed, and retry the instruction.
			 */
			curthread->t_fasttrap_tp_gen = gen;
#ifdef __amd64
			tf->tf_rip = pc;
#else
			tf->tf_eip = pc;
#endif
			return (0);
		}
		return (-1);
	}
	if (pp != p)
		PRELE(pp);

	/*
	 * Set the program counter to the address of the traced instruction
	 * so that it looks right in ustack() output.
	 */
	rp->r_rip = pc;

	if (tp->ftt_ids != NULL) {
		fasttrap_id_t *id;

#ifdef __amd64
		if (p->p_model == DATAMODEL_LP64) {
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
					dtrace_probe(probe->ftp_id, rp->r_rdi,
					    rp->r_rsi, rp->r_rdx, rp->r_rcx,
					    rp->r_r8);
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
					dtrace_probe(probe->ftp_id, rp->r_rdi,
					    rp->r_rsi, rp->r_rdx, rp->r_rcx,
					    rp->r_r8);
				} else {
					uintptr_t t[5];

					fasttrap_usdt_args64(probe, rp,
					    sizeof (t) / sizeof (t[0]), t);

					dtrace_probe(probe->ftp_id, t[0], t[1],
					    t[2], t[3], t[4]);
				}
			}
		} else {
#endif
			uintptr_t s0, s1, s2, s3, s4, s5;
			uint32_t *stack = (uint32_t *)rp->r_rsp;

			/*
			 * In 32-bit mode, all arguments are passed on the
			 * stack. If this is a function entry probe, we need
			 * to skip the first entry on the stack as it
			 * represents the return address rather than a
			 * parameter to the function.
			 */
			s0 = fasttrap_fuword32_noerr(&stack[0]);
			s1 = fasttrap_fuword32_noerr(&stack[1]);
			s2 = fasttrap_fuword32_noerr(&stack[2]);
			s3 = fasttrap_fuword32_noerr(&stack[3]);
			s4 = fasttrap_fuword32_noerr(&stack[4]);
			s5 = fasttrap_fuword32_noerr(&stack[5]);

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
					dtrace_probe(probe->ftp_id, s1, s2,
					    s3, s4, s5);
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
					dtrace_probe(probe->ftp_id, s0, s1,
					    s2, s3, s4);
				} else {
					uint32_t t[5];

					fasttrap_usdt_args32(probe, rp,
					    sizeof (t) / sizeof (t[0]), t);

					dtrace_probe(probe->ftp_id, t[0], t[1],
					    t[2], t[3], t[4]);
				}
			}
#ifdef __amd64
		}
#endif
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
	 * Set the program counter to appear as though the traced instruction
	 * had completely executed. This ensures that fasttrap_getreg() will
	 * report the expected value for REG_RIP.
	 */
	rp->r_rip = pc + tp->ftt_size;

	/*
	 * If there's an is-enabled probe connected to this tracepoint it
	 * means that there was a 'xorl %eax, %eax' or 'xorq %rax, %rax'
	 * instruction that was placed there by DTrace when the binary was
	 * linked. As this probe is, in fact, enabled, we need to stuff 1
	 * into %eax or %rax. Accordingly, we can bypass all the instruction
	 * emulation logic since we know the inevitable result. It's possible
	 * that a user could construct a scenario where the 'is-enabled'
	 * probe was on some other instruction, but that would be a rather
	 * exotic way to shoot oneself in the foot.
	 */
	if (is_enabled) {
		rp->r_rax = 1;
		new_pc = rp->r_rip;
		goto done;
	}

	/*
	 * We emulate certain types of instructions to ensure correctness
	 * (in the case of position dependent instructions) or optimize
	 * common cases. The rest we have the thread execute back in user-
	 * land.
	 */
	switch (tp->ftt_type) {
	case FASTTRAP_T_RET:
	case FASTTRAP_T_RET16:
	{
		uintptr_t dst = 0;
		uintptr_t addr = 0;
		int ret = 0;

		/*
		 * We have to emulate _every_ facet of the behavior of a ret
		 * instruction including what happens if the load from %esp
		 * fails; in that case, we send a SIGSEGV.
		 */
#ifdef __amd64
		if (p->p_model == DATAMODEL_NATIVE) {
			ret = dst = fasttrap_fulword((void *)rp->r_rsp);
			addr = rp->r_rsp + sizeof (uintptr_t);
		} else {
#endif
			uint32_t dst32;
			ret = dst32 = fasttrap_fuword32((void *)rp->r_rsp);
			dst = dst32;
			addr = rp->r_rsp + sizeof (uint32_t);
#ifdef __amd64
		}
#endif

		if (ret == -1) {
			fasttrap_sigsegv(p, curthread, rp->r_rsp);
			new_pc = pc;
			break;
		}

		if (tp->ftt_type == FASTTRAP_T_RET16)
			addr += tp->ftt_dest;

		rp->r_rsp = addr;
		new_pc = dst;
		break;
	}

	case FASTTRAP_T_JCC:
	{
		uint_t taken = 0;

		switch (tp->ftt_code) {
		case FASTTRAP_JO:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_OF) != 0;
			break;
		case FASTTRAP_JNO:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_OF) == 0;
			break;
		case FASTTRAP_JB:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_CF) != 0;
			break;
		case FASTTRAP_JAE:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_CF) == 0;
			break;
		case FASTTRAP_JE:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_ZF) != 0;
			break;
		case FASTTRAP_JNE:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_ZF) == 0;
			break;
		case FASTTRAP_JBE:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_CF) != 0 ||
			    (rp->r_rflags & FASTTRAP_EFLAGS_ZF) != 0;
			break;
		case FASTTRAP_JA:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_CF) == 0 &&
			    (rp->r_rflags & FASTTRAP_EFLAGS_ZF) == 0;
			break;
		case FASTTRAP_JS:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_SF) != 0;
			break;
		case FASTTRAP_JNS:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_SF) == 0;
			break;
		case FASTTRAP_JP:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_PF) != 0;
			break;
		case FASTTRAP_JNP:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_PF) == 0;
			break;
		case FASTTRAP_JL:
			taken = ((rp->r_rflags & FASTTRAP_EFLAGS_SF) == 0) !=
			    ((rp->r_rflags & FASTTRAP_EFLAGS_OF) == 0);
			break;
		case FASTTRAP_JGE:
			taken = ((rp->r_rflags & FASTTRAP_EFLAGS_SF) == 0) ==
			    ((rp->r_rflags & FASTTRAP_EFLAGS_OF) == 0);
			break;
		case FASTTRAP_JLE:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_ZF) != 0 ||
			    ((rp->r_rflags & FASTTRAP_EFLAGS_SF) == 0) !=
			    ((rp->r_rflags & FASTTRAP_EFLAGS_OF) == 0);
			break;
		case FASTTRAP_JG:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_ZF) == 0 &&
			    ((rp->r_rflags & FASTTRAP_EFLAGS_SF) == 0) ==
			    ((rp->r_rflags & FASTTRAP_EFLAGS_OF) == 0);
			break;

		}

		if (taken)
			new_pc = tp->ftt_dest;
		else
			new_pc = pc + tp->ftt_size;
		break;
	}

	case FASTTRAP_T_LOOP:
	{
		uint_t taken = 0;
#ifdef __amd64
		greg_t cx = rp->r_rcx--;
#else
		greg_t cx = rp->r_ecx--;
#endif

		switch (tp->ftt_code) {
		case FASTTRAP_LOOPNZ:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_ZF) == 0 &&
			    cx != 0;
			break;
		case FASTTRAP_LOOPZ:
			taken = (rp->r_rflags & FASTTRAP_EFLAGS_ZF) != 0 &&
			    cx != 0;
			break;
		case FASTTRAP_LOOP:
			taken = (cx != 0);
			break;
		}

		if (taken)
			new_pc = tp->ftt_dest;
		else
			new_pc = pc + tp->ftt_size;
		break;
	}

	case FASTTRAP_T_JCXZ:
	{
#ifdef __amd64
		greg_t cx = rp->r_rcx;
#else
		greg_t cx = rp->r_ecx;
#endif

		if (cx == 0)
			new_pc = tp->ftt_dest;
		else
			new_pc = pc + tp->ftt_size;
		break;
	}

	case FASTTRAP_T_PUSHL_EBP:
	{
		int ret = 0;

#ifdef __amd64
		if (p->p_model == DATAMODEL_NATIVE) {
			rp->r_rsp -= sizeof (uintptr_t);
			ret = fasttrap_sulword((void *)rp->r_rsp, rp->r_rbp);
		} else {
#endif
			rp->r_rsp -= sizeof (uint32_t);
			ret = fasttrap_suword32((void *)rp->r_rsp, rp->r_rbp);
#ifdef __amd64
		}
#endif

		if (ret == -1) {
			fasttrap_sigsegv(p, curthread, rp->r_rsp);
			new_pc = pc;
			break;
		}

		new_pc = pc + tp->ftt_size;
		break;
	}

	case FASTTRAP_T_NOP:
		new_pc = pc + tp->ftt_size;
		break;

	case FASTTRAP_T_JMP:
	case FASTTRAP_T_CALL:
		if (tp->ftt_code == 0) {
			new_pc = tp->ftt_dest;
		} else {
			uintptr_t value, addr = tp->ftt_dest;

			if (tp->ftt_base != FASTTRAP_NOREG)
				addr += fasttrap_getreg(rp, tp->ftt_base);
			if (tp->ftt_index != FASTTRAP_NOREG)
				addr += fasttrap_getreg(rp, tp->ftt_index) <<
				    tp->ftt_scale;

			if (tp->ftt_code == 1) {
				/*
				 * If there's a segment prefix for this
				 * instruction, we'll need to check permissions
				 * and bounds on the given selector, and adjust
				 * the address accordingly.
				 */
				if (tp->ftt_segment != FASTTRAP_SEG_NONE &&
				    fasttrap_do_seg(tp, rp, &addr) != 0) {
					fasttrap_sigsegv(p, curthread, addr);
					new_pc = pc;
					break;
				}

#ifdef __amd64
				if (p->p_model == DATAMODEL_NATIVE) {
#endif
					if ((value = fasttrap_fulword((void *)addr))
					     == -1) {
						fasttrap_sigsegv(p, curthread,
						    addr);
						new_pc = pc;
						break;
					}
					new_pc = value;
#ifdef __amd64
				} else {
					uint32_t value32;
					addr = (uintptr_t)(uint32_t)addr;
					if ((value32 = fasttrap_fuword32((void *)addr))
					    == -1) {
						fasttrap_sigsegv(p, curthread,
						    addr);
						new_pc = pc;
						break;
					}
					new_pc = value32;
				}
#endif
			} else {
				new_pc = addr;
			}
		}

		/*
		 * If this is a call instruction, we need to push the return
		 * address onto the stack. If this fails, we send the process
		 * a SIGSEGV and reset the pc to emulate what would happen if
		 * this instruction weren't traced.
		 */
		if (tp->ftt_type == FASTTRAP_T_CALL) {
			int ret = 0;
			uintptr_t addr = 0, pcps;
#ifdef __amd64
			if (p->p_model == DATAMODEL_NATIVE) {
				addr = rp->r_rsp - sizeof (uintptr_t);
				pcps = pc + tp->ftt_size;
				ret = fasttrap_sulword((void *)addr, pcps);
			} else {
#endif
				addr = rp->r_rsp - sizeof (uint32_t);
				pcps = (uint32_t)(pc + tp->ftt_size);
				ret = fasttrap_suword32((void *)addr, pcps);
#ifdef __amd64
			}
#endif

			if (ret == -1) {
				fasttrap_sigsegv(p, curthread, addr);
				new_pc = pc;
				break;
			}

			rp->r_rsp = addr;
		}

		break;

	case FASTTRAP_T_COMMON:
	{
		uintptr_t addr;
#if defined(__amd64)
		uint8_t scratch[2 * FASTTRAP_MAX_INSTR_SIZE + 22];
#else
		uint8_t scratch[2 * FASTTRAP_MAX_INSTR_SIZE + 7];
#endif
		uint_t i = 0;
		fasttrap_scrspace_t *scrspace;
		scrspace = fasttrap_scraddr(curthread, tp->ftt_proc);
		if (scrspace == NULL) {
			/*
			 * We failed to allocate scratch space for this thread.
			 * Try to write the original instruction back out and
			 * reset the pc.
			 */
			if (fasttrap_copyout(tp->ftt_instr, (void *)pc,
			    tp->ftt_size))
				fasttrap_sigtrap(p, curthread, pc);
			new_pc = pc;
			break;
		}
		addr = scrspace->ftss_addr;

		/*
		 * Generic Instruction Tracing
		 * ---------------------------
		 *
		 * This is the layout of the scratch space in the user-land
		 * thread structure for our generated instructions.
		 *
		 *	32-bit mode			bytes
		 *	------------------------	-----
		 * a:	<original instruction>		<= 15
		 *	jmp	<pc + tp->ftt_size>	    5
		 * b:	<original instruction>		<= 15
		 *	int	T_DTRACE_RET		    2
		 *					-----
		 *					<= 37
		 *
		 *	64-bit mode			bytes
		 *	------------------------	-----
		 * a:	<original instruction>		<= 15
		 *	jmp	0(%rip)			    6
		 *	<pc + tp->ftt_size>		    8
		 * b:	<original instruction>		<= 15
		 * 	int	T_DTRACE_RET		    2
		 * 					-----
		 * 					<= 46
		 *
		 * The %pc is set to a, and curthread->t_dtrace_astpc is set
		 * to b. If we encounter a signal on the way out of the
		 * kernel, trap() will set %pc to curthread->t_dtrace_astpc
		 * so that we execute the original instruction and re-enter
		 * the kernel rather than redirecting to the next instruction.
		 *
		 * If there are return probes (so we know that we're going to
		 * need to reenter the kernel after executing the original
		 * instruction), the scratch space will just contain the
		 * original instruction followed by an interrupt -- the same
		 * data as at b.
		 *
		 * %rip-relative Addressing
		 * ------------------------
		 *
		 * There's a further complication in 64-bit mode due to %rip-
		 * relative addressing. While this is clearly a beneficial
		 * architectural decision for position independent code, it's
		 * hard not to see it as a personal attack against the pid
		 * provider since before there was a relatively small set of
		 * instructions to emulate; with %rip-relative addressing,
		 * almost every instruction can potentially depend on the
		 * address at which it's executed. Rather than emulating
		 * the broad spectrum of instructions that can now be
		 * position dependent, we emulate jumps and others as in
		 * 32-bit mode, and take a different tack for instructions
		 * using %rip-relative addressing.
		 *
		 * For every instruction that uses the ModRM byte, the
		 * in-kernel disassembler reports its location. We use the
		 * ModRM byte to identify that an instruction uses
		 * %rip-relative addressing and to see what other registers
		 * the instruction uses. To emulate those instructions,
		 * we modify the instruction to be %rax-relative rather than
		 * %rip-relative (or %rcx-relative if the instruction uses
		 * %rax; or %r8- or %r9-relative if the REX.B is present so
		 * we don't have to rewrite the REX prefix). We then load
		 * the value that %rip would have been into the scratch
		 * register and generate an instruction to reset the scratch
		 * register back to its original value. The instruction
		 * sequence looks like this:
		 *
		 *	64-mode %rip-relative		bytes
		 *	------------------------	-----
		 * a:	<modified instruction>		<= 15
		 *	movq	$<value>, %<scratch>	    6
		 *	jmp	0(%rip)			    6
		 *	<pc + tp->ftt_size>		    8
		 * b:	<modified instruction>  	<= 15
		 * 	int	T_DTRACE_RET		    2
		 * 					-----
		 *					   52
		 *
		 * We set curthread->t_dtrace_regv so that upon receiving
		 * a signal we can reset the value of the scratch register.
		 */

		ASSERT(tp->ftt_size <= FASTTRAP_MAX_INSTR_SIZE);

		curthread->t_dtrace_scrpc = addr;
		bcopy(tp->ftt_instr, &scratch[i], tp->ftt_size);
		i += tp->ftt_size;

#ifdef __amd64
		if (tp->ftt_ripmode != 0) {
			greg_t *reg = NULL;

			ASSERT(p->p_model == DATAMODEL_LP64);
			ASSERT(tp->ftt_ripmode &
			    (FASTTRAP_RIP_1 | FASTTRAP_RIP_2));

			/*
			 * If this was a %rip-relative instruction, we change
			 * it to be either a %rax- or %rcx-relative
			 * instruction (depending on whether those registers
			 * are used as another operand; or %r8- or %r9-
			 * relative depending on the value of REX.B). We then
			 * set that register and generate a movq instruction
			 * to reset the value.
			 */
			if (tp->ftt_ripmode & FASTTRAP_RIP_X)
				scratch[i++] = FASTTRAP_REX(1, 0, 0, 1);
			else
				scratch[i++] = FASTTRAP_REX(1, 0, 0, 0);

			if (tp->ftt_ripmode & FASTTRAP_RIP_1)
				scratch[i++] = FASTTRAP_MOV_EAX;
			else
				scratch[i++] = FASTTRAP_MOV_ECX;

			switch (tp->ftt_ripmode) {
			case FASTTRAP_RIP_1:
				reg = &rp->r_rax;
				curthread->t_dtrace_reg = REG_RAX;
				break;
			case FASTTRAP_RIP_2:
				reg = &rp->r_rcx;
				curthread->t_dtrace_reg = REG_RCX;
				break;
			case FASTTRAP_RIP_1 | FASTTRAP_RIP_X:
				reg = &rp->r_r8;
				curthread->t_dtrace_reg = REG_R8;
				break;
			case FASTTRAP_RIP_2 | FASTTRAP_RIP_X:
				reg = &rp->r_r9;
				curthread->t_dtrace_reg = REG_R9;
				break;
			}

			/* LINTED - alignment */
			*(uint64_t *)&scratch[i] = *reg;
			curthread->t_dtrace_regv = *reg;
			*reg = pc + tp->ftt_size;
			i += sizeof (uint64_t);
		}
#endif

		/*
		 * Generate the branch instruction to what would have
		 * normally been the subsequent instruction. In 32-bit mode,
		 * this is just a relative branch; in 64-bit mode this is a
		 * %rip-relative branch that loads the 64-bit pc value
		 * immediately after the jmp instruction.
		 */
#ifdef __amd64
		if (p->p_model == DATAMODEL_LP64) {
			scratch[i++] = FASTTRAP_GROUP5_OP;
			scratch[i++] = FASTTRAP_MODRM(0, 4, 5);
			/* LINTED - alignment */
			*(uint32_t *)&scratch[i] = 0;
			i += sizeof (uint32_t);
			/* LINTED - alignment */
			*(uint64_t *)&scratch[i] = pc + tp->ftt_size;
			i += sizeof (uint64_t);
		} else {
#endif
			/*
			 * Set up the jmp to the next instruction; note that
			 * the size of the traced instruction cancels out.
			 */
			scratch[i++] = FASTTRAP_JMP32;
			/* LINTED - alignment */
			*(uint32_t *)&scratch[i] = pc - addr - 5;
			i += sizeof (uint32_t);
#ifdef __amd64
		}
#endif

		curthread->t_dtrace_astpc = addr + i;
		bcopy(tp->ftt_instr, &scratch[i], tp->ftt_size);
		i += tp->ftt_size;
		scratch[i++] = FASTTRAP_INT;
		scratch[i++] = T_DTRACE_RET;

		ASSERT(i <= sizeof (scratch));

		if (fasttrap_copyout(scratch, (char *)addr, i)) {
			fasttrap_sigtrap(p, curthread, pc);
			new_pc = pc;
			break;
		}
		if (tp->ftt_retids != NULL) {
			curthread->t_dtrace_step = 1;
			curthread->t_dtrace_ret = 1;
			new_pc = curthread->t_dtrace_astpc;
		} else {
			new_pc = curthread->t_dtrace_scrpc;
		}

		curthread->t_dtrace_pc = pc;
		curthread->t_dtrace_npc = pc + tp->ftt_size;
		curthread->t_dtrace_on = 1;
		break;
	}

	default:
		panic("fasttrap: mishandled an instruction");
	}

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
			/*
			 * Set the program counter to the address of the traced
			 * instruction so that it looks right in ustack()
			 * output. We had previously set it to the end of the
			 * instruction to simplify %rip-relative addressing.
			 */
			rp->r_rip = pc;

			fasttrap_return_common(rp, pc, pid, new_pc);
		} else {
			ASSERT(curthread->t_dtrace_ret != 0);
			ASSERT(curthread->t_dtrace_pc == pc);
			ASSERT(curthread->t_dtrace_scrpc != 0);
			ASSERT(new_pc == curthread->t_dtrace_astpc);
		}
	}

	rp->r_rip = new_pc;

	PROC_LOCK(p);
	proc_write_regs(curthread, rp);
	PROC_UNLOCK(p);

	return (0);
}

int
fasttrap_return_probe(struct trapframe *tf)
{
	struct reg reg, *rp;
	proc_t *p = curproc;
	uintptr_t pc = curthread->t_dtrace_pc;
	uintptr_t npc = curthread->t_dtrace_npc;

	fill_frame_regs(tf, &reg);
	rp = &reg;

	curthread->t_dtrace_pc = 0;
	curthread->t_dtrace_npc = 0;
	curthread->t_dtrace_scrpc = 0;
	curthread->t_dtrace_astpc = 0;

#ifdef illumos
	/*
	 * Treat a child created by a call to vfork(2) as if it were its
	 * parent. We know that there's only one thread of control in such a
	 * process: this one.
	 */
	while (p->p_flag & SVFORK) {
		p = p->p_parent;
	}
#endif

	/*
	 * We set rp->r_rip to the address of the traced instruction so
	 * that it appears to dtrace_probe() that we're on the original
	 * instruction.
	 */
	rp->r_rip = pc;

	fasttrap_return_common(rp, pc, p->p_pid, npc);

	return (0);
}

/*ARGSUSED*/
uint64_t
fasttrap_pid_getarg(void *arg, dtrace_id_t id, void *parg, int argno,
    int aframes)
{
	struct reg r;

	fill_regs(curthread, &r);

	return (fasttrap_anarg(&r, 1, argno));
}

/*ARGSUSED*/
uint64_t
fasttrap_usdt_getarg(void *arg, dtrace_id_t id, void *parg, int argno,
    int aframes)
{
	struct reg r;

	fill_regs(curthread, &r);

	return (fasttrap_anarg(&r, 0, argno));
}

static ulong_t
fasttrap_getreg(struct reg *rp, uint_t reg)
{
#ifdef __amd64
	switch (reg) {
	case REG_R15:		return (rp->r_r15);
	case REG_R14:		return (rp->r_r14);
	case REG_R13:		return (rp->r_r13);
	case REG_R12:		return (rp->r_r12);
	case REG_R11:		return (rp->r_r11);
	case REG_R10:		return (rp->r_r10);
	case REG_R9:		return (rp->r_r9);
	case REG_R8:		return (rp->r_r8);
	case REG_RDI:		return (rp->r_rdi);
	case REG_RSI:		return (rp->r_rsi);
	case REG_RBP:		return (rp->r_rbp);
	case REG_RBX:		return (rp->r_rbx);
	case REG_RDX:		return (rp->r_rdx);
	case REG_RCX:		return (rp->r_rcx);
	case REG_RAX:		return (rp->r_rax);
	case REG_TRAPNO:	return (rp->r_trapno);
	case REG_ERR:		return (rp->r_err);
	case REG_RIP:		return (rp->r_rip);
	case REG_CS:		return (rp->r_cs);
	case REG_RFL:		return (rp->r_rflags);
	case REG_RSP:		return (rp->r_rsp);
	case REG_SS:		return (rp->r_ss);
	case REG_FS:		return (rp->r_fs);
	case REG_GS:		return (rp->r_gs);
	case REG_DS:		return (rp->r_ds);
	case REG_ES:		return (rp->r_es);
	case REG_FSBASE:	return (rdmsr(MSR_FSBASE));
	case REG_GSBASE:	return (rdmsr(MSR_GSBASE));
	}

	panic("dtrace: illegal register constant");
	/*NOTREACHED*/
#else
#define _NGREG 19
	if (reg >= _NGREG)
		panic("dtrace: illegal register constant");

	return (((greg_t *)&rp->r_gs)[reg]);
#endif
}
