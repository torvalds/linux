/* Define per-register tables for data flow info and register allocation.
   Copyright (C) 1987, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2003, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_REGS_H
#define GCC_REGS_H

#include "varray.h"
#include "obstack.h"
#include "hard-reg-set.h"
#include "basic-block.h"

#define REG_BYTES(R) mode_size[(int) GET_MODE (R)]

/* When you only have the mode of a pseudo register before it has a hard
   register chosen for it, this reports the size of each hard register
   a pseudo in such a mode would get allocated to.  A target may
   override this.  */

#ifndef REGMODE_NATURAL_SIZE
#define REGMODE_NATURAL_SIZE(MODE)	UNITS_PER_WORD
#endif

#ifndef SMALL_REGISTER_CLASSES
#define SMALL_REGISTER_CLASSES 0
#endif

/* Maximum register number used in this function, plus one.  */

extern int max_regno;

/* Register information indexed by register number */
typedef struct reg_info_def
{				/* fields set by reg_scan */
  int first_uid;		/* UID of first insn to use (REG n) */
  int last_uid;			/* UID of last insn to use (REG n) */

				/* fields set by reg_scan & flow_analysis */
  int sets;			/* # of times (REG n) is set */

				/* fields set by flow_analysis */
  int refs;			/* # of times (REG n) is used or set */
  int freq;			/* # estimated frequency (REG n) is used or set */
  int deaths;			/* # of times (REG n) dies */
  int live_length;		/* # of instructions (REG n) is live */
  int calls_crossed;		/* # of calls (REG n) is live across */
  int throw_calls_crossed;	/* # of calls that may throw (REG n) is live across */
  int basic_block;		/* # of basic blocks (REG n) is used in */
} reg_info;

typedef reg_info *reg_info_p;

DEF_VEC_P(reg_info_p);
DEF_VEC_ALLOC_P(reg_info_p,heap);

extern VEC(reg_info_p,heap) *reg_n_info;

/* Indexed by n, gives number of times (REG n) is used or set.  */

#define REG_N_REFS(N) (VEC_index (reg_info_p, reg_n_info, N)->refs)

/* Estimate frequency of references to register N.  */

#define REG_FREQ(N) (VEC_index (reg_info_p, reg_n_info, N)->freq)

/* The weights for each insn varries from 0 to REG_FREQ_BASE.
   This constant does not need to be high, as in infrequently executed
   regions we want to count instructions equivalently to optimize for
   size instead of speed.  */
#define REG_FREQ_MAX 1000

/* Compute register frequency from the BB frequency.  When optimizing for size,
   or profile driven feedback is available and the function is never executed,
   frequency is always equivalent.  Otherwise rescale the basic block
   frequency.  */
#define REG_FREQ_FROM_BB(bb) (optimize_size				      \
			      || (flag_branch_probabilities		      \
				  && !ENTRY_BLOCK_PTR->count)		      \
			      ? REG_FREQ_MAX				      \
			      : ((bb)->frequency * REG_FREQ_MAX / BB_FREQ_MAX)\
			      ? ((bb)->frequency * REG_FREQ_MAX / BB_FREQ_MAX)\
			      : 1)

/* Indexed by n, gives number of times (REG n) is set.
   ??? both regscan and flow allocate space for this.  We should settle
   on just copy.  */

#define REG_N_SETS(N) (VEC_index (reg_info_p, reg_n_info, N)->sets)

/* Indexed by N, gives number of insns in which register N dies.
   Note that if register N is live around loops, it can die
   in transitions between basic blocks, and that is not counted here.
   So this is only a reliable indicator of how many regions of life there are
   for registers that are contained in one basic block.  */

#define REG_N_DEATHS(N) (VEC_index (reg_info_p, reg_n_info, N)->deaths)

/* Get the number of consecutive words required to hold pseudo-reg N.  */

#define PSEUDO_REGNO_SIZE(N) \
  ((GET_MODE_SIZE (PSEUDO_REGNO_MODE (N)) + UNITS_PER_WORD - 1)		\
   / UNITS_PER_WORD)

/* Get the number of bytes required to hold pseudo-reg N.  */

#define PSEUDO_REGNO_BYTES(N) \
  GET_MODE_SIZE (PSEUDO_REGNO_MODE (N))

/* Get the machine mode of pseudo-reg N.  */

#define PSEUDO_REGNO_MODE(N) GET_MODE (regno_reg_rtx[N])

/* Indexed by N, gives number of CALL_INSNS across which (REG n) is live.  */

#define REG_N_CALLS_CROSSED(N)					\
  (VEC_index (reg_info_p, reg_n_info, N)->calls_crossed)

/* Indexed by N, gives number of CALL_INSNS that may throw, across which
   (REG n) is live.  */

#define REG_N_THROWING_CALLS_CROSSED(N) \
  (VEC_index (reg_info_p, reg_n_info, N)->throw_calls_crossed)

/* Total number of instructions at which (REG n) is live.
   The larger this is, the less priority (REG n) gets for
   allocation in a hard register (in global-alloc).
   This is set in flow.c and remains valid for the rest of the compilation
   of the function; it is used to control register allocation.

   local-alloc.c may alter this number to change the priority.

   Negative values are special.
   -1 is used to mark a pseudo reg which has a constant or memory equivalent
   and is used infrequently enough that it should not get a hard register.
   -2 is used to mark a pseudo reg for a parameter, when a frame pointer
   is not required.  global.c makes an allocno for this but does
   not try to assign a hard register to it.  */

#define REG_LIVE_LENGTH(N)				\
  (VEC_index (reg_info_p, reg_n_info, N)->live_length)

/* Vector of substitutions of register numbers,
   used to map pseudo regs into hardware regs.

   This can't be folded into reg_n_info without changing all of the
   machine dependent directories, since the reload functions
   in the machine dependent files access it.  */

extern short *reg_renumber;

/* Vector indexed by hardware reg saying whether that reg is ever used.  */

extern char regs_ever_live[FIRST_PSEUDO_REGISTER];

/* Like regs_ever_live, but saying whether reg is set by asm statements.  */

extern char regs_asm_clobbered[FIRST_PSEUDO_REGISTER];

/* Vector indexed by machine mode saying whether there are regs of that mode.  */

extern bool have_regs_of_mode [MAX_MACHINE_MODE];

/* For each hard register, the widest mode object that it can contain.
   This will be a MODE_INT mode if the register can hold integers.  Otherwise
   it will be a MODE_FLOAT or a MODE_CC mode, whichever is valid for the
   register.  */

extern enum machine_mode reg_raw_mode[FIRST_PSEUDO_REGISTER];

/* Vector indexed by regno; gives uid of first insn using that reg.
   This is computed by reg_scan for use by cse and loop.
   It is sometimes adjusted for subsequent changes during loop,
   but not adjusted by cse even if cse invalidates it.  */

#define REGNO_FIRST_UID(N) (VEC_index (reg_info_p, reg_n_info, N)->first_uid)

/* Vector indexed by regno; gives uid of last insn using that reg.
   This is computed by reg_scan for use by cse and loop.
   It is sometimes adjusted for subsequent changes during loop,
   but not adjusted by cse even if cse invalidates it.
   This is harmless since cse won't scan through a loop end.  */

#define REGNO_LAST_UID(N) (VEC_index (reg_info_p, reg_n_info, N)->last_uid)

/* List made of EXPR_LIST rtx's which gives pairs of pseudo registers
   that have to go in the same hard reg.  */
extern rtx regs_may_share;

/* Flag set by local-alloc or global-alloc if they decide to allocate
   something in a call-clobbered register.  */

extern int caller_save_needed;

/* Predicate to decide whether to give a hard reg to a pseudo which
   is referenced REFS times and would need to be saved and restored
   around a call CALLS times.  */

#ifndef CALLER_SAVE_PROFITABLE
#define CALLER_SAVE_PROFITABLE(REFS, CALLS)  (4 * (CALLS) < (REFS))
#endif

/* On most machines a register class is likely to be spilled if it
   only has one register.  */
#ifndef CLASS_LIKELY_SPILLED_P
#define CLASS_LIKELY_SPILLED_P(CLASS) (reg_class_size[(int) (CLASS)] == 1)
#endif

/* Select a register mode required for caller save of hard regno REGNO.  */
#ifndef HARD_REGNO_CALLER_SAVE_MODE
#define HARD_REGNO_CALLER_SAVE_MODE(REGNO, NREGS, MODE) \
  choose_hard_reg_mode (REGNO, NREGS, false)
#endif

/* Registers that get partially clobbered by a call in a given mode.
   These must not be call used registers.  */
#ifndef HARD_REGNO_CALL_PART_CLOBBERED
#define HARD_REGNO_CALL_PART_CLOBBERED(REGNO, MODE) 0
#endif

/* Allocate reg_n_info tables */
extern void allocate_reg_info (size_t, int, int);

/* Specify number of hard registers given machine mode occupy.  */
extern unsigned char hard_regno_nregs[FIRST_PSEUDO_REGISTER][MAX_MACHINE_MODE];

#endif /* GCC_REGS_H */
