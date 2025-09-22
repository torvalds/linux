/* Data flow analysis for GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation,
   Inc.

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

/* This file contains the data flow analysis pass of the compiler.  It
   computes data flow information which tells combine_instructions
   which insns to consider combining and controls register allocation.

   Additional data flow information that is too bulky to record is
   generated during the analysis, and is used at that time to create
   autoincrement and autodecrement addressing.

   The first step is dividing the function into basic blocks.
   find_basic_blocks does this.  Then life_analysis determines
   where each register is live and where it is dead.

   ** find_basic_blocks **

   find_basic_blocks divides the current function's rtl into basic
   blocks and constructs the CFG.  The blocks are recorded in the
   basic_block_info array; the CFG exists in the edge structures
   referenced by the blocks.

   find_basic_blocks also finds any unreachable loops and deletes them.

   ** life_analysis **

   life_analysis is called immediately after find_basic_blocks.
   It uses the basic block information to determine where each
   hard or pseudo register is live.

   ** live-register info **

   The information about where each register is live is in two parts:
   the REG_NOTES of insns, and the vector basic_block->global_live_at_start.

   basic_block->global_live_at_start has an element for each basic
   block, and the element is a bit-vector with a bit for each hard or
   pseudo register.  The bit is 1 if the register is live at the
   beginning of the basic block.

   Two types of elements can be added to an insn's REG_NOTES.
   A REG_DEAD note is added to an insn's REG_NOTES for any register
   that meets both of two conditions:  The value in the register is not
   needed in subsequent insns and the insn does not replace the value in
   the register (in the case of multi-word hard registers, the value in
   each register must be replaced by the insn to avoid a REG_DEAD note).

   In the vast majority of cases, an object in a REG_DEAD note will be
   used somewhere in the insn.  The (rare) exception to this is if an
   insn uses a multi-word hard register and only some of the registers are
   needed in subsequent insns.  In that case, REG_DEAD notes will be
   provided for those hard registers that are not subsequently needed.
   Partial REG_DEAD notes of this type do not occur when an insn sets
   only some of the hard registers used in such a multi-word operand;
   omitting REG_DEAD notes for objects stored in an insn is optional and
   the desire to do so does not justify the complexity of the partial
   REG_DEAD notes.

   REG_UNUSED notes are added for each register that is set by the insn
   but is unused subsequently (if every register set by the insn is unused
   and the insn does not reference memory or have some other side-effect,
   the insn is deleted instead).  If only part of a multi-word hard
   register is used in a subsequent insn, REG_UNUSED notes are made for
   the parts that will not be used.

   To determine which registers are live after any insn, one can
   start from the beginning of the basic block and scan insns, noting
   which registers are set by each insn and which die there.

   ** Other actions of life_analysis **

   life_analysis sets up the LOG_LINKS fields of insns because the
   information needed to do so is readily available.

   life_analysis deletes insns whose only effect is to store a value
   that is never used.

   life_analysis notices cases where a reference to a register as
   a memory address can be combined with a preceding or following
   incrementation or decrementation of the register.  The separate
   instruction to increment or decrement is deleted and the address
   is changed to a POST_INC or similar rtx.

   Each time an incrementing or decrementing address is created,
   a REG_INC element is added to the insn's REG_NOTES list.

   life_analysis fills in certain vectors containing information about
   register usage: REG_N_REFS, REG_N_DEATHS, REG_N_SETS, REG_LIVE_LENGTH,
   REG_N_CALLS_CROSSED, REG_N_THROWING_CALLS_CROSSED and REG_BASIC_BLOCK.

   life_analysis sets current_function_sp_is_unchanging if the function
   doesn't modify the stack pointer.  */

/* TODO:

   Split out from life_analysis:
	- local property discovery
	- global property computation
	- log links creation
	- pre/post modify transformation
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "insn-config.h"
#include "regs.h"
#include "flags.h"
#include "output.h"
#include "function.h"
#include "except.h"
#include "toplev.h"
#include "recog.h"
#include "expr.h"
#include "timevar.h"

#include "obstack.h"
#include "splay-tree.h"
#include "tree-pass.h"
#include "params.h"

#ifndef HAVE_epilogue
#define HAVE_epilogue 0
#endif
#ifndef HAVE_prologue
#define HAVE_prologue 0
#endif
#ifndef HAVE_sibcall_epilogue
#define HAVE_sibcall_epilogue 0
#endif

#ifndef EPILOGUE_USES
#define EPILOGUE_USES(REGNO)  0
#endif
#ifndef EH_USES
#define EH_USES(REGNO)  0
#endif

#ifdef HAVE_conditional_execution
#ifndef REVERSE_CONDEXEC_PREDICATES_P
#define REVERSE_CONDEXEC_PREDICATES_P(x, y) \
  (GET_CODE ((x)) == reversed_comparison_code ((y), NULL))
#endif
#endif

/* This is the maximum number of times we process any given block if the
   latest loop depth count is smaller than this number.  Only used for the
   failure strategy to avoid infinite loops in calculate_global_regs_live.  */
#define MAX_LIVENESS_ROUNDS 20

/* Nonzero if the second flow pass has completed.  */
int flow2_completed;

/* Maximum register number used in this function, plus one.  */

int max_regno;

/* Indexed by n, giving various register information */

VEC(reg_info_p,heap) *reg_n_info;

/* Regset of regs live when calls to `setjmp'-like functions happen.  */
/* ??? Does this exist only for the setjmp-clobbered warning message?  */

static regset regs_live_at_setjmp;

/* List made of EXPR_LIST rtx's which gives pairs of pseudo registers
   that have to go in the same hard reg.
   The first two regs in the list are a pair, and the next two
   are another pair, etc.  */
rtx regs_may_share;

/* Set of registers that may be eliminable.  These are handled specially
   in updating regs_ever_live.  */

static HARD_REG_SET elim_reg_set;

/* Holds information for tracking conditional register life information.  */
struct reg_cond_life_info
{
  /* A boolean expression of conditions under which a register is dead.  */
  rtx condition;
  /* Conditions under which a register is dead at the basic block end.  */
  rtx orig_condition;

  /* A boolean expression of conditions under which a register has been
     stored into.  */
  rtx stores;

  /* ??? Could store mask of bytes that are dead, so that we could finally
     track lifetimes of multi-word registers accessed via subregs.  */
};

/* For use in communicating between propagate_block and its subroutines.
   Holds all information needed to compute life and def-use information.  */

struct propagate_block_info
{
  /* The basic block we're considering.  */
  basic_block bb;

  /* Bit N is set if register N is conditionally or unconditionally live.  */
  regset reg_live;

  /* Bit N is set if register N is set this insn.  */
  regset new_set;

  /* Element N is the next insn that uses (hard or pseudo) register N
     within the current basic block; or zero, if there is no such insn.  */
  rtx *reg_next_use;

  /* Contains a list of all the MEMs we are tracking for dead store
     elimination.  */
  rtx mem_set_list;

  /* If non-null, record the set of registers set unconditionally in the
     basic block.  */
  regset local_set;

  /* If non-null, record the set of registers set conditionally in the
     basic block.  */
  regset cond_local_set;

#ifdef HAVE_conditional_execution
  /* Indexed by register number, holds a reg_cond_life_info for each
     register that is not unconditionally live or dead.  */
  splay_tree reg_cond_dead;

  /* Bit N is set if register N is in an expression in reg_cond_dead.  */
  regset reg_cond_reg;
#endif

  /* The length of mem_set_list.  */
  int mem_set_list_len;

  /* Nonzero if the value of CC0 is live.  */
  int cc0_live;

  /* Flags controlling the set of information propagate_block collects.  */
  int flags;
  /* Index of instruction being processed.  */
  int insn_num;
};

/* Number of dead insns removed.  */
static int ndead;

/* When PROP_REG_INFO set, array contains pbi->insn_num of instruction
   where given register died.  When the register is marked alive, we use the
   information to compute amount of instructions life range cross.
   (remember, we are walking backward).  This can be computed as current
   pbi->insn_num - reg_deaths[regno].
   At the end of processing each basic block, the remaining live registers
   are inspected and live ranges are increased same way so liverange of global
   registers are computed correctly.
  
   The array is maintained clear for dead registers, so it can be safely reused
   for next basic block without expensive memset of the whole array after
   reseting pbi->insn_num to 0.  */

static int *reg_deaths;

/* Forward declarations */
static int verify_wide_reg_1 (rtx *, void *);
static void verify_wide_reg (int, basic_block);
static void verify_local_live_at_start (regset, basic_block);
static void notice_stack_pointer_modification_1 (rtx, rtx, void *);
static void notice_stack_pointer_modification (void);
static void mark_reg (rtx, void *);
static void mark_regs_live_at_end (regset);
static void calculate_global_regs_live (sbitmap, sbitmap, int);
static void propagate_block_delete_insn (rtx);
static rtx propagate_block_delete_libcall (rtx, rtx);
static int insn_dead_p (struct propagate_block_info *, rtx, int, rtx);
static int libcall_dead_p (struct propagate_block_info *, rtx, rtx);
static void mark_set_regs (struct propagate_block_info *, rtx, rtx);
static void mark_set_1 (struct propagate_block_info *, enum rtx_code, rtx,
			rtx, rtx, int);
static int find_regno_partial (rtx *, void *);

#ifdef HAVE_conditional_execution
static int mark_regno_cond_dead (struct propagate_block_info *, int, rtx);
static void free_reg_cond_life_info (splay_tree_value);
static int flush_reg_cond_reg_1 (splay_tree_node, void *);
static void flush_reg_cond_reg (struct propagate_block_info *, int);
static rtx elim_reg_cond (rtx, unsigned int);
static rtx ior_reg_cond (rtx, rtx, int);
static rtx not_reg_cond (rtx);
static rtx and_reg_cond (rtx, rtx, int);
#endif
#ifdef AUTO_INC_DEC
static void attempt_auto_inc (struct propagate_block_info *, rtx, rtx, rtx,
			      rtx, rtx);
static void find_auto_inc (struct propagate_block_info *, rtx, rtx);
static int try_pre_increment_1 (struct propagate_block_info *, rtx);
static int try_pre_increment (rtx, rtx, HOST_WIDE_INT);
#endif
static void mark_used_reg (struct propagate_block_info *, rtx, rtx, rtx);
static void mark_used_regs (struct propagate_block_info *, rtx, rtx, rtx);
void debug_flow_info (void);
static void add_to_mem_set_list (struct propagate_block_info *, rtx);
static int invalidate_mems_from_autoinc (rtx *, void *);
static void invalidate_mems_from_set (struct propagate_block_info *, rtx);
static void clear_log_links (sbitmap);
static int count_or_remove_death_notes_bb (basic_block, int);
static void allocate_bb_life_data (void);

/* Return the INSN immediately following the NOTE_INSN_BASIC_BLOCK
   note associated with the BLOCK.  */

rtx
first_insn_after_basic_block_note (basic_block block)
{
  rtx insn;

  /* Get the first instruction in the block.  */
  insn = BB_HEAD (block);

  if (insn == NULL_RTX)
    return NULL_RTX;
  if (LABEL_P (insn))
    insn = NEXT_INSN (insn);
  gcc_assert (NOTE_INSN_BASIC_BLOCK_P (insn));

  return NEXT_INSN (insn);
}

/* Perform data flow analysis for the whole control flow graph.
   FLAGS is a set of PROP_* flags to be used in accumulating flow info.  */

void
life_analysis (int flags)
{
#ifdef ELIMINABLE_REGS
  int i;
  static const struct {const int from, to; } eliminables[] = ELIMINABLE_REGS;
#endif

  /* Record which registers will be eliminated.  We use this in
     mark_used_regs.  */

  CLEAR_HARD_REG_SET (elim_reg_set);

#ifdef ELIMINABLE_REGS
  for (i = 0; i < (int) ARRAY_SIZE (eliminables); i++)
    SET_HARD_REG_BIT (elim_reg_set, eliminables[i].from);
#else
  SET_HARD_REG_BIT (elim_reg_set, FRAME_POINTER_REGNUM);
#endif


#ifdef CANNOT_CHANGE_MODE_CLASS
  if (flags & PROP_REG_INFO)
    init_subregs_of_mode ();
#endif

  if (! optimize)
    flags &= ~(PROP_LOG_LINKS | PROP_AUTOINC | PROP_ALLOW_CFG_CHANGES);

  /* The post-reload life analysis have (on a global basis) the same
     registers live as was computed by reload itself.  elimination
     Otherwise offsets and such may be incorrect.

     Reload will make some registers as live even though they do not
     appear in the rtl.

     We don't want to create new auto-incs after reload, since they
     are unlikely to be useful and can cause problems with shared
     stack slots.  */
  if (reload_completed)
    flags &= ~(PROP_REG_INFO | PROP_AUTOINC);

  /* We want alias analysis information for local dead store elimination.  */
  if (optimize && (flags & PROP_SCAN_DEAD_STORES))
    init_alias_analysis ();

  /* Always remove no-op moves.  Do this before other processing so
     that we don't have to keep re-scanning them.  */
  delete_noop_moves ();

  /* Some targets can emit simpler epilogues if they know that sp was
     not ever modified during the function.  After reload, of course,
     we've already emitted the epilogue so there's no sense searching.  */
  if (! reload_completed)
    notice_stack_pointer_modification ();

  /* Allocate and zero out data structures that will record the
     data from lifetime analysis.  */
  allocate_reg_life_data ();
  allocate_bb_life_data ();

  /* Find the set of registers live on function exit.  */
  mark_regs_live_at_end (EXIT_BLOCK_PTR->il.rtl->global_live_at_start);

  /* "Update" life info from zero.  It'd be nice to begin the
     relaxation with just the exit and noreturn blocks, but that set
     is not immediately handy.  */

  if (flags & PROP_REG_INFO)
    {
      memset (regs_ever_live, 0, sizeof (regs_ever_live));
      memset (regs_asm_clobbered, 0, sizeof (regs_asm_clobbered));
    }
  update_life_info (NULL, UPDATE_LIFE_GLOBAL, flags);
  if (reg_deaths)
    {
      free (reg_deaths);
      reg_deaths = NULL;
    }

  /* Clean up.  */
  if (optimize && (flags & PROP_SCAN_DEAD_STORES))
    end_alias_analysis ();

  if (dump_file)
    dump_flow_info (dump_file, dump_flags);

  /* Removing dead insns should have made jumptables really dead.  */
  delete_dead_jumptables ();
}

/* A subroutine of verify_wide_reg, called through for_each_rtx.
   Search for REGNO.  If found, return 2 if it is not wider than
   word_mode.  */

static int
verify_wide_reg_1 (rtx *px, void *pregno)
{
  rtx x = *px;
  unsigned int regno = *(int *) pregno;

  if (REG_P (x) && REGNO (x) == regno)
    {
      if (GET_MODE_BITSIZE (GET_MODE (x)) <= BITS_PER_WORD)
	return 2;
      return 1;
    }
  return 0;
}

/* A subroutine of verify_local_live_at_start.  Search through insns
   of BB looking for register REGNO.  */

static void
verify_wide_reg (int regno, basic_block bb)
{
  rtx head = BB_HEAD (bb), end = BB_END (bb);

  while (1)
    {
      if (INSN_P (head))
	{
	  int r = for_each_rtx (&PATTERN (head), verify_wide_reg_1, &regno);
	  if (r == 1)
	    return;
	  if (r == 2)
	    break;
	}
      if (head == end)
	break;
      head = NEXT_INSN (head);
    }
  if (dump_file)
    {
      fprintf (dump_file, "Register %d died unexpectedly.\n", regno);
      dump_bb (bb, dump_file, 0);
    }
  internal_error ("internal consistency failure");
}

/* A subroutine of update_life_info.  Verify that there are no untoward
   changes in live_at_start during a local update.  */

static void
verify_local_live_at_start (regset new_live_at_start, basic_block bb)
{
  if (reload_completed)
    {
      /* After reload, there are no pseudos, nor subregs of multi-word
	 registers.  The regsets should exactly match.  */
      if (! REG_SET_EQUAL_P (new_live_at_start,
	    		     bb->il.rtl->global_live_at_start))
	{
	  if (dump_file)
	    {
	      fprintf (dump_file,
		       "live_at_start mismatch in bb %d, aborting\nNew:\n",
		       bb->index);
	      debug_bitmap_file (dump_file, new_live_at_start);
	      fputs ("Old:\n", dump_file);
	      dump_bb (bb, dump_file, 0);
	    }
	  internal_error ("internal consistency failure");
	}
    }
  else
    {
      unsigned i;
      reg_set_iterator rsi;

      /* Find the set of changed registers.  */
      XOR_REG_SET (new_live_at_start, bb->il.rtl->global_live_at_start);

      EXECUTE_IF_SET_IN_REG_SET (new_live_at_start, 0, i, rsi)
	{
	  /* No registers should die.  */
	  if (REGNO_REG_SET_P (bb->il.rtl->global_live_at_start, i))
	    {
	      if (dump_file)
		{
		  fprintf (dump_file,
			   "Register %d died unexpectedly.\n", i);
		  dump_bb (bb, dump_file, 0);
		}
	      internal_error ("internal consistency failure");
	    }
	  /* Verify that the now-live register is wider than word_mode.  */
	  verify_wide_reg (i, bb);
	}
    }
}

/* Updates life information starting with the basic blocks set in BLOCKS.
   If BLOCKS is null, consider it to be the universal set.

   If EXTENT is UPDATE_LIFE_LOCAL, such as after splitting or peepholing,
   we are only expecting local modifications to basic blocks.  If we find
   extra registers live at the beginning of a block, then we either killed
   useful data, or we have a broken split that wants data not provided.
   If we find registers removed from live_at_start, that means we have
   a broken peephole that is killing a register it shouldn't.

   ??? This is not true in one situation -- when a pre-reload splitter
   generates subregs of a multi-word pseudo, current life analysis will
   lose the kill.  So we _can_ have a pseudo go live.  How irritating.

   It is also not true when a peephole decides that it doesn't need one
   or more of the inputs.

   Including PROP_REG_INFO does not properly refresh regs_ever_live
   unless the caller resets it to zero.  */

int
update_life_info (sbitmap blocks, enum update_life_extent extent,
		  int prop_flags)
{
  regset tmp;
  unsigned i = 0;
  int stabilized_prop_flags = prop_flags;
  basic_block bb;

  tmp = ALLOC_REG_SET (&reg_obstack);
  ndead = 0;

  if ((prop_flags & PROP_REG_INFO) && !reg_deaths)
    reg_deaths = XCNEWVEC (int, max_regno);

  timevar_push ((extent == UPDATE_LIFE_LOCAL || blocks)
		? TV_LIFE_UPDATE : TV_LIFE);

  /* Changes to the CFG are only allowed when
     doing a global update for the entire CFG.  */
  gcc_assert (!(prop_flags & PROP_ALLOW_CFG_CHANGES)
	      || (extent != UPDATE_LIFE_LOCAL && !blocks));

  /* For a global update, we go through the relaxation process again.  */
  if (extent != UPDATE_LIFE_LOCAL)
    {
      for ( ; ; )
	{
	  int changed = 0;

	  calculate_global_regs_live (blocks, blocks,
				prop_flags & (PROP_SCAN_DEAD_CODE
					      | PROP_SCAN_DEAD_STORES
					      | PROP_ALLOW_CFG_CHANGES));

	  if ((prop_flags & (PROP_KILL_DEAD_CODE | PROP_ALLOW_CFG_CHANGES))
	      != (PROP_KILL_DEAD_CODE | PROP_ALLOW_CFG_CHANGES))
	    break;

	  /* Removing dead code may allow the CFG to be simplified which
	     in turn may allow for further dead code detection / removal.  */
	  FOR_EACH_BB_REVERSE (bb)
	    {
	      COPY_REG_SET (tmp, bb->il.rtl->global_live_at_end);
	      changed |= propagate_block (bb, tmp, NULL, NULL,
				prop_flags & (PROP_SCAN_DEAD_CODE
					      | PROP_SCAN_DEAD_STORES
					      | PROP_KILL_DEAD_CODE));
	    }

	  /* Don't pass PROP_SCAN_DEAD_CODE or PROP_KILL_DEAD_CODE to
	     subsequent propagate_block calls, since removing or acting as
	     removing dead code can affect global register liveness, which
	     is supposed to be finalized for this call after this loop.  */
	  stabilized_prop_flags
	    &= ~(PROP_SCAN_DEAD_CODE | PROP_SCAN_DEAD_STORES
		 | PROP_KILL_DEAD_CODE);

	  if (! changed)
	    break;

	  /* We repeat regardless of what cleanup_cfg says.  If there were
	     instructions deleted above, that might have been only a
	     partial improvement (see PARAM_MAX_FLOW_MEMORY_LOCATIONS  usage).
	     Further improvement may be possible.  */
	  cleanup_cfg (CLEANUP_EXPENSIVE);

	  /* Zap the life information from the last round.  If we don't
	     do this, we can wind up with registers that no longer appear
	     in the code being marked live at entry.  */
	  FOR_EACH_BB (bb)
	    {
	      CLEAR_REG_SET (bb->il.rtl->global_live_at_start);
	      CLEAR_REG_SET (bb->il.rtl->global_live_at_end);
	    }
	}

      /* If asked, remove notes from the blocks we'll update.  */
      if (extent == UPDATE_LIFE_GLOBAL_RM_NOTES)
	count_or_remove_death_notes (blocks,
				     prop_flags & PROP_POST_REGSTACK ? -1 : 1);
    }
  else
    {
      /* FIXME: This can go when the dataflow branch has been merged in.  */
      /* For a local update, if we are creating new REG_DEAD notes, then we
	 must delete the old ones first to avoid conflicts if they are
	 different.  */
      if (prop_flags & PROP_DEATH_NOTES)
	count_or_remove_death_notes (blocks,
				     prop_flags & PROP_POST_REGSTACK ? -1 : 1);
    }
				     

  /* Clear log links in case we are asked to (re)compute them.  */
  if (prop_flags & PROP_LOG_LINKS)
    clear_log_links (blocks);

  if (blocks)
    {
      sbitmap_iterator sbi;

      EXECUTE_IF_SET_IN_SBITMAP (blocks, 0, i, sbi)
	{
	  bb = BASIC_BLOCK (i);
	  if (bb)
	    {
	      /* The bitmap may be flawed in that one of the basic
		 blocks may have been deleted before you get here.  */
	      COPY_REG_SET (tmp, bb->il.rtl->global_live_at_end);
	      propagate_block (bb, tmp, NULL, NULL, stabilized_prop_flags);
	      
	      if (extent == UPDATE_LIFE_LOCAL)
		verify_local_live_at_start (tmp, bb);
	    }
	};
    }
  else
    {
      FOR_EACH_BB_REVERSE (bb)
	{
	  COPY_REG_SET (tmp, bb->il.rtl->global_live_at_end);

	  propagate_block (bb, tmp, NULL, NULL, stabilized_prop_flags);

	  if (extent == UPDATE_LIFE_LOCAL)
	    verify_local_live_at_start (tmp, bb);
	}
    }

  FREE_REG_SET (tmp);

  if (prop_flags & PROP_REG_INFO)
    {
      reg_set_iterator rsi;

      /* The only pseudos that are live at the beginning of the function
	 are those that were not set anywhere in the function.  local-alloc
	 doesn't know how to handle these correctly, so mark them as not
	 local to any one basic block.  */
      EXECUTE_IF_SET_IN_REG_SET (ENTRY_BLOCK_PTR->il.rtl->global_live_at_end,
				 FIRST_PSEUDO_REGISTER, i, rsi)
	REG_BASIC_BLOCK (i) = REG_BLOCK_GLOBAL;

      /* We have a problem with any pseudoreg that lives across the setjmp.
	 ANSI says that if a user variable does not change in value between
	 the setjmp and the longjmp, then the longjmp preserves it.  This
	 includes longjmp from a place where the pseudo appears dead.
	 (In principle, the value still exists if it is in scope.)
	 If the pseudo goes in a hard reg, some other value may occupy
	 that hard reg where this pseudo is dead, thus clobbering the pseudo.
	 Conclusion: such a pseudo must not go in a hard reg.  */
      EXECUTE_IF_SET_IN_REG_SET (regs_live_at_setjmp,
				 FIRST_PSEUDO_REGISTER, i, rsi)
	{
	  if (regno_reg_rtx[i] != 0)
	    {
	      REG_LIVE_LENGTH (i) = -1;
	      REG_BASIC_BLOCK (i) = REG_BLOCK_UNKNOWN;
	    }
	}
    }
  if (reg_deaths)
    {
      free (reg_deaths);
      reg_deaths = NULL;
    }
  timevar_pop ((extent == UPDATE_LIFE_LOCAL || blocks)
	       ? TV_LIFE_UPDATE : TV_LIFE);
  if (ndead && dump_file)
    fprintf (dump_file, "deleted %i dead insns\n", ndead);
  return ndead;
}

/* Update life information in all blocks where BB_DIRTY is set.  */

int
update_life_info_in_dirty_blocks (enum update_life_extent extent, int prop_flags)
{
  sbitmap update_life_blocks = sbitmap_alloc (last_basic_block);
  int n = 0;
  basic_block bb;
  int retval = 0;

  sbitmap_zero (update_life_blocks);
  FOR_EACH_BB (bb)
    {
      if (bb->flags & BB_DIRTY)
	{
	  SET_BIT (update_life_blocks, bb->index);
	  n++;
	}
    }

  if (n)
    retval = update_life_info (update_life_blocks, extent, prop_flags);

  sbitmap_free (update_life_blocks);
  return retval;
}

/* Free the variables allocated by find_basic_blocks.  */

void
free_basic_block_vars (void)
{
  if (basic_block_info)
    {
      clear_edges ();
      basic_block_info = NULL;
    }
  n_basic_blocks = 0;
  last_basic_block = 0;
  n_edges = 0;

  label_to_block_map = NULL;

  ENTRY_BLOCK_PTR->aux = NULL;
  ENTRY_BLOCK_PTR->il.rtl->global_live_at_end = NULL;
  EXIT_BLOCK_PTR->aux = NULL;
  EXIT_BLOCK_PTR->il.rtl->global_live_at_start = NULL;
}

/* Delete any insns that copy a register to itself.  */

int
delete_noop_moves (void)
{
  rtx insn, next;
  basic_block bb;
  int nnoops = 0;

  FOR_EACH_BB (bb)
    {
      for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb)); insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (INSN_P (insn) && noop_move_p (insn))
	    {
	      rtx note;

	      /* If we're about to remove the first insn of a libcall
		 then move the libcall note to the next real insn and
		 update the retval note.  */
	      if ((note = find_reg_note (insn, REG_LIBCALL, NULL_RTX))
		       && XEXP (note, 0) != insn)
		{
		  rtx new_libcall_insn = next_real_insn (insn);
		  rtx retval_note = find_reg_note (XEXP (note, 0),
						   REG_RETVAL, NULL_RTX);
		  REG_NOTES (new_libcall_insn)
		    = gen_rtx_INSN_LIST (REG_LIBCALL, XEXP (note, 0),
					 REG_NOTES (new_libcall_insn));
		  XEXP (retval_note, 0) = new_libcall_insn;
		}

	      delete_insn_and_edges (insn);
	      nnoops++;
	    }
	}
    }

  if (nnoops && dump_file)
    fprintf (dump_file, "deleted %i noop moves\n", nnoops);

  return nnoops;
}

/* Delete any jump tables never referenced.  We can't delete them at the
   time of removing tablejump insn as they are referenced by the preceding
   insns computing the destination, so we delay deleting and garbagecollect
   them once life information is computed.  */
void
delete_dead_jumptables (void)
{
  basic_block bb;

  /* A dead jump table does not belong to any basic block.  Scan insns
     between two adjacent basic blocks.  */
  FOR_EACH_BB (bb)
    {
      rtx insn, next;

      for (insn = NEXT_INSN (BB_END (bb));
	   insn && !NOTE_INSN_BASIC_BLOCK_P (insn);
	   insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (LABEL_P (insn)
	      && LABEL_NUSES (insn) == LABEL_PRESERVE_P (insn)
	      && JUMP_P (next)
	      && (GET_CODE (PATTERN (next)) == ADDR_VEC
		  || GET_CODE (PATTERN (next)) == ADDR_DIFF_VEC))
	    {
	      rtx label = insn, jump = next;

	      if (dump_file)
		fprintf (dump_file, "Dead jumptable %i removed\n",
			 INSN_UID (insn));

	      next = NEXT_INSN (next);
	      delete_insn (jump);
	      delete_insn (label);
	    }
	}
    }
}

/* Determine if the stack pointer is constant over the life of the function.
   Only useful before prologues have been emitted.  */

static void
notice_stack_pointer_modification_1 (rtx x, rtx pat ATTRIBUTE_UNUSED,
				     void *data ATTRIBUTE_UNUSED)
{
  if (x == stack_pointer_rtx
      /* The stack pointer is only modified indirectly as the result
	 of a push until later in flow.  See the comments in rtl.texi
	 regarding Embedded Side-Effects on Addresses.  */
      || (MEM_P (x)
	  && GET_RTX_CLASS (GET_CODE (XEXP (x, 0))) == RTX_AUTOINC
	  && XEXP (XEXP (x, 0), 0) == stack_pointer_rtx))
    current_function_sp_is_unchanging = 0;
}

static void
notice_stack_pointer_modification (void)
{
  basic_block bb;
  rtx insn;

  /* Assume that the stack pointer is unchanging if alloca hasn't
     been used.  */
  current_function_sp_is_unchanging = !current_function_calls_alloca;
  if (! current_function_sp_is_unchanging)
    return;

  FOR_EACH_BB (bb)
    FOR_BB_INSNS (bb, insn)
      {
	if (INSN_P (insn))
	  {
	    /* Check if insn modifies the stack pointer.  */
	    note_stores (PATTERN (insn),
			 notice_stack_pointer_modification_1,
			 NULL);
	    if (! current_function_sp_is_unchanging)
	      return;
	  }
      }
}

/* Mark a register in SET.  Hard registers in large modes get all
   of their component registers set as well.  */

static void
mark_reg (rtx reg, void *xset)
{
  regset set = (regset) xset;
  int regno = REGNO (reg);

  gcc_assert (GET_MODE (reg) != BLKmode);

  SET_REGNO_REG_SET (set, regno);
  if (regno < FIRST_PSEUDO_REGISTER)
    {
      int n = hard_regno_nregs[regno][GET_MODE (reg)];
      while (--n > 0)
	SET_REGNO_REG_SET (set, regno + n);
    }
}

/* Mark those regs which are needed at the end of the function as live
   at the end of the last basic block.  */

static void
mark_regs_live_at_end (regset set)
{
  unsigned int i;

  /* If exiting needs the right stack value, consider the stack pointer
     live at the end of the function.  */
  if ((HAVE_epilogue && epilogue_completed)
      || ! EXIT_IGNORE_STACK
      || (! FRAME_POINTER_REQUIRED
	  && ! current_function_calls_alloca
	  && flag_omit_frame_pointer)
      || current_function_sp_is_unchanging)
    {
      SET_REGNO_REG_SET (set, STACK_POINTER_REGNUM);
    }

  /* Mark the frame pointer if needed at the end of the function.  If
     we end up eliminating it, it will be removed from the live list
     of each basic block by reload.  */

  if (! reload_completed || frame_pointer_needed)
    {
      SET_REGNO_REG_SET (set, FRAME_POINTER_REGNUM);
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
      /* If they are different, also mark the hard frame pointer as live.  */
      if (! LOCAL_REGNO (HARD_FRAME_POINTER_REGNUM))
	SET_REGNO_REG_SET (set, HARD_FRAME_POINTER_REGNUM);
#endif
    }

#ifndef PIC_OFFSET_TABLE_REG_CALL_CLOBBERED
  /* Many architectures have a GP register even without flag_pic.
     Assume the pic register is not in use, or will be handled by
     other means, if it is not fixed.  */
  if ((unsigned) PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM
      && fixed_regs[PIC_OFFSET_TABLE_REGNUM])
    SET_REGNO_REG_SET (set, PIC_OFFSET_TABLE_REGNUM);
#endif

  /* Mark all global registers, and all registers used by the epilogue
     as being live at the end of the function since they may be
     referenced by our caller.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if (global_regs[i] || EPILOGUE_USES (i))
      SET_REGNO_REG_SET (set, i);

  if (HAVE_epilogue && epilogue_completed)
    {
      /* Mark all call-saved registers that we actually used.  */
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (regs_ever_live[i] && ! LOCAL_REGNO (i)
	    && ! TEST_HARD_REG_BIT (regs_invalidated_by_call, i))
	  SET_REGNO_REG_SET (set, i);
    }

#ifdef EH_RETURN_DATA_REGNO
  /* Mark the registers that will contain data for the handler.  */
  if (reload_completed && current_function_calls_eh_return)
    for (i = 0; ; ++i)
      {
	unsigned regno = EH_RETURN_DATA_REGNO(i);
	if (regno == INVALID_REGNUM)
	  break;
	SET_REGNO_REG_SET (set, regno);
      }
#endif
#ifdef EH_RETURN_STACKADJ_RTX
  if ((! HAVE_epilogue || ! epilogue_completed)
      && current_function_calls_eh_return)
    {
      rtx tmp = EH_RETURN_STACKADJ_RTX;
      if (tmp && REG_P (tmp))
	mark_reg (tmp, set);
    }
#endif
#ifdef EH_RETURN_HANDLER_RTX
  if ((! HAVE_epilogue || ! epilogue_completed)
      && current_function_calls_eh_return)
    {
      rtx tmp = EH_RETURN_HANDLER_RTX;
      if (tmp && REG_P (tmp))
	mark_reg (tmp, set);
    }
#endif

  /* Mark function return value.  */
  diddle_return_value (mark_reg, set);
}

/* Propagate global life info around the graph of basic blocks.  Begin
   considering blocks with their corresponding bit set in BLOCKS_IN.
   If BLOCKS_IN is null, consider it the universal set.

   BLOCKS_OUT is set for every block that was changed.  */

static void
calculate_global_regs_live (sbitmap blocks_in, sbitmap blocks_out, int flags)
{
  basic_block *queue, *qhead, *qtail, *qend, bb;
  regset tmp, new_live_at_end, invalidated_by_eh_edge;
  regset registers_made_dead;
  bool failure_strategy_required = false;
  int *block_accesses;

  /* The registers that are modified within this in block.  */
  regset *local_sets;

  /* The registers that are conditionally modified within this block.
     In other words, regs that are set only as part of a COND_EXEC.  */
  regset *cond_local_sets;

  unsigned int i;

  /* Some passes used to forget clear aux field of basic block causing
     sick behavior here.  */
#ifdef ENABLE_CHECKING
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
    gcc_assert (!bb->aux);
#endif

  tmp = ALLOC_REG_SET (&reg_obstack);
  new_live_at_end = ALLOC_REG_SET (&reg_obstack);
  invalidated_by_eh_edge = ALLOC_REG_SET (&reg_obstack);
  registers_made_dead = ALLOC_REG_SET (&reg_obstack);

  /* Inconveniently, this is only readily available in hard reg set form.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; ++i)
    if (TEST_HARD_REG_BIT (regs_invalidated_by_call, i))
      SET_REGNO_REG_SET (invalidated_by_eh_edge, i);

  /* The exception handling registers die at eh edges.  */
#ifdef EH_RETURN_DATA_REGNO
  for (i = 0; ; ++i)
    {
      unsigned regno = EH_RETURN_DATA_REGNO (i);
      if (regno == INVALID_REGNUM)
	break;
      SET_REGNO_REG_SET (invalidated_by_eh_edge, regno);
    }
#endif

  /* Allocate space for the sets of local properties.  */
  local_sets = XCNEWVEC (bitmap, last_basic_block);
  cond_local_sets = XCNEWVEC (bitmap, last_basic_block);

  /* Create a worklist.  Allocate an extra slot for the `head == tail'
     style test for an empty queue doesn't work with a full queue.  */
  queue = XNEWVEC (basic_block, n_basic_blocks + 1);
  qtail = queue;
  qhead = qend = queue + n_basic_blocks;

  /* Queue the blocks set in the initial mask.  Do this in reverse block
     number order so that we are more likely for the first round to do
     useful work.  We use AUX non-null to flag that the block is queued.  */
  if (blocks_in)
    {
      FOR_EACH_BB (bb)
	if (TEST_BIT (blocks_in, bb->index))
	  {
	    *--qhead = bb;
	    bb->aux = bb;
	  }
    }
  else
    {
      FOR_EACH_BB (bb)
	{
	  *--qhead = bb;
	  bb->aux = bb;
	}
    }

  block_accesses = XCNEWVEC (int, last_basic_block);
  
  /* We clean aux when we remove the initially-enqueued bbs, but we
     don't enqueue ENTRY and EXIT initially, so clean them upfront and
     unconditionally.  */
  ENTRY_BLOCK_PTR->aux = EXIT_BLOCK_PTR->aux = NULL;

  if (blocks_out)
    sbitmap_zero (blocks_out);

  /* We work through the queue until there are no more blocks.  What
     is live at the end of this block is precisely the union of what
     is live at the beginning of all its successors.  So, we set its
     GLOBAL_LIVE_AT_END field based on the GLOBAL_LIVE_AT_START field
     for its successors.  Then, we compute GLOBAL_LIVE_AT_START for
     this block by walking through the instructions in this block in
     reverse order and updating as we go.  If that changed
     GLOBAL_LIVE_AT_START, we add the predecessors of the block to the
     queue; they will now need to recalculate GLOBAL_LIVE_AT_END.

     We are guaranteed to terminate, because GLOBAL_LIVE_AT_START
     never shrinks.  If a register appears in GLOBAL_LIVE_AT_START, it
     must either be live at the end of the block, or used within the
     block.  In the latter case, it will certainly never disappear
     from GLOBAL_LIVE_AT_START.  In the former case, the register
     could go away only if it disappeared from GLOBAL_LIVE_AT_START
     for one of the successor blocks.  By induction, that cannot
     occur.

     ??? This reasoning doesn't work if we start from non-empty initial
     GLOBAL_LIVE_AT_START sets.  And there are actually two problems:
       1) Updating may not terminate (endless oscillation).
       2) Even if it does (and it usually does), the resulting information
	  may be inaccurate.  Consider for example the following case:

	  a = ...;
	  while (...) {...}  -- 'a' not mentioned at all
	  ... = a;

	  If the use of 'a' is deleted between two calculations of liveness
	  information and the initial sets are not cleared, the information
	  about a's liveness will get stuck inside the loop and the set will
	  appear not to be dead.

     We do not attempt to solve 2) -- the information is conservatively
     correct (i.e. we never claim that something live is dead) and the
     amount of optimization opportunities missed due to this problem is
     not significant.

     1) is more serious.  In order to fix it, we monitor the number of times
     each block is processed.  Once one of the blocks has been processed more
     times than the maximum number of rounds, we use the following strategy:
     When a register disappears from one of the sets, we add it to a MAKE_DEAD
     set, remove all registers in this set from all GLOBAL_LIVE_AT_* sets and
     add the blocks with changed sets into the queue.  Thus we are guaranteed
     to terminate (the worst case corresponds to all registers in MADE_DEAD,
     in which case the original reasoning above is valid), but in general we
     only fix up a few offending registers.

     The maximum number of rounds for computing liveness is the largest of
     MAX_LIVENESS_ROUNDS and the latest loop depth count for this function.  */

  while (qhead != qtail)
    {
      int rescan, changed;
      basic_block bb;
      edge e;
      edge_iterator ei;

      bb = *qhead++;
      if (qhead == qend)
	qhead = queue;
      bb->aux = NULL;

      /* Should we start using the failure strategy?  */
      if (bb != ENTRY_BLOCK_PTR)
	{
	  int max_liveness_rounds =
	    MAX (MAX_LIVENESS_ROUNDS, cfun->max_loop_depth);

	  block_accesses[bb->index]++;
	  if (block_accesses[bb->index] > max_liveness_rounds)
	    failure_strategy_required = true;
	}

      /* Begin by propagating live_at_start from the successor blocks.  */
      CLEAR_REG_SET (new_live_at_end);

      if (EDGE_COUNT (bb->succs) > 0)
	FOR_EACH_EDGE (e, ei, bb->succs)
	  {
	    basic_block sb = e->dest;

	    /* Call-clobbered registers die across exception and
	       call edges.  */
	    /* ??? Abnormal call edges ignored for the moment, as this gets
	       confused by sibling call edges, which crashes reg-stack.  */
	    if (e->flags & EDGE_EH)
	      bitmap_ior_and_compl_into (new_live_at_end,
					 sb->il.rtl->global_live_at_start,
					 invalidated_by_eh_edge);
	    else
	      IOR_REG_SET (new_live_at_end, sb->il.rtl->global_live_at_start);

	    /* If a target saves one register in another (instead of on
	       the stack) the save register will need to be live for EH.  */
	    if (e->flags & EDGE_EH)
	      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
		if (EH_USES (i))
		  SET_REGNO_REG_SET (new_live_at_end, i);
	  }
      else
	{
	  /* This might be a noreturn function that throws.  And
	     even if it isn't, getting the unwind info right helps
	     debugging.  */
	  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	    if (EH_USES (i))
	      SET_REGNO_REG_SET (new_live_at_end, i);
	}

      /* The all-important stack pointer must always be live.  */
      SET_REGNO_REG_SET (new_live_at_end, STACK_POINTER_REGNUM);

      /* Before reload, there are a few registers that must be forced
	 live everywhere -- which might not already be the case for
	 blocks within infinite loops.  */
      if (! reload_completed)
	{
	  /* Any reference to any pseudo before reload is a potential
	     reference of the frame pointer.  */
	  SET_REGNO_REG_SET (new_live_at_end, FRAME_POINTER_REGNUM);

#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	  /* Pseudos with argument area equivalences may require
	     reloading via the argument pointer.  */
	  if (fixed_regs[ARG_POINTER_REGNUM])
	    SET_REGNO_REG_SET (new_live_at_end, ARG_POINTER_REGNUM);
#endif

	  /* Any constant, or pseudo with constant equivalences, may
	     require reloading from memory using the pic register.  */
	  if ((unsigned) PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM
	      && fixed_regs[PIC_OFFSET_TABLE_REGNUM])
	    SET_REGNO_REG_SET (new_live_at_end, PIC_OFFSET_TABLE_REGNUM);
	}

      if (bb == ENTRY_BLOCK_PTR)
	{
	  COPY_REG_SET (bb->il.rtl->global_live_at_end, new_live_at_end);
	  continue;
	}

      /* On our first pass through this block, we'll go ahead and continue.
	 Recognize first pass by checking if local_set is NULL for this
         basic block.  On subsequent passes, we get to skip out early if
	 live_at_end wouldn't have changed.  */

      if (local_sets[bb->index] == NULL)
	{
	  local_sets[bb->index] = ALLOC_REG_SET (&reg_obstack);
	  cond_local_sets[bb->index] = ALLOC_REG_SET (&reg_obstack);
	  rescan = 1;
	}
      else
	{
	  /* If any bits were removed from live_at_end, we'll have to
	     rescan the block.  This wouldn't be necessary if we had
	     precalculated local_live, however with PROP_SCAN_DEAD_CODE
	     local_live is really dependent on live_at_end.  */
	  rescan = bitmap_intersect_compl_p (bb->il.rtl->global_live_at_end,
					     new_live_at_end);

	  if (!rescan)
	    {
	      regset cond_local_set;

	       /* If any of the registers in the new live_at_end set are
		  conditionally set in this basic block, we must rescan.
		  This is because conditional lifetimes at the end of the
		  block do not just take the live_at_end set into
		  account, but also the liveness at the start of each
		  successor block.  We can miss changes in those sets if
		  we only compare the new live_at_end against the
		  previous one.  */
	      cond_local_set = cond_local_sets[bb->index];
	      rescan = bitmap_intersect_p (new_live_at_end, cond_local_set);
	    }

	  if (!rescan)
	    {
	      regset local_set;

	      /* Find the set of changed bits.  Take this opportunity
		 to notice that this set is empty and early out.  */
	      bitmap_xor (tmp, bb->il.rtl->global_live_at_end, new_live_at_end);
	      if (bitmap_empty_p (tmp))
		continue;
  
	      /* If any of the changed bits overlap with local_sets[bb],
 		 we'll have to rescan the block.  */
	      local_set = local_sets[bb->index];
	      rescan = bitmap_intersect_p (tmp, local_set);
	    }
	}

      /* Let our caller know that BB changed enough to require its
	 death notes updated.  */
      if (blocks_out)
	SET_BIT (blocks_out, bb->index);

      if (! rescan)
	{
	  /* Add to live_at_start the set of all registers in
	     new_live_at_end that aren't in the old live_at_end.  */
	  
	  changed = bitmap_ior_and_compl_into (bb->il.rtl->global_live_at_start,
					       new_live_at_end,
					       bb->il.rtl->global_live_at_end);
	  COPY_REG_SET (bb->il.rtl->global_live_at_end, new_live_at_end);
	  if (! changed)
	    continue;
	}
      else
	{
	  COPY_REG_SET (bb->il.rtl->global_live_at_end, new_live_at_end);

	  /* Rescan the block insn by insn to turn (a copy of) live_at_end
	     into live_at_start.  */
	  propagate_block (bb, new_live_at_end,
			   local_sets[bb->index],
			   cond_local_sets[bb->index],
			   flags);

	  /* If live_at start didn't change, no need to go farther.  */
	  if (REG_SET_EQUAL_P (bb->il.rtl->global_live_at_start,
			       new_live_at_end))
	    continue;

	  if (failure_strategy_required)
	    {
	      /* Get the list of registers that were removed from the
	         bb->global_live_at_start set.  */
	      bitmap_and_compl (tmp, bb->il.rtl->global_live_at_start,
				new_live_at_end);
	      if (!bitmap_empty_p (tmp))
		{
		  bool pbb_changed;
		  basic_block pbb;
                
		  /* It should not happen that one of registers we have
		     removed last time is disappears again before any other
		     register does.  */
		  pbb_changed = bitmap_ior_into (registers_made_dead, tmp);
		  gcc_assert (pbb_changed);

		  /* Now remove the registers from all sets.  */
		  FOR_EACH_BB (pbb)
		    {
		      pbb_changed = false;

		      pbb_changed
			|= bitmap_and_compl_into
			    (pbb->il.rtl->global_live_at_start,
			     registers_made_dead);
		      pbb_changed
			|= bitmap_and_compl_into
			    (pbb->il.rtl->global_live_at_end,
			     registers_made_dead);
		      if (!pbb_changed)
			continue;

		      /* Note the (possible) change.  */
		      if (blocks_out)
			SET_BIT (blocks_out, pbb->index);

		      /* Makes sure to really rescan the block.  */
		      if (local_sets[pbb->index])
			{
			  FREE_REG_SET (local_sets[pbb->index]);
			  FREE_REG_SET (cond_local_sets[pbb->index]);
			  local_sets[pbb->index] = 0;
			}

		      /* Add it to the queue.  */
		      if (pbb->aux == NULL)
			{
			  *qtail++ = pbb;
			  if (qtail == qend)
			    qtail = queue;
			  pbb->aux = pbb;
			}
		    }
		  continue;
		}
	    } /* end of failure_strategy_required */

	  COPY_REG_SET (bb->il.rtl->global_live_at_start, new_live_at_end);
	}

      /* Queue all predecessors of BB so that we may re-examine
	 their live_at_end.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  basic_block pb = e->src;

	  gcc_assert ((e->flags & EDGE_FAKE) == 0);

	  if (pb->aux == NULL)
	    {
	      *qtail++ = pb;
	      if (qtail == qend)
		qtail = queue;
	      pb->aux = pb;
	    }
	}
    }

  FREE_REG_SET (tmp);
  FREE_REG_SET (new_live_at_end);
  FREE_REG_SET (invalidated_by_eh_edge);
  FREE_REG_SET (registers_made_dead);

  if (blocks_out)
    {
      sbitmap_iterator sbi;

      EXECUTE_IF_SET_IN_SBITMAP (blocks_out, 0, i, sbi)
	{
	  basic_block bb = BASIC_BLOCK (i);
 	  FREE_REG_SET (local_sets[bb->index]);
 	  FREE_REG_SET (cond_local_sets[bb->index]);
	};
    }
  else
    {
      FOR_EACH_BB (bb)
	{
 	  FREE_REG_SET (local_sets[bb->index]);
 	  FREE_REG_SET (cond_local_sets[bb->index]);
	}
    }

  free (block_accesses);
  free (queue);
  free (cond_local_sets);
  free (local_sets);
}


/* This structure is used to pass parameters to and from the
   the function find_regno_partial(). It is used to pass in the
   register number we are looking, as well as to return any rtx
   we find.  */

typedef struct {
  unsigned regno_to_find;
  rtx retval;
} find_regno_partial_param;


/* Find the rtx for the reg numbers specified in 'data' if it is
   part of an expression which only uses part of the register.  Return
   it in the structure passed in.  */
static int
find_regno_partial (rtx *ptr, void *data)
{
  find_regno_partial_param *param = (find_regno_partial_param *)data;
  unsigned reg = param->regno_to_find;
  param->retval = NULL_RTX;

  if (*ptr == NULL_RTX)
    return 0;

  switch (GET_CODE (*ptr))
    {
    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
    case STRICT_LOW_PART:
      if (REG_P (XEXP (*ptr, 0)) && REGNO (XEXP (*ptr, 0)) == reg)
	{
	  param->retval = XEXP (*ptr, 0);
	  return 1;
	}
      break;

    case SUBREG:
      if (REG_P (SUBREG_REG (*ptr))
	  && REGNO (SUBREG_REG (*ptr)) == reg)
	{
	  param->retval = SUBREG_REG (*ptr);
	  return 1;
	}
      break;

    default:
      break;
    }

  return 0;
}

/* Process all immediate successors of the entry block looking for pseudo
   registers which are live on entry. Find all of those whose first
   instance is a partial register reference of some kind, and initialize
   them to 0 after the entry block.  This will prevent bit sets within
   registers whose value is unknown, and may contain some kind of sticky
   bits we don't want.  */

static int
initialize_uninitialized_subregs (void)
{
  rtx insn;
  edge e;
  unsigned reg, did_something = 0;
  find_regno_partial_param param;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
    {
      basic_block bb = e->dest;
      regset map = bb->il.rtl->global_live_at_start;
      reg_set_iterator rsi;

      EXECUTE_IF_SET_IN_REG_SET (map, FIRST_PSEUDO_REGISTER, reg, rsi)
	{
	  int uid = REGNO_FIRST_UID (reg);
	  rtx i;

	  /* Find an insn which mentions the register we are looking for.
	     Its preferable to have an instance of the register's rtl since
	     there may be various flags set which we need to duplicate.
	     If we can't find it, its probably an automatic whose initial
	     value doesn't matter, or hopefully something we don't care about.  */
	  for (i = get_insns (); i && INSN_UID (i) != uid; i = NEXT_INSN (i))
	    ;
	  if (i != NULL_RTX)
	    {
	      /* Found the insn, now get the REG rtx, if we can.  */
	      param.regno_to_find = reg;
	      for_each_rtx (&i, find_regno_partial, &param);
	      if (param.retval != NULL_RTX)
		{
		  start_sequence ();
		  emit_move_insn (param.retval,
				  CONST0_RTX (GET_MODE (param.retval)));
		  insn = get_insns ();
		  end_sequence ();
		  insert_insn_on_edge (insn, e);
		  did_something = 1;
		}
	    }
	}
    }

  if (did_something)
    commit_edge_insertions ();
  return did_something;
}


/* Subroutines of life analysis.  */

/* Allocate the permanent data structures that represent the results
   of life analysis.  */

static void
allocate_bb_life_data (void)
{
  basic_block bb;

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, NULL, next_bb)
    {
      if (bb->il.rtl->global_live_at_start)
	{
	  CLEAR_REG_SET (bb->il.rtl->global_live_at_start);
	  CLEAR_REG_SET (bb->il.rtl->global_live_at_end);
	}
      else
	{
	  bb->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
	  bb->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
	}
    }

  regs_live_at_setjmp = ALLOC_REG_SET (&reg_obstack);
}

void
allocate_reg_life_data (void)
{
  int i;

  max_regno = max_reg_num ();
  gcc_assert (!reg_deaths);
  reg_deaths = XCNEWVEC (int, max_regno);

  /* Recalculate the register space, in case it has grown.  Old style
     vector oriented regsets would set regset_{size,bytes} here also.  */
  allocate_reg_info (max_regno, FALSE, FALSE);

  /* Reset all the data we'll collect in propagate_block and its
     subroutines.  */
  for (i = 0; i < max_regno; i++)
    {
      REG_N_SETS (i) = 0;
      REG_N_REFS (i) = 0;
      REG_N_DEATHS (i) = 0;
      REG_N_CALLS_CROSSED (i) = 0;
      REG_N_THROWING_CALLS_CROSSED (i) = 0;
      REG_LIVE_LENGTH (i) = 0;
      REG_FREQ (i) = 0;
      REG_BASIC_BLOCK (i) = REG_BLOCK_UNKNOWN;
    }
}

/* Delete dead instructions for propagate_block.  */

static void
propagate_block_delete_insn (rtx insn)
{
  rtx inote = find_reg_note (insn, REG_LABEL, NULL_RTX);

  /* If the insn referred to a label, and that label was attached to
     an ADDR_VEC, it's safe to delete the ADDR_VEC.  In fact, it's
     pretty much mandatory to delete it, because the ADDR_VEC may be
     referencing labels that no longer exist.

     INSN may reference a deleted label, particularly when a jump
     table has been optimized into a direct jump.  There's no
     real good way to fix up the reference to the deleted label
     when the label is deleted, so we just allow it here.  */

  if (inote && LABEL_P (inote))
    {
      rtx label = XEXP (inote, 0);
      rtx next;

      /* The label may be forced if it has been put in the constant
	 pool.  If that is the only use we must discard the table
	 jump following it, but not the label itself.  */
      if (LABEL_NUSES (label) == 1 + LABEL_PRESERVE_P (label)
	  && (next = next_nonnote_insn (label)) != NULL
	  && JUMP_P (next)
	  && (GET_CODE (PATTERN (next)) == ADDR_VEC
	      || GET_CODE (PATTERN (next)) == ADDR_DIFF_VEC))
	{
	  rtx pat = PATTERN (next);
	  int diff_vec_p = GET_CODE (pat) == ADDR_DIFF_VEC;
	  int len = XVECLEN (pat, diff_vec_p);
	  int i;

	  for (i = 0; i < len; i++)
	    LABEL_NUSES (XEXP (XVECEXP (pat, diff_vec_p, i), 0))--;

	  delete_insn_and_edges (next);
	  ndead++;
	}
    }

  delete_insn_and_edges (insn);
  ndead++;
}

/* Delete dead libcalls for propagate_block.  Return the insn
   before the libcall.  */

static rtx
propagate_block_delete_libcall (rtx insn, rtx note)
{
  rtx first = XEXP (note, 0);
  rtx before = PREV_INSN (first);

  delete_insn_chain_and_edges (first, insn);
  ndead++;
  return before;
}

/* Update the life-status of regs for one insn.  Return the previous insn.  */

rtx
propagate_one_insn (struct propagate_block_info *pbi, rtx insn)
{
  rtx prev = PREV_INSN (insn);
  int flags = pbi->flags;
  int insn_is_dead = 0;
  int libcall_is_dead = 0;
  rtx note;
  unsigned i;

  if (! INSN_P (insn))
    return prev;

  note = find_reg_note (insn, REG_RETVAL, NULL_RTX);
  if (flags & PROP_SCAN_DEAD_CODE)
    {
      insn_is_dead = insn_dead_p (pbi, PATTERN (insn), 0, REG_NOTES (insn));
      libcall_is_dead = (insn_is_dead && note != 0
			 && libcall_dead_p (pbi, note, insn));
    }

  /* If an instruction consists of just dead store(s) on final pass,
     delete it.  */
  if ((flags & PROP_KILL_DEAD_CODE) && insn_is_dead)
    {
      /* If we're trying to delete a prologue or epilogue instruction
	 that isn't flagged as possibly being dead, something is wrong.
	 But if we are keeping the stack pointer depressed, we might well
	 be deleting insns that are used to compute the amount to update
	 it by, so they are fine.  */
      if (reload_completed
	  && !(TREE_CODE (TREE_TYPE (current_function_decl)) == FUNCTION_TYPE
		&& (TYPE_RETURNS_STACK_DEPRESSED
		    (TREE_TYPE (current_function_decl))))
	  && (((HAVE_epilogue || HAVE_prologue)
	       && prologue_epilogue_contains (insn))
	      || (HAVE_sibcall_epilogue
		  && sibcall_epilogue_contains (insn)))
	  && find_reg_note (insn, REG_MAYBE_DEAD, NULL_RTX) == 0)
	fatal_insn ("Attempt to delete prologue/epilogue insn:", insn);

      /* Record sets.  Do this even for dead instructions, since they
	 would have killed the values if they hadn't been deleted.  To
	 be consistent, we also have to emit a clobber when we delete
	 an insn that clobbers a live register.  */
      pbi->flags |= PROP_DEAD_INSN;
      mark_set_regs (pbi, PATTERN (insn), insn);
      pbi->flags &= ~PROP_DEAD_INSN;

      /* CC0 is now known to be dead.  Either this insn used it,
	 in which case it doesn't anymore, or clobbered it,
	 so the next insn can't use it.  */
      pbi->cc0_live = 0;

      if (libcall_is_dead)
	prev = propagate_block_delete_libcall (insn, note);
      else
	{

	/* If INSN contains a RETVAL note and is dead, but the libcall
	   as a whole is not dead, then we want to remove INSN, but
	   not the whole libcall sequence.

	   However, we need to also remove the dangling REG_LIBCALL
	   note so that we do not have mis-matched LIBCALL/RETVAL
	   notes.  In theory we could find a new location for the
	   REG_RETVAL note, but it hardly seems worth the effort.

	   NOTE at this point will be the RETVAL note if it exists.  */
	  if (note)
	    {
	      rtx libcall_note;

	      libcall_note
		= find_reg_note (XEXP (note, 0), REG_LIBCALL, NULL_RTX);
	      remove_note (XEXP (note, 0), libcall_note);
	    }

	  /* Similarly if INSN contains a LIBCALL note, remove the
	     dangling REG_RETVAL note.  */
	  note = find_reg_note (insn, REG_LIBCALL, NULL_RTX);
	  if (note)
	    {
	      rtx retval_note;

	      retval_note
		= find_reg_note (XEXP (note, 0), REG_RETVAL, NULL_RTX);
	      remove_note (XEXP (note, 0), retval_note);
	    }

	  /* Now delete INSN.  */
	  propagate_block_delete_insn (insn);
	}

      return prev;
    }

  /* See if this is an increment or decrement that can be merged into
     a following memory address.  */
#ifdef AUTO_INC_DEC
  {
    rtx x = single_set (insn);

    /* Does this instruction increment or decrement a register?  */
    if ((flags & PROP_AUTOINC)
	&& x != 0
	&& REG_P (SET_DEST (x))
	&& (GET_CODE (SET_SRC (x)) == PLUS
	    || GET_CODE (SET_SRC (x)) == MINUS)
	&& XEXP (SET_SRC (x), 0) == SET_DEST (x)
	&& GET_CODE (XEXP (SET_SRC (x), 1)) == CONST_INT
	/* Ok, look for a following memory ref we can combine with.
	   If one is found, change the memory ref to a PRE_INC
	   or PRE_DEC, cancel this insn, and return 1.
	   Return 0 if nothing has been done.  */
	&& try_pre_increment_1 (pbi, insn))
      return prev;
  }
#endif /* AUTO_INC_DEC */

  CLEAR_REG_SET (pbi->new_set);

  /* If this is not the final pass, and this insn is copying the value of
     a library call and it's dead, don't scan the insns that perform the
     library call, so that the call's arguments are not marked live.  */
  if (libcall_is_dead)
    {
      /* Record the death of the dest reg.  */
      mark_set_regs (pbi, PATTERN (insn), insn);

      insn = XEXP (note, 0);
      return PREV_INSN (insn);
    }
  else if (GET_CODE (PATTERN (insn)) == SET
	   && SET_DEST (PATTERN (insn)) == stack_pointer_rtx
	   && GET_CODE (SET_SRC (PATTERN (insn))) == PLUS
	   && XEXP (SET_SRC (PATTERN (insn)), 0) == stack_pointer_rtx
	   && GET_CODE (XEXP (SET_SRC (PATTERN (insn)), 1)) == CONST_INT)
    {
      /* We have an insn to pop a constant amount off the stack.
         (Such insns use PLUS regardless of the direction of the stack,
         and any insn to adjust the stack by a constant is always a pop
	 or part of a push.)
         These insns, if not dead stores, have no effect on life, though
         they do have an effect on the memory stores we are tracking.  */
      invalidate_mems_from_set (pbi, stack_pointer_rtx);
      /* Still, we need to update local_set, lest ifcvt.c:dead_or_predicable
	 concludes that the stack pointer is not modified.  */
      mark_set_regs (pbi, PATTERN (insn), insn);
    }
  else
    {
      /* Any regs live at the time of a call instruction must not go
	 in a register clobbered by calls.  Find all regs now live and
	 record this for them.  */

      if (CALL_P (insn) && (flags & PROP_REG_INFO))
	{
	  reg_set_iterator rsi;
	  EXECUTE_IF_SET_IN_REG_SET (pbi->reg_live, 0, i, rsi)
	    REG_N_CALLS_CROSSED (i)++;
          if (can_throw_internal (insn))
	    EXECUTE_IF_SET_IN_REG_SET (pbi->reg_live, 0, i, rsi)
	      REG_N_THROWING_CALLS_CROSSED (i)++;
	}

      /* Record sets.  Do this even for dead instructions, since they
	 would have killed the values if they hadn't been deleted.  */
      mark_set_regs (pbi, PATTERN (insn), insn);

      if (CALL_P (insn))
	{
	  regset live_at_end;
	  bool sibcall_p;
	  rtx note, cond;
	  int i;

	  cond = NULL_RTX;
	  if (GET_CODE (PATTERN (insn)) == COND_EXEC)
	    cond = COND_EXEC_TEST (PATTERN (insn));

	  /* Non-constant calls clobber memory, constant calls do not
	     clobber memory, though they may clobber outgoing arguments
	     on the stack.  */
	  if (! CONST_OR_PURE_CALL_P (insn))
	    {
	      free_EXPR_LIST_list (&pbi->mem_set_list);
	      pbi->mem_set_list_len = 0;
	    }
	  else
	    invalidate_mems_from_set (pbi, stack_pointer_rtx);

	  /* There may be extra registers to be clobbered.  */
	  for (note = CALL_INSN_FUNCTION_USAGE (insn);
	       note;
	       note = XEXP (note, 1))
	    if (GET_CODE (XEXP (note, 0)) == CLOBBER)
	      mark_set_1 (pbi, CLOBBER, XEXP (XEXP (note, 0), 0),
			  cond, insn, pbi->flags);

	  /* Calls change all call-used and global registers; sibcalls do not
	     clobber anything that must be preserved at end-of-function,
	     except for return values.  */

	  sibcall_p = SIBLING_CALL_P (insn);
	  live_at_end = EXIT_BLOCK_PTR->il.rtl->global_live_at_start;
	  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	    if (TEST_HARD_REG_BIT (regs_invalidated_by_call, i)
		&& ! (sibcall_p
		      && REGNO_REG_SET_P (live_at_end, i)
		      && ! refers_to_regno_p (i, i+1,
					      current_function_return_rtx,
					      (rtx *) 0)))
	      {
		enum rtx_code code = global_regs[i] ? SET : CLOBBER;
		/* We do not want REG_UNUSED notes for these registers.  */
		mark_set_1 (pbi, code, regno_reg_rtx[i], cond, insn,
			    pbi->flags & ~(PROP_DEATH_NOTES | PROP_REG_INFO));
	      }
	}

      /* If an insn doesn't use CC0, it becomes dead since we assume
	 that every insn clobbers it.  So show it dead here;
	 mark_used_regs will set it live if it is referenced.  */
      pbi->cc0_live = 0;

      /* Record uses.  */
      if (! insn_is_dead)
	mark_used_regs (pbi, PATTERN (insn), NULL_RTX, insn);

      /* Sometimes we may have inserted something before INSN (such as a move)
	 when we make an auto-inc.  So ensure we will scan those insns.  */
#ifdef AUTO_INC_DEC
      prev = PREV_INSN (insn);
#endif

      if (! insn_is_dead && CALL_P (insn))
	{
	  int i;
	  rtx note, cond;

	  cond = NULL_RTX;
	  if (GET_CODE (PATTERN (insn)) == COND_EXEC)
	    cond = COND_EXEC_TEST (PATTERN (insn));

	  /* Calls use their arguments, and may clobber memory which
	     address involves some register.  */
	  for (note = CALL_INSN_FUNCTION_USAGE (insn);
	       note;
	       note = XEXP (note, 1))
	    /* We find USE or CLOBBER entities in a FUNCTION_USAGE list: both
	       of which mark_used_regs knows how to handle.  */
	    mark_used_regs (pbi, XEXP (XEXP (note, 0), 0), cond, insn);

	  /* The stack ptr is used (honorarily) by a CALL insn.  */
	  if ((flags & PROP_REG_INFO)
	      && !REGNO_REG_SET_P (pbi->reg_live, STACK_POINTER_REGNUM))
	    reg_deaths[STACK_POINTER_REGNUM] = pbi->insn_num;
	  SET_REGNO_REG_SET (pbi->reg_live, STACK_POINTER_REGNUM);

	  /* Calls may also reference any of the global registers,
	     so they are made live.  */
	  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	    if (global_regs[i])
	      mark_used_reg (pbi, regno_reg_rtx[i], cond, insn);
	}
    }

  pbi->insn_num++;

  return prev;
}

/* Initialize a propagate_block_info struct for public consumption.
   Note that the structure itself is opaque to this file, but that
   the user can use the regsets provided here.  */

struct propagate_block_info *
init_propagate_block_info (basic_block bb, regset live, regset local_set,
			   regset cond_local_set, int flags)
{
  struct propagate_block_info *pbi = XNEW (struct propagate_block_info);

  pbi->bb = bb;
  pbi->reg_live = live;
  pbi->mem_set_list = NULL_RTX;
  pbi->mem_set_list_len = 0;
  pbi->local_set = local_set;
  pbi->cond_local_set = cond_local_set;
  pbi->cc0_live = 0;
  pbi->flags = flags;
  pbi->insn_num = 0;

  if (flags & (PROP_LOG_LINKS | PROP_AUTOINC))
    pbi->reg_next_use = XCNEWVEC (rtx, max_reg_num ());
  else
    pbi->reg_next_use = NULL;

  pbi->new_set = BITMAP_ALLOC (NULL);

#ifdef HAVE_conditional_execution
  pbi->reg_cond_dead = splay_tree_new (splay_tree_compare_ints, NULL,
				       free_reg_cond_life_info);
  pbi->reg_cond_reg = BITMAP_ALLOC (NULL);

  /* If this block ends in a conditional branch, for each register
     live from one side of the branch and not the other, record the
     register as conditionally dead.  */
  if (JUMP_P (BB_END (bb))
      && any_condjump_p (BB_END (bb)))
    {
      regset diff = ALLOC_REG_SET (&reg_obstack);
      basic_block bb_true, bb_false;
      unsigned i;

      /* Identify the successor blocks.  */
      bb_true = EDGE_SUCC (bb, 0)->dest;
      if (!single_succ_p (bb))
	{
	  bb_false = EDGE_SUCC (bb, 1)->dest;

	  if (EDGE_SUCC (bb, 0)->flags & EDGE_FALLTHRU)
	    {
	      basic_block t = bb_false;
	      bb_false = bb_true;
	      bb_true = t;
	    }
	  else
	    gcc_assert (EDGE_SUCC (bb, 1)->flags & EDGE_FALLTHRU);
	}
      else
	{
	  /* This can happen with a conditional jump to the next insn.  */
	  gcc_assert (JUMP_LABEL (BB_END (bb)) == BB_HEAD (bb_true));

	  /* Simplest way to do nothing.  */
	  bb_false = bb_true;
	}

      /* Compute which register lead different lives in the successors.  */
      bitmap_xor (diff, bb_true->il.rtl->global_live_at_start,
		  bb_false->il.rtl->global_live_at_start);
      
      if (!bitmap_empty_p (diff))
	  {
	  /* Extract the condition from the branch.  */
	  rtx set_src = SET_SRC (pc_set (BB_END (bb)));
	  rtx cond_true = XEXP (set_src, 0);
	  rtx reg = XEXP (cond_true, 0);
 	  enum rtx_code inv_cond;

	  if (GET_CODE (reg) == SUBREG)
	    reg = SUBREG_REG (reg);

	  /* We can only track conditional lifetimes if the condition is
 	     in the form of a reversible comparison of a register against
 	     zero.  If the condition is more complex than that, then it is
 	     safe not to record any information.  */
 	  inv_cond = reversed_comparison_code (cond_true, BB_END (bb));
 	  if (inv_cond != UNKNOWN
 	      && REG_P (reg)
	      && XEXP (cond_true, 1) == const0_rtx)
	    {
	      rtx cond_false
		= gen_rtx_fmt_ee (inv_cond,
				  GET_MODE (cond_true), XEXP (cond_true, 0),
				  XEXP (cond_true, 1));
	      reg_set_iterator rsi;

	      if (GET_CODE (XEXP (set_src, 1)) == PC)
		{
		  rtx t = cond_false;
		  cond_false = cond_true;
		  cond_true = t;
		}

	      SET_REGNO_REG_SET (pbi->reg_cond_reg, REGNO (reg));

	      /* For each such register, mark it conditionally dead.  */
	      EXECUTE_IF_SET_IN_REG_SET (diff, 0, i, rsi)
		{
		  struct reg_cond_life_info *rcli;
		  rtx cond;

		  rcli = XNEW (struct reg_cond_life_info);

		  if (REGNO_REG_SET_P (bb_true->il.rtl->global_live_at_start,
				       i))
		    cond = cond_false;
		  else
		    cond = cond_true;
		  rcli->condition = cond;
		  rcli->stores = const0_rtx;
		  rcli->orig_condition = cond;

		  splay_tree_insert (pbi->reg_cond_dead, i,
				     (splay_tree_value) rcli);
		}
	    }
	}

      FREE_REG_SET (diff);
    }
#endif

  /* If this block has no successors, any stores to the frame that aren't
     used later in the block are dead.  So make a pass over the block
     recording any such that are made and show them dead at the end.  We do
     a very conservative and simple job here.  */
  if (optimize
      && ! (TREE_CODE (TREE_TYPE (current_function_decl)) == FUNCTION_TYPE
	    && (TYPE_RETURNS_STACK_DEPRESSED
		(TREE_TYPE (current_function_decl))))
      && (flags & PROP_SCAN_DEAD_STORES)
      && (EDGE_COUNT (bb->succs) == 0
	  || (single_succ_p (bb)
	      && single_succ (bb) == EXIT_BLOCK_PTR
	      && ! current_function_calls_eh_return)))
    {
      rtx insn, set;
      for (insn = BB_END (bb); insn != BB_HEAD (bb); insn = PREV_INSN (insn))
	if (NONJUMP_INSN_P (insn)
	    && (set = single_set (insn))
	    && MEM_P (SET_DEST (set)))
	  {
	    rtx mem = SET_DEST (set);
	    rtx canon_mem = canon_rtx (mem);

	    if (XEXP (canon_mem, 0) == frame_pointer_rtx
		|| (GET_CODE (XEXP (canon_mem, 0)) == PLUS
		    && XEXP (XEXP (canon_mem, 0), 0) == frame_pointer_rtx
		    && GET_CODE (XEXP (XEXP (canon_mem, 0), 1)) == CONST_INT))
	      add_to_mem_set_list (pbi, canon_mem);
	  }
    }

  return pbi;
}

/* Release a propagate_block_info struct.  */

void
free_propagate_block_info (struct propagate_block_info *pbi)
{
  free_EXPR_LIST_list (&pbi->mem_set_list);

  BITMAP_FREE (pbi->new_set);

#ifdef HAVE_conditional_execution
  splay_tree_delete (pbi->reg_cond_dead);
  BITMAP_FREE (pbi->reg_cond_reg);
#endif

  if (pbi->flags & PROP_REG_INFO)
    {
      int num = pbi->insn_num;
      unsigned i;
      reg_set_iterator rsi;

      EXECUTE_IF_SET_IN_REG_SET (pbi->reg_live, 0, i, rsi)
	{
	  REG_LIVE_LENGTH (i) += num - reg_deaths[i];
	  reg_deaths[i] = 0;
	}
    }
  if (pbi->reg_next_use)
    free (pbi->reg_next_use);

  free (pbi);
}

/* Compute the registers live at the beginning of a basic block BB from
   those live at the end.

   When called, REG_LIVE contains those live at the end.  On return, it
   contains those live at the beginning.

   LOCAL_SET, if non-null, will be set with all registers killed
   unconditionally by this basic block.
   Likewise, COND_LOCAL_SET, if non-null, will be set with all registers
   killed conditionally by this basic block.  If there is any unconditional
   set of a register, then the corresponding bit will be set in LOCAL_SET
   and cleared in COND_LOCAL_SET.
   It is valid for LOCAL_SET and COND_LOCAL_SET to be the same set.  In this
   case, the resulting set will be equal to the union of the two sets that
   would otherwise be computed.

   Return nonzero if an INSN is deleted (i.e. by dead code removal).  */

int
propagate_block (basic_block bb, regset live, regset local_set,
		 regset cond_local_set, int flags)
{
  struct propagate_block_info *pbi;
  rtx insn, prev;
  int changed;

  pbi = init_propagate_block_info (bb, live, local_set, cond_local_set, flags);

  if (flags & PROP_REG_INFO)
    {
      unsigned i;
      reg_set_iterator rsi;

      /* Process the regs live at the end of the block.
	 Mark them as not local to any one basic block.  */
      EXECUTE_IF_SET_IN_REG_SET (live, 0, i, rsi)
	REG_BASIC_BLOCK (i) = REG_BLOCK_GLOBAL;
    }

  /* Scan the block an insn at a time from end to beginning.  */

  changed = 0;
  for (insn = BB_END (bb); ; insn = prev)
    {
      /* If this is a call to `setjmp' et al, warn if any
	 non-volatile datum is live.  */
      if ((flags & PROP_REG_INFO)
	  && CALL_P (insn)
	  && find_reg_note (insn, REG_SETJMP, NULL))
	IOR_REG_SET (regs_live_at_setjmp, pbi->reg_live);

      prev = propagate_one_insn (pbi, insn);
      if (!prev)
        changed |= insn != get_insns ();
      else
        changed |= NEXT_INSN (prev) != insn;

      if (insn == BB_HEAD (bb))
	break;
    }

#ifdef EH_RETURN_DATA_REGNO
  if (bb_has_eh_pred (bb))
    {
      unsigned int i;
      for (i = 0; ; ++i)
	{
	  unsigned regno = EH_RETURN_DATA_REGNO (i);
	  if (regno == INVALID_REGNUM)
	    break;
	  if (pbi->local_set)
	    {
	      CLEAR_REGNO_REG_SET (pbi->cond_local_set, regno);
	      SET_REGNO_REG_SET (pbi->local_set, regno);
	    }
	  if (REGNO_REG_SET_P (pbi->reg_live, regno))
	    SET_REGNO_REG_SET (pbi->new_set, regno);
	  
	  regs_ever_live[regno] = 1;
	}
    }
#endif

  free_propagate_block_info (pbi);

  return changed;
}

/* Return 1 if X (the body of an insn, or part of it) is just dead stores
   (SET expressions whose destinations are registers dead after the insn).
   NEEDED is the regset that says which regs are alive after the insn.

   Unless CALL_OK is nonzero, an insn is needed if it contains a CALL.

   If X is the entire body of an insn, NOTES contains the reg notes
   pertaining to the insn.  */

static int
insn_dead_p (struct propagate_block_info *pbi, rtx x, int call_ok,
	     rtx notes ATTRIBUTE_UNUSED)
{
  enum rtx_code code = GET_CODE (x);

  /* Don't eliminate insns that may trap.  */
  if (flag_non_call_exceptions && may_trap_p (x))
    return 0;

#ifdef AUTO_INC_DEC
  /* As flow is invoked after combine, we must take existing AUTO_INC
     expressions into account.  */
  for (; notes; notes = XEXP (notes, 1))
    {
      if (REG_NOTE_KIND (notes) == REG_INC)
	{
	  int regno = REGNO (XEXP (notes, 0));

	  /* Don't delete insns to set global regs.  */
	  if ((regno < FIRST_PSEUDO_REGISTER && global_regs[regno])
	      || REGNO_REG_SET_P (pbi->reg_live, regno))
	    return 0;
	}
    }
#endif

  /* If setting something that's a reg or part of one,
     see if that register's altered value will be live.  */

  if (code == SET)
    {
      rtx r = SET_DEST (x);

#ifdef HAVE_cc0
      if (GET_CODE (r) == CC0)
	return ! pbi->cc0_live;
#endif

      /* A SET that is a subroutine call cannot be dead.  */
      if (GET_CODE (SET_SRC (x)) == CALL)
	{
	  if (! call_ok)
	    return 0;
	}

      /* Don't eliminate loads from volatile memory or volatile asms.  */
      else if (volatile_refs_p (SET_SRC (x)))
	return 0;

      if (MEM_P (r))
	{
	  rtx temp, canon_r;

	  if (MEM_VOLATILE_P (r) || GET_MODE (r) == BLKmode)
	    return 0;

	  canon_r = canon_rtx (r);

	  /* Walk the set of memory locations we are currently tracking
	     and see if one is an identical match to this memory location.
	     If so, this memory write is dead (remember, we're walking
	     backwards from the end of the block to the start).  Since
	     rtx_equal_p does not check the alias set or flags, we also
	     must have the potential for them to conflict (anti_dependence).  */
	  for (temp = pbi->mem_set_list; temp != 0; temp = XEXP (temp, 1))
	    if (anti_dependence (r, XEXP (temp, 0)))
	      {
		rtx mem = XEXP (temp, 0);

		if (rtx_equal_p (XEXP (canon_r, 0), XEXP (mem, 0))
		    && (GET_MODE_SIZE (GET_MODE (canon_r))
			<= GET_MODE_SIZE (GET_MODE (mem))))
		  return 1;

#ifdef AUTO_INC_DEC
		/* Check if memory reference matches an auto increment. Only
		   post increment/decrement or modify are valid.  */
		if (GET_MODE (mem) == GET_MODE (r)
		    && (GET_CODE (XEXP (mem, 0)) == POST_DEC
			|| GET_CODE (XEXP (mem, 0)) == POST_INC
			|| GET_CODE (XEXP (mem, 0)) == POST_MODIFY)
		    && GET_MODE (XEXP (mem, 0)) == GET_MODE (r)
		    && rtx_equal_p (XEXP (XEXP (mem, 0), 0), XEXP (r, 0)))
		  return 1;
#endif
	      }
	}
      else
	{
	  while (GET_CODE (r) == SUBREG
		 || GET_CODE (r) == STRICT_LOW_PART
		 || GET_CODE (r) == ZERO_EXTRACT)
	    r = XEXP (r, 0);

	  if (REG_P (r))
	    {
	      int regno = REGNO (r);

	      /* Obvious.  */
	      if (REGNO_REG_SET_P (pbi->reg_live, regno))
		return 0;

	      /* If this is a hard register, verify that subsequent
		 words are not needed.  */
	      if (regno < FIRST_PSEUDO_REGISTER)
		{
		  int n = hard_regno_nregs[regno][GET_MODE (r)];

		  while (--n > 0)
		    if (REGNO_REG_SET_P (pbi->reg_live, regno+n))
		      return 0;
		}

	      /* Don't delete insns to set global regs.  */
	      if (regno < FIRST_PSEUDO_REGISTER && global_regs[regno])
		return 0;

	      /* Make sure insns to set the stack pointer aren't deleted.  */
	      if (regno == STACK_POINTER_REGNUM)
		return 0;

	      /* ??? These bits might be redundant with the force live bits
		 in calculate_global_regs_live.  We would delete from
		 sequential sets; whether this actually affects real code
		 for anything but the stack pointer I don't know.  */
	      /* Make sure insns to set the frame pointer aren't deleted.  */
	      if (regno == FRAME_POINTER_REGNUM
		  && (! reload_completed || frame_pointer_needed))
		return 0;
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
	      if (regno == HARD_FRAME_POINTER_REGNUM
		  && (! reload_completed || frame_pointer_needed))
		return 0;
#endif

#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
	      /* Make sure insns to set arg pointer are never deleted
		 (if the arg pointer isn't fixed, there will be a USE
		 for it, so we can treat it normally).  */
	      if (regno == ARG_POINTER_REGNUM && fixed_regs[regno])
		return 0;
#endif

	      /* Otherwise, the set is dead.  */
	      return 1;
	    }
	}
    }

  /* If performing several activities, insn is dead if each activity
     is individually dead.  Also, CLOBBERs and USEs can be ignored; a
     CLOBBER or USE that's inside a PARALLEL doesn't make the insn
     worth keeping.  */
  else if (code == PARALLEL)
    {
      int i = XVECLEN (x, 0);

      for (i--; i >= 0; i--)
	if (GET_CODE (XVECEXP (x, 0, i)) != CLOBBER
	    && GET_CODE (XVECEXP (x, 0, i)) != USE
	    && ! insn_dead_p (pbi, XVECEXP (x, 0, i), call_ok, NULL_RTX))
	  return 0;

      return 1;
    }

  /* A CLOBBER of a pseudo-register that is dead serves no purpose.  That
     is not necessarily true for hard registers until after reload.  */
  else if (code == CLOBBER)
    {
      if (REG_P (XEXP (x, 0))
	  && (REGNO (XEXP (x, 0)) >= FIRST_PSEUDO_REGISTER
	      || reload_completed)
	  && ! REGNO_REG_SET_P (pbi->reg_live, REGNO (XEXP (x, 0))))
	return 1;
    }

  /* ??? A base USE is a historical relic.  It ought not be needed anymore.
     Instances where it is still used are either (1) temporary and the USE
     escaped the pass, (2) cruft and the USE need not be emitted anymore,
     or (3) hiding bugs elsewhere that are not properly representing data
     flow.  */

  return 0;
}

/* If INSN is the last insn in a libcall, and assuming INSN is dead,
   return 1 if the entire library call is dead.
   This is true if INSN copies a register (hard or pseudo)
   and if the hard return reg of the call insn is dead.
   (The caller should have tested the destination of the SET inside
   INSN already for death.)

   If this insn doesn't just copy a register, then we don't
   have an ordinary libcall.  In that case, cse could not have
   managed to substitute the source for the dest later on,
   so we can assume the libcall is dead.

   PBI is the block info giving pseudoregs live before this insn.
   NOTE is the REG_RETVAL note of the insn.  */

static int
libcall_dead_p (struct propagate_block_info *pbi, rtx note, rtx insn)
{
  rtx x = single_set (insn);

  if (x)
    {
      rtx r = SET_SRC (x);

      if (REG_P (r) || GET_CODE (r) == SUBREG)
	{
	  rtx call = XEXP (note, 0);
	  rtx call_pat;
	  int i;

	  /* Find the call insn.  */
	  while (call != insn && !CALL_P (call))
	    call = NEXT_INSN (call);

	  /* If there is none, do nothing special,
	     since ordinary death handling can understand these insns.  */
	  if (call == insn)
	    return 0;

	  /* See if the hard reg holding the value is dead.
	     If this is a PARALLEL, find the call within it.  */
	  call_pat = PATTERN (call);
	  if (GET_CODE (call_pat) == PARALLEL)
	    {
	      for (i = XVECLEN (call_pat, 0) - 1; i >= 0; i--)
		if (GET_CODE (XVECEXP (call_pat, 0, i)) == SET
		    && GET_CODE (SET_SRC (XVECEXP (call_pat, 0, i))) == CALL)
		  break;

	      /* This may be a library call that is returning a value
		 via invisible pointer.  Do nothing special, since
		 ordinary death handling can understand these insns.  */
	      if (i < 0)
		return 0;

	      call_pat = XVECEXP (call_pat, 0, i);
	    }

	  if (! insn_dead_p (pbi, call_pat, 1, REG_NOTES (call)))
	    return 0;

	  while ((insn = PREV_INSN (insn)) != call)
	    {
	      if (! INSN_P (insn))
		continue;
	      if (! insn_dead_p (pbi, PATTERN (insn), 0, REG_NOTES (insn)))
		return 0;
	    }
	  return 1;
	}
    }
  return 0;
}

/* 1 if register REGNO was alive at a place where `setjmp' was called
   and was set more than once or is an argument.
   Such regs may be clobbered by `longjmp'.  */

int
regno_clobbered_at_setjmp (int regno)
{
  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return 0;

  return ((REG_N_SETS (regno) > 1
	   || REGNO_REG_SET_P (ENTRY_BLOCK_PTR->il.rtl->global_live_at_end,
	     		       regno))
	  && REGNO_REG_SET_P (regs_live_at_setjmp, regno));
}

/* Add MEM to PBI->MEM_SET_LIST.  MEM should be canonical.  Respect the
   maximal list size; look for overlaps in mode and select the largest.  */
static void
add_to_mem_set_list (struct propagate_block_info *pbi, rtx mem)
{
  rtx i;

  /* We don't know how large a BLKmode store is, so we must not
     take them into consideration.  */
  if (GET_MODE (mem) == BLKmode)
    return;

  for (i = pbi->mem_set_list; i ; i = XEXP (i, 1))
    {
      rtx e = XEXP (i, 0);
      if (rtx_equal_p (XEXP (mem, 0), XEXP (e, 0)))
	{
	  if (GET_MODE_SIZE (GET_MODE (mem)) > GET_MODE_SIZE (GET_MODE (e)))
	    {
#ifdef AUTO_INC_DEC
	      /* If we must store a copy of the mem, we can just modify
		 the mode of the stored copy.  */
	      if (pbi->flags & PROP_AUTOINC)
	        PUT_MODE (e, GET_MODE (mem));
	      else
#endif
	        XEXP (i, 0) = mem;
	    }
	  return;
	}
    }

  if (pbi->mem_set_list_len < PARAM_VALUE (PARAM_MAX_FLOW_MEMORY_LOCATIONS))
    {
#ifdef AUTO_INC_DEC
      /* Store a copy of mem, otherwise the address may be
	 scrogged by find_auto_inc.  */
      if (pbi->flags & PROP_AUTOINC)
	mem = shallow_copy_rtx (mem);
#endif
      pbi->mem_set_list = alloc_EXPR_LIST (0, mem, pbi->mem_set_list);
      pbi->mem_set_list_len++;
    }
}

/* INSN references memory, possibly using autoincrement addressing modes.
   Find any entries on the mem_set_list that need to be invalidated due
   to an address change.  */

static int
invalidate_mems_from_autoinc (rtx *px, void *data)
{
  rtx x = *px;
  struct propagate_block_info *pbi = data;

  if (GET_RTX_CLASS (GET_CODE (x)) == RTX_AUTOINC)
    {
      invalidate_mems_from_set (pbi, XEXP (x, 0));
      return -1;
    }

  return 0;
}

/* EXP is a REG or MEM.  Remove any dependent entries from
   pbi->mem_set_list.  */

static void
invalidate_mems_from_set (struct propagate_block_info *pbi, rtx exp)
{
  rtx temp = pbi->mem_set_list;
  rtx prev = NULL_RTX;
  rtx next;

  while (temp)
    {
      next = XEXP (temp, 1);
      if ((REG_P (exp) && reg_overlap_mentioned_p (exp, XEXP (temp, 0)))
	  /* When we get an EXP that is a mem here, we want to check if EXP
	     overlaps the *address* of any of the mems in the list (i.e. not
	     whether the mems actually overlap; that's done elsewhere).  */
	  || (MEM_P (exp)
	      && reg_overlap_mentioned_p (exp, XEXP (XEXP (temp, 0), 0))))
	{
	  /* Splice this entry out of the list.  */
	  if (prev)
	    XEXP (prev, 1) = next;
	  else
	    pbi->mem_set_list = next;
	  free_EXPR_LIST_node (temp);
	  pbi->mem_set_list_len--;
	}
      else
	prev = temp;
      temp = next;
    }
}

/* Process the registers that are set within X.  Their bits are set to
   1 in the regset DEAD, because they are dead prior to this insn.

   If INSN is nonzero, it is the insn being processed.

   FLAGS is the set of operations to perform.  */

static void
mark_set_regs (struct propagate_block_info *pbi, rtx x, rtx insn)
{
  rtx cond = NULL_RTX;
  rtx link;
  enum rtx_code code;
  int flags = pbi->flags;

  if (insn)
    for (link = REG_NOTES (insn); link; link = XEXP (link, 1))
      {
	if (REG_NOTE_KIND (link) == REG_INC)
	  mark_set_1 (pbi, SET, XEXP (link, 0),
		      (GET_CODE (x) == COND_EXEC
		       ? COND_EXEC_TEST (x) : NULL_RTX),
		      insn, flags);
      }
 retry:
  switch (code = GET_CODE (x))
    {
    case SET:
      if (GET_CODE (XEXP (x, 1)) == ASM_OPERANDS)
	flags |= PROP_ASM_SCAN;
      /* Fall through */
    case CLOBBER:
      mark_set_1 (pbi, code, SET_DEST (x), cond, insn, flags);
      return;

    case COND_EXEC:
      cond = COND_EXEC_TEST (x);
      x = COND_EXEC_CODE (x);
      goto retry;

    case PARALLEL:
      {
	int i;

	/* We must scan forwards.  If we have an asm, we need to set
	   the PROP_ASM_SCAN flag before scanning the clobbers.  */
	for (i = 0; i < XVECLEN (x, 0); i++)
	  {
	    rtx sub = XVECEXP (x, 0, i);
	    switch (code = GET_CODE (sub))
	      {
	      case COND_EXEC:
		gcc_assert (!cond);

		cond = COND_EXEC_TEST (sub);
		sub = COND_EXEC_CODE (sub);
		if (GET_CODE (sub) == SET)
		  goto mark_set;
		if (GET_CODE (sub) == CLOBBER)
		  goto mark_clob;
		break;

	      case SET:
	      mark_set:
		if (GET_CODE (XEXP (sub, 1)) == ASM_OPERANDS)
		  flags |= PROP_ASM_SCAN;
		/* Fall through */
	      case CLOBBER:
	      mark_clob:
		mark_set_1 (pbi, code, SET_DEST (sub), cond, insn, flags);
		break;

	      case ASM_OPERANDS:
		flags |= PROP_ASM_SCAN;
		break;

	      default:
		break;
	      }
	  }
	break;
      }

    default:
      break;
    }
}

/* Process a single set, which appears in INSN.  REG (which may not
   actually be a REG, it may also be a SUBREG, PARALLEL, etc.) is
   being set using the CODE (which may be SET, CLOBBER, or COND_EXEC).
   If the set is conditional (because it appear in a COND_EXEC), COND
   will be the condition.  */

static void
mark_set_1 (struct propagate_block_info *pbi, enum rtx_code code, rtx reg, rtx cond, rtx insn, int flags)
{
  int regno_first = -1, regno_last = -1;
  unsigned long not_dead = 0;
  int i;

  /* Modifying just one hardware register of a multi-reg value or just a
     byte field of a register does not mean the value from before this insn
     is now dead.  Of course, if it was dead after it's unused now.  */

  switch (GET_CODE (reg))
    {
    case PARALLEL:
      /* Some targets place small structures in registers for return values of
	 functions.  We have to detect this case specially here to get correct
	 flow information.  */
      for (i = XVECLEN (reg, 0) - 1; i >= 0; i--)
	if (XEXP (XVECEXP (reg, 0, i), 0) != 0)
	  mark_set_1 (pbi, code, XEXP (XVECEXP (reg, 0, i), 0), cond, insn,
		      flags);
      return;

    case SIGN_EXTRACT:
      /* SIGN_EXTRACT cannot be an lvalue.  */
      gcc_unreachable ();

    case ZERO_EXTRACT:
    case STRICT_LOW_PART:
      /* ??? Assumes STRICT_LOW_PART not used on multi-word registers.  */
      do
	reg = XEXP (reg, 0);
      while (GET_CODE (reg) == SUBREG
	     || GET_CODE (reg) == ZERO_EXTRACT
	     || GET_CODE (reg) == STRICT_LOW_PART);
      if (MEM_P (reg))
	break;
      not_dead = (unsigned long) REGNO_REG_SET_P (pbi->reg_live, REGNO (reg));
      /* Fall through.  */

    case REG:
      regno_last = regno_first = REGNO (reg);
      if (regno_first < FIRST_PSEUDO_REGISTER)
	regno_last += hard_regno_nregs[regno_first][GET_MODE (reg)] - 1;
      break;

    case SUBREG:
      if (REG_P (SUBREG_REG (reg)))
	{
	  enum machine_mode outer_mode = GET_MODE (reg);
	  enum machine_mode inner_mode = GET_MODE (SUBREG_REG (reg));

	  /* Identify the range of registers affected.  This is moderately
	     tricky for hard registers.  See alter_subreg.  */

	  regno_last = regno_first = REGNO (SUBREG_REG (reg));
	  if (regno_first < FIRST_PSEUDO_REGISTER)
	    {
	      regno_first += subreg_regno_offset (regno_first, inner_mode,
						  SUBREG_BYTE (reg),
						  outer_mode);
	      regno_last = (regno_first
			    + hard_regno_nregs[regno_first][outer_mode] - 1);

	      /* Since we've just adjusted the register number ranges, make
		 sure REG matches.  Otherwise some_was_live will be clear
		 when it shouldn't have been, and we'll create incorrect
		 REG_UNUSED notes.  */
	      reg = gen_rtx_REG (outer_mode, regno_first);
	    }
	  else
	    {
	      /* If the number of words in the subreg is less than the number
		 of words in the full register, we have a well-defined partial
		 set.  Otherwise the high bits are undefined.

		 This is only really applicable to pseudos, since we just took
		 care of multi-word hard registers.  */
	      if (((GET_MODE_SIZE (outer_mode)
		    + UNITS_PER_WORD - 1) / UNITS_PER_WORD)
		  < ((GET_MODE_SIZE (inner_mode)
		      + UNITS_PER_WORD - 1) / UNITS_PER_WORD))
		not_dead = (unsigned long) REGNO_REG_SET_P (pbi->reg_live,
							    regno_first);

	      reg = SUBREG_REG (reg);
	    }
	}
      else
	reg = SUBREG_REG (reg);
      break;

    default:
      break;
    }

  /* If this set is a MEM, then it kills any aliased writes and any
     other MEMs which use it.
     If this set is a REG, then it kills any MEMs which use the reg.  */
  if (optimize && (flags & PROP_SCAN_DEAD_STORES))
    {
      if (REG_P (reg) || MEM_P (reg))
	invalidate_mems_from_set (pbi, reg);

      /* If the memory reference had embedded side effects (autoincrement
	 address modes) then we may need to kill some entries on the
	 memory set list.  */
      if (insn && MEM_P (reg))
	for_each_rtx (&PATTERN (insn), invalidate_mems_from_autoinc, pbi);

      if (MEM_P (reg) && ! side_effects_p (reg)
	  /* ??? With more effort we could track conditional memory life.  */
	  && ! cond)
	add_to_mem_set_list (pbi, canon_rtx (reg));
    }

  if (REG_P (reg)
      && ! (regno_first == FRAME_POINTER_REGNUM
	    && (! reload_completed || frame_pointer_needed))
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
      && ! (regno_first == HARD_FRAME_POINTER_REGNUM
	    && (! reload_completed || frame_pointer_needed))
#endif
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
      && ! (regno_first == ARG_POINTER_REGNUM && fixed_regs[regno_first])
#endif
      )
    {
      int some_was_live = 0, some_was_dead = 0;

      for (i = regno_first; i <= regno_last; ++i)
	{
	  int needed_regno = REGNO_REG_SET_P (pbi->reg_live, i);
	  if (pbi->local_set)
	    {
	      /* Order of the set operation matters here since both
		 sets may be the same.  */
	      CLEAR_REGNO_REG_SET (pbi->cond_local_set, i);
	      if (cond != NULL_RTX
		  && ! REGNO_REG_SET_P (pbi->local_set, i))
		SET_REGNO_REG_SET (pbi->cond_local_set, i);
	      else
		SET_REGNO_REG_SET (pbi->local_set, i);
	    }
	  if (code != CLOBBER || needed_regno)
	    SET_REGNO_REG_SET (pbi->new_set, i);

	  some_was_live |= needed_regno;
	  some_was_dead |= ! needed_regno;
	}

#ifdef HAVE_conditional_execution
      /* Consider conditional death in deciding that the register needs
	 a death note.  */
      if (some_was_live && ! not_dead
	  /* The stack pointer is never dead.  Well, not strictly true,
	     but it's very difficult to tell from here.  Hopefully
	     combine_stack_adjustments will fix up the most egregious
	     errors.  */
	  && regno_first != STACK_POINTER_REGNUM)
	{
	  for (i = regno_first; i <= regno_last; ++i)
	    if (! mark_regno_cond_dead (pbi, i, cond))
	      not_dead |= ((unsigned long) 1) << (i - regno_first);
	}
#endif

      /* Additional data to record if this is the final pass.  */
      if (flags & (PROP_LOG_LINKS | PROP_REG_INFO
		   | PROP_DEATH_NOTES | PROP_AUTOINC))
	{
	  rtx y;
	  int blocknum = pbi->bb->index;

	  y = NULL_RTX;
	  if (flags & (PROP_LOG_LINKS | PROP_AUTOINC))
	    {
	      y = pbi->reg_next_use[regno_first];

	      /* The next use is no longer next, since a store intervenes.  */
	      for (i = regno_first; i <= regno_last; ++i)
		pbi->reg_next_use[i] = 0;
	    }

	  if (flags & PROP_REG_INFO)
	    {
	      for (i = regno_first; i <= regno_last; ++i)
		{
		  /* Count (weighted) references, stores, etc.  This counts a
		     register twice if it is modified, but that is correct.  */
		  REG_N_SETS (i) += 1;
		  REG_N_REFS (i) += 1;
		  REG_FREQ (i) += REG_FREQ_FROM_BB (pbi->bb);

	          /* The insns where a reg is live are normally counted
		     elsewhere, but we want the count to include the insn
		     where the reg is set, and the normal counting mechanism
		     would not count it.  */
		  REG_LIVE_LENGTH (i) += 1;
		}

	      /* If this is a hard reg, record this function uses the reg.  */
	      if (regno_first < FIRST_PSEUDO_REGISTER)
		{
		  for (i = regno_first; i <= regno_last; i++)
		    regs_ever_live[i] = 1;
		  if (flags & PROP_ASM_SCAN)
		    for (i = regno_first; i <= regno_last; i++)
		      regs_asm_clobbered[i] = 1;
		}
	      else
		{
		  /* Keep track of which basic blocks each reg appears in.  */
		  if (REG_BASIC_BLOCK (regno_first) == REG_BLOCK_UNKNOWN)
		    REG_BASIC_BLOCK (regno_first) = blocknum;
		  else if (REG_BASIC_BLOCK (regno_first) != blocknum)
		    REG_BASIC_BLOCK (regno_first) = REG_BLOCK_GLOBAL;
		}
	    }

	  if (! some_was_dead)
	    {
	      if (flags & PROP_LOG_LINKS)
		{
		  /* Make a logical link from the next following insn
		     that uses this register, back to this insn.
		     The following insns have already been processed.

		     We don't build a LOG_LINK for hard registers containing
		     in ASM_OPERANDs.  If these registers get replaced,
		     we might wind up changing the semantics of the insn,
		     even if reload can make what appear to be valid
		     assignments later.

		     We don't build a LOG_LINK for global registers to
		     or from a function call.  We don't want to let
		     combine think that it knows what is going on with
		     global registers.  */
		  if (y && (BLOCK_NUM (y) == blocknum)
		      && (regno_first >= FIRST_PSEUDO_REGISTER
			  || (asm_noperands (PATTERN (y)) < 0
			      && ! ((CALL_P (insn)
				     || CALL_P (y))
				    && global_regs[regno_first]))))
		    LOG_LINKS (y) = alloc_INSN_LIST (insn, LOG_LINKS (y));
		}
	    }
	  else if (not_dead)
	    ;
	  else if (! some_was_live)
	    {
	      if (flags & PROP_REG_INFO)
		REG_N_DEATHS (regno_first) += 1;

	      if (flags & PROP_DEATH_NOTES
#ifdef STACK_REGS
		  && (!(flags & PROP_POST_REGSTACK)
		      || !IN_RANGE (REGNO (reg), FIRST_STACK_REG,
				    LAST_STACK_REG))
#endif
		  )
		{
		  /* Note that dead stores have already been deleted
		     when possible.  If we get here, we have found a
		     dead store that cannot be eliminated (because the
		     same insn does something useful).  Indicate this
		     by marking the reg being set as dying here.  */
		  REG_NOTES (insn)
		    = alloc_EXPR_LIST (REG_UNUSED, reg, REG_NOTES (insn));
		}
	    }
	  else
	    {
	      if (flags & PROP_DEATH_NOTES
#ifdef STACK_REGS
		  && (!(flags & PROP_POST_REGSTACK)
		      || !IN_RANGE (REGNO (reg), FIRST_STACK_REG,
				    LAST_STACK_REG))
#endif
		  )
		{
		  /* This is a case where we have a multi-word hard register
		     and some, but not all, of the words of the register are
		     needed in subsequent insns.  Write REG_UNUSED notes
		     for those parts that were not needed.  This case should
		     be rare.  */

		  for (i = regno_first; i <= regno_last; ++i)
		    if (! REGNO_REG_SET_P (pbi->reg_live, i))
		      REG_NOTES (insn)
			= alloc_EXPR_LIST (REG_UNUSED,
					   regno_reg_rtx[i],
					   REG_NOTES (insn));
		}
	    }
	}

      /* Mark the register as being dead.  */
      if (some_was_live
	  /* The stack pointer is never dead.  Well, not strictly true,
	     but it's very difficult to tell from here.  Hopefully
	     combine_stack_adjustments will fix up the most egregious
	     errors.  */
	  && regno_first != STACK_POINTER_REGNUM)
	{
	  for (i = regno_first; i <= regno_last; ++i)
	    if (!(not_dead & (((unsigned long) 1) << (i - regno_first))))
	      {
		if ((pbi->flags & PROP_REG_INFO)
		    && REGNO_REG_SET_P (pbi->reg_live, i))
		  {
		    REG_LIVE_LENGTH (i) += pbi->insn_num - reg_deaths[i];
		    reg_deaths[i] = 0;
		  }
		CLEAR_REGNO_REG_SET (pbi->reg_live, i);
	      }
	  if (flags & PROP_DEAD_INSN)
	    emit_insn_after (gen_rtx_CLOBBER (VOIDmode, reg), insn);
	}
    }
  else if (REG_P (reg))
    {
      if (flags & (PROP_LOG_LINKS | PROP_AUTOINC))
	pbi->reg_next_use[regno_first] = 0;

      if ((flags & PROP_REG_INFO) != 0
	  && (flags & PROP_ASM_SCAN) != 0
	  &&  regno_first < FIRST_PSEUDO_REGISTER)
	{
	  for (i = regno_first; i <= regno_last; i++)
	    regs_asm_clobbered[i] = 1;
	}
    }

  /* If this is the last pass and this is a SCRATCH, show it will be dying
     here and count it.  */
  else if (GET_CODE (reg) == SCRATCH)
    {
      if (flags & PROP_DEATH_NOTES
#ifdef STACK_REGS
	  && (!(flags & PROP_POST_REGSTACK)
	      || !IN_RANGE (REGNO (reg), FIRST_STACK_REG, LAST_STACK_REG))
#endif
	  )
	REG_NOTES (insn)
	  = alloc_EXPR_LIST (REG_UNUSED, reg, REG_NOTES (insn));
    }
}

#ifdef HAVE_conditional_execution
/* Mark REGNO conditionally dead.
   Return true if the register is now unconditionally dead.  */

static int
mark_regno_cond_dead (struct propagate_block_info *pbi, int regno, rtx cond)
{
  /* If this is a store to a predicate register, the value of the
     predicate is changing, we don't know that the predicate as seen
     before is the same as that seen after.  Flush all dependent
     conditions from reg_cond_dead.  This will make all such
     conditionally live registers unconditionally live.  */
  if (REGNO_REG_SET_P (pbi->reg_cond_reg, regno))
    flush_reg_cond_reg (pbi, regno);

  /* If this is an unconditional store, remove any conditional
     life that may have existed.  */
  if (cond == NULL_RTX)
    splay_tree_remove (pbi->reg_cond_dead, regno);
  else
    {
      splay_tree_node node;
      struct reg_cond_life_info *rcli;
      rtx ncond;

      /* Otherwise this is a conditional set.  Record that fact.
	 It may have been conditionally used, or there may be a
	 subsequent set with a complementary condition.  */

      node = splay_tree_lookup (pbi->reg_cond_dead, regno);
      if (node == NULL)
	{
	  /* The register was unconditionally live previously.
	     Record the current condition as the condition under
	     which it is dead.  */
	  rcli = XNEW (struct reg_cond_life_info);
	  rcli->condition = cond;
	  rcli->stores = cond;
	  rcli->orig_condition = const0_rtx;
	  splay_tree_insert (pbi->reg_cond_dead, regno,
			     (splay_tree_value) rcli);

	  SET_REGNO_REG_SET (pbi->reg_cond_reg, REGNO (XEXP (cond, 0)));

	  /* Not unconditionally dead.  */
	  return 0;
	}
      else
	{
	  /* The register was conditionally live previously.
	     Add the new condition to the old.  */
	  rcli = (struct reg_cond_life_info *) node->value;
	  ncond = rcli->condition;
	  ncond = ior_reg_cond (ncond, cond, 1);
	  if (rcli->stores == const0_rtx)
	    rcli->stores = cond;
	  else if (rcli->stores != const1_rtx)
	    rcli->stores = ior_reg_cond (rcli->stores, cond, 1);

	  /* If the register is now unconditionally dead, remove the entry
	     in the splay_tree.  A register is unconditionally dead if the
	     dead condition ncond is true.  A register is also unconditionally
	     dead if the sum of all conditional stores is an unconditional
	     store (stores is true), and the dead condition is identically the
	     same as the original dead condition initialized at the end of
	     the block.  This is a pointer compare, not an rtx_equal_p
	     compare.  */
	  if (ncond == const1_rtx
	      || (ncond == rcli->orig_condition && rcli->stores == const1_rtx))
	    splay_tree_remove (pbi->reg_cond_dead, regno);
	  else
	    {
	      rcli->condition = ncond;

	      SET_REGNO_REG_SET (pbi->reg_cond_reg, REGNO (XEXP (cond, 0)));

	      /* Not unconditionally dead.  */
	      return 0;
	    }
	}
    }

  return 1;
}

/* Called from splay_tree_delete for pbi->reg_cond_life.  */

static void
free_reg_cond_life_info (splay_tree_value value)
{
  struct reg_cond_life_info *rcli = (struct reg_cond_life_info *) value;
  free (rcli);
}

/* Helper function for flush_reg_cond_reg.  */

static int
flush_reg_cond_reg_1 (splay_tree_node node, void *data)
{
  struct reg_cond_life_info *rcli;
  int *xdata = (int *) data;
  unsigned int regno = xdata[0];

  /* Don't need to search if last flushed value was farther on in
     the in-order traversal.  */
  if (xdata[1] >= (int) node->key)
    return 0;

  /* Splice out portions of the expression that refer to regno.  */
  rcli = (struct reg_cond_life_info *) node->value;
  rcli->condition = elim_reg_cond (rcli->condition, regno);
  if (rcli->stores != const0_rtx && rcli->stores != const1_rtx)
    rcli->stores = elim_reg_cond (rcli->stores, regno);

  /* If the entire condition is now false, signal the node to be removed.  */
  if (rcli->condition == const0_rtx)
    {
      xdata[1] = node->key;
      return -1;
    }
  else
    gcc_assert (rcli->condition != const1_rtx);

  return 0;
}

/* Flush all (sub) expressions referring to REGNO from REG_COND_LIVE.  */

static void
flush_reg_cond_reg (struct propagate_block_info *pbi, int regno)
{
  int pair[2];

  pair[0] = regno;
  pair[1] = -1;
  while (splay_tree_foreach (pbi->reg_cond_dead,
			     flush_reg_cond_reg_1, pair) == -1)
    splay_tree_remove (pbi->reg_cond_dead, pair[1]);

  CLEAR_REGNO_REG_SET (pbi->reg_cond_reg, regno);
}

/* Logical arithmetic on predicate conditions.  IOR, NOT and AND.
   For ior/and, the ADD flag determines whether we want to add the new
   condition X to the old one unconditionally.  If it is zero, we will
   only return a new expression if X allows us to simplify part of
   OLD, otherwise we return NULL to the caller.
   If ADD is nonzero, we will return a new condition in all cases.  The
   toplevel caller of one of these functions should always pass 1 for
   ADD.  */

static rtx
ior_reg_cond (rtx old, rtx x, int add)
{
  rtx op0, op1;

  if (COMPARISON_P (old))
    {
      if (COMPARISON_P (x)
	  && REVERSE_CONDEXEC_PREDICATES_P (x, old)
	  && REGNO (XEXP (x, 0)) == REGNO (XEXP (old, 0)))
	return const1_rtx;
      if (GET_CODE (x) == GET_CODE (old)
	  && REGNO (XEXP (x, 0)) == REGNO (XEXP (old, 0)))
	return old;
      if (! add)
	return NULL;
      return gen_rtx_IOR (0, old, x);
    }

  switch (GET_CODE (old))
    {
    case IOR:
      op0 = ior_reg_cond (XEXP (old, 0), x, 0);
      op1 = ior_reg_cond (XEXP (old, 1), x, 0);
      if (op0 != NULL || op1 != NULL)
	{
	  if (op0 == const0_rtx)
	    return op1 ? op1 : gen_rtx_IOR (0, XEXP (old, 1), x);
	  if (op1 == const0_rtx)
	    return op0 ? op0 : gen_rtx_IOR (0, XEXP (old, 0), x);
	  if (op0 == const1_rtx || op1 == const1_rtx)
	    return const1_rtx;
	  if (op0 == NULL)
	    op0 = gen_rtx_IOR (0, XEXP (old, 0), x);
	  else if (rtx_equal_p (x, op0))
	    /* (x | A) | x ~ (x | A).  */
	    return old;
	  if (op1 == NULL)
	    op1 = gen_rtx_IOR (0, XEXP (old, 1), x);
	  else if (rtx_equal_p (x, op1))
	    /* (A | x) | x ~ (A | x).  */
	    return old;
	  return gen_rtx_IOR (0, op0, op1);
	}
      if (! add)
	return NULL;
      return gen_rtx_IOR (0, old, x);

    case AND:
      op0 = ior_reg_cond (XEXP (old, 0), x, 0);
      op1 = ior_reg_cond (XEXP (old, 1), x, 0);
      if (op0 != NULL || op1 != NULL)
	{
	  if (op0 == const1_rtx)
	    return op1 ? op1 : gen_rtx_IOR (0, XEXP (old, 1), x);
	  if (op1 == const1_rtx)
	    return op0 ? op0 : gen_rtx_IOR (0, XEXP (old, 0), x);
	  if (op0 == const0_rtx || op1 == const0_rtx)
	    return const0_rtx;
	  if (op0 == NULL)
	    op0 = gen_rtx_IOR (0, XEXP (old, 0), x);
	  else if (rtx_equal_p (x, op0))
	    /* (x & A) | x ~ x.  */
	    return op0;
	  if (op1 == NULL)
	    op1 = gen_rtx_IOR (0, XEXP (old, 1), x);
	  else if (rtx_equal_p (x, op1))
	    /* (A & x) | x ~ x.  */
	    return op1;
	  return gen_rtx_AND (0, op0, op1);
	}
      if (! add)
	return NULL;
      return gen_rtx_IOR (0, old, x);

    case NOT:
      op0 = and_reg_cond (XEXP (old, 0), not_reg_cond (x), 0);
      if (op0 != NULL)
	return not_reg_cond (op0);
      if (! add)
	return NULL;
      return gen_rtx_IOR (0, old, x);

    default:
      gcc_unreachable ();
    }
}

static rtx
not_reg_cond (rtx x)
{
  if (x == const0_rtx)
    return const1_rtx;
  else if (x == const1_rtx)
    return const0_rtx;
  if (GET_CODE (x) == NOT)
    return XEXP (x, 0);
  if (COMPARISON_P (x)
      && REG_P (XEXP (x, 0)))
    {
      gcc_assert (XEXP (x, 1) == const0_rtx);

      return gen_rtx_fmt_ee (reversed_comparison_code (x, NULL),
			     VOIDmode, XEXP (x, 0), const0_rtx);
    }
  return gen_rtx_NOT (0, x);
}

static rtx
and_reg_cond (rtx old, rtx x, int add)
{
  rtx op0, op1;

  if (COMPARISON_P (old))
    {
      if (COMPARISON_P (x)
	  && GET_CODE (x) == reversed_comparison_code (old, NULL)
	  && REGNO (XEXP (x, 0)) == REGNO (XEXP (old, 0)))
	return const0_rtx;
      if (GET_CODE (x) == GET_CODE (old)
	  && REGNO (XEXP (x, 0)) == REGNO (XEXP (old, 0)))
	return old;
      if (! add)
	return NULL;
      return gen_rtx_AND (0, old, x);
    }

  switch (GET_CODE (old))
    {
    case IOR:
      op0 = and_reg_cond (XEXP (old, 0), x, 0);
      op1 = and_reg_cond (XEXP (old, 1), x, 0);
      if (op0 != NULL || op1 != NULL)
	{
	  if (op0 == const0_rtx)
	    return op1 ? op1 : gen_rtx_AND (0, XEXP (old, 1), x);
	  if (op1 == const0_rtx)
	    return op0 ? op0 : gen_rtx_AND (0, XEXP (old, 0), x);
	  if (op0 == const1_rtx || op1 == const1_rtx)
	    return const1_rtx;
	  if (op0 == NULL)
	    op0 = gen_rtx_AND (0, XEXP (old, 0), x);
	  else if (rtx_equal_p (x, op0))
	    /* (x | A) & x ~ x.  */
	    return op0;
	  if (op1 == NULL)
	    op1 = gen_rtx_AND (0, XEXP (old, 1), x);
	  else if (rtx_equal_p (x, op1))
	    /* (A | x) & x ~ x.  */
	    return op1;
	  return gen_rtx_IOR (0, op0, op1);
	}
      if (! add)
	return NULL;
      return gen_rtx_AND (0, old, x);

    case AND:
      op0 = and_reg_cond (XEXP (old, 0), x, 0);
      op1 = and_reg_cond (XEXP (old, 1), x, 0);
      if (op0 != NULL || op1 != NULL)
	{
	  if (op0 == const1_rtx)
	    return op1 ? op1 : gen_rtx_AND (0, XEXP (old, 1), x);
	  if (op1 == const1_rtx)
	    return op0 ? op0 : gen_rtx_AND (0, XEXP (old, 0), x);
	  if (op0 == const0_rtx || op1 == const0_rtx)
	    return const0_rtx;
	  if (op0 == NULL)
	    op0 = gen_rtx_AND (0, XEXP (old, 0), x);
	  else if (rtx_equal_p (x, op0))
	    /* (x & A) & x ~ (x & A).  */
	    return old;
	  if (op1 == NULL)
	    op1 = gen_rtx_AND (0, XEXP (old, 1), x);
	  else if (rtx_equal_p (x, op1))
	    /* (A & x) & x ~ (A & x).  */
	    return old;
	  return gen_rtx_AND (0, op0, op1);
	}
      if (! add)
	return NULL;
      return gen_rtx_AND (0, old, x);

    case NOT:
      op0 = ior_reg_cond (XEXP (old, 0), not_reg_cond (x), 0);
      if (op0 != NULL)
	return not_reg_cond (op0);
      if (! add)
	return NULL;
      return gen_rtx_AND (0, old, x);

    default:
      gcc_unreachable ();
    }
}

/* Given a condition X, remove references to reg REGNO and return the
   new condition.  The removal will be done so that all conditions
   involving REGNO are considered to evaluate to false.  This function
   is used when the value of REGNO changes.  */

static rtx
elim_reg_cond (rtx x, unsigned int regno)
{
  rtx op0, op1;

  if (COMPARISON_P (x))
    {
      if (REGNO (XEXP (x, 0)) == regno)
	return const0_rtx;
      return x;
    }

  switch (GET_CODE (x))
    {
    case AND:
      op0 = elim_reg_cond (XEXP (x, 0), regno);
      op1 = elim_reg_cond (XEXP (x, 1), regno);
      if (op0 == const0_rtx || op1 == const0_rtx)
	return const0_rtx;
      if (op0 == const1_rtx)
	return op1;
      if (op1 == const1_rtx)
	return op0;
      if (op0 == XEXP (x, 0) && op1 == XEXP (x, 1))
	return x;
      return gen_rtx_AND (0, op0, op1);

    case IOR:
      op0 = elim_reg_cond (XEXP (x, 0), regno);
      op1 = elim_reg_cond (XEXP (x, 1), regno);
      if (op0 == const1_rtx || op1 == const1_rtx)
	return const1_rtx;
      if (op0 == const0_rtx)
	return op1;
      if (op1 == const0_rtx)
	return op0;
      if (op0 == XEXP (x, 0) && op1 == XEXP (x, 1))
	return x;
      return gen_rtx_IOR (0, op0, op1);

    case NOT:
      op0 = elim_reg_cond (XEXP (x, 0), regno);
      if (op0 == const0_rtx)
	return const1_rtx;
      if (op0 == const1_rtx)
	return const0_rtx;
      if (op0 != XEXP (x, 0))
	return not_reg_cond (op0);
      return x;

    default:
      gcc_unreachable ();
    }
}
#endif /* HAVE_conditional_execution */

#ifdef AUTO_INC_DEC

/* Try to substitute the auto-inc expression INC as the address inside
   MEM which occurs in INSN.  Currently, the address of MEM is an expression
   involving INCR_REG, and INCR is the next use of INCR_REG; it is an insn
   that has a single set whose source is a PLUS of INCR_REG and something
   else.  */

static void
attempt_auto_inc (struct propagate_block_info *pbi, rtx inc, rtx insn,
		  rtx mem, rtx incr, rtx incr_reg)
{
  int regno = REGNO (incr_reg);
  rtx set = single_set (incr);
  rtx q = SET_DEST (set);
  rtx y = SET_SRC (set);
  int opnum = XEXP (y, 0) == incr_reg ? 0 : 1;
  int changed;

  /* Make sure this reg appears only once in this insn.  */
  if (count_occurrences (PATTERN (insn), incr_reg, 1) != 1)
    return;

  if (dead_or_set_p (incr, incr_reg)
      /* Mustn't autoinc an eliminable register.  */
      && (regno >= FIRST_PSEUDO_REGISTER
	  || ! TEST_HARD_REG_BIT (elim_reg_set, regno)))
    {
      /* This is the simple case.  Try to make the auto-inc.  If
	 we can't, we are done.  Otherwise, we will do any
	 needed updates below.  */
      if (! validate_change (insn, &XEXP (mem, 0), inc, 0))
	return;
    }
  else if (REG_P (q)
	   /* PREV_INSN used here to check the semi-open interval
	      [insn,incr).  */
	   && ! reg_used_between_p (q,  PREV_INSN (insn), incr)
	   /* We must also check for sets of q as q may be
	      a call clobbered hard register and there may
	      be a call between PREV_INSN (insn) and incr.  */
	   && ! reg_set_between_p (q,  PREV_INSN (insn), incr))
    {
      /* We have *p followed sometime later by q = p+size.
	 Both p and q must be live afterward,
	 and q is not used between INSN and its assignment.
	 Change it to q = p, ...*q..., q = q+size.
	 Then fall into the usual case.  */
      rtx insns, temp;

      start_sequence ();
      emit_move_insn (q, incr_reg);
      insns = get_insns ();
      end_sequence ();

      /* If we can't make the auto-inc, or can't make the
	 replacement into Y, exit.  There's no point in making
	 the change below if we can't do the auto-inc and doing
	 so is not correct in the pre-inc case.  */

      XEXP (inc, 0) = q;
      validate_change (insn, &XEXP (mem, 0), inc, 1);
      validate_change (incr, &XEXP (y, opnum), q, 1);
      if (! apply_change_group ())
	return;

      /* We now know we'll be doing this change, so emit the
	 new insn(s) and do the updates.  */
      emit_insn_before (insns, insn);

      if (BB_HEAD (pbi->bb) == insn)
	BB_HEAD (pbi->bb) = insns;

      /* INCR will become a NOTE and INSN won't contain a
	 use of INCR_REG.  If a use of INCR_REG was just placed in
	 the insn before INSN, make that the next use.
	 Otherwise, invalidate it.  */
      if (NONJUMP_INSN_P (PREV_INSN (insn))
	  && GET_CODE (PATTERN (PREV_INSN (insn))) == SET
	  && SET_SRC (PATTERN (PREV_INSN (insn))) == incr_reg)
	pbi->reg_next_use[regno] = PREV_INSN (insn);
      else
	pbi->reg_next_use[regno] = 0;

      incr_reg = q;
      regno = REGNO (q);

      if ((pbi->flags & PROP_REG_INFO)
	  && !REGNO_REG_SET_P (pbi->reg_live, regno))
	reg_deaths[regno] = pbi->insn_num;

      /* REGNO is now used in INCR which is below INSN, but
	 it previously wasn't live here.  If we don't mark
	 it as live, we'll put a REG_DEAD note for it
	 on this insn, which is incorrect.  */
      SET_REGNO_REG_SET (pbi->reg_live, regno);

      /* If there are any calls between INSN and INCR, show
	 that REGNO now crosses them.  */
      for (temp = insn; temp != incr; temp = NEXT_INSN (temp))
	if (CALL_P (temp))
	  {
	    REG_N_CALLS_CROSSED (regno)++;
	    if (can_throw_internal (temp))
	      REG_N_THROWING_CALLS_CROSSED (regno)++;
	  }

      /* Invalidate alias info for Q since we just changed its value.  */
      clear_reg_alias_info (q);
    }
  else
    return;

  /* If we haven't returned, it means we were able to make the
     auto-inc, so update the status.  First, record that this insn
     has an implicit side effect.  */

  REG_NOTES (insn) = alloc_EXPR_LIST (REG_INC, incr_reg, REG_NOTES (insn));

  /* Modify the old increment-insn to simply copy
     the already-incremented value of our register.  */
  changed = validate_change (incr, &SET_SRC (set), incr_reg, 0);
  gcc_assert (changed);

  /* If that makes it a no-op (copying the register into itself) delete
     it so it won't appear to be a "use" and a "set" of this
     register.  */
  if (REGNO (SET_DEST (set)) == REGNO (incr_reg))
    {
      /* If the original source was dead, it's dead now.  */
      rtx note;

      while ((note = find_reg_note (incr, REG_DEAD, NULL_RTX)) != NULL_RTX)
	{
	  remove_note (incr, note);
	  if (XEXP (note, 0) != incr_reg)
	    {
	      unsigned int regno = REGNO (XEXP (note, 0));

	      if ((pbi->flags & PROP_REG_INFO)
		  && REGNO_REG_SET_P (pbi->reg_live, regno))
		{
		  REG_LIVE_LENGTH (regno) += pbi->insn_num - reg_deaths[regno];
		  reg_deaths[regno] = 0;
		}
	      CLEAR_REGNO_REG_SET (pbi->reg_live, REGNO (XEXP (note, 0)));
	    }
	}

      SET_INSN_DELETED (incr);
    }

  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      /* Count an extra reference to the reg.  When a reg is
	 incremented, spilling it is worse, so we want to make
	 that less likely.  */
      REG_FREQ (regno) += REG_FREQ_FROM_BB (pbi->bb);

      /* Count the increment as a setting of the register,
	 even though it isn't a SET in rtl.  */
      REG_N_SETS (regno)++;
    }
}

/* X is a MEM found in INSN.  See if we can convert it into an auto-increment
   reference.  */

static void
find_auto_inc (struct propagate_block_info *pbi, rtx x, rtx insn)
{
  rtx addr = XEXP (x, 0);
  HOST_WIDE_INT offset = 0;
  rtx set, y, incr, inc_val;
  int regno;
  int size = GET_MODE_SIZE (GET_MODE (x));

  if (JUMP_P (insn))
    return;

  /* Here we detect use of an index register which might be good for
     postincrement, postdecrement, preincrement, or predecrement.  */

  if (GET_CODE (addr) == PLUS && GET_CODE (XEXP (addr, 1)) == CONST_INT)
    offset = INTVAL (XEXP (addr, 1)), addr = XEXP (addr, 0);

  if (!REG_P (addr))
    return;

  regno = REGNO (addr);

  /* Is the next use an increment that might make auto-increment? */
  incr = pbi->reg_next_use[regno];
  if (incr == 0 || BLOCK_NUM (incr) != BLOCK_NUM (insn))
    return;
  set = single_set (incr);
  if (set == 0 || GET_CODE (set) != SET)
    return;
  y = SET_SRC (set);

  if (GET_CODE (y) != PLUS)
    return;

  if (REG_P (XEXP (y, 0)) && REGNO (XEXP (y, 0)) == REGNO (addr))
    inc_val = XEXP (y, 1);
  else if (REG_P (XEXP (y, 1)) && REGNO (XEXP (y, 1)) == REGNO (addr))
    inc_val = XEXP (y, 0);
  else
    return;

  if (GET_CODE (inc_val) == CONST_INT)
    {
      if (HAVE_POST_INCREMENT
	  && (INTVAL (inc_val) == size && offset == 0))
	attempt_auto_inc (pbi, gen_rtx_POST_INC (Pmode, addr), insn, x,
			  incr, addr);
      else if (HAVE_POST_DECREMENT
	       && (INTVAL (inc_val) == -size && offset == 0))
	attempt_auto_inc (pbi, gen_rtx_POST_DEC (Pmode, addr), insn, x,
			  incr, addr);
      else if (HAVE_PRE_INCREMENT
	       && (INTVAL (inc_val) == size && offset == size))
	attempt_auto_inc (pbi, gen_rtx_PRE_INC (Pmode, addr), insn, x,
			  incr, addr);
      else if (HAVE_PRE_DECREMENT
	       && (INTVAL (inc_val) == -size && offset == -size))
	attempt_auto_inc (pbi, gen_rtx_PRE_DEC (Pmode, addr), insn, x,
			  incr, addr);
      else if (HAVE_POST_MODIFY_DISP && offset == 0)
	attempt_auto_inc (pbi, gen_rtx_POST_MODIFY (Pmode, addr,
						    gen_rtx_PLUS (Pmode,
								  addr,
								  inc_val)),
			  insn, x, incr, addr);
      else if (HAVE_PRE_MODIFY_DISP && offset == INTVAL (inc_val))
	attempt_auto_inc (pbi, gen_rtx_PRE_MODIFY (Pmode, addr,
						    gen_rtx_PLUS (Pmode,
								  addr,
								  inc_val)),
			  insn, x, incr, addr);
    }
  else if (REG_P (inc_val)
	   && ! reg_set_between_p (inc_val, PREV_INSN (insn),
				   NEXT_INSN (incr)))

    {
      if (HAVE_POST_MODIFY_REG && offset == 0)
	attempt_auto_inc (pbi, gen_rtx_POST_MODIFY (Pmode, addr,
						    gen_rtx_PLUS (Pmode,
								  addr,
								  inc_val)),
			  insn, x, incr, addr);
    }
}

#endif /* AUTO_INC_DEC */

static void
mark_used_reg (struct propagate_block_info *pbi, rtx reg,
	       rtx cond ATTRIBUTE_UNUSED, rtx insn)
{
  unsigned int regno_first, regno_last, i;
  int some_was_live, some_was_dead, some_not_set;

  regno_last = regno_first = REGNO (reg);
  if (regno_first < FIRST_PSEUDO_REGISTER)
    regno_last += hard_regno_nregs[regno_first][GET_MODE (reg)] - 1;

  /* Find out if any of this register is live after this instruction.  */
  some_was_live = some_was_dead = 0;
  for (i = regno_first; i <= regno_last; ++i)
    {
      int needed_regno = REGNO_REG_SET_P (pbi->reg_live, i);
      some_was_live |= needed_regno;
      some_was_dead |= ! needed_regno;
    }

  /* Find out if any of the register was set this insn.  */
  some_not_set = 0;
  for (i = regno_first; i <= regno_last; ++i)
    some_not_set |= ! REGNO_REG_SET_P (pbi->new_set, i);

  if (pbi->flags & (PROP_LOG_LINKS | PROP_AUTOINC))
    {
      /* Record where each reg is used, so when the reg is set we know
	 the next insn that uses it.  */
      pbi->reg_next_use[regno_first] = insn;
    }

  if (pbi->flags & PROP_REG_INFO)
    {
      if (regno_first < FIRST_PSEUDO_REGISTER)
	{
	  /* If this is a register we are going to try to eliminate,
	     don't mark it live here.  If we are successful in
	     eliminating it, it need not be live unless it is used for
	     pseudos, in which case it will have been set live when it
	     was allocated to the pseudos.  If the register will not
	     be eliminated, reload will set it live at that point.

	     Otherwise, record that this function uses this register.  */
	  /* ??? The PPC backend tries to "eliminate" on the pic
	     register to itself.  This should be fixed.  In the mean
	     time, hack around it.  */

	  if (! (TEST_HARD_REG_BIT (elim_reg_set, regno_first)
	         && (regno_first == FRAME_POINTER_REGNUM
		     || regno_first == ARG_POINTER_REGNUM)))
	    for (i = regno_first; i <= regno_last; ++i)
	      regs_ever_live[i] = 1;
	}
      else
	{
	  /* Keep track of which basic block each reg appears in.  */

	  int blocknum = pbi->bb->index;
	  if (REG_BASIC_BLOCK (regno_first) == REG_BLOCK_UNKNOWN)
	    REG_BASIC_BLOCK (regno_first) = blocknum;
	  else if (REG_BASIC_BLOCK (regno_first) != blocknum)
	    REG_BASIC_BLOCK (regno_first) = REG_BLOCK_GLOBAL;

	  /* Count (weighted) number of uses of each reg.  */
	  REG_FREQ (regno_first) += REG_FREQ_FROM_BB (pbi->bb);
	  REG_N_REFS (regno_first)++;
	}
      for (i = regno_first; i <= regno_last; ++i)
	if (! REGNO_REG_SET_P (pbi->reg_live, i))
	  {
	    gcc_assert (!reg_deaths[i]);
	    reg_deaths[i] = pbi->insn_num;
	  }
    }

  /* Record and count the insns in which a reg dies.  If it is used in
     this insn and was dead below the insn then it dies in this insn.
     If it was set in this insn, we do not make a REG_DEAD note;
     likewise if we already made such a note.  */
  if ((pbi->flags & (PROP_DEATH_NOTES | PROP_REG_INFO))
      && some_was_dead
      && some_not_set)
    {
      /* Check for the case where the register dying partially
	 overlaps the register set by this insn.  */
      if (regno_first != regno_last)
	for (i = regno_first; i <= regno_last; ++i)
	  some_was_live |= REGNO_REG_SET_P (pbi->new_set, i);

      /* If none of the words in X is needed, make a REG_DEAD note.
	 Otherwise, we must make partial REG_DEAD notes.  */
      if (! some_was_live)
	{
	  if ((pbi->flags & PROP_DEATH_NOTES)
#ifdef STACK_REGS
	      && (!(pbi->flags & PROP_POST_REGSTACK)
		  || !IN_RANGE (REGNO (reg), FIRST_STACK_REG, LAST_STACK_REG))
#endif
	      && ! find_regno_note (insn, REG_DEAD, regno_first))
	    REG_NOTES (insn)
	      = alloc_EXPR_LIST (REG_DEAD, reg, REG_NOTES (insn));

	  if (pbi->flags & PROP_REG_INFO)
	    REG_N_DEATHS (regno_first)++;
	}
      else
	{
	  /* Don't make a REG_DEAD note for a part of a register
	     that is set in the insn.  */
	  for (i = regno_first; i <= regno_last; ++i)
	    if (! REGNO_REG_SET_P (pbi->reg_live, i)
		&& ! dead_or_set_regno_p (insn, i))
	      REG_NOTES (insn)
		= alloc_EXPR_LIST (REG_DEAD,
				   regno_reg_rtx[i],
				   REG_NOTES (insn));
	}
    }

  /* Mark the register as being live.  */
  for (i = regno_first; i <= regno_last; ++i)
    {
#ifdef HAVE_conditional_execution
      int this_was_live = REGNO_REG_SET_P (pbi->reg_live, i);
#endif

      SET_REGNO_REG_SET (pbi->reg_live, i);

#ifdef HAVE_conditional_execution
      /* If this is a conditional use, record that fact.  If it is later
	 conditionally set, we'll know to kill the register.  */
      if (cond != NULL_RTX)
	{
	  splay_tree_node node;
	  struct reg_cond_life_info *rcli;
	  rtx ncond;

	  if (this_was_live)
	    {
	      node = splay_tree_lookup (pbi->reg_cond_dead, i);
	      if (node == NULL)
		{
		  /* The register was unconditionally live previously.
		     No need to do anything.  */
		}
	      else
		{
		  /* The register was conditionally live previously.
		     Subtract the new life cond from the old death cond.  */
		  rcli = (struct reg_cond_life_info *) node->value;
		  ncond = rcli->condition;
		  ncond = and_reg_cond (ncond, not_reg_cond (cond), 1);

		  /* If the register is now unconditionally live,
		     remove the entry in the splay_tree.  */
		  if (ncond == const0_rtx)
		    splay_tree_remove (pbi->reg_cond_dead, i);
		  else
		    {
		      rcli->condition = ncond;
		      SET_REGNO_REG_SET (pbi->reg_cond_reg,
					 REGNO (XEXP (cond, 0)));
		    }
		}
	    }
	  else
	    {
	      /* The register was not previously live at all.  Record
		 the condition under which it is still dead.  */
	      rcli = XNEW (struct reg_cond_life_info);
	      rcli->condition = not_reg_cond (cond);
	      rcli->stores = const0_rtx;
	      rcli->orig_condition = const0_rtx;
	      splay_tree_insert (pbi->reg_cond_dead, i,
				 (splay_tree_value) rcli);

	      SET_REGNO_REG_SET (pbi->reg_cond_reg, REGNO (XEXP (cond, 0)));
	    }
	}
      else if (this_was_live)
	{
	  /* The register may have been conditionally live previously, but
	     is now unconditionally live.  Remove it from the conditionally
	     dead list, so that a conditional set won't cause us to think
	     it dead.  */
	  splay_tree_remove (pbi->reg_cond_dead, i);
	}
#endif
    }
}

/* Scan expression X for registers which have to be marked used in PBI.  
   X is considered to be the SET_DEST rtx of SET.  TRUE is returned if
   X could be handled by this function.  */

static bool
mark_used_dest_regs (struct propagate_block_info *pbi, rtx x, rtx cond, rtx insn)
{
  int regno;
  bool mark_dest = false;
  rtx dest = x;
  
  /* On some platforms calls return values spread over several 
     locations.  These locations are wrapped in a EXPR_LIST rtx
     together with a CONST_INT offset.  */
  if (GET_CODE (x) == EXPR_LIST
      && GET_CODE (XEXP (x, 1)) == CONST_INT)
    x = XEXP (x, 0);
  
  if (x == NULL_RTX)
    return false;

  /* If storing into MEM, don't show it as being used.  But do
     show the address as being used.  */
  if (MEM_P (x))
    {
#ifdef AUTO_INC_DEC
      if (pbi->flags & PROP_AUTOINC)
	find_auto_inc (pbi, x, insn);
#endif
      mark_used_regs (pbi, XEXP (x, 0), cond, insn);
      return true;
    }
	    
  /* Storing in STRICT_LOW_PART is like storing in a reg
     in that this SET might be dead, so ignore it in TESTREG.
     but in some other ways it is like using the reg.
     
     Storing in a SUBREG or a bit field is like storing the entire
     register in that if the register's value is not used
	       then this SET is not needed.  */
  while (GET_CODE (x) == STRICT_LOW_PART
	 || GET_CODE (x) == ZERO_EXTRACT
	 || GET_CODE (x) == SUBREG)
    {
#ifdef CANNOT_CHANGE_MODE_CLASS
      if ((pbi->flags & PROP_REG_INFO) && GET_CODE (x) == SUBREG)
	record_subregs_of_mode (x);
#endif
      
      /* Modifying a single register in an alternate mode
	 does not use any of the old value.  But these other
	 ways of storing in a register do use the old value.  */
      if (GET_CODE (x) == SUBREG
	  && !((REG_BYTES (SUBREG_REG (x))
		+ UNITS_PER_WORD - 1) / UNITS_PER_WORD
	       > (REG_BYTES (x)
		  + UNITS_PER_WORD - 1) / UNITS_PER_WORD))
	;
      else
	mark_dest = true;
      
      x = XEXP (x, 0);
    }
  
  /* If this is a store into a register or group of registers,
     recursively scan the value being stored.  */
  if (REG_P (x)
      && (regno = REGNO (x),
	  !(regno == FRAME_POINTER_REGNUM
	    && (!reload_completed || frame_pointer_needed)))
#if FRAME_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
      && !(regno == HARD_FRAME_POINTER_REGNUM
	   && (!reload_completed || frame_pointer_needed))
#endif
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
      && !(regno == ARG_POINTER_REGNUM && fixed_regs[regno])
#endif
      )
    {
      if (mark_dest)
	mark_used_regs (pbi, dest, cond, insn);
      return true;
    }
  return false;
}

/* Scan expression X and store a 1-bit in NEW_LIVE for each reg it uses.
   This is done assuming the registers needed from X are those that
   have 1-bits in PBI->REG_LIVE.

   INSN is the containing instruction.  If INSN is dead, this function
   is not called.  */

static void
mark_used_regs (struct propagate_block_info *pbi, rtx x, rtx cond, rtx insn)
{
  RTX_CODE code;
  int flags = pbi->flags;

 retry:
  if (!x)
    return;
  code = GET_CODE (x);
  switch (code)
    {
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_INT:
    case CONST:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case PC:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return;

#ifdef HAVE_cc0
    case CC0:
      pbi->cc0_live = 1;
      return;
#endif

    case CLOBBER:
      /* If we are clobbering a MEM, mark any registers inside the address
	 as being used.  */
      if (MEM_P (XEXP (x, 0)))
	mark_used_regs (pbi, XEXP (XEXP (x, 0), 0), cond, insn);
      return;

    case MEM:
      /* Don't bother watching stores to mems if this is not the
	 final pass.  We'll not be deleting dead stores this round.  */
      if (optimize && (flags & PROP_SCAN_DEAD_STORES))
	{
	  /* Invalidate the data for the last MEM stored, but only if MEM is
	     something that can be stored into.  */
	  if (GET_CODE (XEXP (x, 0)) == SYMBOL_REF
	      && CONSTANT_POOL_ADDRESS_P (XEXP (x, 0)))
	    /* Needn't clear the memory set list.  */
	    ;
	  else
	    {
	      rtx temp = pbi->mem_set_list;
	      rtx prev = NULL_RTX;
	      rtx next;

	      while (temp)
		{
		  next = XEXP (temp, 1);
		  if (anti_dependence (XEXP (temp, 0), x))
		    {
		      /* Splice temp out of the list.  */
		      if (prev)
			XEXP (prev, 1) = next;
		      else
			pbi->mem_set_list = next;
		      free_EXPR_LIST_node (temp);
		      pbi->mem_set_list_len--;
		    }
		  else
		    prev = temp;
		  temp = next;
		}
	    }

	  /* If the memory reference had embedded side effects (autoincrement
	     address modes.  Then we may need to kill some entries on the
	     memory set list.  */
	  if (insn)
	    for_each_rtx (&PATTERN (insn), invalidate_mems_from_autoinc, pbi);
	}

#ifdef AUTO_INC_DEC
      if (flags & PROP_AUTOINC)
	find_auto_inc (pbi, x, insn);
#endif
      break;

    case SUBREG:
#ifdef CANNOT_CHANGE_MODE_CLASS
      if (flags & PROP_REG_INFO)
	record_subregs_of_mode (x);
#endif

      /* While we're here, optimize this case.  */
      x = SUBREG_REG (x);
      if (!REG_P (x))
	goto retry;
      /* Fall through.  */

    case REG:
      /* See a register other than being set => mark it as needed.  */
      mark_used_reg (pbi, x, cond, insn);
      return;

    case SET:
      {
	rtx dest = SET_DEST (x);
	int i;
	bool ret = false;

	if (GET_CODE (dest) == PARALLEL)
	  for (i = 0; i < XVECLEN (dest, 0); i++)
	    ret |= mark_used_dest_regs (pbi, XVECEXP (dest, 0, i), cond, insn);
	else
	  ret = mark_used_dest_regs (pbi, dest, cond, insn);
	
	if (ret)
	  {
	    mark_used_regs (pbi, SET_SRC (x), cond, insn);
	    return;
	  }
      }
      break;

    case ASM_OPERANDS:
    case UNSPEC_VOLATILE:
    case TRAP_IF:
    case ASM_INPUT:
      {
	/* Traditional and volatile asm instructions must be considered to use
	   and clobber all hard registers, all pseudo-registers and all of
	   memory.  So must TRAP_IF and UNSPEC_VOLATILE operations.

	   Consider for instance a volatile asm that changes the fpu rounding
	   mode.  An insn should not be moved across this even if it only uses
	   pseudo-regs because it might give an incorrectly rounded result.

	   ?!? Unfortunately, marking all hard registers as live causes massive
	   problems for the register allocator and marking all pseudos as live
	   creates mountains of uninitialized variable warnings.

	   So for now, just clear the memory set list and mark any regs
	   we can find in ASM_OPERANDS as used.  */
	if (code != ASM_OPERANDS || MEM_VOLATILE_P (x))
	  {
	    free_EXPR_LIST_list (&pbi->mem_set_list);
	    pbi->mem_set_list_len = 0;
	  }

	/* For all ASM_OPERANDS, we must traverse the vector of input operands.
	   We can not just fall through here since then we would be confused
	   by the ASM_INPUT rtx inside ASM_OPERANDS, which do not indicate
	   traditional asms unlike their normal usage.  */
	if (code == ASM_OPERANDS)
	  {
	    int j;

	    for (j = 0; j < ASM_OPERANDS_INPUT_LENGTH (x); j++)
	      mark_used_regs (pbi, ASM_OPERANDS_INPUT (x, j), cond, insn);
	  }
	break;
      }

    case COND_EXEC:
      gcc_assert (!cond);

      mark_used_regs (pbi, COND_EXEC_TEST (x), NULL_RTX, insn);

      cond = COND_EXEC_TEST (x);
      x = COND_EXEC_CODE (x);
      goto retry;

    default:
      break;
    }

  /* Recursively scan the operands of this expression.  */

  {
    const char * const fmt = GET_RTX_FORMAT (code);
    int i;

    for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
      {
	if (fmt[i] == 'e')
	  {
	    /* Tail recursive case: save a function call level.  */
	    if (i == 0)
	      {
		x = XEXP (x, 0);
		goto retry;
	      }
	    mark_used_regs (pbi, XEXP (x, i), cond, insn);
	  }
	else if (fmt[i] == 'E')
	  {
	    int j;
	    for (j = 0; j < XVECLEN (x, i); j++)
	      mark_used_regs (pbi, XVECEXP (x, i, j), cond, insn);
	  }
      }
  }
}

#ifdef AUTO_INC_DEC

static int
try_pre_increment_1 (struct propagate_block_info *pbi, rtx insn)
{
  /* Find the next use of this reg.  If in same basic block,
     make it do pre-increment or pre-decrement if appropriate.  */
  rtx x = single_set (insn);
  HOST_WIDE_INT amount = ((GET_CODE (SET_SRC (x)) == PLUS ? 1 : -1)
			  * INTVAL (XEXP (SET_SRC (x), 1)));
  int regno = REGNO (SET_DEST (x));
  rtx y = pbi->reg_next_use[regno];
  if (y != 0
      && SET_DEST (x) != stack_pointer_rtx
      && BLOCK_NUM (y) == BLOCK_NUM (insn)
      /* Don't do this if the reg dies, or gets set in y; a standard addressing
	 mode would be better.  */
      && ! dead_or_set_p (y, SET_DEST (x))
      && try_pre_increment (y, SET_DEST (x), amount))
    {
      /* We have found a suitable auto-increment and already changed
	 insn Y to do it.  So flush this increment instruction.  */
      propagate_block_delete_insn (insn);

      /* Count a reference to this reg for the increment insn we are
	 deleting.  When a reg is incremented, spilling it is worse,
	 so we want to make that less likely.  */
      if (regno >= FIRST_PSEUDO_REGISTER)
	{
	  REG_FREQ (regno) += REG_FREQ_FROM_BB (pbi->bb);
	  REG_N_SETS (regno)++;
	}

      /* Flush any remembered memories depending on the value of
	 the incremented register.  */
      invalidate_mems_from_set (pbi, SET_DEST (x));

      return 1;
    }
  return 0;
}

/* Try to change INSN so that it does pre-increment or pre-decrement
   addressing on register REG in order to add AMOUNT to REG.
   AMOUNT is negative for pre-decrement.
   Returns 1 if the change could be made.
   This checks all about the validity of the result of modifying INSN.  */

static int
try_pre_increment (rtx insn, rtx reg, HOST_WIDE_INT amount)
{
  rtx use;

  /* Nonzero if we can try to make a pre-increment or pre-decrement.
     For example, addl $4,r1; movl (r1),... can become movl +(r1),...  */
  int pre_ok = 0;
  /* Nonzero if we can try to make a post-increment or post-decrement.
     For example, addl $4,r1; movl -4(r1),... can become movl (r1)+,...
     It is possible for both PRE_OK and POST_OK to be nonzero if the machine
     supports both pre-inc and post-inc, or both pre-dec and post-dec.  */
  int post_ok = 0;

  /* Nonzero if the opportunity actually requires post-inc or post-dec.  */
  int do_post = 0;

  /* From the sign of increment, see which possibilities are conceivable
     on this target machine.  */
  if (HAVE_PRE_INCREMENT && amount > 0)
    pre_ok = 1;
  if (HAVE_POST_INCREMENT && amount > 0)
    post_ok = 1;

  if (HAVE_PRE_DECREMENT && amount < 0)
    pre_ok = 1;
  if (HAVE_POST_DECREMENT && amount < 0)
    post_ok = 1;

  if (! (pre_ok || post_ok))
    return 0;

  /* It is not safe to add a side effect to a jump insn
     because if the incremented register is spilled and must be reloaded
     there would be no way to store the incremented value back in memory.  */

  if (JUMP_P (insn))
    return 0;

  use = 0;
  if (pre_ok)
    use = find_use_as_address (PATTERN (insn), reg, 0);
  if (post_ok && (use == 0 || use == (rtx) (size_t) 1))
    {
      use = find_use_as_address (PATTERN (insn), reg, -amount);
      do_post = 1;
    }

  if (use == 0 || use == (rtx) (size_t) 1)
    return 0;

  if (GET_MODE_SIZE (GET_MODE (use)) != (amount > 0 ? amount : - amount))
    return 0;

  /* See if this combination of instruction and addressing mode exists.  */
  if (! validate_change (insn, &XEXP (use, 0),
			 gen_rtx_fmt_e (amount > 0
					? (do_post ? POST_INC : PRE_INC)
					: (do_post ? POST_DEC : PRE_DEC),
					Pmode, reg), 0))
    return 0;

  /* Record that this insn now has an implicit side effect on X.  */
  REG_NOTES (insn) = alloc_EXPR_LIST (REG_INC, reg, REG_NOTES (insn));
  return 1;
}

#endif /* AUTO_INC_DEC */

/* Find the place in the rtx X where REG is used as a memory address.
   Return the MEM rtx that so uses it.
   If PLUSCONST is nonzero, search instead for a memory address equivalent to
   (plus REG (const_int PLUSCONST)).

   If such an address does not appear, return 0.
   If REG appears more than once, or is used other than in such an address,
   return (rtx) 1.  */

rtx
find_use_as_address (rtx x, rtx reg, HOST_WIDE_INT plusconst)
{
  enum rtx_code code = GET_CODE (x);
  const char * const fmt = GET_RTX_FORMAT (code);
  int i;
  rtx value = 0;
  rtx tem;

  if (code == MEM && XEXP (x, 0) == reg && plusconst == 0)
    return x;

  if (code == MEM && GET_CODE (XEXP (x, 0)) == PLUS
      && XEXP (XEXP (x, 0), 0) == reg
      && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
      && INTVAL (XEXP (XEXP (x, 0), 1)) == plusconst)
    return x;

  if (code == SIGN_EXTRACT || code == ZERO_EXTRACT)
    {
      /* If REG occurs inside a MEM used in a bit-field reference,
	 that is unacceptable.  */
      if (find_use_as_address (XEXP (x, 0), reg, 0) != 0)
	return (rtx) (size_t) 1;
    }

  if (x == reg)
    return (rtx) (size_t) 1;

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  tem = find_use_as_address (XEXP (x, i), reg, plusconst);
	  if (value == 0)
	    value = tem;
	  else if (tem != 0)
	    return (rtx) (size_t) 1;
	}
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    {
	      tem = find_use_as_address (XVECEXP (x, i, j), reg, plusconst);
	      if (value == 0)
		value = tem;
	      else if (tem != 0)
		return (rtx) (size_t) 1;
	    }
	}
    }

  return value;
}

/* Write information about registers and basic blocks into FILE.
   This is part of making a debugging dump.  */

void
dump_regset (regset r, FILE *outf)
{
  unsigned i;
  reg_set_iterator rsi;

  if (r == NULL)
    {
      fputs (" (nil)", outf);
      return;
    }

  EXECUTE_IF_SET_IN_REG_SET (r, 0, i, rsi)
    {
      fprintf (outf, " %d", i);
      if (i < FIRST_PSEUDO_REGISTER)
	fprintf (outf, " [%s]",
		 reg_names[i]);
    }
}

/* Print a human-readable representation of R on the standard error
   stream.  This function is designed to be used from within the
   debugger.  */

void
debug_regset (regset r)
{
  dump_regset (r, stderr);
  putc ('\n', stderr);
}

/* Recompute register set/reference counts immediately prior to register
   allocation.

   This avoids problems with set/reference counts changing to/from values
   which have special meanings to the register allocators.

   Additionally, the reference counts are the primary component used by the
   register allocators to prioritize pseudos for allocation to hard regs.
   More accurate reference counts generally lead to better register allocation.

   It might be worthwhile to update REG_LIVE_LENGTH, REG_BASIC_BLOCK and
   possibly other information which is used by the register allocators.  */

static unsigned int
recompute_reg_usage (void)
{
  allocate_reg_life_data ();
  /* distribute_notes in combiner fails to convert some of the
     REG_UNUSED notes to REG_DEAD notes.  This causes CHECK_DEAD_NOTES
     in sched1 to die.  To solve this update the DEATH_NOTES
     here.  */
  update_life_info (NULL, UPDATE_LIFE_LOCAL, PROP_REG_INFO | PROP_DEATH_NOTES);

  if (dump_file)
    dump_flow_info (dump_file, dump_flags);
  return 0;
}

struct tree_opt_pass pass_recompute_reg_usage =
{
  "life2",                              /* name */
  NULL,                                 /* gate */
  recompute_reg_usage,                  /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  'f'                                   /* letter */
};

/* Optionally removes all the REG_DEAD and REG_UNUSED notes from a set of
   blocks.  If BLOCKS is NULL, assume the universal set.  Returns a count
   of the number of registers that died.
   If KILL is 1, remove old REG_DEAD / REG_UNUSED notes.  If it is 0, don't.
   if it is -1, remove them unless they pertain to a stack reg.  */

int
count_or_remove_death_notes (sbitmap blocks, int kill)
{
  int count = 0;
  unsigned int i = 0;
  basic_block bb;

  /* This used to be a loop over all the blocks with a membership test
     inside the loop.  That can be amazingly expensive on a large CFG
     when only a small number of bits are set in BLOCKs (for example,
     the calls from the scheduler typically have very few bits set).

     For extra credit, someone should convert BLOCKS to a bitmap rather
     than an sbitmap.  */
  if (blocks)
    {
      sbitmap_iterator sbi;

      EXECUTE_IF_SET_IN_SBITMAP (blocks, 0, i, sbi)
	{
	  basic_block bb = BASIC_BLOCK (i);
	  /* The bitmap may be flawed in that one of the basic blocks
	     may have been deleted before you get here.  */
	  if (bb)
	    count += count_or_remove_death_notes_bb (bb, kill);
	};
    }
  else
    {
      FOR_EACH_BB (bb)
	{
	  count += count_or_remove_death_notes_bb (bb, kill);
	}
    }

  return count;
}
  
/* Optionally removes all the REG_DEAD and REG_UNUSED notes from basic
   block BB.  Returns a count of the number of registers that died.  */

static int
count_or_remove_death_notes_bb (basic_block bb, int kill)
{
  int count = 0;
  rtx insn;

  for (insn = BB_HEAD (bb); ; insn = NEXT_INSN (insn))
    {
      if (INSN_P (insn))
	{
	  rtx *pprev = &REG_NOTES (insn);
	  rtx link = *pprev;

	  while (link)
	    {
	      switch (REG_NOTE_KIND (link))
		{
		case REG_DEAD:
		  if (REG_P (XEXP (link, 0)))
		    {
		      rtx reg = XEXP (link, 0);
		      int n;

		      if (REGNO (reg) >= FIRST_PSEUDO_REGISTER)
		        n = 1;
		      else
		        n = hard_regno_nregs[REGNO (reg)][GET_MODE (reg)];
		      count += n;
		    }

		  /* Fall through.  */

		case REG_UNUSED:
		  if (kill > 0
		      || (kill
#ifdef STACK_REGS
			  && (!REG_P (XEXP (link, 0))
			      || !IN_RANGE (REGNO (XEXP (link, 0)),
					    FIRST_STACK_REG, LAST_STACK_REG))
#endif
			  ))
		    {
		      rtx next = XEXP (link, 1);
		      free_EXPR_LIST_node (link);
		      *pprev = link = next;
		      break;
		    }
		  /* Fall through.  */

		default:
		  pprev = &XEXP (link, 1);
		  link = *pprev;
		  break;
		}
	    }
	}

      if (insn == BB_END (bb))
	break;
    }

  return count;
}

/* Clear LOG_LINKS fields of insns in a selected blocks or whole chain
   if blocks is NULL.  */

static void
clear_log_links (sbitmap blocks)
{
  rtx insn;

  if (!blocks)
    {
      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	if (INSN_P (insn))
	  free_INSN_LIST_list (&LOG_LINKS (insn));
    }
  else
    {
      unsigned int i = 0;
      sbitmap_iterator sbi;

      EXECUTE_IF_SET_IN_SBITMAP (blocks, 0, i, sbi)
	{
	  basic_block bb = BASIC_BLOCK (i);

	  for (insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
	       insn = NEXT_INSN (insn))
	    if (INSN_P (insn))
	      free_INSN_LIST_list (&LOG_LINKS (insn));
	}
    }
}

/* Given a register bitmap, turn on the bits in a HARD_REG_SET that
   correspond to the hard registers, if any, set in that map.  This
   could be done far more efficiently by having all sorts of special-cases
   with moving single words, but probably isn't worth the trouble.  */

void
reg_set_to_hard_reg_set (HARD_REG_SET *to, bitmap from)
{
  unsigned i;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (from, 0, i, bi)
    {
      if (i >= FIRST_PSEUDO_REGISTER)
	return;
      SET_HARD_REG_BIT (*to, i);
    }
}


static bool
gate_remove_death_notes (void)
{
  return flag_profile_values;
}

static unsigned int
rest_of_handle_remove_death_notes (void)
{
  count_or_remove_death_notes (NULL, 1);
  return 0;
}

struct tree_opt_pass pass_remove_death_notes =
{
  "ednotes",                            /* name */
  gate_remove_death_notes,              /* gate */
  rest_of_handle_remove_death_notes,    /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0                                     /* letter */
};

/* Perform life analysis.  */
static unsigned int
rest_of_handle_life (void)
{
  regclass_init ();

  life_analysis (PROP_FINAL);
  if (optimize)
    cleanup_cfg (CLEANUP_EXPENSIVE | CLEANUP_UPDATE_LIFE | CLEANUP_LOG_LINKS
                 | (flag_thread_jumps ? CLEANUP_THREADING : 0));

  if (extra_warnings)
    {
      setjmp_vars_warning (DECL_INITIAL (current_function_decl));
      setjmp_args_warning ();
    }

  if (optimize)
    {
      if (initialize_uninitialized_subregs ())
        {
          /* Insns were inserted, and possibly pseudos created, so
             things might look a bit different.  */
          allocate_reg_life_data ();
          update_life_info (NULL, UPDATE_LIFE_GLOBAL_RM_NOTES,
                            PROP_LOG_LINKS | PROP_REG_INFO | PROP_DEATH_NOTES);
        }
    }

  no_new_pseudos = 1;
  return 0;
}

struct tree_opt_pass pass_life =
{
  "life1",                              /* name */
  NULL,                                 /* gate */
  rest_of_handle_life,                  /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_FLOW,                              /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  TODO_verify_flow,                     /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'f'                                   /* letter */
};

static unsigned int
rest_of_handle_flow2 (void)
{
  /* If optimizing, then go ahead and split insns now.  */
#ifndef STACK_REGS
  if (optimize > 0)
#endif
    split_all_insns (0);

  if (flag_branch_target_load_optimize)
    branch_target_load_optimize (epilogue_completed);

  if (optimize)
    cleanup_cfg (CLEANUP_EXPENSIVE);

  /* On some machines, the prologue and epilogue code, or parts thereof,
     can be represented as RTL.  Doing so lets us schedule insns between
     it and the rest of the code and also allows delayed branch
     scheduling to operate in the epilogue.  */
  thread_prologue_and_epilogue_insns (get_insns ());
  epilogue_completed = 1;
  flow2_completed = 1;
  return 0;
}

struct tree_opt_pass pass_flow2 =
{
  "flow2",                              /* name */
  NULL,                                 /* gate */
  rest_of_handle_flow2,                 /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_FLOW2,                             /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  TODO_verify_flow,                     /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect,                     /* todo_flags_finish */
  'w'                                   /* letter */
};

