/*
 * Copyright 2013-2014 Andrew Turner.
 * Copyright 2013-2014 Ian Lepore.
 * Copyright 2013-2014 Rui Paulo.
 * Copyright 2013 Eitan Adler.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker.h>

#include <machine/stack.h>

#include "linker_if.h"

/*
 * Definitions for the instruction interpreter.
 *
 * The ARM EABI specifies how to perform the frame unwinding in the
 * Exception Handling ABI for the ARM Architecture document. To perform
 * the unwind we need to know the initial frame pointer, stack pointer,
 * link register and program counter. We then find the entry within the
 * index table that points to the function the program counter is within.
 * This gives us either a list of three instructions to process, a 31-bit
 * relative offset to a table of instructions, or a value telling us
 * we can't unwind any further.
 *
 * When we have the instructions to process we need to decode them
 * following table 4 in section 9.3. This describes a collection of bit
 * patterns to encode that steps to take to update the stack pointer and
 * link register to the correct values at the start of the function.
 */

/* A special case when we are unable to unwind past this function */
#define	EXIDX_CANTUNWIND	1

/*
 * These are set in the linker script. Their addresses will be
 * either the start or end of the exception table or index.
 */
extern int exidx_start, exidx_end;

/*
 * Entry types.
 * These are the only entry types that have been seen in the kernel.
 */
#define	ENTRY_MASK	0xff000000
#define	ENTRY_ARM_SU16	0x80000000
#define	ENTRY_ARM_LU16	0x81000000

/* Instruction masks. */
#define	INSN_VSP_MASK		0xc0
#define	INSN_VSP_SIZE_MASK	0x3f
#define	INSN_STD_MASK		0xf0
#define	INSN_STD_DATA_MASK	0x0f
#define	INSN_POP_TYPE_MASK	0x08
#define	INSN_POP_COUNT_MASK	0x07
#define	INSN_VSP_LARGE_INC_MASK	0xff

/* Instruction definitions */
#define	INSN_VSP_INC		0x00
#define	INSN_VSP_DEC		0x40
#define	INSN_POP_MASKED		0x80
#define	INSN_VSP_REG		0x90
#define	INSN_POP_COUNT		0xa0
#define	INSN_FINISH		0xb0
#define	INSN_POP_REGS		0xb1
#define	INSN_VSP_LARGE_INC	0xb2

/* An item in the exception index table */
struct unwind_idx {
	uint32_t offset;
	uint32_t insn;
};

/* Expand a 31-bit signed value to a 32-bit signed value */
static __inline int32_t
expand_prel31(uint32_t prel31)
{

	return ((int32_t)(prel31 & 0x7fffffffu) << 1) / 2;
}

struct search_context {
	uint32_t addr;
	caddr_t exidx_start;
	caddr_t exidx_end;
};

static int
module_search(linker_file_t lf, void *context)
{
	struct search_context *sc = context;
	linker_symval_t symval;
	c_linker_sym_t sym;

	if (lf->address <= (caddr_t)sc->addr &&
	    (lf->address + lf->size) >= (caddr_t)sc->addr) {
		if ((LINKER_LOOKUP_SYMBOL(lf, "__exidx_start", &sym) == 0 ||
		    LINKER_LOOKUP_SYMBOL(lf, "exidx_start", &sym) == 0) &&
		    LINKER_SYMBOL_VALUES(lf, sym, &symval) == 0)
			sc->exidx_start = symval.value;

		if ((LINKER_LOOKUP_SYMBOL(lf, "__exidx_end", &sym) == 0 ||
		    LINKER_LOOKUP_SYMBOL(lf, "exidx_end", &sym) == 0) &&
		    LINKER_SYMBOL_VALUES(lf, sym, &symval) == 0)
			sc->exidx_end = symval.value;

		if (sc->exidx_start != NULL && sc->exidx_end != NULL)
			return (1);
		panic("Invalid module %s, no unwind tables\n", lf->filename);
	}
	return (0);
}

/*
 * Perform a binary search of the index table to find the function
 * with the largest address that doesn't exceed addr.
 */
static struct unwind_idx *
find_index(uint32_t addr, int search_modules)
{
	struct search_context sc;
	caddr_t idx_start, idx_end;
	unsigned int min, mid, max;
	struct unwind_idx *start;
	struct unwind_idx *item;
	int32_t prel31_addr;
	uint32_t func_addr;

	start = (struct unwind_idx *)&exidx_start;
	idx_start = (caddr_t)&exidx_start;
	idx_end = (caddr_t)&exidx_end;

	/* This may acquire a lock */
	if (search_modules) {
		bzero(&sc, sizeof(sc));
		sc.addr = addr;
		if (linker_file_foreach(module_search, &sc) != 0 &&
		   sc.exidx_start != NULL && sc.exidx_end != NULL) {
			start = (struct unwind_idx *)sc.exidx_start;
			idx_start = sc.exidx_start;
			idx_end = sc.exidx_end;
		}
	}

	min = 0;
	max = (idx_end - idx_start) / sizeof(struct unwind_idx);

	while (min != max) {
		mid = min + (max - min + 1) / 2;

		item = &start[mid];

		prel31_addr = expand_prel31(item->offset);
		func_addr = (uint32_t)&item->offset + prel31_addr;

		if (func_addr <= addr) {
			min = mid;
		} else {
			max = mid - 1;
		}
	}

	return &start[min];
}

/* Reads the next byte from the instruction list */
static uint8_t
unwind_exec_read_byte(struct unwind_state *state)
{
	uint8_t insn;

	/* Read the unwind instruction */
	insn = (*state->insn) >> (state->byte * 8);

	/* Update the location of the next instruction */
	if (state->byte == 0) {
		state->byte = 3;
		state->insn++;
		state->entries--;
	} else
		state->byte--;

	return insn;
}

/* Executes the next instruction on the list */
static int
unwind_exec_insn(struct unwind_state *state)
{
	unsigned int insn;
	uint32_t *vsp = (uint32_t *)state->registers[SP];
	int update_vsp = 0;

	/* This should never happen */
	if (state->entries == 0)
		return 1;

	/* Read the next instruction */
	insn = unwind_exec_read_byte(state);

	if ((insn & INSN_VSP_MASK) == INSN_VSP_INC) {
		state->registers[SP] += ((insn & INSN_VSP_SIZE_MASK) << 2) + 4;

	} else if ((insn & INSN_VSP_MASK) == INSN_VSP_DEC) {
		state->registers[SP] -= ((insn & INSN_VSP_SIZE_MASK) << 2) + 4;

	} else if ((insn & INSN_STD_MASK) == INSN_POP_MASKED) {
		unsigned int mask, reg;

		/* Load the mask */
		mask = unwind_exec_read_byte(state);
		mask |= (insn & INSN_STD_DATA_MASK) << 8;

		/* We have a refuse to unwind instruction */
		if (mask == 0)
			return 1;

		/* Update SP */
		update_vsp = 1;

		/* Load the registers */
		for (reg = 4; mask && reg < 16; mask >>= 1, reg++) {
			if (mask & 1) {
				state->registers[reg] = *vsp++;
				state->update_mask |= 1 << reg;

				/* If we have updated SP kep its value */
				if (reg == SP)
					update_vsp = 0;
			}
		}

	} else if ((insn & INSN_STD_MASK) == INSN_VSP_REG &&
	    ((insn & INSN_STD_DATA_MASK) != 13) &&
	    ((insn & INSN_STD_DATA_MASK) != 15)) {
		/* sp = register */
		state->registers[SP] =
		    state->registers[insn & INSN_STD_DATA_MASK];

	} else if ((insn & INSN_STD_MASK) == INSN_POP_COUNT) {
		unsigned int count, reg;

		/* Read how many registers to load */
		count = insn & INSN_POP_COUNT_MASK;

		/* Update sp */
		update_vsp = 1;

		/* Pop the registers */
		for (reg = 4; reg <= 4 + count; reg++) {
			state->registers[reg] = *vsp++;
			state->update_mask |= 1 << reg;
		}

		/* Check if we are in the pop r14 version */
		if ((insn & INSN_POP_TYPE_MASK) != 0) {
			state->registers[14] = *vsp++;
		}

	} else if (insn == INSN_FINISH) {
		/* Stop processing */
		state->entries = 0;

	} else if (insn == INSN_POP_REGS) {
		unsigned int mask, reg;

		mask = unwind_exec_read_byte(state);
		if (mask == 0 || (mask & 0xf0) != 0)
			return 1;

		/* Update SP */
		update_vsp = 1;

		/* Load the registers */
		for (reg = 0; mask && reg < 4; mask >>= 1, reg++) {
			if (mask & 1) {
				state->registers[reg] = *vsp++;
				state->update_mask |= 1 << reg;
			}
		}

	} else if ((insn & INSN_VSP_LARGE_INC_MASK) == INSN_VSP_LARGE_INC) {
		unsigned int uleb128;

		/* Read the increment value */
		uleb128 = unwind_exec_read_byte(state);

		state->registers[SP] += 0x204 + (uleb128 << 2);

	} else {
		/* We hit a new instruction that needs to be implemented */
#if 0
		db_printf("Unhandled instruction %.2x\n", insn);
#endif
		return 1;
	}

	if (update_vsp) {
		state->registers[SP] = (uint32_t)vsp;
	}

#if 0
	db_printf("fp = %08x, sp = %08x, lr = %08x, pc = %08x\n",
	    state->registers[FP], state->registers[SP], state->registers[LR],
	    state->registers[PC]);
#endif

	return 0;
}

/* Performs the unwind of a function */
static int
unwind_tab(struct unwind_state *state)
{
	uint32_t entry;

	/* Set PC to a known value */
	state->registers[PC] = 0;

	/* Read the personality */
	entry = *state->insn & ENTRY_MASK;

	if (entry == ENTRY_ARM_SU16) {
		state->byte = 2;
		state->entries = 1;
	} else if (entry == ENTRY_ARM_LU16) {
		state->byte = 1;
		state->entries = ((*state->insn >> 16) & 0xFF) + 1;
	} else {
#if 0
		db_printf("Unknown entry: %x\n", entry);
#endif
		return 1;
	}

	while (state->entries > 0) {
		if (unwind_exec_insn(state) != 0)
			return 1;
	}

	/*
	 * The program counter was not updated, load it from the link register.
	 */
	if (state->registers[PC] == 0) {
		state->registers[PC] = state->registers[LR];

		/*
		 * If the program counter changed, flag it in the update mask.
		 */
		if (state->start_pc != state->registers[PC])
			state->update_mask |= 1 << PC;
	}

	return 0;
}

int
unwind_stack_one(struct unwind_state *state, int can_lock)
{
	struct unwind_idx *index;
	int finished;

	/* Reset the mask of updated registers */
	state->update_mask = 0;

	/* The pc value is correct and will be overwritten, save it */
	state->start_pc = state->registers[PC];

	/* Find the item to run */
	index = find_index(state->start_pc, can_lock);

	finished = 0;
	if (index->insn != EXIDX_CANTUNWIND) {
		if (index->insn & (1U << 31)) {
			/* The data is within the instruction */
			state->insn = &index->insn;
		} else {
			/* A prel31 offset to the unwind table */
			state->insn = (uint32_t *)
			    ((uintptr_t)&index->insn +
			     expand_prel31(index->insn));
		}
		/* Run the unwind function */
		finished = unwind_tab(state);
	}

	/* This is the top of the stack, finish */
	if (index->insn == EXIDX_CANTUNWIND)
		finished = 1;

	return (finished);
}
