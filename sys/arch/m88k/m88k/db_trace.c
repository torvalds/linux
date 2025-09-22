/*	$OpenBSD: db_trace.c,v 1.21 2024/11/07 16:02:29 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/db_machdep.h>

#include <ddb/db_variables.h>	/* db_variable, DB_VAR_GET, etc.  */
#include <ddb/db_output.h>	/* db_printf                      */
#include <ddb/db_sym.h>		/* DB_STGY_PROC, etc.             */
#include <ddb/db_command.h>	/* db_recover                     */
#include <ddb/db_access.h>
#include <ddb/db_interface.h>

#ifdef DEBUG
#define	DPRINTF(stmt) printf stmt
#else
#define	DPRINTF(stmt) do { } while (0)
#endif

static inline u_int
br_dest(vaddr_t addr, u_int inst)
{
	inst = (inst & 0x03ffffff) << 2;
	/* check if sign extension is needed */
	if (inst & 0x08000000)
		inst |= 0xf0000000;
	return (addr + inst);
}

int frame_is_sane(db_regs_t *regs, int);
const char *m88k_exception_name(u_int vector);
u_int db_trace_get_val(vaddr_t addr, u_int *ptr);

/*
 * Some macros to tell if the given text is the instruction.
 */
#define JMPN_R1(I)		((I) == 0xf400c401)	/* jmp.n r1 */
#define JMP_R1(I)		((I) == 0xf400c001)	/* jmp r1 */

/* gets the IMM16 value from an instruction */
#define IMM16VAL(I)		((I) & 0x0000ffff)

/* subu r31, r31, IMM */
#define SUBU_R31_R31_IMM(I)	(((I) & 0xffff0000) == 0x67ff0000U)

/* st r30, r31, IMM */
#define ST_R30_R31_IMM(I)	(((I) & 0xffff0000) == 0x27df0000U)

extern label_t *db_recover;

/*
 * m88k trace/register state interface for ddb.
 */

#define N(s, x)  {s, (long *)&ddb_regs.x, FCN_NULL}

struct db_variable db_regs[] = {
	N("r1", r[1]),     N("r2", r[2]),    N("r3", r[3]),    N("r4", r[4]),
	N("r5", r[5]),     N("r6", r[6]),    N("r7", r[7]),    N("r8", r[8]),
	N("r9", r[9]),     N("r10", r[10]),  N("r11", r[11]),  N("r12", r[12]),
	N("r13", r[13]),   N("r14", r[14]),  N("r15", r[15]),  N("r16", r[16]),
	N("r17", r[17]),   N("r18", r[18]),  N("r19", r[19]),  N("r20", r[20]),
	N("r21", r[21]),   N("r22", r[22]),  N("r23", r[23]),  N("r24", r[24]),
	N("r25", r[25]),   N("r26", r[26]),  N("r27", r[27]),  N("r28", r[28]),
	N("r29", r[29]),   N("r30", r[30]),  N("r31", r[31]),  N("epsr", epsr),
	N("sxip", sxip),   N("snip", snip),  N("sfip", sfip),  N("ssbr", ssbr),
	N("dmt0", dmt0),   N("dmd0", dmd0),  N("dma0", dma0),  N("dmt1", dmt1),
	N("dmd1", dmd1),   N("dma1", dma1),  N("dmt2", dmt2),  N("dmd2", dmd2),
	N("dma2", dma2),   N("fpecr", fpecr),N("fphs1", fphs1),N("fpls1", fpls1),
	N("fphs2", fphs2), N("fpls2", fpls2),N("fppt", fppt),  N("fprh", fprh),
	N("fprl", fprl),   N("fpit", fpit),  N("fpsr", fpsr),  N("fpcr", fpcr),
};
#undef N

struct db_variable *db_eregs = db_regs + nitems(db_regs);

#define TRASHES    0x001	/* clobbers instruction field D */
#define STORE      0x002	/* does a store to S1+IMM16 */
#define LOAD       0x004	/* does a load from S1+IMM16 */
#define DOUBLE     0x008	/* double-register */
#define FLOW_CTRL  0x010	/* flow-control instruction */
#define DELAYED    0x020	/* delayed flow control */
#define JSR	   0x040	/* flow-control is a jsr[.n] */
#define BSR	   0x080	/* flow-control is a bsr[.n] */

/*
 * Given a word of instruction text, return some flags about that
 * instruction (flags defined above).
 */
static u_int
m88k_instruction_info(u_int32_t instruction)
{
	static const struct {
		u_int32_t mask, value;
		u_int flags;
	} *ptr, control[] = {
		/* runs in the same order as 2nd Ed 88100 manual Table 3-14 */
		{ 0xf0000000U, 0x00000000U, /* xmem */     TRASHES | STORE | LOAD},
		{ 0xec000000U, 0x00000000U, /* ld.d */     TRASHES | LOAD | DOUBLE},
		{ 0xe0000000U, 0x00000000U, /* load */     TRASHES | LOAD},
		{ 0xfc000000U, 0x20000000U, /* st.d */     STORE | DOUBLE},
		{ 0xf0000000U, 0x20000000U, /* store */    STORE},
		{ 0xc0000000U, 0x40000000U, /* arith */    TRASHES},
		{ 0xfc004000U, 0x80004000U, /* ld cr */    TRASHES},
		{ 0xfc004000U, 0x80000000U, /* st cr */    0},
		{ 0xfc008060U, 0x84000000U, /* f */        TRASHES},
		{ 0xfc008060U, 0x84000020U, /* f.d */      TRASHES | DOUBLE},
		{ 0xfc000000U, 0xcc000000U, /* bsr.n */    FLOW_CTRL | DELAYED | BSR},
		{ 0xfc000000U, 0xc8000000U, /* bsr */      FLOW_CTRL | BSR},
		{ 0xe4000000U, 0xc4000000U, /* br/bb.n */  FLOW_CTRL | DELAYED},
		{ 0xe4000000U, 0xc0000000U, /* br/bb */    FLOW_CTRL},
		{ 0xfc000000U, 0xec000000U, /* bcnd.n */   FLOW_CTRL | DELAYED},
		{ 0xfc000000U, 0xe8000000U, /* bcnd */     FLOW_CTRL},
		{ 0xfc00c000U, 0xf0008000U, /* bits */     TRASHES},
		{ 0xfc00c000U, 0xf000c000U, /* trap */     0},
		{ 0xfc00f0e0U, 0xf4002000U, /* st */       0},
		{ 0xfc00cce0U, 0xf4000000U, /* ld.d */     TRASHES | DOUBLE},
		{ 0xfc00c0e0U, 0xf4000000U, /* ld */       TRASHES},
		{ 0xfc00c0e0U, 0xf4004000U, /* arith */    TRASHES},
		{ 0xfc00c3e0U, 0xf4008000U, /* bits */     TRASHES},
		{ 0xfc00ffe0U, 0xf400cc00U, /* jsr.n */    FLOW_CTRL | DELAYED | JSR},
		{ 0xfc00ffe0U, 0xf400c800U, /* jsr */      FLOW_CTRL | JSR},
		{ 0xfc00ffe0U, 0xf400c400U, /* jmp.n */    FLOW_CTRL | DELAYED},
		{ 0xfc00ffe0U, 0xf400c000U, /* jmp */      FLOW_CTRL},
		{ 0xfc00fbe0U, 0xf400e800U, /* ff */       TRASHES},
		{ 0xfc00ffe0U, 0xf400f800U, /* tbnd */     0},
		{ 0xfc00ffe0U, 0xf400fc00U, /* rte */      FLOW_CTRL},
		{ 0xfc000000U, 0xf8000000U, /* tbnd */     0},
	};

	for (ptr = &control[0]; ptr < &control[nitems(control)]; ptr++)
		if ((instruction & ptr->mask) == ptr->value)
			return ptr->flags;
	return 0;
}

static int
hex_value_needs_0x(u_int value)
{
	int c;
	int have_a_hex_digit = 0;

	if (value <= 9)
		return (0);

	while (value != 0) {
		c = value & 0xf;
		value >>= 4;
		if (c > 9)
			have_a_hex_digit = 1;
	}
	if (have_a_hex_digit == 0)	/* has no letter, thus needs 0x */
		return (1);
	if (c > 9)		/* starts with a letter, thus needs 0x */
		return (1);
	return (0);
}

/*
 * returns
 *   1 if regs seems to be a reasonable kernel exception frame.
 *   2 if regs seems to be a reasonable user exception frame
 * 	(in the current task).
 *   0 if this looks like neither.
 */
int
frame_is_sane(db_regs_t *regs, int quiet)
{
	/* no good if we can't read the whole frame */
	if (badaddr((vaddr_t)regs, 4) || badaddr((vaddr_t)&regs->fpit, 4)) {
		if (quiet == 0)
			db_printf("[WARNING: frame at %p : unreadable]\n", regs);
		return 0;
	}

	/* r0 must be 0 (obviously) */
	if (regs->r[0] != 0) {
		if (quiet == 0)
			db_printf("[WARNING: frame at %p : r[0] != 0]\n", regs);
		return 0;
	}

	/* stack sanity ... r31 must be nonzero, and must be word aligned */
	if (regs->r[31] == 0 || (regs->r[31] & 3) != 0) {
		if (quiet == 0)
			db_printf("[WARNING: frame at %p : r[31] == 0 or not word aligned]\n", regs);
		return 0;
	}

#ifdef M88100
	if (CPU_IS88100) {
		/* sxip is reasonable */
#if 0
		if ((regs->sxip & XIP_E) != 0)
			goto out;
#endif
		/* snip is reasonable */
		if ((regs->snip & ~NIP_ADDR) != NIP_V)
			goto out;
		/* sfip is reasonable */
		if ((regs->sfip & ~FIP_ADDR) != FIP_V)
			goto out;
	}
#endif

	/* epsr sanity */
	if (regs->epsr & PSR_BO)
		goto out;

	return ((regs->epsr & PSR_MODE) ? 1 : 2);

out:
	if (quiet == 0)
		db_printf("[WARNING: not an exception frame?]\n");
	return (0);
}

const char *
m88k_exception_name(u_int vector)
{
	switch (vector) {
	default:
	case   0: return "Reset";
	case   1: return "Interrupt";
	case   2: return "Instruction Access Exception";
	case   3: return "Data Access Exception";
	case   4: return "Misaligned Access Exception";
	case   5: return "Unimplemented Opcode Exception";
	case   6: return "Privilege Violation";
	case   7: return "Bounds Check";
	case   8: return "Integer Divide Exception";
	case   9: return "Integer Overflow Exception";
	case  10: return "Error Exception";
	case  11: return "Non Maskable Interrupt";
	case 114: return "FPU precise";
	case 115: return "FPU imprecise";
	case DDB_ENTRY_BKPT_NO:
		return "ddb break";
	case DDB_ENTRY_TRACE_NO:
		return "ddb trace";
	case DDB_ENTRY_TRAP_NO:
		return "ddb trap";
	case 451: return "Syscall";
	}
}

/*
 * Read a word at address addr.
 * Return 1 if was able to read, 0 otherwise.
 */
u_int
db_trace_get_val(vaddr_t addr, u_int *ptr)
{
	label_t db_jmpbuf;
	label_t *prev = db_recover;

	if (setjmp((db_recover = &db_jmpbuf)) != 0) {
		db_recover = prev;
		return 0;
	} else {
		db_read_bytes(addr, 4, (char *)ptr);
		db_recover = prev;
		return 1;
	}
}

#define	FIRST_CALLPRESERVED_REG	14
#define	LAST_CALLPRESERVED_REG	29
#define	FIRST_ARG_REG		2
#define	LAST_ARG_REG		9
#define	RETURN_VAL_REG		1

static u_int global_saved_list = 0x0; /* one bit per register */
static u_int local_saved_list  = 0x0; /* one bit per register */
static u_int trashed_list      = 0x0; /* one bit per register */
static u_int saved_reg[32];		 /* one value per register */

#define	reg_bit(reg)	1 << (reg)

static void
save_reg(int reg, u_int value)
{
	reg &= 0x1f;
	if (trashed_list & reg_bit(reg))
		return;	/* don't save trashed registers */

	saved_reg[reg] = value;
	global_saved_list |= reg_bit(reg);
	local_saved_list |= reg_bit(reg);
}

#define	mark_reg_trashed(reg)	trashed_list |= reg_bit((reg) & 0x1f)

#define	have_global_reg(reg)	(global_saved_list & reg_bit(reg))
#define	have_local_reg(reg)	(local_saved_list & reg_bit(reg))

#define	clear_local_saved_regs()	local_saved_list = trashed_list = 0
#define	clear_global_saved_regs()	local_saved_list = global_saved_list = 0

#define	saved_reg_value(reg)	saved_reg[(reg)]

/*
 * Show any arguments that we might have been able to determine.
 */
static void
print_args(void)
{
	int reg, last_arg;

	/* find the highest argument register saved */
	for (last_arg = LAST_ARG_REG; last_arg >= FIRST_ARG_REG; last_arg--)
		if (have_local_reg(last_arg))
			break;
	if (last_arg < FIRST_ARG_REG)
		return;	/* none were saved */

	db_printf("(");

	/* print each one, up to the highest */
	for (reg = FIRST_ARG_REG; /*nothing */; reg++) {
		if (!have_local_reg(reg))
			db_printf("?");
		else {
			u_int value = saved_reg_value(reg);
			db_printf("%s%x", hex_value_needs_0x(value) ?
				  "0x" : "", value);
		}
		if (reg == last_arg)
			break;
		else
			db_printf(", ");
	}
	db_printf(")");
}

#define JUMP_SOURCE_IS_BAD		0
#define JUMP_SOURCE_IS_OK		1
#define JUMP_SOURCE_IS_UNLIKELY		2

/*
 * Give an address to where we return, and an address to where we'd jumped,
 * Decided if it all makes sense.
 *
 * Gcc sometimes optimized something like
 *	if (condition)
 *		func1();
 *	else
 *		OtherStuff...
 * to
 *	bcnd	!condition, mark
 *	bsr.n	func1
 *	 or	r1, r0, mark2
 *    mark:
 *	OtherStuff...
 *    mark2:
 *
 * So RETURN_TO will be mark2, even though we really did branch via
 * 'bsr.n func1', so this makes it difficult to be certain about being
 * wrong.
 */
static int
is_jump_source_ok(vaddr_t return_to, vaddr_t jump_to)
{
	u_int flags;
	uint32_t instruction;

	/*
	 * Delayed branches are the most common... look two instructions before
	 * where we were going to return to to see if it's a delayed branch.
	 */
	if (!db_trace_get_val(return_to - 8, &instruction))
		return JUMP_SOURCE_IS_BAD;

	flags = m88k_instruction_info(instruction);
	if ((flags & (FLOW_CTRL | DELAYED)) == (FLOW_CTRL | DELAYED) &&
	    (flags & (JSR | BSR)) != 0) {
		if ((flags & JSR) != 0)
			return JUMP_SOURCE_IS_OK; /* have to assume it's correct */
		/* calculate the offset */
		if (br_dest(return_to - 8, instruction) == jump_to)
			return JUMP_SOURCE_IS_OK; /* exactamundo! */
		else
			return JUMP_SOURCE_IS_UNLIKELY;	/* seems wrong */
	}

	/*
	 * Try again, looking for a non-delayed jump one instruction back.
	 */
	if (!db_trace_get_val(return_to - 4, &instruction))
		return JUMP_SOURCE_IS_BAD;

	flags = m88k_instruction_info(instruction);
	if ((flags & (FLOW_CTRL | DELAYED)) == FLOW_CTRL &&
	    (flags & (JSR | BSR)) != 0) {
		if ((flags & JSR) != 0)
			return JUMP_SOURCE_IS_OK; /* have to assume it's correct */
		/* calculate the offset */
		if (br_dest(return_to - 4, instruction) == jump_to)
			return JUMP_SOURCE_IS_OK; /* exactamundo! */
		else
			return JUMP_SOURCE_IS_UNLIKELY;	/* seems wrong */
	}

	return JUMP_SOURCE_IS_UNLIKELY;
}

static int next_address_likely_wrong = 0;

/* How much slop we expect in the stack trace */
#define FRAME_PLAY 8

/*
 *  Stack decode -
 *	vaddr_t addr;    program counter
 *	vaddr_t *stack; IN/OUT stack pointer
 *
 * 	given an address within a function and a stack pointer,
 *	try to find the function from which this one was called
 *	and the stack pointer for that function.
 *
 *	The return value is zero if we get confused or
 *	we determine that the return address has not yet
 *	been saved (early in the function prologue). Otherwise
 *	the return value is the address from which this function
 *	was called.
 *
 *	Note that even is zero is returned (the second case) the
 *	stack pointer can be adjusted.
 */
static vaddr_t
stack_decode(vaddr_t addr, vaddr_t *stack, int (*pr)(const char *, ...))
{
	Elf_Sym *proc;
	db_expr_t offset_from_proc;
	uint instructions_to_search;
	vaddr_t check_addr;
	vaddr_t function_addr;    /* start of function */
	uint32_t r31;
	uint32_t inst;
	vaddr_t ret_addr;	    /* address to which we return */
	u_int tried_to_save_r1 = 0;
	vaddr_t str30_addr = 0;
	vaddr_t last_subu_addr = 0;

	/* get what we hope will be the symbol for the function name */
	proc = db_search_symbol(addr, DB_STGY_PROC, &offset_from_proc);
	if (offset_from_proc == addr) /* i.e. no symbol found */
		proc = NULL;

	/*
	 * Try and find the start of this function, and its stack usage.
	 * If we do not have symbols available, we will need to
	 * look back in memory for a prologue pattern.
	 */
	if (proc != NULL) {
		const char *names = NULL;
		db_symbol_values(proc, &names, &function_addr);
		if (names == NULL)
			return 0;
	} else {
		/*
		 * Unable to find symbol. Search back looking for a function
		 * prolog.
		 *
		 * This is a difficult game because of the compiler
		 * optimizations. However, we can rely upon the first two
		 * instructions of the function being:
		 *	subu	r31, r31, imm16
		 *	st	r30, r31, imm16
		 * unless the function did not need a frame, in which case
		 * the store of r30 is missing...
		 */
		instructions_to_search = 400;
		for (check_addr = addr; instructions_to_search-- != 0;
		    check_addr -= 4) {
			if (!db_trace_get_val(check_addr, &inst))
				break;

			if (ST_R30_R31_IMM(inst)) {
				DPRINTF(("{st r30 found at %p}\n",
				    check_addr));
				str30_addr = (vaddr_t)check_addr;
			} else if (SUBU_R31_R31_IMM(inst)) {
				DPRINTF(("{subu r31 found at %p}\n",
				    check_addr));
				if (str30_addr == (vaddr_t)check_addr + 4)
					break;	/* success */
				else
					last_subu_addr = (vaddr_t)check_addr;
			}

			/*
			 * if we come across a [jmp r1] or [jmp.n r1] assume
			 * we have hit the previous functions epilogue and
			 * stop our search.
			 * Since we know we would have hit the "subu r31, r31"
			 * if it was right in front of us, we know this doesn't
			 * have one so we just return failure....
			 */
			if (JMP_R1(inst) || JMPN_R1(inst)) {
				if (last_subu_addr != 0) {
					check_addr = last_subu_addr;
					break;
				} else
					return 0;
			}
		}
		if (instructions_to_search == 0)
			return 0; /* bummer, couldn't find it */
		function_addr = check_addr;
	}

	DPRINTF(("{start of function at %p}\n", function_addr));
	/*
	 * We now know the start of the function (function_addr).
	 * If we're stopped right there, or if it's not a
	 *		subu r31, r31, imm16
	 * then we're done.
	 */
	if (addr == function_addr)
		return 0;
	if (!db_trace_get_val(function_addr, &inst))
		return 0;
	if (!SUBU_R31_R31_IMM(inst))
		return 0;

 	/*
	 * Unfortunately for us, functions with variable number of
	 * arguments will further alter r31 in va_start(), through the
	 * use of __builtin_alloca(). Since panic() is one such
	 * function, we need to take this into account to be able to
	 * backtrack further.
	 *
	 * So we need to look for further addu/subu r31 sequences after the
	 * prologue.
	 *
	 * Of course, control flow may cause execution to NOT pass
	 * through the __builtin_alloca() expansion...
	 */
	DPRINTF(("{orig r31 is %p}\n", *stack));
	*stack += IMM16VAL(inst);

	if (function_addr == (vaddr_t)&panic) /* XXX others? */ {
		check_addr = function_addr + 4;
		instructions_to_search = (addr - check_addr) / 4;
		while (instructions_to_search-- != 0) {
			if (!db_trace_get_val(check_addr, &inst))
				break;
			if (SUBU_R31_R31_IMM(inst)) {
				DPRINTF(("{variadic subu r31 found at %p}\n",
				    check_addr));
				*stack += IMM16VAL(inst);
			}
			check_addr += 4;
		}
	}

	/*
	 * Search from the beginning of the function (function_addr) to where
	 * we are in the function (addr) looking to see what kind of registers
	 * have been saved on the stack.
	 *
	 * We'll stop looking before we get to addr if we hit a branch.
	 */
	clear_local_saved_regs();
	check_addr = function_addr;
	r31 = *stack;
	DPRINTF(("{entry r31 would be %p}\n", r31));
	for (instructions_to_search = (addr - check_addr) / sizeof(uint32_t);
	    instructions_to_search-- != 0; check_addr += 4) {
		uint32_t s1, d;
		uint flags;

		/* read the instruction */
		if (!db_trace_get_val(check_addr, &inst))
			break;

		if (SUBU_R31_R31_IMM(inst)) {
			r31 -= IMM16VAL(inst);
			DPRINTF(("{adjust r31 to %p at %p}\n",
			    r31, check_addr));
		}

		/* find out the particulars about this instruction */
		flags = m88k_instruction_info(inst);

		/* split the instruction in its diatic components anyway */
		s1 = (inst >> 16) & 0x1f;
		d = (inst >> 21) & 0x1f;

		/* if a store to something off the stack pointer, note the value */
		if ((flags & STORE) && s1 == 31 /*stack pointer*/) {
			u_int value;
			if (!have_local_reg(d)) {
				if (d == 1)
					tried_to_save_r1 = r31 + IMM16VAL(inst);
				DPRINTF(("{r%d saved at %p to %p}\n",
				    d, check_addr, r31 + IMM16VAL(inst)));
				if (db_trace_get_val(r31 + IMM16VAL(inst),
				    &value))
					save_reg(d, value);
			}
			if ((flags & DOUBLE) && !have_local_reg(d + 1)) {
				if (d == 0)
					tried_to_save_r1 = r31 +
					    IMM16VAL(inst) + 4;
				DPRINTF(("{r%d saved at %p to %p}\n",
				    d + 1, check_addr, r31 + IMM16VAL(inst)));
				if (db_trace_get_val(r31 + IMM16VAL(inst) + 4,
				    &value))
					save_reg(d + 1, value);
			}
		}

		/* if an inst that kills D (and maybe D+1), note that */
		if (flags & TRASHES) {
			mark_reg_trashed(d);
			if (flags & DOUBLE)
				mark_reg_trashed(d + 1);
		}

		/* if a flow control instruction, stop now (or next if delayed) */
		if ((flags & FLOW_CTRL) && instructions_to_search != 0)
			instructions_to_search = (flags & DELAYED) ? 1 : 0;
	}

	/*
	 * If we didn't save r1 at some point, we're hosed.
	 */
	if (!have_local_reg(1)) {
		if (tried_to_save_r1 != 0) {
			(*pr)("    <return value of next fcn unreadable in %08x>\n",
				  tried_to_save_r1);
		}
		return 0;
	}

	ret_addr = saved_reg_value(1);

	if (ret_addr != 0) {
		switch (is_jump_source_ok(ret_addr, function_addr)) {
		case JUMP_SOURCE_IS_OK:
			break; /* excellent */

		case JUMP_SOURCE_IS_BAD:
			return 0; /* bummer */

		case JUMP_SOURCE_IS_UNLIKELY:
			next_address_likely_wrong = 1;
			break;
		}
	}

	return ret_addr;
}

static void
db_stack_trace_cmd2(db_regs_t *regs, int (*pr)(const char *, ...))
{
	vaddr_t stack;
	u_int depth=1;
	vaddr_t where;
	u_int ft;
	u_int pair[2];
	int i;

	/*
	 * Frame_is_sane returns:
	 *   1 if regs seems to be a reasonable kernel exception frame.
	 *   2 if regs seems to be a reasonable user exception frame
	 *      (in the current task).
	 *   0 if this looks like neither.
	 */
	if ((ft = frame_is_sane(regs, 1)) == 0) {
		(*pr)("Register frame 0x%x is suspicious; skipping trace\n", regs);
		return;
	}

	/* if user space and no user space trace specified, puke */
	if (ft == 2)
		return;

	/* fetch address */
	where = PC_REGS(regs);
	stack = regs->r[31];
	(*pr)("stack base = 0x%x\n", stack);
	(*pr)("(0) "); /* depth of trace */
	db_printsym(where, DB_STGY_PROC, pr);
	clear_global_saved_regs();

	/* see if this routine had a stack frame */
	if ((where = stack_decode(where, &stack, pr)) == 0) {
		where = regs->r[1];
		(*pr)("(stackless)");
	} else
		print_args();
	(*pr)("\n");

	do {
		/*
		 * If requested, show preserved registers at the time
		 * the next-shown call was made. Only registers known to have
		 * changed from the last exception frame are shown, as others
		 * can be gotten at by looking at the exception frame.
		 */
		(*pr)("(%d)%c", depth++, next_address_likely_wrong ? '?' : ' ');
		next_address_likely_wrong = 0;

		db_printsym(where, DB_STGY_PROC, pr);
		where = stack_decode(where, &stack, pr);
		print_args();
		(*pr)("\n");
	} while (where);

	/* try to trace back over trap/exception */

	stack &= ~7; /* double word aligned */
	/* take last top of stack, and try to find an exception frame near it */

	i = FRAME_PLAY;
	while (i) {
		/*
		 * On the stack, a pointer to the exception frame is written
		 * in two adjacent words. In the case of a fault from the kernel,
		 * this should point to the frame right above them:
		 *
		 * Exception Frame Top
		 * ..
		 * Exception Frame Bottom  <-- frame addr
		 * frame addr
		 * frame addr		<-- stack pointer
		 *
		 * In the case of a fault from user mode, the top of stack
		 * will just have the address of the frame
		 * replicated twice.
		 *
		 * frame addr		<-- top of stack
		 * frame addr
		 *
		 * Here we are just looking for kernel exception frames.
		 */

		if (badaddr((vaddr_t)stack, 4) ||
		    badaddr((vaddr_t)(stack + 4), 4))
			break;

		db_read_bytes((vaddr_t)stack, 2 * sizeof(int), (char *)pair);

		/* the pairs should match and equal stack+8 */
		if (pair[0] == pair[1]) {
			if (pair[0] != stack+8) {
#if 0
				if (!badaddr((vaddr_t)pair[0], 4) &&
				    pair[0] != 0)
					(*pr)("stack_trace:found pair 0x%x but != to stack+8\n",
					    pair[0]);
#endif
			} else if (frame_is_sane((db_regs_t*)pair[0], 1) != 0) {
				struct trapframe *frame =
				    (struct trapframe *)pair[0];

				(*pr)("-------------- %s [EF: 0x%x] -------------\n",
				    m88k_exception_name(frame->tf_vector),
				    frame);
				db_stack_trace_cmd2(&frame->tf_regs, pr);
				return;
			}
		}
		stack += 8;
		i--;
	}
}

/*
 * stack trace - needs a pointer to a m88k saved state.
 *
 * If argument f is given, the stack pointer of each call frame is
 * printed.
 */
void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	enum {
		Default, Stack, Frame
	} style = Default;
	db_regs_t frame;
	db_regs_t *regs;
	union {
		db_regs_t *frame;
		db_expr_t num;
	} arg;

	arg.num = addr;

	while (modif && *modif) {
		switch (*modif++) {
		case 's': style = Stack  ; break;
		case 'f': style = Frame  ; break;
		default:
			(*pr)("unknown trace modifier [%c]\n", modif[-1]);
			/*FALLTHROUGH*/
		case 'h':
			(*pr)("usage: trace/[MODIFIER]  [ARG]\n");
			(*pr)("  s = ARG is a stack pointer\n");
			(*pr)("  f = ARG is a frame pointer\n");
			return;
		}
	}

	if (!have_addr && style != Default) {
		(*pr)("expecting argument with /s or /f\n");
		return;
	}
	if (have_addr && style == Default)
		style = Frame;

	switch (style) {
	case Default:
		regs = &ddb_regs;
		break;
	case Frame:
		regs = arg.frame;
		break;
	case Stack:
	    {
		u_int val1, val2, sxip;
		u_int ptr;
		bzero((void *)&frame, sizeof(frame));
#define REASONABLE_FRAME_DISTANCE 2048

		/*
		 * We've got to find the top of a stack frame so we can get both
		 * a PC and a real SP.
		 */
		for (ptr = arg.num;/**/; ptr += 4) {
			/* Read a word from the named stack */
			if (db_trace_get_val(ptr, &val1) == 0) {
				(*pr)("can't read from %x, aborting.\n", ptr);
				return;
			}

			/*
			 * See if it's a frame pointer.... if so it will be larger than
			 * the address it was taken from (i.e. point back up the stack)
			 * and we'll be able to read where it points.
			 */
			if (val1 <= ptr ||
			    (val1 & 3)  ||
			    val1 > (ptr + REASONABLE_FRAME_DISTANCE))
				continue;

			/* peek at the next word to see if it could be a return address */
			if (db_trace_get_val(ptr, &sxip) == 0) {
				(*pr)("can't read from %x, aborting.\n", ptr);
				return;
			}
			if (sxip == 0 || !db_trace_get_val(sxip, &val2))
				continue;

			if (db_trace_get_val(val1, &val2) == 0) {
				(*pr)("can't read from %x, aborting.\n", val1);
				continue;
			}

			/*
			 * The value we've just read will be either
			 * another frame pointer, or the start of
			 * another exception frame.
			 */
			if (val2 == 0x12345678 &&
			    db_trace_get_val(val1 - 4, &val2) &&
			    val2 == val1 &&
			    db_trace_get_val(val1 - 8, &val2) &&
			    val2 == val1) {
				/* we've found a frame, so the stack
				   must have been good */
				(*pr)("%x looks like a frame, accepting %x\n",val1,ptr);
				break;
			}

			if (val2 > val1 && (val2 & 3) == 0) {
				/* well, looks close enough to be another frame pointer */
				(*pr)("*%x = %x looks like a stack frame pointer, accepting %x\n", val1, val2, ptr);
				break;
			}
		}
		frame.r[31] = ptr;
		frame.epsr = 0x800003f0U;
#ifdef M88100
		if (CPU_IS88100) {
			frame.sxip = sxip | XIP_V;
			frame.snip = frame.sxip + 4;
			frame.sfip = frame.snip + 4;
		}
#endif
		(*pr)("[r31=%x, %sxip=%x]\n", frame.r[31],
		    CPU_IS88110 ? "e" : "s", frame.sxip);
		regs = &frame;
	    }
		break;
	}
	db_stack_trace_cmd2(regs, pr);
}
