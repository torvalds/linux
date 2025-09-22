/*	$OpenBSD: fp_emulate.c,v 1.25 2023/01/11 03:19:52 visa Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Floating Point completion/emulation code (MI softfloat code control engine).
 *
 * Supports all MIPS IV COP1 and COP1X floating-point instructions.
 *
 * Floating-point load and store instructions, as well as branch instructions,
 * are only handled if the kernel is compiled with option FPUEMUL.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>

#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/fpu.h>
#include <machine/frame.h>
#include <machine/ieee.h>
#include <machine/ieeefp.h>
#include <machine/mips_opcode.h>
#include <machine/regnum.h>

#include <lib/libkern/softfloat.h>
#if defined(DEBUG) && defined(DDB)
#include <machine/db_machdep.h>
#endif

int	fpu_emulate(struct proc *, struct trapframe *, uint32_t,
	    union sigval *);
int	fpu_emulate_cop1(struct proc *, struct trapframe *, uint32_t);
int	fpu_emulate_cop1x(struct proc *, struct trapframe *, uint32_t);
uint64_t
	fpu_load(struct proc *, struct trapframe *, uint, uint);
void	fpu_store(struct proc *, struct trapframe *, uint, uint, uint64_t);
#ifdef FPUEMUL
int	nofpu_emulate_cop1(struct proc *, struct trapframe *, uint32_t,
	    union sigval *);
int	nofpu_emulate_cop1x(struct proc *, struct trapframe *, uint32_t,
	    union sigval *);
int	nofpu_emulate_loadstore(struct proc *, struct trapframe *, uint32_t,
	    union sigval *);
int	nofpu_emulate_movci(struct trapframe *, uint32_t);
#endif

typedef	int (fpu_fn3)(struct proc *, struct trapframe *, uint, uint, uint,
	    uint);
typedef	int (fpu_fn4)(struct proc *, struct trapframe *, uint, uint, uint,
	    uint, uint);
fpu_fn3	fpu_abs;
fpu_fn3	fpu_add;
int	fpu_c(struct proc *, struct trapframe *, uint, uint, uint, uint, uint);
fpu_fn3	fpu_ceil_l;
fpu_fn3	fpu_ceil_w;
fpu_fn3	fpu_cvt_d;
fpu_fn3	fpu_cvt_l;
fpu_fn3	fpu_cvt_s;
fpu_fn3	fpu_cvt_w;
fpu_fn3	fpu_div;
fpu_fn3	fpu_floor_l;
fpu_fn3	fpu_floor_w;
int	fpu_int_l(struct proc *, struct trapframe *, uint, uint, uint, uint,
	    uint);
int	fpu_int_w(struct proc *, struct trapframe *, uint, uint, uint, uint,
	    uint);
fpu_fn4	fpu_madd;
fpu_fn4	fpu_msub;
fpu_fn3	fpu_mov;
fpu_fn3	fpu_movcf;
fpu_fn3	fpu_movn;
fpu_fn3	fpu_movz;
fpu_fn3	fpu_mul;
fpu_fn3	fpu_neg;
fpu_fn4	fpu_nmadd;
fpu_fn4	fpu_nmsub;
fpu_fn3	fpu_recip;
fpu_fn3	fpu_round_l;
fpu_fn3	fpu_round_w;
fpu_fn3	fpu_rsqrt;
fpu_fn3	fpu_sqrt;
fpu_fn3	fpu_sub;
fpu_fn3	fpu_trunc_l;
fpu_fn3	fpu_trunc_w;

/*
 * Encoding of operand format within opcodes `fmt' and `fmt3' fields.
 */
#define	FMT_S	0x00
#define	FMT_D	0x01
#define	FMT_W	0x04
#define	FMT_L	0x05

/*
 * Inlines from softfloat-specialize.h which are not made public, needed
 * for fpu_abs.
 */
#define	float32_is_nan(a) \
	(0xff000000 < (a << 1))
#define	float32_is_signaling_nan(a) \
	((((a >> 22) & 0x1ff) == 0x1fe) && (a & 0x003fffff))

/*
 * Precomputed results of intXX_to_floatXX(1)
 */
#define	ONE_F32	(float32)(SNG_EXP_BIAS << SNG_FRACBITS)
#define	ONE_F64	(float64)((uint64_t)DBL_EXP_BIAS << DBL_FRACBITS)

static inline uint32_t
getfsr(void)
{
	uint32_t fsr;

	__asm__ volatile (
	"	.set	push\n"
	"	.set	hardfloat\n"
	"	cfc1	%0, $31\n"	/* stall until FPU done */
	"	cfc1	%0, $31\n"	/* now get status */
	"	.set	pop\n"
	: "=r" (fsr));
	return fsr;
}

static inline void
setfsr(uint32_t fsr)
{
	__asm__ volatile (
	"	.set	push\n"
	"	.set	hardfloat\n"
	"	ctc1	%0, $31\n"
	"	.set	pop\n"
	: : "r" (fsr));
}

/*
 * Handle a floating-point exception.
 */
void
MipsFPTrap(struct trapframe *tf)
{
	struct cpu_info *ci = curcpu();
	struct proc *p = ci->ci_curproc;
	union sigval sv;
	vaddr_t pc;
	register_t sr;
	uint32_t fsr, excbits;
	uint32_t branch = 0;
	uint32_t insn;
	InstFmt inst;
	int sig = 0;
	int fault_type = SI_NOINFO;
	int update_pcb = 0;
	int emulate = 0;
	int skip_insn = 1;

	KDASSERT(tf == p->p_md.md_regs);

	pc = (vaddr_t)tf->pc;
	if (tf->cause & CR_BR_DELAY)
		pc += 4;

	if (CPU_HAS_FPU(ci)) {
		/*
		 * Enable FPU, and read its status register.
		 */

		sr = getsr();
		setsr(sr | SR_COP_1_BIT);
		fsr = getfsr();

		/*
		 * If this is not an unimplemented operation, but a genuine
		 * FPU exception, signal the process.
		 */

		if ((fsr & FPCSR_C_E) == 0) {
			sig = SIGFPE;
			goto deliver;
		}
	} else {
#ifdef CPU_OCTEON
		/*
		 * SR_FR_32 is hardwired to zero on Octeon; make sure it is
		 * set in the emulation view of the FPU state.
		 */
		tf->sr |= SR_FR_32;
#endif
		fsr = tf->fsr;
	}

	/*
	 * Get the faulting instruction.  This should not fail, and
	 * if it does, it's probably not your lucky day.
	 */

	if (copyinsn(p, pc, &insn) != 0) {
		sig = SIGBUS;
		fault_type = BUS_OBJERR;
		sv.sival_ptr = (void *)pc;
		goto deliver;
	}
	inst = *(InstFmt *)&insn;

	if (tf->cause & CR_BR_DELAY) {
		if (copyinsn(p, tf->pc, &branch) != 0) {
			sig = SIGBUS;
			fault_type = BUS_OBJERR;
			sv.sival_ptr = (void *)tf->pc;
			goto deliver;
		}
	}

	/*
	 * Emulate the instruction.
	 */

#ifdef DEBUG
#ifdef DDB
	printf("%s: unimplemented FPU completion, fsr 0x%08x\n0x%lx: ",
	    p->p_p->ps_comm, fsr, pc);
	dbmd_print_insn(insn, pc, printf);
#else
	printf("%s: unimplemented FPU completion, insn 0x%08x fsr 0x%08x\n",
	    p->p_p->ps_comm, insn, fsr);
#endif
#endif

	switch (inst.FRType.op) {
	default:
		/*
		 * Not a FPU instruction.
		 */
		break;
#ifdef FPUEMUL
	case OP_SPECIAL:
		switch (inst.FRType.func) {
		default:
			/*
			 * Not a FPU instruction.
			 */
			break;
		case OP_MOVCI:
			/*
			 * This instruction should not require emulation,
			 * unless there is no FPU.
			 */
			if (!CPU_HAS_FPU(ci))
				emulate = 1;
			break;
		}
		break;
	case OP_LDC1:
	case OP_LWC1:
	case OP_SDC1:
	case OP_SWC1:
		/*
		 * These instructions should not require emulation,
		 * unless there is no FPU.
		 */
		if (!CPU_HAS_FPU(ci))
			emulate = 1;
		break;
#endif
	case OP_COP1:
		switch (inst.RType.rs) {
		case OP_BC:
			skip_insn = 0;
			/* FALLTHROUGH */
		case OP_MF:
		case OP_DMF:
		case OP_CF:
		case OP_MT:
		case OP_DMT:
		case OP_CT:
			/*
			 * These instructions should not require emulation,
			 * unless there is no FPU.
			 */
			if (!CPU_HAS_FPU(ci))
				emulate = 1;
			break;
		default:
			emulate = 1;
			break;
		}
		break;
	case OP_COP1X:
		switch (inst.FQType.op4) {
		default:
			switch (inst.FRType.func) {
#ifdef FPUEMUL
			case OP_LDXC1:
			case OP_LWXC1:
			case OP_SDXC1:
			case OP_SWXC1:
			case OP_PREFX:
				/*
				 * These instructions should not require
				 * emulation, unless there is no FPU.
				 */
				if (!CPU_HAS_FPU(ci))
					emulate = 1;
				break;
#endif
			default:
				/*
				 * Not a valid instruction.
				 */
				break;
			}
			break;
		case OP_MADD:
		case OP_MSUB:
		case OP_NMADD:
		case OP_NMSUB:
			emulate = 1;
			break;
		}
		break;
	}

	if (emulate) {
		if (CPU_HAS_FPU(ci)) {
			KASSERT(p == ci->ci_fpuproc);
			save_fpu();
		}

		update_pcb = 1;

		sig = fpu_emulate(p, tf, insn, &sv);
		/* reload fsr, possibly modified by softfloat code */
		fsr = tf->fsr;
		if (sig == 0) {
			/* raise SIGFPE if necessary */
			excbits = (fsr & FPCSR_C_MASK) >> FPCSR_C_SHIFT;
			excbits &= (fsr & FPCSR_E_MASK) >> FPCSR_E_SHIFT;
			if (excbits != 0)
				sig = SIGFPE;
		}
	} else {
		sig = SIGILL;
		fault_type = ILL_ILLOPC;
	}

deliver:
	switch (sig) {
	case SIGFPE:
		excbits = (fsr & FPCSR_C_MASK) >> FPCSR_C_SHIFT;
		excbits &= (fsr & FPCSR_E_MASK) >> FPCSR_E_SHIFT;
		if (excbits & FP_X_INV)
			fault_type = FPE_FLTINV;
		else if (excbits & FP_X_DZ)
			fault_type = FPE_INTDIV;
		else if (excbits & FP_X_OFL)
			fault_type = FPE_FLTUND;
		else if (excbits & FP_X_UFL)
			fault_type = FPE_FLTOVF;
		else /* if (excbits & FP_X_IMP) */
			fault_type = FPE_FLTRES;

		break;
#ifdef FPUEMUL
	case SIGBUS:
		if (fault_type == SI_NOINFO)
			fault_type = BUS_ADRALN;
		break;
	case SIGSEGV:
		if (fault_type == SI_NOINFO)
			fault_type = SEGV_MAPERR;
		break;
#endif
	}

	/*
	 * Skip the instruction, unless we are delivering SIGILL.
	 */
	if (CPU_HAS_FPU(ci) || skip_insn) {
		if (sig != SIGILL) {
			if (tf->cause & CR_BR_DELAY) {
				/*
				 * Note that it doesn't matter, at this point,
				 * that we pass the updated FSR value, as it is
				 * only used to decide whether to branch or not
				 * if the faulting instruction was BC1[FT].
				 */
				tf->pc = MipsEmulateBranch(tf, tf->pc, fsr,
				    branch);
			} else
				tf->pc += 4;
		}
	}

	/*
	 * Update the FPU status register.
	 * We need to make sure that this will not cause an exception
	 * in kernel mode.
	 */

	/* propagate raised exceptions to the sticky bits */
	fsr &= ~FPCSR_C_E;
	excbits = (fsr & FPCSR_C_MASK) >> FPCSR_C_SHIFT;
	fsr |= excbits << FPCSR_F_SHIFT;
	/* clear all exception sources */
	fsr &= ~FPCSR_C_MASK;
	if (update_pcb)
		tf->fsr = fsr;

	if (CPU_HAS_FPU(ci)) {
		setfsr(fsr);
		/* disable fpu before returning to trap() */
		setsr(sr);
	}

	if (sig != 0) {
		if (sig != SIGBUS && sig != SIGSEGV)
			sv.sival_ptr = (void *)pc;
		trapsignal(p, sig, 0, fault_type, sv);
	}
}

/*
 * Emulate an FPU instruction.  The FPU register set has been saved in the
 * current PCB, and is pointed to by the trap frame.
 */
int
fpu_emulate(struct proc *p, struct trapframe *tf, uint32_t insn,
    union sigval *sv)
{
	InstFmt inst;

	tf->zero = 0;	/* not written by trap code */

	inst = *(InstFmt *)&insn;

	if (CPU_HAS_FPU(p->p_cpu)) {
		switch (inst.FRType.op) {
		default:
			break;
		case OP_COP1:
			return fpu_emulate_cop1(p, tf, insn);
		case OP_COP1X:
			return fpu_emulate_cop1x(p, tf, insn);
		}

		return SIGILL;
	}

#ifdef FPUEMUL
	switch (inst.FRType.op) {
	default:
		break;
	case OP_SPECIAL:
		return nofpu_emulate_movci(tf, insn);
	case OP_LDC1:
	case OP_LWC1:
	case OP_SDC1:
	case OP_SWC1:
		return nofpu_emulate_loadstore(p, tf, insn, sv);
	case OP_COP1:
		switch (inst.RType.rs) {
		case OP_MF:
		case OP_DMF:
		case OP_CF:
		case OP_MT:
		case OP_DMT:
		case OP_CT:
		case OP_BC:
			return nofpu_emulate_cop1(p, tf, insn, sv);
		default:
			return fpu_emulate_cop1(p, tf, insn);
		}
		break;
	case OP_COP1X:
		switch (inst.FQType.op4) {
		default:
			switch (inst.FRType.func) {
			case OP_LDXC1:
			case OP_LWXC1:
			case OP_SDXC1:
			case OP_SWXC1:
			case OP_PREFX:
				return nofpu_emulate_cop1x(p, tf, insn, sv);
			default:
				break;
			}
			break;
		case OP_MADD:
		case OP_MSUB:
		case OP_NMADD:
		case OP_NMSUB:
			return fpu_emulate_cop1x(p, tf, insn);
		}
	}
#endif

	return SIGILL;
}

/*
 * Emulate a COP1 FPU instruction.
 */
int
fpu_emulate_cop1(struct proc *p, struct trapframe *tf, uint32_t insn)
{
	InstFmt inst;
	uint ft, fs, fd;
	fpu_fn3 *fpu_op;
	static fpu_fn3 *const fpu_ops1[1 << 6] = {
		fpu_add,		/* 0x00 */
		fpu_sub,
		fpu_mul,
		fpu_div,
		fpu_sqrt,
		fpu_abs,
		fpu_mov,
		fpu_neg,
		fpu_round_l,		/* 0x08 */
		fpu_trunc_l,
		fpu_ceil_l,
		fpu_floor_l,
		fpu_round_w,
		fpu_trunc_w,
		fpu_ceil_w,
		fpu_floor_w,
		NULL,			/* 0x10 */
		fpu_movcf,
		fpu_movz,
		fpu_movn,
		NULL,
		fpu_recip,
		fpu_rsqrt,
		NULL,
		NULL,			/* 0x18 */
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		fpu_cvt_s,		/* 0x20 */
		fpu_cvt_d,
		NULL,
		NULL,
		fpu_cvt_w,
		fpu_cvt_l,
		NULL,
		NULL,
		NULL,			/* 0x28 */
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		(fpu_fn3 *)fpu_c,	/* 0x30 */
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,	/* 0x38 */
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c,
		(fpu_fn3 *)fpu_c
	};

	inst = *(InstFmt *)&insn;

	/* 
	 * Check for valid function code.
	 */

	fpu_op = fpu_ops1[inst.FRType.func];
	if (fpu_op == NULL)
		return SIGILL;

	/*
	 * Check for valid format.  FRType assumes bit 25 is always set,
	 * so we need to check for it explicitly.
	 */

	if ((insn & (1 << 25)) == 0)
		return SIGILL;
	switch (inst.FRType.fmt) {
	default:
		return SIGILL;
	case FMT_S:
	case FMT_D:
	case FMT_W:
	case FMT_L:
		break;
	}

	/*
	 * Check for valid register values. Only even-numbered registers
	 * can be used if the FR bit is clear in coprocessor 0 status
	 * register.
	 *
	 * Note that c.cond does not specify a register number in the fd
	 * field, but the fd field must have zero in its low two bits, so
	 * the test will not reject valid c.cond instructions.
	 */

	ft = inst.FRType.ft;
	fs = inst.FRType.fs;
	fd = inst.FRType.fd;
	if ((tf->sr & SR_FR_32) == 0) {
		if ((ft | fs | fd) & 1)
			return SIGILL;
	}

	/*
	 * Finally dispatch to the proper routine.
	 */

	if (fpu_op == (fpu_fn3 *)&fpu_c)
		return
		    fpu_c(p, tf, inst.FRType.fmt, ft, fs, fd, inst.FRType.func);
	else
		return (*fpu_op)(p, tf, inst.FRType.fmt, ft, fs, fd);
}

/*
 * Emulate a COP1X FPU instruction.
 */
int
fpu_emulate_cop1x(struct proc *p, struct trapframe *tf, uint32_t insn)
{
	InstFmt inst;
	uint fr, ft, fs, fd;
	fpu_fn4 *fpu_op;
	static fpu_fn4 *const fpu_ops1x[1 << 3] = {
		NULL,
		NULL,
		NULL,
		NULL,
		fpu_madd,
		fpu_msub,
		fpu_nmadd,
		fpu_nmsub
	};

	inst = *(InstFmt *)&insn;

	/* 
	 * Check for valid function code.
	 */

	fpu_op = fpu_ops1x[inst.FQType.op4];
	if (fpu_op == NULL)
		return SIGILL;

	/*
	 * Check for valid format.
	 */

	switch (inst.FQType.fmt3) {
	default:
		return SIGILL;
	case FMT_S:
	case FMT_D:
	case FMT_W:
	case FMT_L:
		break;
	}

	/*
	 * Check for valid register values. Only even-numbered registers
	 * can be used if the FR bit is clear in coprocessor 0 status
	 * register.
	 */

	fr = inst.FQType.fr;
	ft = inst.FQType.ft;
	fs = inst.FQType.fs;
	fd = inst.FQType.fd;
	if ((tf->sr & SR_FR_32) == 0) {
		if ((fr | ft | fs | fd) & 1)
			return SIGILL;
	}

	/*
	 * Finally dispatch to the proper routine.
	 */

	return (*fpu_op)(p, tf, inst.FRType.fmt, fr, ft, fs, fd);
}

/*
 * Load a floating-point argument according to the specified format.
 */
uint64_t
fpu_load(struct proc *p, struct trapframe *tf, uint fmt, uint regno)
{
	uint64_t tmp, tmp2;

	tmp = ((uint64_t *)p->p_md.md_regs)[FPBASE + regno];
	if (tf->sr & SR_FR_32) {
		switch (fmt) {
		case FMT_D:
		case FMT_L:
			break;
		case FMT_S:
		case FMT_W:
			tmp &= 0xffffffff;
			break;
		}
	} else {
		tmp &= 0xffffffff;
		switch (fmt) {
		case FMT_D:
		case FMT_L:
			/* caller has enforced regno is even */
			tmp2 =
			    ((uint64_t *)p->p_md.md_regs)[FPBASE + regno + 1];
			tmp |= tmp2 << 32;
			break;
		case FMT_S:
		case FMT_W:
			break;
		}
	}

	return tmp;
}

/*
 * Store a floating-point result according to the specified format.
 */
void
fpu_store(struct proc *p, struct trapframe *tf, uint fmt, uint regno,
    uint64_t rslt)
{
	if (tf->sr & SR_FR_32) {
		((uint64_t *)p->p_md.md_regs)[FPBASE + regno] = rslt;
	} else {
		/* caller has enforced regno is even */
		((uint64_t *)p->p_md.md_regs)[FPBASE + regno] =
		    rslt & 0xffffffff;
		((uint64_t *)p->p_md.md_regs)[FPBASE + regno + 1] =
		    (rslt >> 32) & 0xffffffff;
	}
}

/*
 * Integer conversion
 */

int
fpu_int_l(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd, uint rm)
{
	uint64_t raw;
	uint32_t oldrm;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);

	/* round towards required mode */
	oldrm = tf->fsr & FPCSR_RM_MASK;
	tf->fsr = (tf->fsr & ~FPCSR_RM_MASK) | rm;
	if (fmt == FMT_S)
		raw = float32_to_int64((float32)raw);
	else
		raw = float64_to_int64((float64)raw);
	/* restore rounding mode */
	tf->fsr = (tf->fsr & ~FPCSR_RM_MASK) | oldrm;

	if ((tf->fsr & (FPCSR_C_V | FPCSR_E_V)) != (FPCSR_C_V | FPCSR_E_V))
		fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_int_w(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd, uint rm)
{
	uint64_t raw;
	uint32_t oldrm;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);

	/* round towards required mode */
	oldrm = tf->fsr & FPCSR_RM_MASK;
	tf->fsr = (tf->fsr & ~FPCSR_RM_MASK) | rm;
	if (fmt == FMT_S)
		raw = float32_to_int32((float32)raw);
	else
		raw = float64_to_int32((float64)raw);
	/* restore rounding mode */
	tf->fsr = (tf->fsr & ~FPCSR_RM_MASK) | oldrm;

	if ((tf->fsr & (FPCSR_C_V | FPCSR_E_V)) != (FPCSR_C_V | FPCSR_E_V))
		fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

/*
 * FPU Instruction emulation
 */

int
fpu_abs(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	/* clear sign bit unless NaN */
	if (fmt == FMT_S) {
		float32 f32 = (float32)raw;
		if (float32_is_nan(f32)) {
			float_set_invalid();
		} else {
			f32 &= ~(1L << 31);
			raw = (uint64_t)f32;
		}
	} else {
		float64 f64 = (float64)raw;
		if (float64_is_nan(f64)) {
			float_set_invalid();
		} else {
			f64 &= ~(1L << 63);
			raw = (uint64_t)f64;
		}
	}
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_add(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw1, raw2, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	if (fmt == FMT_S) {
		float32 f32 = float32_add((float32)raw1, (float32)raw2);
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_add((float64)raw1, (float64)raw2);
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_c(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd, uint op)
{
	uint64_t raw1, raw2;
	uint cc, lt, eq, uo;

	if ((fd & 0x03) != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	lt = eq = uo = 0;
	cc = fd >> 2;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);

	if (fmt == FMT_S) {
		float32 f32a = (float32)raw1;
		float32 f32b = (float32)raw2;
		if (float32_is_nan(f32a)) {
			uo = 1 << 0;
			if (float32_is_signaling_nan(f32a))
				op |= 0x08;	/* force invalid exception */
		}
		if (float32_is_nan(f32b)) {
			uo = 1 << 0;
			if (float32_is_signaling_nan(f32b))
				op |= 0x08;	/* force invalid exception */
		}
		if (uo == 0) {
			if (float32_eq(f32a, f32b))
				eq = 1 << 1;
			else if (float32_lt(f32a, f32b))
				lt = 1 << 2;
		}
	} else {
		float64 f64a = (float64)raw1;
		float64 f64b = (float64)raw2;
		if (float64_is_nan(f64a)) {
			uo = 1 << 0;
			if (float64_is_signaling_nan(f64a))
				op |= 0x08;	/* force invalid exception */
		}
		if (float64_is_nan(f64b)) {
			uo = 1 << 0;
			if (float64_is_signaling_nan(f64b))
				op |= 0x08;	/* force invalid exception */
		}
		if (uo == 0) {
			if (float64_eq(f64a, f64b))
				eq = 1 << 1;
			else if (float64_lt(f64a, f64b))
				lt = 1 << 2;
		}
	}

	if (uo && (op & 0x08)) {
		float_set_invalid();
		if (tf->fsr & FPCSR_E_V) {
			/* comparison result intentionally not written */
			goto skip;
		}
	}

	if ((uo | eq | lt) & op)
		tf->fsr |= FPCSR_CONDVAL(cc);
	else
		tf->fsr &= ~FPCSR_CONDVAL(cc);
skip:

	return 0;
}

int
fpu_ceil_l(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards positive infinity */
	return fpu_int_l(p, tf, fmt, ft, fs, fd, FP_RP);
}

int
fpu_ceil_w(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards positive infinity */
	return fpu_int_w(p, tf, fmt, ft, fs, fd, FP_RP);
}

int
fpu_cvt_d(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt == FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	switch (fmt) {
	case FMT_L:
		raw = int64_to_float64((int64_t)raw);
		break;
	case FMT_S:
		raw = float32_to_float64((float32)raw);
		break;
	case FMT_W:
		raw = int32_to_float64((int32_t)raw);
		break;
	}
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_cvt_l(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;
	uint32_t rm;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	rm = tf->fsr & FPCSR_RM_MASK;
	raw = fpu_load(p, tf, fmt, fs);
	if (fmt == FMT_D) {
		if (rm == FP_RZ)
			raw = float64_to_int64_round_to_zero((float64)raw);
		else
			raw = float64_to_int64((float64)raw);
	} else {
		if (rm == FP_RZ)
			raw = float32_to_int64_round_to_zero((float32)raw);
		else
			raw = float32_to_int64((float32)raw);
	}
	if ((tf->fsr & (FPCSR_C_V | FPCSR_E_V)) != (FPCSR_C_V | FPCSR_E_V))
		fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_cvt_s(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt == FMT_S)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	switch (fmt) {
	case FMT_D:
		raw = float64_to_float32((float64)raw);
		break;
	case FMT_L:
		raw = int64_to_float32((int64_t)raw);
		break;
	case FMT_W:
		raw = int32_to_float32((int32_t)raw);
		break;
	}
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_cvt_w(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;
	uint32_t rm;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	rm = tf->fsr & FPCSR_RM_MASK;
	raw = fpu_load(p, tf, fmt, fs);
	if (fmt == FMT_D) {
		if (rm == FP_RZ)
			raw = float64_to_int32_round_to_zero((float64)raw);
		else
			raw = float64_to_int32((float64)raw);
	} else {
		if (rm == FP_RZ)
			raw = float32_to_int32_round_to_zero((float32)raw);
		else
			raw = float32_to_int32((float32)raw);
	}
	if ((tf->fsr & (FPCSR_C_V | FPCSR_E_V)) != (FPCSR_C_V | FPCSR_E_V))
		fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_div(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw1, raw2, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	if (fmt == FMT_S) {
		float32 f32 = float32_div((float32)raw1, (float32)raw2);
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_div((float64)raw1, (float64)raw2);
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_floor_l(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards negative infinity */
	return fpu_int_l(p, tf, fmt, ft, fs, fd, FP_RM);
}

int
fpu_floor_w(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards negative infinity */
	return fpu_int_w(p, tf, fmt, ft, fs, fd, FP_RM);
}

int
fpu_madd(struct proc *p, struct trapframe *tf, uint fmt, uint fr, uint ft,
    uint fs, uint fd)
{
	uint64_t raw1, raw2, raw3, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	raw3 = fpu_load(p, tf, fmt, fr);
	if (fmt == FMT_S) {
		float32 f32 = float32_add(
		    float32_mul((float32)raw1, (float32)raw2),
		    (float32)raw3);
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_add(
		    float64_mul((float64)raw1, (float64)raw2),
		    (float64)raw3);
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_mov(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_movcf(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;
	uint cc, istf;
	int condition;

	if ((ft & 0x02) != 0)
		return SIGILL;
	cc = ft >> 2;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	condition = tf->fsr & FPCSR_CONDVAL(cc);
	istf = ft & COPz_BC_TF_MASK;
	if ((!condition && !istf) /*movf*/ || (condition && istf) /*movt*/) {
		raw = fpu_load(p, tf, fmt, fs);
		fpu_store(p, tf, fmt, fd, raw);
	}

	return 0;
}

int
fpu_movn(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	register_t *regs = (register_t *)tf;
	uint64_t raw;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	if (ft != ZERO && regs[ft] != 0) {
		raw = fpu_load(p, tf, fmt, fs);
		fpu_store(p, tf, fmt, fd, raw);
	}

	return 0;
}

int
fpu_movz(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	register_t *regs = (register_t *)tf;
	uint64_t raw;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	if (ft == ZERO || regs[ft] == 0) {
		raw = fpu_load(p, tf, fmt, fs);
		fpu_store(p, tf, fmt, fd, raw);
	}

	return 0;
}

int
fpu_msub(struct proc *p, struct trapframe *tf, uint fmt, uint fr, uint ft,
    uint fs, uint fd)
{
	uint64_t raw1, raw2, raw3, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	raw3 = fpu_load(p, tf, fmt, fr);
	if (fmt == FMT_S) {
		float32 f32 = float32_sub(
		    float32_mul((float32)raw1, (float32)raw2),
		    (float32)raw3);
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_sub(
		    float64_mul((float64)raw1, (float64)raw2),
		    (float64)raw3);
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_mul(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw1, raw2, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	if (fmt == FMT_S) {
		float32 f32 = float32_mul((float32)raw1, (float32)raw2);
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_mul((float64)raw1, (float64)raw2);
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_neg(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	/* flip sign bit unless NaN */
	if (fmt == FMT_S) {
		float32 f32 = (float32)raw;
		if (float32_is_nan(f32)) {
			float_set_invalid();
		} else {
			f32 ^= 1L << 31;
			raw = (uint64_t)f32;
		}
	} else {
		float64 f64 = (float64)raw;
		if (float64_is_nan(f64)) {
			float_set_invalid();
		} else {
			f64 ^= 1L << 63;
			raw = (uint64_t)f64;
		}
	}
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_nmadd(struct proc *p, struct trapframe *tf, uint fmt, uint fr, uint ft,
    uint fs, uint fd)
{
	uint64_t raw1, raw2, raw3, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	raw3 = fpu_load(p, tf, fmt, fr);
	if (fmt == FMT_S) {
		float32 f32 = float32_add(
		    float32_mul((float32)raw1, (float32)raw2),
		    (float32)raw3);
		if (float32_is_nan(f32))
			float_set_invalid();
		else
			f32 ^= 1L << 31;
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_add(
		    float64_mul((float64)raw1, (float64)raw2),
		    (float64)raw3);
		if (float64_is_nan(f64))
			float_set_invalid();
		else
			f64 ^= 1L << 63;
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_nmsub(struct proc *p, struct trapframe *tf, uint fmt, uint fr, uint ft,
    uint fs, uint fd)
{
	uint64_t raw1, raw2, raw3, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	raw3 = fpu_load(p, tf, fmt, fr);
	if (fmt == FMT_S) {
		float32 f32 = float32_sub(
		    float32_mul((float32)raw1, (float32)raw2),
		    (float32)raw3);
		if (float32_is_nan(f32))
			float_set_invalid();
		else
			f32 ^= 1L << 31;
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_sub(
		    float64_mul((float64)raw1, (float64)raw2),
		    (float64)raw3);
		if (float64_is_nan(f64))
			float_set_invalid();
		else
			f64 ^= 1L << 63;
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_recip(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	if (fmt == FMT_S) {
		float32 f32 = float32_div(ONE_F32, (float32)raw);
		raw = (uint64_t)f32;
	} else {
		float64 f64 = float64_div(ONE_F64, (float64)raw);
		raw = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_round_l(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards nearest */
	return fpu_int_l(p, tf, fmt, ft, fs, fd, FP_RN);
}

int
fpu_round_w(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards nearest */
	return fpu_int_w(p, tf, fmt, ft, fs, fd, FP_RN);
}

int
fpu_rsqrt(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	if (fmt == FMT_S) {
		float32 f32 = float32_sqrt((float32)raw);
		if ((tf->fsr & (FPCSR_C_V | FPCSR_E_V)) !=
		    (FPCSR_C_V | FPCSR_E_V))
			f32 = float32_div(ONE_F32, f32);
		raw = (uint64_t)f32;
	} else {
		float64 f64 = float64_sqrt((float64)raw);
		if ((tf->fsr & (FPCSR_C_V | FPCSR_E_V)) !=
		    (FPCSR_C_V | FPCSR_E_V))
			f64 = float64_div(ONE_F64, f64);
		raw = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_sqrt(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw;

	if (ft != 0)
		return SIGILL;
	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw = fpu_load(p, tf, fmt, fs);
	if (fmt == FMT_S) {
		float32 f32 = float32_sqrt((float32)raw);
		raw = (uint64_t)f32;
	} else {
		float64 f64 = float64_sqrt((float64)raw);
		raw = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, raw);

	return 0;
}

int
fpu_sub(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	uint64_t raw1, raw2, rslt;

	if (fmt != FMT_S && fmt != FMT_D)
		return SIGILL;

	raw1 = fpu_load(p, tf, fmt, fs);
	raw2 = fpu_load(p, tf, fmt, ft);
	if (fmt == FMT_S) {
		float32 f32 = float32_sub((float32)raw1, (float32)raw2);
		rslt = (uint64_t)f32;
	} else {
		float64 f64 = float64_sub((float64)raw1, (float64)raw2);
		rslt = (uint64_t)f64;
	}
	fpu_store(p, tf, fmt, fd, rslt);

	return 0;
}

int
fpu_trunc_l(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards zero */
	return fpu_int_l(p, tf, fmt, ft, fs, fd, FP_RZ);
}

int
fpu_trunc_w(struct proc *p, struct trapframe *tf, uint fmt, uint ft, uint fs,
    uint fd)
{
	/* round towards zero */
	return fpu_int_w(p, tf, fmt, ft, fs, fd, FP_RZ);
}

#ifdef FPUEMUL

/*
 * Emulate a COP1 non-FPU instruction.
 */
int
nofpu_emulate_cop1(struct proc *p, struct trapframe *tf, uint32_t insn,
    union sigval *sv)
{
	register_t *regs = (register_t *)tf;
	InstFmt inst;
	int32_t cval;

	inst = *(InstFmt *)&insn;

	switch (inst.RType.rs) {
	case OP_MF:
		if (inst.FRType.fd != 0 || inst.FRType.func != 0)
			return SIGILL;
		if (inst.FRType.ft != ZERO)
			regs[inst.FRType.ft] = (int32_t)
			    ((uint64_t *)p->p_md.md_regs)
			      [FPBASE + inst.FRType.fs];
		break;
	case OP_DMF:
		if (inst.FRType.fd != 0 || inst.FRType.func != 0)
			return SIGILL;
		if ((tf->sr & SR_FR_32) != 0 || (inst.FRType.fs & 1) == 0) {
			if (inst.FRType.ft != ZERO)
				regs[inst.FRType.ft] =
				    fpu_load(p, tf, FMT_L, inst.FRType.fs);
		}
		break;
	case OP_CF:
		if (inst.FRType.fd != 0 || inst.FRType.func != 0)
			return SIGILL;
		if (inst.FRType.ft != ZERO) {
			switch (inst.FRType.fs) {
			case 0:	/* FPC_ID */
				cval = MIPS_SOFT << 8;
				break;
			case 31: /* FPC_CSR */
				cval = (int32_t)tf->fsr;
				break;
			default:
				cval = 0;
				break;
			}
			regs[inst.FRType.ft] = (int64_t)cval;
		}
		break;
	case OP_MT:
		if (inst.FRType.fd != 0 || inst.FRType.func != 0)
			return SIGILL;
		((uint64_t *)p->p_md.md_regs)[FPBASE + inst.FRType.fs] =
		    (int32_t)regs[inst.FRType.ft];
		break;
	case OP_DMT:
		if (inst.FRType.fd != 0 || inst.FRType.func != 0)
			return SIGILL;
		if ((tf->sr & SR_FR_32) != 0 || (inst.FRType.fs & 1) == 0) {
			fpu_store(p, tf, FMT_L, inst.FRType.fs,
			    regs[inst.FRType.ft]);
		}
		break;
	case OP_CT:
		if (inst.FRType.fd != 0 || inst.FRType.func != 0)
			return SIGILL;
		cval = (int32_t)regs[inst.FRType.ft];
		switch (inst.FRType.fs) {
		case 31: /* FPC_CSR */
			cval &= ~FPCSR_C_E;
			tf->fsr = cval;
			break;
		case 0:	/* FPC_ID */
		default:
			break;
		}
		break;
	case OP_BC:
	   {
		uint cc, nd, istf;
		int condition;
		vaddr_t dest;
		uint32_t dinsn;

		cc = (inst.RType.rt & COPz_BC_CC_MASK) >> COPz_BC_CC_SHIFT;
		nd = inst.RType.rt & COPz_BCL_TF_MASK;
		istf = inst.RType.rt & COPz_BC_TF_MASK;
		condition = tf->fsr & FPCSR_CONDVAL(cc);
		if ((!condition && !istf) /*bc1f*/ ||
		    (condition && istf) /*bc1t*/) {
			/*
			 * Branch taken: if the delay slot is not a nop,
			 * copy the delay slot instruction to the dedicated
			 * relocation page, in order to be able to have the
			 * cpu process it and give control back to the
			 * kernel, for us to redirect to the branch
			 * destination.
			 */
			/* inline MipsEmulateBranch(tf, tf->pc, tf->fsr, insn)*/
			dest = tf->pc + 4 + ((short)inst.IType.imm << 2);
			if (copyinsn(p, tf->pc + 4, &dinsn) != 0) {
				sv->sival_ptr = (void *)(tf->pc + 4);
				return SIGSEGV;
			}
			if (dinsn == 0x00000000 /* nop */ ||
			    dinsn == 0x00000040 /* ssnop */) {
				tf->pc = dest;
			} else {
				if (fpe_branch_emulate(curproc, tf, dinsn,
				    dest) != 0)
					return SIGILL;
			}
		} else {
			/*
			 * Branch not taken: skip the instruction, and
			 * skip the delay slot if it was a `branch likely'
			 * instruction.
			 */
			tf->pc += 4;
			if (nd)
				tf->pc += 4;
		}
	    }
		break;
	}

	return 0;
}

/*
 * Emulate a COP1X non-FPU instruction.
 */
int
nofpu_emulate_cop1x(struct proc *p, struct trapframe *tf, uint32_t insn,
    union sigval *sv)
{
	register_t *regs = (register_t *)tf;
	InstFmt inst;
	vaddr_t va;
	uint64_t ddata;
	uint32_t wdata;

	inst = *(InstFmt *)&insn;
	switch (inst.FRType.func) {
	case OP_LDXC1:
		if (inst.FQType.fs != 0)
			return SIGILL;
		va = (vaddr_t)regs[inst.FQType.fr] +
		    (vaddr_t)regs[inst.FQType.ft];
		if ((va & 0x07) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		if (copyin((const void *)va, &ddata, sizeof ddata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		if ((tf->sr & SR_FR_32) != 0 || (inst.FQType.fd & 1) == 0)
			fpu_store(p, tf, FMT_L, inst.FQType.fd, ddata);
		break;
	case OP_LWXC1:
		if (inst.FQType.fs != 0)
			return SIGILL;
		va = (vaddr_t)regs[inst.FQType.fr] +
		    (vaddr_t)regs[inst.FQType.ft];
		if ((va & 0x03) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		if (copyin((const void *)va, &wdata, sizeof wdata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		((uint64_t *)p->p_md.md_regs)[FPBASE + inst.FQType.fd] = wdata;
		break;
	case OP_SDXC1:
		if (inst.FQType.fd != 0)
			return SIGILL;
		va = (vaddr_t)regs[inst.FQType.fr] +
		    (vaddr_t)regs[inst.FQType.ft];
		if ((va & 0x07) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		if ((tf->sr & SR_FR_32) != 0 || (inst.FQType.fs & 1) == 0)
			ddata = fpu_load(p, tf, FMT_L, inst.FQType.fs);
		else {
			/* undefined behaviour, don't expose stack content */
			ddata = 0;
		}
		if (copyout(&ddata, (void *)va, sizeof ddata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		break;
	case OP_SWXC1:
		if (inst.FQType.fd != 0)
			return SIGILL;
		va = (vaddr_t)regs[inst.FQType.fr] +
		    (vaddr_t)regs[inst.FQType.ft];
		if ((va & 0x03) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		wdata = ((uint64_t *)p->p_md.md_regs)[FPBASE + inst.FQType.fs];
		if (copyout(&wdata, (void *)va, sizeof wdata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		break;
	case OP_PREFX:
		/* nothing to do */
		break;
	}

	return 0;
}

/*
 * Emulate a load/store instruction on FPU registers.
 */
int
nofpu_emulate_loadstore(struct proc *p, struct trapframe *tf, uint32_t insn,
    union sigval *sv)
{
	register_t *regs = (register_t *)tf;
	InstFmt inst;
	vaddr_t va;
	uint64_t ddata;
	uint32_t wdata;

	inst = *(InstFmt *)&insn;
	switch (inst.IType.op) {
	case OP_LDC1:
		va = (vaddr_t)regs[inst.IType.rs] + (int16_t)inst.IType.imm;
		if ((va & 0x07) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		if (copyin((const void *)va, &ddata, sizeof ddata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		if ((tf->sr & SR_FR_32) != 0 || (inst.IType.rt & 1) == 0)
			fpu_store(p, tf, FMT_L, inst.IType.rt, ddata);
		break;
	case OP_LWC1:
		va = (vaddr_t)regs[inst.IType.rs] + (int16_t)inst.IType.imm;
		if ((va & 0x03) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		if (copyin((const void *)va, &wdata, sizeof wdata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		((uint64_t *)p->p_md.md_regs)[FPBASE + inst.IType.rt] = wdata;
		break;
	case OP_SDC1:
		va = (vaddr_t)regs[inst.IType.rs] + (int16_t)inst.IType.imm;
		if ((va & 0x07) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		if ((tf->sr & SR_FR_32) != 0 || (inst.IType.rt & 1) == 0)
			ddata = fpu_load(p, tf, FMT_L, inst.IType.rt);
		else {
			/* undefined behaviour, don't expose stack content */
			ddata = 0;
		}
		if (copyout(&ddata, (void *)va, sizeof ddata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		break;
	case OP_SWC1:
		va = (vaddr_t)regs[inst.IType.rs] + (int16_t)inst.IType.imm;
		if ((va & 0x03) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGBUS;
		}
		wdata = ((uint64_t *)p->p_md.md_regs)[FPBASE + inst.IType.rt];
		if (copyout(&wdata, (void *)va, sizeof wdata) != 0) {
			sv->sival_ptr = (void *)va;
			return SIGSEGV;
		}
		break;
	}

	return 0;
}

/*
 * Emulate MOVF and MOVT.
 */
int
nofpu_emulate_movci(struct trapframe *tf, uint32_t insn)
{
	register_t *regs = (register_t *)tf;
	InstFmt inst;
	uint cc, istf;
	int condition;

	inst = *(InstFmt *)&insn;
	if ((inst.RType.rt & 0x02) != 0 || inst.RType.shamt != 0)
		return SIGILL;

	cc = inst.RType.rt >> 2;
	istf = inst.RType.rt & COPz_BC_TF_MASK;
	condition = tf->fsr & FPCSR_CONDVAL(cc);
	if ((!condition && !istf) /*movf*/ || (condition && istf) /*movt*/) {
		if (inst.RType.rd != ZERO)
			regs[inst.RType.rd] = regs[inst.RType.rs];
	}

	return 0;
}

#endif	/* FPUEMUL */
